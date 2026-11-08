// --selftest-lights (L2, docs/lighting.md): the lighting engine. Report: selftest_out/report_lights.txt, numbers in
// selftest_out/lights.json, frames in selftest_out/lights_*.png.
//  * time of day: the four presets (sun path, key light, moon, stars, exposure range), Day exactly the old constants,
//    the Settings hour round trip, a frame at each preset (no non-finite HDR pixel, Day brighter than Night);
//  * clusters: 48 lights in front of the camera binned on the GPU equal a CPU replica of the froxel test;
//  * a closed test room (asset written by the test: walls, floor, ceiling, a free-standing partition, no openings):
//    a lit room is brighter than the unlit one; indirect light reaches the partition face turned away from the lamp
//    (probe read-back and the image with GI on vs off); no light leaks through the wall (probe read-back outside the
//    room and the outside image, lamp on vs off); the lamp's shadow map darkens the floor behind the partition;
//  * emission: an "emissive" material is parsed and glows at night;
//  * Day matches the pre-change look: the same view with probe GI on (Medium) and off (the old fill term), against the
//    frame-to-frame noise floor; frame times reported.
#include "selftest.h"
#include "mptest.h"
#include "lighting.h"
#include "settings.h"
#include <cstdarg>
#include <fstream>
#include <functional>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, int)> fn; };
std::vector<Step> seq; size_t cur = 0; int sf = 0; float total = 0;
fs::path pendingShot;
json report = json::object();
char B[800];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[lights] %s", B); }
void push(const std::string& n, std::function<bool(App&, int)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }

vec3 O(0);   // test room origin (floor level)
vec3 W(vec3 local) { return O + local; }
void look(App& a, vec3 eye, vec3 at) {
  vec3 d = glm::normalize(at - eye);
  a.camPos = eye; a.camPitch = degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))); a.camYaw = degrees(std::atan2(-d.x, -d.z));
}
// LDR frame (last render) helpers
std::vector<uint8_t> px; int pw = 0, ph = 0;
void grab(App& a) { a.R.readPixels(px, pw, ph); }
double luma(const uint8_t* p) { return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; }
double meanLuma(int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1) {
  if (x1 < 0) { x1 = pw; y1 = ph; }
  double s = 0; long n = 0;
  for (int y = std::max(0, y0); y < std::min(ph, y1); ++y) for (int x = std::max(0, x0); x < std::min(pw, x1); ++x) { s += luma(&px[((size_t)y * pw + x) * 4]); ++n; }
  return n ? s / n : 0;
}
double lumaAt(App& a, vec3 w, int r = 6) {   // mean luma of a (2r+1)^2 box around a world point (-1: off screen)
  vec4 c = a.projM * a.viewM * vec4(w, 1);
  if (c.w <= 0.01f) return -1;
  int x = (int)((c.x / c.w * 0.5f + 0.5f) * pw), y = (int)((c.y / c.w * 0.5f + 0.5f) * ph);
  if (x < r || y < r || x >= pw - r || y >= ph - r) return -1;
  return meanLuma(x - r, y - r, x + r + 1, y + r + 1);
}
vec3 skyRgb() {   // mean colour of the top tenth of the frame
  double s[3] = {0, 0, 0}; long n = 0;
  for (int y = ph * 9 / 10; y < ph; ++y) for (int x = 0; x < pw; ++x) { const uint8_t* p = &px[((size_t)y * pw + x) * 4]; for (int c = 0; c < 3; ++c) s[c] += p[c]; ++n; }
  return n ? vec3(s[0] / n, s[1] / n, s[2] / n) : vec3(0);
}
float lumf(vec3 c) { return glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f)); }

