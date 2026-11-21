// User settings and the Settings window (docs/settings.md). Values live in Settings::s, are pushed into the running
// game by apply() and are saved to exeDir()/settings_user.json (same place as gore_user.json). Medium is exactly the
// built-in defaults; self-test runs never read or write the file (persist = false) and start on Medium.
#include "user_cache.h"
#include "settings.h"
#include "ui_flat.h"
#include "mainmenu.h"
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
// P3 A/B: NMK_PRESET_OLD=1 gives the pre-P3 Low / Medium values (presets, the shadow / probe / SSAO knobs, grass Low radius)
bool presetsOld() { static const bool o = std::getenv("NMK_PRESET_OLD") != nullptr; return o; }
// CP preset re-cut (corpus 13.10, docs/corpus-implementation.md): Medium is the art-directed baseline; High adds samples
// and range; Low removes density and resolution, never a cue: every preset keeps near sun shadows, contact shadows,
// ambient occlusion (Low / Ultra Low at quarter resolution), haze (the weather's, the same everywhere) and temporal AA.
// Fixed scales are the dynamic-resolution floors: Medium 0.77, Low 0.67 (the corpus's 1080p floor; it was 0.5)
GraphicsOpts presetOpts(GfxPreset p) {
  GraphicsOpts g;   // Medium: the built-in defaults (Renderer::Quality, Grass, Fx, the cull rules in maps.json)
  if (p == GfxPreset::Medium) g.resScale = 0.77f;   // CP: was 0.67
  if (presetsOld() && p != GfxPreset::High) g.shadowLights = 4;
  if (p == GfxPreset::Low) {
    g.resScale = 0.67f; g.shadowQuality = 0;   // P1: TAAU reconstructs the output; CP: 0.67, the corpus's 1080p floor (was 0.5)
    g.shafts = false; g.ssr = false; g.ssao = true;   // WX1: cloud shadows stay on (every preset sees the same sun patches); CP: AO stays (quarter resolution), a cue
    g.cloudQuality = 0;
    g.bloom = presetsOld();                              // P3: no bloom on Low
    g.grass = 1; g.grassWind = false; g.renderDist = presetsOld() ? 600 : 450; g.particles = 0.6f; g.ivy = false; g.dustMotes = 0;
    g.lightCount = 64; g.shadowLights = 0; g.gi = 0;     // L2: the old fill term, no light shadows
    g.materials = 0;                                     // GX5: base materials only (no layer, no static decals)
  } else if (p == GfxPreset::UltraLow) {   // PERF1: below Low - no reflections / AO / bloom / shafts / cloud shadows, static vegetation, shortest distances, lowest scale
    g.resScale = 0.5f; g.shadowQuality = 0;
    g.shafts = false; g.ssr = false; g.ssao = true; g.bloom = false; g.cloudShadows = false;   // CP: AO stays (quarter resolution): a grounding cue
    g.cloudQuality = 0;
    g.grass = 1; g.grassWind = false; g.renderDist = 300; g.particles = 0.4f; g.ivy = false; g.dustMotes = 0;
    g.lightCount = 32; g.shadowLights = 0; g.gi = 0;
    g.materials = 0;
  } else if (p == GfxPreset::High) {
    g.resScale = 1.0f; g.shadowQuality = 2; g.ssaoFull = true; g.taaPhases = 16; g.msaa = 0;   // P1: High stays native; VO P2: TAA alone (4x MSAA under the TAA cost ms for edges the TAA already resolves; the Video tab still offers it)
    g.shafts = true;                                     // WX1: the marched sun shafts are High only (Low / Medium: the fog's sun glow)
    g.grass = 3; g.renderDist = 2500; g.particles = 1.5f; g.dustMotes = 2;
    g.lightCount = 512; g.shadowLights = 8; g.gi = 2;    // L2: near + far probe volumes, faster updates
    g.materials = 2;                                     // GX5: + parallax on height-capable texture sets
    g.cloudQuality = 2;                                  // WX1: + the high thin cloud layer
  } else if (p == GfxPreset::Ultra) {   // UL2: High plus only what showed in the High | Ultra sheets (UL1 step 1 dropped 6144 maps, every cascade every frame, POM 20 m)
    g = presetOpts(GfxPreset::High); g.ultraFx = true;
  }
  return g;
}
const char* presetName(GfxPreset p) { return p == GfxPreset::UltraLow ? "Ultra Low" : p == GfxPreset::Low ? "Low" : p == GfxPreset::Medium ? "Medium" : p == GfxPreset::High ? "High" : p == GfxPreset::Ultra ? "Ultra" : "Custom"; }
GfxPreset presetFromName(const std::string& n) { for (GfxPreset p : {GfxPreset::UltraLow, GfxPreset::Low, GfxPreset::High, GfxPreset::Ultra}) if (n == presetName(p)) return p; return GfxPreset::Medium; }

GfxPreset UserSettings::preset() const {
  for (GfxPreset p : {GfxPreset::UltraLow, GfxPreset::Low, GfxPreset::Medium, GfxPreset::High, GfxPreset::Ultra}) if (gfx == presetOpts(p)) return p;
  return GfxPreset::Custom;
}

// ---------------------------------------------------------------- file
static const char* const kThemeNames[4] = {"wwii", "postapoc", "modern", "desert"};   // TH1: as palette.cpp kThemeKeys

