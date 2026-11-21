# 01. Asset audit: what the supplied GLBs actually contain, and what to copy

Everything in this chapter is measured from the supplied files by parsing the glTF JSON chunk and the binary buffer directly. No figure here is recalled or estimated unless it says so. The point of the chapter is to replace the sentence "the house looks good" with a list of the specific, reproducible decisions that make it look good, and the specific decisions in it that are wrong and must not be copied.

## 1.1 Measured inventory

| File | Bounds (file units) | Tris | Primitives | Nodes | Materials | Images | Image MP | Exporter |
|---|---|---|---|---|---|---|---|---|
| `fantasy_house_with_interior.glb` | 2574 × 4059 × 2835 | 537,281 | 11,123 | 22,374 | 5 | 10 | 54.5 | Sketchfab 16.99 |
| `update_dirt_road_through_forest.glb` | 551 × 111 × 548 | 341,109 | 27 | 56 | 26 | 70 | — | Sketchfab 17.17 |
| `stone_with_moss.glb` | 2.34 × 1.12 × 2.13 | 372 | 1 | 4 | 1 | 3 | 50.3 | Sketchfab 12.66 |
| `old_stone_wall_full_of_moss_-_scan.glb` | 8.77 × 0.84 × 32.91 | 1,000,000 | 12 | 16 | 1 | 1 | 16.8 | Sketchfab 15.50 |
| `cc0__apple_moss_bartramia_pomiformis.glb` | 0.124 × 0.048 × 0.055 | 1,390,394 | 28 | 33 | 2 | 2 | 17.8 | Sketchfab 16.62 |
| `bridge.glb` | 5.48 × 6.10 × 16.72 | — | — | — | 1 | 2 | — | — |

Three of these are in metres already (`stone_with_moss`, `old_stone_wall`, `apple_moss`, `bridge`). Two are in arbitrary units.

### Unit calibration procedure

Do not guess scale from the overall bounds. Measure one object of known real size inside the file and divide:

```
s = L_real / L_file          s = scene scale factor, applied uniformly on import
```

| Scene | Calibration object | File size | Assumed real | s | Resulting scene size |
|---|---|---|---|---|---|
| House | total height 4059 u (tower house) | 4059 | ≈ 20 m | 0.0049 | 12.6 × 20 × 13.9 m |
| Road | `Wood_Log` minor axis 3.95 u (fallen log diameter) | 3.95 | ≈ 0.30 m | 0.076 | 41.9 × 8.4 × 41.6 m |

Both `s` values are *estimates from object identity*, not measurements. Verify by importing, placing a 1.8 m capsule, and checking door clearance and fence height. Everything downstream — texel density, moss patch size, ejecta scale, physical light units — is wrong by `s` if this step is skipped.

## 1.2 The house: five materials for 537k triangles

| # | Name | Base colour | Metal-rough | Normal | metallic | roughness | alphaMode | doubleSided | KHR specularFactor |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `tile` | 4096² | 2048² | 2048² | 0.0 | from tex | OPAQUE | true | 0.075 |
| 1 | `decal` | 4096² RGBA | — | — | 0.0 | 0.981 | **BLEND** | true | 0.075 |
| 2 | `uniq` | 2048² | 1024² | 1024² | — | — | OPAQUE | true | 0.045 |
| 3 | `ground` | 2048² | 1024² | 1024² | 0.0 | from tex | OPAQUE | true | 0.045 |
| 4 | `water` | — (factor 0.084, 0.089, 0.090) | — | — | 0.0 | 0.166 | OPAQUE | true | 0.373 |

This is the single most important fact in the file. **537,281 triangles, 11,123 primitives, five materials.** The naming convention encodes the assignment: every mesh is `OBJECTNAME_<materialgroup>_<index>`, e.g. `ROOF_low_poly_tile_0`, `BOOKS_low_poly_uniq_0`, `BOTTLES_low_poly_decal_0`.

The distribution across those five materials is extremely uneven, and reading it wrongly leads to the wrong conclusion about the asset:

| Material | Primitives | Triangles | % of total |
|---|---|---|---|
| 0 `tile` | 42 | 112,205 | 20.9 % |
| 1 `decal` (BLEND) | **11,061** | **325,386** | **60.6 %** |
| 2 `uniq` | 18 | 82,014 | 15.3 % |
| 3 `ground` | 1 | 8,680 | 1.6 % |
| 4 `water` | 1 | 8,996 | 1.7 % |

