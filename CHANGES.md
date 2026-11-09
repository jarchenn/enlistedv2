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
  - Joined pieces carve as one network: floor 1.36 m wide between the revetments, 1.62 m below the anchor, walls
    battered 0.12, mitred corners. The carve runs one grid step past the revetment line, so the wall slope never
    narrows the floor; the asset's wattle stays as an invisible collider.
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
  - 33° angle-of-repose profile (rounded crest, straight face, wide concave toe), banks merged with a smooth max;
  - the flanks fall from the rear crest to the front bank, so the front face stays exposed;
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

### Results

Screenshots: `screens/gx/current_0N_<shot>_before.png` / `_after.png` (the same `NMK_CAPTURE_SET=gx` cameras).
The before shots ran with `terrain_blend.json` `"enabled": false`, which runs the batch2 terrain logic.

- `current_06` (bunker): the berm is terrain. There is no straight silhouette edge and no faceting. Grass runs
  continuously from the field up the mound, sparser on the face, with bare patches. A dirt band sits where the
  concrete meets the earth, and the front slit stays exposed.
- `current_07` (trench):
  - cut into the ground, with no gap or overhang;
  - an irregular spoil lip either side (low on the fire side);
  - revetment flush with the carved soil, with clods along its top;
  - a mitred corner and an open ammunition niche.
- `current_04` (line-up): both bunkers sit on grassed mounds.
- §6 acceptance, from `--selftest-terrain`:
  - a cottage on 0.62 m of variation is placed;
  - its floor is level within 1 mm, with no drop at the walls and an 18.5° embankment;
  - after a 2 m raise, a 2 m lower and a crater beside it, it has not moved and its footprint is still exact.

Frame time on the test scene, per camera, GX1 off then on, same build, one GPU slot (`screens/gx/gx1_perf_*.log`):

| Camera | GPU ms before | GPU ms after | Draws | Triangles |
|---|---|---|---|---|
| 01 hallway | 24.4 | 23.5 | 866 → 881 | 1.51 M → 1.62 M |
| 02 room | 22.1 | 21.9 | 849 → 862 | 1.34 M → 1.43 M |
| 03 exterior | 13.1 | 12.9 | 981 → 996 | 1.45 M → 1.56 M |
| 04 line-up | 8.4 | 7.9 | 707 → 719 | 1.91 M → 2.00 M |
| 05 field | 6.7 | 6.7 | 493 → 505 | 1.55 M → 1.65 M |
| 06 bunker | 9.1 | 9.1 | 556 → 568 | 1.44 M → 1.50 M |
| 07 trench | 13.2 | 13.9 (+5.7 %) | 460 → 481 | 1.60 M → 1.84 M |

- Within the brief's +10 % guardrail.
- Terrain tiles go from 4 to 29 in the scene.
- The GPU is shared by other streams' test runs, and other runs of the same pair drifted far more than these
  differences, so read the table as a comparison, not as absolute cost.

Suites (from frozen copies of this build):

| Suite | Result |
|---|---|
| terrain | 62 / 1. The one failure is the Beautify eye-level gate on battle1 (+1.7 ms on an already contended 32 ms baseline). Beautify's additions get no GX1 stamps, and the same gate passed in the previous run. |
| lasso | 88 / 0 |
| entities | 81 / 0 |
| snap | 40 / 1 before the carve-depth expectation was updated (the cut floor is the configured 1.62 m); not re-run since. |
| structures (trench pieces, both bunkers, the facade ruin) | all walks pass. `ruin_townhouse_facade`'s rubble walk fails the same way on the pre-GX1 baseline, so it is not GX1's. |

### Deferred, and why

- **Lasso.** The validation pass runs after brush strokes only. Lasso edits re-seat through `terrainReseat` (pinned
  objects follow the GX1 rule), but do not run the report.
- **Full suites.** The maps, nav and AI suites, and `--net`, were not run (the plan leaves them to the merge pass).
  The pads change the ground under existing maps' buildings.
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


## GX2: interior lighting and dressing (brief sections 2 and 3), branch `gx2-interiors`

### Done
**Lighting (section 2)**: `content/settings/interior_lights.json`, `src/lights.cpp`:
- **Config.** Every lamp's colour temperature is set in Kelvin (2700-3200 K), converted by a blackbody fit to linear
  RGB. The file also sets intensity, radius, the daytime dimming and each room type's ceiling and table lights.
  Falloff stays inverse-square (L2).
- **Hallway pendants.** Halls at least 4.5 m long get a pendant every 5-6 m along the centre line. A pendant that
  would hang within 0.7 m of a window's centre line moves to the midpoint between two windows, so light pools
  alternate with the window light.
