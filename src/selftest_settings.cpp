// --selftest-settings (docs/settings.md): graphics presets, the settings file, the Settings window and the firing
// smoke. Report: selftest_out/report_settings.txt.
//  * Medium is exactly the built-in defaults (renderer quality flags, shadow and AO targets, resolution, grass,
//    effect caps, FOV, sensitivity), and a fresh game captures as Medium;
//  * Low and High apply live: shadow map sizes, internal resolution, AO resolution, effects off / on, grass, caps,
//    with every framebuffer complete and every shader compiled; a frame at each (screenshots, frame time reported);
//  * the file round-trips every field; a preset name alone loads as that preset; a broken file gives defaults;
//    the self-test never touches the user's settings_user.json;
//  * the window opens from the Creative menu path and the pause menu, and closes when play resumes;
//  * gun smoke: about 8 puffs at alpha <= 0.3 that are gone in 2.4 s; vehicle and station muzzleScale load.
#include "selftest.h"
#include "settings.h"
#include "mptest.h"
#include "tank.h"
#include <chrono>
#include <cstdarg>

namespace {
int gMode = -1;
MpTest* T = nullptr;
int gStep = 0; float gT = 0; int gFrames = 0;
double gLast = 0; double gMs[3] = {0, 0, 0}; int gN[3] = {0, 0, 0};
fs::file_time_type gUserTime{}; bool gUserExisted = false;
std::string fmt(const char* f, ...) { char b[600]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }
double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void checkMediumIsDefault(App& app, const char* when) {
  Renderer::Quality d; const Renderer& R = app.R;
  bool q = R.q.farCascade == d.farCascade && R.q.cloudShadows == d.cloudShadows && R.q.shafts == d.shafts && R.q.ssr == d.ssr && R.q.ssao == d.ssao &&
           R.q.bloom == d.bloom && R.q.taa == d.taa && R.q.taaPhases == d.taaPhases && R.q.shadowSoft == d.shadowSoft && R.q.taaJitter == d.taaJitter;
  T->check(q, fmt("%s: renderer quality flags equal Renderer::Quality defaults", when));
  T->check(R.shadowNearPx() == 2048 && R.shadowFarPx() == 2048 && R.q.shadowNearR == 32.0f && R.q.farEvery == 4 && R.q.shadowTaps == 6 && R.q.leafShadowLodM == 8.0f,
           fmt("%s: P3 Medium shadows: maps %d / %d, near radius 32 m, far every 4th frame, 6 PCF taps, leaf shadow LOD from 8 m", when, R.shadowNearPx(), R.shadowFarPx()));
  { float sc = R.resolutionScale();   // P1: start-up (nothing applied) native; Medium applied: its 0.67 with TAAU
    T->check((sc == 1.0f || sc == GraphicsOpts{}.resScale) && R.width == std::max(16, (int)std::lround(R.displayW * sc)) && R.height == std::max(16, (int)std::lround(R.displayH * sc)) && !R.ssaoFull() && R.q.ssaoDiv == 4 && R.q.probeHalf,
             fmt("%s: resolution scale %.2f (%d x %d of %d x %d), quarter-resolution AO, half the probe capture budget", when, sc, R.width, R.height, R.displayW, R.displayH)); }
  T->check(app.grass.quality == GrassQuality::Medium && R.clouds, fmt("%s: grass Medium, clouds on", when));
  T->check(R.lt.gi == 1 && R.lt.shadowLights == 2 && R.lt.maxLights == 256 && R.lt.hour == 14.0f, fmt("%s: L2 lighting: near probe GI, 2 shadowed lights (P3), 256 lights, Day (14:00)", when));
  T->check(app.fx.maxParts == 900 && app.fx.maxStreaks == 160 && app.fx.maxDebris == 72 && settings().particleBudget() == 1.0f && settings().drawDist() == 1.0f,
           fmt("%s: effect caps 900 / 160 / 72, particle budget and draw distance x1", when));
}

void finishT(App& app) {
  settings().applyPreset(app, GfxPreset::Medium);
  int code = T->finish();
  app.selftest->exitCode = code; app.selftest->done = true;
}
}

