// CP 5.1 dynamic resolution: see dynres.h
#include "dynres.h"
#include "app.h"
#include "settings.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {   // CP 5.1's controller, kept behind the governor switch (off: this one)
struct DynResCtl {
  float s = 1.0f, sMin = 0.77f, sMax = 1.0f, tFilt = 0.0f; int over = 0;
  float update(float tGpuMs, float sUsed, float targetMs) {   // corpus 10 s10.7.2
    const float budget = targetMs * 0.90f;
    tFilt = tFilt == 0.0f ? tGpuMs : tFilt + 0.2f * (tGpuMs - tFilt);
    over = tGpuMs > targetMs ? over + 1 : 0;
    float t = over >= 2 ? tGpuMs : tFilt;
    float sTarget = sUsed * std::sqrt(budget / std::max(0.1f, t)), err = sTarget - s;
    if (over == 0 && std::fabs(err) < 0.03f) return s;
    float g = err < 0.0f ? (over >= 2 ? 1.0f : 0.5f) : 0.1f;
    s = std::clamp(s + g * err, sMin, sMax); return s;
  }
};
DynResCtl gOld; double gOldLast = 0; bool gUseGov = true;
}
namespace {
const float kRes[6] = {1.00f, 0.93f, 0.86f, 0.80f, 0.72f, 0.64f};
const GovCaps kCaps[6] = {{1, 1, 1}, {0.8f, 0.7f, 1}, {0.6f, 0.45f, 0.7f}, {0.45f, 0.25f, 0.5f}, {0.3f, 0.1f, 0.35f}, {0.2f, 0, 0.25f}};
}
float Governor::update(float gpuMs, float targetMs, bool frozen) {
  ema = ema == 0 ? gpuMs : ema + (2.0f / 9.0f) * (gpuMs - ema);   // EMA over 8 frames
  ++sinceChange;
  down = ema > targetMs * 1.08f ? down + 1 : 0;
  up = ema < targetMs * 0.88f ? up + 1 : 0;
  if (!frozen) {
    if (down >= 4 && rung < 5) { ++rung; down = up = 0; }
    else if (up >= 30 && rung > 0) { --rung; down = up = 0; }
  }
  const float want = std::clamp(sMax * kRes[rung], sMin, sMax);
  if (sinceChange >= 8 && std::fabs(want - scale) >= 0.005f) {   // <= 7 % per change, at most once every 8 frames
    float next = std::clamp(want, scale * 0.93f, scale * 1.07f);
    next = next < scale ? std::ceil(next * 100.0f - 1e-3f) / 100.0f : std::floor(next * 100.0f + 1e-3f) / 100.0f;   // to 0.01, rounded toward the old scale (never past 7 %)
    next = std::clamp(next, sMin, sMax);
    if (next != scale) { scale = next; sinceChange = 0; }
  }
  return scale;
}
GovCaps Governor::caps() const { return kCaps[rung]; }

namespace {
Governor gGov; int gHold = 0; bool gWasOn = false;
}

