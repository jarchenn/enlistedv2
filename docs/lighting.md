# Lighting engine (L2)

The lighting engine adds four things to the forward renderer:

- **time of day**, a global Settings option;
- **many local lights** (clustered forward), with the nearest ones **shadowed**;
- **indirect light** from a DDGI-lite probe volume;
- **emissive materials**.

Code:

- `src/lighting.cpp` holds the engine.
- `src/lighting.h` holds the read-back and test API, the time-of-day presets and the app hook.
- `src/light_types.h` holds `LightDesc`, shared with L3's `src/lights.cpp`.
- The shader side lives in `kCommon` in `src/render.cpp`: `localLights()`, `giSample()` and `giAmbient()`, plus the terms in `shade()`, the leaf, grass and glass shaders, and the water shader in `src/water.cpp`.

Tests: `--selftest-lights` (`src/selftest_lights.cpp`). `--selftest-maps` also reports Night frame times.

## Day is the old look

At **14:00 (Day)** every lighting constant is exactly what it was before L2:

- sun direction, sun colour, sky, ground, fog and fill;
- the sky dome's zenith and horizon colours;
- the exposure range 0.9-1.45 with key 0.24;
- fixed exposure 1.0 in Creative and the benchmarks.

`todCompute` returns early with the Day constants. It doesn't evaluate the curves at 14:00.

Self-tests never read `settings_user.json`, so they always run at Day. Thumbnails and model sheets (`thumbMode`) also always render at Day, with no local lights and no probe light.

With **Medium probe GI at Day**, the image matches the GI-off image (the old fill term) to within the frame-to-frame noise. Measured in `--selftest-lights`:

| Comparison | Mean luma difference | Pixels differing by more than 12 levels |
|---|---|---|
| Medium GI vs GI off | 1.0 | 1.1 % |
| GI off vs GI off, next frame (noise floor) | 0.5 | 0.4 % |

Probe light only ever *adds* to the analytic term. Three things follow from that:

- A room can never get darker than with the old fill at Day.
- In the open sky by day the analytic model is used as before: the weight is `skyVis × uGiOpenK`, and `uGiOpenK` is 1 by day.
- By night the analytic term fades, and lamps and window spill take over.

## Time of day

**Settings** (Graphics tab, Lighting section):

- Time of day: Day / Dusk / Night / Dawn / Custom;
- an Hour slider;
- the value is saved in `settings_user.json` as `graphics.timeOfDay` and `graphics.hour`.

The hour is personal, like FOV: picking a graphics preset keeps it. `Renderer::lt.hour` is the live value.

| Preset | Hour | Sun |
|---|---|---|
| Day | 14:00 | the pre-L2 sun, elevation 46.5° |
| Dusk | 18:36 | 3.5° in the west |
| Night | 00:00 | sun −26°, the moon is the key light at 26° |
| Dawn | 05:00 | 3.5° in the east |

**Sun path.** The site is at 49° N, with north = −Z and east = +X. The sun turns about the celestial pole by 15° per hour, on the circle through the Day sun:

- solar noon is about 11:45 at 55.8°;
- sunrise is about 04:35 and sunset about 19:00.

The moon is opposite the sun (a full moon).

**Key light.** The sun is the key light while it is above −1°, otherwise the moon is. The renderer's `sunDir` is set to the key light for the duration of `render()` and restored afterwards. Code outside `render()`, such as thumbnails, always sees the Day sun.

Everything the key light drives follows it automatically: shadow cascades, cloud shadows, light shafts and decal lighting.

**Colours.** These are keyframed on sun elevation (`todCompute`):

- the sun or moon multiplier (warm and dim near the horizon; the moon is about 1/50 of the sun, blue);
- sky zenith and horizon;
- sky, ground, fill and fog;
- the twilight glow, which stays on the horizon after sunset;
- cloud lighting;
- stars and the moon disc (in `kSkyFS`).

**Exposure.** `nightK` runs from 0 at a sun elevation of +4° to 1 at −12°. It moves three values:

- the auto-exposure range, from 0.9-1.45 to 0.35-5;
- the key, from 0.24 to 0.11, so night stays dark instead of being normalised to day;
- the fixed exposure used in Creative and the benchmarks, which follows sky brightness up to about 3.2 at night.

## Local lights

`LightDesc` (`src/light_types.h`):

- `pos`, `radius`: the light falls to zero at the radius.
- `color` (linear) and `intensity`.
- a spot cone: `dir`, `cosInner`, `cosOuter`. With `cosOuter <= -1` the light is a point light.
- `flicker`, from 0 to 1.
- `shadow`: whether the light may get a shadow map.
- `id`: stable per light.

