#pragma once
// Gameplay rules (workstream W1): spawn protection (SPEC 2.2) and player fall damage (SPEC 4).
// Data: content/settings/rules.json (protection) and content/settings/movement.json "fallDamage" (heights, converted
// to impact speeds with the player's gravity). Every entry point below is a one-line hook from the shared core.
#include "common.h"
#include "physics.h"

class App;
class Game;
struct Soldier;

struct RulesConfig {
  float protectSeconds = 3.0f;          // spawnProtectionSeconds
  bool endsOnFire = true;               // [DECISION] protectionEndsOnFire
  bool endsOnThrow = true;              // [DECISION] protectionEndsOnThrow
  float shimmerHz = 2.2f, tintDepth = 0.35f, iconRange = 150.0f;
  vec3 tint{0.78f, 0.86f, 1.18f};
  float safeHeight = 3.0f, lethalHeight = 10.0f;   // movement.json fallDamage (m)
  std::string error;
};
RulesConfig& rulesConfig();             // loaded on first use (mutable: tests flip the options)
void rulesReload();

// ---- hooks
void rulesOnSpawn(Game& g, Soldier& s);                     // Game::trySpawn: protection starts now
bool rulesIgnoreDamage(const Game& g, int victim);          // Game::damage: true = protected, nothing applied
void rulesOnFire(Game& g, int slot);                        // mpaiProjectile, first segment of every round (host)
void rulesOnThrow(Game& g, int slot);                       // mpaiGrenadeSpawn: a grenade leaves a hand (host)
vec4 rulesTint(const Game& g, const Soldier& s);            // Game::drawItems: shimmer tint while protected
void rulesHud(Game& g);                                     // after Game::drawHud: shield icons, own countdown ring
void rulesNetProtect(Game& g, Soldier& s, bool on);         // LAN client snapshot: the host's protection bit
void rulesAfterMove(App& app, const Physics::CharState& before, const Physics::CharState& after, float dt);   // fall damage
void rulesInWater(App& app);                                // swim.cpp: water breaks a fall (no fall damage climbing out)
void rulesDrown(App& app, float dmg);                       // swim.cpp: out of breath with the head under (match, player only)
void rulesDeflectPuff(App& app, vec3 p, vec3 n, vec3 dir);  // hit on a protected soldier (no gore)

// ---- helpers / test probes
float rulesImpactSpeed(float height, float gravity);        // sqrt(2 g h)
float rulesFallDamage(float impactSpeed, float gravity);    // HP for that impact speed under that gravity
struct RulesStats { int shieldIcons = 0; bool ring = false; float ringLeft = 0; int deflects = 0, falls = 0, endedByFire = 0, endedByThrow = 0; float lastFallSpeed = 0, lastFallDamage = 0; };
extern RulesStats gRulesStats;
