# Ground and grass (Phase 2 V1, first pass)

The brief is `GROUND_AND_GRASS.md` in the Phase 2 package. Its target image is `references/grass_target.png`, a screenshot of a commercial engine with photographed foliage assets.

**This pass is procedural and does not match the reference.** No realistic foliage or terrain textures came with the package. Every blade and every ground pattern here is generated in code, so the result is closer to the target than before but clearly not equivalent:
- fewer blades per square metre;
- no photographed albedo or normal maps;
- no alpha-cutout clusters.

Matching the reference would take scanned grass-cluster meshes or cards, plus tiling ground texture sets (base colour, normal, roughness) for turf, soil, mud, gravel and litter. Those are the missing assets.

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
