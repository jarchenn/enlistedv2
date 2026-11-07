# Plan: Visual & Physics Overhaul (the user's PDF, items 0–6)

## Context
The user sent a seven-part request list ("Untitled document.pdf"). It covers:
- new ruined buildings, rubble, water and a wheat field;
- auto-furnishing every building at 3x density, with destructible furniture;
- a creative-mode overhaul: Beautify, mantle out of trenches, seamless snapping, crushing under buildings, first-person placement, new camera controls, facade shimmer and wall roughness;
- terrain sculpting for rolling hills;
- auto dirt paths from door to door;
- frames that break realistically;
- Low/Medium/High graphics presets with a settings menu;
- less oppressive tank and AT-gun firing smoke. Item 6 is cut off at "taper"; we read it as reducing that smoke.

The goal is all of it, as fast as possible, through parallel sessions.

What the code exploration found today:
- **Content gaps:** no ruined houses, rubble assets, wheat or a water renderer.
- **Furnishing:** only on request; the default density gives about 0.7 of the slot stock. Built-in furniture is part of the static mesh, so it is indestructible.
- **Frames:** frames, jambs, sills, lintels and hood moulds are permanent static parts, outside the damage bays, and there's no structural support logic.
- **Vault:** max 1.35 m; trench lips are about 1.9–2.0 m.
- **Snapping:** none for trenches or hedges. Pieces each take their own height, the spoil parapets overlap by 0.4 m, and texture coordinates are per part, so seams show.
- **Placement:** buildings sit over vegetation without removing it, and nothing flattens terrain.
- **Editor camera:** RMB orbit, MMB pan, LMB drag moves the instance, and there are no zoom buttons.
- **Facade shimmer:**
  - coplanar faces (`raggedWall` in `catalog_village2.mjs:47` uses `aIn = aOut`);
  - layers 5 mm apart with a 0.03 m near plane and standard depth;
  - unfiltered 85–230 cycles/m procedural detail, made worse by the derivative bump.
- **Flat walls:** relief is only bump at millimetre scale; ambient light ignores sideways normals.
- **Terrain:** no sculpt tools. `doc.terrainDeltas` (fine 0.45 m tiles) exists, and `sculpt()` in `selftest_ai_course.cpp:52` is a model brush.
- **Paths:** `path_dirt` items and `snapPath` exist. Building entrances (`nav.entrances[].p/.dir`) give door anchors.
- **Settings:** no settings window and no presets. Quality flags exist in `Renderer::Quality` (`render.h:70`), and grass presets exist. The only persisted setting is `gore_user.json` (the `userFile()` pattern in `gore.cpp:45`).
- **Muzzle smoke:** `Fx::muzzle` (`fx.cpp:44`) spawns 20 puffs living 3–5 s at alpha 0.5–0.55, all hard-coded.

**User decisions (2026-11-07):**
- Merge W5b (`mpu-w5b-montcastre` 8dbc862, the "Normandy Village Assault" template) into master **first**. Every stream branches from that.
- Beautify grows **only the outer ring**: the playable area and placements are unchanged. It adds about 80–120 m of terrain beyond the world border, filled with 2–3 rows of decorative buildings, plus rocks, vegetation and water.

## Step 0: coordinator setup (main session, before the streams start)
1. Merge `mpu-w5b-montcastre` into master. Fast-forward if possible, otherwise a merge commit. Build master and run the `maps` suite.
2. Copy this plan to `docs/plan-visual-physics-overhaul.md` in master. That is where the streams and later models find it.
3. Add `tools/gpu_turn.sh <cmd…>`, a mkdir lock at `%TEMP%/nmk/gpu.lock` with stale-lock recovery. Every stream runs GPU self-tests through it, one at a time, so 9 sessions don't measure each other's frame times.
4. Start one worktree session per stream, cut from the new master:

   | Item | Pattern | Example (V1) |
   |---|---|---|
   | Worktree | `nmk-vo<N>` | `nmk-vo1` |
   | Branch | `vo-<N>-<name>` | `vo-1-render` |
   | Build dir | `%TEMP%/nmk/build-vo<N>` | `%TEMP%/nmk/build-vo1` |

   Builds are configured the same way as `build-pkg`, using the FetchContent cache in `%TEMP%/nmk/fc`.

