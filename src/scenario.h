#pragma once
// Authored match scenario: ordered capture objectives (1..10, labelled A..J), spawn markers (each active for one or more
// phases), approach lane hints, defense slots and rule defaults. Stored in the map file under doc.extra["scenario"] with
// its own version (migration defaults for old maps); runtime match state (match.h) is separate and never written back.
// Version 2 (MP & Creative update): letter labels + display name, circle or polygon capture shape, timed capture,
// linked structures, defense slots, multi-phase spawns with an auto-placed weapons rack, Allies attack by default.
#include "common.h"
#include <algorithm>

enum class Faction : int { Allies = 0, Axis = 1 };
enum class CombatRole : int { Defender = 0, Attacker = 1 };
inline const char* factionName(Faction f) { return f == Faction::Allies ? "Allies" : "Axis"; }
inline const char* roleName(CombatRole r) { return r == CombatRole::Attacker ? "Attackers" : "Defenders"; }
inline Faction otherFaction(Faction f) { return f == Faction::Allies ? Faction::Axis : Faction::Allies; }

constexpr int kScenarioVersion = 2;
constexpr int kMinObjectives = 1, kMaxObjectives = 10;
inline char objectiveLetter(int order) { return char('A' + std::clamp(order, 0, 25)); }

struct SpawnMarker {
  std::string id, label;
  CombatRole role = CombatRole::Attacker;
  std::vector<int> phases{0};    // objective sequence indices this marker serves (spawns while one of them is active)
  vec3 pos{0}; float yaw = 0;
  uint64_t rack = 0;             // instance id of the team weapons rack placed next to it (0 = none / deleted)
  bool activeIn(int phase) const { return std::find(phases.begin(), phases.end(), phase) != phases.end(); }
  int firstPhase() const { return phases.empty() ? -1 : *std::min_element(phases.begin(), phases.end()); }
};

// Approach lane hints per objective. Left / right are relative to the objective's attack-direction arrow
// (the direction attackers move toward the objective), never to world axes or the camera.
enum ApproachLane : int { LANE_LEFT = 0, LANE_MAIN = 1, LANE_RIGHT = 2 };
struct ApproachRoute { int lane = LANE_MAIN; std::vector<vec3> points; };

// An authored defender position belonging to an objective (SPEC 5.5). Bots fill slots by priority (higher first).
enum class SlotKind : int { Position = 0, MgNest, TrenchBay, Window, Foxhole, Embrasure, Count };
const char* slotKindName(SlotKind k);
SlotKind slotKindFrom(const std::string& s);
struct DefenseSlot { vec3 pos{0}; float yaw = 0; SlotKind kind = SlotKind::Position; int priority = 1; };

enum class CaptureShape : int { Circle = 0, Polygon = 1 };

struct Objective {
  std::string id;                // stable: never changes when reordered
  std::string label;             // display name ("WN65 - Ruquet casemate"); the letter A..J comes from `order`
  int order = 0;                 // sequence index (0-based, unique, contiguous)
  vec3 pos{0}; float yaw = 0;
  CaptureShape shape = CaptureShape::Circle;
  float radius = 25.0f;          // m (circle shape; v1 maps keep their stored radius)
  std::vector<vec2> polygon;     // world x/z outline (polygon shape, >= 3 points)
  float yMin = -2.0f, yMax = 4.0f;   // vertical bounds relative to pos.y (feet height must lie inside)
  float captureTime = 45.0f;     // s with one attacker and no defender (timed capture model)
  float attackYaw = 0;           // attack-direction arrow (deg about +Y; see attackForward)
  std::vector<ApproachRoute> routes;          // optional lane hints (AI_SPEC 3.4)
  std::vector<uint64_t> linkedStructures;     // instance ids of the bunkers / emplacements that make up the point
  std::vector<DefenseSlot> defenseSlots;
  char letter() const { return objectiveLetter(order); }
  std::string displayName() const { return label.empty() ? id : label; }
  bool containsXZ(vec2 p) const;
  bool contains(vec3 feet) const { float dy = feet.y - pos.y; return dy >= yMin && dy <= yMax && containsXZ(vec2(feet.x, feet.z)); }
  float extent() const;          // radius of a circle around pos that holds the whole shape
};

