// Cover (AI_SPEC 5), defense slots (AI_SPEC 6.6) and building annotations (AI_SPEC 8.1).
//   generation: wall edges of each navmesh tile are sampled every 1.5 m; rays at knee / waist / head height toward
//     the wall classify low cover (crouch, 0.8-1.4 m) and high cover (standing), with peek left / right / over.
//     Craters (terrain bowls) are natural low cover all round. Assets add hand-authored nav.cover points.
//   scoring: protection from known threats (raycast threat -> chest / head), progress toward the goal, route
//     exposure to known threats, spacing to teammates (8 m), reservations, and a firing position on the threats.
#include "ai_core.h"
#include "app.h"
#include <algorithm>
#include <cmath>

namespace { vec3 xz(vec3 v) { return vec3(v.x, 0, v.z); } float lenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); } }

static void addCoverPoint(AiCore& A, const CoverPoint& c) {
  int idx = (int)A.cover.size();
  A.cover.push_back(c);
  A.cover.back().serial = A.coverSerial++;
  A.coverGrid[coverKey(c.p.x, c.p.z)].push_back(idx);
}
static bool coverNear(const AiCore& A, vec3 p, float r) {
  auto it = A.coverGrid.find(coverKey(p.x, p.z));
  if (it == A.coverGrid.end()) return false;
  for (int i : it->second) if (A.cover[i].serial && glm::length(A.cover[i].p - p) < r) return true;
  return false;
}

void AiCore::generateCoverTile(int tx, int tz, int maxRays) {
  int rays = 0;
  for (auto& e : G.nav.tileEdges(tx, tz)) {
    if (navAreaCost(navarea::toNav(e.area)) < 0) continue;
    { NavArea na = navarea::toNav(e.area); if (na == NavArea::Water || na == NavArea::Marsh) continue; }   // nobody takes cover lying in a ditch's water or a marsh (W4 puts the ditch's cover on its lip)
    if (G.nav.isTrap((e.a + e.b) * 0.5f, 0.3f)) continue;   // no cover in dead-end pockets (a ditch floor reached by a drop)
    vec3 d = e.b - e.a; float L = lenXZ(d);
    int n = std::max(1, (int)std::lround(L / 1.5f));
    vec3 right(-e.n.z, 0, e.n.x);
    for (int k = 0; k < n; ++k) {
      if (rays > maxRays) { coverTodo.push_back({tx, tz}); return; }   // continue next step (tombstones keep it idempotent)
      vec3 P = glm::mix(e.a, e.b, (k + 0.5f) / n) - e.n * 0.05f;
      if (coverNear(*this, P, 1.2f)) continue;
      Physics::Hit h;
      const float reach = G.nav.cfg.radius + 0.6f;
      rays += 1;
      if (!ray(P + vec3(0, 0.5f, 0), e.n, reach, &h)) continue;               // a ledge, not an obstacle
      rays += 2;
      bool waist = ray(P + vec3(0, 1.0f, 0), e.n, reach), head = ray(P + vec3(0, 1.55f, 0), e.n, reach);
      if (!waist) continue;
      CoverPoint c; c.p = P; c.n = e.n; c.tx = tx; c.tz = tz; c.high = head; c.peekOver = !head;
      if (head) { rays += 2; c.peekL = !ray(P - right * 0.75f + vec3(0, 1.5f, 0), e.n, reach + 0.3f); c.peekR = !ray(P + right * 0.75f + vec3(0, 1.5f, 0), e.n, reach + 0.3f); }
      if (c.high && !c.peekL && !c.peekR) { c.peekL = false; }   // a wall without corners: still cover, no firing position
      addCoverPoint(*this, c);
    }
  }
  // craters / bowls in the terrain (natural low cover all round)
  const float T = G.nav.cfg.tile;
  for (float z = tz * T + 2; z < (tz + 1) * T; z += 4) for (float x = tx * T + 2; x < (tx + 1) * T; x += 4) {
    float h0 = G.app.terrain.height(x, z), ring = 0;
    for (int k = 0; k < 8; ++k) { float a = k * 0.785398f; ring += G.app.terrain.height(x + std::cos(a) * 2.2f, z + std::sin(a) * 2.2f); }
    if (ring / 8 - h0 < 0.45f) continue;
    vec3 p(x, h0, z);
    if (G.nav.nearest(p, 0.8f) < 0 || coverNear(*this, p, 2.0f) || G.nav.isTrap(p)) continue;
    CoverPoint c; c.p = p; c.n = vec3(0); c.natural = true; c.peekOver = true; c.tx = tx; c.tz = tz;
    addCoverPoint(*this, c);
  }
}

