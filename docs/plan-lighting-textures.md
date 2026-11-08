# Plan: Frustum culling, lights with indirect lighting, time of day, ambientCG textures

## Context
The user asked for four things, in this order:
1. Frustum culling.
2. Interior lighting for every building that isn't rubble.
3. Placeable light sources (street lights, lamps, lantern posts and similar) that produce **indirect light**.
4. Using that lighting to **improve all textures**, with ambientCG CC0 texture sets.

The user's choices:
- A **curated ambientCG set** fetched by our own script. The pasted third-party downloader is not used; it pulls the whole 10 GB library.
- **1K resolution everywhere.**
- **Time of day as a global option in the Settings menu**, not per map.

What the code looks like today (exploration of `vo-batch-b`):
- **No frustum culling for meshes.** Every opaque item, tree and model is drawn 4× (near cascade, far cascade, sky occlusion, main) with one `glDrawElements` each, about 1930 draws in the village.
- **No local lights.** The sun is fixed (`render.h:99`), and night is blocked by the exposure clamp (0.9-1.45).
- **The renderer is forward**, with an MRT HDR target and attachments for normal/roughness and indirect light; it has no G-buffer.
- **Lighting code is duplicated** in `shade()`, `kLeafFS`, `kGrassFS`, `kGlassFS` and `water.cpp`.
- **Interiors look dark** because `amb = mix(0.06,1,skyVis)` (`render.cpp:192-194`), and their only bounce light is `uFill`.
- **Materials are 30 procedural patterns.** Box-kit meshes have no UVs and no tangents. Only GLB models have a texture path: `loadTex` and normal mapping in `kModelFS`.
- **Texture units 0-15 are all in use.**
- **Omaha has about 1.3 ms of headroom** under the 16.7 ms Medium gate.
- **Room data** in `InteriorSys::rooms()` gives the storey floor and ceiling, room boxes, `hearthPos` and window sills. `furn_lamp` and `furn_candlesticks` are already placed by the furnishing templates.
- **Lamp breakage would replicate for free.** Prop breaks run through `breakProp`/`blast`/`bulletHit`, which LAN replays as world events.
- **No "ruined" flag exists.** Ruins are recognised by id.

## Step 0: finish the overhaul merges first (coordinator, with the V sessions)
The light streams are cut from `vo-batch-b` now, and the fixes are merged into them later.
- **Batch B failures:** the integration problems come from V4's auto-furnishing. Send them back to V4 (`local_7f9cb527`), with V4 working on top of `vo-batch-b`:
  - B "exactly one placed instance after reopen";
  - grass exclusion footprints;
  - editor house place/undo counts;
  - nature "patch back exactly" and save/reopen;
  - interiors: `ruin_townhouse_facade` is empty;
  - structures: 12 tours of the ruins and `vil_chateau_keep` blocked.

  Fix: **stop auto-furnishing ruins** (`ruin_*`, `vil_church_ruined`, `vil_chateau_keep`; the same exclusion list the lighting uses), and make the counting suites skip `extra.furnish`.
- **LAN host:** its log stops after the match starts. The coordinator investigates (V1/V4/V5 candidates) and routes the fix to the owning session.
- **aicourse batch-A failure:** I treat it as timing-sensitive. Async navmesh rebuilds against the fixed 1/60 s step change the result whenever frame cost changes. Batch B, a superset of A, passes it. Note it in `docs/testing.md`.
- **Batch C:** merge V6 at **17604a3** and then V8. Wire `builder.cpp` to `snapPlacement()`. Fix the `beautify.json` ids to the real V1/V3/V9 asset names.
- **V1's TAA tweak** goes on top of batch B.
- Full suite plus `--net`, then **ask the user** to fast-forward master to the overhaul.

## New streams (4 parallel sessions; worktrees `nmk-l1..4`, branches `lt-N-<name>` from `vo-batch-b`, build dirs `%TEMP%/nmk/build-ltN`)
Ownership follows the overhaul's rules: one or two hook lines in shared files, with the logic in the stream's own files. `tools/gpu_turn.sh` is used for every GPU test.

### L1: Frustum culling and draw cost
- Add bounds to `GpuMesh`, computed in `GpuMesh::upload` (`assets.cpp:70`). All `DrawItem` sources get them for free.
- Per-pass plane culling in `drawItems` / `drawFoliage` / `drawModels(Depth)`:
  - main pass: `vp`, from the standard-Z projection before `reverseZ`;
  - near cascade: `lightVP`;
  - far cascade: `lightVPFar`;
  - sky occlusion: `occVP`.
