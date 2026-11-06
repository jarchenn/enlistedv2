# Status against the build spec

Reference machine: AMD Ryzen 7 5800X, NVIDIA GeForce RTX 2080 Ti (driver 610.88, GL 4.5.0), 32 GB RAM, Windows 11 Pro, 1920×1080.

## Milestone 1, the first playable sequence (spec §2): done and automated

Each step in the spec's sequence is run by `--selftest` and `--selftest-reopen` against the real systems. Phase A passes 50/50 checks and phase B passes 68/68.

| Spec step | How it is verified |
|---|---|
| 1. Blank Slate | New map from the `blank_slate` template. It starts with zero placed structures, and a build-area corner accepts a building |
| 2. Place furnished two-storey farmhouse | `placeInstance` runs the same validation as a click. An overlapping placement is rejected with a reason |
| 3. Rotate 37°, start marker at the front door | Marker validity uses a physics capsule overlap test |
| 4. F5 to eye level | Eye height is checked at 1.62 m above the marker |
| 5. Open front door, kitchen, shuttered casement | The prompt must show the right interactable. The casement opens inward, then the shutters open outward |
| 6. Front stair up, across, rear stair down | The floor height is checked at each landing (3.30 m and 0.30 m). The test also stops halfway up and reverses |
| 7. Back to Creative, move, save, close | The map is saved atomically and the process exits |
| 8. Relaunch, reopen, repeat | A new process opens the map. The ID, transform and marker are compared, and the full walkthrough runs again at the new position |

Further checks:

- A duplicate at 251° gets a fresh ID and deep-copied defaults, and the walkthrough passes on it.
- Each copy keeps its own interaction state.
- 10 Creative/Explore cycles leave the collider count, runtime instances and audio emitters stable, with no stale prompt.
- Delete, undo, redo, undo restores exactly one building with identical settings and no orphan colliders.
- A placement whose asset is missing keeps its data through a save, and the previous save is kept as `.bak`.
- A door that closes on the player reopens. A door opening into the player stops.

## Destruction, field ordnance and environment update

Each milestone has its own self-test mode, and all of them drive the real systems. Reports are in `docs/bench/`. Design notes are in [destruction.md](destruction.md), [weapons.md](weapons.md) and [implementation-notes.md](implementation-notes.md).

| Milestone | State | Verified by |
|---|---|---|
| M1 Wall damage panels on all 14 assets (generator, loader, render, Jolt) | Done | `--selftest-damage` (97 checks) |
| M2 Deformable Blank Slate ground, craters, foundation protection | Done | `--selftest-damage` |
| M3 PaK 40 station, HE shell, ammunition crate | Done | `--selftest-pak` (52 checks, real fired shells) |
| M4 Eight weapon entries, container ammunition, reload and resupply | Done | `--selftest-weapons` (139 checks) |
| M5 Materials: steel, stone, wood end grain, plaster, flooring, bunker concrete, pasture | Done | `--capture-materials` before and after at identical cameras, in `docs/materials/` |
| M5 Instanced grass with Off/Low/Medium/High presets, LOD and exclusion masks | Done | `--selftest-grass`; `--benchmark-stress` |
| V1 (first pass) Grass species, clustered density, near-field sward ground | Partial | `docs/ground-grass.md`; `--selftest-grass` |
| Session overlay, plus explicit *Apply damage to map* (undoable) | Done | damage and grass suites |
| Range example map (`content/examples/range.nmap.json`) | Done | `--build-range-map` (13 checks) |
| One continuous recording | Done, scripted | `--record-range` (17 checks), 79 s MJPEG AVI |

What M5 found and fixed:

