# Plan: Multiplayer & Creative Update, run across parallel sessions

## Context
The user sent `mp-update-spec-bundle (1).zip`. It contains:
- `SPEC.md`:
  - 1–10 ordered objectives and spawn protection
  - two 1200×2400 m default maps (Omaha E-1 and Mont-Castre)
  - about 30 new structures, jump and gravity changes, and a per-weapon damage table
  - gore, StG 44 and M3 sight fixes, thumbnail and bunker fixes
  - editor map size, navigation and drop-in
- `AI_SPEC.md`: a Recast/Detour navmesh rewrite of the AI with cover, utility and fireteams, 32 bots, and 3 difficulty presets.
- Reference images and scripts.

It targets Normandy Mapmaker on master 45c4d8f, which already includes my `multiplayer-ai` work. The user wants several sessions running in parallel, set up so that no session gets in another's way.

User decisions:
- Parallel sessions.
- LCVP is a **procedural static prop** (non-drivable).
- The 5 missing weapons (Springfield, G43, M1919A6, M1911A1, P38) are **data only for now**.
- **Download** the historic photos.

## What exists (survey of master 45c4d8f)

| Area | Today | Gap |
|---|---|---|
| Objectives | `scenario.h` has 3–5 objectives, the `5*(A-D)/(A+D)` capture rate, and defenders default to Allies | 1–10 objectives, `captureTime` (45 s, up to 3× faster), polygon shapes, `linkedStructures`, `defenseSlots`, multi-phase spawns, auto-placed racks, Allies attack |
| Spawning | `Game::trySpawn` (`game.cpp:182`) | No spawn protection |
| Damage | Caliber base × zone × linear falloff in `combat.json` (`game.cpp:287-316`); zones are head 3 / torso / limb 0.55; hp 100 | Per-weapon table with smoothstep falloff, upper/lower torso split, helmet, real range |
| Movement | `movement.json`: g 7.848, jump 3.757; no fall damage | g ×2.133, new v0, fall damage by height |
| Sights | R/F anchor method (`viewmodel.cpp:30-55`, `selftest_sights.cpp`, 8 weapons calibrated); stg44 and m3 are uncalibrated | StG 44 roll and offset, M3 aperture, debug overlay |
| Thumbnails | `renderThumbnail` (`render.cpp:1989`) uses a fixed 3/4 view, no cache; `fmtFoot` uses `%.0f` | Bounds framing, view with the most projected area, alpha, cache, blank check, decimals |
| Bunkers | Overhaul 8471552 and 196ccaa skipped the field shelter and observation post | Same treatment plus ammo boxes; resupply already exists (`crateTransaction`) |
| Terrain/editor | Fixed 800 m, n=256, one mesh, 600×600 build area; fly camera only; M is the start-marker tool | Rectangular 400–4000 m chunked terrain, camera controls, overview, bookmarks, drop-in |
| Catalogue | 47 assets, no nav field, no trench/Tobruk/gate/LCVP/church | About 30 new assets, nav flags and annotations |
| AI | Hand-written layered grid (`nav.cpp`) and `ai.cpp` | Recast/Detour tiles, links, live rebuild, perception, cover, layers, difficulty |
| Gore | None (flesh hits show a grey puff); no decal system | Everything in §7 |

## Isolation rules (every session)
- Each session works in its own git worktree and branch. All branches are cut from the integration branch **`mp-update`**, which this session owns and which starts from master 45c4d8f.
- Each session has its own build dir: `%TEMP%/nmk/build-<stream>` with a private `_deps` and `FETCHCONTENT_SOURCE_DIR_*` pointing at `%TEMP%/nmk/fc`.
- Each stream **owns** the files listed in its row below. Logic goes into **new files**. Shared core files (`app.cpp`, `editor.cpp`, `explore.cpp`, `render.cpp`, `game.cpp`, `physics.cpp`) get only one-line hooks.
- Cross-stream contracts are fixed in W0 before anything else starts, so no stream waits on another's code.
- Common rules:
  - Never kill the user's exe.
  - Don't push.
  - Commit only own files.
  - Never re-run the whole `tools/build_assets.mjs`. Use the per-family builders (`build_bunkers.mjs` or the new ones).