## Workstreams (9 parallel sessions)
File ownership prevents overlapping edits. **"Hook only"** means: one or two lines at a named call site, with the logic in the stream's own file.

### V1: Facades, rougher walls, water (items 1g, 0a water)
- **Shimmer:**
  - Move world rendering to reverse-Z: `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`, depth cleared to 0, `GL_GREATER`. Patch every depth reader: SSAO, SSR, shafts, TAA, fog, soft depth, picking (`app.cpp:682` projection, `render.cpp`).
  - Fallback: near plane 0.1 m outside the viewmodel, plus polygon offset on layered trim.
  - Fix the coplanar skins: in `raggedWall`, inset `aIn` like `shell()` does (`catalog_village2.mjs:17-20, 47`). Move shutters and hinges to at least 2 cm off the wall (`parts.mjs:252, 265`). Regenerate only the `vil_*` family via `build_mpu_assets.mjs`.
  - Filter procedural detail by `fwidth`: fade limewash grain, ashlar tooling and rubble pits towards their mean beyond the Nyquist frequency (`render.cpp:542-646, 949-961`).
  - Retune the TAA jitter and weight if needed.
- **Rough walls:**
  - Add mid-scale relief to patterns 17, 18, 19, 22 and 20: plaster bulges and patches, per-stone pillows 2–4 cm, mortar recess, cracks, grime and weathering streaks.
  - Make ambient and sky light respond to normals that face sideways (a cavity / bent-normal term in `shade()`, `render.cpp:175-198`).
  - Raise bump amplitudes, with the filtering above.
- **Water:**
  - New `src/water.cpp/.h` and a water pass after opaque: animated normals, Fresnel, sky reflection plus SSR when it's on, depth-based shore fade and colour.
  - Assets with a `water` block: pond (round and irregular), shell-crater pool, puddle cluster, stream straight, stream bend and stream end. The stream pieces get V6 snap sockets.
  - Port `env_sea_shallows` to the new pass.
- **Owns:** `kMainFS`/`kMainVS` material code, the depth convention, `water.*`, `catalog_village2.mjs`, the shutter and hinge offsets in `parts.mjs`.
- **Tests:** `visuals`, `env`, `interiors`, `maps` (FPS gate); before/after screenshots of facades at 20, 50 and 100 m.

### V2: Settings menu, graphics presets, firing smoke (items 5, 6)
- **Settings:**
  - New `src/settings.cpp/.h`, persisted to `exeDir()/settings_user.json` (same pattern as gore).
  - A **Settings** window, opened from the Creative top menu, the pause menu and Loadout. Categories:
    - **Graphics:** preset Low / Medium / High / Custom, resolution scale, shadow quality, SSAO, SSR, bloom, light shafts, TAA, clouds and cloud shadows, grass, draw distance, particle budget, vsync, FPS cap, FOV.
    - **Gameplay:** mouse sensitivity, ADS sensitivity, invert Y, reduced motion, gore level, show arms, sight markers.
    - **Audio:** master, effects, UI and ambience volume. This needs a master gain in `audio.cpp`.
    - **Controls:** rebinding of the actions in `bindings.json`, plus a list of fixed keys.
    - **Interface:** UI scale, editor camera speeds.
- **Presets:**
  - **Medium = exactly today's defaults.**
  - **Low:** near shadows 2048 / far 1024, no cloud shadows, shafts, SSR or SSAO; grass Low; decor cull ×0.6; resolution scale 0.85; smaller particle and fragment caps.
  - **High:** far shadows 4096, full-resolution SSAO, grass High, cull ×1.4, larger budgets, more jitter samples.
