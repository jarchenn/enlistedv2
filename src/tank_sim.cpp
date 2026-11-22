// Tanks, Explore simulation: session state, driving, turret / gun, hatch MG, HE/AP loading, damage rules, wrecks,
// entry / exit, effects and persistence. docs/tanks.md
#include "app.h"
#include "fxdata.h"   // Battle Foundry 6 (fxrecoil)
#include "mpai.h"
#include "game.h"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace {
float approach(float v, float t, float step) { float d = t - v; return std::fabs(d) <= step ? t : v + (d > 0 ? step : -step); }
float wrap180(float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; }
uint64_t bodyUD(uint64_t id) { return id << 16; }
const char* shellName(int s) { return s == SHELL_HE ? "HE" : s == SHELL_AP ? "AP" : "-"; }
int shellFrom(const std::string& s) { return s == "HE" ? SHELL_HE : s == "AP" ? SHELL_AP : SHELL_NONE; }
// rate- and acceleration-limited approach to a target angle (stops at the target without overshoot)
void slew(float& a, float& v, float delta, float rate, float accel, float dt) {
  float want = 0;
  if (std::fabs(delta) > 1e-4f) want = (delta > 0 ? 1.0f : -1.0f) * std::min(rate, std::sqrt(2.0f * accel * std::fabs(delta)));
  v = approach(v, want, accel * dt);
  float step = v * dt;
  if ((delta > 0 && step > delta) || (delta < 0 && step < delta)) { step = delta; v = 0; }
  a += step;
}
}

// ============================================================ rig
mat4 TankSys::hullMatrix(const TankRT& t) const {
  return glm::translate(mat4(1), t.pos + t.shove) * glm::mat4_cast(yawQuat(t.yaw)) *
         glm::rotate(mat4(1), radians(-(t.pitch + t.suspPitch)), vec3(1, 0, 0)) * glm::rotate(mat4(1), radians(t.roll + t.suspRoll), vec3(0, 0, 1));
}
void TankSys::rig(const TankRT& t, mat4& hull, mat4& turret, mat4& gun, mat4& recoil) const {
  const TankDef& d = *t.def;
  hull = hullMatrix(t);
  turret = hull * glm::translate(mat4(1), d.turretPivot) * glm::rotate(mat4(1), radians(t.tYaw), vec3(0, 1, 0)) * glm::translate(mat4(1), -d.turretPivot);
  mat4 g = glm::translate(mat4(1), d.gunPivot) * glm::rotate(mat4(1), radians(-t.gElev), vec3(1, 0, 0));
  gun = turret * g * glm::translate(mat4(1), -d.gunPivot);
  float r = t.recoilT, off = 0, stroke = 1.3f * d.recoil;   // RC2: a longer, faster stroke and a slower run-out back to battery
  if (gFxRecoil && !d.autoGun) {   // fxrecoil (spec 7.2): the brake's curve on the rig's own stroke (travelFraction = D / L), the row's timing at every tier,
    const FxGunRow& g = fxGunFor(d.id);   // moving its pre-exit travel before the flash from Medium up
    off = fxGunRecoilX(r + (gFxTier >= 2 ? fxGunPreTime(g) : 0.0f), g) * (stroke / std::max(g.L, 1e-3f));
    if (gFxTier >= 2 && r < 0.2f) off += 0.004f * std::sin(r * 6.2831853f * 35.0f) * std::exp(-r / 0.06f);   // mantlet shudder, 2-6 mm over 0-200 ms (7.4)
  }
  else if (r < 0.03f) off = stroke * (r / 0.03f);
  else if (r < 1.33f) { float u = (r - 0.03f) / 1.3f; off = stroke * (1 - u * u * (3 - 2 * u)); }
  recoil = turret * g * glm::translate(mat4(1), vec3(0, 0, -off)) * glm::translate(mat4(1), -d.gunPivot);
}
vec3 TankSys::muzzleWorld(const TankRT& t, vec3& dir) const {
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  dir = glm::normalize(vec3(g * vec4(0, 0, 1, 0)));
  return vec3(g * vec4(t.def->muzzle, 1));
}
mat4 TankSys::mgMatrix(const TankRT& t) const {
  const TankDef& d = *t.def;
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  return tu * glm::translate(mat4(1), d.mgPivot) * glm::rotate(mat4(1), radians(t.mgYaw), vec3(0, 1, 0)) * glm::rotate(mat4(1), radians(-t.mgElev), vec3(1, 0, 0));
}
vec3 TankSys::mgMuzzleWorld(const TankRT& t, vec3& dir) const {
  const TankDef& d = *t.def;
  mat4 M = mgMatrix(t);
  dir = glm::normalize(vec3(M * vec4(0, 0, 1, 0)));
  if (!d.mgParts.empty()) return vec3(M * vec4(d.mgMuzzle - d.mgPivot, 1));
  // weapon model on the mount: the registry weapon's measured muzzle (weapon space) through the mount offset
  extern App* gApp;
  int wi = gApp ? gApp->weaponIndex(d.mgWeapon) : -1;
  vec3 wm = wi >= 0 ? gApp->weaponDefs[wi]->muzzle : vec3(0, 0, 1.2f);
  return vec3(M * glm::translate(mat4(1), d.mgModelOffset) * vec4(wm, 1));
}

void TankSys::stripDamage(json& extra) {
  if (!extra.is_object() || !extra.contains("tank") || !extra["tank"].is_object()) return;
  for (const char* k : {"hp", "wrecked", "hitsFront", "hitsSide", "loaded", "reserveHE", "reserveAP"}) extra["tank"].erase(k);
  if (extra["tank"].empty()) extra.erase("tank");
}

// ============================================================ session
void TankSys::groundPose(App& app, const TankDef& d, vec3 pos, float yaw, float& y, float& pitch, float& roll, float& slopeAlong, uint64_t self) const {
  quat q = yawQuat(yaw);
  float G = d.trackHalfGauge, L = d.trackHalfLen;
  // ED1: a flat object top within reach below the hull (a bridge deck, a parking deck) is the ground there, not only the terrain
  auto h = [&](float x, float z) {
    vec3 w = pos + q * vec3(x, 0, z); float g = app.terrain.height(w.x, w.z);
    vec3 o(w.x, pos.y + 1.0f, w.z);
    for (int k = 0; k < 4; ++k) {
      Physics::Hit hh; if (!Physics::castRayEx(o, vec3(0, -1, 0), 3.0f, hh, self ? self << 16 : ~0ull)) break;   // its own hull (the ray starts inside it) skipped
      uint64_t id = hh.ud >> 16;
      if (id && (id == self || std::none_of(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) { return i.id == id; }))) { o.y = hh.pos.y - 0.01f; continue; }
      if (id && hh.normal.y > 0.9f) g = std::max(g, hh.pos.y);
      break;
    }
    return g;
  };
  float fl = h(G, L), fr = h(-G, L), rl = h(G, -L), rr = h(-G, -L);
  float front = 0.5f * (fl + fr), rear = 0.5f * (rl + rr), left = 0.5f * (fl + rl), right = 0.5f * (fr + rr);
  // the hull rests on the higher track contacts (no sinking into a crater rim)
  y = std::max(0.25f * (fl + fr + rl + rr), std::max(std::max(fl, fr), std::max(rl, rr)) - 0.35f);
  pitch = degrees(std::atan2(front - rear, 2 * L));
  roll = degrees(std::atan2(left - right, 2 * G));
  slopeAlong = pitch;
}

static void initFromInstance(App& app, TankSys& sys, TankRT& t, const Instance& i, const TankDef& d) {
  TankRT n;
  n.id = i.id; n.def = &d; n.body = t.body; n.shapeYaw = 1e9f;
  n.engineLoop = t.engineLoop; n.trackLoop = t.trackLoop; n.turretLoop = t.turretLoop;
  n.pos = i.pos; n.yaw = i.yaw;
  json o = i.extra.is_object() && i.extra.contains("tank") ? i.extra["tank"] : json::object();
  n.tYaw = o.value("turretYaw", 0.0f);
  n.hp = o.value("hp", 100); n.wrecked = o.value("wrecked", false);
  n.hitsFront = o.value("hitsFront", 0); n.hitsSide = o.value("hitsSide", 0);
  n.reserveHE = o.value("reserveHE", d.reserveHE); n.reserveAP = o.value("reserveAP", d.reserveAP);
  n.loaded = o.contains("loaded") ? shellFrom(o.value("loaded", "")) : shellFrom(d.startLoaded);
  n.selected = n.loaded != SHELL_NONE ? n.loaded : SHELL_AP;
  n.mgLoaded = d.mgBelt; n.mgReserve = d.mgReserve;
  n.bowLoaded = d.bowOn ? d.bowBelt : 0; n.bowReserve = d.bowOn ? d.bowReserve : 0;
  n.towLeft = d.towCount;
  n.aiSide = d.mech ? o.value("aiSide", -1) : -1;   // Mech Tank: the owning side (spawnMech)
  if (n.wrecked) { n.hp = 0; n.loaded = SHELL_NONE; n.burnT = 0; }
  if (d.heli) { n.gunAmmo = d.gun30Ammo; n.rockets = d.rocketCount; n.grounded = true; n.rpm = 0; if (n.wrecked) n.roll = 18; t = n; return; }   // VH5: sits at the instance's height (a raised pad)
  float y, p, r, s; sys.groundPose(app, d, n.pos, n.yaw, y, p, r, s, n.id);
  n.pos.y = y; n.pitch = p; n.roll = r;
  t = n;
}

