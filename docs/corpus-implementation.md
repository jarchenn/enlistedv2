# Applying the ultra-realistic games corpus

Approved by the user on 2026-11-20. The corpus is at `Documents/MapMaker/corpus/ultrarealistic-games/` (14 chapters; start with 13 and 14).

### Context
The user supplied a 154,000-word reference corpus (`Documents\MapMaker\corpus\ultrarealistic-games\`, 14 chapters) and
said: "read and implement ideas in here, ingest all before you start". Every chapter was read in full (13 and 14 by the
coordinator; 01-12 by three readers who also checked each idea against `wt-int\src`). The corpus's own caveat stands:
its constants come from the literature or the web, some from memory, and its budget tables are planning figures. Every
item below is therefore measured in this engine before it is kept.

**The corpus's argument, in one paragraph.** Realism comes from brightness ratios at medium scale (sun to shade, sky to
ground), objects that are grounded, distance haze, and an image that holds still; top quality tiers and texture
resolution add little. So: measure first, get light and exposure physically consistent, keep every preset coherent,
remove shimmer before adding sharpness, and cut resolution and sample counts before removing any cue.

**Decisions the user made for this work (2026-11-20):**
- **Target:** Medium and High both hold 60 fps at 1080p on the RTX 2080 Ti; 1440p is reached by upscaling. Budget
  15.0 ms GPU at P99 on a real-time camera path (the corpus's 10 % headroom), CPU inside the same frame.
- **Resolution:** dynamic, bounded. Medium and High 0.77-1.0, Low 0.67-1.0 (Low no longer 0.5 at 1080p); a fixed
  native option stays.
- **Lighting:** physical units everywhere. Every map is re-lit from real sun, sky and exposure values; moods will
  shift and each map is re-approved by the user. This supersedes the earlier "keep each map's authored colours" rule
  and L5's option A; "Lighting: Classic" stays as the old look for comparison until all maps are approved.

**What the readers found in the engine (the reasons for the order below):**
- Sun to sky on open ground is about 2:1 (corpus: about 5:1 at this sun height); shade is about 1.2 stops under
  sunlit ground (corpus: 2.4-2.8; under 1.5 "reads flat"). This is the measured cause of "looks flat".
- The grade runs after the tone curve and pulls mid grey from sRGB 118 to about 105; overcast is darkened and
  desaturated in the grade instead of by exposure (the cause of the grey snow map).
- Textures are sampled 0.6-1.6 mip levels too coarse and no negative bias follows the upscaler; two sharpeners are
  stacked; upscaler jitter has 8 phases where Medium needs 18 and Low 32; TAA does not dilate velocity. Together these
  are "Medium looks soft".
- Damp ground cannot look wet: the matte-ground floor (roughness 0.82, specular x0.35) overrides wetness.
- Environment specular uses a hand fudge (x0.6), no multi-scatter; no furnace test; no albedo range check (grass
  falls to 0.08).
- High captures 8 GI probes a frame (10-12 ms); poses run once per simulation step (up to 2 a frame) and pose LOD
  only applies above 200 soldiers; a "lazy" texture set triggers a reload of every set mid-match (prime suspect for
  the 1.24 s hitch); two shaders compile on first use; the benchmark samples 4 s of static views.
- Hi-Z culling and multi-draw-indirect are poor GPU value while the frame is pixel-bound; texture streaming saves
  memory, not frame time. Both drop to "only if a measurement asks for it".

### Work, in the corpus's order (measure, light, materials, grounding, stability, cost)

**Step 0. Measuring tools first** (lighting + performance sessions). Nothing else is judged without these.
- Look-dev kit: spawnable grey, white and chrome spheres plus a colour chart; false-colour luminance; illuminance
  view (albedo forced to 0.18); albedo range view (red < 0.02, blue > 0.9, metal checks); texel-density checker;
  overdraw count (`GL_FRAGMENT_SHADER_INVOCATIONS`); a readout of sun:shade stops, sun:sky ratio, mid-grey display
  value, clipped and crushed pixel shares. One command, `--selftest-lookdev`, writes the numbers per map.
- White furnace test as a self-test (uniform sky, spheres roughness 0-1, result within 2 %).
- Benchmark hardening (`tools/bench_views.sh`, `src/bench_views.cpp`): a real-time camera path per map and one
  100-bot scene; warm-up plus 3 laps, thousands of frames; P50/P95/P99/P99.9/max and hitch count; GPU clocks locked
  in `tools/gpu_turn.sh`; time blocked in swap reported apart from CPU; regression rule (over median by threshold and
  over 3 sigma). Always-on hitch log at 1.5x the moving median with what happened that frame (texture uploads,
  first-use shaders, `buildRT` count).

**Step 1. Free sharpness and stability** (small, each with an A/B; materials + lighting sessions).
- Mip bias: gradient scale 3.0 / 1.5 -> 1.0 and bias = log2(render/display) - 0.5 under upscaling
  (`src/textures.cpp:328-336, 521`); anisotropy 16 on ground and walls; shimmer held by the existing Toksvig mips and
  specular AA (`--selftest-render` shimmer metric must stay in tolerance).
- TAA / upscaler (`kTaaFS`, `src/render.cpp:2470-2580`, `settings.cpp:40`): jitter phases 8/s^2 at +-0.5 px;
  velocity taken from the closest depth in 3x3; history clip clamped to the neighbourhood box.
- One sharpener: a contrast-adaptive sharpen after the tone map with a player slider; remove the TAA and grade ones.
- Triangular +-1 LSB dither at the final write. Shadow slope-bias cap 6 -> 2; contact-shadow rays to tens of cm.

**Step 2. Physical light and exposure** (lighting session; replaces `lighting.cpp`'s authored tables under "New").
- Sun from elevation: air mass m(h), E_DN = 133,000 exp(-0.21 m) lux; sky E = 800 + 15,500 sqrt(sin h) lux, split by
  weather (clear, hazy, overcast 10-20 klx, rain 2-5 klx); sun colour by elevation (5,800 K to 2,500 K). The existing
  atmosphere code (L5) drives colours directly instead of scaling authored ones.
- Ground bounce from the map's ground albedo: L_ground = rho (E_sun sin h s + E_sky) / pi (snow 0.8, grass 0.2,
  forest floor 0.1); hemisphere ambient recomputed per frame; probes relit from the same sky (L6).
- Exposure in EV100: exposure = 1 / (1.2 * 2^EV); histogram metering kept but adapted in EV, clamped per environment
  (day 9-15.5, interior 2-10), partial compensation k 0.5-0.8; overcast and night are exposure states, not grade
  multipliers; night with the scotopic blend. Lamps in lumens with the windowed inverse-square falloff; local lights
  culled by exposure (E_min = pi 1.2 2^EV / 512).
- Display chain order: exposure -> grade in scene-linear (contrast pivot 0.18) -> tone curve -> encode -> dither; one
  32^3 LUT with a log shaper. Identical on every preset.
- Per-map art that remains: hour, weather, visibility (haze as sigma = 3.912 / V), cloud knobs, a light grade.
- **Gate per map:** a Classic | Physical sheet (same cameras) plus the measured numbers; the user approves map by
  map; an unapproved map keeps Classic as its default. Order: Blackwater Thaw (it is being rebuilt now), forest
  outpost, Rue Haute, Chateau town, then the rest.
- Acceptance numbers: clear midday sun:shade on open ground 2.4 +- 0.3 stops; sun:sky 4-7:1; scene 0.18 lands at
  sRGB 118-128; under 1-2 % clipped, under 2-5 % crushed; fresh snow +2 stops over the grey card.

**Step 3. Materials to one standard** (materials session).
- Environment specular: split-sum (EnvBRDFApprox) with multi-scatter 1 + F0 (1/E_ss - 1), replacing the x0.6 fudge;
  multi-bounce AO with the pixel's own albedo; furnace test green.
- Albedo: clamp dielectric base colour to linear 0.02-0.9; fix grass clump darkening (floor 0.10); report maps with
  more than 1 % of pixels out of range.
- Wetness per the corpus: albedo x lerp(1, 0.35, wet x porosity), roughness -> max(0.35 r, 0.25 while reflections
  are weak), exempt from the matte-ground floor (`render.cpp:3002-3027`, `ground_gloss.json`); puddles 0.03-0.08.
- Wear with causes: roughness variation +-0.1-0.2 at macro and meso scale; contrast budget (neighbouring materials
  within 20-40 % luminance, which is the corner-stone fix QS1); a shared BC4 mask library in place of the single
  grunge atlas; a micro detail normal/roughness layer that fades by 7 m; dirt that follows splash height, runoff and
  use. M4's rebake applies these.
- Water by absorption (0.34, 0.057, 0.009 per m, to be checked) and scattering, not a tint.
- Texel density: report per class, bring outliers toward 512-1,024 per metre for walk-up surfaces.

**Step 4. Grounding, depth and air** (lighting + terrain + vegetation sessions).
- Shadows: four cascades on High and Ultra, movers redrawn every frame in every cascade, a cached static layer for
  the mid cascade; a baked terrain sun-shadow (horizon) map so distant relief is shadowed.
- Bent normals into ambient and probe lookups; reflection cube boxes fitted to rooms; SSR fading over roughness
  0.3-0.5.
- Fog colour derived from the same sun and sky with a phase term; haze by visibility; froxel fog on High and Ultra.
- Particles lit, shadowed, fogged and depth-faded (`kFxFS`).
- Vegetation: model-tree LOD and cull by projected pixels with 10 % hysteresis and a dithered cross-fade; octahedral
  impostors for far trees; alpha pre-pass with an EQUAL test for model foliage and grass; ground under grass tinted
  to the grass colour.
- Terrain: relief that reads from eye height (T2), as already briefed.

**Step 5. Cost, by the corpus's ladder** (performance + gameplay sessions). Classify CPU- or GPU-bound first; change
one thing; keep only measured wins.
1. Dynamic resolution from GPU timestamps: s_target = s sqrt(budget / t_gpu), 10 % headroom, fall fast, rise slow,
   3 % dead band, viewport sub-rectangle (no reallocation), mip bias and jitter follow s; off in benchmarks.
2. Drop 4x MSAA on High (P2, already written).
3. Probes: with sky relighting in place, capture only when geometry changes, by priority, 1-2 a frame on High.
4. Terrain pass: diagnose with the knock-out, mip-bias and two-resolution tests before changing it; then the cheap
   fixes they point to (snow terms and detail taps by distance, `discard` only where tiles deform, backdrop LOD);
   a clipmap material cache only if the diagnosis says texture-bound.
5. Shadow cost: far-cascade resolution and cadence, small-caster cull, vegetation in the two nearest cascades only.
6. Half-resolution SSR and transparency where still over.
7. CPU: poses once per drawn frame (not per step), distance bands at any bot count, skip soldiers in no view, pose
   jobs on workers; simulation steps capped at 3-4 with render interpolation and filtered dt.
8. Hitches: preload every texture set a map names and assert no reload in a match; compile all shaders at load with
   a program-binary cache; time-slice `syncRuntime`; stop presenting frames from inside the load loop.
- Hi-Z culling, multi-draw-indirect and texture streaming: not scheduled; revisit only if step 0's counters show a
  draw-count or memory bound.

**Presets, re-cut by the corpus's rules** (`src/settings.cpp`). Medium is the art-directed baseline; High and Ultra
add samples and range, Low removes density and resolution. Every preset keeps every cue (shadows near the camera,
contact shadows, AO, haze, temporal AA, the same exposure, tone curve, grade and fog colour). Steps of about 2x cost.
Settings shows each preset's internal resolution. Ultra (UL1, in progress) follows this template and has no frame
target.

### Who does what
| Session | Steps |
|---|---|
| Lighting (S0->D1) | 0 look-dev kit, 1 TAA items, 2 physical light, 4 shadows / fog / reflections, then Ultra |
| Performance (PF3) | 0 benchmark and hitch log, 5.1-5.6 and 5.8, preset re-cut |
| Materials (PF1) | 1 mip bias, 3 materials, QS1 corner stones, M4 rebake under the new rules |
| Gameplay (zombie-variants) | 5.7 poses and simulation step |
| Vegetation (gam-5) | 4 vegetation items, DF1b |
| Terrain (MP2) | 4 terrain shadow map, T2 relief |
| Map (L1 leaves) | the castle; Blackwater Thaw re-fit under physical light |
The coordinator relays, merges into `integ-dday`, builds, checks startup through `tools/gpu_turn.sh`, pushes, and
shows the user each gate.

### Not in this pass (corpus items left out, and why)
- Foot placement and audio occlusion / reverb (corpus 13.12 item 12): real gains, but outside rendering; offered as
  the next pass.
- Ray tracing, frame generation, visibility-buffer or deferred rewrite, virtual texturing, meshlets: the corpus
  itself rules them out or ranks them last for a small OpenGL engine.
- Depth of field, motion blur and lens effects in first person: the corpus calls them realism risks.

### Verification
- Every item lands with its number from the list above, not an opinion, plus an A/B sheet.
- `--selftest-lookdev` per map before and after step 2; the furnace test and albedo report after step 3.
- Benchmark at 1080p on the real-time path, 3 laps, 100-bot scene included: Medium and High at or under 15.0 ms GPU
  P99, hitch count 0, CPU frame under budget; 1440p rows recorded with the internal resolution shown.
- Each preset captured on the same path and checked to read as the same place at the same hour.
- The user plays each re-lit map and approves or rejects it; nothing is play-tested by the coordinator.

---

## Appendix: what the readers found in the code

Read-only findings from 2026-11-20 against `integ-dday`. Line numbers drift; treat them as where to start looking.
"Corpus" means `Documents\MapMaker\corpus\ultrarealistic-games\`; chapter and section are given so the owner can read
the reasoning. Figures marked (computed) were derived from shader formulas, not measured in a capture: measure them
with the step 0 tools before and after.

### A. Light, exposure and display (corpus 02, 04; step 2)
| Finding | Where | Corpus value |
|---|---|---|
| Day sun (3.30, 3.05, 2.62) at 46.5 deg; sky (0.30, 0.36, 0.47); sun:sky on open ground about 2.0:1 (computed) | `render.cpp:4282`, `lighting.cpp:250` | about 5:1 at that height (04 s2.1 table: 90 deg 6.6, 60 deg 6.0, 30 deg 3.7, 20 deg 2.5, 10 deg 1.0) |
| Shade depth 1.2 stops with `newShadowLift = 0.85`; 1.58 at full shadow; 0.9 in Classic (computed) | `render.h:148` | 2.4-2.8 stops; under 1.5 reads flat, over 4-5 reads gamey (02 s6) |
| Ground bounce colour is a constant (0.17, 0.155, 0.12) scaled by time of day only | `lighting.cpp:295`, `render.cpp:510` | rho_ground (E_sun sin h s + E_sky) / pi, s 0.5-1; snow bounces 4x grass (04 s2.2-2.3) |
| Units are arbitrary: sun 3.3 scene units, lamps `kLampUnit = 0.25` | `lighting.cpp:319` | sun 100-109 klx, sky 15-20 klx, lamps in lumens (02 s4-5) |
| Exposure adapts in linear space at 0.6/s up, 1.2/s down; Day clamp 0.9-1.45 (0.69 stop) | `render.cpp:3692-3720`, `lighting.cpp:302-304` | adapt in EV, about 3/s brighten and 5/s darken; clamp per environment; partial compensation k 0.5-0.8 (02 s7.4) |
| Overcast: sun x0.30, sky about x1.5 grey; then the grade multiplies by (1 - 0.38k), -22 % saturation, cooler | `lighting.cpp:526-537`, `render.cpp:3568` | overcast is an exposure state (EV 12-13), saturation rises, contrast from occlusion only (04 s3) |
| Grade runs after the tone curve in display-linear; contrast pivots at 0.5 and moves scene 0.18 from sRGB 118 to about 105 | `gradeColor`, `render.cpp:3560-3572`, `content/settings/grade.json` | grade before the curve, pivot 0.18; mid grey at sRGB 118-128 (04 s8.4-8.5) |
| LUT 32^3 indexed by sqrt of the tone-mapped value | `render.cpp:3651` | log shaper, -12 to +10 EV (04 s8.4) |
| No output dither except inside the vignette branch | `render.cpp:3654-3682` | triangular +-1 LSB at the final write (07 s8, 10 s10.11.11) |
| No exposure-driven light culling | `lighting.cpp` | E_min = pi 1.2 2^EV / 512; d_max = sqrt(I / E_min) (02 s9) |
| Fog colour mixes fog and sky colour plus a pow(.,8) sun glow; haze sigma 0.0010 x map haze | `render.cpp:283-313` | fog colour from the same sun and sky with a phase term; sigma = 3.912 / visibility (11.3) |

Validation kit status (04 s10, 03 s10): no spheres or chart prop, no luminance or exposure false colour, no
illuminance view, no albedo range view, no furnace test, no sun:sky or shade-stops readout, no texel-density view, no
overdraw view. Existing pieces to build on: `NMK_DBG_TERRAIN` 1 and 2, `hdrProbe()`, `--selftest-lights`,
`--selftest-render` (shimmer), the `--rq-off` switches.

### B. Materials and textures (corpus 03, 07, 08; steps 1 and 3)
| Finding | Where | Corpus value |
|---|---|---|
| Mip bias is positive: gradient scale 3.0 (+1.58) on sets beyond 10 m, 1.5 (+0.58) on ground, 1.6 on relief sets; no render-scale term | `textures.cpp:328-336, 521-523` | never coarser than hardware LOD; under upscaling bias = log2(render/display), i.e. -0.58 at 0.67, -1.0 at 0.5 (07 s4.1, s6.3; 10 s10.5.4) |
| Anisotropy 8 on sets and grunge, 4 on grass and leaves | `render.h:166` | 8-16 on ground, walls and normals (07 s5) |
| Environment specular = rough Fresnel x (1 - 0.6 r) x 0.6; no DFG table, no multi-scatter | `render.cpp:529, 546` | split-sum F0 A + B (or EnvBRDFApprox) and 1 + F0 (1/E_ss - 1) (03 s4.2, s6.2) |
| Visibility is Schlick-GGX with k = (r+1)^2/8; minimum alpha 0.02 | `render.cpp:496-499` | height-correlated Smith, or the fast form on low tiers (03 s2.2) |
| Multi-bounce AO uses a fixed albedo 0.4 | `render.cpp:2459` | per-pixel albedo (03 s6.3) |
| No albedo clamp; grass base 0.17-0.24 x 0.5-0.85 clump shading reaches about 0.08; soil 0.16-0.23; mud 0.10-0.14; snow (0.80, 0.83, 0.88) on target | `render.cpp:2890-2906, 230` | linear 0.02-0.9; grass 0.15-0.25; wet dark soil 0.05-0.08 (03 s4.4) |
| Wetness: albedo x0.62 at wet >= 0.83 with no porosity; roughness drops only in puddles; matte floor 0.82 and dry specular x0.35 apply to all damp ground (`mudDamp` 0.8) | `render.cpp:3002-3027`, `content/settings/ground_gloss.json` | albedo x lerp(1, 0.35, wet x porosity); roughness -> max(0.35 r, 0.05), keep >= 0.25 while reflections are weak; puddles 0.03 (08 s7) |
| One RGBA8 grunge mask (3 channels); curvature only as box-edge distance; no micro detail layer beyond a height grain within 7 m | `textures.cpp:602-610, 744-763, 536` | shared BC4 mask library (16 masks = 2.7 MB); micro detail 512^2 BC5 at 20 cm, strength 0.2-0.5, fade d_2px to 3 d_2px (08 s9-10) |
| Dressed stone (pattern 19) is kept cleaner than its wall (`matK` 0.95) | `render.cpp:1240-1274` | neighbouring materials within 20-40 % luminance; roughness varies before colour (08 s12) |
| Texel density 137 to 2,560 per metre across sets (set sizes 0.4-7.5 m) | `content/textures/textures.json` | uniform per class; walk-up 512-1,024 per metre (07 s1.3) |
| Albedo stored BC3 (alpha = AO), normals BC5, roughness + height BC5; about 2.3 GB | `textures.cpp:188, 855` | acceptable; texture resolution costs memory, not frame time (07 s2-3) |
| Water extinction x (1.35, 1, 0.85): red:blue 1.6:1 | `water.cpp:262-269` | absorption about (0.34, 0.057, 0.009) per m, from memory in the corpus: check a source (11.7) |
| Particles: unlit vertex colour, no depth fade, no fog, no phase | `kFxFS`, `render.cpp:3781-3824` | lit, shadowed, fogged, soft; smoke never emissive (11.8) |

### C. Anti-aliasing, upscaling, shadows, geometry (corpus 06, 09, 10; steps 1 and 4)
| Finding | Where | Corpus value |
|---|---|---|
| Jitter phases 8 on Low and Medium, 16 on High native; native amplitude +-0.25 px | `settings.cpp:40`, `render.cpp:5227-5228` | 8 at native, 8 x (display/render)^2 when upscaling (18 at 0.67, 32 at 0.5), +-0.5 px (10 s10.4.2) |
| TAA takes velocity at the pixel, no 3x3 closest-depth dilation; clip gamma 2.25 still to 1.1 moving, not clamped to the min/max box | `render.cpp:2491-2560` | dilate; gamma about 1, clamp to the box (10 s10.4.3-10.4.5) |
| Two sharpeners: TAA 0.30 (halved under upscaling) and grade 0.22 | `render.cpp:2570, 3627-3633` | exactly one, after tone mapping, exposed to the player (10 s10.4.7, s10.10) |
| Low renders at 0.5 at any output size | `settings.cpp` | not below 0.67 at 1080p (10 s10.5.5, 13.10.2) |
| Shadow slope-bias term capped at 6, plus `glPolygonOffset(1.5, 2)`; no `GL_DEPTH_CLAMP` | `render.cpp:99-104, 5072` | cap 2, bias scaled by filter radius, depth clamp for directional casters (06 s2.1, s2.4) |
| Contact-shadow rays 0.4-1.6 m | `render.cpp:2400-2419` | centimetres to tens of centimetres (06 s2.7) |
| Cascades are fixed radii round a forward focus (near 32/40 m, mid 3x, far max(240, 2.5 r)); 3 on Medium and High; mid redrawn every 4th / 2nd frame so movers lag | `render.cpp:5053-5177`, `settings.cpp:233-236` | 3-4 cascades, movers always in the per-frame layer, static layer cached and scrolled by whole texels (06 s2.2, s2.8) |
| No terrain shadow beyond the far cascade | `terrain.cpp` (sky-view AO only) | shadows that reach the horizon matter more than sharp ones (06 s2.2 l.163) |
| GTAO present, bent normals unused; quarter resolution on Medium | `render.cpp:2294, 5745` | half resolution on the middle tier; bent normals about +25 % of the AO cost (06 s3.3-3.5) |
| Reflection cube uses a fixed 24 m box | `render.cpp:147-152` | box fitted to the room (06 s5.2) |
| SSR: linear 28-step march, hard cut-off at roughness 0.4, full resolution inside composite | `render.cpp:2381-2438` | fade band 0.3-0.5; half resolution is 1/4 the cost (06 s5.4-5.5; 05 s8.4) |
| Model trees switch LOD at 30 m and cull at 400 m in metres, no hysteresis, no cross-fade; buildings already use pixels with 15 % hysteresis | `foliage.cpp:489-490`, `assets.cpp:1323`, `app.cpp:281, 547-559` | pixel threshold with k = H / (2 tan(fov/2)), 5-15 % hysteresis, dithered fade over 5-10 % (09 s2.1-2.5) |
| No impostors; canopy pre-pass exists, model foliage and grass have none; building z-prepass exists but is off | `render.cpp:5309-5348`, `render.h:204, 224` | octahedral impostors 12x12 in 2048^2; masked pre-pass then EQUAL with no discard (09 s2.6, s7.1; 05 s5.3) |
| Terrain LOD rings 260/560/1100/2200 m; `terrainCache` bakes noise only; the terrain shader can `discard` on every chunk | `terrain_chunks.h:14-24`, `render.h:221`, `render.cpp:2858-2861` | cap active layers, detail only within 50-100 m, clipmap material cache turns 12-40 taps into 2-3 (09 s6.3-6.4); `discard` delays depth writes (05 s5.1) |

### D. Frame cost, CPU and hitches (corpus 01, 05, 12; steps 0 and 5)
| Finding | Where | Corpus rule |
|---|---|---|
| High captures 8 probes a frame (6 scene views each), Medium 1; 10-12 ms on Chateau town | `lighting.cpp:772` | do it less often; indirect light may be slow to update (01 s9.1; 13.2.3) |
| `posesStep` runs inside `Game::step`, which loops up to 8 times a frame; pose LOD bands (80/150 m) only when more than 200 soldiers; no skip for soldiers in no view; serial loop | `explore.cpp:568-579`, `game.cpp:1065, 1086-1189` | pose once per drawn frame, rate and feature LOD at any count, skip unseen, evaluate on workers (12 s12.4) |
| Fixed 60 Hz step with a cap of 8 and no interpolation; Jolt on variable dt; raw dt clamped at 0.1 | `explore.cpp:568-581`, `physics.cpp:257`, `app.cpp:1235` | cap 3-4, interpolate by alpha, filter dt (12 s12.5, s12.11) |
| `tex::flushPending()` runs every frame; a "lazy" set reloads and re-uploads every set | `app.cpp:1227`, `textures.cpp:807-927` | no blocking work on the main thread; upload within a per-frame byte budget (12 s12.9-12.10). Prime suspect for the 1.24 s hitch |
| Shaders compile synchronously; water and `modelLive` compile on first use; no program-binary cache | `render.cpp:3860-3874, 4901`, `water.cpp:390` | compile at load, binary cache keyed by source + GL_RENDERER/VERSION, warm-up draws (12 l.802-808) |
| `syncRuntime` runs `buildRT` synchronously; `loadingTick` presents and polls events from inside the loop over `doc.instances` | `app.cpp:229-259, 1254`, `loading.cpp:195, 243` | time-slice integration. The re-entrant present is an unverified candidate for the "sync: built" crash |
| Benchmark: 4 s per static view; median/p95/p99 only; no camera path, no bots, no clock lock; hitch log only over 150 ms behind `NMK_FRAME_PROF` | `bench_views.cpp:93-163`, `app.cpp:1585-1624`, `tools/gpu_turn.sh` | fixed path plus a turn, warm-up + 3 laps, P99.9 and max, hitch at 1.5x moving median, regression rule (01 s3, s7-8) |
| No dynamic resolution; GPU timestamps already exist per pass | `render.cpp:4964-4993`, `render.h:185` | control law in 10 s10.7 and 01 s10.2 |
| 259 `glUniform` call sites, per-draw uniforms; no persistent mapping; MDI for terrain only; `occCull` and `hlod` exist but are off | `render.cpp:4390`, `terrain_chunks.cpp:312`, `render.h:208, 251` | CPU submission fixes; no GPU gain while pixel-bound (01 s9.2; 05 s7) |
| Terrain pass up to 21.7 ms on High (Blackwater Thaw) | `kTerrainFS` | diagnose first: knock-out ceiling, positive mip bias test, two-resolution fit t(s) = t_fixed + t_px s^2, fragment-invocation count (01 s4.3, s9.1) |

### E. Where the corpus disagrees with the earlier overhaul plan
- P3 (Hi-Z) and P4 (multi-draw-indirect): CPU-side or geometry-side wins; under 10 % of GPU geometry time and little
  in open terrain. Demoted until a counter shows a draw or vertex bound.
- P8 (texture streaming): a memory and load-time item, not a frame-time one, on an 11 GB card.
- P5: a canopy pre-pass already exists; extend the same method to model foliage and grass.
- Targets: 16.7 ms p95 left no headroom and static views cannot catch hitches. Now 15.0 ms at P99 on a moving path.
- L3: four cascades were promised on High and not built.
- "Not doing: MSAA with alpha-to-coverage": the corpus rates it well for foliage edges in a forward renderer, but it
  needs MSAA, which is being removed from High for cost. Left out; revisit only for Ultra.
- Low at 0.5 render scale contradicts the corpus's 1080p floor of 0.67.
