# Renderer: BSL-referenced lighting and atmosphere pass

This pass improves sunlight and sky balance, shadows, atmosphere, contact shading, bloom, reflections and anti-aliasing in the existing C++20 / OpenGL 4.5 renderer (`src/render.cpp`). The visual reference was the supplied BSL v10.1.8 shader pack by Capt Tatsu.

## Licensing

The archive contains no licence file, and every shader carries only the header "BSL Shaders v10 Series by Capt Tatsu". The author's site (capttatsu.com) did not show usable reuse or redistribution terms when checked for this pass. Reuse was therefore treated as **not permitted**:
- No BSL code, constants tables or textures were copied into the project.
- Every effect below is an original implementation, written from general, published techniques (named where used).
- BSL was used only to see which effects matter and how they are layered.
- The pack stays outside the repository. `docs/credits.md` credits it as the visual reference.

## 1. Pipeline audit: what BSL needs and what this engine has

| BSL requirement | BSL source (inspected) | Engine equivalent |
|---|---|---|
| Deferred G-buffers (`gbuffers_*`: albedo, normal, specular, lightmap) | `program/deferred*.glsl`, `lib/surface/materialDeferred.glsl` | **Forward** renderer into RGBA16F HDR. Added: world-pass MRT attachments 2 (indirect light), 3 (world normal + roughness) and 4 (motion). The weapon pass writes attachments 0 (colour) and 1 (eye distance) only. |
| `depthtex0/1/2` (with / without translucents, hand) | `composite*.glsl` | One D32F world depth (`depthTex`, **reverse-Z** since V1: near 0.03 → 1, far 4000 → 0; see §9.1). Glass and ghosts don't write depth. The weapon is drawn after a depth clear with its own projection, so the world post-passes run **before** it and never see its depth. |
| Hand mask (`z < 0.56` "hand" tests) | `ambientOcclusion.glsl`, `taa.glsl` | Not needed: the first-person pass happens after AO, shafts, SSR and TAA, unjittered. |
| `shadowtex0/1` + `shadowcolor0`, distorted single map | `lighting/shadows.glsl` | Two orthographic cascades: near 4096² (±40 m in Explore) and far 2048² (≥ ±240 m). Distortion was rejected: large building quads would warp between vertices. No coloured shadows (no stained glass). |
| Lightmap (block / sky light) | `forwardLighting.glsl` | Sky light = top-down sky-occlusion map (`skyVis`), plus material AO and SSAO on indirect light. Block light has no equivalent (no torches); not emulated. |
| Block IDs (foliage, water, emissive) | `block.properties` | Per-shader material class: leaf and grass programs get subsurface and soft shadows; terrain puddles get low roughness for SSR. No emissive sources. |
| Biome / weather / time-of-day uniforms | `lib/color/*.glsl` | One art-directed Normandy daylight: fixed sun, cloud cover slider, wind toggle. Dimension logic doesn't apply. |
| Exposure (`eyeBrightnessSmooth`) | `composite5.glsl` | Existing auto-exposure from the HDR mip average (Explore); fixed exposure in Creative and in benchmarks. |
| Tonemap + gamma in `final` | `composite5.glsl` / `final.glsl` | One display transform in `kToneFS`: exposure → tonemap → grade → one sRGB encode. No sRGB framebuffer, so nothing is applied twice. ImGui and the HUD draw after the blit, outside exposure, bloom and TAA. |

**Coordinate spaces:**
- World: +y up, metres.
- View: GL, −z forward.
- Shadow cascades: `stableOrtho` with texel-snapped focus (no shimmering).
- Post passes reconstruct view and world positions from `depthTex` with the inverse of the projection that rendered it (jittered under TAA).

## 2. Pass order (Explore)

1. Shadow maps: near and far cascades, then sky occlusion.
2. World HDR pass (jittered when TAA is on, reverse-Z): sky; then terrain, opaque, world models, foliage and grass. The last group writes attachments 0 / 2 / 3 / 4.
3. **Water** (V1, §9.4): surfaces of the water assets, after the opaque world, writing colour, normal + roughness and depth.
4. Gore decals; transparent glass and placement ghosts (attachment 0 only).
5. **World composite** (`worldComposite`): SSAO (half res), light shafts (half res), then one composite. It applies SSR, removes the occluded share of indirect light, and adds the shafts.
6. **TAA resolve** (`taaResolve`), world only.
7. Bloom chain from the world; then effects (smoke, flashes, tracers, impacts), unjittered, with their own blend state. Until the fix, the composite and TAA passes left blending off, so effects drew as opaque squares.
8. First-person weapon and arms: depth clear, own projection (also reverse-Z), unjittered.
9. Exposure → tonemap → LDR → editor overlays → blit → ImGui / HUD.

## 3. What was implemented (stage by stage, each visually checked)

