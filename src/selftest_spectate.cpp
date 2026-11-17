// GP1 --selftest-spectate: after death the spectated teammate's body always draws. Team Deathmatch (soldier, operator
// and trooper-kit bodies on the player's side) and Search and Destroy; the local player dies, its own eye is turned away from
// everyone (the old cull used it), and the spectate camera is set to each living teammate in turn: the body must reach
// the draw pass and the camera must sit at least 0.9 m from its head. Then the watched teammate dies: the camera must
// move to a living teammate. selftest_out/report_spectate.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "sd_mode.h"
#include "sd_game.h"

namespace {
MpTest* T = nullptr;
int mode = -1, cur = -1, stage = 0, idx = 0; float st = 0; std::vector<int> mates; int drawnOk = 0, closeOk = 0; int badFrames = 0, frames = 0; bool sawHalf = false;
bool finiteM(const mat4& m) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) if (!std::isfinite(m[c][r])) return false; return true; }
struct Case { const char* name; GameMode m; const char* map; };
const Case kCases[] = {{"Team Deathmatch", GameMode::TDM, "examples/village_assault_3.nmap.json"}, {"Search and Destroy", GameMode::SearchDestroy, "templates/chateau_town.nmap.json"}};
bool start(App& a, const Case& c) {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / c.map);
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = c.m; sc.rules.killCap = 999; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
  for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 5; }
  sc.rules.aiTypes[0] = aiBit(AiType::Soldiers) | (c.m == GameMode::TDM ? aiBit(AiType::Operators) | aiBit(AiType::Troopers) : 0); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
  sc.rules.aiCount[0][(int)AiType::Soldiers] = c.m == GameMode::TDM ? 2 : 4; if (c.m == GameMode::TDM) { sc.rules.aiCount[0][(int)AiType::Operators] = 1; sc.rules.aiCount[0][(int)AiType::Troopers] = 1; }
  sc.rules.aiCount[1][(int)AiType::Soldiers] = 5;
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
}   // namespace

bool spectateSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-spectate") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("spectate");
  st += dt;
  auto next = [&]() {
    ++cur; stage = 0; st = 0; idx = 0; mates.clear(); drawnOk = closeOk = 0;
    if (cur >= (int)(sizeof kCases / sizeof kCases[0])) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; return; }
    T->check(start(app, kCases[cur]), std::string(kCases[cur].name) + ": the match starts");
  };
  if (cur < 0) { next(); return true; }
  if (!gGame || !gGame->active) { next(); return true; }
  Game& g = *gGame; Soldier* L = g.local();
  const char* nm = kCases[cur].name;
  if (st > 240) { T->check(false, std::string(nm) + ": timed out at stage " + std::to_string(stage)); next(); return true; }
  if (stage == 0) {   // alive and live, then die
    if (!L || !L->alive || st < 3.0f) return true;
    if (SdMatch* m = sdMatch(); m && m->phase != SdPhase::LiveUnplanted) return true;
    g.killSoldier(L->slot, -1, vec3(0, 0, 1)); stage = 1; st = 0; return true;
  }
  if (stage == 1) {   // the teammates to watch
    if (st < 0.5f) return true;
    for (auto& s : g.sold) if (s.alive && !s.localPlayer && s.team == L->team) mates.push_back(s.slot);
    T->check(mates.size() >= 2, std::string(nm) + ": " + std::to_string(mates.size()) + " living teammates to watch");
    stage = 2; st = 0; idx = 0; return true;
  }
  if (stage == 2) {
    if (idx >= (int)mates.size()) {
      T->check(drawnOk == (int)mates.size() && closeOk == (int)mates.size(), std::string(nm) + ": every teammate's body drew while watched (" + std::to_string(drawnOk) + " of " + std::to_string(mates.size()) + "), the camera off its head (" + std::to_string(closeOk) + ")");
      stage = 3; st = 0; return true;
    }
    Soldier& w = g.sold[mates[idx]];
    if (!w.alive) { ++idx; ++drawnOk; ++closeOk; st = 0; return true; }   // died meanwhile (not this check's subject)
    g.spectate = w.slot;
    vec3 away = L->feet - w.feet; app.eyeYaw = degrees(std::atan2(-away.x, -away.z)); app.eyePitch = 0;   // the dead eye looks away from the watched one
    if (st > 0.6f) {
      bool drawn = g.t - w.drawnAt < 0.25; float dh = glm::length(g.specEye - (w.feet + vec3(0, 1.6f, 0)));
      drawnOk += drawn; closeOk += dh >= 0.9f;
      char b[200]; std::snprintf(b, sizeof b, "watching slot %d (model %d, kit %d): drawn %d, camera %.2f m from its head", w.slot, w.model, w.kit, (int)drawn, dh); T->note(b);
      ++idx; st = 0;
    }
    return true;
  }
  if (stage == 3) {   // the watched one dies: the camera moves to a living teammate
    int w = -1; for (int s : mates) if (g.sold[s].alive) { w = s; break; }
    if (w < 0) { T->note("no living teammate left for the switch check"); next(); return true; }
    g.spectate = w; g.killSoldier(w, -1, vec3(0, 0, 1)); stage = 4; st = 0; return true;
  }
  if (stage == 4 && st > 0.3f) {
    int s = g.spectate; bool ok = s >= 0 && s < (int)g.sold.size() && g.sold[s].alive && g.sold[s].team == L->team;
    bool any = false; for (auto& o : g.sold) any |= o.alive && !o.localPlayer && o.team == L->team;
    T->check(ok || !any, std::string(nm) + ": the watched teammate died, the camera moved to a living teammate (slot " + std::to_string(s) + ")");
    // the last teammate dies (SD: this round end is halftime: the sides swap while the player is dead)
    if (SdMatch* m = sdMatch()) { m->cfg.halfRounds = m->round; m->cfg.roundEnd = 1.5f; m->cfg.halftime = 2.0f; m->cfg.freeze = 2.0f; }
    for (auto& o : g.sold) if (o.alive && !o.localPlayer && o.team == L->team) g.killSoldier(o.slot, -1, vec3(0, 0, 1));
    stage = 5; st = 0; badFrames = frames = 0; sawHalf = false; return true;
  }
  if (stage == 5) {   // every frame until well into the next round: a finite camera
    ++frames; if (!finiteM(app.viewM) || !std::isfinite(g.specEye.x) || !std::isfinite(g.specEye.y) || !std::isfinite(g.specEye.z)) ++badFrames;
    SdMatch* m = sdMatch(); if (m && m->phase == SdPhase::Halftime) sawHalf = true;
    const bool done = m ? (m->round >= 2 && m->phase == SdPhase::LiveUnplanted && m->t - m->liveStart > 1.0) : st > 2.0f;
    if (done || st > 60) {
      T->check(badFrames == 0 && (!m || sawHalf) && done, std::string(nm) + ": the last teammate died" + (m ? ", halftime swapped the sides" : "") + " while the player was dead: " + std::to_string(badFrames) + " of " + std::to_string(frames) + " frames with a non-finite camera");
      next();
    }
  }
  return true;
}