- **Renderer changes:** shadow sizes as parameters, internal resolution scale with upscale at tonemap, SSAO resolution, and a reset of the cached `w5CullDraw` multiplier. These are in the target, shadow and composite setup in `render.cpp`/`render.h`, not the material shaders, which V1 owns.
- **Firing smoke:**
  - Move the `Fx::muzzle` parameters into `content/settings/fx.json` and taper them:
    - puffs 20 → about 8;
    - alpha 0.5 → about 0.28;
    - life 3–5 s → 1.2–2.4 s;
    - faster growth and fade, with a fade-out curve.
  - Per-weapon `muzzleScale` comes from the vehicle and gun JSON (Sherman, Tiger, PaK 40, Flak).
  - Touches `fx.cpp`, `tank_sim.cpp:478`, `station.cpp:383` and `weapons.cpp:405`.
- **Tests:**
  - new `--selftest-settings`: preset application, round trip through the file, Medium equal to defaults;
  - the `maps` FPS view at Low, Medium and High (reported, not gated except Medium);
  - `tanks`, `pak`.

### V3: Ruined buildings and rubble (items 0, 0a rubble)
- New `tools/catalog_ruins.mjs`, registered in `build_mpu_assets.mjs`, generating only the new `ruin_*`/`rub_*` assets.
- **Ruins**, all walkable, with nav, walkthrough and ruined interiors:
  - shelled two-storey house with a collapsed roof;
  - gutted farmhouse shell;
  - half-collapsed shop and terrace row;
  - burnt-out barn;
  - ruined cottage corner;
  - bombed townhouse facade-only.
- **Rubble:** brick and stone piles (S, M, L), beam and tile heap, collapsed wall section, spill strip (snappable through V6 sockets), scattered debris patch.
- Use the existing kit (`parts.mjs` `wall`/`window`/`door`/`stairs`, ragged walls), `k:0` rubble boxes and `setNav`/`walkthrough`. Thumbnails are automatic.
- **Tests:** `structures` (tours), `interiors`, `visuals`.

### V4: Furniture everywhere, 3x density, destructible (item 0b)
- **Auto-furnish:**
  - Furnish every enterable building with no `extra.interior` on map open, on new map and on placement (`interior.furnish(app,id,0)` hooks after `syncRuntime` in `App::openMap`, `App::newMap` and `placeInstance`). Keep it out of undo noise.
  - Skip rules in `roomType()`/a skip list: bunkers, `aw_*`, `bch_*`. Churches get pews instead.
  - Verify room detection on the `vil_*` buildings and the V3 ruins.
- **Density at least 3x the current piece count:**
  - New recipes in `Planner::room()` (`interior.cpp:920-989`) with more slots per wall, clutter on surfaces, and corner and centre pieces.
  - About 20 new furniture types in `tools/gen_furniture.py`, such as dresser, bookcase, washstand, stove, sideboard, pantry shelves, sacks, barrels, coat rack, clock, pictures, utensils, buckets and workbench tools.
  - Raise the density range (line 1018, slider 1160).
  - Measure pieces per building before and after, with the gate in `selftest_interiors` at ≥ 3x.
- **Destructible:** auto-furnish turns `hideBuiltin` on, so all furniture becomes breakable pieces.
  - Route tank HE/AP shells, AT shells and rockets into `interior.blast`/a direct-hit `breakProp`.
  - Make grenades in a room "explode" the furniture: tune the thresholds, add more fragments plus splinter and dust FX.
  - Tanks push furniture instead of being blocked (`tank_ram.cpp:302-339`, the furniture branch only).
- **Performance:**
  - Batch intact furniture per building into merged per-material meshes, splitting out a piece when it breaks, so 3x pieces doesn't mean 3x draws.
  - Fragment caps follow V2's particle-budget setting through a getter.
- **Tests:** `interiors` (extended), `damage`, `tanks`, perf numbers in the kitchen and inn views.

