#pragma once
// One gameplay water query for every stream (docs/movement.md#swimming): the highest of
//   - the map's global water level (Terrain::waterLevel, -1000 = none),
//   - V1's placed water assets (water.h waterSurfaceAt), when that header is in the build,
//   - the lasso tool's map-saved water bodies (lasso_water.h lassoWaterAt), when that header is in the build.
// Each source answers -1e9 (or the -1000 sentinel) for "no water here", so the result is <= -999 when dry.
// The optional sources are picked up with __has_include, so this file merges in any order with batch B and the lasso
// branch; nothing else needs to change when they land.
#include "common.h"
class App;

float waterAt(const App& app, float x, float z);
inline bool waterNone(float level) { return level <= -999.0f; }
bool underwaterAt(const App& app, vec3 p);   // p is more than 2 cm below the surface over it (cameras, eyes)
