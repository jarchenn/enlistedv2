// SD5 --selftest-sd-plant [map]: do the attacker bots get the bomb planted? Bots only (the local player sits out), 5 v 5,
// NMK_SD_ROUNDS rounds (default 20) of normal play on the map (default templates/sd_desert.nmap.json: the harness swaps
// clutter variants in, tools/sd5_variants.sh). Per round: did an attacker reach a plant pad, did a plant start, how long
// it took. A round without a plant logs each attacker's last state: what it was doing (br.why), where it stood, how far
// from the target pad, how long it had not moved, its stuck ladder / path failures, and whether the navmesh has a path
// from there to the pad. selftest_out/report_sdplant.txt (the summary line: "plant rate").
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "ai_core.h"
#include <cstdlib>

namespace {
MpTest* T = nullptr;
int mode = -1, rounds = 20, lastRound = 0, done = 0, planted = 0, reached = 0; bool started = false, rPlanted = false, rReached = false;
double rPlantAt = -1, nextSample = 0; float total = 0;
struct Sample { vec3 p{0}; std::string why; float speed = 0; int ladder = 0; float pathFails = 0; double stillSince = 0; vec3 anchor{0}; bool alive = false; double diedAt = -1; };
std::map<int, Sample> last;
std::vector<int> noPlantSeeds;
// SD9: "ran past": a bot moved more than 3 m while a visible enemy was within 25 m and it did not fire (per side)
struct Pass { float acc = 0; vec3 last{0}; bool on = false; };
std::map<int, Pass> passes; int passBy[2] = {0, 0}, timeouts = 0, attWins = 0;

int padAt(Game& g, vec3 p) { for (int k = 0; k < 2 && k < (int)g.match.sc.objectives.size(); ++k) if (g.match.sc.objectives[k].contains(p)) return k; return -1; }
}   // namespace