void (*gFxCapsHook)(App&, const FxTierCaps&) = nullptr;
static void pushFxCaps(App& app) {
  if (!gFxCapsHook) return;
  static FxTierCaps last{}; static double at = -1; static GfxPreset lastP = GfxPreset::Custom;
  const FxTierCaps c = fxTierCaps(); const GfxPreset p = settings().s.preset(); const double now = glfwGetTime();
  if (std::memcmp(&c, &last, sizeof c) == 0 || (p == lastP && at >= 0 && now - at < 0.5)) return;
  gFxCapsHook(app, c); last = c; at = now; lastP = p;
}
void dynResFrame(App& app) {
  pushFxCaps(app);
  const UserSettings& us = settings().s;
  static const bool force = std::getenv("NMK_DYNRES_FORCE") != nullptr;   // test hooks: on in any run; FLIP=<s>: 0.77 <-> 1 every s seconds
  static const double flip = std::getenv("NMK_DYNRES_FLIP") ? std::atof(std::getenv("NMK_DYNRES_FLIP")) : 0.0;
  if (flip > 0) {
    static double last = 0; const double now = glfwGetTime();
    if (now - last >= flip) { last = now; const float to = app.R.resolutionScale() > 0.9f ? 0.77f : 1.0f; app.R.setResolutionScale(to); logf("dynres: flip to %.2f", to); }
    return;
  }
  static const bool off = std::getenv("NMK_DYNRES_OFF") != nullptr;   // A/Bs: never scales (NMK_DYNRES_TARGET still scales on load hitches)
  bool on = (us.dynRes || force) && !off && us.upscale == 0 && app.R.gpuTimers;   // temporal upscaling only (the scale is what TAAU reconstructs)
  if (!on) {
    if (gWasOn) { app.R.setResolutionScale(us.gfx.resScale); gWasOn = false; }   // back to the fixed scale
    return;
  }
  GfxPreset p = us.preset();
  float sMax = 1.0f, sMin = p == GfxPreset::High || p == GfxPreset::Medium ? 0.77f : p == GfxPreset::Low ? 0.67f : std::max(0.5f, us.gfx.resScale * 0.77f);
  if (p == GfxPreset::Custom || p == GfxPreset::UltraLow) sMax = std::max(sMin, us.gfx.resScale);
  if (!gWasOn || gGov.sMin != sMin || gGov.sMax != sMax) {   // on, or a new preset: rung 0 at the top
    gWasOn = true; gGov = Governor{}; gGov.sMin = sMin; gGov.sMax = sMax; gGov.scale = sMax; app.R.setResolutionScale(sMax); gHold = 4; return;
  }
  if (gUseGov != app.R.perf.governor) { gUseGov = app.R.perf.governor; gWasOn = false; gOld = DynResCtl{sMax, sMin, sMax}; return; }   // switched: restart at the top
  if (gHold > 0) { --gHold; return; }   // the timings in flight (2-3 frames late) were rendered at the old scale
  double gpu = 0; for (int i = 0; i < GP_Count; ++i) gpu += app.R.gpuPassMs[i];
  if (gpu <= 0) return;
  if (!gUseGov) {   // switch governor off: CP 5.1's controller, unchanged
    static const float t60 = [] { const char* e = std::getenv("NMK_DYNRES_TARGET"); return e ? (float)std::atof(e) : 1000.0f / 60.0f; }();
    gOld.sMin = sMin; gOld.sMax = sMax;
    const float sUsed = app.R.resolutionScale(), want = std::round(gOld.update((float)gpu, sUsed, t60) * 100.0f) / 100.0f;
    const double now = glfwGetTime(), gap = want < sUsed ? (gOld.over >= 2 ? 0.0 : 0.25) : 1.0;
    if (std::fabs(want - sUsed) >= 0.01f && now - gOldLast >= gap) { app.R.setResolutionScale(want); gOldLast = now; gHold = 4; logf("dynres (old controller): scale %.2f -> %.2f (GPU %.1f ms)", sUsed, want, gpu); }
    return;
  }
  static const float target = [] { const char* e = std::getenv("NMK_DYNRES_TARGET"); return e ? (float)std::atof(e) : 15.0f; }();
  const float sUsed = app.R.resolutionScale(); const int r0 = gGov.rung;
  const bool adsMoving = app.adsT > 0.02f && app.adsT < 0.98f;   // no rung change while aiming in / out
  const float want = gGov.update((float)std::min(gpu, 200.0), target, adsMoving);   // a load hitch (seconds) counts as one long frame, not as 7 s of EMA
  if (gGov.rung != r0) logf("governor: rung %d -> %d (GPU EMA %.1f ms)", r0, gGov.rung, gGov.ema);
  if (want != sUsed) {
    double t0 = glfwGetTime(); app.R.setResolutionScale(want); gHold = 4;
    logf("dynres: scale %.2f -> %.2f (GPU %.1f ms, rung %d), targets re-created in %.1f ms CPU", sUsed, want, gpu, gGov.rung, (glfwGetTime() - t0) * 1000.0);
  }
}

