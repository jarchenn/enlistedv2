// WSM2 --capture-wsm2: every human soldier type on the shared skeleton, side by side. Team Deathmatch on village_assault_3, the
// player with one plain Allied soldier on side 0; on side 1 (Axis) one each of: plain soldier, Allied soldiers, SS, gas-mask 1 / 2,
// desert, Delta Force, operators, Allied paratroopers. The AI frozen, the bots stood in a row facing the camera, then posed through
// aim (own gun), aim with an LMG (far grip), aim with a pistol, idle, walk, run, crouch, a shot, a reload at 40 %, a throw wind-up:
// selftest_out/wsm2_<state>.png and per-bot facts in report_wsm2.txt (model, hold body, clips, head height).
// NMK_WSM2=match: 10 against 10, mixed types on both sides, the AI on for 60 s: no crash, shots fired, the frame time.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "physics.h"
#include "scenario.h"
#include "character.h"
#include "grenade.h"
#include "settings.h"
#include "editor_nav.h"
#include <GLFW/glfw3.h>
#include <map>

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0; bool started = false, matchMode = false, blurMode = false, costMode = false, blur2 = false, stepMode = false, cpabMode = false, hitMode = false, smokeMode = false, wxMode = false, wedgeMode = false, w6Mode = false; float total = 0, st = 0, runT = 0; int runFrames = 0; double runDt = 0, worstDt = 0;
std::vector<int> row; vec3 origin(0);
const char* kStates[] = {"aim", "aim_lmg", "aim_pistol", "idle", "walk", "run", "crouch", "shot", "reload", "throw"};
}   // namespace

// AIB1 NMK_WSM2=blur: one soldier standing, one walking across the view at 3, 8 and 20 m from a still camera, per configuration
// (Medium / High, the soldiers' own TAA motion on / off, TAAU off, Classic lighting, specular AA off): selftest_out/aib1_<cfg>_<d>.png.
// shortcut: the walker's feet are moved by the test (the AI is frozen), its clip from dbgVel.
// NMK_WSM2=blur2 (AIB1b): the camera strafing past the walker at 8 m (3 m/s, Medium, before / after), and the stander idle and
// aiming level at the camera at 3 m (High): the clip each plays is noted.
struct BlurShot { const char* name; GfxPreset preset; bool charVel, taau, lightNew, specAA; float d; bool strafe, aim; };
static std::vector<BlurShot> blurShots(bool second) {
  std::vector<BlurShot> v;
  if (second) {
    v = {{"strafe_med_before", GfxPreset::Medium, false, true, true, true, 8, true, false}, {"strafe_med_after", GfxPreset::Medium, true, true, true, true, 8, true, false},
         {"pose_idle", GfxPreset::High, true, true, true, true, 3, false, false}, {"pose_aim", GfxPreset::High, true, true, true, true, 3, false, true}};
    return v;
  }
  const BlurShot cfg[] = {{"med_before", GfxPreset::Medium, false, true, true, true}, {"med_after", GfxPreset::Medium, true, true, true, true},
                          {"med_native", GfxPreset::Medium, false, false, true, true}, {"high_before", GfxPreset::High, false, true, true, true},
                          {"high_after", GfxPreset::High, true, true, true, true}, {"med_classic", GfxPreset::Medium, false, true, false, true},
                          {"med_nospecaa", GfxPreset::Medium, false, true, true, false}};
  for (const BlurShot& c : cfg) for (float d : {3.0f, 8.0f, 20.0f}) { v.push_back(c); v.back().d = d; }
  return v;
}
static bool blurStep(App& app, Game& G, Soldier& L, bool second) {
  static std::vector<int> two; static vec3 eye0; static int k = -1; static std::vector<BlurShot> shots;
  if (two.empty()) {
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && two.size() < 2) two.push_back(s.slot);
    if (two.size() < 2) { T->check(false, "two soldiers to film"); app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; }
    eye0 = L.feet; app.eyeYaw = 0; app.eyePitch = 0; shots = blurShots(second);
    for (int i : two) { const Soldier& s = G.sold[i]; char b[200]; std::snprintf(b, sizeof b, "soldier %d: model %s, hold body %d", i, G.models[s.model]->id.c_str(), (int)s.holdBody); T->note(b); }
    k = 0; frames = 0;
  }
  if (k >= (int)shots.size()) { if (frames > 4) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; } return true; }
  const BlurShot& c = shots[k]; const float d = c.d;
  if (frames == 1) {
    if (k == 0 || shots[k - 1].preset != c.preset) settings().applyPreset(app, c.preset);
    app.R.perf.charVel = c.charVel; app.R.perf.taau = c.taau; app.R.perf.specAA = c.specAA; app.R.q.lightNew = c.lightNew;
    app.R.resetTemporal();
  }
  // the camera's frame from the start (yaw 0); strafing: the eye slides along its right at 3 m/s, centred on the shot frame
  const vec3 f = App::forward(0, 0), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  const vec3 eyeFeet = eye0 + (c.strafe ? r * (3.0f * (frames - 70) / 60.0f) : vec3(0));
  Physics::setCharacterPos(eyeFeet + vec3(0, 0.9f, 0)); app.eyeYaw = 0;
  vec3 eye, ef; G.renderView(eye, ef);
  const vec3 mid = eye0 + f * d;
  app.eyePitch = degrees(std::atan2(app.terrain.height(mid.x, mid.z) + 1.1f - eye.y, d));
  for (int i = 0; i < 2; ++i) {
    Soldier& s = G.sold[two[i]]; s.hp = 100; s.posture = Physics::STAND;
    const bool hot = i == 0 && c.aim;
    s.br.target = hot ? L.slot : -1; if (!hot) s.lastShot = G.t - 10;
    vec3 p = mid + r * (i == 0 ? -1.6f : -0.7f + std::fmod(frames / 60.0f * 1.5f, 2.4f));   // the stander left of centre; the walker left to right at 1.5 m/s
    p.y = app.terrain.height(p.x, p.z);
    Physics::setAgentPos(s.agent, p); s.feet = p;
    // App::forward(yaw) faces (-sin, -cos): the stander faces the camera (-f), the walker the way it walks (+r)
    s.yaw = i == 0 ? degrees(std::atan2(f.x, f.z)) : degrees(std::atan2(-r.x, -r.z));
    if (hot) { s.br.aimYaw = s.anGunYaw = s.yaw; s.br.aimPitch = s.anGunPitch = 0; }
    s.dbgVel = i == 1 ? App::forward(s.yaw, 0) * 1.5f : vec3(0);
  }
  if (frames == 70) {
    char b[300]; std::snprintf(b, sizeof b, "aib1_%s_%02d.png", c.name, (int)d);
    app.uiShot = exeDir() / "selftest_out" / b;
    for (int i = 0; i < 2; ++i) {   // which clip each plays (the pose question)
      const Soldier& s = G.sold[two[i]]; const CharModel& m = *G.models[s.model];
      std::snprintf(b, sizeof b, "%s: %s plays '%s'%s%s %.0f%%, hot %d, aim pitch %.1f deg", c.name, i ? "walker" : "stander", s.anim.a >= 0 ? m.clips[s.anim.a].name.c_str() : "-",
                    s.anim.b >= 0 && s.anim.b != s.anim.a ? " -> " : "", s.anim.b >= 0 && s.anim.b != s.anim.a ? m.clips[s.anim.b].name.c_str() : "", s.anim.w * 100, (int)(s.br.target >= 0), s.br.aimPitch);
      T->note(b);
    }
  }
  if (frames > 74) { ++k; frames = 0; }
  return true;
}

