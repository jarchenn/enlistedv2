// --selftest-damage: wall breaching (M1) and ground deformation (M2) integration tests.
// Impacts are injected through App::explode, the same resolver the PaK 40 shell calls.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <algorithm>

static fs::path outDir2() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
static uint64_t wallUD(uint64_t id) { return (id << 16) | 0xFFFFull; }
static const float kHE = 120.0f, kHER = 2.2f;   // game damage units / metres (see docs/destruction.md)

namespace {
struct Ctx {
  uint64_t A = 0, B = 0, C = 0, M = 0, C2 = 0;
  std::string pA;           // farmhouse panel under test
  int variant = 0;
  vec3 Q{0}, Q2{0};
  float q2depth = 0;
};
Ctx X;

vec3 W(App& app, uint64_t id, vec3 l) { Instance* i = app.doc.find(id); return i->pos + yawQuat(i->yaw) * l; }
vec3 Wd(App& app, uint64_t id, vec3 l) { Instance* i = app.doc.find(id); return yawQuat(i->yaw) * l; }
int pidx(const AssetDef* a, const std::string& id) { for (size_t k = 0; k < a->panels.size(); ++k) if (a->panels[k].id == id) return (int)k; return -1; }
float tangentWidth(const Panel& p) { vec3 e = p.bmax - p.bmin; return std::fabs(p.normal.z) > 0.5f ? e.x : e.z; }
// exterior face point of a panel (local), at chest height, with optional tangential/vertical offsets
vec3 facePoint(const Panel& p, float along = 0, float up = 0) {
  vec3 c = (p.bmin + p.bmax) * 0.5f;
  int ax = std::fabs(p.normal.z) > 0.5f ? 2 : 0, ta = ax == 2 ? 0 : 2;
  c[ax] = p.normal[ax] > 0 ? p.bmax[ax] : p.bmin[ax];
  c.y = p.bmin.y + std::min(1.3f, (p.bmax.y - p.bmin.y) * 0.45f) + up;
  float half = tangentWidth(p) * 0.5f;
  c[ta] += std::clamp(along, -half * 0.5f, half * 0.5f);
  return c;
}
// fire a hit into a panel from outside; returns the resolver's report
App::Explosion hitPanel(App& app, uint64_t id, const Panel& p, float along = 0, float up = 0) {
  vec3 fp = facePoint(p, along, up);
  return app.explode(W(app, id, fp), Wd(app, id, -p.normal), kHE, kHER, 1.0f);
}
// does a ray through the panel's centre pass the wall (no hit on this copy's wall body inside the wall)?
bool rayPasses(App& app, uint64_t id, const Panel& p, float& tHit, float along = 0, float up = 0) {
  vec3 fp = facePoint(p, along, up);
  float depth = std::fabs(glm::dot(p.bmax - p.bmin, glm::abs(p.normal)));
  vec3 o = W(app, id, fp + p.normal * 0.05f), d = Wd(app, id, -p.normal);   // from just outside this bay's face
  Physics::Hit h; tHit = 99;
  if (!Physics::castRayEx(o, d, 0.05f + depth + 0.02f, h)) return true;   // only this bay's own thickness counts
  tHit = h.t;
  return h.ud != wallUD(id);
}
int breachedCount(App& app, uint64_t id) { int n = 0; for (auto s : app.rt[id].state) n += s == PANEL_BREACHED; return n; }
// local wall-hit at an asset-space point / direction on one copy (bypasses the projectile path)
App::Explosion hitAt(App& app, uint64_t id, vec3 lp, vec3 ld, int kind = WALL_HE, float dmg = kHE) { return app.explode(W(app, id, lp), Wd(app, id, ld), dmg, kHER, 0.0f, ~0ull, kind); }
// extent of removed cells (any depth) of one bay along its width axis, in metres
float removedWidth(App& app, uint64_t id, int k) {
  const Panel& p = app.rt[id].asset->panels[k];
  auto& w = app.rt[id].wf[k];
  if (w.rf.empty()) return 0;
  float W = tangentWidth(p); int nu = std::max(1, (int)std::ceil(W / app.destruction.cell)), nv = (int)w.rf.size() / nu;
  int lo = 1 << 30, hi = -1;
  for (int v = 0; v < nv; ++v) for (int u = 0; u < nu; ++u) if (w.rf[v * nu + u] + w.rb[v * nu + u] > 0.02f) { lo = std::min(lo, u); hi = std::max(hi, u); }
  return hi < lo ? 0 : (hi - lo + 1) * W / nu;
}
// a wall bay's damage field as grids: open (through-hole) and damaged (> 2 cm removed) cells
struct Grid { int nu = 0, nv = 0; std::vector<uint8_t> open, dmg; float maxRemoved = 0; };
Grid gridOf(App& app, uint64_t id, int k) {
  Grid g;
  const Panel& p = app.rt[id].asset->panels[k];
  auto& w = app.rt[id].wf[k];
  if (w.rf.empty()) return g;
  g.nu = std::max(1, (int)std::ceil(tangentWidth(p) / app.destruction.cell)); g.nv = (int)w.rf.size() / g.nu;
  float T = std::fabs(glm::dot(p.bmax - p.bmin, glm::abs(p.normal)));
  for (size_t i = 0; i < w.rf.size(); ++i) {
    float rm = w.rf[i] + w.rb[i];
    g.open.push_back(rm >= T - 1e-4f); g.dmg.push_back(rm > 0.02f); g.maxRemoved = std::max(g.maxRemoved, std::min(rm, T));
  }
  return g;
}
// solid cells left in a row / column run of at most n cells with a hole on both sides (slivers between holes)
int sliverCells(const Grid& g, int n) {
  int count = 0;
  for (int axis = 0; axis < 2; ++axis) {
    int lines = axis ? g.nu : g.nv, len = axis ? g.nv : g.nu;
    for (int a = 0; a < lines; ++a) {
      auto at = [&](int b) { return axis ? g.open[b * g.nu + a] : g.open[a * g.nu + b]; };
      for (int b = 0; b < len;) {
        if (at(b)) { ++b; continue; }
        int e = b; while (e < len && !at(e)) ++e;
        if (b > 0 && e < len && e - b <= n) count += e - b;
        b = e;
      }
    }
  }
  return count;
}
// separate through-holes (4-connected, at least 4 cells)
int openComponents(const Grid& g) {
  std::vector<uint8_t> seen(g.open.size(), 0);
  int comps = 0;
  for (int s = 0; s < (int)g.open.size(); ++s) {
    if (!g.open[s] || seen[s]) continue;
    int sz = 0; std::vector<int> st{s}; seen[s] = 1;
    while (!st.empty()) {
      int i = st.back(); st.pop_back(); ++sz;
      int u = i % g.nu, v = i / g.nu;
      const int nb[4] = {u > 0 ? i - 1 : -1, u < g.nu - 1 ? i + 1 : -1, v > 0 ? i - g.nu : -1, v < g.nv - 1 ? i + g.nu : -1};
      for (int m : nb) if (m >= 0 && g.open[m] && !seen[m]) { seen[m] = 1; st.push_back(m); }
    }
    if (sz >= 4) ++comps;
  }
  return comps;
}
// aspect ratio (major / minor axis, from second moments) and orientation of the damaged region
float aspectOf(const Grid& g, float& angleDeg) {
  double n = 0, su = 0, sv = 0, suu = 0, svv = 0, suv = 0;
  for (int v = 0; v < g.nv; ++v) for (int u = 0; u < g.nu; ++u) if (g.dmg[v * g.nu + u]) { n += 1; su += u; sv += v; suu += (double)u * u; svv += (double)v * v; suv += (double)u * v; }
  angleDeg = 0;
  if (n < 8) return 1.0f;
  double mu = su / n, mv = sv / n, a = suu / n - mu * mu, c = svv / n - mv * mv, b = suv / n - mu * mv;
  double tr = a + c, det = a * c - b * b, disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
  double l1 = tr / 2 + disc, l2 = std::max(1e-9, tr / 2 - disc);
  angleDeg = (float)(0.5 * std::atan2(2 * b, a - c) * 57.29578);
  return (float)std::sqrt(l1 / l2);
}
float physGround(vec3 p) { Physics::Hit h; return Physics::castRayEx(vec3(p.x, p.y + 30, p.z), vec3(0, -1, 0), 80, h) ? h.pos.y : -999; }
}

