// BR2 Battle Royale loot (loot.h): data, floor spawner, pick-up, pooled ammo, armour, healing, molotov, bots.
#include "loot.h"
#include "app.h"
#include "game.h"
#include "damage_model.h"
#include "physics.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <random>
#include <algorithm>

// ---------------------------------------------------------------- data (content/settings/loot.json)
namespace {
struct LootCfg {
  std::vector<ItemDef> items;
  float capacity[3] = {100, 200, 300};
  int box[AM_COUNT] = {30, 20, 30, 30, 5, 1, 8};
  std::string ammoNames[AM_COUNT] = {"9 mm", ".45", "5.56", "7.62", ".50", "Rockets", "12 gauge"};
  float boxWeight = 10;
  float reduce[4] = {0, 0.30f, 0.40f, 0.55f}, helmetDur[4] = {0, 80, 150, 230}, vestDur[4] = {0, 200, 220, 250};
  float healAmount[3] = {0, 25, 100}, healSeconds[3] = {0, 3, 7}, healCap[3] = {0, 75, 100};
  float largeArea = 120; int smallGuns[2] = {1, 1}, largeGuns[2] = {2, 3}, boxesPerGun[2] = {1, 3}, smallItems[2] = {1, 3}, largeItems[2] = {3, 6};
  float militaryScoped = 3;
  json gunWeight = json::object();
};
const LootCfg& cfg() {
  static LootCfg c = [] {
    LootCfg c;
    std::ifstream f(contentDir() / "settings" / "loot.json");
    json j; try { if (f) j = json::parse(f); } catch (std::exception& e) { logf("loot.json: %s (defaults used)", e.what()); }
    if (!j.is_object()) return c;
    if (j.contains("capacity")) for (int k = 0; k < 3 && k < (int)j["capacity"].size(); ++k) c.capacity[k] = j["capacity"][k];
    if (j.contains("ammo")) for (int k = 0; k < AM_COUNT && k < (int)j["ammo"].size(); ++k) { c.box[k] = j["ammo"][k].value("box", c.box[k]); c.ammoNames[k] = j["ammo"][k].value("name", c.ammoNames[k]); }
    c.boxWeight = j.value("ammoBoxWeight", c.boxWeight);
    if (j.contains("armor")) for (int k = 0; k < 4; ++k) {
      const json& a = j["armor"];
      if (a.contains("reduce")) c.reduce[k] = a["reduce"][k]; if (a.contains("helmetDurability")) c.helmetDur[k] = a["helmetDurability"][k]; if (a.contains("vestDurability")) c.vestDur[k] = a["vestDurability"][k];
    }
    if (j.contains("heal")) for (int k = 1; k <= 2; ++k) {
      const json& h = j["heal"].value(k == 1 ? "bandage" : "medkit", json::object());
      c.healAmount[k] = h.value("amount", c.healAmount[k]); c.healSeconds[k] = h.value("seconds", c.healSeconds[k]); c.healCap[k] = h.value("cap", c.healCap[k]);
    }
    if (j.contains("spawn")) {
      const json& s = j["spawn"]; c.largeArea = s.value("largeArea_m2", c.largeArea); c.militaryScoped = s.value("militaryScopedWeight", c.militaryScoped);
      auto pair = [&](const char* k, int* o) { if (s.contains(k)) { o[0] = s[k][0]; o[1] = s[k][1]; } };
      pair("smallGuns", c.smallGuns); pair("largeGuns", c.largeGuns); pair("ammoBoxesPerGun", c.boxesPerGun); pair("smallItems", c.smallItems); pair("largeItems", c.largeItems);
    }
    c.gunWeight = j.value("gunWeight", json::object());
    static const std::map<std::string, ItemKind> kinds = {{"bandage", ItemKind::Bandage}, {"medkit", ItemKind::Medkit}, {"armor1", ItemKind::Armor1}, {"helmet", ItemKind::Helmet},
      {"vest", ItemKind::Vest}, {"backpack", ItemKind::Backpack}, {"sight", ItemKind::Sight}, {"mag", ItemKind::Mag}, {"muzzle", ItemKind::Muzzle}, {"stock", ItemKind::Stock}, {"throwable", ItemKind::Throwable}};
    for (auto& i : j.value("items", json::array())) {
      ItemDef d; d.id = i.value("id", ""); d.name = i.value("name", d.id); d.weapon = i.value("weapon", "");
      auto it = kinds.find(i.value("kind", "")); if (d.id.empty() || it == kinds.end()) continue;
      d.kind = it->second; d.level = i.value("level", 0); d.value = i.value("value", 0); d.weight = i.value("weight", 0.0f); d.rarity = i.value("rarity", 1.0f); d.military = i.value("military", false);
      c.items.push_back(d);
    }
    return c;
  }();
  return c;
}

struct LootState {
  std::vector<FloorItem> floor;
  std::vector<Kit> kits;
  std::vector<int> life;        // the lifeId each slot was stripped for
  uint32_t nextUid = 1;
  int poolWeapon = -1;          // lootBeforeWeapons -> lootAfterWeapons
  int target = -1;              // index into floor: what the player looks at
  bool inventory = false;
  std::vector<LootCase> cases;  // BR5: one per dead combatant that carried something
  int caseTarget = -1;          // index into cases: the case the player looks at
  uint32_t openCase = 0;        // the case window's case
  GpuMesh box;                  // white unit box, tinted per kind
} gL;

bool contains(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }
uint32_t gRng = 12345u;
float frand() { gRng = gRng * 1664525u + 1013904223u; return (gRng >> 8) * (1.0f / 16777216.0f); }
int irange(int a, int b) { return a + (int)(frand() * (b - a + 1) * 0.9999f); }
}   // namespace

const ItemDef* lootItem(const std::string& id) { for (auto& d : cfg().items) if (d.id == id) return &d; return nullptr; }
const char* ammoName(int t) { return t >= 0 && t < AM_COUNT ? cfg().ammoNames[t].c_str() : "-"; }
int ammoBox(int t) { return t >= 0 && t < AM_COUNT ? cfg().box[t] : 0; }

