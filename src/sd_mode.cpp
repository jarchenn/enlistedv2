// SD1 Search and Destroy round controller (sd_mode.h). Step order inside one update, documented for the same-timestamp
// policy (spec §4): 1. the plant (or the defuse) completing at its exact time inside the step, 2. the round deadline (or the
// detonation), compared by timestamp with 1 (a plant must finish strictly before the deadline; at a defuse / detonation
// tie the bomb detonates), 3. eliminations. The first result latches; nothing later in the round changes it or pays out.
#include "sd_mode.h"
#include <algorithm>
#include <cmath>

const char* sdPhaseName(SdPhase p) {
  static const char* n[] = {"Warmup", "Ready", "Freeze", "Live", "Bomb planted", "Round over", "Halftime", "Overtime", "Match over", "Timeout"};
  return n[std::clamp((int)p, 0, 9)];
}
const char* sdReasonName(SdReason r) {
  static const char* n[] = {"", "enemy eliminated", "time ran out", "the bomb detonated", "the bomb was defused"};
  return n[std::clamp((int)r, 0, 4)];
}

// ---------------------------------------------------------------- config
const SdItem* SdConfig::item(const std::string& id) const { for (auto& i : shop) if (i.id == id) return &i; return nullptr; }

SdConfig SdConfig::load() {
  SdConfig c;
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "search_destroy.json")); } catch (...) { logf("sd: content/settings/search_destroy.json missing or unreadable: the spec defaults, no shop"); return c; }
  auto F = [&](const char* k, float& v) { if (j.contains(k) && j[k].is_number()) v = j[k].get<float>(); };
  auto I = [&](const char* k, int& v) { if (j.contains(k) && j[k].is_number()) v = j[k].get<int>(); };
  F("warmup_s", c.warmup); c.warmupDefault = j.value("warmup", c.warmupDefault); c.vehiclesDefault = j.value("vehicles", c.vehiclesDefault); F("ready_s", c.ready); F("freeze_s", c.freeze); F("liveBuy_s", c.liveBuy); F("round_s", c.roundTime); F("plant_s", c.plantTime);
  F("fuse_s", c.fuse); F("defuseKit_s", c.defuseKit); F("defuse_s", c.defuseNoKit); F("roundEnd_s", c.roundEnd); F("halftime_s", c.halftime); F("timeout_s", c.timeout);
  I("timeouts", c.timeouts); I("halfRounds", c.halfRounds); I("winRounds", c.winRounds); I("otHalfRounds", c.otHalf); I("otWinRounds", c.otWin); I("otCash", c.otCash); I("teamSize", c.teamSize);
  I("startCash", c.startCash); I("maxCash", c.maxCash); I("winElimination", c.winElim); I("winTime", c.winTime); I("winDetonation", c.winDetonate); I("winDefuse", c.winDefuse);
  I("plantReward", c.plantBonus); I("defuseReward", c.defuseBonus); I("plantedLossBonus", c.plantedLossBonus); I("lossBase", c.lossBase); I("lossStep", c.lossStep); I("lossMax", c.lossMax); I("lossStart", c.lossStart);
  I("teamKillPenalty", c.teamKillPenalty);
  F("defuseRange_m", c.defuseRange); F("bombDamage", c.bombDamage); F("bombRadius_m", c.bombRadius); F("bombCoverMult", c.bombCoverMult); F("buyRadius_m", c.buyRadius); F("speed_x", c.speedX);
  if (j.contains("friendlyFire")) c.friendlyFire = j.value("friendlyFire", true);
  if (j.contains("killReward") && j["killReward"].is_object()) {
    const char* k[8] = {"firearm", "smg", "shotgun", "sniperHeavy", "knife", "grenade", "bomb", "world"};
    for (int i = 0; i < 8; ++i) c.killReward[i] = j["killReward"].value(k[i], c.killReward[i]);
  }
  if (j.contains("freePistol") && j["freePistol"].is_array() && j["freePistol"].size() == 2) for (int s = 0; s < 2; ++s) c.freePistol[s] = j["freePistol"][s].get<std::string>();
  if (j.contains("factions") && j["factions"].is_array() && j["factions"].size() == 2) for (int s = 0; s < 2; ++s) c.factionName[s] = j["factions"][s].get<std::string>();
  if (j.contains("shop") && j["shop"].is_array())
    for (auto& e : j["shop"]) {
      SdItem it; it.id = e.value("id", ""); it.name = e.value("name", it.id); it.cat = e.value("cat", ""); it.weapon = e.value("weapon", "");
      it.price[0] = e.value("raiders", -1); it.price[1] = e.value("security", -1);
      it.slot = e.value("slot", 0); it.maxCarry = e.value("maxCarry", 1); it.maxPerRound = e.value("maxPerRound", 1);
      std::string k = e.value("kill", "firearm");
      it.kill = k == "smg" ? SdKill::Smg : k == "shotgun" ? SdKill::Shotgun : k == "sniperHeavy" ? SdKill::SniperHeavy : k == "knife" ? SdKill::Knife : k == "grenade" ? SdKill::Grenade : SdKill::Firearm;
      if (!it.id.empty()) c.shop.push_back(it);
    }
  return c;
}

