// TH2 --check-th2: the thermal scope sees only what the eye sees. Team Deathmatch (village_assault_3, the AI frozen), the
// player's Garand with the thermal scope, aimed: one enemy 30 m away in the open (the reference count of hot pixels),
// then behind a tall wall (zero hot pixels), behind a low wall (only the part above it: some pixels, none below the wall
// top), behind a terrain crest (a spot found on the map: zero). Then captures: one soldier at 15 m turned three-quarters,
// a group of three at 60 m. selftest_out/th2_<case>.png, selftest_out/report_th2.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "loot.h"
#include "ai_core.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0; float st = 0; bool started = false; float yaw0 = 0; int ref = 0; uint64_t wallId = 0; vec3 home(0);
std::vector<int> foes; const AssetDef* tall = nullptr; const AssetDef* low = nullptr;
vec3 dirOf(float y) { return vec3(-std::sin(radians(y)), 0, -std::cos(radians(y))); }
void putFoe(App& a, Game& g, int i, vec3 p, float yaw) { Soldier& e = g.sold[foes[i]]; p.y = a.terrain.height(p.x, p.z); Physics::setAgentPos(e.agent, p + vec3(0, 0.05f, 0)); e.feet = p; e.yaw = yaw; }
void aimAt(App& a, vec3 p) { vec3 d = p - a.eyePos(); a.eyeYaw = degrees(std::atan2(-d.x, -d.z)); a.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
void dropWall(App& a) { if (!wallId) return; auto& v = a.doc.instances; v.erase(std::remove_if(v.begin(), v.end(), [](const Instance& i) { return i.id == wallId; }), v.end()); wallId = 0; a.syncRuntime(); }
float wallH(const AssetDef* d) { return d ? d->bmax.y - d->bmin.y : 0; }
}   // namespace

