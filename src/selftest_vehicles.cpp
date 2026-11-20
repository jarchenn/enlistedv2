// VH1 --capture-jeep: no suite, a look at the drivable vehicles (selftest_out/vehicles_*.png, frames with the HUD) and a
// few logged numbers. The nine types (VH2: the Humvee, then the Boxer APC last) are placed in a row in Creative (two overview frames); then each in turn is
// entered through its E prompt from the driver's door: the driver's view through the windshield, W held then D held
// (speed, smoothed steering, the wheel turned), handbrake to a stop, out again; for the first jeep, the cargo truck, the Humvee and the APC
// an outside frame of the steered front wheels; for the APC also the gunner's remote weapon station sight firing (wide
// and zoomed), and its armour against a rifle, a .50, a rocket
// an outside frame of the steered front wheels.
#include "selftest.h"
#include <functional>
#include <cstdarg>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
int mode = -1; bool started = false;
const char* kIds[] = {"car_jeep_willys", "car_jeep_ww2", "car_truck_cargo", "car_truck_canopy", "car_truck_medical", "car_truck_flak", "car_truck_kitchen", "car_humvee", "car_boxer_apc"};
const int kN = 9;
const int kHumvee = 7, kApc = 8;
uint64_t J[kN] = {};
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
void lookAtP(App& a, vec3 p) { vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
void standAt(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z), p.z)); }
void shot(App& a, const std::string& name) { a.uiShot = outDir() / ("vehicles_" + name + ".png"); }
void note(const char* fmt, ...) { char b[400]; va_list v; va_start(v, fmt); std::vsnprintf(b, sizeof b, fmt, v); va_end(v); logf("[vehicles] %s", b); }
TankRT& T(App& a, int k) { return *a.tanks.get(J[k]); }
std::set<long> marks;   // one-shot time marks (step index, ms)
bool at(float t, float m) { long key = (long)cur * 100000 + std::lround(m * 1000); if (t >= m && !marks.count(key)) { marks.insert(key); return true; } return false; }
std::string tag(App& a, int k) { return std::to_string(k + 1) + "_" + T(a, k).def->id; }

// VH3 --capture-abrams: the M1A2 on the ground in Creative, turret turned 60 deg and the gun up 5 deg (one frame)
void buildAbrams() {
  push("abrams", [](App& a, float t) {
    static uint64_t id = 0;
    if (t == 0) {
      a.newMap(*a.findTemplate("blank_slate"), "Abrams capture");
      const AssetDef* as = a.lib.find("tank_abrams_m1a2");
      if (!as) { note("FAIL: no asset tank_abrams_m1a2"); return true; }
      id = a.placeInstance(as, vec3(0, a.terrain.height(0, 0), 0), 0);
      a.syncRuntime(); a.tanks.sync(a);
      a.camPos = vec3(-9, a.terrain.height(0, 0) + 4.0f, 11); a.camYaw = -40; a.camPitch = -12;
    }
    TankRT* r = a.tanks.get(id); if (!r) { note("FAIL: no vehicle state"); return true; }
    r->tYaw = 60; r->gElev = 5;
    if (at(t, 0.8f)) { shot(a, "abrams_turret_turned"); note("abrams: %s, %zu parts, hp %d", r->def->name.c_str(), r->def->model ? r->def->model->parts.size() : 0, r->hp); }
    return t > 1.1f; });
}

