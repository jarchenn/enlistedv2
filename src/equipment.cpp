// Hand equipment (complete update E3 / E5): Mk 2 and M24 grenades, the knife.
//
// Grenades are carried items of the weapon roster (action "grenade": Q cycles to them, LMB throws; H throws the
// side's grenade from any weapon and returns to it). One throw per press: a short wind-up, then release - the fuse
// starts at release, never earlier (no cooking). The thrown grenade is a small Jolt dynamic body (linear-cast CCD,
// tuned restitution / friction, damping so it settles) drawn with the supplied model; when its fuse runs out it
// goes off exactly once through the shared explode() path with the grenade wall stamp (chips, no launcher holes),
// a shallow crater, glass / leaf / tree / defence / wire effects and the blast exposure readout. Swapping during the
// wind-up cancels the throw: nothing spawns and the count is unchanged. The spawn point is pulled back along the
// eye ray if it would be inside a wall. All numbers are game tuning ("grenade" block of the weapon JSON).
//
// The knife (action "melee") strikes on LMB: wind-up, then one sweep of a few eye rays over a small arc within reach;
// the first hit of each ray counts and each target is hit at most once per swing; walls end the ray. A hit acts
// through the bullet-impact rules with a slow, heavy "projectile" (glass breaks, doors / shutters take a hit, trunks
// and defences register), so it can never pass through anything. There is no actor health in this engine.
#include "app.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>

static float gf(const json& j, const char* k, float d) { return j.is_object() ? j.value(k, d) : d; }

void App::loadEquipment(WeaponDef& d, const json& j) {
  if (j.contains("grenade")) {
    const json& g = j["grenade"]; d.grenade.on = true;
    d.grenade.fuse = gf(g, "fuse", 4.5f); d.grenade.speed = gf(g, "throwSpeed", 15.0f); d.grenade.lobDeg = gf(g, "lobDeg", 10.0f);
    d.grenade.restitution = gf(g, "restitution", 0.3f); d.grenade.friction = gf(g, "friction", 0.6f); d.grenade.mass = gf(g, "mass", 0.6f);
    d.grenade.damage = gf(g, "damage", 80.0f); d.grenade.radius = gf(g, "radius", 5.0f); d.grenade.crater = gf(g, "crater", 0.12f);
    d.grenade.linDamp = gf(g, "linearDamping", 0.4f); d.grenade.rollDamp = gf(g, "rollingDamping", 3.0f);
    d.grenade.windup = gf(g, "windup", 0.32f); d.grenade.ready = gf(g, "ready", 0.9f); d.grenade.volume = gf(g, "volume", 0.8f);
  }
  if (j.contains("melee")) {
    const json& m = j["melee"]; d.melee.on = true;
    d.melee.reach = gf(m, "reach", 1.7f); d.melee.arcDeg = gf(m, "arcDeg", 40.0f); d.melee.rays = (int)gf(m, "rays", 5.0f);
    d.melee.windup = gf(m, "windup", 0.12f); d.melee.strike = gf(m, "strike", 0.1f); d.melee.cooldown = gf(m, "cooldown", 0.6f); d.melee.damage = gf(m, "damage", 35.0f);
  }
}