Sixty per cent of the house is one alpha-blended material in eleven thousand primitives. That looks like an enormous grunge-decal layer. **It is not.** Decomposing material 1 by mesh name:

| Group | Primitives | Triangles | % of 537,281 |
|---|---|---|---|
| `GROUND\|GRASS\|Dupli\|_decal` | 7,000 | — | — |
| `TREE_low_poly\|LEAVE\|Dupli\|_decal` | 3,200 | — | — |
| `BUSH_low_poly\|LEAVE\|Dupli\|_decal` | 850 | — | — |
| **All `Dupli` vegetation cards** | **11,050** | **320,500** | **59.7 %** |
| **Authored grunge decals** | **11** | **4,886** | **0.91 %** |

Median vegetation-card primitive: **40 triangles**. These are duplicated grass and leaf cards scattered around the building, exported as 11,050 separate primitives rather than instanced — which is also where the 22,374 nodes come from. They share the BLEND material with the grunge decals only because both are alpha-blended cut-outs on one atlas.

The eleven authored grunge decals, in full:

| Mesh | Tris | What it is |
|---|---|---|
| `BOTTLES_low_poly_decal_0` | 2,408 | prop cut-outs, not grunge |
| `LEAKING_WALL_decal_0` | 799 | the vertical streak band |
| `ALPHA_WINDOW_low_poly_decal_0` | 792 | glazing cut-outs |
| `LIANE_decal_0` | 346 | hanging stringy moss / vine |
| `MOSS_decal_0` | 338 | moss patches |
| `LEAKING_BOTTOM_decal_0` | 101 | the ground-contact waterline band |
| `ASH_decal_0` | 40 | soot |
| `GRASS_decal_0` | 40 | grass tufts |
| `LEAVE_1_decal_0`, `LEAVE_2_decal_0` | 20 | leaves |
| `PICTURE_low_poly_decal_0` | 2 | the painted banner |

Those names independently confirm the atlas reading below: `LEAKING_WALL` is the streak band, `LEAKING_BOTTOM` is the waterline band, `LIANE` is the hanging moss, `MOSS` is the patch set. The weathering decal set proper — `LEAKING_WALL`, `LEAKING_BOTTOM`, `LIANE`, `MOSS`, `ASH` — is **1,624 triangles, 0.30 % of the asset**.

Triangle distribution, top primitives:

| Mesh | Tris | Verts | Material |
|---|---|---|---|
| `BOOKS_low_poly_uniq_0` | 39,032 | 48,767 | uniq |
| `ROOF_low_poly_tile_0` | 24,929 | 41,336 | tile |
| `SCROLLS_low_poly_uniq_0` | 22,152 | 16,485 | uniq |
| `BOXS_low_poly_tile_0` | 14,592 | 23,990 | tile |
| `CARCASS_low_poly_tile_0` | 13,725 | 19,023 | tile |
| `STAIRS_MAIN_low_poly_tile_0` | 9,750 | 14,126 | tile |
| `WATER_water_0` | 8,996 | 4,880 | water |
| `GROUND_ground_0` | 8,680 | 4,473 | ground |
| `FLOOR_INSIDE_low_poly_tile_0` | 8,626 | 14,808 | tile |
| `STONE_WALL_low_poly_tile_0` | 8,092 | 16,170 | tile |
| `BOTTLES_low_poly_decal_0` | 2,408 | 1,881 | decal |

### The tile atlas is a packed library of seamless materials, not a trim sheet

Reading the 4096² base-colour image: it is divided into axis-aligned rectangles, each holding one **horizontally tileable** material. Measured UV ranges confirm the packing and the tiling:

| Mesh | u range | v range | Interpretation |
|---|---|---|---|
| `ROOF` | 0.01 … 0.99 | 0.50 … 0.62 | band occupying 12 % of v, tiled across full u |
| `BOXS` | 0.03 … 1.02 | 0.66 … 0.98 | band, slight u overrun |
| `STONE_WALL` | 0.70 … 0.89 | 0.02 … 0.24 | small square region |
| `FLOOR_INSIDE` | 0.37 … 0.91 | 0.27 … 0.47 | mid band |
| `CARCASS` | −0.16 … 1.63 | 0.64 … 0.99 | **tiled 1.8× horizontally** |
| `STAIRS_MAIN` | −0.84 … 1.93 | 0.65 … 0.99 | **tiled 2.8× horizontally** |