// Weather W6 NMK_WSM2=w6look: stills of a smoke cloud (16 m ahead) and a grenade explosion's puff (10 m) in Clear at noon, in
// Dense fog, and in Clear at 07:30 looking into the low sun; each with the particles lit and fogged (fxlit, fxsoft) and without.
static bool w6Step(App& app, Game& G, Soldier& L) {
  struct Sc { const char* name; int wx; float hour; bool intoSun; }; static const Sc sc[] = {{"clear", 0, 12.0f, false}, {"densefog", 9, 12.0f, false}, {"lowsun", 0, 7.5f, true}};
  static int k = -1, f0 = 0; static vec3 eye0;
  static const int only = std::getenv("NMK_W6_SCENE") ? std::atoi(std::getenv("NMK_W6_SCENE")) : -1;   // one scene (0 clear, 1 dense fog, 2 low sun)
  const int nS = only >= 0 ? 1 : 3, phase = k < 0 ? 0 : k % 2; const Sc& c = sc[only >= 0 ? only : std::max(0, k) / 2 % nS];
  if (k < 0) { k = 0; f0 = frames; eye0 = L.feet; settings().applyPreset(app, GfxPreset::High); }
  app.R.q.lightNew = true; app.R.wx.target = c.wx; app.R.wx.userSet = true; app.R.lt.hour = c.hour;
  Physics::setCharacterPos(eye0 + vec3(0, 0.9f, 0)); app.scriptedInput = ExploreInput{};
  if (c.intoSun) { vec3 sd = app.R.sunDir; app.eyeYaw = degrees(std::atan2(-sd.x, -sd.z)); } else app.eyeYaw = 20;
  app.eyePitch = 2;
  for (auto& s : G.sold) if (s.inUse && !s.localPlayer) s.hp = 100;
  app.R.perf.fxLit = app.R.perf.fxSoft = phase == 0;
  const int f = frames - f0;
  const vec3 fw = App::forward(app.eyeYaw, 0), rt = glm::normalize(glm::cross(fw, vec3(0, 1, 0)));
  if (f == 2) { app.R.wx.snap(); vec3 cp = eye0 + fw * 16.0f - rt * 3.0f; cp.y = app.terrain.height(cp.x, cp.z); G.gren.addCloud(cp, G.t - 4.0, 21 + k); }
  if (f == 150) { vec3 ep = eye0 + fw * 10.0f + rt * 3.0f; ep.y = app.terrain.height(ep.x, ep.z); app.fx.explosion(ep, vec3(0, 1, 0), "grenade", true); }
  if (f == 175) { char b[96]; std::snprintf(b, sizeof b, "w6_%s_%s.png", c.name, phase == 0 ? "lit" : "unlit"); app.uiShot = exeDir() / "selftest_out" / b; }
  if (f > 180) { G.gren.clear(); f0 = frames; if (++k >= nS * 2) { T->check(true, "six stills written"); if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; } }
  return true;
}

// Weather NMK_WSM2=wxsight: the bots' sight follows the weather state. A bot detects only while Renderer::fogT along the
// sight line is over 0.06 (ai_combat.cpp), so it sees to 2.81 / sigma = 0.72 x the preset's visibility V = min(visM,
// dustVisM): per preset, fogT at V / 2 must pass and at V must not; Clear must not limit a 200 m sight line.
static bool wxSightStep(App& app, Game& G, Soldier& L) {
  struct P { int id; const char* name; }; static const P ps[] = {{0, "Clear"}, {9, "Dense fog"}, {11, "Blizzard"}, {14, "Sandstorm"}};
  static int k = -1, f0 = 0; char b[300];
  auto force = [&](int id) { app.R.q.lightNew = true; app.R.wx.target = id; app.R.wx.userSet = true; app.R.wx.snap(); };   // as NMK_WEATHER does (a host resets wx.forced each frame)
  if (k < 0) { k = 0; f0 = frames; force(ps[0].id); }
  if (frames - f0 < 40) return true;   // the state settles (weatherDerive each frame)
  const auto& D = app.R.wx.der; const vec3 a = app.eyePos(), h = glm::normalize(vec3(App::forward(app.eyeYaw, 0).x, 0, App::forward(app.eyeYaw, 0).z));
  const float V = D.dustVisM > 0 ? std::min(D.visM, D.dustVisM) : D.visM;
  if (ps[k].id == 0) {
    const float Tc = app.R.fogT(a, a + h * 200.0f);
    std::snprintf(b, sizeof b, "%s: physical %d, a 200 m sight line keeps fogT %.3f (detection needs > 0.06)", ps[k].name, (int)D.phys, Tc);
    T->check(Tc > 0.06f, b);
  } else {
    const float Th = app.R.fogT(a, a + h * (0.5f * V)), Tv = app.R.fogT(a, a + h * V);
    std::snprintf(b, sizeof b, "%s: physical %d, visibility %.0f m (sigma %.4f /m): fogT %.3f at V/2 (seen), %.3f at V (not seen); the bots see to %.0f m", ps[k].name, (int)D.phys, V, D.sigma, Th, Tv, 2.81f / std::max(D.sigma, 1e-6f));
    T->check(D.phys && Th > 0.06f && Tv < 0.06f, b);
  }
  if (++k >= (int)(sizeof(ps) / sizeof(ps[0]))) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; }
  force(ps[k].id); f0 = frames;
  return true;
}