LightDesc lamp(vec3 p, float intensity, bool shadow) {
  LightDesc l; l.pos = p; l.radius = 7.0f; l.intensity = intensity; l.color = vec3(1.0f, 0.78f, 0.5f); l.shadow = shadow ? 1 : 0; l.id = 0xC0FFEEu; return l;
}
const vec3 kLampL(1.3f, 1.6f, 0.0f);   // in the room, on the +x side of the partition
void setLamp(App& a, bool on, bool shadow = true) {
  a.R.testLights.clear();
  if (on) a.R.testLights.push_back(lamp(W(kLampL), 3.0f, shadow));
  lightingResetProbes();
}
// the test room asset: 6 x 3.2 x 6 m outside, 0.3 m walls, a 1.2 x 1.8 m partition in the middle, no openings;
// two small boxes outside (one emissive) for the emission check
bool writeRoomAsset(const fs::path& dir) {
  std::error_code ec; fs::create_directories(dir, ec);
  json mats = json::array({
    {{"name", "wall"}, {"albedo", {0.70, 0.68, 0.62}}, {"roughness", 0.9}, {"pattern", "plaster"}},
    {{"name", "glow"}, {"albedo", {0.8, 0.8, 0.8}}, {"roughness", 0.5}, {"emissive", {6.0, 4.5, 2.5}}},
    {{"name", "white"}, {"albedo", {0.8, 0.8, 0.8}}, {"roughness", 0.5}}});
  auto box = [](vec3 c, vec3 s, const char* m) { return json{{"c", {c.x, c.y, c.z}}, {"s", {s.x, s.y, s.z}}, {"m", m}}; };
  json parts = json::array({
    box({0, 0.1f, 0}, {6, 0.2f, 6}, "wall"), box({0, 3.1f, 0}, {6, 0.2f, 6}, "wall"),
    box({2.85f, 1.6f, 0}, {0.3f, 3.2f, 6}, "wall"), box({-2.85f, 1.6f, 0}, {0.3f, 3.2f, 6}, "wall"),
    box({0, 1.6f, 2.85f}, {5.4f, 3.2f, 0.3f}, "wall"), box({0, 1.6f, -2.85f}, {5.4f, 3.2f, 0.3f}, "wall"),
    box({0, 1.1f, 0}, {0.2f, 1.8f, 1.2f}, "wall"),
    box({5.0f, 0.5f, 0.0f}, {0.4f, 0.4f, 0.4f}, "glow"), box({5.0f, 0.5f, 1.2f}, {0.4f, 0.4f, 0.4f}, "white")});
  json j = {{"id", "lt_testroom"}, {"name", "Lighting test room"}, {"category", "Test"}, {"footprint", {8, 7}}, {"materials", mats}, {"parts", parts}};
  std::ofstream f(dir / "lt_testroom.json"); f << j.dump(1);
  return (bool)f;
}
json js(vec3 v) { return json::array({v.x, v.y, v.z}); }
double gpuMs(App& a) { return a.R.stats.gpuMs; }

// accumulators shared between steps
double lumOn = 0, lumOff = 0;
vec3 giOnAway{0}, giOffAway{0}, giOnIn{0}, giOffIn{0}, giOnOut{0}, giOffOut{0};
double awayGiOn = 0, awayGiOff = 0, outOn = 0, outOff = 0, shadowOn = 0, shadowOff = 0;
std::vector<uint8_t> imgA, imgB, imgB2;
double msGi = 0, msNoGi = 0; int nGi = 0, nNoGi = 0;

// one measurement step: settle `frames` frames with a setup on frame 1, then measure
void measure(const std::string& name, int frames, std::function<void(App&)> setup, std::function<void(App&)> fn) {
  push(name, [=](App& a, int f) {
    if (f == 1) setup(a);
    if (f < frames) return false;
    grab(a); fn(a);
    return true;
  });
}

