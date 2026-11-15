#pragma once
// EN1 enemy types (content/ai/zombie.json "types"): melee-only AI on the zombie brain and pack code. Entry 0 is the
// plain zombie; the AI type "Zombies + heavies" picks a type per slot by weight. Everything is data, the model too.
#include "common.h"
#include <string>
#include <vector>

struct EnemyType {
  std::string id = "zombie", name = "Zombie";
  std::string model = "zombie"; float height = 1.85f;    // content/characters/<model>.glb, rest pose scaled to height
  std::vector<std::string> models;                        // variants (json "models"; [0] = model): Soldier::evariant picks one in turn
  float scale = 1, lean = 0;                              // drawn (and hit) at scale x; lean: whole-body forward tilt (deg)
  float fallbackScale = 1, fallbackLean = 0;              // on the plain zombie's model and clips when this model or its walk clip is missing
  vec3 tint{1};
  float hp = 100, damage = 35, reach = 1.6f, lunge = 0;   // lunge: a strike starts this far out and closes the gap in the wind-up
  float cooldown = 0.9f, windup = 0.25f, attackRate = 1.4f;   // the blow lands at the attack clip's swing frame / attackRate (windup without clips)
  float walk = 4.0f, run = 4.0f, runWithin = 1e9f;        // m/s: run when the target is within runWithin, else walk
  float weight = 1, zoneScale = 1;                        // share in a horde; hit-capsule radii x (on top of scale)
  float knock = 0;                                        // m/s shove on the player hit
  float staggerMin = 0, staggerS = 0;                     // a single hit of at least staggerMin flinches it (and stops it staggerS)
  float kickCrater = 0;                                   // each strike digs a crater of this scale where the blow lands (the Fighter)
  int shellKills = 0;                                     // > 0: a direct tank / cannon shell or rocket hit takes exactly hp / shellKills (that round's blast adds nothing)
  float locoRate = 1;                                     // walk / run clip speed x (legs move faster than the ground speed alone gives)
  float nonBlastMult = 1;                                 // damage x from everything but explosions (the Fighters: 0.1)
  float stompArea = 0;                                    // a strike hurts every hostile within stompArea x height x scale of where it lands
  bool blastOnly = false;                                 // only blast damage hurts it (the Fighter)
  float explosiveMult = 1;                                // EN2: blast damage x (Game::explosionDamage: grenades, rockets, shells, packs, artillery)
  float capsuleH = 0, capsuleR = 0;                       // EN2: physics capsule height / radius (0: the soldier's)
  float climbM = 0, stepGap = 0;                          // EN4: takes the heavies-only climb links (lips up to the nav pass's 2.2 m) / gap links; stuck in a pit, climbs a lip up to climbM
  std::string climbClip;                                  // EN4: played over a climb (empty or missing: the walk clip)
  std::vector<std::string> walkClips{"walk01", "walk02"}, idleClips{"idle01", "idle02"}, attackClips{"attack01", "attack02"};
  std::string runClip = "run";
  std::vector<std::string> damageClips{"damage"}, deathClips{"death"};   // EN2: lists, one picked per hit / death
};
const std::vector<EnemyType>& enemyTypes();          // never empty (the plain zombie at least)
const EnemyType& enemyType(int k);                   // clamped
int enemyTypeIndex(const std::string& id);          // -1: no such type
int enemyTypePick(uint32_t seed);                    // weighted pick over the types (Zombies + heavies)
int enemyTypeHeavyPick(uint32_t seed, float trollShare);   // EN3 AI type Heavies: "brute" with that share, else "stalker" (by weight among the heavies without those ids)
