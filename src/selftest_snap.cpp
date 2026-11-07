// --selftest-snap (V6, docs/plan-visual-physics-overhaul.md): seamless snapping.
//  * a trench chain (straight -> zig-zag -> corner) placed by snapping from a cursor near the free ends: sockets meet
//    exactly (no gap, no overlap: the pieces' geometry ends on the joint planes), one base height, equal cut floors
//    either side of every joint, the joined ends' caps gone from mesh and collision (a ray along the floor passes),
//    the free ends closed; snapped neighbours pass the overlap test; moving a piece re-snaps; undo un-joins
//  * paths join end to end through the same API (snapPath), hedgerow banks (bcg_*) chain and corner, the legacy
//    hedgerow_bocage joins only its own kind
//  * every piece of a chain draws with one world-space texture origin (kMainVS uSnapAp)
// Also crushStep(): the --selftest-nature step "a house placed on shrubs crushes them and flattens the ground".
#include "selftest.h"
#include "mptest.h"
#include "snap.h"
#include "editor_nav.h"
#include "physics.h"
#include <cstdarg>
#include <functional>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[700];
bool chk(MpTest& t, bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return t.check(ok, B); }
#define CHK(ok, ...) chk(*T, ok, __VA_ARGS__)
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }

struct Ids { uint64_t straight = 0, zig = 0, corner = 0, path1 = 0, path2 = 0, h1 = 0, h2 = 0, h3 = 0, b1 = 0, b2 = 0; } ids;

// snap `asset` from a cursor `off` metres past instance `onto`'s socket `sock`, at heading `yaw`; then place it
uint64_t snapPlace(App& app, const char* asset, uint64_t onto, int sock, vec3 off, float yaw, bool* snapped = nullptr) {
  const AssetDef* a = app.lib.find(asset);
  Instance* o = app.doc.find(onto);
  if (!a || !o) { CHK(false, "%s / #%llu present", asset, (unsigned long long)onto); return 0; }
  auto ws = snapWorldSockets(app, *o);
  vec3 pos = ws[sock].p + off; float y = yaw;
  bool s = snapPlacement(app, a, pos, y, 0);
  if (snapped) *snapped = s;
  uint64_t id = app.placeInstance(a, pos, y);
  app.syncRuntime(); snapSync(app); app.syncRuntime(); navSyncCuts(app);
  return id;
}
float sockGap(App& app, uint64_t a, int sa, uint64_t b, int sb, float* dirDot = nullptr, bool xzOnly = false) {
  auto wa = snapWorldSockets(app, *app.doc.find(a)), wb = snapWorldSockets(app, *app.doc.find(b));
  if (dirDot) *dirDot = glm::dot(wa[sa].dir, wb[sb].dir);
  vec3 d = wa[sa].p - wb[sb].p; if (xzOnly) d.y = 0;   // paths drape: their ends differ in height by the ground
  return glm::length(d);
}
// how far piece i's drawn geometry reaches past (> 0) / short of (< 0) joint plane (point J, normal d), within `lat`
// metres of the joint sideways: returns (max along d)
float reach(App& app, uint64_t id, vec3 J, vec3 d, float lat) {
  Instance* in = app.doc.find(id); const AssetDef* a = app.assetFor(*in);
  std::vector<vec3> tris; snapMesh(app, a, snapMask(*in), &tris);
  mat4 M = app.instanceMatrix(*in);
  float mx = -1e9f;
  for (auto& v : tris) {
    vec3 w = vec3(M * vec4(v, 1)), r = w - J;
    vec3 side = r - d * glm::dot(r, d);
    if (glm::length(vec2(side.x, side.z)) > lat) continue;
    mx = std::max(mx, glm::dot(r, d));
  }
  return mx;
}

