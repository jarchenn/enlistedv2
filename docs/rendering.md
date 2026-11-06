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
| `depthtex0/1/2` (with / without translucents, hand) | `composite*.glsl` | One D32F world depth (`depthTex`, standard 0..1, near 0.03, far 4000). Glass and ghosts don't write depth. The weapon is drawn after a depth clear with its own projection, so the world post-passes run **before** it and never see its depth. |
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
2. World HDR pass (jittered when TAA is on): sky; then terrain, opaque, world models, foliage and grass. The last group writes attachments 0 / 2 / 3 / 4.
3. Transparent glass and placement ghosts (attachment 0 only).
4. **World composite** (`worldComposite`): SSAO (half res), light shafts (half res), then one composite. It applies SSR, removes the occluded share of indirect light, and adds the shafts.
5. **TAA resolve** (`taaResolve`), world only.
6. Effects (smoke, flashes, tracers), unjittered.
7. First-person weapon and arms: depth clear, own projection, unjittered.
8. Bloom chain → exposure → tonemap → LDR → editor overlays → blit → ImGui / HUD.

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
- **Bloom.** A six-level downsample chain (first level luminance-weighted against fireflies) with a tent upsample, normalised and blended at 4.5 % (energy-preserving mix, no threshold). The muzzle flash glows softly; the sky doesn't streak.
- **Leaf alpha.** Distance-aware: leaves grow by `fwidth(uv)` once a card covers few pixels, so distant canopies don't break into crawling single-pixel sparkles. TAA then resolves the remaining sub-pixel coverage.

**Stage 4: reflections and TAA.** These were added only after the buffers they need existed: normals, roughness, motion and history.
- **SSR.** Only for roughness < 0.4: puddles, wet ruts, polished metal.
  - It marches the view-space reflected ray (28 growing steps, dithered) against world depth with a thickness test and 5-step bisection.
  - On a hit it replaces the sky estimate already in the lit colour, weighted by Schlick Fresnel, roughness, screen edges and ray length. A miss keeps the environment and sky reflection.
  - Hazed-out surfaces are excluded.
- **TAA.** Halton (2, 3) 8-phase jitter at ±0.25 px, world projection only.
  - **Reprojection.** Static geometry reprojects from depth with the *unjittered* current and previous matrices. Wind-animated leaves and grass write their own motion vectors: each re-evaluates last frame's sway and projects it with last frame's matrix.
  - **History.** Catmull-Rom history fetch, then YCoCg 3×3 variance clipping (±1.1σ) in a luminance-compressed space. The current-frame weight is 0.08, rising to 0.30 with motion.
  - **Sharpening.** An unsharp mask (0.40) on the displayed image only; it never enters the history.
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
- **Near/far depth.** All reconstruction uses the exact projection that wrote the depth; sky pixels (depth 1) are skipped (SSAO, SSR) or reprojected from the far plane (TAA).
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

- **No coloured or translucent shadows.** The scene has no stained glass and no water.
- **No block light, night or weather cycle.** The game has one daylight setting.
- **SSR is screen-space only.** Reflections of off-screen objects fall back to the sky. Glass windows keep their analytic sky reflection, since they're forward-blended after the composite.
- **No velocity for rigid moving bodies** (debris, felled trunks, swinging doors). They rely on TAA clipping, which can leave a faint trail on a fast-moving door.
- **Grass, terrain materials and tree geometry are unchanged by this pass.** Its only foliage changes are lighting and alpha stability. Asset work is still needed, and the shader work doesn't replace it.
