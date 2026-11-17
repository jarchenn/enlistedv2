#pragma once
// SD1 Search and Destroy: the authoritative round controller (docs/search-and-destroy.md; the user's specification
// "Search & Destroy - implementation specification", 16 Nov 2026). Pure logic, like Match: the game layer (sd_game.cpp)
// reports who is alive and where, who holds plant / defuse, kills and purchases; the controller owns phases and clocks,
// the bomb, round results (precedence, ties, one latched result), the economy and the match format, and emits events the
// game layer acts on (respawn everyone, effects, banners). Server (host / solo) only; clients mirror stateJson().
// Every value comes from content/settings/search_destroy.json (SdConfig defaults = the spec's).
#include "common.h"
#include <string>
#include <vector>

enum class SdPhase : int { Warmup = 0, Ready, Freeze, LiveUnplanted, LivePlanted, RoundEnd, Halftime, OvertimeBreak, MatchEnd, Timeout };
const char* sdPhaseName(SdPhase p);
enum class SdReason : int { None = 0, Elimination, Time, Detonation, Defuse };
const char* sdReasonName(SdReason r);
enum class SdBomb : int { None = 0, Carried, Dropped, Planted, Detonated, Defused };
enum class SdKill : int { Firearm = 0, Smg, Shotgun, SniperHeavy, Knife, Grenade, Bomb, World };   // kill reward class

// one shop entry (data: content/settings/search_destroy.json "shop")
struct SdItem {
  std::string id, name, cat;      // cat: pistol, smg, shotgun, rifle, sniper, heavy, grenade, equipment
  std::string weapon;             // the game weapon id it gives (firearms, grenades); "" for armor / kit
  int price[2] = {0, 0};          // [attackers (Raiders), defenders (Security)]; < 0: not sold to that side
  int slot = 0;                   // 1 primary, 2 secondary, 4 grenade, 0 equipment
  int maxCarry = 1, maxPerRound = 1;   // grenades: carry limit / purchases per round
  SdKill kill = SdKill::Firearm;
};

struct SdConfig {
  // clocks (s)
  float warmup = 60, ready = 5, freeze = 15, liveBuy = 20, roundTime = 115, plantTime = 3.2f, fuse = 40, defuseKit = 5, defuseNoKit = 10,
        roundEnd = 7, halftime = 30, timeout = 30;
  int timeouts = 2;               // per team in regulation
  // format: rounds per half and the win target (MR15: 15 / 16; MR12: 12 / 13); overtime halves of 3, first to 4 in a block
  int halfRounds = 15, winRounds = 16, otHalf = 3, otWin = 4, otCash = 10000;
  int teamSize = 5;
  // economy
  int startCash = 800, maxCash = 16000;
  int winElim = 3250, winTime = 3250, winDetonate = 3500, winDefuse = 3500, plantBonus = 300, defuseBonus = 300, plantedLossBonus = 800;
  int lossBase = 1400, lossStep = 500, lossMax = 4, lossStart = 1;
  int killReward[8] = {300, 600, 900, 100, 1500, 300, 0, 0};   // by SdKill
  int teamKillPenalty = 300;
  // bomb / site
  float defuseRange = 1.5f, bombDamage = 500, bombRadius = 35, bombCoverMult = 0.25f, buyRadius = 12;
  bool warmupDefault = false, vehiclesDefault = false;   // SD3: the setup screen's Warmup / Vehicles options start at these
  float speedX = 1.18f;           // GP1: everyone's movement speed in this mode x this (players and bots; on top of movement.json infantrySpeed_x)
  bool friendlyFire = true;
  std::vector<SdItem> shop;
  std::string freePistol[2] = {"modern_pistol", "m1911a1"};   // [attackers, defenders]
  std::string factionName[2] = {"Raiders", "Security"};
  static SdConfig load();          // content/settings/search_destroy.json over these defaults
  const SdItem* item(const std::string& id) const;
};

// what a player keeps between rounds (spec §6)
struct SdGear {
  std::string primary, secondary;   // weapon ids ("" none)
  std::vector<std::string> grenades;   // weapon ids, at most 4
  int armor = 0; bool helmet = false, kit = false;
  void clear() { *this = SdGear{}; }
};

struct SdPlayer {
  int slot = -1, team = 0;          // persistent team 0 / 1 (Match slot team)
  int cash = 800;
  SdGear gear;
  bool alive = false; vec3 feet{0};
  bool everAlive = false;
  int kills = 0, deaths = 0, plants = 0, defuses = 0;
  struct Purchase { std::string item; int price = 0; bool refundable = true; SdGear before; };   // this round's buys (allowances, refunds)
  std::vector<Purchase> bought;
  int bombAssigned = 0;             // times it got the bomb (spread assignment)
};

