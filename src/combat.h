#pragma once
#include "assets.h"
#include "model.h"

// ---------------------------------------------------------------- personal weapons
// handling class (content/weapons/handling.json "classes"): picks the hip / ADS dispersion cone
enum WeaponClass : int { WC_RIFLE = 0, WC_SNIPER = 1, WC_AUTO = 2, WC_LMG = 3 };
// firing stance of the local player, best first by priority: mounted > prone > crouched > sprinting > moving > standing
enum Stance : int { STANCE_MOVING = 0, STANCE_STANDING, STANCE_CROUCHED, STANCE_PRONE, STANCE_MOUNTED, STANCE_SPRINTING, STANCE_COUNT };
struct Handling {   // content/weapons/handling.json (defaults = the file's values)
  float hip[4] = {0.15f, 0.15f, 3.0f, 3.0f}, ads[4] = {0.03f, 0.03f, 0.45f, 0.45f};   // cone half-angle (deg) by WeaponClass
  float stance[STANCE_COUNT] = {1.0f, 0.8f, 0.4f, 0.08f, 0.05f, 1.6f};               // cone multiplier by Stance
  float recoil[STANCE_COUNT] = {1.0f, 1.0f, 0.85f, 0.6f, 0.5f, 1.0f};                // recoil multiplier by Stance
  float movingYaw = 0.25f;   // share of the weapon's horizontal recoil while moving (still: pitch only)
  float cone(int wclass, bool ads, int st) const { return (ads ? this->ads[wclass] : hip[wclass]) * stance[st]; }
};
// 0 untraced, 1 red tracer (every `every`-th round), 2 the short white-yellow streak of the guns' other rounds
inline int tracerKindFor(int every, int shot) { return every <= 0 ? 0 : shot % every == 0 ? 1 : 2; }
struct WeaponDef {
  std::string id, name, side, action, feed, container, caliber, note, reticle;
  int cap = 1, loaded = 1, reserve = 0, containerSize = 1, tracerEvery = 0;
  float rpm = 60, velocity = 800, zero = 100, boltTime = 0, reloadEmpty = 2.5f, reloadPartial = 2.5f, reloadRound = 0.5f;
  float rPitch = 1, rYaw = 0.4f, rRecover = 8 /*unused: recoil no longer recovers*/, rClimb = 0, rVisual = 0.05f, rSupported = 1;
  vec3 rear{0}, front{0}, muzzle{0}, scopeEye{0};
  bool hasScope = false, scopeIrons = false, laterWar = false;
  bool tool = false;     // GM3 "tool": "build": the Engineer's hammer (never fires; the build tool in a match, builder.cpp)
  bool locked = false;   // GM3 "locked": fixed in its slot (never dropped, swapped at a rack or replaced by a pickup)
  float sway = 1;                       // MG6: JSON "sway": breathing sway x this while standing (crouched / prone: x1; the .50)
  bool modern = false;                  // MG1: JSON "modern" (side "modern"): only from the modern weapons case, never in 1944 loadouts / classes / bot pools
  int pellets = 1; float spreadDeg = 0;  // MG1: a shot fires `pellets` rounds, the extra ones inside a cone of half-angle spreadDeg (shotgun)
  float scopeMag = 1, scopeRelief = 0.07f;   // scope.relief: eye to the ocular lens in the scoped pose (m)
  vec3 boltPivot{0, 0.028f, 0}; float boltLift = 70;   // bolt action: handle lift about the z-parallel axis through the pivot (deg)
  std::vector<Material> mats;
  GpuMesh mesh[8];
  // imported textured model (content/weapons/models/<dir>); the box parts above stay as the fallback
  std::shared_ptr<GpuModel> model;
  std::shared_ptr<struct AnimVM> anim;   // MG1: animated first-person rig (arms + gun + clips, vm_anim.h); null = the static path
  float reloadInsert = 0;                // MG1: animated reloads move the rounds at this fraction of the clip (0 = at its end)
  float reloadStart = 0, reloadEnd = 0;  // MG2 shell loop (single feed): the start before the first round, the end after the last
  std::vector<int> modelParts[8];       // model part indices per WPart role
  // MG4: the third-person / world model: the bake's lod1 (the reduced mesh) when the weapon has one, else the model
  std::shared_ptr<GpuModel> modelLod; std::vector<int> modelPartsLod[8];
  const GpuModel* worldModel() const { return modelLod ? modelLod.get() : model.get(); }
  const std::vector<int>* worldParts() const { return modelLod ? modelPartsLod : modelParts; }
  fs::path file;                        // source JSON (calibration panel saves back into it)
  vec3 gripR{0, -0.06f, 0.30f}, gripL{0, -0.02f, 0.62f};   // IK hand sockets (weapon space)
  vec3 coverPivot{0, 0.065f, 0.58f}, bipodPivot{0, 0.02f, 1.1f};   // MG9 belt guns: top-cover hinge and bipod hinge (weapon space, JSON "pivots")
  float coverDeg = -75, bipodDeg = -80;                              // rotation about +x when open / deployed
  float bulletMass = 10;                // grams (penetration energy)
  std::vector<std::string> modes;       // fire-selector modes, first is the default ("auto", "semi")
  std::string deploys;                  // heavy weapon: station asset it deploys as
  float rocketDmg = 0, rocketR = 0, rocketCrater = 0, rocketSplashGun = 0;   // launcher rocket tuning (data)
  float rocketGrav = 0.35f; bool rocketAtKill = true, rocketBackblast = true;   // MG4: "gravity" (x g; a 40 mm grenade arcs at 1), a direct hit kills an AT gun, backblast
  // per-weapon ADS calibration (JSON "ads", docs/fps-weapons.md#calibration). rear / front / muzzle above are the
  // measured anchors on the mesh; these refine the pose without touching the mesh.
  float eyeRelief = 0.21f;              // m, eye to rear sight in ADS (irons); smaller = larger sight picture
  float adsVmFov = 0;                   // deg, weapon-projection FOV while aiming (0 = keep the fixed vmFov)
  vec3 adsPos{0};                       // view-space translation correction in ADS (m)
  vec3 adsRot{0};                       // view-space rotation correction about the rear sight (deg: pitch, yaw, roll)
  vec3 hipOffset{0.12f, -0.07f, 0.06f}; // view-space offset of the hip pose from the ADS line (m; JSON ads.hipOffset)
  float adsWorldFov = 58;               // deg, world FOV at full ADS (irons / launchers; JSON ads.worldFov)
  bool sightLineOnly = false;           // JSON ads.virtualLine: rear/front are a view line beside the weapon (launcher
                                        // aimed past its shield), so aimed rounds leave the muzzle, not the "front sight"
  vec3 muzzleDir{0, 0, 1};              // bore axis at the muzzle (weapon space)
  // hand equipment (equipment.cpp; JSON "grenade" / "melee", game tuning)
  struct GrenadeDef { bool on = false; float fuse = 4.5f, speed = 15, lobDeg = 10, restitution = 0.3f, friction = 0.6f, mass = 0.6f,
                      damage = 80, radius = 5, crater = 0.12f, windup = 0.32f, ready = 0.9f, volume = 0.8f,
                      linDamp = 0.4f, rollDamp = 3.0f; bool smoke = false, fire = false; } grenade;   // smoke: no blast, starts a smoke cloud (mpai)
  struct MeleeDef { bool on = false; float reach = 1.7f, arcDeg = 40; int rays = 5; float windup = 0.12f, strike = 0.1f, cooldown = 0.6f, damage = 35; } melee;
  // GM2 explosion pack (JSON "pack"; docs/abilities.md): placed on the surface in reach, goes off `fuse` s later
  struct PackDef { bool on = false; float fuse = 10, damage = 450, radius = 6, crater = 0.4f, reach = 2.2f;
                   bool hasLed = false; vec3 led{0}; } pack;   // MG10: the model's LED (weapon space) blinks while the fuse runs
  bool equipment() const { return grenade.on || melee.on || pack.on; }
  bool useModel() const { return model && model->vao; }
  int wclass = WC_RIFLE;                // handling.json "classes" (else derived from action / feed / scope)
  float adsSpeed = 1;                   // JSON ads.speed: ADS blend rate x this
  bool magReload = false;               // JSON "reloadStyle":"magazine": an en-bloc rifle reloads with the magazine-style hand motion (support hand to the magwell, no handle pull)
  float moveSpeed = 1;                  // GM1: JSON "moveSpeed": player speed x this while the weapon is in hand (knife 1.3)
  bool showArms = true;                 // JSON "arms": false hides the placeholder first-person arms (until a weapon-specific arm model lands)
  float adsZoom = 0;                    // JSON ads.zoom (> 0): ADS only narrows the world FOV by this fraction; the weapon keeps its hip pose, no sight alignment (flamethrower)
  // flamethrower (FT1, action "flame", JSON "flame"; docs/flamethrower.md): game tuning, not real data
  struct FlameDef { bool on = false; float fuelSeconds = 20, range = 22, speed = 30, coneDeg = 3.5f, parcelsPerSec = 30, parcelDamage = 3.5f,
                    burnSeconds = 3, burnDps = 14, patchSeconds = 3, patchRadius = 1.3f, patchDps = 12, botRange = 18, botBurst = 1.6f, botPause = 1.4f; } flame;
  int tracerKind(int shot) const { return tracerKindFor(tracerEvery, shot); }
  // WP4 visual recoil (handling.json "vmRecoil": per kind, per-weapon overrides): the peaks of a single shot's
  // critically damped spring (back along the bore into the shoulder m, muzzle up deg, leftward deg), its rate omega
  // (1/s: peak at 1 / omega, settled ~5 / omega) and the back travel cap (eye relief: never into the camera)
  float vmBack = 0.035f, vmPitch = 1.6f, vmYaw = 0.35f, vmOmega = 28, vmBackMax = 0.07f, flashScale = 1;
  bool suppressed = false;              // a built-in suppressor: a tiny flash, no light
};
struct WeaponState {
  int loaded = 0;                 // rounds in the weapon, chamber included
  std::vector<int> containers;    // reserve: clips / magazines / belt segments, each with its own count
  int att[4] = {0, 0, 0, 0};      // BR2 attachments (loot.h ATT_*): sight 1 red dot 2 ACOG 3 4x 4 10x; muzzle 1 suppressor 2 brake; mag 1 extended; stock 1 recoil pad
  int reserveMax = 0;
  float cooldown = 0, bolt = 0, reload = 0, reloadTotal = 0;
  int reloadKind = 0;             // 1 empty, 2 partial (retain), 3 single rounds
  bool triggerReleased = true, clicked = false;
  bool magOff = false;            // WP5: the magazine left the gun mid-reload (cancelled after mag-out): off until a reload completes
  float recoilP = 0, recoilY = 0, kick = 0;
  float kickV = 0, kickP = 0, kickPV = 0, kickY = 0, kickYV = 0;   // WP4 spring: kick (back, m) and its rate, muzzle up / left (deg) and rates
  float sinceShot = 99;           // s since the last shot (a burst ends, and its recoil is folded into the aim, once firing has stopped)
  int shotIdx = 0, burst = 0, clipPings = 0;
  float flameFrac = 0;            // flamethrower: fuel used since the last whole percent
  int mode = 0;                   // index into WeaponDef::modes (AUTO first); reset with the session
  float throwT = -1;              // grenade wind-up / knife swing progress (s), -1 = idle
  bool struck = false;            // knife: this swing's sweep has happened
  uint32_t seed = 1;
  int reserveRounds() const;
};