void TankSys::sync(App& app) {
  std::set<uint64_t> live;
  for (auto& [idx, rp] : app.activeSet().vehicles) {   // tanks only (app.cpp), not every instance
    const Instance& i = app.doc.instances[idx]; InstanceRT* it = rp;
    const TankDef* d = defFor(it->asset);
    if (!d) continue;
    live.insert(i.id);
    auto& t = tanks[i.id];
    bool fresh = t.def != d || t.id != i.id;
    if (fresh || t.rev != i.rev) {
      if (fresh || app.mode == Mode::Creative) initFromInstance(app, *this, t, i, *d);
      t.rev = i.rev;
    }
    if (t.body != it->staticBody) { t.body = it->staticBody; t.shapeYaw = 1e9f; }
    updateBody(app, t, false);
  }
  for (auto it = tanks.begin(); it != tanks.end();) {
    if (!live.count(it->first) && !it->second.adopted) {   // BR7: adopted cars are not catalog vehicles
      for (int* h : {&it->second.engineLoop, &it->second.trackLoop, &it->second.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
      if (occupied == it->first) occupied = 0;
      it = tanks.erase(it);
    } else ++it;
  }
}

void TankSys::reset(App& app) {
  occupied = 0; target = 0; fireArmed = false; zoom = false; lastHit = {};
  resetRams(app);
  for (auto& [id, t] : tanks) {
    for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
    Instance* i = app.doc.find(id);
    if (i && t.def) initFromInstance(app, *this, t, *i, *t.def);
    t.shapeYaw = 1e9f;
    updateBody(app, t, true);
  }
}

// collision: one compound per tank on the instance's own static body (user data id << 16, so every collider resolves
// to the tank), hull boxes plus turret boxes rotated to the achieved traverse; rebuilt when the turret has moved > 2 deg
void TankSys::updateBody(App& app, TankRT& t, bool force) {
  if (t.body == 0xffffffffu || !t.def) return;
  (void)app;
  const TankDef& d = *t.def;
  if (!t.adopted && (force || t.shapeYaw > 1e8f || (!d.heli && std::fabs(wrap180(t.tYaw - t.shapeYaw)) > 2.0f))) {   // VH5: the chin gun's tYaw moves no collider   // BR7: an adopted car keeps its asset's exact collision
    std::vector<Part> parts;
    for (auto& b : d.hullBoxes) { Part p; p.center = b.c; p.size = b.h * 2.0f; parts.push_back(p); }
    quat ty = glm::angleAxis(radians(t.tYaw), vec3(0, 1, 0));
    for (auto& b : d.turretBoxes) { Part p; p.center = d.turretPivot + ty * (b.c - d.turretPivot); p.size = b.h * 2.0f; p.rot = ty; parts.push_back(p); }
    Physics::setShape(t.body, Physics::buildShape(parts));
    t.shapeYaw = t.tYaw;
  }
  mat4 H = hullMatrix(t);
  Physics::setTransform(t.body, t.pos, glm::quat_cast(glm::mat3(H)));
}

// ============================================================ per step
void TankSys::stepAll(App& app, float dt, const ExploreInput& in) {
  double t0 = glfwGetTime();
  // Enter prompt: a ray at the crosshair resolves any tank collider to its owner (line of sight is the ray itself)
  target = 0;
  if (!occupied && !app.occupied) {
    Physics::Hit h;
    vec3 e = app.eyePos(), f = App::forward(app.eyeYaw, app.eyePitch);
    if (Physics::castRayEx(e, f, 4.0f, h) && (h.ud & 0xFFFF) == 0) {
      auto it = tanks.find(h.ud >> 16);
      if (it != tanks.end() && it->second.aiSide < 0) target = it->first;   // Mech Tank: crewless, no Enter prompt
    }
  }
  for (auto& [id, t] : tanks) { if (t.aiSide >= 0 && id != occupied && !t.wrecked) mechThink(app, t, dt); stepOne(app, t, dt, id == occupied ? &in : nullptr); }   // Mech Tank: its AI sets the controls first
  stepTows(app, dt);   // VH7
  // the occupant's body rides with the vehicle: everything that asks where the player is (the local soldier's feet, AI
  // targeting and LOD, enemies' chase) follows the vehicle instead of the spot where the player got in
  if (occupied) if (TankRT* t = get(occupied); t && !(t->def && t->def->heli)) Physics::setCharacterPos(t->pos + vec3(0, 1.0f, 0));   // the helicopter places its own (heli.cpp)
  stepKnocked(app, dt);
  stepMs = (glfwGetTime() - t0) * 1000.0; stepMsMax = std::max(stepMsMax, stepMs);
}

bool TankSys::handleInteract(App& app, const ExploreInput& in) {
  if (!in.interact || occupied || !target) return false;
  TankRT* t = get(target);
  if (t && t->wrecked) { app.toast(t->def->name + " is destroyed.", 3); return true; }
  if (t && !t->def->car && !t->def->heli && !mpaiMayDriveTanks(app)) { app.toast("Only a Tanker can drive tanks.", 3); return true; }   // VH5: anyone may fly   // GM1 (cars: anyone)
  enter(app, target);
  return true;
}

bool TankSys::enter(App& app, uint64_t id) {
  TankRT* t = get(id);
  if (!t || !t->def) return false;
  if (t->wrecked) { app.toast("The tank is destroyed.", 3); return false; }
  if (app.occupied) { app.toast("Leave the gun first.", 3); return false; }
  if (occupied && occupied != id) return false;
  occupied = id; ++entries;
  t->station = TS_GUNNER; t->hatchT = 0;
  fireArmed = false; zoom = false; zoomT = 0; aimPrev = true; crouchLatch = true;
  app.build.active = false; app.adsT = 0; app.vault.active = false; app.choiceFor = 0;
  if (t->def->heli) {   // VH5: the pilot; the chase camera looks along the nose, the rotor spins up
    vec3 f = yawQuat(t->yaw) * vec3(0, 0, 1);
    app.eyeYaw = wrapDeg(degrees(std::atan2(-f.x, -f.z))); app.eyePitch = -8;
    t->vel = vec3(0); t->reloadPrev = true;
    Physics::setCharacterPos(t->pos + vec3(0, 0.6f, 0));
    app.audio.play("steel_close", t->pos + vec3(0, 1.5f, 4), 0.4f);
    app.toast("In " + t->def->name + " - rotor spinning up. Space up | Ctrl down | W/S forward / back | A/D strafe | mouse: nose + aim | LMB 30 mm | R rocket | E get out (landed)", 8);
    return true;
  }
  if (t->def->car) {   // VH1: the driver's seat, looking ahead through the windshield
    vec3 f = yawQuat(t->yaw) * vec3(0, 0, 1);
    app.eyeYaw = wrapDeg(degrees(std::atan2(-f.x, -f.z))); app.eyePitch = -9;
    Physics::setCharacterPos(t->pos + vec3(0, 0.6f, 0));
    app.audio.play("steel_close", t->pos + vec3(0, 1.2f, 0), 0.4f);
    app.toast("In " + t->def->name + ". W/S throttle / brake / reverse | A/D steer | Space handbrake | mouse look" +
              std::string(t->def->carGun() ? (t->def->rwsSight ? " | Ctrl gunner (RWS sight: mouse aim, LMB fire, RMB zoom)" : " | Ctrl pintle MG (LMB fire)") : "") + " | E get out", 7);
    return true;
  }
  // the desired aim starts on the gun's achieved line (no turret swing on entry)
  vec3 dir; muzzleWorld(*t, dir);
  app.eyeYaw = wrapDeg(degrees(std::atan2(-dir.x, -dir.z))); app.eyePitch = degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
  // the infantry body waits inside the hull (no collision update while crewing)
  Physics::setCharacterPos(t->pos + vec3(0, 1.0f, 0));
  app.audio.play("steel_close", t->pos + vec3(0, 2.5f, 0), 0.8f);
  if (t->def->towCount) app.toast("In " + t->def->name + " - gunner sight. W/S A/D drive | mouse aim | LMB 25 mm (hold) | R AP / HE | Space coax | Q " + t->def->towName + " | RMB zoom | Ctrl pop out of the hatch | E get out", 7);   // VH7
  else app.toast("In " + t->def->name + " - gunner sight. W/S A/D drive | mouse aim | LMB main gun | Space " + (t->def->bowOn ? t->def->bowShort : std::string("-")) +
            " | RMB zoom | R next shell HE/AP | Ctrl hatch (" + std::string(t->def->mgName.empty() ? "look out" : t->def->bowOn ? "LMB roof MG, Space both MGs" : "LMB / Space roof MG") + ", RMB main gun) | E get out", 7);   // VH8: only the guns it has
  return true;
}

bool TankSys::exitSpot(App& app, const TankRT& t, vec3& out, std::string& why) const {
  const TankDef& d = *t.def;
  mat4 H = hullMatrix(t);
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec3 hatch = vec3(tu * vec4(d.hatchEye, 1));
  uint64_t own = bodyUD(t.id);
  int blockedCap = 0, blockedLos = 0, noGround = 0;
  for (auto& e : d.exits) {
    vec3 p = vec3(H * vec4(e, 1));
    Physics::Hit gh;
    float top = std::max(p.y, t.pos.y) + 2.5f;
    if (!Physics::castRayEx(vec3(p.x, top, p.z), vec3(0, -1, 0), 6.0f, gh, own) || gh.normal.y < 0.7f) { ++noGround; continue; }
    p.y = gh.pos.y;
    if (std::fabs(p.y - app.terrain.height(p.x, p.z)) > 1.2f) { ++noGround; continue; }   // on top of a wall / roof: not a safe dismount
    if (Physics::capsuleBlocked(p + vec3(0, 0.08f, 0), Physics::standHeight() - 0.06f, Physics::radius())) { ++blockedCap; continue; }
    Physics::Hit lh; vec3 a = hatch, b = p + vec3(0, 1.2f, 0);
    if (Physics::castRayEx(a, b - a, glm::length(b - a), lh, own)) { ++blockedLos; continue; }   // never through a wall
    out = p; return true;
  }
  char b[160]; std::snprintf(b, sizeof b, "no safe exit: %d blocked by obstacles, %d behind walls, %d without ground", blockedCap, blockedLos, noGround);
  why = b;
  return false;
}

void TankSys::vacate(App& app) {
  (void)app;
  if (TankRT* t = get(occupied)) { t->station = TS_GUNNER; t->hatchT = 0; t->throttleIn = t->steerIn = 0; }
  occupied = 0; zoom = false; zoomT = 0;
}

uint64_t TankSys::spawnFresh(App& app, const std::string& assetId, vec3 near, float faceYaw, int station, float firstR) {
  const AssetDef* a = app.lib.find(assetId);
  if (!a || !defFor(a)) return 0;
  // the cap: the oldest wrecks go first (they would pile up with every respawn)
  bool removed = false;
  while ((int)wreckOrder.size() > wreckCap) {
    uint64_t old = wreckOrder.front(); wreckOrder.erase(wreckOrder.begin());
    if (old == occupied) continue;
    auto& I = app.doc.instances; size_t n0 = I.size();
    I.erase(std::remove_if(I.begin(), I.end(), [&](const Instance& i) { return i.id == old; }), I.end());
    removed |= I.size() != n0;
  }
  if (removed) { app.syncRuntime(); sync(app); }
  for (float r : {firstR, 7.0f, 10.0f, 14.0f, 18.0f, 24.0f, 30.0f})
    for (int k = 0; k < (r < 0.5f ? 1 : 12); ++k) {
      float an = k * 0.5235988f; vec3 p = near + vec3(std::cos(an) * r, 0, std::sin(an) * r); p.y = app.terrain.height(p.x, p.z);
      bool near2 = false; for (auto& [id, o] : tanks) if (glm::length(vec2(o.pos.x - p.x, o.pos.z - p.z)) < 8.0f) near2 = true;   // not on another vehicle
      if (near2 || !app.checkPlacement(a, p, faceYaw, 0).ok) continue;
      uint64_t id = app.placeInstance(a, p, faceYaw);
      if (!id) continue;
      app.syncRuntime(); sync(app);
      if (!enter(app, id)) return 0;
      if (TankRT* t = get(id); t && station == TS_HATCH && !t->def->heli) { t->station = TS_HATCH; t->hatchT = 1; }
      return id;
    }
  return 0;
}

bool TankSys::exitTank(App& app, bool force) {
  TankRT* t = get(occupied);
  if (!t) { occupied = 0; return true; }
  if (!force && std::fabs(t->speed) > 1.0f) {
    ++blockedExits; lastExitWhy = "moving";
    char b[96]; std::snprintf(b, sizeof b, "Slow down to get out (%.0f km/h).", std::fabs(t->speed) * 3.6f);
    app.toast(b, 2.5); return false;
  }
  vec3 p; std::string why;
  if (!exitSpot(app, *t, p, why)) {
    if (!force) { ++blockedExits; lastExitWhy = why; app.toast("Cannot get out here: " + why + ".", 3.5); return false; }
    // knocked out with every exit blocked: widest search, then the start marker (no player-health system to apply)
    bool found = false;
    for (float r = 3.5f; r <= 9.0f && !found; r += 0.75f)
      for (int k = 0; k < 16 && !found; ++k) {
        float a = k * 6.2831853f / 16; vec3 q = t->pos + vec3(std::cos(a) * r, 0, std::sin(a) * r); q.y = app.terrain.height(q.x, q.z);
        if (!Physics::capsuleBlocked(q + vec3(0, 0.08f, 0), Physics::standHeight() - 0.06f, Physics::radius())) { p = q; found = true; }
      }
    if (!found) p = app.exploreStartPos;
  }
  Physics::setCharacterPos(p);
  // look back toward the tank from the dismount point
  vec3 to = t->pos + vec3(0, 1.5f, 0) - (p + vec3(0, 1.62f, 0));
  app.eyeYaw = wrapDeg(degrees(std::atan2(-to.x, -to.z))); app.eyePitch = std::clamp(degrees(std::atan2(to.y, glm::length(vec2(to.x, to.z)))), -30.0f, 30.0f);
  app.eyeHeight = app.move.eyeStand; app.postureWanted = Physics::STAND; app.proneToggle = false;
  t->station = TS_GUNNER; t->hatchT = 0; t->throttleIn = t->steerIn = 0;
  occupied = 0; ++exits; lastExitWhy = "";
  zoom = false; zoomT = 0;
  app.audio.play("steel_open", p, 0.7f);
  return true;
}

void TankSys::stepOne(App& app, TankRT& t, float dt, const ExploreInput* in) {
  const TankDef& d = *t.def;
  t.recoilT += dt; t.kickT += dt;
  if (t.wrecked && d.heli && t.falling) { stepHeliFall(app, t, dt); effects(app, t, dt); return; }   // VH5: going down
  if (t.wrecked) {
    t.speed = 0; t.yawRate = 0; t.reload = 0;
    for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
    effects(app, t, dt);
    return;
  }
  if (d.car) { stepCar(app, t, dt, in); return; }
  if (d.heli) { stepHeli(app, t, dt, in); return; }
  bool focused = app.selftest || glfwGetWindowAttrib(app.win, GLFW_FOCUSED);
  const bool ai = !in && t.aiSide >= 0;   // Mech Tank: the AI's controls (tank_mech.cpp)
  t.throttleIn = in ? std::clamp(in->move.y, -1.0f, 1.0f) : ai ? t.aiThrottle : 0.0f;
  bool rmbFire = false;
  t.steerIn = in ? std::clamp(in->move.x, -1.0f, 1.0f) : ai ? t.aiSteer : 0.0f;
  // ---- station: Left Ctrl toggles gunner sight <-> open hatch / roof MG once per press (edge, never crouches)
  if (in) {
    bool ctrl = in->crouch;
    if (crouchLatch) { if (!ctrl) crouchLatch = false; }   // a Ctrl held while entering does not toggle
    else if (ctrl && !ctrlPrev && t.hatchT == (t.station == TS_HATCH ? 1.0f : 0.0f)) {
      if (t.station == TS_GUNNER) {
        // overhead clearance above the hatch: a roof / overhang keeps the gunner position
        mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
        vec3 hp = vec3(tu * vec4(d.hatchEye - vec3(0, 0.6f, 0), 1));
        Physics::Hit oh;
        if (Physics::castRayEx(hp, vec3(0, 1, 0), 1.1f, oh, bodyUD(t.id))) app.toast("Hatch blocked overhead.", 2);
        else { t.station = TS_HATCH; ++hatchToggles; app.audio.play("steel_open", hp, 0.6f); }
      } else { t.station = TS_GUNNER; ++hatchToggles; app.audio.play("steel_close", t.pos + vec3(0, 2.6f, 0), 0.6f); }
      fireArmed = false;   // a station change needs a fresh press
    }
    ctrlPrev = ctrl;
    // RMB click: in the sight it toggles the magnification (zooms into the current view, eased over 0.18 s); from the
    // open hatch it fires the main gun (one round per click). The hatch drops the zoom.
    if (in->aim && !aimPrev && focused) {
      if (t.station == TS_GUNNER && t.hatchT == 0) zoom = !zoom;
      else if (t.station == TS_HATCH && t.hatchT >= 1.0f) rmbFire = true;
    }
    aimPrev = in->aim;
    if (t.station != TS_GUNNER) zoom = false;
    zoomT = approach(zoomT, zoom ? 1.0f : 0.0f, dt / 0.18f);
  }
  t.hatchT = approach(t.hatchT, t.station == TS_HATCH ? 1.0f : 0.0f, dt / 0.8f);
  // ---- drive (hull movement in both stations: single-player crew abstraction)
  drive(app, t, dt);
  t.clipDist += t.speed * dt;   // MT1: the track clip's phase (tank_ui.cpp)
  // ---- main gun follows the desired aim from the gunner sight, and from the open hatch (the commander lays it: RMB fires)
  if (ai) aimGun(t, dt, t.aiAimOn, &t.aiAim);
  else aimGun(t, dt, in && ((t.station == TS_GUNNER && t.hatchT == 0) || (t.station == TS_HATCH && t.hatchT >= 1.0f)));
  if (ai && t.aiFire && t.loaded != SHELL_NONE) { fireCannon(app, t); ++t.aiShots; }
  if (ai) t.aiFire = false;
  // ---- main gun: LMB in the sight (one round per click), RMB from the hatch; a fresh press after any change
  bool fireHeld = in && in->fire && focused;
  // stale input: after a pause menu / focus loss (a gap in the fixed steps) the trigger needs a fresh press
  if (in) { if (!focused || (!app.selftest && app.now - lastStepTime > 0.25)) fireArmed = false; else if (!fireHeld) fireArmed = true; lastStepTime = app.now; }
  auto cannon = [&]() {
    if (t.loaded != SHELL_NONE) fireCannon(app, t);
    else { app.audio.play("click", t.pos + vec3(0, 2, 0), 0.6f); app.toast(t.reload > 0 ? "Loading..." : "No round loaded.", 1.5); }
  };
  if (in && fireHeld && (fireArmed || d.autoGun) && t.station == TS_GUNNER && t.hatchT == 0) {   // VH7: an autocannon keeps firing while held
    if (d.autoGun) { fireArmed = false; if (t.loaded != SHELL_NONE) fireCannon(app, t); else if (t.reserveAP + t.reserveHE == 0 && t.mgCool <= 0) { app.audio.play("click", t.pos + vec3(0, 2, 0), 0.5f); t.mgCool = 0.4f; } }
    else { fireArmed = false; cannon(); }
  }
  if (d.towCount > 0) {   // VH7 TOW: Q fires one (gunner sight); the pair reloads one by one
    bool q = in && in->nextWeapon && focused;
    if (q && !t.towPrev && t.station == TS_GUNNER && t.hatchT == 0) fireTow(app, t);
    t.towPrev = q;
    if (t.towLeft < d.towCount) { if (t.towReloadT <= 0) t.towReloadT = d.towReload / d.towCount; t.towReloadT -= dt; if (t.towReloadT <= 0) { ++t.towLeft; t.towReloadT = 0; if (occupied == t.id) app.toast(d.towName + " ready (" + std::to_string(t.towLeft) + ")", 1.5); } }
  }
  if (in && rmbFire) cannon();
  // ---- machine guns: roof MG (hatch: LMB or Space), bow MG (Space, any station); belts change automatically
  stepMgs(app, t, dt, in, focused);
  // ---- HE/AP loading (toggle, then the timer); runs with or without the player inside (abstract crew loader)
  stepAmmo(app, t, dt, in && in->reload);
  // ---- exit
  if (in && in->interact) exitTank(app, false);
  updateBody(app, t, false);
  effects(app, t, dt);
}

// VH1 car step: driver seat (W/S throttle, brake, reverse; A/D steering, smoothed; Space handbrake; free head inside the
// cab) or, with a pintle MG, Ctrl to the gun (the car rolls to a stop on its handbrake; LMB / Space fire, belts change
// on their own). Entry / exit, collision, damage, wreck and effects are the tanks'.
void TankSys::stepCar(App& app, TankRT& t, float dt, const ExploreInput* in) {
  const TankDef& d = *t.def;
  bool focused = app.selftest || glfwGetWindowAttrib(app.win, GLFW_FOCUSED);
  if (in) {
    bool ctrl = in->crouch;
    if (crouchLatch) { if (!ctrl) crouchLatch = false; }
    else if (ctrl && !ctrlPrev && d.carGun() && t.hatchT == (t.station == TS_HATCH ? 1.0f : 0.0f)) {
      t.station = t.station == TS_HATCH ? TS_GUNNER : TS_HATCH; ++hatchToggles; fireArmed = false;
      app.audio.play("gun_move", t.pos + vec3(0, 1.4f, 0), 0.5f);
      if (d.rwsSight && t.station == TS_HATCH) {   // the sight opens on the gun's current line
        vec3 dir; mgMuzzleWorld(t, dir);
        app.eyeYaw = wrapDeg(degrees(std::atan2(-dir.x, -dir.z))); app.eyePitch = degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
      }
    }
    ctrlPrev = ctrl;
    // VH2 RWS sight: RMB toggles the magnification
    if (in->aim && !aimPrev && focused && d.rwsSight && t.station == TS_HATCH) zoom = !zoom;
    aimPrev = in->aim;
    if (t.station != TS_HATCH) zoom = false;
    zoomT = approach(zoomT, zoom ? 1.0f : 0.0f, dt / 0.18f);
  }
  t.hatchT = approach(t.hatchT, t.station == TS_HATCH ? 1.0f : 0.0f, dt / 0.6f);
  bool driving = in && t.station == TS_GUNNER && t.hatchT == 0;
  t.throttleIn = driving ? std::clamp(in->move.y, -1.0f, 1.0f) : 0.0f;
  t.steerIn = driving ? std::clamp(in->move.x, -1.0f, 1.0f) : 0.0f;
  t.handbrake = (driving && in->jumpHeld) || (in && !driving);   // at the MG (or getting there) the handbrake is on
  // the steering follows the keys at a limited rate (lock to lock in 2 / steerRate s) and recentres a little faster
  t.steer = approach(t.steer, t.steerIn, d.steerRate * (t.steerIn == 0 ? 1.4f : 1.0f) * dt);
  float yaw0 = t.yaw;
  drive(app, t, dt);
  if (in) {
    // the head turns with the car and stays inside a sensible range of the bonnet: +-110 deg, -50 .. +35 deg
    vec3 f = yawQuat(t.yaw) * vec3(0, 0, 1);
    float carEye = degrees(std::atan2(-f.x, -f.z));
    app.eyeYaw = wrapDeg(app.eyeYaw + wrap180(t.yaw - yaw0));
    float lim = t.station == TS_HATCH ? 180.0f : 110.0f;
    app.eyeYaw = wrapDeg(carEye + std::clamp(wrap180(app.eyeYaw - carEye), -lim, lim));
    app.eyePitch = std::clamp(app.eyePitch, t.pitch - 50.0f, t.pitch + (t.station == TS_HATCH ? std::max(35.0f, d.mgElevMax) : 35.0f));
    if (!focused || (!app.selftest && app.now - lastStepTime > 0.25)) fireArmed = false; else if (!(in->fire && focused)) fireArmed = true;
    lastStepTime = app.now;
  }
  if (d.carGun()) {
    // Space is the handbrake in the seat: the MG only hears it standing at the gun
    ExploreInput gin; const ExploreInput* gp = nullptr;
    if (in) { gin = *in; if (t.station != TS_HATCH) gin.jumpHeld = false; gp = &gin; }
    stepMgs(app, t, dt, gp, focused);
  }
  if (in && in->interact) exitTank(app, false);
  updateBody(app, t, false);
  effects(app, t, dt);
}

namespace {
// 2D separating-axis test of two yawed rectangles (centre xz, half extents x / z, yaw deg)
bool rectsOverlap(vec2 c1, vec2 h1, float y1, vec2 c2, vec2 h2, float y2) {
  auto axes = [](float yaw, vec2& ax, vec2& az) { float a = radians(yaw); ax = vec2(std::cos(a), -std::sin(a)); az = vec2(std::sin(a), std::cos(a)); };
  vec2 a1x, a1z, a2x, a2z; axes(y1, a1x, a1z); axes(y2, a2x, a2z);
  vec2 d = c2 - c1;
  for (vec2 L : {a1x, a1z, a2x, a2z}) {
    float r1 = h1.x * std::fabs(glm::dot(a1x, L)) + h1.y * std::fabs(glm::dot(a1z, L));
    float r2 = h2.x * std::fabs(glm::dot(a2x, L)) + h2.y * std::fabs(glm::dot(a2z, L));
    if (std::fabs(glm::dot(d, L)) > r1 + r2) return false;
  }
  return true;
}
}

// VH8: before the static obstacles: a vehicle or wreck the hull would overlap is shoved by the mass ratio. Twice the other's
// mass or more: it is carried along at nearly the full speed; equal mass: slowly; under 0.7 x: it holds (a tank does not
// push a heavier tank). The pushed one slides along the push and a little aside, stays on the ground, takes 1 durability per
// metre shoved (a wreck nothing), and holds when it would be pushed into something solid itself
bool TankSys::pushVehicles(App& app, TankRT& t, vec3& np, float ny, std::string& why) {
  const TankDef& d = *t.def;
  vec3 move = np - t.pos; move.y = 0;
  if (glm::length(move) < 1e-5f) return true;
  vec2 hT(d.halfSize.x, d.halfSize.z);
  for (auto& [oid, o] : tanks) {
    if (oid == t.id || !o.def || o.falling) continue;
    if (o.def->heli && o.pos.y - heliGround(app, o, o.pos) > 0.5f) continue;   // a helicopter in the air is not in the way
    vec2 hO(o.def->halfSize.x, o.def->halfSize.z);
    if (!rectsOverlap(vec2(np.x, np.z), hT, ny, vec2(o.pos.x, o.pos.z), hO, o.yaw)) continue;
    if (rectsOverlap(vec2(t.pos.x, t.pos.z), hT, t.yaw, vec2(o.pos.x, o.pos.z), hO, o.yaw) && glm::dot(vec2(o.pos.x - t.pos.x, o.pos.z - t.pos.z), vec2(move.x, move.z)) < 0) continue;   // already overlapping and moving away: let it go
    float k = std::clamp((d.massKg / std::max(1.0f, o.def->massKg) - 0.7f) / 1.3f, 0.0f, 1.0f);
    if (k < 0.05f) { why = "a heavier vehicle (" + o.def->name + ")"; return false; }
    vec3 step = move * k; np = t.pos + step;   // the pusher slows to the pushed one's pace
    vec3 fwd = glm::normalize(step), side = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
    float sgn = glm::dot(o.pos - t.pos, side) >= 0 ? 1.0f : -1.0f;
    vec3 shove = step + side * (sgn * glm::length(step) * 0.3f);
    vec3 to = o.pos + shove;
    for (int it = 0; it < 30 && rectsOverlap(vec2(np.x, np.z), hT, ny, vec2(to.x, to.z), hO, o.yaw); ++it) to += fwd * 0.03f + side * (sgn * 0.01f);   // clear of the pusher
    float oy, op, orl, os; groundPose(app, *o.def, to, o.yaw, oy, op, orl, os, o.id); to.y = oy;
    TankRT probe = o; probe.pos = to; mat4 OH = hullMatrix(probe); quat oq = glm::quat_cast(glm::mat3(OH));
    vec3 oc = vec3(OH * vec4(0, o.def->clearance + o.def->halfSize.y, 0, 1));
    if (!app.terrain.inBuildArea(to.x, to.z) || Physics::boxBlocked(oc, o.def->halfSize * 0.95f, oq, bodyUD(o.id), true)) { why = o.def->name + " is pinned"; return false; }
    float moved = glm::length(vec2(to.x - o.pos.x, to.z - o.pos.z));
    o.pos = to; o.pitch = op; o.roll = orl; o.pushedM += moved;
    if (!o.wrecked) { o.pushedDmg += moved; while (o.pushedDmg >= 1.0f) { o.pushedDmg -= 1.0f; o.hp = std::max(1, o.hp - 1); } }
    updateBody(app, o, true);
    if (glm::length(t.pos - app.eyePos()) < 60.0f && app.fx.rnd() < 0.05f) app.audio.play("impact_metal", o.pos + vec3(0, 1, 0), 0.5f);
  }
  return true;
}

void TankSys::drive(App& app, TankRT& t, float dt) {
  const TankDef& d = *t.def;
  bool road = app.terrain.wearAt(t.pos.x, t.pos.z) > 0.5f;
  float vF = d.maxFwd * (road ? 1.0f : d.offroadMul), vR = d.maxRev;
  float y0, p0, r0, slope;
  groundPose(app, d, t.pos, t.yaw, y0, p0, r0, slope, t.id);
  // slope: uphill in the direction of travel reduces the attainable speed to zero at the slope limit
  float dirSign = t.throttleIn > 0 ? 1.0f : t.throttleIn < 0 ? -1.0f : (t.speed >= 0 ? 1.0f : -1.0f);
  float uphill = slope * dirSign;
  float slopeK = std::clamp(1.0f - std::max(0.0f, uphill) / d.maxSlopeDeg, 0.0f, 1.0f);
  float target = t.throttleIn > 0 ? vF * t.throttleIn * slopeK : t.throttleIn < 0 ? -vR * (-t.throttleIn) * slopeK : 0.0f;
  bool braking = (t.throttleIn > 0 && t.speed < -0.05f) || (t.throttleIn < 0 && t.speed > 0.05f);
  float rate = braking ? d.brake : t.throttleIn == 0 ? d.coast + d.brake * 0.25f * (std::fabs(t.speed) < 1 ? 1.0f : 0.0f) : (std::fabs(target) < std::fabs(t.speed) ? d.brake * 0.6f : d.accel);
  t.speed = approach(t.speed, target, rate * dt);
  if (d.car && t.handbrake) t.speed = approach(t.speed, 0.0f, d.handbrakeDecel * dt);   // VH1: rear wheels locked
  // downhill roll / uphill drag when coasting (bounded, deterministic)
  // (a parked tank holds on its brakes; only a crewed, still-rolling tank feels the slope)
  if (t.throttleIn == 0 && occupied == t.id && std::fabs(t.speed) > 0.5f && std::fabs(slope) > 4.0f) t.speed -= 9.81f * std::sin(radians(slope)) * 0.15f * dt;
  // tracked steering: pivot turn at standstill, a speed-dependent turn rate when moving; yaw + turns the nose left
  float spd = std::fabs(t.speed);
  float turn = glm::mix(d.pivotRate, d.turnRate, std::min(1.0f, spd / 2.0f)) * (spd > 0.5f * d.maxFwd ? glm::mix(1.0f, 0.7f, (spd - 0.5f * d.maxFwd) / (0.5f * d.maxFwd)) : 1.0f);
  t.yawRate = approach(t.yawRate, -t.steerIn * turn, 70.0f * dt);
  if (d.car) {
    // VH1 bicycle model: yaw rate = v tan(delta) / wheelbase, capped by tyre grip (lateral acceleration ~ 0.75 g; more
    // with the rear locked on the handbrake, so the tail steps out)
    float delta = radians(-t.steer * d.maxSteerDeg), L = d.carL;
    float w = t.speed * std::tan(delta) / std::max(0.5f, L), wmax = (t.handbrake ? d.grip * 1.5f : d.grip) / std::max(std::fabs(t.speed), 1.0f);
    t.yawRate = degrees(std::clamp(w, -wmax, wmax));
  }
  if (std::fabs(t.speed) < 1e-3f && std::fabs(t.yawRate) < 1e-3f) {
    // parked: settle on the ground (craters can open under a parked tank)
    t.speed = 0; t.yawRate = 0;
    t.pos.y = approach(t.pos.y, y0, 2.0f * dt); t.pitch = approach(t.pitch, p0, 20 * dt); t.roll = approach(t.roll, r0, 20 * dt);
  } else {
    float ny = wrapDeg(t.yaw + t.yawRate * dt);
    vec3 np = t.pos + yawQuat(ny) * vec3(0, 0, 1) * t.speed * dt;
    float y, p, r, s;
    groundPose(app, d, np, ny, y, p, r, s, t.id);
    np.y = y;
    bool ok = true; std::string why;
    if (!app.terrain.inBuildArea(np.x, np.z)) { ok = false; why = "edge of the map"; }
    if (ok && std::fabs(s) > d.maxSlopeDeg + 4 && s * (t.speed >= 0 ? 1.0f : -1.0f) > 0) { ok = false; why = "too steep"; }
    if (ok && app.terrain.height(np.x, np.z) < app.terrain.waterLevel + 0.3f) { ok = false; why = "water"; }
    // swept hull box at the new pose (terrain ignored: the pose follows it)
    TankRT probe = t; probe.pos = np; probe.yaw = ny; probe.pitch = p; probe.roll = r; probe.suspPitch = probe.suspRoll = 0;
    mat4 H = hullMatrix(probe);
    quat hq = glm::quat_cast(glm::mat3(H));
    vec3 he = d.halfSize, c = vec3(H * vec4(0, d.clearance + he.y, 0, 1));
    // VH1: soldiers and zombies in the car's path are struck (damage by impact speed); the car loses some speed
    if (ok && d.car && std::fabs(t.speed) > 1.5f) {
      int hits = mpaiRunOver(app, c, he + vec3(0.2f, 0, 0.25f), hq, yawQuat(ny) * vec3(0, 0, 1) * t.speed, d.runOverDmg, t.runOverT);
      if (hits) { t.runOvers += hits; t.speed *= std::pow(0.8f, (float)hits); app.audio.play("impact_metal", c, 0.7f); }
    }
    if (ok && !pushVehicles(app, t, np, ny, why)) ok = false;   // VH8: other vehicles and wrecks: shoved by mass, or holding
    if (ok) { groundPose(app, d, np, ny, y, p, r, s, t.id); np.y = y; probe.pos = np; H = hullMatrix(probe); c = vec3(H * vec4(0, d.clearance + he.y, 0, 1)); }
    // anything in the way: knocked over, crushed, pushed through or holding (tank_ram.cpp, one energy rule)
    if (ok && Physics::boxBlocked(c, he, hq, bodyUD(t.id), true) && !ramObstacles(app, t, dt, H, c, he, hq, why)) ok = false;
    // never drive into the player (no crushing; the tank stops)
    if (ok && Physics::hasCharacter() && !occupied) {
      auto ch = Physics::character();
      vec3 a = ch.pos + vec3(0, 0.3f, 0), b = ch.pos + vec3(0, 1.5f, 0);
      if (segOBBDist2(a, b, c, he + vec3(0.1f), hq) < 0.3f * 0.3f) { ok = false; why = "a soldier in the way"; }
    }
    if (ok && Physics::hasCharacter() && occupied && occupied != t.id) {
      auto ch = Physics::character();
      vec3 a = ch.pos + vec3(0, 0.3f, 0), b = ch.pos + vec3(0, 1.5f, 0);
      if (segOBBDist2(a, b, c, he + vec3(0.1f), hq) < 0.3f * 0.3f) { ok = false; why = "a soldier in the way"; }
    }
    if (ok) {
      // suspension: pitch / roll follow the ground through a damped spring (no snapping over crater rims)
      float sp = (p - t.pitch), sr = (r - t.roll);
      t.pitch += sp * std::min(1.0f, dt * 7.0f); t.roll += sr * std::min(1.0f, dt * 7.0f);
      float oldY = t.pos.y;
      t.pos = np; t.pos.y = glm::mix(oldY, y, std::min(1.0f, dt * 12.0f)); t.yaw = ny;
      t.blockedBy.clear();
    } else {
      if (t.blockedBy != why && occupied == t.id && why.rfind("pushing", 0) != 0) app.toast("Blocked: " + why, 2);
      t.blockedBy = why;
      t.speed = 0; t.yawRate = 0;
    }
  }
  if (d.car) {
    // VH1: each wheel drops / rises to the ground under it within its travel (visual), and rolls with the ground speed
    mat4 H = hullMatrix(t);
    for (int k = 0; k < d.nWheels; ++k) {
      vec3 w = vec3(H * vec4(d.wheelC[k].x, 0, d.wheelC[k].z, 1));
      float want = std::clamp(app.terrain.height(w.x, w.z) - w.y, -d.suspTravel, d.suspTravel);
      t.wheelSusp[k] = approach(t.wheelSusp[k], want, 1.5f * dt);
      if (!(t.handbrake && TankDef::rearWheel(k))) t.wheelSpin[k] = std::fmod(t.wheelSpin[k] + t.speed / std::max(0.1f, d.wheelR) * dt, 6.2831853f);
    }
  }
  crushUnder(app, t);   // fallen poles / fences under the tracks
  // TK1: the firing rock springs back, critically damped (omega 8.5: peak ~0.12 s, settled ~0.8 s; visual only)
  {
    const float w = 8.5f, e = std::exp(-w * dt);
    auto spring = [&](float& x, float& v) { float c = v + w * x; x = (x + c * dt) * e; v = (v - w * c * dt) * e; };
    if (gFxRecoil && !t.def->autoGun) {   // fxrecoil: the hull pitches and settles at the platform's own frequency and damping (7.3; ~1.3 s to rest)
      const FxGunRow& g = fxGunFor(t.def->id); const float wn = 2.0f * 3.14159265f * g.fn, z = g.zeta;
      auto under = [&](float& x, float& v) { for (int s = 0; s < 4; ++s) { const float h = dt / 4; v += (-wn * wn * x - 2 * z * wn * v) * h; x += v * h; } };
      under(t.suspPitch, t.suspPitchV); under(t.suspRoll, t.suspRollV);
    } else { spring(t.suspPitch, t.suspPitchV); spring(t.suspRoll, t.suspRollV); }
    for (int k = 0; k < 3; ++k) spring(t.shove[k], t.shoveV[k]);
  }
  // track speeds (+x / -x side) for the HUD, sounds and dust
  float w = radians(t.yawRate);
  t.trackPhase[0] += (t.speed + w * d.trackHalfGauge) * dt;
  t.trackPhase[1] += (t.speed - w * d.trackHalfGauge) * dt;
  float load = std::clamp(std::fabs(t.throttleIn) * 0.6f + std::fabs(t.steerIn) * 0.3f + std::max(0.0f, slope * dirSign) / d.maxSlopeDeg * 0.5f, 0.0f, 1.0f);
  float rpmT = (occupied == t.id ? 0.2f : 0.0f) + 0.55f * std::fabs(t.speed) / std::max(1.0f, d.maxFwd) + 0.35f * load;
  if (occupied != t.id && std::fabs(t.speed) < 0.05f) rpmT = 0;
  t.rpm = approach(t.rpm, std::min(rpmT, 1.0f), 0.8f * dt);
}

void TankSys::aimGun(TankRT& t, float dt, bool active, const vec3* wantDir) {
  const TankDef& d = *t.def;
  extern App* gApp;
  float dy = 0, de = 0;
  if (active && (gApp || wantDir)) {
    vec3 want = wantDir ? *wantDir : App::forward(gApp->eyeYaw, gApp->eyePitch);
    mat4 H = hullMatrix(t);
    vec3 l = glm::inverse(glm::mat3(H)) * want;
    float ty = degrees(std::atan2(l.x, l.z)), te = degrees(std::atan2(l.y, glm::length(vec2(l.x, l.z))));
    te = std::clamp(te, d.elevMin, d.elevMax);
    dy = wrap180(ty - t.tYaw); de = te - t.gElev;
  }
  float before = t.tYaw;
  slew(t.tYaw, t.tYawV, dy, d.traverseRate, d.traverseAccel, dt);
  t.tYaw = wrap180(t.tYaw);
  slew(t.gElev, t.gElevV, de, d.elevRate, d.elevAccel, dt);
  if (t.gElev <= d.elevMin) { t.gElev = d.elevMin; if (t.gElevV < 0) t.gElevV = 0; }
  if (t.gElev >= d.elevMax) { t.gElev = d.elevMax; if (t.gElevV > 0) t.gElevV = 0; }
  (void)before;
}

// ============================================================ ammunition state machine
// Order inside one step (deterministic): fire (stepOne) -> R toggle -> load timer -> completion -> automatic start.
//  * a chambered round never changes type; R only changes the NEXT / in-progress choice (timer untouched)
//  * the reserve is decremented once, at completion, for the type selected at that moment (late selection)
//  * an empty selected reserve falls back to the other type with feedback; no reserves: stays empty
void TankSys::stepAmmo(App& app, TankRT& t, float dt, bool toggle) {
  const TankDef& d = *t.def;
  if (toggle) {
    t.selected = t.selected == SHELL_HE ? SHELL_AP : SHELL_HE; ++t.toggles;
    if (occupied == t.id) {
      if (t.reload > 0) app.toast(std::string("Loading ") + shellName(t.selected) + " (timer continues)", 1.8);
      else if (t.loaded != SHELL_NONE) app.toast(std::string(shellName(t.loaded)) + " stays loaded - next round " + shellName(t.selected), 1.8);
      else app.toast(std::string("Next round ") + shellName(t.selected), 1.8);
    }
  }
  if (t.reload > 0) {
    t.reload -= dt;
    if (t.reload <= 0) {
      t.reload = 0;
      int pick = t.selected;
      int have = pick == SHELL_HE ? t.reserveHE : t.reserveAP;
      if (have <= 0) {
        int other = pick == SHELL_HE ? SHELL_AP : SHELL_HE;
        int h2 = other == SHELL_HE ? t.reserveHE : t.reserveAP;
        if (h2 > 0) { if (occupied == t.id) app.toast(std::string("No ") + shellName(pick) + " left - " + shellName(other) + " loaded", 2.5); pick = other; t.selected = other; }
        else pick = SHELL_NONE;
      }
      if (pick == SHELL_HE) --t.reserveHE; else if (pick == SHELL_AP) --t.reserveAP;
      t.loaded = pick;
      if (pick != SHELL_NONE) { ++t.loadsCompleted; app.audio.play("breech", vec3(hullMatrix(t) * vec4(d.gunPivot, 1)), 0.7f); }
      else if (occupied == t.id) app.toast("Out of main-gun ammunition.", 3);
    }
  }
  if (t.loaded == SHELL_NONE && t.reload <= 0 && t.reserveHE + t.reserveAP > 0) t.reload = d.reloadTime;
}

void TankSys::fireCannon(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec3 dir = glm::normalize(vec3(g * vec4(0, 0, 1, 0)));
  vec3 muzzle = vec3(g * vec4(d.muzzle, 1)), piv = vec3(g * vec4(d.gunPivot, 1)), optic = vec3(g * vec4(d.optic, 1));
  bool ap = t.loaded == SHELL_AP;
  float v = ap ? d.velAP : d.velHE;
  // game zero: the round crosses the sight line at zeroRange (parallax between optic and bore and the drop included)
  vec3 P = optic + dir * d.zeroRange;
  float tof = d.zeroRange / v, drop = 0.5f * 9.81f * tof * tof;
  vec3 fireDir = glm::normalize(P + vec3(0, drop, 0) - muzzle);
  // the shell is swept from the trunnions along the bore: a wall at the muzzle stops it (no firing through cover)
  vec3 start = piv + fireDir * 0.05f;
  app.fireProjectile(start, fireDir, v, PROJ_HE, ap ? (d.autoGun ? 12.0f : 120.0f) : d.dmgHE, bodyUD(t.id), d.autoGun && (t.shots % 3) == 0 ? 2 : 0, ap ? d.ammoAP : d.ammoHE);
  if (!app.projectiles.empty()) {
    auto& pr = app.projectiles.back(); pr.at = ap && !d.autoGun; pr.atKill = !d.autoGun;
    if (d.autoGun) {   // VH7: a 25 mm round: small HE burst (AP: a puff), light anti-armour, every third a tracer
      pr.blastR = ap ? 0.5f : d.heBlastR; pr.craterScale = ap ? 0.05f : 0.15f; pr.lightAT = ap ? d.lightAP : d.lightHE;
    }
  }
  t.loaded = SHELL_NONE; ++t.shots; t.recoilT = 0;
  {   // TK1: the hull rocks away from the barrel on its suspension (more broadside: the hull is narrower than long) and is
      // shoved a little back along it; springs in the drive step bring it back
    float deg = d.rockDeg >= 0 ? d.rockDeg : 9.0f * d.recoil, k = 8.5f * 2.718282f, a = radians(t.tYaw);
    if (gFxRecoil && !d.autoGun) {   // fxrecoil (7.3): the derived peak pitch (M1A2 0.77, M60 0.79 deg), split by elevation, on the row's 1.2 Hz / zeta 0.3 hull
      const FxGunRow& g = fxGunFor(d.id); deg = g.pitchDeg * std::cos(radians(t.gElev));
      k = 2.0f * 3.14159265f * g.fn / fxSpringPeak(g.zeta);   // v0 for that first peak (Fx: the hull spring below runs underdamped)
    }
    t.suspPitchV += deg * std::cos(a) * k;
    t.suspRollV += 1.6f * deg * std::sin(a) * k;
    vec3 back = -vec3(dir.x, 0, dir.z); float L = glm::length(back);
    if (L > 1e-3f) t.shoveV += back / L * (0.012f * deg * k);
  }
  if (d.autoGun) {   // VH8: an autocannon's shot is a small short puff (200 rpm: a full tank-gun cloud each round built a wall in the sight)
    app.fx.flash(muzzle + dir * 0.15f, 0.35f * d.muzzleScale / 0.55f, 0.04f, vec4(10, 7, 3, 1));
    for (int k = 0; k < 2; ++k) app.fx.smoke(muzzle + dir * 0.3f, dir * (2.0f + 2.0f * app.fx.rnd()) + vec3(0, 0.3f, 0), 0.12f, 0.45f + 0.25f * app.fx.rnd(), vec4(0.62f, 0.6f, 0.57f, 0.35f), 0.9f, 0.25f);
  } else app.fx.muzzle(muzzle, dir, d.muzzleScale, d.id.c_str());
  if (gFxRecoil && !d.autoGun && !d.hullBoxes.empty()) {   // fxrecoil (7.4): every horizontal surface sheds its dust, 5-60 ms (Medium 6 / High 16 / Ultra 30)
    static const int nShed[5] = {0, 0, 6, 16, 30}; const mat4 H = hullMatrix(t); const TankBox& b = d.hullBoxes[0];
    for (int k = 0; k < nShed[std::clamp(gFxTier, 0, 4)]; ++k) {
      const vec3 lp = b.c + vec3((app.fx.rnd() * 2 - 1) * b.h.x, b.h.y, (app.fx.rnd() * 2 - 1) * b.h.z), wp = vec3(H * vec4(lp, 1));
      app.fx.smoke(wp, vec3(app.fx.rnd() - 0.5f, 0.6f + 0.6f * app.fx.rnd(), app.fx.rnd() - 0.5f) * 1.2f, 0.25f + 0.25f * app.fx.rnd(), 1.5f + 1.5f * app.fx.rnd(), vec4(0.5f, 0.45f, 0.37f, 0.35f), 0.5f, 0.1f);
    }
  }   // vehicle JSON gun.muzzleScale (firing smoke: content/settings/fx.json)
  app.audio.play(!d.gunSound.empty() && app.audio.hasEvent(d.gunSound) ? d.gunSound : t.def->id == "tiger_i" ? "tank_88" : "tank_17pdr", muzzle, d.autoGun ? 0.8f : 1.0f);
  app.viewKickPitch = d.autoGun ? 0.12f : 0.6f;
  t.kickT = 0; t.kickDeg = d.viewKick >= 0 ? d.viewKick : 5.5f * d.recoil;   // RC2: every seat's view jumps (cameraKick)
  if (!d.autoGun) {
    float gy = app.terrain.height(t.pos.x, t.pos.z), R = std::max(2.5f, 1.2f * d.trackHalfLen);   // RC2: the blast lifts a dust ring round the hull
    for (int k = 0; k < 20; ++k) {
      float a = 6.2831853f * (k + app.fx.rnd()) / 20.0f; vec3 o(std::sin(a), 0, std::cos(a));
      app.fx.smoke(vec3(t.pos.x, gy + 0.3f, t.pos.z) + o * R, o * (3.0f + 2.0f * app.fx.rnd()) + vec3(0, 0.6f, 0), 0.7f + 0.4f * app.fx.rnd(), 1.6f + 0.8f * app.fx.rnd(), vec4(0.52f, 0.47f, 0.38f, 0.45f), 1.4f, 0.3f);
    }
    if (occupied != t.id) {   // infantry nearby feel it (concussion shake by distance)
      float dist = glm::length(app.eyePos() - muzzle);
      if (dist < 60.0f) app.blastShake = std::max(app.blastShake, 0.7f * (1.0f - dist / 60.0f));
    }
  }
}
void TankSys::cameraKick(const App& app, vec3& eye, vec3& fwd) const {
  auto it = tanks.find(occupied);
  if (it == tanks.end()) return;
  const TankRT& t = it->second; float r = t.kickT, k = t.kickDeg;
  if (k <= 0 || r > 1.2f) return;
  // a hard jump up and back (peak 40 ms), a damped settle in ~0.7 s, and a short shake; the aim itself never moves
  float env = r < 0.04f ? r / 0.04f : std::exp(-(r - 0.04f) * 6.0f) * std::cos((r - 0.04f) * 9.0f);
  float shake = app.reducedMotion ? 0.0f : 0.2f * k * std::exp(-r * 7.0f);   // reduced motion: the jump stays, no shake
  float yaw = degrees(std::atan2(-fwd.x, -fwd.z)) + shake * std::sin(r * 47.0f + 1.3f), pitch = degrees(std::asin(std::clamp(fwd.y, -1.0f, 1.0f))) + k * env + shake * std::sin(r * 61.0f);
  fwd = App::forward(yaw, std::clamp(pitch, -89.0f, 89.0f));
  eye -= fwd * (0.035f * k * std::max(0.0f, env));
}

void TankSys::fireMg(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  if (d.mgName.empty()) return;   // VH7: no roof gun (the Bradley's commander only looks out)
  if (t.mgCool > 0 || t.mgReloadT > 0) return;
  if (t.mgLoaded <= 0) {   // belts change on their own (stepMgs); nothing left at all: dry click
    if (t.mgReserve <= 0) { app.audio.play("click", t.pos + vec3(0, 2.8f, 0), 0.6f); t.mgCool = 0.4f; }
    return;
  }
  vec3 dir; vec3 m = mgMuzzleWorld(t, dir);
  // self-obstruction: the line of fire must not cross the tank's own turret / hull
  Physics::Hit sh;
  if (Physics::castRayOnly(m, dir, 9.0f, bodyUD(t.id), sh)) { if (occupied == t.id) app.toast("Roof MG: line of fire blocked by the tank.", 1.0); t.mgCool = 0.2f; return; }
  float s = radians(d.mgSpread);
  uint32_t& rng = app.fx.rng;
  auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng % 20001u) / 10000.0f - 1.0f; };
  vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), rgt = glm::normalize(glm::cross(dir, up)), u2 = glm::cross(rgt, dir);
  vec3 fd = glm::normalize(dir + rgt * (rnd() * s) + u2 * (rnd() * s));
  app.fireProjectile(m, fd, d.mgVel, PROJ_BULLET, 0, bodyUD(t.id), (t.mgShots % d.mgTracerEvery) == 0, d.mgName);
  if (!app.projectiles.empty()) app.projectiles.back().mass = d.mgBulletMass;
  --t.mgLoaded; ++t.mgShots; t.mgCool += 60.0f / d.mgRpm; t.mgBurst = true;
  app.fx.muzzleFlash(m + dir * 0.03f, dir, 1.7f, false, m, vec3(-1), "7.92x57 mm");   // WP4 (fxev: rifle-calibre coax)
  if ((t.mgShots - 1) % d.mgSoundEvery == 0) app.audio.play(app.audio.hasSound(d.mgSound) ? d.mgSound : "shot_m2hb", m, 0.9f);   // TK1: the minigun's sound (until the sound session's shot_minigun lands: the M2's)
  app.viewKickPitch = 0.25f;
}