int ammoTypeOf(const WeaponDef& d) {
  const std::string& c = d.caliber;
  if (d.action == "launcher") return AM_ROCKET;
  if (contains(c, "gauge")) return AM_12GA;
  if (contains(c, ".50") || contains(c, "12.7")) return AM_50;
  if (contains(c, "9x19") || contains(c, "4.6x30") || contains(c, ".22") || contains(c, "7.65")) return AM_9MM;
  if (contains(c, ".45") || contains(c, "ACP")) return AM_45;
  if (contains(c, "5.56") || contains(c, "5.45") || contains(c, "7.92x33") || contains(c, "Carbine")) return AM_556;   // plan: Kurz and .30 Carbine count as 5.56
  if (contains(c, "7.62") || contains(c, "7.92x57") || contains(c, ".30-06")) return AM_762;
  return AM_NONE;
}
bool lootIsPistol(const WeaponDef& d) { int t = ammoTypeOf(d); return d.action == "semi" && d.feed == "magazine" && (t == AM_9MM || t == AM_45) && d.cap <= 16; }
static bool isShotgun(const WeaponDef& d) { return ammoTypeOf(d) == AM_12GA; }
static bool isSmg(const WeaponDef& d) { int t = ammoTypeOf(d); return d.action == "auto" && (t == AM_9MM || t == AM_45); }
int lootMagSize(const WeaponDef& d, const WeaponState& s) {
  if (!s.att[ATT_MAG]) return d.cap;
  return (int)std::lround(d.cap * (lootIsPistol(d) || isSmg(d) ? 2.0f : 1.5f));
}
int lootSight(const WeaponDef& d, const WeaponState& s) { return lootBuiltInAcog(d, s) ? 2 : s.att[ATT_SIGHT]; }
bool lootSightFits(const WeaponDef& d, int sight) {
  bool pistol = lootIsPistol(d), sniper = d.wclass == WC_SNIPER, shotgun = isShotgun(d);
  switch (sight) {
    case 1: return !pistol && !sniper;               // red dot: SMG, rifle, shotgun
    case 2: return !pistol;                          // ACOG: all but pistols
    case 3: return !pistol && !shotgun;              // 4x
    case 4: return sniper;                           // 10x: snipers only
    default: return false;
  }
}
bool lootAttachFits(const WeaponDef& d, const ItemDef& it) {
  if (d.equipment() || d.action == "launcher" || d.flame.on || ammoTypeOf(d) == AM_NONE) return false;
  switch (it.kind) {
    case ItemKind::Sight: return lootSightFits(d, it.value);
    case ItemKind::Mag: return d.feed == "magazine";
    case ItemKind::Muzzle: return !isShotgun(d);
    case ItemKind::Stock: return d.wclass == WC_RIFLE || d.wclass == WC_SNIPER;   // recoil pad: rifles and snipers
    default: return false;
  }
}

// ---------------------------------------------------------------- pure rules
float kitCapacity(const Kit& k) { return cfg().capacity[std::clamp(k.backpack, 0, 2)]; }
float kitWeight(const App* app, const Kit& k, const std::vector<int>* carried) {
  const LootCfg& c = cfg();
  float w = 0;
  for (int t = 0; t < AM_COUNT; ++t) w += k.ammo[t] * c.boxWeight / std::max(1, c.box[t]);
  for (auto& [id, n] : k.items) if (const ItemDef* d = lootItem(id)) w += d->weight * n;
  if (app && carried) for (int wi : *carried) {
    const WeaponDef& d = *app->weaponDefs[wi];
    if (!d.grenade.on) continue;
    auto it = app->wstate.find(d.id); if (it == app->wstate.end()) continue;
    for (auto& id : c.items) if (id.kind == ItemKind::Throwable && id.weapon == d.id) w += id.weight * (it->second.loaded + (int)it->second.containers.size());
  }
  return w;
}
float armorAbsorb(Kit& k, bool head, float dmg) {
  int& lvl = head ? k.helmet : k.vest; float& dur = head ? k.helmetDur : k.vestDur;
  if (lvl <= 0 || dmg <= 0) return dmg;
  float absorbed = dmg * cfg().reduce[std::clamp(lvl, 0, 3)];
  dur -= absorbed;
  if (dur <= 0) { dur = 0; lvl = 0; }   // broken: this hit was still softened
  return dmg - absorbed;
}
bool healStart(Kit& k, int kind, float hp, std::string* why) {
  const LootCfg& c = cfg();
  const char* id = kind == 1 ? "br_bandage" : "br_medkit";
  auto it = k.items.find(id);
  if (it == k.items.end() || it->second <= 0) { if (why) *why = kind == 1 ? "No bandages" : "No medkits"; return false; }
  if (hp >= c.healCap[kind] - 0.01f) { if (why) *why = kind == 1 ? "Bandages heal only to 75" : "Health is full"; return false; }
  k.healKind = kind; k.healT = 0; k.healFrom = hp; k.healTo = std::min(c.healCap[kind], hp + c.healAmount[kind]);
  return true;
}
float healStep(Kit& k, float hp, float dt) {
  if (!k.healKind) return hp;
  const LootCfg& c = cfg();
  k.healT += dt;
  float u = std::min(1.0f, k.healT / std::max(0.1f, c.healSeconds[k.healKind]));
  float h = std::max(hp, k.healFrom + (k.healTo - k.healFrom) * u);
  if (u >= 1) { auto& n = k.items[k.healKind == 1 ? "br_bandage" : "br_medkit"]; if (--n <= 0) k.items.erase(k.healKind == 1 ? "br_bandage" : "br_medkit"); k.healKind = 0; }
  return h;
}

// ---------------------------------------------------------------- state
bool lootActive() { return gGame && gGame->active && !gGame->isClient && gGame->match.mode() == GameMode::BattleRoyale; }
Kit* kitOf(int slot) { if (slot < 0) return nullptr; if ((int)gL.kits.size() <= slot) gL.kits.resize(slot + 1); return &gL.kits[slot]; }
std::vector<FloorItem>& lootFloor() { return gL.floor; }
bool lootInventoryOpen() { return lootActive() && (gL.inventory || gL.openCase); }
std::vector<LootCase>& lootCases() { return gL.cases; }
void lootOpenCase(uint32_t uid) { gL.openCase = uid; if (uid) gL.inventory = false; }
static LootCase* findCase(uint32_t uid) { for (auto& c : gL.cases) if (c.uid == uid) return &c; return nullptr; }
void lootSetInventory(bool open) { gL.inventory = open; }

static void syncWorn(Game& G, int slot) {
  if (slot < 0 || slot >= (int)G.sold.size()) return;
  Kit& k = *kitOf(slot); Soldier& s = G.sold[slot];
  s.helmetLvl = k.helmet; s.vestLvl = k.vest; s.backpack = k.backpack;
}
// an item set aside: into a case (`into`) or onto the floor around `at`
static void stash(std::vector<FloorItem>* into, vec3 at, FloorItem f) {
  f.uid = gL.nextUid++;
  if (into) { f.pos = at; into->push_back(std::move(f)); return; }
  float a = frand() * 6.2831853f, r = 0.3f + 0.5f * frand();
  f.pos = at + vec3(std::cos(a) * r, 0.02f, std::sin(a) * r); f.yaw = frand() * 360;
  gL.floor.push_back(std::move(f));
}
static FloorItem& dropAt(vec3 p, const std::string& id, int count) {
  FloorItem f; f.id = id; f.count = count; stash(nullptr, p, f); return gL.floor.back();
}
void lootDropItem(vec3 at, const std::string& id, int count) { dropAt(at, id, count); }
// what is left of item `uid` after a take: -1 refused (unchanged), 0 all taken, n > 0 left
static void settle(std::vector<FloorItem>& v, uint32_t uid, int left) {
  if (left < 0) return;
  auto it = std::find_if(v.begin(), v.end(), [&](const FloorItem& x) { return x.uid == uid; }); if (it == v.end()) return;
  if (left == 0) v.erase(it); else it->count = left;
}
static bool inReach(vec3 a, vec3 b) { vec3 d = a - b; return d.x * d.x + d.z * d.z < 1.8f * 1.8f && std::abs(d.y) < 1.6f; }

