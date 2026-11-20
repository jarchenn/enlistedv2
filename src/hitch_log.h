#pragma once
// CP step 0 (docs/corpus-implementation.md): the always-on hitch log and the per-frame counters the benchmark reads.
// A frame over 1.5x the moving median (121 frames) logs one line with what happened in it: texture and buffer uploads
// (calls, MB), programs linked (first use compiles), tex::flushPending ms, buildRT calls, time blocked in swap.
// The GL counts come from wrapping glad's function pointers once (hitchInstall), so every call site is counted.
struct HitchFrame {
  int texUploads = 0; double texMB = 0; int bufUploads = 0; double bufMB = 0; int links = 0;
  double flushMs = 0; int buildRT = 0;
  double swapMs = 0, frameMs = 0, medianMs = 0; bool hitch = false;
};
void hitchInstall();                         // after gladLoadGL
HitchFrame& hitchCur();                      // this frame's counters (hooks add to it)
const HitchFrame& hitchLast();               // the last finished frame
void hitchFrameEnd(double swapMs, bool loading);   // after present and the FPS cap: closes the frame, logs a hitch
long long hitchCount();                      // hitches since start
