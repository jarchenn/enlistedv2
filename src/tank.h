#pragma once
// Phase 2 tanks: Sherman Firefly stand-in and Tiger I built from the two supplied GLBs (docs/tanks.md).
// Data:    content/vehicles/<id>.json (bake spec, rig sockets, colliders, gun / ammunition / roof MG, mobility, durability)
// Catalog: content/assets/tank_<id>.json (placeable entity, "vehicle": "<id>")
// Runtime: everything here is an Explore session overlay, like the crew-served stations; applied damage (wreck,
//          durability) lives in Instance::extra["tank"] and follows the Apply / undo rules.
#include "common.h"
#include "model.h"

struct AssetDef;
struct Instance;
struct InstanceRT;
struct Projectile;
class App;
struct ExploreInput;

bool importVehicleModels(const fs::path& srcDir, const fs::path& contentRoot);   // tank_import.cpp
bool postProcessVehicleModel(const fs::path& dir, const json& spec, std::string& report);
bool isVehicleAsset(const AssetDef* a);
// T7 thin-glass rule (tank_sim.cpp): a shell / rocket striking the GLASS of a closed window breaks it and continues
// (bounded); frames, shutters, doors and walls still detonate it. Open / broken apertures are empty space already.
bool shellThroughGlass(App& app, Projectile& p, vec3 hitPos, uint64_t hitUd);
class Audio;
void buildTankSounds(Audio& au);                 // tank_ui.cpp: temporary synthesized engine / track / cannon sounds

// Struck zone in the frame of the component hit (hull or turret).
enum TankZone : int { TZ_FRONT = 0, TZ_SIDE = 1, TZ_REAR = 2, TZ_ROOF = 3, TZ_BELLY = 4 };
const char* tankZoneName(int z);
// Attack classes for the damage matrix.
enum TankAttack : int { TA_SMALL_ARMS = 0, TA_CANNON = 1, TA_LAUNCHER = 2 };   // cannon = tank gun, AT gun, Flak

struct TankBox { vec3 c{0}, h{0.5f}; };
struct TankDef {
  std::string id, name, faction, identity;
  json raw;
  std::shared_ptr<GpuModel> model;
  std::vector<int> hullParts, turretParts, gunParts, hatchParts, mgParts, trackParts[2], wheelParts[2];   // [0] left (-x), [1] right (+x)
  // rig (hull space, metres, turret at 0 deg, gun level)
  vec3 turretPivot{0}, gunPivot{0}, muzzle{0}, optic{0}, hatchEye{0}, mgPivot{0}, mgMuzzle{0}, engine{0};
  std::vector<vec3> exhausts, exits;
  std::vector<TankBox> hullBoxes, turretBoxes;   // colliders: hull space, turret boxes at 0 deg
  TankBox armourHull, armourTurret;              // zone classification volumes (hull / turret frame at 0 deg)
  // main gun
  float traverseRate = 24, traverseAccel = 36, elevRate = 6, elevAccel = 18, elevMin = -5, elevMax = 20, recoil = 0.3f;
  float fov = 18, fovZoom = 8;                   // gunner sight field (deg): base / RMB magnification
  float reloadTime = 6.0f, velAP = 880, velHE = 600, dmgHE = 140;
  int reserveHE = 20, reserveAP = 30; std::string startLoaded = "AP", ammoHE, ammoAP, cannonName, reticle;
  float zeroRange = 400;                         // m, game zero of the sight
  // roof MG (hatch mode)
  std::string mgWeapon, mgName, mgSound;         // mgWeapon: registry weapon whose supplied GLB is drawn on the mount
  vec3 mgModelOffset{0};
  float mgYawMin = -180, mgYawMax = 180, mgElevMin = -10, mgElevMax = 45, mgRpm = 500, mgVel = 890, mgSpread = 0.25f;
  int mgBelt = 100, mgReserve = 400; float mgReload = 5.0f, mgBulletMass = 0.012f;
  // mobility (gameplay profile; historical road speed carried for the docs / HUD only)
  float maxFwd = 9, maxRev = 3, accel = 2, brake = 5, coast = 0.8f, turnRate = 28, pivotRate = 20, maxSlopeDeg = 30, offroadMul = 0.85f;
  float histRoadKmh = 0; std::string histSource;
  vec3 halfSize{1.5f, 1.4f, 3.0f};               // hull half extents (sweep / crush / player overlap)
  float clearance = 0.4f;                        // ground clearance of the sweep box (m)
  float trackHalfGauge = 1.2f, trackHalfLen = 2.4f;
  // durability (user rule): survives N front hits and M side / rear hits. HP 100; front hit = 100 / (N + 1), side = 100 / (M + 1)
  int frontSurvive = 4, sideSurvive = 1;
  int frontDamage() const { return 100 / (frontSurvive + 1); }
  int sideDamage() const { return 100 / (sideSurvive + 1); }
};

