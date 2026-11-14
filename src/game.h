#pragma once
// Match game layer (server authority): soldiers (humans + bots), spawning, damage, the AI, grenades and smoke, and
// the presentation (skinned characters, HUD). Runs inside Explore mode when a match was started from the Play menu
// (Singleplayer / Host LAN); plain Explore walkthroughs are unchanged. Clients (net.cpp) mirror it from snapshots.
#include "match.h"
#include "nav.h"
#include "character.h"
#include "combat.h"
#include "grenade.h"

class App;

struct EnemyMemory {
  int enemy = -1;                // soldier index
  vec3 lastPos{0};               // last position actually perceived (never updated while unseen)
  double lastSeen = -100;
  float awareness = 0;           // 0..1 detection build-up (distance, movement, smoke)
  bool visible = false;
  bool heardOnly = false;        // position comes from hearing (approximate)
};

struct Brain {
  // archetype (data)
  int arch = 0; float reaction = 0.6f, aimTurn = 220, sigma = 1.1f, sight = 110, fov = 150, aggression = 0.5f; int burstMin = 1, burstMax = 1;
  enum Mode : int { M_IDLE, M_ADVANCE, M_HOLD, M_ENGAGE, M_COVER, M_INVESTIGATE, M_EVADE, M_RETURN, M_RELOAD, M_THROW };
  int mode = M_IDLE; float modeT = 0;
  int target = -1; float targetTime = 0, aimSteady = 0;
  float aimYaw = 0, aimPitch = 0;            // aim direction (turns at aimTurn), separate from the body / locomotion yaw
  std::vector<EnemyMemory> mem;
  int lane = LANE_MAIN;
  std::vector<vec3> route; size_t routeIdx = 0;
  std::vector<int> path; size_t pathIdx = 0; vec3 goal{0}; int goalSlot = -1;
  float repathT = 0, stuckT = 0; vec3 progressPos{0}; int pathFails = 0; float pathFailT = 0;
  vec3 holdPos{0}; int holdSpot = -1;        // reserved defend spot / objective position
  vec3 coverPos{0}; bool hasCover = false;
  float nextPerceive = 0, nextTactic = 0, grenadeCD = 4, burstLeft = 0, burstPause = 0, suppressed = 0, stallT = 0;
  float bestObjDist = 1e9f;
  vec3 hurtFrom{0}; double hurtAt = -100;
  vec3 evadeFrom{0}; double evadeUntil = -100;
  vec3 investigate{0};
  float doorWait = 0; uint64_t doorInst = 0; int doorIdx = -1;
  double coverCooldown = 0; vec3 boundFrom{0}; int routeStage = -1;
  bool crouch = false;
  std::string why;                           // debug overlay: current reason
};

struct Soldier {
  int slot = -1; bool inUse = false, alive = false;
  Faction faction = Faction::Allies; CombatRole role = CombatRole::Defender;
  int team = 0;                              // friend / foe (G1): Assault / TDM = (int)role, Solo = own slot
  bool zombie = false;                       // G2: AI type Zombies for its side (zombie.cpp): melee only, no weapon
  float meleeWind = -1; double meleeReady = 0;   // G2: zombie strike wind-up (s, < 0 = none), next strike allowed at Game::t
  int meleeCount = 0, meleeClip = 0; AnimPlayer anim;   // G2: strikes started, attack clip of the current one; clip state (zombie.cpp)
  bool localPlayer = false, human = false; int peer = -1;
  int agent = -1;                            // physics agent (bots, remote humans); the local player uses the explore character
  vec3 feet{0}, vel{0}; float yaw = 0, pitch = 0; int posture = 0; float speed = 0;
  bool swim = false; float swimBlend = 0;    // swimming (swim.cpp; bots: floating agents; LAN: snapshot flags 2|4, client posture 3)
  float hp = 100; double diedAt = 0; int deathStyle = 0; int killer = -1; vec3 deathDir{0};
  int weapon = -1; WeaponState ws; float reload = 0, reloadTotal = 0; int shots = 0;
  int frags = 0, smokes = 0;
  float throwT = -1; int throwKind = 0; vec3 throwTarget{0}; vec3 throwVel{0}; bool throwReleased = false;
  int model = -1; CharInstance ci; bool ciReady = false; PoseResult pose; float walkPhase = 0, recoil = 0, crouchBlend = 0;
  uint32_t poseRev = 0, skinRev = ~0u; int poseFrom = -1;   // PF1: skin only a changed pose; a far pack follower draws its leader's skin
  mat4 world{1};
  Brain br;
  uint32_t rng = 1;
  int lifeId = 0;                            // increments per spawn (net / tests)
  double lastShot = -100;
  float burnT = 0, burnDps = 0, burnTick = 0; int burnBy = -1;   // FT1: on fire (s left, damage per s, tick timer, who lit it)
  double protectUntil = -1;                  // spawn protection (SPEC 2.2): Game::t until which damage is ignored (W1 sets it)
  bool isProtected(double now) const { return alive && now < protectUntil; }
  std::map<int, WeaponState> wsBy;          // remote human: ammo per carried weapon (host)
  std::vector<int> carried;                 // remote human: the weapons the host lets it hold (loadout + rack swaps)
  double netHeard = 0, lastShotReal = -100;   // host: request timing in wall-clock seconds
};

