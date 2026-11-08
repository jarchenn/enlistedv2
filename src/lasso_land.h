#pragma once
// Lasso "Move land" (docs/lasso.md#move-land): slide (and turn) a region's ground shape to a new spot.
//   relief  = ground - membrane(region grown by the blend width)     (the patch's own shape: hills, hollows, banks)
//   source  = ground - w * relief                                     (smoothed back to its surroundings)
//   target  = mix(ground, membrane(target rim) + relief, w)           (the patch replaces what was there)
// where the membrane is the smooth (Laplace) surface through the ground around the grown region and w blends from 1
// inside the region to 0 at the blend width outside it. Placements, paint and water bodies inside the region move
// with it. One undo step; refused (and reverted) when a moved placement would not fit at its new spot.
#include "lasso.h"

struct LandMoveReport {
  bool ok = false;
  std::string why;
  int objects = 0, texels = 0, water = 0, tiles = 0;
  float maxRelief = 0;
};
LandMoveReport lassoMoveLand(App& app, const LassoRegion& r, vec2 delta, float yawDeg, float falloff);
// the patch relief (ground - membrane) at points, for the region as it is now (self-test / diagnostics)
std::vector<float> lassoLandRelief(const App& app, const LassoRegion& r, float falloff, const std::vector<vec2>& pts);