// Weather 7 NMK_WSM2=smoke: the camera inside one smoke cloud, then inside three (Medium); fxnear (near fade + screen-size cap)
// switched on / off every 60 frames (interleaved): the GP_Fx pass and the GPU frame per state; a shot of each at the end.
static bool smokeStep(App& app, Game& G, Soldier& L) {
  static vec3 eye0; static int f0 = -1, k0Outside = -1; static double acc[2][2][3] = {}; static int n[2][2] = {};
  if (f0 < 0) { f0 = frames; eye0 = L.feet; settings().applyPreset(app, GfxPreset::Medium); G.gren.addCloud(eye0 + vec3(0, 1.0f, 0), G.t, 7); }
  if (k0Outside < 0) { k0Outside = 0; G.gren.addCloud(eye0 + App::forward(20, 0) * 16.0f + vec3(0, 0.5f, 0), G.t, 11); }   // W6: one seen from outside too
  const int k = frames - f0;
  Physics::setCharacterPos(eye0 + vec3(0, 0.9f, 0)); app.eyeYaw = 20; app.eyePitch = 0; app.scriptedInput = ExploreInput{};
  for (auto& s : G.sold) if (s.inUse && !s.localPlayer) { s.hp = 100; s.dbgVel = vec3(0); }
  if (k == 600) { G.gren.addCloud(eye0 + vec3(2.5f, 1.0f, -2.0f), G.t, 8); G.gren.addCloud(eye0 + vec3(-2.5f, 1.0f, -3.0f), G.t, 9); }
  const int phase = k < 600 ? 0 : 1, on = (k / 60) % 2;
  app.R.perf.fxNear = on; app.R.perf.fxLit = app.R.perf.fxSoft = !(k >= 380 && k < 440);   // W6 look check: lit / soft off for 60 frames
  if (k == 430 || k == 490) { char b[64]; std::snprintf(b, sizeof b, "smoke_w6_%s.png", k == 430 ? "off" : "on"); app.R.perf.fxNear = true; app.uiShot = exeDir() / "selftest_out" / b; }
  const bool meas = (k > 180 && k < 580) || (k > 780 && k < 1180);
  if (meas && k % 60 > 5) { acc[phase][on][0] += app.R.gpuPassMs[GP_Fx]; acc[phase][on][1] = std::max(acc[phase][on][1], (double)app.R.gpuPassMs[GP_Fx]); acc[phase][on][2] += app.R.stats.gpuMs; ++n[phase][on]; }
  if (k == 510 || k == 570 || k == 1110 || k == 1170) { char b[64]; std::snprintf(b, sizeof b, "smoke_%d_%s.png", phase ? 3 : 1, (k / 60) % 2 ? "fxnear" : "before"); app.uiShot = exeDir() / "selftest_out" / b; }
  if (k == 1190) {
    char b[300];
    for (int p = 0; p < 2; ++p) for (int o = 0; o < 2; ++o) {
      std::snprintf(b, sizeof b, "inside %d cloud%s, fxnear %s: %d frames, GP_Fx avg %.3f ms, max %.3f ms, GPU frame %.2f ms, eye smoke sigma %.2f /m", p ? 3 : 1, p ? "s" : "", o ? "on " : "off",
                    n[p][o], acc[p][o][0] / std::max(1, n[p][o]), acc[p][o][1], acc[p][o][2] / std::max(1, n[p][o]), app.R.smokeSigma);
      T->note(b);
    }
    T->check(n[0][0] > 50 && n[1][1] > 50, "both cloud counts measured in both states");
    if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true;
  }
  return true;
}

