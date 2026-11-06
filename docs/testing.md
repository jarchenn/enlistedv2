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
| `--selftest-grass` | M5: 20+ checks covering placement determinism, footprint exclusion at 33°, preset invariance, Off frees memory, crater re-masking only touched chunks, reset, move and undo, Apply damage, template-version guard |
| `--selftest-fps` | FPS update F1-F4: GLB import fit (within 1 % of real length) and textures, fallback, ADS rear/front on centre (< 0.5 px) for every weapon at world FOV 72 / 58 and weapon FOV 60, critically damped spring, ADS round from F along normalize(F - R) (< 1 mm, < 1e-4 rad), hip muzzle sweep, sway decay (< 1 % in 0.5 s), bob vs speed, breathing and hold-breath, no sprint in ADS, IK residual < 1 mm and elbows down, no stretch out of reach, DoF CoC values, GL-error-free hip / ADS frames, Garand reload clip, shaders compiled |
| `--selftest-env` | F5-F8: glass HP 1 and pane energy loss, empty sash, shutter HP 2 + rigid body + fractured mesh, .45 stopped by a shutter, door HP 10 and E_new = E_old - mu dT, debris cap, standard and AT crater against the formula at 0 / R/2 / R and wall slope <= 35 deg, walking out of an AT crater, PaK AT shell crater, apple felled on the 30th round, trunk topples, HE fells the oak, clouds without non-finite pixels, tank trap immune to 100 rifle hits but not HE, sandbag stops .30-06, blast occlusion 0.1 behind sandbags / 1.0 in the open, builder slope threshold on a real crater wall, snapping (gap, tangent continuity), curved chaining, F5 discard, Creative snap, Apply keeps a built sandbag and a felled tree, Ctrl+Z, shaders compiled |
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