struct HitZone { int zone = 1; float t = 0; };   // 0 head, 1 torso, 2 limb

class Game {
public:
  explicit Game(App& a) : app(a) {}
  App& app;
  Match match;
  Nav nav;
  Grenades gren;
  std::vector<Soldier> sold;                 // index == roster slot
  bool active = false;                       // a match is running in this Explore session
  bool pendingStart = false;                 // Play menu asked for a match; begins on enterExplore
  bool isClient = false;                     // mirror only (net client): no authority
  bool paused = false;
  int localSlot = 0;
  int pendingLocal = -1;                     // LAN client: our slot while its AI soldier still lives (late join)
  json combat, archetypes;
  std::vector<std::unique_ptr<CharModel>> models; std::map<std::string, int> modelIdx;
  int fragDef(Faction f) const;              // weapon-roster grenade items (E3 core): mk2_grenade / m24_grenade
  int smokeDef() const;                      // smoke_grenade
  void syncGrenadeView();                    // host: gren.live from App::grenades
  void localGrenadeCounts(Soldier& L, bool write);   // local player: inventory lives in the E3 items (App::wstate)
  struct Sound { vec3 p; double t; int team; float range; int src; int kind; };   // kind 0 shot, 1 blast, 2 grenade landing
  std::vector<Sound> sounds;
  double t = 0; uint64_t steps = 0;
  struct DefSpot { vec3 p; int nav = -1; float cover = 0, view = 0; int heldBy = -1; };
  std::vector<DefSpot> defSpots; int defSpotsStage = -1;
  std::map<int, int> grenadeReserve;         // target cell -> expiry step (squad reservation)
  std::map<int, double> smokeReserve;        // crossing cell -> time of the last smoke
  std::string setupError, lastNotice;
  bool debugOverlay = false;
  // LAN (net.cpp)
  json clientStart;                          // client: START message (slots, scenario)
  std::vector<std::pair<int, std::string>> humanPeers;   // host: lobby players -> defender slots
  struct Ghost { vec3 p, v; float life; int tracer; };   // tracer: tracerKindFor()   // visual-only rounds (client tracers)
  std::vector<Ghost> ghosts;
  std::map<uint64_t, int> doorSeen;          // door direction last replicated ((inst << 8) | idx)
  int spectate = -1;                          // soldier the dead local player watches
  float hitMarker = 0, hurtFlash = 0; vec3 lastHurtDir{0};
  std::vector<int> markedSlots;              // G4: the soldiers that got an ally head marker on the last HUD frame (tests)
  void drawAllyMarkers();                    // G4 (mpui.cpp)
  // statistics (tests / results)
  double pathMs = 0, stepMs = 0; int pathCalls = 0, stepCalls = 0;
  int shotsByBots = 0, hitsOnSoldiers = 0, kills = 0, doorOpens = 0, smokeThrows = 0, fragThrows = 0, navRebuilds = 0;

