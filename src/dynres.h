#pragma once
// CP 5.1 dynamic resolution (docs/corpus-implementation.md; corpus 10 s10.7.2, 01 s10.2): the render scale follows the
// GPU time (the per-pass timestamps summed) to hold a 60 fps budget with 10 % headroom (15.0 ms):
//   s_target = s * sqrt(budget / t_gpu);  s += g * (s_target - s), g = 0.5 falling, 1.0 after two frames over the target,
//   0.1 rising; no change inside a 3 % dead band; clamped to the preset's range (Medium / High 0.77-1.0, Low 0.67-1.0).
// Settings > Video "Dynamic resolution" (UserSettings::dynRes); off: the fixed resolution scale. Off in benchmarks
// (NMK_BENCH_DYNRES=1 turns it on there). shortcut: a scale change re-creates the internal targets (Renderer::resize);
// the corpus's viewport sub-rectangle needs every screen-space pass to take a UV scale (TAA: stream L), so changes are
// rate-limited (0.25 s falling, 1 s rising) and quantised to 0.01 until that lands.
class App;
struct DynResCtl {
  float s = 1.0f, sMin = 0.77f, sMax = 1.0f, tFilt = 0.0f;
  int over = 0;
  // tGpuMs: a finished frame's GPU time, rendered at sUsed; returns the scale wanted now
  float update(float tGpuMs, float sUsed, float targetMs);
};
void dynResFrame(App& app);   // app.cpp, once per frame after present
int dynResSelfCheck();        // --check-dynres: the control law on synthetic timings (0 = pass)