// ---------------------------------------------------------------- spawner
static std::vector<int> gunPool(App& app, bool modern) {
  std::vector<int> v;
  for (size_t k = 0; k < app.weaponDefs.size(); ++k) {
    const WeaponDef& d = *app.weaponDefs[k];
    if (d.equipment() || d.flame.on || d.locked || d.action == "heavy" || d.action == "tool" || ammoTypeOf(d) == AM_NONE) continue;
    if (d.hasScope && d.wclass != WC_SNIPER) continue;   // plan: guns without a built-in optic, plus sniper rifles
    if (d.modern != modern) continue;
    v.push_back((int)k);
  }
  return v;
}
static float gunWeight(const WeaponDef& d, bool military) {
  const json& gw = cfg().gunWeight;
  auto w = [&](const char* k, float def) { return gw.value(k, def); };
  float x = d.action == "launcher" ? w("launcher", 0.5f) : ammoTypeOf(d) == AM_50 ? w("fifty", 0.5f) : d.wclass == WC_SNIPER ? w("sniper", 1) : lootIsPistol(d) ? w("pistol", 4)
          : isSmg(d) ? w("smg", 4) : isShotgun(d) ? w("shotgun", 3) : d.wclass == WC_LMG ? w("lmg", 1) : d.wclass == WC_AUTO ? w("auto", 3) : w("rifle", 3);
  if (military && (d.wclass == WC_SNIPER || ammoTypeOf(d) == AM_50)) x *= cfg().militaryScoped;
  return x;
}
template <class T, class W> static int pickWeighted(const std::vector<T>& v, W weight) {
  float sum = 0; for (auto& x : v) sum += weight(x);
  if (sum <= 0) return -1;
  float r = frand() * sum;
  for (size_t k = 0; k < v.size(); ++k) { r -= weight(v[k]); if (r <= 0) return (int)k; }
  return (int)v.size() - 1;
}

// floor points inside an enterable building: its nav rooms (asset space), else the footprint's middle at ground level
static std::vector<vec3> floorSpots(App& app, const Instance& i, const AssetDef& a) {
  std::vector<vec3> out;
  quat q = yawQuat(i.yaw);
  std::vector<std::pair<vec3, vec3>> boxes;
  for (auto& r : a.navMeta.rooms) boxes.push_back({r.min, r.max});
  if (boxes.empty()) boxes.push_back({vec3(-a.footprint.x * 0.3f, 0, -a.footprint.y * 0.3f), vec3(a.footprint.x * 0.3f, 2.5f, a.footprint.y * 0.3f)});
  for (int n = 0; n < 24; ++n) {
    auto& b = boxes[(size_t)(frand() * boxes.size()) % boxes.size()];
    vec3 l(glm::mix(b.first.x + 0.4f, b.second.x - 0.4f, frand()), b.first.y + 1.2f, glm::mix(b.first.z + 0.4f, b.second.z - 0.4f, frand()));
    vec3 w = i.pos + q * l;
    Physics::Hit h;
    if (!Physics::castRayEx(w, vec3(0, -1, 0), 2.5f, h)) continue;
    out.push_back(h.pos + vec3(0, 0.02f, 0));
  }
  return out;
}

void lootBegin(Game& G) {
  gL.floor.clear(); gL.kits.clear(); gL.life.clear(); gL.target = -1; gL.inventory = false; gL.poolWeapon = -1;
  gL.cases.clear(); gL.caseTarget = -1; gL.openCase = 0;
  if (G.isClient || G.match.mode() != GameMode::BattleRoyale) return;
  App& app = G.app;
  const LootCfg& c = cfg();
  gRng = 0x9E3779B9u ^ (uint32_t)app.doc.instances.size() * 2654435761u ^ (uint32_t)(app.now * 1000);
  std::vector<int> pool = gunPool(app, G.match.rules.lootModern);
  int guns = 0, items = 0, houses = 0;
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !it->second.asset) continue;
    const AssetDef& a = *it->second.asset;
    if (!a.enterable) continue;
    json tag = i.extra.is_object() ? i.extra.value("loot", json::object()) : json::object();
    bool military = tag.value("military", false);
    std::string size = tag.value("size", std::string());
    bool large = size.empty() ? a.footprint.x * a.footprint.y > c.largeArea : size == "large";
    std::vector<vec3> spots = floorSpots(app, i, a);
    if (spots.empty()) continue;
    ++houses;
    size_t si = 0; auto spot = [&] { return spots[si++ % spots.size()]; };
    int nGuns = large ? irange(c.largeGuns[0], c.largeGuns[1]) : irange(c.smallGuns[0], c.smallGuns[1]);
    for (int g = 0; g < nGuns && !pool.empty(); ++g) {
      int k = pickWeighted(pool, [&](int w) { return gunWeight(*app.weaponDefs[w], military); });
      const WeaponDef& d = *app.weaponDefs[pool[k]];
      FloorItem& f = dropAt(spot(), "", 1);
      f.weapon = pool[k];
      // plan: snipers spawn with the ACOG: a scoped sniper's own scope is it (lootSight), nothing fitted
      f.ws.loaded = lootMagSize(d, f.ws); f.ws.seed = 1234567u + (uint32_t)f.uid * 7919u;   // one full magazine
      ++guns;
      int t = ammoTypeOf(d), nb = irange(c.boxesPerGun[0], c.boxesPerGun[1]);
      for (int b = 0; b < nb; ++b) dropAt(f.pos, "ammo:" + std::to_string(t), c.box[t]);
    }
    int nItems = large ? irange(c.largeItems[0], c.largeItems[1]) : irange(c.smallItems[0], c.smallItems[1]);
    for (int n = 0; n < nItems; ++n) {
      std::vector<const ItemDef*> v; for (auto& d : c.items) if (!d.military || military) v.push_back(&d);
      int k = pickWeighted(v, [](const ItemDef* d) { return d->rarity; });
      if (k < 0) break;
      const ItemDef& d = *v[k];
      int count = d.kind == ItemKind::Helmet || d.kind == ItemKind::Armor1 ? (int)c.helmetDur[std::max(1, d.level)] : d.kind == ItemKind::Vest ? (int)c.vestDur[d.level] : 1;
      dropAt(spot(), d.id, count); ++items;
    }
    if (nGuns == 0) { int t = irange(0, AM_12GA); dropAt(spot(), "ammo:" + std::to_string(t), c.box[t]); }   // every house has some ammo
  }
  logf("loot: %zu floor items (%d guns, %d other) in %d buildings, %s guns", gL.floor.size(), guns, items, houses, G.match.rules.lootModern ? "modern" : "WW2");
}

