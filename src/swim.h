#pragma once
// Swimming (docs/movement.md#swimming): settings and state held by App (app.h), logic in swim.cpp.
// Water comes from waterAt() (water_query.h): the global sea level, V1 water assets and lasso water bodies.
#include "common.h"

// content/settings/movement.json "swim"
struct SwimSettings {
  float enterDepth = 1.25f, exitDepth = 1.05f;   // m of water over the feet: start swimming at chest depth, stop below
                                                 // exitDepth once grounded again (hysteresis)
  float floatDepth = 1.38f;                      // m: the feet hang this far below the surface when floating
  float speed = 2.2f, sprint = 3.2f;             // m/s
  float rise = 1.6f, dive = 1.5f;                // m/s: Space up, Ctrl down
  float buoyancy = 0.45f;                        // m/s: drift back up toward the surface with no vertical input
  float drag = 4.0f;                             // 1/s: velocity approaches the wish velocity at this rate
  float diveSteerPitch = 30.0f;                  // deg: at the surface, looking further down than this dives
  float bobAmp = 0.035f, bobHz = 0.55f, rollDeg = 1.6f;   // surface camera bob (m, Hz idle) and roll
  float strokeHz = 0.85f;                        // breaststroke cycles per second at full speed (idle ~ a third)
  float breath = 30.0f, breathRecover = 7.5f;    // s of breath with the head under; s of breath back per s above
  float drownDps = 10.0f, drownTick = 1.0f;      // HP per second once out of breath, applied every drownTick s
  float fogDensity = 0.11f;                      // 1/m underwater
  vec3 fogColor{0.06f, 0.24f, 0.25f};            // linear HDR, before exposure
};

struct SwimState {
  bool active = false;          // swimming (movement through Physics::updateSwim)
  bool headUnder = false;       // the eye is below the surface (breath runs down; the view goes underwater)
  float surface = -1e9f;        // water surface over the feet this step
  float depth = 0;              // surface - feet
  float lower = 0;              // 0 weapon up .. 1 weapon lowered out of view (eased in the viewmodel)
  float phase = 0;              // stroke phase (cycles)
  float bobT = 0, bob = 0, roll = 0;   // surface bob time, eye offset (m) and view roll (deg)
  float breath = 30.0f;         // s left
  float drownAcc = 0;           // s out of breath since the last damage tick
  float drownDealt = 0;         // HP of drowning damage dealt (or due, outside a match) this life
  int drownTicks = 0, enters = 0, exits = 0, mantles = 0;
  std::string why;              // last transition (tests / debug)
};
