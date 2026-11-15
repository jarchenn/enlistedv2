// VH1 --capture-jeep: no suite, a look at the three jeep types (selftest_out/jeep_*.png, frames with the HUD) and a
// few logged numbers: the three placed in Creative, the E prompt and the driver's seat (view through the windshield),
// W and then D held (speed, smoothed steering, the wheel turned, the front wheels steered), outside views of the
// steered car, the stripped variant and the pintle MG station (Ctrl), and the off-road top speed.
#include "selftest.h"
#include <functional>
#include <cstdarg>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
int mode = -1; bool started = false;
uint64_t J[3] = {0, 0, 0};
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
void lookAtP(App& a, vec3 p) { vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
void standAt(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z), p.z)); }
void shot(App& a, const char* name) { a.uiShot = outDir() / name; }
void note(App& a, const char* fmt, ...) { char b[400]; va_list v; va_start(v, fmt); std::vsnprintf(b, sizeof b, fmt, v); va_end(v); (void)a; logf("[jeep] %s", b); }
TankRT& T(App& a, int k) { return *a.tanks.get(J[k]); }
std::set<long> marks;   // one-shot time marks (step index, ms)
bool at(float t, float m) { long key = (long)cur * 100000 + std::lround(m * 1000); if (t >= m && !marks.count(key)) { marks.insert(key); return true; } return false; }

