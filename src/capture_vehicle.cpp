// --capture-vehicle <asset id>: no suite, one look at a placed vehicle (selftest_out/capture_<asset>.png): a blank map,
// the vehicle on the ground in Creative, a tank's turret turned 60 deg and its gun up 5 deg, the camera ahead-left of it.
#include "selftest.h"

namespace {
std::string gAsset; float gT = 0; uint64_t gId = 0; bool gShot = false;
}

bool captureVehicleSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--capture-vehicle") { mode = 1; gAsset = __argv[k + 1]; } }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  if (gT == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "Vehicle capture");
    const AssetDef* as = a.lib.find(gAsset);
    if (!as) { logf("[capture] FAIL: no asset %s", gAsset.c_str()); return finish(1); }
    gId = a.placeInstance(as, vec3(0, a.terrain.height(0, 0), 0), 0);
    a.syncRuntime(); a.tanks.sync(a);
    float R = std::max(as->bmax.x - as->bmin.x, as->bmax.z - as->bmin.z);
    a.camPos = vec3(-0.8f * R, a.terrain.height(0, 0) + 0.45f * R, 1.0f * R); a.camYaw = -40; a.camPitch = -14;
  }
  gT += dt;
  if (TankRT* r = a.tanks.get(gId)) { r->tYaw = 60; r->gElev = 5; }
  if (gT > 0.8f && !gShot) {
    gShot = true; a.uiShot = exeDir() / "selftest_out" / ("capture_" + gAsset + ".png");
    TankRT* r = a.tanks.get(gId);
    logf("[capture] %s: vehicle %s, %zu model parts, hp %d", gAsset.c_str(), r && r->def ? r->def->id.c_str() : "NONE", r && r->def && r->def->model ? r->def->model->parts.size() : 0, r ? r->hp : -1);
  }
  if (gT > 1.2f) return finish(0);
  return true;
}
