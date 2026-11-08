# Walls, interiors and furniture (Phase 2 V1, second half)

Source: `Normandy_Phase_2_Tanks/ENVIRONMENT_AND_INTERIORS.md` (outside the repo). The two reference screenshots in that
package were used only as visual guidance. No texture, mesh or other asset was taken from them.

## 1. Diagnosis (before)

Every material in this engine is procedural, evaluated in the main fragment shader from the asset-space position (`src/render.cpp`, `kMainFS`). There are no texture files, so missing bindings, colour-space mistakes or normal-map conventions cannot be the cause. The causes are in the patterns:

- **Plaster reads as a flat fill.** Pattern 3 (`plaster`) is the albedo times 0.9–1.1 low-frequency noise, plus a 1.2 mm bump from a single noise octave. It has no fine grain, no pits, no cracks, no coverage variation and no damp. Two lines of the pattern are dead code (`dirt * 0.0`). The inn's and roadside office's exterior `render` uses the same pattern, which gives the grey wall in `wall_current.png`.
- **Brick is too contrasty.** Pattern 9 multiplies each brick by 0.75–1.15 independently, which is close to ±25 % luminance noise. Its mortar is a fixed light grey (0.55), not related to the brick colour, and it is flat (no recess).
- **Quoins look pasted on.** `stone_dressed` uses the coursed-rubble pattern 1, with wide mortar joints of fixed colour, on boxes that stand proud of the render. The result reads as high-contrast tiles.
- **Floors** are one pattern per material: `floor_x` is plank pattern 2 and the ground-floor `terracotta` is a 0.3 m square tile grid. Neither has wear, dirt at the edges, or variety between rooms.
- **Furniture** is authored inside each building's JSON (`furniture` groups) and merged into the building's static parts at load. It is sharp-edged boxes with one grain axis per material and one collider per group. It has no damage: a shell in the kitchen leaves the table untouched.

## 2. Proposal

### Shader patterns (17–28)

Numbers 11–16 are taken: concrete / weather 11, paths 12–14, bunker 15, net 16.

| # | Name (`"pattern"`) | Used by | What it draws |
|---|---|---|---|
| 17 | `limewash` | exterior `render` (inn, roadside office) | Lime render over rubble. Broad tonal drift, fine mineral grain, pits, sparse hairline cracks, uneven coverage. Spalled patches show the rubble behind, with a lighter render edge and contact shading. Damp at the foot, run-off below real window sills. |
| 18 | `brickwork` | `brick` (schoolhouse) | Flemish bond. Cohesive palette with ±8 % per-brick variation and a few darker headers. Mortar is mixed from the brick colour and lime, with a shallow recess. Arrises are slightly chipped. |
| 19 | `ashlar` | `stone_dressed` (quoins, dressings, the municipal building) | The same course and block layout as pattern 1, so breaches still follow the joints (`wallfield.cpp: stoneId`). Fine lime joints close in value to the stone, low block-to-block contrast, tooling striations and edge wear. |
| 20 | `interior` | interior `plaster` skin | Per-room finish looked up in a room table: painted limewash, sprig wallpaper, striped wallpaper, wainscot under paper, wainscot under paint, or bare whitewash. Skirting and dado bands are drawn relative to the room's floor level. Paper fades, its seams lift and it peels near the foot. Wear rises with the building's wear setting. |
| 21 | `floorfin` | `floor_x`, `terracotta` | Per-room floor: worn boards, herringbone parquet, hexagonal terracotta tomettes, stone flags or beaten earth. Dirt gathers at the room's edges, and traffic polish runs along the centre. Ceilings (downward faces) keep plain boards. |
| 22 | `rubble` | exterior `stone` (farmhouse, cottage, workshop, dairy, warehouse) | Pattern 1's layout with muted shading. Mortar is a neighbouring value, recessed, never black. The palette is cohesive. |
| 23 | `rug` | rugs | Part-local woven rug: border bands, a muted field with a medallion or kilim stripes, weave, worn patches. |
| 24 | `ceramic` | crockery, jugs | Glazed faience with a coloured rim band and fine crackle. |
| 25 | `joinery` | furniture wood | Continuous grain along each part's longest axis (`"grain": -1` = automatic), pores, figure, end grain, restrained dents, dirt on the feet. |
| 26 | `paintwood` | painted furniture | Paint over the same grain, worn through on high-noise spots and edges. |
| 27 | `paper` | papers, books | Off-white sheets with faint printed rows and foxing. |
| 28 | `textile` | bedding, curtains | Woven check or ticking stripes (variant in `grain`). |

