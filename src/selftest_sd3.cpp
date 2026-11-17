// SD3 --selftest-sd3: the SD3 changes through the real game on Chateau town (temporary layout), 5 v 5 bots.
//  - defaults: the Warmup and Vehicles options start off (search_destroy.json, Rules);
//  - vehicles off: a vehicle placed on the map is gone for the match (doc, vehicle system) and back after it;
//  - the plant pads: about 3.5 m across, on the navmesh, and the drawn outline is the plant test (0.12 m inside: in a
//    site; 0.12 m outside: not);
//  - the carrier killed: the attacker nearest the dropped bomb picks it up; the dead carrier's gun in hand drops;
//  - plant discipline: a carrier planting on the pad lets go when an enemy shows up, then plants once it is dead;
//  - carry-over: a dead attacker on the winning side keeps its gun next round, a dead defender on the losing side not;
//  - a capture of the buy menu (selftest_out/sd_buymenu.png).
// selftest_out/report_sd3.txt.
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "tank.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0; bool started = false; float total = 0; double at = 0;
int carrier0 = -1, nearest = -1, defender0 = -1, enemy = -1, vehBefore = 0; size_t drops0 = 0; vec3 drop0{0};
uint64_t jeepId = 0;

bool chk(bool ok, const std::string& s) { return T->check(ok, s); }
int vehiclesInDoc(App& a) { int n = 0; for (auto& i : a.doc.instances) n += a.tanks.defFor(a.lib.find(i.assetId)) != nullptr; return n; }
void put(Game& g, Soldier& s, vec3 p) { if (s.localPlayer) Physics::setCharacterPos(p); else if (s.agent >= 0) Physics::setAgentPos(s.agent, p); s.feet = p; s.vel = vec3(0); }
vec3 ground(Game& g, vec3 p) { Physics::Hit h; if (Physics::castRayEx(vec3(p.x, p.y + 2.0f, p.z), vec3(0, -1, 0), 6.0f, h)) p.y = h.pos.y; else p.y = g.app.terrain.height(p.x, p.z); return p + vec3(0, 0.05f, 0); }
vec3 defHome(Game& g, int k) { int n = 0; for (auto& m : g.match.sc.spawns) if (m.team < 0 && m.role == CombatRole::Defender && n++ == k % 5) return m.pos + vec3(0, 0.1f, 0); return vec3(0); }
void parkDefenders(Game& g, SdMatch& m, int except) {   // the defenders kept at their spawn (they would come to the site)
  int k = 0; for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1 && s.slot != except) put(g, s, defHome(g, k++));
}
}   // namespace

