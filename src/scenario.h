#pragma once
// Authored match scenario: ordered capture objectives, spawn markers, approach corridors and rule defaults.
// Stored in the map file under doc.extra["scenario"] with its own version (migration defaults for old maps);
// runtime match state (match.h) is separate and never written back into the map.
#include "common.h"

enum class Faction : int { Allies = 0, Axis = 1 };
enum class CombatRole : int { Defender = 0, Attacker = 1 };
inline const char* factionName(Faction f) { return f == Faction::Allies ? "Allies" : "Axis"; }
inline const char* roleName(CombatRole r) { return r == CombatRole::Attacker ? "Attackers" : "Defenders"; }
inline Faction otherFaction(Faction f) { return f == Faction::Allies ? Faction::Axis : Faction::Allies; }

constexpr int kScenarioVersion = 1;

struct SpawnMarker {
  std::string id, label;
  CombatRole role = CombatRole::Attacker;
  int stage = 0;                 // objective sequence index this marker serves (spawns while that objective is active)
  vec3 pos{0}; float yaw = 0;
};

// Approach corridor waypoints per objective. Left / right are relative to the objective's attack-direction arrow
// (the direction attackers move toward the objective), never to world axes or the camera.
enum ApproachLane : int { LANE_LEFT = 0, LANE_MAIN = 1, LANE_RIGHT = 2 };
struct ApproachRoute { int lane = LANE_MAIN; std::vector<vec3> points; };

struct Objective {
  std::string id;                // stable: never changes when reordered
  std::string label;
  int order = 0;                 // sequence index (0-based, unique, contiguous)
  vec3 pos{0}; float yaw = 0;
  float radius = 15.0f;          // m (capture cylinder)
  float yMin = -2.0f, yMax = 4.0f;   // vertical bounds relative to pos.y (feet height must lie inside)
  float attackYaw = 0;           // attack-direction arrow (deg about +Y; 0 = toward +z... see forwardOf)
  std::vector<ApproachRoute> routes;
  std::vector<vec3> defendPoints;   // optional authored defender positions
  bool contains(vec3 feet) const {
    vec2 d(feet.x - pos.x, feet.z - pos.z);
    float dy = feet.y - pos.y;
    return glm::dot(d, d) <= radius * radius && dy >= yMin && dy <= yMax;
  }
};

struct ScenarioRules {
  int defenderSlots = 5;          // humans + friendly AI
  int attackerAI = 10;
  int attackerRespawns = 5;       // per roster slot (1 initial life + this many respawns)
  float attackerWave = 10.0f, defenderWave = 8.0f, minRespawnDelay = 3.0f;   // s
  Faction defendingFaction = Faction::Allies;
  int fragPerLife = 2, smokePerLifeAttacker = 1, smokePerLifeDefender = 0;
  float captureBase = 5.0f, capturePresenceFull = 5.0f;   // %/s at full advantage; attackers for full presence
  float laneLeft = 0.2f, laneMain = 0.6f, laneRight = 0.2f;
  bool friendlyFire = false;      // explosive / bullet damage to own side (default off; exposed in setup)
  static constexpr int kMaxCombatants = 20;
  json toJson() const;
  static ScenarioRules fromJson(const json& j);
};

struct Scenario {
  int version = kScenarioVersion;
  std::vector<Objective> objectives;   // kept sorted by order
  std::vector<SpawnMarker> spawns;
  ScenarioRules rules;
  bool devTestMap = false;             // allows a single objective (developer test map)
  uint64_t nextSerial = 1;

  json toJson() const;
  static Scenario fromJson(const json& j, std::vector<std::string>* migrationNotes = nullptr);
  void sortByOrder();
  void renumber();                     // contiguous 0..n-1 in current sequence
  bool moveEarlier(const std::string& id);
  bool moveLater(const std::string& id);
  Objective* find(const std::string& id);
  const Objective* find(const std::string& id) const;
  std::string newId(const char* prefix);
  std::vector<const SpawnMarker*> spawnsFor(CombatRole r, int stage) const;
};

struct ScenarioIssue { bool error = true; std::string what; };
// Static validation (data only). Geometry checks (clearance, reachability) are done by the app with physics + nav
// (validateScenarioWorld in match.cpp) and append to the same list.
std::vector<ScenarioIssue> validateScenario(const Scenario& s, bool forPublish);

// largest-remainder allocation of n bots over weights, keeping at least `minMain` in index 1 (main assault)
std::vector<int> allocateLanes(int n, float left, float main, float right, int minMain = 1);

vec3 attackForward(float attackYawDeg);   // unit vector of the attack arrow
vec3 attackRight(float attackYawDeg);     // right of the arrow (attacker's right when moving along it)
