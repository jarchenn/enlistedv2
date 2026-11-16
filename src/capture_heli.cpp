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
    vec3 fwd = yawQuat(r->yaw) * vec3(0, 0, 1), rgt = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
    float vf = glm::dot(r->vel, fwd), vs = glm::dot(r->vel, rgt), agl = r->pos.y - a.terrain.height(r->pos.x, r->pos.z);
    static float stopAt = -1, maxSusp = 0, impactV = 0;
    in.jumpHeld = (s > 4.5f && s < 8.0f) || (s > 33.0f && s < 36.0f);   // climb once spun up; again before the shoot-down
    in.move.y = s > 8.5f && s < 14.5f ? 1.0f : s > 17.5f && s < 23.0f ? -1.0f : 0.0f;   // forward, release, back (Shift: fly)
    in.sprint = s > 8.5f && s < 23.0f;
    // VH5d: hover, W alone tilts the nose down and holds it (no thrust), firing at the ground ahead
    if (s > 36.6f && s < 38.2f) in.move.y = 1.0f;
    static vec3 aimP0;
    if (at(36.6f)) aimP0 = r->pos;
    in.move.x = s > 23.0f && s < 26.0f ? 1.0f : 0.0f;                                     // bank right
    in.crouch = s > 27.0f && s < 33.0f && !r->grounded;                                   // descend to land
    in.fire = (s > 10.0f && s < 11.5f) || (s > 38.6f && s < 40.0f);                                                    // chin gun burst
    in.reload = (s > 11.6f && s < 11.7f) || (s > 12.0f && s < 12.1f);                    // two rockets
    if (s > 17.5f && s < 23.0f && stopAt < 0 && vf <= 0) stopAt = s - 17.5f;
    if (s > 27.0f && s < 33.0f) { maxSusp = std::max(maxSusp, r->susp); if (r->grounded && impactV == 0) impactV = r->suspV; }
    if (at(4.7f)) logf("[heli] rotor %.2f grounded %d", r->rotor, (int)r->grounded);
    if (at(8.0f)) logf("[heli] climbing: agl %.1f m, vy %.1f", agl, r->vel.y);
    if (at(8.5f)) logf("[heli] 0.5 s after releasing Space: vy %.2f (eases to a hold)", r->vel.y);
    if (at(9.5f)) logf("[heli] 1.5 s after: vy %.2f, agl %.1f", r->vel.y, agl);
    if (at(9.0f)) logf("[heli] W 0.5 s: pitch %.1f deg, forward %.1f m/s (grounded %d, ground-pos %.2f, rotor %.2f, move %.1f)", r->pitch, vf, (int)r->grounded, a.tanks.heliGround(a, *r, r->pos) - r->pos.y, r->rotor, in.move.y);
    if (at(11.7f)) a.uiShot = shotPath("flying_firing");
    if (at(14.4f)) logf("[heli] W 6 s: pitch %.1f, %.0f km/h; 30 mm fired %d (heat %.2f), rockets fired %d (left %d)", r->pitch, vf * 3.6f, r->gunShots, r->gunHeat, r->rocketsFired, r->rockets);
    if (at(17.4f)) logf("[heli] 3 s after release: %.0f km/h (momentum kept), pitch %.1f (assist levels against the drift)", vf * 3.6f, r->pitch);
    if (at(22.9f)) logf("[heli] S: forward motion stopped after %.1f s, now %.1f m/s (reversing), pitch %.1f", stopAt, vf, r->pitch);
    if (at(25.9f)) logf("[heli] D 3 s: bank %.1f deg, sideways %.1f m/s", r->roll, vs);
    if (at(32.9f)) logf("[heli] landed %d, gear compression max %.3f m, susp rate at touchdown %.2f", (int)r->grounded, maxSusp, impactV);
    if (at(38.4f)) logf("[heli] VH5d W alone 1.6 s: nose %.1f deg (held %.1f), thrust tilt %.1f, view pitch %.1f", r->pitch, r->aimPitch, r->thrPitch, a.eyePitch);
    if (at(39.6f)) a.uiShot = shotPath("nose_down_firing");
    if (at(40.2f)) logf("[heli] VH5d after 3.6 s nose-down: drift %.2f m, nose %.1f deg (stays), 30 mm fired %d", glm::length(vec2(r->pos.x - aimP0.x, r->pos.z - aimP0.z)), r->pitch, r->gunShots);
    if (s > 40.5f) { a.tanks.destroy(a, *r, "capture: shot down"); gPhase = 3; gT = 0; }
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