void AiCore::addAuthoredCover() {
  for (auto& in : G.app.doc.instances) {
    auto it = G.app.rt.find(in.id);
    if (it == G.app.rt.end() || !it->second.asset) continue;
    quat q = yawQuat(in.yaw);
    for (auto& cp : it->second.asset->navMeta.cover) {
      CoverPoint c; c.p = in.pos + q * cp.p; c.n = q * App::forward(cp.yaw, 0) * -1.0f; c.n.y = 0;
      if (glm::length(c.n) > 0.1f) c.n = glm::normalize(c.n);
      c.high = cp.high; c.peekL = cp.peekLeft; c.peekR = cp.peekRight; c.peekOver = cp.peekOver; c.authored = true;
      // authored points are placed against the face; the mesh is eroded by the agent radius: stand on the mesh, or drop it
      { NavRef r; vec3 on; if (!G.nav.closest(c.p, vec3(1.0f, 1.2f, 1.0f), r, on) || lenXZ(on - c.p) > 0.9f || std::fabs(on.y - c.p.y) > 1.0f || G.nav.isTrap(on, 0.3f)) continue; c.p = on; }
      if (coverNear(*this, c.p, tj("cover", "authoredMinGap_m", 1.0f))) continue;   // one point per spot (generated wall cover or a neighbour already there)
      c.tx = G.nav.tileOf(c.p.x); c.tz = G.nav.tileOf(c.p.z);
      addCoverPoint(*this, c);
    }
  }
}

bool AiCore::coverProtects(const CoverPoint& c, vec3 threat) {
  if (c.natural) return true;
  vec3 d = xz(threat - c.p); float L = glm::length(d);
  if (L < 1.5f) return false;
  return glm::dot(d / L, c.n) > 0.35f;
}

void AiCore::releaseCover(Soldier& s) {
  BotMind& m = mind(s);
  if (m.cover >= 0 && m.cover < (int)cover.size() && cover[m.cover].heldBy == s.slot) cover[m.cover].heldBy = -1;
  m.cover = -1; m.set = false;
}

