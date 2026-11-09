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

<a id="gpu-budget"></a>
## 11. GPU budget and frame cost (PF2)

### 11.1 Per-pass GPU timers

`Renderer::gpuMark(GpuPass)` writes a `GL_TIMESTAMP` (`glQueryCounter`) where each pass starts; the app writes the end mark after ImGui. `gpuPassMs[GP_Count]` holds the last resolved frame.

- **No stall.** Three slots are in flight, and a slot is read only when every result in it is available, so the CPU never waits. Results are 2-3 frames old.
- **Order independent.** A pass lasts from its mark to the next mark in GPU time, so passes that move (the sky drawn last) are still timed right. A pass that did not run reads 0.
- **Passes:** `cloud_shadow, shadow_near, shadow_far, sky_occlusion, light_shadows, probes, clusters, clears, sky, terrain, opaque, models, trees, grass, nature, msaa_resolve, water, gore, glass, composite, taa, bloom, fx, weapon, post, ui`. There is no depth prepass in this renderer. `post` is exposure, tonemap, editor overlays and the blit.
- **What a span means.** A span is GPU wall time, so it includes time the GPU waits for the CPU to submit. Where the CPU loop is slow (the probe capture loop, grass instancing), the span is mostly submission time, not shading.
- **`stats.gpuMs`** is now the `render()` span from the same timestamps (read late, no stall). Before PF2 it came from a `GL_TIME_ELAPSED` query read back with `GL_QUERY_RESULT` at the start of the next frame. That read **blocked the CPU until the GPU had finished the previous frame**, so the GPU then sat idle while the CPU built the next one. The old path is kept as the fallback when timestamps are missing, and as `--rq-off asyncq`.
- **Mac.** The calls are `glQueryCounter`, `glGetQueryObjectiv` (`GL_QUERY_RESULT_AVAILABLE`) and `glGetQueryObjectui64v`, the same ones `decals.cpp` already uses. When `glQueryCounter` is missing, `gpuTimers` turns off and everything reads 0.

Reports:
- `--selftest-render`: a "gpu budget" step, with per-pass ms and the probe-capture CPU time at Medium and High on one fixed elevated view over the facades and water (`render_shimmer.json` `gpu_budget`). Reported only, never gated.
- `--selftest-maps`: every view in `summary_maps.json` has `gpuMs` and `gpuPassMs`.

