// Tanks, Explore simulation: session state, driving, turret / gun, hatch MG, HE/AP loading, damage rules, wrecks,
// entry / exit, effects and persistence. docs/tanks.md
#include "app.h"
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
  return glm::translate(mat4(1), t.pos) * glm::mat4_cast(yawQuat(t.yaw)) *
         glm::rotate(mat4(1), radians(-(t.pitch + t.suspPitch)), vec3(1, 0, 0)) * glm::rotate(mat4(1), radians(t.roll + t.suspRoll), vec3(0, 0, 1));
}
void TankSys::rig(const TankRT& t, mat4& hull, mat4& turret, mat4& gun, mat4& recoil) const {
  const TankDef& d = *t.def;
  hull = hullMatrix(t);
  turret = hull * glm::translate(mat4(1), d.turretPivot) * glm::rotate(mat4(1), radians(t.tYaw), vec3(0, 1, 0)) * glm::translate(mat4(1), -d.turretPivot);
  mat4 g = glm::translate(mat4(1), d.gunPivot) * glm::rotate(mat4(1), radians(-t.gElev), vec3(1, 0, 0));
  gun = turret * g * glm::translate(mat4(1), -d.gunPivot);
  float r = t.recoilT, off = 0;
  if (r < 0.05f) off = d.recoil * (r / 0.05f);
  else if (r < 0.9f) { float u = (r - 0.05f) / 0.85f; off = d.recoil * (1 - u * u * (3 - 2 * u)); }
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
void TankSys::groundPose(App& app, const TankDef& d, vec3 pos, float yaw, float& y, float& pitch, float& roll, float& slopeAlong) const {
  quat q = yawQuat(yaw);
  float G = d.trackHalfGauge, L = d.trackHalfLen;
  auto h = [&](float x, float z) { vec3 w = pos + q * vec3(x, 0, z); return app.terrain.height(w.x, w.z); };
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
  if (n.wrecked) { n.hp = 0; n.loaded = SHELL_NONE; n.burnT = 0; }
  float y, p, r, s; sys.groundPose(app, d, n.pos, n.yaw, y, p, r, s);
  n.pos.y = y; n.pitch = p; n.roll = r;
  t = n;
}

void TankSys::sync(App& app) {
  std::set<uint64_t> live;
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end()) continue;
    const TankDef* d = defFor(it->second.asset);
    if (!d) continue;
    live.insert(i.id);
    auto& t = tanks[i.id];
    bool fresh = t.def != d || t.id != i.id;
    if (fresh || t.rev != i.rev) {
      if (fresh || app.mode == Mode::Creative) initFromInstance(app, *this, t, i, *d);
      t.rev = i.rev;
    }
    if (t.body != it->second.staticBody) { t.body = it->second.staticBody; t.shapeYaw = 1e9f; }
    updateBody(app, t, false);
  }
  for (auto it = tanks.begin(); it != tanks.end();) {
    if (!live.count(it->first)) {
      for (int* h : {&it->second.engineLoop, &it->second.trackLoop, &it->second.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
      if (occupied == it->first) occupied = 0;
      it = tanks.erase(it);
    } else ++it;
  }
}

void TankSys::reset(App& app) {
  occupied = 0; target = 0; fireArmed = false; zoom = false; lastHit = {};
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
  if (force || std::fabs(wrap180(t.tYaw - t.shapeYaw)) > 2.0f) {
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
      if (it != tanks.end()) target = it->first;
    }
  }
  for (auto& [id, t] : tanks) stepOne(app, t, dt, id == occupied ? &in : nullptr);
  stepMs = (glfwGetTime() - t0) * 1000.0; stepMsMax = std::max(stepMsMax, stepMs);
}

