// SD1 Search and Destroy: the game layer (sd_game.h). The controller (sd_mode.cpp) decides; this file feeds it (alive,
// positions, plant / defuse holds, kills, purchases), applies its decisions to the world (round reset and respawns, the
// kept gear, side swaps, the planted bomb drawn as a pack, the blast) and draws the HUD and the buy menu.
#include "sd_game.h"
#include "sd_mode.h"
#include "game.h"
#include "app.h"
#include "scenario.h"
#include "abilities.h"
#include "net.h"
#include "physics.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace {
SdMatch* gSd = nullptr;
struct Feed { std::string text; double at; };
std::vector<Feed> feed;
std::string banner; double bannerAt = -100;
int botSite = 0;                // the attackers' target site this round (0 A, 1 B)
bool buyMenu = false, bWas = false;
bool holdE = false;             // the local player holds Interact this step (raw key: E is also the lean-right hold)
std::map<int, std::pair<bool, double>> remoteHold;   // LAN: slot -> holding Interact, last request (host)
double lastSend = -1;
int clientBomb = 0, clientRound = 0, clientWinner = -1;   // client edge detection

void say(const std::string& s, Game& g) { banner = s; bannerAt = g.t; logf("sd: %s", s.c_str()); }
void addFeed(const std::string& s, Game& g) { feed.push_back({s, g.t}); if (feed.size() > 6) feed.erase(feed.begin()); }
const Objective* site(Game& g, int k) { return k >= 0 && k < (int)g.match.sc.objectives.size() ? &g.match.sc.objectives[k] : nullptr; }
int siteAt(Game& g, vec3 feet) { for (int k = 0; k < 2; ++k) if (const Objective* o = site(g, k); o && o->contains(feet)) return k; return -1; }
bool knownPistol(const std::string& id) { return id == "luger" || id == "m1911a1" || id == "modern_deagle" || id == "modern_revolver" || id.rfind("modern_pistol", 0) == 0; }
int weaponSlotKind(SdMatch& m, const std::string& id) {   // 1 primary, 2 secondary
  for (auto& it : m.cfg.shop) if (it.weapon == id && (it.slot == 1 || it.slot == 2)) return it.slot;
  if (knownPistol(id) || id == m.cfg.freePistol[0] || id == m.cfg.freePistol[1]) return 2;
  return 1;
}
SdKill killKind(SdMatch& m, const std::string& weapon) {
  for (auto& it : m.cfg.shop) if (it.weapon == weapon && it.slot != 4) return it.kill;
  return SdKill::Firearm;
}
float armorPen(SdMatch& m, const std::string& weapon) {   // the share of a protected hit that reaches health (spec §8 "P"), by the shop role
  for (auto& it : m.cfg.shop) if (it.weapon == weapon) return it.cat == "sniper" ? 0.95f : it.cat == "rifle" || it.cat == "heavy" ? 0.775f : it.cat == "shotgun" ? 0.8f : 0.6f;
  return 0.7f;
}
// the side's faction (uniform) and role follow the side, not the persistent team (spec §2)
void applySides(Game& g) {
  for (auto& rs : g.match.slots) {
    bool att = rs.team == gSd->attackerTeam;
    rs.role = att ? CombatRole::Attacker : CombatRole::Defender;
    rs.faction = g.match.rules.teamFaction[att ? 1 : 0];
    if (rs.id < (int)g.sold.size()) { g.sold[rs.id].role = rs.role; g.sold[rs.id].faction = rs.faction; }
  }
}
// what a survivor keeps (spec §6), read from what it actually carries (picked-up guns, thrown grenades)
void syncGear(Game& g, int slot) {
  SdPlayer& p = gSd->players[slot]; Soldier& s = g.sold[slot];
  std::vector<int> have = s.localPlayer ? g.app.carried : s.carried;
  if (!s.localPlayer && s.weapon >= 0 && std::find(have.begin(), have.end(), s.weapon) == have.end()) have.push_back(s.weapon);
  std::string pri, sec;
  for (int w : have) {
    if (w < 0 || w >= (int)g.app.weaponDefs.size()) continue;
    const WeaponDef& d = *g.app.weaponDefs[w];
    if (d.action == "grenade" || d.action == "melee" || d.action == "tool" || d.equipment()) continue;
    (weaponSlotKind(*gSd, d.id) == 2 ? sec : pri) = d.id;
  }
  if (sec == gSd->cfg.freePistol[gSd->sideOf(slot)]) sec.clear();   // a free pistol is granted again, not kept as bought gear
  p.gear.primary = pri; p.gear.secondary = sec;
  if (s.localPlayer) g.localGrenadeCounts(s, false);
  std::vector<std::string> gr;
  for (int k = 0; k < s.frags; ++k) gr.push_back("mk2_grenade");
  for (int k = 0; k < s.smokes; ++k) gr.push_back("smoke_grenade");
  int mi = g.app.weaponIndex("molotov");
  if (s.localPlayer && mi >= 0 && std::find(g.app.carried.begin(), g.app.carried.end(), mi) != g.app.carried.end() && g.app.wstate["molotov"].loaded > 0) gr.push_back("molotov");
  if (gr.size() > 4) gr.resize(4);
  p.gear.grenades = gr;
}
WeaponState fullAmmo(const WeaponDef& d, uint32_t seed) {
  WeaponState ws; ws.loaded = d.loaded; ws.reserveMax = d.loaded + d.reserve;
  for (int r = d.reserve; r > 0; r -= d.containerSize) ws.containers.push_back(std::min(r, d.containerSize));
  ws.seed = seed | 1; return ws;
}
// the slot's gear onto its soldier: the knife, the kept or free pistol, the primary, the grenades, full ammo (spec §6)
void applyGear(Game& g, int slot) {
  Soldier& s = g.sold[slot]; SdPlayer& p = gSd->players[slot];
  const int side = gSd->sideOf(slot);
  int pri = p.gear.primary.empty() ? -1 : g.app.weaponIndex(p.gear.primary);
  int sec = g.app.weaponIndex(p.gear.secondary.empty() ? gSd->cfg.freePistol[side] : p.gear.secondary);
  int knife = g.app.weaponIndex("knife_bayonet");
  int nHE = 0, nSm = 0; bool molo = false;
  for (auto& w : p.gear.grenades) { if (w == "smoke_grenade") ++nSm; else if (w == "molotov") molo = true; else ++nHE; }
  std::vector<int> pool;
  if (pri >= 0) pool.push_back(pri);
  if (sec >= 0) pool.push_back(sec);
  if (s.localPlayer) {
    if (nHE > 0 && g.fragDef(s.faction) >= 0) pool.push_back(g.fragDef(s.faction));
    if (nSm > 0 && g.smokeDef() >= 0) pool.push_back(g.smokeDef());
    if (molo && g.app.weaponIndex("molotov") >= 0) pool.push_back(g.app.weaponIndex("molotov"));
  }
  if (knife >= 0) pool.push_back(knife);
  s.frags = nHE; s.smokes = nSm;
  if (s.localPlayer) {
    g.app.resetWeapons(); g.app.carried = pool;
    if (!pool.empty()) g.app.selectWeapon(pool[0]);
    s.weapon = g.app.activeWeapon;
    g.localGrenadeCounts(s, true);
  } else {
    s.wsBy.clear(); s.weapon = pri >= 0 ? pri : sec >= 0 ? sec : knife;
    if (s.weapon >= 0) s.ws = fullAmmo(*g.app.weaponDefs[s.weapon], s.rng);
    s.carried = pool;
    for (int w : pool) if (w != s.weapon) s.wsBy[w] = fullAmmo(*g.app.weaponDefs[w], s.rng ^ (uint32_t)w);
  }
  s.reload = 0; s.throwT = -1; s.throwReleased = false;
}
// bots spend their money at the round start (spec §14: budgets): a primary (or save), armor, a kit, a grenade
void botBuy(Game& g, int slot) {
  SdPlayer& p = gSd->players[slot]; const int side = gSd->sideOf(slot);
  auto tryBuy = [&](const std::string& id) { return gSd->buy(slot, id, true); };   // bots: before their gear is applied (no drops)
  if (p.cash <= gSd->cfg.startCash && p.gear.primary.empty()) { if (!tryBuy("vest") && side == 1) tryBuy("kit"); return; }   // pistol round
  if (p.gear.primary.empty()) {
    std::vector<std::pair<int, std::string>> pr;
    for (auto& it : gSd->cfg.shop) if (it.slot == 1 && it.price[side] >= 0 && it.cat != "heavy" && it.kill != SdKill::SniperHeavy) pr.push_back({it.price[side], it.id});
    std::sort(pr.rbegin(), pr.rend());
    for (auto& [price, id] : pr) if (p.cash - price >= 650 || (p.cash >= price && p.cash < 2000)) { if (tryBuy(id)) break; }   // money left for armor when it can
  }
  if (!tryBuy("vesthelmet")) tryBuy("vest");
  if (side == 1) tryBuy("kit");
  if (p.cash >= 300) tryBuy((slot + gSd->round) % 2 ? "smoke" : "he");
}
// a purchase through the controller, the gear onto the soldier, and a replaced primary / secondary dropped at the buyer's
// feet (spec §10; a gun is never deleted silently: the drop is a normal pickup)
bool buyAndApply(Game& g, int slot, const std::string& id, bool zone) {
  SdGear before = gSd->players[slot].gear;
  if (!gSd->buy(slot, id, zone)) return false;
  const SdGear& now = gSd->players[slot].gear; Soldier& s = g.sold[slot];
  for (const std::string* old : {&before.primary, &before.secondary})
    if (!old->empty() && *old != now.primary && *old != now.secondary)
      if (int w = g.app.weaponIndex(*old); w >= 0) g.drops.push_back({s.feet + vec3(0, 0.06f, 0), s.yaw, w, fullAmmo(*g.app.weaponDefs[w], 7u), g.t});
  applyGear(g, slot);
  return true;
}
bool inZone(Game& g, int slot) {
  const Soldier& s = g.sold[slot];
  for (auto& m : g.match.sc.spawns) if (m.team < 0 && m.role == s.role && glm::length(vec2(m.pos.x - s.feet.x, m.pos.z - s.feet.z)) <= gSd->cfg.buyRadius && std::fabs(m.pos.y - s.feet.y) < 3.0f) return true;
  return false;
}
vec3 snapNav(Game& g, vec3 p, float r) { NavRef ref; vec3 on; return g.nav.closest(p, r, ref, on) ? on : p; }
bool bombVisible(vec3 eye, vec3 at) { Physics::Hit h; vec3 d = at + vec3(0, 0.15f, 0) - eye; float L = glm::length(d); return L < 0.05f || !Physics::castRayEx(eye, d / L, L - 0.05f, h); }
const char* sideName(int side) { return gSd ? gSd->cfg.factionName[std::clamp(side, 0, 1)].c_str() : ""; }
std::string mmss(float s) { int t = (int)std::ceil(std::max(0.0f, s)); char b[16]; std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60); return b; }
}   // namespace

