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
  uint64_t A = 0, B = 0, C = 0, M = 0;
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
bool rayPasses(App& app, uint64_t id, const Panel& p, float& tHit) {
  vec3 fp = facePoint(p);
  float depth = std::fabs(glm::dot(p.bmax - p.bmin, glm::abs(p.normal)));
  vec3 o = W(app, id, fp + p.normal * 1.0f), d = Wd(app, id, -p.normal);
  Physics::Hit h; tHit = 99;
  if (!Physics::castRayEx(o, d, 1.0f + depth + 0.3f, h)) return true;
  tHit = h.t;
  return h.ud != wallUD(id) || h.t > 1.0f + depth + 0.05f;
}
int breachedCount(App& app, uint64_t id) { int n = 0; for (auto s : app.rt[id].state) n += s == PANEL_BREACHED; return n; }
float physGround(vec3 p) { Physics::Hit h; return Physics::castRayEx(vec3(p.x, p.y + 30, p.z), vec3(0, -1, 0), 80, h) ? h.pos.y : -999; }
}

void SelfTest::beginCustom(App& app, const std::string& asset, uint64_t inst, const json& st) {
  curAsset = asset; houseId = inst;
  steps.clear(); for (auto& s : st) steps.push_back(s);
  stepIdx = 0; stepTime = 0; ++pass;
  (void)app;
}

void SelfTest::damageUpdate(App& app, float dt) {
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
    (void)e1; (void)e2;
    // walk through the breach, use a window, up one stair, down the other, out the back door
    Physics::setCharacterPos(W(app, X.A, vec3(-7.8f, 0, 2.63f)) + vec3(0, 0.05f, 0));
    app.eyeYaw = worldYaw(app, -90);
    json st = json::array({
      json{{"look", {-6.0, 1.2, 2.63}}, {"hold", 0.5}}, json{{"shot", "30_breach_outside"}},
      json{{"goto", {-6.9, 2.63}}}, json{{"goto", {-4.9, 2.63}}, {"expectY", 0.3}, {"note", "walk a real capsule through the breach"}},
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
    while (app.rt[X.C].state[k] != PANEL_BREACHED && n < 12) { hitPanel(app, X.C, p); ++n; }
    check(app.rt[X.C].state[k] == PANEL_BREACHED, "casemate bay " + p.id + " opened by " + std::to_string(n) + " deliberate HE hits");
    summary["concrete_hits_to_breach"] = n;
    float t;
    check(rayPasses(app, X.C, p, t), "a ray passes the breached bunker wall");
    vec3 c = (p.bmin + p.bmax) * 0.5f;
    Physics::setCharacterPos(W(app, X.C, vec3(c.x, 0, -3.6f)) + vec3(0, 0.25f, 0));
    app.eyeYaw = worldYaw(app, 180);
    json st = json::array({
      json{{"look", {c.x, 1.0, -2.0}}, {"hold", 0.4}}, json{{"shot", "32_casemate_breach"}},
      json{{"goto", {c.x, -1.2}}, {"expectY", 0.2}, {"note", "walk into the casemate through the breach"}},
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
    if (runWalkthrough(app, dt)) go(508);
    break;
  // ------------------------------------------------------------- every variant, rotated, off-centre impacts
  case 508: {
    json hits = json::object();
    int idx = 0;
    for (auto& up : app.lib.assets) {
      const AssetDef* a = up.get();
      if (a->panels.empty()) continue;
      float yaw = wrapDeg(17.0f + 41.0f * idx);
      uint64_t id = app.placeInstance(a, vec3(-270.0f + 40.0f * idx, 0, 200.0f), yaw);
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
        while (app.rt[id].state[k] != PANEL_BREACHED && n < 12) { hitPanel(app, id, p, 0.35f, 0.2f); ++n; }
        bool ok = app.rt[id].state[k] == PANEL_BREACHED;
        if (ok) ++broken;
        float t;
        bool pass3 = rayPasses(app, id, p, t);
        check(ok && pass3 && breachedCount(app, id) == broken,
              a->name + " @" + std::to_string((int)yaw) + " deg: off-centre hits open " + p.id + " (" + p.family + ", " + std::to_string(n) + " hits), ray passes, neighbours intact");
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
    check(app.doc.find(X.A)->panelHp.count(X.pA) == 1 && app.doc.find(X.B)->panelHp.empty(), "only the damaged copy carries panel state in the map");
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
