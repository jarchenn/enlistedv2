# Testing

The self-test runs in a visible window and uses no mocks. It drives the same functions the UI calls: `placeInstance`, `checkPlacement`, `enterExplore`, `toggleInteract`, save, load, undo and duplicate. The character is steered with the same `ExploreInput` the keyboard produces, so it moves through the real Jolt character controller and the real colliders. The simulation runs at a fixed 1/60 s step, which makes the walkthrough deterministic.

| Command | What it does |
|---|---|
| `--selftest` | **Phase A.** New Blank Slate map. Places the farmhouse at 37°, sets the marker and walks the full route. Returns to Creative, moves the house, saves, then walks every other asset that has a `walkthrough` (the cottage, at 245°). Exits |
| `--selftest-reopen <map>` | **Phase B**, in a new process. Reopens the map and checks the ID, transform and marker. Walks the route at the new position, then duplicates the house at 251° and walks that copy. Runs the instance-independence check, 10 mode cycles, delete/undo/redo/undo, the missing-asset round trip and the `.bak` check |
| `--benchmark` | Places 30 furnished structures in rotated copies, records a 12 s flyover, then walks the farmhouse interior. Writes frame times and memory to `selftest_out/benchmark.json`. Only the 14 enterable structures are placed, so the scene matches the pre-update baseline |
| `--selftest-damage` | M1 and M2: every panel ID on all 14 assets is stable and unique; HE breaches masonry, concrete and timber at the expected counts; rotated copies; collision follows render; craters match physics to 1 mm; foundations are protected; reset, apply and undo |
| `--selftest-pak` | M3, with real shells from the world muzzle: crew prompt, sight camera, casemate breach, the four mechanical stops, hold-fire across position changes, handler move and turn, carriage sweep against a house, steep-crater refusal, failed deployment, ground crater, emptied gun, crate service radius, finite crate, F5 reset |
| `--selftest-weapons` | M4: presets and allowances, container accounting through every reload kind, bolt cycling, sights and zero, scope FOV, recoil determinism, bipod support, crate resupply to the allowance, strict-preset filtering, M2HB belts |
| `--selftest-grass` | M5: 20+ checks covering placement determinism, footprint exclusion at 33°, preset invariance, Off frees memory, crater re-masking only touched chunks, reset, move and undo, Apply damage, template-version guard, every shader compiled |
| `--selftest-fps` | FPS update F1-F4: GLB import fit (within 1 % of real length) and textures, fallback, ADS rear/front on centre (< 0.5 px) for every weapon at world FOV 72 / 58 and weapon FOV 60, critically damped spring, ADS round from F along normalize(F - R) (< 1 mm, < 1e-4 rad), hip muzzle sweep, sway decay (< 1 % in 0.5 s), bob vs speed, breathing and hold-breath, no sprint in ADS, IK residual < 1 mm and elbows down, no stretch out of reach, DoF CoC values, GL-error-free hip / ADS frames, Garand reload clip, shaders compiled |
| `--selftest-env` | F5-F8: glass HP 1 and pane energy loss, empty sash, shutter HP 2 + rigid body + fractured mesh, .45 stopped by a shutter, door HP 10 and E_new = E_old - mu dT, debris cap, standard and AT crater against the formula at 0 / R/2 / R and wall slope <= 35 deg, walking out of an AT crater, PaK AT shell crater, apple felled on the 30th round, trunk topples, HE fells the oak, clouds without non-finite pixels, tank trap immune to 100 rifle hits but not HE, sandbag stops .30-06, blast occlusion 0.1 behind sandbags / 1.0 in the open, builder slope threshold on a real crater wall, snapping (gap, tangent continuity), curved chaining, F5 discard, Creative snap, Apply keeps a built sandbag and a felled tree, Ctrl+Z, shaders compiled |
| `--selftest-arsenal` | Expansion: registry-built faction racks (every US / DE weapon, deployables), display models, prompt, panel input suppression, equip, no free shot through a held-trigger swap, swapping keeps ammunition, leave-range / Esc / mode-change close, fire selector (AUTO default, edge toggle, release required, SEMI once per press, AUTO cadence at 60 and 10 fps), Carbine / launchers unchanged, bazooka rocket damages building panels, launcher reload lifecycle, Bazooka / Panzerschreck / AT-shell direct hits wreck PaK 40s once, near miss splash only, crewed gun releases its operator, idempotent wreck, wreck not interactable, F5 reset, Apply + Ctrl+Z, carried M2 deploy / SEMI / pack-up with ammunition moved, shaders compiled |
| `--selftest-entities` | Complete update E1-E5:
- wire obstacles: catalogue, budgets, LOD / cull, editor round trip, walk-through blocked, no vault and no mantle (incl. a low wall and a 1.6 m ledge beside wire), rays through gaps, posts stop rounds, blast cuts only near stretches, small arms do nothing, "slow" traversal mode
- tank trap: ×1.25 and v1-map migration applied once
- Mk 2 / M24: one throw per press, fuse from release, single detonation, settles, distances by grenade and posture, swap cancels, H quick throw, wall-safe spawn, grenade-sized blast
- knife: one swing, glass, reach
- paths: size / undo / save, drape, crater re-drape, slab breaking, grass verges, snapping, walk across, surface lookup
- Flak 18: part roles, Move refused, 360° traverse, elevation stops, HE from the bore, AP select, reload, wreck
- audio (skipped without extracted samples): bank events, no shared recordings between distinct guns, MG 42 burst bodies and tail, reload foley, far layer; V9 pack pass: every arsenal / gun-station / tank sound slot (shot, far, tail, reload, clip, bolt open / close, single-round load, belt, smoke pop, wading step) resolves to bank samples, slots without a recording stay procedural, the Kar98k bolt is worked after a shot, the smoke grenade pops, wading plays the water steps