SdMatch* sdMatch() { return gSd; }
bool sdActive() { return gSd != nullptr; }
bool sdFrozen() { return gSd && gSd->frozen(); }
bool sdInBuyZone(Game& g, int slot) { return gSd && slot >= 0 && slot < (int)g.sold.size() && inZone(g, slot); }

// ---------------------------------------------------------------- the map layout (or the temporary one)
void sdPrepareScenario(App& app, Scenario& sc) {
  if (sc.rules.mode != GameMode::SearchDestroy) return;
  int na = 0, nd = 0; for (auto& m : sc.spawns) if (m.team < 0) (m.role == CombatRole::Attacker ? na : nd)++;
  SdConfig cfg = SdConfig::load();
  sc.rules.friendlyFire = cfg.friendlyFire;
  if (sc.objectives.size() == 2 && na >= 5 && nd >= 5) return;   // an authored Search and Destroy map
  // temporary layout (docs/search-and-destroy.md §9): two sites and five spawns a side from what the map has
  std::vector<Objective> sites; vec2 att(1e9f), def(1e9f);
  for (auto& o : sc.objectives) if (sites.size() < 2) sites.push_back(o);
  if (app.doc.templateId == "chateau_town") { att = vec2(-42, -88); def = vec2(-30, 74); }   // A: the chateau forecourt, B: the church graveyard
  else {
    for (auto& m : sc.spawns) { if (m.team == 1 && att.x > 1e8f) att = vec2(m.pos.x, m.pos.z); if (m.team == 0 && def.x > 1e8f) def = vec2(m.pos.x, m.pos.z); }
    for (auto& m : sc.spawns) { if (m.team < 0 && m.role == CombatRole::Attacker && att.x > 1e8f) att = vec2(m.pos.x, m.pos.z); if (m.team < 0 && m.role == CombatRole::Defender && def.x > 1e8f) def = vec2(m.pos.x, m.pos.z); }
  }
  if (sites.size() < 2 || att.x > 1e8f || def.x > 1e8f) { logf("sd: this map has no Search and Destroy layout and none can be made (needs two objectives and team / role spawns)"); return; }
  for (int k = 0; k < 2; ++k) {
    Objective& o = sites[k]; o.order = k; o.shape = CaptureShape::Circle; o.radius = std::min(o.radius, 7.0f); o.polygon.clear();
    o.label = std::string(k == 0 ? "Site A (" : "Site B (") + o.displayName() + ")"; o.id = k == 0 ? "sdA" : "sdB";
  }
  std::vector<SpawnMarker> sp;
  const vec2 off[5] = {{0, 0}, {2.6f, 0}, {-2.6f, 0}, {0, 2.6f}, {0, -2.6f}};
  const vec2 mid = vec2(sites[0].pos.x + sites[1].pos.x, sites[0].pos.z + sites[1].pos.z) * 0.5f;
  for (int side = 0; side < 2; ++side) for (int k = 0; k < 5; ++k) {
    vec2 c = (side == 0 ? att : def) + off[k], to = mid - c;
    SpawnMarker m; m.id = std::string(side == 0 ? "sdatt" : "sddef") + std::to_string(k); m.label = side == 0 ? "Raiders" : "Security";
    m.role = side == 0 ? CombatRole::Attacker : CombatRole::Defender; m.phases = {0, 1}; m.team = -1;
    m.pos = vec3(c.x, app.terrain.height(c.x, c.y), c.y); m.yaw = glm::degrees(std::atan2(-to.x, -to.y)); sp.push_back(m);
  }
  sc.objectives = sites; sc.spawns = sp;
  logf("sd: temporary layout: %s / %s, Raiders at (%.0f, %.0f), Security at (%.0f, %.0f)", sites[0].label.c_str(), sites[1].label.c_str(), att.x, att.y, def.x, def.y);
}

