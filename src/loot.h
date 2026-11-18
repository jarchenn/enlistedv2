#pragma once
// BR2 Battle Royale loot (plan Addendum 9; content/settings/loot.json): floor items in buildings, pick-up, ammo pooled
// in seven types, carry weight and backpacks, armour (helmet: head, vest: torso) with durability, bandage / medkit,
// the molotov, and gun attachments (stored in the gun's WeaponState::att, so they travel with it). Active only in the
// Battle Royale mode on the host / single player; LAN replication is a follow-up.
#include "common.h"
#include "combat.h"
#include <imgui.h>
#include <map>

class App; class Game; struct Soldier; struct WeaponDef; struct ModelDraw; struct DrawItem;

enum AmmoType : int { AM_9MM = 0, AM_45, AM_556, AM_762, AM_50, AM_ROCKET, AM_12GA, AM_COUNT, AM_NONE = -1 };
enum AttachSlot : int { ATT_SIGHT = 0, ATT_MUZZLE, ATT_MAG, ATT_STOCK };
enum class ItemKind : int { Gun, Ammo, Helmet, Vest, Armor1, Backpack, Sight, Muzzle, Mag, Stock, Bandage, Medkit, Throwable };

struct ItemDef {
  std::string id, name, weapon;   // weapon: the throwable's weapon id
  ItemKind kind = ItemKind::Bandage;
  int level = 0, value = 0;       // armour / backpack level; attachment value (sight 1-4, muzzle 1-2, mag / stock 1)
  float weight = 0, rarity = 1;
  bool military = false;
};

struct FloorItem {
  uint32_t uid = 0; std::string id;     // item id, or "" for a gun (weapon >= 0)
  int count = 1;                        // ammo rounds, pieces in a stack, or the armour's durability
  int weapon = -1; WeaponState ws;      // guns keep their magazine and attachments
  vec3 pos{0}; float yaw = 0;
};

// what one soldier carries besides its guns
struct Kit {
  int ammo[AM_COUNT] = {};
  std::map<std::string, int> items;     // bandages, medkits, throwables-to-be, loose attachments by item id
  std::vector<FloorItem> gear;          // BR6: spare helmets / vests in the bag (count: durability)
  int helmet = 0, vest = 0, backpack = 0;
  float helmetDur = 0, vestDur = 0;
  int healKind = 0;                     // 1 bandage, 2 medkit
  float healT = 0, healFrom = 0, healTo = 0; int healShots = 0;
};

// BR5: what a dead combatant carried, in a case by the body; anyone may take items out, the rest stays for the match
struct LootCase {
  uint32_t uid = 0; vec3 pos{0}; float yaw = 0;
  std::string owner;
  std::vector<FloorItem> items;
};

const ItemDef* lootItem(const std::string& id);
int ammoTypeOf(const WeaponDef& d);
const char* ammoName(int type);
int ammoBox(int type);
bool lootIsPistol(const WeaponDef& d);
// UI11: the weapon case filter. Exactly one per weapon: JSON "category" if set, else from the data (blasters, launchers, flame,
// heavy, belt-fed / LMG, shotguns and equipment are special; scoped rifles and the .50 snipers; then pistols, SMGs, rifles)
enum GunFilter : int { GF_ALL = 0, GF_PISTOL, GF_SMG, GF_RIFLE, GF_SNIPER, GF_SPECIAL, GF_COUNT };
int weaponFilterClass(const WeaponDef& d);
int lootMagSize(const WeaponDef& d, const WeaponState& s);   // the magazine with an extended magazine fitted
bool lootSightFits(const WeaponDef& d, int sight);
// the sight in use: the fitted one, else a scoped sniper rifle's own scope, which counts as the ACOG (2)
int lootSight(const WeaponDef& d, const WeaponState& s);
inline bool lootBuiltInAcog(const WeaponDef& d, const WeaponState& s) { return s.att[ATT_SIGHT] == 0 && d.hasScope && d.wclass == WC_SNIPER; }
bool lootAttachFits(const WeaponDef& d, const ItemDef& it);
// SM1: fitting, shared by the inventory, the smithing table and its check. Item ids per slot value (sight 1-4, muzzle 1-2,
// mag / stock 1); fit puts a replaced part back in the bag; a sniper's own scope never comes off
constexpr float kBrakeRecoil = 0.8f, kBrakeSpread = 0.7f, kPadAdsTime = 0.75f;   // BR2 muzzle brake / recoil pad (weapons.cpp)
const char* lootAttachItemId(int slot, int value);
const char* lootSlotName(int slot);
int lootSlotOf(ItemKind k);
bool lootGunTakesParts(const WeaponDef& d);   // any slot can take a part in Battle Royale
bool lootFit(App& app, Kit& k, int weapon, const std::string& itemId, std::string* why = nullptr);
bool lootTakeOff(App& app, Kit& k, int weapon, int slot, std::string* why = nullptr);
struct GunStats { float adsTime = 0, recoil = 1, spread = 1; int mag = 0, sight = 0; bool quiet = false; };
GunStats lootGunStats(const App& app, const WeaponDef& d, const WeaponState& s);

