#pragma once
// Tiny check / report helper for the multiplayer + AI suites. Writes selftest_out/report_<name>.txt in the same format
// as the other suites (last line "N passed, M failed"), so tools/run_all_tests.sh can tally it.
#include "common.h"
#include <fstream>

struct MpTest {
  std::string name; std::vector<std::string> lines; int fails = 0, passes = 0;
  explicit MpTest(std::string n) : name(std::move(n)) {}
  bool check(bool ok, const std::string& what) {
    lines.push_back(std::string(ok ? "PASS  " : "FAIL  ") + what);
    logf("[%s] %s %s", name.c_str(), ok ? "PASS" : "FAIL", what.c_str());
    (ok ? passes : fails)++;
    return ok;
  }
  void note(const std::string& s) { lines.push_back("      " + s); }
  int finish() {
    fs::path out = exeDir() / "selftest_out";
    std::error_code ec; fs::create_directories(out, ec);
    std::ofstream f(out / ("report_" + name + ".txt"));
    f << "Normandy Mapmaker self-test: " << name << "\n\n";
    for (auto& l : lines) f << l << "\n";
    f << "\n" << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
  }
};

int runMatchSelfTest();   // selftest_match.cpp (headless)
