# Normandy expansion: weapons, racks, fire modes, AT-gun destruction, movement, visuals

This implements `Normandy_Claude_Expansion.zip` (`IMPLEMENTATION.md`, `ACCEPTANCE.md`, `ASSETS.md`) in the existing C++20 / OpenGL 4.5 / Jolt engine. There is no Unreal or Unity layer, no networking and no third-person body.

**Package handling:**
- The package is kept unchanged at `%TEMP%\nmk\exp\Normandy_Claude_Expansion`. It is outside the repository because the repository path plus the package's folder depth exceeds Windows `MAX_PATH`.
- `scripts/verify_package.py` reports 12/12 assets verified.
- The GLBs were copied into `assets_src/weapons/`, which is git-ignored. `kar98k (1).glb` was renamed to `kar98k_new.glb` there.

## Commits

| Commit | Scope |
|---|---|
| `d7d88f2` | Multi-material GLB import; Carbine, FG 42, Bazooka, Panzerschreck, carried M2 and the new Kar 98k model; fire selector; launchers; AT-gun one-shot wrecks; faction racks; village map and baseline; `--selftest-arsenal` |
| `feba879` | Posture state machine; data-driven movement tuning; rebindable keys; `--selftest-move` |
| *(final)* | Surface mask (routes, ruts, wetness); layered terrain; vegetation v2 (branching trees, hedgerows, shrubs); dressing; lighting, exposure and weathering; village dressing; after measurements; docs |

## Build and test commands

```bash
cmake --build <build dir>
```

```bash
bash tools/run_all_tests.sh <build dir>
```

```bash
<build>/NormandyMapmaker.exe --import-weapon-models assets_src/weapons content
```

```bash
<build>/NormandyMapmaker.exe --weapon-sheets <dir>
```

```bash
<build>/NormandyMapmaker.exe --build-village-map
```

```bash
<build>/NormandyMapmaker.exe --benchmark-village <tag>
```

```bash
<build>/NormandyMapmaker.exe --benchmark
```

What the commands do:
- **`run_all_tests.sh`** runs A, B, damage, PaK, weapons, grass, FPS, env, arsenal and move. Add `--bench` for the benchmarks.
- **`--import-weapon-models`** bakes the models.
- **`--weapon-sheets`** writes the calibration renders.
- **`--build-village-map`** writes `maps/village.nmap.json`, shipped as `content/examples/village.nmap.json`.
- **`--benchmark-village <tag>`** renders six fixed cameras and writes timing and screenshots.
- **`--benchmark`** runs the legacy flyover and interior benchmark.

## 1. Imported weapons

**Baker upgrade.** `src/model.cpp` now bakes one texture set per source material, and each image/role pair is written once. This was needed for the FG 42 (10 materials, 30 images), the M2 (11 materials, 33 images) and the Tommy gun (3 materials). It also supports:
- `matOverride`: repaint factor-only parts and tame gloss.
- `tint`.
- `lengthNode` fitting: the bazooka is sized and centred on its tube, not on the spare rockets beside it.

**Calibration.** Every model was checked on side and rear orthographic sheets (`--weapon-sheets`) and on hip and ADS screenshots. Sight points come from those sheets plus vertex slices, not an automatic top-edge guess.