json UserSettings::toJson() const {
  const GraphicsOpts& g = gfx;
  json k = json::object(); for (auto& [a, n] : keys) k[a] = n;
  return {
    {"version", 1},
    {"graphics", {{"preset", presetName(preset())}, {"resolutionScale", g.resScale}, {"shadowQuality", g.shadowQuality}, {"farCascade", g.farCascade},
                  {"cloudShadows", g.cloudShadows}, {"lightShafts", g.shafts}, {"ssr", g.ssr}, {"ssao", g.ssao}, {"ssaoFullRes", g.ssaoFull},
                  {"bloom", g.bloom}, {"taa", g.taa}, {"taaPhases", g.taaPhases}, {"clouds", g.clouds}, {"cloudQuality", g.cloudQuality}, {"grass", g.grass}, {"grassWind", g.grassWind},
                  {"renderDistance", g.renderDist}, {"particleBudget", g.particles}, {"fov", fov}, {"gamma", gamma}, {"microcontrast", microcontrast}, {"sharpness", sharpness}, {"lighting", !lightNew ? "classic" : lightPhys ? "physical" : "new"}, {"vsync", vsync}, {"dynamicResolution2", dynRes}, {"fpsCap", fpsCap}, {"upscaling", upscale == 0 ? "temporal" : upscale == 1 ? "spatial" : "off"},
                  {"lightCount", g.lightCount}, {"shadowedLights", g.shadowLights}, {"gi", g.gi}, {"msaa", g.msaa}, {"ivy", g.ivy}, {"materialDetail", g.materials}, {"ultraEffects", g.ultraFx}, {"dustMotes", g.dustMotes},
                  {"timeOfDay", todPresetName(todPresetOf(hour))}, {"hour", hour}, {"weather", wxPresetName(weather)}, {"envFog", envFog}}},
    {"gameplay", {{"mouseSensitivity", mouseSens}, {"adsSensitivity", adsSens}, {"invertY", invertY}, {"reducedMotion", reducedMotion},
                  {"showArms", showArms}, {"sightMarkers", sightMarkers}, {"weaponList", weaponList}, {"friendlyMarkers", friendlyMarkers}, {"heliCameraMotion", heliCamMotion}, {"vehicleWorldBars", vehicleBars}, {"reticleSize", reticleSize}, {"reticleBright", reticleBright}}},
    {"audio", {{"master", master}, {"effects", effects}, {"ui", ui}, {"ambience", ambience}, {"gunfire", gunfire}, {"heartbeat", heartbeat}}},
    {"controls", k},
    {"player", {{"character", character}, {"primary", primary}}},
    {"build", {{"hotbar", hotbar}, {"themes", [&] { json t = json::array(); for (int k = 0; k < 4; ++k) if (themes & (1u << k)) t.push_back(kThemeNames[k]); return t; }()}}},   // UI12; TH1
    {"interface", {{"uiScale", uiScale}, {"minimapRotate", minimapRotate}, {"developerOverlay", devOverlay}, {"editorFlySpeed", editorFly}, {"editorOrbitSpeed", editorOrbit}, {"editorZoomSpeed", editorZoom}}},
  };
}

UserSettings UserSettings::fromJson(const json& j) {
  UserSettings u;
  if (!j.is_object()) return u;
  if (j.contains("graphics") && j["graphics"].is_object()) {
    const json& q = j["graphics"]; GraphicsOpts& g = u.gfx;
    // a named preset is the starting point; explicit values on top make it Custom when they differ
    std::string pn = q.value("preset", std::string("Medium"));
    g = presetOpts(presetFromName(pn));
    g.resScale = std::clamp(q.value("resolutionScale", g.resScale), 0.5f, 1.0f);
    g.shadowQuality = std::clamp(q.value("shadowQuality", g.shadowQuality), 0, 2);
    g.farCascade = q.value("farCascade", g.farCascade); g.cloudShadows = q.value("cloudShadows", g.cloudShadows);
    g.shafts = q.value("lightShafts", g.shafts); g.ssr = q.value("ssr", g.ssr); g.ssao = q.value("ssao", g.ssao); g.ssaoFull = q.value("ssaoFullRes", g.ssaoFull);
    g.bloom = q.value("bloom", g.bloom); g.taa = q.value("taa", g.taa); g.taaPhases = std::clamp(q.value("taaPhases", g.taaPhases), 1, 64);
    g.clouds = q.value("clouds", g.clouds); g.cloudQuality = std::clamp(q.value("cloudQuality", g.cloudQuality), 0, 2); u.weather = q.contains("weather") && q["weather"].is_number_integer() ? std::max(0, q["weather"].get<int>()) : std::max(0, wxPresetFromName(q.value("weather", std::string("Clear"))));   // W1: a preset name ("Cloudy" = Overcast) u.envFog = std::clamp(q.value("envFog", u.envFog), 0, 3); g.grass = std::clamp(q.value("grass", g.grass), 0, 4); g.grassWind = q.value("grassWind", g.grassWind);
    g.materials = std::clamp(q.value("materialDetail", g.materials), 0, 2);
    if (q.contains("drawDistance") && !q.contains("renderDistance")) g.renderDist = 1200.0f * q.value("drawDistance", 1.0f);   // migration: the old multiplier of 1200 m
    g.renderDist = std::clamp(q.value("renderDistance", g.renderDist), 300.0f, 5000.0f); g.particles = std::clamp(q.value("particleBudget", g.particles), 0.25f, 4.0f);
    { std::string m = q.value("upscaling", std::string()); if (m == "temporal") u.upscale = 0; else if (m == "spatial") u.upscale = 1; else if (m == "off") u.upscale = 2; }
    u.fov = std::clamp(q.value("fov", u.fov), 55.0f, 100.0f); u.gamma = std::clamp(q.value("gamma", u.gamma), 0.5f, 2.0f); u.sharpness = std::clamp(q.value("sharpness", u.sharpness), 0.0f, 1.0f); u.microcontrast = std::clamp(q.value("microcontrast", u.microcontrast), 0.0f, 1.0f); { std::string lm = q.value("lighting", std::string(u.lightNew ? "new" : "classic")); u.lightNew = lm != "classic"; u.lightPhys = lm == "physical"; } u.vsync = q.value("vsync", u.vsync); u.dynRes = q.value("dynamicResolution2", u.dynRes);   /* a new key: saves from the default-on builds come back off */ u.fpsCap = std::clamp(q.value("fpsCap", u.fpsCap), 0, 500);
    g.lightCount = std::clamp(q.value("lightCount", g.lightCount), 0, 4096); g.shadowLights = std::clamp(q.value("shadowedLights", g.shadowLights), 0, 8);
    g.gi = std::clamp(q.value("gi", g.gi), 0, 2);
    g.msaa = q.value("msaa", g.msaa) >= 2 ? 4 : 0; g.ivy = q.value("ivy", g.ivy); g.ultraFx = q.value("ultraEffects", g.ultraFx); g.dustMotes = std::clamp(q.value("dustMotes", g.dustMotes), 0, 2);
    if (pn == "High") { GraphicsOpts old = presetOpts(GfxPreset::High); old.msaa = 4; if (g == old) g.msaa = 0; }   // VO P2: a saved High (with its old 4x MSAA) is the new High, not Custom
    for (GfxPreset pp : {GfxPreset::Low, GfxPreset::UltraLow, GfxPreset::Medium}) if (pn == presetName(pp)) {   // CP re-cut: a saved preset with its old values is the new one
      GraphicsOpts old = presetOpts(pp); old.resScale = pp == GfxPreset::Medium ? 0.67f : 0.5f; if (pp != GfxPreset::Medium) old.ssao = false;
      if (g == old) g = presetOpts(pp);
    }
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
    u.weaponList = q.value("weaponList", u.weaponList); u.friendlyMarkers = q.value("friendlyMarkers", u.friendlyMarkers);
    u.reticleSize = std::clamp(q.value("reticleSize", u.reticleSize), 50, 200); u.reticleBright = std::clamp(q.value("reticleBright", u.reticleBright), 25, 200);
    u.heliCamMotion = std::clamp(q.value("heliCameraMotion", u.heliCamMotion), 0.0f, 3.0f);
    u.vehicleBars = q.value("vehicleWorldBars", u.vehicleBars);   // GP1: was "vehicleHealthBars" (default on); the new key starts off
  }
  if (j.contains("audio") && j["audio"].is_object()) {
    const json& q = j["audio"];
    u.master = std::clamp(q.value("master", u.master), 0.0f, 1.0f); u.effects = std::clamp(q.value("effects", u.effects), 0.0f, 1.0f);
    u.ui = std::clamp(q.value("ui", u.ui), 0.0f, 1.0f); u.ambience = std::clamp(q.value("ambience", u.ambience), 0.0f, 1.0f);
    u.gunfire = std::clamp(q.value("gunfire", u.gunfire), 0.0f, 2.0f);
    u.heartbeat = q.value("heartbeat", u.heartbeat);   // SND13
  }
  if (j.contains("controls") && j["controls"].is_object())
    for (auto it = j["controls"].begin(); it != j["controls"].end(); ++it) if (it->is_string()) u.keys[it.key()] = it->get<std::string>();
  if (j.contains("player") && j["player"].is_object()) { u.character = jkey(j, "player").value("character", std::string()); u.primary = jkey(j, "player").value("primary", std::string()); }
  if (j.contains("build") && j["build"].is_object() && j["build"].contains("themes") && j["build"]["themes"].is_array()) {   // TH1
    u.themes = 0; for (auto& t : j["build"]["themes"]) for (int k = 0; k < 4; ++k) if (t.is_string() && t.get<std::string>() == kThemeNames[k]) u.themes |= 1u << k;
  }
  if (j.contains("build") && j["build"].is_object() && j["build"].contains("hotbar") && j["build"]["hotbar"].is_array()) {   // UI12
    const json& h = j["build"]["hotbar"]; for (size_t k = 0; k < u.hotbar.size() && k < h.size(); ++k) if (h[k].is_string()) u.hotbar[k] = h[k].get<std::string>();
  }
  if (j.contains("interface") && j["interface"].is_object()) {
    const json& q = j["interface"];
    u.uiScale = std::clamp(q.value("uiScale", u.uiScale), 0.75f, 2.0f);
    u.minimapRotate = q.value("minimapRotate", u.minimapRotate); u.devOverlay = q.value("developerOverlay", u.devOverlay);
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
  // UI1: movement, jump, interact, reload, build, loadout, weapon slots and next weapon are rebindable now
  {"C", "crouch (with the crouch binding)"}, {"T", "switch gun position"},
  {"X", "AT / HE ammunition"}, {"V", "scope irons"}, {"P", "pause (drop-in)"},
  {"UP", "rack up"}, {"DOWN", "rack down"}, {"ENTER", "rack: equip"}, {"BACKSPACE", "return to start"},
  {"F3", "performance stats"}, {"F4", "damage debug"}, {"F5", "Explore / Creative"}, {"F6", "match debug overlay"}, {"ESCAPE", "menu / back"},
};
const char* fixedUse(const std::string& n) { for (auto& f : kFixed) if (n == f.name) return f.what; return nullptr; }
// "LEFT_SHIFT" -> "Left Shift", "ESCAPE" -> "Esc"
static std::string prettyKey(std::string k) {
  if (k == "ESCAPE") return "Esc"; if (k == "BACKSPACE") return "Backspace"; if (k == "ENTER") return "Enter";
  bool up = true; for (char& c : k) { if (c == '_') { c = ' '; up = true; continue; } c = up ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c); up = false; }
  return k;
}
struct Action { const char* id; const char* label; };
const Action kActions[] = {{"fireMode", "Fire mode"}, {"prone", "Prone"}, {"crouch", "Crouch"}, {"packUp", "Deploy / pack up heavy weapon"},
                           {"grenade", "Throw grenade"}, {"smokeGrenade", "Smoke grenade (match)"},
                           {"forward", "Move forward"}, {"back", "Move back"}, {"left", "Move left"}, {"right", "Move right"}, {"sprint", "Sprint"}, {"jump", "Jump"},
                           {"leanLeft", "Lean left"}, {"leanRight", "Lean right"}, {"interact", "Interact / enter"}, {"reload", "Reload"}, {"nextWeapon", "Next weapon"},
                           {"slot1", "Weapon slot 1"}, {"slot2", "Weapon slot 2"}, {"slot3", "Weapon slot 3 (knife)"}, {"slot4", "Weapon slot 4 (grenades)"},
                           {"build", "Build"}, {"loadout", "Loadout / range"}, {"map", "Map"}, {"artillery", "Artillery"}, {"scoreboard", "Scoreboard"}, {"flashlight", "Flashlight (headlamp)"}};   // UI1
