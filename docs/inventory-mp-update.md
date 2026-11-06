# Inventory for the Multiplayer & Creative Update (SPEC.md §0)

This is a survey of master 45c4d8f (2026-11-06). For each area it lists what exists and which spec section changes it.

The plan, workstreams and file ownership are in [plan-mp-update.md](plan-mp-update.md). The spec bundle, with its reference images and the historic photos, is kept outside the repo in the shared notes folder, under `mp-update-spec/`.

## What exists today

| Area | What exists | Where | Spec section |
|---|---|---|---|
| Objectives and capture | **Before W0:** scenario v1 with 3–5 ordered objectives, a capture cylinder (radius, vertical bounds), an attack arrow, lane routes, defend points and the contest formula `5*(A-D)/(A+D)*min(A/5,1)`.<br>**After W0:** scenario v2, see below. | `src/scenario.*`, `src/match.*` | §1.1–1.2 (done in W0) |
| Spawns | Markers per role and phase. The game picks a marker for the active phase (clearance ring, nav check). Waves are 10 s / 8 s with a 3 s minimum; attackers get 1 life + 5. | `src/game.cpp` `Game::trySpawn`, `src/match.cpp` | §1.2, §2.1 (W0), §2.2 protection (W1) |
| Creative scenario tools | Scenario panel (Creative › Scenario) and world overlay. W0 adds mouse placement, drag handles, drag reorder, polygons, linked structures, defense slots, multi-phase spawns with auto racks, and validation on save. | `src/mpui.cpp` | §1.3 (W0) |
| Bot AI | Hand-written layered walk grid (0.5 m, 4 layers, door links, A\*) and a utility-flavoured state machine, both described in [ai-inventory.md](ai-inventory.md). | `src/nav.*`, `src/ai.cpp` | AI_SPEC (W6) |
| Weapons | 20 weapon JSONs: Garand, M1C, Carbine, Thompson, M3, BAR, M2HB (carried), Kar98k, K98k ZF39, MP 40, FG 42, MG 42, StG 44, bazooka, Panzerschreck, Mk 2, M24, smoke, knife. The JSONs hold no bullet-damage fields. | `content/weapons/*.json` | §6 (W1) |
| Bullet damage | Caliber base × zone (head 3 / torso 1 / limb 0.55) × linear falloff (60→250 m, down to 0.6), from `content/ai/combat.json`. Range is estimated as life × speed. Health is 100. | `src/game.cpp` `projectileSegment`, `Game::damage` | §6 (W1) |
| Player movement | `movement.json`: gravity 7.848 m/s², jump 3.757 m/s (≈0.90 m apex, 0.96 s airtime), buffer and coyote time. There is no fall damage. | `content/settings/movement.json`, `src/physics.cpp`, `src/explore.cpp` | §4 (W1) |
| ADS / sights | Rear (R) and front (F) anchors measured on the mesh; the sight line is mapped to view −z, with eye relief and an optional `ads` correction block. `selftest_sights` covers 8 calibrated weapons. StG 44 and M3 are not calibrated. | `src/viewmodel.cpp`, `src/selftest_sights.cpp`, history 6dbe90a / 6535d8c / f9c6527 | §8 (W2) |
| Gore | None: a flesh hit shows the grey dust puff, and there is no decal system. W0 adds the `FleshHit` hook (stub). | `src/fx.cpp`, `src/gore_api.h` | §7 (W7) |
| New Map dialog | Blank Slate, Omaha (unavailable) and Forest (unavailable). "Create editable copy" calls `App::newMap`. | `src/editor.cpp:478`, `content/templates/*.json` | §3.1 (W5), §9.1 (W3) |
| Terrain | Fixed 800 m, n = 256, a single mesh, 600 × 600 m build area. Deformation tiles and a 2048² surface mask. Not chunked. | `src/terrain.*` | §9.1 (W3) |
| Editor camera | RMB look, WASD/QE fly, Shift ×4, wheel changes speed. There is no zoom-to-cursor, pan, orbit, F/Home, overview or bookmarks. The M key is the start-marker tool. | `src/editor.cpp:22-65` | §9.2 (W3) |
| Explore entry | F5 starts at the start marker (or the view-ray ground point); Esc opens the pause menu; F5 exits. `doc.start` is saved with the map. | `src/explore.cpp:19-81` | §9.3 (W3) |
| Thumbnails | `renderThumbnail` at 256² with a fixed 3/4 view at 2.1× the half-diagonal, no disk cache. `fmtFoot` prints `%.0f`, which gives "0x0 m". | `src/render.cpp:1989`, `src/app.cpp:454`, `src/editor.cpp:239` | §9.4 (W2) |
| Bunkers | MG casemate, observation post, field shelter, command shelter, large casemate, PaK casemate (R612), MG 42 embrasure mount. The overhaul (8471552, 196ccaa) skipped the OP and field shelter. | `tools/catalog_bunkers.mjs`, `tools/build_bunkers.mjs` | §10 (W2) |
| Resupply | E on a crate triggers `crateTransaction`, which refills guns within 6 m or the personal reserve. Racks swap weapons only. | `src/station.cpp:384`, `src/weapons.cpp:226` | §10.2 [DECISION] (W2) |
| Catalogue | 47 assets. W0 adds the nav field (`navmeta.h`). | `content/assets`, `tools/catalog_*.mjs`, `tools/gen_env_assets.py` | §3.6 (W4), AI_SPEC 3.1 |
| Normandy terrain research | `docs/normandy-terrain.md` defines a terrain-only Omaha (CMH 100-11), with no obstacles or strongpoints in v1, and a fictional forest with three clearings. | `docs/normandy-terrain.md` | §3.3–3.4 (W5) |

