// --selftest-rules (W1, stages 2700+): gameplay rules of the MP & Creative update.
//  1. damage table (pure math, SPEC 6.2): every row's shots to kill at 10/50/100/200 m torso and 10/100 m limbs,
//     the [DECISION] rows as written, the repo RPMs, a row for every bullet weapon, the formula and zones;
//  2. movement data (SPEC 4): g x2.133, v0 = sqrt(2 g h), fall thresholds kept as heights;
//  3. jump on real physics: apex and airtime within 2 % of the target (h x1.2, t x0.75);
//  4. a match on a blank map: spawn protection lasts 3.0 s +- one frame, blocks bullets / blasts / falls (deflected
//     FleshHit, no damage), ends on firing and on throwing (and not when the option is off), shield icons / ring /
//     tint; after it: fall damage by height, zones (upper / lower torso, limb, head, helmet), true travelled range;
//     a respawn restarts protection and the local player's own shot ends it.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "rules.h"
#include "damage_model.h"
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
    check(T.health == 100 && T.upperTorso == 1.0f && T.lowerTorso == 0.9f && T.limbs == 0.65f && !T.helmetReducesDamage, "health 100; zones upper torso x1.0, lower torso x0.9, limbs x0.65; helmets do not reduce damage");
    struct Row { const char* id; int torso[4]; int limbs[2]; bool dataOnly; };
    const Row rows[] = {
      {"kar98k", {1, 1, 1, 1}, {2, 2}, false}, {"kar98k_zf39", {1, 1, 1, 1}, {2, 2}, false}, {"m1903a4", {1, 1, 1, 2}, {2, 2}, true},
      {"m1_garand", {1, 1, 2, 2}, {2, 3}, false}, {"g43", {1, 1, 2, 2}, {2, 3}, true}, {"m1_carbine", {2, 2, 3, 3}, {3, 5}, false},
      {"m1918a2_bar", {2, 2, 3, 3}, {3, 4}, false}, {"stg44", {3, 3, 4, 4}, {5, 6}, false}, {"m1_thompson", {4, 5, 5, 5}, {6, 7}, false},
      {"m3_grease_gun", {3, 4, 4, 4}, {5, 7}, false}, {"mp40", {3, 5, 5, 5}, {5, 7}, false}, {"mg42", {3, 3, 3, 3}, {4, 4}, false},
      {"m1919a6", {2, 2, 3, 3}, {4, 4}, true}, {"m1911a1", {3, 5, 5, 5}, {4, 7}, true}, {"walther_p38", {3, 5, 5, 5}, {5, 8}, true},
      // repo weapons outside the SPEC table, documented in damage.json: the band they slot into
      {"m1c_garand", {1, 1, 2, 2}, {2, 3}, false}, {"fg42", {3, 3, 3, 3}, {4, 4}, false}, {"m2hb_carry", {1, 1, 1, 2}, {2, 2}, false},
    };
    const float R[4] = {10, 50, 100, 200};
    json stk = json::object();
    for (auto& r : rows) {
      auto it = T.weapons.find(r.id);
      if (it == T.weapons.end()) { check(false, std::string("damage.json has a row for ") + r.id); continue; }
      const WeaponDamage& w = it->second;
      int t[4], l[2];
      for (int k = 0; k < 4; ++k) t[k] = dmShotsToKill(w, R[k], DZ_UPPER_TORSO);
      l[0] = dmShotsToKill(w, 10, DZ_LIMB); l[1] = dmShotsToKill(w, 100, DZ_LIMB);
      bool ok = true; for (int k = 0; k < 4; ++k) ok &= t[k] == r.torso[k]; ok &= l[0] == r.limbs[0] && l[1] == r.limbs[1];
      int wi = app.weaponIndex(r.id);
      float rpm = wi >= 0 ? app.weaponDefs[wi]->rpm : w.rpm;
      char b[300]; std::snprintf(b, sizeof b, "%-13s STK torso %d/%d/%d/%d (want %d/%d/%d/%d), limbs %d/%d (want %d/%d) | near %.0f far %.0f falloff %.0f-%.0f m, head x%.1f, %.0f rpm, close TTK %.0f ms%s",
                                r.id, t[0], t[1], t[2], t[3], r.torso[0], r.torso[1], r.torso[2], r.torso[3], l[0], l[1], r.limbs[0], r.limbs[1], w.nearDmg, w.farDmg, w.falloffStart, w.falloffEnd, w.headMult, rpm,
                                (t[0] - 1) * 60000.0f / std::max(1.0f, rpm), w.dataOnly ? " [data only]" : "");
      check(ok, b);
      check(w.dataOnly == r.dataOnly && (wi >= 0) == !r.dataOnly, std::string(r.id) + (r.dataOnly ? ": data-only row, no weapon model in the repo" : ": weapon exists in the repo"));
      if (wi >= 0) check(std::fabs(w.rpm - app.weaponDefs[wi]->rpm) < 0.5f, std::string(r.id) + ": table rpm " + f2(w.rpm) + " = the repo's " + f2(app.weaponDefs[wi]->rpm));
      stk[r.id] = {{"torso", {t[0], t[1], t[2], t[3]}}, {"limbs", {l[0], l[1]}}, {"rpm", rpm}};
    }
    summary["stk"] = stk;
    // every bullet-firing weapon in the repo has its own row
    for (auto& d : app.weaponDefs) {
      bool bullet = (d->action == "semi" || d->action == "auto" || d->action == "bolt" || d->action == "heavy") && !d->container.empty();
      if (bullet) check(T.weapons.count(d->id) > 0, "bullet weapon " + d->id + " has its own damage row");
    }
    // [DECISION] rows as written, formula and zones
    const WeaponDamage& kar = dmRow("kar98k"); const WeaponDamage& spr = dmRow("m1903a4"); const WeaponDamage& gar = dmRow("m1_garand"); const WeaponDamage& tom = dmRow("m1_thompson");
    check(T.kar98kLimbMult == 0.65f && dmShotsToKill(kar, 10, DZ_LIMB) == 2 && dmShotsToKill(dmRow("kar98k_zf39"), 300, DZ_LIMB) == 2 && dmShotsToKill(kar, 300, DZ_UPPER_TORSO) == 1 && dmShotsToKill(kar, 300, DZ_HEAD) == 1,
          "[DECISION] decisions.kar98kLimbMult 0.65: Kar98k / Kar98k sniper limb hits take 2 shots (97.5), torso and head one shot at any range (300 m too)");
    {
      float oneShot = 0; for (float r = 100; r <= 200; r += 0.5f) if (dmShotsToKill(spr, r, DZ_UPPER_TORSO) == 1) oneShot = r;
      check(T.springfieldProfile == "brief" && dmShotsToKill(spr, 100, DZ_UPPER_TORSO) == 1 && dmShotsToKill(spr, 150, DZ_UPPER_TORSO) == 2 && oneShot > 120 && oneShot < 150,
            "[DECISION] decisions.springfieldProfile 'brief': Springfield one torso shot up to " + f2(oneShot) + " m (falloff 100-150 m), 2 beyond");
    }
    {   // the other value of each decision (parsed into a scratch table)
      json j = json::parse(readTextFile(contentDir() / "ai" / "damage.json"));
      j["decisions"]["kar98kLimbMult"] = 0.67; j["decisions"]["springfieldProfile"] = "kar98k";
      DamageTable alt; std::string err; bool ok = parseDamageTable(j, alt, err);
      const WeaponDamage& k2 = alt.weapons["kar98k"]; const WeaponDamage& s2 = alt.weapons["m1903a4"];
      check(ok && dmBaseDamage(k2, 50) * dmZoneMult(alt, k2, DZ_LIMB) >= 100 && dmBaseDamage(s2, 300) == 150,
            "decision switches work: kar98kLimbMult 0.67 -> Kar98k limb " + f2(dmBaseDamage(k2, 50) * dmZoneMult(alt, k2, DZ_LIMB)) + " (one hit), springfieldProfile 'kar98k' -> 150 at 300 m");
    }
    check(dmBaseDamage(kar, 0) == 150 && dmBaseDamage(kar, 5000) == 150, "no falloff when start == end (Kar98k 150 at 0 and 5000 m)");
    check(std::fabs(dmBaseDamage(gar, 80) - 92.5f) < 1e-3f && dmBaseDamage(gar, 60) == 115 && dmBaseDamage(gar, 100) == 70,
          "smoothstep falloff: Garand 115 at 60 m, 92.5 at 80 m (midpoint), 70 at 100 m");
    check(dmShotsToKill(gar, 10, DZ_LOWER_TORSO) == 1 && std::fabs(dmDamage(gar, 10, DZ_LOWER_TORSO) - 103.5f) < 1e-3f, "Garand lower-torso hit up close still kills (115 x 0.9 = 103.5)");
    check(std::fabs(dmDamage(tom, 10, DZ_HEAD) - 54) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_NECK) - 27) < 1e-3f && std::fabs(dmDamage(tom, 10, DZ_UPPER_THIGH) - 17.55f) < 1e-3f,
          "zones: Thompson head 54 (x2.0), neck counts as upper torso (27), upper thigh as limb (17.55)");
    // ================================================================ 2. movement data
    {
      auto& m = app.move;
      float g0 = 7.848f, v0 = 3.757f, h0 = v0 * v0 / (2 * g0), t0 = 2 * v0 / g0;
      float h = m.jumpVelocity * m.jumpVelocity / (2 * m.gravity), t = 2 * m.jumpVelocity / m.gravity;
      char b[300]; std::snprintf(b, sizeof b, "movement.json: player g %.4f m/s^2 (x%.4f of 7.848), jump %.4f m/s; analytic apex %.4f m (x%.4f), airtime %.4f s (x%.4f)", m.gravity, m.gravity / g0, m.jumpVelocity, h, h / h0, t, t / t0);
      check(std::fabs(m.gravity / g0 - 2.13333f) < 1e-3f && std::fabs(h / h0 - 1.2f) < 1e-3f && std::fabs(t / t0 - 0.75f) < 1e-3f && std::fabs(m.jumpVelocity - std::sqrt(2 * m.gravity * 1.2f * h0)) < 2e-3f, b);
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
      float g0 = 7.848f, v0 = 3.757f, hT = 1.2f * v0 * v0 / (2 * g0), tT = 0.75f * 2 * v0 / g0;
      char b[260]; std::snprintf(b, sizeof b, "measured jump: apex %.4f m (target %.4f, %+.2f %%), airtime %.4f s (target %.4f, %+.2f %%); old apex 0.8993 m, airtime 0.9574 s",
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
        bool all = L.isProtected(G.t); for (int s : bots) all &= G.sold[s].isProtected(G.t) && std::fabs(G.sold[s].protectUntil - G.t - 3.0) < 0.1;
        char b[200]; std::snprintf(b, sizeof b, "every soldier spawns protected; the player for %.4f s from its spawn step (t=%.3f)", L.protectUntil - tSpawn, tSpawn);
        check(all && tSpawn >= 0 && std::fabs(L.protectUntil - tSpawn - 3.0) < 1e-6, b);
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
        if (hpBullet == 0) { hpBullet = L.hp; gDmLastHit.valid = false; vec3 c = jointW(L, CJ_SPINE2); shoot(a, c + vec3(0, 0, 8), c, 285, "45_mag20", A); return false; }
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
      char b[220]; std::snprintf(b, sizeof b, "player protection lasted %.4f s (3.0 s +- one 1/60 s frame), %d protected frames", held, protFrames);
      check(std::fabs(held - 3.0) <= kDt + 1e-4, b);
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
        {"upper torso, Thompson 10 m", 1, 0.75f, 0, 10, "45_mag20", 285, "m1_thompson", DZ_UPPER_TORSO, false},
        {"lower torso, Thompson 10 m", 1, 0.25f, 0, 10, "45_mag20", 285, "m1_thompson", DZ_LOWER_TORSO, false},
        {"thigh, Thompson 10 m", 2, 0.65f, 0, 10, "45_mag20", 285, "m1_thompson", DZ_LIMB, false},
        {"helmet (back of the head), Thompson 10 m", 0, 0, 0.20f, 10, "45_mag20", 285, "m1_thompson", DZ_HEAD, true},
        {"head below the helmet rim (side, ear level), Thompson 10 m", 0, 0, 0.05f, 10, "45_mag20", 285, "m1_thompson", DZ_HEAD, false, true},
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
    // ---- a respawn restarts protection; the player's own shot ends it (real input path)
    push("respawn + own shot", [this](App& a, float t) {
      Game& G = *gGame; Soldier& L = *G.local();
      static int life, ph, shots0, fired0; static double tStart;
      if (t == 0) { life = L.lifeId; ph = 0; G.killSoldier(L.slot, -1, vec3(0, 0, -1)); return false; }
      if (ph == 0) {
        if (!(L.alive && L.lifeId != life)) return t > 25 ? (check(false, "player respawned"), true) : false;
        tStart = G.t;
        check(L.isProtected(G.t) && std::fabs(L.protectUntil - G.t - 3.0) < 1e-6, "respawn: protection starts again (3.0 s)");
        ph = 1; return false;
      }
      if (ph == 1) { if (G.t - tStart < 0.5) return false; shots0 = a.shotsFired; fired0 = gRulesStats.endedByFire; ph = 2; }
      if (ph == 2) {
        a.scriptedInput.fire = a.shotsFired == shots0;
        if (a.shotsFired == shots0) return G.t - tStart > 2.8 ? (check(false, "the player's weapon fired"), a.scriptedInput.fire = false, true) : false;
        a.scriptedInput.fire = false; ph = 3; return false;   // the round's first segment runs on this frame's step
      }
      check(!L.isProtected(G.t) && gRulesStats.endedByFire > fired0 && G.t - tStart < 2.9,
            "the player's own shot (" + a.weaponDefs[a.activeWeapon]->id + ", real input path) ended protection " + f2((float)(L.protectUntil - tStart)) + " s after the respawn");
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
