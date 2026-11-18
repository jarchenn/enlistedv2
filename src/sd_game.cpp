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
#include "ai_core.h"
#include "render.h"
#include "ui_glass.h"
#include "assets.h"
#include "model.h"
#include "mpui.h"
#include "ui_flat.h"
#include "minimap.h"
#include <map>
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
int buyCol = -1;                // SD3: the buy menu column chosen by its number key (-1: none)
void buyMenuKey(Game& g, int k); // SD3: number key k (0-based) in the open buy menu
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
    Objective& o = sites[k]; o.order = k;
    o.shape = CaptureShape::Polygon; o.polygon.clear(); o.yMin = -1.0f; o.yMax = 1.5f;   // SD3: a 3.5 x 3.5 m plant pad (map contract §2); the site itself stays the area around it
    for (vec2 c : {vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1)}) o.polygon.push_back(vec2(o.pos.x, o.pos.z) + c * 1.75f);
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
App* gSdApp = nullptr; bool gSdSpeed = false; App::MoveSettings gSdMove;   // GP1: the player's movement before the mode's speed factor
std::vector<Instance> gSdParked;   // SD3: the map's vehicles while the Vehicles option is off (out of the doc, back at the end)
static void sdRestoreVehicles() {
  if (!gSdApp || gSdParked.empty()) return;
  App& a = *gSdApp;
  for (auto& i : gSdParked) a.doc.instances.push_back(std::move(i));
  gSdParked.clear(); a.syncRuntime(); a.tanks.sync(a);
}
static void sdParkVehicles(Game& g) {   // map-placed vehicles out of the world; none spawn later (no seat to die in, so no fresh copy)
  App& a = g.app; std::vector<std::pair<vec3, vec3>> areas;
  auto& v = a.doc.instances;
  for (size_t k = 0; k < v.size();) {
    if (a.tanks.defFor(a.lib.find(v[k].assetId))) { areas.push_back({v[k].pos - vec3(7, 3, 7), v[k].pos + vec3(7, 4, 7)}); gSdParked.push_back(std::move(v[k])); v.erase(v.begin() + k); }
    else ++k;
  }
  if (gSdParked.empty()) return;
  a.tanks.occupied = 0; a.syncRuntime(); a.tanks.sync(a);
  for (auto& [lo, hi] : areas) g.worldChanged(lo, hi);   // the ground they stood on is walkable now
  logf("sd: %zu vehicles removed for the match (Vehicles option off)", gSdParked.size());
}
const MapDoc& sdDocToSave(const MapDoc& doc, MapDoc& scratch) {
  if (gSdParked.empty()) return doc;
  scratch = doc; for (auto& i : gSdParked) scratch.instances.push_back(i);
  return scratch;
}
// ---------------------------------------------------------------- SD3 the plant pads: on walkable ground, marked on it
// The pad is the objective's own shape (Objective::contains is the plant test): its outline is drawn from that same shape.
struct PadVert { vec3 p; int cls; };   // cls 0 fill, 1 stripe light, 2 stripe dark, 3 bracket, 4 letter
std::vector<PadVert> padMesh[2];
std::vector<vec2> padOutline(const Objective& o) {
  if (o.shape == CaptureShape::Polygon && o.polygon.size() >= 3) return o.polygon;
  std::vector<vec2> c; for (int k = 0; k < 48; ++k) { float a = k * 6.2831853f / 48; c.push_back(vec2(o.pos.x + std::cos(a) * o.radius, o.pos.z + std::sin(a) * o.radius)); }
  return c;
}
std::vector<vec2> sdPadOutline(Game& g, int k) { const Objective* o = site(g, k); return o ? padOutline(*o) : std::vector<vec2>{}; }
void snapPads(Game& g) {   // the pad's centre onto the navmesh (bots must stand in it); host and clients alike (same map, same mesh)
  for (int k = 0; k < 2 && k < (int)g.match.sc.objectives.size(); ++k) {
    Objective& o = g.match.sc.objectives[k]; NavRef ref; vec3 on;
    if (!g.nav.closest(o.pos, vec3(3.0f, 3.0f, 3.0f), ref, on)) continue;
    vec2 d(on.x - o.pos.x, on.z - o.pos.z);
    if (glm::length(d) > 0.3f) { for (auto& q : o.polygon) q += d; logf("sd: site %c pad moved %.1f m onto walkable ground", 'A' + k, glm::length(d)); }
    o.pos = on;
  }
}
void buildPads(Game& g) {
  vec2 att(0); int na = 0; for (auto& m : g.match.sc.spawns) if (m.team < 0 && m.role == CombatRole::Attacker) { att += vec2(m.pos.x, m.pos.z); ++na; }
  if (na) att /= (float)na;
  for (int k = 0; k < 2; ++k) {
    padMesh[k].clear();
    const Objective* o = site(g, k); if (!o) continue;
    const vec2 c(o->pos.x, o->pos.z); const float y0 = o->pos.y;
    auto at = [&](vec2 p, float lift) { Physics::Hit h; float y = y0;
      if (Physics::castRayEx(vec3(p.x, y0 + 1.2f, p.y), vec3(0, -1, 0), 2.6f, h)) y = h.pos.y;
      return vec3(p.x, y + lift, p.y); };
    auto tri = [&](vec2 a, vec2 b, vec2 q, int cls, float lift) { for (vec2 p : {a, b, q}) padMesh[k].push_back({at(p, lift), cls}); };
    auto quad = [&](vec2 a, vec2 b, vec2 q, vec2 d, int cls, float lift) { tri(a, b, q, cls, lift); tri(a, q, d, cls, lift); };
    auto bar = [&](vec2 a, vec2 b, float w, int cls, float lift) { vec2 t = b - a; float L = glm::length(t); if (L < 1e-3f) return; t /= L; vec2 n(-t.y, t.x); n *= w * 0.5f; quad(a - n, b - n, b + n, a + n, cls, lift); };
    std::vector<vec2> ol = padOutline(*o);
    vec2 cen(0); for (auto& p : ol) cen += p; cen /= (float)ol.size();
    for (size_t i = 0; i < ol.size(); ++i) tri(cen, ol[i], ol[(i + 1) % ol.size()], 0, 0.035f);   // the fill (a convex pad)
    // the border: hazard stripes on a 0.22 m band just inside the outline
    int seg = 0;
    for (size_t i = 0; i < ol.size(); ++i) {
      vec2 a = ol[i], b = ol[(i + 1) % ol.size()], t = b - a; float L = glm::length(t); if (L < 1e-3f) continue; t /= L;
      vec2 n(-t.y, t.x); if (glm::dot(n, cen - a) < 0) n = -n;   // inward
      int pieces = std::max(1, (int)std::round(L / 0.3f));
      for (int s = 0; s < pieces; ++s, ++seg) { vec2 p0 = a + t * (L * s / pieces), p1 = a + t * (L * (s + 1) / pieces);
        quad(p0, p1, p1 + n * 0.22f, p0 + n * 0.22f, seg % 2 ? 2 : 1, 0.045f); }
    }
    if (o->shape == CaptureShape::Polygon && ol.size() <= 8)   // corner brackets just outside each corner
      for (size_t i = 0; i < ol.size(); ++i) {
        vec2 p = ol[i], a = glm::normalize(ol[(i + ol.size() - 1) % ol.size()] - p), b = glm::normalize(ol[(i + 1) % ol.size()] - p), out = -glm::normalize(a + b);
        vec2 q = p + out * 0.2f; bar(q, q + a * 0.7f, 0.1f, 3, 0.05f); bar(q, q + b * 0.7f, 0.1f, 3, 0.05f);
      }
    // the site letter, large, upright for an attacker walking in from its spawn
    vec2 up = glm::length(c - att) > 1 && na ? glm::normalize(c - att) : vec2(0, -1), rt(-up.y, up.x);
    const float lh = std::min(2.0f, o->extent() * 1.05f), lw = lh * 0.68f;
    auto P = [&](float x, float y) { return cen + rt * ((x - 0.5f) * lw) + up * ((y - 0.5f) * lh); };
    static const float A[][4] = {{0, 0, 0.5f, 1}, {0.5f, 1, 1, 0}, {0.22f, 0.42f, 0.78f, 0.42f}};
    static const float B[][4] = {{0.05f, 0, 0.05f, 1}, {0.05f, 1, 0.62f, 1}, {0.62f, 1, 0.82f, 0.87f}, {0.82f, 0.87f, 0.82f, 0.65f}, {0.82f, 0.65f, 0.62f, 0.52f}, {0.05f, 0.52f, 0.62f, 0.52f},
                                {0.62f, 0.52f, 0.88f, 0.38f}, {0.88f, 0.38f, 0.88f, 0.14f}, {0.88f, 0.14f, 0.62f, 0}, {0.62f, 0, 0.05f, 0}};
    if (k == 0) for (auto& s : A) bar(P(s[0], s[1]), P(s[2], s[3]), lh * 0.09f, 4, 0.055f);
    else for (auto& s : B) bar(P(s[0], s[1]), P(s[2], s[3]), lh * 0.09f, 4, 0.055f);
  }
}
void drawPads(Game& g) {   // this frame's colours into gWorldMarks (the render pass draws them next frame)
  gWorldMarks.clear();
  if (!gSd) return;
  SdMatch& m = *gSd;
  const int me = g.localSlot;
  const bool carrierHere = me >= 0 && m.carrier == me && g.sold[me].alive;
  const float pulse = 0.5f + 0.5f * std::sin((float)g.t * 6.0f);
  for (int k = 0; k < 2; ++k) {
    const bool glow = carrierHere && siteAt(g, g.sold[me].feet) == k;
    const bool live = m.bomb != SdBomb::Planted || m.bombSite == k;
    const float a = live ? 1.0f : 0.45f;
    const vec4 amber = glow ? vec4(1.0f, 0.86f, 0.35f, 1) : vec4(1.0f, 0.68f, 0.12f, 1);
    for (auto& v : padMesh[k]) {
      vec4 c = v.cls == 0 ? vec4(amber.r, amber.g, amber.b, (glow ? 0.22f + 0.16f * pulse : 0.10f) * a)
             : v.cls == 1 ? vec4(amber.r, amber.g, amber.b, 0.9f * a) : v.cls == 2 ? vec4(0.06f, 0.06f, 0.05f, 0.75f * a)
             : v.cls == 3 ? vec4(0.96f, 0.94f, 0.86f, (glow ? 1.0f : 0.85f) * a) : vec4(amber.r, amber.g, amber.b, (glow ? 0.95f : 0.7f) * a);
      gWorldMarks.tris.push_back({v.p, c});
    }
  }
}
static void sdTiming(SdConfig& c, const ScenarioRules& r) {   // SD8: the match's round clocks over search_destroy.json's
  if (r.sdRoundTime >= 0) c.roundTime = r.sdRoundTime; if (r.sdFreeze >= 0) c.freeze = r.sdFreeze; if (r.sdBuy >= 0) c.liveBuy = r.sdBuy;
  if (r.sdFuse >= 0) c.fuse = r.sdFuse; if (r.sdWarmupTime >= 0) c.warmup = r.sdWarmupTime;
}
bool sdTimingUI(ScenarioRules& r, bool midMatch) {
  static const SdConfig def = SdConfig::load();
  struct Row { const char* label; float* v; float d, lo, hi; };
  const Row rows[] = {{"Round length", &r.sdRoundTime, def.roundTime, 30, 600}, {"Freeze time", &r.sdFreeze, def.freeze, 0, 60},
                      {"Buy time into the round", &r.sdBuy, def.liveBuy, 0, 120}, {"Bomb timer", &r.sdFuse, def.fuse, 10, 120}, {"Warmup length", &r.sdWarmupTime, def.warmup, 10, 300}};
  bool changed = false;
  for (const Row& w : rows) {
    if (midMatch && w.v == &r.sdWarmupTime) continue;
    float v = *w.v >= 0 ? *w.v : w.d; char f[24]; std::snprintf(f, sizeof f, "%d:%02d", (int)v / 60, (int)v % 60);
    if (midMatch ? flat::sliderFloat(w.label, &v, w.lo, w.hi, f) : ImGui::SliderFloat(w.label, &v, w.lo, w.hi, f)) { *w.v = std::round(v); changed = true; }
  }
  return changed;
}
void sdPauseTimingUI(Game& g) {
  if (!gSd || g.isClient) return;
  flat::section("Round clocks (from the next round)");
  if (sdTimingUI(g.match.rules, true)) gUi.rules = g.match.rules;   // kept for the next match / the lobby
}
static void sdClear() { if (gSdApp && gSdSpeed) gSdApp->move = gSdMove; gSdSpeed = false; delete gSd; gSd = nullptr; }
void sdBegin(Game& g) {
  sdEnd();
  if (g.match.mode() != GameMode::SearchDestroy) return;
  gSd = new SdMatch(); gSd->cfg = SdConfig::load(); sdTiming(gSd->cfg, g.match.rules);   // SD8
  std::vector<int> team; for (auto& rs : g.match.slots) team.push_back(rs.team);
  if (g.match.rules.sdShort) { gSd->cfg.halfRounds = 12; gSd->cfg.winRounds = 13; }   // the MR12 preset (spec §2)
  gSd->cfg.friendlyFire = g.match.rules.friendlyFire;
  gSd->setup((int)g.match.slots.size(), team, !g.app.selftest && g.match.rules.sdWarmup);
  gSdApp = &g.app;
  if (!g.match.rules.sdVehicles) sdParkVehicles(g);
  snapPads(g); buildPads(g);
  applySides(g);
  {   // GP1: a brisker mode: everyone's speeds x speed_x (the player's movement, the bots' speed table); restored at the end
    const float k = std::clamp(gSd->cfg.speedX, 0.5f, 2.0f);
    gSdApp = &g.app; gSdMove = g.app.move; gSdSpeed = true;
    g.app.move.run *= k; g.app.move.sprint *= k; g.app.move.crouch *= k; g.app.move.prone *= k;
    if (AiCore* A = aiCore()) { A->vSprint *= k; A->vJog *= k; A->vWalk *= k; A->vCrouch *= k; A->vProne *= k; }
  }
  feed.clear(); banner.clear(); buyMenu = false; remoteHold.clear(); lastSend = -1; clientBomb = 0; clientRound = 0; clientWinner = -1;
  gm2RemovePacksNear(vec3(0), 1e9f);
  logf("sd: Search and Destroy, %d players, first to %d (halves of %d), %zu shop items", (int)g.match.slots.size(), gSd->cfg.winRounds, gSd->cfg.halfRounds, gSd->cfg.shop.size());
}
void sdEnd() { sdClear(); sdRestoreVehicles(); gWorldMarks.clear(); padMesh[0].clear(); padMesh[1].clear(); }

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
      case SdEvent::RoundEnd: sdTiming(m.cfg, g.match.rules); say(e.text, g); addFeed("Round " + std::to_string(e.amount) + ": " + e.text, g); break;
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
  if (v.localPlayer && m.phase != SdPhase::Warmup) {   // SD3: the gun in the local player's hands falls, as a bot's does (killSoldier)
    const int w = g.app.activeWeapon;
    if (w >= 0 && w < (int)g.app.weaponDefs.size()) { const WeaponDef& d = *g.app.weaponDefs[w];
      if (d.action != "melee" && d.action != "grenade" && d.action != "tool" && !d.equipment()) g.drops.push_back({v.feet + vec3(0, 0.06f, 0), v.yaw, w, g.app.wstate[d.id], g.t}); }
  }
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
  static int menuRound = -1;
  if (b && !bWas && !app.selftest) { buyMenu = !buyMenu; buyCol = -1; menuRound = m.round; }
  bWas = b;
  if (buyMenu && !app.selftest && (!m.buyOpen() || m.round != menuRound || !g.local() || !g.local()->alive)) { buyMenu = false; buyCol = -1; }   // buy time over, a new round, dead: the menu closes (the cursor is captured again)
  if (buyMenu) {   // SD3: the menu has the number keys (a column, then an item) and the mouse; no weapon switching or firing
    static bool was[7] = {};
    for (int k = 0; k < 7; ++k) {
      bool d = !app.selftest && glfwGetKey(app.win, k < 6 ? GLFW_KEY_1 + k : GLFW_KEY_BACKSPACE) == GLFW_PRESS;
      if (d && !was[k]) { if (k < 6) buyMenuKey(g, k); else buyCol = -1; }
      was[k] = d;
    }
    in.choose1 = in.choose2 = in.slot3 = in.slot4 = in.nextWeapon = in.quickGrenade = false; in.fire = in.aim = false; in.wheel = 0;
  }
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
  // GP1 (user): sprint and prone both allowed in this mode (the spec turned them off)
  if (m.frozen() || m.phase == SdPhase::MatchEnd) { in.move = vec2(0); in.fire = in.aim = in.jump = false; in.quickGrenade = false; }
}