### V5: Frames and structural collapse (item 4)
- **At load time only, with no asset regeneration:**
  - In `extractWallPanels` (`assets.cpp:279-339`), bind "trim" parts to their bay by material and adjacency: window frames, jambs, lintels, sills, hood moulds, thresholds.
  - Trim breaks into debris when:
    - the supporting bay cells under or around it are removed;
    - an HE/AP/AT shell or explosive hit lands within its radius;
    - a tank rams it.
- **Window sashes** become breakable (`ballistics.cpp:27`).
- **Support:** a support pass between bays: a bay or spandrel left floating over a destroyed bay collapses. This extends the unsupported-fragment pass in `wallfield.cpp:255-329` to neighbours above.
- **Tank ramming:** a "core contact" within the trim margin breaks the trim instead of blocking (`tank_ram.cpp:308-339`).
- **Multiplayer:** keep events deterministic and replayable (`authoredWallEvents`/`replayWallDamage`), so LAN late-join stays in sync.
- **Tests:** `damage`, `tanks`, `env`, `net` (once, at merge).

### V6: Editor controls, seamless snapping, crush and flatten (items 1f, 1b snap, 1d)
- **Controls:**
  - Select tool, LMB press-drag: orbit around the pressed point (ground or object hit); this never selects.
  - Click with no drag: select.
  - Press-drag that starts **on the already-selected object**: move it, keeping the current `checkPlacement` and undo.
  - RMB orbit and MMB pan stay.
  - **+ / −** buttons at the bottom centre zoom towards the screen centre.
  - Touches `editor.cpp:104-135` and `editor_nav.cpp:294-326`. Update `w3StatusHelp`.
- **Snapping** (new `src/snap.cpp`):
  - Assets declare sockets (`snap: [{p, dir, kind}]`) for trenches (straight, corner, zigzag, entry), hedgerows, sunken lane, `path_*`, V9 wheat squares, V3 rubble strip and V1 stream pieces.
  - When placing or moving within 1.5 m of a compatible socket, snap position and yaw. Pieces in a chain share their base height, so the cut floors join.
  - The overlap check allows snapped neighbours.
  - Seam removal:
    - spoil 8.4 → 8.0 m;
    - end caps hidden on connected ends;
    - a world-space texture offset uniform for snap-family materials (one line in `kMainVS`, coordinated with V1).
  - Absorb the existing `snapPath`/`snapDefense` through the same API (calls into `paths.cpp`, which V9 owns).
  - Socket data goes into the `catalog_atlantic`/`catalog_bocage` generators. Regenerate only `aw_*`/`bcg_*`.
- **Crush and flatten:**
  - Placing a building removes trees, bushes and nature instances under its footprint whose height is ≤ a limit (default 1.6 m; mature trees above it block with a message).
  - It flattens the terrain under the footprint to the base height with a feathered skirt through `terrainDeltas`, in one undo step.
  - Hooks in `checkPlacement`/`placeInstance`, `app.cpp:338-420`.
- **Tests:** `editor` (with scripted mouse orbit, move and zoom), `structures`, `nature`, a new snap check (no gap or overlap, matching heights).

### V7: Mantle out of trenches, first-person building (items 1b climb, 1e)
- **Mantle/climb:**
  - A new climb onto ledges 1.35–2.1 m high: hold Space facing a ledge with standing room on top.
  - Procedural hands-on-ledge pose with the viewmodel lowered. The camera arc rises, then rolls forward over 0.9–1.3 s.
  - Separate from vault, so `maxDepth`/landing rules don't reject it. Never through wire.
  - Code in `explore.cpp:202-264` (next to `tryStartVault`/`stepVault`), `pose.cpp`, `viewmodel.cpp`. Values in `movement.json`.
- **First-person placement:**
  - In drop-in Explore, **B** opens build mode with the full Assets palette (categories, search, thumbnails), reusing the editor's palette code.
  - A ghost appears in front of the player within 12 m. Wheel or Z/X rotates, LMB places, Del removes the aimed placement, Ctrl+Z undoes.
  - Placements are real map edits when the session started from Creative, using the same `checkPlacement`/`placeInstance` and V6's snapping once merged.
  - Code in `builder.cpp` and `dropin.cpp`.
