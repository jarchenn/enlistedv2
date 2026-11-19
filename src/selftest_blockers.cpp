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
#include "lines.h"
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
  // walk-through: every door and gate open, the player character driven at a jog through every ground-floor building
  // entrance (nav "entrances", dir = inward) and every line gate; a passage it cannot cross (a solid opening, a step
  // over the 0.35 m step-up, a collider wider than the drawn frame) is listed
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !it->second.asset) continue;
    InstanceRT& r = it->second;
    for (size_t k = 0; k < r.asset->interactables.size() && k < r.progress.size(); ++k) { r.progress[k] = 1.0f; app.updateLeafBodies(i, r, (int)k); }
  }
  json walks = json::array(); int nWalk = 0, nFail = 0;
  auto walk = [&](const char* kind, const Instance& i, vec2 p, vec2 d) {
    d = glm::normalize(d);
    vec2 s = p - d * 1.5f, t = p + d * 0.8f;
    Physics::setCharacterPos(vec3(s.x, app.terrain.height(s.x, s.y) + 0.05f, s.y));
    float best = 0;
    for (int f = 0; f < 150; ++f) {
      Physics::updateCharacter(1.0f / 60.0f, vec3(d.x, 0, d.y) * 2.7f, false, false);
      vec3 c = Physics::character().pos; best = std::max(best, glm::dot(vec2(c.x, c.z) - s, d));
    }
    bool ok = best >= glm::length(t - s) - 0.3f; ++nWalk; nFail += !ok;
    vec3 c = Physics::character().pos;
    walks.push_back({{"kind", kind}, {"id", i.id}, {"asset", i.assetId}, {"p", {p.x, p.y}}, {"dir", {d.x, d.y}}, {"ok", ok}, {"reached", best}, {"end", {c.x, c.y, c.z}}});
  };
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !it->second.asset) continue;
    const InstanceRT& r = it->second;
    if (std::fabs(i.pos.x) > x1 || std::fabs(i.pos.z) > z1) continue;
    quat q = yawQuat(i.yaw);
    for (auto& e : r.asset->navMeta.entrances) {
      if (e.p.y > 1.0f) continue;   // upper-floor entrances (an outside stair) are not ground walks
      vec3 w = i.pos + q * e.p, dw = q * e.dir;
      if (glm::length(vec2(dw.x, dw.z)) < 1e-3f) continue;
      walk("entrance", i, vec2(w.x, w.z), vec2(dw.x, dw.z));
    }
    if (r.line) for (auto& g : r.line->gates) {
      vec2 a = vec2(i.pos.x, i.pos.z) + g.a, b = vec2(i.pos.x, i.pos.z) + g.b, m = (a + b) * 0.5f, n(-(b - a).y, (b - a).x);
      if (glm::length(n) < 1e-3f) continue;
      walk("gate", i, m, n); walk("gate", i, m, -n);
    }
  }
  // stairs: in each enterable building, the character steered along the navmesh path from a ground-floor room to every
  // upper room; an upper room it cannot reach (a riser over the step-up, a tread whose collider sticks out) is listed
  int nStair = 0, nStairFail = 0;
  if (gGame) for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !it->second.asset || !it->second.asset->enterable) continue;
    if (std::fabs(i.pos.x) > x1 || std::fabs(i.pos.z) > z1) continue;
    const auto& rooms = it->second.asset->navMeta.rooms; quat q = yawQuat(i.yaw);
    auto centre = [&](const NavRoom& r) { vec3 c = (r.min + r.max) * 0.5f; c.y = r.min.y; return i.pos + q * c; };
    const NavRoom* g0 = nullptr;
    for (auto& r : rooms) if (r.min.y < 1.0f && (!g0 || (r.max.x - r.min.x) * (r.max.z - r.min.z) > (g0->max.x - g0->min.x) * (g0->max.z - g0->min.z))) g0 = &r;
    if (!g0) continue;
    for (auto& r : rooms) {
      if (r.min.y < 2.0f) continue;
      vec3 a = centre(*g0), b = centre(r);
      std::vector<vec3> pts; float len = 0;
      bool path = gGame->nav.pathSync(a, b, pts, nullptr, nullptr, &len) && !pts.empty() && glm::length(pts.back() - b) < 1.5f;
      bool ok = false; vec3 c(0);
      if (path) {
        Physics::setCharacterPos(a + vec3(0, 0.05f, 0));
        size_t k = 0;
        for (int f = 0; f < 60 * 25 && k < pts.size(); ++f) {
          c = Physics::character().pos;
          vec2 d = vec2(pts[k].x - c.x, pts[k].z - c.z);
          if (glm::length(d) < 0.35f && std::fabs(pts[k].y - c.y) < 0.8f) { ++k; continue; }
          Physics::updateCharacter(1.0f / 60.0f, glm::length(d) > 1e-3f ? vec3(d.x, 0, d.y) / glm::length(d) * 2.7f : vec3(0), false, false);
        }
        c = Physics::character().pos;
        ok = glm::length(vec2(c.x - b.x, c.z - b.z)) < 1.0f && std::fabs(c.y - b.y) < 0.6f;
      }
      ++nStair; nStairFail += path && !ok;
      walks.push_back({{"kind", path ? "stairs" : "stairs (no nav path)"}, {"id", i.id}, {"asset", i.assetId}, {"room", r.id}, {"p", {b.x, b.z}}, {"ok", ok || !path}, {"reached", 0}, {"end", {c.x, c.y, c.z}}, {"to", {b.x, b.y, b.z}}});
    }
  }
  logf("blocker-scan walks: %d, %d blocked; upper rooms %d, %d not reached", nWalk, nFail, nStair, nStairFail);
  json inst = json::object();   // every instance at run time (runtime dressing and furniture are not in the map file)
  for (auto& i : app.doc.instances) inst[std::to_string(i.id)] = {i.assetId, i.pos.x, i.pos.y, i.pos.z, i.yaw};
  std::ofstream(out) << json{{"map", map}, {"cells", cells}, {"assets", assets}, {"instances", inst}, {"walks", walks}, {"scanned", n}}.dump();
  logf("blocker-scan %s: %d cells, %d blocked -> %s", map.c_str(), n, (int)cells.size(), out.string().c_str());
  app.selftest->exitCode = 0; app.selftest->done = true;
  return true;
}