bool sd3SelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd3") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sd3");
    SdConfig c = SdConfig::load(); ScenarioRules r;
    chk(!c.warmupDefault && !c.vehiclesDefault && !r.sdWarmup && !r.sdVehicles, "the Warmup and Vehicles options default to off (search_destroy.json \"warmup\" / \"vehicles\", the rules)");
    auto bad = sdLogicSelfCheck();
    for (auto& b : bad) chk(false, "round logic: " + b);
    chk(bad.empty(), "round logic self-check (with SD3 carry-over: winners keep guns)");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "templates/chateau_town.nmap.json");
    {   // a jeep on the map, by the attackers' spawn
      Instance i; i.id = 0x5D3000001ull; i.assetId = "car_jeep_willys"; vec3 p(-30, 0, -80); p.y = app.terrain.height(p.x, p.z); i.pos = p; i.yaw = 30;
      app.doc.instances.push_back(i); jeepId = i.id; app.syncRuntime(); app.tanks.sync(app);
    }
    vehBefore = vehiclesInDoc(app);
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0;   // Warmup / Vehicles: the defaults
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 0 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    chk(ok && sdMatch(), "Search and Destroy starts on Chateau town (" + (gGame ? gGame->setupError : std::string("-")) + ")");
    int live = 0; for (auto& [id, t] : app.tanks.tanks) live += !t.adopted;
    chk(vehBefore >= 1 && vehiclesInDoc(app) == 0 && live == 0, "Vehicles off: the map's " + std::to_string(vehBefore) + " vehicle(s) are gone for the match (doc " + std::to_string(vehiclesInDoc(app)) + ", vehicle system " + std::to_string(live) + ")");
    if (SdMatch* m = sdMatch()) {
      m->cfg.freeze = 3; m->cfg.roundEnd = 1.5f;
      Game& g = *gGame;
      for (int k = 0; k < 2; ++k) {   // the pads
        const Objective& o = g.match.sc.objectives[k];
        auto ol = sdPadOutline(g, k); vec2 cen(0); for (auto& p : ol) cen += p; if (!ol.empty()) cen /= (float)ol.size();
        int in = 0, out = 0, n = 0; float across = 0;
        for (size_t i = 0; i < ol.size(); ++i) {
          vec2 a = ol[i], b = ol[(i + 1) % ol.size()];
          for (vec2 p : {a, (a + b) * 0.5f}) { vec2 d = glm::normalize(p - cen); ++n;
            in += o.contains(vec3(p.x - d.x * 0.12f, o.pos.y, p.y - d.y * 0.12f)); out += !o.contains(vec3(p.x + d.x * 0.12f, o.pos.y, p.y + d.y * 0.12f)); }
          for (auto& q : ol) across = std::max(across, glm::length(q - a));
        }
        NavRef ref; vec3 on; bool nav = g.nav.closest(o.pos, vec3(0.5f, 2.0f, 0.5f), ref, on);
        char b[240]; std::snprintf(b, sizeof b, "site %c pad: %.1f m corner to corner (3.5 x 3.5 m), on the navmesh %d; outline vs plant test: %d of %d points inside, %d of %d outside",
          'A' + k, across, (int)nav, in, n, out, n);
        chk(n > 0 && in == n && out == n && across < 5.5f && nav, b);
      }
    }
  }
  total += dt;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() {
    if (app.mode == Mode::Explore) app.exitExplore();
    bool back = false; for (auto& i : app.doc.instances) back |= i.id == jeepId;
    chk(back && vehiclesInDoc(app) == vehBefore, "after the match the map's vehicles are back (" + std::to_string(vehiclesInDoc(app)) + " of " + std::to_string(vehBefore) + ")");
    app.selftest->exitCode = T->finish(); app.selftest->done = true;
  };
  if (!mp || !g.active) { if (!app.selftest->done) finish(); return true; }
  SdMatch& m = *mp;
  if (total > 900) { chk(false, "timed out at stage " + std::to_string(stage)); finish(); return true; }
  const double live = m.phase == SdPhase::LiveUnplanted || m.phase == SdPhase::LivePlanted ? m.t - m.liveStart : -1;
  if (Soldier* L = g.local(); L && L->alive && m.round == 1 && live > 0.2) g.killSoldier(L->slot, -1, vec3(0, 0, 1));   // round 1: bots only
  switch (stage) {
    case 0:   // round 1 live: kill the carrier (a gun in its gear first) and one defender (likewise)
      if (live < 1.0 || m.carrier < 0) break;
      carrier0 = m.carrier; m.players[carrier0].gear.primary = "modern_ak";
      for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1) { defender0 = s.slot; break; }
      if (defender0 >= 0) { m.players[defender0].gear.primary = "modern_m4"; g.killSoldier(defender0, -1, vec3(0, 0, 1)); }
      { float bd = 1e9f; for (auto& s : g.sold) if (s.alive && s.slot != carrier0 && m.sideOf(s.slot) == 0) { float d = glm::length(s.feet - g.sold[carrier0].feet); if (d < bd) { bd = d; nearest = s.slot; } } }
      drops0 = g.drops.size(); drop0 = g.sold[carrier0].feet;
      g.killSoldier(carrier0, -1, vec3(0, 0, 1));
      { bool dropped = false; for (size_t k = drops0; k < g.drops.size(); ++k) dropped |= glm::length(g.drops[k].pos - drop0) < 1.5f;
        chk(dropped, "the killed carrier dropped the gun in its hands"); }
      chk(m.bomb == SdBomb::Dropped, "the carrier killed: the bomb is on the ground");
      stage = 1; at = m.t; break;
    case 1:   // the nearest attacker fetches it
      if (m.bomb == SdBomb::Carried) {
        chk(m.carrier == nearest, "the nearest attacker (slot " + std::to_string(nearest) + ") picked the bomb up after " + std::to_string((int)(m.t - at)) + " s (carrier " + std::to_string(m.carrier) + ")");
        stage = 2; at = m.t;
      } else if (m.t - at > 30) { chk(false, "nobody picked the dropped bomb up in 30 s"); stage = 2; at = m.t; }
      break;
    case 2: {   // the carrier onto pad A, the defenders away: it plants; then an enemy shows up
      if (m.carrier < 0) { chk(false, "no carrier for the plant test"); stage = 9; break; }
      Soldier& c = g.sold[m.carrier]; c.hp = 1e4f;
      if (m.t - at < 0.1) put(g, c, ground(g, g.match.sc.objectives[0].pos));
      parkDefenders(g, m, -1);
      if (m.planter == m.carrier && m.plantProgress > 0.5f) {
        for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1) { enemy = s.slot; break; }
        if (enemy >= 0) { Soldier& e = g.sold[enemy]; e.hp = 1e4f; vec3 p = c.feet + vec3(5.5f, 0, 0); put(g, e, ground(g, p)); }
        stage = 3; at = m.t;
      } else if (m.t - at > 12) { chk(false, "the carrier did not start planting on the pad in 12 s (" + c.br.why + ")"); stage = 9; }
      break; }
    case 3: {   // engaged: it lets go
      parkDefenders(g, m, enemy);
      if (enemy >= 0) { Soldier& e = g.sold[enemy]; if (glm::length(e.feet - g.sold[std::max(0, m.carrier)].feet) > 9) put(g, e, ground(g, g.sold[std::max(0, m.carrier)].feet + vec3(5.5f, 0, 0))); }
      if (m.planter < 0 && m.bomb == SdBomb::Carried) {
        chk(m.plantProgress == 0, "plant discipline: an enemy in view, the carrier stopped planting (progress reset) after " + std::to_string(m.t - at).substr(0, 4) + " s");
        if (enemy >= 0) g.killSoldier(enemy, m.carrier, vec3(0, 0, 1));
        stage = 4; at = m.t;
      } else if (m.bomb == SdBomb::Planted) { chk(false, "plant discipline: the carrier finished planting with an enemy 5.5 m away"); stage = 4; at = m.t; }
      else if (m.t - at > 6) { chk(false, "plant discipline: the carrier kept planting with an enemy in view for 6 s"); if (enemy >= 0) g.killSoldier(enemy, -1, vec3(0, 0, 1)); stage = 4; at = m.t; }
      break; }
    case 4:   // clear again: it plants
      parkDefenders(g, m, -1);
      if (m.carrier >= 0) g.sold[m.carrier].hp = 1e4f;
      if (m.bomb == SdBomb::Planted) { chk(true, "the enemy dead, the carrier planted after " + std::to_string((int)(m.t - at)) + " s"); stage = 5; }
      else if (m.t - at > 20) { chk(false, "the carrier did not resume the plant within 20 s (" + (m.carrier >= 0 ? g.sold[m.carrier].br.why : std::string("no carrier")) + ")"); stage = 5; }
      break;
    case 5:   // the defenders fall: the attackers win the round; carry-over at the round end (before anyone buys again)
      for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1) g.killSoldier(s.slot, -1, vec3(0, 0, 1));
      if (m.phase == SdPhase::RoundEnd) {
        chk(m.players[carrier0].gear.primary == "modern_ak" && m.players[defender0].gear.primary.empty(),
            "carry-over at the round end: the dead attacker (winners) keeps its AK (" + m.players[carrier0].gear.primary + "), the dead defender (losers) lost its M4 (" + (m.players[defender0].gear.primary.empty() ? std::string("none") : m.players[defender0].gear.primary) + ")");
        stage = 6;
      }
      break;
    case 6:   // round 2: carry-over, then the buy menu
      if (m.round < 2 || m.phase != SdPhase::Freeze) break;
      { int w = g.app.weaponIndex("modern_ak"); const auto& cr = g.sold[carrier0].carried; chk(w >= 0 && std::find(cr.begin(), cr.end(), w) != cr.end(), "carry-over: it spawned with the AK"); }
      if (Soldier* L = g.local(); L && L->alive) {
        m.players[L->slot].cash = 3400;
        T->note(sdCommand(g, "buy vest")); T->note(sdCommand(g, "buy smoke"));
        T->note(sdCommand(g, "buymenu"));
        stage = 7; at = m.t;
      }
      break;
    case 7:
      if (m.t - at > 0.6) { app.uiShot = exeDir() / "selftest_out" / "sd_buymenu.png"; stage = 8; at = m.t; }
      break;
    case 8:
      if (m.t - at > 0.5) { sdCommand(g, "buymenu"); chk(true, "buy menu captured: selftest_out/sd_buymenu.png"); stage = 9; }
      break;
    default: finish(); break;
  }
  return true;
}
