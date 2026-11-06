# W6b AI tuning harness (AI_SPEC 12.2-12.5): --ai-harness <course|rebuild|soak>, thresholds content/ai/test_thresholds.json,
# runner tools/run_ai_harness.sh.
target_sources(NormandyMapmaker PRIVATE src/selftest_ai_harness.cpp)
