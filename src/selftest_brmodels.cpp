// --capture-br-models (BR3): no checks, a look at the Battle Royale models. A friendly bot in the marine body stands
// 2.6 m in front of the player, unarmoured and in each armour level (helmet + vest, level 1-2 with a backpack / the
// military one), front and back; the inventory preview is written as a PNG; then each optic on the AK (animated rig)
// and the ACOG on the Kar98k (static model), from the hip and aimed. selftest_out/brm_*.png.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "br_models.h"
#include "character.h"
#include "loot.h"
#include <functional>
#include <cstdlib>

bool brInventoryPreviewPng(const fs::path& out, float yaw, float cy = 0, float halfH = 0);   // br_models.cpp

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr; int mode = -1; bool started = false; fs::path pendingShot;
int bot = -1;
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }

void hold(App& a) {   // the bot stays put in front of the player, facing it (or its back to it)
  Game& G = *gGame; Soldier* L = G.local(); if (!L || bot < 0) return;
  vec3 f = App::forward(a.eyeYaw, 0); vec3 p = L->feet + glm::normalize(vec3(f.x, 0, f.z)) * (mode == 2 ? 1.7f : 2.6f);
  Soldier& b = G.sold[bot];
  if (b.agent >= 0) Physics::setAgentPos(b.agent, p);
  b.feet = p; b.vel = vec3(0); b.hp = 1000; b.protectUntil = -1; if (mode != 2) b.weapon = -1;   // --capture-br-worn: the rifle stance
}
float botYaw = 0;
bool holdBot = true;
void shot(const std::string& name, std::function<void(App&)> setup, float wait = 0.6f) {
  push(name, [name, setup, wait](App& a, float t) {
    if (t == 0) setup(a);
    if (holdBot) { hold(a); if (bot >= 0) gGame->sold[bot].yaw = botYaw; }
    else if (bot >= 0 && gGame->local()) { Soldier& b = gGame->sold[bot]; vec3 p = gGame->local()->feet + vec3(0, 0, -40); if (b.agent >= 0) Physics::setAgentPos(b.agent, p); b.feet = p; }
    if (t < wait) return false;
    if (pendingShot.empty()) { pendingShot = outDir() / (name.rfind("brw_", 0) == 0 ? name + ".png" : std::string("brm_") + name + ".png"); return true; }
    return false; });
}
void wear(int h, int v, int b) { Soldier& s = gGame->sold[bot]; s.helmetLvl = h; s.vestLvl = v; s.backpack = b; if (Soldier* L = gGame->local()) { L->helmetLvl = h; L->vestLvl = v; L->backpack = b; } }

void pushStart() {
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
    for (auto& s : G.sold) if (s.faction != L->faction) { s.hp = 1000; if (s.agent >= 0) Physics::setAgentPos(s.agent, L->feet + vec3(200, 0, 200)); s.feet = L->feet + vec3(200, 0, 200); }
    int m = brMarineModel(G);
    T->check(m >= 0, "br_marine.glb loaded");
    T->check(bot >= 0, "a friendly bot");
    if (m >= 0 && bot >= 0) { Soldier& b = G.sold[bot]; if (b.ciReady) b.ci.destroy(); b.ciReady = false; b.model = m; b.human = true; L->model = m; }
    L->hp = 5000; a.eyePitch = -8;
    botYaw = a.eyeYaw + 180;   // facing the player
    return true; });
}

