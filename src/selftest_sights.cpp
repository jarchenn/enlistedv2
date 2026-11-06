// --selftest-sights: per-weapon ADS calibration measured on the *rendered* weapon: FG 42 and MG 42 (full set), then
// Kar98k (V-notch), M1 Carbine / Thompson / Garand (apertures), BAR (front sight only, folded leaf) and the
// Panzerschreck (launcher composition), plus the smooth-ADS blend.
// The first-person pass writes each pixel's eye distance (vmDist, the depth-of-field attachment); the rear sight
// (~0.2 m) and the front sight (~0.75 m) separate cleanly by distance, so the aperture hole, the front post tip and
// the MG 42's V-notch are found in the weapon-projection image itself and compared with the fixed centre dot.
// Also: hip-fire flash / tracer origin on the drawn barrel opening, projection-consistent tracer start, aspect
// ratios, recoil, sway, posture, reload, a close wall, and that every other weapon keeps its default calibration.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <set>

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
  int bx0 = 1 << 30, bx1 = -1; for (int q : bestPts) { bx0 = std::min(bx0, q % W); bx1 = std::max(bx1, q % W); }
  for (int q : bestPts) { sx += q % W + x0 + 0.5; sy += q / W + y0 + 0.5; }
  m.rear = vec2(float(sx / bestPts.size()), float(sy / bestPts.size()));
  m.rearOk = true; m.leafW = float(bx1 - bx0 + 1);
  m.errRear = glm::length(m.rear - vec2(p.w * 0.5f, p.h * 0.5f));
  // front post through the opening: topmost far row in the columns at the aperture centre (protective ears beside
  // the blade can reach into the ring off-centre), tip x = mean of that row
  auto postCol = [&](int x) { return std::fabs(x + 0.5f - m.rear.x) <= 3.0f; };
  int top = 1 << 30; for (int q : bestPts) { int x = q % W + x0, y = q / W + y0; float d = at(p, x, y); if (postCol(x) && d >= farMin && d <= farMax) top = std::min(top, y); }
  if (top < (1 << 30)) {
    double tx = 0; int n = 0;
    for (int q : bestPts) { int x = q % W + x0, y = q / W + y0; float d = at(p, x, y); if (postCol(x) && y == top && d >= farMin && d <= farMax) { tx += x + 0.5; ++n; } }
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

// Kar98k: tangent leaf (near) with a shallow V-notch. The V bottom is found in the image; the rear reference is the
// shoulder line over it (gestrichen Korn: the barleycorn tip sits level with the shoulders, centred in the V), i.e. the
// V bottom raised by the notch depth drawn at the eye relief (dropPx). The front tip is the topmost far pixel in the V.
Meas measureVNotch(const Probe& p, float nearMax, float farMin, float farMax, float dropPx) {
  Meas m;
  int cx = p.w / 2, cy = p.h / 2, X0 = std::max(0, cx - 160), X1 = std::min(p.w - 1, cx + 160);
  std::vector<int> top(X1 - X0 + 1, -1);
  for (int x = X0; x <= X1; ++x)
    for (int y = std::max(0, cy - 200); y <= std::min(p.h - 1, cy + 200); ++y) { float d = at(p, x, y); if (d > 0.001f && d < nearMax) { top[x - X0] = y; break; } }
  // V bottom: the lowest top (largest y) within 60 px of the centre with higher tops on both sides
  int by = -1;
  for (int k = 60; k < (int)top.size() - 60; ++k) {
    if (top[k] < 0 || std::abs(k + X0 - cx) > 60) continue;
    int l = 1 << 30, r = 1 << 30;
    for (int q = k - 60; q < k; ++q) if (top[q] >= 0) l = std::min(l, top[q]);
    for (int q = k + 1; q <= k + 60; ++q) if (top[q] >= 0) r = std::min(r, top[q]);
    if (l < top[k] - 3 && r < top[k] - 3 && top[k] > by) by = top[k];
  }
  if (by < 0) return m;
  // centre of the V: midpoint of the flanks at half the drawn depth (sub-pixel robust vs the 1-px flat bottom)
  int yh = (int)std::lround(by - dropPx * 0.5f), a = -1, b = -1;
  for (int k = 0; k < (int)top.size(); ++k) if (std::abs(k + X0 - cx) <= 80 && top[k] >= 0 && top[k] >= yh) { if (a < 0) a = k; b = k; }
  if (a < 0) return m;
  float vx = (a + b) * 0.5f + X0 + 0.5f;
  m.rear = vec2(vx, by + 1 - dropPx); m.rearOk = true;   // by + 1: lower edge of the bottom pixel row
  m.errRear = glm::length(m.rear - vec2(cx, cy)); m.leafW = float(b - a + 1);
  int t = 1 << 30; double tx = 0; int n = 0;
  for (int x = a; x <= b; ++x) for (int y = std::max(0, by - 60); y <= by + 2; ++y) { float d = at(p, x + X0, y); if (d >= farMin && d <= farMax) { if (y < t) { t = y; tx = 0; n = 0; } if (y == t) { tx += x + X0 + 0.5; ++n; } break; } }
  if (n) { m.front = vec2(float(tx / n), (float)t); m.frontOk = true; m.errFront = glm::length(m.front - vec2(cx, cy)); }
  return m;
}

// BAR (front sight only): the hooded front post. Down each column at the dot the thin hood arc comes first (a short
// solid run), then the opening, then the post running down into its base; the tip is the top of the first run of at
// least minRun solid pixels, the highest such top over the central columns.
Meas measureFrontPost(const Probe& p, float farMin, float farMax, int minRun) {
  Meas m;
  int cx = p.w / 2, cy = p.h / 2, best = 1 << 30; std::vector<int> tips;
  auto solid = [&](int x, int y) { float d = at(p, x, y); return d >= farMin && d <= farMax; };
  for (int x = cx - 6; x < cx + 6; ++x) {
    int tip = -1;
    for (int y = cy - 80, run = 0; y < cy + 80; ++y) { if (solid(x, y)) { if (++run == minRun) { tip = y - minRun + 1; break; } } else run = 0; }
    tips.push_back(tip); if (tip >= 0) best = std::min(best, tip);
  }
  if (best == (1 << 30)) return m;
  double tx = 0; int n = 0; for (int k = 0; k < (int)tips.size(); ++k) if (tips[k] >= 0 && tips[k] <= best) { tx += cx - 6 + k + 0.5; ++n; }
  m.front = vec2(float(tx / n), (float)best); m.frontOk = true; m.errFront = glm::length(m.front - vec2(cx, cy));
  m.rearOk = true; m.errRear = 0; m.leafW = (float)n;   // post width at the tip (px)
  return m;
}

// near weapon pixels (eye distance < nearMax) above the dot within +-halfW columns: the BAR's raised ladder leaf
int nearAbove(const Probe& p, float nearMax, int halfW, int margin) {
  int cx = p.w / 2, cy = p.h / 2, n = 0;
  for (int y = 0; y < cy - margin; ++y) for (int x = std::max(0, cx - halfW); x <= std::min(p.w - 1, cx + halfW); ++x) { float d = at(p, x, y); if (d > 0.001f && d < nearMax) ++n; }
  return n;
}
// launcher composition: weapon coverage of the central box (fraction of its pixels) and the weapon's mean x
struct Cover { float centre = 0, meanX = 0, leftShare = 0; };
Cover coverage(const Probe& p, float box) {
  Cover c; int cx = p.w / 2, cy = p.h / 2, bw = int(p.w * box), bh = int(p.h * box), in = 0, all = 0, left = 0; double sx = 0;
  for (int y = 0; y < p.h; ++y) for (int x = 0; x < p.w; ++x) {
    if (at(p, x, y) <= 0.001f) continue;
    ++all; sx += x; if (x < cx) ++left;
    if (std::abs(x - cx) < bw && std::abs(y - cy) < bh) ++in;
  }
  c.centre = in / float(std::max(1, 4 * bw * bh)); c.meanX = all ? float(sx / all) : 0; c.leftShare = all ? left / float(all) : 0;
  return c;
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
    const char* only = std::getenv("NMK_SIGHTS_ONLY");
    auto wanted = [only](const std::string& id) { return !only || !*only || (std::string(",") + only + ",").find("," + id + ",") != std::string::npos; };
    for (Spec sp : {Spec{"fg42", true, kLegacyFG42}, Spec{"mg42", false, kLegacyMG42}}) {
      const std::string id = sp.id;
      if (!wanted(id)) continue;
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
        char b[200]; std::snprintf(b, sizeof b, "%s hip: muzzle (%.3f, %.3f, %.3f) on the bore; flash and tracer use the original effects", id.c_str(), d.muzzle.x, d.muzzle.y, d.muzzle.z);
        check(true, b);
        // the round (and its tracer) starts at the true muzzle
        check(glm::length(a.lastShotStart - a.lastShotMuzzle) < 0.001f, id + " hip: projectile starts at the true muzzle (" + std::to_string(glm::length(a.lastShotStart - a.lastShotMuzzle) * 1000).substr(0, 4) + " mm)");
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

    // ---- W1 (complete update, part 2): anchors measured on the supplied GLB meshes for six more weapons, each
    // checked on the rendered image with the measurement its sight needs; old values first for the report.
    enum Kind { AP, VN, HOOD, LAUNCH };
    struct W1 { std::string id; Kind kind; Calib legacy; float sizeLo, sizeHi; };
    const W1 w1[] = {
      {"kar98k", VN, {{0, 0.082f, 0.6f}, {0, 0.081f, 1.085f}, {0, 0.06f, 1.11f}, {0, 0.02f, 0.27f}, {0, 0.045f, 0.72f}, 0.21f}, 0, 0},
      {"m1_carbine", AP, {{0, 0.079f, 0.33f}, {0, 0.0805f, 0.87f}, {0, 0.06f, 0.904f}, {0, 0.0f, 0.27f}, {0, 0.04f, 0.6f}, 0.21f}, 2.2f, 3.4f},
      {"m1_thompson", AP, {{0.003f, 0.107f, 0.25f}, {0.003f, 0.1068f, 0.805f}, {0.003f, 0.082f, 0.813f}, {0, -0.02f, 0.36f}, {0, 0.055f, 0.57f}, 0.21f}, 1.10f, 1.15f},
      {"m1_garand", AP, {{-0.0012f, 0.111f, 0.306f}, {-0.0008f, 0.113f, 1.069f}, {0, 0.086f, 1.107f}, {0, 0.03f, 0.25f}, {0, 0.045f, 0.62f}, 0.21f}, 1.30f, 1.50f},
      {"m1918a2_bar", HOOD, {{0, 0.112f, 0.285f}, {0, 0.1075f, 1.09f}, {0, 0.079f, 1.214f}, {0, 0.0f, 0.27f}, {0, 0.035f, 0.64f}, 0.21f}, 0, 0},
      {"panzerschreck", LAUNCH, {{-0.1f, 0.06f, 1.0f}, {-0.1f, 0.06f, 1.55f}, {0, 0, 1.64f}, {0, -0.13f, 0.86f}, {0, -0.06f, 1.3f}, 0.21f}, 0, 0},
    };
    for (const W1& w : w1) {
      const std::string id = w.id;
      if (!wanted(id)) continue;
      const Kind kind = w.kind;
      auto focal = [](App& a, const Probe& p) { return p.h * 0.5f / std::tan(radians(a.vmFovNow()) * 0.5f); };
      auto measure = [kind, focal](App& a, const Probe& p) {
        const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
        if (kind == AP) return measureAperture(p, 0.32f, 0.5f, 1.3f);
        if (kind == VN) {   // notch depth drawn at the live eye-to-rear distance (recoil slides the rifle back)
          vec3 Rw, Fw; a.sightLineWorld(d, a.wstate[d.id], Rw, Fw);
          float dist = glm::dot(Rw - a.eyePos(), App::forward(a.eyeYaw + a.viewKickYaw, a.eyePitch + a.viewKickPitch));
          return measureVNotch(p, 0.32f, 0.5f, 1.4f, 0.0012f / std::max(0.02f, dist) * focal(a, p));
        }
        return measureFrontPost(p, 0.5f, 1.4f, 8);
      };
      auto fmt = [kind](const Meas& m) {
        char b[260];
        if (kind == AP) std::snprintf(b, sizeof b, "aperture centre %s (%.1f, %.1f) err %.2f px, %.0f px wide, post tip %s (%.1f, %.1f) err %.2f px", m.rearOk ? "found" : "NOT FOUND", m.rear.x, m.rear.y, m.rearOk ? m.errRear : -1.0f, m.leafW, m.frontOk ? "seen through it" : "NOT IN THE APERTURE", m.front.x, m.front.y, m.frontOk ? m.errFront : -1.0f);
        else if (kind == VN) std::snprintf(b, sizeof b, "V-notch shoulder line over the V centre %s (%.1f, %.1f) err %.2f px (V %.0f px wide), barleycorn tip %s (%.1f, %.1f) err %.2f px", m.rearOk ? "found" : "NOT FOUND", m.rear.x, m.rear.y, m.rearOk ? m.errRear : -1.0f, m.leafW, m.frontOk ? "in the V" : "NOT IN THE V", m.front.x, m.front.y, m.frontOk ? m.errFront : -1.0f);
        else std::snprintf(b, sizeof b, "hooded front post tip %s (%.1f, %.1f) err %.2f px, post %.0f px wide at the tip", m.frontOk ? "found" : "NOT FOUND", m.front.x, m.front.y, m.frontOk ? m.errFront : -1.0f, m.leafW);
        return std::string(b);
      };
      auto pass = [](const Meas& m) { return m.rearOk && m.errRear <= 2.0f && m.frontOk && m.errFront <= 2.0f; };
      auto ads = [id](App& a, float t) {
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; }
        a.scriptedInput.aim = true;
      };
      struct Extra { vec3 hip; float fov; bool line; };
      static Calib saved; static Extra savedX;
      auto legacyOn = [w](App& a) {
        WeaponDef& d = WD(a); saved = grab(d); savedX = {d.hipOffset, d.adsWorldFov, d.sightLineOnly};
        apply(d, w.legacy); d.hipOffset = vec3(0.12f, -0.07f, 0.06f); d.adsWorldFov = 58; d.sightLineOnly = false; a.vm.snap = true;
      };
      auto legacyOff = [](App& a) { WeaponDef& d = WD(a); apply(d, saved); d.hipOffset = savedX.hip; d.adsWorldFov = savedX.fov; d.sightLineOnly = savedX.line; a.vm.snap = true; };

      if (kind == LAUNCH) {
        push(id + " before hip", [this, id, legacyOn](App& a, float t) {
          if (t == 0) { a.selectWeapon(a.weaponIndex(id)); legacyOn(a); a.vm.procedural = false; a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.sightMarkers = false; }
          if (t > 0.5f && t < 0.52f) shotQueue.push_back("sights_" + id + "_before_hip");
          return probeThen(t, 0.6f, [&] {
            Cover c = coverage(probe, 0.06f);
            char b[200]; std::snprintf(b, sizeof b, "%s BEFORE hip (sight line x -0.1 behind the shield): weapon covers %.0f%% of the central box", id.c_str(), c.centre * 100);
            check(true, b); summary[id]["before"]["hipCentre"] = c.centre;
          }); });
        push(id + " before ads", [this, id, ads, legacyOff](App& a, float t) {
          ads(a, t); a.sightMarkers = false;
          if (t > 1.0f && t < 1.02f) shotQueue.push_back("sights_" + id + "_before_ads");
          return probeThen(t, 1.1f, [&] {
            Cover c = coverage(probe, 0.06f);
            char b[200]; std::snprintf(b, sizeof b, "%s BEFORE ADS: weapon (shield) covers %.0f%% of the central box", id.c_str(), c.centre * 100);
            check(true, b); summary[id]["before"]["adsCentre"] = c.centre;
            legacyOff(a);
          }); });
        push(id + " hip", [this, id](App& a, float t) {
          if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.vm.snap = true; a.scriptedInput = ExploreInput{}; a.sightMarkers = false; a.eyeYaw = 0; a.eyePitch = 0; }
          if (t > 0.5f && t < 0.52f) shotQueue.push_back("sights_" + id + "_hip");
          return probeThen(t, 0.6f, [&] {
            Cover c = coverage(probe, 0.06f);
            char b[220]; std::snprintf(b, sizeof b, "%s hip, seen from its right side: weapon covers %.1f%% of the central box (+-6%% of the frame; limit 2%%), %.0f%% of its pixels left of the dot (shield peripheral)", id.c_str(), c.centre * 100, c.leftShare * 100);
            check(c.centre <= 0.02f && c.leftShare >= 0.7f, b); summary[id]["after"]["hipCentre"] = c.centre;
          }); });
        push(id + " ads", [this, id, ads](App& a, float t) {
          ads(a, t); a.sightMarkers = false;
          if (t > 1.0f && t < 1.02f) shotQueue.push_back("sights_" + id + "_ads");
          return probeThen(t, 1.1f, [&] {
            Cover c = coverage(probe, 0.06f);
            const WeaponDef& d = WD(a);
            char b[260]; std::snprintf(b, sizeof b, "%s ADS keeps the composition: weapon covers %.1f%% of the central box (+-6%% of the frame; limit 2%%), %.0f%% left of the dot; mild zoom to world FOV %.1f (hip 72, rifles 58)", id.c_str(), c.centre * 100, c.leftShare * 100, a.worldFov);
            check(c.centre <= 0.02f && c.leftShare >= 0.7f && std::fabs(a.worldFov - d.adsWorldFov) < 0.01f && d.adsWorldFov > 58 && d.adsWorldFov < 72, b);
            summary[id]["after"]["adsCentre"] = c.centre; summary[id]["after"]["worldFov"] = a.worldFov;
            summary[id]["after"]["rear"] = {d.rear.x, d.rear.y, d.rear.z}; summary[id]["after"]["front"] = {d.front.x, d.front.y, d.front.z}; summary[id]["after"]["muzzle"] = {d.muzzle.x, d.muzzle.y, d.muzzle.z};
          }); });
        push(id + " ads fire", [this, id, ads](App& a, float t) {
          static int s0;
          ads(a, t); a.sightMarkers = false;
          if (t == 0) { s0 = a.shotsFired; a.wstate[id].loaded = 1; }
          if (t < 1.0f) return false;
          if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
          a.scriptedInput.fire = false;
          shotQueue.push_back("sights_" + id + "_adsfire");
          vec3 e = a.eyePos(), f = App::forward(a.eyeYaw, a.eyePitch); Physics::Hit h;
          vec3 P = Physics::castRayEx(e, f, 600, h) ? h.pos : e + f * 600.0f;
          float ang = degrees(std::acos(std::clamp(glm::dot(glm::normalize(a.lastShotDir), glm::normalize(P - a.lastShotStart)), -1.0f, 1.0f)));
          char b[220]; std::snprintf(b, sizeof b, "%s ADS: rocket leaves the tube muzzle (%.2f mm) toward the aim point (%.2f deg incl. zero / spread)", id.c_str(), glm::length(a.lastShotStart - a.lastShotMuzzle) * 1000, ang);
          check(glm::length(a.lastShotStart - a.lastShotMuzzle) < 0.001f && ang < 1.0f, b);
          return true; });
        continue;
      }

      push(id + " before ads", [this, id, ads, measure, fmt, legacyOn, legacyOff](App& a, float t) {
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); legacyOn(a); }
        ads(a, t);
        if (t > 1.0f && t < 1.02f) shotQueue.push_back("sights_" + id + "_before_ads");
        return probeThen(t, 1.1f, [&] {
          Meas m = measure(a, probe);
          check(true, id + " BEFORE (old anchors, relief 0.21): " + fmt(m));
          summary[id]["before"] = {{"rearErrPx", m.rearOk ? m.errRear : -1}, {"frontErrPx", m.frontOk ? m.errFront : -1}, {"widthPx", m.leafW}};
          legacyOff(a);
        }); });
      push(id + " hip", [this, id](App& a, float t) {
        if (t == 0) { a.selectWeapon(a.weaponIndex(id)); a.vm.procedural = false; a.vm.snap = true; a.scriptedInput = ExploreInput{}; a.sightMarkers = true; a.eyeYaw = 0; a.eyePitch = 0; }
        if (t > 0.5f && t < 0.52f) shotQueue.push_back("sights_" + id + "_hip");
        return t > 0.6f; });
      push(id + " ads", [this, id, kind, ads, measure, fmt, pass](App& a, float t) {
        ads(a, t);
        if (t > 1.0f && t < 1.02f) { a.sightMarkers = false; shotQueue.push_back("sights_" + id + "_ads_clean"); }
        if (t > 1.1f && t < 1.12f) { a.sightMarkers = true; shotQueue.push_back("sights_" + id + "_ads"); }
        return probeThen(t, 1.2f, [&] {
          Meas m = measure(a, probe);
          const WeaponDef& d = WD(a);
          check(pass(m), id + " AFTER at " + std::to_string(probe.w) + "x" + std::to_string(probe.h) + ", world FOV 58: " + fmt(m) + " (limit 2 px)");
          summary[id]["after"] = {{"rearErrPx", m.rearOk ? m.errRear : -1}, {"frontErrPx", m.frontOk ? m.errFront : -1}, {"widthPx", m.leafW}, {"res", {probe.w, probe.h}},
                                  {"rear", {d.rear.x, d.rear.y, d.rear.z}}, {"front", {d.front.x, d.front.y, d.front.z}}, {"muzzle", {d.muzzle.x, d.muzzle.y, d.muzzle.z}}, {"eyeRelief", d.eyeRelief}};
          if (kind == HOOD) {
            int n = nearAbove(probe, 0.32f, 300, 4);
            check(n == 0, id + ": no rear-sight structure above the dot (folded ladder leaf): " + std::to_string(n) + " near pixels above the centre row within 300 px");
            summary[id]["after"]["nearAbovePx"] = n;
          }
          std::vector<App::SightMark> mk; a.sightMarks(mk);
          vec2 c(probe.w * 0.5f, probe.h * 0.5f);
          char b[200]; std::snprintf(b, sizeof b, "%s: anchors through the weapon projection: rear %.2f px, front %.2f px from the dot; aim point %.2f px", id.c_str(), glm::length(mk[0].px - c), glm::length(mk[1].px - c), glm::length(mk[4].px - c));
          check(glm::length(mk[0].px - c) < 0.5f && glm::length(mk[1].px - c) < 0.5f && glm::length(mk[4].px - c) < 0.5f, b);
        }); });
      if (w.sizeHi > 0)
        push(id + " size", [this, id, w, ads, measure](App& a, float t) {
          static float wRef = 0;
          ads(a, t);
          if (t == 0) wRef = 0;
          if (wRef == 0) return probeThen(t, 1.0f, [&] { wRef = std::max(1.0f, measure(a, probe).leafW); WD(a).eyeRelief = 0.21f; WD(a).adsVmFov = 0; a.vm.snap = true; }) && false;
          return probeThen(t, 2.0f, [&] {
            float w0 = measure(a, probe).leafW;
            json j; try { j = json::parse(readTextFile(WD(a).file)); } catch (...) {}
            WD(a).eyeRelief = j["ads"].value("eyeRelief", 0.21f); WD(a).adsVmFov = j["ads"].value("vmFov", 0.0f); a.vm.snap = true;
            float r = wRef / std::max(1.0f, w0);
            char b[260]; std::snprintf(b, sizeof b, "%s sight picture %.0f px wide at eye relief %.3f m, weapon FOV %.0f vs %.0f px at the shared 0.21 m / 60: %.0f%% larger (want %.0f-%.0f%%), mesh not scaled", id.c_str(), wRef, WD(a).eyeRelief, WD(a).adsVmFov > 0 ? WD(a).adsVmFov : 60.0f, w0, (r - 1) * 100, (w.sizeLo - 1) * 100, (w.sizeHi - 1) * 100);
            check(r >= w.sizeLo && r <= w.sizeHi, b);
            summary[id]["sizeRatio"] = r;
          }); });
      push(id + " aspect", [this, id, ads, measure, fmt, pass](App& a, float t) {
        if (t == 0) glfwSetWindowSize(a.win, 2400, 1080);
        ads(a, t);
        return probeThen(t, 0.8f, [&] {
          Meas m = measure(a, probe);
          check(pass(m), id + " at " + std::to_string(probe.w) + "x" + std::to_string(probe.h) + ": " + fmt(m));
          glfwSetWindowSize(a.win, 1920, 1080);
        }); });
      push(id + " restore size", [](App& a, float t) { (void)a; return t > 0.5f; });
      push(id + " recoil", [this, id, ads, measure, fmt, pass](App& a, float t) {
        static int s0, frames;
        ads(a, t);
        if (t == 0) { s0 = a.shotsFired; frames = 0; a.wstate[id].loaded = 5; }
        if (t < 1.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        if (++frames < 3) return false;
        return probeThen(t, 0, [&] { Meas m = measure(a, probe); check(pass(m), id + " during recoil: " + fmt(m)); }); });
      push(id + " reload", [this, id, ads, measure, fmt, pass](App& a, float t) {
        static int phase; static float tDone;
        ads(a, t);
        if (t == 0) { phase = 0; tDone = 0; a.wstate[id].loaded = 1; }
        auto& s = a.wstate[id];
        if (phase == 0) { a.scriptedInput.aim = false; a.scriptedInput.reload = t > 0.2f; if (s.reload > 0) { phase = 1; a.scriptedInput.reload = false; } return t > 4; }
        if (phase == 1) { a.scriptedInput.aim = false; if (s.reload <= 0) { phase = 2; tDone = t; } return t > 20; }
        if (t < tDone + 1.0f) return false;
        return probeThen(t, 0, [&] { Meas m = measure(a, probe); check(pass(m) && s.loaded > 1, id + " after a reload (" + std::to_string(s.loaded) + " loaded): " + fmt(m)); }); });
      push(id + " ads fire", [this, id, ads](App& a, float t) {
        static int s0;
        ads(a, t);
        if (t == 0) { s0 = a.shotsFired; a.wstate[id].loaded = 5; }
        if (t < 1.0f) return false;
        if (a.shotsFired == s0) { a.scriptedInput.fire = true; return t > 4; }
        a.scriptedInput.fire = false;
        shotQueue.push_back("sights_" + id + "_adsfire");
        check(glm::length(a.lastShotStart - a.lastSightF) < 0.001f, id + " ADS: the round leaves the calibrated front sight point along the sight line");
        return true; });
    }

    // ---- smooth ADS: one blend, editable time, continuous world FOV, mid-way reversal, stable hold under recoil
    if (wanted("ads")) push("ads smooth", [this](App& a, float t) {
      static std::vector<vec2> fe, fr; static float tFull, tRev, holdMin, holdMax; static int phase, s0;
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); a.vm.procedural = false; a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = 0; fe.clear(); fr.clear(); phase = 0; tFull = tRev = -1; holdMin = 1e9f; holdMax = -1e9f; return false; }
      if (phase == 0) { if (t > 0.6f) { phase = 1; tRev = t; } return false; }   // settle at the hip
      float fov = 72; a.weaponCamera(fov);
      if (phase == 1) {   // entry
        a.scriptedInput.aim = true; fe.push_back({t - tRev, fov});
        if (a.adsT >= 1.0f && tFull < 0) { tFull = t - tRev; phase = 2; tRev = t; s0 = a.shotsFired; a.wstate["m1_garand"].loaded = 8; }
        return t > 5;
      }
      if (phase == 2) {   // hold 1 s and fire: FOV stays put
        a.scriptedInput.aim = true; a.scriptedInput.fire = (int)((t - tRev) * 8) % 2 == 0;
        holdMin = std::min(holdMin, fov); holdMax = std::max(holdMax, fov);
        if (t - tRev > 1.0f) { phase = 3; tRev = t; a.scriptedInput.fire = false; }
        return false;
      }
      if (phase == 3) {   // exit, reverse half-way back in, then out again
        fr.push_back({t - tRev, fov});
        static bool reversed; static float tIn; if (fr.size() == 1) reversed = false;
        if (!reversed && a.adsT < 0.5f) { reversed = true; tIn = t; }
        a.scriptedInput.aim = reversed && t - tIn < 0.1f;   // back in for 0.1 s from the half-way blend, then out
        if (t - tRev > 1.2f) phase = 4;
        return false;
      }
      float maxStep = 0; bool mono = true;
      for (size_t k = 1; k < fe.size(); ++k) { maxStep = std::max(maxStep, std::fabs(fe[k].y - fe[k - 1].y)); if (fe[k].y > fe[k - 1].y + 1e-4f) mono = false; }
      float maxStepR = 0; int turns = 0;
      for (size_t k = 1; k < fr.size(); ++k) { maxStepR = std::max(maxStepR, std::fabs(fr[k].y - fr[k - 1].y)); if (k > 1 && (fr[k].y - fr[k - 1].y) * (fr[k - 1].y - fr[k - 2].y) < -1e-8f) ++turns; }
      char b[300];
      std::snprintf(b, sizeof b, "ADS entry %.3f s (setting adsTime_s %.2f; want 0.20-0.30), world FOV 72 -> 58 monotonic %s, largest per-frame step %.2f deg (no jump; was a 14 deg step at adsT 0.85)", tFull, a.move.adsTime, mono ? "yes" : "NO", maxStep);
      check(tFull >= 0.2f && tFull <= 0.32f && mono && maxStep < 2.5f, b);
      std::snprintf(b, sizeof b, "held ADS while firing: world FOV %.2f..%.2f (recoil does not reset the zoom)", holdMin, holdMax);
      check(holdMax - holdMin < 1e-3f && std::fabs(holdMin - 58) < 1e-3f, b);
      std::snprintf(b, sizeof b, "exit reversed half-way: %d direction changes, largest per-frame step %.2f deg, ends at %.1f deg (continuous from the current blend)", turns, maxStepR, fr.empty() ? 0.0f : fr.back().y);
      check(turns >= 1 && turns <= 3 && maxStepR < 2.5f && !fr.empty() && std::fabs(fr.back().y - 72) < 0.01f, b);
      summary["ads"] = {{"entry_s", tFull}, {"maxStepDeg", maxStep}, {"reverseMaxStepDeg", maxStepR}, {"adsTime_s", a.move.adsTime}};
      a.scriptedInput = ExploreInput{};
      return true; });

    // ---- every other weapon keeps the shared defaults and its sight line on the dot
    if (!only || !*only) push("others unchanged", [this](App& a, float t) {
      (void)t;
      int n = 0, bad = 0; std::string list;
      for (auto& dp : a.weaponDefs) {
        const WeaponDef& d = *dp;
        static const std::set<std::string> cal = {"fg42", "mg42", "kar98k", "m1_carbine", "m1_thompson", "m1_garand", "m1918a2_bar", "panzerschreck"};
        if (cal.count(d.id) || d.equipment()) continue;   // calibrated above / hand equipment (no sights; hand pose in "ads.hipOffset")
        ++n;
        bool def = d.eyeRelief == 0.21f && d.adsVmFov == 0 && d.adsPos == vec3(0) && d.adsRot == vec3(0) && d.muzzleDir == vec3(0, 0, 1);
        json j; try { j = json::parse(readTextFile(d.file)); } catch (...) {}
        if (!def || j.contains("ads") || j.contains("muzzleDir")) { ++bad; list += " " + d.id; }
      }
      int equip = 0; for (auto& dp : a.weaponDefs) equip += dp->equipment();
      check(bad == 0 && n == (int)a.weaponDefs.size() - 8 - equip && n >= 5, std::to_string(n) + " other weapons keep the shared calibration (relief 0.21 m, weapon FOV 60, no corrections)" + (bad ? ": changed" + list : ""));
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