// ---------------------------------------------------------------- grenades
void App::throwGrenade(const WeaponDef& d, WeaponState& s) {
  vec3 e = eyePos(), f = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  vec3 rt = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  vec3 half = d.useModel() ? glm::max((d.model->bmax - d.model->bmin) * 0.5f, vec3(0.025f)) : vec3(0.03f, 0.03f, 0.06f);
  float reach = std::max({half.x, half.y, half.z}) + 0.04f;   // the whole grenade, not just its centre, stays clear
  vec3 want = e + f * (0.35f + reach) + rt * 0.12f - vec3(0, 0.05f, 0);
  // never spawn inside a wall: stop short of the first surface on the eye -> spawn path by the grenade's own size
  Physics::Hit h; vec3 sp = want;
  if (Physics::castRayEx(e, want - e, glm::length(want - e) + reach, h)) sp = e + glm::normalize(want - e) * std::max(0.0f, h.t - reach);
  // release velocity: aim lifted by the lob angle, plus the thrower's own movement
  vec3 up = glm::cross(rt, f);
  float lob = radians(d.grenade.lobDeg);
  vec3 dir = glm::normalize(f * std::cos(lob) + up * std::sin(lob));
  auto ch = Physics::character();
  float postureK = ch.posture == Physics::PRONE ? 0.6f : ch.crouched ? 0.85f : 1.0f;   // a lying or crouching throw is shorter
  vec3 v = dir * d.grenade.speed * postureK + vec3(ch.vel.x, 0, ch.vel.z);
  Part p; p.center = vec3(0); p.size = half * 2.0f;
  ShapePtr shape = Physics::buildShape({p});
  quat q = glm::quatLookAt(-dir, vec3(0, 1, 0));   // model +z along the throw
  uint64_t ud = (0xE0ull << 56) | (uint64_t)(++grenadeSerial);   // not an instance id: never mistaken for map content
  uint32_t body = Physics::addDynamic(shape, sp, q, ud, d.grenade.mass);
  Physics::setMaterial(body, d.grenade.friction, d.grenade.restitution, true);
  Physics::setDamping(body, d.grenade.linDamp, d.grenade.rollDamp);   // grass / soil rolling resistance: it settles, it does not roll on
  Physics::setVelocity(body, v, glm::cross(dir, vec3(0, 1, 0)) * -9.0f);   // end over end
  LiveGrenade g; g.body = body; g.fuse = d.grenade.fuse; g.def = weaponIndex(d.id); g.spawn = sp; g.v0 = v;
  grenades.push_back(g);
  ++grenadesThrown; lastGrenadeSpawn = sp; lastGrenadeWant = want;
  audio.play("click", e, 0.6f);
}

void App::stepGrenades(float dt) {
  for (auto& g : grenades) {
    if (g.done) continue;
    g.fuse -= dt; g.age += dt;
    vec3 p; quat q;
    if (Physics::getTransform(g.body, p, q)) { g.pos = p; g.rot = q; }
    if (g.fuse > 0) continue;
    g.done = true;   // exactly one detonation, whatever happens later this frame
    const WeaponDef& d = *weaponDefs[g.def];
    Physics::remove(g.body); g.body = 0xffffffffu;
    explode(g.pos + vec3(0, 0.05f, 0), vec3(0, -1, 0), d.grenade.damage, d.grenade.radius, d.grenade.crater, ~0ull, WALL_GRENADE);
    fx.explosion(g.pos, vec3(0, 1, 0), "grenade", true);
    audio.play("explosion_grenade", g.pos, d.grenade.volume);
    ++grenadesExploded; lastGrenadeBlast = g.pos;
  }
  grenades.erase(std::remove_if(grenades.begin(), grenades.end(), [](const LiveGrenade& g) { return g.done; }), grenades.end());
}

void App::clearGrenades() {
  for (auto& g : grenades) Physics::remove(g.body);
  grenades.clear(); quickReturn = -1;
}

void App::grenadeDrawItems() {
  for (auto& g : grenades) {
    const WeaponDef& d = *weaponDefs[g.def];
    if (!d.useModel()) continue;
    vec3 c = (d.model->bmin + d.model->bmax) * 0.5f;
    mat4 M = glm::translate(mat4(1), g.pos) * glm::mat4_cast(g.rot) * glm::translate(mat4(1), -c);
    for (size_t k = 0; k < d.model->parts.size(); ++k) R.worldModels.push_back({d.model.get(), (int)k, M});
  }
}

