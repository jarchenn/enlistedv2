// --selftest-weapons: Milestone 4 — eight weapon entries, ammunition accounting, sights, recoil, crates.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include "settings.h"
#include "game.h"
#include "mpui.h"
#include "ai_core.h"
#include "scenario.h"
#include "abilities.h"
#include "classes.h"
#include "minimap.h"
#include <functional>
#include <cstdlib>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> wseq; size_t wcur = 0; float wt = 0;
uint64_t F = 0, K = 0, KP = 0, M2 = 0;
int shotsAtStart = 0;
WeaponState& S(App& a, const char* id) { return a.wstate[id]; }
const WeaponDef& D(App& a) { return *a.weaponDefs[a.activeWeapon]; }
int total(App& a, const char* id) { auto& s = S(a, id); return s.loaded + s.reserveRounds(); }
void face(App& a, vec3 target) {
  vec3 e = a.eyePos();
  a.eyeYaw = degrees(std::atan2(-(target.x - e.x), -(target.z - e.z)));
  a.eyePitch = degrees(std::atan2(target.y - e.y, glm::length(vec2(target.x - e.x, target.z - e.z))));
}
// hold / pulse the trigger until `n` rounds have left the active weapon (semi: pulses, auto: held)
std::function<bool(App&, float)> fireN(int n, float timeout = 20) {
  return [n, timeout](App& a, float t) {
    auto& in = a.scriptedInput;
    static int start = 0;
    if (t == 0) start = a.shotsFired;
    if (a.shotsFired - start >= n) { in.fire = false; return true; }
    bool autoW = D(a).action == "auto";
    in.fire = autoW ? true : !in.fire;
    return t > timeout;
  };
}
// FT1: a Solo match on the village (the player + one AI, a soldier or a zombie) for the flame damage tests
bool flameMatch(App& a, AiType ai) {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / "examples" / "village_assault_3.nmap.json");
  Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
  sc.rules.mode = GameMode::Solo; sc.rules.killCap = 999; sc.rules.soloPlayers = 2; sc.rules.respawnDelay = 2.0f; sc.rules.aiType[0] = ai;
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
bool flameOpenPoint(Game& G, vec3 from, float dist, vec3& out) {
  for (int k = 0; k < 24; ++k) {
    float an = k * 0.2618f; vec3 p = from + vec3(std::cos(an), 0, std::sin(an)) * dist; NavRef r; vec3 on;
    if (!G.nav.closest(p, 2.0f, r, on) || glm::length(vec2(on.x - p.x, on.z - p.z)) > 1.0f || std::fabs(on.y - from.y) > 2.0f) continue;
    if (!G.lineOfSight(from + vec3(0, 1.4f, 0), on + vec3(0, 1.4f, 0))) continue;
    out = on; return true;
  }
  return false;
}
void flamePlace(Soldier& s, vec3 p) { if (s.localPlayer) Physics::setCharacterPos(p); else if (s.agent >= 0) Physics::setAgentPos(s.agent, p); s.feet = p; s.vel = vec3(0); }
Soldier* flameOther(Game& G) { for (auto& s : G.sold) if (!s.human && s.alive) return &s; return nullptr; }
std::function<bool(App&, float)> wait(float s) { return [s](App& a, float t) { a.scriptedInput = ExploreInput{}; return t >= s; }; }
}