// ---------------------------------------------------------------- bots (SD3)
// Defenders: about 60 % hunt in pairs (forward along the attackers' approach, onto what the pair has seen), the rest
// hold the two sites' defense slots; everyone converges on a planted bomb, the nearest defuses.
// Attackers: two groups (alternate attackers) take two routes to this round's site (the site's authored routes, else
// two lanes either side of the direct line), wait at their staging points until both groups are there (or the clock
// says go), then execute together. A dropped bomb: the nearest attacker sprints for it at once, contact or not.
// Plant discipline: the carrier plants only with no enemy in view and not under fire; engaged, it lets go (the
// controller resets the plant), fights, and starts again once clear.
namespace {
struct BotRound { int round = -1; std::map<int, int> leg; bool exec = false; double stagedAt = -1; vec3 way[2]{}, stage[2]{}; };
BotRound gBots;
bool enemyInView(const Soldier& s) { for (auto& x : s.br.mem) if (x.visible && !x.heardOnly) return true; return false; }
vec3 attackersHome(Game& g) { vec3 c(0); int n = 0; for (auto& m : g.match.sc.spawns) if (m.team < 0 && m.role == CombatRole::Attacker) { c += m.pos; ++n; } return n ? c / (float)n : vec3(0); }
void planRound(Game& g) {   // the two routes of this round's site
  SdMatch& m = *gSd;
  if (gBots.round == m.round) return;
  gBots = BotRound{}; gBots.round = m.round;
  const Objective* o = site(g, botSite); if (!o) return;
  const vec3 home = attackersHome(g), c = o->pos; vec3 d = c - home; d.y = 0; const float L = glm::length(d); if (L < 1) return; d /= L;
  const vec3 side(-d.z, 0, d.x);
  for (int k = 0; k < 2; ++k) {
    vec3 w;
    if ((int)o->routes.size() >= 2 && !o->routes[k].points.empty()) w = o->routes[k].points[o->routes[k].points.size() / 2];   // authored lanes
    else w = home + d * (L * 0.5f) + side * ((k ? -1.0f : 1.0f) * std::max(14.0f, L * 0.28f));
    gBots.way[k] = snapNav(g, w, 8.0f);
    vec3 sd = gBots.way[k] - c; sd.y = 0; sd = glm::length(sd) > 1 ? glm::normalize(sd) : -d;
    gBots.stage[k] = snapNav(g, c + sd * 16.0f, 6.0f);
  }
}
int attackerIndex(SdMatch& m, Game& g, int slot) { int k = 0; for (auto& o : g.sold) if (m.sideOf(o.slot) == 0) { if (o.slot == slot) return k; ++k; } return 0; }
int fetcher(SdMatch& m, Game& g) {
  if (m.bomb != SdBomb::Dropped) return -1;
  int best = -1; float bd = 1e9f; for (auto& o : g.sold) if (o.alive && m.sideOf(o.slot) == 0) { float d = glm::length(o.feet - m.bombPos); if (d < bd) { bd = d; best = o.slot; } }
  return best;
}
}   // namespace

