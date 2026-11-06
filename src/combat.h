#pragma once
#include "assets.h"
#include "model.h"

// ---------------------------------------------------------------- personal weapons
struct WeaponDef {
  std::string id, name, side, action, feed, container, caliber, note, reticle;
  int cap = 1, loaded = 1, reserve = 0, containerSize = 1, tracerEvery = 0;
  float rpm = 60, velocity = 800, zero = 100, boltTime = 0, reloadEmpty = 2.5f, reloadPartial = 2.5f, reloadRound = 0.5f;
  float rPitch = 1, rYaw = 0.4f, rRecover = 8 /*unused: recoil no longer recovers*/, rClimb = 0, rVisual = 0.05f, rSupported = 1;
  vec3 rear{0}, front{0}, muzzle{0}, scopeEye{0};
  bool hasScope = false, scopeIrons = false, laterWar = false;
  float scopeMag = 1;
  std::vector<Material> mats;
  GpuMesh mesh[8];
  // imported textured model (content/weapons/models/<dir>); the box parts above stay as the fallback
  std::shared_ptr<GpuModel> model;
  std::vector<int> modelParts[8];       // model part indices per WPart role
  fs::path file;                        // source JSON (calibration panel saves back into it)
  vec3 gripR{0, -0.06f, 0.30f}, gripL{0, -0.02f, 0.62f};   // IK hand sockets (weapon space)
  float bulletMass = 10;                // grams (penetration energy)
  std::vector<std::string> modes;       // fire-selector modes, first is the default ("auto", "semi")
  std::string deploys;                  // heavy weapon: station asset it deploys as
  float rocketDmg = 0, rocketR = 0, rocketCrater = 0, rocketSplashGun = 0;   // launcher rocket tuning (data)
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
                      linDamp = 0.4f, rollDamp = 3.0f; } grenade;
  struct MeleeDef { bool on = false; float reach = 1.7f, arcDeg = 40; int rays = 5; float windup = 0.12f, strike = 0.1f, cooldown = 0.6f, damage = 35; } melee;
  bool equipment() const { return grenade.on || melee.on; }
  bool useModel() const { return model && model->vao; }
};
struct WeaponState {
  int loaded = 0;                 // rounds in the weapon, chamber included
  std::vector<int> containers;    // reserve: clips / magazines / belt segments, each with its own count
  int reserveMax = 0;
  float cooldown = 0, bolt = 0, reload = 0, reloadTotal = 0;
  int reloadKind = 0;             // 1 empty, 2 partial (retain), 3 single rounds
  bool triggerReleased = true, clicked = false;
  float recoilP = 0, recoilY = 0, kick = 0;
  float sinceShot = 99;           // s since the last shot (a burst ends, and its recoil is folded into the aim, once firing has stopped)
  int shotIdx = 0, burst = 0, clipPings = 0;
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
  bool tracer = false;
  std::string ammo;
};
struct Particle { vec3 p{0}, v{0}; float life = 0, maxLife = 1, size = 0.3f, grow = 0, drag = 0, lift = 0; vec4 col{1}; bool additive = false; };
struct Streak { vec3 a{0}, b{0}; float life = 0, maxLife = 0.05f, width = 0.02f; vec4 col{1}; };
struct Debris { vec3 p{0}, v{0}, w{0}, size{0.1f}; quat r{1, 0, 0, 0}; float life = 0; bool resting = false; };
struct FxVert { vec3 p; vec4 c; vec2 uv; };

class Terrain;
struct Fx {
  std::vector<Particle> parts;
  std::vector<Streak> streaks;
  std::vector<Debris> debris;
  size_t maxParts = 900, maxStreaks = 160, maxDebris = 72;
  bool rangeMode = true;          // range presentation: every trajectory clearly visible
  void update(float dt, const Terrain& t);
  void clear() { parts.clear(); streaks.clear(); debris.clear(); }
  void smoke(vec3 p, vec3 v, float size, float life, vec4 col, float grow = 1.2f, float lift = 0.6f);
  void flash(vec3 p, float size, float life, vec4 col);
  void streak(vec3 a, vec3 b, float width, float life, vec4 col);
  void explosion(vec3 p, vec3 n, const std::string& family, bool ground);
  void muzzle(vec3 p, vec3 dir, float scale);
  void impact(vec3 p, vec3 n, const std::string& surface);
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
  int shots = 0;
  float cool = 0;
};
