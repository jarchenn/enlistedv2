# Natural ground dressing (docs/nature.md): procedural boulders / outcrops, flower / tall-grass / stone patches,
# and the --selftest-nature suite.
target_sources(NormandyMapmaker PRIVATE src/nature.cpp src/selftest_nature.cpp src/foundation.cpp)
# V9 auto paths between houses (Paths > Connect houses) and the --selftest-autopath suite
target_sources(NormandyMapmaker PRIVATE src/autopath.cpp src/selftest_autopath.cpp)
