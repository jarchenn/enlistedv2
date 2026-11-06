# W3 editor & terrain sources (SPEC 9.1-9.3: map size, chunked terrain, terrain cuts, editor camera, overview map,
# bookmarks, drop-in explorer). Kept out of the main source list so merges touch CMakeLists.txt by one line only.
target_sources(NormandyMapmaker PRIVATE
  src/terrain_chunks.cpp src/terrain_cuts.cpp src/editor_nav.cpp src/dropin.cpp src/selftest_editor.cpp)
