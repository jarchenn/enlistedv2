// --capture-reticles (AT2): every optic in ADS (the guns' own red dots, the fitted red dot / holographic / ACOG / 4x / 10x /
// thermal, the scoped rifles, a blaster) with a soldier 100 m ahead: selftest_out/ret_<tag>.png (with the HUD), no sway.
// Then the zero check: one real shot per case, its trajectory (start, direction, gravity) at 25 / 50 / 100 m projected
// with the world camera and compared with the drawn mark (the red dot / holographic mark: sight_overlay.cpp; the others:
// the screen centre). [ret] lines in the log.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "loot.h"
#include "sight_overlay.h"
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr; int mode = -1; bool started = false;
int bot = -1; vec3 botAt{0}; float aimYaw = 0, aimPitch = 0;
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }

void holdBot() {
  if (bot < 0 || !gGame) return;
  Soldier& b = gGame->sold[bot];
  if (b.agent >= 0) Physics::setAgentPos(b.agent, botAt);
  b.feet = botAt; b.vel = vec3(0); b.hp = 1000; b.protectUntil = -1; b.weapon = -1;
  if (Soldier* L = gGame->local()) b.yaw = aimYaw + 180;
}

struct Case { const char* tag; const char* gun; int att; };
const Case kCases[] = {
  {"m4_own", "modern_m4", 0}, {"ak12_own", "modern_ak12", 0}, {"glauncher_own", "modern_glauncher", 0}, {"mp7_own", "modern_mp7", 0},
  {"shotgun_own", "modern_shotgun", 0}, {"smg9_own", "modern_smg9", 0},
  {"ak_reddot", "modern_ak", 1}, {"ak_holo", "modern_ak", 5}, {"ak_acog", "modern_ak", 2}, {"ak_4x", "modern_ak", 3}, {"ak_10x", "modern_ak", 4},
  {"ak_thermal", "modern_ak", 6},
  {"sniper_bolt_mil", "modern_sniper_bolt", 0}, {"sniper_50_bmg", "modern_sniper_50", 0}, {"sniper_semi_acog", "modern_sniper_semi", 0},
  {"ar15_plus", "modern_ar15", 0}, {"kar98k_zf39", "kar98k_zf39", 0}, {"m1c", "m1c_garand", 0}, {"blaster_rifle", "blaster_rifle", 0},
};

