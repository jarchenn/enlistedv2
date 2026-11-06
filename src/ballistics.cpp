// Surface damage and penetration (the guide's "BallisticsManager").
//
// Surface types: Glass, WoodShutter, WoodDoor, Steel (doors / flaps whose sound is "steel"), wall families
// (masonry / concrete / timber from the panel), Earth (terrain), Sandbag, Foliage.
// Openings keep a per-copy session overlay (InstanceRT::leafSt / leafHits); "Apply damage to map" stores it.
//   Glass   HP 1  -> glass mesh hidden, the pane no longer stops rounds, shard burst
//   Shutter HP 2  -> detaches: Jolt dynamic body, impulse I = V_bullet * M_force at the hit, fractured mesh
//   Door    HP 10 -> detaches: dynamic body, angular impulse tau = (P_hit - hinge) x I
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

float App::leafHpFor(const InteractDef& d) const {
  if (isSteel(d)) return 1e9f;
  if (d.hp > 0) return d.hp;
  if (isDoor(d)) return 10;
  if (isShutter(d)) return 2;
  return 1e9f;   // window sashes: only their glass breaks
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

void App::detachLeaf(Instance& i, InstanceRT& r, int k, int l, vec3 impulse, vec3 at, bool fracture) {
  if (r.leafSt[k][l] & LEAF_GONE) return;
  const InteractDef& d = r.asset->interactables[k];
  const Leaf& lf = d.leaves[l];
  r.leafSt[k][l] |= LEAF_GONE | LEAF_GLASS_BROKEN; ++leafBreaks;
  vec3 pos = vec3(instanceMatrix(i) * vec4(lf.hinge, 1));
  quat q = yawQuat(i.yaw) * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
  Physics::remove(r.leafBodies[k][l]); r.leafBodies[k][l] = 0xffffffffu;
  auto dl = std::make_unique<DynLeaf>();
  dl->inst = i.id; dl->k = k; dl->l = l;
  float mass = isDoor(d) ? 28.0f : 7.0f;
  dl->body = Physics::addDynamic(lf.shape, pos, q, (i.id << 16) | 0xFFF0u, mass);
  if (fracture) {   // shutter: split the struck plank around the hit (a ragged gap where the rounds went through)
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
  if (isDoor(d)) {
    vec3 tau = glm::cross(at - pos, impulse);   // rotational impulse about the (broken) hinge line
    Physics::addAngularImpulse(dl->body, tau);
    Physics::addImpulse(dl->body, impulse * 0.5f, at);
  } else Physics::addImpulse(dl->body, impulse, at);
  // the hinges tear out: the leaf drops away from its frame instead of staying wedged in the reveal
  vec3 bc = i.pos + yawQuat(i.yaw) * ((r.asset->bmin + r.asset->bmax) * 0.5f);
  vec3 out = vec3(at.x - bc.x, 0, at.z - bc.z);
  if (glm::dot(out, out) > 1e-6f) Physics::addImpulse(dl->body, glm::normalize(out) * mass * 0.6f, pos);
  audio.play(isDoor(d) ? "door_break" : "shutter_break", at, 0.9f);
  fx.impact(at, -glm::normalize(impulse + vec3(0, 1e-4f, 0)), "wood");
  dynLeaves.push_back(std::move(dl));
  // cap the number of live rigid bodies: the oldest becomes static where it lies
  size_t live = 0; for (auto& x : dynLeaves) live += !x->frozen;
  for (auto& x : dynLeaves) {
    if (live <= maxDynLeaves) break;
    if (x->frozen) continue;
    vec3 p; quat qq;
    Physics::getTransform(x->body, p, qq);
    const Leaf& L = rt[x->inst].asset->interactables[x->k].leaves[x->l];
    Physics::remove(x->body);
    x->body = Physics::addStatic(L.shape, p, qq, (x->inst << 16) | 0xFFF0u);
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
  if (low == 0) return false;   // any other static shell stops the round
  Instance* inst = doc.find(iid);
  auto it = rt.find(iid);
  if (!inst || it == rt.end()) return false;
  InstanceRT& r = it->second;
  vec3 dir = glm::normalize(p.v);
  float speed = glm::length(p.v);
  float mu = kMuWood;
  int k = -1, l = -1; bool onGlass = false;
  if (low == 0xFFF0u) { surface = "wood"; }   // loose debris from a detached leaf
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
        if (!isSteel(d) && (isDoor(d) || isShutter(d)) && dist < radius) {
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
