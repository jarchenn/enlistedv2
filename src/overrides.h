#pragma once
// ED2 user stat overrides (the Guns / Vehicles screens of the main menu, stat_editor.cpp): JSON merge patches per item,
// loaded after the shipped and local-only files and never written into them.
//   content/weapons/overrides_user.json  { "<weapon id>": { <weapon JSON patch>, "_damage": { <content/ai/damage.json row patch> },
//                                                          "_handling": { <handling.json vmRecoil row patch> } } }
//   content/vehicles/overrides_user.json { "<vehicle file stem>": { <vehicle JSON patch> }, "_helicopter": { <settings/helicopter.json patch> } }
// Both files are git-ignored. Applied when the definitions load (startup, and App::reloadGameplayDefs from the menu).
#include "common.h"

json& weaponOverrides();     // loaded on first use
json& vehicleOverrides();
bool saveOverrides(std::string* err = nullptr);
void reloadOverrides();      // re-read both files (tests)
int overrideCount();         // items with an override (the "overrides active" marker)
