#pragma once
#include "assets.h"
#include "render.h"
#include "recorder.h"
#include "terrain.h"
#include "mapdoc.h"
#include "physics.h"
#include "audio.h"
#include "combat.h"
#include "viewmodel.h"

struct GLFWwindow;

enum class Mode { Creative, Explore };
enum class Tool { Select, Place, Marker };

// Impact-centred wall damage (wallfield.cpp): per bay, depth removed from the front and back face per cell.
enum WallHitKind { WALL_HE = 0, WALL_AP = 1, WALL_ROCKET = 2, WALL_GRENADE = 3, WALL_BULLET = 4 };
struct WallEvent { vec3 p{0}, d{0, 0, -1}; int kind = WALL_HE; float dmg = 120; uint32_t seed = 1; };   // asset-local contact, direction
struct WallField {
  std::vector<float> rf, rb;   // removed depth (m) from the front (+normal) and back face, per cell
  bool active = false;         // has its own rebuilt mesh / collision
  float openArea = 0, removedFrac = 0; int boxes = 0;
};

// Runtime (non-saved) state for one placed instance. Rebuilt whenever Instance::rev changes.
struct InstanceRT {
  uint64_t id = 0; uint32_t rev = 0;
  const AssetDef* asset = nullptr;
  uint32_t staticBody = 0xffffffffu;
  std::vector<std::vector<uint32_t>> leafBodies; // [interactable][leaf]
  std::vector<float> progress;                    // 0 closed .. 1 open
  std::vector<int> dir;                           // -1 closing, 0 idle, +1 opening
  uint32_t wallBody = 0xffffffffu;                // replaceable wall compound (panels)
  std::vector<float> hp;                          // per panel, Explore session overlay
  std::vector<uint8_t> state;                     // PanelState per panel
  bool damaged = false;                           // any panel not intact
  float lastRebuildMs = 0, lastPanelMs = 0;
  std::vector<WallField> wf;                      // damage field per panel (empty = untouched)
  std::vector<GpuMesh> pmesh;                     // rebuilt mesh per damaged panel
  std::vector<ShapePtr> pshape;                   // rebuilt collision per damaged panel (null = nothing left)
  std::vector<WallEvent> wallEvents;              // authored + session events, in order (replayable)
  StationRT st;                                   // crew-served station overlay (if the asset is a station)
  std::vector<std::vector<uint8_t>> leafSt;       // [interactable][leaf] LEAF_* flags (session overlay)
  std::vector<std::vector<float>> leafHits;       // bullet hits taken
  float treeHits = 0; bool treeFelled = false, trunkApplied = false; uint32_t trunkBody = 0xffffffffu;   // trees (foliage.cpp)
  float defHp = 0; bool defDestroyed = false;       // defences (builder.cpp)
  uint32_t playerBody = 0xffffffffu;                 // player-only traversal volume (wire)
  GpuMesh pathMesh; uint32_t pathGround = ~0u, pathRev = 0; std::vector<uint8_t> slabCut;   // path entities (paths.cpp)
  std::vector<float> secHp; std::vector<uint8_t> secCut;   // wire sections (session overlay): hp, cut
};

struct ExploreInput {
  vec2 move{0}; bool sprint = false, crouch = false, jump = false, interact = false; float dyaw = 0, dpitch = 0;
  bool fire = false, aim = false, reload = false, switchPos = false, choose1 = false, choose2 = false, nextWeapon = false, toggleAmmo = false, build = false; float wheel = 0, lookYaw = 0, lookPitch = 0;
  bool fireMode = false, prone = false, packUp = false, rackUp = false, rackDown = false, rackEquip = false, rackClose = false;
  bool jumpHeld = false;   // Space held (vault)
  bool quickGrenade = false;   // H (rebindable "grenade"): throw the side's grenade from any weapon
};

struct Snapshot { std::vector<Instance> inst; StartMarker start; std::set<std::string> removedVeg; uint64_t selected = 0; std::map<int, std::vector<float>> terrain; };

struct PlaceCheck { bool ok = true; std::string reason; float y = 0; };

struct InteractTarget { bool valid = false; uint64_t inst = 0; int idx = -1; float t = 0; int kind = 0; /*0 door/window, 1 station, 2 crate, 3 rack*/ };
struct ImpactInfo { vec3 pos{0}; std::string surface; int kind = 0; float time = -10; int breached = 0; };

class SelfTest;

