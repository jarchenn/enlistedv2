#pragma once
// Water surfaces (V1, docs/rendering.md#water). Assets with a "water" block (tools/catalog_water.mjs, env_sea_shallows)
// are drawn by the water pass after the opaque world (render.cpp calls waterRenderPass): animated normals (wave trains +
// flow-advected ripples), Fresnel, sky reflection plus SSR (the world composite reflects it when SSR is on), refraction
// of the bed, depth-based absorption, colour and a soft shore line from the opaque depth buffer.
#include "render.h"

class App; struct Instance; struct InstanceRT;

struct WaterSurfaceInfo { std::string kind; float level = 0, area = 0; int tris = 0; bool flows = false; vec2 bmin{0}, bmax{0}; };
bool waterAsset(const AssetDef* a);                                       // the asset has a "water" block
std::vector<WaterSurfaceInfo> waterSurfaces(const AssetDef* a);            // parsed surfaces (asset space)
// App::buildDrawLists hook: true when the instance is a water asset (its opaque dressing goes to op, its surfaces to
// app.R.water; the glass outline proxy that serves thumbnails and bounds is not drawn)
bool waterDrawItems(App& app, const Instance& i, const InstanceRT& r, const mat4& M, std::vector<DrawItem>& op);
// world-space water height under (x, z) from the placed water assets (no water: -1e9). Gameplay / tests.
float waterSurfaceAt(const App& app, float x, float z);
struct WaterStats { int draws = 0, tris = 0; double ms = 0; };
const WaterStats& waterStats();                                            // last frame
