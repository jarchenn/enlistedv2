// GM1 / GM2 --capture-gm1 (NMK_GM_KIND=gasmask|desert|german, default gasmask): a body with its gun built in, in a match. Team Deathmatch on
// village_assault_3, the player (Allies) alone on side 0, one bot on side 1 (Axis): (the gas-mask soldiers were removed, RM2), a
// desert soldier (AiType::Desert) or a plain Axis soldier (the German body); variant 0 of its list. Checks: its body is a
// <kind>_* model with clips, it holds its body's weapon, the separate gun is not drawn, its head joint at a man's height, its
// muzzle (pose.weapon x the weapon's muzzle: the model's 'flash' joint) ahead of it. Captures (the camera off its front
// quarter), selftest_out/gm_<kind>_idle.png (AI frozen), _moving.png (AI on, the camera following, 0.5 s into a move),
// _runshoot.png (AI on, running over 2.5 m/s, Game::soldierFire along its heading every 0.1 s: the RunShoot clip),
// _firing.png (frozen, aimed at the camera, the real Game::soldierFire), _free.png (AI on, the camera fixed 15 m away: the
// frame of a shot the bot fired on its own; a check, failed if none in 40 s). The player's health is reset each frame (not
// protected: a protected player is not shot at). selftest_out/report_gm_<kind>.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "physics.h"
#include "scenario.h"

namespace {
MpTest* T = nullptr;
std::string kind;
int mode = -1, stage = 0, frames = 0, bot = -1, shots0 = 0, rsShots = 0; bool rsCheck = false; bool started = false; float total = 0, st = 0, movingFor = 0, freeT = 0;
vec3 camAt(0), camLook(0);
fs::path shot(const char* what) { return exeDir() / "selftest_out" / ("gm_" + kind + "_" + what + ".png"); }
}   // namespace

