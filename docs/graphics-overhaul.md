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
| `gx_trench_floor` | on the trench floor at eye height, along the corner's first leg toward the corner (floor, both revetments, the lip against the sky) | (extra: the trench criteria from inside) |

Frame time on the scene: run the same capture with `GPU_TURN_EXCLUSIVE=1`; the log reports per-shot GPU ms.

## GX1 terrain conformance

Code: `src/terrain_blend.cpp` (stamps from the instances, placement, generated meshes, validation) and the stamp
evaluation in `src/terrain_cuts.cpp`. Tunables: `content/settings/terrain_blend.json` (`"enabled": false` restores
the batch2 behaviour exactly). The suite: `--selftest-terrain`, steps `gx1: ...`.

The terrain representation is unchanged: a base grid plus 0.45 m fine tiles. GX1 adds one derived layer, the
**stamps**, computed from `MapDoc::instances` on every change (`navSyncCuts` → `blendSyncStamps`). It is evaluated
inside `Terrain::cutBase`, so `Tile::base` includes it. Crater deltas and the sculpt layer stay what they were, and
nothing new is saved in the map: every client derives the same ground from the same instances.

Composition per fine sample, in order:

1. **Pad rings.** A pinned object's footprint is blended into the ground around it over a smoothstep ring.
   The ring is 2-3.5 m wide, scaled by the largest height difference.
2. **Berms.** Earth banks against a bunker's walls.
3. **Trench parapets.** Spoil raised either side of the lip.
4. **Pinned footprints.** Set exactly to their anchor, so a neighbour's ring or berm can never move a floor.
5. **Trench carves.** The floor, then walls leaning back by `batter`.

The polygon `terrainCut`s of other assets (foxholes, ditches, ponds) are applied after all of these, as before.

### §6 placement and reform robustness

- **Auto-levelling.** A pinned object stands on the median of a 5 x 5 grid plus perimeter samples
  (`blendPlacement`, called from `App::checkPlacement`).
  - Pinned objects: buildings (the snap `crush` categories, enterable assets with wall bays) and Atlantic Wall
    emplacements without cuts.
  - The ground under the footprint is raised or lowered to that level.
  - Placement is refused only when the ground varies more than `placement.margin` (1.5 m). The message gives the
    actual variance.
  - The old flatten in `crushAndFlatten` (persisted crater deltas) is skipped for pinned objects.
- **Retaining skirt.** Where a pad stands more than `skirtThreshold` (0.9 m) above the ground at its edge:
  - the embankment is capped at that height;
  - a stone retaining wall (`stone_plinth`) is generated along the footprint edge, from the floor down into the
    ground (`buildSkirt`).
- **Masks.** Raised or cut ground in the ring and footprint gets the soil mask (`Tile::sdirt`, read through
  `Terrain::dirtAt`), fading to grass at the ring's edge.
- **Preview.** The Place tool draws the levelled pad and the ring's outer edge on the ground: amber for a raise,
  blue for a cut, plus a skirt line when one will be built.
- **Pinned registry.** The pads are the registry: the footprint, anchor y and ring of every pinned object.
  - A brush, lasso or generator reform re-bases the tiles under it. The pad then re-conforms the ground to the
    footprint, and the ring blends the new ground into it.
  - `terrainReseat` moves a pinned object only by the shift common to its whole footprint (a lasso plateau under
    it), never by a reform next to it.
  - Craters already spare footprints (`groundWeight`). In the editor, a crater now re-seats loose props near it.
- **Validation** (`blendValidate`, after every brush stroke):
  - pinned bases are checked at their corners and edge midpoints;
  - loose props on open ground are checked at their pivot;
  - floating more than 5 cm or buried more than 10 cm is logged;
  - loose props are re-seated (pinned objects never move: the ground conforms to them).

### §5 earthworks

