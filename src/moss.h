#pragma once
// BF B1 moss field (docs/vfx-rev3/BATTLE-FOUNDRY-SPEC.md 4.5, reference 03 s3.2). The GLSL side is kCommon's
// mossField / mossMoist / mossShelter (render.cpp); mossFieldCpu below is its mirror with the same integer hash, so the
// cushions and scatter placed on the CPU agree with the film and mat the shaders draw.
//
// The habitat grid holds the low-frequency terms a shader cannot compute, at 2+ m cells over the terrain:
//   x water nearness  sat(1 - distToWater / 8)  (water bodies, lasso water, marsh paint)
//   y shelter0        sat(1 - sunHours / 6)     (sun swept over the day against terrain, buildings and canopies)
//   z exclusion       1 within ~1.2 m of a door or gate (no moss where a door swings)
//   w canopy          crown cover over the cell (moss under trees, litter, root proximity)
// It rebuilds when the map, its instances, paint or water change (mossHabitatSync, once a frame).
#include "common.h"
class App;

void mossHabitatSync(App& app);            // rebuild if the signature changed (cheap otherwise)
vec4 mossHabitat(float x, float z);        // bilinear lookup; (0, 0, 0, 0) before the first build. Thread-safe once built
uint32_t mossHabitatRev();                 // bumps per rebuild (InstanceRT::contact caches)

float mossVnCpu(vec3 p);
float mossMoistCpu(float y, float drip, float cav, float distW);
float mossShelterCpu(float sh0, vec3 n, vec3 sunDir);
// (M appearance, G instanced relief, L lichen); nearK = 1 (the 8 cm octave at full strength)
vec3 mossFieldCpu(vec3 p, float upDot, float wallAttach, float groundRecv, float moist, float shelter, float por, float root,
                  float traffic, float excl, float snow, float site, float lichenAge = 0.5f, float nearK = 1.0f);
