// GP1 --selftest-playerdmg: the local player is hurt and killed by bot fire in every mode. Each mode starts as the setup
// screen starts it (gUi.matchScenario + pendingStart + enterExplore); the nearest hostile bot is held 9 m in front of the
// player once (then it acts on its own), the player standing still, until the player dies (or 40 s). Logged per mode: shots by bots, the player's hp, and where
// hits on the player were dropped (Game::damage counters). selftest_out/report_playerdmg.txt.
#include "selftest.h"
#include "game.h"
#include "settings.h"
#include "character.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "sd_mode.h"
#include "sd_game.h"
#include <cstdlib>

extern int gPlayerDmgBlock[8];

namespace {
MpTest* T = nullptr;
int mode = -1, cur = -1; double t0 = 0; float st = 0; bool begun = false; int shots0 = 0, onMe = 0, frames = 0; std::string heldInfo;
struct Case { const char* name; GameMode m; const char* map; };
const Case kCases[] = {{"Team Deathmatch", GameMode::TDM, "examples/village_assault_3.nmap.json"}, {"Solo", GameMode::Solo, "examples/village_assault_3.nmap.json"},
                       {"Assault", GameMode::Assault, "examples/village_assault_3.nmap.json"}, {"Search and Destroy", GameMode::SearchDestroy, "templates/chateau_town.nmap.json"},
                       {"Team Deathmatch, a non-soldier body (Character screen)", GameMode::TDM, "examples/village_assault_3.nmap.json"}};   // GP1: the user's alien body made the player unhittable

bool start(App& a, const Case& c) {
  static std::string saved = settings().s.character;
  settings().s.character = std::string(c.name).find("body") != std::string::npos ? "troll" : saved;   // a non-soldier rig as the player's body (the repo's stand-in for the alien)
  if (const char* body = std::getenv("NMK_PD_BODY")) settings().s.character = body;
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / c.map);
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = c.m; sc.rules.killCap = 999; sc.rules.soloPlayers = 4; sc.rules.teamPlayers[0] = 3; sc.rules.teamPlayers[1] = 3; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
  for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
  if (c.m == GameMode::Assault) { sc.rules.defenderSlots = 3; sc.rules.attackerAI = 3; }
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
}   // namespace