bool settingsSelfTest(App& app, float dt) {
  if (gMode < 0) { gMode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-settings") gMode = 1; }
  if (gMode != 1) return false;
  if (dt < 0) return true;   // post-render: screenshots go through app.screenshot below
  Settings& S = settings();
  gT += dt; ++gFrames;
  auto next = [&] { ++gStep; gT = 0; gFrames = 0; };
  switch (gStep) {
  case 0: {   // ---- static checks: defaults, file, getters
    T = new MpTest("settings");
    std::error_code ec;
    gUserExisted = fs::exists(S.path(), ec); if (gUserExisted) gUserTime = fs::last_write_time(S.path(), ec);
    T->check(!S.persist, "self-test run: the user's settings_user.json is neither read nor written");
    T->check(presetOpts(GfxPreset::Medium) == GraphicsOpts{} && UserSettings{}.preset() == GfxPreset::Medium, "the Medium preset is exactly the built-in defaults");
    UserSettings cap; Settings tmp; tmp.capture(app); cap = tmp.s;
    { GraphicsOpts m = presetOpts(GfxPreset::Medium); m.resScale = 1.0f;   // P1: the renderer starts native; Medium's 0.67 arrives when settings apply
      T->check(cap.gfx == m, "a fresh game (nothing applied) captures as Medium at native resolution"); }
    checkMediumIsDefault(app, "start-up");
    T->check(std::fabs(S.lookDegPerCount(0) - 0.1f) < 1e-6f && S.pitchSign() == 1.0f && S.fov() == 72.0f, "defaults: 0.1 deg per count, Y not inverted, FOV 72");
    // presets
    GraphicsOpts lo = presetOpts(GfxPreset::Low), hi = presetOpts(GfxPreset::High);
    T->check(lo.shadowQuality == 0 && !lo.cloudShadows && !lo.shafts && !lo.ssr && !lo.ssao && !lo.bloom && lo.grass == 1 && lo.renderDist == 450.0f && lo.resScale == 0.5f && hi.resScale == 1.0f && GraphicsOpts{}.resScale == 0.67f && lo.particles < 1,
             "Low: shadows 1024 / 1024, no cloud shadows / shafts / SSR / SSAO / bloom, grass Low, render distance 450 m, resolution 0.5 (TAAU), smaller caps; High native, Medium 0.67");
    T->check(hi.shadowQuality == 2 && hi.ssaoFull && hi.grass == 3 && hi.renderDist == 2500.0f && hi.particles > 1 && hi.taaPhases > 8,
             "High: far shadows 4096, full-resolution AO, grass High, render distance 2.5 km, bigger budgets, more jitter phases");
    T->check(hi.msaa == 4 && GraphicsOpts{}.msaa == 0 && lo.msaa == 0 && !lo.ivy && GraphicsOpts{}.ivy && hi.ivy, "B1: 4x MSAA on High only; ivy off on Low, on at Medium / High");
    // file round trip
    UserSettings u; u.gfx = presetOpts(GfxPreset::High); u.gfx.bloom = false; u.gfx.renderDist = 1500.0f; u.fov = 84; u.vsync = false; u.fpsCap = 144;
    u.mouseSens = 0.135f; u.adsSens = 0.7f; u.invertY = true; u.reducedMotion = true; u.showArms = false; u.sightMarkers = true;
    u.master = 0.8f; u.effects = 0.6f; u.ui = 0.4f; u.ambience = 0.2f; u.keys["grenade"] = "G"; u.keys["prone"] = "LEFT_ALT";
    u.uiScale = 1.25f; u.editorFly = 2.5f; u.editorOrbit = 0.75f; u.editorZoom = 1.5f;
    fs::path rt = exeDir() / "selftest_out" / "settings_roundtrip.json";
    fs::create_directories(rt.parent_path(), ec);
    Settings a; a.s = u; bool w = a.save(rt);
    Settings b; bool r = b.load(rt);
    T->check(w && r && b.s == u, "settings file round trip: every field survives save / load (" + rt.filename().string() + ")");
    T->check(u.preset() == GfxPreset::Custom && b.s.toJson()["graphics"].value("preset", "") == "Custom", "a changed preset value is stored and reported as Custom");
    { std::ofstream(rt) << R"({"graphics":{"preset":"Low"}})"; Settings c; c.load(rt);
      T->check(c.s.preset() == GfxPreset::Low && c.s.fov == 72.0f && c.s.mouseSens == 0.1f, "a file naming only a preset loads that preset with default everything else"); }
    { std::ofstream(rt) << R"({"graphics":{"preset":"Medium","drawDistance":1.5}})"; Settings c; c.load(rt);   // PF2: the old multiplier key
      T->check(std::fabs(c.s.gfx.renderDist - 1800.0f) < 0.5f && c.s.toJson()["graphics"].value("renderDistance", 0.0f) == 1800.0f,
               "render distance: an old file's drawDistance x1.5 migrates to 1800 m and is saved as renderDistance"); }
    {   // G1: Ultra grass is GrassQuality 4, appended; saved 0..3 keep their meaning, Ultra survives a save and a fresh load
      bool old = true;
      for (int q = 0; q <= 3; ++q) { std::ofstream(rt) << "{\"graphics\":{\"preset\":\"Medium\",\"grass\":" << q << "}}"; Settings c; c.load(rt); old = old && c.s.gfx.grass == q; }
      Settings c; c.s.gfx = presetOpts(GfxPreset::Medium); c.s.gfx.grass = 4; bool w4 = c.save(rt);
      Settings d; bool r4 = d.load(rt);   // as on the next start
      std::ofstream(rt) << R"({"graphics":{"preset":"Medium","grass":9}})"; Settings e; e.load(rt);
      bool noPreset = true; for (GfxPreset p : {GfxPreset::Low, GfxPreset::Medium, GfxPreset::High}) noPreset = noPreset && presetOpts(p).grass != 4;
      T->check(old && w4 && r4 && d.s.gfx.grass == 4 && d.s.preset() == GfxPreset::Custom && e.s.gfx.grass == 4 && noPreset && std::string(grassPreset(GrassQuality::Ultra).name) == "Ultra",
               "grass Ultra (4): saved Off / Low / Medium / High (0-3) load unchanged, Ultra saves and reloads as 4 (Custom), out-of-range clamps to Ultra, no graphics preset sets it");
    }
    { std::ofstream(rt) << "{ not json"; Settings c; c.s.fov = 90; c.load(rt);
      T->check(c.s == UserSettings{}, "a broken file falls back to the defaults"); }
    { Settings c; c.s.adsSens = 0.5f; c.s.invertY = true;
      T->check(std::fabs(c.lookDegPerCount(1.0f) - 0.05f) < 1e-6f && std::fabs(c.lookDegPerCount(0.5f) - 0.075f) < 1e-6f && c.pitchSign() == -1.0f, "aiming sensitivity blends with the ADS state; invert Y flips the pitch"); }
    // firing smoke
    app.fx.clear();
    app.fx.muzzle(vec3(0, 2, 0), vec3(0, 0, 1), 1.2f);
    int puffs = 0, flashes = 0; float maxA = 0, maxLife = 0;
    for (auto& p : app.fx.parts) { if (p.additive) ++flashes; else { ++puffs; maxA = std::max(maxA, p.col.a); maxLife = std::max(maxLife, p.maxLife); } }
    T->check(puffs >= 6 && puffs <= 10 && flashes == 3 && maxA <= 0.3f && maxLife <= 2.45f && maxLife >= 1.0f,
             fmt("gun discharge smoke (content/settings/fx.json): %d puffs (was 20), alpha <= %.2f (was 0.55), life <= %.1f s (was 5)", puffs, maxA, maxLife));
    for (int k = 0; k < 150; ++k) app.fx.update(1.0f / 60.0f, app.terrain);
    int left = 0; for (auto& p : app.fx.parts) left += !p.additive;
    T->check(left == 0, fmt("the discharge smoke has cleared 2.5 s after the shot (%d puffs left)", left));
    app.fx.clear();
    app.fx.backblast(vec3(0, 2, 0), vec3(0, 0, -1));
    float bbA = 0; int bb = 0; for (auto& p : app.fx.parts) { ++bb; bbA = std::max(bbA, p.col.a); }
    T->check(bb >= 3 && bb <= 6 && bbA <= 0.45f, fmt("launcher backblast: %d puffs at alpha <= %.2f (was 6 at 0.5)", bb, bbA));
    app.fx.clear();
    { auto ti = app.tanks.defs.find("tiger_i"), sf = app.tanks.defs.find("sherman_firefly");
      bool ok = ti != app.tanks.defs.end() && sf != app.tanks.defs.end() && ti->second->muzzleScale == 1.2f && sf->second->muzzleScale == 1.2f;
      T->check(ok, "vehicle JSON gun.muzzleScale loaded (Tiger I, Sherman Firefly: 1.2)"); }
    { const AssetDef* fl = app.lib.find("gun_flak18"); const AssetDef* pk = app.lib.find("gun_pak40");
      T->check(fl && pk && fl->station.muzzleScale == 1.4f && pk->station.muzzleScale == 1.0f, "station muzzleScale: Flak 18 1.4, PaK 40 1.0"); }
    // audio gains
    app.audio.setGains(0.5f, 0.25f, 0.75f, 0.0f);
    T->check(app.audio.masterGain == 0.5f && app.audio.effectsGain == 0.25f && app.audio.uiGain == 0.75f && app.audio.ambienceGain == 0.0f, "audio: master / effects / UI / ambience gains set");
    app.audio.setGains(1, 1, 1, 1);
    // a scene for the preset frames
    const TemplateInfo* t = app.findTemplate("blank_slate");
    if (t) app.newMap(*t, "Settings test");
    const AssetDef* h = app.lib.find("house_farmhouse_2story");
    if (h) app.placeInstance(h, vec3(0, 0, -20), 20.0f);
    app.camPos = vec3(14, 6, 6); app.camYaw = 25; app.camPitch = -8;
    {   // G1: Ultra grass on top of Medium changes the grass only (no shadow map size, probes, MSAA, lights, jitter)
      S.applyPreset(app, GfxPreset::Medium);
      const Renderer& R = app.R;
      int sn = R.shadowNearPx(), sf = R.shadowFarPx(), ms = R.msaa(), gi = R.lt.gi, sl = R.lt.shadowLights, tp = R.q.taaPhases;
      S.s.gfx.grass = 4; S.apply(app);
      T->check(app.grass.quality == GrassQuality::Ultra && R.shadowNearPx() == sn && R.shadowFarPx() == sf && R.msaa() == ms && R.lt.gi == gi && R.lt.shadowLights == sl && R.q.taaPhases == tp,
               fmt("Ultra grass applied live over Medium: grass Ultra; shadows %d / %d, MSAA %d, GI %d, %d shadowed lights, %d jitter phases unchanged", sn, sf, ms, gi, sl, tp));
      S.applyPreset(app, GfxPreset::Medium);
    }
    next(); break;
  }
  case 1: case 2: case 3: {   // ---- Low, Medium, High live: targets, flags, a frame, frame time
    GfxPreset p = gStep == 1 ? GfxPreset::Low : gStep == 2 ? GfxPreset::Medium : GfxPreset::High;
    int idx = (int)p;
    if (gFrames == 1) { S.applyPreset(app, p); gLast = 0; }
    if (gT > 1.5f && gT < 3.0f) { double t = nowS(); if (gLast > 0) { gMs[idx] += (t - gLast) * 1000; ++gN[idx]; } gLast = t; }
    if (gT >= 3.0f) {
      const Renderer& R = app.R;
      if (p == GfxPreset::Low)
        T->check(R.shadowNearPx() == 1024 && R.shadowFarPx() == 1024 && R.q.farEvery == 8 && R.q.shadowTaps == 4 && !R.q.bloom && !R.q.ssao && !R.q.ssr && !R.q.shafts && !R.q.cloudShadows && app.grass.quality == GrassQuality::Low &&
                 R.width == (int)std::lround(R.displayW * 0.5) && app.fx.maxParts == 540 && S.renderDist() == 450.0f && R.lt.gi == 0 && R.lt.shadowLights == 0,
                 fmt("Low applied live: shadows 1024 / 1024 (far every 8th frame, 4 taps), effects and bloom off, grass Low, render %d x %d upscaled to %d x %d, %zu particles", R.width, R.height, R.displayW, R.displayH, app.fx.maxParts));
      if (p == GfxPreset::Medium) checkMediumIsDefault(app, "Medium applied after Low");
      if (p == GfxPreset::High)
        T->check(R.shadowNearPx() == 4096 && R.shadowFarPx() == 4096 && R.ssaoFull() && R.q.taaPhases == 16 && app.grass.quality == GrassQuality::High &&
                 R.width == R.displayW && app.fx.maxParts == 1350 && S.renderDist() == 2500.0f && R.lt.gi == 2 && R.lt.shadowLights == 8 && R.q.shadowNearR == 40.0f && R.q.farEvery == 2 && R.q.shadowTaps == 16 && !R.q.probeHalf,
                 fmt("High applied live (P3: unchanged): shadows 4096 / 4096, 40 m, far every 2nd frame, full PCF, full-resolution AO, 16 jitter phases, grass High, %zu particles", app.fx.maxParts));
      T->check(p == GfxPreset::High ? R.msaa() == 4 && R.msaaActive() : R.msaa() == 0 && !R.msaaActive(),
               fmt("%s: MSAA %s (world target %s)", presetName(p), R.msaa() ? "4x" : "off", R.msaaActive() ? "multisampled" : "single-sample"));
      T->check(R.fboErrors == 0 && R.shaderErrors == 0, fmt("%s: every framebuffer complete, every shader compiled", presetName(p)));
      T->check(S.s.preset() == p, fmt("the settings report the %s preset", presetName(p)));
      T->note(fmt("%s: %.2f ms per frame over %d frames (reported only)", presetName(p), gN[idx] ? gMs[idx] / gN[idx] : 0.0, gN[idx]));
      app.screenshot(exeDir() / "selftest_out" / (std::string("settings_") + presetName(p) + ".png"));
      next();
    }
    break;
  }
  case 4: {   // ---- custom: one change away from a preset
    if (gFrames == 1) { S.s.gfx = presetOpts(GfxPreset::Medium); S.s.gfx.bloom = false; S.apply(app); }
    if (gFrames == 3) {
      T->check(S.s.preset() == GfxPreset::Custom && !app.R.q.bloom, "one value changed from Medium: Custom, applied (bloom off)");
      S.applyPreset(app, GfxPreset::Medium);
      T->check(app.R.q.bloom && S.s.preset() == GfxPreset::Medium, "picking Medium again restores it");
      next();
    }
    break;
  }
  case 5: {   // ---- the window from the Creative menu (settingsMenuItem sets open)
    if (gFrames == 1) { S.windowFrames = 0; S.open = true; }
    if (gFrames == 10) {
      T->check(S.windowFrames >= 8 && S.open, fmt("Settings window opens in Creative (drawn %d frames)", S.windowFrames));
      S.open = false;
      next();
    }
    break;
  }
  case 6: {   // ---- from the Explore pause menu; closes when play resumes
    if (gFrames == 1) { app.doc.start = {true, vec3(10, app.terrain.height(10, 4), 4), 200.0f}; app.enterExplore(); app.useScripted = true; }
    if (gFrames == 5) { app.pauseMenu = true; S.windowFrames = 0; S.open = true; }
    if (gFrames == 15) {
      T->check(app.mode == Mode::Explore && S.windowFrames >= 8 && S.open, fmt("Settings window opens from the pause menu (drawn %d frames)", S.windowFrames));
      app.pauseMenu = false;
    }
    if (gFrames == 18) {
      T->check(!S.open, "resuming play closes the Settings window (the cursor goes back to the game)");
      app.showLoadout = true; S.windowFrames = 0; S.open = true;
    }
    if (gFrames == 28) {
      T->check(S.windowFrames >= 8 && S.open, "Settings window opens from Loadout");
      app.showLoadout = false; S.open = false;
      app.exitExplore();
      next();
    }
    break;
  }
  default: {
    std::error_code ec;
    bool untouched = gUserExisted ? (fs::exists(S.path(), ec) && fs::last_write_time(S.path(), ec) == gUserTime) : !fs::exists(S.path(), ec);
    T->check(untouched, "the user's settings_user.json was not created or changed by the test");
    T->note(fmt("frame time (reported only): Low %.2f ms, Medium %.2f ms, High %.2f ms", gN[0] ? gMs[0] / gN[0] : 0.0, gN[1] ? gMs[1] / gN[1] : 0.0, gN[2] ? gMs[2] / gN[2] : 0.0));
    finishT(app);
    break;
  }
  }
  return true;
}
