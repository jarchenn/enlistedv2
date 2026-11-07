# Lasso tool

The **Lasso** tool (toolbar button, or **L**) works on a region you draw on the map:
- a freehand loop;
- or a circle: press at the centre, drag out the radius.

What you do with the region depends on the mode in the Lasso panel. Every action is one undo step (Ctrl+Z).

The region itself is editor state: it is never saved and never enters the map hash. What the actions produce is saved in the map, so LAN games, which match maps by hash, replicate it:
- placements;
- sculpted ground;
- water bodies;
- ground paint.

| Input | Effect |
|---|---|
| LMB drag | Draw a region (replaces the old one) |
| Shift + LMB drag | Add to the region (Objects: add to the selection) |
| Alt + LMB drag | Cut from the region (Objects: remove from the selection) |
| Click | Objects: select the one object under the cursor (Shift adds, Alt removes); elsewhere clears |
| Backspace | Clear the region |
| RMB / MMB | Orbit / pan, as everywhere in Creative |

Code:
- `src/lasso.{h,cpp}`: the region, groups, elevation, the water-fill entry points.
- `src/lasso_ui.cpp`: input, overlay, panel.
- `src/lasso_water.{h,cpp}`: spill height and outline; drawing water bodies.
- `src/ground_paint.{h,cpp}`: paint.
- `src/lasso_land.{h,cpp}`: move land.
- `src/geom2d.h`: polygon helpers.
- `src/selftest_lasso.cpp`: the `--selftest-lasso` suite.

## Objects

The selection is every placement whose pivot is inside the region; "Touching counts" also takes any whose footprint corner is inside. Generated furniture (`extra.furnish`) is never selected: it follows its building.

| Action | How |
|---|---|
| Move the group | Drag any selected object. Members keep their layout and each one re-seats on the ground. Grid snap (G) snaps the offset. Z / X turn the group while dragging |
| Turn about the centre | Z / X: 15° per press with rotate snapping, continuous while held without it. The panel's ±15 buttons do the same |
| Duplicate | Ctrl+D or the panel. The copy goes beside the group (it tries 12 offsets), and the copies become the selection |
| Delete | Del or the panel |

How the group behaves:
- **Rejected moves:** if any member cannot stand at its new spot, the group draws red. On release, everything goes back and no undo step is left.
- **Crushing:** buildings crush low growth at their new spot (`crushAndFlatten`), as single moves do.
- **Placement checks:** `App::placeIgnore` makes `checkPlacement` ignore the other members, so they never collide with the group's old positions.
- **Other tools:** the group stays selected (orange boxes, a centre marker) when you switch tools. Del and Ctrl+D act on the whole group (`App::deleteSelected` / `duplicateSelected` route to the lasso). A click on one object in Select replaces the group.

## Elevation

Drawing a region starts a live edit. Panel controls:

| Control | Range / options |
|---|---|
| Raise / lower | −40 … +40 m |
| Falloff width | 0–60 m. The ring lies outside the region: the whole region moves by the full amount, and the ring blends into the untouched ground |
| Profile | **Plateau** (flat top or floor) or **Dome / bowl** (rises all the way to the middle) |
| Edge | Smooth (smoothstep) or linear |

The edit is previewed until **Apply** (Enter) or **Cancel**. Drawing a new region discards an edit that was not applied.

Implementation:
- **Weight:** with `sd` the signed distance to the region (positive inside):
  - plateau: `t = (sd + F) / F`;
  - dome: `t = (sd + F) / (F + max sd)`.
- **Write path:** the same as V8's sculpt brush, which keeps tile borders from cracking:
  1. `Terrain::sculpt` on the global fine lattice;
  2. `sculptChanged` re-meshes the touched tiles;
  3. `terrainReseat` makes placements ride the ground;
  4. `terrainCommitSculpt` writes it to `MapDoc::terrainSculpt`.
- **Previews** re-apply from the tiles' pre-edit sculpt, never on top of the last preview, and are throttled to about 8 per second.