- **Tests:**
  - `move` adds a trench-exit check from straight, corner and zigzag pieces, plus a 2.1 m wall;
  - `update`, `entities`, `env`;
  - a new first-person placement check in `selftest_editor`.

### V8: Terrain sculpt and Beautify (items 2, 1a)
- **Terrain tool** (new `src/terrain_brush.cpp`; one Toolbar/Tool enum entry in `editor.cpp`):
  - Brushes: Raise, Lower, Smooth, Flatten (to the clicked height), Rolling hills (seeded noise stamp), Cliff/terrace (stepped falloff).
  - Radius, strength and falloff controls; one undo step per stroke.
  - Writes `terrainDeltas` with incremental chunk mesh and collider rebuilds.
  - Placed objects inside the stroke re-seat to the ground. Raised ground shades as grass, not dirt (`applyDeltas`, `terrain.cpp:325`).
  - **Generate hills…** for the whole map: amplitude, scale and seed, avoiding footprints, objectives and paths.
- **Beautify…** (Creative top menu). A dialog with density sliders (rocks, vegetation, ground cover, water, border buildings), a seed, Preview/Apply and a single undo.
  1. Scatter open ground, avoiding footprints, paths, objectives, spawns and attack routes. The asset roles come from `content/settings/beautify.json`, so V1, V3 and V9 assets slot in.
  2. Grow the outer ring by about 100 m: per-map margin in the doc, terrain regenerated, with a **terrain-delta key remap** to the new tile grid (`terrain.h:81`). Ring terrain gets gentle hills.
  3. Place 2–3 rows of decorative buildings (houses, barns, ruins) in the ring facing inward, with hedges and trees between.
     - They're tagged `extra.decor`: allowed outside the border (a `checkPlacement` exception), not furnished, not in the navmesh, and cheaper culling.
- **Tests:** a new `--selftest-terrain` covering brush determinism, undo, save/reload, delta remap and object re-seating, plus a Beautify run on Battle 1 (instance counts, nothing inside footprints, ring present). Also `editor` and the `maps` FPS gate after Beautify.

### V9: Ground cover, wheat field, auto paths (items 0a wheat, 1c, 3)
- **Nature content** (`catalog_nature.mjs`, `nature.cpp`), as scatter patches like `nat_stones_patch`:
  - **Wheat field square** (10×10 m): golden stalk cards with wind, edge sockets for V6 snapping, and ripe, trampled and stubble variants. Tanks and explosions flatten it.
  - **Patchy brush:** 3 variants.
  - **Yellow/dry grass:** 3 variants.
  - Clover/green tufts and a mixed meadow.
- **Auto paths** (new `src/autopath.cpp`; owns `paths.cpp`):
  - Each house's soil band becomes a real editable `path_dirt` apron at its entrance (`nav.entrances`).
  - **Paths > Connect houses** (plus an optional "Auto paths" toggle that connects each newly placed house):
    - a minimum spanning tree plus a few loop links between entrances;
    - A* on a coarse grid around footprints, water and steep slopes;
    - smoothed, then emitted as chained `path_dirt` segments (curved joints added to `paths.cpp`) plus a ground-wear route in `doc.extra.routes`.
    - Re-running replaces the previous auto paths only (tagged `extra.auto`).
- **Tests:** `nature` (new patches, wheat flattening), `entities` (paths), a new auto-path check on Battle 1 (every house connected, no path through a footprint).

## Coordination rules (every stream)
- **Branching:**
  - Cut from the post-W5b master. Don't push.
  - Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - Never run `build_assets.mjs` wholesale; regenerate only your own families.
  - Audio and sample banks never go into packages.
