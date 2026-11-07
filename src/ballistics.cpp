// Surface damage and penetration (the guide's "BallisticsManager").
//
// Surface types: Glass, WoodShutter, WoodDoor, Steel (doors / flaps whose sound is "steel"), wall families
// (masonry / concrete / timber from the panel), Earth (terrain), Sandbag, Foliage.
// Openings keep a per-copy session overlay (InstanceRT::leafSt / leafHits); "Apply damage to map" stores it.
//   Glass   HP 1  -> glass mesh hidden, the pane no longer stops rounds, shard burst
//   Shutter HP 2  -> detaches: Jolt dynamic body, impulse I = V_bullet * M_force at the hit, fractured mesh
//   Door    HP 10 -> breaks apart: plank fragments (own rigid bodies, splintered ends), impulse concentrated at the hit
//   Sash    HP 5  -> (window casement, rounds into its frame) breaks apart the same way, its panes bursting first
//   Frames, jambs, sills and lintels around an opening are trim bays (wallfield.cpp): they break whole, taking the
//   leaves with them
//   Steel doors, steel shutters and flaps: never break, whatever the weapon (rounds ring off, blasts leave them)
// Penetration through glass / shutters / doors (rounds only):
//   exit:   P_exit = P_hit + V_dir * dT   (dT from a back-cast against the same body)
//   energy: E_new = E_old - mu * dT,  mu = 20 kJ/m glass, 40 kJ/m wood; v_new = v * sqrt(E_new / E_old)
#include "app.h"
#include <GLFW/glfw3.h>
#include <algorithm>

enum : uint8_t { LEAF_GLASS_BROKEN = 1, LEAF_GONE = 2 };
static constexpr float kMuGlass = 20000.0f, kMuWood = 40000.0f, kMForce = 0.01f;

static bool isSteel(const InteractDef& d) { return d.sound == "steel"; }
static bool isDoor(const InteractDef& d) { return d.kind == "door"; }
static bool isShutter(const InteractDef& d) { return d.kind == "shutter" || d.kind == "shutters"; }
static bool isSash(const InteractDef& d) { return d.kind == "window"; }

