# Asset usage on the default maps

SPEC 3.5: every catalogue asset is used where it is historically plausible, or listed here with the reason it is not.
Counts are placed instances (companions included), from the `tools/mapgen` build reports (`out/<map>.summary.json`).
`--selftest-maps` fails when a catalogue asset is missing from this table.

- **Omaha:** *Normandy Beaches - Omaha, Easy Red* (W5a, `tools/mapgen/maps/omaha_easy_red.py`).
- **Mont-Castre:** *Normandy Forest - Mont-Castre, Hill 122* is a terrain-only base map (no assets placed; the user places them in the editor), built by W5b. Every cell in that column reads "terrain-only base map".
- **Chateau town:** *Chateau town* (M1, `tools/mapgen/maps/chateau_town.py`): placed counts, companions included; "not used" = not on this map.
- **D-day:** *D-day* (D1, `tools/mapgen/maps/dday.py`): placed counts, companions included; "not used" = not on this map.
- **Tunnels:** *Tunnels* (UG1, `tools/mapgen/maps/tunnels.py`): placed counts; "not used" = not on this map.
- **Backrooms:** *Backrooms* (BR1, `tools/mapgen/maps/backrooms.py`): placed counts; "not used" = not on this map.

| Asset | Category | Omaha | Mont-Castre | Chateau town | D-day | Tunnels | Backrooms |
|---|---|---|---|---|---|---|---|
| `aw_dugout_log` | Atlantic Wall | 1: C: log-roofed dugout behind the draw-head roadblock | terrain-only base map | not used | not used | not used | not used |
| `aw_dugout_personnel` | Atlantic Wall | 1: B: WN64 personnel dugout behind the crest | terrain-only base map | not used | not used | not used | not used |
| `aw_foxhole` | Atlantic Wall | 15: Three per objective (fallback positions) | terrain-only base map | not used | not used | not used | not used |
| `aw_r612_unfinished` | Atlantic Wall | 1: B: WN64, the unfinished R612 | terrain-only base map | not used | not used | not used | not used |
| `aw_r667_casemate` | Atlantic Wall | 1: A: WN65 Ruquet casemate, embrasure west along the beach (companion PaK 40, shell crates at the rear door) | terrain-only base map | not used | 1 | not used | not used |
| `aw_tobruk_mg` | Atlantic Wall | 1: A: WN65 MG ringstand | terrain-only base map | not used | 2 | not used | not used |
| `aw_tobruk_mortar` | Atlantic Wall | 2: A: WN65; B: WN64 mortar Tobruk | terrain-only base map | not used | not used | not used | not used |
| `aw_tobruk_turret` | Atlantic Wall | 1: A: WN65 tank-turret Tobruk (ref photo 6) | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_bend` | Atlantic Wall | not used: added with D-day (45-degree trench bend) | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_corner` | Atlantic Wall | 2: D: trench line across the hedgerow fields | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_cross` | Atlantic Wall | not used: added with D-day (trench crossroads) | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_entry` | Atlantic Wall | 9: Ends of every trench run (A, B, D) | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_straight` | Atlantic Wall | 22: A; B (crest line over the beach); D | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_tee` | Atlantic Wall | not used: added with D-day (trench T-junction) | terrain-only base map | not used | not used | not used | not used |
| `aw_trench_zigzag` | Atlantic Wall | 20: A, B and D trench runs | terrain-only base map | not used | not used | not used | not used |
| `bcg_abatis` | Bocage | 45: Plateau: felled trees across the flank lanes; attack-path cover fill above the bluff | terrain-only base map | not used | not used | not used | not used |
| `bcg_hedgerow_corner` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used |
| `bcg_hedgerow_gap` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used |
| `bcg_hedgerow_straight` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used |
| `bcg_marsh_patch` | Bocage | 8: Marsh pools on the sand shelf | terrain-only base map | not used | not used | not used | not used |
| `bcg_quarry_face` | Bocage | excluded: No quarry at E-1 (a Mont-Castre feature) | terrain-only base map | not used | not used | not used | not used |
| `bcg_rampart` | Bocage | excluded: The Roman / medieval earthworks belong to Mont-Castre | terrain-only base map | not used | not used | not used | not used |
| `bcg_sunken_lane` | Bocage | 98: Plateau: west and east flank sunken lanes | terrain-only base map | 3 | not used | not used | not used |
| `bch_at_ditch` | Beach obstacles | 26: Across the E-1 mouth (s 476); second ditch at the draw head (C) | terrain-only base map | not used | not used | not used | not used |
| `bch_belgian_gate` | Beach obstacles | 90: Belt 1, 250 m out, with the Easy Red gaps | terrain-only base map | not used | 12 | not used | not used |
| `bch_lcvp_beached` | Landing craft | 4: A1 landing (spawn cover, rack crates in the well) | terrain-only base map | not used | 6 | not used | not used |
| `bch_lcvp_intact` | Landing craft | 2: A1: craft still afloat behind the first wave | terrain-only base map | not used | 3 | not used | not used |
| `bch_lcvp_wrecked` | Landing craft | 5: Wrecks on the tidal flat (cover) | terrain-only base map | not used | 6 | not used | not used |
| `bch_log_ramp_mined` | Beach obstacles | 72: Belt 3 (ramps) | terrain-only base map | not used | 13 | not used | not used |
| `bch_log_stake_mine` | Beach obstacles | 253: Belt 2 (two staggered rows of stakes) | terrain-only base map | not used | 40 | not used | not used |
| `bch_minefield_fence` | Beach obstacles | as `line_minefield_fence` | terrain-only base map | as `line_minefield_fence` | not used | not used | not used |
| `bch_minen_sign` | Beach obstacles | 19: Achtung Minen boards along the minefields | terrain-only base map | not used | not used | not used | not used |
| `bch_roadblock` | Beach obstacles | 1: C: across the draw road | terrain-only base map | not used | not used | not used | not used |
| `bch_seawall_wood` | Beach | 55: East half of the beach behind the shingle | terrain-only base map | not used | not used | not used | not used |
| `bch_shingle_bank` | Beach | 100: The whole shingle bank (s 404-416) | terrain-only base map | not used | not used | not used | not used |
| `bunker_command_shelter` | Bunkers | 1: D: battery and CP shelter | terrain-only base map | not used | not used | not used | not used |
| `bunker_field_shelter` | Bunkers | 2: C and D | terrain-only base map | not used | not used | not used | not used |
| `bunker_large_casemate` | Bunkers | excluded: No large casemate at E-1: WN64 had only the unfinished R612; the H669s stood at other draws | terrain-only base map | not used | 1 | not used | not used |
| `bunker_mg_casemate` | Bunkers | 1: A: WN65 flank MG casemate | terrain-only base map | not used | 2 | not used | not used |
| `bunker_observation_post` | Bunkers | 1: B: WN64 observation post | terrain-only base map | not used | not used | not used | not used |
| `bunker_pak_casemate` | Bunkers | excluded: WN65's gun casemate is the R667 (`aw_r667_casemate`) | terrain-only base map | not used | 1 | not used | not used |
| `crate_mixed_supply` | Supply | 14: Ammunition dump at D; dressing at every objective | terrain-only base map | 10 | 15 | 9 | 8 |
| `crate_pak40_he` | Supply | 7: A: behind the R667; D: ammunition dump | terrain-only base map | not used | 9 | not used | not used |
| `def_sandbag_curved` | Defences | as `line_sandbag_low` | terrain-only base map | as `line_sandbag_low` | not used | not used | not used |
| `def_sandbag_straight` | Defences | as `line_sandbag_low` | terrain-only base map | as `line_sandbag_low` | not used | not used | not used |
| `def_tank_trap` | Defences | 233: Belt 4 (hedgehogs); beach cover fill | terrain-only base map | not used | 58 | not used | not used |
| `drs_barrels` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 90 | 11 | not used | not used |
| `drs_bridge_stone_large` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used |
| `drs_bridge_stone_small` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used |
| `drs_cart` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 62 | 5 | not used | not used |
| `drs_churchyard_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 2 | 1 | not used | not used |
| `drs_flower_bed` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 233 | 22 | not used | not used |
| `drs_garden_wall` | Dressing | as `line_garden_wall` | terrain-only base map | as `line_garden_wall` | not used | not used | not used |
| `drs_gate_piers` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 17 | not used | not used | not used |
| `drs_gravestone_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 34 | 8 | not used | not used |
| `drs_gravestone_round` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 64 | 15 | not used | not used |
| `drs_hay_bale_stack` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 77 | 13 | not used | not used |
| `drs_picket_fence` | Dressing | as `line_picket_fence` | terrain-only base map | as `line_picket_fence` | not used | not used | not used |
| `drs_sandbag_ring` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 7 | not used | not used | not used |
| `drs_sandbag_wall` | Defences | as `line_sandbag_wall` | terrain-only base map | as `line_sandbag_wall` | not used | not used | not used |
| `drs_tomb_chest` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 22 | 7 | not used | not used |
| `drs_well` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 19 | 3 | not used | not used |
| `drs_wooden_x_obstacle` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 32 | 30 | not used | not used |
| `drs_woodpile` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 86 | 11 | not used | not used |
| `env_sea_shallows` | Beach | 87: The sea (s < 50): shallow water surface | terrain-only base map | not used | 20 | not used | not used |
| `fence_post_wire` | Dressing | as `line_post_wire_fence` | terrain-only base map | as `line_post_wire_fence` | not used | not used | not used |
| `field_gate` | Dressing | 6: Bocage field gates | terrain-only base map | 1 | not used | not used | not used |
| `fire_brazier` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used |
| `fire_campfire` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used |
| `furn_armchair` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_armoire` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 6 |
| `furn_barrel` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 18 | 16 | 12 | not used |
| `furn_barrel_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_basket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_bed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 4 |
| `furn_bed_iron` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_bench` | Furniture | 1: D: farmyard CP | terrain-only base map | 12 | 2 | not used | not used |
| `furn_bookcase` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_books` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used |
| `furn_bottles` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_bucket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 20 | 8 | not used | not used |
| `furn_candelabra` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): church altars | terrain-only base map | not used | not used | not used | not used |
| `furn_candle` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bedrooms, small rooms | terrain-only base map | not used | not used | not used | not used |
| `furn_candlestick` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_candlesticks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_chair_ladder` | Furniture | 3: D: CP map table | terrain-only base map | not used | not used | not used | not used |
| `furn_chair_slat` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_chest` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_clock` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_coat_hooks` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_coat_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_coat_stand` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_commode` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_crate` | Furniture | 11: Ammunition dumps and dressing | terrain-only base map | 21 | 13 | 13 | not used |
| `furn_crockery` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_curtains` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_curtains_half` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_debris_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_debris_plaster` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_dresser` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_fire_hearth` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): a fire on every hearth | terrain-only base map | not used | not used | not used | not used |
| `furn_fireplace` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_frame_dusk` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_frame_landscape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_frame_portrait` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_frame_seascape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_frame_small` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_gramophone` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_hay_bales` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 37 | not used | not used | not used |
| `furn_hutch` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_hutch_glazed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_jars` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_jug_bowl` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp_caged` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bunkers, casemates and low ceilings | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp_ceiling` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): kitchens, sitting rooms, halls, offices, classrooms, taverns | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp_pendant_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp_table` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): sitting rooms | terrain-only base map | not used | not used | not used | not used |
| `furn_lamp_wall_oil` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_ammo_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_bunk` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used |
| `furn_mil_crates` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used |
| `furn_mil_fusebox` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_gasmask_hooks` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_generator` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 1 | not used |
| `furn_mil_jerrycans` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used |
| `furn_mil_locker` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used |
| `furn_mil_map` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_phone` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_radio` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_rifle_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used |
| `furn_mil_table` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used |
| `furn_mirror` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_pan_rail` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_pantry` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_papers` | Furniture | 1: D: CP map table (maps) | terrain-only base map | not used | not used | not used | not used |
| `furn_pew` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_picture` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_plate_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_portrait` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_pot` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_radio` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_rug_kilim` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_rug_large` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_rug_medallion` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_rug_runner` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_sacks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 33 | 9 | not used | not used |
| `furn_school_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_sconce` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_sconce_candle` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_screen_folding` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_settee` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_shelf` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_side_table` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_sideboard` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_stool` | Furniture | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used |
| `furn_stove` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_stove_potbelly` | Furniture | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used |
| `furn_suitcases` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_table_dining` | Furniture | 1: D: the HQ map table in the farmyard | terrain-only base map | not used | not used | not used | 4 |
| `furn_table_low` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used |
| `furn_table_small` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used |
| `furn_tool_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_utensil_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_wall_clock` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_wall_shelf` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_washstand` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `furn_wood_pile` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 24 | not used | not used | not used |
| `furn_workbench` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used |
| `fx_impact_smoke` | Lighting | not used: added with D-day (smouldering impact, smoke column) | terrain-only base map | not used | 6 | not used | not used |
| `fx_wreck_fire` | Lighting | not used: added with D-day (burning wreckage, smoke column) | terrain-only base map | not used | 2 | not used | not used |
| `gun_flak18` | Ordnance | 1: D: 8.8 cm Flak 18 in the orchard (representative, SPEC 3.3) | terrain-only base map | not used | 2 | not used | not used |
| `gun_m2hb_tripod` | Field guns | 1: US .50 set up on the shingle near A2 | terrain-only base map | not used | not used | not used | not used |
| `gun_mg42_embrasure` | Field guns | 16: Every sandbag MG nest | terrain-only base map | not used | 10 | not used | not used |
| `gun_pak40` | Field guns | 1: A: in the R667 (stands in for the 5 cm gun) | terrain-only base map | not used | 2 | not used | not used |
| `hedgerow_bocage` | Vegetation | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used |
| `house_farmhouse_2story` | Houses | 4: Saint-Laurent | terrain-only base map | 9 | not used | not used | not used |
| `house_stone_cottage` | Houses | 7: Saint-Laurent; the beach house on the sand shelf | terrain-only base map | 11 | 1 | not used | not used |
| `kit_demo_folly` | Landmarks | excluded: the B1 geometry-kit demonstration piece (docs/asset-format.md#b1-geometry-kit), not a period building | terrain-only base map | not used | not used | not used | not used |
| `lamp_gas_street` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md); the Beautify street_lamps role lines roads with it | terrain-only base map | 10 | 4 | not used | not used |
| `lamp_hanging_lantern` | Lighting | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): barns, workshops, stores, church naves | terrain-only base map | not used | not used | not used | not used |
| `lamp_lantern_post` | Lighting | not hand-placed: Paths > Line auto paths with lamp posts (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used |
| `lamp_wall_lantern` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used |
| `large_municipal_building` | Large buildings | 1: Saint-Laurent (mairie) | terrain-only base map | 3 | not used | not used | not used |
| `large_stone_warehouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used |
| `large_village_inn` | Large buildings | 1: Saint-Laurent crossroads | terrain-only base map | 3 | not used | not used | not used |
| `large_village_schoolhouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used |
| `line_trench` | Lines | not used: LN1 trench line (old maps' trench pieces migrate to it on load) | terrain-only base map | not used | 10 | not used | not used |
| `line_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used |
| `line_concertina` | Lines | 5 | terrain-only base map | not used | 8 | not used | not used |
| `line_knife_rest` | Lines | not used | terrain-only base map | not used | not used | not used | not used |
| `line_minefield_fence` | Lines | 11 | terrain-only base map | not used | not used | not used | not used |
| `line_post_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used |
| `line_picket_fence` | Lines | 16 | terrain-only base map | 177 | 30 | not used | not used |
| `line_stone_wall` | Lines | 6 | terrain-only base map | 126 | 25 | not used | not used |
| `line_garden_wall` | Lines | 7 | terrain-only base map | 23 | 10 | not used | not used |
| `line_sandbag_wall` | Lines | not used | terrain-only base map | 12 | 9 | not used | not used |
| `line_sandbag_low` | Lines | 99 | terrain-only base map | 8 | 21 | not used | not used |
| `line_hedgerow` | Lines | 111 | terrain-only base map | 108 | not used | not used | not used |
| `mil_stronghold` | Bunkers | excluded: GX6 Atlantic Wall command stronghold (60 x 34 m with earthworks); larger than any Omaha strongpoint, for hand-built maps | terrain-only base map | not used | 1 | not used | not used |
| `nat_blackthorn_low` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 20 | not used | not used |
| `nat_boulder_granite_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_boulder_granite_s` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_boulder_limestone_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used |
| `nat_boulder_limestone_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 5 | not used | not used |
| `nat_boulder_schist_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_boulder_schist_slab` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_boulders_granite_group` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_bracken_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 42 | not used | not used |
| `nat_bramble_thicket` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 1 | not used | not used | not used |
| `nat_brush_patchy` | Vegetation | not placed: V9 ground cover (patchy brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `nat_brush_rank` | Vegetation | not placed: V9 ground cover (rank brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `nat_brush_scrub` | Vegetation | not placed: V9 ground cover (dry scrub, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 59 | not used | not used |
| `nat_clover_tufts` | Vegetation | not placed: V9 ground cover (clover and green tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 9 | not used | not used | not used |
| `nat_drygrass_sparse` | Vegetation | not placed: V9 ground cover (sparse dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 113 | not used | not used |
| `nat_drygrass_tall` | Vegetation | not placed: V9 ground cover (tall dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 3 | 419 | not used | not used |
| `nat_drygrass_tufts` | Vegetation | not placed: V9 ground cover (dry grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 12 | 177 | not used | not used |
| `nat_fern_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 59 | 18 | not used | not used |
| `nat_flowers_buttercup` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 21 | 1 | not used | not used |
| `nat_flowers_clover` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_flowers_cornflower` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 56 | 7 | not used | not used |
| `nat_flowers_cowparsley` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 63 | 37 | not used | not used |
| `nat_flowers_daisy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 103 | 47 | not used | not used |
| `nat_flowers_foxglove` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 3 | not used | not used |
| `nat_flowers_meadow` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 18 | not used | not used | not used |
| `nat_flowers_poppy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 110 | 39 | not used | not used |
| `nat_gorse_shrub` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 91 | not used | not used |
| `nat_green_tufts` | Vegetation | not placed: V9 ground cover (green grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 23 | 42 | not used | not used |
| `nat_hawthorn_bush` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 155 | 70 | not used | not used |
| `nat_meadow_mixed` | Vegetation | not placed: V9 ground cover (mixed meadow, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 44 | 62 | not used | not used |
| `nat_nettle_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 7 | 21 | not used | not used |
| `nat_outcrop_limestone` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 2 | not used | not used |
| `nat_outcrop_schist` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_sapling_ash` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_sapling_oak` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_scree_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used |
| `nat_stones_granite_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used |
| `nat_stones_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 4 | not used | not used |
| `nat_tallgrass_patch` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 37 | 111 | not used | not used |
| `nat_tallgrass_verge` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 9 | 75 | not used | not used |
| `nat_wheat_field` | Vegetation | not placed: V9 ground cover (wheat field square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 52 | not used | not used | not used |
| `nat_wheat_stubble` | Vegetation | not placed: V9 ground cover (wheat stubble square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 58 | not used | not used | not used |
| `nat_wheat_trampled` | Vegetation | not placed: V9 ground cover (trampled wheat square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 8 | not used | not used | not used |
| `path_concrete` | Paths | 1: A: apron behind the R667 | terrain-only base map | not used | not used | not used | not used |
| `path_dirt` | Paths | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used |
| `path_gravel` | Paths | 1: E: church path | terrain-only base map | not used | not used | not used | not used |
| `rack_allied` | Supply | 3: Attacker spawns A3-A5 | terrain-only base map | 5 | 4 | not used | not used |
| `rack_allied_crates` | Supply | 2: A1 (in an LCVP well) and A2 (on the shingle): crate racks, SPEC 3.2 | terrain-only base map | not used | not used | not used | not used |
| `rack_axis` | Supply | 5: Defender spawns D1-D5 | terrain-only base map | 5 | 4 | not used | not used |
| `rub_beam_tile_heap` | Rubble | not placed: V3 rubble (Beam and tile heap, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used |
| `rub_brick_pile_l` | Rubble | not placed: V3 rubble (Brick rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_brick_pile_m` | Rubble | not placed: V3 rubble (Brick rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_brick_pile_s` | Rubble | not placed: V3 rubble (Brick rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_collapsed_wall` | Rubble | not placed: V3 rubble (Collapsed wall section, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_debris_patch` | Rubble | not placed: V3 rubble (Scattered debris patch, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 15 | not used | not used |
| `rub_spill_strip` | Rubble | not placed: V3 rubble (Rubble spill strip (8 m), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_stone_pile_l` | Rubble | not placed: V3 rubble (Stone rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `rub_stone_pile_m` | Rubble | not placed: V3 rubble (Stone rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 1 | 2 | not used |
| `rub_stone_pile_s` | Rubble | not placed: V3 rubble (Stone rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used |
| `ruin_barn_burnt` | Ruins | not placed: V3 ruin (Burnt-out barn, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `ruin_cottage_corner` | Ruins | not placed: V3 ruin (Ruined cottage corner, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `ruin_farmhouse_gutted` | Ruins | not placed: V3 ruin (Gutted farmhouse shell, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `ruin_house_shelled` | Ruins | not placed: V3 ruin (Shelled house (two storeys, roof collapsed), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `ruin_shop_row` | Ruins | not placed: V3 ruin (Half-collapsed shop terrace, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `ruin_townhouse_facade` | Ruins | not placed: V3 ruin (Bombed townhouse (facade only), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used |
| `scn_treeline_a` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 25 | not used | not used |
| `scn_treeline_b` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 22 | not used | not used |
| `scn_treeline_c` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 26 | not used | not used |
| `shrub_bramble` | Vegetation | 3974: Bluff scrub (about 1 per 38 m2), draw undergrowth, dunes | terrain-only base map | 40 | 33 | not used | not used |
| `shrub_hazel` | Vegetation | 1070: Bluff scrub, E-1 draw undergrowth | terrain-only base map | 346 | 211 | not used | not used |
| `small_carpenter_workshop` | Small buildings | 2: Saint-Laurent | terrain-only base map | 8 | not used | not used | not used |
| `small_roadside_office` | Small buildings | 1: Saint-Laurent crossroads | terrain-only base map | 7 | not used | not used | not used |
| `small_stone_dairy` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 8 | 1 | not used | not used |
| `small_storehouse` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 14 | 1 | not used | not used |
| `stone_wall_low` | Dressing | as `line_stone_wall` | terrain-only base map | as `line_stone_wall` | not used | not used | not used |
| `tank_sherman_firefly` | Vehicles | excluded: The Firefly (17-pdr) was British, not at Omaha; no DD Sherman asset exists; player vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used |
| `tank_tiger_i` | Vehicles | excluded: No Tigers at Omaha; vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used |
| `tree_apple` | Vegetation | 396: Orchards (D battery, A5, two more) | terrain-only base map | 305 | 58 | not used | not used |
| `tree_ash` | Vegetation | 213: Hedgerow trees; wooded E-1 draw | terrain-only base map | 368 | 283 | not used | not used |
| `tree_oak` | Vegetation | 316: Hedgerow trees; wooded E-1 draw | terrain-only base map | 294 | 310 | not used | not used |
| `tree_poplar` | Vegetation | 73: Roadside poplars on the draw road; hedgerow trees | terrain-only base map | 130 | 149 | not used | not used |
| `utility_pole` | Dressing | 23: Telegraph line from E-1 to the village, branch to the D command post | terrain-only base map | not used | not used | not used | not used |
| `vil_bakery` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_brick_townhouse` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_cafe_corner` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_chateau_grand` | Landmarks | excluded: B3 grand chateau, the centre of the Chateau-town map (M1); no chateau stood at Saint-Laurent | terrain-only base map | 1 | not used | not used | not used |
| `vil_chateau_keep` | Landmarks | excluded: Belongs to Le Plessis-Lastelle (Mont-Castre) | terrain-only base map | not used | not used | not used | not used |
| `vil_church` | Landmarks | 1: E: Saint-Laurent church (slots in the tower) | terrain-only base map | 1 | 1 | not used | not used |
| `vil_church_ruined` | Landmarks | excluded: Saint-Laurent's church stood; the ruined church is on the Mont-Castre summit | terrain-only base map | not used | not used | not used | not used |
| `vil_colombage_auge` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_corner_shop` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 14 | 1 | not used | not used |
| `vil_cottage_barn` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 3 | 1 | not used | not used |
| `vil_farm_courtyard` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used |
| `vil_gatehouse_chapel` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used |
| `vil_granite_longere` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_longhouse_thatched` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | not used | not used | not used |
| `vil_manor_turret` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used |
| `vil_merchant_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_mill_house` | Houses | excluded: B3 house for the Chateau-town map (M1); no mill stream on the E-1 plateau | terrain-only base map | 4 | not used | not used | not used |
| `vil_norman_farmhouse` | Houses | 2: D: farm HQ; Saint-Laurent | terrain-only base map | 6 | 1 | not used | not used |
| `vil_notary_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used |
| `vil_pension_hotel` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used |
| `vil_rectory` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | 1 | not used | not used |
| `vil_relais_inn` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used |
| `vil_rendered_shutters` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used |
| `vil_stone_barn` | Houses | 3: C: barn strongpoint; D farm; Saint-Laurent | terrain-only base map | 5 | 1 | not used | not used |
| `vil_terrace_pair` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used |
| `vil_townhouse_jettied` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used |
| `vil_villa_deauville` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 9 | 1 | not used | not used |
| `wat_crater_pool` | Water | not placed: V1 water feature added after the map was built (Beautify / the user place it) | terrain-only base map | not used | not used | not used | not used |
| `wat_pond_irregular` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used |
| `wat_pond_round` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used |
| `wat_puddles` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used |
| `wat_stream_bend` | Water | not placed: V1 stream piece (snaps with V6) added after the map was built | terrain-only base map | not used | not used | not used | not used |
| `wat_stream_end` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | not used | not used | not used | not used |
| `wat_stream_straight` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | 63 | not used | not used | not used |
| `wire_concertina` | Defences | as `line_concertina` | terrain-only base map | as `line_concertina` | not used | not used | not used |
| `wire_fence` | Defences | as `line_wire_fence` | terrain-only base map | as `line_wire_fence` | not used | not used | not used |
| `wire_knife_rest` | Defences | as `line_knife_rest` (3 single) | terrain-only base map | as `line_knife_rest` | 3 | not used | not used |
| `tun_chamber_bunks` | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_collapse` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_chamber_generator` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_chamber_store` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_chamber_sump` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_corner` | not used | not used | not used | not used | not used | 12 | not used |
| `tun_corner_lined` | not used | not used | not used | not used | not used | 2 | not used |
| `tun_crawl` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_cross` | not used | not used | not used | not used | not used | 4 | not used |
| `tun_end` | not used | not used | not used | not used | not used | 10 | not used |
| `tun_lamp` | not used | not used | not used | not used | not used | 33 | not used |
| `tun_lamp_dead` | not used | not used | not used | not used | not used | 6 | not used |
| `tun_lamp_flicker` | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_red` | not used | not used | not used | not used | not used | 4 | not used |
| `tun_lamp_shadow` | not used | not used | not used | not used | not used | not used | not used |
| `tun_stairs` | not used | not used | not used | not used | not used | 5 | not used |
| `tun_straight` | not used | not used | not used | not used | not used | 4 | not used |
| `tun_straight_kink` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_straight_lined` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_straight_niche` | not used | not used | not used | not used | not used | not used | not used |
| `tun_tee` | not used | not used | not used | not used | not used | 9 | not used |
| `tun_wide` | not used | not used | not used | not used | not used | 2 | not used |
| `brm_bulkhead` | not used | not used | not used | not used | not used | not used | 88 |
| `brm_cell_pool` | not used | not used | not used | not used | not used | not used | 69 |
| `brm_cell_pool_basin` | not used | not used | not used | not used | not used | not used | 15 |
| `brm_cell_pool_flooded` | not used | not used | not used | not used | not used | not used | 41 |
| `brm_cell_service` | not used | not used | not used | not used | not used | not used | 76 |
| `brm_cell_service_cross` | not used | not used | not used | not used | not used | not used | 46 |
| `brm_cell_yellow` | not used | not used | not used | not used | not used | not used | 122 |
| `brm_cell_yellow_damp` | not used | not used | not used | not used | not used | not used | not used |
| `brm_chair_lone` | not used | not used | not used | not used | not used | not used | 54 |
| `brm_column_p` | not used | not used | not used | not used | not used | not used | 3 |
| `brm_column_y` | not used | not used | not used | not used | not used | not used | 22 |
| `brm_panel` | not used | not used | not used | not used | not used | not used | 1408 |
| `brm_panel_dead` | not used | not used | not used | not used | not used | not used | 51 |
| `brm_panel_lit` | not used | not used | not used | not used | not used | not used | 469 |
| `brm_partition` | not used | not used | not used | not used | not used | not used | 130 |
| `brm_partition_l` | not used | not used | not used | not used | not used | not used | 28 |
| `brm_partition_low` | not used | not used | not used | not used | not used | not used | 17 |
| `brm_partition_short` | not used | not used | not used | not used | not used | not used | 27 |
| `brm_pile_chairs` | not used | not used | not used | not used | not used | not used | 7 |
| `brm_pile_wardrobes` | not used | not used | not used | not used | not used | not used | 5 |
| `brm_pool_light` | not used | not used | not used | not used | not used | not used | 114 |
| `brm_sunk_armchair_floor` | not used | not used | not used | not used | not used | not used | 4 |
| `brm_sunk_bed_wall` | not used | not used | not used | not used | not used | not used | 5 |
| `brm_sunk_chair_wall` | not used | not used | not used | not used | not used | not used | 7 |
| `brm_sunk_table_floor` | not used | not used | not used | not used | not used | not used | 5 |
| `brm_sunk_wardrobe_floor` | not used | not used | not used | not used | not used | not used | 3 |
| `brm_sunk_wardrobe_wall` | not used | not used | not used | not used | not used | not used | 14 |
| `brm_wall_p` | not used | not used | not used | not used | not used | not used | 99 |
| `brm_wall_p_arch` | not used | not used | not used | not used | not used | not used | 77 |
| `brm_wall_p_door` | not used | not used | not used | not used | not used | not used | 30 |
| `brm_wall_p_wide` | not used | not used | not used | not used | not used | not used | not used |
| `brm_wall_s` | not used | not used | not used | not used | not used | not used | 131 |
| `brm_wall_s_door` | not used | not used | not used | not used | not used | not used | 60 |
| `brm_wall_s_wide` | not used | not used | not used | not used | not used | not used | 24 |
| `brm_wall_y` | not used | not used | not used | not used | not used | not used | 238 |
| `brm_wall_y_door` | not used | not used | not used | not used | not used | not used | 211 |
| `brm_wall_y_wide` | not used | not used | not used | not used | not used | not used | 126 |
| `brm_column_s` | not used | not used | not used | not used | not used | not used | 1 |
| `tun_breach` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_cross_shelled` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_room_m_dugout` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_room_m_magazine` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_room_m_mess` | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_office` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_room_m_pump` | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_workshop` | not used | not used | not used | not used | not used | 1 | not used |
| `tun_room_s_corner` | not used | not used | not used | not used | not used | 15 | not used |
| `tun_room_s_cross` | not used | not used | not used | not used | not used | 4 | not used |
| `tun_room_s_end` | not used | not used | not used | not used | not used | 10 | not used |
| `tun_room_s_straight` | not used | not used | not used | not used | not used | 2 | not used |
| `tun_room_s_tee` | not used | not used | not used | not used | not used | 13 | not used |
| `tun_straight_shelled` | not used | not used | not used | not used | not used | 2 | not used |
| `tun_tee_shelled` | not used | not used | not used | not used | not used | 4 | not used |
| `brm_cell_yellow_b` | not used | not used | not used | not used | not used | not used | 121 |
| `brm_cell_yellow_c` | not used | not used | not used | not used | not used | not used | 239 |
| `tun_cable_reel` | not used | not used | not used | not used | not used | 13 | 14 |
| `tun_chamber_shaft` | not used | not used | not used | not used | not used | 2 | 2 |
| `tun_compressor` | not used | not used | not used | not used | not used | 2 | 2 |
| `tun_conveyor` | not used | not used | not used | not used | not used | 1 | 1 |
| `tun_drill_rig` | not used | not used | not used | not used | not used | 1 | 1 |
| `tun_duct` | not used | not used | not used | not used | not used | 5 | 4 |
| `tun_fan` | not used | not used | not used | not used | not used | 1 | 1 |
| `tun_hopper` | not used | not used | not used | not used | not used | 1 | 1 |
| `tun_ladder` | not used | not used | not used | not used | not used | 4 | 4 |
| `tun_ore_pile` | not used | not used | not used | not used | not used | 14 | 15 |
| `tun_powder_crates` | not used | not used | not used | not used | not used | 8 | 7 |
| `tun_pump` | not used | not used | not used | not used | not used | 1 | 1 |
| `tun_ramp_12` | not used | not used | not used | not used | not used | 23 | 15 |
| `tun_ramp_24` | not used | not used | not used | not used | not used | 40 | 17 |
| `tun_straight_humped` | not used | not used | not used | not used | not used | 1 | 3 |
| `tun_straight_meshed` | not used | not used | not used | not used | not used | 2 | 2 |
| `tun_straight_timbered` | not used | not used | not used | not used | not used | 3 | 2 |
| `tun_tool_rack` | not used | not used | not used | not used | not used | 5 | 7 |
| `tun_warning_board` | not used | not used | not used | not used | not used | 10 | 15 |
| `tun_winch` | not used | not used | not used | not used | not used | 2 | 2 |
| `tun_cart_derailed` | not used | not used | not used | not used | not used | 2 | 2 |
| `tun_cart_ore` | not used | not used | not used | not used | not used | 3 | 2 |
| `tun_cart_tipped` | not used | not used | not used | not used | not used | 4 | 4 |
| `tun_track_buffer` | not used | not used | not used | not used | not used | 3 | 2 |
| `tun_track_curve` | not used | not used | not used | not used | not used | 9 | 9 |
| `tun_track_ramp_12` | not used | not used | not used | not used | not used | 4 | 4 |
| `tun_track_ramp_24` | not used | not used | not used | not used | not used | 9 | 8 |
| `tun_track_straight` | not used | not used | not used | not used | not used | 5 | 6 |
| `tun_track_points` | not used | not used | not used | not used | not used | 1 | 1 |

## Notes

- LN2 (docs/lines.md#ln2): fence, wire, wall, sandbag and hedge pieces are drawn as lines (`line_*` rows: lines per map). The map scripts still lay the pieces; `write_map` chains them into lines (`tools/mapgen/mapgen/lines.py`). A piece row reads "as `line_<type>`", plus any single pieces kept as objects (knife rests).

- `gun_pak40` stands in for WN65's 5 cm gun; there is no 5 cm gun asset. It is the R667's own companion.
- The ruined beach house is an intact `house_stone_cottage`, because the catalogue has no ruined-house variant.
- SPEC 3.5 also lists helmets, jerrycans, spent cases, abandoned rifles, field telephones and camouflage netting. None of
  them exist as catalogue assets yet, so the map uses sandbags, crates and ammunition boxes instead.
- The spec's knocked-out vehicle at the Saint-Laurent crossroads is missing for the same reason. The only vehicles are
  the Firefly and Tiger tank GLBs, both excluded above.
