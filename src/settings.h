#pragma once
// User settings (docs/settings.md): graphics presets Low / Medium / High / Custom, gameplay, audio, controls and
// interface options, persisted to exeDir()/settings_user.json and edited in the Settings window (Creative top menu,
// the Explore pause menu and Loadout). Medium is exactly the built-in defaults, so a missing or deleted file, and
// every self-test run (which never reads or writes the user file), renders as before.
#include "common.h"
#include <map>
#include <algorithm>

class App;

enum class GfxPreset : int { Low = 0, Medium = 1, High = 2, Custom = 3 };

// The part of the graphics options a preset sets. Everything else (FOV, vsync, FPS cap, cloud cover) is personal and
// kept when a preset is picked.
struct GraphicsOpts {
  float resScale = 0.67f;      // internal render resolution (0.5 .. 1); below 1 TAA reconstructs at output size (P1 TAAU). Medium 0.67, Low 0.5, High 1
  int shadowQuality = 1;       // 0: near 2048 / far 1024, 1: 4096 / 2048 (default), 2: 4096 / 4096
  bool farCascade = true, cloudShadows = true, shafts = true, ssr = true, ssao = true, ssaoFull = false, bloom = true, taa = true;
  int taaPhases = 8;           // Halton jitter sequence length
  bool clouds = true;
  int grass = 2;               // GrassQuality: 0 Off, 1 Low, 2 Medium, 3 High, 4 Ultra (grass only; no GfxPreset uses it)
  float renderDist = 1200.0f;  // m (PF2): instance draw cut, decor / tree / nature / ivy culls (x renderDist / 1200), far terrain LOD, matching haze
  bool grassWind = true;       // GX3: vertex wind on the near grass clumps (Low: static grass)
  float particles = 1.0f;      // effect budgets (Fx caps) and, through particleBudget(), fragment caps
  // L2 lighting (docs/lighting.md): local lights per frame, shadowed lights, probe GI 0 off / 1 near / 2 near + far
  int lightCount = 256, shadowLights = 2, gi = 1;   // P3: Medium 2 shadowed lights (was 4)
  int msaa = 0;                // B1: 4 = 4x MSAA on the opaque world (High), 0 = off
  bool ivy = true;             // B1: ivy on stone walls (density follows the grass quality)
  int materials = 1;           // GX5 material detail: 0 Low (base materials), 1 wear / grime / seams / decals, 2 High (+ parallax)
  int dustMotes = 1;           // GX2: dust motes in indoor sun shafts: 0 off (Low), 1 low (Medium), 2 high (High)
  bool operator==(const GraphicsOpts&) const = default;
};
GraphicsOpts presetOpts(GfxPreset p);
bool presetsOld();   // P3 A/B (NMK_PRESET_OLD): the pre-P3 Low / Medium values
const char* presetName(GfxPreset p);

struct UserSettings {
  // graphics
  GraphicsOpts gfx;
  float fov = 72.0f;           // hip / running world FOV (vertical deg); ADS and sights narrow from it
  bool vsync = true;
  int fpsCap = 0;              // 0 = unlimited
  float hour = 14.0f;          // time of day (L2): 14:00 Day, 18:36 Dusk, 00:00 Night, 05:00 Dawn, or any hour
#ifdef __APPLE__
  int upscale = 1;             // 0 temporal, 1 spatial, 2 off (Renderer::upscaleMode). Mac: spatial (MetalFX temporal at Retina costs ~25 ms)
#else
  int upscale = 0;             // 0 temporal (TAAU), 1 spatial (bilinear final blit), 2 off (Renderer::upscaleMode)
#endif
  // gameplay
  float mouseSens = 0.1f;      // deg per mouse count at the 72 deg reference view (matched across FOVs, explore.cpp)
  float adsSens = 1.0f;        // extra factor while aiming (blended with the ADS state)
  bool invertY = false, reducedMotion = false, showArms = true, sightMarkers = false, weaponList = true;
  // audio (linear gains 0..1)
  float master = 1.0f, effects = 1.0f, ui = 1.0f, ambience = 1.0f;
  // controls: action -> key name overrides of content/settings/bindings.json (only the ones the user changed)
  std::map<std::string, std::string> keys;
  // interface
  float uiScale = 1.0f, editorFly = 1.0f, editorOrbit = 1.0f, editorZoom = 1.0f;

  GfxPreset preset() const;    // Low / Medium / High when gfx matches one exactly, else Custom
  json toJson() const;
  static UserSettings fromJson(const json& j);
  bool operator==(const UserSettings&) const = default;
};

class Settings {
public:
  UserSettings s;
  bool persist = true;         // false in self-tests: the user file is never read or written
  fs::path path() const { return exeDir() / "settings_user.json"; }
  bool load(const fs::path& p);
  bool save(const fs::path& p) const;
  void save() const { if (persist) save(path()); }
  // push every value into the running game (renderer targets, grass, audio, gore, bindings, ImGui scale...)
  void apply(App& app);
  // read back the values other UI can change (editor View menu, Loadout), so the window and the file follow them
  void capture(const App& app);
  void applyPreset(App& app, GfxPreset p) { if (p != GfxPreset::Custom) s.gfx = presetOpts(p); apply(app); }
  // getters for other systems
  float drawDist() const { return s.gfx.renderDist / 1200.0f; }   // the old multiplier (1 = Medium's 1200 m), used by the cull sites
  float renderDist() const { return s.gfx.renderDist; }
  float particleBudget() const { return s.gfx.particles; }   // multiplier on effect / fragment caps (1 = Medium)
  float fov() const { return s.fov; }
  float lookDegPerCount(float adsT) const { return s.mouseSens * (1.0f + (s.adsSens - 1.0f) * std::clamp(adsT, 0.0f, 1.0f)); }
  float pitchSign() const { return s.invertY ? -1.0f : 1.0f; }
  bool open = false;           // the Settings window
  int tab = 0;
  int applied = 0;             // apply() calls (tests)
  int windowFrames = 0;        // frames the window was drawn (tests)
private:
  std::string rebinding;       // action waiting for a key press
  std::string bindMsg;
  friend void settingsWindow(App& app);
};
Settings& settings();

// ---- hooks (one line each at the call site)
void settingsInit(App& app, bool selftest);   // app.cpp, after the renderer and bindings exist
void settingsWindow(App& app);                // drawn every frame in both modes (no-op while closed)
void settingsEndFrame(App& app);              // app.cpp, after SwapBuffers: FPS cap
bool settingsMenuItem();                      // "Settings..." menu entry / button helper: opens the window