// ---------------------------------------------------------------- lifecycle
void sdBegin(Game& g) {
  sdEnd();
  if (g.match.mode() != GameMode::SearchDestroy) return;
  gSd = new SdMatch(); gSd->cfg = SdConfig::load();
  std::vector<int> team; for (auto& rs : g.match.slots) team.push_back(rs.team);
  if (g.match.rules.sdShort) { gSd->cfg.halfRounds = 12; gSd->cfg.winRounds = 13; }   // the MR12 preset (spec §2)
  gSd->cfg.friendlyFire = g.match.rules.friendlyFire;
  gSd->setup((int)g.match.slots.size(), team, !g.app.selftest && g.match.rules.sdWarmup);
  applySides(g);
  feed.clear(); banner.clear(); buyMenu = false; remoteHold.clear(); lastSend = -1; clientBomb = 0; clientRound = 0; clientWinner = -1;
  gm2RemovePacksNear(vec3(0), 1e9f);
  logf("sd: Search and Destroy, %d players, first to %d (halves of %d), %zu shop items", (int)g.match.slots.size(), gSd->cfg.winRounds, gSd->cfg.halfRounds, gSd->cfg.shop.size());
}
void sdEnd() { delete gSd; gSd = nullptr; }

void sdOnSpawned(Game& g, int slot) {
  if (!gSd || slot < 0 || slot >= (int)gSd->players.size()) return;
  gSd->players[slot].alive = true; gSd->players[slot].feet = g.sold[slot].feet;
  const Soldier& s = g.sold[slot];
  if (!s.human && !s.localPlayer && gSd->buyOpen() && gSd->players[slot].bought.empty()) botBuy(g, slot);   // bots shop as they arrive in spawn
  applyGear(g, slot);
}

