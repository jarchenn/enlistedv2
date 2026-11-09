// User settings and the Settings window (docs/settings.md). Values live in Settings::s, are pushed into the running
// game by apply() and are saved to exeDir()/settings_user.json (same place as gore_user.json). Medium is exactly the
// built-in defaults; self-test runs never read or write the file (persist = false) and start on Medium.
#include "settings.h"
#include "app.h"
#include "editor_nav.h"
#include "lighting.h"
#include "platform_gfx.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

// ---------------------------------------------------------------- presets
GraphicsOpts presetOpts(GfxPreset p) {
  GraphicsOpts g;   // Medium: the built-in defaults (Renderer::Quality, Grass, Fx, the cull rules in maps.json)
  if (p == GfxPreset::Low) {
    g.resScale = 0.85f; g.shadowQuality = 0;
    g.cloudShadows = false; g.shafts = false; g.ssr = false; g.ssao = false;
    g.grass = 1; g.drawDist = 0.6f; g.particles = 0.6f; g.ivy = false;
    g.lightCount = 64; g.shadowLights = 0; g.gi = 0;     // L2: the old fill term, no light shadows
  } else if (p == GfxPreset::High) {
    g.shadowQuality = 2; g.ssaoFull = true; g.taaPhases = 16; g.msaa = 4;
    g.grass = 3; g.drawDist = 1.4f; g.particles = 1.5f;
    g.lightCount = 512; g.shadowLights = 8; g.gi = 2;    // L2: near + far probe volumes, faster updates
  }
  return g;
}
const char* presetName(GfxPreset p) { return p == GfxPreset::Low ? "Low" : p == GfxPreset::Medium ? "Medium" : p == GfxPreset::High ? "High" : "Custom"; }

GfxPreset UserSettings::preset() const {
  for (GfxPreset p : {GfxPreset::Low, GfxPreset::Medium, GfxPreset::High}) if (gfx == presetOpts(p)) return p;
  return GfxPreset::Custom;
}

// ---------------------------------------------------------------- file
json UserSettings::toJson() const {
  const GraphicsOpts& g = gfx;
  json k = json::object(); for (auto& [a, n] : keys) k[a] = n;
  return {
    {"version", 1},
    {"graphics", {{"preset", presetName(preset())}, {"resolutionScale", g.resScale}, {"shadowQuality", g.shadowQuality}, {"farCascade", g.farCascade},
                  {"cloudShadows", g.cloudShadows}, {"lightShafts", g.shafts}, {"ssr", g.ssr}, {"ssao", g.ssao}, {"ssaoFullRes", g.ssaoFull},
                  {"bloom", g.bloom}, {"taa", g.taa}, {"taaPhases", g.taaPhases}, {"clouds", g.clouds}, {"grass", g.grass},
                  {"drawDistance", g.drawDist}, {"particleBudget", g.particles}, {"fov", fov}, {"vsync", vsync}, {"fpsCap", fpsCap},
                  {"lightCount", g.lightCount}, {"shadowedLights", g.shadowLights}, {"gi", g.gi}, {"msaa", g.msaa}, {"ivy", g.ivy},
                  {"timeOfDay", todPresetName(todPresetOf(hour))}, {"hour", hour}}},
    {"gameplay", {{"mouseSensitivity", mouseSens}, {"adsSensitivity", adsSens}, {"invertY", invertY}, {"reducedMotion", reducedMotion},
                  {"showArms", showArms}, {"sightMarkers", sightMarkers}}},
    {"audio", {{"master", master}, {"effects", effects}, {"ui", ui}, {"ambience", ambience}}},
    {"controls", k},
    {"interface", {{"uiScale", uiScale}, {"editorFlySpeed", editorFly}, {"editorOrbitSpeed", editorOrbit}, {"editorZoomSpeed", editorZoom}}},
  };
}

