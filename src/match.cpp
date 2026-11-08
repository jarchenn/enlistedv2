#include "match.h"
#include <algorithm>
#include <cmath>

float captureRate(int A, int D, float base, float presenceFull) {
  if (A <= D || A <= 0) return 0.0f;                     // tie or defender superiority freezes; no decay
  float advantage = float(A - D) / float(A + D);         // A + D > 0 here
  float presence = std::min(float(A) / std::max(presenceFull, 1e-3f), 1.0f);
  return base * advantage * presence;
}

void Match::log(const std::string& s) {
  events.push_back({time, s});
  if (events.size() > 200) events.erase(events.begin());
  logf("[match %.1f] %s", time, s.c_str());
  ++rev;
}

bool Match::setup(const Scenario& s, int humans, std::string& err) {
  sc = s; sc.renumber(); rules = sc.rules;
  phase = MatchPhase::Setup; winner = Winner::None; result.clear();
  slots.clear(); obj.clear(); events.clear(); spawnProblem.clear();
  time = 0; step = 0; active = 0; winnerSlot = -1;
  if (rules.mode != GameMode::Assault) return setupDeathmatch(humans, err);   // G1: Solo / TDM
  if (sc.objectives.empty()) { err = "The map has no objectives (Creative > Scenario)."; return false; }
  int defSlots = std::max(rules.defenderSlots, humans);
  if (defSlots + rules.attackerAI > ScenarioRules::kMaxCombatants) { err = "Roster exceeds " + std::to_string(ScenarioRules::kMaxCombatants) + " combatants."; return false; }
  if (rules.attackerAI < 1) { err = "At least one attacking AI is required."; return false; }
  Faction df = rules.defendingFaction, af = otherFaction(df);
  for (int k = 0; k < defSlots; ++k) {
    RosterSlot r; r.id = (int)slots.size(); r.faction = df; r.role = CombatRole::Defender; r.team = 0; r.livesLeft = -1;
    if (k < humans) { r.human = true; r.peer = k; r.name = k == 0 ? "Host" : "Player " + std::to_string(k + 1); }
    else r.name = std::string(df == Faction::Allies ? "Pvt. " : "Gren. ") + std::to_string(k + 1);
    slots.push_back(r);
  }
  std::vector<int> lanes = allocateLanes(rules.attackerAI, rules.laneLeft, rules.laneMain, rules.laneRight);
  int laneOf[3] = {lanes[0], lanes[1], lanes[2]};
  for (int k = 0; k < rules.attackerAI; ++k) {
    RosterSlot r; r.id = (int)slots.size(); r.faction = af; r.role = CombatRole::Attacker; r.team = 1;
    r.livesLeft = 1 + rules.attackerRespawns;
    r.name = std::string(af == Faction::Allies ? "Pvt. " : "Gren. ") + std::to_string(100 + k + 1);
    // deterministic lane assignment: left block, then main, then right (roster order)
    r.lane = k < laneOf[0] ? LANE_LEFT : k < laneOf[0] + laneOf[1] ? LANE_MAIN : LANE_RIGHT;
    slots.push_back(r);
  }
  for (auto& o : sc.objectives) { ObjectiveRT x; x.id = o.id; x.label = std::string(1, o.letter()) + " " + o.displayName(); obj.push_back(x); }
  ++rev;
  return true;
}

