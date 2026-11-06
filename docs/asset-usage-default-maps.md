# Asset usage on the default maps

SPEC 3.5: every catalogue asset is used where it is historically plausible, or listed here with the reason it is not.
Counts are placed instances (companions included), from the `tools/mapgen` build reports (`out/<map>.summary.json`).
`--selftest-maps` fails when a catalogue asset is missing from this table.

- **Omaha:** *Normandy Beaches - Omaha, Easy Red* (W5a, `tools/mapgen/maps/omaha_easy_red.py`).
- **Mont-Castre:** not done. The Mont-Castre map was not built in this update: the user cancelled it and plans to build Normandy maps in the editor. Every cell in that column reads "not built".

| Asset | Category | Omaha | Mont-Castre |
|---|---|---|---|
| `aw_dugout_log` | Atlantic Wall | 1: C: log-roofed dugout behind the draw-head roadblock | not built (map cancelled) |
| `aw_dugout_personnel` | Atlantic Wall | 1: B: WN64 personnel dugout behind the crest | not built (map cancelled) |
| `aw_foxhole` | Atlantic Wall | 15: Three per objective (fallback positions) | not built (map cancelled) |
| `aw_r612_unfinished` | Atlantic Wall | 1: B: WN64, the unfinished R612 | not built (map cancelled) |
| `aw_r667_casemate` | Atlantic Wall | 1: A: WN65 Ruquet casemate, embrasure west along the beach (companion PaK 40, shell crates at the rear door) | not built (map cancelled) |
| `aw_tobruk_mg` | Atlantic Wall | 1: A: WN65 MG ringstand | not built (map cancelled) |
| `aw_tobruk_mortar` | Atlantic Wall | 2: A: WN65; B: WN64 mortar Tobruk | not built (map cancelled) |
| `aw_tobruk_turret` | Atlantic Wall | 1: A: WN65 tank-turret Tobruk (ref photo 6) | not built (map cancelled) |
| `aw_trench_corner` | Atlantic Wall | 2: D: trench line across the hedgerow fields | not built (map cancelled) |
| `aw_trench_entry` | Atlantic Wall | 9: Ends of every trench run (A, B, D) | not built (map cancelled) |
| `aw_trench_straight` | Atlantic Wall | 22: A; B (crest line over the beach); D | not built (map cancelled) |
| `aw_trench_zigzag` | Atlantic Wall | 20: A, B and D trench runs | not built (map cancelled) |
| `bcg_abatis` | Bocage | 45: Plateau: felled trees across the flank lanes; attack-path cover fill above the bluff | not built (map cancelled) |
| `bcg_hedgerow_corner` | Bocage | 2: C: MG positions in hedgerow corners | not built (map cancelled) |
| `bcg_hedgerow_gap` | Bocage | 55: Field gates in the bocage network | not built (map cancelled) |
| `bcg_hedgerow_straight` | Bocage | 1319: Plateau bocage (about 13 km of hedgerow) | not built (map cancelled) |
| `bcg_marsh_patch` | Bocage | 8: Marsh pools on the sand shelf | not built (map cancelled) |
| `bcg_quarry_face` | Bocage | excluded: No quarry at E-1 (a Mont-Castre feature) | not built (map cancelled) |
| `bcg_rampart` | Bocage | excluded: The Roman / medieval earthworks belong to Mont-Castre | not built (map cancelled) |
| `bcg_sunken_lane` | Bocage | 98: Plateau: west and east flank sunken lanes | not built (map cancelled) |
| `bch_at_ditch` | Beach obstacles | 26: Across the E-1 mouth (s 476); second ditch at the draw head (C) | not built (map cancelled) |
| `bch_belgian_gate` | Beach obstacles | 90: Belt 1, 250 m out, with the Easy Red gaps | not built (map cancelled) |
| `bch_lcvp_beached` | Landing craft | 4: A1 landing (spawn cover, rack crates in the well) | not built (map cancelled) |
| `bch_lcvp_intact` | Landing craft | 2: A1: craft still afloat behind the first wave | not built (map cancelled) |
| `bch_lcvp_wrecked` | Landing craft | 5: Wrecks on the tidal flat (cover) | not built (map cancelled) |
| `bch_log_ramp_mined` | Beach obstacles | 72: Belt 3 (ramps) | not built (map cancelled) |
| `bch_log_stake_mine` | Beach obstacles | 253: Belt 2 (two staggered rows of stakes) | not built (map cancelled) |
| `bch_minefield_fence` | Beach obstacles | 123: Minefields behind the shingle, on the bluff, both sides of the Spalding path | not built (map cancelled) |
| `bch_minen_sign` | Beach obstacles | 19: Achtung Minen boards along the minefields | not built (map cancelled) |
| `bch_roadblock` | Beach obstacles | 1: C: across the draw road | not built (map cancelled) |
| `bch_seawall_wood` | Beach | 55: East half of the beach behind the shingle | not built (map cancelled) |
| `bch_shingle_bank` | Beach | 100: The whole shingle bank (s 404-416) | not built (map cancelled) |
| `bunker_command_shelter` | Bunkers | 1: D: battery and CP shelter | not built (map cancelled) |
| `bunker_field_shelter` | Bunkers | 2: C and D | not built (map cancelled) |
| `bunker_large_casemate` | Bunkers | excluded: No large casemate at E-1: WN64 had only the unfinished R612; the H669s stood at other draws | not built (map cancelled) |
| `bunker_mg_casemate` | Bunkers | 1: A: WN65 flank MG casemate | not built (map cancelled) |
| `bunker_observation_post` | Bunkers | 1: B: WN64 observation post | not built (map cancelled) |
| `bunker_pak_casemate` | Bunkers | excluded: WN65's gun casemate is the R667 (`aw_r667_casemate`) | not built (map cancelled) |
| `crate_mixed_supply` | Supply | 14: Ammunition dump at D; dressing at every objective | not built (map cancelled) |
| `crate_pak40_he` | Supply | 7: A: behind the R667; D: ammunition dump | not built (map cancelled) |
| `def_sandbag_curved` | Defences | 33: MG nests, mortar pits (B), dressing, cover fill on the bluff | not built (map cancelled) |
| `def_sandbag_straight` | Defences | 66: MG nests, Saint-Laurent barricades, dressing | not built (map cancelled) |
| `def_tank_trap` | Defences | 233: Belt 4 (hedgehogs); beach cover fill | not built (map cancelled) |
| `env_sea_shallows` | Beach | 87: The sea (s < 50): shallow water surface | not built (map cancelled) |
| `fence_post_wire` | Dressing | 30: Plateau field fences | not built (map cancelled) |
| `field_gate` | Dressing | 6: Bocage field gates | not built (map cancelled) |
| `furn_armoire` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_bed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_bench` | Furniture | 1: D: farmyard CP | not built (map cancelled) |
| `furn_books` | Furniture | 1: D: CP map table | not built (map cancelled) |
| `furn_bottles` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_chair_ladder` | Furniture | 3: D: CP map table | not built (map cancelled) |
| `furn_chair_slat` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_chest` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_clock` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_commode` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_crate` | Furniture | 11: Ammunition dumps and dressing | not built (map cancelled) |
| `furn_crockery` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_curtains` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_fireplace` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_hutch` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_hutch_glazed` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_jug_bowl` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_lamp` | Furniture | 1: D: CP map table | not built (map cancelled) |
| `furn_papers` | Furniture | 1: D: CP map table (maps) | not built (map cancelled) |
| `furn_picture` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_rug_kilim` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_rug_medallion` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_shelf` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_side_table` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `furn_stool` | Furniture | 1: D: farmyard | not built (map cancelled) |
| `furn_table_dining` | Furniture | 1: D: the HQ map table in the farmyard | not built (map cancelled) |
| `furn_table_small` | Furniture | not hand-placed: interior furnishing comes from the room templates inside the farm and Saint-Laurent houses | not built (map cancelled) |
| `gun_flak18` | Ordnance | 1: D: 8.8 cm Flak 18 in the orchard (representative, SPEC 3.3) | not built (map cancelled) |
| `gun_m2hb_tripod` | Field guns | 1: US .50 set up on the shingle near A2 | not built (map cancelled) |
| `gun_mg42_embrasure` | Field guns | 16: Every sandbag MG nest | not built (map cancelled) |
| `gun_pak40` | Field guns | 1: A: in the R667 (stands in for the 5 cm gun) | not built (map cancelled) |
| `hedgerow_bocage` | Vegetation | 228: Plateau: older 8 m hedges mixed into the network | not built (map cancelled) |
| `house_farmhouse_2story` | Houses | 4: Saint-Laurent | not built (map cancelled) |
| `house_stone_cottage` | Houses | 7: Saint-Laurent; the beach house on the sand shelf | not built (map cancelled) |
| `large_municipal_building` | Large buildings | 1: Saint-Laurent (mairie) | not built (map cancelled) |
| `large_stone_warehouse` | Large buildings | 1: Saint-Laurent | not built (map cancelled) |
| `large_village_inn` | Large buildings | 1: Saint-Laurent crossroads | not built (map cancelled) |
| `large_village_schoolhouse` | Large buildings | 1: Saint-Laurent | not built (map cancelled) |
| `path_concrete` | Paths | 1: A: apron behind the R667 | not built (map cancelled) |
| `path_dirt` | Paths | 1: D: farmyard | not built (map cancelled) |
| `path_gravel` | Paths | 1: E: church path | not built (map cancelled) |
| `rack_allied` | Supply | 3: Attacker spawns A3-A5 | not built (map cancelled) |
| `rack_allied_crates` | Supply | 2: A1 (in an LCVP well) and A2 (on the shingle): crate racks, SPEC 3.2 | not built (map cancelled) |
| `rack_axis` | Supply | 5: Defender spawns D1-D5 | not built (map cancelled) |
| `shrub_bramble` | Vegetation | 3974: Bluff scrub (about 1 per 38 m2), draw undergrowth, dunes | not built (map cancelled) |
| `shrub_hazel` | Vegetation | 1070: Bluff scrub, E-1 draw undergrowth | not built (map cancelled) |
| `small_carpenter_workshop` | Small buildings | 2: Saint-Laurent | not built (map cancelled) |
| `small_roadside_office` | Small buildings | 1: Saint-Laurent crossroads | not built (map cancelled) |
| `small_stone_dairy` | Small buildings | 2: D farm; Saint-Laurent | not built (map cancelled) |
| `small_storehouse` | Small buildings | 2: D farm; Saint-Laurent | not built (map cancelled) |
| `stone_wall_low` | Dressing | 90: Saint-Laurent garden walls | not built (map cancelled) |
| `tank_sherman_firefly` | Vehicles | excluded: The Firefly (17-pdr) was British, not at Omaha; no DD Sherman asset exists; player vehicles are out of scope (SPEC 11) | not built (map cancelled) |
| `tank_tiger_i` | Vehicles | excluded: No Tigers at Omaha; vehicles are out of scope (SPEC 11) | not built (map cancelled) |
| `tree_apple` | Vegetation | 396: Orchards (D battery, A5, two more) | not built (map cancelled) |
| `tree_ash` | Vegetation | 213: Hedgerow trees; wooded E-1 draw | not built (map cancelled) |
| `tree_oak` | Vegetation | 316: Hedgerow trees; wooded E-1 draw | not built (map cancelled) |
| `tree_poplar` | Vegetation | 73: Roadside poplars on the draw road; hedgerow trees | not built (map cancelled) |
| `utility_pole` | Dressing | 23: Telegraph line from E-1 to the village, branch to the D command post | not built (map cancelled) |
| `vil_chateau_keep` | Landmarks | excluded: Belongs to Le Plessis-Lastelle (Mont-Castre) | not built (map cancelled) |
| `vil_church` | Landmarks | 1: E: Saint-Laurent church (slots in the tower) | not built (map cancelled) |
| `vil_church_ruined` | Landmarks | excluded: Saint-Laurent's church stood; the ruined church is on the Mont-Castre summit | not built (map cancelled) |
| `vil_norman_farmhouse` | Houses | 2: D: farm HQ; Saint-Laurent | not built (map cancelled) |
| `vil_stone_barn` | Houses | 3: C: barn strongpoint; D farm; Saint-Laurent | not built (map cancelled) |
| `wire_concertina` | Defences | 337: Shelf belt (s 438), WN65 front | not built (map cancelled) |
| `wire_fence` | Defences | 195: Post-and-wire fence behind the concertina | not built (map cancelled) |
| `wire_knife_rest` | Defences | 3: Closing the wire gaps | not built (map cancelled) |

## Notes

- `gun_pak40` stands in for WN65's 5 cm gun; there is no 5 cm gun asset. It is the R667's own companion.
- The ruined beach house is an intact `house_stone_cottage`, because the catalogue has no ruined-house variant.
- SPEC 3.5 also lists helmets, jerrycans, spent cases, abandoned rifles, field telephones and camouflage netting. None of
  them exist as catalogue assets yet, so the map uses sandbags, crates and ammunition boxes instead.
- The spec's knocked-out vehicle at the Saint-Laurent crossroads is missing for the same reason. The only vehicles are
  the Firefly and Tiger tank GLBs, both excluded above.