// ---------------------------------------------------------------- the host step
void sdStep(Game& g, float dt) {
  if (!gSd || g.isClient) return;
  SdMatch& m = *gSd;
  g.deployReady = true;   // no class / deploy screen in this mode
  if (m.phase == SdPhase::Warmup) for (auto& rs : g.match.slots) if (!rs.alive && !rs.pending && rs.deadFor > 2.0f) rs.pending = true;   // warmup: free respawns
  std::vector<SdInput> in(g.sold.size());
  for (auto& s : g.sold) {
    SdInput& x = in[s.slot]; x.alive = s.alive; x.feet = s.feet;
    if (!s.alive) continue;
    const bool still = s.localPlayer ? glm::length(vec2(s.vel.x, s.vel.z)) < 0.6f : s.speed < 0.35f;
    bool held = s.localPlayer ? holdE : s.human ? (remoteHold.count(s.slot) && remoteHold[s.slot].first && g.t - remoteHold[s.slot].second < 0.5) : sdBotHolding(g, s);
    if (m.carrier == s.slot) { int k = siteAt(g, s.feet); if (held && still && k >= 0) { x.plantHeld = true; x.plantSite = k; } }
    if (m.bomb == SdBomb::Planted && m.sideOf(s.slot) == 1 && held && still && glm::length(s.feet - m.bombPos) <= m.cfg.defuseRange + 0.3f && bombVisible(s.feet + vec3(0, 1.2f, 0), m.bombPos)) x.defuseHeld = true;
    if (m.bomb == SdBomb::Dropped && m.sideOf(s.slot) == 0 && glm::length(s.feet - m.bombPos) < 1.3f) m.onBombPickup(s.slot);   // walking over it picks it up
  }
  m.update(dt, in);
  bool changed = false;
  for (auto& e : m.takeEvents()) {
    changed = true;
    switch (e.type) {
      case SdEvent::RoundStart: {
        for (auto& s : g.sold) if (s.alive && s.slot < (int)m.players.size()) syncGear(g, s.slot);   // survivors keep what they carry
        applySides(g);
        g.drops.clear(); g.gren.clear(); gm2RemovePacksNear(vec3(0), 1e9f);
        for (auto& s : g.sold) {   // everyone back to the side's spawn at full health, the living included
          if (s.alive) { s.alive = false; if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; } }
          auto& rs = g.match.slots[s.slot]; rs.alive = false; rs.pending = true;
        }
        botSite = (int)(((uint32_t)m.round * 2654435761u) >> 7) % 2;
        say("Round " + std::to_string(m.round) + ": " + std::string(sideName(0)) + " attack", g);
        break; }
      case SdEvent::LiveStart: say("Go", g); break;
      case SdEvent::Planted: gm2AddBombVisual(g.app, m.bombPos, m.cfg.fuse); g.app.audio.play("click", m.bombPos, 1.0f);
        say(std::string("Bomb planted at site ") + (e.site == 0 ? "A" : "B"), g); addFeed(g.match.slots[e.slot].name + " planted the bomb", g); break;
      case SdEvent::DefuseStart: g.app.audio.play("click", m.bombPos, 1.0f); break;
      case SdEvent::Defused: gm2RemovePacksNear(m.bombPos, 1.0f); addFeed(g.match.slots[e.slot].name + " defused the bomb", g); break;
      case SdEvent::Detonated: {
        vec3 p = m.bombPos; gm2RemovePacksNear(p, 1.0f);
        g.explosionDamage(p + vec3(0, 0.3f, 0), m.cfg.bombDamage, m.cfg.bombRadius / std::max(0.1f, g.combat["explosion"].value("personnelRadiusFactor", 3.0f)), -1, false);   // personnel only: no crater, no breach (spec §11)
        g.app.fx.explosion(p, vec3(0, 1, 0), "ground", true); g.app.audio.play("shell_hit", p, 1.0f);
        g.app.blastShake = std::max(g.app.blastShake, std::max(0.0f, 1.0f - glm::length(p - g.app.eyePos()) / 60.0f));
        if (gNet && gNet->role == Net::HOST) gNet->hostWorldEvent(json{{"t", "sdboom"}, {"p", tojson(p)}});
        break; }
      case SdEvent::RoundEnd: say(e.text, g); addFeed("Round " + std::to_string(e.amount) + ": " + e.text, g); break;
      case SdEvent::Halftime: applySides(g); say(e.text == "overtime" ? "Overtime: $10,000 each" : "Halftime: the teams swap sides", g); break;
      case SdEvent::MatchEnd: {
        g.match.phase = MatchPhase::Ended; g.match.winner = e.team == 0 ? Winner::Defenders : Winner::Attackers;
        g.match.result = std::string("Team ") + (e.team == 0 ? "A" : "B") + " wins " + std::to_string(m.score[e.team]) + "-" + std::to_string(m.score[1 - e.team]);
        g.match.log("result: " + g.match.result); say(g.match.result, g); break; }
      case SdEvent::BombDropped: if (m.bomb == SdBomb::Dropped) m.bombPos = snapNav(g, m.bombPos, 3.0f); break;
      default: break;
    }
  }
  if (gNet && gNet->role == Net::HOST && (changed || g.t - lastSend > 0.5)) { lastSend = g.t; gNet->hostWorldEvent(json{{"t", "sd"}, {"s", m.stateJson()}}); }
}

