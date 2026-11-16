// --capture-dash: no suite, a contact sheet of driver's views (selftest_out/capture_dash.png): a military vehicle (the
// Humvee), a civilian car (an adopted part-built sedan) and the Ural-4320, each by day and at night, driven forward for
// a moment so the needles / digits read; then the six shots stitched 3 x 2 (rows: day, night).
#include "selftest.h"
#include "tank.h"
#include "settings.h"
#include <stb_image.h>
#include <stb_image_write.h>

namespace {
const char* kIds[3] = {"car_humvee", "car_ural_4320", "mod_sedan"};
uint64_t gIds[3] = {0, 0, 0}; int gShot = 0; float gT = 0; bool gSetup = false; UserSettings gKeep;
fs::path shotPath(int k) { return exeDir() / "selftest_out" / ("dash_" + std::to_string(k) + ".png"); }
}

bool captureDashSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-dash") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  if (!gSetup) {
    gSetup = true;
    a.newMap(*a.findTemplate("blank_slate"), "Dash capture");
    for (int k = 0; k < 3; ++k) {
      const AssetDef* as = a.lib.find(kIds[k]);
      if (!as) { logf("[capture-dash] FAIL: no asset %s", kIds[k]); return finish(1); }
      float x = -30.0f + k * 30.0f;
      gIds[k] = a.placeInstance(as, vec3(x, a.terrain.height(x, 0), 0), 0);
    }
    a.syncRuntime(); a.tanks.sync(a);
    if (!a.enterExplore()) { logf("[capture-dash] FAIL: Explore"); return finish(1); }
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    if (!a.tanks.get(gIds[2])) a.tanks.adoptCar(a, gIds[2]);   // the civilian car drives as an adopted car
    gKeep = settings().s;
    gT = 0; return true;
  }
  // per shot: k = vehicle, night = second row; 0..0.2 s enter, 0.2..1.6 s drive forward, 2.4 s shot
  int k = gShot % 3; bool night = gShot >= 3;
  gT += dt;
  if (gT < dt * 1.5f) {
    Settings& S = settings(); S.s.hour = night ? 23.0f : 14.0f; S.apply(a);
    a.tanks.exitTank(a, true);
    bool in = a.tanks.enter(a, gIds[k]);
    logf("[capture-dash] %s %s: entered %d", kIds[k], night ? "night" : "day", (int)in);
    a.eyePitch = -12;
    if (const char* y = std::getenv("NMK_DASH_YAW")) a.eyeYaw += (float)std::atof(y);   // look-around checks
  }
  a.scriptedInput.move = vec2(0, gT > 0.2f && gT < 1.6f ? 1.0f : 0.0f);
  if (gT > 2.4f && gT < 2.4f + dt * 1.5f) a.uiShot = shotPath(gShot);
  if (gT > 2.8f) { ++gShot; gT = 0; }
  if (gShot < 6) return true;
  // stitch: three across (vehicles), two down (day, night), each shot scaled down by 2
  std::vector<std::vector<uint8_t>> img(6); int w = 0, h = 0;
  for (int i = 0; i < 6; ++i) {
    int iw, ih, ch; stbi_uc* p = stbi_load(shotPath(i).string().c_str(), &iw, &ih, &ch, 3);
    if (!p) { logf("[capture-dash] FAIL: shot %d missing", i); return finish(1); }
    if (!w) { w = iw / 2; h = ih / 2; }
    img[i].resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
      int s = 0; for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) s += p[((size_t)std::min(ih - 1, y * 2 + dy) * iw + std::min(iw - 1, x * 2 + dx)) * 3 + c];
      img[i][((size_t)y * w + x) * 3 + c] = (uint8_t)(s / 4);
    }
    stbi_image_free(p);
  }
  std::vector<uint8_t> sheet((size_t)w * 3 * h * 2 * 3);
  for (int i = 0; i < 6; ++i) for (int y = 0; y < h; ++y)
    std::memcpy(&sheet[(((size_t)(i / 3) * h + y) * w * 3 + (size_t)(i % 3) * w) * 3], &img[i][(size_t)y * w * 3], (size_t)w * 3);
  stbi_write_png((exeDir() / "selftest_out" / "capture_dash.png").string().c_str(), w * 3, h * 2, 3, sheet.data(), w * 3 * 3);
  settings().s = gKeep; settings().apply(a);
  logf("[capture-dash] wrote selftest_out/capture_dash.png");
  return finish(0);
}
