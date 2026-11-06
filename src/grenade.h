#pragma once
// Smoke clouds, and a read-only view of the live hand grenades for the AI, snapshots and client drawing.
// The grenades themselves are the shared core (equipment.cpp: App::spawnGrenade / stepGrenades, Jolt bodies, fuse,
// one detonation through explode(WALL_GRENADE)); a smoke grenade's detonation starts a cloud here (mpai hook).
// A smoke cloud is replicated as {position, start time, seed, radius, durations}: density, particles and visibility
// are pure functions of its age, so clients and late joiners rebuild it at the right age.
#include "common.h"

struct Fx;

enum GrenadeKind : int { GK_FRAG = 0, GK_SMOKE = 1 };

struct GrenadeTuning {
  float fragDamage = 160, fragMaxR = 12.0f;        // personnel damage profile (game values; walls / blast: the weapon JSON)
  float smokeRadius = 5.0f, smokeBuild = 2.0f, smokeDense = 15.0f, smokeFade = 5.0f, smokeSigma = 1.2f;   // 1/m at the core
  int maxClouds = 6, particlesPerCloud = 70;
};

struct GrenadeRT {   // view of one live grenade (host: from App::grenades each step; client: from snapshots)
  uint32_t id = 0; int kind = GK_FRAG; int def = -1; int owner = -1;
  vec3 p{0}; quat rot{1, 0, 0, 0}; float fuse = 0, age = 0; bool resting = false;
};

struct SmokeCloud { uint32_t id = 0; vec3 p{0}; double start = 0; uint32_t seed = 1; float radius = 5, build = 2, dense = 15, fade = 5; };   // durations travel with the cloud (replicated)

class Grenades {
public:
  GrenadeTuning tun;
  std::vector<GrenadeRT> live;
  std::vector<SmokeCloud> clouds;
  uint32_t nextId = 1;
  int detonations = 0;
  float density(const SmokeCloud& c, double now) const;       // 0..1 build-up / dense / fade
  float opticalDepth(vec3 a, vec3 b, double now) const;       // integrated along the segment (core-weighted)
  float transmittance(vec3 a, vec3 b, double now) const { return std::exp(-opticalDepth(a, b, now)); }
  void addCloud(vec3 p, double start, uint32_t seed);   // uses the current tuning
  void addCloud(const SmokeCloud& c) { clouds.push_back(c); while ((int)clouds.size() > tun.maxClouds) clouds.erase(clouds.begin()); }
  void expire(double now);
  void particles(Fx& fx, double now, float dt) const;
  void clear() { live.clear(); clouds.clear(); }
  static float cloudLife(const SmokeCloud& c) { return c.build + c.dense + c.fade; }
};

// simple ballistic aim: velocity of magnitude `speed` from p to land at t (high or low arc); false if out of range
bool grenadeVelocity(vec3 from, vec3 to, float speed, bool high, vec3& vel);
