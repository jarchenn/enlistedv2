# Graphics audit after P2 (DR1)

Measured on `integ-dday` 7c3a195 (R1, R2, G1, L1, P1–P3, MA2, MA3, LN1/LN2 and P2 merged), 1920×1080, RTX 2080 Ti, `--selftest-maps` on Battle 1, Chateau town and D-day, 44 Medium views. Raw data: `screens/perf/dr1_audit_base.json`.

**Read the GPU numbers as shares, not milliseconds.** The capture shared the GPU with other work sessions, so absolute pass times are inflated about 3×. The shares, the draw and triangle counts, and the CPU times are usable. Per-switch gains below come from interleaved A/B runs, where both sides see the same load.

## Where the frame goes (Medium, share of GPU time)

| Pass | Share | What drives it |
|---|---|---|
| Tree canopies | 28 % | Leaf-card count: about 20,000 triangles per canopy on average (trees, hedgerow sections, shrubs), the same at every distance |
| Buildings and props (opaque) | 25 % | Pixel cost of the material shader; far walls at full detail |
| Terrain | 12 % | Pixel cost |
| Light probes | 8 % | Capture draws |
| Far sun cascade | 5 % | Geometry |
| Near sun cascade | 4 % | Geometry |
| TAA, grass, models | 2.5 % each | |
| Post, composite, light shadows, bloom | 1–2 % each | |

Before L1's species leaves, canopies were about 7 % of the frame. They are now the largest pass.

## Load per map (Medium gameplay views, means)

| Map | Draws | Sun-shadow draws | Triangles | `render()` CPU | `buildDrawLists` CPU |
|---|---|---|---|---|---|
| Battle 1 | 1,392 | 236 | 5.0 M | 1.8 ms | 0.8 ms |
| Chateau town | 3,791 | 896 | 17.7 M | 3.4 ms | 3.5 ms |
| D-day | 1,252 | 288 | 10.5 M | 2.0 ms | 1.4 ms |

## How much of it is far away

Draw-list contents by distance from the eye, before per-pass culling (`NMK_DLPROF=1`). Chateau town, orchard view:

| | 0–60 m | 60–150 m | 150–400 m | 400 m+ |
|---|---|---|---|---|
| Canopies, trees | 125 | 454 | 357 | 3 |
| Canopies, triangles | 1.3 M | 5.0 M | 7.1 M | 0.1 M |
| Structures and props, items | 213 | 568 | 622 | 10 |
| Structures and props, triangles | 0.2 M | 1.6 M | 2.4 M | 0.02 M |

- **Canopies:** 12 M of 13.5 M triangles are beyond 60 m. A canopy at 300 m covers a few dozen pixels and still submits about 20,000 triangles per pass on average.
- **Structures:** a building's opaque mesh already drops to about 2,000 triangles at its far LOD, but its wall panels are drawn at full detail at every distance: 4,000 to 52,000 triangles per building.
- **Door, shutter and sash leaves** (merged per building by P2): 0.36 M triangles beyond 60 m. Not worth a LOD.

## Top remaining costs and what to do about each

| # | Cost | Fix | Status |
|---|---|---|---|
| 1 | Far canopies: full card count at any distance | Far tiers: 40 / 15 / 6 % of the cards past 80 / 180 / 400 m, cards grown to keep the leaf area | **Done: `farleaf`** |
| 2 | `folbatch` memory: 147 MB of leaf instance buffers for 315 trees | Batch only the two farthest tiers | **Done** (20.7 MB for the same view) |
| 3 | Far walls at full detail | Wall LOD without small parts, and without opening trim below 5 px/m | **Done: `walllod`** |
| 4 | Opaque pixel cost (25 %) | MA1's image materials replace procedural branches rather than stack on them; `matlod` already fades far detail | Other stream (MA1) |
| 5 | Terrain pixel cost (12 %) | R1's cache already replaces the noise; a cheaper far shading path past the third LOD ring is the next step | Not done |
| 6 | Light probes (8 %) | R2's budget is in; capture draws could use the wall LOD and skip leaves beyond the capture range | Not done |
| 7 | Far sun cascade (5 %) | Far tiers and wall LOD apply to it automatically; furniture is already excluded | Covered by 1 and 3 |
| 8 | `buildDrawLists` 3.5 ms on Chateau town | Per-building cached item lists, rebuilt on damage or door movement | Not done |

## Tried and rejected

- **Merged far-building batches (PF2's HLOD):** `buildDrawLists` 2.97 → 4.86 ms for 33 fewer draws on Chateau town, with 38–140 ms rebuild hitches. After P2's leaf merge a far building is 3–4 draws, so there is little left for merging to save.
- **Baked impostors for buildings:** not built. With the wall LOD a far building is 3,000–8,000 triangles in 3–4 draws; the cost that remains is pixels, which an impostor does not reduce.

## Not done from the DR1 list

- A view-distance fade at the render-distance cut.
- A far terrain shading LOD.
- Cross-fading between LOD levels: the tiers switch with hysteresis only. The far canopy subsets are nested, so a switch adds or removes cards rather than replacing the set.
