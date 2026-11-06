# W2 visual fixes (MP & Creative update: sights 8, thumbnails 9.4, bunkers 10). Kept out of the main source list so
# the merge touches CMakeLists.txt by one line only.
target_sources(NormandyMapmaker PRIVATE src/sight_overlay.cpp src/thumbs.cpp src/selftest_visuals.cpp)