// Solo: `soloPlayers` slots, each its own team (humans first). TDM: teamPlayers[t] per team, team 0 = the Defender role,
// team 1 = Attacker; humans join the host's team first, then fill the team with fewer humans. Unlimited respawns.
bool Match::setupDeathmatch(int humans, std::string& err) {
  const bool solo = rules.mode == GameMode::Solo;
  int counts[2] = {solo ? std::max(rules.soloPlayers, humans) : rules.teamPlayers[0], solo ? 0 : rules.teamPlayers[1]};
  if (counts[0] + counts[1] > ScenarioRules::kMaxCombatants) { err = "Roster exceeds " + std::to_string(ScenarioRules::kMaxCombatants) + " combatants."; return false; }
  if (counts[0] + counts[1] < 2) { err = "A match needs at least two combatants."; return false; }
  int humansOn[2] = {0, 0};
  if (solo) humansOn[0] = humans;
  else for (int h = 0; h < humans; ++h) {   // host on humanTeam, then balance
    int t = h == 0 ? rules.humanTeam : (humansOn[0] <= humansOn[1] ? 0 : 1);
    if (humansOn[t] >= counts[t]) t = 1 - t;
    if (humansOn[t] >= counts[t]) { err = "More players than slots: raise the team sizes."; return false; }
    ++humansOn[t];
  }
  int peer = 0;
  for (int t = 0; t < (solo ? 1 : 2); ++t)
    for (int k = 0; k < counts[t]; ++k) {
      RosterSlot r; r.id = (int)slots.size(); r.livesLeft = -1; r.lane = LANE_MAIN;
      r.role = t == 0 ? CombatRole::Defender : CombatRole::Attacker;
      r.team = solo ? r.id : t;
      r.faction = solo ? (k % 2 == 0 ? rules.teamFaction[0] : rules.teamFaction[1]) : rules.teamFaction[t];
      if (k < humansOn[t]) { r.human = true; r.peer = peer; r.name = peer == 0 ? "Host" : "Player " + std::to_string(peer + 1); ++peer; }
      else r.name = std::string(r.faction == Faction::Allies ? "Pvt. " : "Gren. ") + std::to_string(r.id + 1);
      slots.push_back(r);
    }
  ++rev;
  return true;
}

void Match::start() {
  phase = MatchPhase::Running;
  if (rules.mode != GameMode::Assault) {
    for (auto& s : slots) { s.pending = true; s.deadFor = 999; }
    log(std::string("match start: ") + gameModeName(rules.mode) + ", first to " + std::to_string(rules.killCap) + " kills, " + std::to_string(slots.size()) + " combatants");
    return;
  }
  if (!obj.empty()) obj[0].state = ObjState::Active;
  for (auto& s : slots) { s.pending = true; s.deadFor = 999; }   // initial spawn of everyone (spends the first life)
  attackerWaveIn = rules.attackerWave; defenderWaveIn = rules.defenderWave;
  char b[200]; std::snprintf(b, sizeof b, "match start: %s defend (%d slots), %s attack (%d AI, %d lives each = 1 + %d respawns)",
                             factionName(rules.defendingFaction), (int)std::count_if(slots.begin(), slots.end(), [](const RosterSlot& s) { return s.role == CombatRole::Defender; }),
                             factionName(otherFaction(rules.defendingFaction)), rules.attackerAI, 1 + rules.attackerRespawns, rules.attackerRespawns);
  log(b);
}

void Match::reportAlive(int slot, bool a, vec3 feet) {
  if (slot < 0 || slot >= (int)slots.size()) return;
  slots[slot].alive = a; slots[slot].sampleAlive = a;
  if (a) slots[slot].feet = slots[slot].sampleFeet = feet;
}

void Match::onSpawned(int slot) {
  if (slot < 0 || slot >= (int)slots.size()) return;
  auto& s = slots[slot];
  if (!s.pending) return;                                 // duplicate report: ignore (never double-spend)
  s.pending = false; s.alive = true; s.everSpawned = true; s.deadFor = 0; s.spawnFailFor = 0; ++s.spawns;
  if (s.livesLeft > 0) --s.livesLeft;
  if (s.pendingPeer >= 0) { s.human = true; s.peer = s.pendingPeer; s.pendingPeer = -1; log(s.name + " now played by a human (took the slot at respawn)"); }
  if (spawnProblem.size()) { bool any = false; for (auto& x : slots) if (x.spawnFailFor > 3) any = true; if (!any) spawnProblem.clear(); }
  ++rev;
}

void Match::onSpawnFailed(int slot, float dt, const std::string& why) {
  if (slot < 0 || slot >= (int)slots.size()) return;
  auto& s = slots[slot];
  s.spawnFailFor += dt;                                   // no life is spent; retried next step
  if (s.spawnFailFor > 3.0f && spawnProblem.empty()) {
    spawnProblem = std::string(roleName(s.role)) + " cannot spawn for stage " + std::to_string(active + 1) + ": " + why +
                   ". Add or move spawn markers for this stage in Creative (Scenario panel) - they need 0.7 m of clear, walkable ground.";
    log(spawnProblem);
  }
}

void Match::onDeath(int slot, int killer) {
  if (slot < 0 || slot >= (int)slots.size()) return;
  auto& s = slots[slot];
  if (!s.alive) return;
  s.alive = false; s.deadFor = 0; ++s.deaths;   // the step's occupancy sample is kept (capture precedence)
  if (killer >= 0 && killer < (int)slots.size() && killer != slot) {
    auto& k = slots[killer];
    if (k.team == s.team && rules.mode != GameMode::Assault) k.score -= 100;   // a team kill counts against the killer
    else { ++k.kills; k.score += 100; }
  }
  ++rev;
}

