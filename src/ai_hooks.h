#pragma once
// W6 AI one-line hooks for shared files (game.cpp, mpui.cpp). The AI core itself is ai_core.h.
#include "common.h"
class Game;
void aiMatchBegin(Game& g, vec3 lo, vec3 hi);              // Game::begin: bakes the navmesh, sets up the AI
void aiMatchEnd(Game& g);                                  // Game::end
bool aiDebugCamera(Game& g, vec3& eye, vec3& fwd, float& fov);   // Game::camera: follow-cam on a bot (debug)
void aiDrawDebug(Game& g);                                 // Game::drawDebug: AI overlays and the debug window
void aiZombieStep(Game& g, struct Soldier& s, float dt);  // Game::aiStep: a zombie's whole brain (zombie.cpp)
int aiZombieModel(Game& g);                                // Game::begin: the zombie character (-1: none, soldier model kept)
void aiZombiePose(Game& g, struct Soldier& s, float dt);  // Game::posesStep: clip-driven pose instead of poseCharacter
void aiDifficultyCombo();                                 // MpUi::setupWindow: Recruit / Regular / Veteran
class App;
inline fs::path navSidecarOf(const fs::path& map) {   // PF1: maps/x.nmap.json -> maps/x.nav
  std::string s = map.string(); const std::string ext = ".nmap.json";
  if (s.size() > ext.size() && s.compare(s.size() - ext.size(), ext.size(), ext) == 0) s.resize(s.size() - ext.size());
  return fs::path(s + ".nav");
}
void aiNavSidecarSave(App& app, const fs::path& from, const fs::path& to);   // App::saveMapTo: still-valid navmesh entries -> <map>.nav