// ---------------------------------------------------------------- projectiles & effects (pooled)
enum ProjKind : int { PROJ_BULLET = 0, PROJ_HE = 1, PROJ_ROCKET = 2 };
struct Projectile {
  vec3 p{0}, v{0};
  float life = 0, dmg = 0;
  float mass = 0.01f;           // kg (small arms: penetration energy E = m v^2 / 2)
  int kind = PROJ_BULLET;
  int pens = 0;                 // surfaces already passed through
  bool at = false;              // anti-tank shell: deep crater profile, concentrated wall damage
  bool atKill = false;          // a direct hit destroys an AT gun in one shot (PaK shells, rockets)
  float blastDmg = 0, blastR = 0, craterScale = 1, splashGun = 40, gravScale = 1;
  uint64_t ignore = ~0ull;      // body user data to ignore (the firing gun)
  int owner = -1;               // firing soldier's roster slot (multiplayer + AI); -1 = the local player
  int tracerKind = 0;           // tracerKindFor(): 0 none, 1 red tracer, 2 white-yellow streak
  bool extraPellet = false;     // MG1: a shotgun's 2nd..nth pellet (a LAN client sends one request per shot; the host fires them all)
  std::string ammo;
  float travelled = 0;          // m flown so far (W1 damage falloff: the true range)
  bool cannon30 = false;        // VH5 helicopter chin gun: an HE round that counts as small arms on tanks, as a launcher on cars
};
struct Particle { vec3 p{0}, v{0}; float life = 0, maxLife = 1, size = 0.3f, grow = 0, drag = 0, lift = 0; vec4 col{1}; bool additive = false; float fade = 0;
                  float flame = -1; };   // flame >= 0 (additive): a flame sprite, id 0..255 = its rotation and noise seed; col.x = brightness, the colour follows its age (Fx::build) and the fragment shader erodes it with noise   // fade > 0: alpha x (life left)^fade (firing smoke, content/settings/fx.json)
