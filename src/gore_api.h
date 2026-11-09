#pragma once
// Gore contract (SPEC 7). The damage path reports every bullet hit on a soldier through goreFleshHit; the gore system
// (src/gore.cpp, workstream W7) turns it into wounds, spray, droplets and decals. On a LAN host the same FleshHit is
// replicated (W7: net event) and every client simulates the droplets locally from `seed`, so only the event travels.
// Until W7 lands, src/gore_stub.cpp implements these as no-ops (the old grey impact puff stays).
#include "common.h"

class App;

struct FleshHit {
  vec3 pos{0};               // exact hit point on the posed body (world)
  vec3 normal{0, 1, 0};      // outward surface normal at the hit (approximate: from the hit capsule axis)
  vec3 dir{0, 0, -1};        // bullet direction (unit)
  int victimSlot = -1;       // roster slot of the soldier hit (Game::sold index)
  int shooterSlot = -1;
  int weapon = -1;           // weapon registry index of the round (-1 unknown)
  int bone = -1;             // character joint nearest the hit (decals attach to it), -1 unknown
  int zone = 1;              // 0 head, 1 upper torso, 2 leg / foot, 3 lower torso (W1 adds 3), 4 neck, 5 upper thigh, 6 arm
  float damage = 0;          // applied damage (0 when ignored)
  float energy = 1;          // 0 pistol .. 1 rifle / MG: spray size and speed
  bool exits = false;        // the round passes through (rifles, MGs, BAR): exit wound + forward spray from the exit
  bool helmet = false;       // helmet strike: sparks, no gore
  bool deflected = false;    // spawn-protected target: "deflected" puff, no gore (SPEC 2.2)
  uint32_t seed = 0;         // droplet randomisation (replicated)
};

void goreFleshHit(App& app, const FleshHit& h);   // after damage is applied (or ignored)
void goreStep(App& app, float dt);                // fixed explore step (droplets, bleeding, pools)
void goreReset(App& app);                         // leaving Explore / new match: clears every decal and droplet
