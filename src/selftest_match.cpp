// --selftest-match: headless unit suite for the scenario data and the server match controller (no window, no GL).
// Capture formula and times, ordered objectives, life budgets, spawn-failure accounting, wave timing, the end-of-match
// precedence rule, freeze on end, lanes (largest remainder), roster limits, join / leave, scenario save / migrate /
// reorder / validate.
#include "match.h"
#include "mptest.h"
#include <cmath>
#include <fstream>

namespace {
Scenario testScenario(int nObj) {
  Scenario s;
  for (int k = 0; k < nObj; ++k) {
    Objective o; o.id = s.newId("obj"); o.label = std::string("Point ") + char('A' + k); o.order = k; o.pos = vec3(0, 0, -60.0f * k); o.radius = 15;
    s.objectives.push_back(o);
    SpawnMarker a; a.id = s.newId("spawn"); a.role = CombatRole::Attacker; a.stage = k; a.pos = o.pos + vec3(0, 0, 80); s.spawns.push_back(a);
    SpawnMarker d; d.id = s.newId("spawn"); d.role = CombatRole::Defender; d.stage = k; d.pos = o.pos + vec3(0, 0, -20); s.spawns.push_back(d);
  }
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
    s.rules.defenderSlots = 5; s.rules.attackerAI = 16; s.rules = ScenarioRules::fromJson(s.rules.toJson());
    T.check(s.rules.defenderSlots + s.rules.attackerAI <= 20, "rules clamp the roster to 20 combatants (5 + " + std::to_string(s.rules.attackerAI) + ")");
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
    int sp1 = 0; for (auto& m : s.spawns) if (m.stage == 1) ++sp1;
    s.moveLater(id0);
    T.check(s.objectives[0].id == id1 && s.objectives[1].id == id0 && s.objectives[1].order == 1, "Move Later swaps the sequence and keeps stable ids");
    int sp0now = 0; for (auto& m : s.spawns) if (m.stage == 0) ++sp0now;
    T.check(sp0now == sp1, "spawn groups follow their objective when reordered");
    json j = s.toJson();
    Scenario r = Scenario::fromJson(json::parse(j.dump()));
    T.check(r.toJson() == j, "scenario save -> load round trip is identical");
    std::vector<std::string> notes;
    json old = {{"objectives", json::array({{{"pos", {1, 2, 3}}}})}};
    Scenario mig = Scenario::fromJson(old, &notes);
    T.check(mig.objectives.size() == 1 && mig.objectives[0].radius == 15 && mig.objectives[0].yMin == -2 && !mig.objectives[0].id.empty() && !notes.empty(), "version-0 scenario migrates with defaults (radius 15 m, bounds, id): " + (notes.empty() ? std::string() : notes[0]));
    auto iss = validateScenario(testScenario(4), true);
    int errs = 0; for (auto& i : iss) errs += i.error;
    T.check(errs == 0, "a 4-point scenario with spawns per stage validates for publish");
    Scenario bad = testScenario(2); bad.spawns.erase(bad.spawns.begin() + 1); bad.objectives[1].order = 5;
    auto iss2 = validateScenario(bad, true);
    std::string all; for (auto& i : iss2) if (i.error) all += i.what + " | ";
    T.check(all.find("published maps need 3 to 5") != std::string::npos && all.find("defender spawn") != std::string::npos && all.find("contiguous") != std::string::npos, "validation reports count, missing spawn group and non-contiguous order: " + all.substr(0, 160));
    Scenario one = testScenario(1); one.devTestMap = true;
    int e1 = 0; for (auto& i : validateScenario(one, false)) e1 += i.error;
    T.check(e1 == 0, "one-point developer test map is allowed");
  }
  return T.finish();
}
