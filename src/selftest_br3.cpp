// --shots-backrooms (BR3): play-camera captures of the Backrooms map with characters in the room, for tuning its look
// (auto exposure, the first-person arms and weapon, a soldier and a zombie lit by the room). A Singleplayer match on
// templates/backrooms.nmap.json, paused after the spawn; the local player and two bots are held at fixed spots each frame.
// Spots: NMK_BR3_SHOTS = "name:x:z:tx:tz;name:..." (eye at x, z looking at tx, tz; the bots stand 4.5 m and 7 m along
// the view, 1.2 m either side). Shots: selftest_out/br3_<name>.png. No checks beyond "the match started".
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "physics.h"
#include "ai_hooks.h"
#include <cstdlib>
#include <cmath>
#include <sstream>

namespace {
int mode = -1;
float st = 0;
int stage = 0, shotIdx = 0;
fs::path pending;
struct Spot { std::string name; vec2 eye, at; };
std::vector<Spot> spots;
int b1 = -1, b2 = -1;

float yawToward(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
void place(Soldier& s, vec3 feet, vec3 face) {
  s.feet = feet; s.vel = vec3(0); s.speed = 0; s.yaw = yawToward(face); s.br.aimYaw = s.yaw; s.br.aimPitch = 0;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, feet);
}
void hold(App& app, const Spot& sp) {
  Game& G = *gGame;
  vec3 e(sp.eye.x, 0, sp.eye.y), t(sp.at.x, 0, sp.at.y), d = glm::normalize(t - e), side = glm::normalize(glm::cross(d, vec3(0, 1, 0)));
  Physics::setCharacterPos(e);
  if (Soldier* L = G.local()) { L->feet = e; L->hp = 100; }
  vec3 eye = e + vec3(0, 1.62f, 0), look = e + d * 6.0f + vec3(0, 1.3f, 0), v = look - eye;
  app.eyeYaw = yawToward(v); app.eyePitch = degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  if (b1 >= 0) { place(G.sold[b1], e + d * 4.5f - side * 1.2f, e); G.sold[b1].hp = 100; }
  if (b2 >= 0) { place(G.sold[b2], e + d * 7.0f + side * 1.3f, e); G.sold[b2].hp = 100; }
}
}   // namespace

bool br3Shots(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--shots-backrooms") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pending.empty()) { app.screenshot(pending); pending.clear(); } return true; }
  st += dt;
  if (stage == 0) {
    if (const char* e = std::getenv("NMK_BR3_SHOTS")) {
      std::stringstream ss(e); std::string item;
      while (std::getline(ss, item, ';')) {
        std::stringstream is(item); std::string f[5]; for (auto& x : f) std::getline(is, x, ':');
        if (!f[4].empty()) spots.push_back({f[0], vec2(std::atof(f[1].c_str()), std::atof(f[2].c_str())), vec2(std::atof(f[3].c_str()), std::atof(f[4].c_str()))});
      }
    }
    bool ok = app.openMap(contentDir() / "templates" / "backrooms.nmap.json");
    gUi.loaded = false; gUi.matchScenario = json();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    ok = ok && app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    logf("[br3] map open + match: %s, %d spots", ok ? "ok" : "FAILED", (int)spots.size());
    if (!ok || spots.empty()) { app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    stage = 1; st = 0;
    return true;
  }
  Game& G = *gGame;
  if (stage == 1) {
    if (st < 3.0f) return true;
    G.paused = true;
    for (auto& s : G.sold) if (s.alive && !s.human && !s.localPlayer) { if (b1 < 0) b1 = s.slot; else if (b2 < 0 && s.slot != b1) b2 = s.slot; }
    if (b2 >= 0 && !G.sold[b2].zombie) { G.sold[b2].zombie = true; aiZombieAssign(G, G.sold[b2]); }   // the second bot as a zombie
    logf("[br3] bots %d, %d (zombie)", b1, b2);
    stage = 2; st = 0; shotIdx = 0;
    return true;
  }
  if (stage == 2) {
    if (shotIdx >= (int)spots.size()) { app.selftest->exitCode = 0; app.selftest->done = true; return true; }
    hold(app, spots[shotIdx]);
    G.posesStep(dt > 0 ? dt : 1.0f / 60);
    if (st > 2.5f) { pending = exeDir() / "selftest_out" / ("br3_" + spots[shotIdx].name + ".png"); ++shotIdx; st = 0; app.R.resetTemporal(); }
    return true;
  }
  return true;
}
