# V8 terrain tools (docs/terrain-tools.md): sculpt brush, Generate hills, Beautify (scatter, outer ring, decor buildings)
target_sources(NormandyMapmaker PRIVATE src/terrain_brush.cpp src/decor.cpp src/selftest_terrain.cpp)   # UI2: Beautify removed; decor.cpp draws what maps saved