void build(App& app) {
  (void)app;
  push("setup: Blank Slate", [](App& a, float t) {
    if (t == 0) { const TemplateInfo* tp = a.findTemplate("blank_slate"); CHK(tp && a.newMap(*tp, "Snap test"), "Blank Slate map"); }
    return t > 0.3f;
  });
  push("assets declare sockets", [](App& a, float) {
    const char* want[] = {"aw_trench_straight", "aw_trench_zigzag", "aw_trench_corner", "aw_trench_entry", "bcg_hedgerow_straight", "bcg_hedgerow_corner", "bcg_hedgerow_gap",
                          "bcg_sunken_lane", "bch_at_ditch", "hedgerow_bocage", "path_dirt", "path_gravel", "path_concrete"};
    for (const char* id : want) {
      const AssetDef* d = a.lib.find(id);
      auto s = d ? snapSockets(a, d) : std::vector<SnapSocket>{};
      CHK(d && !s.empty(), "%s: %zu snap socket(s)%s", id, s.size(), s.empty() ? "" : (" (" + s[0].kind + ")").c_str());
    }
    const AssetDef* tr = a.lib.find("aw_trench_straight");
    std::vector<vec3> tv; if (tr) snapMesh(a, tr, 3, &tv);
    float x0 = 1e9f, x1 = -1e9f; for (auto& v : tv) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); }
    CHK(!tv.empty() && std::fabs(x0 + 4) < 1e-3f && std::fabs(x1 - 4) < 1e-3f, "straight trench without its end caps spans exactly 8.0 m (spoil %.3f .. %.3f m; was 8.4 m)", x0, x1);
    return true;
  });
  // ---------------------------------------------------------------- trenches
  push("trench chain: straight -> zig-zag -> corner", [](App& a, float) {
    const AssetDef* st = a.lib.find("aw_trench_straight");
    ids.straight = a.placeInstance(st, vec3(20, 0, -30), 30.0f);
    a.syncRuntime();
    CHK(ids.straight != 0, "straight trench placed (yaw 30)");
    bool s1 = false, s2 = false;
    ids.zig = snapPlace(a, "aw_trench_zigzag", ids.straight, 1, vec3(0.7f, 0, 0.5f), 47.0f, &s1);    // cursor 0.9 m off the free end, heading 17 deg off
    ids.corner = snapPlace(a, "aw_trench_corner", ids.zig, 1, vec3(-0.4f, 0, 0.9f), 20.0f, &s2);
    CHK(s1 && s2 && ids.zig && ids.corner, "zig-zag and corner snap from a cursor within 1.5 m of the free ends and place");
    float dd1, dd2;
    float g1 = sockGap(a, ids.straight, 1, ids.zig, 0, &dd1), g2 = sockGap(a, ids.zig, 1, ids.corner, 0, &dd2);
    CHK(g1 < 0.005f && g2 < 0.005f && dd1 < -0.999f && dd2 < -0.999f, "sockets meet exactly: gaps %.4f / %.4f m, facing (%.4f / %.4f)", g1, g2, dd1, dd2);
    Instance *A = a.doc.find(ids.straight), *Z = a.doc.find(ids.zig), *C = a.doc.find(ids.corner);
    CHK(A->pos.y == Z->pos.y && Z->pos.y == C->pos.y, "one base height for the chain (%.3f / %.3f / %.3f)", A->pos.y, Z->pos.y, C->pos.y);
    CHK(std::fabs(Z->yaw - 30.0f) < 0.01f, "the zig-zag continues in line (yaw %.2f, wanted 47 -> 30)", Z->yaw);
    CHK(snapMask(*A) == 2 && snapMask(*Z) == 3 && snapMask(*C) == 1, "joins: straight %u, zig-zag %u, corner %u (bit = joined socket)", snapMask(*A), snapMask(*Z), snapMask(*C));
    // no gap, no overlap: each side's geometry ends exactly on the joint plane
    int bad = 0; std::string rep;
    auto joint = [&](uint64_t p, int sp, uint64_t q) {
      auto w = snapWorldSockets(a, *a.doc.find(p))[sp];
      float rp = reach(a, p, w.p, w.dir, 2.0f), rq = reach(a, q, w.p, -w.dir, 2.0f);
      if (std::fabs(rp) > 0.005f || std::fabs(rq) > 0.005f) ++bad;
      char b[96]; std::snprintf(b, sizeof b, " %+.4f / %+.4f", rp, rq); rep += b;
    };
    joint(ids.straight, 1, ids.zig); joint(ids.zig, 1, ids.corner);
    CHK(bad == 0, "no gap, no overlap at the joints: geometry reaches the joint planes to within 5 mm (%s m)", rep.c_str());
    // equal floors and cuts either side of each joint
    float worst = 0; float floorY = A->pos.y - (1.5f + 0.25f);
    for (auto [p, sp] : {std::pair<uint64_t, int>{ids.straight, 1}, {ids.zig, 1}}) {
      auto w = snapWorldSockets(a, *a.doc.find(p))[sp];
      for (float s : {-0.6f, 0.0f, 0.6f}) { vec3 q = w.p + w.dir * s; worst = std::max(worst, std::fabs(a.terrain.height(q.x, q.z) - floorY)); }
    }
    CHK(worst < 0.02f, "cut floors equal across the joints (worst %.3f m off the chain floor %.2f)", worst, floorY);
    // collision: the joined ends are open, the free ends closed
    int blocked = 0;
    for (auto [p, sp] : {std::pair<uint64_t, int>{ids.straight, 1}, {ids.zig, 1}}) {
      auto w = snapWorldSockets(a, *a.doc.find(p))[sp];
      float t; uint64_t ud;
      vec3 o = w.p - w.dir * 1.5f + vec3(0, A->pos.y - w.p.y - 1.5f + 0.9f, 0);
      if (Physics::castRay(o, w.dir, 3.0f, t, ud)) ++blocked;
    }
    CHK(blocked == 0, "a ray along the trench floor passes both joints (%d blocked)", blocked);
    {
      auto w = snapWorldSockets(a, *C)[1]; float t; uint64_t ud;
      vec3 o = w.p - w.dir * 1.0f + vec3(0, -1.5f + 0.9f, 0);
      bool hit = Physics::castRay(o, w.dir, 2.0f, t, ud);
      auto w0 = snapWorldSockets(a, *A)[0]; vec3 o0 = w0.p - w0.dir * 1.0f + vec3(0, -1.5f + 0.9f, 0);
      bool hit0 = Physics::castRay(o0, w0.dir, 2.0f, t, ud);
      CHK(hit && hit0, "the chain's free ends are closed by their caps (corner %d, straight %d)", hit, hit0);
    }
    std::vector<vec3> t0, t1; snapMesh(a, a.assetFor(*C), 0, &t0); snapMesh(a, a.assetFor(*C), 1, &t1);
    CHK(t1.size() < t0.size(), "a joined end draws without its cap and joint faces (%zu -> %zu triangles)", t0.size() / 3, t1.size() / 3);
    // overlap test: the neighbours of a snapped pose pass, a pose not on the sockets is rejected
    SnapFit f = snapFit(a, a.assetFor(*Z), Z->pos, Z->yaw, Z->id);
    bool nb = std::find(f.neighbours.begin(), f.neighbours.end(), ids.straight) != f.neighbours.end() && std::find(f.neighbours.begin(), f.neighbours.end(), ids.corner) != f.neighbours.end();
    PlaceCheck pc = a.checkPlacement(a.assetFor(*Z), Z->pos, Z->yaw, Z->id);
    PlaceCheck bad2 = a.checkPlacement(a.lib.find("aw_trench_straight"), A->pos + yawQuat(30) * vec3(3, 0, 0), 30.0f, 0);
    CHK(nb && pc.ok && !bad2.ok, "overlap test: the zig-zag's snapped neighbours pass (%s), a piece half inside the straight is refused (%s)", pc.reason.c_str(), bad2.reason.c_str());
    // texture origin: one per chain
    DrawItem d1{&a.assetFor(*A)->opaque, mat4(1)}, d2{&a.assetFor(*C)->opaque, mat4(1)};
    snapDraw(a, *A, d1); snapDraw(a, *C, d2);
    CHK(d1.apWorld.w == 1 && d1.apWorld == d2.apWorld && d2.mesh != &a.assetFor(*C)->opaque, "the chain draws with one world-space texture origin (%.0f, %.0f) and its joined-end meshes", d1.apWorld.x, d1.apWorld.z);
    return true;
  });
  push("move re-snaps, undo un-joins", [](App& a, float) {
    Instance* C = a.doc.find(ids.corner); const AssetDef* ca = a.assetFor(*C);
    vec3 keep = C->pos; float keepYaw = C->yaw;
    vec3 p = keep + vec3(1.0f, 0, -0.6f); float y = keepYaw + 25.0f;
    bool s = snapPlacement(a, ca, p, y, C->id);   // moving: its own sockets are free again
    CHK(s && glm::length(vec2(p.x - keep.x, p.z - keep.z)) < 0.005f && std::fabs(wrapDeg(y - keepYaw)) < 0.01f, "dragging the corner near its old spot snaps back onto the zig-zag (%.4f m)", glm::length(vec2(p.x - keep.x, p.z - keep.z)));
    vec3 far = keep + vec3(6, 0, 6); float y2 = keepYaw;
    CHK(!snapPlacement(a, ca, far, y2, C->id) || glm::length(far - (keep + vec3(6, 0, 6))) < 1e-4f, "no snap 6 m away from any free socket");
    size_t n = a.doc.instances.size();
    a.undo(); a.syncRuntime(); snapSync(a);
    Instance* Z = a.doc.find(ids.zig);
    CHK(a.doc.instances.size() == n - 1 && !a.doc.find(ids.corner) && Z && snapMask(*Z) == 1, "undo removes the corner in one step; the zig-zag's end is free again (mask %u)", Z ? snapMask(*Z) : 99);
    a.redo(); a.syncRuntime(); snapSync(a);
    CHK(a.doc.find(ids.corner) && snapMask(*a.doc.find(ids.zig)) == 3, "redo joins it again");
    return true;
  });
  // ---------------------------------------------------------------- paths, hedges
  push("paths join end to end", [](App& a, float) {
    const AssetDef* pd = a.lib.find("path_dirt");
    ids.path1 = a.placeInstance(pd, vec3(-40, 0, 30), 15.0f); a.syncRuntime();
    bool s = false;
    ids.path2 = snapPlace(a, "path_dirt", ids.path1, 1, vec3(0.5f, 0, 0.3f), 15.0f, &s);
    float dd; float g = ids.path2 ? sockGap(a, ids.path1, 1, ids.path2, 0, &dd, true) : 9;
    Instance* p2 = a.doc.find(ids.path2);
    CHK(s && ids.path2 && g < 0.01f && p2 && std::fabs(p2->yaw - 15.0f) < 0.01f, "path_dirt continues a path from its free end (snapPath through snapPlacement): end gap %.4f m (xz)", g);
    CHK(p2 && snapMask(*a.doc.find(ids.path1)) == 2 && snapMask(*p2) == 1, "the two paths are joined (path sockets follow the length)");
    DrawItem d1{nullptr, mat4(1)}, d2{nullptr, mat4(1)};
    snapDraw(a, *a.doc.find(ids.path1), d1); if (p2) snapDraw(a, *p2, d2);
    CHK(d1.apWorld.w == 1 && d1.apWorld == d2.apWorld, "joined paths share the world-space texture origin");
    return true;
  });
  push("hedgerow banks chain and turn", [](App& a, float) {
    const AssetDef* hs = a.lib.find("bcg_hedgerow_straight");
    ids.h1 = a.placeInstance(hs, vec3(-60, 0, -60), 0.0f); a.syncRuntime();
    bool s1 = false, s2 = false;
    ids.h2 = snapPlace(a, "bcg_hedgerow_straight", ids.h1, 1, vec3(1.0f, 0, 0.4f), 8.0f, &s1);
    ids.h3 = snapPlace(a, "bcg_hedgerow_corner", ids.h2, 1, vec3(0.6f, 0, 0.6f), 0.0f, &s2);
    CHK(s1 && s2 && ids.h2 && ids.h3, "straight + corner snap onto a hedgerow bank's free end");
    if (!(ids.h2 && ids.h3)) return true;
    float d1, d2; float g1 = sockGap(a, ids.h1, 1, ids.h2, 0, &d1), g2 = sockGap(a, ids.h2, 1, ids.h3, 0, &d2);
    Instance *H1 = a.doc.find(ids.h1), *H2 = a.doc.find(ids.h2), *H3 = a.doc.find(ids.h3);
    CHK(g1 < 0.005f && g2 < 0.005f && H1->pos.y == H2->pos.y && H2->pos.y == H3->pos.y, "hedge joints meet (%.4f / %.4f m) at one base height", g1, g2);
    auto w = snapWorldSockets(a, *H1)[1];
    float r1 = reach(a, ids.h1, w.p, w.dir, 1.6f), r2 = reach(a, ids.h2, w.p, -w.dir, 1.6f);
    CHK(std::fabs(r1) < 0.005f && std::fabs(r2) < 0.005f, "hedge banks close at the joint: reach %+.4f / %+.4f m (caps hidden, sides run to the end)", r1, r2);
    CHK(snapMask(*H1) == 2 && snapMask(*H2) == 3 && snapMask(*H3) == 1, "hedge joins %u / %u / %u", snapMask(*H1), snapMask(*H2), snapMask(*H3));
    // the legacy low hedge only joins its own kind
    const AssetDef* hb = a.lib.find("hedgerow_bocage");
    ids.b1 = a.placeInstance(hb, vec3(60, 0, 60), 90.0f); a.syncRuntime();
    bool sb = false; ids.b2 = snapPlace(a, "hedgerow_bocage", ids.b1, 0, vec3(0.3f, 0, -0.8f), 90.0f, &sb);
    vec3 p = snapWorldSockets(a, *a.doc.find(ids.h3))[1].p + vec3(0.4f, 0, 0.3f); float y = 0;   // placing reallocated the instance list
    bool cross = snapPlacement(a, hb, p, y, 0);
    CHK(sb && ids.b2 && snapMask(*a.doc.find(ids.b1)) == 1 && !cross, "hedgerow_bocage (settings sockets, kind hedge_low) chains with itself, not onto a bocage bank");
    return true;
  });
  push("screenshot", [](App& a, float t) {
    if (t == 0) {
      Instance* Z = a.doc.find(ids.zig);
      if (Z) { a.camPos = Z->pos + vec3(-6, 9, 14); vec3 d = Z->pos + vec3(0, -1, 0) - a.camPos; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
    }
    if (t > 0.6f && pendingShot.empty() && t < 0.7f) pendingShot = outDir() / "snap_trench_chain.png";
    return t > 1.0f;
  });
  push("finish", [](App& a, float t) {
    CHK(a.R.shaderErrors == 0, "shader errors: %d", a.R.shaderErrors);
    return t >= 0;
  });
}
}  // namespace

