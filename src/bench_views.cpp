// P1 --bench-views (docs/visual-overhaul-plan.md 3.E, 6): the VO benchmark. Five maps (Chateau town, Newtown, the forest
// outpost, D-day, Rue Haute), three fixed editor-camera views each (street level, dense foliage, overview) at Low, Medium
// and High, in a borderless 2560 x 1440 window (app.cpp), 2 pm clear sky, UI hidden. Per view: settle NMK_BENCH_SETTLE s
// (default 6, probe GI converging; 12 after a map loads), then NMK_BENCH_MEASURE s (default 4) of frames: GPU total (the per-pass timers summed) and CPU frame
// time (wall clock, no vsync) as median / p95 / p99, render() CPU, draws, triangles and the median of every GPU pass.
// Views come from content/settings/bench_views.json; a map missing there gets derived ones (the densest building cluster
// at eye height, the densest vegetation, the template's overview), appended to selftest_out/bench_views_derived.jsonl for
// committing. NMK_BENCH_MAPS="a,b" / NMK_BENCH_PRESETS="Low,High" narrow a run. Each finished run is appended at once
// (bench_views_rows.md, bench_views.jsonl). Run it through the script (one process per map, a crashed map retried once,
// the card held exclusively):   bash tools/bench_views.sh <build dir> [tag]   -> docs/perf/bench_<tag>.md (+ .jsonl)
// NMK_BENCH_SHOTS=1: per run a still after the measurement and a frame 0.5 s into a 20 deg/s pan (TAA under motion),
// selftest_out/bench_<map>_<view>_<preset>_<tag>_still|motion.png
#include "selftest.h"
#include "settings.h"
#include "physics.h"
#include <GLFW/glfw3.h>
#include <array>
#include <fstream>
#include <algorithm>
#include <numeric>

namespace {
struct BView { std::string map, name; vec3 pos; float yaw = 0, pitch = 0; };
struct Run { std::string map, view, preset; std::vector<double> gpu, cpu, rcpu, draws, tris, probes; std::vector<std::array<double, GP_Count>> pass; };
const char* kMaps[] = {"chateau_town", "newtown", "outpost_woods", "dday", "sd_town"};
std::vector<std::string> gMaps; std::vector<GfxPreset> gPresets;
std::vector<BView> gViews; json gDerived = json::object();
std::vector<Run> gRuns;
std::string gTag = std::getenv("NMK_BENCH_TAG") ? std::getenv("NMK_BENCH_TAG") : "run";
size_t gMi = 0, gPi = 0, gVi = 0; int gPhase = 0; double gT0 = 0, gLast = 0; bool gFirstView = true;

double nowS() { return glfwGetTime(); }
float envF(const char* n, float d) { const char* v = std::getenv(n); return v && *v ? (float)std::atof(v) : d; }
std::vector<std::string> envList(const char* n) { std::vector<std::string> o; const char* v = std::getenv(n); if (!v) return o; std::string s = v;
  for (size_t a = 0; a <= s.size();) { size_t c = s.find(',', a); if (c == std::string::npos) c = s.size(); if (c > a) o.push_back(s.substr(a, c - a)); a = c + 1; } return o; }
double pct(std::vector<double> v, double q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)std::floor(q * (v.size() - 1) + 0.5))]; }

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
// the densest spot of instances passing `want` (count within 30 m)
bool densest(App& a, bool (*want)(const AssetDef*), vec2& c) {
  std::vector<vec2> pts; for (auto& i : a.doc.instances) if (const AssetDef* d = a.assetFor(i); d && want(d)) pts.push_back(vec2(i.pos.x, i.pos.z));
  if (pts.empty()) return false;
  size_t step = std::max<size_t>(1, pts.size() / 600); int best = -1;   // candidates: up to 600 of them
  for (size_t k = 0; k < pts.size(); k += step) { int n = 0; for (auto& q : pts) n += glm::length(q - pts[k]) < 30.0f; if (n > best) { best = n; c = pts[k]; } }
  return true;
}
bool isBuilding(const AssetDef* d) { return d->enterable; }
bool isGreen(const AssetDef* d) { return d->tree.on || d->nature.kind != NatureDef::NONE || d->category == "Trees" || d->category == "Bushes" || d->category == "Nature"; }

