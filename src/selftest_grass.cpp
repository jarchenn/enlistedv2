// --selftest-grass: deterministic placement, exclusion masks, rebuild on crater / reset / undo / apply.
// --benchmark-stress: dense-grass low flyover (Off vs High on one path) and repeated HE impacts with smoke.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <algorithm>
#include <windows.h>
#include <psapi.h>

namespace {
struct G {
  uint64_t FH = 0, BK = 0;
  vec3 cam{0}, P{0};
  uint64_t H0 = 0, Hcr = 0, chunkP0 = 0, chunkFar0 = 0;
  int pcx = 0, pcz = 0, fcx = 0, fcz = 0;
  size_t nearP0 = 0;
  // stress
  std::vector<double> ft; double lastT = 0;
  int impacts = 0; double impactMax = 0, impactSum = 0, grassPeak = 0;
  size_t bladesSum = 0; int bladeFrames = 0;
  int pass = 0;
};
G g;

double ms(std::chrono::high_resolution_clock::time_point a) {
  return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - a).count();
}
size_t countNear(const Grass& gr, vec3 p, float r) {
  size_t n = 0;
  gr.forEachInstance([&](const GrassInstance& i) { if (glm::length(vec2(i.posYaw.x - p.x, i.posYaw.z - p.z)) < r) ++n; });
  return n;
}
json stats(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  double sum = 0; for (double x : v) sum += x;
  auto at = [&](double q) { return v.empty() ? 0.0 : v[std::min(v.size() - 1, (size_t)(v.size() * q))]; };
  return {{"frames", v.size()}, {"avg_ms", sum / std::max<size_t>(1, v.size())}, {"p50_ms", at(0.5)}, {"p95_ms", at(0.95)}, {"p99_ms", at(0.99)}, {"max_ms", v.empty() ? 0 : v.back()}};
}
double workingSetMB(bool peak) {
  PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
  return (peak ? pmc.PeakWorkingSetSize : pmc.WorkingSetSize) / 1048576.0;
}
}

