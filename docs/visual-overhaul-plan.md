# Visual overhaul plan (VO)

Date: 2026-11-20. Goal: substantially better, realistic lighting and surfaces at a frame cost that is the same or
lower. Written from two inputs: an audit of the renderer as it is today and a literature review with checked
sources. Costs marked "est." are estimates to be replaced by measurements on the RTX 2080 Ti.

## 1. Where the game is today (audit)

- **Shadows:** two sun cascades (near 32 m at Medium), Vogel PCF, no contact shadows, and every shadow is lightened by
  30 % (`mix(1, s, 0.7)`), which is why objects look unanchored. Grass casts none.
- **Ambient light:** a sky / ground hemisphere times a top-down sky-occlusion map, plus a constant "fill". Probe GI
  (1.5 m grid, 16² captures) only takes over under cover. Ambient occlusion is depth-only, 8 samples, quarter
  resolution on Medium.
- **Reflections:** no reflection probes. Shiny surfaces reflect an analytic sky gradient; SSR only below roughness 0.4.
- **Sky and fog:** gradient sky with an fbm cloud deck, exponential height haze, a 2D ground-mist field. Sky, fog and
  ambient colours are set separately and do not agree.
- **Exposure and tone:** mean-luminance auto exposure, a filmic curve, a fixed grade, bloom with a small strength.
- **Materials:** 554 tiling texture slots, no de-tiling, no specular anti-aliasing, wear from one tiling grunge mask.
  Grunge is baked into 323 imported models; the generated buildings get wear only in the shader.
- **Terrain:** 3.14 m cells with per-vertex normals, so facets show. Maps are built nearly flat: Rue Haute has no
  relief at all, Chateau town 0.9 m, every building sits on a flattened pad, ruts are paint. No erosion.
- **Vegetation:** alpha test with no coverage preservation (thin, shimmering foliage at distance), no translucency on
  model foliage, no impostors, one LOD level.
- **Assets:** 22,288 placed objects are engine-generated against 14,443 imported models. Vegetation is the largest
  generated group (10,225, mostly bramble and hazel shrubs, dry grass, flowers, weeds). All 1,528 placed buildings are
  generated from box parts; 20 are imported.
- **Cost:** Chateau town at Medium is about 15.6 ms GPU, High 32.6 ms. Terrain is the largest shading pass
  (2.7–4.7 ms). High pays for 4x MSAA on top of TAA. Texture memory rose to about 2.3 GB with the 2K sets.

## 2. Targets

| Preset | 1440p GPU, 95th percentile, RTX 2080 Ti | Notes |
|---|---|---|
| Low | 7 ms | Analytic fog and sky tint, no probes, 2 cascades |
| Medium | 11 ms | The default look: everything in section 3 at reduced sample counts |
| High | 16.7 ms | Full set; replaces today's 32 ms High |

Rules for every item:
- **Measured, not assumed.** Before / after GPU pass times from the fixed benchmark views (section 6), with the card
  held exclusively (`tools/gpu_turn.sh`). An item that costs more than its row allows is cut down or dropped.
- **Switchable.** Each gets a `--rq-off` / `NMK_PERF_AB` name and a tier setting, and the Settings menu gains
  **Lighting: New / Classic** so the old look can be compared in game at any time.
- **Art direction is kept.** Each map's overall brightness, colour and mood stay where they are; the change should
  read as more depth, grounding and material realism, not a different mood. (The user rejected a re-grade of the
  forest map on 2026-11-20.) Per-map look values are re-fitted to match the old captures after each lighting step.

## 3. What gets built

Ranked by value for cost. "Src" is the checked source.