Screenshots `entities_*.png` |
| `--selftest-sights` | Per-weapon sight calibration (FG 42, MG 42), measured on the rendered first-person distance buffer with sight blur off. Before (legacy anchors) vs after. FG 42 aperture centre and post tip, MG 42 V-notch, each within 2 px of the dot at 1920×1080, 1440×1080, 2400×1080 and 1280×720, mid-recoil, after sway, crouched, prone and after a reload. MG 42 sight picture 10–15 % larger. Hip fire: the projectile (and its tracer) starts at the true muzzle; the flash and tracer use the original effects. ADS shots leave F. The close-wall obstruction sweep still works. **Complete update W1:** Kar98k (V-notch shoulder line and barleycorn tip), M1 Carbine, Thompson and Garand (aperture centre and the post tip at the aperture centre) and the BAR (hooded post tip, nothing near above the dot: folded leaf), each within 2 px before / after, at 2400×1080, mid-recoil and after a reload; sight-picture size ratios (Carbine, Thompson 10–15 %, Garand); ADS shots leave F. Panzerschreck: hip and ADS keep the central ±6 % box clear (≤ 2 % weapon pixels) with the shield left, mild zoom to world FOV 62, rocket from the muzzle toward the aim point. Smooth ADS: entry 0.20–0.30 s, monotonic world FOV with no per-frame step over 2.5°, constant while firing, continuous mid-way reversal. The remaining weapons keep the shared defaults. `NMK_SIGHTS_ONLY=id,id,ads` runs a subset while tuning. Screenshots `sights_<id>_{before_hip,before_ads,hip,ads,ads_clean,sway,hipfire,adsfire,wall}.png` |
| `--selftest-update` | Complete update gameplay: recoil (no ceiling, posture, bipod, no recovery), tap jump vs hold-Space vault, interaction reach, every bunker slit seen and shot through from the standing eye, the crew stations placed with their casemates (bore clearance, gunner on the floor, a round leaves the bunker). **MG 42 embrasure mount = the handheld MG 42:** the eye on the GLB sight line as drawn (< 1 mm), notch and front post on the centre (< 0.5 px), notch drawn at the handheld's full-ADS size (±1 %) with the same world FOV, the round launched along the sight line with the handheld's 200 m zero, tail on release, `reload_mg42`, a 200-round belt that refills to 200 without creating rounds, Move refused. Screenshots `update_mg42_{mount,handheld}_sight.png` |
| `--selftest-tanks` | Phase 2 tanks ([tanks.md](tanks.md)): catalog, bake and scale; entry through the E prompt; Ctrl toggles once and never crouches; hatch camera and roof MG; Space bow MG from the sight (rate, ball mount, BAR / MG 42 sound) and both MGs from the hatch, RMB main gun from the hatch, automatic belt changes; mouse look scaled by tan(fov/2) / tan(36°) for every ADS / scope / tank sight; the turret rate limit with the sight on the achieved gun line, wrap-around and elevation stop; bore zero against a real impact; the HE/AP state machine (loaded + R, R while loading, repeated R, empty reserve, same-step toggle and fire, hatch, no reserves, exit / re-entry); driving, steering, reverse, the moving-exit refusal, tank trap, tree, ramming (pole at speed and slowly against v' = sqrt(v² − 2E/M), fence + barbed wire, stone wall pushed in E / P, Tiger momentum through a wall, cottage wall pushed through, concrete casemate holds, props reset on leaving Explore), crater; a safe exit beside a wall; the damage matrix with real projectiles (cannon, Flak-class, bazooka, Panzerschreck, rifle; front, sides, rear, roof; turret at 0° and 90°; corners, underside, mixed, splash, a real PaK 40 shot); T7 masonry and glass with tank shells; the occupied wreck; the Tiger sight, zoom, exhaust, sound loops, hatch and MG; Apply / undo / redo / duplicate / save / reopen |
| `--selftest-lasso` | Lasso tool ([lasso.md](lasso.md)): freehand / circle capture with add / subtract and the touching rule, never generated furniture, no map-hash change; group move (< 1 mm), 90° turn about the centre, rejected move reverted, duplicate / delete / Del / Ctrl+D, one undo step each; elevation plateau / dome with the falloff ring (monotonic, no tile cracks, placements ride, exact undo, preview + cancel); water fill to the lip of a notched pit vs a brute-force spill level (< 2 cm), refusals, one undo step with the lowering, save / reopen and hash; all 12 paint types, soft edge, two-type seam, eraser, grass / nature / nav / footstep effects, byte-exact save, rendered patches distinct; move land (relief kept < 2 cm, smooth source, tree / paint / pond carried, turn, refusal onto a cottage). Screenshots in `selftest_out/lasso/` |
| `--selftest-move` | Expansion: tuning values vs old (speeds ×1.1, jump buffer / coyote), run / diagonal / 10 fps / sprint / crouch / prone speeds, jump apex and airtime, jump reliability (press on a no-step frame, buffered press before landing, coyote off a ledge, no double jump, jump right after releasing crouch, every press on a ramp), rigid bodies keep world gravity, Ctrl hold, low ceiling with pending stand and camera clamp, Z / Ctrl+Z policy, prone exit blocked under a crawl space, prone turning clearance, no jump crouched, crawling out of an AT crater; mantle ([movement.md](movement.md)): out of the straight / corner / zig-zag trenches from the floor and every firestep, over a 2.1 m wall, onto 2.0 m and 1.2 m-deep platforms, refused at 2.4 m, weapon lowered / hands on the ledge / camera arc, movement.json vs the bots' link limits |
| `--build-village-map` / `--benchmark-village <tag>` | Representative village (routes, hedgerows, dressing, racks); six fixed cameras with frame / GPU time percentiles and screenshots |
| `--benchmark-stress` | Grass flyover at three presets on the same path, then 40 HE impacts with smoke in Explore. Writes `selftest_out/benchmark_stress.json` |
| `--build-range-map` / `--record-range` | Build and verify the range example; scripted recording of it (see the README) |

Exit code 0 means every check passed. Reports go to `selftest_out/report_A.txt` and `report_B.txt`.

## Walkthrough steps, per asset

The steps live in the asset JSON in asset-local coordinates, so they run at any position and rotation:

- `goto [x,z]`, with optional `expectY` to check floor height
- `interact <id>` with an `aim` point. This checks that the prompt targets the expected interactable, then waits for the motion to finish and checks the open/closed state
- `look`, `shot` and `wait`

The farmhouse route covers:

- The front door, including a close attempt while standing in its swing, which must reopen.
- The kitchen casement, then the shutters.
- The front stair, including stopping mid-flight and reversing.
- The upper floor and an upstairs casement.
- The rear service stair down.
- Opening the rear door from inside and closing it from outside.

## Fast mode (`tools/run_fast_tests.sh`)

`tools/run_fast_tests.sh <build dir> <base ref> [--dry-run] [--net-port N]` is for checking work in progress. It
runs only the suites that the change touches, chosen from the files changed since `<base ref>` (e.g. `master`). Each
suite has a path rule, and `--dry-run` prints which suites were chosen and the file that triggered each one. A suite
with no rule always runs, so new suites are never skipped silently.

- Suite `A` always runs.
- The slow AI suites (`ai`, `aicourse`) run only when AI, nav or `content/ai` changes.
- The LAN test runs only when net, game, damage, interiors, lights or the map format changes.
- The `maps` frame-time gate runs whenever rendering, terrain, maps, assets, textures or lighting changes.
- `match` needs no GPU, so it runs in parallel with the GPU suites. Every GPU suite goes through
  `tools/gpu_turn.sh`.

Fast mode can miss a break between streams in a suite it skipped, so it does not replace the full run:
`tools/run_all_tests.sh --net` is still the check before master.

**aicourse timing:** its live-rebuild checks (bots re-route within 1 s, and none get stuck against a dropped
obstacle) depend on timing. Navmesh tiles rebuild on worker threads in real time while the simulation steps a fixed
1/60 s, so a change in frame cost can flip a check. Treat one failure as a re-run, and repeated failures on a quiet
GPU as a real problem.