**Building parameters** need no shader edits. Each building copy uploads a small uniform block:

- `uInst`: a seed taken from the instance id, the wear (0–2) and the palette variation (0–1). These are set per copy in the editor (`extra.finish`).
- up to 16 room rectangles, each with its wall and floor finish codes;
- up to 12 window sills.

Neighbouring buildings therefore share the detail functions but differ in tone and wear, and the variation is fixed by the instance id.

### Furniture family (`tools/gen_furniture.py` → `content/assets/furn_*.json`, category "Furniture")

All furniture is original and parametric. It is built from rounded boxes (`"t": "r"`, 4–10 mm edge radius), so every edge has a small bevel.

- **Tables:** a dining table in two sizes (separate top boards, apron, square tapered legs) and a small side table.
- **Seating:** a ladder-back chair with a rush seat, a slat-back chair and a stool.
- **Storage:** a vaisselier hutch (framed lower doors, drawers, open plate rack or glazed upper doors, crockery on the shelves) and an armoire normande (panelled doors, cornice, feet, handles). A commode-dresser with drawer fronts and handles, and a blanket chest.
- **Bed:** frame, mattress, bolster and folded blanket.
- **Shelves and benches:** a wall shelf with brackets and varied boards, and a bench.
- **Fireplace surround:** stone jambs and mantel, hearth slab, a sooted firebox and fire-dogs.
- **Small props:** a crockery stack, a jug and bowl, bottles, a book stack, a picture frame, an oil lamp, a crate, a paper cluster, two rugs and a curtain pair.

Each asset has a simplified collider of 1–3 invisible boxes. Small props, rugs, papers and curtains have no blocking collision. Each also has a `"prop"` block:

```jsonc
"prop": { "family": "wood|painted|ceramic|glass|cloth|masonry|paper",
          "rounds": 14,          // small-arms hits to break (0 = rounds pass / never break)
          "blast": 40,           // blast energy that breaks it
          "blocking": true,      // simple collider; false = cosmetic (no blocking collision)
          "wall": true,          // templates put its back against a wall
          "front": 0.6,          // access clearance kept in front (m)
          "groups": ["top", "leg", ...],   // parts carry "g": group index
          "fragments": [0, 1],   // groups thrown as rigid fragments when it breaks (at most 4)
          "keep": [3],           // groups left standing in the broken state (static)
          "glass": 2,            // a group that shatters on its own first hit
          "surfaces": [[x0, z0, x1, z1, y]] }   // tops that small props may stand on
```

### Runtime (`src/interior.cpp`, `src/interior.h`; one `InteriorSys interior` member in `App`)

- **Collision.** The prop's collider boxes form an ordinary static body. Built-in box furniture is hidden, in drawing and collision, on a building copy that is furnished by templates.
- **Small arms.** A round deals one hit and then continues through the wood (by the existing penetration rule) or stops in masonry. Glass groups shatter on their first hit.
- **Blasts** (`explode`). Damage falls off over 1.4 × the blast radius. Occlusion comes from rays from the epicentre to the prop's centre and top:
  - a wall, floor or terrain in between shields the prop completely;
  - other furniture shields half.
  - A shell through an open window or door reaches the room behind, because it explodes at the next real surface (the existing projectile rule).
