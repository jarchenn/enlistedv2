// --selftest-weapons: Milestone 4 — eight weapon entries, ammunition accounting, sights, recoil, crates.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

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
      struct E { const char* id; int l, r; } tab[] = {{"m1_garand", 8, 72}, {"kar98k", 5, 55}, {"mp40", 32, 160}, {"m1_thompson", 20, 80}, {"m1c_garand", 8, 72}, {"kar98k_zf39", 5, 55}, {"mg42", 50, 200}};
      for (auto& e : tab) {
        auto& s = S(a, e.id);
        check(a.weaponIndex(e.id) >= 0 && s.loaded == e.l && s.reserveRounds() == e.r, std::string(e.id) + " starts " + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + " (table " + std::to_string(e.l) + " + " + std::to_string(e.r) + ")");
      }
      StationRT* m = a.station(M2);
      check(m && m->loaded == 110 && m->reserve == 110, "M2HB emplacement starts 110 + 110 = 220");
      check(S(a, "mp40").containers.size() == 5 && S(a, "m1_thompson").containers.size() == 4 && S(a, "mg42").containers.size() == 4, "reserve held as real containers (MP 40 5 mags, Thompson 4 mags, MG 42 4 belts)");
      return true; });
    // ---- iron sights line up with the aim; shot lands under the sight line at the declared zero
    push("garand ads", [this](App& a, float t) {
      if (t == 0) a.selectWeapon(a.weaponIndex("m1_garand"));
      Instance* f = a.doc.find(F);
      face(a, vec3(f->pos.x, f->pos.y + 1.6f, f->pos.z + 4.5f));
      a.scriptedInput.aim = true;
      if (t < 0.6f) return false;
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
    // ---- resupply before reloading: total never above 80, loaded stays 0
    push("crate garand", [this](App& a, float t) {
      std::string m = a.crateTransaction(K);
      auto& s = S(a, "m1_garand");
      check(s.loaded == 0 && total(a, "m1_garand") == 80 && s.reserveRounds() == 80, "crate refills reserve only: Garand 0 + 80, still empty until R (" + m + ")");
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
      check(s.loaded == 8 && total(a, "m1_garand") == 80, "R loads a fresh en-bloc clip: 8 + 72");
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
      bool kept = std::find(s.containers.begin(), s.containers.end(), 20 - (int)summary["thompson_rounds_per_s"]) != s.containers.end();
      check(s.loaded == 20 && kept && total(a, "m1_thompson") == 100 - (int)summary["thompson_rounds_per_s"], "partial Thompson magazine goes back to the pouch with its rounds");
      a.selectWeapon(a.weaponIndex("mg42"));
      return true; });
    // ---- MG 42 bipod: supported bursts are easier to control
    push("mg42 unsupported", [this](App& a, float t) {
      a.scriptedInput.fire = true;
      if (t < 0.5f) return false;
      summary["mg42_climb_unsupported_deg"] = S(a, "mg42").recoilP;
      a.scriptedInput.fire = false;
      return true; });
    push("mg42 settle", wait(1.5f));
    push("mg42 supported", [this](App& a, float t) {
      a.scriptedInput.crouch = true;
      if (t < 0.6f) return false;
      a.scriptedInput.fire = true;
      if (t < 1.1f) return false;
      float sup = S(a, "mg42").recoilP, uns = summary["mg42_climb_unsupported_deg"];
      summary["mg42_climb_supported_deg"] = sup;
      a.scriptedInput.fire = false;
      check(a.supported && sup < uns * 0.6f, "bipod-supported burst climbs far less (" + std::to_string(sup).substr(0, 4) + " vs " + std::to_string(uns).substr(0, 4) + " deg)");
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
    // ---- M2HB at the emplacement, .50 belts only there
    push("m2hb", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); check(a.occupyStation(M2, ST_GUNNER), "crew the M2HB from its spade grips"); return false; }
      StationRT* m = a.station(M2);
      if (m->loaded > 0) { a.scriptedInput.fire = true; return t > 30; }
      a.scriptedInput.fire = false;
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
      int rifle = total(a, "m1_garand");
      std::string m = a.crateTransaction(K);
      StationRT* s = a.station(M2);
      check(s->reserve == 110 && a.crateStock[K]["50bmg_belt"] == 5 && total(a, "m1_garand") == rifle, ".50 belt from the crate goes to the M2HB reserve only (" + m + ")");
      return true; });
    push("reset", [this](App& a, float) {
      a.exitExplore();
      check(S(a, "m1_garand").loaded == 8 && total(a, "m1_garand") == 80 && a.station(M2)->loaded == 110 && a.crateStock[K]["30-06_clip"] == 30, "return to Creative resets weapons, the M2HB and crate stock (test consumption not persisted)");
      return true; });
  }
  stage = 701;
  if (wcur >= wseq.size()) { finish(app); return; }
  if (wt == 0) logf("[selftest] step: %s", wseq[wcur].name.c_str());
  bool d2 = wseq[wcur].fn(app, wt);
  wt += dt;
  if (d2) { ++wcur; wt = 0; }
}