bool playerDmgSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-playerdmg") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("playerdmg");
  st += dt;
  if (cur < 0 || (begun && st > 40)) {   // next case
    if (cur >= 0 && begun) {
      Game& g = *gGame; Soldier* L = g.local();
      int shots = 0; for (auto& s : g.sold) if (!s.localPlayer) shots += s.shots;
      char b[300]; std::snprintf(b, sizeof b, "%s: the player %s (hp %.0f) under bot fire; %d bot shots; hits on the player: %d landed, dropped by protection %d, admin god %d, tank %d / %d, friendly fire %d, SD freeze %d, SD armor %d",
        kCases[cur].name, L && !L->alive ? "died" : "survived", L ? L->hp : -1.0f, shots - shots0, gPlayerDmgBlock[7], gPlayerDmgBlock[0], gPlayerDmgBlock[1], gPlayerDmgBlock[2], gPlayerDmgBlock[3], gPlayerDmgBlock[4], gPlayerDmgBlock[5], gPlayerDmgBlock[6]);
      T->check(L && (!L->alive || L->hp < 100), b);
      T->note("the held bot had the player as its target in " + std::to_string(onMe) + " of " + std::to_string(frames) + " frames; " + heldInfo);
    }
    ++cur; begun = false; st = 0;
    { static std::string only; static bool read = false; if (!read) { read = true; for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--selftest-playerdmg" && __argv[k + 1][0] != '-') only = __argv[k + 1]; }
      while (!only.empty() && cur < (int)(sizeof kCases / sizeof kCases[0]) && std::string(kCases[cur].name).find(only) == std::string::npos) ++cur; }
    if (cur >= (int)(sizeof kCases / sizeof kCases[0])) {
      if (app.mode == Mode::Explore) app.exitExplore();
      settings().s.character.clear();   // never left as the test's body
      app.selftest->exitCode = T->finish(); app.selftest->done = true; return true;
    }
    for (int& x : gPlayerDmgBlock) x = 0;
    bool ok = start(app, kCases[cur]);
    T->check(ok, std::string(kCases[cur].name) + ": the match starts (" + (gGame ? gGame->setupError : std::string("-")) + ")");
    if (!ok) { begun = true; st = 1000; }
    return true;
  }
  Game& g = *gGame; Soldier* L = g.local();
  if (!L || !L->alive) {
    if (begun && L && !L->alive) st = 1000;   // died: done with this case
    if (!begun && st > 30) { std::string ph = sdMatch() ? sdPhaseName(sdMatch()->phase) : "-"; T->check(false, std::string(kCases[cur].name) + ": the player never spawned in 30 s (SD phase " + ph + ", slot " + std::to_string(g.localSlot) + ", pending " + std::to_string(L && g.localSlot >= 0 ? (int)g.match.slots[g.localSlot].pending : -1) + ", deployReady " + std::to_string((int)g.deployReady) + ")"); begun = true; st = 1000; }
    return true;
  }
  if (!begun) { if (SdMatch* m = sdMatch()) { m->cfg.freeze = 1; } begun = true; st = 0; shots0 = 0; onMe = frames = 0; for (auto& s : g.sold) if (!s.localPlayer) shots0 += s.shots; }
  L->protectUntil = -1;   // past the spawn protection at once: what is left must be the real damage path
  // the nearest hostile bot, held 9 m in front of the player
  Soldier* e = nullptr; float bd = 1e9f;
  for (auto& o : g.sold) if (o.alive && !o.localPlayer && Game::hostile(o, *L)) { float d = glm::length(o.feet - L->feet); if (d < bd) { bd = d; e = &o; } }
  if (e && e->agent >= 0) {
    vec3 f(-std::sin(radians(app.eyeYaw)), 0, -std::cos(radians(app.eyeYaw)));
    vec3 p = L->feet + f * 9.0f; p.y = app.terrain.height(p.x, p.z) + 0.05f;
    if (frames == 0 || glm::length(e->feet - L->feet) > 30.0f) { Physics::setAgentPos(e->agent, p); e->feet = p; }   // placed once (and again only if it wanders off): then it acts on its own
    ++frames; onMe += e->br.target == L->slot;
    { int mem = (int)e->br.mem.size(), vis = 0; for (auto& x : e->br.mem) vis += x.enemy == L->slot && x.visible;
      bool los = g.lineOfSight(g.eye(*e), g.chest(*L), nullptr); vec3 cp = Physics::character().pos;
      vec3 hj = vec3(L->world * vec4(L->pose.joint[CJ_HEAD], 1)), hip = vec3(L->world * vec4(L->pose.joint[CJ_HIPS], 1));
      if (frames == 200) T->note("player body model " + std::to_string(L->model) + ": posed head at " + std::to_string(hj.y - L->feet.y) + " m over the feet, " + std::to_string(glm::length(vec2(hj.x - L->feet.x, hj.z - L->feet.z))) + " m off to the side; hips at " + std::to_string(hip.y - L->feet.y) + " m");
      char b[400]; std::snprintf(b, sizeof b, "held bot %d team %d role %d (player team %d role %d), target %d, why '%s', %.1f m; LOS %d, memory %d (player visible %d); player feet y %.1f, terrain %.1f, character y %.1f; SD phase %s, frozen %d, bot speed %.1f",
        e->slot, e->team, (int)e->role, L->team, (int)L->role, e->br.target, e->br.why.c_str(), glm::length(e->feet - L->feet), (int)los, mem, vis, L->feet.y, app.terrain.height(L->feet.x, L->feet.z), cp.y,
        sdMatch() ? sdPhaseName(sdMatch()->phase) : "-", (int)sdFrozen(), e->speed); heldInfo = b; }
  }
  return true;
}
