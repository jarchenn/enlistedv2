// Bot AI: one utility-driven state machine per bot with shared, staggered services.
//   perception (5 Hz, staggered): field of view, line of sight (walls / closed doors block, glass does not), smoke
//     opacity integrated along the sight line, hearing (shots / blasts / landings, approximate positions), detection
//     build-up with distance and movement, decaying last-known positions. Unseen enemies are never tracked: memory
//     keeps only what was perceived.
//   tactics (1 Hz): attackers advance by covered bounds along their lane (left / main / right relative to the
//     objective's attack arrow) and converge into the capture area; defenders hold scored cover spots inside the point,
//     spread out, reposition when hit from an uncovered side and return when the point is contested. Frag and smoke
//     throws are planned here with squad reservations, safety and trajectory checks.
//   fire (every step): aim turns at a finite rate separate from body facing, reaction delay, distance / movement /
//     suppression dependent dispersion, bursts; the same projectiles, magazines and cooldowns as the player.
//   move (every step): A* on the walk grid, string pulling, doors opened through the shared interaction, stuck timers,
//     alternate goals; never teleports.
#include "game.h"
#include "app.h"
#include "crumb.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace {
float angDiff(float a, float b) { return wrapDeg(a - b + 180.0f) - 180.0f; }
float yawOf(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }           // inverse of App::forward
float pitchOf(vec3 d) { return degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
bool isLeaf(uint64_t u) { if (u == 0 || (u >> 60) == 0xF) return false; uint64_t l = u & 0xFFFF; return l != 0 && l != 0xFFFF; }
int gPathBudget = 0;   // A* requests per step (shared)
}

// ---------------------------------------------------------------- perception
bool Game::lineOfSight(vec3 a, vec3 b, float* smokeT) const {
  vec3 d = b - a; float L = glm::length(d);
  if (L < 1e-3f) { if (smokeT) *smokeT = 1; return true; }
  Physics::Hit h[8];
  int n = Physics::castRayAll(a, d, L, h, 8, false);
  for (int k = 0; k < n; ++k) {
    if (isLeaf(h[k].ud)) {
      // glazed window leaves are see-through; doors / shutters / broken-out frames block
      uint64_t inst = h[k].ud >> 16; int idx = (int)(h[k].ud & 0xFFFF) - 1;
      auto it = app.rt.find(inst);
      if (it != app.rt.end() && idx >= 0 && idx < (int)it->second.asset->interactables.size() && it->second.asset->interactables[idx].kind == "window") continue;
    }
    return false;
  }
  if (smokeT) *smokeT = gren.transmittance(a, b, t);
  return true;
}

void Game::aiPerceive(Soldier& s) {
  Brain& b = s.br;
  const float dtp = 0.2f;
  vec3 e = eye(s);
  vec3 look = App::forward(b.aimYaw, 0);
  for (auto& m : b.mem) m.visible = false;
  for (auto& o : sold) {
    if (!o.alive || o.role == s.role) continue;
    vec3 tc = chest(o), th = eye(o);
    float dist = glm::length(tc - e);
    EnemyMemory* m = nullptr;
    for (auto& x : b.mem) if (x.enemy == o.slot) m = &x;
    bool firing = t - o.lastShot < 0.6;
    if (o.localPlayer && o.weapon >= 0 && o.weapon < (int)app.weaponDefs.size()) { auto it = app.wstate.find(app.weaponDefs[o.weapon]->id); if (it != app.wstate.end() && it->second.sinceShot < 0.6f) firing = true; }
    if (dist > b.sight * (firing ? 1.4f : 1.0f)) { if (m) m->awareness = std::max(0.0f, m->awareness - dtp * 0.5f); continue; }
    vec3 dir = (tc - e) / std::max(dist, 1e-3f);
    float cosHalf = std::cos(radians(b.fov * 0.5f));
    bool inFov = dist < 6.0f || glm::dot(glm::normalize(vec3(dir.x, 0, dir.z) + vec3(1e-5f)), look) >= cosHalf;
    float T = 0; bool los = false;
    if (inFov) { los = lineOfSight(e, tc, &T); if (!los) los = lineOfSight(e, th, &T); }
    if (los && T > 0.35f) {
      if (!m) { b.mem.push_back({o.slot}); m = &b.mem.back(); }
      float rate = dist < 15 ? 4.0f : dist < 50 ? 1.6f : 0.7f;
      if (o.speed > 1.0f) rate *= 1.5f;
      if (o.posture == Physics::PRONE) rate *= 0.5f; else if (o.posture == Physics::CROUCH) rate *= 0.8f;
      if (firing) rate *= 2.0f;
      rate *= std::clamp((T - 0.35f) / 0.4f, 0.15f, 1.0f);     // thin smoke slows detection, dense smoke stops it
      m->awareness = std::min(1.0f, m->awareness + rate * dtp);
      if (m->awareness >= 1.0f) { m->visible = true; m->lastPos = o.feet; m->lastSeen = t; m->heardOnly = false; }
    } else if (m) m->awareness = std::max(0.0f, m->awareness - dtp * 0.5f);
  }
  // hearing: approximate positions only
  float hearShot = archetypes["perception"].value("hearShot_m", 70.0f);
  for (auto& snd : sounds) {
    if (t - snd.t > 0.25 || snd.src < 0 || snd.src >= (int)sold.size()) continue;
    const Soldier& o = sold[snd.src];
    if (o.role == s.role || !o.alive) continue;
    float d = glm::length(snd.p - s.feet);
    if (d > (snd.kind == 0 ? hearShot : snd.range)) continue;
    EnemyMemory* m = nullptr;
    for (auto& x : b.mem) if (x.enemy == o.slot) m = &x;
    if (!m) { b.mem.push_back({o.slot}); m = &b.mem.back(); }
    if (m->visible || t - m->lastSeen < 1.0) continue;
    float err = std::min(8.0f, d * 0.12f);
    m->lastPos = snd.p + vec3((rnd(s) - 0.5f) * 2 * err, 0, (rnd(s) - 0.5f) * 2 * err);
    m->lastSeen = t; m->heardOnly = true; m->awareness = std::max(m->awareness, 0.5f);
  }
  float memS = archetypes["perception"].value("memory_s", 20.0f);
  b.mem.erase(std::remove_if(b.mem.begin(), b.mem.end(), [&](const EnemyMemory& m) { return t - m.lastSeen > memS && m.awareness <= 0.01f; }), b.mem.end());
  // grenade danger: seen in the air / landing nearby, or heard landing within a few metres
  for (auto& g : gren.live) {
    if (g.kind != GK_FRAG) continue;
    float d = glm::length(g.p - s.feet);
    if (d > 8.0f) continue;
    bool seen = glm::dot(glm::normalize(g.p - e + vec3(1e-4f)), look) > 0.3f && lineOfSight(e, g.p);
    if (seen || (g.resting && d < 4.0f)) { b.evadeFrom = g.p; b.evadeUntil = t + g.fuse + 0.5; }
  }
  // target selection: visible enemies, nearest first, those inside the objective preferred
  int best = -1; float bs = 1e9f;
  const Objective* ob = match.activeObjective();
  for (auto& m : b.mem) {
    if (!m.visible) continue;
    float sc = glm::length(m.lastPos - s.feet);
    if (ob && ob->contains(m.lastPos)) sc *= 0.7f;
    if (m.enemy == b.target) sc *= 0.8f;                     // stickiness: no constant reshuffling
    if (sc < bs) { bs = sc; best = m.enemy; }
  }
  if (best != b.target) { b.target = best; b.targetTime = 0; b.aimSteady = 0; }
}

