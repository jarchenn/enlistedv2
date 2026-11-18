#pragma once
// SD1 Search and Destroy: the game layer around the round controller (sd_mode.h). Host / solo run the controller;
// LAN clients mirror its state ("sd" world events, 2 Hz and on every change) and send buy / hold requests.
#include "common.h"
#include <string>
#include <vector>

class App; class Game; struct Soldier; struct Scenario; struct ExploreInput; class SdMatch;

SdMatch* sdMatch();                                   // the live controller (host / solo), or the client mirror; null outside the mode
bool sdActive();
// the map's Search and Destroy layout (docs/search-and-destroy.md), or the temporary one: before validation / setup
void sdPrepareScenario(App& app, Scenario& sc);
void sdBegin(Game& g);                                // after Match::setup (host and client)
void sdEnd();
void sdStep(Game& g, float dt);                       // host / solo: each Game::step after the alive reports
void sdClientStep(Game& g, float dt);                 // LAN client: effects from the mirrored state
void sdOnSpawned(Game& g, int slot);                  // trySpawn: the slot's kept gear instead of the class loadout
void sdOnKill(Game& g, int victim, int killer, const std::string& how);
bool sdFrozen();                                      // freeze / break phases: no locomotion, no fire, no damage
float sdArmor(Game& g, int victim, float dmg, const char* how, int attacker);   // the health damage after the mode's armor
void sdInput(App& app, ExploreInput& in);             // local player: freeze, no sprint / prone, plant / defuse holds, the buy menu key
bool sdBotGoal(Game& g, const Soldier& s, vec3& goal);   // AI: where this bot goes now (false: the default goal)
bool sdBotHolding(Game& g, const Soldier& s);
bool sdBotFetching(Game& g, const Soldier& s);
bool sdBotPushing(Game& g, const Soldier& s);        // SD5: the carrier near a pad / late, or an attacker stalled: straight for the goal       // SD3: the attacker nearest the dropped bomb (it goes straight for it)        // AI: planting / defusing / at its post: hold still
void sdDrawHud(Game& g);
// SD9: the bomb on the minimap / M map: grey carried (at the carrier), blue dropped, red planted (pulsing, the site
// letter). Attackers always see it; defenders only once it is planted. The same colours on the world marker.
enum class SdBombMark : int { None = -1, Carried = 0, Dropped = 1, Planted = 2 };
SdBombMark sdBombMarkFor(Game& g, vec3& pos);   // what the local player sees, and where
struct ImDrawList; struct MinimapView; void sdMinimap(ImDrawList* dl, const MinimapView& v);
bool sdClientEvent(App& app, const json& ev);
bool sdHostRequest(Game& g, int slot, const json& req);
std::string sdCommand(Game& g, const std::string& args);   // /sd skip | buy <item> | timeout | state
bool sdInBuyZone(Game& g, int slot);
std::vector<vec2> sdPadOutline(Game& g, int site);   // SD3: the outline drawn on the ground (the objective's own plant shape)
bool sdBuyMenuOpen();                                 // SD3: Esc closes it (before the pause menu)
void sdCloseBuyMenu();
// SD8: the round clocks (rules sdRoundTime / sdFreeze / sdBuy / sdFuse / sdWarmupTime): sliders for match setup (ImGui) or the
// host's pause menu (flat, midMatch: no warmup; a change takes effect next round); true when one changed
struct ScenarioRules; bool sdTimingUI(ScenarioRules& r, bool midMatch);
void sdPauseTimingUI(Game& g);   // the pause menu's section (SD host / solo only)
// SD3: with the Vehicles option off the map's vehicles leave the doc for the match (restored by sdEnd); a save in the
// meantime writes them back in (the doc to save gets them appended)
struct MapDoc; const MapDoc& sdDocToSave(const MapDoc& doc, MapDoc& scratch);
