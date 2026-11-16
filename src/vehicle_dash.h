#pragma once
// VH6 vehicle interiors seen from the driver's seat: a generated instrument panel attached per vehicle in hull space
// (anchored on the driver's eye: content/vehicles/<id>.json "dash": {"style": "military" | "civilian", "offset":
// [x, y, z] from the eye, default [0, -0.40, 0.70], "scale"}). Military: painted metal panel, a gauge cluster whose
// needles move (speed, rpm, fuel, temperature), toggle rows, knobs, a warning-light panel and backlit buttons that glow
// (brighter at night / underground), levers, a radio, data plates, grime. Civilian: digital cluster with the live speed,
// a centre screen (our own simple map UI), round vents, buttons, gear selector, console; parked cars show dark screens,
// wrecks dusty and cracked ones. Our own geometry and colours (no textures, no logos).
#include "common.h"

class App;
struct TankRT;

void vehicleDashDraw(App& app, const TankRT& t, const mat4& hull);   // tank_ui.cpp drawCar: a car's interior
void vehicleDashFrame(App& app);                                      // app.cpp: parked civilian car props near the camera
