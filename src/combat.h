#pragma once
#include "common.h"

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
};