bool sdBotFetching(Game& g, const Soldier& s) { return gSd && s.alive && fetcher(*gSd, g) == s.slot; }

// SD5: the attacker execute must not stall (the plant-rate harness, --selftest-sd-plant on Sun Fort clutter variants: in
// the rounds lost on time the carrier sat 5-13 m from a pad in a firefight from cover for 45 s+, and flankers stood
// "cover to cover" for 20-50 s with no target). A pushing attacker drops cover-to-cover and goes straight for its goal:
//  - the carrier within 30 m of a pad, or with under 35 s on the clock;
//  - any attacker without a target that has moved under 1.5 m in 8 s (for the next 6 s).
namespace { std::map<int, std::pair<vec3, double>> gStill; std::map<int, double> gUnstickUntil, gOnPadSince; }
bool sdBotPushing(Game& g, const Soldier& s) {
  if (!gSd || !s.alive) return false;
  SdMatch& m = *gSd;
  if (m.phase != SdPhase::LiveUnplanted || m.sideOf(s.slot) != 0) { gStill.erase(s.slot); return false; }
  if (m.carrier == s.slot) {
    if (m.clock() < 35.0f) return true;
    for (int k = 0; k < 2; ++k) if (const Objective* o = site(g, k); o && glm::length(s.feet - o->pos) < 30.0f) return true;
  }
  auto& st = gStill[s.slot];
  if (glm::length(s.feet - st.first) > 1.5f) st = {s.feet, g.t};
  else if (s.br.target < 0 && g.t - st.second > 8.0) { gUnstickUntil[s.slot] = g.t + 6.0; st = {s.feet, g.t}; }
  return g.t < gUnstickUntil[s.slot];
}

