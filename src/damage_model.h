#pragma once
// Per-weapon bullet damage (SPEC 6, workstream W1). Data: content/ai/damage.json.
//   damage(r) = lerp(near, far, smoothstep(falloffStart, falloffEnd, r)) x zone multiplier
// with r the distance the round actually travelled (Projectile::travelled + the hit distance along the last segment).
// Zones, one table for every gun (damage.json "zones"): head 1.3, torso (upper, lower) / neck / arms 1.0, legs and feet 0.6.
// Helmets do full damage (metal sound + sparks, FleshHit::helmet). Spawn-protected victims take nothing
// (FleshHit::deflected, a "deflected" puff instead of the flesh puff; rules.cpp).
#include "common.h"
#include "gore_api.h"

class Game;
struct Projectile;
struct HitZone;

// FleshHit::zone codes (gore_api.h)
// DZ_LIMB is a leg below the upper thigh (or the foot); arms are DZ_ARM.
enum DmZone : int { DZ_HEAD = 0, DZ_UPPER_TORSO = 1, DZ_LIMB = 2, DZ_LOWER_TORSO = 3, DZ_NECK = 4, DZ_UPPER_THIGH = 5, DZ_ARM = 6 };

struct WeaponDamage {
  std::string id;
  float nearDmg = 50, farDmg = 35, falloffStart = 50, falloffEnd = 150, rpm = 0;
  bool dataOnly = false;      // no model in the repo yet
};

struct DamageTable {
  float health = 100, head = 1.3f, torso = 1.0f, arms = 1.0f, legs = 0.6f;   // zone multipliers (neck counts as torso)
  float lowerTorsoSplit = 0.5f;   // fraction along hips -> neck below which a torso hit is lower torso (mid-spine)
  float neckFrom = 0.9f;          // fraction along hips -> neck above which a torso hit is reported as the neck (gore spurting)
  float upperThighTo = 0.5f;      // fraction along hip -> knee below which a leg hit is the upper thigh (gore spurting)
  float helmetAbove = 0.12f;      // m above the head joint (along neck -> head) where the helmet starts (brow level; the eye line is ~0.09)
  bool helmetReducesDamage = false;
  std::string springfieldProfile = "brief";   // [DECISION] decisions.springfieldProfile ("brief" | "kar98k")
  WeaponDamage def;               // unlisted bullet weapons
  std::map<std::string, WeaponDamage> weapons;
  std::string error;              // load problem (empty = ok)
};

const DamageTable& damageTable();                                   // loaded on first use
bool damageTableReload();                                           // re-read content/ai/damage.json (tests, tuning)
bool parseDamageTable(const json& j, DamageTable& out, std::string& err);
const WeaponDamage& dmRow(const std::string& weaponId);            // the weapon's row, or the default row

float dmSmoothstep(float a, float b, float x);
float dmBaseDamage(const WeaponDamage& w, float range);            // before the zone multiplier
float dmZoneMult(const DamageTable& t, int zone);
float dmDamage(const WeaponDamage& w, float range, int zone);      // base x zone
int dmShotsToKill(const WeaponDamage& w, float range, int zone);   // hits to take `health` to 0

// Game::projectileSegment after the soldier search. victim < 0: no soldier on this segment (the travelled distance
// grows by tHit). Otherwise applies the hit (zones, helmet, protection, puffs, FleshHit report) and returns true.
bool dmBulletHit(Game& g, Projectile& p, int victim, const HitZone& hz, vec3 a, vec3 d, float tHit, int shooter);

// last hit resolved by dmBulletHit (tests)
struct DmLastHit { FleshHit fh; float range = 0; std::string weaponId; bool valid = false; };
extern DmLastHit gDmLastHit;
