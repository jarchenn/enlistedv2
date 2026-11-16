// --selftest-rules (W1, stages 2700+): gameplay rules of the MP & Creative update.
//  1. damage table (pure math, SPEC 6.2 + the gunplay overhaul): every row's shots to kill at 10/50/100/200 m torso and
//     10/100 m legs, the one zone table (head 1.3, torso / neck / arms 1.0, legs 0.6), the StG 44 2-shot / 3-shot
//     ranges, the [DECISION] row as written, the repo RPMs, a row for every bullet weapon, the formula;
//  2. movement data (SPEC 4): g x2.133, v0 = sqrt(2 g h), fall thresholds kept as heights;
//  3. jump on real physics: apex and airtime within 2 % of the target (h x1.2, t x0.75);
//  4. a match on a blank map: spawn protection lasts 3.0 s +- one frame, blocks bullets / blasts / falls (deflected
//     FleshHit, no damage), ends on firing and on throwing (and not when the option is off), shield icons / ring /
//     tint; after it: fall damage by height, a hit on the player flinches the aim, zones (upper / lower torso, leg,
//     head, helmet), true travelled range;
//     a respawn restarts protection, which the local player's own shot keeps (GM1: 5 s regardless of firing).
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "rules.h"
#include "damage_model.h"
#include "loot.h"
#include "classes.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <cmath>
#include <algorithm>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> rseq; size_t rcur = 0; float rt = 0;
const float kDt = 1.0f / 60.0f;
double gTSpawnSeen = -1;   // Game::t on the first frame the player is alive (the step that spawned it)

json rulesScenario() {
  Scenario s;
  Objective o; o.id = s.newId("obj"); o.label = "Crossroads"; o.order = 0; o.pos = vec3(0, 0, 0); o.radius = 15; o.attackYaw = 0; o.yMin = -2; o.yMax = 4;
  s.objectives.push_back(o);
  SpawnMarker a; a.id = s.newId("spawn"); a.role = CombatRole::Attacker; a.phases = {0}; a.pos = vec3(0, 0, 60); a.yaw = 0; a.label = "Attack"; s.spawns.push_back(a);
  SpawnMarker d; d.id = s.newId("spawn"); d.role = CombatRole::Defender; d.phases = {0}; d.pos = vec3(0, 0, -60); d.yaw = 180; d.label = "Defend"; s.spawns.push_back(d);
  s.rules.defenderSlots = 1; s.rules.attackerAI = 4; s.rules.captureModel = CaptureModel::Contest;
  return s.toJson();
}
vec3 jointW(const Soldier& s, int j) { return vec3(s.world * vec4(s.pose.joint[j], 1)); }
// a round from `from` at `target` (aimed up by the drop over its flight), fired by `owner` with that ammunition
void shoot(App& a, vec3 from, vec3 target, float speed, const std::string& ammo, int owner) {
  float T = glm::length(target - from) / speed;
  vec3 aim = target + vec3(0, 0.5f * 9.81f * T * T, 0);
  a.fireProjectile(from, glm::normalize(aim - from), speed, PROJ_BULLET, 0, ~0ull, false, ammo);
  a.projectiles.back().owner = owner;
}
std::string f2(float v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
}