bool sdBotHolding(Game& g, const Soldier& s) {
  if (!gSd || !s.alive) return false;
  SdMatch& m = *gSd;
  if (m.carrier == s.slot && siteAt(g, s.feet) >= 0) {   // on the pad: plant, unless engaged (SD5: discipline times out: 3 s on the pad and not hit for 1 s, or under 35 s left)
    auto it = gOnPadSince.find(s.slot); if (it == gOnPadSince.end() || g.t - it->second > 1e6) it = gOnPadSince.insert_or_assign(s.slot, g.t).first;
    const bool clear = !enemyInView(s) && g.t - s.br.hurtAt > 2.0;
    return clear || (g.t - it->second > 3.0 && g.t - s.br.hurtAt > 1.0) || m.clock() < 35.0f;
  }
  if (m.carrier == s.slot) gOnPadSince.erase(s.slot);
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
    if (fetcher(m, g) == s.slot) { goal = m.bombPos; return true; }
    if (m.bomb == SdBomb::Planted) { goal = ring(m.bombPos, 7.0f + (s.slot % 3) * 2.0f); return true; }   // guard the bomb
    if (!tgt) return false;
    planRound(g);
    if (m.carrier == s.slot && siteAt(g, s.feet) >= 0) { goal = s.feet; return true; }   // on the pad: plant here
    if (m.carrier == s.slot) for (int k = 0; k < 2; ++k) if (const Objective* o = site(g, k); o && glm::length(s.feet - o->pos) < 25.0f) { goal = o->pos; return true; }   // at a site (pushed off its pad, or this round's other site): its pad
    const int grp = attackerIndex(m, g, s.slot) % 2;
    int& leg = gBots.leg[s.slot];
    if (leg == 0 && glm::length(vec2(s.feet.x - gBots.way[grp].x, s.feet.z - gBots.way[grp].z)) < 6.0f) leg = 1;
    if (leg == 0 && glm::length(s.feet - tgt->pos) < glm::length(gBots.way[grp] - tgt->pos)) leg = 1;   // already past its waypoint
    if (leg == 1 && glm::length(vec2(s.feet.x - gBots.stage[grp].x, s.feet.z - gBots.stage[grp].z)) < 5.0f) leg = 2;
    if (glm::length(s.feet - tgt->pos) < 18.0f) leg = 3;   // already at the site (closer than any staging point): stay on it
    if (!gBots.exec) {   // execute together: every living attacker staged, or 30 s after the first one, or the clock runs short
      bool all = true; for (auto& o : g.sold) if (o.alive && m.sideOf(o.slot) == 0 && gBots.leg[o.slot] < 2) all = false;
      bool any = false; for (auto& [k2, l] : gBots.leg) any |= l >= 2;
      if (any && gBots.stagedAt < 0) gBots.stagedAt = g.t;
      if (all || (gBots.stagedAt >= 0 && g.t - gBots.stagedAt > 30) || m.clock() < 50) gBots.exec = true;
    }
    if (gBots.exec || leg >= 3) { leg = 3; goal = m.carrier == s.slot ? tgt->pos : ring(tgt->pos, 3.0f + (s.slot % 4) * 2.0f); return true; }
    goal = leg == 0 ? gBots.way[grp] : ring(gBots.stage[grp], 2.0f); return true;
  }
  if (m.bomb == SdBomb::Planted) {   // Security: everyone converges; the nearest one to the bomb defuses, the rest cover it
    int best = -1; float bd = 1e9f; for (auto& o : g.sold) if (o.alive && m.sideOf(o.slot) == 1) { float d = glm::length(o.feet - m.bombPos); if (d < bd) { bd = d; best = o.slot; } }
    goal = best == s.slot ? m.bombPos : ring(m.bombPos, 5.0f);
    return true;
  }
  std::vector<int> def; for (auto& o : g.sold) if (m.sideOf(o.slot) == 1) def.push_back(o.slot);
  const int n = (int)def.size(); int k = (int)(std::find(def.begin(), def.end(), s.slot) - def.begin());
  const int hunters = std::clamp((int)std::lround(n * 0.6), 0, std::max(0, n - 2));   // about 60 %, two at least hold (one per site)
  if (k < hunters) {   // a hunting pair (an odd last hunter joins the pair before it)
    const int pair = std::min(k / 2, std::max(0, hunters / 2 - 1)), lead = def[pair * 2];
    const Soldier& L = g.sold[lead];
    for (int q : {pair * 2, pair * 2 + 1, pair * 2 + 2}) if (q < hunters) for (auto& x : g.sold[def[q]].br.mem)   // what the pair has seen
      if (g.t - x.lastSeen < 12.0 && x.enemy >= 0 && x.enemy < (int)g.sold.size() && g.sold[x.enemy].alive) { goal = ring(x.lastPos, 3.0f); return true; }
    if (s.slot != lead && L.alive) { goal = ring(L.feet, 3.0f); return true; }   // the partner keeps with its leader
    const Objective* from = site(g, pair % 2); if (!from) return false;
    const vec3 home = attackersHome(g); const float f = std::clamp(0.25f + 0.012f * (float)(m.t - m.liveStart), 0.25f, 0.65f);
    goal = snapNav(g, glm::mix(from->pos, home, f), 8.0f); return true;
  }
  const Objective* hold = site(g, (k - hunters) % 2);   // the holders: both sites
  if (!hold) return false;
  if (!hold->defenseSlots.empty()) { const DefenseSlot& d = hold->defenseSlots[(size_t)((k - hunters) / 2 + s.slot) % hold->defenseSlots.size()]; goal = snapNav(g, d.pos, 3.0f); return true; }
  goal = ring(hold->pos, 9.0f); return true;
}

