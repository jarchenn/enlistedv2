# Ground and grass (Phase 2 V1, first pass)

The brief is `GROUND_AND_GRASS.md` in the Phase 2 package. Its target image is `references/grass_target.png`, a screenshot of a commercial engine with photographed foliage assets.

**This pass is procedural and does not match the reference.** No realistic foliage or terrain textures came with the package. Every blade and every ground pattern here is generated in code, so the result is closer to the target than before but clearly not equivalent:
- fewer blades per square metre;
- no photographed albedo or normal maps;
- no alpha-cutout clusters.

Matching the reference would take scanned grass-cluster meshes or cards, plus tiling ground texture sets (base colour, normal, roughness) for turf, soil, mud, gravel and litter. Those are the missing assets.

## GX3: clumps and ground cover (graphics overhaul §4, 2026-11)

This section replaces the first-pass design below where they differ (the first-pass text is kept for history).

**Clumps.** One instance is a clump of 12 crossed cards (`Grass::kBlades`), pre-arranged around the root: tall in the
middle, shorter and leaning outward at the rim (a fountain), so neighbouring clumps overlap into a continuous sward.
The first 8 cards have 4 segments, the 4 short rim cards 2. Nature patches (`nature.cpp`) keep their 7-blade plants
(`Grass::kPlantBlades`) and the old shader path.

**Species layers** (noise masks, `Grass::build`), each with its own procedural card texture in `kGrassFS`:

| Id | Layer | Cards | Texture |
|---|---|---|---|
| 0 | fine turf (base) | 12 blades, 0.2–0.34 m | midrib, darker margins, veins, some browned tips |
| 1 | meadow grass (dominant) | 12 blades, 0.34–0.62 m, drooping rim | as above |
| 2 | seed-head grass (clumpy patches, verges) | 3 culms with a spike or open panicle + 9 leaves | straw / brown heads |
| 3 | clover (small colonies) | 3 trifoliate leaves (9 leaflets) + 3 grass blades | notched leaflets, pale chevron |
| 17 | broadleaf rosette (plantain) | 7 lanceolate leaves + 2 seed spikes + 3 blades | parallel veins |
| 16 | small shrub (rare, more near walls) | 12 leafy twig cards in a dome | alternate oval leaves on a woody twig |
| 4 | dry straw (dry patches, painted dry grass) | 12 curled blades | straw |

**Colour.** Per clump: the meadow palette plus a random tint along green → yellow-green → straw. Per card: value and
an occasional yellower card. Per vertex: darker, more saturated base → lighter, drier tip; the lower 30 % blends into
the ground colour under the clump (a CPU port of `kTerrainFS`'s palette, bare soil, wear, craters and painted ground,
packed per instance), so grass and ground never disagree at the root.

**Shading.** Base AO darker inside the clump and under the canopy (`pastureClump`), translucency when looking toward
the sun, and sun shadows cast by the clumps within `shadowRadius` (`Grass::drawShadow`, the near cascade; the tall 8
cards only). Blades sample the sun map 5 cm toward the sun so they do not shadow themselves.

**Placement.** Clumps lean with the terrain normal (packed per instance) and sit on the slope plane; the sward thins
toward the 0.8 normal cut-off; natural bare-earth patches (the `bare` field of `kTerrainFS`, now 0.65 soil) and
painted paths thin it; it grows back from footprint edges over 2–3 m (short first).

**LOD (no pop).** Instances are generated at 44 per m² in a shuffled order. Within `nearRadius` a tier draws its full
density; beyond it the drawn share falls as (nearRadius / d)^falloff (floor `floor`) and clumps keep fewer cards
(`midBlades` by `midRadius`, `farBlades` at `radius`), the rest widening to keep coverage. Each chunk draws the prefix
for its nearest point; the vertex shader fades every clump and card by its own rank at its own distance, so nothing
pops as the eye moves. Beyond `radius` the grass has shrunk into the tinted ground (moss / dirt near, the meadow palette
far: `terrain_grass` = `grass_forest`, ambientCG Ground037, CC0).

**Wind.** Vertex-shader wind only within `windRadius` (13 m Medium, 15 m High, none on Low); further out the clumps
keep their static lean. Settings → Graphics → Grass wind turns it off (`grassWind`).