// CP NMK_WSM2=cphit: hits read the pose, and the pose LOD holds poses off view. (1) A soldier walking 120 m behind the camera
// (off view, pose held) is shot at the head of its up-to-date pose through the server's bullet path (coarse sphere -> on-demand
// pose -> zones): a head hit; the held pose's answer for the same ray is noted. (2) The same soldier goes prone: it is posed at once
// and while it settles, and afterwards a ray at standing chest height misses while one through the prone torso hits.
static bool hitStep(App& app, Game& G, Soldier& L) {
  static std::vector<int> two; static vec3 eye0, f, r; char b[300];
  if (two.empty()) {
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && two.size() < 2) two.push_back(s.slot);
    if (two.size() < 2) { T->check(false, "two soldiers"); app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; }
    eye0 = L.feet; f = App::forward(0, 0); r = glm::normalize(glm::cross(f, vec3(0, 1, 0))); frames = 0;
    for (int i = 0; i < 2; ++i) { Soldier& s = G.sold[two[i]]; vec3 p = eye0 - f * (120.0f + 6 * i); p.y = app.terrain.height(p.x, p.z) + 0.05f; Physics::setAgentPos(s.agent, p); s.feet = s.prevFeet = p; s.posture = Physics::STAND; }
  }
  app.eyeYaw = 0; app.eyePitch = 0; Physics::setCharacterPos(eye0 + vec3(0, 0.9f, 0));
  Soldier& V = G.sold[two[0]]; V.hp = 100; V.br.target = -1; V.lastShot = G.t - 10;
  V.yaw = degrees(std::atan2(-r.x, -r.z)); V.dbgVel = frames < 100 ? r * 2.5f : vec3(0);
  auto ray = [&](vec3 target, vec3 from, int& zone, bool onDemand) {   // the server path (Game::projectileSegment) without the damage
    vec3 d = glm::normalize(target - from); HitZone hz;
    if (onDemand && V.poseAcc > 0 && G.coarseHit(V, from, d)) G.poseNow(V);
    bool hit = G.hitTest(V, from, d, 400.0f, hz); zone = hit ? hz.zone : -1; return hit;
  };
  auto jw = [&](int j) { return vec3(V.world * vec4(V.pose.joint[j], 1)); };
  if (frames == 80) {   // (1)
    const float held = V.poseAcc; const vec3 Hh = jw(CJ_HEAD); const PoseResult heldPose = V.pose; const mat4 heldW = V.world;
    const int od0 = G.posesOnDemand;
    const vec3 from = Hh + r * 6.0f + vec3(0, 0.3f, 0);   // a shooter 6 m to the side
    G.poseNow(V); const vec3 Hf = jw(CJ_HEAD);   // the up-to-date pose (what a client, posing it every frame, aims at)
    const PoseResult fresh = V.pose; const mat4 freshW = V.world;
    V.pose = heldPose; V.world = heldW; V.poseAcc = held;
    int zHeld = -1; HitZone hz; { vec3 d = glm::normalize(Hf - from); zHeld = G.hitTest(V, from, d, 400.0f, hz) ? hz.zone : -1; }
    int zNow = -1; ray(Hf, from, zNow, true);
    std::snprintf(b, sizeof b, "(1) walking 120 m behind the camera, pose held %.2f s: the head moved %.3f m since; a shot at the up-to-date head: held pose answers zone %d, the server path (on-demand pose, %d evaluated) zone %d",
                  held, glm::length(Hf - Hh), zHeld, G.posesOnDemand - od0, zNow);
    T->check(held > 0 && zNow == 0 && G.posesOnDemand > od0, b); (void)fresh; (void)freshW;
  }
  if (frames == 100) { V.posture = Physics::PRONE; }
  if (frames == 101) { std::snprintf(b, sizeof b, "(2) prone at 120 m off view: posed the next frame (time held %.3f s)", V.poseAcc); T->check(V.poseAcc <= 1.0f / 30, b); }
  if (frames == 160) {
    const vec3 head = jw(CJ_HEAD), chest = jw(CJ_SPINE2); int zs = -1, zp = -1;
    const vec3 standChest = V.feet + vec3(0, 1.3f, 0);
    const bool hs = ray(standChest, standChest + r * 6.0f, zs, true), hp = ray(chest, chest + r * 6.0f + vec3(0, 0.2f, 0), zp, true);
    std::snprintf(b, sizeof b, "(2) settled prone: head %.2f m over the feet; a ray at standing chest height %s, one through the prone torso %s (zone %d)", head.y - V.feet.y, hs ? "HITS" : "misses", hp ? "hits" : "MISSES", zp);
    T->check(!hs && hp && head.y - V.feet.y < 0.6f, b);
    if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true;
  }
  return true;
}

// CP NMK_WSM2=step (run with NMK_SELFTEST_DT=0.0222: 45 fps, 1-2 fixed steps a frame): the player strafes right, a soldier walks
// across 8 m ahead through the real steps (dbgVel); per drawn frame the walker's and the eye's position across the view. 150 frames
// drawing the last step as it is, then 150 drawn between the last two steps: the spread of the per-frame movement is the stepping.
static bool stepStep(App& app, Game& G, Soldier& L) {
  static std::vector<int> two; static vec3 eye0, r0; static std::vector<float> wx[2], ex[2], stepv[2]; static vec3 lastW, lastE, lastF; static int ph = -1; static long lastSteps = 0;
  if (two.empty()) {
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && two.size() < 2) two.push_back(s.slot);
    if (two.size() < 2) { T->check(false, "two soldiers"); app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; }
    eye0 = L.feet; app.eyeYaw = 0; app.eyePitch = -3; frames = 0;
    const vec3 f = App::forward(0, 0); r0 = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
    for (int i = 0; i < 2; ++i) { Soldier& s = G.sold[two[i]]; vec3 p = eye0 + f * (8.0f + 4 * i) - r0 * 3.0f; p.y = app.terrain.height(p.x, p.z) + 0.05f; Physics::setAgentPos(s.agent, p); s.feet = s.prevFeet = p;
      s.yaw = degrees(std::atan2(-r0.x, -r0.z)); s.dbgVel = r0 * 3.0f; s.br.target = -1; s.hp = 100; }
  }
  app.eyeYaw = 0; app.eyePitch = -3;
  for (int i : two) { Soldier& s = G.sold[i]; s.yaw = degrees(std::atan2(-r0.x, -r0.z)); s.dbgVel = r0 * 3.0f; s.hp = 100; }
  app.scriptedInput = ExploreInput{}; app.scriptedInput.move = vec2(1, 0);   // strafe right
  const int phase = frames < 160 ? 0 : 1;
  if (phase != ph) { ph = phase; app.interpOff = phase == 0; }
  const Soldier& w = G.sold[two[0]];
  const vec3 W = vec3(w.world[3]), E = app.eyePos();
  const int k = frames % 160;
  const long nst = (long)G.steps - lastSteps;
  if (k > 10 && k < 150) { wx[phase].push_back(glm::dot(W - lastW, r0)); ex[phase].push_back(glm::dot(E - lastE, r0)); if (nst > 0) stepv[phase].push_back(glm::dot(w.feet - lastF, r0) / nst); }
  lastW = W; lastE = E; lastF = w.feet; lastSteps = (long)G.steps;
  if (frames == 158 || frames == 318) {   // reset the walker and the player to the start for the second half
    for (int i = 0; i < 2; ++i) { Soldier& s = G.sold[two[i]]; vec3 p = eye0 + App::forward(0, 0) * (8.0f + 4 * i) - r0 * 3.0f; p.y = app.terrain.height(p.x, p.z) + 0.05f; Physics::setAgentPos(s.agent, p); s.feet = s.prevFeet = p; }
    Physics::setCharacterPos(eye0 + vec3(0, 0.9f, 0));
  }
  if (frames == 320) {
    auto stat = [](const std::vector<float>& v, double& mean, double& sd) { mean = 0; for (float x : v) mean += x; mean /= std::max<size_t>(1, v.size()); sd = 0; for (float x : v) sd += (x - mean) * (x - mean); sd = std::sqrt(sd / std::max<size_t>(1, v.size())); };
    char b[300]; double sdOn[2] = {};
    for (int p = 0; p < 2; ++p) {
      double wm, ws, em, es, rm, rs; stat(wx[p], wm, ws); stat(ex[p], em, es);
      std::vector<float> rel(wx[p].size()); for (size_t i = 0; i < rel.size(); ++i) rel[i] = wx[p][i] - ex[p][i]; stat(rel, rm, rs);
      double sm, ss; stat(stepv[p], sm, ss); const double pf = 60.0 * 0.0222;   // the walker's own per-step movement, scaled to a frame (its speed's own spread)
      std::snprintf(b, sizeof b, "%s: per drawn frame, walker %.4f m (sd %.4f), eye %.4f m (sd %.4f), walker against the eye sd %.4f m; the walker's own per-step movement x frame/step %.4f m (sd %.4f) (%zu frames)",
                    p ? "drawn between the last two steps" : "the last step as it is", wm, ws, em, es, rs, sm * pf, ss * pf, wx[p].size());
      T->note(b); sdOn[p] = std::max(std::sqrt(std::max(0.0, ws * ws - ss * pf * ss * pf)), es);   // the spread beyond its own speed's
    }
    std::snprintf(b, sizeof b, "stepping (per-frame spread of the walker beyond its own speed's / of the eye): %.4f m without interpolation, %.4f m with", sdOn[0], sdOn[1]);
    T->check(sdOn[1] < 0.35 * sdOn[0] + 0.002, b);
    app.uiShot = exeDir() / "selftest_out" / "cp_step.png";
  }
  if (frames > 324) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}

