# Asset usage on the default maps

SPEC 3.5: every catalogue asset is used where it is historically plausible, or listed here with the reason it is not.
Counts are placed instances (companions included), from the `tools/mapgen` build reports (`out/<map>.summary.json`).
`--selftest-maps` fails when a catalogue asset is missing from this table.

- **Omaha:** *Normandy Beaches - Omaha, Easy Red* (W5a, `tools/mapgen/maps/omaha_easy_red.py`).
- **Mont-Castre:** *Normandy Forest - Mont-Castre, Hill 122* is a terrain-only base map (no assets placed; the user places them in the editor), built by W5b. Every cell in that column reads "terrain-only base map".
- **Chateau town:** *Chateau town* (M1, `tools/mapgen/maps/chateau_town.py`): placed counts, companions included; "not used" = not on this map.
- **D-day:** *D-day* (D1, `tools/mapgen/maps/dday.py`): placed counts, companions included; "not used" = not on this map.

| Asset | Category | Omaha | Mont-Castre | Chateau town | D-day |
|---|---|---|---|---|---|
| `aw_dugout_log` | Atlantic Wall | 1: C: log-roofed dugout behind the draw-head roadblock | terrain-only base map | not used | not used |
| `aw_dugout_personnel` | Atlantic Wall | 1: B: WN64 personnel dugout behind the crest | terrain-only base map | not used | not used |
| `aw_foxhole` | Atlantic Wall | 15: Three per objective (fallback positions) | terrain-only base map | not used | not used |
| `aw_r612_unfinished` | Atlantic Wall | 1: B: WN64, the unfinished R612 | terrain-only base map | not used | not used |
| `aw_r667_casemate` | Atlantic Wall | 1: A: WN65 Ruquet casemate, embrasure west along the beach (companion PaK 40, shell crates at the rear door) | terrain-only base map | not used | 1 |
| `aw_tobruk_mg` | Atlantic Wall | 1: A: WN65 MG ringstand | terrain-only base map | not used | 2 |
| `aw_tobruk_mortar` | Atlantic Wall | 2: A: WN65; B: WN64 mortar Tobruk | terrain-only base map | not used | not used |
| `aw_tobruk_turret` | Atlantic Wall | 1: A: WN65 tank-turret Tobruk (ref photo 6) | terrain-only base map | not used | not used |
| `aw_trench_corner` | Atlantic Wall | 2: D: trench line across the hedgerow fields | terrain-only base map | not used | 3 |
| `aw_trench_entry` | Atlantic Wall | 9: Ends of every trench run (A, B, D) | terrain-only base map | not used | 11 |
| `aw_trench_straight` | Atlantic Wall | 22: A; B (crest line over the beach); D | terrain-only base map | not used | 19 |
| `aw_trench_zigzag` | Atlantic Wall | 20: A, B and D trench runs | terrain-only base map | not used | 29 |
| `bcg_abatis` | Bocage | 45: Plateau: felled trees across the flank lanes; attack-path cover fill above the bluff | terrain-only base map | not used | not used |
| `bcg_hedgerow_corner` | Bocage | 2: C: MG positions in hedgerow corners | terrain-only base map | not used | not used |
| `bcg_hedgerow_gap` | Bocage | 55: Field gates in the bocage network | terrain-only base map | 34 | not used |
| `bcg_hedgerow_straight` | Bocage | 1319: Plateau bocage (about 13 km of hedgerow) | terrain-only base map | 211 | not used |
| `bcg_marsh_patch` | Bocage | 8: Marsh pools on the sand shelf | terrain-only base map | not used | not used |
| `bcg_quarry_face` | Bocage | excluded: No quarry at E-1 (a Mont-Castre feature) | terrain-only base map | not used | not used |
| `bcg_rampart` | Bocage | excluded: The Roman / medieval earthworks belong to Mont-Castre | terrain-only base map | not used | not used |
| `bcg_sunken_lane` | Bocage | 98: Plateau: west and east flank sunken lanes | terrain-only base map | 3 | not used |
| `bch_at_ditch` | Beach obstacles | 26: Across the E-1 mouth (s 476); second ditch at the draw head (C) | terrain-only base map | not used | not used |
| `bch_belgian_gate` | Beach obstacles | 90: Belt 1, 250 m out, with the Easy Red gaps | terrain-only base map | not used | 10 |
| `bch_lcvp_beached` | Landing craft | 4: A1 landing (spawn cover, rack crates in the well) | terrain-only base map | not used | 3 |
| `bch_lcvp_intact` | Landing craft | 2: A1: craft still afloat behind the first wave | terrain-only base map | not used | 7 |
| `bch_lcvp_wrecked` | Landing craft | 5: Wrecks on the tidal flat (cover) | terrain-only base map | not used | 5 |
| `bch_log_ramp_mined` | Beach obstacles | 72: Belt 3 (ramps) | terrain-only base map | not used | 5 |
| `bch_log_stake_mine` | Beach obstacles | 253: Belt 2 (two staggered rows of stakes) | terrain-only base map | not used | 20 |
| `bch_minefield_fence` | Beach obstacles | 123: Minefields behind the shingle, on the bluff, both sides of the Spalding path | terrain-only base map | not used | not used |
| `bch_minen_sign` | Beach obstacles | 19: Achtung Minen boards along the minefields | terrain-only base map | not used | not used |
| `bch_roadblock` | Beach obstacles | 1: C: across the draw road | terrain-only base map | not used | not used |
| `bch_seawall_wood` | Beach | 55: East half of the beach behind the shingle | terrain-only base map | not used | not used |
| `bch_shingle_bank` | Beach | 100: The whole shingle bank (s 404-416) | terrain-only base map | not used | not used |
| `bunker_command_shelter` | Bunkers | 1: D: battery and CP shelter | terrain-only base map | not used | not used |
| `bunker_field_shelter` | Bunkers | 2: C and D | terrain-only base map | not used | not used |
| `bunker_large_casemate` | Bunkers | excluded: No large casemate at E-1: WN64 had only the unfinished R612; the H669s stood at other draws | terrain-only base map | not used | 1 |
| `bunker_mg_casemate` | Bunkers | 1: A: WN65 flank MG casemate | terrain-only base map | not used | 2 |
| `bunker_observation_post` | Bunkers | 1: B: WN64 observation post | terrain-only base map | not used | not used |
| `bunker_pak_casemate` | Bunkers | excluded: WN65's gun casemate is the R667 (`aw_r667_casemate`) | terrain-only base map | not used | 1 |
| `crate_mixed_supply` | Supply | 14: Ammunition dump at D; dressing at every objective | terrain-only base map | 10 | 14 |
| `crate_pak40_he` | Supply | 7: A: behind the R667; D: ammunition dump | terrain-only base map | not used | 6 |
| `def_sandbag_curved` | Defences | 33: MG nests, mortar pits (B), dressing, cover fill on the bluff | terrain-only base map | 8 | 18 |
| `def_sandbag_straight` | Defences | 66: MG nests, Saint-Laurent barricades, dressing | terrain-only base map | not used | 3 |
| `def_tank_trap` | Defences | 233: Belt 4 (hedgehogs); beach cover fill | terrain-only base map | not used | 40 |
| `drs_barrels` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 90 | 12 |
| `drs_bridge_stone_large` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used |
| `drs_bridge_stone_small` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used |
| `drs_cart` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 62 | 5 |
| `drs_churchyard_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 2 | 1 |
| `drs_flower_bed` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 233 | 16 |
| `drs_garden_wall` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 178 | 10 |
| `drs_gate_piers` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 17 | 2 |
| `drs_gravestone_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 34 | 8 |
| `drs_gravestone_round` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 64 | 15 |
| `drs_hay_bale_stack` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 77 | 11 |
| `drs_picket_fence` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 389 | 52 |
| `drs_sandbag_ring` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 7 | not used |
| `drs_sandbag_wall` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 12 | 11 |
| `drs_tomb_chest` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 22 | 7 |
| `drs_well` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 19 | 3 |
| `drs_wooden_x_obstacle` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 32 | 19 |
| `drs_woodpile` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 86 | 10 |
| `env_sea_shallows` | Beach | 87: The sea (s < 50): shallow water surface | terrain-only base map | not used | 20 |
| `fence_post_wire` | Dressing | 30: Plateau field fences | terrain-only base map | not used | not used |
| `field_gate` | Dressing | 6: Bocage field gates | terrain-only base map | 1 | not used |
| `fire_brazier` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used |
| `fire_campfire` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used |
| `furn_armchair` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_armoire` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_barrel` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 18 | 8 |
| `furn_barrel_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_basket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_bed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_bed_iron` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_bench` | Furniture | 1: D: farmyard CP | terrain-only base map | 12 | 2 |
| `furn_bookcase` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_books` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used |
| `furn_bottles` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_bucket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 20 | 3 |
| `furn_candelabra` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): church altars | terrain-only base map | not used | not used |
| `furn_candle` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bedrooms, small rooms | terrain-only base map | not used | not used |
| `furn_candlestick` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_candlesticks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_chair_ladder` | Furniture | 3: D: CP map table | terrain-only base map | not used | not used |
| `furn_chair_slat` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_chest` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_clock` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_coat_hooks` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_coat_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_coat_stand` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_commode` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_crate` | Furniture | 11: Ammunition dumps and dressing | terrain-only base map | 21 | 13 |
| `furn_crockery` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_curtains` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_curtains_half` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_debris_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_debris_plaster` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_dresser` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_fire_hearth` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): a fire on every hearth | terrain-only base map | not used | not used |
| `furn_fireplace` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_frame_dusk` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_frame_landscape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_frame_portrait` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_frame_seascape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_frame_small` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_gramophone` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_hay_bales` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 37 | not used |
| `furn_hutch` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_hutch_glazed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_jars` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_jug_bowl` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_lamp` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used |
| `furn_lamp_caged` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bunkers, casemates and low ceilings | terrain-only base map | not used | not used |
| `furn_lamp_ceiling` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): kitchens, sitting rooms, halls, offices, classrooms, taverns | terrain-only base map | not used | not used |
| `furn_lamp_pendant_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_lamp_table` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): sitting rooms | terrain-only base map | not used | not used |
| `furn_lamp_wall_oil` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_mil_ammo_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_bunk` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_crates` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_fusebox` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_gasmask_hooks` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_generator` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_jerrycans` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_locker` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_map` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_phone` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_radio` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_rifle_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mil_table` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used |
| `furn_mirror` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_pan_rail` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_pantry` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_papers` | Furniture | 1: D: CP map table (maps) | terrain-only base map | not used | not used |
| `furn_pew` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_picture` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_plate_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_portrait` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_pot` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_radio` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_rug_kilim` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_rug_large` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_rug_medallion` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_rug_runner` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_sacks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 33 | 5 |
| `furn_school_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_sconce` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_sconce_candle` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_screen_folding` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_settee` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_shelf` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_side_table` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_sideboard` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_stool` | Furniture | 1: D: farmyard | terrain-only base map | not used | not used |
| `furn_stove` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_stove_potbelly` | Furniture | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used |
| `furn_suitcases` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_table_dining` | Furniture | 1: D: the HQ map table in the farmyard | terrain-only base map | not used | not used |
| `furn_table_low` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used |
| `furn_table_small` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used |
| `furn_tool_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_utensil_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_wall_clock` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_wall_shelf` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_washstand` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `furn_wood_pile` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 24 | not used |
| `furn_workbench` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used |
| `gun_flak18` | Ordnance | 1: D: 8.8 cm Flak 18 in the orchard (representative, SPEC 3.3) | terrain-only base map | not used | 2 |
| `gun_m2hb_tripod` | Field guns | 1: US .50 set up on the shingle near A2 | terrain-only base map | not used | not used |
| `gun_mg42_embrasure` | Field guns | 16: Every sandbag MG nest | terrain-only base map | not used | 2 |
| `gun_pak40` | Field guns | 1: A: in the R667 (stands in for the 5 cm gun) | terrain-only base map | not used | 2 |
| `hedgerow_bocage` | Vegetation | 228: Plateau: older 8 m hedges mixed into the network | terrain-only base map | 156 | not used |
| `house_farmhouse_2story` | Houses | 4: Saint-Laurent | terrain-only base map | 9 | not used |
| `house_stone_cottage` | Houses | 7: Saint-Laurent; the beach house on the sand shelf | terrain-only base map | 11 | 1 |
| `kit_demo_folly` | Landmarks | excluded: the B1 geometry-kit demonstration piece (docs/asset-format.md#b1-geometry-kit), not a period building | terrain-only base map | not used | not used |
| `lamp_gas_street` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md); the Beautify street_lamps role lines roads with it | terrain-only base map | 10 | 4 |
| `lamp_hanging_lantern` | Lighting | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): barns, workshops, stores, church naves | terrain-only base map | not used | not used |
| `lamp_lantern_post` | Lighting | not hand-placed: Paths > Line auto paths with lamp posts (L3, docs/lights.md) | terrain-only base map | not used | not used |
| `lamp_wall_lantern` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used |
| `large_municipal_building` | Large buildings | 1: Saint-Laurent (mairie) | terrain-only base map | 3 | not used |
| `large_stone_warehouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used |
| `large_village_inn` | Large buildings | 1: Saint-Laurent crossroads | terrain-only base map | 3 | not used |
| `large_village_schoolhouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used |
| `mil_stronghold` | Bunkers | excluded: GX6 Atlantic Wall command stronghold (60 x 34 m with earthworks); larger than any Omaha strongpoint, for hand-built maps | terrain-only base map | not used | 1 |
| `nat_blackthorn_low` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 12 |
| `nat_boulder_granite_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_boulder_granite_s` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_boulder_limestone_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 |
| `nat_boulder_limestone_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 5 |
| `nat_boulder_schist_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_boulder_schist_slab` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_boulders_granite_group` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_bracken_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 35 |
| `nat_bramble_thicket` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 1 | not used |
| `nat_brush_patchy` | Vegetation | not placed: V9 ground cover (patchy brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `nat_brush_rank` | Vegetation | not placed: V9 ground cover (rank brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `nat_brush_scrub` | Vegetation | not placed: V9 ground cover (dry scrub, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 65 |
| `nat_clover_tufts` | Vegetation | not placed: V9 ground cover (clover and green tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 9 | not used |
| `nat_drygrass_sparse` | Vegetation | not placed: V9 ground cover (sparse dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 81 |
| `nat_drygrass_tall` | Vegetation | not placed: V9 ground cover (tall dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 3 | 362 |
| `nat_drygrass_tufts` | Vegetation | not placed: V9 ground cover (dry grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 12 | 156 |
| `nat_fern_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 59 | 20 |
| `nat_flowers_buttercup` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 21 | 1 |
| `nat_flowers_clover` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_flowers_cornflower` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 56 | 7 |
| `nat_flowers_cowparsley` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 63 | 39 |
| `nat_flowers_daisy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 103 | 50 |
| `nat_flowers_foxglove` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 3 |
| `nat_flowers_meadow` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 18 | not used |
| `nat_flowers_poppy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 110 | 43 |
| `nat_gorse_shrub` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 73 |
| `nat_green_tufts` | Vegetation | not placed: V9 ground cover (green grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 23 | 50 |
| `nat_hawthorn_bush` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 155 | 80 |
| `nat_meadow_mixed` | Vegetation | not placed: V9 ground cover (mixed meadow, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 44 | 62 |
| `nat_nettle_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 7 | 25 |
| `nat_outcrop_limestone` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 2 |
| `nat_outcrop_schist` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_sapling_ash` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_sapling_oak` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_scree_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 1 |
| `nat_stones_granite_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used |
| `nat_stones_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 8 |
| `nat_tallgrass_patch` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 37 | 117 |
| `nat_tallgrass_verge` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 9 | 64 |
| `nat_wheat_field` | Vegetation | not placed: V9 ground cover (wheat field square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 52 | not used |
| `nat_wheat_stubble` | Vegetation | not placed: V9 ground cover (wheat stubble square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 58 | not used |
| `nat_wheat_trampled` | Vegetation | not placed: V9 ground cover (trampled wheat square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 8 | not used |
| `path_concrete` | Paths | 1: A: apron behind the R667 | terrain-only base map | not used | not used |
| `path_dirt` | Paths | 1: D: farmyard | terrain-only base map | not used | not used |
| `path_gravel` | Paths | 1: E: church path | terrain-only base map | not used | not used |
| `rack_allied` | Supply | 3: Attacker spawns A3-A5 | terrain-only base map | 5 | 4 |
| `rack_allied_crates` | Supply | 2: A1 (in an LCVP well) and A2 (on the shingle): crate racks, SPEC 3.2 | terrain-only base map | not used | not used |
| `rack_axis` | Supply | 5: Defender spawns D1-D5 | terrain-only base map | 5 | 4 |
| `rub_beam_tile_heap` | Rubble | not placed: V3 rubble (Beam and tile heap, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_brick_pile_l` | Rubble | not placed: V3 rubble (Brick rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_brick_pile_m` | Rubble | not placed: V3 rubble (Brick rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 2 |
| `rub_brick_pile_s` | Rubble | not placed: V3 rubble (Brick rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_collapsed_wall` | Rubble | not placed: V3 rubble (Collapsed wall section, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_debris_patch` | Rubble | not placed: V3 rubble (Scattered debris patch, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 9 |
| `rub_spill_strip` | Rubble | not placed: V3 rubble (Rubble spill strip (8 m), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_stone_pile_l` | Rubble | not placed: V3 rubble (Stone rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `rub_stone_pile_m` | Rubble | not placed: V3 rubble (Stone rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 1 |
| `rub_stone_pile_s` | Rubble | not placed: V3 rubble (Stone rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 1 |
| `ruin_barn_burnt` | Ruins | not placed: V3 ruin (Burnt-out barn, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `ruin_cottage_corner` | Ruins | not placed: V3 ruin (Ruined cottage corner, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `ruin_farmhouse_gutted` | Ruins | not placed: V3 ruin (Gutted farmhouse shell, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `ruin_house_shelled` | Ruins | not placed: V3 ruin (Shelled house (two storeys, roof collapsed), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `ruin_shop_row` | Ruins | not placed: V3 ruin (Half-collapsed shop terrace, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `ruin_townhouse_facade` | Ruins | not placed: V3 ruin (Bombed townhouse (facade only), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used |
| `shrub_bramble` | Vegetation | 3974: Bluff scrub (about 1 per 38 m2), draw undergrowth, dunes | terrain-only base map | 40 | 39 |
| `shrub_hazel` | Vegetation | 1070: Bluff scrub, E-1 draw undergrowth | terrain-only base map | 346 | 192 |
| `small_carpenter_workshop` | Small buildings | 2: Saint-Laurent | terrain-only base map | 8 | not used |
| `small_roadside_office` | Small buildings | 1: Saint-Laurent crossroads | terrain-only base map | 7 | not used |
| `small_stone_dairy` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 8 | 1 |
| `small_storehouse` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 14 | 1 |
| `stone_wall_low` | Dressing | 90: Saint-Laurent garden walls | terrain-only base map | 174 | 39 |
| `tank_sherman_firefly` | Vehicles | excluded: The Firefly (17-pdr) was British, not at Omaha; no DD Sherman asset exists; player vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used |
| `tank_tiger_i` | Vehicles | excluded: No Tigers at Omaha; vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used |
| `tree_apple` | Vegetation | 396: Orchards (D battery, A5, two more) | terrain-only base map | 305 | 43 |
| `tree_ash` | Vegetation | 213: Hedgerow trees; wooded E-1 draw | terrain-only base map | 368 | 289 |
| `tree_oak` | Vegetation | 316: Hedgerow trees; wooded E-1 draw | terrain-only base map | 294 | 266 |
| `tree_poplar` | Vegetation | 73: Roadside poplars on the draw road; hedgerow trees | terrain-only base map | 130 | 113 |
| `utility_pole` | Dressing | 23: Telegraph line from E-1 to the village, branch to the D command post | terrain-only base map | not used | not used |
| `vil_bakery` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_brick_townhouse` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_cafe_corner` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_chateau_grand` | Landmarks | excluded: B3 grand chateau, the centre of the Chateau-town map (M1); no chateau stood at Saint-Laurent | terrain-only base map | 1 | not used |
| `vil_chateau_keep` | Landmarks | excluded: Belongs to Le Plessis-Lastelle (Mont-Castre) | terrain-only base map | not used | not used |
| `vil_church` | Landmarks | 1: E: Saint-Laurent church (slots in the tower) | terrain-only base map | 1 | 1 |
| `vil_church_ruined` | Landmarks | excluded: Saint-Laurent's church stood; the ruined church is on the Mont-Castre summit | terrain-only base map | not used | not used |
| `vil_colombage_auge` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_corner_shop` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 14 | not used |
| `vil_cottage_barn` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 3 | 1 |
| `vil_farm_courtyard` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used |
| `vil_gatehouse_chapel` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used |
| `vil_granite_longere` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_longhouse_thatched` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | not used |
| `vil_manor_turret` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used |
| `vil_merchant_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_mill_house` | Houses | excluded: B3 house for the Chateau-town map (M1); no mill stream on the E-1 plateau | terrain-only base map | 4 | not used |
| `vil_norman_farmhouse` | Houses | 2: D: farm HQ; Saint-Laurent | terrain-only base map | 6 | 1 |
| `vil_notary_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used |
| `vil_pension_hotel` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used |
| `vil_rectory` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | 1 |
| `vil_relais_inn` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used |
| `vil_rendered_shutters` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 |
| `vil_stone_barn` | Houses | 3: C: barn strongpoint; D farm; Saint-Laurent | terrain-only base map | 5 | 1 |
| `vil_terrace_pair` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 |
| `vil_townhouse_jettied` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 |
| `vil_villa_deauville` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 9 | not used |
| `wat_crater_pool` | Water | not placed: V1 water feature added after the map was built (Beautify / the user place it) | terrain-only base map | not used | not used |
| `wat_pond_irregular` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used |
| `wat_pond_round` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used |
| `wat_puddles` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used |
| `wat_stream_bend` | Water | not placed: V1 stream piece (snaps with V6) added after the map was built | terrain-only base map | not used | not used |
| `wat_stream_end` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | not used | not used |
| `wat_stream_straight` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | 63 | not used |
| `wire_concertina` | Defences | 337: Shelf belt (s 438), WN65 front | terrain-only base map | not used | 98 |
| `wire_fence` | Defences | 195: Post-and-wire fence behind the concertina | terrain-only base map | not used | not used |
| `wire_knife_rest` | Defences | 3: Closing the wire gaps | terrain-only base map | not used | 3 |

## Notes

- `gun_pak40` stands in for WN65's 5 cm gun; there is no 5 cm gun asset. It is the R667's own companion.
- The ruined beach house is an intact `house_stone_cottage`, because the catalogue has no ruined-house variant.
- SPEC 3.5 also lists helmets, jerrycans, spent cases, abandoned rifles, field telephones and camouflage netting. None of
  them exist as catalogue assets yet, so the map uses sandbags, crates and ammunition boxes instead.
- The spec's knocked-out vehicle at the Saint-Laurent crossroads is missing for the same reason. The only vehicles are
  the Firefly and Tiger tank GLBs, both excluded above.