// ============================================================ bow MG (glacis ball mount)
mat4 TankSys::bowMatrix(const TankRT& t) const {
  const TankDef& d = *t.def;
  if (d.bowCoax) { mat4 h, tu, g, rc; rig(t, h, tu, g, rc); return g * glm::translate(mat4(1), d.bowPivot); }   // VH7: the coax rides on the gun
  return hullMatrix(t) * glm::translate(mat4(1), d.bowPivot) * glm::rotate(mat4(1), radians(t.bowYaw), vec3(0, 1, 0)) * glm::rotate(mat4(1), radians(-t.bowElev), vec3(1, 0, 0));
}
vec3 TankSys::bowMuzzleWorld(const TankRT& t, vec3& dir) const {
  mat4 M = bowMatrix(t);
  dir = glm::normalize(vec3(M * vec4(0, 0, 1, 0)));
  return vec3(M * vec4(t.def->bowMuzzle - t.def->bowPivot, 1));
}
vec3 TankSys::aimPoint(App& app, const TankRT& t) const {
  vec3 eye, fwd; float fov;
  camera(app, eye, fwd, fov);
  Physics::Hit ah;
  if (Physics::castRayEx(eye, fwd, 600.0f, ah, bodyUD(t.id))) return ah.pos;
  return eye + fwd * 400.0f;
}