**Stage 1: lighting, shadows, materials, tonemap.**
- **Sun : sky balance.** Raised from about 1.75:1 to about 3:1 on lit diffuse. The sky fill is cooler, so shadows read blue-grey without going black.
- **Shadows.** Two cascades with Vogel-disk PCF (8 taps, rotated per pixel and frame by interleaved gradient noise, so TAA integrates the rotation). The normal offset grows with the slope to the sun, plus a small slope-scaled depth bias. The cascades blend at 78–96 % of the near radius, and the far cascade fades out at its edge.
- **Foliage.** Leaves take a blurred 8-tap shadow and back-lit subsurface tinted by the leaf, and the inner canopy sees less sky. Grass takes a 4-tap soft shadow.
- **Cloud shadows.** The same fBm field the sky draws, projected along the sun onto the ground, so the shadows match the visible clouds.
- **Material AO.** Now darkens indirect light only, not direct sun.
- **Tonemap.** A rational shoulder `x/(1+x^p)^(1/p)` (p = 1.7), applied 55 % per channel (highlights desaturate like film) and 45 % on luminance (hue kept: no orange or yellow drift), with a gentle toe. It replaces ACES, which oversaturated the greens to lime.

**Stage 2: sky, clouds, distance haze, light shafts.**
- **Sky.** An exponential horizon-to-zenith gradient, plus a warm forward-scattering halo on the sun side and a slightly deeper anti-solar sky.
- **Clouds.** World-anchored, so they have parallax and match their ground shadows. They are self-shadowed: three samples toward the sun, Beer–Lambert attenuation, blue-grey bases. They have a forward-scattering silver lining and fade into the horizon haze.
- **Aerial perspective.** Height-falloff haze integrated analytically along the view ray: `tau = rho0·L·e^(−y_eye/H)·(1−e^(−dy/H))/(dy/H)`. Defaults are ρ0 = 0.001 /m and H = 70 m, with a clear 12 m combat zone. The haze colour is the horizon sky in that direction plus a sun glow.
  - The first try at twice this density washed the village out. It was rejected after inspection.
- **Light shafts.** Half resolution, 14 dithered steps along the view ray up to the world depth (≤ 150 m), sampling both cascades, with a forward Henyey–Greenstein lobe (g = 0.72) at 0.35 strength. Beams show toward the sun and add nothing looking away.
  - The first version used a partly isotropic phase. It read as uniform fog and was rejected.

**Stage 3: contact shading, bloom, foliage stability.**
- **SSAO.** Half resolution: 8 spiral samples in a 0.6 m world radius (wider far away), with normals rebuilt from depth (smaller neighbour difference, no silhouette smear) and a range fade (no halos).
  - A depth-aware 4×4 upsample blur follows.
  - It subtracts `indirect·(1−ao)·0.85` using the indirect light the world shaders wrote, so sunlit faces are never darkened.
  - Grass counts 0.6 and canopies 0.5, since they're sparse occluders.
- **Bloom.** A six-level downsample chain (first level luminance-weighted against fireflies) with a tent upsample, normalised and blended at 4.5 % (energy-preserving mix, no threshold). It is built from the lit world *before* effects are drawn, so muzzle flashes, tracers and impact sparks keep their original look (no glow).
- **Leaf alpha.** Distance-aware: leaves grow by `fwidth(uv)` once a card covers few pixels, so distant canopies don't break into crawling single-pixel sparkles. TAA then resolves the remaining sub-pixel coverage.

**Stage 4: reflections and TAA.** These were added only after the buffers they need existed: normals, roughness, motion and history.
- **SSR.** Only for roughness < 0.4: puddles, wet ruts, polished metal.
  - It marches the view-space reflected ray (28 growing steps, dithered) against world depth with a thickness test and 5-step bisection.
  - On a hit it replaces the sky estimate already in the lit colour, weighted by Schlick Fresnel, roughness, screen edges and ray length. A miss keeps the environment and sky reflection.
  - Hazed-out surfaces are excluded.
- **TAA.** Halton (2, 3) 8-phase jitter at ±0.25 px, world projection only.
  - **Reprojection.** Static geometry reprojects from depth with the *unjittered* current and previous matrices. Wind-animated leaves and grass write their own motion vectors: each re-evaluates last frame's sway and projects it with last frame's matrix.
  - **History.** Catmull-Rom history fetch, then YCoCg 3×3 variance clipping (±1.1σ) in a luminance-compressed space. The current-frame weight is 0.08, rising to 0.30 with motion.
  - **Sharpening.** An unsharp mask (0.30 since V1, was 0.40: it amplified facade shimmer, §9.5) on the displayed image only; it never enters the history.
  - **Invalidation.** History is invalid after a resize, a camera jump over 4 m (teleport, respawn, Creative↔Explore) or a disabled frame.
  - **FOV changes.** ADS zoom reprojects correctly because the previous matrix carries the previous FOV.
  - **Weapon effects.** The weapon, sights, muzzle flash and tracers are drawn after the resolve and unjittered, so they cannot ghost.
  - Two defects were found and fixed by pixel A/B crops:
    - History was reprojected with the jittered inverse, which resampled it half a pixel off every frame and blurred it.
    - The ±0.5 px jitter blurred edges noticeably; it is now ±0.25 px with output sharpening.

