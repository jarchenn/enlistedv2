# Changes: graphics and terrain overhaul

The user's `GRAPHICS_OVERHAUL_BRIEF.md` (2026-11-09), one section per workstream. Engine summary, test scene and
cameras: [docs/graphics-overhaul.md](docs/graphics-overhaul.md). Screenshots: `Documents/MapMaker/screens/gx/`.

## GX1: test scene (§0), earthworks (§5), placement and reform robustness (§6), trenches (§7)

### Done

- **§0 test scene.**
  - `content/templates/gx_test_scene` (New Map): the B3 chateau (timber-frame, corridors, furnished rooms), a ruin /
    villa / two-bunker line-up, an L-shaped six-piece trench, and a clear 40 x 40 m field.
  - `NMK_CAPTURE_SET=gx` cameras match `current_01..07`; `NMK_CAPTURE_PERF=<frames>` logs per-camera frame time.
  - `docs/graphics-overhaul.md` holds the step-0 engine summary.
- **Terrain stamps.** One derived layer in the existing terrain (`src/terrain_cuts.cpp`, `src/terrain_blend.cpp`):
  - built from the instances on every change; nothing new is saved;
  - deterministic, so no replication change;
  - the heightmap + 0.45 m tile representation is unchanged.
  - Config: `content/settings/terrain_blend.json`; `"enabled": false` gives the batch2 behaviour.
- **§7 trenches.**
  - Joined pieces carve as one network: floor 1.36 m wide, 1.62 m below the anchor, walls battered 0.12, mitred
    corners.
  - Spoil parapets of 0.45 m ±35 % noise either side (half on the fire side so the firestep sees over it), fading
    to the field over 1.9 m; lip to floor about 2.05 m on the parados side.
  - The slab-and-gap parts are removed at load: wattle, spoil, picket, earth and turf above -1 m.
  - Generated along the cut:
    - wattle revetment from below the floor to the actual soil behind it, leaning with the wall, with a top pole;
    - pickets driven into the floor;
    - sandbags resting on the parados;
    - end walls where a run stops;
    - openings at junctions, inner corners and niches.
  - Masks: mud on the floor and walls, turned earth on the parapet fading to sparse grass, then full grass.
  - Trench pieces belong to the pinned registry.
- **§5 berms.** The observation post and field shelter banks are terrain (`earthwork` asset block):
  - 33° angle-of-repose profile (convex shoulder, concave toe), banks merged with a smooth max;
  - low-frequency height noise and outline jitter;
  - a 3 m smoothing band;
  - slope-driven bare / sparse / full grass mask and a contact dirt band at the concrete.
  - The faceted bank prisms are removed at load.
- **§6 placement.**
  - Auto-levelling to the median of the footprint, refused only beyond ±1.5 m with the variance in the message.
  - Pad plus a smoothstep blend ring of 2-3.5 m, scaled by the delta.
  - A stone retaining skirt where the raise exceeds 0.9 m.
  - Soil mask on the changed ground.
  - Place-tool preview of the pad and ring.
- **§6 reform robustness.**
  - Pads are the pinned registry: footprint + anchor.
  - Brush, lasso and generator reforms re-conform the ground to them.
  - Pinned objects ride only a shift common to their whole footprint.
  - Craters spare footprints as before and now re-seat loose props in the editor.
  - A validation pass (after every brush stroke) logs floating (> 5 cm) and buried (> 10 cm) objects and re-seats
    loose props.
- **Tests.** `--selftest-terrain` steps `gx1: ...`:
  - a house on 60 cm of variation: level floor, no gap, embankment slope / step;
  - a raise, a lower and a crater next to it;
  - the margin refusal;
  - the trench network: depth, parapets, lip-to-floor, mitre, removed parts, masks;
  - the berm: height, repose slope, irregular crest, clear passage, slope-blend band, removed prisms.

### Deferred, and why

- **Lasso.** The validation pass runs after brush strokes only. Lasso edits re-seat through `terrainReseat` (pinned
  objects follow the GX1 rule), but do not run the report.
- **Craters during play** leave placements alone. They are a session overlay that resets on return to the editor;
  re-seating doc instances there would outlive the overlay.
- **Optional dressing.** No debris (ammo crates, helmet, shovel) along the trench floor; the pieces' own duckboards
  and ammunition niches remain.
- **Other earth-covered assets.** Only the two bunkers with bank prisms are converted. Other assets with earth meshes
  (hedgerow banks, the R667) are untouched.

### Engine limits hit

- **Grid resolution.** The terrain grid is 0.448 m. A near-vertical trench wall therefore spans one grid cell, and
  diagonal trenches show a fine sawtooth at the lip.
  - The revetment top is fitted to the highest soil within 0.7 m behind the wall, so no gap shows.
  - The groove behind the revetment top is at most one cell wide.
