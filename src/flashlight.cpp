// FX1 player headlamp (user spec 2026-11-16, incoming/flashlight_spec.md). A default ability, no item or model: the
// 'flashlight' binding (default N) toggles App::flashlight; while on (Explore, the local player) one shadowed spot light
// rides the forehead along the view (pitch, crouch, mantle and swim eye height through App::eyePos; lean once it
// moves the eye there). Tunables: content/settings/flashlight.json. The lighting engine (lighting.cpp) gives it a
// shadow slot even at Low, ignores casters within shadowNear_m of the lamp and leaves it out of the probe GI captures;
// the exposure meter gets a floor at switch-on (Renderer::flashExpoFloor) so the scene does not pump.
// Not networked: other players' headlamps are not seen on LAN. Bots have none.
#include "app.h"
#include "light_types.h"
#include <cmath>

namespace {
struct FlashCfg { float inner = 22, outer = 40, intensity = 0.9f, radius = 12, up = 0.07f, fwd = 0.12f, shadowNear = 0.25f, expoHold = 30; vec3 color{0.80f, 0.86f, 0.95f}; };
const FlashCfg& cfg() {
  static FlashCfg c; static bool loaded = false;
  if (loaded) return c;
  loaded = true;
  try {
    json j = json::parse(readTextFile(contentDir() / "settings" / "flashlight.json"));
    c.inner = j.value("innerDeg", c.inner); c.outer = std::max(j.value("outerDeg", c.outer), c.inner + 1.0f);
    c.intensity = j.value("intensity", c.intensity); c.radius = j.value("radius_m", c.radius);
    c.up = j.value("up_m", c.up); c.fwd = j.value("forward_m", c.fwd); c.shadowNear = j.value("shadowNear_m", c.shadowNear); c.expoHold = j.value("expoHold_s", c.expoHold);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3) c.color = vec3(j["color"][0].get<float>(), j["color"][1].get<float>(), j["color"][2].get<float>());
  } catch (...) {}
  return c;
}
}

float flashlightShadowNear() { return cfg().shadowNear; }

// after lightsGather: the headlamp joins the frame's lights (lighting.cpp lightingAppFrame)
void flashlightAppFrame(App& app) {
  Renderer& R = app.R;
  bool on = app.flashlight && (app.mode == Mode::Explore || app.selftest);   // self-tests: the editor views too (FX1 captures)
  if (!on) { R.flashOn = false; R.flashExpoFloor = 0; return; }
  const FlashCfg& c = cfg();
  if (!R.flashOn) R.flashExpoFloor = R.exposure();   // switched on: the meter may not drop below this (no pump)
  else R.flashExpoFloor *= std::exp(-app.frameDt / std::max(1.0f, c.expoHold));
  R.flashOn = true;
  const bool fp = app.mode == Mode::Explore;   // else a self-test's editor view: from its camera
  vec3 f = fp ? App::forward(app.eyeYaw, app.eyePitch) : App::forward(app.camYaw, app.camPitch);
  LightDesc d;
  d.pos = (fp ? app.eyePos() : app.camPos) + vec3(0, c.up, 0) + f * c.fwd;   // the forehead
  d.dir = f; d.radius = c.radius; d.color = c.color; d.intensity = c.intensity;
  d.cosInner = std::cos(radians(c.inner * 0.5f)); d.cosOuter = std::cos(radians(c.outer * 0.5f));
  d.shadow = 1; d.id = kFlashlightLightId;
  R.lights.push_back(d);
}
