#pragma once
// FT1 flamethrower (docs/flamethrower.md): the stream is a train of burning parcels, not a hitscan. A nozzle (the local
// player's, a bot's, a LAN player's) emits ~30 parcels a second at 30 m/s inside a narrow cone; each flies until its
// range is used up, stops on the first solid surface (openings let it through), and on the host damages and ignites
// the soldiers it passes. A parcel that lands on ground leaves a short ground fire patch that keeps damaging.
// Clients run the same stream for the look only (damage = false); the host's events keep them in step.
#include "common.h"
#include "light_types.h"
#include <unordered_map>
#include <vector>

class App; struct WeaponDef; struct WeaponState;

struct FlameParcel {
  vec3 p{0}, o{0}, v{0}; float age = 0, life = 1; int owner = -1, weapon = -1; bool dmg = false;
  uint32_t seen[8] = {0};   // soldier slots this parcel has already hit (once each)
};
struct FlamePatch { vec3 p{0}; float t = 0, radius = 1.3f, dps = 12, burnSeconds = 3, burnDps = 14, tick = 0; int owner = -1; bool dmg = false; };
struct FlameNozzle { vec3 o{0}, d{0, 0, 1}, bore{0}; float acc = 0, netT = 0, fxT = 0; double lastOn = -1; int loop = -1, weapon = -1; };

// fuel: `seconds` of firing off the weapon's tank (cap = 100 % = flame.fuelSeconds); true while any is left
bool flameDrain(WeaponState& ws, const WeaponDef& d, float seconds);

class FlameSys {
public:
  // every frame the nozzle `key` is open (-1: the local player, else a roster slot): emits parcels for `dt`
  // bore: the gun's own bore direction in the player's view (zero for other nozzles): a flame tongue follows it for the look
  void spray(App& app, int key, int weapon, vec3 origin, vec3 dir, float dt, bool damage, vec3 bore = vec3(0));
  void step(App& app, float dt);                       // parcels, patches, particles, sound (App::stepProjectiles)
  void gatherLights(std::vector<LightDesc>& out) const; // lights.cpp lightsGather: a moving light per open nozzle
  void clear(App* app);
  // diagnostics / tests
  float farthest = 0;                                  // the farthest any parcel got from its nozzle since clear()
  int emitted = 0, wallStops = 0, groundPatches = 0, soldierHits = 0;
  size_t parcelCount() const { return parcels_.size(); }
  size_t patchCount() const { return patches_.size(); }
  bool open(int key) const { auto it = nz_.find(key); return it != nz_.end() && it->second.lastOn >= t_ - 0.12; }
  bool anyOpen() const { for (auto& [k, n] : nz_) if (n.lastOn >= t_ - 0.12) return true; return false; }
private:
  std::unordered_map<int, FlameNozzle> nz_;
  std::vector<FlameParcel> parcels_;
  std::vector<FlamePatch> patches_;
  double t_ = 0;
  uint32_t rng_ = 0x9E3779B9u;
  float rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ % 100000u) / 100000.0f; }
};