// ---------------------------------------------------------------- match
void SdMatch::setup(int n, const std::vector<int>& teamOf, bool warmup) {
  players.assign(n, SdPlayer{});
  for (int i = 0; i < n; ++i) { players[i].slot = i; players[i].team = i < (int)teamOf.size() ? teamOf[i] : 0; players[i].cash = cfg.startCash; }
  t = 0; round = 0; score[0] = score[1] = 0; attackerTeam = 1; lossIdx[0] = lossIdx[1] = cfg.lossStart;
  overtime = false; otBlock = 0; otRound = 0; otScore[0] = otScore[1] = 0; matchWinner = -1;
  timeoutsLeft[0] = timeoutsLeft[1] = cfg.timeouts; timeoutPending[0] = timeoutPending[1] = false;
  bomb = SdBomb::None; carrier = planter = defuser = -1;
  if (warmup && cfg.warmup > 0) { phase = SdPhase::Warmup; phaseEnd = cfg.warmup; }
  else { phase = SdPhase::Ready; phaseEnd = 0; }
  ++rev;
}

void SdMatch::skipWarmup() { if (phase == SdPhase::Warmup) phaseEnd = t; }

void SdMatch::pay(int s, int amount, const char* why) {
  if (s < 0 || s >= (int)players.size() || amount == 0) return;
  int before = players[s].cash; players[s].cash = std::clamp(before + amount, 0, cfg.maxCash);
  SdEvent e{SdEvent::Cash}; e.slot = s; e.amount = players[s].cash - before; e.text = why; emit(e);
}

void SdMatch::resetHalfEconomy(int cash) {
  for (auto& p : players) { p.cash = cash; p.gear.clear(); p.bought.clear(); }
  lossIdx[0] = lossIdx[1] = cfg.lossStart;
}

void SdMatch::assignBomb() {
  int best = -1, least = 1 << 30; uint32_t h = 2166136261u ^ (uint32_t)round * 16777619u;
  for (auto& p : players) {
    if (p.team != attackerTeam) continue;
    h = (h ^ (uint32_t)p.slot) * 16777619u;   // a deterministic shuffle per round among the least-assigned
    int key = p.bombAssigned * 4096 + (int)(h % 4096u);
    if (key < least) { least = key; best = p.slot; }
  }
  carrier = best; bomb = best >= 0 ? SdBomb::Carried : SdBomb::None; bombSite = -1;
  if (best >= 0) { ++players[best].bombAssigned; SdEvent e{SdEvent::BombPicked}; e.slot = best; e.text = "assigned"; emit(e); }
}

void SdMatch::startRound() {
  ++round;
  bomb = SdBomb::None; carrier = planter = defuser = -1; plantProgress = defuseProgress = 0; planted = false; bombSite = -1;
  roundWinner = -1; reason = SdReason::None;
  for (auto& p : players) p.bought.clear();
  assignBomb();
  bool to = false; for (int k = 0; k < 2; ++k) if (timeoutPending[k]) { timeoutPending[k] = false; to = true; }
  if (to) { phase = SdPhase::Timeout; phaseEnd = t + cfg.timeout; SdEvent e{SdEvent::Timeout}; emit(e); }
  else { phase = SdPhase::Freeze; phaseEnd = t + cfg.freeze; }
  SdEvent e{SdEvent::RoundStart}; e.amount = round; e.team = attackerTeam; emit(e);
}

void SdMatch::requestTimeout(int team) {
  if (team < 0 || team > 1 || timeoutsLeft[team] <= 0 || timeoutPending[team] || phase == SdPhase::MatchEnd) return;
  --timeoutsLeft[team]; timeoutPending[team] = true; ++rev;
}

bool SdMatch::buyOpen() const {
  if (roundWinner >= 0) return false;
  if (phase == SdPhase::Freeze || phase == SdPhase::Timeout) return true;
  return (phase == SdPhase::LiveUnplanted || phase == SdPhase::LivePlanted) && t < liveStart + cfg.liveBuy;
}

float SdMatch::clock() const {
  switch (phase) {
    case SdPhase::LiveUnplanted: return (float)std::max(0.0, roundDeadline - t);
    case SdPhase::LivePlanted: return (float)std::max(0.0, detonateAt - t);
    case SdPhase::Freeze: return cfg.roundTime;   // the round clock waits at 1:55 (the freeze countdown is separate)
    default: return (float)std::max(0.0, phaseEnd - t);
  }
}

int SdMatch::alive(int side) const { int n = 0; for (auto& p : players) n += p.alive && sideOf(p.slot) == side; return n; }