bool gm1Capture(App& app, float dt) {
  if (mode < 0) {
    mode = 0;
    for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-gm1") mode = 1;
    const char* e = std::getenv("NMK_GM_KIND"); kind = e && *e ? e : "desert";   // RM2: the gas-mask soldiers are gone
  }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("gm_" + kind);
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.humanTeam = 0; sc.rules.killCap = 999;
    sc.rules.teamFaction[0] = Faction::Allies; sc.rules.teamFaction[1] = Faction::Axis;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 1; }
    const AiType ty = kind == "german" ? AiType::Soldiers : AiType::Desert;
    sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(ty); sc.rules.aiCount[1][(int)ty] = 1;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts, one " + std::string(aiTypeName(ty)) + " bot on side 1 (Axis)");
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
  Soldier& s = G.sold[bot];
  auto look = [&](vec3 at, vec3 c) {
    Physics::setCharacterPos(at + vec3(0, 0.9f, 0));
    vec3 d = c - (at + vec3(0, 1.6f, 0));
    app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  };
  auto place = [&](float dist) {   // the camera off the bot's front quarter, looking at its chest
    vec3 f = vec3(s.world[2]); f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1);
    vec3 r(-f.z, 0, f.x);
    look(s.feet + (f + r) * (dist * 0.7071f), s.feet + vec3(0, 1.1f, 0));
  };
  char b[300];
  if (stage == 0) {
    const CharModel& m = *G.models[s.model];
    T->check(m.id.rfind(kind + "_", 0) == 0 && m.clips.size() >= 15 && s.gunBody, "its body: " + m.id + ", " + std::to_string(m.clips.size()) + " clips, gun built in " + std::to_string((int)s.gunBody));
    T->check(s.weapon >= 0 && s.weapon == s.gmWeapon, "it holds its body's weapon: " + (s.weapon >= 0 ? app.weaponDefs[s.weapon]->id : std::string("none")));
    std::snprintf(b, sizeof b, "hit zones from its joints: head %.2f m, hips %.2f m over the feet", s.pose.joint[CJ_HEAD].y, s.pose.joint[CJ_HIPS].y);
    T->check(s.pose.joint[CJ_HEAD].y > 1.2f && s.pose.joint[CJ_HEAD].y < 1.9f && s.pose.joint[CJ_HIPS].y > 0.6f && s.pose.joint[CJ_HIPS].y < 1.1f, b);
    if (A) A->frozen = true;
    place(5); stage = 1; frames = 0; return true;
  }
  if (stage == 1) {   // idle, frozen
    place(5);
    if (frames == 40) {
      const WeaponDef& d = *app.weaponDefs[s.weapon];
      vec3 mz = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1)), f = vec3(s.world[2]);
      float ahead = glm::dot(mz - s.feet, glm::normalize(vec3(f.x, 0, f.z))), up = mz.y - s.feet.y;
      std::snprintf(b, sizeof b, "the muzzle at the model's flash joint: %.2f m ahead, %.2f m up (clip %d)", ahead, up, s.anim.a);
      T->check(ahead > 0.1f && ahead < 1.6f && up > 0.5f && up < 1.8f, b);
      bool drawn = false; for (auto& md : app.R.worldModels) if (d.useModel() && md.model == d.worldModel()) drawn = true;
      T->check(!drawn, "no separate gun model drawn");
      app.uiShot = shot("idle");
    }
    if (frames > 44) { if (A) A->frozen = false; stage = 2; frames = 0; }
    return true;
  }
  if (stage == 2) {   // moving, AI on, the camera following
    place(5);
    movingFor = s.speed > 0.8f ? movingFor + dt : 0.0f;
    if (movingFor > 0.5f) { std::snprintf(b, sizeof b, "moving at %.1f m/s (clip %d, key %d)", s.speed, s.anim.a, s.anim.key); T->check(true, b); app.uiShot = shot("moving"); stage = 6; frames = 0; }
    else if (frames > 60 * 30) { T->check(false, "no move in 30 s"); stage = 3; frames = 0; }
    return true;
  }
  if (stage == 6) {   // firing on the move: AI on, running, the real Game::soldierFire along its heading (the pose: RunShoot)
    place(5);
    if (rsCheck) {   // the frame after the capture: its pose state (the hook runs before the frame's pose)
      std::snprintf(b, sizeof b, "firing on the move at %.1f m/s: the run-and-shoot state (key %d, clip %d '%s')", s.speed, s.anim.key, s.anim.b,
                    s.anim.b >= 0 ? G.models[s.model]->clips[s.anim.b].name.c_str() : "-");
      T->check(s.anim.key == 53 && s.anim.b >= 0 && G.models[s.model]->clips[s.anim.b].name == "RunShoot", b);
      stage = 3; frames = 0; return true;
    }
    if (s.speed > 2.5f && frames % 6 == 0) {
      vec3 f = vec3(s.world[2]); f.y = 0; f = glm::normalize(f) + vec3(0, -0.05f, 0);
      int n0 = s.shots; s.ws.cooldown = 0; s.ws.bolt = 0; if (s.ws.loaded <= 0) s.ws.loaded = 5; G.soldierFire(s, glm::normalize(f));
      if (s.shots > n0 && ++rsShots >= 4) { app.uiShot = shot("runshoot"); rsCheck = true; }
    } else if (frames > 60 * 25) { T->check(false, "no run over 2.5 m/s in 25 s for firing on the move"); stage = 3; frames = 0; }
    return true;
  }
  if (stage == 3) {   // firing on demand: frozen, aimed at the camera
    if (A) A->frozen = true;
    place(6);
    vec3 d = G.chest(*L) - G.eye(s); s.br.aimYaw = degrees(std::atan2(-d.x, -d.z)); s.br.aimPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); s.br.target = L->slot;
    if (frames > 40 && frames % 9 == 0) {
      int n0 = s.shots; G.soldierFire(s, glm::normalize(G.chest(*L) + vec3(0.6f, 0.4f, 0) - G.eye(s)));
      if (s.shots > n0 && frames > 60) {
        std::snprintf(b, sizeof b, "firing: shot %d from the model's muzzle (clip %d, key %d)", s.shots, s.anim.a, s.anim.key); T->check(true, b);
        app.uiShot = shot("firing"); stage = 4; frames = 0;
        vec3 f = vec3(s.world[2]); f.y = 0; f = glm::normalize(f);
        camAt = s.feet + f * 15.0f; camLook = s.feet + vec3(0, 1.1f, 0); shots0 = s.shots; s.br.target = -1;
      }
    }
    return true;
  }
  if (stage == 4) {   // free: AI on, the camera fixed 15 m in front, waiting for the bot's own shot
    if (A) A->frozen = false;
    look(camAt, s.alive ? s.feet + vec3(0, 1.1f, 0) : camLook);
    freeT += dt;
    if (s.shots > shots0) {
      std::snprintf(b, sizeof b, "a shot of its own after %.1f s ('%s', clip %d, key %d, %d shots)", freeT, s.br.why.c_str(), s.anim.a, s.anim.key, s.shots - shots0);
      T->check(true, b); app.uiShot = shot("free"); stage = 5; frames = 0;
    } else if (freeT > 40) {
      std::snprintf(b, sizeof b, "no shot of its own in 40 s (target %d, why '%s', speed %.1f)", s.br.target, s.br.why.c_str(), s.speed);
      T->check(false, b); stage = 5; frames = 0;
    }
    return true;
  }
  if (stage == 5 && frames > 6) finish();
  return true;
}
