# Terrain tools: sculpting, Generate hills, Beautify (V8)

Creative mode gains three tools:
- a **Terrain** sculpt brush (toolbar button, or **Terrain > Sculpt brush**);
- **Terrain > Generate hills...** for the whole map;
- **Beautify...** (top menu), which dresses the map and grows an outer countryside ring beyond the world border.

| Code | What's in it |
|---|---|
| `src/terrain_brush.cpp` | the brushes, the sculpt layer, placement re-seating, Generate hills, and the UI for both |
| `src/beautify.cpp` | Beautify, the ring, decor buildings and the decor draw path |
| `src/terrain.cpp` | the generator layers (`Terrain::applyGenLayers`) |
| `src/terrain_tools.h` | the API |
| `content/settings/beautify.json` | asset roles, densities, ring and decor rules |
| `src/selftest_terrain.cpp` | the tests (`--selftest-terrain`) |

## Ground layers

The ground is built in layers, bottom to top. `Terrain::height()` is still the single query.

1. **Base heightfield** (3.14 m grid): the template, plus `MapDoc::terrainGen`. That covers the outer ring (`ring`) and Generate-hills stamps (`hills`). `Terrain::generate` applies them, and they are saved as `"terrainGen"` in the map.
2. **Sculpt layer** (`Terrain::sculpt` / `MapDoc::terrainSculpt`): sparse fine tiles, 36×36 samples at 0.45 m, the same keys as `terrainDeltas`. Values are absolute metres added to the base. They are saved as `"terrainSculpt"` and rounded to millimetres.
3. **Terrain cuts** (trenches, ditches, foxholes) lower the sculpted ground.
4. **Crater deltas** (`MapDoc::terrainDeltas`) are relative to everything below them.

`groundBase = baseHeight + sculpt` is the authored ground. `Tile::base` holds it, with the cuts applied, so:
- persisted crater deltas never include sculpting;
- the disturbed-soil shading (`dirt`, derived from crater depth on reload) never sees it. **Sculpted ground, raised or lowered, shades as grass**, and so do valleys.
- a crater on a sculpted hill persists only the crater.

Every sculpt key keeps its tile active. `clearTiles()` and `resetGround()` reinstall the sculpt from the document.

## Sculpt brush

| Brush | Effect |
|---|---|
| Raise / Lower | ± strength m/s at the centre |
| Smooth | blends toward an 8-tap ring average (kernel 12 % of the radius) |
| Flatten | blends toward the height clicked at the stroke start |
| Rolling hills | a seeded fbm stamp (hill size, seed) |
| Cliff / terrace | a stepped falloff with flat treads and short steep risers (1–8 steps) |

**Controls:**
- radius 1–80 m, strength 0.1–12, falloff 0–1 (the fraction of the radius that feathers);
- a ring preview shows the radius and the hard core;
- the Smooth and Flatten strength works as a blend rate.

**Strokes:**
- LMB press starts a stroke, which is **one undo step**. An empty stroke leaves no step.
- Each frame applies one dab (`terrainBrushDab`) at the cursor.
- Only the tiles under the brush are re-based, remeshed (plus neighbours, for normals) and marked in `physDirty`, so their colliders rebuild in the frame's `terrainPhysicsSync`.
- `groundRev` bumps every 0.3 s and at the end of the stroke, so paths re-drape, nature patches re-check and the AI navmesh refreshes.
- Measured cost on a 600 m map: see `selftest_out/terrain_perf.json`.

**Tile borders:** samples are taken at the global fine-grid position, so a sample shared by two tiles gets bit-identical inputs. Borders never crack; the test checks this.

**Re-seating:** placements within the brush move by the change in their seat height.
- Buildings use the editor's footprint rule: the highest of nine samples.
- Trees, nature, defences, paths, guns and vehicles use the ground under their pivot.
- Furniture rides with the building it stands in.
- The explore start marker follows the ground.

## Generate hills

**Terrain > Generate hills...** takes amplitude (0.5–20 m), scale (30–400 m), seed, a keep-flat margin and a feather.

- It adds a seeded fbm stamp to the base grid.
- The stamp is zero inside the protected shapes (+ margin) and feathers out over the feather distance. It also fades toward open water.
- **Protected shapes** (`terrainProtectShapes`), stored with the stamp so reopening reproduces it exactly:
  - every non-dressing footprint, as an oriented box;
  - `path_*` placements;
  - worn routes (`extra.routes`);
  - objectives (capture radius) and their approach routes;
  - spawns (10 m);
  - the attack axis (attacker start through each objective);
  - the explore start.
- Trees, shrubs and boulders re-seat on the new ground.
- One undo step. **Terrain > Remove generated hills** drops every stamp.

Because hills live in the base and sculpt and craters are relative, earlier sculpting rides on the new hills.

## Beautify

Open **Beautify...** from the top menu. Its controls:
- density sliders: rocks, vegetation, ground cover, water, border buildings;
- a seed;
- toggles for "scatter inside the playable area" and "outer ring".

Preview applies the result live, and a repeated Preview replaces it. Apply keeps it as **one undo step**. Cancel (or closing the window) restores the map. **Remove earlier Beautify** deletes every `extra.beautify` placement. A re-run always replaces earlier Beautify output; it never touches authored placements.