void SelfTest::grassUpdate(App& app, float dt) {
  auto go = [&](int s) { stage = s; stageTime = 0; };
  auto syncGrass = [&]() { app.grassBudgetMs = -1; app.updateGrass(g.cam); };
  switch (stage) {
  // ---------------------------------------------------------------- determinism and exclusion
  case 900: {
    reportName = "grass";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Grass test");
    g.FH = app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(0, 0, 0), 33.0f);
    g.BK = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(30, 0, 4), 0.0f);
    app.placeInstance(app.lib.find("gun_pak40"), vec3(14, 0, 18), 180.0f);
    app.syncRuntime();
    app.grass.quality = GrassQuality::Medium;
    g.cam = vec3(12, 1.7f, 16);
    app.camPos = g.cam; app.camYaw = 200; app.camPitch = -20;
    app.hideEditorOverlays = true;
    app.grass.clear();   // drop chunks streamed around the template camera before the test
    auto t0 = std::chrono::high_resolution_clock::now();
    syncGrass();
    double buildMs = ms(t0);
    size_t n = app.grass.instanceCount();
    check(n > 10000, "Medium preset places grass around the camera (" + std::to_string(n) + " tufts, " + std::to_string(app.grass.chunkCount()) + " chunks)");
    summary["medium_full_build_ms"] = buildMs; summary["medium_tufts"] = n; summary["medium_chunks"] = app.grass.chunkCount();
    summary["medium_bytes"] = app.grass.gpuBytes();
    // exclusion: nothing inside any placed structure's bounds + margin (rotated farmhouse included)
    auto fps = app.grassFootprints();
    check(fps.size() == 2, "the field gun is not an exclusion footprint; the two buildings are");
    size_t inside = 0, ring = 0;
    app.grass.forEachInstance([&](const GrassInstance& i) {
      for (auto& f : fps) {
        float d = Grass::footprintDistance(f, i.posYaw.x, i.posYaw.z);
        if (d < Grass::kMargin - 0.01f) ++inside;
        if (d > 1.5f && d < 3.0f) ++ring;
      }
    });
    check(inside == 0, "no grass inside footprints, steps or bunker berm bounds (rotated 33 degrees): " + std::to_string(inside) + " violations");
    check(ring > 100, "grass resumes within 3 m of the buildings (" + std::to_string(ring) + " tufts in the 1.5-3 m band)");
    g.H0 = app.grass.layoutHash();
    app.grass.clear(); syncGrass();
    check(app.grass.layoutHash() == g.H0, "clearing and rebuilding reproduces bit-identical grass");
    // preset invariance: chunk contents never depend on the preset
    g.pcx = (int)std::floor(g.cam.x / Grass::kChunk); g.pcz = (int)std::floor(g.cam.z / Grass::kChunk);
    uint64_t hc = app.grass.chunkHash(g.pcx, g.pcz);
    app.grass.quality = GrassQuality::High; syncGrass();
    t0 = std::chrono::high_resolution_clock::now();
    summary["high_tufts"] = app.grass.instanceCount(); summary["high_chunks"] = app.grass.chunkCount(); summary["high_bytes"] = app.grass.gpuBytes();
    bool same = app.grass.chunkHash(g.pcx, g.pcz) == hc;
    app.grass.quality = GrassQuality::Low; syncGrass();
    same = same && app.grass.chunkHash(g.pcx, g.pcz) == hc;
    check(same, "switching Low / Medium / High never re-places grass (preset only changes the drawn prefix)");
    app.grass.quality = GrassQuality::Off; syncGrass();
    check(app.grass.chunkCount() == 0 && app.grass.instanceCount() == 0, "Off frees every grass chunk");
    app.grass.quality = GrassQuality::Medium;
    t0 = std::chrono::high_resolution_clock::now(); syncGrass(); summary["medium_rebuild_after_off_ms"] = ms(t0);
    check(app.grass.layoutHash() == g.H0, "Off -> Medium restores the identical layout");
    // crater site in open pasture; a far chunk for the 'untouched' check
    g.P = vec3(16, 0, 24); g.P.y = app.terrain.height(g.P.x, g.P.z);
    g.pcx = (int)std::floor(g.P.x / Grass::kChunk); g.pcz = (int)std::floor(g.P.z / Grass::kChunk);
    g.fcx = g.pcx - 3; g.fcz = g.pcz;
    g.chunkP0 = app.grass.chunkHash(g.pcx, g.pcz); g.chunkFar0 = app.grass.chunkHash(g.fcx, g.fcz);
    g.nearP0 = countNear(app.grass, g.P, 1.0f);
    check(g.nearP0 > 0 && g.chunkFar0 != 0, "pasture at the crater site is grassed before the impact");
    // view of the crater site for the before/after pair
    g.cam = vec3(g.P.x + 3.5f, g.P.y + 1.8f, g.P.z + 4.5f);
    app.camPos = g.cam; app.camYaw = degrees(std::atan2(3.5f, 4.5f)); app.camPitch = -24;
    syncGrass();
    app.grass.clear(); syncGrass();
    g.H0 = app.grass.layoutHash();
    go(901);
    break;
  }
  case 901:
    if (stageTime > 0.1f && g.pass == 0) { shotQueue.push_back("grass_crater_before"); g.pass = 1; }
    if (stageTime > 0.2f) {
      auto t0 = std::chrono::high_resolution_clock::now();
      bool c = app.deformGround(g.P, 1.0f);
      double craterMs = ms(t0);
      t0 = std::chrono::high_resolution_clock::now();
      syncGrass();
      summary["crater_ground_ms"] = craterMs; summary["crater_grass_rebuild_ms"] = ms(t0);
      summary["crater_grass_chunks_rebuilt"] = app.grass.lastBuilt;
      check(c, "HE crater deforms the pasture");
      check(app.grass.chunkHash(g.pcx, g.pcz) != g.chunkP0, "the chunk under the crater is rebuilt");
      check(app.grass.chunkHash(g.fcx, g.fcz) == g.chunkFar0, "a chunk 36 m away is untouched");
      check(app.grass.lastBuilt <= 4, "only chunks overlapping the changed deformation tiles rebuild (" + std::to_string(app.grass.lastBuilt) + ")");
      size_t n = countNear(app.grass, g.P, 1.0f);
      check(n == 0, "disturbed soil within 1 m of the impact has no grass (" + std::to_string(g.nearP0) + " -> " + std::to_string(n) + ")");
      size_t dirtBad = 0;
      app.grass.forEachInstance([&](const GrassInstance& i) { if (app.terrain.dirtAt(i.posYaw.x, i.posYaw.z) > 0.18f) ++dirtBad; });
      check(dirtBad == 0, "no tuft stands on disturbed soil anywhere");
      g.Hcr = app.grass.layoutHash();
      g.pass = 0;
      go(902);
    }
    break;
  case 902:
    if (stageTime > 0.1f && g.pass == 0) { shotQueue.push_back("grass_crater_after"); g.pass = 1; }
    if (stageTime > 0.2f) {
      // session reset (F5 return) restores the authored ground and its grass exactly
      app.resetGround(); syncGrass();
      check(app.grass.layoutHash() == g.H0, "Explore reset restores identical grass");
      // footprint edit + undo
      app.pushUndo();
      app.doc.find(g.BK)->pos.x += 1.0f; app.doc.find(g.BK)->rev++;
      app.syncRuntime(); syncGrass();
      bool moved = app.grass.layoutHash() != g.H0;
      app.undo(); app.syncRuntime(); syncGrass();
      check(moved && app.grass.layoutHash() == g.H0, "moving a building re-masks grass and undo restores the identical layout");
      // explicit Apply damage persists the crater, and its grass, through a reset
      app.deformGround(g.P, 1.0f); syncGrass();
      check(app.grass.layoutHash() == g.Hcr, "the same impact on the same ground reproduces the same cratered grass");
      app.applyDamageToMap();
      app.resetGround(); syncGrass();
      uint64_t Ha = app.grass.layoutHash();
      size_t bare = countNear(app.grass, g.P, 1.0f);
      app.resetGround(); syncGrass();
      check(Ha == app.grass.layoutHash() && Ha != g.H0 && bare == 0,
            "after Apply damage to map, every reset restores the same cratered grass (crater stays bare)");
      app.undo(); app.resetGround(); syncGrass();
      check(app.grass.layoutHash() == g.H0, "undoing Apply damage restores the original grass");
      app.hideEditorOverlays = false;
      finish(app);
    }
    break;

  // ---------------------------------------------------------------- stress benchmarks
  case 950: {
    reportName = "stress";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Stress");
    for (int k = 0; k < 6; ++k) {
      const char* ids[] = {"house_farmhouse_2story", "large_stone_warehouse", "bunker_mg_casemate", "small_stone_dairy", "house_stone_cottage", "large_village_inn"};
      app.placeInstance(app.lib.find(ids[k]), vec3(-60 + k * 24.0f, 0, (k % 2) ? 22.0f : -22.0f), k * 41.0f);
    }
    app.syncRuntime();
    app.grassBudgetMs = 2.5;
    app.grass.clear(); app.grass.peakBuildMs = 0;
    app.grass.quality = GrassQuality::Off;
    g.ft.clear(); g.pass = 0; g.lastT = glfwGetTime();
    benchOut["structures"] = app.doc.instances.size();
    go(951);
    break;
  }
  case 951: {   // low flyover along the settlement street: pass 0 grass Off, pass 1 High, pass 2 Medium
    double nowT = glfwGetTime();
    if (stageTime > 0.25f) g.ft.push_back((nowT - g.lastT) * 1000.0);
    g.lastT = nowT;
    float u = std::min(1.0f, (float)stageTime / 10.0f);
    float x = -110 + 220 * u;
    app.camPos = vec3(x, app.terrain.height(x, 2) + 2.2f + 1.2f * std::sin(u * 9.0f), 2 + 6 * std::sin(u * 5.0f));
    app.camYaw = -90 + 25 * std::sin(u * 4.0f); app.camPitch = -8;
    if (app.grass.quality != GrassQuality::Off) { g.bladesSum += app.grass.lastDrawnBlades; ++g.bladeFrames; }
    if (u >= 1) {
      const char* names[] = {"grass_flyover_off", "grass_flyover_high", "grass_flyover_medium"};
      json s = stats(g.ft);
      s["working_set_mb"] = workingSetMB(false);
      s["gpu_ms_last"] = app.R.stats.gpuMs;
      if (g.pass > 0) {
        s["grass_peak_build_ms"] = app.grass.peakBuildMs; s["grass_tufts_resident"] = app.grass.instanceCount();
        s["grass_bytes"] = app.grass.gpuBytes(); s["blades_drawn_avg"] = g.bladeFrames ? (double)g.bladesSum / g.bladeFrames : 0.0;
      }
      benchOut[names[g.pass]] = s;
      if (g.pass == 1) shotQueue.push_back("bench_grass_high");
      g.ft.clear(); g.bladesSum = 0; g.bladeFrames = 0; app.grass.peakBuildMs = 0;
      ++g.pass;
      if (g.pass == 1) { app.grass.quality = GrassQuality::High; stageTime = 0; }
      else if (g.pass == 2) { app.grass.quality = GrassQuality::Medium; app.grass.clear(); stageTime = 0; }
      else {
        // repeated impacts with smoke: Explore, standing in the pasture, HE every 0.3 s across a 30 m arc
        app.doc.start = {true, vec3(0, app.terrain.height(0, 60), 60), 180};
        app.enterExplore();
        app.eyeYaw = 0; app.eyePitch = -6;
        g.impacts = 0; g.impactMax = 0; g.impactSum = 0; g.ft.clear(); g.lastT = glfwGetTime();
        app.grass.peakBuildMs = 0;
        go(952);
      }
    }
    break;
  }
  case 952: {
    double nowT = glfwGetTime();
    if (stageTime > 0.25f) g.ft.push_back((nowT - g.lastT) * 1000.0);
    g.lastT = nowT;
    app.eyeYaw = 0; app.eyePitch = -6;
    int due = (int)(stageTime / 0.3);
    while (g.impacts < due && g.impacts < 40) {
      float a = -0.6f + 1.2f * ((g.impacts * 7) % 40) / 40.0f;
      vec3 p(std::sin(a) * 28.0f, 0, 60 - std::cos(a) * 28.0f);
      p.y = app.terrain.height(p.x, p.z);
      auto t0 = std::chrono::high_resolution_clock::now();
      app.explode(p, vec3(0, -1, 0), 120.0f, 2.2f, 1.0f);
      double m = ms(t0);
      app.fx.explosion(p, vec3(0, 1, 0), "ground", true);
      g.impactMax = std::max(g.impactMax, m); g.impactSum += m;
      ++g.impacts;
    }
    if (g.impacts == 20 && g.pass == 3) { shotQueue.push_back("bench_impacts"); g.pass = 4; }
    if (stageTime > 40 * 0.3 + 4.0) {
      json s = stats(g.ft);
      s["impacts"] = g.impacts;
      s["impact_resolve_avg_ms"] = g.impactSum / std::max(1, g.impacts);
      s["impact_resolve_max_ms"] = g.impactMax;      // crater + collision patch + wall rebuild, on the frame
      s["grass_peak_rebuild_ms"] = app.grass.peakBuildMs;
      s["deformation_tiles"] = app.terrain.tiles.size();
      s["working_set_mb"] = workingSetMB(false);
      benchOut["repeated_impacts"] = s;
      benchOut["peak_working_set_mb"] = workingSetMB(true);
      benchOut["resolution"] = {app.R.width, app.R.height};
      benchOut["gl_renderer"] = (const char*)glGetString(GL_RENDERER);
      app.exitExplore();
      finish(app);
    }
    break;
  }
  }
  (void)dt;
}