- **Shared-file hooks:**
  - Stay at one or two lines in `editor.cpp`, `app.cpp`, `render.cpp` or `explore.cpp`; the logic lives in the stream's own new file.
  - Ownership for overlapping areas:
    - `kMainFS`/`kMainVS`: V1
    - render targets, shadow sizes and presets: V2
    - `interior.cpp`: V4
    - `wallfield.cpp`/`extractWallPanels`: V5
    - editor mouse and camera, `snap.cpp`: V6
    - vault/mantle and `builder.cpp`: V7
    - `terrain*.cpp`: V8
    - `paths.cpp`, `nature.cpp`: V9
- **GPU:** run only your touched suites, through `tools/gpu_turn.sh`. No full suite per stream. The coordinator runs the full suite once per merge batch, plus `--net` once.
- **Docs:** each stream updates its own doc (for example `docs/interiors.md`, `docs/destruction.md`) and adds a handoff note to `docs/plan-visual-physics-overhaul.md`.

## Merge order (coordinator, main session)
1. **Batch A, independent content and settings:** V2 → V3 → V9 → V7.
2. **Batch B:** V1 → V5 → V4.
   - V4 after V5, because both touch damage events.
   - V1 early, because of the depth convention.
3. **Batch C:** V6 → V8.
   - V6 wires sockets into the V1, V3 and V9 assets.
   - V8 uses every new asset in `beautify.json`.

After each batch:
1. `build_mpu_assets.mjs --check` is clean.
2. Build.
3. Run the full suite plus `--net`.
4. Fast-forward master. Only the user approves master.
5. Rebuild the play folder and the LAN zip on request.

## Verification (end to end)
1. **Build:** 0 errors; no new warnings in the new files.
2. **Suites:**
   - all the existing suites stay green;
   - the new suites pass: `--selftest-settings`, `--selftest-terrain`, the snap, first-person placement, auto-path and Beautify checks;
   - the interiors density gate is ≥ 3x.
3. **FPS:** the Medium `maps` gate holds (Omaha eye-level < 16.7 ms on a free GPU). Low is faster and High is reported.
4. **Manual pass on Battle 1** in the play folder:
   1. Furnished houses.
   2. A grenade in a kitchen breaks the furniture.
   3. A tank shell takes out a window and its frame.
   4. Climb out of a trench.
   5. Snap three trench pieces with no seam.
   6. Place a house on shrubs: they're crushed and the ground is flattened.
   7. In first person, place a crate.
   8. LMB orbit and the +/− zoom.
   9. Facades at 50–100 m don't shimmer.
   10. Sculpt a hill.
   11. Beautify adds scatter and the border town ring.
   12. Connect houses with paths.
   13. Low/Medium/High switch live and persist.
   14. Tank smoke clears in about 2 s.
5. The user plays the rebuilt build before anything goes into a LAN zip.

## Critical files
- **New:**
  - `src/settings.*`, `water.*`, `snap.cpp`, `terrain_brush.cpp`, `beautify.cpp`, `autopath.cpp`;
  - `tools/catalog_ruins.mjs`, `tools/gpu_turn.sh`;
  - `content/settings/fx.json`, `beautify.json`;
  - `docs/plan-visual-physics-overhaul.md`.
- **Modified:**
  - `src/render.cpp`/`render.h`, `interior.cpp`, `assets.cpp`, `wallfield.cpp`, `ballistics.cpp`, `tank_ram.cpp`, `editor.cpp`, `editor_nav.cpp`, `explore.cpp`, `builder.cpp`, `dropin.cpp`, `fx.cpp`, `tank_sim.cpp`, `station.cpp`, `terrain.cpp`, `paths.cpp`, `nature.cpp`, `audio.cpp`, `app.cpp` (hooks);
  - `tools/catalog_village2.mjs`, `catalog_atlantic.mjs`, `catalog_bocage.mjs`, `catalog_nature.mjs`, `gen_furniture.py`, `build_mpu_assets.mjs`.