### 1. Scatter

Roles come from `beautify.json`. Each role has:
- asset patterns (an exact id, or a trailing `*`);
- `perHa` at slider 0.5;
- `spacing`, `pad`, `maxSlope` and `cluster` (noise patches);
- `inside` / `ring`;
- `block`, which lays wheat-style squares edge to edge.

Patterns cover today's `nat_*`, `shrub_*` and `tree_*`, V1 `water_*`, V3 `rub_*` (rubble) / `ruin_*` (decor buildings) and V9 ground cover (`nat_brush_*`, `nat_drygrass_*`, `nat_clover_*`, `nat_meadow_*`, `gc_*`, `wheat_*`). Assets missing from the catalogue are skipped, and the dialog lists the skipped roles.

**Inside the playable area**, a candidate is placed only if:
- it is clear of every protected shape above by its pad;
- it is clear of every existing placement and of its own role by its spacing;
- it isn't in water and isn't too steep;
- it passes `checkPlacement`, which covers overlap, grade and water.

Existing placements and the terrain are never changed, except under a pond Beautify places (V1 `wat_pond_*` carry a terrain cut, so the basin is dug like any placed pond's and saved with the map).

### 2. Outer ring

The terrain grows by `ring.tiles` (6 tiles = 94 m) on every side: `terrainGen.ring`, so the margin goes from 100 to 194 m.

- **The inner grid keeps its world positions:** the grid grows by whole tiles, so every base vertex of the playable area samples the same point. W5 heightmaps carry an explicit `org`.
- Tile keys of `terrainDeltas` and `terrainSculpt` are remapped: `Terrain::remapKey`, `(tz+s)*newTX + (tx+s)`.
- Ring vertices continue the inner edge: an authored heightmap's edge level is kept, plus the blank-slate relief, a slight rise, and gentle hills (`amp` 3.5 m at a 110 m scale) that fade in over 45 m.
- The test checks that the playable area's ground is bit-for-bit unchanged (< 0.1 mm) across 1500 points, including through save/reopen and undo.

### 3. Decor buildings

Beyond the border, the slider sets 1–3 rows (default 2) at `rowOffsets` 16 / 44 / 72 m.

- Houses, barns, dairies, workshops, the ruined church and V3 `ruin_*` face inward, with ±5° jitter and a gap set by the slider.
- `hedgerow_bocage` pieces run along the gaps, with trees behind them.
- Placement uses the editor's height rules (`seatOutside`): no water, grade within the asset's limit, no overlaps.

Everything Beautify puts beyond the border is tagged `extra.decor` (plus `extra.beautify`). Decor placements:
- **are allowed outside the border:** the `decorMayLeaveBorder` exception in `App::checkPlacement`, for moving or rotating an existing decor placement; new placements there are still refused;
- **are never furnished:** V4's auto-furnish skips the tag;
- **are not in the navmesh:** the AI navmesh is clamped to the build area (`AiCore::begin`), and everything decor stands beyond it;
- **are drawn cheaply** (`beautifyDecorDraw`, the hook next to `w5CullDraw` in `App::buildDrawLists`):
  - the scene renderer draws every opaque item in the main pass and in all three shadow passes with no frustum culling. So beyond `nearDist` (140 m), decor and Beautify scatter whose bounds, padded by 30 m, fall outside the view frustum are skipped. The padding keeps off-screen shadow casters near the view;
  - buildings are culled beyond `cullDist` (950 m), trees and hedges beyond `treeCullDist` (480 m), and other ring dressing beyond `smallCullDist` (300 m). Inside the border, low Beautify scatter (under `insideLowHeight`, 1.3 m: ground cover, stones, rubble, small shrubs) is culled beyond `insideLowCullDist` (170 m); Beautify trees take the ordinary path;
  - beyond `nearDist`, a building draws only its shell and intact panels (glass up to `glassDist`, 320 m), with no door or shutter leaves: 1–3 draws per building;
  - near, damaged or furnished decor takes the ordinary path.

## Tests

`--selftest-terrain` writes `report_terrain.txt`, `terrain_perf.json` and `terrain_beautify_ring.png`. It covers:
- every brush mode, the falloff, determinism, tile-border continuity and grass shading of sculpted ground;
- colliders, one undo step per stroke, and re-seating (tree, house);
- craters over sculpt, save/reopen and the dab cost;
- Generate hills: relief, footprints and paths unchanged, re-seating, undo;
- the ring: tile counts, key remap, exact playable ground, save/reopen, undo;
- Beautify on Normandy Village Assault: counts per role, nothing in footprints, paths, objectives or spawns, tags, the decor edit exception, existing placements untouched, one undo step, save/reopen;
- eye-level frame time before and after. The gate is < 16.7 ms, like `--selftest-maps`. Where the un-beautified map is already over budget on the machine (battle1's objective B view measured 16.4–17.0 ms on the shared RTX 2080 Ti), the gate is instead that Beautify adds at most 0.5 ms. Measured: about +0.1 ms on the worst view, and +3 ms looking straight at the ring from the edge (5.5 → 8.5 ms).
