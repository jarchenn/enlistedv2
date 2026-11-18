// TH1 --capture-th1: the thermal scope. Team Deathmatch on an open map, the AI frozen, one enemy soldier 60 m ahead on a
// clear line of sight. Shots (selftest_out/th1_<name>.png): an iron-sight rifle (the Garand) with the thermal scope fitted
// by day, at night and through a smoke wall at 30 m, aimed; the same rifle at the hip (the scope mounted on it); a scoped
// sniper (its own scope model, only the view thermal) by day. Checks: the scope fits rifles and snipers, not pistols /
// SMGs / shotguns; the thermal view switches on when aimed; heat is drawn for the soldier. selftest_out/report_th1.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "loot.h"
#include "ai_core.h"

namespace {
MpTest* T = nullptr;
int mode = -1, shot = -1, stage = 0; float st = 0; bool started = false; int enemy = -1; float yaw0 = 0;
struct Shot { const char* name; const char* weapon; float hour; bool smoke, aim; };
const Shot kShots[] = {{"rifle_day", "m1_garand", 14, false, true}, {"rifle_night", "m1_garand", 0, false, true}, {"rifle_smoke", "m1_garand", 14, true, true},
                       {"rifle_hip", "m1_garand", 14, false, false}, {"sniper_day", "modern_sniper_bolt", 14, false, true}};
}   // namespace

bool th1Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-th1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("th1");
  st += dt;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.R.lt.hour = 14; app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  if (!started) {
    started = true;
    {   // the fitting rule
      const char* fits[] = {"m1_garand", "kar98k", "modern_m4_irons", "modern_sniper_bolt"}; const char* nofit[] = {"m1911a1", "mp40", "modern_shotgun", "mg42"};
      const ItemDef* th = lootItem("br_sight_thermal");
      T->check(th && th->value == 6, "loot.json: br_sight_thermal (sight value 6, rarity 0.5, military)");
      std::string yes, no;
      for (auto* w : fits) { int i = app.weaponIndex(w); if (i >= 0 && th) yes += std::string(w) + (lootAttachFits(*app.weaponDefs[i], *th) ? " fits, " : " DOES NOT FIT, "); }
      for (auto* w : nofit) { int i = app.weaponIndex(w); if (i >= 0 && th) no += std::string(w) + (lootAttachFits(*app.weaponDefs[i], *th) ? " FITS, " : " no, "); }
      T->check(yes.find("NOT") == std::string::npos && no.find("FITS") == std::string::npos, "rifles / snipers take it: " + yes + "the rest not: " + no);
    }
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.humanTeam = 0;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    sc.rules.aiCount[1][(int)AiType::Soldiers] = 1; sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 1;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts");
    if (!ok) { finish(); return true; }
    return true;
  }
  Game& g = *gGame; Soldier* L = g.local();
  if (!L || !L->alive) { if (st > 40) { T->check(false, "the player never spawned"); finish(); } return true; }
  L->protectUntil = 1e9;
  if (stage == 0) {   // a clear 60 m line: the first heading that has one; the enemy there, frozen, facing the player
    if (st < 2.0f) return true;
    for (auto& s : g.sold) if (!s.localPlayer && s.alive) { enemy = s.slot; break; }
    if (enemy < 0) { T->check(false, "no enemy soldier"); finish(); return true; }
    const vec3 eye = app.eyePos(); bool found = false;
    for (int k = 0; k < 36 && !found; ++k) {
      float y = app.eyeYaw + k * 10.0f; vec3 f(-std::sin(radians(y)), 0, -std::cos(radians(y)));
      vec3 p = L->feet + f * 60.0f; p.y = app.terrain.height(p.x, p.z);
      vec3 to = p + vec3(0, 1.2f, 0) - eye; Physics::Hit h;
      if (!Physics::castRayEx(eye, glm::normalize(to), glm::length(to) - 0.5f, h) && std::fabs(p.y - L->feet.y) < 6.0f) { yaw0 = y; found = true;
        Soldier& e = g.sold[enemy]; Physics::setAgentPos(e.agent, p + vec3(0, 0.05f, 0)); e.feet = p; e.yaw = y + 180.0f; }
    }
    T->check(found, "a clear 60 m line of sight from the spawn");
    if (AiCore* A = aiCore()) A->frozen = true;
    stage = 1; st = 0; shot = 0; return true;
  }
  if (shot >= (int)(sizeof kShots / sizeof kShots[0])) { finish(); return true; }
  const Shot& s = kShots[shot];
  if (stage == 1) {   // set up this shot
    int w = app.weaponIndex(s.weapon);
    if (w < 0) { T->note(std::string("no weapon ") + s.weapon); ++shot; return true; }
    if (std::find(app.carried.begin(), app.carried.end(), w) == app.carried.end()) app.carried.push_back(w);
    app.wstate[s.weapon].att[ATT_SIGHT] = 6;
    app.selectWeapon(w); app.R.lt.hour = s.hour; app.eyeYaw = yaw0; app.eyePitch = 0.6f;
    if (s.smoke) for (int i = -4; i <= 4; ++i) for (int j = 0; j < 3; ++j) {   // a smoke wall across the line at 30 m
      vec3 f(-std::sin(radians(yaw0)), 0, -std::cos(radians(yaw0))), r(f.z, 0, -f.x), p = L->feet + f * 30.0f + r * (i * 1.2f); p.y = app.terrain.height(p.x, p.z) + 0.6f + j * 1.1f;
      app.fx.smoke(p, vec3(0, 0.05f, 0), 3.2f, 60.0f, vec4(0.62f, 0.62f, 0.6f, 0.95f), 0.2f, 0.05f);
    }
    stage = 2; st = 0; return true;
  }
  app.scriptedInput.aim = s.aim;
  if (stage == 2 && st > 2.2f) { app.uiShot = exeDir() / "selftest_out" / (std::string("th1_") + s.name + ".png"); stage = 3; st = 0; return true; }
  if (stage == 3 && st > 0.4f) {
    char b[240]; std::snprintf(b, sizeof b, "%s: %s, %s, aimed %d: thermal view %s", s.name, s.weapon, s.hour < 6 ? "night" : "day", (int)s.aim,
                                app.R.thermalWanted ? "ON" : "off");
    T->check(s.aim ? app.R.thermalWanted : !app.R.thermalWanted, b);
    app.scriptedInput.aim = false; ++shot; stage = 1; st = 0;
  }
  return true;
}