### A. Light and shadow (stream L)
| # | Item | Fixes | Cost (est.) | Src |
|---|---|---|---|---|
| L1 | Screen-space sun contact shadows; remove the 30 % shadow lightening and re-fit exposure | Floating props and grass, soft grounding | +0.3–0.5 ms | Aldridge, Bend Studio, SIGGRAPH 2023 |
| L2 | GTAO with bent normals, multi-bounce and specular occlusion, on indirect light only; replaces SSAO | Grey flat occlusion, glow in crevices | 0.5–0.8 ms, replaces ~0.4 | Jimenez et al. 2016; XeGTAO |
| L3 | Three cascades on Medium, four on High, stable and texel-snapped, far cascade cached; 2 on Low | Blurry mid-range shadow, crawl | neutral | MJP "A Sampling of Shadow Techniques"; Lauritzen 2011 |
| L4 | Micro-shadowing from material AO; two-sided foliage translucency | Flat texture detail at the terminator; black backlit leaves | ~0 | Brinck et al. 2016; Barré-Brisebois 2011 |
| L5 | Sky atmosphere LUTs with aerial perspective; sun colour, sky SH, fog in-scatter and probe sky all derived from them | Sky, haze and ambient disagreeing; flat distance | < 0.5 ms | Hillaire, EGSR 2020 |
| L6 | Probes store sky visibility (SH) and are relit each frame from the sky SH; recomputed near a broken wall | Flat or glowing interiors, time-of-day lag | ~0.1 ms | Patry, SIGGRAPH 2021 |
| L7 | Runtime-captured, parallax-corrected reflection probes with split-sum IBL; SSR falls back to them | Wrong reflections, SSR holes, dead metals and glass | 0.2–0.5 ms amortised | Lagarde & Zanuttini 2012; Karis 2013 |
| L8 | Histogram auto exposure (centre-weighted, clamped range); AgX-style tone curve with the grade as a LUT; threshold-free bloom | Blown desert and snow, murky forest, flickery glow | ~0.4 ms | Lagarde & de Rousiers 2014; Jimenez 2014 |
| L9 | Froxel volumetric fog on Medium / High in place of screen-space shafts; analytic fog tinted from L5 on Low | Shafts that break off-screen, unlit fog | 0.5–1 ms, replaces shafts | Wronski 2014; Hillaire 2015 |
| L10 | Local tone mapping by exposure fusion (High) | Crushed interiors against a bright sky | 0.3–0.5 ms | Wronski 2022 |
| L11 | Visibility-bitmask screen-space bounce (High only, if budget remains) | Missing near-field bounce | 1–2 ms | Therrien et al. 2023 |
| L12 | Soft, sorted particles | Hard smoke edges against walls | ~0 | — |

### B. Materials and baked wear (stream M)
| # | Item | Fixes | Cost (est.) | Src |
|---|---|---|---|---|
| M1 | Hex tiling on terrain and ground-paint layers, plus low-frequency macro variation | Visible texture repeats on ground | 0.3–1 ms | Mikkelsen, JCGT 2022 |
| M2 | Height-based blending between ground layers | Mushy paint transitions; mud and snow sitting between stones | ~0 | Mishkinis 2013 |
| M3 | Specular anti-aliasing: geometric (Tokuyoshi & Kaplanyan) and normal-variance roughness written into mips at bake | Sparkle and flicker in motion | ~0 | JCGT 2021; Toksvig 2004 |
| M4 | **Texture rebake.** Every texture set and every imported model's textures re-baked with wear: dirt in cavities (AO), edge wear (curvature), leaks and streaks (position, up-vector), driven by the full grunge library (the thirteen Texturelabs images in use plus the eighteen new ones). Generated buildings get the same wear baked per material family instead of one tiling mask in the shader | Clean "CG" surfaces; saves shader work | 0 at runtime | Substance-style generators, done offline in Blender / numpy |
| M5 | Clustered decals from atlases: graffiti (`graffiti_set001`), torn posters and paste-ups (new images), stains, cracks, bullet pocks; placed by rule per building style, never on WWII maps for modern graffiti | Bare walls and roads | 0.2–0.5 ms | Sousa & Geffroy 2016 |
| M6 | Wetness model: darkening by porosity and gloss only where water really pools | Dry-looking wet ground, or gloss everywhere | ~0 | Lagarde 2013 |
| M7 | Glass: reflection from L7 probes, dirt film from the grunge set | Flat windows | ~0 | — |

Licence rule unchanged: Texturelabs images and anything that is essentially one of them stay local-only, with a CC0
fallback committed. Baked results on other textures may be committed.