- An asset's `earthwork` block turns its earth cover into terrain:
  - `core`: the walls' half-size;
  - `banks`: ridge segments `[ax, az, bx, bz, crest at a, crest at b]` along the walls (a flank falls toward an
    open face, so the observation post's front stays exposed);
  - `holes`: rectangles kept clear, such as entrance passages;
  - `replace`: the prism parts it supersedes, dropped at load.
- Profile:
  - a 0.35 m crest shelf, a rounded shoulder, a straight face at `reposeDeg` (33°) less a 15 % margin for the noise,
    and a wide concave toe;
  - banks merged with a smooth max (no crease ridges);
  - ±12 % low-frequency height noise and ±0.45 m outline jitter (no straight ridge line);
  - a 3 m smoothing band past the toe into the natural ground.
- Soil mask by slope: bare above `bareSlope` (31°) and in low-frequency patches, sparse dry grass from `sparseSlope` (14°), full grass below.
  It is continuous because the profile is. A 0.8 m dirt contact band runs where the concrete meets the earth.
- Done for `bunker_observation_post` and `bunker_field_shelter` (the two bunkers with earth banks). Their roof earth
  stays a mesh.

### §7 trenches

- A trench piece's `trench` block:
  - `path`: the centreline in asset space; the third value scales the depth, 0 at the entry ramp's mouth;
  - `branches`: the ammunition niches;
  - `replace`: the slab parts, meaning wattle, spoil, picket, earth and turf above -1 m. The firestep, floor and
    duckboards stay.
- Pieces whose path ends meet form one network and one stamp. Shared ends average their floor heights.
- The carve:
  - floor at the anchor minus `depth` (1.62 m); 2 x `floorHalf` = 1.36 m wide between the revetments;
  - the floor runs one grid step (0.45 m) past the revetment line and past free ends, so on the 0.45 m grid the wall
    slope never comes in front of the revetment, whatever the trench's angle. The generated panel is that thick,
    with heaped clods (spoil, dark earth) flush with the soil on top, so nothing shows behind it. The asset's wattle boxes stay as invisible
    colliders (`keepCollision`): players and bots meet the walls exactly where they did before;
  - walls lean back 0.12 m per metre;
  - corners are mitred: the legs are boxes that overlap by the floor half-width.
- Parapets:
  - 0.45 m of spoil (±35 % noise) over 1.9 m from the lip, smoothstep in and out; half that on the fire side
    (the piece's local +z), so a man on the firestep sees over it (`parapetFront`);
  - rounded around the outside of corners and run ends;
  - lip to floor is about 2.05 m.
- The revetment is generated along the network for every piece (`buildTrench`), fitted to the carve:
  - wattle panels from below the floor to just above the actual soil behind them (sampled over 0.7 m), leaning with
    the wall;
  - a top pole;
  - pickets every 0.75 m driven 0.35 m into the floor;
  - sandbags resting on the parados (bottom at the lowest ground under the bag), two courses with gaps; the fire
    side stays clear;
  - end walls where a run stops;
  - openings where the ground behind is carved (junctions, the inner side of a corner, niches).
- Masks:
  - `Tile::sdirt` (the soil mask that grass reads) is 1 on the floor and walls, and turned earth on the parapet
    fading to sparse grass on its outer slope, then full grass.
  - `Tile::sew` goes to the terrain shader as vertex attribute 3: packed mud, freshly turned earth and wetness. Inside
    the cut it overrides the slope rock / scree shading, so the walls and floor are dark packed mud, wetter and
    duller low down. The spoil crest is dark crumbly earth breaking up into the outer slope over 1-2 m.
  - The material weathering (rising damp and moss) stops at an asset's ground line, and the generated revetment is
    built below its draw position, so dug-in timber gets no moss blotches.
  - The trench pieces' mud uses the `soil` texture slot (asset version 3).
- Collision: the asset's wattle boxes (invisible) and the carved heightfield; the generated revetment is visual.

### For the other streams

- GX3 grass: `Terrain::dirtAt` returns the larger of crater soil and stamp soil. Grass already thins on
  `dirt > 0.02` and drops at `> 0.18`. `Terrain::stampSoilAt` gives the stamp mask alone.
- GX6 new buildings: pinned automatically. Give an earth-covered stronghold an `earthwork` block instead of bank
  prisms.
