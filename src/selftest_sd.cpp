// SD1 Search and Destroy selftest (--selftest-sd [map]): the pure round logic (sdLogicSelfCheck: precedence, ties, the
// economy ladder, MR15 / overtime, buying), then bot rounds 5 v 5 on a map (default Chateau town, temporary layout) with
// short timers, one round per win condition driven through the real game: defenders eliminated, attackers eliminated,
// time out, a bot plant that detonates, a bot plant a defender bot defuses; halftime swaps the sides and resets the money;
// then free rounds of bots on their own. selftest_out/report_sd.txt.
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include <cstdlib>

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0; double stageAt = 0; bool started = false, forcedThisRound = false, sawHalftime = false;
int lastRound = 0, freeRounds = 0; float total = 0; fs::path pendingShot; bool shotPlanted = false, shotFreeze = false;
SdReason want = SdReason::None; int wantSide = -1;
std::map<int, int> reasons;

bool chk(bool ok, const std::string& s) { return T->check(ok, s); }
bool startSd(App& a, const std::string& map) {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / map);
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
  for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 0 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
void kill(Game& g, int side) { SdMatch& m = *sdMatch(); for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == side) g.killSoldier(s.slot, -1, vec3(0, 0, 1)); }
void put(Game& g, Soldier& s, vec3 p) { if (s.localPlayer) Physics::setCharacterPos(p); else if (s.agent >= 0) Physics::setAgentPos(s.agent, p); s.feet = p; s.vel = vec3(0); }
const char* sideName(int s) { return s == 0 ? "attackers" : "defenders"; }
}   // namespace

