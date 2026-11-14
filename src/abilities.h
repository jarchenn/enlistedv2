#pragma once
// GM2 abilities (docs/abilities.md): the explosion pack (a placed charge), artillery (a called-in barrage) and the
// suppression vignette. Items work from a weapon id ("explosion_pack") and abilities from an ability id
// ("artillery"); the class system (GM1) grants them, tests and debugging grant them with gm2Grant.
#include "common.h"
#include <string>
#include <vector>

class App;
class Game;
struct Soldier;
struct DrawItem;

// ---- grants
bool gm2Grant(App& app, const std::string& id, int count = 2);   // a weapon id (added to the stack, `count` of it) or an ability id
bool gm2HasAbility(const std::string& id);
void gm2GrantSlot(int slot, const std::string& ability);   // host: another soldier's ability (GM1 class data), checked on its LAN requests
void gm2Reset(App& app);                                        // a new Explore session: no live packs, no strikes, no grants

// ---- explosion pack: placed on the surface in reach (walls, vehicles, ground), it goes off `fuse` s later through explode()
struct LivePack { vec3 pos{0}, nrm{0, 1, 0}; float fuse = 10; int def = -1, owner = -1; uint64_t tank = 0; vec3 local{0}, localN{0}; bool visualOnly = false; };
bool gm2PlacePack(App& app, int def, vec3 eye, vec3 dir, int owner, std::string* why = nullptr);   // host / solo: places it; a LAN client: a request
const std::vector<LivePack>& gm2Packs();

// ---- artillery: a barrage of `shells` rounds within `spread` m of the target, the first `delay` s after the call
struct ArtyConfig { float cooldown = 90, delay = 4, gap = 0.55f, spread = 14, damage = 300, radius = 7, crater = 0.45f; int shells = 6; };
const ArtyConfig& gm2Arty();
bool gm2CallArtillery(App& app, vec2 target, int owner, std::string* why = nullptr);   // host / solo: schedules it; a LAN client: a request
float gm2ArtyCooldownLeft(App& app);   // the local player's cooldown (s)
bool gm2ArtyMapOpen();
void gm2ToggleArtyMap(App& app);       // key 7 (needs the "artillery" ability)

// ---- suppression vignette (local player): every near miss darkens the screen edges; it fades when fire stops
float gm2Suppression();
void gm2AddNearMiss(float closeness);   // 0..1 (1 = brushing past the head)

// ---- per frame
void gm2Step(App& app, float dt);                  // packs, strikes, suppression fade (per frame)
void gm2NearMisses(App& app, float dt);            // the fixed step, after the rounds moved by dt
void gm2DrawItems(App& app, std::vector<DrawItem>& op);
void gm2Hud(App& app);                             // vignette and the artillery map
// LAN: the host's handling of a client's "pack" / "arty" request (validated); a client's world event ("gm2pack")
bool gm2HostRequest(App& app, Game& g, Soldier& s, int slot, const json& m, std::string& why);
bool gm2ClientEvent(App& app, const json& ev);   // true: a GM2 world event (handled)
struct Gm2Stats { int packsPlaced = 0, packsExploded = 0, strikesCalled = 0, shellsLanded = 0, nearMisses = 0, refused = 0; vec3 lastBlast{0}; };
Gm2Stats& gm2Stats();
