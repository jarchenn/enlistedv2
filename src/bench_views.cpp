// --bench-views: the performance benchmark (P1, hardened for CP step 0, docs/corpus-implementation.md; corpus 01 s3, s7-8).
// Run it through the script (one process per map, a crashed or shared run retried once, the card held exclusively):
//   bash tools/bench_views.sh <build dir> [tag]   -> docs/perf/bench_<tag>.md (+ .jsonl, + per-frame CSVs in selftest_out)
// Kinds (NMK_BENCH_KIND):
//   path   (default) a real-time camera path per map: a closed loop through the densest content at NMK_BENCH_PATH_H m
//          (default 9) over the ground, 40 s, then a 360 deg turn in 8 s at eye height at the street spot. One untimed
//          warm-up lap (shaders, streaming, probe GI; fragment counts are taken here), then NMK_BENCH_LAPS timed laps (3).
//   bots   one 100-bot match (NMK_BENCH_BOTS, default 100) on the map given; the player stands at the attacker spawn turning
//          360 deg every 20 s; 15 s warm-up, then the same number of laps.
//   views  the P1 static views (street, foliage, overview), NMK_BENCH_SETTLE / NMK_BENCH_MEASURE s; NMK_BENCH_SHOTS=1 adds
//          a still and a frame 0.5 s into a 20 deg/s pan per view (A/B sheets).
// Per run (map x preset): GPU (per-pass timestamps summed), CPU frame (wall clock), CPU without the time blocked in swap,
// swap: P50 / P95 / P99 / P99.9 / max; per-lap GPU P99; hitches (frames over 1.5x the moving median, hitch_log.cpp; over
// 33.3 and 50 ms); per-pass mean ms and fragment invocations per internal pixel; draws, triangles, probes; internal
// resolution. Window: NMK_BENCH_RES (default 2560x1440, borderless, app.cpp). Dynamic resolution, vsync and the FPS cap
// are off. NMK_BENCH_MAPS / NMK_BENCH_PRESETS narrow a run; NMK_BENCH_LIGHTING=classic, NMK_BENCH_MSAA=0|4 and
// NMK_BENCH_RQOFF=a,b (Renderer::perf switches) are A/B levers. Paths and views come from content/settings/bench_views.json
// ("maps": views, "paths": loops); a map missing there gets derived ones, appended to selftest_out/bench_views_derived.jsonl.
#include "selftest.h"
#include "settings.h"
#include "physics.h"
#include "hitch_log.h"
#include "game.h"
#include "mpui.h"
#include "editor_nav.h"
#include <GLFW/glfw3.h>
#include <array>
#include <fstream>
#include <algorithm>
#include <numeric>