// ---------------------------------------------------------------- tactics helpers
bool Game::findCover(Soldier& s, vec3 threat, float radius, vec3& out) {
  if (!nav.valid()) return false;
  const Objective* ob = match.activeObjective();
  float bestScore = -1e9f;
  int tries = 0;
  for (int k = 0; k < 28; ++k) {
    float a = rnd(s) * 6.2831853f, r = 1.5f + rnd(s) * radius;
    vec3 q = s.feet + vec3(std::cos(a) * r, 0, std::sin(a) * r);
    int slot = nav.nearest(q, 1.0f);
    if (slot < 0) continue;
    vec3 p = nav.pos(slot);
    if (!(nav.slots[slot].flags & (Nav::N_WALL | Nav::N_STRUCT))) continue;   // cover is next to something solid
    ++tries;
    vec3 td = threat - p; td.y = 0; float tl = glm::length(td); if (tl < 2) continue; td /= tl;
    Physics::Hit h;
    bool low = Physics::castRayEx(p + vec3(0, 0.8f, 0), td, 2.0f, h);      // crouched body covered toward the threat
    if (!low) continue;
    bool peek = !Physics::castRayEx(p + vec3(0, 1.55f, 0), threat + vec3(0, 1.2f, 0) - (p + vec3(0, 1.55f, 0)), glm::length(threat - p), h);
    float sc = (peek ? 2.0f : 0.5f) - glm::length(p - s.feet) * 0.1f;
    if (ob && s.role == CombatRole::Attacker) sc += (glm::length(s.feet - ob->pos) - glm::length(p - ob->pos)) * 0.15f;   // forward progress
    for (auto& o : sold) if (&o != &s && o.alive && o.role == s.role && glm::length(o.br.coverPos - p) < 2.5f && o.br.hasCover) sc -= 3;   // reserved by a mate
    if (sc > bestScore) { bestScore = sc; out = p; }
  }
  return bestScore > -1e8f && tries > 0;
}

