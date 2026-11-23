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
// Match modes (G1). Assault: the objective mode (attackers / defenders, lives, waves). Solo: free-for-all, every
// soldier is its own team, first to the kill cap wins. TDM: two teams (team 0 = the Defender role, team 1 = Attacker),
// unlimited respawns, the first team to the kill cap wins. The role stays the team's identity in TDM, so role-based
// code keeps working; friend / foe is always the roster team (Game::hostile).
// BR1 Battle royale (royale.h): every soldier its own team like Solo, one life, last alive wins; drop, storm.
enum class GameMode : int { Assault = 0, Solo = 1, TDM = 2, BattleRoyale = 3, SearchDestroy = 4 };   // SD1: Search and Destroy (sd_mode.h)
inline const char* gameModeName(GameMode m) { return m == GameMode::Solo ? "Solo" : m == GameMode::TDM ? "Team Deathmatch" : m == GameMode::BattleRoyale ? "Battle royale" : m == GameMode::SearchDestroy ? "Search and Destroy" : "Assault"; }
inline bool teamMode(GameMode m) { return m == GameMode::TDM || m == GameMode::SearchDestroy; }   // two persistent teams (Match slot.team 0 / 1)
inline bool ffaMode(GameMode m) { return m == GameMode::Solo || m == GameMode::BattleRoyale; }   // every soldier its own team
enum class AiType : int { Soldiers = 0, Zombies = 1, ZombiesHeavies = 2, Brutes = 3, RetiredA = 4, Operators = 5, RetiredB = 6, Spiders = 7, Creatures = 8, Knights = 9, RetiredE = 10, RetiredC = 11, RetiredD = 12, Abominations = 13, Golems = 14, Boglords = 15, Troopers = 16, Robots = 17, RetiredF = 18, Desert = 19, Delta = 20, Allied = 21, RetiredG = 22, SS = 23, Mechs = 24, AlliedPara = 25, Knights2 = 26, Knights3 = 27, Knights4 = 28, Knights5 = 29, Knights6 = 30, Knights7 = 31, Knights8 = 32, Knights9 = 33, Archers1 = 34, Knights10 = 35, Archers2 = 36, KnightsMounted = 37, KnightsAssorted = 38, FallenKnight = 39, Dragon = 40, MechTank = 41, SpearKnight = 42, Civilians = 43, OperatorF = 44, FallenKnight2 = 45, Boss = 46, ElkKnight = 47, Samurai = 48, Crusader = 49, Crusader2 = 50, Paladin = 51, Paladin2 = 52, RomanArcher = 53, ChineseSoldier = 54 };   // EN1: 2 mixes the enemy types by weight; 3 brutes (trolls; was "Heavies", trolls and spiders), 7 spiders (stalkers), 8 creatures; 4 retired (RM1); 5 soldiers in operator bodies; 6 retired (RM1); CH1: 9-12 the knight, animatronic, alien, alien_queen types (the last three local-only: a zombie without them)
constexpr int kAiTypeCount = 55;   // CS1: 54 Chinese Soldier (zombie.json 'chinese_soldier': Ming-style lamellar armour and a 2.69 m yari on Knight 4's rig and great-sword clips; committed)   // RA1: 53 Roman Archer (zombie.json 'roman_archer': Archer 2's behaviour and clip names on a Roman archer; Archer 2, or Archer 1, were its model missing)   // PK2: 52 Paladin 2 (zombie.json 'paladin2': the heavy armour with the Guts sword on Knight 4's two-handed clips; committed model, the Paladin without it)   // PK1: 51 Paladin (zombie.json 'paladin': the platemail knight with the Guts sword on Knight 4's two-handed clips; committed)   // LT1: 50 Crusader 2 (zombie_local.json 'crusader2': Letholdus, the templar in the Anikin head and Helmet No11 with the Chevalier sword, the sword-and-shield clips without the shield; local-only model, Knight 1 without it)   // CRU1: 49 Crusader (zombie.json 'crusader': a medieval guard with a mace on Knight 5's rig and clips; Knight 5 were its model missing)   // SAM1: 48 Samurai (zombie.json 'samurai': the armoured samurai with a katana in both hands, a knight in class, size and health; five great-sword attacks)   // EK: 47 Elk Knight (zombie.json 'knight10e': Knight 10 with a halberd, a shield and wings on an armoured elk; looks round after each stab; on foot as 'knight10e_foot' with the Spear Knight's thrusts when the elk falls; the Mounted Knight without its models)   // BC1: 46 Boss (zombie_local.json 'boss_choi': 5x, invulnerable, a seated statue that breathes and talks; local-only model, the Brute without it)   // FK2: 45 Fallen Knight 2 (zombie.json 'fallen_knight2': the plague-helm knight with axe and shield at the Fallen Knight's size and health, its own walk, guarded walk, run and four attacks; its model is local-only: Knight 1 at that size without it)   // OPF1: 44 Giant Operator (the Operators' brain, guns and clips in a body of its own at 1.5x: combat.json operator_f; its model is local-only, the operator body at that size without it)   // NPC1: 43 Civilians (zombie.json types with "npc": fight back when hurt above half health, else flee; flee types also flee from harm they see; nobody's enemy until then)   // KS: 42 Spear Knight (zombie.json 'knight_spear': three builds (helm, breastplate, spear, shield), stab clips only, 1.3x the knights' reach; committed)   // MT1: 41 Mech Tank (an autonomous AI vehicle, not a soldier: Game::begin places that many crewless tanks per side, tank_mech.cpp; local only)   // DR1: 40 Dragon (the Mountain Dragon, ground only: zombie.json 'dragon', committed); FK1: 39 Fallen Knight (the Brother Knight at 2x a knight's body, 5x health: zombie.json 'fallen_knight'; Knight 1 at that size without its local model); AK1: 38 Assorted Knights (each slot one of zombie.json "assorted": Knights 1-10 and the Spear Knight by weight; AK2: no archers); HK1: 37 Mounted Knight (Knight 10 on the armoured horse: zombie.json 'knight10m'; Knight 10 on foot with perf horseknight off or without its model); AR2: 36 Archer 2 (Archer 1's behaviour, the hooded archer: zombie.json 'archer2'; its model is local-only, Archer 1 without it); K10: 35 Knight 10 (the winged knight with cuirass, sword, triangular shield and a baked cloak: zombie.json 'knight10', committed); AR1: 34 Archer 1 (longbow: the first ranged enemy type, zombie.json 'archer1', committed); KN8 / KN9: 32 Knight 8 (halberd), 33 Knight 9 (sabre): zombie.json 'knight8' / 'knight9' (committed)
//   // KN7: 31 Knight 7 (great helm, cuirass, viking axe, steel scutum; zombie.json 'knight7', committed)
//   // KN6: 30 Knight 6 (sallet, breastplate, sword, Polish shield; zombie.json 'knight6', committed). The aiTypes mask is 64-bit (KN6): 63 types at most
//   // KN4 / KN5: 28 Knight 4 (zweihander), 29 Knight 5 (battle axe): melee, zombie.json 'knight4' / 'knight5' (committed models). (KN6: the mask widened to 64 bits)
//   // KN3: 27 Knight 3 (melee: zombie.json 'knight3', the flail / winged helm / Roman shield knight; local-only model, Knight 1 without it)
//   // KN2: 26 Knight 2 (melee: zombie.json 'knight2', the mace / frog-helm / round-shield knight; local-only model, Knight 1 without it); 9 shown as "Knight 1"
//   // WS1: 25 Allied paratroopers (soldier brain, the Allied clip body with the game's guns in stored per-gun holds: pose.cpp gripPose)
//     // NA1: 21 Allied soldiers, 22 gas-mask soldiers 2, 23 SS soldiers (gun-body clips, 8-way: gunbody.cpp), 24 mechs (melee: zombie.json 'mech')
//   // DF1: 20 Delta Force (soldier brain, clip-driven body, the game's guns in procedural hands: gunbody.cpp deltaBodyPose)
//   // GM1 / GM2: 18 gas-mask soldiers, 19 desert soldiers (soldier brain, clip-driven bodies with the gun built in: gunbody.cpp)
//   // SW1: 16 / 17 blaster troopers and robot commandos (soldier brain, kit bodies and blasters; combat.json kits)
//   // CH3 / CH4: 13-15 the abomination, golem, boglord types (local-only: a zombie without them)
inline const char* aiTypeName(AiType t) {
  static const char* n[kAiTypeCount] = {"Soldiers", "Zombies", "Zombies + heavies", "Brutes", "(retired)", "Operators", "(retired)", "Spiders", "Creatures", "Knight 1", "(retired)", "(retired)", "(retired)", "Abominations", "Golems", "Boglords", "Blaster troopers", "Robot commandos", "(retired)", "Desert soldiers", "FSB operator", "Allied soldiers", "(retired)", "SS soldiers", "Mechs", "Allied paratroopers", "Knight 2", "Knight 3", "Knight 4", "Knight 5", "Knight 6", "Knight 7", "Knight 8", "Knight 9", "Archer 1", "Knight 10", "Archer 2", "Mounted Knight", "Assorted Knights", "Fallen Knight", "Dragon", "Mech Tank", "Spear Knight", "Civilians", "Giant Operator", "Fallen Knight 2", "Boss", "Elk Knight", "Samurai", "Crusader", "Crusader 2", "Paladin", "Paladin 2", "Roman Archer", "Chinese Soldier"};
  return n[std::clamp((int)t, 0, kAiTypeCount - 1)];
}
inline int64_t aiBit(AiType t) { return int64_t(1) << (int)t; }   // KN6: 64-bit (31 types and more)   // FSB1: slot 20 (AiType::Delta, kit 4) is shown as "FSB operator" and wears skin_fsb (same slot / LAN bit: saves naming Delta Force load as it)
// RM1: slots 4 and 6 were removed enemy types; kept empty so later types keep their numbers (saved setups, LAN bits).
// Loading a setup turns them into Zombies (ScenarioRules::fromJson); menus skip them.
// RM2 (the user, 2026-11-21): 10 animatronics (the clown), 11 aliens, 12 alien queens, 18 / 22 gas-mask soldiers 1 / 2 removed the same way;
// a setup naming them loads with the gas-mask AI as plain soldiers and the other three as plain zombies.
inline bool aiTypeRetired(int t) { return t == (int)AiType::RetiredA || t == (int)AiType::RetiredB || t == (int)AiType::RetiredC || t == (int)AiType::RetiredD || t == (int)AiType::RetiredE || t == (int)AiType::RetiredF || t == (int)AiType::RetiredG; }
inline AiType aiTypeRetiredTo(int t) { return t == (int)AiType::RetiredF || t == (int)AiType::RetiredG ? AiType::Soldiers : AiType::Zombies; }
// a side's k-th AI plays the (k mod n)-th of its n ticked types (an empty mask counts as Soldiers)
inline AiType aiTypeFor(int64_t mask, int k) {
  int n = 0; for (int t = 0; t < kAiTypeCount; ++t) n += (mask >> t) & 1;
  if (!n) return AiType::Soldiers;
  k %= n; for (int t = 0; t < kAiTypeCount; ++t) if ((mask >> t) & 1 && k-- == 0) return (AiType)t;
  return AiType::Soldiers;
}
// AI3: a side's next AI with per-type counts: the type furthest behind its share, so the types come out mixed in
// proportion (40 / 5 / 200 / 1 interleave, and more AI than counted keep the proportions). `given` counts what this side
// has handed out so far. No counts (rules before AI3): the ticked types in turn, as aiTypeFor.
inline AiType aiTypeNext(const int* count, int64_t mask, int* given) {
  int sum = 0, n = 0; for (int t = 0; t < kAiTypeCount; ++t) { sum += std::max(0, count[t]); n += given[t]; }
  AiType a = aiTypeFor(mask, n);
  if (sum > 0) {
    double bd = -1e18;
    for (int t = 0; t < kAiTypeCount; ++t) if (count[t] > 0) { double d = (double)count[t] * (n + 1) / sum - given[t]; if (d > bd) { bd = d; a = (AiType)t; } }
  }
  ++given[(int)a]; return a;
}

