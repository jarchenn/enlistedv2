# P2: draw submission, leaf merge, furniture

Goal: fewer submitted draws and less CPU per frame on Chateau town and D-day, with no visual change. Every part is a `Renderer::Perf` switch, on by default. Turn one off with `--rq-off <name>`; A/B it with `NMK_PERF_AB=<name>`. `p2` names the first three together.

| Switch | What it does | Code |
|---|---|---|
| `dlcache` | Room-template furniture past the furniture draw distance skips the `buildDrawLists` loop (only the piece that pushes its building's merged batch stays). Nature patches reuse their footprint hash until the footprint list changes. | `App::buildDrawLists`, `natureDrawItems` |
| `instdepth` | The sun cascades and the sky-occlusion map draw every mesh that occurs more than once as one instanced draw. | `Renderer::drawItems`, `GpuMesh::ivao` |
| `folbatch` | Standing trees of one species in one 64 m cell share a world-space leaf instance buffer. A pass draws a run of visible trees at the same LOD as one draw. | `Renderer::prepFoliageBatches`, `drawFoliage` |
| `leafmerge` | A building whose doors, shutters and sashes are all at rest and intact draws them from one mesh pair (opaque + glass). | `buildLeafPose` (assets.cpp), `App::buildDrawLists` |
| `furndist` | Furniture draw distances per preset, with zoom and hysteresis; no furniture in the far sun cascade or sky occlusion. | `InteriorSys::updateBatches`, `propDraw` |
| `furnclutter` | Low preset: every second small non-blocking room piece is not drawn. | `InteriorSys::clutterHidden` |

## What each part keeps unchanged

- **`instdepth`:** culling and caster filtering run before the grouping, so the same items are drawn. Only the two sun cascades and sky occlusion use it; light shadows keep per-item draws. Each mesh owns its instancing VAO, freed with the mesh, so a rebuilt damaged wall bay never inherits stale setup. GL calls: `glDrawElementsInstancedBaseVertexBaseInstance` and `glVertexArrayBindingDivisor`, both already in the Mac shim.
- **`folbatch`:** per-tree frustum culling, LOD hysteresis and the R2 shadow LOD are unchanged. A felled, moved or removed tree changes its cell's signature and the cell is rebuilt (at most 8 cells per frame; until then its trees draw one by one). Felled trunks and ivy are never batched.
- **`leafmerge`:** one mesh pair is cached per asset and open/closed combination, built on first use. Any leaf that is moving, stopped part-way, detached or has broken glass puts that building back on the per-leaf path the same frame. Vertices keep their leaf-local position and normal (attributes 8 and 9, marked by attribute 7 w = 1.3), so material patterns match the per-leaf draw.

## Furniture rules

Room-template furniture and interior props (`extra.furnish`) take no damage:
- rounds still stop at or pass through a piece by the existing ballistics rules;
- blasts change nothing;
- a room lamp can still be shot out: the light goes dark, its mesh and the furniture batch are untouched (LAN peers replay the round, as before);
- a piece whose floor or wall is destroyed, or that a tank drives over, is removed: no fragments, no broken mesh, collider removed. `InteriorSys::reset` restores it.

Hand-placed and outdoor props (street lamps, crates) and all structure (walls, floors, ceilings, trims, doors, windows) stay destructible as before. `InteriorSys::furnDestructible = true` turns the old rule back on; the break-machinery steps of `--selftest-interiors` do that after the new rule's checks.

Furniture damage was never a LAN event (each peer derived it from the explosion), so no message or version changed.

**Distances (`furndist`), eye to the building's furniture, × the zoom:**

| Preset | Small pieces | Large silhouettes |
|---|---|---|
| Low | 24 m | 50 m |
| Medium | 32 m (as before) | 75 m (as before) |
| High | 32 m (as before) | 75 m (as before) |

A building's batch switches tier with 10 % hysteresis. Within 18 m pieces draw at full detail; beyond that from the merged box-LOD batch.

**Low clutter (`furnclutter`)** hides pieces draw-side only. Colliders, cover and nav are the same on every preset, so game state matches between players. Lamps are never hidden.

## Measuring

- `NMK_DLPROF=1`: logs draw-list items and CPU per source category (trees, nature, props, buildings), how many buildings merged their leaves, and `render()` CPU per pass.
- `screens/perf/r2ab.js <summary_maps.json> [filter]`: A/B table per preset.
- `screens/perf/pngdiff.js <dirA> <dirB>`: mean difference per frame; `pngcells.js` shows where two frames differ.

## Measured and rejected

- **HLOD (PF2's far-building merge):** `buildDrawLists` 2.97 → 4.86 ms for 33 fewer draws on Chateau town, plus 38–140 ms rebuild hitches. Stays off.
- **Epoch-keyed cache of light off-reasons:** kept a lamp lit after its room was breached when state changed without a wall rebuild. Replaced by a cache of only the furnish host/room lookup; lamp, glass and breach state are read live.

## Not done

- Full-detail merged furniture meshes per building: about 15,000 pieces on Chateau town would need on the order of gigabytes of vertex memory. Near pieces draw from shared asset meshes; far ones from the box-LOD batch.
- Merged furniture colliders: bullet hits and the support check identify a piece by its own body.
- A new intermediate furniture LOD: pieces already switch to merged box meshes at 18 m.
- Main-pass instancing of repeated props: the main shader takes per-item uniforms; the depth passes are instanced.
