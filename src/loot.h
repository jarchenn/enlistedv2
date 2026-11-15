#pragma once
// BR2 Battle Royale loot (plan Addendum 9; content/settings/loot.json): floor items in buildings, pick-up, ammo pooled
// in seven types, carry weight and backpacks, armour (helmet: head, vest: torso) with durability, bandage / medkit,
// the molotov, and gun attachments (stored in the gun's WeaponState::att, so they travel with it). Active only in the
// Battle Royale mode on the host / single player; LAN replication is a follow-up.
#include "common.h"
#include "combat.h"
#include <imgui.h>
#include <map>

class App; class Game; struct Soldier; struct WeaponDef;

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

// what one soldier carries besides its guns
struct Kit {
  int ammo[AM_COUNT] = {};
  std::map<std::string, int> items;     // bandages, medkits and loose attachments by item id
  int helmet = 0, vest = 0, backpack = 0;
  float helmetDur = 0, vestDur = 0;
  int healKind = 0;                     // 1 bandage, 2 medkit
  float healT = 0, healFrom = 0, healTo = 0; int healShots = 0;
};

struct FloorItem {
  uint32_t uid = 0; std::string id;     // item id, or "" for a gun (weapon >= 0)
  int count = 1;                        // ammo rounds, or the armour's durability
  int weapon = -1; WeaponState ws;      // guns keep their magazine and attachments
  vec3 pos{0}; float yaw = 0;
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
int lootMagSize(const WeaponDef& d, const WeaponState& s);   // the magazine with an extended magazine fitted
bool lootSightFits(const WeaponDef& d, int sight);
bool lootAttachFits(const WeaponDef& d, const ItemDef& it);

// pure rules (headless tests)
float kitWeight(const App* app, const Kit& k, const std::vector<int>* carried);   // carried throwables: the player's weapon list (app may be null without them)
float kitCapacity(const Kit& k);
float armorAbsorb(Kit& k, bool head, float dmg);       // the damage that gets through; wears / breaks the piece
bool healStart(Kit& k, int kind, float hp, std::string* why = nullptr);
float healStep(Kit& k, float hp, float dt);            // hp after dt of the running heal (ends it, spending the item)

bool lootActive();                     // a Battle Royale match is running here (host / single player)
Kit* kitOf(int slot);
std::vector<FloorItem>& lootFloor();
std::vector<LootCase>& lootCases();
void lootDropItem(vec3 at, const std::string& id, int count);   // an item from the inventory to the floor
std::string lootItemName(App& app, const FloorItem& f);

// hooks
void lootBegin(Game& G);                               // Game::begin: places the floor loot
void lootStep(Game& G, float dt);                      // Game::step: strip new spawns, heal, bots pick up
bool lootInteract(App& app);                           // E: pick up what the player looks at
bool lootBotGoal(Game& G, Soldier& s, vec3& goal);     // AiCore::objectiveGoal: the bot wants an item first
// BR5: a bot's starting guns (royale.cpp calls it when the bot lands): n different guns from the loot pool, each with
// one full magazine and its ammo boxes; the first is in hand (s.weapon / s.ws), the rest in s.wsBy (s.carried lists all)
void lootArmBot(Game& G, Soldier& s, int n);
float lootDamage(int victim, int zone, float dmg);     // damage_model: armour on the hit zone
void lootBeforeWeapons(App& app);                      // around App::stepWeapons: the pooled ammo as magazines
void lootAfterWeapons(App& app);
void lootMolotov(App& app, vec3 pos, int owner);       // the molotov's burst: ground fire
bool lootScope(const App& app, float& mag, int& reticle);   // an attached optic in ADS: magnification + eyepiece reticle id
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
