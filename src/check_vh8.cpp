// VH8 --check-vh8: the Bradley's 25 mm HE radius, the coax / TOW sound events, every vehicle gun's event exists, and a Tiger
// driven into a wrecked jeep pushes it at least 5 m
#include "selftest.h"

namespace {
int phase = 0, fails = 0; float t = 0; uint64_t tiger = 0, jeep = 0; vec3 jeep0{0};
bool chk(bool ok, const std::string& w) { logf("[vh8] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; return ok; }
}

bool vh8SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-vh8") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  t += dt;
  auto done = [&]() { if (a.mode == Mode::Explore) a.exitExplore(); logf("[vh8] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; return true; };
  if (phase == 0) {
    auto it = a.tanks.defs.find("bradley_m2");
    if (!chk(it != a.tanks.defs.end(), "Bradley loaded")) return done();
    chk(std::fabs(it->second->heBlastR - 0.8f) < 1e-4f, "25 mm HE blast radius 0.8 m (was 1.6)");
    std::string missing;
    for (auto& [id, d] : a.tanks.defs) {
      auto need = [&](const std::string& ev, const char* what) { if (!ev.empty() && !a.audio.hasSound(ev)) missing += " " + id + "." + what + "=" + ev; };
      if (!d->mgName.empty()) need(d->mgSound, "roofMg");
      if (d->bowOn) need(d->bowSound, "bowMg");
      if (!d->gunSound.empty()) need(d->gunSound, "gun");
      if (d->heli) need(d->gun30Sound, "chinGun");
    }
    for (auto& [id, d] : a.tanks.defs) {   // roof MG geometry (vehicle space): the part bounds the muzzle must sit at
      if (d->car || d->heli || !d->model || d->mgName.empty()) continue;
      auto bb = [&](const std::vector<int>& ps) { vec3 lo(1e9f), hi(-1e9f); for (int k : ps) { lo = glm::min(lo, d->model->parts[k].bmin); hi = glm::max(hi, d->model->parts[k].bmax); } char b[120]; std::snprintf(b, sizeof b, "[%.2f %.2f %.2f]..[%.2f %.2f %.2f]", lo.x, lo.y, lo.z, hi.x, hi.y, hi.z); return ps.empty() ? std::string("none") : std::string(b); };
      logf("[vh8] geo %s mgParts %s gunParts %s mgPivot %.2f %.2f %.2f mgMuzzle %.2f %.2f %.2f gunMuzzle %.2f %.2f %.2f weapon '%s'", id.c_str(), bb(d->mgParts).c_str(), bb(d->gunParts).c_str(),
           d->mgPivot.x, d->mgPivot.y, d->mgPivot.z, d->mgMuzzle.x, d->mgMuzzle.y, d->mgMuzzle.z, d->muzzle.x, d->muzzle.y, d->muzzle.z, d->mgWeapon.c_str());
    }
    chk(missing.empty(), "every vehicle gun names an existing sound event" + (missing.empty() ? std::string() : ":" + missing));
    a.newMap(*a.findTemplate("blank_slate"), "VH8 check");
    uint64_t b = a.placeInstance(a.lib.find("tank_bradley_m2"), vec3(-30, a.terrain.height(-30, 0), 0), 0);
    tiger = a.placeInstance(a.lib.find("tank_tiger_i"), vec3(0, a.terrain.height(0, 0), 0), 0);
    jeep = a.placeInstance(a.lib.find("car_jeep_willys"), vec3(0.4f, a.terrain.height(0.4f, 7.5f), 7.5f), 90);
    a.syncRuntime(); a.tanks.sync(a); a.enterExplore(); a.useScripted = true;
    TankRT* bt = a.tanks.get(b);
    if (bt) {
      a.tanks.enter(a, b);
      a.tanks.fireBow(a, *bt); std::string s1 = a.audio.lastPlayed;
      a.tanks.fireTow(a, *bt); std::string s2 = a.audio.lastPlayed;
      chk(s1 == "shot_modern_lmg" && a.audio.hasEvent(s1), "the Bradley's coax plays " + s1);
      chk(s2 == "shot_modern_rpg7" && a.audio.hasEvent(s2), "the TOW plays " + s2);
      a.tanks.exitTank(a, true);
    }
    {   // VH8 Apache small arms / 40 mm grenade on armour: hits to destroy a fresh one each (placed apart, out of the way)
      float px = 40;
      auto hitsTo = [&](const char* asset, int attack, vec3 off, vec3 n, float J, float mult) {
        px += 15; uint64_t id = a.placeInstance(a.lib.find(asset), vec3(px, a.terrain.height(px, 60), 60), 0); a.syncRuntime(); a.tanks.sync(a);
        TankRT* v = a.tanks.get(id); if (!v) return -1;
        int k = 0; while (!v->wrecked && k < 2000) { a.tanks.applyHit(a, id, attack, v->pos + off, n, -n, J, 0, mult); ++k; }
        return v->wrecked ? k : -1;
      };
      vec3 side(1.4f, 1.3f, 0), sn(1, 0, 0), front(0, 1.3f, 2.8f), fn(0, 0, 1);
      int rifle = hitsTo("heli_ah64d_apache", TA_SMALL_ARMS, side, sn, 3697, 1), fifty = hitsTo("heli_ah64d_apache", TA_SMALL_ARMS, side, sn, 16634, 1),
          ak = hitsTo("heli_ah64d_apache", TA_SMALL_ARMS, side, sn, 1377, 1), pistol = hitsTo("heli_ah64d_apache", TA_SMALL_ARMS, side, sn, 562, 1);
      char b[260]; std::snprintf(b, sizeof b, "Apache destroyed by small arms: %d Kar98k, %d .50, %d AK, %d pistol hits (want 60-80 rifle, 25-30 .50)", rifle, fifty, ak, pistol);
      chk(rifle >= 60 && rifle <= 80 && fifty >= 25 && fifty <= 30 && pistol > rifle, b);
      int tg0 = hitsTo("tank_tiger_i", TA_LAUNCHER, side, sn, 0, 1), tg = hitsTo("tank_tiger_i", TA_LAUNCHER, side, sn, 0, 0.2f),
          bf0 = hitsTo("tank_bradley_m2", TA_LAUNCHER, front, fn, 0, 1), bf = hitsTo("tank_bradley_m2", TA_LAUNCHER, front, fn, 0, 0.2f),
          bs0 = hitsTo("tank_bradley_m2", TA_LAUNCHER, side, sn, 0, 1), bs = hitsTo("tank_bradley_m2", TA_LAUNCHER, side, sn, 0, 0.2f);
      std::snprintf(b, sizeof b, "40 mm grenades to kill (full -> 0.2): Tiger side %d -> %d, Bradley front %d -> %d, Bradley side %d -> %d", tg0, tg, bf0, bf, bs0, bs);
      chk(tg0 > 0 && tg >= 4 * tg0 && bf >= 4 * bf0 && bs >= 4 * bs0, b);
      int gi = a.weaponIndex("modern_glauncher");
      chk(gi >= 0 && std::fabs(a.weaponDefs[gi]->rocketArmour - 0.2f) < 1e-4f, "the six-shot grenade launcher carries armourMult 0.2");
    }
    TankRT* jt = a.tanks.get(jeep);
    if (!chk(jt && a.tanks.get(tiger), "Tiger and jeep placed")) return done();
    a.tanks.destroy(a, *jt, "test wreck"); jeep0 = jt->pos;
    a.tanks.enter(a, tiger);
    phase = 1; t = 0; return true;
  }
  if (phase == 1) {   // full ahead into the wreck
    ExploreInput& in = a.scriptedInput; in = ExploreInput{}; in.move.y = 1.0f;
    if (t > 9.0f) {
      TankRT* jt = a.tanks.get(jeep); TankRT* tt = a.tanks.get(tiger);
      float moved = jt ? glm::length(vec2(jt->pos.x - jeep0.x, jt->pos.z - jeep0.z)) : 0;
      char b[200]; std::snprintf(b, sizeof b, "a Tiger driven into a wrecked jeep pushed it %.1f m (Tiger now at z %.1f, blocked '%s')", moved, tt ? tt->pos.z : 0.0f, tt ? tt->blockedBy.c_str() : "");
      chk(moved >= 5.0f, b);
      in = ExploreInput{};
      return done();
    }
    return true;
  }
  return true;
}
