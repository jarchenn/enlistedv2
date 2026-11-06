# MP & Creative update: structure assets (W4, SPEC §3.6)

37 new catalogue assets for the two default maps (Omaha E-1 and Mont-Castre / Hill 122). They are ordinary catalogue assets, so they are in the Creative palette as well.

- **Generators** (the house kit: `tools/kit.mjs`, `parts.mjs`, `furniture2.mjs`, plus `catalog_ammo.mjs`):
  - `tools/catalog_atlantic.mjs`, which also holds the shared helpers
  - `tools/catalog_beach.mjs`
  - `tools/catalog_bocage.mjs`
  - `tools/catalog_village2.mjs`
- **Builder:** `node tools/build_mpu_assets.mjs` writes only these files. `--check` writes nothing and exits 1 if a file would change. Never run `build_assets.mjs` for these.
- **Test:** `--selftest-structures`, in `src/selftest_structures.cpp`, registered in `tools/run_all_tests.sh`.

## Asset list

Below grade means the asset carries a `terrainCut`. Each screenshot is written by the suite to `selftest_out/mpu_<id>.png`.

### Atlantic Wall (palette category *Atlantic Wall*)

| Id | What | Size | Below grade | Nav |
|---|---|---|---|---|
| `aw_r667_casemate` | R667 flanking casemate, WN65 | 8.2 × 7.8 m plus a 3.8 m wing wall; roof 4.3 m | – | block; gun room; 2 embrasure firing points |
| `aw_r612_unfinished` | R612, unfinished (WN64): rebar, formwork, cement mixer | 8.4 × 7.7 m | – | block; gun room; embrasure |
| `aw_tobruk_turret` | Tank-turret Tobruk (Panzerstellung), ref photo 06 | 3.2 × 4 m chamber plus a 4.4 m entrance ramp | 2.0 m | block; chamber |
| `aw_tobruk_mg` | Tobruk ringstand (MG) | same | 2.0 m | block; chamber; MG firing point |
| `aw_tobruk_mortar` | Mortar Tobruk (open pit) | same | 1.4 m | block; pit; indirect firing point |
| `aw_trench_straight` | Fire trench, 8 m, wattle revetment | 8 m | 1.5 m | area: trench; firesteps |
| `aw_trench_zigzag` | Zig-zag trench: fire bay between traverses | 12 m | 1.5 m | area: trench |
| `aw_trench_corner` | Trench corner, 90° | 4 + 4 m | 1.5 m | area: trench |
| `aw_trench_entry` | Trench end with a ramp from grade | 6 m | 1.5 m | area: trench |
| `aw_dugout_personnel` | Personnel dugout (Mannschaftsunterstand), bunks for 6 | 4 × 3 m chamber | 2.3 m | block; chamber |
| `aw_dugout_log` | Log-roofed dugout, half sunk, with a firing slit | 4 × 3.2 m | 1.0 m | block; chamber; embrasure |
| `aw_foxhole` | Two-man foxhole | 1.9 × 0.8 m | 1.2 m | area: trench; 2 firestep points |

Every Atlantic Wall asset holds a small-arms cache (`smallArmsCache`). Trench pieces keep theirs in an ammunition niche in the rear revetment. The R667 also has 5 cm shell crates (`gunAmmoStack`, `cal "50"`) and a shell rack.

### Beach (palette categories *Beach obstacles*, *Beach*, *Landing craft*)

| Id | What | Size | Notes |
|---|---|---|---|
| `bch_belgian_gate` | Element C (Cointet gate) | 3.0 m wide, 2.5 m high, 4 m deep | `defense` tank_trap, 2500 HP, immune to small arms, blocks vehicles; Teller mines on top |
| `bch_log_stake_mine` | Seaward-leaning log stake with a Teller mine | 3.6 m log, tip at 2.75 m | `defense` tank_trap, 600 HP |
| `bch_log_ramp_mined` | Mined log ramp | 5 m, 1.9 m high end | `defense` tank_trap, 900 HP |
| `bch_shingle_bank` | Shingle bank piece | 12 × 12 m, 2.5 m crest | area: sand; high cover along the landward foot |
| `bch_seawall_wood` | Wooden seawall section | 10 m, 1.4 m high | the planking bays are timber damage panels |
| `bch_at_ditch` | Anti-tank ditch segment | 12 m long, 7 m wide, 3 m deep, faces at 75° | cut 3.3 m; **faces not climbable** (the suite probes them); carve; water in the bottom |
| `bch_roadblock` | Log crib plus steel rails in sockets | 9 m | the crib faces are timber panels; infantry can squeeze between the rails |
| `bch_minefield_fence` | Wire fence with an "ACHTUNG MINEN!" board | 10 m | area: **minefield** (forbidden), 10 × 10 m on the +Z side |
| `bch_minen_sign` | The sign alone on a stake | – | nav none (dressing) |
| `bch_lcvp_intact` | LCVP, ramp up | 11.0 × 3.3 m | the ramp is an interactable leaf (E lowers it) |
| `bch_lcvp_beached` | LCVP, ramp down on the sand | 11.0 × 3.3 m (ramp reaches 1.9 m further) | spawn cover; walk-in well |
| `bch_lcvp_wrecked` | LCVP: holed, burnt, listing 7°, sunk 0.5 m, ramp torn off | 11.0 × 3.3 m | enterable over the bow sill |

