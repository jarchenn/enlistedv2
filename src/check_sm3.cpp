// SM3 --check-sm3: the smithing table in every mode. Explore (blank slate), Team Deathmatch, Search and Destroy and
// Battle Royale (Backrooms: the round starts on the ground): a table placed 2 m in front of the player, looked at: the
// prompt's target (a smithing table within reach); E (the scripted interact) opens the screen and it stays open; fits:
// outside Battle Royale every part that fits the gun is free (holographic and thermal included where they fit), in
// Battle Royale a part must be carried (refused without, used up with); E closes it. TDM: the fits survive a respawn;
// SD: they survive the round change. A capture of the screen in SD with a rifle: selftest_out/sm3_sd_fitting.png.
// selftest_out/report_sm3.txt.
#include "selftest.h"
#include "smithy.h"
#include "loot.h"
#include "physics.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "scenario.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "ai_core.h"
#include <array>

namespace {
MpTest* T = nullptr;
int mi = -1, stage = 0, frames = 0, gun = -1, sdRound = 0; float st = 0; uint64_t table = 0; std::array<int, 4> fitted{};
const char* kModes[] = {"Explore", "TDM", "SD", "BR"};
std::string attStr(const WeaponState& s) { std::string o; for (int k = 0; k < 4; ++k) o += std::to_string(s.att[k]) + (k < 3 ? "/" : ""); return o; }

bool startMode(App& a, int m) {
  if (a.mode == Mode::Explore) a.exitExplore();
  if (m == 0) { a.newMap(*a.findTemplate("blank_slate"), "SM3"); bool ok = a.enterExplore(); a.useScripted = true; a.scriptedInput = ExploreInput{}; return ok; }
  const char* map = m == 1 ? "examples/village_assault_3.nmap.json" : m == 2 ? "templates/sd_desert.nmap.json" : "templates/backrooms.nmap.json";
  a.openMap(contentDir() / map);
  Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
  sc.rules.mode = m == 1 ? GameMode::TDM : m == 2 ? GameMode::SearchDestroy : GameMode::BattleRoyale; sc.rules.humanTeam = 0;
  if (m == 1) { sc.rules.killCap = 999; for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); sc.rules.teamPlayers[t] = 2; } sc.rules.aiCount[0][(int)AiType::Soldiers] = 1; sc.rules.aiCount[1][(int)AiType::Soldiers] = 2; }
  if (m == 2) { sc.rules.sdWarmup = false; for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 0 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); } }
  if (m == 3) sc.rules.royalePlayers = 10;
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  if (SdMatch* sm = sdMatch()) { sm->cfg.freeze = 30; sm->cfg.roundEnd = 1.0f; }
  return ok;
}
}   // namespace