static uint32_t hmix(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
static float hrnd(uint32_t seed, uint32_t a, uint32_t b) { return (hmix(seed ^ hmix(a * 0x9E3779B1u + 0x85ebca6bu) ^ hmix(b * 0xc2b2ae35u + 7u)) & 0xFFFFFF) / 16777216.0f; }

bool App::leafBreakable(const InteractDef& d) const { return !isSteel(d) && (isDoor(d) || isShutter(d) || isSash(d)); }

float App::leafHpFor(const InteractDef& d) const {
  if (isSteel(d)) return 1e9f;
  if (d.hp > 0) return d.hp;
  if (isDoor(d)) return 10;
  if (isShutter(d)) return 2;
  if (isSash(d)) return 5;   // a casement's frame: lighter than a door (its glass goes on the first round)
  return 1e9f;
}

static bool pointInPart(const Part& p, vec3 lp, float pad) {
  vec3 q = glm::inverse(p.rot) * (lp - p.center);
  vec3 h = p.size * 0.5f + vec3(pad);
  return std::fabs(q.x) <= h.x && std::fabs(q.y) <= h.y && std::fabs(q.z) <= h.z;
}

bool App::locateLeaf(const Instance& i, const InstanceRT& r, int k, vec3 w, int& leaf, bool& onGlass) const {
  const InteractDef& d = r.asset->interactables[k];
  float best = 1e9f; leaf = -1; onGlass = false;
  for (size_t l = 0; l < d.leaves.size(); ++l) {
    if (r.leafSt[k][l] & LEAF_GONE) continue;
    const Leaf& lf = d.leaves[l];
    vec3 lp = vec3(glm::inverse(leafMatrix(i, lf, d, r.progress[k])) * vec4(w, 1));
    for (auto& c : lf.colliders) {
      vec3 q = glm::inverse(c.rot) * (lp - c.center), h = c.size * 0.5f;
      float dist = glm::length(glm::max(glm::abs(q) - h, vec3(0)));
      if (dist < best) { best = dist; leaf = (int)l; }
    }
  }
  if (leaf < 0) return false;
  const Leaf& lf = d.leaves[leaf];
  vec3 lp = vec3(glm::inverse(leafMatrix(i, lf, d, r.progress[k])) * vec4(w, 1));
  for (auto& p : lf.parts)
    if (r.asset->materials[p.mat].pattern == PAT_GLASS && pointInPart(p, lp, 0.03f)) onGlass = true;
  return true;
}

void App::breakGlass(Instance& i, InstanceRT& r, int k, int l, vec3 at) {
  if (r.leafSt[k][l] & LEAF_GLASS_BROKEN) return;
  r.leafSt[k][l] |= LEAF_GLASS_BROKEN; ++leafBreaks;
  for (int n = 0; n < 26; ++n) {   // shard burst
    vec3 v(fx.rnd() * 2 - 1, fx.rnd() * 1.5f - 0.3f, fx.rnd() * 2 - 1);
    fx.debris.push_back({at, v * 2.5f, vec3(fx.rnd(), fx.rnd(), fx.rnd()) * 12.0f, vec3(0.03f + 0.04f * fx.rnd(), 0.004f, 0.03f + 0.05f * fx.rnd())});
  }
  if (fx.debris.size() > fx.maxDebris) fx.debris.erase(fx.debris.begin(), fx.debris.begin() + (fx.debris.size() - fx.maxDebris));
  fx.impact(at, vec3(0, 1, 0), "glass");
  audio.play("glass_break", at, 0.9f);
  (void)i;
}

// A door shot or blown down breaks apart: vertical plank strips (jittered widths) split across the grain into one
// to three pieces, finest around the hit. Each piece is clipped from the leaf's own parts (paint, battens, stiles),
// gets splintered ends where it broke, and becomes its own small rigid body with its share of the impulse.
int App::shatterLeaf(Instance& i, InstanceRT& r, int k, int l, vec3 impulse, vec3 at) {
  const InteractDef& d = r.asset->interactables[k];
  const Leaf& lf = d.leaves[l];
  mat4 LM = leafMatrix(i, lf, d, r.progress[k]);
  quat q = yawQuat(i.yaw) * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
  vec3 lo(1e9f), hi(-1e9f); int mainMat = -1; float mainVol = 0;
  for (auto& p : lf.parts) {
    if (!p.visible || p.shape != Part::BOX || r.asset->materials[p.mat].pattern == PAT_GLASS) continue;
    glm::mat3 Rm = glm::mat3_cast(p.rot);
    vec3 h = glm::abs(Rm[0]) * p.size.x * 0.5f + glm::abs(Rm[1]) * p.size.y * 0.5f + glm::abs(Rm[2]) * p.size.z * 0.5f;
    lo = glm::min(lo, p.center - h); hi = glm::max(hi, p.center + h);
    float vol = p.size.x * p.size.y * p.size.z;
    if (vol > mainVol) { mainVol = vol; mainMat = p.mat; }
  }
  if (mainMat < 0) return 0;
  vec3 ext = hi - lo;
  int tn = ext.x <= ext.y && ext.x <= ext.z ? 0 : ext.z <= ext.y ? 2 : 1;   // through the leaf
  int ha = tn == 1 ? 2 : 1, wa = 3 - tn - ha;                              // height (along the grain), width
  vec3 hit = vec3(glm::inverse(LM) * vec4(at, 1));
  uint32_t seed = hmix((uint32_t)i.id * 2654435761u ^ (uint32_t)(k * 977 + l * 131) ^ (uint32_t)leafBreaks * 7919u);
  auto R = [&](uint32_t a, uint32_t b) { return hrnd(seed, a, b); };
  float W = ext[wa], H = ext[ha];
  int ns = std::clamp((int)std::round(W / 0.16f), 3, 6);
  std::vector<float> cu{lo[wa]};
  for (int s2 = 1; s2 < ns; ++s2) cu.push_back(lo[wa] + W * (s2 + (R(s2, 1) - 0.5f) * 0.45f) / ns);
  cu.push_back(hi[wa]);
  struct Cell { float u0, u1, v0, v1; };
  std::vector<Cell> cells;
  for (int s2 = 0; s2 < ns; ++s2) {
    float mid = 0.5f * (cu[s2] + cu[s2 + 1]);
    std::vector<float> cv{lo[ha]};
    if (std::fabs(mid - hit[wa]) < 0.3f) {   // strips at the hit: broken out around it
      cv.push_back(hit[ha] - 0.12f - 0.12f * R(s2, 3));
      cv.push_back(hit[ha] + 0.12f + 0.12f * R(s2, 4));
    } else {
      int pieces = 1 + (R(s2, 2) < 0.7f ? 1 : 0) + (R(s2, 5) < 0.3f ? 1 : 0);
      for (int c = 1; c < pieces; ++c) cv.push_back(lo[ha] + H * (c + (R(s2, 10 + c) - 0.5f) * 0.6f) / pieces);
    }
    cv.push_back(hi[ha]);
    std::sort(cv.begin(), cv.end());
    std::vector<float> keep{cv.front()};   // no splinter-thin slices: every piece at least 12 cm along the grain
    for (size_t c = 1; c + 1 < cv.size(); ++c) if (cv[c] - keep.back() >= 0.12f && hi[ha] - cv[c] >= 0.12f) keep.push_back(cv[c]);
    keep.push_back(cv.back());
    for (size_t c = 0; c + 1 < keep.size(); ++c) cells.push_back({cu[s2], cu[s2 + 1], keep[c], keep[c + 1]});
  }
  const float leafMass = 28.0f;
  vec3 bc = i.pos + yawQuat(i.yaw) * ((r.asset->bmin + r.asset->bmax) * 0.5f);
  vec3 out = vec3(at.x - bc.x, 0, at.z - bc.z);
  out = glm::dot(out, out) > 1e-6f ? glm::normalize(out) : vec3(0);
  vec3 tnAxis(0); tnAxis[tn] = 1;
  int made = 0;
  for (size_t c = 0; c < cells.size(); ++c) {
    const Cell& C = cells[c];
    uint32_t c32 = (uint32_t)c;
    vec3 cmin = lo, cmax = hi; cmin[wa] = C.u0; cmax[wa] = C.u1; cmin[ha] = C.v0; cmax[ha] = C.v1;
    vec3 cc = 0.5f * (cmin + cmax);
    MeshData md;
    for (auto& p : lf.parts) {
      if (!p.visible || r.asset->materials[p.mat].pattern == PAT_GLASS) continue;
      bool aligned = p.shape == Part::BOX && std::fabs(std::fabs(p.rot.w) - 1.0f) < 1e-4f;
      if (!aligned) {   // hardware / odd shapes go with the piece that holds their centre
        if (p.shape == Part::BOX && p.center[wa] >= C.u0 && p.center[wa] < C.u1 && p.center[ha] >= C.v0 && p.center[ha] < C.v1) {
          Part s2 = p; s2.center -= cc; appendPart(md, s2, r.asset->materials[s2.mat], mat4(1));
        }
        continue;
      }
      vec3 a0 = p.center - p.size * 0.5f, a1 = p.center + p.size * 0.5f;
      a0[wa] = std::max(a0[wa], C.u0); a1[wa] = std::min(a1[wa], C.u1);
      a0[ha] = std::max(a0[ha], C.v0); a1[ha] = std::min(a1[ha], C.v1);
      if (a1[wa] - a0[wa] < 0.002f || a1[ha] - a0[ha] < 0.002f) continue;
      Part s2 = p; s2.center = 0.5f * (a0 + a1) - cc; s2.size = a1 - a0;
      appendPart(md, s2, r.asset->materials[s2.mat], mat4(1));
    }
    // splintered ends where the piece broke (not along the leaf's own edges)
    for (int end = 0; end < 2; ++end) {
      float y = end ? C.v1 : C.v0;
      if (end ? C.v1 >= hi[ha] - 0.01f : C.v0 <= lo[ha] + 0.01f) continue;
      int nsp = 2 + (R(c32, 40 + end) < 0.5f ? 1 : 0);
      for (int j = 0; j < nsp; ++j) {
        uint32_t t = (uint32_t)(j + end * 8);
        float wdt = 0.012f + 0.02f * R(c32, 50 + t), len = 0.03f + 0.07f * R(c32, 60 + t);
        Part sp; sp.shape = Part::BOX; sp.mat = mainMat; sp.collide = false;
        vec3 ctr = cc;
        ctr[wa] = C.u0 + wdt + std::max(0.0f, C.u1 - C.u0 - 2 * wdt) * R(c32, 70 + t);
        ctr[ha] = y + (end ? 0.5f : -0.5f) * len;
        ctr[tn] = cc[tn] + (R(c32, 80 + t) - 0.5f) * ext[tn] * 0.3f;
        vec3 sz(0); sz[wa] = wdt; sz[ha] = len; sz[tn] = ext[tn] * (0.35f + 0.4f * R(c32, 90 + t));
        sp.center = ctr - cc; sp.size = sz;
        sp.rot = glm::angleAxis(radians((R(c32, 100 + t) - 0.5f) * 18.0f), tnAxis);
        appendPart(md, sp, r.asset->materials[mainMat], mat4(1));
      }
    }
    if (md.v.empty()) continue;
    Part box; box.shape = Part::BOX; box.center = vec3(0);
    box.size = glm::max(cmax - cmin - vec3(0.006f), vec3(0.012f));   // a hair smaller: neighbours start apart
    ShapePtr sh = Physics::buildShape({box});
    if (!sh) continue;
    float mass = std::max(0.8f, leafMass * (C.u1 - C.u0) * (C.v1 - C.v0) / std::max(1e-4f, W * H));
    vec3 pos = vec3(LM * vec4(cc, 1));
    auto dl = std::make_unique<DynLeaf>();
    dl->inst = i.id; dl->k = k; dl->l = l; dl->fragment = true; dl->shape = sh;
    dl->body = Physics::addDynamic(sh, pos, q, (i.id << 16) | 0xFFF0u, mass);
    dl->fractured = std::make_unique<GpuMesh>(); dl->fractured->upload(md);
    // its share of the impulse, concentrated on the pieces around the hit; torn out of the frame; some scatter
    float wgt = 0.6f + 1.6f * std::exp(-glm::length(cc - hit) / 0.35f);
    vec3 J = impulse * (mass / leafMass) * wgt + out * mass * 0.6f + (vec3(R(c32, 110), R(c32, 111), R(c32, 112)) - 0.5f) * mass * 0.8f;
    Physics::addImpulse(dl->body, J, pos + q * ((vec3(R(c32, 113), R(c32, 114), R(c32, 115)) - 0.5f) * 0.1f));
    dynLeaves.push_back(std::move(dl));
    ++made;
  }
  for (int n = 0; n < 18; ++n) {   // splinters and paint flakes
    vec3 v(fx.rnd() * 2 - 1, fx.rnd() * 1.6f - 0.2f, fx.rnd() * 2 - 1);
    fx.debris.push_back({at, v * 2.2f + impulse * 0.05f, vec3(fx.rnd(), fx.rnd(), fx.rnd()) * 10.0f, vec3(0.01f + 0.015f * fx.rnd(), 0.006f, 0.04f + 0.06f * fx.rnd())});
  }
  if (fx.debris.size() > fx.maxDebris) fx.debris.erase(fx.debris.begin(), fx.debris.begin() + (fx.debris.size() - fx.maxDebris));
  return made;
}

void App::detachLeaf(Instance& i, InstanceRT& r, int k, int l, vec3 impulse, vec3 at, bool fracture) {
  if (r.leafSt[k][l] & LEAF_GONE) return;
  const InteractDef& d = r.asset->interactables[k];
  if (!leafBreakable(d)) return;   // iron doors, steel shutters and flaps: no weapon breaks them, whatever the caller
  const Leaf& lf = d.leaves[l];
  if (isDoor(d) || isSash(d)) {   // doors and window sashes break apart into pieces instead of falling off whole
    if (isSash(d)) breakGlass(i, r, k, l, at);   // the panes burst first
    shatterLeaf(i, r, k, l, impulse, at);
    r.leafSt[k][l] |= LEAF_GONE | LEAF_GLASS_BROKEN; ++leafBreaks;
    Physics::remove(r.leafBodies[k][l]); r.leafBodies[k][l] = 0xffffffffu;
  } else {   // shutters: the leaf comes off whole, the struck plank split around the hit
    r.leafSt[k][l] |= LEAF_GONE | LEAF_GLASS_BROKEN; ++leafBreaks;
    vec3 pos = vec3(instanceMatrix(i) * vec4(lf.hinge, 1));
    quat q = yawQuat(i.yaw) * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
    Physics::remove(r.leafBodies[k][l]); r.leafBodies[k][l] = 0xffffffffu;
    auto dl = std::make_unique<DynLeaf>();
    dl->inst = i.id; dl->k = k; dl->l = l;
    float mass = 7.0f;
    dl->body = Physics::addDynamic(lf.shape, pos, q, (i.id << 16) | 0xFFF0u, mass);
    if (fracture) {   // split the struck plank around the hit (a ragged gap where the rounds went through)
      MeshData md;
      vec3 lp = vec3(glm::inverse(leafMatrix(i, lf, d, r.progress[k])) * vec4(at, 1));
      for (auto& p : lf.parts) {
        if (!p.visible || r.asset->materials[p.mat].pattern == PAT_GLASS) continue;
        if (p.shape == Part::BOX && pointInPart(p, lp, 0.02f)) {
          int ax = p.size.x >= p.size.y ? 0 : 1;
          vec3 q2 = glm::inverse(p.rot) * (lp - p.center);
          float lo = -p.size[ax] * 0.5f, hi = p.size[ax] * 0.5f, cut = std::clamp(q2[ax], lo + 0.05f, hi - 0.05f), gap = 0.035f;
          for (int side = 0; side < 2; ++side) {
            float a0 = side ? cut + gap : lo, a1 = side ? hi : cut - gap;
            if (a1 - a0 < 0.01f) continue;
            Part s = p; vec3 off(0); off[ax] = (a0 + a1) * 0.5f;
            s.center = p.center + p.rot * off; s.size[ax] = a1 - a0;
            s.rot = p.rot * glm::angleAxis(radians(side ? 4.0f : -3.0f), vec3(0, 0, 1));
            appendPart(md, s, r.asset->materials[s.mat], mat4(1));
          }
          continue;
        }
        appendPart(md, p, r.asset->materials[p.mat], mat4(1));
      }
      dl->fractured = std::make_unique<GpuMesh>(); dl->fractured->upload(md);
    }
    Physics::addImpulse(dl->body, impulse, at);
    // the hinges tear out: the leaf drops away from its frame instead of staying wedged in the reveal
    vec3 bc = i.pos + yawQuat(i.yaw) * ((r.asset->bmin + r.asset->bmax) * 0.5f);
    vec3 out = vec3(at.x - bc.x, 0, at.z - bc.z);
    if (glm::dot(out, out) > 1e-6f) Physics::addImpulse(dl->body, glm::normalize(out) * mass * 0.6f, pos);
    dynLeaves.push_back(std::move(dl));
  }
  audio.play(isDoor(d) || isSash(d) ? "door_break" : "shutter_break", at, 0.9f);
  fx.impact(at, -glm::normalize(impulse + vec3(0, 1e-4f, 0)), "wood");
  // cap the number of live rigid bodies: the oldest becomes static where it lies
  size_t live = 0; for (auto& x : dynLeaves) live += !x->frozen;
  for (auto& x : dynLeaves) {
    if (live <= maxDynLeaves) break;
    if (x->frozen) continue;
    ShapePtr sh = x->shape;
    if (!sh) { auto it = rt.find(x->inst); if (it == rt.end()) continue; sh = it->second.asset->interactables[x->k].leaves[x->l].shape; }
    vec3 p; quat qq;
    Physics::getTransform(x->body, p, qq);
    Physics::remove(x->body);
    x->body = Physics::addStatic(sh, p, qq, (x->inst << 16) | 0xFFF0u);
    x->frozen = true; --live;
  }
}

bool App::bulletImpact(const Projectile& p, const Physics::Hit& hit, std::string& surface) {
  if (p.kind != PROJ_BULLET) return false;
  uint64_t low = hit.ud & 0xFFFF;
  bool ground = hit.ud == 0 || (hit.ud >> 60) == 0xF;
  if (ground || low == 0xFFFF) return false;   // terrain and walls stop the round
  uint64_t iid = hit.ud >> 16;
  if (!ground && low == 0 && defenseBulletHit(iid, hit.pos)) { surface = "sandbag"; return false; }   // defences stop every round
  if (!ground && (low == 0 || low == 0xFFF1u) && treeImpact(iid, hit.pos, glm::normalize(p.v), false)) { surface = "wood"; return false; }   // trunks stop rounds
  int propHit = (!ground && low == 0) ? interior.bulletHit(*this, iid, hit.pos, glm::normalize(p.v), surface) : 0;   // furniture (interior.cpp)
  if (propHit == 1) return false;
  if (low == 0 && !propHit) return false;   // any other static shell stops the round
  Instance* inst = doc.find(iid);
  auto it = rt.find(iid);
  if (!inst || it == rt.end()) return false;
  InstanceRT& r = it->second;
  vec3 dir = glm::normalize(p.v);
  float speed = glm::length(p.v);
  float mu = kMuWood;
  int k = -1, l = -1; bool onGlass = false;
  if (propHit) { mu = surface == "glass" ? kMuGlass : kMuWood; }   // furniture: wood / glass / cloth penetration
  else if (low == 0xFFF0u) { surface = "wood"; }   // loose debris from a detached leaf
  else if (low >= 1 && low <= r.asset->interactables.size()) {
    k = (int)low - 1;
    const InteractDef& d = r.asset->interactables[k];
    if (isSteel(d)) { surface = "steel"; return false; }
    if (!locateLeaf(*inst, r, k, hit.pos, l, onGlass)) return false;
    if (onGlass) {
      surface = "glass"; mu = kMuGlass;
      if (r.leafSt[k][l] & LEAF_GLASS_BROKEN) {   // empty sash: the round passes untouched
        Projectile c = p; c.p = hit.pos + dir * 0.06f; c.ignore = hit.ud; c.pens++;
        spawnedProjectiles.push_back(c);
        lastPen = {"glass (broken)", 0, 0.5f * p.mass * speed * speed, 0.5f * p.mass * speed * speed, true, hit.pos, c.p};
        return true;
      }
      breakGlass(*inst, r, k, l, hit.pos);
    } else {
      surface = isDoor(d) ? "wood_door" : isShutter(d) ? "wood_shutter" : "wood";
      r.leafHits[k][l] += 1;
      if (r.leafHits[k][l] >= leafHpFor(d)) detachLeaf(*inst, r, k, l, p.v * kMForce, hit.pos, isShutter(d));
    }
  } else return false;
  // ---- penetration: thickness along the path from a back-cast against the same body
  float dT = 0.02f;
  Physics::Hit back;
  const float probe = 0.6f;
  if (Physics::castRayOnly(hit.pos + dir * probe, -dir, probe, hit.ud, back)) dT = std::clamp(probe - back.t, 0.003f, 0.3f);
  if (onGlass) dT = std::min(dT, 0.008f);   // the pane itself, not the sash behind it
  float eOld = 0.5f * p.mass * speed * speed, eNew = eOld - mu * dT;
  lastPen = {surface, dT, eOld, std::max(0.0f, eNew), eNew > 0, hit.pos, hit.pos + dir * dT};
  if (eNew <= 0 || p.pens >= 4) return false;
  Projectile c = p;
  float s = std::sqrt(eNew / eOld);
  c.p = hit.pos + dir * (dT + 0.005f); c.v = p.v * s; c.dmg = p.dmg * s; c.ignore = hit.ud; c.pens = p.pens + 1;
  spawnedProjectiles.push_back(c);
  return true;
}

void App::blastLeaves(vec3 p, float radius) {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    const AssetDef* a = r.asset;
    if (a->interactables.empty()) continue;
    for (size_t k = 0; k < a->interactables.size(); ++k) {
      const InteractDef& d = a->interactables[k];
      for (size_t l = 0; l < d.leaves.size(); ++l) {
        const Leaf& lf = d.leaves[l];
        if (lf.colliders.empty() || (r.leafSt[k][l] & LEAF_GONE)) continue;
        vec3 c = vec3(leafMatrix(i, lf, d, r.progress[k]) * vec4(lf.colliders[0].center, 1));
        float dist = glm::length(c - p);
        if (dist > radius * 1.6f) continue;
        bool hasGlass = lf.glass.count > 0;
        if (hasGlass && dist < radius * 1.6f) breakGlass(i, r, (int)k, (int)l, c);
        if (leafBreakable(d) && dist < radius) {
          vec3 J = glm::normalize(c - p + vec3(0, 0.2f, 0)) * (isDoor(d) ? 120.0f : 40.0f) * (1 - dist / radius);
          detachLeaf(i, r, (int)k, (int)l, J, c, isShutter(d));
        }
      }
    }
  }
}