void viewsFor(App& a, const std::string& map, const TemplateInfo& t) {
  json all = json::object();
  try { std::ifstream f(contentDir() / "settings" / "bench_views.json"); if (f) all = json::parse(f); } catch (...) {}
  json mv = all.value("maps", json::object()).value(map, json::array());
  if (mv.is_array() && !mv.empty()) {
    for (auto& v : mv) gViews.push_back({map, v.value("name", std::string("view")), jvec3(v["pos"], vec3(0)), v.value("yaw", 0.0f), v.value("pitch", 0.0f)});
    return;
  }
  BView s{map, "street"}, g{map, "foliage"}, o{map, "overview", t.camPos, t.camYaw, t.camPitch};
  vec2 c;
  if (densest(a, isBuilding, c)) {
    if (standNear(a, c, 9.0f, s)) gViews.push_back(s);
    o.yaw = 35.0f; o.pitch = -32.0f;   // overview: 110 m back and 70 m above the densest buildings, three-quarter
    o.pos = vec3(c.x + std::sin(radians(o.yaw)) * 110.0f, a.terrain.height(c.x, c.y) + 70.0f, c.y + std::cos(radians(o.yaw)) * 110.0f);
  }
  if (densest(a, isGreen, c) && standNear(a, c, 4.0f, g)) gViews.push_back(g);
  gViews.push_back(o);
  json arr = json::array();
  for (auto& v : gViews) if (v.map == map) arr.push_back({{"name", v.name}, {"pos", {v.pos.x, v.pos.y, v.pos.z}}, {"yaw", v.yaw}, {"pitch", v.pitch}});
  gDerived[map] = arr;
  logf("[bench] %s: %zu views derived (commit selftest_out/bench_views_derived.json into content/settings/bench_views.json)", map.c_str(), arr.size());
}

// one finished run, appended at once (a crash later in the process keeps it): a table row (bench_views_rows.md) and a JSON
// line (bench_views.jsonl, with the framebuffer size); tools/bench_views.sh assembles the table
void writeRun(App& a, const Run& r) {
  json pj = json::object(); std::vector<std::pair<double, int>> top;
  for (int i = 0; i < GP_Count; ++i) { std::vector<double> v; for (auto& p : r.pass) v.push_back(p[i]); double m = pct(v, 0.5); pj[gpuPassName(i)] = std::round(m * 1000) / 1000; top.push_back({m, i}); }
  std::sort(top.rbegin(), top.rend());
  int fw, fh; glfwGetFramebufferSize(a.win, &fw, &fh);
  json j = {{"map", r.map}, {"view", r.view}, {"preset", r.preset}, {"frames", r.gpu.size()}, {"framebuffer", {fw, fh}}, {"lighting", a.R.q.lightNew ? "new" : "classic"},
            {"gpuMs", {{"median", pct(r.gpu, 0.5)}, {"p95", pct(r.gpu, 0.95)}, {"p99", pct(r.gpu, 0.99)}}},
            {"cpuFrameMs", {{"median", pct(r.cpu, 0.5)}, {"p95", pct(r.cpu, 0.95)}, {"p99", pct(r.cpu, 0.99)}}},
            {"renderCpuMs", pct(r.rcpu, 0.5)}, {"probesPerFrame", std::accumulate(r.probes.begin(), r.probes.end(), 0.0) / std::max<size_t>(1, r.probes.size())}, {"draws", pct(r.draws, 0.5)}, {"tris", pct(r.tris, 0.5)}, {"gpuPassMs", pj}};
  char b[512]; std::snprintf(b, sizeof b, "| %s | %s | %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.0f | %.1f | %s %.2f, %s %.2f, %s %.2f |\n",
                              r.map.c_str(), r.view.c_str(), r.preset.c_str(), pct(r.gpu, 0.5), pct(r.gpu, 0.95), pct(r.gpu, 0.99), pct(r.cpu, 0.5), pct(r.cpu, 0.95), pct(r.cpu, 0.99),
                              pct(r.rcpu, 0.5), std::accumulate(r.probes.begin(), r.probes.end(), 0.0) / std::max<size_t>(1, r.probes.size()), pct(r.draws, 0.5), pct(r.tris, 0.5) / 1e6, gpuPassName(top[0].second), top[0].first, gpuPassName(top[1].second), top[1].first, gpuPassName(top[2].second), top[2].first);
  fs::path d = exeDir() / "selftest_out";
  std::ofstream(d / "bench_views.jsonl", std::ios::app) << j.dump() << "\n";
  std::ofstream(d / "bench_views_rows.md", std::ios::app) << b;
}
void writeDerived() { if (!gDerived.empty()) std::ofstream(exeDir() / "selftest_out" / "bench_views_derived.jsonl", std::ios::app) << json{{"maps", gDerived}}.dump() << "\n"; gDerived = json::object(); }
}   // namespace

