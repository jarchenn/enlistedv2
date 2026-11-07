# V1 visual overhaul (docs/rendering.md#facades-walls-water): the water pass and the --selftest-render suite
# (facade shimmer at 20 / 50 / 100 m, rough walls, water assets and rendering).
target_sources(NormandyMapmaker PRIVATE src/water.cpp src/selftest_render.cpp)