### Catalogue by family

| Family | Assets | Also exists | Absent; built in W4 |
|---|---|---|---|
| Bunkers | `bunker_mg_casemate`, `bunker_observation_post`, `bunker_field_shelter`, `bunker_command_shelter`, `bunker_large_casemate`, `bunker_pak_casemate`, `gun_mg42_embrasure` | — | R667, unfinished R612, Tobruks |
| Defences | `def_tank_trap` (Czech hedgehog), `def_sandbag_straight` / `def_sandbag_curved`, `wire_concertina` / `wire_fence` / `wire_knife_rest` | — | Belgian gate, stakes, ramps, minefield fence, AT ditch, roadblock |
| Guns | `gun_flak18`, `gun_pak40`, `gun_m2hb_tripod` | — | — |
| Supply | `crate_mixed_supply`, `crate_pak40_he`, `rack_allied`, `rack_axis` | — | — |
| Houses | farmhouse (two storeys), stone cottage, workshop, dairy, storehouse, office, schoolhouse, inn, warehouse, municipal building | — | Norman stone farmhouse, stone barn, church with steeple, ruined church, château keep |
| Vegetation | oak, ash, apple, poplar, hazel, bramble, `hedgerow_bocage` | — | hedgerow straight / corner / gap, sunken lane, quarry face, earth rampart, abatis, marsh |
| Dressing | `utility_pole` (telegraph pole), `fence_post_wire`, `field_gate`, `stone_wall_low`; paths in gravel, dirt and concrete | — | trenches, dugouts, foxholes, shingle, seawall |
| Vehicles | Sherman Firefly and Tiger I (from the user's GLBs) | — | LCVP: a procedural static prop, by the user's decision |

## What W0 added

### Scenario v2 (`src/scenario.*`)

- **Objectives:** 1–10, lettered A–J by order.
  - `label` is now the display name; the save file writes `label` = letter and `displayName` = name.
  - Shape is a circle (default 25 m) or a polygon.
  - `captureTime` defaults to 45 s with one attacker, up to 3× faster. Any defender on the point pauses capture, and progress never decays.
- **Per-objective data:** `linkedStructures[]` (instance ids) and `defenseSlots[]` (position, yaw, kind, priority).
- **Spawns:** each spawn has `phases[]` and is active while any of those objectives is the current one. A spawn carries `rack`, the id of its auto-placed team weapons rack.
- **Defaults:** Allies attack and Axis defends. The roster cap is 32.
- **Migration from v1:**
  - Capture rule stays the contest formula; the stored defending side and radii are kept.
  - Each `stage` becomes a single phase.
  - `defendPoints` become defense slots.
  - Test fixture: `content/tests/village_assault_5_v1.nmap.json`.
- **Validation (§1.3 / §2.1):**
  - Errors: 0 objectives; more than 10; a phase with no attacker or no defender spawn.
  - Warnings, distances in metres:

    | Check | Warn below |
    |---|---|
    | Objective to the previous objective | 150 |
    | Attacker spawn to its objective | 150 |
    | Defender spawn to its objective | 80 |
    | Defender spawn to an active attacker spawn | 200 |

  - Also warns when an objective is not further along the attack axis than the one before it.
  - Validation runs on save (hook `mpaiBeforeSave`) and from the panel's button.
- **Creative tools:**
  - Place objective, attacker spawn, defender spawn, polygon corners, defense slots, and pick linked structures by clicking.
  - Drag the centre, ring or polygon corners of an objective, or drag a spawn (its rack moves with it).
  - Drag rows to reorder objectives.
  - The editor refuses an 11th objective.
  - Deleting a spawn also deletes its rack.
  - The overlay draws the A→B→C attack path.

### Contracts for the parallel workstreams

| Contract | File | Used by |
|---|---|---|
| Asset nav metadata: `nav.effect` (block / carve / area / none), `nav.area`, sub-areas, room graph, entrances, corners, firing points, cover, `terrainCut[]` | `src/navmeta.h`, parsed into `AssetDef::navMeta` | W4 writes it, W2 writes it for bunkers, W3 applies the cuts, W6 reads it |
| `Soldier::protectUntil` and `isProtected(now)` | `src/game.h` | W1 sets them; W6 lowers target priority; W7 shows a deflect puff |
| `FleshHit` and `goreFleshHit` / `goreStep` / `goreReset` (stub in `gore_stub.cpp`) | `src/gore_api.h` | Reported in `Game::projectileSegment`; W1 enriches the hit; W7 implements it |
| `content/settings/rules.json` (protection 3 s, ends on fire or throw, regroup 30 s, 32 bots) | config | W1, W6 |
| `content/ai/damage.json` (schema; the table is empty) | config | W1 |
| `content/settings/gore.json` (budgets) | config | W7 |
| `content/ai/difficulty.json` (AI_SPEC §10 presets) | config | W6 |
| `tools/catalog_ammo.mjs` (`smallArmsCache`, `gunAmmoStack`, single items) | generator | W2 (bunkers), W4 (new casemates and Tobruks) |
| `--baseline-ai` and `docs/baseline/ai_baseline.json` | test mode | W6 compares against it |

## Not in this repo

- **Weapons named in SPEC §6.2 with no model supplied:** M1903A4 Springfield, Gewehr 43, M1919A6, M1911A1 and Walther P38. By the user's decision they are data-only rows (W1).
- **LCVP:** no model was supplied. The user chose a procedural static prop (W4).
- **Historic photos:** 11 Wikimedia Commons files, downloaded to the notes folder for reference. They never ship.