void SdMatch::endRound(int win, SdReason why) {
  if (roundWinner >= 0) return;   // latched
  int wt = win == 0 ? attackerTeam : 1 - attackerTeam, lt = 1 - wt;
  roundWinner = wt; reason = why;
  ++score[wt]; if (overtime) { ++otScore[wt]; ++otRound; }
  if (why == SdReason::Elimination && win == 0 && bomb == SdBomb::Planted) bomb = SdBomb::None;   // spec §4: the fuse is cancelled
  if (why == SdReason::Detonation) bomb = SdBomb::Detonated;
  if (why == SdReason::Defuse) bomb = SdBomb::Defused;
  planter = defuser = -1; plantProgress = defuseProgress = 0;
  int winPay = why == SdReason::Detonation ? cfg.winDetonate : why == SdReason::Defuse ? cfg.winDefuse : why == SdReason::Time ? cfg.winTime : cfg.winElim;
  const int loss = lossPayout(lt);
  for (auto& p : players) {
    if (p.team == wt) pay(p.slot, winPay, "round win");
    else {
      const bool attackersLost = lt == attackerTeam;
      if (!(attackersLost && why == SdReason::Time && p.alive)) pay(p.slot, loss, "round loss");   // spec §9: attackers alive at the unplanted timeout get no loss pay
      if (attackersLost && planted) pay(p.slot, cfg.plantedLossBonus, "planted, lost");
    }
  }
  for (auto& p : players) if (p.team == lt && !p.alive) { p.gear.primary.clear(); p.gear.secondary.clear(); }   // SD3: the losers' dead lose their guns, the winners' dead keep theirs
  lossIdx[lt] = std::min(cfg.lossMax, lossIdx[lt] + 1); lossIdx[wt] = std::max(0, lossIdx[wt] - 1);
  phase = SdPhase::RoundEnd; phaseEnd = t + cfg.roundEnd;
  SdEvent e{SdEvent::RoundEnd}; e.team = wt; e.reason = why; e.amount = round;
  e.text = std::string(win == 0 ? cfg.factionName[0] : cfg.factionName[1]) + " win: " + sdReasonName(why); emit(e);
}

void SdMatch::nextAfterRoundEnd() {
  auto matchEnd = [&](int w) { matchWinner = w; phase = SdPhase::MatchEnd; SdEvent e{SdEvent::MatchEnd}; e.team = w; emit(e); };
  auto brk = [&](SdPhase p, int cash) { attackerTeam = 1 - attackerTeam; resetHalfEconomy(cash); phase = p; phaseEnd = t + cfg.halftime; SdEvent e{SdEvent::Halftime}; e.team = attackerTeam; emit(e); };
  if (!overtime) {
    for (int k = 0; k < 2; ++k) if (score[k] >= cfg.winRounds) { matchEnd(k); return; }
    if (round == cfg.halfRounds) { brk(SdPhase::Halftime, cfg.startCash); return; }
    if (round >= 2 * cfg.halfRounds && score[0] != score[1]) { matchEnd(score[0] > score[1] ? 0 : 1); return; }   // (a win target above half + 1 in custom data)
    if (round >= 2 * cfg.halfRounds) {   // 15-15: overtime (MR3 blocks), the sides stay for the first OT half
      overtime = true; otBlock = 1; otRound = 0; otScore[0] = otScore[1] = 0;
      resetHalfEconomy(cfg.otCash); phase = SdPhase::OvertimeBreak; phaseEnd = t + cfg.halftime; SdEvent e{SdEvent::Halftime}; e.team = attackerTeam; e.text = "overtime"; emit(e); return;
    }
    startRound(); return;
  }
  for (int k = 0; k < 2; ++k) if (otScore[k] >= cfg.otWin) { matchEnd(k); return; }
  if (otRound == cfg.otHalf) { brk(SdPhase::OvertimeBreak, cfg.otCash); return; }
  if (otRound >= 2 * cfg.otHalf) { ++otBlock; otRound = 0; otScore[0] = otScore[1] = 0; brk(SdPhase::OvertimeBreak, cfg.otCash); return; }   // 3-3: the block repeats
  startRound();
}