bool sdSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) {
    started = true; T = new MpTest("sd");
    auto bad = sdLogicSelfCheck();
    for (auto& b : bad) chk(false, "round logic: " + b);
    chk(bad.empty(), "round logic self-check (precedence, deadline and detonation ties, latch, loss ladder, timeout pay, MR15 / overtime, buying): " + std::to_string(bad.size()) + " failures");
    std::string map = "templates/chateau_town.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd" && __argv[k + 1][0] != '-') map = __argv[k + 1];
    chk(startSd(app, map), "Search and Destroy starts on " + map + " (" + (gGame ? gGame->setupError : std::string("no game")) + ")");
    SdMatch* m = sdMatch();
    chk(m != nullptr && gGame->match.sc.objectives.size() == 2, "two bomb sites, the controller running");
    if (m) { m->cfg.freeze = 2; m->cfg.roundEnd = 1.5f; m->cfg.halftime = 2; m->cfg.halfRounds = 3; m->cfg.winRounds = 4; }
    stage = 0; stageAt = 0;
  }
  total += dt;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() {
    for (auto& [r, n] : reasons) T->note(std::string(sdReasonName((SdReason)r)) + ": " + std::to_string(n) + " rounds");
    if (mp) { char b[200]; std::snprintf(b, sizeof b, "score Team A %d - %d Team B after %d rounds", mp->score[0], mp->score[1], mp->round); T->note(b); }
    if (app.mode == Mode::Explore) app.exitExplore();
    app.selftest->exitCode = T->finish(); app.selftest->done = true;
  };
  if (!mp || !g.active) { if (!app.selftest->done) finish(); return true; }
  SdMatch& m = *mp;
  if (total > 1500) { chk(false, "timed out"); finish(); return true; }
  // the local player sits out (dead each round): bots decide every round
  if (Soldier* L = g.local(); L && L->alive && m.phase == SdPhase::LiveUnplanted && m.t - m.liveStart > 0.2) g.killSoldier(L->slot, -1, vec3(0, 0, 1));
  if (m.phase == SdPhase::Halftime && !sawHalftime) {
    sawHalftime = true; int cash = 0; for (auto& p : m.players) cash += p.cash == m.cfg.startCash;
    bool roles = true; for (auto& s : g.sold) roles &= (s.role == CombatRole::Attacker) == (s.team == m.attackerTeam);
    chk(m.round == 3 && m.attackerTeam == 0 && cash == (int)m.players.size() && roles, "halftime after round 3: Team A attacks now, roles follow, everyone back to $800");
  }
  if (m.round != lastRound && m.phase == SdPhase::Freeze) {   // a new round: arm this round's forced case
    lastRound = m.round; forcedThisRound = false; stageAt = m.t;
    m.cfg.roundTime = m.round == 3 ? 6.0f : 115.0f; m.cfg.fuse = m.round == 4 ? 12.0f : 40.0f;
    want = m.round == 1 ? SdReason::Elimination : m.round == 2 ? SdReason::Elimination : m.round == 3 ? SdReason::Time : m.round == 4 ? SdReason::Detonation : m.round == 5 ? SdReason::Defuse : SdReason::None;
    wantSide = m.round == 1 ? 0 : m.round == 2 ? 1 : m.round == 3 ? 1 : m.round == 4 ? 0 : m.round == 5 ? 1 : -1;
    int armed = 0, vest = 0; for (auto& p : m.players) { armed += !p.gear.primary.empty(); vest += p.gear.armor > 0; }
    T->note("round " + std::to_string(m.round) + ": " + std::to_string(armed) + " primaries, " + std::to_string(vest) + " vests bought");
  }
  const double live = m.phase == SdPhase::LiveUnplanted || m.phase == SdPhase::LivePlanted ? m.t - m.liveStart : -1;
  if (live >= 1.0 && !forcedThisRound) {
    if (m.round == 1) { forcedThisRound = true; kill(g, 1); }
    if (m.round == 2) { forcedThisRound = true; kill(g, 0); }
    if ((m.round == 4 || m.round == 5) && m.carrier >= 0) {   // the carrier straight into site A: the bot plants by itself
      forcedThisRound = true; vec3 p = g.match.sc.objectives[0].pos; p.y = g.app.terrain.height(p.x, p.z) + 0.1f; put(g, g.sold[m.carrier], p);
    }
  }
  { static double nextLog = 0; if (live >= 0 && m.t >= nextLog && (m.round == 4 || m.round >= 6)) { nextLog = m.t + 3;   // diagnostics: the carrier on its way
      if (m.carrier >= 0) { const Soldier& c = g.sold[m.carrier]; const Objective& A = g.match.sc.objectives[0]; const Objective& B = g.match.sc.objectives[1];
        char b[260]; std::snprintf(b, sizeof b, "r%d t%.0f carrier %d at (%.1f %.1f %.1f) speed %.2f, %.1f m from A (in %d), %.1f m from B (in %d), holding %d, why %s", m.round, live, m.carrier, c.feet.x, c.feet.y, c.feet.z, c.speed,
          glm::length(vec2(c.feet.x - A.pos.x, c.feet.z - A.pos.z)), (int)A.contains(c.feet), glm::length(vec2(c.feet.x - B.pos.x, c.feet.z - B.pos.z)), (int)B.contains(c.feet), (int)sdBotHolding(g, c), c.br.why.c_str()); T->note(b); } } }
  if (!shotFreeze && m.round == 2 && m.phase == SdPhase::Freeze && m.t - m.phaseEnd > -1.0) { shotFreeze = true; app.uiShot = exeDir() / "selftest_out" / "sd_freeze.png"; }   // the HUD in freeze
  if (!shotPlanted && m.round == 4 && m.phase == SdPhase::LivePlanted) {   // the HUD with the bomb planted (spectating a teammate)
    shotPlanted = true; app.uiShot = exeDir() / "selftest_out" / "sd_planted.png";
  }
  if (m.round == 4 && m.phase == SdPhase::LivePlanted)   // nobody comes to defuse: it detonates
    for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1 && glm::length(s.feet - m.bombPos) < 60.0f) { vec3 far = m.bombPos + glm::normalize(s.feet - m.bombPos + vec3(0.01f, 0, 0)) * 70.0f; far.y = g.app.terrain.height(far.x, far.z) + 0.1f; put(g, s, far); }
  if (m.round == 5 && m.phase == SdPhase::LivePlanted && stage == 0) {   // the attackers fall; a defender bot comes to defuse
    stage = 1; kill(g, 0);
    for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1) { vec3 p = m.bombPos + vec3(4, 0, 0); p.y = g.app.terrain.height(p.x, p.z) + 0.1f; put(g, s, p); break; }
  }
  if (m.phase == SdPhase::RoundEnd && m.round > 0 && reasons.size() < 1000) {
    static int logged = 0;
    if (logged != m.round) {
      logged = m.round; ++reasons[(int)m.reason];
      const int winSide = m.roundWinner == m.attackerTeam ? 0 : 1;
      char b[240]; std::snprintf(b, sizeof b, "round %d: %s win, %s (%.1f s live%s)", m.round, sideName(winSide), sdReasonName(m.reason), (float)(m.t - m.liveStart), m.planted ? ", planted" : "");
      if (want != SdReason::None) chk(m.reason == want && winSide == wantSide, b); else { T->note(b); ++freeRounds; }
      if (m.round == 5) stage = 0;
    }
  }
  if (freeRounds >= 1 || m.phase == SdPhase::MatchEnd) {
    chk(freeRounds >= 1 || m.phase == SdPhase::MatchEnd, "bots play free rounds to a result");
    finish();
  }
  return true;
}
