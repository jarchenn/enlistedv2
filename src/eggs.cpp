// EGG1 spawn eggs (user: "spawn eggs in the creative menu ... spawn in the AI types (for testing) by right clicking the
// ground"). Creative > toolbar "Eggs": one egg per AI type the game has, read at runtime (the soldier-brain AiTypes that
// are not retired, then every enemy type of content/ai/zombie.json), so additions and removals follow. With an egg
// selected, right-click on the ground spawns one there facing the camera (held: one every 0.3 s); right-click with no
// egg selected orbits as before. Side A / B / hostile to all, AI on / frozen, Clear.
// The AI live in a sandbox Game (Game::sandbox): no objectives, no score, no human, no respawn, a navmesh over the build
// area, stepped here at 60 Hz while in Creative. Never saved into the map (soldiers never are), never on LAN (refused
// while a LAN session exists), torn down on Clear, entering Explore, a map change (App::clearRuntime) and shutdown.
// --selftest-eggs: one of each egg through the same call, "[eggs] spawned <type> at ...", 5 s of steps, then Clear.
#include "app.h"
#include "game.h"
#include "net.h"
#include "ai_core.h"
#include "enemy_types.h"
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
struct Egg { std::string name; AiType ait; int etype; bool soldier; bool vehicle = false; };   // MT1 vehicle: placed into the map (acts in Explore)
struct EggState { bool open = false; int sel = -1; int side = 0; bool frozen = false; float holdT = 0; bool rmbWas = false; float acc = 0; } E;

std::vector<Egg> eggs() {   // rebuilt each call: cheap, and always the current type lists
  std::vector<Egg> v;
  for (int t = 0; t < kAiTypeCount; ++t) {
    if (aiTypeRetired(t) || !Game::soldierBrain((AiType)t)) continue;
    v.push_back({aiTypeName((AiType)t), (AiType)t, -1, true});
  }
  const auto& et = enemyTypes();
  v.push_back({"Mech Tank (vehicle: placed in the map, acts in Explore)", AiType::MechTank, -1, false, true});   // MT1: not a sandbox soldier
  for (int k = 0; k < (int)et.size(); ++k) v.push_back({et[k].name.empty() ? et[k].id : et[k].name, AiType::Zombies, k, false});   // the plain melee carrier, its enemy type overridden (gameplay: ZombiesHeavies is the weighted mix)
  return v;
}
bool lanBusy() { return gNet && gNet->role != Net::NONE; }
bool sandboxOn() { return gGame && gGame->active && gGame->sandbox; }
int spawnedCount() { return sandboxOn() ? (int)gGame->sold.size() : 0; }

// the one spawn call (the UI and the selftest): starts the sandbox on first use
int spawnEgg(App& app, const Egg& e, vec3 feet, float yaw, int side) {
  if (lanBusy()) { app.toast("Spawn eggs are local only: leave the LAN session first", 4); return -1; }
  if (app.mode != Mode::Creative) return -1;
  if (e.vehicle) {   // MT1: a crewless Mech Tank instance for that side (hostile to all: side B); undo / delete like any placed vehicle
    const uint64_t id = app.tanks.spawnMech(app, feet, yaw, side == 0 ? 0 : 1);
    if (!id) app.toast("Mech Tank: no room here (or its vehicle definition is missing)", 4);
    return id ? 0 : -1;
  }
  if (gGame && gGame->active && !gGame->sandbox) { app.toast("A match is running: spawn eggs work in Creative", 4); return -1; }
  if (spawnedCount() >= ScenarioRules::kMaxAI) { app.toast("Combatant limit reached (" + std::to_string(ScenarioRules::kMaxAI) + " AI): clear some first", 4); return -1; }
  if (!sandboxOn()) {
    if (!gGame) gGame = new Game(app);
    std::string err;
    if (!gGame->sandboxBegin(err)) { app.toast("Spawn eggs: " + err, 6); return -1; }
  }
  int team = side == 0 ? 0 : side == 1 ? 1 : 100 + (int)gGame->sold.size();   // hostile to all: a team of its own
  int slot = gGame->sandboxAdd(e.ait, e.etype, team, feet, yaw);
  if (slot >= 0) logf("[eggs] spawned %s at %.1f %.1f %.1f (slot %d, side %s)", e.name.c_str(), feet.x, feet.y, feet.z, slot, side == 0 ? "A" : side == 1 ? "B" : "hostile");
  return slot;
}
float yawFacing(App& app, vec3 p) { vec3 d = app.camPos - p; return glm::degrees(std::atan2(d.x, d.z)); }   // trySpawn's convention (atan2 of the facing direction's x, z)
}  // namespace

