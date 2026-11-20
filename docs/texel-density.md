# Texel density per material class (CP step 8)

`python -I tools/texel_density.py` (corpus 07 s1.3). Density = array resolution x slot scale / set size (texels per
metre). Corpus targets at 1080p-1440p: walk-up surfaces and ground 512-1024 /m (ground plus a detail layer), mid props
128-256 /m, skyline 32-128 /m. At 1080p 512 /m is resolved at 1.9 m, 1024 /m at 0.9 m.

| Class | Slots | Min | Median | Max | Under 512 | Over 1024 |
|---|---:|---:|---:|---:|---:|---:|
| ground | 202 | 137 | 512 | 5120 | 36 | 18 |
| overlay / special | 14 | 256 | 796 | 5120 | 3 | 6 |
| props / interior / other | 91 | 341 | 1024 | 6827 | 11 | 18 |
| walk-up (walls, roofs, cover) | 250 | 79 | 627 | 2560 | 39 | 32 |

## walk-up (walls, roofs, cover) outliers

Under 512 /m (soft up close; candidates for a 2K set, a smaller tile or a detail layer):

- `facade_020a` (set `facade_020a`, 1024 px over 13.00 m, scale 1, lazy): 79 /m
- `facade_brown_windows` (set `facade_brown_windows`, 1024 px over 8.00 m, scale 1, lazy): 128 /m
- `facade_glass_curtain` (set `facade_glass_curtain`, 1024 px over 8.00 m, scale 1, lazy): 128 /m
- `facade_dark_office` (set `facade_dark_office`, 1024 px over 8.00 m, scale 1, lazy): 128 /m
- `facade_glass_grid` (set `facade_glass_grid`, 1024 px over 8.00 m, scale 1, lazy): 128 /m
- `facade_dark_grid` (set `facade_dark_grid`, 1024 px over 8.00 m, scale 1, lazy): 128 /m
- `office_ceiling_003` (set `office_ceiling_003`, 1024 px over 5.15 m, scale 1, lazy): 199 /m
- `concrete_panel_bolted` (set `concrete_panel_bolted`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `facade_005` (set `facade_005`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `facade_013` (set `facade_013`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `facade_002` (set `facade_002`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `office_ceiling_002` (set `office_ceiling_002`, 1024 px over 3.85 m, scale 1, lazy): 266 /m
- `roofing_tiles_006` (set `roofing_tiles_006`, 1024 px over 3.20 m, scale 1, lazy): 320 /m
- `roofing_tiles_008` (set `roofing_tiles_008`, 1024 px over 3.20 m, scale 1, lazy): 320 /m
- `roofing_tiles_007` (set `roofing_tiles_007`, 1024 px over 3.20 m, scale 1, lazy): 320 /m
- `roofing_tiles_009` (set `roofing_tiles_009`, 1024 px over 3.20 m, scale 1, lazy): 320 /m
- `roofing_tiles_010` (set `roofing_tiles_010`, 1024 px over 3.20 m, scale 1, lazy): 320 /m
- `wall_concrete_023` (set `concrete_cracked_white`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_white` (set `concrete_white`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_grey_cast` (set `concrete_grey_cast`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_bluegrey` (set `concrete_bluegrey`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_ribbed_panel` (set `concrete_ribbed_panel`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_speckled` (set `concrete_speckled`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_rough_grey` (set `concrete_rough_grey`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_pale` (set `concrete_pale`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_cracked_white` (set `concrete_cracked_white`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_stained_blue` (set `concrete_stained_blue`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `concrete_dirty_wet` (set `concrete_dirty_wet`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `rock_slate_blue` (set `rock_slate_blue`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `town_wall_stone` (set `limestone_coursed`, 1024 px over 3.00 m, scale 1): 341 /m
- `roofing_tiles_011a` (set `roofing_tiles_011a`, 1024 px over 2.90 m, scale 1, lazy): 353 /m
- `bricks_086` (set `bricks_086`, 1024 px over 2.60 m, scale 1, lazy): 394 /m
- `ceiling_tiles_office` (set `ceiling_tiles_office`, 1024 px over 2.40 m, scale 1, lazy): 427 /m
- `ceiling_tiles_grid` (set `ceiling_tiles_grid`, 1024 px over 2.40 m, scale 1, lazy): 427 /m
- `brick_veneer_yellow` (set `brick_veneer_yellow`, 1024 px over 2.40 m, scale 1, lazy): 427 /m
- `planks_029s` (set `planks_029s`, 1024 px over 2.40 m, scale 1, lazy): 427 /m
- `bricks_089` (set `bricks_089`, 1024 px over 2.20 m, scale 1, lazy): 465 /m
- `bricks_093` (set `bricks_093`, 1024 px over 2.10 m, scale 1, lazy): 488 /m
- `sd_render_aged` (set `render_aged`, 2048 px over 2.50 m, scale 0.6): 492 /m

Over 1024 /m (more texels than 1080p resolves at 1 m; harmless for frame time, a tiling-visibility risk):

- `brickwork` (set `clay_flat`, 2048 px over 0.40 m, scale 0.5): 2560 /m
- `wood` (set `wood_dark`, 2048 px over 0.80 m, scale 1): 2560 /m
- `joinery` (set `wood_oak`, 2048 px over 0.80 m, scale 1): 2560 /m
- `joinery_dark` (set `wood_dark`, 2048 px over 0.80 m, scale 1): 2560 /m
- `wood_058` (set `wood_058`, 1024 px over 0.40 m, scale 1): 2560 /m
- `wood_067` (set `wood_067`, 1024 px over 0.40 m, scale 1): 2560 /m
- `metal` (set `steel_scratched`, 2048 px over 1.00 m, scale 1): 2048 /m
- `steel_rusty` (set `steel_rusty`, 2048 px over 1.00 m, scale 1): 2048 /m
- `iron` (set `iron_rust`, 2048 px over 1.00 m, scale 1): 2048 /m
- `iron_old` (set `iron_rusted`, 2048 px over 1.00 m, scale 1): 2048 /m
- `rust` (set `rust_heavy`, 2048 px over 1.00 m, scale 1): 2048 /m
- `rb_iron_rust` (set `iron_rust`, 2048 px over 1.00 m, scale 1): 2048 /m
- `rb_steel_rusty` (set `steel_rusty`, 2048 px over 1.00 m, scale 1): 2048 /m
- `brick_042` (set `brick_042`, 2048 px over 1.00 m, scale 1): 2048 /m
- `brick_077` (set `brick_077`, 2048 px over 1.10 m, scale 1): 1862 /m
- `wallpaper_backrooms` (set `wallpaper_backrooms`, 1024 px over 0.60 m, scale 1): 1707 /m
- `ashlar` (set `limestone_smooth`, 2048 px over 1.20 m, scale 1): 1707 /m
- `bark` (set `bark_fir`, 2048 px over 1.20 m, scale 1): 1707 /m
- `brick_054` (set `brick_054`, 2048 px over 1.20 m, scale 1): 1707 /m
- `brick_059` (set `brick_059`, 2048 px over 1.20 m, scale 1): 1707 /m
- `concrete_038` (set `concrete_038`, 1024 px over 0.60 m, scale 1): 1707 /m
- `crate_wood` (set `planks_raw`, 2048 px over 1.40 m, scale 1): 1463 /m
- `wood_raw` (set `wood_old_planks`, 2048 px over 1.50 m, scale 1): 1365 /m
- `rb_rust` (set `rb_rust_patch`, 2048 px over 1.50 m, scale 1): 1365 /m
- `rb_beam` (set `beam_weathered`, 2048 px over 1.50 m, scale 1): 1365 /m
- `tiles_073` (set `tiles_073`, 1024 px over 0.75 m, scale 1): 1365 /m
- `town_oak` (set `beam_weathered`, 2048 px over 1.50 m, scale 1): 1365 /m
- `rb_log` (set `rb_wood_rough_old`, 2048 px over 1.60 m, scale 1): 1280 /m
- `wood_048` (set `wood_048`, 1024 px over 0.80 m, scale 1): 1280 /m
- `planks_028` (set `planks_028`, 1024 px over 0.80 m, scale 1): 1280 /m
- `brick` (set `brick_old`, 2048 px over 1.80 m, scale 1): 1138 /m
- `brick_082b` (set `brick_082b`, 2048 px over 1.80 m, scale 1): 1138 /m

## ground outliers

Under 512 /m (soft up close; candidates for a 2K set, a smaller tile or a detail layer):

- `road_008a` (set `road_008a`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_009a` (set `road_009a`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_009b` (set `road_009b`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_007` (set `road_007`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_006` (set `road_006`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_003` (set `road_003`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `road_001` (set `road_001`, 1024 px over 7.50 m, scale 1, lazy): 137 /m
- `paving_stones_151` (set `paving_stones_151`, 1024 px over 5.40 m, scale 1, lazy): 190 /m
- `asphalt_020l` (set `asphalt_020l`, 1024 px over 4.60 m, scale 1, lazy): 223 /m
- `asphalt_warm` (set `asphalt_warm`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `asphalt_dark` (set `asphalt_dark`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `asphalt_light` (set `asphalt_light`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `asphalt_plain` (set `asphalt_plain`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `asphalt_patched` (set `asphalt_patched`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `road_011a` (set `road_011a`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `road_014a` (set `road_014a`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `road_010a` (set `road_010a`, 1024 px over 4.00 m, scale 1, lazy): 256 /m
- `terrain_rock` (set `rock_cliff`, 2048 px over 3.00 m, scale 0.5): 341 /m
- `grass_bright` (set `grass_bright`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `grass_lush` (set `grass_lush`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `grass_dark` (set `grass_dark`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `ground_rubble_b` (set `ground_rubble_b`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `ground_brown_pebbly` (set `ground_brown_pebbly`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `rock_layered_mossy` (set `rock_layered_mossy`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `rock_green_mossy` (set `rock_green_mossy`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `sand_rippled` (set `sand_rippled`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `forest_litter` (set `forest_litter`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `paving_stones_122` (set `paving_stones_122`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `pathway_002` (set `pathway_002`, 1024 px over 3.00 m, scale 1, lazy): 341 /m
- `ground_107` (set `ground_107`, 1024 px over 2.80 m, scale 1, lazy): 366 /m
- `wall_pp009` (set `plaster_paint_009`, 1024 px over 2.50 m, scale 1, lazy): 410 /m
- `wall_pp010` (set `plaster_paint_010`, 1024 px over 2.50 m, scale 1, lazy): 410 /m
- `wall_pp011` (set `plaster_paint_011`, 1024 px over 2.50 m, scale 1, lazy): 410 /m
- `ground_106` (set `ground_106`, 1024 px over 2.50 m, scale 1, lazy): 410 /m
- `gravel_039` (set `gravel_039`, 1024 px over 2.40 m, scale 1, lazy): 427 /m
- `asphalt_030` (set `asphalt_030`, 1024 px over 2.20 m, scale 1, lazy): 465 /m

Over 1024 /m (more texels than 1080p resolves at 1 m; harmless for frame time, a tiling-visibility risk):

- `clay_006` (set `clay_006`, 1024 px over 0.20 m, scale 1): 5120 /m
- `clay_007` (set `clay_007`, 1024 px over 0.20 m, scale 1): 5120 /m
- `moss_001` (set `moss_001`, 1024 px over 0.45 m, scale 1): 2276 /m
- `painted_wood` (set `paint_wood_scratched`, 2048 px over 1.00 m, scale 1): 2048 /m
- `paintwood` (set `paint_wood_scratched`, 2048 px over 1.00 m, scale 1): 2048 /m
- `rb_wood_paint_old` (set `rb_painted_wood_old`, 2048 px over 1.00 m, scale 1): 2048 /m
- `paint_cobbles` (set `cobbles_old`, 2048 px over 1.25 m, scale 1): 1638 /m
- `soil` (set `soil_brown`, 2048 px over 1.40 m, scale 1): 1463 /m
- `terrain_soil` (set `soil_brown`, 2048 px over 1.40 m, scale 1): 1463 /m
- `paint_dry_grass` (set `grass_lawn`, 2048 px over 1.40 m, scale 1): 1463 /m
- `brick_037` (set `brick_037`, 2048 px over 1.45 m, scale 1): 1412 /m
- `paint_green` (set `paint_metal_green`, 2048 px over 1.50 m, scale 1): 1365 /m
- `paint_grey` (set `paint_metal_grey`, 2048 px over 1.50 m, scale 1): 1365 /m
- `ph_leaf_floor` (set `ph_forest_leaves_04`, 2048 px over 1.50 m, scale 1): 1365 /m
- `paint_pebbles` (set `pebbles_river`, 2048 px over 1.50 m, scale 1): 1365 /m
- `terrain_gravel` (set `gravel_fine`, 2048 px over 1.60 m, scale 1): 1280 /m
- `tactile_paving_006` (set `tactile_paving_006`, 1024 px over 0.90 m, scale 1): 1138 /m
- `tactile_paving_007` (set `tactile_paving_007`, 1024 px over 0.90 m, scale 1): 1138 /m
