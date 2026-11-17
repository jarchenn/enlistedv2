// RC2 --capture-rc2: the Tiger firing HE, from outside (left rear quarter) and from the gunner's sight, frames at
// +0.03 / 0.12 / 0.35 / 0.8 s after the shot: selftest_out/rc2_<view>_<k>.png (NMK_RC2_OFF named the "before" frames of the RC2 A/B, taken with a temporary switch)
#include "selftest.h"

namespace {
int phase = 0; float t = 0; fs::path pend; uint64_t tg = 0; float tShot = -1; int shot = 0;
const float kAt[4] = {0.03f, 0.12f, 0.35f, 0.8f};
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("rc2_" + std::string(std::getenv("NMK_RC2_OFF") ? "before_" : "after_") + n + ".png"); }
}
extern bool gVh8CamOn; extern vec3 gVh8CamEye, gVh8CamAt;

bool captureRc2SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-rc2") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  float t0 = t; t += dt;
  auto done = [&]() { gVh8CamOn = false; if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; return true; };
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "RC2 capture");
    tg = a.placeInstance(a.lib.find("tank_tiger_i"), vec3(0, a.terrain.height(0, 0), 0), 0);
    a.syncRuntime(); a.tanks.sync(a); a.enterExplore(); a.useScripted = true;
    if (!a.tanks.get(tg) || !a.tanks.enter(a, tg)) { logf("[rc2] no Tiger"); return done(); }
    phase = 1; t = 0; return true;
  }
  TankRT* T = a.tanks.get(tg);
  if (!T) return done();
  a.scriptedInput = ExploreInput{};
  const char* view = phase == 1 ? "outside" : "sight";
  if (phase == 1 && t0 < 0.1f && t >= 0.1f) { mat4 H = a.tanks.hullMatrix(*T); gVh8CamEye = vec3(H * vec4(7.5f, 3.2f, -6.5f, 1)); gVh8CamAt = vec3(H * vec4(0, 1.8f, 1.5f, 1)); gVh8CamOn = true; }
  if (phase == 2 && t0 < 0.1f && t >= 0.1f) gVh8CamOn = false;
  if (tShot < 0 && t > 2.5f) {   // loaded, laid level ahead
    T->loaded = SHELL_HE; T->reload = 0; a.tanks.fireCannon(a, *T); tShot = t; shot = 0;
    logf("[rc2] %s: fired, rock %.2f deg pitch / %.2f roll (rockDeg %.2f, recoil %.2f m), view kick %.2f deg", view, T->def->rockDeg >= 0 ? T->def->rockDeg : 9 * T->def->recoil,
         1.6f * (T->def->rockDeg >= 0 ? T->def->rockDeg : 9 * T->def->recoil), T->def->rockDeg, T->def->recoil, T->kickDeg);
  }
  if (tShot >= 0 && shot < 4 && t - tShot >= kAt[shot]) { pend = out(std::string(view) + "_" + std::to_string(shot)); ++shot; }
  if (tShot >= 0 && shot >= 4 && t - tShot > 1.0f) { if (phase == 2) return done(); phase = 2; t = 0; tShot = -1; }
  return true;
}
