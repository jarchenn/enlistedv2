# Lasso tool (Creative) + swimming — plan

## Context
The user wants a **Lasso tool** in Creative mode: draw a freehand lasso or a circle over the map, and everything inside is selected and can be worked on. Today the editor has a single `uint64_t selected` (`src/app.h:109`) and no multi-select. Terrain edits exist only as V8's round sculpt brush, ground colour is purely procedural (there is no saved per-texel ground type), and water is one global `terrain.waterLevel` plus V1's asset water surfaces (batch B).

User decisions (2026-11-07):
- Add a fourth mode, **Move land**.
- **Download 4–5 extra ambientCG sets** for the ground types that have none.
- **Swimming goes to a parallel session**, started right after this plan is approved.
- **Drowning is on**: 30 s of breath.

Setup:
- Worktree `nmk-lasso`, branch `lasso-tool`, cut from `vo-batch-c` (f4d653f). Build dir `%TEMP%/nmk/build-lasso`.
- Merge new `vo-batch-c` commits (V6/V8 ground-restore fixes) as they land.
- Never push. Master is the user's call; message coordinator `local_a807b82d…` when ready.

## Shared UI
- New `Tool::Lasso` (`src/app.h:17`) with a toolbar button (pattern: `terrainToolbar`, `src/terrain_brush.cpp:351`) and hotkey **L**. A panel holds the mode tabs **Objects | Elevation | Paint | Move land** and the shape choice **Freehand | Circle**.
- **Capture:**
  - Freehand: LMB drag, sampling a point every 6 px and ray-casting it to the ground (`App::cursorRay` + `Terrain::raycast`). The polygon closes on release, is simplified (`simplify.cpp`), and self-intersections are tolerated (even-odd test).
  - Circle: LMB press = centre on the ground, drag = radius, giving a 64-gon on the ground.
  - Modifiers: Shift adds, Alt subtracts; Esc or RMB clears.
- **Selection storage:** the region is kept as a world-xz polygon set (`LassoRegion`) and drawn draped with `ov.line` (`editor.cpp:115` pattern). Editor-only: it is not in `mapToJson`, so the map hash is unchanged.
- **2-D geometry:** a new `src/geom2d.h` holds point-in-polygon, signed inside distance and the bbox. Move `insideDistance()` out of the anonymous namespace in `terrain_cuts.cpp:14-24` and include it from the new header.
- **Undo:** every action calls `pushUndo()` exactly once. A cancelled or rejected action pops that step, as the move reject does at `editor_nav.cpp:437`.

## Mode 1: Objects
- **Selection:** `std::vector<uint64_t> selection` in `App`, with `selected` = primary. It is added to `Snapshot` (`app.h:67`) for undo.
  - Hit test: an instance is in when its pivot is inside the region. Option "touching" = any footprint corner inside.
  - Furnish children (`extra.furnish.host`) are excluded; they follow their host (`interior.cpp:1088`).
- **Group move:** in `edSelectPointer` (`editor_nav.cpp:373-443`), pressing on any member starts a group drag that applies the ground delta plus grid snap to all members.
  - Each member re-seats through a new `checkPlacement` overload that ignores the whole group (today it takes only one `ignoreId`, `app.cpp:346-406`).
  - If any member is invalid, the ghost shows red. On release a rejection restores all members and pops the undo step. On success, `crushAndFlatten` runs per member, with ids looked up again because it erases instances.
- **Rotate about the centroid:**
  - Z/X and the wheel while dragging rotate by the 15° snap; Shift gives free rotation.
  - Positions rotate around the group centroid and each `yaw` gets the same delta.
- **Delete:** Del removes the group in one step.
- **Duplicate:** Ctrl+D copies the group with its layout, offset by the group's bbox width (trying 8 offsets as `duplicateSelected` does, `app.cpp:439`). The selection becomes the copies.
- The properties panel shows the count, and per-asset counts.

## Mode 2: Elevation (+ water fill)
- **Panel:**
  - Amount, −40…+40 m.
  - Falloff width, 0–60 m, applied **outside** the lasso, so the whole selection moves by the full amount and the ring blends into the surrounding ground.
  - Profile: **Plateau** (flat top or floor) or **Dome** (a rounded hill or bowl, weighted by normalised inside distance).
  - Edge curve: smooth or linear.
  - Buttons: Apply / Cancel.