The bands visible in the atlas are: cream lime plaster (largest, ~40 % of area), dark lichened stone, rusted iron column, tan weathered plank, blue-grey weathered plank in two values, brown weathered plank (largest plank band). The plank bands are the **same plank pattern recoloured** — the pale blotch pattern and the knot/split lines are identical across the tan, blue-grey and brown variants.

Three rules follow, and they are the cheapest realism available:

1. **One atlas, one draw call, N materials.** Bands only need to tile along u; v is clamped by the band. Horizontal tiling is free because wrap mode is `REPEAT` (10497) on the single sampler and the band is full-width.
2. **Hue variation is free.** Three visually distinct woods cost one extra band each because they share the structure and differ only in a hue/value shift. Better: store one band and shift at runtime (section 2.6).
3. **The grunge is painted into the albedo of each band.** The pale mottling on the planks is the low-frequency variation that normally has to be added with a second texture. Baking it in costs nothing per pixel but loses the ability to vary it per instance; the house compensates with the decal layer.

### The decal atlas is the realism

The 4096² RGBA `decal` image is applied to separate, thin, double-sided, alpha-blended geometry laid over the base surfaces. Contents, by row (v descending from 1.0):

| v band | Content | Tiling | Role |
|---|---|---|---|
| 0.78 … 1.00 | Dark vertical water streaks over a pale ground, green algae wash at the lower edge | tiles in u | roof-drip line, under sills and beams |
| 0.66 … 0.78 | Green moss/algae horizontal band with ragged black drip fringe hanging downward | tiles in u | ground-contact damp line, waterline on walls |
| 0.44 … 0.66 | Four cut-out patches: crustose lichen plate, hanging stringy moss, two fern/foliage clusters | not tiling | point accents on stone and timber |
| 0.22 … 0.44 | Painted banner, root/grass clump cut-out, dark moss sheet | not tiling | hero accents |
| 0.00 … 0.22 | Large verdigris-copper and rust panel | tiles | metal overlay |

So the house's "blending of textures and grunge" is **not** a runtime mask blend. It is:

```
final look = tiling atlas material (albedo with low-frequency grunge painted in)
           + alpha-blended decal MESH offset a few mm along the surface normal
```

That is important because it is directly reproducible in any engine, needs no extra shader permutation, needs no second UV set, and the decal mesh follows curved and broken surfaces that a projected box decal cannot. Its costs are overdraw and a sorted transparent pass — both quantified in chapter 2.

### The three UV sets are duplicates

Every `tile` and `uniq` primitive carries `TEXCOORD_0`, `TEXCOORD_1` and `TEXCOORD_2`. Measured: all three have **identical ranges on every primitive tested**. They are an export artefact. Do not build a second-UV lightmap or grunge-mask pipeline on the assumption that the source had one — it did not. Strip UV1 and UV2 on import and save 16 bytes per vertex (979,482 verts × 16 B = 15.7 MB of buffer).

**This is true of the house and not of the road.** In `update_dirt_road_through_forest.glb`, three materials bind their normal map to `TEXCOORD_1`: material 0 `Background_Tree_Atlas`, material 11 `Fallen_Maple_Leaves`, and material 12 `Rock_Decal`. Stripping UV1 there silently breaks those three normal maps. Make the strip conditional: remove a UV set only when no material in the file references it *and* its values match UV0.

### Specular is deliberately suppressed, and overdone

`KHR_materials_specular` gives `F0_dielectric = 0.04 × specularFactor`:

| Material | specularFactor | Resulting F0 | Physical F0 for this class |
|---|---|---|---|
| tile (plaster, wood, stone) | 0.075 | 0.0030 | 0.02 – 0.04 |
| uniq (props) | 0.045 | 0.0018 | 0.02 – 0.05 |
| ground | 0.045 | 0.0018 | 0.02 – 0.03 |
| water | 0.373 | 0.0149 | 0.020 |

Every surface is 10–20× below its physical Fresnel reflectance. This is an over-correction for the "everything looks like wet plastic" failure, and it is why the asset reads as matte and grounded. Copy the *intent*, not the number: **F0 = 0.02–0.035 for aged porous dielectrics** and let high roughness, not a crushed F0, remove the shine. Crushing F0 kills grazing-angle Fresnel, which is what makes a wall look like it has a surface at all (chapter 03 of volume 1).

### Water is vertex-coloured and untextured

`WATER_water_0`: 8,996 tris, `COLOR_0` present with mean (0.332, 0.332, 0.332, 1.0), no textures, baseColorFactor (0.084, 0.089, 0.090), roughness 0.166. A dark, slightly blue-green, fairly smooth surface with per-vertex depth painted in. No normal animation, no refraction, no depth absorption. This is a placeholder; see volume 1 chapter 11.7 for what water needs.