// ---------------------------------------------------------------- taking an item
std::string lootItemName(App& app, const FloorItem& f) {
  if (f.weapon >= 0) return app.weaponDefs[f.weapon]->name;
  if (f.id.rfind("ammo:", 0) == 0) return std::string(ammoName(std::stoi(f.id.substr(5)))) + " ammo (" + std::to_string(f.count) + ")";
  const ItemDef* d = lootItem(f.id); if (!d) return f.id;
  if (d->kind == ItemKind::Helmet || d->kind == ItemKind::Vest) return d->name + "  (" + std::to_string(f.count) + ")";   // count: durability
  return f.count > 1 && d->kind != ItemKind::Armor1 && d->kind != ItemKind::Backpack ? d->name + "  x" + std::to_string(f.count) : d->name;
}
static float unitWeight(const FloorItem& f) {   // per round / piece; armour, backpacks and guns use slots
  if (f.weapon >= 0) return 0;
  if (f.id.rfind("ammo:", 0) == 0) { int t = std::stoi(f.id.substr(5)); return cfg().boxWeight / std::max(1, cfg().box[t]); }
  const ItemDef* d = lootItem(f.id);
  return d && d->kind != ItemKind::Helmet && d->kind != ItemKind::Vest && d->kind != ItemKind::Armor1 && d->kind != ItemKind::Backpack ? d->weight : 0;
}
// wear an armour / backpack piece; the worse one it replaces goes into `into` (a case) or to the floor. false: already as good
static bool wear(Kit& k, const ItemDef& d, int dur, vec3 at, std::vector<FloorItem>* into = nullptr) {
  const LootCfg& c = cfg();
  auto out = [&](const std::string& id, int n) { FloorItem f; f.id = id; f.count = n; stash(into, at, f); };
  auto swapPiece = [&](int& lvl, float& du, int newLvl, float newDur, const char* prefix) {
    if (newLvl <= lvl) return false;
    if (lvl > 0) out(lvl == 1 ? "br_vest_1" : std::string(prefix) + std::to_string(lvl), (int)du);
    lvl = newLvl; du = newDur; return true;
  };
  switch (d.kind) {
    case ItemKind::Armor1: { bool a = k.helmet < 1, b = k.vest < 1; if (!a && !b) return false; if (a) { k.helmet = 1; k.helmetDur = c.helmetDur[1]; } if (b) { k.vest = 1; k.vestDur = c.vestDur[1]; } return true; }
    case ItemKind::Helmet: return swapPiece(k.helmet, k.helmetDur, d.level, dur > 0 ? dur : c.helmetDur[d.level], "br_helmet_");
    case ItemKind::Vest: return swapPiece(k.vest, k.vestDur, d.level, dur > 0 ? dur : c.vestDur[d.level], "br_vest_");
    case ItemKind::Backpack: { if (d.level <= k.backpack) return false; if (k.backpack > 0) out(k.backpack == 1 ? "br_backpack" : "br_backpack_mil", 1); k.backpack = d.level; return true; }
    default: return false;
  }
}

// a carried gun out of the player's hands, with its magazine and attachments (the pooled reserve stays in the kit)
static FloorItem gunOut(App& app, int weapon) {
  const WeaponDef& d = *app.weaponDefs[weapon];
  FloorItem f; f.weapon = weapon; f.ws = app.wstate[d.id]; f.ws.containers.clear(); f.ws.reload = 0;
  app.carried.erase(std::find(app.carried.begin(), app.carried.end(), weapon));
  if (app.activeWeapon == weapon) { int kn = app.weaponIndex("knife_bayonet"); app.selectWeapon(!app.carried.empty() ? app.carried[0] : kn >= 0 ? kn : 0); }
  return f;
}

// the player takes item f (from the floor or a case); returns what is left of it (settle). A gun swapped out or an
// armour piece replaced goes into `into` (the case) or to the floor; without `swap` a full gun slot refuses.
static int playerTake(App& app, Game& G, const FloorItem& f, std::string& msg, std::vector<FloorItem>* into, bool swap = true) {
  Soldier* L = G.local(); if (!L || !L->alive) return -1;
  Kit& k = *kitOf(L->slot);
  if (f.weapon >= 0) {   // a gun: 2 long guns + a pistol; a full slot swaps, the old gun goes where this one was
    const WeaponDef& d = *app.weaponDefs[f.weapon];
    if (std::find(app.carried.begin(), app.carried.end(), f.weapon) != app.carried.end()) { msg = "You already carry a " + d.name; return -1; }
    bool pistol = lootIsPistol(d);
    std::vector<int> same; for (int w : app.carried) { const WeaponDef& o = *app.weaponDefs[w]; if (!o.equipment() && lootIsPistol(o) == pistol) same.push_back(w); }
    if ((pistol && !same.empty()) || (!pistol && same.size() >= 2)) {
      if (!swap) { msg = "No free slot for the " + d.name; return -1; }
      int out = pistol ? same[0] : (std::find(same.begin(), same.end(), app.activeWeapon) != same.end() ? app.activeWeapon : same.back());
      stash(into, L->feet, gunOut(app, out));
    }
    app.carried.push_back(f.weapon); app.capCarried(app.carried);
    app.wstate[d.id] = f.ws;
    app.selectWeapon(f.weapon); app.wstate[d.id].triggerReleased = false; app.stackShownAt = app.now;
    msg = "Picked up " + d.name; return 0;
  }
  int n = f.count; float u = unitWeight(f);
  if (u > 0) {   // as many as fit
    float wt = kitWeight(&app, k, &app.carried);
    n = std::min(n, (int)std::floor((kitCapacity(k) - wt) / u + 1e-4f));
    if (n <= 0) { msg = "Not enough room (" + std::to_string((int)wt) + " / " + std::to_string((int)kitCapacity(k)) + ")"; return -1; }
  }
  int left = f.count - n;
  if (f.id.rfind("ammo:", 0) == 0) { int t = std::stoi(f.id.substr(5)); k.ammo[t] += n; msg = "+" + std::to_string(n) + " " + ammoName(t) + (left ? " (bag full)" : ""); return left; }
  const ItemDef* d = lootItem(f.id); if (!d) return 0;
  switch (d->kind) {
    case ItemKind::Helmet: case ItemKind::Vest: case ItemKind::Armor1: case ItemKind::Backpack:
      if (!wear(k, *d, f.count, L->feet, into)) { msg = "You already wear a better one"; return -1; }
      syncWorn(G, L->slot); msg = "Equipped " + d->name; return 0;
    case ItemKind::Throwable: {
      int wi = app.weaponIndex(d->weapon); if (wi < 0) { msg = d->name + " is not in this build"; return -1; }
      if (std::find(app.carried.begin(), app.carried.end(), wi) == app.carried.end()) { app.carried.push_back(wi); app.capCarried(app.carried); app.wstate[d->weapon] = WeaponState{}; }
      WeaponState& s = app.wstate[d->weapon];
      for (int q = 0; q < n; ++q) { if (s.loaded <= 0) s.loaded = 1; else s.containers.push_back(1); }
      msg = "Picked up " + d->name; return left;
    }
    case ItemKind::Sight: case ItemKind::Muzzle: case ItemKind::Mag: case ItemKind::Stock: {
      const WeaponDef& gd = *app.weaponDefs[app.activeWeapon];
      int slot = d->kind == ItemKind::Sight ? ATT_SIGHT : d->kind == ItemKind::Muzzle ? ATT_MUZZLE : d->kind == ItemKind::Mag ? ATT_MAG : ATT_STOCK;
      WeaponState& s = app.wstate[gd.id];
      int loose = n; msg = "Picked up " + d->name;
      if (lootAttachFits(gd, *d) && !s.att[slot]) { s.att[slot] = d->value; --loose; msg = d->name + " fitted to the " + gd.name; }
      if (loose > 0) k.items[d->id] += loose;
      return left;
    }
    default: k.items[d->id] += n; msg = "Picked up " + d->name; return left;
  }
}

