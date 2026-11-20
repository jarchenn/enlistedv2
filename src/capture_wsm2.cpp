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
#include <GLFW/glfw3.h>
#include <map>

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0; bool started = false, matchMode = false, blurMode = false, costMode = false, blur2 = false; float total = 0, st = 0, runT = 0; int runFrames = 0; double runDt = 0, worstDt = 0;
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
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-wsm2") mode = 1; const char* e = std::getenv("NMK_WSM2"); matchMode = e && std::string(e) == "match"; blurMode = e && (std::string(e) == "blur" || std::string(e) == "blur2"); blur2 = e && std::string(e) == "blur2"; costMode = e && std::string(e) == "cost"; if (costMode) matchMode = true; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (app.win && glfwGetWindowAttrib(app.win, GLFW_ICONIFIED)) glfwRestoreWindow(app.win);
  if (!app.uiShot.empty()) return true;
  static float boot = 0; if (!started && (boot += dt) < 5.0f) return true;   // let start-up loading settle before opening the map (a load-time race in assets.cpp crashed early opens)
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
      for (int i = 0; i < 19; ++i) { int side = i < 9 ? 0 : 1; AiType t = types[i % 9]; sc.rules.aiTypes[side] |= aiBit(t); sc.rules.aiCount[side][(int)t] += 1; }
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