UserSettings UserSettings::fromJson(const json& j) {
  UserSettings u;
  if (!j.is_object()) return u;
  if (j.contains("graphics") && j["graphics"].is_object()) {
    const json& q = j["graphics"]; GraphicsOpts& g = u.gfx;
    // a named preset is the starting point; explicit values on top make it Custom when they differ
    std::string pn = q.value("preset", std::string("Medium"));
    g = presetOpts(pn == "Low" ? GfxPreset::Low : pn == "High" ? GfxPreset::High : GfxPreset::Medium);
    g.resScale = std::clamp(q.value("resolutionScale", g.resScale), 0.5f, 1.0f);
    g.shadowQuality = std::clamp(q.value("shadowQuality", g.shadowQuality), 0, 2);
    g.farCascade = q.value("farCascade", g.farCascade); g.cloudShadows = q.value("cloudShadows", g.cloudShadows);
    g.shafts = q.value("lightShafts", g.shafts); g.ssr = q.value("ssr", g.ssr); g.ssao = q.value("ssao", g.ssao); g.ssaoFull = q.value("ssaoFullRes", g.ssaoFull);
    g.bloom = q.value("bloom", g.bloom); g.taa = q.value("taa", g.taa); g.taaPhases = std::clamp(q.value("taaPhases", g.taaPhases), 1, 64);
    g.clouds = q.value("clouds", g.clouds); g.grass = std::clamp(q.value("grass", g.grass), 0, 3);
    g.drawDist = std::clamp(q.value("drawDistance", g.drawDist), 0.3f, 3.0f); g.particles = std::clamp(q.value("particleBudget", g.particles), 0.25f, 4.0f);
    u.fov = std::clamp(q.value("fov", u.fov), 55.0f, 100.0f); u.vsync = q.value("vsync", u.vsync); u.fpsCap = std::clamp(q.value("fpsCap", u.fpsCap), 0, 500);
    g.lightCount = std::clamp(q.value("lightCount", g.lightCount), 0, 4096); g.shadowLights = std::clamp(q.value("shadowedLights", g.shadowLights), 0, 8);
    g.gi = std::clamp(q.value("gi", g.gi), 0, 2);
    g.msaa = q.value("msaa", g.msaa) >= 2 ? 4 : 0; g.ivy = q.value("ivy", g.ivy);
    // time of day: a preset name gives its hour; an explicit hour wins
    std::string tn = q.value("timeOfDay", std::string("Day"));
    for (TodPreset p : {TodPreset::Day, TodPreset::Dusk, TodPreset::Night, TodPreset::Dawn}) if (tn == todPresetName(p)) u.hour = todPresetHour(p);
    float h = q.value("hour", u.hour); if (std::isfinite(h)) u.hour = std::fmod(std::fmod(h, 24.0f) + 24.0f, 24.0f);
  }
  if (j.contains("gameplay") && j["gameplay"].is_object()) {
    const json& q = j["gameplay"];
    u.mouseSens = std::clamp(q.value("mouseSensitivity", u.mouseSens), 0.01f, 1.0f); u.adsSens = std::clamp(q.value("adsSensitivity", u.adsSens), 0.2f, 2.0f);
    u.invertY = q.value("invertY", u.invertY); u.reducedMotion = q.value("reducedMotion", u.reducedMotion);
    u.showArms = q.value("showArms", u.showArms); u.sightMarkers = q.value("sightMarkers", u.sightMarkers);
  }
  if (j.contains("audio") && j["audio"].is_object()) {
    const json& q = j["audio"];
    u.master = std::clamp(q.value("master", u.master), 0.0f, 1.0f); u.effects = std::clamp(q.value("effects", u.effects), 0.0f, 1.0f);
    u.ui = std::clamp(q.value("ui", u.ui), 0.0f, 1.0f); u.ambience = std::clamp(q.value("ambience", u.ambience), 0.0f, 1.0f);
  }
  if (j.contains("controls") && j["controls"].is_object())
    for (auto it = j["controls"].begin(); it != j["controls"].end(); ++it) if (it->is_string()) u.keys[it.key()] = it->get<std::string>();
  if (j.contains("interface") && j["interface"].is_object()) {
    const json& q = j["interface"];
    u.uiScale = std::clamp(q.value("uiScale", u.uiScale), 0.75f, 2.0f);
    u.editorFly = std::clamp(q.value("editorFlySpeed", u.editorFly), 0.1f, 10.0f);
    u.editorOrbit = std::clamp(q.value("editorOrbitSpeed", u.editorOrbit), 0.25f, 4.0f);
    u.editorZoom = std::clamp(q.value("editorZoomSpeed", u.editorZoom), 0.25f, 4.0f);
  }
  return u;
}