void Game::buildDefSpots() {
  defSpots.clear(); defSpotsStage = match.active;
  const Objective* o = match.activeObjective();
  if (!o || !nav.valid()) return;
  vec3 threatDir = -attackForward(o->attackYaw);
  std::vector<DefSpot> cand;
  int R = (int)std::ceil(o->radius / Nav::kCell);
  int cx = (int)std::floor((o->pos.x - nav.origin.x) / Nav::kCell), cz = (int)std::floor((o->pos.z - nav.origin.y) / Nav::kCell);
  for (int dz = -R; dz <= R; dz += 2) for (int dx = -R; dx <= R; dx += 2) {
    int x = cx + dx, z = cz + dz;
    if (x < 0 || z < 0 || x >= nav.nx || z >= nav.nz) continue;
    for (int l = 0; l < Nav::kLayers; ++l) {
      int sl = (z * nav.nx + x) * Nav::kLayers + l;
      if (!nav.used(sl)) continue;
      vec3 p = nav.pos(sl);
      if (!o->contains(p)) continue;
      int links = 0; for (int d = 0; d < 8; ++d) links += nav.slots[sl].link[d] >= 0;
      if (links < 3) continue;
      DefSpot d; d.p = p; d.nav = sl;
      Physics::Hit h;
      int cov = 0;
      for (int k = 0; k < 8; ++k) { float a = k * 0.785398f; if (Physics::castRayEx(p + vec3(0, 0.9f, 0), vec3(std::cos(a), 0, std::sin(a)), 1.3f, h)) ++cov; }
      bool front = Physics::castRayEx(p + vec3(0, 0.8f, 0), threatDir, 1.6f, h);
      int view = 0;
      for (float off : {-0.45f, 0.0f, 0.45f}) {
        vec3 vd = glm::normalize(glm::angleAxis(off, vec3(0, 1, 0)) * threatDir);
        if (!Physics::castRayEx(p + vec3(0, 1.5f, 0), vd, 30.0f, h)) ++view;
        else if (h.t > 12) ++view;
      }
      if (view == 0) continue;                                  // a spot that sees nothing of the approaches is useless
      d.cover = cov / 8.0f + (front ? 1.0f : 0.0f);
      d.view = view / 3.0f;
      float bonus = 0; for (auto& dp : o->defendPoints) if (glm::length(dp - p) < 2.0f) bonus = 1.5f;
      d.cover += bonus;
      cand.push_back(d);
    }
  }
  std::sort(cand.begin(), cand.end(), [](const DefSpot& a, const DefSpot& b) { return a.cover * 2 + a.view * 1.5f > b.cover * 2 + b.view * 1.5f; });
  for (auto& c : cand) {
    bool close = false; for (auto& d : defSpots) if (glm::length(d.p - c.p) < 3.0f) close = true;
    if (!close) defSpots.push_back(c);
    if (defSpots.size() >= 40) break;
  }
  logf("ai: %zu defend spots for objective %d", defSpots.size(), match.active + 1);
}

int Game::pickDefSpot(Soldier& s) {
  if (defSpotsStage != match.active) buildDefSpots();
  int best = -1; float bs = -1e9f;
  for (size_t k = 0; k < defSpots.size(); ++k) {
    DefSpot& d = defSpots[k];
    if (d.heldBy >= 0 && d.heldBy != s.slot && sold[d.heldBy].alive) continue;
    float sc = d.cover * 2 + d.view * 1.5f - glm::length(d.p - s.feet) * 0.03f;
    for (auto& o : defSpots) if (&o != &d && o.heldBy >= 0 && o.heldBy != s.slot && glm::length(o.p - d.p) < 5.0f) sc -= 2.0f;   // spread out
    if (s.br.hurtAt > t - 4 && glm::length(d.p - s.br.holdPos) < 2.0f) sc -= 5;      // just got hit there: move
    // cover toward known threats
    for (auto& m : s.br.mem) if (t - m.lastSeen < 8) { vec3 td = m.lastPos - d.p; td.y = 0; if (glm::length(td) > 1) { Physics::Hit h; if (Physics::castRayEx(d.p + vec3(0, 0.8f, 0), glm::normalize(td), 1.6f, h)) sc += 0.6f; } break; }
    if (sc > bs) { bs = sc; best = (int)k; }
  }
  for (auto& d : defSpots) if (d.heldBy == s.slot) d.heldBy = -1;
  if (best >= 0) { defSpots[best].heldBy = s.slot; s.br.holdSpot = best; s.br.holdPos = defSpots[best].p; }
  return best;
}

bool Game::aiPathTo(Soldier& s, vec3 goal) {
  Brain& b = s.br;
  if (glm::length(goal - b.goal) < 0.5f && !b.path.empty() && b.repathT > 0) return true;
  b.goal = goal;
  if (!nav.valid()) return false;
  if (gPathBudget <= 0) { b.repathT = 0; return true; }       // try again next step (spread A* across frames)
  --gPathBudget;
  int a = nav.nearest(s.feet, 1.5f), g = nav.nearest(goal, 2.5f);
  b.repathT = 3.0f + rnd(s);
  auto p0 = std::chrono::steady_clock::now();
  bool found = a >= 0 && g >= 0 && nav.path(a, g, b.path, 40000);
  pathMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count(); ++pathCalls;
  if (!found) {
    b.path.clear(); ++b.pathFails; b.pathFailT = (float)t;
    b.why = a < 0 ? "off the walk grid" : g < 0 ? "goal off the walk grid" : "no route";
    return false;
  }
  b.pathIdx = 0; b.pathFails = 0; b.goalSlot = g;
  return true;
}

