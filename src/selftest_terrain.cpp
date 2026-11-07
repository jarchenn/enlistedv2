// --selftest-terrain (V8, docs/terrain-tools.md): terrain sculpting, Generate hills and Beautify.
//  * brushes: every mode changes the ground the way it says, a stroke sequence is deterministic, tile borders never
//    crack, sculpted ground (raised or lowered) has no disturbed-soil shading, colliders follow, one undo step per
//    stroke, save / reopen restores it, craters on sculpted ground persist only the crater, dab cost on a 600 m map;
//  * placements in a stroke re-seat (trees on their pivot, buildings on the footprint rule);
//  * Generate hills: relief everywhere except footprints / paths, placements re-seat, undo;
//  * outer ring: the terrain grows by whole tiles, crater / sculpt tile keys remap, the playable area keeps its exact
//    ground (also after save / reopen and undo);
//  * Beautify on Normandy Village Assault (battle1): scatter counts per role, nothing inside footprints, paths,
//    objectives or spawns, decor buildings beyond the border only, existing placements untouched, the ring present,
//    one undo step, save / reopen; eye-level frame time before / after (gated at 16.7 ms like --selftest-maps).
#include "selftest.h"
#include "mptest.h"
#include "terrain_tools.h"
#include "terrain_chunks.h"
#include "physics.h"
#include "scenario.h"
#include <cstdarg>
#include <functional>
#include <chrono>
#include <fstream>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[900];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[terrain] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
json perf = json::object();

uint64_t hashSculpt(const Terrain& t) {
  uint64_t h = 1469598103934665603ull;
  for (auto& [k, v] : t.sculpt) { h = (h ^ (uint64_t)k) * 1099511628211ull; for (float f : v) { uint32_t b; std::memcpy(&b, &f, 4); h = (h ^ b) * 1099511628211ull; } }
  return h;
}
void commitAll(App& app) { std::vector<int> k; for (auto& [key, v] : app.terrain.sculpt) k.push_back(key); app.doc.terrainSculpt.clear(); terrainCommitSculpt(app, k); }
void clearSculpt(App& app) { app.doc.terrainSculpt.clear(); app.doc.terrainDeltas.clear(); app.resetGround(); }
std::vector<vec2> samplePts(float hx, float hz, int n, uint32_t seed) {
  std::vector<vec2> p; uint32_t s = seed;
  auto r = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
  for (int k = 0; k < n; ++k) p.push_back(vec2((r() * 2 - 1) * hx, (r() * 2 - 1) * hz));
  return p;
}
std::vector<float> heightsAt(const App& app, const std::vector<vec2>& p) { std::vector<float> h; for (auto& q : p) h.push_back(app.terrain.height(q.x, q.y)); return h; }
float maxDiff(const std::vector<float>& a, const std::vector<float>& b) { float m = 0; for (size_t k = 0; k < a.size() && k < b.size(); ++k) m = std::max(m, std::fabs(a[k] - b[k])); return m; }
float borderCrack(const Terrain& t) {   // shared border samples of neighbouring tiles must agree
  float worst = 0; const int N = Terrain::kFineN, F = Terrain::kFine;
  for (auto& [k, a] : t.tiles) {
    if (const Terrain::Tile* r = t.tileAt(a.tx + 1, a.tz)) for (int j = 0; j < N; ++j) worst = std::max(worst, std::fabs(a.h[j * N + F] - r->h[j * N]));
    if (const Terrain::Tile* u = t.tileAt(a.tx, a.tz + 1)) for (int i = 0; i < N; ++i) worst = std::max(worst, std::fabs(a.h[F * N + i] - u->h[i]));
  }
  return worst;
}
const TemplateInfo* tpl(App& app, const std::string& id) { const TemplateInfo* t = app.findTemplate(id); if (!t) chk(false, "template %s exists", id.c_str()); return t; }
uint64_t place(App& a, const std::string& id, vec3 p, float yaw = 0) {
  const AssetDef* d = a.lib.find(id);
  if (!d) { chk(false, "asset %s exists", id.c_str()); return 0; }
  uint64_t r = a.placeInstance(d, p, yaw);
  if (!r) chk(false, "%s placed at (%.1f, %.1f): %s", id.c_str(), p.x, p.z, a.checkPlacement(d, p, yaw, 0).reason.c_str());
  return r;
}
float seatRule(const App& app, const Instance& i) {   // the editor's footprint rule (max of nine samples)
  const AssetDef* a = app.assetFor(i); vec2 hf = a->footprint * 0.5f; quat q = yawQuat(i.yaw);
  const vec3 loc[] = {{0, 0, 0}, {-hf.x, 0, -hf.y}, {hf.x, 0, -hf.y}, {-hf.x, 0, hf.y}, {hf.x, 0, hf.y}, {0, 0, -hf.y}, {0, 0, hf.y}, {-hf.x, 0, 0}, {hf.x, 0, 0}};
  float top = -1e9f; for (auto& l : loc) { vec3 w = i.pos + q * l; top = std::max(top, app.terrain.height(w.x, w.z)); }
  return top;
}
bool insideObb(vec2 p, vec2 c, vec2 h, float yaw, float pad) {
  vec3 l = glm::inverse(yawQuat(yaw)) * vec3(p.x - c.x, 0, p.y - c.y);
  return std::fabs(l.x) < h.x + pad && std::fabs(l.z) < h.y + pad;
}