| Weapon (faction) | Source | Tris | Fit | Orientation / notes |
|---|---|---|---|---|
| M1 Carbine (US) | m1_carbine.glb | 6.9k | 0.904 m | rot 90° y. Parts: bolt, op rod → handle, magazine → mag. Peep (0, 79, 330) mm, post (0, 80.5, 870) mm |
| FG 42 (DE) | fg42.glb | 16.9k | 0.975 m | rot 180° y (muzzle was −z). 10 materials at 512². Magazine, bolt and bipod are separate parts |
| M9 Bazooka (US) | bazooka.glb | 3.2k (after hiding spares) | 1.55 m tube | rot 90° y. Two-piece tube with centre coupling, which reads as an M9/M9A1 rather than an M1. No textures: the single factor material is repainted OD (rough 0.7). One spare rocket is the reload visual; two more are hidden. Sights on the tube's left side |
| Panzerschreck (DE) | panzerschreck_pickup.glb | 2.9k | 1.64 m | rot 180° y. **Pickup-grade:** a single mesh, albedo + MR only (no normal map, an emissive map is ignored). Acceptable in first person at 1024² but low detail. No separate parts |
| Carried M2HB (US) | m2_browning_machine_gun.glb | 59k | 1.65 m | rot 90° y. 338 unnamed parts in 11 materials, including tripod and ammo can. Also drawn on the M2HB tripod station: tripod parts on the base, gun parts on the cradle, station pivots re-measured (pintle 0.49 m, muzzle 0.985 m ahead) |
| Kar 98k + ZF39 (DE) | kar98k (1).glb | 14.9k | 1.11 m | Replaces the box model; the box parts stay as the fallback. The ZF39 keeps its procedural scope |
| Tommy gun | tommy_gun.glb | 18.2k | — | **Not integrated.** It is an M1928-style gun (drum, finned barrel, Cutts compensator, vertical foregrip), a different variant from the M1 Thompson, so it would change weapon identity. The existing M1 model and its calibration stay |
| PaK | pak_anti_tank_gun.glb | 227k | — | **Not integrated.** One static mesh with a `KHR_materials_unlit` material and no separable shield, cradle or trails. Using it would lose traverse, elevation, recoil and the wreck pose. The procedural PaK 40 stays and is fully part of the destruction work |

**Unsupported features**, reported by inspection:
- `KHR_materials_unlit` (PaK) and `KHR_materials_specular` (Tommy gun) are ignored.
- The Panzerschreck's emissive map is ignored.
- None of the new GLBs has an animation, so reloads are procedural: magazine dip, rocket slide-in, bolt motion where a bolt part exists.

**Launchers:**
- One rocket loaded, 5 (M9) or 4 (Panzerschreck) in reserve.
- Reload 4.0 s or 4.5 s, through the single-round reload path.
- Speed is 85 m/s or 110 m/s, with effective gravity ×0.35 (sustained motor) and a 50 m zero.
- Backblast smoke and a smoke trail.
- All values are game tuning in the weapon JSON (`rocket` block), not cited measurements.

## 2. Racks

**Assets.** `rack_allied` and `rack_axis` are generated by `tools/gen_env_assets.py`, following `references/weapon_rack_reference.png`:
- two A-shaped end frames with angled legs, feet boards and stretchers;
- a lipped buttstock tray;
- an upper rail with individual barrel slots and a rear brace;
- iron side hooks for launchers;
- weathered wood and iron materials;
- a simple box collision hull, so nothing snags.

**Display models** are the real world models (lightweight instances, not live weapons):
- **Allied:** Garand, Carbine, Thompson, BAR and M1C, plus the Bazooka on the side hooks and the M2 on its tripod beside the rack.
- **Axis:** Kar 98k, MP 40, FG 42, ZF39 and MG 42, plus the Panzerschreck.

**Panel.** E at a rack (within about 2.4 m, line-of-sight checked) opens a panel built from the weapon registry, filtered by `side`. It lists every US or German entry (7 Allied, 6 Axis), heavy and launcher weapons included, plus deployable equipment: the M2HB tripod (Allied) and the PaK 40 (Axis), placed through build mode. Each row shows name, calibre and current ammunition. Mouse or Up/Down selects, Enter or double-click equips, Esc or Close closes.

**Panel input and closing:**
- While it is open the cursor is released, and movement, fire, aim, look, Q, F, Z and B are suppressed for both keyboard and scripted input.
- It closes on leaving range (3.5 m), when the rack is removed, on a mode change or reset, or after equipping.

**Loadout policy:**
- Explore starts with every small arm carried. Q cycles the carried list.
- Equipping a carried weapon only selects it. Equipping a new weapon replaces the active slot.
- Each weapon keeps its own session ammunition; only crates restock. Swapping never refills or duplicates.
- A trigger held through a swap does not fire.
- F5 resets loadout, ammunition and fire modes.
- **Factions:** there is no player faction in this sandbox, so either rack can be used. Each rack lists only its own arsenal.

**Placement.** One rack of each faction is in the village: Allied by the spawn crate, Axis by the PaK position. Both are in the Creative catalog (Supply) and persist like any asset.

## 3. Fire modes