// Capture model: Timed (v2 default) = 100 % in captureTime with one attacker, rate x min(attackers, cap), paused while
// any defender is inside, never decays. Contest (v1 maps) = the AI/LAN milestone formula 5 * (A-D)/(A+D) * min(A/5, 1).
enum class CaptureModel : int { Timed = 0, Contest = 1 };

struct ScenarioRules {
  int defenderSlots = 5;          // humans + friendly AI
  int attackerAI = 10;
  int attackerRespawns = 5;       // per roster slot (1 initial life + this many respawns)
  float attackerWave = 10.0f, defenderWave = 8.0f, minRespawnDelay = 3.0f;   // s
  Faction defendingFaction = Faction::Axis;    // v2 default: Allies attack, Germans defend (v1 maps keep theirs)
  CaptureModel captureModel = CaptureModel::Timed;
  float captureSpeedCap = 3.0f;   // timed model: at most this many attackers speed the capture up
  int fragPerLife = 2, smokePerLifeAttacker = 1, smokePerLifeDefender = 0;
  float captureBase = 5.0f, capturePresenceFull = 5.0f;   // contest model: %/s at full advantage; attackers for full presence
  float laneLeft = 0.2f, laneMain = 0.6f, laneRight = 0.2f;
  bool friendlyFire = false;      // explosive / bullet damage to own side (default off; exposed in setup)
  static constexpr int kMaxCombatants = 32;   // AI_SPEC: up to 32 per match, both teams combined
  json toJson() const;
  static ScenarioRules fromJson(const json& j, int version = kScenarioVersion);
};

struct Scenario {
  int version = kScenarioVersion;
  std::vector<Objective> objectives;   // kept sorted by order
  std::vector<SpawnMarker> spawns;
  ScenarioRules rules;
  bool devTestMap = false;             // v1 flag (one objective is always allowed since v2); kept for round trips
  uint64_t nextSerial = 1;

  json toJson() const;
  static Scenario fromJson(const json& j, std::vector<std::string>* migrationNotes = nullptr);
  void sortByOrder();
  void renumber();                     // contiguous 0..n-1 in current sequence
  bool moveEarlier(const std::string& id);
  bool moveLater(const std::string& id);
  bool moveTo(const std::string& id, int to);   // drag reorder; spawn phases follow their objective
  // remove objective `id`: spawns lose that phase (deleted when they serve no phase; their racks are reported), later
  // phases shift down
  bool removeObjective(const std::string& id, std::vector<uint64_t>* orphanRacks = nullptr);
  bool canAddObjective() const { return (int)objectives.size() < kMaxObjectives; }
  Objective* find(const std::string& id);
  const Objective* find(const std::string& id) const;
  std::string newId(const char* prefix);
  std::vector<const SpawnMarker*> spawnsFor(CombatRole r, int phase) const;
  vec2 attackerStart() const;          // centroid of the phase-0 attacker spawns (or behind the first objective's arrow)
  vec2 attackAxis() const;             // unit x/z axis from attackerStart toward the last objective
};

// capture progress in percentage points per second for the given occupancy
float captureRateFor(const ScenarioRules& r, const Objective& o, int A, int D);

struct ScenarioIssue { bool error = true; std::string what; };
// Static validation (data only): SPEC 1.3 errors (0 / >10 objectives, a phase without attacker or defender spawn) and
// SPEC 1.3 / 2.1 distance warnings. Geometry checks (clearance, reachability) are done by the app with physics + nav
// (validateScenarioWorld) and append to the same list. `forPublish` is kept for callers; the rules are the same.
std::vector<ScenarioIssue> validateScenario(const Scenario& s, bool forPublish);

// largest-remainder allocation of n bots over weights, keeping at least `minMain` in index 1 (main assault)
std::vector<int> allocateLanes(int n, float left, float main, float right, int minMain = 1);

vec3 attackForward(float attackYawDeg);   // unit vector of the attack arrow
vec3 attackRight(float attackYawDeg);     // right of the arrow (attacker's right when moving along it)