SdBombMark sdBombMarkFor(Game& g, vec3& pos) {
  if (!gSd) return SdBombMark::None;
  SdMatch& m = *gSd;
  const int me = g.localSlot >= 0 && g.localSlot < (int)m.players.size() ? g.localSlot : -1;
  const bool attacker = me < 0 || m.sideOf(me) == 0;   // no local slot (spectating tests): the attackers' view
  if (m.bomb == SdBomb::Planted) { pos = m.bombPos; return SdBombMark::Planted; }
  if (!attacker) return SdBombMark::None;   // defenders: the planted bomb only
  if (m.bomb == SdBomb::Dropped) { pos = m.bombPos; return SdBombMark::Dropped; }
  if (m.carrier >= 0 && m.carrier < (int)g.sold.size() && g.sold[m.carrier].alive) { pos = g.sold[m.carrier].feet; return SdBombMark::Carried; }
  return SdBombMark::None;
}
void sdMinimap(ImDrawList* dl, const MinimapView& v) {
  if (!gSd || !gGame || !gGame->active) return;
  vec3 p; const SdBombMark k = sdBombMarkFor(*gGame, p);
  if (k == SdBombMark::None) return;
  const vec2 c = v.toScreen(vec2(p.x, p.z));
  if (!v.contains(c)) return;
  const double now = ImGui::GetTime();
  ImU32 col = k == SdBombMark::Carried ? IM_COL32(175, 178, 182, 255) : k == SdBombMark::Dropped ? IM_COL32(70, 150, 255, 255) : IM_COL32(235, 70, 60, 255);
  const ImU32 ink = IM_COL32(12, 14, 16, 235);
  if (k == SdBombMark::Planted) {   // a pulsing ring
    const float ph = (float)std::fmod(now, 1.0), r = 7.0f + 10.0f * ph;
    dl->AddCircle(ImVec2(c.x, c.y), r, (col & 0x00FFFFFFu) | ((uint32_t)(220 * (1.0f - ph)) << 24), 24, 2.0f);
  }
  // the bomb: a round charge with a fuse stub (the site rhombi's vector style: a fill, an ink outline)
  // larger than a teammate's dot, with a white rim
  dl->AddLine(ImVec2(c.x + 5.0f, c.y - 5.0f), ImVec2(c.x + 10.0f, c.y - 11.0f), ink, 3.5f); dl->AddLine(ImVec2(c.x + 5.0f, c.y - 5.0f), ImVec2(c.x + 9.5f, c.y - 10.5f), IM_COL32(245, 240, 225, 255), 1.8f);
  dl->AddCircleFilled(ImVec2(c.x, c.y), 9.0f, ink, 20); dl->AddCircleFilled(ImVec2(c.x, c.y), 7.5f, IM_COL32(245, 240, 225, 255), 20); dl->AddCircleFilled(ImVec2(c.x, c.y), 6.0f, col, 20);
  if (k == SdBombMark::Planted && gSd->bombSite >= 0) {
    const char L[2] = {(char)('A' + gSd->bombSite), 0};
    dl->AddText(ImVec2(c.x + 12, c.y - 2), ink, L); dl->AddText(ImVec2(c.x + 11, c.y - 3), col, L);
  }
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
  if (args == "buymenu") { buyMenu = !buyMenu; buyCol = -1; return buyMenu ? "Buy menu open." : "Buy menu closed."; }
  if (args == "timeout") { if (g.localSlot < 0) return "No player."; m.requestTimeout(g.sold[g.localSlot].team); return "Timeout requested for the next freeze."; }
  if (args.rfind("buy ", 0) == 0) {
    std::string id = args.substr(4), why = m.canBuy(g.localSlot, id, sdInBuyZone(g, g.localSlot));
    if (!why.empty()) return "Cannot buy " + id + ": " + why;
    buyAndApply(g, g.localSlot, id, true); return "Bought " + id + ".";
  }
  char b[220]; std::snprintf(b, sizeof b, "%s, round %d, Team A %d - %d Team B, Team %s attacks, bomb %d, clock %.1f", sdPhaseName(m.phase), m.round, m.score[0], m.score[1], m.attackerTeam == 0 ? "A" : "B", (int)m.bomb, m.clock());
  return b;
}