void sdOnKill(Game& g, int victim, int killer, const std::string& how) {
  if (!gSd || g.isClient || victim < 0 || victim >= (int)gSd->players.size()) return;
  SdMatch& m = *gSd;
  SdKill kind = SdKill::Firearm;
  if (killer < 0 || killer == victim) kind = SdKill::World;
  else if (how == "melee") kind = SdKill::Knife;
  else if (how == "blast" || how == "fire" || how == "burn") kind = SdKill::Grenade;
  else { const Soldier& k = g.sold[killer]; int w = k.localPlayer ? g.app.activeWeapon : k.weapon; if (w >= 0 && w < (int)g.app.weaponDefs.size()) { kind = killKind(m, g.app.weaponDefs[w]->id); if (g.app.weaponDefs[w]->action == "melee") kind = SdKill::Knife; } }
  const bool tk = killer >= 0 && killer != victim && g.sold[killer].team == g.sold[victim].team;
  const bool carrier = m.carrier == victim;
  m.onKill(victim, killer, kind, tk);
  if (carrier && m.bomb == SdBomb::Dropped) m.bombPos = snapNav(g, g.sold[victim].feet, 3.0f);   // on recoverable ground
  Soldier& v = g.sold[victim];
  if (v.localPlayer && m.phase != SdPhase::Warmup)   // the local player's main gun falls too (bots drop theirs in killSoldier)
    for (int w : g.app.carried) if (w >= 0 && weaponSlotKind(m, g.app.weaponDefs[w]->id) == 1 && g.app.weaponDefs[w]->action != "melee" && g.app.weaponDefs[w]->action != "grenade") { g.drops.push_back({v.feet + vec3(0, 0.06f, 0), v.yaw, w, g.app.wstate[g.app.weaponDefs[w]->id], g.t}); break; }
  if (m.phase != SdPhase::Warmup) addFeed(killer >= 0 && killer != victim ? g.match.slots[killer].name + "  >  " + g.match.slots[victim].name + (tk ? " (team kill)" : "")
                                          : m.bomb == SdBomb::Detonated ? "The bomb  >  " + g.match.slots[victim].name : g.match.slots[victim].name + " died", g);
}

float sdArmor(Game& g, int victim, float D, const char* how, int attacker) {
  if (!gSd || victim < 0 || victim >= (int)gSd->players.size()) return D;
  SdGear& gr = gSd->players[victim].gear;
  std::string h = how ? how : "";
  const bool head = h == "head", legs = h == "leg" || h == "upper thigh", blast = h == "blast";
  if (gr.armor <= 0 || legs || (head && !gr.helmet) || !(blast || head || h == "upper torso" || h == "lower torso" || h == "neck" || h == "arm")) return D;
  std::string w; if (attacker >= 0 && attacker < (int)g.sold.size()) { const Soldier& a = g.sold[attacker]; int wi = a.localPlayer ? g.app.activeWeapon : a.weapon; if (wi >= 0 && wi < (int)g.app.weaponDefs.size()) w = g.app.weaponDefs[wi]->id; }
  const float P = blast ? 0.5f : armorPen(*gSd, w);
  float health = D * P, armorDmg = (D - D * P) * 0.5f;   // spec §8
  if (armorDmg > gr.armor) { health = D - 2.0f * gr.armor; gr.armor = 0; }
  else gr.armor = std::max(0, gr.armor - (int)std::ceil(armorDmg));
  ++gSd->rev;
  return std::max(0.0f, health);
}

// ---------------------------------------------------------------- input (local player)
void sdInput(App& app, ExploreInput& in) {
  if (!gSd || !gGame) return;
  SdMatch& m = *gSd; Game& g = *gGame;
  { static bool gWas = false; bool gk = glfwGetKey(app.win, GLFW_KEY_G) == GLFW_PRESS;   // G: drop the bomb (spec §5; the only droppable item here)
    if (gk && !gWas && !app.selftest && g.local() && g.local()->alive && m.carrier == g.localSlot) {
      if (g.isClient && gNet) gNet->clientRequest(json{{"t", "sddrop"}});
      else { vec3 f = g.local()->feet + vec3(-std::sin(radians(app.eyeYaw)), 0, -std::cos(radians(app.eyeYaw))) * 0.8f; m.onBombDropped(g.localSlot, snapNav(g, f, 3.0f)); }
    }
    gWas = gk; }
  bool b = glfwGetKey(app.win, GLFW_KEY_B) == GLFW_PRESS;
  if (b && !bWas && !app.selftest) buyMenu = !buyMenu;
  bWas = b;
  int ki = app.bk("interact");
  bool rawE = !app.selftest && ki >= 0 && glfwGetKey(app.win, ki) == GLFW_PRESS;
  Soldier* L = g.local();
  const bool moving = glm::length(in.move) > 0.01f || in.jump;
  bool context = false;
  if (L && L->alive) {
    if (m.carrier == L->slot && siteAt(g, L->feet) >= 0) context = true;
    if (m.bomb == SdBomb::Planted && m.sideOf(L->slot) == 1 && glm::length(L->feet - m.bombPos) <= m.cfg.defuseRange + 0.3f) context = true;
  }
  holdE = rawE && context && !moving;
  if (context && rawE) in.leanR = in.leanL = false;   // E means plant / defuse here, not lean
  if (holdE) { in.fire = in.aim = false; in.move = vec2(0); }   // planting / defusing locks movement and weapons
  if (g.isClient && gNet) { static bool sent = false; static double at = 0; if (holdE != sent || (holdE && g.t - at > 0.2)) { sent = holdE; at = g.t; gNet->clientRequest(json{{"t", "sdhold"}, {"h", holdE}}); } }
  in.sprint = false; in.prone = false;   // spec §11: no sprint, no prone
  if (m.frozen() || m.phase == SdPhase::MatchEnd) { in.move = vec2(0); in.fire = in.aim = in.jump = false; in.quickGrenade = false; }
}