// ---------------------------------------------------------------- Game: the sandbox
bool Game::soldierBrain(AiType a) {
  static const bool opfIdle = fs::exists(contentDir() / "characters" / "operator_f1_idle.glb");   // BC1: the Giant Operator's local idle model: it then stands still on the melee brain (zombie_local.json operator_f_idle)
  return a == AiType::Soldiers || a == AiType::Operators || (a == AiType::OperatorF && !opfIdle) || a == AiType::Troopers || a == AiType::Robots || a == AiType::Desert ||
         a == AiType::Delta || a == AiType::Allied || a == AiType::SS || a == AiType::AlliedPara || (a == AiType::Mechs && gMechRobot);   // gameplay 62c4fe18: the gas-mask types retired
}

bool Game::sandboxBegin(std::string& err) {
  if (!loadData(err) || !ensureModels(err)) return false;
  match = Match{};
  match.rules.mode = GameMode::TDM; match.rules.killCap = 1 << 30; match.rules.respawnDelay = 1e9f;   // no win, no respawn
  match.slots.reserve(ScenarioRules::kMaxAI); for (auto& s : sold) if (s.ciReady) { s.ci.destroy(); s.ciReady = false; } sold.clear(); sold.reserve(ScenarioRules::kMaxAI);    // never reallocated while spawning
  localSlot = -1; pendingLocal = -1; isClient = false; pendingStart = false; paused = false;
  app.syncRuntime();   // bodies placed since the load
  const vec3 hv(app.terrain.buildHalfX, app.terrain.buildHalf, app.terrain.buildHalfZ);
  aiMatchBegin(*this, -hv, hv);   // navmesh over the build area
  pool.clear(); poolBuilt = false;
  sounds.clear(); gren.clear(); grenadeReserve.clear(); smokeReserve.clear(); defSpots.clear(); defSpotsStage = -1;
  t = 0; steps = 0; spectate = -1; specCam = false; hitMarker = killMarker = hurtFlash = 0;
  ghosts.clear(); doorSeen.clear();
  match.phase = MatchPhase::Running;
  active = true; sandbox = true;
  setupError.clear();
  logf("[eggs] sandbox: nav %d floors in %.0f ms", nav.usedNodes, nav.buildMs);
  return true;
}

int Game::sandboxAdd(AiType ait, int etype, int team, vec3 feet, float yaw) {
  if (!active || !sandbox || (int)sold.size() >= ScenarioRules::kMaxAI) return -1;
  static int kind[kAiTypeCount] = {}; static int germanSeen = 0;
  RosterSlot rs; rs.id = (int)match.slots.size(); rs.team = team;
  rs.faction = team == 0 ? Faction::Allies : Faction::Axis; rs.role = team == 0 ? CombatRole::Defender : CombatRole::Attacker;
  rs.human = false; rs.peer = -1; rs.livesLeft = 1;   // placeSoldier spends it: 0 left, never respawns
  rs.name = aiTypeName(ait);
  match.slots.push_back(rs); sold.emplace_back();
  setupSlotSoldier(match.slots.back(), ait, kind[(int)ait]++, germanSeen, etype);
  Physics::Hit h; if (Physics::castRayEx(feet + vec3(0, 2.0f, 0), vec3(0, -1, 0), 6.0f, h)) feet = h.pos;   // on whatever is under the cursor (a floor, a roof)
  placeSoldier(rs.id, feet, yaw);
  return rs.id;
}

// ---------------------------------------------------------------- the App side
void eggsEnd(App& app) {   // Clear / Explore / map change / shutdown
  E.acc = 0;
  if (!sandboxOn()) return;
  if (AiCore* c = aiCore()) c->frozen = false;
  gGame->end(); gGame->sandbox = false;
  (void)app;
}

bool eggsTakesRmb(App& app) { return app.mode == Mode::Creative && E.sel >= 0 && !lanBusy(); }   // editor_nav: no orbit while an egg is selected