void SelfTest::weaponsUpdate(App& app, float dt) {
  auto push = [&](const char* n, std::function<bool(App&, float)> f) { wseq.push_back({n, f}); };
  if (stage == 700) {
    reportName = "weapons";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Weapons range");
    F = app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(0, 0, -40), 0.0f);
    K = app.placeInstance(app.lib.find("crate_mixed_supply"), vec3(2.5f, 0, 2.0f), 0.0f);
    KP = app.placeInstance(app.lib.find("crate_pak40_he"), vec3(-2.5f, 0, 2.0f), 0.0f);
    M2 = app.placeInstance(app.lib.find("gun_m2hb_tripod"), vec3(5.0f, 0, 3.5f), 180.0f);
    check(F && K && KP && M2, "range: farmhouse target wall at 40 m, mixed crate, PaK crate, M2HB on tripod");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(app.enterExplore(), "F5"); app.useScripted = true;
    wseq.clear(); wcur = 0; wt = 0;
    // ---- inventories from the brief's table
    push("inventories", [this](App& a, float) {
      struct E { const char* id; int l, r; } tab[] = {{"m1_garand", 8, 72}, {"kar98k", 5, 55}, {"mp40", 32, 160}, {"m1_thompson", 50, 150}, {"m1c_garand", 8, 72}, {"kar98k_zf39", 5, 55}, {"mg42", 50, 200}};
      for (auto& e : tab) {
        auto& s = S(a, e.id);
        check(a.weaponIndex(e.id) >= 0 && s.loaded == e.l && s.reserveRounds() == e.r, std::string(e.id) + " starts " + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + " (table " + std::to_string(e.l) + " + " + std::to_string(e.r) + ")");
      }
      StationRT* m = a.station(M2);
      check(m && m->loaded == 110 && m->reserve == 110, "M2HB emplacement starts 110 + 110 = 220");
      check(S(a, "mp40").containers.size() == 5 && S(a, "m1_thompson").containers.size() == 3 && S(a, "mg42").containers.size() == 4, "reserve held as real containers (MP 40 5 mags, Thompson 3 mags, MG 42 4 belts)");
      return true; });
    // ---- iron sights line up with the aim; shot lands under the sight line at the declared zero
    push("garand ads", [this](App& a, float t) {
      if (t == 0) a.selectWeapon(a.weaponIndex("m1_garand"));
      Instance* f = a.doc.find(F);
      face(a, vec3(f->pos.x, f->pos.y + 1.6f, f->pos.z + 4.5f));
      a.scriptedInput.aim = true;
      if (t < 1.0f) return false;   // ADS blend 0.25 s + spring settle
      const WeaponDef& d = D(a); auto& s = S(a, "m1_garand");
      mat4 M = a.viewModelMatrix(d, s), vp = a.projM * a.viewM;
      auto scr = [&](vec3 l) { vec4 c = vp * (M * vec4(l, 1)); return vec2(c.x / c.w, c.y / c.w); };
      vec2 r = scr(d.rear), fr = scr(d.front);
      float px = glm::length(r) * 540.0f, pf = glm::length(fr) * 540.0f;
      char b[160]; std::snprintf(b, sizeof b, "ADS: rear aperture and front post on the aim axis (%.1f px, %.1f px from centre)", px, pf);
      check(px < 3 && pf < 3, b);
      float fov = 0; check(a.weaponCamera(fov) && fov < 72, "ADS narrows the field of view");
      return true; });
    push("garand zero shot", [this](App& a, float t) {
      static vec3 aimPt; static float t0;
      if (t == 0) { Physics::Hit h; vec3 e = a.eyePos(); Physics::castRayEx(e, App::forward(a.eyeYaw, a.eyePitch), 200, h); aimPt = h.pos; t0 = a.lastImpact.time; shotsAtStart = a.shotsFired; }
      a.scriptedInput.aim = true;
      if (a.shotsFired == shotsAtStart) { a.scriptedInput.fire = true; return false; }
      a.scriptedInput.fire = false;
      if (a.lastImpact.time <= t0) return t > 3;
      float err = glm::length(a.lastImpact.pos - aimPt);
      char b[160]; std::snprintf(b, sizeof b, "40 m shot lands %.2f m from the point under the sights (impact feedback visible)", err);
      check(err < 0.2f, b);
      shotQueue.push_back("50_garand_ads_impact");
      return true; });
    push("garand empty clip", fireN(7));
    push("garand ping", [this](App& a, float t) {
      if (t < 0.3f) return false;
      auto& s = S(a, "m1_garand");
      check(s.loaded == 0 && s.clipPings == 1 && total(a, "m1_garand") == 72, "8 rounds fired: clip ejects with a ping, 0 + 72 left");
      return true; });
    // ---- the crate refills the held weapon to its allowance: a full clip in the rifle plus a full reserve
    push("crate garand", [this](App& a, float t) {
      int stock0 = a.crateStock[K]["30-06_clip"];
      std::string m = a.crateTransaction(K);
      auto& s = S(a, "m1_garand");
      check(s.loaded == 8 && s.reserveRounds() == 72 && a.crateStock[K]["30-06_clip"] == stock0 - 1, "crate refills the Garand to max: 8 + 72, one clip from the crate (" + m + ")");
      int stock = a.crateStock[K]["30-06_clip"];
      std::string m2 = a.crateTransaction(K);
      check(a.crateStock[K]["30-06_clip"] == stock && total(a, "m1_garand") == 80, "full reserve leaves the crate stock untouched (" + m2 + ")");
      check(a.crateStock[K]["50bmg_belt"] == 6, ".50 links stay at the crate for the M2HB: a rifle cannot claim them");
      std::string m3 = a.crateTransaction(KP);
      check(m3.find("no 30-06_clip") != std::string::npos || m3.find("wrong feed") != std::string::npos, "PaK crate rejects the rifle feed type: " + m3);
      a.scriptedInput.reload = true;
      return true; });
    push("garand reload", [this](App& a, float t) {
      if (t < 2.6f) return false;
      auto& s = S(a, "m1_garand");
      check(s.loaded == 8 && total(a, "m1_garand") == 80, "R with a full rifle keeps 8 + 72");
      return true; });
    push("garand partial", fireN(3));
    push("garand partial reload", [this](App& a, float t) {
      if (t == 0) { a.scriptedInput.reload = true; return false; }
      if (t < 3.1f) return false;
      auto& s = S(a, "m1_garand");
      check(s.loaded == 8 && total(a, "m1_garand") == 77 && std::count(s.containers.begin(), s.containers.end(), 5) == 1, "partial reload ejects the 5-round clip and keeps it in reserve (8 + 69, total 77 after 3 shots)");
      return true; });
    // ---- switching while reloading moves nothing; stocks persist per weapon
    push("switch while reloading", [this](App& a, float t) {
      if (t == 0) { a.scriptedInput.fire = true; return false; }   // fire one so a reload is possible
      if (t < 0.2f) { a.scriptedInput.fire = false; return false; }
      if (t < 0.25f) { a.scriptedInput.reload = true; return false; }
      if (t < 1.0f) return false;
      int before = total(a, "m1_garand"); int l = S(a, "m1_garand").loaded;
      a.selectWeapon(a.weaponIndex("kar98k"));
      a.selectWeapon(a.weaponIndex("m1_garand"));
      check(total(a, "m1_garand") == before && S(a, "m1_garand").loaded == l && S(a, "m1_garand").reload <= 0, "switching mid-reload cancels it without moving rounds (" + std::to_string(l) + " loaded)");
      a.selectWeapon(a.weaponIndex("kar98k"));
      return true; });
    // ---- Kar98k: bolt cycle between shots; top-off differs from empty reload
    push("kar bolt", [this](App& a, float t) {
      static int s0;
      if (t == 0) { s0 = a.shotsFired; a.scriptedInput.fire = true; return false; }
      a.scriptedInput.fire = (int)(t * 60) % 2 == 0;   // hammer the trigger
      if (t < 0.6f) { if (t > 0.5f) check(a.shotsFired - s0 == 1, "Kar98k: no second shot before the bolt is worked (0.85 s)"); return false; }
      if (t < 2.0f) return false;
      a.scriptedInput.fire = false;
      check(a.shotsFired - s0 == 2 || a.shotsFired - s0 == 3, "Kar98k fires again only after each bolt cycle (" + std::to_string(a.shotsFired - s0) + " shots in 2 s)");
      return true; });
    push("kar top-off", [this](App& a, float t) {
      static int l0, tot0;
      if (t == 0) { l0 = S(a, "kar98k").loaded; tot0 = total(a, "kar98k"); a.scriptedInput.reload = true; return false; }
      auto& s = S(a, "kar98k");
      if (t < 0.1f) { check(s.reloadKind == 3, "partial Kar98k reload tops off single rounds"); return false; }
      if (s.reload > 0) return t > 6;
      check(s.loaded == 5 && total(a, "kar98k") == tot0, "topped off to 5 one round at a time (" + std::to_string(l0) + " -> 5), rounds conserved");
      return true; });
    push("kar empty", fireN(5, 12));
    push("kar strip", [this](App& a, float t) {
      if (t == 0) { a.scriptedInput.reload = true; return false; }
      auto& s = S(a, "kar98k");
      if (t < 0.1f) { check(s.reloadKind == 1, "empty Kar98k reload strips a 5-round clip"); return false; }
      if (t < 2.6f) return false;
      check(s.loaded == 5, "clip stripped into the magazine: 5 loaded");
      return true; });
    // ---- Kar98k: a shot drops out of ADS; aim waits for the bolt and the cooldown, then the held key re-enters
    push("kar ads drop", [this](App& a, float t) {
      static int ph, s0; static float tShot, tReady;
      auto& s = S(a, "kar98k");
      if (t == 0) { a.selectWeapon(a.weaponIndex("kar98k")); a.scriptedInput = ExploreInput{}; ph = 0; }
      a.scriptedInput.aim = true;
      if (ph == 0) {
        if (a.adsT < 1 || t < 0.8f) return t > 3 ? (check(false, "Kar98k reaches full ADS"), true) : false;
        float fov = 0; a.weaponCamera(fov);
        check(std::fabs(fov - 45) < 0.01f, "iron-sight Kar98k: ADS world FOV " + std::to_string(fov).substr(0, 5) + " (45, about x1.6 of 72)");
        s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 1; return false;
      }
      if (ph == 1) {
        if (a.shotsFired == s0) return t > 4 ? (check(false, "Kar98k fired from ADS"), true) : false;
        a.scriptedInput.fire = false; tShot = t;
        check(a.adsT == 0 && s.bolt > 0, "Kar98k shot: out of ADS at once (adsT " + std::to_string(a.adsT).substr(0, 4) + ") while the bolt is worked");
        ph = 2; return false;
      }
      if (ph == 2) {
        if (s.bolt > 0 || s.cooldown > 0) { if (a.adsT > 0) { check(false, "aim held: ADS stays out until the bolt and the cooldown are done"); ph = 3; } return false; }
        tReady = t; ph = 3; return false;
      }
      if (a.adsT < 1) return t - tShot > 4 ? (check(false, "Kar98k re-enters ADS"), true) : false;
      char b[200]; std::snprintf(b, sizeof b, "aim held through the bolt: ADS stays out for %.2f s (bolt %.2f, cooldown %.2f), then the rifle comes back up (full ADS %.2f s after the shot)",
                                tReady - tShot, a.weaponDefs[a.activeWeapon]->boltTime, 60.0f / a.weaponDefs[a.activeWeapon]->rpm, t - tShot);
      check(tReady - tShot > 0.8f && t - tReady < 0.4f, b);
      a.scriptedInput = ExploreInput{};
      a.selectWeapon(a.weaponIndex("mp40"));
      return true; });
    // ---- MP 40 vs Thompson cyclic rates; partial magazines kept
    push("mp40 burst", [this](App& a, float t) {
      static int s0; if (t == 0) s0 = a.shotsFired;
      a.scriptedInput.fire = true;
      if (t < 1.0f) return false;
      a.scriptedInput.fire = false;
      summary["mp40_rounds_per_s"] = a.shotsFired - s0;
      a.selectWeapon(a.weaponIndex("m1_thompson"));
      return true; });
    push("thompson burst", [this](App& a, float t) {
      static int s0; if (t == 0) s0 = a.shotsFired;
      a.scriptedInput.fire = true;
      if (t < 1.0f) return false;
      a.scriptedInput.fire = false;
      int mp = summary["mp40_rounds_per_s"], th = a.shotsFired - s0;
      summary["thompson_rounds_per_s"] = th;
      check(mp >= 8 && mp <= 10 && th >= 11 && th <= 13, "distinct cyclic rates: MP 40 " + std::to_string(mp) + "/s, M1 Thompson " + std::to_string(th) + "/s");
      return true; });
    push("thompson partial mag", [this](App& a, float t) {
      if (t == 0) { a.scriptedInput.reload = true; return false; }
      if (t < 2.9f) return false;
      auto& s = S(a, "m1_thompson");
      bool kept = std::find(s.containers.begin(), s.containers.end(), 50 - (int)summary["thompson_rounds_per_s"]) != s.containers.end();
      check(s.loaded == 50 && kept && total(a, "m1_thompson") == 200 - (int)summary["thompson_rounds_per_s"], "partial Thompson magazine goes back to the pouch with its rounds");
      a.selectWeapon(a.weaponIndex("mg42"));
      return true; });
    // ---- MG 42 bipod: supported bursts are easier to control
    push("mg42 unsupported", [this](App& a, float t) {
      if (t == 0) { a.eyePitch = 0; S(a, "mg42").recoilP = 0; }   // the rifle steps' climb (x1.5 recoil) can reach the 85 deg look clamp, which zeroes the MG's climb
      a.scriptedInput.fire = true;
      if (t < 0.5f) return false;
      summary["mg42_climb_unsupported_deg"] = S(a, "mg42").recoilP;
      a.scriptedInput.fire = false;
      return true; });
    push("mg42 settle", wait(1.5f));
    push("mg42 supported", [this](App& a, float t) {
      if (t == 0) a.eyePitch = 0;
      a.scriptedInput.crouch = true;
      if (t < 0.6f) return false;
      a.scriptedInput.fire = true;
      if (t < 1.1f) return false;
      float sup = S(a, "mg42").recoilP, uns = summary["mg42_climb_unsupported_deg"];
      summary["mg42_climb_supported_deg"] = sup;
      a.scriptedInput.fire = false;
      check(a.supported && sup < uns * 0.6f, "bipod-supported burst climbs far less (" + std::to_string(sup).substr(0, 4) + " vs " + std::to_string(uns).substr(0, 4) + " deg)");
      check(a.lastStance == STANCE_MOUNTED && std::fabs(a.lastSpreadDeg - a.handling.cone(WC_LMG, false, STANCE_MOUNTED)) < 1e-5f,
            "bipod deployed: mounted stance, hip cone " + std::to_string(a.lastSpreadDeg).substr(0, 5) + " deg (3.0 x 0.05)");
      {
        const WeaponDef& mg = *a.weaponDefs[a.weaponIndex("mg42")];
        float want = 0.70f * mg.rSupported;   // pre-overhaul crouched 0.70 x the weapon's own bipod factor, not the 0.5 fallback
        char b[200]; std::snprintf(b, sizeof b, "mounted recoil x%.4f = old crouched 0.70 x the MG 42's recoil.supported %.2f (handling.json mounted %.2f is only the fallback)",
                                  a.lastRecoilScale, mg.rSupported, a.handling.recoil[STANCE_MOUNTED]);
        check(mg.rSupported < 0.999f && std::fabs(a.lastRecoilScale - want) < 1e-5f, b);
      }
      shotQueue.push_back("51_mg42_tracers");
      return true; });
    push("mg42 empty", [this](App& a, float t) {
      a.scriptedInput.crouch = false;
      auto& s = S(a, "mg42");
      if (s.loaded > 0) { a.scriptedInput.fire = true; return t > 10; }
      a.scriptedInput.fire = false; a.scriptedInput.reload = true;
      return true; });
    push("mg42 reload", [this](App& a, float t) {
      auto& s = S(a, "mg42");
      if (t < 0.5f) { check(s.reload > 0, "belt change in progress (top cover open)"); return false; }
      if (t < 5.0f) return false;
      check(s.loaded == 50 && s.reserveRounds() == 150, "belt change complete: 50 + 150");
      int rifleBefore = total(a, "m1_garand");
      std::string m = a.crateTransaction(K);
      check(s.reserveRounds() == 200 && s.loaded == 50 && total(a, "m1_garand") == rifleBefore, "one belt segment from the crate: MG 42 reserve 150 -> 200, rifle clips unchanged (" + m + ")");
      return true; });
    // ---- tracers: MG 42 / BAR a red tracer every 3rd round, a short white-yellow streak on the others
    push("mg42 tracers", [this](App& a, float t) {
      static int last; static std::vector<int> kinds;
      if (t == 0) { kinds.clear(); last = a.shotsFired; a.scriptedInput = ExploreInput{}; }
      a.scriptedInput.fire = true;
      if (a.shotsFired != last) { if (a.shotsFired == last + 1 && !a.projectiles.empty()) kinds.push_back(a.projectiles.back().tracerKind); last = a.shotsFired; }
      if (kinds.size() < 9) return t > 3 ? (check(false, "MG 42 fired 9 single rounds"), a.scriptedInput.fire = false, true) : false;
      a.scriptedInput.fire = false;
      int first = -1; for (int k = 0; k < 3; ++k) if (kinds[k] == 1 && first < 0) first = k;
      bool ok = first >= 0; std::string seq;
      for (int k = 0; k < 9; ++k) { ok &= kinds[k] == (k % 3 == first ? 1 : 2); seq += std::to_string(kinds[k]); }
      const WeaponDef& bar = *a.weaponDefs[a.weaponIndex("m1918a2_bar")];
      bool barOk = bar.tracerEvery == 3 && bar.tracerKind(3) == 1 && bar.tracerKind(4) == 2 && bar.tracerKind(5) == 2 && bar.tracerKind(6) == 1;
      bool rifle = a.weaponDefs[a.weaponIndex("m1_garand")]->tracerKind(3) == 0;
      check(ok && barOk && rifle, "MG 42 burst tracer kinds " + seq + " (1 red every 3rd round, 2 white-yellow streak); BAR the same cadence; the Garand untraced");
      return true; });
    // ---- scoped variants: real magnification, offset M1C scope, single-round ZF39 loading
    push("m1c scope", [this](App& a, float t) {
      if (t == 0) a.selectWeapon(a.weaponIndex("m1c_garand"));
      a.scriptedInput.aim = true;
      if (t < 0.6f) return false;
      float fov = 0; a.weaponCamera(fov);
      check(std::fabs(fov - 72.0f / 2.5f) < 0.1f, "M1C: 2.5x scope through the projection (fov " + std::to_string(fov).substr(0, 4) + ")");
      check(a.weaponDefs[a.activeWeapon]->scopeEye.x < -0.02f, "M1C scope is offset left of the receiver (clip path clear)");
      shotQueue.push_back("52_m1c_scope");
      return true; });
    push("zf39", [this](App& a, float t) {
      if (t == 0) a.selectWeapon(a.weaponIndex("kar98k_zf39"));
      a.scriptedInput.aim = true;
      if (t < 0.6f) return false;
      float fov = 0; a.weaponCamera(fov);
      check(std::fabs(fov - 18.0f) < 0.1f && !a.weaponDefs[a.activeWeapon]->scopeIrons, "Kar98k ZF39: 4x, irons not offered (scope blocks the sight line)");
      a.scriptedInput.aim = false;
      return true; });
    push("zf39 fire", fireN(1, 3));
    push("zf39 reload", [this](App& a, float t) {
      if (t < 1.1f) return false;
      if (t < 1.15f) { a.scriptedInput.reload = true; return false; }
      check(S(a, "kar98k_zf39").reloadKind == 3 || S(a, "kar98k_zf39").loaded == 5, "ZF39 rifle reloads one cartridge at a time");
      if (t < 2.0f) return false;
      check(S(a, "kar98k_zf39").loaded == 5, "single round loaded: 5");
      return true; });
    // ---- strict D-Day preset hides the M1C
    push("strict preset", [this](App& a, float) {
      a.strictDDay = true;
      a.selectWeapon(a.weaponIndex("m1_garand"));
      a.selectWeapon(a.weaponIndex("m1c_garand"));
      check(a.weaponDefs[a.activeWeapon]->id == "m1_garand", "strict 6 June 1944 preset hides the later-war M1C");
      a.strictDDay = false;
      return true; });
    // ---- muzzle obstruction: eye sees past a wall corner, muzzle is behind it
    push("muzzle wall", [this](App& a, float t) {
      Instance* f = a.doc.find(F);
      if (t == 0) {
        // stand 0.35 m from the farmhouse front wall, facing along it toward the corner, then turn into the wall
        Physics::setCharacterPos(f->pos + vec3(-0.5f, 0, 4.5f + 0.32f + 0.05f));
        return false;
      }
      a.eyeYaw = 0; a.eyePitch = 0;     // facing -z: straight into the wall 0.35 m ahead
      if (t < 0.4f) return false;
      static int s0; static float t0;
      if (t < 0.42f) { s0 = a.shotsFired; t0 = a.lastImpact.time; a.scriptedInput.fire = true; return false; }
      a.scriptedInput.fire = false;
      if (a.lastImpact.time <= t0) return t > 2;
      float d = glm::length(a.lastImpact.pos - a.eyePos());
      check(d < 0.7f, "shot stopped at the wall beside the barrel (impact " + std::to_string(d).substr(0, 4) + " m from the eye; muzzle would be past the wall)");
      return true; });
    // ---- frame drops must not multiply rounds
    push("frame drop", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("mp40")); Physics::setCharacterPos(vec3(0, a.terrain.height(0, 0), 0)); }
      return true; });
    push("reload mp40", [this](App& a, float t) { if (t == 0) a.scriptedInput.reload = true; return t > 2.8f; });
    push("burst normal", [this](App& a, float t) { static int s0; if (t == 0) s0 = a.shotsFired; a.scriptedInput.fire = true; if (t < 0.5f) return false; summary["burst_60hz"] = a.shotsFired - s0; a.scriptedInput.fire = false; return true; });
    push("reload mp40 2", [this](App& a, float t) { if (t == 0) a.scriptedInput.reload = true; return t > 2.8f; });
    push("burst slow frames", [this](App& a, float t) {
      static int s0; if (t == 0) { s0 = a.shotsFired; slowFrames = true; }
      a.scriptedInput.fire = true;
      if (t < 0.5f) return false;
      slowFrames = false;
      int slow = a.shotsFired - s0, norm = summary["burst_60hz"];
      summary["burst_10fps"] = slow;
      a.scriptedInput.fire = false;
      check(std::abs(slow - norm) <= 1, "0.5 s burst: " + std::to_string(norm) + " rounds at 60 fps, " + std::to_string(slow) + " at 10 fps (no multiplied rounds)");
      return true; });
    // ---- dispersion and recoil by stance (content/weapons/handling.json)
    push("stance cones", [this](App& a, float) {
      const Handling& h = a.handling; bool ok = true;
      const int order[6] = {STANCE_SPRINTING, STANCE_MOVING, STANCE_STANDING, STANCE_CROUCHED, STANCE_PRONE, STANCE_MOUNTED};
      for (int c = 0; c < 4; ++c) for (int ads = 0; ads < 2; ++ads) {
        for (int k = 1; k < 6; ++k) ok &= h.cone(c, ads, order[k]) < h.cone(c, ads, order[k - 1]);
        ok &= h.cone(c, true, STANCE_MOVING) < h.cone(c, false, STANCE_MOVING);
      }
      check(ok && h.hip[WC_RIFLE] == 0.15f && h.ads[WC_RIFLE] == 0.03f && h.hip[WC_AUTO] == 3.0f && h.ads[WC_AUTO] == 0.45f && h.stance[STANCE_STANDING] == 0.8f && h.stance[STANCE_PRONE] == 0.08f,
            "cone order for every class, hip and ADS: sprinting > moving > standing > crouched > prone > mounted; ADS tighter than hip (rifle hip 0.15 / ADS 0.03, auto 3.0 / 0.45)");
      auto cls = [&](const char* id) { return a.weaponDefs[a.weaponIndex(id)]->wclass; };
      check(cls("kar98k") == WC_RIFLE && cls("m1_garand") == WC_RIFLE && cls("kar98k_zf39") == WC_SNIPER && cls("m1c_garand") == WC_SNIPER && cls("stg44") == WC_AUTO && cls("mp40") == WC_AUTO && cls("mg42") == WC_LMG && cls("m1918a2_bar") == WC_LMG,
            "weapon classes: Kar98k / Garand rifle, ZF39 / M1C sniper, StG 44 / MP 40 auto, MG 42 / BAR lmg");
      return true; });
    auto stanceShot = [this](const char* nm, int want, vec2 mv, bool crouch, bool prone) {
      return [this, nm, want, mv, crouch, prone](App& a, float t) {
        static int s0;
        auto& s = S(a, "mp40");
        if (t == 0) { a.selectWeapon(a.weaponIndex("mp40")); s.loaded = 32; a.scriptedInput = ExploreInput{}; a.proneToggle = prone; }
        a.scriptedInput.move = mv; a.scriptedInput.crouch = crouch;
        if (t < 1.8f) { s0 = a.shotsFired; return false; }
        a.scriptedInput.fire = true;
        if (a.shotsFired - s0 < 4) return t > 5 ? (check(false, std::string(nm) + ": MP 40 fired"), a.scriptedInput.fire = false, true) : false;
        float cone = a.handling.cone(WC_AUTO, false, want), yaw = s.recoilY;
        char b[220]; std::snprintf(b, sizeof b, "MP 40 hip, %s: stance %d (want %d), cone %.3f deg (want %.3f), recoil x%.2f, horizontal recoil %.3f deg after %d rounds",
                                  nm, a.lastStance, want, a.lastSpreadDeg, cone, a.lastRecoilScale, yaw, a.shotsFired - s0);
        bool yawOk = want == STANCE_STANDING || want == STANCE_PRONE ? yaw == 0 : want == STANCE_MOVING ? std::fabs(yaw) > 1e-4f : true;
        check(a.lastStance == want && std::fabs(a.lastSpreadDeg - cone) < 1e-5f && std::fabs(a.lastRecoilScale - a.handling.recoil[want]) < 1e-5f && yawOk, b);
        summary[std::string("cone_") + nm] = a.lastSpreadDeg;
        a.scriptedInput = ExploreInput{}; a.proneToggle = false;
        return true; };
    };
    push("stance standing", stanceShot("standing still (pitch-only recoil)", STANCE_STANDING, vec2(0), false, false));
    push("stance moving", stanceShot("moving (25 % of the yaw)", STANCE_MOVING, vec2(0, 1), false, false));
    push("stance crouched", stanceShot("crouched", STANCE_CROUCHED, vec2(0), true, false));
    push("stance prone", stanceShot("prone (laser-like)", STANCE_PRONE, vec2(0), false, true));
    push("stand up", wait(1.8f));
    // ---- ADS speed: StG 44 (and FG 42, BAR, M3) aim 1.2x faster than the default rate
    push("stg44 ads speed", [this](App& a, float t) {
      static int ph; static float t0, tG;
      if (t == 0) { ph = 0; a.selectWeapon(a.weaponIndex("m1_garand")); a.scriptedInput = ExploreInput{}; t0 = 0; return false; }
      if (ph == 0) { if (t < 0.5f) return false; a.scriptedInput.aim = true; t0 = t; ph = 1; return false; }
      if (ph == 1) { if (a.adsT < 1) return t > 3; tG = t - t0; a.scriptedInput.aim = false; a.selectWeapon(a.weaponIndex("stg44")); ph = 2; return false; }
      if (ph == 2) { a.scriptedInput.aim = true; t0 = t; ph = 3; return false; }
      if (a.adsT < 1) return t > 6;
      float tS = t - t0;
      bool data = true; for (const char* id : {"stg44", "fg42", "m1918a2_bar", "m3_grease_gun"}) data &= a.weaponDefs[a.weaponIndex(id)]->adsSpeed == 1.2f;
      data &= a.weaponDefs[a.weaponIndex("m1_garand")]->adsSpeed == 1.0f;
      char b[200]; std::snprintf(b, sizeof b, "ADS entry: Garand %.3f s, StG 44 %.3f s (ads.speed 1.2 on StG 44, FG 42, BAR, M3)", tG, tS);
      check(data && tS < tG * 0.9f && std::fabs(tG / tS - 1.2f) < 0.12f, b);
      a.scriptedInput = ExploreInput{};
      return true; });
    // ---- M2HB at the emplacement, .50 belts only there
    push("m2hb", [this](App& a, float t) {
      static std::vector<int> kinds; static int last;
      if (t == 0) { kinds.clear(); a.selectWeapon(a.weaponIndex("m1_garand")); check(a.occupyStation(M2, ST_GUNNER), "crew the M2HB from its spade grips"); last = a.station(M2)->loaded; return false; }
      StationRT* m = a.station(M2);
      if (m->loaded != last) { if (m->loaded == last - 1 && kinds.size() < 9 && !a.projectiles.empty()) kinds.push_back(a.projectiles.back().tracerKind); last = m->loaded; }
      if (m->loaded > 0) { a.scriptedInput.fire = true; return t > 30; }
      a.scriptedInput.fire = false;
      std::string seq; int red = 0; for (int k : kinds) { seq += std::to_string(k); red += k == 1; }
      check(kinds.size() == 9 && red == 3 && kinds[2] == 1 && kinds[5] == 1 && kinds[8] == 1, "M2HB emplacement tracer kinds " + seq + " (a red tracer every 3rd round, the others the short streak)");
      check(m->loaded == 0 && m->reserve == 110, "M2HB fired out its 110-round increment");
      return true; });
    push("m2hb reload", [this](App& a, float t) {
      StationRT* m = a.station(M2);
      if (t == 0) { a.scriptedInput.reload = true; return false; }
      if (t < 6.0f) return false;
      check(m->loaded == 110 && m->reserve == 0, "fresh belt at the gun: 110 + 0");
      a.scriptedInput.interact = true;
      return true; });
    push("m2hb crate", [this](App& a, float t) {
      if (t < 0.3f) return false;
      std::string m = a.crateTransaction(K);
      bool rifleFirst = m.rfind("Refilled", 0) == 0;   // the held rifle is served first; the next press restocks the gun
      if (rifleFirst) m = a.crateTransaction(K);
      StationRT* s = a.station(M2);
      check(s->reserve == 110 && a.crateStock[K]["50bmg_belt"] == 5,
            std::string(rifleFirst ? "after the rifle refill, " : "") + ".50 belt from the crate goes to the M2HB reserve only (" + m + ")");
      return true; });
    // ---- FT1 flamethrower (docs/flamethrower.md): data, ADS, fuel, reach, wall; NMK_FT1_SHOTS=1 also queues the look shots
    push("flame data", [this](App& a, float) {
      int us = a.weaponIndex("m2_flamethrower"), de = a.weaponIndex("flammenwerfer41");
      check(us >= 0 && de >= 0 && a.weaponDefs[us]->side == "US" && a.weaponDefs[de]->side == "DE" && a.weaponDefs[us]->name == "M2 flamethrower" && a.weaponDefs[de]->name == "Flammenwerfer 41",
            "both sides have a flamethrower: M2 flamethrower (US) and Flammenwerfer 41 (DE)");
      const WeaponDef& d = *a.weaponDefs[us];
      check(d.flame.on && d.action == "flame" && d.useModel() && d.model->tris < 12000 && d.cap == 100 && S(a, "m2_flamethrower").loaded == 100 && std::string(a.fireModeName(d, S(a, "m2_flamethrower"))) == "FLAME",
            "the flame gun: imported model (" + std::to_string(d.model ? d.model->tris : 0) + " tris, no tanks or hose), tank 100 %, fire mode text Flame");
      a.selectWeapon(us);
      return true; });
    push("flame ads", [this](App& a, float t) {
      const WeaponDef& d = D(a); auto& s = S(a, "m2_flamethrower");
      a.scriptedInput.aim = true;
      if (t < 1.0f) return false;
      float fov = 0; bool cam = a.weaponCamera(fov);
      float hip = settings().fov(), want = hip * (1.0f - d.adsZoom);
      float adsAt1 = a.adsT; vec3 pose1 = a.vmTarget(d); a.adsT = 0; vec3 pose0 = a.vmTarget(d); a.adsT = adsAt1;
      char b[200]; std::snprintf(b, sizeof b, "flamethrower ADS: world FOV %.1f -> %.1f deg (-%.0f %%), the weapon pose moves %.4f m, vm FOV unchanged", hip, fov, 100.0f * (1 - fov / hip), glm::length(pose1 - pose0));
      check(cam && std::fabs(fov - want) < 0.3f && std::fabs(d.adsZoom - 0.12f) < 1e-4f && glm::length(pose1 - pose0) < 1e-4f && a.vmFovNow() == a.vm.vmFov, b);
      (void)s; a.scriptedInput = ExploreInput{};
      return true; });
    if (std::getenv("NMK_FT1_SHOTS")) {
      push("flame shot hip idle", [this](App& a, float t) { face(a, vec3(0, 1.4f, 40)); if (t == 0) a.adsT = 0; if (t > 1.0f) { shotQueue.push_back("ft1_hip_idle"); return true; } return false; });
      push("flame shot hip fire", [this](App& a, float t) { face(a, vec3(0, 1.4f, 40)); a.scriptedInput.fire = true; if (t > 0.9f) { shotQueue.push_back("ft1_hip_fire"); return true; } return false; });
      push("flame shot ads fire", [this](App& a, float t) { face(a, vec3(0, 1.4f, 40)); a.scriptedInput.fire = true; a.scriptedInput.aim = true; if (t > 1.4f) { shotQueue.push_back("ft1_ads_fire"); a.scriptedInput = ExploreInput{}; return true; } return false; });
      push("flame shot settle", [this](App& a, float t) { a.scriptedInput = ExploreInput{}; S(a, "m2_flamethrower").loaded = 100; return t > 2.5f; });
    }
    push("flame fuel", [this](App& a, float t) {
      static float tEmpty; static int emitted0;
      auto& s = S(a, "m2_flamethrower");
      if (t == 0) { s.loaded = 100; s.flameFrac = 0; tEmpty = -1; a.flame.clear(&a); a.scriptedInput = ExploreInput{}; }
      face(a, vec3(0, 1.6f, 40));
      a.scriptedInput.fire = true;
      if (s.loaded <= 0 && tEmpty < 0) { tEmpty = t; emitted0 = a.flame.emitted; }
      if (tEmpty >= 0 && t < tEmpty + 1.0f) return false;
      if (tEmpty < 0) return t > 40;
      char b[200]; std::snprintf(b, sizeof b, "fuel: 100 %% lasts %.1f s of continuous fire (about 20 s), then the stream stops (%d parcels after empty, %d emitted)", tEmpty, a.flame.emitted - emitted0, a.flame.emitted);
      check(tEmpty > 18.5f && tEmpty < 21.5f && a.flame.emitted == emitted0 && a.flame.emitted > 400, b);
      a.scriptedInput = ExploreInput{};
      return true; });
    push("flame reach", [this](App& a, float t) {
      auto& s = S(a, "m2_flamethrower");
      if (t == 0) { s.loaded = 100; a.flame.clear(&a); }
      a.eyeYaw = 180; a.eyePitch = 3;   // open ground behind the start: nothing within 30 m
      a.scriptedInput.fire = true;
      if (t < 2.0f) return false;
      a.scriptedInput = ExploreInput{};
      float r = a.flame.farthest;
      char b[160]; std::snprintf(b, sizeof b, "the stream reaches %.1f m (20 m yes, 30 m no): %d parcels", r, a.flame.emitted);
      check(r >= 20.0f && r < 25.0f, b);
      return true; });
    push("flame wall", [this](App& a, float t) {
      static float dWall;
      auto& s = S(a, "m2_flamethrower");
      if (t == 0) {   // 12 m from the farmhouse wall
        Instance* f = a.doc.find(F); Physics::Hit h; vec3 from(0, a.terrain.height(0, 0) + 1.6f, 0);
        face(a, vec3(f->pos.x, f->pos.y + 1.6f, f->pos.z + 4.5f));
        Physics::castRayEx(from, App::forward(a.eyeYaw, 0), 80, h); dWall = h.t;
        vec3 at = from + App::forward(a.eyeYaw, 0) * (h.t - 12.0f); Physics::setCharacterPos(vec3(at.x, a.terrain.height(at.x, at.z), at.z));
        s.loaded = 100; a.flame.clear(&a);
        return false;
      }
      a.eyePitch = 0;
      a.scriptedInput.fire = true;
      if (t < 2.0f) return false;
      a.scriptedInput = ExploreInput{};
      float r = a.flame.farthest;
      char b[200]; std::snprintf(b, sizeof b, "a wall blocks the stream: the wall is 12 m away, the farthest parcel got %.1f m (%d stopped on it)", r, a.flame.wallStops);
      check(r > 10.0f && r < 12.7f && a.flame.wallStops > 20, b);
      return true; });
    // ---- GM2 (docs/abilities.md, docs/minimap.md): explosion pack, artillery, suppression vignette, minimap mapping
    push("gm2 classes", [this](App& a, float) {
      const ClassDef* heavy = classById("heavy"); const ClassDef* ranger = classById("ranger"); const ClassDef* recon = classById("recon");
      bool ok = heavy && ranger && recon;
      int hp = 0; bool rArty = false, hArty = true, recPack = true;
      if (ok) {
        gm2ApplyClass(a, *heavy); hp = S(a, "explosion_pack").loaded + (int)S(a, "explosion_pack").containers.size(); hArty = gm2HasAbility("artillery");
        gm2ApplyClass(a, *ranger); rArty = gm2HasAbility("artillery");
        gm2ApplyClass(a, *recon); recPack = std::find(a.carried.begin(), a.carried.end(), a.weaponIndex("explosion_pack")) != a.carried.end();
        gm2Reset(a);
      }
      check(ok && heavy->packs == 2 && hp == 2 && !hArty && rArty && ranger->ability == "artillery" && !recPack,
            "classes.json drives GM2: Heavy gets 2 explosion packs and no artillery; Ranger gets artillery; Recon carries no pack");
      return true; });
    push("gm2 pack grant", [this](App& a, float) {
      int w = a.weaponIndex("explosion_pack");
      bool inDefault = w >= 0 && std::find(a.carried.begin(), a.carried.end(), w) != a.carried.end();
      bool ok = gm2Grant(a, "explosion_pack", 2);
      const WeaponDef* d = w >= 0 ? a.weaponDefs[w].get() : nullptr;
      check(d && d->pack.on && d->equipment() && d->pack.fuse == 10.0f && !inDefault && ok && S(a, "explosion_pack").loaded + (int)S(a, "explosion_pack").containers.size() == 2,
            "explosion pack: a weapon id (equipment, 10 s fuse), not in the default loadout, a grant gives 2");
      a.selectWeapon(w); a.scriptedInput = ExploreInput{};
      return true; });
    push("gm2 pack no surface", [this](App& a, float t) {
      static int placed0;
      if (t == 0) { placed0 = gm2Stats().packsPlaced; a.eyeYaw = 0; a.eyePitch = 60; }   // up at the sky: nothing in reach
      a.scriptedInput.fire = t > 0.1f && t < 0.2f;
      if (t < 0.5f) return false;
      a.scriptedInput = ExploreInput{};
      check(gm2Stats().packsPlaced == placed0 && S(a, "explosion_pack").loaded + (int)S(a, "explosion_pack").containers.size() == 2, "explosion pack: nothing in reach (sky) places nothing and keeps the count");
      return true; });
    push("gm2 pack place", [this](App& a, float t) {
      static int placed0, ex0; static vec3 at; static uint32_t rev0;
      if (t == 0) { placed0 = gm2Stats().packsPlaced; ex0 = gm2Stats().packsExploded; a.eyeYaw = 0; a.eyePitch = -55; rev0 = a.groundRev; }   // down at the ground in front
      a.scriptedInput.fire = t > 0.1f && t < 0.2f;
      if (t < 0.4f) return false;
      a.scriptedInput.fire = false;
      if (gm2Stats().packsPlaced == placed0 + 1 && !gm2Packs().empty() && at == vec3(0)) at = gm2Packs().back().pos;
      if (t < 9.9f) {   // still ticking
        if (gm2Stats().packsExploded != ex0) { check(false, "explosion pack: went off before its 10 s fuse"); return true; }
        return false;
      }
      if (t < 10.6f) return false;
      const LivePack* none = gm2Packs().empty() ? nullptr : &gm2Packs().back(); (void)none;
      float gy = a.terrain.height(at.x, at.z);
      char b[240]; std::snprintf(b, sizeof b, "explosion pack: placed on the ground in reach (%.2f m above the ground, %.1f m away), one left; went off once 10 s later (packs %d, blasts %d), crater (ground rev %u -> %u)",
                                 at.y - gy, glm::length(vec2(at.x, at.z) - vec2(a.eyePos().x, a.eyePos().z)), gm2Stats().packsPlaced - placed0, gm2Stats().packsExploded - ex0, rev0, a.groundRev);
      check(gm2Stats().packsPlaced == placed0 + 1 && gm2Stats().packsExploded == ex0 + 1 && std::fabs(at.y - gy) < 0.35f && gm2Packs().empty() && a.groundRev != rev0 &&
            S(a, "explosion_pack").loaded + (int)S(a, "explosion_pack").containers.size() == 1, b);
      a.eyePitch = 0;
      return true; });
    push("gm2 artillery", [this](App& a, float t) {
      static int land0; static bool second;
      if (t == 0) {
        std::string why;
        bool refused = !gm2CallArtillery(a, vec2(0, -40), -1, &why);
        gm2Grant(a, "artillery");
        land0 = gm2Stats().shellsLanded;
        bool called = gm2CallArtillery(a, vec2(0, -40), -1, &why);
        second = gm2CallArtillery(a, vec2(0, -40), -1, &why);
        check(refused && called && !second && gm2ArtyCooldownLeft(a) > 89.0f, "artillery: refused without the ability; granted, a call is accepted; a second call is refused (90 s cooldown)");
        return false;
      }
      const ArtyConfig& c = gm2Arty();
      if (t < c.delay - 0.2f) { if (gm2Stats().shellsLanded != land0) { check(false, "artillery: a shell landed before the delay"); return true; } return false; }
      if (t < c.delay + c.gap * c.shells + 0.6f) return false;
      vec3 lb = gm2Stats().lastBlast;
      char b[200]; std::snprintf(b, sizeof b, "artillery: %d shells landed after the %.0f s delay; the last %.1f m from the target (spread %.0f m)", gm2Stats().shellsLanded - land0, c.delay, glm::length(vec2(lb.x, lb.z) - vec2(0, -40)), c.spread);
      check(gm2Stats().shellsLanded - land0 == c.shells && glm::length(vec2(lb.x, lb.z) - vec2(0, -40)) <= c.spread + 0.5f, b);
      return true; });
    push("gm2 suppression", [this](App& a, float t) {
      static int nm0; static float one, burst; static bool shotBurst;
      auto shoot = [&](float side) {   // an enemy round passing the eye `side` m to the right, flying +z
        vec3 e = a.eyePos(); Projectile p; p.kind = PROJ_BULLET; p.owner = 7; p.p = e + vec3(side, 0, -6); p.v = vec3(0, 0, 400); p.life = 0.001f; a.projectiles.push_back(p);
      };
      if (t == 0) { nm0 = gm2Stats().nearMisses; one = burst = 0; shotBurst = false; shoot(0.8f); return false; }
      if (!shotBurst && t > 0.3f) { shotBurst = true; one = gm2Suppression(); for (int k = 0; k < 4; ++k) shoot(0.6f + 0.2f * k); return false; }
      if (burst == 0 && t > 0.6f) { burst = std::max(1e-4f, gm2Suppression()); return false; }
      if (t < 2.8f) return false;
      char b[220]; std::snprintf(b, sizeof b, "suppression: near misses %d (5 rounds past the eye); one round -> vignette %.2f, then a burst of 4 -> %.2f; 2 s after the fire stops %.2f",
                                 gm2Stats().nearMisses - nm0, one, burst, gm2Suppression());
      check(gm2Stats().nearMisses - nm0 == 5 && one > 0.1f && one < 0.5f && burst > one + 0.3f && gm2Suppression() < 0.05f, b);
      return true; });
    push("gm2 minimap", [this](App& a, float) {
      MinimapView v = drawMinimap(a, nullptr, vec2(100, 100), vec2(300, 300), vec2(10, -20), 200.0f);
      vec2 w(35, -60), back = v.toWorld(v.toScreen(w));
      MinimapView f = drawMinimap(a, nullptr, vec2(0, 0), vec2(400, 400), vec2(0), 0);
      vec2 c0 = f.toScreen(a.terrain.org), c1 = f.toScreen(a.terrain.org + vec2(a.terrain.size, a.terrain.sizeZ));
      check(glm::length(back - w) < 1e-3f && std::fabs(v.scale - 1.0f) < 1e-4f && glm::length(v.toScreen(vec2(10, -20)) - vec2(200, 200)) < 1e-3f && c0.x >= -0.5f && c1.x <= 400.5f && c1.y <= 400.5f,
            "minimap: 200 m across a 200 px rect = 1 px/m, centred; screen <-> world round trip; metres <= 0 fits the whole terrain");
      return true; });
    push("reset", [this](App& a, float) {
      a.exitExplore();
      check(S(a, "m1_garand").loaded == 8 && total(a, "m1_garand") == 80 && a.station(M2)->loaded == 110 && a.crateStock[K]["30-06_clip"] == 30, "return to Creative resets weapons, the M2HB and crate stock (test consumption not persisted)");
      return true; });
    // ---- FT1 in a match: the stream burns a soldier and a zombie, the burn outlasts the stream and ends; a bot carrying one closes in and fires bursts
    for (int zombie = 0; zombie < 2; ++zombie) {
      push(zombie ? "flame burn zombie" : "flame burn soldier", [this, zombie](App& a, float t) {
        static int ph; static float t0, hpAfter, hpLate; static int ti; static vec3 tp;
        if (t == 0) { ph = 0; ti = -1; check(flameMatch(a, zombie ? AiType::Zombies : AiType::Soldiers) && gGame->active, std::string("Solo match: the player and one ") + (zombie ? "zombie" : "soldier")); return false; }
        Game& G = *gGame; Soldier* L = G.local();
        if (L && L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
        if (ph == 0) {
          if (t < 1.5f || !L || !L->alive || !flameOther(G)) return t > 25;
          Soldier* o = flameOther(G); ti = o->slot; if (!flameOpenPoint(G, L->feet, 9.0f, tp)) { check(false, "an open point 9 m from the player"); return true; }
          if (aiCore()) aiCore()->frozen = true;
          flamePlace(*o, tp); o->protectUntil = -1; o->hp = 100;
          a.selectWeapon(a.weaponIndex("m2_flamethrower")); S(a, "m2_flamethrower").loaded = 100;
          ph = 1; t0 = t; return false;
        }
        Soldier& o = G.sold[ti];
        vec3 c = G.chest(o); vec3 e = a.eyePos();
        a.eyeYaw = degrees(std::atan2(-(c.x - e.x), -(c.z - e.z))); a.eyePitch = degrees(std::atan2(c.y - e.y, glm::length(vec2(c.x - e.x, c.z - e.z)))) + 1.0f;
        a.scriptedInput.fire = ph == 1 && t - t0 < 0.3f;   // a 0.3 s burst
        if (ph == 1 && t - t0 > 1.2f) { hpAfter = o.hp; check(o.hp < 100.0f && o.burnT > 0.5f && o.alive, "the stream burns a " + std::string(zombie ? "zombie" : "soldier") + ": " + std::to_string((int)o.hp) + " hp left and " + std::to_string(o.burnT).substr(0, 3) + " s of burn to go"); ph = 2; t0 = t; return false; }
        if (ph == 2 && t - t0 > 1.5f) { hpLate = o.hp; check(hpLate < hpAfter - 8.0f && o.alive, "the burn keeps damaging after it left the stream: " + std::to_string((int)hpAfter) + " -> " + std::to_string((int)hpLate) + " hp with the trigger released"); ph = 3; t0 = t; return false; }
        if (ph == 3 && t - t0 > 2.0f) {   // the burn is over (3 s): no more damage
          float h0 = o.hp; ph = 4; t0 = t; hpAfter = h0; return false;
        }
        if (ph == 4 && t - t0 > 1.0f) { check(std::fabs(o.hp - hpAfter) < 1.0f && o.burnT <= 0.0f, "the burn ends after about 3 s (hp steady at " + std::to_string((int)o.hp) + ")"); a.scriptedInput = ExploreInput{}; return true; }
        return t > 40;
      });
    }
    push("flame bot", [this](App& a, float t) {   // a bot with the flame gun closes inside ~18 m and fires a burst; third-person look
      static int ph; static float t0, d0; static int bi; static vec3 fwd;
      if (t == 0) { ph = 0; d0 = -1; check(flameMatch(a, AiType::Soldiers) && gGame->active, "Solo match: the player and a soldier carrying the flame gun"); return false; }
      Game& G = *gGame; Soldier* L = G.local();
      if (L && L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
      if (ph == 0) {
        Soldier* b = flameOther(G);
        if (t < 1.5f || !L || !L->alive || !b) return t > 25;
        bi = b->slot; vec3 p; if (!flameOpenPoint(G, L->feet, 30.0f, p)) { check(false, "an open point 30 m from the player"); return true; }
        flamePlace(*b, p); b->protectUntil = -1;
        b->weapon = a.weaponIndex(b->faction == Faction::Allies ? "m2_flamethrower" : "flammenwerfer41"); b->ws = WeaponState{}; b->ws.loaded = 100; b->ws.seed = 7;
        a.scriptedInput = ExploreInput{}; ph = 1; t0 = t; return false;
      }
      Soldier& b = G.sold[bi];
      vec3 c = G.chest(b), e = a.eyePos();
      a.eyeYaw = degrees(std::atan2(-(c.x - e.x), -(c.z - e.z))); a.eyePitch = degrees(std::atan2(c.y - e.y, glm::length(vec2(c.x - e.x, c.z - e.z))));
      if (ph == 1) {
        if (a.flame.open(b.slot) && d0 < 0) { d0 = glm::length(vec2(b.feet.x - L->feet.x, b.feet.z - L->feet.z)); t0 = t; if (std::getenv("NMK_FT1_SHOTS")) { ph = 2; if (aiCore()) aiCore()->frozen = true; fwd = glm::normalize(vec3(L->feet.x - b.feet.x, 0, L->feet.z - b.feet.z)); } else ph = 3; }
        return t > 60 ? (check(false, "the bot never opened its flame gun within 60 s"), true) : false;
      }
      if (ph == 2) {   // the bot (AI frozen) sprays along `fwd`; the player stands 5 m to its right, 3 m behind its nozzle
        vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
        if (t - t0 > 0.05f && t - t0 < 0.1f) { vec3 p = b.feet - fwd * 3.0f + right * 5.0f; flamePlace(*L, vec3(p.x, G.nav.valid() ? p.y : b.feet.y, p.z)); }
        b.br.aimYaw = degrees(std::atan2(-fwd.x, -fwd.z)); b.yaw = b.br.aimYaw; b.br.aimPitch = 0; b.ws.loaded = std::max(b.ws.loaded, 60);
        G.soldierFlame(b, fwd + vec3(0, 0.03f, 0), 1.0f / 60);
        vec3 c2 = G.chest(b) + fwd * 1.5f, e2 = a.eyePos();
        a.eyeYaw = degrees(std::atan2(-(c2.x - e2.x), -(c2.z - e2.z))); a.eyePitch = degrees(std::atan2(c2.y - e2.y, glm::length(vec2(c2.x - e2.x, c2.z - e2.z))));
        if (t - t0 > 0.9f) { shotQueue.push_back("ft1_third_person"); ph = 3; t0 = t; }
        return false;
      }
      if (ph == 3 && t - t0 > 0.8f) {
        char bs[200]; std::snprintf(bs, sizeof bs, "the bot closed to %.1f m before it opened fire (reach 18 m), spent %d %% of its fuel in the burst, %d parcels", d0, 100 - b.ws.loaded, a.flame.emitted);
        check(d0 > 0 && d0 < 19.5f && b.ws.loaded < 100, bs);
        if (aiCore()) aiCore()->frozen = false;
        return true;
      }
      return false;
    });
    push("flame match end", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
  }
  stage = 701;
  if (std::getenv("NMK_FT1_ONLY") && wt == 0 && wcur == 0) wseq.erase(std::remove_if(wseq.begin(), wseq.end(), [](const Step& s) { return s.name.rfind("flame", 0) != 0; }), wseq.end());   // dev: just the flamethrower steps
  if (wcur >= wseq.size()) { finish(app); return; }
  if (wt == 0) logf("[selftest] step: %s", wseq[wcur].name.c_str());
  bool d2 = wseq[wcur].fn(app, wt);
  wt += dt;
  if (d2) { ++wcur; wt = 0; }
}