// VH7 TOW: one missile from the turret-side launcher toward the crosshair; the gunner guides it (stepTows) and a wire
// trail is paid out behind it
void TankSys::fireTow(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  if (t.towLeft <= 0) { app.audio.play("click", t.pos + vec3(0, 2.5f, 0), 0.6f); app.toast(d.towName + ": reloading", 1.5); return; }
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec3 m = vec3(tu * vec4(d.towMuzzle, 1)), dir = glm::normalize(aimPoint(app, t) - m);
  static uint32_t uid = 0; ++uid;
  app.fireProjectile(m + dir * 0.6f, dir, d.towVel, PROJ_ROCKET, d.towDmg, bodyUD(t.id), false, "tow");
  Projectile& p = app.projectiles.back();
  p.blastDmg = d.towDmg; p.blastR = d.towBlastR; p.craterScale = 0.6f; p.atKill = true; p.gravScale = 0; p.splashGun = 120; p.towOf = t.id; p.uid = uid;
  app.fx.backblast(m - dir * 1.0f, -dir);
  app.fx.flash(m + dir * 0.4f, 1.2f, 0.1f, vec4(9, 6, 3, 1));
  app.audio.play(app.audio.hasEvent("shot_tow") ? "shot_tow" : app.audio.hasEvent("shot_modern_rpg7") ? "shot_modern_rpg7" : "shot_panzerschreck", m, 1.0f);   // VH8: the RPG launch recording
  {   // RC2: the launch nudges the hull (the launcher sits on the turret's left) and the view
    float k = 8.5f * 2.718282f, a = radians(t.tYaw);
    t.suspPitchV += 0.5f * std::cos(a) * k; t.suspRollV += 0.6f * std::sin(a) * k;
    t.kickT = 0; t.kickDeg = 0.7f;
  }
  --t.towLeft;
  TowTrail tr; tr.uid = uid; tr.tank = t.id; tr.p = {m, m + dir * 0.6f}; tr.v = {vec3(0), dir * 2.0f}; tr.last = m + dir * 0.6f;
  towTrails.push_back(tr);
}

