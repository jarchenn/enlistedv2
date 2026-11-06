// --selftest-arsenal: faction racks (registry-built menus, interaction, input suppression, range, equip /
// ammunition policy), fire-mode selector, launchers (reload lifecycle, building damage), AT-gun one-shot
// destruction (direct hit vs near miss, operator release, idempotence, reset / Apply), carried M2 deploy / pack up.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> aseq; size_t acur = 0; float at_ = 0;
uint64_t RA = 0, RX = 0, G1 = 0, G2 = 0, G3 = 0, G4 = 0, G5 = 0, WALL = 0;
const WeaponDef& D(App& a) { return *a.weaponDefs[a.activeWeapon]; }
WeaponState& S(App& a) { return a.wstate[D(a).id]; }
void lookAt(App& a, vec3 p) { vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
void standAt(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z), p.z)); }
vec3 gunCentre(App& a, uint64_t id) { Instance* i = a.doc.find(id); auto& r = a.rt[id]; return vec3(a.stationMatrix(*i, r) * vec4((r.asset->bmin + r.asset->bmax) * 0.5f, 1)); }
}

void SelfTest::arsenalUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { aseq.push_back({n, f}); };
  if (stage == 1400) {
    reportName = "arsenal";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Arsenal range");
    RA = app.placeInstance(app.lib.find("rack_allied"), vec3(0, 0, -4), 180);
    RX = app.placeInstance(app.lib.find("rack_axis"), vec3(8, 0, -4), 180);
    WALL = app.placeInstance(app.lib.find("small_stone_dairy"), vec3(0, 0, -40), 0);
    G1 = app.placeInstance(app.lib.find("gun_pak40"), vec3(-20, 0, -25), 0);
    G2 = app.placeInstance(app.lib.find("gun_pak40"), vec3(-28, 0, -25), 0);
    G3 = app.placeInstance(app.lib.find("gun_pak40"), vec3(-36, 0, -25), 0);
    G4 = app.placeInstance(app.lib.find("gun_pak40"), vec3(-44, 0, -25), 0);
    G5 = app.placeInstance(app.lib.find("gun_pak40"), vec3(-52, 0, -25), 0);
    check(RA && RX && WALL && G1 && G2 && G3 && G4 && G5, "range: Allied and Axis racks, a stone target building, five PaK 40s");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 2), 2), 180};
    check(app.enterExplore(), "F5"); app.useScripted = true; app.vm.procedural = false;
    aseq.clear(); acur = 0; at_ = 0;

    push("registry", [this](App& a, float) {
      auto names = [&](const std::string& f) { std::string s; for (int k : a.rackArsenal(f)) s += a.weaponDefs[k]->id + " "; return s; };
      std::string us = names("US"), de = names("DE");
      auto has = [](const std::string& l, const char* id) { return l.find(std::string(id) + " ") != std::string::npos; };
      bool okUS = has(us, "m1_garand") && has(us, "m1c_garand") && has(us, "m1_carbine") && has(us, "m1_thompson") && has(us, "m1918a2_bar") && has(us, "bazooka_m9") && has(us, "m2hb_carry");
      bool okDE = has(de, "kar98k") && has(de, "kar98k_zf39") && has(de, "mp40") && has(de, "mg42") && has(de, "fg42") && has(de, "panzerschreck");
      check(okUS, "Allied rack lists the whole US registry: " + us);
      check(okDE, "Axis rack lists the whole German registry: " + de);
      const AssetDef* ra = a.lib.find("rack_allied"); const AssetDef* rx = a.lib.find("rack_axis");
      check(ra->rackDisplay.size() >= 5 && rx->rackDisplay.size() >= 5, "each rack displays several real weapon models (" + std::to_string(ra->rackDisplay.size()) + " Allied, " + std::to_string(rx->rackDisplay.size()) + " Axis)");
      check(ra->rackDeploy.size() == 1 && rx->rackDeploy.size() == 1 && rx->rackDeploy[0] == "gun_pak40", "stationary guns listed as deployable equipment (M2HB tripod, PaK 40)");
      int smallArms = 0; for (auto& d : a.weaponDefs) smallArms += a.isSmallArm(*d);
      check((int)a.carried.size() == smallArms, "sandbox loadout carries every small arm (" + std::to_string(a.carried.size()) + "), launchers and the M2 come from racks");
      return true; });
    push("rack screenshot", [this](App& a, float t) {
      if (t == 0) { standAt(a, vec3(3.5f, 0, 0.5f)); }
      Instance* r = a.doc.find(RA); lookAt(a, r->pos + vec3(0.5f, 0.7f, 0));
      if (t > 0.5f && t < 0.52f) shotQueue.push_back("arsenal_rack_allied");
      Instance* x = a.doc.find(RX);
      if (t > 0.6f) { standAt(a, x->pos + vec3(3.0f, 0, 4.0f)); lookAt(a, x->pos + vec3(0.4f, 0.7f, 0)); }
      if (t > 1.1f && t < 1.12f) shotQueue.push_back("arsenal_rack_axis");
      return t > 1.2f; });
    // ---------------------------------------------------------------- rack interaction
    push("rack prompt + open", [this](App& a, float t) {
      Instance* r = a.doc.find(RA);
      if (t == 0) { standAt(a, r->pos + vec3(0, 0, 1.6f)); return false; }
      lookAt(a, r->pos + vec3(0, 0.8f, 0.2f));
      if (t < 0.3f) return false;
      if (t < 0.32f) { check(a.target.valid && a.target.kind == 3 && a.target.inst == RA, "looking at the rack within reach shows 'E - Open Allied weapon rack'"); a.scriptedInput.interact = true; return false; }
      if (t < 0.5f) return false;
      check(a.rackOpen && a.rackId == RA, "E opens the faction panel");
      return true; });
    push("panel suppresses input", [this](App& a, float t) {
      static int s0; static vec3 p0; static float yaw0;
      if (t == 0) { s0 = a.shotsFired; p0 = Physics::character().pos; yaw0 = a.eyeYaw; }
      a.scriptedInput.fire = true; a.scriptedInput.move = vec2(0, 1); a.scriptedInput.dyaw = 0.5f;
      if (t < 0.6f) return false;
      a.scriptedInput = ExploreInput{};
      float moved = glm::length(Physics::character().pos - p0);
      check(a.shotsFired == s0 && moved < 0.02f && a.rackOpen, "with the panel open, held fire / move do not leak through (no shot, " + std::to_string(moved * 1000).substr(0, 4) + " mm moved)");
      (void)yaw0;
      return true; });
    push("equip carbine", [this](App& a, float t) {
      static int prev;
      if (t == 0) {
        prev = a.activeWeapon;
        auto ars = a.rackArsenal("US");
        for (size_t k = 0; k < ars.size(); ++k) if (a.weaponDefs[ars[k]]->id == "m1_carbine") a.rackSel = (int)k;
        a.scriptedInput.fire = true;   // trigger held through the swap
        a.scriptedInput.rackEquip = true;
        return false;
      }
      if (t < 0.4f) return false;
      auto& s = a.wstate["m1_carbine"];
      check(D(a).id == "m1_carbine" && !a.rackOpen && a.lastRackClose == "equipped", "Equip selects the M1 Carbine and closes the panel");
      check(s.loaded == 15 && s.reserveRounds() == 60, "held trigger through the swap: no free shot (" + std::to_string(s.loaded) + " loaded)");
      a.scriptedInput.fire = false;
      (void)prev;
      return true; });
    push("swap does not duplicate", [this](App& a, float t) {
      static int phase = 0;
      if (t == 0) phase = 0;
      auto& s = a.wstate["m1_carbine"];
      if (phase == 0) { a.scriptedInput.fire = !a.scriptedInput.fire; if (s.loaded <= 12) { a.scriptedInput.fire = false; phase = 1; } return t > 5; }
      if (phase == 1) {
        int l = s.loaded, rsv = s.reserveRounds();
        std::string m;
        a.equipWeapon(a.weaponIndex("bazooka_m9"), m); a.equipWeapon(a.weaponIndex("m1_carbine"), m);
        a.equipWeapon(a.weaponIndex("m1_thompson"), m); a.equipWeapon(a.weaponIndex("m1_carbine"), m);
        check(s.loaded == l && s.reserveRounds() == rsv, "rapid swapping keeps the carbine's own ammunition (" + std::to_string(l) + " + " + std::to_string(rsv) + ", no refill)");
        bool c = std::find(a.carried.begin(), a.carried.end(), a.weaponIndex("m1_carbine")) != a.carried.end();
        size_t n = a.carried.size();
        a.equipWeapon(a.weaponIndex("m1_carbine"), m);
        check(c && a.carried.size() == n, "equipping a carried weapon only selects it (no duplicate slot)");
        return true;
      }
      return false; });
    push("leave range closes", [this](App& a, float t) {
      Instance* r = a.doc.find(RA);
      if (t == 0) { standAt(a, r->pos + vec3(0, 0, 1.6f)); lookAt(a, r->pos + vec3(0, 0.8f, 0.2f)); a.openRack(RA); return false; }
      static bool moved = false;
      if (!moved) { moved = true; check(a.rackOpen, "panel open again"); standAt(a, r->pos + vec3(0, 0, 6)); return false; }
      if (t < 0.4f) return false;
      check(!a.rackOpen && a.lastRackClose == "left range", "walking out of range closes the panel safely");
      standAt(a, r->pos + vec3(0, 0, 1.6f)); a.openRack(RA); a.scriptedInput.rackClose = true;
      return true; });
    push("esc closes", [this](App& a, float t) {
      if (t < 0.2f) return false;
      check(!a.rackOpen && a.lastRackClose == "closed", "Esc / Close shuts the panel (mouse capture restored outside self-test)");
      a.openRack(RA); a.exitExplore();
      check(!a.rackOpen && a.lastRackClose == "mode change", "a mode transition closes it too");
      a.enterExplore(); a.useScripted = true; a.vm.procedural = false;
      return true; });
    // ---------------------------------------------------------------- fire modes
    push("selector", [this](App& a, float t) {
      static int phase = 0, s0 = 0; static float t0 = 0;
      if (t == 0) { phase = 0; standAt(a, vec3(0, 0, 2)); lookAt(a, vec3(0, 2, -40)); std::string m; a.equipWeapon(a.weaponIndex("m1_thompson"), m); }
      auto& s = S(a);
      if (phase == 0) { check(std::string(a.fireModeName(D(a), s)) == "AUTO", "Thompson starts AUTO"); if (t < 0.3f) return false; a.scriptedInput.fireMode = true; a.scriptedInput.fire = true; s0 = a.shotsFired; phase = 1; return false; }   // trigger pressed with the toggle
      if (phase == 1) { check(std::string(a.fireModeName(D(a), s)) == "SEMI", "one key-down edge switches to SEMI"); t0 = t; phase = 2; return false; }
      if (phase == 2) {   // trigger held from before the switch: release required
        if (t - t0 < 0.5f) return false;
        check(a.shotsFired == s0, "trigger held across the mode change: no shot until it is released");
        a.scriptedInput.fire = false; phase = 3; t0 = t; return false;
      }
      if (phase == 3) { a.scriptedInput.fire = true; s0 = a.shotsFired; t0 = t; phase = 4; return false; }
      if (phase == 4) {
        if (t - t0 < 1.0f) return false;
        check(a.shotsFired - s0 == 1, "SEMI: a held trigger fires exactly once in 1 s (" + std::to_string(a.shotsFired - s0) + ")");
        a.scriptedInput.fire = false; phase = 5; return false;
      }
      if (phase == 5) { a.scriptedInput.fireMode = true; a.scriptedInput.fire = false; phase = 6; return false; }
      if (phase == 6) { a.scriptedInput.fire = true; s0 = a.shotsFired; t0 = t; phase = 7; return false; }
      if (phase == 7) {
        if (t - t0 < 1.0f) return false;
        int n = a.shotsFired - s0, want = (int)std::round(D(a).rpm / 60.0f);
        a.scriptedInput.fire = false;
        check(std::string(a.fireModeName(D(a), s)) == "AUTO" && std::abs(n - want) <= 1, "AUTO: held for 1 s fires " + std::to_string(n) + " rounds at " + std::to_string((int)D(a).rpm) + " rpm (~" + std::to_string(want) + ")");
        phase = 8; return false;
      }
      if (phase == 8) { a.scriptedInput.reload = true; phase = 9; return false; }
      return t > 6; });
    push("selector at 10 fps", [this](App& a, float t) {
      static int s0;
      if (t == 0) { slowFrames = true; s0 = a.shotsFired; a.scriptedInput.fire = true; return false; }
      if (t < 1.0f) return false;
      slowFrames = false; a.scriptedInput.fire = false;
      int n = a.shotsFired - s0, want = (int)std::round(D(a).rpm / 60.0f);
      check(std::abs(n - want) <= 1, "AUTO cadence stable at 10 fps: " + std::to_string(n) + " rounds in 1 s");
      return true; });
    push("selector coverage", [this](App& a, float) {
      bool ok = true; std::string list;
      for (const char* id : {"m1_thompson", "mg42", "fg42", "mp40"}) { auto& d = *a.weaponDefs[a.weaponIndex(id)]; ok &= d.modes.size() == 2 && d.modes[0] == "auto"; list += std::string(id) + " "; }
      auto& c = *a.weaponDefs[a.weaponIndex("m1_carbine")]; auto& b = *a.weaponDefs[a.weaponIndex("bazooka_m9")];
      check(ok, "AUTO/SEMI selector on " + list + "(AUTO default)");
      check(c.modes.empty() && c.action == "semi" && std::string(a.fireModeName(b, a.wstate[b.id])) == "SINGLE", "M1 Carbine stays semi-auto, launchers stay single-shot");
      std::string m; a.equipWeapon(a.weaponIndex("m1_carbine"), m);
      a.scriptedInput.fireMode = true;
      return true; });
    push("selector on carbine", [this](App& a, float t) {
      if (t < 0.1f) return false;
      check(std::string(a.fireModeName(D(a), S(a))) == "SEMI" && a.weaponMsg.find("no fire selector") != std::string::npos, "toggle on the carbine changes nothing (message shown)");
      return true; });
    // ---------------------------------------------------------------- launchers
    push("bazooka vs building", [this](App& a, float t) {
      static int phase = 0; static size_t hits0;
      Instance* w = a.doc.find(WALL);
      static vec3 aimP, aimN;
      if (t == 0) {
        phase = 0; std::string m; a.equipWeapon(a.weaponIndex("bazooka_m9"), m);
        auto& wr = a.rt[WALL]; float area = 0;   // the largest wall panel (the dairy's front is door and window, not panels)
        for (auto& pn : wr.asset->panels) { vec3 e = pn.bmax - pn.bmin; float ar = std::max(e.x, e.z) * e.y; if (ar > area) { area = ar; aimP = w->pos + yawQuat(w->yaw) * ((pn.bmin + pn.bmax) * 0.5f); aimN = yawQuat(w->yaw) * pn.normal; } }
        standAt(a, aimP + aimN * 18.0f);
      }
      lookAt(a, aimP);
      auto& s = S(a);
      if (phase == 0 && t > 0.6f) { hits0 = a.lastExplosion.hits.size(); a.scriptedInput.fire = true; phase = 1; return false; }
      if (phase == 1) { a.scriptedInput.fire = false; if (!a.projectiles.empty() || t < 1.0f) return false;
        int dmg = 0; auto& r = a.rt[WALL]; for (size_t k = 0; k < r.hp.size(); ++k) if (r.hp[k] < r.asset->panels[k].hp) ++dmg;
        char b[200]; std::snprintf(b, sizeof b, " (%d panels; impact %.1f %.1f %.1f on %s)", dmg, a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, a.lastImpact.surface.c_str());
        check(dmg > 0 && !a.lastExplosion.hits.empty(), std::string("M9 rocket damages the stone building's wall panels through App::explode") + b);
        check(s.loaded == 0, "launcher is empty after its single shot");
        a.scriptedInput.fire = true; phase = 2; return false; }
      if (phase == 2) { int f0 = a.shotsFired; (void)f0; a.scriptedInput.fire = false; a.scriptedInput.reload = true; phase = 3; return false; }
      if (phase == 3) { if (s.reload > 0) return false; if (t < 3) return false;
        check(s.loaded == 1 && s.reserveRounds() == 4, "reload loads one rocket from the reserve (1 + 4)");
        return true; }
      return t > 15; });
    // ---------------------------------------------------------------- AT-gun destruction
    auto fireAtGun = [this](uint64_t gun, const char* weapon, bool nearMiss, const char* label) {
      return [this, gun, weapon, nearMiss, label](App& a, float t) {
        static int w0, h0, phase;
        if (t == 0) { phase = 0; w0 = a.stationsWrecked; h0 = a.directHits; std::string m; a.equipWeapon(a.weaponIndex(weapon), m); S(a).loaded = 1; S(a).cooldown = 0; S(a).reload = 0;
          vec3 g = gunCentre(a, gun); standAt(a, g + vec3(0, 0, 18)); }
        vec3 g = gunCentre(a, gun);
        lookAt(a, nearMiss ? vec3(g.x - 1.5f, a.terrain.height(g.x - 1.5f, g.z + 0.3f), g.z + 0.3f) : g);
        if (phase == 0 && t > 0.6f && nearMiss) {   // a Panzerschreck rocket landing 1.4 m beside the gun (outside its hit volume)
          vec3 gp = a.rt[gun].st.pos; vec3 tgt(gp.x - 1.4f, a.terrain.height(gp.x - 1.4f, gp.z + 0.2f), gp.z + 0.2f);
          const WeaponDef& w = *a.weaponDefs[a.weaponIndex(weapon)];
          a.fireProjectile(tgt + vec3(0, 6, 0), vec3(0, -1, 0), w.velocity, PROJ_ROCKET, w.rocketDmg, ~0ull, false, w.container);
          Projectile& rp = a.projectiles.back(); rp.blastDmg = w.rocketDmg; rp.blastR = w.rocketR; rp.craterScale = w.rocketCrater; rp.splashGun = w.rocketSplashGun; rp.atKill = true;
          phase = 1; return false;
        }
        if (phase == 0 && t > 0.6f) { a.scriptedInput.fire = true; phase = 1; return false; }
        if (phase == 1) {
          a.scriptedInput.fire = false;
          if (!a.projectiles.empty() || t < 1.0f) return t > 8;
          auto& st = a.rt[gun].st;
          char nb[160]; std::snprintf(nb, sizeof nb, " impact %.1f %.1f %.1f (%.2f m from the gun)", a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, glm::length(a.lastImpact.pos - (st.pos + vec3(0, 0.8f, 0))));
          if (nearMiss) check(!st.wrecked && st.hp < 100 && a.directHits == h0, std::string(label) + ": not a direct hit, splash only (HP " + std::to_string((int)st.hp) + "/100)" + nb);
          else { char b[200]; std::snprintf(b, sizeof b, " (impact %.1f %.1f %.1f on %s, gun at %.1f %.1f %.1f)", a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, a.lastImpact.surface.c_str(), g.x, g.y, g.z);
            check(st.wrecked && a.stationsWrecked == w0 + 1 && a.directHits == h0 + 1, std::string(label) + ": one direct hit destroys the PaK 40" + b); }
          return true;
        }
        return t > 8; };
    };
    push("bazooka direct", fireAtGun(G1, "bazooka_m9", false, "M9 bazooka rocket"));
    push("panzerschreck direct", fireAtGun(G2, "panzerschreck", false, "Panzerschreck rocket"));
    push("near miss", fireAtGun(G4, "panzerschreck", true, "Panzerschreck rocket landing 1.4 m beside the gun"));
    push("at shell direct, crewed gun", [this](App& a, float t) {
      static int phase;
      if (t == 0) {
        phase = 0;
        a.occupyStation(G3, ST_GUNNER);
        check(a.occupied == G3, "player crews PaK 40 #3");
        vec3 g = gunCentre(a, G3);
        // another AT gun's shell (PaK HE, atKill) arriving from downrange
        a.fireProjectile(g + vec3(0, 0.2f, -30), vec3(0, -0.006f, 1), 790, PROJ_HE, 120, ~0ull, false, "75mm_ap");
        a.projectiles.back().at = true; a.projectiles.back().atKill = true;
        return false;
      }
      if (!a.projectiles.empty() && t < 3) return false;
      check(a.rt[G3].st.wrecked && a.occupied == 0, "AT-gun shell direct hit destroys the crewed gun and safely releases the operator");
      int w = a.stationsWrecked;
      a.wreckStation(G3, "repeat"); a.wreckStation(G3, "repeat");
      check(a.stationsWrecked == w, "the destruction transition is idempotent (repeat events, overlapping colliders)");
      check(!a.occupyStation(G3, ST_GUNNER), "a wreck cannot be crewed");
      standAt(a, gunCentre(a, G3) + vec3(0, 0, 1.6f)); lookAt(a, gunCentre(a, G3));
      return true; });
    push("wreck not interactable", [this](App& a, float t) {
      if (t < 0.2f) return false;
      check(!(a.target.valid && a.target.inst == G3), "looking at the wreck offers no prompt");
      shotQueue.push_back("arsenal_pak_wreck");
      return true; });
    push("reset and apply", [this](App& a, float t) {
      if (t < 0.3f) return false;
      a.exitExplore();
      check(!a.rt[G1].st.wrecked && !a.rt[G2].st.wrecked, "returning to Creative restores the guns (session overlay)");
      a.enterExplore(); a.useScripted = true;
      a.wreckStation(G5, "test");
      a.applyDamageToMap(); a.exitExplore();
      check(a.rt[G5].st.wrecked, "Apply session changes keeps a destroyed gun through the reset");
      a.undo(); a.syncRuntime(); a.resetCombat();
      check(!a.rt[G5].st.wrecked, "Ctrl+Z restores it");
      a.enterExplore(); a.useScripted = true; a.vm.procedural = false;
      return true; });
    // ---------------------------------------------------------------- carried M2
    push("m2 deploy", [this](App& a, float t) {
      static int phase;
      if (t == 0) { phase = 0; standAt(a, vec3(-10, 0, 10)); a.eyeYaw = 0; a.eyePitch = 0; std::string m; check(a.equipWeapon(a.weaponIndex("m2hb_carry"), m), "Allied rack equips the carried M2HB"); return false; }
      if (phase == 0 && t > 0.3f) { check(D(a).id == "m2hb_carry" && a.adsT == 0, "carried M2: no sights, no firing"); a.scriptedInput.fire = true; phase = 1; return false; }
      if (phase == 1 && t > 0.6f) {
        a.scriptedInput.fire = false;
        check(a.occupied != 0 && a.rt[a.occupied].asset->station.kind == "m2hb" && a.deploys == 1, "LMB deploys it on its tripod ahead and crews it (" + a.weaponMsg + ")");
        StationRT* s = a.station(a.occupied);
        check(s && s->loaded == 110 && s->reserve == 110, "the gun keeps the carried belts (110 + 110)");
        std::find(a.carried.begin(), a.carried.end(), a.weaponIndex("m2hb_carry")) == a.carried.end() ? check(true, "the M2 leaves the carried loadout while deployed") : check(false, "M2 still carried after deploying");
        a.scriptedInput.fireMode = true; phase = 2; return false;
      }
      if (phase == 2 && t > 0.8f) {
        StationRT* s = a.station(a.occupied);
        check(s && s->semi, "M2HB selector: SEMI");
        a.scriptedInput.fire = true; phase = 3; return false;
      }
      if (phase == 3 && t > 1.8f) {
        StationRT* s = a.station(a.occupied);
        check(s && s->loaded == 109, "SEMI on the M2: a held trigger fires one round (" + std::to_string(s ? s->loaded : -1) + " left)");
        a.scriptedInput.fire = false; a.scriptedInput.interact = true; phase = 4; return false;
      }
      if (phase == 4 && t > 2.0f) { a.scriptedInput.packUp = true; phase = 5; return false; }
      if (phase == 5 && t > 2.3f) {
        auto& s = a.wstate["m2hb_carry"];
        check(D(a).id == "m2hb_carry" && s.loaded == 109 && s.reserveRounds() == 110 && a.packs == 1, "G packs it up again with the rounds it had (109 + 110)");
        return true;
      }
      return t > 6; });
    push("reset", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });
  }
  stage = 1401;
  if (acur >= aseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); finish(app); return; }
  if (at_ == 0) logf("[selftest] step: %s", aseq[acur].name.c_str());
  bool d2 = aseq[acur].fn(app, at_);
  at_ += dt;
  if (at_ > 60) { check(false, "step timed out: " + aseq[acur].name); d2 = true; }
  if (d2) { ++acur; at_ = 0; }
}