### Water fill

**Fill with water up to the lip** (when lowering) or **Fill hollow with water** (any region) floods the hollow from the lowest ground point inside the region. It is the same undo step as the lowering. This is `lassoComputeFill` in `lasso_water.cpp`:

1. **Priority flood.** On the 0.448 m fine lattice, the lowest frontier sample is popped first, and the running maximum is the water level reached so far.
2. **Spill.** The first popped sample that is outside the region and lower than that level lies past the rim. The level at that moment is the **lip**: the lowest point on the hollow's rim.
   - Lower pockets inside the region belong to the hollow, because a lowered floor is never perfectly flat.
   - If you lasso only part of a natural hollow, the parts outside the lasso count as "past the rim".
3. **Refusals.** The flood is refused with a toast when:
   - it reaches the search window (region + 80 m) still climbing ("no closed hollow");
   - the hollow drains into the template sea;
   - the hollow is less than 5 cm deep.
4. **Outline.** The lake is the samples below the lip that connect to the seed, grown one sample into dry ground. Islands are filled in (they poke through the flat surface). The boundary is traced along the cell edges and simplified (Douglas-Peucker, 0.2 m).

Bodies are saved as `MapDoc::waterBodies`:
- **Format:** `"waterBodies": [{id, kind: "pond", level, seed: [x, z], outline: [[x, z], ...]}]`, in millimetres.
- **Overwriting:** a new fill over an existing body replaces it.
- **Remove water:** deletes the bodies whose seed is inside the region.
- **Queries:**
  - `lassoWaterAt(app, x, z)` gives the surface height, or −1e9 where there is none. It is the swim stream's contract.
  - `lassoWaterAtCurrent(x, z)` is the same for code without an `App`: grass, nature patches.
- **Rendering:** with V1's water pass in the build (`water.h`, batch B), `lassoWaterDrawItems` feeds each body to it as a flat pond. The mesh is ear-clipped once per outline and level. Before that, Creative shows a translucent sheet at the level.

## Paint

Twelve ground types over the default grass:

| | | | |
|---|---|---|---|
| marsh / shallow water | dirt | mud | gravel |
| sand | cobbles | rock | dry grass |
| ploughed farmland | leaf litter | scorched earth | pebbles |

**Grass** erases. Each shape you draw is painted at once, with these settings:
- **Strength:** opacity, 0.05–1.
- **Edge blend:** 0–10 m. The weight is a smoothstep across the outline, and a noise-jittered edge keeps it from following the polygon exactly.

**Paint region again** repaints the current region with a new setting.

### Map format

`"groundPaint": {"texel": 0.5, "chunk": 32, "chunks": [{"c": [cx, cz], "d": "<base64>"}]}`:
- **Chunks:** world-anchored 16 m chunks of 32 × 32 texels (0.5 m), so the outer-ring growth never remaps them.
- **Texels:** each is RGBA8: top type, top weight, the type underneath, its weight. A soft stroke over existing paint keeps the old type underneath, so both show across the seam.
- **Encoding:** `d` is a run-length encoding of the texels (count, r, g, b, a), then base64.
- **Undo:** paint is part of the undo snapshot. `MapDoc::paintSig` (runtime) changes with every edit.

### Shading

- **The paint texture:** `groundPaintSync`, called from `App::updateSurface` every frame, mirrors the chunks into one RGBA8 texture over the terrain.
  - It uses texture unit 23, at 0.5 m texels, coarser only on terrain over 2 km.
  - Only the chunks that changed are uploaded.
- **The shader function:** `paintApply(...)` comes from `groundPaintGlsl()`, which is prepended to `kTerrainFS`. It is called once, just before `shade()`.
  - It fetches the four surrounding texels by hand, with a jittered position so no texel grid shows, and accumulates per-type weights.
  - Types below 1 % are not evaluated.
  - Crater soil shows through the paint.
