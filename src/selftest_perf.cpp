// --profile-map <template id> <AI count> <seconds>: CPU frame profile of one built-in map (PF3). Opens the map the way
// New Map > "Create editable copy" does, enters Explore at the A1 spawn (AI count 0: the player walks forward, turning
// slowly) or starts a match with that many combatants, discards a 6 s warm-up and records <seconds>. Writes
// selftest_out/profile_<id>_<ai>.json: load ms, enter-Explore ms, frame avg / p95 and per-crumb section avg / p95 ms.
// Run with NMK_FRAME_PROF=1 (sections come from crumb() marks, app.cpp) and the GPU exclusive (tools/gpu_turn.sh).
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "editor_nav.h"
#include <GLFW/glfw3.h>
#include <fstream>

json crumbProfReport(const char* tag);   // app.cpp

bool perfSelfTest(App& app, float dt) {
  static int mode = -1; static std::string tpl; static int ai = 0; static float secs = 30, t = 0; static json out;
  if (mode < 0) {
    mode = 0;
    for (int k = 1; k + 3 < __argc; ++k) if (std::string(__argv[k]) == "--profile-map") { mode = 1; tpl = __argv[k + 1]; ai = std::atoi(__argv[k + 2]); secs = (float)std::atof(__argv[k + 3]); }
  }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (t == 0) {
    const TemplateInfo* tp = app.findTemplate(tpl);
    if (!tp) { logf("[profile] no template %s", tpl.c_str()); app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    double t0 = glfwGetTime();
    navNewMapSized(app, *tp, "Profile " + tp->name, vec2(tp->width, tp->depth));
    out["loadMs"] = (glfwGetTime() - t0) * 1000; out["instances"] = app.doc.instances.size();
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json()) : json());
    vec3 sp = tp->camPos; float yaw = 180;
    for (auto& s : sc.spawns) if (s.role == CombatRole::Attacker && s.activeIn(0)) { sp = s.pos; break; }
    app.doc.start = {true, sp, yaw};
    if (ai > 0) {
      sc.rules.defenderSlots = ai / 2; sc.rules.attackerAI = ai - ai / 2;
      gUi.matchScenario = sc.toJson(); gUi.loaded = false;
      if (!gGame) gGame = new Game(app);
      gGame->pendingStart = true;
    }
    t0 = glfwGetTime();
    bool ok = app.enterExplore() && (ai == 0 || (gGame && gGame->active));
    out["enterExploreMs"] = (glfwGetTime() - t0) * 1000;
    if (!ok) { logf("[profile] could not enter Explore / start the match on %s", tpl.c_str()); app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    logf("[profile] %s loaded in %.0f ms (%zu instances), Explore / match start %.0f ms", tpl.c_str(), out["loadMs"].get<double>(), app.doc.instances.size(), out["enterExploreMs"].get<double>());
  }
  t += dt;
  if (ai == 0) { app.scriptedInput.move = vec2(0, 1); app.scriptedInput.dyaw = 0.25f; }   // a walk with a slow turn
  else { app.scriptedInput = ExploreInput{}; app.eyePitch = -2; app.eyeYaw += 0.15f; }
  if (t > 6.0f && !out.contains("warm")) { out["warm"] = crumbProfReport("warm-up"); }
  if (t > 6.0f + secs) {
    out["run"] = crumbProfReport(("run " + tpl + " ai " + std::to_string(ai)).c_str());
    if (ai > 0 && gGame) { int alive = 0; for (auto& s : gGame->sold) alive += s.alive; out["combatants"] = gGame->sold.size(); out["alive"] = alive; }
    fs::path d = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(d, ec);
    std::ofstream(d / ("profile_" + tpl + "_" + std::to_string(ai) + ".json")) << out.dump(2);
    if (app.mode == Mode::Explore) app.exitExplore();
    app.selftest->done = true;
  }
  return true;
}
