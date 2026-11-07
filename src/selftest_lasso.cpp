// --selftest-lasso (docs/lasso.md): the Creative lasso tool, driven through the same functions the panel calls.
//  * capture: freehand and circle regions select exactly the placements inside (add / subtract, "touching"), never
//    generated furniture, and never change the map hash (the region and the selection are editor state);
//  * objects: a group move keeps the layout (< 1 mm), a turn about the centre lands on the analytic poses, an invalid
//    move is rejected and reverted, duplicate / delete / move / turn are one undo step each and undo exactly;
//  * elevation: plateau +5 m is +5 inside, zero beyond the falloff, monotonic across the ring, no tile cracks,
//    placements ride the ground, the dome rises to the middle, one undo step, exact undo;
//  * water fill: a pit with a notch in a mesa holds water exactly to the lip (checked against a brute-force level
//    search), lassoWaterAt is the level inside and nothing outside, an open hollow is refused, lowering + filling is
//    one undo step, save / reopen keeps the bodies and the hash changes with them;
//  * paint and move land: see their own steps (ground_paint.cpp, lasso_land.cpp).
#include "selftest.h"
#include "mptest.h"
#include "lasso.h"
#include "geom2d.h"
#include "net.h"
#include "terrain_tools.h"
#include "navmeta.h"
#include "grass.h"
#if __has_include("ground_paint.h")
#include "ground_paint.h"
#define LT_PAINT 1
#endif
#if __has_include("lasso_land.h")
#include "lasso_land.h"
#define LT_LAND 1
#endif
#include <cstdarg>
#include <functional>
#include <fstream>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[900];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[lasso] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out" / "lasso"; std::error_code ec; fs::create_directories(p, ec); return p; }

uint64_t place(App& a, const std::string& id, vec3 p, float yaw = 0) {
  const AssetDef* d = a.lib.find(id);
  if (!d) { chk(false, "asset %s exists", id.c_str()); return 0; }
  uint64_t r = a.placeInstance(d, p, yaw);
  if (!r) chk(false, "%s placed at (%.1f, %.1f): %s", id.c_str(), p.x, p.z, a.checkPlacement(d, p, yaw, 0).reason.c_str());
  return r;
}
LassoRegion circleRegion(vec2 c, float r) { LassoRegion g; g.add(geom2d::circle(c, r, 64)); return g; }
LassoRegion rectRegion(vec2 lo, vec2 hi) { LassoRegion g; g.add({lo, vec2(hi.x, lo.y), hi, vec2(lo.x, hi.y)}); return g; }
std::set<uint64_t> selSet(const App& a) { return std::set<uint64_t>(a.selection.begin(), a.selection.end()); }
float borderCrack(const Terrain& t) {
  float worst = 0; const int N = Terrain::kFineN, F = Terrain::kFine;
  for (auto& [k, a] : t.tiles) {
    if (const Terrain::Tile* r = t.tileAt(a.tx + 1, a.tz)) for (int j = 0; j < N; ++j) worst = std::max(worst, std::fabs(a.h[j * N + F] - r->h[j * N]));
    if (const Terrain::Tile* u = t.tileAt(a.tx, a.tz + 1)) for (int i = 0; i < N; ++i) worst = std::max(worst, std::fabs(a.h[F * N + i] - u->h[i]));
  }
  return worst;
}
// brute-force reference for the spill height: the highest level L at which the water below L connected to the seed
// does not reach the search window's edge (bisection over L), on the same fine lattice
float referenceLip(const App& app, vec2 seed, vec2 lo, vec2 hi) {
  const Terrain& Tr = app.terrain; float fs = Tr.fineSpacing();
  int i0 = (int)std::floor((lo.x - Tr.org.x) / fs), j0 = (int)std::floor((lo.y - Tr.org.y) / fs);
  int W = (int)std::ceil((hi.x - lo.x) / fs) + 1, H = (int)std::ceil((hi.y - lo.y) / fs) + 1;
  std::vector<float> h((size_t)W * H);
  for (int z = 0; z < H; ++z) for (int x = 0; x < W; ++x) h[(size_t)z * W + x] = Tr.height(Tr.org.x + (i0 + x) * fs, Tr.org.y + (j0 + z) * fs);
  int sx = (int)std::lround((seed.x - Tr.org.x) / fs) - i0, sz = (int)std::lround((seed.y - Tr.org.y) / fs) - j0;
  auto closed = [&](float L) {
    std::vector<char> m((size_t)W * H, 0); std::vector<int> st{sz * W + sx}; m[(size_t)sz * W + sx] = 1;
    while (!st.empty()) {
      int k = st.back(); st.pop_back(); int x = k % W, z = k / W;
      if (x == 0 || z == 0 || x == W - 1 || z == H - 1) return false;
      for (int d = 0; d < 4; ++d) {
        int nx = x + (d == 0) - (d == 1), nz = z + (d == 2) - (d == 3); int n = nz * W + nx;
        if (!m[n] && h[n] < L) { m[n] = 1; st.push_back(n); }
      }
    }
    return true;
  };
  float a = h[(size_t)sz * W + sx], b = a + 60.0f;
  for (int it = 0; it < 40; ++it) { float mid = 0.5f * (a + b); if (closed(mid)) a = mid; else b = mid; }
  return a;
}