// ---------------------------------------------------------------- per-frame: grenade / melee as the active item
void App::stepEquipment(float dt, const ExploreInput& in, const WeaponDef& d, WeaponState& s, bool fireHeld) {
  if (d.grenade.on) {
    // ready the next one from the pouch once the hand is free
    if (s.loaded == 0 && s.cooldown <= 0 && !s.containers.empty()) { s.containers.pop_back(); s.loaded = 1; }
    if (s.throwT < 0 && fireHeld && s.triggerReleased && s.loaded > 0 && s.cooldown <= 0) { s.throwT = 0; s.triggerReleased = false; }
    if (s.throwT >= 0) {
      s.throwT += dt;
      if (s.throwT >= d.grenade.windup) {   // release: the fuse starts now
        throwGrenade(d, s);
        s.loaded -= 1; s.throwT = -1; s.cooldown = d.grenade.ready;
        if (quickReturn >= 0) quickReturnT = 0.45f;
      }
    }
    if (s.loaded == 0 && s.containers.empty() && s.throwT < 0) weaponMsg = d.name + ": none left";
  }
  if (d.melee.on) {
    if (s.throwT < 0 && fireHeld && s.triggerReleased && s.cooldown <= 0) { s.throwT = 0; s.triggerReleased = false; meleeHits.clear(); s.struck = false; }
    if (s.throwT >= 0) {
      s.throwT += dt;
      if (!s.struck && s.throwT >= d.melee.windup) { s.struck = true; meleeSweep(d); }
      if (s.throwT >= d.melee.windup + d.melee.strike) { s.throwT = -1; s.cooldown = d.melee.cooldown; }
    }
  }
  (void)in;
}

void App::meleeSweep(const WeaponDef& d) {
  vec3 e = eyePos();
  int n = std::max(1, d.melee.rays);
  ++meleeSwings;
  for (int k = 0; k < n; ++k) {
    float a = n == 1 ? 0.0f : (k / float(n - 1) - 0.5f) * d.melee.arcDeg;
    vec3 f = forward(eyeYaw + a, eyePitch - 4.0f);
    Physics::Hit h;
    if (!Physics::castRayEx(e, f, d.melee.reach, h)) continue;
    uint64_t key = h.ud == 0 ? 0 : h.ud;
    if (meleeHits.count(key)) continue;   // one hit per target per swing
    meleeHits.insert(key);
    Projectile p; p.p = h.pos - f * 0.02f; p.v = f * 20.0f; p.mass = 0.3f; p.dmg = d.melee.damage; p.kind = PROJ_BULLET;
    size_t spawned = spawnedProjectiles.size();
    std::string surface = (h.ud == 0 || (h.ud >> 60) == 0xF) ? "earth" : "wood";
    bulletImpact(p, h, surface);
    spawnedProjectiles.resize(spawned);   // a blade never continues through anything
    fx.impact(h.pos, h.normal, surface);
    std::string km = surface == "glass" ? "" : surface == "metal" || surface == "steel" ? "metal" : surface == "wood" || surface == "wood_door" || surface == "wood_shutter" ? "wood" : surface == "masonry" || surface == "concrete" || surface == "stone" ? "concrete" : "generic";
    audio.play(km.empty() ? "glass_break" : "knife_" + km, h.pos, 0.6f);
    ++meleeHitCount; lastMeleeHit = h.pos; lastMeleeUd = h.ud;
  }
}

// procedural hand motion for the held item (no animations in the supplied GLBs)
mat4 App::equipmentPose(const WeaponDef& d, const WeaponState& s) const {
  if (d.grenade.on) {
    if (s.loaded == 0 && s.throwT < 0) return glm::translate(mat4(1), vec3(0, -0.6f, 0));   // hand empty: out of view
    if (s.throwT < 0) return mat4(1);
    float u = std::clamp(s.throwT / std::max(0.01f, d.grenade.windup), 0.0f, 1.0f);
    float back = std::sin(u * 3.14159f * 0.5f);                                // draw back and up, then release
    return glm::translate(mat4(1), vec3(0.05f * back, 0.12f * back, -0.18f * back)) * glm::mat4_cast(glm::angleAxis(radians(-35.0f * back), vec3(1, 0, 0)));
  }
  if (d.melee.on && s.throwT >= 0) {
    float w = d.melee.windup, st = d.melee.strike, t = s.throwT;
    float pull = t < w ? t / w : std::max(0.0f, 1.0f - (t - w) / st);
    float thrust = t < w ? 0.0f : std::sin(std::min(1.0f, (t - w) / st) * 3.14159f);
    return glm::translate(mat4(1), vec3(-0.03f * thrust, 0.02f * pull, -0.08f * pull + 0.35f * thrust)) * glm::mat4_cast(glm::angleAxis(radians(-12.0f * thrust), vec3(0, 1, 0)));
  }
  return mat4(1);
}
