// --selftest-ai: integration suite for the match layer on the shipped village with a 3-objective scenario (real physics,
// walk grid, AI, weapons, grenades, smoke, HUD). Also writes the 3- and 5-point test maps to maps/ (shipped copies:
// content/examples/village_assault_3 / _5). Stages 2000+.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <chrono>
#include <functional>
#include <fstream>
#include <cmath>
#include <algorithm>

int runAiSelfTestArg = 0;   // 1 = --selftest-ai, 2 = --benchmark-match, 3 = --baseline-ai

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0;
double stepMsSum = 0; int stepMsN = 0; double frameMsSum = 0; int frameN = 0;

json makeScenario(int points) {
  Scenario s;
  // attackers come from the south (+z) and push north (-z): attack arrow yaw 0
  struct P { const char* label; vec3 pos; float r; vec3 atk; vec3 def; };
  std::vector<P> ps = {
    {"Crossroads", vec3(18, 0, 14), 12, vec3(0, 0, 80), vec3(14, 0, -2)},
    {"Town hall", vec3(-24, 0, -8), 15, vec3(-5, 0, 45), vec3(-26, 0, -32)},
    {"Cottage", vec3(-2, 0, -46), 13, vec3(-8, 0, 12), vec3(-4, 0, -72)},
    {"Farm", vec3(34, 0, -40), 13, vec3(30, 0, 0), vec3(36, 0, -70)},
    {"Dairy", vec3(-44, 0, -40), 12, vec3(-30, 0, -10), vec3(-50, 0, -70)},
  };
  ps.resize(points);
  for (int k = 0; k < points; ++k) {
    Objective o; o.id = s.newId("obj"); o.label = ps[k].label; o.order = k; o.pos = ps[k].pos; o.radius = ps[k].r; o.attackYaw = 0;
    o.yMin = -2; o.yMax = 4;
    s.objectives.push_back(o);
    for (int m = 0; m < 3; ++m) {
      SpawnMarker a; a.id = s.newId("spawn"); a.role = CombatRole::Attacker; a.phases = {k}; a.pos = ps[k].atk + vec3((m - 1) * 6.0f, 0, m * 2.0f); a.yaw = 0; a.label = "Attack " + std::to_string(k + 1); s.spawns.push_back(a);
      SpawnMarker d; d.id = s.newId("spawn"); d.role = CombatRole::Defender; d.phases = {k}; d.pos = ps[k].def + vec3((m - 1) * 4.0f, 0, 0); d.yaw = 180; d.label = "Defend " + std::to_string(k + 1); s.spawns.push_back(d);
    }
  }
  s.rules.defendingFaction = Faction::Allies; s.rules.captureModel = CaptureModel::Contest;   // the milestone setup these tests measure
  return s.toJson();
}
}