int* bindSlot(App& app, const std::string& a) {
  auto& b = app.binds;
  return a == "fireMode" ? &b.fireMode : a == "prone" ? &b.prone : a == "crouch" ? &b.crouch : a == "packUp" ? &b.packUp : a == "grenade" ? &b.grenade : a == "smokeGrenade" ? &b.smokeGrenade : b.key.count(a) ? &b.key[a] : nullptr;
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
  if (R.upscaleMode != s.upscale) { R.upscaleMode = s.upscale; R.resetTemporal(); }   // render() re-derives the internal size
  // P3 per-tier shadow / post costs (docs/settings.md): shadowQuality 0 Low, 1 Medium, 2 High (High = the pre-P3 values)
  const int sq = std::clamp(g.shadowQuality, 0, 2);
  const bool old = presetsOld();
  R.setShadowSizes(old ? (sq == 0 ? 2048 : 4096) : (sq == 0 ? 1024 : sq == 1 ? 2048 : 4096), sq == 0 ? 1024 : sq == 2 ? 4096 : 2048);
  R.shadowTier = sq;
  R.q.shadowNearR = old || sq == 2 ? 40.0f : sq == 1 ? 32.0f : 28.0f;   // texels: Low 5.5 cm, Medium 3.1 cm, High 2.0 cm
  R.q.shadowTaps = old || sq == 2 ? 16 : sq == 1 ? 6 : 4;
  R.q.farEvery = old || sq == 2 ? 2 : sq == 1 ? 4 : 8;
  R.q.leafShadowLodM = old || sq == 2 ? 12.0f : sq == 1 ? 8.0f : 4.0f;
  R.q.ssaoDiv = old || g.ssaoFull ? 2 : 4;                              // not full resolution: quarter (Medium)
  R.q.probeHalf = !old && g.gi == 1;                                    // near-probe GI (Medium): half the capture budget
  R.setSsaoFull(g.ssaoFull);
  R.q.farCascade = g.farCascade; R.q.cloudShadows = g.cloudShadows; R.q.shafts = g.shafts; R.q.ssr = g.ssr; R.q.ssao = g.ssao; R.q.bloom = g.bloom;
  R.q.microcontrast = s.microcontrast;   // UI2
  R.q.sharpness = s.sharpness;   // CP1
  R.q.lightNew = s.lightNew;   // VO stream L
  R.q.lightPhys = s.lightNew && s.lightPhys;   // CP2
  R.q.ultra = g.ultraFx;   // UL2
  if (R.q.taa != g.taa || R.q.taaPhases != g.taaPhases) R.resetTemporal();
  R.q.taa = g.taa; R.q.taaPhases = g.taaPhases;
  R.setMsaa(g.msaa);
  R.q.matLayer = g.materials;   // GX5 (docs/graphics-overhaul.md#gx5)
  app.ivyOn = g.ivy;
  R.grassAtlasTier = g.grass >= 2;   // GT1 grass plant atlas: Medium, High, Ultra (Low keeps the procedural blades)
  R.leafLodScale = g.grass <= 1 ? 0.6f : g.ultraFx ? 1.5f : 1.0f;   // UL2: Ultra holds full tree detail 1.5x further   // L1: vegetation detail follows the grass quality (Low: near 6 m, mid 13 m)
  R.clouds = g.clouds; R.wx.quality = g.cloudQuality; R.wx.target = s.weather; R.wx.fog = s.envFog;   // WX1 (a LAN client takes the host's: Renderer::Weather::forced)
  R.q.hazeDensity = 0.0010f;   // W1: visibility is the weather's, the same on every quality preset (fair sight; was scaled by render distance)
  R.q.leafDriftN = g.cloudQuality <= 0 ? 0 : g.cloudQuality == 1 ? 800 : 2000;   // WS2 (corpus 07 s7.3)
  R.q.snowfallScale = g.cloudQuality <= 0 ? 0.4f : g.cloudQuality == 1 ? 0.75f : 1.0f;   // SF1: a map's snowfall at this sky quality   // PF2: the haze reaches the same depth at the render distance (Medium: unchanged)
  R.lt.maxLights = g.lightCount; R.lt.shadowLights = g.shadowLights; R.lt.gi = g.gi; R.lt.hour = s.hour;   // L2 lighting
  R.q.impAtlas = g.grass >= 3 ? 2048 : 1024; R.q.impMax = g.grass >= 4 ? 16 : g.grass >= 2 ? 12 : 6;   // CP3: about 28 MB a species at 2048, 7 MB at 1024
  app.grass.quality = (GrassQuality)std::clamp(g.grass, 0, 4); app.grass.nearWind = g.grassWind;
  app.fx.setBudget(g.particles);
  { const GfxPreset p = s.preset(); app.fx.tier = p == GfxPreset::UltraLow ? 0 : p == GfxPreset::Low ? 1 : p == GfxPreset::High ? 3 : p == GfxPreset::Ultra ? 4 : 2; }   // spec 8.3 columns (Custom: Medium)
  if (!gSelftest) platformSwapInterval(s.vsync ? 1 : 0);
  app.reducedMotion = s.reducedMotion; app.vm.showArms = s.showArms; app.sightMarkers = s.sightMarkers; app.showWeaponList = s.weaponList; app.friendlyMarkers = s.friendlyMarkers;
  app.reticleSize = s.reticleSize / 100.0f; app.reticleBright = s.reticleBright / 100.0f;
  app.audio.setGains(s.master, s.effects, s.ui, s.ambience); app.audio.gunfireGain = std::clamp(s.gunfire, 0.0f, 2.0f);   // UI1b
  // UI1b: the font scale is set every frame (app.cpp uiFontScale: resolution x UI scale)
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
  g.shadowQuality = R.shadowTier;
  g.farCascade = R.q.farCascade; g.cloudShadows = R.q.cloudShadows; g.shafts = R.q.shafts; g.ssr = R.q.ssr; g.ssao = R.q.ssao; g.bloom = R.q.bloom;
  g.ssaoFull = R.ssaoFull(); g.taa = R.q.taa; g.taaPhases = R.q.taaPhases; g.clouds = R.clouds; g.cloudQuality = R.wx.quality;
  g.lightCount = R.lt.maxLights; g.shadowLights = R.lt.shadowLights; g.gi = R.lt.gi; s.hour = R.lt.hour;
  g.grass = (int)app.grass.quality; g.grassWind = app.grass.nearWind;
  g.msaa = R.msaa(); g.ivy = app.ivyOn; g.materials = R.q.matLayer;
  s.reducedMotion = app.reducedMotion; s.showArms = app.vm.showArms; s.sightMarkers = app.sightMarkers; s.weaponList = app.showWeaponList; s.friendlyMarkers = app.friendlyMarkers;
  s.reticleSize = (int)std::lround(app.reticleSize * 100); s.reticleBright = (int)std::lround(app.reticleBright * 100);
  s.editorFly = navState().speedMult;
}

