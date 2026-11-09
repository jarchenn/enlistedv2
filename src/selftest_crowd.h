#pragma once
// PF1 crowd benchmark for the 200-AI cases (--selftest-modes, --selftest-zombies): wall frame time (mean, p95, frames
// over 150 ms and when), the AI's CPU ms per step (AiProf), the whole game step's ms and path requests per simulated second.
#include "game.h"
#include "ai_core.h"
#include "crumb.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>

struct CrowdBench {
  double last = 0, stepMs0 = 0, aiSum0 = 0; long aiFrames0 = 0; int stepCalls0 = 0, paths0 = 0, frames = 0; bool on = false;
  std::vector<float> ft; std::string stalls;
  void begin() { last = glfwGetTime(); on = false; ft.clear(); stalls.clear(); frames = 0; }
  // call every frame; samples start after `warm` s
  void frame(Game& G, float t, float warm) {
    double now = glfwGetTime(); float f = (float)(now - last); last = now;
    if (t < warm) return;
    AiCore* A = aiCore();
    if (!on) { on = true; stepMs0 = G.stepMs; stepCalls0 = G.stepCalls; paths0 = G.pathCalls; if (A) { aiSum0 = A->prof.sum[PROF_TOTAL]; aiFrames0 = A->prof.frames; } return; }
    ++frames; ft.push_back(f);
    if (f > 0.15f && stalls.size() < 300) { char w[80]; std::snprintf(w, sizeof w, " t%.1f:%.0fms(%s)", t, f * 1000, gCrumb ? gCrumb : "?"); stalls += w; }
  }
  int over150() const { int n = 0; for (float f : ft) n += f > 0.15f; return n; }
  float mean() const { double s = 0; for (float f : ft) s += f; return ft.empty() ? 0.0f : (float)(s / ft.size() * 1000); }
  float p95() const { if (ft.empty()) return 0; auto v = ft; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, v.size() * 95 / 100)] * 1000; }
  float aiMs() const { AiCore* A = aiCore(); long n = A ? A->prof.frames - aiFrames0 : 0; return n > 0 ? (float)((A->prof.sum[PROF_TOTAL] - aiSum0) / n) : 0.0f; }
  float stepMs(const Game& G) const { int n = G.stepCalls - stepCalls0; return n > 0 ? (float)((G.stepMs - stepMs0) / n) : 0.0f; }
  float pathsPerS(const Game& G) const { return frames ? (G.pathCalls - paths0) / (frames / 60.0f) : 0.0f; }   // fixed 1/60 s steps
  std::string line(const Game& G) const {
    char b[240]; std::snprintf(b, sizeof b, "frame %.1f ms mean, %.1f ms p95, %d over 150 ms (%d frames); game step %.2f ms, AI %.2f ms; %.0f path requests/s",
                               mean(), p95(), over150(), frames, stepMs(G), aiMs(), pathsPerS(G));
    return b;
  }
};