// CP NMK_WSM2=cpab (with NMK_SELFTEST_REALTIME=1): 100 combatants, the AI live, the player still and turning 18 deg/s. After a 10 s
// warm-up the pose / step settings cycle every 6 s through five configurations, four rounds (interleaved, so other load on the
// machine hits all alike); per configuration: wall frame ms (mean, p95), main-thread pose ms per frame, fixed steps per frame.
struct CpCfg { const char* name; bool perStep, lodOff, serial, cap8, interpOff; };
static const CpCfg kCp[] = {{"old: poses per step, no LOD, serial, cap 8", true, true, true, true, true},
                            {"1 poses once per frame", false, true, true, true, true},
                            {"2 + rate / view LOD", false, false, true, true, true},
                            {"3 + pose workers", false, false, false, true, true},
                            {"4 + cap 4, interpolation", false, false, false, false, false}};
static bool cpabStep(App& app, Game& G, Soldier& L) {
  static double t0 = -1, last = -1, lastPose = 0; static uint64_t lastSteps = 0; static int cur = -1;
  static std::vector<float> fr[5], pm[5], sp[5];
  const double now = glfwGetTime(); if (t0 < 0) { t0 = now; last = now; lastPose = G.poseMs; lastSteps = G.steps; }
  app.eyeYaw = std::fmod(app.eyeYaw + 18.0f * (float)(now - last), 360.0f); app.eyePitch = -2; app.scriptedInput = ExploreInput{};
  const double el = now - t0 - 10.0; const int slot = el < 0 ? -1 : (int)(el / 6.0), cfg = slot < 0 ? 4 : slot % 5;
  if (cfg != cur) { cur = cfg; const CpCfg& c = kCp[cfg]; auto& P = app.R.perf; P.poseFrame = !c.perStep; P.poseLod = !c.lodOff; P.poseJobs = !c.serial; P.stepInterp = !c.cap8; app.interpOff = c.interpOff; }
  if (slot >= 0 && slot < 20 && el - slot * 6.0 > 1.0) { fr[cfg].push_back((float)((now - last) * 1000)); pm[cfg].push_back((float)(G.poseMs - lastPose)); sp[cfg].push_back((float)(G.steps - lastSteps)); }
  last = now; lastPose = G.poseMs; lastSteps = G.steps;
  if (slot >= 20) {
    int bots = 0, alive = 0; for (auto& s : G.sold) if (s.inUse && !s.localPlayer) { ++bots; alive += s.alive; }
    char b[300]; std::snprintf(b, sizeof b, "%d bots (%d alive), village_assault_3, real time, Medium as set", bots, alive); T->note(b);
    for (int k = 0; k < 5; ++k) {
      auto mean = [](const std::vector<float>& v) { double a = 0; for (float x : v) a += x; return v.empty() ? 0.0 : a / v.size(); };
      std::vector<float> f = fr[k]; std::sort(f.begin(), f.end()); const double p95 = f.empty() ? 0 : f[std::min(f.size() - 1, (size_t)(f.size() * 0.95))];
      std::vector<float> q = pm[k]; std::sort(q.begin(), q.end()); const double q95 = q.empty() ? 0 : q[std::min(q.size() - 1, (size_t)(q.size() * 0.95))];
      std::snprintf(b, sizeof b, "%-44s %4zu frames: frame %.2f ms (p95 %.2f), poses %.2f ms main thread per frame (p95 %.2f), %.2f steps per frame",
                    kCp[k].name, fr[k].size(), mean(fr[k]), p95, mean(pm[k]), q95, mean(sp[k]));
      T->note(b);
    }
    T->check(bots >= 90 && !fr[4].empty(), "a 100-combatant match ran the five configurations");
    if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true;
  }
  return true;
}