// ---------------------------------------------------------------- bots
bool sdBotHolding(Game& g, const Soldier& s) {
  if (!gSd || !s.alive) return false;
  SdMatch& m = *gSd;
  if (m.carrier == s.slot && siteAt(g, s.feet) >= 0) return true;   // in the plant area: plant
  if (m.bomb == SdBomb::Planted && m.sideOf(s.slot) == 1 && glm::length(s.feet - m.bombPos) <= m.cfg.defuseRange) return true;
  return false;
}

bool sdBotGoal(Game& g, const Soldier& s, vec3& goal) {
  if (!gSd) return false;
  SdMatch& m = *gSd;
  if (m.phase == SdPhase::Warmup) return false;
  if (m.frozen() || m.phase == SdPhase::RoundEnd || m.phase == SdPhase::MatchEnd) { goal = s.feet; return true; }
  const float a = s.slot * 2.39996f; auto ring = [&](vec3 c, float r) { return snapNav(g, c + vec3(std::cos(a) * r, 0, std::sin(a) * r), 4.0f); };
  const Objective* tgt = site(g, botSite);
  if (m.sideOf(s.slot) == 0) {   // Raiders
    if (m.bomb == SdBomb::Dropped) {   // the nearest one fetches it; the others go on
      int best = -1; float bd = 1e9f; for (auto& o : g.sold) if (o.alive && m.sideOf(o.slot) == 0) { float d = glm::length(o.feet - m.bombPos); if (d < bd) { bd = d; best = o.slot; } }
      if (best == s.slot) { goal = m.bombPos; return true; }
    }
    if (m.bomb == SdBomb::Planted) { goal = ring(m.bombPos, 7.0f + (s.slot % 3) * 2.0f); return true; }   // guard the bomb
    if (!tgt) return false;
    if (m.carrier == s.slot) { goal = siteAt(g, s.feet) >= 0 ? s.feet : snapNav(g, tgt->pos, 3.0f); return true; }   // in a plant area: plant here
    goal = ring(tgt->pos, 6.0f + (s.slot % 4) * 2.0f); return true;
  }
  if (m.bomb == SdBomb::Planted) {   // Security: the nearest one to the bomb defuses, the rest cover it
    int best = -1; float bd = 1e9f; for (auto& o : g.sold) if (o.alive && m.sideOf(o.slot) == 1) { float d = glm::length(o.feet - m.bombPos); if (d < bd) { bd = d; best = o.slot; } }
    goal = best == s.slot ? m.bombPos : ring(m.bombPos, 5.0f);
    return true;
  }
  int k = 0, n = 0; for (auto& o : g.sold) if (m.sideOf(o.slot) == 1) { if (o.slot == s.slot) k = n; ++n; }
  const Objective* hold = site(g, k % 2);   // split over both sites
  if (!hold) return false;
  if (!hold->defenseSlots.empty()) { const DefenseSlot& d = hold->defenseSlots[(size_t)(k / 2 + s.slot) % hold->defenseSlots.size()]; goal = snapNav(g, d.pos, 3.0f); return true; }
  goal = ring(hold->pos, 9.0f); return true;
}

// ---------------------------------------------------------------- LAN
bool sdHostRequest(Game& g, int slot, const json& req) {
  if (!gSd) return false;
  std::string t = req.value("t", "");
  if (t == "sdhold") { remoteHold[slot] = {req.value("h", false), g.t}; return true; }
  if (t == "sdbuy") { buyAndApply(g, slot, req.value("i", ""), inZone(g, slot)); return true; }
  if (t == "sddrop") { if (gSd->carrier == slot) gSd->onBombDropped(slot, snapNav(g, g.sold[slot].feet, 3.0f)); return true; }
  return false;
}

bool sdClientEvent(App& app, const json& ev) {
  std::string t = ev.value("t", "");
  if (t != "sd" && t != "sdboom") return false;
  if (t == "sdboom") { vec3 p = jvec3(ev["p"]); app.fx.explosion(p, vec3(0, 1, 0), "ground", true); app.audio.play("shell_hit", p, 1.0f); return true; }
  if (!gSd) { gSd = new SdMatch(); gSd->cfg = SdConfig::load(); }
  gSd->fromJson(ev["s"]);
  return true;
}

void sdClientStep(Game& g, float dt) {
  (void)dt;
  if (!gSd || !g.isClient) return;
  SdMatch& m = *gSd;
  if ((int)m.bomb != clientBomb) {
    if (m.bomb == SdBomb::Planted) { gm2AddBombVisual(g.app, m.bombPos, (float)std::max(1.0, m.detonateAt - m.t)); say(std::string("Bomb planted at site ") + (m.bombSite == 0 ? "A" : "B"), g); }
    else if (clientBomb == (int)SdBomb::Planted) gm2RemovePacksNear(m.bombPos, 1.0f);
    clientBomb = (int)m.bomb;
  }
  if (m.round != clientRound) { clientRound = m.round; say("Round " + std::to_string(m.round), g); }
  if (m.roundWinner != clientWinner) { clientWinner = m.roundWinner; if (m.roundWinner >= 0) say(std::string("Team ") + (m.roundWinner == 0 ? "A" : "B") + " wins the round: " + sdReasonName(m.reason), g); }
}