bool TankSys::handleInteract(App& app, const ExploreInput& in) {
  if (!in.interact || occupied || !target) return false;
  TankRT* t = get(target);
  if (t && t->wrecked) { app.toast(t->def->name + " is destroyed.", 3); return true; }
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
  // the desired aim starts on the gun's achieved line (no turret swing on entry)
  vec3 dir; muzzleWorld(*t, dir);
  app.eyeYaw = wrapDeg(degrees(std::atan2(-dir.x, -dir.z))); app.eyePitch = degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
  // the infantry body waits inside the hull (no collision update while crewing)
  Physics::setCharacterPos(t->pos + vec3(0, 1.0f, 0));
  app.audio.play("steel_close", t->pos + vec3(0, 2.5f, 0), 0.8f);
  app.toast("In " + t->def->name + " - gunner sight. W/S A/D drive | mouse aim | LMB fire | R next shell HE/AP | Ctrl hatch / roof MG | RMB zoom | E get out", 6);
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
  t.recoilT += dt;
  if (t.wrecked) {
    t.speed = 0; t.yawRate = 0; t.reload = 0;
    for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
    effects(app, t, dt);
    return;
  }
  bool focused = app.selftest || glfwGetWindowAttrib(app.win, GLFW_FOCUSED);
  t.throttleIn = in ? std::clamp(in->move.y, -1.0f, 1.0f) : 0.0f;
  t.steerIn = in ? std::clamp(in->move.x, -1.0f, 1.0f) : 0.0f;
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
    // RMB click toggles the sight magnification (zooms into the current view, eased over 0.18 s); the hatch drops it
    if (in->aim && !aimPrev && t.station == TS_GUNNER) zoom = !zoom;
    aimPrev = in->aim;
    if (t.station != TS_GUNNER) zoom = false;
    zoomT = approach(zoomT, zoom ? 1.0f : 0.0f, dt / 0.18f);
  }
  t.hatchT = approach(t.hatchT, t.station == TS_HATCH ? 1.0f : 0.0f, dt / 0.8f);
  // ---- drive (hull movement in both stations: single-player crew abstraction)
  drive(app, t, dt);
  // ---- main gun follows the desired aim only from the gunner position
  aimGun(t, dt, in && t.station == TS_GUNNER && t.hatchT == 0);
  // ---- roof MG follows the desired aim in hatch mode, converging on the camera's aim point
  if (in && t.station == TS_HATCH && t.hatchT >= 1.0f) {
    vec3 eye, fwd; float fov;
    camera(app, eye, fwd, fov);
    Physics::Hit ah; vec3 aimP = eye + fwd * 300.0f;
    if (Physics::castRayEx(eye, fwd, 300.0f, ah, bodyUD(t.id))) aimP = ah.pos;
    mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
    vec3 piv = vec3(tu * vec4(d.mgPivot, 1));
    vec3 l = glm::inverse(glm::mat3(tu)) * (aimP - piv);
    float ty = degrees(std::atan2(l.x, l.z)), te = degrees(std::atan2(l.y, glm::length(vec2(l.x, l.z))));
    ty = std::clamp(wrap180(ty), d.mgYawMin, d.mgYawMax); te = std::clamp(te, d.mgElevMin, d.mgElevMax);
    t.mgYaw = wrap180(t.mgYaw + std::clamp(wrap180(ty - t.mgYaw), -150.0f * dt, 150.0f * dt));
    t.mgElev = approach(t.mgElev, te, 120.0f * dt);
  }
  // ---- fire: LMB goes to exactly one weapon (cannon in the sight, roof MG in the hatch); a fresh press after any change
  bool fireHeld = in && in->fire && focused;
  // stale input: after a pause menu / focus loss (a gap in the fixed steps) the trigger needs a fresh press
  if (in) { if (!focused || (!app.selftest && app.now - lastStepTime > 0.25)) fireArmed = false; else if (!fireHeld) fireArmed = true; lastStepTime = app.now; }
  if (in && fireHeld && fireArmed) {
    if (t.station == TS_GUNNER && t.hatchT == 0) {
      fireArmed = false;   // one round per click
      if (t.loaded != SHELL_NONE) fireCannon(app, t);
      else { app.audio.play("click", t.pos + vec3(0, 2, 0), 0.6f); app.toast(t.reload > 0 ? "Loading..." : "No round loaded.", 1.5); }
    } else if (t.station == TS_HATCH && t.hatchT >= 1.0f) fireMg(app, t);
  }
  t.mgCool = std::max(0.0f, t.mgCool - dt);
  if (t.mgReloadT > 0) {
    t.mgReloadT -= dt;
    if (t.mgReloadT <= 0) { t.mgReloadT = 0; int n = std::min(d.mgBelt - t.mgLoaded, t.mgReserve); t.mgLoaded += n; t.mgReserve -= n; app.audio.play("belt", t.pos + vec3(0, 2.8f, 0), 0.6f); }
  }
  // ---- HE/AP loading (toggle, then the timer); runs with or without the player inside (abstract crew loader)
  stepAmmo(app, t, dt, in && in->reload);
  // ---- exit
  if (in && in->interact) exitTank(app, false);
  updateBody(app, t, false);
  effects(app, t, dt);
}

