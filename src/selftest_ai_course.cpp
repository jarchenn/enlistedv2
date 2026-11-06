// W6 AI test content (AI_SPEC 12.2 / 12.3): the obstacle course map, the random soak map and the test arguments.
//   --make-ai-course        builds content/examples/ai_obstacle_course.nmap.json (written to maps/; the shipped copy
//                           is that file) from the Blank Slate: terrain cuts and banks, existing assets, nav areas
//   --selftest-ai-course    live rebuild (AI_SPEC 12.4) + obstacle course run (selftest_ai.cpp, report "aicourse")
//   --soak-ai <minutes> [village|course|random|all]   32-combatant soak with the AI_SPEC 12.3 metrics
// The course uses W4's structures (zig-zag trench, AT ditch, shingle bank, marsh patch, minefield fence) with their own
// terrain cuts and nav areas; craters, the sunken lane and the ditch ramp crossing are terrain profiles.
#include "app.h"
#include "scenario.h"
#include "ai_course.h"
#include <algorithm>
#include <cmath>
#include <random>

int gSoakMinutes = 20; std::string gSoakMaps = "all";

bool aiTestArg(const std::string& a, int argc, char** argv, int& k, bool& runSelf) {
  extern int runAiSelfTestArg;
  if (a == "--selftest-ai-course") { runSelf = true; runAiSelfTestArg = 4; return true; }
  if (a == "--soak-ai") {
    runSelf = true; runAiSelfTestArg = 5;
    if (k + 1 < argc && argv[k + 1][0] != '-') gSoakMinutes = std::max(1, std::atoi(argv[++k]));
    if (k + 1 < argc && argv[k + 1][0] != '-') gSoakMaps = argv[++k];
    return true;
  }
  if (a == "--make-ai-course") { runSelf = true; runAiSelfTestArg = 6; return true; }
  { extern bool aiHarnessArg(const std::string&, int, char**, int&, bool&); if (aiHarnessArg(a, argc, argv, k, runSelf)) return true; }   // W6b: --ai-harness (selftest_ai_harness.cpp)
  return false;
}