**Coverage.** Thompson, MP 40, MG 42 and FG 42 have `"modes": ["auto", "semi"]`, defaulting to AUTO. The M2HB station toggles the same way.

**Key.** F, rebindable in `content/settings/bindings.json`. V was not used because it is already the M1C iron-sight toggle.

**Behaviour:**
- A key-down edge switches once.
- Switching requires a trigger release and resets the burst counter, so no shot is produced and no auto timer is kept.
- SEMI fires once per press and still respects cadence and ammunition.
- AUTO cadence comes from the fixed 60 Hz step: 11 rounds per second at 700 rpm, both at 60 fps and at 10 fps.
- The HUD shows `[AUTO]` / `[SEMI]` / `[SINGLE]` / `[CARRY]`.
- The Carbine stays semi-auto and the launchers stay single-shot.

## 4. Rocket and AT-gun damage

**Building damage.** Rockets and shells use `App::explode`: the panel damage, fracture and collision path, crater (rocket crater scale 0.6 or 0.75), openings, trees, defences and effects. Blast and splash are tuned per weapon.

**Direct hits:**
- A PaK shell (HE or AT) or a rocket that hits an AT gun directly destroys it in one shot. "Directly" means the gun's body, or a swept segment test against its gun hit volume (union of cradle, barrel, shield and raised carriage parts).
- The swept test was needed because the open-frame collision hull let rockets pass between shield and trails.
- A near miss only applies splash, tuned separately: `splashGun` falls off to 0 at the blast radius against 100 HP. A Panzerschreck rocket landing 1.6 m from a gun leaves it at 92/100.

**One idempotent wreck transition** (`wreckStation`):
- disables firing;
- releases the operator safely;
- clears the prompt and choice;
- cancels the reload and loaded round;
- blackens and slumps the visuals;
- plays effects once;
- refuses crewing.

Repeat events are ignored. F5 restores guns. Apply persists `station.wrecked`, and Ctrl+Z restores it.

**Self-hits.** Shells ignore their own gun body, and the hit volume skips the firing gun.

## 5. Movement

| Setting | Old | New | Units / note |
|---|---|---|---|
| Run | 2.3 | 3.1625 | m/s (×1.25, then ×1.1) |
| Sprint | 4.6 | 6.325 | m/s (×1.25, then ×1.1, Shift) |
| Crouch | 1.2 | 1.32 | m/s (×1.1; Left Ctrl hold, or C) |
| Prone | — | 0.66 | m/s (0.6, then ×1.1; Z toggle) |
| Player gravity | 9.81 | 7.848 | m/s² (×0.8). Rigid bodies keep 9.81: verified |
| Jump velocity | 4.2 | 3.757 | m/s (×√0.8): apex 0.90 m held, measured 0.87 m; airtime 0.86 → 0.92 s |
| Eye height | 1.62 / 1.05 | 1.62 / 1.05 / 0.32 | m, stand / crouch / prone, smoothing 10/s |
| Jump buffer / coyote | — | 0.15 / 0.12 | s (`jumpBuffer_s`, `coyote_s`): see *Jump reliability* |

**Shapes:**
- Stand: capsule 1.8 m.
- Crouch: capsule 1.2 m.
- Prone: a 1.7 m horizontal capsule behind the head, turned with the body.

**Posture rules:**
- Postures are mutually exclusive.
- An expansion (into a taller posture, or into prone) happens only when the full target shape fits, ignoring the ground under the feet. Jolt's `SetShape` also refuses penetration.
- A blocked stand request stays pending and resolves when space frees up.
- Entering prone clears crouch intent. While prone, Ctrl does nothing. Z out of prone goes to crouch if Ctrl is held, otherwise to standing.
- Prone turning rotates the body only when the turned body fits.
- No sprint and no jump in lower postures. Getting up from prone also blocks the jump until the rise ends (Space while prone is the get-up). Releasing crouch does not: Space jumps at once during the 0.45 s crouch → stand rise.
- The camera never rises through a ceiling.
- Focus loss clears held input.

**What was investigated:**
- Movement already ran on a fixed 60 Hz step with normalised diagonals and direct velocity, so there is no acceleration slide; nothing to fix there.
- The only frame-rate artefact in speed was in the test's measurement, not the game: 10 fps measures the run speed exactly. (Dropped key presses on short frames were a real frame-rate artefact; see *Jump reliability* below.)
- There is no remote or third-person body, so remote posture is **not applicable**. Collision posture is real and tested.

