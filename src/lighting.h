#pragma once
// Lighting engine (L2, docs/lighting.md): time of day, clustered forward local lights, light shadow atlas, DDGI-lite
// probe volumes. The renderer calls it through the LightingPass hooks (render.cpp); this header is the read-back /
// debug side used by the self-tests (--selftest-lights) and the Settings window.
#include "render.h"

class App;

// Time-of-day presets (Settings window, settings_user.json). Day is exactly the pre-L2 look.
enum class TodPreset : int { Day = 0, Dusk = 1, Night = 2, Dawn = 3, Custom = 4 };
float todPresetHour(TodPreset p);            // 14:00, 18:36, 00:00, 05:00
TodPreset todPresetOf(float hour);           // Custom unless the hour is one of the presets
const char* todPresetName(TodPreset p);

struct TodInfo {
  bool day = true;                 // exactly the Day constants
  float sunEl = 0, moonEl = 0;     // degrees above the horizon
  glm::vec3 sun{0}, moon{0}, key{0};   // true sun, moon, the shadow-casting key light
  bool keyIsMoon = false;
  glm::vec3 keyMul{1}, skyMul{1};      // key light and sky irradiance relative to Day
  float stars = 0, nightK = 0, fixedExpo = 1, expoMin = 0.9f, expoMax = 1.45f, expoKey = 0.24f, giOpenK = 1;
};
TodInfo lightingTod(float hour);

// ---- read-back (tests; stalls the GPU)
struct LightingStats { int lights = 0, shadowSlots = 0, shadowFacesDrawn = 0, probesLive = 0, probesDead = 0, probesTotal = 0, captured = 0, captureDraws = 0; bool giAvailable = false; };
LightingStats lightingStats();
// froxel light counts: GPU (last cluster pass) and a CPU reference of the same test over the last uploaded lights
bool lightingClusterCounts(std::vector<int>& gpu, std::vector<int>& cpu, glm::ivec3& dims);
// probe irradiance / pi at p for normal n (CPU replica of giSample over the probe read-back); returns the coverage
float lightingGiAt(glm::vec3 p, glm::vec3 n, glm::vec3& irr);
void lightingResetProbes();   // every probe recaptured (tests: toggled a light)
// R2: probes inside this world box (grown by a probe spacing) are re-captured first, blended into their old value:
// a breach, a blast, a door that opened or closed (damage.cpp, explore.cpp). Under probebudget they jump the queue.
void lightingInvalidate(glm::vec3 lo, glm::vec3 hi);

// ---- app hook (app.cpp, before Renderer::render): gathers the frame's lights (src/lights.h when present) and the
// room volumes near the camera for probe placement
void lightingAppFrame(App& app);