void build(App&) {
  push("place", [](App& a, float t) {
    if (t == 0) {
      a.newMap(*a.findTemplate("blank_slate"), "Jeep capture");
      const char* ids[3] = {"car_jeep_willys", "car_jeep_willys_open", "car_jeep_willys_mg"};
      for (int k = 0; k < 3; ++k) {
        const AssetDef* as = a.lib.find(ids[k]);
        if (!as) { note(a, "FAIL: no asset %s", ids[k]); return true; }
        vec3 p((k - 1) * 4.5f, 0, 0); p.y = a.terrain.height(p.x, p.z);
        J[k] = a.placeInstance(as, p, 0);
      }
      a.syncRuntime(); a.tanks.sync(a);
      for (int k = 0; k < 3; ++k) { TankRT* r = a.tanks.get(J[k]); note(a, "type %d: %s %s", k + 1, r && r->def ? r->def->id.c_str() : "-", r && r->def ? r->def->name.c_str() : "missing"); }
      a.camPos = vec3(-7, 3.2f, 8.5f); a.camYaw = -35; a.camPitch = -14;
      return false;
    }
    if (at(t, 0.4f)) shot(a, "jeep_00_creative_three_types.png");
    return t > 0.8f; });
  push("explore", [](App& a, float t) {
    if (t == 0) { a.enterExplore(); a.useScripted = true; standAt(a, T(a, 0).pos + vec3(2.4f, 0, -0.6f)); return false; }   // left of type 1 (driver's side)
    lookAtP(a, T(a, 0).pos + vec3(0.4f, 0.9f, -0.3f));
    if (at(t, 0.5f)) shot(a, "jeep_01_enter_prompt.png");
    if (t < 0.8f) return false;
    note(a, "prompt target %s", a.tanks.target == J[0] ? "type 1" : "none");
    a.scriptedInput.interact = true; return true; });
  push("seat", [](App& a, float t) {
    a.scriptedInput.interact = false;
    if (at(t, 0.6f)) { note(a, "occupied %s", a.tanks.occupied == J[0] ? "type 1" : "no"); shot(a, "jeep_02_driver_view.png"); }
    if (at(t, 0.9f)) { a.eyeYaw += 70; a.eyePitch = -30; }   // head turned left and down: the cab side
    if (at(t, 1.3f)) shot(a, "jeep_03_driver_look_left_down.png");
    if (at(t, 1.5f)) { a.eyeYaw -= 70; a.eyePitch = -12; }
    return t > 1.8f; });
  push("drive", [](App& a, float t) {
    a.scriptedInput.move = vec2(0, 1);
    if (t > 2.0f) a.scriptedInput.move.x = 1;   // D: steer right
    TankRT& k = T(a, 0);
    if (at(t, std::floor(t * 2) * 0.5f)) note(a, "t %.2f speed %.1f km/h steer %.2f yawRate %.1f pos (%.1f %.1f %.1f) blocked '%s'", t, k.speed * 3.6f, k.steer, k.yawRate, k.pos.x, k.pos.y, k.pos.z, k.blockedBy.c_str());
    if (at(t, 2.6f)) shot(a, "jeep_04_driver_steering_right.png");
    return t > 2.8f; });
  push("stop", [](App& a, float t) {
    a.scriptedInput.move = vec2(0, 0); a.scriptedInput.jumpHeld = true;   // handbrake, the wheel held right
    a.scriptedInput.move.x = 1;
    TankRT& k = T(a, 0);
    if (std::fabs(k.speed) < 0.3f || t > 6) { a.scriptedInput.jumpHeld = false; a.scriptedInput.move = vec2(0); note(a, "stopped after %.2f s on the handbrake, steer %.2f", t, k.steer); a.scriptedInput.interact = true; return true; }
    return false; });
  push("outside", [](App& a, float t) {
    a.scriptedInput.interact = false;
    TankRT& k = T(a, 0);
    if (t < 0.1f) return false;
    vec3 f = yawQuat(k.yaw) * vec3(0, 0, 1), l = yawQuat(k.yaw) * vec3(1, 0, 0);
    if (t < 0.15f) { note(a, "out: occupied %d", (int)a.tanks.occupied); standAt(a, k.pos + f * 4.2f + l * 2.6f); }
    lookAtP(a, k.pos + vec3(0, 0.6f, 0));
    if (at(t, 0.5f)) shot(a, "jeep_05_outside_front_wheels_steered.png");
    if (at(t, 0.7f)) standAt(a, k.pos - f * 4.0f - l * 3.0f + vec3(0, 0.0f, 0));
    if (at(t, 1.1f)) shot(a, "jeep_06_outside_rear_three_quarter.png");
    return t > 1.3f; });
  push("variants", [](App& a, float t) {
    TankRT& o = T(a, 1);
    if (t < 0.05f) standAt(a, o.pos + vec3(-3.2f, 0, 4.2f));
    lookAtP(a, o.pos + vec3(0, 0.7f, 0));
    if (at(t, 0.4f)) shot(a, "jeep_07_type2_windshield_down.png");
    if (at(t, 0.6f)) standAt(a, T(a, 2).pos + vec3(3.0f, 0, 3.6f));
    if (t > 0.6f) lookAtP(a, T(a, 2).pos + vec3(0, 1.0f, 0));
    if (at(t, 1.0f)) shot(a, "jeep_08_type3_pintle_m2.png");
    if (at(t, 1.2f)) { standAt(a, T(a, 2).pos + vec3(-2.4f, 0, -0.6f)); }
    if (t > 1.25f) lookAtP(a, T(a, 2).pos + vec3(0.3f, 0.9f, -0.3f));
    if (t > 1.6f) { a.scriptedInput.interact = true; return true; }
    return false; });
  push("mg", [](App& a, float t) {
    a.scriptedInput.interact = false;
    if (at(t, 0.3f)) { note(a, "type 3 occupied %d", (int)(a.tanks.occupied == J[2])); a.scriptedInput.crouch = true; }
    if (t > 0.5f) a.scriptedInput.crouch = false;
    if (at(t, 1.4f)) { TankRT& k = T(a, 2); note(a, "type 3 station %s hatchT %.2f", k.station == TS_HATCH ? "MG" : "driver", k.hatchT); a.eyeYaw -= 60; a.eyePitch = -4; }
    if (at(t, 1.7f)) shot(a, "jeep_09_type3_at_the_m2.png");
    a.scriptedInput.fire = t > 1.8f && t < 2.4f;
    if (at(t, 2.1f)) shot(a, "jeep_10_type3_firing.png");
    if (at(t, 2.6f)) { TankRT& k = T(a, 2); note(a, "type 3 M2 belt %d / %d", k.mgLoaded, k.mgReserve); }
    return t > 2.8f; });
  push("done", [](App& a, float t) { if (t < 0.3f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

bool vehiclesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-jeep") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) { started = true; build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 120) cur = seq.size();
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = 0; app.selftest->done = true; }
  return true;
}