enum TankStation : int { TS_GUNNER = 0, TS_HATCH = 1 };
enum TankShell : int { SHELL_NONE = 0, SHELL_HE = 1, SHELL_AP = 2 };
struct TankRT {
  uint64_t id = 0; uint32_t rev = 0;
  const TankDef* def = nullptr;
  // pose (yaw: asset convention, +z forward at 0)
  vec3 pos{0}; float yaw = 0, pitch = 0, roll = 0;
  float speed = 0, yawRate = 0, trackPhase[2] = {0, 0}, suspPitch = 0, suspRoll = 0;
  float throttleIn = 0, steerIn = 0;
  // turret / gun relative to the hull: achieved angle and rate
  float tYaw = 0, tYawV = 0, gElev = 0, gElevV = 0, recoilT = 9;
  float mgYaw = 0, mgElev = 0, mgCool = 0, mgReloadT = 0; int mgLoaded = 0, mgReserve = 0, mgShots = 0;
  float hatchT = 0;   // 0 closed (gunner) .. 1 open (head out)
  int station = TS_GUNNER;
  // ammunition state machine (docs/tanks.md#ammunition)
  int reserveHE = 0, reserveAP = 0;
  int loaded = SHELL_NONE;            // chambered round
  int selected = SHELL_AP;            // next / in-progress choice
  float reload = 0;                   // remaining seconds of the running load (0 = idle)
  int shots = 0, loadsCompleted = 0, toggles = 0;
  // damage
  int hp = 100; bool wrecked = false; int hitsFront = 0, hitsSide = 0, hitsIgnored = 0;
  float burnT = 0;
  // engine / effects
  float rpm = 0.2f, exhaustAcc = 0, dustAcc = 0; int engineLoop = -1, trackLoop = -1, turretLoop = -1;
  // physics
  uint32_t body = 0xffffffffu; float shapeYaw = 1e9f;
  std::string blockedBy; int crushes = 0;
};

struct TankHitResult { bool tank = false, counted = false, consumed = false, destroyed = false; int zone = -1, attack = 0; uint64_t id = 0; std::string info; };

class TankSys {
public:
  std::map<std::string, std::unique_ptr<TankDef>> defs;
  std::map<uint64_t, TankRT> tanks;
  uint64_t occupied = 0;              // tank the player is in (0 = on foot)
  uint64_t target = 0;                // tank under the crosshair within reach (Enter prompt)
  bool ctrlPrev = false, fireArmed = true, zoom = false, crouchLatch = false, aimPrev = false;
  float zoomT = 0;                    // 0 base sight field .. 1 RMB magnification (eased)
  double lastStepTime = -1e9;
  double stepMs = 0, stepMsMax = 0;   // CPU time of stepAll (all tanks), last step / running max (indicative)         // app.now of the occupant's last fixed step (gap = pause / focus change)
  std::string msg; double msgUntil = 0;
  TankHitResult lastHit;
  int exits = 0, entries = 0, blockedExits = 0, wrecksMade = 0, hatchToggles = 0;
  std::string lastExitWhy;

  void loadDefs(App& app);                       // content/vehicles/*.json + baked models (needs GL)
  const TankDef* defFor(const AssetDef* a) const;
  void makeThumbnails(App& app);
  static void stripDamage(json& extra);          // duplicate: copy the spawn configuration, never the damage history
  bool writePreview(App& app, const fs::path& outDir);   // --tank-preview: 4 views per tank, rig sockets marked
  // session
  void reset(App& app);                           // Explore entry / exit: authored state
  void sync(App& app);                            // instances added / removed / edited
  TankRT* get(uint64_t id) { auto it = tanks.find(id); return it == tanks.end() ? nullptr : &it->second; }
  void stepAll(App& app, float dt, const ExploreInput& in);
  bool handleInteract(App& app, const ExploreInput& in);   // E on a tank: enter (true = consumed)
  bool enter(App& app, uint64_t id);
  bool exitTank(App& app, bool force = false);
  bool camera(const App& app, vec3& eye, vec3& fwd, float& fov) const;
  void drawItems(App& app, const Instance& i, const InstanceRT& r);
  void drawUI(App& app);
  void drawEditorPanel(App& app, Instance& inst);
  void ghostItems(App& app, const AssetDef* a, const mat4& M, vec4 tint);   // Creative placement preview (tinted model)   // Creative selection panel (undoable edits of extra["tank"])
  // combat
  TankHitResult projectileHit(App& app, const Projectile& p, vec3 pos, vec3 normal, uint64_t ud);
  TankHitResult applyHit(App& app, uint64_t id, int attack, vec3 worldPos, vec3 worldNormal, vec3 dir);
  int classify(const TankRT& t, vec3 worldPos, vec3 worldNormal, bool& onTurret) const;
  void destroy(App& app, TankRT& t, const std::string& why);
  // persistence (Apply session changes)
  void applyToMap(App& app);
  // rig helpers (world space)
  mat4 hullMatrix(const TankRT& t) const;
  void rig(const TankRT& t, mat4& hull, mat4& turret, mat4& gun, mat4& recoil) const;
  vec3 muzzleWorld(const TankRT& t, vec3& dir) const;
  mat4 mgMatrix(const TankRT& t) const;
  vec3 mgMuzzleWorld(const TankRT& t, vec3& dir) const;
  // simulation helpers (also used by the self-test)
  void stepOne(App& app, TankRT& t, float dt, const ExploreInput* in);
  void drive(App& app, TankRT& t, float dt);
  void aimGun(TankRT& t, float dt, bool active);
  void fireCannon(App& app, TankRT& t);
  void fireMg(App& app, TankRT& t);
  void stepAmmo(App& app, TankRT& t, float dt, bool togglePressed);
  void updateBody(App& app, TankRT& t, bool force);
  bool exitSpot(App& app, const TankRT& t, vec3& out, std::string& why) const;
  void effects(App& app, TankRT& t, float dt);
  void toast(App& app, const std::string& s, double secs = 3.0);
  void groundPose(App& app, const TankDef& d, vec3 pos, float yaw, float& y, float& pitch, float& roll, float& slopeAlong) const;
};