bool snapSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-snap") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("snap"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 120) { T->check(false, "snap test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}

// --selftest-nature step (V6): a house placed on shrubs crushes them, blocks on a mature tree, flattens its ground with a
// feathered skirt, and undoes in one step.
bool crushStep(App& a, MpTest& t) {
  const AssetDef* house = a.lib.find("house_stone_cottage");
  const AssetDef *shrub = nullptr, *tall = nullptr, *patch = nullptr;
  for (auto& d : a.lib.assets) {
    float h = std::max(d->bmax.y, d->tree.on ? d->tree.top : 0.0f);
    if (d->tree.on && !d->tree.hedge && h <= snapSettings().crushHeight && h > 0.3f && !shrub) shrub = d.get();
    if (d->tree.on && !d->tree.hedge && h > 6.0f && !tall) tall = d.get();
    if (d->nature.kind == NatureDef::SCATTER && !patch) patch = d.get();
  }
  if (!chk(t, house && shrub && tall && patch, "crush test assets: house, shrub %s, tree %s, patch %s", shrub ? shrub->id.c_str() : "-", tall ? tall->id.c_str() : "-", patch ? patch->id.c_str() : "-")) return true;
  Terrain& Tr = a.terrain;
  vec3 c(-150, 0, 150); const float yaw = 20;
  // a gentle tilt and bump under the site so there is something to flatten
  {
    auto d = a.doc.terrainDeltas;
    float fs = Tr.fineSpacing(), ts = Tr.tileSize();
    for (int tz = 0; tz < Tr.tilesZ(); ++tz) for (int tx = 0; tx < Tr.tilesX(); ++tx) {
      vec2 o = Tr.tileOrigin(tx, tz);
      if (o.x > c.x + 16 || o.x + ts < c.x - 16 || o.y > c.z + 16 || o.y + ts < c.z - 16) continue;
      std::vector<float> dv((size_t)Terrain::kFineN * Terrain::kFineN, 0.0f); bool any = false;
      for (int j = 0; j < Terrain::kFineN; ++j) for (int i = 0; i < Terrain::kFineN; ++i) {
        float x = o.x + i * fs - c.x, z = o.y + j * fs - c.z, r = std::sqrt(x * x + z * z);
        float v = r < 14 ? (0.03f * x + 0.15f * std::cos(z * 0.5f)) * (1 - r / 14) : 0;
        if (std::fabs(v) > 1e-4f) { dv[(size_t)j * Terrain::kFineN + i] = v; any = true; }
      }
      if (any) d[Tr.key(tx, tz)] = dv;
    }
    a.doc.terrainDeltas = d; a.doc.terrainTemplateVersion = a.doc.templateVersion; a.resetGround();
  }
  quat q = yawQuat(yaw);
  vec2 hf = house->footprint * 0.5f;
  std::vector<uint64_t> inside, outside;
  // a mature tree in the footprint blocks
  uint64_t tree = a.placeInstance(tall, c + q * vec3(hf.x * 0.6f, 0, -hf.y * 0.6f), 0);
  for (vec3 l : {vec3(-hf.x * 0.5f, 0, hf.y * 0.1f), vec3(hf.x * 0.1f, 0, hf.y * 0.6f), vec3(-hf.x * 0.4f, 0, -hf.y * 0.6f)}) { vec3 w = c + q * l; inside.push_back(a.placeInstance(shrub, w, 0)); }
  uint64_t patchId = a.placeInstance(patch, c + q * vec3(hf.x * 0.3f, 0, -hf.y * 0.3f), 0);   // patches clear under footprints themselves: kept
  vec3 outP = c + q * vec3(hf.x + snapSettings().skirt + 2.0f, 0, 0);
  outside.push_back(a.placeInstance(shrub, outP, 0));
  a.syncRuntime();
  chk(t, tree && std::count(inside.begin(), inside.end(), 0ull) == 0 && outside[0], "test growth placed: tree #%llu, %zu shrubs in the footprint, one outside", (unsigned long long)tree, inside.size());
  PlaceCheck blocked = a.checkPlacement(house, c, yaw, 0);
  chk(t, !blocked.ok && blocked.reason.find(tall->name) != std::string::npos, "a %.1f m %s in the footprint blocks the house: \"%s\"", std::max(tall->bmax.y, tall->tree.top), tall->name.c_str(), blocked.reason.c_str());
  a.selected = tree; a.deleteSelected(); a.syncRuntime();
  // ground before
  float before0 = 1e9f, before1 = -1e9f, farBefore = Tr.height(outP.x, outP.z);
  for (float u = -0.9f; u <= 0.91f; u += 0.3f) for (float v = -0.9f; v <= 0.91f; v += 0.3f) { vec3 w = c + q * vec3(u * hf.x, 0, v * hf.y); float h = Tr.height(w.x, w.z); before0 = std::min(before0, h); before1 = std::max(before1, h); }
  auto deltasBefore = a.doc.terrainDeltas; size_t nBefore = a.doc.instances.size(), undoBefore = a.undoStack.size();
  uint64_t hid = a.placeInstance(house, c, yaw);
  a.syncRuntime();
  Instance* H = a.doc.find(hid);
  if (!chk(t, H != nullptr, "house placed over the shrubs once the tree is gone")) return true;
  int gone = 0; for (auto id : inside) gone += a.doc.find(id) == nullptr;
  bool kept = a.doc.find(outside[0]) != nullptr;
  chk(t, gone == (int)inside.size() && kept && a.doc.find(patchId), "%d of %zu shrubs under the footprint crushed (limit %.1f m); the one outside and the %s patch (it clears itself under footprints) kept", gone, inside.size(), snapSettings().crushHeight, patch->id.c_str());
  float worst = 0;
  for (float u = -0.95f; u <= 0.96f; u += 0.1f) for (float v = -0.95f; v <= 0.96f; v += 0.1f) { vec3 w = c + q * vec3(u * hf.x, 0, v * hf.y); worst = std::max(worst, std::fabs(Tr.height(w.x, w.z) - H->pos.y)); }
  chk(t, before1 - before0 > 0.2f && worst < 0.03f, "ground under the footprint flattened to the base %.2f m (was %.2f m of relief, now within %.3f m)", H->pos.y, before1 - before0, worst);
  // the skirt feathers into the old ground; beyond it nothing changed
  vec3 mid = c + q * vec3(hf.x + snapSettings().skirt * 0.5f, 0, 0);
  float hm = Tr.height(mid.x, mid.z), hOld = H->pos.y;
  chk(t, std::fabs(Tr.height(outP.x, outP.z) - farBefore) < 0.01f && std::isfinite(hm), "beyond the %.1f m skirt the ground is untouched (%.3f m change); half way out it is %.2f m (base %.2f)", snapSettings().skirt, std::fabs(Tr.height(outP.x, outP.z) - farBefore), hm, hOld);
  chk(t, a.undoStack.size() == undoBefore + 1, "placing, crushing and flattening is one undo step (%zu)", a.undoStack.size() - undoBefore);
  a.undo(); a.syncRuntime();
  int back = 0; for (auto id : inside) back += a.doc.find(id) != nullptr;
  chk(t, back == (int)inside.size() && a.doc.instances.size() == nBefore && a.doc.terrainDeltas == deltasBefore, "undo brings the shrubs and the old ground back (%d of %zu)", back, inside.size());
  a.redo(); a.syncRuntime();
  return true;
}