// ---------------------------------------------------------------- shared state
uint64_t gCot = 0, gFarm = 0, gTree = 0, gCrate = 0;
std::map<uint64_t, Instance> gPose;
size_t gUndo = 0;
std::string gHash;
vec2 gMesa{120, -110}; float gLip = 0;

std::function<void(App&)> gProbe;   // runs after the next frame is rendered (pixel probes)
void paintSteps(); void landSteps();

void snapshotPoses(App& a) { gPose.clear(); for (auto& i : a.doc.instances) gPose[i.id] = i; }

void build() {
  push("blank map", [](App& app, float) {
    const TemplateInfo* t = app.findTemplate("blank_slate");
    if (!chk(t != nullptr, "template blank_slate exists")) return true;
    app.newMap(*t, "Lasso selftest");
    lassoRegion().clear(); app.selection.clear(); app.selected = 0;
    return true;
  });
  // ================================================================ capture
  push("capture: freehand, circle, add, subtract, touching", [](App& app, float) {
    gCot = place(app, "house_stone_cottage", vec3(-80, 0, 0), 0);
    gFarm = place(app, "house_farmhouse_2story", vec3(-20, 0, 0), 30);
    gTree = place(app, "tree_oak", vec3(-50, 0, 30), 0);
    gCrate = place(app, "crate_mixed_supply", vec3(-50, 0, -20), 0);
    for (int k = 0; k < 4; ++k) app.syncRuntime();
    gHash = Net::mapHash(app);
    // a freehand-like loop around the cottage and the crate (not the farmhouse, not the tree)
    LassoRegion r; r.add({{-100, -30}, {-60, -32}, {-40, -28}, {-38, -10}, {-62, 12}, {-98, 14}});
    lassoSelectObjects(app, r, 0);
    chk(selSet(app) == std::set<uint64_t>{gCot, gCrate}, "freehand loop selects the cottage and the crate (%zu selected)", app.selection.size());
    lassoSelectObjects(app, circleRegion(vec2(-50, 30), 6), 1);
    chk(selSet(app) == std::set<uint64_t>{gCot, gCrate, gTree}, "Shift + circle adds the tree (%zu selected)", app.selection.size());
    lassoSelectObjects(app, circleRegion(vec2(-50, -20), 4), 2);
    chk(selSet(app) == std::set<uint64_t>{gCot, gTree}, "Alt + circle removes the crate (%zu selected)", app.selection.size());
    // a region over one corner of the farmhouse only
    const AssetDef* fa = app.lib.find("house_farmhouse_2story");
    vec3 corner = app.doc.find(gFarm)->pos + yawQuat(30) * vec3(fa->footprint.x * 0.5f, 0, fa->footprint.y * 0.5f);
    LassoRegion cr = circleRegion(vec2(corner.x, corner.z), 1.5f);
    lassoSelectObjects(app, cr, 0, false);
    bool pivotOnly = app.selection.empty();
    lassoSelectObjects(app, cr, 0, true);
    chk(pivotOnly && selSet(app) == std::set<uint64_t>{gFarm}, "a region over a footprint corner: pivot rule %s, touching selects the farmhouse", pivotOnly ? "skips it" : "SELECTS it");
    // generated furniture never joins a group
    lassoSelectObjects(app, rectRegion(vec2(-200), vec2(200)), 0);
    int furn = 0, furnSel = 0;
    for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("furnish")) { ++furn; if (std::count(app.selection.begin(), app.selection.end(), i.id)) ++furnSel; }
    chk(furnSel == 0 && app.selection.size() == app.doc.instances.size() - furn, "a map-wide region selects the %zu placements and none of the %d generated furniture pieces", app.selection.size(), furn);
    chk(Net::mapHash(app) == gHash, "selecting changes nothing in the map (hash %s)", gHash.c_str());
    return true;
  });
  // ================================================================ objects
  push("objects: group move keeps the layout", [](App& app, float) {
    lassoSelectObjects(app, rectRegion(vec2(-100, -30), vec2(-40, 40)), 0);   // cottage, tree, crate
    chk(app.selection.size() == 3, "three placements in the group (%zu)", app.selection.size());
    snapshotPoses(app); gUndo = app.undoStack.size();
    vec2 d(25, 60);
    bool ok = lassoGroupMove(app, d, 0);
    float worst = 0;
    for (uint64_t id : app.selection) { Instance* i = app.doc.find(id); vec2 e = vec2(i->pos.x, i->pos.z) - vec2(gPose[id].pos.x, gPose[id].pos.z) - d; worst = std::max(worst, glm::length(e)); }
    chk(ok && worst < 1e-3f, "moved by (25, 60): every member within %.4f mm of its offset", worst * 1000);
    chk(app.undoStack.size() == gUndo + 1, "one undo step (%zu -> %zu)", gUndo, app.undoStack.size());
    for (uint64_t id : app.selection) { Instance* i = app.doc.find(id); if (std::fabs(i->pos.y - app.checkPlacement(app.assetFor(*i), i->pos, i->yaw, i->id).y) > 0.02f && !app.assetFor(*i)->tree.on) { chk(false, "member #%llu seated on the ground", (unsigned long long)id); break; } }
    app.undo();
    worst = 0; for (auto& [id, p] : gPose) if (Instance* i = app.doc.find(id)) worst = std::max(worst, glm::length(i->pos - p.pos));
    chk(worst < 1e-4f && app.undoStack.size() == gUndo, "Ctrl+Z puts the group back (%.5f m)", worst);
    app.redo();
    return true;
  });
  push("objects: turn about the centre", [](App& app, float) {
    snapshotPoses(app); gUndo = app.undoStack.size();
    vec3 c = lassoGroupCentre(app);
    bool ok = lassoGroupMove(app, vec2(0), 90);
    float worst = 0, yawErr = 0;
    for (uint64_t id : app.selection) {
      Instance* i = app.doc.find(id); vec3 want = c + yawQuat(90) * (gPose[id].pos - c);
      worst = std::max(worst, glm::length(vec2(i->pos.x - want.x, i->pos.z - want.z)));
      yawErr = std::max(yawErr, std::fabs(wrapDeg(i->yaw - wrapDeg(gPose[id].yaw + 90))));
    }
    chk(ok && worst < 1e-3f && yawErr < 1e-3f, "90 deg about (%.1f, %.1f): positions within %.4f mm, yaws within %.4f deg", c.x, c.z, worst * 1000, yawErr);
    chk(app.undoStack.size() == gUndo + 1, "one undo step");
    return true;
  });
  push("objects: invalid move is rejected", [](App& app, float) {
    snapshotPoses(app); gUndo = app.undoStack.size();
    Instance* cot = app.doc.find(gCot); Instance* farm = app.doc.find(gFarm);
    vec2 d(farm->pos.x - cot->pos.x, farm->pos.z - cot->pos.z);   // the cottage onto the farmhouse
    bool ok = lassoGroupMove(app, d, 0);
    float worst = 0; for (auto& [id, p] : gPose) if (Instance* i = app.doc.find(id)) worst = std::max(worst, glm::length(i->pos - p.pos) + std::fabs(i->yaw - p.yaw));
    chk(!ok && worst < 1e-5f && app.undoStack.size() == gUndo, "rejected (%s), everything back in place, no undo step", lassoGroupReason().c_str());
    return true;
  });
  push("objects: duplicate and delete", [](App& app, float) {
    size_t n0 = app.doc.instances.size(); gUndo = app.undoStack.size();
    std::vector<uint64_t> orig = app.selection;
    int made = lassoDuplicateGroup(app);
    bool fresh = true; for (uint64_t id : app.selection) if (std::count(orig.begin(), orig.end(), id)) fresh = false;
    chk(made == 3 && app.doc.instances.size() == n0 + 3 && fresh && app.undoStack.size() == gUndo + 1, "duplicate: 3 copies, they become the selection, one undo step");
    // the copies keep the layout
    vec3 o0 = app.doc.find(orig[0])->pos, o1 = app.doc.find(orig[1])->pos, c0 = app.doc.find(app.selection[0])->pos, c1 = app.doc.find(app.selection[1])->pos;
    chk(glm::length(vec2((o1 - o0).x, (o1 - o0).z) - vec2((c1 - c0).x, (c1 - c0).z)) < 1e-3f, "the copies keep the group's layout");
    int del = lassoDeleteGroup(app);
    chk(del == 3 && app.doc.instances.size() == n0 && app.undoStack.size() == gUndo + 2 && app.selection.empty(), "delete: the 3 copies go in one undo step");
    app.undo();
    chk(app.doc.instances.size() == n0 + 3, "undo the delete: copies back (%zu)", app.doc.instances.size());
    app.undo();
    chk(app.doc.instances.size() == n0 && app.undoStack.size() == gUndo, "undo the duplicate: %zu placements", app.doc.instances.size());
    // Del / Ctrl+D route to the group
    app.selection = orig; app.selected = orig[0];
    app.duplicateSelected();
    chk(app.doc.instances.size() == n0 + 3, "Ctrl+D (duplicateSelected) copies the whole group");
    app.deleteSelected();
    chk(app.doc.instances.size() == n0, "Del (deleteSelected) deletes the whole group");
    app.selection = orig; app.selected = orig[0];
    return true;
  });
  // ================================================================ elevation
  push("elevation: plateau raise with falloff", [](App& app, float) {
    app.selection.clear(); app.selected = 0;
    vec2 c(100, 100); float R = 20, F = 10;
    uint64_t tree = place(app, "tree_ash", vec3(c.x + 5, 0, c.y - 4), 0);
    float treeY = tree ? app.doc.find(tree)->pos.y : 0;
    std::vector<vec2> inPts, outPts, ray;
    for (int k = 0; k < 24; ++k) { float a = k * 0.2618f; inPts.push_back(c + vec2(std::cos(a), std::sin(a)) * (R - 1.0f) * ((k % 3 + 1) / 3.0f)); outPts.push_back(c + vec2(std::cos(a), std::sin(a)) * (R + F + 1.0f + (k % 4))); }
    for (int k = 0; k <= 60; ++k) ray.push_back(c + vec2(1, 0.3f) / glm::length(vec2(1, 0.3f)) * (R - 2 + k * 0.25f));
    auto H = [&](const std::vector<vec2>& p) { std::vector<float> h; for (auto& q : p) h.push_back(app.terrain.height(q.x, q.y)); return h; };
    auto h0i = H(inPts), h0o = H(outPts), h0r = H(ray);
    gUndo = app.undoStack.size();
    ElevParams p; p.amount = 5; p.falloff = F; p.profile = 0;
    bool ok = lassoElevate(app, circleRegion(c, R), p);
    auto h1i = H(inPts), h1o = H(outPts), h1r = H(ray);
    float ei = 0, eo = 0; for (size_t k = 0; k < inPts.size(); ++k) ei = std::max(ei, std::fabs(h1i[k] - h0i[k] - 5)); for (size_t k = 0; k < outPts.size(); ++k) eo = std::max(eo, std::fabs(h1o[k] - h0o[k]));
    chk(ok && ei < 0.01f, "+5 m inside the region (worst %.4f m off)", ei);
    chk(eo < 1e-3f, "unchanged beyond the falloff ring (worst %.5f m)", eo);
    bool mono = true; float prev = 1e9f; for (size_t k = 0; k < ray.size(); ++k) { float d = h1r[k] - h0r[k]; if (d > prev + 1e-3f) mono = false; prev = d; }
    chk(mono, "the rise falls monotonically across the %.0f m ring", F);
    chk(borderCrack(app.terrain) < 1e-4f, "no cracks between tiles (%.6f m)", borderCrack(app.terrain));
    if (tree) chk(std::fabs(app.doc.find(tree)->pos.y - treeY - 5) < 0.05f, "a tree inside rides up with the ground (%.2f m)", app.doc.find(tree)->pos.y - treeY);
    chk(app.undoStack.size() == gUndo + 1 && !app.doc.terrainSculpt.empty(), "one undo step, the sculpt is in the map (%zu tiles)", app.doc.terrainSculpt.size());
    app.undo();
    auto h2i = H(inPts); float eu = 0; for (size_t k = 0; k < inPts.size(); ++k) eu = std::max(eu, std::fabs(h2i[k] - h0i[k]));
    chk(eu < 1e-3f && app.undoStack.size() == gUndo, "undo restores the ground exactly (%.5f m)", eu);
    // dome: rises to the middle
    ElevParams d = p; d.profile = 1; d.amount = 6;
    float hc0 = app.terrain.height(c.x, c.y), he0 = app.terrain.height(c.x + R, c.y);
    lassoElevate(app, circleRegion(c, R), d);
    float dc = app.terrain.height(c.x, c.y) - hc0, de = app.terrain.height(c.x + R, c.y) - he0;
    chk(dc > 5.8f && de > 0.5f && de < dc - 2.0f, "dome: +%.2f m in the middle, +%.2f m at the edge", dc, de);
    app.undo();
    // live preview + cancel leaves nothing behind
    gUndo = app.undoStack.size();
    lassoElevBegin(app, circleRegion(c, R)); ElevParams q = p; q.amount = -3; lassoElevPreview(app, q); q.amount = 7; lassoElevPreview(app, q);
    float mid = app.terrain.height(c.x, c.y) - hc0;
    lassoElevCancel(app);
    chk(std::fabs(mid - 7) < 0.01f && std::fabs(app.terrain.height(c.x, c.y) - hc0) < 1e-3f && app.undoStack.size() == gUndo, "preview -3 then +7 shows +%.2f m; Cancel restores the ground, no undo step", mid);
    return true;
  });
  // ================================================================ water
  push("water: pit with a notch fills to the lip", [](App& app, float) {
    vec2 c = gMesa;
    ElevParams mesa; mesa.amount = 6; mesa.falloff = 4;
    lassoElevate(app, circleRegion(c, 30), mesa);                      // a mesa
    ElevParams notch; notch.amount = -2; notch.falloff = 0.6f;
    lassoElevate(app, rectRegion(c + vec2(8, -1.5f), c + vec2(40, 1.5f)), notch);   // a notch through its east rim
    gHash = Net::mapHash(app);
    float centre0 = app.terrain.height(c.x, c.y);
    size_t nb = app.doc.waterBodies.size(); gUndo = app.undoStack.size();
    ElevParams pit; pit.amount = -4; pit.falloff = 3;
    WaterFill f; LassoRegion pr = circleRegion(c, 12);
    bool ok = lassoElevate(app, pr, pit, true, &f);
    chk(ok && f.ok && app.doc.waterBodies.size() == nb + 1, "lower + fill: one water body (%s)", f.ok ? "ok" : f.why.c_str());
    chk(app.undoStack.size() == gUndo + 1, "lowering and filling are one undo step");
    if (!f.ok) return true;
    gLip = f.level;
    float ref = referenceLip(app, f.seed, c - vec2(110), c + vec2(110));
    chk(std::fabs(f.level - ref) < 0.02f, "lip %.3f m = brute-force spill level %.3f m (diff %.1f mm)", f.level, ref, std::fabs(f.level - ref) * 1000);
    float top = app.terrain.height(c.x - 20, c.y), notchFloor = app.terrain.height(c.x + 20, c.y);
    chk(f.level < top - 1.0f && std::fabs(f.level - notchFloor) < 0.6f, "the water spills through the notch: level %.2f, mesa top %.2f, notch floor %.2f", f.level, top, notchFloor);
    chk(std::fabs(lassoWaterAt(app, f.seed.x, f.seed.y) - f.level) < 1e-4f && lassoWaterAt(app, c.x, c.y) == f.level, "lassoWaterAt is the level in the pit");
    chk(lassoWaterAt(app, c.x - 20, c.y) < -1e8f && lassoWaterAt(app, c.x + 60, c.y) < -1e8f, "no water on the mesa top or outside it");
    chk(geom2d::inside(app.doc.waterBodies.back().outline, f.seed), "the outline (%zu points) contains the seed", app.doc.waterBodies.back().outline.size());
    chk(Net::mapHash(app) != gHash, "the water body changes the map hash");
    // open ground is refused
    WaterFill open = lassoComputeFill(app, geom2d::circle(c, 25));   // includes the mesa top: the deepest point is the pit (filled) ...
    WaterFill hill = lassoComputeFill(app, geom2d::circle(vec2(-150, 150), 8));
    note("fill on the open blank-slate ground: %s", hill.ok ? "filled" : hill.why.c_str());
    (void)open;
    LassoRegion top2 = circleRegion(c + vec2(-22, 0), 3);
    WaterFill onTop = lassoComputeFill(app, top2.shapes[0].poly);
    chk(!onTop.ok, "a fill on the mesa top is refused (%s)", onTop.why.c_str());
    app.undo();
    chk(app.doc.waterBodies.size() == nb && std::fabs(app.terrain.height(c.x, c.y) - centre0) < 1e-3f, "undo removes the water and the pit together");
    app.redo();
    chk(app.doc.waterBodies.size() == nb + 1, "redo brings them back");
    return true;
  });
  push("water: save / reopen", [](App& app, float) {
    fs::path p = outDir() / "lasso_map.json";
    std::string err;
    std::string h0 = Net::mapHash(app);
    bool sv = saveMap(p, app.doc, err);
    MapDoc d; bool ld = loadMap(p, d, err);
    bool same = ld && d.waterBodies.size() == app.doc.waterBodies.size();
    for (size_t k = 0; same && k < d.waterBodies.size(); ++k) same = std::fabs(d.waterBodies[k].level - app.doc.waterBodies[k].level) < 1e-3f && d.waterBodies[k].outline.size() == app.doc.waterBodies[k].outline.size();
    chk(sv && same, "the water bodies survive save / reopen (%zu)", d.waterBodies.size());
    // two PCs opening the same file agree on the hash
    App* a = &app; MapDoc keep = a->doc; a->doc = d; std::string h1 = Net::mapHash(*a);
    MapDoc d2; loadMap(p, d2, err); a->doc = d2; std::string h2 = Net::mapHash(*a); a->doc = keep;
    chk(h1 == h2, "two loads of the saved map hash the same (%s)", h1.c_str());
    (void)h0;
    return true;
  });