## 1.3 The dirt road: a layered ground built from 26 overlapping meshes

This file is the answer to "the ground looks flat". It contains no terrain and no splat map. It contains **26 materials on 27 primitives**, every one of which carries `COLOR_0`, and the material names form a complete taxonomy of a realistic forest floor:

| Material | Tris | alphaMode | Layer role |
|---|---|---|---|
| `Dirt_Road_Bare` | — | OPAQUE | **base** substrate |
| `Ground_Dirt` | 3,487 | OPAQUE | base substrate, off-road |
| `Mud_Pile` | 1,509 | OPAQUE | raised form |
| `Sloped_Rock` | 10,287 | OPAQUE | raised form |
| `Tall_Cliff` | 7,820 | OPAQUE | raised form |
| `Broken_Rocks` | 4,228 | OPAQUE | raised form |
| `Cobblestone` | 905 | OPAQUE | raised form |
| `Terrain_Far` | 2,644 | OPAQUE | distant shell, albedo only |
| `Dirt_Road` | 10,052 | **BLEND** | road surface laid over the base |
| `Dirt_Road_Trails` | 5,292 | **BLEND** | wheel ruts laid over the road |
| `Road_Edge_Gravel_Dusty` | 3,126 | **BLEND** | road-to-verge transition |
| `Puddle_Streaks` | 3,693 | **BLEND** | standing water, roughness 0.778 |
| `Rock_Decal` | 20,529 | **BLEND** | embedded stones |
| `Fallen_Maple_Leaves` | 11,505 | **BLEND** | litter |
| `Fallen_Generic_Leaves` | 7,649 | **BLEND** | litter |
| `Grass_Close` | 7,328 | OPAQUE | near grass clumps |
| `Aerial_Grass` | 10,729 | OPAQUE | grass seen from above |
| `Grass_Vegetation_Green` | 16,726 | BLEND | grass cards |
| `Grass_Vegetation_Dry` | 26,126 | BLEND | grass cards |
| `Forest_Bush` | 8,570 | BLEND | bushes |
| `Background_Tree_Atlas` | 93,803 | BLEND | canopy cards |
| `Trunk_Oak` | 70,153 | OPAQUE | trunks |
| `Trunk_Birch` | 12,921 | OPAQUE | trunks |
| `Wood_Log`, `Wood_Fence`, `Metal_Fence` | 1,550 | mixed | props |

Three findings to carry forward:

- **The road itself is a BLEND layer.** It is not the base surface; it is painted over `Dirt_Road_Bare` as transparent geometry with a soft alpha edge. So is every transition, rut, puddle and embedded stone. The flatness problem is solved by *stacking*, not by a better single texture.
- **`Grass_Vegetation_Dry` and `Grass_Vegetation_Green` share texture 26 (metal-rough) and texture 27 (normal)** and differ only in base colour. One seasonal variant costs one albedo map, not three.
- **Vertex colour is on every primitive** and is what fades the blend layers at their edges. A low-resolution per-vertex alpha driving a height-aware blend (section 2.3) produces a crisp, detailed boundary for almost no memory.

Triangle cost is dominated by vegetation and trunks: 220k of 341k triangles (65 %) are trees, grass and bushes. The ground layers themselves total about 66k triangles over a ~42 × 42 m area, i.e. **about 37 tris/m² of ground**, which is cheap.

## 1.4 The three moss and stone assets, and what is wrong with them

| Asset | Tris | Real size | Tris per m of longest axis | Texture | Texel density |
|---|---|---|---|---|---|
| `stone_with_moss` | 372 | 2.34 × 1.12 × 2.13 m | 159 | 3 × 4096² | **1448 px/m** |
| `old_stone_wall_full_of_moss` | 1,000,000 | 8.77 × 0.84 × 32.91 m | 30,400 | 1 × 4096² (albedo, also bound as emissive) | **230 px/m** |
| `cc0__apple_moss_bartramia` | 1,390,394 | 0.124 × 0.048 × 0.055 m | 11,213,000 | 2 images, 17.8 MP, `KHR_materials_unlit` | ~20,000 px/m |

Texel density is `sqrt(total_texels / surface_area_m²)` in texels per linear metre. Targets for 1080p at 60–70° horizontal FOV, where a 1 m object filling a third of the screen subtends 640 px:

