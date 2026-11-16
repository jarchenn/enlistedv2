// TK1 --capture-tk1: no suite, one capture and a short headless check per item (numbers in the log, [tk1] lines):
//   1 a Tiger fires with the turret ahead and broadside: the hull's peak rock, the shove and when it settles; the frame
//     at the first shot's peak (gunner view, the durability bar flashing from a blow just before)
//   2 small-arms hits change nothing; 14 heavy blows leave 2, the 15th destroys a parked Sherman
//   3 the bar is in that frame (drawn for every vehicle by TankSys::drawUI)
//   4 armour / heavy targets, the carried launcher and the weapon switch (Game helpers the bot brain uses)
#include "selftest.h"
#include "game.h"
#include "enemy_types.h"

namespace {
uint64_t gT1 = 0, gT2 = 0; float gT = 0; int gPhase = 0, gShot = 0;
float peakP = 0, peakR = 0, peakS = 0, settleAt = -1;
fs::path shotPath(const char* tag) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / (std::string("tk1_") + tag + ".png"); }
}

bool captureTk1SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-tk1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  static int fails = 0;
  auto check = [&](bool ok, const std::string& what) { logf("[tk1] %s %s", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++fails; };
  auto finish = [&]() { if (!a.selftest->done) { logf("[tk1] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; } return true; };
  float t0 = gT; gT += dt;
  auto at = [&](float m) { return t0 < m && gT >= m; };
  if (gPhase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "TK1 capture");
    const AssetDef* tg = a.lib.find("tank_tiger_i"); const AssetDef* sh = a.lib.find("tank_sherman_m4");
    if (!sh) sh = a.lib.find("tank_sherman_firefly");
    if (!tg || !sh) { check(false, "tank assets"); return finish(); }
    gT1 = a.placeInstance(tg, vec3(0, a.terrain.height(0, 0), 0), 0);
    gT2 = a.placeInstance(sh, vec3(-12, a.terrain.height(-12, -16), -16), 0);
    a.syncRuntime(); a.tanks.sync(a); a.enterExplore(); a.useScripted = true;
    gPhase = 1; gT = 0; return true;
  }
  TankRT* t = a.tanks.get(gT1); TankRT* s2 = a.tanks.get(gT2);
  if (!t || !s2) { check(false, "tank state"); return finish(); }
  if (gPhase == 1) {   // enter, then the shots
    if (gT < 0.3f) return true;
    if (!a.tanks.occupied) { check(a.tanks.enter(a, gT1), "enter the Tiger"); a.eyeYaw = 180; a.eyePitch = 0; gT = 0.31f; return true; }
    if (at(1.5f) || at(4.0f)) {   // 1: fire, turret ahead then broadside
      if (gShot == 1) t->tYaw = 90;
      a.tanks.meleeBlow(a, gT1, t->pos + vec3(0, 0, 5));   // 3: a blow first, so the bar flashes in the frame
      t->loaded = SHELL_AP; a.tanks.fireCannon(a, *t); peakP = peakR = peakS = 0; settleAt = -1; ++gShot;
    }
    float since = gT - (gShot == 1 ? 1.5f : 4.0f);
    if (gShot > 0 && since >= 0) {
      peakP = std::max(peakP, std::fabs(t->suspPitch)); peakR = std::max(peakR, std::fabs(t->suspRoll)); peakS = std::max(peakS, glm::length(t->shove));
      if (settleAt < 0 && since > 0.2f && std::fabs(t->suspPitch) < 0.05f && std::fabs(t->suspRoll) < 0.05f && glm::length(t->shove) < 0.002f) settleAt = since;
    }
    if (at(1.62f)) a.uiShot = shotPath("fire_rock_hud");   // ~0.14 s: the rock's peak (1 / omega)
    if (at(3.4f) || at(5.9f)) {
      char b[200]; std::snprintf(b, sizeof b, "1: shot %d (turret %+.0f deg): peak pitch %.2f deg, roll %.2f deg, shove %.3f m; settled (<0.05 deg, <2 mm) after %.2f s", gShot, gShot == 1 ? 0.0f : 90.0f, peakP, peakR, peakS, settleAt);
      check(settleAt > 0.5f && settleAt < 1.0f && (gShot == 1 ? peakP > 0.5f && peakR < 0.05f : peakR > peakP && peakR > 0.8f), b);
    }
    if (gT > 6.0f) { gPhase = 2; gT = 0; }
    return true;
  }
  if (gPhase == 2) {
    // 2: small arms on the parked Sherman: no damage; heavy blows 1/15 each
    s2->hp = 100; int hp0 = s2->hp;
    for (int k = 0; k < 20; ++k) a.tanks.applyHit(a, gT2, TA_SMALL_ARMS, s2->pos + vec3(0, 1.2f, 2), vec3(0, 0, 1), vec3(0, 0, -1), 20000);
    check(s2->hp == hp0 && !s2->wrecked, "2: 20 small-arms hits (incl. .50-class energy): durability " + std::to_string(s2->hp) + " (unchanged)");
    for (int k = 0; k < 14; ++k) a.tanks.meleeBlow(a, gT2, s2->pos + vec3(3, 0, 0));
    int hp14 = s2->hp; bool alive14 = !s2->wrecked;
    a.tanks.meleeBlow(a, gT2, s2->pos + vec3(3, 0, 0));
    check(alive14 && hp14 == 2 && s2->wrecked, "2: 14 heavy blows leave " + std::to_string(hp14) + ", the 15th destroys it (wrecked " + std::to_string((int)s2->wrecked) + ")");
    // 4: the targeting helpers
    if (!gGame) gGame = new Game(a);
    Game& G = *gGame;
    Soldier z; z.zombie = true; z.etype = std::max(0, enemyTypeIndex("brute")); Soldier z0; z0.zombie = true; z0.etype = 0;
    Soldier crew; crew.localPlayer = true;
    check(G.bigTarget(z) && !G.bigTarget(z0) && enemyType(z.etype).tankDamage && !enemyType(0).tankDamage, "4: the brute is a big target that can hurt tanks; the plain zombie is neither");
    check(a.tanks.occupied == gT1 && G.bulletProof(crew) && G.bigTarget(crew), "4: the Tiger's crew: bullet-proof and a rocket target");
    Soldier bot; int k98 = a.weaponIndex("kar98k"), ps = a.weaponIndex("panzerschreck");
    bot.carried = {k98, ps}; bot.weapon = k98; bot.ws.loaded = 5;
    check(G.carriedLauncher(bot) == ps, "4: the heavy's carried launcher is found");
    G.switchWeapon(bot, ps); bool toL = bot.weapon == ps && bot.ws.loaded == 1 && bot.wsBy.count(k98);
    G.switchWeapon(bot, k98); bool back = bot.weapon == k98 && bot.ws.loaded == 5 && bot.wsBy.count(ps);
    check(toL && back, "4: switch to the Panzerschreck (1 loaded) and back to the Kar98k (its 5 kept)");
    gPhase = 3; gT = 0; return true;
  }
  if (gT > 0.5f) { if (a.mode == Mode::Explore) a.exitExplore(); return finish(); }
  return true;
}
