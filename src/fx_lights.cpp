// BF fxbright (Battle Foundry spec 5.4 / 8.3, reference 05 5.9): transient combustion lights. A source registers once and
// updates its temperature, emissivity and interval-averaged envelope k each presentation frame; the light's strength
// follows from physics, never a typed candela:
//   Phi = pi^3 d^2 eps L_v(T)  [lm]  (pi d^2 surface x pi sr, L_v from the blackbody LUT at eps 1),  I = Phi / 4 pi  [cd]
//   r_cut = sqrt(I / E_cut), E_cut 2 lx by day, 0.05 lx at night
// Engine units: 1 = 30,000 lux of illuminance (the CP2 scale); a light's `intensity` 1 lights kLampUnit (0.25) at 1 m, so
// intensity = I / 30000 / 0.25. The nearest-strongest fxLightCap lights a frame go into the clustered list.
#include "render.h"
#include "blackbody_lut.h"
#include <algorithm>
#include <cmath>

namespace {
float bbLum(float T) {   // nits at eps 1, log-linear between the LUT's entries
  float f = std::clamp((T - kBlackbodyT0) / kBlackbodyDT, 0.0f, (float)(kBlackbodyN - 1));
  int i = (int)f, j = std::min(i + 1, kBlackbodyN - 1); float u = f - (float)i;
  return std::exp(std::log(std::max(kBlackbody[i].L, 1e-12f)) * (1.0f - u) + std::log(std::max(kBlackbody[j].L, 1e-12f)) * u);
}
vec3 bbHue(float T) {
  float f = std::clamp((T - kBlackbodyT0) / kBlackbodyDT, 0.0f, (float)(kBlackbodyN - 1));
  int i = (int)f, j = std::min(i + 1, kBlackbodyN - 1); float u = f - (float)i;
  return glm::mix(vec3(kBlackbody[i].r, kBlackbody[i].g, kBlackbody[i].b), vec3(kBlackbody[j].r, kBlackbody[j].g, kBlackbody[j].b), u);
}
}

int Renderer::addCombustionLight(vec3 pos, float T, float eps, float diameterM) {
  for (int s = 0; s < (int)combLights.size(); ++s) {
    CombLight& c = combLights[s];
    if (c.live) continue;
    c = CombLight{true, pos, T, eps, diameterM, 1.0f, (uint16_t)(c.gen + 1)};
    return s | ((int)c.gen << 8);
  }
  return -1;   // all 16 slots busy: the source draws without a light
}
void Renderer::updateCombustionLight(int h, vec3 pos, float T, float eps, float k) {
  if (h < 0) return;
  CombLight& c = combLights[h & 0xFF];
  if (!c.live || c.gen != (uint16_t)(h >> 8)) return;   // a stale handle never moves another source's light
  c.pos = pos; c.T = T; c.eps = eps; c.k = std::max(k, 0.0f);
}
void Renderer::endCombustionLight(int h) {
  if (h < 0) return;
  CombLight& c = combLights[h & 0xFF];
  if (c.live && c.gen == (uint16_t)(h >> 8)) c.live = false;
}
void Renderer::combLightsGather(std::vector<LightDesc>& out, vec3 eye) {
  if (!perf.fxBright || fxLightCap <= 0) return;
  struct Cand { float pri; LightDesc d; };
  std::vector<Cand> cand;
  const bool day = sunDir.y > 0.05f;
  const vec3 lw(0.2126f, 0.7152f, 0.0722f);
  for (int s = 0; s < (int)combLights.size(); ++s) {
    const CombLight& c = combLights[s];
    if (!c.live || c.k <= 0.0f) continue;
    const float phi = 31.00628f * c.d * c.d * c.eps * bbLum(c.T) * c.k;   // pi^3 d^2 eps L_v x envelope
    const float I = phi / 12.566371f;
    if (I <= 0.0f) continue;
    vec3 hue = bbHue(c.T); hue /= std::max(glm::dot(hue, lw), 1e-4f);
    LightDesc d;
    d.pos = c.pos; d.color = hue; d.intensity = I / 30000.0f / 0.25f;
    d.radius = std::clamp(std::sqrt(I / (day ? 2.0f : 0.05f)), 1.0f, 200.0f);
    d.id = 0xC0000000u | (uint32_t)s;   // the combustion range: no Physical lamp scaling, no lightcull
    const float dist2 = glm::dot(c.pos - eye, c.pos - eye);
    cand.push_back({I / (1.0f + dist2), d});
  }
  std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.pri > b.pri; });
  for (int i = 0; i < (int)cand.size() && i < fxLightCap; ++i) out.push_back(cand[i].d);
}
