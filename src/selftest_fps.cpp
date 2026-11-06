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

    // ---- F1 import
    push("import", [this](App& a, float) {
      int models = 0;
      for (auto& dp : a.weaponDefs) {
        const WeaponDef& d = *dp;
        json j; try { j = json::parse(readTextFile(d.file)); } catch (...) {}
        if (!j.contains("model")) continue;
        ++models;
        bool ok = d.useModel();
        float L = ok ? d.model->bmax.z - d.model->bmin.z : 0, want = j["model"].value("realLength", 1.0f);
        char b[220]; std::snprintf(b, sizeof b, "%s: imported model %s, %d tris, %zu parts, length %.3f m vs real %.3f m (%.2f%%)",
                                   d.id.c_str(), j["model"].value("dir", "").c_str(), ok ? d.model->tris : 0, ok ? d.model->parts.size() : 0, L, want, ok ? 100 * std::fabs(L - want) / want : 100.0f);
        check(ok && std::fabs(L - want) / want < 0.01f, b);
        bool texExpected = d.id != "m1_thompson";
        if (ok) check(!texExpected || (d.model->texAlb && d.model->texMR && d.model->texN), d.id + (texExpected ? ": albedo, metal-rough and normal textures bound" : ": factor-only materials (source has no textures)"));
      }
      check(models == 6, "six weapon entries use imported models (Garand, M1C, MP 40, Thompson, MG 42, BAR); " + std::to_string(models) + " found");
      GpuModel g; std::string err;
      check(!g.load(contentDir() / "weapons" / "models" / "does_not_exist", err), "a missing bake fails cleanly (" + err + ") and the weapon keeps its box parts");
      int box = a.weaponIndex("kar98k");
      check(box >= 0 && !a.weaponDefs[box]->useModel() && a.weaponDefs[box]->mesh[0].count > 0, "Kar 98k still draws its procedural box model");
      const WeaponDef* g1 = a.weaponDefs[a.weaponIndex("m1_garand")].get();
      int clip = 0, anim = 0; for (auto& p : g1->model->parts) { clip += p.role == "clip"; anim += !p.anim.empty(); }
      check(clip == 2 && anim >= 5 && g1->model->animDuration > 6.0f, "Garand: clip + rounds mapped to the clip role, " + std::to_string(anim) + " parts carry the 6.3 s reload clip");
      return true; });

    // ---- F2 ADS alignment for every weapon at both world FOVs
    for (size_t wi = 0; wi < 8; ++wi) {
      push("ads " + std::to_string(wi), [this, wi](App& a, float t) {
        if (wi >= a.weaponDefs.size()) return true;
        if (t == 0) { a.selectWeapon((int)wi); a.vm.procedural = false; a.eyeYaw = 0; a.eyePitch = 0; }
        a.scriptedInput.aim = true;
        if (t < 0.9f) return false;
        const WeaponDef& d = D(a); auto& s = a.wstate[d.id];
        if (a.vmScoped(d)) { check(true, d.id + ": scoped, sight picture replaced by the scope mask"); return true; }
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
      check(settle > 0.1f && settle < 0.6f, "hip -> ADS on a critically damped spring (omega 18/s): within 0.5 mm after " + std::to_string(settle).substr(0, 4) + " s, no overshoot needed");
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
      check(v < 2.6f, "Shift while aiming does not sprint (" + std::to_string(v).substr(0, 4) + " m/s)");
      return true; });

    // ---- F4 IK arms
    push("ik", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); a.scriptedInput = ExploreInput{}; return false; }
      if (t < 0.5f) return false;
      float worst = 0; bool elbowsDown = true;
      for (size_t k = 0; k < a.weaponDefs.size(); ++k) {
        a.selectWeapon((int)k);
        std::vector<DrawItem> tmp; a.weaponDrawItems(tmp);
        worst = std::max({worst, a.lastHandErr.x, a.lastHandErr.y});
        auto below = [](vec3 S, vec3 E, vec3 H) { vec3 u = glm::normalize(H - S); vec3 c = S + u * glm::dot(E - S, u); return E.y < c.y - 0.01f; };
        bool bR = below(a.lastShoulderR, a.lastElbowR, a.lastHandR), bL = below(a.lastShoulderL, a.lastElbowL, a.lastHandL);
        if (!bR || !bL) elbowsDown = false;
        char b[200]; std::snprintf(b, sizeof b, "  ik %-12s right %.1f mm left %.1f mm, elbows %s/%s, reach R %.3f L %.3f", D(a).id.c_str(), a.lastHandErr.x * 1000, a.lastHandErr.y * 1000, bR ? "down" : "UP", bL ? "down" : "UP",
                      glm::length(a.lastHandR - a.lastShoulderR), glm::length(a.lastHandL - a.lastShoulderL));
        results.push_back(b);
      }
      check(worst < 0.001f, "two-bone IK: both hands on their sockets for every weapon (worst residual " + std::to_string(worst * 1000).substr(0, 5) + " mm)");
      check(elbowsDown, "elbows hang below the shoulder-hand line (pole vector down and outward)");
      vec3 e, h, S(0), T(0, 0, -2);
      solveTwoBone(S, T, kUpperArm, kForearm, vec3(0, -1, 0), e, h);
      check(std::fabs(glm::length(e - S) - kUpperArm) < 1e-4f && std::fabs(glm::length(h - e) - kForearm) < 1e-3f && glm::length(h - T) > 1.3f, "out of reach: the arm straightens toward the target without stretching");
      int armItems = 0; for (auto& it : a.R.viewModel) for (auto& m : a.armMesh) armItems += it.mesh == &m;
      check(armItems == 8, "arms drawn in the first-person pass (2 x upper arm, forearm, wrist, hand): " + std::to_string(armItems));
      a.selectWeapon(a.weaponIndex("m1_garand"));
      return true; });

    // ---- F3 depth of field
    push("dof", [this](App& a, float t) {
      if (t == 0) a.scriptedInput = ExploreInput{};
      a.scriptedInput.aim = true;
      if (t < 1.0f) return false;
      float focus, amount; a.dofParams(focus, amount);
      const WeaponDef& d = D(a);
      float want = 0.21f + glm::length(d.front - d.rear) + 0.03f;
      auto coc = [&](float dd) { return amount * std::clamp((focus - dd) / (0.6f * focus), 0.0f, 1.0f) * 6.0f; };
      char b[200]; std::snprintf(b, sizeof b, "ADS focus %.3f m (front post + 3 cm), blend %.2f; CoC at the rear aperture %.1f px, at the front post %.1f px, at the target 0", focus, amount, coc(0.21f), coc(focus));
      check(std::fabs(focus - want) < 1e-4f && amount > 0.99f && coc(0.21f) > 5.9f && coc(focus) == 0.0f, b);
      a.reducedMotion = true; a.dofParams(focus, amount); a.reducedMotion = false;
      check(amount == 0, "reduced motion turns the sight blur off");
      return true; });

    // ---- screenshots of every imported weapon, hip and ADS (procedural motion on)
    for (size_t wi = 0; wi < 8; ++wi) {
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
  if (fcur >= fseq.size()) { finish(app); return; }
  if (ft == 0) logf("[selftest] step: %s", fseq[fcur].name.c_str());
  bool d2 = fseq[fcur].fn(app, ft);
  ft += dt;
  if (ft > 60) { check(false, "step timed out: " + fseq[fcur].name); d2 = true; }
  if (d2) { ++fcur; ft = 0; }
}