**Tiers:** see [G1](#g1-species-cards-shadows-ultra-and-regions) below (GX3 drew 12 cards near; mid / far 5 / 3, 6 / 3, 8 / 4; shadows 10 / 14 m cut per chunk).

**Test field.** `NMK_CAPTURE_SET=grass --capture-materials <tag>`: a 40 x 40 m pasture with a painted dirt path, a dry
patch and a bunker from six cameras; each logs GPU ms with grass on and off, blades drawn, cards per m² within 15 m
and the clumps drawn into the sun map.

## G1: species cards, shadows, Ultra and regions (2026-11)

**Species groups.** Each chunk stores its clumps in two ranges, each in the original shuffled order: the coarse species
(meadow 1, seed-head 2, straw 4, shrub 16, broadleaf 17, dune grass 18) first, then short turf (0) and clover (3)
(`Grass::fineSpecies`, `Chunk::coarse`). Each range is one instanced draw with its own card budget (`cards` /
`fineCards`), so a chunk costs two draws in the main pass and one in the shadow pass. Thinning stays a nested prefix
of each range, so raising the tier only adds clumps and nothing reshuffles; the vertex shader still fades every clump
and card by its own rank and distance (no pop, no hysteresis needed: the per-chunk counts are only upper bounds).

**Cards.** Blade cards (role 10) follow the pointed blade silhouette in the vertex shader (`tap`), so the transparent
corners of the old rectangular cards are no longer rasterised; the fragment shader rescales its across-blade
coordinate (`vTap`) and keeps the same midrib / margin texture. The alpha test already runs first in every card branch,
before the shadow, occlusion, probe and lamp work.

**Shadows.** Only coarse clumps cast, `shadowShare` of the drawn ones (a nested prefix), with `shadowCards` cards, and
they shrink away over the last 30 % of `shadowRadius` per clump. (GX3 cut the shadow radius per chunk, so a chunk whose
nearest corner was inside 10 m cast all of its clumps out to ~27 m.) Every clump still receives shadows. Chunks are
culled against the eye frustum with bounds grown by their tallest clump's bend and wind (+3 m in the shadow pass for
the casting reach).

**Tiers** (`content/settings/grass.json`, read at start-up):

| Tier | Clumps / m² near | Near / mid / radius | Floor | Cards coarse | Cards turf / clover | Wind | Shadows (cards, share) |
|---|---|---|---|---|---|---|---|
| Low | 12 | 10 / 22 / 36 m | 12 % | 12 / 5 / 3 | 12 / 5 / 3 | off | off |
| Medium | 24 | 15 / 32 / 48 m | 14 % | 8 / 4 / 2 | 6 / 3 / 2 | 13 m | 7 m (3, all) |
| High | 30 | 18 / 40 / 54 m | 12 % | 10 / 6 / 3 | 7 / 4 / 2 | 15 m | 9 m (4, all) |
| Ultra | 38 | 20 / 44 / 62 m | 13 % | 10 / 6 / 3 | 7 / 4 / 2 | 19 m | 11 m (3, 80 %) |

