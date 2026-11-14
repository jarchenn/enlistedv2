#pragma once
// Hooks from the shared core (app.cpp, explore.cpp, station.cpp, damage.cpp) into the multiplayer + AI systems.
// Every hook is a single call so merges with other workstreams stay small. Implemented in mpai.cpp.
#include "common.h"

class App;
struct Projectile;
struct Overlay;
struct ExploreInput;

extern int mpaiExit;   // >= 0: the flag handled by mpaiArg asks run() to return this code before any window opens
bool mpaiArg(App& app, int argc, char** argv, int& k, bool& runSelf);   // --selftest-match / -ai / -net, --host, --join
void mpaiUI(App& app);                         // after the mode's own UI (menus, panels, HUD)
void mpaiMenus(App& app);                      // inside the Creative main menu bar: Play and Scenario menus
bool mpaiCreativeMouse(App& app);              // Creative, before the editor's picking: scenario tools / handles (true = consumed)
bool mpaiBeforeSave(App& app);                 // start of saveMapTo: false refuses the save (scenario without objectives)
void mpaiFrame(App& app, Overlay& ov);         // draw lists (soldiers, grenades) and overlays, before rendering
bool mpaiCamera(App& app, vec3& eye, vec3& fwd, float& fov);   // spectator / death camera
void mpaiEnterExplore(App& app);               // end of enterExplore: starts a match if the Play menu asked for one
void mpaiExitExplore(App& app);                // start of exitExplore
void mpaiInput(App& app, ExploreInput& in);    // filters local input (dead, match over) and handles grenade keys
bool mpaiLocalFrozen(App& app);                // local player dead: skip its movement this step
void mpaiStep(App& app, float dt);             // end of the fixed explore step
bool mpaiProjectile(App& app, Projectile& p, vec3 seg, float len, float tWorld);   // true: hit a soldier (consume)
bool mpaiMeleeRay(App& app, vec3 o, vec3 d, float& t, float dmg);   // knife ray: true = a soldier nearer than t took it (t = its distance)
bool mpaiGrenadeSpawn(App& app, int def, vec3 pos, vec3 vel, int owner);   // true: handled (LAN client request), no local body
// grenade fuse ran out: phase 0 before the blast (returns true when it handled it: smoke), phase 1 after explode()
bool mpaiGrenadeDetonate(App& app, int def, vec3 pos, int owner, int phase);
void mpaiExplosion(App& app, vec3 p, vec3 dir, float dmg, float radius, float craterScale, int wallKind, int breached, size_t wallHits);
struct PhysicsHitFwd;
void mpaiImpact(App& app, const Projectile& p, vec3 hitPos, vec3 hitNormal, uint64_t hitUd, float hitT, const std::string& surface);   // after bulletImpact (LAN replication)
// LAN: crew-served guns, racks, crates and field defences for clients (net.cpp). A client's call becomes a request
// (true = consumed); on the host the result replicates (snapshots for guns, reliable world events otherwise).
bool mpaiStationRequest(App& app, uint64_t id, int pos);   // occupyStation
void mpaiStationLeft(App& app, uint64_t id);                // end of leaveStation
void mpaiStationShot(App& app, uint64_t id, vec3 from, vec3 dir, int tracer, int owner);   // stationShoot
bool mpaiRackRequest(App& app, uint64_t rack, int weapon);  // rackActivate
bool mpaiMayDriveTanks(App& app);                           // GM1: TankSys::handleInteract (Tanker class only, in a match)
bool mpaiPickupWeapon(App& app);                            // GM1: E near a dropped AI weapon (explore.cpp)
bool mpaiCrateRequest(App& app, uint64_t crate);            // crateTransaction
void mpaiCrateChanged(App& app, uint64_t crate);            // the crate's stock changed (host)
bool mpaiBuildClient(App& app);                             // in a LAN match as a client
bool mpaiBuildRequest(App& app, const struct AssetDef* a, vec3 pos, float yaw);   // build mode LMB
void mpaiBuildChanged(App& app, const std::vector<uint64_t>& placed, const std::vector<uint64_t>& removed);   // host: session pieces placed / removed