struct Streak { vec3 a{0}, b{0}; float life = 0, maxLife = 0.05f, width = 0.02f; vec4 col{1}; };
struct Debris { vec3 p{0}, v{0}, w{0}, size{0.1f}; quat r{1, 0, 0, 0}; float life = 0; bool resting = false; };
struct FxVert { vec3 p; vec4 c; vec2 uv; };

class Terrain;
struct Fx {
  std::vector<Particle> parts;
  std::vector<Streak> streaks;
  std::vector<Debris> debris;
  size_t maxParts = 1500, maxStreaks = 160, maxDebris = 72;
  void setBudget(float k);        // settings: caps = the defaults above x k (Low 0.6, Medium 1, High 1.5)
  bool rangeMode = true;          // range presentation: every trajectory clearly visible
  void update(float dt, const Terrain& t);
  void clear() { parts.clear(); streaks.clear(); debris.clear(); }
  void bullet(vec3 a, vec3 b, int tracerKind);   // one flight segment of a round, drawn by its tracer kind
  void smoke(vec3 p, vec3 v, float size, float life, vec4 col, float grow = 1.2f, float lift = 0.6f);
  void flash(vec3 p, float size, float life, vec4 col);
  void streak(vec3 a, vec3 b, float width, float life, vec4 col);
  void explosion(vec3 p, vec3 n, const std::string& family, bool ground);
  void muzzle(vec3 p, vec3 dir, float scale);   // tank / AT / Flak gun discharge (content/settings/fx.json "muzzle")
  // WP4 small-arms muzzle flash, once per shot: a yellow-orange core and 4-6 orange flame petals along the bore
  // (20-40 ms, randomised), plus a warm point light at `light` (world muzzle; the player's flash is drawn at the
  // viewmodel's apparent muzzle). suppressed: a faint core, no light
  void muzzleFlash(vec3 p, vec3 dir, float scale, bool suppressed, vec3 light);
  struct MuzzleLight { vec3 p{0}; float age = 0, life = 0.035f, k = 1; uint32_t id = 0; };
  std::vector<MuzzleLight> mlights; uint32_t mlightSerial = 0;
  // WP4b: handling.json "muzzleFlash" (HDR linear colours; the light's radius m and intensity)
  struct FlashLook { vec3 core{5.5f, 2.9f, 0.45f}, petal{4.6f, 1.45f, 0.18f}, edge{2.6f, 0.55f, 0.06f}, light{1.0f, 0.48f, 0.14f}; float radius = 3.2f, intensity = 4.5f; } flashLook;
  void backblast(vec3 p, vec3 back);              // launcher backblast (fx.json "backblast")
  void impact(vec3 p, vec3 n, const std::string& surface);
  void splinters(vec3 p, vec3 v, int n, vec4 col, float size);   // furniture breaks (V4): falling chips
  void dust(vec3 p, float radius, vec4 col);                     // furniture blown apart (V4): low slow dust
  void build(const mat4& view, std::vector<FxVert>& alpha, std::vector<FxVert>& add) const;
  uint32_t rng = 1234567u;
  float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng % 100000u) / 100000.0f; }
};