Measurement environment variables for `--selftest-maps`:
- `NMK_MAPS=battle1,omaha_easy_red`: only these templates.
- `NMK_PERF_AB=all | <switch>`: an **interleaved A/B**. Each view measures 2-6 s in ten 0.4 s blocks, alternating the baseline (all: the pre-PF2 renderer, `Renderer::Perf::legacy()`; else that switch off) and PF2. The first 5 frames of each block are skipped. `views[].ab` holds frames, frame ms, pass ms and the probe CPU ms for both sides. Other GPU load (other sessions' runs, a game the user is playing) hits both sides alike. This is the only frame-time comparison that held up on the shared GPU: back-to-back runs varied by a factor of 2-4.

### 11.2 Switches (`Renderer::perf`)

Each switch can be turned off with `--rq-off <name>` and on with `--rq-on <name>`; both name the same switches as `NMK_PERF_AB`.

| Switch | Default | What it does |
|---|---|---|
| `asyncq` | on | The frame GPU time comes from the timestamps. Off: the blocking `GL_TIME_ELAPSED` read-back. |
| `farcache` | on | The far cascade is redrawn every 2nd frame, or at once when its focus moves more than 8 m or its radius or the key light changes. In between, the shaders sample the last map with the matrix it was drawn with, so static shadows are exact and moving casters lag at most one frame (a tank at 10 m/s: 0.17 m, under a far texel). |
| `occcache` | on | The sky-occlusion map (±90 m, 2048²) is redrawn every 16 frames, or when the camera moves 4 m across or 8 m up or down. Thumbnails always redraw. |
| `smallcasters` | on | The far cascade skips casters whose world diagonal is under 2 texels (Medium 0.47 m, High 0.23 m). Sky occlusion skips those under max(0.3 m, 2 texels). The near cascade and light shadows are unchanged. |
| `probecull` | on | A GI probe capture skips items narrower than one capture texel as seen from that probe (90° / 16 = 0.098 rad; a probe inside the item's box never skips it). Such items add next to nothing to an L1 SH but cost six layers of geometry. |
| `leafprepass` | on | Canopies are drawn depth-only first (the `leaf_shadow` program, alpha test), then shaded once per pixel at `GL_EQUAL` with depth writes off. `kLeafVS` declares `invariant gl_Position` so both passes produce the same depth. |
| `skylast` | on | The sky is drawn after the opaque world. Its triangle now sits on the reverse-Z far plane (`kSkyVS`, depth 0), so `GL_GEQUAL` passes only where nothing was drawn, and early-Z skips the cloud shader under terrain and buildings. Thumbnails (standard Z, sky first) are unchanged. |
| `terrainlast` | on | The terrain is drawn after solids, models and canopies (before grass, which is alpha-tested over it), so the ground they hide is never shaded. In principle this changes which side of an exact coplanar tie wins. In the maps suite every frame was byte-identical with it on and off (battle1, village_assault, Omaha, all presets; the suites run on a fixed time step, so frames are deterministic). |
| `proberefresh` | on | The full probe budget (8 per frame at High, 3 at Medium) is spent only while probes wait for their first capture (the camera moved, rooms changed, or time of day jumped by more than 0.1 h, which now re-captures everything). Once the volumes are converged, 2 (High) or 1 (Medium) re-captures per frame keep them fresh, oldest and nearest first. Tests that set `lt.probeBudget` keep their rate. |
| `probecapture` | on | Measurement only: off skips every probe capture (GI frozen). Its A/B gives the true cost of live capture. |

### 11.3 Results

**Frame time, interleaved A/B, before → after.** Before is the pre-PF2 renderer, `Perf::legacy()`; after is the shipped switches. `NMK_PERF_AB=all`, exclusive GPU turn, 1920x1080, RTX 2080 Ti, ms, mean of each map's eye-level (gameplay) views:

| Map | Low | Medium | High | Night (Medium) |
|---|---|---|---|---|
| battle1 (5 views) | 13.3 → **6.5** (−51 %) | 19.1 → **10.8** (−44 %) | 25.1 → **15.4** (−39 %) | 20.3 → **11.8** (−42 %) |
| village_assault (1) | 12.0 → **4.9** (−59 %) | 15.3 → **8.1** (−47 %) | 21.2 → **12.3** (−42 %) | 17.6 → **9.7** (−45 %) |
| normandy_forest (1) | 5.7 → **4.0** (−30 %) | 8.1 → **6.3** (−23 %) | 10.3 → **9.2** (−11 %) | 8.9 → **6.9** (−23 %) |
| omaha_easy_red (8) | −17 % | −12 % | −30 % | −22 % |

Worst eye-level view at Medium: battle1 28.8 → 17.8 ms, village_assault 15.3 → 8.1 ms. Elevated editor views at Medium: battle1 −40 %, village_assault −39 %, forest −20 %, Omaha −13 %.

**Omaha is given as percentages only.** During that part of the run another process loaded the GPU heavily: both sides measured 30-85 ms, and fixed-cost passes like the tonemap ran up to 20x slower. The paired percentages still hold. An earlier, cleaner A/B (without `terrainlast` and `proberefresh`) measured Omaha −19 to −22 %. Terrain-last on its own was neutral on Omaha (±3 %).

**Contributions, from single-switch A/Bs:**
- `terrainlast`: battle1 −19 to −21 % and village_assault −12 to −18 % at every preset. It was measured on top of the other switches, in an exclusive run with less load.
- `proberefresh` at High, converged view: −0.6 ms (battle1) and −1.1 ms (village_assault). See §11.4.
- The other switches are measured together in the first A/B: −7 to −36 % across the four maps and presets, under heavier outside load.

`normandy_forest` is the biome/forest stand-in, since batch2 has no biome engine. Its views are terrain and grass only, so only `asyncq`, `skylast` and the shadow caches apply.

**Where the time went** (final A/B pass spans, baseline → PF2, battle1 Medium unless noted):
- `cloud_shadow` 5.2 → **0.03 ms** on every map (`asyncq`). This span starts right after the old blocking read-back, so it was GPU idle time while the CPU built the frame.
- `terrain` 5.4 → **2.7 ms**; village_assault High 6.8 → 3.8 ms (`terrainlast`: ground hidden by buildings, props and canopies is no longer shaded).
- `trees` 0.70 → **0.31 ms**; Omaha High 6.4 → 2.9 ms (`leafprepass`).
- `sky_occlusion` 0.36 → **0.02 ms** (`occcache`).
- `shadow_far` 0.41 → **0.21 ms** (`farcache`, `smallcasters`).
- `sky` 0.30 → **0.16 ms** (`skylast`).
- `probes` at High, converged view: 1.6-1.9 → **0.6 ms** (`proberefresh`).

Some spans grew a little on the PF2 side (`grass`, `opaque`). The GPU no longer starts each frame idle, so time it spends waiting for the CPU lands in the pass after a slow CPU section. Only the frame time is a clean sum.

**Visual check.** The suites run on a fixed time step, so their frames are deterministic. The same build was run twice: with every PF2 switch on, and with every switch off (`--rq-off` for each switch, which is the pre-PF2 renderer). The frames were then diffed.
- **`--selftest-maps`** (battle1, village_assault, omaha_easy_red, normandy_forest; every view at Medium, Low, High and Night; 79 frames). 78 frames differ by at most 0.02/255 on average. The other, battle1 A1_spawn at Night, differs by 0.23/255 on average, with 0.45 % of pixels over 24. Only the first-person weapon and arms differ there: they are lit by the probe at the player. After the jump to Night, PF2 re-captures every probe at once, while the old path waited for the age-based refresh.
- **`--selftest-render`**. Every frame (facades, kit, walls, water) is within 0.005/255. The 156 shimmer metrics agree to about 1e-4 relative, which is float summation order.
- `terrainlast` on its own was byte-identical in every maps frame.

Files in `docs/rendering/pf2/`:
- Raw A/B data: `summary_maps_ab_{final,probecapture,proberefresh_settled,terrainlast}.json`.
- Render metrics for both renderers: `render_shimmer_{pf2,legacy}.json`.
- Frame pairs (pre-PF2 / PF2, same deterministic frame): `{before,after}_battle1_obj_A_eye.png` and `{before,after}_village_explore_start_high.png`.

### 11.4 Saved lighting (probe GI): decision

**Measured cost of live capture.** An interleaved A/B with capture frozen (`NMK_PERF_AB=probecapture`; exclusive run, eye-level means) gave the frame-time difference:

| Map | High | Medium |
|---|---|---|
| battle1 | +1.09 ms (worst view +1.96) | +0.61 ms |
| village_assault | +1.35 ms | +0.75 ms |
| omaha_easy_red | +0.12 ms | +0.81 ms |

The A/B noise is about ±0.5 ms: Omaha at Low captures nothing and still read +0.56. The probe span was 1.2-1.5 ms at High, of which 0.2-0.4 ms is CPU (`stats.probeCpuMs`). The rest is the layered capture's geometry: every item within range is drawn into up to 48 cube-face layers. `probecull` barely changed this, because the cost is in the large nearby buildings, not in small parts.

So live capture cost about **1.1-1.4 ms per frame at High** on the building-heavy maps, just over the 1 ms threshold.

**What was done: `proberefresh`.** Before PF2 the full budget (8 per frame at High) was spent every frame, even once every probe in the volumes had been captured. Now the full budget is used only while probes wait for their first capture. Once converged, 2 per frame (High) or 1 (Medium) keep the volumes fresh.

A/B on a converged view (`NMK_PERF_AB=proberefresh NMK_SETTLE=14`; the view is held 14 s first, since full convergence takes about 450 frames at High):

| Map, High | Captures / frame | Probe span | Frame |
|---|---|---|---|
| battle1 | 8 → 2 | 1.55 → **0.58 ms** | 16.47 → 15.83 ms |
| village_assault | 8 → 2 | 1.91 → **0.64 ms** | 13.69 → 12.61 ms |

At Medium the captures go 3 → 1 and the span 0.69-0.85 → 0.52-0.55 ms.

**Decision: no `.gi` sidecar for now.** The steady-state capture cost at High is now about 0.6 ms, under the 1 ms threshold. The remaining cost is when the camera moves into cells the volumes have not captured, and only a saved set would remove that. A saved set is a large feature:
- The volumes are camera-relative. Near: 16x8x16 probes at 1.5 m; far: 16x6x16 at 6 m. Both scroll with the camera, so a saved set is keyed by world cell.
- Size: battle1's near cascade over the whole 300 x 400 m map is about 427 k probes x (64 B SH + 256 B visibility), roughly 137 MB.
- It needs a GPU-side probe cache (copy out on scroll, copy in on re-entry), a bake path, invalidation hooks for breaches and fires, and a key of map content and time of day.

It is worth building only if moving-camera captures show up in play.

Changes to probe behaviour:
- A time-of-day jump of more than 0.1 h now re-captures every probe at the full budget. Before, they refreshed at the age-based rate.
- A probe near a change that does not move rooms (a breached wall, a new fire) is re-captured by age. That takes up to about 30 s at High (2 per frame over about 3600 probes), against about 7.5 s before; nearer and older probes go first.

### 11.5 Not done

- **LOD and merged static batches for B1/B2/B3 parts.** The main opaque pass is not the frame's limit on these maps: battle1 Medium `opaque` is 4.1 ms after PF2 against 3.8 ms before, and the span includes CPU submission waits. The frame was limited by the GPU idling, terrain, and the shadow, probe and sky passes above. Merging per chunk would cut CPU submission, which is PF3's area (draw lists).
- **Interior occlusion, half-resolution effects, cheaper TAA / MSAA.**
  - Bloom is already a half-resolution chain.
  - SSAO is already half resolution on Medium.
  - `composite`, `taa` and `msaa_resolve` are each 0.15-0.5 ms in the exclusive baseline.
- **Terrain shading** is still the largest real-shading pass: 2.7-4.7 ms at Medium after PF2, and 6.8 ms on the `--selftest-render` budget view, which is mostly ground. Most of it is the shared `shade()` (two-cascade PCF, probe GI, clustered lights) plus the fBm layers of `kTerrainFS`. Trimming either changes the look, so it is the next candidate, not done here.