void App::stepDynLeaves(float dt) { for (auto& x : dynLeaves) x->age += dt; }

void App::dynLeafDrawItems(std::vector<DrawItem>& op) {
  for (auto& x : dynLeaves) {
    auto it = rt.find(x->inst); if (it == rt.end()) continue;
    const Leaf& lf = it->second.asset->interactables[x->k].leaves[x->l];
    vec3 p; quat q;
    if (!Physics::getTransform(x->body, p, q)) continue;
    op.push_back({x->fractured ? x->fractured.get() : &lf.opaque, glm::translate(mat4(1), p) * glm::mat4_cast(q)});
  }
}

void App::resetLeafDamage() {
  for (auto& x : dynLeaves) { Physics::remove(x->body); if (x->fractured) x->fractured->destroy(); }
  dynLeaves.clear();
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    bool changed = false;
    for (size_t k = 0; k < r.leafSt.size(); ++k)
      for (size_t l = 0; l < r.leafSt[k].size(); ++l) {
        auto s = i.leafState.find(r.asset->interactables[k].id + "/" + std::to_string(l));
        uint8_t want = s == i.leafState.end() ? 0 : s->second == 2 ? (LEAF_GONE | LEAF_GLASS_BROKEN) : LEAF_GLASS_BROKEN;
        if (r.leafSt[k][l] != want || r.leafHits[k][l] != 0) changed = true;
      }
    if (changed) buildRT(i, r);   // recreates leaf bodies from the authored / applied state
  }
}
