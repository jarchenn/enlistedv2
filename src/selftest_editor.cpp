// --selftest-editor (stages 3300-3399): SPEC 9.1-9.3 editor & terrain.
//  * map sizes (rules, presets, rectangular grid, build area, save / reload) and a pre-9.1 map at its old size
//  * chunk streaming bounds (LOD rings, eviction, no visible holes) and memory on large maps
//  * terrain cuts: depth, bank, move / delete, crater + persistence on top, placement never sinks
//  * camera maths: zoom to cursor, orbit pivot, pan anchor, height-scaled speed, F / Home framing, bookmarks
//  * drop-in round trip: facing from the drag, first person at the drop, Esc restores the editor camera exactly
//  * first-person placement (VO7, builder.cpp): B in a drop-in, palette pick, ghost within 12 m, rotate, place as a
//    real map edit (editor undo stack), Del the aimed piece, Ctrl+Z, kept on return; a playtest stays a session overlay
//  * frame time: 600 x 600 vs 1200 x 2400 (same cameras), memory on 1200 x 2400
#include "selftest.h"
#include "editor_nav.h"
#include "scenario.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <fstream>
#include <cstring>
#include <cstdarg>
#include <windows.h>
#include <psapi.h>

namespace {
fs::path outDirEd() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
double wsMB() { PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc); return pmc.WorkingSetSize / 1048576.0; }
std::string fmt(const char* f, ...) { char b[512]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }
// the pre-9.1 terrain generator (800 m square, n = 256), kept here to prove old maps load unchanged
float lhash2(int x, int y, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
  return (h & 0xffffff) / float(0xffffff);
}
float lvnoise(float x, float y, uint32_t s) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  float a = lhash2(xi, yi, s), b = lhash2(xi + 1, yi, s), c = lhash2(xi, yi + 1, s), d = lhash2(xi + 1, yi + 1, s);
  return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy) * 2 - 1;
}
float legacyHeight(int x, int z, uint32_t seed) {
  const int n = 256; float size = 800, buildHalf = 300, cell = size / (n - 1);
  float wx = -size * 0.5f + x * cell, wz = -size * 0.5f + z * cell;
  float v = 0.35f * lvnoise(wx / 140.f, wz / 140.f, seed) + 0.12f * lvnoise(wx / 45.f, wz / 45.f, seed + 7) + 0.04f * lvnoise(wx / 12.f, wz / 12.f, seed + 13);
  float edge = std::max(std::fabs(wx), std::fabs(wz)) - buildHalf;
  if (edge > 0) v += edge * 0.03f + 2.0f * lvnoise(wx / 60.f, wz / 60.f, seed + 3) * std::min(1.0f, edge / 40.f);
  return v;
}
vec2 project(const mat4& vp, vec3 p) { vec4 q = vp * vec4(p, 1); return vec2(q) / q.w; }
mat4 viewProj(const App& app) {
  return glm::perspective(radians(60.0f), navAspect(app), 0.1f, 20000.0f) * glm::lookAt(app.camPos, app.camPos + App::forward(app.camYaw, app.camPitch), vec3(0, 1, 0));
}
void rayAt(const App& app, vec2 ndc, vec3& ro, vec3& rd) {
  mat4 inv = glm::inverse(viewProj(app));
  vec4 a = inv * vec4(ndc, -1, 1), b = inv * vec4(ndc, 1, 1);
  ro = vec3(a) / a.w; rd = glm::normalize(vec3(b) / b.w - ro);
}
struct PerfRun { std::string map, cam; std::vector<double> ms, cpu, gpuMs, grassMs, streamMs; int draws = 0, chunks = 0; int64_t tris = 0; size_t gpu = 0; int holes = 0; };
std::vector<PerfRun> gRuns;
int gPerfIdx = 0, gPerfFrame = 0; double gLastT = 0;
CamPose gEdCam; vec3 gDropAt{0}; float gDropYaw = 0; int gExploreFrames = 0;
}