- **Weight:** `w = smoothstep(0, falloff, falloff + signedInsideDist)` × profile.
- **Write path:** reuses V8's read/write loop on the global fine grid (`terrain_brush.cpp:86-153`), so tile borders never crack: `T.sculpt` → `sculptChanged(keys)` → `terrainReseat` (objects ride the ground) → `terrainCommitSculpt` → `++groundRev`. Values are clamped like the brush.
- **Live preview:** a slider change re-applies the edit from a stored copy of the touched tiles; only touched tiles are re-meshed (a 100 m region is about 49 tiles). Apply keeps the single undo step; Cancel restores the copy and pops it.
- **Fill with water to the lip:**
  - Shown as a checkbox when Amount < 0, and as a "Fill hollow" button for any region.
  - Spill height comes from a priority flood on the 0.448 m fine grid:
    1. Seed it from the lowest point inside the region.
    2. Pop cells in height order and track the running maximum.
    3. The first popped cell lower than the running level is the spill point; that level is the lip.
  - The flood is bounded to the region bbox plus 80 m. If it reaches the window edge or the global sea, it is refused with the message "no closed hollow".
  - Lake = the visited cells below the lip. Its outer boundary is traced into one simple polygon, simplified, and grown by 0.5 m; islands are left to poke through the plane.
  - Stored as `doc.waterBodies[] {id, outline, level, kind:"pond", seed}`, in the V1 surface schema. It is saved in `mapToJson` (so it enters the map hash and LAN check) and in `Snapshot`.
  - A "Remove water" action deletes the bodies whose seed is inside the region. "Refill" recomputes the lip after later terrain edits.
- **Query:** `float lassoWaterAt(const App&, x, z)` in `src/lasso_water.h`, which the swim stream's `waterAt` picks up. Grass (`grass.cpp:155`) and nature (`nature.cpp:533,558,579`) exclude water bodies.
- **Rendering:** a small hook in V1's `water.cpp` pushes `WaterDraw` entries for `doc.waterBodies`: the polygon is ear-clipped at `level` and cached by body id and revision. It also extends `waterSurfaceAt`.
  - Wired after batch B is in this branch's base. Until then, a draped outline plus a flat translucent debug sheet stands in.

## Mode 3: Paint (ground types)
- **Types**, in data-driven `content/ground_paint.json`. 0 = default grass, which also acts as the eraser. The other 12:
  - marsh/shallow water
  - dirt
  - mud
  - gravel
  - sand
  - cobbles
  - rock
  - dry grass
  - ploughed farmland
  - leaf litter
  - scorched earth
  - pebbles

  Each entry carries a procedural colour/roughness, an L4 texture slot, a grass factor, a nature-suppress flag, a nav area and a footstep event.
- **Data (`src/ground_paint.{h,cpp}`):**
  - World-anchored 16 m chunks of 32×32 texels (0.5 m). Texel RGBA8 = top type, top weight, under type, under weight. A partial stroke over existing paint keeps the old type underneath, so feathers between types blend.
  - Saved as `groundPaint {texel, chunk, chunks:[{c:[cx,cz], d:base64(RLE)}]}` in `mapToJson`; part of `Snapshot` and the map hash.
  - World anchoring means Beautify's ring growth never needs to remap it.
- **Brush on the lasso:** type, strength (opacity) and feather 0–10 m, with `w = smoothstep(0, feather, insideDist) × strength` and a noise-jittered edge. One undo step.
- **GPU and shader:**
  - One RGBA8 texture over the terrain extent (capped at 4096²), on texture unit **23** (units 14/15 are terrain, 16–19 L2, 20–22 L4).
  - Sub-image uploads cover dirty chunks only.
  - In `kTerrainFS`, one call `paintApply(p, alb, rough, N, ao, wet)` is inserted after the track and slope stages, before wet/puddles. It does a manual 4-tap bilinear fetch that accumulates weights per type, and evaluates only types above 1 %.
  - The function lives in its own GLSL string, so the splice into L4's shader is about three lines.
  - Each type has a procedural material (furrows for ploughed, char and ash for scorched, standing-water gloss for marsh) that works with `--no-textures` and `--legacy-look`.
  - When L4 is present, each type also calls `terrTex(slot)`.