bool benchViews(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--bench-views") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return false;   // the post-render pass writes the queued shots (stage 0: nothing else runs there)
  if (gMaps.empty()) {
    int fw, fh; glfwGetFramebufferSize(a.win, &fw, &fh);
    if (fw != 2560 || fh != 1440) { glfwSetWindowSize(a.win, 2560, 1440); glfwGetFramebufferSize(a.win, &fw, &fh); }
    logf("[bench] framebuffer %d x %d", fw, fh);
    gMaps = envList("NMK_BENCH_MAPS"); if (gMaps.empty()) gMaps.assign(std::begin(kMaps), std::end(kMaps));
    for (auto& n : envList("NMK_BENCH_PRESETS")) gPresets.push_back(n == "Low" ? GfxPreset::Low : n == "High" ? GfxPreset::High : n == "Ultra Low" ? GfxPreset::UltraLow : GfxPreset::Medium);
    if (gPresets.empty()) gPresets = {GfxPreset::Low, GfxPreset::Medium, GfxPreset::High};
    a.hideUi = true; a.hideEditorOverlays = true;
  }
  if (gMi >= gMaps.size()) { a.selftest->exitCode = 0; a.selftest->done = true; return true; }
  const std::string& map = gMaps[gMi];
  if (gPhase == 0) {   // load the map, its views
    const TemplateInfo* t = a.findTemplate(map);
    bool ok = t && a.openMap(contentDir() / "templates" / (map + ".nmap.json"));
    logf("[bench] open %s: %s", map.c_str(), ok ? "ok" : "FAILED");
    if (!ok) { ++gMi; return true; }
    gViews.erase(std::remove_if(gViews.begin(), gViews.end(), [](const BView&) { return true; }), gViews.end());
    viewsFor(a, map, *t); writeDerived();
    gPi = 0; gVi = 0; gPhase = 1; gFirstView = true;
    return true;
  }
  if (gPhase == 1) {   // a preset and a view: fixed light, the camera, settle
    settings().applyPreset(a, gPresets[gPi]);
    if (const char* l = std::getenv("NMK_BENCH_LIGHTING")) a.R.q.lightNew = std::string(l) != "classic";   // Lighting New (default) / Classic
    if (const char* m = std::getenv("NMK_BENCH_MSAA")) a.R.setMsaa(std::atoi(m));   // A/B over the preset's MSAA (VO P2)
    for (auto& f : envList("NMK_BENCH_RQOFF")) if (!a.R.perf.set(f, false)) logf("[bench] NMK_BENCH_RQOFF: unknown switch %s", f.c_str());   // A/B: Renderer::perf switches off
    a.R.lt.hour = 14.0f; a.R.wx.target = 0; a.R.wx.userSet = true; a.R.wx.snap();
    const BView& v = gViews[gVi];
    a.camPos = v.pos; a.camYaw = v.yaw; a.camPitch = v.pitch; a.R.resetTemporal();
    gRuns.push_back({map, v.name, presetName(gPresets[gPi])});
    gT0 = nowS() + envF("NMK_BENCH_SETTLE", 6.0f) + (gFirstView ? 6.0f : 0.0f); gFirstView = false; gLast = 0; gPhase = 2;
    return true;
  }
  // gPhase 2: measure (3-5: the NMK_BENCH_SHOTS stills and motion frames)
  double t = nowS();
  Run& r = gRuns.back();
  if (gPhase == 2 && t >= gT0 && gLast > 0) {
    double g = 0; std::array<double, GP_Count> ps{};
    for (int i = 0; i < GP_Count; ++i) { ps[i] = a.R.gpuPassMs[i]; g += ps[i]; }
    r.gpu.push_back(g); r.pass.push_back(ps); r.cpu.push_back((t - gLast) * 1000.0); r.rcpu.push_back(a.R.stats.cpuMs);
    r.draws.push_back(a.R.stats.draws); r.tris.push_back(a.R.stats.tris); r.probes.push_back(a.R.stats.probes);
  }
  const double fdt = t - gLast; gLast = t;
  if (gPhase == 3) {   // NMK_BENCH_SHOTS: a still was queued; now pan 20 deg/s and take a frame 0.5 s into the motion (TAA under motion)
    a.camYaw += 20.0f * (float)fdt;
    if (t >= gT0 + 0.5) { a.selftest->queueShot("bench_" + r.map + "_" + r.view + "_" + r.preset + "_" + gTag + "_motion"); gPhase = 4; gT0 = t; }
    return true;
  }
  if (gPhase == 4) { if (t >= gT0 + 0.2) gPhase = 5; return true; }   // the motion shot is written after this frame
  if (gPhase == 2 && t >= gT0 + envF("NMK_BENCH_MEASURE", 4.0f) && std::getenv("NMK_BENCH_SHOTS")) {
    a.selftest->queueShot("bench_" + r.map + "_" + r.view + "_" + r.preset + "_" + gTag + "_still");
    writeRun(a, r); gPhase = 3; gT0 = t + 0.1; gLast = t; return true;
  }
  if (gPhase == 5 || t >= gT0 + envF("NMK_BENCH_MEASURE", 4.0f)) {
    if (gPhase != 5) writeRun(a, r);
    logf("[bench] %s %s %s: GPU median %.2f p95 %.2f ms, CPU median %.2f ms (%zu frames)", r.map.c_str(), r.view.c_str(), r.preset.c_str(), pct(r.gpu, 0.5), pct(r.gpu, 0.95), pct(r.cpu, 0.5), r.gpu.size());
    if (++gVi >= gViews.size()) { gVi = 0; if (++gPi >= gPresets.size()) { ++gMi; gPhase = 0; return true; } }
    gPhase = 1;
  }
  return true;
}
