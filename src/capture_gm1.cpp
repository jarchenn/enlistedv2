// GM1 --capture-gm1: the gas-mask soldier in a match. Team Deathmatch on village_assault_3, the player alone on side 0, one
// gas-mask soldier (AiType::GasMask, variant 0: the AK-47 body) on side 1. Checks: its body is a gasmask_* model with clips,
// it holds its body's weapon, the separate gun is not drawn, its head joint at a man's height, its muzzle (pose.weapon x the
// weapon's muzzle: the model's 'flash' joint) ahead of it at shoulder height. Captures, the camera 5 m off its front quarter:
// selftest_out/gm1_idle.png (AI frozen), gm1_moving.png (the AI running: the first frame it moves), gm1_firing.png (the frame
// after a shot). The player is kept alive (its health reset each frame), not protected (a protected player is not shot at).
// selftest_out/report_gm1.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "physics.h"
#include "scenario.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0, bot = -1; bool started = false; float total = 0, st = 0;
bool gotMove = false, gotFire = false; float movingFor = 0, diagT = 0;
}   // namespace

bool gm1Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-gm1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("gm1");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.humanTeam = 0; sc.rules.killCap = 999;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 1; }
    sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::GasMask); sc.rules.aiCount[1][(int)AiType::GasMask] = 1;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts, one gas-mask soldier on side 1");
    return true;
  }
  total += dt; st += dt; ++frames;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  Game* gp = gGame && gGame->active ? gGame : nullptr;
  if (!gp || total > 90) { T->check(false, "timed out at stage " + std::to_string(stage) + (gotMove ? "" : " (no moving frame)") + (gotFire ? "" : " (no firing frame)")); finish(); return true; }
  Game& G = *gp; Soldier* L = G.local();
  if (!L || !L->alive) return true;
  L->hp = 100;
  if (bot < 0) for (auto& s : G.sold) if (s.inUse && s.alive && s.gasmask) bot = s.slot;
  AiCore* A = aiCore();
  if (bot < 0 || !G.sold[bot].alive || st < 3.0f) { if (A) A->frozen = true; if (st > 20 && bot < 0) { T->check(false, "a gas-mask soldier spawned"); finish(); } return true; }
  Soldier& s = G.sold[bot];
  auto place = [&](float side) {   // the camera 5 m off the bot's front quarter (side +-1), looking at its chest
    vec3 f = vec3(s.world[2]); f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1);
    vec3 r(-f.z, 0, f.x), at = s.feet + f * 3.5f + r * (3.5f * side), c = s.feet + vec3(0, 1.1f, 0);
    Physics::setCharacterPos(at + vec3(0, 0.9f, 0));
    vec3 d = c - (at + vec3(0, 1.6f, 0));
    app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  };
  char b[300];
  if (stage == 0) {
    const CharModel& m = *G.models[s.model];
    T->check(m.id.rfind("gasmask_", 0) == 0 && m.clips.size() >= 10, "its body: " + m.id + ", " + std::to_string(m.clips.size()) + " clips");
    T->check(s.weapon >= 0 && s.weapon == s.gmWeapon && s.carried.size() == 1, "it holds its body's weapon: " + (s.weapon >= 0 ? app.weaponDefs[s.weapon]->id : std::string("none")));
    std::snprintf(b, sizeof b, "hit zones from its joints: head %.2f m, hips %.2f m over the feet", s.pose.joint[CJ_HEAD].y, s.pose.joint[CJ_HIPS].y);
    T->check(s.pose.joint[CJ_HEAD].y > 1.3f && s.pose.joint[CJ_HEAD].y < 1.9f && s.pose.joint[CJ_HIPS].y > 0.6f && s.pose.joint[CJ_HIPS].y < 1.1f, b);
    if (A) A->frozen = true;
    place(1); stage = 1; frames = 0; return true;
  }
  if (stage == 1) {
    place(1);
    if (frames == 30) {
      const WeaponDef& d = *app.weaponDefs[s.weapon];
      vec3 mz = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1)), f = vec3(s.world[2]);
      float ahead = glm::dot(mz - s.feet, glm::normalize(vec3(f.x, 0, f.z))), up = mz.y - s.feet.y;
      std::snprintf(b, sizeof b, "the muzzle at the model's flash joint: %.2f m ahead, %.2f m up", ahead, up);
      T->check(ahead > 0.2f && ahead < 1.6f && up > 0.6f && up < 1.7f, b);
      bool drawn = false; for (auto& md : app.R.worldModels) if (d.useModel() && md.model == d.worldModel()) drawn = true;
      T->check(!drawn, "no separate gun model drawn");
      app.uiShot = exeDir() / "selftest_out" / "gm1_idle.png";
    }
    if (frames > 34) { if (A) A->frozen = false; stage = 2; frames = 0; }
    return true;
  }
  place(1);
  movingFor = s.speed > 0.8f ? movingFor + dt : 0.0f;
  if ((diagT += dt) > 5.0f) {   // why it does (not) shoot
    diagT = 0; const EnemyMemory* tm = nullptr; for (auto& x : s.br.mem) if (x.enemy == s.br.target) tm = &x;
    logf("gm1: t %.1f target %d (visible %d) why '%s' speed %.1f loaded %d reload %.1f shots %d anim %d", G.t, s.br.target, tm ? (int)tm->visible : -1, s.br.why.c_str(), s.speed, s.ws.loaded, s.reload, s.shots, s.anim.a);
  }
  if (!gotMove && movingFor > 0.5f && s.gmFireT > 0.3f) { gotMove = true; std::snprintf(b, sizeof b, "moving at %.1f m/s (clip %d)", s.speed, s.anim.a); T->check(true, b); app.uiShot = exeDir() / "selftest_out" / "gm1_moving.png"; return true; }
  if (!gotMove) return true;
  // firing: the brain's own shots depend on the fight (the follow camera kept it bounding to cover); frozen, aimed at the
  // camera, the real Game::soldierFire path (muzzle at the flash joint, flash, sound) a shot every 0.15 s
  if (A) A->frozen = true;
  if (stage == 2) { stage = 3; frames = 0; L->hp = 100; }
  { vec3 d = G.chest(*L) - G.eye(s); s.br.aimYaw = degrees(std::atan2(-d.x, -d.z)); s.br.aimPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); s.br.target = L->slot; }
  if (frames > 40 && frames % 9 == 0) { int n0 = s.shots; G.soldierFire(s, glm::normalize(G.chest(*L) + vec3(0.6f, 0.4f, 0) - G.eye(s))); if (s.shots > n0 && frames > 60 && !gotFire) { gotFire = true; std::snprintf(b, sizeof b, "firing: shot %d from the model's muzzle (clip %d at %.2f s)", s.shots, s.anim.a, s.anim.time); T->check(true, b); app.uiShot = exeDir() / "selftest_out" / "gm1_firing.png"; } }
  if (gotFire && frames > 80) finish();
  return true;
}
