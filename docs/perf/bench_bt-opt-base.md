# VO benchmark: bt-opt-base

`bash tools/bench_views.sh <build dir> bt-opt-base` on 2026-11-20 16:42, commit 18dff3c0, framebuffer 2560x1440, 2 pm clear sky, UI hidden, the card held exclusively. Views: content/settings/bench_views.json. Times in ms: GPU = the per-pass timers summed, CPU = wall-clock frame time (no vsync), render CPU = render() alone, probes / frame = GI probe captures per frame (mean); median / p95 / p99 over the measured frames.

| Map | View | Preset | GPU med | GPU p95 | GPU p99 | CPU med | CPU p95 | CPU p99 | render CPU | Probes / frame | Draws | Tris (M) | Top passes (median ms) |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| blackwater_thaw | orchard_approach | Medium | 25.84 | 29.30 | 29.93 | 26.25 | 29.67 | 30.32 | 3.75 | 1.00 | 498 | 7.8 | terrain 13.33, models 5.92, opaque 2.95 |
| blackwater_thaw | creek_bend | Medium | 26.58 | 29.74 | 31.11 | 26.78 | 30.51 | 34.16 | 3.66 | 1.00 | 435 | 6.1 | terrain 14.85, models 6.52, opaque 2.16 |
| blackwater_thaw | wall_breach | Medium | 26.65 | 28.90 | 29.93 | 26.73 | 33.10 | 48.58 | 3.60 | 1.00 | 347 | 6.7 | terrain 14.66, models 6.41, opaque 1.86 |
| blackwater_thaw | service_passage | Medium | 27.35 | 29.50 | 30.39 | 27.44 | 33.24 | 43.22 | 3.49 | 1.00 | 349 | 5.2 | terrain 15.76, models 5.51, opaque 2.35 |
| blackwater_thaw | workshop_yard | Medium | 22.16 | 24.53 | 25.17 | 22.29 | 25.34 | 27.66 | 3.51 | 1.00 | 410 | 3.4 | terrain 13.35, models 2.80, opaque 2.25 |
| blackwater_thaw | quarry_overlook | Medium | 22.17 | 25.09 | 25.84 | 22.15 | 26.34 | 31.60 | 3.54 | 1.00 | 451 | 4.8 | terrain 13.42, models 3.26, opaque 1.78 |
| blackwater_thaw | slice_passage | Medium | 36.53 | 39.68 | 41.67 | 36.28 | 40.09 | 41.75 | 4.41 | 1.00 | 600 | 6.4 | terrain 14.86, opaque 10.05, models 7.14 |
| blackwater_thaw | slice_bridge | Medium | 27.88 | 31.05 | 31.70 | 27.94 | 31.16 | 32.19 | 4.23 | 1.00 | 438 | 6.6 | terrain 15.07, models 7.00, opaque 2.04 |
| blackwater_thaw | overlook_improved | Medium | 23.67 | 25.94 | 26.44 | 23.79 | 27.47 | 33.56 | 4.04 | 1.00 | 402 | 4.5 | terrain 14.28, models 3.69, opaque 2.38 |
| blackwater_thaw | orchard_approach | High | 65.62 | 68.44 | 68.89 | 65.61 | 68.77 | 68.89 | 4.59 | 8.00 | 908 | 23.1 | terrain 32.87, models 14.09, opaque 7.61 |
| blackwater_thaw | creek_bend | High | 68.59 | 72.01 | 72.59 | 68.80 | 72.60 | 77.40 | 5.21 | 8.00 | 611 | 7.3 | terrain 38.11, models 14.05, opaque 6.60 |
| blackwater_thaw | wall_breach | High | 66.76 | 69.65 | 69.76 | 66.49 | 70.00 | 71.14 | 5.38 | 8.00 | 457 | 8.2 | terrain 34.85, models 13.08, opaque 6.15 |
| blackwater_thaw | service_passage | High | 66.96 | 71.78 | 72.60 | 66.94 | 71.32 | 72.49 | 4.26 | 7.95 | 412 | 6.6 | terrain 36.15, models 12.08, opaque 6.81 |
| blackwater_thaw | workshop_yard | High | 52.71 | 57.52 | 58.34 | 52.60 | 57.83 | 59.79 | 4.50 | 7.92 | 922 | 17.1 | terrain 30.60, opaque 6.34, models 5.60 |
| blackwater_thaw | quarry_overlook | High | 57.23 | 58.87 | 59.22 | 56.92 | 59.02 | 60.51 | 4.50 | 7.96 | 873 | 22.9 | terrain 32.47, models 7.18, opaque 5.67 |
| blackwater_thaw | slice_passage | High | 93.16 | 100.73 | 104.86 | 93.33 | 102.31 | 104.86 | 5.37 | 8.00 | 741 | 7.6 | terrain 37.81, opaque 25.97, models 17.06 |
| blackwater_thaw | slice_bridge | High | 68.77 | 77.48 | 79.29 | 68.99 | 77.89 | 79.95 | 5.38 | 8.00 | 986 | 23.3 | terrain 36.78, models 14.50, opaque 6.55 |
| blackwater_thaw | overlook_improved | High | 57.24 | 65.89 | 66.08 | 57.58 | 65.21 | 66.38 | 4.67 | 8.00 | 949 | 21.6 | terrain 33.09, models 7.11, opaque 7.06 |
