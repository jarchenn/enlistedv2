// --selftest-render (V1, docs/rendering.md#facades-walls-water): facade shimmer, rough walls and the water pass.
//  * facades: the Norman farmhouse (shutters, rubble render), the ruined church (ragged walls) and the old stone cottage
//    seen in first person at 20, 50 and 100 m. Each distance holds the camera still for 24 frames and measures the
//    temporal shimmer of the shipped image (TAA on): the mean per-pixel luminance standard deviation inside each facade
//    (exposure drift divided out), plus screenshots and 2x crops; then a slow strafe at 50 m (moving-camera strip).
//    selftest_out/render_shimmer.json holds the numbers, render_*.png the frames.
//  * depth convention: the world pass is reverse-Z (Renderer::revZ) and the depth readers agree with it.
//  * water (src/water.cpp): every asset with a water block loads (pond round / irregular, shell-crater pool, puddles,
//    stream straight / bend / end, sea shallows), stream pieces carry stream snap sockets, placement checks still refuse
//    a pond on a footprint and accept it on open ground, the pass draws, and screenshots of each.
#include "selftest.h"
#include <chrono>
#include "mptest.h"
#if __has_include("water.h")   // the same file builds against the pre-V1 tree for the before numbers
#include "water.h"
#define NMK_WATER 1
#endif
#include "settings.h"
#include <stb_image_write.h>
#include <cstdarg>
#include <functional>
#include <fstream>
void buildIvyCards(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, std::vector<float>& v);   // ivy.cpp

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[700];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[render] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
json report = json::object();

uint64_t place(App& a, const std::string& id, vec3 p, float yaw = 0) {
  const AssetDef* d = a.lib.find(id);
  if (!d) { chk(false, "asset %s exists", id.c_str()); return 0; }
  p.y = a.terrain.height(p.x, p.z);
  uint64_t r = a.placeInstance(d, p, yaw);
  if (!r) chk(false, "%s placed at (%.1f, %.1f)", id.c_str(), p.x, p.z);
  return r;
}

// ---------------------------------------------------------------- frame capture
// The post-render hook (dt < 0) reads the LDR frame back when a capture is armed.
struct Region { std::string name; vec3 lo, hi; int x0 = 0, y0 = 0, x1 = 0, y1 = 0; std::vector<double> s, s2; double meanSum = 0; int frames = 0; };
std::vector<Region> regions;
int captureLeft = 0;
std::vector<uint8_t> lastPx; int lastW = 0, lastH = 0;
std::vector<std::vector<double>> moving; int movingLeft = 0;   // strafe frames: luma of region 0, scaled to a mean of 100
void captureMoving(const std::vector<uint8_t>& px, int w) {
  const Region& r = regions[0]; int rw = r.x1 - r.x0, rh = r.y1 - r.y0; if (rw <= 0 || rh <= 0) return;
  std::vector<double> L((size_t)rw * rh); double m = 0;
  for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
    const uint8_t* p = &px[((size_t)(r.y0 + y) * w + (r.x0 + x)) * 4];
    double l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; L[(size_t)y * rw + x] = l; m += l;
  }
  m /= (double)L.size(); for (auto& l : L) l *= m > 1 ? 100.0 / m : 1.0;
  moving.push_back(std::move(L));
}
// facade boxes (world) -> screen rectangles with the current camera
void projectRegions(App& a, int w, int h) {
  mat4 vp = a.projM * a.viewM;
  for (auto& r : regions) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 8; ++k) {
      vec3 p((k & 1) ? r.hi.x : r.lo.x, (k & 2) ? r.hi.y : r.lo.y, (k & 4) ? r.hi.z : r.lo.z);
      vec4 c = vp * vec4(p, 1); if (c.w <= 0.01f) continue;
      float sx = (c.x / c.w * 0.5f + 0.5f) * w, sy = (c.y / c.w * 0.5f + 0.5f) * h;
      x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
    }
    r.x0 = std::clamp((int)x0, 0, w); r.x1 = std::clamp((int)x1, 0, w); r.y0 = std::clamp((int)y0, 0, h); r.y1 = std::clamp((int)y1, 0, h);
    size_t n = (size_t)std::max(0, r.x1 - r.x0) * std::max(0, r.y1 - r.y0);
    r.s.assign(n, 0.0); r.s2.assign(n, 0.0); r.meanSum = 0; r.frames = 0;
  }
}
void accumulate(const std::vector<uint8_t>& px, int w) {
  for (auto& r : regions) {
    int rw = r.x1 - r.x0, rh = r.y1 - r.y0; if (rw <= 0 || rh <= 0) continue;
    double m = 0;
    std::vector<double> L((size_t)rw * rh);
    for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
      const uint8_t* p = &px[((size_t)(r.y0 + y) * w + (r.x0 + x)) * 4];
      double l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
      L[(size_t)y * rw + x] = l; m += l;
    }
    m /= (double)L.size();
    double k = m > 1 ? 100.0 / m : 1.0;   // exposure drift divided out: every frame scaled to a region mean of 100
    for (size_t i = 0; i < L.size(); ++i) { double l = L[i] * k; r.s[i] += l; r.s2[i] += l * l; }
    r.meanSum += m; ++r.frames;
  }
}
double shimmer(const Region& r) {
  if (r.frames < 2 || r.s.empty()) return -1;
  double sum = 0;
  for (size_t i = 0; i < r.s.size(); ++i) { double mu = r.s[i] / r.frames; sum += std::sqrt(std::max(0.0, r.s2[i] / r.frames - mu * mu)); }
  return sum / (double)r.s.size();
}
// spatial detail of the time-averaged facade: luma standard deviation over the region (the contrast shimmer acts on)
double detail(const Region& r) {
  if (r.frames < 1 || r.s.empty()) return -1;
  double m = 0, m2 = 0;
  for (double v : r.s) { double mu = v / r.frames; m += mu; m2 += mu * mu; }
  m /= (double)r.s.size(); return std::sqrt(std::max(0.0, m2 / (double)r.s.size() - m * m));
}
void writeCrop(const fs::path& p, const std::vector<uint8_t>& px, int w, int h, int x0, int y0, int x1, int y1, int zoom) {
  x0 = std::clamp(x0, 0, w); x1 = std::clamp(x1, 0, w); y0 = std::clamp(y0, 0, h); y1 = std::clamp(y1, 0, h);
  int cw = x1 - x0, ch = y1 - y0; if (cw <= 0 || ch <= 0) return;
  std::vector<uint8_t> out((size_t)cw * zoom * ch * zoom * 4);
  for (int y = 0; y < ch * zoom; ++y) for (int x = 0; x < cw * zoom; ++x) {
    int sx = x0 + x / zoom, sy = y1 - 1 - y / zoom;   // bottom-up read-back -> top-down image
    std::memcpy(&out[((size_t)y * cw * zoom + x) * 4], &px[((size_t)sy * w + sx) * 4], 4);
    out[((size_t)y * cw * zoom + x) * 4 + 3] = 255;
  }
  stbi_write_png(p.string().c_str(), cw * zoom, ch * zoom, 4, out.data(), cw * zoom * 4);
}