**Units.** A light of intensity 1 gives an irradiance of **0.25 scene units at 1 m** (`kLampUnit`; the Day sun is about 3.3). Falloff is inverse square with a smooth window to zero at the radius, clamped within 0.3 m.

**Per frame:**

1. `lightingAppFrame(app)`, called from `app.cpp` before `render()`, fills `Renderer::lights`. It calls `lightsGather(app, lights)` from L3's `src/lights.h` when that file exists (`__has_include`). `Renderer::testLights` is appended (self-tests).
2. The lights are sorted nearest first and capped by `lt.maxLights`: Low 64, Medium 256, High 512. Flicker is applied on the CPU as three sines per light id. The result is uploaded to SSBO binding 5.
3. **Froxel clusters.** A compute pass bins the lights into a 16 × 9 × 24 grid in view space: screen tiles × exponential depth slices from 0.5 m to 300 m, at most 64 lights per froxel (binding 6). Each froxel is tested sphere against box, in the unjittered camera projection.
4. `localLights()` reads the froxel of the fragment. Probe captures and the probe compute instead loop over every light (`uLightMode 1`).

Each shader that gets the term:

| Shader | Local-light term |
|---|---|
| `shade()`: world, terrain, models, the first-person weapon | GGX specular plus diffuse, the same AO treatment as the sun |
| Leaves | wrapped diffuse on both faces of the card |
| Grass | lit like the ground under the blade |
| Glass | highlights on the panes |
| Water | in-scatter and highlights |

## Shadowed lights

The nearest shadow-casting lights, up to `lt.shadowLights` (Low 0, Medium 4, High 8) within 45 m plus their radius, get a slot in a depth atlas:

- 6 × 512² tiles per slot, D32F, standard Z.
- A **point light** renders 6 cube faces at 92°.
- A **spot light** with `cosOuter > 0.25` renders one tile with its cone's field of view.

Rendering goes through `progShadow`. Items are culled by sphere and frustum: L1's `Frustum` from `src/frustum.h` when present, otherwise a local copy of the same interface. Mesh bounds come from L1's `GpuMesh::bmin/bmax` when present, otherwise from a one-time read-back of each vertex buffer.

Slots stick to light ids. A slot is redrawn when it is new or its light moved (at most 2 per frame), and one slot is refreshed round-robin each frame so moving objects update.

Sampling uses 4 PCF taps at the tile, with a normal offset and a pull toward the light. Texture unit 16 holds the atlas; the face matrices are in SSBO binding 7.

## Indirect light: probe volumes (DDGI-lite)

**Volumes.** Probe grids scroll with the camera (toroidal addressing):

- **near**: 16 × 8 × 16 probes at 1.5 m spacing (24 × 12 × 24 m);
- **far**: 16 × 6 × 16 at 6 m (96 × 36 × 96 m).

| Preset | Volumes | Probes captured per frame |
|---|---|---|
| Low | none (the old fill term) | 0 |
| Medium | near | 3 |
| High | near + far | 8 |

`lt.probeBudget` overrides the rate for tests.

**Placement:**

- A probe that falls in a room box from `InteriorSys::rooms()` is pulled into the room's free space: 0.35 m from the walls, 0.5 m above the floor, 0.3 m below the ceiling. That gives every interior its own probes, and they refresh at twice the rate of outdoor probes. `lightingAppFrame` collects the room boxes of enterable buildings within 90 m.
- A probe under the terrain is lifted 0.35 m above it, or disabled if it is more than one spacing deep.
- A probe that sees more than 25 % back faces is inside geometry and gets no weight.

**Scheduling.** Probes that are new or moved come first, nearest first. After that, the oldest capture goes first, weighted toward interiors and probes near the camera.

**Capture** (`probePass`). Up to 16 probes per frame are drawn in one layered pass:

- Each probe is a 16 × 16 cube, in a 2D-array target with 6 layers per probe.
- Each opaque item is drawn once, instanced into exactly the faces whose frustum and capture range it touches: 24 m near, 60 m far.
- The layer is selected in the vertex shader through `GL_ARB_shader_viewport_layer_array`. Without that extension, GI is off.
- The fragment shader is the lit forward shader (`kMainFS`) with `uCapture`. It writes radiance and a front-facing flag, uses the probe as the eye, and leaves out emission.
- The lighting in the capture includes the sun with its shadows, the sky, the local lights with their shadows, and the previous probes, which gives multi-bounce light that converges over updates.

**Projection** (compute, one work group per probe):