// --capture-br-worn (BR8): every helmet, every vest, each backpack over each vest on the marine. Per combination: the
// inventory preview renderer close up on the head and the torso and in full (front, side, back:
// selftest_out/brw_<combo>_<view>_<head|torso|full>.png), then a bot 1.7 m away in the rifle stance (brw_<combo>_world_<view>.png).
void buildWorn(App&) {
  pushStart();
  push("pitch", [](App& a, float) { a.eyePitch = -12; return true; });
  struct C { int h, v, b; };
  std::vector<C> combos = {{1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
  for (int b = 1; b <= 2; ++b) for (int v = 0; v <= 3; ++v) combos.push_back({0, v, b});
  combos.push_back({3, 3, 2});
  static const char* vw[3] = {"front", "side", "back"};
  for (C c : combos) {
    char tag[32]; std::snprintf(tag, sizeof tag, "h%d_v%d_b%d", c.h, c.v, c.b); std::string tg = tag;
    push("previews " + tg, [c, tg](App&, float t) {
      if (t == 0) { wear(c.h, c.v, c.b); return false; }
      if (t < 0.25f) return false;
      for (int k = 0; k < 3; ++k) {
        std::string base = "brw_" + tg + "_" + vw[k];
        brInventoryPreviewPng(outDir() / (base + "_head.png"), k * 90.0f, 1.62f, 0.24f);
        brInventoryPreviewPng(outDir() / (base + "_torso.png"), k * 90.0f, 1.25f, 0.45f);
        brInventoryPreviewPng(outDir() / (base + "_full.png"), k * 90.0f);
      }
      return true; });
    for (int k = 0; k < (std::getenv("NMK_BRW_FAST") ? 0 : 3); ++k) {   // NMK_BRW_FAST: previews only
      float off = k == 0 ? 180.0f : k == 1 ? 90.0f : 0.0f;
      shot("brw_" + tg + "_world_" + vw[k], [off](App& a) { botYaw = a.eyeYaw + off; }, 0.5f);
    }
  }
  push("done", [](App& a, float t) { if (t < 0.5f) return false; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}

void build(App&) {
  pushStart();
  const char* lv[4] = {"none", "lvl1", "lvl2", "lvl3"};
  for (int k = 0; k < 4; ++k) {
    std::string nm = std::string("armor_") + lv[k];
    int b = k == 0 ? 0 : k == 3 ? 2 : k;   // level 1 with the backpack, 2 and 3 with the military one
    shot(nm + "_front", [k, b](App& a) { wear(k, k, b); botYaw = a.eyeYaw + 180; });
    shot(nm + "_side", [](App& a) { botYaw = a.eyeYaw + 90; });
    shot(nm + "_back", [](App& a) { botYaw = a.eyeYaw; });
  }
  push("preview", [](App&, float t) { if (t < 0.2f) return false; wear(3, 3, 2); T->check(brInventoryPreviewPng(outDir() / "brm_inventory_preview.png", 200), "inventory preview rendered"); return true; });
  // optics: the AK (animated rig) and the Kar98k (static model)
  // from here a Battle Royale match (BR2's loot rules: the optic reticles, a sniper's own scope as its ACOG)
  push("battle royale", [](App&, float) { gGame->match.rules.mode = GameMode::BattleRoyale; lootBegin(*gGame); return true; });
  static const char* sn[5] = {"none", "reddot", "acog", "4x", "10x"};
  for (const char* gun : {"modern_ak", "kar98k", "modern_sniper_bolt"}) {
    for (int k = 0; k <= 4; ++k) {
      if (std::string(gun) == "kar98k" && k != 2) continue;
      if (std::string(gun) == "modern_sniper_bolt" && k != 0 && k != 4) continue;   // its own scope (= the ACOG) and the 10x
      for (int ads = 0; ads < 2; ++ads) {
        std::string g = gun;
        shot(std::string("sight_") + gun + "_" + sn[k] + (ads ? "_ads" : "_hip"), [g, k, ads](App& a) {
          holdBot = false;
          int w = a.weaponIndex(g); std::string msg;
          if (w >= 0 && a.activeWeapon != w) a.equipWeapon(w, msg);
          if (w >= 0) a.wstate[a.weaponDefs[w]->id].att[0] = k;
          a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = ads != 0;
        }, 1.2f);
      }
    }
  }
  push("inventory", [](App& a, float t) {   // the inventory screen with the sniper and the AK carried
    if (t == 0) {
      a.scriptedInput = ExploreInput{}; a.carried.clear();
      for (const char* id : {"modern_sniper_bolt", "modern_ak"}) { int w = a.weaponIndex(id); if (w >= 0) a.carried.push_back(w); }
      a.wstate["modern_sniper_bolt"].att[0] = 0; a.wstate["modern_ak"].att[0] = 1;
      wear(2, 2, 2); lootSetInventory(true);
    }
    if (t < 1.0f) return false;
    a.uiShot = outDir() / "brm_inventory_ui.png"; return true;   // uiShot: after ImGui, so the inventory is in it
    return false; });
  push("close", [](App&, float t) { if (t < 1.0f) return false; lootSetInventory(false); return true; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}

bool brModelsSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { std::string s = __argv[k]; if (s == "--capture-br-models") mode = 1; else if (s == "--capture-br-worn") mode = 2; } }
  if (mode != 1 && mode != 2) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); T->note(pendingShot.filename().string()); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest(mode == 2 ? "br_worn" : "br_models"); if (mode == 2) buildWorn(app); else build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 300) { T->check(false, "timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
