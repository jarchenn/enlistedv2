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
#include "mptest.h"
#if __has_include("water.h")   // the same file builds against the pre-V1 tree for the before numbers
#include "water.h"
#define NMK_WATER 1
#endif
#include <stb_image_write.h>
#include <cstdarg>
#include <functional>
#include <fstream>

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
      a.syncRuntime();
      chk(a.R.shaderErrors == 0, "shader compile errors at start: %d", a.R.shaderErrors);
    }
    return t > 0.5f;
  });
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
