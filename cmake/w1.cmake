# W1 gameplay rules (MP & Creative update): spawn protection and fall damage (rules.cpp), the per-weapon damage model
# (damage_model.cpp) and their self-test suite (--selftest-rules). One include line in CMakeLists.txt.
target_sources(NormandyMapmaker PRIVATE src/rules.cpp src/damage_model.cpp src/selftest_rules.cpp)
