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
