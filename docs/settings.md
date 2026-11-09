# Settings, graphics presets and firing smoke

User items 5 and 6 of the visual & physics overhaul (stream V2, `docs/plan-visual-physics-overhaul.md`).

## The Settings window

Open it from any of these:
- **Creative:** the **Settings...** entry in the top menu bar, next to "Explore from start".
- **Explore:** the pause menu (Esc), **Settings...**.
- **Loadout (L):** the **Settings...** button.

Loadout no longer carries its own render-quality, cloud, grass and gore controls; they are in Settings. Loadout keeps a small **Look development** section with the tuning values that are not part of any preset: shadow softness, haze density, cloud cover and the legacy look.

The window closes by itself when play resumes in Explore (the pause menu and Loadout both closed), because the cursor goes back to the game.

Every change applies immediately. Two sliders apply when you release them instead, because they rebuild things: **Resolution scale** recreates the render targets and **UI scale** resizes the window you are dragging in. The file is written when no control is being dragged and when the window closes.

### Graphics

| Control | What it changes |
|---|---|
| Preset | Low / Medium / High. Any change away from a preset shows **Custom**. |
| Resolution scale | Internal render size = window × scale (0.5–1). The final blit upscales it bilinearly. |
| Shadow quality | Near / far shadow-map sizes: Low 2048 / 1024, Medium 4096 / 2048, High 4096 / 4096. |
| Far shadow cascade, Cloud shadows, Ambient occlusion, Full-resolution AO, Screen-space reflections, Bloom, Light shafts, Temporal AA | `Renderer::Quality` flags and the AO target size. |
| Clouds | The cloud layer. Cloud cover is under Creative > View > Sky. |
| Grass | `GrassQuality` Off / Low / Medium / High (`grassPreset`, `grass.cpp`; tiers in `content/settings/grass.json`, docs/ground-grass.md#gx3). |
| Grass wind | `grassWind`: vertex wind on the near grass clumps (within 13–15 m). Low turns it off. |
| 4x MSAA | `Renderer::setMsaa`: the opaque world (sky, terrain, solids, models, foliage, grass) renders into a 4x multisampled copy of the HDR target's world attachments, resolved (colour averaged, depth one sample) before water, decals and TAA. `"msaa": 4 / 0`. |
| Ivy on walls | `App::ivyOn` (`ivy.cpp`): the assets' `"ivy"` leaf cards. Their density follows Grass: none at Off, 60 % at Low. `"ivy"`. |
| Render distance | Metres (PF2; 300-5000, stops Low 600 m, Medium 1.2 km, High 2.5 km, Ultra 5 km). Drives the hard cut of every instance in `App::buildDrawLists`, and x renderDistance / 1200 the nature (1200 m), tree (900 m), ivy (260 m) and decor (`w5CullDraw`, rules in `content/settings/maps.json`) culls; the far terrain LOD rings (x0.6-1.5, LOD0 unchanged); the Explore far plane (at least 4 km, 1.2 x the distance); and the haze density (0.001 / m x 1200 / distance, clamped 0.4-2, so the haze reaches the same depth at the cut). Files with the old `drawDistance` multiplier migrate to `renderDistance` = 1200 m x it.
| Particle budget | Multiplier on the effect caps (`Fx` particles 900, streaks 160, debris 72). Other systems read it through `settings().particleBudget()`. |
| Field of view | Vertical FOV on foot, 72° by default. ADS blends from it. Optics keep their real magnification of the 72° view. |
| Vsync, FPS cap | Swap interval, and a sleep limiter after `SwapBuffers` (Unlimited, 30–240). |

The FOV, vsync and FPS cap are personal: picking a preset never changes them.

### Gameplay

| Control | What it changes |
|---|---|
| Mouse sensitivity | Degrees per mouse count in the 72° reference view (0.100 by default). It is still matched across FOVs (`App::lookScaleFor`), so irons, scopes and tank sights move the picture by the same distance per count. |
| Aiming sensitivity | An extra factor, blended in with the ADS state (`adsT`). |
| Invert mouse Y | Flips the pitch. |
| Reduced motion, Show arms, Sight calibration markers | The same flags as before (`App::reducedMotion`, `vm.showArms`, `App::sightMarkers`). |
| Gore | Off / Reduced / Full. This is still stored in `gore_user.json` by `gore.cpp`. |

### Audio

Master, effects, interface and ambience volumes (`Audio::setGains`):
- **Master** is the miniaudio engine volume.
- **Effects** scales every spatial `play()` and every looping voice (tank engines, tracks).
- **Interface** applies to events whose name starts with `ui_` or whose manifest `kind` is `"ui"`.
- **Ambience** scales the wind and leaf beds.

### Controls

The six rebindable Explore actions from `content/settings/bindings.json`: fire mode, prone, crouch, deploy / pack up, grenade and smoke grenade. Click the key, then press the new one; Esc cancels.

Fixed keys (WASD, E, R, Q, T, X, V, B, L, F3–F6, Esc...) and keys already bound to another action are refused, with a message. The window also lists the fixed keys.

Overrides are stored in `settings_user.json` and applied at the end of `App::loadBindings` (`settingsApplyKeys`). **Reset keys** goes back to `bindings.json`.

### Interface

| Control | What it changes |
|---|---|
| UI scale | ImGui `FontGlobalScale`. |
| Editor camera fly speed | The Alt+wheel multiplier (`NavState::speedMult`). Alt+wheel changes show up here too. |
| Orbit speed, Zoom step | Multipliers on `orbitDegPerPixel` and `zoomStep` from `content/settings/editor.json`. |

## Presets

**Medium is exactly the built-in defaults.** A missing, deleted or broken settings file renders exactly as the game did before V2.

| | Low | Medium | High |
|---|---|---|---|
| Resolution scale | 0.85 | 1.0 | 1.0 |
| Shadows (near / far) | 2048 / 1024 | 4096 / 2048 | 4096 / 4096 |
| Cloud shadows, light shafts, SSR, SSAO | off | on | on |
| SSAO resolution | — | half | full |
| TAA jitter phases | 8 | 8 | 16 |
| Grass | Low | Medium | High |
| Render distance | 600 m | 1.2 km | 2.5 km |
| Particle budget | ×0.6 | ×1 | ×1.5 |
| 4x MSAA | off | off | on |
| Ivy on walls | off | on | on |

Measured by `--selftest-settings` (RTX 2080 Ti, 1920×1080, one farmhouse on Blank Slate, reported only): Low 2.96 ms, Medium 3.77 ms, High 6.23 ms. `--selftest-maps` reports Low / Medium / High for every eye-level view of the shipped maps; only Medium is gated.

## The file

`exeDir()/settings_user.json`, next to `gore_user.json`. It is written by the window only.

```json
{
  "version": 1,
  "graphics": { "preset": "Custom", "resolutionScale": 1.0, "shadowQuality": 1, "farCascade": true, "cloudShadows": true,
                "lightShafts": true, "ssr": true, "ssao": true, "ssaoFullRes": false, "bloom": false, "taa": true, "taaPhases": 8,
                "clouds": true, "grass": 2, "renderDistance": 1200, "particleBudget": 1.0, "fov": 72, "vsync": true, "fpsCap": 0 },
  "gameplay": { "mouseSensitivity": 0.1, "adsSensitivity": 1.0, "invertY": false, "reducedMotion": false, "showArms": true, "sightMarkers": false },
  "audio": { "master": 1, "effects": 1, "ui": 1, "ambience": 1 },
  "controls": { "grenade": "G" },
  "interface": { "uiScale": 1, "editorFlySpeed": 1, "editorOrbitSpeed": 1, "editorZoomSpeed": 1 }
}
```

Loading works like this:
- `preset` is read first, and the values after it override it. A file holding only `{"graphics":{"preset":"Low"}}` is the Low preset with default everything else.
- Values are clamped to the slider ranges.
- An unreadable file falls back to the defaults and is logged.

**Self-test runs never read or write the file** (`Settings::persist = false`) and never apply it, so every suite runs on Medium whatever the user has chosen.

## Code

| File | Contents |
|---|---|
| `src/settings.h/.cpp` | `UserSettings` (values, JSON), `presetOpts()`, `Settings` (load, save, `apply()` into the game, `capture()` back from it), the window, and the hooks `settingsInit`, `settingsWindow`, `settingsEndFrame`, `settingsMenuItem` and `settingsApplyKeys`. |
| `src/render.h/.cpp` | Target, shadow and composite setup only; no material shaders (see below). |
| `src/audio.h/.cpp` | `setGains` and the per-category gains. |
| `src/selftest_settings.cpp`, `cmake/settings.cmake` | The `--selftest-settings` suite and its build file. |

The renderer changes in `render.h/.cpp`:
- `setShadowSizes()` recreates the shadow maps through `createShadowTargets()`.
- `setResolutionScale()`: `resize()` keeps `displayW`/`displayH` (the window) and renders at `width`/`height` = display × scale. The final `glBlitNamedFramebuffer` to the window upscales with `GL_LINEAR` when the sizes differ.
- `setSsaoFull()`: the AO target is width / `aoDiv`, and the SSAO and composite texel uniforms follow it.
- `Quality::taaPhases` sets the Halton jitter length.

`readPixels`, screenshots and the sight-marker pixel maths all work in the internal size, as before.

The hooks are one line each:
- **`app.cpp`:** `settingsInit` after `R.init`, before the `--rq-off` flags; `settingsWindow` after `mpaiUI`; `settingsEndFrame` after `SwapBuffers`; the FOV; the three hard draw cuts.
- **`editor.cpp`:** the menu entry.
- **`explore.cpp`:** the pause-menu button, and the sensitivity and invert-Y factors in mouse look.
- **`weapons.cpp`:** the Loadout button, the ADS FOV base and the backblast.
- **`viewmodel.cpp`:** the `fpCamera` FOV.
- **`w5_maps.cpp`:** the cull multiplier. It is applied to the result, so the cached rules need no reset.
- **`arsenal.cpp`:** `settingsApplyKeys`.

**For other streams:**
- `settings().particleBudget()`: the multiplier for fragment caps (V4).
- `settings().drawDist()`: the cull multiplier (V8 decor ring).
- `settings().fov()`: the hip FOV.

## Firing smoke

`Fx::muzzle` and the new `Fx::backblast` read their parameters from `content/settings/fx.json`. Sizes scale with each gun's `muzzleScale`:

| Gun | muzzleScale | Where it is set |
|---|---|---|
| Tiger I, Sherman Firefly | 1.2 | the vehicle JSON `gun.muzzleScale` (was hard-coded 1.2 in `tank_sim.cpp`) |
| Flak 18 | 1.4 | the station `muzzleScale` |
| PaK 40 | 1.0 | the station `muzzleScale` |

| | Before | Now |
|---|---|---|
| Puffs per discharge | 20 (6 per side + 8 forward) | 8 (2 per side + 4 forward) |
| Alpha | 0.5–0.55 | 0.28 |
| Life | 3–5 s | 1.2–2.4 s |
| Growth | 1.5–1.6 | 2.2 |
| Fade | hold, then fade at the end | `fadePow` 1.6: alpha × (life left)^1.6 after a quick fade-in, so the cloud thins from the start |

The launcher backblast goes from 6 puffs at alpha 0.5 to 5 puffs at alpha 0.4, living 1.0–1.5 s with the same fade curve.

The smoke is gone about 2.4 s after the shot; `--selftest-settings` checks the count, the alpha and the clearing.

## Tests

- **`--selftest-settings`** (report `settings`):
  - Medium equals the defaults, checked field by field against `Renderer::Quality{}`, the shadow, AO and resolution targets, grass, effect caps, FOV and sensitivity. A fresh game captures as Medium.
  - Low, Medium and High are applied live: target sizes and flags, every FBO complete, every shader compiled, a screenshot each (`settings_Low.png` and so on), frame time reported.
  - Custom detection.
  - The file round-trip, a preset-only file and a broken file.
  - Aim-sensitivity blending and invert Y.
  - Audio gains.
  - The firing smoke and backblast, and the `muzzleScale` loading.
  - The window opening from the Creative menu path, the pause menu and Loadout, and closing on resume.
  - The user file is left untouched.
- **`--selftest-maps`:** every gameplay (eye-level) view is measured at Medium (gated < 16.7 ms as before), then again at Low and High (reported, with `_low`/`_high` screenshots). The per-map worst values go to `summary_maps.json` (`presetWorstMs`).