| Surface class | Viewing range | Target px/m | 2048² covers | 4096² covers |
|---|---|---|---|---|
| Hero prop, weapon in hand | 0.3–1 m | 2048 | 1.0 m² | 4.0 m² |
| Interior wall, floor, door | 1–3 m | 512 | 16 m² | 64 m² |
| Exterior building wall | 3–15 m | 256 | 64 m² | 256 m² |
| Ground under foot | 1–4 m | 512 | 16 m² | 64 m² |
| Ground mid-distance | 4–30 m | 128 | 256 m² | 1024 m² |
| Terrain far | 30 m + | 16 | 16,384 m² | 65,536 m² |
| Decal / grunge overlay | 1–5 m | 256 | 64 m² | 256 m² |

Measured against those targets:

| Asset | px/m | vs target | Verdict |
|---|---|---|---|
| `stone_with_moss` | 1448 | 2.8× the 512 target | Over-budget on texture, perfect on geometry. **Copy this ratio.** |
| House tile atlas | 547 | 1.07× | Correct. |
| House ground | 324 | 0.63× | Under-budget — a contributor to the flat ground. |
| `old_stone_wall` scan | 230 | 0.45× | Raw scan: geometry is 30,000 tris/m where 300 would do, and the texture is half the density it should be. Invert the balance. |
| `apple_moss` | ~20,000 | 40× | A 12 cm clump with 1.39 M triangles. Unusable as-is. |

`cc0__apple_moss_bartramia` uses `KHR_materials_unlit`: the lighting is baked into its albedo. Under any directional light it will not respond, and against lit neighbours it will read as a flat sticker. It must be de-lit (divide out the baked irradiance, or re-author albedo) before use. Chapter 03 gives the pipeline.

`old_stone_wall` binds image 0 as **both** base colour and emissive. Left as-is, the wall glows with its own albedo. Clear the emissive slot on import.

## 1.4a Licensing

The GLBs carry embedded author and licence metadata. Two of the six are **non-commercial**:

| Asset | Licence |
|---|---|
| `fantasy_house_with_interior.glb` | **CC-BY-NC-4.0** |
| `stone_with_moss.glb` | **CC-BY-NC-4.0** |
| `update_dirt_road_through_forest.glb` | CC-BY-4.0 |
| `old_stone_wall_full_of_moss_-_scan.glb` | CC-BY-4.0 |
| `bridge.glb` | CC-BY-4.0 |
| `cc0__apple_moss_bartramia_pomiformis.glb` | CC0-1.0 |

The two NC assets are usable as references and for an internal art test, and **not shippable in a commercial build**. The supplied reference photographs have no verified terms in the bundle. Treat the house and the mossy rock as material language to imitate with original geometry, which is what chapters 02 and 03 describe. Confirm attribution requirements for the CC-BY assets before shipping.

## 1.5 Draw calls and the import pipeline

The house has 11,123 primitives and 22,374 nodes against 5 materials. One draw call per primitive is 11,123 draw calls, which at a typical 20–50 µs of CPU per call is 220–550 ms per frame. The file must be re-authored on import.

**The dominant problem is the 11,050 un-instanced vegetation cards**, not the house itself. The building — tile, uniq, ground, water and the eleven authored decals — is 60 primitives and 216,781 triangles. Everything else is grass and leaves exported one card at a time. Handle them separately: either instance them off a single card mesh with a per-instance transform buffer, or discard them entirely and re-scatter with the engine's own foliage system (chapter 04, section 4.7), which is what they should have been.

```
IMPORT PIPELINE (apply to every supplied GLB)

1  Scale:          multiply all node translations and mesh positions by s (section 1.1).
2  Strip:          remove a UV set only if NO material references it and its
                   values match UV0. (House: UV1/UV2 are safe to strip.
                   Road: materials 0, 11, 12 bind their normal map to UV1.)
                   clear emissiveTexture where it equals baseColorTexture.
3  Flatten:        bake node transforms into vertices; discard the node hierarchy
                   except for anything that must animate.
4  Merge:          group primitives by (material, alphaMode, 32 m spatial cell).
                   Concatenate index and vertex buffers per group.
                   Result for the house: ~5 materials x ~2 cells = ~10 draw calls.
5  Split blended:  keep BLEND groups as separate draw calls; they must sort.
6  Re-bound:       recompute per-group AABB for culling.
7  LOD:            generate LOD1 at 50 % tris, LOD2 at 20 %, LOD3 at 5 % or an impostor.
8  Compress:       BC7 for base colour, BC5 for normal (2-channel), BC4 per packed
                   grayscale channel, or BC7 for a packed ORM.
```