int AiCore::pickCover(Soldier& s, vec3 goal, float maxDist, float minProgress, bool reactive, vec3 fireFrom) {
  BotMind& m = mind(s);
  TeamBrain& tb = teams[(int)s.role];
  std::vector<vec3> threats;
  for (auto& [e, k] : tb.known) if (k.conf > 0.3f && (G.t - k.seen < diff.shareDelay + 8 || k.by == s.slot)) threats.push_back(k.pos + vec3(0, 1.5f, 0));
  if (reactive && glm::length(fireFrom) > 0) threats.push_back(fireFrom + vec3(0, 1.5f, 0));
  float d0 = lenXZ(goal - s.feet);
  struct C { int i; float sc; float prot; };
  std::vector<C> cand;
  int R = (int)std::ceil(maxDist / 8.0f);
  int64_t cx = (int64_t)std::floor(s.feet.x / 8), cz = (int64_t)std::floor(s.feet.z / 8);
  for (int64_t dz = -R; dz <= R; ++dz) for (int64_t dx = -R; dx <= R; ++dx) {
    auto it = coverGrid.find(((cx + dx) << 32) ^ (int64_t)(uint32_t)(int)(cz + dz));
    if (it == coverGrid.end()) continue;
    for (int i : it->second) {
      CoverPoint& c = cover[i];
      if (!c.serial) continue;
      float dist = glm::length(c.p - s.feet);
      if (dist > maxDist || dist < 1.0f || std::fabs(c.p.y - s.feet.y) > 4) continue;
      if (c.heldBy >= 0 && c.heldBy != s.slot && c.heldUntil > G.t && c.heldBy < (int)G.sold.size() && G.sold[c.heldBy].alive) continue;   // reserved
      if (i == m.cover && m.needNewGoal) continue;
      float progress = (d0 - lenXZ(goal - c.p)) / std::max(5.0f, maxDist);
      if (progress < minProgress) continue;
      // cheap protection estimate: faces the threats
      float prot = 0; for (auto& t : threats) prot += coverProtects(c, t) ? 1.0f : 0.0f;
      prot = threats.empty() ? 0.5f : prot / threats.size();
      if (reactive && prot <= 0) continue;
      // route exposure: straight line samples visible to known threats
      float expo = 0; int ns = std::max(1, (int)(dist / 3));
      for (int k = 1; k <= ns; ++k) expo += exposure((int)s.role, glm::mix(s.feet, c.p, k / (float)ns) + vec3(0, 1.2f, 0));
      expo = expo / ns * std::min(1.0f, dist / 15.0f);
      // spacing: teammates (and their reserved cover) within 8 m
      float sp = 0; const float spacing = tj("cover", "spacing_m", 8.0f), spW = tj("cover", "spacingWeight", 1.1f), occ = tj("cover", "occupied_m", 2.5f);
      bool taken = false;   // a teammate already standing there (a Tobruk pit, a trench bay holds one or two)
      for (auto& o : G.sold) if (&o != &s && o.alive && o.role == s.role && glm::length(o.feet - c.p) < occ) { taken = true; break; }
      if (taken) continue;
      for (auto& o : G.sold) {
        if (&o == &s || !o.alive || o.role != s.role) continue;
        float dd = glm::length(o.feet - c.p);
        if (o.slot < (int)minds.size() && minds[o.slot] && minds[o.slot]->cover >= 0 && minds[o.slot]->cover < (int)cover.size()) dd = std::min(dd, glm::length(cover[minds[o.slot]->cover].p - c.p));
        if (dd < spacing) sp += 1.0f - dd / spacing;
      }
      float sc = 2.0f * prot + 1.6f * progress - 1.4f * expo - spW * sp - (reactive ? tj("cover", "reactDistWeight", 0.1f) : 0.015f) * dist + (c.high ? 0.1f : 0.0f) + (c.authored ? 0.2f : 0.0f);
      cand.push_back({i, sc, prot});
    }
  }
  if (cand.empty()) return -1;
  std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.sc > b.sc; });
  // the best few: real protection (threat -> chest / head rays) and a firing position
  int best = -1; float bs = -1e9f;
  for (size_t k = 0; k < cand.size() && k < 6; ++k) {
    CoverPoint& c = cover[cand[k].i];
    float sc = cand[k].sc;
    if (!threats.empty() && rayFree()) {
      vec3 t = threats.front();
      vec3 chest = c.p + vec3(0, c.high ? 1.3f : 0.8f, 0);
      bool blocked = ray(t, chest - t, glm::length(chest - t));
      sc += blocked ? 1.0f : -1.5f;
      vec3 peek = c.p + vec3(0, 1.5f, 0) + (c.high ? vec3(-c.n.z, 0, c.n.x) * (c.peekR ? 0.7f : c.peekL ? -0.7f : 0.0f) : vec3(0));
      bool canFire = !ray(peek, t - peek, glm::length(t - peek) - 0.5f);
      sc += canFire ? 0.8f : 0.0f;
    }
    if (sc > bs) { bs = sc; best = cand[k].i; }
  }
  if (best >= 0) {
    releaseCover(s);
    CoverPoint& c = cover[best];
    c.heldBy = s.slot; c.heldUntil = G.t + 25;
    m.cover = best; m.coverSerial = c.serial; m.coverPos = c.p; m.coverHigh = c.high; m.set = false; m.needNewGoal = false;
    s.br.coverPos = c.p; s.br.hasCover = true;
  }
  return best;
}

