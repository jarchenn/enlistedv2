// BTA: per-map ambience driven by map data (docs/map-ambience.md). Positions are engine x / z; heights follow the terrain.
#include "map_ambience.h"
#include "app.h"
#include "physics.h"
#include "mainmenu.h"
#include "ground_paint.h"
#include "water_query.h"
#include "gore.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <random>

namespace {
std::string gKey;                      // template id + extra.audio, the cached config's source
json gCfg; bool gHas = false;
json gMix;                             // content/audio/custom/mix.json "map_ambience": category trims
std::vector<std::pair<std::string, fs::path>> gVoices;   // voices started (id, file): silenced when not in first person
float gCover = 0;                      // 0 open sky .. 1 under a roof (smoothed)
std::vector<double> gCreakNext;
std::mt19937 gRng{20261120};

fs::path customDir() { return contentDir() / "audio" / "custom"; }
float trim(const char* cat) { return gMix.is_object() ? gMix.value(cat, 1.0f) : 1.0f; }
vec2 xz(const json& a) { return a.is_array() && a.size() >= 2 ? vec2(a[0].get<float>(), a[1].get<float>()) : vec2(0); }
float smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
// a zone's weight: 1 within r of its centre, falling to 0 over fade beyond
float zoneW(const json& z, vec2 l) { float d = glm::length(l - xz(z["at"])), r = z.value("r", 20.0f), f = std::max(0.1f, z.value("fade", 15.0f)); return 1 - smooth01((d - r) / f); }
vec2 nearestOnPath(const json& path, vec2 l) {
  vec2 best = xz(path[0]); float bd = 1e30f;
  for (size_t k = 0; k + 1 < path.size(); ++k) {
    vec2 a = xz(path[k]), b = xz(path[k + 1]), ab = b - a; float t = std::clamp(glm::dot(l - a, ab) / std::max(1e-6f, glm::dot(ab, ab)), 0.0f, 1.0f);
    vec2 q = a + ab * t; float d = glm::length(l - q); if (d < bd) { bd = d; best = q; }
  }
  return best;
}
void voice(App& app, const std::string& id, const std::string& file, bool spatial, vec3 pos, float vol, float minD, float maxD) {
  fs::path f = customDir() / file;
  if (std::none_of(gVoices.begin(), gVoices.end(), [&](auto& v) { return v.first == id; })) { if (vol <= 0) return; gVoices.push_back({id, f}); }   // decoded on first audible use
  app.audio.ambVoice(id, f, spatial, pos, vol, minD, maxD);
}
}   // namespace

float mapWindGust() {
  double t = glfwGetTime();
  float swell = 0.5f + 0.3f * (float)std::sin(t * 0.21) + 0.2f * (float)std::sin(t * 0.53 + 1.7);   // slow breathing
  // a gust in every 9 s cell, at a hashed offset, 2.5-4 s long
  double cell = std::floor(t / 9.0); uint32_t h = (uint32_t)(int64_t)cell * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  float off = (h & 1023) / 1023.0f * 5.0f, len = 2.5f + ((h >> 10) & 1023) / 1023.0f * 1.5f, x = (float)(t - cell * 9.0) - off;
  float gust = x > 0 && x < len ? std::sin(3.14159265f * x / len) : 0.0f;
  return std::clamp(0.35f * swell + 0.75f * gust * gust, 0.0f, 1.0f);
}

