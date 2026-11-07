# Natural ground dressing

Boulders, stones, flowers, tall grass and small vegetation for hand-built maps (first use: the Mont-Castre terrain
base). Everything is a normal catalogue asset, placed from the Asset browser:

- **Nature:** boulders, outcrops and stone patches;
- **Vegetation:** flower patches, tall-grass patches and small plants, next to the existing trees.

The catalogue comes from `tools/catalog_nature.mjs`. It is standalone: `node tools/catalog_nature.mjs` rewrites only
`content/assets/nat_*.json`. Do not run `tools/build_assets.mjs` for it.

The code is in `src/nature.{h,cpp}`, the grass shader (`kGrassVS/FS`), pattern 29 in `kMainFS`, and the fern / nettle
card styles in `src/foliage.cpp` and `leafAlpha`. The suite is `--selftest-nature`.

## How to place each item

| Item | Asset ids | How it is placed |
|---|---|---|
| Boulders, 0.7–2.9 m | `nat_boulder_granite_s`, `nat_boulder_granite_l`, `nat_boulder_limestone_m`, `nat_boulder_limestone_l`, `nat_boulder_schist_slab`, `nat_boulder_schist_m` | Single instance. Rotate it freely. It sits into any slope: the placement height is half way between the centre and the lowest footprint edge, and the base is sunk 20–35 % of the height. |
| Boulder group, outcrops | `nat_boulders_granite_group`, `nat_outcrop_limestone`, `nat_outcrop_schist` | Single instance; several rocks (pieces) in one asset. |
| Stones / scree | `nat_stones_patch`, `nat_scree_patch`, `nat_stones_granite_patch` | **Patch.** |
| Flowers | `nat_flowers_poppy`, `_daisy`, `_buttercup`, `_cornflower`, `_cowparsley`, `_foxglove`, `_clover`, `_meadow` (mixed) | **Patch.** |
| Tall grass | `nat_tallgrass_patch`, `nat_tallgrass_verge` (with cow parsley and foxglove) | **Patch.** |
| Shrubs | `nat_hawthorn_bush`, `nat_bramble_thicket`, `nat_blackthorn_low`, `nat_gorse_shrub` | Single instance. |
| Ferns, nettles | `nat_bracken_clump`, `nat_fern_clump`, `nat_nettle_clump` | Single instance. |
| Saplings | `nat_sapling_ash`, `nat_sapling_oak` | Single instance. |

**Patches.** One placed instance scatters its items in a disc. The Selection panel has:
- **Radius** (0.5–15 m);
- **Density** (items per m²);
- **Seed**, and a **New seed** button.

The values are saved per instance in `extra.scatter`. Move or rotate the patch to move its whole layout.

**Overlap.** Nature items never block other placements and are never blocked by them, so flowers can grow under a tree
or around a boulder. Where a structure is placed on a patch, the patch clears under the structure's footprint. Items
also skip water, steep rock (plants only), craters and the edge of the map.

## Rocks (`"rock"` block)

**Shape.** Each piece is an icosphere (642 vertices above 1.4 m, otherwise 162):
1. displaced by seeded noise;
2. cut by fracture planes;
3. flattened at the base.

| Lithology | Shape |
|---|---|
| Granite | Rounded: five shallow cuts. |
| Limestone | Blocky: a flat bedding top, steep joint faces and more cuts. |
| Schist | A slab between two parallel tilted beds. |

The lower 22 % of each piece becomes a flat base, sunk below the ground line. Normals blend the face normal with the
smoothed normal, so facets read without hard aliasing.

**Shading** (pattern 29) is the same palette and the same features as `kTerrainFS`'s slope rock:
- per-bed limestone / schist tones;
- bedding lines, schist laminae and sub-vertical joints;
- iron staining, and moss / grey lichen on upward faces.

It is evaluated in world space, so boulders share bed tones with the cliffs. Beds are 1.2 m, half the cliff's 2.4 m, to
read at boulder scale. Granite gets feldspar flecks and mica specks instead of beds. Every rock gets crusty yellow
lichen and a damp, mossy foot.

**Collision.**
- Each piece gets its own convex hull of its vertices (Jolt `ConvexHullShape`), in one static compound.
- The hull stops rounds like any static shell.
- The navmesh rasterizes it as solid (`"nav": {"effect": "block"}`).

## Scatter patches (`"scatter"` block)

**Layout** is dart throwing (Poisson disc) in the disc:
- in random acceptance order;
- with a soft, ragged rim;
- with clumping from value noise (`clump` 0–1).

Stones have a power-law size distribution (`size`, `sizePow`), and their spacing grows with their size.

In mixed patches, each species grows in its own noise drifts, so a meadow forms groups rather than confetti. The layout is
a pure function of (asset id, radius, density, seed), so the same input always gives the same layout. Rebuilds after
ground changes, undo, save and reopen are bit-identical (tested).