void lootDropGun(App& app, int weapon) {
  if (!gGame || weapon < 0) return;
  Soldier* L = gGame->local(); if (!L) return;
  if (std::find(app.carried.begin(), app.carried.end(), weapon) == app.carried.end()) return;
  stash(nullptr, L->feet, gunOut(app, weapon));
}

bool lootTakeFromCase(App& app, uint32_t item, bool swap, std::string& msg) {
  LootCase* c = findCase(gL.openCase); if (!c || !gGame) return false;
  auto it = std::find_if(c->items.begin(), c->items.end(), [&](const FloorItem& x) { return x.uid == item; }); if (it == c->items.end()) return false;
  FloorItem f = *it;   // a copy: the take may add the swapped-out piece to this case
  int left = playerTake(app, *gGame, f, msg, &c->items, swap);
  settle(c->items, f.uid, left);
  return left >= 0;
}

bool lootInteract(App& app) {
  if (!lootActive()) return false;
  if (gL.openCase) { gL.openCase = 0; return true; }   // E again closes the case
  if (gL.caseTarget >= 0 && gL.caseTarget < (int)gL.cases.size()) { lootOpenCase(gL.cases[gL.caseTarget].uid); gL.caseTarget = -1; return true; }
  if (gL.target < 0 || gL.target >= (int)gL.floor.size()) return false;
  std::string msg; FloorItem f = gL.floor[gL.target];   // a copy: a swap drops onto this same floor list
  settle(gL.floor, f.uid, playerTake(app, *gGame, f, msg, nullptr));
  gL.target = -1;
  if (!msg.empty()) app.toast(msg, 2);
  return true;
}

// ---------------------------------------------------------------- death: the gear goes into a case by the body
bool lootOnDeath(Game& G, int slot) {
  if (!lootActive() || slot < 0 || slot >= (int)G.sold.size()) return false;
  App& app = G.app; Soldier& s = G.sold[slot]; Kit& k = *kitOf(slot);
  LootCase c; c.uid = gL.nextUid++; c.pos = s.feet + vec3(0, 0.02f, 0); c.yaw = s.yaw;
  c.owner = slot < (int)G.match.slots.size() && !G.match.slots[slot].name.empty() ? G.match.slots[slot].name : "Soldier " + std::to_string(slot + 1);
  auto add = [&](const std::string& id, int n) { if (n > 0) { FloorItem f; f.uid = gL.nextUid++; f.id = id; f.count = n; f.pos = c.pos; c.items.push_back(f); } };
  auto addGun = [&](int w, const WeaponState& ws) {
    const WeaponDef& d = *app.weaponDefs[w]; if (d.locked || d.equipment()) return;
    FloorItem f; f.uid = gL.nextUid++; f.weapon = w; f.ws = ws; f.ws.containers.clear(); f.ws.reload = 0; f.pos = c.pos; c.items.push_back(f);
  };
  auto throwId = [](const std::string& weaponId) { for (auto& d : cfg().items) if (d.kind == ItemKind::Throwable && d.weapon == weaponId) return d.id; return std::string(); };
  if (s.localPlayer) {
    for (int w : app.carried) {
      const WeaponDef& d = *app.weaponDefs[w]; const WeaponState& ws = app.wstate[d.id];
      if (d.grenade.on) { std::string id = throwId(d.id); if (!id.empty()) add(id, ws.loaded + (int)ws.containers.size()); }
      else addGun(w, ws);
    }
  } else {   // the gun in hand and the others it carries (lootArmBot: s.carried, their states in s.wsBy), each gun's reserve as ammo
    auto gun = [&](int w, const WeaponState& ws) { addGun(w, ws); int t = ammoTypeOf(*app.weaponDefs[w]); if (t >= 0) k.ammo[t] += ws.reserveRounds(); };
    if (s.weapon >= 0) gun(s.weapon, s.ws);
    for (int w : s.carried) if (w != s.weapon) { auto it = s.wsBy.find(w); if (it != s.wsBy.end()) gun(w, it->second); }
    add("br_frag", s.frags); add("br_smoke", s.smokes);
  }
  for (int t = 0; t < AM_COUNT; ++t) add("ammo:" + std::to_string(t), k.ammo[t]);
  if (k.helmet == 1 || k.vest == 1) add("br_vest_1", 1);   // level 1 helmet and vest are one item
  if (k.helmet >= 2) add("br_helmet_" + std::to_string(k.helmet), std::max(1, (int)k.helmetDur));
  if (k.vest >= 2) add("br_vest_" + std::to_string(k.vest), std::max(1, (int)k.vestDur));
  if (k.backpack) add(k.backpack == 1 ? "br_backpack" : "br_backpack_mil", 1);
  for (auto& [id, n] : k.items) add(id, n);
  k = Kit{}; s.frags = s.smokes = 0; syncWorn(G, slot);
  if (!c.items.empty()) gL.cases.push_back(std::move(c));
  return true;
}

// ---------------------------------------------------------------- per step
void lootArmBot(Game& G, Soldier& s, int n) {
  if (!lootActive() || s.localPlayer || s.human || s.zombie) return;
  App& app = G.app; const LootCfg& c = cfg();
  std::vector<int> pool = gunPool(app, G.match.rules.lootModern);
  s.carried.clear(); s.wsBy.clear();
  for (int g = 0; g < n && !pool.empty(); ++g) {
    int k = pickWeighted(pool, [&](int w) { return gunWeight(*app.weaponDefs[w], false); });
    if (k < 0) break;
    int w = pool[k]; pool.erase(pool.begin() + k);   // two different guns
    const WeaponDef& d = *app.weaponDefs[w];
    WeaponState ws; if (d.wclass == WC_SNIPER) ws.att[ATT_SIGHT] = 2;
    ws.loaded = lootMagSize(d, ws); ws.seed = s.rng | 1u;
    int t = ammoTypeOf(d), nb = irange(c.boxesPerGun[0], c.boxesPerGun[1]);
    for (int b = 0; b < nb && t >= 0; ++b) ws.containers.push_back(c.box[t]);
    s.carried.push_back(w);
    if (g == 0) { s.weapon = w; s.ws = ws; } else s.wsBy[w] = ws;
  }
}

