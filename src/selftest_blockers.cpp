// IW1 --blocker-scan [map]: every 0.5 m cell of the play area (rules.sd.bounds, else the map) gets a standing player
// capsule on the ground; a cell the capsule does not fit in (terrain ignored) is written with the body that blocks it:
// selftest_out/blockers_<map>.json {"cells": [[x, z, ground, instanceId, sub], ...], "assets": {id: assetId}}. Whether
// the blocker draws anything there is judged offline against the asset's mesh (a collider with nothing drawn = an
// invisible wall). Starts the map in Search and Destroy as --sd-timings does, so the bodies are the match's.
#include "selftest.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "physics.h"
#include <fstream>

bool blockerScan(App& app, float dt) {
  static int mode = -1, frames = 0; static bool started = false; static std::string map;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--blocker-scan") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; map = "templates/sd_village.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--blocker-scan" && __argv[k + 1][0] != '-') map = __argv[k + 1];
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / map);
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 1; }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    app.enterExplore();
    return true;
  }
  if (++frames < 20) return true;
  json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
  const json sdj = scj.value("rules", json::object()).value("sd", json::object());
  float x0 = -std::max(app.doc.width, 100.0f) * 0.5f, x1 = -x0, z0 = -std::max(app.doc.depth, 100.0f) * 0.5f, z1 = -z0;
  if (sdj.contains("bounds")) {
    x0 = z0 = 1e9f; x1 = z1 = -1e9f;
    for (auto& p : sdj["bounds"]) { x0 = std::min(x0, p[0].get<float>()); x1 = std::max(x1, p[0].get<float>()); z0 = std::min(z0, p[1].get<float>()); z1 = std::max(z1, p[1].get<float>()); }
  }
  json cells = json::array(), assets = json::object();
  int n = 0;
  for (float z = z0; z <= z1; z += 0.5f)
    for (float x = x0; x <= x1; x += 0.5f) {
      float g = app.terrain.height(x, z); ++n;
      uint64_t ud = 0;   // blocked: no fit from the ground up to the character's 0.35 m step (a floor slab or a kerb is walkable)
      for (float dy = 0.0f; dy <= 0.351f; dy += 0.05f) { ud = Physics::shapeHitUD(Physics::STAND, vec3(x, g + dy, z), false); if (!ud) break; }
      if (!ud) continue;
      uint64_t id = ud >> 16;
      cells.push_back({x, z, g, id, (uint32_t)(ud & 0xffffu)});
      const Instance* in = app.doc.find(id);
      if (in && !assets.contains(std::to_string(id))) assets[std::to_string(id)] = in->assetId;
    }
  std::string tid = fs::path(map).filename().string(); tid = tid.substr(0, tid.find('.'));
  fs::path out = exeDir() / "selftest_out" / ("blockers_" + tid + ".json");
  fs::create_directories(out.parent_path());
  json inst = json::object();   // every instance at run time (runtime dressing and furniture are not in the map file)
  for (auto& i : app.doc.instances) inst[std::to_string(i.id)] = {i.assetId, i.pos.x, i.pos.y, i.pos.z, i.yaw};
  std::ofstream(out) << json{{"map", map}, {"cells", cells}, {"assets", assets}, {"instances", inst}, {"scanned", n}}.dump();
  logf("blocker-scan %s: %d cells, %d blocked -> %s", map.c_str(), n, (int)cells.size(), out.string().c_str());
  app.selftest->exitCode = 0; app.selftest->done = true;
  return true;
}
