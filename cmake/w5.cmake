# W5 default maps (MP & Creative update, SPEC 3): authored-template runtime (heightmap, template content, previews,
# Play map picker, decorator cull) and the --selftest-maps suite. Map content is built by tools/mapgen.
target_sources(NormandyMapmaker PRIVATE src/w5_maps.cpp src/selftest_maps.cpp)
# the suite checks docs/asset-usage-default-maps.md in the source tree
set_source_files_properties(src/selftest_maps.cpp PROPERTIES COMPILE_DEFINITIONS "W5_REPO_DIR=\"${CMAKE_SOURCE_DIR}\"")
