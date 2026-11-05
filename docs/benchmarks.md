# Benchmarks

**Reference PC:** AMD Ryzen 7 5800X (8 cores), NVIDIA GeForce RTX 2080 Ti, driver 610.88 (OpenGL 4.5.0), 32 GB RAM, Windows 11 Pro. Resolution 1920×1080, vsync off, single quality level: 4096² shadow map, 2048² sky-occlusion map, HDR RGBA16F.

Run with `NormandyMapmaker.exe --benchmark`. Raw output is in [benchmark.json](benchmark.json).

| Scene | Frames | Avg ms | p95 ms | Max ms | Notes |
|---|---|---|---|---|---|
| Dense settlement flyover (30 furnished structures, rotated copies) | 709 | 1.91 | 2.21 | 3.39 | GPU 0.88 ms, render CPU 1.55 ms, 2550 draws, 640k tris |
| Walk through a furnished farmhouse in that settlement | 2035 | 2.80 | 2.92 | 128.7 | The max is the frame where a 1080p PNG screenshot is encoded on the main thread |

Working set is about 107 MB.

The target is 60 FPS (16.7 ms), which leaves a lot of headroom. Two parts of the spec's benchmarks can't run yet because those assets don't exist:

- The settlement has no vegetation, where the spec asks for 1,000 vegetation instances.
- The *forest flight* and *beach-to-interior* scenes need the forest and beach templates.