const float kFacadeZ = 3.5f;   // the farmhouse front wall (asset +Z)
vec3 F0(0);                    // farmhouse origin
vec3 K0(0);                    // B1 kit folly origin (west of the cottage)
uint64_t kitId = 0;
const vec3 W0(55, 0, 10);      // water features: the round pond (east of the facades)
int wDraws = 0, wN = 0; double wGpu = 0;
void firstPerson(App& a, vec3 p, float yaw, float pitch) {
  if (a.mode == Mode::Explore) a.exitExplore();
  p.y = a.terrain.height(p.x, p.z);
  a.doc.start = {true, p, yaw};
  a.enterExplore();
  a.eyeYaw = yaw; a.eyePitch = pitch;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
}

void build(App& app) {
  (void)app;
  push("setup: Blank Slate with three facades", [](App& a, float t) {
    if (t == 0) {
      const TemplateInfo* tp = a.findTemplate("blank_slate");
      chk(tp && a.newMap(*tp, "Render test"), "Blank Slate map");
      a.grass.quality = GrassQuality::Medium;
      F0 = vec3(0, 0, -20);
      place(a, "vil_norman_farmhouse", F0);
      place(a, "vil_church_ruined", F0 + vec3(19.5f, 0, -0.5f));
      place(a, "house_stone_cottage", F0 + vec3(-17.0f, 0, 0));
      K0 = F0 + vec3(-45.0f, 0, 0);
      kitId = place(a, "kit_demo_folly", K0);
      a.syncRuntime();
      chk(a.R.shaderErrors == 0, "shader compile errors at start: %d", a.R.shaderErrors);
    }
    return t > 0.5f;
  });
  // MA1 material capture (NMK_MA1_SHOTS=1 runs only this): the chateau's brick chimney breast in shade at 5 m, its sunlit
  // front at 5 m, the roof at 10 and 30 m, the farmhouse front at 20 and 50 m; Medium's 0.67 scale with TAAU ->
  // selftest_out/ma1_<view>.png (run once with --rq-off imgmat for the before set)
  if (std::getenv("NMK_MA1_SHOTS")) {
    static const vec3 C(-100.0f, 0, 40.0f);   // clear of the facades, the kit folly and the ponds
    push("ma1: the chateau", [](App& a, float t) { if (t == 0) { place(a, "vil_chateau_grand", C); a.syncRuntime(); } return t > 0.5f; });
    struct V { const char* name; vec3 at; float yaw, pitch; };
    static const V vs[6] = {{"chimney_shade_5m", C + vec3(-5.5f, 0, -16.0f), 180.0f, 22.0f}, {"chateau_sun_5m", C + vec3(-3.0f, 0, 16.5f), 0.0f, 18.0f},
                            {"roof_10m", C + vec3(-2.0f, 0, -21.0f), 180.0f, 38.0f}, {"roof_30m", C + vec3(-2.0f, 0, -41.0f), 180.0f, 16.0f},
                            {"facade_20m", F0 + vec3(0, 0, kFacadeZ + 20.0f), 0.0f, 4.0f}, {"facade_50m", F0 + vec3(0, 0, kFacadeZ + 50.0f), 0.0f, 2.0f}};
    for (int k = 0; k < 6; ++k) push(std::string("ma1_") + vs[k].name, [k](App& a, float t) {
      if (t == 0) { a.R.setResolutionScale(0.67f); a.R.resetTemporal(); firstPerson(a, vs[k].at, vs[k].yaw, vs[k].pitch); }
      a.eyeYaw = vs[k].yaw; a.eyePitch = vs[k].pitch;
      std::string nm = std::string("ma1_") + vs[k].name;
      if (t > 1.2f && t < 1.2f + 1.5f / 60.0f) pendingShot = outDir() / (nm + ".png");
      if (t > 1.5f && pendingShot.empty()) { chk(a.R.shaderErrors == 0, "%s -> selftest_out/%s.png", nm.c_str(), nm.c_str()); return true; }
      return false;
    });
    push("ma1: back to native", [](App& a, float t) { a.R.setResolutionScale(1.0f); return t > 0.2f; });
    return;
  }
  // MA1b relief capture (NMK_MA1B_SHOTS=1 runs only this): the chateau's brick chimney breast and roof at 3 / 15 / 35 m,
  // then stone, brick, timber, rock, sandbags and bark at 1 and 5 m, lit from the side (the Day sun rakes the +z faces
  // from the left). Editor camera (no viewmodel), Medium's 0.67 scale -> selftest_out/ma1b_<view>.png
  if (std::getenv("NMK_MA1B_SHOTS")) {
    static const vec3 C(-100.0f, 0, 40.0f), R0(40.0f, 0, 60.0f);
    static const char* ids[5] = {"drs_garden_wall", "vil_colombage_auge", "drs_sandbag_wall", "nat_boulder_granite_l", "tree_oak"};
    static uint64_t got[5];
    push("ma1b: the scene", [](App& a, float t) {
      if (t == 0) { place(a, "vil_chateau_grand", C); for (int k = 0; k < 5; ++k) got[k] = place(a, ids[k], R0 + vec3(k * 22.0f, 0, 0)); a.syncRuntime(); }
      return t > 0.5f; });
    struct V { std::string name; int obj; float d, camY, aimY, dx; };   // obj -1: chateau (dx / d absolute, from its back / front face)
    static std::vector<V> vs = {{"chimney_3m", -1, -3.0f, 1.7f, 6.0f, -5.5f}, {"roof_15m", -1, -15.0f, 1.7f, 12.0f, -5.5f}, {"roof_35m", -1, -35.0f, 1.7f, 12.0f, -5.5f},
                                {"brick_1m", -1, 1.0f, 1.6f, 1.4f, -3.0f}, {"brick_5m", -1, 5.0f, 1.6f, 1.6f, -3.0f}};
    for (int k = 0; k < 5; ++k) for (float d : {1.0f, 5.0f}) {
      static const char* nm[5] = {"stone", "timber", "sandbag", "rock", "bark"};
      static const float aim[5] = {0.6f, 1.4f, 0.5f, 0.8f, 1.3f};
      vs.push_back({std::string(nm[k]) + (d < 2 ? "_1m" : "_5m"), k, d, aim[k] + (d < 2 ? 0.25f : 0.6f), aim[k], 0.0f});
    }
    for (size_t k = 0; k < vs.size(); ++k) push("ma1b_" + vs[k].name, [k](App& a, float t) {
      const V& v = vs[k];
      if (t == 0) {
        a.R.setResolutionScale(0.67f); a.R.resetTemporal(); a.hideEditorOverlays = true;
        vec3 at;
        if (v.obj < 0) at = C + vec3(v.dx, 0, v.d < 0 ? -11.0f + v.d : 11.5f + v.d);
        else {
          const Instance* in = nullptr; for (auto& i : a.doc.instances) if (i.id == got[v.obj]) in = &i;
          const AssetDef* ad = in ? a.lib.find(in->assetId) : nullptr;
          float face = v.obj == 4 ? 0.45f : ad ? ad->bmax.z : 0.5f;   // the oak: its trunk, not the canopy
          at = (in ? in->pos : R0) + vec3(0, 0, face + v.d);
        }
        float gy = a.terrain.height(at.x, at.z), dist = std::fabs(v.d);
        a.camPos = vec3(at.x, gy + v.camY, at.z); a.camYaw = v.d < 0 ? 180.0f : 0.0f;
        a.camPitch = glm::degrees(std::atan2(v.aimY - v.camY, dist));
      }
      std::string nm = "ma1b_" + v.name;
      if (t > 1.2f && t < 1.2f + 1.5f / 60.0f) pendingShot = outDir() / (nm + ".png");
      if (t > 1.5f && pendingShot.empty()) { chk(a.R.shaderErrors == 0, "%s -> selftest_out/%s.png", nm.c_str(), nm.c_str()); return true; }
      return false;
    });
    push("ma1b: back to native", [](App& a, float t) { a.R.setResolutionScale(1.0f); a.hideEditorOverlays = false; return t > 0.2f; });
    return;
  }
  // P1 TAAU capture set (NMK_TAAU_SHOTS=1 runs only this, rendering.md 14): still, turning (100 deg/s), foliage and Kar98k
  // sights at Medium's 0.67 scale, native (taau off) then TAAU, from identical cameras -> selftest_out/taau_<view>_<mode>.png
  if (std::getenv("NMK_TAAU_SHOTS")) {
    push("taau: an oak for the foliage view", [](App& a, float t) { if (t == 0) { place(a, "tree_oak", F0 + vec3(-8.0f, 0, 30.0f)); a.syncRuntime(); } return t > 0.5f; });
    // views 4 / 5 (pitching 50 deg/s, strafing right) check the motion's y and the jitter: temporal modes only
    static const char* vn[6] = {"still", "turning", "foliage", "sights", "pitching", "strafe"};
    // with an upscale hook (Mac MetalFX): 2 = GL TAAU, 3 = hook, 4 = hook with the jitter sign flipped, 5 = motion y flipped;
    // 6 = spatial (the pre-P1 path). NMK_TAAU_ONLY=<substring> keeps only the shots whose name contains it
    static const char* mn[7] = {"_native", "_taau", "_gltaau", "_hook", "_hook_jitflip", "_hook_motflip", "_spatial"};
    const int m1 = Renderer::temporalUpscaler ? 7 : 2;
    const char* only = std::getenv("NMK_TAAU_ONLY");
    for (int m = 0; m < m1; ++m) for (int k = 0; k < 6; ++k) {
      if (m == 1 && m1 == 7) continue;   // with a hook, "taau" is the hook (3)
      if (k >= 4 && (m == 0 || m == 6)) continue;            // pitch / strafe: temporal modes
      if ((m == 4 || m == 5) && (k == 2 || k == 3)) continue;   // sign checks: still, turning, pitching, strafe
      std::string nm = std::string("taau_") + vn[k] + mn[m];
      if (only && *only && nm.find(only) == std::string::npos) continue;
      push(nm, [m, k, nm](App& a, float t) {
        vec3 at = k == 2 ? F0 + vec3(-8.0f, 0, 38.0f) : F0 + vec3(0, 0, 24.0f);
        float yaw = k == 1 ? -50.0f + 100.0f * t : 0.0f, pitch = k == 2 ? 10.0f : k == 4 ? -25.0f + 50.0f * t : 2.0f;
        if (t == 0) {
          a.R.perf.taau = m != 0; a.R.setResolutionScale(0.67f); a.R.resetTemporal(); a.R.upscaleMode = m == 6 ? 1 : 0;
          a.R.hookOn = m >= 3; a.R.hookSign = vec2(m == 4 ? -1.0f : 1.0f, m == 5 ? -1.0f : 1.0f);
          firstPerson(a, at, yaw, pitch);
          if (k == 3) a.selectWeapon(a.weaponIndex("kar98k"));
        }
        a.eyeYaw = yaw; a.eyePitch = pitch;
        if (k == 3) a.scriptedInput.aim = true;
        a.scriptedInput.move = k == 5 ? vec2(1.0f, 0.0f) : vec2(0.0f);   // strafe right
        if (t > 1.0f && t < 1.0f + 1.5f / 60.0f) pendingShot = outDir() / (nm + ".png");
        static double acc = 0, last = 0; static int n = 0;   // frame time (wall clock) over the settled half
        double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (t < 0.01f) { acc = 0; n = 0; } else if (t > 0.5f) { acc += now - last; ++n; } last = now;
        if (t > 1.3f && pendingShot.empty()) { chk(a.R.shaderErrors == 0, "%s -> selftest_out/%s.png (frame %.2f ms, gpu %.2f ms)", nm.c_str(), nm.c_str(), n ? acc / n : 0.0, a.R.stats.gpuMs); return true; }
        return false;
      });
    }
    push("taau: back to native", [](App& a, float t) { a.R.perf.taau = true; a.R.setResolutionScale(1.0f); a.scriptedInput.aim = false; a.scriptedInput.move = vec2(0.0f); a.R.hookOn = true; a.R.hookSign = vec2(1.0f); a.R.upscaleMode = settings().s.upscale;
      if (t > 0.2f) chk(a.R.fboErrors == 0 && a.R.shaderErrors == 0, "TAAU capture: framebuffer errors %d, shader errors %d", a.R.fboErrors, a.R.shaderErrors);
      return t > 0.2f; });
    return;
  }
  // ---------------------------------------------------------------- facades at 20 / 50 / 100 m
  // the last two runs are a TAA A/B at 50 m (report only): output sharpening 0.40 -> 0.20, jitter 0.5 -> 0.35 px span
  // (and, in the V1 tree, the rough-wall relief switched off at 20 and 50 m, and the old sharpening at 20 m)
  struct FV { int D; float sharpen, jitter; const char* tag; int relief; };
  static const FV fvs[] = {{20, -1, -1, "", 1}, {50, -1, -1, "", 1}, {100, -1, -1, "", 1}, {50, 0.2f, -1, "_taa_sharpen0.20", 1}, {50, -1, 0.35f, "_taa_jitter0.35", 1},
#ifdef NMK_REVZ
                           {20, -1, -1, "_no_wall_relief", 0}, {50, -1, -1, "_no_wall_relief", 0}, {20, 0.4f, -1, "_taa_sharpen0.40", 1},
#endif
  };
  for (const FV& fv : fvs) {
    int D = fv.D;
    push("facade " + std::to_string(D) + " m" + fv.tag, [D, fv](App& a, float t) {
      static float keepS = 0, keepJ = 0;
      if (t == 0) {
        keepS = a.R.q.taaSharpen; keepJ = a.R.q.taaJitter;
        if (fv.sharpen >= 0) a.R.q.taaSharpen = fv.sharpen;
        if (fv.jitter >= 0) a.R.q.taaJitter = fv.jitter;
#ifdef NMK_REVZ
        a.R.wallRelief = fv.relief;
#endif
        vec3 eye = F0 + vec3(0, 0, kFacadeZ + (float)D);
        firstPerson(a, eye, 0.0f, D == 20 ? 4.0f : D == 50 ? 1.5f : 0.6f);
        regions.clear();
        float y0 = a.terrain.height(F0.x, F0.z);
        regions.push_back({"farmhouse", F0 + vec3(-7.5f, y0 + 0.2f, kFacadeZ), F0 + vec3(7.5f, y0 + 3.4f, kFacadeZ)});
        regions.push_back({"ruined_church", F0 + vec3(19.5f - 7.0f, y0 + 0.2f, 3.5f), F0 + vec3(19.5f + 7.0f, y0 + 5.0f, 3.5f)});
        regions.push_back({"cottage", F0 + vec3(-17.0f - 4.0f, y0 + 0.2f, 3.0f), F0 + vec3(-17.0f + 4.0f, y0 + 3.0f, 3.0f)});
      }
      a.eyeYaw = 0.0f;
      if (t > 2.5f && captureLeft == 0 && regions[0].frames == 0) {
        captureLeft = 24; projectRegions(a, a.R.width, a.R.height);
#ifdef NMK_REVZ
        if (D == 20) chk(a.R.revZ, "world pass depth convention: reverse-Z (clip 0..1, near 1, cleared to 0, GREATER)");
#endif
      }
      if (regions[0].frames >= 24) {
        json j = json::object();
        for (auto& r : regions) {
          double s = shimmer(r);
          double dt = detail(r);
          j[r.name] = {{"shimmer", s}, {"detail", dt}, {"relative", dt > 0 ? s / dt : -1}, {"px", (r.x1 - r.x0) * (r.y1 - r.y0)}, {"mean_luma", r.meanSum / std::max(1, r.frames)}};
          note("%3d m%s %-14s shimmer %.3f (luma std, region mean scaled to 100), detail %.2f, relative %.4f over %d frames, %d x %d px", D, fv.tag, r.name.c_str(), s, dt, dt > 0 ? s / dt : -1.0, r.frames, r.x1 - r.x0, r.y1 - r.y0);
        }
        report["static_" + std::to_string(D) + "m" + fv.tag] = j;
        a.R.q.taaSharpen = keepS; a.R.q.taaJitter = keepJ;
#ifdef NMK_REVZ
        a.R.wallRelief = 1;
#endif
        if (fv.tag[0]) { chk(true, "TAA A/B at %d m%s: farmhouse shimmer %.3f (report only)", D, fv.tag, shimmer(regions[0])); regions[0].frames = -1; return false; }
        std::string nm = "render_facade_" + std::to_string(D) + "m";
        writeCrop(outDir() / (nm + "_crop.png"), lastPx, lastW, lastH, regions[0].x0 - 10, regions[0].y0 - 10, regions[0].x1 + 10, regions[0].y1 + 10, D >= 50 ? 3 : 1);
        pendingShot = outDir() / (nm + ".png");
        chk(regions[0].frames >= 24 && shimmer(regions[0]) >= 0, "facades at %d m: %d frames captured -> selftest_out/%s.png", D, regions[0].frames, nm.c_str());
        regions[0].frames = -1;   // done
        return false;
      }
      return regions[0].frames < 0 && pendingShot.empty();
    });
  }
  // ---------------------------------------------------------------- B1 primitives: the kit folly at 20 / 50 m
  // Arches, round columns, turrets, spires, wedges and bevelled trims (kit_demo_folly, docs/asset-format.md#b1-geometry-kit): the
  // same static shimmer measure; relative shimmer (shimmer / detail) no worse than the farmhouse's at the same range.
  // The last run repeats 50 m with 4x MSAA (the High preset): no worse than without it.
  struct KV { int D; int msaa; };
  for (KV kv : {KV{20, 0}, KV{50, 0}, KV{50, 4}}) {
    int D = kv.D;
    std::string tag = kv.msaa ? "_msaa4" : "";
    push("kit primitives " + std::to_string(D) + " m" + tag, [D, kv, tag](App& a, float t) {
      if (t == 0) {
        a.R.setMsaa(kv.msaa);
        if (kv.msaa) chk(a.R.msaa() == 4 && a.R.msaaActive() && a.R.fboErrors == 0, "4x MSAA world target created (framebuffer complete)");
        firstPerson(a, K0 + vec3(0, 0, 3.9f + (float)D), 0.0f, D == 20 ? 8.0f : 3.0f);
        float y0 = a.terrain.height(K0.x, K0.z);
        regions.clear();
        regions.push_back({"kit_folly", K0 + vec3(-4.6f, y0 + 0.2f, 3.6f), K0 + vec3(4.6f, y0 + 11.5f, 3.6f)});
      }
      a.eyeYaw = 0.0f;
      if (t > 2.5f && captureLeft == 0 && regions[0].frames == 0) { captureLeft = 24; projectRegions(a, a.R.width, a.R.height); }
      if (regions[0].frames >= 24) {
        const Region& r = regions[0];
        double s = shimmer(r), dt = detail(r), rel = dt > 0 ? s / dt : -1;
        report["kit_" + std::to_string(D) + "m" + tag] = {{"shimmer", s}, {"detail", dt}, {"relative", rel}, {"px", (r.x1 - r.x0) * (r.y1 - r.y0)}};
        if (kv.msaa) {
          double ref = report["kit_50m"].value("relative", -1.0);
          chk(rel >= 0 && ref > 0 && rel <= ref * 1.1 + 0.003, "kit primitives at 50 m with 4x MSAA: relative shimmer %.4f (shimmer %.3f, detail %.2f) <= without MSAA %.4f x 1.1 -> selftest_out/render_kit_50m_msaa4.png",
              rel, s, dt, ref);
        } else {
          double ref = report["static_" + std::to_string(D) + "m"]["farmhouse"].value("relative", -1.0);
          chk(rel >= 0 && ref > 0 && rel <= ref * 1.25 + 0.005, "kit primitives at %d m: relative shimmer %.4f (shimmer %.3f, detail %.2f) <= farmhouse %.4f x 1.25 -> selftest_out/render_kit_%dm.png",
              D, rel, s, dt, ref, D);
        }
        pendingShot = outDir() / ("render_kit_" + std::to_string(D) + "m" + tag + ".png");
        regions[0].frames = -1;
        return false;
      }
      if (regions[0].frames < 0 && pendingShot.empty()) {
        if (kv.msaa) { a.R.setMsaa(0); chk(!a.R.msaaActive(), "MSAA off again: the world renders straight into the HDR target"); }
        return true;
      }
      return false;
    });
  }
  push("render_kit_close", [](App& a, float t) {
    if (t == 0) firstPerson(a, K0 + vec3(8.0f, 0, 12.0f), 33.7f, 14.0f);
    a.eyeYaw = 33.7f; a.eyePitch = 14.0f;
    if (t > 2.0f && t < 2.0f + 1.5f / 60.0f) pendingShot = outDir() / "render_kit_close.png";
    if (t > 2.3f && pendingShot.empty()) { chk(true, "kit folly close-up -> selftest_out/render_kit_close.png"); return true; }
    return false;
  });
  // ---------------------------------------------------------------- B1 ivy on the folly ("ivy": "auto", src/ivy.cpp)
  push("kit ivy", [](App& a, float t) {
    static int phase = 0; static int verts0 = 0, best = -1; static GrassQuality gq = GrassQuality::Medium;
    if (t == 0) phase = 0;
    auto it = a.rt.find(kitId);
    if (it == a.rt.end()) { chk(false, "kit folly runtime"); return true; }
    InstanceRT& r = it->second; const AssetDef& d = *r.asset;
    int verts = r.ivy ? r.ivy->cardVerts : 0;
    if (phase == 0 && t > 0.3f) {
      chk(verts / 6 >= 100, "ivy: %d leaf cards on the folly's stone walls", verts / 6);
      std::vector<uint8_t> intact(d.panels.size(), PANEL_INTACT);
      std::vector<float> v1, v2, v3; buildIvyCards(d, d.ivy, intact, 77, 1.0f, v1); buildIvyCards(d, d.ivy, intact, 77, 1.0f, v2); buildIvyCards(d, d.ivy, intact, 78, 1.0f, v3);
      chk(!v1.empty() && v1 == v2 && v1 != v3, "ivy: the same seed grows identical cards, another instance's seed a different growth");
      float worst = 0;   // every card hugs a wall: its centre 2 .. 5.5 cm out from the face it grows on (nearest stone face)
      bool hug = true;
      for (size_t k = 0; k + 59 < v1.size(); k += 60) {
        vec3 c(0); for (int q : {0, 1, 2, 5}) c += vec3(v1[k + q * 10], v1[k + q * 10 + 1], v1[k + q * 10 + 2]) * 0.25f;   // the quad's four corners
        float dmin = 1e9f;
        for (auto& p : d.parts) if (p.visible && p.shape == Part::BOX && std::fabs(std::fabs(p.rot.w) - 1.0f) < 1e-4f) { vec3 e = glm::max(glm::abs(c - p.center) - p.size * 0.5f, vec3(0)); dmin = std::min(dmin, glm::length(e)); }
        for (auto& pn : d.panels) for (auto& p : pn.parts[0]) if (p.visible) { vec3 e = glm::max(glm::abs(c - p.center) - p.size * 0.5f, vec3(0)); dmin = std::min(dmin, glm::length(e)); }
        worst = std::max(worst, dmin); hug &= dmin > 0.0f && dmin < 0.06f;
      }
      chk(hug, "ivy: every card hugs its wall (centre outside the stone, within 6 cm of it; farthest %.3f m)", worst);
      size_t fewest = v1.size();
      for (size_t k = 0; k < d.panels.size(); ++k) { auto s = intact; s[k] = PANEL_BREACHED; std::vector<float> w; buildIvyCards(d, d.ivy, s, 77, 1.0f, w); if (w.size() < fewest) { fewest = w.size(); best = (int)k; } }
      { IvyDef au; au.mode = IvyDef::AUTO; size_t most = 0, least = SIZE_MAX;   // "auto": colonies from the ground on the outer stone walls
        for (uint32_t sd = 1; sd <= 8; ++sd) { std::vector<float> w; buildIvyCards(d, au, intact, sd, 1.0f, w); most = std::max(most, w.size()); least = std::min(least, w.size()); }
        chk(least > 0, "ivy \"auto\": every one of 8 instance seeds grows some (%zu .. %zu cards)", least / 60, most / 60); }
      chk(best >= 0 && fewest < v1.size(), "ivy: breaching bay %d takes its %zu cards with it (%zu left)", best, (v1.size() - fewest) / 60, fewest / 60);
      verts0 = verts;
      if (best >= 0) r.state[best] = PANEL_BREACHED;   // the runtime path: the next frame rebuilds without them
      phase = 1;
    } else if (phase == 1 && t > 0.6f) {
      chk(verts > 0 && verts < verts0, "ivy: the instance's cards rebuilt after the breach (%d -> %d)", verts0 / 6, verts / 6);
      if (best >= 0) r.state[best] = PANEL_INTACT;
      a.ivyOn = false; phase = 2;
    } else if (phase == 2 && t > 0.9f) {
      chk(!r.ivy, "ivy: Settings > Ivy on walls off removes it");
      a.ivyOn = true; gq = a.grass.quality; a.grass.quality = GrassQuality::Off; phase = 3;
    } else if (phase == 3 && t > 1.2f) {
      chk(!r.ivy, "ivy: none with the grass quality Off (density follows it)");
      a.grass.quality = gq; phase = 4;
    } else if (phase == 4 && t > 1.5f) {
      chk(verts == verts0, "ivy: back with every bay intact and the toggle on (%d cards)", verts / 6);
      return true;
    }
    return false;
  });
  // ---------------------------------------------------------------- moving camera: a slow strafe at 50 and 100 m
  // Walking sideways moves the facade well under a pixel per frame (fixed 60 Hz steps: the same path in every build), so
  // a well-filtered image changes almost linearly from frame to frame. The flicker index is the mean absolute second
  // temporal difference |f(t+1) - 2 f(t) + f(t-1)| per pixel inside the facade (region mean scaled to 100): shimmer,
  // crawling joints and depth fighting raise it, smooth motion does not.
  for (int D : {50, 100}) {
    push("moving camera " + std::to_string(D) + " m", [D](App& a, float t) {
      if (t == 0) {
        firstPerson(a, F0 + vec3(-6.0f, 0, kFacadeZ + (float)D), 0.0f, D == 50 ? 1.5f : 0.6f);
        regions.clear(); moving.clear(); movingLeft = 0;
      }
      a.eyeYaw = 0.0f;
      a.scriptedInput = ExploreInput{};
      if (t > 2.0f) a.scriptedInput.move = vec2(1.0f, 0.0f);   // walk right
      if (t > 2.6f && regions.empty()) {
        float y0 = a.terrain.height(F0.x, F0.z);
        regions.push_back({"farmhouse", F0 + vec3(-7.5f, y0 + 0.2f, kFacadeZ), F0 + vec3(7.5f, y0 + 3.4f, kFacadeZ)});
        projectRegions(a, a.R.width, a.R.height);
        movingLeft = 40;
      }
      for (int k = 0; k < 4; ++k) {
        float ts = 2.7f + 0.15f * k;
        if (t >= ts && t < ts + 1.5f / 60.0f) pendingShot = outDir() / ("render_moving_" + std::to_string(D) + "m_" + std::to_string(k) + ".png");
      }
      if (!regions.empty() && movingLeft == 0 && moving.size() >= 3) {
        a.scriptedInput = ExploreInput{};
        double sum = 0; size_t n = 0;
        for (size_t f = 1; f + 1 < moving.size(); ++f)
          for (size_t i = 0; i < moving[f].size(); ++i) { sum += std::fabs(moving[f + 1][i] - 2.0 * moving[f][i] + moving[f - 1][i]); ++n; }
        double fl = n ? sum / n : -1, d1 = 0; size_t n1 = 0;
        for (size_t f = 1; f < moving.size(); ++f) for (size_t i = 0; i < moving[f].size(); ++i) { d1 += std::fabs(moving[f][i] - moving[f - 1][i]); ++n1; }
        d1 = n1 ? d1 / n1 : -1;   // first difference: legitimate change from the motion over the facade's detail
        report["moving_" + std::to_string(D) + "m"] = {{"flicker", fl}, {"motion", d1}, {"relative", d1 > 0 ? fl / d1 : -1}, {"frames", (int)moving.size()}, {"px", (int)moving[0].size()}};
        note("%3d m strafe: flicker %.3f (mean |2nd temporal difference| of luma, region mean scaled to 100), motion %.3f (|1st difference|), relative %.3f over %zu frames", D, fl, d1, d1 > 0 ? fl / d1 : -1.0, moving.size());
        chk(fl >= 0, "moving camera at %d m: %zu frames, flicker %.3f -> selftest_out/render_moving_%dm_*.png", D, moving.size(), fl, D);
        return true;
      }
      return false;
    });
  }
  // ---------------------------------------------------------------- rough walls up close: sunlit front, shadowed back
  struct CV { const char* name; vec3 at; float yaw, pitch; };
  static const CV closeups[] = {{"render_walls_close_sun", vec3(-3.0f, 0, kFacadeZ + 6.5f), 0.0f, 6.0f},
                                {"render_walls_close_shadow", vec3(2.0f, 0, -kFacadeZ - 6.5f), 180.0f, 6.0f},
                                {"render_walls_ruin_close", vec3(19.5f - 2.0f, 0, 4.0f + 7.0f), 0.0f, 12.0f}};
  for (const CV& cv : closeups) {
    push(cv.name, [cv](App& a, float t) {
      if (t == 0) firstPerson(a, F0 + cv.at, cv.yaw, cv.pitch);
      a.eyeYaw = cv.yaw; a.eyePitch = cv.pitch;
      if (t > 2.0f && t < 2.0f + 1.5f / 60.0f) pendingShot = outDir() / (std::string(cv.name) + ".png");
      if (t > 2.3f && pendingShot.empty()) { chk(true, "%s -> selftest_out/%s.png", cv.name, cv.name); return true; }
      return false;
    });
  }
  // R1 terrain material cache shots (rendering.md 13): close ground, a steep fresh crater, a diagonal across a chunk corner
  for (int k = 0; k < 3; ++k) {
    static const char* names[3] = {"render_r1_terrain_close", "render_r1_crater", "render_r1_chunk_diag"};
    push(names[k], [k](App& a, float t) {
      const float cs = Terrain::kChunkCells * Terrain::kCell;
      vec2 c = a.terrain.org + glm::round((vec2(-40.0f, 60.0f) - a.terrain.org) / cs) * cs;   // a chunk corner
      vec3 at = k == 0 ? vec3(-30.0f, 0, 8.0f) : k == 1 ? vec3(-10.0f, 0, 31.0f) : vec3(c.x + 4.5f, 0, c.y + 4.5f);
      float yaw = k == 2 ? 45.0f : 0.0f, pitch = k == 0 ? -40.0f : k == 1 ? -32.0f : -14.0f;
      if (t == 0) {
        if (k == 1) { vec3 p(-10.0f, 0, 25.0f); p.y = a.terrain.height(p.x, p.z); a.craterAt(p, 3.0f, 1.6f, false); }
        firstPerson(a, at, yaw, pitch);
      }
      a.eyeYaw = yaw; a.eyePitch = pitch;
      if (t > 2.0f && t < 2.0f + 1.5f / 60.0f) pendingShot = outDir() / (std::string(names[k]) + ".png");
      if (t > 2.3f && pendingShot.empty()) { chk(true, "%s -> selftest_out/%s.png", names[k], names[k]); return true; }
      return false;
    });
  }
  push("leave first person", [](App& a, float t) {
    if (t == 0 && a.mode == Mode::Explore) a.exitExplore();
    return t > 0.3f;
  });
  // ---------------------------------------------------------------- water
#ifdef NMK_WATER
  // ---------------------------------------------------------------- water (water.cpp, tools/catalog_water.mjs)
  push("water assets", [](App& a, float t) {
    if (t > 0) return true;
    struct Want { const char* id; const char* kind; int sockets; bool cut; };
    const Want W[] = {{"wat_pond_round", "pond", 0, true}, {"wat_pond_irregular", "pond", 0, true}, {"wat_crater_pool", "crater", 0, true},
                      {"wat_puddles", "puddle", 0, true}, {"wat_stream_straight", "stream", 2, true}, {"wat_stream_bend", "stream", 2, true},
                      {"wat_stream_end", "stream", 1, true}, {"env_sea_shallows", "sea", 0, false}};
    for (auto& w : W) {
      const AssetDef* d = a.lib.find(w.id);
      if (!chk(d && waterAsset(d), "%s: loads with a water block", w.id)) continue;
      auto S = waterSurfaces(d);
      int tris = 0; float area = 0; bool kindOk = !S.empty(), flows = true;
      for (auto& s : S) { tris += s.tris; area += s.area; kindOk = kindOk && s.kind == w.kind; flows = flows && (s.kind != "stream" || s.flows); }
      int nsock = 0; bool sockOk = true;
      if (d->raw.contains("snap")) for (auto& s : d->raw["snap"]) {
        ++nsock;
        vec3 p = jvec3(s["p"]), dir = jvec3(s["dir"]);
        sockOk = sockOk && s.value("kind", "") == "stream" && std::fabs(glm::length(dir) - 1.0f) < 0.01f && std::fabs(p.y) < 1e-3f;
      }
      chk(kindOk && tris > 0 && flows && nsock == w.sockets && sockOk && (d->navMeta.terrainCuts.empty() != w.cut) && d->thumbnail != 0,
          "%s: %zu %s surface(s), %d triangles, %.1f m^2, %s, %d snap socket(s)%s, %s, thumbnail", w.id, S.size(), w.kind, tris, area,
          w.cut ? "terrain-cut bed" : "no cut (sheet over the sea bed)", nsock, nsock ? " (kind stream, unit directions)" : "", flows ? (std::string(w.kind) == "stream" ? "flow along the centreline" : "still") : "NO FLOW");
    }
    return true;
  });
  push("water placement", [](App& a, float t) {
    if (t > 0) return true;
    const AssetDef* pond = a.lib.find("wat_pond_round"); const AssetDef* house = a.lib.find("vil_norman_farmhouse");
    if (!pond || !house) return true;
    vec3 P = W0 + vec3(0, 0, 0); P.y = a.terrain.height(P.x, P.z);
    PlaceCheck c1 = a.checkPlacement(pond, P, 0, 0);
    PlaceCheck c2 = a.checkPlacement(pond, F0 + vec3(3, 0, 0), 0, 0);
    chk(c1.ok && !c2.ok, "pond: valid on open ground (%s); refused over the farmhouse (%s)", c1.reason.c_str(), c2.reason.c_str());
    uint64_t id = place(a, "wat_pond_round", P);
    PlaceCheck c3 = a.checkPlacement(house, P + vec3(2, 0, 1), 0, 0);
    chk(id && !c3.ok, "a house on the pond is refused: %s", c3.reason.c_str());
    float keep = a.terrain.waterLevel;   // the terrain-water rule (sea maps): footprint in water is refused
    a.terrain.waterLevel = a.terrain.height(P.x + 40, P.z) + 5.0f;
    PlaceCheck c4 = a.checkPlacement(pond, P + vec3(40, 0, 0), 0, 0);
    PlaceCheck c5 = a.checkPlacement(house, P + vec3(40, 0, 0), 0, 0);
    a.terrain.waterLevel = keep;
    chk(!c4.ok && !c5.ok, "below the template water level both a pond and a house are refused (%s / %s)", c4.reason.c_str(), c5.reason.c_str());
    a.syncRuntime();
    Instance* pi = a.doc.find(id);
    float ws = waterSurfaceAt(a, P.x, P.z), dry = waterSurfaceAt(a, P.x + 12, P.z);
    chk(pi && std::fabs(ws - (pi->pos.y - 0.35f)) < 0.01f && dry < -1e8f, "water surface lookup: %.2f m at the pond centre (placement %.2f, level -0.35), none outside", ws, pi ? pi->pos.y : 0.0f);
    return true;
  });
  push("water layout: ponds, crater, puddles, a snapped stream, a sea sheet", [](App& a, float t) {
    if (t > 0) return t > 1.0f;
    place(a, "wat_pond_irregular", W0 + vec3(28, 0, -4));
    place(a, "wat_crater_pool", W0 + vec3(0, 0, 24));
    place(a, "wat_puddles", W0 + vec3(18, 0, 22));
    // the stream: spring end -> straight -> bend -> straight, each placed by its sockets (socket on socket, directions
    // opposed) and on one base height, as snapping will (V6)
    auto sockets = [&](const AssetDef* d) { std::vector<std::pair<vec3, vec3>> v; if (d && d->raw.contains("snap")) for (auto& s : d->raw["snap"]) v.push_back({jvec3(s["p"]), jvec3(s["dir"])}); return v; };
    auto ang = [](vec3 d) { return degrees(std::atan2(d.x, d.z)); };
    float baseY = a.terrain.height(W0.x + 60, W0.z);
    struct Piece { const char* id; int in; } chain[] = {{"wat_stream_end", -1}, {"wat_stream_straight", 0}, {"wat_stream_bend", 0}, {"wat_stream_straight", 0}};
    vec3 openP = W0 + vec3(60, 0, -12), openD(0, 0, 1); openP.y = baseY;
    bool joined = true; int n = 0; float worst = 0;
    for (auto& pc : chain) {
      const AssetDef* d = a.lib.find(pc.id); auto S = sockets(d); if (S.empty()) { joined = false; break; }
      float yaw; vec3 pos;
      if (pc.in < 0) { yaw = 0; pos = openP - yawQuat(yaw) * S[0].first; }
      else { yaw = ang(-openD) - ang(S[pc.in].second); pos = openP - yawQuat(yaw) * S[pc.in].first; }
      pos.y = baseY;
      Instance i; i.id = a.doc.newId(); i.assetId = d->id; i.assetVersion = d->version; i.pos = pos; i.yaw = wrapDeg(yaw);
      a.doc.instances.push_back(i); ++n;
      int out = pc.in < 0 ? 0 : (int)S.size() - 1 - pc.in;
      if (pc.in >= 0) worst = std::max(worst, glm::length(pos + yawQuat(yaw) * S[pc.in].first - openP));
      openP = pos + yawQuat(yaw) * S[out].first; openD = yawQuat(yaw) * S[out].second;
    }
    a.syncRuntime();
    // across every joint the water is continuous: same surface height 10 cm either side
    bool level = true; float dl = 0;
    {
      float h0 = waterSurfaceAt(a, W0.x + 60, W0.z - 8.0f), h1 = waterSurfaceAt(a, W0.x + 60, W0.z - 2.2f), h2 = waterSurfaceAt(a, W0.x + 60, W0.z - 1.8f);
      dl = std::max(std::fabs(h1 - h0), std::fabs(h2 - h1)); level = h0 > -1e8f && h1 > -1e8f && h2 > -1e8f && dl < 0.01f;
    }
    chk(joined && n == 4 && worst < 0.01f && level, "stream chain: %d pieces joined socket to socket (worst gap %.3f m), water continuous across the joint (step %.3f m)", n, worst, dl);
    // a sea sheet raised 0.6 m over the pasture: shallow water with a shore fade at nothing (flat bed)
    if (const AssetDef* sea = a.lib.find("env_sea_shallows")) {
      Instance i; i.id = a.doc.newId(); i.assetId = sea->id; i.assetVersion = sea->version;
      i.pos = W0 + vec3(30, 0, 70); i.pos.y = a.terrain.height(i.pos.x, i.pos.z) + 0.6f;
      a.doc.instances.push_back(i); a.syncRuntime();
    }
    return false;
  });
  struct WView { const char* name; vec3 from, at; bool fp; };
  static const WView views[] = {
    {"render_water_pond", vec3(0, 0, 12.5f), vec3(0, -0.6f, 0), true},
    {"render_water_pond_irregular", vec3(28, 0, 9), vec3(28, -0.5f, -4), true},
    {"render_water_crater", vec3(-6.5f, 0, 31), vec3(0, -1.2f, 24), true},
    {"render_water_puddles", vec3(18, 0, 28.5f), vec3(18, -0.2f, 22), true},
    {"render_water_stream", vec3(52, 0, -18), vec3(60, -0.6f, 0), true},
    {"render_water_overview", vec3(30, 26, 48), vec3(32, 0, 4), false},
    {"render_water_sea", vec3(30, 0, 34), vec3(30, 0, 70), true},
  };
  for (auto& vw : views) {
    push(vw.name, [vw](App& a, float t) {
      if (t == 0) {
        vec3 from = W0 + vw.from, at = W0 + vw.at;
        at.y += a.terrain.height(at.x, at.z);
        if (vw.fp) { vec3 eye = from; eye.y = a.terrain.height(from.x, from.z) + 1.62f; vec3 d = at - eye; firstPerson(a, from, degrees(std::atan2(-d.x, -d.z)), degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))))); }
        else { if (a.mode == Mode::Explore) a.exitExplore(); from.y += a.terrain.height(from.x, from.z); vec3 d = at - from; a.camPos = from; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
        wDraws = 0; wGpu = 0; wN = 0;
      }
      if (t > 1.5f) { wDraws = std::max(wDraws, waterStats().draws); wGpu += a.R.stats.gpuMs; ++wN; }
      if (t > 2.5f && pendingShot.empty() && wN > 0 && wN < 1000) { pendingShot = outDir() / (std::string(vw.name) + ".png"); wN += 1000; return false; }
      if (wN >= 1000 && pendingShot.empty()) {
        int frames = wN - 1000;
        report[std::string("water_") + vw.name] = {{"draws", wDraws}, {"gpu_ms", wGpu / std::max(1, frames)}};
        chk(wDraws > 0 && a.R.shaderErrors == 0, "%s: %d water surfaces drawn, GPU %.2f ms -> selftest_out/%s.png", vw.name, wDraws, wGpu / std::max(1, frames), vw.name);
        return true;
      }
      return false;
    });
  }
  push("leave first person (water)", [](App& a, float t) {
    if (t == 0 && a.mode == Mode::Explore) a.exitExplore();
    return t > 0.3f;
  });