const json* mapAudioConfig(App& app) {
  const json* ex = app.doc.extra.is_object() && app.doc.extra.contains("audio") && app.doc.extra["audio"].is_object() ? &app.doc.extra["audio"] : nullptr;
  std::string key = app.doc.templateId + "|" + (ex ? ex->dump() : std::string());
  if (key != gKey) {
    gKey = key; gCfg = json::object(); gHas = false;
    try { std::ifstream f(contentDir() / "audio" / "map_ambience" / (app.doc.templateId + ".json")); if (f && !app.doc.templateId.empty()) { gCfg = json::parse(f); gHas = gCfg.is_object(); } }
    catch (...) { logf("map ambience: audio/map_ambience/%s.json unreadable", app.doc.templateId.c_str()); gCfg = json::object(); }
    if (ex) { for (auto& [k, v] : ex->items()) gCfg[k] = v; gHas = true; }   // the map's own keys win
    try { std::ifstream f(customDir() / "mix.json"); json m = f ? json::parse(f) : json::object(); gMix = m.value("map_ambience", json::object()); } catch (...) { gMix = json::object(); }
    app.audio.ambClear(); gVoices.clear(); gCreakNext.clear(); gCover = 0;
    if (gHas) logf("map ambience: %s (%zu zones, %zu loops, %zu creak spots)", app.doc.templateId.c_str(), gCfg.value("zones", json::array()).size(),
                   gCfg.value("loops", json::array()).size(), gCfg.value("creaks", json::array()).size());
  }
  return gHas ? &gCfg : nullptr;
}

void mapAmbienceFrame(App& app, float dt) {
  const json* c = mapAudioConfig(app);
  bool live = c && app.mode == Mode::Explore && !mainMenuActive();
  if (!live) {   // keep the decoded loops (back in a moment from the editor), silent
    for (auto& [id, f] : gVoices) app.audio.ambVoice(id, f, false, vec3(0), 0);
    if (c) app.audio.setAmbience(0, 0);
    return;
  }
  const json& C = *c;
  vec3 L = app.audio.listenerPos(); vec2 l(L.x, L.z);
  {   // under a roof, a canopy or a ceiling: a ray straight up
    Physics::Hit h; float target = Physics::castRayEx(L + vec3(0, 0.1f, 0), vec3(0, 1, 0), 14.0f, h) ? 1.0f : 0.0f;
    gCover += (target - gCover) * std::min(1.0f, dt * 2.5f);
  }
  const float g = mapWindGust();
  // zones: wind multiplier, a gust bed of their own, the leaves bed
  float windMul = 1, leaves = 0; const json zones = C.value("zones", json::array());
  for (size_t k = 0; k < zones.size(); ++k) {
    const json& z = zones[k]; float w = zoneW(z, l);
    windMul += w * (z.value("wind", 1.0f) - 1); leaves += w * z.value("leaves", 0.0f);
    if (z.contains("gust") && z["gust"].is_object()) {
      const json& gz = z["gust"]; float roof = 1 + gCover * (gz.value("underRoof", 0.25f) - 1);
      voice(app, "zone_" + z.value("id", std::to_string(k)), gz.value("file", std::string()), false, vec3(0), w * gz.value("volume", 0.6f) * (0.2f + 0.8f * g) * roof * trim("gust"), 0, 0);
    }
  }
  if (C.contains("wind") && C["wind"].is_object()) {   // the steady low bed everywhere, swelling with the gusts, quieter under roofs
    const json& wb = C["wind"]; float roof = 1 + gCover * (wb.value("underRoof", 0.35f) - 1);
    voice(app, "wind", wb.value("file", std::string()), false, vec3(0), wb.value("volume", 0.5f) * (0.75f + 0.35f * g) * std::max(0.0f, windMul) * roof * trim("wind"), 0, 0);
  }
  app.audio.setAmbience(0, std::clamp(leaves, 0.0f, 1.0f) * (0.7f + 0.5f * g) * (1 - 0.7f * gCover) * trim("leaves"));
  // loops: points ("at") or lines ("path": the voice rides the nearest point), heights over the terrain
  const json loops = C.value("loops", json::array());
  for (size_t k = 0; k < loops.size(); ++k) {
    const json& e = loops[k];
    vec2 p = e.contains("path") && e["path"].is_array() && e["path"].size() >= 2 ? nearestOnPath(e["path"], l) : xz(e.value("at", json::array()));
    float maxD = e.value("max", 30.0f), d = glm::length(p - l);
    float vol = d > maxD + 5 ? 0.0f : e.value("volume", 0.6f) * (1 + gCover * (e.value("underRoof", 0.6f) - 1)) * trim(e.value("cat", std::string("water")).c_str());
    vec3 pos(p.x, std::max(app.terrain.height(p.x, p.y), waterAt(app, p.x, p.y)) + e.value("y", 0.3f), p.y);   // on the water surface where there is water (a raised creek)
    voice(app, "loop_" + e.value("id", std::to_string(k)), e.value("file", std::string()), true, pos, vol, e.value("min", 2.0f), maxD);
  }
  // creaks: old sheds, now and then while the listener is near, more often and louder in gusts
  const json creaks = C.value("creaks", json::array());
  gCreakNext.resize(creaks.size(), 0);
  double now = glfwGetTime();
  for (size_t k = 0; k < creaks.size(); ++k) {
    const json& e = creaks[k]; vec2 p = xz(e["at"]);
    if (glm::length(p - l) > e.value("r", 14.0f) || now < gCreakNext[k]) continue;
    float lo = e.value("every", json::array({6, 16}))[0].get<float>(), hi = e.value("every", json::array({6, 16}))[1].get<float>();
    gCreakNext[k] = now + (lo + (hi - lo) * std::uniform_real_distribution<float>(0, 1)(gRng)) * (1.2f - 0.5f * g);
    if (gCreakNext[k] - now > 0.5 && app.audio.hasEvent("amb_creak")) {
      std::uniform_real_distribution<float> j(-1.5f, 1.5f);
      vec3 at(p.x + j(gRng), app.terrain.height(p.x, p.y) + 1.5f + 0.5f * j(gRng), p.y + j(gRng));
      app.audio.play("amb_creak", at, e.value("volume", 0.35f) * (0.6f + 0.6f * g) * trim("creak"), 0.9f + 0.2f * std::uniform_real_distribution<float>(0, 1)(gRng));
    }
  }
}

