#pragma once
// Match game layer (server authority): soldiers (humans + bots), spawning, damage, the AI, grenades and smoke, and
// the presentation (skinned characters, HUD). Runs inside Explore mode when a match was started from the Play menu
// (Singleplayer / Host LAN); plain Explore walkthroughs are unchanged. Clients (net.cpp) mirror it from snapshots.
#include "match.h"
#include "commands.h"   // CMD2 gAdmin (hostile)
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
  float aimWantYaw = 0;                      // AI5: where the aim wants to go (the body turns toward it; the aim twists at most 50 deg off the body)
  vec2 faceVel{0}; bool faceAim = false; float yawVel = 0;   // AN1 step 3: the travel direction the body faces (low-passed), the strafe / backpedal latch, the body's turn rate (deg/s)
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

extern bool gPosePerStep, gPoseLodOff, gPoseSerial, gStepCap8;   // CP A/B switches (game.cpp)
struct Soldier {
  int slot = -1; bool inUse = false, alive = false;
  Faction faction = Faction::Allies; CombatRole role = CombatRole::Defender;
  int team = 0;                              // friend / foe (G1): Assault / TDM = (int)role, Solo = own slot
  bool zombie = false;
  bool op = false;
  uint8_t kit = 0; float kitSpeed = 1; bool kitSparks = false;
  mat4 dfMagK{1}; bool dfMagHeld = false;   // DF1: a Delta Force reload: the magazine in the left hand (its offset from the hand at the grab)
  vec2 dfMagWin{0}; float dfPUp = 1;   /* WS1: the pistol posture, smoothed (gunbody.cpp) */ float dfBoltBack = 0, dfPumpBack = 0, dfPrevYaw = 0, dfYawRate = 0, dfYawOff = 0;   // DF1 gunbody.cpp: the reload span the magazine is out; the bolt / pump travel (m); the body's turn rate (deg/s)
  vec3 dbgVel{0};
  mat4 velWorld{1}, velGun{1}; int velFrame = -10, velGunFrame = -10;   // AIB1: the world matrix the body was last drawn with (TAA motion vectors), and that frame   // WSM2 captures: the velocity a frozen bot reports (its walk / run clip in place)
  bool holdBody = false;   // WSM2: a skin on the Allied paratrooper's skeleton (combat.json hold_bodies): the shared clips, the game's guns in the stored per-gun holds (gunbody.cpp deltaBodyPose)
  bool gunBody = false; int gmWeapon = -1; double gmShotAt = -100; float gmFireT = 1;   // GM1 / GM2: a body with its gun built in (gas-mask / desert soldiers, the Axis bots: combat.json gunbody_models): its weapon, the clip state of its shots (gunbody.cpp)   // SW1: 2 blaster trooper, 3 robot commando (combat.json kits: body, blasters, pace, hit sparks)
  float lean = 0;                            // MV3: the player's Q / E lean (-1 .. 1; local from App::lean, LAN in the snapshot)                           // AI5: an Operator bot (AiType::Operators): modern weapons (combat.json loadouts_operator)                       // G2: the melee brain (zombie.cpp: Zombies, heavies): no weapon
  int etype = 0; double staggerUntil = 0;    // EN1: enemy type (enemy_types.h; LAN snapshot), stopped by a heavy hit until
  int evariant = 0;                          // the type's model variant (Game::begin, in turn per type; clients compute the same)
  float horseHp = 0, riderHpKeep = 0; bool hitHorse = false;   // HK3: the mounted knight's horse health, the rider's health when the horse fell, a bullet that met the horse (damage() routes it)
  int8_t mountDeath = -1;   // HK2: a mounted knight's death: -1 not decided, 0 the rider shot off, 1 the horse down (on foot after it), 2 done
  bool flat = false;   // crushed by a heavy's stomp (stomp_area): the body is drawn pressed flat into the ground
  float eScale = 1, eLean = 0, eBody = 1; bool eFallback = false;   // EN1 (aiZombieAssign): draw scale, lean, body size vs a 1.85 m zombie, on the zombie rig
  float meleeWind = -1; double meleeReady = 0;   // G2: zombie strike wind-up (s, < 0 = none), next strike allowed at Game::t
  int meleeCount = 0, meleeClip = 0; AnimPlayer anim;   // G2: strikes started, attack clip of the current one; clip state (zombie.cpp)
  bool localPlayer = false, human = false; int peer = -1;
  int agent = -1;                            // physics agent (bots, remote humans); the local player uses the explore character
  vec3 feet{0}, vel{0}; float yaw = 0, pitch = 0; int posture = 0; float speed = 0;
  vec3 prevFeet{0}; float poseAcc = 0; int poseRate = 1; int poseKey = -1;   // poseKey: posture / alive / swim / life when last posed (a change forces a pose)   // CP: feet at the last step's start (drawn between the two), time since the last pose evaluation, its update interval (frames)
  vec3 lastGoodFeet{0};                      // the last finite feet (Game::step restores it if a NaN appears)
  bool swim = false; float swimBlend = 0;    // swimming (swim.cpp; bots: floating agents; LAN: snapshot flags 2|4, client posture 3)
  float hp = 100; double diedAt = 0; int deathStyle = 0; int killer = -1; vec3 deathDir{0};
  int weapon = -1; WeaponState ws; float reload = 0, reloadTotal = 0; int shots = 0;
  int frags = 0, smokes = 0;
  int helmetLvl = 0, vestLvl = 0, backpack = 0;   // BR2 Battle Royale: worn armour levels 0-3, backpack 0 none / 1 / 2 military (BR3 draws them)
  int cls = -1, clsPick[2] = {0, 0};          // GM1: class (classes.h index, -1 = none) and its primary / secondary choice
  float throwT = -1; int throwKind = 0; vec3 throwTarget{0}; vec3 throwVel{0}; bool throwReleased = false;
  int model = -1; CharInstance ci; bool ciReady = false; PoseResult pose; float walkPhase = 0, recoil = 0, crouchBlend = 0;
  float anProne = 0;   // AN1 step 3: the prone blend (was a one-frame switch)
  // AN1 step 3b: foot planting: a planted foot's world anchor, the point a releasing foot blends from, lock / standing re-step flags,
  // the release blend's clock (-1 none) and length (s)
  vec3 anFootW[2] = {vec3(0), vec3(0)}, anFootFrom[2] = {vec3(0), vec3(0)}; bool anFootLock[2] = {false, false}, anFootStep[2] = {false, false};
  float anFootRel[2] = {-1, -1}, anFootDur[2] = {0.3f, 0.3f};
  vec3 anFootN[2] = {vec3(0), vec3(0)};   // AN1 step 3c: the ground normal (world) under a planted foot, from its touchdown probe
  float anGunYaw = 0, anGunVel = 0, anGunPitch = 0, anGunPVel = 0; bool anGunInit = false;   // AN1 step 3: the gun's drawn world facing (deg), a damped follow of the brain's aim
  float anKneel = 0, anCarry = 0, anMoveAim = 0, anLow = 0, anTwist = 0, anStride = 0; vec3 anDir{0, 0, 1}; vec2 anSlope{0}; bool netEngaged = false;   // AN1 posture blends (Game::posesStep)
  uint32_t poseRev = 0, skinRev = ~0u; int poseFrom = -1;   // PF1: skin only a changed pose; a far pack follower draws its leader's skin
  mat4 world{1};
  Brain br;
  uint32_t rng = 1;
  int lifeId = 0;                            // increments per spawn (net / tests)
  double lastShot = -100;
  int deathClip = -1, deathPickLife = -1; bool deathRunning = false;   // deathRunning: over 3 m/s when killed    // the death clip picked for this life (gunbody.cpp pickDeath: by side, zone, crouch, blast)
  std::string lastHow;                       // SD1: the last damage kind (kill rewards: knife, grenade, firearm)
  double drawnAt = -100;                    // GP1 spectate check: the last time the draw pass drew this body (not culled)
  bool genericHits = false;                  // GP1: hit zones from the feet / posture / facing, not the posed body (a non-soldier body picked on the Character screen)
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
  struct HorseCorpse { CharInstance ci; mat4 world{1}; std::vector<int> parts; };   // HK2: a mounted knight's fallen horse, its skin held at the last pose
  std::vector<std::unique_ptr<HorseCorpse>> horseCorpses;
  bool active = false;                       // a match is running in this Explore session
  bool pendingStart = false;                 // Play menu asked for a match; begins on enterExplore
  bool noFootLock = false;                   // AN1 step 3b: the pose harness (a bot posed in place) turns the world foot locks off
  bool isClient = false;                     // mirror only (net client): no authority
  bool paused = false;
  int localSlot = 0;
  std::string respawnVehicle; int respawnStation = 0;   // TK1: the vehicle the local player died in (a fresh copy at the next spawn)
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
  struct Ghost { vec3 p, v; float life; int tracer; vec3 off{0}; float trav = 0; };   // tracer: tracerKindFor()   // visual-only rounds (client tracers); AT4 off / trav: drawn from the muzzle (visFade)
  std::vector<Ghost> ghosts;
  std::map<uint64_t, int> doorSeen;          // door direction last replicated ((inst << 8) | idx)
  vec3 specEye{0}, specFwd{0, 0, -1}; bool specCam = false;   // the death / spectate camera this frame (renderView culls against it)
  int spectate = -1;                          // soldier the dead local player watches
  float hitMarker = 0, killMarker = 0, hurtFlash = 0; vec3 lastHurtDir{0};
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
  void posesFrame(float dt, float alpha);   // CP: once per drawn frame (dt: the steps' summed time; alpha: the step fraction left, for drawing between the last two steps)
  float drawAlpha = 1; int poseFrameNo = 0; double poseMs = 0; Soldier* poseOnly = nullptr; int posesOnDemand = 0;
  void poseNow(Soldier& s);   // CP: evaluate a soldier's held pose now (a bullet reached its bounds)   // CP (poseMs: main-thread time in posesStep, summed)
  void stepRest(float dt);
  bool loadData(std::string& err);
  bool ensureModels(std::string& err);
  // spawning
  bool trySpawn(int slot, std::string& why);
  const std::vector<vec3>& spawnPool();      // G1: walkable spawn points over the map (Solo / unmarked TDM)
  std::vector<vec3> pool; bool poolBuilt = false;
  void killSoldier(int s, int killer, vec3 dir);
  void applyLoadout(Soldier& s, bool newLife);
  // GM1 deploy screen (deploy.cpp): before each local spawn; auto-deploys after kDeployAuto s
  struct SpawnOption { vec3 pos{0}; float yaw = 0; std::string label; int team = -1; };
  struct Drop { vec3 pos{0}; float yaw = 0; int weapon = -1; WeaponState ws; double t = 0; };
  std::vector<Drop> drops;                   // GM1: weapons AI soldiers dropped on death (host / singleplayer); E picks one up
  std::vector<SpawnOption> outposts;         // GM3 hook: team-built outposts (team = Soldier::team), listed as spawn points
  std::vector<SpawnOption> spawnOptions(int slot) const;   // the slot's markers (role / team) + its team's outposts
  static constexpr double kDeployAuto = 10.0;   // GM1b: was 5 s
  double deployOpenedAt = 0; bool deployReady = false; int deploySpawn = -1;   // deploySpawn: index into spawnOptions, -1 = random
  void deployNow();
  void drawDeploy();
  void chooseClass(Soldier& s);              // GM1: humans keep an allowed choice, bots draw by botWeight
  std::vector<int> classWeapons(const Soldier& s) const;   // GM1: primary, secondary, pistol (weapon indices)
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
  bool coarseHit(const Soldier& s, vec3 a, vec3 d) const;   // CP: hitTest's bounding sphere only
  int soldierOfOwner(int owner) const;
  bool sameTeam(int a, int b) const { return a >= 0 && b >= 0 && sold[a].team == sold[b].team; }
  static bool hostile(const Soldier& a, const Soldier& b) {   // G1: every enemy check; CMD2: an AI never takes the /admin (ignored) or anyone (/peace) for an enemy
    return &a != &b && a.team != b.team && !(!a.human && (gAdmin.peace || (gAdmin.ignored && b.localPlayer))); }
  // perception helpers
  bool lineOfSight(vec3 a, vec3 b, float* smokeT = nullptr) const;
  vec3 eye(const Soldier& s) const;
  void renderView(vec3& eye, vec3& fwd) const;   // the camera the frame renders from: a vehicle / gun camera when seated, else the eye
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
  // TK1: armour and big enemies. bulletProof: crew of an armoured tank (small arms do nothing); bigTarget: that, an
  // armoured car or helicopter crew, or a heavy enemy type: worth a rocket
  bool bulletProof(const Soldier& o) const;
  bool bigTarget(const Soldier& o) const;
  int carriedLauncher(const Soldier& s) const;   // a rocket launcher in the bot's carried kit (weapon index), -1: none
  void switchWeapon(Soldier& s, int w);           // a bot's hand weapon: the old one's state kept in wsBy
  void soldierWeaponStep(Soldier& s, float dt);
  bool startThrow(Soldier& s, int kind, vec3 vel, vec3 target);
  void throwStep(Soldier& s, float dt);
  // presentation
  void drawItems();
  void drawHud();
  void drawDebug();
  // G1 HUD (mpui_modes.cpp)
  void drawModeBanner(); void drawModePanel(); void drawVitals(); void drawScoreboardOverlay(); void scoreboardTable(bool full = false);   // UI1: full = the end screen (every row, scrollable)
  bool showScoreboard = false;               // tests: hold the Tab scoreboard open
  bool camera(vec3& eye, vec3& fwd, float& fov);
  bool cameraOrbit(vec3& eye, vec3& fwd, float& fov);   // camera()'s choice (camera() records it for renderView)
  Soldier* local() { return localSlot >= 0 && localSlot < (int)sold.size() ? &sold[localSlot] : nullptr; }
  vec3 soldierFeet(int s) const;
  // nav maintenance after destruction
  void worldChanged(vec3 lo, vec3 hi);
  float rnd(Soldier& s) { s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5; return (s.rng % 100000u) / 100000.0f; }
};

extern Game* gGame;