The Czech hedgehog is the existing `def_tank_trap`. It now has nav metadata (carve, plus a cover point).

### Bocage and Mont-Castre (palette category *Bocage*)

| Id | What | Size | Notes |
|---|---|---|---|
| `bcg_hedgerow_straight` | Earth bank 1.6 m high with hedge and two trees | 10 m | block; high cover on both sides; foliage block (`hedge: true`) |
| `bcg_hedgerow_corner` | Right-angle corner with a corner oak | 6 + 6 m | block |
| `bcg_hedgerow_gap` | Bank with a 3.6 m field gateway | 10 m | block, with a grass area through the gap |
| `bcg_sunken_lane` | Chemin creux: floor 2 m down, hedged banks on both lips | 12 m | cut 2.3 m; area: road |
| `bcg_quarry_face` | 12 m sandstone face, lip MG nest, walkable back slope | 22 × 24 m | block; 2 MG firing points on the lip |
| `bcg_rampart` | Earth rampart, 3 m high, with a crest walk and parapet | 12 m | block; 4 firestep points |
| `bcg_abatis` | Four felled trees, crowns toward +Z | 8 m | trunks solid; the branch tangle is a player-only volume |
| `bcg_marsh_patch` | Standing water, mud and reeds in a shallow dip | 14 × 10 m | cut 0.35 m; area: **marsh**; nothing collides |

### Village (palette categories *Houses*, *Landmarks*)

| Id | What | Size | Interior |
|---|---|---|---|
| `vil_norman_farmhouse` | Norman longère | 15 × 7 m | kitchen with hearth, salle with box bed, cellier; 3 doors |
| `vil_stone_barn` | Stone barn, 5 m to the eaves | 16 × 9 m | cart doors, loopholes, hay loft by stair, gable pitching door |
| `vil_church` | Church with steeple: tower, nave, choir; spire to 25 m | 25.4 × 8 m | four-flight tower stair to the belfry (10.7 m), three open arches (**sniper**), pews, altar |
| `vil_church_ruined` | Roofless ruined church | 17 × 8 m | broken walls 1.2–6 m, south breach, rubble |
| `vil_chateau_keep` | Ruined donjon of Le Plessis-Lastelle | 11 m square, walls 2 m thick, 11–15 m high, plus a curtain wall | stair to a part-collapsed first floor; window embrasures (MG) |

## Conventions (contract notes for W3 / W5 / W6)

- **Front is local +Z.** That is the sea, the enemy, or the fields.
  - Exceptions: the R667 embrasure fires along local **+X**, with the wing wall on the +Z (sea) side. On the map, turn the casemate so +X points west along the beach.
  - Trench, ditch, hedgerow, rampart and shingle pieces join end to end along local X. The sunken lane joins along Z. The trench corner leaves along +Z at (0, +4).
- **Nav yaw.** `firingPoints[].yaw` and `cover[].yaw` are degrees about +Y in asset space.
  - 0 faces local +Z and 90 faces +X, the same rotation as an instance yaw.
  - For a cover point, the yaw is the direction of the threat the cover faces.
- **Firing-point flags.** Some firing points carry extra keys, which `navmeta` ignores:
  - `"unglazed": true` marks an opening with no casement: belfry arches, keep windows, the barn pitching door.
  - `"indirect": true` marks the mortar position.
- **Terrain cuts.**
  - `depth` is measured down from the instance's placement height.
  - `bank` is the sloped band inside the outline.
  - Where pieces or cuts join, the outlines overlap by at least 0.6 m. Cuts are combined as the minimum height, so the bank of one cut never leaves a ridge inside another.
  - Trench, ditch and lane cuts run 0.8 m past their open ends for the same reason.
  - W3 pads every outline outward by one terrain grid step (0.45 m, `content/settings/editor.json` `terrainCut.edgePad` -1) and resolves the cut on that grid, so the ground steps down anywhere up to about 0.9 m outside an outline. Every such step lies under asset geometry (constants `CUT_*` in `tools/catalog_atlantic.mjs`):
    - **Closed walls** (Tobruk and dugout chambers): the outline lies 0.45 m (`CUT_PAD`) inside the outer wall face, so the padded floor reaches the face; a grade-level ring 0.6 m wide (`CUT_RING`), earth mounds or spoil heaps cover the step outside.
    - **Ramps out to grade** (Tobruk, dugout and trench-entry ramps): the outline stops 0.3 m (`CUT_SETBACK`) short of the ramp top, under the ramp, and a 1.0 m apron at grade (`CUT_APRON`) covers the step beyond it. Without it, the step opens a pit in front of the ramp top that is too deep to step out of.
    - **Sides**: trench parapet and parados slabs, ramp spoil heaps, lane banks and the AT ditch's earth (out to 1.25 m beyond the top of the face) cover it. The foxhole outline is the hole itself, with a grade-level rear lip.
  - The floors, revetments and earth fill are asset geometry. The cut only removes the ground.
  - The suite uses W3's cuts when W3's terrain code is in the build (`editor_nav.h`: it calls `navSyncCuts` right after placing). Without it, it writes the same padded cut through the terrain-delta path as a stand-in. Never both, or the ground would be lowered twice.
