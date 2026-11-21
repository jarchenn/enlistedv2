// CP 5.1 dynamic resolution: see dynres.h
#include "dynres.h"
#include "app.h"
#include "settings.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

float DynResCtl::update(float tGpuMs, float sUsed, float targetMs) {   // corpus 10 s10.7.2, verbatim apart from the clamp
  const float budget = targetMs * 0.90f;
  tFilt = tFilt == 0.0f ? tGpuMs : tFilt + 0.2f * (tGpuMs - tFilt);   // low-pass
  over = tGpuMs > targetMs ? over + 1 : 0;
  float t = over >= 2 ? tGpuMs : tFilt;                                  // panic uses the raw time
  float sTarget = sUsed * std::sqrt(budget / std::max(0.1f, t));
  float err = sTarget - s;
  if (over == 0 && std::fabs(err) < 0.03f) return s;
  float g = err < 0.0f ? (over >= 2 ? 1.0f : 0.5f) : 0.1f;            // fall fast, rise slowly
  s = std::clamp(s + g * err, sMin, sMax);
  return s;
}

namespace {
DynResCtl gCtl; double gLastChange = 0; int gHold = 0; bool gWasOn = false;
}

void dynResFrame(App& app) {
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
  gCtl.sMax = 1.0f;
  gCtl.sMin = p == GfxPreset::High || p == GfxPreset::Medium ? 0.77f : p == GfxPreset::Low ? 0.67f : std::max(0.5f, us.gfx.resScale * 0.77f);
  if (p == GfxPreset::Custom || p == GfxPreset::UltraLow) gCtl.sMax = std::max(gCtl.sMin, us.gfx.resScale);
  if (!gWasOn) { gWasOn = true; gCtl = DynResCtl{gCtl.sMax, gCtl.sMin, gCtl.sMax}; app.R.setResolutionScale(gCtl.s); gHold = 4; return; }
  if (gHold > 0) { --gHold; return; }   // the timings in flight (2-3 frames late) were rendered at the old scale
  double gpu = 0; for (int i = 0; i < GP_Count; ++i) gpu += app.R.gpuPassMs[i];
  if (gpu <= 0) return;
  static const float target = [] { const char* e = std::getenv("NMK_DYNRES_TARGET"); return e ? (float)std::atof(e) : 1000.0f / 60.0f; }();
  float sUsed = app.R.resolutionScale();
  float want = std::round(gCtl.update((float)gpu, sUsed, target) * 100.0f) / 100.0f;
  double now = glfwGetTime(), gap = want < sUsed ? (gCtl.over >= 2 ? 0.0 : 0.25) : 1.0;
  if (std::fabs(want - sUsed) >= 0.01f && now - gLastChange >= gap) {
    double t0 = glfwGetTime(); app.R.setResolutionScale(want); gLastChange = now; gHold = 4;
    logf("dynres: scale %.2f -> %.2f (GPU %.1f ms), targets re-created in %.1f ms CPU", sUsed, want, gpu, (glfwGetTime() - t0) * 1000.0);   // the sub-rectangle question: what a change costs
  }
}

int dynResSelfCheck() {
  int bad = 0; auto chk = [&](bool ok, const char* what) { if (!ok) { ++bad; logf("[dynres] FAIL %s", what); } else logf("[dynres] PASS %s", what); };
  const float T = 1000.0f / 60.0f;
  { DynResCtl c; float s = 1; for (int k = 0; k < 60; ++k) s = c.update(30.0f * s * s, s, T);   // fully pixel-bound 30 ms at native
    chk(s >= 0.77f && s < 0.8f, "a 2x over-budget frame falls to the floor (0.77)"); }
  { DynResCtl c; c.sMin = 0.5f; float s = 1; for (int k = 0; k < 200; ++k) s = c.update(20.0f * s * s, s, T);
    chk(20.0f * s * s > 13.0f && 20.0f * s * s < T, "settles under the 60 fps target, near the 15 ms budget (3 % dead band)"); }
  { DynResCtl c; float s = 1; for (int k = 0; k < 50; ++k) s = c.update(14.6f, s, T);
    chk(s == 1.0f, "inside the dead band at native: no change"); }
  { DynResCtl c; c.s = 0.77f; float s = 0.77f; int steps = 0; for (int k = 0; k < 100 && s < 0.99f; ++k, ++steps) s = c.update(6.0f, s, T);
    chk(s > 0.95f && steps >= 3, "rises back to native, at the slow gain"); }
  { DynResCtl c; float s = c.update(40.0f, 1.0f, T); s = c.update(40.0f, s, T);
    chk(s <= 0.78f, "panic: two frames over the target drop at gain 1"); }
  logf("[dynres] %d failures", bad);
  return bad ? 1 : 0;
}