// ---------------------------------------------------------------- SD3 the buy menu
// After the user's reference (layout only): a wide dark glass panel, "Buy Time Remaining 00:19" on top, five numbered
// columns (1 Equipment, 2 Pistols, 3 Mid-Tier, 4 Rifles, 5 Grenades) of numbered cards: name, an amber silhouette of
// the item (the weapon silhouettes; armor, the kit and the Molotov drawn glyphs), the price in
// green (red: not enough money). Owned items are greyed with "OWNED" (unaffordable ones keep their colours, red price).
// Keys: a column number, then the item number; the mouse clicks a card (right click refunds this round's purchase);
// B or Esc closes it.
namespace {
std::string buyFlash; double buyFlashAt = -100;
const char* kBuyCols[5] = {"Equipment", "Pistols", "Mid-Tier", "Rifles", "Grenades"};
int buyColumn(const SdItem& it) { return it.cat == "equipment" ? 0 : it.cat == "pistol" ? 1 : it.cat == "smg" || it.cat == "shotgun" || it.cat == "heavy" ? 2 : it.cat == "rifle" || it.cat == "sniper" ? 3 : it.cat == "grenade" ? 4 : -1; }
std::vector<const SdItem*> buyList(SdMatch& m, int side, int col) {
  std::vector<const SdItem*> v;
  for (auto& it : m.cfg.shop) if (it.price[side] >= 0 && buyColumn(it) == col) v.push_back(&it);
  std::stable_sort(v.begin(), v.end(), [&](const SdItem* a, const SdItem* b) { return a->price[side] < b->price[side]; });
  return v;
}
bool buyOwned(SdMatch& m, int me, const SdItem& it) {
  const SdGear& gr = m.players[me].gear;
  if (it.slot == 1) return gr.primary == it.weapon;
  if (it.slot == 2) return gr.secondary == it.weapon;
  if (it.slot == 4) return (int)std::count(gr.grenades.begin(), gr.grenades.end(), it.weapon) >= std::max(1, it.maxCarry);
  if (it.id == "vest") return gr.armor >= 100;
  if (it.id == "helmet") return gr.helmet;
  if (it.id == "vesthelmet") return gr.armor >= 100 && gr.helmet;
  if (it.id == "kit") return gr.kit;
  return false;
}
void drawItemIcon(App& app, ImDrawList* dl, ImVec2 a, ImVec2 b, const SdItem& it, ImU32 col) {   // fitted inside a..b
  const float w = b.x - a.x, h = b.y - a.y, cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
  auto img = [&](GLuint t, float iw, float ih, ImVec2 uv0, ImVec2 uv1) { if (t) dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(cx - iw * 0.5f, cy - ih * 0.5f), ImVec2(cx + iw * 0.5f, cy + ih * 0.5f), uv0, uv1, col); };
  if (!it.weapon.empty()) {
    auto f = app.weaponSilhouette.find(it.weapon); GLuint t = f == app.weaponSilhouette.end() ? 0 : f->second;
    if (!t || it.weapon == "molotov") {   // a throwable without a usable silhouette (the Molotov's is blank): a bottle with a rag
      const float k = std::min(h, w) * 0.42f;
      dl->AddRectFilled(ImVec2(cx - k * 0.32f, cy - k * 0.15f), ImVec2(cx + k * 0.32f, cy + k), col, k * 0.18f);
      dl->AddRectFilled(ImVec2(cx - k * 0.12f, cy - k * 0.65f), ImVec2(cx + k * 0.12f, cy - k * 0.1f), col);
      dl->AddTriangleFilled(ImVec2(cx - k * 0.12f, cy - k * 0.62f), ImVec2(cx + k * 0.3f, cy - k * 1.0f), ImVec2(cx + k * 0.05f, cy - k * 0.55f), col);
      return;
    }
    if (it.slot == 4) { float s = std::min(h, w) * 0.95f; img(t, s, s, ImVec2(0.375f, 0), ImVec2(0.625f, 1)); }   // a grenade: the middle of the 4:1 strip
    else if (it.slot == 2) { float iw = std::min(w * 0.8f, h * 2.6f); img(t, iw, iw * 0.25f * 1.9f, ImVec2(0.2f, 0.025f), ImVec2(0.8f, 0.975f)); }   // a pistol: its middle
    else { float iw = std::min(w * 0.94f, h * 3.6f); img(t, iw, iw * 0.25f, ImVec2(0, 0), ImVec2(1, 1)); }
    return;
  }
  const float s = std::min(h, w) * 0.98f;
  // armor: drawn glyphs (the BR vest / helmet models are local-only: without them the asset is a stand-in box)
  auto vest = [&](float x, float y, float k) {   // a plate carrier: shoulders, neck cut-out, body
    const ImVec2 P[] = {{x - k * 0.5f, y - k * 0.3f}, {x - k * 0.3f, y - k * 0.5f}, {x - k * 0.12f, y - k * 0.5f}, {x, y - k * 0.32f}, {x + k * 0.12f, y - k * 0.5f},
                        {x + k * 0.3f, y - k * 0.5f}, {x + k * 0.5f, y - k * 0.3f}, {x + k * 0.42f, y + k * 0.5f}, {x - k * 0.42f, y + k * 0.5f}};
    dl->AddConvexPolyFilled(P + 6, 3, col); dl->AddConvexPolyFilled(P, 4, col);   // shoulders
    dl->AddRectFilled(ImVec2(x - k * 0.44f, y - k * 0.3f), ImVec2(x + k * 0.44f, y + k * 0.5f), col, k * 0.06f);
    dl->AddLine(ImVec2(x - k * 0.3f, y + k * 0.12f), ImVec2(x + k * 0.3f, y + k * 0.12f), IM_COL32(30, 32, 34, 200), std::max(1.5f, k * 0.04f));
  };
  auto helmet = [&](float x, float y, float k) {   // a dome with a brim
    dl->PathArcTo(ImVec2(x, y + k * 0.15f), k * 0.42f, 3.14159f, 6.28318f, 24); dl->PathFillConvex(col);
    dl->AddRectFilled(ImVec2(x - k * 0.52f, y + k * 0.12f), ImVec2(x + k * 0.52f, y + k * 0.24f), col, k * 0.05f);
  };
  if (it.id == "vest") vest(cx, cy, s * 0.9f);
  else if (it.id == "helmet") helmet(cx, cy, s * 0.9f);
  else if (it.id == "vesthelmet") { vest(cx - s * 0.22f, cy + s * 0.04f, s * 0.8f); helmet(cx + s * 0.38f, cy + s * 0.12f, s * 0.55f); }
  else {   // the defuse kit: wire cutters
    const float k = s * 0.5f, t = std::max(2.0f, s * 0.07f);
    dl->AddLine(ImVec2(cx - k * 0.9f, cy + k * 0.8f), ImVec2(cx + k * 0.15f, cy - k * 0.1f), col, t * 1.6f); dl->AddLine(ImVec2(cx + k * 0.9f, cy + k * 0.8f), ImVec2(cx - k * 0.15f, cy - k * 0.1f), col, t * 1.6f);
    dl->AddLine(ImVec2(cx + k * 0.15f, cy - k * 0.1f), ImVec2(cx - k * 0.2f, cy - k * 0.85f), col, t); dl->AddLine(ImVec2(cx - k * 0.15f, cy - k * 0.1f), ImVec2(cx + k * 0.2f, cy - k * 0.85f), col, t);
    dl->AddCircleFilled(ImVec2(cx, cy - k * 0.12f), t * 0.9f, col);
  }
}
void buyItem(Game& g, SdMatch& m, int me, const SdItem& it) {
  const bool zone = inZone(g, me);
  std::string why = buyOwned(m, me, it) ? "already owned" : m.canBuy(me, it.id, zone);
  if (!why.empty()) { buyFlash = it.name + ": " + why; buyFlashAt = g.t; return; }
  if (g.isClient && gNet) gNet->clientRequest(json{{"t", "sdbuy"}, {"i", it.id}});
  else buyAndApply(g, me, it.id, zone);
}
std::string shortName(const std::string& n) { size_t p = n.find(" ("); return p == std::string::npos ? n : n.substr(0, p); }
void drawBuyMenu(Game& g, int me, int side) {
  SdMatch& m = *gSd; SdPlayer& p = m.players[me];
  ImGuiIO& io = ImGui::GetIO(); const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  g.app.uiCursor = true;
  const float PW = std::min(W - 40.0f, 1180.0f), x0 = (W - PW) * 0.5f, head = 46, colHead = 34, pad = 10, gap = 8;
  const float colW = (PW - pad * 2 - gap * 4) / 5;
  const float cardH = std::clamp((H * 0.84f - head - colHead - pad * 3) / 6 - gap, 64.0f, 118.0f);
  const float PH = head + colHead + pad * 3 + (cardH + gap) * 6 + 22, y0 = std::max(10.0f, (H - PH) * 0.5f);
  ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(PW, PH));
  ImGui::Begin("##sdbuy", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto T = [&](float x, float y, ImU32 c, const std::string& s, float sc = 1.0f) { dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * sc, ImVec2(x, y), c, s.c_str()); };
  auto TW = [&](const std::string& s, float sc = 1.0f) { return ImGui::CalcTextSize(s.c_str()).x * sc; };
  // the glass panel
  uiGlassPanel(dl, ImVec2(x0, y0), ImVec2(x0 + PW, y0 + PH), 8);   // UI2 glass
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + PW, y0 + PH), IM_COL32(10, 12, 14, 110), 8);   // a darker glass under the cards
  dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + PW, y0 + PH), IM_COL32(255, 255, 255, 46), 8, 0, 1.2f);
  dl->AddLine(ImVec2(x0 + 8, y0 + 1), ImVec2(x0 + PW - 8, y0 + 1), IM_COL32(255, 255, 255, 60));
  // header: the buy clock, money, side
  const bool zone = inZone(g, me), open = m.buyOpen();
  float left = m.phase == SdPhase::Freeze ? (float)(m.phaseEnd - m.t) + m.cfg.liveBuy : (float)(m.liveStart + m.cfg.liveBuy - m.t);
  if (!open) left = 0;
  int sl = (int)std::ceil(std::max(0.0f, left)); char clk[16]; std::snprintf(clk, sizeof clk, "%02d:%02d", sl / 60, sl % 60);
  const std::string lab = "Buy Time Remaining   ";
  const float hx = x0 + PW * 0.5f - (TW(lab, 1.15f) + TW(clk, 1.3f)) * 0.5f;
  dl->AddRectFilled(ImVec2(x0 + 1, y0 + 1), ImVec2(x0 + PW - 1, y0 + head), IM_COL32(8, 10, 12, 120), 8, ImDrawFlags_RoundCornersTop);
  T(hx, y0 + 12, IM_COL32(205, 205, 200, 255), lab, 1.15f); T(hx + TW(lab, 1.15f), y0 + 10, left <= 5 && open ? IM_COL32(255, 110, 90, 255) : IM_COL32(255, 255, 255, 255), clk, 1.3f);
  char money[32]; std::snprintf(money, sizeof money, "$%d", p.cash);
  T(x0 + 16, y0 + 10, IM_COL32(120, 230, 120, 255), money, 1.35f);
  const std::string st = !open ? "Buy time is over" : !zone ? "Outside your buy zone" : std::string(sideName(side));
  T(x0 + PW - 16 - TW(st), y0 + 15, !open || !zone ? IM_COL32(255, 150, 110, 255) : IM_COL32(170, 175, 180, 255), st);
  // the columns
  const float cy0 = y0 + head + pad;
  for (int c = 0; c < 5; ++c) {
    const float cx = x0 + pad + c * (colW + gap);
    const bool sel = buyCol == c;
    if (sel) dl->AddRect(ImVec2(cx - 3, cy0 - 3), ImVec2(cx + colW + 3, cy0 + colHead + pad + (cardH + gap) * 6), IM_COL32(255, 190, 60, 150), 6, 0, 1.5f);
    T(cx + 4, cy0 + 8, IM_COL32(150, 155, 160, 255), std::to_string(c + 1));
    T(cx + colW * 0.5f - TW(kBuyCols[c], 1.12f) * 0.5f, cy0 + 6, IM_COL32(240, 240, 236, 255), kBuyCols[c], 1.12f);
    dl->AddLine(ImVec2(cx, cy0 + colHead - 4), ImVec2(cx + colW, cy0 + colHead - 4), IM_COL32(255, 255, 255, 30));
    auto items = buyList(m, side, c);
    for (int k = 0; k < (int)items.size() && k < 6; ++k) {
      const SdItem& it = *items[k];
      const ImVec2 a(cx, cy0 + colHead + pad + k * (cardH + gap)), b2(cx + colW, a.y + cardH);
      const bool owned = buyOwned(m, me, it), afford = p.cash >= it.price[side];
      ImGui::SetCursorScreenPos(a); ImGui::PushID(c * 16 + k);
      ImGui::InvisibleButton("card", ImVec2(colW, cardH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      const bool hov = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) buyItem(g, m, me, it);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !g.isClient && m.refund(me, it.id, zone)) applyGear(g, me);
      ImGui::PopID();
      dl->AddRectFilled(a, b2, owned ? IM_COL32(52, 54, 56, 150) : hov ? IM_COL32(58, 62, 66, 235) : IM_COL32(38, 41, 44, 225), 5);
      dl->AddRect(a, b2, owned ? IM_COL32(200, 200, 196, 150) : hov ? IM_COL32(255, 190, 60, 200) : IM_COL32(255, 255, 255, 26), 5, 0, owned ? 1.6f : 1.0f);
      const ImU32 dim = IM_COL32(120, 122, 124, 255);
      T(a.x + 7, a.y + 5, owned ? dim : IM_COL32(150, 155, 160, 255), std::to_string(k + 1));
      std::string nm = shortName(it.name); float sc = 0.92f; while (sc > 0.7f && TW(nm, sc) > colW - 26) sc -= 0.04f;
      T(b2.x - 7 - TW(nm, sc), a.y + 5, owned ? dim : IM_COL32(230, 230, 226, 255), nm, sc);
      const ImU32 icon = owned ? IM_COL32(125, 125, 122, 200) : afford ? IM_COL32(245, 190, 60, 255) : IM_COL32(245, 190, 60, 150);
      drawItemIcon(g.app, dl, ImVec2(a.x + 10, a.y + 22), ImVec2(b2.x - 10, b2.y - 20), it, icon);
      char pr[24]; std::snprintf(pr, sizeof pr, "$%d", it.price[side]);
      if (owned) T(a.x + 7, b2.y - 19, IM_COL32(200, 200, 196, 230), "OWNED", 0.85f);
      T(b2.x - 7 - TW(pr), b2.y - 19, owned ? dim : afford ? IM_COL32(110, 220, 110, 255) : IM_COL32(240, 80, 70, 255), pr);
    }
  }
  const std::string foot = buyCol < 0 ? "Number keys: a column, then an item     Click: buy     Right click: refund     B / Esc: close"
                                      : std::string(kBuyCols[buyCol]) + ": press the item's number (Backspace: back)";
  T(x0 + 16, y0 + PH - 22, IM_COL32(150, 155, 160, 255), foot, 0.9f);
  if (!buyFlash.empty() && g.t - buyFlashAt < 2.5) T(x0 + PW - 16 - TW(buyFlash, 0.9f), y0 + PH - 22, IM_COL32(255, 150, 110, 255), buyFlash, 0.9f);
  ImGui::End();
}
}   // namespace