#ifdef LT_PAINT
  paintSteps();
#endif
#ifdef LT_LAND
  landSteps();
#endif
  push("screenshot", [](App& app, float t) {
    static int ph = 0;
    if (ph == 0) { app.camPos = vec3(gMesa.x - 45, app.terrain.height(gMesa.x, gMesa.y) + 28, gMesa.y + 45); app.camYaw = -45; app.camPitch = -28; app.tool = Tool::Lasso; lassoRegion() = circleRegion(gMesa, 12); ph = 1; return false; }
    if (ph == 1 && t > 1.0f) { pendingShot = outDir() / "lasso_water.png"; ph = 2; return false; }
    return ph == 2 && t > 1.3f;
  });
}
#ifdef LT_PAINT
// ================================================================ paint
const float kPaintZ = 220.0f;
vec2 patchC(int t) { return vec2(-132.0f + t * 12.0f, kPaintZ); }
void paintSteps() {
  push("paint: every type, eraser, blend, save", [](App& app, float) {
    groundPaintSync(app);
    const auto& T = groundPaintTypes();
    chk((int)T.size() == kPaintTypes, "%d ground types (grass + 12)", (int)T.size());
    std::string h0 = Net::mapHash(app);
    bool allIn = true, allOut = true, steps = true; float worstIn = 1, worstOut = 0;
    for (int t = 1; t < kPaintTypes; ++t) {
      vec2 c = patchC(t);
      size_t u0 = app.undoStack.size();
      int n = groundPaintRegion(app, rectRegion(c - vec2(4), c + vec2(4)), t, 1.0f, 1.0f);
      if (n <= 0 || app.undoStack.size() != u0 + 1) steps = false;
      int ty = 0; float w = groundPaintAt(app, c.x, c.y, &ty);
      if (ty != t || w < 0.99f) allIn = false;
      worstIn = std::min(worstIn, ty == t ? w : 0.0f);
      float wo = groundPaintAt(app, c.x, c.y + 7.5f);
      if (wo > 0.01f) allOut = false;
      worstOut = std::max(worstOut, wo);
    }
    chk(steps, "each type paints in one undo step");
    chk(allIn, "inside every patch the painted type has weight %.3f or more", worstIn);
    chk(allOut, "3.5 m beyond the patch edge nothing is painted (worst %.3f)", worstOut);
    chk(Net::mapHash(app) != h0, "the paint changes the map hash");
    // a soft edge: the weight falls across the feather
    vec2 c(60, kPaintZ);
    groundPaintRegion(app, rectRegion(c - vec2(10), c + vec2(10)), 5, 1.0f, 6.0f);
    float wIn = groundPaintAt(app, c.x, c.y), wEdge = groundPaintAt(app, c.x + 10, c.y), wOut = groundPaintAt(app, c.x + 16, c.y);
    chk(wIn > 0.99f && wEdge > 0.1f && wEdge < 0.9f && wOut < 0.01f, "6 m edge blend: %.2f inside, %.2f on the outline, %.2f beyond", wIn, wEdge, wOut);
    // two types meet: both show across the seam (two layers per texel)
    groundPaintRegion(app, rectRegion(c + vec2(0, -10), c + vec2(20, 10)), 4, 1.0f, 6.0f);
    float best = 0, bg = 0, bs = 0;
    for (int k = 0; k <= 48; ++k) { float w[kPaintTypes]; groundPaintWeights(app.doc, c.x - 4 + k * 0.25f, c.y, w); if (std::min(w[4], w[5]) > best) { best = std::min(w[4], w[5]); bg = w[4]; bs = w[5]; } }
    chk(best > 0.2f, "where gravel is painted over sand with a soft edge both show (best mix: gravel %.2f, sand %.2f)", bg, bs);
    // the eraser
    vec2 d = patchC(2);
    groundPaintRegion(app, rectRegion(d - vec2(5), d + vec2(5)), 0, 1.0f, 0.0f);
    chk(groundPaintAt(app, d.x, d.y) < 0.01f, "painting Grass erases (dirt weight %.3f)", groundPaintAt(app, d.x, d.y));
    app.undo();
    chk(groundPaintAt(app, d.x, d.y) > 0.99f, "undo the erase: dirt is back");
    // grass, nature, navmesh, footsteps follow the paint
    groundPaintSync(app);
    vec2 cb = patchC(6), dg = patchC(8), ma = patchC(1);
    int grow = 0; for (int k = 0; k < 40; ++k) grow += app.grass.density(cb.x - 3 + (k % 8) * 0.8f, cb.y - 3 + (k / 8) * 1.4f, app.terrain, {});
    int growFree = 0; for (int k = 0; k < 40; ++k) growFree += app.grass.density(-132.0f + (k % 8) * 0.8f, kPaintZ + 30 + (k / 8) * 1.4f, app.terrain, {});
    chk(grow == 0 && growFree > 30, "no pasture grass on cobbles (%d of 40 points), unpainted ground keeps it (%d of 40)", grow, growFree);
    chk(std::fabs(groundPaintGrass(dg.x, dg.y) - T[8].grass) < 0.01f, "dry grass keeps %.0f %% of the sward", groundPaintGrass(dg.x, dg.y) * 100);
    chk(groundPaintBlocksNature(cb.x, cb.y) && !groundPaintBlocksNature(dg.x, dg.y) && !groundPaintBlocksNature(-132, kPaintZ + 30), "nature patches skip cobbles, grow on dry grass and grass");
    chk(groundPaintNavArea(ma.x, ma.y) == (int)NavArea::Marsh && groundPaintNavArea(-132, kPaintZ + 30) == -1, "marsh paint is a Marsh navmesh area");
    chk(groundPaintStep(patchC(4).x, kPaintZ) == "step_gravel", "gravel has its footstep event");
    // save / reopen, byte for byte
    fs::path p = outDir() / "lasso_paint.json"; std::string err;
    bool sv = saveMap(p, app.doc, err); MapDoc m; bool ld = loadMap(p, m, err);
    chk(sv && ld && m.groundPaint == app.doc.groundPaint && m.groundPaint.size() > 0, "%zu paint chunks survive save / reopen byte for byte", app.doc.groundPaint.size());
    chk(app.R.shaderErrors == 0, "every shader compiled (terrain with paintApply)");
    return true;
  });
  push("paint: rendered patches", [](App& app, float t) {
    static int ph = 0; static std::vector<vec3> col;
    if (ph == 0) {
      app.tool = Tool::Select; lassoRegion().clear();
      float y = app.terrain.height(-54, kPaintZ);
      app.camPos = vec3(-54, y + 85, kPaintZ + 14); app.camYaw = 0; app.camPitch = -80;
      ph = 1; return false;
    }
    if (ph == 1 && t > 1.5f) {
      ph = 2;
      gProbe = [](App& a) {
        std::vector<uint8_t> px; int w, h; a.R.readPixels(px, w, h);
        mat4 vp = a.projM * a.viewM;
        col.clear();
        for (int k = 0; k < kPaintTypes; ++k) {
          vec2 c = k == 0 ? vec2(-54, kPaintZ + 14) : patchC(k);
          vec4 q = vp * vec4(c.x, a.terrain.height(c.x, c.y), c.y, 1); vec3 n = vec3(q) / q.w;
          int sx = (int)((n.x * 0.5f + 0.5f) * w), sy = (int)((n.y * 0.5f + 0.5f) * h);
          vec3 s(0); int cnt = 0;
          for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            int x = sx + dx, yy = sy + dy; if (x < 0 || yy < 0 || x >= w || yy >= h) continue;
            const uint8_t* p = &px[((size_t)yy * w + x) * 4]; s += vec3(p[0], p[1], p[2]) / 255.0f; ++cnt;
          }
          col.push_back(cnt ? s / (float)cnt : vec3(-1));
        }
      };
      pendingShot = outDir() / "lasso_paint_types.png";
      return false;
    }
    if (ph == 2 && t > 2.0f) {
      int dark = 0, sameAsGrass = 0, pairs = 0, distinct = 0; float minPair = 9;
      for (int k = 1; k < (int)col.size(); ++k) {
        if (col[k].x < 0 || glm::dot(col[k], vec3(0.3f, 0.59f, 0.11f)) < 0.008f) ++dark;
        if (glm::length(col[k] - col[0]) < 0.02f) ++sameAsGrass;
        for (int l = k + 1; l < (int)col.size(); ++l) { float d = glm::length(col[k] - col[l]); minPair = std::min(minPair, d); ++pairs; if (d > 0.01f) ++distinct; }
      }
      for (int k = 0; k < (int)col.size(); ++k) note("type %2d %-12s rgb %.3f %.3f %.3f", k, groundPaintTypes()[k].id.c_str(), col[k].x, col[k].y, col[k].z);
      chk((int)col.size() == kPaintTypes && dark == 0, "every painted patch renders (none black or off screen)");
      chk(sameAsGrass == 0, "every painted type looks different from the grass around it");
      chk(distinct >= pairs * 9 / 10, "%d of %d type pairs look distinct (closest pair %.3f)", distinct, pairs, minPair);
      return true;
    }
    return false;
  });
}
#endif
#ifdef LT_LAND
// ================================================================ move land
void landSteps() {
  push("move land: a hill with a tree, paint and a pond", [](App& app, float) {
    vec2 A(-150, -150), d(60, 6); float R = 14, F = 6;
    ElevParams hill; hill.amount = 5; hill.falloff = 3; hill.profile = 1;
    lassoElevate(app, circleRegion(A, 9), hill);                                  // a rounded hill
    uint64_t tree = place(app, "tree_oak", vec3(A.x + 1, 0, A.y + 1), 0);
#ifdef LT_PAINT
    groundPaintRegion(app, circleRegion(A + vec2(-4, 4), 3), 5, 1.0f, 0.5f);   // sand on its flank
#endif
    WaterBody pond; pond.id = app.doc.newId(); pond.seed = A + vec2(6, -6); pond.level = app.terrain.height(pond.seed.x, pond.seed.y) + 0.2f;
    pond.outline = geom2d::circle(pond.seed, 1.5f, 12); app.doc.waterBodies.push_back(pond);
    LassoRegion reg = circleRegion(A, R);
    std::vector<vec2> pts, ptsD;
    for (int k = 0; k < 40; ++k) { float a = k * 0.61f, rr = (k % 5) * 2.5f; pts.push_back(A + rr * vec2(std::cos(a), std::sin(a))); ptsD.push_back(pts.back() + d); }
    std::vector<float> rel0 = lassoLandRelief(app, reg, F, pts);
    float peak0 = *std::max_element(rel0.begin(), rel0.end());
    std::vector<float> hBefore; for (auto& p : pts) hBefore.push_back(app.terrain.height(p.x, p.y));
    vec3 treeP = tree ? app.doc.find(tree)->pos : vec3(0);
    float treeRise = tree ? treeP.y - app.terrain.height(treeP.x, treeP.z) : 0;
    gUndo = app.undoStack.size();
    LandMoveReport rep = lassoMoveLand(app, reg, d, 0, F);
    chk(rep.ok && app.undoStack.size() == gUndo + 1, "moved (%s): %d placements, %d paint texels, %d water bodies, %d tiles, one undo step", rep.ok ? "ok" : rep.why.c_str(), rep.objects, rep.texels, rep.water, rep.tiles);
    std::vector<float> rel1 = lassoLandRelief(app, reg.moved(d, 0, A), F, ptsD);
    float worst = 0; for (size_t k = 0; k < pts.size(); ++k) worst = std::max(worst, std::fabs(rel1[k] - rel0[k]));
    chk(peak0 > 3.0f && worst < 0.02f, "the hill arrives with its shape: relief %.2f m, worst difference %.1f mm", peak0, worst * 1000);
    std::vector<float> relS = lassoLandRelief(app, reg, F, pts);
    float left = 0; for (float v : relS) left = std::max(left, std::fabs(v));
    float slope = 0; for (auto& p : pts) { vec3 n = app.terrain.normal(p.x, p.y); slope = std::max(slope, std::acos(std::clamp(n.y, -1.0f, 1.0f))); }
    chk(left < 0.05f && degrees(slope) < 12.0f, "the old spot is smooth: relief left %.3f m, steepest %.1f deg", left, degrees(slope));
    if (tree) {
      Instance* t = app.doc.find(tree);
      vec2 want = vec2(treeP.x, treeP.z) + d;
      chk(glm::length(vec2(t->pos.x, t->pos.z) - want) < 1e-3f && std::fabs(t->pos.y - app.terrain.height(t->pos.x, t->pos.z) - treeRise) < 0.05f, "the tree rides along and stays on the hill top");
    }
#ifdef LT_PAINT
    chk(groundPaintAt(app, A.x - 4 + d.x, A.y + 4 + d.y) > 0.9f && groundPaintAt(app, A.x - 4, A.y + 4) < 0.01f, "the sand moves with the land");
#endif
    WaterBody& pw = app.doc.waterBodies.back();
    chk(glm::length(pw.seed - (pond.seed + d)) < 1e-3f && std::fabs(pw.level - app.terrain.height(pw.seed.x, pw.seed.y) - 0.2f) < 0.05f, "the pond moves and keeps its depth");
    app.undo();
    float back = 0; for (size_t k = 0; k < pts.size(); ++k) back = std::max(back, std::fabs(app.terrain.height(pts[k].x, pts[k].y) - hBefore[k]));
    chk(back < 1e-3f && (!tree || glm::length(app.doc.find(tree)->pos - treeP) < 1e-3f), "undo puts the land, the tree, the paint and the pond back (%.4f m)", back);
    // a turn: the tree lands at the analytic spot
    LandMoveReport r2 = lassoMoveLand(app, reg, vec2(0), 90, F);
    if (tree && r2.ok) { vec3 want = vec3(A.x, 0, A.y) + yawQuat(90) * vec3(treeP.x - A.x, 0, treeP.z - A.y); Instance* t = app.doc.find(tree); chk(glm::length(vec2(t->pos.x - want.x, t->pos.z - want.z)) < 1e-3f, "a 90 deg turn about the region centre carries the tree to (%.2f, %.2f)", want.x, want.z); }
    else chk(r2.ok, "turning the land: %s", r2.why.c_str());
    app.undo();
    // a move onto a building is refused and changes nothing
    uint64_t h = place(app, "house_stone_cottage", vec3(A.x + 60, 0, A.y + 30), 0);
    std::vector<float> hb; for (auto& p : pts) hb.push_back(app.terrain.height(p.x, p.y));
    size_t u1 = app.undoStack.size();
    LandMoveReport r3 = lassoMoveLand(app, reg, vec2(60, 30) - vec2(1, 1), 0, F);
    float same = 0; for (size_t k = 0; k < pts.size(); ++k) same = std::max(same, std::fabs(app.terrain.height(pts[k].x, pts[k].y) - hb[k]));
    chk(h && !r3.ok && app.undoStack.size() == u1 && same < 1e-4f, "moving the tree onto a cottage is refused (%s); nothing changed", r3.why.c_str());
    return true;
  });
}
#endif
}   // namespace

bool lassoSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-lasso") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (gProbe) { auto f = gProbe; gProbe = nullptr; f(app); } if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("lasso"); build(); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = false;
    try { done = seq[cur].fn(app, st); } catch (std::exception& e) { T->check(false, "step '" + seq[cur].name + "' threw: " + e.what()); done = true; }
    st += dt; total += dt;
    if (done) { logf("[lasso] step done: %s (%.1f s)", seq[cur].name.c_str(), total); ++cur; st = 0; }
    if (total > 600) { T->check(false, "lasso test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
