#pragma once
// SN1 shared snow layer (snow.cpp): per-map mask + shader terms; maps without "snow" are unchanged.
#include "common.h"
class App;
void snowSync(App& app);                  // (re)loads the open map's snow mask when it changes
void snowUniforms(unsigned int prog, bool thumb);   // uSnowOn / mask (unit 12) / box / params for a world program
bool snowOn();
vec4 snowMaskAt(float x, float z);        // R deposit, G traffic, B melt, A pooling (0..1)
const char* snowStepAt(float x, float z); // "step_snow" / "step_snow_deep" / "step_slush" / "step_shallow", or nullptr
