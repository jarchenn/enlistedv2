#pragma once
// Server-side match controller: roster slots with persistent life budgets, spawn waves, ordered capture and the
// single, central end-of-match decision per simulation step. Pure logic: the game layer (soldier.cpp) reports
// which slots are alive and where their feet are, performs the actual spawns and reports success.
#include "scenario.h"

enum class ObjState : int { Locked = 0, Active = 1, Captured = 2 };
enum class MatchPhase : int { Setup = 0, Running = 1, Ended = 2 };
enum class Winner : int { None = -1, Defenders = 0, Attackers = 1 };

struct RosterSlot {
  int id = 0;
  Faction faction = Faction::Allies;
  CombatRole role = CombatRole::Defender;
  bool human = false;            // occupied by a human player (local or network peer)
  int peer = -1;                 // network peer id (0 = the host's own player), -1 = AI
  int pendingPeer = -1;          // a human waiting to take this slot at its next spawn (in-match join)
  std::string name;
  int livesLeft = -1;            // spawns this slot may still perform (attackers: 1 + respawns); -1 = unlimited
  bool alive = false;            // a living soldier occupies the slot
  vec3 feet{0};                  // reported by the game layer each step (alive only)
  bool sampleAlive = false;      // occupancy sample taken at the start of the step (capture uses this, so a soldier
  vec3 sampleFeet{0};            // who dies during the step still counted for that step's capture)
  float deadFor = 0;             // s since death (or since match start before the first spawn)
  bool pending = false;          // committed to the next spawn attempt (a wave released it)
  bool everSpawned = false;
  float spawnFailFor = 0;        // s of consecutive failed placement attempts
  int deaths = 0, kills = 0, spawns = 0;
  int lane = LANE_MAIN;          // attacker approach lane (stable until objective change / rebalance)
};

struct ObjectiveRT {
  std::string id, label;
  ObjState state = ObjState::Locked;
  float progress = 0;            // 0..100, cumulative, never decays
  int A = 0, D = 0;              // attackers / defenders inside the volume this step
  float rate = 0;                // %/s this step
};

struct MatchEvent { double t = 0; std::string text; };

// capture rate in percentage points per second (spec formula); guards zero denominators
float captureRate(int A, int D, float base = 5.0f, float presenceFull = 5.0f);

class Match {
public:
  Scenario sc;
  ScenarioRules rules;
  MatchPhase phase = MatchPhase::Setup;
  Winner winner = Winner::None;
  std::string result;            // human-readable reason
  std::vector<RosterSlot> slots;
  std::vector<ObjectiveRT> obj;
  int active = 0;                // index of the active objective (in sequence order)
  double time = 0;               // match seconds
  uint64_t step = 0;             // simulation steps resolved
  float attackerWaveIn = 0, defenderWaveIn = 0;   // s until the next wave release
  std::vector<MatchEvent> events;
  std::string spawnProblem;      // actionable message when placement keeps failing
  uint32_t rev = 1;              // bumps on any replicated change (net dirty flag)

  // Build the roster: `humans` human defenders (peers 0..humans-1) fill defender slots first, friendly AI the rest.
  bool setup(const Scenario& s, int humans, std::string& err);
  void start();
  // one simulation step: capture -> objective advance -> end-of-match decision -> waves. Idempotent per step id.
  void update(float dt);
  // game layer callbacks
  void reportAlive(int slot, bool alive, vec3 feet);
  void onSpawned(int slot);                     // successful placement: spends one life (if budgeted)
  void onSpawnFailed(int slot, float dt, const std::string& why);
  void onDeath(int slot, int killerSlot);
  std::vector<int> spawnQueue() const;          // slots committed to spawn now
  // humans (lobby / in-match): join takes an AI defender slot (in-match: at that slot's next spawn), leave hands the
  // living soldier to the AI in place (no orphan, no duplicate, inventory kept)
  int humanJoin(int peer, const std::string& name);
  void humanLeave(int peer);
  int slotOfPeer(int peer) const;
  // aggregates for the HUD
  int livingAttackers() const, livingDefenders() const, unusedAttackerLives() const, pendingAttackers() const;
  int occupancyFriendly(CombatRole viewer) const;
  float nextWaveIn(CombatRole r) const { return r == CombatRole::Attacker ? attackerWaveIn : defenderWaveIn; }
  const Objective* activeObjective() const { return active >= 0 && active < (int)sc.objectives.size() ? &sc.objectives[active] : nullptr; }
  bool attackersExhausted() const;
  void log(const std::string& s);
  json stateJson() const;                       // replicated summary (tests / net)
private:
  void releaseWave(CombatRole r);
};