- Cascades test their own ortho boxes, so off-screen casters are kept.
- Reuse `frustumPlanes`/`boxVisible` (`terrain_chunks.cpp:226-237`), moved into a shared header. Door leaves get bounds.
- Cache uniform locations: no `glGetUniformLocation` per item. Sort by mesh/state where it's cheap.
- Thumbnails and model sheets stay unculled.
- Expose a per-view culling API that L2 uses for light and shadow views.
- **Tests:** the village draw count drops a lot, and the `maps` gate gains headroom (report ms before and after). Run `render` (shimmer and legacy identity), `visuals`, `fps`, `sights` and `maps`.

### L2: Lighting engine: time of day, many lights, indirect light
- **Time of day:**
  - A global Settings option (Graphics or Gameplay tab, persisted in `settings_user.json`): Day / Dusk / Night / Dawn, plus a fine hour slider.
  - It drives `sunDir`, sun/sky/fog colours, the sky model, cloud shadows and stars or moon at night.
  - Widen the exposure clamp, with a night exposure curve.
  - `setLighting` stays the single entry point.
  - Self-tests force Day, so the existing gates don't change.
- **Local lights:**
  - `struct LightDesc {pos, color, intensity, radius, dir, cosInner, cosOuter, shadow, flicker, id}` in `render.h`, filled into `R.lights` each frame. L3 fills it.
  - Clustered forward lighting: a froxel grid (for example 16×9×24) built in a compute pass, with a light SSBO and index lists. The `character.cpp` compute/SSBO pattern is the model.
  - A `localLights()` term is added to `shade()` **and** to the leaf, grass, glass and water shaders.
- **Shadowed lights:** the nearest N spot or point lights (Low 0, Medium 4, High 8) render into a depth atlas through `progShadow` with L1's culling. Keep standard Z in that pass.
- **Indirect light:** a **probe volume**, DDGI-lite.
  - An irradiance probe grid around the camera (about 1.5-2 m spacing near, coarser far) and denser probes inside building interiors, placed from room boxes.
  - Probes are captured by rasterising small cube views with the lit forward shaders: sun, sky and local lights.
  - A few probes update per frame. They're projected to L1 spherical harmonics, with visibility from capture depth so light doesn't leak through walls.
  - `irr`/`envL` in `shade()` and the copies are replaced. `gInd` stays on attachment 2, so SSAO still works.
  - Lamplight then bounces onto walls and ceilings, and window light spills into rooms.
  - Presets: Low off (the old fill term), Medium near volume at a low update rate, High full.
- **Emissive surfaces:** add an `emissive` material field (lamp glass, flames, embers) that feeds the HDR before bloom, so lights bloom.
- **Units and budget:**
  - The new texture units come from units 16-19. Check `GL_MAX_TEXTURE_IMAGE_UNITS`.
  - The Medium gate holds with L1's savings.
  - Add Settings controls for light count, shadowed lights and GI quality.
- **Tests:** new `--selftest-lights` covering:
  - cluster correctness, using a known light count per froxel;
  - a lit room is brighter than an unlit one;
  - indirect light reaches a wall facing away from the lamp;
  - no light leaks through a wall;
  - time-of-day presets;
  - Day equals the pre-change look within tolerance.

  Also run `render`, `maps` (Day gate, plus Night reported) and `settings`.

### L3: Light assets, interior lighting, placement
- **`light` block in asset JSON**, parsed like `InteriorSys::prop()`: `{color, intensity, radius, cone, dir, flicker, offset, group, shadow}`. `group` links the light to a prop group, so breaking the glass or chimney turns the light off.
- **New assets**, built with `gen_env_assets.py` (outdoor) and `gen_furniture.py` (indoor), period-correct for 1944 Normandy:
  - gas street lamp, iron lantern post, wall lantern, hanging lantern;
  - oil lamp, table lamp, candle and candelabra, ceiling lamp;
  - brazier, campfire, fireplace fire and stove glow.

  Add lights to the existing `furn_lamp` and `furn_candlesticks`.
- **Interior lighting for every building that isn't rubble:**
  - Exclude `ruin_*`, `rub_*`, `vil_church_ruined`, `vil_chateau_keep`, `aw_r612_unfinished` and `bch_lcvp_wrecked`. Bunkers get electric caged lamps.
  - Per room, from `rooms()`: one ceiling or table light by room type and area, a hearth fire glow at `hearthPos`, and window spill from L2's probes.
  - Lights are on at dusk and night, and interiors are dimly lit in the day.
  - Placed with auto-furnishing (no undo noise). `extra.light {on}` is saved.
  - A breach of the host walls, or breaking the light's prop, turns it off. This uses the existing `breakProp`/`blast`/`checkSupport` paths, which are already replicated by world-event replay.
