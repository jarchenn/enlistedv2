#pragma once
// W6 AI one-line hooks for shared files (game.cpp, mpui.cpp). The AI core itself is ai_core.h.
#include "common.h"
class Game;
void aiMatchBegin(Game& g, vec3 lo, vec3 hi);              // Game::begin: bakes the navmesh, sets up the AI
void aiMatchEnd(Game& g);                                  // Game::end
bool aiDebugCamera(Game& g, vec3& eye, vec3& fwd, float& fov);   // Game::camera: follow-cam on a bot (debug)
void aiDrawDebug(Game& g);                                 // Game::drawDebug: AI overlays and the debug window
void aiZombieStep(Game& g, struct Soldier& s, float dt);
void aiZombieLoopsStop(class App& app);   // SND2: stop every enemy "loop" sound (Game::end)  // Game::aiStep: a zombie's whole brain (zombie.cpp)
int aiZombieModel(Game& g, int type = 0, int variant = 0);  // the character of enemy type `type`, its variant-th model (EN1; -1: none)
void aiZombieAssign(Game& g, Soldier& s);                  // EN1: model, draw scale, lean and body size for s.etype (the zombie rig when its own model is unusable)
void aiZombiePose(Game& g, struct Soldier& s, float dt);  // Game::posesStep: clip-driven pose instead of poseCharacter
void aiZombieFeet(Game& g, struct Soldier& s);            // Game::posesStep, after s.world: the fighters' lower sole on the ground; footprints where a foot comes down
void aiDifficultyCombo();                                 // MpUi::setupWindow: Recruit / Regular / Veteran
class App;
inline fs::path navSidecarOf(const fs::path& map) {   // PF1: maps/x.nmap.json -> maps/x.nav
  std::string s = map.string(); const std::string ext = ".nmap.json";
  if (s.size() > ext.size() && s.compare(s.size() - ext.size(), ext.size(), ext) == 0) s.resize(s.size() - ext.size());
  return fs::path(s + ".nav");
}
void aiNavSidecarSave(App& app, const fs::path& from, const fs::path& to);   // App::saveMapTo: still-valid navmesh entries -> <map>.nav