**Sight blur.** The ADS depth-of-field is off by default and its toggles were removed (Loadout window, View menu). The code path stays, disabled.

## 4. Quality toggles and fallback

`Renderer::q` (`src/render.h`) holds every switch:
- `legacy`: the full pre-pass look, with the same constants, ACES tonemap and 3×3 shadows.
- `farCascade`, `shadowSoft`, `cloudShadows`, `shafts` / `shaftStrength`, `hazeDensity` / `hazeHeight`.
- `ssao` / `ssaoStrength` / `ssaoRadius`, `bloom` / `bloomStrength`, `ssr` / `ssrStrength`.
- `taa` / `taaSharpen` / `taaJitter`.

Command line:
- `--legacy-look` restores the old look; the baseline frames and `--legacy-look` frames are pixel-identical.
- `--rq-off <taa|ssr|ssao|bloom|shafts|cascade|cloudshadow|wind>` switches one feature off for A/B.

## 5. Correctness checks done

- **Shader compilation.** `shaderErrors` stays 0 (every suite asserts it). The shared `kCommon` no longer carries `#version`: `compile()` prepends the version and an `NMK_VS` / `NMK_FS` stage define, so fragment-only helpers (`gl_FragCoord` noise, `fwidth`) can live in it.
- **Framebuffer completeness.** Every target is checked after creation and after each resize (`fboErrors`): HDR with five attachments, LDR, composite, shafts, AO, TAA ×2, bloom ×6, both shadow cascades.
- **Resizing and minimised windows.** The sights suite renders at 1920×1080, 1440×1080, 2400×1080 and 1280×720. A minimised window reports a 0×0 framebuffer; the old code then rendered with aspect 1.0 into the 16:9 targets, which showed up as horizontally stretched benchmark frames. It now keeps the targets' aspect (`app.cpp`).
- **Shadow acne and peter-panning.** Inspected on walls and shutters, tree trunks, the kerb and poles in the village frames: contact holds and no acne bands.
- **Transparency.** Glass and ghosts are drawn after the MRT world pass with attachment 0 only, so they can't corrupt the AO, normal or motion buffers.
- **Foliage alpha edges.** Checked with the TAA on/off pixel crops (`--rq-off taa`).
- **Near/far depth.** All reconstruction uses the exact projection that wrote the depth; sky pixels (depth 0 under reverse-Z, §9.1) are skipped (SSAO, SSR) or reprojected from the far plane (TAA).
- **Weapon presentation.** `--selftest-sights` re-run after the pass (see `docs/testing.md`).

## 6. Performance (same machine, same resolution, back-to-back)

Setup: 1920x1080, grass Medium, vsync off, RTX 2080 Ti, the seven fixed village cameras (Creative view, fixed exposure), 1 s warm-up then about 4 s per camera.

**Measurement conditions.** The user's own NormandyMapmaker instances were running throughout (GPU about 80 % busy before my runs started). Absolute numbers are inflated and drift between runs. The checkpoint build measured 9.5-12.2 ms (`benchmark_village_bench_bsl_before.json`) about an hour earlier on the same code the legacy look reproduces. So the comparison that counts is the legacy look in the *same* final build, run immediately before and after the current look:

| Camera | GPU p50 ms: legacy look (run before) | **current look** | legacy look (run after) | GPU p95 ms (current) | Frame p50 ms (legacy / current) | Draws (legacy / current) |
|---|---|---|---|---|---|---|
| overview | 5.71 | **9.98** | 6.36 | 11.63 | 7.63 / 11.87 | 1674 / 1931 |
| road | 5.74 | **9.92** | 6.10 | 11.36 | 7.15 / 11.74 | 1674 / 1931 |
| area | 5.81 | **10.20** | 6.10 | 11.61 | 7.19 / 12.12 | 1673 / 1930 |
| foliage | 5.34 | **11.08** | 6.92 | 12.29 | 8.35 / 12.74 | 1674 / 1931 |
| building | 5.46 | **7.52** | 6.16 | 8.85 | 7.71 / 9.40 | 1673 / 1930 |
| lane_north | 4.29 | **5.78** | 4.38 | 6.49 | 5.41 / 7.49 | 1676 / 1933 |
| into_sun | 5.21 | **6.75** | 5.52 | 8.08 | 6.50 / 8.69 | 1670 / 1927 |
| **mean** | 5.36 | **8.75** | 5.94 | | | |