class App {
public:
  int run(int argc, char** argv);

  // ---- core
  GLFWwindow* win = nullptr;
  Renderer R;
  AssetLibrary lib;
  Terrain terrain;
  std::vector<TemplateInfo> templates;
  MapDoc doc;
  fs::path docPath;
  bool dirty = false;
  Mode mode = Mode::Creative;
  std::map<uint64_t, InstanceRT> rt;
  uint32_t revCounter = 1;
  Audio audio;
  double now = 0;
  float frameDt = 0.016f;
  bool quit = false;
  std::string notice; double noticeUntil = 0;
  void toast(const std::string& s, double secs = 4.0);

  // ---- camera (Creative)
  vec3 camPos{0, 40, 80}; float camYaw = 0, camPitch = -20, flySpeed = 20;
  bool looking = false;
  mat4 viewM{1}, projM{1};

  // ---- editor
  Tool tool = Tool::Select;
  const AssetDef* placing = nullptr;
  float placeYaw = 0;
  bool gridSnap = false, rotSnap = true, gridOverlay = false, editInteract = false, showBrowser = true, showStats = false;
  uint64_t selected = 0;
  bool dragging = false, dragPending = false; vec3 dragOffset{0}; vec2 dragStartMouse{0}; Instance dragOrig;
  std::vector<Snapshot> undoStack, redoStack;
  std::string browserFilter; int browserCategory = 0; bool browserEnterableOnly = false; int browserMinFloors = 0;
  bool showNewMap = false, showOpen = false, showSaveAs = false;
  char nameBuf[128] = "My Normandy map";
  char saveAsBuf[128] = "my_map";
  int newTemplateIdx = 0;
  PlaceCheck lastCheck;
  vec3 hoverGround{0}; bool hoverValid = false;

  // ---- explore
  float eyeYaw = 0, eyePitch = 0, eyeHeight = 1.62f;
  bool pauseMenu = false;
  vec3 exploreStartPos{0}; float exploreStartYaw = 0;
  InteractTarget target;
  float physAccum = 0;
  float stepTimer = 0;
  EditorCam savedCam;
  ExploreInput scriptedInput; bool useScripted = false;

  // ---- self test
  SelfTest* selftest = nullptr;