Expected result for the house:

| Stage | Draw calls | Tris | Vertex bytes |
|---|---|---|---|
| As supplied | 11,123 | 537,281 | ~47 MB (pos+nrm+tan+3×uv) |
| After steps 2–4, vegetation instanced | ~12 | 537,281 | ~17 MB |
| After step 7, LOD1 | ~10 | 268,000 | ~16 MB |

## 1.6 What to copy, and what not to

**Copy:**

| Decision | Measured evidence | Why it works |
|---|---|---|
| One tiling-material atlas for all architecture | 4096², ~14 bands, 547 px/m, 1 sampler | 1 draw call, 1 shader, hue variants nearly free |
| Low-frequency grunge painted into every tiling band | Pale mottling on all plank bands | Adds the 1–10 m scale of variation a tiling texture cannot have |
| Separate alpha-blended decal **mesh** for streaks, moss and damp | 5 weathering decal meshes, 1,624 tris total (0.30 %), on a 4096² RGBA atlas | Follows arbitrary surfaces, no second UV, no shader permutation |
| Tileable-in-u streak and waterline decal bands | Two full-width bands in the decal atlas | One asset serves every wall of every length |
| Stacked BLEND ground layers over an OPAQUE base | 10 of 26 road materials are BLEND | The only way a ground stops looking like one texture |
| Shared normal/ORM between seasonal albedo variants | Grass dry/green share tex26, tex27 | 3× memory saving per variant |
| Heavy geometric budget on a few hero rocks | 372 tris + 3 × 4096² on `stone_with_moss` | Silhouette from mesh, detail from texture |
| metallic = 0 everywhere non-metal, roughness from texture | All 5 house materials | No accidental metal, no uniform gloss |

**Do not copy:**

| Decision | Measured evidence | Replace with |
|---|---|---|
| F0 = 0.003 | specularFactor 0.075 and 0.045 | F0 = 0.02–0.035, raise roughness instead |
| Unique hand-painted ground map at 324 px/m | `GROUND_ground_0`, 2048² over ~40 m² | Layered tiling + height blend + detail (chapter 04) |
| 11,123 primitives / 22,374 nodes | parsed counts | Merge to ~10 draw calls |
| 11,050 vegetation cards exported one primitive at a time | parsed counts; 59.7 % of all triangles | Instance them, or re-scatter with the engine's foliage system |
| Sharing one BLEND material between vegetation cards and weathering decals | material 1 serves both | Split them: they have different draw order, LOD and culling needs |
| Duplicated UV1 / UV2 | identical ranges on all tested prims | Strip |
| `KHR_materials_unlit` moss | `apple_moss` materials 0 and 1 | De-light and shade normally (chapter 03) |
| 1 M-tri and 1.39 M-tri scans used directly | parsed counts | Decimate to the budgets in chapter 03 |
| Albedo bound as emissive | `old_stone_wall` material 0 | Clear the slot |
| Untextured vertex-colour water | `WATER_water_0` | Volume 1 chapter 11.7 |

## Rules of thumb

- Five materials for half a million triangles is the correct target for a building. If a building needs more than eight, the atlas is wrong.
- A tiling atlas band only has to tile in one direction. Pack bands full-width and clamp v.
- Grunge that must vary per instance goes on decal geometry. Grunge that never varies goes into the atlas albedo.
- Texel density is a ratio, not a resolution. 4096² on a 2 m rock is right; 4096² on a 33 m wall is half of right.
- A scan gives you a silhouette and a texture. It does not give you a game asset until the triangle count matches the silhouette and the texel density matches the viewing distance.
- Check `KHR_materials_unlit` and emissive bindings on every imported scan before judging how it looks.

## Sources

- All numeric claims in this chapter are parsed from the supplied `.glb` files: glTF 2.0 JSON chunk (accessors, meshes, materials, images, samplers, textures, nodes) and the binary chunk, with world transforms composed through the node hierarchy. Texel densities are computed as `sqrt(texels / triangulated world surface area)` sampling up to 2000 triangles per primitive.
- glTF 2.0 specification and `KHR_materials_specular` (`dielectric_f0 = 0.04 × specularFactor × specularColorFactor`): https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- Texel-density targets and block-compression sizes follow volume 1 chapter 07.
- Unit-scale factors `s` are inferred from object identity and are marked as estimates.