bool th2Check(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-th2") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("th2");
  st += dt;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.R.lt.hour = 14; app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; };
  auto shot = [&](const char* n) { app.uiShot = exeDir() / "selftest_out" / (std::string("th2_") + n + ".png"); };
  if (!started) {
    started = true;
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.humanTeam = 0;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    sc.rules.aiCount[1][(int)AiType::Soldiers] = 3; sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 3;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts");
    std::vector<std::string> ids; for (auto& a : app.lib.assets) if (a->id.find("wall") != std::string::npos && !a->enterable && a->footprint.x >= 2.5f) ids.push_back(a->id);
    for (auto& id : ids) {   // a tall wall (hides a standing man) and a low one (about waist high)
      const AssetDef* a = app.lib.find(id); if (!a) continue;
      const float h = wallH(a);
      if (h > 2.2f && h < 4.5f && (!tall || h < wallH(tall))) tall = a;
      if (h > 0.8f && h < 1.25f && (!low || h > wallH(low))) low = a;
    }
    T->check(tall && low, std::string("walls: tall ") + (tall ? tall->id + " " + std::to_string(wallH(tall)) + " m" : "none") + ", low " + (low ? low->id + " " + std::to_string(wallH(low)) + " m" : "none"));
    return ok ? true : finish();
  }
  Game& g = *gGame; Soldier* L = g.local();
  if (!L || !L->alive) { if (st > 40) { T->check(false, "the player never spawned"); return finish(); } return true; }
  L->protectUntil = 1e9;
  if (AiCore* A = aiCore()) A->frozen = true;
  char b[300];
  if (stage == 0) {
    if (st < 2.0f) return true;
    for (auto& s : g.sold) if (!s.localPlayer && s.alive) foes.push_back(s.slot);
    if (foes.size() < 3) { T->check(false, "three enemies"); return finish(); }
    int w = app.weaponIndex("m1_garand");
    if (std::find(app.carried.begin(), app.carried.end(), w) == app.carried.end()) app.carried.push_back(w);
    app.wstate["m1_garand"].att[ATT_SIGHT] = 6; app.selectWeapon(w);
    const vec3 eye = app.eyePos(); bool found = false;
    for (int k = 0; k < 36 && !found; ++k) {   // a clear, level 30 m line
      float y = app.eyeYaw + k * 10.0f; vec3 p = L->feet + dirOf(y) * 30.0f; p.y = app.terrain.height(p.x, p.z);
      vec3 to = p + vec3(0, 1.2f, 0) - eye; Physics::Hit h;
      if (!Physics::castRayEx(eye, glm::normalize(to), glm::length(to) - 0.5f, h) && std::fabs(p.y - L->feet.y) < 2.0f) { yaw0 = y; found = true; }
    }
    T->check(found, "a clear 30 m line from the spawn");
    if (!found) return finish();
    for (size_t i = 0; i < foes.size(); ++i) putFoe(app, g, (int)i, L->feet + dirOf(yaw0) * (30.0f + 40.0f * (float)i) + vec3(0, 0, 200.0f * (float)i), yaw0 + 180.0f);   // one ahead, the others far off
    app.R.lt.hour = 14; app.scriptedInput.aim = true; home = L->feet;
    stage = 1; st = 0; return true;
  }
  const Soldier& F = g.sold[foes[stage >= 8 ? 1 : 0]];
  aimAt(app, F.feet + vec3(0, 1.1f, 0)); app.scriptedInput.aim = true;
  if (stage == 1 && st > 2.5f) {   // the open reference
    Renderer::HeatStats h = app.R.heatStats(0.3f); ref = h.hot;
    std::snprintf(b, sizeof b, "in the open at 30 m: %d hot pixels (rows %d-%d), thermal view %s", h.hot, h.y0, h.y1, app.R.thermalWanted ? "on" : "OFF");
    T->check(h.hot > 40 && app.R.thermalWanted, b); shot("open30");
    const vec3 wp = F.feet - dirOf(yaw0) * 1.4f;   // a tall wall 1.4 m in front of him, across the line
    wallId = tall ? app.placeInstance(tall, vec3(wp.x, app.terrain.height(wp.x, wp.z), wp.z), yaw0) : 0; app.syncRuntime();
    stage = 2; st = 0; return true;
  }
  if (stage == 2 && st > 1.5f) {
    Renderer::HeatStats h = app.R.heatStats(0.3f);
    std::snprintf(b, sizeof b, "behind the tall wall (%s): %d hot pixels (open: %d)", tall ? tall->id.c_str() : "-", h.hot, ref);
    T->check(wallId && h.hot == 0, b); shot("wall_tall");
    dropWall(app);
    const vec3 wp = F.feet - dirOf(yaw0) * 1.4f;
    wallId = low ? app.placeInstance(low, vec3(wp.x, app.terrain.height(wp.x, wp.z), wp.z), yaw0) : 0; app.syncRuntime();
    stage = 3; st = 0; return true;
  }
  if (stage == 3 && st > 1.5f) {
    Renderer::HeatStats h = app.R.heatStats(0.3f);
    const vec3 wp = F.feet - dirOf(yaw0) * 1.4f; const vec4 c = app.projM * app.viewM * vec4(wp.x, app.terrain.height(wp.x, wp.z) + wallH(low), wp.z, 1);
    const int topRow = (int)((0.5f - 0.5f * c.y / c.w) * (float)h.h);   // the wall's top edge on screen (rows from the top)
    std::snprintf(b, sizeof b, "behind the low wall (%s, %.2f m): %d hot pixels = %.0f %% of the open view, lowest hot row %d, the wall top at row %d",
                  low ? low->id.c_str() : "-", wallH(low), h.hot, ref ? 100.0 * h.hot / ref : 0.0, h.y1, topRow);
    T->check(wallId && ref && h.hot > ref * 0.15f && h.hot < ref * 0.85f && h.y1 <= topRow + 3, b); shot("wall_low");
    dropWall(app);
    stage = 4; st = 0; return true;
  }
  if (stage == 4) {   // a terrain crest: from a sample of spots, the first where the ground between hides a man 30 m off
    bool found = false; vec3 P(0), Q(0);
    const float half = 220.0f;
    for (int gx = -10; gx <= 10 && !found; ++gx) for (int gz = -10; gz <= 10 && !found; ++gz) for (int k = 0; k < 8 && !found; ++k) {
      vec3 p(gx * half / 10, 0, gz * half / 10); p.y = app.terrain.height(p.x, p.z);
      vec3 q = p + dirOf(k * 45.0f) * 30.0f; q.y = app.terrain.height(q.x, q.z);
      if (std::fabs(q.y - p.y) > 6) continue;
      const vec3 e = p + vec3(0, 1.6f, 0), head = q + vec3(0, 1.8f, 0);
      bool hidden = false;
      for (int i = 3; i < 28 && !hidden; ++i) { vec3 s = glm::mix(e, head, i / 30.0f); if (app.terrain.height(s.x, s.z) > s.y + 0.4f) hidden = true; }
      Physics::Hit h; vec3 to = head - e;
      if (!hidden || !Physics::castRayEx(e, glm::normalize(to), glm::length(to), h) || std::fabs(h.pos.y - app.terrain.height(h.pos.x, h.pos.z)) > 0.3f) continue;   // the first hit is the ground
      bool clear = true; for (auto& i : app.doc.instances) if (glm::length(vec2(i.pos.x - (p.x + q.x) * 0.5f, i.pos.z - (p.z + q.z) * 0.5f)) < 18.0f) clear = false;   // nothing built near the line
      if (clear) { P = p; Q = q; found = true; }
    }
    T->check(found, "a terrain crest spot on the map" + (found ? " at (" + std::to_string((int)P.x) + ", " + std::to_string((int)P.z) + ")" : std::string()));
    if (!found) { stage = 6; st = 0; return true; }
    Physics::setCharacterPos(P + vec3(0, 1.0f, 0)); putFoe(app, g, 0, Q, 0);
    stage = 5; st = 0; return true;
  }
  if (stage == 5 && st > 2.0f) {
    Renderer::HeatStats h = app.R.heatStats(0.3f);
    std::snprintf(b, sizeof b, "behind a terrain crest at 30 m: %d hot pixels", h.hot);
    T->check(h.hot == 0 && app.R.thermalWanted, b); shot("terrain");
    stage = 6; st = 0; return true;
  }
  if (stage == 6) {   // captures from the first line: one man at 15 m turned three-quarters, then a group at 60 m
    Physics::setCharacterPos(home + vec3(0, 1.0f, 0));
    vec3 base = home;
    putFoe(app, g, 0, base + dirOf(yaw0) * 15.0f, yaw0 + 180.0f - 45.0f);
    for (size_t i = 1; i < foes.size(); ++i) putFoe(app, g, (int)i, base + dirOf(yaw0) * 300.0f + vec3(0, 0, 50.0f * i), 0);
    stage = 7; st = 0; return true;
  }
  if (stage == 7 && st > 2.0f) { shot("man15"); stage = 8; st = 0; return true; }
  if (stage == 8 && st > 0.5f) {
    vec3 base = home; const vec3 side(dirOf(yaw0).z, 0, -dirOf(yaw0).x);
    for (size_t i = 0; i < foes.size(); ++i) putFoe(app, g, (int)i, base + dirOf(yaw0) * 60.0f + side * (1.6f * ((float)i - 1.0f)), yaw0 + 180.0f + 30.0f * ((float)i - 1.0f));
    stage = 9; st = 0; return true;
  }
  if (stage == 9 && st > 2.0f) { shot("group60"); stage = 10; st = 0; return true; }
  if (stage == 10 && st > 0.5f) return finish();
  return true;
}
