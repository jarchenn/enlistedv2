# Testing

The self-test runs in a visible window and uses no mocks. It drives the same functions the UI calls: `placeInstance`, `checkPlacement`, `enterExplore`, `toggleInteract`, save, load, undo and duplicate. The character is steered with the same `ExploreInput` the keyboard produces, so it moves through the real Jolt character controller and the real colliders. The simulation runs at a fixed 1/60 s step, which makes the walkthrough deterministic.

| Command | What it does |
|---|---|
| `--selftest` | **Phase A.** New Blank Slate map. Places the farmhouse at 37°, sets the marker and walks the full route. Returns to Creative, moves the house, saves, then walks every other asset that has a `walkthrough` (the cottage, at 245°). Exits |
| `--selftest-reopen <map>` | **Phase B**, in a new process. Reopens the map and checks the ID, transform and marker. Walks the route at the new position, then duplicates the house at 251° and walks that copy. Runs the instance-independence check, 10 mode cycles, delete/undo/redo/undo, the missing-asset round trip and the `.bak` check |
| `--benchmark` | Places 30 furnished structures in rotated copies, records a 12 s flyover, then walks the farmhouse interior. Writes frame times and memory to `selftest_out/benchmark.json` |

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
