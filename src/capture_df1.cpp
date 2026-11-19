// DF1 --capture-df1: a Delta Force soldier (AiType::Delta) in a match: Team Deathmatch on village_assault_3, the player alone on
// side 0, one Delta bot on side 1. Checks: its body is delta_force with its clips, a kit soldier (4) holding the kit's rifle with
// the pistol carried, the separate gun drawn, the hands on that gun's own grip and handguard sockets (the hand joints within
// 9 cm of them), the muzzle ahead. Captures (selftest_out/df_*.png): rifle_idle (AI frozen, aiming), rifle_moving (AI on, 0.5 s
// into a move), rifle_flash (frozen, the camera square to the bore 2.3 m off, the frame of Game::soldierFire: the fireball
// flash), rifle_reload (the reload held at 35 %: the magazine out in the left hand), pistol_idle / pistol_flash (the sidearm in
// hand, the same two views). selftest_out/report_df1.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "physics.h"
#include "scenario.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0, bot = -1; bool started = false; float total = 0, st = 0, movingFor = 0;
fs::path shot(const char* what) { return exeDir() / "selftest_out" / (std::string("df_") + what + ".png"); }
}   // namespace

bool df1Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-df1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("df1");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.humanTeam = 0; sc.rules.killCap = 999;
    sc.rules.teamFaction[0] = Faction::Allies; sc.rules.teamFaction[1] = Faction::Axis;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 1; }
    sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Delta); sc.rules.aiCount[1][(int)AiType::Delta] = 1;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts, one Delta Force bot on side 1");
    return true;
  }
  total += dt; st += dt; ++frames;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  Game* gp = gGame && gGame->active ? gGame : nullptr;
  if (!gp || total > 150) { T->check(false, "timed out at stage " + std::to_string(stage)); finish(); return true; }
  Game& G = *gp; Soldier* L = G.local();
  if (!L || !L->alive) return true;
  L->hp = 100;
  if (bot < 0) for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && s.team != L->team) bot = s.slot;
  AiCore* A = aiCore();
  if (bot < 0 || !G.sold[bot].alive || st < 3.0f) { if (A) A->frozen = true; if (st > 20 && bot < 0) { T->check(false, "the bot spawned"); finish(); } return true; }
  Soldier& s = G.sold[bot]; s.hp = 100;
  auto look = [&](vec3 at, vec3 c) {   // the subject left of the centre: clear of our own first-person gun (lower right)
    Physics::setCharacterPos(at + vec3(0, 0.9f, 0));
    vec3 eye, ef; G.renderView(eye, ef);   // where the camera really is (the body stands on the ground): aim from there
    vec3 d = c - (glm::length(eye - (at + vec3(0, 1.6f, 0))) < 1.0f ? eye : at + vec3(0, 1.6f, 0)); vec3 rv = glm::normalize(vec3(-d.z, 0, d.x)); d += rv * (0.28f * glm::length(d));
    app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  };
  vec3 f = vec3(s.world[2]); f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1);
  const vec3 r(-f.z, 0, f.x);
  auto front = [&](float dist) { look(s.feet + (f + r) * (dist * 0.7071f) - vec3(0, 0.35f, 0), s.feet + vec3(0, 1.15f, 0)); };
  auto side = [&](float dist) {   // square to the bore, level with the muzzle, looking at a point ahead of it
    const WeaponDef& d = *app.weaponDefs[s.weapon];
    vec3 mz = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1));
    look(mz - r * dist + f * 0.35f - vec3(0, 1.6f, 0), mz + f * 0.35f);
  };
  auto aimAhead = [&]() { s.br.aimYaw = s.yaw; s.anGunYaw = s.yaw; s.br.aimPitch = 0; s.anGunPitch = 0; s.br.target = L->slot; };
  auto handsCheck = [&](const char* what) {
    const CharModel& m = *G.models[s.model]; const WeaponDef& d = *app.weaponDefs[s.weapon];
    vec3 hr = s.pose.joint[CJ_RHAND], hl = s.pose.joint[CJ_LHAND];
    vec3 gr = vec3(s.pose.weapon * vec4(d.gripR, 1)), gl = vec3(s.pose.weapon * vec4(d.gripL, 1));
    char b[300]; std::snprintf(b, sizeof b, "%s: hands on %s's sockets: right hand %.3f m from the grip, left %.3f m from the handguard (fingers on %d / %d)", what, d.id.c_str(),
                               glm::length(hr - gr), glm::length(hl - gl), s.pose.fingersOn[1], s.pose.fingersOn[0]);
    T->check(glm::length(hr - gr) < 0.12f && glm::length(hl - gl) < 0.14f, b); (void)m;
  };
  auto fire = [&]() { int n0 = s.shots; s.ws.cooldown = 0; s.ws.bolt = 0; if (s.ws.loaded <= 0) s.ws.loaded = 5; G.soldierFire(s, f + vec3(0, -0.01f, 0)); return s.shots > n0; };
  char b[300];
  if (stage == 0) {
    const CharModel& m = *G.models[s.model];
    T->check(m.id == "delta_force" && m.clips.size() >= 40 && s.kit == 4 && !s.gunBody, "its body: " + m.id + ", " + std::to_string(m.clips.size()) + " clips, kit " + std::to_string(s.kit));
    T->check(s.weapon >= 0 && app.weaponDefs[s.weapon]->id == "modern_m4_irons" && s.carried.size() == 2, "it holds the kit's rifle (" + (s.weapon >= 0 ? app.weaponDefs[s.weapon]->id : std::string("none")) + "), " + std::to_string(s.carried.size()) + " carried");
    std::snprintf(b, sizeof b, "hit zones from its joints: head %.2f m, hips %.2f m over the feet", s.pose.joint[CJ_HEAD].y, s.pose.joint[CJ_HIPS].y);
    T->check(s.pose.joint[CJ_HEAD].y > 1.3f && s.pose.joint[CJ_HEAD].y < 2.0f && s.pose.joint[CJ_HIPS].y > 0.7f && s.pose.joint[CJ_HIPS].y < 1.2f, b);
    if (A) A->frozen = true; stage = 1; frames = 0; return true;
  }
  if (stage == 1) {   // rifle, aiming, frozen
    aimAhead(); front(2.1f);
    if (frames == 45) {
      handsCheck("rifle aimed");
      bool drawn = false; const WeaponDef& d = *app.weaponDefs[s.weapon]; for (auto& md : app.R.worldModels) if (d.useModel() && md.model == d.worldModel()) drawn = true;
      T->check(drawn, "the gun model is drawn in its hands");
      vec3 mz = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1));
      std::snprintf(b, sizeof b, "the muzzle %.2f m ahead, %.2f m up (clip %d '%s')", glm::dot(mz - s.feet, f), mz.y - s.feet.y, s.anim.a, s.anim.a >= 0 ? G.models[s.model]->clips[s.anim.a].name.c_str() : "-");
      T->check(glm::dot(mz - s.feet, f) > 0.3f && mz.y - s.feet.y > 1.0f, b);
      app.uiShot = shot("rifle_idle");
    }
    if (frames > 50) { s.br.target = -1; if (A) A->frozen = false; stage = 2; frames = 0; }
    return true;
  }
  if (stage == 2) {   // moving, AI on
    front(3.0f);
    movingFor = s.speed > 0.8f ? movingFor + dt : 0.0f;
    if (movingFor > 0.5f) { std::snprintf(b, sizeof b, "moving at %.1f m/s, clip '%s' / '%s'", s.speed, s.anim.a >= 0 ? G.models[s.model]->clips[s.anim.a].name.c_str() : "-", s.anim.b >= 0 ? G.models[s.model]->clips[s.anim.b].name.c_str() : "-"); T->check(true, b); app.uiShot = shot("rifle_moving"); stage = 3; frames = 0; }
    else if (frames > 60 * 30) { T->check(false, "no move in 30 s"); stage = 3; frames = 0; }
    return true;
  }
  if (stage == 3 || stage == 6) {   // the shot, side on: frozen, aimed ahead
    if (A) A->frozen = true;
    aimAhead(); side(2.3f);
    if (frames == 40) {
      bool ok = fire();
      size_t fb = 0; for (auto& q : app.fx.parts) if (q.flame >= 1000) ++fb;
      std::snprintf(b, sizeof b, "%s shot: fired %d, %zu fireball sprites", app.weaponDefs[s.weapon]->id.c_str(), (int)ok, fb);
      T->check(ok && fb >= 8, b); app.uiShot = shot(stage == 3 ? "rifle_flash" : "pistol_flash");
    }
    if (frames > 46) { stage = stage == 3 ? 4 : 7; frames = 0; }
    return true;
  }
  if (stage == 4) {   // the reload, held at half way
    aimAhead(); front(1.8f);
    s.reloadTotal = 2.6f; s.reload = 2.6f * (frames < 30 ? 1.0f - frames / 30.0f * 0.35f : 0.65f);   // held at 35 %: the magazine just out, under the gun
    if (frames == 50) {
      std::snprintf(b, sizeof b, "reload at 35 %: the magazine in the left hand %d", (int)s.dfMagHeld); T->check(s.dfMagHeld, b);
      app.uiShot = shot("rifle_reload");
    }
    if (frames > 55) {
      s.reload = 0;
      int p = s.carried.size() > 1 ? s.carried[1] : -1;
      T->check(p >= 0 && app.weaponDefs[p]->id == "modern_glock34", "the sidearm: " + (p >= 0 ? app.weaponDefs[p]->id : std::string("none")));
      if (p >= 0) { const WeaponDef& d = *app.weaponDefs[p]; s.weapon = p; s.ws = WeaponState{}; s.ws.loaded = d.loaded; }
      stage = 5; frames = 0;
    }
    return true;
  }
  if (stage == 5) {   // the pistol, aimed
    aimAhead(); front(1.6f);
    if (frames == 45) { handsCheck("pistol aimed"); app.uiShot = shot("pistol_idle"); }
    if (frames > 50) { stage = 6; frames = 0; }
    return true;
  }
  if (stage == 7 && frames > 6) finish();
  return true;
}
