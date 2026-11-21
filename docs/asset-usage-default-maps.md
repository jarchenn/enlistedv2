# Asset usage on the default maps

SPEC 3.5: every catalogue asset is used where it is historically plausible, or listed here with the reason it is not.
Counts are placed instances (companions included), from the `tools/mapgen` build reports (`out/<map>.summary.json`).
`--selftest-maps` fails when a catalogue asset is missing from this table.

- **DF1 (Sun Fort, `sd_desert`):** no column here; its desert shrubs (on the stand-in shrub spots and outside the compound) and dry grass are in `tools/mapgen/maps/sd_desert.py` `flora()`.
- **Omaha:** *Normandy Beaches - Omaha, Easy Red* (W5a, `tools/mapgen/maps/omaha_easy_red.py`).
- **Mont-Castre:** *Normandy Forest - Mont-Castre, Hill 122* is a terrain-only base map (no assets placed; the user places them in the editor), built by W5b. Every cell in that column reads "terrain-only base map".
- **Chateau town:** *Chateau town* (M1, `tools/mapgen/maps/chateau_town.py`): placed counts, companions included; "not used" = not on this map.
- **D-day:** *D-day* (D1, `tools/mapgen/maps/dday.py`): placed counts, companions included; "not used" = not on this map.
- **Tunnels:** *Tunnels* (UG1, `tools/mapgen/maps/tunnels.py`): placed counts; "not used" = not on this map.
- **Backrooms:** *Backrooms* (BR1, `tools/mapgen/maps/backrooms.py`): placed counts; "not used" = not on this map.
- **Newtown:** *Newtown* (BR4, `tools/mapgen/maps/newtown.py`): placed counts; "not used" = not on this map.
- **Outpost Desert:** *Outpost Desert* (NT3, `tools/mapgen/maps/outpost_desert.py`): placed counts; "not used" = not on this map.
- **Outpost Woods:** *Outpost Woods* (NT3, `tools/mapgen/maps/outpost_woods.py`): placed counts; "not used" = not on this map.
- **Hollow Village:** *Hollow Village* (MP1, `tools/mapgen/maps/hollow_village.py`): placed counts; "not used" = not on this map.