### C. Terrain depth (stream T)
| # | Item | Fixes | Src |
|---|---|---|---|
| T1 | Per-pixel terrain normals from a baked normal / height texture at 0.4–0.5 m, and geomorphing between LOD steps | 3 m facets, LOD pops | Far Cry 5 (Moore 2018); CoD (Etienne 2023) |
| T2 | Finer heightfield (fine tiles everywhere within the playable area) where relief is added | Relief that the 3.14 m grid cannot hold | — |
| T3 | Map relief in mapgen: larger, layered amplitudes outside building pads; erosion at bake time (thermal and a simple hydraulic pass); drainage lines to water; real banks, ditches, hollow ways and road camber; ruts and wheel tracks as geometry; terraces with retaining walls instead of flat pads on slopes | Flat ground | Mei et al. 2007 for erosion |
| T4 | Ground meshes that break the plane: model rocks and stones (the user's rock models when they arrive), kerbs, rubble, root flares, banks under hedges | Bare flat plane between buildings | Moore 2018 |
| T5 | Terrain self-occlusion in the sky-visibility map and contact shadows on terrain | Hollows that are as bright as crests | — |

Constraints: routes, spawn timings, navigation and Search and Destroy timings must still pass after each map is
regenerated; relief on lanes stays within walkable slopes; maps that cannot be regenerated (forest outpost) get
relief by patching terrain only.

### D. Vegetation and 3D assets over generated ones (stream A)
| # | Item | Fixes | Src |
|---|---|---|---|
| A1 | Coverage-preserving alpha mips at bake; hashed alpha under TAA on Medium / High | Foliage thinning and shimmer | Castaño; Wyman & McGuire 2017 |
| A2 | Replace generated vegetation instances with the imported packs: bramble and hazel shrubs, dry grass, flowers, weeds, hedgerows, saplings (about 10,000 instances), through alias tables as was done for trees and rocks | Generated look of the most common objects | — |
| A3 | Second LOD and octahedral impostors with dithered cross-fade for trees and large bushes | Tree pop, far draw cost | Brucks 2018 |
| A4 | Greenery colour fitted to the spring-forest reference (in progress as FL1) | Dark, heavy foliage | — |
| A5 | Replace generated props with imported models wherever a licensed model exists; list the generated families that have no model so the user can supply or approve sources | Box-built props | — |
| A6 | Buildings: there are no imported building models to switch to (20 against 1,528). Until models are supplied, generated buildings get M4 wear, M5 decals, deeper trim and L-stream lighting. Building model packs the user sends are imported the same way as the plant packs | Box-built buildings | — |

### E. Performance (stream P)
| # | Item | Saves | Src |
|---|---|---|---|
| P1 | Benchmark harness: fixed views on five maps, per-pass GPU ms, median / p95 / p99, one command, results table committed per merge | — | — |
| P2 | Drop 4x MSAA on High in favour of TAA with tuned clipping and sharpening | est. 3–6 ms on High | Salvi 2016; Karis 2014 |
| P3 | Hierarchical-Z occlusion culling of buildings, props and foliage cells, using last frame's depth with a safety margin | draw and vertex cost in towns | Rákos 2010; nvpro gl_occlusion_culling |
| P4 | Multi-draw-indirect batching for instanced props and model parts where draw order does not matter | CPU draw submission | Haar & Aaltonen 2015 |
| P5 | Depth pre-pass for alpha-tested foliage with depth-EQUAL shading; evaluate a full pre-pass for the terrain pass | overdraw | — |
| P6 | Shadow pass: caster culling by projected size per cascade, impostor casters in the far cascade | shadow ms | — |
| P7 | Dynamic resolution holding the frame target, with the existing temporal upscaler | worst-case spikes | — |
| P8 | Texture memory: BC7 only where it shows, BC1 for opaque albedo, mip streaming by distance so 2K sets are resident only near the camera | about 1 GB | Reed, "Understanding BCn" |
| P9 | Terrain shading cost: fewer taps through the material cache once M1 / M2 land | part of 2.7–4.7 ms | — |

### Not doing, and why
- Full DDGI: needs ray tracing; destructible walls invalidate it.
- FSR 2 / 3 port: no OpenGL backend; the one port measured slow.
- MSAA with alpha-to-coverage: fights the temporal upscaler.
- Variance / moment shadow maps for the sun: light bleeding.
- Variable-rate shading: vendor-specific in OpenGL; dynamic resolution covers it.
- Hardware tessellation and parallax occlusion everywhere: poor value at this map size; POM stays where it is.
- Procedural blade grass: large; the atlas grass stays, with contact shadows from L1.

## 4. Order and ownership

Five streams run in parallel in separate worktrees; the coordinator merges into `integ-dday`.

| Stream | Session | Order inside the stream |
|---|---|---|
| L light and shadow | maps session | L1 → L2 → L3 → L4 → L8 → L5 → L6 → L7 → L9 → L12 → L10 → L11 |
| M materials and rebake | texture session (after the load-crash fix) | M3 → M1 → M2 → M4 (includes the queued grunge images) → M5 → M6 → M7 |
| T terrain depth | desert-town map session | T1 → T5 → T3 on Rue Haute and Chateau town first → T2 → T4 → remaining maps |
| A vegetation and assets | plant session (after the plant-pack import) | A1 → A2 → A3 → A5 list → A6 as models arrive |
| P performance | interface session (after the new map's sound) | P1 → P2 → P3 → P6 → P5 → P8 → P4 → P7 → P9 |

File ownership to keep merges clean:
- `shade()`, shadow, AO, sky, fog, exposure, tonemap, probes in `src/render.cpp` / `lighting.cpp`: stream L.
- `src/textures.cpp`, `tools/props/*bake*`, `content/textures/**`, decal code: stream M.
- `src/terrain*.cpp`, `kTerrainVS` geometry, `tools/mapgen/**`, map templates while a map is being regenerated: stream T.
- foliage shaders and draw path, `model.cpp` LOD / mips, alias tables, asset JSON: stream A.
- culling, batching, draw submission, settings tiers, benchmark code: stream P.
- A stream that needs a line in another's area asks for it or keeps the change to one hook.

## 5. Risks

- **The look changes when shadows stop being lightened and exposure changes.** Mitigation: re-fit per-map values to
  the old captures, ship behind Lighting: New / Classic, send A/B sheets for five maps before each lighting merge.
- **Relief breaks routes or balance.** Mitigation: route, spawn-timing and navigation checks per regenerated map; lanes
  keep their grades.
- **Five sessions share one graphics card and `render.cpp`.** Mitigation: exclusive GPU turns for timing, the
  ownership table above, small commits merged in order.
- **Costs are estimates.** Mitigation: the benchmark table decides; High may ship without L10 / L11.

## 6. Verification

- **Benchmark views** (P1): Chateau town, Newtown, forest outpost, D-day, Rue Haute; three views each (street level,
  dense foliage, overview); Low / Medium / High at 1440p; median, p95 and p99 GPU and CPU; per-pass ms.
- **Before / after sheets** per item from the same cameras, plus one sheet per map after each stream's milestone.
- **Motion check** per stream: a fixed 20 s walk recorded as frames, looked at for shimmer, ghosting, LOD pops and
  shadow crawl.
- **Gameplay checks** after terrain changes: routes loop, spawns reach objectives within the existing timing
  tolerances, bots path, no object floats or sinks more than 5 cm.
- **Sign-off per stream:** frame time within section 2 on the 2080 Ti, the look comparison accepted by the user.

## 7. Sources

- Aldridge, "Inside Bend: Screen Space Shadows", SIGGRAPH 2023: https://bendstudio.com/blog/inside-bend-screen-space-shadows
- Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion", 2016; XeGTAO: https://github.com/GameTechDev/XeGTAO
- MJP, "A Sampling of Shadow Techniques": https://therealmjp.github.io/posts/shadow-maps
- Lauritzen et al., "Sample Distribution Shadow Maps", I3D 2011
- Brinck, Jiang, Maximov, Naughty Dog material shading, SIGGRAPH 2016
- Barré-Brisebois & Bouchard, "Approximating Translucency", GDC 2011
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020: https://github.com/sebh/UnrealEngineSkyAtmosphere
- Patry, "Real-Time Samurai Cinema", SIGGRAPH 2021: https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html
- Lagarde & Zanuttini, "Local Image-based Lighting With Parallax-corrected Cubemaps", SIGGRAPH 2012; Karis, "Real Shading in Unreal Engine 4", 2013
- Lagarde & de Rousiers, "Moving Frostbite to PBR", 2014
- Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
- Wronski, "Volumetric Fog", SIGGRAPH 2014; Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", 2015
- Wronski, "Exposure Fusion: Local Tonemapping for Real-Time Rendering", 2022
- Therrien, Levesque, Gilet, "Screen Space Indirect Lighting with Visibility Bitmask", 2023: https://arxiv.org/abs/2301.11376
- Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 2022: https://jcgt.org/published/0011/03/05/
- Mishkinis, "Advanced Terrain Texture Splatting", 2013
- Tokuyoshi & Kaplanyan, "Stable Geometric Specular Antialiasing with Projected-Space NDF Filtering", JCGT 2021; Toksvig, "Mipmapping Normal Maps", 2004
- Sousa & Geffroy, "The Devil is in the Details: idTech 666", SIGGRAPH 2016
- Lagarde, "Water drop 3b: Physically based wet surfaces", 2013
- Moore, "Terrain Rendering in Far Cry 5", GDC 2018; Etienne, "Large Scale Terrain Rendering in Call of Duty", SIGGRAPH 2023
- Mei, Decaudin, Hu, "Fast Hydraulic Erosion Simulation and Visualization on GPU", 2007
- Castaño, "Computing Alpha Mipmaps"; Wyman & McGuire, "Hashed Alpha Testing", I3D 2017
- Brucks, "Octahedral Impostors", 2018
- Salvi, "An Excursion in Temporal Supersampling", GDC 2016; Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014
- Rákos, "Hierarchical-Z map based occlusion culling", 2010; nvpro-samples gl_occlusion_culling
- Haar & Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015