// ---------------------------------------------------------------- hooks
void settingsInit(App& app, bool selftest) {
  Settings& S = settings();
  gSelftest = selftest;
  S.persist = !selftest;
  if (selftest) {   // self-tests: Medium defaults, nothing read or written, nothing applied
    if (const char* e = std::getenv("NMK_RES_SCALE")) app.R.setResolutionScale((float)std::atof(e));   // benchmarks at a chosen scale
    if (const char* e = std::getenv("NMK_UPSCALE")) S.s.upscale = app.R.upscaleMode = e[0] == 's' ? 1 : e[0] == 'o' ? 2 : 0;   // temporal | spatial | off (kept by preset applies)
    if (const char* e = std::getenv("NMK_SETTINGS_FILE")) if (*e && S.load(fs::path(e))) { S.apply(app); logf("settings: %s applied (self-test)", e); }   // VM1: a user's own settings, read only
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
  using clk = std::chrono::steady_clock;
  static clk::time_point last = clk::now();
  int cap = settings().s.fpsCap;
  auto lim = [&](int m) { cap = cap > 0 ? std::min(cap, m) : m; };
  const bool focused = glfwGetWindowAttrib(app.win, GLFW_FOCUSED);
  if (mainMenuActive()) lim(focused ? 60 : 30);   // UI2: the menu is a still: 60 fps, 30 in the background
  if (!focused) lim(30);                                       // MEM1: a background game does not need a full core
  if (glfwGetWindowAttrib(app.win, GLFW_ICONIFIED)) lim(10);   // vsync does not hold a minimised window: it would spin
  static const bool capInTest = std::getenv("NMK_FRAMECAP_IN_TEST") != nullptr;   // MEM2: --mem-probe measures the real idle CPU
  if (cap > 0 && (!gSelftest || capInTest)) {
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
// WX2: the window's state at file scope, so the Weather combo and the Apply button are callable (scripted settings test)
static UserSettings gApplied;            // what is in effect: edits wait for Apply, closing without it drops them
static bool gWasOpen = false, gPendingSave = false;
void settingsUiWeather(App& app, int w) {   // the Weather combo: live (no Apply needed), kept when the window closes
  settings().s.weather = w; gApplied.weather = w; app.R.wx.target = w; app.R.wx.userSet = true; gPendingSave = true;
}
void settingsUiHour(App& app, float h) {   // SD8: Time of day / Hour: live, kept when the window closes; under the open sky it overrides the map's hour
  settings().s.hour = h; gApplied.hour = h; app.R.lt.hour = h; app.R.lt.userHour = true; gPendingSave = true;
}
void settingsUiMapHour(App& app) {   // SD8: "Map default": back to the map's own hour
  app.R.lt.userHour = false; if (app.R.lt.mapHour >= 0) { settings().s.hour = gApplied.hour = app.R.lt.mapHour; app.R.lt.hour = app.R.lt.mapHour; }
}
void settingsUiApply(App& app) {   // the Apply button (always enabled: it re-applies and saves even when nothing is pending)
  Settings& S = settings();
  if (S.s.keys != gApplied.keys) app.loadBindings();   // re-reads bindings.json, then the overrides (settingsApplyKeys)
  if (S.s.envFog != gApplied.envFog || S.s.weather != gApplied.weather) app.R.wx.userSet = true;   // NT2: the user overrides the map's sky
  if (S.s.hour != gApplied.hour) app.R.lt.userHour = true;   // SD8
  S.apply(app); gApplied = S.s; gPendingSave = true;
}
// UI1b: the controls page (pause menu "Controls"): mouse, every action with its key, then the fixed keys (no debug keys)
void controlsWindow(App& app, bool* open) {
  if (!*open) return;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::Begin("Controls", open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    const ImVec4 keyCol(0.92f, 0.70f, 0.28f, 1.0f);
    if (ImGui::BeginTable("controls", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
      int n = 0;
      auto cell = [&](const std::string& key, const char* what) { if (n++ % 2 == 0) ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(keyCol, "%s", key.c_str()); ImGui::TableNextColumn(); ImGui::TextUnformatted(what); };
      cell("Mouse", "look"); cell("Left button", "fire"); cell("Right button", "aim / sights"); cell("Wheel", "next / previous weapon");
      for (auto& a : kActions) { auto it = app.binds.names.find(a.id); cell(it == app.binds.names.end() ? "-" : prettyKey(it->second), a.label); }
      for (auto& f : kFixed) { std::string nm = f.name; if (nm == "F3" || nm == "F4" || nm == "F6") continue; cell(prettyKey(nm), f.what); }
      cell("F12", "developer overlay");
      ImGui::EndTable();
    }
    ImGui::Spacing(); ImGui::TextDisabled("Change keys in Settings > Controls.");
  }
  ImGui::End();
}

int gSettingsTab = 0;   // UI4: the Settings page tab (captures set it)
void settingsWindow(App& app) {
  Settings& S = settings();
  bool& wasOpen = gWasOpen; bool& pendingSave = gPendingSave;
  UserSettings& applied = gApplied;
  if (S.open && app.mode == Mode::Explore && !app.pauseMenu && !app.showLoadout) S.open = false;   // back in play: the cursor is captured
  if (!S.open) { S.rebinding.clear(); if (wasOpen) S.s = applied; if (wasOpen || pendingSave) S.save(); wasOpen = pendingSave = false; return; }
  if (!wasOpen) { S.capture(app); applied = S.s; }   // on opening: the editor View menu / Loadout may have changed something
  wasOpen = true;
  UserSettings& u = S.s;
  const UserSettings before = u;
  bool applyNow = false, saveNow = false;
  ImGuiIO& io = ImGui::GetIO();
  // UI4: a full page in the flat options language (ui_flat.h): title, version, copper rule, "//" tabs, flat rows in the
  // left half, a hint, a rule and the text actions at the bottom; the blurred scene (or the menu's backdrop) behind
  const float W = io.DisplaySize.x, H = io.DisplaySize.y, sc = H / 1080.0f, X0 = 84 * sc;
  ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
  if (!ImGui::Begin("Settings", &S.open, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) { ImGui::End(); return; }
  ++S.windowFrames;
  ImDrawList* pdl = ImGui::GetWindowDrawList();
  flat::backdrop(pdl, ImVec2(0, 0), io.DisplaySize);
  pdl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W * 0.6f, H), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));
  { const float tp = 104 * sc; flat::text(pdl, flat::titleFont(), tp, ImVec2(X0, 54 * sc), flat::ink(), "OPTIONS", 0.01f);
    flat::text(pdl, ImGui::GetFont(), 13 * sc, ImVec2(X0 + 4 * sc, 54 * sc + tp * 1.02f), flat::grey(0.8f), "BATTLE FOUNDRY  PRE-ALPHA  BUILD " __DATE__, 0.12f);
    flat::rule(pdl, X0, W - X0, 54 * sc + tp * 1.02f + 30 * sc); }
  int& tab = gSettingsTab;
  ImGui::SetCursorScreenPos(ImVec2(X0, 54 * sc + 104 * sc * 1.02f + 46 * sc));
  { static const char* tn[] = {"Video", "Gameplay", "Audio", "Keybindings", "Interface"}; flat::tabs(tn, 5, tab); }
  const float bodyTop = ImGui::GetCursorScreenPos().y + 18 * sc, bodyBot = H - 116 * sc;
  ImGui::SetCursorScreenPos(ImVec2(X0, bodyTop));
  // UI5: the rows span the page: two columns on the long tabs (Video, Keybindings), else one wide column whose values sit
  // near 45 % of the width; tight row spacing (the rows carry their own height)
  ImGui::BeginChild("##setbody", ImVec2(W - 2 * X0, bodyBot - bodyTop), 0, ImGuiWindowFlags_NoBackground);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * sc, 2)); ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
  const bool twoCol = tab == 0 || tab == 3;
  ImGui::BeginTable("##setcols", 3, ImGuiTableFlags_SizingStretchProp);
  ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, twoCol ? 1.0f : 0.62f);
  ImGui::TableSetupColumn("gap", ImGuiTableColumnFlags_WidthFixed, 72 * sc);
  ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, twoCol ? 1.0f : 0.38f);
  ImGui::TableNextColumn();
  auto nextCol = [] { ImGui::TableNextColumn(); ImGui::TableNextColumn(); };
  {
    // ------------------------------------------------ graphics
    if (tab == 0) {
      GraphicsOpts& g = u.gfx;
      // the list in quality order; GfxPreset keeps its stored values (Ultra Low was appended as 4)
      const GfxPreset order[6] = {GfxPreset::UltraLow, GfxPreset::Low, GfxPreset::Medium, GfxPreset::High, GfxPreset::Ultra, GfxPreset::Custom};
      const char* pn[] = {"Ultra Low", "Low", "Medium", "High", "Ultra", "Custom"};
      int pr = 5; for (int k = 0; k < 6; ++k) if (order[k] == u.preset()) pr = k;
      
      if (flat::combo("Preset", &pr, pn, 6) && order[pr] != GfxPreset::Custom) g = presetOpts(order[pr]);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Medium is the standard look (77-100 %% resolution with dynamic resolution). Low: half-size shadows, no light shafts / reflections / bloom,\nless grass and decoration, 67-100 %% resolution; ambient occlusion, contact shadows, haze and anti-aliasing stay on every preset.\nHigh: sharper far shadows, full-resolution AO, more grass, longer view.");
      flat::sliderFloat("Resolution scale", &g.resScale, 0.5f, 1.0f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit()) applyNow = true;   // targets are recreated once, on release
      flat::checkbox("Dynamic resolution", &u.dynRes);   // CP 5.1 (dynres.cpp)
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lowers the internal resolution when a frame would miss 60 fps and raises it again when there is room (Medium / High 77-100 %%, Low 67-100 %%; Temporal upscaling). Off: the fixed resolution scale above.");
      { int fw = app.R.displayW, fh = app.R.displayH; float rs = app.R.perf.taau && settings().s.upscale != 2 ? app.R.resolutionScale() : 1.0f;
        ImGui::TextDisabled("Internal resolution now: %d x %d (%.0f %%)", (int)std::lround(fw * rs), (int)std::lround(fh * rs), rs * 100); }
      { const char* um[] = {"Temporal", "Spatial", "Off (native)"}; flat::combo("Upscaling", &u.upscale, um, 3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Below 100 %% resolution: Temporal rebuilds full detail over several frames (sharpest),\nSpatial upscales each frame (fastest on the Mac), Off renders at full resolution."); }
      { const char* sq[] = {"Low (2048 / 1024)", "Medium (4096 / 2048)", "High (4096 / 4096)"}; flat::combo("Shadow quality", &g.shadowQuality, sq, 3); }
      flat::checkbox("Far shadow cascade", &g.farCascade); flat::checkbox("Cloud shadows", &g.cloudShadows);
      flat::checkbox("Ambient occlusion", &g.ssao); ImGui::BeginDisabled(!g.ssao); flat::checkbox("Full-resolution AO", &g.ssaoFull); ImGui::EndDisabled();
      flat::checkbox("Screen-space reflections", &g.ssr); flat::checkbox("Bloom", &g.bloom);
      flat::checkbox("Light shafts", &g.shafts); flat::checkbox("Temporal AA", &g.taa);
      { bool m = g.msaa >= 2; if (flat::checkbox("4x MSAA", &m)) g.msaa = m ? 4 : 0; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multisampled world geometry (smoother building and roof edges), resolved before temporal AA. High turns it on.");
      flat::checkbox("Ivy on walls", &g.ivy);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ivy on stone walls; its density follows the grass quality.");
      { const char* dm[] = {"Off", "Low", "High"}; flat::combo("Dust motes", &g.dustMotes, dm, 3); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dust drifting in the sunlight that falls through windows indoors (a few dozen motes per lit window near you).");
      flat::checkbox("Clouds", &g.clouds);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cloud cover: Creative > View > Sky");
      { const char* gq[] = {"Off", "Low", "Medium", "High", "Ultra"}; flat::combo("Grass", &g.grass, gq, 5); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ultra: denser, longer-reaching grass (grass only: no preset sets it, and it leaves shadows, probes and MSAA alone).");
      nextCol();
      {   // PF2: render distance in metres, four named stops plus any value on the slider
        const float stops[4] = {600, 1200, 2500, 5000}; const char* names[5] = {"Low (600 m)", "Medium (1.2 km)", "High (2.5 km)", "Ultra (5 km)", "Custom"};
        int cur = 4; for (int k = 0; k < 4; ++k) if (std::fabs(g.renderDist - stops[k]) < 1) cur = k;
        
        if (flat::combo("Render distance", &cur, names, 5) && cur < 4) g.renderDist = stops[cur];
        flat::sliderFloat("##renderdist", &g.renderDist, 300.0f, 5000.0f, "%.0f m");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far buildings, props, trees and nature are drawn; also the far terrain detail and the haze (thinner haze for longer distances).");
      }
      flat::checkbox("Grass wind", &g.grassWind);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wind sway on the grass near you (within ~13 m; distant grass is always still). Off saves a little GPU time.");
      { const char* mq[] = {"Low", "Medium", "High"}; flat::combo("Material detail", &g.materials, mq, 3); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Medium: wear and grime by room, corner shading, wall decals (stains, soot, cracks, scuffs).\nHigh: also parallax relief on textured walls. Low: the base materials.");
      flat::sliderFloat("Particle budget", &g.particles, 0.25f, 2.5f, "x %.2f");
      flat::section("Lighting");
      {   // L2 (docs/lighting.md): time of day is personal (kept when a preset is picked); the rest belongs to the presets
        // SD8: live (no Apply needed); a map's own hour is its default ("Map default"), fixed only on an enclosed map
        const bool mapDef = app.R.lt.mapHour >= 0 && !app.R.wx.enclosed, hostHour = app.R.lt.forcedHour >= 0;
        int tp = mapDef && !app.R.lt.userHour ? 5 : (int)todPresetOf(u.hour); const char* tn[] = {"Day", "Dusk", "Night", "Dawn", "Custom", "Map default"};
        ImGui::BeginDisabled(hostHour || (app.R.lt.mapHour >= 0 && app.R.wx.enclosed));
        const float h0 = u.hour;
        if (flat::combo("Time of day", &tp, tn, mapDef ? 6 : 5)) { if (tp == 5) settingsUiMapHour(app); else if (tp != (int)TodPreset::Custom) settingsUiHour(app, todPresetHour((TodPreset)tp)); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(hostHour ? "A LAN match uses the host's time of day." : app.R.lt.mapHour >= 0 && app.R.wx.enclosed ? "This map is enclosed: it keeps its own hour." :
                                                       "Day is the standard look. Dusk and Dawn: low sun, long shadows. Night: moon, stars, lamps.\nMap default: the map's own hour. Applies at once; in a LAN match the host's applies.");
        int hh = (int)u.hour, mm = (int)std::lround((u.hour - hh) * 60); if (mm == 60) { ++hh; mm = 0; }
        char hl[32]; std::snprintf(hl, sizeof hl, "%02d:%02d", hh % 24, mm);
        if (flat::sliderFloat("Hour", &u.hour, 0.0f, 23.99f, hl) && u.hour != h0) settingsUiHour(app, u.hour);
        ImGui::EndDisabled();
        { const bool wxDef = app.R.wx.mapWx >= 0 && !app.R.wx.enclosed;   // WX2: live (no Apply needed) and kept when the window closes
          // W1: the fifteen presets in the corpus table's order, then Map default (maps with an authored sky); enclosed maps have none
          static const char* wn[32]; const int np = wxPresetCount(); for (int k = 0; k < np; ++k) wn[k] = wxPresetName(wxPresetIdAt(k)); wn[np] = "Map default";
          int wi = wxDef && !app.R.wx.userSet ? np : wxPresetMenuIndex(u.weather);
          ImGui::BeginDisabled(app.R.wx.enclosed);
          if (flat::combo("Weather", &wi, wn, wxDef ? np + 1 : np)) { if (wi == np) app.R.wx.userSet = false; else { u.weather = wxPresetIdAt(wi); settingsUiWeather(app, u.weather); } }
          ImGui::EndDisabled();
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(app.R.wx.enclosed ? "This map has no sky." : "Applies at once; the hour stays as set (Dawn ground fog is meant for dawn). Clear and Overcast keep today's look; the others set light, air, wind and falling snow from the weather corpus. Lying snow and sand only on maps made for them. In a LAN match the host's weather applies."); }
        { const char* fn[] = {"Off", "Light", "Moderate", "Dense"}; flat::combo("Environmental fog", &u.envFog, fn, 4); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground fog in hollows, low ground and along the shore, drifting on the wind. Limits how far bots see. A LAN match uses the host's.");
        flat::sliderInt("Light count", &g.lightCount, 0, 1024);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lamps, lanterns and fires lit each frame (nearest first).");
        { int sl[] = {0, 2, 4, 8}; const char* sn[] = {"Off", "2", "4", "8"}; int si = 0; for (int k = 0; k < 4; ++k) if (sl[k] == g.shadowLights) si = k;
          if (flat::combo("Shadowed lights", &si, sn, 4)) g.shadowLights = sl[si]; }
        { const char* gn[] = {"Off", "Medium", "High"}; flat::combo("Indirect light", &g.gi, gn, 3);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Probe light around the camera: lamplight and window light bounce onto walls and ceilings.\nOff: the flat fill of earlier versions."); }
      }
      flat::section("Display");
      flat::sliderFloat("Field of view", &u.fov, 55.0f, 100.0f, "%.0f deg");
      flat::sliderFloat("Gamma", &u.gamma, 0.5f, 2.0f, "%.2f");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Above 1 brightens the dark tones, below 1 darkens them. 1.00 is the default.");
      { float pc = u.microcontrast * 100.0f;   // UI2: live (no Apply needed), like Weather
        if (flat::sliderFloat("Microcontrast", &pc, 0.0f, 100.0f, "%.0f %%")) { u.microcontrast = std::clamp(pc / 100.0f, 0.0f, 1.0f); gApplied.microcontrast = u.microcontrast; app.R.q.microcontrast = u.microcontrast; gPendingSave = true; } }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local contrast (clarity): brings out surface detail and depth in flat, hazy light without sharpening edges or dark noise. 0 %% is off.");
      { float sp = u.sharpness * 100.0f;   // CP1: live, like Microcontrast
        if (flat::sliderFloat("Sharpness", &sp, 0.0f, 100.0f, "%.0f %%")) { u.sharpness = std::clamp(sp / 100.0f, 0.0f, 1.0f); gApplied.sharpness = u.sharpness; app.R.q.sharpness = u.sharpness; gPendingSave = true; } }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Contrast-adaptive sharpening of the final image: crisper texture detail, held back on edges that are already sharp. 0 %% is off.");
      { int li = !u.lightNew ? 1 : u.lightPhys ? 2 : 0; const char* ln[] = {"New", "Classic", "Physical"};   // VO stream L: live, like Microcontrast; CP2 Physical
        if (flat::combo("Lighting", &li, ln, 3)) { u.lightNew = li != 1; u.lightPhys = li == 2; gApplied.lightNew = u.lightNew; gApplied.lightPhys = u.lightPhys; app.R.q.lightNew = u.lightNew; app.R.q.lightPhys = u.lightPhys; gPendingSave = true; } }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("New: full-strength shadows grounded by contact shadows and the newer lighting. Classic: the lighting before the visual overhaul, for comparison.");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical field of view on foot (72 standard). Aiming and gun sights narrow from it.");
      flat::checkbox("Vsync", &u.vsync);
      { int caps[] = {0, 30, 60, 90, 120, 144, 165, 240}; const char* cn[] = {"Unlimited", "30", "60", "90", "120", "144", "165", "240"};
        int ci = 0; for (int k = 0; k < 8; ++k) if (caps[k] == u.fpsCap) ci = k;
        if (flat::combo("FPS cap", &ci, cn, 8)) u.fpsCap = caps[ci]; }
    }
    // ------------------------------------------------ gameplay
    if (tab == 1) {
      flat::sliderFloat("Mouse sensitivity", &u.mouseSens, 0.02f, 0.4f, "%.3f deg/count");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees per mouse count in the standard 72 deg view (0.100 default). Matched to the view, so\nirons, scopes and tank sights move the picture by the same distance per count.");
      flat::sliderFloat("Aiming sensitivity", &u.adsSens, 0.3f, 1.5f, "x %.2f");
      flat::checkbox("Invert mouse Y", &u.invertY);
      flat::checkbox("Reduced motion (keeps aim displacement)", &u.reducedMotion);
      flat::sliderFloat("Helicopter camera motion", &u.heliCamMotion, 0.0f, 3.0f, "x %.2f");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera banking, rotor vibration and landing shake while flying a helicopter (0 = none, 1 = default, up to 3x more).");
      flat::checkbox("Minimap turns with you (heading up)", &u.minimapRotate);
      flat::checkbox("Health bars over other vehicles", &u.vehicleBars);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("A health bar floating over every vehicle within about 90 m.");
      flat::checkbox("Show arms", &u.showArms);
      flat::checkbox("Sight calibration markers", &u.sightMarkers);
      flat::checkbox("Show weapon list", &u.weaponList);
      flat::checkbox("Friendly markers over teammates", &u.friendlyMarkers);
      flat::sliderInt("Reticle size", &u.reticleSize, 50, 200, "%d %%");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sight dots, holographic and scope reticles and the blaster crosshair (100 %% = a 2 MOA red dot).");
      flat::sliderInt("Reticle brightness", &u.reticleBright, 25, 200, "%d %%");
      { extern void goreOptionsUI(); goreOptionsUI(); }   // gore_user.json (gore.cpp)
    }
    // ------------------------------------------------ audio
    if (tab == 2) {
      auto vol = [&](const char* l, float& v) { int p = (int)std::lround(v * 100); if (flat::sliderInt(l, &p, 0, 100, "%d %%")) v = p / 100.0f; };
      vol("Master", u.master); vol("Effects", u.effects);
      { int p = (int)std::lround(u.gunfire * 100); if (flat::sliderInt("Gunfire", &p, 0, 200, "%d %%")) u.gunfire = p / 100.0f; }   // UI1b: guns, cannons, shell blasts
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gunshots, cannon fire and shell explosions, on top of Effects (100 %% = as mixed, up to 200 %%).");
      vol("Interface", u.ui); vol("Ambience (wind, leaves)", u.ambience);
      flat::checkbox("Low-health heartbeat", &u.heartbeat);   // SND13
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("A heartbeat while your health is under 30 %%, faster and a little louder toward 10 %%.");
      if (!app.audio.ok()) ImGui::TextDisabled("No audio device.");
    }
    // ------------------------------------------------ controls
    if (tab == 3) {
      ImGui::TextWrapped("Rebindable Explore actions (defaults in content/settings/bindings.json). Click a key, then press the new one (Esc cancels).");
      int ai = 0;
      for (auto& a : kActions) {
        if (ai++ == (int)(sizeof kActions / sizeof kActions[0] + 1) / 2) nextCol();
        auto it = app.binds.names.find(a.id); std::string cur = it == app.binds.names.end() ? "?" : it->second;
        if (flat::keyRow((std::string(a.label) + "##" + a.id).c_str(), cur.c_str(), S.rebinding == a.id)) { S.rebinding = a.id; S.bindMsg.clear(); }   // UI4
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
      if (flat::action("Reset keys")) { u.keys.clear(); app.loadBindings(); S.bindMsg = "keys reset"; saveNow = true; }
      if (ImGui::CollapsingHeader("Fixed keys")) {
        ImGui::TextDisabled("Mouse: LMB fire, RMB sights (tank: zoom), wheel: tank sight range");
        for (auto& f : kFixed) { ImGui::BulletText("%-10s %s", f.name, f.what); }
      }
    }
    // ------------------------------------------------ interface
    if (tab == 4) {
      flat::sliderFloat("UI scale", &u.uiScale, 0.75f, 2.0f, "x %.2f");
      flat::checkbox("Developer overlay (F12)", &u.devOverlay);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame time, the full key list, debug keys and status lines over play. Off for normal play.");
      if (ImGui::IsItemDeactivatedAfterEdit()) applyNow = true;   // resizing the window while dragging its own slider: on release
      flat::section("Editor camera");
      flat::sliderFloat("Fly speed", &u.editorFly, 0.1f, 10.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also Alt+wheel in the editor.");
      flat::sliderFloat("Orbit speed", &u.editorOrbit, 0.25f, 4.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      flat::sliderFloat("Zoom step", &u.editorZoom, 0.25f, 4.0f, "x %.2f", ImGuiSliderFlags_Logarithmic);
      flat::section("Caches");   // CACHE1 (user_cache.h): thumbnails, furnishing plans, compressed textures
      static double scanned = -1; if (scanned < 0 || ImGui::GetTime() - scanned > 5) { for (int c = 0; c < UC_COUNT; ++c) userCacheBytes(c, true); scanned = ImGui::GetTime(); }   // recount every 5 s while the tab is open
      for (int c = 0; c < UC_COUNT; ++c) ImGui::TextDisabled("%-20s %6.0f MB of %.0f MB", userCacheName(c), userCacheBytes(c, false) / 1048576.0, userCacheCapBytes(c) / 1048576.0);   // CACHE2: each its own cap
      ImGui::TextDisabled("Each is pruned at launch when over its cap: unused for 30 days first, then the oldest.");
      ImGui::TextDisabled("%s", userCacheDir("texbc", nullptr).parent_path().string().c_str());
      if (flat::action("Clear caches")) { userCacheClear(); scanned = -1; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frees the disk space. They fill again as needed: the next launch and first map loads are slower.");
    }
  }
  ImGui::EndTable(); ImGui::PopStyleVar(2);
  ImGui::EndChild();
  const bool dirty = !(u == applied);
  {   // the footer: a hint, a rule, the text actions, a small status line
    flat::rule(pdl, X0, W - X0, H - 104 * sc);
    ImGui::SetCursorScreenPos(ImVec2(X0, H - 92 * sc));
    if (flat::action("Back")) S.open = false;   // unapplied edits are dropped, as Close did
    ImGui::SameLine(0, 48 * sc);
    if (flat::action("Reset to defaults")) { std::map<std::string, std::string> k = u.keys; u = UserSettings{}; u.keys = k; }
    ImGui::SameLine(0, 48 * sc);
    if (flat::action("Apply settings")) applyNow = true;   // WX2: never greyed out
    char stb[200]; std::snprintf(stb, sizeof stb, "%s   //   FRAME %.2f MS (%.0f FPS)   RENDER %d X %d OF %d X %d", dirty ? "CHANGES NOT APPLIED YET" : S.persist ? "SAVED TO SETTINGS_USER.JSON" : "SELF-TEST: NOT SAVED",
                                 io.DeltaTime * 1000, io.Framerate, app.R.width, app.R.height, app.R.displayW, app.R.displayH);   // UI5: the Video tab's frame line lives here
    const char* st = stb;
    flat::text(pdl, ImGui::GetFont(), 12 * sc, ImVec2(W * 0.5f - flat::textW(ImGui::GetFont(), 12 * sc, st, 0.12f) * 0.5f, H - 34 * sc), flat::grey(0.7f), st, 0.12f);
  }
  ImGui::End();
  (void)before;
  // apply: only on the Apply button; closing the window without it drops the edits
  if (applyNow) {
    settingsUiApply(app); saveNow = true;
  }
  pendingSave = pendingSave || saveNow;
  if (pendingSave && !ImGui::IsAnyItemActive()) { S.save(); pendingSave = false; }   // not every frame of a slider drag
}