void SelfTest::rulesUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { rseq.push_back({n, f}); };
  if (stage == 2700) {
    reportName = "rules";
    rseq.clear(); rcur = 0; rt = 0;
    // ================================================================ 1. damage table (headless math)
    check(damageTableReload() && damageTable().error.empty(), "content/ai/damage.json loads (" + (damageTable().error.empty() ? std::string("ok") : damageTable().error) + ")");
    const DamageTable& T = damageTable();
    check(T.health == 100 && T.head == 4.0f && T.torso == 1.0f && T.stomach == 1.25f && T.arms == 1.0f && T.legs == 0.75f && !T.helmetReducesDamage, "DMG1: health 100; zones head x4, chest / neck / arms x1, stomach x1.25, legs x0.75 (every gun)");
    {
      int z[7] = {DZ_HEAD, DZ_UPPER_TORSO, DZ_LOWER_TORSO, DZ_NECK, DZ_ARM, DZ_UPPER_THIGH, DZ_LIMB};
      float want[7] = {4.0f, 1, 1.25f, 1, 1, 0.75f, 0.75f}; bool ok = true;
      for (int k = 0; k < 7; ++k) ok &= dmZoneMult(T, z[k]) == want[k];
      check(ok, "zone multipliers: head 4, upper torso 1, lower torso (stomach) 1.25, neck 1, arm 1, upper thigh 0.75, leg / foot 0.75");
    }
    // DMG1 shots to kill, unarmoured: chest at 10 / 50 / 100 / 200 m, legs at 10 / 100 m, head at 10 m (the user's reference sheet)
    struct Row { const char* id; int torso[4]; int legs[2]; int head; bool dataOnly; };
    const Row rows[] = {
      {"kar98k", {2, 2, 2, 2}, {2, 2}, 1, false}, {"kar98k_zf39", {2, 2, 2, 2}, {2, 2}, 1, false}, {"m1903a4", {2, 2, 2, 2}, {2, 2}, 1, true},
      {"m1_garand", {2, 2, 2, 2}, {3, 3}, 1, false}, {"m1c_garand", {2, 2, 2, 2}, {3, 3}, 1, false}, {"g43", {2, 2, 2, 2}, {3, 3}, 1, true},
      {"m1_carbine", {4, 4, 5, 5}, {5, 6}, 1, false}, {"m1918a2_bar", {3, 3, 3, 4}, {4, 4}, 1, false}, {"stg44", {4, 4, 4, 4}, {5, 5}, 1, false},
      {"m1_thompson", {4, 5, 5, 5}, {5, 7}, 2, false}, {"m3_grease_gun", {4, 5, 5, 5}, {5, 7}, 2, false}, {"mp40", {4, 6, 6, 6}, {5, 8}, 2, false},
      {"mg42", {3, 3, 3, 4}, {4, 4}, 1, false}, {"fg42", {3, 3, 3, 4}, {4, 4}, 1, false}, {"m1919a6", {3, 3, 3, 4}, {4, 4}, 1, true},
      {"m2hb_carry", {1, 1, 1, 2}, {2, 2}, 1, false}, {"m1911a1", {3, 5, 5, 5}, {4, 7}, 2, false}, {"luger", {3, 5, 5, 5}, {4, 7}, 2, false},
      {"walther_p38", {3, 5, 5, 5}, {4, 7}, 2, true}, {"modern_scar", {3, 3, 3, 3}, {4, 4}, 1, false}, {"modern_ak", {3, 3, 3, 4}, {4, 4}, 1, false},
      {"modern_m4", {4, 4, 4, 5}, {5, 5}, 1, false}, {"modern_m4_irons", {4, 4, 4, 5}, {5, 5}, 1, false}, {"modern_m16", {4, 4, 4, 4}, {5, 5}, 1, false},
      {"modern_lmg", {4, 4, 4, 4}, {5, 5}, 1, false}, {"modern_shotgun", {4, 17, 17, 17}, {5, 23}, 1, false}, {"modern_sniper_bolt", {1, 1, 1, 1}, {2, 2}, 1, false},
      {"modern_sniper_semi", {2, 2, 2, 2}, {2, 2}, 1, false}, {"modern_pistol", {3, 5, 5, 5}, {4, 7}, 2, false}, {"modern_ar15", {4, 4, 4, 5}, {5, 5}, 1, false},
      {"modern_mp7", {4, 4, 6, 6}, {5, 8}, 2, false}, {"modern_scorpion", {4, 5, 7, 7}, {6, 9}, 2, false}, {"modern_uzi", {4, 5, 7, 7}, {5, 9}, 2, false},
      {"modern_shotgun2", {4, 17, 17, 17}, {6, 23}, 1, false}, {"modern_sniper_50", {1, 1, 1, 1}, {1, 1}, 1, false}, {"modern_sniper_3", {1, 1, 1, 1}, {2, 2}, 1, false},
      {"modern_pistol3", {4, 6, 6, 6}, {5, 8}, 2, false}, {"modern_ak12", {4, 4, 4, 4}, {5, 5}, 1, false}, {"modern_fal", {3, 3, 3, 3}, {4, 4}, 1, false},
      {"modern_pistol2", {5, 8, 8, 8}, {7, 11}, 2, false}
    };
    const float R[4] = {10, 50, 100, 200};
    json stk = json::object();
    for (auto& r : rows) {
      auto it = T.weapons.find(r.id);
      if (it == T.weapons.end()) { check(false, std::string("damage.json has a row for ") + r.id); continue; }
      const WeaponDamage& w = it->second;
      int t[4], l[2], h = dmShotsToKill(w, 10, DZ_HEAD);
      for (int k = 0; k < 4; ++k) t[k] = dmShotsToKill(w, R[k], DZ_UPPER_TORSO);
      l[0] = dmShotsToKill(w, 10, DZ_LIMB); l[1] = dmShotsToKill(w, 100, DZ_LIMB);
      bool ok = h == r.head; for (int k = 0; k < 4; ++k) ok &= t[k] == r.torso[k]; ok &= l[0] == r.legs[0] && l[1] == r.legs[1];
      int wi = app.weaponIndex(r.id);
      float rpm = wi >= 0 ? app.weaponDefs[wi]->rpm : w.rpm;
      char b[340]; std::snprintf(b, sizeof b, "%-18s STK chest %d/%d/%d/%d (want %d/%d/%d/%d), legs %d/%d (want %d/%d), head %d (want %d) | near %.0f far %.0f falloff %.0f-%.0f m, %s pen %.2f, %.0f rpm%s",
                                r.id, t[0], t[1], t[2], t[3], r.torso[0], r.torso[1], r.torso[2], r.torso[3], l[0], l[1], r.legs[0], r.legs[1], h, r.head, w.nearDmg, w.farDmg, w.falloffStart, w.falloffEnd, w.cls.c_str(), w.pen, rpm, w.dataOnly ? " [data only]" : "");
      check(ok, b);
      check(w.dataOnly == r.dataOnly && (wi >= 0) == !r.dataOnly, std::string(r.id) + (r.dataOnly ? ": data-only row, no weapon model in the repo" : ": weapon exists in the repo"));
      if (wi >= 0) check(std::fabs(w.rpm - app.weaponDefs[wi]->rpm) < 0.5f, std::string(r.id) + ": table rpm " + f2(w.rpm) + " = the repo's " + f2(app.weaponDefs[wi]->rpm));
      if (w.cls == "pistol" || w.cls == "smg") check(dmDamage(w, 0, DZ_HEAD) >= 70 && dmDamage(w, 0, DZ_HEAD) <= 80, std::string(r.id) + ": a pistol / SMG headshot does " + f2(dmDamage(w, 0, DZ_HEAD)) + " (70-80: two to kill)");
      stk[r.id] = {{"torso", {t[0], t[1], t[2], t[3]}}, {"legs", {l[0], l[1]}}, {"head", h}, {"rpm", rpm}};
    }
    summary["stk"] = stk;
    // every bullet-firing weapon in the repo has its own row
    for (auto& d : app.weaponDefs) {
      bool bullet = (d->action == "semi" || d->action == "auto" || d->action == "bolt" || d->action == "heavy") && !d->container.empty();
      if (bullet) check(T.weapons.count(d->id) > 0, "bullet weapon " + d->id + " has its own damage row");
    }
    // formula, zones, decisions
    const WeaponDamage& kar = dmRow("kar98k"); const WeaponDamage& spr = dmRow("m1903a4"); const WeaponDamage& gar = dmRow("m1_garand"); const WeaponDamage& tom = dmRow("m1_thompson");
    check(dmShotsToKill(kar, 300, DZ_HEAD) == 1 && dmShotsToKill(kar, 300, DZ_LOWER_TORSO) == 1 && dmShotsToKill(kar, 300, DZ_UPPER_TORSO) == 2 && std::fabs(dmDamage(kar, 300, DZ_UPPER_TORSO) - 90) < 1e-3f,
          "Kar98k (scout class): head and stomach one shot at any range, a chest hit leaves a sliver (90)");
    check(T.springfieldProfile == "brief" && dmBaseDamage(spr, 100) == 95 && dmBaseDamage(spr, 150) == 80, "[DECISION] decisions.springfieldProfile 'brief': Springfield 95 up to 100 m, 80 from 150 m");
    {   // the other value of the decision and a zone override (parsed into a scratch table)
      json j = json::parse(readTextFile(contentDir() / "ai" / "damage.json"));
      j["decisions"]["springfieldProfile"] = "kar98k"; j["zones"]["legs"] = 1.2;
      DamageTable alt; std::string err; bool ok = parseDamageTable(j, alt, err);
      const WeaponDamage& k2 = alt.weapons["kar98k"]; const WeaponDamage& s2 = alt.weapons["m1903a4"];
      check(ok && dmBaseDamage(k2, 50) * dmZoneMult(alt, DZ_LIMB) >= 100 && dmBaseDamage(s2, 300) == 95,
            "switches work: zones.legs 1.2 -> Kar98k leg " + f2(dmBaseDamage(k2, 50) * dmZoneMult(alt, DZ_LIMB)) + " (one hit), springfieldProfile 'kar98k' -> 95 at 300 m");
    }
    check(dmBaseDamage(kar, 0) == 90 && dmBaseDamage(kar, 5000) == 90, "no falloff when start == end (Kar98k 90 at 0 and 5000 m)");
    check(std::fabs(dmBaseDamage(gar, 105) - 57.5f) < 1e-3f && dmBaseDamage(gar, 60) == 65 && dmBaseDamage(gar, 150) == 50,
          "smoothstep falloff: Garand 65 at 60 m, 57.5 at 105 m (midpoint), 50 at 150 m");
    check(dmShotsToKill(gar, 10, DZ_HEAD) == 1 && dmShotsToKill(gar, 10, DZ_UPPER_TORSO) == 2 && std::fabs(dmDamage(gar, 10, DZ_LOWER_TORSO) - 81.25f) < 1e-3f, "Garand: one headshot, two to the body (stomach 81.25)");
    check(std::fabs(dmDamage(tom, 10, DZ_HEAD) - 80) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_NECK) - 33) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_ARM) - 33) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_LOWER_TORSO) - 41.25f) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_UPPER_THIGH) - 24.75f) < 1e-3f,
          "zones: Thompson head 80 (x4 capped at the SMG 80), neck and arm count as chest (33), stomach 41.25, upper thigh as leg (24.75)");
    {   // DMG1 armour: damage x (1 - (1 - pen) x level share)
      Kit k; k.vest = 3; k.vestDur = 1000; k.helmet = 1; k.helmetDur = 1000;
      const WeaponDamage& ak = dmRow("modern_ak"); const WeaponDamage& pis = dmRow("m1911a1");
      float v3 = armorAbsorb(k, false, 35, ak.pen), h1 = armorAbsorb(k, true, 80, pis.pen);
      char b[200]; std::snprintf(b, sizeof b, "armour: AK chest 35 through a level 3 vest %.2f (x pen %.3f), M1911 head 80 through a level 1 helmet %.2f (gap x 0.6)", v3, ak.pen, h1);
      check(std::fabs(v3 - 35 * ak.pen) < 1e-3f && std::fabs(h1 - 80 * (1 - 0.5f * 0.6f)) < 1e-3f, b);
    }
    // ================================================================ 2. movement data
    {
      auto& m = app.move;
      float g0 = 7.848f, v0 = 3.757f, h0 = v0 * v0 / (2 * g0), t0 = 2 * v0 / g0;
      float h = m.jumpVelocity * m.jumpVelocity / (2 * m.gravity), t = 2 * m.jumpVelocity / m.gravity;
      char b[300]; std::snprintf(b, sizeof b, "movement.json: player g %.4f m/s^2 (x%.4f of 7.848), jump %.4f m/s; analytic apex %.4f m (x%.4f), airtime %.4f s (x%.4f)", m.gravity, m.gravity / g0, m.jumpVelocity, h, h / h0, t, t / t0);
      (void)h0; (void)t0;   // was SPEC 4 (x1.2 apex, x0.75 airtime of these); 2026-11-08 jumps like real life
      check(std::fabs(m.gravity - 9.81f) < 1e-3f && h > 0.4f && h < 0.6f && t > 0.55f && t < 0.7f, b);
      summary["movement"] = {{"gravity", m.gravity}, {"jump", m.jumpVelocity}, {"apex", h}, {"airtime", t}};
      const RulesConfig& c = rulesConfig();
      float vs = rulesImpactSpeed(c.safeHeight, m.gravity), vl = rulesImpactSpeed(c.lethalHeight, m.gravity);
      std::snprintf(b, sizeof b, "fall damage thresholds stay heights (safe %.1f m, lethal %.1f m) -> impact speeds %.2f / %.2f m/s at g %.2f (old g: %.2f / %.2f)",
                    c.safeHeight, c.lethalHeight, vs, vl, m.gravity, rulesImpactSpeed(c.safeHeight, g0), rulesImpactSpeed(c.lethalHeight, g0));
      check(rulesFallDamage(vs * 0.999f, m.gravity) == 0 && rulesFallDamage(vl, m.gravity) == 100 && c.lethalHeight > c.safeHeight, b);
      bool same = true; for (float hh : {2.0f, 4.0f, 6.0f, 8.0f, 12.0f}) same &= std::fabs(rulesFallDamage(rulesImpactSpeed(hh, g0), g0) - rulesFallDamage(rulesImpactSpeed(hh, m.gravity), m.gravity)) < 1e-3f;
      check(same && rulesFallDamage(rulesImpactSpeed(c.safeHeight, g0), m.gravity) == 0, "a drop does the same damage under the old and new gravity (2..12 m); the old safe-height impact speed does nothing now");
    }
    // ================================================================ 3. jump on real physics
    const TemplateInfo* tpl = app.findTemplate("blank_slate");
    app.newMap(*tpl, "Rules range");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(app.enterExplore(), "Explore on a blank map"); app.useScripted = true; app.vm.procedural = false;
    push("jump apex + airtime", [this](App& a, float t) {
      static float y0, apex, yPrev, vPrev, air; static int j0, k; static bool up, done;
      auto c = Physics::character();
      if (t == 0) { Physics::setCharacterPos(vec3(40, a.terrain.height(40, 0), 0)); a.scriptedInput = ExploreInput{}; apex = 0; up = done = false; k = 0; air = 0; return false; }
      if (t < 0.6f) { y0 = c.pos.y; j0 = Physics::jumpCount(); return false; }
      if (!up) { if (Physics::jumpCount() == j0) { a.scriptedInput.jump = true; return false; } a.scriptedInput.jump = false; up = true; k = 1; yPrev = c.pos.y - y0; vPrev = c.vel.y; apex = yPrev; return false; }
      if (!done) {
        float y = c.pos.y - y0;
        if (c.grounded && y < 1e-3f && k > 2) {   // landed during the last step: the crossing inside that step
          float vStep = std::fabs(vPrev - a.move.gravity * kDt);
          air = (k + std::clamp(yPrev / std::max(1e-4f, vStep * kDt), 0.0f, 1.0f)) * kDt;
          done = true;
        } else { apex = std::max(apex, y); yPrev = y; vPrev = c.vel.y; ++k; if (k > 200) done = true; return false; }
      }
      const auto& mv = a.move; float hT = mv.jumpVelocity * mv.jumpVelocity / (2 * mv.gravity), tT = 2 * mv.jumpVelocity / mv.gravity;   // the configured (real-life) jump
      char b[260]; std::snprintf(b, sizeof b, "measured jump: apex %.4f m (target %.4f, %+.2f %%), airtime %.4f s (target %.4f, %+.2f %%); SPEC 4 was apex 1.0791 m, airtime 0.7181 s",
                                apex, hT, 100 * (apex / hT - 1), air, tT, 100 * (air / tT - 1));
      check(std::fabs(apex / hT - 1) < 0.02f && std::fabs(air / tT - 1) < 0.02f, b);
      summary["jumpMeasured"] = {{"apex", apex}, {"airtime", air}, {"apexTarget", hT}, {"airtimeTarget", tT}};
      return true; });
    push("start match", [this](App& a, float t) {
      if (t == 0) {
        a.exitExplore();
        gUi.matchScenario = rulesScenario(); gUi.loaded = false;
        if (!gGame) gGame = new Game(a);
        gTSpawnSeen = -1;
        gGame->pendingStart = true; a.doc.start = {true, vec3(0, a.terrain.height(0, -60), -60), 180};
        check(a.enterExplore() && gGame->active, "match starts on the blank map (1 human defender, 4 attacking bots)");
        a.useScripted = true; a.scriptedInput = ExploreInput{};
        return false;
      }
      Game& G = *gGame;
      Soldier* L = G.local();
      if (L && L->alive && gTSpawnSeen < 0) gTSpawnSeen = G.t;
      int bots = 0; for (auto& s : G.sold) if (!s.localPlayer && s.alive) ++bots;
      if (!(L && L->alive && bots >= 4)) { if (t > 20) { check(false, "everyone spawned"); return true; } return false; }
      return true; });
    // ---- GM1: classes (data, bots, the player's loadout, allowed list, stamina, knife)
    push("classes", [this](App& a, float) {
      Game& G = *gGame;
      const auto& C = classDefs();
      int unknown = 0; std::string miss;
      for (auto& c : C) for (const ClassSide* s : {&c.us, &c.de}) {
        std::vector<std::string> ids = s->primary; ids.insert(ids.end(), s->secondary.begin(), s->secondary.end()); if (!s->pistol.empty()) ids.push_back(s->pistol);
        for (auto& w : ids) if (a.weaponIndex(w) < 0) { ++unknown; miss += " " + w; }
      }
      check(C.size() == 7 && unknown == 0 && classById("tanker") && classById("tanker")->drivesTanks && classById("assault")->hpMul == 1.25f, std::to_string(C.size()) + " classes, every weapon they name exists" + miss);
      int bad = 0, bots = 0;
      for (auto& s : G.sold) if (s.alive && !s.localPlayer) {
        ++bots; auto cw = G.classWeapons(s);
        if (s.cls < 0 || C[s.cls].botWeight <= 0 || cw.empty() || s.weapon != cw[0]) ++bad;
      }
      check(bots > 0 && bad == 0, std::to_string(bots) + " bots spawned with a weighted class and its primary weapon (" + std::to_string(bad) + " wrong)");
      Soldier* L = G.local();
      bool lo = L && L->cls >= 0 && !a.carried.empty() && a.carried[0] == G.classWeapons(*L)[0];
      check(lo, "the player spawned with a class and its weapons (" + (L && L->cls >= 0 ? C[L->cls].id : std::string("none")) + ")");
      check(L && std::fabs(a.stamina - a.staminaCap()) < 0.01f && a.staminaMul == C[L->cls].staminaMul, "full stamina on spawn, the class's bar (x" + std::to_string(a.staminaMul) + ")");
      check(std::fabs(a.move.staminaRegen * 9.8f - a.move.staminaMax) < 0.5f, "stamina refills the whole bar in 9.8 s (" + std::to_string(a.move.staminaRegen) + " pts/s)");
      ScenarioRules r; r.allowedClasses = {"tanker"};
      ScenarioRules back = ScenarioRules::fromJson(r.toJson());
      check(back.classAllowed("tanker") && !back.classAllowed("ranger") && ScenarioRules{}.classAllowed("ranger"), "allowed classes survive the rules JSON (LAN lobby / start)");
      int k = a.weaponIndex("knife_bayonet");
      check(k >= 0 && a.weaponDefs[k]->moveSpeed == 1.3f, "knife in hand: x1.3 speed");
      return true; });
    // ---- first life: protection window
    push("spawn protection", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static double tSpawn, tEnd; static int A, B, C, D, life, ph, icons, ringFrames, protFrames, falls0; static bool sawFire, fallChecked, bulletChecked, blastChecked, tintOk;
      static float hpFall, hpBullet, ringMax;
      std::vector<int> bots; for (auto& s : G.sold) if (!s.localPlayer && s.alive) bots.push_back(s.slot);
      if (t == 0) {
        tSpawn = gTSpawnSeen; tEnd = -1; life = L.lifeId; ph = 0; icons = ringFrames = protFrames = 0; ringMax = 0; hpBullet = 0;
        sawFire = fallChecked = bulletChecked = blastChecked = false; tintOk = true;
        A = bots[0]; B = bots[1]; C = bots[2]; D = bots[3];
        for (int s : bots) G.sold[s].human = true;   // no AI: the bots only do what this test tells them
        bool all = L.isProtected(G.t); for (int s : bots) all &= G.sold[s].isProtected(G.t) && std::fabs(G.sold[s].protectUntil - G.t - rulesConfig().protectSeconds) < 0.1;
        char b[200]; std::snprintf(b, sizeof b, "every soldier spawns protected; the player for %.4f s from its spawn step (t=%.3f)", L.protectUntil - tSpawn, tSpawn);
        check(all && tSpawn >= 0 && std::fabs(L.protectUntil - tSpawn - rulesConfig().protectSeconds) < 1e-6, b);
        check(rulesConfig().protectSeconds == 5.0f && !rulesConfig().endsOnFire && !rulesConfig().endsOnThrow, "GM1: spawn protection 5 s, kept while moving, firing or throwing (rules.json)");
        rulesConfig().endsOnFire = rulesConfig().endsOnThrow = true;   // the options themselves are still tested below; restored after the protection phase
        return false;
      }
      if (L.isProtected(G.t)) {
        ++protFrames;
        icons = std::max(icons, gRulesStats.shieldIcons);
        if (gRulesStats.ring) { ++ringFrames; ringMax = std::max(ringMax, gRulesStats.ringLeft); }
        if (rulesTint(G, L) == vec4(1) && G.sold[B].isProtected(G.t) && rulesTint(G, G.sold[B]) == vec4(1)) tintOk = false;
      } else if (tEnd < 0) tEnd = G.t;
      // ---- bots: firing / throwing ends protection; the option off keeps it
      if (ph == 0 && t > 0.3f) {
        check(G.soldierFire(G.sold[A], glm::normalize(vec3(0.3f, 0.35f, 1))), "protected bot A fires (into the empty field)");
        check(G.startThrow(G.sold[C], GK_SMOKE, vec3(0, 4, 9), G.sold[C].feet + vec3(0, 0, 12)), "protected bot C starts a smoke throw");
        ph = 1; return false;
      }
      if (ph == 1) {   // A's round had its first segment in the step after the shot (this selftest call runs before the next)
        check(!G.sold[A].isProtected(G.t) && std::fabs(G.sold[A].protectUntil - G.sold[A].lastShot) <= kDt + 1e-6, "firing ended bot A's protection within one frame of the shot (" + f2((float)(G.sold[A].protectUntil - tSpawn)) + " s after spawn)");
        check(G.sold[C].isProtected(G.t) && G.sold[C].throwT >= 0, "bot C is still protected while winding up the throw");
        rulesConfig().endsOnFire = false;
        check(G.soldierFire(G.sold[D], glm::normalize(vec3(-0.3f, 0.35f, 1))), "protected bot D fires with protectionEndsOnFire off");
        ph = 10; return false;
      }
      if (ph == 10) {
        check(G.sold[D].isProtected(G.t) && G.sold[D].lastShot > tSpawn, "with protectionEndsOnFire off, bot D stays protected after firing");
        rulesConfig().endsOnFire = true;
        Physics::setCharacterPos(L.feet + vec3(0, 12, 0)); hpFall = L.hp; falls0 = gRulesStats.falls;   // 12 m: lethal when unprotected
        ph = 2; return false;
      }
      if (ph == 2 && G.sold[C].throwReleased && !sawFire) {
        sawFire = true;
        check(!G.sold[C].isProtected(G.t) && G.t - G.sold[C].protectUntil <= kDt + 1e-6, "releasing the grenade ended bot C's protection (" + f2((float)(G.sold[C].protectUntil - tSpawn)) + " s after spawn)");
      }
      if (ph == 2 && !fallChecked && gRulesStats.falls > falls0 && Physics::character().grounded) {
        fallChecked = true;
        char b[200]; std::snprintf(b, sizeof b, "12 m fall while protected: impact %.2f m/s (would do %.0f HP), health %.0f -> %.0f", gRulesStats.lastFallSpeed, gRulesStats.lastFallDamage, hpFall, L.hp);
        check(L.alive && L.hp == hpFall && gRulesStats.lastFallDamage >= 99 && L.isProtected(G.t), b);
      }
      if (ph == 2 && fallChecked && t > 1.7f && !bulletChecked) {   // a .45 round at the protected player's chest
        if (hpBullet == 0) { hpBullet = L.hp; gDmLastHit.valid = false; vec3 c = jointW(L, CJ_SPINE2); shoot(a, c + vec3(0, 0, 8), c, 285, "45_mag50", A); return false; }
        if (!gDmLastHit.valid) return false;
        bulletChecked = true;
        const FleshHit& fh = gDmLastHit.fh;
        check(fh.victimSlot == L.slot && fh.deflected && fh.damage == 0 && L.hp == hpBullet && gRulesStats.deflects > 0, "bullet on the protected player: FleshHit deflected, 0 damage, deflected puff (health " + f2(L.hp) + ")");
      }
      if (ph == 2 && bulletChecked && !blastChecked) {
        blastChecked = true;
        float h0 = L.hp, hb = G.sold[B].hp;
        G.damage(L.slot, 60, A, vec3(0, 0, -1), "test");
        G.explosionDamage(G.chest(L) + vec3(0.8f, 0, 0), 0, 0, A, true);
        G.explosionDamage(G.chest(G.sold[B]) + vec3(0.8f, 0, 0), 0, 0, L.slot, true);
        check(L.hp == h0 && G.sold[B].hp == hb, "direct damage and grenade blasts at 0.8 m do nothing to protected soldiers (player and bot B)");
      }
      if (tEnd < 0) return t > 6;   // timeout guard (checked below)
      double held = tEnd - tSpawn;
      char b[220]; std::snprintf(b, sizeof b, "player protection lasted %.4f s (5.0 s +- one 1/60 s frame), %d protected frames", held, protFrames);
      check(std::fabs(held - rulesConfig().protectSeconds) <= kDt + 1e-4, b);
      rulesConfig().endsOnFire = rulesConfig().endsOnThrow = false;   // back to the rules.json defaults (GM1)
      check(icons >= 1, "shield icon drawn over protected enemies in view (" + std::to_string(icons) + " at once)");
      check(ringFrames >= protFrames - 2 && ringMax > 2.9f, "countdown ring on the protected player's HUD (from " + f2(ringMax) + " s)");
      check(tintOk && rulesTint(G, L) == vec4(1), "shimmer tint while protected, plain after");
      check(fallChecked && bulletChecked && blastChecked && sawFire, "all protected-window checks ran");
      summary["protection"] = {{"held_s", held}, {"botFireEnded_s", G.sold[A].protectUntil - tSpawn}, {"botThrowEnded_s", G.sold[C].protectUntil - tSpawn}};
      (void)life;
      return true; });
    // ---- after protection: fall damage by height
    push("fall damage", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static int ph, n0; static float hp0;
      if (t == 0) { ph = 0; return false; }
      auto drop = [&](float h) { L.hp = 100; hp0 = 100; n0 = gRulesStats.falls; Physics::setCharacterPos(L.feet + vec3(0, h, 0)); };
      if (ph == 0) { drop(2.0f); ph = 1; return false; }
      if (ph == 1) {
        if (gRulesStats.falls == n0 || !Physics::character().grounded) return t > 5 ? (check(false, "2 m drop landed"), true) : false;
        check(L.hp == 100, "2 m drop (below the 3 m safe height): no damage (impact " + f2(gRulesStats.lastFallSpeed) + " m/s)");
        drop(6.0f); ph = 2; return false;
      }
      if (gRulesStats.falls == n0 || !Physics::character().grounded) return t > 10 ? (check(false, "6 m drop landed"), true) : false;
      float want = rulesFallDamage(rulesImpactSpeed(6.0f, a.move.gravity), a.move.gravity);
      char b[200]; std::snprintf(b, sizeof b, "6 m drop: %.1f HP (impact %.2f m/s; analytic %.1f HP at %.2f m/s)", hp0 - L.hp, gRulesStats.lastFallSpeed, want, rulesImpactSpeed(6.0f, a.move.gravity));
      check(L.alive && std::fabs((hp0 - L.hp) - want) < 3.0f, b);
      L.hp = 100;
      return true; });
    // ---- after protection: a bullet hit on the player jolts the aim (suppression flinch)
    push("hit flinch", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static float p0, y0;
      if (t == 0) {
        L.hp = 100; gDmLastHit.valid = false; a.lastFlinch = 0; a.scriptedInput = ExploreInput{}; p0 = a.eyePitch; y0 = a.eyeYaw;
        int A = -1; for (auto& s : G.sold) if (!s.localPlayer && s.alive && A < 0) A = s.slot;
        vec3 c = jointW(L, CJ_SPINE2); shoot(a, c + vec3(0, 0, 8), c, 285, "45_mag50", A);
        return false;
      }
      if (!gDmLastHit.valid) return t > 3 ? (check(false, "flinch: the round hit the player"), true) : false;
      float moved = glm::length(vec2(a.eyePitch - p0, wrapDeg(a.eyeYaw - y0)));
      char b[200]; std::snprintf(b, sizeof b, "a Thompson hit on the player (%.1f HP) moves the aim %.2f deg (flinch %.2f; want 0.6-1.2)", gDmLastHit.fh.damage, moved, a.lastFlinch);
      check(gDmLastHit.fh.victimSlot == L.slot && gDmLastHit.fh.damage > 0 && moved >= 0.59f && moved <= 1.21f && std::fabs(moved - a.lastFlinch) < 0.02f, b);
      summary["flinch_deg"] = moved;
      L.hp = 100;
      return true; });
    // ---- after protection: zones, helmet, travelled range (bot B is the target, the player shoots)
    push("zones + range", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static int ph, B; static std::string what, wid; static int wantZone; static bool wantHelmet; static vec3 from;
      if (t == 0) {   // the target stands alone 25 m east of the attackers' spawn, facing -z (shots come from behind: no arms or gun in the way)
        ph = 0; B = -1; for (auto& s : G.sold) if (!s.localPlayer && s.alive && B < 0) B = s.slot;
        Soldier& V = G.sold[B]; vec3 p(25, 0, 60); p.y = a.terrain.height(p.x, p.z);
        V.feet = p; V.yaw = 0; V.br.aimYaw = 0; if (V.agent >= 0) Physics::setAgentPos(V.agent, p);
        return false;
      }
      if (t < 0.3f) return false;   // pose and world matrix at the new spot
      Soldier& V = G.sold[B];
      struct Shot { const char* what; int target; float u; float up; float range; const char* ammo; float speed; const char* wid; int zone; bool helmet; bool side = false; };
      static const Shot shots[] = {
        {"upper torso, Thompson 10 m", 1, 0.75f, 0, 10, "45_mag50", 285, "m1_thompson", DZ_UPPER_TORSO, false},
        {"lower torso, Thompson 10 m", 1, 0.25f, 0, 10, "45_mag50", 285, "m1_thompson", DZ_LOWER_TORSO, false},
        {"thigh, Thompson 10 m", 2, 0.65f, 0, 10, "45_mag50", 285, "m1_thompson", DZ_LIMB, false},
        {"helmet (back of the head), Thompson 10 m", 0, 0, 0.20f, 10, "45_mag50", 285, "m1_thompson", DZ_HEAD, true},
        {"head below the helmet rim (side, ear level), Thompson 10 m", 0, 0, 0.05f, 10, "45_mag50", 285, "m1_thompson", DZ_HEAD, false, true},
        {"upper torso, Garand 80 m", 1, 0.75f, 0, 80, "30-06_clip", 853, "m1_garand", DZ_UPPER_TORSO, false},
        {"upper torso, Garand 150 m", 1, 0.75f, 0, 150, "30-06_clip", 853, "m1_garand", DZ_UPPER_TORSO, false},
      };
      const int nShots = sizeof shots / sizeof shots[0];
      int k = ph / 2;
      if (k >= nShots) {
        float hb = V.hp = 100;
        G.explosionDamage(G.chest(V) + vec3(0.8f, 0, 0), 0, 0, L.slot, true);
        check(V.hp < hb, "after protection the same 0.8 m grenade blast hurts (" + f2(hb - V.hp) + " HP)");
        V.hp = 100;
        return true;
      }
      const Shot& s = shots[k];
      if (ph % 2 == 0) {   // aim and fire
        V.hp = 100; gDmLastHit.valid = false;
        vec3 tgt;
        if (s.target == 1) { vec3 h = jointW(V, CJ_HIPS), n = jointW(V, CJ_NECK); tgt = h + (n - h) * s.u; }
        else if (s.target == 2) { vec3 h = jointW(V, CJ_LUPLEG), n = jointW(V, CJ_LLEG); tgt = h + (n - h) * s.u; }
        else { vec3 h = jointW(V, CJ_HEAD), n = jointW(V, CJ_NECK); tgt = h + glm::normalize(h - n) * s.up; }
        vec3 back(std::sin(glm::radians(V.yaw)), 0, std::cos(glm::radians(V.yaw)));   // the soldier looks along (-sin yaw, 0, -cos yaw)
        from = tgt + (s.side ? vec3(back.z, 0, -back.x) : back) * s.range;
        shoot(a, from, tgt, s.speed, s.ammo, L.slot);
        what = s.what; wantZone = s.zone; wantHelmet = s.helmet; wid = s.wid;
        ++ph; return false;
      }
      if (!gDmLastHit.valid) { if (t > 8.0f * (k + 1)) { check(false, std::string(s.what) + ": the round hit the bot"); ++ph; } return false; }
      const FleshHit& fh = gDmLastHit.fh;
      bool zoneOk = fh.zone == wantZone || (wantZone == DZ_LIMB && fh.zone == DZ_UPPER_THIGH);
      float dist = glm::length(fh.pos - from);   // straight line to the struck point
      float expect = dmDamage(dmRow(wid), dist, wantZone), formula = dmDamage(dmRow(gDmLastHit.weaponId), gDmLastHit.range, fh.zone);
      char b[320]; std::snprintf(b, sizeof b, "%s: %s, zone %d (want %d), helmet %d, %.2f HP (table %.2f at %.2f m), travelled %.2f m%s",
                                what.c_str(), gDmLastHit.weaponId.c_str(), fh.zone, wantZone, (int)fh.helmet, fh.damage, expect, dist, gDmLastHit.range, fh.deflected ? " DEFLECTED" : "");
      check(zoneOk && fh.helmet == wantHelmet && !fh.deflected && (gDmLastHit.weaponId == wid || a.weaponDefs[a.weaponIndex(gDmLastHit.weaponId)]->container == a.weaponDefs[a.weaponIndex(wid)]->container) && std::fabs(fh.damage - formula) < 0.01f && std::fabs(fh.damage - expect) < 0.6f && std::fabs(gDmLastHit.range / dist - 1) < 0.01f, b);
      V.hp = 100;
      ++ph; return false; });
    // ---- a respawn restarts protection; the player's own shot keeps it (real input path, GM1)
    push("respawn + own shot", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static int life, ph, shots0, fired0; static double tStart;
      if (t == 0) { life = L.lifeId; ph = 0; G.killSoldier(L.slot, -1, vec3(0, 0, -1)); return false; }
      if (ph == 0) {
        if (!(L.alive && L.lifeId != life)) return t > 25 ? (check(false, "player respawned"), true) : false;
        tStart = G.t;
        check(L.isProtected(G.t) && std::fabs(L.protectUntil - G.t - rulesConfig().protectSeconds) < 1e-6, "respawn: protection starts again (5.0 s)");
        ph = 1; return false;
      }
      if (ph == 1) { if (G.t - tStart < 0.5) return false; shots0 = a.shotsFired; fired0 = gRulesStats.endedByFire; ph = 2; }
      if (ph == 2) {
        a.scriptedInput.fire = a.shotsFired == shots0;
        if (a.shotsFired == shots0) return G.t - tStart > 2.8 ? (check(false, "the player's weapon fired"), a.scriptedInput.fire = false, true) : false;
        a.scriptedInput.fire = false; ph = 3; return false;   // the round's first segment runs on this frame's step
      }
      check(L.isProtected(G.t) && gRulesStats.endedByFire == fired0,   // GM1: protection is kept while firing (rules.json protectionEndsOnFire false)
            "the player's own shot (" + a.weaponDefs[a.activeWeapon]->id + ", real input path) keeps the 5 s protection (" + f2((float)(L.protectUntil - tStart)) + " s)");
      return true; });
    push("exit", [this](App& a, float t) { if (t < 0.2f) return false; a.exitExplore(); return true; });
  }
  stage = 2701;
  if (rcur >= rseq.size()) { app.useScripted = false; finish(app); return; }
  if (rt == 0) logf("[selftest] step: %s", rseq[rcur].name.c_str());
  bool d2 = rseq[rcur].fn(app, rt);
  rt += dt;
  if (rt > 90) { check(false, "step timed out: " + rseq[rcur].name); d2 = true; }
  if (d2) { ++rcur; rt = 0; }
}
