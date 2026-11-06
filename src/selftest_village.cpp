// --build-village-map: the representative Normandy village area (large plaster house, stone cottages, orchard,
// poplar windbreak, defences, guns, crates, lane) saved to maps/village.nmap.json (shipped as
// content/examples/village.nmap.json).
// --benchmark-village <tag>: loads the shipped village and renders five fixed cameras (Creative camera,
// FOV 60, 1920x1080, grass Medium, overlays hidden) for 4 s each: frame time median / p95, GPU timer,
// render CPU, draws, triangles, working set; a screenshot per camera -> selftest_out/village_<tag>_<cam>.png.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <windows.h>
#include <psapi.h>
#include <algorithm>

namespace {
struct Cam { const char* name; vec3 pos; float yaw, pitch; };
// camera 0 reproduces the composition of references/current_game.png
const Cam kCams[] = {
  {"overview", vec3(-6, 2.2f, 38), 8, -2},
  {"road", vec3(4, 1.7f, 10), 0, -4},
  {"area", vec3(22, 6, 26), 40, -12},
  {"foliage", vec3(16, 2.0f, 6), -70, 6},
  {"building", vec3(-24, 2.0f, 4), 70, 6},
};
std::string vTag;
std::vector<double> vFrames, vGpu, vCpu;
size_t vCam = 0; double vLast = 0;
}

void SelfTest::villageUpdate(App& app, float dt) {
  (void)dt;
  if (stage == 1300) {
    reportName = "village_build";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Normandy village");
    auto put = [&](const char* id, float x, float z, float yaw) { const AssetDef* a = app.lib.find(id); uint64_t i = a ? app.placeInstance(a, vec3(x, 0, z), yaw) : 0; return i; };
    int n = 0;
    n += put("large_municipal_building", -24, -8, 0) != 0;      // the tall plaster house on the left
    n += put("small_storehouse", 10, -14, 0) != 0;               // stone outbuilding, centre right
    n += put("house_stone_cottage", -2, -46, 0) != 0;            // further cottage behind
    n += put("house_farmhouse_2story", 34, -40, 200) != 0;
    n += put("small_stone_dairy", -44, -40, 90) != 0;
    // orchard between the houses (irregular, not a grid)
    float orch[][2] = {{-10, 2}, {-4, -6}, {2, 3}, {-7, -16}, {0, -24}, {5, -9}, {-13, -26}, {7, -30}, {-2, 12}};
    for (auto& o : orch) n += put("tree_apple", o[0], o[1], o[0] * 37) != 0;
    // poplar windbreak along the right-hand lane
    for (int k = 0; k < 6; ++k) n += put("tree_poplar", 26 + k * 1.8f, 8 - k * 7.5f, k * 50.0f) != 0;
    n += put("tree_oak", 40, 14, 30) != 0;
    n += put("tree_oak", -40, 10, 120) != 0;
    // defences on the right flank, guns and a crate
    for (int k = 0; k < 3; ++k) n += put("def_tank_trap", 30 + k * 3.2f, 14 + k * 0.5f, k * 40.0f) != 0;
    n += put("def_sandbag_straight", 20, 18, 0) != 0;
    n += put("gun_m2hb_tripod", 16, 16, 180) != 0;
    n += put("gun_pak40", 8, -34, 180) != 0;
    n += put("crate_mixed_supply", 13, 17, 0) != 0;
    n += put("rack_allied", -5, 24, 180) != 0;      // Allied resupply point beside the spawn
    n += put("crate_mixed_supply", -9, 25, 0) != 0;
    n += put("rack_axis", 15, -30, 0) != 0;          // Axis rack by the PaK position
    n += put("crate_pak40_he", 3, -31, 0) != 0;
    app.doc.start = {true, vec3(0, app.terrain.height(0, 30), 30), 180};
    check(n >= 25, "village: " + std::to_string(n) + " instances placed");
    fs::path p = userMapsDir() / "village.nmap.json";
    check(app.saveMapTo(p), "saved " + p.string());
    finish(app);
    return;
  }
  if (stage == 1350) {
    reportName = "village_bench_" + vTag;
    bench = true;
    fs::path p = contentDir() / "examples" / "village.nmap.json";
    check(app.openMap(p), "opened the shipped village map");
    app.hideEditorOverlays = true; app.showBrowser = false; app.showStats = false;
    vCam = 0; vFrames.clear(); vGpu.clear(); vCpu.clear();
    stage = 1351; stageTime = 0;
    return;
  }
  if (stage == 1351) {
    const Cam& c = kCams[vCam];
    app.camPos = c.pos; app.camPos.y += app.terrain.height(c.pos.x, c.pos.z); app.camYaw = c.yaw; app.camPitch = c.pitch;
    double now = glfwGetTime();
    if (stageTime > 1.0) { vFrames.push_back((now - vLast) * 1000.0); vGpu.push_back(app.R.stats.gpuMs); vCpu.push_back(app.R.stats.cpuMs); }   // 1 s warm-up (grass streaming)
    vLast = now;
    if (stageTime > 2.4 && stageTime < 2.42) shotQueue.push_back("village_" + vTag + "_" + c.name);
    if (stageTime < 5.0) return;
    auto pct = [](std::vector<double> v, double q) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(v.size() * q))]; };
    PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    benchOut[c.name] = {{"frames", vFrames.size()}, {"frame_p50_ms", pct(vFrames, 0.5)}, {"frame_p95_ms", pct(vFrames, 0.95)},
                        {"gpu_p50_ms", pct(vGpu, 0.5)}, {"gpu_p95_ms", pct(vGpu, 0.95)}, {"render_cpu_p50_ms", pct(vCpu, 0.5)},
                        {"draws", app.R.stats.draws}, {"tris", app.R.stats.tris}, {"working_set_mb", pmc.WorkingSetSize / 1048576.0},
                        {"camera", {{"pos", tojson(app.camPos)}, {"yaw", c.yaw}, {"pitch", c.pitch}, {"fov", 60}}}};
    vFrames.clear(); vGpu.clear(); vCpu.clear();
    if (++vCam >= sizeof kCams / sizeof kCams[0]) {
      benchOut["resolution"] = {app.R.width, app.R.height};
      benchOut["gl_renderer"] = (const char*)glGetString(GL_RENDERER);
      benchOut["grass"] = grassPreset(app.grass.quality).name;
      benchOut["vsync"] = false;
      benchOut["method"] = "wall-clock frame interval (glfwGetTime) and GL_TIME_ELAPSED query per frame; 1 s warm-up then ~4 s per camera";
      finish(app);
      return;
    }
    stageTime = 0;
  }
}

void setVillageTag(const std::string& t) { vTag = t; }
