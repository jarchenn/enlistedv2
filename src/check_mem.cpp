// MEM1 --mem-probe: committed (private) and resident memory and CPU at the main menu, a small map, a large map (loaded
// twice: a per-load leak shows as growth), and a 20-bot match on it. [mem] lines; NMK_MEMLOG=1 also stamps every log line.
#include "selftest.h"
#include <chrono>
#include "mainmenu.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include "game.h"
#include "mpui.h"
#include "ai_core.h"
#include "scenario.h"

namespace {
bool startMatch(App& a, int attackers, std::string& err) {   // as selftest_ai_harness.cpp's
  if (!a.doc.extra.is_object() || !a.doc.extra.contains("scenario")) { err = "map has no scenario"; return false; }
  Scenario sc = Scenario::fromJson(a.doc.extra["scenario"]); sc.rules.attackerAI = attackers;
  gUi.matchScenario = sc.toJson(); gUi.loaded = false;
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active && aiCore();
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  if (!ok) err = "match did not start: " + gGame->setupError;
  return ok;
}
int phase = 0; float t = 0; int frames = 0; double cpu0 = 0, wall0 = 0;
double cpuSec() {
#ifdef _WIN32
  FILETIME c, e, k, u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  auto s = [](FILETIME f) { return (((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; }; return s(k) + s(u);
#else
  return 0;
#endif
}
double wall() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void report(const char* stage) {
  ProcMem m = processMemory(); double c = cpuSec(), w = wall();
  logf("[mem] %-26s commit %6.0f MB  working set %6.0f MB  CPU %5.1f %% of one core over %.1f s (%d frames)", stage, m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0,
       w > wall0 ? 100.0 * (c - cpu0) / (w - wall0) : 0.0, w - wall0, frames);
  cpu0 = c; wall0 = w; frames = 0;
}
}

bool memProbeSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--mem-probe") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  t += dt; ++frames;
  static float tick = 0; tick += dt;
  if (tick >= 10 && phase == 8) { tick = 0; ProcMem m = processMemory(); logf("[mem]   t %4.0f s %s: commit %6.0f MB  working set %6.0f MB", t, phase == 0 ? (mainMenuActive() ? "menu" : "startup (no menu)") : "match", m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0); }
  auto next = [&]() { ++phase; t = 0; };
  auto load = [&](const char* id) { const TemplateInfo* ti = a.findTemplate(id); if (ti) a.newMap(*ti, id); else logf("[mem] no template %s", id); cpu0 = cpuSec(); wall0 = wall(); frames = 0; };
  switch (phase) {
    case 0: { static double w0 = -1; if (w0 < 0) { w0 = wall(); cpu0 = cpuSec(); wall0 = w0; frames = 0; mainMenuOpen(a); }   // the real menu (self-tests skip it); wall clock: the menu may hold dt
      static double lastTick = 0; t = (float)(wall() - w0);
      if (t - lastTick >= 10) { lastTick = t; ProcMem m = processMemory(); logf("[mem]   t %4.0f s %s: commit %6.0f MB  working set %6.0f MB", t, mainMenuActive() ? "menu" : "no menu", m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0); } } if (t > (std::getenv("NMK_MEM_MENU_S") ? std::atof(std::getenv("NMK_MEM_MENU_S")) : 10.0)) { report("main menu idle"); mainMenuGo(a, 0); next(); if (std::getenv("NMK_MEM_MENU_ONLY")) phase = 99; } break;
    case 1: load("blank_slate"); next(); break;
    case 2: if (t > 5) { report("small map (blank slate)"); next(); } break;
    case 3: load("chateau_town"); next(); break;
    case 4: if (t > 5) { report("large map (chateau_town)"); next(); } break;
    case 5: load("chateau_town"); next(); break;
    case 6: if (t > 5) { report("large map again"); next(); } break;
    case 7: { std::string err; if (!startMatch(a, 20, err)) { logf("[mem] match: %s", err.c_str()); phase = 9; } else next(); t = 0; } break;
    case 8: if (t > 60) { report("match, 20 bots, 60 s"); a.exitExplore(); next(); } break;
    default: { ProcMem m = processMemory(); logf("[mem] done (peak working set %.0f MB)", m.PeakWorkingSetSize / 1048576.0); a.selftest->exitCode = 0; a.selftest->done = true; } break;
  }
  return true;
}