The current look costs about **+2.8 ms of GPU time per frame** on average (x1.5 against the legacy look) at 1080p on this GPU, under contention. Per-feature A/B (`--rq-off`, back-to-back; each figure is the mean-GPU saving when that feature is off):

| Feature | Saving |
|---|---|
| far shadow cascade | about 0.3 ms |
| SSAO | about 0 ms (within noise) |
| light shafts | about 0.25 ms |
| TAA | about 0.3 ms |
| SSR | within noise |
| per-fragment cloud shadow | 0.8 ms on average, 2.1 ms on the overview camera; now baked into a 512² map per frame |

The rest is the softer shading itself: 8-tap two-cascade PCF in every lit fragment, filtered foliage and grass shadows, and sky cloud self-shadowing. Fixes made on these measurements:
- cloud shadow baked per frame (−1.0 ms mean);
- sky self-shadow on a 3-octave field;
- PCF reduced from 12 to 8 taps, since TAA integrates the per-frame rotation.

Raw data: `docs/rendering/benchmark_village_bench_{pL3,after,legacy,bsl_before}.json`.

## 7. Screenshots

These are in `docs/rendering/`, all 1920x1080 at the same fixed cameras:
- `before_{overview,road,foliage,building}.png`: the checkpoint build (commit 6535d8c).
- `after_{...}.png`: the final build.
- `legacy_into_sun.png` / `after_into_sun.png`: the sun-facing camera added for the light shafts and back-lit leaves, as an A/B inside the final build. `--legacy-look` frames are pixel-identical to the checkpoint build's at the same camera.

Stage-by-stage frames (s1-s4) and the TAA / SSR pixel A/B crops were inspected during the work and are not shipped.

## 8. Not done / limits

- **No coloured or translucent shadows.** The scene has no stained glass; water (§9.4) neither casts nor receives caustics.
- **No block light, night or weather cycle.** The game has one daylight setting.
- **SSR is screen-space only.** Reflections of off-screen objects fall back to the sky. Glass windows keep their analytic sky reflection, since they're forward-blended after the composite.
- **No velocity for rigid moving bodies** (debris, felled trunks, swinging doors). They rely on TAA clipping, which can leave a faint trail on a fast-moving door.
- **Grass, terrain materials and tree geometry are unchanged by this pass.** Its only foliage changes are lighting and alpha stability. Asset work is still needed, and the shader work doesn't replace it.

## 9. V1 visual overhaul: depth convention, facade shimmer, rough walls, water

Plan: `docs/plan-visual-physics-overhaul.md` (V1, user items 1g and 0a water). Suite: `--selftest-render` (`src/selftest_render.cpp`, [testing.md](testing.md)).

### 9.1 Depth convention: reverse-Z

