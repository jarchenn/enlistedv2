// VH5 --capture-heli: no suite, a look at the helicopter and a short scripted flight (selftest_out/capture_heli_*.png,
// numbers in the log): parked in Creative with the rotor drawn; then in Explore: enter, spin up, climb (Space), fly
// forward (W) firing the chin gun (LMB) and two rockets (R), hover, a chase-camera frame.
#include "selftest.h"

namespace {
uint64_t gId = 0; float gT = 0; int gPhase = 0;
fs::path shotPath(const char* tag) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / (std::string("capture_heli_") + tag + ".png"); }
}

bool captureHeliSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-heli") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  TankRT* r = gId ? a.tanks.get(gId) : nullptr;
  float t0 = gT; gT += dt;
  auto at = [&](float m) { return t0 < m && gT >= m; };
  if (gPhase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "Helicopter capture");
    const AssetDef* as = a.lib.find("heli_ah64d_apache");
    if (!as) { logf("[heli] FAIL: no asset heli_ah64d_apache"); return finish(1); }
    gId = a.placeInstance(as, vec3(0, a.terrain.height(0, 0), 0), 0);
    a.syncRuntime(); a.tanks.sync(a);
    a.camPos = vec3(-16, a.terrain.height(0, 0) + 7, 18); a.camYaw = -40; a.camPitch = -14;
    gPhase = 1; gT = 0; return true;
  }
  if (!r) { logf("[heli] FAIL: no vehicle state"); return finish(1); }
  if (gPhase == 1) {   // parked, rotor blades drawn at an angle
    r->rotorAng = 25; r->tailAng = 40;
    if (at(0.8f)) a.uiShot = shotPath("parked");
    if (gT > 1.1f) { a.enterExplore(); a.useScripted = true; gPhase = 2; gT = 0; }
    return true;
  }
  if (gPhase == 2) {
    if (gT < 0.2f) return true;
    if (!a.tanks.occupied) { bool ok = a.tanks.enter(a, gId); logf("[heli] enter: %s", ok ? "yes" : "NO"); if (!ok) return finish(1); }
    ExploreInput& in = a.scriptedInput; in = ExploreInput();
    float s = gT - 0.2f;
    in.jumpHeld = s > 4.5f && s < 8.0f;                 // climb once spun up
    in.move.y = s > 8.0f && s < 12.0f ? 1.0f : 0.0f;    // forward
    in.fire = s > 9.0f && s < 10.5f;                    // chin gun burst
    in.reload = (s > 10.6f && s < 10.7f) || (s > 11.0f && s < 11.1f);   // two rockets
    if (at(4.7f)) logf("[heli] rotor %.2f grounded %d alt %.1f", r->rotor, (int)r->grounded, r->pos.y);
    if (at(8.2f)) logf("[heli] after climbing 3.5 s: height above ground %.1f m, vy %.1f", r->pos.y - a.terrain.height(r->pos.x, r->pos.z), r->vel.y);
    if (at(10.7f)) a.uiShot = shotPath("flying_firing");
    if (at(11.6f)) logf("[heli] forward: %.0f km/h, 30 mm fired %d (ammo %d, heat %.2f), rockets fired %d (left %d)", r->speed * 3.6f, r->gunShots, r->gunAmmo, r->gunHeat, r->rocketsFired, r->rockets);
    if (at(14.5f)) logf("[heli] hover after release: %.1f km/h, vy %.2f", r->speed * 3.6f, r->vel.y);
    if (s > 15.0f) { a.tanks.destroy(a, *r, "capture: shot down"); gPhase = 3; gT = 0; }
    return true;
  }
  if (gPhase == 3) {   // the fall and the crash
    if (at(1.0f)) logf("[heli] falling %d, yaw rate %.0f, height %.1f", (int)r->falling, r->yawRate, r->pos.y - a.terrain.height(r->pos.x, r->pos.z));
    if (!r->falling && gT > 0.3f && gT < 20.0f) { logf("[heli] crashed after %.1f s, wrecked %d, occupied %d", gT, (int)r->wrecked, (int)(a.tanks.occupied != 0)); gT = 20.0f; }
    if (gT > 22.0f) { a.camPos = r->pos + vec3(-14, 8, 16); a.camYaw = -40; a.camPitch = -18; a.uiShot = shotPath("wreck"); gPhase = 4; gT = 0; }
    return true;
  }
  if (gT > 0.6f) { if (a.mode == Mode::Explore) a.exitExplore(); return finish(0); }
  return true;
}