// pure rules (headless tests)
float kitWeight(const App* app, const Kit& k, const std::vector<int>* carried);   // carried throwables: the player's weapon list (app may be null without them)
float kitCapacity(const Kit& k);
float armorAbsorb(Kit& k, bool head, float dmg, float pen = -1);   // DMG1 pen >= 0: the weapon's penetration (level applies 60 / 80 / 100 % of the gap); < 0: the level's flat reduce       // the damage that gets through; wears / breaks the piece
bool healStart(Kit& k, int kind, float hp, std::string* why = nullptr);
float healStep(Kit& k, float hp, float dt);            // hp after dt of the running heal (ends it, spending the item)

bool lootActive();                     // a Battle Royale match is running here (host / single player)
// BR11: an indoor map (map extra "royale": {"indoor": true}: Tunnels, Backrooms): no drop, everyone starts inside, loot is
// scattered through the rooms and corridors instead of placed in tagged buildings
bool lootIndoorMap(const App& app);
bool lootIndoorPoint(Game& G, vec3& out);   // a random navmesh point with a ceiling over it (not a roof, not the open surface)
Kit* kitOf(int slot);
std::vector<FloorItem>& lootFloor();
std::vector<LootCase>& lootCases();
void lootDropItem(vec3 at, const std::string& id, int count);   // an item from the inventory to the floor
std::string lootItemName(App& app, const FloorItem& f);
std::string lootPieceId(bool helmet, int level);
std::string lootAssetId(const std::string& itemId);   // the asset (model) an item is drawn with
// BR9 icons (inventory_ui.cpp): the item's icon at p in a 2s x s slot (guns: the white silhouette; items: a small render
// of the model, cached; a drawn glyph without one); the ImGui form reserves the slot in the current line
void lootDrawIcon(App& app, ImDrawList* dl, ImVec2 p, float s, const FloorItem& f);
void lootIcon(App& app, const FloorItem& f, float s = 22);
void lootIcon(App& app, const std::string& itemId, float s = 22);       // the item id of a single helmet / vest piece
void lootWearGear(App& app, uint32_t uid);             // inventory: wear a spare piece; the worn one goes into the bag
void lootDiscard(App& app, const std::string& key);    // inventory: drop a whole row ("ammo:<t>", "item:<id>", "gear:<uid>", "gun:<w>", "throw:<w>", "worn:helmet|vest|bag")

// hooks
void lootBegin(Game& G);                               // Game::begin: places the floor loot
void lootStep(Game& G, float dt);                      // Game::step: strip new spawns, heal, bots pick up
bool lootInteract(App& app);                           // E: pick up what the player looks at
bool lootBotGoal(Game& G, Soldier& s, vec3& goal);     // AiCore::objectiveGoal: the bot wants an item first
// BR5: a bot's starting guns (royale.cpp calls it when the bot lands): n different guns from the loot pool, each with
// one full magazine and its ammo boxes; the first is in hand (s.weapon / s.ws), the rest in s.wsBy (s.carried lists all)
void lootArmBot(Game& G, Soldier& s, int n);
float lootDamage(int victim, int zone, float dmg, float pen = -1);     // damage_model: armour on the hit zone
void lootBeforeWeapons(App& app);                      // around App::stepWeapons: the pooled ammo as magazines
void lootAfterWeapons(App& app);
void lootMolotov(App& app, vec3 pos, int owner);       // the molotov's burst: ground fire
bool lootScope(const App& app, float& mag, int& reticle);   // an attached optic in ADS: magnification + eyepiece reticle id
void lootAttachSheetUi(App& app, bool zoom = false);
// BR10 rail fit: for each gun that takes an optic, the receiver's top under the ACOG's footprint (a side render of the
// bare gun); writes {id: [x, y, z]} rail suggestions to `out` (applied to content/weapons/<id>.json "rail")
void lootRailFit(App& app, const fs::path& out);   // zoom: framed on the optic and receiver   // BR10 check: every loot gun from the side with sight / brake / magazine / pad fitted, bore (red) and sight line (green)
// BR10: the fitted muzzle device, extended magazine and recoil pad on a gun (weapon space W); the optic is brSightDraws'
void lootAttachDraws(App& app, const WeaponDef& d, const WeaponState& ws, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes);
void lootDraw(Game& G);                                // floor items (stand-in boxes until BR3's br_* assets exist)
void lootUi(App& app);                                 // pick-up prompt, heal ring, keys; the inventory screen
bool lootInventoryOpen();                              // the inventory or a case window is open
void lootSetInventory(bool open);
bool lootOnDeath(Game& G, int slot);                   // Game::killSoldier: the gear goes into a case (true: skip the normal weapon drop)
void lootOpenCase(uint32_t uid);                       // 0 closes
bool lootTakeFromCase(App& app, uint32_t item, bool swap, std::string& msg);   // the open case's item into the inventory
void drawCaseScreen(App& app, LootCase& c);            // inventory_ui.cpp
void lootDropGun(App& app, int weapon);                // from the inventory: the gun goes to the floor with its state
void drawInventoryScreen(App& app);                    // inventory_ui.cpp
void drawInventoryCharacter(ImVec2 min, ImVec2 max);   // hook for BR3's character preview (inventory_ui.cpp: a placeholder)
