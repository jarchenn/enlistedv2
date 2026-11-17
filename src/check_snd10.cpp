// SND10 --check-snd10: a bot running 10 m from the listener sounds a step every half gait cycle (the stride rate); a
// walking bot at 10 m (beyond walkRange_m) and a prone one are silent; a robot commando's step is the metal one
#include "selftest.h"
#include "game.h"

float advanceStride(App& app, Soldier& s, float v, float dt);   // game.cpp
extern int gBotSteps;

bool snd10SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-snd10") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  int fails = 0;
  auto chk = [&](bool ok, const std::string& w) { logf("[snd10] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; };
  a.newMap(*a.findTemplate("blank_slate"), "SND10 check");
  vec3 ear(0, a.terrain.height(0, 0) + 1.7f, 0);
  a.audio.setListener(ear, vec3(0, 0, -1));
  auto run = [&](float v, int posture, float dist, int kit, std::string* last) {
    Soldier s; s.alive = true; s.localPlayer = false; s.posture = posture; s.kit = (uint8_t)kit; s.slot = 3;
    s.feet = vec3(dist, a.terrain.height(dist, 0), 0);
    int n0 = gBotSteps; const float h = 1.0f / 60.0f;
    for (int k = 0; k < 120; ++k) advanceStride(a, s, v, h);   // 2 s
    if (last) *last = a.audio.lastPlayed;
    return gBotSteps - n0;
  };
  std::string ev;
  extern float gaitCadence(float); float cad = gaitCadence(4.5f);   // AN2: the shared cadence (game.cpp)
  int nRun = run(4.5f, Physics::STAND, 10, 0, &ev), nWalk = run(1.4f, Physics::STAND, 10, 0, nullptr), nProne = run(1.0f, Physics::PRONE, 3, 0, nullptr);
  char b[200]; std::snprintf(b, sizeof b, "running bot (4.5 m/s) at 10 m: %d steps in 2 s, stride rate %.1f /s expected %.1f (%s)", nRun, nRun / 2.0f, 2 * cad, ev.c_str());
  chk(std::abs(nRun - (int)std::lround(4 * cad)) <= 1 && ev.rfind("step_", 0) == 0, b);
  chk(nWalk == 0, "walking bot at 10 m (past the 8 m walk range): " + std::to_string(nWalk) + " steps");
  chk(nProne == 0, "prone bot at 3 m: " + std::to_string(nProne) + " steps");
  int nBot = run(4.5f, Physics::STAND, 5, 3, &ev);
  chk(nBot > 0 && ev == "step_metal", "robot commando step: " + ev + " (" + std::to_string(nBot) + ")");
  logf("[snd10] done: %d failures", fails);
  a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
  return true;
}
