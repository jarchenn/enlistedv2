#pragma once
// W6b AI tuning harness (AI_SPEC 12.2-12.5): selftest_ai_harness.cpp. Thresholds: content/ai/test_thresholds.json.
//   --ai-harness <course|rebuild|soak> [--map m1,m2,...] [--minutes f] [--bots n] [--attackers n] [--respawns n]
//                [--difficulty recruit|regular|veteran] [--seed n] [--size m] [--objectives n] [--structures n]
//                [--thresholds file] [--baseline file] [--save-baseline file] [--save-map] [--full-think] [--tag s]
// Maps: village, course, random[:seed[:size]], a map name in content/examples or maps/, or a .nmap.json path.
// Writes selftest_out/aiharness_<suite>_<map>.json / .txt and report_aiharness_<suite>.txt; exit code 1 on a miss.
// The whole set on a list of maps: tools/run_ai_harness.sh.
#include "common.h"
class App;
bool aiHarnessArg(const std::string& a, int argc, char** argv, int& k, bool& runSelf);
// large blank map with random structures, hedgerow and wire lines with gaps, craters and a scenario of n objectives
bool buildLargeRandomMap(App& app, uint32_t seed, float size, int objectives, int structures, std::string& err);