namespace {
struct BView { std::string map, name; vec3 pos; float yaw = 0, pitch = 0; };
struct BPath { std::vector<vec2> pts; float height = 9; vec3 turn{0}; float turnYaw = 0; float len = 0; };
struct Run {
  std::string map, view, preset;
  std::vector<double> gpu, cpu, cpuNoSwap, swap, rcpu, draws, tris, probes; std::vector<int> lap, hitch;
  std::vector<std::array<double, GP_Count>> pass;
  std::array<double, GP_Count> frag{}; int fragN = 0;   // fragment invocations (millions), summed over the warm-up lap
  int iw = 0, ih = 0;
};
const char* kMaps[] = {"chateau_town", "newtown", "outpost_woods", "dday", "sd_town"};
std::vector<std::string> gMaps; std::vector<GfxPreset> gPresets;
std::vector<BView> gViews; BPath gPath; json gDerived = json::object();
std::vector<Run> gRuns;
std::string gTag = std::getenv("NMK_BENCH_TAG") ? std::getenv("NMK_BENCH_TAG") : "run";
std::string gKind = std::getenv("NMK_BENCH_KIND") ? std::getenv("NMK_BENCH_KIND") : "path";
std::vector<std::string> gCfgs{""}; size_t gCi = 0;   // NMK_BENCH_AB: knock-out configs, innermost loop
size_t gMi = 0, gPi = 0, gVi = 0; int gPhase = 0; double gT0 = 0, gLast = 0; bool gFirstView = true;
int gW = 2560, gH = 1440;
double kPathS = 40.0; constexpr double kTurnS = 8.0, kBotLapS = 20.0, kBotWarmS = 15.0;

double nowS() { return glfwGetTime(); }
float envF(const char* n, float d) { const char* v = std::getenv(n); return v && *v ? (float)std::atof(v) : d; }
std::vector<std::string> envList(const char* n) { std::vector<std::string> o; const char* v = std::getenv(n); if (!v) return o; std::string s = v;
  for (size_t a = 0; a <= s.size();) { size_t c = s.find(',', a); if (c == std::string::npos) c = s.size(); if (c > a) o.push_back(s.substr(a, c - a)); a = c + 1; } return o; }
double pct(std::vector<double> v, double q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)std::floor(q * (v.size() - 1) + 0.5))]; }
double mean(const std::vector<double>& v) { return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
int laps() { return std::max(1, (int)envF("NMK_BENCH_LAPS", 3)); }

// an open spot to stand near c (a capsule fits), spiralling out; the eye 1.7 m up looking at c
bool standNear(App& a, vec2 c, float r0, BView& v) {
  for (int k = 0; k < 160; ++k) {
    float ang = k * 2.39996f, r = r0 + 0.35f * k;
    vec2 p = c + r * vec2(std::cos(ang), std::sin(ang));
    if (!a.terrain.inBuildArea(p.x, p.y)) continue;
    vec3 f(p.x, a.terrain.height(p.x, p.y), p.y);
    if (f.y < a.terrain.waterLevel + 0.1f || !a.startMarkerValid(f)) continue;
    v.pos = f + vec3(0, 1.7f, 0); vec2 d = c - p; v.yaw = degrees(std::atan2(-d.x, -d.y)); v.pitch = -2.0f;
    return true;
  }
  return false;
}
std::vector<vec2> pointsOf(App& a, bool (*want)(const AssetDef*)) {
  std::vector<vec2> pts; for (auto& i : a.doc.instances) if (const AssetDef* d = a.assetFor(i); d && want(d)) pts.push_back(vec2(i.pos.x, i.pos.z));
  return pts;
}
// the densest spot of pts (count within 30 m), skipping spots within `away` of any in `no`
bool densestOf(const std::vector<vec2>& pts, vec2& c, const std::vector<vec2>& no = {}, float away = 0) {
  size_t step = std::max<size_t>(1, pts.size() / 600); int best = -1;   // candidates: up to 600 of them
  for (size_t k = 0; k < pts.size(); k += step) {
    if (std::any_of(no.begin(), no.end(), [&](vec2 q) { return glm::length(q - pts[k]) < away; })) continue;
    int n = 0; for (auto& q : pts) n += glm::length(q - pts[k]) < 30.0f; if (n > best) { best = n; c = pts[k]; }
  }
  return best >= 0;
}
bool isBuilding(const AssetDef* d) { return d->enterable; }
bool isGreen(const AssetDef* d) { return d->tree.on || d->nature.kind != NatureDef::NONE || d->category == "Trees" || d->category == "Bushes" || d->category == "Nature"; }
bool isAny(const AssetDef*) { return true; }

json loadViewsFile() {   // NMK_BENCH_VIEWFILE: another views file (a session's own bookmarks)
  json all = json::object(); const char* e = std::getenv("NMK_BENCH_VIEWFILE");
  try { std::ifstream f(e && *e ? fs::path(e) : contentDir() / "settings" / "bench_views.json"); if (f) all = json::parse(f); } catch (...) {}
  return all;
}

void viewsFor(App& a, const std::string& map, const TemplateInfo& t) {
  json all = loadViewsFile();
  json mv = all.value("maps", json::object()).value(map, json::array());
  if (mv.is_array() && !mv.empty()) {
    for (auto& v : mv) gViews.push_back({map, v.value("name", std::string("view")), jvec3(v["pos"], vec3(0)), v.value("yaw", 0.0f), v.value("pitch", 0.0f)});
    return;
  }
  BView s{map, "street"}, g{map, "foliage"}, o{map, "overview", t.camPos, t.camYaw, t.camPitch};
  vec2 c;
  if (densestOf(pointsOf(a, isBuilding), c)) {
    if (standNear(a, c, 9.0f, s)) gViews.push_back(s);
    o.yaw = 35.0f; o.pitch = -32.0f;   // overview: 110 m back and 70 m above the densest buildings, three-quarter
    o.pos = vec3(c.x + std::sin(radians(o.yaw)) * 110.0f, a.terrain.height(c.x, c.y) + 70.0f, c.y + std::cos(radians(o.yaw)) * 110.0f);
  }
  if (densestOf(pointsOf(a, isGreen), c) && standNear(a, c, 4.0f, g)) gViews.push_back(g);
  gViews.push_back(o);
  json arr = json::array();
  for (auto& v : gViews) if (v.map == map) arr.push_back({{"name", v.name}, {"pos", {v.pos.x, v.pos.y, v.pos.z}}, {"yaw", v.yaw}, {"pitch", v.pitch}});
  gDerived["maps"][map] = arr;
}

// the closed loop: up to six of the densest spots (buildings first, then vegetation, then anything), each 70 m from the
// others, toured nearest-first; the 360 turn at the street view
void pathFor(App& a, const std::string& map) {
  gPath = BPath{};
  json pj = loadViewsFile().value("paths", json::object()).value(map, json());
  if (pj.is_object() && pj.contains("points")) {
    for (auto& p : pj["points"]) gPath.pts.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
    gPath.height = pj.value("height", 9.0f); gPath.turn = jvec3(pj["turn"], vec3(0)); gPath.turnYaw = pj.value("turnYaw", 0.0f);
  } else {
    std::vector<vec2> chosen; vec2 c;
    for (auto want : {isBuilding, isBuilding, isGreen, isGreen, isAny, isAny}) {
      auto pts = pointsOf(a, want);
      if (densestOf(pts, c, chosen, 70.0f) && a.terrain.inBuildArea(c.x, c.y)) chosen.push_back(c);
    }
    if (chosen.size() < 2) chosen = {vec2(-60, -40), vec2(60, -40), vec2(60, 40), vec2(-60, 40)};   // an empty map: a box round the centre
    std::vector<vec2> tour{chosen[0]}; chosen.erase(chosen.begin());
    while (!chosen.empty()) { auto it = std::min_element(chosen.begin(), chosen.end(), [&](vec2 p, vec2 q) { return glm::length(p - tour.back()) < glm::length(q - tour.back()); }); tour.push_back(*it); chosen.erase(it); }
    gPath.pts = tour; gPath.height = envF("NMK_BENCH_PATH_H", 9.0f);
    BView s{map, "street"}; vec2 b;
    if (densestOf(pointsOf(a, isBuilding), b) && standNear(a, b, 9.0f, s)) { gPath.turn = s.pos; gPath.turnYaw = s.yaw; }
    else { vec2 p = gPath.pts[0]; gPath.turn = vec3(p.x, a.terrain.height(p.x, p.y) + 1.7f, p.y); }
    json pts = json::array(); for (auto p : gPath.pts) pts.push_back({std::round(p.x * 10) / 10, std::round(p.y * 10) / 10});
    gDerived["paths"][map] = {{"points", pts}, {"height", gPath.height}, {"turn", {gPath.turn.x, gPath.turn.y, gPath.turn.z}}, {"turnYaw", gPath.turnYaw}};
  }
  if (gPath.pts.size() < 2) gPath.pts = {vec2(-60, -40), vec2(60, 40)};
  for (size_t k = 0; k < gPath.pts.size(); ++k) gPath.len += glm::length(gPath.pts[(k + 1) % gPath.pts.size()] - gPath.pts[k]);
  kPathS = envF("NMK_BENCH_PATH_SPEED", 0) > 0 ? gPath.len / envF("NMK_BENCH_PATH_SPEED", 0) : 40.0;   // NMK_BENCH_PATH_SPEED=5.4: a sprint (default: the loop in 40 s)
  logf("[bench] %s: path of %zu points, %.0f m (%.1f m/s), turn at (%.0f, %.0f)", map.c_str(), gPath.pts.size(), gPath.len, gPath.len / kPathS, gPath.turn.x, gPath.turn.z);
}
// Catmull-Rom through the closed loop at parameter u in [0,1) (by arc length of the control polygon)
vec2 pathAt(float u) {
  const auto& P = gPath.pts; size_t n = P.size(); float d = (u - std::floor(u)) * gPath.len;
  size_t k = 0; float seg = glm::length(P[1 % n] - P[0]);
  while (d > seg && k + 1 < n) { d -= seg; ++k; seg = glm::length(P[(k + 1) % n] - P[k]); }
  float t = seg > 0 ? std::min(1.0f, d / seg) : 0;
  vec2 p0 = P[(k + n - 1) % n], p1 = P[k], p2 = P[(k + 1) % n], p3 = P[(k + 2) % n];
  float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
// the camera at time s into a lap: the loop, then the turn
void pathCamera(App& a, double s) {
  if (s < kPathS) {
    float u = (float)(s / kPathS), du = 15.0f / std::max(1.0f, gPath.len);
    vec2 p = pathAt(u), q = pathAt(u + du);
    float ground = a.terrain.height(p.x, p.y);
    for (float f : {0.25f, 0.5f, 0.75f, 1.0f}) { vec2 r = glm::mix(p, q, f); ground = std::max(ground, a.terrain.height(r.x, r.y) - 2.0f * f); }   // clear rising ground ahead
    a.camPos = vec3(p.x, std::max(ground, a.terrain.waterLevel) + gPath.height, p.y);
    vec2 d = q - p; a.camYaw = degrees(std::atan2(-d.x, -d.y)); a.camPitch = -10.0f;
  } else {
    a.camPos = gPath.turn; a.camYaw = gPath.turnYaw + 360.0f * (float)((s - kPathS) / kTurnS); a.camPitch = -2.0f;
  }
}

void sample(App& a, Run& r, int lap) {
  double g = 0; std::array<double, GP_Count> ps{};
  for (int i = 0; i < GP_Count; ++i) { ps[i] = a.R.gpuPassMs[i]; g += ps[i]; }
  const HitchFrame& h = hitchLast();
  if (lap < 0) {   // the warm-up lap: fragment counts only
    if (a.R.fragStats) { for (int i = 0; i < GP_Count; ++i) r.frag[i] += a.R.fragInvM[i]; ++r.fragN; }
    return;
  }
  r.gpu.push_back(g); r.pass.push_back(ps); r.cpu.push_back(h.frameMs); r.swap.push_back(h.swapMs); r.cpuNoSwap.push_back(std::max(0.0, h.frameMs - h.swapMs));
  r.rcpu.push_back(a.R.stats.cpuMs); r.draws.push_back(a.R.stats.draws); r.tris.push_back(a.R.stats.tris); r.probes.push_back(a.R.stats.probes);
  r.lap.push_back(lap); r.hitch.push_back(h.hitch);
}

json pctJ(const std::vector<double>& v) { return {{"p50", pct(v, 0.5)}, {"p95", pct(v, 0.95)}, {"p99", pct(v, 0.99)}, {"p999", pct(v, 0.999)}, {"max", pct(v, 1.0)}, {"mean", mean(v)}}; }

// one finished run, appended at once (a crash later in the process keeps it): a table row (bench_views_rows.md), a JSON
// line (bench_views.jsonl) and the per-frame CSV (bench_frames/<tag>_<map>_<view>_<preset>.csv)
void writeRun(App& a, const Run& r) {
  int fw, fh; glfwGetFramebufferSize(a.win, &fw, &fh);
  json pj = json::object(), fj = json::object(); std::vector<std::pair<double, int>> top;
  double px = std::max(1, r.iw * r.ih);
  auto fragPx = [&](int i) { return r.fragN ? r.frag[i] / r.fragN * 1e6 / px : 0.0; };   // fragment invocations per internal pixel
  for (int i = 0; i < GP_Count; ++i) {
    std::vector<double> v; for (auto& p : r.pass) v.push_back(p[i]);
    double m = mean(v); pj[gpuPassName(i)] = {{"mean", std::round(m * 1000) / 1000}, {"p95", std::round(pct(v, 0.95) * 1000) / 1000}}; top.push_back({m, i});
    if (r.fragN) fj[gpuPassName(i)] = std::round(fragPx(i) * 100) / 100;
  }
  std::sort(top.rbegin(), top.rend());
  int nl = 0; for (int l : r.lap) nl = std::max(nl, l + 1);
  json lapP99 = json::array(), lapP50 = json::array(), lapCpu99 = json::array();   // per lap: the spread the regression rule (tools/bench_compare.py) needs
  for (int l = 0; l < nl; ++l) { std::vector<double> v, c; for (size_t k = 0; k < r.gpu.size(); ++k) if (r.lap[k] == l) { v.push_back(r.gpu[k]); c.push_back(r.cpuNoSwap[k]); } lapP99.push_back(pct(v, 0.99)); lapP50.push_back(pct(v, 0.5)); lapCpu99.push_back(pct(c, 0.99)); }
  int h15 = std::accumulate(r.hitch.begin(), r.hitch.end(), 0), h33 = 0, h50 = 0; for (double c : r.cpu) { h33 += c > 33.3; h50 += c > 50.0; }
  json j = {{"kind", gKind}, {"map", r.map}, {"view", r.view}, {"preset", r.preset}, {"frames", r.gpu.size()}, {"laps", nl}, {"framebuffer", {fw, fh}}, {"internal", {r.iw, r.ih}},
            {"lighting", a.R.q.lightNew ? "new" : "classic"}, {"msaa", a.R.msaa()}, {"vsync", false}, {"fpsCap", 0}, {"weather", std::getenv("NMK_BENCH_WEATHER") ? std::getenv("NMK_BENCH_WEATHER") : "map"}, {"hour", a.R.lt.hour}, {"envFog", a.R.wx.fog}, {"visCap", settings().visCap}, {"pathSpeed", gKind == "path" ? gPath.len / kPathS : 0.0},   // self-tests: swap interval 0, no cap (settings.cpp)
            {"gpuMs", pctJ(r.gpu)}, {"gpuP99PerLap", lapP99}, {"gpuP50PerLap", lapP50}, {"cpuP99PerLap", lapCpu99}, {"cpuFrameMs", pctJ(r.cpu)}, {"cpuNoSwapMs", pctJ(r.cpuNoSwap)}, {"swapMs", pctJ(r.swap)},
            {"hitches", {{"over1_5xMedian", h15}, {"over33ms", h33}, {"over50ms", h50}}},
            {"renderCpuMs", pct(r.rcpu, 0.5)}, {"probesPerFrame", mean(r.probes)}, {"draws", {{"p50", pct(r.draws, 0.5)}, {"max", pct(r.draws, 1)}}}, {"tris", {{"p50", pct(r.tris, 0.5)}, {"max", pct(r.tris, 1)}}},
            {"gpuPassMs", pj}, {"fragPerPixel", fj}};
  char b[768]; std::snprintf(b, sizeof b, "| %s | %s | %s | %dx%d | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %d / %d / %d | %.1f | %.0f | %.1f | %s %.2f (%.1fx), %s %.2f (%.1fx), %s %.2f (%.1fx) |\n",
                              r.map.c_str(), r.view.c_str(), r.preset.c_str(), r.iw, r.ih, pct(r.gpu, 0.5), pct(r.gpu, 0.95), pct(r.gpu, 0.99), pct(r.gpu, 0.999), pct(r.gpu, 1.0),
                              pct(r.cpuNoSwap, 0.5), pct(r.cpuNoSwap, 0.99), pct(r.cpu, 0.99), pct(r.swap, 0.5), h15, h33, h50,
                              mean(r.probes), pct(r.draws, 0.5), pct(r.tris, 0.5) / 1e6,
                              gpuPassName(top[0].second), top[0].first, fragPx(top[0].second), gpuPassName(top[1].second), top[1].first, fragPx(top[1].second), gpuPassName(top[2].second), top[2].first, fragPx(top[2].second));
  fs::path d = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(d / "bench_frames", ec);
  std::ofstream(d / "bench_views.jsonl", std::ios::app) << j.dump() << "\n";
  std::ofstream(d / "bench_views_rows.md", std::ios::app) << b;
  std::ofstream csv(d / "bench_frames" / (gTag + "_" + r.map + "_" + r.view + "_" + r.preset + ".csv"));
  csv << "frame,lap,gpu_ms,cpu_ms,cpu_noswap_ms,swap_ms,hitch,draws,tris"; for (int i = 0; i < GP_Count; ++i) csv << "," << gpuPassName(i); csv << "\n";
  for (size_t k = 0; k < r.gpu.size(); ++k) {
    csv << k << "," << r.lap[k] << "," << r.gpu[k] << "," << r.cpu[k] << "," << r.cpuNoSwap[k] << "," << r.swap[k] << "," << r.hitch[k] << "," << r.draws[k] << "," << r.tris[k];
    for (int i = 0; i < GP_Count; ++i) csv << "," << r.pass[k][i];
    csv << "\n";
  }
}
void writeDerived() { if (!gDerived.empty()) std::ofstream(exeDir() / "selftest_out" / "bench_views_derived.jsonl", std::ios::app) << gDerived.dump() << "\n"; gDerived = json::object(); }

void applyRunSettings(App& a) {
  static const auto perf0 = a.R.perf; a.R.perf = perf0;   // every run starts from the launch switches (a knock-out does not leak into the next)
  settings().s.dynRes = std::getenv("NMK_BENCH_DYNRES") != nullptr;   // CP 5.1: off in benchmarks (the corpus rule), on to test it
  settings().applyPreset(a, gPresets[gPi]);
  if (const char* l = std::getenv("NMK_BENCH_LIGHTING")) a.R.q.lightNew = std::string(l) != "classic";   // Lighting New (default) / Classic
  if (const char* m = std::getenv("NMK_BENCH_MSAA"); m && *m) a.R.setMsaa(std::atoi(m));   // A/B over the preset's MSAA (VO P2)
  for (auto& f : envList("NMK_BENCH_RQOFF")) if (!a.R.perf.set(f, false)) logf("[bench] NMK_BENCH_RQOFF: unknown switch %s", f.c_str());   // A/B: Renderer::perf switches off
  // the knock-out config: Renderer::perf switch names, plus ssr (R.q.ssr), msaa4, scale<s> (render scale), joined by '+'
  for (size_t a0 = 0, c; a0 <= gCfgs[gCi].size(); a0 = c + 1) {
    c = gCfgs[gCi].find('+', a0); if (c == std::string::npos) c = gCfgs[gCi].size();
    std::string f = gCfgs[gCi].substr(a0, c - a0); if (f.empty()) continue;
    if (f[0] == '#' || f.rfind("hour=", 0) == 0 || f.rfind("envfog=", 0) == 0 || f.rfind("wx=", 0) == 0) continue;   // #n: a repeat (noise); hour= / envfog=: applied with the weather below
    if (f == "ssr") a.R.q.ssr = false; else if (f == "msaa4") { if (gPresets[gPi] == GfxPreset::High) a.R.setMsaa(4); }   // P2 off: the old High (Medium never had MSAA) else if (f[0] == '^') a.R.perf.set(f.substr(1), true);   // ^name: a switch on
    else if (f.rfind("scale", 0) == 0) a.R.setResolutionScale((float)std::atof(f.c_str() + 5));
    else if (!a.R.perf.set(f, false)) logf("[bench] NMK_BENCH_AB: unknown switch %s", f.c_str());
  }
  // weather: the map's own sky by default (fog layer, cloud deck, snowfall as players see the map); NMK_BENCH_WEATHER=clear
  // or cloudy forces one (secondary rows)
  a.R.lt.hour = envF("NMK_BENCH_HOUR", 14.0f);   // NMK_BENCH_HOUR=0: night
  if (const char* w = std::getenv("NMK_BENCH_WEATHER"); w && *w) { int id = wxPresetFromName(w); a.R.wx.target = id >= 0 ? id : 0; a.R.wx.userSet = true; }   // a W1 preset name
  else a.R.wx.userSet = false;
  if (const char* f = std::getenv("NMK_BENCH_ENVFOG"); f && *f) { a.R.wx.fog = std::atoi(f); a.R.wx.userSet = true; }   // 0 Off .. 3 Dense
  for (size_t a0 = 0, c; a0 <= gCfgs[gCi].size(); a0 = c + 1) {   // config tokens hour=<h>, envfog=<0..3> (the viscull pairs)
    c = gCfgs[gCi].find('+', a0); if (c == std::string::npos) c = gCfgs[gCi].size();
    std::string f = gCfgs[gCi].substr(a0, c - a0);
    if (f.rfind("hour=", 0) == 0) a.R.lt.hour = (float)std::atof(f.c_str() + 5);
    if (f.rfind("envfog=", 0) == 0) { a.R.wx.fog = std::atoi(f.c_str() + 7); a.R.wx.userSet = true; }
    if (f.rfind("wx=", 0) == 0) {   // wx=<W1 preset name> ("_" for " "): that preset, as the menu would set it
      int id = wxPresetFromName(f.substr(3));
      if (id < 0) logf("[bench] unknown weather preset %s", f.c_str() + 3); else { a.R.wx.target = id; a.R.wx.userSet = true; }
    }
  }
  a.R.wx.snap();
}
void internalRes(App& a, Run& r) { float s = a.R.perf.taau ? a.R.resolutionScale() : 1.0f; int fw, fh; glfwGetFramebufferSize(a.win, &fw, &fh); r.iw = (int)std::lround(fw * s); r.ih = (int)std::lround(fh * s); }

void nextRun(App& a) {   // the next config / view / preset / map
  if (++gCi < gCfgs.size()) { gPhase = 1; return; }
  gCi = 0;
  if (++gVi < (gKind == "views" ? gViews.size() : 1)) { gPhase = 1; return; }
  gVi = 0;
  if (++gPi < gPresets.size()) { gPhase = 1; return; }
  if (a.mode == Mode::Explore) a.exitExplore();
  ++gMi; gPhase = 0;
}
}   // namespace

bool benchViews(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--bench-views") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return false;   // the post-render pass writes the queued shots (stage 0: nothing else runs there)
  if (gMaps.empty()) {
    if (const char* r = std::getenv("NMK_BENCH_RES")) std::sscanf(r, "%dx%d", &gW, &gH);
    int fw, fh; glfwGetFramebufferSize(a.win, &fw, &fh);
    if (fw != gW || fh != gH) { glfwSetWindowSize(a.win, gW, gH); glfwGetFramebufferSize(a.win, &fw, &fh); }
    logf("[bench] framebuffer %d x %d, kind %s", fw, fh, gKind.c_str());
    gMaps = envList("NMK_BENCH_MAPS"); if (gMaps.empty()) gMaps.assign(std::begin(kMaps), std::end(kMaps));
    for (auto& n : envList("NMK_BENCH_PRESETS")) gPresets.push_back(n == "Low" ? GfxPreset::Low : n == "High" ? GfxPreset::High : n == "Ultra Low" ? GfxPreset::UltraLow : GfxPreset::Medium);
    if (gPresets.empty()) gPresets = {GfxPreset::Low, GfxPreset::Medium, GfxPreset::High};
    const char* ab = std::getenv(gKind == "views" ? "NMK_BENCH_AB" : "NMK_BENCH_AB_MOVING"); if (!ab) ab = std::getenv("NMK_BENCH_AB");   // path / bots: their own (short) config list
    if (ab) { gCfgs.clear(); std::string s = ab; for (size_t a0 = 0, c; a0 <= s.size(); a0 = c + 1) { c = s.find(';', a0); if (c == std::string::npos) c = s.size(); std::string x = s.substr(a0, c - a0); gCfgs.push_back(x == "none" ? "" : x); } }
    a.hideUi = true; a.hideEditorOverlays = true;
    a.selftest->realTime = true;   // wall-clock frame time: the simulation takes as many steps as a player's frame would (not one 1/60 s step)
  }
  if (gMi >= gMaps.size()) { a.selftest->exitCode = 0; a.selftest->done = true; return true; }
  const std::string& map = gMaps[gMi];
  double t = nowS();
  if (gPhase == 0) {   // load the map, its views / path / match
    const TemplateInfo* tp = a.findTemplate(map);
    bool ok = tp != nullptr;
    if (ok && gKind == "bots") {   // the way --profile-map starts a match: an editable copy, the AI split over both sides
      ok = navNewMapSized(a, *tp, "Bench " + tp->name, vec2(tp->width, tp->depth));
      Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json()) : json());
      vec3 sp = tp->camPos; for (auto& s : sc.spawns) if (s.role == CombatRole::Attacker && s.activeIn(0)) { sp = s.pos; break; }
      a.doc.start = {true, sp, 180.0f};
      int n = (int)envF("NMK_BENCH_BOTS", 100); sc.rules.defenderSlots = n / 2; sc.rules.attackerAI = n - n / 2;
      gUi.matchScenario = sc.toJson(); gUi.loaded = false;
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true;
      ok = ok && a.enterExplore() && gGame && gGame->active;
      if (ok) { a.useScripted = true; a.scriptedInput = ExploreInput{}; }
    } else if (ok) ok = a.openMap(contentDir() / "templates" / (map + ".nmap.json"));
    logf("[bench] open %s (%s): %s", map.c_str(), gKind.c_str(), ok ? "ok" : "FAILED");
    if (!ok) { ++gMi; return true; }
    gViews.clear();
    if (gKind == "views") viewsFor(a, map, *tp); else if (gKind == "path") pathFor(a, map);
    writeDerived();
    gPi = 0; gVi = 0; gPhase = 1; gFirstView = true;
    return true;
  }
  if (gPhase == 1) {   // a preset (and a view): fixed light, the camera, settle / warm-up
    applyRunSettings(a);
    std::string view = (gKind == "views" ? gViews[gVi].name : gKind) + (gCfgs[gCi].empty() ? "" : "-" + gCfgs[gCi]);
    gRuns.push_back({map, view, presetName(gPresets[gPi])}); internalRes(a, gRuns.back());
    if (gKind == "views") { const BView& v = gViews[gVi]; a.camPos = v.pos; a.camYaw = v.yaw; a.camPitch = v.pitch; gT0 = t + envF("NMK_BENCH_SETTLE", 6.0f) + (gFirstView ? 6.0f : 0.0f); }
    else gT0 = t;   // path / bots: the warm-up lap starts now
    a.R.resetTemporal(); a.R.fragStats = gKind != "views";
    gFirstView = false; gLast = 0; gPhase = 2;
    return true;
  }
  Run& r = gRuns.back();
  if (gKind == "path" || gKind == "bots") {   // gPhase 2: warm-up lap + timed laps, in real time
    double lapS = gKind == "path" ? kPathS + kTurnS : kBotLapS, warm = gKind == "path" ? lapS : kBotWarmS, s = t - gT0;
    int lap = s < warm ? -1 : (int)((s - warm) / lapS);
    if (gLast > 0) sample(a, r, lap);
    gLast = t;
    if (lap >= 0 && a.R.fragStats) a.R.fragStats = false;   // fragment counts from the warm-up lap only
    if (gKind == "path") pathCamera(a, std::fmod(s, lapS));
    else { a.scriptedInput = ExploreInput{}; a.eyePitch = -2; a.eyeYaw += 360.0f / (float)kBotLapS * dt; }
    if (lap >= laps()) {
      r.gpu.pop_back(); r.pass.pop_back(); r.cpu.pop_back(); r.swap.pop_back(); r.cpuNoSwap.pop_back(); r.rcpu.pop_back(); r.draws.pop_back(); r.tris.pop_back(); r.probes.pop_back(); r.lap.pop_back(); r.hitch.pop_back();   // the frame past the last lap
      writeRun(a, r);
      logf("[bench] %s %s %s: GPU P50 %.2f P99 %.2f P99.9 %.2f ms, CPU (no swap) P99 %.2f ms, %zu frames", r.map.c_str(), r.view.c_str(), r.preset.c_str(), pct(r.gpu, 0.5), pct(r.gpu, 0.99), pct(r.gpu, 0.999), pct(r.cpuNoSwap, 0.99), r.gpu.size());
      nextRun(a);
    }
    return true;
  }
  // views: gPhase 2 measure (3-5: the NMK_BENCH_SHOTS stills and motion frames)
  if (gPhase == 2 && t >= gT0 && gLast > 0) sample(a, r, 0);
  const double fdt = t - gLast; gLast = t;
  if (gPhase == 3) {   // NMK_BENCH_SHOTS: a still was queued; now pan 20 deg/s and take a frame 0.5 s into the motion (TAA under motion)
    a.camYaw += 20.0f * (float)fdt;
    if (t >= gT0 + 0.5) { a.selftest->queueShot("bench_" + r.map + "_" + r.view + "_" + r.preset + "_" + gTag + "_motion"); gPhase = 4; gT0 = t; }
    return true;
  }
  if (gPhase == 4) { if (t >= gT0 + 0.2) gPhase = 5; return true; }   // the motion shot is written after this frame
  auto shotCfg = [] { const char* l = std::getenv("NMK_BENCH_SHOTCFG"); if (!l || !*l) return true;   // NMK_BENCH_SHOTCFG="none,viscull": shots only for those configs
    std::string want = gCfgs[gCi].empty() ? "none" : gCfgs[gCi]; for (auto& x : envList("NMK_BENCH_SHOTCFG")) if (x == want) return true; return false; };
  if (gPhase == 2 && t >= gT0 + envF("NMK_BENCH_MEASURE", 4.0f) && std::getenv("NMK_BENCH_SHOTS") && shotCfg()) {
    a.selftest->queueShot("bench_" + r.map + "_" + r.view + "_" + r.preset + "_" + gTag + "_still");
    writeRun(a, r); gPhase = 3; gT0 = t + 0.1; gLast = t; return true;
  }
  if (gPhase == 5 || t >= gT0 + envF("NMK_BENCH_MEASURE", 4.0f)) {
    if (gPhase != 5) writeRun(a, r);
    logf("[bench] %s %s %s: GPU P50 %.2f P99 %.2f ms (%zu frames)", r.map.c_str(), r.view.c_str(), r.preset.c_str(), pct(r.gpu, 0.5), pct(r.gpu, 0.99), r.gpu.size());
    nextRun(a);
  }
  return true;
}