// BT1 (src/snow.h, on the map's branch): the snow mask's step at a point ("step_snow", "step_snow_deep", "step_slush",
// "step_shallow") or nullptr. Weak: null until that code is linked in, then used with no further change
extern "C++" __attribute__((weak)) const char* snowStepAt(float x, float z);
// the ground paint's footstep: five samples (centre counted twice, four 0.35 m around), each the dominant painted type
// (under 35 % paint: the unpainted ground); the most votes wins, so a transition dithers over ~0.7 m instead of a hard line
std::string mapStepEvent(App& app, vec3 pos) {
  const json* c = mapAudioConfig(app);
  const json surf = c ? c->value("surfaces", json::object()) : json::object();
  const auto& T = groundPaintTypes();
  std::map<std::string, int> votes; std::string best; int bv = 0;
  const vec2 off[5] = {{0, 0}, {0, 0}, {0.35f, 0}, {-0.35f, 0}, {0, 0.35f}};
  for (int s = 0; s < 6; ++s) {
    vec2 o = s < 5 ? off[s] : vec2(0, -0.35f);
    std::string ev;
    if (const char* sn = snowStepAt ? snowStepAt(pos.x + o.x, pos.z + o.y) : nullptr) ev = sn;   // the snow layer first
    else {
      int t = 0; float w = groundPaintAt(app, pos.x + o.x, pos.z + o.y, &t);
      if (w < 0.35f || t < 0 || t >= (int)T.size()) t = 0;
      ev = surf.contains(T[t].id) ? surf[T[t].id].get<std::string>() : T[t].step;
    }
    if (++votes[ev] > bv) { bv = votes[ev]; best = ev; }
  }
  return best;
}

void mapStepFeedback(App& app, const std::string& ev, vec3 foot) {
  int n = ev == "step_shallow" || ev == "step_water" ? 2 : ev == "step_slush" ? 1 : 0;   // a few ripples per step (the gore puff pool, capped at 600)
  if (!n || glm::length(foot - app.audio.listenerPos()) > 30.0f) return;
  float wl = waterAt(app, foot.x, foot.z), y = wl > foot.y - 0.3f ? std::max(wl, foot.y) : foot.y + 0.01f;
  for (int k = 0; k < n; ++k) goreRipple(vec3(foot.x + (k ? 0.12f : -0.06f), y, foot.z + (k ? -0.08f : 0.05f)));
}
