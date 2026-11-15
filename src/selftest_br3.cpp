// --shots-backrooms (BR3): play-camera captures of the Backrooms map with characters in the room, for tuning its look
// (auto exposure, the first-person arms and weapon, a soldier and a zombie lit by the room). A Singleplayer match on
// templates/backrooms.nmap.json, paused after the spawn; the local player and two bots are held at fixed spots each frame.
// Spots: NMK_BR3_SHOTS = "name:x:z:tx:tz;name:..." or "name:x:y:z:tx:tz" (UG3: y = the floor; NMK_SHOTS_MAP = another map) (eye at x, z looking at tx, tz; the bots stand 4.5 m and 7 m along
// the view, 1.2 m either side). Shots: selftest_out/br3_<name>.png. No checks beyond "the match started".
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "physics.h"
#include "ai_hooks.h"
#include "br_models.h"
#include <cstdlib>
#include <cmath>
#include <sstream>

namespace {
int mode = -1;
float st = 0;
int stage = 0, shotIdx = 0;
fs::path pending;
struct Spot { std::string name; vec2 eye, at; float y = 0; };
std::vector<Spot> spots;
int b1 = -1, b2 = -1;

float yawToward(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
void place(Soldier& s, vec3 feet, vec3 face) {
  s.feet = feet; s.vel = vec3(0); s.speed = 0; s.yaw = yawToward(face); s.br.aimYaw = s.yaw; s.br.aimPitch = 0;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, feet);
}
void hold(App& app, const Spot& sp) {
  Game& G = *gGame;
  vec3 e(sp.eye.x, sp.y, sp.eye.y), t(sp.at.x, sp.y, sp.at.y), d = glm::normalize(t - e), side = glm::normalize(glm::cross(d, vec3(0, 1, 0)));
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
        std::stringstream is(item); std::string f[6]; for (auto& x : f) std::getline(is, x, ':');
        auto n = [&](int i) { return (float)std::atof(f[i].c_str()); };
        if (!f[5].empty()) spots.push_back({f[0], vec2(n(1), n(3)), vec2(n(4), n(5)), n(2)});   // UG3: name:x:y:z:tx:tz (y = the floor)
        else if (!f[4].empty()) spots.push_back({f[0], vec2(n(1), n(2)), vec2(n(3), n(4))});
      }
    }
    const char* mp = std::getenv("NMK_SHOTS_MAP");   // UG3: another map (content-relative), default Backrooms
    bool ok = app.openMap(contentDir() / (mp && *mp ? mp : "templates/backrooms.nmap.json"));
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