void build(App&) {
  push("setup: Blank Slate, the test room, a stone cottage", [](App& a, int f) {
    if (f == 1) {
      const TemplateInfo* tp = a.findTemplate("blank_slate");
      chk(tp && a.newMap(*tp, "Lights test"), "Blank Slate map");
      fs::path dir = outDir() / "lt_assets";
      chk(writeRoomAsset(dir), "test room asset written");
      a.lib.loadAll(dir);
      const AssetDef* d = a.lib.find("lt_testroom");
      chk(d != nullptr, "test room asset loaded");
      if (d) {
        bool em = false; for (auto& m : d->materials) if (m.name == "glow") em = m.emissive == vec3(6.0f, 4.5f, 2.5f);
        chk(em, "material \"emissive\": [6, 4.5, 2.5] parsed");
        O = vec3(0, a.terrain.height(0, -20), -20);
        a.placeInstance(d, O, 0);
      }
      const AssetDef* c = a.lib.find("house_stone_cottage");
      if (c) a.placeInstance(c, vec3(-18, a.terrain.height(-18, -24), -24), 0);
      a.syncRuntime();
      a.hideEditorOverlays = true;
      a.R.lt.probeBudget = 16;   // converge in a few hundred frames
      chk(a.R.shaderErrors == 0 && a.R.fboErrors == 0, "shaders compiled (%d errors), framebuffers complete (%d)", a.R.shaderErrors, a.R.fboErrors);
      LightingStats s = lightingStats();
      chk(s.giAvailable, "probe GI available (layered capture, >= 20 texture units)");
    }
    return f > 10;
  });
  // ---------------------------------------------------------------- time of day (CPU)
  push("time of day: presets", [](App& a, int) {
    TodInfo d = lightingTod(todPresetHour(TodPreset::Day));
    chk(d.day && d.key == a.R.sunDir && d.keyMul == vec3(1) && d.fixedExpo == 1.0f && d.expoMin == 0.9f && d.expoMax == 1.45f && d.expoKey == 0.24f && d.giOpenK == 1.0f,
        "Day (14:00): exactly the pre-L2 sun (%.3f %.3f %.3f), colours, exposure 0.9 .. 1.45, fixed exposure 1", d.key.x, d.key.y, d.key.z);
    TodInfo near = lightingTod(13.99f);
    chk(!near.day && glm::length(near.key - d.key) < 0.01f && std::fabs(lumf(near.keyMul) - 1) < 0.02f && std::fabs(lumf(near.skyMul) - 1) < 0.02f, "13:59: continuous with Day (sun, key, sky within 2 %%)");
    TodInfo du = lightingTod(todPresetHour(TodPreset::Dusk)), dn = lightingTod(todPresetHour(TodPreset::Dawn)), ni = lightingTod(todPresetHour(TodPreset::Night));
    chk(du.sunEl > 1 && du.sunEl < 8 && du.sun.x < -0.5f && !du.keyIsMoon && du.keyMul.r > du.keyMul.b * 1.6f, "Dusk (18:36): low western sun %.1f deg, warm key (r/b %.2f)", du.sunEl, du.keyMul.r / std::max(du.keyMul.b, 1e-4f));
    chk(dn.sunEl > 1 && dn.sunEl < 8 && dn.sun.x > 0.5f && !dn.keyIsMoon, "Dawn (05:00): low eastern sun %.1f deg", dn.sunEl);
    chk(ni.sunEl < -18 && ni.keyIsMoon && ni.moonEl > 15 && ni.stars > 0.9f && ni.expoMax >= 5 && ni.fixedExpo > 2 && lumf(ni.keyMul) < 0.05f,
        "Night (00:00): sun %.1f deg, moon key at %.1f deg, stars %.2f, exposure up to %.1f (fixed %.2f)", ni.sunEl, ni.moonEl, ni.stars, ni.expoMax, ni.fixedExpo);
    // noon is highest; the sun rises in the east and sets in the west
    float best = -90, bestH = 0; for (float h = 0; h < 24; h += 0.25f) { float e = lightingTod(h).sunEl; if (e > best) { best = e; bestH = h; } }
    chk(bestH > 11 && bestH < 13 && best > 50 && best < 62, "solar noon at %.2f h, %.1f deg (49 deg N, mid summer)", bestH, best);
    for (TodPreset p : {TodPreset::Day, TodPreset::Dusk, TodPreset::Night, TodPreset::Dawn}) chk(todPresetOf(todPresetHour(p)) == p, "preset %s round trips through its hour", todPresetName(p));
    // settings: the hour is personal (kept by a graphics preset), saved, and applied
    Settings& S = settings();
    UserSettings u; u.hour = todPresetHour(TodPreset::Night); u.gfx.lightCount = 100; u.gfx.shadowLights = 8; u.gfx.gi = 2;
    fs::path rt = outDir() / "lights_settings.json";
    Settings w; w.s = u; w.save(rt); Settings r; r.load(rt);
    chk(r.s == u && r.s.toJson()["graphics"].value("timeOfDay", "") == "Night", "settings file: time of day Night, hour, light count, shadowed lights, GI round trip");
    { std::ofstream(rt) << R"({"graphics":{"timeOfDay":"Dusk"}})"; Settings c; c.load(rt); chk(std::fabs(c.s.hour - 18.6f) < 1e-4f, "a file naming only \"Dusk\" loads 18:36"); }
    GraphicsOpts lo = presetOpts(GfxPreset::Low), md = presetOpts(GfxPreset::Medium), hi = presetOpts(GfxPreset::High);
    chk(lo.gi == 0 && lo.shadowLights == 0 && md.gi == 1 && md.shadowLights == 4 && md.lightCount == 256 && hi.gi == 2 && hi.shadowLights == 8,
        "presets: Low GI off / no light shadows, Medium near GI / 4 / 256 lights, High full GI / 8");
    UserSettings keep = S.s;
    S.s.hour = todPresetHour(TodPreset::Dusk); S.s.gfx = presetOpts(GfxPreset::High); S.apply(a);
    chk(a.R.lt.hour == S.s.hour && a.R.lt.gi == 2 && a.R.lt.shadowLights == 8 && a.R.lt.maxLights == 512, "Settings::apply drives the renderer (hour, GI, shadowed lights, light count)");
    S.applyPreset(a, GfxPreset::Medium);
    chk(a.R.lt.hour == todPresetHour(TodPreset::Dusk), "picking a graphics preset keeps the time of day");
    S.s = keep; S.apply(a);
    chk(a.R.lt.hour == 14.0f && a.R.lt.gi == 1, "back to Day / Medium");
    a.R.lt.probeBudget = 16;
    return true;
  });
  // ---------------------------------------------------------------- clusters
  measure("clusters: 48 lights binned on the GPU = CPU reference", 6, [](App& a) {
    look(a, W({0, 2, 30}), W({0, 1, 0}));
    a.R.testLights.clear();
    uint32_t h = 12345u; auto rnd = [&] { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    vec3 eye = W({0, 2, 30});
    for (int i = 0; i < 48; ++i) {
      LightDesc l; float d = 2 + rnd() * 70, x = (rnd() - 0.5f) * d * 1.1f, y = (rnd() - 0.3f) * d * 0.5f;
      l.pos = eye + vec3(x, y, -d); l.radius = 0.5f + rnd() * 9; l.intensity = 0.2f; l.id = 1000 + i;
      a.R.testLights.push_back(l);
    }
  }, [](App& a) {
    std::vector<int> g, c; glm::ivec3 dims; lightingClusterCounts(g, c, dims);
    int bad = 0, sum = 0, maxc = 0; for (size_t i = 0; i < g.size(); ++i) { bad += g[i] != c[i]; sum += g[i]; maxc = std::max(maxc, g[i]); }
    chk(lightingStats().lights == 48, "48 lights uploaded");
    chk(sum > 200 && bad <= (int)g.size() / 500, "froxels %d x %d x %d: %d light references, max %d in one; GPU = CPU in %zu of %zu (%d differ at float edges)",
        dims.x, dims.y, dims.z, sum, maxc, g.size() - bad, g.size(), bad);
    report["clusters"] = {{"refs", sum}, {"max", maxc}, {"mismatch", bad}};
    // a single small light: exactly the froxels it overlaps
    a.R.testLights.clear();
  });
  // ---------------------------------------------------------------- Day matches the old look
  auto dayView = [](App& a) { look(a, W({9, 4.5f, 16}), W({-6, 1.0f, -3})); };
  measure("Day, probe GI off (the old fill term): reference frame", 240, [=](App& a) { dayView(a); a.R.testLights.clear(); a.R.lt.hour = 14; a.R.lt.gi = 0; }, [](App& a) {
    imgB = px; msNoGi = gpuMs(a); a.screenshot(outDir() / "lights_day_gi_off.png");
  });
  measure("Day, probe GI off again (noise floor)", 30, [](App&) {}, [](App&) { imgB2 = px; });
  measure("Day, Medium probe GI", 400, [](App& a) { a.R.lt.gi = 1; lightingResetProbes(); }, [](App& a) {
    imgA = px; msGi = gpuMs(a); pendingShot = outDir() / "lights_day_medium.png";
    auto cmp = [](const std::vector<uint8_t>& x, const std::vector<uint8_t>& y, double& mean, double& over) {
      mean = 0; over = 0; size_t n = std::min(x.size(), y.size()) / 4;
      for (size_t i = 0; i < n; ++i) { double d = std::fabs(luma(&x[i * 4]) - luma(&y[i * 4])); mean += d; over += d > 12; }
      mean /= std::max<size_t>(n, 1); over /= std::max<size_t>(n, 1);
    };
    double m0, o0, m1, o1; cmp(imgB, imgB2, m0, o0); cmp(imgA, imgB, m1, o1);
    LightingStats s = lightingStats();
    chk(s.probesLive > 300, "probes live: %d of %d (%d below the ground)", s.probesLive, s.probesTotal, s.probesDead);
    // open air: the probe SH reproduces the analytic hemisphere (sky above, ground below)
    vec3 up, dn; float cu = lightingGiAt(W({13, 1.6f, 13}), vec3(0, 1, 0), up), cd = lightingGiAt(W({13, 1.6f, 13}), vec3(0, -1, 0), dn);
    float lu = lumf(up), ld = lumf(dn), su = lumf(vec3(0.30f, 0.36f, 0.47f)), sd = lumf(vec3(0.17f, 0.155f, 0.12f));
    chk(cu > 0.9f && cd > 0.9f && std::fabs(lu / su - 1) < 0.25f && std::fabs(ld / sd - 1) < 0.35f,
        "open-air probe = the analytic sky / ground: up %.3f (sky %.3f), down %.3f (ground %.3f)", lu, su, ld, sd);
    chk(m1 < 2.5 && o1 < 0.04, "Day with Medium GI matches the pre-change look: mean |dL| %.2f levels, %.2f %% of pixels > 12 (noise floor %.2f, %.2f %%)", m1, o1 * 100, m0, o0 * 100);
    report["day_match"] = {{"mean", m1}, {"over12", o1}, {"noise_mean", m0}, {"noise_over12", o0}, {"gpu_ms_gi", msGi}, {"gpu_ms_nogi", msNoGi}};
    note("GPU frame (reported only): Day GI off %.2f ms, Day Medium GI %.2f ms", msNoGi, msGi);
  });
  // inside the cottage by Day (reported only): the old fill term vs Medium probe GI where the sky is hidden
  auto cottage = [](App& a) { vec3 c(-18, a.terrain.height(-18, -24), -24); look(a, c + vec3(-1.2f, 1.6f, -1.0f), c + vec3(1.5f, 1.0f, 1.5f)); };
  measure("Day inside the cottage, GI off", 60, [=](App& a) { cottage(a); a.R.lt.gi = 0; }, [](App& a) { report["cottage_day"]["gi_off"] = meanLuma(); a.screenshot(outDir() / "lights_cottage_gi_off.png"); });
  measure("Day inside the cottage, Medium GI", 300, [](App& a) { a.R.lt.gi = 1; lightingResetProbes(); }, [](App&) {
    double on = meanLuma(), off = report["cottage_day"].value("gi_off", 0.0); report["cottage_day"]["gi_on"] = on;
    chk(on > off * 0.97, "Day inside the stone cottage: Medium GI never darker than the old fill term (mean luma %.1f vs %.1f)", on, off);
    pendingShot = outDir() / "lights_cottage_gi.png";
  });
  // ---------------------------------------------------------------- time-of-day frames
  for (TodPreset p : {TodPreset::Day, TodPreset::Dusk, TodPreset::Night, TodPreset::Dawn}) {
    measure(std::string("frame at ") + todPresetName(p), 90, [=](App& a) {
      look(a, W({9, 3.0f, 16}), W({-4, 4.0f, -3})); a.R.lt.hour = todPresetHour(p); a.R.lt.gi = 1;
    }, [=](App& a) {
      std::string hp = a.R.hdrProbe(); size_t nf = 0; if (auto k = hp.find("non-finite pixels "); k != std::string::npos) nf = std::stoul(hp.substr(k + 18));
      double m = meanLuma(); vec3 sky = skyRgb();
      chk(nf == 0, "%s: no non-finite HDR pixel (%s)", todPresetName(p), hp.c_str());
      report["tod"][todPresetName(p)] = {{"mean", m}, {"sky", js(sky)}, {"gpu_ms", gpuMs(a)}};
      note("%s: mean luma %.1f, sky (%.0f %.0f %.0f), GPU %.2f ms", todPresetName(p), m, sky.x, sky.y, sky.z, gpuMs(a));
      pendingShot = outDir() / (std::string("lights_tod_") + todPresetName(p) + ".png");
    });
  }
  push("time of day: ordering", [](App&, int) {
    auto m = [](const char* n) { return report["tod"][n].value("mean", 0.0); };
    auto sky = [](const char* n) { auto s = report["tod"][n]["sky"]; return vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>()); };
    chk(m("Day") > m("Dusk") && m("Dusk") > m("Night") && m("Day") > m("Night") * 2, "Day %.1f > Dusk %.1f > Night %.1f (mean luma, Creative fixed exposure)", m("Day"), m("Dusk"), m("Night"));
    vec3 d = sky("Day"), du = sky("Dusk"), ni = sky("Night");
    chk(du.x / std::max(du.z, 1.0f) > d.x / std::max(d.z, 1.0f) && lumf(ni) < lumf(d) * 0.4f, "sky: dusk warmer (r/b %.2f vs %.2f), night dark (%.0f vs %.0f)", du.x / std::max(du.z, 1.0f), d.x / std::max(d.z, 1.0f), lumf(ni), lumf(d));
    return true;
  });
  // ---------------------------------------------------------------- the test room at night
  auto inRoom = [](App& a) { look(a, W({-2.2f, 2.2f, -2.2f}), W({1.5f, 1.0f, 1.5f})); };
  measure("night room: lamp off", 220, [=](App& a) { inRoom(a); a.R.lt.hour = 0; a.R.lt.gi = 1; setLamp(a, false); }, [](App& a) {
    lumOff = meanLuma(); a.screenshot(outDir() / "lights_room_off.png");
    lightingGiAt(W({-0.12f, 1.1f, 0}), vec3(-1, 0, 0), giOffAway);
    lightingGiAt(W({0, 1.2f, 2.68f}), vec3(0, 0, -1), giOffIn);
    lightingGiAt(W({0, 1.2f, 3.05f}), vec3(0, 0, 1), giOffOut);
  });
  measure("night room: lamp on", 260, [](App& a) { setLamp(a, true); }, [](App& a) {
    lumOn = meanLuma(); pendingShot = outDir() / "lights_room_on.png";
    chk(lumOn > lumOff * 2.5 && lumOn > 25, "a lit room is brighter than an unlit one: mean luma %.1f vs %.1f", lumOn, lumOff);
    lightingGiAt(W({-0.12f, 1.1f, 0}), vec3(-1, 0, 0), giOnAway);
    lightingGiAt(W({0, 1.2f, 2.68f}), vec3(0, 0, -1), giOnIn);
    float cov = lightingGiAt(W({0, 1.2f, 3.05f}), vec3(0, 0, 1), giOnOut);
    float away = lumf(giOnAway - giOffAway), in = lumf(giOnIn - giOffIn), out = lumf(giOnOut - giOffOut);
    chk(away > 0.002f && lumf(giOnAway) > lumf(giOffAway) * 3, "indirect light reaches the partition face turned away from the lamp: probe irradiance %.4f (lamp off %.4f)", lumf(giOnAway), lumf(giOffAway));
    chk(cov > 0.5f && in > 0.004f && out < in * 0.08f, "no leak through the wall: lamp adds %.5f outside (probe coverage %.2f) vs %.5f inside the same wall", out, cov, in);
    report["room"] = {{"lum_on", lumOn}, {"lum_off", lumOff}, {"gi_away_on", lumf(giOnAway)}, {"gi_away_off", lumf(giOffAway)}, {"gi_in_delta", in}, {"gi_out_delta", out}};
    LightingStats s = lightingStats();
    chk(s.shadowSlots >= 1 && s.lights == 1, "the lamp holds a shadow slot (%d slots, %d lights)", s.shadowSlots, s.lights);
  });
  auto awayView = [](App& a) { look(a, W({-2.3f, 1.4f, 0.3f}), W({-0.1f, 1.2f, 0.0f})); };
  measure("night: partition face away from the lamp, GI on", 160, [=](App& a) { awayView(a); a.R.lt.gi = 1; }, [](App& a) {
    awayGiOn = lumaAt(a, W({-0.1f, 1.2f, 0.0f}), 10); pendingShot = outDir() / "lights_away_gi.png";
  });
  measure("night: partition face away from the lamp, GI off", 40, [](App& a) { a.R.lt.gi = 0; }, [](App& a) {
    awayGiOff = lumaAt(a, W({-0.1f, 1.2f, 0.0f}), 10); a.screenshot(outDir() / "lights_away_nogi.png");
    chk(awayGiOn > awayGiOff * 1.4 && awayGiOn > 8, "the face turned away from the lamp is lit by bounce light: luma %.1f with probe GI, %.1f without", awayGiOn, awayGiOff);
  });
  auto outView = [](App& a) { look(a, W({0, 1.6f, 9.0f}), W({0, 1.4f, 3.0f})); };
  measure("night: outside wall, lamp on", 200, [=](App& a) { outView(a); a.R.lt.gi = 1; setLamp(a, true); }, [](App& a) {
    outOn = lumaAt(a, W({0, 1.4f, 3.0f}), 12); pendingShot = outDir() / "lights_outside_on.png";
  });
  measure("night: outside wall, lamp off", 200, [](App& a) { setLamp(a, false); }, [](App& a) {
    outOff = lumaAt(a, W({0, 1.4f, 3.0f}), 12);
    chk(outOn >= 0 && outOff >= 0 && outOn < outOff * 1.15 + 2, "no light leaks through the wall in the image: outside wall luma %.1f lamp on, %.1f off", outOn, outOff);
  });
  // ---------------------------------------------------------------- light shadow map
  auto shView = [](App& a) { look(a, W({-2.3f, 2.7f, 2.3f}), W({-0.6f, 0.2f, 0.0f})); };
  measure("night: floor behind the partition, light shadows on", 60, [=](App& a) { shView(a); a.R.lt.gi = 0; a.R.lt.shadowLights = 4; setLamp(a, true); }, [](App& a) {
    shadowOn = lumaAt(a, W({-0.6f, 0.2f, 0.0f}), 6); pendingShot = outDir() / "lights_shadow_on.png";
    chk(lightingStats().shadowSlots >= 1, "shadow slot assigned");
  });
  measure("night: floor behind the partition, light shadows off", 30, [](App& a) { a.R.lt.shadowLights = 0; }, [](App& a) {
    shadowOff = lumaAt(a, W({-0.6f, 0.2f, 0.0f}), 6);
    chk(shadowOn >= 0 && shadowOn < shadowOff * 0.65, "the lamp's shadow map darkens the floor behind the partition: luma %.1f with, %.1f without", shadowOn, shadowOff);
    report["shadow"] = {{"on", shadowOn}, {"off", shadowOff}};
    a.R.lt.shadowLights = 4;
  });
  // ---------------------------------------------------------------- emission
  measure("night: emissive material", 60, [](App& a) { look(a, W({8.5f, 1.2f, 0.6f}), W({5.0f, 0.5f, 0.6f})); a.R.lt.gi = 1; setLamp(a, false); }, [](App& a) {
    double g = lumaAt(a, W({5.0f, 0.5f, 0.0f}), 4), w = lumaAt(a, W({5.0f, 0.5f, 1.2f}), 4);
    chk(g > w + 60, "an emissive box glows at night: luma %.1f vs %.1f for the same box without emission", g, w);
    pendingShot = outDir() / "lights_emissive.png";
  });
  measure("night, Medium: frame time with the lamp", 60, [](App& a) { look(a, W({9, 3.0f, 16}), W({-4, 2.0f, -3})); setLamp(a, true); }, [](App& a) {
    report["night_gpu_ms"] = gpuMs(a); note("GPU frame (reported only): Night, Medium, one shadowed lamp %.2f ms", gpuMs(a));
  });
  push("finish", [](App& a, int f) {
    if (f < 5) return false;
    a.R.testLights.clear(); a.R.lt = Renderer::LightQ{};
    std::ofstream(outDir() / "lights.json") << report.dump(2);
    chk(a.R.shaderErrors == 0, "shader errors at the end: %d", a.R.shaderErrors);
    chk(a.R.fboErrors == 0, "framebuffer errors: %d", a.R.fboErrors);
    return true;
  });
}
}  // namespace

bool lightsSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-lights") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("lights"); build(app); cur = 0; sf = 0; }
  if (cur < seq.size()) {
    ++sf;
    if (sf == 1) logf("[lights] step: %s", seq[cur].name.c_str());
    bool done = seq[cur].fn(app, sf);
    total += dt;
    if (done) { ++cur; sf = 0; }
    if (total > 400) { T->check(false, "lights test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
