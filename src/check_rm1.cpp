// RM1 --check-rm1: the removed fighter enemy types stay removed. A local zombie_local.json that still lists "fighter" /
// "fighter2" loads (its other types load, the fighters are skipped with a log line) and nothing spawns as them; a saved
// setup naming the two retired AI type slots comes back with those AI as plain zombies.
#include "selftest.h"
#include "scenario.h"
#include "enemy_types.h"

bool rm1SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-rm1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  int fails = 0;
  auto chk = [&](bool ok, const std::string& w) { logf("[rm1] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; };
  chk(enemyTypeIndex("fighter") < 0 && enemyTypeIndex("fighter2") < 0, "no fighter enemy type loaded");
  chk(enemyTypeIndex("rm1_probe") >= 0, "the local file's other type (rm1_probe) loaded beside the skipped fighters");
  json rj = ScenarioRules{}.toJson();
  rj["aiTypes"] = {(1 << 4) | (1 << 6), 1};
  std::vector<int> c(kAiTypeCount, 0); c[4] = 5; c[6] = 3; rj["aiCount"] = {c, std::vector<int>(kAiTypeCount, 0)};
  ScenarioRules r = ScenarioRules::fromJson(rj);
  chk(!(r.aiTypes[0] & ((1 << 4) | (1 << 6))) && (r.aiTypes[0] & aiBit(AiType::Zombies)) && r.aiCount[0][(int)AiType::Zombies] == 8 && r.aiCount[0][4] == 0 && r.aiCount[0][6] == 0,
      "a saved setup with the retired types (5 + 3 AI) loads as 8 plain zombies (types " + std::to_string(r.aiTypes[0]) + ", zombies " + std::to_string(r.aiCount[0][(int)AiType::Zombies]) + ")");
  for (int k = 0; k < kAiTypeCount; ++k) if (aiTypeRetired(k)) chk(std::string(aiTypeName((AiType)k)) == "(retired)", "slot " + std::to_string(k) + " is retired");
  logf("[rm1] done: %d failures", fails);
  a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
  return true;
}
