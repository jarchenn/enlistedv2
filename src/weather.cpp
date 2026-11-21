// W1 weather state and presets (weather corpus 08 s2-3, docs/weather-corpus-implementation.md sections 1-2): one state
// (Renderer::Weather::st), one derived block (::der) that every system reads, fifteen presets at the cheap level (light and
// colour, exposure with two-thirds compensation, air density from visibility, cloud cover, sun shadows, wind).
// Constants are the corpus's starting values; they are measured here before they are kept.
#include "render.h"
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdlib>

using WxState = Renderer::WxState; using WxDerived = Renderer::WxDerived;
constexpr int WxNone = Renderer::WxNone, WxDrizzle = Renderer::WxDrizzle, WxRain = Renderer::WxRain, WxSnow = Renderer::WxSnow;

namespace {
struct Row { int id; const char* name; WxState st; float skyLux; vec3 tint; float deckK; };
// id, name, {cover, tau, visM, fogTopM, precip, mm/h, windDirDeg, windMs, gust, gustPeriodS, tempC, dustVisM, lightning/min}, diffuse lux, tint, deck
// deckK: the engine's overcast deck (grey sky, cloud cover 0.86, grade) 0..1; the light itself comes from the formulas
const Row kRows[] = {
  {0,  "Clear",             {0.1f, 0,    40000, 0,  WxNone,    0,  37, 3,   1.3f, 8,  18, 0,    0}, 15000, vec3(1),                  0},
  {2,  "Haze",              {0.2f, 0,    7000,  0,  WxNone,    0,  37, 2,   1.2f, 8,  22, 0,    0}, 25000, vec3(1.0f, 0.98f, 0.95f), 0},
  {3,  "Broken cloud",      {0.5f, 30,   30000, 0,  WxNone,    0,  37, 5,   1.4f, 10, 16, 0,    0}, 25000, vec3(1),                  0},
  {4,  "Light overcast",    {1.0f, 10,   20000, 0,  WxNone,    0,  37, 4,   1.3f, 10, 14, 0,    0}, 50000, vec3(1),                  1},
  {1,  "Overcast",          {1.0f, 30,   15000, 0,  WxNone,    0,  37, 5,   1.4f, 10, 12, 0,    0}, 22000, vec3(1),                  1},
  {5,  "Drizzle and mist",  {1.0f, 50,   2500,  0,  WxDrizzle, 1,  37, 3,   1.3f, 10, 9,  0,    0}, 11000, vec3(1),                  1},
  {6,  "Steady rain",       {1.0f, 100,  2500,  0,  WxRain,    10, 37, 7,   1.5f, 12, 10, 0,    0}, 7500,  vec3(1),                  1},
  {7,  "Thunderstorm",      {1.0f, 300,  800,   0,  WxRain,    50, 37, 14,  1.6f, 15, 16, 0,    6}, 2500,  vec3(1),                  1},
  {8,  "Dawn ground fog",   {0.1f, 0,    20000, 50, WxNone,    0,  37, 0.5f,1.1f, 8,  6,  0,    0}, 15000, vec3(1),                  0},
  {9,  "Dense fog",         {1.0f, 30,   90,    0,  WxNone,    0,  37, 1,   1.1f, 8,  5,  0,    0}, 7000,  vec3(1),                  1},
  {10, "Light snow",        {1.0f, 30,   2000,  0,  WxSnow,    1,  37, 2,   1.3f, 10, -2, 0,    0}, 27000, vec3(1),                  1},
  {11, "Blizzard",          {1.0f, 100,  250,   0,  WxSnow,    4,  37, 15,  1.6f, 12, -8, 0,    0}, 10000, vec3(1),                  1},
  {12, "Clear day on snow", {0.1f, 0,    30000, 0,  WxNone,    0,  37, 3,   1.3f, 8,  -5, 0,    0}, 26000, vec3(0.97f, 0.99f, 1.03f),0},
  {13, "Blowing dust",      {0.2f, 0,    40000, 0,  WxNone,    0,  37, 9,   1.5f, 10, 30, 3000, 0}, 25000, vec3(1.0f, 0.88f, 0.72f), 0},
  {14, "Sandstorm",         {0.3f, 0,    40000, 0,  WxNone,    0,  37, 18,  1.6f, 12, 32, 100,  0}, 12000, vec3(1.0f, 0.74f, 0.50f), 0},
};
const Row* rowOf(int id) { for (const Row& r : kRows) if (r.id == id) return &r; return &kRows[0]; }
float lumOf(vec3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
// direct normal illuminance at the ground through a clean atmosphere (Kasten-Young air mass)
float eDN(float mu) { float h = glm::degrees(std::asin(glm::clamp(mu, 0.0f, 1.0f))); float m = 1.0f / (mu + 0.50572f * std::pow(h + 6.07995f, -1.6364f)); return 133000.0f * std::exp(-0.21f * m); }
// the share of the direct beam through the haze / dust (aerosol scale height 1.2 km) and a closed deck
float beam(const WxState& s, float mu) {
  float ta = (3.912f / std::max(s.visM, 10.0f) + (s.dustVisM > 0 ? 3.912f / s.dustVisM : 0.0f)) * 1200.0f;
  return std::exp(-ta / mu) * (s.cover >= 0.95f ? std::exp(-s.tau / mu) : 1.0f);   // broken cloud: the cloud-shadow map carries it
}
}

int wxPresetCount() { return (int)(sizeof kRows / sizeof kRows[0]); }
int wxPresetIdAt(int menuIndex) { return kRows[std::clamp(menuIndex, 0, wxPresetCount() - 1)].id; }
int wxPresetMenuIndex(int id) { for (int k = 0; k < wxPresetCount(); ++k) if (kRows[k].id == id) return k; return 0; }
const char* wxPresetName(int id) { return rowOf(id)->name; }
int wxPresetFromName(const std::string& n) {   // a preset's name (any case, "_" for " ") or the old "cloudy" / "clear"; -1 unknown
  std::string a; for (char c : n) a += c == '_' ? ' ' : (char)std::tolower((unsigned char)c);
  if (a == "cloudy") return 1;
  for (const Row& r : kRows) { std::string b; for (const char* p = r.name; *p; ++p) b += (char)std::tolower((unsigned char)*p); if (a == b) return r.id; }
  return -1;
}
float wxPresetDeck(int id) { return rowOf(id)->deckK; }

// Once a frame after lightingBegin (sunDir is the day's key light). Preset ids 0 / 1 (Clear, Overcast) and Map default keep
// today's look (Lighting: Classic and every approved map); the formulas below drive ids 2-14 under Lighting New, as ratios to
// Clear at the same sun height, so Clear stays today's picture and every other preset moves from it by the corpus's numbers.
void weatherForce(Renderer& R, int id) { R.wx.target = std::max(0, id); R.wx.userSet = true; R.wx.snap(); }

void weatherDerive(Renderer& R, vec3 eye) {
  auto& W = R.wx; WxDerived& d = W.der;
  static bool envDone = false;   // NMK_WEATHER=<name or id>: once, at the first frame (the collective run / captures)
  if (!envDone) { envDone = true; if (const char* e = std::getenv("NMK_WEATHER")) { int id = std::isdigit((unsigned char)e[0]) ? std::atoi(e) : wxPresetFromName(e); if (id >= 0) weatherForce(R, id); } }
  const int id = W.want();
  const Row& row = *rowOf(id);
  W.st = row.st;
  W.st.wet = id == 5 ? 0.5f : id == 6 || id == 7 ? 1.0f : 0.0f; W.st.puddle = id == 6 ? 0.5f : id == 7 ? 1.0f : 0.0f;   // until accumulation (W3)
  W.flashes.clear();
  const bool phys = W.presetOn() && id >= 2 && R.q.lightNew && !R.q.legacy;
  d = WxDerived{};
  d.phys = phys; d.preset = id;
  const float mu = std::max(R.sunDir.y, 0.02f);
  const WxState& s = W.st; const Row& clr = *rowOf(0);
  const float dn = eDN(mu);
  d.eSunLux = dn * beam(s, mu); d.eSkyLux = row.skyLux * std::sqrt(mu / 0.77f);   // the table's diffuse light is for a high sun (~50 deg)
  if (id == 12) d.eSkyLux *= 1.3f;   // Clear day on snow: the ground's bounce (rho 0.8) under an open sky
  d.eTotalLux = d.eSunLux * mu + d.eSkyLux;
  const float clrSun = dn * beam(clr.st, mu), clrTot = clrSun * mu + clr.skyLux * std::sqrt(mu / 0.77f);
  d.visM = s.visM; d.dustVisM = s.dustVisM;
  if (phys) {
    d.sunK = clrSun > 0 ? d.eSunLux / clrSun : 0.0f;
    d.skyK = d.eSkyLux / (clr.skyLux * std::sqrt(mu / 0.77f));
    d.tint = row.tint / std::max(lumOf(row.tint), 1e-3f);
    const float r = std::max(d.eTotalLux / std::max(clrTot, 1.0f), 1e-3f);   // the scene's light against Clear's
    d.keyMul = std::pow(r, 1.0f / 3.0f);   // auto exposure: a third of the change stays on screen (two-thirds compensation)
    d.fixedMul = std::pow(r, -2.0f / 3.0f);   // a fixed exposure moves by two thirds of it
    float vis = s.dustVisM > 0 ? std::min(s.visM, s.dustVisM) : s.visM;
    d.sigma = 3.912f / vis;
    d.fogTopM = s.fogTopM; d.fogLayerSigma = s.fogTopM > 0 ? 3.912f / 150.0f : 0.0f;   // Dawn ground fog: 150 m inside a 5-50 m layer
    d.fogAlbedo = s.dustVisM > 0 ? 0.7f : 0.9f; d.fogG = s.dustVisM > 0 ? 0.7f : 0.85f;
    d.visM = s.fogTopM > 0 ? 150.0f : vis;
    d.windK = std::clamp(s.windMs / 3.0f, 0.1f, 3.0f);
  } else {
    d.sigma = 0.0010f;   // today's haze at Medium, now on every quality preset (fair sight on Low / High)
    d.visM = 3.912f / d.sigma;
  }
  d.skyRatio = row.skyLux / clr.skyLux * (id == 12 ? 1.3f : 1.0f); d.beamRatio = clrSun > 0 ? d.eSunLux / clrSun : 0.0f;
  d.cullM = 1.42f * d.visM;
  const float a = glm::radians(s.windDirDeg); d.windXZ = vec2(std::sin(a), std::cos(a)) * s.windMs * (phys ? 1.0f : 1.0f);
  d.sunShadow = R.sunShK;
  // W2 lightning (Thunderstorm): Poisson strikes at lightningPerMin; each a double pulse ~0.25 s; distance 0.5-8 km
  static float lastT = -1; static uint32_t rng = 12345u; auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
  const float dt = lastT < 0 ? 0.0f : std::clamp(R.cloudClock() - lastT, 0.0f, 0.25f); lastT = R.cloudClock();
  if (R.perf.lightning && phys && s.lightningPerMin > 0 && rnd() < s.lightningPerMin / 60.0f * dt) {
    ++W.strikes; W.strikeT = R.cloudClock(); W.strikeDistM = 500.0f + 7500.0f * rnd() * rnd();
    const float az = 6.2831853f * rnd(), hd = std::sqrt(std::max(W.strikeDistM * W.strikeDistM - 1200.0f * 1200.0f, 0.0f));
    W.flashes.push_back({eye + vec3(std::cos(az) * hd, 1200.0f, std::sin(az) * hd), W.strikeT, W.strikeDistM});
  }
  if (R.perf.lightning && phys && W.strikeT >= 0) {
    float a = R.cloudClock() - W.strikeT, near = 1.0f - std::min(W.strikeDistM / 8000.0f, 0.9f);
    d.flash = near * (std::exp(-a / 0.05f) + (a > 0.12f ? 0.7f * std::exp(-(a - 0.12f) / 0.08f) : 0.0f));
    if (a > 1.0f) d.flash = 0;
  }
}