- Radiance becomes L1 spherical harmonics, stored as irradiance / π: E(n)/π = c.x + dot(c.yzw, n). The values sit in SSBO binding 8: 4 vec4 per probe (R, G and B coefficients, then position and valid).
- A direction with no geometry in range sees the sky, or, looking down, a ground plane at the terrain height under the probe. The ground plane is lit by the analytic ground colour plus local-light pools.
- In the open, the probe therefore reproduces the analytic hemisphere exactly: measured up 0.356 against sky 0.355, down 0.156 against ground 0.156.
- Each probe also gets an 8 × 8 octahedral tile of the distance to the nearest surface (mean, mean²), stored in an RG16F atlas on texture unit 17.
- New values are blended at 0.6 with the previous capture.

**Sampling** (`giSample`): 8 probes, trilinear, with a backface weight and a Chebyshev visibility test from the distance tiles, which stops light from leaking through walls. The near volume fades into the far one over its outer cell. Coverage below 1 keeps the analytic term for the rest.

- `shade()` replaces the hemisphere term and the occluded specular environment.
- Leaves, grass and water use `giAmbient`.
- Glass uses the probes for reflections where the sky is hidden.
- `gInd` (attachment 2) still carries the indirect term, so SSAO keeps working.

## Emission

Materials take `"emissive": [r, g, b]`, a linear HDR value parsed in `assets.cpp` as `Material::emissive`. It travels as vertex attribute 6, `Vertex::emis`, and is added in `kMainFS` (and as a full-strength add on glass) after lighting and before fog. It lands in the HDR target before bloom, so lamp glass and flames glow.

- `DrawItem::glow` multiplies it per item; 0 means the lamp is off.
- `Renderer::lt.emissive` multiplies it globally.
- Probe captures leave emission out, so a lamp's light isn't counted twice.

## Texture units, buffers and hooks

**Texture units:**

- 16: light shadow atlas;
- 17: probe distance atlas;
- 18-19: probe capture colour and depth, bound only during the compute pass;
- 20-23: reserved for L4.

The 2080 Ti reports 32 fragment texture units; GI needs at least 20.

**SSBO bindings** (0-3 are skinning and decals):

- 5: lights;
- 6: froxels;
- 7: light shadow matrices;
- 8: probes;
- 9: capture matrices.

**Hooks in `render.cpp`** (one line each):

- `lightingInit` at the end of `init`;
- `lightingBegin` / `lightingEnd` around the frame;
- `lightingPrepass` after the sky-occlusion pass, still in standard Z: light upload, light shadows, probe captures, clusters;
- `lightingTodColors` + `lightingUniforms` in `setLighting`;
- `lightingExposure` and `lightingFixedExposure` for exposure;
- the `uGlow` lines in `drawItems`.

**Hook in `app.cpp`:** `lightingAppFrame`.

## Cost

GPU time from `--selftest-lights` on the 2080 Ti, Blank Slate with a cottage and the test room:

| Case | GPU time |
|---|---|
| Day, GI off | about 3.1-3.7 ms |
| Day, Medium GI | the same; the open sky skips the probes |
| Night, Medium, one shadowed lamp | about 3.8-4.1 ms |

See `--selftest-maps` (`summary_maps.json`, `presetWorstMs.MediumNight`) for the default maps at Night.

## Tests (`--selftest-lights`)

**Time of day:**

- The four presets: sun path, key light, moon, stars, exposure.
- Day exactly the old constants, and continuous at 13:59.
- Solar noon.
- The Settings file round trip and `apply`.
- A frame at each preset: no non-finite HDR pixel, Day > Dusk > Night, warmer sky at dusk, dark sky at night.

**Clusters:** 48 lights; the GPU froxel lists equal a CPU replica (3456 of 3456 froxels).

**Test room.** The test writes and loads a closed room asset with a partition and no openings, and runs these checks at Night:

- A lit room is brighter than an unlit one: 77.9 vs 12.3 mean luma.
- Indirect light reaches the partition face turned away from the lamp, checked two ways:
  - in the probe read-back (`lightingGiAt`);
  - in the image, with GI on vs off: 31 vs 7 luma.
- No leak through the wall:
  - the probe irradiance outside changes by 0.00000 while the same wall inside gains 0.020;
  - the outside image is unchanged.
- The lamp's shadow map darkens the floor behind the partition: 15 vs 83 luma.

**Day:**

- Medium GI matches the old fill term within the noise floor.
- Inside the stone cottage, the image is never darker than with the old fill.
- An open-air probe equals the analytic sky and ground.

**Emission:** the material is parsed, and an emissive box glows at night.