static void stripForDrop(Game& G, Soldier& s) {   // a new life in Battle Royale starts with the knife only
  Kit& k = *kitOf(s.slot); k = Kit{};
  s.frags = s.smokes = 0; syncWorn(G, s.slot);
  if (s.localPlayer) {
    App& app = G.app;
    int kn = app.weaponIndex("knife_bayonet");
    app.carried.clear(); if (kn >= 0) { app.carried.push_back(kn); app.selectWeapon(kn); }
  } else { s.weapon = -1; s.ws = WeaponState{}; }
}

static int gunScore(const WeaponDef& d) { return lootIsPistol(d) ? 1 : isSmg(d) || isShotgun(d) ? 2 : d.wclass == WC_RIFLE ? 3 : 4; }
static bool botWants(Game& G, const Soldier& s, const FloorItem& f) {
  Kit& k = *kitOf(s.slot);
  if (f.weapon >= 0) return std::find(s.carried.begin(), s.carried.end(), f.weapon) == s.carried.end() && (s.weapon < 0 || (f.ws.loaded > 0 && gunScore(*G.app.weaponDefs[f.weapon]) > gunScore(*G.app.weaponDefs[s.weapon])));   // a gun, or a better one
  if (f.id.rfind("ammo:", 0) == 0) return s.weapon >= 0 && ammoTypeOf(*G.app.weaponDefs[s.weapon]) == std::stoi(f.id.substr(5)) && s.ws.reserveRounds() < 3 * ammoBox(std::stoi(f.id.substr(5)));
  const ItemDef* d = lootItem(f.id); if (!d) return false;
  switch (d->kind) {
    case ItemKind::Helmet: return d->level > k.helmet;
    case ItemKind::Vest: return d->level > k.vest;
    case ItemKind::Armor1: return k.helmet < 1 || k.vest < 1;
    case ItemKind::Backpack: return d->level > k.backpack;
    case ItemKind::Bandage: case ItemKind::Medkit: { auto it = k.items.find(d->id); return it == k.items.end() || it->second < 3; }
    default: return false;
  }
}
// a bot takes item idx of v (the floor or a case's items); what it swaps out goes back into the same place
static void botTake(Game& G, Soldier& s, std::vector<FloorItem>& v, int idx) {
  Kit& k = *kitOf(s.slot);
  std::vector<FloorItem>* into = &v == &gL.floor ? nullptr : &v;
  FloorItem f = v[idx];
  v.erase(v.begin() + idx);
  if (f.weapon >= 0) {
    int keep = 0;
    if (s.weapon >= 0) {   // the old gun stays here; its reserve goes along unless the new gun takes the same ammo
      FloorItem old; old.weapon = s.weapon; old.ws = s.ws; old.ws.containers.clear(); old.ws.reload = 0;
      int r = s.ws.reserveRounds(), ot = ammoTypeOf(*G.app.weaponDefs[s.weapon]);
      if (ot == ammoTypeOf(*G.app.weaponDefs[f.weapon])) keep = r;
      else if (r > 0 && ot >= 0) { FloorItem a; a.id = "ammo:" + std::to_string(ot); a.count = r; stash(into, s.feet, a); }
      stash(into, s.feet, old);
    }
    auto it = std::find(s.carried.begin(), s.carried.end(), s.weapon);   // the new gun takes the old one's place in the list
    if (it != s.carried.end()) *it = f.weapon; else s.carried.push_back(f.weapon);
    s.weapon = f.weapon; s.ws = f.ws; if (keep > 0) s.ws.containers.push_back(keep);
    return;
  }
  if (f.id.rfind("ammo:", 0) == 0) { s.ws.containers.push_back(f.count); return; }
  const ItemDef* d = lootItem(f.id); if (!d) return;
  if (d->kind == ItemKind::Bandage || d->kind == ItemKind::Medkit) k.items[d->id] += f.count;
  else { wear(k, *d, f.count, s.feet, into); syncWorn(G, s.slot); }
}

void lootStep(Game& G, float dt) {
  if (!lootActive()) return;
  if ((int)gL.life.size() < (int)G.sold.size()) gL.life.resize(G.sold.size(), 0);
  App& app = G.app;
  gL.cases.erase(std::remove_if(gL.cases.begin(), gL.cases.end(), [](const LootCase& c) { return c.items.empty(); }), gL.cases.end());   // emptied cases go
  if (gL.openCase && !findCase(gL.openCase)) gL.openCase = 0;
  for (auto& s : G.sold) {
    if (!s.alive) continue;
    if (gL.life[s.slot] != s.lifeId) { gL.life[s.slot] = s.lifeId; stripForDrop(G, s); }
    Kit& k = *kitOf(s.slot);
    if (k.healKind) {   // firing cancels a heal
      int shots = s.localPlayer ? (app.activeWeapon >= 0 ? app.wstate[app.weaponDefs[app.activeWeapon]->id].shotIdx : 0) : s.shots;
      if (shots != k.healShots) k.healKind = 0;
      else s.hp = healStep(k, s.hp, dt);
    }
    if (s.localPlayer || s.human) continue;
    // bots: take wanted items within reach; heal when hurt and nobody is in sight
    bool took = false;
    for (int i = 0; i < (int)gL.floor.size() && !took; ++i)
      if (inReach(gL.floor[i].pos, s.feet) && botWants(G, s, gL.floor[i])) { botTake(G, s, gL.floor, i); took = true; }
    for (auto& c : gL.cases) {   // BR5: one item per step out of a case by its feet
      if (took || !inReach(c.pos, s.feet)) continue;
      for (int i = 0; i < (int)c.items.size(); ++i) if (botWants(G, s, c.items[i])) { botTake(G, s, c.items, i); took = true; break; }
    }
    if (!k.healKind && s.hp < 60 && G.t - s.lastShot > 4) {
      bool seen = false; for (auto& m : s.br.mem) seen |= m.visible;
      if (!seen && (healStart(k, 2, s.hp) || healStart(k, 1, s.hp))) k.healShots = s.shots;
    }
  }
}