namespace {
float smooth(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
// signed terrain delta of the course at x, z (metres; negative = cut)
float courseDelta(float x, float z) {
  float d = 0;
  // craters of several sizes
  struct Cr { float x, z, r, dep; };
  static const Cr cr[] = {{-18, 48, 1.6f, 0.5f}, {-4, 52, 2.6f, 1.0f}, {12, 46, 4.0f, 1.5f}, {26, 51, 6.0f, 2.2f}, {-30, 44, 3.0f, 1.2f}, {4, 42, 1.2f, 0.4f}};
  for (auto& c : cr) { float q = ((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z)) / (c.r * c.r); if (q < 1) d -= c.dep * (1 - q) * (1 - q); }
  // the anti-tank ditch's one ramp crossing at x 27..33 (20 degree slopes, 3.5 m deep at the ditch line z = 30.5); the
  // ditch itself is a row of W4 bch_at_ditch pieces either side of it
  {
    float ramp = z >= 30.5f ? 3.5f * std::clamp((40.0f - z) / 9.5f, 0.0f, 1.0f) : 3.5f * std::clamp((z - 21.0f) / 9.5f, 0.0f, 1.0f);
    float w = smooth(24.5f, 26.5f, x) * (1.0f - smooth(33.5f, 35.5f, x));
    d -= std::min(ramp, 3.5f) * w;
  }
  // sunken lane: 4 m wide, 1.2 m deep, steep banks, east-west
  d -= 1.2f * (1.0f - smooth(2.0f, 2.9f, std::fabs(z + 15.0f))) * (1.0f - smooth(70, 75, std::fabs(x)));
  return d;
}

void sculpt(App& app, const std::function<float(float, float)>& f, vec2 lo, vec2 hi) {
  Terrain& T = app.terrain;
  std::map<int, std::vector<float>> deltas;
  float fs = T.fineSpacing(), ts = T.tileSize();
  int n = T.tilesN();
  for (int tz = 0; tz < n; ++tz) for (int tx = 0; tx < n; ++tx) {
    vec2 o = T.tileOrigin(tx, tz);
    if (o.x > hi.x || o.x + ts < lo.x || o.y > hi.y || o.y + ts < lo.y) continue;
    std::vector<float> dv((size_t)Terrain::kFineN * Terrain::kFineN, 0.0f); bool any = false;
    for (int j = 0; j < Terrain::kFineN; ++j) for (int i = 0; i < Terrain::kFineN; ++i) { float v = f(o.x + i * fs, o.y + j * fs); if (std::fabs(v) > 1e-4f) { dv[j * Terrain::kFineN + i] = v; any = true; } }
    if (any) deltas[T.key(tx, tz)] = dv;
  }
  app.doc.terrainDeltas = deltas;
  app.doc.terrainTemplateVersion = app.doc.templateVersion;   // persisted deltas are tied to the template version
  app.resetGround();
}

uint64_t place(App& app, const std::string& asset, vec3 p, float yaw) {
  Instance in; in.id = app.doc.newId(); in.assetId = asset; in.pos = vec3(p.x, app.terrain.height(p.x, p.z), p.z); in.yaw = yaw;
  app.doc.instances.push_back(in);
  return in.id;
}

}

bool buildAiObstacleCourse(App& app, std::string& err) {
  const TemplateInfo* t = app.findTemplate("blank_slate");
  if (!t) { err = "blank_slate template missing"; return false; }
  if (!app.newMap(*t, "AI obstacle course")) { err = "newMap failed"; return false; }
  sculpt(app, courseDelta, vec2(-80, -40), vec2(80, 100));
  // beach obstacle field: hedgehogs on a jittered grid
  std::mt19937 rng(1944);
  std::uniform_real_distribution<float> U(-1.0f, 1.0f);
  for (float z = 98; z <= 112; z += 5) for (float x = -40; x <= 40; x += 6.5f) place(app, "def_tank_trap", vec3(x + U(rng) * 1.5f + (std::fmod(z, 10.0f) < 1 ? 3.0f : 0.0f), 0, z + U(rng)), U(rng) * 180);
  // barbed wire belt (concertina, long axis local z) with two gaps
  for (float x = -60; x <= 60; x += 3.8f) { if (std::fabs(x + 28) < 3.5f || std::fabs(x - 18) < 3.5f) continue; place(app, "wire_concertina", vec3(x, 0, 76), 90); }
  // shingle bank (W4, 12 m pieces, seaward face to +z) along z = 88
  for (float x = -54; x <= 54; x += 12) place(app, "bch_shingle_bank", vec3(x, 0, 88), 0);
  // marked minefield (W4 fence pieces: forbidden nav area 10 m x 10 m behind each fence) over x -44..-4, z 59..69
  for (float x = -39; x <= -9; x += 10) place(app, "bch_minefield_fence", vec3(x, 0, 59), 0);
  // anti-tank ditch (W4, 12 m pieces with a terrain cut, long axis local x) along z = 30.5, open only at the ramp x 24.5..35.5
  for (float x = 17.7f; x >= -67; x -= 12) place(app, "bch_at_ditch", vec3(x, 0, 30.5f), 0);
  for (float x = 42.3f; x <= 67; x += 12) place(app, "bch_at_ditch", vec3(x, 0, 30.5f), 0);
  // zig-zag trench (W4 12 m bays, cut 1.75 m deep) along z = 15 across the course
  for (float x = -60; x <= 60; x += 12) place(app, "aw_trench_zigzag", vec3(x, 0, 15), 0);
  // marsh (W4 reed patch: shallow cut, marsh nav area)
  place(app, "bcg_marsh_patch", vec3(-21, 0, -38.5f), 0);
  // hedgerow line with gaps (bocage banks block; bots go through the gaps)
  for (float x = -64; x <= 64; x += 8) { if (std::fabs(x + 24) < 1 || std::fabs(x - 16) < 1) continue; place(app, "hedgerow_bocage", vec3(x, 0, 0), 0); }
  // low walls (vault)
  for (float x = -36; x <= 36; x += 6) { if (std::fabs(x) < 1) continue; place(app, "stone_wall_low", vec3(x, 0, -28), 0); }
  // the building and the bunker on objective A
  place(app, "house_stone_cottage", vec3(-14, 0, -60), 0);
  place(app, "bunker_mg_casemate", vec3(16, 0, -60), 0);
  app.syncRuntime();
  if (!app.doc.extra.is_object()) app.doc.extra = json::object();
  // scenario: Allies attack from the south (+z) toward the building / bunker (A), then B beyond
  Scenario sc;
  Objective a; a.id = sc.newId("obj"); a.label = "Cottage and casemate"; a.order = 0; a.pos = vec3(0, app.terrain.height(0, -60), -60); a.radius = 28; a.attackYaw = 0; a.yMin = -3; a.yMax = 5;
  Objective b; b.id = sc.newId("obj"); b.label = "Far field"; b.order = 1; b.pos = vec3(0, app.terrain.height(0, -150), -150); b.radius = 20; b.attackYaw = 0;
  for (auto& in : app.doc.instances) if (in.assetId == "house_stone_cottage" || in.assetId == "bunker_mg_casemate") a.linkedStructures.push_back(in.id);
  sc.objectives = {a, b};
  for (int ph = 0; ph < 2; ++ph) {
    for (int m = 0; m < 3; ++m) { SpawnMarker s; s.id = sc.newId("spawn"); s.role = CombatRole::Attacker; s.phases = {ph}; s.pos = vec3(-8.0f + m * 8.0f, 0, ph == 0 ? 126.0f : -40.0f); s.yaw = 0; s.label = "Attack"; sc.spawns.push_back(s); }
    SpawnMarker d; d.id = sc.newId("spawn"); d.role = CombatRole::Defender; d.phases = {ph}; d.pos = vec3(0, 0, -240); d.yaw = 0; d.label = "Defend"; sc.spawns.push_back(d);
  }
  sc.rules.defenderSlots = 1; sc.rules.attackerAI = 10; sc.rules.attackerRespawns = 3; sc.rules.defendingFaction = Faction::Axis;
  app.doc.extra["scenario"] = sc.toJson();
  app.doc.start = {true, vec3(0, 0, -240), 0};
  return true;
}

bool buildRandomSoakMap(App& app, uint32_t seed, std::string& err) {
  const TemplateInfo* t = app.findTemplate("blank_slate");
  if (!t || !app.newMap(*t, "AI soak (random structures)")) { err = "newMap failed"; return false; }
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(-1.0f, 1.0f);
  const char* pool[] = {"house_stone_cottage", "house_farmhouse_2story", "small_storehouse", "small_stone_dairy", "stone_wall_low", "hedgerow_bocage", "hedgerow_bocage", "def_sandbag_straight",
                        "def_tank_trap", "wire_concertina", "bunker_field_shelter", "bunker_mg_casemate", "tree_oak", "tree_ash", "shrub_hazel", "large_stone_warehouse"};
  std::vector<vec3> placed;
  for (int k = 0; k < 140; ++k) {
    vec3 p(U(rng) * 270, 0, U(rng) * 270);
    if (std::fabs(p.x) < 14 && std::fabs(p.z) > 200) continue;             // keep the spawn lanes open
    bool close = false; for (auto& q : placed) if (glm::length(vec2(q.x - p.x, q.z - p.z)) < 9) close = true;
    if (close) continue;
    placed.push_back(p);
    place(app, pool[rng() % (sizeof pool / sizeof pool[0])], p, (float)(rng() % 360));
  }
  // a few craters
  std::vector<vec4> cr; for (int k = 0; k < 30; ++k) cr.push_back(vec4(U(rng) * 250, U(rng) * 250, 1.5f + std::fabs(U(rng)) * 3.5f, 0.5f + std::fabs(U(rng)) * 1.2f));
  sculpt(app, [&](float x, float z) { float d = 0; for (auto& c : cr) { float q = ((x - c.x) * (x - c.x) + (z - c.y) * (z - c.y)) / (c.z * c.z); if (q < 1) d -= c.w * (1 - q) * (1 - q); } return d; }, vec2(-280, -280), vec2(280, 280));
  for (auto& in : app.doc.instances) in.pos.y = app.terrain.height(in.pos.x, in.pos.z);
  app.syncRuntime();
  Scenario sc;
  vec3 objs[3] = {vec3(-40, 0, 120), vec3(30, 0, 0), vec3(-20, 0, -130)};
  for (int k = 0; k < 3; ++k) { Objective o; o.id = sc.newId("obj"); o.label = std::string("Point ") + char('A' + k); o.order = k; o.pos = objs[k]; o.pos.y = app.terrain.height(o.pos.x, o.pos.z); o.radius = 24; o.attackYaw = 0; o.yMin = -3; o.yMax = 5; sc.objectives.push_back(o); }
  for (int k = 0; k < 3; ++k) {
    for (int m = 0; m < 3; ++m) { SpawnMarker s; s.id = sc.newId("spawn"); s.role = CombatRole::Attacker; s.phases = {k}; s.pos = objs[k] + vec3(-12.0f + m * 12.0f, 0, 140); s.pos.z = std::min(s.pos.z, 280.0f); s.label = "Attack"; sc.spawns.push_back(s); }
    for (int m = 0; m < 2; ++m) { SpawnMarker d; d.id = sc.newId("spawn"); d.role = CombatRole::Defender; d.phases = {k}; d.pos = objs[k] + vec3(-6.0f + m * 12.0f, 0, -60); d.yaw = 180; d.label = "Defend"; sc.spawns.push_back(d); }
  }
  sc.rules.defenderSlots = 12; sc.rules.attackerAI = 20; sc.rules.attackerRespawns = 20; sc.rules.defendingFaction = Faction::Axis;
  if (!app.doc.extra.is_object()) app.doc.extra = json::object();
  app.doc.extra["scenario"] = sc.toJson();
  app.doc.start = {true, objs[0] + vec3(0, 0, -60), 180};
  return true;
}
