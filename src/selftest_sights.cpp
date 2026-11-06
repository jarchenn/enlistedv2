// --selftest-sights: per-weapon ADS calibration of the FG 42 and the MG 42, measured on the *rendered* weapon.
// The first-person pass writes each pixel's eye distance (vmDist, the depth-of-field attachment); the rear sight
// (~0.2 m) and the front sight (~0.75 m) separate cleanly by distance, so the aperture hole, the front post tip and
// the MG 42's V-notch are found in the weapon-projection image itself and compared with the fixed centre dot.
// Also: hip-fire flash / tracer origin on the drawn barrel opening, projection-consistent tracer start, aspect
// ratios, recoil, sway, posture, reload, a close wall, and that every other weapon keeps its default calibration.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> sseq; size_t scur = 0; float stt = 0;
uint64_t SWALL = 0;
WeaponDef& WD(App& a) { return *a.weaponDefs[a.activeWeapon]; }

struct Probe { std::vector<float> d; int w = 0, h = 0; };
struct Meas {
  bool rearOk = false, frontOk = false;
  vec2 rear{0}, front{0};          // framebuffer pixels, origin top-left (pixel edges)
  float errRear = 1e9f, errFront = 1e9f, leafW = 0;
};
// legacy calibration (before this pass) for the before/after comparison
struct Calib { vec3 rear, front, muzzle, gripR, gripL; float relief; };
Calib grab(const WeaponDef& d) { return {d.rear, d.front, d.muzzle, d.gripR, d.gripL, d.eyeRelief}; }
void apply(WeaponDef& d, const Calib& c) { d.rear = c.rear; d.front = c.front; d.muzzle = c.muzzle; d.gripR = c.gripR; d.gripL = c.gripL; d.eyeRelief = c.relief; }
const Calib kLegacyFG42 = {{0, 0.088f, 0.30f}, {0, 0.100f, 0.855f}, {0, 0.03f, 0.975f}, {0, -0.05f, 0.29f}, {0, 0.02f, 0.51f}, 0.21f};
const Calib kLegacyMG42 = {{0, 0.087f, 0.62f}, {0, 0.087f, 1.05f}, {0, 0.034f, 1.22f}, {0, -0.06f, 0.33f}, {0, 0.005f, 0.72f}, 0.21f};

Probe probe;
bool probeWanted = false, probeReady = false;
// request a vmDist read-back after this frame's render, then run fn on the next frame
bool probeThen(float t, float when, const std::function<void()>& fn) {
  if (t < when) { probeReady = false; return false; }
  if (!probeReady) { probeWanted = true; return false; }
  probeReady = false; fn(); return true;
}

float at(const Probe& p, int x, int y) { return p.d[(size_t)(p.h - 1 - y) * p.w + x]; }   // y from the top