// AIB1 NMK_WSM2=cost: 19 soldiers walking in a grid 5-25 m in front of a still camera (Medium), the soldiers' own TAA motion
// switched on / off every 60 frames (interleaved, so other load on the GPU hits both alike): GPU frame and Models pass time.
static bool costStep(App& app, Game& G, Soldier& L) {
  static std::vector<int> bots; static vec3 eye0; static double acc[2][3] = {}; static int n[2] = {}, phase = 0;
  if (bots.empty()) {
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer) bots.push_back(s.slot);
    eye0 = L.feet; settings().applyPreset(app, GfxPreset::Medium); frames = 0;
  }
  Physics::setCharacterPos(eye0 + vec3(0, 0.9f, 0)); app.eyeYaw = 0; app.eyePitch = -4;
  vec3 eye, ef; G.renderView(eye, ef);
  const vec3 f = glm::normalize(vec3(ef.x, 0, ef.z)), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  for (int i = 0; i < (int)bots.size(); ++i) {
    Soldier& s = G.sold[bots[i]]; s.hp = 100; s.br.target = -1; s.lastShot = G.t - 10;
    vec3 p = eye0 + f * (5.0f + 4.0f * (i / 5)) + r * (-4.0f + 2.0f * (i % 5) + std::fmod(frames / 60.0f * 1.5f, 2.0f));
    p.y = app.terrain.height(p.x, p.z); Physics::setAgentPos(s.agent, p); s.feet = p;
    s.yaw = degrees(std::atan2(r.x, r.z)); s.dbgVel = App::forward(s.yaw, 0) * 1.5f;
  }
  const int on = (frames / 60) % 2; app.R.perf.charVel = on;
  if (frames > 120 && frames % 60 > 5) { acc[on][0] += app.R.stats.gpuMs; acc[on][1] += app.R.gpuPassMs[GP_Models]; acc[on][2] += app.R.gpuPassMs[GP_Taa]; ++n[on]; }
  if (frames == 2520) {
    char b[300];
    for (int k = 0; k < 2; ++k) { std::snprintf(b, sizeof b, "charvel %s: %d frames, GPU frame %.3f ms, Models pass %.3f ms, TAA pass %.3f ms", k ? "on " : "off", n[k], acc[k][0] / n[k], acc[k][1] / n[k], acc[k][2] / n[k]); T->note(b); }
    std::snprintf(b, sizeof b, "cost of the soldiers' own TAA motion (19 walking in view, Medium): GPU frame %+.3f ms, Models pass %+.3f ms", acc[1][0] / n[1] - acc[0][0] / n[0], acc[1][1] / n[1] - acc[0][1] / n[0]);
    T->check(n[0] > 100 && n[1] > 100, b);
    app.uiShot = exeDir() / "selftest_out" / "aib1_cost.png";
  }
  if (frames > 2525) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}