void SdMatch::update(float dt, const std::vector<SdInput>& in) {
  if (!(dt > 0) || !std::isfinite(dt) || phase == SdPhase::MatchEnd) return;
  dt = std::min(dt, 0.25f);
  const double t0 = t; t += dt;
  for (size_t i = 0; i < players.size() && i < in.size(); ++i) { players[i].alive = in[i].alive; if (in[i].alive) { players[i].feet = in[i].feet; players[i].everAlive = true; } }
  auto inp = [&](int s) -> const SdInput& { static SdInput none; return s >= 0 && s < (int)in.size() ? in[s] : none; };
  switch (phase) {
    case SdPhase::Warmup: if (t >= phaseEnd) { phase = SdPhase::Ready; phaseEnd = t + cfg.ready; ++rev; } return;
    case SdPhase::Ready: if (t >= phaseEnd) { resetHalfEconomy(cfg.startCash); startRound(); } return;   // warmup money and gear are discarded
    case SdPhase::Timeout: if (t >= phaseEnd) { phase = SdPhase::Freeze; phaseEnd = t + cfg.freeze; ++rev; } return;
    case SdPhase::Freeze:
      if (t >= phaseEnd) { phase = SdPhase::LiveUnplanted; liveStart = phaseEnd; roundDeadline = liveStart + cfg.roundTime; SdEvent e{SdEvent::LiveStart}; emit(e); }
      return;
    case SdPhase::RoundEnd: if (t >= phaseEnd) nextAfterRoundEnd(); return;
    case SdPhase::Halftime: case SdPhase::OvertimeBreak: if (t >= phaseEnd) startRound(); return;
    default: break;
  }
  if (t0 < liveStart + cfg.liveBuy && t >= liveStart + cfg.liveBuy) { SdEvent e{SdEvent::BuyClosed}; emit(e); }
  if (phase == SdPhase::LiveUnplanted) {
    // 1. the plant: the carrier holding plant inside a site (validity is the game layer's); anything else resets it
    double plantedAt = -1;
    const SdInput& c = inp(carrier);
    if (bomb == SdBomb::Carried && carrier >= 0 && c.alive && c.plantHeld && c.plantSite >= 0) {
      if (planter != carrier) { planter = carrier; plantProgress = 0; SdEvent e{SdEvent::PlantStart}; e.slot = carrier; e.site = c.plantSite; emit(e); }
      float before = plantProgress; plantProgress += dt;
      if (plantProgress >= cfg.plantTime) plantedAt = t0 + (cfg.plantTime - before);
      ++rev;
    } else if (planter >= 0) { SdEvent e{SdEvent::PlantCancel}; e.slot = planter; emit(e); planter = -1; plantProgress = 0; }
    // 2. against the round deadline: strictly before it
    if (plantedAt >= 0 && plantedAt < roundDeadline) {
      phase = SdPhase::LivePlanted; planted = true; bomb = SdBomb::Planted; bombSite = c.plantSite; bombPos = c.feet; detonateAt = plantedAt + cfg.fuse;
      ++players[carrier].plants; pay(carrier, cfg.plantBonus, "plant");
      SdEvent e{SdEvent::Planted}; e.slot = carrier; e.site = bombSite; emit(e);
      carrier = -1; planter = -1; plantProgress = 0;
    } else if (t >= roundDeadline) { endRound(1, SdReason::Time); return; }
    // 3. eliminations (a simultaneous wipe before a plant goes to the defenders)
    if (phase == SdPhase::LiveUnplanted) {
      if (alive(0) == 0) { endRound(1, SdReason::Elimination); return; }
      if (alive(1) == 0) { endRound(0, SdReason::Elimination); return; }
    } else if (alive(1) == 0) { endRound(0, SdReason::Elimination); return; }   // planted this very step, defenders gone
    return;
  }
  if (phase == SdPhase::LivePlanted) {
    // 1. the defuse: one active defuser; the next holder (lowest slot) starts from zero
    double defusedAt = -1;
    if (defuser >= 0 && !(inp(defuser).alive && inp(defuser).defuseHeld)) { SdEvent e{SdEvent::DefuseCancel}; e.slot = defuser; emit(e); defuser = -1; defuseProgress = 0; }
    if (defuser < 0)
      for (auto& p : players) if (sideOf(p.slot) == 1 && inp(p.slot).alive && inp(p.slot).defuseHeld) {
        defuser = p.slot; defuseProgress = 0; defuseNeeded = p.gear.kit ? cfg.defuseKit : cfg.defuseNoKit;   // fixed at the start
        SdEvent e{SdEvent::DefuseStart}; e.slot = defuser; e.amount = (int)defuseNeeded; emit(e); break;
      }
    if (defuser >= 0) { float before = defuseProgress; defuseProgress += dt; if (defuseProgress >= defuseNeeded) defusedAt = t0 + (defuseNeeded - before); ++rev; }
    // 2. against the detonation: strictly before it; a tie detonates
    if (defusedAt >= 0 && defusedAt < detonateAt) {
      ++players[defuser].defuses; pay(defuser, cfg.defuseBonus, "defuse");
      SdEvent e{SdEvent::Defused}; e.slot = defuser; e.site = bombSite; emit(e);
      endRound(1, SdReason::Defuse); return;
    }
    if (t >= detonateAt) { SdEvent e{SdEvent::Detonated}; e.site = bombSite; emit(e); endRound(0, SdReason::Detonation); return; }
    // 3. eliminations: no defender left wins for the attackers at once; no attacker left does not end it
    if (alive(1) == 0) { endRound(0, SdReason::Elimination); return; }
  }
}