| Asset | Category | Omaha | Mont-Castre | Chateau town | D-day | Tunnels | Backrooms | Newtown | Outpost Desert | Outpost Woods | Chateau town | Caverns | Hollow Village |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `aw_dugout_log` | Atlantic Wall | 1: C: log-roofed dugout behind the draw-head roadblock | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_dugout_personnel` | Atlantic Wall | 1: B: WN64 personnel dugout behind the crest | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_foxhole` | Atlantic Wall | 15: Three per objective (fallback positions) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_r612_unfinished` | Atlantic Wall | 1: B: WN64, the unfinished R612 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_r667_casemate` | Atlantic Wall | 1: A: WN65 Ruquet casemate, embrasure west along the beach (companion PaK 40, shell crates at the rear door) | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_tobruk_mg` | Atlantic Wall | 1: A: WN65 MG ringstand | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_tobruk_mortar` | Atlantic Wall | 2: A: WN65; B: WN64 mortar Tobruk | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_tobruk_turret` | Atlantic Wall | 1: A: WN65 tank-turret Tobruk (ref photo 6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_bend` | Atlantic Wall | not used: added with D-day (45-degree trench bend) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_corner` | Atlantic Wall | 2: D: trench line across the hedgerow fields | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_cross` | Atlantic Wall | not used: added with D-day (trench crossroads) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_entry` | Atlantic Wall | 9: Ends of every trench run (A, B, D) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_straight` | Atlantic Wall | 22: A; B (crest line over the beach); D | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_tee` | Atlantic Wall | not used: added with D-day (trench T-junction) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_zigzag` | Atlantic Wall | 20: A, B and D trench runs | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_abatis` | Bocage | 45: Plateau: felled trees across the flank lanes; attack-path cover fill above the bluff | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_corner` | Bocage | as `line_hedgerow` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_gap` | Bocage | as `line_hedgerow` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_straight` | Bocage | as `line_hedgerow` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_marsh_patch` | Bocage | 8: Marsh pools on the sand shelf | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_quarry_face` | Bocage | excluded: No quarry at E-1 (a Mont-Castre feature) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_rampart` | Bocage | excluded: The Roman / medieval earthworks belong to Mont-Castre | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bcg_sunken_lane` | Bocage | 98: Plateau: west and east flank sunken lanes | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `bch_at_ditch` | Beach obstacles | 26: Across the E-1 mouth (s 476); second ditch at the draw head (C) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_belgian_gate` | Beach obstacles | 90: Belt 1, 250 m out, with the Easy Red gaps | terrain-only base map | not used | 12 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_lcvp_beached` | Landing craft | 4: A1 landing (spawn cover, rack crates in the well) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_lcvp_intact` | Landing craft | 2: A1: craft still afloat behind the first wave | terrain-only base map | not used | 3 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_lcvp_wrecked` | Landing craft | 5: Wrecks on the tidal flat (cover) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_log_ramp_mined` | Beach obstacles | 72: Belt 3 (ramps) | terrain-only base map | not used | 13 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_log_stake_mine` | Beach obstacles | 253: Belt 2 (two staggered rows of stakes) | terrain-only base map | not used | 40 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_minefield_fence` | Beach obstacles | as `line_minefield_fence` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_minen_sign` | Beach obstacles | 19: Achtung Minen boards along the minefields | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_roadblock` | Beach obstacles | 1: C: across the draw road | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_seawall_wood` | Beach | 55: East half of the beach behind the shingle | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bch_shingle_bank` | Beach | 100: The whole shingle bank (s 404-416) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bunker_command_shelter` | Bunkers | 1: D: battery and CP shelter | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bunker_field_shelter` | Bunkers | 2: C and D | terrain-only base map | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `bunker_large_casemate` | Bunkers | excluded: No large casemate at E-1: WN64 had only the unfinished R612; the H669s stood at other draws | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bunker_mg_casemate` | Bunkers | 1: A: WN65 flank MG casemate | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used | not used | not used | not used |
| `bunker_observation_post` | Bunkers | 1: B: WN64 observation post | terrain-only base map | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `bunker_pak_casemate` | Bunkers | excluded: WN65's gun casemate is the R667 (`aw_r667_casemate`) | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `crate_mixed_supply` | Supply | 14: Ammunition dump at D; dressing at every objective | terrain-only base map | 10 | 15 | 9 | 8 | 2 | 2 | 2 | 10 | not used | 8 |
| `crate_pak40_he` | Supply | 7: A: behind the R667; D: ammunition dump | terrain-only base map | not used | 9 | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_sandbag_curved` | Defences | as `line_sandbag_low` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_sandbag_straight` | Defences | as `line_sandbag_low` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_tank_trap` | Defences | 233: Belt 4 (hedgehogs); beach cover fill | terrain-only base map | not used | 58 | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_barrels` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 90 | 11 | not used | not used | not used | not used | not used | 90 | not used | not used |
| `drs_bridge_stone_large` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `drs_bridge_stone_small` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `drs_cart` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 63 | 5 | not used | not used | not used | not used | not used | 63 | not used | not used |
| `drs_retaining_wall` | Dressing | not used (VOT T2: Rue Haute's churchyard terrace wall) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_town_steps` | Dressing | not used (VOT T2: the flight up Rue Haute's churchyard terrace) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_churchyard_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 2 | 1 | not used | not used | not used | not used | not used | 2 | not used | not used |
| `drs_flower_bed` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 233 | 22 | not used | not used | not used | not used | not used | 233 | not used | not used |
| `drs_garden_wall` | Dressing | as `line_garden_wall` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_gate_piers` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_gravestone_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 33 | 8 | not used | not used | not used | not used | not used | 33 | not used | 7 |
| `drs_gravestone_round` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 57 | 15 | not used | not used | not used | not used | not used | 57 | not used | 14 |
| `drs_hay_bale_stack` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 81 | 13 | not used | not used | 1 | not used | not used | 81 | not used | not used |
| `drs_picket_fence` | Dressing | as `line_picket_fence` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_sandbag_ring` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `drs_sandbag_wall` | Defences | as `line_sandbag_wall` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `drs_tomb_chest` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 23 | 7 | not used | not used | not used | not used | not used | 23 | not used | 7 |
| `drs_well` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 19 | 3 | not used | not used | not used | not used | not used | 19 | not used | 1 |
| `drs_wooden_x_obstacle` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 13 | 30 | not used | not used | not used | not used | not used | 13 | not used | not used |
| `drs_woodpile` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 86 | 11 | not used | not used | not used | not used | not used | 86 | not used | not used |
| `env_sea_shallows` | Beach | 87: The sea (s < 50): shallow water surface | terrain-only base map | not used | 20 | not used | not used | not used | not used | not used | not used | not used | not used |
| `fence_post_wire` | Dressing | as `line_post_wire_fence` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `field_gate` | Dressing | 6: Bocage field gates | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `fire_brazier` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fire_campfire` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_armchair` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_armchair_wing` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_armoire` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 6 | not used | not used | not used | not used | not used | not used |
| `furn_barrel` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 15 | 16 | 12 | not used | not used | not used | not used | 15 | not used | not used |
| `furn_barrel_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_basket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used |
| `furn_bed_iron` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bench` | Furniture | 1: D: farmyard CP | terrain-only base map | 12 | 2 | not used | not used | not used | not used | not used | 12 | not used | not used |
| `furn_bookcase` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_books` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bottles` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bucket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 20 | 8 | not used | not used | not used | not used | not used | 20 | not used | not used |
| `furn_candelabra` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): church altars | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_candle` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bedrooms, small rooms | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_candlestick` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_candlesticks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_chair_ladder` | Furniture | 3: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_chair_slat` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_chest` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_clock` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_hooks` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_stand` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_commode` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_crate` | Furniture | 11: Ammunition dumps and dressing | terrain-only base map | 20 | 13 | 13 | not used | not used | not used | not used | 20 | not used | not used |
| `furn_crockery` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_curtains` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_curtains_half` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_debris_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_debris_plaster` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_dresser` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_fire_hearth` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): a fire on every hearth | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_fireplace` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_dusk` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_landscape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_portrait` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_seascape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_small` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_gramophone` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_hay_bales` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 34 | not used | not used | not used | 1 | not used | not used | 34 | not used | not used |
| `furn_hutch` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_hutch_glazed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_jars` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_jug_bowl` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_caged` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bunkers, casemates and low ceilings | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_ceiling` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): kitchens, sitting rooms, halls, offices, classrooms, taverns | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_pendant_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_table` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): sitting rooms | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_wall_oil` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_ammo_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_bunk` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_crates` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_fusebox` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_gasmask_hooks` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_generator` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_jerrycans` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | 1 | 1 | not used | not used | not used |
| `furn_mil_locker` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_map` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_phone` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_radio` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_rifle_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_table` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used | not used | not used | not used | not used | not used | not used |
| `furn_mirror` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_pan_rail` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_pantry` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_papers` | Furniture | 1: D: CP map table (maps) | terrain-only base map | not used | not used | not used | not used | 29 | not used | not used | not used | not used | not used |
| `furn_pew` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_picture` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_plate_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_portrait` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_pot` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_radio` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_kilim` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_large` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_medallion` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_runner` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sacks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 33 | 9 | not used | not used | not used | not used | not used | 33 | not used | not used |
| `furn_school_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sconce` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sconce_candle` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_screen_folding` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_settee` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_shelf` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_side_table` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sideboard` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_stool` | Furniture | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_stove` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_stove_potbelly` | Furniture | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_suitcases` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_table_dining` | Furniture | 1: D: the HQ map table in the farmyard | terrain-only base map | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used |
| `furn_table_low` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_table_small` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_tool_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_utensil_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_wall_clock` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_wall_shelf` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_washstand` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_wood_pile` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 24 | not used | not used | not used | not used | not used | not used | 24 | not used | not used |
| `furn_workbench` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fx_impact_smoke` | Lighting | not used: added with D-day (smouldering impact, smoke column) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used | not used | not used | not used |
| `fx_wreck_fire` | Lighting | not used: added with D-day (burning wreckage, smoke column) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used | not used | not used | not used |
| `gun_flak18` | Ordnance | 1: D: 8.8 cm Flak 18 in the orchard (representative, SPEC 3.3) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used | not used | not used | not used |
| `gun_m2hb_tripod` | Field guns | 1: US .50 set up on the shingle near A2 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `gun_mg42_embrasure` | Field guns | 16: Every sandbag MG nest | terrain-only base map | not used | 10 | not used | not used | not used | not used | not used | not used | not used | not used |
| `gun_pak40` | Field guns | 1: A: in the R667 (stands in for the 5 cm gun) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used | not used | not used | not used |
| `hedgerow_bocage` | Vegetation | as `line_hedgerow` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `house_farmhouse_2story` | Houses | 4: Saint-Laurent | terrain-only base map | 9 | not used | not used | not used | not used | not used | not used | 9 | not used | 4 |
| `house_stone_cottage` | Houses | 7: Saint-Laurent; the beach house on the sand shelf | terrain-only base map | 11 | 1 | not used | not used | not used | not used | not used | 11 | not used | 5 |
| `kit_demo_folly` | Landmarks | excluded: the B1 geometry-kit demonstration piece (docs/asset-format.md#b1-geometry-kit), not a period building | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `lamp_gas_street` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md); the Beautify street_lamps role lines roads with it | terrain-only base map | 10 | 4 | not used | not used | not used | not used | not used | 10 | not used | not used |
| `lamp_hanging_lantern` | Lighting | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): barns, workshops, stores, church naves | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `lamp_lantern_post` | Lighting | not hand-placed: Paths > Line auto paths with lamp posts (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `lamp_wall_lantern` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `large_municipal_building` | Large buildings | 1: Saint-Laurent (mairie) | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `large_stone_warehouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `large_village_inn` | Large buildings | 1: Saint-Laurent crossroads | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `large_village_schoolhouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `line_trench` | Lines | not used: LN1 trench line (old maps' trench pieces migrate to it on load) | terrain-only base map | not used | 10 | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_concertina` | Lines | 5 | terrain-only base map | not used | 8 | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_knife_rest` | Lines | not used | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_minefield_fence` | Lines | 11 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_post_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | 13 |
| `line_picket_fence` | Lines | 16 | terrain-only base map | 180 | 30 | not used | not used | not used | not used | not used | 180 | not used | not used |
| `line_stone_wall` | Lines | 6 | terrain-only base map | 71 | 25 | not used | not used | 9 | not used | not used | 71 | not used | 10 |
| `line_garden_wall` | Lines | 7 | terrain-only base map | 23 | 10 | not used | not used | not used | not used | not used | 23 | not used | 1 |
| `line_sandbag_wall` | Lines | not used | terrain-only base map | 12 | 9 | not used | not used | not used | 2 | not used | 12 | not used | not used |
| `line_sandbag_low` | Lines | 99 | terrain-only base map | 8 | 21 | not used | not used | not used | 6 | 8 | 8 | not used | not used |
| `line_hedgerow` | Lines | 111 | terrain-only base map | 93 | not used | not used | not used | not used | not used | not used | 93 | not used | not used |
| `mil_stronghold` | Bunkers | excluded: GX6 Atlantic Wall command stronghold (60 x 34 m with earthworks); larger than any Omaha strongpoint, for hand-built maps | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hydrant_cast` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used | not used | not used | not used |
| `mod_payphone` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used | not used | not used | not used |
| `mod_play_slide_old` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_play_swings_old` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_traffic_barrel` | not used | not used | not used | not used | not used | not used | not used | 18 | 8 | 4 | not used | not used | not used |
| `nat_blackthorn_low` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 31 | 20 | not used | not used | not used | not used | not used | 31 | not used | not used |
| `nat_boulder_granite_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `nat_boulder_granite_s` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_boulder_limestone_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used | not used | 33 | not used | not used | 25 | not used |
| `nat_boulder_limestone_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 5 | not used | not used | not used | 41 | not used | not used | 9 | not used |
| `nat_boulder_schist_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_boulder_schist_slab` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 4 | not used | 22 | not used |
| `nat_boulders_granite_group` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 4 | 26 | 3 | not used | 21 | not used |
| `nat_bracken_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 42 | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_bramble_thicket` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 43 | not used | 49 | not used | not used | not used |
| `nat_brush_patchy` | Vegetation | not placed: V9 ground cover (patchy brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_brush_rank` | Vegetation | not placed: V9 ground cover (rank brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_brush_scrub` | Vegetation | not placed: V9 ground cover (dry scrub, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 59 | not used | not used | not used | 91 | not used | not used | not used | not used |
| `nat_clover_tufts` | Vegetation | not placed: V9 ground cover (clover and green tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 8 | not used | not used | not used | not used | not used | not used | 8 | not used | not used |
| `nat_drygrass_sparse` | Vegetation | not placed: V9 ground cover (sparse dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 113 | not used | not used | not used | 138 | not used | not used | not used | not used |
| `nat_drygrass_tall` | Vegetation | not placed: V9 ground cover (tall dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 3 | 419 | not used | not used | not used | 1 | not used | 3 | not used | not used |
| `nat_drygrass_tufts` | Vegetation | not placed: V9 ground cover (dry grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 7 | 177 | not used | not used | 54 | 180 | not used | 7 | not used | not used |
| `nat_fern_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 52 | 18 | not used | not used | not used | not used | not used | 52 | 21 | not used |
| `nat_flowers_buttercup` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 27 | 1 | not used | not used | not used | not used | not used | 27 | not used | not used |
| `nat_flowers_clover` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_flowers_cornflower` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 56 | 7 | not used | not used | not used | not used | not used | 56 | not used | not used |
| `nat_flowers_cowparsley` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 63 | 37 | not used | not used | not used | not used | not used | 63 | not used | not used |
| `nat_flowers_daisy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 104 | 47 | not used | not used | not used | not used | not used | 104 | not used | not used |
| `nat_flowers_foxglove` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 3 | not used | not used | not used | not used | not used | 41 | not used | not used |
| `nat_flowers_meadow` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 21 | not used | not used | not used | 2 | not used | not used | 21 | not used | not used |
| `nat_flowers_poppy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 105 | 39 | not used | not used | not used | not used | not used | 105 | not used | not used |
| `nat_gorse_shrub` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 91 | not used | not used | not used | 174 | not used | not used | not used | not used |
| `nat_green_tufts` | Vegetation | not placed: V9 ground cover (green grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 30 | 42 | not used | not used | not used | not used | 33 | 30 | 19 | not used |
| `nat_hawthorn_bush` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 147 | 70 | not used | not used | 49 | not used | not used | 147 | not used | not used |
| `nat_meadow_mixed` | Vegetation | not placed: V9 ground cover (mixed meadow, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 42 | 62 | not used | not used | 35 | not used | 3 | 42 | not used | not used |
| `nat_nettle_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 7 | 21 | not used | not used | 122 | not used | 76 | 7 | not used | not used |
| `nat_outcrop_limestone` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 2 | not used | not used | 5 | 25 | not used | not used | not used | not used |
| `nat_outcrop_schist` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used |
| `nat_sapling_ash` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 4 | not used | 86 | not used | not used | not used |
| `nat_sapling_oak` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nat_scree_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used | not used | 5 | not used | not used | not used | not used |
| `nat_stones_granite_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 107 | not used | not used | not used | not used | not used |
| `nat_stones_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 4 | not used | not used | 267 | 4 | not used | not used | not used | not used |
| `nat_tallgrass_patch` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 35 | 111 | not used | not used | 125 | not used | 35 | 35 | not used | not used |
| `nat_tallgrass_verge` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 19 | 75 | not used | not used | 10 | not used | not used | 19 | not used | not used |
| `nat_wheat_field` | Vegetation | not placed: V9 ground cover (wheat field square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 51 | not used | not used | not used | not used | not used | not used | 51 | not used | not used |
| `nat_wheat_stubble` | Vegetation | not placed: V9 ground cover (wheat stubble square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 67 | not used | not used | not used | not used | not used | not used | 67 | not used | not used |
| `nat_wheat_trampled` | Vegetation | not placed: V9 ground cover (trampled wheat square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 7 | not used | not used | not used | not used | not used | not used | 7 | not used | not used |
| `path_concrete` | Paths | 1: A: apron behind the R667 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `path_dirt` | Paths | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `path_gravel` | Paths | 1: E: church path | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rack_allied` | Supply | 3: Attacker spawns A3-A5 | terrain-only base map | 5 | 4 | not used | not used | 1 | 1 | 1 | 5 | not used | 4 |
| `rack_allied_crates` | Supply | 2: A1 (in an LCVP well) and A2 (on the shingle): crate racks, SPEC 3.2 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rack_axis` | Supply | 5: Defender spawns D1-D5 | terrain-only base map | 5 | 4 | not used | not used | 1 | 1 | 1 | 5 | not used | 4 |
| `rub_beam_tile_heap` | Rubble | not placed: V3 rubble (Beam and tile heap, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_brick_pile_l` | Rubble | not placed: V3 rubble (Brick rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_brick_pile_m` | Rubble | not placed: V3 rubble (Brick rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_brick_pile_s` | Rubble | not placed: V3 rubble (Brick rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_collapsed_wall` | Rubble | not placed: V3 rubble (Collapsed wall section, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_debris_patch` | Rubble | not placed: V3 rubble (Scattered debris patch, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 15 | not used | not used | 26 | 2 | not used | not used | not used | not used |
| `rub_spill_strip` | Rubble | not placed: V3 rubble (Rubble spill strip (8 m), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_stone_pile_l` | Rubble | not placed: V3 rubble (Stone rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rub_stone_pile_m` | Rubble | not placed: V3 rubble (Stone rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 1 | 2 | not used | not used | not used | not used | not used | not used | not used |
| `rub_stone_pile_s` | Rubble | not placed: V3 rubble (Stone rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used | not used | not used | not used | not used | not used | not used |
| `ruin_barn_burnt` | Ruins | not placed: V3 ruin (Burnt-out barn, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ruin_cottage_corner` | Ruins | not placed: V3 ruin (Ruined cottage corner, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 |
| `ruin_farmhouse_gutted` | Ruins | not placed: V3 ruin (Gutted farmhouse shell, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ruin_house_shelled` | Ruins | not placed: V3 ruin (Shelled house (two storeys, roof collapsed), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 |
| `ruin_shop_row` | Ruins | not placed: V3 ruin (Half-collapsed shop terrace, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ruin_townhouse_facade` | Ruins | not placed: V3 ruin (Bombed townhouse (facade only), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sb_bag_lib` | Defences | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) | not used: the sandbag mesh library (hidden; every sandbag box part draws its bags, src/sandbags.cpp) |
| `scn_treeline_a` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 25 | not used | not used | not used | not used | not used | not used | not used | not used |
| `scn_treeline_b` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 22 | not used | not used | not used | not used | not used | not used | not used | not used |
| `scn_treeline_c` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 26 | not used | not used | not used | not used | not used | not used | not used | not used |
| `shrub_bramble` | Vegetation | 3974: Bluff scrub (about 1 per 38 m2), draw undergrowth, dunes | terrain-only base map | 27 | 33 | not used | not used | 26 | not used | not used | 27 | not used | not used |
| `shrub_hazel` | Vegetation | 1070: Bluff scrub, E-1 draw undergrowth | terrain-only base map | 318 | 211 | not used | not used | 84 | not used | not used | 318 | not used | not used |
| `small_carpenter_workshop` | Small buildings | 2: Saint-Laurent | terrain-only base map | 8 | not used | not used | not used | not used | not used | not used | 8 | not used | 2 |
| `small_roadside_office` | Small buildings | 1: Saint-Laurent crossroads | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used | 7 | not used | not used |
| `small_stone_dairy` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 8 | 1 | not used | not used | not used | not used | not used | 8 | not used | 2 |
| `small_storehouse` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 14 | 1 | not used | not used | not used | not used | not used | 14 | not used | 2 |
| `stone_wall_low` | Dressing | as `line_stone_wall` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tank_sherman_firefly` | Vehicles | excluded: The Firefly (17-pdr) was British, not at Omaha; no DD Sherman asset exists; player vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tank_tiger_i` | Vehicles | excluded: No Tigers at Omaha; vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tree_apple` | Vegetation | 396: Orchards (D battery, A5, two more) | terrain-only base map | 245 | 58 | not used | not used | 13 | not used | not used | 245 | not used | not used |
| `tree_ash` | Vegetation | 213: Hedgerow trees; wooded E-1 draw | terrain-only base map | 324 | 283 | not used | not used | 330 | not used | not used | 324 | not used | not used |
| `tree_oak` | Vegetation | 316: Hedgerow trees; wooded E-1 draw | terrain-only base map | 282 | 310 | not used | not used | 276 | not used | not used | 282 | not used | not used |
| `tree_poplar` | Vegetation | 73: Roadside poplars on the draw road; hedgerow trees | terrain-only base map | 128 | 149 | not used | not used | 114 | not used | not used | 128 | not used | not used |
| `utility_pole` | Dressing | 23: Telegraph line from E-1 to the village, branch to the D command post | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_bakery` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_brick_townhouse` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_cafe_corner` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_chateau_grand` | Landmarks | excluded: B3 grand chateau, the centre of the Chateau-town map (M1); no chateau stood at Saint-Laurent | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `vil_chateau_keep` | Landmarks | excluded: Belongs to Le Plessis-Lastelle (Mont-Castre) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_church` | Landmarks | 1: E: Saint-Laurent church (slots in the tower) | terrain-only base map | 1 | 1 | not used | not used | not used | not used | not used | 1 | not used | 1 |
| `vil_church_ruined` | Landmarks | excluded: Saint-Laurent's church stood; the ruined church is on the Mont-Castre summit | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_colombage_auge` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_corner_shop` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 14 | 1 | not used | not used | not used | not used | not used | 14 | not used | not used |
| `vil_cottage_barn` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 3 | 1 | not used | not used | not used | not used | not used | 3 | not used | 1 |
| `vil_farm_courtyard` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_gatehouse_chapel` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used | 7 | not used | not used |
| `vil_granite_longere` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | 2 |
| `vil_longhouse_thatched` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | not used | not used | not used | not used | not used | not used | 8 | not used | 1 |
| `vil_manor_turret` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used | 7 | not used | not used |
| `vil_merchant_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_mill_house` | Houses | excluded: B3 house for the Chateau-town map (M1); no mill stream on the E-1 plateau | terrain-only base map | 4 | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `vil_norman_farmhouse` | Houses | 2: D: farm HQ; Saint-Laurent | terrain-only base map | 6 | 1 | not used | not used | not used | not used | not used | 6 | not used | 1 |
| `vil_notary_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_pension_hotel` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_rectory` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | 1 | not used | not used | not used | not used | not used | 8 | not used | not used |
| `vil_relais_inn` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_rendered_shutters` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used | not used | not used | not used |
| `vil_stone_barn` | Houses | 3: C: barn strongpoint; D farm; Saint-Laurent | terrain-only base map | 5 | 1 | not used | not used | not used | not used | not used | 5 | not used | 1 |
| `vil_terrace_pair` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used | not used | not used | not used | 15 | not used | not used |
| `vil_townhouse_jettied` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used | not used | not used | not used | 15 | not used | not used |
| `vil_villa_deauville` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 9 | 1 | not used | not used | not used | not used | not used | 9 | not used | not used |
| `wat_crater_pool` | Water | not placed: V1 water feature added after the map was built (Beautify / the user place it) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wat_pond_irregular` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wat_pond_round` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wat_puddles` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | 12 | not used | 2 | not used | not used | not used |
| `wat_stream_bend` | Water | not placed: V1 stream piece (snaps with V6) added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wat_stream_end` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bt_creek` | Water | Blackwater Thaw only: the creek as one graded, flowing stream ribbon and the lane puddles (written by tools/mapgen/maps/blackwater_thaw.py) |
| `bt_cable_station` | Landmarks | Blackwater Thaw only: the far-ridge cable-haul station silhouette (written by blackwater_thaw.py) |
| `bt_flume` | Small buildings | Blackwater Thaw only: the waterworks court's stepped header channel (written by blackwater_thaw.py) |
| `bt_boundary` | Effects | Blackwater Thaw only: the play-area limit as hidden collider boxes along the envelope (written by blackwater_thaw.py) |
| `bt_boundary_gate` | Effects | Blackwater Thaw only: the play limit across the castle road, solid only during a match (written by blackwater_thaw.py) |
| `bt_castle` | Landmarks | Blackwater Thaw only: the scenic hilltop castle on the north-west spur (written by blackwater_thaw.py) |
| `castle_interior` | Landmarks | Blackwater Thaw: the hilltop castle's rooms, built only near (loadRadius), placed with bt_castle (written by blackwater_thaw.py) |
| `castle_hilltop` | Landmarks | The hilltop castle as one palette asset: the shell with its explorable interior (built only near) and lamps as companions (written by blackwater_thaw.py); doors are open doorways (no swinging doors yet) |
| `bt_snowcap_vil_stone_barn` | Effects | Blackwater Thaw only: the roof snow cap of vil_stone_barn (written by blackwater_thaw.py) |
| `bt_snowcap_vil_rendered_shutters` | Effects | Blackwater Thaw only: the roof snow cap of vil_rendered_shutters (written by blackwater_thaw.py) |
| `bt_snowcap_small_storehouse` | Effects | Blackwater Thaw only: the roof snow cap of small_storehouse (written by blackwater_thaw.py) |
| `bt_snowcap_small_stone_dairy` | Effects | Blackwater Thaw only: the roof snow cap of small_stone_dairy (written by blackwater_thaw.py) |
| `bt_snowcap_small_carpenter_workshop` | Effects | Blackwater Thaw only: the roof snow cap of small_carpenter_workshop (written by blackwater_thaw.py) |
| `bt_snowcap_sd_market_lean_to` | Effects | Blackwater Thaw only: the roof snow cap of sd_market_lean_to (written by blackwater_thaw.py) |
| `bt_snowcap_house_stone_cottage` | Effects | Blackwater Thaw only: the roof snow cap of house_stone_cottage (written by blackwater_thaw.py) |
| `bt_snowcap_house_farmhouse_2story` | Effects | Blackwater Thaw only: the roof snow cap of house_farmhouse_2story (written by blackwater_thaw.py) |
| `bt_vent_smoke` | Effects | Blackwater Thaw: the smithy chimney's smoke emitter (vent_smoke); usable on any map |
| `bt_orchard_tree_a` | Vegetation | Blackwater Thaw only: a pruned winter orchard tree (written by blackwater_thaw.py) |
| `bt_orchard_tree_b` | Vegetation | Blackwater Thaw only: a pruned winter orchard tree (written by blackwater_thaw.py) |
| `bt_orchard_tree_c` | Vegetation | Blackwater Thaw only: a pruned winter orchard tree (written by blackwater_thaw.py) |
| `bt_orchard_tree_d` | Vegetation | Blackwater Thaw only: a pruned winter orchard tree (written by blackwater_thaw.py) |
| `wat_stream_straight` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | 63 | not used | not used | not used | not used | not used | not used | 63 | not used | not used |
| `wire_concertina` | Defences | as `line_concertina` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wire_fence` | Defences | as `line_wire_fence` | terrain-only base map | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `wire_knife_rest` | Defences | as `line_knife_rest` (3 single) | terrain-only base map | not used | 3 | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_bunks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_collapse` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_generator` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_store` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_sump` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_corner` | not used | not used | not used | not used | not used | 12 | not used | not used | not used | not used | not used | not used | not used |
| `tun_corner_lined` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `tun_crawl` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_cross` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used | not used |
| `tun_end` | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp` | not used | not used | not used | not used | not used | 33 | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_dead` | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_flicker` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_red` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_shadow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_stairs` | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used | not used | not used |
| `tun_straight` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used | not used |
| `tun_straight_kink` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_straight_lined` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_straight_niche` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_tee` | not used | not used | not used | not used | not used | 9 | not used | not used | not used | not used | not used | not used | not used |
| `tun_wide` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `brm_bulkhead` | not used | not used | not used | not used | not used | not used | 88 | not used | not used | not used | not used | not used | not used |
| `brm_cell_pool` | not used | not used | not used | not used | not used | not used | 69 | not used | not used | not used | not used | not used | not used |
| `brm_cell_pool_basin` | not used | not used | not used | not used | not used | not used | 15 | not used | not used | not used | not used | not used | not used |
| `brm_cell_pool_flooded` | not used | not used | not used | not used | not used | not used | 41 | not used | not used | not used | not used | not used | not used |
| `brm_cell_service` | not used | not used | not used | not used | not used | not used | 76 | not used | not used | not used | not used | not used | not used |
| `brm_cell_service_cross` | not used | not used | not used | not used | not used | not used | 46 | not used | not used | not used | not used | not used | not used |
| `brm_cell_yellow` | not used | not used | not used | not used | not used | not used | 122 | not used | not used | not used | not used | not used | not used |
| `brm_cell_yellow_damp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `brm_chair_lone` | not used | not used | not used | not used | not used | not used | 54 | not used | not used | not used | not used | not used | not used |
| `brm_column_p` | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used | not used |
| `brm_column_y` | not used | not used | not used | not used | not used | not used | 22 | not used | not used | not used | not used | not used | not used |
| `brm_panel` | not used | not used | not used | not used | not used | not used | 1408 | not used | not used | not used | not used | not used | not used |
| `brm_panel_dead` | not used | not used | not used | not used | not used | not used | 51 | not used | not used | not used | not used | not used | not used |
| `brm_panel_lit` | not used | not used | not used | not used | not used | not used | 469 | not used | not used | not used | not used | not used | not used |
| `brm_partition` | not used | not used | not used | not used | not used | not used | 130 | not used | not used | not used | not used | not used | not used |
| `brm_partition_l` | not used | not used | not used | not used | not used | not used | 28 | not used | not used | not used | not used | not used | not used |
| `brm_partition_low` | not used | not used | not used | not used | not used | not used | 17 | not used | not used | not used | not used | not used | not used |
| `brm_partition_short` | not used | not used | not used | not used | not used | not used | 27 | not used | not used | not used | not used | not used | not used |
| `brm_pile_chairs` | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used | not used | not used | not used |
| `brm_pile_wardrobes` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used | not used |
| `brm_pool_light` | not used | not used | not used | not used | not used | not used | 114 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_armchair_floor` | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_bed_wall` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_chair_wall` | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_table_floor` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_wardrobe_floor` | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used | not used |
| `brm_sunk_wardrobe_wall` | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used | not used | not used |
| `brm_wall_p` | not used | not used | not used | not used | not used | not used | 99 | not used | not used | not used | not used | not used | not used |
| `brm_wall_p_arch` | not used | not used | not used | not used | not used | not used | 77 | not used | not used | not used | not used | not used | not used |
| `brm_wall_p_door` | not used | not used | not used | not used | not used | not used | 30 | not used | not used | not used | not used | not used | not used |
| `brm_wall_p_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `brm_wall_s` | not used | not used | not used | not used | not used | not used | 131 | not used | not used | not used | not used | not used | not used |
| `brm_wall_s_door` | not used | not used | not used | not used | not used | not used | 60 | not used | not used | not used | not used | not used | not used |
| `brm_wall_s_wide` | not used | not used | not used | not used | not used | not used | 24 | not used | not used | not used | not used | not used | not used |
| `brm_wall_y` | not used | not used | not used | not used | not used | not used | 238 | not used | not used | not used | not used | not used | not used |
| `brm_wall_y_door` | not used | not used | not used | not used | not used | not used | 211 | not used | not used | not used | not used | not used | not used |
| `brm_wall_y_wide` | not used | not used | not used | not used | not used | not used | 126 | not used | not used | not used | not used | not used | not used |
| `brm_column_s` | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used |
| `tun_breach` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_cross_shelled` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_dugout` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_magazine` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_mess` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_office` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_pump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_workshop` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_s_corner` | not used | not used | not used | not used | not used | 15 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_s_cross` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_s_end` | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_s_straight` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_s_tee` | not used | not used | not used | not used | not used | 13 | not used | not used | not used | not used | not used | not used | not used |
| `tun_straight_shelled` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used | not used | not used |
| `tun_tee_shelled` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used | not used | not used |
| `brm_cell_yellow_b` | not used | not used | not used | not used | not used | not used | 121 | not used | not used | not used | not used | not used | not used |
| `brm_cell_yellow_c` | not used | not used | not used | not used | not used | not used | 239 | not used | not used | not used | not used | not used | not used |
| `tun_cable_reel` | not used | not used | not used | not used | not used | 13 | 14 | not used | not used | not used | not used | not used | not used |
| `tun_chamber_shaft` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_compressor` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_conveyor` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `tun_drill_rig` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `tun_duct` | not used | not used | not used | not used | not used | 5 | 4 | not used | not used | not used | not used | not used | not used |
| `tun_fan` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `tun_hopper` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `tun_ladder` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used | not used | not used | not used |
| `tun_ore_pile` | not used | not used | not used | not used | not used | 14 | 15 | not used | not used | not used | not used | not used | not used |
| `tun_powder_crates` | not used | not used | not used | not used | not used | 8 | 7 | not used | not used | not used | not used | not used | not used |
| `tun_pump` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `tun_ramp_12` | not used | not used | not used | not used | not used | 23 | 15 | not used | not used | not used | not used | not used | not used |
| `tun_ramp_24` | not used | not used | not used | not used | not used | 40 | 17 | not used | not used | not used | not used | not used | not used |
| `tun_straight_humped` | not used | not used | not used | not used | not used | 1 | 3 | not used | not used | not used | not used | not used | not used |
| `tun_straight_meshed` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_straight_timbered` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_tool_rack` | not used | not used | not used | not used | not used | 5 | 7 | not used | not used | not used | not used | not used | not used |
| `tun_warning_board` | not used | not used | not used | not used | not used | 10 | 15 | not used | not used | not used | not used | not used | not used |
| `tun_winch` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_cart_derailed` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_cart_ore` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_cart_tipped` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used | not used | not used | not used |
| `tun_track_buffer` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used | not used | not used | not used |
| `tun_track_curve` | not used | not used | not used | not used | not used | 9 | 9 | not used | not used | not used | not used | not used | not used |
| `tun_track_ramp_12` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used | not used | not used | not used |
| `tun_track_ramp_24` | not used | not used | not used | not used | not used | 9 | 8 | not used | not used | not used | not used | not used | not used |
| `tun_track_straight` | not used | not used | not used | not used | not used | 5 | 6 | not used | not used | not used | not used | not used | not used |
| `tun_track_points` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used | not used | not used |
| `line_mod_chainlink` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `line_mod_fence_white_picket` | not used | not used | not used | not used | not used | not used | not used | 30 | not used | not used | not used | not used | not used |
| `line_mod_hedge_modern` | not used | not used | not used | not used | not used | not used | not used | 16 | not used | not used | not used | not used | not used |
| `line_mod_hedge_modern_tall` | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used |
| `line_mod_road_2lane` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_sidewalk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ammo_box_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_apc_wheeled` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_barrel_group` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 | not used | not used | not used |
| `mod_barrel_steel_rust` | not used | not used | not used | not used | not used | not used | not used | 1 | 2 | 2 | not used | not used | not used |
| `mod_bench` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used | not used | not used | not used |
| `mod_bike_rack` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_bld_bigbox` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_bungalow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_diner_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_family_house` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_fastfood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_gas_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_mixed_use` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_office_setback` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_office_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_family` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_glass` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_concrete` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_box_truck` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `mod_bus_shelter` | not used | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used | not used | not used |
| `mod_cable_spool_large` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_camo_net` | not used | not used | not used | not used | not used | not used | not used | 1 | 2 | 1 | not used | not used | not used |
| `mod_car_shell_burned` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | not used | not used | not used | not used |
| `mod_cart_corral` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_cinder_wall` | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used |
| `mod_city_bus` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_concrete_pipe` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_concrete_pipe_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 | not used | not used | not used |
| `mod_construction_fence` | not used | not used | not used | not used | not used | not used | not used | 14 | 1 | not used | not used | not used | not used |
| `mod_container_20` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_container_20_damaged` | not used | not used | not used | not used | not used | not used | not used | 1 | 2 | not used | not used | not used | not used |
| `mod_container_40` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_container_40_open` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_container_40_stack` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_crate_plastic_stack` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_crate_wood` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 2 | not used | not used | not used |
| `mod_culvert_headwall` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_delivery_van` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_dumpster_frontload` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_dumpster_rolloff` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_excavator` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_fallen_tree` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_fighting_position_oh` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_fire_hydrant` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_flower_bed` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_fountain_tiered` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_garden_bench` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_garden_shed` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used |
| `mod_gazebo` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_generator_set` | not used | not used | not used | not used | not used | not used | not used | 2 | 1 | 2 | not used | not used | not used |
| `mod_greenhouse` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_guard_tower` | not used | not used | not used | not used | not used | not used | not used | 2 | 3 | 2 | not used | not used | not used |
| `mod_hard_cases` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_hatchback` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_2x1` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used |
| `mod_hesco_3x1` | not used | not used | not used | not used | not used | not used | not used | 4 | 37 | 1 | not used | not used | not used |
| `mod_hvac_unit` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_ibc_tote` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_jerry_cans` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `mod_jersey_barrier` | not used | not used | not used | not used | not used | not used | not used | 4 | 2 | not used | not used | not used | not used |
| `mod_jersey_barrier_damaged` | not used | not used | not used | not used | not used | not used | not used | 15 | 4 | 1 | not used | not used | not used |
| `mod_lumber_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 | not used | not used | not used |
| `mod_mailbox` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_mil_generator` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 | not used | not used | not used |
| `mod_mil_utility` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_mil_utility_green` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 | not used | not used | not used |
| `mod_newspaper_box` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_ambulance` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_nt_armory` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_barn_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_barracks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_boat_motor` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 1 | not used | not used | not used |
| `mod_nt_boat_trailer` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `mod_nt_boathouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_boxcar` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_nt_cafe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_channel_steps` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_nt_clock_tower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_compound_wall` | not used | not used | not used | not used | not used | not used | not used | 39 | not used | not used | not used | not used | not used |
| `mod_nt_compound_wall_breach` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_compound_wall_culvert` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_construction_frame` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_construction_frame_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_covered_passage` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_crawl_frame` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_excavation_shoring` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_nt_farmhouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_fire_engine` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_nt_flatcar` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_footbridge` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_hotel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_split` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_two` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_hq` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_kiosk` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_motel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_motor_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_municipal` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_parking_garage` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_pier` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_pontoon_finger` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used |
| `mod_nt_promenade_rail` | not used | not used | not used | not used | not used | not used | not used | 24 | not used | not used | not used | not used | not used |
| `mod_nt_radar_tower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_rail_buffer` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_rail_track` | not used | not used | not used | not used | not used | not used | not used | 12 | not used | not used | not used | not used | not used |
| `mod_nt_road_bridge` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_nt_shop_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_shop_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_site_office` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_storage_tank` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_tower_crane` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_training_frame` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_training_wall` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_warehouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_waterfront_house` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_workshop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pallet_stack` | not used | not used | not used | not used | not used | not used | not used | 4 | 1 | 2 | not used | not used | not used |
| `mod_parking_meter` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_pickup` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_picnic_table` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used |
| `mod_planter_concrete` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used |
| `mod_play_roundabout` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_play_seesaw` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_play_spring_rider_twin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_portable_toilet` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_raised_bed` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_razor_coil` | not used | not used | not used | not used | not used | not used | not used | 2 | 8 | 2 | not used | not used | not used |
| `mod_rebar_bundle` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_roadblock` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_rock_large` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_rock_medium` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used | not used | not used | not used |
| `mod_sandbag_position` | not used | not used | not used | not used | not used | not used | not used | 4 | 4 | 2 | not used | not used | not used |
| `mod_scaffold_bay` | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used |
| `mod_sedan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_red` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_wrecked` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_semi_tractor` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_semi_trailer` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_skip` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 | not used | not used | not used |
| `mod_sliding_gate` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_street_lamp` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_sun_lounger` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_suv` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_chest` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_traffic_drum` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_traffic_light` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_trash_bin` | not used | not used | not used | not used | not used | not used | not used | 6 | 1 | 1 | not used | not used | not used |
| `mod_tree_blossom` | not used | not used | not used | not used | not used | not used | not used | 27 | not used | not used | not used | not used | not used |
| `mod_tree_columnar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tree_ornamental` | not used | not used | not used | not used | not used | not used | not used | 80 | not used | not used | not used | not used | not used |
| `mod_tyre_pile` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 | not used | not used | not used |
| `mod_water_barrier_red` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_weapon_rack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_apartment_block` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_hospital` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_car_dealership` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_pump_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_bus_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_compound_gate` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_fire_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_climbing_frame` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_streetlight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_jeep_willys` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_jeep_ww2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_canopy` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `car_truck_cargo` | not used | not used | not used | not used | not used | not used | not used | not used | 2 | 1 | not used | not used | not used |
| `car_truck_flak` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_kitchen` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_medical` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `eng_outpost` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_armchair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bar_stool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bath` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bedside_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bleachers` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bookshelf` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_booth_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_booth_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cafeteria_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_canvas_square` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_canvas_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cart_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chalkboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_checkout` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_clothes_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_coffee_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_condiment_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_copier` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cubicle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_desk_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_table4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_table6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dish_sink` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dishwasher` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_display_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dresser` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dumbbell_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_end_cap` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_exercise_bike` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_filing_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_floor_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fridge` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fridge_case` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fryer` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_gondola` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_griddle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_gym_mat` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_high_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hoop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hospital_bed` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_ice_machine` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_iv_stand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_kitchen_base` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_kitchen_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_locker_bench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_lockers` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_lockers_school` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_meeting_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_menu_board` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_microwave` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_mirror` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_nightstand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_notice_board` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_nurse_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_office_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_patient_monitor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_plant_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_plant_tall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_prep_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_privacy_curtain` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_range_oven` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_reception_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rest_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rest_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rug` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_server_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_service_counter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_shop_wall_shelf` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_shower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sink_unit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_soda_machine` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_stock_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_stove` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_student_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_table_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_teacher_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_toilet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_treadmill` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_stand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_vanity` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_vending` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_waiting_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_walkin_door` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wall_clock` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wardrobe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_warming_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_water_cooler` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_weight_bench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wheelchair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_whiteboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_fence_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_road_4lane` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_air_compressor` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_ammo_can` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_antenna_mast` | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used |
| `mod_asphalt_crater` | not used | not used | not used | not used | not used | not used | not used | 20 | not used | not used | not used | not used | not used |
| `mod_barrel_plastic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_barrel_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_barriers_weathered` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_basketball_court` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_basketball_half` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bench_backless` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bird_bath` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_leisure` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_school` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_stone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_townhouse_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_retractable` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_retracted` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_cable_spool_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_car_wreck_sedan` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_cars_burned_pair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_gate_open` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_panel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_razor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_collapsed_slab` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_container_20_open` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_crate_plastic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_crosswalk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_decking_terrace` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ecology_block` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_electrical_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fence_white_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fence_white_picket_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_field_radio` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fountain_round` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fountain_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gate_chainlink` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gate_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_glass_pile` | not used | not used | not used | not used | not used | not used | not used | 48 | 2 | not used | not used | not used | not used |
| `mod_hedge_box_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_tall_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_tall_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_1x1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_1x2` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_hesco_2x1_half` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_hesco_3x2` | not used | not used | not used | not used | not used | not used | not used | not used | 16 | not used | not used | not used | not used |
| `mod_highway_divider` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_jersey_barrier_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_lamp_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_manhole_cover` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_palisade_fence` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 53 | not used | not used | not used |
| `mod_pallet` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_parking_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_medium` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_surface` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pergola` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_planter_corten` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_fence` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_fence_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_monkey_bars` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_safety_tiles_green` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_safety_tiles_red` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_sandpit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_spring_rider` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_swings_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_swings_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_retaining_wall` | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_crossroads` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_t_junction` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rock_small` | not used | not used | not used | not used | not used | not used | not used | 144 | not used | not used | not used | not used | not used |
| `mod_roundabout` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rubble_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rubble_small` | not used | not used | not used | not used | not used | not used | not used | 45 | 1 | not used | not used | not used | not used |
| `mod_sawhorse_barricade` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_burned` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 | not used | not used | not used |
| `mod_sidewalk_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_post_stop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_post_street` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_speed_bump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_stepping_stones` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_streetlight_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_swimming_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tennis_court` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_ball` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_balls` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_cone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_traffic_cone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_traffic_cone_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_twall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_utility_pole` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_van_burned` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_van_wrecked` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_chunks` | not used | not used | not used | not used | not used | not used | not used | 33 | not used | not used | not used | not used | not used |
| `mod_water_barrier_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rack_modern` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_12ga` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_50` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_556` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_762` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_9mm` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_rocket` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_backpack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_backpack_mil` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_bomber` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_pistol` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_rifle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_smg` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_sniper` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_molotov` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_muzzle_brake` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_parachute` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_recoil_pad` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_10x` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_4x` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_acog` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_reddot` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_suppressor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_road_2lane_ruin` | not used | not used | not used | not used | not used | not used | not used | 20 | 1 | not used | not used | not used | not used |
| `line_mod_sidewalk_ruin` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used | not used | not used | not used |
| `mod_bld_apartment_block_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_bigbox_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_bungalow_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | 1 | not used | not used | not used | not used |
| `mod_bld_bus_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_diner_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 11 | not used | not used | not used | not used | not used |
| `mod_bld_family_house_ruin` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_bld_fastfood_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_gas_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_bld_hospital_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_mixed_use_ruin` | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used | not used |
| `mod_bld_office_setback_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_office_wide_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_family_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_bld_tower_glass_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_armory_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_nt_barn_steel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 | not used | not used | not used |
| `mod_nt_barracks_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `mod_nt_boathouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_cafe_ruin` | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used | not used |
| `mod_nt_car_dealership_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_clock_tower_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_construction_frame_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_construction_frame_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_farmhouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_fire_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_hotel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_house_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 14 | 1 | 1 | not used | not used | not used |
| `mod_nt_house_split_ruin` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used |
| `mod_nt_house_two_ruin` | not used | not used | not used | not used | not used | not used | not used | 13 | not used | 1 | not used | not used | not used |
| `mod_nt_hq_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_nt_motel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_motor_pool_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `mod_nt_municipal_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_parking_garage_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_pump_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used | not used |
| `mod_nt_radar_tower_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 | not used | not used | not used |
| `mod_nt_shop_row_ruin` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used |
| `mod_nt_shop_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 20 | 1 | not used | not used | not used | not used |
| `mod_nt_site_office_ruin` | not used | not used | not used | not used | not used | not used | not used | 8 | 1 | not used | not used | not used | not used |
| `mod_nt_warehouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_nt_waterfront_house_ruin` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_nt_workshop_ruin` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 1 | not used | not used | not used |
| `mod_sapling_paving` | not used | not used | not used | not used | not used | not used | not used | 57 | not used | not used | not used | not used | not used |
| `mod_weeds_clump` | not used | not used | not used | not used | not used | not used | not used | 129 | not used | 172 | not used | not used | not used |
| `mod_weeds_crack` | not used | not used | not used | not used | not used | not used | not used | 190 | 26 | not used | not used | not used | not used |
| `line_mod_road_4lane_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_leisure_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_large_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_school_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_station_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_stone_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_townhouse_row_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_crosswalk_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ivy_patch_ground` | not used | not used | not used | not used | not used | not used | not used | 95 | not used | not used | not used | 13 | not used |
| `mod_parking_large_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_medium_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_small_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_surface_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_crossroads_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_end_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_t_junction_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_vines_hanging` | not used | not used | not used | not used | not used | not used | not used | not used (VR1: the tall blocks' curtains are wall ivy now; interiors of ruins still get it) | not used | not used | not used | not used | not used |
| `mod_vines_hanging_long` | not used | not used | not used | not used | not used | not used | not used | not used (VR1: see mod_vines_hanging) | not used | not used | not used | not used | not used |
| `mod_mil_hut_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 | not used | not used | not used |
| `mod_mil_hut` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pcar_compact` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `mod_pcar_compact_wreck` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_pcar_hatchback` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_pcar_hatchback_wreck` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_pcar_minivan` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_pcar_minivan_wreck` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_pcar_offroad` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used | not used | not used | not used |
| `mod_pcar_offroad_wreck` | not used | not used | not used | not used | not used | not used | not used | 7 | 1 | 1 | not used | not used | not used |
| `mod_pcar_pickup` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used | not used |
| `mod_pcar_pickup_wreck` | not used | not used | not used | not used | not used | not used | not used | 4 | 1 | 1 | not used | not used | not used |
| `mod_pcar_sedan` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used | not used | not used |
| `mod_pcar_sedan_wreck` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used | not used |
| `mod_pcar_sport` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used | not used |
| `mod_pcar_sport_wreck` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 1 | not used | not used | not used |
| `mod_pcar_suv` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used | not used |
| `mod_pcar_suv_wreck` | not used | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used | not used | not used |
| `mod_pcar_wagon` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used | not used |
| `mod_pcar_wagon_wreck` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used | not used | not used |
| `br_bandage` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_death_crate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_medkit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_boxer_apc` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_humvee` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `furn_art_a_00` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_14` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_17` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_18` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_22` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_26` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_27` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_28` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_29` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_30` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_31` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_32` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_34` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_35` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_36` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_37` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_38` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_39` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_40` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_41` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_42` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_43` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_44` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_46` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_47` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_48` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_49` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_51` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_52` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_53` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_54` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_00` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_15` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_24` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_25` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_28` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_29` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_30` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_31` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_34` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_36` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_37` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_38` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_40` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_42` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_43` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_44` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_48` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_49` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_52` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_53` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_54` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_56` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_57` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_58` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bed_rustic` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_bed_victorian` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_cash_register` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_chair_soviet` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_low` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_panel` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bunk_pine` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chair_bowl` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chair_egg` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chair_tufted` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chair_white` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chair_woodframe` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_plant_pot` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa_corner` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa_grey` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_synth` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_bench` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_crt` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_flat` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_led` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_piano` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sacks_burlap` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_sofa_chesterfield` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_stool_metal` | not hand-placed: auto-furnish (PR5 user model) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nt3_tree_dead_ash` | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used |
| `nt3_tree_dead_oak` | not used | not used | not used | not used | not used | not used | not used | not used | 37 | not used | not used | not used | not used |
| `nt3_tree_dry_ash` | not used | not used | not used | not used | not used | not used | not used | not used | 29 | not used | not used | not used | not used |
| `nt3_tree_forest_ash` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used |
| `nt3_tree_forest_oak` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 111 | not used | not used | not used |
| `nt3_tree_forest_poplar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ammo_crate_old` | not used | not used | not used | not used | not used | not used | not used | not used | 3 | 4 | not used | not used | not used |
| `mod_helipad_raised` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_range_booth` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_range_flag` | not used | not used | not used | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `mod_range_marker` | not used | not used | not used | not used | not used | not used | not used | not used | 6 | 5 | not used | not used | not used |
| `mod_range_shelter` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_range_sign` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_range_target_double` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_range_target_moving` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_range_target_paper` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `mod_range_target_single` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_helipad_ground` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `heli_ah64d_apache` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `mod_tent_command` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `mod_tent_medical` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `mod_tent_mess` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `mod_tent_sleeping` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `car_ural_4320` | not used | not used | not used | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tank_abrams_m1a2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tank_panzer_iv` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tank_sherman_m4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_column_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_column_m` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_fire_altar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_glow_ember` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 15 | not used |
| `cave_lid_l0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 90 | not used |
| `cave_lid_l1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 64 | not used |
| `cave_lid_l2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 81 | not used |
| `cave_lid_m0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 16 | not used |
| `cave_lid_m1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 17 | not used |
| `cave_lid_m2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 15 | not used |
| `cave_lid_s0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_lid_s1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_light_cold` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_light_crack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_light_crack_ns` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_ribwall_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used |
| `cave_ribwall_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_ribwall_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_shelf_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used |
| `cave_shelf_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 | not used |
| `cave_shelf_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 | not used |
| `cave_spine_arch` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_spire_ancient` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalactites_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalactites_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalactites_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalactites_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalagmites_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalagmites_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalagmites_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_stalagmites_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_column_xl` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_column_h45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used |
| `cave_drip_down_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used |
| `cave_drip_down_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 36 | not used |
| `cave_drip_down_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 39 | not used |
| `cave_drip_down_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 28 | not used |
| `cave_drip_down_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 16 | not used |
| `cave_drip_down_13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used |
| `cave_drip_down_14` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used |
| `cave_drip_down_15` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_drip_down_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 16 | not used |
| `cave_drip_down_17` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 44 | not used |
| `cave_drip_down_18` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 51 | not used |
| `cave_drip_down_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 29 | not used |
| `cave_drip_down_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 45 | not used |
| `cave_drip_down_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 | not used |
| `cave_drip_down_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used |
| `cave_drip_down_22` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_drip_down_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used |
| `cave_drip_down_24` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 | not used |
| `cave_drip_down_25` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 40 | not used |
| `cave_drip_down_26` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 50 | not used |
| `cave_drip_down_27` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 37 | not used |
| `cave_drip_down_28` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 | not used |
| `cave_drip_down_29` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used |
| `cave_drip_down_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 33 | not used |
| `cave_drip_down_30` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_drip_down_31` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_drip_down_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 | not used |
| `cave_drip_down_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_drip_down_6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_drip_down_7` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used |
| `cave_drip_down_8` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 | not used |
| `cave_drip_down_9` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 29 | not used |
| `cave_drip_up_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 23 | not used |
| `cave_drip_up_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 | not used |
| `cave_drip_up_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used |
| `cave_drip_up_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_drip_up_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_drip_up_13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 | not used |
| `cave_drip_up_14` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used |
| `cave_drip_up_15` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used |
| `cave_drip_up_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 19 | not used |
| `cave_drip_up_17` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used |
| `cave_drip_up_18` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 9 | not used |
| `cave_drip_up_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used |
| `cave_drip_up_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_drip_up_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used |
| `cave_drip_up_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_drip_up_22` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_drip_up_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used |
| `cave_drip_up_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used |
| `cave_drip_up_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 | not used |
| `cave_drip_up_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used |
| `cave_drip_up_6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 | not used |
| `cave_drip_up_7` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 9 | not used |
| `cave_drip_up_8` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 20 | not used |
| `cave_drip_up_9` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_light_rim` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_light_shaft_main` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_organ_0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used |
| `cave_organ_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_organ_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_organ_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used |
| `cave_standing_stones` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_steps` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_column_h60` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_column_h75` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_light_uplight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used |
| `cave_monument` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `cave_brazier` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used |
| `cave_crystals` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used |
| `cave_crystals_violet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cave_fungi` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 53 | not used |
| `cave_light_mineral_up` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 | not used |
| `cave_light_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used |
| `cave_crystals_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 37 | not used |
| `cave_lid_v0` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 30 | not used |
| `cave_lid_v1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 25 | not used |
| `cave_lid_v2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 16 | not used |
| `ph_boulder_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used | 7 | 3 |
| `ph_dead_tree_trunk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 9 | 113 |
| `ph_dead_tree_trunk_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 10 | not used | 4 | 41 |
| `ph_dry_branches_medium_01_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 11 | 285 |
| `ph_dry_branches_medium_01_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 6 | 15 |
| `ph_dry_branches_medium_01_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used | 7 | 313 |
| `ph_fern_02_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 266 | not used | 289 | 482 |
| `ph_fern_02_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 261 | not used | 253 | 435 |
| `ph_fern_02_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 237 | not used | 278 | 356 |
| `ph_fern_02_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 313 | not used | 283 | 305 |
| `ph_fir_sapling_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 91 | not used | 96 | not used |
| `ph_fir_sapling_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 104 | not used | 106 | not used |
| `ph_fir_sapling_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 90 | not used | 92 | not used |
| `ph_fir_sapling_medium_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 30 | not used | 1 | 93 |
| `ph_fir_sapling_medium_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 39 | not used | 5 | 3 |
| `ph_fir_sapling_medium_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 40 | not used | 5 | 72 |
| `ph_fir_tree_01_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 44 | not used | 48 | not used |
| `ph_fir_tree_01_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 72 | not used | 77 | 59 |
| `ph_fir_tree_01_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 54 | not used | 70 | 194 |
| `ph_grass_medium_01_large_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 58 | not used | 56 | not used |
| `ph_grass_medium_01_large_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 73 | not used | 56 | not used |
| `ph_grass_medium_01_large_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 53 | not used | 71 | not used |
| `ph_grass_medium_01_mid_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 77 | not used | 75 | not used |
| `ph_grass_medium_01_mid_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 54 | not used | 62 | not used |
| `ph_grass_medium_01_mid_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 70 | not used | 75 | not used |
| `ph_grass_medium_01_small_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 66 | not used | 69 | not used |
| `ph_grass_medium_01_small_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 71 | not used | 56 | not used |
| `ph_grass_medium_01_tall_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 63 | not used | 58 | not used |
| `ph_grass_medium_01_tall_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 71 | not used | 55 | not used |
| `ph_grass_medium_01_tall_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 60 | not used | 67 | 9 |
| `ph_grass_medium_01_tiny_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 72 | not used | 62 | not used |
| `ph_grass_medium_01_tiny_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 57 | not used | 55 | not used |
| `ph_grass_medium_01_tiny_e` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 65 | not used | 51 | not used |
| `ph_island_tree_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 | not used |
| `ph_pine_roots_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 3 | 116 |
| `ph_pine_roots_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 7 | not used |
| `ph_pine_sapling_medium_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 35 | not used | 1 | 53 |
| `ph_pine_sapling_medium_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 34 | not used | 4 | 9 |
| `ph_pine_sapling_medium_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 35 | not used | 18 | 65 |
| `ph_pine_sapling_small_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 85 | not used | 112 | not used |
| `ph_pine_sapling_small_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 128 | not used | 112 | not used |
| `ph_pine_sapling_small_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 118 | not used | 114 | not used |
| `ph_pine_tree_01_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 44 | not used | 31 | not used |
| `ph_pine_tree_01_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 49 | not used | 39 | 83 |
| `ph_pine_tree_01_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 45 | not used | 51 | 31 |
| `ph_rock_09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 19 | not used | 12 | 118 |
| `ph_rock_moss_set_01_rock01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 12 | 4 |
| `ph_rock_moss_set_01_rock02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 12 | 132 |
| `ph_rock_moss_set_01_rock03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used | 6 | 8 |
| `ph_rock_moss_set_01_rock04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 8 | 2 |
| `ph_rock_moss_set_01_rock05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 | not used | 5 | 7 |
| `ph_rock_moss_set_01_rock06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 5 | 17 |
| `ph_rock_moss_set_02_rock08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used | 3 | 122 |
| `ph_rock_moss_set_02_rock09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 6 | not used |
| `ph_rock_moss_set_02_rock10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 4 | 5 |
| `ph_rock_moss_set_02_rock11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 8 | not used |
| `ph_rock_moss_set_02_rock12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 3 | 1 |
| `ph_rock_moss_set_02_rock13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used | 1 | not used |
| `ph_root_cluster_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 | not used | 2 | 1 |
| `ph_root_cluster_02_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 4 | 164 |
| `ph_root_cluster_02_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 6 | not used |
| `ph_root_cluster_02_e` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 | not used | 12 | 2 |
| `ph_root_cluster_02_f` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used | 5 | 51 |
| `ph_root_cluster_02_g` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 2 | not used |
| `ph_root_cluster_02_h` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used | 6 | not used |
| `ph_sand_rocks_small_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 | not used | 4 | not used |
| `ph_shrub_03_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 128 | not used | 124 | not used |
| `ph_shrub_03_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 123 | not used | 122 | not used |
| `ph_shrub_03_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 110 | not used | 112 | not used |
| `ph_shrub_03_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 114 | not used | 120 | not used |
| `ph_stone_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 21 | not used | 5 | not used |
| `ph_coast_rocks_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_coastal_cliff_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_coastal_cliff_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_grass_medium_01_tiny_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_grass_medium_01_tiny_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_grass_medium_01_tiny_f` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rock_face_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 |
| `ph_rock_moss_set_02_rock07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 |
| `ph_root_cluster_02_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 99 |
| `ph_root_cluster_02_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `rb1_container_cabin` | Small buildings | not used: RB1 reference building (the rb1_test map, docs/rb1-buildings.md) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rb1_log_house` | Houses | not used: RB1 reference building (the rb1_test map, docs/rb1-buildings.md) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rb1_ref_shop` | Small buildings | not used: RB1 comparison, the reference model beside its recreation (rb1_test, docs/rb1-buildings.md) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rb1_weapon_shop` | Small buildings | not used: RB1 reference building (the rb1_test map, docs/rb1-buildings.md) | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_holo` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_pistol` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_thermal` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_fuel_dump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_fuel_point` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_generator_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_guard_point` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_motor_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_rocks_deadwood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_rocks_quiver` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_sandbag_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_scrap_pile` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_supply_dump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_desert_supply_stack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_barricade` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_bench_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_cafe_terrace` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_cafe_terrace_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_doorstep` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_market_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_market_stall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_town_roadblock` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_barrel_stack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_camera_pole` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_generator_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_loading_area` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_pallet_stack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_pipe_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_pipe_yard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_tool_carts` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_workshop_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `cl_yard_workshop_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_sandbag_bags_high` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_sandbag_bags_low` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sb_bag_lib` | hidden library: the four bag meshes every sandbag box part draws (src/sandbags.cpp), never placed | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `def_sandbag_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor_16x9` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor_4x3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor_curved` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor_superwide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor_ultrawide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_def_sandbag_bags_high` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_def_sandbag_bags_low` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_wall_brick_rail` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_wall_cement` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_wall_compound` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_rack_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_rack_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_rack_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_rack_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_unit_box` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_unit_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_unit_tall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ac_unit_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_14` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_billboard_15` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_bollard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_box_closed` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_box_open` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_box_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_caution_sign` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_cone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_container_closed` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_container_open` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_crate_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_crate_wood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_door_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_door_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_door_frame` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_drum_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_drum_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_extinguisher` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_folding_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_gas_cylinder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_generator_big` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_generator_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_ladder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_light_batten` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_light_batten_twin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_light_pendant` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_light_tower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_locker_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_locker_tall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pallet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_partition` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_bend` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_elbow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_flange` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_long` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_short` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_tee` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_pipe_valve` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_planks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_platform_trolley` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_racking_low` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_racking_tall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_sack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_sack_truck` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_sign_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_stepladder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_tools_scatter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_vent_fan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_vent_louvre` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_washbasin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_wc` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_worktable_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fp_worktable_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_compound_wall_plain` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_security_hut` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_airport` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_bicycle_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_bike_lane_wrong_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_caution_slow_down` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_chevron` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_construction_zone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_curve_right` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_dead_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_detour` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_detour_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_disabled_parking` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_do_not_enter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_airport` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_bicycle_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_bike_lane_wrong_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_caution_slow_down` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_chevron` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_construction_zone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_curve_right` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_dead_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_detour` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_detour_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_disabled_parking` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_do_not_enter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_exit_6_tab` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_guide_sign` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_interstate_95` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_large_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_left_or_straight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_left_turn_only` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_men_at_work` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_merging_traffic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_no_bicycles` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_no_left_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_no_parking_any_time` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_no_right_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_no_u_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_one_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_parking` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_pedestrian_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_railroad_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_reverse_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_road_narrows` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_road_work_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_roundabout_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_route_66` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_school_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_slippery_road` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_slow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_speed_limit_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_stop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_tow_away_zone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_traffic_signal_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_two_way_traffic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_u_turn_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_under_construction` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_winding_road` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_wrong_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_yield` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_face_yield_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_guide_sign` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_interstate_95` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_large_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_left_or_straight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_left_turn_only` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_men_at_work` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_merging_traffic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_no_bicycles` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_no_left_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_no_parking_any_time` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_no_right_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_no_u_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_one_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_parking` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_pedestrian_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_railroad_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_reverse_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_road_narrows` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_road_work_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_roundabout_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_route_66` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_school_crossing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_slippery_road` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_slow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_speed_limit_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_stop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_tow_away_zone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_traffic_signal_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_two_way_traffic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_u_turn_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_under_construction` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_winding_road` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_wrong_way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_yield` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_yield_ahead` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_vent_grille_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_vent_grille_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_brick_rail_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_brick_rail_pillar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_cement_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_cement_pillar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_compound_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_compound_doorway` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_compound_pillar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_window_frame_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_window_frame_wood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_window_wide_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_window_wide_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pad_concrete_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pad_dirt_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_adjustable_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_alarm_clock_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_antique_ceramic_vase_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_armchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_bar_chair_round_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_barbershopchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_barrel_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_barrel_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_barrel_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_barrel_stove` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_bench_vice_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_binder_notebook` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_binoculars` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_bleach_bottle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_bolt_cutters_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_book_encyclopedia_set_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_boombox` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_blowtorch` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_candleholders` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_goblets` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_pan_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_pot_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_brass_pot_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_vase_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_brass_vase_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_caged_hanging_light` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_camera_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_can_rusted` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_cardboard_box_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_carved_wooden_plate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_cashregister_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_cassette_player` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ceiling_fan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_cement_bag` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ceramic_pot` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 |
| `ph_ceramic_vase_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ceramic_vase_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ceramic_vase_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_ceramic_vase_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_chandelier_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_chandelier_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_chandelier_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_cheiridopsis_succulent` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_chemistry_set` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_chess_set` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_classic_laptop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_classicconsole_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_classicnightstand_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_clipboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_coffee_table_round_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_coffeecart_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_coffeetable_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_compost_bag_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 |
| `ph_compost_bags` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_concrete_road_barrier` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_concrete_road_barrier_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_covered_car` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_crowbar_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dartboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dead_quiver_branch_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dead_quiver_branch_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dead_quiver_trunk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_desk_lamp_arm_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dining_chair_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dining_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_drawer_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_drill_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_drill_press_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_dustpan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_electric_stove` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_exterior_aircon_unit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_fancy_picture_frame_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_fancy_picture_frame_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_filmstrip_projector_8mm` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_fire_alarm` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_fire_hydrant` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_flood_lights` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_folding_wooden_stool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 |
| `ph_gallinera_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_gallinera_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_garden_gnome` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 |
| `ph_garden_hose_wall_mounted_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_garden_sprinkler_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_gothicbed_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_gothiccabinet_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_gothiccommode_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_greenchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hand_plane_no4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hand_truck` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_handsaw_wood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hanging_industrial_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hanging_picture_frame_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hanging_picture_frame_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hanging_picture_frame_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_hatchet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 9 |
| `ph_industrial_caged_sconce` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_coffee_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_pastic_container` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_pipe_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_storage_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_wall_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_industrial_wall_sconce` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_jug_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 18 |
| `ph_korean_fire_extinguisher_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ladder_sectioned_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_lantern_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 |
| `ph_lantern_chandelier_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_large_iron_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_life_jacket` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_lifebuoy` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_lubricant_spray` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_mantel_clock_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_measuring_tape_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_medical_box` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_megaphone_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_detector` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_jerrycan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_metal_jerrycan_green` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_jug` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 |
| `ph_metal_office_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_stool_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_stool_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_stool_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_tool_chest` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_toolbox` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_metal_trash_can` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_mid_century_lounge_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modern_arm_chair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modern_ceiling_lamp_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modern_coffee_table_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modern_coffee_table_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modern_wooden_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_circular_01_bend` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_circular_01_fan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_circular_01_straight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_rectangular_01_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_rectangular_01_straight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_airduct_rectangular_01_vent` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_chainlink_fence_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_chainlink_fence_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_chainlink_fence_panel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_chainlink_fence_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_electric_cables_box_4way` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_electric_cables_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_electric_cables_straight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_electric_cables_turn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_electricity_poles` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_fire_escape_ladder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_fire_escape_platform` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_fire_escape_platform_bottom` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_fire_escape_railing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_fire_escape_stairs` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_industrial_pipes_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_metal_gutter_corner` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_metal_gutter_downpipe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_metal_gutter_section` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_s_curve` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_straight_1m` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_straight_2m` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_support` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_tee` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_modular_pipes_valve_chamber` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_mounted_fluorescent_lights` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_multi_cleaner_5_litre` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_multi_cleaner_bottle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_boulder_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_boulder_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_boulder_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_boulder_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_boulders_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_rocks_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_namaqualand_stones_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ocean_buoy` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_oil_tin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_old_bed_frame` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 2 |
| `ph_old_drill_press` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_old_gas_mask` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_old_military_compressor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_old_military_crate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_old_tyre` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 |
| `ph_ornate_mirror_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_othonna_cerarioides` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ottoman_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_outdoor_table_chair_set_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_overhead_crane` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_bench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 13 |
| `ph_painted_wooden_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_cabinet_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_chair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 |
| `ph_painted_wooden_chair_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_nightstand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_shelves` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_sofa` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_painted_wooden_stool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 21 |
| `ph_painted_wooden_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_picke_dirty_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 |
| `ph_pipe_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_planter_box_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_planter_box_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_planter_box_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_planter_pot_clay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_plastic_bottle_gallon` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_broom` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_container` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_crate_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_crate_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_crate_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_jerrycan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plastic_monobloc_chair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_plunger` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_portable_generator` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_portable_searchlight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_portable_welding_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_pot_enamel_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 |
| `ph_potted_plant_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_potted_plant_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_potted_plant_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_power_box_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_projector_screen` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_propane_tank` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_propane_torch_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_pull_chain_light_socket` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_quiver_tree_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_quiver_tree_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_ratchet_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_retro_multimeter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rock_face_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rockingchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 |
| `ph_rollershutter_door` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rollershutter_window_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rollershutter_window_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rollershutter_window_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_round_wooden_table_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_round_wooden_table_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rubber_boots` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 3 |
| `ph_rusted_hacksaw` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_rusted_spade_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 7 |
| `ph_rusted_wheel_rim_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 |
| `ph_rusted_wheel_rim_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_scandinavian_masonry_heater` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_schoolchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_schooldesk_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_searsia_burchellii` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_searsia_lucida` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_security_camera_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_security_camera_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_security_light` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_seeding_tray_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 |
| `ph_shelf_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_side_table_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_side_table_tall_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_sledgehammer_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_small_lpg_tank` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_small_oil_can_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_small_wooden_table_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_sofa_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_sofa_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_sofa_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_spinning_wheel_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_spray_paint_bottles` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_spray_paint_bottles_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_standing_chalkboard_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_standing_picture_frame_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_stationery_supplies` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_steel_frame_shelves_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_steel_frame_shelves_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_steel_frame_shelves_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_stone_fire_pit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_street_lamp_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_street_lamp_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tea_set_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_television_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_television_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_throw_pillows_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tire_pump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tool_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_trashbag` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_treasure_chest` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tree_stump_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tree_stump_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_trowel_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_utility_box_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_utility_box_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_cabinet_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_crutches_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_day_bed` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_electric_kettle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_flashlight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_grandfather_clock_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_hand_drill` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_microwave` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_oil_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_radio_transceiver` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_stapler` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_suitcase` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 |
| `ph_vintage_telephone_wall_clock` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_vintage_wooden_drawer_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 |
| `ph_wall_clock` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_water_manhole_cover` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_watering_can_metal_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_wetfloorsign_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wheelchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wicker_basket_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 16 |
| `ph_wicker_basket_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 |
| `ph_wine_barrel_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 36 |
| `ph_wine_bottles_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 15 |
| `ph_wooden_axe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_wooden_barrels_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 |
| `ph_wooden_bookshelf_worn` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_wooden_bowl_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 |
| `ph_wooden_bowl_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_broom` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 25 |
| `ph_wooden_bucket_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 11 |
| `ph_wooden_bucket_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 25 |
| `ph_wooden_candlestick` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_crate_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 |
| `ph_wooden_crate_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 6 |
| `ph_wooden_cutting_board` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_display_shelves_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_ladder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 8 |
| `ph_wooden_ladder_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_wooden_lantern_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_military_crate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_wooden_picnic_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 5 |
| `ph_wooden_stool_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 14 |
| `ph_wooden_table_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_woodenchair_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_woodentable_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_woodentable_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_woodentable_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_worn_metal_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 4 |
| `sd_beacon_red` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_blue` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_green` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_grey` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_maroon` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_orange` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_box_yellow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_chainlink_panel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_compound_wall_warm` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_compound_wall_warm_breach` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_compound_wall_warm_plain` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_crane_rail` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_drain` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_dry_container_20_damaged` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_dry_jersey_barrier` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_dry_pallet_stack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_dry_rock_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_brick` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_cafe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_render_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_render_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_render_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_slate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_stone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_timber_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_facade_timber_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_flood_mast` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_forklift` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_lit_windows` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_bay_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_dash` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_hazard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mark_line` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_market_lean_to` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_memorial_railing` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_monument` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mud_wall_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mud_wall_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mud_wall_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_mud_wall_door` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_puddle_yard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_roof_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_roof_bay_timber` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_shade_canopy` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_a_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_a_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_a_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_b_arrow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_b_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sign_b_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_slab_joints` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_sodium_light` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_stall_awning_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_stall_awning_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_stall_awning_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_static_boxer` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_static_humvee` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_static_truck_canopy` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_static_truck_cargo` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `sd_static_ural` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_stencil_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_stencil_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_town_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_town_wall_door` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_mil_hut` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_armory` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_barracks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_hq` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_motor_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_pump_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_site_office` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_warm_nt_workshop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_washing_line` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `sd_work_light` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `smithing_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `smithing_table_tech_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `smithing_table_tech_pegboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `smithing_table_tech_saw` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tank_bradley_m2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gas_drums` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gas_station_soviet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_lpg_dispenser` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_lpg_dispensers_pair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_old_garage` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_debris_soda_can` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_debris_stove` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_adjustable_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_clamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_hammer_claw` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_hammer_club` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_hatchet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_pincers` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_prybar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_screwdriver` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_socket_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_wrench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_debris_papers_pile` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_debris_papers_scattered` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_concrete_outbuilding` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_crate_slatted` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 36 |
| `line_pa_fence_picket` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 20 |
| `pa_chest_wooden` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 21 |
| `pa_fence_picket_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_fruit_crate_high` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 12 |
| `pa_fruit_crate_low` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 20 |
| `pa_fruit_crate_mid` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 15 |
| `pa_military_house` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `pa_tree_dead_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 43 |
| `pa_tree_dead_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 95 |
| `pa_tree_dead_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 100 |
| `pa_tree_dead_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 98 |
| `pa_tree_dead_e` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 41 |
| `pa_abandoned_shop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_rail_caboose` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_warehouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `pa_log_house_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `pa_stone_house_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `pa_shed_ruin_long` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `pa_shack_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `mp1_footbridge` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `mp1_watchtower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `ph_island_tree_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_island_tree_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ph_tree_small_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_apple_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_apple_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_apple_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_ash_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_ash_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_ash_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_dead_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_dead_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_dead_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_dead_d` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_oak_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_oak_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_oak_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_poplar_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_poplar_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_sapling_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_sapling_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_street_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_street_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tr_street_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `ivy_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_boulder_l_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_boulder_l_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_boulder_s_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_group_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_lime_l_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_lime_l_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_lime_m_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_lime_m_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_outcrop_l_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_outcrop_l_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_outcrop_m_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_outcrop_m_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_l_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_l_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_m_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_m_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_s_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_rock_s_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_schist_m_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_schist_m_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_slab_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_slab_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_blackthorn_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_blackthorn_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_bramble_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_bramble_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_bramble_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_gorse_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_gorse_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hawthorn_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hawthorn_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hawthorn_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hazel_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hazel_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_hazel_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_thicket_a` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `bs_thicket_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bare_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bare_tree_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bare_tree_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bare_tree_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_big_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_big_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_big_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_big_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_big_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_medium_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_medium_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_medium_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_medium_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_medium_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_small_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_small_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_small_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_small_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_bush_small_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_clover_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_clover_patch_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_clover_patch_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_clover_patch_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_clover_patch_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_deadbranch_tree_7` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_gnarly_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_gnarly_tree_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_gnarly_tree_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_gnarly_tree_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_gnarly_tree_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_grass_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_grass_reeds` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_bush_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_bush_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_bush_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_sapling_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_sapling_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_sapling_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_single_tree_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_single_tree_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_single_tree_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_small_bush_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_small_bush_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_lilac_small_bush_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_large_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_large_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_large_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_medium_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_medium_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_medium_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_sapling_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_sapling_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_sapling_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_small_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_small_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_maple_small_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_big_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_big_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_big_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_big_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_large_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_large_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_large_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_large_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_med_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_med_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_med_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_sapling_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_sapling_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_sapling_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_small_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_small_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_oak_small_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_group` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_stone_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_stone_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_stone_5` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_stone_stone_6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_succulent_agave` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_succulent_ball` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_succulent_columns` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_succulent_pack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_succulent_prickly_pear` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_stone_boulder` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_stone_boulder_g` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_stone_outcrop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_stone_outcrop_m` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rk_stone_rock_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_agave_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 15 | not used | not used | not used | not used |
| `fl_desertshrub_agave_2` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_agave_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_cholla_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used |
| `fl_desertshrub_cholla_2` | Desert | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used |
| `fl_desertshrub_cholla_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_creosote_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 13 | not used | not used | not used | not used |
| `fl_desertshrub_creosote_2` | Desert | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used | not used | not used |
| `fl_desertshrub_creosote_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_ocotillo_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used | not used |
| `fl_desertshrub_ocotillo_2` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_ocotillo_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_ocotillo_4` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_pack` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_desertshrub_yucca_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used |
| `fl_desertshrub_yucca_2` | Desert | not used | not used | not used | not used | not used | not used | not used | 12 | not used | not used | not used | not used |
| `fl_desertshrub_yucca_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_drygrass_pack` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_drygrass_patch_1` | Desert | not used | not used | not used | not used | not used | not used | not used | 64 | not used | not used | not used | not used |
| `fl_drygrass_patch_2` | Desert | not used | not used | not used | not used | not used | not used | not used | 65 | not used | not used | not used | not used |
| `fl_drygrass_patch_3` | Desert | not used | not used | not used | not used | not used | not used | not used | 76 | not used | not used | not used | not used |
| `fl_drygrass_tuft_1` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_drygrass_tuft_2` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `fl_drygrass_tuft_3` | Desert | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |

## Notes

- LN2 (docs/lines.md#ln2): fence, wire, wall, sandbag and hedge pieces are drawn as lines (`line_*` rows: lines per map). The map scripts still lay the pieces; `write_map` chains them into lines (`tools/mapgen/mapgen/lines.py`). A piece row reads "as `line_<type>`", plus any single pieces kept as objects (knife rests).

- `gun_pak40` stands in for WN65's 5 cm gun; there is no 5 cm gun asset. It is the R667's own companion.
- The ruined beach house is an intact `house_stone_cottage`, because the catalogue has no ruined-house variant.
- SPEC 3.5 also lists helmets, jerrycans, spent cases, abandoned rifles, field telephones and camouflage netting. None of
  them exist as catalogue assets yet, so the map uses sandbags, crates and ammunition boxes instead.
- The spec's knocked-out vehicle at the Saint-Laurent crossroads is missing for the same reason. The only vehicles are
  the Firefly and Tiger tank GLBs, both excluded above.