void build(App&) {
  push("place", [](App& a, float t) {
    if (t == 0) {
      a.newMap(*a.findTemplate("blank_slate"), "Vehicle capture");
      float x = 0;
      for (int k = 0; k < kN; ++k) {
        const AssetDef* as = a.lib.find(kIds[k]);
        if (!as) { note("FAIL: no asset %s", kIds[k]); return true; }
        float w = as->bmax.x - as->bmin.x;
        x -= w * 0.5f + 1.6f; vec3 p(x, 0, -9.0f * k); x -= w * 0.5f + 1.6f;   // staggered back: each one drives off ahead and to the right, clear of the rest
        p.y = a.terrain.height(p.x, p.z);
        J[k] = a.placeInstance(as, p, 0);
      }
      a.syncRuntime(); a.tanks.sync(a);
      for (int k = 0; k < kN; ++k) {
        TankRT* r = a.tanks.get(J[k]);
        if (!r || !r->def) { note("FAIL: type %d has no vehicle state", k + 1); continue; }
        const TankDef& d = *r->def;
        note("type %d: %s '%s' model %s, %zu parts, wheels r %.2f track %.2f wheelbase %.2f, steering wheel (%.2f %.2f %.2f), eye (%.2f %.2f %.2f)", k + 1, d.id.c_str(), d.name.c_str(),
             jkey(d.raw, "model").value("dir", "").c_str(), d.model->parts.size(), d.wheelR, 2 * d.trackHalfGauge, 2 * d.trackHalfLen, d.steerC.x, d.steerC.y, d.steerC.z, d.driverEye.x, d.driverEye.y, d.driverEye.z);
      }
      a.camPos = T(a, 1).pos + vec3(7, 4.0f, 12); a.camYaw = 30; a.camPitch = -14;
      return false;
    }
    if (at(t, 0.4f)) shot(a, "00_creative_jeeps");
    if (at(t, 0.6f)) { a.camPos = T(a, 4).pos + vec3(16, 9, 30); a.camYaw = 33; a.camPitch = -14; }
    if (at(t, 1.0f)) shot(a, "00_creative_trucks");
    if (at(t, 1.1f)) { vec3 f = yawQuat(T(a, kApc).yaw) * vec3(0, 0, 1); a.camPos = T(a, kApc).pos + f * 9.0f + vec3(-8, 4.5f, 0); vec3 d = T(a, kApc).pos + vec3(0, 1.5f, 0) - a.camPos;
                       a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
    if (at(t, 1.4f)) shot(a, "00_creative_apc");
    return t > 1.7f; });
  push("explore", [](App& a, float t) { a.enterExplore(); a.useScripted = true; (void)t; return true; });
  for (int k = 0; k < kN; ++k) {
    push("approach " + std::to_string(k), [k](App& a, float t) {
      TankRT& r = T(a, k);
      if (t == 0) standAt(a, vec3(a.tanks.hullMatrix(r) * vec4(r.def->exits[0], 1)));
      lookAtP(a, vec3(a.tanks.hullMatrix(r) * vec4(r.def->driverEye - vec3(0.2f, 0.5f, 0), 1)));
      if (t < 0.4f) return false;
      note("type %d prompt %s", k + 1, a.tanks.target == J[k] ? "yes" : "NO");
      a.scriptedInput.interact = true; return true; });
    if (k == kApc) push("gunner " + std::to_string(k), [k](App& a, float t) {   // VH2: Ctrl to the RWS sight, a burst at a point ahead-right, RMB zoom, Ctrl back
      a.scriptedInput = ExploreInput();
      TankRT& r = T(a, k);
      a.scriptedInput.crouch = t > 0.3f && t < 0.45f || (t > 4.4f && t < 4.55f);
      if (t > 1.2f && t < 4.3f) { vec3 f = yawQuat(r.yaw) * vec3(0, 0, 1), l = yawQuat(r.yaw) * vec3(1, 0, 0); lookAtP(a, r.pos + f * 45.0f - l * 18.0f + vec3(0, 0.5f, 0)); }
      a.scriptedInput.fire = t > 2.0f && t < 3.0f;
      a.scriptedInput.aim = t > 3.2f && t < 3.3f;
      if (at(t, 1.0f)) note("type %d gunner station %s (hatchT %.2f)", k + 1, r.station == TS_HATCH ? "yes" : "NO", r.hatchT);
      if (at(t, 1.9f)) shot(a, tag(a, k) + "_d_gunner_sight");
      if (at(t, 2.6f)) { shot(a, tag(a, k) + "_e_gunner_firing"); note("type %d firing: %d rounds so far, belt %d, gun yaw %.1f elev %.1f", k + 1, r.mgShots, r.mgLoaded, r.mgYaw, r.mgElev); }
      if (at(t, 3.9f)) { shot(a, tag(a, k) + "_f_gunner_zoomed"); note("type %d zoom %d, %d rounds fired", k + 1, (int)a.tanks.zoom, r.mgShots); }
      if (t > 5.4f) { note("type %d back in the driver's seat: %s", k + 1, r.station == TS_GUNNER && r.hatchT == 0 ? "yes" : "NO"); return true; }
      return false; });
    push("drive " + std::to_string(k), [k](App& a, float t) {
      a.scriptedInput.interact = false;
      TankRT& r = T(a, k);
      if (at(t, 0.6f)) { note("type %d occupied %s", k + 1, a.tanks.occupied == J[k] ? "yes" : "NO"); shot(a, tag(a, k) + "_a_driver_view"); }
      if (t > 1.0f) a.scriptedInput.move = vec2(t > 2.6f ? 1.0f : 0.0f, t < 3.4f ? 1.0f : 0.0f);
      a.scriptedInput.jumpHeld = t > 3.4f;
      if (at(t, 2.6f)) note("type %d after 1.6 s of W: %.1f km/h", k + 1, r.speed * 3.6f);
      if (at(t, 3.3f)) { shot(a, tag(a, k) + "_b_steering_right"); note("type %d steering right: %.1f km/h steer %.2f yaw rate %.1f deg/s", k + 1, r.speed * 3.6f, r.steer, r.yawRate); }
      if (t > 3.4f && std::fabs(r.speed) < 0.3f) { note("type %d stopped on the handbrake %.2f s after it went on", k + 1, t - 3.4f); a.scriptedInput = ExploreInput(); a.scriptedInput.interact = true; return true; }
      return t > 12; });
    push("outside " + std::to_string(k), [k](App& a, float t) {
      a.scriptedInput.interact = false;
      TankRT& r = T(a, k);
      if (k != 0 && k != 2 && k != kHumvee && k != kApc) return t > 0.3f;
      vec3 f = yawQuat(r.yaw) * vec3(0, 0, 1), l = yawQuat(r.yaw) * vec3(1, 0, 0);
      float L = r.def->halfSize.z;
      if (at(t, 0.2f)) { note("type %d out: occupied %d", k + 1, (int)a.tanks.occupied); standAt(a, r.pos + f * (L + 3.0f) + l * 2.8f); }
      lookAtP(a, r.pos + f * (L * 0.5f) + vec3(0, 0.6f, 0));
      if (at(t, 0.7f)) shot(a, tag(a, k) + "_c_outside_wheels_steered");
      return t > 0.9f; });
  }
  push("armour", [](App& a, float t) {   // VH2: the APC against a rifle round (~3.6 kJ), a .50 (~18 kJ), a rocket
    (void)t; TankRT& r = T(a, kApc); vec3 side = vec3(a.tanks.hullMatrix(r) * vec4(1.48f, 1.5f, 0, 1)), n = yawQuat(r.yaw) * vec3(1, 0, 0);
    int h0 = r.hp; a.tanks.applyHit(a, J[kApc], TA_SMALL_ARMS, side, n, -n, 3600); int h1 = r.hp;
    a.tanks.applyHit(a, J[kApc], TA_SMALL_ARMS, side, n, -n, 18000); int h2 = r.hp;
    a.tanks.applyHit(a, J[kApc], TA_LAUNCHER, side, n, -n); int h3 = r.hp;
    note("type %d armour: rifle %d -> %d, .50 -> %d, rocket -> %d (%s)", kApc + 1, h0, h1, h2, h3, h1 == h0 && h2 < h1 && h3 < h2 && !r.wrecked ? "ok" : "UNEXPECTED");
    return true; });
  push("done", [](App& a, float t) { if (t < 0.3f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

bool vehiclesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { if (std::string(__argv[k]) == "--capture-jeep") mode = 1; if (std::string(__argv[k]) == "--capture-abrams") mode = 2; } }
  if (mode != 1 && mode != 2) return false;
  if (dt < 0) return true;
  if (!started) { started = true; if (mode == 2) buildAbrams(); else build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 300) cur = seq.size();
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = 0; app.selftest->done = true; }
  return true;
}