**Jump reliability ("Space sometimes does nothing").** Every press on the ground now jumps. There were four causes:
1. *Frames with no fixed step.* Movement runs on 1/60 s steps. A frame shorter than that (any refresh rate above 60 Hz, or a 60 Hz frame that comes in a hair short) runs no step, and the edge-triggered `kp()` press read that frame was thrown away. Now edge presses (Space, E, R, T, X, B, Q, fire selector, prone, pack-up, rack keys, 1 / 2) and hand-wheel / scroll deltas from a no-step frame are carried to the next frame that runs one (`App::edgeCarry`). The scroll delta is also applied once per frame, no longer once per step.
2. *Exact-frame ground test.* The impulse needed the controller to be `OnGround` and settled on the very step the press arrived. A one-step ground flicker on bumps, slopes or step-ups ate the press. `Physics::updateCharacter` now allows a jump up to `coyote_s` (0.12 s) after the last settled ground contact.
3. *Pressing just before landing.* A press up to `jumpBuffer_s` (0.15 s) before touchdown is kept and jumps on landing.
4. *Crouch → stand rise.* The 0.45 s eye rise after releasing Ctrl used to block jumping.

Neither the buffer nor the coyote window allows a double jump: one impulse per settled ground contact. Steep ground never refreshes it, so the steep-face jump-climb fix stands. Presses made while crouched or prone are not buffered. `--selftest-move` covers a press on a 240 Hz no-step frame, a press 5–30 cm above the ground while falling, a press 0.05 s after walking off a 1 m ledge plus a mid-air press (no second jump), a press one frame after releasing Ctrl, and 8 of 8 presses on a 14° ramp walked up and down at run and sprint.

## 6. Visuals

**Ground and routes:**
- `surface.cpp` builds a 2048² wear / wet / kind / rut mask (0.39 m texels) from map routes (`extra.routes`: lanes, footpaths, gravel) and from structure footprints (a soil and splash band around foundations).
- The terrain shader blends meadow greens, dry grass, clover, soil, mud, gravel and compacted earth with pebbles, at four noise scales (80 m, 14 m, 1 m, 0.2 m) so nothing tiles.
- Wheel ruts turn muddy. Wetness darkens locally and is glossy only where water stands.
- Crater dirt and destruction are unchanged.

**Grass:**
- None on worn track (wear > 0.62), thinned and cropped short toward it.
- Taller rank verges just beyond the track edge, and taller darker weeds against walls.
- A darker palette with no bleached tips.
- Chunks rebuild only where the mask under them changed (content signature).

**Vegetation:**
- Trees were rebuilt with a recursive branch hierarchy and foliage clusters at limb tips plus inner mass: apple, oak, poplar and a new ash.
- Leaf cards scale with cluster area, carry per-card canopy self-shading (darker inside and lower), have hue variation and restrained translucency.
- Damage, stump and falling trunk are unchanged.
- New: `hedgerow_bocage` (8 m turf bank, woody stems, layered hedge; stops rounds, never falls), `shrub_hazel`, `shrub_bramble`.

**Dressing:** post-and-wire fence, field gate, telegraph pole and low dry-stone wall, all with simple snag-free collision.

**Lighting:**
- Lower ambient fill and a cooler sky and haze.
- The haze starts 35 m out, so the near view stays clear.
- Cloud cover is 0.62.
- Exposure is limited to 0.9–1.45 and adapts at about half its old rate (no pumping).
- Vignette 0.35 → 0.16, desaturation 12% → 7%.
- No world DoF; the sight-only DoF from the FPS update is unchanged.

**Buildings.** Localized weathering in asset space on stone, brick, plaster, wood and concrete: rising damp and splash at the foot of vertical faces, moss patches in that band, faint run-off streaks. Painted wood already had chipped-edge wear.

**Village.** Shipped as `content/examples/village.nmap.json`. It has:
- a worn lane through the village, footpaths to the doors and a gravel yard by the PaK;
- bocage hedgerows on selected lane and field edges, leaving the spawn exit, lane and gun lines open;
- fences, a gate, poles, walls, shrubs and brambles;
- more varied trees;
- both racks.