  // lifecycle
  bool begin(std::string& err);              // called by enterExplore when pendingStart
  void end();                                // exitExplore
  void step(float dt);                       // fixed 60 Hz, after the explore step (server)
  void clientStep(float dt);                 // LAN client: mirror, interpolation, local requests
  void posesStep(float dt);
  void stepRest(float dt);
  bool loadData(std::string& err);
  bool ensureModels(std::string& err);
  // spawning
  bool trySpawn(int slot, std::string& why);
  const std::vector<vec3>& spawnPool();      // G1: walkable spawn points over the map (Solo / unmarked TDM)
  std::vector<vec3> pool; bool poolBuilt = false;
  void killSoldier(int s, int killer, vec3 dir);
  void applyLoadout(Soldier& s, bool newLife);
  // damage (server)
  bool shellSegment(const Projectile& p, vec3 a, vec3 seg, float len, float& tHit);   // GM3: a tank / AT gun shell striking a soldier (kills; tHit = the strike)
  bool projectileSegment(Projectile& p, vec3 a, vec3 seg, float len, float& tHit);   // true: a soldier was hit
  void explosionDamage(vec3 p, float dmg, float radius, int owner, bool grenade);
  void damage(int s, float dmg, int attacker, vec3 dir, const char* how);
  bool flameHit(int victim, int owner, float dmg, vec3 dir, float burnSec, float burnDps);   // FT1 (flame.cpp): damage + ignite; false when refused
  void burnStep(float dt);                                  // FT1: burning soldiers: flames on the body (all), damage ticks (host)
  bool soldierFlame(Soldier& s, vec3 dir, float dt);        // FT1: a bot holds its nozzle open for dt
  bool meleeHit(int attacker, int victim, float dmg);   // G2 (zombie.cpp): a blow that landed (zombie strike, the knife)
  bool hitTest(const Soldier& s, vec3 a, vec3 d, float maxT, HitZone& hz) const;
  int soldierOfOwner(int owner) const;
  bool sameTeam(int a, int b) const { return a >= 0 && b >= 0 && sold[a].team == sold[b].team; }
  static bool hostile(const Soldier& a, const Soldier& b) { return &a != &b && a.team != b.team; }   // G1: every enemy check
  // perception helpers
  bool lineOfSight(vec3 a, vec3 b, float* smokeT = nullptr) const;
  vec3 eye(const Soldier& s) const;
  vec3 chest(const Soldier& s) const;
  // AI (ai.cpp)
  void aiStep(Soldier& s, float dt);
  void aiPerceive(Soldier& s);
  void aiTactics(Soldier& s);
  void aiFire(Soldier& s, float dt);
  void aiMove(Soldier& s, float dt);
  bool aiPathTo(Soldier& s, vec3 goal);
  bool findCover(Soldier& s, vec3 threat, float radius, vec3& out);
  void buildDefSpots();
  int pickDefSpot(Soldier& s);
  bool planFrag(Soldier& s, vec3& vel, vec3& target);
  bool planSmoke(Soldier& s, vec3& vel, vec3& target);
  void hearSound(vec3 p, int team, float range, int src, int kind);
  void assignLanes();
  // weapons for soldiers
  bool soldierFire(Soldier& s, vec3 dir);
  void soldierWeaponStep(Soldier& s, float dt);
  bool startThrow(Soldier& s, int kind, vec3 vel, vec3 target);
  void throwStep(Soldier& s, float dt);
  // presentation
  void drawItems();
  void drawHud();
  void drawDebug();
  // G1 HUD (mpui_modes.cpp)
  void drawModeBanner(); void drawModePanel(); void drawScoreboardOverlay(); void scoreboardTable();
  bool showScoreboard = false;               // tests: hold the Tab scoreboard open
  bool camera(vec3& eye, vec3& fwd, float& fov);
  Soldier* local() { return localSlot >= 0 && localSlot < (int)sold.size() ? &sold[localSlot] : nullptr; }
  vec3 soldierFeet(int s) const;
  // nav maintenance after destruction
  void worldChanged(vec3 lo, vec3 hi);
  float rnd(Soldier& s) { s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5; return (s.rng % 100000u) / 100000.0f; }
};

extern Game* gGame;
