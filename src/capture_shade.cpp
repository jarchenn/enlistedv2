// MEM1 follow-up --capture-shade: close-ups of curved, glossy part-built assets (64-byte packed vertices: normals 10:10:10,
// albedo / emission half floats) for a banding check: selftest_out/shade_<asset>_<k>.png
#include "selftest.h"

namespace {
int phase = 0; float t = 0; fs::path pend; std::vector<uint64_t> ids;
const char* kA[] = {"mod_semi_tractor", "mod_nt_pump_station", "mod_swimming_pool"};
void look(App& a, vec3 eye, vec3 at) { vec3 d = at - eye; a.camPos = eye; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
}

bool captureShadeSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-shade") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  float t0 = t; t += dt;
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "shade check");
    for (int k = 0; k < 3; ++k) { const AssetDef* d = a.lib.find(kA[k]); ids.push_back(d ? a.placeInstance(d, vec3(k * 60.0f, a.terrain.height(k * 60.0f, 0), 0), 0) : 0); }
    a.syncRuntime(); phase = 1; t = 0; return true;
  }
  int shot = (int)(t / 1.2f);   // 3 assets x 2 views, 1.2 s each
  if (shot >= 6) { a.selftest->exitCode = 0; a.selftest->done = true; return true; }
  if ((int)(t0 / 1.2f) != shot || t0 == 0) {
    int k = shot / 2; const AssetDef* d = a.lib.find(kA[k]);
    if (d) {
      vec3 c = vec3(k * 60.0f, a.terrain.height(k * 60.0f, 0), 0) + (d->bmin + d->bmax) * 0.5f, e = d->bmax - d->bmin;
      float r = std::max({e.x, e.y, e.z}) * (shot % 2 ? 0.35f : 0.75f);   // a near and a closer view
      look(a, c + glm::normalize(vec3(0.8f, 0.45f, 1.0f)) * r, c);
    }
  }
  if (t - shot * 1.2f > 1.0f && t0 - shot * 1.2f <= 1.0f) pend = exeDir() / "selftest_out" / ("shade_" + std::string(kA[shot / 2]) + "_" + std::to_string(shot % 2) + ".png");
  return true;
}
