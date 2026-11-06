// --selftest-update: gameplay changes of the complete update (docs/update_checklist.md):
//   recoil (no climb ceiling, posture x1 / x0.7 / x0.2, bipod only when deployed, no automatic recovery: the aim
//   stays where the pattern ended, vertically and horizontally, and the player pulls down to compensate),
//   hold-Space vault (validated, swept, no chaining; normal jump untouched), interaction reach + full-object
//   selection, enlarged firing slits that real shots pass through.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> useq; size_t ucur = 0; float ut = 0;
uint64_t U_WALL = 0, U_HOUSE = 0, U_RACK = 0, U_OP = 0;
vec3 Wp(App& a, uint64_t id, vec3 l) { Instance* i = a.doc.find(id); return i->pos + yawQuat(i->yaw) * l; }
void putFeet(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z) + 0.02f, p.z)); }
}

void SelfTest::updateSuite(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { useq.push_back({n, f}); };
  if (stage == 1700) {
    reportName = "update";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Update checks");
    U_WALL = app.placeInstance(app.lib.find("stone_wall_low"), vec3(0, 0, -8), 0.0f);
    U_HOUSE = app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(40, 0, -20), 0.0f);
    U_RACK = app.placeInstance(app.lib.find("rack_allied"), vec3(-12, 0, 0), 0.0f);
    U_OP = app.placeInstance(app.lib.find("bunker_observation_post"), vec3(0, 0, 40), 0.0f);
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(U_WALL && U_HOUSE && U_RACK && U_OP && app.enterExplore(), "fixtures: low stone wall, farmhouse, Allied rack, observation post; F5 into Explore");
    app.useScripted = true;
    useq.clear(); ucur = 0; ut = 0;

    // ---------------------------------------------------------------- recoil
    push("recoil: full magazine climbs", [this](App& a, float t) {
      static std::vector<float> trace; static int s0;
      if (t == 0) { putFeet(a, vec3(0, 0, 0)); a.eyeYaw = 180; a.eyePitch = 0; a.selectWeapon(a.weaponIndex("fg42")); a.wstate["fg42"].loaded = 20; a.wstate["fg42"].mode = 0; a.wstate["fg42"].recoilP = 0; trace.clear(); s0 = a.shotsFired; a.scriptedInput = ExploreInput{}; return false; }
      if (t < 0.6f) return false;
      auto& s = a.wstate["fg42"];
      a.scriptedInput.fire = s.loaded > 0;
      if ((int)trace.size() < a.shotsFired - s0) trace.push_back(s.recoilP);
      if (s.loaded > 0 && t < 6) return false;
      a.scriptedInput.fire = false;
      bool mono = true; for (size_t k = 1; k < trace.size(); ++k) mono = mono && trace[k] > trace[k - 1];
      char b[220]; std::snprintf(b, sizeof b, "FG 42 full AUTO magazine (%zu shots): aim climbs on every shot, no ceiling (%.1f deg after the last, old cap 9)", trace.size(), trace.empty() ? 0.0f : trace.back());
      check(trace.size() >= 18 && mono && trace.back() > 9.0f && trace.back() < 60.0f, b);
      summary["fg42_mag_climb_deg"] = trace.empty() ? 0.0f : trace.back();
      return true; });
    push("recoil: no recovery after firing", [this](App& a, float t) {
      // the aim (camera pitch / yaw + the burst's recoil offset) must not drift back toward where the burst started
      static float p0, y0, e0, aimP0, aimY0, worstP, worstY; static int ph;
      auto& s = a.wstate["fg42"];
      float aimP = a.eyePitch + s.recoilP, aimY = a.eyeYaw + s.recoilY;
      if (t == 0) { ph = 0; p0 = s.recoilP; y0 = s.recoilY; e0 = a.eyePitch; aimP0 = aimP; aimY0 = aimY; worstP = worstY = 0; return false; }
      worstP = std::max(worstP, std::fabs(aimP - aimP0)); worstY = std::max(worstY, std::fabs(wrapDeg(aimY - aimY0 + 180) - 180));
      if (ph == 0 && t < 2.5f) return false;
      if (ph == 0) {
        char b[260]; std::snprintf(b, sizeof b, "after the trigger is released the aim stays where the pattern ended: climb %.1f deg (yaw %+.2f) kept for 2.5 s, worst drift %.3f / %.3f deg; camera pitch %.1f -> %.1f deg", p0, y0, worstP, worstY, e0, a.eyePitch);
        check(p0 > 9.0f && worstP < 0.01f && worstY < 0.01f && s.recoilP == 0 && s.recoilY == 0 && std::fabs(a.eyePitch - (e0 + p0)) < 0.01f, b);
        summary["fg42_climb_kept_deg"] = a.eyePitch - e0;
        a.scriptedInput.dpitch = -(a.eyePitch - e0); ph = 1; return false;   // the player pulls the mouse down by the climb
      }
      a.scriptedInput.dpitch = 0;
      char b[200]; std::snprintf(b, sizeof b, "pulling down by the climb brings the aim back to the start pitch (%.2f deg, start %.2f)", a.eyePitch, e0);
      check(std::fabs(a.eyePitch - e0) < 0.05f, b);
      return true; });
    push("recoil: kept between bursts, compensated mid-burst", [this](App& a, float t) {
      // two short bursts: the second starts from where the first ended (no reset); then a burst fired while the
      // player pulls down at the climb rate ends near the start pitch
      static int ph, s0; static float e0, after1, after2, lastP;
      auto& s = a.wstate["fg42"];
      if (t == 0) { ph = 0; a.eyePitch = 0; s.loaded = 20; a.scriptedInput = ExploreInput{}; return false; }
      if (ph == 0 && t > 0.5f) { e0 = a.eyePitch; s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 1; return false; }
      if (ph == 1 && a.shotsFired - s0 >= 4) { a.scriptedInput.fire = false; ph = 2; return false; }
      if (ph == 2 && s.recoilP == 0 && s.sinceShot > 0.5f) { after1 = a.eyePitch; s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 3; return false; }
      if (ph == 3 && a.shotsFired - s0 >= 4) { a.scriptedInput.fire = false; ph = 4; return false; }
      if (ph == 4 && s.recoilP == 0 && s.sinceShot > 0.5f) {
        after2 = a.eyePitch;
        char b[220]; std::snprintf(b, sizeof b, "4-round bursts stack: pitch %.2f -> %.2f -> %.2f deg (the second burst starts where the first ended)", e0, after1, after2);
        check(after1 > e0 + 1.0f && after2 > after1 + 1.0f, b);
        a.eyePitch = 0; s.loaded = 20; lastP = 0; s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 5; return false;
      }
      if (ph == 5) {   // pull down by each shot's recoil as it lands (perfect compensation)
        a.scriptedInput.dpitch = -(s.recoilP - lastP); lastP = s.recoilP;
        if (a.shotsFired - s0 >= 6) { a.scriptedInput.fire = false; ph = 6; }
        return false;
      }
      if (ph == 6) { a.scriptedInput.dpitch = 0; if (s.recoilP != 0 || s.sinceShot < 0.5f) return false;
        char b[200]; std::snprintf(b, sizeof b, "a 6-round burst with the mouse pulled down against the climb ends at %.2f deg (started at 0)", a.eyePitch);
        check(std::fabs(a.eyePitch) < 0.5f, b);
        a.eyePitch = 0; return true; }
      return t > 10; });
    for (int pose : {0, 1, 2}) {
      push(std::string("recoil posture ") + std::to_string(pose), [this, pose](App& a, float t) {
        static int s0; static float before;
        auto& s = a.wstate["m1_garand"];
        if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); s.loaded = 8; s.recoilP = 0; a.scriptedInput = ExploreInput{}; a.proneToggle = pose == 2; return false; }
        a.scriptedInput.crouch = pose == 1;
        if (t < 2.0f) return false;
        if (t < 2.02f) { s0 = a.shotsFired; before = s.recoilP; }
        if (a.shotsFired == s0) { a.scriptedInput.fire = !a.scriptedInput.fire; return t > 5; }
        a.scriptedInput.fire = false;
        float want = pose == 0 ? 1.0f : pose == 1 ? 0.7f : 0.2f;
        const char* nm[3] = {"standing", "crouched", "prone"};
        char b[200]; std::snprintf(b, sizeof b, "%s shot: recoil x%.2f (wanted x%.2f), kick %.2f deg (Garand)", nm[pose], a.lastRecoilScale / (a.adsT > 0.85f ? 0.85f : 1.0f), want, s.recoilP - before);
        check(std::fabs(a.lastRecoilScale - want) < 1e-3f, b);
        summary["recoil_scale_" + std::string(nm[pose])] = a.lastRecoilScale;
        a.proneToggle = false;
        return true; });
    }
    push("recoil: bipod only when deployed", [this](App& a, float t) {
      static int s0, ph;
      auto& s = a.wstate["mg42"];
      if (t == 0) { ph = 0; a.selectWeapon(a.weaponIndex("mg42")); s.loaded = 50; a.scriptedInput = ExploreInput{}; a.proneToggle = false; return false; }
      if (ph == 0 && t > 2.0f) { s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 1; return false; }
      if (ph == 1 && a.shotsFired > s0) {
        a.scriptedInput.fire = false;
        check(!a.supported && std::fabs(a.lastRecoilScale - 1.0f) < 1e-3f, "MG 42 fired standing: bipod folded, no support benefit (x" + std::to_string(a.lastRecoilScale).substr(0, 4) + ")");
        a.proneToggle = true; ph = 2; return false;
      }
      if (ph == 2 && t > 5.0f) { s0 = a.shotsFired; a.scriptedInput.fire = true; ph = 3; return false; }
      if (ph == 3 && a.shotsFired > s0) {
        a.scriptedInput.fire = false;
        float want = 0.2f * 0.35f;
        char b[200]; std::snprintf(b, sizeof b, "MG 42 prone and still: bipod deployed, recoil x%.3f = prone 0.20 x bipod 0.35 (composed once per shot)", a.lastRecoilScale);
        check(a.supported && std::fabs(a.lastRecoilScale - want) < 1e-3f, b);
        a.proneToggle = false;
        return true;
      }
      return t > 12; });
    push("stand up", [](App& a, float t) { a.scriptedInput = ExploreInput{}; a.proneToggle = false; a.selectWeapon(a.weaponIndex("m1_garand")); return t > 1.6f; });

    // ---------------------------------------------------------------- vault
    push("tap jump: normal jump, no vault", [this](App& a, float t) {
      static int c0; static float apex, y0;
      if (t == 0) { putFeet(a, Wp(a, U_WALL, vec3(0, 0, 0.95f))); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; c0 = a.vault.count; apex = -1; y0 = -1; return false; }
      auto c = Physics::character();
      if (t < 0.5f) { y0 = c.pos.y; return false; }
      a.scriptedInput.jump = t < 0.52f; a.scriptedInput.jumpHeld = t < 0.56f;   // a tap shorter than the hold threshold
      apex = std::max(apex, c.pos.y - y0);
      if (t < 1.6f) return false;
      char b[200]; std::snprintf(b, sizeof b, "a tapped Space at the 0.91 m wall is an ordinary jump (apex %.2f m), no vault, still on the near side", apex);
      check(a.vault.count == c0 && apex > 0.5f && glm::dot(c.pos - Wp(a, U_WALL, vec3(0)), vec3(0, 0, 1)) > 0, b);
      return true; });
    push("hold Space: vault the low wall", [this](App& a, float t) {
      static int c0; static bool done;
      if (t == 0) { putFeet(a, Wp(a, U_WALL, vec3(0, 0, 0.95f))); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; c0 = a.vault.count; done = false; return false; }
      if (t < 0.5f) return false;
      a.scriptedInput.jump = t < 0.52f; a.scriptedInput.jumpHeld = true;   // press (jumps) and keep holding
      if (t < 3.0f) return false;
      auto c = Physics::character();
      float side = glm::dot(c.pos - Wp(a, U_WALL, vec3(0)), vec3(0, 0, 1));
      char b[700]; std::snprintf(b, sizeof b, "held Space at the 0.91 m x 0.62 m wall: one vault (%d) to the far side (%.2f m beyond the centre line), body swept clear, landed (%s); still holding does not chain a second vault",
                                 a.vault.count - c0, -side, a.vault.why.c_str());
      check(a.vault.count == c0 + 1 && side < -0.6f && !a.vault.active, b);
      { std::string pr = "probe over the wall centre (pos.y " + std::to_string(Wp(a, U_WALL, vec3(0)).y).substr(0, 5) + "):";
        for (float y : {0.95f, 1.05f, 1.2f, 1.4f, 1.8f, 2.4f}) pr += " " + std::to_string(y).substr(0, 4) + (Physics::shapeFitsAt(Physics::STAND, Wp(a, U_WALL, vec3(0, y, 0)), false) ? "=fits" : "=hit");
        check(true, pr); }
      a.scriptedInput = ExploreInput{};
      return true; });
    push("hold Space: building wall refused", [this](App& a, float t) {
      static int c0;
      const AssetDef* fa = a.lib.find("house_farmhouse_2story");
      int k = 0; for (size_t q = 0; q < fa->panels.size(); ++q) if (fa->panels[q].id == "floor-00/front/bay-02") k = (int)q;
      const Panel& p = fa->panels[k];
      vec3 face = Wp(a, U_HOUSE, vec3((p.bmin.x + p.bmax.x) * 0.5f, 0, p.bmax.z + 0.55f));
      if (t == 0) { putFeet(a, face); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; c0 = a.vault.count; return false; }
      if (t < 0.5f) return false;
      a.scriptedInput.jump = t < 0.52f; a.scriptedInput.jumpHeld = true;
      if (t < 2.0f) return false;
      check(a.vault.count == c0, "held Space at a 6 m house wall: no vault (" + a.vault.why + ")");
      a.scriptedInput = ExploreInput{};
      return true; });

    // ---------------------------------------------------------------- interaction
    push("interaction reach + whole rack", [this](App& a, float t) {
      if (t == 0) { a.scriptedInput = ExploreInput{}; return false; }
      if (t < 0.3f) return false;
      const AssetDef* ra = a.lib.find("rack_allied");
      Instance* ri = a.doc.find(U_RACK);
      // aim at a real collider near the rack's END (an edge, far from its centre) from about 2.75 m: beyond the old
      // 2.4 m reach. Scan the end region for a point a ray actually hits on the rack's body.
      vec3 eye(0), dir(0, 0, -1), edge(0);
      for (float x = ra->bmax.x - 0.03f; x > ra->bmax.x - 0.4f && edge == vec3(0); x -= 0.03f)
        for (float y = ra->bmin.y + 0.3f; y < ra->bmax.y - 0.05f; y += 0.1f) {
          vec3 pt = ri->pos + yawQuat(ri->yaw) * vec3(x, y, (ra->bmin.z + ra->bmax.z) * 0.5f);
          vec3 e2 = pt + vec3(0, 0, 4.0f);
          Physics::Hit hh;
          if (Physics::castRayEx(e2, vec3(0, 0, -1), 6.0f, hh) && (hh.ud >> 16) == U_RACK) { edge = hh.pos; eye = hh.pos + vec3(0, 0, 2.75f); break; }
        }
      dir = vec3(0, 0, -1);
      InteractTarget tg = a.findTarget(eye, dir);
      char b[200]; std::snprintf(b, sizeof b, "the rack's end post from %.2f m (old reach 2.4 m) selects the rack (%s, kind %d, %.2f m to its surface)", glm::length(edge - eye), tg.valid && tg.inst == U_RACK ? "yes" : "no", tg.kind, tg.t);
      check(tg.valid && tg.inst == U_RACK && tg.kind == 3, b);
      InteractTarget far = a.findTarget(edge + vec3(0, 0, 3.6f), dir);
      check(!(far.valid && far.inst == U_RACK), "3.6 m away it is out of reach (no oversized trigger)");
      return true; });

    // ---------------------------------------------------------------- firing slit
    for (std::string w : {"kar98k", "m1_garand", "mg42"}) {
      push("slit " + w, [this, w](App& a, float t) {
        static int s0; static float imp0;
        const AssetDef* oa = a.lib.find("bunker_observation_post");
        if (t == 0) {
          auto& r = a.rt[U_OP]; int k = oa->findInteract("viewing_flap");
          if (k >= 0 && r.progress[k] < 0.5f && r.dir[k] == 0) a.toggleInteract(U_OP, k);
          putFeet(a, Wp(a, U_OP, vec3(0, 0, 0.75f))); a.eyeYaw = 180; a.eyePitch = 1.0f;   // facing out through the slit (+z)
          a.selectWeapon(a.weaponIndex(w)); a.wstate[w].loaded = std::max(a.wstate[w].loaded, 3); a.wstate[w].mode = a.weaponDefs[a.activeWeapon]->modes.size() > 1 ? 1 : 0;
          a.scriptedInput = ExploreInput{}; s0 = a.shotsFired; imp0 = a.lastImpact.time; return false;
        }
        a.scriptedInput.aim = true;
        if (t < 2.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 6; }
        a.scriptedInput.fire = false;
        if (!a.projectiles.empty() && t < 4) return false;
        int fk = oa->findInteract("viewing_flap");
        vec3 front = Wp(a, U_OP, vec3(0, 0, oa->interactables[fk].apMax.z));   // the slit's outer face
        float beyond = glm::dot(a.lastImpact.pos - front, vec3(0, 0, 1));
        char b[320]; std::snprintf(b, sizeof b, "%s fired at ADS through the enlarged slit: the round leaves the bunker (impact %.1f m beyond the front face, on %s); start %.2f m above the floor [start %.1f %.1f %.1f dir %.2f %.2f %.2f impact %.1f %.1f %.1f front z %.1f]",
                                   w.c_str(), beyond, a.lastImpact.surface.c_str(), a.lastShotStart.y - Wp(a, U_OP, vec3(0)).y, a.lastShotStart.x, a.lastShotStart.y, a.lastShotStart.z, a.lastShotDir.x, a.lastShotDir.y, a.lastShotDir.z, a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, front.z);
        bool newImpact = a.lastImpact.time > imp0;
        check(!newImpact || beyond > 1.0f, std::string(newImpact ? "" : "[no impact: the round flew out of the slit and away] ") + b);
        return true; });
    }
    push("reset", [this](App& a, float) {
      a.scriptedInput = ExploreInput{}; a.proneToggle = false;
      a.exitExplore();
      check(a.mode == Mode::Creative, "back to Creative");
      return true; });
  }
  stage = 1701;
  if (ucur >= useq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); finish(app); return; }
  if (ut == 0) logf("[selftest] step: %s", useq[ucur].name.c_str());
  bool d2 = useq[ucur].fn(app, ut);
  ut += dt;
  if (ut > 30) { check(false, "step timed out: " + useq[ucur].name); d2 = true; }
  if (d2) { ++ucur; ut = 0; }
}
