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
