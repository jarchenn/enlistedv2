#pragma once
// MG1: animated first-person viewmodels (docs/fps-weapons.md#mg1-animated). A weapon JSON "animated" block points at a
// skinned GLB holding the source's own arms + the gun and its clips (tools/modern_guns_anim.py). The rig is authored in
// camera space (eye at the origin, -z forward); the clips drive the arms and the gun, the generated support arm is not
// drawn, sway / bob stay procedural on top, and ADS rotates / moves the rig so the posed sights sit on the view axis.
// Other soldiers, the racks and the icons keep the static gun model. Skinning: the character compute pass (CharInstance).
#include "common.h"

struct App; struct WeaponDef; struct WeaponState; struct AnimVM;

// <dir>/vm.glb + vm.json (clip lengths, gun joint, rig-space points). nullptr with err when it cannot be used.
std::shared_ptr<AnimVM> animVmLoad(const fs::path& dir, const json& block, std::string& err);
float animVmClipDur(const AnimVM& a, const std::string& clip);   // < 0: no such clip
// per frame for the weapon in hand: the clip state machine (draw / fire / reload / idle), pose, skinning, rig placement
void animVmStep(App& app, const WeaponDef& d, WeaponState& s, float dt);
// the skinned parts into the first-person pass; false when not ready (the caller draws the static weapon instead)
bool animVmDraw(App& app, const WeaponDef& d);
// posed world points of the weapon in hand: 0 rear sight, 1 front sight, 2 muzzle; false when not ready
bool animVmPoint(const App& app, const WeaponDef& d, int which, vec3& world);
// AT2: the rig's own red-dot sight: the mark's colour and its ring (in dot radii, 0 none); false when the rig has none
bool animVmDot(const WeaponDef& d, vec3& col, float& ring);
// the weapon in hand has no animated rig (the next animated one plays its draw clip)
void animVmNotActive();
void animVmReset();   // ED2: drop every rig's state (its definition is being replaced)
// the clip playing now (self-tests): name and time; "" when none
std::string animVmClipNow(const WeaponDef& d, float* time = nullptr);
