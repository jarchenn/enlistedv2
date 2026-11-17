// VH8 --capture-vh8: each tank's roof MG firing (crew at the hatch, gun swung 35 deg right), seen from outside beside the
// turret, so the tracer / flash origin shows against the MG and the main gun. selftest_out/vh8_mg_<tank>.png; [vh8cap]
// lines give the muzzle's distance from the MG pivot and from the main gun's muzzle.
#include "selftest.h"

bool gVh8CamOn = false; vec3 gVh8CamEye{0}, gVh8CamAt{0};   // read by App's view setup (app.cpp): an outside camera

namespace {
int idx = -1; float t = 0; fs::path pend; uint64_t cur = 0; std::string only;   // VH9: "--capture-vh8 <tank>" shoots that one only
const char* kTanks[] = {"sherman_m4", "sherman_firefly", "tiger_i", "panzer_iv", "abrams_m1a2"};
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("vh8_mg_" + n + ".png"); }
}

bool captureVh8SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-vh8") { mode = 1; if (k + 1 < __argc && __argv[k + 1][0] != '-') only = __argv[k + 1]; } }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  float t0 = t; t += dt;
  auto at = [&](float m) { return t0 < m && t >= m; };
  ExploreInput& in = a.scriptedInput;
  if (idx < 0) {
    a.newMap(*a.findTemplate("blank_slate"), "VH8 capture");
    a.syncRuntime(); a.enterExplore(); a.useScripted = true;
    idx = 0; t = 0; return true;
  }
  if (idx >= (int)(sizeof kTanks / sizeof *kTanks)) { gVh8CamOn = false; if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; return true; }
  const std::string id = kTanks[idx];
  if (!only.empty() && id != only) { ++idx; t = 0; return true; }
  if (at(0.05f)) {
    float x = idx * 20.0f;
    cur = a.placeInstance(a.lib.find("tank_" + id), vec3(x, a.terrain.height(x, 0), 0), 0); a.syncRuntime(); a.tanks.sync(a);
    if (!a.tanks.get(cur) || !a.tanks.enter(a, cur)) { logf("[vh8cap] %s: could not place / enter", id.c_str()); ++idx; t = 0; return true; }
  }
  TankRT* tt = a.tanks.get(cur);
  if (!tt) return true;
  in = ExploreInput{};
  if (t > 0.3f && t < 0.4f) in.crouch = true;   // Ctrl: up into the hatch
  if (t > 2.0f) {   // swing the MG 35 deg right of the hull, slightly up
    vec3 d = yawQuat(tt->yaw) * glm::normalize(vec3(-0.57f, 0.06f, 0.82f));
    a.eyeYaw = degrees(std::atan2(-d.x, -d.z)); a.eyePitch = degrees(std::asin(d.y));
  }
  if (t > 3.0f && t < 3.9f) in.fire = true;
  if (at(2.5f)) {   // depression sweep: per 15 deg sector, the lowest elevation whose line of fire clears the own hull / turret
    float y0 = tt->mgYaw, e0 = tt->mgElev; std::string s; int clear = 0; float front = -99;
    for (float y = -180; y < 180; y += 15) {
      float lo = 99;
      for (float e = tt->def->mgElevMin; e <= 10; e += 1) {
        tt->mgYaw = std::clamp(y, tt->def->mgYawMin, tt->def->mgYawMax); tt->mgElev = e;
        vec3 dir; vec3 m = a.tanks.mgMuzzleWorld(*tt, dir); Physics::Hit sh;
        if (!Physics::castRayOnly(m, dir, 9.0f, tt->id << 16, sh)) { lo = e; break; }
      }
      if (lo <= tt->def->mgElevMin + 0.5f) ++clear;
      if (std::fabs(y) <= 90) front = std::max(front, lo);
      char b[24]; std::snprintf(b, sizeof b, " %+.0f:%s", y, lo > 50 ? "x" : std::to_string((int)lo).c_str()); s += b;
    }
    tt->mgYaw = y0; tt->mgElev = e0;
    logf("[vh8cap] %s depression: limit %.0f / +%.0f deg, reached in %d of 24 sectors; lowest clear elevation by yaw (0 = ahead):%s", id.c_str(), tt->def->mgElevMin, tt->def->mgElevMax, clear, s.c_str());
    logf("[vh8cap] %s %s: the frontal 180 deg reach at least %.0f deg", id.c_str(), front <= -15 ? "PASS" : "below -15 FAIL", front);
  }
  if (at(2.9f)) {   // outside, low on the left rear quarter, looking past the turret roof along the MG's line
    mat4 H = a.tanks.hullMatrix(*tt);
    gVh8CamEye = vec3(H * vec4(4.2f, 3.6f, -3.0f, 1)); gVh8CamAt = vec3(H * vec4(-0.4f, 2.6f, 1.6f, 1)); gVh8CamOn = true;
  }
  if (at(3.6f)) {
    vec3 dir; vec3 m = a.tanks.mgMuzzleWorld(*tt, dir);
    mat4 h, tu, g, rc; a.tanks.rig(*tt, h, tu, g, rc);
    vec3 piv = vec3(tu * vec4(tt->def->mgPivot, 1)), gm = vec3(g * vec4(tt->def->muzzle, 1));
    logf("[vh8cap] %s: station %d, mg yaw %.1f elev %.1f, rounds fired %d, muzzle %.2f m from the MG pivot, %.2f m from the main gun muzzle, height %.2f over the hull origin",
         id.c_str(), tt->station, tt->mgYaw, tt->mgElev, tt->mgShots, glm::length(m - piv), glm::length(m - gm), m.y - tt->pos.y);
    pend = out(id);
  }
  if (at(4.1f)) gVh8CamOn = false;   // the popped-out gunner's own view
  if (at(4.35f)) pend = out(id + "_hatch");
  if (t > 4.6f) { gVh8CamOn = false; a.tanks.exitTank(a, true); ++idx; t = 0; }
  return true;
}
