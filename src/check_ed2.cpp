// ED2 --check-ed2: the stat overrides (overrides.h) change what loads and a reset restores it; then one capture of the
// Guns and Vehicles screens (selftest_out/ed2_guns.png, ed2_vehicles.png). The user's own override files are kept and
// put back afterwards.
#include "selftest.h"
#include "overrides.h"
#include "damage_model.h"
#include "mainmenu.h"
#include <fstream>

namespace {
int phase = 0, fails = 0; float t = 0;
bool chk(bool ok, const std::string& w) { logf("[ed2] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; return ok; }
fs::path out(const char* n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / n; }
std::string slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
}

bool ed2SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-ed2") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  t += dt;
  static json keepW, keepV; static std::string shippedW, shippedV;
  auto stats = [&](float& rpm, float& near, float& back, float& rel, float& mg) {
    int g = a.weaponIndex("m1_garand"); rpm = g >= 0 ? a.weaponDefs[g]->rpm : -1; back = g >= 0 ? a.weaponDefs[g]->vmBack : -1;
    near = dmRow("m1_garand").nearDmg;
    auto it = a.tanks.defs.find("tiger_i"); rel = it != a.tanks.defs.end() ? it->second->reloadTime : -1; mg = it != a.tanks.defs.end() ? it->second->mgRpm : -1;
  };
  if (phase == 0) {
    keepW = weaponOverrides(); keepV = vehicleOverrides();
    shippedW = slurp(contentDir() / "weapons" / "m1_garand.json"); shippedV = slurp(contentDir() / "vehicles" / "tiger_i.json");
    weaponOverrides() = json::object(); vehicleOverrides() = json::object(); saveOverrides(); a.reloadGameplayDefs();
    float r0, n0, b0, l0, m0; stats(r0, n0, b0, l0, m0);
    weaponOverrides()["m1_garand"] = {{"rpm", 999}, {"_damage", {{"near", 77}}}, {"_handling", {{"back_m", 0.2}}}};
    vehicleOverrides()["tiger_i"] = {{"ammunition", {{"reloadTime", 3.3}}}, {"roofMg", {{"rpm", 1111}}}};
    std::string e; chk(saveOverrides(&e), "overrides saved " + e);
    a.reloadGameplayDefs();
    float r1, n1, b1, l1, m1; stats(r1, n1, b1, l1, m1);
    char b[300]; std::snprintf(b, sizeof b, "override applied: Garand rpm %.0f -> %.0f, damage near %.0f -> %.0f, visual back %.3f -> %.3f; Tiger reload %.1f -> %.1f s, roof MG %.0f -> %.0f rpm", r0, r1, n0, n1, b0, b1, l0, l1, m0, m1);
    chk(r1 == 999 && n1 == 77 && std::fabs(b1 - 0.2f) < 1e-4f && std::fabs(l1 - 3.3f) < 1e-4f && m1 == 1111, b);
    chk(slurp(contentDir() / "weapons" / "m1_garand.json") == shippedW && slurp(contentDir() / "vehicles" / "tiger_i.json") == shippedV, "the shipped files are untouched");
    std::error_code ec; chk(fs::exists(exeDir() / "overrides" / "weapons.json", ec) && fs::exists(exeDir() / "overrides" / "vehicles.json", ec), "overrides written to <exe>/overrides/{weapons,vehicles}.json");
    weaponOverrides().erase("m1_garand"); vehicleOverrides().erase("tiger_i"); saveOverrides(); a.reloadGameplayDefs();
    float r2, n2, b2, l2, m2; stats(r2, n2, b2, l2, m2);
    std::snprintf(b, sizeof b, "reset restores: Garand rpm %.0f, near %.0f, back %.3f; Tiger reload %.1f s, roof MG %.0f rpm", r2, n2, b2, l2, m2);
    chk(r2 == r0 && n2 == n0 && b2 == b0 && l2 == l0 && m2 == m0 && !fs::exists(exeDir() / "overrides" / "weapons.json", ec), b);
    // a visible override for the capture, then the screens
    weaponOverrides()["m1_garand"] = {{"rpm", 520}}; saveOverrides();
    mainMenuOpen(a); mainMenuGo(a, 4);
    phase = 1; t = 0; return true;
  }
  if (phase == 1) {
    if (t > 1.5f && t < 1.5f + dt * 1.5f) a.uiShot = out("ed2_guns.png");
    if (t > 2.0f && t < 2.0f + dt * 1.5f) mainMenuGo(a, 5);
    if (t > 3.6f && t < 3.6f + dt * 1.5f) a.uiShot = out("ed2_vehicles.png");
    if (t > 4.0f) {
      weaponOverrides() = keepW; vehicleOverrides() = keepV; saveOverrides();   // the user's own overrides back
      logf("[ed2] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
    }
  }
  return true;
}
