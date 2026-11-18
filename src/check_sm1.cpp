// SM1 --check-sm1: through the smithing table's own path (smithyApply), fit and remove every attachment type a weapon
// can take: a weapon that takes a part in all four slots, every fitting part of each slot (sight swaps put the old one
// back), the bag counts after each; a weapon with no fittings refuses them; a table goes inside a workshop.
// --capture-sm1: the table in a workshop (outside view), then the fitting screen with a rifle carrying three parts.
#include "selftest.h"
#include "smithy.h"
#include "loot.h"
#include "physics.h"
#include "game.h"
#include "mpui.h"
#include "scenario.h"

namespace {
int fails = 0;
void chk(bool ok, const std::string& w) { logf("[sm1] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; }
int phase = 0; float t = 0; fs::path pend; uint64_t shop = 0, table = 0; Kit kit;
void look(App& a, vec3 eye, vec3 at) { vec3 d = at - eye; a.camPos = eye; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
fs::path out(const char* n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / (std::string("sm1_") + n + ".png"); }
}

bool sm1SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { std::string s = __argv[k]; if (s == "--check-sm1") mode = 1; if (s == "--capture-sm1") mode = 2; if (s == "--check-sm1-br") mode = 3; } }
  if (!mode) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  float t0 = t; t += dt;
  auto at = [&](float m) { return t0 < m && t >= m; };
  auto done = [&]() { smithyUseKit(nullptr); smithyTestHover(-1); if (smithyOpen()) smithyClose(a); if (a.mode == Mode::Explore) a.exitExplore(); logf("[sm1] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; return true; };
  if (mode == 3) {   // SM2: real Battle Royale matches: how many tables each map gets, the anvils on the large map
    static const char* maps[] = {"newtown", "outpost_woods"}; static int mi = -1; static float tm = 0;
    if (mi < 0 || tm > 4.0f) {
      if (mi >= 0) {
        size_t n = smithyPlaced().size(); std::string where;
        for (uint64_t id : smithyPlaced()) if (const Instance* ti = a.doc.find(id)) {   // the building each stands in: the nearest enterable one
          const Instance* best = nullptr; float bd = 1e9f;
          for (auto& i : a.doc.instances) { const AssetDef* ad = a.assetFor(i); if (!ad || !ad->enterable) continue; float d2 = glm::length(i.pos - ti->pos); if (d2 < bd) { bd = d2; best = &i; } }
          if (best) where += " " + best->assetId;
        }
        chk(n >= 2, std::string("battle royale on ") + maps[mi] + ": " + std::to_string(n) + " smithing tables placed (in" + where + ")");
        a.exitExplore();
      }
      if (++mi >= 2) return done();
      a.openMap(contentDir() / "templates" / (std::string(maps[mi]) + ".nmap.json"));
      Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
      sc.rules.mode = GameMode::BattleRoyale;
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true;
      bool ok = a.enterExplore() && gGame->active;
      a.useScripted = true; a.scriptedInput = ExploreInput{};
      chk(ok, std::string("battle royale starts on ") + maps[mi]);
      tm = 0; return true;
    }
    float t1 = tm; tm += dt;
    if (t1 < 2.5f && tm >= 2.5f) { a.mapLarge = true; a.uiShot = out((std::string("brmap_") + maps[mi]).c_str()); }
    if (t1 < 3.2f && tm >= 3.2f) a.mapLarge = false;
    return true;
  }
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "SM1");
    const AssetDef* ws = a.lib.find("mod_nt_workshop");
    shop = ws ? a.placeInstance(ws, vec3(0, a.terrain.height(0, 0), 0), 0) : 0;
    a.syncRuntime(); Physics::optimizeBroadPhase();
    const Instance* si = a.doc.find(shop);
    table = si ? smithyPlaceIn(a, *si) : 0;
    a.syncRuntime();
    const Instance* ti = a.doc.find(table);
    chk(ti != nullptr, "a smithing table placed inside the workshop" + (ti ? " at (" + std::to_string(ti->pos.x) + ", " + std::to_string(ti->pos.z) + ")" : std::string()));
    smithyUseKit(&kit);
    if (mode == 1) {
      // a weapon with a part for every slot
      int gun = -1;
      for (int w = 0; w < (int)a.weaponDefs.size() && gun < 0; ++w) {
        const WeaponDef& d = *a.weaponDefs[w]; bool all = true;
        for (int sl = 0; sl < 4 && all; ++sl) { bool any = false; for (int v = 1; v < 7; ++v) { const char* id = lootAttachItemId(sl, v); if (*id) if (const ItemDef* it = lootItem(id)) any |= lootAttachFits(d, *it); } all = any; }
        if (all) gun = w;
      }
      chk(gun >= 0, std::string("a weapon taking all four slots: ") + (gun >= 0 ? a.weaponDefs[gun]->id : "none"));
      if (gun >= 0) {
        const WeaponDef& d = *a.weaponDefs[gun]; WeaponState& s = a.wstate[d.id]; for (int& x : s.att) x = 0;
        int fitted = 0, removed = 0;
        for (int sl = 0; sl < 4; ++sl) for (int v = 1; v < 7; ++v) {
          const char* id = lootAttachItemId(sl, v); if (!*id) continue;
          const ItemDef* it = lootItem(id); if (!it || !lootAttachFits(d, *it)) continue;
          kit.items[id] = 1; std::string why;
          int prev = s.att[sl]; const char* prevId = lootAttachItemId(sl, prev);
          bool ok = smithyApply(a, gun, sl, id, &why);
          bool swapBack = !*prevId || kit.items.count(prevId);   // a replaced part went back into the bag
          chk(ok && s.att[sl] == v && !kit.items.count(id) && swapBack, std::string("fit ") + it->name + " on the " + d.name + (ok ? "" : ": " + why));
          fitted += ok;
        }
        for (int sl = 0; sl < 4; ++sl) {
          if (!s.att[sl]) continue;
          const char* id = lootAttachItemId(sl, s.att[sl]); int n0 = kit.items.count(id) ? kit.items[id] : 0; std::string why;
          bool ok = smithyApply(a, gun, sl, "", &why);
          chk(ok && s.att[sl] == 0 && kit.items[id] == n0 + 1, std::string("remove the ") + lootSlotName(sl) + " part (back in the bag)" + (ok ? "" : ": " + why));
          removed += ok;
        }
        chk(fitted >= 4 && removed == 4, "fitted " + std::to_string(fitted) + " parts across the four slots and removed one per slot");
      }
      int launcher = -1; for (int w = 0; w < (int)a.weaponDefs.size(); ++w) if (a.weaponDefs[w]->action == "launcher") { launcher = w; break; }
      if (launcher >= 0) { kit.items["br_sight_reddot"] = 1; std::string why; bool ok = smithyApply(a, launcher, ATT_SIGHT, "br_sight_reddot", &why);
        chk(!ok && !lootGunTakesParts(*a.weaponDefs[launcher]), "a " + a.weaponDefs[launcher]->name + " takes no fittings (" + why + ")"); }
      return done();
    }
    phase = 1; t = 0; return true;
  }
  // capture: outside view of the table in the workshop, then the fitting screen
  const Instance* ti = a.doc.find(table);
  if (!ti) return done();
  quat q = yawQuat(ti->yaw);
  if (phase == 1) {
    if (at(0.1f)) look(a, ti->pos + q * vec3(1.6f, 1.75f, 2.6f), ti->pos + vec3(0, 0.9f, 0));
    if (at(1.2f)) pend = out("table");
    if (t > 1.5f) {
      a.enterExplore(); a.useScripted = true;
      int rifle = -1; for (const char* id : {"modern_m4_irons", "modern_ak", "stg44", "m1_garand"}) if ((rifle = a.weaponIndex(id)) >= 0 && lootGunTakesParts(*a.weaponDefs[rifle])) break;
      std::string msg; if (rifle >= 0) a.equipWeapon(rifle, msg);
      if (rifle >= 0) {
        for (const char* id : {"br_sight_acog", "br_suppressor", "br_mag_ext", "br_sight_reddot", "br_muzzle_brake", "br_recoil_pad"}) kit.items[id] = 1;
        smithyApply(a, rifle, ATT_SIGHT, "br_sight_acog"); smithyApply(a, rifle, ATT_MUZZLE, "br_suppressor"); smithyApply(a, rifle, ATT_MAG, "br_mag_ext");
      }
      Physics::setCharacterPos(ti->pos + q * vec3(0, 0, 1.2f));
      phase = 2; t = 0;
    }
    return true;
  }
  if (phase == 2) {
    a.scriptedInput = ExploreInput{};
    if (at(0.4f)) { smithyOpenAt(a, table); smithyTestHover(3); }   // the "after" column for [4] Fit Muzzle brake
    if (at(1.6f)) { logf("[sm1] fitting screen open: %s", smithyOpen() ? "yes" : "NO"); a.uiShot = out("fitting"); }   // with the UI (screenshot() is the scene only)
    if (t > 2.0f) return done();
  }
  return true;
}
