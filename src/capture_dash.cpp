// --capture-dash: no suite, a contact sheet of driver's views (selftest_out/capture_dash.png), one per interior spec.
// Driven (Explore, the player in the driver's seat, rolled forward a moment so needles / digits read): both jeeps, two
// Opel trucks, the Humvee, the Boxer, the Ural, the nine car-pack kinds and the BR cars. Parked (Creative camera at the
// driver's eye): an abandoned BR sedan, a wrecked car-pack SUV and a wrecked van. Each by day and at night; stitched six
// across, day rows over night rows.
#include "selftest.h"
#include "tank.h"
#include "settings.h"
#include "vehicle_dash.h"
#include <stb_image.h>
#include <stb_image_write.h>

namespace {
struct V { const char* id; bool drive; };
const V kV[] = {{"car_jeep_willys", true}, {"car_jeep_ww2", true}, {"car_truck_cargo", true}, {"car_truck_canopy", true}, {"car_humvee", true},
                {"car_boxer_apc", true}, {"car_ural_4320", true}, {"mod_pcar_compact", true}, {"mod_pcar_hatchback", true}, {"mod_pcar_minivan", true},
                {"mod_pcar_offroad", true}, {"mod_pcar_pickup", true}, {"mod_pcar_sedan", true}, {"mod_pcar_sport", true}, {"mod_pcar_suv", true},
                {"mod_pcar_wagon", true}, {"mod_sedan", true}, {"mod_hatchback", true}, {"mod_suv", true}, {"mod_pickup", true},
                {"mod_delivery_van", true}, {"mod_sedan_red", false}, {"mod_pcar_suv_wreck", false}, {"mod_van_wrecked", false}};
const int N = (int)(sizeof kV / sizeof kV[0]);
std::vector<uint64_t> gIds; int gShot = 0; float gT = 0; bool gSetup = false; UserSettings gKeep;
fs::path shotPath(int k) { return exeDir() / "selftest_out" / ("dash_" + std::to_string(k) + ".png"); }
// the driver's eye of a parked part-built car (as TankSys::adoptCar seats it): behind the steering wheel, over the dash
bool parkedEye(App& a, uint64_t id, vec3& eye, float& yaw) {
  Instance* inst = a.doc.find(id); auto rt = a.rt.find(id);
  if (!inst || rt == a.rt.end() || !rt->second.asset) return false;
  const AssetDef& as = *rt->second.asset; vec3 dc(0, 1, 0.6f), ds(0, 0.3f, 0.4f); float wx = -0.37f;
  ds = vec3(0);
  for (auto& p : as.parts) { const std::string& mn = p.mat >= 0 && p.mat < (int)as.materials.size() ? as.materials[p.mat].name : std::string();
    if (mn == "dash" && p.size.x * p.size.y * p.size.z > ds.x * ds.y * ds.z) { dc = p.center; ds = p.size; } }   // the biggest, as adoptCar
  for (auto& p : as.parts) { const std::string& mn = p.mat >= 0 && p.mat < (int)as.materials.size() ? as.materials[p.mat].name : std::string();
    if (mn == "rubber" && p.shape == Part::CYL && p.size.y < 0.06f && p.size.x < 0.5f && std::fabs(p.center.y - dc.y) < 0.4f) wx = p.center.x; }
  vec3 l(wx, dc.y + ds.y * 0.5f + 0.30f, dc.z - ds.z * 0.5f - 0.62f);
  vehicleDashModelEye(&as, l);   // a car-pack copy: on its model's seat
  eye = inst->pos + yawQuat(inst->yaw) * l; yaw = inst->yaw + 180.0f; return true;
}
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
    for (int k = 0; k < N; ++k) {
      const AssetDef* as = a.lib.find(kV[k].id);
      if (!as) { logf("[capture-dash] no asset %s (skipped)", kV[k].id); gIds.push_back(0); continue; }
      float x = -140.0f + k * 12.0f;
      gIds.push_back(a.placeInstance(as, vec3(x, a.terrain.height(x, 0), 0), 0));
    }
    a.syncRuntime(); a.tanks.sync(a);
    gKeep = settings().s;
    gT = 0; return true;
  }
  // per shot: k = vehicle, night = the second half; driven: enter, 0.2..1.6 s forward, shot at 2.4 s; parked: Creative camera
  int k = gShot % N; bool night = gShot >= N;
  gT += dt;
  if (gT < dt * 1.5f) {
    Settings& S = settings(); S.s.hour = night ? 23.0f : 14.0f; S.apply(a);
    if (a.mode == Mode::Explore) a.tanks.exitTank(a, true);
    if (kV[k].drive && gIds[k]) {
      if (a.mode != Mode::Explore) { a.enterExplore(); a.useScripted = true; a.scriptedInput = ExploreInput{}; }
      if (!a.tanks.get(gIds[k])) a.tanks.adoptCar(a, gIds[k]);   // a part-built car drives adopted (BR)
      bool in = a.tanks.enter(a, gIds[k]);
      a.eyePitch = -12;
      if (const char* y = std::getenv("NMK_DASH_YAW")) a.eyeYaw += (float)std::atof(y);   // look-around checks
      logf("[capture-dash] %s %s: entered %d", kV[k].id, night ? "night" : "day", (int)in);
    } else {
      if (a.mode == Mode::Explore) a.exitExplore();
      vec3 e; float yaw;
      if (gIds[k] && parkedEye(a, gIds[k], e, yaw)) { a.camPos = e; a.camYaw = yaw; a.camPitch = -12; }
      logf("[capture-dash] %s %s: parked view", kV[k].id, night ? "night" : "day");
    }
  }
  if (a.mode == Mode::Explore) a.scriptedInput.move = vec2(0, kV[k].drive && gT > 0.2f && gT < 1.6f ? 1.0f : 0.0f);
  if (gT > 2.4f && gT < 2.4f + dt * 1.5f) a.uiShot = shotPath(gShot);
  if (gT > 2.8f) { ++gShot; gT = 0; }
  if (gShot < 2 * N) return true;
  // stitch: six across, day rows then night rows, each shot scaled down by 3
  const int C = 6, rowsPer = (N + C - 1) / C, R = rowsPer * 2;
  int w = 0, h = 0; std::vector<std::vector<uint8_t>> img(2 * N);
  for (int i = 0; i < 2 * N; ++i) {
    int iw, ih, ch; stbi_uc* p = stbi_load(shotPath(i).string().c_str(), &iw, &ih, &ch, 3);
    if (!p) { logf("[capture-dash] shot %d missing", i); continue; }
    if (!w) { w = iw / 3; h = ih / 3; }
    img[i].resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
      int s = 0; for (int dy = 0; dy < 3; ++dy) for (int dx = 0; dx < 3; ++dx) s += p[((size_t)std::min(ih - 1, y * 3 + dy) * iw + std::min(iw - 1, x * 3 + dx)) * 3 + c];
      img[i][((size_t)y * w + x) * 3 + c] = (uint8_t)(s / 9);
    }
    stbi_image_free(p);
  }
  std::vector<uint8_t> sheet((size_t)w * C * h * R * 3, 20);
  for (int i = 0; i < 2 * N; ++i) {
    if (img[i].empty()) continue;
    int j = i % N, row = (i / N) * rowsPer + j / C, col = j % C;
    for (int y = 0; y < h; ++y) std::memcpy(&sheet[(((size_t)row * h + y) * w * C + (size_t)col * w) * 3], &img[i][(size_t)y * w * 3], (size_t)w * 3);
  }
  stbi_write_png((exeDir() / "selftest_out" / "capture_dash.png").string().c_str(), w * C, h * R, 3, sheet.data(), w * C * 3);
  settings().s = gKeep; settings().apply(a);
  logf("[capture-dash] wrote selftest_out/capture_dash.png (%d vehicles x day / night)", N);
  return finish(0);
}