static std::vector<vec3> laneRoute(const Game& G, const Soldier& s) {
  const Objective* o = G.match.activeObjective();
  std::vector<vec3> r;
  if (!o) return r;
  for (auto& ar : o->routes) if (ar.lane == s.br.lane && !ar.points.empty()) { r = ar.points; break; }
  if (r.empty() && s.br.lane != LANE_MAIN) {
    // generated detour: a point beside the approach line, on the lane's side of the attack arrow, ~60 % of the way
    vec3 from = s.feet, f = attackForward(o->attackYaw), right = attackRight(o->attackYaw);
    float side = s.br.lane == LANE_RIGHT ? 1.0f : -1.0f;
    float d = glm::length(vec2(o->pos.x - from.x, o->pos.z - from.z));
    vec3 mid = o->pos - f * std::max(o->radius + 10.0f, d * 0.4f) + right * side * std::max(14.0f, o->radius * 1.4f);
    r.push_back(mid);
  }
  // converge: every lane ends inside the capture area
  float a = (s.slot * 2.39996f), rr = o->radius * (0.25f + 0.35f * ((s.slot * 37) % 10) / 10.0f);
  r.push_back(o->pos + vec3(std::cos(a) * rr, 0, std::sin(a) * rr));
  return r;
}

bool Game::planFrag(Soldier& s, vec3& vel, vec3& target) {
  Brain& b = s.br;
  if (s.frags <= 0 || b.grenadeCD > 0 || s.throwT >= 0) return false;
  const json& gj = archetypes["grenade"];
  float rMin = gj["fragRange_m"][0], rMax = gj["fragRange_m"][1], safe = gj.value("safeRadius_m", 7.0f), speed = gj.value("throwSpeed_mps", 14.0f), cell = gj.value("reserveCell_m", 6.0f);
  for (auto& m : b.mem) {
    if (m.heardOnly || t - m.lastSeen > 4.0) continue;
    float d = glm::length(m.lastPos - s.feet);
    if (d < rMin || d > rMax) continue;
    // worth a grenade: the enemy went out of sight (behind cover / in a room), or several are clustered there
    int cluster = 0; for (auto& n : b.mem) if (t - n.lastSeen < 4 && glm::length(n.lastPos - m.lastPos) < 5) ++cluster;
    bool inBuilding = false; { int sl = nav.nearest(m.lastPos, 1.5f); if (sl >= 0 && (nav.slots[sl].flags & Nav::N_STRUCT)) inBuilding = true; }
    if (m.visible && cluster < 2 && !inBuilding) continue;
    vec3 tgt = m.lastPos + vec3(0, 0.1f, 0);
    int key = ((int)std::floor(tgt.x / cell) * 73856093) ^ ((int)std::floor(tgt.z / cell) * 19349663);
    auto it = grenadeReserve.find(key);
    if (it != grenadeReserve.end() && it->second > (int)steps) continue;           // a squad mate already plans one there
    bool unsafe = false;
    for (auto& o : sold) if (o.alive && o.role == s.role && glm::length(o.feet - tgt) < safe) unsafe = true;   // friendlies (humans included)
    if (glm::length(s.feet - tgt) < safe) unsafe = true;
    if (unsafe) continue;
    vec3 hand = s.feet + vec3(0, 1.7f, 0);
    for (int hi = 0; hi < 2; ++hi) {
      vec3 v;
      if (!grenadeVelocity(hand, tgt, speed, hi == 1, v)) continue;
      // simulate the arc against the world: clear initial path, no door / window geometry, lands near the target
      vec3 p = hand, vv = v; bool ok = false; float tt = 0;
      for (int k = 0; k < 120; ++k) {
        vec3 seg = vv * 0.05f; Physics::Hit h;
        if (Physics::castRayEx(p, seg, glm::length(seg), h)) {
          if (isLeaf(h.ud)) break;                                                // a door / window in the way
          if (glm::length(h.pos - s.feet) < 4.0f) break;                          // would bounce back at the thrower
          ok = glm::length(vec2(h.pos.x - tgt.x, h.pos.z - tgt.z)) < 3.0f && std::fabs(h.pos.y - tgt.y) < 2.0f;
          break;
        }
        p += seg; vv.y -= 9.81f * 0.05f; tt += 0.05f;
      }
      if (!ok) continue;
      vel = v; target = tgt;
      grenadeReserve[key] = (int)steps + 60 * 7;
      b.grenadeCD = gj.value("cooldown_s", 8.0f) + rnd(s) * 4;
      return true;
    }
  }
  return false;
}

bool Game::planSmoke(Soldier& s, vec3& vel, vec3& target) {
  Brain& b = s.br;
  if (s.smokes <= 0 || s.throwT >= 0 || s.role != CombatRole::Attacker || b.grenadeCD > 0) return false;
  const json& sj = archetypes["smoke"];
  if (b.stallT < sj.value("stall_s", 18.0f)) return false;
  // known defender firing positions (perceived), and where this bot needs to cross next
  vec3 sum(0); int n = 0;
  for (auto& m : b.mem) if (t - m.lastSeen < 12) { sum += m.lastPos; ++n; }
  if (!n || b.route.empty()) return false;
  vec3 def = sum / (float)n;
  vec3 next = b.route[std::min(b.routeIdx, b.route.size() - 1)];
  vec3 cross = glm::mix(s.feet, next, 0.4f);
  vec3 screen = glm::mix(cross, def, 0.3f);                                         // between their guns and our path
  if (glm::length(screen - s.feet) < 8) screen = s.feet + glm::normalize(screen - s.feet + vec3(1e-3f)) * 9.0f;
  float cell = sj.value("reserveCell_m", 10.0f);
  int key = ((int)std::floor(screen.x / cell) * 73856093) ^ ((int)std::floor(screen.z / cell) * 19349663);
  auto it = smokeReserve.find(key);
  if (it != smokeReserve.end() && t - it->second < sj.value("repeat_s", 25.0f)) return false;   // one smoke per crossing at a time
  Physics::Hit h; vec3 down = screen + vec3(0, 3, 0);
  if (Physics::castRayEx(down, vec3(0, -1, 0), 10, h)) screen = h.pos;
  vec3 hand = s.feet + vec3(0, 1.7f, 0);
  float sp = archetypes["grenade"].value("throwSpeed_mps", 17.0f);
  if (!grenadeVelocity(hand, screen, sp, false, vel) && !grenadeVelocity(hand, screen, sp, true, vel)) return false;
  target = screen;
  smokeReserve[key] = t;
  b.grenadeCD = 4.0f; b.stallT = 0;
  return true;
}