int Match::teamKills(int team) const { int n = 0; for (auto& s : slots) if (s.team == team) n += s.kills; return n; }
int Match::topKills(int* slot) const {
  int best = 0, bs = -1;
  for (auto& s : slots) if (s.kills > best) { best = s.kills; bs = s.id; }
  if (slot) *slot = bs;
  return best;
}

std::vector<int> Match::spawnQueue() const {
  std::vector<int> q;
  if (phase != MatchPhase::Running) return q;
  for (auto& s : slots) if (s.pending && !s.alive) q.push_back(s.id);
  return q;
}

void Match::releaseWave(CombatRole r) {
  for (auto& s : slots) {
    if (s.role != r || s.alive || s.pending) continue;
    if (s.deadFor < rules.minRespawnDelay) continue;      // minimum death-to-spawn delay
    if (s.livesLeft == 0) continue;                       // exhausted budget
    s.pending = true;                                     // committed: counts as a pending respawn from now on
  }
}

bool Match::attackersExhausted() const {
  for (auto& s : slots) {
    if (s.role != CombatRole::Attacker) continue;
    if (s.alive || s.pending) return false;               // living, or a committed pending respawn
    if (s.livesLeft != 0) return false;                   // an available respawn remains
  }
  return true;
}

void Match::update(float dt) {
  if (phase != MatchPhase::Running) return;               // match end freezes scoring and spawning
  if (!(dt > 0) || !std::isfinite(dt)) return;            // time-step guard
  struct ClearSample { std::vector<RosterSlot>& v; ~ClearSample() { for (auto& s : v) if (!s.alive) s.sampleAlive = false; } } clr{slots};
  dt = std::min(dt, 0.25f);
  ++step; time += dt;
  for (auto& s : slots) if (!s.alive) s.deadFor += dt;
  if (rules.mode != GameMode::Assault) {   // G1: kill cap, then plain respawn delays (no waves, no lives)
    if (rules.mode == GameMode::Solo) {
      int ws = -1; int top = topKills(&ws);
      if (top >= rules.killCap) {
        phase = MatchPhase::Ended; winnerSlot = ws; winner = Winner::None;
        result = slots[ws].name + " reached " + std::to_string(rules.killCap) + " kills"; log("result: " + result); return;
      }
    } else for (int t = 0; t < 2; ++t) if (teamKills(t) >= rules.killCap) {
      phase = MatchPhase::Ended; winner = t == 0 ? Winner::Defenders : Winner::Attackers;
      result = std::string(factionName(rules.teamFaction[t])) + " (team " + (t == 0 ? "A" : "B") + ") reached " + std::to_string(rules.killCap) + " kills";
      log("result: " + result); return;
    }
    for (auto& s : slots) if (!s.alive && !s.pending && s.deadFor >= rules.respawnDelay) { s.pending = true; ++rev; }
    return;
  }
  // ---- capture (active objective only; later points stay locked even if occupied)
  bool finalCaptured = false;
  if (active < (int)obj.size()) {
    auto& o = obj[active]; const Objective& od = sc.objectives[active];
    o.A = o.D = 0;
    for (auto& s : slots) if (s.sampleAlive && od.contains(s.sampleFeet)) (s.role == CombatRole::Attacker ? o.A : o.D)++;
    o.rate = captureRateFor(rules, od, o.A, o.D);
    float before = o.progress;
    o.progress = std::clamp(o.progress + o.rate * dt, 0.0f, 100.0f);
    if (o.progress != before) ++rev;
    if (o.progress >= 100.0f) {
      o.state = ObjState::Captured; o.progress = 100.0f;
      log("objective " + std::to_string(active + 1) + " '" + o.label + "' captured");
      if (active + 1 >= (int)obj.size()) finalCaptured = true;
      else {
        ++active; obj[active].state = ObjState::Active;   // living pawns and budgets are untouched
        log("objective " + std::to_string(active + 1) + " '" + obj[active].label + "' is now active");
      }
    }
  }
  // ---- end-of-match decision, once per step. Precedence: final capture reaching 100 % in this step wins for the
  // attackers; only otherwise is attacker exhaustion evaluated.
  if (finalCaptured) {
    phase = MatchPhase::Ended; winner = Winner::Attackers;
    result = "Attackers captured the final objective";
    log("result: attackers win (final capture this step; precedence over exhaustion)"); return;
  }
  if (attackersExhausted()) {
    phase = MatchPhase::Ended; winner = Winner::Defenders;
    result = "Defenders held: no attacker alive and no respawn left";
    log("result: defenders win (attackers exhausted)"); return;
  }
  // ---- waves (server controlled)
  attackerWaveIn -= dt; defenderWaveIn -= dt;
  if (attackerWaveIn <= 0) { releaseWave(CombatRole::Attacker); attackerWaveIn += rules.attackerWave; if (attackerWaveIn <= 0) attackerWaveIn = rules.attackerWave; }
  if (defenderWaveIn <= 0) { releaseWave(CombatRole::Defender); defenderWaveIn += rules.defenderWave; if (defenderWaveIn <= 0) defenderWaveIn = rules.defenderWave; }
}

