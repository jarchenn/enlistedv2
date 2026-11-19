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
void aiZombiePose(Game& g, struct Soldier& s, float dt);
void gunBodyPose(Game& g, struct Soldier& s, float dt);
vec3 weaponPartCentre(const struct WeaponDef& d, int role, vec3 def);   // DF1 gunbody.cpp: the centre of a gun's parts of one WPart role (weapon space; def without any)
bool deltaBodyPose(Game& g, struct Soldier& s, float dt, struct PoseParams& pp);   // DF1 gunbody.cpp: Delta Force clips + procedural arms on the held gun (false: no clips, pose as a soldier)   // GM1 gunbody.cpp: the gas-mask soldier's clips (Game::posesStep)
//  // Game::posesStep: clip-driven pose instead of poseCharacter
void aiForget(Game& g, int slot);                         // the slot died, respawned or jumped: drop it from every bot's knowledge / targets (ai_core.cpp)
void aiDifficultyCombo();                                 // MpUi::setupWindow: Recruit / Regular / Veteran
class App;
inline fs::path navSidecarOf(const fs::path& map) {   // PF1: maps/x.nmap.json -> maps/x.nav
  std::string s = map.string(); const std::string ext = ".nmap.json";
  if (s.size() > ext.size() && s.compare(s.size() - ext.size(), ext.size(), ext) == 0) s.resize(s.size() - ext.size());
  return fs::path(s + ".nav");
}
void aiNavSidecarSave(App& app, const fs::path& from, const fs::path& to);   // App::saveMapTo: still-valid navmesh entries -> <map>.nav
