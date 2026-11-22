#pragma once
// EN1 enemy types (content/ai/zombie.json "types"): melee-only AI on the zombie brain and pack code. Entry 0 is the
// plain zombie; the AI type "Zombies + heavies" picks a type per slot by weight. Everything is data, the model too.
#include "common.h"
#include <string>
#include <vector>

struct EnemyType {
  std::string id = "zombie", name = "Zombie";
  std::string sndAttack, sndIdle, sndStep;                // CH1 json "sounds" {attack, idle, step}: audio events for a type without built-in ones
  std::string sndMiss, sndHurt, sndWeaponDrop;           // KS1 "miss" (a swing that hits nobody, at the blow), "hurt" (it takes damage), "weapon_drop" (at the death clip's handFloor)
  std::string sndContact, sndDeath, sndLoop;              // SND1 / SND2: "contact" (a blow lands on someone), "death", "loop" (a looping sound on each living one, nearest few)
  std::string sndWindup; float voicePitch = 1;            // SND4: "windup" (as it commits to a blow on a human player, in place of "attack", which then sounds at the blow if the wind-up is 1 s or longer); json voice_pitch: idle / attack / windup x
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
  float blockR = 0;                                       // > 0: the local player cannot stand within blockR x height x scale of its feet (pushed out)
  float locoRate = 1;                                     // walk / run clip speed x (legs move faster than the ground speed alone gives)
  float groundShake = 0;                                  // CH3 json "ground_shake" 0-1: each landing blow shakes the player's view (App::blastShake), fading out by 30 m
  // CH6 json "attacks": [{clip, joint, extend, radius, damage, range [min, max], leap, victims, weight, swing, window [pre, post]}]:
  // the strike of an attack clip. joint: the semantic joint that lands it (default: the clip's fastest hand); extend: metres
  // past it along its limb (a mace head, a jaw); radius: the strike sphere; range: the target distances it is used at (default:
  // the joint's reach at the swing); leap: dashes in during the wind-up; victims: at most this many (0: any); swing: the
  // blow's moment as a fraction of the clip (default: the fastest hand); window: seconds the sphere is live before / after it
  // KN8 json "bone" + "at": the sphere's centre is the point `at` (glTF units, in that skin joint's own rest frame: a weapon head,
  // the flail ball) carried by the joint's pose, so it follows the weapon through the swing; extend is then unused
  // AR1 json "shoot": {"speed", "spread_deg", "ammo", "mass_g"}: a ranged attack: at the swing a projectile leaves the left
  // hand toward the target's chest (aimed over for its drop; damage from the ammo's weapon row); used only with line of
  // sight; the type stands and faces the target while one fits (no strike sphere, no step-in)
  struct AttackSpec { std::string clip, bone, ammo; vec3 at{0}; bool hasAt = false, shoot = false; float speed = 55, spreadDeg = 1, massG = 60; int joint = -1; float extend = 0, radius = 0, damage = -1, rangeMin = 0, rangeMax = 0, weight = 1, swing = 0, pre = 0.15f, post = 0.1f, reach = 0; bool leap = false; int victims = 0; };   // reach: the distance it closes to (default: the fastest hand's at the swing)
  std::vector<AttackSpec> attackSpecs;
  bool attackDust = true;                                 // CH5 json "attack_dust": false: its blows throw no dust (the area, shake and sounds stay)
  bool tankDamage = false;                                // TK1 json "tank_damage" (default: hp >= 500): its landed blows wear down a tank, 1/15 of its durability each
  float headMult = 0;                                     // CH7 json "head_mult": damage x on a head hit instead of the global head multiplier (0: the global one)
  float stompArea = 0;                                    // a strike hurts every hostile within stompArea x height x scale of where it lands
  float explosiveMult = 1;                                // EN2: blast damage x (Game::explosionDamage: grenades, rockets, shells, packs, artillery)
  float capsuleH = 0, capsuleR = 0;                       // EN2: physics capsule height / radius (0: the soldier's)
  float climbM = 0, stepGap = 0;                          // EN4: takes the heavies-only climb links (lips up to the nav pass's 2.2 m) / gap links; stuck in a pit, climbs a lip up to climbM
  std::string climbClip;                                  // EN4: played over a climb (empty or missing: the walk clip)
  std::vector<std::string> walkClips{"walk01", "walk02"}, idleClips{"idle01", "idle02"}, attackClips{"attack01", "attack02"};
  std::string runClip = "run";
  std::vector<std::string> damageClips{"damage"}, deathClips{"death"};   // EN2: lists, one picked per hit / death
};
extern bool gMechRobot;                              // mechrobot perf switch (zombie.cpp, read as each slot spawns): AI type Mechs = the KR-714 robot soldier; off = the melee mech
extern bool gKnight2;                                // knight2 perf switch (zombie.cpp): off = the types' "legacy" patches at the next table load
const std::vector<EnemyType>& enemyTypes();          // never empty (the plain zombie at least)
const EnemyType& enemyType(int k);                   // clamped
int enemyTypeIndex(const std::string& id);          // -1: no such type
int enemyTypePick(uint32_t seed);                    // weighted pick over the types (Zombies + heavies)
int enemyTypeHeavyPick(uint32_t seed, float trollShare);   // EN3 AI type Heavies: "brute" with that share, else "stalker" (by weight among the heavies without those ids)