bool sdPlantSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-plant") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sdplant");
    if (const char* e = std::getenv("NMK_SD_ROUNDS")) rounds = std::max(1, std::atoi(e));
    std::string map = "templates/sd_desert.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-plant" && __argv[k + 1][0] != '-') map = __argv[k + 1];
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / map);
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 0 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok && sdMatch(), "Search and Destroy starts on " + map + " (" + (gGame ? gGame->setupError : std::string("-")) + ")");
    if (SdMatch* m = sdMatch()) { m->cfg.freeze = 3; m->cfg.roundEnd = 1.5f; m->cfg.halfRounds = rounds + 2; m->cfg.winRounds = rounds + 2; }
    if (ok) {   // the pads reachable on the navmesh from the attackers' spawn
      Game& g = *gGame; vec3 sp(0); int n = 0; for (auto& mk : g.match.sc.spawns) if (mk.team < 0 && mk.role == CombatRole::Attacker) { sp += mk.pos; ++n; }
      if (n) sp /= (float)n;
      for (int k = 0; k < 2 && k < (int)g.match.sc.objectives.size(); ++k) { std::vector<vec3> pts; float len = 0; bool p = g.nav.pathSync(sp, g.match.sc.objectives[k].pos, pts, nullptr, nullptr, &len);
        T->note(std::string("site ") + char('A' + k) + " pad from the attackers' spawn: " + (p ? "a path of " + std::to_string((int)len) + " m" : "NO PATH")); }
    }
  }
  total += dt;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() {
    char b[360]; std::snprintf(b, sizeof b, "plant rate: a plant started in %d of %d rounds (%.0f %%); an attacker reached a pad in %d; lost on time %d; attacker wins %d; ran past a visible enemy (3 m, 25 m, no shot): attackers %d, defenders %d",
                                planted, done, done ? 100.0 * planted / done : 0.0, reached, timeouts, attWins, passBy[0], passBy[1]);
    T->note(b);
    T->check(done >= rounds, "played " + std::to_string(done) + " of " + std::to_string(rounds) + " rounds");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.selftest->exitCode = T->finish(); app.selftest->done = true;
  };
  if (!mp || !g.active) { if (!app.selftest->done) finish(); return true; }
  SdMatch& m = *mp;
  if (total > 60.0f * 4.0f * rounds) { T->note("timed out"); finish(); return true; }
  if (Soldier* L = g.local(); L && L->alive && m.phase == SdPhase::LiveUnplanted && m.t - m.liveStart > 0.2) g.killSoldier(L->slot, -1, vec3(0, 0, 1));   // bots only
  if (m.round != lastRound && m.phase == SdPhase::Freeze) { lastRound = m.round; rPlanted = rReached = false; rPlantAt = -1; last.clear(); }
  const bool live = m.phase == SdPhase::LiveUnplanted || m.phase == SdPhase::LivePlanted;
  if (live) {
    for (auto& s : g.sold) {   // SD9: the run-past measure
      Pass& p = passes[s.slot];
      if (!s.alive || s.human || s.localPlayer) { p.on = false; continue; }
      bool near = false;
      for (auto& x : s.br.mem) if (x.visible && !x.heardOnly && x.enemy >= 0 && x.enemy < (int)g.sold.size() && g.sold[x.enemy].alive && glm::length(g.sold[x.enemy].feet - s.feet) < 25.0f) near = true;
      if (!near || g.t - s.lastShot < 0.5) { p.acc = 0; p.on = near; p.last = s.feet; continue; }
      if (p.on) p.acc += glm::length(vec2(s.feet.x - p.last.x, s.feet.z - p.last.z));
      p.on = true; p.last = s.feet;
      if (p.acc > 3.0f) { ++passBy[std::clamp(m.sideOf(s.slot), 0, 1)]; p.acc = 0; }
    }
    if (!rPlanted && (m.planter >= 0 || m.bomb == SdBomb::Planted)) { rPlanted = true; rPlantAt = m.t - m.liveStart; }
    for (auto& s : g.sold) {
      if (m.sideOf(s.slot) != 0) continue;
      Sample& x = last[s.slot];
      if (!s.alive) { if (x.alive) { x.alive = false; x.diedAt = m.t - m.liveStart; } continue; }
      if (padAt(g, s.feet) >= 0) rReached = true;
      x.alive = true; x.p = s.feet; x.why = s.br.why; x.speed = s.speed;
      if (AiCore* A = aiCore()) { BotMind& bm = A->mind(s); x.ladder = bm.ladder; x.pathFails = bm.pathFails; }
      if (glm::length(s.feet - x.anchor) > 1.5f) { x.anchor = s.feet; x.stillSince = m.t; }
    }
  }
  if (m.phase == SdPhase::RoundEnd && m.round > 0 && done < m.round) {
    done = m.round; planted += rPlanted; reached += rReached;
    const bool attWin = m.roundWinner == m.attackerTeam;
    timeouts += m.reason == SdReason::Time; attWins += attWin; passes.clear();
    char b[300]; std::snprintf(b, sizeof b, "round %d: %s win (%s), plant %s, pad reached %d", m.round, attWin ? "attackers" : "defenders", sdReasonName(m.reason),
                                rPlanted ? ("started at " + std::to_string((int)rPlantAt) + " s").c_str() : "NONE", (int)rReached);
    T->note(b);
    if (!rPlanted) {   // why not: every attacker's last state
      const Objective* pads[2] = {&g.match.sc.objectives[0], &g.match.sc.objectives[1]};
      for (auto& [slot, x] : last) {
        const Soldier& s = g.sold[slot];
        int near = glm::length(x.p - pads[0]->pos) < glm::length(x.p - pads[1]->pos) ? 0 : 1;
        std::vector<vec3> pts; float len = 0; bool path = g.nav.pathSync(x.p, pads[near]->pos, pts, nullptr, nullptr, &len);
        char c[420]; std::snprintf(c, sizeof c, "   attacker %d%s: %s at (%.1f %.1f %.1f), %.0f m from pad %c (navmesh path %s %.0f m), still for %.0f s, speed %.1f, stuck ladder %d, path fails %.0f, doing '%s'",
          slot, m.carrier == slot ? " (carrier)" : "", x.alive ? "alive" : ("died at " + std::to_string((int)x.diedAt) + " s").c_str(), x.p.x, x.p.y, x.p.z,
          glm::length(x.p - pads[near]->pos), 'A' + near, path ? "yes" : "NO", len, x.alive ? m.t - x.stillSince : 0.0, x.speed, x.ladder, x.pathFails, x.why.c_str());
        T->note(c); (void)s;
      }
    }
  }
  if (done >= rounds || m.phase == SdPhase::MatchEnd) finish();
  return true;
}
