// --selftest-grass: deterministic placement, exclusion masks, rebuild on crater / reset / undo / apply.
// --benchmark-stress: dense-grass low flyover (Off vs High on one path) and repeated HE impacts with smoke.
#include "selftest.h"
#include "ground_paint.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cstdarg>

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
  // G1 grass bench
  std::vector<int> tiers; int bv = 0, bt = 0, blk = -1, blkFrames = 0;
  struct Acc { int n = 0; double grass = 0, shadow = 0, gpu = 0, clumps = 0, blades = 0, chunks = 0, sclumps = 0, sdraws = 0; std::vector<double> ft; } acc[2];   // [0] grass shadows on, [1] off
};
G g;
LassoRegion rectRegion(vec2 lo, vec2 hi) { LassoRegion r; r.add({lo, vec2(hi.x, lo.y), hi, vec2(lo.x, hi.y)}); return r; }
std::string fmt(const char* f, ...) { char b[600]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }

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
  ProcMem pmc = processMemory();
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
    {   // GX3: species layers, clump density near the eye, tunables
      int spc[24] = {0};
      app.grass.forEachInstance([&](const GrassInstance& i) { int s = Grass::speciesOf(i); if (s >= 0 && s < 24) ++spc[s]; });
      int fine = spc[0] + spc[1], tall = spc[2], broad = spc[3] + spc[17], shrub = spc[16];
      summary["species"] = {{"fine", fine}, {"seed_head", tall}, {"clover", spc[3]}, {"broadleaf", spc[17]}, {"straw", spc[4]}, {"shrub", shrub}};
      check(fine > tall && tall > 0 && broad > 0 && shrub > 0 && spc[4] > 0,
            "4+ species layers: fine " + std::to_string(fine) + ", seed-head " + std::to_string(tall) + ", clover / broadleaf " + std::to_string(broad) + ", shrub " + std::to_string(shrub) + ", straw " + std::to_string(spc[4]));
      const GrassPreset& M = grassPreset(GrassQuality::Medium);
      size_t nearC = 0, nearF = 0;   // G1: turf / clover keep fewer cards than the coarse species
      app.grass.forEachInstance([&](const GrassInstance& i) { if (glm::length(vec2(i.posYaw.x - g.cam.x, i.posYaw.z - g.cam.z)) < 15.0f) ++(Grass::fineSpecies(Grass::speciesOf(i)) ? nearF : nearC); });
      double cards = (nearC * M.blades[0] + nearF * M.fine[0]) * (M.density / Grass::kMaxDensity) / (3.14159265 * 225.0);
      summary["medium_cards_per_m2_within_15m"] = cards;
      // pre-GX3 Medium: 16 of 40 tufts per m^2 drawn x 7 blades, ~65 % of candidates kept = ~73 blades per m^2
      // G1: fewer cards per clump, each widened by (cards / 12)^-0.6 (the shader's widen): blade coverage ~ cards^0.4
      double gx3 = (nearC + nearF) * 12.0 * (M.density / Grass::kMaxDensity) / (3.14159265 * 225.0);
      double cover = (nearC * std::pow((double)M.blades[0], 0.4) + nearF * std::pow((double)M.fine[0], 0.4)) * std::pow(12.0, 0.6) * (M.density / Grass::kMaxDensity) / (3.14159265 * 225.0);
      summary["medium_cover_vs_gx3"] = cover / gx3;
      check(gx3 > 3.0 * 73.0 && cover > 0.8 * gx3, fmt("Medium within 15 m: %.0f cards per m^2 (GX3 %.0f, pre-GX3 73), blade coverage %.0f %% of GX3's", cards, gx3, 100.0 * cover / gx3));
      check(M.windR > 0 && M.windR <= 15 && M.shadowR > 0 && grassPreset(GrassQuality::Low).windR == 0 && M.radius < 58,
            "tiers: Medium wind <= 15 m, near clumps cast shadows, Low static, Medium trades radius for density");
      // G1: per chunk the coarse species come first, turf / clover after (two ranges, each a nested shuffled prefix)
      size_t bad = 0, nCoarse = 0, nFine = 0;
      app.grass.forEachChunk([&](const Grass::Chunk& c) { for (int k = 0; k < c.count; ++k) { bool f = Grass::fineSpecies(Grass::speciesOf(c.inst[k])); bad += f != (k >= c.coarse); ++(f ? nFine : nCoarse); } });
      check(bad == 0 && nCoarse > 0 && nFine > 0, fmt("species ranges: coarse first in every chunk (%zu nCoarse, %zu turf / clover, %zu out of place)", nCoarse, nFine, bad));
      const GrassPreset& H = grassPreset(GrassQuality::High); const GrassPreset& U = grassPreset(GrassQuality::Ultra);
      check(U.density > H.density && H.density >= M.density && U.radius > H.radius && H.radius > M.radius && U.nearR > H.nearR && U.windR > H.windR && U.shadowR > 0 &&
            M.fine[0] < M.blades[0] && H.fine[0] < H.blades[0] && M.shadowBlades < M.blades[0] && U.density <= Grass::kMaxDensity,
            fmt("tiers: Ultra %.0f clumps/m2 to %.0f m > High %.0f to %.0f m >= Medium %.0f to %.0f m; turf / clover and shadows use fewer cards", U.density, U.radius, H.density, H.radius, M.density, M.radius));
    }
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
      // applied ground damage is tied to the template version it was cut into
      app.deformGround(g.P, 1.0f); app.applyDamageToMap();
      fs::path tmp = userMapsDir() / "grass_template_version.nmap.json";
      app.saveMapTo(tmp);
      bool sameOk = app.openMap(tmp) && !app.doc.terrainDeltas.empty() && !app.terrain.tiles.empty();
      { json j; { std::ifstream in(tmp); in >> j; } j["terrainDeltas"]["templateVersion"] = 999; std::ofstream(tmp) << j.dump(2); }
      bool otherOk = app.openMap(tmp) && app.doc.terrainDeltas.empty() && app.terrain.tiles.empty();
      check(sameOk && otherOk, "applied ground damage reloads on its own template version and is dropped (with a notice) on another");
      app.hideEditorOverlays = false;
      {   // G1 regions: partly sanded ground grows sparse dune-grass tussocks, full sand none, sand near the water line none
        vec2 a(-44, 16), b(-30, 30), c(-44, -30), d(-30, -16);
        auto inRect = [](const GrassInstance& i, vec2 lo, vec2 hi) { return i.posYaw.x > lo.x + 2 && i.posYaw.x < hi.x - 2 && i.posYaw.z > lo.y + 2 && i.posYaw.z < hi.y - 2; };
        size_t before = 0; app.grass.forEachInstance([&](const GrassInstance& i) { before += inRect(i, a, b); });
        groundPaintRegion(app, rectRegion(a, b), kPaintSand, 0.6f, 1.0f);
        groundPaintRegion(app, rectRegion(c, d), kPaintSand, 1.0f, 1.0f);
        groundPaintSync(app); app.grass.clear(); syncGrass();
        size_t part = 0, dune = 0, full = 0;
        app.grass.forEachInstance([&](const GrassInstance& i) { if (inRect(i, a, b)) { ++part; dune += Grass::speciesOf(i) == Grass::DUNE; } full += inRect(i, c, d); });
        float wl = app.terrain.waterLevel; app.terrain.waterLevel = app.terrain.height(-37, 23) - 1.0f; app.grass.clear(); syncGrass();
        size_t wet = 0; app.grass.forEachInstance([&](const GrassInstance& i) { wet += inRect(i, a, b); });
        app.terrain.waterLevel = wl; app.grass.clear(); syncGrass();
        check(before > 500 && part > 0 && part * 3 < before && dune * 10 >= part * 9 && full == 0 && wet == 0,
              fmt("sand: %zu clumps unpainted -> %zu on 60 %% sand (%zu dune grass), %zu on full sand, %zu on sand 1 m above the water", before, part, dune, full, wet));
      }
      check(app.R.shaderErrors == 0, "all shaders compiled, terrain and grass included (" + std::to_string(app.R.shaderErrors) + " errors)");
      finish(app);
    }
    break;


  // ---------------------------------------------------------------- range example map (content/examples)
  case 980: {
    reportName = "range";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Range: walls, bunker, ordnance");
    struct P { const char* id; vec3 pos; float yaw; const char* role; };
    // the gun sits at the origin facing -Z (yaw 180); every target is inside its 32.5 deg traverse half-arc
    P list[] = {
      {"small_stone_dairy", vec3(-13, 0, -32), 0.0f, "ordinary masonry walls (200 hp panels)"},
      {"bunker_mg_casemate", vec3(15, 0, -38), 155.0f, "reinforced concrete bunker, rotated 155 deg (450 hp panels)"},
      {"gun_pak40", vec3(0, 0, 0), 180.0f, "PaK 40 station"},
      {"gun_m2hb_tripod", vec3(-7, 0, 1.5f), 180.0f, "M2HB tripod station"},
      {"crate_pak40_he", vec3(3.2f, 0, 3.0f), 90.0f, "PaK 40 HE crate inside the 6 m service radius"},
      {"crate_mixed_supply", vec3(-4, 0, 5.5f), 0.0f, "mixed small-arms / .50 belt crate"},
    };
    bool all = true;
    for (auto& e : list) {
      uint64_t id = app.placeInstance(app.lib.find(e.id), e.pos, e.yaw);
      check(id != 0, std::string("placed ") + e.id + ": " + e.role);
      all = all && id;
    }
    // grassy plot: the open pasture between the gun line and the targets is left empty on purpose
    vec3 g(0, 0, -16);
    check(app.groundWeight(g.x, g.z) >= 0.999f, "grassy plot 16 m down range is open pasture (no footprint)");
    for (int k = 0; k < 2; ++k) {
      vec3 tp = list[k].pos;
      float ang = degrees(std::atan2(tp.x, -tp.z));
      check(std::fabs(ang) < 32.0f, std::string(list[k].id) + " is within the PaK traverse (" + std::to_string((int)std::round(ang)) + " deg)");
    }
    app.doc.start = {true, vec3(0, app.terrain.height(0, 9), 9), 180};
    app.doc.camera = {true, vec3(26, 14, 22), 50, -22};
    app.syncRuntime();
    fs::path out = userMapsDir() / "range_example.nmap.json";
    check(all && app.saveMapTo(out), "range map saved to " + out.string());
    // reopen it the way a user would and step into Explore
    app.newMap(*t, "scratch");
    check(app.openMap(out) && app.doc.instances.size() == 6, "range map reopens with 6 placements");
    bool ex = app.enterExplore();
    check(ex && app.mode == Mode::Explore, "Explore (F5) starts at the range start marker");
    app.updateGrass(app.eyePos());
    check(app.grass.instanceCount() > 1000, "the grassy plot is grassed");
    app.exitExplore();
    finish(app);
    break;
  }

  // ---------------------------------------------------------------- G1 grass bench (--bench-grass)
  // Open pasture: standing, prone and walking views at each tier from Medium up. Per (view, tier): 1 s settle, then
  // 4 s of 0.4 s blocks alternating the grass sun shadow on / off (Renderer::perf.grassShadow), so the grass share of
  // the near cascade is the on - off difference of GP_ShadowNear. Shots of the static views go to selftest_out.
  case 960: {
    reportName = "grassbench";
    app.newMap(*app.findTemplate("blank_slate"), "Grass bench");
    app.syncRuntime(); app.hideEditorOverlays = true;
    g.tiers.clear();
    for (int q = 2; q <= 4; ++q) if (q == 2 || std::string(grassPreset((GrassQuality)q).name) != grassPreset((GrassQuality)(q - 1)).name) g.tiers.push_back(q);
    g.bv = 0; g.bt = 0; g.blk = -2; g.lastT = glfwGetTime();
    benchOut["resolution"] = {app.R.width, app.R.height};
    benchOut["gl_renderer"] = (const char*)glGetString(GL_RENDERER);
    go(961);
    break;
  }
  case 961: {
    static const char* views[] = {"open_field", "prone", "moving"};
    int q = g.tiers[g.bt];
    const char* tier = grassPreset((GrassQuality)q).name;
    float t = (float)stageTime;
    float x = g.bv == 2 ? -40.0f + 4.5f * t : 0.0f, z = 20.0f;   // moving: a 4.5 m/s jog
    app.camPos = vec3(x, app.terrain.height(x, z) + (g.bv == 1 ? 0.35f : 1.7f), z);
    app.camYaw = g.bv == 2 ? -90.0f + 20.0f * std::sin(t * 0.9f) : 30.0f; app.camPitch = g.bv == 1 ? -3.0f : -8.0f;
    if (g.blk == -2) {   // first frame of this (view, tier)
      app.grass.quality = (GrassQuality)q; app.R.perf.grassShadow = true;
      if (g.bv != 2) syncGrass();
      g.blk = -1; g.acc[0] = G::Acc(); g.acc[1] = G::Acc();
    }
    double nowT = glfwGetTime(), fms = (nowT - g.lastT) * 1000.0; g.lastT = nowT;
    if (g.bv != 2 && t > 0.8f && g.blk == -1 && g.pass != 100) { shotQueue.push_back(std::string("g1_") + views[g.bv] + "_" + tier); g.pass = 100; }
    if (t >= 1.0f) {
      int blk = std::min(9, (int)((t - 1.0f) / 0.4f));
      static const bool gaAB = std::getenv("NMK_GA_AB") != nullptr;   // GT1: alternate the grass plant atlas instead of the grass shadow (same-run A/B)
      if (blk != g.blk) { g.blk = blk; g.blkFrames = 0; if (gaAB) app.R.perf.grassAtlas = blk % 2 == 0; else app.R.perf.grassShadow = blk % 2 == 0; }
      if (++g.blkFrames > 5) {   // the pass timers lag 2-3 frames
        G::Acc& a = g.acc[blk % 2];
        a.grass += app.R.gpuPassMs[GP_Grass]; a.shadow += app.R.gpuPassMs[GP_ShadowNear]; a.gpu += app.R.stats.gpuMs;
        a.clumps += app.grass.lastDrawnClumps; a.blades += app.grass.lastDrawnBlades; a.chunks += app.grass.lastDrawnChunks;
        a.sclumps += app.grass.lastShadowClumps; a.sdraws += app.grass.lastShadowDraws; a.ft.push_back(fms); ++a.n;
      }
    }
    if (t >= 5.0f) {
      G::Acc& on = g.acc[0]; G::Acc& off = g.acc[1];
      double n0 = std::max(1, on.n), n1 = std::max(1, off.n);
      json s = stats(on.ft);
      s["grass_main_ms"] = on.grass / n0; s["shadow_near_ms"] = on.shadow / n0; s["shadow_near_no_grass_ms"] = off.shadow / n1;
      s["grass_shadow_ms"] = on.shadow / n0 - off.shadow / n1; s["grass_total_ms"] = on.grass / n0 + on.shadow / n0 - off.shadow / n1;
      s["gpu_ms"] = on.gpu / n0; s["clumps"] = on.clumps / n0; s["cards"] = on.blades / n0; s["main_draws"] = on.chunks / n0;
      s["shadow_clumps"] = on.sclumps / n0; s["shadow_draws"] = on.sdraws / n0; s["frames"] = {on.n, off.n};
      s["grass_main_off_ms"] = off.grass / n1; s["gpu_off_ms"] = off.gpu / n1;   // NMK_GA_AB: the blocks without the atlas
      if (std::getenv("NMK_GA_AB")) app.R.perf.grassAtlas = true;   // only the A/B mode toggles it (--rq-off grassatlas stays off)
      benchOut[views[g.bv]][tier] = s;
      check(true, fmt("%s %s: grass main %.3f + shadow %.3f ms (near cascade %.3f vs %.3f without grass), %.0f clumps, %.0f + %.0f draws",
                      views[g.bv], tier, on.grass / n0, on.shadow / n0 - off.shadow / n1, on.shadow / n0, off.shadow / n1, on.clumps / n0, on.chunks / n0, on.sdraws / n0));
      app.R.perf.grassShadow = true; g.pass = 0; g.blk = -2;
      if (++g.bt >= (int)g.tiers.size()) { g.bt = 0; ++g.bv; }
      if (g.bv >= 3) { app.grass.quality = GrassQuality::Medium; app.hideEditorOverlays = false; finish(app); }
      else stageTime = 0;
    }
    break;
  }

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
