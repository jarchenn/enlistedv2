// VH7 --capture-vh7: the Bradley from 3 m (front three-quarter, side; scene only), its gunner sight, the commander popped
// out of the hatch (his view), a TOW in mid-flight toward a Tiger 150 m out (the gunner's sight, the wire trail), then the
// Abrams' redesigned sight. selftest_out/vh7_*.png; numbers in [vh7] lines.
#include "selftest.h"

namespace {
int phase = 0; float t = 0; fs::path pend; uint64_t brad = 0, tgt = 0, abr = 0;
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("vh7_" + n + ".png"); }
void look(App& a, vec3 eye, vec3 at) { vec3 d = at - eye; a.camPos = eye; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
}

bool captureVh7SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-vh7") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  float t0 = t; t += dt;
  auto at = [&](float m) { return t0 < m && t >= m; };
  auto done = [&]() { if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; return true; };
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "VH7 capture");
    const AssetDef* b = a.lib.find("tank_bradley_m2"); const AssetDef* tg = a.lib.find("tank_tiger_i");
    if (!b || !tg) { logf("[vh7] missing asset"); return done(); }
    brad = a.placeInstance(b, vec3(0, a.terrain.height(0, 0), 0), 0);
    tgt = a.placeInstance(tg, vec3(8, a.terrain.height(8, 150), 150), 200);
    a.syncRuntime(); a.tanks.sync(a);
    phase = 1; t = 0; return true;
  }
  TankRT* bt = a.tanks.get(brad);
  if (!bt) { logf("[vh7] no Bradley state"); return done(); }
  if (phase == 1) {   // close-ups (Creative camera)
    logf("[vh7] Bradley %s: model %s, %zu parts", bt->def->name.c_str(), bt->def->model ? "loaded" : "MISSING", bt->def->model ? bt->def->model->parts.size() : 0);
    if (at(0.2f)) look(a, bt->pos + vec3(3.9f, 3.0f, 5.4f), bt->pos + vec3(0, 1.6f, 0));
    if (at(0.8f)) pend = out("front34");
    if (at(1.0f)) look(a, bt->pos + vec3(4.9f, 1.9f, 0.2f), bt->pos + vec3(0, 1.5f, 0));
    if (at(1.6f)) pend = out("side");
    if (t > 2.0f) { a.enterExplore(); a.useScripted = true; phase = 2; t = 0; }
    return true;
  }
  ExploreInput& in = a.scriptedInput;
  if (phase == 2) {   // the gunner's sight, a TOW toward the Tiger, popping out
    in = ExploreInput{};
    if (at(0.3f)) { bool ok = a.tanks.enter(a, brad); logf("[vh7] enter the Bradley: %s", ok ? "yes" : "NO"); vec3 to = vec3(8, 1.5f, 150) - bt->pos; a.eyeYaw = degrees(std::atan2(-to.x, -to.z)); a.eyePitch = -0.6f; }   // the Tiger's hull centre from the gunner's sight
    if (at(3.0f)) a.uiShot = out("gunner_sight");
    if (at(3.4f)) { a.tanks.fireTow(a, *bt); logf("[vh7] TOW fired: %d left, %zu trails", bt->towLeft, a.tanks.towTrails.size()); }
    for (float m : {3.6f, 3.8f, 4.0f, 4.3f, 4.8f}) if (at(m)) for (auto& pr : a.projectiles) if (pr.towOf) logf("[vh7] t %.1f TOW at (%.1f %.1f %.1f) v (%.0f %.0f %.0f)", m, pr.p.x, pr.p.y, pr.p.z, pr.v.x, pr.v.y, pr.v.z);
    if (at(3.95f)) { size_t n = a.tanks.towTrails.empty() ? 0 : a.tanks.towTrails[0].p.size(); logf("[vh7] TOW mid-flight: trail %zu points", n); a.uiShot = out("tow_midflight"); }
    if (at(5.5f)) { const TankRT* tg = a.tanks.get(tgt); logf("[vh7] after the TOW: Tiger durability %d, wrecked %d, trails %zu", tg ? tg->hp : -1, tg ? (int)tg->wrecked : -1, a.tanks.towTrails.size()); }
    if (at(6.0f)) { bt->station = TS_HATCH; a.eyeYaw += 70; a.eyePitch = -5; }   // pop out (the Ctrl toggle's state) and look round
    if (at(7.4f)) { a.uiShot = out("popped_out"); logf("[vh7] popped out: station %d hatch %.2f", bt->station, bt->hatchT); }
    if (t > 7.8f) { bt->station = TS_GUNNER; a.tanks.exitTank(a, true); phase = 3; t = 0; }
    return true;
  }
  if (phase == 3) {   // the Abrams' sight
    in = ExploreInput{};
    if (at(0.2f)) {
      const AssetDef* aa = a.lib.find("tank_abrams_m1a2");
      abr = aa ? a.placeInstance(aa, vec3(-20, a.terrain.height(-20, 0), 0), 0) : 0; a.syncRuntime(); a.tanks.sync(a);
      bool ok = abr && a.tanks.enter(a, abr); logf("[vh7] enter the Abrams: %s", ok ? "yes" : "NO");
      vec3 to = vec3(8, 1.5f, 150) - vec3(-20, 0, 0); a.eyeYaw = degrees(std::atan2(-to.x, -to.z)); a.eyePitch = 0.3f;
    }
    if (at(2.8f)) a.uiShot = out("abrams_sight");
    if (t > 3.2f) return done();
    return true;
  }
  return true;
}