- **Shipped maps.** Pads raise or cut the ground under every pinned building inside the build area whose footprint
  deviates more than 5 cm (the validation threshold for floating). That includes existing maps, where buildings were
  placed on the highest sample: their floors now meet the ground. Decor beyond the border keeps its ground.
  - This adds fine tiles: more draw calls on large maps.
  - The maps / nav suites are left for the merge pass, as the plan asks.

## GX5: materials, decals and post (§1 interior / material part, §8)

Branch `gx5-materials` (pf2-gpu-perf + gx-base). Details: [docs/graphics-overhaul.md](docs/graphics-overhaul.md#gx5-materials-decals-and-post-1-interior--material-part-8).
Screenshots: `screens/gx/gx5/<shot>_gx5_before.png` / `_gx5_after.png` (all iterations as `_d<n>.png`, High as `_h7.png`).

### Done

- **Layered materials (`gxLayer`, kMainFS).** Mask-driven grime and wear, not baked albedo:
  - grime at the floor, in room corners and under the ceiling, plus world-space blotches;
  - hand-height grease;
  - roughness breakup on every surface;
  - a high-frequency detail relief on walls.
  - Wear is per room: each room scales the copy's wear by 0.4-1.6, so one house has kept and ruined rooms.
- **Seam occlusion, about 0.5 m.** Wall-wall, wall-floor and wall-ceiling joints, and floor and ceiling edges, from the room rectangles. It darkens the indirect light, and the lamp and window light by 45 % of that.
- **Two-material blends.**
  - Interior plaster with its paper or paint falls away to the brick behind (target_08): a torn ring and a broken rim, recessed mortar, a 1.4 cm relief step, more at the foot and in corners, seeded per wall plane.
  - Exterior lime render / infill falls off to the brick nogging.
- **Wallpaper.** A damask variant (palmette on an ogee lattice, tone on tone), the print embossed 0.5 mm with shadowed edges, and paper fibre.
- **Static decals** (`src/static_decals.cpp`, `content/settings/decals.json`).
  - Eight types: cracks, water stains, soot, scuffs, bullet pocks, rain streaks, picture outlines, damp.
  - Scattered per building from its rooms, hearths and windows, deterministic, prioritised under a per-room cap.
  - Evaluated in the material, so they are lit and have relief.
  - One SSBO for every building.
- **Rooms past 16.** Large buildings (the chateau) now get finishes, seams and decals in every room, not only the first 16.
- **Post.**
  - SSAO retuned to 0.5 m / 0.95.
  - The grade (`content/settings/grade.json`): saturation 0.91, cool shadows and warm highlights, an S-curve, vignette 0.22, an edge-aware sharpen against TAA softening, optional chromatic aberration (off).
- **POM (High).** Replace-mode texture sets within 14 m.
- **Tiers.** Settings > Material detail: Low (the batch2 look), Medium (default), High (+ POM).
- **Measurement.**
  - A/B switches `matlayer`, `sdecals`, `grade`, `gx5`.
  - Capture environment variables: `NMK_CAPTURE_AB` (interleaved, wall clock), `NMK_CAPTURE_PRESET`, `NMK_GX5_CLOSE`, `NMK_GX5_DEBUG`.

### Frame time (Medium, 1920 x 1080, paired interleaved A/B at the gx cameras, wall clock, shared GPU)

| switch | interior cameras (corridor, hall, 3 close-ups) | exterior / field / bunker / trench |
|---|---|---|
| matlayer | +3.0 .. +3.7 %, one close wall view +10.6 % | +0.3 .. +2.7 % |
| sdecals | +1.1 .. +5.0 % | -3 .. +2.6 % |
| grade | -0.8 .. +0.7 % | +0.4 .. +5.8 % (noise under load) |

- Summed, the interior cameras come to about +5..9 % and the outdoor ones +1..5 %, within the brief's +10 %.
- The GPU was heavily shared during every run (absolute frame times 30-55 ms), so treat these as ratios.
- The first pass measured +22 % at a close interior view; the noise masks were halved after that.

### Deferred / limits

- GTAO: not added. The analytic seam occlusion answers the brief's "flat lighting at seams", and SSAO was retuned instead.
- Decals are procedural shapes in the shader, not a texture atlas: no new asset pack. A baked atlas would need art.
- Exterior rain streaks come from the room analysis's sills. GX4's anchors can push `SD_STREAK` decals through `staticDecalsFor`.
- The corridor's light level is GX2's (lamps). The new detail reads best under their light pools.
- Texel density: unchanged (L4 1K sets). Procedural relief carries the close-up detail.