bool wsm2Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-wsm2") mode = 1; const char* e = std::getenv("NMK_WSM2"); matchMode = e && std::string(e) == "match"; blurMode = e && (std::string(e) == "blur" || std::string(e) == "blur2"); blur2 = e && std::string(e) == "blur2"; costMode = e && std::string(e) == "cost"; stepMode = e && std::string(e) == "step"; if (stepMode) blurMode = true; cpabMode = e && std::string(e) == "cpab"; if (cpabMode) matchMode = true; hitMode = e && std::string(e) == "cphit"; if (hitMode) blurMode = true; smokeMode = e && std::string(e) == "smoke"; if (smokeMode) blurMode = true; wxMode = e && std::string(e) == "wxsight"; if (wxMode) blurMode = true; wedgeMode = e && std::string(e) == "wedge"; w6Mode = e && std::string(e) == "w6look"; if (w6Mode) blurMode = true; if (costMode) matchMode = true; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (app.win && glfwGetWindowAttrib(app.win, GLFW_ICONIFIED)) glfwRestoreWindow(app.win);
  if (!app.uiShot.empty()) return true;
  static float boot = 0; if (!started && (boot += dt) < 5.0f) return true;   // let start-up loading settle before opening the map (a load-time race in assets.cpp crashed early opens)
  if (wedgeMode) {   // chateau_town churchyard wedge: what physically blocks the 1 m the path asks for at (-92.4, 9.13, 26.2) -> (-93.4, 26.2)
    static int wf = 0; ++wf;
    if (wf == 1) {
      T = new MpTest("wsm2");
      const TemplateInfo* tp = app.findTemplate("chateau_town");
      bool ok = tp && navNewMapSized(app, *tp, "Wedge", vec2(tp->width, tp->depth));
      if (ok) {   // its own Assault with objective A taken out: the churchyard (B) is the attackers' goal, as when the soak wedged there
        Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json()) : json());
        if (!sc.objectives.empty()) sc.objectives.erase(sc.objectives.begin());
        for (int i = 0; i < (int)sc.objectives.size(); ++i) sc.objectives[i].order = i;
        for (auto& sp : sc.spawns) {   // the markers' phases follow (phase 0 = A is gone: its markers serve B)
          std::vector<int> ph; for (int q : sp.phases) { int r = std::max(0, q - 1); if (r < (int)sc.objectives.size() && std::find(ph.begin(), ph.end(), r) == ph.end()) ph.push_back(r); }
          sp.phases = ph.empty() ? std::vector<int>{0} : ph;
        }
        sc.rules.defenderSlots = 6; sc.rules.attackerAI = 12;
        gUi.matchScenario = sc.toJson(); gUi.loaded = false;
        if (!gGame) gGame = new Game(app);
        gGame->pendingStart = true;
      }
      T->check(ok && app.enterExplore() && gGame && gGame->active, "chateau_town opened from its template: its Assault without objective A");
      return true;
    }
    if (wf > 90 && gGame && gGame->active) {   // five attackers put at the wedge spot; the gate stays as the map has it (closed)
      Game& G = *gGame; static std::vector<int> five; static int opens0 = 0; static double t0 = 0; char b[300];
      if (five.empty()) {
        for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && s.role == CombatRole::Attacker && five.size() < 5) five.push_back(s.slot);
        for (int i = 0; i < (int)five.size(); ++i) { Soldier& s = G.sold[five[i]]; vec3 p(-92.4f + 0.6f * (i % 2), 9.2f, 26.0f - 0.7f * i); Physics::setAgentPos(s.agent, p); s.feet = s.prevFeet = p; }
        opens0 = G.doorOpens; t0 = G.t;
        std::snprintf(b, sizeof b, "%zu attackers placed south of the churchyard gate (closed)", five.size()); T->note(b);
      }
      for (int i : five) G.sold[i].hp = 100;   // the test is the way through, not the fight
      if (G.t - t0 > 60) {
        int through = 0; for (int i : five) through += G.sold[i].feet.z > 28.0f;
        for (int i : five) {   // where each ended, doing what, and its stuck events at the gate
          const Soldier& q = G.sold[i]; int ev = 0; std::string last;
          if (AiCore* A = aiCore()) for (auto& e : A->stuck) if (e.slot == i && std::fabs(e.pos.x + 93.0f) < 4 && std::fabs(e.pos.z - 26.5f) < 4) { ++ev; last = e.why; }
          std::snprintf(b, sizeof b, "bot %d: (%.1f %.1f %.1f) alive %d, %s; %d stuck events at the gate%s%s", i, q.feet.x, q.feet.y, q.feet.z, (int)q.alive, q.br.why.c_str(), ev, ev ? ", last: " : "", last.c_str());
          T->note(b);
        }
        std::snprintf(b, sizeof b, "in 60 s %d of %zu went north through the gate; doors opened by bots %d", through, five.size(), G.doorOpens - opens0);
        T->check(through >= 4, b);
        if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true;
      }
      return true;
    }
    if (wf == 90) {
      char b[400]; auto name = [&](uint64_t ud) -> std::string {
        if (ud == 0 || (ud >> 60) == 0xF) return "terrain";
        for (auto& in : app.doc.instances) if (in.id == (ud >> 16)) { char x[160]; std::snprintf(x, sizeof x, "%s #%llu part %d at (%.1f %.1f %.1f)", in.assetId.c_str(), (unsigned long long)in.id, (int)(ud & 0xFFFF) - 1, in.pos.x, in.pos.y, in.pos.z); return x; }
        char x[64]; std::snprintf(x, sizeof x, "ud 0x%llx", (unsigned long long)ud); return x;
      };
      const vec3 A(-92.4f, 9.13f, 26.2f), B(-93.4f, 9.13f, 26.2f);
      for (vec3 from : {A, vec3(-91.5f, 9.13f, 25.9f)}) for (float hgt : {0.1f, 0.3f, 0.6f, 1.0f, 1.5f}) {
        Physics::Hit h; const vec3 o = from + vec3(0, hgt, 0), d = glm::normalize(vec3(B.x - from.x, 0, B.z - from.z));
        const bool hit = Physics::castRayEx(o, d, 2.5f, h);
        std::snprintf(b, sizeof b, "ray from (%.1f %.2f %.1f) +%.1f m toward the waypoint: %s", from.x, from.y, from.z, hgt, hit ? (std::to_string(h.t).substr(0, 4) + " m: " + name(h.ud)).c_str() : "clear 2.5 m");
        T->note(b);
      }
      for (float ang = 0; ang < 360; ang += 45) {   // which directions out of the spot are free (a ring of rays at knee height)
        Physics::Hit h; const vec3 d(std::cos(radians(ang)), 0, std::sin(radians(ang)));
        const bool hit = Physics::castRayEx(A + vec3(0, 0.4f, 0), d, 2.0f, h);
        std::snprintf(b, sizeof b, "ring %3.0f deg (x %+.2f z %+.2f): %s", ang, d.x, d.z, hit ? (std::to_string(h.t).substr(0, 4) + " m: " + name(h.ud)).c_str() : "clear 2 m"); T->note(b);
      }
      { Physics::Hit h; Physics::castRayEx(A + vec3(0, 3, 0), vec3(0, -1, 0), 5, h); std::snprintf(b, sizeof b, "ground under the spot: y %.2f (%s), terrain height %.2f", h.pos.y, name(h.ud).c_str(), app.terrain.height(A.x, A.z)); T->note(b); }
      { Physics::Hit h; Physics::castRayEx(B + vec3(0, 3, 0), vec3(0, -1, 0), 5, h); std::snprintf(b, sizeof b, "ground under the waypoint: y %.2f (%s), terrain height %.2f", h.pos.y, name(h.ud).c_str(), app.terrain.height(B.x, B.z)); T->note(b); }
      T->check(true, "probes done");
    }
    return true;
  }
  if (!started) {
    started = true; T = new MpTest("wsm2");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / (blurMode ? "templates/blackwater_thaw.nmap.json" : "examples/village_assault_3.nmap.json"));   // AIB1: the map the blur was reported on
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.humanTeam = 0; sc.rules.killCap = 9999;
    sc.rules.teamFaction[0] = Faction::Allies; sc.rules.teamFaction[1] = Faction::Axis;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = 0; }
    const AiType types[] = {AiType::Soldiers, AiType::Allied, AiType::SS, AiType::GasMask, AiType::GasMask2, AiType::Desert, AiType::Delta, AiType::Operators, AiType::AlliedPara};
    if (blurMode) {   // AIB1: the player and two Axis soldiers (one standing, one walking across)
      sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 2; sc.rules.aiTypes[1] = aiBit(AiType::Soldiers); sc.rules.aiCount[1][(int)AiType::Soldiers] = 2;
    } else if (!matchMode) {
      sc.rules.teamPlayers[0] = 2; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiCount[0][(int)AiType::Soldiers] = 1;
      sc.rules.teamPlayers[1] = 9; for (AiType t : types) { sc.rules.aiTypes[1] |= aiBit(t); sc.rules.aiCount[1][(int)t] = 1; }
    } else {   // 10 a side (the player one of side 0's): the nine types spread over both sides
      sc.rules.teamPlayers[0] = 10; sc.rules.teamPlayers[1] = 10;
      const int nb = cpabMode ? 99 : 19; if (cpabMode) sc.rules.teamPlayers[0] = sc.rules.teamPlayers[1] = 50;   // CP: 100 combatants
      for (int i = 0; i < nb; ++i) { int side = i < nb / 2 ? 0 : 1; AiType t = types[i % 9]; sc.rules.aiTypes[side] |= aiBit(t); sc.rules.aiCount[side][(int)t] += 1; }
    }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, matchMode ? "Team Deathmatch starts, 10 a side, mixed types" : "Team Deathmatch starts, one bot of every human type");
    return true;
  }
  total += dt; st += dt; ++frames;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  Game* gp = gGame && gGame->active ? gGame : nullptr;
  if (!gp || total > 600) { T->check(false, "timed out / match ended at stage " + std::to_string(stage)); finish(); return true; }
  Game& G = *gp; Soldier* L = G.local();
  if (!L) return true;
  if (L->alive) L->hp = 100;
  AiCore* A = aiCore();
  char b[400];
  if (cpabMode) return cpabStep(app, G, *L);
  if (matchMode && !costMode) {   // the AI on for 60 s after a 4 s settle
    if (st < 4) return true;
    static double wall = 0; const double now = glfwGetTime(); const double wdt = wall > 0 ? now - wall : 0; wall = now;   // wall-clock frame time (dt is the fixed step)
    static std::map<int, vec3> start; if (runFrames == 0) for (auto& s : G.sold) if (s.inUse) start[s.slot] = s.feet;
    runT += dt; ++runFrames; runDt += wdt; worstDt = std::max(worstDt, wdt);
    if (runT > 60) {
      int alive = 0, shots = 0, hold = 0, total = 0, moved = 0;
      for (auto& s : G.sold) if (s.inUse && !s.localPlayer) { ++total; alive += s.alive; shots += s.shots; hold += s.holdBody; moved += glm::length(s.feet - start[s.slot]) > 5; }
      std::snprintf(b, sizeof b, "60 s of a 20-combatant mixed match: %d bots, %d alive now, %d on hold bodies, %d moved over 5 m, %d shots by bots, match phase %d, frame avg %.1f ms, worst %.1f ms",
                    total, alive, hold, moved, shots, (int)G.match.phase, 1000.0 * runDt / std::max(1, runFrames), 1000.0 * worstDt);
      T->check(total >= 19 && moved > 0, b); finish();
    }
    return true;
  }
  if (st < 3.0f) { if (A) A->frozen = true; return true; }
  if (stepMode) return stepStep(app, G, *L);
  if (hitMode) return hitStep(app, G, *L);
  if (smokeMode) return smokeStep(app, G, *L);
  if (wxMode) return wxSightStep(app, G, *L);
  if (w6Mode) return w6Step(app, G, *L);
  if (blurMode) return blurStep(app, G, *L, blur2);
  if (costMode) return costStep(app, G, *L);
  if (stage == 0) {   // the row: side 0's bot first, then side 1's in spawn order; facts per bot
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer) row.push_back(s.slot);
    origin = G.sold[row[0]].feet;
    for (int i = 0; i < (int)row.size(); ++i) {
      const Soldier& s = G.sold[row[i]]; const CharModel& m = *G.models[s.model];
      std::snprintf(b, sizeof b, "bot %d: team %d, kit %d, model %s, hold body %d, %zu clips, gun %s, head %.2f m", i, s.team, s.kit, m.id.c_str(), (int)s.holdBody, m.clips.size(),
                    s.weapon >= 0 ? app.weaponDefs[s.weapon]->id.c_str() : "-", s.pose.joint[CJ_HEAD].y);
      T->check(s.holdBody && m.clips.size() >= 60, b);
    }
    if (A) A->frozen = true; stage = 1; frames = 0; return true;
  }
  const int ns = (int)(sizeof(kStates) / sizeof(kStates[0]));
  if (stage - 1 >= ns) { if (frames > 4) finish(); return true; }
  const std::string what = kStates[stage - 1];
  const int n = (int)row.size();
  for (int i = 0; i < n; ++i) {   // stood in a row along x, facing +z (the camera)
    Soldier& s = G.sold[row[i]]; if (!s.alive) continue;
    vec3 p = origin + vec3((i - 0.5f * (n - 1)) * 1.25f, 0, 0);
    Physics::setAgentPos(s.agent, p); s.feet = p; s.yaw = 180; s.hp = 100;
    const bool hot = what != "idle" && what != "walk" && what != "run";
    s.br.aimYaw = s.anGunYaw = 180; s.br.aimPitch = s.anGunPitch = 0; s.br.target = hot ? L->slot : -1; if (!hot) s.lastShot = G.t - 10;
    s.dbgVel = what == "walk" ? vec3(0, 0, 1.5f) : what == "run" ? vec3(0, 0, 4.4f) : vec3(0);
    s.posture = what == "crouch" ? Physics::CROUCH : Physics::STAND;
    if (frames == 1) {
      int w = what == "aim_lmg" ? app.weaponIndex("modern_lmg") : what == "aim_pistol" ? app.weaponIndex("modern_glock34") : s.carried.empty() ? -1 : s.carried[0];
      if (w >= 0) { s.weapon = w; s.ws = WeaponState{}; s.ws.loaded = app.weaponDefs[w]->loaded; }
      s.reload = 0; s.throwT = -1; s.frags = 2; s.anim = AnimPlayer{}; s.anim.life = s.lifeId;
      if (what == "throw") G.startThrow(s, GK_FRAG, vec3(0, 4, -8), s.feet + vec3(0, 0, 10));
    }
    if (what == "reload") { s.reloadTotal = 3.0f; s.reload = 3.0f * (1.0f - std::min(0.4f, frames / 40.0f * 0.4f)); }
    if (what == "throw" && s.throwT >= 0) s.throwT = std::min(0.42f, frames / 40.0f * 0.42f);
    if (what == "shot" && frames == 38) { s.ws.cooldown = 0; s.ws.bolt = 0; if (s.ws.loaded <= 0) s.ws.loaded = 5; G.soldierFire(s, vec3(0, -0.02f, 1)); }
  }
  {   // the camera: in front of the row's middle, far enough for all of it
    vec3 at = origin + vec3(0, 0, 0.55f * 1.25f * n + 1.5f);
    Physics::setCharacterPos(at + vec3(0, 0.9f, 0));
    vec3 eye, ef; G.renderView(eye, ef); vec3 d = origin + vec3(0, 1.0f, 0) - eye;
    app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  }
  if (frames == 40) {
    std::string clips;
    for (int i = 0; i < n; ++i) { const Soldier& s = G.sold[row[i]]; const CharModel& m = *G.models[s.model]; clips += (i ? ", " : "") + (s.anim.a >= 0 ? m.clips[s.anim.a].name : std::string("-")) + (s.anim.b >= 0 && s.anim.b != s.anim.a ? " -> " + m.clips[s.anim.b].name + " " + std::to_string((int)std::lround(s.anim.w * 100)) + "%" : std::string()); }
    T->note(what + ": " + clips);
    app.uiShot = exeDir() / "selftest_out" / ("wsm2_" + what + ".png");
  }
  if (frames > 44) { for (int i : row) { G.sold[i].dbgVel = vec3(0); G.sold[i].reload = 0; G.sold[i].throwT = -1; } ++stage; frames = 0; }
  return true;
}