bool Settings::load(const fs::path& p) {
  std::error_code ec;
  if (!fs::exists(p, ec)) return false;
  try { s = UserSettings::fromJson(json::parse(readTextFile(p))); }
  catch (std::exception& e) { logf("settings: %s: %s (defaults)", p.string().c_str(), e.what()); s = UserSettings{}; return false; }
  logf("settings: %s (graphics %s)", p.string().c_str(), presetName(s.preset()));
  return true;
}
bool Settings::save(const fs::path& p) const {
  std::ofstream f(p);
  if (!f) { logf("settings: cannot write %s", p.string().c_str()); return false; }
  f << s.toJson().dump(2) << "\n";
  return (bool)f;
}

Settings& settings() { static Settings S; return S; }

// ---------------------------------------------------------------- keys (rebindable Explore actions)
namespace {
struct KeyName { int key; const char* name; };
const std::vector<KeyName>& keyNames() {
  static std::vector<KeyName> v = [] {
    static std::vector<std::string> store;   // stable storage for the generated names
    std::vector<KeyName> k;
    store.reserve(64);
    for (char c = 'A'; c <= 'Z'; ++c) store.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) store.push_back(std::string(1, c));
    for (int i = 0; i < 26; ++i) k.push_back({GLFW_KEY_A + i, store[i].c_str()});
    for (int i = 0; i < 10; ++i) k.push_back({GLFW_KEY_0 + i, store[26 + i].c_str()});
    const KeyName named[] = {{GLFW_KEY_LEFT_CONTROL, "LEFT_CONTROL"}, {GLFW_KEY_RIGHT_CONTROL, "RIGHT_CONTROL"}, {GLFW_KEY_LEFT_SHIFT, "LEFT_SHIFT"},
      {GLFW_KEY_RIGHT_SHIFT, "RIGHT_SHIFT"}, {GLFW_KEY_LEFT_ALT, "LEFT_ALT"}, {GLFW_KEY_TAB, "TAB"}, {GLFW_KEY_CAPS_LOCK, "CAPS_LOCK"},
      {GLFW_KEY_SPACE, "SPACE"}, {GLFW_KEY_F6, "F6"}, {GLFW_KEY_F7, "F7"}, {GLFW_KEY_F8, "F8"}, {GLFW_KEY_F9, "F9"}, {GLFW_KEY_F10, "F10"},
      {GLFW_KEY_ESCAPE, "ESCAPE"}, {GLFW_KEY_F1, "F1"}, {GLFW_KEY_F2, "F2"}, {GLFW_KEY_F3, "F3"}, {GLFW_KEY_F4, "F4"}, {GLFW_KEY_F5, "F5"},
      {GLFW_KEY_BACKSPACE, "BACKSPACE"}, {GLFW_KEY_ENTER, "ENTER"}, {GLFW_KEY_UP, "UP"}, {GLFW_KEY_DOWN, "DOWN"}};
    for (auto& n : named) k.push_back(n);
    return k;
  }();
  return v;
}
int keyCode(const std::string& n) { for (auto& k : keyNames()) if (n == k.name) return k.key; return GLFW_KEY_UNKNOWN; }
// keys the game reads directly (explore.cpp, weapons, tanks, the editor): shown in the Controls tab, refused as bindings
struct FixedKey { const char* name; const char* what; };
const FixedKey kFixed[] = {
  {"W", "forward (rack: up)"}, {"A", "left"}, {"S", "back (rack: down)"}, {"D", "right"}, {"LEFT_SHIFT", "sprint"}, {"SPACE", "jump / stand up from prone (tank: bow MG)"},
  {"C", "crouch (with the crouch binding)"}, {"E", "interact / enter"}, {"R", "reload"}, {"Q", "next weapon"}, {"T", "switch gun position"},
  {"X", "AT / HE ammunition"}, {"V", "scope irons"}, {"B", "build (Explore)"}, {"L", "loadout / range"}, {"P", "pause (drop-in)"},
  {"1", "choose 1"}, {"2", "choose 2"}, {"UP", "rack up"}, {"DOWN", "rack down"}, {"ENTER", "rack: equip"}, {"BACKSPACE", "return to start"},
  {"F3", "performance stats"}, {"F4", "damage debug"}, {"F5", "Explore / Creative"}, {"F6", "match debug overlay"}, {"ESCAPE", "menu / back"},
};
const char* fixedUse(const std::string& n) { for (auto& f : kFixed) if (n == f.name) return f.what; return nullptr; }
struct Action { const char* id; const char* label; };
const Action kActions[] = {{"fireMode", "Fire mode"}, {"prone", "Prone"}, {"crouch", "Crouch"}, {"packUp", "Deploy / pack up heavy weapon"},
                           {"grenade", "Throw grenade"}, {"smokeGrenade", "Smoke grenade (match)"}};
