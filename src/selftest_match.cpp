// --selftest-match: headless unit suite for the scenario data and the server match controller (no window, no GL).
// Capture formula and times, ordered objectives, life budgets, spawn-failure accounting, wave timing, the end-of-match
// precedence rule, freeze on end, lanes (largest remainder), roster limits, join / leave, scenario save / migrate /
// reorder / validate.
#include "match.h"
#include "mptest.h"
#include "navmeta.h"
#include "net.h"
#include <cmath>
#include <fstream>

namespace {
Scenario testScenario(int nObj) {
  Scenario s;
  for (int k = 0; k < nObj; ++k) {
    Objective o; o.id = s.newId("obj"); o.label = std::string("Point ") + char('A' + k); o.order = k; o.pos = vec3(0, 0, -60.0f * k); o.radius = 15;
    s.objectives.push_back(o);
    SpawnMarker a; a.id = s.newId("spawn"); a.role = CombatRole::Attacker; a.phases = {k}; a.pos = o.pos + vec3(0, 0, 80); s.spawns.push_back(a);
    SpawnMarker d; d.id = s.newId("spawn"); d.role = CombatRole::Defender; d.phases = {k}; d.pos = o.pos + vec3(0, 0, -20); s.spawns.push_back(d);
  }
  s.rules.captureModel = CaptureModel::Contest;   // the milestone formula cases below; v2 timed cases set Timed
  return s;
}
// spawn everyone that is queued (at their marker area), as the game layer would
void spawnAll(Match& m, vec3 atkPos, vec3 defPos) {
  for (int id : m.spawnQueue()) { m.onSpawned(id); m.reportAlive(id, true, m.slots[id].role == CombatRole::Attacker ? atkPos : defPos); }
}
// put exactly A attackers and D defenders inside the active objective (others far away / alive)
void occupy(Match& m, int A, int D) {
  vec3 c = m.sc.objectives[m.active].pos;
  int a = 0, d = 0;
  for (auto& s : m.slots) {
    if (!s.alive) continue;
    bool in = s.role == CombatRole::Attacker ? (a++ < A) : (d++ < D);
    m.reportAlive(s.id, true, in ? c + vec3(1, 0, 1) : c + vec3(500, 0, 0));
  }
}
int errorsIn(const Scenario& s) { int e = 0; for (auto& i : validateScenario(s, true)) e += i.error; return e; }
float timeToCapture(int A, int D, int attackers, int defenders) {
  Scenario s = testScenario(3); s.rules.attackerAI = attackers; s.rules.defenderSlots = std::max(1, defenders);
  Match m; std::string err; m.setup(s, 0, err); m.start();
  spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
  occupy(m, A, D);
  float t = 0;
  while (m.obj[0].state != ObjState::Captured && t < 400) { m.update(1.0f / 60.0f); t += 1.0f / 60.0f; }
  return t;
}
}