void SdMatch::onKill(int v, int k, SdKill kind, bool teamKill) {
  if (v < 0 || v >= (int)players.size()) return;
  SdPlayer& p = players[v];
  if (carrier == v && bomb == SdBomb::Carried) { bomb = SdBomb::Dropped; bombPos = p.feet; carrier = -1; SdEvent e{SdEvent::BombDropped}; e.slot = v; emit(e); }
  if (planter == v) { planter = -1; plantProgress = 0; }
  p.alive = false;
  if (phase == SdPhase::Warmup) return;
  ++p.deaths; p.gear.armor = 0; p.gear.helmet = p.gear.kit = false; p.gear.grenades.clear();   // death loses armor, kit and grenades (spec §6)
  if (roundWinner >= 0 && p.team != roundWinner) { p.gear.primary.clear(); p.gear.secondary.clear(); }   // SD3: the guns wait for the result: the winning side keeps them (deviation 17)
  if (roundWinner >= 0 || phase == SdPhase::MatchEnd) return;   // latched: no more cash
  if (k < 0 || k >= (int)players.size() || k == v) return;
  if (teamKill) { pay(k, -cfg.teamKillPenalty, "team kill"); return; }
  ++players[k].kills;
  pay(k, cfg.killReward[std::clamp((int)kind, 0, 7)], "kill");
}

void SdMatch::onBombDropped(int s, vec3 at) {
  if (bomb != SdBomb::Carried || carrier != s) return;
  bomb = SdBomb::Dropped; bombPos = at; carrier = -1; planter = -1; plantProgress = 0;
  SdEvent e{SdEvent::BombDropped}; e.slot = s; emit(e);
}
void SdMatch::onBombPickup(int s) {
  if (bomb != SdBomb::Dropped || s < 0 || s >= (int)players.size() || !players[s].alive || !attacker(s)) return;
  bomb = SdBomb::Carried; carrier = s;
  SdEvent e{SdEvent::BombPicked}; e.slot = s; emit(e);
}

// ---------------------------------------------------------------- buying
std::string SdMatch::canBuy(int s, const std::string& id, bool inZone) const {
  if (s < 0 || s >= (int)players.size()) return "no such player";
  const SdPlayer& p = players[s];
  const SdItem* it = cfg.item(id); if (!it) return "not in the shop";
  if (!buyOpen()) return roundWinner >= 0 ? "round over" : "buy time is over";
  if (!p.alive) return "dead";
  if (!inZone) return "outside your buy zone";
  const int side = sideOf(s), price = it->price[side];
  if (price < 0) return std::string("not sold to ") + cfg.factionName[side];
  if (p.cash < price) return "not enough money";
  const SdGear& g = p.gear;
  int boughtSame = 0; for (auto& b : p.bought) boughtSame += b.item == id;
  if (it->slot == 1 && g.primary == it->weapon) return "already owned";
  if (it->slot == 2 && g.secondary == it->weapon) return "already owned";
  if (it->slot == 4) {
    int same = (int)std::count(g.grenades.begin(), g.grenades.end(), it->weapon);
    if ((int)g.grenades.size() >= 4) return "four grenades carried";
    if (same >= it->maxCarry) return "carrying the most allowed";
    if (boughtSame >= it->maxPerRound) return "round limit reached";
  }
  if (id == "vest" && g.armor >= 100) return "armor is full";
  if (id == "helmet" && (g.helmet || g.armor <= 0)) return g.helmet ? "already have a helmet" : "needs armor first";
  if (id == "vesthelmet" && g.helmet) return g.armor >= 100 ? "already have both" : "buy the vest to repair armor";
  if (id == "kit" && g.kit) return "already have a kit";
  return "";
}

bool SdMatch::buy(int s, const std::string& id, bool inZone) {
  if (!canBuy(s, id, inZone).empty()) return false;
  SdPlayer& p = players[s]; const SdItem* it = cfg.item(id); const int price = it->price[sideOf(s)];
  SdPlayer::Purchase rec; rec.item = id; rec.price = price; rec.before = p.gear;
  if (it->slot == 1) p.gear.primary = it->weapon;
  else if (it->slot == 2) p.gear.secondary = it->weapon;
  else if (it->slot == 4) p.gear.grenades.push_back(it->weapon);
  else if (id == "vest") p.gear.armor = 100;
  else if (id == "helmet") p.gear.helmet = true;
  else if (id == "vesthelmet") { p.gear.armor = 100; p.gear.helmet = true; }
  else if (id == "kit") p.gear.kit = true;
  p.bought.push_back(rec);
  pay(s, -price, "buy");
  return true;
}

