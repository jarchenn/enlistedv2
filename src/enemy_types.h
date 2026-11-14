#pragma once
// EN1 enemy types (content/ai/zombie.json "types"): melee-only AI on the zombie brain and pack code. Entry 0 is the
// plain zombie; the AI type "Zombies + heavies" picks a type per slot by weight. Everything is data, the model too.
#include "common.h"
#include <string>
#include <vector>

struct EnemyType {
  std::string id = "zombie", name = "Zombie";
  std::string model = "zombie"; float height = 1.85f;    // content/characters/<model>.glb, rest pose scaled to height
  float scale = 1, lean = 0;                              // drawn (and hit) at scale x; lean: whole-body forward tilt (deg)
  float fallbackScale = 1, fallbackLean = 0;              // on the plain zombie's model and clips when this model or its walk clip is missing
  vec3 tint{1};
  float hp = 100, damage = 35, reach = 1.6f, lunge = 0;   // lunge: a strike starts this far out and closes the gap in the wind-up
  float cooldown = 0.9f, windup = 0.25f, attackRate = 1.4f;   // the blow lands at the attack clip's swing frame / attackRate (windup without clips)
  float walk = 4.0f, run = 4.0f, runWithin = 1e9f;        // m/s: run when the target is within runWithin, else walk
  float weight = 1, zoneScale = 1;                        // share in a horde; hit-capsule radii x (on top of scale)
  float knock = 0;                                        // m/s shove on the player hit
  float staggerMin = 0, staggerS = 0;                     // a single hit of at least staggerMin flinches it (and stops it staggerS)
  std::vector<std::string> walkClips{"walk01", "walk02"}, idleClips{"idle01", "idle02"}, attackClips{"attack01", "attack02"};
  std::string runClip = "run", damageClip = "damage", deathClip = "death";
};
const std::vector<EnemyType>& enemyTypes();          // never empty (the plain zombie at least)
const EnemyType& enemyType(int k);                   // clamped
int enemyTypePick(uint32_t seed);                    // weighted pick over the types (Zombies + heavies)