// ---------------------------------------------------------------- tactics (1 Hz)
void Game::aiTactics(Soldier& s) {
  Brain& b = s.br;
  const Objective* ob = match.activeObjective();
  const WeaponDef& wd = *app.weaponDefs[s.weapon];
  // reload when empty, or low with nobody in sight
  if (s.reload <= 0 && !s.ws.containers.empty() && (s.ws.loaded == 0 || (b.target < 0 && s.ws.loaded < wd.cap / 3))) {
    s.reloadTotal = s.ws.loaded == 0 ? wd.reloadEmpty : wd.reloadPartial;
    if (wd.feed == "single" || (wd.feed == "stripper" && s.ws.loaded > 0)) s.reloadTotal = wd.reloadRound * (wd.cap - s.ws.loaded);
    s.reload = s.reloadTotal;
    app.audio.play(wd.feed == "magazine" ? "mag_out" : "bolt", s.feet + vec3(0, 1.2f, 0), 0.5f);
  }
  if (t < b.evadeUntil) {   // grenade nearby: get away from it (and out of its line) first
    // the farthest of 8 escape points 8 m away; defenders prefer one that stays on the point
    vec3 best = s.feet; float bs = -1e9f;
    const Objective* ob0 = match.activeObjective();
    for (int k = 0; k < 8; ++k) {
      float a = k * 0.785398f; vec3 q = s.feet + vec3(std::cos(a), 0, std::sin(a)) * 8.0f;
      int sl = nav.nearest(q, 1.5f); if (sl < 0) continue; q = nav.pos(sl);
      float sc = glm::length(q - b.evadeFrom);
      if (s.role == CombatRole::Defender && ob0 && ob0->contains(q)) sc += 4.0f;
      Physics::Hit hh; if (Physics::castRayEx(b.evadeFrom + vec3(0, 0.5f, 0), q - b.evadeFrom, glm::length(q - b.evadeFrom), hh)) sc += 3.0f;   // behind something solid
      if (sc > bs) { bs = sc; best = q; }
    }
    aiPathTo(s, best);
    b.mode = Brain::M_EVADE; b.why = "evading a grenade"; return;
  }
  // grenades (planned at most once per tactic tick)
  vec3 v, tg;
  if (planSmoke(s, v, tg)) { startThrow(s, GK_SMOKE, v, tg); b.why = "smoke screen"; b.stallT = 0; b.mode = Brain::M_ADVANCE; }
  else if (planFrag(s, v, tg)) { startThrow(s, GK_FRAG, v, tg); b.why = "frag at last-known position"; }
  if (!ob) { b.mode = Brain::M_HOLD; return; }
  bool inside = ob->contains(s.feet);
  const ObjectiveRT& ort = match.obj[match.active];
  if (s.role == CombatRole::Attacker) {
    // route per lane, rebuilt when the objective changes (living soldiers are not teleported, only re-tasked)
    if (b.route.empty() || b.routeStage != match.active) { b.route = laneRoute(*this, s); b.routeIdx = 0; b.routeStage = match.active; b.bestObjDist = 1e9f; b.stallT = 0; b.boundFrom = s.feet; }
    float od = glm::length(vec2(s.feet.x - ob->pos.x, s.feet.z - ob->pos.z));
    if (od < b.bestObjDist - 1.0f) { b.bestObjDist = od; b.stallT = 0; } else b.stallT += 1.0f;
    // a lane that cannot be walked falls back to the main assault (rebalance), never strands the bot
    if (b.pathFails >= 3 && b.lane != LANE_MAIN) { b.lane = LANE_MAIN; b.route = laneRoute(*this, s); b.routeIdx = 0; b.pathFails = 0; b.why = "flank route failed: main assault"; }
    // return fire happens while moving (aiFire); cover is a short reaction to being hit or pinned, never a new goal
    if (b.mode == Brain::M_COVER && b.modeT < 3.5f && !inside) return;
    bool pinned = t - b.hurtAt < 1.5 || b.suppressed > 0.6f;
    if (pinned && !inside && t > b.coverCooldown) {
      vec3 cp; vec3 thr = b.target >= 0 ? sold[b.target].feet : b.hurtFrom;
      b.coverCooldown = t + 9.0;
      if (findCover(s, thr, 6.0f, cp)) { b.coverPos = cp; b.hasCover = true; aiPathTo(s, cp); b.mode = Brain::M_COVER; b.modeT = 0; b.crouch = true; b.why = "taking cover (under fire)"; return; }
    }
    if (inside && od < ob->radius * 0.8f) {
      // in the capture area: take a spot (cover toward the defenders if possible) and fight from there
      if (b.mode != Brain::M_HOLD) { vec3 cp; vec3 thr = b.target >= 0 ? sold[b.target].feet : ob->pos + attackForward(ob->attackYaw) * 20.0f;
        if (findCover(s, thr, 5.0f, cp) && ob->contains(cp)) aiPathTo(s, cp); else b.path.clear();
        b.mode = Brain::M_HOLD; b.modeT = 0; b.why = "holding inside the objective"; }
      b.crouch = true; return;
    }
    // advance: next waypoint, covered bounds when the enemy was recently seen
    while (b.routeIdx + 1 < b.route.size() && glm::length(vec2(b.route[b.routeIdx].x - s.feet.x, b.route[b.routeIdx].z - s.feet.z)) < 4.0f) ++b.routeIdx;
    vec3 wp = b.route[std::min(b.routeIdx, b.route.size() - 1)];
    bool threat = false; for (auto& m : b.mem) if (t - m.lastSeen < 10) threat = true;
    if (threat && b.mode == Brain::M_ADVANCE && glm::length(s.feet - b.boundFrom) > 12.0f && t > b.coverCooldown) {   // covered bound: a short pause every ~12 m under threat
      vec3 cp; vec3 thr(0); int n = 0; for (auto& m : b.mem) if (t - m.lastSeen < 10) { thr += m.lastPos; ++n; }
      b.boundFrom = s.feet; b.coverCooldown = t + 6.0;
      if (n && findCover(s, thr / (float)n, 5.0f, cp)) { aiPathTo(s, cp); b.coverPos = cp; b.hasCover = true; b.mode = Brain::M_COVER; b.modeT = 1.5f; b.why = "covered bound"; b.crouch = true; return; }
    }
    if (b.mode != Brain::M_ADVANCE) { b.mode = Brain::M_ADVANCE; b.modeT = 0; }
    b.crouch = false;
    aiPathTo(s, wp);
    b.why = b.lane == LANE_LEFT ? "advancing (left flank)" : b.lane == LANE_RIGHT ? "advancing (right flank)" : "advancing (main assault)";
    return;
  }
  // ---------------- defender
  if (defSpotsStage != match.active) buildDefSpots();
  bool contested = ort.A > 0;
  bool hurtUncovered = false;
  if (t - b.hurtAt < 1.5 && b.mode == Brain::M_HOLD) {
    vec3 hd = b.hurtFrom - s.feet; hd.y = 0;
    Physics::Hit h;
    if (glm::length(hd) > 1 && !Physics::castRayEx(s.feet + vec3(0, 0.8f, 0), glm::normalize(hd), 1.6f, h)) hurtUncovered = true;   // outflanked
  }
  if (b.holdSpot < 0 || b.holdSpot >= (int)defSpots.size() || defSpots[b.holdSpot].heldBy != s.slot || hurtUncovered) {
    int k = pickDefSpot(s);
    if (k < 0) { b.holdPos = ob->pos; }
    b.mode = Brain::M_RETURN; b.modeT = 0; b.why = hurtUncovered ? "repositioning: flanked" : "taking a defend position";
  }
  // bounded pursuit: chase a lost target only a little beyond the point, never while the point is contested
  if (b.target < 0 && !contested) {
    for (auto& m : b.mem) if (!m.heardOnly && !m.visible && t - m.lastSeen < 6 && glm::length(vec2(m.lastPos.x - ob->pos.x, m.lastPos.z - ob->pos.z)) < ob->radius + 6) {
      if (b.mode != Brain::M_INVESTIGATE && b.aggression > 0.4f) { b.investigate = m.lastPos; b.mode = Brain::M_INVESTIGATE; b.modeT = 0; b.why = "checking last-known position"; }
      break;
    }
  }
  if (b.mode == Brain::M_INVESTIGATE) {
    bool tooFar = glm::length(vec2(s.feet.x - ob->pos.x, s.feet.z - ob->pos.z)) > ob->radius + 8;
    if (b.modeT > 8 || contested || tooFar || b.target >= 0 || glm::length(s.feet - b.investigate) < 2.0f) { b.mode = Brain::M_RETURN; b.modeT = 0; }
    else { aiPathTo(s, b.investigate); b.crouch = false; return; }
  }
  if (contested && !inside) b.why = "reinforcing the contested point";
  if (glm::length(s.feet - b.holdPos) > 1.2f) { aiPathTo(s, b.holdPos); b.crouch = false; if (b.mode != Brain::M_RETURN) { b.mode = Brain::M_RETURN; b.modeT = 0; } }
  else { b.path.clear(); b.mode = Brain::M_HOLD; b.crouch = true; b.why = "holding a covered spot"; }
}

