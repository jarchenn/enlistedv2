# Graphics and terrain overhaul

The user's brief (`GRAPHICS_OVERHAUL_BRIEF.md`, 2026-11-09) asks for grounded WW2-FPS realism: material depth, lived-in
interiors, dense mixed grass, and terrain that conforms to what is placed on it. Sky and daylight stay as they are.
This file is the brief's step 0: what the engine has today for each workstream, the shared test scene, and the
fixed cameras. Each stream adds its own section and its `CHANGES.md` notes.

| Stream | Brief sections | Branch |
|---|---|---|
| GX1 | §0 test scene, §5 earthworks, §6 placement and reform robustness, §7 trenches | `gx-base` |
| GX2 | §2 interior lighting, §3 interior dressing | `gx2-interiors` |
| GX3 | §4 grass and ground cover | `gx3-grass` |
| GX4 | §1 exterior part (toward the GLB references) | `gx4-exteriors` |
| GX5 | §1 interior / material part, §8 post (after PF2 merges) | |
| GX6 | new houses and a stronghold | `gx6-buildings` |

Rules from the brief: ask the user before changing the terrain representation (heightmap vs mesh), adding a
third-party library or asset pack, or changing network replication. Frame time may regress at most about 10 % on the
default preset, with Low / Medium / High tiers so that Low costs about what it does today.

## Engine summary (step 0)

**Renderer.** C++20 / OpenGL 4.5 forward renderer (`src/render.cpp`), clustered point and spot lights
(`src/lighting.cpp`), probe GI, volumetric sun shafts, SSAO, two shadow cascades, TAA, filmic grade.
Physics is Jolt. Content is JSON assets made of box / cylinder / mesh parts (`docs/asset-format.md`).

### §1 Materials

- 30+ procedural patterns in `kMainFS` (`src/render.cpp`), with per-instance wear (`uInst`).
- L4 textures (`docs/textures.md`): 63 ambientCG CC0 sets as triplanar texture arrays (albedo + AO, normal,
  roughness + height), mapped through `content/textures/textures.json` slots in detail or replace mode.
- Missing:
  - parallax / POM;
  - a mask-driven grime and wear layer;
  - material-over-material blends (plaster over brick);
  - static decals (`src/decals.cpp` is gore-only, but its depth-projected renderer can be reused).
- Skirting and wainscot exist only in the shader. Door and window frames are geometry; partition doorways have no
  jambs.

### §2 Interior lighting

- Clustered point / spot lights with inverse-square falloff (`src/lighting.cpp`, the light block in `src/render.cpp`).
- Automatic room lamps (`src/lights.cpp`), probe GI, volumetric sun shafts.
- Missing: hallway spacing rules, wall sconces, colour temperature in Kelvin from a config, dust motes.

### §3 Interior dressing

- Auto-furnish with 12 room types (`src/interior.cpp`), 63 `furn_*` pieces, deterministic per-building seeds, wall
  contact by ray.
- Recipes are hard-coded C++ and only partly clustered. No attic or cellar types, no subdivision of oversized rooms,
  no rule config.

### §4 Grass

- 7-blade tufts, 5 species, 8 / 16 / 40 tufts per m² by preset, LOD, vertex-shader wind, receives shadows
  (`src/grass.cpp`, `kGrassVS` / `kGrassFS` in `src/render.cpp`).
- Missing: terrain-normal tilt, shadow casting, translucency, clump meshes.
- Ground: 13 lasso paint types with grass factors (`src/ground_paint.cpp`) plus the route / wear surface mask
  (`src/surface.cpp`).

### §5 Earthworks

- Bunker berms are box parts in the asset meshes (`bunker_field_shelter`, `bunker_observation_post`), not terrain.
  Their edges are straight, their faces planar, and they meet the field at a knife edge (`current_06`).

### §6 Placement and reform

- `App::checkPlacement` (`src/app.cpp`): the instance stands at the highest of 9 footprint samples and is refused
  beyond `min(maxGrade, plinthDepth)` of fall.