// FG 42: diopter ring (near) around an enclosed hole; the front post (far) seen through it.
Meas measureAperture(const Probe& p, float nearMax, float farMin, float farMax) {
  Meas m;
  int cx = p.w / 2, cy = p.h / 2, R = 220;
  int x0 = std::max(1, cx - R), x1 = std::min(p.w - 2, cx + R), y0 = std::max(1, cy - R), y1 = std::min(p.h - 2, cy + R);
  auto nearPx = [&](int x, int y) { float d = at(p, x, y); return d > 0.001f && d < nearMax; };
  int W = x1 - x0 + 1, H = y1 - y0 + 1;
  std::vector<int> lab((size_t)W * H, 0);
  int best = -1; float bestScore = -1; std::vector<int> bestPts;
  int id = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      size_t k = (size_t)(y - y0) * W + (x - x0);
      if (lab[k] || nearPx(x, y)) continue;
      ++id; std::vector<int> st{(int)k}, pts; lab[k] = id; bool border = false;
      while (!st.empty()) {
        int q = st.back(); st.pop_back(); pts.push_back(q);
        int qx = q % W, qy = q / W;
        if (qx == 0 || qy == 0 || qx == W - 1 || qy == H - 1) border = true;
        const int nx[4] = {qx - 1, qx + 1, qx, qx}, ny[4] = {qy, qy, qy - 1, qy + 1};
        for (int n = 0; n < 4; ++n) {
          if (nx[n] < 0 || ny[n] < 0 || nx[n] >= W || ny[n] >= H) continue;
          size_t kk = (size_t)ny[n] * W + nx[n];
          if (!lab[kk] && !nearPx(nx[n] + x0, ny[n] + y0)) { lab[kk] = id; st.push_back((int)kk); }
        }
      }
      if (border || pts.size() < 20 || pts.size() > 6000) continue;
      int bx0 = 1 << 30, bx1 = -1, by0 = 1 << 30, by1 = -1;
      for (int q : pts) { bx0 = std::min(bx0, q % W); bx1 = std::max(bx1, q % W); by0 = std::min(by0, q / W); by1 = std::max(by1, q / W); }
      float ar = float(bx1 - bx0 + 1) / float(by1 - by0 + 1);
      float fill = pts.size() / float((bx1 - bx0 + 1) * (by1 - by0 + 1));
      if (ar < 0.6f || ar > 1.6f || fill < 0.6f) continue;   // round opening, not a sliver between parts
      float score = (float)pts.size();
      if (score > bestScore) { bestScore = score; best = id; bestPts = pts; }
    }
  if (best < 0) return m;
  double sx = 0, sy = 0;
  for (int q : bestPts) { sx += q % W + x0 + 0.5; sy += q / W + y0 + 0.5; }
  m.rear = vec2(float(sx / bestPts.size()), float(sy / bestPts.size()));
  m.rearOk = true;
  m.errRear = glm::length(m.rear - vec2(p.w * 0.5f, p.h * 0.5f));
  // front post through the opening: topmost far row, tip x = mean of that row
  int top = 1 << 30; for (int q : bestPts) { int x = q % W + x0, y = q / W + y0; float d = at(p, x, y); if (d >= farMin && d <= farMax) top = std::min(top, y); }
  if (top < (1 << 30)) {
    double tx = 0; int n = 0;
    for (int q : bestPts) { int x = q % W + x0, y = q / W + y0; float d = at(p, x, y); if (y == top && d >= farMin && d <= farMax) { tx += x + 0.5; ++n; } }
    m.front = vec2(float(tx / n), (float)top); m.frontOk = true;
    m.errFront = glm::length(m.front - vec2(p.w * 0.5f, p.h * 0.5f));
  }
  return m;
}

// MG 42: Y-shaped rear leaf (near) rising above the receiver; the aiming point is the bottom of its V-notch.
Meas measureNotch(const Probe& p, float nearMax) {
  Meas m;
  int cx = p.w / 2, cy = p.h / 2;
  int x0 = std::max(0, cx - 300), x1 = std::min(p.w - 1, cx + 300), y0 = std::max(0, cy - 320), y1 = std::min(p.h - 1, cy + 120);
  std::vector<int> top(x1 - x0 + 1, -1);
  for (int x = x0; x <= x1; ++x)
    for (int y = y0; y <= y1; ++y) { float d = at(p, x, y); if (d > 0.001f && d < nearMax) { top[x - x0] = y; break; } }
  std::vector<int> def; for (int t : top) if (t >= 0) def.push_back(t);
  if (def.size() < 10) return m;
  std::sort(def.begin(), def.end());
  int base = def[def.size() * 3 / 4];   // receiver line: most columns
  int ra = -1, rb = -1, a = -1;
  for (int k = 0; k <= (int)top.size(); ++k) {
    bool leaf = k < (int)top.size() && top[k] >= 0 && top[k] < base - 10;
    if (leaf && a < 0) a = k;
    if (!leaf && a >= 0) { if (k - 1 - a > rb - ra) { ra = a; rb = k - 1; } a = -1; }
  }
  if (ra < 0 || rb - ra < 6) return m;
  int mid = (ra + rb) / 2, lp = ra, rp = rb;
  for (int k = ra; k <= mid; ++k) if (top[k] < top[lp]) lp = k;
  for (int k = rb; k >= mid; --k) if (top[k] < top[rp]) rp = k;
  int lo = -1; for (int k = lp; k <= rp; ++k) lo = std::max(lo, top[k]);
  double sx = 0; int n = 0; for (int k = lp; k <= rp; ++k) if (top[k] == lo) { sx += k + x0 + 0.5; ++n; }
  if (!n || rp - lp < 3) return m;
  m.rear = vec2(float(sx / n), (float)lo); m.rearOk = true;
  m.errRear = glm::length(m.rear - vec2(p.w * 0.5f, p.h * 0.5f));
  m.leafW = float(rb - ra + 1);
  return m;
}
}