// eye-level frame time over a set of views (editor camera, like the gated --selftest-maps views)
struct View { std::string name; vec3 pos; float yaw, pitch; };
struct Perf { double ms = 0, worst = 0; int frames = 0; };
std::vector<View> gViews; std::map<std::string, Perf> gPerf; double gLast = 0;
void pushViews(const std::string& tag, std::function<std::vector<View>(App&)> make) {
  auto vi = std::make_shared<size_t>(0);
  push(tag + ": views", [=](App& app, float) { gViews = make(app); *vi = 0; return true; });
  push(tag + ": frame time", [=](App& app, float s) {
    if (*vi >= gViews.size()) {
      double worst = 0; for (auto& v : gViews) worst = std::max(worst, gPerf[tag + "/" + v.name].ms);
      perf[tag] = {{"worstAvgMs", worst}};
      return true;
    }
    View& v = gViews[*vi];
    static float t0 = 0; static size_t lastVi = ~size_t(0); static std::string lastTag;
    if (lastVi != *vi || lastTag != tag) { lastVi = *vi; lastTag = tag; t0 = s; app.camPos = v.pos; app.camYaw = v.yaw; app.camPitch = v.pitch; gLast = 0; }
    float e = s - t0;
    double tn = nowS();
    Perf& p = gPerf[tag + "/" + v.name];
    if (e > 1.5f && e < 3.5f && gLast > 0) { double ms = (tn - gLast) * 1000.0; p.ms = (p.ms * p.frames + ms) / (p.frames + 1); p.worst = std::max(p.worst, ms); ++p.frames; }
    gLast = tn;
    if (e >= 3.6f) {
      note("%s view %s: %.2f ms avg (worst %.1f) over %d frames, %d draws", tag.c_str(), v.name.c_str(), p.ms, p.worst, p.frames, app.R.stats.draws);
      perf[tag + "/" + v.name] = {{"avgMs", p.ms}, {"worstMs", p.worst}, {"frames", p.frames}, {"draws", app.R.stats.draws}};
      ++*vi;
    }
    return false;
  });
}
std::vector<View> battleViews(App& app) {
  std::vector<View> v;
  Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
  for (auto& o : s.objectives) {
    vec3 back = -attackForward(o.attackYaw);
    vec3 e = o.pos + back * 45.0f; e.y = app.terrain.height(e.x, e.z) + 1.7f;
    v.push_back({std::string("obj_") + o.letter() + "_eye", e, o.attackYaw, 0.0f});
  }
  const Terrain& t = app.terrain;
  // toward the border ring from inside the playable area (north, east)
  vec3 n(0, 0, t.buildHalfZ - 25); n.y = t.height(n.x, n.z) + 1.7f; v.push_back({"north_edge_eye", n, 180.0f, 0.0f});
  vec3 ea(t.buildHalfX - 25, 0, 30); ea.y = t.height(ea.x, ea.z) + 1.7f; v.push_back({"east_edge_eye", ea, -90.0f, 0.0f});
  return v;
}

// ---------------------------------------------------------------- state shared by the steps
BrushParams gB; vec2 gC[BR_COUNT]; float gBefore[BR_COUNT];
std::vector<vec2> gPts; std::vector<float> gH0;
uint64_t gTree = 0, gHouse = 0, gPath = 0;
struct InstRec { vec3 pos; float yaw; std::string asset; };
std::map<uint64_t, InstRec> gInst; size_t gUndo0 = 0, gN0 = 0; int gTilesX0 = 0;
BeautifyReport gRep;