void SelfTest::editorUpdate(App& app, float dt) {
  auto go = [&](int s) { stage = s; stageTime = 0; };
  if (reportName.empty()) reportName = "editor";
  const TemplateInfo* blank = app.findTemplate("blank_slate");
  switch (stage) {
  case 3300: {   // ================= map sizes, old maps
    if (!blank) { check(false, "Blank Slate template present"); finish(app); return; }
    for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--editor-shots") { reportName = "editor_shots"; gPerfFrame = 0; go(3370); return; }   // screenshots only (render A/B)
    const MapSizeRules& r = mapSizeRules();
    check(r.minSide == 400 && r.maxSide == 4000 && r.step == 100, fmt("size rules 400-4000 m in 100 m steps (got %.0f-%.0f / %.0f)", r.minSide, r.maxSide, r.step));
    vec2 c1 = r.clampSize(vec2(350, 4120)), c2 = r.clampSize(vec2(1234, 2449));
    check(c1 == vec2(400, 4000) && c2 == vec2(1200, 2400), fmt("sizes clamp and snap: 350x4120 -> %.0fx%.0f, 1234x2449 -> %.0fx%.0f", c1.x, c1.y, c2.x, c2.y));
    bool presets = r.presets.size() == 4 && r.presets[0] == vec2(600, 600) && r.presets[1] == vec2(1000, 2000) && r.presets[2] == vec2(1200, 2400) && r.presets[3] == vec2(2000, 4000);
    check(presets, "presets 600x600, 1000x2000, 1200x2400, 2000x4000");
    check(blank->width == 600 && blank->depth == 600, fmt("Blank Slate defaults to 600 x 600 (template %.0f x %.0f)", blank->width, blank->depth));
    check(blank->description.find("about 600 x 600") == std::string::npos && blank->description.find("size you choose") != std::string::npos, "Blank Slate description no longer says 'about 600 x 600 m'");
    // estimate + warning
    TerrainEstimate e6 = estimateTerrain(600, 600), e12 = estimateTerrain(1200, 2400), e24 = estimateTerrain(2000, 4000);
    check(e6.comfortable && e12.comfortable && !e24.comfortable, fmt("dialog warns above the comfortable size: 600x600 %d, 1200x2400 %d, 2000x4000 %d", e6.comfortable, e12.comfortable, e24.comfortable));
    check(e6.chunks == 13 * 13 && e12.chunks > e6.chunks && e24.chunks > e12.chunks, fmt("chunk count estimate: 600x600 %d, 1200x2400 %d, 2000x4000 %d", e6.chunks, e12.chunks, e24.chunks));
    summary["estimate"] = {{"600x600", {{"chunks", e6.chunks}, {"MB", e6.totalMB}}}, {"1200x2400", {{"chunks", e12.chunks}, {"MB", e12.totalMB}, {"cpu", e12.cpuMB}, {"physics", e12.physicsMB}, {"gpu", e12.gpuMB}, {"surface", e12.surfaceMB}}}, {"2000x4000", {{"chunks", e24.chunks}, {"MB", e24.totalMB}}}};

    // ---- a pre-9.1 map loads at its old size, with the old heights and its saved ground damage
    app.openMap(contentDir() / "examples" / "village_assault_5.nmap.json");
    const Terrain& T = app.terrain;
    check(app.doc.width == 0 && T.nx == 256 && T.nz == 256 && T.buildHalfX == 300 && T.buildHalfZ == 300 && std::fabs(T.size - 800) < 0.01f,
          fmt("pre-9.1 map (no mapSize) loads at its template size: %d x %d vertices, build %.0f x %.0f, terrain %.2f m", T.nx, T.nz, T.buildHalfX * 2, T.buildHalfZ * 2, T.size));
    float worst = 0;
    for (int z = 0; z < 256; z += 3) for (int x = 0; x < 256; x += 3) worst = std::max(worst, std::fabs(T.h[(size_t)z * T.nx + x] - legacyHeight(x, z, blank->seed)));
    check(worst < 2e-4f, fmt("pre-9.1 terrain heights unchanged (max diff %.6f m over the old 256 x 256 grid)", worst));
    check(T.tilesX() == 51 && T.key(3, 7) == 7 * 51 + 3, "deformation tile keys keep the old 51 x 51 indexing (saved crater deltas stay valid)");
    {  // saved ground damage round trip on the old grid
      std::map<int, std::vector<float>> d; std::vector<float> v(Terrain::kFineN * Terrain::kFineN, 0.0f); v[600] = -0.4f; d[T.key(20, 22)] = v;
      app.doc.terrainDeltas = d; app.doc.terrainTemplateVersion = app.doc.templateVersion; app.resetGround();
      auto back = app.terrain.deltas();
      check(back.size() == 1 && back.count(T.key(20, 22)) && std::fabs(back[T.key(20, 22)][600] + 0.4f) < 1e-4f, "saved crater deltas apply to the same tile and read back unchanged");
      app.doc.terrainDeltas.clear(); app.resetGround();
    }

    // ---- 1200 x 2400: rectangular grid, build area, save / reload
    navNewMapSized(app, *blank, "W3 size test", vec2(1200, 2400));
    check(app.doc.width == 1200 && app.doc.depth == 2400 && T.buildHalfX == 600 && T.buildHalfZ == 1200,
          fmt("new 1200 x 2400 map: doc %.0f x %.0f, build half %.0f / %.0f", app.doc.width, app.doc.depth, T.buildHalfX, T.buildHalfZ));
    check(T.size >= 1400 && T.sizeZ >= 2600 && T.cell() <= 800.0f / 255.0f + 1e-6f, fmt("terrain %.0f x %.0f m at %.3f m per vertex (resolution kept)", T.size, T.sizeZ, T.cell()));
    check(T.chunksX() * T.chunksZ() == estimateTerrain(1200, 2400).chunks, fmt("estimate matches the real chunk count (%d)", T.chunksX() * T.chunksZ()));
    const AssetDef* house = app.lib.find("house_stone_cottage");
    if (house) {
      vec2 hf = house->footprint * 0.5f;
      PlaceCheck in = app.checkPlacement(house, vec3(600 - hf.x - 1, 0, 1200 - hf.y - 1), 0, 0), out = app.checkPlacement(house, vec3(0, 0, 1200 + hf.y + 3), 0, 0), outX = app.checkPlacement(house, vec3(600 + hf.x + 3, 0, 0), 0, 0);
      check(in.ok && !out.ok && !outX.ok, fmt("build area follows the size: far corner accepts (%s), past z 1200 / x 600 rejected", in.reason.c_str()));
    }
    fs::path tmp = outDirEd() / "w3_size_test.nmap.json";
    app.saveMapTo(tmp);
    app.newMap(*blank, "other");
    check(app.terrain.buildHalfZ == 300, "a plain New Map is back to 600 x 600");
    app.openMap(tmp);
    check(app.doc.width == 1200 && app.doc.depth == 2400 && app.terrain.buildHalfZ == 1200 && app.terrain.nz == T.nz, fmt("map size saved with the map and restored on load (%.0f x %.0f)", app.doc.width, app.doc.depth));
    { json j = json::parse(readTextFile(tmp)); check(j.contains("mapSize") && j["mapSize"].value("width", 0) == 1200 && j["mapSize"].value("depth", 0) == 2400, "file stores mapSize {width, depth}"); }
    // physics: one heightfield per chunk, ground where the terrain is
    {
      int bodies = 0; for (auto b : app.terrain.chunkBodies) bodies += b != 0xffffffffu;
      float t; uint64_t ud; vec3 p(550, 0, -1150); float gy = app.terrain.height(p.x, p.z);
      bool hit = Physics::castRay(vec3(p.x, gy + 20, p.z), vec3(0, -1, 0), 50, t, ud);
      check(bodies == app.terrain.chunksX() * app.terrain.chunksZ() && hit && std::fabs(gy + 20 - t - gy) < 0.05f && ud == 0,
            fmt("Jolt heightfield per chunk (%d bodies); ground collides at the far corner (err %.3f m)", bodies, hit ? std::fabs(20 - t) : -1.0f));
    }
    go(3310);
    break;
  }
  case 3310: {   // ================= chunk streaming bounds and memory (2000 x 4000)
    navNewMapSized(app, *blank, "W3 stream test", vec2(2000, 4000));
    Terrain& T = app.terrain;
    TerrainStream& S = T.stream;
    TerrainStreamConfig keep = S.cfg;
    auto settle = [&](vec3 cam, float yaw, float pitch) {
      app.camPos = cam; app.camYaw = yaw; app.camPitch = pitch;
      mat4 vp = viewProj(app);
      for (int k = 0; k < 6; ++k) S.update(T, cam, vp, -1.0f);
      return vp;
    };
    vec3 cam(-900, T.height(-900, -1900) + 2.0f, -1900);
    S.update(T, cam, viewProj(app), -1.0f);   // first update resets the stream (and its config) for this terrain
    keep = S.cfg;
    S.cfg.maxDist = 1500;   // tighter than the map so eviction is exercised
    settle(cam, 225, -10);
    int total = (int)S.chunks.size(), loaded = (int)S.loadedCount(), badLod = 0, farLoaded = 0, lod0 = S.stats.loaded[0];
    for (int i = 0; i < total; ++i) {
      int l = S.chunks[i].lod; float d = S.chunkDistance(T, i, cam);
      if (l >= 0 && d > S.cfg.maxDist * (1 + S.cfg.hysteresis)) ++farLoaded;
      if (l == 0 && d > S.cfg.lodDist[0] * (1 + S.cfg.hysteresis)) ++badLod;
      if (l > 0 && l < 4 && d < S.cfg.lodDist[l - 1] * (1 - S.cfg.hysteresis)) ++badLod;
    }
    float cs = Terrain::kCell * Terrain::kChunkCells;
    int lod0Max = (int)(3.14159f * std::pow(S.cfg.lodDist[0] * 1.08f + cs * 1.42f, 2) / (cs * cs)) + 1;
    check(loaded < total && farLoaded == 0, fmt("streaming: %d of %d chunks hold a mesh at the map corner; none beyond %.0f m", loaded, total, S.cfg.maxDist));
    check(badLod == 0 && lod0 > 0 && lod0 <= lod0Max, fmt("LOD rings: %d chunks at full resolution (bound %d), no chunk off its ring", lod0, lod0Max));
    // walk to the far corner: meshes follow, the first corner is evicted
    vec3 cam2(900, T.height(900, 1900) + 2.0f, 1900);
    settle(cam2, 45, -10);
    int nearOld = 0; for (int i = 0; i < total; ++i) if (S.chunks[i].lod >= 0 && S.chunkDistance(T, i, cam) < 200) ++nearOld;
    check(nearOld == 0 && S.stats.loaded[0] > 0, fmt("moving 4.4 km: chunks near the start evicted, %d full-resolution chunks around the camera", S.stats.loaded[0]));
    // memory
    size_t gpuCorner = S.stats.gpuBytes;
    S.cfg = keep;
    CamPose home = navHomePose(app, navAspect(app));
    settle(home.pos, home.yaw, home.pitch);
    size_t gpuHome = S.stats.gpuBytes;
    check(gpuHome < (size_t)48 << 20 && gpuCorner < (size_t)48 << 20, fmt("terrain mesh memory on 2000 x 4000: %.1f MB walking, %.1f MB whole map in view (budget 48 MB)", gpuCorner / 1048576.0, gpuHome / 1048576.0));
    summary["stream_2000x4000"] = {{"chunks", total}, {"loaded_corner", loaded}, {"lod0_corner", lod0}, {"gpu_mb_corner", gpuCorner / 1048576.0}, {"gpu_mb_home", gpuHome / 1048576.0}, {"cpu_heights_mb", T.h.size() * 4 / 1048576.0}};
    // budgeted streaming: a camera jump never leaves visible holes for long
    app.camPos = cam; app.camYaw = 225; app.camPitch = -10;
    int frames = 0; for (; frames < 30; ++frames) { S.update(T, app.camPos, viewProj(app), S.cfg.buildBudgetMs); if (S.stats.visibleUnloaded == 0) break; }
    check(frames < 30, fmt("after a 4.4 km jump the view is complete within %d budgeted frame(s)", frames + 1));
    go(3320);
    break;
  }
  case 3320: {   // ================= terrain cuts
    navNewMapSized(app, *blank, "W3 cut test", vec2(600, 600));
    app.lib.loadAll(contentDir() / "tests" / "assets");
    const AssetDef* tr = app.lib.find("test_trench");
    check(tr && tr->navMeta.terrainCuts.size() == 1, "authored test asset with a terrainCut loads (content/tests/assets/test_trench.json)");
    if (!tr) { go(3330); break; }
    Terrain& T = app.terrain;
    vec3 at(40, 0, -30);
    uint64_t id = app.placeInstance(tr, at, 30.0f);
    Instance* in = app.doc.find(id);
    float y0 = in ? in->pos.y : 0;
    navSyncCuts(app);
    quat q = yawQuat(30.0f);
    auto W = [&](float x, float z) { vec3 v = q * vec3(x, 0, z); return vec2(at.x + v.x, at.z + v.z); };
    vec2 c = W(0, 0), deep = W(4.0f, 0.4f);
    float hc = T.height(c.x, c.y), pad = T.fineSpacing();
    check(std::fabs(hc - (y0 - 1.2f)) < 0.01f && std::fabs(T.height(deep.x, deep.y) - (y0 - 1.2f)) < 0.01f, fmt("cut floor is flat at instance y - depth: centre %.3f, off-centre %.3f, want %.3f", hc, T.height(deep.x, deep.y), y0 - 1.2f));
    // the floor reaches the outline (revetments stand there): one grid step of padding takes the bank outward
    vec2 edgeIn = W(0, -1.2f + 0.05f), gridIn = W(0, -1.2f + pad + 0.05f), bankP = W(0, -1.2f - pad + 0.2f);
    float wantBank = std::min(T.baseHeight(bankP.x, bankP.y), y0 - 1.2f * 0.5f), cb = T.cutBase(bankP.x, bankP.y);
    check(std::fabs(T.cutBase(edgeIn.x, edgeIn.y) - (y0 - 1.2f)) < 1e-4f && std::fabs(T.height(gridIn.x, gridIn.y) - (y0 - 1.2f)) < 0.02f,
          fmt("floor reaches the outline: cut %.3f 5 cm inside the edge, drawn %.3f one grid step in (floor %.3f)", T.cutBase(edgeIn.x, edgeIn.y), T.height(gridIn.x, gridIn.y), y0 - 1.2f));
    check(std::fabs(cb - wantBank) < 1e-4f, fmt("bank: halfway up the 0.4 m bank (%.2f m grid padding) the cut is %.3f (want %.3f)", pad, cb, wantBank));
    vec2 out2 = W(0, -1.2f - 2 * pad - 0.05f);
    float ho2 = T.height(out2.x, out2.y), bo2 = T.baseHeight(out2.x, out2.y);
    check(std::fabs(ho2 - bo2) < 1e-4f, fmt("two grid steps outside the outline the ground is untouched (%.4f vs base %.4f)", ho2, bo2));
    {
      float t; uint64_t ud;
      bool hit = Physics::castRay(vec3(c.x, y0 + 5, c.y), vec3(0, -1, 0), 20, t, ud);
      check(hit && std::fabs((y0 + 5 - t) - hc) < 0.03f, fmt("physics follows the cut floor (hit %.3f, ground %.3f)", hit ? y0 + 5 - t : -99.0f, hc));
    }
    // re-placing never sinks it into its own cut
    PlaceCheck pc = app.checkPlacement(tr, in->pos, in->yaw, id);
    check(pc.ok && std::fabs(pc.y - y0) < 1e-4f, fmt("placement height ignores the asset's own cut (%.4f vs %.4f)", pc.y, y0));
    // move
    app.pushUndo(); in = app.doc.find(id); in->pos.x += 25; { PlaceCheck m = app.checkPlacement(tr, in->pos, in->yaw, id); in->pos.y = m.y; } app.bump(*in);
    navSyncCuts(app);
    float oldC = T.height(c.x, c.y);
    vec2 c2 = W(0, 0) + vec2(25, 0);
    check(std::fabs(oldC - T.baseHeight(c.x, c.y)) < 1e-4f && T.height(c2.x, c2.y) < T.baseHeight(c2.x, c2.y) - 1.0f, fmt("moving the trench refills the old cut (%.3f) and cuts the new spot (%.2f m deep)", oldC - T.baseHeight(c.x, c.y), T.baseHeight(c2.x, c2.y) - T.height(c2.x, c2.y)));
    // undo restores the first position's cut
    app.undo(); navSyncCuts(app);
    check(std::fabs(T.height(c.x, c.y) - hc) < 1e-3f, "undo restores the cut where it was");
    // crater inside the trench, applied to the map, survives save / reload without cutting twice
    app.craterAt(vec3(c.x, T.height(c.x, c.y), c.y), 1.0f, 0.3f, false);
    float hcr = T.height(c.x, c.y);
    app.applyDamageToMap();
    fs::path tmp = outDirEd() / "w3_cut_test.nmap.json";
    app.saveMapTo(tmp);
    app.openMap(tmp);
    navSyncCuts(app);
    check(std::fabs(app.terrain.height(c.x, c.y) - hcr) < 2e-3f && hcr < hc - 0.05f, fmt("crater in the trench: %.3f after reload (saved %.3f, trench floor %.3f) - the cut is not stored twice", app.terrain.height(c.x, c.y), hcr, hc));
    // delete: ground back to the base, tiles freed
    for (auto& i : app.doc.instances) if (i.assetId == "test_trench") { app.selected = i.id; break; }
    app.deleteSelected(); app.doc.terrainDeltas.clear(); app.resetGround(); navSyncCuts(app);
    check(std::fabs(app.terrain.height(c.x, c.y) - app.terrain.baseHeight(c.x, c.y)) < 1e-4f && app.terrain.tiles.empty() && app.terrain.cuts.empty(), fmt("deleting the trench restores the ground (%zu tiles left)", app.terrain.tiles.size()));
    go(3330);
    break;
  }
  case 3330: {   // ================= camera maths, framing, bookmarks
    navNewMapSized(app, *blank, "W3 camera test", vec2(1200, 2400));
    Terrain& T = app.terrain;
    float aspect = navAspect(app);
    // zoom toward the cursor: the ground point under the cursor stays put and gets closer
    app.camPos = vec3(10, T.height(10, 40) + 60, 40); app.camYaw = 20; app.camPitch = -35;
    vec2 ndc(0.35f, -0.2f); vec3 ro, rd, hit; rayAt(app, ndc, ro, rd);
    bool g = T.raycast(ro, rd, 5000, hit);
    float d0 = glm::length(hit - app.camPos);
    navZoomAlongRay(app, 2.0f, ro, rd);
    vec2 after = project(viewProj(app), hit);
    float d1 = glm::length(hit - app.camPos), want = std::pow(1 - navSettings().zoomStep, 2.0f);
    check(g && glm::length(after - ndc) < 2e-3f && std::fabs(d1 / d0 - want) < 0.02f, fmt("wheel zooms toward the cursor: point stays at the cursor (err %.4f), distance x%.3f (want %.3f)", glm::length(after - ndc), d1 / d0, want));
    // orbit about a ground pivot: the pivot keeps its place on screen
    vec3 pivot = hit; vec2 pv0 = project(viewProj(app), pivot);
    navOrbit(app, pivot, 35.0f, -12.0f);
    vec2 pv1 = project(viewProj(app), pivot);
    check(glm::length(pv1 - pv0) < 2e-3f && std::fabs(glm::length(app.camPos - pivot) - d1) < 0.01f, fmt("right-drag orbit keeps the pivot under the cursor (err %.4f) at the same distance", glm::length(pv1 - pv0)));
    // pan: the grabbed ground point ends up under the cursor's new ray
    vec3 anchor = pivot; vec3 ro2, rd2; rayAt(app, vec2(-0.3f, 0.25f), ro2, rd2);
    navPanTo(app, anchor, anchor.y, ro2, rd2);
    vec3 ro3, rd3; rayAt(app, vec2(-0.3f, 0.25f), ro3, rd3);
    float tt = (anchor.y - ro3.y) / rd3.y; vec3 q3 = ro3 + rd3 * tt;
    check(glm::length(vec2(q3.x - anchor.x, q3.z - anchor.z)) < 0.02f, fmt("middle-drag pan keeps the grabbed point under the cursor (err %.3f m)", glm::length(vec2(q3.x - anchor.x, q3.z - anchor.z))));
    // fly speed scales with height
    app.camPos.y = T.height(app.camPos.x, app.camPos.z) + 2; float s2 = navFlySpeed(app);
    app.camPos.y = T.height(app.camPos.x, app.camPos.z) + 300; float s300 = navFlySpeed(app);
    check(s300 > s2 * 10, fmt("WASD speed scales with height: %.1f m/s at 2 m, %.0f m/s at 300 m", s2, s300));
    // F: frame the selection
    const AssetDef* house = app.lib.find("house_farmhouse_2story");
    uint64_t hid = house ? app.placeInstance(house, vec3(200, 0, -700), 15) : 0;
    app.selected = hid;
    app.camPos = vec3(-300, 200, 400); app.camYaw = 140; app.camPitch = -20;
    bool fr = navFrameSelection(app);
    navFinishFlight(app);
    if (Instance* hi = app.doc.find(hid)) {
      mat4 M = app.instanceMatrix(*hi); vec3 lo(1e30f), hh(-1e30f);
      for (int k = 0; k < 8; ++k) { vec3 cc((k & 1) ? house->bmax.x : house->bmin.x, (k & 2) ? house->bmax.y : house->bmin.y, (k & 4) ? house->bmax.z : house->bmin.z); vec3 w = vec3(M * vec4(cc, 1)); lo = glm::min(lo, w); hh = glm::max(hh, w); }
      float m = 0; bool inView = navBoxInView(lo, hh, {app.camPos, app.camYaw, app.camPitch}, aspect, 60.0f, &m);
      check(fr && inView && m > 0.8f / navSettings().frameMargin && app.camYaw == 140.0f, fmt("F frames the selection: whole house in view, filling %.0f %% of the frame, heading kept", m * 100));
    } else check(false, "farmhouse placed for the F test");
    // Home: the whole build area in view, tightly
    CamPose home = navHomePose(app, aspect);
    float m = 0;
    bool all = navBoxInView(vec3(-T.buildHalfX, T.height(0, 0) - 1, -T.buildHalfZ), vec3(T.buildHalfX, T.height(0, 0) + 1, T.buildHalfZ), home, aspect, 60.0f, &m);
    bool corners = true;
    for (int k = 0; k < 4; ++k) { float x = (k & 1) ? T.buildHalfX : -T.buildHalfX, z = (k & 2) ? T.buildHalfZ : -T.buildHalfZ; vec3 p(x, T.height(x, z), z); vec2 s = project(glm::perspective(radians(60.0f), aspect, 0.1f, 1e6f) * glm::lookAt(home.pos, home.pos + App::forward(home.yaw, home.pitch), vec3(0, 1, 0)), p); corners &= std::fabs(s.x) <= 1 && std::fabs(s.y) <= 1; }
    check(all && corners && m > 0.75f, fmt("Home frames the whole 1200 x 2400 map (corners in view, %.0f %% of the frame) from %.0f m up", m * 100, home.pos.y - T.height(home.pos.x, home.pos.z)));
    navFrameMap(app);
    for (int k = 0; k < 40; ++k) navStepFlight(app, 1.0f / 60.0f);
    check(!navState().flying && app.camPos == home.pos, "Home flight ends exactly on the framed pose");
    // bookmarks
    app.camPos = vec3(123.25f, 45.5f, -321.75f); app.camYaw = 33.5f; app.camPitch = -21.25f;
    navSaveBookmark(app, 3);
    app.camPos = vec3(0, 400, 0); app.camYaw = 200; app.camPitch = -70;
    bool rec = navRecallBookmark(app, 3); navFinishFlight(app);
    check(rec && app.camPos == vec3(123.25f, 45.5f, -321.75f) && app.camYaw == 33.5f && app.camPitch == -21.25f, "Ctrl+3 / 3: bookmark recall restores the exact view");
    check(!navRecallBookmark(app, 7), "an empty bookmark slot does nothing");
    fs::path tmp = outDirEd() / "w3_bookmarks.nmap.json";
    app.saveMapTo(tmp); app.openMap(tmp);
    CamPose bk; bool kept = navBookmark(app, 3, bk);
    check(kept && bk.pos == vec3(123.25f, 45.5f, -321.75f), "bookmarks are saved with the map");
    // overview: jump keeps height / heading and centres the view on the clicked point
    app.camPos = vec3(0, T.height(0, 0) + 80, 100); app.camYaw = 0; app.camPitch = -40;
    navOverviewJump(app, vec2(300, -900)); navFinishFlight(app);
    vec3 hc; bool ok = T.raycast(app.camPos, App::forward(app.camYaw, app.camPitch), 5000, hc);
    check(ok && glm::length(vec2(hc.x - 300, hc.z + 900)) < 3.0f, fmt("overview click moves the camera to look at the point (centre lands %.1f m away)", ok ? glm::length(vec2(hc.x - 300, hc.z + 900)) : -1.0f));
    // the overview reads the scenario (objectives, spawns, path) from doc.extra["scenario"]
    {
      Scenario sc; Objective o; o.id = "o1"; o.order = 0; o.pos = vec3(100, 0, -500); sc.objectives.push_back(o);
      SpawnMarker sm; sm.id = "s1"; sm.pos = vec3(0, 0, 900); sc.spawns.push_back(sm);
      app.doc.extra["scenario"] = sc.toJson();
      Scenario back = Scenario::fromJson(app.doc.extra["scenario"]);
      check(back.objectives.size() == 1 && back.spawns.size() == 1, "overview data: objectives and spawns read back through Scenario::fromJson");
      navState().overview = true;   // drawn on the next frames (exercises the texture + drawing path)
    }
    gPerfFrame = 0;
    go(3335);
    break;
  }
  case 3335: {   // overview drawn for a few frames, then closed
    if (++gPerfFrame < 4) break;
    check(navState().ovTex != 0 && navState().ovTexW == (int)(navSettings().overviewTexture * app.terrain.size / app.terrain.sizeZ) && navState().ovTexH == navSettings().overviewTexture,
          fmt("overview texture built (%d x %d for a 1:2 map)", navState().ovTexW, navState().ovTexH));
    navState().overview = false;
    go(3340);
    break;
  }
  case 3340: {   // ================= drop-in round trip
    navNewMapSized(app, *blank, "W3 drop-in test", vec2(600, 600));
    Terrain& T = app.terrain;
    gEdCam = {vec3(-12.3456f, T.height(-12, 30) + 23.4567f, 30.789f), 123.456f, -27.891f};
    app.camPos = gEdCam.pos; app.camYaw = gEdCam.yaw; app.camPitch = gEdCam.pitch;
    // drag over the ground towards +x: the facing follows the drag direction
    NavState& n = navState();
    n.dropDrag = true; n.dropFaceSet = false; n.dropYaw = app.camYaw;
    for (int k = 0; k <= 10; ++k) dropinDragUpdate(app, vec3(-20 + k * 0.5f, 0, 10 + k * 0.05f), true);
    float wantYaw = yawOfDir(vec3(0.5f, 0, 0.05f));
    check(n.dropValid && std::fabs(n.dropYaw - wantYaw) < 0.5f, fmt("drag direction sets the facing (%.1f deg, want %.1f)", n.dropYaw, wantYaw));
    n.dropDrag = false;
    gDropAt = n.dropPos; gDropYaw = n.dropYaw;
    bool ok = dropinCommit(app, gDropAt, gDropYaw);
    auto ch = Physics::character();
    check(ok && app.mode == Mode::Explore && glm::length(vec2(ch.pos.x - gDropAt.x, ch.pos.z - gDropAt.z)) < 0.05f && app.eyeYaw == wrapDeg(gDropYaw),
          fmt("release enters first person at the drop point (%.2f m off), facing %.1f deg", glm::length(vec2(ch.pos.x - gDropAt.x, ch.pos.z - gDropAt.z)), app.eyeYaw));
    check(app.doc.start.set && glm::length(vec2(app.doc.start.pos.x - gDropAt.x, app.doc.start.pos.z - gDropAt.z)) < 1e-4f && app.doc.start.yaw == wrapDeg(gDropYaw), "the drop point is saved as the map's explore start");
    check(navState().creativeSession, "drop-in session: Esc returns to the editor (P pauses)");
    gExploreFrames = 0;
    app.useScripted = true; app.scriptedInput = ExploreInput{}; app.scriptedInput.move = vec2(0, 1);   // walk a little
    go(3345);
    break;
  }
  case 3345: {
    if (++gExploreFrames < 45) break;
    app.scriptedInput = ExploreInput{}; app.useScripted = false;
    auto ch = Physics::character();
    float walked = glm::length(vec2(ch.pos.x - gDropAt.x, ch.pos.z - gDropAt.z));
    check(walked > 0.5f, fmt("normal player movement from the drop point (walked %.2f m in 0.75 s)", walked));
    bool back = dropinReturn(app);
    check(back && app.mode == Mode::Creative && app.camPos == gEdCam.pos && app.camYaw == gEdCam.yaw && app.camPitch == gEdCam.pitch,
          fmt("Esc returns the editor camera exactly (pos err %.7f, yaw %.7f, pitch %.7f)", glm::length(app.camPos - gEdCam.pos), app.camYaw - gEdCam.yaw, app.camPitch - gEdCam.pitch));
    // "Explore from start" reuses the saved drop point
    bool again = w3EnterExplore(app);
    auto ch2 = Physics::character();
    check(again && glm::length(vec2(ch2.pos.x - gDropAt.x, ch2.pos.z - gDropAt.z)) < 0.05f && app.eyeYaw == wrapDeg(gDropYaw), "Explore from start reuses the last drop point and facing");
    dropinReturn(app);
    check(app.camPos == gEdCam.pos && app.camYaw == gEdCam.yaw, "and returns to the same editor camera");
    fs::path tmp = outDirEd() / "w3_dropin.nmap.json";
    app.saveMapTo(tmp); app.newMap(*blank, "x"); app.openMap(tmp);
    check(app.doc.start.set && glm::length(app.doc.start.pos - vec3(gDropAt.x, app.terrain.height(gDropAt.x, gDropAt.z), gDropAt.z)) < 1e-3f, "explore start saved with the map");
    gExploreFrames = 0;
    go(3346);
    break;
  }
  case 3346: {   // ================= first-person placement (builder.cpp, VO7): B in a drop-in builds real map edits
    static uint64_t crate = 0, house = 0; static size_t n0 = 0, u0 = 0; static int removed0 = 0;
    static const AssetDef* crateA = nullptr; static const AssetDef* houseA = nullptr;
    int f = gExploreFrames++;
    auto aimAt = [&](vec3 p) { vec3 e = app.eyePos(); app.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); app.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); };
    auto has = [&](uint64_t id) { return app.doc.find(id) != nullptr; };
    app.scriptedInput.fire = false;
    if (f == 0) {
      navNewMapSized(app, *blank, "VO7 first-person build", vec2(600, 600));
      crateA = app.lib.find("crate_mixed_supply"); houseA = app.lib.find("house_stone_cottage");
      check(crateA && houseA, "first-person build: test assets (crate_mixed_supply, house_stone_cottage) in the library");
      if (!crateA || !houseA) { go(3350); break; }
      bool ok = dropinCommit(app, vec3(20, 0, 20), 0.0f);
      check(ok && app.mode == Mode::Explore && navState().creativeSession, "first-person build: dropped in from Creative");
      app.useScripted = true; app.scriptedInput = ExploreInput{}; app.vm.procedural = false;
      n0 = app.doc.instances.size(); u0 = app.undoStack.size(); app.dirty = false;
      app.scriptedInput.build = true;   // B
      break;
    }
    if (f == 2) {
      check(app.build.active && app.build.realEdits, "B opens build mode; placements are map edits in a Creative drop-in");
      app.build.pick = crateA->id;   // what clicking its thumbnail in the Assets palette does (palette.cpp)
      app.eyeYaw = 0; app.eyePitch = -28;
    }
    if (f == 6) {
      vec3 feet = Physics::character().pos;
      float d = glm::length(vec2(app.build.pos.x - feet.x, app.build.pos.z - feet.z));
      check(app.build.asset == crateA && app.build.valid && d > 1.0f && d < 12.0f, fmt("crate ghost in front of the player, %.1f m away, valid (%s)", d, app.build.reason.c_str()));
      app.scriptedInput.fire = true;   // LMB
    }
    if (f == 8) {
      crate = app.doc.instances.size() > n0 ? app.doc.instances.back().id : 0;
      const Instance* ci = app.doc.find(crate);
      bool real = ci && ci->assetId == crateA->id && !(ci->extra.is_object() && ci->extra.value("session", false));
      check(real && app.dirty && app.undoStack.size() == u0 + 1, fmt("LMB places it as a real map edit (no session flag, map dirty, one editor undo step: %d)", (int)(app.undoStack.size() - u0)));
      app.build.pick = houseA->id; app.build.pendRot = 90.0f;   // Z / X / wheel: rotate
      app.eyeYaw = 90; app.eyePitch = -14;
    }
    if (f == 12) {
      vec3 feet = Physics::character().pos;
      vec3 lp = glm::inverse(yawQuat(app.build.ghostYaw)) * (feet - app.build.pos);
      bool outside = std::fabs(lp.x) > houseA->footprint.x * 0.5f || std::fabs(lp.z) > houseA->footprint.y * 0.5f;
      check(app.build.asset == houseA && app.build.valid && outside && std::fabs(wrapDeg(app.build.ghostYaw - app.eyeYaw - 90.0f)) < 0.5f,
            fmt("house ghost: valid (%s), rotated 90 deg off the view, its footprint kept off the player", app.build.reason.c_str()));
      app.scriptedInput.fire = true;
    }
    if (f == 14) {
      house = app.doc.instances.size() > n0 + 1 ? app.doc.instances.back().id : 0;
      check(has(house) && app.doc.find(house)->assetId == houseA->id, "the house is placed through checkPlacement / placeInstance");
      if (const Instance* ci = app.doc.find(crate)) aimAt(ci->pos + vec3(0, 0.3f, 0));
    }
    if (f == 18) { check(app.build.aimId == crate, fmt("aiming at the crate targets it for Del (aimed #%llu)", (unsigned long long)app.build.aimId)); removed0 = app.build.removed; app.build.pendDel = true; }
    if (f == 20) { check(!has(crate) && app.build.removed == removed0 + 1, "Del removes the aimed placement"); app.build.pendUndo = true; }
    if (f == 22) { check(has(crate), "Ctrl+Z brings it back"); app.build.pendUndo = true; }
    if (f == 24) { check(!has(house) && has(crate), "Ctrl+Z again takes the house away"); app.build.pendUndo = true; }
    if (f == 26) {
      check(!has(crate) && app.doc.instances.size() == n0 && app.undoStack.size() == u0, fmt("and once more the crate: map and editor undo stack back where they started (%d instances, %d undo steps)", (int)(app.doc.instances.size() - n0), (int)(app.undoStack.size() - u0)));
      app.build.pick = crateA->id; app.eyeYaw = 0; app.eyePitch = -28;
    }
    if (f == 30) app.scriptedInput.fire = true;
    if (f == 32) {
      crate = app.doc.instances.size() > n0 ? app.doc.instances.back().id : 0;
      app.scriptedInput = ExploreInput{}; app.useScripted = false;
      dropinReturn(app);
      check(app.mode == Mode::Creative && has(crate) && app.dirty, "back in the editor the first-person placement is part of the map");
      app.undo();
      check(!has(crate), "and the editor's Undo removes it");
      // outside a Creative drop-in (F5 playtest) placements stay a session overlay
      check(app.enterExplore(), "F5 playtest");
      app.useScripted = true; app.scriptedInput = ExploreInput{}; app.scriptedInput.build = true;
    }
    if (f == 34) { app.build.pick = crateA->id; app.eyeYaw = 0; app.eyePitch = -28; check(app.build.active && !app.build.realEdits, "in a playtest build mode is a session overlay"); }
    if (f == 38) app.scriptedInput.fire = true;
    if (f == 40) {
      const Instance* ci = app.doc.instances.empty() ? nullptr : &app.doc.instances.back();
      bool sess = ci && ci->assetId == crateA->id && ci->extra.is_object() && ci->extra.value("session", false);
      uint64_t sid = ci ? ci->id : 0;
      app.scriptedInput = ExploreInput{}; app.useScripted = false;
      app.exitExplore();
      check(sess && !has(sid), "...placed with the session flag and discarded on the way back to Creative");
      gRuns.clear(); gPerfIdx = 0; gPerfFrame = 0;
      go(3350);
    }
    break;
  }
  case 3350: {   // ================= frame time: same cameras on 600 x 600 and 1200 x 2400
    // Interleaved blocks (cam, map A, map B, A, B, ...) and the minimum of the block medians: other GPU work on the
    // machine inflates single runs, never all blocks of one configuration.
    static const char* cams[] = {"ground", "flyover", "home"};
    static const vec2 maps[] = {{600, 600}, {1200, 2400}};
    const int blocksPerMap = 3, warm = 40, frames = 150, nBlocks = 3 * 2 * blocksPerMap;
    double tnow = glfwGetTime();
    if (gPerfIdx >= nBlocks) { go(3360); break; }
    int cam = gPerfIdx / (2 * blocksPerMap), mi = gPerfIdx % 2;
    Terrain& T = app.terrain;
    char mapName[32]; std::snprintf(mapName, sizeof mapName, "%.0fx%.0f", maps[mi].x, maps[mi].y);
    if (gPerfFrame == 0) { navNewMapSized(app, *blank, std::string("perf ") + mapName, maps[mi]); gRuns.push_back({mapName, cams[cam]}); }
    // camera path (identical in metres on both maps)
    float u = (float)gPerfFrame / (warm + frames);
    if (cam == 0) { float x = -200 + 400 * u, z = 120; app.camPos = vec3(x, T.height(x, z) + 1.8f, z); app.camYaw = 290; app.camPitch = -3; }
    else if (cam == 1) { float a = u * 6.2831853f; app.camPos = vec3(std::cos(a) * 180, 0, std::sin(a) * 180); app.camPos.y = T.height(app.camPos.x, app.camPos.z) + 60; app.camYaw = wrapDeg(-degrees(a)); app.camPitch = -18; }
    else { CamPose h = navHomePose(app, navAspect(app)); app.camPos = h.pos; app.camYaw = wrapDeg(h.yaw + 30 * std::sin(u * 6.28f)); app.camPitch = h.pitch; }
    PerfRun& r = gRuns.back();
    if (gPerfFrame > warm) { r.ms.push_back((tnow - gLastT) * 1000.0); r.cpu.push_back(app.R.stats.cpuMs); r.gpuMs.push_back(app.R.stats.gpuMs); r.grassMs.push_back(app.grass.lastBuildMs); r.streamMs.push_back(T.stream.stats.buildMs); }
    if (gPerfFrame > warm + 5 && T.stream.stats.visibleUnloaded > 0) ++r.holes;
    r.draws = std::max(r.draws, T.stream.stats.drawCalls); r.chunks = std::max(r.chunks, T.stream.stats.drawnLastFrame);
    r.tris = std::max<int64_t>(r.tris, T.stream.stats.tris); r.gpu = std::max(r.gpu, T.stream.stats.gpuBytes);
    gLastT = tnow;
    if (++gPerfFrame > warm + frames) { ++gPerfIdx; gPerfFrame = 0; }
    break;
  }
  case 3360: {
    json perf = json::object();
    auto med = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    auto p95 = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[(size_t)(v.size() * 0.95)]; };
    std::map<std::string, std::vector<const PerfRun*>> by;
    for (auto& r : gRuns) by[r.map + "_" + r.cam].push_back(&r);
    std::map<std::string, double> best;
    for (auto& [k, runs] : by) {
      json blocks = json::array(); double b = 1e9, bc = 1e9, bg = 1e9; int holes = 0; const PerfRun* last = runs.back();
      for (auto* r : runs) { double m = med(r->ms); blocks.push_back(m); if (m < b) { b = m; bc = med(r->cpu); bg = med(r->gpuMs); } holes += r->holes; }
      best[k] = b;
      perf[k] = {{"p50_ms_best_block", b}, {"p50_ms_blocks", blocks}, {"p95_ms_last_block", p95(last->ms)}, {"render_cpu_ms", bc}, {"gpu_ms", bg}, {"grass_build_ms", med(last->grassMs)}, {"terrain_stream_ms", med(last->streamMs)},
                 {"terrain_draw_calls", last->draws}, {"terrain_chunks_drawn", last->chunks}, {"terrain_tris", last->tris}, {"terrain_gpu_mb", last->gpu / 1048576.0}, {"frames_with_holes", holes}};
      check(holes == 0, fmt("%s: no missing terrain chunks in view after warm-up", k.c_str()));
    }
    for (const char* cam : {"ground", "flyover", "home"}) {
      double a = best[std::string("600x600_") + cam], b = best[std::string("1200x2400_") + cam];
      check(b <= a * 1.15 + 0.4, fmt("frame time on 1200 x 2400 holds the 600 x 600 time (%s camera, median of the best block): %.2f ms vs %.2f ms", cam, b, a));
    }
    perf["working_set_mb_1200x2400"] = wsMB();
    perf["terrain_cpu_heights_mb_1200x2400"] = app.terrain.h.size() * 4 / 1048576.0;
    perf["surface_mask_mb_1200x2400"] = app.terrain.surf.size() / 1048576.0 * 2;
    { int b = 0; for (auto x : app.terrain.chunkBodies) b += x != 0xffffffffu; perf["chunk_bodies_1200x2400"] = b; }
    perf["gl_renderer"] = (const char*)glGetString(GL_RENDERER);
    perf["resolution"] = {app.R.width, app.R.height};
    summary["perf"] = perf;
    std::ofstream(outDirEd() / "editor_perf.json") << perf.dump(2);
    gPerfFrame = 0;
    go(3370);
    break;
  }
  case 3370: {   // ================= screenshots for review (selftest_out/editor_*.png)
    Terrain& T = app.terrain;
    int f = gPerfFrame++;
    if (f == 0) { navNewMapSized(app, *blank, "W3 shots", vec2(1200, 2400)); CamPose h = navHomePose(app, navAspect(app)); app.camPos = h.pos; app.camYaw = h.yaw; app.camPitch = h.pitch; }
    if (f == 20) shotQueue.push_back("editor_home_1200x2400");
    if (f == 21) { app.camPos = vec3(-40, T.height(-40, 1050) + 1.8f, 1050); app.camYaw = 0; app.camPitch = -1.5f; }
    if (f == 80) shotQueue.push_back("editor_ground_1200x2400");
    if (f == 81) { app.camPos = vec3(300, T.height(300, -600) + 120, -450); app.camYaw = 0; app.camPitch = -25; }
    if (f == 120) shotQueue.push_back("editor_oblique_1200x2400");
    if (f == 121) {
      if (!app.lib.find("test_trench")) app.lib.loadAll(contentDir() / "tests" / "assets");
      if (const AssetDef* tr = app.lib.find("test_trench")) { uint64_t id = app.placeInstance(tr, vec3(30, 0, 40), 20); app.selected = 0; (void)id; }
      app.camPos = vec3(30, T.height(30, 40) + 5.5f, 49); app.camYaw = 0; app.camPitch = -32;
    }
    if (f == 170) shotQueue.push_back("editor_trench");
    if (f == 171) { app.camPos = vec3(0, 300, 900); app.camYaw = 0; app.camPitch = -30; navState().overview = true; navState().ovScale = 0; }
    if (f == 180) app.uiShot = outDirEd() / "editor_overview.png";
    if (f == 182) { navState().overview = false; app.showNewMap = true; }
    if (f == 190) app.uiShot = outDirEd() / "editor_newmap.png";
    if (f == 192) finish(app);
    break;
  }
  default: finish(app); break;
  }
  (void)dt;
}