int Match::humanJoin(int peer, const std::string& name) {
  // prefer an AI defender slot whose soldier is dead (immediate, normal respawn flow), then any AI defender slot
  int best = -1;
  int wantTeam = -1;   // TDM: the team with fewer humans
  if (rules.mode == GameMode::TDM) { int h[2] = {0, 0}; for (auto& s : slots) if (s.human || s.pendingPeer >= 0) ++h[s.team]; wantTeam = h[0] <= h[1] ? 0 : 1; }
  auto eligible = [&](const RosterSlot& s) {
    if (s.human || s.pendingPeer >= 0) return false;
    if (rules.mode == GameMode::Assault) return s.role == CombatRole::Defender;
    return wantTeam < 0 || s.team == wantTeam;
  };
  for (auto& s : slots) if (eligible(s)) { if (best < 0 || (!s.alive && slots[best].alive)) best = s.id; }
  if (best < 0 && wantTeam >= 0) { wantTeam = -1; for (auto& s : slots) if (eligible(s)) { if (best < 0 || (!s.alive && slots[best].alive)) best = s.id; } }   // that team is all human: the other one
  if (best < 0) return -1;                                // lobby full
  auto& s = slots[best];
  if (phase == MatchPhase::Setup) { s.human = true; s.peer = peer; s.name = name; }
  else { s.pendingPeer = peer; s.name = name; log(name + " joined: takes defender slot " + std::to_string(best) + " at its next spawn"); }
  ++rev;
  return best;
}

void Match::humanLeave(int peer) {
  for (auto& s : slots) {
    if (s.pendingPeer == peer) { s.pendingPeer = -1; ++rev; }
    if (s.human && s.peer == peer) {
      s.human = false; s.peer = -1;                       // a living soldier keeps fighting under AI control
      s.name = std::string(s.faction == Faction::Allies ? "Pvt. " : "Gren. ") + std::to_string(s.id + 1);
      log("a player left: slot " + std::to_string(s.id) + " restored to AI" + (s.alive ? " (soldier kept in place)" : ""));
    }
  }
}

int Match::slotOfPeer(int peer) const { for (auto& s : slots) if (s.human && s.peer == peer) return s.id; for (auto& s : slots) if (s.pendingPeer == peer) return s.id; return -1; }
int Match::livingAttackers() const { int n = 0; for (auto& s : slots) n += s.role == CombatRole::Attacker && s.alive; return n; }
int Match::livingDefenders() const { int n = 0; for (auto& s : slots) n += s.role == CombatRole::Defender && s.alive; return n; }
int Match::unusedAttackerLives() const { int n = 0; for (auto& s : slots) if (s.role == CombatRole::Attacker && s.livesLeft > 0) n += s.livesLeft; return n; }
int Match::pendingAttackers() const { int n = 0; for (auto& s : slots) n += s.role == CombatRole::Attacker && s.pending; return n; }
int Match::occupancyFriendly(CombatRole r) const { if (active >= (int)obj.size()) return 0; return r == CombatRole::Attacker ? obj[active].A : obj[active].D; }

json Match::stateJson() const {
  json o = json::array();
  for (auto& x : obj) o.push_back({{"id", x.id}, {"state", (int)x.state}, {"progress", x.progress}, {"A", x.A}, {"D", x.D}});
  return {{"phase", (int)phase}, {"winner", (int)winner}, {"active", active}, {"time", time}, {"objectives", o},
          {"livingAttackers", livingAttackers()}, {"livingDefenders", livingDefenders()}, {"unusedAttackerLives", unusedAttackerLives()}};
}