GovCaps governorCaps() { return gWasOn && gUseGov ? gGov.caps() : GovCaps{}; }
int governorRung() { return gWasOn ? gGov.rung : 0; }

int dynResSelfCheck() {
  int bad = 0; auto chk = [&](bool ok, const char* what) { if (!ok) { ++bad; logf("[dynres] FAIL %s", what); } else logf("[dynres] PASS %s", what); };
  const float T = 15.0f;
  {   // a pixel-bound 30 ms at native (a barrage): falls to the floor in <= 7 % steps >= 8 frames apart, rungs keep cutting effects
    Governor g; float s = 1, prev = 1; int last = -100, minGap = 1000; float maxStep = 0;
    for (int k = 0; k < 400; ++k) { s = g.update(30.0f * s * s, T, false); if (s != prev) { minGap = std::min(minGap, k - last); maxStep = std::max(maxStep, 1 - s / prev); last = k; prev = s; } }
    chk(s == 0.77f, "a 2x over-budget frame falls to the floor (0.77)");
    chk(maxStep <= 0.0701f && minGap >= 8, "each change <= 7 %, at most once every 8 frames");
    chk(g.rung == 5 && g.caps().grains == 0, "below the floor the rungs cut effects (rung 5: no grains)"); }
  { Governor g; float s = 1; for (int k = 0; k < 300; ++k) s = g.update(14.0f, T, false);
    chk(s == 1.0f && g.rung == 0, "inside the band at native: no change"); }
  { Governor g; float s = 1; for (int k = 0; k < 60; ++k) s = g.update(30.0f * s * s, T, false);
    int r = g.rung, k = 0; for (; k < 2000 && (g.rung > 0 || s < 1); ++k) s = g.update(6.0f, T, false);
    chk(r > 0 && g.rung == 0 && s == 1.0f && k >= 30 * r, "recovers to native, one rung per 30 calm frames"); }
  { Governor g; for (int k = 0; k < 100; ++k) g.update(40.0f, T, true);
    chk(g.rung == 0, "no rung change during an ADS transition"); }
  logf("[dynres] %d failures", bad);
  return bad ? 1 : 0;
}

FxTierCaps fxTierCaps() {
  static const FxTierCaps t[5] = {   // s8.3 / 8.4: soft instances, clods, flash lights, dust / burst, grains / burst, fragments, ground dust, plume / impact
    {6, 24, 1, 3, 0, 0, 0, 2}, {8, 48, 1, 8, 40, 0, 8, 4}, {12, 96, 2, 20, 200, 40, 16, 8}, {16, 160, 3, 45, 800, 120, 28, 13}, {22, 224, 4, 90, 2500, 300, 40, 18}};
  const GfxPreset p = settings().s.preset();
  FxTierCaps c = t[p == GfxPreset::UltraLow ? 0 : p == GfxPreset::Low ? 1 : p == GfxPreset::High ? 3 : p == GfxPreset::Ultra ? 4 : 2];   // Custom: Medium
  const GovCaps g = governorCaps();
  auto k = [](int n, float f) { return (int)std::lround(n * f); };
  c.softEffects = std::max(1, k(c.softEffects, g.smoke)); c.plumePerImpact = std::max(1, k(c.plumePerImpact, g.smoke)); c.dustPerBurst = std::max(1, k(c.dustPerBurst, g.smoke));   // s8.1 item 5: a dust puff survives
  c.grainsPerBurst = k(c.grainsPerBurst, g.grains); c.flashLights = std::max(1, k(c.flashLights, g.lights));
  return c;
}