- **Placement:**
  - Beautify role `street_lamps` along paths and roads.
  - Auto paths can line routes with lamp posts (`extra.auto="lamp"`, removed on re-run).
  - An editor Light panel for colour, intensity and on/off.
- Fire and flicker reuse the wreck-flame particle recipe (`tank_sim.cpp:741-761`).
- **Tests:**
  - `interiors`: every intact building gets lights, ruins get none, and a broken lamp turns off.
  - `nature`/`autopath` with the lamp role.
  - LAN: a lamp shot out on the host is off on the client.

### L4: Textures, ambientCG CC0, 1K
- **Fetch:** new `tools/fetch_ambientcg.py` (Python `-I`, standard library only), driven by `tools/texture_sets.json`.
  - About 40-60 curated materials: plaster/limewash, brick, rubble stone, ashlar, slate, terracotta, planks, painted wood, concrete, rust/metal, fabric, cobbles, dirt, mud, gravel, grass, rock, sand.
  - It calls the official ambientCG API (`ambientcg.com/api/v2/full_json`) for the **1K-JPG** zips and keeps Color, NormalGL, Roughness, AO and Displacement.
  - It packs roughness, AO and height into one RGB.
  - Output goes to `content/textures/<set>/`, about 150-250 MB kept.
  - **The download is confirmed with the user (source, file list, size) before it runs.**
  - Raw zips stay outside the repo; only the packed 1K images are committed.
  - Credits go in `docs/credits.md` (CC0, ambientCG).
- **Engine:**
  - `GL_TEXTURE_2D_ARRAY` per channel (sRGB colour, normal, packed RMA), mips and 8× anisotropy, units 20-23.
  - A texture-set index per material: `"texture": "<set>"` in the materials JSON, carried as pattern ≥ 100 or as a new vertex attribute.
  - **Triplanar sampling** on asset space (`vAp/vAn`), or part space for oriented grain. Use a whiteout normal blend, with height into the existing derivative bump and the `gPix` detail fade.
- **Look and lighting:**
  - Keep the procedural per-stone tint, weathering and room-finish logic layered on top, so each building still varies.
  - Normal maps respond to the sun, L2's local lights and probe GI.
  - Missing files fall back to the procedural patterns, and the legacy look stays procedural.
- **Coverage:**
  - Map every building, prop and furniture material family to a set: buildings, ruins, rubble, furniture, defences, paths.
  - Terrain layers in `kTerrainFS` (grass, soil, gravel, mud, rock, sand) use world-space planar or triplanar samples blended by the existing masks.
- **Tests:**
  - `render`: shimmer index does not get worse, using mips and anisotropy.
  - `visuals` (thumbnails re-render), `maps` (FPS gate), `interiors`.
  - Missing-texture fallback.

## Merge order (coordinator)
1. Step 0, to master with the user's OK.
2. Then L1 → L2 → L3 → L4.
3. After each one: `build_mpu_assets --check`, the full suite plus `--net`, and the user's OK for master.
4. A final tuning pass, L2 and L4 together: exposure, light intensities, texture tiling.
5. Rebuild the play folder for the user.
6. The LAN zip only on request. It grows by about 150-250 MB of textures, all CC0 and fine to share.

## Verification
- **Build:** no new warnings in the new files.
- **Suites:** all existing suites plus the new `lights` suite.
- **Medium `maps` gate at Day** still under 16.7 ms. Night and High are reported.
- **Manual pass on Battle 1:**
  1. Night via Settings: windows glow, rooms are lit, light bounces onto walls and ceilings.
  2. Street lamps along the paths.
  3. Shoot out a lamp and it goes dark.
  4. Grenade a lit room: the lamps break and the lights go out.
  5. Day: walls and terrain show the new textures with no shimmer.
  6. The frame rate is equal or better than before.

## Critical files
- **New:**
  - `src/lights.cpp/.h` (L3 data), `src/lighting.cpp` (L2 clusters, probes, time of day), `src/textures.cpp` (L4);
  - `tools/fetch_ambientcg.py`, `tools/texture_sets.json`, `content/textures/`;
  - `docs/lighting.md`, `docs/textures.md`.
- **Modified:**
  - `src/render.cpp/.h`, with ownership split: L1 draw and pass loops, L2 `shade()`/`setLighting`/exposure, L4 material and pattern sampling;
  - `src/assets.cpp/.h` (bounds, material texture field);
  - `src/interior.cpp`, `src/settings.cpp`, `src/water.cpp`, `src/grass.cpp`;
  - `tools/gen_env_assets.py`, `tools/gen_furniture.py`;
  - `content/settings/beautify.json`.

