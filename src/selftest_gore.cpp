// --selftest-gore (W7, SPEC 7): the gore system in a real Singleplayer match on the shipped 3-point village.
// The match is paused after the first spawn; the test places one attacker bot in front of a wall and shoots it through
// the real damage path (Game::projectileSegment -> FleshHit -> goreFleshHit). Covers: config, teardrop maths,
// seeded spray (same seed = same droplets, counts / cones / speeds / energy), wound decals following their bone through
// a pose change and the death pose, droplet landing (teardrops, water ripple), splatter ray, spurting, pool growth,
// ageing, hard budgets (256 / 24 / 1500, oldest fade first), Off mode, protected and helmet hits, mist-only range, the
// LAN event round trip, rendering (decals visible, no shader errors) and the cost per hit / at full budgets.
#include "selftest.h"
#include "gore.h"
#include "decals.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include <imgui.h>
#include <chrono>
#include <fstream>
#include <cmath>
#include <cstdarg>
#include <algorithm>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr;
int mode = -1;
bool started = false;
fs::path pendingShot; bool probeWant = false; vec3 probeAt{0}; vec3 probeCol{-1}; vec3 probeBefore{-1};
int tgt = -1, tgt2 = -1;           // soldier slots used
vec3 camFeet{0}, wallDir{0, 0, -1}, tgtFeet{0}; float wallDist = 0;
json out = json::object();

fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
char B[600];
bool chk(bool ok, const char* fmt, ...) { va_list a; va_start(a, fmt); std::vsnprintf(B, sizeof B, fmt, a); va_end(a); return T->check(ok, B); }

float yawToward(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
void placeSoldier(Soldier& s, vec3 feet, vec3 face) {
  s.feet = feet; s.vel = vec3(0); s.speed = 0; s.yaw = yawToward(face); s.br.aimYaw = s.yaw; s.br.aimPitch = 0;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, feet);
}
void lookAt(App& app, vec3 feet, vec3 target) {
  Physics::setCharacterPos(feet);
  vec3 e = feet + vec3(0, 1.62f, 0), d = target - e;
  app.eyeYaw = yawToward(d); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  app.R.resetTemporal();
}
vec3 chestOf(const Soldier& s) { return vec3(s.world * vec4(s.pose.joint[CJ_SPINE1], 1)); }
// a real round from `from` through `at`: the damage path builds the FleshHit
bool shoot(App& app, vec3 from, vec3 at, const char* weapon = "kar98k") {
  Game& G = *gGame;
  int w = app.weaponIndex(weapon);
  Projectile p; p.kind = PROJ_BULLET; p.owner = -1; p.ammo = w >= 0 ? app.weaponDefs[w]->container : ""; vec3 d = glm::normalize(at - from);
  p.p = from; p.v = d * 760.0f; p.life = 0.01f;
  float len = glm::length(at - from) + 3.0f, tHit = len;
  return G.projectileSegment(p, from, d * len, len, tHit);
}
void simulate(App& app, float secs, bool poses = true) {   // gore (and the paused match clock) without rendering
  Game& G = *gGame;
  int n = (int)std::lround(secs * 60);
  for (int k = 0; k < n; ++k) { G.t += 1.0 / 60; if (poses) G.posesStep(1.0f / 60); goreStep(app, 1.0f / 60); }
}
size_t boneCount(int slot) { auto& S = goreState(); auto it = S.sold.find(slot); return it == S.sold.end() ? 0 : it->second.decals.size(); }
void shot(const std::string& name) { pendingShot = outDir() / (name + ".png"); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }

void build(App& app) {
  // ---- setup: the shipped 3-point village, Singleplayer from the Play flow
  push("setup", [](App& a, float t) {
    if (t == 0) {
      chk(a.openMap(contentDir() / "examples" / "village_assault_3.nmap.json"), "open village_assault_3");
      gUi.loaded = false; gUi.matchScenario = json();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
      chk(a.enterExplore() && gGame->active, "Singleplayer match starts (%s)", gGame->setupError.c_str());
      a.useScripted = true; a.scriptedInput = ExploreInput{};
      goreSetLevel(GoreLevel::Full);
      return false;
    }
    if (t < 2.5f) return false;
    Game& G = *gGame; G.paused = true;
    for (auto& s : G.sold) if (s.alive && !s.human && !s.localPlayer) { if (tgt < 0) tgt = s.slot; else if (tgt2 < 0 && s.slot != tgt) tgt2 = s.slot; }
    chk(tgt >= 0 && tgt2 >= 0, "two bots to shoot (slots %d, %d); match paused", tgt, tgt2);
    if (tgt < 0 || tgt2 < 0) { cur = seq.size() - 2; return true; }
    // a spot with a wall 5-9 m away: the target stands 1.6 m in front of it (the 2.5 m splatter ray reaches it)
    Soldier* L = G.local();
    vec3 base = L ? L->feet : vec3(0, a.terrain.height(0, 30), 30);
    float bestScore = -1;
    for (int r = 0; r < 7; ++r) for (int k = 0; k < 24; ++k) {
      vec3 c = base + vec3(std::sin(r * 1.7f) * r * 4.0f, 0, std::cos(r * 1.7f) * r * 4.0f); c.y = a.terrain.height(c.x, c.z);
      if (!Physics::shapeFitsAt(Physics::STAND, c + vec3(0, 0.05f, 0))) continue;
      vec3 d(std::sin(k * 0.2618f), 0, std::cos(k * 0.2618f));
      Physics::Hit h;
      if (!Physics::castRayEx(c + vec3(0, 1.3f, 0), d, 9.0f, h) || h.t < 4.5f || std::fabs(h.normal.y) > 0.3f || h.ud == 0) continue;
      int flat = 0; vec3 sd = glm::normalize(glm::cross(d, vec3(0, 1, 0)));   // a building wall, not a trunk: flat across 1 m and up
      for (vec3 o : {sd * 0.5f, -sd * 0.5f, vec3(0, 0.7f, 0), vec3(0, -0.5f, 0)}) { Physics::Hit q; if (Physics::castRayEx(c + vec3(0, 1.3f, 0) + o, d, 9.0f, q) && std::fabs(q.t - h.t) < 0.35f && glm::dot(q.normal, h.normal) > 0.9f) ++flat; }
      if (flat < 3) continue;
      vec3 tf = c + d * (h.t - 1.6f); tf.y = a.terrain.height(tf.x, tf.z);
      Physics::Hit h2; bool clear = !Physics::castRayEx(c + vec3(0, 1.4f, 0), d, h.t - 1.7f, h2);
      if (!clear || std::fabs(tf.y - c.y) > 0.6f) continue;
      float score = 10 - std::fabs(h.t - 6.5f) + glm::dot(-d, h.normal);
      if (score > bestScore) { bestScore = score; camFeet = c; wallDir = d; wallDist = h.t; tgtFeet = tf; }
    }
    chk(bestScore > 0, "test spot: camera at (%.1f, %.1f), wall %.1f m away, target 1.6 m in front of it", camFeet.x, camFeet.z, wallDist);
    Soldier& S = G.sold[tgt];
    S.hp = 1e6f; S.protectUntil = -1;
    placeSoldier(S, tgtFeet, -wallDir);
    placeSoldier(G.sold[tgt2], tgtFeet + glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * 1.2f, -wallDir);
    G.sold[tgt2].hp = 1e6f; G.sold[tgt2].protectUntil = -1;
    G.posesStep(0);
    lookAt(a, camFeet, chestOf(S));
    return true; });

  // ---- pure maths
  push("config + maths", [](App& a, float t) {
    (void)a; (void)t;
    GoreState& S = goreState(); const GoreCfg& c = S.cfg;
    chk(c.maxWorldDecals == 256 && c.maxDecalsPerCharacter == 24 && c.maxDroplets == 1500 && c.mistOnlyBeyond == 60 && c.level == GoreLevel::Full,
        "gore.json budgets: %d world decals, %d per character, %d droplets, mist only beyond %.0f m, default level full", c.maxWorldDecals, c.maxDecalsPerCharacter, c.maxDroplets, c.mistOnlyBeyond);
    // teardrop elongation: aspect = 1/sin(impact angle), clamped to 6, tail along travel
    struct Cs { float deg, want; } cs[] = {{90, 1.0f}, {60, 1.1547f}, {30, 2.0f}, {20, 2.9238f}, {10, 5.7588f}, {9.594f, 6.0f}, {5, 6.0f}, {1, 6.0f}};
    bool ok = true; std::string s;
    for (auto& k : cs) {
      vec3 v = vec3(std::cos(radians(k.deg)), -std::sin(radians(k.deg)), 0) * 4.0f; float asp; vec3 tail;
      goreTeardrop(v, vec3(0, 1, 0), c.maxAspect, asp, tail);
      ok = ok && std::fabs(asp - k.want) < 2e-3f && (k.deg > 89 || glm::dot(tail, vec3(1, 0, 0)) > 0.9999f);
      char b[48]; std::snprintf(b, sizeof b, " %.0f deg->%.3f", k.deg, asp); s += b;
    }
    { float asp; vec3 tail; goreTeardrop(vec3(0.3f, -0.1f, 2.0f), glm::normalize(vec3(0, 0, -1)), 6, asp, tail); ok = ok && glm::dot(tail, glm::normalize(vec3(0.3f, -0.1f, 0))) > 0.999f; }   // a wall: tail along the travel projected on the wall
    chk(ok, "teardrop elongation 1/sin(angle) clamped to 6, tail along the travel direction:%s", s.c_str());
    // ageing: fresh red -> maroon-brown over ~60 s
    vec3 f = goreAgeTint(0, c.ageToBrown), m = goreAgeTint(30, c.ageToBrown), o = goreAgeTint(60, c.ageToBrown), late = goreAgeTint(300, c.ageToBrown);
    chk(f.r > 10 * f.g && o.g / o.r > 0.35f && m.g > f.g && o.g > m.g && late == o, "decal ageing: fresh (%.2f %.2f %.2f) -> 30 s (%.2f %.2f %.2f) -> 60 s brown (%.2f %.2f %.2f), stable after", f.r, f.g, f.b, m.r, m.g, m.b, o.r, o.g, o.b);
    // seeded spray
    FleshHit h; h.pos = vec3(1, 1.3f, 2); h.dir = glm::normalize(vec3(0.2f, -0.05f, 1)); h.normal = -h.dir; h.energy = 1; h.exits = true; h.seed = 12345;
    std::vector<GoreDropInit> b1, f1, b2, f2, b3, f3;
    goreGenerateSpray(h, h.pos, h.pos + h.dir * 0.2f, true, c, 1, b1, f1);
    goreGenerateSpray(h, h.pos, h.pos + h.dir * 0.2f, true, c, 1, b2, f2);
    FleshHit h2 = h; h2.seed = 12346; goreGenerateSpray(h2, h.pos, h.pos + h.dir * 0.2f, true, c, 1, b3, f3);
    auto same = [](const std::vector<GoreDropInit>& x, const std::vector<GoreDropInit>& y) { if (x.size() != y.size()) return false; for (size_t k = 0; k < x.size(); ++k) if (x[k].p != y[k].p || x[k].v != y[k].v || x[k].r != y[k].r) return false; return true; };
    chk(same(b1, b2) && same(f1, f2) && !(same(b1, b3) && same(f1, f3)) && goreSprayHash(h) == goreSprayHash(h) && goreSprayHash(h) != goreSprayHash(h2),
        "same seed gives the same droplets (%zu back + %zu forward, bit-identical); the next seed differs", b1.size(), f1.size());
    // ranges over many seeds, rifle vs pistol
    int bmin = 999, bmax = 0, fmin = 999, fmax = 0; float worstB = 0, worstF = 0; bool spd = true, rad = true;
    double rifleN = 0, pistolN = 0, rifleV = 0, pistolV = 0; int nv1 = 0, nv2 = 0;
    for (int k = 0; k < 400; ++k) {
      FleshHit q = h; q.seed = 1000 + k; q.energy = k & 1 ? 1.0f : 0.4f;
      std::vector<GoreDropInit> bb, ff; goreGenerateSpray(q, q.pos, q.pos + q.dir * 0.2f, true, c, 1, bb, ff);
      bmin = std::min(bmin, (int)bb.size()); bmax = std::max(bmax, (int)bb.size()); fmin = std::min(fmin, (int)ff.size()); fmax = std::max(fmax, (int)ff.size());
      for (auto& d : bb) { float sp = glm::length(d.v); worstB = std::max(worstB, degrees(std::acos(std::clamp(glm::dot(d.v / sp, -q.dir), -1.0f, 1.0f)))); spd = spd && sp >= 1 - 1e-4f && sp <= 4 + 1e-4f; rad = rad && d.r >= 0.0005f - 1e-7f && d.r <= 0.003f; }
      for (auto& d : ff) { float sp = glm::length(d.v); worstF = std::max(worstF, degrees(std::acos(std::clamp(glm::dot(d.v / sp, q.dir), -1.0f, 1.0f)))); spd = spd && sp >= 3 - 1e-4f && sp <= 9 + 1e-4f; rad = rad && d.r >= 0.0005f - 1e-7f && d.r <= 0.003f + 1e-7f;
        if (k & 1) { rifleV += sp; ++nv1; } else { pistolV += sp; ++nv2; } }
      (k & 1 ? rifleN : pistolN) += ff.size();
    }
    chk(bmin >= 10 && bmax <= 25 && fmin >= 20 && fmax <= 60 && worstB <= 45.01f && worstF <= 25.01f && spd && rad,
        "400 seeds: back-spatter %d-%d droplets within %.1f deg (cone <= 45) at 1-4 m/s, forward spray %d-%d within %.1f deg (<= 25) at 3-9 m/s, radius 0.5-3 mm", bmin, bmax, worstB, fmin, fmax, worstF);
    chk(rifleN / 200 > pistolN / 200 + 8 && rifleV / nv1 > pistolV / nv2 + 1, "energy scales the forward spray: rifle %.1f droplets at %.2f m/s mean, pistol %.1f at %.2f m/s", rifleN / 200, rifleV / nv1, pistolN / 200, pistolV / nv2);
    return true; });

  // ---- a rifle hit on the target: wounds on the mesh, spray, splatter on the wall
  push("rifle hit", [](App& a, float t) {
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    static int hits0 = 0, splats0 = 0, spawned0 = 0; static size_t bone0 = 0;
    if (t == 0) {
      // "before" colour probe at the chest
      probeAt = chestOf(V) - wallDir * 0.12f; probeWant = true; probeBefore = vec3(-1);
      return false;
    }
    if (probeBefore.x < 0 && probeCol.x >= 0) { probeBefore = probeCol; probeCol = vec3(-1); }
    if (probeBefore.x < 0) return false;
    if (hits0 == 0 && spawned0 == 0) {
      hits0 = gGoreHitsReported + 1; splats0 = S.st.splats; spawned0 = S.st.spawned + 1; bone0 = boneCount(tgt);
      vec3 eye = camFeet + vec3(0, 1.62f, 0);
      { vec3 side = glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))); vec3 f = tgtFeet - wallDir * 2.2f + side * 1.6f; f.y = a.terrain.height(f.x, f.z); lookAt(a, f, chestOf(V) + wallDir * 0.4f); }   // watch the spray from the side
      int miss0 = S.st.meshMisses;
      bool hit = shoot(a, eye, chestOf(V));
      SoldierGore& rec = S.sold[tgt];
      chk(hit && gGoreHitsReported == hits0 && S.st.meshMisses == miss0, "Kar98k round hits the target: one FleshHit through the damage path, placed by a ray cast on the posed mesh");
      int entry = -1, exitI = -1, soak = 0; for (int k = 0; k < (int)rec.decals.size(); ++k) { if (rec.decals[k].kind == GD_ENTRY) entry = k; if (rec.decals[k].kind == GD_EXIT) exitI = k; if (rec.decals[k].kind == GD_SOAK) ++soak; }
      vec3 ep, en, xp, xn;
      bool okE = entry >= 0 && gorePosedPoint(tgt, rec.decals[entry].tri, rec.decals[entry].bary, ep, en);
      bool okX = exitI >= 0 && gorePosedPoint(tgt, rec.decals[exitI].tri, rec.decals[exitI].bary, xp, xn);
      vec3 d = glm::normalize(chestOf(V) - eye);
      chk(okE && rec.decals[entry].radius * 2 >= 0.02f - 1e-5f && rec.decals[entry].radius * 2 <= 0.04f + 1e-5f && glm::dot(en, -d) > 0.2f,
          "entry wound on the body surface facing the shooter: %.1f cm across (2-4), normal . back-dir %.2f", rec.decals[entry >= 0 ? entry : 0].radius * 200, okE ? glm::dot(en, -d) : 0.0f);
      chk(okX && glm::dot(xp - ep, d) > 0.1f && rec.decals[exitI].radius * 2 >= 0.04f - 1e-5f && rec.decals[exitI].radius * 2 <= 0.08f + 1e-5f && glm::dot(xn, d) > 0.0f,
          "rifle round exits: exit wound %.1f cm across (4-8) on the far side, %.2f m further along the path", okX ? rec.decals[exitI].radius * 200 : 0.0f, okX ? glm::dot(xp - ep, d) : 0.0f);
      chk(soak >= 1 && boneCount(tgt) == bone0 + 2 + soak, "%zu bone decals on the soldier (entry, exit and %d soak stains)", boneCount(tgt) - bone0, soak);
      chk(S.st.splats == splats0 + 1, "surface splatter: the 2.5 m ray behind the target put a splat on the wall %.1f m behind it", wallDist - (wallDist - 1.6f));
      int thisHit = S.st.spawned - (spawned0 - 1);
      chk(thisHit >= 30 && thisHit <= 85, "%d droplets from this hit (back-spatter 10-25 + forward spray 20-60; %zu in flight in all, %d gore hits so far) and %zu mist puffs", thisHit, S.drops.size(), S.st.goreHits, S.puffs.size());
      out["dropletsPerRifleHit"] = S.drops.size();
      return false;
    }
    if (t < 0.035f) return false;
    if (t < 0.045f) { shot("gore_spray"); return false; }
    if (!S.drops.empty() && t < 4) return false;
    chk(S.drops.empty() && S.st.landed > 20, "all droplets came down (quadratic drag + gravity) within %.1f s: %d landed as stains", t, S.st.landed);
    int specks = 0, stamps = 0, elong = 0; for (auto& d : S.world) if (d.kind == GD_SPECKS) { ++specks; for (auto& s : d.stamps) { ++stamps; if (s.aspect > 1.5f) ++elong; } }
    chk(specks >= 1 && stamps > 20 && elong > 0, "landed droplets: %d stains in %d decals (bursts share decals), %d stretched into teardrops", stamps, specks, elong);
    return true; });

  push("wound close-up", [](App& a, float t) {
    Game& G = *gGame; Soldier& V = G.sold[tgt];
    if (t == 0) {
      vec3 eye = camFeet + vec3(0, 1.62f, 0);
      for (int k = 0; k < 3; ++k) shoot(a, eye, chestOf(V) + vec3(0.05f * (k - 1), 0.08f * k - 0.12f, 0));
      simulate(a, 3.0f);
      lookAt(a, tgtFeet - wallDir * 1.5f + glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * 0.3f, chestOf(V));
      probeAt = chestOf(V) - wallDir * 0.12f; probeWant = true; probeCol = vec3(-1);
      return false;
    }
    if (t < 0.4f) return false;
    if (t < 0.42f) { shot("gore_wounds"); return false; }
    if (probeCol.x < 0) return t > 2;
    const DecalGpuStats& gs = decalsLastStats();
    chk(gs.charsDrawn >= 1 && gs.boneDrawn >= 4 && a.R.shaderErrors == 0, "bone decals drawn on %d skinned soldier(s) (%d decals), %d world decals; %d shader errors", gs.charsDrawn, gs.boneDrawn, gs.worldDrawn, a.R.shaderErrors);
    out["probeBefore"] = {probeBefore.r, probeBefore.g, probeBefore.b}; out["probeAfter"] = {probeCol.r, probeCol.g, probeCol.b};
    chk(true, "chest colour before hits (%.0f %.0f %.0f) and after (%.0f %.0f %.0f) (informational: framing differs)", probeBefore.r, probeBefore.g, probeBefore.b, probeCol.r, probeCol.g, probeCol.b);
    return true; });

  push("splatter view", [](App& a, float t) {
    if (t == 0) {   // the splat on the wall behind the target, from beside the target
      GoreState& S = goreState(); vec3 sp = tgtFeet + wallDir * 1.6f + vec3(0, 1.2f, 0);
      for (auto& d : S.world) if (d.kind == GD_SPLAT) sp = d.pos;
      vec3 side = glm::normalize(glm::cross(wallDir, vec3(0, 1, 0)));
      vec3 f = sp - wallDir * 2.4f + side * 0.9f; f.y = a.terrain.height(f.x, f.z);
      lookAt(a, f, sp - vec3(0, 0.25f, 0)); return false; }
    if (t < 0.4f) return false;
    shot("gore_splatter");
    return true; });

  // ---- decals follow their bone (pose change, move, death pose)
  push("follow bone", [](App& a, float t) {
    (void)t;
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    SoldierGore& rec = S.sold[tgt];
    const CharModel& M = *G.models[V.model];
    struct Ref { vec3 p; vec3 relHead; int joint; };
    std::vector<Ref> before;
    for (auto& d : rec.decals) { vec3 p, n; gorePosedPoint(tgt, d.tri, d.bary, p, n); vec3 jh = vec3(V.world * V.pose.delta[d.joint] * vec4(M.jointHead[d.joint], 1)); before.push_back({p, glm::inverse(glm::mat3(V.world * V.pose.delta[d.joint])) * (p - jh), d.joint}); }
    // weight of the decal's bone at its triangle: >= 0.95 = rigidly on that bone, else skinned across neighbours
    auto boneWeight = [&](const BoneDecal& d) { float w = 1; for (int i = 0; i < 3; ++i) { const auto& sv = M.verts[M.indices[d.tri * 3 + i]]; float wi = 0; for (int q = 0; q < 4; ++q) if ((int)sv.j[q] == d.joint) wi += sv.w[q]; w = std::min(w, wi); } return w; };
    auto measure = [&](const char* what, float minMove) {
      float worstRigid = 0, worstBlend = 0, minMoved = 1e9f, worstGpu = 0; int nRigid = 0;
      for (size_t k = 0; k < rec.decals.size() && k < before.size(); ++k) {
        auto& d = rec.decals[k]; vec3 p, n; gorePosedPoint(tgt, d.tri, d.bary, p, n);
        mat4 Bm = V.world * V.pose.delta[d.joint];
        vec3 jh = vec3(Bm * vec4(M.jointHead[d.joint], 1));
        float dev = glm::length(jh + glm::mat3(Bm) * before[k].relHead - p);   // vs where the bone alone carries the point
        // the triangle's blended skin matrix carries the rest-space point (what the GPU pass projects) onto the posed point
        mat4 Mb(0.0f); for (int i = 0; i < 3; ++i) { const auto& sv = M.verts[M.indices[d.tri * 3 + i]]; for (int q = 0; q < 4; ++q) if (sv.w[q] > 0) Mb += V.pose.skin[sv.j[q]] * (sv.w[q] * d.bary[i]); }
        worstGpu = std::max(worstGpu, glm::length(vec3(V.world * Mb * vec4(d.restPos, 1)) - p));
        if (boneWeight(d) >= 0.95f) { ++nRigid; worstRigid = std::max(worstRigid, dev); }
        else worstBlend = std::max(worstBlend, dev);
        minMoved = std::min(minMoved, glm::length(p - before[k].p));
      }
      return chk(worstRigid < 0.005f && worstGpu < 0.025f && worstBlend < 0.15f && minMoved > minMove,
                 "%s: %zu decals moved >= %.2f m with the body; skinning their rest-space point lands on the posed surface within %.2f mm (%d on a single bone stay on it within %.2f mm, the rest skinned across joints within %.0f mm of their main bone)",
                 what, rec.decals.size(), minMoved, worstGpu * 1000, nRigid, worstRigid * 1000, worstBlend * 1000);
    };
    // aim up, crouch, turn and step aside
    vec3 f0 = V.feet; float y0 = V.yaw;
    V.feet += glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * 0.8f; V.yaw += 70; V.posture = Physics::CROUCH; V.crouchBlend = 1; V.br.aimPitch = 25;
    G.posesStep(0);
    measure("crouch + turn 70 deg + aim up 25 deg + 0.8 m step", 0.15f);
    // death pose
    for (auto& r : before) { (void)r; }
    before.clear();
    for (auto& d : rec.decals) { vec3 p, n; gorePosedPoint(tgt, d.tri, d.bary, p, n); vec3 jh = vec3(V.world * V.pose.delta[d.joint] * vec4(M.jointHead[d.joint], 1)); before.push_back({p, glm::inverse(glm::mat3(V.world * V.pose.delta[d.joint])) * (p - jh), d.joint}); }
    V.alive = false; V.diedAt = G.t - 1.0; V.deathStyle = 0; G.posesStep(0);
    measure("death pose (falls backward)", 0.2f);
    V.alive = true; V.feet = f0; V.yaw = y0; V.posture = Physics::STAND; V.crouchBlend = 0; V.br.aimPitch = 0; G.posesStep(0);
    lookAt(a, camFeet, chestOf(V));
    return true; });

  // ---- droplets: teardrop landing, water ripple
  push("droplet landing", [](App& a, float t) {
    (void)t;
    GoreState& S = goreState();
    vec3 g = camFeet + vec3(0, 0, 0) + glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * -1.5f; g.y = a.terrain.height(g.x, g.z);
    size_t w0 = S.world.size();
    goreTestDroplet(g + vec3(0, 0.05f, 0), vec3(6.0f, -0.4f, 0), 0.002f);   // fast and low: lands at a shallow angle
    simulate(a, 1.0f, false);
    bool okLong = S.world.size() == w0 + 1 && S.world.back().stamps.size() == 1;
    float asp = okLong ? S.world.back().stamps[0].aspect : 0; vec3 tw(0);
    if (okLong) { auto& d = S.world.back(); vec3 b = glm::cross(d.n, d.t); tw = d.t * d.stamps[0].tail.x + b * d.stamps[0].tail.y; }
    goreTestDroplet(g + vec3(0.5f, 1.0f, 0.5f), vec3(0, -0.5f, 0), 0.002f);   // straight down: round
    simulate(a, 1.0f, false);
    float asp2 = S.world.size() >= 2 ? S.world.back().stamps.back().aspect : 0;
    chk(okLong && asp > 2.5f && glm::dot(tw, vec3(1, 0, 0)) > 0.9f && asp2 < 1.1f, "a shallow droplet lands as a teardrop (aspect %.2f, tail along +x: %.2f), a vertical one round (aspect %.2f)", asp, glm::dot(tw, vec3(1, 0, 0)), asp2);
    // water: no decal, a ripple
    float wl = a.terrain.waterLevel; a.terrain.waterLevel = g.y + 0.3f;
    int rip0 = S.st.ripples; size_t w1 = S.world.size();
    goreTestDroplet(g + vec3(-0.4f, 1.2f, 0.3f), vec3(0.2f, 0, 0), 0.0015f);
    simulate(a, 1.0f, false);
    a.terrain.waterLevel = wl;
    chk(S.st.ripples == rip0 + 1 && S.world.size() == w1, "a droplet that reaches water makes a ripple and no decal");
    // drag: a small droplet loses speed much faster than a large one
    goreTestDroplet(g + vec3(0, 30, 0), vec3(8, 0, 0), 0.0005f); goreTestDroplet(g + vec3(0, 30, 2), vec3(8, 0, 0), 0.003f);
    simulate(a, 0.25f, false);
    float vs = 0, vl = 0; for (auto& d : S.drops) { if (d.r < 0.001f) vs = d.v.x; else vl = d.v.x; }
    chk(vs > 0 && vl > 0 && vs < vl * 0.8f, "quadratic drag: after 0.25 s a 0.5 mm droplet keeps %.2f m/s of 8, a 3 mm one %.2f m/s", vs, vl);
    S.drops.clear();
    return true; });

  // ---- off mode, protected and helmet hits, mist-only range
  push("off / protected / helmet / far", [](App& a, float t) {
    (void)t;
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    vec3 eye = camFeet + vec3(0, 1.62f, 0);
    goreSetLevel(GoreLevel::Off);
    int rep0 = gGoreHitsReported, dust0 = S.st.dustPuffs, spawn0 = S.st.spawned; size_t parts0 = a.fx.parts.size();
    for (int k = 0; k < 5; ++k) shoot(a, eye, chestOf(V));
    simulate(a, 0.5f);
    chk(gGoreHitsReported == rep0 + 5 && S.st.dustPuffs == dust0 + 5 && S.st.spawned == spawn0 && S.world.empty() && boneCount(tgt) == 0 && a.fx.parts.size() > parts0,
        "Gore Off: 5 hits reported, 5 dust puffs (%zu smoke particles), no droplets, no world or bone decals", a.fx.parts.size() - parts0);
    goreSetLevel(GoreLevel::Full);
    // spawn protection: deflect puff, no gore
    V.protectUntil = G.t + 100;
    int def0 = S.st.deflects; spawn0 = S.st.spawned; size_t b0 = boneCount(tgt), w0 = S.world.size();
    shoot(a, eye, chestOf(V));
    chk(S.st.deflects == def0 + 1 && S.st.spawned == spawn0 && boneCount(tgt) == b0 && S.world.size() == w0, "spawn-protected target: deflect puff, no droplets and no decals");
    V.protectUntil = -1;
    // helmet: sparks, no gore
    FleshHit h; h.pos = vec3(V.world * vec4(V.pose.joint[CJ_HEAD], 1)) + vec3(0, 0.1f, 0); h.dir = glm::normalize(h.pos - eye); h.normal = -h.dir; h.victimSlot = tgt; h.zone = 0; h.bone = CJ_HEAD; h.helmet = true; h.seed = 77; h.energy = 1;
    int sp0 = S.st.sparks; size_t streaks0 = a.fx.streaks.size(); spawn0 = S.st.spawned;
    goreFleshHit(a, h);
    chk(S.st.sparks == sp0 + 1 && a.fx.streaks.size() > streaks0 && S.st.spawned == spawn0 && boneCount(tgt) == b0, "helmet hit: sparks (%zu spark streaks), no droplets and no decals", a.fx.streaks.size() - streaks0);
    // beyond 60 m: mist only
    Soldier& U = G.sold[tgt2];
    vec3 uf0 = U.feet;
    placeSoldier(U, camFeet + vec3(80, 0, 0), vec3(-1, 0, 0)); G.posesStep(0);
    int mist0 = S.st.mistOnly; spawn0 = S.st.spawned; size_t ub = boneCount(tgt2);
    shoot(a, chestOf(U) + vec3(-5, 0, 0.3f), chestOf(U));
    chk(S.st.mistOnly == mist0 + 1 && S.st.spawned == spawn0 && boneCount(tgt2) > ub, "a hit %.0f m from the camera: mist puff only, no droplets (its wound decals still placed)", glm::length(chestOf(U) - eye));
    placeSoldier(U, uf0, -wallDir); G.posesStep(0);
    return true; });

  // ---- spurting on the neck and upper thigh
  push("spurting", [](App& a, float t) {
    (void)t;
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    vec3 eye = camFeet + vec3(0, 1.62f, 0);
    S.bleeders.clear();
    vec3 neck = vec3(V.world * vec4(V.pose.joint[CJ_NECK], 1)) + vec3(0, 0.04f, 0);
    FleshHit h; h.pos = neck; h.dir = glm::normalize(neck - eye); h.normal = -h.dir; h.victimSlot = tgt; h.zone = 1; h.bone = CJ_NECK; h.seed = 4242; h.energy = 0.4f;
    goreFleshHit(a, h);
    bool neckB = S.bleeders.size() == 1;
    double until = neckB ? S.bleeders[0].until - S.bleeders[0].t0 : 0;
    int p0 = S.st.spurtPulses; std::vector<int> per; int lastSp = S.st.spawned;
    for (int k = 0; k < 7 * 60; ++k) { G.t += 1.0 / 60; goreStep(a, 1.0f / 60); if (S.st.spurtPulses != p0 + (int)per.size()) { per.push_back(S.st.spawned - lastSp); } lastSp = S.st.spawned; }
    int pulses = S.st.spurtPulses - p0;
    bool weaker = per.size() >= 3 && per.back() < per.front();
    chk(neckB && until >= 3 && until <= 6 && pulses >= 4 && pulses <= 8 && weaker && S.bleeders.empty(),
        "neck hit spurts: %d pulses about every 0.8 s over %.1f s (3-6), weakening (%d -> %d droplets), then stops", pulses, until, per.empty() ? 0 : per.front(), per.empty() ? 0 : per.back());
    FleshHit th = h; th.bone = CJ_LUPLEG; th.zone = 2; th.pos = vec3(V.world * vec4(glm::mix(V.pose.joint[CJ_LUPLEG], V.pose.joint[CJ_LLEG], 0.3f), 1)); th.dir = glm::normalize(th.pos - eye); th.seed = 4343;
    FleshHit arm = th; arm.bone = CJ_LFOREARM; arm.seed = 4444;
    goreFleshHit(a, arm); size_t afterArm = S.bleeders.size();
    goreFleshHit(a, th);
    chk(afterArm == 0 && S.bleeders.size() == 1 && goreZone(th) == 5 && goreZone(h) == 4, "upper-thigh hit spurts too (zone 5 from the bone), a forearm hit does not");
    S.bleeders.clear(); S.drops.clear();
    return true; });

  // ---- dripping while a wounded soldier moves
  push("drip trail", [](App& a, float t) {
    (void)t;
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt2];
    vec3 eye = camFeet + vec3(0, 1.62f, 0);
    shoot(a, eye, chestOf(V)); simulate(a, 2.0f, true);
    int d0 = S.st.drips;
    vec3 f0 = V.feet;
    for (int k = 0; k < 180; ++k) { V.feet = f0 + wallDir * 0.0f + glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * (k * 2.5f / 60.0f); V.vel = glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * 2.5f; V.speed = 2.5f; G.t += 1.0 / 60; G.posesStep(1.0f / 60); goreStep(a, 1.0f / 60); }
    simulate(a, 1.0f);
    int drips = S.st.drips - d0;
    chk(drips >= 3 && drips <= 12, "a wounded soldier walking 7.5 m leaves a drip trail: %d drips (every 0.7-1.4 m)", drips);
    V.speed = 0; V.vel = vec3(0); placeSoldier(V, f0, -wallDir); G.posesStep(0);
    return true; });

  // ---- LAN event: what a client would apply
  push("lan event", [](App& a, float t) {
    (void)t;
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    vec3 eye = camFeet + vec3(0, 1.62f, 0);
    vec3 c = chestOf(V); vec3 d = glm::normalize(c - eye); HitZone hz;
    bool okHit = G.hitTest(V, eye, d, 20, hz);
    FleshHit h; h.pos = eye + d * hz.t; h.dir = d; h.normal = -d; h.victimSlot = tgt; h.zone = hz.zone; h.bone = CJ_SPINE1; h.seed = 0xABCDEu; h.energy = 1; h.exits = true; h.weapon = a.weaponIndex("kar98k");
    json ev = goreEventJsonFor(h);
    json wire = json::parse(ev.dump());   // as sent
    size_t b0 = boneCount(tgt); int r0 = S.st.netReceived;
    goreNetEvent(a, wire);
    auto& rec = S.sold[tgt];
    bool sameTri = rec.decals.size() >= b0 + 2 && wire.contains("et") && rec.decals[b0].tri == wire["et"].get<int>();
    chk(okHit && S.st.netReceived == r0 + 1 && S.sprayHashBySeed[h.seed] == goreSprayHash(h) && sameTri && wire.dump().size() < 600,
        "LAN gore event (%zu bytes: position, normal, direction, weapon, bone, seed + mesh point) applied on a client: same wound triangle, same seeded spray hash", wire.dump().size());
    out["eventBytes"] = wire.dump().size();
    S.drops.clear();
    return true; });

  // ---- budgets: hard caps under a sustained barrage; profile cost per hit and at full budgets
  push("budgets", [](App& a, float t) {
    Game& G = *gGame; GoreState& S = goreState();
    static double base0 = 0; static int baseN = 0; static double gpu0 = 0; static int gpuN0 = 0, hitN0 = 0, stepN0 = 0; static double hitMs0 = 0, stepMs0 = 0;
    static double fullGpu = 0, fullStep = 0, fullFrame = 0, fullDecalGpu = 0; static int fullN = 0; static bool violated = false; static uint32_t firstSeed = 0; static double firstBorn = -1;
    static std::vector<double> frame;
    vec3 eye = camFeet + vec3(0, 1.62f, 0);
    if (t == 0) { lookAt(a, camFeet - wallDir * 1.0f, tgtFeet + vec3(0, 0.8f, 0)); base0 = 0; baseN = 0; violated = false; fullN = 0; fullGpu = fullStep = fullFrame = fullDecalGpu = 0; return false; }
    if (t < 1.0f) { if (t > 0.3f) { base0 += a.R.stats.gpuMs; ++baseN; } return false; }   // baseline GPU frame with the current decals
    if (gpuN0 == 0) { gpuN0 = S.st.gpuN + 1; gpu0 = S.st.gpuMs; hitN0 = S.st.hitN; hitMs0 = S.st.hitMs; stepN0 = S.st.stepN; stepMs0 = S.st.stepMs; if (!S.world.empty()) { firstSeed = S.world.front().seed; firstBorn = S.world.front().born; } }
    if (t < 9.0f) {   // barrage: 4 rounds a frame from around the arc into both soldiers
      for (int k = 0; k < 4; ++k) {
        int who = k & 1 ? tgt2 : tgt; Soldier& V = G.sold[who]; V.hp = 1e6f;
        float ang = std::sin(t * 7.0f + k) * 0.6f;
        vec3 side = glm::normalize(glm::cross(wallDir, vec3(0, 1, 0)));
        vec3 from = eye + side * (std::sin(ang) * 3.0f) + vec3(0, std::cos(t * 3.0f + k) * 0.3f, 0);
        vec3 aim = chestOf(V) + vec3(0, std::sin(t * 11.0f + k) * 0.35f, 0) + side * std::cos(t * 5.0f + k) * 0.08f;
        shoot(a, from, aim, k == 3 ? "mp40" : "kar98k");
      }
    }
    // hard caps, checked every frame
    int cw = goreWorldCap(), cc = goreCharCap();
    if ((int)S.drops.size() > S.cfg.maxDroplets || (int)S.world.size() > cw) violated = true;
    for (auto& [k, r] : S.sold) if ((int)r.decals.size() > cc) violated = true;
    if (t > 3.0f && t < 9.0f && (int)S.drops.size() > S.cfg.maxDroplets * 9 / 10 && (int)S.world.size() >= cw * 8 / 10) {
      fullGpu += a.R.stats.gpuMs; fullDecalGpu += decalsLastStats().gpuMs; fullFrame += ImGui::GetIO().DeltaTime * 1000.0; ++fullN;
    }
    if (t > 5.0f && t < 5.02f) shot("gore_budgets");
    if (t < 9.0f) return false;
    double perHit = (S.st.hitMs - hitMs0) / std::max(1, S.st.hitN - hitN0);
    double perStep = (S.st.stepMs - stepMs0) / std::max(1, S.st.stepN - stepN0);
    double decGpu = (S.st.gpuMs - gpu0) / std::max(1, S.st.gpuN - gpuN0);
    chk(!violated && S.st.maxDrops <= (size_t)S.cfg.maxDroplets && S.st.maxWorld <= (size_t)cw && S.st.maxChar <= (size_t)cc,
        "hard caps held every frame of a %d-hit barrage: droplets peak %zu / %d (%d not spawned), world decals peak %zu / %d, per character peak %zu / %d",
        S.st.hitN - hitN0, S.st.maxDrops, S.cfg.maxDroplets, S.st.dropped, S.st.maxWorld, cw, S.st.maxChar, cc);
    { int per = 0; std::map<int, int> c; for (auto& b : S.bleeders) per = std::max(per, ++c[b.slot]);
      chk(S.st.maxBleeders <= S.cfg.spurtTotal && per <= S.cfg.spurtPerSoldier, "spurting wounds bounded: peak %d of %d at once, at most %d per soldier now (%d allowed)", S.st.maxBleeders, S.cfg.spurtTotal, per, S.cfg.spurtPerSoldier); }
    chk(S.st.maxDrops == (size_t)S.cfg.maxDroplets && S.st.maxWorld == (size_t)cw && S.st.maxChar == (size_t)cc, "the barrage saturated all three budgets");
    bool oldestFade = true; double lastFading = -1, firstLive = 1e18;
    for (auto& d : S.world) { if (d.fadeStart >= 0) lastFading = std::max(lastFading, d.born); else firstLive = std::min(firstLive, d.born); }
    oldestFade = lastFading <= firstLive + 1e-9;
    bool firstGone = true; for (auto& d : S.world) if (d.seed == firstSeed && d.born == firstBorn) firstGone = false;
    chk(oldestFade && firstGone, "oldest fades first: every fading world decal is older than every live one; the oldest decal from before the barrage is gone");
    auto& rec = S.sold[tgt]; bool charOld = true; double lf = -1, fl = 1e18; for (auto& d : rec.decals) { if (d.fadeStart >= 0) lf = std::max(lf, d.born); else fl = std::min(fl, d.born); } charOld = lf <= fl + 1e-9;
    chk(charOld, "per-character decals also fade oldest first");
    double base = baseN ? base0 / baseN : 0, fg = fullN ? fullGpu / fullN : 0;
    std::snprintf(B, sizeof B, "cost: %.3f ms CPU per hit (mesh ray cast, wounds, spray, splatter ray); at full budgets (%d frames: ~1500 droplets, %d world decals, 2 x 24 bone decals): gore step %.3f ms, decal pass GPU %.3f ms (whole frame GPU %.2f ms vs %.2f ms before), CPU render prep included in the frame",
                  perHit, fullN, cw, perStep, fullN ? fullDecalGpu / fullN : decGpu, fg, base);
    T->check(fullN > 10 && perHit < 2.0 && perStep < 4.0, B);
    out["profile"] = {{"cpuMsPerHit", perHit}, {"stepMsFullBudgets", perStep}, {"decalGpuMsFullBudgets", fullN ? fullDecalGpu / fullN : decGpu}, {"frameGpuMsFull", fg}, {"frameGpuMsBefore", base}, {"framesAtFull", fullN}, {"frameMsFull", fullN ? fullFrame / fullN : 0.0}};
    return true; });

  // ---- reduced level budgets
  push("reduced", [](App& a, float t) {
    (void)t; GoreState& S = goreState();
    goreSetLevel(GoreLevel::Reduced);
    chk(goreWorldCap() == 128 && goreCharCap() == 12 && (int)S.world.size() <= 128, "Gore Reduced: budgets %d world / %d per character, existing decals trimmed to %zu", goreWorldCap(), goreCharCap(), S.world.size());
    FleshHit h; h.pos = vec3(0, 1, 0); h.dir = vec3(0, 0, 1); h.seed = 99; h.energy = 1; h.exits = true;
    std::vector<GoreDropInit> b, f, b2, f2; goreGenerateSpray(h, h.pos, h.pos, false, S.cfg, S.cfg.redDroplets, b, f); goreGenerateSpray(h, h.pos, h.pos, false, S.cfg, 1, b2, f2);
    chk(b.size() + f.size() < (b2.size() + f2.size()) * 0.5, "Reduced sprays %zu droplets where Full sprays %zu", b.size() + f.size(), b2.size() + f2.size());
    goreSetLevel(GoreLevel::Full);
    (void)a;
    return true; });

  // ---- corpse pool: spreads to 0.6-1.0 m over 10-20 s; ageing to brown
  push("pool", [](App& a, float t) {
    Game& G = *gGame; GoreState& S = goreState(); Soldier& V = G.sold[tgt];
    static size_t pi = 0; static double born = 0;
    if (t == 0) {
      S.world.clear(); S.drops.clear(); S.bleeders.clear();
      vec3 eye = camFeet + vec3(0, 1.62f, 0);
      V.hp = 50; int p0 = S.st.pools;
      shoot(a, eye, chestOf(V));
      if (V.alive) G.killSoldier(tgt, G.localSlot, glm::normalize(chestOf(V) - eye));
      simulate(a, 2.0f);
      chk(!V.alive && S.st.pools == p0 + 1, "the killed soldier gets a blood pool under the corpse after %.1f s", S.cfg.poolDelay);
      pi = 0; for (size_t k = 0; k < S.world.size(); ++k) if (S.world[k].kind == GD_POOL) pi = k + 1;
      if (!pi) { std::string ks; for (auto& d : S.world) ks += std::to_string(d.kind) + (d.fadeStart >= 0 ? "f " : " "); logf("gore test: no pool in %zu world decals (pools %d, cap %d): %s", S.world.size(), S.st.pools, goreWorldCap(), ks.c_str()); }
      if (pi) { auto& d = S.world[pi - 1]; born = d.born;
        chk(d.r1 >= 0.6f && d.r1 <= 1.0f && d.growT >= 10 && d.growT <= 20, "pool spreads to %.2f m (0.6-1.0) over %.1f s (10-20)", d.r1, d.growT); }
      lookAt(a, tgtFeet - wallDir * 3.2f + glm::normalize(glm::cross(wallDir, vec3(0, 1, 0))) * 1.0f, V.feet);
      return false;
    }
    if (t < 0.5f) return false;
    if (t < 0.52f) { shot("gore_pool_early"); return false; }
    if (t < 0.6f) return false;
    if (t < 0.62f) { simulate(a, 15.0f); return false; }
    if (t < 1.0f) return false;
    if (t < 1.02f) { shot("gore_pool_15s"); return false; }
    if (t < 1.1f) return false;
    if (t < 1.12f) { simulate(a, 60.0f); return false; }
    if (t < 1.5f) return false;
    shot("gore_aged_75s");
    chk(pi > 0 && S.t - born > 70, "decals aged %.0f s (maroon-brown, no gloss) - screenshots gore_pool_early / gore_pool_15s / gore_aged_75s", S.t - born);
    return true; });

  push("finish", [](App& a, float t) {
    if (t < 0.2f) return false;
    GoreState& S = goreState();
    out["stats"] = {{"hits", S.st.hits}, {"goreHits", S.st.goreHits}, {"droplets", S.st.spawned}, {"landed", S.st.landed}, {"boneDecals", S.st.boneDecals}, {"worldDecals", S.st.worldDecals}, {"meshMisses", S.st.meshMisses}};
    std::ofstream(outDir() / "summary_gore.json") << out.dump(2);
    a.useScripted = false;
    a.exitExplore();
    chk(S.world.empty() && S.drops.empty() && S.sold.empty(), "leaving Explore clears every decal and droplet (goreReset)");
    return true; });
}
}   // namespace

bool goreSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-gore") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) {   // post-render: screenshots and colour probes of the frame just drawn
    if (probeWant) {
      std::vector<uint8_t> px; int w, h;
      if (app.R.readPixels(px, w, h)) {
        vec4 c = app.projM * app.viewM * vec4(probeAt, 1);
        if (c.w > 0) {
          int x = (int)((c.x / c.w * 0.5f + 0.5f) * w), y = (int)((c.y / c.w * 0.5f + 0.5f) * h);
          vec3 s(0); int n = 0;
          for (int yy = y - 4; yy <= y + 4; ++yy) for (int xx = x - 4; xx <= x + 4; ++xx) if (xx >= 0 && yy >= 0 && xx < w && yy < h) { uint8_t* p = &px[((size_t)yy * w + xx) * 4]; s += vec3(p[0], p[1], p[2]); ++n; }
          probeCol = n ? s / (float)n : vec3(0);
        }
      }
      probeWant = false;
    }
    if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); }
    return true;
  }
  if (!started) { started = true; T = new MpTest("gore"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 240) { T->check(false, "gore test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