// --shots-palette (BR5): the editor with the open map's asset palette, written as a UI screenshot after the
// thumbnails have had time to render: NMK_SHOTS_MAP (content-relative) -> selftest_out/palette_<map stem>.png
bool paletteShots(App& app, float dt) {
  static int pm = -1; static float t = 0; static int stage = 0;
  if (pm < 0) { pm = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--shots-palette") pm = 1; }
  if (pm != 1) return false;
  if (dt < 0) return true;
  t += dt;
  const char* mp = std::getenv("NMK_SHOTS_MAP");
  fs::path map = contentDir() / (mp && *mp ? mp : "templates/backrooms.nmap.json");
  if (stage == 0) { bool ok = app.openMap(map); logf("[palette] open %s: %s", map.string().c_str(), ok ? "ok" : "FAILED"); stage = 1; t = 0; return true; }
  if (stage == 1 && t > 8.0f) { app.uiShot = exeDir() / "selftest_out" / ("palette_" + map.stem().stem().string() + ".png"); stage = 2; t = 0; return true; }
  if (stage == 2 && t > 1.0f) { app.selftest->exitCode = 0; app.selftest->done = true; }
  return true;
}

// --shots-poses (AN1): one bot on battle1 (or NMK_SHOTS_MAP) held in each soldier posture, seen from the side and the
// three-quarter front: kneel (crouched, still, a target), carry (jogging, no target) and moveaim (jogging, a target).
// NMK_POSE_WEAPONS = "label:id,..." (default rifle:m1_garand,smg:mp40); NMK_POSE_BODIES = "model,..." (operator_allied,
// br_marine, ...; default the bot's own body); NMK_POSE_SLOPE = 1: a spot on a 12-30 % slope, the bot facing uphill;
// NMK_POSE_SPEED: the moving postures' speed (m/s, default 2.69 jog).
// Shots: selftest_out/pose_[<body>_][slope_]<weapon>_<posture>_<view>.png. Logs the feet / knee height over the ground.
bool poseShots(App& app, float dt) {
  static int pm = -1; static float t = 0; static int stage = 0, idx = 0, bot = -1; static vec3 anchor{0}, face{0, 0, 1}; static fs::path pend;
  static bool slope = false; static float worstStance = 0, worstKneel = 0; static int frames = 0;
  if (pm < 0) { pm = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--shots-poses") pm = 1; }
  if (pm != 1) return false;
  if (dt < 0) { if (!pend.empty()) { app.screenshot(pend); pend.clear(); } return true; }
  t += dt;
  auto list = [](const char* env, const char* def) {
    std::vector<std::string> v; const char* e = std::getenv(env); std::stringstream ss(e && *e ? e : def); std::string it;
    while (std::getline(ss, it, ',')) if (!it.empty()) v.push_back(it);
    return v;
  };
  static std::vector<std::pair<std::string, std::string>> weps;
  static std::vector<std::string> bodies;
  if (weps.empty()) {
    for (auto& it : list("NMK_POSE_WEAPONS", "rifle:m1_garand,smg:mp40")) { size_t c = it.find(':'); if (c != std::string::npos) weps.push_back({it.substr(0, c), it.substr(c + 1)}); }
    bodies = list("NMK_POSE_BODIES", "-");
    const char* sl = std::getenv("NMK_POSE_SLOPE"); slope = sl && *sl == '1';
  }
  static const char* posts[3] = {"kneel", "carry", "moveaim"};
  static const char* views[2] = {"side", "front34"};
  if (stage == 0) {
    const char* mp = std::getenv("NMK_SHOTS_MAP");
    bool ok = app.openMap(contentDir() / (mp && *mp ? mp : "templates/battle1.nmap.json"));
    gUi.loaded = false; gUi.matchScenario = json();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    ok = ok && app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    logf("[poses] map open + match: %s", ok ? "ok" : "FAILED");
    if (!ok) { app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    app.shotsNoViewModel = true;
    stage = 1; t = 0; return true;
  }
  Game& G = *gGame;
  auto H = [&](float x, float z) { return app.terrain.height(x, z); };
  if (stage == 1) {
    if (t < 3.0f) return true;
    G.paused = true;
    for (auto& s : G.sold) if (s.alive && !s.human && !s.localPlayer && !s.zombie) { bot = s.slot; break; }
    if (Soldier* L = G.local()) {   // a clear spot near the spawn: flat (or sloped), nothing within 3.5 m at body heights, open sky
      anchor = L->feet; bool found = false;
      auto clear = [&](vec3 o, vec3 d, float len) { float tt; uint64_t ud; return !Physics::castRay(o, glm::normalize(d), len, tt, ud); };
      float steepest = 0;
      for (float r : {8.0f, 12.0f, 16.0f, 20.0f, 25.0f, 30.0f, 40.0f, 55.0f, 70.0f, 90.0f, 120.0f, 150.0f, 190.0f, 240.0f}) {
        for (int a = 0; a < 36 && !found; ++a) {
          float an = a * 6.2831853f / 36; vec3 c = L->feet + vec3(std::cos(an), 0, std::sin(an)) * r; c.y = H(c.x, c.z);
          vec2 g((H(c.x + 0.5f, c.z) - H(c.x - 0.5f, c.z)), (H(c.x, c.z + 0.5f) - H(c.x, c.z - 0.5f)));   // rise over 1 m
          float gl = glm::length(g);
          bool ok = slope ? gl > 0.12f && gl < 0.30f : gl < 0.04f;
          if (gl < 0.5f) steepest = std::max(steepest, gl);
          for (int k = 0; k < 8 && ok; ++k) {
            vec3 d(std::cos(k * 0.785f), 0, std::sin(k * 0.785f));
            float lin = c.y + 3.5f * (g.x * d.x + g.y * d.z);   // the plane through the spot
            ok = std::fabs(H(c.x + 3.5f * d.x, c.z + 3.5f * d.z) - (slope ? lin : c.y)) < (slope ? 0.35f : 0.25f);
            for (float h : {0.9f, 1.6f}) ok = ok && clear(c + vec3(0, h, 0), d + vec3(0, g.x * d.x + g.y * d.z, 0), 3.8f);
          }
          if (ok && clear(c + vec3(0, 0.5f, 0), vec3(0, 1, 0), 10.0f)) { anchor = c; found = true; if (slope) face = glm::normalize(vec3(g.x, 0, g.y)); }
        }
        if (found) break;
      }
      logf("[poses] %s spot %s at %.1f %.1f %.1f, facing %.2f %.2f (steepest seen %.2f)", slope ? "sloped" : "flat", found ? "found" : "NOT found (the spawn)", anchor.x, anchor.y, anchor.z, face.x, face.z, steepest);
    }
    logf("[poses] bot %d", bot);
    if (bot < 0) { app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    stage = 2; t = 0; idx = 0; return true;
  }
  int per = 6 * (int)weps.size();
  if (idx >= per * (int)bodies.size()) {
    logf("[poses] ground: worst stance-foot gap %.3f m, worst kneel contact gap %.3f m (%d frames)", worstStance, worstKneel, frames);
    app.selftest->exitCode = 0; app.selftest->done = true; return true;
  }
  int bi = idx / per, w = (idx % per) / 6, ps = (idx / 2) % 3, vw = idx % 2;
  Soldier& s = G.sold[bot];
  if (bodies[bi] != "-" && t == dt) {   // the body under test (loaded like Game::begin's operator bodies)
    std::string id = bodies[bi]; int mi = -1;
    if (id == "br_marine") mi = brMarineModel(G);
    else if (G.modelIdx.count(id)) mi = G.modelIdx[id];
    else { auto cm = std::make_unique<CharModel>(); std::string e;
      if (cm->load(contentDir() / "characters" / (id + ".glb"), e)) { mi = G.modelIdx[id] = (int)G.models.size(); G.models.push_back(std::move(cm)); }
      else logf("[poses] body %s: %s", id.c_str(), e.c_str()); }
    if (mi >= 0 && mi != s.model) {
      if (s.ciReady) s.ci.destroy();
      s.model = mi; s.ci.frameSkinned = -1; s.ciReady = false;
      const CharModel& m = *G.models[mi];
      auto Hh = [&](int c) { return m.semantic[c] >= 0 ? m.jointHead[m.semantic[c]] : vec3(-9); };
      logf("[poses] body %s rig: lsh %.2f %.2f %.2f larm %.2f %.2f %.2f hand %.2f %.2f %.2f upper %.3f fore %.3f | head y %.2f hips y %.2f thigh %.3f shin %.3f foot y %.2f toe %.2f %.2f",
           id.c_str(), Hh(CJ_LSHOULDER).x, Hh(CJ_LSHOULDER).y, Hh(CJ_LSHOULDER).z, Hh(CJ_LARM).x, Hh(CJ_LARM).y, Hh(CJ_LARM).z, Hh(CJ_LHAND).x, Hh(CJ_LHAND).y, Hh(CJ_LHAND).z,
           glm::length(Hh(CJ_LFOREARM) - Hh(CJ_LARM)), glm::length(Hh(CJ_LHAND) - Hh(CJ_LFOREARM)), Hh(CJ_HEAD).y, Hh(CJ_HIPS).y,
           glm::length(Hh(CJ_LLEG) - Hh(CJ_LUPLEG)), glm::length(Hh(CJ_LFOOT) - Hh(CJ_LLEG)), Hh(CJ_LFOOT).y, Hh(CJ_LTOE).y, Hh(CJ_LTOE).z);
    }
  }
  vec3 B(anchor.x, H(anchor.x, anchor.z), anchor.z);
  s.feet = B; s.hp = 100; s.yaw = degrees(std::atan2(-face.x, -face.z)); s.br.aimYaw = s.yaw; s.br.aimPitch = 0;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, B);
  int wi = app.weaponIndex(weps[w].second); if (wi >= 0) s.weapon = wi;
  s.reload = 0; s.throwT = -1; s.recoil = 0;
  s.posture = ps == 0 ? Physics::CROUCH : Physics::STAND;
  static float runV = [] { const char* e = std::getenv("NMK_POSE_SPEED"); return e && *e ? (float)std::atof(e) : 2.69f; }();   // jog by default
  s.speed = ps == 0 ? 0.0f : runV; s.vel = face * s.speed;
  s.br.target = ps == 1 ? -1 : G.localSlot; s.lastShot = -100;
  vec3 left = glm::cross(vec3(0, 1, 0), face);   // the bot's left
  vec3 cd = vw == 0 ? -left : glm::normalize(left * 0.7f + face);   // side: from its right; three-quarter: front left
  vec3 e = B + cd * 3.2f; e.y = H(e.x, e.z) - 0.45f;   // eye ~1.2 m above the ground
  Physics::setCharacterPos(e);
  if (Soldier* L = G.local()) { L->feet = e; L->hp = 100; }
  vec3 v = (B + vec3(0, 0.85f, 0)) - (e + vec3(0, 1.62f, 0));
  app.eyeYaw = degrees(std::atan2(-v.x, -v.z)); app.eyePitch = degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  G.posesStep(dt > 0 ? dt : 1.0f / 60);
  if (t > 0.6f) {   // settled: the joints over the ground (feet at the ankle's rest height when planted, the knee on the ground)
    const CharModel& m = *G.models[s.model];
    auto gap = [&](int c) { vec3 w = vec3(s.world * vec4(s.pose.joint[c], 1)); return w.y - H(w.x, w.z) - (c == CJ_RLEG ? 0.0f : m.jointHead[m.semantic[c]].y); };
    ++frames;
    if (ps == 0) worstKneel = std::max({worstKneel, std::fabs(gap(CJ_RLEG) - 0.065f), std::fabs(gap(CJ_LFOOT))});
    else worstStance = std::max(worstStance, std::fabs(std::min(gap(CJ_LFOOT), gap(CJ_RFOOT))));
  }
  if (t > 1.6f) {
    std::string tag = (bodies[bi] == "-" ? "" : bodies[bi] + "_") + (slope ? "slope_" : "");
    pend = exeDir() / "selftest_out" / ("pose_" + tag + weps[w].first + "_" + posts[ps] + "_" + views[vw] + ".png");
    logf("[poses] %s: kneel %.2f carry %.2f moveAim %.2f stride %.2f", pend.filename().string().c_str(), s.anKneel, s.anCarry, s.anMoveAim, s.anStride);
    if (s.weapon >= 0) {   // wrists vs the grip sockets (model space), shoulder heights, head
      const WeaponDef& d = *app.weaponDefs[s.weapon]; const PoseResult& r = s.pose;
      vec3 gR = vec3(r.weapon * vec4(d.gripR, 1)), gL = vec3(r.weapon * vec4(d.gripL, 1)), bt = vec3(r.weapon * vec4(0, 0, 0, 1));
      auto P = [](vec3 v) { char b[64]; std::snprintf(b, sizeof b, "(%.2f %.2f %.2f)", v.x, v.y, v.z); return std::string(b); };
      logf("[poses]   wristR %s gripR %s | wristL %s gripL %s | shR %s shL %s head %s butt %s", P(r.joint[CJ_RHAND]).c_str(), P(gR).c_str(), P(r.joint[CJ_LHAND]).c_str(), P(gL).c_str(),
           P(r.joint[CJ_RARM]).c_str(), P(r.joint[CJ_LARM]).c_str(), P(r.joint[CJ_HEAD]).c_str(), P(bt).c_str());
    }
    ++idx; t = 0; app.R.resetTemporal();
  }
  return true;
}