int* bindSlot(App& app, const std::string& a) {
  auto& b = app.binds;
  return a == "fireMode" ? &b.fireMode : a == "prone" ? &b.prone : a == "crouch" ? &b.crouch : a == "packUp" ? &b.packUp : a == "grenade" ? &b.grenade : a == "smokeGrenade" ? &b.smokeGrenade : nullptr;
}
bool gSelftest = false;
NavCamSettings gNavBase; bool gNavBaseSet = false;
}

void settingsApplyKeys(App& app) {   // arsenal.cpp: end of App::loadBindings
  for (auto& [a, n] : settings().s.keys) {
    int* slot = bindSlot(app, a); int k = keyCode(n);
    if (!slot || k == GLFW_KEY_UNKNOWN || fixedUse(n)) { logf("settings: ignoring binding %s = %s", a.c_str(), n.c_str()); continue; }
    *slot = k; app.binds.names[a] = n;
  }
}

// ---------------------------------------------------------------- apply / capture
void Settings::apply(App& app) {
  ++applied;
  const GraphicsOpts& g = s.gfx;
  Renderer& R = app.R;
  R.setResolutionScale(g.resScale);
  R.setShadowSizes(g.shadowQuality == 0 ? 2048 : 4096, g.shadowQuality == 0 ? 1024 : g.shadowQuality == 2 ? 4096 : 2048);
  R.setSsaoFull(g.ssaoFull);
  R.q.farCascade = g.farCascade; R.q.cloudShadows = g.cloudShadows; R.q.shafts = g.shafts; R.q.ssr = g.ssr; R.q.ssao = g.ssao; R.q.bloom = g.bloom;
  if (R.q.taa != g.taa || R.q.taaPhases != g.taaPhases) R.resetTemporal();
  R.q.taa = g.taa; R.q.taaPhases = g.taaPhases;
  R.setMsaa(g.msaa);
  app.ivyOn = g.ivy;
  R.clouds = g.clouds;
  R.lt.maxLights = g.lightCount; R.lt.shadowLights = g.shadowLights; R.lt.gi = g.gi; R.lt.hour = s.hour;   // L2 lighting
  app.grass.quality = (GrassQuality)std::clamp(g.grass, 0, 3);
  app.fx.setBudget(g.particles);
  if (!gSelftest) platformSwapInterval(s.vsync ? 1 : 0);
  app.reducedMotion = s.reducedMotion; app.vm.showArms = s.showArms; app.sightMarkers = s.sightMarkers;
  app.audio.setGains(s.master, s.effects, s.ui, s.ambience);
  if (ImGui::GetCurrentContext()) ImGui::GetIO().FontGlobalScale = s.uiScale;
  // editor camera: fly speed is the Alt+wheel multiplier; orbit / zoom scale the content/settings/editor.json values
  NavCamSettings& nav = const_cast<NavCamSettings&>(navSettings());
  if (!gNavBaseSet) { gNavBase = nav; gNavBaseSet = true; }
  nav.orbitDegPerPixel = gNavBase.orbitDegPerPixel * s.editorOrbit;
  nav.zoomStep = std::clamp(gNavBase.zoomStep * s.editorZoom, 0.02f, 0.6f);
  navState().speedMult = s.editorFly;
}