- Each session's own suite and the full suite must be green before it reports. I then merge it into `mp-update`, re-run the full suite, and the next stream rebases onto it.
- `mp-update` reaches master once, at the end, through the main session (`local_a807b82d`). That keeps master stable for the interiors session (`interiors-v1`), which is still working. I send the main session the file-ownership table at the start.
- The spec bundle is extracted to `side-session-notes/<parent>/mp-update-spec/`, outside the repo. Photos go to its `reference/historic/photos/`.

## W0: Foundation (this session, first; ~1 day)
1. Extract the bundle and run `fetch_reference_photos.ps1` there.
2. Write `docs/inventory-mp-update.md` (from the survey above, mapping each spec section to code) and `docs/ai-inventory.md`.
3. **Record the AI baseline** on `village_assault_5`: stuck events per bot per minute, speed while advancing, time exposed, deaths per minute. Save it to `docs/baseline/ai_baseline.json`.
4. **Objective model v2** (`scenario.h/.cpp`, `match.cpp`, `mpui.cpp`, all mine):
   - 1–10 objectives labelled A–J (an 11th is refused; 0 cannot be saved or played).
   - `displayName`, `captureShape` (circle 25 m or polygon), `captureTime` 45 s with a rate multiplier of `min(attackers, 3)`.
   - `linkedStructures[]` (instance ids), `defenseSlots[]` (position, facing, kind, priority).
   - Spawns with `phases[]`; each spawn auto-places a `rack_allied` / `rack_axis` instance (movable, deletable).
   - Allies attack by default.
   - Validator errors and warnings exactly as in §1.3 / §2.1, run on save and from a button.
   - Path line drawn in the editor; objectives reorderable by drag.
   - Migration v1→v2, plus a test that loads `village_assault_5` unchanged.
5. **Contracts** (additive structs, stubs and config skeletons only):
   - `assets.h`:
     - `nav` (`block|carve|area|none`) and `navArea` (road/grass/brush/marsh/sand/trench/minefield/wire)
     - `annotations` (entrances, rooms, doors, corners, windows/embrasures, cover points)
     - `terrainCut` (a polygon with a depth profile, for trenches, ditches and foxholes)
   - `game.h`: `Soldier::protectUntil` and `bool isProtected()`. `struct FleshHit {pos, normal, dir, weapon, bone, zone, seed, exits}` plus the hook `goreFleshHit(App&, const FleshHit&)`, with a no-op stub in `gore_stub.cpp`.
   - Config skeletons:
     - `content/settings/rules.json` (spawnProtectionSeconds 3.0, protectionEndsOnFire true, regroupSeconds 30, maxBots 32)
     - `content/ai/damage.json` (empty table)
     - `content/settings/gore.json`
     - `content/ai/difficulty.json`
   - `tools/catalog_ammo.mjs`: part builders for the 7.92 MG box, the .30 cal can, rifle crates, bandoliers, and PaK/88 shell crates with wicker carriers.
6. Commit to `mp-update`, then start Wave 1.

## Wave 1: six parallel sessions (each starts from W0's commit)