**Plants** are drawn as grass-shader instances (`GrassInstance`), one instance buffer and one instanced draw per patch.
They share the pasture grass's rooted wind, distance fade, shadow taps and TAA motion vectors.

New species in the shader:

| Id | Species | Height | Look |
|---|---|---|---|
| 5 | Tall meadow grass | 0.6–1.2 m | Thin stems, feathery straw / purplish panicles |
| 6 | Poppy | | Four petals, black eye |
| 7 | Ox-eye daisy | | Rays and a yellow disc |
| 8 | Buttercup | | Five petals |
| 9 | Cornflower | | Ragged blue florets |
| 10 | Cow parsley | 0.7–1.2 m | Umbel of white florets |
| 11 | Foxglove | 0.8–1.4 m | Spike of bells |
| 12 | Clover | | Red / white heads over trefoil leaves |

**Plant geometry.** A plant is 7 blades of 5 rows, plus one head quad per blade (210 indices), used as:
- basal leaves;
- stems;
- flat flower heads (alpha-masked discs, tilted toward the viewer side).

**Plant LOD.**
- The grass preset sets the share of a patch drawn: Low 60 %, Medium 80 %, High 100 %. Off draws none.
- The draw distance is 1.1 × the grass radius.
- Past 40 % of the radius, a patch draws a shrinking prefix of its shuffled list, and the survivors widen slightly.

**Stones** are one merged mesh per patch, in the patch's frame:
- 8 shared low-poly stone shapes of 80 triangles each;
- per stone: scale, yaw, tilt, aspect and tint;
- aligned three-quarters to the ground normal and sunk slightly;
- shaded with pattern 29.

Stones get shadows and SSAO like any solid. Beyond 160 m + radius a stone patch is not drawn.

**Collision and navmesh.** Patches never collide and have no physics body, so the navmesh and AI ignore them
(`"nav": {"effect": "none"}`).

## Ground cover (V9)

Nine more plant patches and three wheat squares, all scatter patches like the stone patches. They follow the grass
quality preset (share drawn, draw distance, LOD; Off draws none).

| Asset | What | Species mix | Pebbles |
|---|---|---|---|
| `nat_brush_patchy` | Patchy brush: shrublets and tussocks in clumps, bare ground between | brush, straw, meadow grass, seed grass, tall grass | yes |
| `nat_brush_scrub` | Dry scrub on stony ground | brush, straw, seed grass | yes, more |
| `nat_brush_rank` | Rank brush at a hedge foot / ruin, up to ~1 m | brush, tall grass, weeds, meadow grass, cow parsley | yes, few |
| `nat_drygrass_tufts` | Yellowed tussocks of a dry pasture | meadow grass, straw, turf | yes, few |
| `nat_drygrass_tall` | Uncut grass gone yellow, 0.5–1.1 m | tall grass, seed grass, straw | no |
| `nat_drygrass_sparse` | Thin straw grass over stony ground | straw, meadow grass, seed grass | yes, many |
| `nat_clover_tufts` | Clover leaves, flowering clover, short lush tufts | weeds, clover, turf, meadow grass | no |
| `nat_green_tufts` | Lush green tussocks | meadow grass, turf, seed grass, weeds | no |
| `nat_meadow_mixed` | Unmown meadow: grasses of every height and wild flowers | 10 species | yes, few |
| `nat_wheat_field` | Ripe wheat, 10 m square, a few poppies and cornflowers | wheat | no |
| `nat_wheat_trampled` | Ripe wheat beaten down in swathes and trails | wheat | no |
| `nat_wheat_stubble` | Harvested: cut stubble, lying straw, stones | stubble | yes |

**New scatter keys:**
- `"kind": "grass"` / `"brush"`: the same plant path as `tallgrass`.
- `"shape": "square"`: a tile; `radius` is the half side, and plants grow right up to the straight edge (no ragged rim).
- `"dry": [min, max]`: the share of each plant's colour turned to straw, varying between min and max in world-space drifts.
- `"tint": {species: [r, g, b]}`: overrides a species' green.
- `"pebbles": {density, size, lith}`: a stone scatter under the plants, in the same merged-mesh style as the stone patches. It scales with the patch's density slider. It is there for the depth the pebbles add.
- Wheat only: `"state"` (`"ripe"` / `"trampled"` / `"stubble"`) and `"soil"` (the albedo of the litter layer).

**Species added:**
- **0–4, the pasture species** (turf, meadow grass, seed grass, weeds / clover leaves, straw), now usable in patches with their own heights.
- **13 wheat:** an even crop of 0.95–1.2 m straws, each with a plump ear of paired spikelets on its last 12 %. Seven straws per tuft, about 34 tufts per m². The wind sways it in waves rolling across the field along the wind direction, not per plant.
- **14 brush:** woody twigs with leaf clusters.
- **15 stubble:** cut straw, 8–18 cm.