- **Dark steel and paint.** Paint was authored as a metal, so the albedo went to specular and almost nothing reflected indoors. The shutter in `before_steel_shutter_indoor.png` was near black. Paint is now dielectric, and bare steels keep metallic values.
- **Indoor specular.** Indoor environment specular reflected the occluded sky. It now falls back to a fill term under the sky-occlusion map.
- **Stone.** It now has irregular course heights, jittered head joints, pillowed faces and a macro tone variation, instead of a regular grid.
- **Crater bowls were drawn filled** since M2. The tile mask is a normalised `GL_R8` texture written with the value 1, so the base mesh was never hidden. Collision was always right; only the rendering was wrong. Fixed, with before and after in `docs/materials/grass_crater_*.png`.
- **A single NaN pixel** from grazing-angle shading poisoned the auto-exposure mip average. The PaK sight view then climbed to the exposure cap and washed out. Shading output is now sanitised, and the meter ignores non-finite averages.

Known limits of this update:

- The recording is a **scripted** run through the same input path as the keyboard and mouse, not a hand-played session. Its MP 40 reserve is trimmed to one magazine to keep the clip short, and the video's captions say so.
- Weapon and gun figures are game tuning. `docs/weapons.md` cites the brief's sources and ships no reference images.
- A 0.42 m crater does not conceal a crouched head; about 0.7 m is needed.
- The other 26 catalog assets, the disabled templates, glTF import, multiplayer and an installer are out of scope.
- The benchmarks in this update were measured while a second copy of the game was running on the same GPU (see [benchmarks.md](benchmarks.md)).

## FPS weapons, ballistics, environment and defences update

This update was requested in `claude_instructions.md`, inside `m1_garand_claude_package_v3.zip`. The plan is [plan-fps-update.md](plan-fps-update.md) and the details are in [fps-weapons.md](fps-weapons.md).

| Phase | State | Verified by |
|---|---|---|
| F1 GLB import and bake (own GLB reader; skins, spec-gloss, animation, 1024² textures), calibration sheets | Done | `--selftest-fps` (import fit within 1 %, textures, fallback) |
| F2 Fixed-FOV textured viewmodel, ADS on the real sight line, critically damped spring, rounds from F along normalize(F − R) | Done | `--selftest-fps` (≤ 0.26 px at three FOVs, < 1 mm / < 1e-4 rad) |
| F3 Sway, bob, breathing with hold-breath, near-field sight depth of field | Done | `--selftest-fps` |
| F4 Two-bone IK arms on per-weapon sockets | Done | `--selftest-fps` (residual < 1 mm, elbows down) |
| F5 Glass, shutters and doors (HP 1 / 2 / 10, Jolt rigid bodies), energy-budget penetration | Done | `--selftest-env` |
| F6 Crater formula Z −= D(1 − d²/R²)², standard 0.5 / 1.5 m and AT 1.2 / 3.0 m; PaK 40 AT round (X) | Done | `--selftest-env` (formula at 0, R/2, R; walk out) |
| F7 Apple, oak and poplar trees with sway mask and felling (30 bullets / 1 AT); fBm clouds | Done | `--selftest-env` |
| F8 Tank trap and straight / curved sandbags; B build mode with a 35° slope rule, socket snapping and blast occlusion ×0.1 | Done | `--selftest-env` |
| Regression | 212 / 68 / 97 / 52 / 139 / 21 / 13 unchanged | `tools/run_all_tests.sh` |

**Decisions you made:**
- The GLBs ship. Fill in the author and licence blanks in [credits.md](credits.md).
- AT craters are scaled down to 1.2 m so they stay climbable.
- The previous update's benchmarks, packaging and report were skipped.

**Known limits:**
- The arms are procedural blocks; no arm model was supplied.
- The BAR and MG 42 are single meshes, so they have no separate magazine or belt part.
- The Thompson has factor-only materials.
- There is no player health; blast exposure is a readout.
- No vehicles exist; `blocksVehicles` is only a flag on the instance.
- Clouds don't appear in reflections.
- The package's reference images were placeholders.

## Normandy expansion (weapons, racks, fire modes, AT destruction, movement, visuals)