## 7. Performance

Measurement setup:
- 1920×1080, grass Medium, vsync off, NVIDIA RTX 2080 Ti.
- Wall-clock frame interval plus a `GL_TIME_ELAPSED` query per frame, with a 1 s warm-up then about 4 s per camera.
- Fixed cameras are listed in `benchmark_village_*.json`.

**Contamination:** the user's own NormandyMapmaker.exe (a separate process, not started by me and not closed) was running throughout. It loaded the GPU to about 40% / about 195 W, so absolute numbers are pessimistic. Before and after ran under the same condition.

| Camera | Frame p50 ms (before → after) | Frame p95 ms | GPU p50 ms | GPU p95 ms | Render CPU p50 ms | Draws | Triangles | Working set MB |
|---|---|---|---|---|---|---|---|---|
| overview | 2.10 → 4.90 | 2.61 → 6.06 | 1.27 → 3.37 | 1.77 → 4.45 | 1.87 → 4.41 | 809 → 1674 | 0.56M → 1.58M | 316 → 448 |
| road | 2.12 → 3.75 | 2.59 → 4.26 | 1.29 → 2.53 | 1.80 → 3.06 | 1.89 → 3.34 | 809 → 1674 | 0.57M → 1.59M | 319 → 451 |
| area | 2.10 → 4.92 | 2.61 → 5.61 | 1.28 → 4.07 | 1.78 → 4.54 | 1.88 → 4.63 | 808 → 1673 | 0.60M → 1.63M | 319 → 448 |
| foliage | 1.96 → 4.98 | 2.49 → 5.94 | 1.15 → 4.15 | 1.65 → 4.58 | 1.73 → 4.64 | 809 → 1674 | 0.53M → 1.59M | 320 → 429 |
| building | 2.23 → 3.45 | 2.80 → 3.88 | 1.43 → 2.33 | 1.94 → 2.79 | 2.01 → 3.08 | 808 → 1673 | 0.51M → 1.56M | 320 → 430 |
| lane_north (after only) | n/a → 2.73 | n/a → 2.99 | n/a → 1.59 | n/a → 2.08 | n/a → 2.47 | n/a → 1676 | n/a → 1.59M | n/a → 430 |

Raw data: `docs/expansion/before/benchmark_village_bench_before.json` and `docs/expansion/after/benchmark_village_bench_after.json`. The renderer pass that followed has its own table in `docs/rendering.md`.

Memory is the process working set. CPU time is the renderer's submit time per frame (`render_cpu`). There is no per-system CPU profiler, and no GPU memory counter beyond the model `gpuBytes`.

## 8. Before / after screenshots

They are in `docs/expansion/before/` and `docs/expansion/after/`, at the same cameras, resolution and settings:
- overview (composition of `references/current_game.png`), road, area, foliage, building;
- plus `lane_north` (after only: it was added for the hedgerows).

The village layout gained routes, hedgerows and dressing between the two runs, so the after shots show both the new rendering and the dressing.

## 9. Acceptance checklist

