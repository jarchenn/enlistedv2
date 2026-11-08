// Gameplay water query (water_query.h).
#include "water_query.h"
#include "app.h"
#include <algorithm>
#if __has_include("water.h")
#include "water.h"
#define NMK_HAS_V1_WATER 1
#endif
#if __has_include("lasso_water.h")
#include "lasso_water.h"
#define NMK_HAS_LASSO_WATER 1
#endif

float waterAt(const App& app, float x, float z) {
  float w = app.terrain.waterLevel;
#ifdef NMK_HAS_V1_WATER
  w = std::max(w, waterSurfaceAt(app, x, z));
#endif
#ifdef NMK_HAS_LASSO_WATER
  w = std::max(w, lassoWaterAt(app, x, z));
#endif
  (void)x; (void)z;
  return w;
}

bool underwaterAt(const App& app, vec3 p) {
  float w = waterAt(app, p.x, p.z);
  return !waterNone(w) && p.y < w - 0.02f;
}