- **L4 coordination** (session `local_4d1e511e…`):
  - Ask L4 to add `paint_*` slots to `textures.json` and raise the `uTerrSlot` count.
  - Proposed sets:
    - marsh → `mud_wet`
    - dirt → `soil_brown`
    - mud → `mud_soil`
    - gravel → `gravel_path`
    - sand → `sand_beach`
    - cobbles → `cobbles_old`
    - rock → `rock_wall`
    - dry grass → `grass_lawn` straw-tinted
  - **New downloads** (user-approved in principle) for leaf litter, ploughed field, scorched earth and pebbles, plus one spare. They go through L4's `tools/fetch_ambientcg.py`, 1K-JPG only.
  - Before downloading, a dry run lists the exact ambientCG ids, file names and sizes for confirmation (about 40–50 MB download, about 10 MB packed).
  - Until L4 merges, paint runs procedurally.
- **Gameplay hooks:**
  - `paintAt(x, z)` on the CPU.
  - Grass density × the type's grass factor; the grass chunk signature includes `paintRev` (`grass.cpp:166-201`).
  - Nature patches are rejected on suppressing types (`nature.cpp:518-600`).
  - Nav area: marsh/mud → `NavArea::Marsh` cost 3 (`nav.cpp:115-128`, `navmeta.cpp:5`).
  - Footsteps: gravel, mud and sand map to bank events where they exist (`explore.cpp:353-368`).

## Mode 4: Move land
- Draw a lasso, then drag inside it. A ghost outline follows the ground and Z/X rotates it in 15° steps; release commits as one undo step.
- **Heights:**
  - Membrane fill: a Laplace solve (Gauss-Seidel) on the fine grid inside each polygon from its rim heights.
  - `rel = H − membrane(srcRim)` is the patch's own shape.
  - The source becomes `membrane(srcRim)`; the destination becomes `membrane(dstRim) + rel`.
  - Both are blended with the same outside falloff as Elevation, computed from a pre-move copy so overlapping moves are correct.
  - Written as sculpt (`target − baseHeight`), with crater deltas kept.
- **Carried along:** instances inside move with the same transform and are re-seated; paint texels are resampled; water bodies inside move and their level shifts by the height change at their seed. Move land shares the Objects-mode group-placement validator.

## Swimming (parallel stream, started right after approval)
A new session in worktree `nmk-swim`, branch `swim`, from `vo-batch-c`. Its brief:
- **Water query:** `waterAt(app, x, z)` in `src/water_query.{h,cpp}` = max of the global `waterLevel`, V1 `waterSurfaceAt` (`__has_include("water.h")`) and `lassoWaterAt` (`__has_include("lasso_water.h")`).
- **State and movement:** `src/swim.{h,cpp}` follows the mantle pattern (`mantle.cpp`, hook beside `explore.cpp:322`).
  - Enter at chest depth (about 1.25 m); leave below 1.05 m when grounded.
  - Movement goes through a `Physics::updateSwim` variant: gravity zeroed, buoyancy toward the surface, drag. That keeps Jolt collisions.
  - Space rises, Ctrl dives, and look pitch steers when submerged.
  - Mantle out onto banks.
- **First-person look:**
  - Weapon lowered; fire, aim and reload stripped (`explore.cpp:296`, `viewmodel.cpp:96`).
  - A hands-only arm path in `armDrawItems` with a procedural breaststroke.
  - Surface camera bob and roll.
- **Underwater view:** `RenderView::underwater` drives dense blue-green fog (`setLighting`, `render.cpp:2086`), a tonemap tint and vignette (`:1760-1789`), and muffled audio.
- **Breath:** 30 s, then damage; a breath bar. Values in `content/movement.json`.
- **Third person and LAN:**
  - Swim pose in `pose.cpp` (prone-like tilt, stroke, kick).
  - Net: flags `2|4` = swim, and client posture 3 is accepted (`net.cpp:270,312,539`).
- **Bots:** nav marks local deep water forbidden (water bodies as well as the global level, `nav.cpp:121-128`). A bot pushed into deep water floats (`physics.cpp:600`).
- **Tests and docs:** `--selftest-swim`, docs in `docs/movement.md`. Its handoff note goes to the shared notes folder.
- The only shared contract is `lasso_water.h`, which I commit first, within the first step.