## L4 handoff: textures (branch `lt-4-textures`)

Full docs: `docs/textures.md`. Credit: `docs/credits.md` → "Textures: ambientCG (CC0)".

**Download.** The user approved 57 ambientCG ids, 1K-JPG only, 448.5 MB (`tools/texture_sets.json`; the dry run is in `side-session-notes/lt4-ambientcg-dryrun.md`).
- All 57 downloaded and packed. There were no skips and no swaps.
- Raw zips stay in `%TEMP%/nmk/ambientcg_raw` (675 MB extracted).
- The commit holds `content/textures/<set>/{color,normal,rma}.jpg` + `meta.json`: 105 MB, 228 files, plus `textures.json`.
- Fifteen sets ship without an AO map, so their AO is white.

**Engine.** All in `src/textures.cpp/.h`. Hooks in shared files:
- `render.cpp`:
  - `Renderer::init`: two `compile` lines prepend the texture GLSL, and one line `tex::init` + `attach`;
  - `kMainFS`: the slot decode at `pat`, a `texReplace` guard ahead of the pattern chain, `texMaterial()` before the weathering, the height added into the derivative bump, and the normal added after it (5 lines);
  - `kTerrainFS`: per-layer samples next to the existing mixes.
- `assets.h/.cpp`: `Material::texCode`, the `"texture"` field, `params.x = pattern + 100 * code`.
- `app.cpp`: `--pack-texture`, `--texture-placeholders`, `--no-textures`, `--selftest-textures`.
- `thumbs.cpp`: one line adds `tex::stateKey()` to the tile cache key.
- `selftest.cpp`: one dispatch line.
- `CMakeLists.txt` → `cmake/lt4.cmake`.
- `tools/run_all_tests.sh`: the `textures` row.

The vertex format is unchanged. No generator or asset JSON changed: the pattern and material-name → set mapping is done at load time (`content/textures/textures.json`), so `build_mpu_assets --check` is unaffected.
- **Texture units 20-22** (colour+AO BC3 sRGB, normal BC5, roughness+height BC5), compressed on the CPU with `stb_dxt`. `GL_MAX_TEXTURE_IMAGE_UNITS` = 32.
- 48 of the 57 sets are referenced by a slot, about 190 MB of VRAM, loaded in about 1.5 s (threaded). Nine sets are unmapped and not loaded (listed in `docs/textures.md`).

**Results** (build `%TEMP%/nmk/build-lt4`, all through `gpu_turn.sh`):
- `render` shimmer, layer on vs `--no-textures`: 0 of 26 views worse by more than 2 %, mean ratio 0.99, worst +0.8 %. The farmhouse and the moving strip improve by 2-3 %. The tuning history is in `docs/textures.md#shimmer`. The first untuned pass was 22 % worse on average.
- `textures` (new): 27/0. Includes the missing-texture fallback: no sets, a corrupt set, a lone set, and `--legacy-look` stays procedural.
- `visuals` 31/0: thumbnails re-render, because the cache key now includes the texture state.
- `interiors` 191/1 and `B` 67/1: the two known batch-B failures from Step 0 (`ruin_townhouse_facade` empty; exactly one instance after reopen). Neither comes from L4.
- `A` 226/0 and `damage` 145/0 (fast mode). The rest of the fast run was still queued at handoff: `%TEMP%/nmk/lt4-fast.log`.
- **`maps` FPS gate: not measured cleanly. Please re-run it on a quiet GPU.**
  - The user raised `gpu_turn` to 5 shared slots today. Even with `GPU_TURN_EXCLUSIVE=1`, other sessions' NormandyMapmaker processes ran outside the lock, so `tank_range` (normally 5 ms) measured 25-46 ms.
  - The exclusive back-to-back A/B is in `%TEMP%/nmk/lt4-fps/report_maps_{off,on}.txt`. The run with the layer **on** was faster on every map (Omaha Medium 20.9 vs 91.3 ms), which only shows the noise.
  - The cleaner signal comes from the earlier same-lock `render` runs: water views took +0.4 ms GPU on average with the layer on.
  - Expected cost: one tap of three BC textures per box-face pixel, and 1-3 planar taps per terrain pixel inside the fade distance.
  - If Omaha's 1.3 ms headroom is not enough, these knobs reduce the cost: narrower terrain `fade`, a lower `uTexLod` threshold, or `--no-textures` on Low.
- Tuning left for the joint L2+L4 pass: per-slot strengths in `textures.json`, the 9 unmapped sets, and terrain anti-tiling (the layers currently fade out by 140-260 m).