// ---------------------------------------------------------------- fire (every step)
void Game::aiFire(Soldier& s, float dt) {
  Brain& b = s.br;
  b.suppressed = std::max(0.0f, b.suppressed - dt * 0.4f);
  b.targetTime += dt;
  float wantYaw = b.aimYaw, wantPitch = 0;
  vec3 e = eye(s);
  bool shoot = false; vec3 aimPoint(0); float smokeT = 1;
  EnemyMemory* tm = nullptr;
  for (auto& m : b.mem) if (m.enemy == b.target) tm = &m;
  if (tm && tm->visible && sold[b.target].alive) {
    const Soldier& o = sold[b.target];
    aimPoint = chest(o) + o.vel * 0.12f;                        // slight lead
    vec3 d = aimPoint - e; wantYaw = yawOf(d); wantPitch = pitchOf(d);
    shoot = true;
    lineOfSight(e, aimPoint, &smokeT);
  } else if (tm && !tm->heardOnly && t - tm->lastSeen < 1.5 && b.aggression > 0.6f && s.ws.loaded > 2) {
    aimPoint = tm->lastPos + vec3(0, 1.0f, 0);                  // suppress where they were, not where they are
    vec3 d = aimPoint - e; wantYaw = yawOf(d); wantPitch = pitchOf(d);
    shoot = true;
  } else if (!b.path.empty() && b.pathIdx < b.path.size()) {
    vec3 d = nav.pos(b.path[std::min(b.pathIdx + 2, b.path.size() - 1)]) - s.feet; d.y = 0;
    if (glm::length(d) > 0.3f) wantYaw = yawOf(d);
    for (auto& m : b.mem) if (t - m.lastSeen < 6) { wantYaw = yawOf(m.lastPos - s.feet); break; }   // watch the last threat
  } else if (const Objective* ob = match.activeObjective(); ob && s.role == CombatRole::Defender) {
    wantYaw = yawOf(-attackForward(ob->attackYaw));
    for (auto& m : b.mem) if (t - m.lastSeen < 8) { wantYaw = yawOf(m.lastPos - s.feet); break; }
  }
  // finite aim turn rate (independent of locomotion facing)
  float dy = angDiff(wantYaw, b.aimYaw), dp = wantPitch - b.aimPitch;
  float step = b.aimTurn * dt;
  b.aimYaw = wrapDeg(b.aimYaw + std::clamp(dy, -step, step));
  b.aimPitch = std::clamp(b.aimPitch + std::clamp(dp, -step, step), -60.0f, 60.0f);
  float err = std::sqrt(dy * dy + dp * dp);
  b.aimSteady = err < 3.0f ? b.aimSteady + dt : 0.0f;
  if (!shoot || s.throwT >= 0 || s.reload > 0 || s.weapon < 0) return;
  if (b.targetTime < b.reaction || err > 4.0f) return;
  if (b.burstPause > 0) { b.burstPause -= dt; return; }
  const WeaponDef& d = *app.weaponDefs[s.weapon];
  if (s.ws.cooldown > 0 || s.ws.bolt > 0) return;
  // dispersion: no perfect shots
  float dist = glm::length(aimPoint - e);
  float sig = b.sigma * (1.0f + dist / 60.0f) * (s.speed > 0.5f ? 1.8f : 1.0f) * (sold[std::max(0, b.target)].speed > 1.0f ? 1.3f : 1.0f)
            * (1.0f + b.suppressed) * std::max(0.6f, 1.6f - b.aimSteady * 0.5f) * (smokeT < 0.6f ? 1.5f : 1.0f);
  if (d.feed == "belt" && s.posture == Physics::STAND) sig *= 2.0f;
  float g1 = rnd(s), g2 = rnd(s);
  float r = std::sqrt(-2.0f * std::log(std::max(g1, 1e-6f))) * sig, a = g2 * 6.2831853f;
  vec3 dir = App::forward(b.aimYaw + r * std::cos(a), b.aimPitch + r * std::sin(a));
  if (soldierFire(s, dir)) {
    if (d.action == "auto") {
      if (b.burstLeft <= 0) b.burstLeft = (float)(b.burstMin + (int)(rnd(s) * (b.burstMax - b.burstMin + 1)));
      if (--b.burstLeft <= 0) b.burstPause = 0.35f + rnd(s) * 0.4f;
    } else b.burstPause = 0.25f + rnd(s) * 0.45f + (dist > 60 ? 0.4f : 0.0f);
  }
}

