# ambientCG texture manifest

All sets in `content/textures/` (109: 62 from L4, 47 added by MA1 on 2026-11-09). Source: [ambientCG](https://ambientcg.com),
licence **CC0 1.0 Universal** (no attribution required; credited as "ambientCG.com" in each `meta.json`). Every set
was fetched with `tools/fetch_ambientcg.py` through the official API (`https://ambientcg.com/api/v2/full_json`) as the
**1K-JPG** download; the packer keeps Color (sRGB), NormalGL (OpenGL, green up), Roughness, AmbientOcclusion and
Displacement (linear) into `color.jpg`, `normal.jpg` and `rma.jpg`. Machine-readable copy: `content/textures/manifest.json`
(regenerate both with `node tools/textures_manifest.mjs docs/textures-manifest.table.md`).

## MA1 acquisition (2026-11-09)

- **Inspected:** about 430 candidate previews on contact sheets built from the API (queries: stone wall, bricks, plaster,
  concrete, roofing, wood, tiles, metal, painted, ground, fabric, wallpaper, paper, burlap), photo-scanned and
  procedural ("p") sets alike.
- **Chosen and downloaded:** 47 sets, 358 MB of zips, **47 / 47 succeeded** (raw cache `%TEMP%/nmk/ambientcg_raw`,
  outside the repo). One more choice (Metal041B, galvanised) was already in the library as `iron_rust`, one
  (PaintedMetal014) as `paint_metal_grey`; they were not downloaded again. No query failed; "burlap" found no materials.
- **Physical size:** ambientCG lists no dimensions for these materials, so each MA1 tile size is an estimate from the
  preview (course / board / tile counts), recorded per set; `limestone_coursed` and `limestone_dressed` were corrected
  to 3 m after the first in-game check.
- **In use:** 39 of the 47 through `textures.json` (imgmat table). Not assigned yet, kept for MA2 / MA3 foundation work and
  variation: `stone_grey_coursed`, `plaster_flaking_b`, `slate_grey`, `clay_tile_mossy`, `paint_wood_blue`,
  `floor_boards_worn`, `floor_cement_tile`, `ground_mossy`. Unassigned sets take no GPU memory.

## Sets
| Set | ambientCG id | Family | Maps | Tile (m) | Slots (base; imgmat:) | Batch |
|---|---|---|---|---|---|---|
| plaster_old | [Plaster007](https://ambientcg.com/a/Plaster007) | limewash / plaster | ao, color, height, normal, roughness | 2 x 2 | limewash | L4 |
| plaster_painted | [PaintedPlaster017](https://ambientcg.com/a/PaintedPlaster017) | limewash / plaster | color, height, normal, roughness | 2 x 2 | interior | L4 |
| plaster_rough | [Plaster003](https://ambientcg.com/a/Plaster003) | limewash / plaster | color, height, normal, roughness | 2 x 2 | plaster | L4 |
| sandstone_wall | [Concrete012](https://ambientcg.com/a/Concrete012) | ashlar | color, height, normal, roughness | 2 x 2 | sandstone, stone_rubble | L4 |
| brick_old | [Bricks094](https://ambientcg.com/a/Bricks094) | brick | ao, color, height, normal, roughness | 1.8 x 0.9 | brick | L4 |
| brick_factory | [Bricks085](https://ambientcg.com/a/Bricks085) | brick | ao, color, height, normal, roughness | 2.4 x 1.2 | (unused) | L4 |
| brick_damaged | [Bricks097](https://ambientcg.com/a/Bricks097) | brick | ao, color, height, normal, roughness | 2 x 2 | (unused) | L4 |
| limestone_smooth | [Rock042S](https://ambientcg.com/a/Rock042S) | ashlar | ao, color, height, normal, roughness | 1.2 x 1.2 | ashlar | L4 |
| limestone_light | [Rock019](https://ambientcg.com/a/Rock019) | rubble stone | ao, color, height, normal, roughness | 2 x 2 | stone_coursed | L4 |
| rock_grey | [Rock026](https://ambientcg.com/a/Rock026) | rubble stone | ao, color, height, normal, roughness | 2 x 2 | (unused) | L4 |
| cobbles_grey | [PavingStones046](https://ambientcg.com/a/PavingStones046) | cobbles | ao, color, height, normal, roughness | 2 x 2 | (unused) | L4 |
| cobbles_old | [PavingStones138](https://ambientcg.com/a/PavingStones138) | cobbles | ao, color, height, normal, roughness | 1.25 x 2.5 | paint_cobbles | L4 |
| slate_roof | [RoofingTiles001](https://ambientcg.com/a/RoofingTiles001) | slate | color, height, normal, roughness | 2 x 2 | (unused) | L4 |
| slate_square | [RoofingTiles003](https://ambientcg.com/a/RoofingTiles003) | slate | color, height, normal, roughness | 2 x 2 | slate | L4 |
| roof_clay_old | [RoofingTiles014B](https://ambientcg.com/a/RoofingTiles014B) | terracotta | ao, color, height, normal, roughness | 2.9 x 2.9 | roof_tile | L4 |
| roof_clay | [RoofingTiles013A](https://ambientcg.com/a/RoofingTiles013A) | terracotta | ao, color, height, normal, roughness | 2.9 x 2.9 | (unused) | L4 |
| clay_flat | [Clay005](https://ambientcg.com/a/Clay005) | terracotta | color, height, normal, roughness | 0.4 x 0.4 | brickwork, terracotta | L4 |
| wood_old_planks | [Wood080](https://ambientcg.com/a/Wood080) | planks | ao, color, height, normal, roughness | 1.5 x 1.5 | wood_raw | L4 |
| planks_raw | [Planks021](https://ambientcg.com/a/Planks021) | planks | ao, color, height, normal, roughness | 1.4 x 1.4 | crate_wood | L4 |
| wood_oak | [Wood049](https://ambientcg.com/a/Wood049) | planks | color, height, normal, roughness | 0.8 x 0.8 | joinery | L4 |
| wood_dark | [Wood051](https://ambientcg.com/a/Wood051) | planks | color, height, normal, roughness | 0.8 x 0.8 | joinery_dark, wood | L4 |
| paint_wood_old | [PaintedWood005](https://ambientcg.com/a/PaintedWood005) | painted wood | color, height, normal, roughness | 1 x 1 | (unused) | L4 |
| paint_wood_scratched | [PaintedWood009B](https://ambientcg.com/a/PaintedWood009B) | painted wood | ao, color, height, normal, roughness | 1 x 1 | painted_wood, paintwood | L4 |
| bark_fir | [Bark014](https://ambientcg.com/a/Bark014) | planks | ao, color, height, normal, roughness | 1.2 x 1.2 | bark | L4 |
| concrete_bunker | [Concrete028](https://ambientcg.com/a/Concrete028) | concrete | ao, color, height, normal, roughness | 2 x 2 | bunker | L4 |
| concrete_bunker_mold | [Concrete032](https://ambientcg.com/a/Concrete032) | concrete | ao, color, height, normal, roughness | 2 x 2 | bunker_wash | L4 |
| concrete_rough | [Concrete037](https://ambientcg.com/a/Concrete037) | concrete | ao, color, height, normal, roughness | 2 x 2 | concrete | L4 |
| concrete_light | [Concrete025](https://ambientcg.com/a/Concrete025) | concrete | ao, color, height, normal, roughness | 2 x 2 | concrete_light, path_concrete | L4 |
| steel_rusty | [Metal021](https://ambientcg.com/a/Metal021) | rusty steel | color, height, normal, roughness | 1 x 1 | steel_rusty | L4 |
| iron_rusted | [Metal053C](https://ambientcg.com/a/Metal053C) | rusty steel | color, height, normal, roughness | 1 x 1 | iron_old | L4 |
| iron_rust | [Metal041B](https://ambientcg.com/a/Metal041B) | rusty steel | color, height, normal, roughness | 1 x 1 | iron | L4 |
| steel_scratched | [Metal038](https://ambientcg.com/a/Metal038) | rusty steel | color, height, normal, roughness | 1 x 1 | metal | L4 |
| paint_metal_green | [PaintedMetal006](https://ambientcg.com/a/PaintedMetal006) | rusty steel | ao, color, height, normal, roughness | 1.5 x 1.5 | paint_green | L4 |
| paint_metal_grey | [PaintedMetal014](https://ambientcg.com/a/PaintedMetal014) | rusty steel | ao, color, height, normal, roughness | 1.5 x 1.5 | paint_grey | L4 |
| rust_heavy | [Rust009](https://ambientcg.com/a/Rust009) | rusty steel | ao, color, height, normal, roughness | 1 x 1 | rust | L4 |
| cloth_grey | [Fabric030](https://ambientcg.com/a/Fabric030) | fabric | ao, color, height, normal, roughness | 0.5 x 0.5 | cloth | L4 |
| canvas_rough | [Fabric061](https://ambientcg.com/a/Fabric061) | fabric | ao, color, height, normal, roughness | 0.4 x 0.4 | fabric | L4 |
| wool_woven | [Fabric019](https://ambientcg.com/a/Fabric019) | fabric | color, height, normal, roughness | 0.3 x 0.3 | wool | L4 |
| rug_rough | [Fabric045](https://ambientcg.com/a/Fabric045) | fabric | ao, color, height, normal, roughness | 0.6 x 0.6 | rug | L4 |
| leather_brown | [Leather030](https://ambientcg.com/a/Leather030) | fabric | color, height, normal, roughness | 0.45 x 0.45 | leather | L4 |
| wicker_old | [Wicker010B](https://ambientcg.com/a/Wicker010B) | fabric | ao, color, height, normal, roughness | 0.4 x 0.4 | wicker | L4 |
| straw_thatch | [ThatchedRoof001A](https://ambientcg.com/a/ThatchedRoof001A) | fabric | ao, color, height, normal, roughness | 1.5 x 1.5 | straw | L4 |
| grass_lawn | [Grass004](https://ambientcg.com/a/Grass004) | grass | ao, color, height, normal, roughness | 1.4 x 1.4 | paint_dry_grass | L4 |
| grass_forest | [Ground037](https://ambientcg.com/a/Ground037) | grass | ao, color, height, normal, roughness | 2.1 x 2.1 | terrain_grass, turf | L4 |
| soil_brown | [Ground048](https://ambientcg.com/a/Ground048) | dirt | ao, color, height, normal, roughness | 1.4 x 1.4 | paint_dirt, soil, terrain_soil | L4 |
| soil_dry | [Ground103](https://ambientcg.com/a/Ground103) | dirt | ao, color, height, normal, roughness | 2 x 2 | path_dirt, terrain_crater | L4 |
| mud_wet | [Ground025](https://ambientcg.com/a/Ground025) | mud | ao, color, height, normal, roughness | 2 x 2 | mud | L4 |
| mud_soil | [Ground036](https://ambientcg.com/a/Ground036) | mud | ao, color, height, normal, roughness | 2 x 2 | paint_mud, terrain_mud | L4 |
| gravel_fine | [Gravel043](https://ambientcg.com/a/Gravel043) | gravel | ao, color, height, normal, roughness | 1.6 x 1.6 | path_gravel, terrain_gravel | L4 |
| gravel_path | [Ground081](https://ambientcg.com/a/Ground081) | gravel | ao, color, height, normal, roughness | 2 x 2 | paint_gravel, shingle | L4 |
| sand_beach | [Ground055S](https://ambientcg.com/a/Ground055S) | sand | ao, color, height, normal, roughness | 1.5 x 1.5 | paint_sand, sand | L4 |
| sand_dirty | [Ground080](https://ambientcg.com/a/Ground080) | sand | ao, color, height, normal, roughness | 2 x 2 | terrain_sand | L4 |
| rock_cliff | [Rock051](https://ambientcg.com/a/Rock051) | rock | ao, color, height, normal, roughness | 3 x 3 | rock, terrain_rock | L4 |
| rock_wall | [Rock022](https://ambientcg.com/a/Rock022) | rock | ao, color, height, normal, roughness | 3 x 3 | paint_rock, stone_field | L4 |
| rock_mossy | [Rock057](https://ambientcg.com/a/Rock057) | rock | ao, color, height, normal, roughness | 3 x 3 | (unused) | L4 |
| ground_rubble | [Ground110](https://ambientcg.com/a/Ground110) | dirt | ao, color, height, normal, roughness | 2.1 x 2.1 | terrain_scree | L4 |
| debris_brick | [Ground111](https://ambientcg.com/a/Ground111) | rubble stone | ao, color, height, normal, roughness | 2.8 x 2.8 | rubble_debris | L4 |
| leaves_forest | [Ground070](https://ambientcg.com/a/Ground070) | ground paint | ao, color, height, normal, roughness | 2.6 x 2.6 | paint_leaf_litter | L4 |
| soil_ploughed | [Ground104](https://ambientcg.com/a/Ground104) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_ploughed | L4 |
| ground_burnt | [Ground051](https://ambientcg.com/a/Ground051) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_scorched | L4 |
| pebbles_river | [Gravel008](https://ambientcg.com/a/Gravel008) | ground paint | ao, color, height, normal, roughness | 1.5 x 1.5 | paint_pebbles | L4 |
| marsh_bog | [Ground024](https://ambientcg.com/a/Ground024) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_marsh | L4 |
| limestone_coursed | [Bricks099](https://ambientcg.com/a/Bricks099) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | imgmat:stone_rubble | MA1 |
| limestone_dressed | [Bricks084](https://ambientcg.com/a/Bricks084) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | imgmat:stone_coursed | MA1 |
| fieldstone_irregular | [Bricks096](https://ambientcg.com/a/Bricks096) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:sub_rubble | MA1 |
| rubble_grey | [Rock023](https://ambientcg.com/a/Rock023) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:stone_field | MA1 |
| stone_grey_coursed | [Bricks108A](https://ambientcg.com/a/Bricks108A) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | (unused) | MA1 |
| ashlar_large | [Bricks063](https://ambientcg.com/a/Bricks063) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | imgmat:ashlar | MA1 |
| brick_muted | [Bricks101](https://ambientcg.com/a/Bricks101) | brick (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:brick | MA1 |
| brick_worn | [Bricks074](https://ambientcg.com/a/Bricks074) | brick (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:brickwork, imgmat:sub_brick | MA1 |
| render_lime | [Concrete034](https://ambientcg.com/a/Concrete034) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:limewash | MA1 |
| render_aged | [PaintedPlaster006](https://ambientcg.com/a/PaintedPlaster006) | plaster (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:plaster | MA1 |
| plaster_flaking | [PaintedPlaster016](https://ambientcg.com/a/PaintedPlaster016) | plaster (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:render_flaking | MA1 |
| plaster_flaking_b | [PaintedPlaster014](https://ambientcg.com/a/PaintedPlaster014) | plaster (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| plaster_old_b | [Plaster006](https://ambientcg.com/a/Plaster006) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:interior_wall | MA1 |
| render_stained | [Concrete036](https://ambientcg.com/a/Concrete036) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:render_stained | MA1 |
| concrete_aggregate | [Concrete049](https://ambientcg.com/a/Concrete049) | concrete (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:concrete_core | MA1 |
| bunker_weathered | [Concrete044B](https://ambientcg.com/a/Concrete044B) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:bunker | MA1 |
| concrete_stained | [Concrete044D](https://ambientcg.com/a/Concrete044D) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:bunker_wash, imgmat:path_concrete | MA1 |
| concrete_aged | [Concrete047A](https://ambientcg.com/a/Concrete047A) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:concrete | MA1 |
| concrete_formwork | [Concrete031](https://ambientcg.com/a/Concrete031) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:formwork | MA1 |
| slate_weathered | [RoofingTiles013C](https://ambientcg.com/a/RoofingTiles013C) | roof (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:slate | MA1 |
| slate_grey | [RoofingTiles015B](https://ambientcg.com/a/RoofingTiles015B) | roof (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| clay_tile_aged | [RoofingTiles012C](https://ambientcg.com/a/RoofingTiles012C) | roof (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:roof_tile | MA1 |
| clay_tile_mossy | [RoofingTiles014D](https://ambientcg.com/a/RoofingTiles014D) | roof (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| beam_weathered | [Wood062](https://ambientcg.com/a/Wood062) | timber (MA1) | ao, color, height, normal, roughness | 1.5 x 1.5 | imgmat:beam | MA1 |
| oak_aged | [Wood060](https://ambientcg.com/a/Wood060) | timber (MA1) | ao, color, height, normal, roughness | 1.5 x 1.5 | imgmat:furniture_wood, imgmat:joinery | MA1 |
| boards_weathered | [Planks023A](https://ambientcg.com/a/Planks023A) | timber (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:wood_raw | MA1 |
| paint_wood_bluegrey | [PaintedWood008B](https://ambientcg.com/a/PaintedWood008B) | timber (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_shutter | MA1 |
| paint_wood_white | [PaintedWood009C](https://ambientcg.com/a/PaintedWood009C) | timber (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_window, imgmat:painted_wood | MA1 |
| paint_wood_blue | [PaintedWood007A](https://ambientcg.com/a/PaintedWood007A) | timber (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| floor_boards_old | [WoodFloor043](https://ambientcg.com/a/WoodFloor043) | floor (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:floor_boards | MA1 |
| floor_boards_worn | [WoodFloor041](https://ambientcg.com/a/WoodFloor041) | floor (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| floor_cement_tile | [Tiles131](https://ambientcg.com/a/Tiles131) | floor (MA1) | ao, color, height, normal, roughness | 1 x 1 | (unused) | MA1 |
| floor_stone_flags | [Tiles130](https://ambientcg.com/a/Tiles130) | floor (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:floor_flags | MA1 |
| floor_tomette | [Tiles144](https://ambientcg.com/a/Tiles144) | floor (MA1) | color, height, normal, roughness | 1.2 x 1.2 | imgmat:floor_tomette, imgmat:terracotta | MA1 |
| iron_worn | [Metal062C](https://ambientcg.com/a/Metal062C) | metal (MA1) | color, height, normal, roughness | 1.5 x 1.5 | imgmat:metal | MA1 |
| steel_rust_light | [Metal053B](https://ambientcg.com/a/Metal053B) | metal (MA1) | color, height, normal, roughness | 1.5 x 1.5 | imgmat:steel_light_rust | MA1 |
| corrugated_galv | [CorrugatedSteel009](https://ambientcg.com/a/CorrugatedSteel009) | metal (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:galvanized | MA1 |
| paint_metal_worn_white | [PaintedMetal012](https://ambientcg.com/a/PaintedMetal012) | metal (MA1) | ao, color, height, normal, roughness | 1.5 x 1.5 | imgmat:paint_metal_worn | MA1 |
| dirt_compacted | [Ground112](https://ambientcg.com/a/Ground112) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:floor_earth, imgmat:paint_dirt, imgmat:path_dirt, imgmat:terrain_soil | MA1 |
| ground_mossy | [Ground068](https://ambientcg.com/a/Ground068) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| leaf_litter | [ScatteredLeaves009](https://ambientcg.com/a/ScatteredLeaves009) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_leaf_litter | MA1 |
| gravel_coarse | [Gravel041](https://ambientcg.com/a/Gravel041) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_gravel, imgmat:path_gravel | MA1 |
| paving_worn | [PavingStones130](https://ambientcg.com/a/PavingStones130) | ground (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:paint_cobbles, imgmat:paving | MA1 |
| canvas_olive | [Fabric066](https://ambientcg.com/a/Fabric066) | fabric (MA1) | ao, color, height, normal, roughness | 1 x 1 | imgmat:sack | MA1 |
| rug_beige | [Carpet016](https://ambientcg.com/a/Carpet016) | fabric (MA1) | ao, color, height, normal, roughness | 1 x 1 | imgmat:rug_img | MA1 |
| wallpaper_aged | [Wallpaper002C](https://ambientcg.com/a/Wallpaper002C) | interior (MA1) | color, height, normal, roughness | 1 x 1 | imgmat:wallpaper_img | MA1 |
| paper_aged | [Paper005](https://ambientcg.com/a/Paper005) | paper (MA1) | color, height, normal, roughness | 0.5 x 0.5 | imgmat:paper_img | MA1 |
