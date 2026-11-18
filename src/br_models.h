#pragma once
// BR3 Battle Royale models (tools/bake_br_assets.py). Every item is a hidden asset content/assets/br_<id>.json: its box
// is the committed stand-in, its "model" the local-only bake (content/assets/models/br_<id>/, licences unknown).
//  - the player / bot body: content/characters/br_marine.glb (local-only; missing: the soldier body stays)
//  - worn gear (Soldier::helmetLvl / vestLvl / backpack): the asset's "worn" placement on the marine's rest pose,
//    carried by its joint (pose.delta), in the world and in the inventory preview
//  - optics (WeaponState::att[0]: 1 red dot, 2 ACOG, 3 4x, 4 10x): mounted at the gun's JSON "rail" ([x, y, z] weapon
//    space, "railScale"), first and third person; in ADS the eye sits behind the optic (its "sight" eye point)
#include "model.h"
#include <imgui.h>

class App; class Game; struct Soldier; struct WeaponDef; struct WeaponState; struct ModelDraw; struct DrawItem;

int brMarineModel(Game& G);   // index into Game::models, loaded once; -1 when the bake is missing
// worn helmet / vest / backpack of s, drawn at `world` (its model -> world) with its current pose
void brWornDraws(App& app, Game& G, const Soldier& s, const mat4& world, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes);
bool brHidePart(const Soldier& s, const std::string& partName);   // the marine's cap under a helmet
// the fitted optic on a gun drawn with weapon -> world W
void brSightDraws(App& app, const WeaponDef& d, const WeaponState& ws, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes);
// weapon-space eye line of the fitted optic (no optic, or a scoped rifle keeping its own scope: false)
bool brSightLine(const WeaponDef& d, const WeaponState& ws, vec3& R, vec3& F, float& relief);
bool brSightLineActive(const WeaponDef& d, vec3& R, vec3& F, float& relief);   // the local player's state of d
// AT2: the fitted red dot / holographic sight's window (weapon space centre, radius), its mount and the asset's bounds
bool brSightWindow(const WeaponDef& d, const WeaponState& ws, vec3& centre, float& radius, mat4* mount = nullptr, vec3* bmin = nullptr, vec3* bmax = nullptr);
void drawInventoryCharacter(ImVec2 mn, ImVec2 mx);   // the local character with what it wears, turnable by dragging
