#pragma once
// BR1 Battle royale (docs: plan Addendum 9): the drop (bomber line, free fall, parachute), the storm (four shrinking
// circles, damage outside), placement and the HUD. Data: content/settings/royale.json. Host / single player only
// (LAN replication of the drop and the storm is a follow-up).
//
// Hooks into shared code (one line each): game.cpp (the AI is skipped while airborne; bare hands at spawn),
// mpai.cpp (begin, step, input, camera, draw), mpui.cpp / mpui_modes.cpp (setup UI, HUD, result), minimap.cpp (rings).
// For the loot stream (BR2): royaleSafePoint / royaleRing / royaleAirState below; the storm damages through
// Game::damage with how = "storm" (armor must not reduce it); rules.lootModern (BR2) holds the WW2 / Modern switch.
#include "common.h"
struct FxVert;

class App;
class Game;
struct Soldier;
struct ExploreInput;
struct ImDrawList;
struct MinimapView;

// ---- the storm: pure data and schedule (no game state; the headless check runs it alone)
struct RoyaleStormCfg {
  float wait[4] = {90, 75, 60, 45}, shrink[4] = {45, 40, 30, 25}, dps[4] = {1, 2, 4, 8};
  float radiusFrac[3] = {0.42f, 0.235f, 0.13f};   // circles 1..3 as a fraction of the area's shorter side; circle 4 = finalRadius
  float finalRadius = 25;
  // BR7: after the last shrink the final circle drifts: it pauses, then glides to a new centre driftMin..driftMax away
  float driftPause = 15, driftMove = 20, driftMin = 30, driftMax = 60;
};
struct RoyaleRing { vec2 c{0}; float r = 1e9f; };
struct RoyaleStorm {
  RoyaleStormCfg cfg;
  RoyaleRing circle[5];   // [0] covers the whole play area; [1..4] the four storm circles, each inside the one before
  std::vector<vec2> drift;   // BR7: the final circle's centres after the last shrink ([0] = circle[4].c)
  // circles from the play area's half extents (x, z) and a seed: every centre random inside the previous circle;
  // walkable (optional) accepts a drift centre (the game: navmesh under it)
  void build(vec2 half, uint32_t seed, bool (*walkable)(vec2) = nullptr);
  // the state at storm time t (s since the match start): stage 0..3 (4 = final), shrinking or waiting, seconds left in
  // that part, the current ring and the next one
  struct At { int stage = 0; bool shrinking = false; float left = 0; RoyaleRing now, next; float dps = 0; int leg = 0; };   // stage 4: leg = drift move index; shrinking = moving
  At at(float t) const;
  bool outside(vec2 p, float t) const { At a = at(t); return glm::length(p - a.now.c) > a.now.r; }
  float total() const { float s = 0; for (int k = 0; k < 4; ++k) s += cfg.wait[k] + cfg.shrink[k]; return s; }
};

// ---- airborne state per soldier
// BR13: aboard -> exit (clearing the aircraft) -> free fall -> deploy (canopy opening) -> chute (glide) -> landing (back
// on the feet, settling into normal movement; the local player only) -> ground. Velocity carries across every change.
enum RoyaleAir : int { RA_GROUND = 0, RA_ABOARD = 1, RA_FREEFALL = 2, RA_CHUTE = 3, RA_EXIT = 4, RA_DEPLOY = 5, RA_LANDING = 6 };

bool royaleActive();                          // a battle royale match is running (host / single player)
void royaleBegin(App& app, Game& G);          // after Game::begin: plane line, storm circles, everyone aboard, drivable cars
void royaleEnd(App& app);
// BR7: the nearest navmesh point within r that is connected to the rest of the map (not a dead-end pocket: NF_TRAP)
bool royaleSnap(const Game& G, vec3 p, float r, vec3& out);                     // leaving Explore: the adopted cars go back to their parking spots
void royaleStep(App& app, Game& G, float dt); // each fixed step before Game::step: plane, falls, landings, storm damage
int royaleAirState(int slot);                 // RoyaleAir of a roster slot
inline bool royaleAirborne(int slot) { int s = royaleAirState(slot); return s != RA_GROUND && s != RA_LANDING; }   // landing: already on the ground
bool royaleBareHands(App& app, Game& G, Soldier& s);   // applyLoadout hook: true = the soldier got fists / knife only
RoyaleRing royaleRing(bool next);
void royaleFx(App& app, std::vector<FxVert>& out);   // BR12: the storm wall (app.cpp, after the effects are built)
void royaleSetTime(float t);                         // tests: the storm clock             // the current ring / the next one (BR2: bots loot inside it)
vec3 royaleSafePoint(const Soldier& s, int salt = 0);   // a point inside the next circle for this soldier (stable per soldier, stage and salt)
float royaleStormSoon(const Soldier& s);      // s until the storm reaches this soldier's spot (0 = already outside), 1e9 = never
void royaleInput(App& app, ExploreInput& in); // local player: Space jumps / opens the chute; no walking while airborne
bool royaleCamera(App& app, vec3& eye, vec3& fwd, float& fov);   // aboard: behind the bomber
void royaleDraw(App& app);                    // bomber, parachutes
void royaleHud(App& app, Game& G);            // timers, alive count, placement, storm wall and the blue tint
void royaleMinimap(ImDrawList* dl, const MinimapView& v);   // current ring (white) and next ring (blue)
struct MpTest;
void royaleSelfTest(MpTest& T);               // --selftest-royale (headless): storm and match checks, run by runRoyaleSelfTest (selftest_royale.cpp)
