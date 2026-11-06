#pragma once
#include "assets.h"
#include "model.h"

// ---------------------------------------------------------------- personal weapons
struct WeaponDef {
  std::string id, name, side, action, feed, container, caliber, note, reticle;
  int cap = 1, loaded = 1, reserve = 0, containerSize = 1, tracerEvery = 0;
  float rpm = 60, velocity = 800, zero = 100, boltTime = 0, reloadEmpty = 2.5f, reloadPartial = 2.5f, reloadRound = 0.5f;
  float rPitch = 1, rYaw = 0.4f, rRecover = 8, rClimb = 0, rVisual = 0.05f, rSupported = 1;
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
  int shotIdx = 0, burst = 0, clipPings = 0;
  uint32_t seed = 1;
  int reserveRounds() const;
};

// ---------------------------------------------------------------- projectiles & effects (pooled)
enum ProjKind : int { PROJ_BULLET = 0, PROJ_HE = 1 };
struct Projectile {
  vec3 p{0}, v{0};
  float life = 0, dmg = 0;
  int kind = PROJ_BULLET;
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
  float reload = 0;                       // remaining reload seconds (0 = idle)
  int pos2 = ST_NONE;                     // occupant position
  float trans = 0;                        // remaining transition seconds
  bool deployed = true, armed = false, atYawStop = false, atElevStop = false, moveOk = true;
  std::string status;
  int shots = 0;
  float cool = 0;
};
