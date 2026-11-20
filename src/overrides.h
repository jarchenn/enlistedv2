#pragma once
// ED2 user stat overrides (the Guns / Vehicles screens of the main menu, stat_editor.cpp): JSON merge patches per item,
// loaded after the shipped and local-only files and never written into them.
// Stored beside the exe in overrides/ (an older build's content/<kind>/overrides_user.json is read until the first save):
//   overrides/weapons.json  { "<weapon id>": { <weapon JSON patch>, "_damage": { <content/ai/damage.json row patch> },
//                                                          "_handling": { <handling.json vmRecoil row patch> } } }
//   overrides/vehicles.json { "<vehicle file stem>": { <vehicle JSON patch> }, "_helicopter": { <settings/helicopter.json patch> } }
//   overrides/ai.json       { "types": { "<enemy type id>": { <content/ai/zombie.json type entry patch> } },
//                                          "_soldier": { <content/ai/combat.json patch> } }   (UI3: Character > AI types)
// All three are git-ignored. Applied when the definitions load (startup, and App::reloadGameplayDefs from the menu);
// the AI ones: enemy types in zcfg() (zombie.cpp), the soldier data in Game::loadData (each match start). The shipped
// zombie.json and the local-only zombie_local.json are never written.
#include "common.h"

json& weaponOverrides();     // loaded on first use
json& vehicleOverrides();
json& aiOverrides();          // UI3 (h)
bool saveOverrides(std::string* err = nullptr);
void reloadOverrides();      // re-read both files (tests)
int overrideCount();         // items with an override (the "overrides active" marker)