// ---------------------------------------------------------------- defense slots
void AiCore::buildDefSlots() {
  std::vector<DefSlot> old; if (defStage == G.match.active) old = defSlots;   // same objective: keep who holds what
  defSlots.clear(); defStage = G.match.active; lastSlotRebuild = G.t;
  const Objective* o = G.match.activeObjective();
  if (!o || !G.nav.valid()) return;
  auto add = [&](vec3 p, float yaw, int pri, int kind, const char* src) {
    NavRef r; vec3 on;
    if (!G.nav.closest(p, 1.5f, r, on)) return;
    for (auto& d : defSlots) if (glm::length(d.p - on) < 2.5f) return;
    DefSlot d; d.p = on; d.yaw = yaw; d.priority = pri; d.kind = kind; d.src = src; defSlots.push_back(d);
  };
  for (auto& ds : o->defenseSlots) add(ds.pos, ds.yaw, 2 + ds.priority, (int)ds.kind, "scenario");
  for (uint64_t id : o->linkedStructures) {
    auto it = G.app.rt.find(id); const Instance* in = G.app.doc.find(id);
    if (it == G.app.rt.end() || !in || !it->second.asset) continue;
    quat q = yawQuat(in->yaw);
    for (auto& fp : it->second.asset->navMeta.firingPoints) add(in->pos + q * fp.p, in->yaw + fp.yaw, 3, fp.kind == "mg" ? (int)SlotKind::MgNest : fp.kind == "embrasure" ? (int)SlotKind::Embrasure : (int)SlotKind::Window, "linked");
  }
  for (auto& b : buildings) for (auto& fp : b.firing) if (o->contains(fp.p) || glm::length(vec2(fp.p.x - o->pos.x, fp.p.z - o->pos.z)) < o->extent() + 5) add(fp.p, fp.yaw, 2, (int)SlotKind::Window, "building");
  vec3 threat = o->pos - attackForward(o->attackYaw) * 40.0f;
  std::vector<std::pair<float, int>> cc;
  for (size_t i = 0; i < cover.size(); ++i) { const CoverPoint& c = cover[i]; if (!c.serial || !o->contains(c.p)) continue; if (coverProtects(c, threat)) cc.push_back({(c.high ? 0.5f : 1.0f) + (c.peekL || c.peekR || c.peekOver ? 1.0f : 0.0f), (int)i}); }
  std::sort(cc.begin(), cc.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (auto& [sc, i] : cc) { if (defSlots.size() >= 40) break; add(cover[i].p, wrapDeg(degrees(std::atan2(cover[i].n.x, cover[i].n.z)) + 180), 1, 0, "cover"); }
  // fallback ring inside the capture area
  for (int k = 0; k < 16 && defSlots.size() < 12; ++k) { float a = k * 0.3927f; float r = o->extent() * (k % 2 ? 0.45f : 0.75f); add(o->pos + vec3(std::cos(a) * r, 0, std::sin(a) * r), o->attackYaw + 180, 0, 0, "ring"); }
  for (auto& s : G.sold) if (s.slot < (int)minds.size() && minds[s.slot]) minds[s.slot]->slot = -1;
  for (auto& o : old) {
    if (o.heldBy < 0 || o.heldBy >= (int)G.sold.size() || !G.sold[o.heldBy].alive || o.heldBy >= (int)minds.size() || !minds[o.heldBy]) continue;
    int best = -1; float bd = 2.5f;
    for (size_t k = 0; k < defSlots.size(); ++k) if (defSlots[k].heldBy < 0) { float d = glm::length(defSlots[k].p - o.p); if (d < bd) { bd = d; best = (int)k; } }
    if (best >= 0) { defSlots[best].heldBy = o.heldBy; minds[o.heldBy]->slot = best; }
  }
  logf("ai: %zu defense slots for objective %c (%zu authored, %zu linked structures)", defSlots.size(), o->letter(), o->defenseSlots.size(), o->linkedStructures.size());
}

int AiCore::pickDefSlot(Soldier& s) {
  BotMind& m = mind(s);
  if (defStage != G.match.active) buildDefSlots();
  int best = -1; float bs = -1e9f;
  for (size_t k = 0; k < defSlots.size(); ++k) {
    DefSlot& d = defSlots[k];
    if (d.heldBy >= 0 && d.heldBy != s.slot && d.heldBy < (int)G.sold.size() && G.sold[d.heldBy].alive) continue;
    float sc = d.priority * 2.0f - glm::length(d.p - s.feet) * 0.02f;
    for (auto& o : defSlots) if (&o != &d && o.heldBy >= 0 && o.heldBy != s.slot && glm::length(o.p - d.p) < 5.0f) sc -= 2.5f;   // spread out
    if (G.t - d.suppressedAt < 10) sc -= 4.0f;
    if ((int)k == m.slot && (G.t - s.br.hurtAt < 4 || m.needNewGoal)) sc -= 6.0f;   // just got hit / flanked there: shift
    if (sc > bs) { bs = sc; best = (int)k; }
  }
  for (auto& d : defSlots) if (d.heldBy == s.slot) d.heldBy = -1;
  if (best >= 0) { defSlots[best].heldBy = s.slot; m.slot = best; s.br.holdSpot = best; s.br.holdPos = defSlots[best].p; }
  return best;
}

// ---------------------------------------------------------------- buildings and bunkers
int AiBuilding::roomAt(vec3 p) const {
  for (size_t k = 0; k < rooms.size(); ++k) { const Room& r = rooms[k]; if (p.x >= r.mn.x - 0.3f && p.x <= r.mx.x + 0.3f && p.z >= r.mn.z - 0.3f && p.z <= r.mx.z + 0.3f && p.y >= r.mn.y - 1.0f && p.y <= r.mx.y + 0.5f) return (int)k; }
  return -1;
}

void AiCore::scanBuildings() {
  buildings.clear();
  for (auto& in : G.app.doc.instances) {
    auto it = G.app.rt.find(in.id);
    if (it == G.app.rt.end() || !it->second.asset) continue;
    const AssetDef& a = *it->second.asset;
    bool bunker = a.id.rfind("bunker", 0) == 0 || a.category.find("unker") != std::string::npos;
    if (!a.enterable && !bunker && !a.navMeta.hasAnnotations()) continue;
    AiBuilding b; b.inst = in.id; b.asset = a.id; b.bunker = bunker;
    quat q = yawQuat(in.yaw);
    auto W = [&](vec3 l) { return in.pos + q * l; };
    vec3 lo(1e9f), hi(-1e9f);
    for (int c = 0; c < 8; ++c) { vec3 w = W(vec3((c & 1) ? a.bmax.x : a.bmin.x, (c & 2) ? a.bmax.y : a.bmin.y, (c & 4) ? a.bmax.z : a.bmin.z)); lo = glm::min(lo, w); hi = glm::max(hi, w); }
    b.bmin = lo; b.bmax = hi;
    const NavMeta& m = a.navMeta;
    if (!m.rooms.empty()) {
      for (auto& r : m.rooms) { vec3 p0 = W(r.min), p1 = W(r.max); b.rooms.push_back({r.id, glm::min(p0, p1), glm::max(p0, p1)}); }
      for (auto& e : m.entrances) b.entrances.push_back({W(e.p), q * e.dir, e.room});
      for (auto& c : m.corners) b.corners.push_back({W(c.p), c.room});
      for (auto& d : m.doors) b.doors.push_back({d.a, d.b, W(d.p)});
    } else {
      // generated: one room (the interior box), entrances at the door openings, corners inset from the box
      b.generated = true;
      vec3 mn = a.bmin + vec3(0.4f, 0, 0.4f), mx = a.bmax - vec3(0.4f, 0, 0.4f); mx.y = std::min(mx.y, a.bmin.y + 3.0f);
      b.rooms.push_back({"r1", glm::min(W(mn), W(mx)), glm::max(W(mn), W(mx))});
      vec3 cen = (a.bmin + a.bmax) * 0.5f;
      for (auto& d : a.interactables) {
        if (d.kind != "door" && d.kind != "hatch" && d.kind != "gate") continue;
        vec3 p = (d.apMin + d.apMax) * 0.5f; p.y = a.bmin.y + 0.05f;
        vec3 dir = cen - p; dir.y = 0; if (glm::length(dir) < 0.1f) continue;
        b.entrances.push_back({W(p), q * glm::normalize(dir), "r1"});
        b.doors.push_back({"r1", "outside", W(p)});
      }
      // corners: the extreme interior points that are really on the mesh (thick bunker walls, inner doors)
      std::vector<vec3> inside;
      for (float z = mn.z; z <= mx.z; z += 0.8f) for (float x = mn.x; x <= mx.x; x += 0.8f) {
        vec3 w = W(vec3(x, a.bmin.y + 0.3f, z)); NavRef r; vec3 on;
        if (!G.nav.closest(w, vec3(0.35f, 1.2f, 0.35f), r, on)) continue;
        vec3 l = glm::inverse(q) * (on - in.pos);
        if (l.x < mn.x - 0.1f || l.x > mx.x + 0.1f || l.z < mn.z - 0.1f || l.z > mx.z + 0.1f || on.y > in.pos.y + a.bmin.y + 2.5f) continue;
        inside.push_back(on);
      }
      if (inside.size() < 3) { b.entrances.clear(); b.doors.clear(); }   // no walkable interior on the mesh: not clearable
      else {
        for (vec2 sgn : {vec2(1, 1), vec2(1, -1), vec2(-1, 1), vec2(-1, -1)}) {
          const vec3* best = nullptr; float bs = -1e9f;
          for (auto& p : inside) { vec3 l = glm::inverse(q) * (p - in.pos); float sc = l.x * sgn.x + l.z * sgn.y; if (sc > bs) { bs = sc; best = &p; } }
          bool dup = false; for (auto& c : b.corners) if (glm::length(c.p - *best) < 0.8f) dup = true;
          if (!dup) b.corners.push_back({*best, "r1"});
        }
      }
      for (auto& d : a.interactables) if (d.kind == "window") { vec3 p = (d.apMin + d.apMax) * 0.5f; vec3 o = p - cen; o.y = 0; float yaw = degrees(std::atan2(-o.x, -o.z)); b.firing.push_back({W(p - glm::normalize(o + vec3(1e-4f)) * 0.6f) - vec3(0, 1.0f, 0), wrapDeg(in.yaw + yaw + 180), "window"}); }
    }
    for (auto& fp : m.firingPoints) b.firing.push_back({W(fp.p), wrapDeg(in.yaw + fp.yaw), fp.kind});
    // entrances must be reachable on the mesh: snap them
    for (auto& e : b.entrances) { NavRef r; vec3 on; if (G.nav.closest(e.p, 1.5f, r, on)) e.p = on; }
    if (!b.generated) for (auto& c : b.corners) { NavRef r; vec3 on; if (G.nav.closest(c.p, 1.2f, r, on)) c.p = on; }
    if (!b.entrances.empty() || !b.firing.empty()) buildings.push_back(b);
  }
  logf("ai: %zu buildings / bunkers with clearing annotations", buildings.size());
}