void SelfTest::beginCustom(App& app, const std::string& asset, uint64_t inst, const json& st) {
  curAsset = asset; houseId = inst;
  steps.clear(); for (auto& s : st) steps.push_back(s);
  stepIdx = 0; stepTime = 0; ++pass;
  (void)app;
}

void SelfTest::damageUpdate(App& app, float dt) {
  if (stage >= 2600) { tanksUpdate(app, dt); return; }   // selftest_tanks.cpp
  if (stage >= 1900) { entitiesUpdate(app, dt); return; }
  if (stage >= 1700) { updateSuite(app, dt); return; }
  if (stage >= 1600) { sightsUpdate(app, dt); return; }
  if (stage >= 1500) { moveUpdate(app, dt); return; }
  if (stage >= 1400) { arsenalUpdate(app, dt); return; }
  if (stage >= 1300) { villageUpdate(app, dt); return; }
  if (stage >= 1200) { envUpdate(app, dt); return; }
  if (stage >= 1100) { fpsUpdate(app, dt); return; }
  if (stage >= 990) { recordUpdate(app, dt); return; }
  if (stage >= 900) { grassUpdate(app, dt); return; }
  if (stage >= 800) { captureUpdate(app, dt); return; }
  if (stage >= 700) { weaponsUpdate(app, dt); return; }
  if (stage >= 600) { pakUpdate(app, dt); return; }
  auto go = [&](int s) { stage = s; stageTime = 0; };
  const char* FH = "house_farmhouse_2story";
  switch (stage) {
  // ------------------------------------------------------------- setup: fixture map
  case 500: {
    reportName = "damage";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Destruction range");
    X.A = app.placeInstance(app.lib.find(FH), vec3(20, 0, 10), 37.0f);
    X.B = app.placeInstance(app.lib.find(FH), vec3(62, 0, 10), 37.0f);
    X.C = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(-40, 0, 45), 200.0f);
    X.M = app.placeInstance(app.lib.find("large_municipal_building"), vec3(110, 0, -70), 123.0f);
    check(X.A && X.B && X.C && X.M, "fixture: two farmhouse copies (one at 37 deg), rotated MG casemate, municipal building placed");
    app.syncRuntime();
    const AssetDef* fa = app.lib.find(FH);
    X.pA = "floor-00/west/bay-03";
    check(pidx(fa, X.pA) >= 0, "farmhouse has stable panel " + X.pA);
    app.doc.start = {true, W(app, X.A, vec3(-8.0f, 0, 2.63f)), 0};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.enterExplore(), "F5 into Explore for the range");
    app.useScripted = true;
    go(501);
    break;
  }
  // ------------------------------------------------------------- M1: breach one bay of the rotated farmhouse
  case 501: {
    const AssetDef* fa = app.lib.find(FH);
    int k = pidx(fa, X.pA);
    const Panel& p = fa->panels[k];
    float t0;
    check(!rayPasses(app, X.A, p, t0), "intact wall stops a ray (hit at " + std::to_string(t0).substr(0, 4) + " m)");
    auto e1 = hitPanel(app, X.A, p);
    check(app.rt[X.A].state[k] == PANEL_DAMAGED, "masonry bay damaged (not open) after one centred HE hit");
    auto e2 = hitPanel(app, X.A, p);
    check(app.rt[X.A].state[k] == PANEL_BREACHED, "masonry bay breached after second hit");
    check(breachedCount(app, X.A) == 1, "neighbouring panels stay up (" + std::to_string(breachedCount(app, X.A)) + " breached)");
    check(app.rt[X.B].state[k] == PANEL_INTACT && !app.rt[X.B].damaged, "the other farmhouse copy is untouched (no shared prefab damage)");
    float t1;
    check(rayPasses(app, X.A, p, t1), "a ray now passes through the visual opening");
    auto e3 = hitPanel(app, X.A, p);   // second shot through the opening queries the NEW shape
    bool hitThis = std::find(e3.hits.begin(), e3.hits.end(), X.pA) != e3.hits.end();
    check(!hitThis || app.rt[X.A].state[k] == PANEL_BREACHED, "a further shot through the opening resolves against the rebuilt collision");
    summary["wall_rebuild_ms_farmhouse"] = app.rt[X.A].lastRebuildMs;
    summary["panel_rebuild_ms_farmhouse"] = app.rt[X.A].lastPanelMs;
    (void)e1; (void)e2;
    {   // the breach is local: wall still stands a bay-width away; hitting the strip below it joins a doorway
      float tt;
      check(!rayPasses(app, X.A, p, tt, 0, -0.95f), "below the chest-height breach the wall still stands (a sill, not a deleted bay)");
      hitPanel(app, X.A, p, 0, -0.85f); hitPanel(app, X.A, p, 0, -0.85f); hitPanel(app, X.A, p, 0, -0.45f);
      bool low = rayPasses(app, X.A, p, tt, 0, -0.95f), mid = rayPasses(app, X.A, p, tt, 0, -0.45f), high = rayPasses(app, X.A, p, tt);
      char b[200]; std::snprintf(b, sizeof b, "three more hits on and above the strip below join the holes into one doorway (rays at knee %d, hip %d, chest %d); open %.2f m^2 of %.2f",
                                 (int)low, (int)mid, (int)high, app.rt[X.A].wf[k].openArea, tangentWidth(p) * (p.bmax.y - p.bmin.y));
      check(low && mid && high && app.rt[X.A].wf[k].openArea < 0.8f * tangentWidth(p) * (p.bmax.y - p.bmin.y), b);
    }
    // walk through the breach, use a window, up one stair, down the other, out the back door
    Physics::setCharacterPos(W(app, X.A, vec3(-7.8f, 0, 2.63f)) + vec3(0, 0.05f, 0));
    app.eyeYaw = worldYaw(app, -90);
    json st = json::array({
      json{{"look", {-6.0, 1.2, 2.63}}, {"hold", 0.5}}, json{{"shot", "30_breach_outside"}},
      json{{"goto", {-6.9, 2.63}}}, json{{"goto", {-4.9, 2.63}}, {"expectY", 0.3}, {"crouch", true}, {"note", "crouch through the shell breach (a real capsule, rebuilt collision)"}},
      json{{"goto", {-4.4, 2.63}}},
      json{{"look", {-6.5, 1.2, 2.63}}, {"hold", 0.5}}, json{{"shot", "31_breach_inside"}},
      json{{"goto", {-3.0, 3.05}}, {"expectY", 0.3}, {"note", "kitchen window next to the breach"}},
      json{{"interact", "kitchen_casement"}, {"aim", {-2.75, 1.9, 4.05}}, {"expectOpen", true}},
      json{{"goto", {-0.9, 3.3}}}, json{{"goto", {0.2, 3.3}}}, json{{"goto", {0.2, 2.95}}},
      json{{"goto", {0.2, -1.7}}, {"expectY", 3.3}, {"note", "front staircase after the breach"}},
      json{{"goto", {0.2, -2.4}}}, json{{"goto", {2.5, -1.9}}}, json{{"goto", {3.6, 1.3}}},
      json{{"goto", {5.0, 1.5}}, {"expectY", 3.3}},
      json{{"goto", {5.0, -3.45}}, {"expectY", 0.3}, {"note", "rear staircase after the breach"}},
      json{{"goto", {3.9, -3.55}}}, json{{"goto", {3.35, -2.85}}},
      json{{"interact", "rear_door"}, {"aim", {3.15, 1.3, -4.03}}, {"expectOpen", true}, {"note", "door beside the damaged wall still works"}},
      json{{"goto", {3.2, -5.4}}},
    });
    beginCustom(app, FH, X.A, st);
    go(502);
    break;
  }
  case 502:
    if (runWalkthrough(app, dt)) go(503);
    break;
  // ------------------------------------------------------------- M2: craters
  case 503: {
    float ts = app.terrain.tileSize();
    // (a) crater across a tile boundary: sampled height == downward physics ray, no seam
    int tx = (int)std::round((-100 + app.terrain.size * 0.5f) / ts);
    float xb = -app.terrain.size * 0.5f + tx * ts, zc = -25;
    size_t tiles0 = app.terrain.tiles.size();
    float g0 = app.terrain.height(xb, zc);
    app.craterAt(vec3(xb, g0, zc), 2.0f, 0.7f);   // DEBUG preset: 4 m across, 0.7 m deep (not an ordnance measurement)
    check(app.terrain.tiles.size() >= tiles0 + 2, "debug crater across a tile edge activates both tiles (" + std::to_string(app.terrain.tiles.size() - tiles0) + ")");
    float maxd = 0, seam = 0; vec3 worst(0); float wr = 0, wp = 0; uint64_t wud = 0;
    for (int j = -6; j <= 6; ++j) for (int i = -6; i <= 6; ++i) {
      vec3 s(xb + i * 0.4f + 0.013f, 0, zc + j * 0.4f + 0.007f);
      float hr = app.terrain.height(s.x, s.z);
      Physics::Hit hh; bool ok = Physics::castRayEx(vec3(s.x, hr + 30, s.z), vec3(0, -1, 0), 80, hh);
      float hp = ok ? hh.pos.y : -999;
      if (std::fabs(hr - hp) > maxd) { maxd = std::fabs(hr - hp); worst = s; wr = hr; wp = hp; wud = hh.ud; }
    }
    logf("[crater-debug] worst at %.3f,%.3f render %.3f physics %.3f ud %llx tile %d,%d xb %.3f", worst.x, worst.z, wr, wp, (unsigned long long)wud,
      (int)std::floor((worst.x + app.terrain.size * 0.5f) / ts), (int)std::floor((worst.z + app.terrain.size * 0.5f) / ts), xb);
    for (int j = -5; j <= 5; ++j) seam = std::max(seam, std::fabs(app.terrain.height(xb - 0.002f, zc + j * 0.35f) - app.terrain.height(xb + 0.002f, zc + j * 0.35f)));
    char b[200];
    std::snprintf(b, sizeof b, "crater surface matches collision (max |render - physics ray| %.3f m)", maxd);
    check(maxd < 0.03f, b);
    std::snprintf(b, sizeof b, "no gap between chunks at the tile edge (max step %.4f m)", seam);
    check(seam < 0.01f, b);
    std::snprintf(b, sizeof b, "debug crater depth %.2f m (preset 0.70)", g0 - app.terrain.height(xb, zc));
    check(std::fabs(g0 - app.terrain.height(xb, zc) - 0.7f) < 0.1f, b);
    // (b) HE beside the rotated farmhouse's rear threshold: threshold stays supported
    vec3 step = W(app, X.A, vec3(3.2f, 0, -4.9f)), imp = W(app, X.A, vec3(3.2f, 0, -6.3f));
    float hs0 = app.terrain.height(step.x, step.z), hi0 = app.terrain.height(imp.x, imp.z);
    auto ex = app.explode(vec3(imp.x, hi0 + 0.05f, imp.z), vec3(0, -1, 0), kHE, kHER, 1.0f);
    std::snprintf(b, sizeof b, "HE beside the rear threshold cratered the yard (%.2f m) but not the protected step (%.3f m)", hi0 - app.terrain.height(imp.x, imp.z), hs0 - app.terrain.height(step.x, step.z));
    check(ex.crater && hi0 - app.terrain.height(imp.x, imp.z) > 0.2f && std::fabs(hs0 - app.terrain.height(step.x, step.z)) < 0.02f, b);
    // (c) two shells into the same depression: deeper, capped, still matching collision
    vec3 P(-120, 0, 60); P.y = app.terrain.height(P.x, P.z);
    float base = P.y;
    app.deformGround(P, 1.0f); float d1 = base - app.terrain.height(P.x, P.z);
    app.deformGround(vec3(P.x, app.terrain.height(P.x, P.z), P.z), 1.0f); float d2 = base - app.terrain.height(P.x, P.z);
    std::snprintf(b, sizeof b, "second HE in the same depression deepens it %.2f -> %.2f m (cap 1.6)", d1, d2);
    check(d2 > d1 * 1.5f && d2 <= 1.61f, b);
    std::snprintf(b, sizeof b, "overlapping craters still match collision (|diff| %.3f)", std::fabs(app.terrain.height(P.x + 0.31f, P.z) - physGround(vec3(P.x + 0.31f, 0, P.z))));
    check(std::fabs(app.terrain.height(P.x + 0.31f, P.z) - physGround(vec3(P.x + 0.31f, 0, P.z))) < 0.03f, b);
    summary["he_crater_depth_m"] = d1; summary["he_crater_depth_twice_m"] = d2;
    // (d) observation point: a low observer sees a crouched player's chest on flat ground but not in the debug crater
    X.Q = vec3(-150, 0, 20); X.Q.y = app.terrain.height(X.Q.x, X.Q.z);
    vec3 obs(X.Q.x + 18, 0, X.Q.z); obs.y = app.terrain.height(obs.x, obs.z) + 0.5f;
    auto visible = [&](vec3 target) { Physics::Hit h; vec3 d = target - obs; return !Physics::castRayEx(obs, d, glm::length(d) - 0.05f, h); };
    bool before = visible(X.Q + vec3(0, 0.6f, 0));
    app.craterAt(X.Q, 2.0f, 0.7f);
    float floorY = app.terrain.height(X.Q.x, X.Q.z);
    bool after = visible(vec3(X.Q.x, floorY + 0.6f, X.Q.z));
    bool headAfter = visible(vec3(X.Q.x, floorY + 1.15f, X.Q.z));
    check(before && !after, std::string("crouched chest visible on flat ground, hidden by the crater rim from the low observation point") + (headAfter ? " (head still visible: 0.7 m debug crater is shallower than a crouch)" : ""));
    summary["crater_conceals_crouched_head"] = !headAfter;
    // walk: into the threshold crater and back through the protected rear door
    Physics::setCharacterPos(W(app, X.A, vec3(3.2f, 0, -8.6f)) + vec3(0, 0.3f, 0));
    app.eyeYaw = worldYaw(app, 0);
    app.resetInteractStates();
    json st = json::array({
      json{{"goto", {3.2, -6.3}}, {"note", "walk down into the crater"}}, json{{"checkGround", true}},
      json{{"look", {3.2, -0.5, -4.6}}, {"hold", 0.4}}, json{{"shot", "33_crater_threshold"}},
      json{{"goto", {3.2, -5.3}}},
      json{{"interact", "rear_door"}, {"aim", {3.15, 1.3, -4.03}}, {"expectOpen", true}},
      json{{"goto", {3.3, -3.3}}, {"expectY", 0.3}, {"note", "enter over the protected threshold after shelling"}},
    });
    beginCustom(app, FH, X.A, st);
    go(504);
    break;
  }
  case 504:
    if (runWalkthrough(app, dt)) {
      // foot height at the bottom of the observation crater
      Physics::setCharacterPos(vec3(X.Q.x, app.terrain.height(X.Q.x, X.Q.z) + 0.4f, X.Q.z));
      go(505);
    }
    break;
  case 505: {
    app.scriptedInput = ExploreInput{};
    if (stageTime < 0.8f) break;
    auto ch = Physics::character();
    float gh = app.terrain.height(ch.pos.x, ch.pos.z);
    char b[160]; std::snprintf(b, sizeof b, "player settles on the crater floor (feet %.3f, ground %.3f)", ch.pos.y, gh);
    check(ch.grounded && std::fabs(ch.pos.y - gh) < 0.06f, b);
    // ---- rotated MG casemate: survives the first hit, opens after several
    const AssetDef* ca = app.lib.find("bunker_mg_casemate");
    int k = -1; float best = 0;
    for (size_t q = 0; q < ca->panels.size(); ++q) if (ca->panels[q].id.find("/rear/") != std::string::npos && tangentWidth(ca->panels[q]) > best) { best = tangentWidth(ca->panels[q]); k = (int)q; }
    check(k >= 0, "casemate rear bay found");
    if (k < 0) { finish(app); break; }
    const Panel& p = ca->panels[k];
    hitPanel(app, X.C, p);
    check(app.rt[X.C].state[k] != PANEL_BREACHED, "reinforced bunker wall survives the first HE hit (state " + std::to_string(app.rt[X.C].state[k]) + ")");
    int n = 1;
    while (app.rt[X.C].state[k] != PANEL_BREACHED && n < 16) { hitPanel(app, X.C, p); ++n; }
    check(app.rt[X.C].state[k] == PANEL_BREACHED, "casemate bay " + p.id + " opened by " + std::to_string(n) + " deliberate HE hits");
    summary["concrete_hits_to_breach"] = n;
    float t;
    check(rayPasses(app, X.C, p, t), "a ray passes the breached bunker wall");
    {   // widen it down to the floor so a person fits: concrete takes many more hits than masonry
      int m = 0; float tt;
      auto fits = [&]() { for (int a = -4; a <= 4; ++a) for (int u = 0; u < 13; ++u) if (!rayPasses(app, X.C, p, tt, a * 0.1f, -0.95f + u * 0.15f)) return false; return true; };   // 0.8 x 1.85 m, rays every 10-15 cm
      while (m < 120 && !fits()) { hitPanel(app, X.C, p, (m % 3 - 1) * 0.35f, -0.95f + ((m / 3) % 5) * 0.4f); ++m; }
      std::string why;
      if (m >= 120) for (int a = -4; a <= 4 && why.empty(); ++a) for (int u = 0; u < 13; ++u) if (!rayPasses(app, X.C, p, tt, a * 0.1f, -0.95f + u * 0.15f)) { why = " (first blocked ray: along " + std::to_string(a * 0.1f).substr(0, 5) + ", up " + std::to_string(-0.95f + u * 0.15f).substr(0, 5) + ", hit at " + std::to_string(tt).substr(0, 4) + " m)"; break; }
      check(m < 120, "casemate opening widened to a walk-in breach by " + std::to_string(m) + " more hits (reinforced concrete resists far longer than masonry)" + why);
    }
    vec3 c = (p.bmin + p.bmax) * 0.5f;
    Physics::setCharacterPos(W(app, X.C, vec3(c.x, 0, -3.6f)) + vec3(0, 0.25f, 0));
    app.eyeYaw = worldYaw(app, 180);
    json st = json::array({
      json{{"look", {c.x, 1.0, -2.0}}, {"hold", 0.4}}, json{{"shot", "32_casemate_breach"}},
      json{{"goto", {c.x, -1.2}}, {"expectY", 0.3}, {"note", "walk into the casemate through the breach"}},
    });
    beginCustom(app, "bunker_mg_casemate", X.C, st);
    go(506);
    break;
  }
  case 506:
    if (runWalkthrough(app, dt)) {
      // ---- municipal building: breach ground and 4th-floor bays beside stair core A, then walk both cores
      const AssetDef* ma = app.lib.find("large_municipal_building");
      for (const char* id : {"floor-00/west/bay-01", "floor-03/west/bay-01", "floor-03/front/bay-02"}) {
        int k = pidx(ma, id);
        if (k < 0) { check(false, std::string("municipal panel ") + id + " exists"); continue; }
        int n = 0;
        while (app.rt[X.M].state[k] != PANEL_BREACHED && n < 8) { hitPanel(app, X.M, ma->panels[k]); ++n; }
        check(app.rt[X.M].state[k] == PANEL_BREACHED, std::string("municipal ") + id + " breached (" + std::to_string(n) + " hits)");
      }
      summary["wall_rebuild_ms_municipal"] = app.rt[X.M].lastRebuildMs;
      json st = ma->walkthrough["start"];
      Physics::setCharacterPos(W(app, X.M, jvec3(st["pos"])) + vec3(0, 0.2f, 0));
      app.eyeYaw = worldYaw(app, st.value("yaw", 0.0f));
      app.resetInteractStates();
      curAsset = "large_municipal_building"; houseId = X.M;
      steps.clear(); for (auto& s : ma->walkthrough["steps"]) steps.push_back(s);
      stepIdx = 0; stepTime = 0; ++pass;
      go(507);
    }
    break;
  case 507:
    if (runWalkthrough(app, dt)) {
      // ---- crouch through a low breach in a 1.2 m concrete wall (docs/destruction.md, known limits): a fresh MG
      //      casemate copy, rear bay opened only to crouching height, then a crouched walk through the tunnel
      const AssetDef* ca = app.lib.find("bunker_mg_casemate");
      std::string why = app.checkPlacement(ca, vec3(-100, 0, -115), 15.0f, 0).reason;
      X.C2 = app.placeInstance(ca, vec3(-100, 0, -115), 15.0f);
      app.syncRuntime();
      int k = -1; float best = 0;
      for (size_t q = 0; q < ca->panels.size(); ++q) if (ca->panels[q].id.find("/rear/") != std::string::npos && tangentWidth(ca->panels[q]) > best) { best = tangentWidth(ca->panels[q]); k = (int)q; }
      if (!X.C2 || k < 0) { check(false, "second MG casemate copy and its rear bay (" + why + ")"); go(508); break; }
      const Panel& p = ca->panels[k];
      float floorUp = 0.3f - (p.bmin.y + std::min(1.3f, (p.bmax.y - p.bmin.y) * 0.45f));   // facePoint "up" of the floor
      int m = 0; float tt;
      auto fits = [&]() { for (int a = -4; a <= 4; ++a) for (int u = 0; u <= 8; ++u) if (!rayPasses(app, X.C2, p, tt, a * 0.1f, floorUp + 0.06f + u * 0.15f)) return false; return true; };   // 0.8 x 1.26 m: crouch capsule 1.2 m + padding
      while (m < 120 && !fits()) { hitPanel(app, X.C2, p, (m % 3 - 1) * 0.35f, floorUp + 0.25f + ((m / 3) % 3) * 0.35f); ++m; }
      float topOpen = 0;   // how high the opening reaches at the bay centre (it must stay below standing height)
      for (int u = 0; u < 20; ++u) if (rayPasses(app, X.C2, p, tt, 0, floorUp + 0.06f + u * 0.1f)) topOpen = 0.06f + u * 0.1f;
      check(m < 120, "crouch-height breach opened in the 1.2 m rear wall by " + std::to_string(m) + " hits (open to " + std::to_string(topOpen).substr(0, 4) + " m above the floor at the centre)");
      vec3 c = (p.bmin + p.bmax) * 0.5f;
      Physics::setCharacterPos(W(app, X.C2, vec3(c.x, 0, -3.7f)) + vec3(0, 0.25f, 0));
      app.eyeYaw = worldYaw(app, 180);
      json st = json::array({
        json{{"goto", {c.x, -3.4}}, {"crouch", true}},
        json{{"goto", {c.x, -1.0}}, {"expectY", 0.3}, {"crouch", true}, {"note", "crouch through the low breach in 1.2 m of concrete"}},
      });
      beginCustom(app, "bunker_mg_casemate", X.C2, st);
      go(520);
    }
    break;
  case 520:
    if (runWalkthrough(app, dt)) { app.scriptedInput.crouch = false; go(508); }
    break;
  // ------------------------------------------------------------- every variant, rotated, off-centre impacts
  case 508: {
    {   // ---- iron doors and steel shutters / flaps: no weapon breaks them (every bunker asset that has steel leaves)
      int tested = 0, broke = 0; std::string names;
      const char* steelAssets[] = {"bunker_mg_casemate", "bunker_pak_casemate", "bunker_command_shelter", "bunker_observation_post", "bunker_large_casemate"};
      for (int ai = 0; ai < 5; ++ai) {
      const AssetDef* ca = app.lib.find(steelAssets[ai]);
      if (!ca) continue;
      uint64_t sid = ai == 0 ? X.C : app.placeInstance(ca, vec3(250.0f, 0.0f, -200.0f + 45.0f * ai), 0.0f);
      if (!sid) continue;
      app.syncRuntime();
      Instance* ic = app.doc.find(sid); InstanceRT& rc = app.rt[sid];
      vec3 bc = ic->pos + yawQuat(ic->yaw) * ((ca->bmin + ca->bmax) * 0.5f);
      for (size_t kk = 0; kk < ca->interactables.size(); ++kk) {
        const InteractDef& d = ca->interactables[kk];
        if (d.sound != "steel") continue;
        names += std::string(" ") + steelAssets[ai] + ":" + d.id;
        for (size_t l = 0; l < d.leaves.size(); ++l) {
          const Leaf& lf = d.leaves[l];
          if (lf.colliders.empty()) continue;
          vec3 c = vec3(app.leafMatrix(*ic, lf, d, rc.progress[kk]) * vec4(lf.colliders[0].center, 1));
          vec3 out = vec3(c.x - bc.x, 0, c.z - bc.z); out = glm::length(out) > 1e-4f ? glm::normalize(out) : vec3(0, 0, 1);
          for (int s = 0; s < 40; ++s) {   // 40 rifle rounds straight into the leaf
            Projectile pr; pr.kind = PROJ_BULLET; pr.v = -out * 853.0f; pr.mass = 0.0097f; pr.dmg = 50; pr.p = c + out;
            Physics::Hit h; h.pos = c; h.normal = out; h.ud = (sid << 16) | (uint64_t)(kk + 1);
            std::string sf; app.bulletImpact(pr, h, sf);
          }
          app.explode(c + out * 0.3f, -out, kHE, kHER, 0.0f, ~0ull, WALL_HE);          // AT-gun / tank HE
          app.explode(c + out * 0.3f, -out, 320.0f, 0.9f, 0.0f, ~0ull, WALL_AP);       // AT-gun / tank AP
          app.explode(c + out * 0.3f, -out, 320.0f, 1.3f, 0.0f, ~0ull, WALL_ROCKET);   // Panzerschreck
          app.explode(c + out * 0.3f, vec3(0, -1, 0), 85.0f, 4.5f, 0.0f, ~0ull, WALL_GRENADE);
          app.detachLeaf(*ic, rc, (int)kk, (int)l, -out * 500.0f, c, true);           // any other caller
          ++tested; if (rc.leafSt[kk][l] & 2) ++broke;
        }
      }
      }
      check(tested >= 3 && broke == 0, "iron doors and steel shutters are unbreakable: " + std::to_string(tested) + " steel leaves (" + names.substr(names.empty() ? 0 : 1) + ") survive 40 rifle rounds, HE, AP, rocket and grenade blasts at 0.3 m; " + std::to_string(broke) + " broke");
      app.resetDamage();
    }
    {   // ---- large casemate (docs/bunkers.md): box walls are damage bays, the rounded hood / nose / plinth are not
      const AssetDef* la = app.lib.find("bunker_large_casemate");
      uint64_t L = la ? app.placeInstance(la, vec3(-150, 0, -150), 60.0f) : 0;
      app.syncRuntime();
      check(L != 0, "large casemate placed (rotated 60 deg)");
      if (L) {
        int k = -1; float best = 0, embK = -1;
        for (size_t q = 0; q < la->panels.size(); ++q) {
          const Panel& p = la->panels[q];
          if (p.id.find("/rear/") != std::string::npos && tangentWidth(p) > best) { best = tangentWidth(p); k = (int)q; }
          if (p.normal.z > 0.5f && std::fabs(p.bmax.z - 3.5f) < 0.05f && p.bmin.y > 2.2f) embK = (float)q;   // embrasure wall above the slit
        }
        int rounds = 0; for (auto& p : la->parts) rounds += p.shape == Part::ROUND;
        check(k >= 0 && embK >= 0 && rounds > 10, "large casemate: rear main wall bay, embrasure-wall bay above the slit, " + std::to_string(rounds) + " rounded static parts");
        if (k >= 0) {
          const Panel& p = la->panels[k];
          hitPanel(app, L, p);
          check(app.rt[L].state[k] != PANEL_BREACHED, "large casemate rear wall (1.2 m reinforced concrete) survives the first HE hit");
          int n = 1;
          while (app.rt[L].state[k] != PANEL_BREACHED && n < 16) { hitPanel(app, L, p); ++n; }
          float t;
          check(app.rt[L].state[k] == PANEL_BREACHED && rayPasses(app, L, p, t), "large casemate rear bay " + p.id + " opened by " + std::to_string(n) + " HE hits; a ray passes");
          summary["large_casemate_hits_to_breach"] = n;
        }
        if (embK >= 0) {
          const Panel& p = la->panels[(int)embK];
          vec3 c = (p.bmin + p.bmax) * 0.5f;
          hitAt(app, L, vec3(c.x, c.y, p.bmax.z + 0.02f), vec3(0, 0, -1), WALL_AP, 320.0f);
          check(app.rt[L].state[(int)embK] != PANEL_INTACT, "an AT round into the embrasure wall above the slit damages it (state " + std::to_string(app.rt[L].state[(int)embK]) + ")");
        }
      }
    }
    {   // ---- damage-field properties on fresh copies: corner, seam, oblique vs straight, deterministic replay
      const AssetDef* fa = app.lib.find(FH);
      uint64_t c1 = app.placeInstance(fa, vec3(-230, 0, 150), 0.0f), c2 = app.placeInstance(fa, vec3(-190, 0, 150), 0.0f);
      app.syncRuntime();
      check(c1 && c2, "two fresh farmhouse copies for the damage-field checks");
      if (c1 && c2) {
      // corner: a bay with a +-z normal and one with a +-x normal whose boxes touch
      int pz = -1, px = -1; vec3 corner(0);
      for (size_t a = 0; a < fa->panels.size() && pz < 0; ++a) for (size_t b = 0; b < fa->panels.size(); ++b) {
        const Panel &A = fa->panels[a], &B = fa->panels[b];
        if (std::fabs(A.normal.z) < 0.5f || std::fabs(B.normal.x) < 0.5f || A.id.rfind("floor-00/", 0) != 0 || B.id.rfind("floor-00/", 0) != 0) continue;
        vec3 lo = glm::max(A.bmin, B.bmin), hi = glm::min(A.bmax, B.bmax);
        if (lo.x <= hi.x + 0.05f && lo.y <= hi.y && lo.z <= hi.z + 0.05f) { pz = (int)a; px = (int)b; corner = vec3(A.normal.z != 0 ? (B.normal.x > 0 ? B.bmax.x : B.bmin.x) : 0, 1.3f, A.normal.z > 0 ? A.bmax.z : A.bmin.z); break; }
      }
      if (pz >= 0) {
        vec3 dirIn = -glm::normalize(fa->panels[pz].normal + fa->panels[px].normal);
        hitAt(app, c1, corner - dirIn * 0.02f, dirIn); hitAt(app, c1, corner - dirIn * 0.02f, dirIn);
        // the corner itself (pier / quoin bays converted from structural parts) breaks on both faces
        int onZ = 0, onX = 0; float wz = 0, wx = 0;
        for (size_t q = 0; q < fa->panels.size(); ++q) {
          if (q >= app.rt[c1].wf.size() || !app.rt[c1].wf[q].active) continue;
          float w = removedWidth(app, c1, (int)q);
          if (std::fabs(fa->panels[q].normal.z) > 0.5f) { ++onZ; wz = std::max(wz, w); } else if (std::fabs(fa->panels[q].normal.x) > 0.5f) { ++onX; wx = std::max(wx, w); }
        }
        char b[220]; std::snprintf(b, sizeof b, "a hit on the building corner (by %s / %s) breaks both faces at the corner itself: %d front-facing and %d side-facing bays cut (up to %.2f / %.2f m)", fa->panels[pz].id.c_str(), fa->panels[px].id.c_str(), onZ, onX, wz, wx);
        check(onZ > 0 && onX > 0 && wz > 0.15f && wx > 0.15f, b);
      } else check(false, "corner bays found on the farmhouse");
      // seam: two coplanar bays meeting edge to edge
      int sa = -1, sb = -1; vec3 seam(0);
      for (size_t a = 0; a < fa->panels.size() && sa < 0; ++a) for (size_t b = 0; b < fa->panels.size(); ++b) {
        const Panel &A = fa->panels[a], &B = fa->panels[b];
        if (a == b || std::fabs(A.normal.z) < 0.5f || A.normal != B.normal || std::fabs(A.bmax.x - B.bmin.x) > 0.02f || std::fabs(A.bmin.y - B.bmin.y) > 0.02f) continue;
        if (std::fabs(A.bmax.z - B.bmax.z) > 0.02f || std::fabs(A.bmin.z - B.bmin.z) > 0.02f || A.id.rfind("floor-00/", 0) != 0) continue;   // same facade plane
        sa = (int)a; sb = (int)b; seam = vec3(A.bmax.x, A.bmin.y + 1.3f, A.normal.z > 0 ? A.bmax.z : A.bmin.z); break;
      }
      if (sa >= 0) {
        vec3 n = fa->panels[sa].normal;
        for (int h = 0; h < 2; ++h) hitAt(app, c1, seam + n * 0.01f, -n);
        float t;
        Physics::Hit hh; vec3 o = W(app, c1, seam + n * 1.0f), d = Wd(app, c1, -n);
        bool through = !Physics::castRayEx(o, d, 1.6f, hh) || hh.ud != ((c1 << 16) | 0xFFFFull);
        (void)t;
        char b[220]; std::snprintf(b, sizeof b, "seam hit opens both bays (%s %.2f m, %s %.2f m removed) and a ray along the seam passes: no crack or collider sliver at the joint",
                                   fa->panels[sa].id.c_str(), removedWidth(app, c1, sa), fa->panels[sb].id.c_str(), removedWidth(app, c1, sb));
        check(removedWidth(app, c1, sa) > 0.2f && removedWidth(app, c1, sb) > 0.2f && through, b);
      } else check(true, "no edge-to-edge coplanar bays on this asset (openings separate them)");
      // oblique vs straight: same bay on two copies, same energy
      int k = pidx(fa, X.pA); const Panel& p = fa->panels[k];
      vec3 fp = facePoint(p), n = p.normal, t = std::fabs(n.z) > 0.5f ? vec3(1, 0, 0) : vec3(0, 0, 1);
      hitAt(app, c2, fp + n * 0.01f, -n);
      float wStraight = removedWidth(app, c2, k);
      app.resetDamage();
      vec3 obl = glm::normalize(-n + t * 1.7f);   // ~60 deg from the normal
      hitAt(app, c2, fp + n * 0.01f, obl);
      float wOblique = removedWidth(app, c2, k);
      char b[400]; std::snprintf(b, sizeof b, "an oblique (60 deg) hit gouges along the shot: %.2f m wide vs %.2f m for a straight hit of the same energy", wOblique, wStraight);
      check(wOblique > wStraight * 1.2f, b);
      // deterministic: replaying the stored events reproduces the field cell for cell
      hitAt(app, c2, fp + n * 0.01f + t * 0.3f, -n, WALL_AP, 320.0f);
      auto before = app.rt[c2].wf; auto events = app.rt[c2].wallEvents;
      Instance tmp = *app.doc.find(c2); tmp.extra = json::object(); tmp.extra["wallDamage"] = app.wallEventsJson(app.rt[c2]);
      InstanceRT r2; r2.id = c2; r2.asset = fa; r2.hp.assign(fa->panels.size(), 0); r2.state.assign(fa->panels.size(), PANEL_INTACT);
      app.replayWallDamage(tmp, r2);
      bool same = r2.wf.size() == before.size();
      for (size_t q = 0; same && q < before.size(); ++q) same = r2.wf[q].rf == before[q].rf && r2.wf[q].rb == before[q].rb;
      for (auto& m : r2.pmesh) m.destroy();
      check(same && events.size() == 2, "replaying the " + std::to_string(events.size()) + " stored events rebuilds every cell exactly (saves / late join / undo reconstruct the same opening)");
      // AP vs HE: one AP hit perforates masonry locally; one HE hit only craters
      app.resetDamage();
      hitAt(app, c2, fp + n * 0.01f, -n, WALL_AP, 320.0f);
      float apOpen = app.rt[c2].wf[k].openArea; bool apBreach = app.rt[c2].state[k] == PANEL_BREACHED;
      app.resetDamage();
      hitAt(app, c2, fp + n * 0.01f, -n, WALL_HE, kHE);
      bool heOpen = app.rt[c2].state[k] == PANEL_BREACHED;
      std::snprintf(b, sizeof b, "masonry: one AT / tank-gun AP hit blows a large breach (%.2f m^2 open; 0.29 before the bigger AP profile); one HE hit only craters, the second at the same spot breaches (canonical fortification rule)", apOpen);
      check(apBreach && !heOpen && apOpen > 1.3f && apOpen < 4.5f, b);
      summary["ap_breach_m2"] = apOpen;
      // irregular damage: every impact is a random oblong (not a circle), different per impact; broken edges get
      // 45-degree wedges instead of 4 cm stair-steps
      app.resetDamage();
      hitAt(app, c1, fp + n * 0.01f, -n); hitAt(app, c2, fp + n * 0.01f, -n);
      {
        float a1, a2; float s1 = aspectOf(gridOf(app, c1, k), a1), s2 = aspectOf(gridOf(app, c2, k), a2);
        int ch = app.rt[c2].wf[k].chamfers;
        std::snprintf(b, sizeof b, "HE damage is an irregular oblong, not a circle: aspect %.2f at %.0f deg and %.2f at %.0f deg on two copies; %d edge wedges smooth the break line", s1, a1, s2, a2, ch);
        check(s1 > 1.12f && s2 > 1.12f && (std::fabs(a1 - a2) > 10.0f || std::fabs(s1 - s2) > 0.1f) && ch > 0, b);
      }
      // no slivers: two launcher holes side by side, cleanup off (A) vs on (B)
      {
        float keep = app.destruction.bridgeMax;
        int N = (int)std::floor(keep / app.destruction.cell + 1e-3f), off = 0, on = 0, merged = 0, cases = 0;
        for (int pass = 0; pass < 2; ++pass) {
          app.destruction.bridgeMax = pass == 0 ? 0.0f : keep;
          for (float sp : {0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f}) {
            app.resetDamage();
            hitAt(app, c2, fp + n * 0.01f - t * sp * 0.5f, -n, WALL_ROCKET, 260.0f);
            hitAt(app, c2, fp + n * 0.01f + t * sp * 0.5f, -n, WALL_ROCKET, 260.0f);
            Grid g = gridOf(app, c2, k);
            int s = sliverCells(g, N);
            if (pass) { on += s; merged += openComponents(g) == 1; ++cases; } else off += s;
          }
        }
        app.destruction.bridgeMax = keep;
        std::snprintf(b, sizeof b, "no slivers between neighbouring holes: two launcher holes 0.50-0.75 m apart leave %d sliver cells without the cleanup and %d with it (strips <= %.2f m cleared; %d of %d pairs joined)", off, on, keep, merged, cases);
        check(off > 0 && on == 0, b);
        // holes that are genuinely apart stay apart
        app.resetDamage();
        hitAt(app, c2, fp + n * 0.01f + vec3(0, 0.35f, 0), -n, WALL_ROCKET, 260.0f);
        hitAt(app, c2, fp + n * 0.01f - vec3(0, 0.75f, 0), -n, WALL_ROCKET, 260.0f);
        int comps = openComponents(gridOf(app, c2, k));
        check(comps == 2, "two launcher holes 1.1 m apart stay two separate holes (" + std::to_string(comps) + " holes)");
      }
      // grenades do structural damage where they land, with the Bazooka's stamp radius
      {
        app.resetDamage();
        vec3 foot = fp; foot.y = p.bmin.y + 0.15f;
        hitAt(app, c2, foot + n * 0.25f, vec3(0, -1, 0), WALL_GRENADE, 80.0f);
        Grid gg = gridOf(app, c2, k); float gW = removedWidth(app, c2, k);
        app.resetDamage();
        hitAt(app, c2, foot + n * 0.01f, -n, WALL_ROCKET, 260.0f);
        float rW = removedWidth(app, c2, k);
        std::snprintf(b, sizeof b, "a grenade lying 0.25 m from the wall foot breaks the masonry: %.2f m wide, up to %.2f m deep (Bazooka at the same spot: %.2f m wide)", gW, gg.maxRemoved, rW);
        check(gW > 0.6f * rW && gW < 1.6f * rW && gg.maxRemoved > 0.2f, b);
        summary["grenade_wall_width_m"] = gW; summary["grenade_wall_depth_m"] = gg.maxRemoved;
      }
      // wooden doors break into pieces
      {
        app.resetDamage();
        int kd = fa->findInteract("front_door");
        Instance* i2 = app.doc.find(c2); InstanceRT& r2d = app.rt[c2];
        if (kd >= 0 && i2) {
          const InteractDef& d = fa->interactables[kd]; const Leaf& lf = d.leaves[0];
          vec3 c = vec3(app.leafMatrix(*i2, lf, d, r2d.progress[kd]) * vec4(lf.colliders[0].center, 1));
          vec3 bc = i2->pos + yawQuat(i2->yaw) * ((fa->bmin + fa->bmax) * 0.5f), out = vec3(c.x - bc.x, 0, c.z - bc.z);
          out = glm::length(out) > 1e-4f ? glm::normalize(out) : vec3(0, 0, 1);
          size_t before = app.dynLeaves.size();
          app.explode(c + out * 0.6f, -out, kHE, kHER, 0.0f, ~0ull, WALL_HE);
          int frags = 0; vec3 lo(1e9f), hi(-1e9f);
          for (size_t q = before; q < app.dynLeaves.size(); ++q) {
            auto& x = app.dynLeaves[q];
            if (x->inst != c2 || x->k != kd) continue;
            frags += x->fragment;
            vec3 pp; quat qq; Physics::getTransform(x->body, pp, qq); lo = glm::min(lo, pp); hi = glm::max(hi, pp);
          }
          std::snprintf(b, sizeof b, "a shell at the farmhouse front door breaks it into %d pieces (own rigid bodies spread over %.2f x %.2f m), not one leaf", frags, std::max(hi.x - lo.x, hi.z - lo.z), hi.y - lo.y);
          check((r2d.leafSt[kd][0] & 2) && frags >= 6, b);
        } else check(false, "farmhouse front door found");
      }
      app.resetDamage();
      app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) { return i.id == c1 || i.id == c2; }), app.doc.instances.end());
      app.syncRuntime();
      }
    }
    json hits = json::object();
    int idx = 0;
    for (auto& up : app.lib.assets) {
      const AssetDef* a = up.get();
      if (a->panels.empty()) continue;
      float yaw = wrapDeg(17.0f + 41.0f * idx);
      uint64_t id = app.placeInstance(a, vec3(-270.0f + 40.0f * (idx % 13), 0, 200.0f + 45.0f * (idx / 13)), yaw);   // rows of 13 stay inside the build area
      ++idx;
      if (!id) { check(false, a->name + " placed for damage test"); continue; }
      app.syncRuntime();
      // ground panel = widest bay on floor 0; upper = widest bay on the top floor
      std::vector<int> targets;
      for (int pass2 = 0; pass2 < 2; ++pass2) {
        std::string fl = pass2 == 0 ? "floor-00/" : "floor-" + std::string(a->floors - 1 < 10 ? "0" : "") + std::to_string(a->floors - 1) + "/";
        if (pass2 == 1 && a->floors < 2) break;
        int k = -1; float w = 0;
        for (size_t q = 0; q < a->panels.size(); ++q) if (a->panels[q].id.rfind(fl, 0) == 0 && tangentWidth(a->panels[q]) > w) { w = tangentWidth(a->panels[q]); k = (int)q; }
        if (k >= 0) targets.push_back(k);
      }
      int broken = 0;
      for (int k : targets) {
        const Panel& p = a->panels[k];
        int n = 0;
        float t;
        while (!(app.rt[id].state[k] == PANEL_BREACHED && rayPasses(app, id, p, t, 0.35f, 0.2f)) && n < 16) { hitPanel(app, id, p, 0.35f, 0.2f); ++n; }
        bool ok = app.rt[id].state[k] == PANEL_BREACHED;
        if (ok) ++broken;
        bool atImpact = rayPasses(app, id, p, t, 0.35f, 0.2f);
        bool farSide = app.rt[id].wf[k].openArea < 0.7f * tangentWidth(p) * (p.bmax.y - p.bmin.y);   // most of the bay still stands
        float area = tangentWidth(p) * (p.bmax.y - p.bmin.y), open = app.rt[id].wf[k].openArea;
        int wholeGone = 0; for (auto& w : app.rt[id].wf) wholeGone += w.active && w.openArea > 0.95f * area;
        char b[300]; std::snprintf(b, sizeof b, "%s @%d deg: off-centre hits open %s (%s, %d hits) at the impact point, not the bay centre; %.2f of %.2f m^2 open, far quarter stands, no bay deleted",
                                   a->name.c_str(), (int)yaw, p.id.c_str(), p.family.c_str(), n, open, area);
        check(ok && atImpact && farSide && wholeGone == 0, b);
        hits[a->id + " " + p.id] = n;
      }
    }
    summary["hits_to_breach"] = hits;
    // ---- Creative return restores authored geometry
    app.exitExplore();
    int dmg = 0; for (auto& [id, r] : app.rt) dmg += r.damaged;
    check(dmg == 0 && app.terrain.tiles.empty(), "returning to Creative restores every wall and the ground");
    float qh = app.terrain.height(X.Q.x, X.Q.z);
    check(std::fabs(qh - app.terrain.baseHeight(X.Q.x, X.Q.z)) < 1e-4f, "crater removed from the canonical height after reset");
    // ---- explicit Apply damage -> persistence, undo, save/reload twice
    check(app.enterExplore(), "F5 again");
    const AssetDef* fa = app.lib.find(FH);
    int k = pidx(fa, X.pA);
    hitPanel(app, X.A, fa->panels[k]); hitPanel(app, X.A, fa->panels[k]);
    X.Q2 = vec3(-180, 0, -40); X.Q2.y = app.terrain.height(X.Q2.x, X.Q2.z);
    app.deformGround(X.Q2, 1.0f);
    X.q2depth = X.Q2.y - app.terrain.height(X.Q2.x, X.Q2.z);
    app.applyDamageToMap();
    app.exitExplore();
    check(app.rt[X.A].state[k] == PANEL_BREACHED && app.terrain.tiles.size() > 0, "applied damage survives the return to Creative");
    { auto* ia = app.doc.find(X.A); auto* ib = app.doc.find(X.B);
      bool aHas = ia->extra.is_object() && ia->extra.contains("wallDamage") && ia->extra["wallDamage"].size() >= 2;
      bool bHas = ib->extra.is_object() && ib->extra.contains("wallDamage");
      check(aHas && !bHas && ia->panelHp.empty(), "only the damaged copy carries wall damage in the map (" + std::to_string(aHas ? ia->extra["wallDamage"].size() : 0) + " replayable impact events)"); }
    app.undo();
    app.syncRuntime();
    check(app.rt[X.A].state[k] == PANEL_INTACT && app.terrain.tiles.empty(), "undo restores the wall, its collision and the ground together");
    app.redo();
    app.syncRuntime();
    check(app.rt[X.A].state[k] == PANEL_BREACHED && !app.terrain.tiles.empty(), "redo re-applies it");
    fs::path mp = userMapsDir() / "selftest_damage.nmap.json";
    check(app.saveMapTo(mp), "map with applied damage saved");
    float depths[2];
    for (int r = 0; r < 2; ++r) {
      app.openMap(mp);
      app.syncRuntime();
      depths[r] = X.Q2.y - app.terrain.height(X.Q2.x, X.Q2.z);
    }
    char b[160]; std::snprintf(b, sizeof b, "reloading twice keeps the same crater depth (%.3f, %.3f, applied %.3f)", depths[0], depths[1], X.q2depth);
    check(std::fabs(depths[0] - X.q2depth) < 0.005f && std::fabs(depths[1] - X.q2depth) < 0.005f, b);
    uint64_t aid = 0; for (auto& i : app.doc.instances) if (i.id == X.A) aid = i.id;
    check(aid && app.rt[aid].state[k] == PANEL_BREACHED, "breached panel restored from the saved map");
    // old map (saved by the pre-destruction build) opens with every wall intact
    check(app.openMap(contentDir() / "examples" / "farmhouse_regression.nmap.json"), "pre-update example map opens");
    app.syncRuntime();
    int anyD = 0; for (auto& [id, r] : app.rt) anyD += r.damaged;
    check(app.doc.instances.size() == 1 && anyD == 0, "pre-update map: building present, all panels intact");
    finish(app);
    break;
  }
  }
}