- **Breaking.** The keep groups stay as a static broken mesh. At most four fragment groups become rigid bodies, each with mass from its volume and wood density. Their speed is capped at 7 m/s. Family-coloured particles are added:
  - wood splinters;
  - ceramic: white chips;
  - glass: glints;
  - masonry: dust.
- **Fragment budget.** At most 24 fragments are live at once. A fragment freezes after 6 s, or when it is the oldest over the budget. A frozen fragment keeps drawing but has **no collision**, so debris never blocks a route. At most 160 are kept, and the oldest are retired.
- **No floating furniture.** After every explosion and every prop break, props within reach probe their support (5 short down-rays). A prop that has lost its floor, or the table it stood on, breaks and falls.
- **Session overlay.** Breaks are undone on F5 back to Creative. *Edit → Apply session changes* stores `extra.propBroken`, and *Clear applied damage* removes it.

### Placement and room templates

- **Room detection** runs per asset and is cached. Each storey gets a 0.1 m occupancy grid:
  - storey levels come from the floor parts;
  - walls, panels and invisible colliders between 0.2 and 1.9 m above the floor block cells;
  - door apertures block too, so they separate rooms.
  
  Connected free regions of at least 2.5 m² are rooms.
- **Keep-clear volumes:**
  - every door's aperture plus its leaf swing on both sides;
  - every stair (treads and ramp, plus 0.9 m landings at both ends);
  - every window from its sill upward, 0.6 m into the room;
  - the explore start marker.
- **Manual placement in Creative** (furniture palette). The cursor ray hits real floors, so furniture can be put on upper floors and on tables. A placement is valid when:
  - it has support (5 rays);
  - it does not intersect walls or other solid bodies (`Physics::boxBlocked`);
  - it is outside the keep-clear volumes;
  - it does not overlap other blocking furniture.
- **Room templates** (seeded): kitchen/dining, bedroom, sitting room, utility/storage and workshop.
  - The type comes from the storey, the area, a hearth in the room, and the building kind.
  - Slots place wall items with their backs to the wall, tables in the open with chairs around them, beds away from door swings, rugs flat on the floor (2 mm gap, no z-fight) and props on the furniture's top surfaces.
  - Density and clutter sliders drop optional slots.
  - The furniture is stored as ordinary instances tagged `extra.furnish = {host, slot}`, so save/load, undo, Apply and LAN map transfer need nothing new.
- **Regeneration** in the building's *Interior* panel is deterministic from the seed:
  - items the user moved are kept as overrides;
  - items the user deleted stay deleted;
  - repeated Apply or load never duplicates.
  - The generated pieces follow their building when it is moved or rotated, and are removed with it.

## 3. Status (2026-11-06, branch `interiors-v1`)

### Built

- **Materials.** Patterns 17–28 are in the main shader.
  - `tools/retarget_materials.py` points the building materials at them. The same table is in `tools/parts.mjs`.
  - `brick` is now defined. The schoolhouse walls used it without a definition and fell back to material 0, a flat grey. That is the wall in `wall_current.png`.
- **Furniture.** 27 original assets, placeable from the Furniture category in Creative:
  - dining and kitchen tables, a side table, two chairs, a stool;
  - an open and a glazed vaisselier, an armoire, a commode, a blanket chest, a bed, a shelf unit, a bench;
  - a fireplace surround, a long-case clock;
  - crockery, a jug and basin, bottles, books, an oil lamp, a crate, papers, a picture, curtains and two rugs.
- **Runtime** (`src/interior.cpp`): props, room detection, keep-clear volumes, templates, finishes and the editor panel.
  - In the Selection panel of any enterable building, the *Interior* header has:
    - the wear and palette sliders;
    - the furniture and clutter density sliders;
    - Furnish / Regenerate / New layout / Reset / Remove.
  - A selected piece shows its family, its rating and whether it is a template piece or an override.
