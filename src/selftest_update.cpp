// --selftest-update: gameplay changes of the complete update (docs/update_checklist.md):
//   recoil (no climb ceiling, posture x1 / x0.7 / x0.2, bipod only when deployed, no automatic recovery: the aim
//   stays where the pattern ended, vertically and horizontally, and the player pulls down to compensate),
//   hold-Space vault (validated, swept, no chaining; normal jump untouched), interaction reach + full-object
//   selection, enlarged firing slits that real shots pass through; every bunker slit seen and shot through from the
//   standing eye (docs/bunkers.md).
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> useq; size_t ucur = 0; float ut = 0;
uint64_t U_WALL = 0, U_HOUSE = 0, U_RACK = 0, U_OP = 0, U_MG = 0, U_CMD = 0, U_LG = 0, U_PAK = 0;
constexpr float SLITS_SILL = 1.47f, SLITS_TOP = 1.77f;   // tools/catalog_bunkers.mjs SLIT
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
    U_MG = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(-45, 0, 45), 0.0f);
    U_CMD = app.placeInstance(app.lib.find("bunker_command_shelter"), vec3(45, 0, 50), 25.0f);
    U_LG = app.placeInstance(app.lib.find("bunker_large_casemate"), vec3(0, 0, 95), -30.0f);
    U_PAK = app.placeInstance(app.lib.find("bunker_pak_casemate"), vec3(-50, 0, 95), 40.0f);
    int comp = app.placeCompanions(U_MG) + app.placeCompanions(U_PAK);
    app.syncRuntime();
    check(comp == 3, "companions placed with their hosts: MG 42 mount in the MG casemate, PaK 40 and its HE crate in the R612 casemate (" + std::to_string(comp) + ")");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(U_WALL && U_HOUSE && U_RACK && U_OP && U_MG && U_CMD && U_LG && U_PAK && app.enterExplore(), "fixtures: low stone wall, farmhouse, Allied rack, observation post, MG casemate, command shelter (25 deg), large casemate (-30 deg), R612 PaK casemate (40 deg); F5 into Explore");
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
        float want = pose == 0 ? 1.0f : pose == 1 ? 0.85f : 0.6f;   // handling.json recoil: standing 1.0, crouched 0.85, prone 0.6
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
        float want = 0.6f * 0.35f;   // handling.json recoil.prone x the MG 42's recoil.supported
        char b[200]; std::snprintf(b, sizeof b, "MG 42 prone and still: bipod deployed, recoil x%.3f = prone 0.6 x bipod 0.35 (composed once per shot)", a.lastRecoilScale);
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
    // ---------------------------------------------------------------- bunker slits (docs/bunkers.md#slit-height)
    // Standing inside at every viewing slit: a fan of eye rays reaches open ground (no wall, roof, reveal or hood
    // in the way), and an ADS round leaves the bunker. Floor-relative slit band 1.47-1.77 m, standing eye 1.62 m.
    struct SlitSpec { const char* name; uint64_t* inst; float gf; vec2 at; vec3 out; float yawFan, outer; const char* flap; const char* weapon; };
    static const SlitSpec specs[] = {
      {"MG casemate front", &U_MG, 0.3f, vec2(1.9f, 1.15f), vec3(0, 0, 1), 20, 2.9f, nullptr, "mg42"},
      {"MG casemate east side", &U_MG, 0.3f, vec2(2.15f, 0.85f), vec3(1, 0, 0), 20, 4.0f, nullptr, nullptr},
      {"MG casemate west side", &U_MG, 0.3f, vec2(-2.15f, 0.85f), vec3(-1, 0, 0), 20, 4.0f, nullptr, nullptr},
      {"command shelter front slit", &U_CMD, 0.3f, vec2(2.2f, 1.85f), vec3(0, 0, 1), 6, 3.7f, nullptr, "kar98k"},
      {"command shelter loophole", &U_CMD, 0.3f, vec2(1.5f, -1.85f), vec3(0, 0, -1), 6, 3.7f, "loophole_flap", nullptr},
      {"large casemate embrasure", &U_LG, 0.5f, vec2(-3.2f, 2.35f), vec3(0, 0, 1), 20, 4.6f, nullptr, "kar98k"},
      {"PaK casemate commander's slit", &U_PAK, 0.02f, vec2(2.3f, 1.4f), vec3(0, 0, 1), 6, 3.5f, nullptr, nullptr},
    };
    for (const SlitSpec& sp : specs) {
      push(std::string("view out: ") + sp.name, [this, &sp](App& a, float t) {
        uint64_t id = *sp.inst;
        if (t == 0) {
          a.scriptedInput = ExploreInput{};
          if (sp.flap) { int k = a.rt[id].asset->findInteract(sp.flap); auto& r = a.rt[id]; if (k >= 0 && r.progress[k] < 0.5f && r.dir[k] == 0) a.toggleInteract(id, k); }
          Physics::setCharacterPos(Wp(a, id, vec3(sp.at.x, sp.gf + 0.05f, sp.at.y)));
          vec3 d = yawQuat(a.doc.find(id)->yaw) * sp.out;
          a.eyeYaw = degrees(std::atan2(-d.x, -d.z)); a.eyePitch = 0;
          return false;
        }
        if (t < 1.6f) return false;
        vec3 eye = a.eyePos();
        float floorY = Wp(a, id, vec3(0, sp.gf, 0)).y;
        int rays = 0, blocked = 0; float worst = 99;
        for (float yaw : {-sp.yawFan, -sp.yawFan / 2, 0.0f, sp.yawFan / 2, sp.yawFan})
          for (float pitch : {-2.0f, 0.0f, 2.0f}) {
            vec3 l = glm::angleAxis(radians(yaw), vec3(0, 1, 0)) * sp.out;
            l = glm::normalize(l + vec3(0, std::tan(radians(pitch)), 0));
            vec3 d = yawQuat(a.doc.find(id)->yaw) * l;
            Physics::Hit h; ++rays;
            if (Physics::castRayEx(eye, d, 10.0f, h)) { ++blocked; worst = std::min(worst, h.t); }
          }
        char b[260]; std::snprintf(b, sizeof b, "%s: standing eye %.2f m above the floor sees out, %d of %d rays (yaw +-%.0f, pitch +-2 deg) clear 10 m (nearest block %.2f m)",
                                   sp.name, eye.y - floorY, rays - blocked, rays, sp.yawFan, worst);
        check(blocked == 0 && std::fabs(eye.y - floorY - 1.62f) < 0.05f, b);
        return true; });
      if (!sp.weapon) continue;
      push(std::string("shoot out: ") + sp.name, [this, &sp](App& a, float t) {
        static int s0; static float imp0;
        uint64_t id = *sp.inst;
        std::string w = sp.weapon;
        if (t == 0) {
          a.selectWeapon(a.weaponIndex(w)); a.wstate[w].loaded = std::max(a.wstate[w].loaded, 3); a.wstate[w].mode = 0;
          a.scriptedInput = ExploreInput{}; s0 = a.shotsFired; imp0 = a.lastImpact.time; return false;
        }
        a.scriptedInput.aim = true;
        if (t < 2.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 6; }
        a.scriptedInput.fire = false;
        if (!a.projectiles.empty() && t < 4) return false;
        vec3 o = yawQuat(a.doc.find(id)->yaw) * sp.out;
        vec3 front = Wp(a, id, sp.out * sp.outer);
        float beyond = glm::dot(a.lastImpact.pos - front, o);
        bool newImpact = a.lastImpact.time > imp0;
        char b[260]; std::snprintf(b, sizeof b, "%s: %s fired at ADS from the standing eye leaves the bunker (%s %.1f m beyond the outer face, on %s)",
                                   sp.name, w.c_str(), newImpact ? "impact" : "no impact,", beyond, newImpact ? a.lastImpact.surface.c_str() : "-");
        check(a.shotsFired > s0 && (!newImpact || beyond > 1.0f), b);
        return true; });
    }
    // ---------------------------------------------------------------- crew stations placed with their casemates
    // The MG 42 embrasure mount (MG casemate) and the PaK 40 (R612 casemate) arrive as companions. Over the whole
    // traverse / elevation the bore from the trunnion clears the slit / embrasure, the gunner stands on the bunker
    // floor with the sight in the slit band, and a real round leaves the bunker.
    struct GunSpec { const char* name; uint64_t* host; const char* gun; float yaws[3]; float elevs[3]; float outer; float clear; };
    static const GunSpec guns[] = {
      {"MG 42 embrasure mount", &U_MG, "gun_mg42_embrasure", {-55, 0, 55}, {-5, 0, 7}, 2.9f, 8.0f},
      {"PaK 40 in the R612 casemate", &U_PAK, "gun_pak40", {-32.5f, 0, 32.5f}, {-5, 0, 22}, 3.5f, 6.0f},
    };
    for (const GunSpec& g : guns) {
      push(std::string("crew: ") + g.name, [this, &g](App& a, float t) {
        static uint64_t gid; static int s0;
        if (t == 0) {
          gid = 0;
          for (auto& i : a.doc.instances) if (i.assetId == g.gun && glm::length(vec2(i.pos.x - a.doc.find(*g.host)->pos.x, i.pos.z - a.doc.find(*g.host)->pos.z)) < 6) gid = i.id;
          check(gid != 0, std::string(g.name) + ": placed with its casemate as a companion");
          if (!gid) return true;
          StationRT* s = a.station(gid);
          Instance* gi = a.doc.find(gid);
          int clear = 0, total = 0; float worst = 99;
          for (float y : g.yaws) for (float e : g.elevs) {
            s->yaw = s->yawT = y; s->elev = s->elevT = e;
            vec3 dir; a.stationMuzzle(*gi, a.rt[gid], dir);
            mat4 b, ym, em, rc; a.stationParts(*gi, a.rt[gid], b, ym, em, rc);
            vec3 o = vec3(em * vec4(0, 0, 0, 1));
            Physics::Hit h; ++total;
            if (!Physics::castRayEx(o, dir, g.clear, h, gid << 16)) ++clear; else worst = std::min(worst, h.t);
          }
          s->yaw = s->yawT = 0; s->elev = s->elevT = 0;
          char b[220]; std::snprintf(b, sizeof b, "%s: the bore from the trunnion clears the opening at %d of %d traverse/elevation limits (%.1f m; nearest block %.2f m)", g.name, clear, total, g.clear, worst);
          check(clear == total, b);
          a.scriptedInput = ExploreInput{};
          check(a.occupyStation(gid, ST_GUNNER), std::string(g.name) + ": the gunner takes position");
          s0 = a.shotsFired;
          return false;
        }
        StationRT* s = a.station(gid);
        if (t < 0.6f) return false;
        if (t < 0.65f) {
          vec3 e, f; float fov; bool cam = a.stationCamera(e, f, fov);
          float floorY = Wp(a, *g.host, vec3(0)).y + (*g.host == U_MG ? 0.3f : 0.02f);
          vec3 feet = Physics::character().pos;
          char b[220]; std::snprintf(b, sizeof b, "%s: gunner stands on the bunker floor (feet %.2f m above it), sight %.2f m above the floor", g.name, feet.y - floorY, e.y - floorY);
          check(cam && std::fabs(feet.y - floorY) < 0.08f && (*g.host != U_MG || (e.y - floorY > SLITS_SILL && e.y - floorY < SLITS_TOP)), b);
          return false;
        }
        a.scriptedInput.fire = t < 1.2f;
        if (t < 3.0f) return false;
        a.scriptedInput = ExploreInput{};
        Instance* hi = a.doc.find(*g.host);
        vec3 out = yawQuat(hi->yaw) * vec3(0, 0, 1), front = Wp(a, *g.host, vec3(0, 0, g.outer));
        float beyond = glm::dot(a.lastImpact.pos - front, out);
        bool fresh = a.shotsFired > s0;
        char b[240]; std::snprintf(b, sizeof b, "%s: %d round(s) fired through the opening, last impact %.1f m beyond the outer face (%s)", g.name, a.shotsFired - s0, beyond, a.lastImpact.surface.c_str());
        check(fresh && s && beyond > 2.0f, b);
        a.leaveStation();
        return true; });
    }
    // ---------------------------------------------------------------- MG 42 mount = the handheld MG 42, fixed, 200 rounds
    // The mount's sight picture is the handheld's at full ADS: the eye on the GLB's own sight line (as drawn in the
    // cradle) at the notch-to-eye distance that draws the notch at the handheld's size, the same world FOV, aimed fire
    // from the front post along the sight line with the handheld's zero; reload and tail foley are the handheld's.
    push("MG 42 mount: handheld sight picture, eye, zero and a 200-round belt", [this](App& a, float t) {
      static uint64_t gid; static int ph, b0, tails0; static float sScale, sFov; static vec3 sEye, sEyeW, sFwd;
      int wi = a.weaponIndex("mg42");
      if (t == 0) {
        gid = 0; ph = 0;
        for (auto& i : a.doc.instances) if (i.assetId == "gun_mg42_embrasure") gid = i.id;
        if (!gid || wi < 0) { check(false, "MG 42 mount and the MG 42 are present"); return true; }
        a.resetStation(*a.doc.find(gid));
        StationRT* s = a.station(gid);
        check(a.rt[gid].asset->station.loaded == 200 && s->loaded == 200 && s->reserve == 250,
              "MG 42 mount holds a 200-round belt (handheld 50): " + std::to_string(s->loaded) + " on the gun, " + std::to_string(s->reserve) + " in reserve");
        a.scriptedInput = ExploreInput{};
        check(!a.occupyStation(gid, ST_HANDLER) && a.occupied == 0, "MG 42 mount: Move is refused - it is not moved");
        check(a.occupyStation(gid, ST_GUNNER), "MG 42 mount: the gunner takes the grips");
        return false;
      }
      const WeaponDef& w = *a.weaponDefs[wi];
      StationRT* s = a.station(gid);
      if (!w.useModel()) { check(true, "MG 42 GLB not loaded: sight-picture checks skipped"); a.leaveStation(); return true; }
      if (t < 0.6f) return false;
      if (ph == 0) {   // the mount's view against the GLB as it is drawn
        vec3 e, f; float fov; bool cam = a.stationCamera(e, f, fov);
        std::vector<DrawItem> op, gl; a.buildDrawLists(op, gl);
        mat4 G(0); for (auto& m : a.R.worldModels) if (m.model == w.model.get()) { G = m.m; break; }
        vec3 ge = vec3(glm::inverse(G) * vec4(e, 1));   // the eye in the gun's own space
        vec3 Rw = vec3(G * vec4(w.rear, 1)), Fw = vec3(G * vec4(w.front, 1));
        int W = std::max(1, a.R.width), H = std::max(1, a.R.height);
        mat4 vp = glm::perspective(radians(fov), (float)W / H, 0.03f, 4000.0f) * glm::lookAt(e, e + f, vec3(0, 1, 0));
        auto off = [&](vec3 p) { vec4 c = vp * vec4(p, 1); return glm::length(vec2(c.x / c.w * 0.5f * W, c.y / c.w * 0.5f * H)); };
        float dR = off(Rw), dF = off(Fw);
        sEye = ge; sFov = fov; sEyeW = e; sFwd = f; sScale = H * 0.5f / std::tan(radians(fov) * 0.5f) / glm::length(Rw - e);
        char b[300]; std::snprintf(b, sizeof b, "MG 42 mount view: eye on the GLB sight line (%.1f / %.1f mm off the notch line), %.1f mm above the bore; notch %.2f px and front post %.2f px off the centre; world FOV %.0f (handheld ADS %.0f)",
                                   (ge.x - w.rear.x) * 1000, (ge.y - w.rear.y) * 1000, (ge.y - w.muzzle.y) * 1000, dR, dF, fov, w.adsWorldFov);
        check(cam && G != mat4(0) && std::fabs(ge.x - w.rear.x) < 1e-3f && std::fabs(ge.y - w.rear.y) < 1e-3f && dR < 0.5f && dF < 0.5f && fov == w.adsWorldFov, b);
        s->semi = false; b0 = a.shotsFired; tails0 = a.audio.eventPlays["tail_mg42"];
        shotQueue.push_back("update_mg42_mount_sight");
        ph = 1; return false;
      }
      if (ph == 1) {   // a short burst: aimed fire leaves the front post along the sight line, tilted by the handheld zero
        a.scriptedInput.fire = t > 0.75f && t < 1.05f;   // after the screenshot frame
        if (t >= 1.05f) {   // trigger released: the burst is over
          if (b0 >= 0) { check(false, "MG 42 mount: no round fired"); a.scriptedInput = ExploreInput{}; a.leaveStation(); return true; }
          ph = 2; return false;
        }
        if (b0 >= 0 && a.shotsFired > b0 && !a.projectiles.empty()) {   // the newest round, at most one step after launch
          vec3 e, f; float fov; a.stationCamera(e, f, fov);
          const Projectile& p = a.projectiles.back();
          vec3 v = glm::normalize(p.v), rel = p.p - e;
          float tz = w.zero / w.velocity, want = std::atan(0.5f * 9.81f * tz * tz / w.zero);
          float ang = std::acos(std::clamp(glm::dot(v, f), -1.0f, 1.0f)), up = v.y - f.y;
          float lineOff = glm::length(rel - v * glm::dot(rel, v));   // launched on the eye's line (from the front post)
          char b[260]; std::snprintf(b, sizeof b, "MG 42 mount round: launched %.1f mm off the sight line, %.3f deg above it (handheld zero at %.0f m: %.3f deg)", lineOff * 1000, degrees(ang), w.zero, degrees(want));
          check(lineOff < 0.01f && glm::length(rel) < 30 && std::fabs(ang - want) < radians(0.03f) && up > 0, b);
          b0 = -1;   // measured
        }
        return false;
      }
      if (ph == 2) {
        a.scriptedInput.fire = false;
        if (t < 1.4f) return false;
        int tails = a.audio.eventPlays["tail_mg42"] - tails0;
        check(a.audio.eventsLoaded == 0 || tails == 1, "MG 42 mount: the handheld's recorded tail once on release (" + std::to_string(tails) + ")");
        b0 = s->loaded + s->reserve;
        a.scriptedInput.reload = true; ph = 3; return false;
      }
      if (ph == 3) {
        a.scriptedInput.reload = false;
        check(a.audio.eventsLoaded == 0 || a.audio.lastEvent == "reload_mg42", "MG 42 mount reload plays the handheld's belt foley (" + a.audio.lastEvent + ")");
        ph = 4; return false;
      }
      if (ph == 4) {
        if (s->reload > 0) return false;
        check(s->loaded == 200 && s->loaded + s->reserve == b0, "MG 42 mount reload: a full 200-round belt (" + std::to_string(s->loaded) + " on the gun, " + std::to_string(s->reserve) + " in reserve, no rounds created)");
        a.leaveStation(); a.scriptedInput = ExploreInput{};
        a.selectWeapon(wi); a.wstate["mg42"].loaded = 50; a.wstate["mg42"].kick = 0;
        a.vm.procedural = false; a.vm.snap = true; a.viewKickPitch = a.viewKickYaw = 0;
        {   // outside the casemate, looking the same way as the mount (for the comparison screenshots)
          vec3 h = glm::normalize(vec3(sFwd.x, 0, sFwd.z)); putFeet(a, sEyeW + h * 4.5f);
          float best = -2; for (int y = 0; y < 360; ++y) { float q = glm::dot(App::forward((float)y, 0), h); if (q > best) { best = q; a.eyeYaw = (float)y; } }
          a.eyePitch = degrees(std::asin(std::clamp(sFwd.y, -1.0f, 1.0f)));
        }
        ph = 5; return false;
      }
      a.scriptedInput.aim = true; a.adsT = 1;   // the handheld at full ADS, procedural motion off
      if (ph == 5) { ph = 6; b0 = 0; return false; }
      if (++b0 < 20) return false;
      if (b0 == 20) { shotQueue.push_back("update_mg42_handheld_sight"); return false; }
      if (b0 < 24) return false;
      const WeaponState& ws = a.wstate["mg42"];
      vec3 e = a.eyePos();
      a.vm.snap = true;   // the exact ADS pose (no spring lag)
      mat4 M = a.viewModelMatrix(w, ws);
      vec3 he = vec3(glm::inverse(M) * vec4(e, 1));
      float hfov = 72; a.weaponCamera(hfov);
      float hScale = a.R.height * 0.5f / std::tan(radians(a.vmFovNow()) * 0.5f) / glm::length(vec3(M * vec4(w.rear, 1)) - e);
      char b[320]; std::snprintf(b, sizeof b, "MG 42 mount vs handheld ADS: eye %.1f mm higher / %.1f mm sideways relative to the gun, %.1f cm further back (FOV compensation); notch drawn at %.3f x the handheld size; world FOV %.0f vs %.0f",
                                 (sEye.y - he.y) * 1000, (sEye.x - he.x) * 1000, (he.z - sEye.z) * 100, sScale / hScale, sFov, hfov);
      check(std::fabs(sEye.y - he.y) < 2e-3f && std::fabs(sEye.x - he.x) < 2e-3f && std::fabs(he.z - sEye.z) < 0.02f && std::fabs(sScale / hScale - 1) < 0.01f && sFov == hfov, b);
      summary["mg42_mount_eye_above_bore_mm"] = (sEye.y - w.muzzle.y) * 1000;
      summary["mg42_mount_notch_scale_vs_handheld"] = sScale / hScale;
      a.scriptedInput = ExploreInput{}; a.adsT = 0; a.vm.procedural = true; a.vm.snap = true;
      return true; });
    push("camouflage nets are visual only", [this](App& a, float) {
      int nets = 0, solid = 0;
      for (uint64_t id : {U_MG, U_CMD, U_LG, U_PAK})
        for (auto& p : a.rt[id].asset->parts) if (a.rt[id].asset->materials[p.mat].name == "camo_net") { ++nets; solid += p.collide; }
      check(nets > 20 && solid == 0, "camouflage netting on the MG casemate, command shelter, large and PaK casemates: " + std::to_string(nets) + " sheets, " + std::to_string(solid) + " with collision");
      return true; });
    push("bunkers stand free", [this](App& a, float) {
      // the reworked MG casemate and command shelter carry no earth wedges any more; every bunker's slit throat
      // sits at the shared floor-relative height (walkthrough look targets are generated from the same constant)
      int turf = 0;
      for (uint64_t id : {U_MG, U_CMD, U_LG, U_PAK}) {
        const AssetDef* d = a.rt[id].asset;
        for (auto& p : d->parts) if (d->materials[p.mat].name == "turf") ++turf;
      }
      check(turf == 0, "MG casemate, command shelter and large casemate have no turf berms (" + std::to_string(turf) + " parts)");
      return true; });
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
