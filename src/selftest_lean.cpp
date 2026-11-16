// MV3 --capture-lean: no suite, a look at the Q / E lean (selftest_out/lean_*.png, the lean value and the eye offset
// logged). Part A, a blank map with an 8 m wall (brm_wall_p) and a jeep beyond it: the player behind the wall's end on
// the leaning side, upright, then leaning right (E) at the -x end and left (Q) at the +x end, standing / crouched, hip /
// ADS. Part B, a TDM match on the village map: a soldier bot held at full right lean, seen from its side and front.
#include "selftest.h"
#include "game.h"
#include "mpui.h"

namespace {
int phase = 0, shotIdx = 0; float t = 0; fs::path pend; int bot = -1;
struct Cfg { const char* name; float x; int side; bool crouch, ads; };
const Cfg kCfg[] = {{"upright", -3.55f, 0, false, false},
  {"right_stand_hip", -3.55f, 1, false, false}, {"right_stand_ads", -3.55f, 1, false, true}, {"right_crouch_hip", -3.55f, 1, true, false}, {"right_crouch_ads", -3.55f, 1, true, true},
  {"left_stand_hip", 3.55f, -1, false, false}, {"left_stand_ads", 3.55f, -1, false, true}, {"left_crouch_hip", 3.55f, -1, true, false}, {"left_crouch_ads", 3.55f, -1, true, true}};
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("lean_" + n + ".png"); }
}

bool leanCapture(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-lean") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  t += dt;
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "Lean capture");
    if (const AssetDef* w = a.lib.find("brm_wall_p")) a.placeInstance(w, vec3(0, a.terrain.height(0, 0), 0), 0);
    if (const AssetDef* j = a.lib.find("car_jeep_willys")) a.placeInstance(j, vec3(-6, a.terrain.height(-6, 12), 12), 0);
    if (const AssetDef* j = a.lib.find("car_jeep_willys")) a.placeInstance(j, vec3(6, a.terrain.height(6, 12), 12), 0);
    a.syncRuntime(); a.enterExplore(); a.useScripted = true;
    phase = 1; t = 0; shotIdx = 0; return true;
  }
  if (phase == 1) {
    if (shotIdx >= (int)(sizeof kCfg / sizeof kCfg[0])) { phase = 2; t = 0; return true; }
    const Cfg& c = kCfg[shotIdx];
    if (t < dt * 1.5f) { vec3 p(c.x, a.terrain.height(c.x, -0.75f), -0.75f); Physics::setCharacterPos(p); a.lean = App::LeanState{}; }
    a.eyeYaw = 180; a.eyePitch = -2;
    ExploreInput& in = a.scriptedInput; in = ExploreInput{};
    in.crouch = c.crouch; in.aim = c.ads; in.leanR = c.side > 0; in.leanL = c.side < 0;
    if (t > 1.0f && pend.empty() && t < 1.0f + dt * 1.5f) {
      vec3 off = a.leanOffset();
      logf("[lean] %s: lean %.2f (limits L %.2f R %.2f), eye offset (%.3f %.3f %.3f) m, roll %.1f deg", c.name, a.lean.x, a.lean.limitL, a.lean.limitR, off.x, off.y, off.z, a.lean.x * a.leanCfg.roll);
      a.uiShot = out(c.name);
    }
    if (t > 1.3f) { ++shotIdx; t = 0; }
    return true;
  }
  if (phase == 2) {   // the match for the third-person body
    if (a.mode == Mode::Explore) a.exitExplore();
    a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.soloPlayers = 2; sc.rules.teamPlayers[0] = 2; sc.rules.teamPlayers[1] = 0;
    sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
    a.enterExplore(); a.useScripted = true; a.scriptedInput = ExploreInput{};
    phase = 3; t = 0; return true;
  }
  Game& G = *gGame; Soldier* L = G.local();
  if (phase == 3) {
    if (!L || !L->alive) return t > 30 ? (a.selftest->done = true, true) : true;
    for (auto& s : G.sold) if (!s.human && s.alive) bot = s.slot;
    if (bot < 0) return t > 30 ? (a.selftest->done = true, true) : true;
    phase = 4; t = 0; return true;
  }
  Soldier& b = G.sold[bot];
  b.lean = 1.0f; b.hp = 100; b.protectUntil = G.t + 1; L->protectUntil = G.t + 1;
  vec3 f = App::forward(b.yaw, 0); f.y = 0; f = glm::normalize(f); vec3 r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  bool front = t > 1.6f;
  vec3 cam = b.feet + (front ? f * 3.2f : -r * 3.2f) + vec3(0, 0.2f, 0);
  Physics::setCharacterPos(cam);
  vec3 d = b.feet + vec3(0, 1.1f, 0) - (cam + vec3(0, 1.6f, 0)); a.eyeYaw = glm::degrees(std::atan2(-d.x, -d.z)); a.eyePitch = glm::degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  if (pend.empty() && ((t > 1.2f && t < 1.2f + dt * 1.5f) || (t > 2.8f && t < 2.8f + dt * 1.5f))) pend = out(front ? "body_front" : "body_side");
  if (t > 3.2f) { if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; }
  return true;
}