void TankSys::drive(App& app, TankRT& t, float dt) {
  const TankDef& d = *t.def;
  bool road = app.terrain.wearAt(t.pos.x, t.pos.z) > 0.5f;
  float vF = d.maxFwd * (road ? 1.0f : d.offroadMul), vR = d.maxRev;
  float y0, p0, r0, slope;
  groundPose(app, d, t.pos, t.yaw, y0, p0, r0, slope);
  // slope: uphill in the direction of travel reduces the attainable speed to zero at the slope limit
  float dirSign = t.throttleIn > 0 ? 1.0f : t.throttleIn < 0 ? -1.0f : (t.speed >= 0 ? 1.0f : -1.0f);
  float uphill = slope * dirSign;
  float slopeK = std::clamp(1.0f - std::max(0.0f, uphill) / d.maxSlopeDeg, 0.0f, 1.0f);
  float target = t.throttleIn > 0 ? vF * t.throttleIn * slopeK : t.throttleIn < 0 ? -vR * (-t.throttleIn) * slopeK : 0.0f;
  bool braking = (t.throttleIn > 0 && t.speed < -0.05f) || (t.throttleIn < 0 && t.speed > 0.05f);
  float rate = braking ? d.brake : t.throttleIn == 0 ? d.coast + d.brake * 0.25f * (std::fabs(t.speed) < 1 ? 1.0f : 0.0f) : (std::fabs(target) < std::fabs(t.speed) ? d.brake * 0.6f : d.accel);
  t.speed = approach(t.speed, target, rate * dt);
  // downhill roll / uphill drag when coasting (bounded, deterministic)
  // (a parked tank holds on its brakes; only a crewed, still-rolling tank feels the slope)
  if (t.throttleIn == 0 && occupied == t.id && std::fabs(t.speed) > 0.5f && std::fabs(slope) > 4.0f) t.speed -= 9.81f * std::sin(radians(slope)) * 0.15f * dt;
  // tracked steering: pivot turn at standstill, a speed-dependent turn rate when moving; yaw + turns the nose left
  float spd = std::fabs(t.speed);
  float turn = glm::mix(d.pivotRate, d.turnRate, std::min(1.0f, spd / 2.0f)) * (spd > 0.5f * d.maxFwd ? glm::mix(1.0f, 0.7f, (spd - 0.5f * d.maxFwd) / (0.5f * d.maxFwd)) : 1.0f);
  t.yawRate = approach(t.yawRate, -t.steerIn * turn, 70.0f * dt);
  if (std::fabs(t.speed) < 1e-3f && std::fabs(t.yawRate) < 1e-3f) {
    // parked: settle on the ground (craters can open under a parked tank)
    t.speed = 0; t.yawRate = 0;
    t.pos.y = approach(t.pos.y, y0, 2.0f * dt); t.pitch = approach(t.pitch, p0, 20 * dt); t.roll = approach(t.roll, r0, 20 * dt);
  } else {
    float ny = wrapDeg(t.yaw + t.yawRate * dt);
    vec3 np = t.pos + yawQuat(ny) * vec3(0, 0, 1) * t.speed * dt;
    float y, p, r, s;
    groundPose(app, d, np, ny, y, p, r, s);
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
    if (ok && Physics::boxBlocked(c, he, hq, bodyUD(t.id), true)) {
      ok = false;
      // what is in the way? weak props are crushed through the shared damage paths, everything else stops the tank
      vec3 fwd = vec3(H * vec4(0, 0, t.speed >= 0 ? 1 : -1, 0));
      uint64_t hitUd = 0; vec3 hitP(0);
      for (float hy : {0.6f, 1.2f, 0.3f})
        for (float hx : {0.0f, -d.halfSize.x * 0.8f, d.halfSize.x * 0.8f}) {
          if (hitUd) break;
          Physics::Hit bh; vec3 o = vec3(H * vec4(hx, hy, 0, 1));
          if (Physics::castRayEx(o, fwd, d.halfSize.z + 1.0f, bh, bodyUD(t.id)) && bh.ud != 0 && (bh.ud >> 60) != 0xF) { hitUd = bh.ud; hitP = bh.pos; }
        }
      Instance* bi = hitUd ? app.doc.find(hitUd >> 16) : nullptr;
      auto br = bi ? app.rt.find(bi->id) : app.rt.end();
      if (bi && br != app.rt.end()) {
        const AssetDef* ba = br->second.asset;
        if (ba->tree.on && !ba->tree.hedge && !br->second.treeFelled) { app.fellTree(*bi, br->second, hitP, fwd * 4.0f); ++t.crushes; t.speed *= 0.5f; why = "pushed over " + ba->name; }
        else if (!ba->defense.kind.empty() && !ba->defense.blocksVehicles && ba->defense.kind != "sandbag" && !br->second.defDestroyed) { app.destroyDefense(*bi, br->second); ++t.crushes; t.speed *= 0.6f; why = "crushed " + ba->name; }
        else why = ba->name;
      } else if (hitUd) why = "an obstacle";
      else why = "an obstacle";
    }
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
      if (t.blockedBy != why && occupied == t.id && why.rfind("pushed", 0) != 0 && why.rfind("crushed", 0) != 0) app.toast("Blocked: " + why, 2);
      t.blockedBy = why;
      if (why.rfind("pushed", 0) != 0 && why.rfind("crushed", 0) != 0) { t.speed = 0; t.yawRate = 0; }
    }
  }
  // suspension kick from firing decays back (visual only)
  t.suspPitch = approach(t.suspPitch, 0.0f, 6.0f * dt);
  t.suspRoll = approach(t.suspRoll, 0.0f, 6.0f * dt);
  // track speeds (+x / -x side) for the HUD, sounds and dust
  float w = radians(t.yawRate);
  t.trackPhase[0] += (t.speed + w * d.trackHalfGauge) * dt;
  t.trackPhase[1] += (t.speed - w * d.trackHalfGauge) * dt;
  float load = std::clamp(std::fabs(t.throttleIn) * 0.6f + std::fabs(t.steerIn) * 0.3f + std::max(0.0f, slope * dirSign) / d.maxSlopeDeg * 0.5f, 0.0f, 1.0f);
  float rpmT = (occupied == t.id ? 0.2f : 0.0f) + 0.55f * std::fabs(t.speed) / std::max(1.0f, d.maxFwd) + 0.35f * load;
  if (occupied != t.id && std::fabs(t.speed) < 0.05f) rpmT = 0;
  t.rpm = approach(t.rpm, std::min(rpmT, 1.0f), 0.8f * dt);
}