| Stream | Scope (spec §) | Owns | Core one-liners |
|---|---|---|---|
| **W1 Gameplay rules** (~2 d) | §2.2 spawn protection (timer, shimmer, shield icon, HUD ring, deflect puff, ends on fire); §4 movement (`movement.json` g=16.74, v0 derived for apex ×1.2, fall damage by height converted to speeds); §6 damage table in `damage.json`, upper/lower torso zones, helmet spark and sound, true travelled range, data-only rows for the 5 missing weapons, the repo's own RPMs; `selftest_rules` (protection 3.0 s ±1 frame, airtime and apex within 2 %, STK table at 10/50/100/200 m) | `src/rules.cpp`, `src/damage_model.cpp`, `selftest_rules.cpp`, `rules.json`, `damage.json`, `movement.json` | `game.cpp` (damage call, protect at spawn), `explore.cpp` (fall-damage hook) |
| **W2 Visual fixes** (~2 d) | §8: StG 44 (zero roll, R/F, `rotCorrection`) and M3 (aperture centred) with the existing R/F method, added to the calibrated set in `selftest_sights` (8 → 10); dev overlay of projected sight points; 1/2 px check at 1080p, 1440p and ultrawide on every weapon. §9.4: thumbnail framing on combined bounds, view with the most projected area for thin assets, alpha-tested wire, disk cache keyed by asset hash, blank-tile (>95 % background) test, one-decimal footprint. §10: OP and field shelter get the 8471552 treatment (materials, camouflage netting, weathering, interior); ammo boxes from `catalog_ammo` in every bunker (gun ammo in the PaK, Flak and new casemates); nav flags and annotations on the bunkers | `stg44.json`, `m3_grease_gun.json`, `selftest_sights.cpp`, `viewmodel.cpp` overlay, `src/thumbs.cpp` (new), `catalog_bunkers.mjs` and the bunker JSONs | `render.cpp` (`renderThumbnail` → `thumbs.cpp`), `app.cpp` (`makeThumbnails`), `editor.cpp` (`fmtFoot`) |
| **W3 Editor & terrain** (~4 d) | §9.1: rectangular 400–4000 m maps; `doc.width/depth`; chunked terrain (64 m chunks, LOD, streamed around the camera, Jolt heightfield per chunk); build-area outline; New Map width/depth fields with presets and a memory/chunk estimate; Blank Slate text. Applying `terrainCut`. §9.2: zoom to cursor, MMB pan, RMB orbit, height-scaled WASD/QE, F, Home, overview map on `M` (the start tool moves to drop-in), bookmarks Ctrl+1–9 / 1–9. §9.3: drop-in soldier with drag facing, Esc returns, saved `exploreStart`, "Explore from start" | `terrain.h/.cpp`, `src/terrain_chunks.cpp`, `src/editor_nav.cpp`, `src/dropin.cpp`, templates `size` fields | `editor.cpp` (camera call, dialog fields), `explore.cpp` (enter at point), `render.cpp` (terrain draw), `physics.cpp` (heightfield), `grass.cpp` (bounds) |
| **W4 Structures** (~4 d) | §3.6 as normal catalogue assets, each with nav flags, annotations and `terrainCut` where relevant:<br>• **Atlantic Wall:** R667 with 5 cm gun, unfinished R612 (rebar, formwork, mixer), tank-turret / MG / mortar Tobruks, trenches (zig-zag, wattle), dugouts (personnel, log-roofed), foxhole<br>• **Beach:** Belgian gate, log stake with Teller mine, mined ramp, shingle bank, seawall, AT ditch, roadblock, minefield fence with *Achtung Minen* sign, **procedural LCVP** (intact / beached / wrecked)<br>• **Bocage:** hedgerow straight / corner / gap, sunken lane, quarry face, earth rampart, abatis, marsh patch<br>• **Village:** Norman farmhouse, stone barn, church with steeple, ruined church, château keep<br>• nav flags backfilled on the existing non-bunker assets | `tools/catalog_{atlantic,beach,bocage,village2}.mjs`, `tools/build_mpu_assets.mjs`, the new `content/assets/*.json`, nav fields in `gen_env_assets.py` output | none (a registration line in the builder) |
| **W6 AI core** (~5 d) | AI_SPEC §3–§10 on existing maps plus `content/examples/ai_obstacle_course.nmap.json` (primitives first, W4 assets swapped in after its merge):<br>• **Navigation:** Recast/Detour via FetchContent (zlib licence), 32 m tiles, area costs, auto off-mesh links (drop, mantle, vault, gap), background tile rebuild with re-plan within 1 s, per-bot threat, crowding and lane costs, funnel smoothing, avoidance with 6 m separation, stuck ladder with logging<br>• **Combat:** perception and memory, team knowledge, suppression, cover generation and scoring, sprint/jog/walk, bounding overwatch, aim model<br>• **Structure:** commander / fireteam / individual utility layers, 25–35 % flankers, defense-slot filling, 30 s regroup, Recruit / Regular / Veteran presets, grenade arcs and safety, room and bunker clearing from annotations<br>• **Tooling:** debug overlays, 32-bot budget (≤ 3 ms, time-sliced) | `nav.*` (replaced), `src/ai_*.cpp`, `cmake/recast.cmake`, `content/ai/*.json`, `selftest_ai.cpp`, `mpui.cpp` difficulty line | `CMakeLists.txt` include line |
| **W7 Gore** (~3 d) | §7: decal system (world plus bone-attached on the skinned mesh), entry and exit wounds, back-spatter and forward spray, surface splatter ray, droplets with quadratic drag and stretched decals, water ripple, spurting, drip trail, pools, ageing, budgets (256 / 24 / 1500, mist beyond 60 m), Off / Reduced / Full setting, LAN replication of `FleshHit` with seeded local simulation, `selftest_gore` | `src/gore.cpp` (replaces the stub), `src/decals.*`, `shaders/decal*`, `gore.json` | `render.cpp` (decal pass), `character.cpp` (bone decals), `net.cpp` (event) |

