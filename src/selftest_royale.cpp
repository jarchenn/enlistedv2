// --selftest-royale (headless): BR2's loot rules. BR1 adds its storm / match checks to the same suite.
#include "loot.h"
#include "app.h"
#include "mptest.h"

int runRoyaleSelfTest() {
  MpTest T("royale");

  // ---- carry capacity: an overweight pick-up is refused
  {
    Kit k; int box = ammoBox(AM_762);
    k.ammo[AM_762] = box * 9;   // 9 boxes x 10 = 90 of 100
    float w = kitWeight(nullptr, k, nullptr);
    bool fitsOne = w + 10 <= kitCapacity(k), fitsTwo = w + 20 <= kitCapacity(k);
    char b[160]; std::snprintf(b, sizeof b, "no backpack: capacity %.0f, 9 ammo boxes weigh %.0f; one more box fits (%d), two do not (%d)", kitCapacity(k), w, fitsOne, fitsTwo);
    T.check(kitCapacity(k) == 100 && std::fabs(w - 90) < 0.01f && fitsOne && !fitsTwo, b);
    k.backpack = 1; T.check(kitCapacity(k) == 200, "backpack: capacity 200");
    k.backpack = 2; T.check(kitCapacity(k) == 300, "military backpack: capacity 300");
  }
  // ---- armour: 30 / 40 / 55 % on the covered zone; the absorbed damage wears it until it breaks
  {
    bool ok = true; char b[200]; std::string s;
    for (int lvl = 1; lvl <= 3; ++lvl) {
      Kit k; k.helmet = lvl; k.helmetDur = 1000; k.vest = lvl; k.vestDur = 1000;
      float h = armorAbsorb(k, true, 100), v = armorAbsorb(k, false, 100);
      float want = 100 * (1 - (lvl == 1 ? 0.30f : lvl == 2 ? 0.40f : 0.55f));
      ok &= std::fabs(h - want) < 0.01f && std::fabs(v - want) < 0.01f;
      std::snprintf(b, sizeof b, " L%d head %.0f torso %.0f", lvl, h, v); s += b;
    }
    T.check(ok, "100 damage through armour (want 70 / 60 / 45):" + s);
    Kit k; k.helmet = 1; k.helmetDur = 50;
    float a = armorAbsorb(k, true, 100);   // absorbs 30 -> 20 left
    float b2 = armorAbsorb(k, true, 100);  // absorbs 30 -> breaks
    float c = armorAbsorb(k, true, 100);   // nothing left
    std::snprintf(b, sizeof b, "durability: a level-1 helmet with 50 takes %.0f, %.0f, then %.0f through (broken: level %d)", a, b2, c, k.helmet);
    T.check(a == 70 && b2 == 70 && c == 100 && k.helmet == 0, b);
    Kit n; T.check(armorAbsorb(n, true, 100) == 100, "no armour: full damage");
  }
  // ---- healing: a bandage stops at 75, a medkit goes to 100; spent only when finished
  {
    Kit k; k.items["br_bandage"] = 2; k.items["br_medkit"] = 1;
    float hp = 60;
    bool started = healStart(k, 1, hp);
    for (int i = 0; i < 400; ++i) hp = healStep(k, hp, 0.01f);
    char b[200]; std::snprintf(b, sizeof b, "bandage from 60: %.1f after 4 s (want 75, not 85); bandages left %d", hp, k.items["br_bandage"]);
    T.check(started && std::fabs(hp - 75) < 0.01f && k.items["br_bandage"] == 1, b);
    std::string why;
    T.check(!healStart(k, 1, hp, &why), "a second bandage at 75 is refused (" + why + ")");
    T.check(healStart(k, 2, hp), "a medkit starts at 75");
    for (int i = 0; i < 300; ++i) hp = healStep(k, hp, 0.01f);
    T.check(k.healKind == 2 && hp < 100, "after 3 s the medkit is still working (7 s)");
    for (int i = 0; i < 500; ++i) hp = healStep(k, hp, 0.01f);
    std::snprintf(b, sizeof b, "medkit ends at %.1f; medkits left %d", hp, k.items.count("br_medkit") ? k.items["br_medkit"] : 0);
    T.check(std::fabs(hp - 100) < 0.01f && !k.items.count("br_medkit"), b);
  }
  // ---- data: every ammo type has a box; the item table loaded
  {
    bool ok = true; for (int t = 0; t < AM_COUNT; ++t) ok &= ammoBox(t) > 0;
    T.check(ok && lootItem("br_bandage") && lootItem("br_helmet_3") && lootItem("br_helmet_3")->military && lootItem("br_sight_10x"), "loot.json: seven ammo boxes, items, level 3 military only");
  }
  return T.finish();
}