Details, the acceptance checklist (PASS / FAIL / NOT TESTED), old and new values and the measurements are in [expansion.md](expansion.md). In short:
- Carbine, FG 42, Bazooka, Panzerschreck and a carried / deployable M2 are integrated, and the Kar 98k uses the supplied model.
- The Tommy gun and PaK GLBs were inspected and not used (variant mismatch; a static unlit mesh).
- Allied and Axis racks, an F fire selector, one-shot AT-gun wrecks, stand / crouch / prone, run ×1.25 and player gravity ×0.8 are in.
- The ground, routes, grass, trees, hedgerows, dressing, lighting and weathering were reworked procedurally.
- Networking checks are not applicable (no multiplayer).

## Sight calibration and the renderer pass (BSL as the visual reference)

- **Sight calibration.** The FG 42 and MG 42 anchors were measured on the meshes, both bores being off-centre. The MG 42 eye relief is 0.186 m, which makes its sight picture 12 % larger. Per-weapon `ads` calibration data and debug markers are in. Hip-fire flashes, tracers and impacts use the original effects. `--selftest-sights` covers it ([fps-weapons.md#calibration](fps-weapons.md#calibration)). Sight blur is removed.
- **Entities, complete update E1-E5:**
  - three wire obstacles from the supplied set: simplified, with an LOD, player-only wire volumes, cut by blasts stretch by stretch;
  - tank trap ×1.25, with v1 migration;
  - Mk 2 and M24 grenades;
  - gravel, dirt and concrete path entities, draped on the ground and re-draped into craters;
  - a knife;
  - the Flak 18 emplacement on the station framework, from the supplied model.

  See [entities.md](entities.md).
- **Sights, complete update W1.** Kar98k, M1 Carbine, Thompson, Garand, BAR and Panzerschreck anchors measured on the supplied GLB meshes. The BAR ladder leaf is folded and the Garand peep opened at bake (`model.meshEdits`). The Panzerschreck is viewed from its right side with the shield peripheral and a mild zoom. ADS is one blend of world FOV, pose and weapon FOV (`adsTime_s`, 0.25 s), with no zoom step ([fps-weapons.md#w1](fps-weapons.md#w1)).
- **Renderer.** Two shadow cascades with soft PCF, cloud shadows, a rebalanced sun and sky, a hue-preserving tonemap, a gradient sky and self-shadowed clouds, height haze, light shafts, SSAO on indirect light only, subtle bloom, SSR on wet ground, and world-only TAA with foliage motion vectors. The weapon is drawn after it, so nothing ghosts. Each effect has a toggle, and `--legacy-look` restores the old look. All shaders are original implementations; BSL's code was not reused (licence unclear). Details, the audit and the measurements are in [rendering.md](rendering.md).

## What's implemented

- **Engine.** C++20, CMake, OpenGL 4.5 core (DSA), GLFW, GLAD, GLM, Dear ImGui, Jolt Physics (`CharacterVirtual`), nlohmann/json, miniaudio.
- **Renderer.** Metallic-roughness PBR in linear space with ACES tonemapping and sRGB output. Lighting comes from a 4096² stable (texel-snapped) PCF directional shadow map, a hemisphere sky, and environment specular from the analytic sky.
  - A top-down **sky-occlusion map** dims sky light indoors, so rooms read darker than the overcast exterior.
  - Auto-exposure adapts on the GPU with no CPU stall, and only in Explore.
  - Procedural object-space materials with bump: coursed limestone, lime plaster, oak/cherry planks with grain direction, worn paint, slate courses, terracotta tiles, brick, metal, fabric, concrete.
  - Separate glass pass with Fresnel reflection and sorting.
  - Editor overlays (selection, footprints, snap preview, marker capsule) are drawn in their own pass and never in Explore.
- **Assets.** Definitions are data files (`content/assets/*.json`) kept separate from placed instances, and GPU meshes and physics shapes are shared. Furniture is part of the building, so it moves, duplicates and deletes with it. Each named interactable stores its hinge pivot, rotation sign, angle, timing, collider, sound, aperture and default state.
- **Creative.**
  - Fly camera; asset browser with real rendered thumbnails, category, search, enterable and min-floors filters, footprint and floor count.
  - Repeated placement with green/red preview and plain-language reasons ("Rear foundation is unsupported…", "Overlaps…", "Outside the build area").
  - Plumb placement on the highest sampled ground point, with a foundation skirt.
  - 15° or free rotation, scroll that depends on context, select by clicking any part (for example a chair through a window), drag-move as one undo step, numeric X/Z/yaw.
  - Grid snap and overlay, duplicate, delete, snapshot undo/redo.
  - Per-instance door and window defaults, both as checkboxes and as clickable 3D labels.
  - Start marker with capsule validation; New, Open and Save As dialogs; unsaved-changes prompt.
- **Explore.**
  - Jolt `CharacterVirtual` for walking, sprinting, crouching (blocked if there's no headroom), jumping and gravity.
  - Stairs use smooth ramps through the tread centres with vertical-sided guards. Interaction is ray-based with occlusion by walls.
  - Doors and windows use eased rigid hinge motion and kinematic colliders, can reverse mid-swing from the current angle, and are checked for obstruction against the player capsule.
  - Spatialised synthesised sounds for wood doors, casements, shutters and steel (steel is ready for bunkers), plus closing sounds at the stop and footsteps that depend on the surface.
  - Ambience starts once and never restarts on mode changes.
  - Obstructed start markers search for nearby clear ground and say so.
- **Save format.** Versioned JSON (`schemaVersion` 1) holding the template id, version and seed; instance IDs, asset id and version, transforms and overrides; removed vegetation IDs; start marker; and editor camera. Unknown fields are preserved. Saves write a temp file, verify it, then swap it in, keeping a `.bak`. Built-in examples open as copies.

## Not done yet

These items in the spec are **not** in this build, roughly in the order the spec's implementation plan would tackle them:

1. **Catalog.** 14 of 40 assets are done:
   - Houses: farmhouse and cottage.
   - Large buildings (all 4, as 3-5 storeys): village schoolhouse (3), village inn (4), stone warehouse (3), municipal building (5). Each has two independent switchback stair cores at opposite ends of a rear corridor, verified up one core and down the other on every floor.
   - Small buildings (all 4): carpenter’s workshop, stone dairy, storehouse, roadside office.
   - Bunkers (all 4): MG casemate with a two-leaf steel shutter, observation post with a top-hung armoured viewing flap, log field shelter with a top-hung timber shutter, command shelter with a blast-wall entrance, gas lock and gas-tight door.
   - Every one of these has a scripted walkthrough at an arbitrary rotation in `--selftest` (212/212 checks).
   - Still to do: Rubble, Ruins, Trees, Bushes, Walls and boundaries, Fieldworks, and the other two Houses variants. The bunker floors sit at grade, not dug in, because the terrain isn’t excavated yet.
2. **Templates.** *Omaha Beach (Easy Red)* and *Normandy Forest* appear in New Map but are disabled. There is no beach research yet (see `docs/normandy-terrain.md`), and no water or tree vegetation. Pasture grass is instanced with LOD; see the update section above.
3. **Edge snapping** for walls and fences (there are no wall assets yet).
4. **glTF import.** Every asset is authored procedurally with `tools/build_assets.mjs`. No glTF importer is wired in, and there is no third-party art.
5. **Rendering polish.** No SSAO or contact shadows, so furniture-to-floor contact is weaker than the spec asks for. No mesh LODs and no water. Grass has distance LOD and wind; trees and bushes don't exist yet. Stone uses a regular procedural coursing pattern, not photographic textures.
6. **Packaging.** The release is a portable zip, not an installer. It hasn't been tried on a second, clean machine.
7. **Recording.** `--record-range` writes one continuous scripted recording of the range example (MJPEG AVI). The original farmhouse walkthrough is still covered only by screenshots.
