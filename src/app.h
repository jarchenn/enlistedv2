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
  float lastRebuildMs = 0;
  StationRT st;                                   // crew-served station overlay (if the asset is a station)
  std::vector<std::vector<uint8_t>> leafSt;       // [interactable][leaf] LEAF_* flags (session overlay)
  std::vector<std::vector<float>> leafHits;       // bullet hits taken
  float treeHits = 0; bool treeFelled = false, trunkApplied = false; uint32_t trunkBody = 0xffffffffu;   // trees (foliage.cpp)
  float defHp = 0; bool defDestroyed = false;       // defences (builder.cpp)
};

struct ExploreInput {
  vec2 move{0}; bool sprint = false, crouch = false, jump = false, interact = false; float dyaw = 0, dpitch = 0;
  bool fire = false, aim = false, reload = false, switchPos = false, choose1 = false, choose2 = false, nextWeapon = false, toggleAmmo = false, build = false; float wheel = 0, lookYaw = 0, lookPitch = 0;
};

struct Snapshot { std::vector<Instance> inst; StartMarker start; std::set<std::string> removedVeg; uint64_t selected = 0; std::map<int, std::vector<float>> terrain; };

struct PlaceCheck { bool ok = true; std::string reason; float y = 0; };

struct InteractTarget { bool valid = false; uint64_t inst = 0; int idx = -1; float t = 0; int kind = 0; /*0 door/window, 1 station, 2 crate*/ };
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
  Explosion explode(vec3 p, vec3 dir, float damage, float radius, float craterScale, uint64_t ignoreUd = ~0ull);
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
  // ---- buildable defences (builder.cpp)
  struct BuildState { bool active = false, valid = false, snapped = false, fireHeld = true; int item = 1; float yaw = 0, ghostYaw = 0, slopeDeg = 0; vec3 pos{0}; std::string reason; const AssetDef* asset = nullptr; };
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
  std::map<std::string, WeaponState> wstate;
  int activeWeapon = 0;
  float adsT = 0;
  bool strictDDay = false, reducedMotion = false, scopeIronsMode = false, supported = false, showLoadout = false;
  std::string weaponMsg;
  vec3 lastShotStart{0}, lastShotDir{0}, lastShotMuzzle{0};
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
  void armDrawItems(const mat4& weaponWorld, const WeaponDef& d, const WeaponState& s);
  vec3 lastSightR{0}, lastSightF{0}, lastSightV{0};   // world sight points of the last aimed shot
  vec3 lastHandErr{0};                  // IK residual (right, left) for tests: x = right, y = left
  vec3 lastElbowR{0}, lastElbowL{0}, lastHandR{0}, lastHandL{0}, lastShoulderR{0}, lastShoulderL{0};
  void dofParams(float& focus, float& amount) const;
  vec3 weaponMuzzleWorld(const WeaponDef& d, const WeaponState& s) const;
  bool deformGround(vec3 p, float scale);   // ground.cpp
  bool blastAT = false;                     // the explosion being resolved is an AT shell (deep crater profile)
  static constexpr float kStdCraterD = 0.5f, kStdCraterR = 1.5f, kATCraterD = 1.2f, kATCraterR = 3.0f, kCraterP = 2.0f;
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
  static vec3 forward(float yawDeg, float pitchDeg);

  void screenshot(const fs::path& p);
  Recorder rec; std::string recCaption; int recTick = 0; std::vector<uint8_t> recPx;   // --record-range
  fs::path uiShot;   // when set, the next frame including UI is written here
};

extern App* gApp;