// VH7: guidance (the missile turns toward the gunner's crosshair at towTurn deg/s and weaves round its line, the weave
// damping as it settles) and the guidance wire: points paid out every 0.3 m, each then sagging, dragged and stirred, so
// the line keeps writhing; the launcher end follows the turret; the trail fades 0.3 s after the missile is gone
void TankSys::stepTows(App& app, float dt) {
  for (auto& tr : towTrails) {
    Projectile* pm = nullptr; for (auto& p : app.projectiles) if (p.uid == tr.uid && p.towOf) { pm = &p; break; }
    TankRT* t = get(tr.tank);
    tr.age += dt;
    if (pm) {
      float sp = glm::length(pm->v); vec3 f = pm->v / std::max(sp, 1e-3f);
      if (t && occupied == tr.tank && !t->wrecked) {   // wire guidance: toward what the crosshair is on
        vec3 want = glm::normalize(aimPoint(app, *t) - pm->p);
        float c = std::clamp(glm::dot(f, want), -1.0f, 1.0f), ang = std::acos(c), mx = radians(t->def->towTurn) * dt;
        if (ang > 1e-4f) { vec3 ax = glm::cross(f, want); if (glm::length(ax) > 1e-6f) f = glm::angleAxis(std::min(ang, mx), glm::normalize(ax)) * f; }
      }
      vec3 up(0, 1, 0), rgt = glm::normalize(glm::cross(f, std::fabs(f.y) > 0.95f ? vec3(1, 0, 0) : up)), u2 = glm::cross(rgt, f);
      float amp = 0.35f + 1.6f * std::exp(-tr.age / 1.1f), w = 6.2831853f * 1.7f;   // the corkscrew: m/s sideways, damping
      vec3 weave = (rgt * std::cos(w * tr.age) + u2 * std::sin(w * tr.age)) * amp;
      pm->v = f * sp; pm->p += weave * dt;   // the path weaves round its line; the heading keeps to the guidance
      // pay out the wire behind the missile
      vec3 back = pm->p - f * 0.4f;
      while (glm::length(back - tr.last) > 0.3f) { vec3 q = tr.last + glm::normalize(back - tr.last) * 0.3f; tr.p.push_back(q); tr.v.push_back(f * sp * 0.02f); tr.last = q; }
      if (app.fx.rnd() < 0.8f) app.fx.smoke(pm->p - f * 0.5f, -f * 1.5f + vec3(0, 0.2f, 0), 0.18f, 1.2f, vec4(0.85f, 0.82f, 0.78f, 0.55f), 1.2f, 0.25f);   // the motor's smoke
    } else if (tr.fade > 1e8f) tr.fade = 0.3f;
    if (tr.fade < 1e8f) tr.fade -= dt;
    // the wire: the launcher end on the turret; the rest sags, drags and stirs (cheap turbulence), never into the ground
    if (t && !tr.p.empty()) { mat4 h, tu, g, rc; rig(*t, h, tu, g, rc); tr.p[0] = vec3(tu * vec4(t->def->towMuzzle, 1)); tr.v[0] = vec3(0); }
    for (size_t k = 1; k + 1 < tr.p.size(); ++k) {
      vec3& v = tr.v[k];
      float s = (float)k * 0.37f + tr.age * 2.3f;
      v += (vec3(0, -2.2f, 0) + vec3(std::sin(s * 1.3f), 0.5f * std::sin(s * 0.7f + 1.0f), std::cos(s * 1.1f)) * 1.6f) * dt;
      v *= std::exp(-1.8f * dt);
      tr.p[k] += v * dt;
      float gy = app.terrain.height(tr.p[k].x, tr.p[k].z) + 0.04f;
      if (tr.p[k].y < gy) { tr.p[k].y = gy; v = vec3(0); }
    }
    if (tr.p.size() > 360) {   // cap: thin the oldest stretch (by the launcher) to every other point
      std::vector<vec3> np, nv; size_t half = tr.p.size() / 2;
      for (size_t k = 0; k < tr.p.size(); ++k) if (k >= half || k % 2 == 0) { np.push_back(tr.p[k]); nv.push_back(tr.v[k]); }
      tr.p.swap(np); tr.v.swap(nv);
    }
    // draw: a thin line, light smoke clinging near the motor end
    float a = tr.fade < 1e8f ? std::clamp(tr.fade / 0.3f, 0.0f, 1.0f) : 1.0f;
    for (size_t k = 0; k + 1 < tr.p.size(); ++k) {
      float u = (float)k / std::max<size_t>(1, tr.p.size() - 1);
      app.fx.streak(tr.p[k], tr.p[k + 1], 0.03f + 0.05f * u * u, dt * 1.5f, vec4(0.45f + 0.9f * u, 0.43f + 0.6f * u, 0.4f + 0.3f * u, a * (0.6f + 0.4f * u)));   // grey wire, brighter toward the motor
    }
    if (pm && tr.p.size() > 4) for (int sm = 0; sm < 2; ++sm) { size_t k = tr.p.size() - 1 - (size_t)(app.fx.rnd() * std::min<size_t>(40, tr.p.size() - 1));
      app.fx.smoke(tr.p[k], vec3(0, 0.15f, 0), 0.12f, 1.6f, vec4(0.8f, 0.78f, 0.75f, 0.25f), 1.4f, 0.1f); }
  }
  towTrails.erase(std::remove_if(towTrails.begin(), towTrails.end(), [](const TowTrail& tr) { return tr.fade <= 0.0f; }), towTrails.end());
}

