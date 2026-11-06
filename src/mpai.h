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
void mpaiFrame(App& app, Overlay& ov);         // draw lists (soldiers, grenades) and overlays, before rendering
bool mpaiCamera(App& app, vec3& eye, vec3& fwd, float& fov);   // spectator / death camera
void mpaiEnterExplore(App& app);               // end of enterExplore: starts a match if the Play menu asked for one
void mpaiExitExplore(App& app);                // start of exitExplore
void mpaiInput(App& app, ExploreInput& in);    // filters local input (dead, match over) and handles grenade keys
bool mpaiLocalFrozen(App& app);                // local player dead: skip its movement this step
void mpaiStep(App& app, float dt);             // end of the fixed explore step
bool mpaiProjectile(App& app, Projectile& p, vec3 seg, float len, float tWorld);   // true: hit a soldier (consume)
void mpaiExplosion(App& app, vec3 p, vec3 dir, float dmg, float radius, float craterScale, int wallKind, int breached, size_t wallHits);
struct PhysicsHitFwd;
void mpaiImpact(App& app, const Projectile& p, vec3 hitPos, vec3 hitNormal, uint64_t hitUd, float hitT, const std::string& surface);   // after bulletImpact (LAN replication)
