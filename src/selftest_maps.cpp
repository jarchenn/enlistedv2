// --selftest-maps (stages 4100-4199): W5 default maps (SPEC 3). For every authored template with map content
// (content/templates/*.json with "map": Omaha Easy Red; Mont-Castre's map file has no "scenario", so it runs the
// terrain-base case instead, terrainBaseChecks: size, relief, slopes, cliffs, walkable route, save / reopen, views):
//  * New Map "Create editable copy" loads the template: authored heightmap and water, every instance, the scenario;
//    the copy is an unsaved creative document that saves / reopens like any map, and edits like any map;
//  * the scenario validates with no errors; SPEC 2.1 distance bands for objectives and spawns; every spawn has its
//    team rack; every objective has linked structures that exist and >= 3 MG-nest defense slots;
//  * placed heights agree with the editor's placement rule; the map is in the Play "Default maps" list; the New Map
//    preview thumbnail exists and is not blank;
//  * docs/asset-usage-default-maps.md names every catalogue asset;
//  * screenshots from the A1 spawn (first person) and of every objective, with frame time, draws and memory.
// --render-map-thumbs renders each template's preview from its "thumbnailCamera" into selftest_out/<id>.thumb.png.
#include "selftest.h"
#include "editor_nav.h"
#include "scenario.h"
#include "w5.h"
#include "mpui.h"
#include "settings.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <chrono>
#include <fstream>
#include <cstdarg>
#include <functional>
#include <algorithm>