void TankSys::fireBow(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  if (t.bowCool > 0 || t.bowReloadT > 0) return;
  if (t.bowLoaded <= 0) {
    if (t.bowReserve <= 0) { app.audio.play("click", t.pos + vec3(0, 1.5f, 0), 0.6f); t.bowCool = 0.4f; }
    return;
  }
  vec3 dir; vec3 m = bowMuzzleWorld(t, dir);
  float s = radians(d.bowSpread);
  uint32_t& rng = app.fx.rng;
  auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng % 20001u) / 10000.0f - 1.0f; };
  vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), rgt = glm::normalize(glm::cross(dir, up)), u2 = glm::cross(rgt, dir);
  vec3 fd = glm::normalize(dir + rgt * (rnd() * s) + u2 * (rnd() * s));
  // the ball sits in the glacis: rounds start at the muzzle and never collide with the own hull
  app.fireProjectile(m, fd, d.bowVel, PROJ_BULLET, 0, bodyUD(t.id), (t.bowShots % 5) == 0, d.bowName);
  if (!app.projectiles.empty()) app.projectiles.back().mass = d.bowBulletMass;
  --t.bowLoaded; ++t.bowShots; t.bowCool += 60.0f / d.bowRpm; t.bowBurst = true;
  app.fx.muzzleFlash(m + dir * 0.03f, dir, 1.5f, false, m, vec3(-1), "7.92x57 mm");   // WP4 (fxev: rifle-calibre bow MG)
  app.fx.smoke(m + dir * 0.1f, dir * 1.5f + vec3(0, 0.3f, 0), 0.05f, 1.0f, vec4(0.6f, 0.58f, 0.55f, 0.3f), 0.6f, 0.2f);
  app.audio.play(app.audio.hasSound(d.bowSound) ? d.bowSound : "shot_mg42", m, 0.95f);   // VH8: never a silent gun
  if (occupied == t.id) app.viewKickPitch = std::max(app.viewKickPitch, 0.08f);
}

void TankSys::stepMgs(App& app, TankRT& t, float dt, const ExploreInput* in, bool focused) {
  const TankDef& d = *t.def;
  bool hatchUp = t.station == TS_HATCH && t.hatchT >= 1.0f;
  bool space = in && in->jumpHeld && focused;
  bool lmb = in && in->fire && focused && fireArmed;
  if (in) {
    vec3 aimP = aimPoint(app, t);
    // roof MG: converges on the aim point from the open hatch
    if (hatchUp) {
      mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
      vec3 piv = vec3(tu * vec4(d.mgPivot, 1));
      vec3 l = glm::inverse(glm::mat3(tu)) * (aimP - piv);
      float ty = degrees(std::atan2(l.x, l.z)), te = degrees(std::atan2(l.y, glm::length(vec2(l.x, l.z))));
      ty = std::clamp(wrap180(ty), d.mgYawMin, d.mgYawMax); te = std::clamp(te, d.mgElevMin, d.mgElevMax);
      t.mgYaw = wrap180(t.mgYaw + std::clamp(wrap180(ty - t.mgYaw), -150.0f * dt, 150.0f * dt));
      t.mgElev = approach(t.mgElev, te, 120.0f * dt);
    }
    // bow MG: the hull gunner lays the ball mount on the same aim point, inside its arc
    if (d.bowOn) {
      mat4 H = hullMatrix(t);
      vec3 piv = vec3(H * vec4(d.bowPivot, 1));
      vec3 l = glm::inverse(glm::mat3(H)) * (aimP - piv);
      float ty = degrees(std::atan2(l.x, l.z)), te = degrees(std::atan2(l.y, glm::length(vec2(l.x, l.z))));
      ty = std::clamp(wrap180(ty), -d.bowYaw, d.bowYaw); te = std::clamp(te, d.bowElevMin, d.bowElevMax);
      t.bowYaw = approach(t.bowYaw, ty, 90.0f * dt);
      t.bowElev = approach(t.bowElev, te, 90.0f * dt);
    }
  }
  // fire: Space works the bow MG from either station and, from the hatch, the roof MG at the same time; LMB still
  // fires the roof MG alone from the hatch. Cooldowns carry the fractional interval (exact cyclic rate at 60 Hz).
  bool roofOn = hatchUp && (space || lmb), bowOnNow = d.bowOn && space;
  t.mgCool -= dt; t.bowCool -= dt;
  if (roofOn) {
    for (int guard = 0; t.mgCool <= 0 && t.mgReloadT <= 0 && t.mgLoaded > 0 && guard < 4; ++guard) fireMg(app, t);
    if (t.mgLoaded <= 0 && t.mgReserve <= 0 && t.mgCool <= 0) fireMg(app, t);
  }
  if (bowOnNow) {
    for (int guard = 0; t.bowCool <= 0 && t.bowReloadT <= 0 && t.bowLoaded > 0 && guard < 4; ++guard) fireBow(app, t);
    if (t.bowLoaded <= 0 && t.bowReserve <= 0 && t.bowCool <= 0) fireBow(app, t);
  }
  if (!roofOn) t.mgCool = std::max(t.mgCool, 0.0f); else t.mgCool = std::max(t.mgCool, -1.0f / 60.0f);
  if (!bowOnNow) t.bowCool = std::max(t.bowCool, 0.0f); else t.bowCool = std::max(t.bowCool, -1.0f / 60.0f);
  // recorded tails once on release (or when the belt runs out mid-burst)
  vec3 dmy;
  if (t.mgBurst && (!roofOn || t.mgLoaded <= 0)) { t.mgBurst = false; if (!d.mgTail.empty()) app.audio.play(d.mgTail, mgMuzzleWorld(t, dmy), 0.85f); }
  if (t.bowBurst && (!bowOnNow || t.bowLoaded <= 0)) { t.bowBurst = false; if (!d.bowTail.empty()) app.audio.play(d.bowTail, bowMuzzleWorld(t, dmy), 0.85f); }
  // automatic belt change: an empty gun with rounds in reserve starts reloading at once (crew), whether or not the
  // trigger is held, and the new belt is in when the timer runs out
  auto belt = [&](int& loaded, int& reserve, float& timer, int beltSize, float time, const std::string& snd, const std::string& name, vec3 at) {
    if (timer > 0) {
      timer -= dt;
      if (timer <= 0) {
        timer = 0; int n = std::min(beltSize - loaded, reserve); loaded += n; reserve -= n;
        app.audio.play("belt", at, 0.6f);
      }
    } else if (loaded <= 0 && reserve > 0) {
      timer = time;
      app.audio.play(app.audio.hasEvent(snd) ? snd : "belt", at, 0.7f);
      if (occupied == t.id) app.toast("Reloading " + name, 1.6);
    }
  };
  belt(t.mgLoaded, t.mgReserve, t.mgReloadT, d.mgBelt, d.mgReload, d.mgReloadSound, d.mgName.substr(0, d.mgName.find(" (")), t.pos + vec3(0, 2.8f, 0));
  if (d.bowOn) belt(t.bowLoaded, t.bowReserve, t.bowReloadT, d.bowBelt, d.bowReload, d.bowReloadSound, d.bowShort, vec3(hullMatrix(t) * vec4(d.bowPivot, 1)));
}

