# Budgets (VF1, 2026-11-10)

Measured on integ-dday 8497fb2, Release build (LLVM-MinGW), one `--selftest-maps` run with
`NMK_MAPS=chateau_town,dday` at Medium. The GPU was shared with two other render streams during the run, so frame times
are noisy (the same view measured 12-390 ms across presets). Treat the timings as an upper bound, not a benchmark.

## The "about 14,000 KB" figure

That is the **executable alone**: `NormandyMapmaker.exe` is 14,434,816 bytes (14,097 KiB) for this build, with code and
libraries statically linked. The game also needs its `content/` folder, which is about 30 times larger.

## Package on disk (`content/`, 418 MB, 1104 files)

| Type | Size | Notes |
|---|---|---|
| Textures (`.jpg` / `.png`) | 315.9 MB | 215 MB material sets under `textures/`, the rest weapon / vehicle / character maps |
| Models (`.glb` / `.bin`) | 40.3 MB | weapons 69 MB and vehicles 46 MB folders include their textures |
| Navmesh sidecars (`.nav`) | 31.9 MB | 7 pre-baked template navmeshes (PF1) |
| JSON (assets, maps, settings) | 29.7 MB | building assets 42 MB folder total |
| Audio | ~0 MB | the sound banks are not packaged: `tools/fsb5.py extract` (543 samples missing in a clean checkout) |

## Loaded memory

| Item | Value |
|---|---|
| Textures on the GPU | 304 MB: 76 material sets, BC3 / BC5 compressed with mips (94 / 94 slots live; 36 sets on disk unused by a slot) |
| Process working set, Chateau town | 3751 MB peak, +463 MB for the map (4031 instances, 15503 furniture pieces, 12.6 s load) |
| Process working set, D-day | 2386 MB, 1.0 s load (3135 instances) |
| Geometry memory | not instrumented separately (inside the working set); no counter exists yet |

## Frame (Medium, gameplay views)

| View | Frame avg (worst) | GPU | Draw calls | Visible triangles |
|---|---|---|---|---|
| Chateau town A1 spawn | 15.6 ms (21.9) | 15.6 ms (opaque 3.9, shadows 1.4, probes 0.5, grass 0.4) | 6476 | 14.7 M |
| Chateau town A1 spawn, Low | 11.4 ms | | 3131 | 7.3 M |
| Chateau town A1 spawn, High | 32.6 ms | | 11066 | 24.8 M |
| D-day A1 spawn | 18.1 ms (32.5) | 18.0 ms | 242 | 1.5 M |
| D-day A1 spawn, Low | 5.8 ms | | 113 | 0.7 M |

CPU and GPU time are close to equal at these views: the frame is GPU-bound. Per-pass GPU timings are in
`selftest_out/summary_maps.json` (`gpuPassMs`).

## VF1 changes

Downpipes added to 9 house-kit buildings: +305 parts, under 0.1 % of a town's visible triangles, all far-LOD (`lod: 1`).
No texture or package change.
