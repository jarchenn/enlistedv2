# V6 (docs/plan-visual-physics-overhaul.md): seamless snapping, crush & flatten under buildings, snap self-test.
# Editor gestures and zoom buttons live in editor_nav.cpp (cmake/w3.cmake).
target_sources(NormandyMapmaker PRIVATE src/snap.cpp src/selftest_snap.cpp)
