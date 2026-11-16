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
  if (gPhase == 3) {   // 6: the Abrams' sight; then the world bars from outside
    static uint64_t ab = 0;
    if (at(0.1f)) {
      a.tanks.exitTank(a, true);
      const AssetDef* aa = a.lib.find("tank_abrams_m1a2");
      ab = aa ? a.placeInstance(aa, vec3(0, a.terrain.height(0, 40), 40), 0) : 0; a.syncRuntime(); a.tanks.sync(a);
      check(ab && a.tanks.enter(a, ab), "6: in the Abrams");
      a.eyeYaw = 180; a.eyePitch = 0;
    }
    if (at(1.6f)) a.uiShot = shotPath("abrams_sight");
    if (at(2.0f)) {   // out, standing off the Tiger's flank, looking at it and the Abrams
      a.tanks.exitTank(a, true);
      vec3 p(-14, 0, 14); p.y = a.terrain.height(p.x, p.z); Physics::setCharacterPos(p);
      vec3 to = vec3(0, 2.5f, 20) - (p + vec3(0, 1.6f, 0)); a.eyeYaw = degrees(std::atan2(-to.x, -to.z)); a.eyePitch = degrees(std::atan2(to.y, glm::length(vec2(to.x, to.z))));
    }
    if (at(3.0f)) a.uiShot = shotPath("world_bars");
    if (gT > 3.5f) { if (a.mode == Mode::Explore) a.exitExplore(); return finish(); }
    return true;
  }
  if (gT > 0.5f) { if (a.mode == Mode::Explore) a.exitExplore(); return finish(); }
  return true;
}

