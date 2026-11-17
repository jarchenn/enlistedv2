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
#include <fstream>
#include "terrain_blend.h"

namespace {
int mode = -1;
float st = 0;
int stage = 0, shotIdx = 0;
fs::path pending;
struct Spot { std::string name; vec2 eye, at; float y = 0, ly = -1; };   // L2: ly >= 0: look at the target at this height over the floor
std::vector<Spot> spots;
int b1 = -1, b2 = -1;

float yawToward(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
void place(Soldier& s, vec3 feet, vec3 face) {
  s.feet = feet; s.vel = vec3(0); s.speed = 0; s.yaw = yawToward(face); s.br.aimYaw = s.yaw; s.br.aimPitch = 0;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, feet);
}
void hold(App& app, const Spot& sp) {
  Game& G = *gGame;
  float fy = sp.y <= -9000 ? gGame->app.terrain.height(sp.eye.x, sp.eye.y) : sp.y;   // L2: y -9999 = the terrain there
  vec3 e(sp.eye.x, fy, sp.eye.y), t(sp.at.x, fy, sp.at.y), d = glm::normalize(t - e), side = glm::normalize(glm::cross(d, vec3(0, 1, 0)));
  Physics::setCharacterPos(e);
  if (Soldier* L = G.local()) { L->feet = e; L->hp = 100; }
  vec3 eye = e + vec3(0, 1.62f, 0), look = sp.ly >= 0 ? t + vec3(0, sp.ly, 0) : e + d * 6.0f + vec3(0, 1.3f, 0), v = look - eye;
  app.eyeYaw = yawToward(v); app.eyePitch = degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  if (std::getenv("NMK_SHOTS_NOBOTS")) {   // L2 scenery shots: every bot far behind the camera
    for (auto& s : G.sold) if (s.alive && !s.localPlayer) { vec3 p = e - d * (300.0f + 3.0f * s.slot); p.y = gGame->app.terrain.height(p.x, p.z); place(s, p, e); }
    return;
  }
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
        std::stringstream is(item); std::string f[7]; for (auto& x : f) std::getline(is, x, ':');
        auto n = [&](int i) { return (float)std::atof(f[i].c_str()); };
        if (!f[5].empty()) spots.push_back({f[0], vec2(n(1), n(3)), vec2(n(4), n(5)), n(2), f[6].empty() ? -1.0f : n(6)});   // UG3: name:x:y:z:tx:tz[:look height] (y = the floor)
        else if (!f[4].empty()) spots.push_back({f[0], vec2(n(1), n(2)), vec2(n(3), n(4))});
      }
    }
    const char* mp = std::getenv("NMK_SHOTS_MAP");   // UG3: another map (content-relative), default Backrooms
    bool ok = app.openMap(contentDir() / (mp && *mp ? mp : "templates/backrooms.nmap.json"));
    gUi.loaded = false; gUi.matchScenario = json();
    if (std::getenv("NMK_SHOTS_NOBOTS") && app.doc.extra.is_object() && app.doc.extra.contains("scenario") && app.doc.extra["scenario"].contains("rules"))
      app.doc.extra["scenario"]["rules"]["mode"] = "solo";   // L2 scenery shots: a royale map would start aboard the drop plane
    if (std::getenv("NMK_SHOTS_NOBOTS")) app.shotsNoViewModel = true;
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
  static std::vector<std::string> posts;   // SW1: NMK_POSE_POSTS = kneel,carry,moveaim (default) and idle (standing still), fire (standing, aiming, one shot just before the shot)
  if (posts.empty()) posts = list("NMK_POSE_POSTS", "kneel,carry,moveaim");
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
    for (auto& o : G.sold) if (o.inUse && !o.localPlayer)   // SW1: what each AI spawned as (kit, body, weapons)
      logf("[poses] slot %d team %d kit %d model %s hp %.0f speed x%.2f sparks %d weapon %s carried %zu frags %d", o.slot, o.team, o.kit,
           [&] { for (auto& [k, v] : G.modelIdx) if (v == o.model) return k; return std::string("?"); }().c_str(), o.hp, o.kitSpeed, (int)o.kitSparks,
           o.weapon >= 0 ? app.weaponDefs[o.weapon]->id.c_str() : "-", o.carried.size(), o.frags);
    if (bot < 0) { app.selftest->exitCode = 1; app.selftest->done = true; return true; }
    stage = 2; t = 0; idx = 0; return true;
  }
  // AN1 study: NMK_POSE_VIEWS (side, front34, front, back, top), NMK_POSE_FRAMES (frames through a moving cycle, 0.1 s apart),
  // postures: kneel carry moveaim idle fire aim walk jog sprint strafe crouchwalk prone reload throw lean lookup lookdown
  static std::vector<std::string> viewsL; if (viewsL.empty()) viewsL = list("NMK_POSE_VIEWS", "side,front34");
  static int nfr = [] { const char* e = std::getenv("NMK_POSE_FRAMES"); return e && *e ? std::max(1, std::atoi(e)) : 1; }();
  static int fk = 0;   // the frame within a moving posture
  const int np = (int)posts.size(), nv = (int)viewsL.size(), perW = np * nv, per = perW * (int)weps.size();
  if (idx >= per * (int)bodies.size()) {
    logf("[poses] ground: worst stance-foot gap %.3f m, worst kneel contact gap %.3f m (%d frames)", worstStance, worstKneel, frames);
    app.selftest->exitCode = 0; app.selftest->done = true; return true;
  }
  int bi = idx / per, w = (idx % per) / perW, vw = idx % nv; const std::string pn = posts[(idx / nv) % np], vn = viewsL[vw];
  const bool movingP = pn == "carry" || pn == "moveaim" || pn == "walk" || pn == "jog" || pn == "sprint" || pn == "strafe" || pn == "crouchwalk";
  const int nframes = movingP ? nfr : 1;
  const int ps = pn == "kneel" ? 0 : pn == "carry" || pn == "idle" || pn == "sprint" ? 1 : 2;   // 0 crouched still, 1 no target, 2 a target
  Soldier& s = G.sold[bot];
  G.noFootLock = true;   // AN1 step 3b: the posed bot stays put while it "walks" (a treadmill): no world foot locks
  if (const char* gear = std::getenv("NMK_POSE_GEAR")) { int h = 0, v = 0, b = 0; std::sscanf(gear, "%d,%d,%d", &h, &v, &b); s.helmetLvl = h; s.vestLvl = v; s.backpack = b; }   // AN1 step 4b: BR gear on the posed bot
  if (bodies[bi] != "-" && t == dt) {   // the body under test (loaded like Game::begin's operator bodies)
    std::string id = bodies[bi]; int mi = -1;
    if (id == "br_marine") mi = brMarineModel(G);
    else if (G.modelIdx.count(id)) mi = G.modelIdx[id];
    else { auto cm = std::make_unique<CharModel>(); std::string e;
      if (cm->load(contentDir() / "characters" / (id + ".glb"), e)) { mi = G.modelIdx[id] = (int)G.models.size(); G.models.push_back(std::move(cm)); }
      else logf("[poses] body %s: %s", id.c_str(), e.c_str()); }
    if (mi >= 0) s.kit = id.rfind("trooper", 0) == 0 ? 2 : id.rfind("robot", 0) == 0 ? 3 : 0;   // AN1 step 2b: the body's kit as in a match (troopers' hunch)
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
  s.feet = B; s.hp = 100; s.yaw = degrees(std::atan2(-face.x, -face.z)); s.br.aimYaw = s.yaw;
  s.br.aimPitch = pn == "lookup" ? 30.0f : pn == "lookdown" ? -30.0f : 0.0f;
  if (s.agent >= 0) Physics::setAgentPos(s.agent, B);
  int wi = app.weaponIndex(weps[w].second); if (wi >= 0) s.weapon = wi;
  if (const char* at = std::getenv("NMK_POSE_SIGHT")) { s.ws.att[0] = std::atoi(at); s.ws.loaded = std::max(s.ws.loaded, 5); }   // AT1: an optic on the posed bot's gun (third person; loaded, or the refill resets the state)
  s.throwT = pn == "throw" ? 0.3f + 0.25f * std::min(1.0f, t / 1.6f) : -1; s.recoil = 0; s.lean = pn == "lean" ? 1.0f : 0.0f;
  if (pn == "reload") { s.reloadTotal = 2.0f; s.reload = 1.0f; } else s.reload = 0;
  s.posture = ps == 0 || pn == "crouchwalk" ? Physics::CROUCH : pn == "prone" ? Physics::PRONE : Physics::STAND;
  static float runV = [] { const char* e = std::getenv("NMK_POSE_SPEED"); return e && *e ? (float)std::atof(e) : 2.69f; }();   // jog by default
  vec3 left = glm::cross(vec3(0, 1, 0), face);   // the bot's left
  s.speed = pn == "walk" ? 1.3f : pn == "jog" ? 2.69f : pn == "sprint" ? 4.6f : pn == "strafe" ? 2.0f : pn == "crouchwalk" ? 1.2f : (pn == "carry" || pn == "moveaim") ? runV : 0.0f;
  s.vel = (pn == "strafe" ? left : face) * s.speed;
  s.br.target = ps == 1 ? -1 : G.localSlot; s.lastShot = -100;
  vec3 cd = vn == "side" ? -left : vn == "front" ? face : vn == "back" ? -face : glm::normalize(left * 0.7f + face);   // side: from its right; three-quarter: front left
  vec3 e = B + cd * 3.2f; e.y = H(e.x, e.z) - 0.45f;   // eye ~1.2 m above the ground
  if (vn == "top") e = B + face * 0.6f + vec3(0, 2.4f, 0);   // from above, a little ahead
  vec3 handsP = vec3(s.world * vec4(0.5f * (s.pose.joint[CJ_RHAND] + s.pose.joint[CJ_LHAND]), 1));   // AN1 step 2: the posed hands (last frame)
  // AN1 step 2b: each hand close up from outside and inside (rhand_o: right of the right hand; rhand_i: front-left, below; lhand_o: left of the
  // left hand; lhand_i: front-right, below)
  const bool handV = vn.rfind("rhand", 0) == 0 || vn.rfind("lhand", 0) == 0;
  if (handV) {
    vec3 hp = vec3(s.world * vec4(s.pose.joint[vn[0] == 'r' ? CJ_RHAND : CJ_LHAND], 1)); handsP = hp;
    vec3 dir = vn == "rhand_o" ? -left * 0.9f + face * 0.3f + vec3(0, 0.15f, 0) : vn == "rhand_i" ? -left * 0.5f + face * 0.6f - vec3(0, 0.5f, 0)
             : vn == "lhand_o" ? left * 0.9f + face * 0.4f + vec3(0, 0.15f, 0) : -left * 0.25f + face * 0.45f - vec3(0, 0.85f, 0);   // inside views from below the hand
    e = hp + glm::normalize(dir) * 0.42f - vec3(0, 1.62f, 0);
  }
  if (vn == "head") { vec3 hp = vec3(s.world * vec4(s.pose.joint[CJ_HEAD], 1)) + vec3(0, 0.06f, 0); handsP = hp; e = hp + glm::normalize(-left * 0.6f + face * 0.8f + vec3(0, 0.1f, 0)) * 0.7f - vec3(0, 1.62f, 0); }   // AN1 step 4b: close on the head and shoulders
  if (vn == "hands") e = handsP + glm::normalize(-left * 0.8f + face * 0.6f + vec3(0, 0.25f, 0)) * 0.75f - vec3(0, 1.62f, 0);   // close on the hands from the front right, a little above
  Physics::setCharacterPos(e);
  if (Soldier* L = G.local()) { L->feet = e; L->hp = 100; }
  vec3 v = (vn == "hands" || vn == "head" || handV ? handsP : B + vec3(0, 0.85f, 0)) - (e + vec3(0, 1.62f, 0));
  app.eyeYaw = degrees(std::atan2(-v.x, -v.z)); app.eyePitch = degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  G.posesStep(dt > 0 ? dt : 1.0f / 60);
  if (t > 0.6f) {   // settled: the joints over the ground (feet at the ankle's rest height when planted, the knee on the ground)
    const CharModel& m = *G.models[s.model];
    auto gap = [&](int c) { vec3 w = vec3(s.world * vec4(s.pose.joint[c], 1)); return w.y - H(w.x, w.z) - (c == CJ_RLEG ? 0.0f : m.jointHead[m.semantic[c]].y); };
    ++frames;
    if (ps == 0) worstKneel = std::max({worstKneel, std::fabs(gap(CJ_RLEG) - 0.065f), std::fabs(gap(CJ_LFOOT))});
    else if (!movingP && pn != "prone") worstStance = std::max(worstStance, std::fabs(std::min(gap(CJ_LFOOT), gap(CJ_RFOOT))));
  }
  const float shotT = 1.6f + 0.1f * fk;
  if (pn == "fire" && t > shotT - 0.015f && t - dt <= shotT - 0.015f && s.weapon >= 0) {   // a frame before the shot: bolt and flash at the muzzle   // SW1: one shot, its bolt / tracer and flash in the picture
    s.ws.loaded = std::max(s.ws.loaded, 1); s.ws.cooldown = 0; s.reload = 0;
    vec3 tgt = B + face * 30.0f + vec3(0, 1.4f, 0); bool fired = G.soldierFire(s, glm::normalize(tgt - (B + vec3(0, 1.5f, 0))));
    logf("[poses] fire %s: %s (cooldown %.2f bolt %.2f throw %.2f loaded %d)", app.weaponDefs[s.weapon]->id.c_str(), fired ? "fired" : "NOT fired", s.ws.cooldown, s.ws.bolt, s.throwT, s.ws.loaded);
  }
  if (t > shotT) {
    std::string tag = (bodies[bi] == "-" ? "" : bodies[bi] + "_") + (slope ? "slope_" : "");
    pend = exeDir() / "selftest_out" / ("pose_" + tag + weps[w].first + "_" + pn + "_" + vn + (nframes > 1 ? "_f" + std::to_string(fk) : std::string()) + ".png");
    logf("[poses] %s: kneel %.2f carry %.2f moveAim %.2f stride %.2f | fingers R %d gap %.4f pen %.4f  L %d gap %.4f pen %.4f", pend.filename().string().c_str(), s.anKneel, s.anCarry, s.anMoveAim, s.anStride,
         s.pose.fingersOn[1], s.pose.fingerGap[1], s.pose.fingerPen[1], s.pose.fingersOn[0], s.pose.fingerGap[0], s.pose.fingerPen[0]);
    if (vn == "side") for (int h = 1; h >= 0; --h) { const auto& D = s.pose.fingerDiag[h];
      logf("[fingers] %s %s: thumb %.2f/%+.0f/%+.0f index %.2f/%+.0f/%+.0f middle %.2f/%+.0f/%+.0f ring %.2f/%+.0f/%+.0f little %.2f/%+.0f/%+.0f (rad / tip gap mm / start clearance mm)", pend.filename().string().c_str(), h ? "R" : "L",
           D[0][0], D[0][1] * 1000, D[0][2] * 1000, D[1][0], D[1][1] * 1000, D[1][2] * 1000, D[2][0], D[2][1] * 1000, D[2][2] * 1000, D[3][0], D[3][1] * 1000, D[3][2] * 1000, D[4][0], D[4][1] * 1000, D[4][2] * 1000); }
    if (s.weapon >= 0) {   // wrists vs the grip sockets (model space), shoulder heights, head
      const WeaponDef& d = *app.weaponDefs[s.weapon]; const PoseResult& r = s.pose;
      vec3 gR = vec3(r.weapon * vec4(d.gripR, 1)), gL = vec3(r.weapon * vec4(d.gripL, 1)), bt = vec3(r.weapon * vec4(0, 0, 0, 1));
      auto P = [](vec3 v) { char b[64]; std::snprintf(b, sizeof b, "(%.2f %.2f %.2f)", v.x, v.y, v.z); return std::string(b); };
      logf("[poses]   wristR %s gripR %s | wristL %s gripL %s | shR %s shL %s head %s butt %s", P(r.joint[CJ_RHAND]).c_str(), P(gR).c_str(), P(r.joint[CJ_LHAND]).c_str(), P(gL).c_str(),
           P(r.joint[CJ_RARM]).c_str(), P(r.joint[CJ_LARM]).c_str(), P(r.joint[CJ_HEAD]).c_str(), P(bt).c_str());
    }
    if (++fk < nframes) return true;   // the next frame of the same cycle (time keeps running)
    fk = 0;
    ++idx; t = 0; app.R.resetTemporal();
  }
  return true;
}