void Settings::capture(const App& app) {
  GraphicsOpts& g = s.gfx; const Renderer& R = app.R;
  g.resScale = R.resolutionScale();
  g.shadowQuality = R.shadowNearPx() < 4096 ? 0 : R.shadowFarPx() >= 4096 ? 2 : 1;
  g.farCascade = R.q.farCascade; g.cloudShadows = R.q.cloudShadows; g.shafts = R.q.shafts; g.ssr = R.q.ssr; g.ssao = R.q.ssao; g.bloom = R.q.bloom;
  g.ssaoFull = R.ssaoFull(); g.taa = R.q.taa; g.taaPhases = R.q.taaPhases; g.clouds = R.clouds;
  g.lightCount = R.lt.maxLights; g.shadowLights = R.lt.shadowLights; g.gi = R.lt.gi; s.hour = R.lt.hour;
  g.grass = (int)app.grass.quality;
  g.msaa = R.msaa(); g.ivy = app.ivyOn;
  s.reducedMotion = app.reducedMotion; s.showArms = app.vm.showArms; s.sightMarkers = app.sightMarkers;
  s.editorFly = navState().speedMult;
}

// ---------------------------------------------------------------- hooks
void settingsInit(App& app, bool selftest) {
  Settings& S = settings();
  gSelftest = selftest;
  S.persist = !selftest;
  if (selftest) {   // self-tests: Medium defaults, nothing read or written, nothing applied
    if (const char* e = std::getenv("NMK_RES_SCALE")) app.R.setResolutionScale((float)std::atof(e));   // benchmarks at a chosen scale
    return;
  }
  if (!S.load(S.path())) {
#ifdef __APPLE__
    // first run on a Mac: Medium with the 3D scene at 60 % of the Retina window (MetalFX upscales it on Metal: crisper
    // than bilinear, and the UI stays at full sharpness). Saved settings are kept as they are
    S.s.gfx = presetOpts(GfxPreset::Medium); S.s.gfx.resScale = 0.6f;
#endif
  }
  S.apply(app);
}

void settingsEndFrame(App& app) {
  (void)app;
  using clk = std::chrono::steady_clock;
  static clk::time_point last = clk::now();
  int cap = settings().s.fpsCap;
  if (cap > 0 && !gSelftest) {
    auto target = last + std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(1.0 / cap));
    auto now = clk::now();
    if (target > now + std::chrono::milliseconds(2)) std::this_thread::sleep_for(target - now - std::chrono::milliseconds(2));
    while (clk::now() < target) std::this_thread::yield();
    last = std::max(target, clk::now() - std::chrono::milliseconds(50));   // no catch-up burst after a long frame
  } else last = clk::now();
}

bool settingsMenuItem() {
  if (ImGui::MenuItem("Settings...")) { settings().open = true; return true; }
  return false;
}