// W1 load check (NMK_WX_CYCLE=1 with --selftest-maps): per gameplay view after the measurement, every weather preset in menu
// order, Map default, then Lighting Classic Clear / Overcast; a shot each (wx_<map>_<view>_NN.png) and its mean luminance,
// black share and white share in the log: a white-out or an all-black preset shows at once
const bool kWxCycle = std::getenv("NMK_WX_CYCLE") != nullptr;
int gWxPh = 0; float gWxT = 0;
bool gLookdevRun = false;   // CP0: --selftest-lookdev (selftest.cpp)
namespace {
fs::path outDirM() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
double wsMB() { ProcMem pmc = processMemory(); return pmc.WorkingSetSize / 1048576.0; }
std::string fmt(const char* f, ...) { char b[600]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<int> mapTemplates(App& app) {
  std::vector<int> v;
  const char* only = std::getenv("NMK_MAPS_ONLY");   // dev hook: just this template id (e.g. chateau_town)
  for (int k = 0; k < (int)app.templates.size(); ++k)
    if (app.templates[k].raw.contains("map") && (!only || !*only || app.templates[k].id == only)) v.push_back(k);
  return v;
}
struct View { std::string name; vec3 pos; float yaw, pitch; bool explore = false; bool gate = true; float hour = -1.0f; bool groundEye = false, neutral = false; int dbg = -1; bool ggOff = false; int weather = -1; float cover = -1.0f; int light = -1; float walk = 0.0f; };   // VO walk: seconds walked forward at 1.4 m/s after the measurement, a frame every 0.5 s (motion check)   // VO: light 1 New / 0 Classic (Settings > Lighting)   // SKY1: weather 0 clear / 1 cloudy, cover forced   // GR3: hour >= 0 sets the time of day for that view; groundEye: pos.y is the eye height above the terrain; neutral: the map's look tint / saturation / ambient off (tracing a colour cast)   // gate: gameplay view held to the frame target
struct Perf { std::string name; bool gate = true; int frames = 0; double ms = 0, worst = 0, draws = 0, tris = 0; int preset = 1; bool night = false; double gpu = 0, pass[GP_Count] = {}; int abN[2] = {}; double abMs[2] = {}, abPass[2][GP_Count] = {}, abProbeCpu[2] = {}, abProbes[2] = {}, abCpu[2] = {};
              double x[10] = {}, abX[2][10] = {}; };   // R2: the frame counters below (kXName), plain run and A/B sides   // ab: [0] PF2 renderer, [1] the A/B baseline (NMK_PERF_AB)   // night: L2 Night at Medium (reported only)   // preset: GfxPreset (V2: gameplay views also at Low / High, reported only)

bool thumbOnly = false;
std::vector<int> sTpl; size_t sTi = 0;
std::vector<View> sViews; size_t sVi = 0; bool sViewInit = false;
// FL1 flicker capture (NMK_FL1=1): per view, Medium, the animation clock held (Renderer::holdTime). Two still frames 0.3 s
// apart should be identical: pixels whose luma moves more than 0.08 flicker (z-fighting under the TAA jitter, sparkle,
// popping, flickering lights). Then 5 cm to the side, settled: mean |dL| (the ZF1 measure). Flickering 32 px cells (6+ such
// pixels) are named by a physics ray through the cell (asset id, "terrain" for ground / grass / water, "sky").
// -> selftest_out/fl1.json, fl1_<map>_<view>_a|b|c.png and _diff.png (flicker in red over the dimmed frame)
const bool kFl1 = std::getenv("NMK_FL1") != nullptr;
// L3: NMK_FL1_WIND=1: the clock runs (wind on) and b is the very next frame, so sway moves leaves only a few mm between
// a and b: what still flickers there is sparkle under motion (smear: look at the _a shots)
const bool kFl1Wind = std::getenv("NMK_FL1_WIND") != nullptr;
int gWalkN = 0; vec3 gWalk0{0};   // VO motion check
int gFlPh = 0; float gFlT = 0; vec3 gFlCam{0}; json gFl1 = json::array(), gFlState = json::array();   // state: exposure / probes at each shot
std::string fl1Analyse(App& app, const std::string& map, const std::string& view, const std::string& base) {
  int w = 0, h = 0, n = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
  auto ld = [&](const char* sfx, int& W, int& H) { return stbi_load((outDirM() / (base + sfx + ".png")).string().c_str(), &W, &H, &n, 3); };
  unsigned char *A = ld("_a", w, h), *B = ld("_b", w2, h2), *C = ld("_c", w3, h3);
  if (!A || !B || !C || w != w2 || h != h2 || w != w3 || h != h3) { stbi_image_free(A); stbi_image_free(B); stbi_image_free(C); return "FL1 " + base + ": captures missing"; }
  auto L = [](const unsigned char* p, size_t i) { return (0.2126f * p[i * 3] + 0.7152f * p[i * 3 + 1] + 0.0722f * p[i * 3 + 2]) / 255.0f; };
  const int cs = 32, cx = (w + cs - 1) / cs, cy = (h + cs - 1) / cs;
  std::vector<int> cell((size_t)cx * cy, 0); std::vector<unsigned char> D((size_t)w * h * 3);
  long flick = 0; double moved = 0;
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    size_t i = (size_t)y * w + x; float d = std::fabs(L(A, i) - L(B, i)); moved += std::fabs(L(A, i) - L(C, i));
    bool f = d > 0.08f; flick += f; if (f) ++cell[(size_t)(y / cs) * cx + x / cs];
    for (int c = 0; c < 3; ++c) D[i * 3 + c] = f ? (c == 0 ? 255 : 0) : (unsigned char)(A[i * 3 + c] * 0.35f);
  }
  stbi_write_png((outDirM() / (base + "_diff.png")).string().c_str(), w, h, 3, D.data(), w * 3);
  stbi_image_free(A); stbi_image_free(B); stbi_image_free(C);
  std::map<std::string, int> who;
  mat4 inv = glm::inverse(app.projM * app.viewM);
  for (int j = 0; j < cy; ++j) for (int i = 0; i < cx; ++i) {
    int k = cell[(size_t)j * cx + i]; if (k < 6) continue;
    vec2 ndc(2.0f * (i + 0.5f) * cs / w - 1.0f, 1.0f - 2.0f * (j + 0.5f) * cs / h);
    vec4 a = inv * vec4(ndc, -1, 1), b = inv * vec4(ndc, 1, 1); vec3 ro = vec3(a) / a.w, rd = glm::normalize(vec3(b) / b.w - ro);
    Physics::Hit ht; std::string nm = "sky";
    if (Physics::castRayEx(ro, rd, 3000.0f, ht)) { const Instance* in = (ht.ud >> 16) ? app.doc.find(ht.ud >> 16) : nullptr; nm = in ? in->assetId : ht.ud ? "body" : "terrain"; }
    who[nm] += k;
  }
  json r = {{"map", map}, {"view", view}, {"state", gFlState}, {"flickerPx", flick}, {"flickerPct", 100.0 * flick / ((double)w * h)}, {"moveMeanDL", moved / ((double)w * h)}, {"regions", who}};
  gFl1.push_back(r);
  std::ofstream(outDirM() / "fl1.json") << gFl1.dump(1);
  std::string top; for (auto& [k2, v2] : who) if (v2 >= 40) top += " " + k2 + ":" + std::to_string(v2);
  char b[600]; std::snprintf(b, sizeof b, "FL1 %s %s: %ld flickering px (%.3f %%), 5 cm move mean dL %.4f;", map.c_str(), view.c_str(), flick, 100.0 * flick / ((double)w * h), moved / ((double)w * h));
  return b + top;
}
// FX1 headlamp captures (NMK_FX1=1, Medium): per view the headlamp off, then on; first-person views also crouched and
// aiming down the sights; frame time with it off and on (1 s each) -> selftest_out/fx1_<map>_<view>_<state>.png
const bool kFx1 = std::getenv("NMK_FX1") != nullptr;
// CP0 look-dev readout (--selftest-lookdev, or NMK_LOOKDEV=1 with --selftest-maps): per gameplay view, at the current
// preset, after the frame-time measurement: the view as played (clipped / crushed pixel shares), then the look-dev props
// (lookdev.cpp) with five grey cards 5.5 m ahead, read in scene-linear HDR under the illum view (albedo 0.18) and the sky
// view (the same without the sun's direct light). The card with the most sun gives sun:shade = log2(illum / sky) and
// sun:sky = illum / sky - 1; its sRGB value in the props shot is the grey card in sun; the sky shot's centre patch is
// scene 0.18 through the display chain. -> selftest_out/lookdev.json and ld_<map>_<view>[_props|_illum|_sky|_lum].png
bool ldRun() { static const bool e = std::getenv("NMK_LOOKDEV") != nullptr; return gLookdevRun || e; }
int gLdPh = 0; float gLdT = 0; float gLdExpo = 0; std::vector<vec3> gLdCards, gLdIll, gLdSky; std::vector<glm::ivec2> gLdPx; json gLd = json::object();
float lumOf(vec3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
std::string ldAnalyse(App& app, const std::string& map, const std::string& view, const std::string& base) {
  int w = 0, h = 0, n = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
  auto ld = [&](const char* sfx, int& W, int& H) { return stbi_load((outDirM() / (base + sfx + ".png")).string().c_str(), &W, &H, &n, 3); };
  unsigned char *A = ld("", w, h), *P = ld("_props", w2, h2), *S = ld("_sky", w3, h3);
  std::string r;
  if (!A || !P || !S || w != w2 || h != h2 || w != w3 || h != h3) r = "look-dev " + base + ": captures missing";
  else {
    long clip = 0, crush = 0;
    for (long k = 0; k < (long)w * h; ++k) { int m = std::max({A[k * 3], A[k * 3 + 1], A[k * 3 + 2]}); clip += m >= 254; crush += m <= 3; }
    auto patch = [&](unsigned char* I, int cx, int cy, int hw) { double s = 0; int c = 0;
      for (int y = std::max(0, cy - hw); y <= std::min(h - 1, cy + hw); ++y) for (int x = std::max(0, cx - hw); x <= std::min(w - 1, cx + hw); ++x, ++c)
        s += 0.2126 * I[(y * w + x) * 3] + 0.7152 * I[(y * w + x) * 3 + 1] + 0.0722 * I[(y * w + x) * 3 + 2];
      return c ? s / c : -1.0; };
    double mid = patch(S, w / 2, h / 2, 6);
    int best = -1; float bestR = 0, minR = 1e9f;
    for (size_t k = 0; k < gLdCards.size() && k < gLdIll.size() && k < gLdSky.size(); ++k) {
      float li = lumOf(gLdIll[k]), ls = lumOf(gLdSky[k]);
      if (li <= 0 || ls <= 0) continue;
      float ra = li / ls; minR = std::min(minR, ra); if (ra > bestR) { bestR = ra; best = (int)k; }
    }
    double card = best >= 0 && best < (int)gLdPx.size() ? patch(P, gLdPx[best].x, gLdPx[best].y, 1) : -1.0;
    json j = {{"clippedPct", 100.0 * clip / ((double)w * h)}, {"crushedPct", 100.0 * crush / ((double)w * h)}, {"mid18Srgb", mid}, {"exposure", gLdExpo},
              {"cardsSeen", best >= 0 ? (int)std::count_if(gLdIll.begin(), gLdIll.end(), [](vec3 c) { return c.r >= 0; }) : 0}};
    if (best >= 0) { j["sunShadeStops"] = std::log2(bestR); j["sunSky"] = bestR - 1.0f; j["shadiestCardStops"] = std::log2(minR); j["cardSunSrgb"] = card; }
    gLd[map][view] = j;
    std::ofstream(outDirM() / "lookdev.json") << gLd.dump(1);
    r = best < 0 ? fmt("look-dev %s %s: no grey card on screen; clipped %.2f %%, crushed %.2f %%, scene 0.18 -> sRGB %.0f", map.c_str(), view.c_str(), j["clippedPct"].get<double>(), j["crushedPct"].get<double>(), mid)
                 : fmt("look-dev %s %s: sun:shade %.2f stops (sun:sky %.1f:1; shadiest card %.2f), grey card in sun sRGB %.0f, scene 0.18 -> sRGB %.0f, clipped %.2f %%, crushed %.2f %%, exposure %.3f",
                       map.c_str(), view.c_str(), std::log2(bestR), bestR - 1.0f, std::log2(minR), card, mid, j["clippedPct"].get<double>(), j["crushedPct"].get<double>(), gLdExpo);
  }
  stbi_image_free(A); stbi_image_free(P); stbi_image_free(S);
  return r;
}
int gFxPh = 0; float gFxT = 0; double gFxMs[2] = {0, 0}; int gFxN[2] = {0, 0}; double gFxLast = 0;
std::vector<Perf> sPerf; int sPresetPass = 0; double sLast = 0; double sLoadS = 0, sMemBefore = 0, sMemAfter = 0;
json sSum = json::object();

bool writeThumb(App& app, const fs::path& p) {
  std::vector<uint8_t> px; int w = 0, h = 0;
  if (!app.R.readPixels(px, w, h) || w < 64 || h < 64) return false;
  // crop to 16:10 about the centre and box-filter to a plain 320 x 200 PNG (the New Map tile's aspect; the loader,
  // w5TemplateThumbnail, pads it into the band the dialog samples)
  int cw = std::min(w, (int)(h * 1.6f)), ch = (int)(cw / 1.6f), x0 = (w - cw) / 2, y0 = (h - ch) / 2;
  const int TW = 320, TH = 200;
  std::vector<uint8_t> out((size_t)TW * TH * 4, 255);
  for (int y = 0; y < TH; ++y)
    for (int x = 0; x < TW; ++x) {
      int sx0 = x0 + x * cw / TW, sx1 = x0 + (x + 1) * cw / TW, sy0 = y0 + y * ch / TH, sy1 = y0 + (y + 1) * ch / TH;
      double acc[3] = {0, 0, 0}; int n = 0;
      for (int sy = sy0; sy < std::max(sy1, sy0 + 1); ++sy)
        for (int sx = sx0; sx < std::max(sx1, sx0 + 1); ++sx) { const uint8_t* q = &px[((size_t)(h - 1 - sy) * w + sx) * 4]; for (int c = 0; c < 3; ++c) acc[c] += q[c]; ++n; }
      uint8_t* o = &out[((size_t)y * TW + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = (uint8_t)(acc[c] / std::max(1, n));
    }
  return stbi_write_png(p.string().c_str(), TW, TH, 4, out.data(), TW * 4) != 0;
}
double blankFraction(const fs::path& p) {
  int w = 0, h = 0, n = 0;
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &n, 3);
  if (!d) return 1.0;
  int y0 = (int)(h * 0.2), y1 = (int)(h * 0.8);
  long long same = 0, tot = 0; const stbi_uc* r = d + ((size_t)y0 * w) * 3;
  for (int y = y0; y < y1; ++y) for (int x = 0; x < w; ++x, ++tot) { const stbi_uc* q = d + ((size_t)y * w + x) * 3; same += std::abs(q[0] - r[0]) + std::abs(q[1] - r[1]) + std::abs(q[2] - r[2]) < 12; }
  stbi_image_free(d);
  return tot ? (double)same / tot : 1.0;
}
// R2: per-frame counters averaged per view (CPU: render() submission, buildDrawLists; draws: all passes / the two sun cascades)
const char* kXName[10] = {"draws", "shadowDraws", "renderCpuMs", "drawListMs", "roomCulled", "casterCulled", "probePending", "probeDirty", "probeStaleMax", "tris"};
void sampleX(App& app, double* x, int n) {
  const FrameStats& s = app.R.stats;
  double v[10] = {(double)s.draws, (double)s.shadowDraws, s.cpuMs, app.drawListMs, (double)s.roomCulled, (double)s.casterCulled, (double)s.probePending, (double)s.probeDirty, (double)s.probeStaleMax, (double)s.tris};
  for (int i = 0; i < 10; ++i) x[i] = (x[i] * n + v[i]) / (n + 1);
}
json xJson(const double* x) { json j = json::object(); for (int i = 0; i < 10; ++i) j[kXName[i]] = std::round(x[i] * 100) / 100; return j; }
json passJson(const double* ms) { json j = json::object(); for (int i = 0; i < GP_Count; ++i) j[gpuPassName(i)] = std::round(ms[i] * 1000) / 1000; return j; }
// Views authored in the template ("views": [{name, pos, yaw, pitch, gameplay}]), e.g. the terrain base's cliff shots.
void templateViews(const TemplateInfo& t) {
  for (auto& v : t.raw.value("views", json::array()))
    sViews.push_back({v.value("name", std::string("view")), jvec3(jkey(v, "pos"), vec3(0)), v.value("yaw", 0.0f), v.value("pitch", 0.0f), false, v.value("gameplay", false)});
  // R2 measurement views (NMK_EXTRA_VIEWS=<file.json>: {"<map id>": [{"name", "pos", "yaw", "pitch", "explore"}]}): orchards,
  // interiors through windows, damaged houses. "explore": first person on the ground at pos (x, z), else a free camera.
  if (const char* xf = std::getenv("NMK_EXTRA_VIEWS")) {
    json xj; try { xj = json::parse(readTextFile(xf)); } catch (...) {}
    if (xj.is_object()) for (auto& v : xj.value(t.id, json::array()))
      sViews.push_back({v.value("name", std::string("view")), jvec3(jkey(v, "pos"), vec3(0)), v.value("yaw", 0.0f), v.value("pitch", 0.0f), v.value("explore", false), true, v.value("hour", -1.0f), v.value("groundEye", false), v.value("neutral", false), v.value("dbg", -1), v.value("ggOff", false), v.value("weather", -1), v.value("cover", -1.0f), v.value("light", -1), v.value("walk", 0.0f)});   // GG1: dbg = terrain debug view
  }
}
}  // namespace

// W5b terrain base (Normandy Forest - Mont-Castre): size, relief and slope statistics, cliffs steeper than 60 deg, a
// walkable route from the attackers' edge to the ridge top, no NaNs, an empty editable document that saves / reopens.
bool SelfTest::terrainBaseChecks(App& app, const TemplateInfo& t, const json& mj) {
  const std::string id = t.id;
  const Terrain& T = app.terrain;
  check(app.doc.instances.empty() && mj.value("instances", json::array()).empty(), id + ": terrain base: no authored instances (the user places everything)");
  check(std::fabs(app.doc.width - 850) < 1 && std::fabs(T.buildHalfZ * 2 - 1700) < 1 && std::fabs(t.width - 850) < 1 && std::fabs(t.depth - 1700) < 1,
        fmt("%s: map size %.0f x %.0f m (half of Omaha's area, 1:2)", id.c_str(), app.doc.width, T.buildHalfZ * 2));
  // vertices inside the build area
  const float c = Terrain::kCell;
  auto inB = [&](int i, int j) { float x = T.org.x + i * c, z = T.org.y + j * c; return std::fabs(x) <= T.buildHalfX && std::fabs(z) <= T.buildHalfZ; };
  auto H = [&](int i, int j) { return T.h[(size_t)j * T.nx + i]; };
  int nan = 0; float lo = 1e9f, hi = -1e9f, maxS = 0; long n60 = 0, n50 = 0, nAll = 0;
  for (int j = 1; j < T.nz - 1; ++j)
    for (int i = 1; i < T.nx - 1; ++i) {
      if (!std::isfinite(H(i, j))) { ++nan; continue; }
      if (!inB(i, j)) continue;
      lo = std::min(lo, H(i, j)); hi = std::max(hi, H(i, j));
      float gx = (H(i + 1, j) - H(i - 1, j)) / (2 * c), gz = (H(i, j + 1) - H(i, j - 1)) / (2 * c);
      float sdeg = degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
      maxS = std::max(maxS, sdeg); n60 += sdeg > 60; n50 += sdeg > 50; ++nAll;
    }
  check(nan == 0, fmt("%s: heightmap has no NaN / inf heights (%d)", id.c_str(), nan));
  check(hi - lo > 60 && hi - lo < 140, fmt("%s: height range %.1f .. %.1f m (relief %.1f m)", id.c_str(), lo, hi, hi - lo));
  check(maxS > 60 && n60 >= 40, fmt("%s: cliffs: steepest slope %.1f deg; %ld vertices over 60 deg, %ld over 50 deg (of %ld)", id.c_str(), maxS, n60, n50, nAll));
  // walkable route: 4-neighbour flood fill from the attackers' edge over steps no steeper than 45 deg (player limit 50)
  std::vector<uint8_t> seen((size_t)T.nx * T.nz, 0); std::vector<int> q;
  const float maxStep = std::tan(radians(45.0f)) * c;
  for (int j = 0; j < T.nz; ++j) for (int i = 0; i < T.nx; ++i)
    if (inB(i, j) && T.org.y + j * c < -T.buildHalfZ + 12) { seen[(size_t)j * T.nx + i] = 1; q.push_back(j * T.nx + i); }
  float reachHi = -1e9f; size_t walk = 0;
  for (size_t k = 0; k < q.size(); ++k) {
    int i = q[k] % T.nx, j = q[k] / T.nx; reachHi = std::max(reachHi, H(i, j)); ++walk;
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& e : d) {
      int a = i + e[0], b = j + e[1];
      if (a < 0 || b < 0 || a >= T.nx || b >= T.nz || !inB(a, b) || seen[(size_t)b * T.nx + a]) continue;
      if (std::fabs(H(a, b) - H(i, j)) > maxStep) continue;
      seen[(size_t)b * T.nx + a] = 1; q.push_back(b * T.nx + a);
    }
  }
  float lowland = 1e9f;
  for (int i = 0; i < T.nx; ++i) for (int j = 0; j < T.nz; ++j) if (inB(i, j) && T.org.y + j * c < -T.buildHalfZ + 300) lowland = std::min(lowland, H(i, j));
  check(reachHi > hi - 20 && reachHi - lowland > 60, fmt("%s: walkable route from the attackers' edge to the ridge top: highest reachable ground %.1f m (%.0f m above the lowland, top %.1f m); %.0f %% of the area reachable on foot",
                                                          id.c_str(), reachHi, reachHi - lowland, hi, 100.0 * walk / std::max<long>(1, nAll)));
  Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
  check(s.objectives.empty() && s.spawns.empty(), id + ": no scenario: no objectives, spawns or racks");
  // the empty creative copy saves and reopens with the authored ground
  float h0 = T.height(120, 400), h1 = T.height(-200, -600);
  fs::path p = userMapsDir() / ("selftest_" + id + ".nmap.json");
  bool ok = app.saveMapTo(p) && app.openMap(p);
  check(ok && std::fabs(app.doc.width - 850) < 1 && std::fabs(app.terrain.buildHalfZ * 2 - 1700) < 1, fmt("%s: reopened at its authored size %.0f x %.0f m", id.c_str(), app.terrain.buildHalfX * 2, app.terrain.buildHalfZ * 2));
  check(ok && app.doc.templateId == id && std::fabs(app.terrain.height(120, 400) - h0) < 0.01f && std::fabs(app.terrain.height(-200, -600) - h1) < 0.01f,
        fmt("%s: the editable copy saves and reopens on the authored terrain (%.2f / %.2f m)", id.c_str(), app.terrain.height(120, 400), app.terrain.height(-200, -600)));
  check(app.R.shaderErrors == 0, fmt("all shaders compiled, terrain slope material included (%d errors)", app.R.shaderErrors));
  sViews.clear(); templateViews(t);
  sViews.push_back({"overview", t.camPos, t.camYaw, t.camPitch, false, false});
  if (const char* only = std::getenv("NMK_MAPS_VIEW"))   // capture runs: only these views (comma list), Medium only
    std::erase_if(sViews, [&](const View& w) { return (std::string(",") + only + ",").find("," + w.name + ",") == std::string::npos; });
  sVi = 0; sPerf.clear(); sViewInit = false;
  sSum[id] = {{"heights", {lo, hi}}, {"maxSlopeDeg", maxS}, {"over60", n60}, {"over50", n50}, {"reachableTop", reachHi}, {"workingSetMB", sMemAfter}};
  check(sViews.size() >= 4, fmt("%s: %zu screenshot views", id.c_str(), sViews.size()));
  return true;
}

// A user's saved map promoted to a template (tools/mapgen/promote_map.py, "selftest": "authored"): it loads whole on
// the template ground, its scenario as authored (reported, not held to the SPEC 2.1 default-map bands), saves and
// reopens; views from the preview camera, first person at the explore start, and the template camera.
static size_t authoredCount(const App& app) { size_t n = authoredInstanceCount(app.doc.instances); for (auto& i : app.doc.instances) n -= i.extra.is_object() && i.extra.contains("lineGate"); return n; }   // V4: openMap / newMap add furniture pieces (extra.furnish); LN2 line gates are made by their lines
static size_t fileCount(const json& mj) { size_t n = 0; for (auto& j : mj.value("instances", json::array())) n += j.dump().find("\"lineGate\"") == std::string::npos; return n; }

void SelfTest::authoredChecks(App& app, const TemplateInfo& t, const json& mj) {
  const std::string id = t.id;
  size_t fileN = fileCount(mj);
  check(authoredCount(app) == fileN && fileN > 0, fmt("%s: all %zu authored instances loaded, %zu furniture pieces added (working set %+.0f MB)", id.c_str(), fileN, app.doc.instances.size() - authoredCount(app), sMemAfter - sMemBefore));
  size_t missing = 0; for (auto& i : app.doc.instances) if (!app.lib.find(i.assetId)) ++missing;
  check(missing == 0, fmt("%s: every instance names a catalogue asset (%zu missing)", id.c_str(), missing));
  check(std::fabs(app.doc.width - t.width) < 1 && std::fabs(app.terrain.buildHalfZ * 2 - t.depth) < 1, fmt("%s: map size %.0f x %.0f m", id.c_str(), app.doc.width, app.terrain.buildHalfZ * 2));
  json sj = mj.value("scenario", json::object());
  Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
  check(s.objectives.size() == sj.value("objectives", json::array()).size() && s.spawns.size() == sj.value("spawns", json::array()).size(),
        fmt("%s: scenario as authored: %zu objectives, %zu spawns", id.c_str(), s.objectives.size(), s.spawns.size()));
  if (!s.objectives.empty()) {
    auto issues = validateScenario(s, true);
    int errs = 0, warns = 0; std::string first;
    for (auto& i : issues) { if (i.error) { ++errs; if (first.empty()) first = i.what; } else ++warns; }
    check(errs == 0, fmt("%s: scenario validator: %d errors, %d warnings%s", id.c_str(), errs, warns, first.empty() ? "" : (" (" + first + ")").c_str()));
    int racks = 0; for (auto& sp : s.spawns) racks += app.doc.find(sp.rack) != nullptr;
    logf("maps %s: %d / %zu spawns have a weapons rack (reported only: the author's layout)", id.c_str(), racks, s.spawns.size());
  }
  size_t n = app.doc.instances.size();
  fs::path p = userMapsDir() / ("selftest_" + id + ".nmap.json");
  bool ok = app.saveMapTo(p) && app.openMap(p);
  Scenario s2 = Scenario::fromJson(app.doc.extra.value("scenario", json()));
  check(ok && app.doc.instances.size() == n && s2.objectives.size() == s.objectives.size() && s2.spawns.size() == s.spawns.size(),
        fmt("%s: the editable copy saves and reopens (%zu instances, %zu objectives)", id.c_str(), app.doc.instances.size(), s2.objectives.size()));
  sViews.clear();
  if (t.raw.contains("thumbnailCamera")) { json c = t.raw["thumbnailCamera"]; sViews.push_back({"preview", jvec3(jkey(c, "pos"), vec3(0)), c.value("yaw", 0.0f), c.value("pitch", 0.0f), false, false}); }
  if (app.doc.start.set) sViews.push_back({"explore_start", app.doc.start.pos, app.doc.start.yaw, 0.0f, true, true});
  sViews.push_back({"overview", t.camPos, t.camYaw, t.camPitch, false, false});
  sVi = 0; sPerf.clear(); sViewInit = false;
  sSum[id] = {{"instances", fileN}, {"objectives", s.objectives.size()}, {"spawns", s.spawns.size()}, {"workingSetMB", sMemAfter}};
}

void SelfTest::mapsUpdate(App& app, float dt) {
  (void)dt;
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  case 4100: {   // ---------------------------------------------------------------- setup
    reportName = "maps";
    gUi.showOverlay = false;   // screenshots and previews without the scenario overlay
    app.hideEditorOverlays = true;   // ... and without the build-area outline / editor gizmos (M1 review)
    for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--render-map-thumbs") thumbOnly = true;
    sTpl = mapTemplates(app); sTi = 0;
    if (const char* only = std::getenv("NMK_MAPS")) { std::string o = std::string(",") + only + ","; std::erase_if(sTpl, [&](int k) { return o.find("," + app.templates[k].id + ",") == std::string::npos; }); }   // measurement runs: only these map ids
    check(!sTpl.empty(), fmt("authored default-map templates found: %zu", sTpl.size()));
    if (sTpl.empty()) { finish(app); return; }
    { const char* ps = std::getenv("NMK_MAPS_PRESET"); std::string q = ps ? ps : "";   // WX2 captures: NMK_MAPS_PRESET=Low / High for the views
      settings().applyPreset(app, presetFromName(q)); }   // UL2: any preset name, Ultra included   // P1: every Medium view at the Medium preset (resolution 0.67 + TAAU), as after a Low / High pass
    if (const char* e = std::getenv("NMK_RES_SCALE")) app.R.setResolutionScale((float)std::atof(e));   // benchmarks at a chosen scale (Mac: 0.6)
    if (const char* e = std::getenv("NMK_UPSCALE")) settings().s.upscale = app.R.upscaleMode = e[0] == 's' ? 1 : e[0] == 'o' ? 2 : 0;
    go(4101);
    break;
  }
  case 4101: {   // ---------------------------------------------------------------- load one template + static checks
    if (sTi >= sTpl.size()) { go(4190); break; }
    const TemplateInfo& t = app.templates[sTpl[sTi]];
    if (thumbOnly) {
      navNewMapSized(app, t, t.name, vec2(t.width, t.depth));
      json c = t.raw.value("thumbnailCamera", t.raw.value("camera", json::object()));
      app.camPos = jvec3(jkey(c, "pos"), t.camPos); app.camYaw = c.value("yaw", t.camYaw); app.camPitch = c.value("pitch", t.camPitch);
      go(4180); break;
    }
    const std::string id = t.id;
    // "userMap": a map the user built in the editor and published as a template (e.g. battle1). It gets the generic
    // checks (loads, editable copy, catalogue, validator, save / reopen, views); the SPEC 2.1 / 3.5 layout rules of the
    // shipped default maps, the authored heightmap and the placed-height rule (free-hand placement) don't apply, and
    // its frame time is reported, not gated.
    const bool userMap = t.raw.value("userMap", false);
    if (userMap) check(t.available, id + ": user map template is available (generated terrain from its seed)");
    else if (t.raw.value("selftest", std::string()) == "authored") check(t.available && !t.raw.contains("heightmap"), id + ": template is available, on the procedural ground its map was built on (seed " + std::to_string(t.seed) + ")");
    else check(t.available && t.raw.contains("heightmap") && t.raw.contains("waterLevel"), id + ": template is available with an authored heightmap and water level");
    // Play / multiplayer listing (same filter as w5PlayMapPicker)
    // preview thumbnail
    fs::path th = contentDir() / "templates" / t.raw.value("thumbnail", std::string("-"));
    double bl = fs::exists(th) ? blankFraction(th) : 1.0;
    check(t.thumbnail != 0 && fs::exists(th) && bl < 0.95, fmt("%s: New Map preview rendered from the fixed camera (%s, %.0f %% background)", id.c_str(), th.filename().string().c_str(), bl * 100));
    // file content vs loaded document
    json mj; try { mj = json::parse(readTextFile(contentDir() / "templates" / t.raw.value("map", std::string()))); } catch (...) {}
    size_t fileN = fileCount(mj);
    const bool authored = t.raw.value("selftest", std::string()) == "authored";   // a user's saved map promoted to a template
    const bool terrainBase = !authored && !mj.contains("scenario");   // W5b: a terrain-only starting map (Mont-Castre): the user authors the rest
    if (authored) {
      bool objs = !mj.value("scenario", json::object()).value("objectives", json::array()).empty();
      check(t.raw.value("playable", false) == objs, id + (objs ? ": has objectives: listed in Play > Default maps" : ": no objectives: kept out of the Play > Default maps list"));
    } else if (terrainBase) check(!t.raw.value("playable", false), id + ": terrain base (no scenario): kept out of the Play > Default maps list");
    else check(t.raw.value("playable", false), id + ": listed in Play > setup > Default maps (playable)");
    sMemBefore = wsMB();
    double t0 = now();
    navNewMapSized(app, t, "Selftest " + t.name, vec2(t.width, t.depth));   // exactly the New Map "Create editable copy" path
    sLoadS = now() - t0; sMemAfter = wsMB();
    check(app.doc.templateId == id && app.docPath.empty(), id + ": Create editable copy gives an unsaved creative document from the template");
    if (authored) { authoredChecks(app, t, mj); go(4102); break; }
    if (terrainBase) { if (terrainBaseChecks(app, t, mj)) go(4102); else go(4101); break; }
    check(authoredCount(app) == fileN && fileN > (userMap ? 0u : 1000u), fmt("%s: all %zu authored instances loaded, %zu furniture pieces added (%.2f s, working set %+.0f MB)", id.c_str(), fileN, app.doc.instances.size() - authoredCount(app), sLoadS, sMemAfter - sMemBefore));
    check(std::fabs(app.doc.width - t.width) < 1 && std::fabs(app.terrain.buildHalfZ * 2 - t.depth) < 1, fmt("%s: map size %.0f x %.0f m", id.c_str(), app.doc.width, app.terrain.buildHalfZ * 2));
    size_t missing = 0; for (auto& i : app.doc.instances) if (!app.lib.find(i.assetId)) ++missing;
    check(missing == 0, fmt("%s: every instance names a catalogue asset (%zu missing)", id.c_str(), missing));
    // scenario
    Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
    auto issues = validateScenario(s, true);
    int errs = 0, warns = 0; std::string firstErr;
    for (auto& i : issues) { if (i.error) { ++errs; if (firstErr.empty()) firstErr = i.what; } else ++warns; }
    check(errs == 0, fmt("%s: scenario validator: %d errors, %d warnings%s", id.c_str(), errs, warns, firstErr.empty() ? "" : (" (" + firstErr + ")").c_str()));
    auto xz = [](vec3 p) { return vec2(p.x, p.z); };
    if (userMap) {
      int att = 0, def = 0; for (auto& sp : s.spawns) (sp.role == CombatRole::Attacker ? att : def)++;
      check(!s.objectives.empty() && att > 0 && def > 0, fmt("%s: user map has a playable scenario: %zu objectives, %d attacker / %d defender spawns", id.c_str(), s.objectives.size(), att, def));
    } else {
    check(s.objectives.size() == 5, fmt("%s: %zu objectives A-E in order", id.c_str(), s.objectives.size()));
    check(s.rules.defendingFaction == Faction::Axis, id + ": Allies attack, Germans defend");
    }
    for (size_t k = 0; k < (userMap ? 0 : s.objectives.size()); ++k) {
      const Objective& o = s.objectives[k];
      std::string L(1, o.letter());
      if (k > 0) { float d = glm::length(xz(o.pos) - xz(s.objectives[k - 1].pos)); check(d >= 450 && d <= 600, fmt("%s: %s-%s objective spacing %.0f m (SPEC 2.1: 450-600)", id.c_str(), std::string(1, s.objectives[k - 1].letter()).c_str(), L.c_str(), d)); }
      vec3 fwd = attackForward(o.attackYaw);
      if (k > 0) check(glm::dot(o.pos - s.objectives[k - 1].pos, vec3(0, 0, 1)) > 0, id + ": " + L + " lies further along the attack axis than its predecessor");
      (void)fwd;
      for (auto* sp : s.spawnsFor(CombatRole::Attacker, (int)k)) {
        float d = glm::length(xz(sp->pos) - xz(o.pos));
        bool ok = k == 0 ? d >= 300 : (d >= 300 && d <= 450);
        check(ok, fmt("%s: attacker spawn %s is %.0f m before %s (SPEC 2.1: 300-450%s)", id.c_str(), sp->label.c_str(), d, L.c_str(), k == 0 ? ", the beach spawn may be further" : ""));
      }
      for (auto* sp : s.spawnsFor(CombatRole::Defender, (int)k)) {
        float d = glm::length(xz(sp->pos) - xz(o.pos));
        check(d >= 250 && d <= 500 && glm::dot(sp->pos - o.pos, vec3(0, 0, 1)) > 0, fmt("%s: defender spawn %s is %.0f m behind %s (SPEC 2.1: 250-500)", id.c_str(), sp->label.c_str(), d, L.c_str()));
      }
      int mg = 0; for (auto& d : o.defenseSlots) mg += d.kind == SlotKind::MgNest;
      size_t gone = 0; for (auto lid : o.linkedStructures) gone += app.doc.find(lid) == nullptr;
      check(mg >= 3, fmt("%s: %s %s: %d MG-nest slots (SPEC 3.5: >= 3), %zu defense slots in all", id.c_str(), L.c_str(), o.label.c_str(), mg, o.defenseSlots.size()));
      check(!o.linkedStructures.empty() && gone == 0, fmt("%s: %s: %zu linked structures, all present", id.c_str(), L.c_str(), o.linkedStructures.size()));
    }
    int racks = 0;
    for (auto& sp : s.spawns) { const Instance* r = app.doc.find(sp.rack); const AssetDef* a = r ? app.lib.find(r->assetId) : nullptr; racks += a && a->rack && glm::length(xz(r->pos) - xz(sp.pos)) < 12; }
    if (!userMap) check(racks == (int)s.spawns.size() && s.spawns.size() == 10, fmt("%s: %d / %zu spawns have their team weapons rack beside them", id.c_str(), racks, s.spawns.size()));
    // terrain: sea, shingle, bluff and draw from the authored heightmap
    if (id == "omaha_easy_red") {
      auto H = [&](float x, float sAx) { return app.terrain.height(x, sAx - t.depth * 0.5f); };
      check(H(0, 20) < app.terrain.waterLevel - 0.3f && H(0, 300) > app.terrain.waterLevel, fmt("Omaha: sea at s 20 (%.1f m) below the water level, tidal flat above it", H(0, 20)));
      float rise = H(300, 860) - H(300, 560);
      check(rise > 30 && rise < 50, fmt("Omaha: bluff rises %.1f m (SPEC: 30-50 m)", rise));
      check(H(300, 700) - H(-25, 700) > 10, fmt("Omaha: the E-1 draw cuts the bluff (%.1f m below the bluff face at s 700)", H(300, 700) - H(-25, 700)));
    }
    // placed heights against the editor's placement rule (instances placed free-hand by the author are skipped)
    int tested = 0, agree = 0; float worst = 0;
    for (size_t k = 0; k < app.doc.instances.size(); k += 7) {
      const Instance& i = app.doc.instances[k];
      const AssetDef* a = app.lib.find(i.assetId);
      if (!a || a->path.on || a->rack || a->category == "Furniture" || i.assetId == "env_sea_shallows" || !a->navMeta.terrainCuts.empty()) continue;
      PlaceCheck c = app.checkPlacement(a, i.pos, i.yaw, i.id);
      if (!c.ok && (c.reason.find("water") != std::string::npos || c.reason.find("Outside") != std::string::npos)) continue;
      ++tested; float e = std::fabs(c.y - i.pos.y); worst = std::max(worst, e); agree += e < 0.25f;
    }
    if (!userMap) check(tested > 300 && agree >= tested * 0.9, fmt("%s: placed heights match the editor rule for %d / %d sampled instances (worst %.2f m)", id.c_str(), agree, tested, worst));
    // asset-usage doc covers the catalogue
    std::string doc; try { doc = readTextFile(fs::path(W5_REPO_DIR) / "docs" / "asset-usage-default-maps.md"); } catch (...) {}
    int nIds = 0, covered = 0; std::string miss;
    for (auto& a : app.lib.assets) { ++nIds; if (doc.find("`" + a->id + "`") != std::string::npos) ++covered; else if (miss.size() < 200) miss += " " + a->id; }
    check(!doc.empty() && covered == nIds, fmt("docs/asset-usage-default-maps.md covers %d / %d catalogue assets%s", covered, nIds, miss.c_str()));
    // views: A1 first person, then each objective from the attackers' side, then the overview
    sViews.clear();
    for (auto& sp : s.spawns) if (sp.role == CombatRole::Attacker && sp.activeIn(0)) { sViews.push_back({"A1_spawn", sp.pos, 180.0f, 2.0f, true}); break; }
    for (auto& o : s.objectives) {
      vec3 back = -attackForward(o.attackYaw);
      vec3 p = o.pos + back * 60.0f; p.y = std::max(app.terrain.height(p.x, p.z), o.pos.y) + 24.0f;   // above hedge crowns
      vec3 d = o.pos + vec3(0, 3, 0) - p;
      sViews.push_back({std::string("obj_") + o.letter(), p, degrees(std::atan2(-d.x, -d.z)), degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))), false, false});
      vec3 e = o.pos + back * 45.0f; e.y = app.terrain.height(e.x, e.z) + 1.7f;   // eye level on the attackers' approach
      sViews.push_back({std::string("obj_") + o.letter() + "_eye", e, o.attackYaw, 0.0f, false, true});
    }
    sViews.push_back({"overview", t.camPos, t.camYaw, t.camPitch, false, false});
    templateViews(t);
    if (const char* only = std::getenv("NMK_MAPS_VIEW"))   // NT5: one-view perf runs on generated maps too
      std::erase_if(sViews, [&](const View& w) { return (std::string(",") + only + ",").find("," + w.name + ",") == std::string::npos; });
    if (id == "omaha_easy_red") sViews.push_back({"bluff_top_to_beach", vec3(60, app.terrain.height(60, 820 - t.depth * 0.5f) + 3, 820 - t.depth * 0.5f), 0.0f, -6.0f, false});
    if (id == "omaha_easy_red") sViews.push_back({"plateau_bocage", vec3(-120, app.terrain.height(-120, 1300 - t.depth * 0.5f) + 2, 1300 - t.depth * 0.5f), 200.0f, -2.0f, false});
    sVi = 0; sPerf.clear(); sViewInit = false;
    sSum[id] = {{"instances", fileN}, {"loadSeconds", sLoadS}, {"workingSetMB", sMemAfter}, {"workingSetDeltaMB", sMemAfter - sMemBefore}, {"validatorWarnings", warns}};
    // editable like any map: move an instance (undoable), save, reopen
    {
      Instance* r = nullptr; for (auto& i : app.doc.instances) if (i.assetId == "def_tank_trap") { r = &i; break; }
      if (r) { app.pushUndo(); r = app.doc.find(r->id); vec3 o = r->pos; r->pos.x += 3; app.bump(*r); check(app.dirty && app.doc.find(r->id)->pos.x == o.x + 3, id + ": authored instances are ordinary editable placements"); }
      fs::path p = userMapsDir() / ("selftest_" + id + ".nmap.json");
      size_t n = app.doc.instances.size();
      bool ok = app.saveMapTo(p);
      ok = ok && app.openMap(p);
      Scenario s2 = Scenario::fromJson(app.doc.extra.value("scenario", json()));
      check(ok && app.doc.instances.size() == n && s2.objectives.size() == s.objectives.size() && s2.spawns.size() == s.spawns.size(),
            fmt("%s: the editable copy saves and reopens (%zu instances, %zu objectives)", id.c_str(), app.doc.instances.size(), s2.objectives.size()));
    }
    go(4102);
    break;
  }
  case 4102: {   // ---------------------------------------------------------------- views: settle, measure, shoot
    if (sVi >= sViews.size()) {
      if (app.mode == Mode::Explore) app.exitExplore();
      json pj = json::array(); double worstAvg = 0, worstEd = 0, worstPre[3] = {0, 0, 0}, worstNight = 0;
      for (auto& p : sPerf) {
        pj.push_back({{"view", p.name}, {"gameplay", p.gate}, {"preset", presetName((GfxPreset)p.preset)}, {"night", p.night}, {"frames", p.frames}, {"avgMs", p.ms}, {"worstMs", p.worst}, {"draws", p.draws}, {"tris", p.tris}, {"gpuMs", p.gpu}, {"gpuPassMs", passJson(p.pass)}, {"x", xJson(p.x)}});
        if (p.abN[0] && p.abN[1]) pj.back()["ab"] = {{"frames", {p.abN[0], p.abN[1]}}, {"frameMs", {p.abMs[0], p.abMs[1]}}, {"gpuPassMs", {passJson(p.abPass[0]), passJson(p.abPass[1])}}, {"probeCpuMs", {p.abProbeCpu[0], p.abProbeCpu[1]}}, {"probesPerFrame", {p.abProbes[0], p.abProbes[1]}}, {"cpuMs", {p.abCpu[0], p.abCpu[1]}}, {"x", {xJson(p.abX[0]), xJson(p.abX[1])}}};
        if (p.night) { worstNight = std::max(worstNight, p.ms); continue; }   // L2: Night is reported, never gated
        if (p.gate) worstPre[p.preset] = std::max(worstPre[p.preset], p.ms);
        if (p.preset != (int)GfxPreset::Medium) continue;   // Low / High: reported below, never gated
        (p.gate ? worstAvg : worstEd) = std::max(p.gate ? worstAvg : worstEd, p.ms);
      }
      const std::string id = app.templates[sTpl[sTi]].id;
      sSum[id]["views"] = pj; sSum[id]["workingSetEndMB"] = wsMB();
      if (app.templates[sTpl[sTi]].raw.value("userMap", false))   // user-built map: frame time reported, not gated
        check(true, fmt("%s: user map frame time (reported only): worst eye-level view %.2f ms, worst editor view %.2f ms", id.c_str(), worstAvg, worstEd));
      else check(worstAvg < 16.7, fmt("%s: every gameplay (eye-level) view holds the 60 FPS target: worst view average %.2f ms (elevated editor views, reported only: worst %.2f ms)", id.c_str(), worstAvg, worstEd));
      check(true, fmt("%s: graphics presets, worst eye-level view (reported; only Medium is gated): Low %.2f ms, Medium %.2f ms, High %.2f ms", id.c_str(), worstPre[0], worstPre[1], worstPre[2]));
      sSum[id]["presetWorstMs"] = {{"Low", worstPre[0]}, {"Medium", worstPre[1]}, {"High", worstPre[2]}, {"MediumNight", worstNight}};
      check(true, fmt("%s: Night at Medium (L2 time of day, reported only): worst eye-level view %.2f ms", id.c_str(), worstNight));
      ++sTi; go(4101); break;
    }
    View& v = sViews[sVi];
    if (!sViewInit) {
      sViewInit = true;
      if (v.hour >= 0.0f) { app.R.lt.hour = v.hour; app.R.lt.userHour = true; } else app.R.lt.userHour = false;   // GR3: a night view among day ones (userHour: the map's fixed hour does not take it back)
      app.R.lt.neutralLook = v.neutral;
      app.R.dbgTerr = v.dbg; app.R.ggOff = v.ggOff;   // GG1 measurement views
      if (v.groundEye) { v.pos.y += app.terrain.height(v.pos.x, v.pos.z); v.groundEye = false; }
      if (v.weather >= 0) { app.R.wx.target = v.weather; app.R.wx.userSet = true; app.R.wx.snap(); }   // SKY1: a sky under a chosen weather
      if (v.cover >= 0.0f) app.R.cloudCover = v.cover;
      if (v.light >= 0) { settings().s.lightNew = v.light != 0; app.R.q.lightNew = v.light != 0; app.R.resetTemporal(); }   // VO: New beside Classic in one run
      if (v.explore) {
        app.doc.start = {true, v.pos, v.yaw};
        bool ok = app.enterExplore();
        check(ok && app.mode == Mode::Explore, "first person at the A1 spawn (" + std::string(ok ? "entered" : "refused") + ")");
      } else {
        if (app.mode == Mode::Explore) app.exitExplore();
        app.camPos = v.pos; app.camYaw = v.yaw; app.camPitch = v.pitch;
      }
      sPerf.push_back({v.name, v.gate}); sLast = 0; gFlPh = 0; gFxPh = 0; gLdPh = 0; gWxPh = 0; gWalkN = 0; gWalk0 = app.camPos;
      logf("maps view %s at (%.1f %.1f %.1f) yaw %.1f pitch %.1f", v.name.c_str(), v.pos.x, v.pos.y, v.pos.z, v.yaw, v.pitch);
    }
    if (v.explore && app.mode == Mode::Explore) { app.useScripted = true; }
    double tnow = now();
    // PF2 interleaved A/B (NMK_PERF_AB=all | <Renderer::perf switch>): 2..6 s (+ NMK_SETTLE) in 0.4 s blocks, alternating the
    // baseline (even blocks: pre-PF2 renderer, or that switch off) and the PF2 renderer, so other GPU load hits both sides alike.
    // The first 5 frames of a block are skipped (the pass timers lag 2-3 frames; caches refill).
    static const char* abEnv = std::getenv("NMK_PERF_AB");
    static const float abT0 = 2.0f + (std::getenv("NMK_SETTLE") ? (float)std::atof(std::getenv("NMK_SETTLE")) : 0.0f);   // NMK_SETTLE=<s>: A/B starts later (converged probe GI)
    const float measEnd = abEnv ? abT0 + 4.0f : 4.0f;
    if (abEnv && sLast > 0) {
      static int lastBlk = -1, blkFrames = 0;
      int blk = stageTime < abT0 ? -1 : std::min(9, (int)((stageTime - abT0) / 0.4f));
      Renderer::Perf A, B = std::string(abEnv) == "all" ? Renderer::Perf::legacy() : A;
      if (std::string(abEnv) != "all")   // one switch, or a comma list (R1: the combination)
        for (std::string l = abEnv; !l.empty();) { size_t c = l.find(','); std::string n = l.substr(0, c); A.set(n, true); B.set(n, false); l = c == std::string::npos ? "" : l.substr(c + 1); }
      bool base = blk >= 0 && blk % 2 == 0;
      if (blk != lastBlk) { lastBlk = blk; blkFrames = 0; app.R.perf = base ? B : A; }
      if (blk >= 0 && stageTime < measEnd && ++blkFrames > 5) {
        Perf& p = sPerf.back(); int s = base ? 1 : 0; double ms = (tnow - sLast) * 1000.0;
        p.abMs[s] = (p.abMs[s] * p.abN[s] + ms) / (p.abN[s] + 1);
        for (int i = 0; i < GP_Count; ++i) p.abPass[s][i] = (p.abPass[s][i] * p.abN[s] + app.R.gpuPassMs[i]) / (p.abN[s] + 1);
        p.abProbeCpu[s] = (p.abProbeCpu[s] * p.abN[s] + app.R.stats.probeCpuMs) / (p.abN[s] + 1);
        p.abProbes[s] = (p.abProbes[s] * p.abN[s] + app.R.stats.probes) / (p.abN[s] + 1);
        sampleX(app, p.abX[s], p.abN[s]);
        p.abCpu[s] = (p.abCpu[s] * p.abN[s] + app.R.stats.cpuMs) / (p.abN[s] + 1);   // R1: render() CPU time
        ++p.abN[s];
      }
    }
    if (stageTime > 2.0f && stageTime < 4.0f && sLast > 0 && !abEnv) {   // streaming settled: measure wall-clock frame time
      Perf& p = sPerf.back(); double ms = (tnow - sLast) * 1000.0;
      p.ms = (p.ms * p.frames + ms) / (p.frames + 1); p.worst = std::max(p.worst, ms);
      p.draws = (p.draws * p.frames + app.R.stats.draws) / (p.frames + 1); p.tris = (p.tris * p.frames + app.R.stats.tris) / (p.frames + 1);
      double g = 0; for (int i = 0; i < GP_Count; ++i) { p.pass[i] = (p.pass[i] * p.frames + app.R.gpuPassMs[i]) / (p.frames + 1); g += app.R.gpuPassMs[i]; }
      p.gpu = (p.gpu * p.frames + g) / (p.frames + 1); sampleX(app, p.x, p.frames); ++p.frames;
    }
    sLast = tnow;
    if (v.walk > 0.0f && !v.explore && sPresetPass == 0 && stageTime >= measEnd) {   // VO motion check: a steady walk, frames to selftest_out/walk_<map>_<view>_NN.png
      float t = stageTime - measEnd;
      if (t < v.walk) {
        vec3 f(-std::sin(radians(app.camYaw)), 0.0f, -std::cos(radians(app.camYaw)));
        vec3 p = gWalk0 + f * 1.4f * t; p.y = app.terrain.height(p.x, p.z) + (gWalk0.y - app.terrain.height(gWalk0.x, gWalk0.z));
        app.camPos = p;
        if (t >= gWalkN * 0.5f) { char b[16]; std::snprintf(b, sizeof b, "_%02d", gWalkN++); shotQueue.push_back("walk_" + app.templates[sTpl[sTi]].id + "_" + v.name + b); }
        break;
      }
    }
    if (kFl1 && sPresetPass == 0 && stageTime >= measEnd && gFlPh < 6) {   // FL1 (see fl1Analyse)
      std::string map = app.templates[sTpl[sTi]].id, base = "fl1_" + map + "_" + v.name;
      if (gFlPh == 0) { app.R.holdTime = !kFl1Wind; gFlT = stageTime; gFlPh = 1; }
      else if (gFlPh == 1 && stageTime > gFlT + 1.2f) { shotQueue.push_back(base + "_a"); gFlState = json::array({{app.R.exposure(), app.R.stats.probePending, app.R.stats.probeDirty, app.eyePos().x, app.eyePos().y, app.eyePos().z, app.eyeYaw, app.eyePitch}}); gFlPh = 2; gFlT = stageTime; }
      else if (gFlPh == 2 && stageTime > gFlT + (kFl1Wind ? 0.0f : 0.3f)) { shotQueue.push_back(base + "_b"); gFlState.push_back({app.R.exposure(), app.R.stats.probePending, app.R.stats.probeDirty, app.eyePos().x, app.eyePos().y, app.eyePos().z, app.eyeYaw, app.eyePitch}); gFlPh = 3; gFlT = stageTime; }
      else if (gFlPh == 3 && stageTime > gFlT + 0.1f) {   // 5 cm to the side (editor camera; a first-person view stays put)
        gFlCam = app.camPos; if (!v.explore) app.camPos += vec3(std::cos(radians(app.camYaw)), 0, -std::sin(radians(app.camYaw))) * 0.05f;
        gFlPh = 4; gFlT = stageTime; }
      else if (gFlPh == 4 && stageTime > gFlT + 1.2f) { shotQueue.push_back(base + "_c"); gFlPh = 5; gFlT = stageTime; }
      else if (gFlPh == 5 && stageTime > gFlT + 0.1f) { check(true, fl1Analyse(app, map, v.name, base)); if (!v.explore) app.camPos = gFlCam; app.R.holdTime = false; gFlPh = 6; }
      break;
    }
    if (kFx1 && sPresetPass == 0 && stageTime >= measEnd && gFxPh < 9) {   // FX1 (see above)
      std::string base = "fx1_" + app.templates[sTpl[sTi]].id + "_" + v.name;
      double tn = now(); int side = app.flashlight ? 1 : 0;
      if ((gFxPh == 1 || gFxPh == 3) && stageTime > gFxT + 0.3f && gFxLast > 0) { gFxMs[side] += (tn - gFxLast) * 1000.0; ++gFxN[side]; }
      gFxLast = tn;
      auto next = [&](float wait) { if (stageTime < gFxT + wait) return false; gFxT = stageTime; ++gFxPh; return true; };
      if (gFxPh == 0) { app.flashlight = false; gFxMs[0] = gFxMs[1] = 0; gFxN[0] = gFxN[1] = 0; gFxT = stageTime; gFxPh = 1; }
      else if (gFxPh == 1) { if (next(1.3f)) shotQueue.push_back(base + "_off"); }
      else if (gFxPh == 2) { app.flashlight = true; gFxT = stageTime; gFxPh = 3; }
      else if (gFxPh == 3) { if (next(1.5f)) shotQueue.push_back(base + "_on"); }
      else if (gFxPh == 4) { if (!v.explore) gFxPh = 8; else { app.scriptedInput.crouch = true; gFxT = stageTime; gFxPh = 5; } }
      else if (gFxPh == 5) { if (next(1.2f)) shotQueue.push_back(base + "_on_crouch"); }
      else if (gFxPh == 6) { app.scriptedInput.crouch = false; app.scriptedInput.aim = true; gFxT = stageTime; gFxPh = 7; }
      else if (gFxPh == 7) { if (next(1.2f)) { shotQueue.push_back(base + "_on_ads"); } }
      else if (gFxPh == 8) {
        app.scriptedInput.aim = false; app.flashlight = false; gFxPh = 9;
        check(true, fmt("FX1 %s %s: frame %.2f ms headlamp off (%d frames), %.2f ms on (%d frames)", app.templates[sTpl[sTi]].id.c_str(), v.name.c_str(),
                        gFxN[0] ? gFxMs[0] / gFxN[0] : 0.0, gFxN[0], gFxN[1] ? gFxMs[1] / gFxN[1] : 0.0, gFxN[1]));
      }
      break;
    }
    if (kWxCycle && v.gate && sPresetPass == 0 && stageTime >= measEnd) {   // W1 preset cycle (see kWxCycle)
      const int np = wxPresetCount(), nAll = np + 5;   // presets, Map default, Classic Clear / Overcast, Physical Clear / Overcast
      std::string base = "wx_" + app.templates[sTpl[sTi]].id + "_" + v.name;
      auto setK = [&](int k) {
        bool classic = k == np + 1 || k == np + 2, phys = k >= np + 3; settings().s.lightNew = !classic; app.R.q.lightNew = !classic; app.R.q.lightPhys = phys;
        if (k < np) weatherForce(app.R, wxPresetIdAt(k)); else if (k == np) { app.R.wx.userSet = false; app.R.wx.snap(); } else weatherForce(app.R, (k - np) % 2 == 1 ? 0 : 1);
        app.R.resetTemporal();
      };
      if (gWxPh == 0) { setK(0); gWxT = stageTime; gWxPh = 1; }
      else if (gWxPh <= nAll && stageTime >= gWxT + 1.6f && gWxT >= 0.0f) {   // the shot is taken after this frame renders: switch on the next update
        char b[16]; std::snprintf(b, sizeof b, "_%02d", gWxPh - 1); shotQueue.push_back(base + b);
        gWxT = -1.0f;
      } else if (gWxPh <= nAll && gWxT < 0.0f) {
        if (gWxPh < nAll) setK(gWxPh);
        gWxT = stageTime; ++gWxPh;
      } else if (gWxPh == nAll + 1 && stageTime >= gWxT + 0.2f) {
        for (int k = 0; k < nAll; ++k) {
          char b[16]; std::snprintf(b, sizeof b, "_%02d", k); int w = 0, h = 0, n = 0;
          unsigned char* I = stbi_load((outDirM() / (base + b + ".png")).string().c_str(), &w, &h, &n, 3);
          if (!I) { check(false, "W1 preset cycle: shot missing " + base + b); continue; }
          double L = 0; long blk = 0, clp = 0;
          for (long q = 0; q < (long)w * h; ++q) { int r = I[q * 3], g = I[q * 3 + 1], bb = I[q * 3 + 2]; L += 0.2126 * r + 0.7152 * g + 0.0722 * bb; int mx = std::max({r, g, bb}); blk += mx <= 3; clp += std::min({r, g, bb}) >= 252; }
          stbi_image_free(I);
          L /= (double)w * h; double bp = 100.0 * blk / ((double)w * h), cp = 100.0 * clp / ((double)w * h);
          const char* nm = k < np ? wxPresetName(wxPresetIdAt(k)) : k == np ? "Map default" : k == np + 1 ? "Classic Clear" : k == np + 2 ? "Classic Overcast" : k == np + 3 ? "Physical Clear" : "Physical Overcast";
          check(L > 8.0 && L < 235.0 && bp < 60.0 && cp < 60.0, fmt("W1 %s %s: %s: mean sRGB %.0f, black %.1f %%, white %.1f %% -> %s%s.png", app.templates[sTpl[sTi]].id.c_str(), v.name.c_str(), nm, L, bp, cp, base.c_str(), b));
        }
        settings().s.lightNew = true; app.R.q.lightNew = true; app.R.q.lightPhys = false; app.R.wx.userSet = false; app.R.wx.snap(); ++gWxPh;
      }
      if (gWxPh <= nAll + 1) break;
    }
    if (ldRun() && v.gate && sPresetPass == 0 && stageTime >= measEnd && gLdPh < 10) {   // CP0 look-dev readout (see ldAnalyse)
      std::string map = app.templates[sTpl[sTi]].id, base = "ld_" + map + "_" + v.name;
      Renderer& R = app.R;
      auto wait = [&](float s) { return stageTime >= gLdT + s; };
      auto next = [&] { gLdT = stageTime; ++gLdPh; };
      auto probe = [&] { R.ldProbe.pts = gLdCards; R.ldProbe.want = true; };
      switch (gLdPh) {
        case 0: shotQueue.push_back(base); next(); break;   // the view as played
        case 1: if (wait(0.1f)) { extern void lookdevPlace(Renderer&, vec3, float, const std::function<float(float, float)>&, std::vector<vec3>*);
                  lookdevPlace(R, v.explore ? app.eyePos() : app.camPos, v.explore ? app.eyeYaw : app.camYaw, [&](float x, float z) { return app.terrain.height(x, z); }, &gLdCards);
                  R.resetTemporal(); next(); } break;
        case 2: if (wait(1.2f)) { shotQueue.push_back(base + "_props"); probe(); next(); } break;
        case 3: gLdPx = R.ldProbe.px; gLdExpo = R.ldProbe.expo; R.dbgView = 2; R.resetTemporal(); next(); break;
        case 4: if (wait(0.8f)) { shotQueue.push_back(base + "_illum"); probe(); next(); } break;
        case 5: gLdIll = R.ldProbe.hdr; R.dbgView = 6; R.resetTemporal(); next(); break;
        case 6: if (wait(0.8f)) { shotQueue.push_back(base + "_sky"); probe(); next(); } break;
        case 7: gLdSky = R.ldProbe.hdr; R.dbgView = 1; next(); break;
        case 8: if (wait(0.3f)) { shotQueue.push_back(base + "_lum"); next(); } break;
        case 9: if (wait(0.1f)) { R.dbgView = 0; R.lookdev.clear(); R.resetTemporal(); check(true, ldAnalyse(app, map, v.name, base)); next(); } break;
      }
      break;
    }
    if (stageTime >= measEnd) {
      Perf& p = sPerf.back();
      if (abEnv) { app.R.perf = Renderer::Perf(); if (std::string(abEnv) != "all") app.R.perf.set(abEnv, true); p.frames = p.abN[0]; p.ms = p.abMs[0]; p.gpu = 0; for (int i = 0; i < GP_Count; ++i) { p.pass[i] = p.abPass[0][i]; p.gpu += p.pass[i]; }
        check(true, fmt("A/B %s, view %s: PF2 %.2f ms (%d frames) vs baseline %.2f ms (%d frames)", abEnv, v.name.c_str(), p.abMs[0], p.abN[0], p.abMs[1], p.abN[1])); }
      std::string nm = "maps_" + app.templates[sTpl[sTi]].id + "_" + v.name + (sPresetPass == 1 ? "_low" : sPresetPass == 2 ? "_high" : sPresetPass == 3 ? "_night" : "");
      shotQueue.push_back(nm);
      check(p.frames > 10, fmt("%s view %s%s: %.2f ms avg (worst %.1f) over %d frames, %.0f draws, %.1f M tris, GPU %.2f ms, sun direct %.0f %%, %d sun-cascade draws, shimmer %.2f px, exposure %.2f%s, %zu lights -> selftest_out/%s.png", v.gate ? "gameplay" : "editor", v.name.c_str(), sPresetPass ? (sPresetPass == 1 ? " at Low" : sPresetPass == 2 ? " at High" : " at Night (Medium)") : "", p.ms, p.worst, p.frames, p.draws, p.tris / 1e6, p.gpu, app.R.stats.sunDirectPct, app.R.stats.shadowDraws, app.R.shimmerPx, app.R.exposure(), app.R.camIndoor ? " (indoors)" : "", app.R.lights.size(), nm.c_str()));
      { static bool once = false; if (!once) { once = true; check(app.R.shaderErrors == 0, fmt("all shaders compiled, terrain variants included (%d errors)", app.R.shaderErrors)); } }   // PF: a missing terrain variant fails here
      {   // PF terrain: the pass time, what it drew per LOD ring, the fine tiles and the share of its shaded fragments that wrote a sample
        const auto& st = app.terrain.stream.stats; const auto& fs = app.R.stats;
        logf("[terrain] %s %s: pass %.2f ms; chunks %d (%d with the discarding shader) per ring %d/%d/%d/%d/%d, tris %.2fM (%.2f/%.2f/%.2f/%.2f/%.2f M); fine tiles %d drawn (lod %d/%d/%d), %d culled; samples written %.2fM",
             app.templates[sTpl[sTi]].id.c_str(), nm.c_str(), p.pass[GP_Terrain], st.drawnLastFrame, st.drawnMasked, st.drawnLod[0], st.drawnLod[1], st.drawnLod[2], st.drawnLod[3], st.drawnLod[4],
             st.tris / 1e6, st.trisLod[0] / 1e6, st.trisLod[1] / 1e6, st.trisLod[2] / 1e6, st.trisLod[3] / 1e6, st.trisLod[4] / 1e6, app.terrain.tileStats.drawn, app.terrain.tileStats.lod[0], app.terrain.tileStats.lod[1], app.terrain.tileStats.lod[2], app.terrain.tileStats.culled,
             fs.terrSamples / 1e6);
      }
      if (v.gate && sPresetPass < 3 && !std::getenv("NMK_MAPS_VIEW") && !kFl1 && !kFx1 && !ldRun() && !kWxCycle) {   // V2 (docs/settings.md): the same gameplay view again at Low, then High; L2: then Night at Medium
        ++sPresetPass; GfxPreset ps = sPresetPass == 1 ? GfxPreset::Low : sPresetPass == 2 ? GfxPreset::High : GfxPreset::Medium;
        settings().applyPreset(app, ps); app.R.resetTemporal();
        if (sPresetPass == 3) app.R.lt.hour = 0.0f;   // Night (docs/lighting.md)
        Perf q; q.name = v.name + (sPresetPass == 1 ? "@low" : sPresetPass == 2 ? "@high" : "@night"); q.gate = true; q.preset = (int)ps; q.night = sPresetPass == 3; sPerf.push_back(q);
        stageTime = 0; sLast = 0; break;
      }
      if (sPresetPass) {   // back to Medium: the next view gets 2 s more to settle (grass / targets rebuild from High)
        settings().applyPreset(app, GfxPreset::Medium); app.R.lt.hour = 14.0f; sPresetPass = 0;   // back to Day
        ++sVi; stageTime = -2.0; sViewInit = false; break;
      }
      ++sVi; stageTime = 0; sViewInit = false;
    }
    break;
  }
  case 4180: {   // ---------------------------------------------------------------- --render-map-thumbs
    if (stageTime > 3.0f) {
      const TemplateInfo& t = app.templates[sTpl[sTi]];
      fs::path p = outDirM() / (t.id + ".thumb.png");
      check(writeThumb(app, p), "preview written: selftest_out/" + p.filename().string() + " (copy to content/templates/)");
      ++sTi; go(4101);
    }
    break;
  }
  case 4190: {
    std::ofstream(outDirM() / "summary_maps.json") << sSum.dump(2);
    finish(app);
    break;
  }
  default: finish(app); break;
  }
}