int runMatchSelfTest() {
  MpTest T("match");
  // ---------------------------------------------------------------- capture formula
  T.check(captureRate(0, 0) == 0 && captureRate(3, 3) == 0 && captureRate(2, 5) == 0, "rate 0 when A <= D (0v0, 3v3 tie, 2v5)");
  T.check(std::fabs(captureRate(1, 0) - 1.0f) < 1e-5f, "1v0: 5 * 1 * 0.2 = 1.0 %/s");
  T.check(std::fabs(captureRate(10, 5) - 5.0f / 3.0f) < 1e-5f, "10v5: 5 * (5/15) * 1 = 1.667 %/s");
  struct C { int A, D; float want; } cases[] = {{1, 0, 100}, {3, 0, 33.333f}, {5, 0, 20}, {10, 0, 20}, {10, 5, 60}};
  for (auto& c : cases) {
    float t = timeToCapture(c.A, c.D, 10, 5);
    char b[160]; std::snprintf(b, sizeof b, "%dv%d stable occupancy captures in %.2f s (spec %.1f s)", c.A, c.D, t, c.want);
    T.check(std::fabs(t - c.want) < 0.1f, b);
  }
  {
    Scenario s = testScenario(3); Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    occupy(m, 5, 0); for (int k = 0; k < 600; ++k) m.update(1.0f / 60.0f);   // 10 s at 5 %/s = 50 %
    float p50 = m.obj[0].progress;
    occupy(m, 3, 3); for (int k = 0; k < 600; ++k) m.update(1.0f / 60.0f);
    float pTie = m.obj[0].progress;
    occupy(m, 0, 0); for (int k = 0; k < 600; ++k) m.update(1.0f / 60.0f);
    float pEmpty = m.obj[0].progress;
    occupy(m, 0, 4); for (int k = 0; k < 600; ++k) m.update(1.0f / 60.0f);
    char b[200]; std::snprintf(b, sizeof b, "progress %.2f %% after 10 s 5v0; tie 3v3 keeps %.2f; empty keeps %.2f; defenders-only keeps %.2f (never decays)", p50, pTie, pEmpty, m.obj[0].progress);
    T.check(std::fabs(p50 - 50) < 0.1f && pTie == p50 && pEmpty == p50 && m.obj[0].progress == p50, b);
    // later objectives stay locked even when occupied
    for (auto& sl : m.slots) if (sl.alive && sl.role == CombatRole::Attacker) m.reportAlive(sl.id, true, m.sc.objectives[2].pos);
    for (int k = 0; k < 300; ++k) m.update(1.0f / 60.0f);
    T.check(m.obj[2].state == ObjState::Locked && m.obj[2].progress == 0 && m.obj[1].progress == 0, "occupying locked objective 3 does nothing (only the active point is capturable)");
    // vertical bounds: an attacker 6 m above the point (another floor) does not count
    occupy(m, 0, 0);
    vec3 c = m.sc.objectives[0].pos;
    int n = 0; for (auto& sl : m.slots) if (sl.alive && sl.role == CombatRole::Attacker && n++ < 5) m.reportAlive(sl.id, true, c + vec3(0, 6.0f, 0));
    m.update(1.0f / 60.0f);
    T.check(m.obj[0].A == 0, "five attackers 6 m above the point (outside yMax 4 m) are not counted");
    // non-finite / zero / negative dt guard
    uint64_t st = m.step; m.update(0); m.update(-1); m.update(std::nanf("")); T.check(m.step == st, "zero / negative / NaN time steps are rejected");
  }
  // ---------------------------------------------------------------- roster and budgets
  {
    Scenario s = testScenario(3); Match m; std::string err;
    T.check(m.setup(s, 1, err), "default setup: " + err);
    int hum = 0, fai = 0, eai = 0, lives = 0;
    for (auto& sl : m.slots) { if (sl.role == CombatRole::Defender) (sl.human ? hum : fai)++; else { eai++; lives += sl.livesLeft; } }
    char b[200]; std::snprintf(b, sizeof b, "singleplayer default: %d human + %d friendly AI vs %d attacking AI; %d attacker lives (spec 1+4 vs 10, 60)", hum, fai, eai, lives);
    T.check(hum == 1 && fai == 4 && eai == 10 && lives == 60, b);
    bool allSix = true; for (auto& sl : m.slots) if (sl.role == CombatRole::Attacker && sl.livesLeft != 6) allSix = false;
    T.check(allSix, "every default attacking slot has exactly 6 lives (1 initial + 5 respawns)");
    m.start();
    // spawn failure spends nothing; success spends one; duplicate success reports never double-spend
    int a0 = -1; for (auto& sl : m.slots) if (sl.role == CombatRole::Attacker) { a0 = sl.id; break; }
    for (int k = 0; k < 30; ++k) m.onSpawnFailed(a0, 1.0f / 60.0f, "test");
    T.check(m.slots[a0].livesLeft == 6 && m.slots[a0].pending, "30 failed placements spend no life (slot stays pending)");
    for (int k = 0; k < 300; ++k) m.onSpawnFailed(a0, 1.0f / 60.0f, "every marker blocked");
    T.check(!m.spawnProblem.empty(), "persistent failure produces an actionable message: \"" + m.spawnProblem.substr(0, 90) + "...\"");
    m.onSpawned(a0); m.onSpawned(a0);
    T.check(m.slots[a0].livesLeft == 5, "successful spawn spends exactly one life; a duplicate report spends none (5 left)");
    spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    // death -> minimum delay -> wave
    m.onDeath(a0, -1);
    for (int k = 0; k < 60 * 2; ++k) m.update(1.0f / 60.0f);
    bool earlyPending = m.slots[a0].pending;
    for (int k = 0; k < 60 * 10; ++k) m.update(1.0f / 60.0f);
    T.check(!earlyPending && m.slots[a0].pending, "dead attacker waits >= 3 s and is released by the next 10 s wave");
    // capture objective 1 -> budgets / living pawns preserved
    int livesBefore = m.unusedAttackerLives(), aliveBefore = m.livingAttackers() + m.livingDefenders();
    occupy(m, 10, 0); for (int k = 0; k < 60 * 21 && m.active == 0; ++k) m.update(1.0f / 60.0f);
    char b2[200]; std::snprintf(b2, sizeof b2, "objective change: active %d, unused attacker lives %d -> %d, living %d -> %d (no refill, no reset)", m.active + 1, livesBefore, m.unusedAttackerLives(), aliveBefore, m.livingAttackers() + m.livingDefenders());
    T.check(m.active == 1 && m.unusedAttackerLives() == livesBefore && m.livingAttackers() + m.livingDefenders() == aliveBefore && m.obj[0].state == ObjState::Captured, b2);
  }
  {
    Scenario s = testScenario(3); s.rules.defenderSlots = 1; s.rules.attackerAI = 1; Match m; std::string err;
    T.check(m.setup(s, 1, err) && m.slots.size() == 2, "zero friendly AI and one enemy AI is a valid roster");
    s.rules.defenderSlots = 5; s.rules.attackerAI = 200; s.rules = ScenarioRules::fromJson(s.rules.toJson());
    T.check(s.rules.defenderSlots + s.rules.attackerAI == ScenarioRules::kMaxCombatants, "rules clamp the roster to " + std::to_string(ScenarioRules::kMaxCombatants) + " combatants (5 + " + std::to_string(s.rules.attackerAI) + ")");
  }
  // ---------------------------------------------------------------- end-of-match precedence
  {
    // last attacker dies in the same step the final capture reaches 100 % -> attackers win
    Scenario s = testScenario(1); s.devTestMap = true; s.rules.attackerAI = 1; s.rules.attackerRespawns = 0; s.rules.defenderSlots = 1;
    Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0), vec3(0, 0, -500));
    int atk = 1; m.reportAlive(atk, true, m.sc.objectives[0].pos);   // occupancy sampled at step start (inside)
    m.obj[0].progress = 99.995f;
    m.onDeath(atk, 0);                // the last attacker (no lives left) is killed during the same step
    m.update(1.0f / 60.0f);           // resolve: capture first, then exhaustion
    T.check(m.phase == MatchPhase::Ended && m.winner == Winner::Attackers, "tie rule: final capture reaching 100 % in the resolved step -> attackers win (" + m.result + ")");
  }
  {
    Scenario s = testScenario(1); s.devTestMap = true; s.rules.attackerAI = 2; s.rules.attackerRespawns = 0; s.rules.defenderSlots = 1;
    Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    m.obj[0].progress = 80;
    m.onDeath(1, 0); m.update(1.0f / 60.0f);
    T.check(m.phase == MatchPhase::Running, "one of two attackers dead (no respawns): match continues");
    m.onDeath(2, 0); m.update(1.0f / 60.0f);
    T.check(m.phase == MatchPhase::Ended && m.winner == Winner::Defenders, "last attacker dead, no lives, nothing pending -> defenders win (" + m.result + ")");
    double t = m.time; float p = m.obj[0].progress; size_t q = m.spawnQueue().size();
    m.reportAlive(1, true, m.sc.objectives[0].pos); for (int k = 0; k < 600; ++k) m.update(1.0f / 60.0f);
    T.check(m.time == t && m.obj[0].progress == p && q == 0 && m.spawnQueue().empty(), "match end freezes scoring, time and spawning");
  }
  {
    // a committed pending respawn keeps the attackers alive in the decision
    Scenario s = testScenario(1); s.devTestMap = true; s.rules.attackerAI = 1; s.rules.attackerRespawns = 1; s.rules.defenderSlots = 1;
    Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    m.onDeath(1, 0); for (int k = 0; k < 60 * 11; ++k) m.update(1.0f / 60.0f);
    bool pend = m.slots[1].pending;
    T.check(m.phase == MatchPhase::Running && pend, "dead attacker with a respawn left is pending, not exhausted");
    m.onSpawned(1); m.onDeath(1, 0); m.update(1.0f / 60.0f);
    T.check(m.phase == MatchPhase::Ended && m.winner == Winner::Defenders && m.slots[1].livesLeft == 0, "after the last respawn dies: defenders win");
  }
  // ---------------------------------------------------------------- lanes
  {
    auto l = allocateLanes(10, 0.2f, 0.6f, 0.2f);
    T.check(l[0] == 2 && l[1] == 6 && l[2] == 2, "10 attackers -> lanes 2 / 6 / 2");
    auto l1 = allocateLanes(1, 0.2f, 0.6f, 0.2f), l3 = allocateLanes(3, 0.2f, 0.6f, 0.2f), l7 = allocateLanes(7, 0.2f, 0.6f, 0.2f), lz = allocateLanes(4, 0.5f, 0.0f, 0.5f);
    char b[200]; std::snprintf(b, sizeof b, "1 -> %d/%d/%d, 3 -> %d/%d/%d, 7 -> %d/%d/%d, 4 with main weight 0 -> %d/%d/%d (main keeps >= 1)", l1[0], l1[1], l1[2], l3[0], l3[1], l3[2], l7[0], l7[1], l7[2], lz[0], lz[1], lz[2]);
    T.check(l1[1] == 1 && l3[0] + l3[1] + l3[2] == 3 && l7[0] + l7[1] + l7[2] == 7 && lz[1] >= 1 && l7[1] == 4, b);
    Scenario s = testScenario(3); Match m; std::string err; m.setup(s, 1, err);
    int c[3] = {0, 0, 0}; for (auto& sl : m.slots) if (sl.role == CombatRole::Attacker) c[sl.lane]++;
    T.check(c[0] == 2 && c[1] == 6 && c[2] == 2, "roster lanes for 10 attackers: 2 left, 6 main, 2 right");
  }
  // ---------------------------------------------------------------- join / leave
  {
    Scenario s = testScenario(3); Match m; std::string err; m.setup(s, 1, err);
    int sl = m.humanJoin(1, "Client 1");
    int hum = 0; for (auto& x : m.slots) hum += x.human;
    T.check(sl >= 0 && hum == 2 && m.slots.size() == 15, "lobby join displaces one friendly AI (2 humans, 3 AI defenders, 15 total)");
    for (int k = 2; k <= 4; ++k) m.humanJoin(k, "Client");
    T.check(m.humanJoin(9, "Late") == -1, "sixth human with 5 defender slots: lobby full");
    m.humanLeave(1);
    hum = 0; for (auto& x : m.slots) hum += x.human;
    T.check(hum == 4 && !m.slots[sl].human && m.slots.size() == 15, "leaving restores an AI in that slot (no orphan, no new slot)");
    m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    int sl2 = m.humanJoin(7, "Mid-match");
    T.check(sl2 >= 0 && !m.slots[sl2].human && m.slots[sl2].pendingPeer == 7 && m.slots[sl2].alive, "in-match join waits for the AI soldier's next spawn (no pawn duplicated)");
    m.onDeath(sl2, -1); for (int k = 0; k < 60 * 9; ++k) m.update(1.0f / 60.0f); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    T.check(m.slots[sl2].human && m.slots[sl2].peer == 7, "the joining human takes the slot through the normal respawn");
  }
  // ---------------------------------------------------------------- scenario data
  {
    Scenario s = testScenario(4);
    std::string id0 = s.objectives[0].id, id1 = s.objectives[1].id;
    int sp1 = 0; for (auto& m : s.spawns) if (m.activeIn(1)) ++sp1;
    s.moveLater(id0);
    T.check(s.objectives[0].id == id1 && s.objectives[1].id == id0 && s.objectives[1].order == 1, "Move Later swaps the sequence and keeps stable ids");
    int sp0now = 0; for (auto& m : s.spawns) if (m.activeIn(0)) ++sp0now;
    T.check(sp0now == sp1, "spawn groups follow their objective when reordered");
    json j = s.toJson();
    Scenario r = Scenario::fromJson(json::parse(j.dump()));
    T.check(r.toJson() == j, "scenario save -> load round trip is identical");
    std::vector<std::string> notes;
    json old = {{"objectives", json::array({{{"pos", {1, 2, 3}}}})}};
    Scenario mig = Scenario::fromJson(old, &notes);
    T.check(mig.objectives.size() == 1 && mig.objectives[0].radius == 15 && mig.objectives[0].yMin == -2 && !mig.objectives[0].id.empty() && !notes.empty(), "version-0 scenario migrates with defaults (radius 15 m, bounds, id): " + (notes.empty() ? std::string() : notes[0]));
    T.check(errorsIn(testScenario(4)) == 0, "a 4-point scenario with spawns per phase validates (no errors)");
    Scenario bad = testScenario(2); bad.spawns.erase(bad.spawns.begin() + 1); bad.objectives[1].order = 5;
    std::string all; for (auto& i : validateScenario(bad, true)) if (i.error) all += i.what + " | ";
    T.check(all.find("no defender spawn") != std::string::npos && all.find("contiguous") != std::string::npos, "validation reports a phase without defender spawn and a non-contiguous order: " + all.substr(0, 160));
  }
  // ---------------------------------------------------------------- v2: objective count 1..10 (SPEC 1.1, 13)
  {
    Scenario none = testScenario(0);
    std::string w0; for (auto& i : validateScenario(none, true)) if (i.error) w0 += i.what;
    T.check(w0.find("No objectives") != std::string::npos, "0 objectives is an error: " + w0.substr(0, 90));
    Match m0; std::string e0; T.check(!m0.setup(none, 0, e0), "a 0-objective scenario cannot start a match (" + e0 + ")");
    T.check(errorsIn(testScenario(1)) == 0, "1 objective is valid (no developer flag needed since v2)");
    T.check(errorsIn(testScenario(10)) == 0 && testScenario(10).objectives.back().letter() == 'J', "10 objectives are valid, labelled A..J");
    std::string w11; for (auto& i : validateScenario(testScenario(11), true)) if (i.error) w11 += i.what;
    T.check(w11.find("at most 10") != std::string::npos && !testScenario(10).canAddObjective(), "11 objectives is an error and the editor refuses an 11th (canAddObjective false at 10)");
    for (int n : {1, 10}) {   // play through every point in order
      Scenario s = testScenario(n); s.rules.captureModel = CaptureModel::Timed; s.rules.attackerAI = 5; s.rules.defenderSlots = 1;
      for (auto& o : s.objectives) o.captureTime = 6;
      Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
      float t = 0; int seen = 0;
      while (m.phase == MatchPhase::Running && t < 200) { occupy(m, 3, 0); m.update(1.0f / 30.0f); t += 1.0f / 30.0f; seen = std::max(seen, m.active); }
      char b[160]; std::snprintf(b, sizeof b, "%d-objective match plays through in order: attackers win after %.1f s (%d points, 2 s each at 3x)", n, t, n);
      T.check(m.winner == Winner::Attackers && std::fabs(t - 2.0f * n) < 0.2f * n + 0.1f && seen == n - 1, b);
    }
  }
  // ---------------------------------------------------------------- v2: timed capture (SPEC 1.1)
  {
    Objective o; o.captureTime = 45;
    ScenarioRules r; r.captureModel = CaptureModel::Timed;
    struct C { int A, D; float want; } cases[] = {{1, 0, 45}, {2, 0, 22.5f}, {3, 0, 15}, {6, 0, 15}};
    for (auto& c : cases) {
      float t = 100.0f / captureRateFor(r, o, c.A, c.D);
      char b[120]; std::snprintf(b, sizeof b, "timed capture %d attacker(s): %.2f s (want %.1f s; speed-up capped at 3x)", c.A, t, c.want);
      T.check(std::fabs(t - c.want) < 0.01f, b);
    }
    T.check(captureRateFor(r, o, 5, 1) == 0 && captureRateFor(r, o, 0, 0) == 0, "timed capture pauses while a defender is on the point and with nobody there");
    Scenario s = testScenario(2); s.rules.captureModel = CaptureModel::Timed; s.rules.attackerAI = 3; s.rules.defenderSlots = 2;
    for (auto& ob : s.objectives) ob.captureTime = 45;
    Match m; std::string err; m.setup(s, 0, err); m.start(); spawnAll(m, vec3(0, 0, 500), vec3(0, 0, -500));
    occupy(m, 1, 0); for (int k = 0; k < 30 * 20; ++k) m.update(1.0f / 30.0f);   // 20 s of 45 -> 44.4 %
    float p = m.obj[0].progress;
    occupy(m, 1, 1); for (int k = 0; k < 30 * 10; ++k) m.update(1.0f / 30.0f);
    char b[160]; std::snprintf(b, sizeof b, "1 attacker for 20 s reaches %.1f %% (want 44.4); a defender arriving holds it at %.1f %% (no decay)", p, m.obj[0].progress);
    T.check(std::fabs(p - 44.44f) < 0.3f && m.obj[0].progress == p, b);
  }
  // ---------------------------------------------------------------- v2: shapes, phases, editing helpers
  {
    Objective o; o.pos = vec3(0); o.shape = CaptureShape::Polygon; o.polygon = {vec2(-10, -5), vec2(10, -5), vec2(10, 5), vec2(-10, 5)};
    T.check(o.contains(vec3(9, 0, 4)) && !o.contains(vec3(0, 0, 8)) && !o.contains(vec3(9, 6, 4)) && std::fabs(o.extent() - std::sqrt(125.0f)) < 1e-3f,
            "polygon capture area: inside / outside / above the vertical bounds; extent covers the outline");
    Objective c; c.pos = vec3(5, 0, 5);
    T.check(c.radius == 25 && c.contains(vec3(5, 0, 29)) && !c.contains(vec3(5, 0, 31)), "new objectives default to a 25 m circle");
    Scenario s = testScenario(3);
    SpawnMarker both; both.id = s.newId("spawn"); both.role = CombatRole::Attacker; both.phases = {1, 2}; both.pos = vec3(0, 0, 300); s.spawns.push_back(both);
    T.check(s.spawnsFor(CombatRole::Attacker, 1).size() == 2 && s.spawnsFor(CombatRole::Attacker, 2).size() == 2 && s.spawnsFor(CombatRole::Attacker, 0).size() == 1,
            "a spawn can be active in several phases");
    std::string idC = s.objectives[2].id;
    s.moveTo(idC, 0);
    const SpawnMarker* bm = nullptr; for (auto& m : s.spawns) if (m.id == both.id) bm = &m;
    T.check(s.objectives[0].id == idC && bm && bm->phases == std::vector<int>({0, 2}), "drag reorder (C to first): the multi-phase spawn follows both of its objectives");
    std::vector<uint64_t> racks; for (auto& m : s.spawns) m.rack = 77;
    s.removeObjective(idC, &racks);
    bm = nullptr; for (auto& m : s.spawns) if (m.id == both.id) bm = &m;
    T.check(s.objectives.size() == 2 && bm && bm->phases == std::vector<int>({1}) && racks.size() == 2 && s.spawnsFor(CombatRole::Defender, 0).size() == 1,
            "deleting an objective drops its phase from shared spawns, deletes single-phase spawns and reports their racks");
  }
  // ---------------------------------------------------------------- v2: placement warnings (SPEC 1.3 / 2.1)
  {
    Scenario s;
    auto obj = [&](float x, float z) { Objective o; o.id = s.newId("obj"); o.order = (int)s.objectives.size(); o.pos = vec3(x, 0, z); s.objectives.push_back(o); };
    auto sp = [&](CombatRole r, int ph, float x, float z) { SpawnMarker m; m.id = s.newId("spawn"); m.role = r; m.phases = {ph}; m.pos = vec3(x, 0, z); m.label = m.id; s.spawns.push_back(m); };
    obj(0, 500); obj(100, 1000); obj(20, 950);          // C: 94 m from B and before it along the axis
    sp(CombatRole::Attacker, 0, 0, 200); sp(CombatRole::Defender, 0, 0, 800);
    sp(CombatRole::Attacker, 1, 0, 900); sp(CombatRole::Defender, 1, 100, 1050);   // atk 141 m from B; def 50 m from B, 180 m from atk
    sp(CombatRole::Attacker, 2, 0, 600); sp(CombatRole::Defender, 2, 0, 1300);
    std::string w; int errs = 0; for (auto& i : validateScenario(s, true)) { if (i.error) ++errs; else w += i.what + " | "; }
    T.check(errs == 0, "placement problems are warnings, not errors");
    T.check(w.find("from the previous one") != std::string::npos, "warns: objective closer than 150 m to the previous one");
    T.check(w.find("Attacker spawn") != std::string::npos && w.find("at least 150 m") != std::string::npos, "warns: attacker spawn closer than 150 m to its objective");
    T.check(w.find("Defender spawn") != std::string::npos && w.find("at least 80 m") != std::string::npos, "warns: defender spawn closer than 80 m to its objective");
    T.check(w.find("at least 200 m") != std::string::npos, "warns: defender spawn closer than 200 m to an active attacker spawn");
    T.check(w.find("not further along the attack axis") != std::string::npos, "warns: objective not behind its predecessor along the attack axis");
  }
  // ---------------------------------------------------------------- v2: the existing 5-point map loads unchanged (SPEC 1.1)
  {
    json f; try { f = json::parse(readTextFile(contentDir() / "tests" / "village_assault_5_v1.nmap.json")); } catch (...) {}
    json v1 = f.is_object() ? f.value("scenario", json()) : json();
    std::vector<std::string> notes;
    Scenario s = Scenario::fromJson(v1, &notes);
    bool same = v1.is_object() && s.objectives.size() == 5 && s.objectives.size() == v1["objectives"].size() && s.spawns.size() == v1["spawns"].size();
    for (size_t k = 0; same && k < s.objectives.size(); ++k) {
      auto& a = s.objectives[k]; auto& b = v1["objectives"][k];
      same = a.id == b["id"].get<std::string>() && a.label == b["label"].get<std::string>() && a.radius == b["radius"].get<float>() && a.pos == jvec3(b["pos"]) && a.shape == CaptureShape::Circle;
    }
    for (size_t k = 0; same && k < s.spawns.size(); ++k) same = s.spawns[k].phases == std::vector<int>({v1["spawns"][k]["stage"].get<int>()}) && s.spawns[k].pos == jvec3(v1["spawns"][k]["pos"]);
    T.check(same, "v1 village_assault_5 fixture: 5 objectives with ids, names, radii and positions kept; spawn stages become single phases");
    T.check(s.rules.defendingFaction == Faction::Allies && s.rules.captureModel == CaptureModel::Contest && errorsIn(s) == 0,
            "v1 map keeps its defending side (Allies) and its capture rule (contest formula), and validates");
    Match m; std::string err;
    T.check(m.setup(s, 1, err) && m.obj.size() == 5, "v1 5-point map starts a match");
    Scenario r = Scenario::fromJson(s.toJson());
    T.check(r.toJson() == s.toJson() && r.version == 2 && notes.size() >= 1, "migrated scenario saves as version 2 and reloads identically");
  }
  // ---------------------------------------------------------------- contract: asset nav metadata + terrain cuts (navmeta.h)
  {
    json a = json::parse(R"({"nav": {"effect": "area", "area": "marsh",
      "areas": [{"outline": [[0,0],[2,0],[2,2]], "area": "trench"}],
      "rooms": [{"id": "r1", "min": [-2,0,-2], "max": [2,2.4,2]}], "doors": [{"a": "r1", "b": "outside", "p": [0,0,2]}],
      "entrances": [{"p": [0,0,2.2], "dir": [0,0,-1], "room": "r1"}], "corners": [{"p": [-1.8,0,-1.8], "room": "r1"}],
      "firingPoints": [{"p": [0,1.5,-2], "yaw": 180, "kind": "embrasure"}],
      "cover": [{"p": [3,0,0], "yaw": 90, "height": "low", "peek": ["left", "over"]}]},
      "terrainCut": [{"outline": [[-1,-1],[1,-1],[1,1],[-1,1]], "depth": 1.8, "bank": 0.4}]})");
    NavMeta m; parseNavMeta(a, m);
    T.check(m.authored && m.effect == NavEffect::Area && m.area == NavArea::Marsh && m.areas.size() == 1 && m.areas[0].area == NavArea::Trench &&
            m.rooms.size() == 1 && m.doors.size() == 1 && m.entrances.size() == 1 && m.corners.size() == 1 && m.firingPoints.size() == 1 &&
            m.cover.size() == 1 && m.cover[0].peekLeft && m.cover[0].peekOver && !m.cover[0].peekRight && m.terrainCuts.size() == 1 && m.terrainCuts[0].depth == 1.8f,
            "asset nav metadata parses: effect, area, sub-areas, room graph, entrances, corners, firing points, cover, terrain cut");
    NavMeta r; parseNavMeta(navMetaToJson(m), r);
    T.check(navMetaToJson(r) == navMetaToJson(m), "nav metadata round trip is identical");
    NavMeta none; parseNavMeta(json::object(), none);
    T.check(!none.authored && resolveNavEffect(none, true, vec3(0), vec3(1)) == NavEffect::Block && resolveNavEffect(none, false, vec3(0), vec3(0.8f)) == NavEffect::Carve &&
            resolveNavEffect(none, false, vec3(0), vec3(1, 2.5f, 1)) == NavEffect::Block, "no nav block: enterable / tall assets block, low ones carve");
    T.check(navAreaCost(NavArea::Road) == 1.0f && navAreaCost(NavArea::Trench) == 0.9f && navAreaCost(NavArea::Marsh) == 3.0f && navAreaCost(NavArea::Minefield) < 0 && navAreaCost(NavArea::Wire) < 0,
            "AI_SPEC 3.1 area costs: road 1.0, trench 0.9, marsh 3.0, minefield and wire forbidden");
  }
  // ---------------------------------------------------------------- cross-play: the join's content hash ignores line endings
  {
    fs::path root = fs::temp_directory_path() / "nmk_selftest_hash";
    auto write = [&](const char* sub, const char* nl) {
      std::error_code ec; fs::create_directories(root / sub / "assets", ec);
      std::ofstream(root / sub / "assets" / "a.json", std::ios::binary) << "{" << nl << "  \"id\": \"a\"" << nl << "}" << nl;
      std::ofstream(root / sub / "assets" / "b.bin", std::ios::binary) << "1234";
    };
    write("crlf", "\r\n"); write("lf", "\n");
    std::string hc = Net::contentHashAt(root / "crlf"), hl = Net::contentHashAt(root / "lf");
    std::ofstream(root / "lf" / "assets" / "a.json", std::ios::binary) << "{\n  \"id\": \"b\"\n}\n";
    std::string hd = Net::contentHashAt(root / "lf");
    T.check(hc == hl && hd != hl, "content hash: CRLF and LF copies match (Windows <-> macOS joins), a changed value differs");
    std::error_code ec; fs::remove_all(root, ec);
  }
  return T.finish();
}