void build(App& app) {
  // ================================================================ brushes on Blank Slate
  push("blank map", [](App& app, float) { const TemplateInfo* t = tpl(app, "blank_slate"); if (t) app.newMap(*t, "Terrain selftest"); return true; });
  push("every brush mode", [](App& app, float) {
    clearSculpt(app);
    for (int m = 0; m < BR_COUNT; ++m) {
      gC[m] = vec2(-150.0f + m * 50.0f, 60.0f);
      gBefore[m] = app.terrain.height(gC[m].x, gC[m].y);
      BrushParams p; p.mode = m; p.radius = 12; p.strength = 3; p.falloff = 0.5f; p.target = gBefore[m] + 2.5f; p.scale = 14; p.seed = 7;
      if (m == BR_SMOOTH) { BrushParams r = p; r.mode = BR_RAISE; r.radius = 3; r.falloff = 0; for (int k = 0; k < 20; ++k) terrainBrushDab(app, r, gC[m], 1 / 30.0f); gBefore[m] = app.terrain.height(gC[m].x, gC[m].y); }
      for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, gC[m], 1 / 30.0f);
    }
    auto H = [&](int m, vec2 o = vec2(0)) { return app.terrain.height(gC[m].x + o.x, gC[m].y + o.y); };
    float raise = H(BR_RAISE) - gBefore[BR_RAISE], lower = H(BR_LOWER) - gBefore[BR_LOWER];
    chk(std::fabs(raise - 3.0f) < 0.05f, "Raise: +%.3f m at the centre after 1 s at 3 m/s (expect +3.0)", raise);
    chk(std::fabs(lower + 3.0f) < 0.05f, "Lower: %.3f m at the centre after 1 s at 3 m/s (expect -3.0)", lower);
    float rim = H(BR_RAISE, vec2(11.9f, 0)) - app.terrain.baseHeight(gC[BR_RAISE].x + 11.9f, gC[BR_RAISE].y), out = H(BR_RAISE, vec2(13.0f, 0)) - app.terrain.baseHeight(gC[BR_RAISE].x + 13.0f, gC[BR_RAISE].y);
    chk(rim < 0.1f && std::fabs(out) < 1e-5f, "falloff: %.3f m just inside the rim, %.5f m outside the radius", rim, out);
    chk(std::fabs(H(BR_FLATTEN) - (gBefore[BR_FLATTEN] + 2.5f)) < 0.15f, "Flatten: centre at %.2f m (target %.2f)", H(BR_FLATTEN), gBefore[BR_FLATTEN] + 2.5f);
    float sm = H(BR_SMOOTH) - gBefore[BR_SMOOTH];
    chk(sm < -0.5f, "Smooth: a 2 m spike lowered by %.2f m", -sm);
    float hmin = 1e9f, hmax = -1e9f; for (int k = 0; k < 40; ++k) { float d = H(BR_HILLS, vec2(-10 + k * 0.5f, 3)) - app.terrain.baseHeight(gC[BR_HILLS].x - 10 + k * 0.5f, gC[BR_HILLS].y + 3); hmin = std::min(hmin, d); hmax = std::max(hmax, d); }
    chk(hmax - hmin > 0.5f, "Rolling hills: relief %.2f .. %.2f m across the stamp", hmin, hmax);
    // terrace: flat treads -> many samples with near-zero slope and some steep ones along a radius
    int flat = 0, steep = 0; for (int k = 0; k < 44; ++k) { float x = k * 0.25f, d = H(BR_TERRACE, vec2(x + 0.25f, 0)) - H(BR_TERRACE, vec2(x, 0)); if (std::fabs(d) < 0.01f) ++flat; if (std::fabs(d) > 0.08f) ++steep; }
    chk(flat >= 8 && steep >= 2, "Cliff / terrace: %d flat and %d steep 25 cm steps along the radius", flat, steep);
    float dirt = 0; for (int m = 0; m < BR_COUNT; ++m) dirt = std::max(dirt, app.terrain.dirtAt(gC[m].x, gC[m].y));
    chk(dirt == 0.0f, "sculpted ground (raised and lowered) has no disturbed-soil shading: max dirt %.3f", dirt);
    chk(borderCrack(app.terrain) < 1e-4f, "tile borders agree after sculpting (worst %.6f m)", borderCrack(app.terrain));
    commitAll(app);
    return true;
  });
  push("determinism", [](App& app, float) {
    auto run = [&] {
      clearSculpt(app);
      for (int m = 0; m < BR_COUNT; ++m) {
        BrushParams p; p.mode = m; p.radius = 9 + m; p.strength = 2.5f; p.target = 4; p.seed = 3;
        for (int k = 0; k < 24; ++k) terrainBrushDab(app, p, vec2(-120.0f + m * 45 + k * 0.4f, -80.0f + k * 0.2f), 1 / 60.0f);
      }
      return hashSculpt(app.terrain);
    };
    uint64_t a = run(), b = run();
    chk(a == b && !app.terrain.sculpt.empty(), "the same strokes give bit-identical sculpt layers (%zu tiles, hash %016llx / %016llx)", app.terrain.sculpt.size(), (unsigned long long)a, (unsigned long long)b);
    clearSculpt(app);
    return true;
  });
  push("colliders follow", [](App& app, float s) {
    if (s == 0) { BrushParams p; p.radius = 10; p.strength = 4; for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, vec2(40, -40), 1 / 30.0f); commitAll(app); return false; }
    if (s < 0.3f) return false;   // a frame or two: the frame syncs Terrain::physDirty
    terrainPhysicsSync(app.terrain);
    float th = app.terrain.height(40, -40), t = 0; uint64_t ud = 0;
    bool hit = Physics::castRay(vec3(40, th + 20, -40), vec3(0, -1, 0), 60, t, ud);
    chk(hit && std::fabs((th + 20 - t) - th) < 0.05f, "the ground collider follows the sculpted height (ray hit %.3f, ground %.3f)", hit ? th + 20 - t : -999.0f, th);
    return true;
  });
  push("one undo step per stroke", [](App& app, float) {
    clearSculpt(app); app.undoStack.clear(); app.redoStack.clear();
    vec2 c(-30, 120); float h0 = app.terrain.height(c.x, c.y);
    BrushParams p; p.radius = 8; p.strength = 3;
    terrainStrokeBegin(app, p, vec3(c.x, h0, c.y));
    for (int k = 0; k < 20; ++k) { auto keys = terrainBrushDab(app, p, c + vec2(k * 0.5f, 0), 1 / 30.0f); terrainCommitSculpt(app, keys); }
    terrainStrokeEnd(app);
    float h1 = app.terrain.height(c.x, c.y);
    size_t n = app.undoStack.size();
    app.undo(); float h2 = app.terrain.height(c.x, c.y);
    app.redo(); float h3 = app.terrain.height(c.x, c.y);
    chk(n == 1 && h1 > h0 + 1.0f && std::fabs(h2 - h0) < 1e-4f && std::fabs(h3 - h1) < 1e-4f, "stroke = %zu undo step(s): %.2f -> %.2f m, undo %.2f, redo %.2f", n, h0, h1, h2, h3);
    // a click that changes nothing (zero radius area outside the map) leaves no empty undo step
    size_t n1 = app.undoStack.size();
    terrainStrokeBegin(app, p, vec3(5000, 0, 5000)); terrainBrushDab(app, p, vec2(5000, 5000), 1 / 30.0f); terrainStrokeEnd(app);
    chk(app.undoStack.size() == n1, "an empty stroke adds no undo step");
    return true;
  });
  push("placements re-seat", [](App& app, float) {
    clearSculpt(app);
    gTree = place(app, "tree_oak", vec3(-100, 0, -120)); gHouse = place(app, "house_stone_cottage", vec3(-80, 0, -120), 30); gPath = place(app, "path_dirt", vec3(-60, 0, -120));
    Instance* h = app.doc.find(gHouse); float hy0 = h ? h->pos.y : 0;
    BrushParams p; p.radius = 30; p.strength = 2; p.falloff = 0.8f;
    for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, vec2(-85, -115), 1 / 30.0f);
    p.mode = BR_SMOOTH; for (int k = 0; k < 10; ++k) terrainBrushDab(app, p, vec2(-85, -115), 1 / 30.0f);
    commitAll(app);
    Instance* tr = app.doc.find(gTree); h = app.doc.find(gHouse);
    float te = tr ? std::fabs(tr->pos.y - app.terrain.height(tr->pos.x, tr->pos.z)) : 9, he = h ? std::fabs(h->pos.y - seatRule(app, *h)) : 9;
    chk(tr && h && te < 0.01f && he < 0.01f && h->pos.y > hy0 + 0.5f, "tree on its pivot (err %.3f m), house on its footprint rule (err %.3f m, rose %.2f m)", te, he, h ? h->pos.y - hy0 : 0.0f);
    PlaceCheck c = app.checkPlacement(app.assetFor(*h), h->pos, h->yaw, h->id);
    chk(c.ok && std::fabs(c.y - h->pos.y) < 0.01f, "the re-seated house is a valid placement at its height (%s)", c.reason.c_str());
    return true;
  });
  push("craters on sculpted ground", [](App& app, float) {
    vec2 c(-85, -100);
    float h0 = app.terrain.height(c.x, c.y);
    app.craterAt(vec3(c.x, h0, c.y), 2.0f, 0.6f, false);
    auto d = app.terrain.deltas(); float m = 0; for (auto& [k, v] : d) for (float f : v) m = std::max(m, std::fabs(f));
    chk(!d.empty() && m < 0.65f && app.terrain.dirtAt(c.x, c.y) > 0.5f, "crater deltas exclude the sculpt (max |delta| %.2f m over %zu tiles), crater soil shaded (dirt %.2f)", m, d.size(), app.terrain.dirtAt(c.x, c.y));
    app.doc.terrainDeltas = d; app.doc.terrainTemplateVersion = app.doc.templateVersion;
    return true;
  });
  push("save and reopen", [](App& app, float) {
    BrushParams p; p.mode = BR_LOWER; p.radius = 9; p.strength = 3; for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, vec2(60, 60), 1 / 30.0f);
    commitAll(app);
    gPts = samplePts(140, 140, 400, 11); for (vec2 q : {vec2(60, 60), vec2(-85, -115), vec2(-85, -100), vec2(-80, -120)}) gPts.push_back(q);
    gH0 = heightsAt(app, gPts);
    float dirtLow = app.terrain.dirtAt(60, 60);
    size_t ns = app.doc.terrainSculpt.size(), ni = app.doc.instances.size();
    fs::path f = userMapsDir() / "selftest_terrain.nmap.json";
    bool ok = app.saveMapTo(f) && app.openMap(f);
    auto h1 = heightsAt(app, gPts);
    chk(ok && app.doc.terrainSculpt.size() == ns && app.doc.instances.size() == ni && maxDiff(gH0, h1) < 2e-3f, "sculpt + craters survive save / reopen: %zu tiles, worst height error %.4f m over %zu points", app.doc.terrainSculpt.size(), maxDiff(gH0, h1), gPts.size());
    chk(app.terrain.dirtAt(60, 60) == 0.0f && dirtLow == 0.0f && app.terrain.dirtAt(-85, -100) > 0.5f, "after reopening, sculpted hollows stay grass (dirt %.2f) and the crater stays soil (%.2f)", app.terrain.dirtAt(60, 60), app.terrain.dirtAt(-85, -100));
    return true;
  });
  push("dab cost on a 600 m map", [](App& app, float) {
    BrushParams p; p.radius = 25; p.strength = 2;
    double t0 = nowS(); int n = 30;
    for (int k = 0; k < n; ++k) { auto keys = terrainBrushDab(app, p, vec2(150 + k * 0.8f, -150), 1 / 60.0f); terrainCommitSculpt(app, keys); terrainPhysicsSync(app.terrain); }
    double ms = (nowS() - t0) * 1000.0 / n;
    p.radius = 60; double t1 = nowS();
    for (int k = 0; k < 10; ++k) { auto keys = terrainBrushDab(app, p, vec2(-150, 150), 1 / 60.0f); terrainCommitSculpt(app, keys); terrainPhysicsSync(app.terrain); }
    double ms60 = (nowS() - t1) * 1000.0 / 10;
    perf["dabMs_r25"] = ms; perf["dabMs_r60"] = ms60;
    chk(ms < 12.0 && ms60 < 45.0, "interactive: %.2f ms per 25 m dab, %.2f ms per 60 m dab (mesh + collider + re-seat)", ms, ms60);
    return true;
  });
  // ================================================================ Generate hills
  push("generate hills", [](App& app, float) {
    const TemplateInfo* t = tpl(app, "blank_slate"); if (!t) return true;
    app.newMap(*t, "Hills selftest");
    gHouse = place(app, "house_farmhouse_2story", vec3(50, 0, 50), 20); gPath = place(app, "path_dirt", vec3(80, 0, 50), 90); gTree = place(app, "tree_ash", vec3(-90, 0, -60));
    Instance* h = app.doc.find(gHouse); float hy = h ? h->pos.y : 0;
    std::vector<vec2> foot, path;
    for (int k = 0; k < 60; ++k) { float u = (k % 10) / 9.0f - 0.5f, v = (k / 10) / 5.0f - 0.5f; vec3 o = yawQuat(20) * vec3(u * 12.2f, 0, v * 9.2f); foot.push_back(vec2(50 + o.x, 50 + o.z)); }
    for (int k = 0; k < 20; ++k) { vec3 o = yawQuat(90) * vec3(0, 0, (k / 19.0f - 0.5f) * 10.0f); path.push_back(vec2(80 + o.x, 50 + o.z)); }
    auto grid = samplePts(290, 290, 600, 5);
    auto f0 = heightsAt(app, foot), p0 = heightsAt(app, path), g0 = heightsAt(app, grid);
    size_t u0 = app.undoStack.size();
    HillsParams hp; hp.amp = 6; hp.scale = 90; hp.seed = 4;
    terrainGenerateHills(app, hp);
    auto f1 = heightsAt(app, foot), p1 = heightsAt(app, path), g1 = heightsAt(app, grid);
    h = app.doc.find(gHouse); Instance* tr = app.doc.find(gTree);
    chk(maxDiff(g0, g1) > 2.0f, "hills raise / lower the open ground (largest change %.2f m over 600 points)", maxDiff(g0, g1));
    chk(maxDiff(f0, f1) < 1e-4f && maxDiff(p0, p1) < 1e-4f && h && std::fabs(h->pos.y - hy) < 1e-4f, "footprint and path ground unchanged (%.5f / %.5f m), the house stays at %.2f m", maxDiff(f0, f1), maxDiff(p0, p1), h ? h->pos.y : 0.0f);
    chk(tr && std::fabs(tr->pos.y - app.terrain.height(tr->pos.x, tr->pos.z)) < 0.01f, "the tree re-seats on the new ground");
    chk(app.undoStack.size() == u0 + 1, "Generate hills is one undo step");
    app.undo();
    chk(maxDiff(g0, heightsAt(app, grid)) < 1e-4f, "undo restores the ground (worst %.5f m)", maxDiff(g0, heightsAt(app, grid)));
    return true;
  });
  // ================================================================ outer ring and the tile-key remap
  push("outer ring remaps tiles", [](App& app, float) {
    const TemplateInfo* t = tpl(app, "blank_slate"); if (!t) return true;
    app.newMap(*t, "Ring selftest");
    BrushParams p; p.radius = 14; p.strength = 3; for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, vec2(100, 100), 1 / 30.0f);
    p.radius = 6; p.mode = BR_LOWER; for (int k = 0; k < 30; ++k) terrainBrushDab(app, p, vec2(-290, 280), 1 / 30.0f);   // near the border
    commitAll(app);
    app.craterAt(vec3(-50, app.terrain.height(-50, 20), 20), 2.0f, 0.6f, false);
    app.doc.terrainDeltas = app.terrain.deltas(); app.doc.terrainTemplateVersion = app.doc.templateVersion;
    gPts = samplePts(300, 300, 800, 9); for (vec2 q : {vec2(100, 100), vec2(-290, 280), vec2(-50, 20)}) gPts.push_back(q);
    gH0 = heightsAt(app, gPts);
    gTilesX0 = app.terrain.tilesX(); float m0 = app.terrain.margin; size_t ns = app.doc.terrainSculpt.size(), nd = app.doc.terrainDeltas.size();
    int k0 = app.doc.terrainSculpt.begin()->first;
    gUndo0 = app.undoStack.size();
    BeautifyParams bp; bp.rocks = bp.vegetation = bp.groundCover = bp.water = bp.buildings = 0; bp.inside = false; bp.ring = true;
    app.pushUndo(); gRep = beautifyRun(app, bp);
    auto h1 = heightsAt(app, gPts);
    chk(gRep.ringTiles == 6 && app.terrain.tilesX() == gTilesX0 + 12 && app.terrain.margin > m0 + 90, "ring: %d tiles each side, %d -> %d tiles across, margin %.0f -> %.0f m", gRep.ringTiles, gTilesX0, app.terrain.tilesX(), m0, app.terrain.margin);
    chk(app.doc.terrainSculpt.size() == ns && app.doc.terrainDeltas.size() == nd && !app.doc.terrainSculpt.count(k0), "tile keys remapped (%zu sculpt, %zu crater tiles; first key %d -> %d)", app.doc.terrainSculpt.size(), app.doc.terrainDeltas.size(), k0, app.doc.terrainSculpt.begin()->first);
    chk(maxDiff(gH0, h1) < 1e-4f, "the playable area keeps its exact ground (worst %.6f m over %zu points incl. sculpt and crater)", maxDiff(gH0, h1), gPts.size());
    float outerH = app.terrain.height(0, app.terrain.buildHalfZ + 170);
    chk(app.terrain.inTerrain(0, app.terrain.buildHalfZ + 170) && std::isfinite(outerH), "the ring extends the terrain to %.0f m beyond the border (ground %.1f m there)", app.terrain.margin, outerH);
    fs::path f = userMapsDir() / "selftest_terrain_ring.nmap.json";
    bool ok = app.saveMapTo(f) && app.openMap(f);
    chk(ok && app.terrain.ringTiles() == 6 && maxDiff(gH0, heightsAt(app, gPts)) < 2e-3f, "ring + remapped layers survive save / reopen (worst %.4f m)", maxDiff(gH0, heightsAt(app, gPts)));
    return true;
  });
  push("ring undo", [](App& app, float) {
    const TemplateInfo* t = tpl(app, "blank_slate"); if (!t) return true;
    app.newMap(*t, "Ring undo selftest");
    BrushParams p; p.radius = 10; p.strength = 3; for (int k = 0; k < 20; ++k) terrainBrushDab(app, p, vec2(-200, -60), 1 / 30.0f);
    commitAll(app);
    auto pts = samplePts(300, 300, 300, 21); auto h0 = heightsAt(app, pts); int tx0 = app.terrain.tilesX();
    BeautifyParams bp; bp.rocks = bp.vegetation = bp.groundCover = bp.water = 0; bp.buildings = 0.5f; bp.inside = false;
    app.pushUndo(); beautifyRun(app, bp);
    int tx1 = app.terrain.tilesX(); size_t n1 = app.doc.instances.size();
    app.undo();
    chk(tx1 == tx0 + 12 && app.terrain.tilesX() == tx0 && app.terrain.ringTiles() == 0 && app.doc.instances.empty() && maxDiff(h0, heightsAt(app, pts)) < 1e-4f,
        "one undo removes the ring (%d -> %d -> %d tiles) and its %zu placements, sculpt intact (worst %.5f m)", tx0, tx1, app.terrain.tilesX(), n1, maxDiff(h0, heightsAt(app, pts)));
    return true;
  });
  // ================================================================ Beautify on battle1
  push("battle1", [](App& app, float s) {
    if (s == 0) { const TemplateInfo* t = tpl(app, "battle1"); if (t) app.newMap(*t, "Beautify selftest"); return false; }
    return s > 1.0f;
  });
  pushViews("before", battleViews);
  push("beautify battle1", [](App& app, float) {
    gInst.clear(); for (auto& i : app.doc.instances) gInst[i.id] = {i.pos, i.yaw, i.assetId};
    gN0 = app.doc.instances.size();
    const Terrain& t = app.terrain;
    gPts = samplePts(t.buildHalfX, t.buildHalfZ, 1500, 77); gH0 = heightsAt(app, gPts);
    gTilesX0 = t.tilesX(); gUndo0 = app.undoStack.size();
    BeautifyParams bp;   // dialog defaults
    double t0 = nowS();
    app.pushUndo(); gRep = beautifyRun(app, bp);
    double secs = nowS() - t0; perf["beautifySeconds"] = secs;
    int scatter = 0; std::string roles; for (auto& [k, v] : gRep.placed) { scatter += v; roles += " " + k + "=" + std::to_string(v); }
    note("Beautify battle1 in %.2f s: %d scatter (%s), %d decor buildings, %d hedges, %d ring trees, %zu -> %zu instances", secs, scatter, roles.c_str(), gRep.decor, gRep.hedges, gRep.ringTrees, gN0, app.doc.instances.size());
    if (!gRep.missing.empty()) { std::string m; for (auto& x : gRep.missing) m += " " + x; note("roles without assets in this catalogue (skipped):%s", m.c_str()); }
    perf["beautify"] = {{"scatter", gRep.placed}, {"decor", gRep.decor}, {"hedges", gRep.hedges}, {"ringTrees", gRep.ringTrees}, {"instancesBefore", gN0}, {"instancesAfter", app.doc.instances.size()}};
    chk(scatter >= 80 && gRep.placed["trees"] > 5 && gRep.placed["boulders"] > 5 && gRep.placed["groundcover"] > 10, "scatter placed: %d (%s)", scatter, roles.c_str());
    chk(gRep.decor >= 40 && gRep.hedges >= 10 && gRep.ringTrees >= 10, "border ring dressed: %d decor buildings, %d hedges, %d trees", gRep.decor, gRep.hedges, gRep.ringTrees);
    chk(gRep.ringTiles == 6 && t.tilesX() == gTilesX0 + 12, "the outer ring is present (%d tiles, margin %.0f m)", gRep.ringTiles, t.margin);
    chk(maxDiff(gH0, heightsAt(app, gPts)) < 1e-4f, "the playable area keeps its exact ground (worst %.6f m over %zu points)", maxDiff(gH0, heightsAt(app, gPts)), gPts.size());
    // existing placements untouched
    int moved = 0, missing = 0;
    for (auto& [id, r] : gInst) { Instance* i = app.doc.find(id); if (!i) { ++missing; continue; } if (i->pos != r.pos || i->yaw != r.yaw || i->assetId != r.asset) ++moved; }
    chk(moved == 0 && missing == 0, "existing placements untouched: %d moved, %d missing of %zu", moved, missing, gInst.size());
    // nothing new inside footprints, paths, objectives or spawns; decor only beyond the border
    Scenario sc = Scenario::fromJson(app.doc.extra.value("scenario", json()));
    int inFoot = 0, inPath = 0, inObj = 0, inSpawn = 0, decorInside = 0, untagged = 0, insideNew = 0; std::string ex;
    for (auto& i : app.doc.instances) {
      if (gInst.count(i.id)) continue;
      bool beyond = std::fabs(i.pos.x) > t.buildHalfX || std::fabs(i.pos.z) > t.buildHalfZ;
      if (!i.extra.is_object() || !i.extra.value("beautify", false) || (beyond && !isDecor(i))) ++untagged;
      if (isDecor(i) && !beyond) ++decorInside;
      if (beyond) continue;
      ++insideNew;
      vec2 p(i.pos.x, i.pos.z);
      for (auto& [id, r] : gInst) {
        const AssetDef* b = app.lib.find(r.asset); if (!b) continue;
        if (b->nature.kind != NatureDef::NONE || b->tree.on || b->category == "Nature" || b->category == "Vegetation") continue;
        if (b->path.on) { if (insideObb(p, vec2(r.pos.x, r.pos.z), b->footprint * 0.5f, r.yaw, 0)) { ++inPath; if (ex.size() < 160) ex += " " + i.assetId + "@" + b->id; } continue; }
        if (insideObb(p, vec2(r.pos.x, r.pos.z), b->footprint * 0.5f, r.yaw, 0)) { ++inFoot; if (ex.size() < 160) ex += " " + i.assetId + "@" + b->id; }
      }
      for (auto& o : sc.objectives) if (glm::length(p - vec2(o.pos.x, o.pos.z)) < o.radius) ++inObj;
      for (auto& sp : sc.spawns) if (glm::length(p - vec2(sp.pos.x, sp.pos.z)) < 10.0f) ++inSpawn;
    }
    chk(inFoot == 0 && inPath == 0 && inObj == 0 && inSpawn == 0, "%d new placements inside the playable area: %d in footprints, %d on paths, %d in objectives, %d at spawns%s", insideNew, inFoot, inPath, inObj, inSpawn, ex.c_str());
    chk(decorInside == 0 && untagged == 0, "tags: %d decor inside the border, %d untagged / mis-tagged placements", decorInside, untagged);
    // a decor building can be moved in the editor beyond the border (checkPlacement exception); others cannot
    const Instance* d = nullptr; for (auto& i : app.doc.instances) if (isDecor(i) && app.assetFor(i)->enterable) { d = &i; break; }
    if (d) {
      PlaceCheck c = app.checkPlacement(app.assetFor(*d), d->pos + vec3(0.5f, 0, 0), d->yaw, d->id);
      PlaceCheck c2 = app.checkPlacement(app.assetFor(*d), d->pos, d->yaw, 0);
      chk(c.ok && !c2.ok, "decor building %s editable beyond the border (%s); a new placement there is refused (%s)", d->assetId.c_str(), c.reason.c_str(), c2.reason.c_str());
    } else chk(false, "a decor building exists");
    chk(app.undoStack.size() == gUndo0 + 1, "Beautify is one undo step");
    return true;
  });
  push("ring screenshot", [](App& app, float s) {
    if (s == 0) { const Terrain& t = app.terrain; app.camPos = vec3(-t.buildHalfX + 40, t.height(-t.buildHalfX + 40, t.buildHalfZ - 60) + 45, t.buildHalfZ - 60); app.camYaw = 150; app.camPitch = -18; return false; }
    if (s > 2.5f && pendingShot.empty()) { pendingShot = outDir() / "terrain_beautify_ring.png"; return true; }
    return false;
  });
  pushViews("after", battleViews);
  push("frame time gate", [](App& app, float) {
    double before = perf.value("before", json::object()).value("worstAvgMs", 0.0), after = perf.value("after", json::object()).value("worstAvgMs", 0.0);
    // 60 FPS, or (when the unbeautified map is already over budget on this GPU) Beautify adds at most 0.5 ms
    chk(after > 0 && (after < 16.7 || after <= before + 0.5), "eye-level views after Beautify: worst view %.2f ms, before %.2f ms (+%.2f ms; gate < 16.7 ms or <= +0.5 ms over an already over-budget baseline)", after, before, after - before);
    return true;
  });
  push("beautified map saves and reopens", [](App& app, float s) {
    static size_t n = 0; static int dec = 0; static bool ok = false;
    if (s == 0) {
      n = app.doc.instances.size(); dec = 0; for (auto& i : app.doc.instances) dec += isDecor(i);
      fs::path f = userMapsDir() / "selftest_terrain_beautify.nmap.json";
      ok = app.saveMapTo(f) && app.openMap(f);
      return false;
    }
    if (s < 0.3f) return false;   // the frame re-cuts the trenches (navSyncCuts) after opening
    int dec2 = 0; for (auto& i : app.doc.instances) dec2 += isDecor(i);
    chk(ok && app.doc.instances.size() == n && dec2 == dec && app.terrain.ringTiles() == 6 && maxDiff(gH0, heightsAt(app, gPts)) < 2e-3f, "save / reopen: %zu instances, %d decor, ring %d tiles", app.doc.instances.size(), dec2, app.terrain.ringTiles());
    app.undoStack.clear();
    return true;
  });
  push("undo Beautify", [](App& app, float) {
    // reopened copy has no history: apply on top, then undo back to it
    size_t n = app.doc.instances.size();
    BeautifyParams bp; bp.seed = 9; app.pushUndo(); beautifyRun(app, bp);
    size_t n2 = app.doc.instances.size();
    app.undo();
    chk(app.doc.instances.size() == n && n2 > 0, "undo after a re-run restores the map (%zu -> %zu -> %zu instances)", n, n2, app.doc.instances.size());
    int removed = beautifyRemove(app);
    chk(app.doc.instances.size() == gN0 && removed > 0, "Remove earlier Beautify takes the map back to its %zu authored placements (%d removed)", gN0, removed);
    return true;
  });
}
}

bool terrainSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-terrain") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("terrain"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = false;
    try { done = seq[cur].fn(app, st); } catch (std::exception& e) { T->check(false, "step '" + seq[cur].name + "' threw: " + e.what()); done = true; }
    st += dt; total += dt;
    if (done) { logf("[terrain] step done: %s (%.1f s)", seq[cur].name.c_str(), total); ++cur; st = 0; }
    if (total > 600) { T->check(false, "terrain test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    std::ofstream(outDir() / "terrain_perf.json") << perf.dump(2);
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