## Status and handoff notes (each stream appends one section when it hands off)
- 2026-11-07 coordinator: Step 0 done. master 4cd7a28+ has W5b merged (Tank Range, Normandy Village Assault; "Battle 1"
  and "Normandy Village Assault" are both the user's battle1 map). `tools/gpu_turn.sh` added.

### V1 handoff (2026-11-07): facades, rough walls, water
Branch `vo-1-render` (worktree `nmk-vo1`), two commits on 0e8479f: 27f8aae (the work) and the follow-up (relief tuning, docs, proof images). Not pushed. Full write-up: `docs/rendering.md` §9; proof images in `docs/rendering/v1/`.

**Tests** (build `%TEMP%/nmk/build-vo1`, each through `tools/gpu_turn.sh`):

| Suite | Result |
|---|---|
| `render` (new) | 48/0 |
| `visuals` | 31/0 |
| `env` | 42/0 |
| `interiors` | 141/0 |
| `maps` | 139/0, FPS gate held: Omaha worst eye-level 15.40 ms (master 15.33); Battle 1 17.5 ms (reported only) |

`visuals`, `env`, `interiors` and `maps` ran on the build one shader tweak before the final one. The tweak only changes the patterns 17/19/22 relief formula; `render` 48/0 confirms the final shaders compile.

**Shimmer** (`--selftest-render`, same camera path on master and here; lower is better):
- Farmhouse, still: 20 m 1.73 → 1.63, 50 m 2.00 → 1.85, 100 m 1.52 → 1.27.
- Ruined church, 100 m: 1.24 → 0.60.
- Strafe flicker: 50 m 3.37 → 3.00, 100 m 1.90 → 1.51.
- With the new wall relief off, the shimmer fixes alone give 33–61 % below master.

**Merge notes**
- **Depth convention: reverse-Z inside `Renderer::render`.**
  - CPU `projM` stays standard GL.
  - Code drawing inside `render()` must use `R.zLess()`, `zLEqual()` and `zToward()`. Depth readers use ndc z = depth, and the sky is 0.
  - Patched: SSAO, composite, shafts, TAA, `decals.cpp`.
  - V2's resolution scale is fine: the water pass uses the internal `width`/`height`.
- **`render.h`:**
  - `WaterDraw`, `Renderer::water`, `revZ` + helpers, `wallRelief` (A/B), `friend struct WaterPass`;
  - **`Quality::taaSharpen` default 0.40 → 0.30.** V2 should take 0.30 as Medium. If V2's settings test pins 0.40, update it.
- **One-line hooks:**
  - `app.cpp`: `buildDrawLists` water hook, `updateSurface` skips water assets, `--selftest-render` arg;
  - `selftest.cpp`;
  - `CMakeLists.txt` (`cmake/vo1.cmake`);
  - `tools/run_all_tests.sh` (`render`);
  - `tools/build_mpu_assets.mjs` (`waterAssets`).
- **Assets:**
  - `vil_norman_farmhouse`, `vil_church_ruined` and `vil_chateau_keep` regenerated;
  - new `wat_*` (7, category Water) from `tools/catalog_water.mjs`;
  - `env_sea_shallows` gains a `water` block. Its JSON was edited by hand: no generator owns it.
  - `build_mpu_assets.mjs --check` reports only `gun_flak18.json`, which already drifts on master.
- **For V6:** stream pieces carry `snap: [{p, dir, kind: "stream"}]`. Their channel cut runs 1.6 m past each socket. The footprints of a bend and a straight overlap when snapped, so the overlap check must allow snapped neighbours.
- **For V8:** `waterSurfaceAt(app, x, z)` and `waterAsset(a)` are in `water.h`, for Beautify's water role.

**Known limits** (`docs/rendering.md` §9.6):
- Old `build_assets.mjs` houses keep 5 mm shutters until that generator is re-run.
- The ruined church at 20–50 m is level with master.
- Water is not physical (no collision, splashes or wading sounds).
- SSR only, no underwater view.
- No far-plane culling of terrain and grass chunks under reverse-Z.
