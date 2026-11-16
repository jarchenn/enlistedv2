#pragma once
// VH6 vehicle interiors seen from the driver's seat, one per vehicle model: content/vehicles/dash/<spec>.json (palette,
// items from shared part builders, optional "base" spec) - see src/vehicle_dash.cpp. Model vehicles name their spec and
// anchor in content/vehicles/<id>.json "dash": {"spec", "offset" from the driver's eye}; part-built cars (BR cars,
// car-pack copies) take theirs from the asset id and anchor on the asset's dash. Every interior has an animated wheel
// (the model's or the kit's), live speed / rpm / fuel / temperature read-outs, warning lamps and backlit controls that
// glow with the engine running (brighter at night / underground), dark screens when parked, dusty cracked ones in wrecks.
// Our own geometry and colours (no textures from the references, no logos).
#include "common.h"
#include <vector>

class App;
struct TankRT;
struct AssetDef;
struct LightDesc;

void vehicleDashDraw(App& app, const TankRT& t, const mat4& hull);   // tank_ui.cpp drawCar: a car's interior
void vehicleDashFrame(App& app);                                      // app.cpp: parked part-built cars near the camera
void vehicleDashLights(App& app, std::vector<LightDesc>& out);       // lights.cpp lightsGather: the cabin fill of each interior drawn
float vehicleCabinDay(App& app);                                    // 1 by day .. 0 by night: cabin fill and trim lift
bool vehicleDashModelEye(const AssetDef* a, vec3& eye);               // a car-pack copy (drawn as its model): the eye on the model's seat, from its spec's "model" fit