// --find-floaters (MAP1): every placed instance against the ground it should stand on. NMK_FLOAT_MAPS = "dday,battle1,..."
// (templates/<name>.nmap.json; default dday). Per instance: footprint corners (0.1 m in), edge midpoints and centre on the
// runtime terrain (pads, berms, trench cuts; its own cut ignored). Floating: the base is over the ground by more than
// 5 cm at any of them (trees: at the trunk); sunk: the centre more than 10 cm under it. Not counted: paths / decals,
// nature / scatter patches and line assets (they conform per plant / segment), anything resting on a host's floor (inside an enterable or pinned footprint and over the ground there), pads with a
// retaining skirt. Writes selftest_out/floaters_<map>.csv and logs "[floaters] <map>: N floating, M sunk".
// the engine's pad core (terrain_blend.cpp padHalf / padOff): an earthwork's walls ("core": [hx, hz, cx, cz]), else the footprint
static vec2 padHalf(const AssetDef* a) { const json& e = a->raw.is_object() && a->raw.contains("earthwork") ? a->raw["earthwork"] : json();
  return e.is_object() && e.contains("core") ? vec2(e["core"][0].get<float>(), e["core"][1].get<float>()) : a->footprint * 0.5f; }
static vec2 padOff(const AssetDef* a) { const json& e = a->raw.is_object() && a->raw.contains("earthwork") ? a->raw["earthwork"] : json();
  return e.is_object() && e.contains("core") && e["core"].size() >= 4 ? vec2(e["core"][2].get<float>(), e["core"][3].get<float>()) : vec2(0); }