| Item | Result | Evidence |
|---|---|---|
| New assets inspected and imported reproducibly; unsupported features reported | PASS | `--import-weapon-models`, `assets_src/weapons/` + weapon JSON `model` blocks; §1 |
| Side and rear calibration renders, hip and ADS screenshots, dimensions, grips, muzzle and sight alignment | PASS | `--weapon-sheets`; `--selftest-fps` (ADS < 0.5 px at FOV 72 / 58 / 60 for every weapon, IK < 1 mm); `docs/fps/` |
| Carbine, FG 42, Bazooka, Panzerschreck playable with ammo, reload and registry; M2 heavy workflow | PASS | arsenal suite (launcher reload 1 + 4, M2 deploy, SEMI, pack-up 109 + 110) |
| No missing textures, invalid indices, broken transforms, skinning artefacts or shader errors | PASS | FPS suite texture checks, "all shaders compiled (0 errors)" in every new suite |
| Variant mismatch, mesh quality and animation disclosed | PASS | §1 (bazooka M9/M9A1, Panzerschreck pickup mesh, Tommy gun and PaK not used, no animations) |
| Both racks in map and catalog with several correct display models | PASS | arsenal registry checks; `docs/expansion/after/arsenal_rack_*.png` |
| Every faction weapon listed, heavy included; no silent omission | PASS | registry checks (7 US incl. M2 and Bazooka, 6 DE incl. Panzerschreck; PaK and M2 tripod as deployables) |
| Range, obstruction, equip, cancel, mouse capture, input suppression, leave range | PASS (single player) | arsenal suite; mouse capture verified by code path only (the self-test runs without OS cursor capture) |
| Swaps during fire, reload and ADS, trigger held: no free shot, stuck state, duplicate or refill | PASS | arsenal "held trigger through the swap", "rapid swapping keeps ammo" |
| Simultaneous users, remote equipment, respawn, authority | NOT TESTED (no multiplayer) | policy in §2; respawn = Backspace / F5 reset |
| Fire modes on Thompson, MG 42, FG 42, MP 40, M2; AUTO default; conflict-free rebindable key | PASS | arsenal selector checks; `content/settings/bindings.json` |
| Held toggle does not repeat; held SEMI fires once; release required | PASS | arsenal selector checks |
| AUTO cadence stable at varied frame rates | PASS | 11 rounds per second at 60 fps and 10 fps |
| Launchers damage real structure through the destruction system | PASS | "M9 rocket damages the stone building's wall panels through App::explode (2 panels)" |
| AT gun, Bazooka and Panzerschreck direct hits destroy an AT gun once; near miss separate | PASS | arsenal suite (three direct hits, near miss 92/100 HP) |
| Occupied gun releases operator; wreck not interactable; reset and Apply | PASS | arsenal suite |
| Thin targets, high speed, overlapping colliders, repeated events, muzzle proximity | PASS (partly) | swept segment tests; idempotence check; own-gun ignore. Muzzle-proximity self-hit: code path only |
| Old and new movement values reported | PASS | §5, `summary_move.json` |
| Distance over time, diagonal, frame-rate comparison | PASS | move suite |
| Ctrl, Z, Ctrl+Z, prone exit with Ctrl, blocked stand | PASS | move suite |
| Real crouch and prone collision, camera, no sprint lowered | PASS | move suite; remote body: N/A |
| Low ceilings, walls, prone turning, crater exit | PASS | move suite. Stairs while crouched / prone: NOT TESTED separately (the walkthrough suites still pass standing) |
| Focus loss, menus, respawn | PASS (code) / NOT TESTED (focus loss needs a real window switch) | |
| Player gravity does not affect physics objects | PASS | move suite rigid-body check |
| Two-client, late-join and latency checks | NOT TESTED: the engine has no networking | |
| Matched before / after cameras | PASS | §8 |
| Ground blends soil, grass, mud, gravel; worn paths; patchy grounded grass; local wetness | PASS (procedural) | after screenshots |
| Trees, hedgerows, materials, cloudy lighting, stable exposure, no world DoF | PASS (procedural, see limits) | after screenshots |
| Foliage LOD, shimmer, overdraw, texture memory, shader logs | PARTLY | Shader logs clean. No foliage LOD system (canopies are full detail at any distance); alpha-test shimmer visible at distance; texture memory: weapon models only |
| Before / after median and p95 frame times | PASS | §7 |
| All existing regression suites plus new coverage | PASS | final `run_all_tests.sh` tallies, report files |

## Remaining limitations

- **Quality.** The visual work is procedural: shader materials, box-part trees with alpha cards, no photographic textures or scanned foliage. It moves toward `references/visual_target.png`'s material layering, vegetation layering and lighting, but does not match it. What limits it is the lack of licensed high-quality textures (ground PBR sets, bark, leaf atlases) and authored tree meshes. None were available offline, and none were downloaded.
- **Windows.** No window recesses, sills, gutters or downspouts were added: the structures are generated by `tools/catalog_*.mjs`, and changing them would also mean re-validating openings and panel physics. Weathering is shader-only.
- **Foliage cost.** There is no foliage LOD or impostor system, so canopies cost the same at every distance (see §7).
- **Rack panel.** It is single player: no authority, replication, simultaneous-user or late-join handling, because no network layer exists.
- **Tommy gun and PaK GLBs** were inspected and rejected, as described in §1.
- **M2 tripod.** The station's collision is still the procedural tripod hull, not the GLB.