The world camera passes of `Renderer::render` are reverse-Z:
- `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`, depth cleared to **0**, `GL_GREATER` (sky `GL_GEQUAL`).
- The projection is converted inside the renderer (`reverseZ()` in `render.cpp`: z' = (w − z) / 2, built in double). The near plane (0.03 m) maps to 1 and the far plane (4000 m) to 0. The app's `projM` and every CPU use of it (picking, projection of markers, culling) stay standard GL.
- Float depth then keeps roughly constant *relative* precision. At 100 m the old standard 0..1 mapping resolved about 2 cm; layers millimetres apart (shutters, hinges, trim) fought. Reverse-Z resolves well under a millimetre there.
- Shadow cascades, sky occlusion, thumbnails (`thumbs.cpp`), model sheets and tank previews keep the standard convention; `render()` sets standard clip control at its start and restores it at its end.
- The first-person weapon uses its own projection, also converted; it still clears depth (now to 0) before it draws.

Depth readers patched for it (ndc z = depth, sky = 0):
- SSAO (`kSsaoFS`), the world composite (SSR and the AO blur, `kCompositeFS`), light shafts (`kShaftFS`) and TAA reprojection (`kTaaFS`: the sky lands on the far plane with ndc 0).
- Gore world decals (`decals.cpp`): reconstruction, sky test, `GL_GEQUAL`, and a polygon offset that now pulls toward the eye with positive values.
- Fog is computed per fragment from the world position and needs nothing; picking is a CPU ray cast.

Code drawing inside `render()` takes its depth state from `Renderer::zLess()`, `zLEqual()` and `zToward()` (polygon-offset sign), and `Renderer::revZ` says the convention is active. The terrain and grass chunk streams still test the jittered reverse-Z `vp`, so they have no far plane (their GL-style tests stay conservative; the far plane is 4 km). The per-item culling of §10 builds its planes from the standard-Z projection and keeps the far plane.

### 9.2 Facade shimmer

Causes found and fixed:
- **Coplanar skins.** `raggedWall` (`tools/catalog_village2.mjs`) ran the inner plaster over the whole outer length, so at every corner the plaster end face lay in the crossing wall's stone face. It now takes `inA`, the inner span inset like `shell()` does. Affects `vil_church_ruined` and `vil_chateau_keep`.
- **Shutters 5 mm off the wall** (`tools/parts.mjs` `window()`): the battens now stand 2 cm off the wall, the boards 3.5–7 cm, the strap hinges sit on the boards and the hinge pivot is 7.5 cm out. Only the `vil_*` family was regenerated (`vil_norman_farmhouse`); the older houses from `build_assets.mjs` keep their 5 mm shutters until that generator is re-run, which reverse-Z covers meanwhile.
- **Depth precision:** §9.1.
- **Unfiltered detail.** `kMainFS` now knows the pixel footprint (`gPix`, metres per pixel). Helpers fade detail toward its mean once it nears two pixels instead of point-sampling it:
  - `res(freq)` (1 resolved .. 0 sub-pixel), `vnF` / `fbmF` (filtered value noise / fBm);
  - `lineAA(mask, width, mean)` for joints, arrises, pits and seams;
  - hairline cracks widen to the pixel but keep their integrated darkness.
  - The 85–230 cycles/m limewash grain, ashlar tooling, rubble pits, brick joints and courses, wallpaper motifs and seams all go through them.
- **Derivative bump.** It uses per-pixel (fine) derivatives instead of 2×2 coarse ones. The relief of joints fades to its mean recess sooner than their tone, because a groove one or two pixels wide is exactly what TAA jitter turns into shimmer.
  - Widening grooves to at least 1.5 px instead made 20 m shimmer *worse* (2.11 → 2.51).
  - Taking the relief slope over the pixel footprint (three evaluations of the height function instead of screen derivatives) measured no better than the plain derivative bump (2.11 vs 2.01). Both were dropped.
- **Shadow and sky-occlusion lookups** offset along the geometric normal when a wall material bumped N. It is the correct receiver offset, but it measured no change on the sunlit test facade.
- **TAA:** the output unsharp mask goes from 0.40 to 0.30. It is the strongest TAA lever on the static index: 0.20 cuts it a further ~12–15 % at the cost of visible softness. Narrowing the jitter span (0.5 → 0.35) changed nothing.

### 9.3 Rough walls

Patterns 17 (lime render), 18 (brick), 19 (ashlar), 22 (rubble) and 20 (interior finishes) gained relief in metres:
- **Rubble (22):** stones pillowed 2–4 cm with rounded 14 cm arrises, tilted ±1.5 cm, knobbly faces, mortar recessed 1.5 cm, grime in the joints, streaks.
- **Render (17):** bulges ±1.2 cm following the rubble behind, trowel lumps, repair patches 3 mm proud with a lip and a cleaner tone, spalls showing pillowed rubble behind the render edge, cracks 3 mm deep, grime in the hollows, run-off streaks.
- **Ashlar (19):** blocks pillowed 6 mm and tilted ±3 mm, arrises worn into 5 mm recessed joints, chips, streaks.
- **Brick (18):** mortar recessed 6 mm, faces pillowed 2 mm with rounded arrises, chips, streaks and soot.
- **Interior (20):** plaster bulges ±6 mm and replastered patches (papered walls follow them; wainscot barely does).

`shade()` (kCommon) has a relief ambient term for these patterns: the material sets `gNg` (geometric normal) and `gCavity`.
- The relief's *deviation* from the geometric normal gathers more sky toward the sun's azimuth and overhead, less away from it, so mortar, pillows and bulges stay legible on walls in shadow. Flat walls are unchanged.
- Recesses see less sky (`gCavity`), on the indirect light and the environment specular.
- Other materials, and the legacy look, are untouched.

### 9.4 Water

`src/water.cpp/.h`, assets from `tools/catalog_water.mjs` (registered in `tools/build_mpu_assets.mjs`).

**Asset block** (asset-local):
```jsonc
"water": { "surfaces": [ { "outline": [[x, z], ...], "level": -0.35, "kind": "pond|stream|puddle|crater|sea",
                           "path": [[x, z], ...], "speed": 0.55, "clarity": 1, "waves": 1, "tint": [r, g, b] } ] }
```
- The bed is a `terrainCut` (flat floor, linear banks). The surface outline reaches a little under the banks: the ground hides its edge and the shore fade blends the waterline.
- Each surface also has a 1 cm glass slab of its outline (`water_proxy`) for bounds and the thumbnail. The world draw skips it (`waterDrawItems`, the `App::buildDrawLists` hook).
- Water assets get no foundation soil band (`App::updateSurface` hook).

**Assets** (category Water):

| Id | What |
|---|---|
| `wat_pond_round` | 13 m farm pond, 1.3 m deep, reeds, stones |
| `wat_pond_irregular` | 15 × 10 m kidney-shaped pond, 1.1 m deep |
| `wat_crater_pool` | 9 m shell crater with an earth rim, 0.7 m of muddy water |
| `wat_puddles` | five puddles 1.2–2.8 m in shallow dips |
| `wat_stream_straight` | 8 m of brook, sockets at both ends |
| `wat_stream_bend` | 90° bend on a 6 m radius, sockets at both ends |
| `wat_stream_end` | spring pool + 4 m, one socket |
| `env_sea_shallows` | ported: same 50 × 50 m sheet, now a `sea` surface |

- Stream pieces carry V6 sockets `"snap": [{p, dir, kind: "stream"}]`. Their channel cut runs on 1.6 m past each socket, so joined pieces share one continuous floor; the water surfaces meet exactly at the sockets with flow along the centreline.
- Navigation: ponds, crater and streams mark their water as nav area `water`; puddles affect nothing.

**Pass** (`waterRenderPass`, called from `render()` after grass and nature patches, before gore decals and glass):
1. Copy the lit colour (into `sceneCopy`, free until the world composite) and the depth.
2. Draw every surface with the depth test and depth write on, both faces, into colour, indirect (0: no SSAO on water), normal + roughness (the world composite adds **SSR** when it is on) and motion (0: TAA reprojects from depth).

**Shading** (`kWaterFS`):
- **Normals:** five directional wave trains with deep-water dispersion, plus value-noise ripples. Streams advect theirs along the flow (two phases cross-faded); still water drifts with a light wind. Components nearing two pixels fade out and their slope variance goes into the roughness, so there is no sparkle at range.
- **Optics:** Schlick Fresnel; sky reflection scaled by the sky-occlusion map; GGX sun glint in the sun and cloud shadow; refraction of the bed (rejected where the offset lands on something in front).
- **Volume:** per-channel absorption over the path length through the water, from the depth copy (red goes first), and in-scattering of the water colour lit by sky and sun. Per kind: sea blue-green, pond peaty green, brook tea-clear, puddle and crater muddy.
- **Shore:** within 1–8 cm of depth the surface fades into the bed; a little foam at sea and brook edges.
- **Fog** is applied once: the bed copy already carries it.
- `waterSurfaceAt(app, x, z)` gives the water height from the placed water assets (tests, gameplay).

### 9.5 Results

`--selftest-render`, 1920×1080, TAA on, Blank Slate, the same deterministic camera path in both builds. **Before** = master 0e8479f with only the new suite added; **after** = this branch. Each value is the shimmer index: the mean per-pixel temporal standard deviation of luma over 24 still frames, with the facade region's mean scaled to 100. The strafe flicker is the mean |second temporal difference| over 40 frames of a slow sideways walk; it varies about ±1 % between runs, while the still index repeats exactly. Lower is better.

| View | Farmhouse before → after | Ruined church before → after | Cottage (old house) before → after |
|---|---|---|---|
| still, 20 m | 1.73 → **1.63** | 0.94 → 0.96 | 1.46 → **1.38** |
| still, 50 m | 2.00 → **1.85** | 1.06 → 1.08 | 1.79 → **1.71** |
| still, 100 m | 1.52 → **1.27** | 1.24 → **0.60** | 1.22 → **0.90** |
| strafe, 50 m | 3.37 → **3.00** | | |
| strafe, 100 m | 1.90 → **1.51** | | |

This is with the new, much stronger wall relief on. The suite's A/B variants (report only) separate the two effects:

- **Shimmer fixes alone** (relief off with `Renderer::wallRelief = 0`, same build): farmhouse 1.16 at 20 m and 1.43 at 50 m; ruined church 0.37 and 0.54. That is 33–61 % below master.
- **The rough walls** add back what pixel-scale relief costs under TAA jitter. Two measures keep that under master's figures:
  - joint grooves fade by ~20 m;
  - per-stone relief is continuous across joints and fades beyond ~70 m.
- **First attempts** made the walls rougher but shimmer more: 20 m farmhouse 2.0–2.5. A groove or tilt step one or two pixels wide is the worst case.
- **TAA:** sharpen 0.40 → 0.30 is worth ~9 % at 20 m (0.40: 1.79, 0.30: 1.63).

Other checks:
- `--selftest-render` 48/0, `visuals` 31/0, `env` 42/0, `interiors` 141/0.
- `maps` 139/0. The FPS gate holds: Omaha worst eye-level view 15.40 ms (master 15.33). Battle 1 is reported only: 17.5 ms worst (master 18.6). Its `obj_B_eye` camera is inside a wall on master too.
- The water pass costs a copy of colour and depth, plus the surfaces' fragments. In the water views of the suite the whole frame is 2.8–4.1 ms GPU.

Before / after crops, the shadowed-wall close-ups, two water views and both runs' `render_shimmer.json` are in `docs/rendering/v1/`. All screenshots, in `selftest_out/` of the build:
- `render_facade_{20,50,100}m.png` and `_crop.png`;
- `render_moving_{50,100}m_*.png`;
- `render_walls_close_{sun,shadow}.png`, `render_walls_ruin_close.png`;
- `render_water_{pond,pond_irregular,crater,puddles,stream,overview,sea}.png`.

Run the same suite on master for the before frames (the suite builds there unchanged); the numbers are in `render_shimmer.json`.

### 9.6 Limits

- **Older houses keep 5 mm shutters.** The houses from `tools/build_assets.mjs` / `catalog_small` / `catalog_large` share `parts.mjs` `window()`, but they keep their 5 mm shutters until that generator is re-run (the plan forbids running it wholesale). Reverse-Z and the filtering cover them meanwhile; the cottage still improves 6–26 %.
- **The ruined church at 20–50 m is level with master, not better.** Its walls carry the most relief.
- **Water is not physical.** The surfaces have no collision and no physical wading:
  - footstep sounds and the navmesh water rule still use the template `waterLevel`; the water assets mark nav area `water` on their polygons;
  - bullets and grenades pass through the water surface;
  - there are no splashes for rounds landing in a pond.
- **Water reflections are screen-space.** Reflections of objects off screen fall back to the sky; there are no planar reflections.
- **No underwater rendering** beyond a flat tint when the eye is below a surface.
- **Stream pieces snap via V6.** Until V6 lands, they join only when placed by hand on the sockets. The overlap check refuses a bend next to a straight, because the footprints overlap; V6 allows snapped neighbours.
- **Terrain and grass chunks have no far-plane cull.** Their GL-style plane tests are conservative under reverse-Z. Item, foliage and model culling (§10) does have one.
- **`build_mpu_assets.mjs --check`** reports `gun_flak18.json`. That drift is already on master and not V1's.

<a id="frustum-culling"></a>
## 10. Frustum culling and draw cost (L1)

Plan: `docs/plan-lighting-textures.md` (L1). Before L1, every opaque item, tree card and model part was submitted to every pass: the main view, both shadow cascades and sky occlusion. Only the terrain and grass chunk streams were culled.

### 10.1 API (`src/frustum.h`)

| Call | Use |
|---|---|
| `Frustum::fromVP(vp)` | Planes from a view-projection in the standard GL clip convention, extracted in double and normalized to metres. A degenerate plane (infinite far) is dropped. `n == 0` means everything is visible. |
| `Frustum::fromProjView(proj, view)` | The same, multiplying in double. Use it for a camera whose far plane is huge (the editor's 1e6 m). |
| `visible(lo, hi, margin)` | World-space AABB, centre-extent test. |
| `visible(lo, hi, model, margin)` | Object-space box under an affine model matrix. The planes go into object space (`p * model`), so a rotated wall is tested as its oriented box. `margin` is in world metres. |
| `sphere(c, r)` | Sphere test. |

The main camera's frustum must come from the standard-Z projection, before `reverseZ()`. In the reverse-Z matrix the far plane is the w row alone, so it would be lost. The terrain chunk stream (`terrain_chunks.cpp`) uses the same header; its old static `frustumPlanes` / `boxVisible` are gone, and its behaviour is unchanged.

### 10.2 Bounds

- **`GpuMesh::bmin/bmax`** are computed in `GpuMesh::upload` over the indexed vertices, in mesh space. Every `DrawItem` source gets them for free: assets, panels, door leaves (`interior.cpp`), interior and wall fragments (`ballistics.cpp`, `wallfield.cpp`), debris, batches, paths and nature. An empty mesh has `0, 0`.
- **`TreeDef::cardMin/cardMax`** cover a tree's leaf-card buffer (`foliage.cpp`).
- **Model parts** use their import bounds, `ModelPart::bmin/bmax`, which are in vertex space (`model.cpp`). Part animations go through the instance matrix, so they're covered.

### 10.3 Which pass culls against what

| Pass | Frustum | What is culled |
|---|---|---|
| near cascade | `lightVP` | items, tree cards, model parts |
| far cascade | `lightVPFar` | items, tree cards, model parts |
| sky occlusion | `occVP` | items, tree cards, model parts |
| main view | `v0.proj * view` (standard Z, unjittered) | opaque items, models, tree cards, glass, ghosts |

Each cascade tests its own ortho box. A caster behind the camera but inside the cascade still casts.

The TAA jitter is sub-pixel, so the main pass uses the unjittered projection. The 0.01 m margin covers it.

These paths stay unculled:
- thumbnails (`thumbs.cpp`) and `renderModelSheet`;
- the first-person weapon;
- skinned soldier parts. The skinning compute pass rewrites their vertex buffer each frame, so their part bounds are degenerate and `partCulled` always draws them.

### 10.4 Margins

| Draw | Margin | Covers |
|---|---|---|
| box-kit items, glass, ghosts | 0.01 m | rounding only, no vertex displacement |
| tree cards | 0.14 x wind + 0.01 m | `kLeafVS` sway: |offset| <= 0.10 x 1.3 x |(0.8, 0.25, 0.6)| x `uWind` |
| model parts | 0.05 m | thin-wire widening (<= 3 cm) and small part animations |

### 10.5 Draw-loop cost

- **Uniform locations are cached once per program.** `Renderer::drawLocs(prog)` returns a `DrawLocs` by value, because the cache may grow while a loop holds a copy. `compile()` clears the cache, since a new program can reuse a deleted program's id. The draw loops make no `glGetUniformLocation` calls.
- **The main opaque pass is stable-sorted by `SurfaceParams*`.** The building finish block (`uInst`, rooms, sills) then uploads once per building, not once per run of items. Because the sort is stable, the order inside a building is unchanged, coplanar ties resolve as before, and nothing depends on the camera.
- **The VAO is bound only when it changes,** and the loop calls `glDrawElements` directly.
- **Glass is culled first and then sorted far to near,** as before. The sort only sees what is drawn.
- **`FrameStats::culled`** counts the items, cards and parts skipped across all passes. `stats.draws` still counts the draws that were made.

### 10.6 Results

**Identity.** `--selftest-render` passes 48/0. `render_shimmer.json` matches the pre-L1 build to within 1e-5 on every metric; the only differences are float summation order in the 20 m cottage and farmhouse views. `visuals` passes 31/0 and `fps` 114/0.

**Draw counts: village bench** (`--benchmark-village`, 1080p, Medium, Day; draws are deterministic):

| Camera | Draws before | Draws L1 | Triangles before | Triangles L1 |
|---|---|---|---|---|
| overview | 1934 | 1492 | 2419 k | 2149 k |
| road | 1954 | 974 | 2473 k | 2068 k |
| area | 1937 | 1776 | 2583 k | 2405 k |
| foliage | 1950 | 726 | 2452 k | 1979 k |
| building | 2198 | 912 | 3087 k | 2177 k |
| lane_north | 1936 | 603 | 2427 k | 1626 k |
| into_sun | 2010 | 1575 | 2567 k | 2182 k |
| **mean** | **1988** | **1151 (−42 %)** | **2573 k** | **2084 k (−19 %)** |

`stats.draws` counts the draws of all four passes. The triangles drop less than the draws, because the terrain and grass chunks were already culled and are most of the triangle budget.

**Draw counts: `--selftest-maps`, Medium** (from `summary_maps.json`, same session):

| Map / view | Before | L1 |
|---|---|---|
| battle1 A1_spawn | 5638 | 736 |
| battle1 obj_A_eye | 6294 | 4473 |
| battle1 obj_B_eye | 7076 | 4400 |
| battle1 obj_D_eye | 6073 | 1820 |
| battle1 overview | 4573 | 3624 |
| omaha_easy_red A1_spawn | 3033 | 1287 |
| omaha_easy_red obj_A_eye | 6809 | 1189 |
| omaha_easy_red obj_A_eye@high | 13387 | 2990 |
| omaha_easy_red obj_D | 7762 | 2261 |
| omaha_easy_red bluff_top_to_beach | 5241 | 2226 |
| village_assault preview | 5451 | 3721 |
| village_assault explore_start | 7033 | 3147 |
| tank_range explore_start | 175 | 108 |
| normandy_forest (all views) | unchanged | unchanged (17-75: terrain only) |

**Frame time.** This could not be measured cleanly. `gpu_turn.sh` lets five GPU jobs run at once, and the other sessions kept the GPU busy throughout. Across five interleaved village A/B pairs, one camera's GPU p50 ranged from 4.6 to 92 ms on the same exe, and normandy_forest, whose draws don't change, measured 7 ms in one `maps` run and 50 ms in the next.

The cleanest data point is the first pair, run back to back while the GPU was nearly idle:
- `into_sun` measured 5.70 ms before and 5.60 ms with L1;
- `lane_north` measured 5.10 ms before and 5.25 ms with L1 (raw data: `docs/rendering/benchmark_village_bench_l1_{before,after}.json`).

At 1080p the village frame is bound by fill and shading, not by submission, so L1 buys CPU time and pass headroom rather than GPU milliseconds.

In `maps`, the early A/B views show the same pattern:
- battle1 A1_spawn went from 9.83 ms to 7.08 ms;
- Omaha's eye-level views stayed level at about 20-23 ms.

Omaha's 60 FPS gate fails in both builds, at 22.3 ms before and 23.1 ms after, because its cost is the terrain and sea. The village and forest gate failures in the L1 run were contention: the forest's draws are identical. A clean ms comparison needs an idle GPU, and the coordinator's full run at merge is that chance.
