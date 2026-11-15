# ambientCG texture manifest

All sets in `content/textures/` (226 ambientCG: 62 from L4, 47 added by MA1 on 2026-11-09, 114 added by TX2 on 2026-11-15, plus 3 user-chosen). Source: [ambientCG](https://ambientcg.com),
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

## TX2 acquisition (2026-11-15)

- **Inspected:** 247 candidate previews (128 px, official API) on five contact sheets, from 38 gap queries: asphalt,
  road, concrete, painted / corrugated metal, metal plates, diamond plate, rust, walkways, facades, carpet, ceiling,
  tiles, terrazzo, marble, wood floor, laminate, plaster, bricks, roofing, wallpaper, fabric, leather, wood veneer,
  plastic, rubber, paving, grass, ground, gravel, rock, moss, leaves, sand, mud, cliff. Skipped: tarmac with road
  markings, near-duplicates of library sets, rusty painted metals (seven rust sets already), snow, plain-white
  wallpaper variants, labelled sheets.
- **Chosen and downloaded:** 114 sets, 830 MB of 1K-JPG zips, **114 / 114 succeeded** (raw cache
  `%TEMP%/nmk/ambientcg_raw`, outside the repo); 112 -> 226 ambientCG sets, +180 MB packed in `content/textures`.
  Tile sizes are estimates from the previews (`tools/texture_sets.json`, `"batch": "TX2"`).
- **Library slots:** every TX2 set has a slot of its own name in `content/textures/textures.json` marked `"lazy": true`:
  its parameters are set (replace / detail, space, relief by family) but the set loads only once a material names it
  (`tex::flushPending`, src/textures.cpp), so unassigned sets take no GPU memory. Slot cap 160 -> 320, layers 128 -> 192.
  The same queue now also loads library sets that a material named before without a slot (they used to fall back to
  the procedural pattern): `brick_factory`, `floor_cement_tile`, `slate_roof`, `slate_grey`, `cobbles_grey`,
  `sand_dirty`, `rock_grey`, `rock_mossy`, `leaf_litter` on modern assets. Texture memory in the capture scene:
  83 sets / 332 MB before, 112 sets / 448 MB after (all modern assets in the library resolved).
- **Assignments (modern assets; material `texture` fields in the catalogs):**

  | Material (catalog) | Was | Now |
  |---|---|---|
  | `asphalt` (modern outdoor roads / lots, buildings A / B, Newtown A / B / C) | concrete_aggregate / concrete_rough | asphalt_plain |
  | `asphalt` (modern props) / `asphalt_dk` | concrete_rough / gravel_fine | asphalt_worn / asphalt_dark |
  | `membrane`, `roof_mem` (flat roofs) | concrete_rough / concrete_stained | asphalt_dark (bitumen) |
  | `precast` (modern buildings B, Newtown A / B) | concrete_aggregate | concrete_grey_cast |
  | `corrugated` (buildings B, Newtown A / B), containers `c_red` / `c_blue` / `c_green` / `c_tan` | corrugated_galv | corrugated_grey |
  | `ceiling` (buildings B, Newtown A / B: offices, hospital, school) | ceiling_backrooms (shared with the Backrooms) | ceiling_tiles_office |
  | `pool_tile` (leisure pool, outdoor pool, Newtown) | pooltile_backrooms (shared) | tiles_pool_cyan |
  | `laminate` (modern floors) | wood_oak | floor_laminate_pale |
  | `tile_white` (buildings A, kitchens / baths; pattern "tile" is the roof tile) | none | tiles_white_grid |
  | `chequer` (Newtown A: armory, warehouse dock plates) | steel_scratched | diamond_plate |
  | `rubber_red`, `rubber_green` (playground safety tiles) | gravel_fine | rubber_surface_black |
  | `rug_dark`, `rug_accent` (modern furniture) | rug_rough | carpet_office_navy, carpet_brick_weave |
  | `plastic_black` (modern furniture) | none | plastic_black |

  Not reassigned: the Backrooms sets, Tunnels floors, terrain / ground-paint layers and the D-day sand / soil / planks
  choices. The other 99 TX2 sets are registered and unassigned (they load when a material or the user names one).

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
| soil_brown | [Ground048](https://ambientcg.com/a/Ground048) | dirt | ao, color, height, normal, roughness | 1.4 x 1.4 | soil, terrain_soil | L4 |
| soil_dry | [Ground103](https://ambientcg.com/a/Ground103) | dirt | ao, color, height, normal, roughness | 2 x 2 | terrain_crater | L4 |
| mud_wet | [Ground025](https://ambientcg.com/a/Ground025) | mud | ao, color, height, normal, roughness | 2 x 2 | mud | L4 |
| mud_soil | [Ground036](https://ambientcg.com/a/Ground036) | mud | ao, color, height, normal, roughness | 2 x 2 | paint_mud, terrain_mud, tun_floor_mud | L4 |
| gravel_fine | [Gravel043](https://ambientcg.com/a/Gravel043) | gravel | ao, color, height, normal, roughness | 1.6 x 1.6 | path_gravel, terrain_gravel | L4 |
| gravel_path | [Ground081](https://ambientcg.com/a/Ground081) | gravel | ao, color, height, normal, roughness | 2 x 2 | paint_gravel, shingle | L4 |
| sand_beach | [Ground055S](https://ambientcg.com/a/Ground055S) | sand | ao, color, height, normal, roughness | 1.5 x 1.5 | (unused) | L4 |
| sand_dirty | [Ground080](https://ambientcg.com/a/Ground080) | sand | ao, color, height, normal, roughness | 2 x 2 | (unused) | L4 |
| rock_cliff | [Rock051](https://ambientcg.com/a/Rock051) | rock | ao, color, height, normal, roughness | 3 x 3 | imgmat:rock_cliff_b, rock, terrain_rock | L4 |
| rock_wall | [Rock022](https://ambientcg.com/a/Rock022) | rock | ao, color, height, normal, roughness | 3 x 3 | paint_rock, stone_field | L4 |
| rock_mossy | [Rock057](https://ambientcg.com/a/Rock057) | rock | ao, color, height, normal, roughness | 3 x 3 | (unused) | L4 |
| ground_rubble | [Ground110](https://ambientcg.com/a/Ground110) | dirt | ao, color, height, normal, roughness | 2.1 x 2.1 | terrain_scree, tun_floor_rubble | L4 |
| debris_brick | [Ground111](https://ambientcg.com/a/Ground111) | rubble stone | ao, color, height, normal, roughness | 2.8 x 2.8 | rubble_debris | L4 |
| leaves_forest | [Ground070](https://ambientcg.com/a/Ground070) | ground paint | ao, color, height, normal, roughness | 2.6 x 2.6 | paint_leaf_litter | L4 |
| soil_ploughed | [Ground104](https://ambientcg.com/a/Ground104) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_ploughed | L4 |
| ground_burnt | [Ground051](https://ambientcg.com/a/Ground051) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_scorched | L4 |
| pebbles_river | [Gravel008](https://ambientcg.com/a/Gravel008) | ground paint | ao, color, height, normal, roughness | 1.5 x 1.5 | paint_pebbles | L4 |
| marsh_bog | [Ground024](https://ambientcg.com/a/Ground024) | ground paint | ao, color, height, normal, roughness | 2 x 2 | paint_marsh | L4 |
| limestone_coursed | [Bricks099](https://ambientcg.com/a/Bricks099) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | (unused) | MA1 |
| limestone_dressed | [Bricks084](https://ambientcg.com/a/Bricks084) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | imgmat:stone_coursed | MA1 |
| fieldstone_irregular | [Bricks096](https://ambientcg.com/a/Bricks096) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:stone_rubble, imgmat:sub_rubble | MA1 |
| rubble_grey | [Rock023](https://ambientcg.com/a/Rock023) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:stone_field | MA1 |
| stone_grey_coursed | [Bricks108A](https://ambientcg.com/a/Bricks108A) | stone (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | (unused) | MA1 |
| ashlar_large | [Bricks063](https://ambientcg.com/a/Bricks063) | stone (MA1) | ao, color, height, normal, roughness | 3 x 3 | imgmat:ashlar | MA1 |
| brick_muted | [Bricks101](https://ambientcg.com/a/Bricks101) | brick (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:brick | MA1 |
| brick_worn | [Bricks074](https://ambientcg.com/a/Bricks074) | brick (MA1) | ao, color, height, normal, roughness | 3.3 x 3.3 | imgmat:brickwork, imgmat:sub_brick | MA1 |
| render_lime | [Concrete034](https://ambientcg.com/a/Concrete034) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:limewash | MA1 |
| render_aged | [PaintedPlaster006](https://ambientcg.com/a/PaintedPlaster006) | plaster (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:plaster | MA1 |
| plaster_flaking | [PaintedPlaster016](https://ambientcg.com/a/PaintedPlaster016) | plaster (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:render_flaking | MA1 |
| plaster_flaking_b | [PaintedPlaster014](https://ambientcg.com/a/PaintedPlaster014) | plaster (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| plaster_old_b | [Plaster006](https://ambientcg.com/a/Plaster006) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:interior_wall | MA1 |
| render_stained | [Concrete036](https://ambientcg.com/a/Concrete036) | plaster (MA1) | color, height, normal, roughness | 2.5 x 2.5 | imgmat:render_stained | MA1 |
| concrete_aggregate | [Concrete049](https://ambientcg.com/a/Concrete049) | concrete (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:concrete_core | MA1 |
| bunker_weathered | [Concrete044B](https://ambientcg.com/a/Concrete044B) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:bunker | MA1 |
| concrete_stained | [Concrete044D](https://ambientcg.com/a/Concrete044D) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:bunker_wash, imgmat:path_concrete | MA1 |
| concrete_aged | [Concrete047A](https://ambientcg.com/a/Concrete047A) | concrete (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:concrete, tun_floor_slab | MA1 |
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
| dirt_compacted | [Ground112](https://ambientcg.com/a/Ground112) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:floor_earth | MA1 |
| ground_mossy | [Ground068](https://ambientcg.com/a/Ground068) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_dirt, imgmat:path_dirt, paint_dirt, path_dirt | MA1 |
| leaf_litter | [ScatteredLeaves009](https://ambientcg.com/a/ScatteredLeaves009) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | (unused) | MA1 |
| gravel_coarse | [Gravel041](https://ambientcg.com/a/Gravel041) | ground (MA1) | ao, color, height, normal, roughness | 2 x 2 | imgmat:paint_gravel, imgmat:path_gravel, tun_floor_gravel | MA1 |
| paving_worn | [PavingStones130](https://ambientcg.com/a/PavingStones130) | ground (MA1) | ao, color, height, normal, roughness | 2.5 x 2.5 | imgmat:paint_cobbles, imgmat:paving | MA1 |
| canvas_olive | [Fabric066](https://ambientcg.com/a/Fabric066) | fabric (MA1) | ao, color, height, normal, roughness | 1 x 1 | imgmat:sack | MA1 |
| rug_beige | [Carpet016](https://ambientcg.com/a/Carpet016) | fabric (MA1) | ao, color, height, normal, roughness | 1 x 1 | imgmat:rug_img | MA1 |
| wallpaper_aged | [Wallpaper002C](https://ambientcg.com/a/Wallpaper002C) | interior (MA1) | color, height, normal, roughness | 1 x 1 | imgmat:wallpaper_img | MA1 |
| paper_aged | [Paper005](https://ambientcg.com/a/Paper005) | paper (MA1) | color, height, normal, roughness | 0.5 x 0.5 | imgmat:paper_img | MA1 |
| planks_038 | [Planks038](https://ambientcg.com/a/Planks038) | planks (user, 2026-11-10) | ao, color, height, normal, roughness | 1.6 x 1.6 | imgmat:planks_038 | L4 |
| sand_079 | [Ground079S](https://ambientcg.com/a/Ground079S) | sand (user, 2026-11-10) | ao, color, height, normal, roughness | 2 x 2 | paint_sand, sand, terrain_sand | L4 |
| sand_054 | [Ground054](https://ambientcg.com/a/Ground054) | sand (user, 2026-11-10) | ao, color, height, normal, roughness | 2 x 2 | sand_b | L4 |
| asphalt_warm | [Asphalt033](https://ambientcg.com/a/Asphalt033) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_warm | TX2 |
| asphalt_dark | [Road012A](https://ambientcg.com/a/Road012A) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_dark | TX2 |
| asphalt_light | [Asphalt031](https://ambientcg.com/a/Asphalt031) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_light | TX2 |
| asphalt_plain | [Road013A](https://ambientcg.com/a/Road013A) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_plain | TX2 |
| asphalt_worn | [Road015C](https://ambientcg.com/a/Road015C) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_worn | TX2 |
| asphalt_patched | [Road015A](https://ambientcg.com/a/Road015A) | asphalt (TX2) | ao, color, height, normal, roughness | 4 x 4 | asphalt_patched | TX2 |
| concrete_beige_smooth | [Concrete048](https://ambientcg.com/a/Concrete048) | concrete (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_beige_smooth | TX2 |
| concrete_white | [Concrete046](https://ambientcg.com/a/Concrete046) | concrete (TX2) | color, height, normal, roughness | 3 x 3 | concrete_white | TX2 |
| concrete_grey_cast | [Concrete042A](https://ambientcg.com/a/Concrete042A) | concrete (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_grey_cast | TX2 |
| concrete_bluegrey | [Concrete030](https://ambientcg.com/a/Concrete030) | concrete (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_bluegrey | TX2 |
| concrete_ribbed_panel | [Concrete045](https://ambientcg.com/a/Concrete045) | concrete panels (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_ribbed_panel | TX2 |
| concrete_speckled | [Concrete040](https://ambientcg.com/a/Concrete040) | concrete (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_speckled | TX2 |
| concrete_rough_grey | [Concrete044C](https://ambientcg.com/a/Concrete044C) | concrete (TX2) | ao, color, height, normal, roughness | 3 x 3 | concrete_rough_grey | TX2 |
| concrete_pale | [Concrete024](https://ambientcg.com/a/Concrete024) | concrete (TX2) | color, height, normal, roughness | 3 x 3 | concrete_pale | TX2 |
| concrete_cracked_white | [Concrete023](https://ambientcg.com/a/Concrete023) | concrete (TX2) | color, height, normal, roughness | 3 x 3 | concrete_cracked_white | TX2 |
| concrete_panel_bolted | [Concrete008](https://ambientcg.com/a/Concrete008) | concrete panels (TX2) | ao, color, height, normal, roughness | 4 x 4 | concrete_panel_bolted | TX2 |
| concrete_stained_blue | [Concrete019](https://ambientcg.com/a/Concrete019) | concrete (TX2) | color, height, normal, roughness | 3 x 3 | concrete_stained_blue | TX2 |
| concrete_dirty_wet | [Concrete035](https://ambientcg.com/a/Concrete035) | concrete (TX2) | color, height, normal, roughness | 3 x 3 | concrete_dirty_wet | TX2 |
| metal_painted_navy | [Metal027](https://ambientcg.com/a/Metal027) | painted metal (TX2) | color, height, normal, roughness | 2 x 2 | metal_painted_navy | TX2 |
| metal_black_gloss | [Metal029](https://ambientcg.com/a/Metal029) | painted metal (TX2) | color, height, normal, roughness | 2 x 2 | metal_black_gloss | TX2 |
| metal_painted_red | [PaintedMetal004](https://ambientcg.com/a/PaintedMetal004) | painted metal (TX2) | color, height, normal, roughness | 2 x 2 | metal_painted_red | TX2 |
| corrugated_grey | [CorrugatedSteel005](https://ambientcg.com/a/CorrugatedSteel005) | corrugated metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corrugated_grey | TX2 |
| metal_hazard_stripes | [PaintedMetal016](https://ambientcg.com/a/PaintedMetal016) | painted metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | metal_hazard_stripes | TX2 |
| corrugated_paleblue | [CorrugatedSteel007A](https://ambientcg.com/a/CorrugatedSteel007A) | corrugated metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corrugated_paleblue | TX2 |
| corrugated_blue_rusty | [CorrugatedSteel007B](https://ambientcg.com/a/CorrugatedSteel007B) | corrugated metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corrugated_blue_rusty | TX2 |
| corrugated_green | [CorrugatedSteel006A](https://ambientcg.com/a/CorrugatedSteel006A) | corrugated metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corrugated_green | TX2 |
| corrugated_yellow | [CorrugatedSteel008A](https://ambientcg.com/a/CorrugatedSteel008A) | corrugated metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corrugated_yellow | TX2 |
| metal_plates_dark | [MetalPlates006](https://ambientcg.com/a/MetalPlates006) | metal plates (TX2) | color, height, normal, roughness | 2 x 2 | metal_plates_dark | TX2 |
| metal_plates_rusty | [MetalPlates013](https://ambientcg.com/a/MetalPlates013) | metal plates (TX2) | ao, color, height, normal, roughness | 2 x 2 | metal_plates_rusty | TX2 |
| metal_hex_grate | [MetalPlates017A](https://ambientcg.com/a/MetalPlates017A) | metal plates (TX2) | ao, color, height, normal, roughness | 1 x 1 | metal_hex_grate | TX2 |
| metal_plate_rusted | [MetalPlates011](https://ambientcg.com/a/MetalPlates011) | metal plates (TX2) | ao, color, height, normal, roughness | 2 x 2 | metal_plate_rusted | TX2 |
| diamond_plate | [DiamondPlate009](https://ambientcg.com/a/DiamondPlate009) | diamond plate (TX2) | ao, color, height, normal, roughness | 1 x 1 | diamond_plate | TX2 |
| diamond_plate_black | [DiamondPlate008C](https://ambientcg.com/a/DiamondPlate008C) | diamond plate (TX2) | ao, color, height, normal, roughness | 1 x 1 | diamond_plate_black | TX2 |
| diamond_plate_yellow | [DiamondPlate006C](https://ambientcg.com/a/DiamondPlate006C) | diamond plate (TX2) | ao, color, height, normal, roughness | 1 x 1 | diamond_plate_yellow | TX2 |
| metal_perforated_rusty | [MetalWalkway014](https://ambientcg.com/a/MetalWalkway014) | metal walkway (TX2) | ao, color, height, normal, roughness | 1 x 1 | metal_perforated_rusty | TX2 |
| steel_worn_grey | [Metal024](https://ambientcg.com/a/Metal024) | metal (TX2) | color, height, normal, roughness | 2 x 2 | steel_worn_grey | TX2 |
| corten_rust | [Rust010](https://ambientcg.com/a/Rust010) | metal (TX2) | ao, color, height, normal, roughness | 2 x 2 | corten_rust | TX2 |
| metal_walkway_grate | [MetalWalkway013](https://ambientcg.com/a/MetalWalkway013) | metal walkway (TX2) | color, height, normal, roughness | 1 x 1 | metal_walkway_grate | TX2 |
| metal_mesh_grid | [MetalWalkway006](https://ambientcg.com/a/MetalWalkway006) | metal walkway (TX2) | color, height, normal, roughness | 1 x 1 | metal_mesh_grid | TX2 |
| facade_brown_windows | [Facade018A](https://ambientcg.com/a/Facade018A) | facade (TX2) | ao, color, height, normal, roughness | 8 x 8 | facade_brown_windows | TX2 |
| facade_glass_curtain | [Facade001](https://ambientcg.com/a/Facade001) | facade (TX2) | color, height, normal, roughness | 8 x 8 | facade_glass_curtain | TX2 |
| facade_dark_office | [Facade017](https://ambientcg.com/a/Facade017) | facade (TX2) | color, height, normal, roughness | 8 x 8 | facade_dark_office | TX2 |
| facade_glass_grid | [Facade006](https://ambientcg.com/a/Facade006) | facade (TX2) | color, height, normal, roughness | 8 x 8 | facade_glass_grid | TX2 |
| facade_dark_grid | [Facade019A](https://ambientcg.com/a/Facade019A) | facade (TX2) | ao, color, height, normal, roughness | 8 x 8 | facade_dark_grid | TX2 |
| carpet_office_navy | [Carpet012](https://ambientcg.com/a/Carpet012) | carpet (TX2) | ao, color, height, normal, roughness | 2 x 2 | carpet_office_navy | TX2 |
| fabric_red | [Fabric026](https://ambientcg.com/a/Fabric026) | fabric (TX2) | color, height, normal, roughness | 1 x 1 | fabric_red | TX2 |
| fabric_blue | [Fabric023](https://ambientcg.com/a/Fabric023) | fabric (TX2) | color, height, normal, roughness | 1 x 1 | fabric_blue | TX2 |
| carpet_brick_weave | [Carpet015](https://ambientcg.com/a/Carpet015) | carpet (TX2) | ao, color, height, normal, roughness | 2 x 2 | carpet_brick_weave | TX2 |
| ceiling_tiles_office | [OfficeCeiling001](https://ambientcg.com/a/OfficeCeiling001) | ceiling tiles (TX2) | ao, color, height, normal, roughness | 2.4 x 2.4 | ceiling_tiles_office | TX2 |
| ceiling_tiles_grid | [OfficeCeiling005](https://ambientcg.com/a/OfficeCeiling005) | ceiling tiles (TX2) | ao, color, height, normal, roughness | 2.4 x 2.4 | ceiling_tiles_grid | TX2 |
| tiles_white_stone | [Tiles141](https://ambientcg.com/a/Tiles141) | tiles (TX2) | color, height, normal, roughness | 2 x 2 | tiles_white_stone | TX2 |
| tiles_dark_stone | [Tiles140](https://ambientcg.com/a/Tiles140) | tiles (TX2) | color, height, normal, roughness | 2 x 2 | tiles_dark_stone | TX2 |
| tiles_charcoal | [Tiles138](https://ambientcg.com/a/Tiles138) | tiles (TX2) | ao, color, height, normal, roughness | 2 x 2 | tiles_charcoal | TX2 |
| tiles_white_large | [Tiles107](https://ambientcg.com/a/Tiles107) | tiles (TX2) | ao, color, height, normal, roughness | 2 x 2 | tiles_white_large | TX2 |
| tiles_white_grid | [Tiles133A](https://ambientcg.com/a/Tiles133A) | tiles (TX2) | ao, color, height, normal, roughness | 1.5 x 1.5 | tiles_white_grid | TX2 |
| tiles_checker | [Tiles074](https://ambientcg.com/a/Tiles074) | tiles (TX2) | color, height, normal, roughness | 1.2 x 1.2 | tiles_checker | TX2 |
| tiles_pool_cyan | [Tiles132A](https://ambientcg.com/a/Tiles132A) | tiles (TX2) | ao, color, height, normal, roughness | 1.5 x 1.5 | tiles_pool_cyan | TX2 |
| tiles_beige_grid | [PavingStones136](https://ambientcg.com/a/PavingStones136) | tiles (TX2) | ao, color, height, normal, roughness | 2 x 2 | tiles_beige_grid | TX2 |
| terrazzo_mixed | [Terrazzo013](https://ambientcg.com/a/Terrazzo013) | terrazzo (TX2) | color, height, normal, roughness | 2 x 2 | terrazzo_mixed | TX2 |
| terrazzo_chips | [Terrazzo018](https://ambientcg.com/a/Terrazzo018) | terrazzo (TX2) | color, height, normal, roughness | 2 x 2 | terrazzo_chips | TX2 |
| marble_white | [Marble012](https://ambientcg.com/a/Marble012) | marble (TX2) | color, height, normal, roughness | 2 x 2 | marble_white | TX2 |
| travertine | [Travertine009](https://ambientcg.com/a/Travertine009) | marble (TX2) | ao, color, height, normal, roughness | 2 x 2 | travertine | TX2 |
| marble_black | [Marble016](https://ambientcg.com/a/Marble016) | marble (TX2) | color, height, normal, roughness | 2 x 2 | marble_black | TX2 |
| floor_parquet_strip | [WoodFloor051](https://ambientcg.com/a/WoodFloor051) | wood floor (TX2) | ao, color, height, normal, roughness | 2 x 2 | floor_parquet_strip | TX2 |
| floor_parquet_herringbone | [WoodFloor070](https://ambientcg.com/a/WoodFloor070) | wood floor (TX2) | color, height, normal, roughness | 2 x 2 | floor_parquet_herringbone | TX2 |
| floor_laminate_pale | [WoodFloor040](https://ambientcg.com/a/WoodFloor040) | wood floor (TX2) | ao, color, height, normal, roughness | 2 x 2 | floor_laminate_pale | TX2 |
| floor_parquet_basket | [WoodFloor071](https://ambientcg.com/a/WoodFloor071) | wood floor (TX2) | color, height, normal, roughness | 2 x 2 | floor_parquet_basket | TX2 |
| drywall_white | [Plaster001](https://ambientcg.com/a/Plaster001) | plaster / drywall (TX2) | color, height, normal, roughness | 2 x 2 | drywall_white | TX2 |
| brick_veneer_yellow | [Bricks105](https://ambientcg.com/a/Bricks105) | brick veneer (TX2) | ao, color, height, normal, roughness | 2.4 x 1.2 | brick_veneer_yellow | TX2 |
| stone_block_mossy | [Bricks108B](https://ambientcg.com/a/Bricks108B) | tunnel stone (TX2) | ao, color, height, normal, roughness | 2 x 2 | stone_block_mossy | TX2 |
| roof_tiles_dark_modern | [RoofingTiles013B](https://ambientcg.com/a/RoofingTiles013B) | roofing (TX2) | ao, color, height, normal, roughness | 2 x 2 | roof_tiles_dark_modern | TX2 |
| wallpaper_white | [Wallpaper001A](https://ambientcg.com/a/Wallpaper001A) | wallpaper (TX2) | color, height, normal, roughness | 1 x 1 | wallpaper_white | TX2 |
| fabric_knit_orange | [Fabric085](https://ambientcg.com/a/Fabric085) | fabric (TX2) | ao, color, height, normal, roughness | 1 x 1 | fabric_knit_orange | TX2 |
| fabric_terry_blue | [Fabric084](https://ambientcg.com/a/Fabric084) | fabric (TX2) | ao, color, height, normal, roughness | 1 x 1 | fabric_terry_blue | TX2 |
| fabric_checker | [Fabric083](https://ambientcg.com/a/Fabric083) | fabric (TX2) | color, height, normal, roughness | 1 x 1 | fabric_checker | TX2 |
| fabric_plaid | [Fabric080](https://ambientcg.com/a/Fabric080) | fabric (TX2) | color, height, normal, roughness | 1 x 1 | fabric_plaid | TX2 |
| fabric_blue_plain | [Fabric081A](https://ambientcg.com/a/Fabric081A) | fabric (TX2) | ao, color, height, normal, roughness | 1 x 1 | fabric_blue_plain | TX2 |
| leather_red | [Leather037](https://ambientcg.com/a/Leather037) | leather (TX2) | color, height, normal, roughness | 1 x 1 | leather_red | TX2 |
| leather_cream | [Leather039](https://ambientcg.com/a/Leather039) | leather (TX2) | color, height, normal, roughness | 1 x 1 | leather_cream | TX2 |
| leather_black | [Leather026](https://ambientcg.com/a/Leather026) | leather (TX2) | color, height, normal, roughness | 1 x 1 | leather_black | TX2 |
| leather_tufted | [Leather034C](https://ambientcg.com/a/Leather034C) | leather (TX2) | ao, color, height, normal, roughness | 1 x 1 | leather_tufted | TX2 |
| wood_veneer_knotty | [Wood096](https://ambientcg.com/a/Wood096) | wood veneer (TX2) | color, height, normal, roughness | 1.5 x 1.5 | wood_veneer_knotty | TX2 |
| wood_veneer_warm | [Wood092](https://ambientcg.com/a/Wood092) | wood veneer (TX2) | color, height, normal, roughness | 1.5 x 1.5 | wood_veneer_warm | TX2 |
| wood_veneer_walnut | [Wood066](https://ambientcg.com/a/Wood066) | wood veneer (TX2) | color, height, normal, roughness | 1.5 x 1.5 | wood_veneer_walnut | TX2 |
| plastic_black | [Plastic006](https://ambientcg.com/a/Plastic006) | plastic / rubber (TX2) | color, height, normal, roughness | 1 x 1 | plastic_black | TX2 |
| rubber_mat | [Rubber004](https://ambientcg.com/a/Rubber004) | plastic / rubber (TX2) | color, height, normal, roughness | 1 x 1 | rubber_mat | TX2 |
| rubber_surface_black | [Rubber001](https://ambientcg.com/a/Rubber001) | plastic / rubber (TX2) | color, height, normal, roughness | 2 x 2 | rubber_surface_black | TX2 |
| rubber_surface_blue | [Rubber003](https://ambientcg.com/a/Rubber003) | plastic / rubber (TX2) | color, height, normal, roughness | 2 x 2 | rubber_surface_blue | TX2 |
| pavers_brick | [PavingStones152](https://ambientcg.com/a/PavingStones152) | paving (TX2) | ao, color, height, normal, roughness | 2 x 2 | pavers_brick | TX2 |
| cobbles_fan | [PavingStones150](https://ambientcg.com/a/PavingStones150) | paving (TX2) | ao, color, height, normal, roughness | 2 x 2 | cobbles_fan | TX2 |
| cobbles_mossy | [PavingStones142](https://ambientcg.com/a/PavingStones142) | paving (TX2) | ao, color, height, normal, roughness | 2 x 2 | cobbles_mossy | TX2 |
| setts_grey | [PavingStones070](https://ambientcg.com/a/PavingStones070) | paving (TX2) | ao, color, height, normal, roughness | 2 x 2 | setts_grey | TX2 |
| pavers_grass | [PavingStones139](https://ambientcg.com/a/PavingStones139) | paving (TX2) | ao, color, height, normal, roughness | 2 x 2 | pavers_grass | TX2 |
| grass_bright | [Grass005](https://ambientcg.com/a/Grass005) | grass (TX2) | ao, color, height, normal, roughness | 3 x 3 | grass_bright | TX2 |
| grass_lush | [Grass001](https://ambientcg.com/a/Grass001) | grass (TX2) | ao, color, height, normal, roughness | 3 x 3 | grass_lush | TX2 |
| grass_dark | [Grass003](https://ambientcg.com/a/Grass003) | grass (TX2) | ao, color, height, normal, roughness | 3 x 3 | grass_dark | TX2 |
| ground_stony | [Ground109](https://ambientcg.com/a/Ground109) | ground (TX2) | ao, color, height, normal, roughness | 3 x 3 | ground_stony | TX2 |
| ground_rubble_b | [Ground108](https://ambientcg.com/a/Ground108) | ground (TX2) | ao, color, height, normal, roughness | 3 x 3 | ground_rubble_b | TX2 |
| ground_brown_pebbly | [Ground085](https://ambientcg.com/a/Ground085) | ground (TX2) | ao, color, height, normal, roughness | 3 x 3 | ground_brown_pebbly | TX2 |
| gravel_white | [Gravel023](https://ambientcg.com/a/Gravel023) | gravel (TX2) | ao, color, height, normal, roughness | 2 x 2 | gravel_white | TX2 |
| gravel_grey_fine | [Gravel040](https://ambientcg.com/a/Gravel040) | gravel (TX2) | ao, color, height, normal, roughness | 2 x 2 | gravel_grey_fine | TX2 |
| pebbles_mixed | [Rocks022](https://ambientcg.com/a/Rocks022) | gravel (TX2) | ao, color, height, normal, roughness | 2 x 2 | pebbles_mixed | TX2 |
| rock_layered_mossy | [Rock064](https://ambientcg.com/a/Rock064) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_layered_mossy | TX2 |
| rock_slate_blue | [Rock058](https://ambientcg.com/a/Rock058) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_slate_blue | TX2 |
| rock_pale_cracked | [Rock060](https://ambientcg.com/a/Rock060) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_pale_cracked | TX2 |
| rock_dark_wet | [Rock035](https://ambientcg.com/a/Rock035) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_dark_wet | TX2 |
| rock_red | [Rock029](https://ambientcg.com/a/Rock029) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_red | TX2 |
| rock_slate_layers | [Rocks011](https://ambientcg.com/a/Rocks011) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_slate_layers | TX2 |
| moss_cushion | [Moss002](https://ambientcg.com/a/Moss002) | moss (TX2) | ao, color, height, normal, roughness | 2 x 2 | moss_cushion | TX2 |
| rock_green_mossy | [Rock059](https://ambientcg.com/a/Rock059) | rock (TX2) | ao, color, height, normal, roughness | 3 x 3 | rock_green_mossy | TX2 |
| leaves_scattered | [ScatteredLeaves008](https://ambientcg.com/a/ScatteredLeaves008) | forest floor (TX2) | ao, color, height, normal, roughness | 2 x 2 | leaves_scattered | TX2 |
| forest_floor | [Ground023](https://ambientcg.com/a/Ground023) | forest floor (TX2) | ao, color, height, normal, roughness | 3 x 3 | forest_floor | TX2 |
| sand_rippled | [Ground097](https://ambientcg.com/a/Ground097) | sand (TX2) | ao, color, height, normal, roughness | 3 x 3 | sand_rippled | TX2 |
| forest_litter | [Ground071](https://ambientcg.com/a/Ground071) | forest floor (TX2) | ao, color, height, normal, roughness | 3 x 3 | forest_litter | TX2 |