**Ultra** is `GrassQuality` 4, appended so saved settings 0-3 keep their meaning (Settings → Graphics → Grass, `--grass
ultra`, the editor's View menu). It is grass only: no graphics preset selects it and it changes no shadow map size,
probe rate, MSAA or light budget; nature patches draw as on High. Placement is the same for every tier (instances are
generated at 44 / m²), so gameplay and networked state never depend on it.

**Measuring.** `--bench-grass`: an open pasture, standing (1.7 m), prone (0.35 m) and a 4.5 m/s walk at every tier from
Medium; per view it alternates `Renderer::perf.grassShadow` (`NMK_PERF_AB=grassshadow` elsewhere) so the grass share of
the near cascade is the on - off difference. Writes `selftest_out/benchmark_grassbench.json` and standing / prone shots.

### Regions (D-day and other maps)

Grass follows ground paint, surface wear, disturbed soil, footprints and slope; no hand-placed exemptions:
- **Beach:** paint the wet beach and dry sand with `sand` at full weight: no grass. Sand within 1.5 m above the water
  line stays bare even where the paint is partial, so the surf and wet sand never grow grass on any tier.
- **Dunes / upper sand:** paint `sand` at weight ~0.4-0.7 (`paint.poly("sand", ..., weight=0.6)`, or a route with
  `strength=0.6`): the remaining share grows sparse, clumped marram tussocks (species 18, 0.45-0.85 m, grey-green, stiff).
- **Bunker slopes, berms, banks:** ground steeper than ~16° (normal y < 0.96) shifts the mix to rough grass: more
  seed-head grass, straw and small shrubs, less turf and clover; it thins out toward the 37° cut-off (normal y 0.8).
  Earthwork stamps and walls also grow taller weeds at their feet (the GX3 `wall` term).
- **Village meadow / gardens:** unpainted ground is the full meadow mix; `dry_grass` gives drier, sparser straw;
  nature patches add flowers and tall meadow.
- **Keep clear:** trench floors (GX1 trench soil counts as disturbed soil: no grass), roads and tracks (`route` wear,
  or `cobbles` / `gravel` / `dirt` paint), doorways and steps (footprint margins), gun platforms (their footprints).
  Spawn exits and other open ground that must stay clear: paint `dirt`, `gravel` or `mud` there (grass 0), or
  `leaf_litter` (10 % grass) to keep it sparse.

## Grass (`src/grass.{h,cpp}`, `kGrassVS/FS` in `src/render.cpp`)

**Density.**
- Tufts are generated once per 8 m chunk at `kMaxDensity` = 40 per m², in a shuffled order.
- The presets draw a prefix of that order:

| Preset | Tufts drawn per m² | Draw radius | LOD start |
|---|---|---|---|
| Low | 8 | 38 m | 14 m |
| Medium | 16 | 58 m | 20 m |
| High | 40 | 85 m | 30 m |

- Each tuft has 7 narrow, tapered, curved blades with per-blade length, lean and yaw.
- Beyond the LOD start, chunks draw fewer tufts and fewer blades, and the remaining blades widen to keep the same coverage.

**Species.** Five species, chosen from correlated noise fields rather than per blade, so they form patches:

| Species | Height | Shape | Wind |
|---|---|---|---|
| 0 short turf (dense base) | 0.09–0.19 m | wide spread, strong curl | barely moves |
| 1 medium meadow grass | 0.26–0.54 m × patch factor | tapered blades | normal |
| 2 tall stems | 0.55–0.95 m | 3 thin stems with oval seed heads | sways most, slower |
| 3 clover / weeds | 0.05–0.09 m | rounded leaves lying low | almost none |
| 4 dry straw | 0.25–0.50 m | pale, strong bend | normal |

**Where each species grows.**
- Tall grass grows in broad patches, on neglected verges just outside worn routes, and against walls and foundations.
- Trodden ground is cropped short.
- Bare spots, steep banks, water edges, craters, foundations, paths and wire footprints get no grass.

**Colour.**
- Colour follows plant clusters (broad fields, then patches, then the species tint), never a per-blade random colour.
- Roots are shaded and tips are sunlit; nothing is bleached.

**Wind.**
- The wind is rooted: the root vertex never moves.
- Each species has its own amplitude and frequency.
- The phase varies with position, so gusts move across the field without the blades swaying in step.

**Craters.**
- Craters still rebuild only the affected chunks, from stable seeds.
- Measured: 2 chunks rebuilt in 7.2 ms.
- Grass inside new soil is removed and the survivors sit on the new surface (`--selftest-grass`).

## Ground (`kTerrainFS`)

**Base turf colour** comes from noise at four scales: 80 m fields, 14 m patches, 1 m clumps and 0.2 m grit.

**Near-field sward detail.** Within 25 m, fading out by 70 m so it never shimmers at distance, the ground also gets:
- blade-scale streaks along a meandering lay;
- shadowed sward depth between plants, so the gaps between tufts read as grass rather than a flat plane;
- flattened, lighter patches;
- organic litter;
- exposed soil;
- normal relief, for shading only: collision is unchanged.

**Other surfaces.** The existing masks still blend turf into worn tracks, ruts, mud, puddles (the only low-roughness surface), crater soil and the path entities (E4).

## Not done in this pass

- Tank bending or flattening of grass.
- Walls, interiors, furniture and room templates (the second half of V1).
- Settings for species weights, height ranges and ground texture scale. They are constants in `grass.cpp` and `kTerrainFS`. Density, draw radius and LOD are editable through the preset (View → Grass).
- A dedicated comparison test area with fixed before/after captures from several viewpoints. The current visual check is the grass suite's `grass_crater_before.png`.

## Performance

See the measurement section below. It is filled in from `--benchmark-stress` (grass flyover).