bool SdMatch::refund(int s, const std::string& id, bool inZone) {
  if (s < 0 || s >= (int)players.size() || !buyOpen() || !inZone || !players[s].alive) return false;
  SdPlayer& p = players[s];
  for (int k = (int)p.bought.size() - 1; k >= 0; --k) {
    auto& b = p.bought[k];
    if (b.item != id) continue;
    if (!b.refundable || k != (int)p.bought.size() - 1) return false;   // used / dropped, or a later purchase depends on the state (ambiguous)
    const SdItem* it = cfg.item(id); if (!it) return false;
    if (it->slot == 4) { auto f = std::find(p.gear.grenades.begin(), p.gear.grenades.end(), it->weapon); if (f == p.gear.grenades.end()) return false; p.gear.grenades.erase(f); }
    else if (it->slot == 1 || it->slot == 2) { if ((it->slot == 1 ? p.gear.primary : p.gear.secondary) != it->weapon) return false; (it->slot == 1 ? p.gear.primary : p.gear.secondary) = it->slot == 1 ? b.before.primary : b.before.secondary; }
    else { if (id != "kit" && p.gear.armor < 100) return false;   // damaged armor: rollback ambiguous
      p.gear.armor = b.before.armor; p.gear.helmet = b.before.helmet; p.gear.kit = b.before.kit; }
    int price = b.price; p.bought.erase(p.bought.begin() + k);
    pay(s, price, "refund");
    return true;
  }
  return false;
}

void SdMatch::onPickedWeapon(int s, const std::string& w, int slotKind) {
  if (s < 0 || s >= (int)players.size()) return;
  SdPlayer& p = players[s];
  for (auto& b : p.bought) { const SdItem* it = cfg.item(b.item); if (it && it->slot == slotKind) b.refundable = false; }
  if (slotKind == 1) p.gear.primary = w; else if (slotKind == 2) p.gear.secondary = w;
  else if (slotKind == 4 && (int)p.gear.grenades.size() < 4) p.gear.grenades.push_back(w);
  ++rev;
}
void SdMatch::onUsedGrenade(int s, const std::string& w) {
  if (s < 0 || s >= (int)players.size()) return;
  SdPlayer& p = players[s]; auto f = std::find(p.gear.grenades.begin(), p.gear.grenades.end(), w);
  if (f != p.gear.grenades.end()) p.gear.grenades.erase(f);
  for (auto& b : p.bought) { const SdItem* it = cfg.item(b.item); if (it && it->weapon == w) b.refundable = false; }
  ++rev;
}

// ---------------------------------------------------------------- replication
json SdMatch::stateJson() const {
  json pl = json::array();
  for (auto& p : players) pl.push_back({p.cash, p.gear.armor, p.gear.helmet, p.gear.kit, p.kills, p.deaths, p.gear.primary, p.gear.secondary, p.gear.grenades});
  return {{"ph", (int)phase}, {"t", t}, {"pe", phaseEnd}, {"ls", liveStart}, {"rd", roundDeadline}, {"da", detonateAt}, {"r", round},
          {"sc", {score[0], score[1]}}, {"at", attackerTeam}, {"li", {lossIdx[0], lossIdx[1]}}, {"ot", overtime}, {"ots", {otScore[0], otScore[1]}},
          {"b", (int)bomb}, {"c", carrier}, {"bp", tojson(bombPos)}, {"bs", bombSite}, {"pl", planter}, {"pp", plantProgress},
          {"df", defuser}, {"dp", defuseProgress}, {"dn", defuseNeeded}, {"rw", roundWinner}, {"rr", (int)reason}, {"mw", matchWinner}, {"p", pl}};
}

void SdMatch::fromJson(const json& j) {
  phase = (SdPhase)j.value("ph", 0); t = j.value("t", 0.0); phaseEnd = j.value("pe", 0.0); liveStart = j.value("ls", 0.0); roundDeadline = j.value("rd", 0.0); detonateAt = j.value("da", 0.0);
  round = j.value("r", 0); attackerTeam = j.value("at", 1); overtime = j.value("ot", false);
  if (j.contains("sc")) { score[0] = j["sc"][0]; score[1] = j["sc"][1]; }
  if (j.contains("li")) { lossIdx[0] = j["li"][0]; lossIdx[1] = j["li"][1]; }
  if (j.contains("ots")) { otScore[0] = j["ots"][0]; otScore[1] = j["ots"][1]; }
  bomb = (SdBomb)j.value("b", 0); carrier = j.value("c", -1); bombPos = jvec3(j["bp"]); bombSite = j.value("bs", -1); planter = j.value("pl", -1); plantProgress = j.value("pp", 0.0f);
  defuser = j.value("df", -1); defuseProgress = j.value("dp", 0.0f); defuseNeeded = j.value("dn", 10.0f); roundWinner = j.value("rw", -1); reason = (SdReason)j.value("rr", 0); matchWinner = j.value("mw", -1);
  if (j.contains("p") && j["p"].is_array()) {
    if (players.size() < j["p"].size()) players.resize(j["p"].size());
    for (size_t i = 0; i < j["p"].size(); ++i) { const json& q = j["p"][i]; SdPlayer& p = players[i]; p.slot = (int)i;
      p.cash = q[0]; p.gear.armor = q[1]; p.gear.helmet = q[2]; p.gear.kit = q[3]; p.kills = q[4]; p.deaths = q[5]; p.gear.primary = q[6]; p.gear.secondary = q[7]; p.gear.grenades = q[8].get<std::vector<std::string>>(); }
  }
  ++rev;
}