// ---------------------------------------------------------------- movement (every step)
void Game::aiMove(Soldier& s, float dt) {
  Brain& b = s.br;
  b.repathT -= dt;
  vec3 vel(0);
  if (!b.path.empty() && b.pathIdx < b.path.size() && b.doorWait <= 0) {
    // string pulling: skip to the farthest of the next few nodes that is directly walkable (checked 4x / s)
    static int pull = 0;
    if (((++pull + s.slot) & 15) == 0) {
      for (size_t k = std::min(b.path.size() - 1, b.pathIdx + 6); k > b.pathIdx + 1; --k)
        if (std::fabs(nav.pos(b.path[k]).y - s.feet.y) < 0.3f && Nav::walkLine(s.feet + vec3(0, 0.05f, 0), nav.pos(b.path[k]))) { b.pathIdx = k; break; }
    }
    vec3 wp = nav.pos(b.path[b.pathIdx]);
    vec3 d = wp - s.feet; d.y = 0;
    if (glm::length(d) < 0.35f) {
      // door on the next link: open it through the shared interaction, then wait for the leaf
      if (b.pathIdx + 1 < b.path.size()) {
        int a = b.path[b.pathIdx], c = b.path[b.pathIdx + 1];
        for (int dir = 0; dir < 8; ++dir) if (nav.neighbour(a, dir) == c && nav.doorLink(a, dir)) {
          vec3 p0 = nav.pos(a) + vec3(0, 1.0f, 0), p1 = nav.pos(c) + vec3(0, 1.0f, 0);
          Physics::Hit h[4]; int n = Physics::castRayAll(p0, p1 - p0, glm::length(p1 - p0) + 0.3f, h, 4, false);
          for (int k = 0; k < n; ++k) if (isLeaf(h[k].ud)) {
            uint64_t inst = h[k].ud >> 16; int idx = (int)(h[k].ud & 0xFFFF) - 1;
            auto it = app.rt.find(inst);
            if (it != app.rt.end() && idx >= 0 && idx < (int)it->second.dir.size() && it->second.dir[idx] <= 0 && it->second.progress[idx] < 0.9f) {
              app.toggleInteract(inst, idx); ++doorOpens; b.doorWait = 1.6f; b.why = "opening a door";
            }
            break;
          }
        }
      }
      ++b.pathIdx;
    } else {
      float speed = b.crouch ? 1.2f : (b.mode == Brain::M_EVADE || (b.target < 0 && s.role == CombatRole::Attacker && b.mode == Brain::M_ADVANCE && s.br.stallT < 1)) ? 4.6f : 2.875f;
      if (b.target >= 0) speed = std::min(speed, 2.2f);                       // fire while moving: slower
      vel = glm::normalize(d) * speed;
    }
  }
  if (b.doorWait > 0) b.doorWait -= dt;
  if (b.path.empty() || b.pathIdx >= b.path.size()) { b.path.clear(); }
  // stuck detection (no teleport): re-plan, then an alternate goal
  b.stuckT += dt;
  if (b.stuckT > 1.5f) {
    bool moving = glm::length(vel) > 0.1f;
    if (moving && glm::length(vec2(s.feet.x - b.progressPos.x, s.feet.z - b.progressPos.z)) < 0.4f) {
      ++b.pathFails; b.repathT = 0; b.why = "stuck: re-planning";
      if (b.pathFails >= 2 && b.pathFails < 4) { vec3 alt = b.goal + vec3((rnd(s) - 0.5f) * 8, 0, (rnd(s) - 0.5f) * 8); b.path.clear(); aiPathTo(s, alt); b.why = "stuck: alternate destination"; }
    }
    b.progressPos = s.feet; b.stuckT = 0;
  }
  if (b.repathT <= 0 && glm::length(b.goal) > 0 && b.mode != Brain::M_HOLD) aiPathTo(s, b.goal);
  // body facing: toward movement when moving fast, else toward the aim (the upper body twists the rest)
  float bodyWant = glm::length(vel) > 1.0f ? yawOf(vel) : b.aimYaw;
  if (std::fabs(angDiff(b.aimYaw, bodyWant)) > 70.0f) bodyWant = b.aimYaw;      // strafe instead of over-twisting
  float turn = 360.0f * dt;
  s.yaw = wrapDeg(s.yaw + std::clamp(angDiff(bodyWant, s.yaw), -turn, turn));
  int posture = b.crouch && glm::length(vel) < 1.5f ? Physics::CROUCH : Physics::STAND;
  Physics::updateAgent(s.agent, dt, vel, false, posture, s.yaw);
  auto st = Physics::agent(s.agent);
  s.vel = (st.pos - s.feet) / std::max(dt, 1e-4f); s.feet = st.pos; s.posture = st.posture;
  s.speed = glm::length(vec2(s.vel.x, s.vel.z));
  if (s.feet.y < -60) { s.hp = 0; killSoldier(s.slot, -1, vec3(0, -1, 0)); }   // fell out of the world (never a hidden teleport)
}

void Game::aiStep(Soldier& s, float dt) {
  Brain& b = s.br;
  b.modeT += dt; b.grenadeCD -= dt;
  static uint64_t lastStep = ~0ull;
  if (lastStep != steps) { lastStep = steps; gPathBudget = 3; }
  crumb("ai: perceive"); if (t >= b.nextPerceive) { aiPerceive(s); b.nextPerceive = (float)t + 0.2f; }
  crumb("ai: tactics"); if (t >= b.nextTactic) { aiTactics(s); b.nextTactic = (float)t + 0.9f + rnd(s) * 0.2f; }
  crumb("ai: fire"); aiFire(s, dt);
  crumb("ai: move"); if (s.agent >= 0) aiMove(s, dt);
}

void Game::assignLanes() {
  std::vector<int> atk; for (auto& s : sold) if (s.role == CombatRole::Attacker) atk.push_back(s.slot);
  auto l = allocateLanes((int)atk.size(), match.rules.laneLeft, match.rules.laneMain, match.rules.laneRight);
  for (size_t k = 0; k < atk.size(); ++k) sold[atk[k]].br.lane = (int)k < l[0] ? LANE_LEFT : (int)k < l[0] + l[1] ? LANE_MAIN : LANE_RIGHT;
}