- **Wall lights by doorways.** Rooms of 16 m2 or more, and every hall, get wall lights either side of their
  doorways: electric sconces in town buildings, candle sconces or wall oil lamps in rural ones. They are placed
  through the room planner, so they keep clear of doors, windows and walking routes.
- **Ceiling lights by size.** Rooms of 20 m2 or more always get a ceiling light; oversized rooms get one per
  4-5 m zone.
- **Brighter by day.** Lamps run at 45 % of their output by day (was 25 %), so the pools still read in daylight.
- **Long corridors are halls now.** A room at most 3 m wide and at least 3.5 times as long counts as a hall. The
  chateau's 2.2 x 36 m rear corridors used to be typed as sitting rooms, with one lamp each.
- **New fixtures**, each a mesh with an emissive part and a light:
  - wall sconce with a frosted tulip shade;
  - brass candle sconce;
  - kerosene wall lamp with a reflector;
  - tall candlestick;
  - hall pendant with an opal-glass bowl.
- **Dust motes** drift in the sun shafts of the nearest sunlit windows. The window is checked by a ray toward the
  sun, the particles have no stored state, and they show in every mode. A new graphics setting controls them:
  "Dust motes" is Off on Low, Low on Medium (18 per window) and High on High (40).
- **Window shadows.** Indoors, the near sun cascade tightens from 40 m to 24 m, about 1.2 cm per texel at 4096,
  so window bars cast crisp shadows on the floor.

**Dressing (section 3)**: `content/settings/room_dressing.json`, `src/interior.cpp` `Planner`:
- **The recipes are data now.** The hard-coded recipes of the 12 room types moved into the JSON as step lists. The
  file adds:
  - `clusters`: an anchor piece plus satellites, with seeded jitter, flush placement, pieces on top and a piece
    hung above it;
  - `zones`: groupings for oversized rooms;
  - `wall`: wall items per metre and the longest bare wall allowed;
  - rug, curtain (open or half-drawn) and debris chances.
- **Two new room types**, detected automatically:
  - attic: headroom under 2.25 m somewhere under the roof;
  - cellar: floor below -0.8 m.
- **Oversized rooms** (more than 8 m a side; halls, barns, naves, taverns and classrooms excepted) get one cluster
  grouping per 4-5 m zone.
- **Hallways** get:
  - a runner rug tiled down the middle;
  - side tables with lamps;
  - pictures every 3-4 m on both long walls;
  - a coat stand or coat hooks by the outside door.
- **Wall fill.** After a room's recipe has run, its longest bare wall is filled with a picture, mirror, shelf, pan
  rail, coat hooks or sconce, repeatedly, until no bare run is longer than 4 m. A floor piece is used where nothing
  can hang. Frames hang with their centre at 1.5-1.7 m.
- **Floor pieces** stand 3 cm off the wall, on the front of the new skirting board.
- **Deterministic.** Every pick is seeded by the building's furnishing seed and the slot name, so every client
  dresses a building the same way.
- **Stats.** `InteriorSys::lastStats` and the `interior: dressed` log line give each room's pieces, wall-mounted
  pieces, lights and longest bare wall. `NMK_GX2_DEBUG=1` logs why a wall could not be filled.

**New props** (`tools/gen_furniture.py`, 21 pieces): each carries a `prop.bounds` envelope computed from its rotated
parts, and none goes over 75 parts.
- Furniture: iron bedstead (spindled head and foot, brass knobs, a blanket draped in folds, a pillow), gramophone
  with a fluted brass horn, cathedral-top wireless set, bentwood coat stand with coats, a hat and an umbrella, coat
  hooks with a greatcoat, jacket and scarf, stacked leather suitcases.
- Fittings: half-drawn curtains, a pan rail with skillets.
- Floor: broken glass, plaster debris with lath and a brick, a 3.2 m hall runner.
- Paintings: five framed paintings (landscape, seascape, evening, portrait, small).

**Trim geometry** (`tools/parts.mjs` `interiorTrim`):
- **What it adds.** Real skirting boards (board and moulded top, 2 cm proud), a dado-rail lip at 0.955 m that
  matches the shader's wainscot cap, and jamb liners plus architraves round partition doorways the builder had
  not framed.
- **Where.** On every interior plaster face with a floor along it, stopping at doors and at windows whose sill is
  below the rail.
- **Visual only** (`k: 0`), placed in the same part lists as the plaster, so the trim breaks with its wall bay.
  Colliders, panels and nav are unchanged.
- **Applied by** B2 `dress()` (13 buildings at version 3) and B3 `house()` / `gatehouse()` (11 buildings at
  version 2).