- `crushAndFlatten` (`src/snap.cpp`) flattens under a placed building with a 2.5 m feather.
- `terrainSeats` / `terrainReseat` (`src/terrain_brush.cpp`) re-seat objects after brush and lasso edits; craters
  do not.
- Missing: auto-levelling pads, a plinth skirt, a pinned-object registry, a floating / buried validation pass.

### §7 Trenches

- `aw_trench_*` pieces each carry a polygon `terrainCut` (`src/terrain_cuts.cpp`, synced by `navSyncCuts` in
  `src/editor_nav.cpp`). A cut only lowers ground; the piece stands at its highest footprint sample, so on any slope
  the downhill side floats above the field.
- Snap chains (`src/snap.cpp`) share a base height (up to 0.8 m of mismatch).
- Parapets and revetments are rigid boxes in the piece mesh: the "slab and gap" of `current_07`.

### §8 Post

- SSAO (radius 0.6, no GTAO), 2 shadow cascades (40 m near), fixed filmic grade (7 % desaturation, 0.16 vignette),
  height fog, TAA sharpen. No LUT.

### Terrain and network

- Ground = a coarse base grid (`Terrain::kCell` ≈ 3.14 m) + sparse fine tiles at 0.448 m (`kFine` = 35 per tile)
  holding the V8 sculpt layer, terrain cuts and crater deltas (`src/terrain.h`).
- Multiplayer requires identical maps (`mapHash`); craters are replicated as events. The terrain must stay
  deterministic from map data, so every GX1 deformation (trench carves, berms, pads) is derived from the instances.

## Test scene

`content/templates/gx_test_scene` (New Map > GX test scene). Built by `gxBuildScene` in `src/capture.cpp` on the
Blank Slate ground, at fixed positions:

| Item | Where | Brief |
|---|---|---|
| `vil_chateau_grand`: timber-frame, 40 x 22 m, a 39 m corridor per floor, furnished rooms | origin | §1-§3, `current_01..03` |
| line-up: `ruin_house_shelled`, `vil_villa_deauville`, `bunker_observation_post`, `bunker_field_shelter` | z = -80 | `current_04` |
| the observation post: the "bunker on a mound" | (8, -80) | §5, `current_06` |
| trench: entry, 2 straights, corner, 2 straights (L-shape) | x 50-80, z 0-20 | §7, `current_07` |
| a 40 x 40 m grass field, kept clear | x -20..20, z 40..80 | §4, `current_05` |

## Fixed cameras

```bash
NMK_CAPTURE_SET=gx tools/gpu_turn.sh ./NormandyMapmaker.exe --capture-materials <tag>
```

This writes `selftest_out/mat_<tag>_<shot>.png` (1920 x 1080). Stream results are kept as
`Documents/MapMaker/screens/gx/<shot>_before.png` (batch2) and `<shot>_<stream>.png` / `_after.png`.
`NMK_GX_REBUILD=1` rebuilds the scene in code instead of loading the template and writes
`selftest_out/gx_test_scene.nmap.json`.

| Shot | Camera | Matches |
|---|---|---|
| `current_01_hallway` | chateau level-1 corridor, looking along it | `current_01_hallway_dark_no_lamps` |
| `current_02_room` | chateau entrance hall, from the front door | `current_02_room_sparse_flat` |
| `current_03_exterior` | chateau rear three-quarter, tower on the right | `current_03_exterior_timberframe_building` |
| `current_04_lineup` | the line-up from 46 m at eye height | `current_04_building_lineup_ruins_house_bunkers` |
| `current_05_field` | eye height over the grass field toward the horizon | `current_05_grass_field_flat_wide` |
| `current_06_bunker` | the observation post, low from the front-left | `current_06_bunker_hard_terrain_edges` |
| `current_07_trench` | above the trench corner's inner side, looking at the mitre | `current_07_trench_floating_slab_gaps_hard_edges` |

Frame time on the scene: run the same capture with `GPU_TURN_EXCLUSIVE=1`; the log reports per-shot GPU ms.