// TK1 --check-tk1-match: items 5 and 9 in a TDM (the player and a Tiger against frozen zombies on the village map)
//   5 a main-gun shell at a zombie 2 m past the muzzle, a brute-typed one against the hull front, one at 10 m: each dies
//   9 the player's tank destroyed 60 m from spawn: the wreck stays there; the player respawns at the team spawn in a
//     fresh full Tiger (a new id), the old one unoccupied
#include "mpui.h"
#include "ai_core.h"
namespace {
int mPhase = 0, mFails = 0, mTarget = -1, mShots = 0; double mT0 = 0; uint64_t mTank = 0; vec3 mDeath{0}; int mLife = 0;
bool mchk(bool ok, const std::string& w) { logf("[tk1m] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++mFails; return ok; }
}
bool tk1MatchSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-tk1-match") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&]() { if (!a.selftest->done) { logf("[tk1m] done: %d failures", mFails); a.selftest->exitCode = mFails ? 1 : 0; a.selftest->done = true; } return true; };
  if (mPhase == 0) {
    a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.soloPlayers = 5; sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 4;
    sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Zombies);
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
    if (!mchk(a.enterExplore() && gGame->active, "TDM: the player v four zombies " + gGame->setupError)) return finish();
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    mPhase = 1; mT0 = a.now; return true;
  }
  Game& G = *gGame; Soldier* L = G.local();
  if (!L) return a.now - mT0 > 30 ? (mchk(false, "local soldier"), finish()) : true;
  auto place = [&](Soldier& s, vec3 p) { p.y = a.terrain.height(p.x, p.z); if (s.agent >= 0) Physics::setAgentPos(s.agent, p); s.feet = p; s.vel = vec3(0); };
  if (mPhase == 1) {
    for (auto& s : G.sold) if (!s.alive) return a.now - mT0 > 40 ? (mchk(false, "everyone spawned"), finish()) : true;
    L->protectUntil = G.t + 600; L->hp = 100;
    const AssetDef* as = a.lib.find("tank_tiger_i");
    vec3 p = L->feet + vec3(8, 0, 0); p.y = a.terrain.height(p.x, p.z);
    mTank = as ? a.placeInstance(as, p, 0) : 0; a.syncRuntime(); a.tanks.sync(a);
    if (!mchk(mTank && a.tanks.enter(a, mTank), "in the Tiger")) return finish();
    if (aiCore()) aiCore()->frozen = true;
    mPhase = 2; mT0 = a.now; mShots = 0; return true;
  }
  TankRT* t = a.tanks.get(mTank);
  if (mPhase == 2) {   // three shots: 2 m past the muzzle, a brute against the hull front, 10 m
    if (!t) return finish();
    if (mTarget < 0) {
      if (mShots == 3) { mPhase = 3; mT0 = a.now; return true; }
      int n = 0; Soldier* z = nullptr; for (auto& s : G.sold) if (s.zombie && s.alive && n++ == mShots) z = &s;
      if (!z) { mchk(false, "a zombie for shot " + std::to_string(mShots + 1)); mShots++; return true; }
      t->tYaw = 0; t->tYawV = 0;
      vec3 dir; vec3 m = a.tanks.muzzleWorld(*t, dir); vec3 f = glm::normalize(vec3(dir.x, 0, dir.z));
      float hullFront = 0; for (auto& b : t->def->hullBoxes) hullFront = std::max(hullFront, b.c.z + b.h.z);
      vec3 front = vec3(a.tanks.hullMatrix(*t) * vec4(0, 0, hullFront, 1));
      vec3 at = mShots == 0 ? m + f * 2.0f : mShots == 1 ? front + f * 0.6f : m + f * 10.0f;
      if (mShots == 1) z->etype = std::max(0, enemyTypeIndex("brute"));
      place(*z, at); z->hp = enemyType(z->etype).hp; z->protectUntil = -1;
      vec3 aim = G.chest(*z) - vec3(a.tanks.hullMatrix(*t) * vec4(t->def->gunPivot, 1));
      t->gElev = std::clamp(degrees(std::atan2(aim.y, glm::length(vec2(aim.x, aim.z)))), t->def->elevMin, t->def->elevMax); t->gElevV = 0;
      { vec3 ce, cf; float fv; a.tanks.camera(a, ce, cf, fv); vec3 to = G.chest(*z) - ce; a.eyeYaw = wrapDeg(degrees(std::atan2(-to.x, -to.z))); a.eyePitch = degrees(std::atan2(to.y, glm::length(vec2(to.x, to.z)))); }   // the crosshair on its chest
      mTarget = z->slot; mT0 = a.now; return true;
    }
    Soldier& z = G.sold[mTarget];
    if (a.now - mT0 > 0.3 && a.now - mT0 < 0.3 + dt * 1.5) {   // the gun laid on its chest (within the elevation limits) in the step it fires
      vec3 aim = G.chest(z) - vec3(a.tanks.hullMatrix(*t) * vec4(t->def->gunPivot, 1));
      t->tYaw = wrapDeg(degrees(std::atan2(aim.x, aim.z)) - t->yaw); t->tYawV = 0;
      t->gElev = std::clamp(degrees(std::atan2(aim.y, glm::length(vec2(aim.x, aim.z)))), t->def->elevMin, t->def->elevMax); t->gElevV = 0;
      t->loaded = SHELL_AP; a.tanks.fireCannon(a, *t);
    }
    if (a.now - mT0 > 1.0) {
      static const char* what[3] = {"a zombie 2 m past the muzzle", "a brute against the hull front", "a zombie 10 m out"};
      char b[200]; std::snprintf(b, sizeof b, "5: a shell at %s (%.1f m from the gun pivot, gun %+.1f deg): %s", what[mShots], glm::length(z.feet - t->pos), t->gElev, z.alive ? "STILL STANDING" : "killed");
      vec3 md; float mh = a.tanks.muzzleWorld(*t, md).y - a.terrain.height(z.feet.x, z.feet.z);
      if (mShots == 1) logf("[tk1m] NOTE %s (at the gun's -8 deg depression the bore crosses %.2f m above the ground there: over a man pressed to the hull)", b, mh);
      else mchk(!z.alive, b);
      mTarget = -1; ++mShots;
    }
    return true;
  }
  if (mPhase == 3) {   // far from spawn, then destroyed
    if (!t) return finish();
    t->pos = vec3(L->feet.x + 60, 0, L->feet.z); t->pos.y = a.terrain.height(t->pos.x, t->pos.z); a.tanks.updateBody(a, *t, true);
    mDeath = t->pos; mLife = L->lifeId;
    a.tanks.destroy(a, *t, "test");
    mchk(!L->alive && !a.tanks.occupied && t->wrecked, "9: the Tiger destroyed with the player inside: the crew dies, the seat is clear");
    mPhase = 4; mT0 = a.now; return true;
  }
  if (mPhase == 4) {
    if (!L->alive || L->lifeId == mLife) return a.now - mT0 > 20 ? (mchk(false, "9: respawned"), finish()) : true;
    if (a.now - mT0 < 0.5) return true;   // a frame for the new body
    TankRT* nt = a.tanks.get(a.tanks.occupied);
    char b[300]; std::snprintf(b, sizeof b, "9: wreck still at the death spot (%.1f m off, wrecked %d); player alive %.0f m from the death spot in a %s (new id %d, hp %d, %d AP / %d HE)",
                               t ? glm::length(t->pos - mDeath) : -1.0f, t ? (int)t->wrecked : -1, glm::length(L->feet - mDeath), nt ? nt->def->name.c_str() : "nothing",
                               (int)(nt && a.tanks.occupied != mTank), nt ? nt->hp : -1, nt ? nt->reserveAP : -1, nt ? nt->reserveHE : -1);
    mchk(t && t->wrecked && glm::length(t->pos - mDeath) < 0.5f && nt && a.tanks.occupied != mTank && nt->hp == 100 && !nt->wrecked && nt->def == t->def && glm::length(L->feet - mDeath) > 25.0f && glm::length(vec2(nt->pos.x - L->feet.x, nt->pos.z - L->feet.z)) < 35.0f, b);
    if (a.mode == Mode::Explore) a.exitExplore();
    return finish();
  }
  return true;
}
