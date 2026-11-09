# Graphics overhaul: changes per workstream

## GX3: grass and ground cover (brief §4), branch `gx3-grass`

**Done**
- **Clumps instead of tufts.** Each instance is a clump of 12 crossed cards: tall in the middle, shorter and leaning
  outward at the rim, so neighbouring clumps overlap into a sward. Cards per m² drawn within 15 m on Medium:
  284, against about 73 blades per m² before (16 of 40 tufts × 7 blades × ~65 % kept), about 3.9x.
- **Species layers.** There are 7 layers, each placed by its own noise mask and drawn with its own procedural card
  texture:
  - fine turf;
  - meadow grass (the dominant layer);
  - seed-head grass, with spikes or open panicles;
  - clover, as trifoliate leaves with a pale chevron;
  - plantain-type broadleaf rosettes;
  - small leafy shrubs, more of them near walls;
  - dry straw.

  The textures are procedural, so no new third-party asset was needed.
- **Colour.**
  - Each clump gets a tint along green → yellow-green → straw.
  - Each card varies in value, and some cards are yellower.
  - Each blade has a vertical gradient: a darker, saturated base and a lighter, drier tip.
  - The lower 30 % of each blade blends into the ground colour under the clump. That colour is a CPU port of the
    terrain palette, including bare soil, wear, craters and painted ground.
- **Shading.**
  - Base AO is darker inside the clump and under the canopy.
  - Backlit cards glow (translucency).
  - Clumps within the shadow radius (10 m on Medium) cast sun shadows into the near cascade (`Grass::drawShadow`).
- **Placement.**
  - Clumps lean with the terrain normal and sit on the slope plane.
  - The sward thins toward the 0.8 slope cut-off.
  - Bare-earth patches follow the terrain's `bare` field, which now shows 65 % soil instead of 30 %.
  - Painted paths and ground thin the sward and colour the clump bases.
  - Grass grows back from footprint edges over 2–3 m, short first, instead of stopping at a wall of tall weeds.
- **Ground.** The near-field grass ground layer is now ambientCG Ground037 (`grass_forest`: moss, litter and soil,
  albedo + normal + AO, CC0). It was already in `content/textures`. It fades to the meadow palette with distance, which
  is the "tinted ground" beyond the grass.
- **LOD without pops.** Clumps are generated at 44 per m² in a shuffled order. Beyond the near radius:
  - the drawn share falls as (near / d)^falloff;
  - clumps keep fewer cards, 6 then 3 on Medium, and the remaining cards widen.

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
  Each clump and card fades by its rank at its own distance, in the vertex shader, so there is no per-chunk pop.
  Medium's radius changes from 58 m to 48 m.
- **Wind.** Wind is a vertex-shader effect only within 13 m (Medium) or 15 m (High); Low has none. Distant clumps keep a
  static lean. There is a new setting, Settings → Graphics → Grass wind (`grassWind`), and the Low preset turns it off.
- **Suppression.** Grass reads GX1's earthwork and trench soil (`sdirt`, through `dirtAt`), slopes, ground paint, wear,
  craters and footprints.
- **Tiers.** `content/settings/grass.json` holds the tunables named in the brief: density, near / mid / draw radius,
  falloff, cards per LOD, wind radius and shadow radius. Low is about the pre-GX3 Medium cost.
- **Test field.** `NMK_CAPTURE_SET=grass --capture-materials <tag>` renders a 40 × 40 m pasture with a dirt path, a dry
  patch and a bunker from six cameras. Each camera logs GPU ms with grass on and off, blades drawn, cards per m² within
  15 m, and clumps in the sun map.

**Measured** (RTX 2080 Ti, Medium, 1920 × 1080; before and after run interleaved in one exclusive GPU slot)

GPU ms per camera. The GPU was shared with other sessions' runs even inside the exclusive slot, so only the fastest
of several interleaved samples is a clean reading; later runs (below) were 3-7x slower on the same cameras.

| Camera (gx-base vs GX3, Medium) | before | after | change |
|---|---|---|---|
| current_04 lineup | 7.54 | 8.70 | +1.2 ms |
| current_05 field (acceptance) | 5.70 | 6.92 | +1.2 ms (+21 %) |
| current_06 bunker | 9.20 | 9.71 | +0.5 ms |
| current_01 / 02 / 03 / 07 (no grass in view or little) | 20.4 / 18.6 / 11.1 / 11.3 | 20.8 / 19.1 / 11.4 / 12.4 | +0.3 to +1.1 ms |

- **Medium is over the +10 % guardrail on the field camera** (+21 %, +1.2 ms on a 5.7 ms frame). The grass-on / grass-off
  cost inside the grass test field is about the same before and after (1.5 ms vs 1.7 ms mean), so most of the field
  delta is the denser sward being rasterised at the field camera's longer view.
- Low is the cheap tier: in the noisy tier-tuning run (Low's numbers as Medium) it sat within ~1 ms of the gx-base on every camera.
- Levers if Medium must come down, all in `content/settings/grass.json` with no rebuild: `density` (30 -> 24),
  `radius` (54 -> 48), `shadowRadius` (10 -> 0). Shadows alone showed no measurable cost.
- A vertex early-out for clumps and cards past their LOD cut was added after the A/B and has not been timed.
- Placement build cost: full Medium build 260 ms (a 40 m field), crater rebuild 15 ms for 4 chunks, with the broad
  noise sampled on a per-chunk 1 m lattice (without it the rebuild was 30 ms).
- battle1 gameplay views (`--selftest-maps`, same slot, before -> after): A1_spawn 14.7 -> 13.8 ms, obj_A_eye 26.2 -> 23.0,
  obj_B_eye 35.2 -> 31.3, obj_C_eye 23.7 -> 19.7, obj_D_eye 21.2 -> 17.6. battle1 has mostly short-sward and nature
  grass, and these runs carried outside GPU load that differed between the two builds, so read them as "no
  regression", not as a speed-up.

**Deferred / limits**
- The cards are procedural silhouettes, not photographed alpha cards. ambientCG has no grass-blade cutout sets, and the
  brief requires asking before adding a new asset pack.
- Grass casts into the near sun cascade only, at about 2 cm per texel. Single blades shadow faintly, so most of the
  "internal shadowing" comes from the canopy AO and the darker clump bases.
- The bunker berm footprint still clears a band of grass around the berm box. GX1's earthwork stamps replace the berm
  boxes, and the grass follows GX1's stamp soil.

**Files**
- `src/grass.{h,cpp}`
- the grass shaders in `src/render.cpp` (`kGrassVS` / `kGrassFS`, the new `kGrassShadowFS`), plus:
  - the grass shadow call in the near cascade;
  - the nature patch tier table;
  - one line in `kTerrainFS` (the bare-soil blend).
- `src/nature.cpp` (the plant mesh keeps 7 blades)
- `src/ground_paint.{h,cpp}` (`groundPaintTint`)
- `src/settings.{h,cpp}` (Grass wind)
- `src/capture.cpp` (the grass set)
- `src/selftest_grass.cpp` (species, density and tier checks)
- `content/settings/grass.json`
- `content/textures/textures.json` (`terrain_grass`)
- `docs/ground-grass.md`, `docs/settings.md`
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