void TankSys::aimGun(TankRT& t, float dt, bool active) {
  const TankDef& d = *t.def;
  extern App* gApp;
  float dy = 0, de = 0;
  if (active && gApp) {
    vec3 want = App::forward(gApp->eyeYaw, gApp->eyePitch);
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
  app.fireProjectile(start, fireDir, v, PROJ_HE, ap ? 120.0f : d.dmgHE, bodyUD(t.id), false, ap ? d.ammoAP : d.ammoHE);
  if (!app.projectiles.empty()) { auto& pr = app.projectiles.back(); pr.at = ap; pr.atKill = true; }
  t.loaded = SHELL_NONE; ++t.shots; t.recoilT = 0;
  t.suspPitch += 1.2f;   // the hull rocks back on its suspension
  app.fx.muzzle(muzzle, dir, 1.2f);
  app.audio.play(t.def->id == "tiger_i" ? "tank_88" : "tank_17pdr", muzzle, 1.0f);
  app.viewKickPitch = 0.6f;
}

void TankSys::fireMg(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  if (t.mgCool > 0 || t.mgReloadT > 0) return;
  if (t.mgLoaded <= 0) {
    if (t.mgReserve > 0) { t.mgReloadT = d.mgReload; app.toast(d.mgName.empty() ? "Reloading MG" : "Reloading " + d.mgName.substr(0, d.mgName.find(" (")), 2); app.audio.play("belt", t.pos + vec3(0, 2.8f, 0), 0.6f); }
    else { app.audio.play("click", t.pos + vec3(0, 2.8f, 0), 0.6f); fireArmed = false; }
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
  app.fireProjectile(m, fd, d.mgVel, PROJ_BULLET, 0, bodyUD(t.id), (t.mgShots % 5) == 0, d.mgName);
  if (!app.projectiles.empty()) app.projectiles.back().mass = d.mgBulletMass;
  --t.mgLoaded; ++t.mgShots; t.mgCool = 60.0f / d.mgRpm;
  app.fx.flash(m + dir * 0.06f, 0.2f, 0.04f, vec4(9, 6, 2.5f, 1));
  app.audio.play(d.mgSound, m, 0.9f);
  app.viewKickPitch = 0.25f;
}

// ============================================================ camera
bool TankSys::camera(const App& app, vec3& eye, vec3& fwd, float& fov) const {
  auto it = tanks.find(occupied);
  if (it == tanks.end() || !it->second.def) return false;
  const TankRT& t = it->second; const TankDef& d = *t.def;
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
  int attack = p.kind == PROJ_BULLET ? TA_SMALL_ARMS : p.kind == PROJ_ROCKET ? TA_LAUNCHER : TA_CANNON;
  return applyHit(app, ud >> 16, attack, pos, normal, p.v);
}

TankHitResult TankSys::applyHit(App& app, uint64_t id, int attack, vec3 pos, vec3 normal, vec3 dir) {
  TankHitResult r; r.tank = true; r.id = id; r.attack = attack;
  TankRT* t = get(id);
  if (!t) { r.tank = false; return r; }
  bool onTurret = false;
  r.zone = classify(*t, pos, normal, onTurret);
  static const char* an[] = {"small arms", "cannon", "launcher"};
  if (t->wrecked) { r.info = std::string(an[attack]) + " on the wreck"; lastHit = r; return r; }
  // the matrix: cannon (tank gun, AT gun, Flak) counts from every side; launchers only side / rear; small arms never.
  // roof / underside: cannon counts as a side-class hit, launchers do not count (no rule given -> conservative)
  bool counted = attack == TA_CANNON || (attack == TA_LAUNCHER && (r.zone == TZ_SIDE || r.zone == TZ_REAR));
  r.counted = counted;
  r.consumed = attack == TA_SMALL_ARMS || (attack == TA_LAUNCHER && !counted);   // no penetration, no blast
  char b[200];
  if (counted) {
    int dmg = r.zone == TZ_FRONT ? t->def->frontDamage() : t->def->sideDamage();
    t->hp = std::max(0, t->hp - dmg);
    if (r.zone == TZ_FRONT) ++t->hitsFront; else ++t->hitsSide;
    std::snprintf(b, sizeof b, "%s hit, %s %s: durability %d (front hits %d, side/rear hits %d)", an[attack], onTurret ? "turret" : "hull", tankZoneName(r.zone), t->hp, t->hitsFront, t->hitsSide);
    r.info = b;
    app.fx.impact(pos, normal, "metal");
    app.fx.flash(pos + normal * 0.1f, 0.6f, 0.08f, vec4(8, 5, 2, 1));
    if (t->hp <= 0) { destroy(app, *t, r.info); r.destroyed = true; }
    else if (occupied == id) app.toast("HIT - " + std::string(onTurret ? "turret " : "hull ") + tankZoneName(r.zone) + ", durability " + std::to_string(t->hp), 3);
  } else {
    ++t->hitsIgnored;
    std::snprintf(b, sizeof b, "%s on the %s %s: no penetration (no damage)", an[attack], onTurret ? "turret" : "hull", tankZoneName(r.zone));
    r.info = b;
    if (attack == TA_LAUNCHER) {   // visible non-penetrating detonation on the armour: flash, sparks, smoke; no blast stamp
      app.fx.flash(pos + normal * 0.15f, 1.1f, 0.1f, vec4(9, 6, 3, 1));
      app.fx.impact(pos, normal, "metal");
      for (int k = 0; k < 6; ++k) app.fx.smoke(pos + normal * 0.2f, normal * 0.8f + vec3(0, 0.5f, 0), 0.35f, 2.5f, vec4(0.35f, 0.33f, 0.3f, 0.5f), 1.6f, 0.4f);
      app.audio.play("explosion", pos, 0.55f);
    } else app.fx.impact(pos, normal, "metal");
  }
  (void)dir;
  lastHit = r;
  return r;
}

void TankSys::destroy(App& app, TankRT& t, const std::string& why) {
  if (t.wrecked) return;   // exactly once
  t.wrecked = true; t.hp = 0; t.reload = 0; t.loaded = SHELL_NONE; t.speed = 0; t.yawRate = 0; t.mgReloadT = 0;
  t.burnT = 0; ++wrecksMade;
  for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
  vec3 c = t.pos + vec3(0, 1.8f, 0);
  app.fx.explosion(c, vec3(0, 1, 0), "metal", false);
  app.fx.flash(c, 4.0f, 0.25f, vec4(9, 5, 2, 1));
  app.audio.play("explosion", c, 1.0f);
  app.toast(t.def->name + " destroyed (" + why + ")", 5);
  if (occupied == t.id) { exitTank(app, true); app.toast("Your tank was knocked out - you bailed out.", 5); }
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
    t.exhaustAcc += dt * (3.0f + 22.0f * t.rpm);
    while (t.exhaustAcc >= 1) {
      t.exhaustAcc -= 1;
      for (auto& e : d.exhausts) {
        vec3 p = vec3(H * vec4(e, 1)), back = vec3(H * vec4(0, -0.3f, -1, 0));
        app.fx.smoke(p, back * 0.7f + vel * 0.3f + vec3(0, 0.35f, 0), 0.18f + 0.15f * t.rpm, 2.2f, vec4(0.32f, 0.33f, 0.35f, 0.10f + 0.22f * t.rpm), 1.5f, 0.35f);
      }
    }
  }
  // dust from the tracks on dry ground when moving (none on wet ground)
  float spd = std::fabs(t.speed);
  if (spd > 1.0f && app.terrain.wetAt(t.pos.x, t.pos.z) < 0.5f) {
    t.dustAcc += dt * spd * 1.6f;
    while (t.dustAcc >= 1) {
      t.dustAcc -= 1;
      float side = app.fx.rnd() < 0.5f ? -1.0f : 1.0f;
      vec3 p = vec3(H * vec4(side * d.trackHalfGauge, 0.15f, t.speed > 0 ? -d.trackHalfLen : d.trackHalfLen, 1));
      app.fx.smoke(p, vec3(0, 0.5f, 0) - vel * 0.15f, 0.45f, 2.6f, vec4(0.48f, 0.42f, 0.33f, 0.16f), 2.0f, 0.15f);
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
