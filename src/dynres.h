#pragma once
// Dynamic governor (Battle Foundry rev 3 s8.5, reference 10.5; replaces CP 5.1's sqrt controller: one mechanism): a
// per-frame governor layered over the preset. GPU frame time (the per-pass timestamps summed), smoothed over 8 frames
// (EMA); 4 frames over target x 1.08 steps DOWN one rung, 30 frames under target x 0.88 steps UP one.
//   rung |  res  | smoke | grains | lights
//    0   | 1.00  | 100 % |  100 % |  100 %
//    1   | 0.93  |  80 % |   70 % |  100 %
//    2   | 0.86  |  60 % |   45 % |   70 %
//    3   | 0.80  |  45 % |   25 % |   50 %
//    4   | 0.72  |  30 % |   10 % |   35 %
//    5   | 0.64  |  20 % |    0 % |   25 %
// The resolution is the rung's x the preset's top, clamped to the preset's range (Medium / High 0.77-1, Low 0.67-1):
// below the floor a rung cuts effects only. The scale moves <= 7 % per change, at most once every 8 frames; no rung
// change during an aim-down-sights transition. Effect caps (governorCaps) apply to NEW spawns only (fx.cpp, the
// gameplay session's): a live effect is never despawned. Target 15.0 ms (the user's 1080p decision; the spec's 16.0
// at 1440p), NMK_DYNRES_TARGET to override. Settings > Video "Dynamic resolution" (UserSettings::dynRes) turns the
// governor on / off; off: the fixed scale, caps at 100 %. Off in benchmarks unless NMK_BENCH_DYNRES=1.
// shortcut: a scale change re-creates the internal targets (Renderer::resize); a viewport sub-rectangle would make
// changes free once every screen-space pass takes a UV scale.
class App;
struct GovCaps { float smoke = 1, grains = 1, lights = 1; };
struct Governor {
  int rung = 0, down = 0, up = 0, sinceChange = 1000;
  float ema = 0, scale = 1.0f, sMin = 0.77f, sMax = 1.0f;
  // gpuMs: a finished frame's GPU time; frozen: an ADS transition (no rung change). Returns the scale wanted now.
  float update(float gpuMs, float targetMs, bool frozen);
  GovCaps caps() const;
};
GovCaps governorCaps();       // the live rung's effect caps (1 / 1 / 1 with the governor off)
int governorRung();
void dynResFrame(App& app);   // app.cpp, once per frame after present
int dynResSelfCheck();        // --check-dynres: the governor on synthetic timings (0 = pass)
// Battle Foundry s8.3 / 8.4 per-preset effect caps (Ultra Low, Low, Medium, High, Ultra), x the governor's rung. fx.cpp
// enforces them at spawn (new spawns only). Flash lights stay >= 1 at every tier: s8.1 item 4 keeps the muzzle flash's
// light at the lowest tier (s8.3's "0" for Very Low contradicts it).
struct FxTierCaps { int softEffects, clods, flashLights, dustPerBurst, grainsPerBurst, fragments, groundDust, plumePerImpact; };
FxTierCaps fxTierCaps();
// fx.cpp (the gameplay session) sets this to forward the caps to Fx::setCaps; dynResFrame calls it at startup, at once
// on a preset change, then on a change at most every 0.5 s (retire fades never stack)
extern void (*gFxCapsHook)(App&, const FxTierCaps&);