// ---------------------------------------------------------------- self-check (pure)
std::vector<std::string> sdLogicSelfCheck() {
  std::vector<std::string> bad;
  auto fail = [&](bool ok, const std::string& what) { if (!ok) bad.push_back(what); };
  auto fresh = [](SdMatch& m) {
    m.cfg = SdConfig{}; m.cfg.shop = {{"ak", "AK", "rifle", "modern_ak", {2700, -1}, 1}, {"vest", "Vest", "equipment", "", {650, 650}, 0}, {"kit", "Kit", "equipment", "", {-1, 400}, 0},
                                       {"flash", "Flash", "grenade", "flash", {200, 200}, 4, 2, 2}};
    std::vector<int> team = {0, 0, 1, 1}; m.setup(4, team, false);   // slots 0, 1: team 0 (defends first); 2, 3: team 1 (attacks)
  };
  auto run = [](SdMatch& m, std::vector<SdInput>& in, double until) { while (m.t + 1e-9 < until) { m.update(0.05f, in); if (m.phase == SdPhase::RoundEnd || m.phase == SdPhase::MatchEnd) break; } };
  auto everyone = [](int n) { std::vector<SdInput> in(n); for (auto& i : in) i.alive = true; return in; };
  auto toLive = [&](SdMatch& m, std::vector<SdInput>& in) { while (m.phase != SdPhase::LiveUnplanted) m.update(0.05f, in); };
  {   // a plant finishing just before the deadline starts a full fuse; one started with 2 s left loses on time
    SdMatch m; fresh(m); auto in = everyone(4); toLive(m, in);
    int c = m.carrier; fail(c == 2 || c == 3, "the bomb goes to an attacker");
    run(m, in, m.roundDeadline - 3.3);
    in[c].plantHeld = true; in[c].plantSite = 0; run(m, in, m.roundDeadline + 0.5);
    fail(m.phase == SdPhase::LivePlanted && std::fabs((m.detonateAt - m.roundDeadline) - 39.9) < 0.2, "a plant 0.1 s before the deadline: planted, a full fuse (" + std::to_string(m.detonateAt - m.roundDeadline) + ")");
    SdMatch m2; fresh(m2); auto in2 = everyone(4); toLive(m2, in2); int c2 = m2.carrier;
    run(m2, in2, m2.roundDeadline - 2.0); in2[c2].plantHeld = true; in2[c2].plantSite = 1; run(m2, in2, m2.roundDeadline + 1);
    fail(m2.roundWinner == 0 && m2.reason == SdReason::Time, "a plant started 2 s before the deadline loses on time");
    fail(m2.players[c2].cash == 800 && m2.players[c2 == 2 ? 3 : 2].cash == 800, "alive attackers at the timeout get no loss pay");
    fail(m2.players[0].cash == 800 + 3250, "defenders win on time: 3250");
  }
  {   // planted: the last attacker dying does not end it; a 5 s kit defuse wins; the planted-and-lost bonus once
    SdMatch m; fresh(m); auto in = everyone(4); toLive(m, in); int c = m.carrier;
    in[c].plantHeld = true; in[c].plantSite = 0; run(m, in, m.t + 3.3); in[c].plantHeld = false;
    fail(m.phase == SdPhase::LivePlanted && m.players[c].cash == 1100, "plant in 3.2 s pays 300");
    in[2].alive = in[3].alive = false; m.onKill(2, 0, SdKill::Firearm, false); m.onKill(3, 0, SdKill::Firearm, false); m.update(0.05f, in);
    fail(m.phase == SdPhase::LivePlanted, "all attackers dead after the plant: the round goes on");
    m.players[1].gear.kit = true; in[1].defuseHeld = true; double s0 = m.t; run(m, in, m.t + 6);
    fail(m.roundWinner == 0 && m.reason == SdReason::Defuse && std::fabs(m.t - s0 - 5.0) < 0.11, "a kit defuse takes 5 s and wins");
    fail(m.players[2].cash == 800 + 1900 + 800 + (c == 2 ? 300 : 0), "planted but lost: loss pay plus 800 (" + std::to_string(m.players[2].cash) + ")");
    fail(m.lossIdx[1] == 2 && m.lossIdx[0] == 0, "loss index: loser 1 -> 2, winner 1 -> 0");
  }
  {   // a defuse finishing exactly at the detonation: the bomb wins; the last defender dying after a plant: attackers, bomb cancelled
    SdMatch m; fresh(m); auto in = everyone(4); toLive(m, in); int c = m.carrier;
    in[c].plantHeld = true; in[c].plantSite = 1; run(m, in, m.t + 3.3); in[c].plantHeld = false;
    run(m, in, m.detonateAt - 10.0); in[0].defuseHeld = true; run(m, in, m.detonateAt + 1);
    fail(m.roundWinner == 1 && m.reason == SdReason::Detonation, "a no-kit defuse ending at (or just after) the detonation loses to the bomb");
    SdMatch m2; fresh(m2); auto in2 = everyone(4); toLive(m2, in2); int c2 = m2.carrier;
    in2[c2].plantHeld = true; in2[c2].plantSite = 0; run(m2, in2, m2.t + 3.3); in2[c2].plantHeld = false;
    in2[0].alive = in2[1].alive = false; m2.update(0.05f, in2);
    fail(m2.roundWinner == 1 && m2.reason == SdReason::Elimination && m2.bomb == SdBomb::None, "last defender dead after the plant: attacker elimination win, bomb cancelled");
    run(m2, in2, m2.t + 50); fail(m2.reason == SdReason::Elimination, "the latched result stays");
  }
  {   // loss ladder 1900, 2400; a win steps the index down; simultaneous wipe before a plant: defenders
    SdMatch m; fresh(m); auto in = everyone(4); toLive(m, in);
    fail(m.lossPayout(1) == 1900, "opening loss pays 1900");
    in[0].alive = in[1].alive = false; m.update(0.05f, in);   // attackers win by elimination
    fail(m.roundWinner == 1 && m.players[0].cash == 800 + 1900 && m.players[2].cash == 800 + 3250, "elimination win 3250, loss 1900");
    fail(m.lossPayout(0) == 2400, "second loss would pay 2400");
    SdMatch m2; fresh(m2); auto in2 = everyone(4); toLive(m2, in2);
    for (auto& i : in2) i.alive = false; m2.update(0.05f, in2);
    fail(m2.roundWinner == 0 && m2.reason == SdReason::Elimination, "simultaneous wipe before a plant: defenders win");
  }
  {   // MR15: swap after 15, first to 16 ends; 15-15 goes to overtime with $10,000
    SdMatch m; fresh(m); auto in = everyone(4);
    for (int r = 0; r < 30 && m.phase != SdPhase::MatchEnd; ++r) {
      while (m.phase != SdPhase::LiveUnplanted) m.update(0.25f, in);
      int lose = r % 2;   // the losing team alternates: 15-15
      for (auto& p : m.players) in[p.slot].alive = p.team != lose;
      m.update(0.05f, in); for (auto& i : in) i.alive = true;
      if (r == 14) { while (m.phase == SdPhase::RoundEnd) m.update(0.25f, in); fail(m.phase == SdPhase::Halftime && m.attackerTeam == 0, "halftime after round 15 swaps sides"); fail(m.players[0].cash == 800, "halftime resets cash to 800"); }
    }
    while (m.phase == SdPhase::RoundEnd) m.update(0.25f, in);
    fail(m.overtime && m.phase == SdPhase::OvertimeBreak && m.players[0].cash == 10000, "15-15: overtime with $10,000 (" + std::string(sdPhaseName(m.phase)) + ")");
  }
  {   // buying: zone, allowance, faction
    SdMatch m; fresh(m); auto in = everyone(4); m.update(0.05f, in); m.update(0.05f, in); m.players[2].cash = 5000;   // freeze
    fail(!m.canBuy(2, "ak", false).empty() && m.canBuy(2, "ak", true).empty(), "buying needs the buy zone");
    fail(!m.canBuy(0, "ak", true).empty(), "the AK is not sold to the defenders");
    fail(!m.canBuy(2, "kit", true).empty() && m.canBuy(0, "kit", true).empty(), "kits are for the defenders only");
    m.buy(2, "flash", true); m.buy(2, "flash", true); fail(!m.buy(2, "flash", true), "two flashes per round");
    fail(m.refund(2, "flash", true) && m.players[2].cash == 5000 - 200, "a refund restores the price");
  }
  {   // SD3: the winning side keeps its guns, its dead too; the losing side's dead lose theirs; armor goes with a death either way
    SdMatch m; fresh(m); auto in = everyone(4); toLive(m, in);
    for (int s : {0, 2, 3}) { m.players[s].gear.primary = "modern_ak"; m.players[s].gear.armor = 100; }
    in[0].alive = false; m.onKill(0, 2, SdKill::Firearm, false);   // a defender dies first
    in[2].alive = in[3].alive = false; m.onKill(2, 1, SdKill::Firearm, false); m.onKill(3, 1, SdKill::Firearm, false); m.update(0.05f, in);
    fail(m.roundWinner == 0, "carry-over: the defenders win by elimination");
    fail(m.players[0].gear.primary == "modern_ak" && m.players[0].gear.armor == 0, "carry-over: a dead winner keeps its gun, not its armor");
    fail(m.players[2].gear.primary.empty() && m.players[3].gear.primary.empty(), "carry-over: the losers' dead lose their guns");
  }
  return bad;
}