// ---------------------------------------------------------------- crew-served station runtime (Explore overlay)
enum StationPos : int { ST_NONE = 0, ST_GUNNER = 1, ST_TO_MOVE = 2, ST_HANDLER = 3, ST_DEPLOYING = 4 };
struct StationRT {
  bool active = false;
  vec3 pos{0}; float carYaw = 0;          // current (possibly moved) carriage transform
  float yaw = 0, elev = 0, yawT = 0, elevT = 0;
  float recoilT = 9;                      // seconds since last discharge
  int loaded = 0, reserve = 0, reserveMax = 0;
  int reserveAT = 0, reserveATMax = 0; bool loadedAT = false, selAT = false;   // PaK 40: AT rounds beside HE
  bool wrecked = false; float hp = 100;   // AT-gun destruction (one idempotent transition)
  bool semi = false;                      // M2HB fire selector (AUTO default)
  float reload = 0;                       // remaining reload seconds (0 = idle)
  int pos2 = ST_NONE;                     // occupant position
  float trans = 0;                        // remaining transition seconds
  bool deployed = true, armed = false, atYawStop = false, atElevStop = false, moveOk = true;
  std::string status;
  int shots = 0, burst = 0;               // burst: rounds since the trigger was pressed (MG 42 tail on release)
  float cool = 0;
};
