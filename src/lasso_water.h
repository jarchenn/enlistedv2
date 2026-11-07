#pragma once
// Lasso water fill (docs/lasso.md). Water bodies are saved in the map (MapDoc::waterBodies): a flat surface at the
// hollow's spill height over a world xz outline.
//
// Contract for other streams (the swim stream's waterAt): lassoWaterAt() is the only call they need.
#include "common.h"
#include <string>
#include <vector>

class App; struct WaterBody;

// world-space surface height of the lasso water bodies at (x, z); -1e9 when there is none
float lassoWaterAt(const App& app, float x, float z);

// the result of a fill attempt
struct WaterFill {
  bool ok = false;
  std::string why;              // refusal reason ("no closed hollow", ...)
  float level = 0;              // spill height (the lowest point of the rim)
  vec2 seed{0}, spill{0};       // lowest point inside the region, and where the water would leave
  std::vector<vec2> outline;    // world xz
  int cells = 0;                // fine-grid cells under water
};

// Find the hollow that contains the lowest ground point inside `region` and compute the level it holds before it
// spills (priority flood on the fine sample grid, bounded to the region bbox + margin). Does not modify the map.
WaterFill lassoComputeFill(const App& app, const std::vector<vec2>& region, float margin = 80.0f);