bool floaterScan(App& app, float dt) {
  static int pm = -1, idx = 0, stage = 0; static float t = 0; static std::vector<std::string> maps;
  if (pm < 0) { pm = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--find-floaters") pm = 1;
    if (pm) { const char* e = std::getenv("NMK_FLOAT_MAPS"); std::stringstream ss(e && *e ? e : "dday"); std::string it; while (std::getline(ss, it, ',')) if (!it.empty()) maps.push_back(it); } }
  if (pm != 1) return false;
  if (dt < 0) return true;
  t += dt;
  if (idx >= (int)maps.size()) { app.selftest->exitCode = 0; app.selftest->done = true; return true; }
  if (stage == 0) {
    bool ok = app.openMap(contentDir() / "templates" / (maps[idx] + ".nmap.json"));
    logf("[floaters] open %s: %s", maps[idx].c_str(), ok ? "ok" : "FAILED");
    if (!ok) { ++idx; return true; }
    stage = 1; t = 0; return true;
  }
  if (t < 2.0f) return true;   // pads and cuts settle
  const Terrain& T = app.terrain;
  std::vector<const Instance*> hosts;
  for (auto& i : app.doc.instances) { const AssetDef* a = app.assetFor(i); if (a && (a->enterable || blendPinned(a))) hosts.push_back(&i); }
  auto toW = [](const Instance& i, vec2 l) { float r = radians(i.yaw), c = std::cos(r), s = std::sin(r); return vec2(i.pos.x + l.x * c + l.y * s, i.pos.z - l.x * s + l.y * c); };
  auto onHost = [&](const Instance& p) {
    for (auto* h : hosts) {
      if (h->id == p.id) continue;
      const AssetDef* ha = app.assetFor(*h); float r = radians(-h->yaw), c = std::cos(r), s = std::sin(r);
      vec2 d(p.pos.x - h->pos.x, p.pos.z - h->pos.z), l(d.x * c + d.y * s, -d.x * s + d.y * c);
      if (std::fabs(l.x) <= ha->footprint.x * 0.5f + 0.2f && std::fabs(l.y) <= ha->footprint.y * 0.5f + 0.2f && p.pos.y > T.placementHeight(p.pos.x, p.pos.z, p.id) + 0.08f) return true;
    }
    return false;
  };
  std::ofstream csv(exeDir() / "selftest_out" / ("floaters_" + maps[idx] + ".csv"));
  csv << "kind,asset,id,x,y,z,yaw,gap_m,category\n";
  int nf = 0, ns = 0;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a || a->path.on) continue;
    // conforming by construction: nature / scatter patches (each plant on the ground, nature.cpp), line assets (each
    // segment on the ground; the instance position is only the anchor)
    if (a->nature.kind != NatureDef::NONE || a->raw.contains("scatter") || a->raw.contains("line")) continue;
    if (onHost(i) || supportOf(i)) continue;   // ED1: rests on its supporter (a deck, a table, a roof), not on the ground
    bool skirted = false;
    for (auto& st : T.stamps) if (st.owner == i.id && st.skirt > 0) skirted = true;
    std::vector<vec2> L;
    if (a->modelFoliage) L = {vec2(0), vec2(0.15f, 0), vec2(-0.15f, 0), vec2(0, 0.15f), vec2(0, -0.15f)};   // NT8 trees / plants / rocks: the trunk or the middle, not the crown's corners
    else if (a->tree.on) { float r = std::max(0.1f, a->tree.trunkR * 0.8f); L = {vec2(0), vec2(r, 0), vec2(-r, 0), vec2(0, r), vec2(0, -r)}; }
    else if (blendPinned(a)) { vec2 h = glm::max(padHalf(a) - vec2(0.15f), vec2(0.02f)), o = padOff(a);   // the engine's pad core (an earthwork's walls), as blendValidate
      L = {o, o + vec2(-h.x, -h.y), o + vec2(h.x, -h.y), o + vec2(h.x, h.y), o + vec2(-h.x, h.y), o + vec2(0, -h.y), o + vec2(0, h.y), o + vec2(-h.x, 0), o + vec2(h.x, 0)}; }
    else { vec2 h = glm::max(a->footprint * 0.5f - vec2(0.1f), vec2(0.02f)); L = {vec2(0), vec2(-h.x, -h.y), vec2(h.x, -h.y), vec2(h.x, h.y), vec2(-h.x, h.y), vec2(0, -h.y), vec2(0, h.y), vec2(-h.x, 0), vec2(h.x, 0)}; }
    float gap = -1e9f, sink = 0;
    for (size_t k = 0; k < L.size(); ++k) {
      vec2 w = toW(i, L[k]); float d = i.pos.y - T.placementHeight(w.x, w.y, i.id);
      gap = std::max(gap, d); if (k == 0) sink = -d;
    }
    const char* kind = nullptr; float v = 0;
    // NT5: resting on another placed structure (a helicopter on a raised pad, a crate on a deck): a short ray down
    // from just above the base hits a placed instance's collider, not the terrain, within 30 cm
    bool onStructure = false;
    if (gap > 0.05f) { Physics::Hit h; if (Physics::castRayEx(i.pos + vec3(0, 0.25f, 0), vec3(0, -1, 0), 0.55f, h) && h.ud != 0 && (h.ud >> 60) != 0xF && (h.ud >> 16) != i.id) onStructure = true; }
    if (gap > 0.05f && !skirted && !onStructure) { kind = "floating"; v = gap; ++nf; }
    else if (sink > (a->tree.on ? 0.35f : a->modelFoliage ? (a->category == "Trees" ? 0.35f : 0.5f * std::max(0.1f, a->bmax.y - a->bmin.y)) : !a->defense.kind.empty() ? 0.30f : a->footprint.x * a->footprint.y < 6.0f ? 0.20f : 0.10f) && a->navMeta.terrainCuts.empty() && !blendTrench(a)) { kind = "sunk"; v = sink; ++ns; }   // a trunk or a small piece into a slope is natural
    if (kind) csv << kind << "," << a->id << "," << i.id << "," << i.pos.x << "," << i.pos.y << "," << i.pos.z << "," << i.yaw << "," << v << "," << a->category << "\n";
  }
  logf("[floaters] %s: %d floating, %d sunk (of %zu instances)", maps[idx].c_str(), nf, ns, app.doc.instances.size());
  ++idx; stage = 0; t = 0;
  return true;
}