- **Materials:** each type has a procedural material (`paintMat`): cobble setts with joints, furrows, pebbles with a domed normal, ash patches, standing water in marsh lows. It needs no textures.
  - L4's ambientCG layers can be added per type through `terrTex` once L4 merges.
  - Coordination note: L4 owns the `kTerrainFS` material layers. The splice is these two lines plus the prepend in `Renderer::init`.

### Gameplay

| What | Effect |
|---|---|
| Pasture grass | `Grass::density` keeps the type's `grass` share. Cobbles, gravel and the like have none; dry grass keeps 55 %. Grass chunks re-check when paint or water changes and rebuild only where it changed (`lassoGroundSig`) |
| Nature patches | Flowers, tall grass, stones and wheat skip types with `natureOk: false` and water bodies |
| Navmesh | Marsh and mud mark plain ground spans as `NavArea::Marsh` (cost 3) |
| Footsteps | The type's `step` event plays, when the audio bank has it |

The per-type values are in `content/ground_paint.json`.

## Move land

1. Draw a region.
2. Drag inside it. A gold outline shows where the patch lands, and Z / X turn it in 15° steps.
3. Release to move it.

`Blend width` is the falloff ring. The model (`lasso_land.h`):
- **Relief:** `relief = ground − membrane(region grown by the blend width)`. The membrane is the smooth Laplace surface through the ground around the grown region (SOR Gauss-Seidel). The relief is the patch's own shape: hills, hollows, banks.
- **Source:** `ground − w · relief`. The old spot is smoothed back into its surroundings.
- **Target:** `mix(ground, membrane(target rim) + relief, w)`. The patch replaces what was there, on the target's own rim height.
- **Weight:** `w` is 1 inside the region and blends to 0 at the blend width.

What moves with the patch:
- Placements inside it, keeping their height above the ground.
- Its paint texels; the old spot's paint is cleared.
- Water bodies whose seed is inside; their level follows the ground.

A move where a carried placement would not fit is refused and reverted. Ground is written as sculpt and clamped like the brush (−60 … +80 m).

## Tests

`--selftest-lasso` (`tools/run_all_tests.sh`: `lasso`; fast-mode rule `lasso`) runs on Blank Slate:
- **Capture:**
  - a freehand loop, Shift + circle and Alt + circle select exactly the expected placements;
  - the pivot and touching rules;
  - generated furniture is never selected;
  - the map hash is unchanged by selection.
- **Objects:**
  - a group move keeps offsets (< 1 mm);
  - a 90° turn about the centre gives the analytic poses;
  - a move onto another building is rejected and reverted, with no undo step;
  - duplicate, delete and Del / Ctrl+D are each one undo step, and every undo is exact.
- **Elevation:**
  - plateau +5 m is +5 inside and 0 beyond the ring, monotonic across it;
  - no tile cracks;
  - a tree rides up;
  - one undo step, exact undo;
  - the dome profile;
  - preview then cancel.
- **Water:**
  - a mesa with a notch and a pit: the fill level equals a brute-force bisection of the spill level (< 2 cm) and sits at the notch floor;
  - `lassoWaterAt` returns the level inside and nothing outside;
  - a fill on the mesa top is refused;
  - lower + fill is one undo step;
  - save / reopen; equal hashes for two loads.
- **Paint:**
  - all 12 types, one undo step each;
  - weight 1 inside and 0 beyond the edge, the soft edge, the two-type seam, the eraser;
  - grass, nature, nav area and footstep;
  - a byte-exact save / reopen, the hash change;
  - shaders compiled;
  - a rendered row of patches: none black, all different from grass, ≥ 90 % of pairs distinct.
- **Move land:**
  - a hill arrives with its relief (< 2 cm), and the old spot is smooth;
  - the tree, the paint and the pond move;
  - exact undo;
  - a 90° turn;
  - a move onto a cottage is refused and changes nothing.

Screenshots go to `selftest_out/lasso/`.