**Merge order into `mp-update`:** W1 → W2 → W7 → W4 → W3 → W6. Each stream rebases onto the latest `mp-update` when it finishes.

**Known overlaps and how they are handled:**
- `editor.cpp`: W2 owns `fmtFoot`; W3 owns the camera and dialog.
- `render.cpp`: W2 owns the thumbnail function; W3 owns terrain drawing; W7 adds one hook line.
- Spawn-protected target priority belongs to W6 (it reads `isProtected()`).

## Wave 2: maps (after W3 and W4 are merged)
- **W5a Map toolkit + Omaha** (~4 d):
  - **Toolkit:** `tools/mapgen/` with a terrain sculpt/stamp from zone tables, spline roads, hedgerows and lanes, jittered density scatter, obstacle belts, and MG-nest siting.
  - **Omaha map:**
    - zones, objectives A–E and spawns exactly as in §3.3, racks (crates on the shingle and in the LCVPs), and hand-authored defense slots and cover points
    - a 30–50 m cover cadence, decorators at the §3.5 minimum densities, and the template made available with its thumbnail
- **W5b Mont-Castre** (~3 d): starts once W5a's toolkit commit lands. Follows §3.4 (template renamed, description updated) and uses the same toolkit.
- Both maps also go into the MP map selection. One of them writes `docs/asset-usage-default-maps.md`, which lists every asset and where it is used or why it is excluded.

## Wave 3: tuning and acceptance (W6b, then this session)
- **W6b:** tune the AI on both maps. Run the obstacle course (zero unrecovered stuck events), the creative live-rebuild test, and 20-minute soaks with 32 bots on both maps plus a large random blank map, all against the §12.3 thresholds. Compare with the baseline. Objectives must fall in 4–10 minutes each.
- **This session:**
  - Run the full §13 pass.
  - Check the performance target (32 bots).
  - Write the PR description with every [DECISION].
  - Hand `mp-update` to the main session to merge into master.

## [DECISION] defaults (one config value each)
- Protection ends on fire or throw: on.
- Kar98k limb hit = 2 shots.
- Springfield as the brief has it.
- Ammo boxes: decorative plus resupply (resupply already exists).
- Capture: defenders on the point pause capture, no decay.
- Old maps keep their stored attacking side.
- The repo's RPMs are used (Garand 400, Kar98k 60, …), with STK bands kept.
- FG 42, M2HB and M1C get rows in the matching bands.
- 32 bots in total; humans replace bots.
- The `M` key opens the overview, and the start marker is set by drop-in.

## Verification
- Each stream: its new suite plus the full suite (`tools/run_all_tests.sh` including match, ai and `--net`).
- After each merge into `mp-update`: the full suite again in `build-mpu`.
- Final:
  - §13 checks: 1/10 objectives load, 0/11 refused, 5-point maps load; protection; damage table; jump within 2 %; sights on every weapon; no blank thumbnails.
  - AI soak report.
  - `--benchmark-match` with 32 bots on both maps.
- Not verifiable here, so reported honestly: separate-PC LAN, min-spec hardware performance, and the playtest recording, which the user must do.

## Change: Mont-Castre cancelled (2026-11-06, user decision)

The user said: "dont make the new map, instead focus all on AI for now".

- **W5b (Mont-Castre / Hill 122)** is cancelled. Its branch is not merged, and the New Map entry "Normandy Forest" stays "Not included in this build yet".
- **W5a (Omaha)** ships.
- **AI tuning (W6b)** is the top priority. It runs on village, Omaha and a random blank map.
- **Merge order:** W5a → W6b tuning → master.
- **Further Normandy maps** will be built by the user in the editor.