- **Tours.** A tour is the scripted walkthrough run by `--selftest-structures`.
  - It is stored under the asset's `"tour"` key, not `"walkthrough"`, so the legacy `--selftest` phase A does not pick these assets up.
  - Its step vocabulary is the same.
  - Extra keys:
    - `cut`: apply the terrain cut first.
    - `companions`: place the host's companions.
    - `join`: a piece placed end to end, such as a trench entry.
    - `climb`: probes that must fail to climb a face.
- **Destruction.**
  - Masonry walls use damage panels: wall bays per `parts.wall`, plus auto panels at load.
  - Concrete uses the concrete family (450 HP).
  - Plank and log faces use the timber family: seawall bays, roadblock crib, log dugout walls, LCVP hull sides.
  - Obstacles use the `defense` tank_trap profile: knocked over by HE or AT, barely marked by small arms, rounds stopped.
  - Hedgerows, rampart and sunken lane are indestructible foliage (`hedge: true`), like `hedgerow_bocage`.
  - Earthworks, rock and the wrecked LCVP are static.

## Nav on the existing catalogue (backfill)

`build_mpu_assets.mjs` appends a `nav` block as the last key of every existing non-bunker asset. It leaves every other byte as the original generator wrote it.

- **Rules:**
  - Wire (concertina, knife rest, fence) is `area: wire`, which is forbidden.
  - Hedgerow banks are `block`, with cover on both sides.
  - Trees, the hedgehog, sandbags, low walls, fences, the gate, poles, racks and the PaK / Flak are `carve`. The hedgehog, sandbags, walls and guns get cover points.
  - Shrubs are `area: brush`. Paths are `area: road`.
  - Small dressing is `none`: crates and the M2HB tripod.
  - Tanks are `none`: they are dynamic obstacles for the AI.
- **Houses and outbuildings (10).** Their nav is derived by `houseNav`:
  - one room per floor (footprint inset 0.6 m, 3 m per storey);
  - doors and entrances from the exterior ground-floor door interactables;
  - a window firing point behind every exterior casement;
  - room corners, and high cover at the four outside flanks.

  It is marked `"derived": "auto ..."`. Hand-authored room graphs for these are a follow-up.
- **`gen_env_assets.py`** writes the same blocks for its own assets from `tools/nav_backfill.json`, which `build_mpu_assets.mjs` writes. Re-running the Python generator reproduces the files byte for byte.
- **Furniture** (`furn_*`, from `tools/gen_furniture.py`): pieces whose `prop.blocking` is true are `carve`; cosmetic props (rugs, papers, crockery, curtains) are `none`.
- **After running `build_assets.mjs` or `gen_furniture.py`**, run `build_mpu_assets.mjs` again.
- **Bunkers** (`bunker_*`, `gun_mg42_embrasure`) belong to W2. The suite reports their nav as pending until W2's commit lands.

## Scale checks and sources

Dimensions come from the sources below. Where the game changes one, the reason is given.