struct SdEvent { enum Type { RoundStart, LiveStart, BuyClosed, Planted, Defused, Detonated, RoundEnd, Halftime, MatchEnd, BombDropped, BombPicked, PlantStart, PlantCancel, DefuseStart, DefuseCancel, Cash, Timeout } type;
                 int slot = -1, team = -1, site = -1, amount = 0; SdReason reason = SdReason::None; std::string text; };

// per-step input for one slot (the game layer decides validity: in a site, carrying, supported ground, trace, ...)
struct SdInput { bool alive = false; vec3 feet{0}; bool plantHeld = false; int plantSite = -1; bool defuseHeld = false; };

class SdMatch {
public:
  SdConfig cfg;
  SdPhase phase = SdPhase::Warmup;
  double t = 0, phaseEnd = 0, liveStart = 0, roundDeadline = 0, detonateAt = 0;   // controller time (s)
  int round = 0;                    // 1-based rounds played / in play
  int score[2] = {0, 0};            // persistent teams
  int attackerTeam = 1;             // which persistent team attacks now
  int lossIdx[2] = {1, 1};
  bool overtime = false; int otBlock = 0, otRound = 0, otScore[2] = {0, 0};   // rounds into the current block
  int timeoutsLeft[2] = {2, 2}; bool timeoutPending[2] = {false, false};
  std::vector<SdPlayer> players;
  // bomb
  SdBomb bomb = SdBomb::None; int carrier = -1; vec3 bombPos{0}; int bombSite = -1;
  int planter = -1; float plantProgress = 0; int defuser = -1; float defuseProgress = 0, defuseNeeded = 10;
  bool planted = false;            // this round saw a completed plant (loss bonus)
  // latched result
  int roundWinner = -1; SdReason reason = SdReason::None; int matchWinner = -1;
  std::vector<SdEvent> events;      // emitted since the last takeEvents()
  uint32_t rev = 1;

  void setup(int nPlayers, const std::vector<int>& teamOf, bool warmup);   // players = Match slots (index = slot)
  void update(float dt, const std::vector<SdInput>& in);
  // game layer reports (server)
  void onKill(int victim, int killer, SdKill kind, bool teamKill);
  void onBombPickup(int slot);                   // an attacker took the dropped bomb
  void onBombDropped(int slot, vec3 at);         // G / death: the carrier dropped it (at: a valid ground spot)
  // purchases: validated here (phase, alive, team side, zone flag from the game layer, cash, faction, capacity, allowance)
  std::string canBuy(int slot, const std::string& item, bool inZone) const;   // "" = allowed, else the reason
  bool buy(int slot, const std::string& item, bool inZone);
  bool refund(int slot, const std::string& item, bool inZone);
  void onPickedWeapon(int slot, const std::string& weapon, int slotKind);   // a picked-up gun becomes kept gear (no refund)
  void onUsedGrenade(int slot, const std::string& weapon);
  void requestTimeout(int team);
  void skipWarmup();
  // queries
  int sideOf(int slot) const { return slot >= 0 && slot < (int)players.size() && players[slot].team == attackerTeam ? 0 : 1; }   // 0 attacker, 1 defender
  bool attacker(int slot) const { return sideOf(slot) == 0; }
  bool buyOpen() const;            // the buy window (zone still applies)
  bool frozen() const { return phase == SdPhase::Freeze || phase == SdPhase::Ready || phase == SdPhase::Halftime || phase == SdPhase::OvertimeBreak || phase == SdPhase::Timeout; }
  bool combat() const { return phase == SdPhase::Warmup || phase == SdPhase::LiveUnplanted || phase == SdPhase::LivePlanted || phase == SdPhase::RoundEnd; }
  float clock() const;             // the HUD round clock (fuse after a plant)
  int alive(int side) const;
  std::vector<SdEvent> takeEvents() { std::vector<SdEvent> e; e.swap(events); return e; }
  json stateJson() const;          // replicated summary
  void fromJson(const json& j);    // client mirror
  int lossPayout(int team) const { return cfg.lossBase + cfg.lossStep * lossIdx[team]; }
private:
  void emit(SdEvent e) { events.push_back(std::move(e)); ++rev; }
  void pay(int slot, int amount, const char* why);
  void startRound();
  void endRound(int winnerSide, SdReason why);
  void nextAfterRoundEnd();
  void assignBomb();
  void resetHalfEconomy(int cash);
};

// self-check of the round logic (precedence, ties, economy ladder); returns failures (empty = ok)
std::vector<std::string> sdLogicSelfCheck();
