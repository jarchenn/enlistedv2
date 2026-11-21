// W1 weather sound (weather corpus; docs/weather-corpus-implementation.md): rain by its rate, wind by its speed and gusts,
// thunder on the lightning flash with the delay by distance, all read from the shared weather state (R.wx.st, never
// written here). Beds play on the Ambience bus through Audio::ambVoice; levels in content/audio/custom/mix.json "weather".
// Only while a W1 preset is on (the map's own authored sky keeps its map ambience: map_ambience.cpp).
#include "app.h"
#include "physics.h"
#include "mainmenu.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <random>

namespace {
json gMix; bool gMixRead = false;
float gCover = 0;   // 0 open sky .. 1 under a roof (smoothed)
std::vector<std::string> gOn;   // bed ids started (silenced when the weather stops)
std::mt19937 gRng{20261120u};
float trim(const char* k) { if (!gMixRead) { gMixRead = true; try { std::ifstream f(contentDir() / "audio" / "custom" / "mix.json"); json m = f ? json::parse(f) : json::object(); gMix = m.value("weather", json::object()); } catch (...) { gMix = json::object(); } } return gMix.is_object() ? gMix.value(k, 1.0f) : 1.0f; }
fs::path wxFile(const char* f) { return contentDir() / "audio" / "custom" / "wx" / f; }
void bed(App& app, const char* id, const char* file, float vol) {
  if (vol > 0 && std::find(gOn.begin(), gOn.end(), id) == gOn.end()) gOn.push_back(id);
  if (vol > 0 || std::find(gOn.begin(), gOn.end(), id) != gOn.end()) app.audio.ambVoice(id, wxFile(file), false, vec3(0), vol);
}
float smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
}   // namespace

// thunder for a strike d m from the eye (R.wx.flashes' distM): the crack or the rumble after d / 343 m/s, quieter with
// distance (a near strike under 1.5 km cracks, farther ones only rumble)
void weatherThunder(App& app, float d) {
  const bool near = d < 1500.0f;
  const char* ev = near ? "wx_thunder_near" : "wx_thunder_far";
  if (!app.audio.hasEvent(ev)) return;
  float vol = std::clamp(1.6f / (1.0f + d / 900.0f), 0.08f, 1.0f) * trim("thunder");
  float pitch = 0.85f + 0.3f * std::uniform_real_distribution<float>(0, 1)(gRng) - std::min(0.15f, d / 40000.0f);   // far: a little lower
  app.audio.play(ev, app.audio.listenerPos(), vol, pitch, std::min(d / 343.0f, 20.0f));   // non-positional: thunder fills the sky
}

void weatherAudioFrame(App& app, float dt) {
  const auto& W = app.R.wx;
  const bool live = W.presetOn() && app.mode == Mode::Explore && !mainMenuActive() && !W.enclosed;
  if (!live) { for (auto& id : gOn) app.audio.ambVoice(id, fs::path(), false, vec3(0), 0); return; }
  const auto& st = W.st;
  for (const auto& f : W.flashes) weatherThunder(app, f.distM);   // W2 lightning: this frame's strikes (weatherDerive clears the list once a frame)
  vec3 L = app.audio.listenerPos();
  {   // under a roof: a ray straight up (the rain moves onto the roof)
    Physics::Hit h; float target = Physics::castRayEx(L + vec3(0, 0.1f, 0), vec3(0, 1, 0), 14.0f, h) ? 1.0f : 0.0f;
    gCover += (target - gCover) * std::min(1.0f, dt * 2.5f);
  }
  // rain: drizzle and light rain on the light bed, heavier rain crossfades to the heavy bed (2 .. 10 mm/h); snow is silent
  const bool rain = st.precip == Renderer::WxDrizzle || st.precip == Renderer::WxRain;
  const float r = rain ? std::max(st.precipMmH, st.precip == Renderer::WxDrizzle ? 0.3f : 1.0f) : 0.0f;
  const float heavy = smooth01((r - 2.0f) / 8.0f), amount = smooth01(r / 1.5f);
  const float out = 1.0f - 0.75f * gCover;
  bed(app, "wx_rain_light", "rain_light.mp3", amount * (1 - heavy) * 0.7f * out * trim("rain"));
  bed(app, "wx_rain_heavy", "rain_heavy.mp3", amount * heavy * 0.85f * out * trim("rain"));
  bed(app, "wx_rain_roof", "rain_roof.mp3", amount * (0.4f + 0.6f * heavy) * 0.8f * gCover * trim("rain"));
  // wind: by speed (0 at 1 m/s, full by 14 m/s), swelling on the gust period up to the gust factor
  const double t = glfwGetTime();
  const float gustK = 1.0f + (std::max(st.gust, 1.0f) - 1.0f) * (float)std::pow(std::max(0.0, std::sin(6.2831853 * t / std::max(1.0f, st.gustPeriodS))), 2.0);
  const float w = smooth01((st.windMs * gustK - 1.0f) / 13.0f) * (1.0f - 0.6f * gCover);
  bed(app, "wx_wind_low", "../bta/wind_low.mp3", std::min(w, 0.6f) * 0.8f * trim("wind"));
  bed(app, "wx_wind_high", "../bta/wind_ridge.mp3", smooth01((st.windMs * gustK - 8.0f) / 10.0f) * 0.7f * (1.0f - 0.6f * gCover) * trim("wind"));
}