- **Capture set.** `NMK_CAPTURE_SET=interiors NormandyMapmaker.exe --capture-materials <tag>` places the farmhouse (with a dirt road), the inn, the schoolhouse and the cottage. It films them from fixed cameras and furnishes the houses.
  - The before / after images are in `docs/interiors/`, downscaled to 1280×720.
  - The "before" set was taken with master 45c4d8f plus only the capture code.

### Shared core touched (all additive)

| File | Change |
|---|---|
| `assets.h/.cpp` | Patterns 17–28 and their names. `"grain": -1` (automatic axis). `Part::grp` (`"g"`). `furnFirst`, plus `noFurnOpaque` / `noFurnGlass` / `noFurnShape` (the building without its built-in furniture). |
| `render.h/.cpp` | `SurfaceParams` and `DrawItem::surf` (uploaded only when the building copy changes). The new patterns and helpers. Patterns 17, 18, 19 and 22 join the weathering list. |
| `app.h/.cpp` | `InteriorSys interior`. In `buildRT`, the static shape comes from `interior.staticShape`. In `buildDrawLists`: the prop draw, the building finish parameters and the no-furniture meshes. `checkPlacement` hands props to `interior` and skips them in the footprint overlap. `interior.sync` after `tanks.sync`, `fragDrawItems`. `PlaceCheck::snapXZ`. The `--selftest-interiors` flag. |
| `damage.cpp` | `interior.blast` after the wall stamp, `interior.reset` in `resetDamage` |
| `ballistics.cpp` | Prop hits: masonry stops the round; wood, glass and cloth go through the normal penetration path |
| `explore.cpp`, `ground.cpp` | `interior.step`, plus `applyToMap` / `clearApplied` |
| `editor.cpp` | The cursor uses a physics hit for furniture. The *Interior* panel. The built-in furniture list is hidden on furnished copies. |
| `capture.cpp`, `selftest*.cpp/.h`, `tools/run_all_tests.sh` | The capture set and the `interiors` suite (stage 2800) |

### Tests: `--selftest-interiors`, 141/141

- **Materials and assets.** Pattern assignments and the brick fix. Finishes sit in the bays' plaster layer (73 of the farmhouse's 115 bays), so holes take them with the wall.
- **Furniture data.** Bevels, colliders, fragment groups and thumbnails for all 27 assets.
- **Rooms.** All ten buildings get rooms and keep-clear volumes. In the farmhouse, the hearth room is the kitchen and both stairs are kept clear.
- **Templates.** Four buildings, two of them turned. Every piece is re-checked with the manual rules, rugs sit 2 mm above the floor and nothing floats. Furniture never cuts off floor that the empty building could reach. Regeneration is deterministic and does not duplicate. Overrides are kept, deleted pieces stay deleted, and a reroll gives a new layout.
- **Placement and editing.** The furniture follows its building through a move, a turn and undo; a duplicated building is furnished on its own; deleting the building removes its furniture. The manual rules refuse:
  - a piece in a door swing or on a stair foot;
  - a piece pushed into a wall;
  - a piece over the stair opening;
  - a picture with no wall behind it.
  
  A rug in a door swing is allowed, and a stool lands upstairs through the cursor ray.
- **Persistence and routes.** Save and reload round-trip. The furnished farmhouse and cottage each pass their own walkthrough, through every door and stair.
- **Damage:**
  - a chair breaks after 8 rounds;
  - 20 rounds on the fireplace all stop;
  - glazed panes go first;
  - HE on the outside of a wall leaves the piece behind it intact, and the same blast inside breaks it;
  - a shell through the open kitchen window explodes inside and breaks furniture;
  - things on a broken table fall;
  - an AP shot up through the floor brings down the piece above it, and nothing floats;
  - 30 crates in one blast peak at 24 live fragments and 75 kept, thrown at most 7 m/s;
  - all fragments sleep within 9 s and keep no collision;
  - F5 resets, Apply stores `propBroken`, Clear and undo work.
