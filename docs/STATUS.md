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
2. **Templates.** *Omaha Beach (Easy Red)* and *Normandy Forest* appear in New Map but are disabled. There is no beach research yet (see `docs/normandy-terrain.md`), and no water, vegetation, instancing or LOD.
3. **Edge snapping** for walls and fences (there are no wall assets yet).
4. **glTF import.** Every asset is authored procedurally with `tools/build_assets.mjs`. No glTF importer is wired in, and there is no third-party art.
5. **Rendering polish.** No SSAO or contact shadows, so furniture-to-floor contact is weaker than the spec asks for. No LODs, no foliage or wind, and no water. Stone uses a regular procedural coursing pattern, not photographic textures.
6. **Packaging.** The release is a portable zip, not an installer. It hasn't been tried on a second, clean machine.
7. **Recording.** There is no continuous video. The automated walkthrough plays visibly in the window, and screenshots are captured from the running app.
