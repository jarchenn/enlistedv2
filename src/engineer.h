#pragma once
// GM3 Engineer building (docs/builder.md#engineer). In a match, B gives the Engineer a fixed build menu
// (content/settings/engineer_builds.json): outpost (a team spawn point), AT gun, barbed wire, sandbags, ammo box, each
// with a build time (hold fire) and a limit per life or standing at a time; holding Del on his own outpost / AT gun
// dismantles it. Placement is the first-person builder's (builder.cpp: checks, session overlay, LAN place events).
#include "app.h"

struct EngItem { std::string id, name, asset; float time = 5; int limit = 1; bool perLife = true, dismantle = false; };
const std::vector<EngItem>& engItems();
float engDismantleTime();

bool engMatchActive();                                  // a match is running: B opens the Engineer menu, not the sandbox palette
bool engineerCanBuild(App& app);                        // the local player is an Engineer (GM1 class; see engineer.cpp)
int engCount(App& app, int item);                       // counted against the item's limit (this life / standing)
void engPlaced(App& app, int item, uint64_t id);        // tag a placement (extra.engineer {owner, team, kind}) and count it
bool engOwnDismantlable(const App& app, const Instance& i);   // the local engineer's own outpost / AT gun

struct EngOutpost { uint64_t id = 0; vec3 pos{0}; float yaw = 0; int team = -1, owner = -1; };
std::vector<EngOutpost> engOutposts(const App& app, int team);   // standing outposts (team -1: all): spawn points for GM1's deploy screen