// ---------------------------------------------------------------- window
void settingsWindow(App& app) {
  Settings& S = settings();
  static bool wasOpen = false, pendingSave = false;
  if (S.open && app.mode == Mode::Explore && !app.pauseMenu && !app.showLoadout) S.open = false;   // back in play: the cursor is captured
  if (!S.open) { S.rebinding.clear(); if (wasOpen || pendingSave) S.save(); wasOpen = pendingSave = false; return; }
  if (!wasOpen) S.capture(app);   // on opening: the editor View menu / Loadout may have changed something
  wasOpen = true;
  UserSettings& u = S.s;
  const UserSettings before = u;
  bool applyNow = false, saveNow = false;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (!ImGui::Begin("Settings", &S.open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
  ++S.windowFrames;
  if (ImGui::BeginTabBar("##settabs")) {
    // ------------------------------------------------ graphics
    if (ImGui::BeginTabItem("Graphics")) {
      GraphicsOpts& g = u.gfx;
      int pr = (int)u.preset(); const char* pn[] = {"Low", "Medium", "High", "Custom"};
      ImGui::SetNextItemWidth(200);
      if (ImGui::Combo("Preset", &pr, pn, 4) && pr != (int)GfxPreset::Custom) g = presetOpts((GfxPreset)pr);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Medium is the standard look. Low: half-size shadows, no cloud shadows / light shafts / reflections / AO,\nless grass and decoration, 85 %% resolution. High: sharper far shadows, full-resolution AO, more grass, longer view.");
      ImGui::Separator();
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Resolution scale", &g.resScale, 0.5f, 1.0f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit()) applyNow = true;   // targets are recreated once, on release
      { const char* sq[] = {"Low (2048 / 1024)", "Medium (4096 / 2048)", "High (4096 / 4096)"}; ImGui::SetNextItemWidth(200); ImGui::Combo("Shadow quality", &g.shadowQuality, sq, 3); }
      ImGui::Checkbox("Far shadow cascade", &g.farCascade); ImGui::SameLine(220); ImGui::Checkbox("Cloud shadows", &g.cloudShadows);
      ImGui::Checkbox("Ambient occlusion", &g.ssao); ImGui::SameLine(220); ImGui::BeginDisabled(!g.ssao); ImGui::Checkbox("Full-resolution AO", &g.ssaoFull); ImGui::EndDisabled();
      ImGui::Checkbox("Screen-space reflections", &g.ssr); ImGui::SameLine(220); ImGui::Checkbox("Bloom", &g.bloom);
      ImGui::Checkbox("Light shafts", &g.shafts); ImGui::SameLine(220); ImGui::Checkbox("Temporal AA", &g.taa);
      { bool m = g.msaa >= 2; if (ImGui::Checkbox("4x MSAA", &m)) g.msaa = m ? 4 : 0; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multisampled world geometry (smoother building and roof edges), resolved before temporal AA. High turns it on.");
      ImGui::SameLine(220); ImGui::Checkbox("Ivy on walls", &g.ivy);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ivy on stone walls; its density follows the grass quality.");
      ImGui::Checkbox("Clouds", &g.clouds);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cloud cover: Creative > View > Sky");
      { const char* gq[] = {"Off", "Low", "Medium", "High"}; ImGui::SetNextItemWidth(200); ImGui::Combo("Grass", &g.grass, gq, 4); }
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Draw distance", &g.drawDist, 0.4f, 2.0f, "x %.2f");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scales the small-decoration cull distances and the far draw cuts (nature, trees, buildings).");
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Particle budget", &g.particles, 0.25f, 2.5f, "x %.2f");
      ImGui::SeparatorText("Lighting");
      {   // L2 (docs/lighting.md): time of day is personal (kept when a preset is picked); the rest belongs to the presets
        int tp = (int)todPresetOf(u.hour); const char* tn[] = {"Day", "Dusk", "Night", "Dawn", "Custom"};
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Time of day", &tp, tn, 5) && tp != (int)TodPreset::Custom) u.hour = todPresetHour((TodPreset)tp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Day is the standard look. Dusk and Dawn: low sun, long shadows. Night: moon, stars, lamps.");
        int hh = (int)u.hour, mm = (int)std::lround((u.hour - hh) * 60); if (mm == 60) { ++hh; mm = 0; }
        char hl[32]; std::snprintf(hl, sizeof hl, "%02d:%02d", hh % 24, mm);
        ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Hour", &u.hour, 0.0f, 23.99f, hl);
        ImGui::SetNextItemWidth(200); ImGui::SliderInt("Light count", &g.lightCount, 0, 1024);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lamps, lanterns and fires lit each frame (nearest first).");
        { int sl[] = {0, 2, 4, 8}; const char* sn[] = {"Off", "2", "4", "8"}; int si = 0; for (int k = 0; k < 4; ++k) if (sl[k] == g.shadowLights) si = k;
          ImGui::SetNextItemWidth(200); if (ImGui::Combo("Shadowed lights", &si, sn, 4)) g.shadowLights = sl[si]; }
        { const char* gn[] = {"Off", "Medium", "High"}; ImGui::SetNextItemWidth(200); ImGui::Combo("Indirect light", &g.gi, gn, 3);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Probe light around the camera: lamplight and window light bounce onto walls and ceilings.\nOff: the flat fill of earlier versions."); }
      }
      ImGui::Separator();
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Field of view", &u.fov, 55.0f, 100.0f, "%.0f deg");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical field of view on foot (72 standard). Aiming and gun sights narrow from it.");
      ImGui::Checkbox("Vsync", &u.vsync); ImGui::SameLine(220);
      { int caps[] = {0, 30, 60, 90, 120, 144, 165, 240}; const char* cn[] = {"Unlimited", "30", "60", "90", "120", "144", "165", "240"};
        int ci = 0; for (int k = 0; k < 8; ++k) if (caps[k] == u.fpsCap) ci = k;
        ImGui::SetNextItemWidth(120); if (ImGui::Combo("FPS cap", &ci, cn, 8)) u.fpsCap = caps[ci]; }
      ImGui::TextDisabled("Frame %.2f ms (%.0f FPS) | render %d x %d of %d x %d", io.DeltaTime * 1000, io.Framerate, app.R.width, app.R.height, app.R.displayW, app.R.displayH);
      ImGui::EndTabItem();
    }
    // ------------------------------------------------ gameplay
    if (ImGui::BeginTabItem("Gameplay")) {
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Mouse sensitivity", &u.mouseSens, 0.02f, 0.4f, "%.3f deg/count");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees per mouse count in the standard 72 deg view (0.100 default). Matched to the view, so\nirons, scopes and tank sights move the picture by the same distance per count.");
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Aiming sensitivity", &u.adsSens, 0.3f, 1.5f, "x %.2f");
      ImGui::Checkbox("Invert mouse Y", &u.invertY);
      ImGui::Checkbox("Reduced motion (keeps aim displacement)", &u.reducedMotion);
      ImGui::Checkbox("Show arms", &u.showArms);
      ImGui::Checkbox("Sight calibration markers", &u.sightMarkers);
      { extern void goreOptionsUI(); goreOptionsUI(); }   // gore_user.json (gore.cpp)
      ImGui::EndTabItem();
    }
    // ------------------------------------------------ audio
    if (ImGui::BeginTabItem("Audio")) {
      auto vol = [&](const char* l, float& v) { int p = (int)std::lround(v * 100); ImGui::SetNextItemWidth(260); if (ImGui::SliderInt(l, &p, 0, 100, "%d %%")) v = p / 100.0f; };
      vol("Master", u.master); vol("Effects", u.effects); vol("Interface", u.ui); vol("Ambience (wind, leaves)", u.ambience);
      if (!app.audio.ok()) ImGui::TextDisabled("No audio device.");
      ImGui::EndTabItem();
    }
    // ------------------------------------------------ controls
    if (ImGui::BeginTabItem("Controls")) {
      ImGui::TextWrapped("Rebindable Explore actions (defaults in content/settings/bindings.json). Click a key, then press the new one (Esc cancels).");
      for (auto& a : kActions) {
        auto it = app.binds.names.find(a.id); std::string cur = it == app.binds.names.end() ? "?" : it->second;
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(a.label); ImGui::SameLine(260);
        std::string lbl = (S.rebinding == a.id ? std::string("press a key...") : cur) + "##" + a.id;
        if (ImGui::Button(lbl.c_str(), ImVec2(150, 0))) { S.rebinding = a.id; S.bindMsg.clear(); }
      }
      if (!S.rebinding.empty()) {
        for (auto& k : keyNames()) {
          if (glfwGetKey(app.win, k.key) != GLFW_PRESS) continue;
          std::string n = k.name;
          if (n == "ESCAPE") { S.rebinding.clear(); break; }
          if (const char* f = fixedUse(n)) { S.bindMsg = n + " is a fixed key (" + f + ")"; break; }
          std::string other; for (auto& [a, nm] : app.binds.names) if (nm == n && a != S.rebinding) other = a;
          if (!other.empty()) { S.bindMsg = n + " is already bound to " + other; break; }
          u.keys[S.rebinding] = n; S.bindMsg = S.rebinding + " = " + n; S.rebinding.clear();
          break;
        }
      }
      if (!S.bindMsg.empty()) ImGui::TextColored(ImVec4(1, 0.82f, 0.45f, 1), "%s", S.bindMsg.c_str());
      if (ImGui::Button("Reset keys to bindings.json")) { u.keys.clear(); app.loadBindings(); S.bindMsg = "keys reset"; saveNow = true; }
      if (ImGui::CollapsingHeader("Fixed keys")) {
        ImGui::TextDisabled("Mouse: LMB fire, RMB sights (tank: zoom), wheel: tank sight range");
        for (auto& f : kFixed) { ImGui::BulletText("%-10s %s", f.name, f.what); }
      }
      ImGui::EndTabItem();
    }
    // ------------------------------------------------ interface
    if (ImGui::BeginTabItem("Interface")) {
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("UI scale", &u.uiScale, 0.75f, 2.0f, "x %.2f");
      if (ImGui::IsItemDeactivatedAfterEdit()) applyNow = true;   // resizing the window while dragging its own slider: on release
      ImGui::SeparatorText("Editor camera");
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Fly speed", &u.editorFly, 0.1f, 10.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also Alt+wheel in the editor.");
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Orbit speed", &u.editorOrbit, 0.25f, 4.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Zoom step", &u.editorZoom, 0.25f, 4.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::Separator();
  if (ImGui::Button("Defaults (Medium)")) { std::map<std::string, std::string> k = u.keys; u = UserSettings{}; u.keys = k; applyNow = true; }
  ImGui::SameLine();
  if (ImGui::Button("Close")) S.open = false;
  ImGui::SameLine();
  ImGui::TextDisabled(S.persist ? "saved to settings_user.json" : "self-test: not saved");
  ImGui::End();
  // apply: immediately for everything except the two sliders that rebuild targets / the UI (on release)
  UserSettings cmp = u; cmp.gfx.resScale = before.gfx.resScale; cmp.uiScale = before.uiScale;
  if (!(cmp == before)) applyNow = true;
  if (u.keys != before.keys) { app.loadBindings(); saveNow = true; }   // re-reads bindings.json, then the overrides (settingsApplyKeys)
  if (applyNow) { S.apply(app); saveNow = true; }
  pendingSave = pendingSave || saveNow;
  if (pendingSave && !ImGui::IsAnyItemActive()) { S.save(); pendingSave = false; }   // not every frame of a slider drag
}