void SelfTest::aiUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); };
  if (stage == 2000 && runAiSelfTestArg == 2) {   // --benchmark-match: 20 combatants, smoke and explosions, 60 s
    reportName = "bench_match";
    seq.clear(); cur = 0; st = 0;
    check(app.openMap(contentDir() / "examples" / "village_assault_3.nmap.json"), "open village_assault_3");
    Scenario sc = Scenario::fromJson(app.doc.extra["scenario"]);
    sc.rules.defenderSlots = 5; sc.rules.attackerAI = 15;
    gUi.matchScenario = sc.toJson(); gUi.loaded = false;
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true; app.doc.start = {true, vec3(-6, 0, 34), 180};
    check(app.enterExplore() && gGame->active, "match with 20 combatants starts");
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    stage = 2001;
    push("bench", [this](App& a, float t) {
      Game& G = *gGame;
      static std::vector<double> fr, cpu, gpu, stepv; static double s0 = 0; static int n0 = 0;
      if (t == 0) { fr.clear(); cpu.clear(); gpu.clear(); stepv.clear(); }
      static float lastCloud = -100; if (t == 0) lastCloud = -100;
      if (t > 5.0f && t - lastCloud > 18.0f) { lastCloud = t; for (int k = 0; k < 3; ++k) G.gren.addCloud(vec3(-8.0f + k * 8.0f, 0, 22), G.t, 11 + k); }   // three clouds kept in view
      if (t > 8.0f && std::fmod(t, 4.0f) < 0.017f) { Physics::Hit h; if (Physics::castRayEx(vec3(-24, 1.2f, 20), vec3(0, 0, -1), 30, h)) { a.explode(h.pos, vec3(0, 0, -1), 320, 0.9f, 1.0f, ~0ull, WALL_AP); a.fx.explosion(h.pos, vec3(0, 1, 0), "masonry", false); } }
      a.eyeYaw = 180; a.eyePitch = -2;
      if (t > 6) { fr.push_back(ImGui::GetIO().DeltaTime * 1000.0); cpu.push_back(a.R.stats.cpuMs); gpu.push_back(a.R.stats.gpuMs); stepv.push_back(G.stepCalls > n0 ? (G.stepMs - s0) / (G.stepCalls - n0) : 0); }
      s0 = G.stepMs; n0 = G.stepCalls;
      if (t < 66) return false;
      auto pct = [](std::vector<double> v, double p) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[std::min(v.size() - 1, (size_t)(p * v.size()))]; };
      auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0.0 : s / v.size(); };
      int alive = 0; for (auto& s : G.sold) alive += s.alive;
      char b[400]; std::snprintf(b, sizeof b, "%d combatants (%d alive), %zu smoke clouds, explosions every 4 s, 1920x1080: frame mean %.1f ms / p95 %.1f ms; render CPU %.1f / p95 %.1f ms; GPU %.1f / p95 %.1f ms; match step (AI, soldiers, capture) %.2f ms mean / p95 %.2f ms",
                    (int)G.sold.size(), alive, G.gren.clouds.size(), mean(fr), pct(fr, 0.95), mean(cpu), pct(cpu, 0.95), mean(gpu), pct(gpu, 0.95), mean(stepv), pct(stepv, 0.95));
      check(G.sold.size() == 20, b);
      benchOut = {{"combatants", (int)G.sold.size()}, {"alive", alive}, {"frameMs", mean(fr)}, {"frameP95", pct(fr, 0.95)}, {"cpuMs", mean(cpu)}, {"cpuP95", pct(cpu, 0.95)}, {"gpuMs", mean(gpu)}, {"gpuP95", pct(gpu, 0.95)}, {"stepMs", mean(stepv)}, {"stepP95", pct(stepv, 0.95)}, {"frames", (int)fr.size()}, {"note", "shared GPU: other processes may load it; compare back to back only"}};
      std::ofstream(exeDir() / "selftest_out" / "benchmark_match.json") << benchOut.dump(2);
      a.uiShot = exeDir() / "selftest_out" / "bench_match.png";
      return true; });
    push("exit", [this](App& a, float t) { if (t < 0.2f) return false; a.exitExplore(); return true; });
    return;
  }
  if (stage == 2000 && runAiSelfTestArg == 3) {   // --baseline-ai: AI_SPEC 2.2 baseline of the current bot AI
    reportName = "ai_baseline";
    seq.clear(); cur = 0; st = 0;
    check(app.openMap(contentDir() / "examples" / "village_assault_5.nmap.json"), "open village_assault_5");
    Scenario sc = Scenario::fromJson(app.doc.extra["scenario"]);
    gUi.matchScenario = sc.toJson(); gUi.loaded = false;
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true; app.doc.start = {true, vec3(-60, 0, -90), 180};   // the local player waits out of the way
    check(app.enterExplore() && gGame->active, "baseline match starts (5 objectives, 10 attacking AI, 5 defenders)");
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    stage = 2001;
    push("baseline", [this](App& a, float t) {
      Game& G = *gGame;
      constexpr float kDur = 180.0f;
      struct Acc { vec3 anchor{0}; float since = 0; bool stuck = false; float stuckFor = 0; };
      static std::map<int, Acc> acc; static double advT = 0, advDist = 0, advSpeedSum = 0, fireT = 0, exposedT = 0, nnSum = 0; static long advN = 0, nnN = 0;
      static int stuckEvents = 0, stuckLong = 0, deaths0 = 0; static float lastT = 0;
      if (t == 0) { acc.clear(); advT = advDist = advSpeedSum = fireT = exposedT = nnSum = 0; advN = nnN = 0; stuckEvents = stuckLong = 0; lastT = 0; deaths0 = 0; for (auto& s : G.match.slots) if (s.role == CombatRole::Attacker) deaths0 += s.deaths; }
      float dt = t - lastT; lastT = t;
      a.eyeYaw = 180; a.eyePitch = -2;
      if (dt > 0) for (auto& s : G.sold) {
        if (!s.alive || s.localPlayer || s.role != CombatRole::Attacker) { acc.erase(s.slot); continue; }
        Brain& b = s.br;
        bool moving = !b.path.empty() && b.pathIdx < b.path.size() && b.mode != Brain::M_HOLD;
        Acc& q = acc[s.slot];
        float hs = glm::length(vec2(s.vel.x, s.vel.z));
        if (moving) {
          advT += dt; advSpeedSum += hs; ++advN; advDist += hs * dt;
          float nn = 1e9f; for (auto& o : G.sold) if (&o != &s && o.alive && o.role == s.role) nn = std::min(nn, glm::length(o.feet - s.feet));
          if (nn < 1e8f) { nnSum += nn; ++nnN; }
          q.since += dt;
          if (glm::length(s.feet - q.anchor) >= 0.5f) { q.anchor = s.feet; q.since = 0; if (q.stuck && q.stuckFor > 5) ++stuckLong; q.stuck = false; q.stuckFor = 0; }
          else if (q.since > 1.5f) { if (!q.stuck) { ++stuckEvents; q.stuck = true; } q.stuckFor += dt; }
        } else { q.anchor = s.feet; q.since = 0; if (q.stuck && q.stuckFor > 5) ++stuckLong; q.stuck = false; q.stuckFor = 0; }
        bool underFire = G.t - b.hurtAt < 2.0 || b.suppressed > 0.3f;
        if (underFire) { fireT += dt; bool inCover = b.hasCover && b.mode == Brain::M_COVER && glm::length(s.feet - b.coverPos) < 1.5f; if (!inCover) exposedT += dt; }
      }
      if (t < kDur && G.match.phase == MatchPhase::Running) return false;
      int deaths = -deaths0; for (auto& s : G.match.slots) if (s.role == CombatRole::Attacker) deaths += s.deaths;
      int bots = 0; for (auto& s : G.match.slots) bots += s.role == CombatRole::Attacker;
      float mins = std::max(t, 1.0f) / 60.0f;
      json out = {{"map", "village_assault_5"}, {"aiVersion", "AI/LAN milestone grid AI (multiplayer-ai 5f923cd)"}, {"seconds", t}, {"attackingBots", bots},
                  {"stuckEventsPerBotPerMin", stuckEvents / std::max(1, bots) / mins}, {"stuckEvents", stuckEvents}, {"stuckOver5s", stuckLong},
                  {"advanceSpeedMean_mps", advN ? advSpeedSum / advN : 0.0}, {"advancingBotSeconds", advT},
                  {"underFireSeconds", fireT}, {"exposedUnderFireFraction", fireT > 0 ? exposedT / fireT : 0.0},
                  {"attackerDeathsPerMin", deaths / mins}, {"nearestTeammateWhileAdvancing_m", nnN ? nnSum / nnN : 0.0},
                  {"objectivesTaken", G.match.active + (G.match.winner == Winner::Attackers ? 1 : 0)},
                  {"definitions", "stuck = trying to follow a path and < 0.5 m covered in 1.5 s (AI_SPEC 3.6); advancing = following a path, not holding; "
                                  "under fire = hurt in the last 2 s or suppression > 0.3; exposed = under fire and not settled at a chosen cover point (AI_SPEC 12.3 proxy)"}};
      char b[400]; std::snprintf(b, sizeof b, "baseline %.0f s, %d attacking bots: stuck %.2f /bot/min (%d events, %d over 5 s), advance speed %.2f m/s, exposed under fire %.0f %%, attacker deaths %.1f /min, nearest teammate %.1f m",
                                 t, bots, out["stuckEventsPerBotPerMin"].get<double>(), stuckEvents, stuckLong, out["advanceSpeedMean_mps"].get<double>(), 100 * out["exposedUnderFireFraction"].get<double>(), out["attackerDeathsPerMin"].get<double>(), out["nearestTeammateWhileAdvancing_m"].get<double>());
      check(advT > 0, b);
      std::ofstream(exeDir() / "selftest_out" / "ai_baseline.json") << out.dump(2);
      return true; });
    push("exit", [this](App& a, float t) { if (t < 0.2f) return false; a.exitExplore(); return true; });
    return;
  }
  if (stage == 2000) {
    reportName = "ai";
    seq.clear(); cur = 0; st = 0;
    // ---- creative scenario tools on a real map: spawn racks, delete, save hook (SPEC 1.3)
    {
      check(app.openMap(contentDir() / "examples" / "village.nmap.json"), "open the shipped village (scenario tools)");
      if (!app.doc.extra.is_object()) app.doc.extra = json::object();
      app.doc.extra["scenario"] = Scenario().toJson(); gUi.loaded = false;
      size_t inst0 = app.doc.instances.size();
      check(gUi.addSpawnAt(app, CombatRole::Attacker, vec3(0, 0, 60), 0).empty(), "a spawn needs an objective first (refused on an empty scenario)");
      bool okA = gUi.addObjectiveAt(app, vec3(18, 0, 14));
      std::string atk = gUi.addSpawnAt(app, CombatRole::Attacker, vec3(0, 0, 70), 0);
      std::string def = gUi.addSpawnAt(app, CombatRole::Defender, vec3(14, 0, -30), 0);
      Scenario& sc = gUi.scenario(app);
      const SpawnMarker *ma = nullptr, *md = nullptr; for (auto& m : sc.spawns) { if (m.id == atk) ma = &m; if (m.id == def) md = &m; }
      Instance* ra = ma ? app.doc.find(ma->rack) : nullptr; Instance* rd = md ? app.doc.find(md->rack) : nullptr;
      char b[240]; std::snprintf(b, sizeof b, "each spawn auto-places its team's weapons rack: attackers (Allies) %s at %.1f m, defenders (Axis) %s at %.1f m",
                                 ra ? ra->assetId.c_str() : "none", ra && ma ? glm::length(ra->pos - ma->pos) : -1.0f, rd ? rd->assetId.c_str() : "none", rd && md ? glm::length(rd->pos - md->pos) : -1.0f);
      check(okA && ra && rd && ra->assetId == "rack_allied" && rd->assetId == "rack_axis" && glm::length(ra->pos - ma->pos) < 5 && glm::length(rd->pos - md->pos) < 5 && app.doc.instances.size() == inst0 + 2, b);
      check(sc.objectives.size() == 1 && sc.objectives[0].radius == 25 && sc.objectives[0].letter() == 'A' && sc.spawnsFor(CombatRole::Attacker, 0).size() == 1,
            "placed objective A (25 m circle) with one attacker and one defender spawn in its phase");
      uint64_t rackA = ma->rack;
      check(gUi.deleteSpawn(app, atk) && !app.doc.find(rackA) && app.doc.instances.size() == inst0 + 1, "deleting a spawn removes its rack");
      for (int k = 1; k < 10; ++k) gUi.addObjectiveAt(app, vec3(18, 0, 14 - 20.0f * k));
      check(gUi.scenario(app).objectives.size() == 10 && !gUi.addObjectiveAt(app, vec3(0, 0, -300)) && gUi.scenario(app).objectives.size() == 10, "the editor refuses an 11th objective");
      // save hook: spawns without any objective cannot be saved
      Scenario bad; SpawnMarker m; m.id = "spawn_1"; m.pos = vec3(0, 0, 50); bad.spawns.push_back(m);
      app.doc.extra["scenario"] = bad.toJson(); gUi.loaded = false;
      fs::path p = userMapsDir() / "selftest_scenario_refused.nmap.json"; std::error_code ec; fs::remove(p, ec);
      check(!app.saveMapTo(p) && !fs::exists(p), "save is refused for a scenario with spawns and 0 objectives");
      app.doc.extra.erase("scenario"); gUi.loaded = false;
      check(app.saveMapTo(p) && fs::exists(p), "a plain creative map without a scenario still saves");
      fs::remove(p, ec);
    }
    // ---- test maps (3 and 5 points) from the shipped village
    for (int pts : {5, 3}) {
      check(app.openMap(contentDir() / "examples" / "village.nmap.json"), "open the shipped village");
      if (!app.doc.extra.is_object()) app.doc.extra = json::object();
      app.doc.extra["scenario"] = makeScenario(pts);
      app.doc.name = "Village assault (" + std::to_string(pts) + " points)";
      fs::path out = userMapsDir() / ("village_assault_" + std::to_string(pts) + ".nmap.json");
      check(app.saveMapTo(out), "saved " + out.filename().string());
      MapDoc back; std::string err;
      bool ok = loadMap(out, back, err);
      Scenario rs = Scenario::fromJson(back.extra.value("scenario", json()));
      int errs = 0; for (auto& i : validateScenario(rs, true)) errs += i.error;
      check(ok && (int)rs.objectives.size() == pts && errs == 0, "reopened: " + std::to_string(rs.objectives.size()) + " objectives in saved order, validates for publish");
    }
    // the 3-point map stays open for the match
    gUi.loaded = false;
    gUi.rules = Scenario::fromJson(app.doc.extra["scenario"]).rules; gUi.rulesInit = true;
    gUi.matchScenario = json();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    app.doc.start = {true, vec3(0, 0, 30), 180};
    check(app.enterExplore() && gGame->active, "Singleplayer match starts from the Play flow (" + gGame->setupError + ")");
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    Game& G = *gGame;
    char b[256];
    std::snprintf(b, sizeof b, "walk grid: %d x %d columns, %d floors, %d links (%d through doors), built in %.0f ms", G.nav.nx, G.nav.nz, G.nav.usedNodes, G.nav.links, G.nav.doorLinks, G.nav.buildMs);
    check(G.nav.usedNodes > 10000 && G.nav.doorLinks > 0, b);
    summary["nav"] = {{"columns", G.nav.nx * G.nav.nz}, {"floors", G.nav.usedNodes}, {"links", G.nav.links}, {"doorLinks", G.nav.doorLinks}, {"buildMs", G.nav.buildMs}};
    int up = 0; for (int s = 0; s < (int)G.nav.slots.size(); ++s) if (G.nav.used(s) && G.nav.slots[s].y > 2.0f) ++up;
    check(up > 200, "upper floors are walkable (" + std::to_string(up) + " floor cells above 2 m: stairs and storeys)");
    // reachability: an upper floor of the town hall from the street
    {
      int a = G.nav.nearest(vec3(-24, 0, 12), 3), best = -1; float by = 0;
      for (int s = 0; s < (int)G.nav.slots.size(); ++s) if (G.nav.used(s) && G.nav.slots[s].y > 2.5f && G.nav.slots[s].y < 9.0f && G.nav.slots[s].head < 590) { vec3 p = G.nav.pos(s); if (glm::length(vec2(p.x + 24, p.z + 8)) < 8 && p.y > by) { by = p.y; best = s; } }
      std::vector<int> path; bool ok = best >= 0 && a >= 0 && G.nav.path(a, best, path, 200000);
      int doors = 0; for (size_t k = 0; k + 1 < path.size(); ++k) for (int d = 0; d < 8; ++d) if (G.nav.neighbour(path[k], d) == path[k + 1] && G.nav.doorLink(path[k], d)) ++doors;
      std::snprintf(b, sizeof b, "path street -> town hall upper floor (y %.1f m): %s, %zu cells, %d door links", by, ok ? "found" : "NOT found", path.size(), doors);
      check(ok, b);
    }
    stage = 2001;
    push("spawn", [this](App& a, float t) {
      Game& G = *gGame;
      if (t < 2.0f) return false;
      int atk = 0, def = 0, overlap = 0, inside = 0;
      for (auto& s : G.sold) if (s.alive) {
        (s.role == CombatRole::Attacker ? atk : def)++;
        for (auto& o : G.sold) if (&o != &s && o.alive && glm::length(o.feet - s.feet) < 0.5f) ++overlap;
        if (!Physics::shapeFitsAt(Physics::CROUCH, s.feet + vec3(0, 0.05f, 0), false)) ++inside;
      }
      char b[200]; std::snprintf(b, sizeof b, "initial spawn: %d attackers, %d defenders alive (want 10 / 5), %d overlapping pairs, %d inside geometry", atk, def, overlap / 2, inside);
      check(atk == 10 && def == 5 && overlap == 0 && inside == 0, b);
      int lives = G.match.unusedAttackerLives();
      check(lives == 50, "first life spent on spawn: " + std::to_string(lives) + " attacker respawns unused (10 x 5)");
      int lanes[3] = {0, 0, 0}; for (auto& s : G.sold) if (s.role == CombatRole::Attacker) lanes[s.br.lane]++;
      check(lanes[0] == 2 && lanes[1] == 6 && lanes[2] == 2, "attacker lanes " + std::to_string(lanes[0]) + "/" + std::to_string(lanes[1]) + "/" + std::to_string(lanes[2]) + " (left/main/right)");
      int frags = 0, smokes = 0; bool ok = true;
      for (auto& s : G.sold) if (s.alive) { frags += s.frags; smokes += s.smokes; if (s.frags != 2 || s.smokes != (s.role == CombatRole::Attacker ? 1 : 0)) ok = false; }
      check(ok, "every soldier starts its life with 2 frag grenades, attackers also 1 smoke (" + std::to_string(frags) + " frag, " + std::to_string(smokes) + " smoke)");
      return true; });
    push("lanes diverge, converge", [this](App& a, float t) {
      Game& G = *gGame;
      static float maxL = 0, maxR = 0; static std::map<int, float> d0; static int inSamples = 0, allSamples = 0; static float minHoldPair = 1e9f;
      const Objective* ob = G.match.activeObjective();
      if (t == 0) { maxL = maxR = 0; d0.clear(); inSamples = allSamples = 0; minHoldPair = 1e9f; for (auto& s : G.sold) if (s.alive && s.role == CombatRole::Attacker) d0[s.slot] = glm::length(s.feet - ob->pos); }
      vec3 r = attackRight(ob->attackYaw);
      for (auto& s : G.sold) if (s.alive && s.role == CombatRole::Attacker && G.match.active == 0) {
        // lateral offset from the straight line spawn-centre -> objective
        float lat = glm::dot(s.feet - ob->pos, r);
        if (s.br.lane == LANE_LEFT) maxL = std::min(maxL, lat);
        if (s.br.lane == LANE_RIGHT) maxR = std::max(maxR, lat);
      }
      if (t > 15) {   // defenders over a 10 s window: on the point and spread out
        std::vector<vec3> hold;
        for (auto& s : G.sold) if (s.alive && s.role == CombatRole::Defender && !s.human && G.match.active == 0) { ++allSamples; if (ob->contains(s.feet)) ++inSamples; if (s.br.mode == Brain::M_HOLD) hold.push_back(s.feet); }
        for (size_t i = 0; i < hold.size(); ++i) for (size_t j = i + 1; j < hold.size(); ++j) minHoldPair = std::min(minHoldPair, glm::length(hold[i] - hold[j]));
      }
      if (t < 25.0f) return false;
      char b[200]; std::snprintf(b, sizeof b, "flanks take separate approaches: left lane reached %.1f m left of the objective line, right lane %.1f m right", -maxL, maxR);
      check(-maxL > 6 && maxR > 6, b);
      int closer = 0, n = 0; for (auto& [slot, d] : d0) if (G.sold[slot].alive) { ++n; if (glm::length(G.sold[slot].feet - ob->pos) < d - 5) ++closer; }
      std::snprintf(b, sizeof b, "after 25 s, %d of %d surviving first-wave attackers are >= 5 m closer to the objective", closer, n);
      check(n == 0 || closer * 2 >= n, b);
      int spread = 0, inObj = 0; float minPair = 1e9f;
      std::vector<vec3> dp;
      int nd = 0;
      for (auto& s : G.sold) if (s.alive && s.role == CombatRole::Defender && !s.human) { ++nd; if (ob->contains(s.feet)) ++inObj; if (s.br.mode == Brain::M_HOLD) dp.push_back(s.feet); }
      for (size_t i = 0; i < dp.size(); ++i) for (size_t j = i + 1; j < dp.size(); ++j) minPair = std::min(minPair, glm::length(dp[i] - dp[j]));
      (void)spread;
      std::string modes; const char* mn[] = {"idle", "advance", "hold", "engage", "cover", "investigate", "evade", "return", "reload", "throw"};
      for (auto& s : G.sold) if (s.alive && s.role == CombatRole::Defender && !s.human) modes += std::string(mn[s.br.mode]) + "/" + s.br.why + "; ";
      std::snprintf(b, sizeof b, "defender bots over 15-25 s: inside the capture volume %.0f %% of samples; holding bots never closer than %.1f m (now: %d of %d inside) [%s]", 100.0 * inSamples / std::max(1, allSamples), minHoldPair, inObj, nd, modes.c_str());
      check(inSamples * 10 >= allSamples * 6 && minHoldPair > 2.5f, b);
      summary["defendSpots"] = G.defSpots.size();
      return true; });
    push("combat", [this](App& a, float t) {
      Game& G = *gGame;
      auto c0 = std::chrono::steady_clock::now(); (void)c0;
      if (t < 60.0f && G.match.phase == MatchPhase::Running) return false;
      char b[300]; std::snprintf(b, sizeof b, "after %.0f s more: bots fired %d rounds, %d hit soldiers, %d kills, %d frag + %d smoke thrown, %d doors opened, %d nav patches",
                                 t, G.shotsByBots, G.hitsOnSoldiers, G.kills, G.fragThrows, G.smokeThrows, G.doorOpens, G.navRebuilds);
      check(G.shotsByBots > 50 && G.hitsOnSoldiers > 0, b);
      { extern int gGoreHitsReported; check(gGoreHitsReported == G.hitsOnSoldiers, "every bullet hit on a soldier is reported to the gore hook (" + std::to_string(gGoreHitsReported) + " FleshHit of " + std::to_string(G.hitsOnSoldiers) + " hits)"); }
      summary["combat60"] = {{"shots", G.shotsByBots}, {"hits", G.hitsOnSoldiers}, {"kills", G.kills}, {"frags", G.fragThrows}, {"smokes", G.smokeThrows}, {"doors", G.doorOpens}, {"progress0", G.match.obj[0].progress}, {"active", G.match.active}};
      // lives: every attacker spawn beyond the first per slot spent exactly one respawn
      int spawns = 0, lives = 0; for (auto& r : G.match.slots) if (r.role == CombatRole::Attacker) { spawns += r.spawns; lives += r.livesLeft; }
      std::snprintf(b, sizeof b, "attacker accounting: %d spawns, %d lives left -> %d (= 60 initial lives)", spawns, lives, spawns + lives);
      check(spawns + lives == 60, b);
      int agents = 0, alive = 0; for (auto& s : G.sold) { if (s.agent >= 0) ++agents; if (s.alive && !s.localPlayer) ++alive; }
      std::snprintf(b, sizeof b, "no orphan pawns: %d physics agents for %d living bots", agents, alive);
      check(agents == alive, b);
      std::snprintf(b, sizeof b, "objective 1 progress %.1f %% (state %d), active objective %d", G.match.obj[0].progress, (int)G.match.obj[0].state, G.match.active + 1);
      check(true, b);
      return true; });
    push("screens", [this](App& a, float t) {
      Game& G = *gGame;
      if (t == 0) { G.debugOverlay = true; }
      if (t < 0.5f) return false;
      // a close look at a living soldier of each side
      for (int side = 0; side < 2; ++side) {
        for (auto& s : G.sold) if (s.alive && !s.human && (int)s.role == side) {
          vec3 f = App::forward(s.yaw, 0);
          vec3 cam = s.feet + f * 3.0f + vec3(0.8f, 1.5f, 0);
          (void)cam;
          break;
        }
      }
      a.uiShot = exeDir() / "selftest_out" / "ai_hud.png";
      return true; });
    push("grenade inventory", [this](App& a, float t) {
      Game& G = *gGame;
      static int s0 = -1; static int before = 0; static uint32_t live0 = 0; static bool c1 = false, c2 = false;
      if (t == 0) {
        s0 = -1; for (auto& s : G.sold) if (s.alive && !s.human && s.role == CombatRole::Attacker && s.frags > 0 && s.throwT < 0) { s0 = s.slot; break; }
        if (s0 >= 0) { G.sold[s0].reload = 0; G.sold[s0].br.grenadeCD = 99; }   // a ready soldier; its own AI does not throw meanwhile
        if (s0 < 0) { check(false, "no attacker with a grenade to test"); return true; }
        Soldier& s = G.sold[s0]; before = s.frags;
        live0 = (uint32_t)G.gren.live.size(); c1 = c2 = false;
        bool started = G.startThrow(s, GK_FRAG, App::forward(s.yaw, 30) * 12.0f, vec3(0));
        if (!started) { check(false, "throw could not start"); return true; }
        return false;
      }
      Soldier& s = G.sold[s0];
      if (t < 0.25f) { if (!c1) { c1 = true; check(s.frags == before && s.throwT >= 0, "throw started: nothing spent before the release"); } return false; }
      if (!c2) {   // cancel test on a second soldier: killed mid wind-up
        c2 = true;
        int s1 = -1; for (auto& o : G.sold) if (o.alive && !o.human && o.slot != s0 && o.frags > 0 && o.throwT < 0) { s1 = o.slot; break; }
        if (s1 >= 0) {
          Soldier& o = G.sold[s1]; o.reload = 0; o.br.grenadeCD = 99; int f = o.frags; size_t n = a.grenades.size();
          G.startThrow(o, GK_FRAG, vec3(0, 5, 5), vec3(0));
          G.killSoldier(s1, -1, vec3(0, 0, 1));
          check(o.frags == f && a.grenades.size() == n, "thrower killed before release: no grenade released, inventory unchanged (" + std::to_string(f) + ")");
        }
        return false;
      }
      if (t < 1.2f) return false;
      check(s.frags == before - 1, "release spends exactly one frag (" + std::to_string(before) + " -> " + std::to_string(s.frags) + ")");
      check(G.gren.live.size() >= live0 + 1 || a.grenadesExploded > 0, "the released grenade is a live body of the shared grenade core (App::grenades), owned by the thrower's slot");
      // released grenade survives its thrower
      if (!G.gren.live.empty()) { uint32_t id = G.gren.live.back().id; G.killSoldier(s0, -1, vec3(0, 0, 1)); bool still = false; for (auto& g : G.gren.live) if (g.id == id) still = true; check(still, "a released grenade stays live when its thrower dies"); }
      return true; });
    push("blast occlusion", [this](App& a, float t) {
      Game& G = *gGame;
      if (t < 0.1f) return false;
      // two victims at the same distance from a blast: one in the open, one behind the town hall wall
      int v1 = -1, v2 = -1; for (auto& s : G.sold) if (s.alive && !s.human) { if (v1 < 0) v1 = s.slot; else if (v2 < 0) { v2 = s.slot; break; } }
      if (v2 < 0) { check(false, "need two living bots"); return true; }
      bool ff = G.match.rules.friendlyFire; G.match.rules.friendlyFire = true;
      // find a wall: cast from the street toward the town hall
      Physics::Hit h; vec3 o(-24, 1.2f, 20);
      bool wall = Physics::castRayEx(o, vec3(0, 0, -1), 30, h);
      vec3 blast = h.pos + vec3(0, -0.6f, 2.5f);      // 2.5 m in front of the wall
      vec3 behind = h.pos + vec3(0, -1.2f, -3.0f);    // inside, behind the wall
      vec3 open = blast + vec3(5.5f, -0.6f, 0);
      auto place = [&](int s, vec3 p) { Physics::Hit g; if (Physics::castRayEx(p + vec3(0, 1.5f, 0), vec3(0, -1, 0), 4, g)) p = g.pos; G.sold[s].feet = p; Physics::setAgentPos(G.sold[s].agent, p); G.sold[s].hp = 1000; G.sold[s].protectUntil = -1; };   // spawn protection (W1) would ignore the blast
      place(v1, open); place(v2, behind);
      float d1 = glm::length(G.chest(G.sold[v1]) - blast), d2 = glm::length(G.chest(G.sold[v2]) - blast);
      G.explosionDamage(blast, 0, 0, -1, true);
      float l1 = 1000 - G.sold[v1].hp, l2 = 1000 - G.sold[v2].hp;
      G.sold[v1].hp = G.sold[v2].hp = 100; G.match.rules.friendlyFire = ff;
      char b[200]; std::snprintf(b, sizeof b, "frag blast: %.0f HP to a soldier in the open (%.1f m), %.0f HP behind a solid wall (%.1f m)", l1, d1, l2, d2);
      check(wall && l1 > 40 && l2 < l1 * 0.2f, b);
      return true; });
    push("smoke", [this](App& a, float t) {
      Game& G = *gGame;
      static vec3 c;
      static bool early = false;
      if (t == 0) {
        c = vec3(0, 0, 60); Physics::Hit g; if (Physics::castRayEx(c + vec3(0, 5, 0), vec3(0, -1, 0), 10, g)) c = g.pos;
        G.gren.addCloud(c, G.t, 7); early = false;
        return false;
      }
      if (!early && t >= 0.3f) {
        early = true;
        vec3 a0 = c + vec3(-12, 1.6f, 0), b0 = c + vec3(12, 1.6f, 0);
        float tr = G.gren.transmittance(a0, b0, G.t);
        check(tr > 0.05f, "smoke builds up over 2 s: transmittance through the centre after 0.3 s = " + std::to_string(tr).substr(0, 5));
      }
      if (t < 3.0f) return false;
      vec3 a0 = c + vec3(-12, 1.6f, 0), b0 = c + vec3(12, 1.6f, 0);
      float centre = G.gren.transmittance(a0, b0, G.t), edge = G.gren.transmittance(a0 + vec3(0, 0, 4.3f), b0 + vec3(0, 0, 4.3f), G.t), clear = G.gren.transmittance(a0 + vec3(0, 0, 9), b0 + vec3(0, 0, 9), G.t);
      char b[200]; std::snprintf(b, sizeof b, "dense smoke: transmittance through the core %.4f, near the edge %.2f, beside it %.2f (thin edges differ from the full cloud)", centre, edge, clear);
      check(centre < 0.02f && edge > centre * 5 && clear > 0.99f, b);
      float L = 0; G.lineOfSight(a0, b0, &L);
      check(L < 0.05f, "AI line of sight through the cloud reports the same opacity (" + std::to_string(L).substr(0, 6) + ")");
      return true; });
    push("perf", [this](App& a, float t) {
      if (t == 0) { stepMsSum = frameMsSum = 0; stepMsN = frameN = 0; }
      frameMsSum += ImGui::GetIO().DeltaTime * 1000; ++frameN;
      if (t < 10) return false;
      Game& G = *gGame;
      int alive = 0; for (auto& s : G.sold) alive += s.alive;
      char b[300]; std::snprintf(b, sizeof b, "%d soldiers alive + smoke: mean frame %.1f ms over %d frames (render cpu %.2f ms, gpu %.2f ms); match step %.2f ms avg, A* %.2f ms avg over %d paths (shared GPU, indicative only)", alive, frameMsSum / frameN, frameN, a.R.stats.cpuMs, a.R.stats.gpuMs, G.stepMs / std::max(1, G.stepCalls), G.pathMs / std::max(1, G.pathCalls), G.pathCalls);
      check(frameN > 0, b);
      summary["perf"] = {{"frameMs", frameMsSum / frameN}, {"alive", alive}};
      return true; });
    push("run on", [this](App& a, float t) {
      Game& G = *gGame;
      if (t < 120 && G.match.phase == MatchPhase::Running) return false;
      int caps = 0; for (auto& o : G.match.obj) caps += o.state == ObjState::Captured;
      char b[300]; std::snprintf(b, sizeof b, "after %.0f s of play: %d objectives captured, active %d at %.1f %%, %d kills, %d attacker lives left, phase %d (%s)", G.match.time, caps, G.match.active + 1, G.match.obj[std::min<int>(G.match.active, (int)G.match.obj.size() - 1)].progress, G.kills, G.match.unusedAttackerLives(), (int)G.match.phase, G.match.result.c_str());
      check(G.match.time > 100, b);
      summary["run"] = {{"time", G.match.time}, {"captured", caps}, {"kills", G.kills}, {"livesLeft", G.match.unusedAttackerLives()}, {"phase", (int)G.match.phase}, {"frags", G.fragThrows}, {"smokes", G.smokeThrows}, {"doors", G.doorOpens}, {"navPatches", G.navRebuilds}};
      int agents = 0, alive = 0; for (auto& s : G.sold) { if (s.agent >= 0) ++agents; if (s.alive && !s.localPlayer) ++alive; }
      check(agents == alive, "still no orphan pawns: " + std::to_string(agents) + " agents, " + std::to_string(alive) + " living bots");
      a.uiShot = exeDir() / "selftest_out" / "ai_late.png";
      return true; });
    push("exit", [this](App& a, float t) {
      if (t < 0.2f) return false;
      a.exitExplore();
      check(!gGame->active && gGame->sold.empty() && a.mode == Mode::Creative, "leaving the match clears soldiers, agents and grenades");
      return true; });
    push("creative ui", [this](App& a, float t) {
      // the Scenario panel, its overlay (capture cylinders, attack arrows, spawn markers) and the setup window
      if (t == 0) {
        gUi.showPanel = true; gUi.showOverlay = true; gUi.showSetup = true; gUi.setupMode = 0; gUi.loaded = false;
        gUi.selObj = gUi.scenario(a).objectives.empty() ? "" : gUi.scenario(a).objectives[1].id;
        a.camPos = vec3(-5, 55, 75); a.camYaw = 0; a.camPitch = -38;
        return false;
      }
      if (t < 1.0f) return false;
      a.uiShot = exeDir() / "selftest_out" / "ai_creative_ui.png";
      check(true, "Creative scenario panel, overlay and setup window captured (selftest_out/ai_creative_ui.png)");
      return true; });
    push("done", [this](App& a, float t) { if (t < 0.2f) return false; gUi.showPanel = gUi.showSetup = false; return true; });
    return;
  }
  // sequence runner
  if (cur >= seq.size()) { app.useScripted = false; finish(app); return; }
  auto c0 = std::chrono::steady_clock::now();
  bool done = seq[cur].fn(app, st);
  (void)c0;
  st += dt;
  if (done) { ++cur; st = 0; }
}