bool sm3Check(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-sm3") mode = 1; }
  if (!mode) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("sm3");
  st += dt; ++frames;
  auto finish = [&]() { if (smithyOpen()) smithyClose(a); if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = T->finish(); a.selftest->done = true; return true; };
  auto next = [&]() { stage = 0; st = 0; frames = 0; table = 0; gun = -1; sdRound = 0; if (++mi >= 4) return finish(); T->check(startMode(a, mi), std::string(kModes[mi]) + ": starts"); return true; };
  if (mi < 0) return next();
  if (st > 90) { T->check(false, std::string(kModes[mi]) + ": timed out at stage " + std::to_string(stage)); return next(); }
  const std::string M = kModes[mi];
  Game* g = gGame && gGame->active ? gGame : nullptr;
  Soldier* L = g ? g->local() : nullptr;
  if (g && (!L || !L->alive) && stage != 7) return true;   // spawning / a respawn
  if (AiCore* A = aiCore()) A->frozen = true;
  if (L) L->protectUntil = 1e9;
  if (stage == 0) {   // standing on the ground: a gun in hand, a table 2 m ahead, looked at
    if (st < 2.0f || !Physics::character().grounded) return true;
    if (mi == 3 || mi == 2) {   // BR starts empty-handed, SD with a pistol: a rifle for the fits and the capture
      int r = a.weaponIndex("modern_m4_irons"); if (r < 0) r = a.weaponIndex("m1_garand");
      if (r >= 0 && std::find(a.carried.begin(), a.carried.end(), r) == a.carried.end()) a.carried.push_back(r);
      if (r >= 0) a.selectWeapon(r);
    }
    for (int w : a.carried) if (lootGunTakesParts(*a.weaponDefs[w]) && (w == a.activeWeapon || gun < 0)) gun = w;
    T->check(gun >= 0, M + ": a gun that takes parts: " + (gun >= 0 ? a.weaponDefs[gun]->id : std::string("none")));
    if (gun < 0) return next();
    a.selectWeapon(gun);
    for (int& x : a.wstate[a.weaponDefs[gun]->id].att) x = 0;
    const AssetDef* td = a.lib.find("smithing_table");
    vec3 f(-std::sin(radians(a.eyeYaw)), 0, -std::cos(radians(a.eyeYaw))), feet = Physics::character().pos;
    vec3 p = vec3(feet.x, 0, feet.z) + f * 2.0f; p.y = feet.y - 0.9f;   // the capsule centre sits ~0.9 m over the feet
    Physics::Hit h; if (Physics::castRayEx(p + vec3(0, 1.5f, 0), vec3(0, -1, 0), 4.0f, h)) p.y = h.pos.y;
    table = td ? a.placeInstance(td, p, a.eyeYaw) : 0; a.syncRuntime();
    T->check(table != 0, M + ": a smithing table placed 2 m ahead");
    if (!table) return next();
    vec3 c = p + vec3(0, (td->bmin.y + td->bmax.y) * 0.5f, 0), d = c - a.eyePos();
    a.eyePitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
    stage = 1; frames = 0; return true;
  }
  if (stage == 1 && frames > 4) {   // the prompt: the table is the interact target within reach
    char b[200]; std::snprintf(b, sizeof b, ": the prompt \"E - Use smithing table\" (target kind %d, %.2f m, valid %d)", a.target.kind, a.target.t, (int)a.target.valid);
    T->check(a.target.valid && a.target.kind == 4 && a.target.inst == table && a.target.t <= App::kInteractReach, M + b);
    a.scriptedInput.interact = true;
    stage = 2; frames = 0; return true;
  }
  if (stage == 2 && frames > 10) {   // E opened it, and it stayed open
    T->check(smithyOpen(), M + ": E opens the fitting screen and it stays open (10 frames later)");
    if (!smithyOpen()) return next();
    const WeaponDef& d = *a.weaponDefs[gun]; WeaponState& s = a.wstate[d.id];
    if (mi == 3) {   // BR: the carried-parts rule
      std::string why; bool ok = smithyApply(a, gun, ATT_SIGHT, "br_sight_reddot", &why);
      T->check(!ok && s.att[ATT_SIGHT] == 0, M + ": a part not carried is refused (" + why + ")");
      Kit* k = kitOf(L->slot); if (k) k->items["br_sight_reddot"] = 1;
      ok = smithyApply(a, gun, ATT_SIGHT, "br_sight_reddot", &why);
      T->check(ok && s.att[ATT_SIGHT] == 1 && k && !k->items.count("br_sight_reddot"), M + ": a carried red dot fits and leaves the bag" + (ok ? "" : ": " + why));
    } else {   // free: every part that fits
      std::string list; int n = 0, bad = 0;
      for (int sl = 0; sl < 4; ++sl) for (int v = 1; v < 7; ++v) {
        const char* id = lootAttachItemId(sl, v); const ItemDef* it = *id ? lootItem(id) : nullptr;
        if (!it || !lootAttachFits(d, *it)) continue;
        std::string why; bool ok = smithyApply(a, gun, sl, id, &why);
        if (ok && s.att[sl] == v) { ++n; list += it->name + ", "; } else { ++bad; list += it->name + " FAILED (" + why + "), "; }
      }
      T->check(n >= 4 && bad == 0, M + ": free fits on the " + d.name + " (no parts carried): " + std::to_string(n) + ": " + list);
      bool holo = lootSightFits(d, 5), therm = lootSightFits(d, 6);
      T->check(holo && therm && list.find(lootItem("br_sight_holo")->name) != std::string::npos && list.find(lootItem("br_sight_thermal")->name) != std::string::npos,
               M + ": the holographic (sight 5) and thermal (sight 6) sights are in the list");
      smithyApply(a, gun, ATT_SIGHT, "br_sight_holo"); smithyApply(a, gun, ATT_MUZZLE, "br_suppressor");   // what should stay on
    }
    for (int k = 0; k < 4; ++k) fitted[k] = s.att[k];
    if (mi == 2) { a.uiShot = exeDir() / "selftest_out" / "sm3_sd_fitting.png"; }
    stage = 3; frames = 0; return true;
  }
  if (stage == 3 && frames > 6) { a.scriptedInput.interact = true; stage = 4; frames = 0; return true; }
  if (stage == 4 && frames > 4) {
    T->check(!smithyOpen(), M + ": E closes it");
    if (mi == 0 || mi == 3) return next();
    if (mi == 1) { g->killSoldier(L->slot, -1, vec3(0, 0, 1)); stage = 7; st = 0; return true; }   // TDM: a respawn
    if (mi == 2) { sdRound = sdMatch()->round; for (auto& so : g->sold) if (so.alive && sdMatch()->sideOf(so.slot) == sdMatch()->sideOf(L->slot)) g->killSoldier(so.slot, -1, vec3(0, 0, 1)); stage = 7; st = 0; return true; }   // SD: the round ends
  }
  if (stage == 7) {   // the fits after the respawn / the next round
    bool back = L && L->alive && (mi != 2 || (sdMatch() && sdMatch()->round > sdRound && sdMatch()->phase == SdPhase::Freeze));
    if (!back || st < 1.0f) return true;
    const WeaponDef& d = *a.weaponDefs[gun]; const WeaponState& s = a.wstate[d.id];
    bool same = true; for (int k = 0; k < 4; ++k) same &= s.att[k] == fitted[k];
    T->check(same, M + ": the " + d.name + "'s parts after " + (mi == 1 ? "a respawn" : "the round change") + ": " + attStr(s) + " (fitted " + std::to_string(fitted[0]) + "/" + std::to_string(fitted[1]) + "/" + std::to_string(fitted[2]) + "/" + std::to_string(fitted[3]) + ")");
    return next();
  }
  return true;
}