  // ---- runtime/doc management
  const AssetDef* assetFor(const Instance& i) const;
  mat4 instanceMatrix(const Instance& i) const;
  void syncRuntime();
  void destroyRT(InstanceRT& r);
  void buildRT(const Instance& i, InstanceRT& r);
  void resetInteractStates();
  bool authoredOpen(const Instance& i, const InteractDef& d) const;
  float leafAngle(const InteractDef& d, float p) const;
  mat4 leafMatrix(const Instance& i, const Leaf& l, const InteractDef& d, float p) const;
  void updateLeafBodies(const Instance& i, InstanceRT& r, int k);
  void clearRuntime();
  // ---- destruction (damage.cpp)
  struct Explosion { vec3 pos{0}; std::vector<std::string> hits; int breached = 0; bool crater = false; };
  Explosion explode(vec3 p, vec3 dir, float damage, float radius, float craterScale, uint64_t ignoreUd = ~0ull, int wallKind = WALL_HE);
  // ---- impact-centred wall damage (wallfield.cpp)
  struct DestructionSettings {
    float cell = 0.04f;         // damage-field resolution (m); quality setting
    float depthStep = 0.02f;    // removed-depth quantisation for the rebuilt mesh (m)
    float breachArea = 0.04f;   // through-hole area that counts a bay as breached (m^2)
    int maxFragmentsPerHit = 8;
  } destruction;
  void initWallFields(InstanceRT& r);
  int stampWall(InstanceRT& r, const WallEvent& e, bool effects, std::vector<std::string>* hitIds, int* newlyBreached);
  void rebuildPanel(InstanceRT& r, size_t k);
  json wallEventsJson(const InstanceRT& r) const;
  void authoredWallEvents(const Instance& i, const AssetDef* a, std::vector<WallEvent>& out) const;
  void replayWallDamage(const Instance& i, InstanceRT& r);
  void rebuildWalls(const Instance& i, InstanceRT& r);
  void resetDamage();
  static uint8_t panelStateFor(float hp, float maxHp);
  int findPanel(const AssetDef* a, const std::string& id) const;
  bool panelDebug = false;
  bool hideEditorOverlays = false;   // fixed-camera material captures
  Explosion lastExplosion;
  void drawDamageDebug();
  // ---- surface damage & penetration (ballistics.cpp)
  struct DynLeaf { uint64_t inst = 0; int k = 0, l = 0; uint32_t body = 0xffffffffu; std::unique_ptr<GpuMesh> fractured; float age = 0; bool frozen = false; };
  std::vector<std::unique_ptr<DynLeaf>> dynLeaves;
  size_t maxDynLeaves = 24;
  std::vector<Projectile> spawnedProjectiles;      // penetrating continuations, appended after the step
  struct PenetrationInfo { std::string surface; float thickness = 0, eIn = 0, eOut = 0; bool through = false; vec3 entry{0}, exit{0}; };
  PenetrationInfo lastPen;
  int leafBreaks = 0;
  bool bulletImpact(const Projectile& p, const Physics::Hit& hit, std::string& surface);   // true: round continues
  bool locateLeaf(const Instance& i, const InstanceRT& r, int k, vec3 world, int& leaf, bool& onGlass) const;
  void breakGlass(Instance& i, InstanceRT& r, int k, int l, vec3 at);
  void detachLeaf(Instance& i, InstanceRT& r, int k, int l, vec3 impulse, vec3 at, bool fracture);
  void blastLeaves(vec3 p, float radius);
  void stepDynLeaves(float dt);
  void resetLeafDamage();
  void dynLeafDrawItems(std::vector<DrawItem>& op);
  float leafHpFor(const InteractDef& d) const;
  // ---- movement (explore.cpp): data-driven tuning and the posture state machine
  struct MoveSettings {
    float run = 2.3f * 1.25f, sprint = 4.6f * 1.25f, crouch = 1.2f, prone = 0.6f;   // m/s
    float eyeStand = 1.62f, eyeCrouch = 1.05f, eyeProne = 0.32f, eyeRate = 10.0f;    // m, 1/s (camera smoothing)
    float gravity = 9.81f * 0.8f, jumpVelocity = 4.2f * 0.894427f;                  // m/s^2, m/s (player only)
    float proneToStand = 1.1f, proneToCrouch = 0.75f, crouchToStand = 0.45f;        // s, getting up (eye rise; lower speed, no sprint / jump)
    float adsTime = 0.25f;                                                            // s, hip <-> ADS (world FOV, pose, weapon FOV share it)
  } move;
  // getting up: a timed, eased camera rise; until it finishes the player moves at the lower posture's speed
  float riseT = 0, riseDur = 0, riseFrom = 0, riseSpeed = 0;
  // ---- vault (hold Space at a taller obstacle): validated, swept, no chaining (explore.cpp)
  struct VaultSettings { float hold = 0.16f, minH = 0.62f, maxH = 1.35f, reach = 0.85f, maxDepth = 1.1f, maxDrop = 2.2f, speed = 1.0f; } vaultCfg;
  struct VaultState { bool active = false, latch = false; float held = 0, t = 0, dur = 0; vec3 pts[4]{}; std::string why; int count = 0; } vault;
  bool tryStartVault(vec3 feet, float yawDeg);
  void stepVault(float dt);
  void loadMoveSettings();
  int postureWanted = 0;          // Physics::Posture requested by input (stand / crouch / prone)
  static constexpr float kInteractReach = 2.5f;   // m (was 2.0; guns / crates / racks reach + 0.5)
  bool proneToggle = false;       // Z state: prone requested
  // ---- arsenal (arsenal.cpp): bindings, loadout, fire modes, launchers, heavy weapons, racks, AT-gun wrecks
  struct Bindings { int fireMode = 0, prone = 0, crouch = 0, packUp = 0, grenade = 0; std::map<std::string, std::string> names; } binds;
  void loadBindings();
  std::vector<int> carried;
  bool isSmallArm(const WeaponDef& d) const;
  void resetLoadout();
  bool equipWeapon(int idx, std::string& msg);
  const char* fireModeName(const WeaponDef& d, const WeaponState& s) const;
  void toggleFireMode(const WeaponDef& d, WeaponState& s);
  bool deployHeavy(const WeaponDef& d, WeaponState& s);
  bool packUpHeavy();
  void resetStation(const Instance& i);
  void wreckStation(uint64_t id, const std::string& why);
  bool isAtGun(const AssetDef* a) const;
  struct WreckInfo { uint64_t id = 0; std::string why; float time = -100; } lastWreck;
  int stationsWrecked = 0, equips = 0, deploys = 0, packs = 0, rackOpens = 0;
  bool rackOpen = false; uint64_t rackId = 0; int rackSel = 0; std::string lastRackClose;
  std::vector<int> rackArsenal(const std::string& faction) const;
  void rackDrawItems(const Instance& i, const InstanceRT& r);
  void layoutRackDisplays();
  void openRack(uint64_t id);
  void closeRack(const char* why);
  void stepRack(const ExploreInput& in);
  void rackActivate();
  void drawRackUI();
  std::vector<DrawItem> worldBoxDraws;   // box-model weapons on racks
  // ---- buildable defences (builder.cpp)
  struct BuildState { std::string forceAsset; bool active = false, valid = false, snapped = false, fireHeld = true; int item = 1; float yaw = 0, ghostYaw = 0, slopeDeg = 0; vec3 pos{0}; std::string reason; const AssetDef* asset = nullptr; };
  BuildState build;
  int buildPlaced = 0;
  bool skipSlopeRule = false;
  struct BlastInfo { float raw = 0, mult = 1, value = 0, time = -100; };
  BlastInfo lastBlast;
  float blastShake = 0;
  float groundSlopeDeg(vec3 p, vec3* n = nullptr) const;
  bool snapDefense(const AssetDef* a, vec3& pos, float& yaw, int* matchedSocket = nullptr) const;
  void stepBuilder(const ExploreInput& in);
  void builderGhosts(std::vector<DrawItem>& ghosts) const;
  void removeSessionPlacements();
  bool defenseBulletHit(uint64_t iid, vec3 at);
  void destroyDefense(Instance& i, InstanceRT& r);
  // ---- path entities (paths.cpp)
  uint32_t groundRev = 1;   // bumps whenever the terrain changes (craters, resets): paths re-drape
  vec2 pathSize(const Instance& i, const AssetDef* a) const;   // (width, length), clamped
  void setPathSize(Instance& i, vec2 wl);
  void rebuildPathMesh(const Instance& i, InstanceRT& r);
  void blastPaths(vec3 p, float dmg, float radius);
  std::string pathSurfaceAt(vec3 w) const;   // "gravel" / "dirt" / "concrete" / ""
  bool snapPath(const AssetDef* a, vec3& pos, float& yaw) const;
  // ---- hand equipment (equipment.cpp): grenades, knife
  struct LiveGrenade { uint32_t body = 0xffffffffu; float fuse = 0, age = 0; int def = 0; vec3 pos{0}, spawn{0}, v0{0}; quat rot{1, 0, 0, 0}; bool done = false; };
  std::vector<LiveGrenade> grenades;
  uint64_t grenadeSerial = 0;
  int grenadesThrown = 0, grenadesExploded = 0, meleeSwings = 0, meleeHitCount = 0;
  vec3 lastGrenadeSpawn{0}, lastGrenadeWant{0}, lastGrenadeBlast{0}, lastMeleeHit{0}; uint64_t lastMeleeUd = 0;
  std::set<uint64_t> meleeHits;
  int quickReturn = -1; float quickReturnT = -1;   // H quick throw: weapon to return to
  void loadEquipment(WeaponDef& d, const json& j);
  void throwGrenade(const WeaponDef& d, WeaponState& s);
  void stepGrenades(float dt);
  void clearGrenades();
  void grenadeDrawItems();
  void stepEquipment(float dt, const ExploreInput& in, const WeaponDef& d, WeaponState& s, bool fireHeld);
  void meleeSweep(const WeaponDef& d);
  mat4 equipmentPose(const WeaponDef& d, const WeaponState& s) const;
  void rebuildSectionBodies(const Instance& i, InstanceRT& r);   // wire: colliders of the uncut sections
  float traversalFactor(vec3 feet) const;                        // wire with traversal "slow": speed multiplier at the feet
  void blastDefenses(vec3 p, float dmg, float radius);
  float blastOcclusion(vec3 from, vec3 to) const;
  void blastPlayer(vec3 p, float dmg, float radius);
  void drawBuilderUI();
  // ---- trees (foliage.cpp)
  int treesFelled = 0;
  void buildTreeRT(const Instance& i, InstanceRT& r);
  void fellTree(Instance& i, InstanceRT& r, vec3 at, vec3 push);
  bool treeImpact(uint64_t iid, vec3 at, vec3 dir, bool shell);
  void blastTrees(vec3 p, float radius);
  void resetTrees();
  void treeDrawItems(const Instance& i, const InstanceRT& r, std::vector<DrawItem>& op);
  json treeApplyState(const Instance& i, const InstanceRT& r) const;
  // ---- combat (station.cpp / fx.cpp / weapons.cpp)
  Fx fx;
  Grass grass;
  double grassBudgetMs = 2.5;        // per-frame chunk build budget (< 0: build synchronously)
  std::vector<GrassFootprint> grassFootprints() const;
  void updateGrass(vec3 cam);
  void updateSurface();   // ground surface mask (routes, foundations) -> terrain shader + grass
  std::vector<Projectile> projectiles;
  GpuMesh debrisMesh;
  uint64_t occupied = 0, choiceFor = 0;
  std::map<uint64_t, std::map<std::string, int>> crateStock;
  std::map<uint64_t, bool> crateUnlimited;
  int shotsFired = 0;
  ImpactInfo lastImpact;
  void resetCombat();
  void fireProjectile(vec3 from, vec3 dir, float speed, int kind, float dmg, uint64_t ignore, bool tracer, const std::string& ammo);
  void stepProjectiles(float dt);
  void stepStation(float dt, const ExploreInput& in);
  bool occupyStation(uint64_t id, int pos);
  void leaveStation();
  void switchStationPosition();
  StationRT* station(uint64_t id);
  mat4 stationMatrix(const Instance& i, const InstanceRT& r) const;
  void stationParts(const Instance& i, const InstanceRT& r, mat4& base, mat4& yawM, mat4& elevM, mat4& recoilM) const;
  vec3 stationMuzzle(const Instance& i, const InstanceRT& r, vec3& dir) const;
  bool stationCamera(vec3& eye, vec3& fwd, float& fov) const;
  bool carriageFits(const Instance& i, const InstanceRT& r, vec3& pos, float yaw, std::string& why) const;
  std::string crateTransaction(uint64_t crateId);
  std::string resupplyPersonal(uint64_t crateId);
  void drawCombatUI();
  // personal weapons (weapons.cpp)
  void resetWeapons();
  void stepWeapons(float dt, const ExploreInput& in);
  void drawWeaponUI();
  void weaponDrawItems(std::vector<DrawItem>& op);
  bool weaponCamera(float& fov) const;
  void buildWeaponMeshes();
  float viewKickYaw = 0, viewKickPitch = 0;
  std::vector<std::unique_ptr<WeaponDef>> weaponDefs;
  std::map<std::string, std::shared_ptr<GpuModel>> modelCache;
  bool useImportedModels = true;        // --box-weapons: draw the procedural fallback parts instead
  void writeWeaponSheets(const fs::path& outDir);
  bool writeModelPreview(const fs::path& glb, const fs::path& outDir, const fs::path& specJson);   // --model-preview: bake + 4 views
  std::map<std::string, WeaponState> wstate;
  int activeWeapon = 0;
  float adsT = 0;
  bool strictDDay = false, reducedMotion = false, scopeIronsMode = false, supported = false, showLoadout = false;
  std::string weaponMsg;
  vec3 lastShotStart{0}, lastShotDir{0}, lastShotMuzzle{0};
  float lastRecoilScale = 1;      // posture x support factor of the last shot (tests / HUD)
  int weaponIndex(const std::string& id) const;
  void selectWeapon(int idx);
  mat4 viewModelMatrix(const WeaponDef& d, const WeaponState& s) const;
  // first-person handling (viewmodel.cpp)
  ViewModelState vm;
  float worldFov = 72.0f;               // world projection FOV used last frame (apparent-position mapping)
  GpuMesh armMesh[3];                   // unit boxes: US sleeve, German sleeve, skin
  void buildArmMeshes();
  void stepViewModel(float dt, const ExploreInput& in, const WeaponDef& d, const WeaponState& s);
  bool vmScoped(const WeaponDef& d) const;
  mat4 vmLocal(const WeaponDef& d, const WeaponState& s, bool withProcedural = true) const;   // weapon -> view space
  vec3 vmTarget(const WeaponDef& d) const;                                    // spring target (view space)
  void sightLineWorld(const WeaponDef& d, const WeaponState& s, vec3& rear, vec3& front) const;
  vec3 vmApparent(vec3 world) const;    // where a first-person point appears in the world projection
  float vmFovNow() const;               // weapon-projection FOV this frame (vm.vmFov, or the weapon's ADS override)
  void fpCamera(mat4& view, float& worldFovNow) const;   // camera + world FOV the weapon is posed against now
  struct SightMark { vec2 px; uint32_t col; int kind; };   // kind: 0 rear, 1 front tip, 2 muzzle, 3 flash origin, 4 aim
  bool sightMarks(std::vector<SightMark>& out) const;
  bool sightMarkers = false;            // draw the calibration markers (HUD and screenshots)
  void armDrawItems(const mat4& weaponWorld, const WeaponDef& d, const WeaponState& s);
  vec3 lastSightR{0}, lastSightF{0}, lastSightV{0};   // world sight points of the last aimed shot
  vec3 lastHandErr{0};                  // IK residual (right, left) for tests: x = right, y = left
  vec3 lastElbowR{0}, lastElbowL{0}, lastHandR{0}, lastHandL{0}, lastShoulderR{0}, lastShoulderL{0};
  void dofParams(float& focus, float& amount) const;
  vec3 weaponMuzzleWorld(const WeaponDef& d, const WeaponState& s) const;
  bool deformGround(vec3 p, float scale);   // ground.cpp
  bool blastAT = false;                     // the explosion being resolved is an AT shell (deep crater profile)
  float blastSplashGun = 40;                // splash damage to AT guns at the blast centre (falls off to 0 at the radius); guns have 100 HP
  int directHits = 0, splashGunHits = 0;
  // crater tuning (game values): AT-gun craters enlarged in the complete update -- standard HE 1.5 m / 0.5 m -> 2.0 / 0.62
  // (+33 % radius, +24 % depth), AT shot 3.0 / 1.2 -> 3.9 / 1.5 (+30 %, +25 %); the depth : radius ratio (climbable walls) is kept
  static constexpr float kStdCraterD = 0.62f, kStdCraterR = 2.0f, kATCraterD = 1.5f, kATCraterR = 3.9f, kCraterP = 2.0f;
  bool craterAt(vec3 p, float radius, float depth);
  float groundWeight(float x, float z) const;
  void syncGroundTiles(const std::vector<int>& keys);
  void removeGroundBodies();
  void resetGround();
  void applyDamageToMap();
  void clearAppliedDamage();
  void buildDrawLists(std::vector<DrawItem>& op, std::vector<DrawItem>& gl);
  void bump(Instance& i) { i.rev = ++revCounter; dirty = true; }