- **Performance** (the kitchen view, built-in vs furnished vs during a blast) is recorded in `summary_interiors.json`. The GPU is shared with other running copies of the game, so absolute numbers drift: 15.1 / 15.5 / 15.6 ms in a quiet run, 48.9 / 50.1 / 40.9 ms in a busy one. Furnishing adds about 5 draws and 0.26 M triangles to that view. Templates generate in 1–6 ms per building.

### Missing or limited (honest list)

- **Materials are procedural shader patterns, not texture maps.** There are no albedo, normal or roughness files, so the spec's texel-density and colour-space checks don't apply. The detail is analytic: fine grain, bump from the height, and a darkened AO term. The look is improved over "before" but is not photographic reference quality.
- **Lighting is unchanged.** Interiors are still dim; window light, ambient fill and shadow bias were not retuned. That is a global change that affects every suite's look, so it is left for a separate pass.
- **Wainscot, skirting and dado** are drawn in the shader on the plaster layer. They are not geometry, so they have no real relief.
- **Room shapes are bounding rectangles** for the finishes. L-shaped rooms take the first matching rectangle.
- **The template stock is the furniture family above.** Specialised built-ins (the inn's bar, school desks, workshop benches) are hidden when a building is furnished by templates, and only generic pieces replace them.
- **Fragments** are the listed rigid pieces plus particles. No mesh is cut at the impact point.
- **Breaks replicate over LAN** only through the replicated explosion events. A late joiner sees applied breaks (`propBroken`) but not session breaks.
- **The CC-BY furniture GLBs** in `docs/assets-ai-lan.md` are not used.
- **Grass bending under tanks** (the first half of V1) is still open.

## 4. V4: furniture everywhere, 3x density, destructible (2026-11-07, branch `vo-4-furniture`)

User item 0b of `docs/plan-visual-physics-overhaul.md`: keep the look of the furniture, but put it in every building
automatically, at least three times as dense, and let shells, AT rounds, rockets, grenades and tanks destroy it.

### Automatic furnishing

- **When.** `InteriorSys::autoFurnishAll` runs after `syncRuntime` in `App::openMap` and after `w5NewMapContent` in
  `App::newMap` (so also the New Map dialog and every template). `App::placeInstance` calls `autoFurnish` for the new
  building (`interior.autoOnPlace`, on by default; tests switch it off to keep a built-in baseline).
- **Which buildings.** Every enterable asset with rooms whose copy has no `extra.interior` with an `on` key. Skipped:
  `bunker_*`, `aw_*`, `bch_*` (and the Bunkers / Atlantic Wall / Landing craft / Beach categories), ruined or
  half-built assets (`isRuinedAsset()` in `interior.h`: `ruin_*`, `rub_*`, `vil_church_ruined`, `vil_chateau_keep`,
  `aw_r612_unfinished`, `bch_lcvp_wrecked`; the lighting stream reuses the list), anything tagged `extra.decor`
  (V8's border buildings), and a building whose template furniture the user removed (`on: false`).
- **Deleting** a building (`deleteSelected`, build-mode Del) removes its template furniture at once; build-mode
  Ctrl+Z after placing a building removes the furniture that came with it.
- **Ground cover.** Template furniture is not a grass / nature-patch footprint and has no foundation soil band: it
  stands inside its building's footprint.
- **Routes stay clear.** Both the asset's `walkthrough` and its structures-suite `tour` keep a 0.9 m corridor free of
  template furniture (church nave, Norman farmhouse), and so do the doorway rings. Solid wall-hung pieces (wall shelf,
  plate rack: head height) keep out of them too; pictures and other cosmetic wall pieces may hang beside a route.
  Pews are 2.1 m and stand in a column against each side wall. A wall shelf with no wall left becomes a picture or a
  portrait, and lived-in rooms get one or two more of those wherever a wall is free.
- **Not an edit.** No undo step is pushed and the dirty flag is restored, so opening a map and closing it again asks
  nothing. The pieces are ordinary instances (`extra.furnish`), so a saved map keeps them and is not furnished twice;
  a built-in example opened as a copy is furnished the same way every time (the seed comes from the host id).
- **Room detection.** Earth floors and thin dressed-stone floors now count, which gives the stone barn, the church
  nave, the chateau keep's ground floor and the ruined church their rooms. New room kinds: church nave, barn, classroom,
  office, tavern room and ruined room (a `ruin_*` asset furnished by hand from the Interior panel), next to kitchen,
  sitting room, bedroom, utility, workshop and hall.

### Density

- **29 new furniture assets** from `tools/gen_furniture.py` (56 in all), the same rounded-box construction and
  materials as the first set:
  - large: buffet bas (dresser), sideboard, bookcase, washstand, cooking range, pantry shelves with jars, grain sacks,
    cask, casks on a cradle, workbench, armchair, writing desk, church pew, school desk, firewood stack, hay bales;
  - wall-hung: coat pegs, wall clock, utensil rack, tool board, mirror, portrait, and a wall shelf and a plate rack
    that carry small things (their board is a thin collider, so what stands on them is supported);
  - small: preserving jars, bucket, basket of apples, cooking pot, candlesticks.
  - `"metal"` is a new prop family (range, pot, utensils, candlesticks): rounds stop on it, like masonry.
- **Recipes** (`Planner::room`): more wall slots per room, clutter on every top, corner pieces (casks, sacks,
  baskets, buckets, firewood), and wall-hung pieces that may hang above a dresser or a bench (they keep out of door
  swings, stairs and windows). Churches get rows of pews toward the end away from the door, with an altar table and
  candlesticks; classrooms get rows of twin desks facing the teacher's desk; taverns several tables, casks and a
  sideboard; barns hay, sacks, casks, crates and tools; ruins a few pieces, most of them broken.
- **Density range.** Furniture density 0–2 (default 1.0, was 0–1 with 0.7), clutter 0–2 (default 0.7).

### Destruction

- **Every shell already reaches the furniture** through `App::explode` → `interior.blast` (tank HE / AP, PaK HE / AP in
  `station.cpp`, rockets, grenades, LAN replays). New in `blast`:
  - a burst within 0.15 m of a piece (the shell struck it) is a **direct hit**: the piece is blown apart whatever its
    rating (masonry excepted);
  - **grenades** count 2.2x against furniture (fragmentation shreds wood); a grenade on a kitchen floor takes out
    nearly everything within 3 m;
  - a grenade, a direct hit or a blast at 2.5x the rating is a **violent break**: all fragment groups fly (up to six,
    was four), faster spin, wood splinters / faience chips / glass shards (`Fx::splinters`) and a dust cloud
    (`Fx::dust`); a grenade also leaves the room dusty;
  - support checks run once at the end of a blast instead of after every break.
- **Fragment budget.** 32 live / 240 kept rigid fragments (was 24 / 160) times the particle budget of V2's graphics
  preset (`settings().particleBudget()`, picked up automatically through `__has_include("settings.h")`; 1.0 before V2
  merges). Over the live budget a new fragment is not spawned (its share goes to the splinters) unless the oldest live
  one has had 0.8 s to settle, so nothing freezes in mid-air.
- **Tanks** (`tank_ram.cpp`, furniture branch only): furniture never blocks a tank. On contact a piece is broken
  violently, its remains lose their collision and draw flattened; fragments are driven over.

### Drawing

- Near a building (within 18 m of its furniture's bounds) every piece draws on its own with full detail from the
  shared asset meshes. Farther away the building's intact pieces draw as **one merged box-LOD mesh** (rounded and chamfered boxes as
  plain boxes, round turnings as hexagonal prisms, glass and tiny parts dropped), small props only to 32 m, nothing beyond 75 m. A piece that breaks, is
  moved or selected leaves the batch, which is rebuilt the next frame (`InteriorSys::updateBatches`, from `sync`).
  Hand-placed furniture outside a building is never culled.
- **Per frame**, `updateBatches` and the Creative follow / override pass in `sync` return at once unless the edit
  revision, the instance count, the selection or a furniture state (a break, glass, a crush, a reset) changed. Without
  that, a map with ~1,200 pieces (Omaha: 28 buildings) spent about 10 ms a frame in JSON lookups.
- **Load time.** Furnishing swaps the building's static collision to its no-built-in-furniture shape in place instead of
  rebuilding the whole building, and the planner probes each wall face once per cell (cached).

### Shared core touched (V4, one-line hooks)

| File | Change |
|---|---|
| `app.cpp` | `interior.autoFurnishAll` after `w5NewMapContent` in `newMap` and after `syncRuntime` in `openMap`; `interior.autoFurnish` at the end of `placeInstance` |
| `tank_ram.cpp` | Furniture branch: `interior.tankContact` for a piece, fragments driven over; never a blocker |
| `fx.cpp`, `combat.h` | `Fx::splinters`, `Fx::dust` (appended) |
| `selftest_maps.cpp` | Instance counts compare the authored instances (pieces with `extra.furnish` are added on open / new map) |
| `selftest_damage.cpp` | The two walk-through farmhouses are unfurnished (their breach routes are authored for the bare house); the pre-update map counts authored instances |
| `docs/asset-usage-default-maps.md` | Rows for the new furniture |

### Tests (`--selftest-interiors`, 2026-11-07)

New checks, next to the 141 of the first set:

- **Auto-furnish.** The four fixtures arrive furnished on placement, one undo step each, and regenerating gives the same
  layout. Every enterable asset is placed once: all 15 houses, barns, churches and civic buildings are furnished,
  all 16 bunkers / Atlantic Wall / landing craft are left alone, every `vil_*` building has rooms, and the two
  churches have 16 pews. `extra.decor` and a building whose furniture was removed stay bare. Opening a saved map with a
  bare Norman farmhouse furnishes it with no undo step and no dirty flag; so does a new map from battle1 (19
  buildings, 769 pieces, 0.7 s).
- **Density gate (x3).** The four fixtures: 164 pieces vs 45 with the first templates (x3.6). Every building: 548 vs
  163 (x3.4; the exact count moves a little with the instance ids, which seed the layouts). Lowest single building:
  the schoolhouse at x2.4.
- **Grenade in a kitchen.** An M24 on the kitchen floor of a furnished farmhouse blows apart at least 70 % of the
  pieces within 4 m (7 of 8 in the last run, 46 pieces in all), with rigid fragments, splinter bursts and dust.
- **Direct hits.** A 75 mm tank HE shell, a PaK AP shot and a rocket each strike a piece in the open: all three are
  blown apart as direct hits.
- **Tanks.** A Sherman drives at a table, a dresser and an armoire, crushes all three and drives on.
- **Performance.** Kitchen view: built-in 8.3 / 8.7 ms, furnished 8.7 / 9.6 ms, during a room blast 8.9 / 9.9 ms in two
  runs (the first set measured 62 ms mean / 260 ms p95 during that blast, mostly repeated support checks). The `maps` FPS gates hold
  with every building furnished (Omaha worst eye-level view 14.6 ms; battle1, reported only, 15.7 ms).

`NMK_INTERIORS_QUICK=1` runs only the furnishing steps (for tuning recipes; the furnish log then lists every placed
slot and the slots that found no room).

### Limits (V4)

- **Furniture behind a breach blocks the way in.** A piece standing against a wall stays there when the wall is
  breached from outside (the wall shields it from that blast); a soldier then has to break or blast it.
- **Some rooms stay sparse.** Rooms crossed by an authored walkthrough route, with doors and windows on most walls
  (the farmhouse sitting room, the municipal offices), have little wall left: they get small and wall-hung pieces.
- **Opening a large map takes longer.** battle1 (19 buildings, 769 pieces) 0.5–0.9 s; Omaha (28 buildings, 1,169
  pieces) about 2.6 s, where each building costs more as the map fills (not profiled further).
- **The pieces are saved with the map.** Undo snapshots and LAN map transfers carry them like any other instance.