namespace { void buyMenuKey(Game& g, int k) {
  SdMatch& m = *gSd; const int me = g.localSlot; if (me < 0 || me >= (int)m.players.size()) return;
  if (buyCol < 0) { if (k < 5) buyCol = k; return; }
  auto items = buyList(m, m.sideOf(me), buyCol);
  if (k < (int)items.size()) buyItem(g, m, me, *items[k]);
  buyCol = -1;
} }   // namespace
bool sdBuyMenuOpen() { return gSd && buyMenu; }
void sdCloseBuyMenu() { buyMenu = false; buyCol = -1; }

// ---------------------------------------------------------------- HUD and buy menu
void sdDrawHud(Game& g) {
  if (!gSd) return;
  SdMatch& m = *gSd;
  drawPads(g);
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
  uiGlassPanel(dl, ImVec2(px, py), ImVec2(px + pw, py + ph), 6);
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
  if (!banner.empty() && g.t - bannerAt < 4.0 && !buyMenu) centered(H * 0.28f, IM_COL32(255, 255, 255, 235), banner, 1.6f);
  // bottom left: money, armor, kit, the bomb
  if (me >= 0) {
    SdPlayer& p = m.players[me];
    const float bx = 16, by = H - 150;
    uiGlassPanel(dl, ImVec2(bx - 6, by - 6), ImVec2(bx + 330, by + 74), 6);
    std::snprintf(b, sizeof b, "$%d", p.cash); text(vec2(bx, by), IM_COL32(120, 230, 120, 255), b, 1.4f);
    std::snprintf(b, sizeof b, "%s   armor %d%s%s", sideName(mySide), p.gear.armor, p.gear.helmet ? " + helmet" : "", p.gear.kit ? "   kit" : ""); text(vec2(bx, by + 30), cWhite, b);
    std::string bomb;
    if (mySide == 0) bomb = planted ? std::string("Bomb planted at ") + (m.bombSite == 0 ? "A" : "B") + ": guard it" : m.carrier == me ? "You carry the bomb: plant it on the A or B pad (G drops it)" : m.bomb == SdBomb::Dropped ? "The bomb is on the ground" : m.carrier >= 0 ? "Bomb: " + g.match.slots[m.carrier].name : "";
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
  // SD3: the plant pads in the world (letter and distance) for the attackers before the plant; after it, the bomb's site for everyone
  if (me >= 0 && m.combat() && m.phase != SdPhase::Warmup)
    for (int k = 0; k < 2; ++k) {
      const Objective* o = site(g, k); if (!o) continue;
      if (planted ? m.bombSite != k : mySide != 0) continue;
      vec4 c = g.app.projM * g.app.viewM * vec4(o->pos + vec3(0, 1.6f, 0), 1);
      if (c.w <= 0.1f) continue;
      vec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (0.5f - c.y / c.w * 0.5f) * H);
      sp = glm::clamp(sp, vec2(24, 90), vec2(W - 24, H - 60));
      const char* L = k == 0 ? "A" : "B"; const ImU32 col = planted ? cRed : IM_COL32(255, 190, 60, 235);
      dl->AddRectFilled(ImVec2(sp.x - 15, sp.y - 15), ImVec2(sp.x + 15, sp.y + 15), IM_COL32(12, 14, 16, 170), 4);
      dl->AddRect(ImVec2(sp.x - 15, sp.y - 15), ImVec2(sp.x + 15, sp.y + 15), col, 4, 0, 2.0f);
      text(vec2(sp.x - width(L, 1.4f) * 0.5f, sp.y - ImGui::GetFontSize() * 0.7f), col, L, 1.4f);
      std::snprintf(b, sizeof b, "%.0f m", glm::length(o->pos - g.app.eyePos())); text(vec2(sp.x - width(b) * 0.5f, sp.y + 18), col, b);
    }
  if (me >= 0 && m.carrier == me && m.players[me].alive && m.planter != me && siteAt(g, g.sold[me].feet) >= 0 && m.phase == SdPhase::LiveUnplanted)
    centered(H * 0.66f, cGold, "Hold E to plant", 1.3f);   // SD3: only inside the pad (the exact plant test)
  // the bomb, for the attackers (spec §5: carrier and dropped bomb shown to attackers); SD9: the minimap's colours (grey
  // carried, over a teammate carrying it; blue dropped); planted: the site marker above turns red
  { vec3 bp; const SdBombMark bk = sdBombMarkFor(g, bp);
    if ((bk == SdBombMark::Dropped || (bk == SdBombMark::Carried && m.carrier != me)) && mySide == 0) {
      const ImU32 bc = bk == SdBombMark::Dropped ? IM_COL32(70, 150, 255, 235) : IM_COL32(175, 178, 182, 225);
      vec4 c = g.app.projM * g.app.viewM * vec4(bp + vec3(0, bk == SdBombMark::Carried ? 2.2f : 0.6f, 0), 1);
      if (c.w > 0.1f) { vec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (0.5f - c.y / c.w * 0.5f) * H);
        dl->AddCircle(ImVec2(sp.x, sp.y), 9, bc, 16, 2.5f);
        std::snprintf(b, sizeof b, "BOMB %.0f m", glm::length(bp - g.app.eyePos())); text(vec2(sp.x - width(b) * 0.5f, sp.y + 12), bc, b); }
    } }
  // kill feed, top right under the minimap
  float fy = 300;
  for (auto& f : feed) if (g.t - f.at < 8.0) { float w = width(f.text); dl->AddRectFilled(ImVec2(W - w - 22, fy - 2), ImVec2(W - 8, fy + ImGui::GetFontSize() + 2), IM_COL32(12, 14, 16, 150), 4); text(vec2(W - w - 15, fy), cWhite, f.text); fy += ImGui::GetFontSize() + 8; }
  if (buyMenu && me >= 0) drawBuyMenu(g, me, mySide);   // SD3
}