## Files (lasso stream)
| | Files |
|---|---|
| **New** | `src/lasso.{h,cpp}` (capture, region, group ops, elevation, move land), `src/lasso_water.{h,cpp}` (flood, outline, bodies), `src/ground_paint.{h,cpp}`, `src/geom2d.h`, `src/selftest_lasso.cpp`, `cmake/lasso.cmake`, `content/ground_paint.json`, `docs/lasso.md` |
| **Edited** | `app.h/.cpp` (Tool, selection, Snapshot, `checkPlacement` overload), `mapdoc.h/.cpp` (`groundPaint`, `waterBodies`), `editor.cpp`, `editor_nav.cpp`, `terrain_cuts.cpp`, `terrain.cpp` (bind unit 23), `render.cpp` (`paintApply` splice), `grass.cpp`, `nature.cpp`, `nav.cpp` (paint areas), `explore.cpp` (footsteps only), `water.cpp` after B, `CMakeLists.txt`, `tools/run_all_tests.sh`, `tools/run_fast_tests.sh` (a `lasso` rule; `lasso|ground_paint` added to the editor and terrain rules), `docs/testing.md`, `README.md` (controls) |

## Order of work
1. **L0:** worktree, branch and build. `lasso_water.h` stub committed (the swim contract).
2. **L1:** capture, region and overlay; `geom2d`.
3. **L2:** Objects mode.
4. **L3:** Elevation with live preview.
5. **L4w:** water fill and bodies, and their save/undo; grass and nature exclusion.
6. **L5:** Paint (procedural first), then L4 slots and the downloads, after confirming ids and sizes.
7. **L6:** Move land.
8. **L7:** `--selftest-lasso`, docs, handoff. Then wire water rendering as soon as batch B is in the base.

## Verification
`--selftest-lasso` (registered like `terrainSelfTest`, `selftest_terrain.cpp:420`, via `MpTest` steps) runs on `blank_slate` plus a scripted bowl:
- **Capture:**
  - Freehand and circle select exactly the expected instances, with add and subtract.
  - Furnish children are excluded.
  - The selection never changes `Net::mapHash`.
- **Objects:**
  - A group move keeps relative offsets (< 1 mm).
  - Rotating 90° about the centroid gives the analytic positions and yaws.
  - An invalid move restores everything.
  - Delete, duplicate, move and rotate each add exactly one undo entry and are fully undone.
  - Save/reopen round trip.
- **Elevation:**
  - +5 m inside gives +5 ± 0.01 m.
  - Beyond the falloff, nothing changes.
  - The profile is monotonic, there are no cracks at tile seams, objects are re-seated, and undo is exact.
- **Water fill:**
  - A carved bowl with a notch at a known height gives the lip = notch height (± 2 cm).
  - The outline contains the seed and `lassoWaterAt` = level inside and none outside.
  - An open basin is refused; save, undo and the hash change are checked.
  - After B: a water draw item exists, plus a screenshot.
- **Paint:**
  - All 12 types plus the eraser.
  - `paintAt` gives weight 1 inside and a monotonic feather; byte-exact save/load; the hash changes.
  - Grass density is 0 on cobbles; nature patches are suppressed; marsh is in the nav area.
  - The shader compiles with textures on and off; one screenshot tile per type, not black and mutually distinct (`selftest_out/lasso/`).
- **Move land:**
  - The destination shape equals the source shape relative to the membrane (± 2 cm).
  - The source is smooth (bounded slope); objects, paint and water are carried; one undo step.
- **LAN:** two docs loaded from the saved file hash equal, and differ after any lasso edit.

Run through `tools/gpu_turn.sh`:
- the fast path, `tools/run_fast_tests.sh $T/build-lasso vo-batch-c`;
- then explicitly `lasso`, `editor`, `terrain`, `nature`, `maps` (FPS gate with paint on) and `move`;
- with `--selftest-swim` added after merging the swim branch.

The full `tools/run_all_tests.sh --net` runs before handing to the coordinator. Done = all of these pass, docs (`docs/lasso.md`, `testing.md`, README) are updated, and a handoff is written in `docs/plan-visual-physics-overhaul.md` style plus the shared notes folder.