#endif
  // PF2 GPU budget (docs/rendering.md#gpu-budget): per-pass GPU ms on one fixed elevated view over the facades and the
  // water, at Medium then High. Reported only, never gated (the GPU is shared with other work).
  push("gpu budget", [](App& a, float t) {
    static double acc[GP_Count], probeCpu = 0; static int n = 0, phase = 0; static float t0 = 0;
    if (t == 0) {
      if (a.mode == Mode::Explore) a.exitExplore();
      vec3 from(25, 0, 62), at(25, 0, -12); from.y = a.terrain.height(from.x, from.z) + 30; at.y = a.terrain.height(at.x, at.z);
      vec3 d = at - from; a.camPos = from; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z))));
      settings().applyPreset(a, GfxPreset::Medium); a.R.resetTemporal(); std::fill(acc, acc + GP_Count, 0.0); n = 0; phase = 0; t0 = 0; probeCpu = 0;
    }
    if (t - t0 > 1.5f) { for (int i = 0; i < GP_Count; ++i) acc[i] += a.R.gpuPassMs[i]; probeCpu += a.R.stats.probeCpuMs; ++n; }
    if (t - t0 < 3.5f) return false;
    const char* pn = phase == 0 ? "Medium" : "High";
    json j = json::object(); double tot = 0; std::string line;
    for (int i = 0; i < GP_Count; ++i) {
      double ms = acc[i] / std::max(1, n); tot += ms; j[gpuPassName(i)] = std::round(ms * 1000) / 1000;
      if (ms >= 0.05) { char b[64]; std::snprintf(b, sizeof b, "%s%s %.2f", line.empty() ? "" : ", ", gpuPassName(i), ms); line += b; }
    }
    report["gpu_budget"][pn] = {{"passMs", j}, {"totalMs", tot}, {"frames", n}, {"timers", a.R.gpuTimers}, {"probeCpuMs", probeCpu / std::max(1, n)}};
    note("GPU budget at %s (reported only, %d frames): %.2f ms total; %s; probe capture CPU %.2f ms", pn, n, tot, line.c_str(), probeCpu / std::max(1, n));
    if (phase == 0) { phase = 1; t0 = t; std::fill(acc, acc + GP_Count, 0.0); n = 0; probeCpu = 0; settings().applyPreset(a, GfxPreset::High); a.R.resetTemporal(); return false; }
    settings().applyPreset(a, GfxPreset::Medium);
    chk(!a.R.gpuTimers || tot > 0, "GPU budget: the per-pass timers report (%.2f ms at High)", tot);
    return true;
  });
  push("finish", [](App& a, float t) {
    if (t < 0.3f) return false;
    std::ofstream(outDir() / "render_shimmer.json") << report.dump(2);
    chk(a.R.shaderErrors == 0, "shader errors at the end: %d", a.R.shaderErrors);
    chk(a.R.fboErrors == 0, "framebuffer errors: %d", a.R.fboErrors);
    return true;
  });
}
}  // namespace

bool renderSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-render") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) {
    if (captureLeft > 0 || movingLeft > 0 || !pendingShot.empty()) {
      app.R.readPixels(lastPx, lastW, lastH);
      if (captureLeft > 0) { accumulate(lastPx, lastW); --captureLeft; }
      if (movingLeft > 0 && !regions.empty()) { captureMoving(lastPx, lastW); --movingLeft; }
    }
    if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); }
    return true;
  }
  if (!started) { started = true; T = new MpTest("render"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    if (st == 0) logf("[render] step: %s", seq[cur].name.c_str());
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 300) { T->check(false, "render test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    if (app.mode == Mode::Explore) app.exitExplore();
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