bool lootBotGoal(Game& G, Soldier& s, vec3& goal) {
  if (!lootActive() || s.zombie) return false;
  const FloorItem* best = nullptr; float bd = s.weapon < 0 ? 150.0f : 40.0f;   // without a gun nothing else matters
  for (auto& f : gL.floor) { float d = glm::length(f.pos - s.feet); if (d < bd && botWants(G, s, f)) { bd = d; best = &f; } }
  const LootCase* bc = nullptr;
  for (auto& c : gL.cases) {   // BR5: a case holding something it wants
    float d = glm::length(c.pos - s.feet); if (d >= bd) continue;
    for (auto& f : c.items) if (botWants(G, s, f)) { bd = d; bc = &c; break; }
  }
  if (bc) { goal = bc->pos; return true; }
  if (!best) return false;
  goal = best->pos; return true;
}

float lootDamage(int victim, int zone, float dmg) {
  if (!lootActive() || victim < 0) return dmg;
  bool head = zone == DZ_HEAD, torso = zone == DZ_UPPER_TORSO || zone == DZ_LOWER_TORSO || zone == DZ_NECK;
  if (!head && !torso) return dmg;
  float out = armorAbsorb(*kitOf(victim), head, dmg);
  syncWorn(*gGame, victim);
  return out;
}

// ---------------------------------------------------------------- pooled ammo around App::stepWeapons
void lootBeforeWeapons(App& app) {
  gL.poolWeapon = -1;
  if (!lootActive()) return;
  Soldier* L = gGame->local(); if (!L || !L->alive || app.activeWeapon < 0) return;
  const WeaponDef& d = *app.weaponDefs[app.activeWeapon];
  int t = ammoTypeOf(d); if (t < 0 || d.equipment()) return;
  WeaponState& s = app.wstate[d.id];
  int mag = std::max(1, lootMagSize(d, s)), n = kitOf(L->slot)->ammo[t];
  s.containers.clear();
  for (; n > 0; n -= mag) s.containers.push_back(std::min(n, mag));
  gL.poolWeapon = app.activeWeapon;
}
void lootAfterWeapons(App& app) {
  if (gL.poolWeapon < 0 || !gGame) return;
  Soldier* L = gGame->local(); if (!L) return;
  const WeaponDef& d = *app.weaponDefs[gL.poolWeapon];
  kitOf(L->slot)->ammo[ammoTypeOf(d)] = app.wstate[d.id].reserveRounds();
  gL.poolWeapon = -1;
}

// ---------------------------------------------------------------- molotov, optics
void lootMolotov(App& app, vec3 pos, int owner) {   // the bottle bursts: a ring of burning parcels lands as ground fire
  int fw = app.weaponIndex("m2_flamethrower"); if (fw < 0) fw = app.weaponIndex("flammenwerfer41");
  if (fw < 0) return;
  bool damage = !gGame || !gGame->isClient;
  for (int k = 0; k < 10; ++k) {
    float a = k * 0.6283185f;
    app.flame.spray(app, owner >= 0 ? owner : -1, fw, pos + vec3(0, 0.4f, 0), vec3(std::cos(a), -0.9f, std::sin(a)), 0.12f, damage);
  }
  app.audio.play("explosion_grenade", pos, 0.35f);
}
bool lootScope(const App& app, float& mag, int& reticle) {
  if (!lootActive() || app.activeWeapon < 0 || app.weaponDefs.empty()) return false;
  auto it = app.wstate.find(app.weaponDefs[app.activeWeapon]->id); if (it == app.wstate.end()) return false;
  switch (lootSight(*app.weaponDefs[app.activeWeapon], it->second)) {   // a sniper keeps its own model: only the magnification and reticle change   // the plan's pictures: ACOG = the marksman rifle's, 4x = the M24's, 10x = the .50's
    case 2: mag = 2; reticle = 7; return true;
    case 3: mag = 4; reticle = 5; return true;
    case 4: mag = 10; reticle = 6; return true;
    default: return false;
  }
}

// ---------------------------------------------------------------- drawing
void lootDraw(Game& G) {
  if (!lootActive() || (gL.floor.empty() && gL.cases.empty())) return;
  App& app = G.app;
  if (!gL.box.count) { Material m{"loot", vec3(0.8f), 0.6f, 0.0f, PAT_PLAIN, 1}; Part p; p.center = vec3(0, 0.5f, 0); p.size = vec3(1); MeshData md; appendPart(md, p, m, mat4(1)); gL.box.upload(md); }
  vec3 cam = app.eyePos();
  static const AssetDef* caseAsset = nullptr; static bool looked = false;
  if (!looked) { looked = true; caseAsset = app.lib.find("br_loot_case"); if (!caseAsset) caseAsset = app.lib.find("furn_crate"); }   // BR3's case, else the pine crate
  for (auto& c : gL.cases) {
    if (glm::length(c.pos - cam) > 150) continue;
    mat4 W = glm::translate(mat4(1), c.pos) * glm::rotate(mat4(1), radians(c.yaw), vec3(0, 1, 0));
    if (caseAsset && caseAsset->model) { for (int i = 0; i < (int)caseAsset->model->parts.size(); ++i) app.R.worldModels.push_back({caseAsset->model.get(), i, W}); continue; }
    if (caseAsset) { app.worldBoxDraws.push_back({&caseAsset->opaque, W}); continue; }
    DrawItem di{&gL.box, W * glm::scale(mat4(1), vec3(0.6f, 0.35f, 0.4f))}; di.tint = vec4(0.33f, 0.36f, 0.22f, 1);
    app.worldBoxDraws.push_back(di);
  }
  for (size_t k = 0; k < gL.floor.size(); ++k) {
    const FloorItem& f = gL.floor[k];
    if (glm::length(f.pos - cam) > 120) continue;
    mat4 W = glm::translate(mat4(1), f.pos) * glm::rotate(mat4(1), radians(f.yaw), vec3(0, 1, 0));
    if (f.weapon >= 0) {   // lying on its side, like a dropped weapon (Game::drawItems)
      const WeaponDef& w = *app.weaponDefs[f.weapon];
      mat4 M = W * glm::rotate(mat4(1), radians(90.0f), vec3(0, 0, 1)) * glm::translate(mat4(1), vec3(0, 0, -0.4f));
      if (w.useModel()) { for (int p = 0; p < 8; ++p) for (int i : w.worldParts()[p]) app.R.worldModels.push_back({w.worldModel(), i, M}); }
      else for (int p = 0; p < 8; ++p) if (w.mesh[p].count) app.worldBoxDraws.push_back({&w.mesh[p], M});
      continue;
    }
    std::string assetId = f.id.rfind("ammo:", 0) == 0 ? "br_ammo_" + std::string(ammoName(std::stoi(f.id.substr(5)))) : f.id;
    if (const AssetDef* a = app.lib.find(assetId)) {   // BR3's mesh: the baked model, else its stand-in box
      if (a->model) for (int i = 0; i < (int)a->model->parts.size(); ++i) app.R.worldModels.push_back({a->model.get(), i, W});
      else app.worldBoxDraws.push_back({&a->opaque, W});
      continue; }
    const ItemDef* d = lootItem(f.id);
    vec3 col = f.id.rfind("ammo:", 0) == 0 ? vec3(0.75f, 0.6f, 0.15f) : !d ? vec3(1) : d->kind == ItemKind::Bandage || d->kind == ItemKind::Medkit ? vec3(0.9f, 0.9f, 0.9f)
             : d->kind == ItemKind::Helmet || d->kind == ItemKind::Vest || d->kind == ItemKind::Armor1 ? vec3(0.25f, 0.35f, 0.75f) : d->kind == ItemKind::Backpack ? vec3(0.35f, 0.45f, 0.2f)
             : d->kind == ItemKind::Throwable ? vec3(0.8f, 0.25f, 0.15f) : vec3(0.15f, 0.15f, 0.15f);
    vec3 sz = d && (d->kind == ItemKind::Vest || d->kind == ItemKind::Armor1 || d->kind == ItemKind::Backpack) ? vec3(0.45f, 0.18f, 0.35f) : vec3(0.22f, 0.12f, 0.16f);
    DrawItem di{&gL.box, W * glm::scale(mat4(1), sz)}; di.tint = vec4(col, 1);
    app.worldBoxDraws.push_back(di);
  }
}