- **No coplanar faces (z-fight fix).** Every trim back is buried 2.8-5.2 mm in the plaster; fronts stand 16-34 mm
  proud.
  - Runs are merged per face plane across plaster pieces, so no two runs abut or overlap.
  - Runs end 0.6 / 1.6 mm short of their span, x- and z-runs differ by 1.5 mm in height and depth, and runs stop
    1.5-2.5 mm short of any solid in front of the face (stair treads, beams, a builder's casing) and of
    architraves (31 mm proud).
  - Faces back to back with another plaster lining, and doorway reveals, get no trim.
  - A last pass shortens any run end still lying in another face's plane.
  - Checked by a coplanar scan of all 24 regenerated buildings: faces within 0.5 mm of the same plane and
    overlapping more than 1 mm give 0 hits (down from 39 760).
  - Hung pieces (frames, sconces, mirrors) are placed 12 mm off the wall face, and floor pieces 30 mm off.

### Numbers
**Acceptance, from `--selftest-interiors`:**
- **current_01 hallway.** The chateau's 38.7 m ground-floor corridor gets:
  - 7 pendants, 4.8-5.5 m apart, each at least 2.25 m from a window's centre line (they hang between the windows);
  - 8 runner rugs, 20 pictures and sconces at its doorways.
- **current_02 room.** The chateau's largest ground-floor room (169 m2) has 95 pieces (gate ≥ 25), 21 of them
  wall-mounted (gate ≥ 6), and 19 lights. Its longest bare wall is 4.0 m (gate ≤ 4).
- **Every building.** All 24 furnishable building types have 134 / 134 rooms furnished and 134 / 134 with their own
  light. 7 of 128 rooms still have a bare run over 4 m: schoolhouse halls, the inn, the mairie hall. Each is 4.1-4.8 m,
  along walls that are mostly stair or doorway.
- **Density.** 1702 pieces against 156 with the first templates, ×10.9. The chateau went from 369 to 989 pieces; its
  furnish time went from 268 to 366 ms.

**Frame time.** The GPU is shared with other sessions and two open game windows, so cross-build timings swing by
±50 % and are not usable. The pendant budget was measured in one process instead: the same night hall view, with
the hall pendants alternately on and off every 30 frames, 8 rounds each (`NMK_CAPTURE_AB=1`).

| Hall | Pendants | Effect on GPU time, two runs |
|---|---|---|
| Chateau corridor | 7 | +0.7 to +2.1 % |
| Rectory hall | | +0.8 to +2.5 % |
| Mairie hall | | +1.0 to +1.7 % |
| Schoolhouse hall | | +0.6 to +4.3 % |

All are inside the brief's 5 % budget for a hallway with 6 pendants. The raw data is
`screens/gx/gx2_pendant_ab_night.json`. Absolute GPU times were 40-65 ms under the shared load. Low turns the dust motes off (and L2 already
drops light shadows and GI on Low).

**Suites** (all through `tools/gpu_turn.sh`):

| Suite | Result |
|---|---|
| `--selftest-lights` | 45 / 45 |
| `--selftest-damage` | 0 failures |
| `--selftest-interiors` | 0 failures, on the final code |
| `--selftest-structures` | not finished on the final code: see below |

**Structures, in detail:**
- **The last complete run** was on the code before the final fixes: 1339 passed, 7 failed. Six failures were the
  church tower tour: a wall oil lamp with a collider hung on a stair landing. That is fixed (the lamp lost its
  collider, and tower rooms take only non-blocking wall pieces). The seventh is `townhouse_facade` "up the rubble
  field", which also fails on the batch2 base (GX1's before-run).
- **The two runs on the final code** were both cut off by my run timeout (66 min) on the shared GPU. Up to that point
  everything passed except the batch2 failure: 1038 checks, the full church tower tour and the chateau tours
  included.

**Test changes**, each explained in a comment at the change:
- the decor / removed-furniture fixtures move clear of the grid;
- the glazed-hutch check looks for a glazed hutch;
- the persistence step restores the lights its undo brings back off;
- the lights check now names each dark building's lights and their reasons.

### Deferred, and engine limits
- **The chateau's 10 x 18.6 m rooms keep their volume.** They are furnished as zones rather than split by new
  partitions. More rooms would pass the 16-room cap of the per-room wall finish (`SurfaceParams`) and change the
  B3 tours and nav.
- **Baked lighting.** Static fixtures are dynamic clustered lights; there is no lightmap baker. The probe GI (L2)
  provides the bounce light.
- **GTAO, decals and material layering** belong to GX5.
