// --selftest-fps: imported GLB weapons, ADS alignment on the real sight line, sight-origin ballistics,
// sway / bob / breathing, IK arms and the depth-of-field focus (docs/fps-weapons.md).
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> fseq; size_t fcur = 0; float ft = 0;
uint64_t WALL = 0;
const WeaponDef& D(App& a) { return *a.weaponDefs[a.activeWeapon]; }
float pxFromCentre(const mat4& vp, vec3 w) { vec4 c = vp * vec4(w, 1); return glm::length(vec2(c.x / c.w, c.y / c.w)) * 540.0f; }
}

void SelfTest::fpsUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { fseq.push_back({n, f}); };
  if (stage == 1100) {
    reportName = "fps";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "FPS range");
    WALL = app.placeInstance(app.lib.find("small_stone_dairy"), vec3(0, 0, -30), 0.0f);
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(WALL != 0 && app.enterExplore(), "range: stone dairy 30 m downrange, F5 into Explore");
    app.useScripted = true;
    fseq.clear(); fcur = 0; ft = 0;
    if (std::getenv("NMK_ARMS_SHOTS")) {   // FA2: only the first-person arm shots (hip, ADS, mid-reload; selftest_out/fa2_*)
      for (const char* id : {"m1_garand", "kar98k", "m1_thompson", "mp40", "m1918a2_bar", "mg42", "bazooka_m9", "knife_bayonet", "m2_flamethrower"}) {
        std::string sid = id;
        push("arms " + sid, [this, sid](App& a, float t) {
          static bool shot = false;
          if (t == 0) { a.selectWeapon(a.weaponIndex(sid)); a.vm.procedural = false; a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = -2; shot = false; }
          bool side = sid == "m1_garand" || sid == "m2_flamethrower" || sid == "mp40";   // debug side view of the rig (weapon and arm turned together)
          a.vmDebugYaw = side && t > 0.25f && t < 0.5f ? 70.0f : 0.0f;
          if (side && t > 0.4f && t < 0.42f) shotQueue.push_back("fa2_" + sid + "_side");
          if (t > 0.6f && t < 0.62f) shotQueue.push_back("fa2_" + sid + "_hip");
          a.scriptedInput.aim = t > 0.7f && t < 2.0f;
          if (t > 1.8f && t < 1.82f) shotQueue.push_back("fa2_" + sid + "_ads");
          auto& s = a.wstate[sid];
          if (t > 2.0f && t < 2.05f) s.loaded = 0;
          a.scriptedInput.reload = t > 2.1f && t < 2.15f;
          if (!shot && s.reload > 0 && s.reload < s.reloadTotal * 0.55f) { shot = true; shotQueue.push_back("fa2_" + sid + "_reload"); }
          if (t < 2.2f || (s.reload > 0 && t < 8.0f)) return false;
          char b[200]; std::snprintf(b, sizeof b, "%s: one arm drawn %d (support %d, no trigger hand %d)%s", sid.c_str(), a.lastArmDraws, (int)a.lastArmsShown[1], (int)!a.lastArmsShown[0], shot ? "" : " (no reload)");
          check(a.lastArmDraws == 1 && !a.lastArmsShown[0] && a.lastArmsShown[1], b);
          a.scriptedInput = ExploreInput{};
          return true; });
      }
      push("reset", [this](App& a, float) { a.vm.procedural = true; a.exitExplore(); return true; });
      stage = 1101;
      return;
    }

    // ---- F1 import
    push("import", [this](App& a, float) {
      int models = 0;
      for (auto& dp : a.weaponDefs) {
        const WeaponDef& d = *dp;
        json j; try { j = json::parse(readTextFile(d.file)); } catch (...) {}
        if (!j.contains("model")) continue;
        ++models;
        bool ok = d.useModel();
        float L = 0, want = j["model"].value("overallLength", j["model"].value("realLength", 1.0f));   // overall length when the fit node is a sub-part (bazooka tube)
        if (ok) { float z0 = 1e9f, z1 = -1e9f; for (auto& mp : d.model->parts) if (mp.role != "clip") { z0 = std::min(z0, mp.bmin.z); z1 = std::max(z1, mp.bmax.z); } L = z1 - z0; }   // loose rounds / rockets excluded
        char b[220]; std::snprintf(b, sizeof b, "%s: imported model %s, %d tris, %zu parts, length %.3f m vs real %.3f m (%.2f%%)",
                                   d.id.c_str(), j["model"].value("dir", "").c_str(), ok ? d.model->tris : 0, ok ? d.model->parts.size() : 0, L, want, ok ? 100 * std::fabs(L - want) / want : 100.0f);
        check(ok && std::fabs(L - want) / want < 0.01f, b);
        bool texExpected = true;   // every imported weapon is textured now (bazooka: Metal046B set)
        bool nrmExpected = d.id != "panzerschreck";                          // the pickup mesh ships no normal map
        if (ok) check(!texExpected || (d.model->texAlb && d.model->texMR && (d.model->texN || !nrmExpected)), d.id + (texExpected ? (nrmExpected ? ": albedo, metal-rough and normal textures bound" : ": albedo and metal-rough bound (source has no normal map)") : ": factor-only materials (source has no textures)"));
      }
      check(models == 21, "twenty-one weapon entries use imported models (Garand, M1C, Carbine, Thompson, BAR, bazooka, carried M2, Kar 98k, ZF39, MP 40, FG 42, MG 42, Panzerschreck, Mk 2, M24, knife, StG 44, M3, smoke grenade, two flamethrowers); " + std::to_string(models) + " found");
      GpuModel g; std::string err;
      check(!g.load(contentDir() / "weapons" / "models" / "does_not_exist", err), "a missing bake fails cleanly (" + err + ") and the weapon keeps its box parts");
      int box = a.weaponIndex("kar98k");
      check(box >= 0 && a.weaponDefs[box]->useModel() && a.weaponDefs[box]->mesh[0].count > 0, "Kar 98k uses the supplied model; its box parts remain as the fallback");
      const WeaponDef* g1 = a.weaponDefs[a.weaponIndex("m1_garand")].get();
      int clip = 0, anim = 0; for (auto& p : g1->model->parts) { clip += p.role == "clip"; anim += !p.anim.empty(); }
      check(clip == 2 && anim >= 5 && g1->model->animDuration > 6.0f, "Garand: clip + rounds mapped to the clip role, " + std::to_string(anim) + " parts carry the 6.3 s reload clip");
      return true; });

    // ---- F2 ADS alignment for every weapon at both world FOVs
    for (size_t wi = 0; wi < 16; ++wi) {
      push("ads " + std::to_string(wi), [this, wi](App& a, float t) {
        if (wi >= a.weaponDefs.size()) return true;
        if (t == 0) { a.selectWeapon((int)wi); a.vm.procedural = false; a.eyeYaw = 0; a.eyePitch = 0; }
        a.scriptedInput.aim = true;
        if (t < 0.9f) return false;
        const WeaponDef& d = D(a); auto& s = a.wstate[d.id];
        if (a.vmScoped(d)) {   // W2: the weapon stays drawn under the eyepiece, the scope eye on the centre
          vec3 r, f; a.sightLineWorld(d, s, r, f);
          mat4 vp = glm::perspective(radians(a.vm.vmFov), 16.0f / 9.0f, 0.03f, 100.0f) * a.viewM;
          float worst = std::max(pxFromCentre(vp, r), pxFromCentre(vp, f));
          char b[200]; std::snprintf(b, sizeof b, "%s: scoped, the weapon stays drawn (%zu + %zu items) with the scope eye on the screen centre (%.2f px), eyepiece view %s", d.id.c_str(), a.R.viewModel.size(), a.R.viewModelTex.size(), worst, a.scopeView() ? "on" : "OFF");
          check(a.scopeView() && !(a.R.viewModel.empty() && a.R.viewModelTex.empty()) && worst < 0.5f, b);
          return true;
        }
        if (d.action == "heavy") { check(a.adsT == 0, d.id + ": carried heavy weapon has no sights (deploy it to aim)"); return true; }
        if (d.equipment()) { check(a.adsT == 0, d.id + ": hand equipment (grenade / knife) has no sights"); return true; }
        if (d.flame.on) return true;   // FT1: no sight; ADS is a zoom from the hip pose
        mat4 M = a.viewModelMatrix(d, s);
        vec3 r = vec3(M * vec4(d.rear, 1)), f = vec3(M * vec4(d.front, 1));
        float aspect = 16.0f / 9.0f;
        float worst = 0;
        for (float fov : {72.0f, 58.0f, a.vm.vmFov}) {
          mat4 vp = glm::perspective(radians(fov), aspect, 0.03f, 100.0f) * a.viewM;
          worst = std::max({worst, pxFromCentre(vp, r), pxFromCentre(vp, f)});
        }
        char b[200]; std::snprintf(b, sizeof b, "%s: rear and front sight on the screen centre at world FOV 72 and 58 and weapon FOV %.0f (worst %.2f px)", d.id.c_str(), a.vm.vmFov, worst);
        check(worst < 0.5f, b);
        return true; });
    }
    push("ads spring", [this](App& a, float t) {
      static float settle = -1;
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); a.vm.procedural = false; settle = -1; a.scriptedInput = ExploreInput{}; return false; }
      if (t < 0.5f) return false;
      a.scriptedInput.aim = true;
      vec3 tgt = a.vmTarget(D(a));
      if (settle < 0 && a.adsT >= 1.0f && glm::length(a.vm.pos - tgt) < 0.0005f) settle = t - 0.5f;
      if (t < 1.6f) return false;
      check(settle > 0.1f && settle < 0.8f, "hip -> ADS on a critically damped spring (omega 18/s, blend adsTime_s 0.25): within 0.5 mm after " + std::to_string(settle).substr(0, 4) + " s, no overshoot needed");
      float fov = 0; check(a.weaponCamera(fov) && fov < 72 && a.vm.vmFov == 60, "world FOV narrows to " + std::to_string((int)fov) + " in ADS while the weapon keeps its fixed 60 deg projection");
      return true; });

    // ---- F2 sight-origin ballistics
    push("sight origin", [this](App& a, float t) {
      static int s0;
      if (t == 0) { a.vm.procedural = true; s0 = a.shotsFired; }
      a.scriptedInput.aim = true;
      if (t < 1.0f) return false;
      if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 5; }
      a.scriptedInput.fire = false;
      float off = glm::length(a.lastShotStart - a.lastSightF);
      float ang = glm::length(glm::cross(a.lastSightV, glm::normalize(a.lastSightF - a.lastSightR)));   // sin(angle): acos is too coarse near 1 in float
      char b[200]; std::snprintf(b, sizeof b, "ADS shot leaves the front sight tip (%.3f mm away) along normalize(F - R) (%.2e rad)", off * 1000, ang);
      check(off < 0.001f && ang < 1e-4f, b);
      float dev = std::acos(std::clamp(glm::dot(glm::normalize(a.lastShotDir), a.lastSightV), -1.0f, 1.0f));
      check(degrees(dev) < 0.2f, "dispersion + zero tilt keep the round within 0.2 deg of the sight line (" + std::to_string(degrees(dev)).substr(0, 5) + " deg)");
      return true; });
    push("hip origin", [this](App& a, float t) {
      static int s0;
      if (t == 0) { s0 = a.shotsFired; a.scriptedInput = ExploreInput{}; }
      if (t < 0.6f) return false;
      if (a.shotsFired == s0) { a.scriptedInput.fire = !a.scriptedInput.fire; return t > 5; }
      a.scriptedInput.fire = false;
      check(glm::length(a.lastShotStart - a.lastShotMuzzle) < 0.02f, "hip fire still sweeps from the muzzle");
      return true; });

    // ---- F3 sway, bob, breathing
    push("sway", [this](App& a, float t) {
      static float peak = 0, tStop = -1;
      if (t == 0) { a.vm.procedural = true; peak = 0; tStop = -1; a.scriptedInput = ExploreInput{}; }
      if (t < 0.25f) { a.scriptedInput.dyaw = 0.6f; a.scriptedInput.dpitch = 0.3f; }
      else { a.scriptedInput.dyaw = a.scriptedInput.dpitch = 0; if (tStop < 0) tStop = t; }
      peak = std::max(peak, glm::length(a.vm.swayP));
      if (tStop < 0 || t < tStop + 0.5f) return false;
      float now = glm::length(a.vm.swayP);
      char b[160]; std::snprintf(b, sizeof b, "mouse sway: peak %.1f mm opposite the look, %.3f%% left 0.5 s after the mouse stops", peak * 1000, peak > 0 ? 100 * now / peak : 100.0f);
      check(peak > 0.002f && now < 0.01f * peak, b);
      return true; });
    push("bob", [this](App& a, float t) {
      static float walk = 0, sprint = 0, peakY = 0;
      if (t == 0) { walk = sprint = peakY = 0; a.scriptedInput = ExploreInput{}; a.eyeYaw = 90; }
      a.scriptedInput.move = vec2(0, 1);
      a.scriptedInput.sprint = t > 1.2f;
      if (t > 0.8f && t < 1.2f) walk = std::max(walk, a.vm.bobAmp);
      if (t > 2.0f && t < 2.4f) { sprint = std::max(sprint, a.vm.bobAmp); peakY = std::max(peakY, std::fabs(a.vm.bob.y)); }
      if (t < 2.4f) return false;
      a.scriptedInput = ExploreInput{};
      char b[180]; std::snprintf(b, sizeof b, "bob scales with speed: walk %.2f, sprint %.2f (vertical peak %.1f mm at sprint, 12 mm max)", walk, sprint, peakY * 1000);
      check(walk > 0.3f && sprint > walk * 1.6f && peakY <= 0.0121f && peakY > 0.006f, b);
      return true; });
    push("breathing", [this](App& a, float t) {
      static float lo = 1, hi = -1, heldLo = 1, heldHi = -1, holdStart = 0;
      static bool forced = false;
      if (t == 0) { lo = 1; hi = -1; heldLo = 1; heldHi = -1; forced = false; holdStart = 0; a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; }
      a.scriptedInput.aim = true;
      if (t > 1.0f && t < 4.0f) { lo = std::min(lo, a.vm.breathPitch); hi = std::max(hi, a.vm.breathPitch); }
      a.scriptedInput.sprint = t > 4.0f;
      if (t > 4.0f && holdStart == 0) holdStart = t;
      if (t > 4.3f && t < 8.5f) { heldLo = std::min(heldLo, a.vm.breathPitch); heldHi = std::max(heldHi, a.vm.breathPitch); }
      if (t > 9.6f && a.vm.breathRecover > 0) forced = true;
      if (t < 10.0f) return false;
      a.scriptedInput = ExploreInput{};
      char b[200]; std::snprintf(b, sizeof b, "idle breathing wanders %.3f deg; holding breath (Left Shift in ADS) freezes it (%.4f deg change over 4 s)", hi - lo, heldHi - heldLo);
      check(hi - lo > 0.05f && heldHi - heldLo < 1e-4f, b);
      check(forced, "after 5 s the breath is released: 3 s recovery at double amplitude");
      return true; });
    push("sprint in ads", [this](App& a, float t) {
      static vec3 p0;
      if (t == 0) { p0 = Physics::character().pos; a.scriptedInput = ExploreInput{}; a.eyeYaw = 180; }
      a.scriptedInput.aim = true; a.scriptedInput.sprint = true; a.scriptedInput.move = vec2(0, 1);
      if (t < 1.5f) return false;
      float v = glm::length(vec2(Physics::character().pos.x - p0.x, Physics::character().pos.z - p0.z)) / 1.5f;
      a.scriptedInput = ExploreInput{};
      check(v < a.move.run * 1.05f, "Shift while aiming does not sprint (" + std::to_string(v).substr(0, 4) + " m/s, run " + std::to_string(a.move.run).substr(0, 4) + ")");
      return true; });

    // ---- F4 IK arms
    push("ik", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); a.scriptedInput = ExploreInput{}; return false; }
      if (t < 0.5f) return false;
      // FA2: one generated support arm for every weapon: no trigger hand, the hand on its grip, the faction's sleeve
      float worst = 0; int one = 0, faction = 0, n = 0, adsDrawn = 0, adsN = 0, guns = 0, framed = 0; std::string unframed;
      auto armCount = [&](int& fac) { int c = 0; fac = -1; for (auto& it : a.R.viewModelTex) if (fpsArmsIsModel(it.model)) { ++c; fac = fpsArmsFaction(it.model); } return c; };
      for (size_t k = 0; k < a.weaponDefs.size(); ++k) {
        a.selectWeapon((int)k); a.adsT = 0;
        a.R.viewModelTex.clear(); std::vector<DrawItem> tmp; a.weaponDrawItems(tmp);
        int fac = -1, c = armCount(fac); ++n;
        worst = std::max(worst, a.lastHandErr.y);
        one += a.lastArmDraws == 1 && !a.lastArmsShown[0] && a.lastArmsShown[1] && c == 5;
        faction += fac == (D(a).side == "DE" ? 1 : 0);
        vec2 hLo, hHi, sl; fpsArmsLastFraming(hLo, hHi, sl);   // at the hip: the whole hand and 15 cm of sleeve past the cuff in view
        bool inView = a.lastArmsShown[1] && hLo.x > -0.99f && hHi.x < 0.99f && hHi.y > -0.8f && hHi.y < 0.99f;   // the hand on screen (the sleeve enters from the bottom edge; the cuff covers the wrist)
        if (!D(a).equipment()) { ++guns; framed += inView; if (!inView) unframed += " " + D(a).id; }
        if (!D(a).equipment() && D(a).action != "heavy") {   // ADS: the hand drops out of the sight cone, or is not drawn
          a.adsT = 1; a.R.viewModelTex.clear(); a.weaponDrawItems(tmp); ++adsN; adsDrawn += armCount(fac) > 0; a.adsT = 0;
        }
        vec3 ax = glm::normalize(glm::mat3(a.vmLocal(D(a), a.wstate[D(a).id])) * vec3(0, 0, 1));   // the barrel axis (camera space)
        char b[260]; std::snprintf(b, sizeof b, "  arm %-14s %s, sleeve %s, %d parts, hand %.1f mm from its grip; hip NDC hand (%.2f, %.2f)-(%.2f, %.2f) sleeve+15cm (%.2f, %.2f)%s heel (%.3f %.3f %.3f) axis (%.3f %.3f %.3f)", D(a).id.c_str(), a.lastArmsShown[1] ? "shown" : "HIDDEN", fac == 1 ? "axis" : fac == 0 ? "allies" : "-", c, a.lastHandErr.y * 1000,
                                     hLo.x, hLo.y, hHi.x, hHi.y, sl.x, sl.y, inView ? "" : " OUT OF VIEW", a.lastHandL.x, a.lastHandL.y, a.lastHandL.z, ax.x, ax.y, ax.z);
        results.push_back(b);
      }
      check(one == n, "one support arm (sleeve, cuff, hand) and no trigger hand drawn for every weapon (" + std::to_string(one) + " of " + std::to_string(n) + ")");
      check(faction == n, "sleeve colour per faction: olive drab for US weapons, charcoal for German (" + std::to_string(faction) + " of " + std::to_string(n) + ")");
      check(framed == guns, "at the hip every gun shows its hand on screen, the sleeve entering from the bottom (" + std::to_string(framed) + " of " + std::to_string(guns) + (unframed.empty() ? "" : "; out:" + unframed) + ")");
      {   // the arm is a fixed body: the same upper-arm / forearm lengths whatever the weapon (short SMG, rifle, MG, launcher, flamethrower)
        float lu0 = -1, lf0 = -1, dev = 0; std::string rows;
        for (const char* id : {"mp40", "m1_garand", "mg42", "bazooka_m9", "m2_flamethrower"}) {
          int wi = a.weaponIndex(id); if (wi < 0) continue;
          a.selectWeapon(wi); a.adsT = 0; a.R.viewModelTex.clear(); std::vector<DrawItem> tmp; a.weaponDrawItems(tmp);
          vec3 S, E, W, T; fpsArmsLastJoints(S, E, W, T);
          float lu = glm::length(E - S), lf = glm::length(W - E);
          if (lu0 < 0) { lu0 = lu; lf0 = lf; }
          dev = std::max({dev, std::fabs(lu - lu0), std::fabs(lf - lf0)});
          char b[200]; std::snprintf(b, sizeof b, "  bones %-16s upper arm %.4f m, forearm %.4f m, shoulder-wrist %.3f m, wrist %.1f mm from its target, elbow (%.2f %.2f %.2f)", id, lu, lf, glm::length(W - S), glm::length(W - T) * 1000, E.x, E.y, E.z);
          results.push_back(b);
        }
        check(lu0 > 0 && dev < 1e-4f, "arm segment lengths identical across SMG / rifle / MG / launcher / flamethrower (upper " + std::to_string(lu0).substr(0, 5) + " m, forearm " + std::to_string(lf0).substr(0, 5) + " m, worst deviation " + std::to_string(dev * 1000).substr(0, 5) + " mm)");
      }
      check(worst < 0.001f, "at the hip the support hand reaches its grip for every weapon: the wrist on its target, no stretch (worst " + std::to_string(worst * 1000).substr(0, 5) + " mm)");
      results.push_back("  ADS: the arm stays drawn (clear of the 6 deg sight cone) for " + std::to_string(adsDrawn) + " of " + std::to_string(adsN) + " sighted weapons; the rest hide it");
      check(adsN > 0 && adsDrawn * 2 >= adsN, "ADS: the support hand is lowered out of the sight cone and stays visible for most weapons (" + std::to_string(adsDrawn) + " / " + std::to_string(adsN) + ")");
      {   // the setting off: no arm; a scoped weapon in ADS: no arm (weapons.cpp skips it in the scope view)
        a.selectWeapon(a.weaponIndex("m1_garand")); a.vm.showArms = false; a.R.viewModelTex.clear(); std::vector<DrawItem> tmp; a.weaponDrawItems(tmp);
        int fac, off = armCount(fac); a.vm.showArms = true;
        check(off == 0, "Show arms off: no arm drawn");
        int sc = a.weaponIndex("m1c_garand");
        if (sc >= 0) { a.selectWeapon(sc); a.adsT = 1; a.R.viewModelTex.clear(); a.weaponDrawItems(tmp); int s2 = armCount(fac); bool scoped = a.scopeView(); a.adsT = 0;
          check(scoped && s2 == 0, "scoped view (M1C in ADS): no arm drawn"); }
      }
      { a.selectWeapon(a.weaponIndex("m1_garand")); a.adsT = 0; a.R.viewModelTex.clear(); a.R.viewModel.clear(); std::vector<DrawItem> tmp; a.weaponDrawItems(tmp); }   // the Garand at the hip for the checks below
      vec3 e, h, S(0), T(0, 0, -2);
      solveTwoBone(S, T, kUpperArm, kForearm, vec3(0, -1, 0), e, h);
      check(std::fabs(glm::length(e - S) - kUpperArm) < 1e-4f && std::fabs(glm::length(h - e) - kForearm) < 1e-3f && glm::length(h - T) > 1.3f, "out of reach: the arm straightens toward the target without stretching");
      int armItems = 0; for (auto& it : a.R.viewModel) for (auto& m : a.armMesh) armItems += it.mesh == &m;
      int armModel = 0; for (auto& it : a.R.viewModelTex) armModel += fpsArmsIsModel(it.model);   // FA2: the generated arm
      check(a.lastArmDraws == 1 && (fpsArmsReady() ? armModel == 5 && armItems == 0 : armItems == 8),
            fpsArmsReady() ? "the generated support arm is drawn in the first-person textured pass (sleeve, cuff, hand; no box arms)" : "box arms drawn in the first-person pass (2 x upper arm, forearm, wrist, hand): " + std::to_string(armItems));
      a.selectWeapon(a.weaponIndex("m1_garand"));
      return true; });

    // ---- F3 depth of field
    push("dof", [this](App& a, float t) {
      if (t == 0) a.scriptedInput = ExploreInput{};
      a.scriptedInput.aim = true;
      if (t < 1.0f) return false;
      float focus, amount; a.dofParams(focus, amount);
      const WeaponDef& d = D(a);
      float want = d.eyeRelief + glm::length(d.front - d.rear) + 0.03f;
      auto coc = [&](float dd) { return amount * std::clamp((focus - dd) / (0.6f * focus), 0.0f, 1.0f) * 6.0f; };
      char b[200]; std::snprintf(b, sizeof b, "ADS focus %.3f m (front post + 3 cm), blend %.2f; CoC at the rear aperture %.1f px, at the front post %.1f px, at the target 0", focus, amount, coc(d.eyeRelief), coc(focus));
      check(std::fabs(focus - want) < 1e-4f && amount > 0.99f && coc(d.eyeRelief) > 5.9f && coc(focus) == 0.0f, b);
      a.reducedMotion = true; a.dofParams(focus, amount); a.reducedMotion = false;
      check(amount == 0, "reduced motion turns the sight blur off");
      check(!a.R.dofEnabled, "sight blur is removed from play: the depth-of-field pass is off by default with no toggle");
      return true; });

    // ---- screenshots of every imported weapon, hip and ADS (procedural motion on)
    for (size_t wi = 0; wi < 16; ++wi) {
      push("shots " + std::to_string(wi), [this, wi](App& a, float t) {
        if (wi >= a.weaponDefs.size()) return true;
        if (t == 0) { a.selectWeapon((int)wi); a.vm.procedural = true; a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = -2; }
        const std::string id = D(a).id;
        if (t > 0.6f && t < 0.62f) shotQueue.push_back("fps_" + id + "_hip");
        if (t > 0.7f) a.scriptedInput.aim = true;
        if (t > 1.8f && t < 1.82f) shotQueue.push_back("fps_" + id + "_ads");
        if (t < 2.0f) return false;
        a.scriptedInput = ExploreInput{};
        GLenum e = glGetError();
        check(e == GL_NO_ERROR, id + ": hip and ADS frames rendered without GL errors");
        return true; });
    }
    push("reload clip", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); a.wstate["m1_garand"].loaded = 0; a.scriptedInput = ExploreInput{}; return false; }
      if (t > 0.3f && t < 0.35f) a.scriptedInput.reload = true; else a.scriptedInput.reload = false;
      auto& s = a.wstate["m1_garand"];
      if (s.reload > 0 && s.reload < s.reloadTotal * 0.55f && s.reload > s.reloadTotal * 0.5f) shotQueue.push_back("fps_m1_garand_reload");
      if (t < 3.5f) return false;
      check(s.loaded == 8, "Garand reload drives the imported clip animation and seats 8 rounds");
      return true; });
    push("reset", [this](App& a, float) {
      a.vm.procedural = true;
      a.exitExplore();
      check(a.mode == Mode::Creative, "back to Creative");
      return true; });
  }
  stage = 1101;
  if (fcur >= fseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled (" + std::to_string(app.R.shaderErrors) + " errors)"); finish(app); return; }
  if (ft == 0) logf("[selftest] step: %s", fseq[fcur].name.c_str());
  bool d2 = fseq[fcur].fn(app, ft);
  ft += dt;
  if (ft > 60) { check(false, "step timed out: " + fseq[fcur].name); d2 = true; }
  if (d2) { ++fcur; ft = 0; }
}