// ============================================================ camera
bool TankSys::camera(const App& app, vec3& eye, vec3& fwd, float& fov) const {
  auto it = tanks.find(occupied);
  if (it == tanks.end() || !it->second.def) return false;
  const TankRT& t = it->second; const TankDef& d = *t.def;
  if (d.heli) return heliCamera(app, t, eye, fwd, fov);
  if (d.car) {   // VH1: the driver's head (free look, clamped in stepCar) easing to the pintle gunner's when standing at the MG
    mat4 H = hullMatrix(t);
    float u = t.hatchT * t.hatchT * (3 - 2 * t.hatchT);
    vec3 gunner = d.mgPivot + glm::angleAxis(radians(t.mgYaw), vec3(0, 1, 0)) * (d.hatchEye - d.mgPivot);   // stands behind the gun as it traverses
    fwd = App::forward(app.eyeYaw, app.eyePitch);
    if (d.rwsSight) {   // VH2: the gunner looks through the remote station's sight (it traverses / elevates with the gun)
      float zt = zoomT * zoomT * (3 - 2 * zoomT);
      eye = glm::mix(vec3(H * vec4(d.driverEye, 1)), vec3(mgMatrix(t) * vec4(d.mgOptic - d.mgPivot, 1)), u);
      fov = glm::mix(75.0f, glm::mix(d.rwsFov, d.rwsFovZoom, zt), u);
      return true;
    }
    eye = vec3(H * vec4(glm::mix(d.driverEye, gunner, u), 1)); fov = 75;
    return true;
  }
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  // gunner: the optic follows the ACHIEVED gun line (the turret lags the mouse; the HUD marks the desired aim)
  vec3 e0 = vec3(g * vec4(d.optic, 1)), f0 = glm::normalize(vec3(g * vec4(0, 0, 1, 0)));
  float zt = zoomT * zoomT * (3 - 2 * zoomT);
  float fov0 = glm::mix(d.fov, d.fovZoom, zt);
  vec3 e1 = vec3(tu * vec4(d.hatchEye, 1)), f1 = App::forward(app.eyeYaw, app.eyePitch);
  float u = t.hatchT * t.hatchT * (3 - 2 * t.hatchT);
  eye = glm::mix(e0, e1, u); fwd = glm::normalize(glm::mix(f0, f1, u)); fov = glm::mix(fov0, 70.0f, u);
  if (t.wrecked) { eye = e1; fwd = f1; fov = 70; }
  return true;
}

// ============================================================ damage (docs/tanks.md#damage)
int TankSys::classify(const TankRT& t, vec3 worldPos, vec3 worldNormal, bool& onTurret) const {
  const TankDef& d = *t.def;
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec3 pt = vec3(glm::inverse(tu) * vec4(worldPos, 1));
  const TankBox& T = d.armourTurret;
  onTurret = glm::all(glm::lessThanEqual(glm::abs(pt - T.c), T.h + vec3(0.06f)));
  glm::mat3 R = glm::mat3(onTurret ? tu : h);
  vec3 n = glm::normalize(glm::transpose(R) * worldNormal);
  // degenerate normal: use the contact point's position on the box instead
  if (!(glm::length(n) > 0.5f)) {
    const TankBox& B = onTurret ? T : d.armourHull;
    vec3 lp = vec3(glm::inverse(onTurret ? tu : h) * vec4(worldPos, 1)) - B.c;
    n = lp / B.h;
  }
  float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
  if (ay >= ax && ay >= az) return n.y > 0 ? TZ_ROOF : TZ_BELLY;
  if (az >= ax - 0.02f) return n.z > 0 ? TZ_FRONT : TZ_REAR;   // corner policy: front / rear wins a 45 deg tie (+-1 deg)
  return TZ_SIDE;
}

TankHitResult TankSys::projectileHit(App& app, const Projectile& p, vec3 pos, vec3 normal, uint64_t ud) {
  TankHitResult r;
  if ((ud >> 60) == 0xF || ud == 0 || (ud & 0xFFFF) != 0) return r;
  if (!tanks.count(ud >> 16)) return r;
  int attack = p.kind == PROJ_BULLET ? TA_SMALL_ARMS : p.kind == PROJ_ROCKET && !p.towOf ? TA_LAUNCHER : TA_CANNON;   // VH7: a TOW counts from every side, like a tank shell
  if (p.cannon30) { const TankRT& t = tanks[ud >> 16]; attack = t.def && t.def->car ? TA_LAUNCHER : TA_SMALL_ARMS; }   // VH5: 30 mm HE wrecks cars, scratches armour
  return applyHit(app, ud >> 16, attack, pos, normal, p.v, 0.5f * p.mass * glm::dot(p.v, p.v), p.towOf ? -1.0f : p.lightAT, p.armourMult);   // -1: a TOW (heavy AT)
}

TankHitResult TankSys::applyHit(App& app, uint64_t id, int attack, vec3 pos, vec3 normal, vec3 dir, float bulletJ, float lightAT, float armourMult) {
  TankHitResult r; r.tank = true; r.id = id; r.attack = attack;
  TankRT* t = get(id);
  if (!t) { r.tank = false; return r; }
  bool onTurret = false;
  r.zone = classify(*t, pos, normal, onTurret);
  static const char* an[] = {"small arms", "cannon", "launcher"};
  if (t->wrecked) { r.info = std::string(an[attack]) + " on the wreck"; lastHit = r; return r; }
  // the matrix: cannon (tank gun, AT gun, Flak) counts from every side; launchers only side / rear; small arms never.
  // roof / underside: cannon counts as a side-class hit, launchers do not count (no rule given -> conservative)
  bool counted = attack == TA_CANNON || (attack == TA_LAUNCHER && (r.zone == TZ_SIDE || r.zone == TZ_REAR || t->def->launcherAllSides));
  // VH7 autocannon: a tank's front shrugs it off; its sides / rear, light armour (an IFV) and soft vehicles take lightAT
  const bool lightArmour = t->def->launcherAllSides;
  if (lightAT > 0 && !t->def->car && !t->def->heli) counted = lightArmour || r.zone != TZ_FRONT;
  // VH1: soft-skinned: every launcher hit, bullets wear it down. VH2 armoured car: only heavy bullets (.50) wear it down
  bool heavy = t->def->heavyBulletJ > 0 && bulletJ >= t->def->heavyBulletJ;
  if (t->def->car || t->def->heli) counted = attack != TA_SMALL_ARMS || t->def->smallArmsDmg > 0 || heavy;
  // VH8: every bullet chips a helicopter by its energy (dmg 1.4 (J / 3500)^0.6: ~71 rifle, ~27 .50, ~230 pistol hits of 100)
  const bool chip = t->def->heli && attack == TA_SMALL_ARMS && bulletJ > 0;
  if (chip) counted = true;
  r.counted = counted;
  r.consumed = attack == TA_SMALL_ARMS || (attack == TA_LAUNCHER && !counted);   // no penetration, no blast
  char b[200];
  if (counted) {
    int dmg = r.zone == TZ_FRONT ? t->def->frontDamage() : t->def->sideDamage();
    if (t->def->car || t->def->heli) dmg = attack == TA_SMALL_ARMS ? (heavy ? t->def->heavyBulletDmg : t->def->smallArmsDmg) : attack == TA_LAUNCHER ? t->def->launcherDmg : t->def->cannonDmg;   // VH1: 100: one shell or rocket wrecks a jeep
    if (lightAT > 0) dmg = (int)std::ceil(lightAT * (t->def->car || t->def->heli ? 10.0f : lightArmour ? 3.0f : 1.0f));   // VH7: 25 mm per hit
    if (lightAT < 0 && !t->def->car && !t->def->heli) dmg = t->def->sideDamage();   // VH7 TOW: a flank shell's damage from any side (a Tiger: 2 kill it)
    if (attack == TA_LAUNCHER && armourMult != 1 && !t->def->car && !t->def->heli) dmg = std::max(1, (int)std::lround(dmg * armourMult));   // VH8 40 mm grenade on armour
    if (chip) { t->chipAcc += 1.4f * std::pow(bulletJ / 3500.0f, 0.6f); dmg = (int)t->chipAcc; t->chipAcc -= (float)dmg; }
    t->hp = std::max(0, t->hp - dmg); t->hitFlash = 1;
    if (r.zone == TZ_FRONT) ++t->hitsFront; else ++t->hitsSide;
    std::snprintf(b, sizeof b, "%s hit, %s %s: durability %d (front hits %d, side/rear hits %d)", an[attack], onTurret ? "turret" : "hull", tankZoneName(r.zone), t->hp, t->hitsFront, t->hitsSide);
    r.info = b;
    app.fx.impact(pos, normal, "metal");
    if (chip) { if (glm::length(pos - app.eyePos()) < 120.0f) app.audio.play("impact_metal", pos, 0.5f, 0.9f + 0.4f * app.fx.rnd()); }   // sparks and a ping, no shell-hit flash / toast
    else app.fx.flash(pos + normal * 0.1f, 0.6f, 0.08f, vec4(8, 5, 2, 1));
    if (t->hp <= 0) { destroy(app, *t, r.info); r.destroyed = true; }
    else if (occupied == id && !chip) app.toast("HIT - " + std::string(onTurret ? "turret " : "hull ") + tankZoneName(r.zone) + ", durability " + std::to_string(t->hp), 3);
  } else {
    ++t->hitsIgnored;
    std::snprintf(b, sizeof b, "%s on the %s %s: no penetration (no damage)", an[attack], onTurret ? "turret" : "hull", tankZoneName(r.zone));
    r.info = b;
    if (attack == TA_LAUNCHER) {   // visible non-penetrating detonation on the armour: flash, sparks, smoke; no blast stamp
      app.fx.flash(pos + normal * 0.15f, 1.1f, 0.1f, vec4(9, 6, 3, 1));
      app.fx.impact(pos, normal, "metal");
      for (int k = 0; k < 6; ++k) app.fx.smoke(pos + normal * 0.2f, normal * 0.8f + vec3(0, 0.5f, 0), 0.35f, 2.5f, vec4(0.35f, 0.33f, 0.3f, 0.5f), 1.6f, 0.4f);
      app.audio.play("explosion", pos, 0.55f);
    } else { app.fx.impact(pos, normal, "metal"); if (attack == TA_SMALL_ARMS && glm::length(pos - app.eyePos()) < 80.0f) app.audio.play("impact_metal", pos, 0.45f, 0.9f + 0.4f * app.fx.rnd()); }   // TK1: the ricochet ping
  }
  (void)dir;
  lastHit = r;
  return r;
}

bool TankSys::meleeBlow(App& app, uint64_t id, vec3 from) {
  TankRT* t = get(id);
  if (!t || t->wrecked) return false;
  t->hp = std::max(0, t->hp - (100 + 14) / 15);   // 7 a blow: 14 leave 2, the 15th destroys it
  vec3 at = t->pos + vec3(0, 1.2f, 0), n = from - t->pos; n.y = 0; n = glm::length(n) > 1e-3f ? glm::normalize(n) : vec3(0, 0, 1);
  at += n * 1.5f;
  app.fx.impact(at, n, "metal"); app.audio.play("impact_metal", at, 1.0f);
  ++t->meleeBlows; t->hitFlash = 1;
  if (occupied == id) app.blastShake = std::max(app.blastShake, 0.25f);
  if (t->hp <= 0) destroy(app, *t, "torn apart");
  return true;
}