void eggsUpdate(App& app, float dt) {   // App::updateCreative: right-click spawning, then the sandbox's steps
  if (app.mode != Mode::Creative) return;
  bool rmb = glfwGetMouseButton(app.win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  if (E.sel >= 0 && !app.selftest) {
    auto list = eggs();
    if (E.sel >= (int)list.size()) E.sel = -1;
    else if (rmb && !ImGui::GetIO().WantCaptureMouse && app.hoverValid) {
      E.holdT = E.rmbWas ? E.holdT - dt : 0.0f;
      if (E.holdT <= 0) { spawnEgg(app, list[E.sel], app.hoverGround, yawFacing(app, app.hoverGround), E.side); E.holdT = 0.3f; }
    }
  }
  E.rmbWas = rmb;
  if (!sandboxOn()) return;
  if (AiCore* c = aiCore()) c->frozen = E.frozen;
  E.acc = std::min(E.acc + dt, 0.25f);
  int n = 0; while (E.acc >= 1.0f / 60.0f) { gGame->step(1.0f / 60.0f); E.acc -= 1.0f / 60.0f; ++n; }
  gGame->posesFrame(n / 60.0f, 1.0f);
  if (n > 0) Physics::step(n / 60.0f, n);
  if (n > 0) app.fx.update(n / 60.0f, app.terrain);   // SG7: the sandbox's effects age with its steps (Creative never stepped them: every hit spark, flash, blood puff and dust burst a sandbox fight made stayed where it was at full brightness, the additive HDR ones stacking into a saturated disc)
}

void eggsDraw(App& app) { (void)app; if (sandboxOn()) gGame->drawItems(); }   // mpaiFrame in Creative

void eggsToolbar(App& app) {   // the Creative toolbar button
  (void)app;
  if (ImGui::Button(E.open ? "Eggs *" : "Eggs")) E.open = !E.open;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spawn eggs: pick an AI type, then right-click the ground to spawn it (testing).");
}

void eggsWindow(App& app) {   // App::drawCreativeUI
  if (!E.open || app.mode != Mode::Creative) return;
  ImGui::SetNextWindowSize(ImVec2(300, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Spawn eggs", &E.open)) { ImGui::End(); return; }
  ImGui::TextWrapped("Select an egg, then right-click the ground to spawn one (hold for several). Testing only: not saved, not on LAN.");
  ImGui::Text("Side:"); ImGui::SameLine(); ImGui::RadioButton("A", &E.side, 0); ImGui::SameLine(); ImGui::RadioButton("B", &E.side, 1); ImGui::SameLine(); ImGui::RadioButton("Hostile to all", &E.side, 2);
  bool on = !E.frozen; if (ImGui::Checkbox("AI on", &on)) E.frozen = !on;
  ImGui::SameLine(); ImGui::TextDisabled(E.frozen ? "(frozen: they stand still)" : "(they move and fight)");
  int alive = 0; if (sandboxOn()) for (auto& s : gGame->sold) alive += s.alive;
  ImGui::Text("Spawned: %d (%d alive) / %d", spawnedCount(), alive, ScenarioRules::kMaxAI);
  if (ImGui::Button("Clear spawned AI")) eggsEnd(app);
  if (E.sel >= 0) { ImGui::SameLine(); if (ImGui::Button("Deselect")) E.sel = -1; }
  if (lanBusy()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "In a LAN session: spawn eggs are off.");
  ImGui::Separator();
  auto list = eggs();
  bool header = false;
  for (int k = 0; k < (int)list.size(); ++k) {
    if (k == 0) ImGui::TextDisabled("Soldiers");
    if (!list[k].soldier && !header) { header = true; ImGui::Separator(); ImGui::TextDisabled("Creatures and enemies"); }
    ImGui::PushID(k);
    if (ImGui::Selectable(list[k].name.c_str(), E.sel == k)) E.sel = E.sel == k ? -1 : k;
    ImGui::PopID();
  }
  ImGui::End();
}

// ---------------------------------------------------------------- --selftest-eggs
bool eggsSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-eggs") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  static int stage = 0; static float t = 0; static int want = 0;
  static bool good = false; static int hk = -1; static vec3 hk0(0), hk1(0); static size_t corp0 = 0;   // HK5
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  t += dt;
  if (stage == 0) {
    const char* mid = std::getenv("NMK_EGGS_MAP"); const TemplateInfo* tp = a.findTemplate(mid ? mid : "hollow_village");
    if (!tp) { logf("[eggs] FAIL no template %s", mid ? mid : "hollow_village"); return finish(1); }
    a.newMap(*tp, "Eggs test");
    stage = 1; t = 0; return true;
  }
  if (stage == 1 && t > 1.0f) {
    auto list = eggs(); want = (int)list.size();
    int ok = 0;
    for (int k = 0; k < want; ++k) {   // a ring round the build area's middle, 4 m apart
      float ang = k * 6.2831853f / std::max(want, 1), r = 6.0f + 0.6f * want;
      vec3 p(std::cos(ang) * r, 0, std::sin(ang) * r); p.y = a.terrain.height(p.x, p.z);
      ok += spawnEgg(a, list[k], p, 0.0f, k % 3) >= 0;
    }
    logf("[eggs] %d / %d eggs spawned (soldier types + enemy types at runtime)", ok, want);
    a.fx.flash(vec3(0, a.terrain.height(0, 0) + 1.5f, 0), 0.1234f, 0.05f, vec4(9, 6, 3, 1));   // SG7: a 50 ms marker flash: it must be gone after the sandbox's 5 s
    if (ok != want) { logf("[eggs] FAIL not every egg spawned"); return finish(1); }
    stage = 2; t = 0; return true;
  }
  if (stage == 2 && t > 5.0f) {   // 5 s of the sandbox stepping (eggsUpdate via updateCreative)
    int alive = 0, finite = 0; for (auto& s : gGame->sold) { alive += s.alive; finite += std::isfinite(s.feet.x) && std::isfinite(s.feet.y) && std::isfinite(s.feet.z); }
    logf("[eggs] after 5 s: %zu soldiers, %d alive, %d finite, sandbox steps %llu", gGame->sold.size(), alive, finite, (unsigned long long)gGame->steps);
    good = finite == (int)gGame->sold.size() && gGame->steps > 0;
    {   // SG7: the sandbox's effects age: the marker flash is gone, no particle outlives its lifetime, none is brighter than an HDR 64
      int marker = 0, overLife = 0, tooBright = 0; float maxC = 0;
      for (auto& p : a.fx.parts) { marker += std::fabs(p.maxLife - 0.05f) < 1e-4f && std::fabs(p.size - 0.1234f) < 1e-4f; overLife += p.life > p.maxLife + 1e-4f; float c = std::max(p.col.r, std::max(p.col.g, p.col.b)); maxC = std::max(maxC, c); tooBright += c > 64.0f; }
      const bool fxOk = marker == 0 && overLife == 0 && tooBright == 0;
      logf("[eggs] SG7 effects in the sandbox: %s (%zu particles, marker flash %s, %d past their life, brightest %.1f, %d above 64)", fxOk ? "ok" : "FAIL", a.fx.parts.size(), marker ? "still there" : "gone", overLife, maxC, tooBright);
      good = good && fxOk;
    }
    // HK5: the Mounted Knight's horse is brought down (the others frozen, so nothing else hurts him): the horse must stay as a
    // corpse and the same slot go on on foot where he landed, and still be there 10 s later
    E.frozen = true;
    for (auto& s : gGame->sold) if (hk < 0 && s.alive && s.zombie && enemyType(s.etype).id == "knight10m") hk = s.slot;
    if (hk >= 0) {
      hk0 = gGame->sold[hk].feet; corp0 = gGame->horseCorpses.size(); gGame->damage(hk, 100000.0f, -1, vec3(1, 0, 0), "blast");
      const bool down = !gGame->sold[hk].alive && gGame->sold[hk].mountDeath == 1 && gGame->match.slots[hk].alive;
      logf("[eggs] HK5 horse down: %s (slot %d, roster %s)", down ? "ok" : "FAIL", hk, gGame->match.slots[hk].alive ? "alive" : "dead"); good = good && down;
      stage = 3; t = 0; return true;
    }
    logf("[eggs] HK5 skipped: no Mounted Knight egg alive");
    stage = 5; t = 0; return true;
  }
  if (stage == 3 && t > 4.5f) {   // past the 3.1 s horse death
    const Soldier& s = gGame->sold[hk]; hk1 = s.feet;
    const bool ok = s.alive && enemyType(s.etype).id == "knight10" && s.hp > 0 && gGame->match.slots[hk].alive && !gGame->match.slots[hk].pending
                    && gGame->horseCorpses.size() == corp0 + 1 && !gGame->horseCorpses.back()->parts.empty() && glm::length(s.feet - hk0) < 6.0f;
    logf("[eggs] HK5 on foot: %s (alive %d, type %s, hp %.0f, %zu horse corpses with %zu parts drawn, %.1f m from where the horse fell)", ok ? "ok" : "FAIL", (int)s.alive,
         enemyType(s.etype).id.c_str(), s.hp, gGame->horseCorpses.size(), gGame->horseCorpses.empty() ? (size_t)0 : gGame->horseCorpses.back()->parts.size(), glm::length(s.feet - hk0));
    good = good && ok; stage = 4; t = 0; return true;
  }
  if (stage == 4 && t > 10.0f) {
    const Soldier& s = gGame->sold[hk];
    const bool ok = s.alive && s.hp > 0 && gGame->horseCorpses.size() == corp0 + 1 && glm::length(s.feet - hk1) < 3.0f;
    logf("[eggs] HK5 10 s later: %s (alive %d, %zu horse corpses, moved %.1f m)", ok ? "ok" : "FAIL", (int)s.alive, gGame->horseCorpses.size(), glm::length(s.feet - hk1));
    good = good && ok; stage = 5; t = 0; return true;
  }
  if (stage == 5) {
    E.frozen = false;
    eggsEnd(a);
    bool cleared = !gGame->active && gGame->sold.empty();
    logf("[eggs] clear: %s", cleared ? "ok" : "FAIL");
    return finish(good && cleared ? 0 : 1);
  }
  return true;
}