// ---------------------------------------------------------------- commands
std::string sdCommand(Game& g, const std::string& args) {
  if (!gSd) return "Search and Destroy is not running.";
  SdMatch& m = *gSd;
  if (args == "skip") { m.skipWarmup(); return "Warmup skipped."; }
  if (args == "timeout") { if (g.localSlot < 0) return "No player."; m.requestTimeout(g.sold[g.localSlot].team); return "Timeout requested for the next freeze."; }
  if (args.rfind("buy ", 0) == 0) {
    std::string id = args.substr(4), why = m.canBuy(g.localSlot, id, sdInBuyZone(g, g.localSlot));
    if (!why.empty()) return "Cannot buy " + id + ": " + why;
    buyAndApply(g, g.localSlot, id, true); return "Bought " + id + ".";
  }
  char b[220]; std::snprintf(b, sizeof b, "%s, round %d, Team A %d - %d Team B, Team %s attacks, bomb %d, clock %.1f", sdPhaseName(m.phase), m.round, m.score[0], m.score[1], m.attackerTeam == 0 ? "A" : "B", (int)m.bomb, m.clock());
  return b;
}

// ---------------------------------------------------------------- HUD and buy menu
void sdDrawHud(Game& g) {
  if (!gSd) return;
  SdMatch& m = *gSd;
  ImDrawList* dl = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  const int me = g.localSlot >= 0 && g.localSlot < (int)m.players.size() ? g.localSlot : -1;
  const int mySide = me >= 0 ? m.sideOf(me) : 0, myTeam = me >= 0 ? m.players[me].team : 0;
  auto text = [&](vec2 p, ImU32 c, const std::string& s, float scale = 1.0f) { dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(p.x, p.y), c, s.c_str()); };
  auto width = [&](const std::string& s, float scale = 1.0f) { return ImGui::CalcTextSize(s.c_str()).x * scale; };
  auto centered = [&](float y, ImU32 c, const std::string& s, float scale = 1.0f) { text(vec2(W * 0.5f - width(s, scale) * 0.5f, y), c, s, scale); };
  const ImU32 cMine = IM_COL32(120, 190, 255, 255), cThem = IM_COL32(255, 150, 110, 255), cWhite = IM_COL32(235, 235, 235, 255), cRed = IM_COL32(255, 80, 70, 255), cGrey = IM_COL32(195, 195, 195, 255), cGold = IM_COL32(255, 220, 120, 255);
  // top: score, sides with alive counts, the round clock
  const float pw = 440, ph = 64, px = W * 0.5f - pw * 0.5f, py = 8;
  dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(12, 14, 16, 165), 6);
  char b[200];
  std::snprintf(b, sizeof b, "Team A  %d", m.score[0]); text(vec2(px + 12, py + 6), myTeam == 0 ? cMine : cThem, b, 1.2f);
  std::snprintf(b, sizeof b, "%d  Team B", m.score[1]); text(vec2(px + pw - 12 - width(b, 1.2f), py + 6), myTeam == 1 ? cMine : cThem, b, 1.2f);
  const int sideA = m.attackerTeam == 0 ? 0 : 1;   // the side Team A plays
  std::snprintf(b, sizeof b, "%s, %d alive", sideName(sideA), m.alive(sideA)); text(vec2(px + 12, py + 36), cGrey, b);
  std::snprintf(b, sizeof b, "%d alive, %s", m.alive(1 - sideA), sideName(1 - sideA)); text(vec2(px + pw - 12 - width(b), py + 36), cGrey, b);
  const bool planted = m.phase == SdPhase::LivePlanted;
  centered(py + 4, planted ? cRed : cWhite, planted ? "BOMB " + mmss(m.clock()) : mmss(m.clock()), 1.5f);
  std::snprintf(b, sizeof b, "round %d%s", m.round, m.overtime ? ", overtime" : ""); centered(py + 40, cGrey, b);
  if (m.phase == SdPhase::Freeze || m.phase == SdPhase::Timeout || m.phase == SdPhase::Warmup || m.phase == SdPhase::Halftime || m.phase == SdPhase::OvertimeBreak || m.phase == SdPhase::Ready)
    centered(py + ph + 8, cGold, std::string(sdPhaseName(m.phase)) + ": " + mmss((float)(m.phaseEnd - m.t)) + (m.phase == SdPhase::Warmup ? "   (/sd skip to start)" : "   (B: buy)"), 1.2f);
  else if (m.buyOpen() && me >= 0 && m.players[me].alive && inZone(g, me)) centered(py + ph + 8, cGold, "Buy time " + mmss((float)(m.liveStart + m.cfg.liveBuy - m.t)) + "   (B)");
  if (!banner.empty() && g.t - bannerAt < 4.0) centered(H * 0.28f, IM_COL32(255, 255, 255, 235), banner, 1.6f);
  // bottom left: money, armor, kit, the bomb
  if (me >= 0) {
    SdPlayer& p = m.players[me];
    const float bx = 16, by = H - 150;
    dl->AddRectFilled(ImVec2(bx - 6, by - 6), ImVec2(bx + 330, by + 74), IM_COL32(12, 14, 16, 165), 6);
    std::snprintf(b, sizeof b, "$%d", p.cash); text(vec2(bx, by), IM_COL32(120, 230, 120, 255), b, 1.4f);
    std::snprintf(b, sizeof b, "%s   armor %d%s%s", sideName(mySide), p.gear.armor, p.gear.helmet ? " + helmet" : "", p.gear.kit ? "   kit" : ""); text(vec2(bx, by + 30), cWhite, b);
    std::string bomb;
    if (mySide == 0) bomb = planted ? std::string("Bomb planted at ") + (m.bombSite == 0 ? "A" : "B") + ": guard it" : m.carrier == me ? "You carry the bomb: hold E in a site (G drops it)" : m.bomb == SdBomb::Dropped ? "The bomb is on the ground" : m.carrier >= 0 ? "Bomb: " + g.match.slots[m.carrier].name : "";
    else if (planted) bomb = std::string("The bomb is planted at site ") + (m.bombSite == 0 ? "A" : "B") + ": hold E at it";
    text(vec2(bx, by + 52), IM_COL32(255, 200, 120, 255), bomb);
    float prog = -1; const char* what = "";
    if (m.planter >= 0 && m.planter == me) { prog = m.plantProgress / m.cfg.plantTime; what = "Planting"; }
    if (m.defuser >= 0 && m.defuser == me) { prog = m.defuseProgress / std::max(0.1f, m.defuseNeeded); what = "Defusing"; }
    if (prog >= 0) {
      const float bw = 300, bh = 14, x0 = W * 0.5f - bw * 0.5f, y0 = H * 0.62f;
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw, y0 + bh), IM_COL32(20, 20, 20, 190), 3);
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * std::clamp(prog, 0.0f, 1.0f), y0 + bh), IM_COL32(255, 200, 80, 230), 3);
      centered(y0 - 22, cWhite, what);
    } else if (m.defuser >= 0 && mySide == 1) centered(H * 0.62f, cWhite, g.match.slots[m.defuser].name + " is defusing");
  }
  // the dropped bomb, for the attackers (spec §5: carrier and dropped bomb shown to attackers)
  if (m.bomb == SdBomb::Dropped && mySide == 0) {
    vec4 c = g.app.projM * g.app.viewM * vec4(m.bombPos + vec3(0, 0.6f, 0), 1);
    if (c.w > 0.1f) { vec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (0.5f - c.y / c.w * 0.5f) * H);
      dl->AddCircle(ImVec2(sp.x, sp.y), 9, IM_COL32(255, 200, 80, 230), 16, 2.5f);
      std::snprintf(b, sizeof b, "BOMB %.0f m", glm::length(m.bombPos - g.app.eyePos())); text(vec2(sp.x - width(b) * 0.5f, sp.y + 12), IM_COL32(255, 200, 80, 230), b); }
  }
  // kill feed, top right under the minimap
  float fy = 300;
  for (auto& f : feed) if (g.t - f.at < 8.0) { float w = width(f.text); dl->AddRectFilled(ImVec2(W - w - 22, fy - 2), ImVec2(W - 8, fy + ImGui::GetFontSize() + 2), IM_COL32(12, 14, 16, 150), 4); text(vec2(W - w - 15, fy), cWhite, f.text); fy += ImGui::GetFontSize() + 8; }
  // the buy menu
  if (buyMenu && me >= 0) {
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660, 520), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.82f);
    bool open = true;
    if (ImGui::Begin("Buy##sd", &open, ImGuiWindowFlags_NoCollapse)) {
      SdPlayer& p = m.players[me]; const bool zone = inZone(g, me);
      ImGui::Text("%s   $%d   %s", sideName(mySide), p.cash, m.buyOpen() ? (zone ? "buying open" : "outside your buy zone") : "buy time is over");
      ImGui::Text("Primary: %s   Secondary: %s   Armor %d%s%s", p.gear.primary.empty() ? "-" : p.gear.primary.c_str(), p.gear.secondary.empty() ? m.cfg.freePistol[mySide].c_str() : p.gear.secondary.c_str(), p.gear.armor, p.gear.helmet ? " + helmet" : "", p.gear.kit ? ", kit" : "");
      ImGui::Separator();
      const char* cats[] = {"pistol", "smg", "shotgun", "rifle", "sniper", "heavy", "grenade", "equipment"};
      const char* catName[] = {"Pistols", "SMGs", "Shotguns", "Rifles", "Snipers", "Heavy", "Grenades", "Equipment"};
      if (ImGui::BeginTabBar("cats")) {
        for (int c = 0; c < 8; ++c) if (ImGui::BeginTabItem(catName[c])) {
          for (auto& it : m.cfg.shop) {
            if (it.cat != cats[c] || it.price[mySide] < 0) continue;
            std::string why = m.canBuy(me, it.id, zone);
            ImGui::BeginDisabled(!why.empty());
            std::snprintf(b, sizeof b, "%s   $%d##%s", it.name.c_str(), it.price[mySide], it.id.c_str());
            if (ImGui::Button(b, ImVec2(400, 0))) {
              if (g.isClient && gNet) gNet->clientRequest(json{{"t", "sdbuy"}, {"i", it.id}});
              else buyAndApply(g, me, it.id, zone);
            }
            ImGui::EndDisabled();
            if (!why.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", why.c_str()); }
          }
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      if (!p.bought.empty()) { ImGui::Separator(); if (ImGui::Button("Refund the last purchase") && !g.isClient && m.refund(me, p.bought.back().item, zone)) applyGear(g, me); }
    }
    ImGui::End();
    if (!open) buyMenu = false;
  }
}
