// NA1 --capture-na1: the four new AI types in one match (Team Deathmatch on village_assault_3, the player alone on side 0, one
// each of Allied soldiers, Gas-mask soldiers 2, SS soldiers and Mechs on side 1, the AI frozen). For each gun type: its body
// and its clip set present, every gun of its list held aimed (a body per gun: combat.json gunbody_models), the built-in gun's
// muzzle ahead; one shot and one reload frame.
// The mech: its body and clips, idle, then the AI on to a move and a blow. selftest_out/na_<type>_<what>.png, report_na1.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "physics.h"
#include "scenario.h"
#include "character.h"
#include "enemy_types.h"
#include <GLFW/glfw3.h>

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0; bool started = false; float total = 0, st = 0, freeT = 0; vec3 camAt(0), camLook(0);
struct Gun { std::string model; int weapon; };
struct Kind { const char* name; int slot = -1; std::vector<Gun> guns; };
std::vector<Kind> kinds; size_t ki = 0, gi = 0; int mechSlot = -1;
fs::path shot(const std::string& k, const std::string& what) { return exeDir() / "selftest_out" / ("na_" + k + "_" + what + ".png"); }
}   // namespace

bool na1Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-na1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (app.win && glfwGetWindowAttrib(app.win, GLFW_ICONIFIED)) glfwRestoreWindow(app.win);   // our own window, minimised from outside: its frames are the captures
  if (!app.uiShot.empty()) return true;   // a shot still pending (a zero-size frame): hold the script until it is written
  if (!started) {
    started = true; T = new MpTest("na1");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::TDM; sc.rules.humanTeam = 0; sc.rules.killCap = 999;
    sc.rules.teamFaction[0] = Faction::Allies; sc.rules.teamFaction[1] = Faction::Axis;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = t ? 4 : 1; }
    sc.rules.aiTypes[0] = aiBit(AiType::Soldiers);
    sc.rules.aiTypes[1] = aiBit(AiType::Allied) | aiBit(AiType::SS) | aiBit(AiType::Mechs);   // RM2: no gas-mask 2
    for (AiType t : {AiType::Allied, AiType::SS, AiType::Mechs}) sc.rules.aiCount[1][(int)t] = 1;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok, "Team Deathmatch starts, one each of the four new types on side 1");
    return true;
  }
  total += dt; st += dt; ++frames;
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  Game* gp = gGame && gGame->active ? gGame : nullptr;
  if (!gp || total > 400) { T->check(false, "timed out at stage " + std::to_string(stage)); finish(); return true; }
  Game& G = *gp; Soldier* L = G.local();
  if (!L || !L->alive) return true;
  L->hp = 100;
  AiCore* A = aiCore();
  if (st < 3.0f) { if (A) A->frozen = true; return true; }
  char b[400];
  auto loadModel = [&](const std::string& id) -> int {
    if (G.modelIdx.count(id)) return G.modelIdx[id];
    auto cm = std::make_unique<CharModel>(); std::string err;
    if (!cm->load(contentDir() / "characters" / (id + ".glb"), err)) { T->check(false, "model " + id + ": " + err); return -1; }
    G.modelIdx[id] = (int)G.models.size(); G.models.push_back(std::move(cm)); return G.modelIdx[id];
  };
  if (stage == 0) {   // find the bots, their gun lists
    for (auto& s : G.sold) if (s.inUse && s.alive && !s.localPlayer && s.team != L->team) {
      const std::string& id = G.models[s.model]->id;
      if (s.zombie) mechSlot = s.slot;
      else if (id.rfind("allied_", 0) == 0) kinds.push_back({"allied", s.slot});
      else if (id.rfind("gasmask2_", 0) == 0) kinds.push_back({"gasmask2", s.slot});
      else if (id.rfind("ss_", 0) == 0) kinds.push_back({"ss", s.slot});
      else T->note("other bot: model " + id);
    }
    for (auto& k : kinds) {
      json list = jkey(G.combat, "gunbody_models").value(k.name, json::array());
      std::set<std::string> seen;
      for (auto& e : list) { std::string m = e[0], w = e[1]; if (seen.insert(m).second) k.guns.push_back({m, app.weaponIndex(w)}); }
      const Soldier& s = G.sold[k.slot]; const CharModel& m = *G.models[s.model];
      std::snprintf(b, sizeof b, "%s: body %s, %zu clips (walk forward %d, idle aiming %d, reload %d), %zu guns to hold", k.name, m.id.c_str(), m.clips.size(),
                    findClip(m, "walk forward") >= 0 || findClip(m, "pistol walk") >= 0, findClip(m, "idle aiming") >= 0 || findClip(m, "pistol idle") >= 0, findClip(m, "reload") >= 0, k.guns.size());
      T->check(m.clips.size() >= 40, b);
    }
    T->check(kinds.size() == 3 && mechSlot >= 0, "all four types spawned (" + std::to_string(kinds.size()) + " gun types, mech " + std::to_string(mechSlot) + ")");
    if (A) A->frozen = true; stage = 1; frames = 0; return true;
  }
  auto look = [&](vec3 at, vec3 c) {
    Physics::setCharacterPos(at + vec3(0, 0.9f, 0));
    vec3 eye, ef; G.renderView(eye, ef);
    vec3 d = c - (glm::length(eye - (at + vec3(0, 1.6f, 0))) < 1.0f ? eye : at + vec3(0, 1.6f, 0)); vec3 rv = glm::normalize(vec3(-d.z, 0, d.x)); d += rv * (0.28f * glm::length(d));
    app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
  };
  auto front = [&](Soldier& s, float dist) {
    vec3 f = vec3(s.world[2]); f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1); vec3 r(-f.z, 0, f.x);
    look(s.feet + (f + r) * (dist * 0.7071f) - vec3(0, 0.35f, 0), s.feet + vec3(0, 1.15f, 0));
  };
  auto aimAt = [&](Soldier& s) { s.br.aimYaw = s.anGunYaw = s.yaw; s.br.aimPitch = s.anGunPitch = 0; s.br.target = L->slot; s.hp = std::max(s.hp, 100.0f); };
  if (stage == 1) {   // every gun of every gun type
    if (ki >= kinds.size()) { stage = 2; frames = 0; return true; }
    Kind& k = kinds[ki]; Soldier& s = G.sold[k.slot];
    if (gi >= k.guns.size() + 2) { ++ki; gi = 0; frames = 0; return true; }
    const bool extra = gi >= k.guns.size();   // the shot frame, then the reload frame (on the first gun)
    const Gun& g = k.guns[extra ? 0 : gi];
    if (frames == 1) {
      int mi = loadModel(g.model);
      if (mi >= 0 && g.weapon >= 0) { s.model = mi; s.anim = AnimPlayer{}; s.anim.life = s.lifeId; s.weapon = g.weapon; s.gmWeapon = g.weapon; s.ws = WeaponState{}; s.ws.loaded = app.weaponDefs[g.weapon]->loaded; s.reload = 0; s.ciReady = false; }
      else { T->check(false, std::string(k.name) + ": " + g.model + " / weapon missing"); }
    }
    aimAt(s); front(s, 2.1f);
    if (extra && gi == k.guns.size() + 1) { s.reloadTotal = 3.0f; s.reload = 3.0f * (1.0f - std::min(0.45f, frames / 40.0f * 0.45f)); }
    if (frames == 28 && extra && gi == k.guns.size()) { int n0 = s.shots; s.ws.cooldown = 0; s.ws.bolt = 0; vec3 f = vec3(s.world[2]); f.y = 0; G.soldierFire(s, glm::normalize(f)); T->check(s.shots > n0, std::string(k.name) + ": a shot fired from its muzzle"); }
    if (frames == 30) {
      const CharModel& m = *G.models[s.model]; const WeaponDef& d = *app.weaponDefs[s.weapon];
      vec3 mz = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1)), f = vec3(s.world[2]); f.y = 0; f = glm::normalize(f);
      float ahead = glm::dot(mz - s.feet, f), up = mz.y - s.feet.y;
      std::string what = extra ? (gi == k.guns.size() ? "shot" : "reload") : d.id;
      std::snprintf(b, sizeof b, "%s %-14s %-22s clip '%s', muzzle %.2f m ahead %.2f m up%s", k.name, g.model.c_str(), d.id.c_str(), s.anim.a >= 0 ? m.clips[s.anim.a].name.c_str() : "-", ahead, up,
                    extra ? (gi == k.guns.size() ? " (the shot)" : " (reload 45 %)") : "");
      T->check(extra || (ahead > 0.15f && up > 0.6f && up < 1.9f), b);
      app.uiShot = shot(k.name, what);
    }
    if (frames > 32) { s.reload = 0; ++gi; frames = 0; }
    return true;
  }
  if (stage == 2) {   // the mech: idle (frozen), then the AI on: a move and a blow
    if (mechSlot < 0) { stage = 9; return true; }
    Soldier& s = G.sold[mechSlot]; const CharModel& m = *G.models[s.model];
    if (frames == 1) { std::snprintf(b, sizeof b, "mech: type %s, body %s, %zu clips, height %.2f m", enemyType(s.etype).id.c_str(), m.id.c_str(), m.clips.size(), s.pose.joint[CJ_HEAD].y); T->check(enemyType(s.etype).id == "mech" && m.id == "mech", b); }
    front(s, 4.5f);
    if (frames == 30) app.uiShot = shot("mech", "idle");
    if (frames > 32) { if (A) A->frozen = false; stage = 3; frames = 0; freeT = 0; vec3 f = vec3(s.world[2]); f.y = 0; f = glm::normalize(f); camAt = s.feet + f * 9.0f; camLook = s.feet + vec3(0, 1.2f, 0); }
    return true;
  }
  if (stage == 3) {
    Soldier& s = G.sold[mechSlot]; const CharModel& m = *G.models[s.model];
    freeT += dt; look(camAt, s.alive ? s.feet + vec3(0, 1.2f, 0) : camLook);   // the camera (the player) stays: the mech comes to it
    static bool moved = false;
    if (!moved && s.speed > 0.8f && freeT > 0.6f) { moved = true; std::snprintf(b, sizeof b, "mech moving at %.1f m/s, clip '%s'", s.speed, s.anim.a >= 0 ? m.clips[s.anim.a].name.c_str() : "-"); T->check(true, b); app.uiShot = shot("mech", "moving"); }
    std::string cn = s.anim.a >= 0 ? m.clips[s.anim.a].name : "";
    if (cn.find("Melee") != std::string::npos && s.anim.time > 0.4f) { T->check(true, "mech attacking: clip '" + cn + "'"); app.uiShot = shot("mech", "attack"); stage = 9; frames = 0; return true; }
    if (freeT > 30) { T->check(false, std::string("mech: no blow in 30 s (moved ") + (moved ? "yes" : "no") + ")"); stage = 9; frames = 0; }
    return true;
  }
  if (stage == 9 && frames > 6) finish();
  return true;
}