void build(App&) {
  push("start", [](App& a, float t) {
    if (t == 0) {
      if (a.mode == Mode::Explore) a.exitExplore();
      a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
      json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
      Scenario sc = Scenario::fromJson(scj);
      sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.teamPlayers[0] = 2; sc.rules.teamPlayers[1] = 1; sc.rules.respawnDelay = 2;
      sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
      T->check(a.enterExplore() && gGame->active, "TDM started (" + gGame->setupError + ")");
      a.useScripted = true; a.scriptedInput = ExploreInput{};
      return false;
    }
    Game& G = *gGame; Soldier* L = G.local();
    for (auto& s : G.sold) if (!s.alive) return t > 30 ? (T->check(false, "everyone spawns"), true) : false;
    if (!L) return false;
    for (auto& s : G.sold) if (!s.localPlayer && s.faction == L->faction) bot = s.slot;
    for (auto& s : G.sold) if (s.faction != L->faction) { s.hp = 1000; if (s.agent >= 0) Physics::setAgentPos(s.agent, L->feet + vec3(300, 0, 300)); s.feet = L->feet + vec3(300, 0, 300); }
    L->hp = 5000; L->protectUntil = -1;   // no spawn-protection ring over the sight picture
    // a clear 100 m line at eye height: the first yaw (15 deg steps) with nothing in the way and ground under the far end
    vec3 eye = a.eyePos(); bool found = false;
    for (int k = 0; k < 24 && !found; ++k) {
      float yaw = a.eyeYaw + 15.0f * k; vec3 f = App::forward(yaw, 0); Physics::Hit h;
      if (Physics::castRayEx(eye, f, 104.0f, h)) continue;
      vec3 far = eye + f * 100.0f; Physics::Hit g;
      if (!Physics::castRayEx(far + vec3(0, 30, 0), vec3(0, -1, 0), 60.0f, g)) continue;
      if (g.pos.y > eye.y - 0.8f || g.pos.y < eye.y - 4.0f) continue;   // standing ground about level with ours
      aimYaw = yaw; botAt = g.pos; found = true;
    }
    if (!found) { aimYaw = a.eyeYaw; botAt = eye + App::forward(aimYaw, 0) * 100.0f - vec3(0, 1.6f, 0); }
    aimPitch = degrees(std::atan2(botAt.y + 1.3f - eye.y, glm::length(vec2(botAt.x - eye.x, botAt.z - eye.z))));   // the chest
    logf("[ret] target 100 m: clear line %s, yaw %.1f pitch %.2f, frame %dx%d", found ? "yes" : "NO", aimYaw, aimPitch, a.R.width, a.R.height);
    return true; });
  // the guns' own sights first (Battle Royale turns a sniper's own scope into its ACOG), then the fitted optics
  for (int pass = 0; pass < 2; ++pass) {
  if (pass == 1) push("battle royale", [](App&, float) { gGame->match.rules.mode = GameMode::BattleRoyale; lootBegin(*gGame); return true; });   // BR2
  for (const Case& c : kCases) {
    if ((c.att != 0) != (pass == 1)) continue;
    Case cc = c;
    push(std::string("aim ") + c.tag, [cc](App& a, float t) {
      holdBot();
      if (t == 0) {
        int w = a.weaponIndex(cc.gun); std::string msg;
        T->check(w >= 0, std::string(cc.gun) + " exists");
        if (w < 0) return true;
        if (a.activeWeapon != w) a.equipWeapon(w, msg);
        WeaponState& s = a.wstate[a.weaponDefs[w]->id]; s.att[0] = cc.att; s.loaded = std::max(s.loaded, 5);
        a.vm.procedural = false;   // no sway, bob or breathing
        a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = true;
      }
      a.eyeYaw = aimYaw; a.eyePitch = aimPitch; a.viewKickYaw = a.viewKickPitch = 0;
      if (t < 1.8f) return false;
      a.uiShot = outDir() / (std::string("ret_") + cc.tag + ".png");
      return true; });
    push(std::string("zero ") + c.tag, [cc](App& a, float t) {
      holdBot();
      a.eyeYaw = aimYaw; a.eyePitch = aimPitch; a.viewKickYaw = a.viewKickPitch = 0;
      static vec2 mark; static float dotPx; static mat4 VP; static vec3 eye, fwd; static int frames;
      if (t == 0) { frames = 0; return false; }
      if (t < 0.3f) return false;   // the UI shot is written
      const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
      if (frames == 0) {   // the mark and the camera this frame, then pull the trigger
        mat4 view; float wf; a.fpCamera(view, wf);
        mat4 P = glm::perspective(radians(wf), (float)a.R.width / std::max(1, a.R.height), 0.03f, 4000.0f); VP = P * view;
        eye = a.eyePos(); fwd = App::forward(a.eyeYaw, a.eyePitch);
        float pxMoa = P[1][1] * a.R.height * 0.5f * 2.908882e-4f;
        mark = vec2(a.R.width * 0.5f, a.R.height * 0.5f); dotPx = 1.0f;
#ifdef AT2_HUD_RETICLE
        ReflexReticle r = reflexReticle(a);
        if (r.ok) { mark = r.pos; dotPx = std::max(0.75f, (r.kind == 1 ? 1.0f : 0.5f) * r.pxMoa); }
#endif
        Physics::Hit eh; float hitD = Physics::castRayEx(eye, fwd, 600.0f, eh) ? eh.t : -1.0f;
        logf("[ret] %s: frame %dx%d, world FOV %.2f, %.3f px per MOA, the eye ray meets the world at %.1f m", cc.tag, a.R.width, a.R.height, wf, pxMoa, hitD);
        for (auto& x : a.handling.ads) x = 0;   // no dispersion: the sight, not the cone, is measured
        for (auto& x : a.handling.hip) x = 0;
        a.lastShotDir = vec3(0); a.scriptedInput.fire = true; ++frames; return false;
      }
      a.scriptedInput.fire = false;
      if (glm::length(a.lastShotDir) < 0.5f) return t > 2.0f ? (T->check(false, std::string(cc.tag) + ": a shot was fired"), true) : false;
      float v = d.velocity, g = d.action == "launcher" ? 9.81f * d.rocketGrav : (d.blaster ? 0.0f : 9.81f);
      std::string line; float worst = 0, atZero = 0;
      for (float D : {25.0f, 50.0f, 100.0f, d.zero}) {
        float tt = std::max(0.0f, glm::dot(eye + fwd * D - a.lastShotStart, fwd)) / std::max(1.0f, v * glm::dot(a.lastShotDir, fwd));
        vec3 p = a.lastShotStart + a.lastShotDir * v * tt - vec3(0, 0.5f * g * tt * tt, 0);
        vec4 q = VP * vec4(p, 1); vec2 px((q.x / q.w * 0.5f + 0.5f) * a.R.width, (0.5f - q.y / q.w * 0.5f) * a.R.height);
        float e = glm::length(px - mark);
        if (D == d.zero) atZero = e; else worst = std::max(worst, e);
        char b[96]; std::snprintf(b, sizeof b, "  %3.0f m%s: %+.2f, %+.2f px", D, D == d.zero ? " (zero)" : "", px.x - mark.x, px.y - mark.y); line += b;
      }
      logf("[ret] %s zero %.0f m: impact against the mark%s (mark radius %.2f px)", cc.tag, d.zero, line.c_str(), dotPx);
#ifdef AT2_HUD_RETICLE
      T->check(atZero <= 0.25f, std::string(cc.tag) + ": the mark sits on the impact at the zero (" + std::to_string(atZero) + " px; 25 / 50 / 100 m worst " + std::to_string(worst) + " px, dot radius " + std::to_string(dotPx) + " px)");
#endif
      a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = true;
      return true; });
  }
  }
  push("done", [](App& a, float t) { if (t < 0.5f) return false; a.vm.procedural = true; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}

bool reticlesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-reticles") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) { started = true; T = new MpTest("reticles"); build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 400) { T->check(false, "timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