void TankSys::destroy(App& app, TankRT& t, const std::string& why) {
  if (t.wrecked) return;   // exactly once
  t.wrecked = true; t.hp = 0; t.reload = 0; t.loaded = SHELL_NONE; t.speed = 0; t.yawRate = 0; t.mgReloadT = 0; t.bowReloadT = 0;
  t.burnT = 0; ++wrecksMade; wreckOrder.push_back(t.id);
  for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
  vec3 c = t.pos + vec3(0, 1.8f, 0);
  app.fx.explosion(c, vec3(0, 1, 0), "metal", false);
  app.fx.flash(c, 4.0f, 0.25f, vec4(9, 5, 2, 1));
  app.audio.play("explosion", c, 1.0f);
  app.toast(t.def->name + " destroyed (" + why + ")", 5);
  if (t.def->heli) {   // VH5: power lost: it falls (spinning) and crashes, or burns where it stands
    if (t.pos.y > heliGround(app, t, t.pos) + 0.5f) { t.falling = true; t.yawRate = 40; }
    else heliCrash(app, t);
    return;
  }
  if (occupied == t.id) {
    // TK1: in a match the crew dies with the vehicle (the wreck stays; the next life starts at the team spawn in a fresh
    // copy, Game::trySpawn); in the sandbox they bail out as before
    if (gGame && gGame->active && !gGame->isClient && gGame->local() && gGame->local()->alive) gGame->killSoldier(gGame->localSlot, -1, vec3(0, -1, 0));
    else { exitTank(app, true); app.toast("Your tank was knocked out - you bailed out.", 5); }
  }
}

void TankSys::applyToMap(App& app) {
  for (auto& [id, t] : tanks) {
    Instance* i = app.doc.find(id); if (!i || !t.def) continue;
    json o = i->extra.is_object() && i->extra.contains("tank") ? i->extra["tank"] : json::object();
    json n = o;
    n["hp"] = t.hp; n["wrecked"] = t.wrecked; n["hitsFront"] = t.hitsFront; n["hitsSide"] = t.hitsSide;
    n["reserveHE"] = t.reserveHE; n["reserveAP"] = t.reserveAP; n["loaded"] = shellName(t.loaded);   // a load in progress is not kept (restarts)
    if (t.hp == 100 && !t.wrecked && t.reserveHE == t.def->reserveHE && t.reserveAP == t.def->reserveAP && t.loaded == shellFrom(t.def->startLoaded) && !o.contains("hp")) continue;
    if (n == o) continue;
    if (!i->extra.is_object()) i->extra = json::object();
    i->extra["tank"] = n;
    i->rev = ++app.revCounter;
  }
}

// ============================================================ effects
void TankSys::effects(App& app, TankRT& t, float dt) {
  const TankDef& d = *t.def;
  vec3 cam = app.eyePos();
  if (glm::length(t.pos - cam) > 300.0f) return;   // LOD: no particles far away
  mat4 H = hullMatrix(t);
  vec3 vel = yawQuat(t.yaw) * vec3(0, 0, 1) * t.speed;
  // one smoke puff: small, varied, rising fast and expanding slowly, so a column of separate billows forms instead of
  // one swelling haze; puffs drift with a light wind and keep their own momentum
  auto puff = [&](vec3 p, float size, float life, float shade, float alpha, float rise) {
    vec3 v(app.fx.rnd() * 1.2f - 0.6f + 0.35f, rise * (0.8f + 0.4f * app.fx.rnd()), app.fx.rnd() * 1.2f - 0.6f + 0.15f);
    float g = shade * (0.75f + 0.5f * app.fx.rnd());
    app.fx.smoke(p, v, size * (0.7f + 0.6f * app.fx.rnd()), life * (0.8f + 0.4f * app.fx.rnd()), vec4(g, g, g * 0.95f, alpha * (0.8f + 0.4f * app.fx.rnd())), 0.45f + 0.5f * app.fx.rnd(), rise * 0.9f);
  };
  // flame tongue: additive, short-lived, licking upward from an opening
  auto flame = [&](vec3 p, float size) {
    if (app.fx.parts.size() >= app.fx.maxParts) app.fx.parts.erase(app.fx.parts.begin());
    Particle q; q.p = p + vec3(app.fx.rnd() * 0.3f - 0.15f, 0, app.fx.rnd() * 0.3f - 0.15f);
    q.v = vec3(app.fx.rnd() * 0.5f - 0.25f, 1.6f + 1.4f * app.fx.rnd(), app.fx.rnd() * 0.5f - 0.25f);
    q.size = size * (0.7f + 0.6f * app.fx.rnd()); q.life = q.maxLife = 0.35f + 0.45f * app.fx.rnd();
    float h = app.fx.rnd();
    q.col = vec4(6.0f + 3.0f * h, 2.2f + 1.6f * h, 0.5f + 0.4f * h, 1); q.additive = true; q.grow = -q.size * 0.9f; q.drag = 0.8f; q.lift = 2.5f;
    app.fx.parts.push_back(q);
  };
  if (t.wrecked) {
    t.burnT += dt;
    float fire = std::clamp(1.0f - t.burnT / 50.0f, 0.0f, 1.0f);   // ~50 s of open fire, then smouldering smoke only
    mat4 hm, tu, gm, rc; rig(t, hm, tu, gm, rc);
    vec3 top = vec3(tu * vec4(d.hatchEye - vec3(0, 0.55f, 0), 1));            // turret roof hatch / cupola
    vec3 deck = vec3(H * vec4(d.engine + vec3(0, 0.55f, 0), 1));               // engine deck grilles
    // fire from the turret top and the engine deck
    t.exhaustAcc += dt * (30.0f * fire);
    while (t.exhaustAcc >= 1) {
      t.exhaustAcc -= 1;
      flame(app.fx.rnd() < 0.55f ? top : deck + vec3(app.fx.rnd() - 0.5f, 0, app.fx.rnd() - 0.5f), 0.55f + 0.35f * fire);
      if (app.fx.rnd() < 0.08f) app.fx.flash(app.fx.rnd() < 0.5f ? top : deck, 1.2f, 0.12f, vec4(7, 3, 1, 1));
    }
    // billowing black smoke from both, thinning to a grey smoulder
    t.dustAcc += dt * (5.0f + 9.0f * fire);
    while (t.dustAcc >= 1) {
      t.dustAcc -= 1;
      bool fromTop = app.fx.rnd() < 0.5f;
      vec3 p = (fromTop ? top : deck) + vec3(0, 0.4f, 0);
      puff(p, 0.45f + 0.25f * fire, 6.5f, glm::mix(0.2f, 0.06f, fire), glm::mix(0.35f, 0.6f, fire), 2.2f + 1.2f * fire);
    }
    return;
  }
  bool engine = t.rpm > 0.02f;
  if (engine) {   // light exhaust from the actual outlets, load-dependent, emitted particles drift on their own
    t.exhaustAcc += dt * (3.0f + 22.0f * t.rpm) * (d.car ? 0.35f : 1.0f);   // VH1: a jeep's exhaust is a thin wisp
    while (t.exhaustAcc >= 1) {
      t.exhaustAcc -= 1;
      for (auto& e : d.exhausts) {
        vec3 p = vec3(H * vec4(e, 1)), back = vec3(H * vec4(0, -0.3f, -1, 0));
        app.fx.smoke(p, back * 0.7f + vel * 0.3f + vec3(0, 0.35f, 0), (0.18f + 0.15f * t.rpm) * (d.car ? 0.6f : 1.0f), d.car ? 1.2f : 2.2f, vec4(0.32f, 0.33f, 0.35f, (0.10f + 0.22f * t.rpm) * (d.car ? 0.5f : 1.0f)), d.car ? 0.8f : 1.5f, 0.35f);
      }
    }
  }
  if (d.heli) {   // VH5: downwash and the turbine instead of tracks, damage smoke below half
    heliEffects(app, t, dt);
    if (t.hp <= 50) { t.burnT += dt; if (std::fmod(t.burnT, 0.18f) < dt) puff(vec3(H * vec4(d.engine + vec3(0, 0.4f, 0), 1)), 0.45f, 4.5f, 0.15f, 0.45f, 1.8f); }
    return;
  }
  // dust from the tracks on dry ground when moving (none on wet ground)
  float spd = std::fabs(t.speed);
  if (spd > 1.0f && app.terrain.wetAt(t.pos.x, t.pos.z) < 0.5f) {
    t.dustAcc += dt * spd * (d.car ? 0.6f : 1.6f);   // VH1: a jeep raises far less dust than a tank
    while (t.dustAcc >= 1) {
      t.dustAcc -= 1;
      float side = app.fx.rnd() < 0.5f ? -1.0f : 1.0f;
      vec3 p = vec3(H * vec4(side * d.trackHalfGauge, 0.15f, t.speed > 0 ? -d.trackHalfLen : d.trackHalfLen, 1));
      app.fx.smoke(p, vec3(0, 0.5f, 0) - vel * 0.15f, d.car ? 0.3f : 0.45f, d.car ? 1.6f : 2.6f, vec4(0.48f, 0.42f, 0.33f, d.car ? 0.09f : 0.16f), 2.0f, 0.15f);
    }
  }
  // damage smoke from the engine deck once durability is at or below half (distinct from the exhaust)
  if (t.hp <= 50) {   // damaged engine: thinner grey-black billows from the deck
    t.burnT += dt;
    if (std::fmod(t.burnT, 0.22f) < dt) puff(vec3(H * vec4(d.engine + vec3(0, 0.5f, 0), 1)) + vel * 0.1f, 0.35f, 4.5f, 0.17f, 0.4f, 1.8f);
  }
  // sound: engine / tracks / turret drive loops (temporary synthesized sounds, docs/tanks.md#sound)
  vec3 ep = vec3(H * vec4(d.engine, 1));
  std::string eng = d.id == "tiger_i" ? "engine_tiger" : "engine_sherman";
  if (engine && t.engineLoop < 0) t.engineLoop = app.audio.loopStart(eng, ep, 0.0f, 0.7f);
  if (!engine && t.engineLoop >= 0) { app.audio.loopStop(t.engineLoop); t.engineLoop = -1; }
  if (d.car) {   // VH1: the Sherman loop pitched up for the jeep's four-cylinder (no recording supplied), quieter; no tracks / turret
    if (t.engineLoop >= 0) app.audio.loopSet(t.engineLoop, ep, 0.18f + 0.4f * t.rpm, 1.5f + 1.3f * t.rpm);
    return;
  }
  if (t.engineLoop >= 0) app.audio.loopSet(t.engineLoop, ep, 0.25f + 0.55f * t.rpm, 0.65f + 0.85f * t.rpm);
  float trackV = std::clamp(spd / std::max(1.0f, d.maxFwd) + std::fabs(t.yawRate) / 60.0f, 0.0f, 1.0f);
  if (trackV > 0.02f && t.trackLoop < 0) t.trackLoop = app.audio.loopStart("tank_tracks", t.pos, 0.0f, 1.0f);
  if (trackV <= 0.02f && t.trackLoop >= 0) { app.audio.loopStop(t.trackLoop); t.trackLoop = -1; }
  if (t.trackLoop >= 0) app.audio.loopSet(t.trackLoop, t.pos, 0.5f * trackV, 0.7f + 0.6f * trackV);
  float tv = std::fabs(t.tYawV) / std::max(1.0f, d.traverseRate);
  if (tv > 0.05f && t.turretLoop < 0) t.turretLoop = app.audio.loopStart("turret_drive", vec3(H * vec4(d.turretPivot, 1)), 0.0f, 1.0f);
  if (tv <= 0.05f && t.turretLoop >= 0) { app.audio.loopStop(t.turretLoop); t.turretLoop = -1; }
  if (t.turretLoop >= 0) app.audio.loopSet(t.turretLoop, vec3(H * vec4(d.turretPivot, 1)), 0.35f * tv, 0.8f + 0.4f * tv);
}

// ============================================================ T7: thin closed glass does not stop shells or rockets
bool shellThroughGlass(App& app, Projectile& p, vec3 hitPos, uint64_t ud) {
  if (p.kind == PROJ_BULLET || p.pens >= 6) return false;
  uint64_t low = ud & 0xFFFF;
  if (ud == 0 || (ud >> 60) == 0xF || low == 0 || low >= 0xFFF0u) return false;
  Instance* inst = app.doc.find(ud >> 16);
  auto it = app.rt.find(ud >> 16);
  if (!inst || it == app.rt.end()) return false;
  InstanceRT& r = it->second;
  int k = (int)low - 1;
  if (k < 0 || k >= (int)r.asset->interactables.size()) return false;
  int l = -1; bool onGlass = false;
  if (!app.locateLeaf(*inst, r, k, hitPos, l, onGlass) || !onGlass) return false;
  app.breakGlass(*inst, r, k, l, hitPos);   // idempotent: an already broken pane is an empty sash
  vec3 dir = glm::normalize(p.v);
  p.p = hitPos + dir * 0.06f; p.ignore = ud; p.pens++;   // one safe advance past this pane; never the same pane twice
  return true;
}
