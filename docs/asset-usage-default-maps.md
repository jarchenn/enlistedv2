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
- **Newtown:** *Newtown* (BR4, `tools/mapgen/maps/newtown.py`): placed counts; "not used" = not on this map.
- **Outpost Desert:** *Outpost Desert* (NT3, `tools/mapgen/maps/outpost_desert.py`): placed counts; "not used" = not on this map.
- **Outpost Woods:** *Outpost Woods* (NT3, `tools/mapgen/maps/outpost_woods.py`): placed counts; "not used" = not on this map.

| Asset | Category | Omaha | Mont-Castre | Chateau town | D-day | Tunnels | Backrooms | Newtown | Outpost Desert | Outpost Woods |
|---|---|---|---|---|---|---|---|---|---|---|
| `aw_dugout_log` | Atlantic Wall | 1: C: log-roofed dugout behind the draw-head roadblock | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_dugout_personnel` | Atlantic Wall | 1: B: WN64 personnel dugout behind the crest | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_foxhole` | Atlantic Wall | 15: Three per objective (fallback positions) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_r612_unfinished` | Atlantic Wall | 1: B: WN64, the unfinished R612 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_r667_casemate` | Atlantic Wall | 1: A: WN65 Ruquet casemate, embrasure west along the beach (companion PaK 40, shell crates at the rear door) | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `aw_tobruk_mg` | Atlantic Wall | 1: A: WN65 MG ringstand | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used |
| `aw_tobruk_mortar` | Atlantic Wall | 2: A: WN65; B: WN64 mortar Tobruk | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_tobruk_turret` | Atlantic Wall | 1: A: WN65 tank-turret Tobruk (ref photo 6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_bend` | Atlantic Wall | not used: added with D-day (45-degree trench bend) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_corner` | Atlantic Wall | 2: D: trench line across the hedgerow fields | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_cross` | Atlantic Wall | not used: added with D-day (trench crossroads) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_entry` | Atlantic Wall | 9: Ends of every trench run (A, B, D) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_straight` | Atlantic Wall | 22: A; B (crest line over the beach); D | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_tee` | Atlantic Wall | not used: added with D-day (trench T-junction) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `aw_trench_zigzag` | Atlantic Wall | 20: A, B and D trench runs | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bcg_abatis` | Bocage | 45: Plateau: felled trees across the flank lanes; attack-path cover fill above the bluff | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_corner` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_gap` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used | not used | not used | not used |
| `bcg_hedgerow_straight` | Bocage | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used | not used | not used | not used |
| `bcg_marsh_patch` | Bocage | 8: Marsh pools on the sand shelf | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bcg_quarry_face` | Bocage | excluded: No quarry at E-1 (a Mont-Castre feature) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bcg_rampart` | Bocage | excluded: The Roman / medieval earthworks belong to Mont-Castre | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bcg_sunken_lane` | Bocage | 98: Plateau: west and east flank sunken lanes | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used |
| `bch_at_ditch` | Beach obstacles | 26: Across the E-1 mouth (s 476); second ditch at the draw head (C) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bch_belgian_gate` | Beach obstacles | 90: Belt 1, 250 m out, with the Easy Red gaps | terrain-only base map | not used | 12 | not used | not used | not used | not used | not used |
| `bch_lcvp_beached` | Landing craft | 4: A1 landing (spawn cover, rack crates in the well) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used |
| `bch_lcvp_intact` | Landing craft | 2: A1: craft still afloat behind the first wave | terrain-only base map | not used | 3 | not used | not used | not used | not used | not used |
| `bch_lcvp_wrecked` | Landing craft | 5: Wrecks on the tidal flat (cover) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used |
| `bch_log_ramp_mined` | Beach obstacles | 72: Belt 3 (ramps) | terrain-only base map | not used | 13 | not used | not used | not used | not used | not used |
| `bch_log_stake_mine` | Beach obstacles | 253: Belt 2 (two staggered rows of stakes) | terrain-only base map | not used | 40 | not used | not used | not used | not used | not used |
| `bch_minefield_fence` | Beach obstacles | as `line_minefield_fence` | terrain-only base map | as `line_minefield_fence` | not used | not used | not used | not used | not used | not used |
| `bch_minen_sign` | Beach obstacles | 19: Achtung Minen boards along the minefields | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bch_roadblock` | Beach obstacles | 1: C: across the draw road | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bch_seawall_wood` | Beach | 55: East half of the beach behind the shingle | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bch_shingle_bank` | Beach | 100: The whole shingle bank (s 404-416) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bunker_command_shelter` | Bunkers | 1: D: battery and CP shelter | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `bunker_field_shelter` | Bunkers | 2: C and D | terrain-only base map | not used | not used | not used | not used | not used | not used | 1 |
| `bunker_large_casemate` | Bunkers | excluded: No large casemate at E-1: WN64 had only the unfinished R612; the H669s stood at other draws | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `bunker_mg_casemate` | Bunkers | 1: A: WN65 flank MG casemate | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used |
| `bunker_observation_post` | Bunkers | 1: B: WN64 observation post | terrain-only base map | not used | not used | not used | not used | not used | 1 | not used |
| `bunker_pak_casemate` | Bunkers | excluded: WN65's gun casemate is the R667 (`aw_r667_casemate`) | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `crate_mixed_supply` | Supply | 14: Ammunition dump at D; dressing at every objective | terrain-only base map | 10 | 15 | 9 | 8 | 2 | 2 | 2 |
| `crate_pak40_he` | Supply | 7: A: behind the R667; D: ammunition dump | terrain-only base map | not used | 9 | not used | not used | not used | not used | not used |
| `def_sandbag_curved` | Defences | as `line_sandbag_low` | terrain-only base map | as `line_sandbag_low` | not used | not used | not used | not used | not used | not used |
| `def_sandbag_straight` | Defences | as `line_sandbag_low` | terrain-only base map | as `line_sandbag_low` | not used | not used | not used | not used | not used | not used |
| `def_tank_trap` | Defences | 233: Belt 4 (hedgehogs); beach cover fill | terrain-only base map | not used | 58 | not used | not used | not used | not used | not used |
| `drs_barrels` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 90 | 11 | not used | not used | not used | not used | not used |
| `drs_bridge_stone_large` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used |
| `drs_bridge_stone_small` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used |
| `drs_cart` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 62 | 5 | not used | not used | not used | not used | not used |
| `drs_churchyard_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 2 | 1 | not used | not used | not used | not used | not used |
| `drs_flower_bed` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 233 | 22 | not used | not used | not used | not used | not used |
| `drs_garden_wall` | Dressing | as `line_garden_wall` | terrain-only base map | as `line_garden_wall` | not used | not used | not used | not used | not used | not used |
| `drs_gate_piers` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 17 | not used | not used | not used | not used | not used | not used |
| `drs_gravestone_cross` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 34 | 8 | not used | not used | not used | not used | not used |
| `drs_gravestone_round` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 64 | 15 | not used | not used | not used | not used | not used |
| `drs_hay_bale_stack` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 77 | 13 | not used | not used | 1 | not used | not used |
| `drs_picket_fence` | Dressing | as `line_picket_fence` | terrain-only base map | as `line_picket_fence` | not used | not used | not used | not used | not used | not used |
| `drs_sandbag_ring` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used |
| `drs_sandbag_wall` | Defences | as `line_sandbag_wall` | terrain-only base map | as `line_sandbag_wall` | not used | not used | not used | not used | not used | not used |
| `drs_tomb_chest` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 22 | 7 | not used | not used | not used | not used | not used |
| `drs_well` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 19 | 3 | not used | not used | not used | not used | not used |
| `drs_wooden_x_obstacle` | Defences | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 32 | 30 | not used | not used | not used | not used | not used |
| `drs_woodpile` | Dressing | excluded: B2 dressing (tools/catalog_dressing.mjs), the reworked buildings' companions in the editor; laid out on the chateau-town map (M1), not on Omaha | terrain-only base map | 86 | 11 | not used | not used | not used | not used | not used |
| `env_sea_shallows` | Beach | 87: The sea (s < 50): shallow water surface | terrain-only base map | not used | 20 | not used | not used | not used | not used | not used |
| `fence_post_wire` | Dressing | as `line_post_wire_fence` | terrain-only base map | as `line_post_wire_fence` | not used | not used | not used | not used | not used | not used |
| `field_gate` | Dressing | 6: Bocage field gates | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used |
| `fire_brazier` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `fire_campfire` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_armchair` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_armoire` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 6 | not used | not used | not used |
| `furn_barrel` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 18 | 16 | 12 | not used | not used | not used | not used |
| `furn_barrel_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_basket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_bed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | 4 | not used | not used | not used |
| `furn_bed_iron` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_bench` | Furniture | 1: D: farmyard CP | terrain-only base map | 12 | 2 | not used | not used | not used | not used | not used |
| `furn_bookcase` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_books` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_bottles` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_bucket` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 20 | 8 | not used | not used | not used | not used | not used |
| `furn_candelabra` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): church altars | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_candle` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bedrooms, small rooms | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_candlestick` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_candlesticks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_chair_ladder` | Furniture | 3: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_chair_slat` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_chest` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_clock` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_hooks` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_coat_stand` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_commode` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_crate` | Furniture | 11: Ammunition dumps and dressing | terrain-only base map | 21 | 13 | 13 | not used | not used | not used | not used |
| `furn_crockery` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_curtains` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_curtains_half` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_debris_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_debris_plaster` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_dresser` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_fire_hearth` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): a fire on every hearth | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_fireplace` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_dusk` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_landscape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_portrait` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_seascape` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_frame_small` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_gramophone` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_hay_bales` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 37 | not used | not used | not used | 1 | not used | not used |
| `furn_hutch` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_hutch_glazed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_jars` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_jug_bowl` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp` | Furniture | 1: D: CP map table | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_caged` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): bunkers, casemates and low ceilings | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_ceiling` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): kitchens, sitting rooms, halls, offices, classrooms, taverns | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_pendant_glass` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_table` | Furniture | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): sitting rooms | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_lamp_wall_oil` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_ammo_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_bunk` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | not used | not used |
| `furn_mil_crates` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used | not used | not used | not used |
| `furn_mil_fusebox` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_gasmask_hooks` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_generator` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 1 | not used | not used | not used | not used |
| `furn_mil_jerrycans` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | 1 | 1 |
| `furn_mil_locker` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 2 | not used | not used | not used | not used |
| `furn_mil_map` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_phone` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_radio` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_rifle_rack` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_mil_table` | Furniture | not hand-placed: the military room templates furnish mil_stronghold (GX6) | terrain-only base map | not used | not used | 3 | not used | not used | not used | not used |
| `furn_mirror` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_pan_rail` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_pantry` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_papers` | Furniture | 1: D: CP map table (maps) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_pew` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_picture` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_plate_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_portrait` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_pot` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_radio` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_kilim` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_large` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_medallion` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_rug_runner` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_sacks` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 33 | 9 | not used | not used | not used | not used | not used |
| `furn_school_desk` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_sconce` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_sconce_candle` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_screen_folding` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_settee` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_shelf` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_side_table` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_sideboard` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_stool` | Furniture | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_stove` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_stove_potbelly` | Furniture | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_suitcases` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_table_dining` | Furniture | 1: D: the HQ map table in the farmyard | terrain-only base map | not used | not used | not used | 4 | not used | not used | not used |
| `furn_table_low` | Furniture | not hand-placed: GX2 room dressing and interior lighting (content/settings/room_dressing.json, interior_lights.json) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_table_small` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_tool_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_utensil_rack` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_wall_clock` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_wall_shelf` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_washstand` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `furn_wood_pile` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | 24 | not used | not used | not used | not used | not used | not used |
| `furn_workbench` | Furniture | not hand-placed: room templates furnish every building on open / new map / placement (V4) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `fx_impact_smoke` | Lighting | not used: added with D-day (smouldering impact, smoke column) | terrain-only base map | not used | 6 | not used | not used | not used | not used | not used |
| `fx_wreck_fire` | Lighting | not used: added with D-day (burning wreckage, smoke column) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used |
| `gun_flak18` | Ordnance | 1: D: 8.8 cm Flak 18 in the orchard (representative, SPEC 3.3) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used |
| `gun_m2hb_tripod` | Field guns | 1: US .50 set up on the shingle near A2 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `gun_mg42_embrasure` | Field guns | 16: Every sandbag MG nest | terrain-only base map | not used | 10 | not used | not used | not used | not used | not used |
| `gun_pak40` | Field guns | 1: A: in the R667 (stands in for the 5 cm gun) | terrain-only base map | not used | 2 | not used | not used | not used | not used | not used |
| `hedgerow_bocage` | Vegetation | as `line_hedgerow` | terrain-only base map | as `line_hedgerow` | not used | not used | not used | not used | not used | not used |
| `house_farmhouse_2story` | Houses | 4: Saint-Laurent | terrain-only base map | 9 | not used | not used | not used | not used | not used | not used |
| `house_stone_cottage` | Houses | 7: Saint-Laurent; the beach house on the sand shelf | terrain-only base map | 11 | 1 | not used | not used | not used | not used | not used |
| `kit_demo_folly` | Landmarks | excluded: the B1 geometry-kit demonstration piece (docs/asset-format.md#b1-geometry-kit), not a period building | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `lamp_gas_street` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md); the Beautify street_lamps role lines roads with it | terrain-only base map | 10 | 4 | not used | not used | not used | not used | not used |
| `lamp_hanging_lantern` | Lighting | not hand-placed: generated with the room templates (L3 interior lighting, docs/lights.md): barns, workshops, stores, church naves | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `lamp_lantern_post` | Lighting | not hand-placed: Paths > Line auto paths with lamp posts (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `lamp_wall_lantern` | Lighting | not hand-placed: an editor placement for maps that want it (L3, docs/lights.md) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `large_municipal_building` | Large buildings | 1: Saint-Laurent (mairie) | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used |
| `large_stone_warehouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used |
| `large_village_inn` | Large buildings | 1: Saint-Laurent crossroads | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used |
| `large_village_schoolhouse` | Large buildings | 1: Saint-Laurent | terrain-only base map | 3 | not used | not used | not used | not used | not used | not used |
| `line_trench` | Lines | not used: LN1 trench line (old maps' trench pieces migrate to it on load) | terrain-only base map | not used | 10 | not used | not used | not used | not used | not used |
| `line_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `line_concertina` | Lines | 5 | terrain-only base map | not used | 8 | not used | not used | not used | not used | not used |
| `line_knife_rest` | Lines | not used | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `line_minefield_fence` | Lines | 11 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `line_post_wire_fence` | Lines | 4 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `line_picket_fence` | Lines | 16 | terrain-only base map | 177 | 30 | not used | not used | not used | not used | not used |
| `line_stone_wall` | Lines | 6 | terrain-only base map | 126 | 25 | not used | not used | 5 | not used | not used |
| `line_garden_wall` | Lines | 7 | terrain-only base map | 23 | 10 | not used | not used | not used | not used | not used |
| `line_sandbag_wall` | Lines | not used | terrain-only base map | 12 | 9 | not used | not used | not used | not used | not used |
| `line_sandbag_low` | Lines | 99 | terrain-only base map | 8 | 21 | not used | not used | not used | 6 | 8 |
| `line_hedgerow` | Lines | 111 | terrain-only base map | 108 | not used | not used | not used | not used | not used | not used |
| `mil_stronghold` | Bunkers | excluded: GX6 Atlantic Wall command stronghold (60 x 34 m with earthworks); larger than any Omaha strongpoint, for hand-built maps | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `nat_blackthorn_low` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 20 | not used | not used | not used | not used | not used |
| `nat_boulder_granite_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 7 | not used | 1 |
| `nat_boulder_granite_s` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 32 |
| `nat_boulder_limestone_l` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used | not used | 26 | not used |
| `nat_boulder_limestone_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 5 | not used | not used | not used | 38 | not used |
| `nat_boulder_schist_m` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 8 |
| `nat_boulder_schist_slab` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 3 |
| `nat_boulders_granite_group` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 3 | 40 | 4 |
| `nat_bracken_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 42 | not used | not used | not used | not used | 737 |
| `nat_bramble_thicket` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 1 | not used | not used | not used | 2 | not used | 224 |
| `nat_brush_patchy` | Vegetation | not placed: V9 ground cover (patchy brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `nat_brush_rank` | Vegetation | not placed: V9 ground cover (rank brush, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | 157 |
| `nat_brush_scrub` | Vegetation | not placed: V9 ground cover (dry scrub, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 59 | not used | not used | not used | 110 | not used |
| `nat_clover_tufts` | Vegetation | not placed: V9 ground cover (clover and green tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 9 | not used | not used | not used | not used | not used | not used |
| `nat_drygrass_sparse` | Vegetation | not placed: V9 ground cover (sparse dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 113 | not used | not used | not used | 167 | not used |
| `nat_drygrass_tall` | Vegetation | not placed: V9 ground cover (tall dry grass, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 3 | 419 | not used | not used | not used | not used | not used |
| `nat_drygrass_tufts` | Vegetation | not placed: V9 ground cover (dry grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 12 | 177 | not used | not used | 49 | 204 | not used |
| `nat_fern_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 59 | 18 | not used | not used | not used | not used | 756 |
| `nat_flowers_buttercup` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 21 | 1 | not used | not used | not used | not used | not used |
| `nat_flowers_clover` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | not used |
| `nat_flowers_cornflower` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 56 | 7 | not used | not used | not used | not used | not used |
| `nat_flowers_cowparsley` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 63 | 37 | not used | not used | not used | not used | not used |
| `nat_flowers_daisy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 103 | 47 | not used | not used | not used | not used | not used |
| `nat_flowers_foxglove` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 41 | 3 | not used | not used | not used | not used | not used |
| `nat_flowers_meadow` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 18 | not used | not used | not used | 9 | not used | not used |
| `nat_flowers_poppy` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 110 | 39 | not used | not used | not used | not used | not used |
| `nat_gorse_shrub` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 91 | not used | not used | not used | 119 | not used |
| `nat_green_tufts` | Vegetation | not placed: V9 ground cover (green grass tufts, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 23 | 42 | not used | not used | not used | not used | 8 |
| `nat_hawthorn_bush` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 155 | 70 | not used | not used | 54 | not used | not used |
| `nat_meadow_mixed` | Vegetation | not placed: V9 ground cover (mixed meadow, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 44 | 62 | not used | not used | 25 | not used | 3 |
| `nat_nettle_clump` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 7 | 21 | not used | not used | 99 | not used | 242 |
| `nat_outcrop_limestone` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 2 | not used | not used | 5 | 31 | not used |
| `nat_outcrop_schist` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 6 |
| `nat_sapling_ash` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 3 | not used | 256 |
| `nat_sapling_oak` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | not used | not used | 205 |
| `nat_scree_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 3 | not used | not used | not used | 10 | not used |
| `nat_stones_granite_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | not used | not used | not used | 112 | not used | not used |
| `nat_stones_patch` | Nature | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | not used | 4 | not used | not used | 250 | 2 | not used |
| `nat_tallgrass_patch` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 37 | 111 | not used | not used | 89 | not used | 22 |
| `nat_tallgrass_verge` | Vegetation | not placed: added after the Omaha map (natural ground dressing, docs/nature.md) | terrain-only base map; for hand-dressing the cliffs, ridge and fields | 9 | 75 | not used | not used | not used | not used | not used |
| `nat_wheat_field` | Vegetation | not placed: V9 ground cover (wheat field square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 52 | not used | not used | not used | not used | not used | not used |
| `nat_wheat_stubble` | Vegetation | not placed: V9 ground cover (wheat stubble square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 58 | not used | not used | not used | not used | not used | not used |
| `nat_wheat_trampled` | Vegetation | not placed: V9 ground cover (trampled wheat square, docs/nature.md#ground-cover) | for hand-dressing and Beautify (content/settings/beautify.json) | 8 | not used | not used | not used | not used | not used | not used |
| `path_concrete` | Paths | 1: A: apron behind the R667 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `path_dirt` | Paths | 1: D: farmyard | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `path_gravel` | Paths | 1: E: church path | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `rack_allied` | Supply | 3: Attacker spawns A3-A5 | terrain-only base map | 5 | 4 | not used | not used | 1 | 1 | 1 |
| `rack_allied_crates` | Supply | 2: A1 (in an LCVP well) and A2 (on the shingle): crate racks, SPEC 3.2 | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `rack_axis` | Supply | 5: Defender spawns D1-D5 | terrain-only base map | 5 | 4 | not used | not used | 1 | 1 | 1 |
| `rub_beam_tile_heap` | Rubble | not placed: V3 rubble (Beam and tile heap, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used | not used | not used | not used |
| `rub_brick_pile_l` | Rubble | not placed: V3 rubble (Brick rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_brick_pile_m` | Rubble | not placed: V3 rubble (Brick rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_brick_pile_s` | Rubble | not placed: V3 rubble (Brick rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_collapsed_wall` | Rubble | not placed: V3 rubble (Collapsed wall section, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_debris_patch` | Rubble | not placed: V3 rubble (Scattered debris patch, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 15 | not used | not used | not used | 2 | not used |
| `rub_spill_strip` | Rubble | not placed: V3 rubble (Rubble spill strip (8 m), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_stone_pile_l` | Rubble | not placed: V3 rubble (Stone rubble pile (large), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `rub_stone_pile_m` | Rubble | not placed: V3 rubble (Stone rubble pile (medium), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 1 | 2 | not used | not used | not used | not used |
| `rub_stone_pile_s` | Rubble | not placed: V3 rubble (Stone rubble pile (small), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | 3 | not used | not used | not used | not used | not used |
| `ruin_barn_burnt` | Ruins | not placed: V3 ruin (Burnt-out barn, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `ruin_cottage_corner` | Ruins | not placed: V3 ruin (Ruined cottage corner, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `ruin_farmhouse_gutted` | Ruins | not placed: V3 ruin (Gutted farmhouse shell, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `ruin_house_shelled` | Ruins | not placed: V3 ruin (Shelled house (two storeys, roof collapsed), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `ruin_shop_row` | Ruins | not placed: V3 ruin (Half-collapsed shop terrace, docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `ruin_townhouse_facade` | Ruins | not placed: V3 ruin (Bombed townhouse (facade only), docs/assets-mpu.md) | for hand-dressing and Beautify (content/settings/beautify.json) | not used | not used | not used | not used | not used | not used | not used |
| `scn_treeline_a` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 25 | not used | not used | not used | not used | not used |
| `scn_treeline_b` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 22 | not used | not used | not used | not used | not used |
| `scn_treeline_c` | Scenery | not used: added with D-day (far forest silhouette) | terrain-only base map | not used | 26 | not used | not used | not used | not used | not used |
| `shrub_bramble` | Vegetation | 3974: Bluff scrub (about 1 per 38 m2), draw undergrowth, dunes | terrain-only base map | 40 | 33 | not used | not used | 32 | not used | not used |
| `shrub_hazel` | Vegetation | 1070: Bluff scrub, E-1 draw undergrowth | terrain-only base map | 346 | 211 | not used | not used | 91 | not used | 188 |
| `small_carpenter_workshop` | Small buildings | 2: Saint-Laurent | terrain-only base map | 8 | not used | not used | not used | not used | not used | not used |
| `small_roadside_office` | Small buildings | 1: Saint-Laurent crossroads | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used |
| `small_stone_dairy` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 8 | 1 | not used | not used | not used | not used | not used |
| `small_storehouse` | Small buildings | 2: D farm; Saint-Laurent | terrain-only base map | 14 | 1 | not used | not used | not used | not used | not used |
| `stone_wall_low` | Dressing | as `line_stone_wall` | terrain-only base map | as `line_stone_wall` | not used | not used | not used | not used | not used | not used |
| `tank_sherman_firefly` | Vehicles | excluded: The Firefly (17-pdr) was British, not at Omaha; no DD Sherman asset exists; player vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `tank_tiger_i` | Vehicles | excluded: No Tigers at Omaha; vehicles are out of scope (SPEC 11) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `tree_apple` | Vegetation | 396: Orchards (D battery, A5, two more) | terrain-only base map | 305 | 58 | not used | not used | 18 | not used | not used |
| `tree_ash` | Vegetation | 213: Hedgerow trees; wooded E-1 draw | terrain-only base map | 368 | 283 | not used | not used | 357 | not used | not used |
| `tree_oak` | Vegetation | 316: Hedgerow trees; wooded E-1 draw | terrain-only base map | 294 | 310 | not used | not used | 348 | not used | 79 |
| `tree_poplar` | Vegetation | 73: Roadside poplars on the draw road; hedgerow trees | terrain-only base map | 130 | 149 | not used | not used | 141 | not used | not used |
| `utility_pole` | Dressing | 23: Telegraph line from E-1 to the village, branch to the D command post | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_bakery` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_brick_townhouse` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_cafe_corner` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_chateau_grand` | Landmarks | excluded: B3 grand chateau, the centre of the Chateau-town map (M1); no chateau stood at Saint-Laurent | terrain-only base map | 1 | not used | not used | not used | not used | not used | not used |
| `vil_chateau_keep` | Landmarks | excluded: Belongs to Le Plessis-Lastelle (Mont-Castre) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_church` | Landmarks | 1: E: Saint-Laurent church (slots in the tower) | terrain-only base map | 1 | 1 | not used | not used | not used | not used | not used |
| `vil_church_ruined` | Landmarks | excluded: Saint-Laurent's church stood; the ruined church is on the Mont-Castre summit | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_colombage_auge` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_corner_shop` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 14 | 1 | not used | not used | not used | not used | not used |
| `vil_cottage_barn` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 3 | 1 | not used | not used | not used | not used | not used |
| `vil_farm_courtyard` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_gatehouse_chapel` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used |
| `vil_granite_longere` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_longhouse_thatched` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | not used | not used | not used | not used | not used | not used |
| `vil_manor_turret` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 7 | not used | not used | not used | not used | not used | not used |
| `vil_merchant_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_mill_house` | Houses | excluded: B3 house for the Chateau-town map (M1); no mill stream on the E-1 plateau | terrain-only base map | 4 | not used | not used | not used | not used | not used | not used |
| `vil_norman_farmhouse` | Houses | 2: D: farm HQ; Saint-Laurent | terrain-only base map | 6 | 1 | not used | not used | not used | not used | not used |
| `vil_notary_house` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_pension_hotel` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_rectory` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 8 | 1 | not used | not used | not used | not used | not used |
| `vil_relais_inn` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `vil_rendered_shutters` | Houses | excluded: GX6 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | not used | 1 | not used | not used | not used | not used | not used |
| `vil_stone_barn` | Houses | 3: C: barn strongpoint; D farm; Saint-Laurent | terrain-only base map | 5 | 1 | not used | not used | not used | not used | not used |
| `vil_terrace_pair` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used | not used | not used | not used |
| `vil_townhouse_jettied` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 15 | 1 | not used | not used | not used | not used | not used |
| `vil_villa_deauville` | Houses | excluded: B3 house for the Chateau-town map (M1) and the Beautify ring | terrain-only base map | 9 | 1 | not used | not used | not used | not used | not used |
| `wat_crater_pool` | Water | not placed: V1 water feature added after the map was built (Beautify / the user place it) | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `wat_pond_irregular` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `wat_pond_round` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `wat_puddles` | Water | not placed: V1 water feature added after the map was built | terrain-only base map | not used | not used | not used | not used | 1 | not used | not used |
| `wat_stream_bend` | Water | not placed: V1 stream piece (snaps with V6) added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `wat_stream_end` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | not used | not used | not used | not used | not used | not used | not used |
| `wat_stream_straight` | Water | not placed: V1 stream piece added after the map was built | terrain-only base map | 63 | not used | not used | not used | not used | not used | not used |
| `wire_concertina` | Defences | as `line_concertina` | terrain-only base map | as `line_concertina` | not used | not used | not used | not used | not used | not used |
| `wire_fence` | Defences | as `line_wire_fence` | terrain-only base map | as `line_wire_fence` | not used | not used | not used | not used | not used | not used |
| `wire_knife_rest` | Defences | as `line_knife_rest` (3 single) | terrain-only base map | as `line_knife_rest` | 3 | not used | not used | not used | not used | not used |
| `tun_chamber_bunks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_chamber_collapse` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_chamber_generator` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_chamber_store` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_chamber_sump` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_corner` | not used | not used | not used | not used | not used | 12 | not used | not used | not used | not used |
| `tun_corner_lined` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used |
| `tun_crawl` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_cross` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used |
| `tun_end` | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used |
| `tun_lamp` | not used | not used | not used | not used | not used | 33 | not used | not used | not used | not used |
| `tun_lamp_dead` | not used | not used | not used | not used | not used | 6 | not used | not used | not used | not used |
| `tun_lamp_flicker` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_lamp_red` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used |
| `tun_lamp_shadow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_stairs` | not used | not used | not used | not used | not used | 5 | not used | not used | not used | not used |
| `tun_straight` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used |
| `tun_straight_kink` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_straight_lined` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_straight_niche` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_tee` | not used | not used | not used | not used | not used | 9 | not used | not used | not used | not used |
| `tun_wide` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used |
| `brm_bulkhead` | not used | not used | not used | not used | not used | not used | 88 | not used | not used | not used |
| `brm_cell_pool` | not used | not used | not used | not used | not used | not used | 69 | not used | not used | not used |
| `brm_cell_pool_basin` | not used | not used | not used | not used | not used | not used | 15 | not used | not used | not used |
| `brm_cell_pool_flooded` | not used | not used | not used | not used | not used | not used | 41 | not used | not used | not used |
| `brm_cell_service` | not used | not used | not used | not used | not used | not used | 76 | not used | not used | not used |
| `brm_cell_service_cross` | not used | not used | not used | not used | not used | not used | 46 | not used | not used | not used |
| `brm_cell_yellow` | not used | not used | not used | not used | not used | not used | 122 | not used | not used | not used |
| `brm_cell_yellow_damp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `brm_chair_lone` | not used | not used | not used | not used | not used | not used | 54 | not used | not used | not used |
| `brm_column_p` | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used |
| `brm_column_y` | not used | not used | not used | not used | not used | not used | 22 | not used | not used | not used |
| `brm_panel` | not used | not used | not used | not used | not used | not used | 1408 | not used | not used | not used |
| `brm_panel_dead` | not used | not used | not used | not used | not used | not used | 51 | not used | not used | not used |
| `brm_panel_lit` | not used | not used | not used | not used | not used | not used | 469 | not used | not used | not used |
| `brm_partition` | not used | not used | not used | not used | not used | not used | 130 | not used | not used | not used |
| `brm_partition_l` | not used | not used | not used | not used | not used | not used | 28 | not used | not used | not used |
| `brm_partition_low` | not used | not used | not used | not used | not used | not used | 17 | not used | not used | not used |
| `brm_partition_short` | not used | not used | not used | not used | not used | not used | 27 | not used | not used | not used |
| `brm_pile_chairs` | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used |
| `brm_pile_wardrobes` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used |
| `brm_pool_light` | not used | not used | not used | not used | not used | not used | 114 | not used | not used | not used |
| `brm_sunk_armchair_floor` | not used | not used | not used | not used | not used | not used | 4 | not used | not used | not used |
| `brm_sunk_bed_wall` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used |
| `brm_sunk_chair_wall` | not used | not used | not used | not used | not used | not used | 7 | not used | not used | not used |
| `brm_sunk_table_floor` | not used | not used | not used | not used | not used | not used | 5 | not used | not used | not used |
| `brm_sunk_wardrobe_floor` | not used | not used | not used | not used | not used | not used | 3 | not used | not used | not used |
| `brm_sunk_wardrobe_wall` | not used | not used | not used | not used | not used | not used | 14 | not used | not used | not used |
| `brm_wall_p` | not used | not used | not used | not used | not used | not used | 99 | not used | not used | not used |
| `brm_wall_p_arch` | not used | not used | not used | not used | not used | not used | 77 | not used | not used | not used |
| `brm_wall_p_door` | not used | not used | not used | not used | not used | not used | 30 | not used | not used | not used |
| `brm_wall_p_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `brm_wall_s` | not used | not used | not used | not used | not used | not used | 131 | not used | not used | not used |
| `brm_wall_s_door` | not used | not used | not used | not used | not used | not used | 60 | not used | not used | not used |
| `brm_wall_s_wide` | not used | not used | not used | not used | not used | not used | 24 | not used | not used | not used |
| `brm_wall_y` | not used | not used | not used | not used | not used | not used | 238 | not used | not used | not used |
| `brm_wall_y_door` | not used | not used | not used | not used | not used | not used | 211 | not used | not used | not used |
| `brm_wall_y_wide` | not used | not used | not used | not used | not used | not used | 126 | not used | not used | not used |
| `brm_column_s` | not used | not used | not used | not used | not used | not used | 1 | not used | not used | not used |
| `tun_breach` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_cross_shelled` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_room_m_dugout` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_room_m_magazine` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_room_m_mess` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_office` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_room_m_pump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_room_m_workshop` | not used | not used | not used | not used | not used | 1 | not used | not used | not used | not used |
| `tun_room_s_corner` | not used | not used | not used | not used | not used | 15 | not used | not used | not used | not used |
| `tun_room_s_cross` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used |
| `tun_room_s_end` | not used | not used | not used | not used | not used | 10 | not used | not used | not used | not used |
| `tun_room_s_straight` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used |
| `tun_room_s_tee` | not used | not used | not used | not used | not used | 13 | not used | not used | not used | not used |
| `tun_straight_shelled` | not used | not used | not used | not used | not used | 2 | not used | not used | not used | not used |
| `tun_tee_shelled` | not used | not used | not used | not used | not used | 4 | not used | not used | not used | not used |
| `brm_cell_yellow_b` | not used | not used | not used | not used | not used | not used | 121 | not used | not used | not used |
| `brm_cell_yellow_c` | not used | not used | not used | not used | not used | not used | 239 | not used | not used | not used |
| `tun_cable_reel` | not used | not used | not used | not used | not used | 13 | 14 | not used | not used | not used |
| `tun_chamber_shaft` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tun_compressor` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tun_conveyor` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `tun_drill_rig` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `tun_duct` | not used | not used | not used | not used | not used | 5 | 4 | not used | not used | not used |
| `tun_fan` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `tun_hopper` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `tun_ladder` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used |
| `tun_ore_pile` | not used | not used | not used | not used | not used | 14 | 15 | not used | not used | not used |
| `tun_powder_crates` | not used | not used | not used | not used | not used | 8 | 7 | not used | not used | not used |
| `tun_pump` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `tun_ramp_12` | not used | not used | not used | not used | not used | 23 | 15 | not used | not used | not used |
| `tun_ramp_24` | not used | not used | not used | not used | not used | 40 | 17 | not used | not used | not used |
| `tun_straight_humped` | not used | not used | not used | not used | not used | 1 | 3 | not used | not used | not used |
| `tun_straight_meshed` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tun_straight_timbered` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used |
| `tun_tool_rack` | not used | not used | not used | not used | not used | 5 | 7 | not used | not used | not used |
| `tun_warning_board` | not used | not used | not used | not used | not used | 10 | 15 | not used | not used | not used |
| `tun_winch` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tun_cart_derailed` | not used | not used | not used | not used | not used | 2 | 2 | not used | not used | not used |
| `tun_cart_ore` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used |
| `tun_cart_tipped` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used |
| `tun_track_buffer` | not used | not used | not used | not used | not used | 3 | 2 | not used | not used | not used |
| `tun_track_curve` | not used | not used | not used | not used | not used | 9 | 9 | not used | not used | not used |
| `tun_track_ramp_12` | not used | not used | not used | not used | not used | 4 | 4 | not used | not used | not used |
| `tun_track_ramp_24` | not used | not used | not used | not used | not used | 9 | 8 | not used | not used | not used |
| `tun_track_straight` | not used | not used | not used | not used | not used | 5 | 6 | not used | not used | not used |
| `tun_track_points` | not used | not used | not used | not used | not used | 1 | 1 | not used | not used | not used |
| `line_mod_chainlink` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `line_mod_fence_white_picket` | not used | not used | not used | not used | not used | not used | not used | 32 | not used | not used |
| `line_mod_hedge_modern` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used |
| `line_mod_hedge_modern_tall` | not used | not used | not used | not used | not used | not used | not used | 16 | not used | not used |
| `line_mod_road_2lane` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_sidewalk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ammo_box_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_apc_wheeled` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_barrel_group` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 |
| `mod_barrel_steel_rust` | not used | not used | not used | not used | not used | not used | not used | 1 | 2 | 2 |
| `mod_bench` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used |
| `mod_bike_rack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_bigbox` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_bungalow` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_diner_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_family_house` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_fastfood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_gas_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_mixed_use` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_office_setback` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_office_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_family` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_glass` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_concrete` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_box_truck` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | 1 |
| `mod_bus_shelter` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used |
| `mod_cable_spool_large` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_camo_net` | not used | not used | not used | not used | not used | not used | not used | 1 | 2 | 2 |
| `mod_car_shell_burned` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | not used |
| `mod_cart_corral` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_cinder_wall` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used |
| `mod_city_bus` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_concrete_pipe` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_concrete_pipe_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 |
| `mod_construction_fence` | not used | not used | not used | not used | not used | not used | not used | 14 | 1 | not used |
| `mod_container_20` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used |
| `mod_container_20_damaged` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 |
| `mod_container_40` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_container_40_open` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used |
| `mod_container_40_stack` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_crate_plastic_stack` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_crate_wood` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 2 |
| `mod_culvert_headwall` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_delivery_van` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_dumpster_frontload` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_dumpster_rolloff` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_excavator` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_fallen_tree` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_fighting_position_oh` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_fire_hydrant` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_flower_bed` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_fountain_tiered` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_garden_bench` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_garden_shed` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_gazebo` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_generator_set` | not used | not used | not used | not used | not used | not used | not used | 2 | 1 | 2 |
| `mod_greenhouse` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_guard_tower` | not used | not used | not used | not used | not used | not used | not used | 2 | 3 | 2 |
| `mod_hard_cases` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_hatchback` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_2x1` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_hesco_3x1` | not used | not used | not used | not used | not used | not used | not used | 4 | 33 | 1 |
| `mod_hvac_unit` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_ibc_tote` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_jerry_cans` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 |
| `mod_jersey_barrier` | not used | not used | not used | not used | not used | not used | not used | 7 | 2 | not used |
| `mod_jersey_barrier_damaged` | not used | not used | not used | not used | not used | not used | not used | 13 | 4 | 1 |
| `mod_lumber_stack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 2 |
| `mod_mailbox` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_mil_generator` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 |
| `mod_mil_utility` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_mil_utility_green` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 |
| `mod_newspaper_box` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_ambulance` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_nt_armory` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_barn_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_barracks` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_boat_motor` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 1 |
| `mod_nt_boat_trailer` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 |
| `mod_nt_boathouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_boxcar` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_nt_cafe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_channel_steps` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_clock_tower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_compound_wall` | not used | not used | not used | not used | not used | not used | not used | 39 | not used | not used |
| `mod_nt_compound_wall_breach` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_compound_wall_culvert` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_construction_frame` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_construction_frame_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_covered_passage` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_crawl_frame` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_excavation_shoring` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_farmhouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_fire_engine` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_nt_flatcar` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_footbridge` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_hotel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_split` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_house_two` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_hq` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_kiosk` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_motel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_motor_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_municipal` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_parking_garage` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_pier` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_pontoon_finger` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_nt_promenade_rail` | not used | not used | not used | not used | not used | not used | not used | 24 | not used | not used |
| `mod_nt_radar_tower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_rail_buffer` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_rail_track` | not used | not used | not used | not used | not used | not used | not used | 12 | not used | not used |
| `mod_nt_road_bridge` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_shop_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_shop_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_site_office` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_storage_tank` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_tower_crane` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_training_frame` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_training_wall` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_warehouse` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_waterfront_house` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_workshop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pallet_stack` | not used | not used | not used | not used | not used | not used | not used | 4 | 1 | 2 |
| `mod_parking_meter` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_pickup` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_picnic_table` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_planter_concrete` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_play_roundabout` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_play_seesaw` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_spring_rider_twin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_portable_toilet` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_raised_bed` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_razor_coil` | not used | not used | not used | not used | not used | not used | not used | 2 | 10 | 2 |
| `mod_rebar_bundle` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_roadblock` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_rock_large` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_rock_medium` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used |
| `mod_sandbag_position` | not used | not used | not used | not used | not used | not used | not used | 3 | 3 | 2 |
| `mod_scaffold_bay` | not used | not used | not used | not used | not used | not used | not used | 10 | not used | not used |
| `mod_sedan` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_red` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_wrecked` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_semi_tractor` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_semi_trailer` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_skip` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 |
| `mod_sliding_gate` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_street_lamp` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_sun_lounger` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_suv` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tool_chest` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_traffic_drum` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_traffic_light` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_trash_bin` | not used | not used | not used | not used | not used | not used | not used | 8 | 1 | 1 |
| `mod_tree_blossom` | not used | not used | not used | not used | not used | not used | not used | 40 | not used | not used |
| `mod_tree_columnar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tree_ornamental` | not used | not used | not used | not used | not used | not used | not used | 75 | not used | not used |
| `mod_tyre_pile` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | 1 |
| `mod_water_barrier_red` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_weapon_rack` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_apartment_block` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_hospital` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_car_dealership` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_pump_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_bus_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_nt_compound_gate` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_fire_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_climbing_frame` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_streetlight` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_jeep_willys` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_jeep_ww2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_canopy` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 |
| `car_truck_cargo` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 |
| `car_truck_flak` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_kitchen` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_truck_medical` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `eng_outpost` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_armchair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bar_stool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bath` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bed_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bedside_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bleachers` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_bookshelf` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_booth_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_booth_single` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cafeteria_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_canvas_square` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_canvas_wide` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cart_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_chalkboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_checkout` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_clothes_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_coffee_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_condiment_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_copier` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_cubicle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_desk_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_table4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dining_table6` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dish_sink` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dishwasher` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_display_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dresser` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_dumbbell_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_end_cap` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_exercise_bike` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_filing_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_floor_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fridge` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fridge_case` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_fryer` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_gondola` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_griddle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_gym_mat` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_high_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hood` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hoop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_hospital_bed` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_ice_machine` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_iv_stand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_kitchen_base` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_kitchen_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_locker_bench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_lockers` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_lockers_school` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_meeting_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_menu_board` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_microwave` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_mirror` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_monitor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_nightstand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_notice_board` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_nurse_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_office_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_patient_monitor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_plant_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_plant_tall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_prep_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_privacy_curtain` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_range_oven` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_reception_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rest_chair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rest_table` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_rug` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_server_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_service_counter` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_shop_wall_shelf` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_shower` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sink_unit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_soda_machine` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_sofa_l` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_stock_rack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_stove` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_student_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_table_lamp` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_teacher_desk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_toilet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_treadmill` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_tv_stand` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_vanity` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_vending` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_waiting_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_walkin_door` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wall_clock` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wardrobe` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_warming_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_water_cooler` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_weight_bench` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_wheelchair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_mod_whiteboard` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_fence_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_road_4lane` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_air_compressor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ammo_can` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_antenna_mast` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `mod_asphalt_crater` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_barrel_plastic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_barrel_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_barriers_weathered` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_basketball_court` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_basketball_half` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bench_backless` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bird_bath` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_leisure` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_school` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_station` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_stone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_townhouse_row` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_retractable` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_retracted` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bollard_steel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_cable_spool_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_car_wreck_sedan` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_cars_burned_pair` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_gate_open` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_panel` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_chainlink_razor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_collapsed_slab` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_container_20_open` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `mod_crate_plastic` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_crosswalk` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_decking_terrace` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ecology_block` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_electrical_cabinet` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fence_white_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fence_white_picket_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_field_radio` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fountain_round` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_fountain_wall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gate_chainlink` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_gate_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_glass_pile` | not used | not used | not used | not used | not used | not used | not used | 30 | not used | not used |
| `mod_hedge_box_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_tall_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hedge_box_tall_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_1x1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_hesco_1x2` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 |
| `mod_hesco_2x1_half` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `mod_hesco_3x2` | not used | not used | not used | not used | not used | not used | not used | not used | 14 | not used |
| `mod_highway_divider` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_jersey_barrier_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_lamp_post` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_manhole_cover` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_palisade_fence` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 53 |
| `mod_pallet` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `mod_parking_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_medium` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_surface` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pergola` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_planter_corten` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_fence` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_fence_gate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_monkey_bars` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_safety_tiles_green` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_safety_tiles_red` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_sandpit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_spring_rider` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_swings_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_play_swings_4` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_retaining_wall` | not used | not used | not used | not used | not used | not used | not used | 12 | not used | not used |
| `mod_road_2lane_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_crossroads` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_end` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_t_junction` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rock_small` | not used | not used | not used | not used | not used | not used | not used | 158 | not used | not used |
| `mod_roundabout` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rubble_large` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_rubble_small` | not used | not used | not used | not used | not used | not used | not used | 24 | 2 | not used |
| `mod_sawhorse_barricade` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sedan_burned` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_sidewalk_bay` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_b` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_c` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_post_stop` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sign_post_street` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_speed_bump` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_stepping_stones` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_streetlight_double` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_swimming_pool` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_tennis_court` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_ball` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_balls` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_topiary_cone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_traffic_cone` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_traffic_cone_small` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_twall` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_utility_pole` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_van_burned` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_van_wrecked` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_wall_chunks` | not used | not used | not used | not used | not used | not used | not used | 35 | not used | not used |
| `mod_water_barrier_white` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `rack_modern` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `tun_cart` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_12ga` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_50` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_556` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_762` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_9mm` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_ammo_rocket` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_backpack` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_backpack_mil` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_bomber` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_helmet_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_pistol` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_rifle` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_smg` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_mag_ext_sniper` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_molotov` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_muzzle_brake` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_parachute` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_recoil_pad` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_10x` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_4x` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_acog` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_sight_reddot` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_suppressor` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_1` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_2` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_vest_3` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `line_mod_road_2lane_ruin` | not used | not used | not used | not used | not used | not used | not used | 20 | 1 | not used |
| `line_mod_sidewalk_ruin` | not used | not used | not used | not used | not used | not used | not used | 9 | not used | not used |
| `mod_bld_apartment_block_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_bigbox_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_bungalow_ruin` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | not used |
| `mod_bld_bus_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_diner_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 7 | not used | not used |
| `mod_bld_family_house_ruin` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_bld_fastfood_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_gas_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used |
| `mod_bld_hospital_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_mixed_use_ruin` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | not used |
| `mod_bld_office_setback_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_office_wide_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_restaurant_family_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_bld_tower_glass_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_armory_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used |
| `mod_nt_barn_steel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 |
| `mod_nt_barracks_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 |
| `mod_nt_boathouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_cafe_ruin` | not used | not used | not used | not used | not used | not used | not used | 14 | not used | not used |
| `mod_nt_car_dealership_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_clock_tower_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_construction_frame_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_construction_frame_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_farmhouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_fire_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_hotel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_house_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 13 | 1 | 1 |
| `mod_nt_house_split_ruin` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_nt_house_two_ruin` | not used | not used | not used | not used | not used | not used | not used | 14 | not used | 1 |
| `mod_nt_hq_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | not used |
| `mod_nt_motel_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_motor_pool_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 1 |
| `mod_nt_municipal_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_parking_garage_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_pump_station_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_nt_radar_tower_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 |
| `mod_nt_shop_row_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_shop_small_ruin` | not used | not used | not used | not used | not used | not used | not used | 23 | 1 | not used |
| `mod_nt_site_office_ruin` | not used | not used | not used | not used | not used | not used | not used | 8 | 1 | not used |
| `mod_nt_warehouse_ruin` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_nt_waterfront_house_ruin` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | not used |
| `mod_nt_workshop_ruin` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 1 |
| `mod_sapling_paving` | not used | not used | not used | not used | not used | not used | not used | 26 | not used | not used |
| `mod_weeds_clump` | not used | not used | not used | not used | not used | not used | not used | 91 | not used | 156 |
| `mod_weeds_crack` | not used | not used | not used | not used | not used | not used | not used | 221 | 41 | not used |
| `line_mod_road_4lane_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_leisure_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_restaurant_large_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_school_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_station_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_tower_stone_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_bld_townhouse_row_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_crosswalk_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_ivy_patch_ground` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_large_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_medium_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_small_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_parking_surface_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_2lane_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_4lane_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_crossroads_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_end_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_road_t_junction_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_b_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_c_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_sidewalk_bay_ruin` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_vines_hanging` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_vines_hanging_long` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_mil_hut_ruin` | not used | not used | not used | not used | not used | not used | not used | 1 | 1 | 1 |
| `mod_mil_hut` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `mod_pcar_compact` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_pcar_compact_wreck` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | 1 |
| `mod_pcar_hatchback` | not used | not used | not used | not used | not used | not used | not used | 3 | 1 | not used |
| `mod_pcar_hatchback_wreck` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | not used |
| `mod_pcar_minivan` | not used | not used | not used | not used | not used | not used | not used | 3 | not used | not used |
| `mod_pcar_minivan_wreck` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | not used |
| `mod_pcar_offroad` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_pcar_offroad_wreck` | not used | not used | not used | not used | not used | not used | not used | 8 | not used | 1 |
| `mod_pcar_pickup` | not used | not used | not used | not used | not used | not used | not used | 6 | not used | not used |
| `mod_pcar_pickup_wreck` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 2 |
| `mod_pcar_sedan` | not used | not used | not used | not used | not used | not used | not used | 4 | 1 | not used |
| `mod_pcar_sedan_wreck` | not used | not used | not used | not used | not used | not used | not used | 2 | 1 | not used |
| `mod_pcar_sport` | not used | not used | not used | not used | not used | not used | not used | 2 | not used | not used |
| `mod_pcar_sport_wreck` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used |
| `mod_pcar_suv` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | not used |
| `mod_pcar_suv_wreck` | not used | not used | not used | not used | not used | not used | not used | 5 | not used | 1 |
| `mod_pcar_wagon` | not used | not used | not used | not used | not used | not used | not used | 4 | not used | 1 |
| `mod_pcar_wagon_wreck` | not used | not used | not used | not used | not used | not used | not used | 1 | not used | 1 |
| `br_bandage` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_death_crate` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `br_medkit` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_boxer_apc` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `car_humvee` | not used | not used | not used | not used | not used | not used | not used | not used | 1 | 1 |
| `furn_art_a_00` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_09` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_13` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_14` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_17` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_18` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_22` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_26` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_27` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_28` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_29` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_30` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_31` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_32` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_34` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_35` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_36` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_37` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_38` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_39` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_40` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_41` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_42` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_43` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_44` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_46` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_47` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_48` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_49` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_51` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_52` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_53` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_54` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_a_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_00` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_01` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_02` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_03` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_04` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_05` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_06` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_07` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_08` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_10` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_11` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_12` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_15` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_16` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_19` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_20` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_21` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_23` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_24` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_25` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_28` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_29` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_30` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_31` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_34` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_36` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_37` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_38` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_40` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_42` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_43` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_44` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_45` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_48` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_49` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_52` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_53` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_54` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_55` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_56` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_57` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `furn_art_b_58` | not used | not used | not used | not used | not used | not used | not used | not used | not used | not used |
| `nt3_tree_dead_ash` | not used | not used | not used | not used | not used | not used | not used | not used | 9 | not used |
| `nt3_tree_dead_oak` | not used | not used | not used | not used | not used | not used | not used | not used | 23 | not used |
| `nt3_tree_dry_ash` | not used | not used | not used | not used | not used | not used | not used | not used | 35 | not used |
| `nt3_tree_forest_ash` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 177 |
| `nt3_tree_forest_oak` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 211 |
| `nt3_tree_forest_poplar` | not used | not used | not used | not used | not used | not used | not used | not used | not used | 107 |

## Notes

- LN2 (docs/lines.md#ln2): fence, wire, wall, sandbag and hedge pieces are drawn as lines (`line_*` rows: lines per map). The map scripts still lay the pieces; `write_map` chains them into lines (`tools/mapgen/mapgen/lines.py`). A piece row reads "as `line_<type>`", plus any single pieces kept as objects (knife rests).

- `gun_pak40` stands in for WN65's 5 cm gun; there is no 5 cm gun asset. It is the R667's own companion.
- The ruined beach house is an intact `house_stone_cottage`, because the catalogue has no ruined-house variant.
- SPEC 3.5 also lists helmets, jerrycans, spent cases, abandoned rifles, field telephones and camouflage netting. None of
  them exist as catalogue assets yet, so the map uses sandbags, crates and ammunition boxes instead.
- The spec's knocked-out vehicle at the Saint-Laurent crossroads is missing for the same reason. The only vehicles are
  the Firefly and Tiger tank GLBs, both excluded above.