**Wheat squares:**
- **Size.** 10 × 10 m, locked so tiles always line up. Each carries four edge sockets `"snap": [{p, dir, kind: "wheat"}]` (±5 m on x and z, `dir` outward) for V6's snapping.
- **Seams.** Neighbouring tiles mix their world position into the layout seed, so they never repeat. Colour drifts and the trampled pattern are world-space, so they run on across seams.
- **Litter layer.** A draped 1 m grid of soil and straw litter, 6 cm up, sits under the crop, with holes at craters and footprints. It is drawn to 480 m, so a field still reads gold beyond the stalks' range.
- **Draw range.** The stalks are drawn to 1.6 × the patch range.
- **Grass.** Pasture grass does not grow under the crop: the tile is a "soft" grass footprint, which other nature patches ignore, so poppies inside the field still grow.

**Flattening.** Tanks and explosions lay the wheat down (`nature.cpp`, the "wheat flattening" section):
- **Encoding.** Each stalk's amount (0–1) and direction are packed into the instance's `shape.y`. The shader tilts the straw up to 81° along that direction and damps its wind.
- **Explosions.** `App::explode` → `blastPaths` → `natureBlast`. The burst is radial: full within 0.4 × radius, fading to 1.15 × radius. Strength is `dmg / 120`, clamped to 0.45–1, and air bursts are reduced by their height.
- **Tanks.** Every 0.45 m a moving tank in Explore stamps its hull rectangle plus 0.1 m. Stalks fall along the direction of travel, splayed outward toward the hull sides.
- **Cost.** Stamps are recorded only near wheat. Each patch applies new stamps incrementally to its instance buffer (one sub-upload), and re-applies all of them after a rebuild.
- **Reset.** This is session state, like felled trees: `App::resetDamage` (F5, Return to start, entering or leaving Explore) stands the crop up again.
- **Built-in states.** Trampled fields bake swathes and winding trails. Ripe fields have a few lodged patches. Stubble has about 20 % lying straw.

## Small vegetation (foliage `"tree"` block)

Shrubs, ferns, nettles and saplings use the tree system: leaf cards, wind sway, felling.

| Group | Assets | Behaviour |
|---|---|---|
| Shrubs | hawthorn, bramble thicket, blackthorn, gorse | Canopy cards, 0.9–2.2 m. Nav `area: brush` (bots path round or through them at brush cost). The hawthorn's two stems stop a few rounds. |
| Ferns, bracken | fern clump, bracken | New `"fronds"` card style: 4-segment arching strips radiating from a crown, drawn with a rachis-and-pinnae mask (card `uv.x` offset 2). |
| Nettles | nettle clump | New `"nettles"` style: upright stem strips (`uv.x` offset 6) with opposite, decussate, drooping serrated leaves (`uv.x` offset 4). |

Ferns, bracken and nettles have no collider, and their nav effect is `none`. A blast fells them like any tree, and they
disappear.

Saplings have a thin trunk collider and nav `none`. A 6–7 cm stem is far below the agent radius.

**Tall grass and concealment.** Tall grass hides a crouched or prone soldier visually: the stems are 0.6–1.2 m and a prone
soldier is under 0.4 m. AI vision rules are unchanged.

## Performance

Measured by `--selftest-nature`. Results go to `selftest_out/nature_perf.json`.

The test places a 33-item dressing set (8 boulders / outcrops, 16 patches, 9 plants) in front of a fixed camera at Medium
grass. Each figure is averaged over 120 frames, before and after the set is placed.

The GPU is shared with other sessions' runs and the user's own instances, so absolute times drift. Compare the two
columns of one run.

Results of the run on 2026-11-06 (RTX 2080 Ti, Medium grass, 1920 × 1080). Each figure is the median of 3 × 60 frames,
taken interleaved without / with the set:

| Map | Draws | GPU ms | CPU ms | Frame ms |
|---|---|---|---|---|
| Village, without | 1928 | 6.26 | 8.13 | 8.8 |
| Village, with the 33 items | 2036 (+108) | 6.50 (+0.24) | 8.25 | 9.0 |
| Mont-Castre (cliff foot), without | 29 | 4.71 | 5.33 | 5.9 |
| Mont-Castre, with the 33 items | 137 (+108) | 5.19 (+0.48) | 5.75 | 6.4 |

**Where the 108 draws come from.**
- 13 plant patches: one instanced draw each, main pass only.
- The 20 solids (boulders, outcrops, stone patches, plant stems) are drawn in the main pass and in the shadow and
  sky-occlusion passes.

**Rebuilds.** Patches are built once, when the camera comes within about 140–160 m. After that they rebuild only:
- when their own instance is edited;
- when the footprint of a structure near them changes;
- when the ground under their items actually moves (a sampled height check on each terrain revision).

The suite checks that a still view causes no rebuilds.

A first, non-interleaved run measured +12 ms in the village. That was another process's GPU load: its baseline later read
77 ms GPU with nothing changed, and there were 0 patch rebuilds. This is why the measurement is interleaved now.
