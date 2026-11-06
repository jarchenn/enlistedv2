#pragma once
// Server-owned hand grenades (fragmentation, smoke) and smoke clouds. Inventory, release and the throw motion live with
// the soldier (game.cpp); this module simulates thrown grenades (gravity, bounces off the physics world, server fuse)
// and the smoke volume. A smoke cloud is replicated as {position, start time, seed}: everything else (density,
// particles, visibility) is a pure function of its age, so clients and late joiners rebuild it at the right age.
#include "common.h"

struct Fx;

enum GrenadeKind : int { GK_FRAG = 0, GK_SMOKE = 1 };

struct GrenadeTuning {
  float fragFuse = 4.5f, smokeFuse = 1.6f;          // s from release (Mk 2 / M24 delay ~4-5 s; balance value)
  float restitution = 0.32f, friction = 0.55f;
  float fragDamage = 160, fragLethalR = 4.0f, fragMaxR = 12.0f;   // personnel damage profile (game values)
  float fragWallDmg = 90, fragWallR = 1.2f, fragCrater = 0.22f;   // through the shared destruction pipeline (WALL_GRENADE)
  float smokeRadius = 5.0f, smokeBuild = 2.0f, smokeDense = 15.0f, smokeFade = 5.0f, smokeSigma = 1.2f;   // 1/m at the core
  int maxClouds = 6, particlesPerCloud = 70;
};

struct GrenadeRT {
  uint32_t id = 0; int kind = GK_FRAG; int owner = -1; int team = -1;
  vec3 p{0}, v{0}; float fuse = 0, age = 0; bool resting = false; int bounces = 0;
};

struct SmokeCloud { uint32_t id = 0; vec3 p{0}; double start = 0; uint32_t seed = 1; float radius = 5, build = 2, dense = 15, fade = 5; };   // durations travel with the cloud (replicated)

class Grenades {
public:
  GrenadeTuning tun;
  std::vector<GrenadeRT> live;
  std::vector<SmokeCloud> clouds;
  uint32_t nextId = 1;
  int detonations = 0;
  uint32_t throwFrom(int kind, int owner, int team, vec3 p, vec3 v);
  // advance; onDetonate(grenade) is called once per grenade when its fuse ends (frag: blast, smoke: cloud starts)
  void step(float dt, double now, const std::function<void(const GrenadeRT&)>& onDetonate);
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