void SelfTest::sightsUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { sseq.push_back({n, f}); };
  if (dt < 0) {   // post-render: read the first-person distance attachment
    if (probeWanted) { app.R.readVmDist(probe.d, probe.w, probe.h); probeWanted = false; probeReady = true; }
    return;
  }
  if (stage == 1600) {
    reportName = "sights";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Sight calibration");
    SWALL = app.placeInstance(app.lib.find("small_stone_dairy"), vec3(0, 0, -40), 0.0f);
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(SWALL != 0 && app.enterExplore(), "range: stone dairy 40 m downrange, F5 into Explore");
    app.useScripted = true;
    app.R.dofEnabled = false;      // calibration with the sight blur off
    sseq.clear(); scur = 0; stt = 0;

    struct Spec { std::string id; bool aperture; Calib legacy; };
    for (Spec sp : {Spec{"fg42", true, kLegacyFG42}, Spec{"mg42", false, kLegacyMG42}}) {
      const std::string id = sp.id;
      auto measure = [sp](const Probe& p) { return sp.aperture ? measureAperture(p, 0.32f, 0.55f, 1.2f) : measureNotch(p, 0.32f); };
      auto fmt = [sp](const Meas& m) {
        char b[220];
        if (sp.aperture) std::snprintf(b, sizeof b, "aperture centre %s (%.1f, %.1f) err %.2f px, post tip %s (%.1f, %.1f) err %.2f px", m.rearOk ? "found" : "NOT FOUND", m.rear.x, m.rear.y, m.rearOk ? m.errRear : -1.0f, m.frontOk ? "seen through it" : "NOT IN THE APERTURE", m.front.x, m.front.y, m.frontOk ? m.errFront : -1.0f);
        else std::snprintf(b, sizeof b, "V-notch bottom %s (%.1f, %.1f) err %.2f px, leaf %.0f px wide", m.rearOk ? "found" : "NOT FOUND", m.rear.x, m.rear.y, m.rearOk ? m.errRear : -1.0f, m.leafW);
        return std::string(b);
      };
      auto pass = [sp](const Meas& m) { return m.rearOk && m.errRear <= 2.0f && (!sp.aperture || (m.frontOk && m.errFront <= 2.0f)); };
      auto ads = [id](App& a, float t) {   // common setup: this weapon, ADS, still procedural off
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; }
        a.scriptedInput.aim = true;
      };
      static Calib saved;

      // ---- before: the legacy anchors (sight line at x = 0 on an off-centre mesh)
      push(id + " before hip", [this, id, sp](App& a, float t) {
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); saved = grab(WD(a)); apply(WD(a), sp.legacy); a.vm.procedural = false; a.vm.snap = true; a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; }
        if (t > 0.5f && t < 0.52f) shotQueue.push_back("sights_" + id + "_before_hip");
        return t > 0.6f; });
      push(id + " before ads", [this, id, sp, ads, measure, fmt](App& a, float t) {
        ads(a, t);
        if (t > 1.0f && t < 1.02f) shotQueue.push_back("sights_" + id + "_before_ads");
        return probeThen(t, 1.1f, [&] {
          Meas m = measure(probe);
          check(true, id + " BEFORE (legacy anchors " + std::to_string(sp.legacy.rear.y).substr(0, 5) + " / " + std::to_string(sp.legacy.front.y).substr(0, 5) + " at x 0): " + fmt(m));
          summary[id]["before"] = {{"rearErrPx", m.rearOk ? m.errRear : -1}, {"frontErrPx", m.frontOk ? m.errFront : -1}, {"leafPx", m.leafW}};
          apply(WD(a), saved); a.vm.snap = true;
        }); });

      // ---- after: measured anchors + per-weapon calibration
      push(id + " hip", [this, id](App& a, float t) {
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.vm.snap = true; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; a.eyeYaw = 0; a.eyePitch = 0; }
        if (t > 0.5f && t < 0.52f) shotQueue.push_back("sights_" + id + "_hip");
        return t > 0.6f; });
      push(id + " ads", [this, id, ads, measure, fmt, pass](App& a, float t) {
        ads(a, t);
        if (t > 1.0f && t < 1.02f) { a.sightMarkers = false; shotQueue.push_back("sights_" + id + "_ads_clean"); }
        if (t > 1.1f && t < 1.12f) { a.sightMarkers = true; shotQueue.push_back("sights_" + id + "_ads"); }
        return probeThen(t, 1.2f, [&] {
          Meas m = measure(probe);
          const WeaponDef& d = WD(a);
          check(pass(m), id + " AFTER at " + std::to_string(probe.w) + "x" + std::to_string(probe.h) + ", world FOV 58, weapon FOV " + std::to_string((int)a.vmFovNow()) + ": " + fmt(m) + " (limit 2 px)");
          summary[id]["after"] = {{"rearErrPx", m.rearOk ? m.errRear : -1}, {"frontErrPx", m.frontOk ? m.errFront : -1}, {"leafPx", m.leafW}, {"res", {probe.w, probe.h}},
                                  {"rear", {d.rear.x, d.rear.y, d.rear.z}}, {"front", {d.front.x, d.front.y, d.front.z}}, {"muzzle", {d.muzzle.x, d.muzzle.y, d.muzzle.z}}, {"eyeRelief", d.eyeRelief}};
          std::vector<App::SightMark> mk; a.sightMarks(mk);
          vec2 c(probe.w * 0.5f, probe.h * 0.5f);
          char b[200]; std::snprintf(b, sizeof b, "%s: anchors through the weapon projection: rear %.2f px, front %.2f px from the dot; aim point %.2f px", id.c_str(), glm::length(mk[0].px - c), glm::length(mk[1].px - c), glm::length(mk[4].px - c));
          check(glm::length(mk[0].px - c) < 0.5f && glm::length(mk[1].px - c) < 0.5f && glm::length(mk[4].px - c) < 0.5f, b);
        }); });
      if (!sp.aperture)   // MG 42 sight picture: eye relief 0.186 m vs the shared 0.21 m
        push(id + " size", [this, id, ads, measure](App& a, float t) {
          static float wCal = 0;
          ads(a, t);
          if (t == 0) wCal = 0;
          if (wCal == 0) return probeThen(t, 1.0f, [&] { wCal = std::max(1.0f, measure(probe).leafW); WD(a).eyeRelief = 0.21f; a.vm.snap = true; }) && false;
          return probeThen(t, 2.0f, [&] {
            float w0 = measure(probe).leafW; const WeaponDef& d = WD(a);
            json j; try { j = json::parse(readTextFile(d.file)); } catch (...) {}
            WD(a).eyeRelief = j["ads"].value("eyeRelief", 0.21f); a.vm.snap = true;
            float r = wCal / std::max(1.0f, w0);
            char b[200]; std::snprintf(b, sizeof b, "MG 42 Y-leaf drawn %.0f px wide at eye relief %.3f m vs %.0f px at the shared 0.21 m: %.1f%% larger (want 10-15%%), mesh not scaled", wCal, WD(a).eyeRelief, w0, (r - 1) * 100);
            check(r >= 1.10f && r <= 1.15f, b);
            summary[id]["sizeRatio"] = r;
          }); });

      // ---- aspect ratios (weapon projection follows the framebuffer)
      for (glm::ivec2 sz : {glm::ivec2(1440, 1080), glm::ivec2(2400, 1080), glm::ivec2(1280, 720)}) {
        push(id + " aspect " + std::to_string(sz.x), [this, id, sz, ads, measure, fmt, pass](App& a, float t) {
          if (t == 0) glfwSetWindowSize(a.win, sz.x, sz.y);
          ads(a, t);
          return probeThen(t, 0.8f, [&] {
            Meas m = measure(probe);
            char b[60]; std::snprintf(b, sizeof b, " (aspect %.3f)", probe.w / float(std::max(1, probe.h)));
            check(pass(m), id + " at " + std::to_string(probe.w) + "x" + std::to_string(probe.h) + b + ": " + fmt(m));
            glfwSetWindowSize(a.win, 1920, 1080);
          }); });
      }
      push(id + " restore size", [](App& a, float t) { (void)a; return t > 0.5f; });

      // ---- recoil: mid-kick the sight picture stays on the dot (kick slides along the sight axis)
      push(id + " recoil", [this, id, ads, measure, fmt, pass](App& a, float t) {
        static int s0, frames;
        ads(a, t);
        if (t == 0) { s0 = a.shotsFired; frames = 0; a.wstate[id].loaded = 30; a.wstate[id].mode = 1; }
        if (t < 1.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        if (++frames < 3) return false;
        float kick = a.wstate[id].kick;
        return probeThen(t, 0, [&] {
          Meas m = measure(probe);
          char b[80]; std::snprintf(b, sizeof b, " during recoil (kick %.1f mm, view kick %.2f deg)", kick * 1000, a.viewKickPitch);
          check(pass(m) && kick > 0, id + b + ": " + fmt(m));
          a.wstate[id].mode = 0;
        }); });
      // ---- sway / bob / breathing on: after a flick and 1 s settle the picture is back on the dot
      push(id + " sway settle", [this, id, ads, measure, fmt, pass](App& a, float t) {
        ads(a, t);
        a.vm.procedural = true;
        if (t > 1.0f && t < 1.3f) { a.scriptedInput.dyaw = 0.5f; a.scriptedInput.dpitch = 0.25f; } else { a.scriptedInput.dyaw = a.scriptedInput.dpitch = 0; }
        if (t > 1.15f && t < 1.17f) shotQueue.push_back("sights_" + id + "_sway");
        return probeThen(t, 2.6f, [&] {
          Meas m = measure(probe);
          check(pass(m), id + " after sway (flick, 1.3 s settle, breathing on - it moves the camera, not the weapon): " + fmt(m));
          a.vm.procedural = false;
        }); });
      // ---- posture: crouched and prone eye heights
      for (int pose : {1, 2}) {
        push(id + (pose == 1 ? " crouch" : " prone"), [this, id, pose, ads, measure, fmt, pass](App& a, float t) {
          ads(a, t);
          a.scriptedInput.crouch = pose == 1; a.proneToggle = pose == 2;
          return probeThen(t, 1.6f, [&] {
            Meas m = measure(probe);
            check(pass(m), id + (pose == 1 ? " crouched" : " prone") + " (posture " + std::to_string((int)Physics::character().posture) + "): " + fmt(m));
            a.scriptedInput.crouch = false; a.proneToggle = false;
          }); });
      }
      push(id + " stand", [](App& a, float t) { a.scriptedInput = ExploreInput{}; a.proneToggle = false; return t > 1.5f; });
      // ---- reload: the dip returns to the same picture
      push(id + " reload", [this, id, ads, measure, fmt, pass](App& a, float t) {
        static int phase; static float tDone;
        ads(a, t);
        if (t == 0) { phase = 0; tDone = 0; a.wstate[id].loaded = 1; }
        auto& s = a.wstate[id];
        if (phase == 0) { a.scriptedInput.aim = false; a.scriptedInput.reload = t > 0.2f; if (s.reload > 0) { phase = 1; a.scriptedInput.reload = false; } return t > 4; }
        if (phase == 1) { a.scriptedInput.aim = false; if (s.reload <= 0) { phase = 2; tDone = t; } return t > 20; }
        if (t < tDone + 1.0f) return false;
        return probeThen(t, 0, [&] { Meas m = measure(probe); check(pass(m) && s.loaded > 1, id + " after a reload (" + std::to_string(s.loaded) + " loaded): " + fmt(m)); }); });

      // ---- hip fire: flash and tracer start on the drawn barrel opening, hits stay on the true path
      push(id + " hip fire", [this, id](App& a, float t) {
        static int s0;
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; a.eyeYaw = 0; a.eyePitch = 0; s0 = a.shotsFired; a.wstate[id].loaded = 30; a.wstate[id].mode = 1; a.fx.rangeMode = true; }
        if (t < 0.8f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        shotQueue.push_back("sights_" + id + "_hipfire");
        const WeaponDef& d = WD(a);
        std::vector<App::SightMark> mk; a.sightMarks(mk);
        float flashVsMuzzle = glm::length(mk[3].px - mk[2].px);   // live mapping (current camera)
        // fire-time camera (recoil has kicked the view since): drawn muzzle through the weapon projection vs the
        // stored flash / tracer start through the world projection
        float aspect = a.R.width / float(std::max(1, a.R.height));
        mat4 wVP = glm::perspective(radians(a.lastFireWorldFov), aspect, 0.03f, 4000.0f) * a.lastFireView;
        mat4 vVP = glm::perspective(radians(a.lastFireVmFov), aspect, 0.01f, 6.0f) * a.lastFireView;
        auto pj = [&](const mat4& vp, vec3 w) { vec4 c = vp * vec4(w, 1); return vec2((c.x / c.w * 0.5f + 0.5f) * a.R.width, (0.5f - c.y / c.w * 0.5f) * a.R.height); };
        auto px = [&](vec3 w) { return pj(wVP, w); };
        vec2 drawnMuzzle = pj(vVP, a.lastShotMuzzle);
        float visVsMuzzle = glm::length(px(a.lastVisStart) - drawnMuzzle), flashOff = glm::length(px(a.lastFlashPos) - drawnMuzzle);
        char b[260]; std::snprintf(b, sizeof b, "%s hip: flash origin %.2f px and tracer start %.2f px from the drawn muzzle; flash %.1f px ahead along the drawn bore; muzzle (%.3f, %.3f, %.3f) bore (%.0f, %.0f, %.0f)",
                                   id.c_str(), flashVsMuzzle, visVsMuzzle, flashOff, d.muzzle.x, d.muzzle.y, d.muzzle.z, d.muzzleDir.x, d.muzzleDir.y, d.muzzleDir.z);
        check(flashVsMuzzle < 0.5f && visVsMuzzle < 0.5f && flashOff < 40, b);
        // the old mapping used the true muzzle: how far off the barrel that was drawn
        std::snprintf(b, sizeof b, "%s hip: an effect at the true muzzle would be drawn %.1f px off the barrel (the vmFov/world-FOV mismatch this offset removes)", id.c_str(), glm::length(px(a.lastShotMuzzle) - drawnMuzzle));
        check(glm::length(px(a.lastShotMuzzle) - drawnMuzzle) > 2, b);
        // authoritative path untouched: the round starts at the true muzzle and only the drawing is offset
        check(glm::length(a.lastShotStart - a.lastShotMuzzle) < 0.001f, id + " hip: projectile starts at the true muzzle (" + std::to_string(glm::length(a.lastShotStart - a.lastShotMuzzle) * 1000).substr(0, 4) + " mm); the offset is visual only");
        // no kink: the drawn path's screen direction turns smoothly from the barrel to the true line
        vec3 off = a.lastVisStart - a.lastShotStart, dir = glm::normalize(a.lastShotDir);
        float maxTurn = 0, maxStep = 0; vec2 prev = px(a.lastVisStart), prevD(0);
        for (float s = 0.25f; s <= 12.0f; s += 0.25f) {
          float u = std::min(s / 6.0f, 1.0f), w = (1 - u) * (1 - u);
          vec2 q = px(a.lastShotStart + dir * s + off * w), dd = q - prev;
          if (glm::length(dd) > 1e-3f) {
            dd = glm::normalize(dd);
            if (prevD != vec2(0)) maxTurn = std::max(maxTurn, degrees(std::acos(std::clamp(glm::dot(dd, prevD), -1.0f, 1.0f))));
            prevD = dd;
          }
          maxStep = std::max(maxStep, glm::length(q - prev)); prev = q;
        }
        std::snprintf(b, sizeof b, "%s hip tracer: largest screen-direction change %.2f deg per 25 cm, no gap at the muzzle (fades into the true path by 6 m)", id.c_str(), maxTurn);
        check(maxTurn < 6.0f, b);
        a.wstate[id].mode = 0;
        return true; });
      push(id + " hip fire settle", [](App& a, float t) { a.fx.rangeMode = t < 0.4f; return t > 0.5f; });
      // ---- ADS fire (screenshot) and the sight-origin rule
      push(id + " ads fire", [this, id, ads](App& a, float t) {
        static int s0;
        ads(a, t);
        if (t == 0) { s0 = a.shotsFired; a.wstate[id].loaded = 30; a.wstate[id].mode = 1; }
        if (t < 1.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        shotQueue.push_back("sights_" + id + "_adsfire");
        check(glm::length(a.lastShotStart - a.lastSightF) < 0.001f, id + " ADS: the round leaves the calibrated front sight point along the sight line");
        a.wstate[id].mode = 0;
        return true; });
      // ---- close wall: the muzzle-obstruction sweep still stops the shot on the near side
      push(id + " close wall", [this, id](App& a, float t) {
        static int s0;
        if (t == 0) {
          a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = 0;
          Physics::Hit h; vec3 e = a.eyePos();
          float dist = Physics::castRayEx(e, vec3(0, 0, -1), 80, h) ? h.t : 40;
          Physics::setCharacterPos(vec3(0, a.terrain.height(0, -(dist - 0.55f)), -(dist - 0.55f)));
          s0 = a.shotsFired; a.wstate[id].loaded = 30; a.wstate[id].mode = 1;
        }
        if (t < 0.8f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        shotQueue.push_back("sights_" + id + "_wall");
        Physics::Hit h; vec3 e = a.eyePos(); float wd = Physics::castRayEx(e, vec3(0, 0, -1), 5, h) ? h.t : 99;
        float sd = glm::dot(a.lastShotStart - e, vec3(0, 0, -1)), md = glm::dot(a.lastShotMuzzle - e, vec3(0, 0, -1));
        char b[200]; std::snprintf(b, sizeof b, "%s at a wall %.2f m away: muzzle %.2f m out (inside the wall), shot starts %.2f m out on the near side", id.c_str(), wd, md, sd);
        check(md > wd && sd < wd, b);
        a.wstate[id].mode = 0;
        Physics::setCharacterPos(vec3(0, a.terrain.height(0, 0), 0));
        return true; });
    }

    // ---- every other weapon keeps the shared defaults and its sight line on the dot
    push("others unchanged", [this](App& a, float t) {
      (void)t;
      int n = 0, bad = 0; std::string list;
      for (auto& dp : a.weaponDefs) {
        const WeaponDef& d = *dp;
        if (d.id == "fg42" || d.id == "mg42") continue;
        ++n;
        bool def = d.eyeRelief == 0.21f && d.adsVmFov == 0 && d.adsPos == vec3(0) && d.adsRot == vec3(0) && d.muzzleDir == vec3(0, 0, 1);
        json j; try { j = json::parse(readTextFile(d.file)); } catch (...) {}
        if (!def || j.contains("ads") || j.contains("muzzleDir")) { ++bad; list += " " + d.id; }
      }
      check(bad == 0 && n == (int)a.weaponDefs.size() - 2 && n >= 11, std::to_string(n) + " other weapons keep the shared calibration (relief 0.21 m, weapon FOV 60, no corrections)" + (bad ? ": changed" + list : ""));
      return true; });
    push("reset", [this](App& a, float) {
      a.sightMarkers = false; a.vm.procedural = true;
      a.exitExplore();
      check(a.mode == Mode::Creative, "back to Creative");
      return true; });
  }
  stage = 1601;
  if (scur >= sseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); check(app.R.fboErrors == 0, "every framebuffer complete, including after the resizes (" + std::to_string(app.R.fboErrors) + " incomplete)"); finish(app); return; }
  if (stt == 0) { logf("[selftest] step: %s", sseq[scur].name.c_str()); probeWanted = probeReady = false; }
  bool d2 = sseq[scur].fn(app, stt);
  stt += dt;
  if (stt > 40) { check(false, "step timed out: " + sseq[scur].name); d2 = true; }
  if (d2) { ++scur; stt = 0; }
}