void lootUi(App& app) {
  if (!lootActive()) { gL.inventory = false; return; }
  Soldier* L = gGame->local();
  if (!L || !L->alive || gGame->match.phase == MatchPhase::Ended || app.pauseMenu) { gL.inventory = false; if (!L || !L->alive) return; }   // the end screen and the Esc menu are never hidden
  ImGuiIO& io = ImGui::GetIO(); ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  Kit& k = *kitOf(L->slot);
  // keys: Tab / I inventory, 7 bandage, 8 medkit (not while typing)
  static bool tabWas = false, iWas = false;
  if (!app.selftest && app.win && !io.WantTextInput) {
    bool tab = glfwGetKey(app.win, GLFW_KEY_TAB) == GLFW_PRESS, ik = glfwGetKey(app.win, GLFW_KEY_I) == GLFW_PRESS;
    if ((tab && !tabWas) || (ik && !iWas)) { if (gL.openCase) gL.openCase = 0; else gL.inventory = !gL.inventory; }   // with a case open, Tab / I close it
    tabWas = tab; iWas = ik;
    std::string why;
    if (glfwGetKey(app.win, GLFW_KEY_7) == GLFW_PRESS && !k.healKind) { if (healStart(k, 1, L->hp, &why)) k.healShots = app.wstate[app.weaponDefs[app.activeWeapon]->id].shotIdx; else app.toast(why, 1.5); }
    if (glfwGetKey(app.win, GLFW_KEY_8) == GLFW_PRESS && !k.healKind) { if (healStart(k, 2, L->hp, &why)) k.healShots = app.wstate[app.weaponDefs[app.activeWeapon]->id].shotIdx; else app.toast(why, 1.5); }
  }
  if (!L->alive || gGame->match.phase == MatchPhase::Ended || app.pauseMenu) gL.openCase = 0;
  if (gL.openCase) {   // the case window, while the case is within reach
    LootCase* oc = findCase(gL.openCase);
    if (oc && glm::length(oc->pos - L->feet) < 3.5f) { app.uiCursor = true; drawCaseScreen(app, *oc); return; }
    gL.openCase = 0;
  }
  if (gL.inventory) { app.uiCursor = true; drawInventoryScreen(app); return; }
  // what the player looks at: the item nearest the aim line within 2.5 m
  vec3 e = app.eyePos(), fwd = App::forward(app.eyeYaw, app.eyePitch);
  gL.target = -1; float best = 0.45f;
  for (int i = 0; i < (int)gL.floor.size(); ++i) {
    vec3 v = gL.floor[i].pos + vec3(0, 0.1f, 0) - e; float along = glm::dot(v, fwd);
    if (along < 0.2f || along > 2.5f) continue;
    float off = glm::length(v - fwd * along);
    if (off < best) { best = off; gL.target = i; }
  }
  gL.caseTarget = -1;
  for (int i = 0; i < (int)gL.cases.size(); ++i) {   // BR5: a case is bigger, so it is easier to aim at
    vec3 v = gL.cases[i].pos + vec3(0, 0.25f, 0) - e; float along = glm::dot(v, fwd);
    if (along < 0.2f || along > 2.8f) continue;
    float off = glm::length(v - fwd * along) - 0.2f;
    if (off < best) { best = off; gL.caseTarget = i; gL.target = -1; }
  }
  if (gL.caseTarget >= 0) {
    const LootCase& lc = gL.cases[gL.caseTarget];
    std::string t = "[E]  Loot " + lc.owner + "  (" + std::to_string(lc.items.size()) + (lc.items.size() == 1 ? " item)" : " items)");
    ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    dl->AddRectFilled(ImVec2(c.x + 40, c.y + 30), ImVec2(c.x + 56 + ts.x, c.y + 56), IM_COL32(10, 10, 10, 180), 4);
    dl->AddText(ImVec2(c.x + 48, c.y + 35), IM_COL32(245, 240, 220, 255), t.c_str());
  }
  if (gL.target >= 0) {
    std::string t = "[E]  " + lootItemName(app, gL.floor[gL.target]);
    ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    dl->AddRectFilled(ImVec2(c.x + 40, c.y + 30), ImVec2(c.x + 56 + ts.x, c.y + 56), IM_COL32(10, 10, 10, 180), 4);
    dl->AddText(ImVec2(c.x + 48, c.y + 35), IM_COL32(245, 240, 220, 255), t.c_str());
  }
  // heal progress ring
  if (k.healKind) {
    float u = std::min(1.0f, k.healT / std::max(0.1f, cfg().healSeconds[k.healKind]));
    dl->PathArcTo(ImVec2(c.x, c.y + 90), 22, -1.5708f, -1.5708f + u * 6.2832f, 32); dl->PathStroke(IM_COL32(120, 230, 120, 230), 0, 4);
    const char* t = k.healKind == 1 ? "Bandage" : "Medkit"; ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + 118), IM_COL32(230, 240, 230, 255), t);
  }
  // red dot: a dot over the iron sight picture in ADS
  if (app.activeWeapon >= 0 && app.adsT > 0.85f && app.wstate[app.weaponDefs[app.activeWeapon]->id].att[ATT_SIGHT] == 1) dl->AddCircleFilled(c, 2.5f, IM_COL32(255, 40, 30, 255));
  // armour / weight strip, bottom centre
  char b[160]; std::snprintf(b, sizeof b, "Helmet %d  Vest %d  Bag %d   %d / %d", k.helmet, k.vest, k.backpack, (int)kitWeight(&app, k, &app.carried), (int)kitCapacity(k));
  ImVec2 bs = ImGui::CalcTextSize(b); dl->AddText(ImVec2(c.x - bs.x * 0.5f, io.DisplaySize.y - 34), IM_COL32(230, 225, 205, 220), b);
}