constexpr int kScenarioVersion = 2;
constexpr int kMinObjectives = 1, kMaxObjectives = 10;
inline char objectiveLetter(int order) { return char('A' + std::clamp(order, 0, 25)); }

// VH7: where a team's vehicles come in (the fresh copy after a death in a vehicle), behind its infantry spawns. team -1:
// neutral (Solo). Read by TankSys::spawnFresh through Game, and by the minimap (Scenario::vehicleSpawns: id, team, pos, yaw)
struct VehicleSpawn { std::string id; int team = -1; vec3 pos{0}; float yaw = 0; };   // yaw: the vehicle's heading (asset convention, +z ahead at 0)

struct SpawnMarker {
  std::string id, label;
  CombatRole role = CombatRole::Attacker;
  std::vector<int> phases{0};    // objective sequence indices this marker serves (spawns while one of them is active)
  vec3 pos{0}; float yaw = 0;
  uint64_t rack = 0;             // instance id of the team weapons rack placed next to it (0 = none / deleted)
  int team = -1;                 // TDM spawn for team 0 / 1 (G1); -1 = an Assault marker (role + phases)
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
  float sdRoundTime = -1, sdFreeze = -1, sdBuy = -1, sdFuse = -1, sdWarmupTime = -1;   // SD8: SD clocks (s); -1: search_destroy.json's
  bool sdShort = false, sdWarmup = false, sdVehicles = false;   // SD1 Search and Destroy: the MR12 preset (first to 13) instead of MR15; a warmup before round 1 (SD3: off by default); SD3 vehicles in the rounds (off: none exist)
  // ---- G1 modes (Solo / TDM); Assault ignores these
  GameMode mode = GameMode::Assault;
  int killCap = 25;               // Solo: the top player's kills; TDM: a team's kills
  int soloPlayers = 8;            // Solo: combatants in all (humans + AI)
  int royalePlayers = 30;         // BR1 battle royale: combatants in all (humans + AI), 10..kMaxCombatants (AI2; was 100)
  int teamPlayers[2] = {8, 8};    // TDM: combatants per team (humans + AI)
  int heavyTrollPct[2] = {35, 35};   // EN3: the troll share of the old AI type Heavies; unused since Brutes / Spiders are their own types (kept in the rules JSON)
  int64_t aiTypes[2] = {1, 1};    // per team, the ticked AI types (aiBit mask; Assault: [Defender], [Attacker]); Solo uses [0]
  // AI3: per team, how many AI of each type (the setup window's typed counts; the team / player totals above are
  // derived from them plus the humans). All 0: the pre-AI3 rules, aiTypes taken in turn (aiTypeNext)
  int aiCount[2][kAiTypeCount] = {};
  int aiSum(int t) const { int s = 0; for (int k = 0; k < kAiTypeCount; ++k) s += aiCount[t][k]; return s; }
  int humanTeam = 0;              // TDM: the host's team (LAN joiners balance the teams)
  Faction teamFaction[2] = {Faction::Allies, Faction::Axis};  // TDM / Solo uniforms
  float respawnDelay = 4.0f;      // Solo / TDM: s from death to respawn
  bool noRespawn = false;         // TDM: one life each; the team with someone left wins
  bool lootModern = false;        // Battle Royale: floor guns from the modern list (else WW2) (loot.cpp)
  std::vector<std::string> allowedClasses;   // GM1: class ids the host allows (empty = every class)
  bool classAllowed(const std::string& id) const { for (auto& c : allowedClasses) if (c == id) return true; return allowedClasses.empty(); }
  int armyUnits[2] = {0, 0};      // GC4 army mode: per team (Assault: defenders, attackers; TDM: A, B) AI beyond the roster, run as the GPU crowd (crowd.cpp)
  static constexpr int kMaxArmy = 100000;        // GC4: the crowd's AI in all (the GPU holds 131,072)
  static constexpr int kMaxAI = 1000;            // up to 1000 AI (AI2, user request 2026-11-15; was 200, 100)
  static constexpr int kMaxCombatants = kMaxAI + 16;   // plus up to 16 humans
  json toJson() const;
  static ScenarioRules fromJson(const json& j, int version = kScenarioVersion);
};

struct Scenario {
  int version = kScenarioVersion;
  std::vector<Objective> objectives;   // kept sorted by order
  std::vector<SpawnMarker> spawns;
  std::vector<VehicleSpawn> vehicleSpawns;   // VH7
  const VehicleSpawn* vehicleSpawnFor(int team, vec3 near) const;   // the team's nearest (else a neutral one; null: none)
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