| Asset | Period dimension | Source | Game |
|---|---|---|---|
| Shingle bank | about 2.5 m (8 ft) high, up to about 15 m wide | SPEC §3.3; Wikipedia, *Omaha Beach*; US Army, *Omaha Beachhead* (American Forces in Action, 1945) | 2.5 m high, 12 m deep |
| Obstacle belts | gates about 250 m out; stakes 30 m behind, every third mine-capped; ramps 30 m further | SPEC §3.3; Wikipedia, *Omaha Beach*; ref photo 05 | layout is W5's job |
| Belgian gate (Element C / Cointet) | about 3 m wide and 2.5 m high, about 1.3 t, on rollers | Wikipedia, *Czech hedgehog* / *Belgian Gate*; Commons category `Cointet-elements` | 3.0 × 2.5 m; 4 m base |
| Log stake, log ramp | logs about 0.3 m thick; stakes leaning seaward; ramps about 2 m high at the landward end | ref photo 05; *Omaha Beachhead* | 3.6 m stake; 5 m ramp, 1.9 m high |
| Teller mine 35 | about 0.32 m across, 0.09 m high | TM-E 30-451, *Handbook on German Military Forces* (1945), mines | 0.30 × 0.09 m |
| LCVP | 36 ft 3 in × 10 ft 10 in (11.05 × 3.30 m); bow ramp; two .30 cal MGs aft; plywood hull | Wikipedia, *LCVP (United States)*; ref photos 01 and 11 | 11.0 × 3.3 m; ramp 1.95 m; empty pintles (guns only from GLBs) |
| R667 | Regelbau 667: single gun room for a 5 cm KwK, embrasure flanking along the beach behind a wing wall, B-strength walls and roof | Landmark Scout, *Easy Red: Casemate Ruquet, WN65*; ref photo 08; R. Rolf, *Atlantic Wall Typology* | walls 1.6 m; gun room enlarged to 5.0 × 4.6 m to hold the PaK 40 stand-in |
| R612 (unfinished) | WN64's only bunker was an unfinished R612 | Traces of War, *Atlantikwall – Widerstandsnest 64*; SPEC §3.3 | same envelope as the existing `bunker_pak_casemate` (8.4 × 7.7 m) |
| Tobruk / Panzerstellung | Ringstand 58c: a buried chamber whose ring is flush with the ground. Captured French light-tank turrets (37 mm) were mounted on Tobruk rings | Wikipedia, *Tobruk (fortification)*; ref photo 06 (US Signal Corps, Omaha) | 2 m chamber; turret 1.36 m wide |
| Trenches | fire trenches with firesteps, revetted with wattle or planks, zig-zag traces with traverses (depth approximate: chest-to-head deep) | general reference: TM-E 30-451, field fortifications; SPEC §3.6 | floor 1.5 m; firestep 0.33 m (eye clears a 0.4 m parapet) |
| Anti-tank ditch | ditch across the mouth of E-1 | SPEC §3.3; *Omaha Beachhead* | 7 m top, 3 m deep, 75° revetted faces. Chosen to exceed every player reach: vault 1.35 m (`VaultSettings::maxH`), jump apex about 1.1 m |
| Hedgerow bank | earth banks 1–2 m high topped with hedges and trees; compare the Sherman in ref photo 10 (hull about 1.9 m) | SPEC §3.3; ref photos 09 and 10; M. Doubler, *Closing with the Enemy* | bank 1.6 m; about 4 m to the hedge top |
| Sunken lane | lanes sunk 1–3 m between banks | SPEC §3.3 and §3.4 | 2 m sunk, plus a 1 m bank |
| Quarry face | old stone quarry, face 8–15 m | SPEC §3.4; Manche Tourism, *Mont Castre* | 12 m |
| Earth rampart | Roman / medieval camp ramparts reused as a trench line | SPEC §3.4; D-Day Overlord, *Mont Castre – Hill 122* | 3 m high, 11 m base |
| Church | Saint-Laurent-sur-Mer: a Norman village church with a west tower and spire | SPEC §3.3; ref photo 07 (period map) | 14 m nave; tower 6.4 m square; belfry floor 10.7 m; spire 25 m |
| Château keep | ruined keep at Le Plessis-Lastelle | SPEC §3.4 | 11 m square, 2 m walls, 11–15 m standing |

Reference photos (internal only, not shipped) are in the coordinator's notes, under `mp-update-spec/reference/historic/photos/`.

## Limitations

- **5 cm gun.** No 5 cm KwK model has been supplied, and guns come only from the user's GLBs. The R667 therefore places the procedural `gun_pak40` station as its crewable gun, with 5 cm shell crates. The mortar Tobruk has no mortar, and the LCVP gun tubs have empty pintles. The tank-turret Tobruk's turret is static dressing, not a crew station.
- **Terrain cuts.** W3's terrain applies them. In a build without W3, they are applied only by the test's stand-in, and below-grade floors sit under the ground in Creative and Explore. The covering geometry assumes W3's pad of one grid step; a larger `edgePad` would open steps again.
- **LCVPs on water.** The intact LCVP cannot be placed afloat: `App::checkPlacement` refuses footprints in water. Placing craft in the surf needs W3 or W5 to allow it, for example a per-asset `"allowWater"`.
- **Marsh.** It is only a nav area (cost ×3). Explore has no slow-down for the player: the engine slows only for `wire` defences.
- **Nav yaw.** The yaw convention above is W4's choice. `navmeta.h` does not fix one.
- **Backfilled houses.** Their rooms are footprint approximations, one per floor. They are not real room graphs.
- **Two stair routes.** The church tower and the keep each have a single stair. The kit's rule of two routes per multi-floor building is waived for a tower or a keep. The barn loft also has one stair.