  bool newMap(const TemplateInfo& t, const std::string& name);
  bool openMap(const fs::path& p);
  bool saveMapTo(const fs::path& p);
  void applyTemplate(const std::string& id);
  const TemplateInfo* findTemplate(const std::string& id) const;
  void makeThumbnails();

  // ---- editor ops
  void pushUndo();
  void undo();
  void redo();
  Snapshot snap() const;
  void restore(const Snapshot& s);
  PlaceCheck checkPlacement(const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId) const;
  uint64_t placeInstance(const AssetDef* a, vec3 pos, float yaw);
  bool duplicateSelected();
  void deleteSelected();
  uint64_t pickInstance(vec3 ro, vec3 rd, float& t) const;
  void cursorRay(vec3& ro, vec3& rd) const;
  bool startMarkerValid(vec3 pos) const;

  void updateCreative(float dt);
  void drawCreativeUI();
  void drawCreativeOverlay(Overlay& ov, std::vector<DrawItem>& ghosts);

  // ---- explore ops
  bool enterExplore();
  void exitExplore();
  void returnToStart();
  void updateExplore(float dt);
  void exploreStep(float dt, const ExploreInput& in);
  void drawExploreUI();
  void toggleInteract(uint64_t inst, int idx);
  bool leafBlocked(const Instance& i, const InteractDef& d, float p) const;
  InteractTarget findTarget(vec3 eye, vec3 dir) const;
  vec3 eyePos() const;
  int placeCompanions(uint64_t hostId);   // bunker crew stations / ammunition placed with their host (docs/bunkers.md)
  static vec3 forward(float yawDeg, float pitchDeg);

  void screenshot(const fs::path& p);
  Recorder rec; std::string recCaption; int recTick = 0; std::vector<uint8_t> recPx;   // --record-range
  fs::path uiShot;   // when set, the next frame including UI is written here
};

extern App* gApp;
