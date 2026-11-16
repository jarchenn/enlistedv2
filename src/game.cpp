#include <cstring>
#include <map>
// Match game layer: lifecycle, spawning, soldier weapons / grenades, damage, presentation. AI is in ai.cpp.
#include "sd_game.h"
#include "game.h"
#include "classes.h"
#include "abilities.h"
#include "gore_api.h"
#include "rules.h"
#include "damage_model.h"
#include "app.h"
#include "settings.h"
#include "mpui.h"
#include "net.h"
#include "crumb.h"
#include "enemy_types.h"
#include "flame.h"
#include "ai_hooks.h"
#include "br_models.h"
#include "royale.h"
#include "loot.h"   // BR2 Battle Royale loot
void netInterpolate(Game& g, double t, float dt);
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <set>

Game* gGame = nullptr;

static const float kLimboDepth = 40.0f;   // dead local player: the explore character waits under the spawn area

bool Game::loadData(std::string& err) {
  try {
    combat = json::parse(readTextFile(contentDir() / "ai" / "combat.json"));
    archetypes = json::parse(readTextFile(contentDir() / "ai" / "archetypes.json"));
  } catch (std::exception& e) { err = std::string("content/ai data: ") + e.what(); return false; }
  json gj = archetypes.value("grenade", json::object());
  return true;
}

bool Game::ensureModels(std::string& err) {
  if (!initSkinning(err)) { err = "skinning shader: " + err; return false; }
  if (!models.empty()) return true;
  json m = combat.value("models", json::object());
  for (auto& [fac, list] : m.items())
    for (auto& id : list) {
      std::string s = id.get<std::string>();
      if (modelIdx.count(s)) continue;
      auto cm = std::make_unique<CharModel>();
      std::string e;
      if (!cm->load(contentDir() / "characters" / (s + ".glb"), e)) { err = "character " + s + ": " + e; return false; }
      modelIdx[s] = (int)models.size(); models.push_back(std::move(cm));
    }
  return !models.empty();
}

vec3 Game::soldierFeet(int s) const { return sold[s].feet; }
int Game::fragDef(Faction f) const { return app.weaponIndex(f == Faction::Axis ? "m24_grenade" : "mk2_grenade"); }
int Game::smokeDef() const { return app.weaponIndex("smoke_grenade"); }

void Game::localGrenadeCounts(Soldier& L, bool write) {
  int fd = fragDef(L.faction), sd = smokeDef();
  auto put = [&](int d, int n) { if (d < 0) return; WeaponState& s = app.wstate[app.weaponDefs[d]->id]; if (s.throwT >= 0) return; s.loaded = n > 0 ? 1 : 0; s.containers.assign(std::max(0, n - 1), 1); };
  auto get = [&](int d) { if (d < 0) return 0; const WeaponState& s = app.wstate[app.weaponDefs[d]->id]; return s.loaded + s.reserveRounds(); };
  if (write) { put(fd, L.frags); put(sd, L.smokes); }
  else { L.frags = get(fd); L.smokes = get(sd); }
}

void Game::syncGrenadeView() {
  gren.live.clear();
  for (auto& g : app.grenades) {
    if (g.done || g.def < 0 || g.def >= (int)app.weaponDefs.size()) continue;
    GrenadeRT v; v.id = (uint32_t)g.body; v.def = g.def; v.kind = app.weaponDefs[g.def]->grenade.smoke ? GK_SMOKE : GK_FRAG;
    v.owner = g.owner < 0 ? localSlot : g.owner; v.p = g.pos; v.rot = g.rot; v.fuse = g.fuse; v.age = g.age;
    v.resting = glm::length(Physics::velocity(g.body)) < 0.4f;
    gren.live.push_back(v);
  }
}
static float bodyScale(const Soldier& s) { return s.zombie ? s.eBody : 1.0f; }   // EN1: a troll stands taller, a spider lower
void Game::renderView(vec3& e, vec3& f) const {
  e = app.eyePos(); f = App::forward(app.eyeYaw, app.eyePitch); float fov = 70;
  vec3 e2 = e, f2 = f;
  if (app.tanks.camera(app, e2, f2, fov) || app.stationCamera(e2, f2, fov)) { e = e2; f = f2; }
}

vec3 Game::eye(const Soldier& s) const {
  if (s.localPlayer && s.alive) return app.eyePos();
  float h = s.posture == Physics::PRONE ? 0.32f : s.posture == Physics::CROUCH ? 1.05f : 1.62f;
  return s.feet + vec3(0, h * bodyScale(s), 0);
}
vec3 Game::chest(const Soldier& s) const {
  float h = s.posture == Physics::PRONE ? 0.25f : s.posture == Physics::CROUCH ? 0.8f : 1.25f;
  return s.feet + vec3(0, h * bodyScale(s), 0);
}

int Game::soldierOfOwner(int owner) const { return owner >= 0 ? owner : localSlot; }

// ---------------------------------------------------------------- lifecycle
bool Game::begin(std::string& err) {
  pendingStart = false;
  if (!loadData(err) || !ensureModels(err)) { setupError = err; return false; }
  json scj = !gUi.matchScenario.is_null() ? gUi.matchScenario : app.doc.extra.is_object() ? app.doc.extra.value("scenario", json()) : json();
  Scenario sc = Scenario::fromJson(scj);
  sdPrepareScenario(app, sc);   // SD1: the map's Search and Destroy layout, or the temporary one
  auto issues = validateScenario(sc, false);
  for (auto& i : issues) if (i.error) { err = i.what; setupError = err; return false; }
  int humans = isClient ? 0 : std::max<int>(1, (int)humanPeers.size());
  if (!match.setup(sc, humans, err)) { setupError = err; return false; }
  if (!isClient) { int k = 0; for (auto& rs : match.slots) if (rs.human && k < (int)humanPeers.size()) { rs.peer = humanPeers[k].first; rs.name = humanPeers[k].second; ++k; } }
  else {   // the host's roster, exactly
    for (auto& sj : clientStart["slots"]) { int id = sj["id"]; if (id < 0 || id >= (int)match.slots.size()) continue; auto& rs = match.slots[id];
      rs.faction = (Faction)sj["f"].get<int>(); rs.role = (CombatRole)sj["r"].get<int>(); rs.human = sj["h"]; rs.peer = sj["p"]; rs.name = sj["n"]; rs.lane = sj.value("lane", 1); rs.team = sj.value("t", (int)rs.role); }
  }
  int me = isClient && gNet ? gNet->myPeer : 0;
  localSlot = -1; for (auto& rs : match.slots) if (rs.human && rs.peer == me) localSlot = rs.id;
  pendingLocal = isClient ? localSlot : -1;
  if (isClient) localSlot = -1;   // becomes ours when the host reports the slot as this peer's (first spawn)
  // navigation over the scenario area (objectives, spawns, routes + margin)
  vec3 lo(1e9f), hi(-1e9f);
  auto grow = [&](vec3 p, float r) { lo = glm::min(lo, p - vec3(r)); hi = glm::max(hi, p + vec3(r)); };
  for (auto& o : match.sc.objectives) { grow(o.pos, o.extent() + 35); for (auto& r : o.routes) for (auto& p : r.points) grow(p, 20); }
  for (auto& s : match.sc.spawns) grow(s.pos, 25);
  const vec3 hv(app.terrain.buildHalfX, app.terrain.buildHalf, app.terrain.buildHalfZ);   // per axis: a 350 x 400 map is not 400 x 400
  if (match.mode() != GameMode::Assault) { lo = -hv; hi = hv; }   // G1: Solo / TDM fight anywhere: the whole build area
  lo = glm::max(lo, -hv); hi = glm::min(hi, hv);
  if (!isClient) aiMatchBegin(*this, lo, hi);   // W6: navmesh bake (Recast/Detour) and AI setup
  // soldiers per roster slot
  sold.clear(); sold.resize(match.slots.size());
  auto& arr = archetypes["archetypes"];
  int aiGiven[2][kAiTypeCount] = {}, kindSeen[kAiTypeCount] = {};   // per side / per AI type: types and model variants go round in turn (same on LAN clients)
  if (const char* at = std::getenv("NMK_AI_TYPES")) {   // SW1 dev: NMK_AI_TYPES=<side 0 type>,<side 1 type> (AiType numbers): every AI of that side that type
    int t0 = -1, t1 = -1; std::sscanf(at, "%d,%d", &t0, &t1); int ts[2] = {t0, t1};
    for (int sd = 0; sd < 2; ++sd) if (ts[sd] >= 0 && ts[sd] < kAiTypeCount) { match.rules.aiTypes[sd] = aiBit((AiType)ts[sd]); for (int k = 0; k < kAiTypeCount; ++k) match.rules.aiCount[sd][k] = 0; }
  }
  for (auto& rs : match.slots) {
    Soldier& s = sold[rs.id];
    s.slot = rs.id; s.inUse = true; s.alive = false; s.faction = rs.faction; s.role = rs.role; s.team = rs.team;
    s.human = rs.human; s.peer = rs.peer; s.localPlayer = rs.id == localSlot;
    int side = ffaMode(match.mode()) ? 0 : std::clamp(rs.team, 0, 1);
    AiType ait = rs.human ? AiType::Soldiers : aiTypeNext(match.rules.aiCount[side], match.rules.aiTypes[side], aiGiven[side]);   // AI3: the typed counts per type
    s.zombie = ait != AiType::Soldiers && ait != AiType::Operators && ait != AiType::Troopers && ait != AiType::Robots;   // G2 melee brain
    s.op = ait == AiType::Operators;
    s.kit = ait == AiType::Troopers ? 2 : ait == AiType::Robots ? 3 : s.op ? 1 : 0;   // SW1
    s.etype = !s.zombie ? 0 : ait == AiType::ZombiesHeavies ? enemyTypePick((uint32_t)rs.id)   // EN1: the mix by weight, per slot
            : ait == AiType::Brutes ? std::max(0, enemyTypeIndex("brute")) : ait == AiType::Spiders ? std::max(0, enemyTypeIndex("stalker"))
            : ait == AiType::Creatures ? std::max(0, enemyTypeIndex("creature"))
            : ait == AiType::Knights ? std::max(0, enemyTypeIndex("knight")) : ait == AiType::Animatronics ? std::max(0, enemyTypeIndex("animatronic"))
            : ait == AiType::Aliens ? std::max(0, enemyTypeIndex("alien")) : ait == AiType::AlienQueens ? std::max(0, enemyTypeIndex("alien_queen"))
            : ait == AiType::Abominations ? std::max(0, enemyTypeIndex("abomination")) : ait == AiType::Golems ? std::max(0, enemyTypeIndex("golem"))
            : ait == AiType::Boglords ? std::max(0, enemyTypeIndex("boglord"))
            : ait == AiType::Fighters ? std::max(0, enemyTypeIndex("fighter"))
            : ait == AiType::Fighters2 ? std::max(0, enemyTypeIndex("fighter2")) : 0;
    s.evariant = kindSeen[(int)ait]++;
    s.rng = 0x9E3779B9u ^ (uint32_t)(rs.id * 2654435761u);
    const json& a = arr[rs.id % arr.size()];
    Brain& b = s.br;
    b.arch = rs.id % (int)arr.size();
    b.reaction = a["reaction_s"][0].get<float>() + rnd(s) * (a["reaction_s"][1].get<float>() - a["reaction_s"][0].get<float>());
    b.aimTurn = a.value("aimTurn_dps", 220.0f); b.sigma = a.value("sigma_deg", 1.1f); b.sight = a.value("sightRange_m", 110.0f);
    b.fov = a.value("fov_deg", 150.0f); b.aggression = a.value("aggression", 0.5f);
    b.burstMin = a["burst"][0]; b.burstMax = a["burst"][1];
    b.lane = rs.lane;
    std::string fac = factionName(rs.faction);
    auto& ml = combat["models"][fac];
    s.model = modelIdx[ml[rs.id % ml.size()].get<std::string>()];
    if (s.zombie) aiZombieAssign(*this, s);   // G2: the animated zombie
    if (s.localPlayer && !settings().s.character.empty()) {   // UI2: the body picked on the Character screen (missing file: the faction body stays)
      const std::string& id = settings().s.character;
      if (!modelIdx.count(id)) { auto cm = std::make_unique<CharModel>(); std::string e; if (cm->load(contentDir() / "characters" / (id + ".glb"), e)) { modelIdx[id] = (int)models.size(); models.push_back(std::move(cm)); } else logf("character %s: %s (faction body kept)", id.c_str(), e.c_str()); }
      if (modelIdx.count(id)) s.model = modelIdx[id];
    }
    const json kitj = s.kit >= 2 ? combat.value("kits", json::object()).value(s.kit == 2 ? "trooper" : "robot", json::object()) : json::object();   // SW1
    if (s.kit >= 2) { s.kitSpeed = kitj.value("speed", 1.0f); s.kitSparks = kitj.value("sparks", false); b.aimTurn *= kitj.value("aimTurn", 1.0f); }
    if (ait == AiType::Operators || s.kit >= 2) {   // a soldier in the faction's operator body (combat.json operator_models; SW1: the kit's models, either side; missing: the soldier body stays)
      json ol = s.kit >= 2 ? kitj.value("models", json::array()) : combat.value("operator_models", json::object()).value(fac, json::array());
      if (!ol.empty()) {
        std::string id = ol[(size_t)s.evariant % ol.size()].get<std::string>();
        static std::set<std::string> failed;
        if (!modelIdx.count(id) && !failed.count(id)) {
          auto cm = std::make_unique<CharModel>(); std::string e;
          if (cm->load(contentDir() / "characters" / (id + ".glb"), e)) { modelIdx[id] = (int)models.size(); models.push_back(std::move(cm)); }
          else { failed.insert(id); logf("operators: model %s: %s (soldier body kept)", id.c_str(), e.c_str()); }
        }
        if (modelIdx.count(id)) s.model = modelIdx[id];
      }
    }
  }
  pool.clear(); poolBuilt = false;
  sounds.clear(); gren.clear(); grenadeReserve.clear(); smokeReserve.clear(); defSpots.clear(); defSpotsStage = -1;
  t = 0; steps = 0; spectate = -1; hitMarker = killMarker = hurtFlash = 0;
  shotsByBots = hitsOnSoldiers = kills = doorOpens = smokeThrows = fragThrows = navRebuilds = 0;
  if (!isClient) match.start(); else { match.phase = MatchPhase::Running; for (auto& o : match.obj) o.state = ObjState::Locked; if (!match.obj.empty()) match.obj[0].state = ObjState::Active; }
  ghosts.clear(); doorSeen.clear();
  active = true;
  deployOpenedAt = t; deployReady = app.selftest; deploySpawn = -1;   // GM1: the first spawn goes through the deploy screen too
  // the local player waits in limbo until its first spawn
  if (Soldier* L = local()) { vec3 p = app.exploreStartPos; Physics::setCharacterPos(vec3(p.x, p.y - kLimboDepth, p.z)); (void)L; }
  setupError.clear();
  lootBegin(*this);   // BR2: floor loot in the buildings
  sdBegin(*this);     // SD1: the round controller (Search and Destroy only)
  logf("match: %zu slots, local slot %d, nav %d floors in %.0f ms", sold.size(), localSlot, nav.usedNodes, nav.buildMs);
  return true;
}

void Game::end() {
  sdEnd();   // SD1
  gAdmin = AdminMode{};   // CMD2: /admin ends with the match
  aiZombieLoopsStop(app);   // SND2: no creature breathing on after the match
  aiMatchEnd(*this);   // W6
  for (auto& s : sold) { if (s.agent >= 0) Physics::destroyAgent(s.agent); s.agent = -1; if (s.ciReady) s.ci.destroy(); s.ciReady = false; }
  Physics::destroyAllAgents();
  sold.clear(); gren.clear(); sounds.clear();
  active = false; isClient = false;
  match.phase = MatchPhase::Setup;
}

// ---------------------------------------------------------------- spawning
void Game::chooseClass(Soldier& s) {
  const auto& C = classDefs();
  if (C.empty() || s.zombie || s.kit >= 2) { s.cls = -1; return; }   // SW1: kit soldiers carry only their blasters (no class kit, no explosion pack)
  auto ok = [&](int k) { return k >= 0 && k < (int)C.size() && match.rules.classAllowed(C[k].id); };
  if (s.localPlayer) { s.cls = app.deployClass; s.clsPick[0] = app.deployPick[0]; s.clsPick[1] = app.deployPick[1]; }
  if ((s.localPlayer || s.human) && ok(s.cls)) return;   // remote humans: set by their deploy request, checked here (host)
  float tot = 0; for (int k = 0; k < (int)C.size(); ++k) if (ok(k)) tot += C[k].botWeight;
  uint32_t h = (uint32_t)s.slot * 7919u + (uint32_t)s.lifeId * 104729u + 17u;
  s.cls = -1;
  if (tot > 0) { float r = (h % 10007u) / 10007.0f * tot; for (int k = 0; k < (int)C.size(); ++k) if (ok(k)) { s.cls = k; if ((r -= C[k].botWeight) < 0) break; } }
  else for (int k = 0; k < (int)C.size() && s.cls < 0; ++k) if (ok(k)) s.cls = k;   // only weight-0 classes allowed (Tanker)
  s.clsPick[0] = (int)(h >> 3); s.clsPick[1] = (int)(h >> 7);   // wrapped by classWeapons
}

std::vector<int> Game::classWeapons(const Soldier& s) const {
  std::vector<int> out;
  if (s.cls < 0 || s.cls >= (int)classDefs().size()) return out;
  const ClassSide& cs = classDefs()[s.cls].side(factionName(s.faction));
  auto pick = [&](const std::vector<std::string>& v, int k) {
    std::vector<int> w; for (auto& id : v) { int i = app.weaponIndex(id); if (i >= 0 && !(!s.human && !s.localPlayer && (app.weaponDefs[i]->action == "heavy" || app.weaponDefs[i]->tool))) w.push_back(i); }   // bots cannot fire the carried M2
    if (!w.empty()) out.push_back(w[(unsigned)k % w.size()]);
  };
  pick(cs.primary, s.clsPick[0]); pick(cs.secondary, s.clsPick[1]);
  if (!cs.pistol.empty()) pick({cs.pistol}, 0);
  return out;
}

void Game::applyLoadout(Soldier& s, bool newLife) {
  if (royaleBareHands(app, *this, s)) return;   // BR1: everyone lands with fists / the knife only (loot is the kit)
  std::string fac = factionName(s.faction);
  const json& lo = combat["loadouts"][fac];
  const ClassDef* cd = s.cls >= 0 && s.cls < (int)classDefs().size() ? &classDefs()[s.cls] : nullptr;
  std::vector<int> cw = classWeapons(s);
  // SW1: blaster troopers / robot commandos carry only their kit's blasters (combat.json kits.<kit>.ai, weighted per slot + life),
  // no sidearm, no grenades; the robot's health share (kits.robot.hp)
  if (s.kit >= 2 && !s.human && !s.localPlayer) {
    const json& kj = combat.value("kits", json::object()).value(s.kit == 2 ? "trooper" : "robot", json::object());
    std::vector<std::pair<int, int>> pool; int tot = 0;
    for (auto& e : kj.value("ai", json::array())) { int w = app.weaponIndex(e[0].get<std::string>()); if (w >= 0) { pool.push_back({w, e[1].get<int>()}); tot += e[1].get<int>(); } }
    if (!pool.empty()) {
      int w = pool[0].first, pick = tot > 0 ? (int)((s.slot * 7 + s.lifeId * 3) % tot) : 0;
      for (auto& [i, n] : pool) { if (pick < n) { w = i; break; } pick -= n; }
      s.wsBy.clear(); s.weapon = w;
      const WeaponDef& d = *app.weaponDefs[w]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
      for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
      s.ws.seed = s.rng | 1;
      s.carried = {w};
      if (newLife) { s.frags = 0; s.smokes = 0; s.hp *= kj.value("hp", 1.0f); }
      s.reload = 0; s.throwT = -1; s.throwReleased = false;
      return;
    }
  }
  // AI5: Operator bots carry modern weapons (combat.json loadouts_operator), not the faction pool or the class weapons:
  // a weighted primary per slot + life, every lmgEvery-th operator of a side the LMG, the sidearm carried too
  if (s.op && !s.human && !s.localPlayer && combat.contains("loadouts_operator") && combat["loadouts_operator"].contains(fac)) {
    const json& ol = combat["loadouts_operator"][fac];
    std::vector<std::pair<int, int>> pool; int tot = 0;
    for (auto& e : ol.value("ai", json::array())) { int w = app.weaponIndex(e[0].get<std::string>()); if (w >= 0) { pool.push_back({w, e[1].get<int>()}); tot += e[1].get<int>(); } }
    int nth = 0; for (auto& o : sold) { if (&o == &s) break; if (o.inUse && o.op && o.team == s.team) ++nth; }
    int lmg = app.weaponIndex(ol.value("lmg", "")), every = ol.value("lmgEvery", 5);
    if (!pool.empty() || lmg >= 0) {
      int w = pool.empty() ? lmg : pool[0].first;
      int pick = tot > 0 ? (int)((s.slot * 7 + s.lifeId * 3) % tot) : 0;
      for (auto& [i, n] : pool) { if (pick < n) { w = i; break; } pick -= n; }
      if (lmg >= 0 && every > 0 && nth % every == every - 1) w = lmg;
      s.wsBy.clear(); s.weapon = w;
      const WeaponDef& d = *app.weaponDefs[w]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
      for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
      s.ws.seed = s.rng | 1;
      s.carried = {w}; if (int p = app.weaponIndex(ol.value("sidearm", "")); p >= 0) s.carried.push_back(p);
      if (int L = app.weaponIndex(ol.value("launcher", "")), le = ol.value("launcherEvery", 4); L >= 0 && le > 0 && nth % le == 1 % le) s.carried.push_back(L);   // TK1: one in launcherEvery carries a rocket launcher
      if (newLife) { s.frags = cd ? cd->frags : match.rules.fragPerLife; s.smokes = cd ? cd->smokes : (s.role == CombatRole::Attacker ? match.rules.smokePerLifeAttacker : match.rules.smokePerLifeDefender); if (cd) s.hp *= cd->hpMul; }
      s.reload = 0; s.throwT = -1; s.throwReleased = false;
      return;
    }
  }
  auto items = [&](std::vector<int>& v) {   // the side's grenades travel only when the class carries some; the knife always
    if (!cd || cd->frags > 0) if (fragDef(s.faction) >= 0) v.push_back(fragDef(s.faction));
    if (!cd || cd->smokes > 0) if (smokeDef() >= 0) v.push_back(smokeDef());
    if (app.weaponIndex("knife_bayonet") >= 0) v.push_back(app.weaponIndex("knife_bayonet"));
  };
  if (cd && !cw.empty() && !s.zombie) {   // GM1: the class decides the weapons
    if (s.localPlayer) {
      app.resetWeapons();
      std::vector<int> pool = cw; items(pool); app.capCarried(pool);
      app.carried = pool; app.selectWeapon(pool[0]); s.weapon = app.activeWeapon;
    } else {
      s.wsBy.clear(); s.weapon = cw[0];
      const WeaponDef& d = *app.weaponDefs[s.weapon]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
      for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
      s.ws.seed = s.rng | 1;
      s.carried = cw; if (s.human) { items(s.carried); app.capCarried(s.carried); }
    }
    if (newLife) { s.frags = cd->frags; s.smokes = cd->smokes; s.hp *= cd->hpMul; }
    if (s.localPlayer) localGrenadeCounts(s, true);
    s.reload = 0; s.throwT = -1; s.throwReleased = false;
    return;
  }
  if (!s.localPlayer && s.human) {   // remote player (host side): the faction pool, ammo kept per weapon
    s.wsBy.clear(); s.weapon = std::max(0, app.weaponIndex(lo["player"][0].get<std::string>()));
    const WeaponDef& d = *app.weaponDefs[s.weapon]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
    s.carried.clear();   // the same pool the client's own applyLoadout builds below
    for (auto& id : lo["player"]) { int w = app.weaponIndex(id.get<std::string>()); if (w >= 0) s.carried.push_back(w); }
    for (int d2 : {fragDef(s.faction), smokeDef(), app.weaponIndex("knife_bayonet")}) if (d2 >= 0) s.carried.push_back(d2);
    app.capCarried(s.carried);   // G4: two main weapons (the pool's first two), the knife, the grenades
  } else if (s.localPlayer) {
    app.resetWeapons();
    std::vector<int> pool;
    for (auto& id : lo["player"]) { int w = app.weaponIndex(id.get<std::string>()); if (w >= 0) pool.push_back(w); }
    if (!pool.empty()) {
      // the side's grenades (E3 items) and the knife travel with the firearms; counts follow the match rules
      for (int d : {fragDef(s.faction), smokeDef(), app.weaponIndex("knife_bayonet")}) if (d >= 0) pool.push_back(d);
      app.capCarried(pool);   // G4: two main weapons (the pool's first two), the knife, the grenades
      app.carried = pool; app.selectWeapon(pool[0]);
    }
    s.weapon = app.activeWeapon;
  } else if (s.zombie) {   // G2: bare hands, nothing to throw
    s.weapon = -1; s.ws = WeaponState{}; s.frags = s.smokes = 0; s.meleeWind = -1; s.reload = 0; s.throwT = -1; s.throwReleased = false;
    return;
  } else {
    // weighted pool, deterministic per slot + life
    std::vector<std::pair<int, int>> pool; int tot = 0;
    for (auto& e : lo["ai"]) { int w = app.weaponIndex(e[0].get<std::string>()); if (w >= 0) { pool.push_back({w, e[1].get<int>()}); tot += e[1].get<int>(); } }
    int pick = tot > 0 ? (int)((s.slot * 7 + s.lifeId * 3) % tot) : 0;
    s.weapon = pool.empty() ? 0 : pool[0].first;
    for (auto& [w, n] : pool) { if (pick < n) { s.weapon = w; break; } pick -= n; }
    const WeaponDef& d = *app.weaponDefs[s.weapon];
    s.ws = WeaponState{}; s.ws.loaded = d.loaded;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
    s.ws.seed = s.rng | 1;
  }
  if (newLife) {   // grenades come only with a new life (never on objective change or possession transfer)
    s.frags = match.rules.fragPerLife;
    s.smokes = s.role == CombatRole::Attacker ? match.rules.smokePerLifeAttacker : match.rules.smokePerLifeDefender;
  }
  if (s.localPlayer) localGrenadeCounts(s, true);
  s.reload = 0; s.throwT = -1; s.throwReleased = false;
}

// G1: Solo (and TDM teams without markers): walkable points over the whole build area, validated once per match
const std::vector<vec3>& Game::spawnPool() {
  if (!poolBuilt) {
    poolBuilt = true; pool.clear();
    float hx = app.terrain.buildHalfX * 0.94f, hz = app.terrain.buildHalfZ * 0.94f; const int n = 28;   // inside the border on both axes
    if (lootIndoorMap(app)) {   // BR11: Tunnels / Backrooms: a ray from the sky lands on the roof or the surface above; inside points instead
      vec3 p; for (int k = 0; k < n * n && (int)pool.size() < 400; ++k) if (lootIndoorPoint(*this, p)) pool.push_back(p);
    } else
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
      float x = -hx + (2 * hx) * (i + 0.5f) / n, z = -hz + (2 * hz) * (j + 0.5f) / n;
      Physics::Hit h; float top = app.terrain.height(x, z) + 60;
      if (!Physics::castRayEx(vec3(x, top, z), vec3(0, -1, 0), 120.0f, h)) continue;
      if (h.pos.y < app.terrain.waterLevel + 0.2f) continue;                       // not in the sea / a pond
      if (!Physics::shapeFitsAt(Physics::STAND, h.pos, false)) continue;
      if (nav.valid() && nav.nearest(h.pos, 1.5f) < 0) continue;
      pool.push_back(h.pos);
    }
    for (auto& m : match.sc.spawns) pool.push_back(m.pos);                         // authored markers count too
    logf("match: spawn pool of %zu walkable points", pool.size());
  }
  return pool;
}

bool Game::trySpawn(int slot, std::string& why) {
  Soldier& s = sold[slot];
  struct Cand { vec3 pos; float yaw; };
  std::vector<Cand> cands;
  const GameMode mode = match.mode();
  if (mode == GameMode::Assault) {
    for (auto* m : match.sc.spawnsFor(s.role, match.active)) cands.push_back({m->pos, m->yaw});
    if (cands.empty()) { why = std::string("no ") + (s.role == CombatRole::Attacker ? "attacker" : "defender") + " spawn markers for this stage"; return false; }
    int start = (slot * 3 + (int)steps) % (int)cands.size();
    std::rotate(cands.begin(), cands.begin() + start, cands.end());
  } else {
    if (mode == GameMode::TDM) for (auto& m : match.sc.spawns) if (m.team == s.team) cands.push_back({m.pos, m.yaw});
    if (mode == GameMode::SearchDestroy) for (auto& m : match.sc.spawns) if (m.team < 0 && m.role == s.role) cands.push_back({m.pos, m.yaw});   // SD1: the side's spawn (roles swap at halftime)
    if (cands.empty()) {   // Solo / unmarked TDM team: the walkable points farthest from living enemies
      std::vector<std::pair<float, vec3>> scored;
      for (vec3 p : spawnPool()) {
        float d = 1e9f;
        for (auto& o : sold) if (o.alive && hostile(s, o)) d = std::min(d, glm::length(vec2(o.feet.x - p.x, o.feet.z - p.z)));
        scored.push_back({d, p});
      }
      std::sort(scored.begin(), scored.end(), [](auto& x, auto& y) { return x.first > y.first; });
      size_t keep = std::min<size_t>(scored.size(), 6);   // a little spread so two respawns don't stack on one point
      for (size_t k = 0; k < keep; ++k) { vec3 p = scored[(k + slot) % keep].second; cands.push_back({p, glm::degrees(std::atan2(-p.x, -p.z))}); }   // face the middle
    } else if (mode == GameMode::TDM && cands.size() > 1) {
      // CV1: a random marker per respawn, safe ones first: no living enemy within 18 m and none with a clear line of
      // sight to it (eye to chest, 100 m); then the rest, farthest from enemies first (the loop below still skips
      // occupied and blocked spots)
      std::mt19937 rg((uint32_t)steps * 2654435761u ^ (uint32_t)slot * 40503u ^ (uint32_t)s.lifeId * 97u);
      std::shuffle(cands.begin(), cands.end(), rg);
      auto risk = [&](const Cand& c) {
        float dMin = 1e9f; bool seen = false;
        for (auto& o : sold) {
          if (&o == &s || !o.alive || !hostile(s, o)) continue;
          vec3 eye = o.feet + vec3(0, 1.6f, 0), d = c.pos + vec3(0, 1.3f, 0) - eye;
          float L = glm::length(d); dMin = std::min(dMin, L);
          if (!seen && L < 100.0f && L > 1.0f) { Physics::Hit h; seen = !Physics::castRayEx(eye + d / L * 0.6f, d / L, L - 0.9f, h); }
        }
        return std::make_pair(dMin < 18.0f || seen, dMin);
      };
      std::vector<std::pair<std::pair<bool, float>, Cand>> rs;
      for (auto& c : cands) rs.push_back({risk(c), c});
      std::stable_sort(rs.begin(), rs.end(), [](const auto& a, const auto& b) { return a.first.first != b.first.first ? !a.first.first : (a.first.first && a.first.second > b.first.second); });
      for (size_t k = 0; k < rs.size(); ++k) cands[k] = rs[k].second;
    } else {
      int start = (slot * 3 + (int)steps) % (int)cands.size();
      std::rotate(cands.begin(), cands.begin() + start, cands.end());
    }
    if (cands.empty()) { why = "no walkable spawn point on this map"; return false; }
  }
  if (s.localPlayer && deploySpawn >= 0) {   // GM1: the point chosen on the deploy screen first (the others stay as fallbacks)
    auto opts = spawnOptions(slot);
    if (deploySpawn < (int)opts.size()) cands.insert(cands.begin(), Cand{opts[deploySpawn].pos, opts[deploySpawn].yaw});
  }
  static const float ring[3] = {0.0f, 1.2f, 2.4f};
  for (const Cand& m : cands) {
    for (float r : ring)
      for (int k = 0; k < (r == 0 ? 1 : 8); ++k) {
        float a = k * 0.785398f + slot * 0.37f;
        vec3 q = m.pos + vec3(std::cos(a) * r, 0, std::sin(a) * r);
        Physics::Hit h;
        if (!Physics::castRayEx(q + vec3(0, 2.0f, 0), vec3(0, -1, 0), 6.0f, h)) continue;
        vec3 feet = h.pos;
        bool occupied = false;
        for (auto& o : sold) if (&o != &s && o.alive && glm::length(o.feet - feet) < 0.75f) occupied = true;   // never inside another capsule
        if (occupied) continue;
        if (!Physics::shapeFitsAt(Physics::STAND, feet, false)) continue;                                       // never inside geometry
        if (nav.valid() && nav.nearest(feet, 1.5f) < 0) continue;                                              // navigable access
        // success
        s.feet = feet; s.yaw = m.yaw; s.pitch = 0; s.alive = true; s.flat = false; s.hp = s.zombie ? enemyType(s.etype).hp : combat.value("health", 100.0f); s.killer = -1; s.staggerUntil = 0; s.posture = Physics::STAND;
        s.human = match.slots[slot].human || match.slots[slot].pendingPeer >= 0; s.peer = match.slots[slot].pendingPeer >= 0 ? match.slots[slot].pendingPeer : match.slots[slot].peer;
        ++s.lifeId;
        aiForget(*this, slot);   // a fresh life: no stale last-known position of it anywhere
        rulesOnSpawn(*this, s);   // W1: spawn protection starts (SPEC 2.2)
        if (s.localPlayer) {
          Physics::setCharacterPos(feet); app.eyeYaw = m.yaw; app.eyePitch = 0; app.exploreStartPos = feet; app.R.resetTemporal();
        } else {
          if (s.agent >= 0) Physics::destroyAgent(s.agent);
          const EnemyType* et = s.zombie ? &enemyType(s.etype) : nullptr;   // EN2: a heavy's own capsule
          s.agent = et && et->capsuleH > 0 ? Physics::createAgent(feet, et->capsuleH, et->capsuleR) : Physics::createAgent(feet);
        }
        if (match.mode() == GameMode::SearchDestroy) { s.cls = -1; sdOnSpawned(*this, slot); }   // SD1: the kept / bought gear, no classes
        else { chooseClass(s);
        applyLoadout(s, true); }
        if (s.localPlayer) { app.staminaMul = s.cls >= 0 ? classDefs()[s.cls].staminaMul : 1.0f; app.stamina = app.staminaCap(); app.staminaIdle = 0; }   // GM1: full stamina on respawn
        if (s.cls >= 0 && match.mode() != GameMode::SearchDestroy) { if (s.localPlayer) gm2ApplyClass(app, classDefs()[s.cls]); else gm2ApplyClassSoldier(app, slot, s, classDefs()[s.cls]); }   // GM2: packs, artillery
        s.br = Brain{s.br.arch, s.br.reaction, s.br.aimTurn, s.br.sigma, s.br.sight, s.br.fov, s.br.aggression, s.br.burstMin, s.br.burstMax};
        s.br.lane = match.slots[slot].lane; s.br.aimYaw = m.yaw; s.br.grenadeCD = 3.0f + rnd(s) * 3;
        s.br.nextPerceive = (float)t + rnd(s) * 0.2f; s.br.nextTactic = (float)t + rnd(s);
        match.onSpawned(slot);
        if (s.localPlayer) { spectate = -1; deployReady = app.selftest; deploySpawn = -1; }
        if (s.localPlayer && s.cls >= 0 && classDefs()[s.cls].drivesTanks) {   // GM1b: the Tanker starts inside the nearest free tank of its side
          uint64_t best = 0; float bd = 1e9f;
          for (auto& [id, tk] : app.tanks.tanks) {
            if (!tk.def || tk.def->car || tk.def->heli || tk.wrecked || app.tanks.occupied == id || (tk.def->faction == "Axis") != (s.faction == Faction::Axis)) continue;
            float d = glm::length(tk.pos - feet); if (d < bd) { bd = d; best = id; }
          }
          if (best && !app.tanks.occupied && app.tanks.enter(app, best)) app.toast("Tanker: in the " + app.tanks.get(best)->def->name + ", hatch closed", 4);
          else app.toast(std::string("Tanker: no free ") + (s.faction == Faction::Axis ? "Tiger" : "Sherman") + " for you, spawned on foot", 5);
        }
        if (s.localPlayer && !respawnVehicle.empty()) {   // TK1: a fresh copy of the vehicle it died in, beside the spawn (one per death)
          std::string aid = respawnVehicle; respawnVehicle.clear();
          if (app.tanks.occupied) {}   // the Tanker class already sat it in one
          else if (const VehicleSpawn* vs = match.sc.vehicleSpawnFor(s.team, feet); vs && app.tanks.spawnFresh(app, aid, vs->pos, vs->yaw, respawnStation, 0.0f)) app.toast("A fresh vehicle is ready for you at your vehicle spawn", 3);   // VH7
          else if (app.tanks.spawnFresh(app, aid, feet, wrapDeg(m.yaw + 180.0f), respawnStation)) app.toast("A fresh vehicle is ready for you", 3);
          else app.toast("No clear ground for a vehicle at this spawn - on foot", 4);
        }
        return true;
      }
  }
  why = mode == GameMode::Assault ? "every marker for this stage is blocked or not walkable" : "every spawn point is blocked or not walkable";
  return false;
}

void Game::killSoldier(int i, int killer, vec3 dir) {
  Soldier& s = sold[i];
  if (gAdmin.god && s.localPlayer && !gAdmin.allowKill) return;   // CMD2: no direct death either (a heli crash, out of the world)
  if (s.zombie && s.alive && !enemyType(s.etype).sndDeath.empty()) app.audio.play(enemyType(s.etype).sndDeath.c_str(), s.feet + vec3(0, 1.0f, 0), 1.0f);   // SND1. shortcut: host / single player only
  if (!s.alive) return;
  aiForget(*this, i);   // nobody keeps attacking where it fell
  s.alive = false; s.diedAt = t; s.killer = killer; s.deathDir = dir; s.hp = 0;
  vec3 f = vec3(-std::sin(radians(s.yaw)), 0, -std::cos(radians(s.yaw)));
  s.deathStyle = glm::dot(f, dir) > 0.3f ? 1 : glm::dot(f, dir) < -0.3f ? 0 : 2;
  if (s.throwT >= 0 && !s.throwReleased) s.throwT = -1;   // cancelled before release: nothing spent
  if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; }   // the corpse has no collision
  if (s.localPlayer && app.tanks.occupied) {   // TK1: died crewing a vehicle: its type comes back with the next life; the seat is cleared
    const Instance* vi = app.doc.find(app.tanks.occupied); const TankRT* vt = app.tanks.get(app.tanks.occupied);
    respawnVehicle = vi ? vi->assetId : std::string(); respawnStation = vt ? vt->station : 0;
    app.tanks.vacate(app);
  }
  if (s.localPlayer) {
    deployOpenedAt = t; deployReady = app.selftest;   // GM1: the deploy screen opens (tests skip it)
    vec3 p = s.feet; Physics::setCharacterPos(vec3(p.x, p.y - kLimboDepth, p.z));
    spectate = killer >= 0 && match.mode() != GameMode::SearchDestroy ? killer : -1;   // SD1: teammates only
  }
  bool cased = lootOnDeath(*this, i);   // BR5: in Battle Royale everything it carried goes into a case by the body
  if (!cased && !s.localPlayer && !s.human && !s.zombie && s.weapon >= 0 && !isClient) {   // GM1: the AI's weapon falls by the body
    if (!(s.weapon >= 0 && s.weapon < (int)app.weaponDefs.size() && app.weaponDefs[s.weapon]->locked)) drops.push_back({s.feet + vec3(0, 0.06f, 0), s.yaw, s.weapon, s.ws, t});   // GM3: never a locked weapon
    if (drops.size() > 24) drops.erase(drops.begin());
  }
  if (killer >= 0 && killer != i && killer < (int)sold.size() && sold[killer].zombie && enemyType(sold[killer].etype).id.rfind("fighter", 0) == 0) app.audio.play("fighter_laugh", sold[killer].feet + vec3(0, 2, 0), 1.0f);   // shortcut: host / single player only
  if (killer == localSlot && killer != i && hostile(sold[killer], s)) killMarker = 0.6f;   // red marker: enemy down
  match.onDeath(i, killer);
  if (match.mode() == GameMode::SearchDestroy) sdOnKill(*this, i, killer, s.lastHow);   // SD1: rewards, the dropped bomb
  if (killer >= 0 && killer != i) ++kills;
  for (auto& o : sold) if (o.br.target == i) o.br.target = -1;
  for (auto& d : defSpots) if (d.heldBy == i) d.heldBy = -1;
}

// ---------------------------------------------------------------- damage
bool Game::hitTest(const Soldier& s, vec3 a, vec3 d, float maxT, HitZone& hz) const {
  // coarse sphere around the body
  float bs = bodyScale(s), zs = s.zombie ? bs * (s.eFallback ? 1.0f : enemyType(s.etype).zoneScale) : 1.0f;   // EN1: the type's size and hit-zone scale
  vec3 c = s.feet + vec3(0, 0.9f * bs, 0);
  vec3 m = a - c; float b = glm::dot(m, d), R0 = 1.3f * std::max(bs, zs), cc = glm::dot(m, m) - R0 * R0;
  if (cc > 0 && b > 0) return false;
  float disc = b * b - cc; if (disc < 0) return false;
  // posed parts (model space -> world)
  const vec3* J = s.pose.joint;
  auto W = [&](int j) { return vec3(s.world * vec4(J[j], 1)); };
  struct Cap { vec3 p, q; float r; int zone; };
  vec3 head = W(CJ_HEAD), neck = W(CJ_NECK);
  vec3 headTop = head + glm::normalize(head - neck + vec3(1e-4f)) * 0.12f * bs;
  Cap caps[] = {
    {head, headTop, 0.11f, 0},
    {W(CJ_HIPS), W(CJ_NECK), 0.17f, 1},
    {W(CJ_LARM), W(CJ_LFOREARM), 0.06f, 2}, {W(CJ_LFOREARM), W(CJ_LHAND), 0.055f, 2},
    {W(CJ_RARM), W(CJ_RFOREARM), 0.06f, 2}, {W(CJ_RFOREARM), W(CJ_RHAND), 0.055f, 2},
    {W(CJ_LUPLEG), W(CJ_LLEG), 0.085f, 2}, {W(CJ_LLEG), W(CJ_LFOOT), 0.07f, 2},
    {W(CJ_RUPLEG), W(CJ_RLEG), 0.085f, 2}, {W(CJ_RLEG), W(CJ_RFOOT), 0.07f, 2},
  };
  float best = maxT, headT = 1e9f; int zone = -1;
  for (auto& k : caps) k.r *= zs;
  for (auto& k : caps) {
    // ray vs capsule: sample the closest approach between the ray and the segment
    vec3 u = d, v = k.q - k.p, w = a - k.p;
    float A = glm::dot(u, u), B = glm::dot(u, v), C = glm::dot(v, v), D = glm::dot(u, w), E = glm::dot(v, w);
    float den = A * C - B * B, sc, tc;
    if (den < 1e-8f) { sc = 0; tc = C > 1e-8f ? E / C : 0; }
    else { sc = (B * E - C * D) / den; tc = (A * E - B * D) / den; }
    tc = std::clamp(tc, 0.0f, 1.0f);
    sc = std::max(0.0f, glm::dot(k.p + v * tc - a, u) / A);
    vec3 cp = a + u * sc, cq = k.p + v * tc;
    float dist = glm::length(cp - cq);
    if (dist > k.r) continue;
    float back = std::sqrt(std::max(0.0f, k.r * k.r - dist * dist));
    float tt = std::max(0.0f, sc - back);
    if (k.zone == 0) headT = std::min(headT, tt);
    if (tt < best) { best = tt; zone = k.zone; }
  }
  // CH7: on an enemy type a ray that meets the head capsule is a head hit (on the big hunched rigs the scaled torso capsule
  // round the neck enclosed most of the head, so a shot at the head counted as the torso); soldiers keep their zones
  if (s.zombie && headT < maxT) { best = headT; zone = 0; }
  if (zone < 0) return false;
  hz.zone = zone; hz.t = best;
  return true;
}

bool Game::shellSegment(const Projectile& p, vec3 a, vec3 seg, float len, float& tHit) {
  if (!active || isClient || (p.kind != PROJ_HE && p.kind != PROJ_ROCKET) || len <= 1e-5f) return false;
  vec3 d = seg / len;
  int shooter = soldierOfOwner(p.owner), best = -1; float bt = tHit;
  for (size_t i = 0; i < sold.size(); ++i) {
    Soldier& s = sold[i];
    if (!s.alive || (int)i == shooter) continue;
    HitZone hz;
    if (hitTest(s, a, d, bt, hz) && hz.t < bt) { bt = hz.t; best = (int)i; }
  }
  if (best < 0) return false;
  tHit = bt;
  damage(best, 10000.0f, shooter, d, "shell");   // a direct hit kills (spawn protection and team rules still apply)
  return true;
}

bool Game::projectileSegment(Projectile& p, vec3 a, vec3 seg, float len, float& tHit) {
  if (!active || isClient || p.kind != PROJ_BULLET) return false;
  vec3 d = seg / len;
  int shooter = soldierOfOwner(p.owner);
  int best = -1; HitZone bz; float bt = tHit;
  for (size_t i = 0; i < sold.size(); ++i) {
    Soldier& s = sold[i];
    if (!s.alive || (int)i == shooter) continue;
    HitZone hz;
    if (hitTest(s, a, d, bt, hz) && hz.t < bt) { bt = hz.t; best = (int)i; bz = hz; }
  }
  if (best >= 0) tHit = bt;
  return dmBulletHit(*this, p, best, bz, a, d, tHit, shooter);   // W1 damage model (damage_model.cpp): table, zones, helmet, protection, FleshHit
}

void Game::damage(int i, float dmg, int attacker, vec3 dir, const char* how) {
  Soldier& s = sold[i];
  if (!s.alive || dmg <= 0) return;
  if (rulesIgnoreDamage(*this, i)) return;
  if (gAdmin.god && s.localPlayer) return;   // CMD2 /admin: nothing hurts you (bullets, blasts, melee, flame, falls, drowning, storm)
  if (s.zombie && enemyType(s.etype).shellKills > 0) {   // a fixed number of direct shell hits kills it
    static std::map<int, double> shellAt; const bool shell = how && std::strcmp(how, "shell") == 0, blast = how && std::strcmp(how, "blast") == 0;
    if (shell) { shellAt[i] = t; s.hp -= enemyType(s.etype).hp / enemyType(s.etype).shellKills - 0.01f; dmg = 0.01f; how = "blast"; }   // applied here; the rest of the function sees a token blast
    else if (blast && shellAt.count(i) && std::fabs(t - shellAt[i]) < 0.05) return;   // that shell's own blast
  }
  if (s.zombie && (!how || std::strcmp(how, "blast") != 0)) { if (enemyType(s.etype).blastOnly || (enemyType(s.etype).nonBlastMult < 1 && how && std::strcmp(how, "shell") == 0)) return; dmg *= enemyType(s.etype).nonBlastMult;   // a direct shell hit is not an instant kill on a resistant type: its blast does the damage
  if (dmg <= 0) return; }   // immune to all but explosions   // W1: spawn-protected (bullets, blasts, falls)
  // TK1: melee on the crew of an armoured tank lands on the tank: a heavy's blow wears it down (1/15 each), the rest do
  // nothing; the crew is hurt only once the tank is destroyed
  if (s.localPlayer && app.tanks.occupied && how && std::strcmp(how, "melee") == 0) {
    auto it = app.tanks.tanks.find(app.tanks.occupied);
    if (it != app.tanks.tanks.end() && !it->second.def->car && !it->second.def->heli && !it->second.wrecked) {
      if (attacker >= 0 && attacker < (int)sold.size() && sold[attacker].zombie && enemyType(sold[attacker].etype).tankDamage) app.tanks.meleeBlow(app, it->first, sold[attacker].feet);
      return;
    }
  }
  // buttoned up in a tank (hatch closed): the crew takes no damage until the tank is knocked out (destroy() bails them out).
  // Head out of the hatch stays exposed.
  if (s.localPlayer && app.tanks.occupied) {
    auto it = app.tanks.tanks.find(app.tanks.occupied);
    if (it != app.tanks.tanks.end() && !it->second.def->car && !it->second.wrecked && it->second.station == TS_GUNNER && it->second.hatchT == 0) return;
  }
  if (attacker >= 0 && attacker != i && sameTeam(attacker, i) && !match.rules.friendlyFire) return;
  if (match.mode() == GameMode::SearchDestroy) {   // SD1: no damage in freeze / breaks; the mode's armor (spec §8)
    if (sdFrozen()) return;
    dmg = sdArmor(*this, i, dmg, how, attacker); s.lastHow = how ? how : "";
    if (dmg <= 0) return;
  }
  s.hp -= dmg;
  if (s.zombie) { const EnemyType& e = enemyType(s.etype); if (e.staggerS > 0 && dmg >= e.staggerMin) s.staggerUntil = t + e.staggerS; }   // EN1: only a heavy hit staggers
  s.br.hurtFrom = attacker >= 0 ? sold[attacker].feet : s.feet - dir * 10.0f; s.br.hurtAt = t;
  s.br.suppressed = std::min(1.0f, s.br.suppressed + 0.5f);
  if (s.localPlayer) { hurtFlash = std::min(1.0f, hurtFlash + dmg / 60.0f); lastHurtDir = -dir; }
  if (s.hp <= 0) killSoldier(i, attacker, dir);
  (void)how;
}

void Game::explosionDamage(vec3 p, float dmg, float radius, int owner, bool grenade) {
  if (!active || isClient) return;
  float R = grenade ? gren.tun.fragMaxR : radius * combat["explosion"].value("personnelRadiusFactor", 3.0f);
  for (size_t i = 0; i < sold.size(); ++i) {
    Soldier& s = sold[i];
    if (!s.alive) continue;
    vec3 c = chest(s), h = eye(s);
    float d = glm::length(c - p);
    if (d > R) continue;
    float raw = grenade ? gren.tun.fragDamage * std::pow(std::max(0.0f, 1.0f - std::max(0.0f, d - 0.5f) / R), 1.6f) : dmg * (1.0f - d / R);
    float occ = 0.5f * app.blastOcclusion(p, c) + 0.5f * app.blastOcclusion(p, h);   // solid walls / ground / sandbags shelter
    if (!grenade && app.blastPersonnel >= 1.99f) {   // GM3: tank / AT gun shells (blastPersonnel x2): in the open full, behind an object 30 %
      auto vis = [&](vec3 q) { return app.blastOcclusion(p, q) >= 0.999f ? 1.0f : 0.3f; };
      occ = 0.5f * vis(c) + 0.5f * vis(h);
    }
    raw *= occ * combat["explosion"].value("scale", 1.0f);
    if (s.zombie) raw *= enemyType(s.etype).explosiveMult;   // EN2: heavies take x5 from every blast
    if (raw > 1) damage((int)i, raw, owner, glm::normalize(c - p + vec3(0, 1e-3f, 0)), "blast");
  }
}

// FT1: damage plus ignition from the flame stream / ground fire. The burn (burnSec at burnDps) keeps damaging after the
// soldier leaves the stream; clients get a "burn" event for the look. Refused hits (spawn protection, own team) ignite nothing.
bool Game::flameHit(int victim, int owner, float dmg, vec3 dir, float burnSec, float burnDps) {
  if (!active || isClient || victim < 0 || victim >= (int)sold.size()) return false;
  Soldier& v = sold[victim];
  if (!v.alive) return false;
  float hp0 = v.hp;
  damage(victim, dmg, owner, dir, "flame");
  bool hurt = v.hp < hp0;
  // catching fire takes 0.4 s of continuous exposure (was instant); a gap of 0.5 s starts the count again
  static std::unordered_map<int, std::pair<double, double>> heat;   // victim -> first hit, last hit
  auto& h = heat[victim]; if (t - h.second > 0.5 || t < h.first) h.first = t; h.second = t;
  if (hurt && v.alive && (v.burnT > 0.05f || t - h.first >= (dir.y > 0.99f ? 0.8 : 0.4))) {   // straight up = a ground fire patch (flame.cpp): twice as long to catch
    bool fresh = v.burnT <= 0.05f;
    v.burnT = std::max(v.burnT, burnSec); v.burnDps = burnDps; v.burnBy = owner;
    if (fresh && gNet && gNet->role == Net::HOST) gNet->hostWorldEvent(json{{"t", "burn"}, {"s", victim}, {"d", burnSec}});
  }
  return hurt;
}

void Game::burnStep(float dt) {
  for (auto& s : sold) {
    if (!s.inUse || s.burnT <= 0) continue;
    if (!s.alive) { s.burnT = 0; continue; }
    s.burnT -= dt;
    Fx& fx = app.fx;
    if (!s.localPlayer && fx.parts.size() < fx.maxParts * 0.85f) {   // flames on the body (the local player's own view stays clear)
      for (int i = 0; i < 2; ++i) {
        Particle q; q.p = s.feet + vec3(fx.rnd() - 0.5f, 0.15f + 1.5f * fx.rnd(), fx.rnd() - 0.5f) * vec3(0.5f, 1, 0.5f);
        q.v = vec3(fx.rnd() - 0.5f, 1.0f + fx.rnd() * 1.4f, fx.rnd() - 0.5f) * 0.7f; q.size = 0.3f + 0.25f * fx.rnd(); q.life = q.maxLife = 0.25f + 0.25f * fx.rnd();
        q.col = vec4(7.0f + 2.0f * fx.rnd(), 2.6f + 1.2f * fx.rnd(), 0.6f, 1); q.additive = true; q.grow = -q.size * 0.9f; q.drag = 0.8f; q.lift = 1.8f;
        fx.parts.push_back(q);
      }
      if (fx.rnd() < 0.08f) fx.smoke(s.feet + vec3(0, 1.7f, 0), vec3(0.1f, 1.2f, 0.1f), 0.5f, 1.6f, vec4(0.2f, 0.19f, 0.18f, 0.4f), 0.9f, 0.9f);
    }
    if (!isClient) {
      s.burnTick += dt;
      if (s.burnTick >= 0.25f) { s.burnTick = 0; damage(s.slot, s.burnDps * 0.25f, s.burnBy, vec3(0, 1, 0), "burn"); }
    }
  }
}

// FT1: a bot's flame gun: the nozzle open for dt (fuel by the second), fired from the muzzle like a rifle's round
bool Game::soldierFlame(Soldier& s, vec3 dir, float dt) {
  if (s.weapon < 0 || s.throwT >= 0) return false;
  const WeaponDef& d = *app.weaponDefs[s.weapon];
  WeaponState& w = s.ws;
  if (!d.flame.on || w.loaded <= 0) return false;
  flameDrain(w, d, dt);
  vec3 e = eye(s), muzzle = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1)), start = muzzle;
  Physics::Hit hb;
  if (Physics::castRayEx(e, muzzle - e, glm::length(muzzle - e) + 1e-3f, hb)) start = e;   // muzzle inside a wall: from the eye
  app.flame.spray(app, s.slot, s.weapon, start, dir, dt, true);
  s.recoil = 0.25f; s.lastShot = t; ++s.shots;
  w.sinceShot += dt;
  if (w.sinceShot >= 0.4f) { w.sinceShot = 0; hearSound(muzzle, (int)s.role, archetypes["perception"].value("hearShot_m", 70.0f), s.slot, 0); ++shotsByBots; }
  return true;
}

// ---------------------------------------------------------------- soldier weapons
bool Game::soldierFire(Soldier& s, vec3 dir) {
  if (s.weapon < 0 || s.throwT >= 0) return false;
  const WeaponDef& d = *app.weaponDefs[s.weapon];
  WeaponState& w = s.ws;
  if (w.cooldown > 0 || s.reload > 0 || w.bolt > 0) return false;
  if (w.loaded <= 0) return false;
  w.loaded--; w.cooldown = 60.0f / std::max(1.0f, d.rpm); if (d.action == "bolt") w.bolt = d.boltTime; ++w.shotIdx; ++s.shots;
  if (d.action == "bolt" && !s.localPlayer && app.audio.hasEvent("bolt_cycle_" + d.id) && glm::length(s.feet - app.eyePos()) < 80.0f) app.audio.play("bolt_cycle_" + d.id, s.feet + vec3(0, 1.5f, 0), 0.6f);   // SND4: a bot works its bolt too
  vec3 e = eye(s);
  vec3 muzzle = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1));
  Physics::Hit hb;
  vec3 start = muzzle;
  if (Physics::castRayEx(e, muzzle - e, glm::length(muzzle - e) + 1e-3f, hb)) start = e;   // muzzle inside a wall: fire from the eye
  float tz = d.zero / std::max(1.0f, d.velocity), drop = d.blaster ? 0.0f : 0.5f * 9.81f * tz * tz;   // SW1: bolts do not drop
  vec3 dd = glm::normalize(dir + vec3(0, drop / std::max(1.0f, d.zero), 0));
  int tracer = d.tracerKind(w.shotIdx);
  if (d.action == "launcher") {   // TK1: a bot's rocket, as the player's (weapons.cpp): blast, crater, backblast
    float g = 9.81f * d.rocketGrav, tr = d.zero / std::max(1.0f, d.velocity), dr = 0.5f * g * tr * tr;
    dd = glm::normalize(dir + vec3(0, dr / std::max(1.0f, d.zero), 0));
    app.fireProjectile(start, dd, d.velocity, PROJ_ROCKET, d.rocketDmg, ~0ull, false, d.container);
    Projectile& rp = app.projectiles.back();
    rp.blastDmg = d.rocketDmg; rp.blastR = d.rocketR; rp.craterScale = d.rocketCrater; rp.splashGun = d.rocketSplashGun; rp.atKill = d.rocketAtKill; rp.gravScale = d.rocketGrav; rp.owner = s.slot;
    if (d.rocketBackblast) app.fx.backblast(muzzle - dd * 1.4f, -dd);
    tracer = 0;
  } else
  app.fireRounds(d, start, dd, tracer, (uint32_t)s.slot * 131u + (uint32_t)w.shotIdx * 7919u, s.slot);   // MG1: a shotgun's pellets
  app.fx.muzzleFlash(muzzle + dd * 0.02f, dd, d.flashScale * 1.2f, d.suppressed, muzzle, d.blaster ? blasterColour(d.blaster) : vec3(-1));   // WP4: the same flash and light as the player's
  app.fx.smoke(muzzle, dd * 2.0f + vec3(0, 0.3f, 0), 0.05f, 1.2f, vec4(0.6f, 0.58f, 0.55f, 0.35f), 0.6f, 0.2f);
  app.audio.play("shot_" + d.id, muzzle, 1.0f);
  if (!s.localPlayer) app.shotCasing(d, s.feet, s.slot);   // SND4
  s.recoil = 1.0f; s.lastShot = t;
  hearSound(muzzle, (int)s.role, archetypes["perception"].value("hearShot_m", 70.0f), s.slot, 0);
  ++shotsByBots;
  if (gNet && gNet->role == Net::HOST) gNet->hostWorldEvent(json{{"t", "shot"}, {"p", tojson(start)}, {"d", tojson(dd)}, {"w", s.weapon}, {"tr", tracer}, {"by", s.slot}});
  return true;
}

bool Game::bulletProof(const Soldier& o) const {
  if (!o.localPlayer || !app.tanks.occupied) return false;
  auto it = app.tanks.tanks.find(app.tanks.occupied);
  return it != app.tanks.tanks.end() && !it->second.wrecked && !it->second.def->car && !it->second.def->heli && it->second.station == TS_GUNNER && it->second.hatchT == 0;   // VH7: popped out of the hatch: shootable
}
bool Game::bigTarget(const Soldier& o) const {
  if (o.zombie) { const EnemyType& e = enemyType(o.etype); return e.tankDamage || e.headMult > 0; }
  if (!o.localPlayer || !app.tanks.occupied) return false;
  auto it = app.tanks.tanks.find(app.tanks.occupied);
  if (it == app.tanks.tanks.end() || it->second.wrecked) return false;
  const TankDef& d = *it->second.def;
  return !d.car || d.heli || d.heavyBulletJ > 0;   // tanks, the helicopter, armoured cars (not the jeep)
}
int Game::carriedLauncher(const Soldier& s) const {
  for (int w : s.carried) if (w >= 0 && w < (int)app.weaponDefs.size() && app.weaponDefs[w]->action == "launcher") return w;
  return -1;
}
void Game::switchWeapon(Soldier& s, int w) {
  if (w < 0 || w == s.weapon || s.reload > 0) return;
  if (s.weapon >= 0) s.wsBy[s.weapon] = s.ws;
  auto it = s.wsBy.find(w);
  if (it != s.wsBy.end()) { s.ws = it->second; s.wsBy.erase(it); }
  else {   // first draw: its full issue
    const WeaponDef& d = *app.weaponDefs[w]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
    s.ws.seed = s.rng | 1;
  }
  s.weapon = w; s.ws.cooldown = std::max(s.ws.cooldown, 0.6f);   // shouldering it takes a moment
}

void Game::soldierWeaponStep(Soldier& s, float dt) {
  if (s.weapon < 0) return;
  const WeaponDef& d = *app.weaponDefs[s.weapon];
  WeaponState& w = s.ws;
  w.cooldown = std::max(0.0f, w.cooldown - dt); w.bolt = std::max(0.0f, w.bolt - dt);
  s.recoil = std::max(0.0f, s.recoil - dt * 8.0f);
  if (s.reload > 0) {
    s.reload -= dt;
    if (s.reload <= 0) {
      s.reload = 0;
      if (w.containers.empty()) return;
      if (d.feed == "single" || d.feed == "stripper") {
        int need = d.cap - w.loaded;
        for (auto& c : w.containers) { int n = std::min(need, c); c -= n; w.loaded += n; need -= n; if (!need) break; }
      } else {
        auto it = std::max_element(w.containers.begin(), w.containers.end());
        int n = *it; w.containers.erase(it);
        if (w.loaded > 0 && d.feed != "enbloc") w.containers.push_back(w.loaded);   // partial magazine kept
        w.loaded = n;
      }
      w.containers.erase(std::remove(w.containers.begin(), w.containers.end(), 0), w.containers.end());
      app.audio.play(d.feed == "enbloc" || d.feed == "stripper" ? "clip_in" : d.feed == "belt" ? "belt" : "mag_in", s.feet + vec3(0, 1.2f, 0), 0.6f);
    }
  }
}

bool Game::startThrow(Soldier& s, int kind, vec3 vel, vec3 target) {
  if (!s.alive || s.throwT >= 0 || s.reload > 0) return false;
  if ((kind == GK_FRAG ? s.frags : s.smokes) <= 0) return false;   // inventory is checked again at release
  s.throwT = 0; s.throwKind = kind; s.throwVel = vel; s.throwTarget = target; s.throwReleased = false;
  return true;
}

void Game::throwStep(Soldier& s, float dt) {
  if (s.throwT < 0) return;
  const float dur = 1.0f, release = 0.55f;
  float before = s.throwT;
  s.throwT += dt / dur;
  if (before < release && s.throwT >= release && !s.throwReleased) {
    int& inv = s.throwKind == GK_FRAG ? s.frags : s.smokes;
    if (inv > 0 && s.alive) {
      --inv;                                             // spent exactly once, on a successful release
      s.throwReleased = true;
      vec3 hand = s.localPlayer ? app.eyePos() + vec3(0, -0.1f, 0) : vec3(s.world * vec4(s.pose.joint[CJ_RHAND], 1));
      if (!s.localPlayer) { Physics::Hit h; vec3 e = eye(s); if (Physics::castRayEx(e, hand - e, glm::length(hand - e) + 1e-3f, h)) hand = e; }
      app.spawnGrenade(s.throwKind == GK_FRAG ? fragDef(s.faction) : smokeDef(), hand, s.throwVel, s.slot);
      app.audio.play("bolt", hand, 0.4f);
      (s.throwKind == GK_FRAG ? fragThrows : smokeThrows)++;
    } else s.throwT = -1;
  }
  if (s.throwT >= 1.0f) s.throwT = -1;
}

// ---------------------------------------------------------------- server step
void Game::clientStep(float dt) {
  // mirror only: the host owns spawning, damage, AI, grenades and capture
  ++steps;
  sdClientStep(*this, dt);   // SD1: the mirrored bomb
  burnStep(dt);   // FT1: the look of the soldiers the host set alight
  netInterpolate(*this, t, dt);
  if (Soldier* L = local(); L && L->alive) {
    auto c = Physics::character();
    L->vel = (c.pos - L->feet) / std::max(dt, 1e-4f); L->feet = c.pos; L->yaw = app.eyeYaw; L->pitch = app.eyePitch; L->posture = c.posture; L->lean = app.lean.x; L->weapon = app.activeWeapon; L->swim = app.swim.active;
    L->speed = glm::length(vec2(L->vel.x, L->vel.z));
  }
  // door toggles made locally are requests: undo them here and ask the host
  for (auto& [id, r] : app.rt) for (size_t k = 0; k < r.dir.size(); ++k) {
    uint64_t key = (id << 8) | (uint64_t)k; int seen = doorSeen.count(key) ? doorSeen[key] : 0;
    if (r.dir[k] != 0 && r.dir[k] != seen) { if (gNet) gNet->clientInteract(id, (int)k); r.dir[k] = seen; }
  }
  for (size_t k = 0; k < ghosts.size();) {
    Ghost& g = ghosts[k]; vec3 seg = g.v * dt; Physics::Hit h;
    bool hit = Physics::castRayEx(g.p, seg, glm::length(seg), h);
    vec3 e = hit ? h.pos : g.p + seg;
    if (g.tracer) app.fx.bullet(g.p, e, g.tracer); else app.fx.streak(g.p, e, 0.007f, 0.025f, vec4(0.7f, 0.68f, 0.62f, 1));
    g.p = e; g.v.y -= 9.81f * dt; g.life += dt;
    if (hit || g.life > 3) { ghosts.erase(ghosts.begin() + k); continue; }
    ++k;
  }
  gren.expire(t);
  gren.particles(app.fx, t, dt);
  if (Soldier* L = local(); L && L->alive) localGrenadeCounts(*L, false);
}

void Game::step(float dt) {
  if (!active || paused) return;
  hitMarker = std::max(0.0f, hitMarker - dt); killMarker = std::max(0.0f, killMarker - dt); hurtFlash = std::max(0.0f, hurtFlash - dt * 0.6f);
  if (isClient) { clientStep(dt); posesStep(dt); return; }
  auto c0 = std::chrono::steady_clock::now();
  struct Timer { Game& g; std::chrono::steady_clock::time_point c; ~Timer() { g.stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c).count(); ++g.stepCalls; } } timer{*this, c0};
  t += dt; ++steps;
  // local player mirror
  if (Soldier* L = local(); L && L->alive) {
    localGrenadeCounts(*L, false);   // the E3 items are the local inventory (spent at release)
    auto c = Physics::character();
    if (glm::length(c.pos - L->feet) > 15.0f) aiForget(*this, L->slot);   // a jump (teleport, admin fly, a vehicle exit far away): enemies re-acquire
    L->vel = (c.pos - L->feet) / std::max(dt, 1e-4f); L->feet = c.pos; L->yaw = app.eyeYaw; L->pitch = app.eyePitch; L->posture = c.posture; L->lean = app.lean.x;
    L->weapon = app.activeWeapon; L->swim = app.swim.active;
    L->speed = glm::length(vec2(L->vel.x, L->vel.z));
  }
  // occupancy sample at step start (capture precedence uses it)
  for (auto& s : sold) match.reportAlive(s.slot, s.alive, s.feet);
  sdStep(*this, dt);   // SD1: rounds, the bomb, the economy (respawns at a round start go through stepRest)
  crumb("game: poses"); posesStep(dt);
  crumb("game: doors");
  // doors moved on the host (players, bots, client requests) -> clients
  if (gNet && gNet->role == Net::HOST)
    for (auto& [id, r] : app.rt) for (size_t k = 0; k < r.dir.size(); ++k) {
      uint64_t key = (id << 8) | (uint64_t)k; int seen = doorSeen.count(key) ? doorSeen[key] : 0;
      if (r.dir[k] != seen) { doorSeen[key] = r.dir[k]; gNet->hostWorldEvent(json{{"t", "door"}, {"i", id}, {"k", k}, {"dir", r.dir[k]}, {"p", r.progress[k]}}); }
    }
  for (auto& s : sold) {
    if (!s.alive) continue;
    crumb("game: soldier weapon"); soldierWeaponStep(s, dt);
    crumb("game: throw"); throwStep(s, dt);
    crumb("game: ai"); if (!s.human && !royaleAirborne(s.slot)) aiStep(s, dt);   // BR1: aboard / falling: the drop moves it
  }
  crumb("game: loot"); lootStep(*this, dt);   // BR2: strip new lives, heal, bots pick up
  crumb("game: burn"); burnStep(dt);
  crumb("game: rest"); stepRest(dt); crumb("game: done");
}

void Game::posesStep(float dt) {
  // AI2 crowds (over 200 combatants): far soldiers re-pose every 2nd step beyond 80 m, every 4th beyond 150 m (staggered)
  const bool poseLod = sold.size() > 200; vec3 cam, camF; renderView(cam, camF);   // the actual camera (vehicle chase / sight cameras too)
  for (auto& s : sold) {
    if (!s.inUse || (!s.alive && t - s.diedAt > (s.zombie ? 3.5 : 2.0))) continue;   // G2: the zombie death clip runs ~3 s
    const CharModel& m = *models[s.model];
    PoseParams pp;
    pp.walkAmount = std::min(1.0f, s.speed / 3.0f);
    float wantCrouch = s.posture == Physics::CROUCH ? 1.0f : 0.0f;
    s.crouchBlend += (wantCrouch - s.crouchBlend) * std::min(1.0f, dt * 8);
    pp.crouch = s.crouchBlend; pp.prone = s.posture == Physics::PRONE ? 1.0f : 0.0f;
    pp.aimPitch = radians(s.localPlayer ? app.eyePitch : s.br.aimPitch);
    float yawOff = s.localPlayer ? 0.0f : std::clamp(radians(wrapDeg(s.br.aimYaw - s.yaw + 180.0f) - 180.0f), -0.9f, 0.9f);
    if (s.zombie) { s.walkPhase += s.speed * dt * 2.6f; pp.aimYawOffset = yawOff; }
    else {   // AN1: kneel (still, crouched), carry (moving, not engaged), moveAim (moving, engaged); ~0.25 s blends
      float v = s.speed, k = 1.0f - std::exp(-dt / 0.08f);
      float cad = std::min(1.75f, 0.75f + 0.25f * v);   // gait cycles per second: the planted foot travels back at the body speed
      s.walkPhase += v * dt > 0 ? 6.2831853f * cad * dt : 0.0f;
      float duty = std::clamp(0.62f - 0.17f * (v - 1.5f), 0.30f, 0.62f);
      s.anStride += (0.5f * duty * v / cad - s.anStride) * k;
      pp.gait = true; pp.stride = s.anStride; pp.duty = duty;
      float yawUse = s.localPlayer ? app.eyeYaw : s.yaw;
      quat bodyQ = glm::angleAxis(radians(yawUse) + 3.14159265f, vec3(0, 1, 0));
      vec3 mv = glm::inverse(bodyQ) * vec3(s.vel.x, 0, s.vel.z);
      if (glm::length(mv) > 0.3f) s.anDir = glm::normalize(glm::mix(s.anDir, glm::normalize(mv), k));
      else s.anDir = glm::normalize(glm::mix(s.anDir, vec3(0, 0, 1), k));
      pp.moveDir = s.anDir;
      bool moving = v > 0.5f;
      bool engaged = (s.br.target >= 0 && !s.human && !s.localPlayer) || t - s.lastShot < 1.5 || s.reload > 0 || s.throwT >= 0
                  || (isClient && s.netEngaged);   // AN1b: a LAN client has no brains: the host's "has a target" bit (net.cpp)
      s.anKneel += ((s.posture == Physics::CROUCH && !moving ? 1.0f : 0.0f) - s.anKneel) * k;
      // AI5: bots never carry the rifle low on the move: shouldered, muzzle forward, sights up whether engaged or not
      bool bot = !s.human && !s.localPlayer;
      s.anCarry += ((moving && !engaged && !bot ? 1.0f : 0.0f) - s.anCarry) * k;
      s.anMoveAim += ((moving && (engaged || bot) ? 1.0f : 0.0f) - s.anMoveAim) * k;
      pp.hunch = bot ? 1.0f : 0.0f;
      s.anLow += ((!moving && !engaged ? (s.posture == Physics::CROUCH ? 0.25f : 0.45f) : 0.0f) - s.anLow) * k;   // relaxed ready: shouldered low
      s.anTwist += (yawOff - s.anTwist) * (1.0f - std::exp(-dt / 0.12f));   // no snapping torso twist
      pp.kneel = s.anKneel; pp.carry = s.anCarry; pp.moveAim = s.anMoveAim; pp.aimYawOffset = s.anTwist;
      pp.lean = s.alive ? s.lean : 0.0f; pp.leanBend = radians(app.leanCfg.bend);   // MV3
      {   // AN1b: the terrain's gradient under the feet (model space) for the stepping feet and the kneel; floors and bridges stay flat
        const float r = 0.4f; vec3 f = s.feet; float h0 = app.terrain.height(f.x, f.z);
        vec2 g(0);
        if (std::fabs(h0 - f.y) < 0.15f && s.alive) {
          g = vec2(app.terrain.height(f.x + r, f.z) - app.terrain.height(f.x - r, f.z), app.terrain.height(f.x, f.z + r) - app.terrain.height(f.x, f.z - r)) / (2 * r);
          if (float gl = glm::length(g); gl > 0.7f) g *= 0.7f / gl;
        }
        vec3 gm = glm::inverse(bodyQ) * vec3(g.x, 0, g.y);
        s.anSlope += (vec2(gm.x, gm.z) - s.anSlope) * k;
        pp.slope = s.anSlope;
      }
    }
    pp.walkPhase = s.walkPhase;
    if (s.weapon >= 0) { const WeaponDef& d = *app.weaponDefs[s.weapon]; pp.gripR = d.gripR; pp.gripL = d.gripL; pp.magPos = d.gripL + vec3(0, -0.1f, -0.12f); pp.holdOffset = d.holdOffset; pp.holdWeld = d.holdWeld; pp.holdKind = d.holdKind; }
    pp.reload = s.reload > 0 && s.reloadTotal > 0 ? 1.0f - s.reload / s.reloadTotal : 0;
    pp.throwT = s.throwT; pp.recoil = s.recoil;
    pp.lowReady = s.zombie ? 0.0f : std::max(s.anLow, !s.human && !s.localPlayer && s.speed > 4.0f ? 0.2f : 0.0f);   // AI5: a sprinting bot dips the muzzle a little, weapon still forward   // AN1: the run carry (pp.carry) replaces the lowered muzzle on the move
    if (s.localPlayer) app.mantlePose(s.feet, pp);   // hands on the ledge while mantling (mantle.cpp)
    s.swimBlend += ((s.swim && s.alive ? 1.0f : 0.0f) - s.swimBlend) * std::min(1.0f, dt * 5);
    pp.swim = s.swimBlend; pp.swimPhase = (float)t * 5.3f + (float)s.slot;   // breaststroke ~0.85 Hz
    if (s.localPlayer) app.swimPose(pp);   // swim.cpp: the player's own stroke phase
    if (!s.alive) { pp.death = std::min(1.0f, (float)(t - s.diedAt) / 0.8f); pp.deathStyle = s.deathStyle; }
    float camD = poseLod ? glm::length(s.feet - cam) : 0.0f; int every = camD > 150 ? 4 : camD > 80 ? 2 : 1;
    bool skipPose = poseLod && !s.localPlayer && s.alive && every > 1 && (steps + s.slot) % every != 0;
    if (s.zombie && !m.clips.empty()) aiZombiePose(*this, s, dt); else if (!skipPose) { poseCharacter(m, pp, s.pose); s.poseFrom = -1; ++s.poseRev; }   // G2: clips
    float yawUse = s.localPlayer ? app.eyeYaw : s.yaw;
    s.world = glm::translate(mat4(1), s.feet) * glm::mat4_cast(glm::angleAxis(radians(yawUse) + 3.14159265f, vec3(0, 1, 0)));
    if (s.zombie) {   // EN1: the type's draw scale, and a forward lean about the feet (the stalker on the zombie rig)
      if (s.eLean != 0 && s.alive) s.world = glm::rotate(s.world, radians(s.eLean), vec3(1, 0, 0));
      if (s.eScale != 1) s.world = glm::scale(s.world, vec3(s.eScale));
      aiZombieFeet(*this, s); }
    if (s.flat && !s.alive) {   // squashed: the whole body pressed into a thin, slightly spread layer on the ground, within 0.12 s
      float k = std::min(1.0f, (float)(t - s.diedAt) / 0.12f);
      s.world = glm::translate(mat4(1), s.feet) * glm::scale(mat4(1), vec3(1.0f + 0.25f * k, 1.0f - 0.96f * k, 1.0f + 0.25f * k)) * glm::translate(mat4(1), -s.feet) * s.world;
    }
  }
}

void Game::stepRest(float dt) {
  // grenades
  crumb("game: grenades");
  // grenades: bodies, fuses and blasts are the shared core (App::stepGrenades); smoke clouds and the AI view here
  syncGrenadeView();
  gren.particles(app.fx, t, dt);
  // match rules
  match.update(dt);
  crumb("game: spawns");
  for (int slot : match.spawnQueue()) {
    if (sold[slot].localPlayer && !deployReady && t - deployOpenedAt < kDeployAuto) continue;   // GM1: the deploy screen is open
    std::string why;
    if (!trySpawn(slot, why)) match.onSpawnFailed(slot, dt, why);
  }
  // corpses
  int maxC = combat["corpses"].value("max", 12); double life = combat["corpses"].value("seconds", 40.0);
  int nC = 0; for (auto& s : sold) if (!s.alive && s.lifeId > 0 && t - s.diedAt < life) ++nC;
  (void)maxC; (void)nC;
  sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [&](const Sound& x) { return t - x.t > 3.0; }), sounds.end());
}

void Game::hearSound(vec3 p, int team, float range, int src, int kind) { sounds.push_back({p, t, team, range, src, kind}); }

void Game::worldChanged(vec3 lo, vec3 hi) {
  if (!active || !nav.valid()) return;
  nav.rebuildArea(lo, hi);
  ++navRebuilds;
  for (auto& d : defSpots) if (d.p.x > lo.x - 3 && d.p.x < hi.x + 3 && d.p.z > lo.z - 3 && d.p.z < hi.z + 3) { defSpotsStage = -1; break; }   // re-evaluate cover after breaches
  for (auto& s : sold) if (s.alive && !s.human) s.br.repathT = 0;
}

// ---------------------------------------------------------------- presentation
void Game::drawItems() {
  if (!active) return;
  double life = combat["corpses"].value("seconds", 40.0);
  drops.erase(std::remove_if(drops.begin(), drops.end(), [&](const Drop& d) { return t - d.t > life; }), drops.end());
  for (auto& d : drops) {   // GM1: lying on its side next to the body
    const WeaponDef& w = *app.weaponDefs[d.weapon];
    mat4 W = glm::translate(mat4(1), d.pos) * glm::rotate(mat4(1), radians(d.yaw + 90.0f), vec3(0, 1, 0)) * glm::rotate(mat4(1), radians(90.0f), vec3(0, 0, 1)) * glm::translate(mat4(1), vec3(0, 0, -0.4f));
    if (w.useModel()) { for (int p = 0; p < 8; ++p) for (int k : w.worldParts()[p]) app.R.worldModels.push_back({w.worldModel(), k, W}); }
    else for (int p = 0; p < 8; ++p) if (w.mesh[p].count) app.worldBoxDraws.push_back({&w.mesh[p], W});
  }
  lootDraw(*this);   // BR2 floor items
  crumb("game: draw");
  vec3 cam, fwd; renderView(cam, fwd);   // cull against the camera actually rendering (a vehicle's chase / sight camera), not the seated body
  static int frame = 0; ++frame;
  for (auto& s : sold) {
    if (!s.inUse || s.lifeId == 0) continue;
    if (!s.alive && t - s.diedAt > combat["corpses"].value("seconds", 40.0)) continue;
    if (s.localPlayer && s.alive) continue;                 // first person: the viewmodel is drawn instead
    float bs = bodyScale(s);   // giants (the fighters at scale 20) reach far above and around their feet
    vec3 c = s.feet + vec3(0, 0.9f * bs, 0);
    float dist = glm::length(c - cam);
    if (dist > 300 + 2 * bs) continue;
    if (dist > 4 * bs && glm::dot(glm::normalize(c - cam), fwd) < -0.2f) continue;   // behind the camera (shadows of close ones kept)
    // PF1: a far pack follower in step with its leader draws the leader's skinned mesh (zombie.cpp aiZombiePose)
    Soldier* src = s.poseFrom >= 0 && s.poseFrom < (int)sold.size() && sold[s.poseFrom].ciReady && sold[s.poseFrom].ci.frameSkinned >= 0 ? &sold[s.poseFrom] : &s;
    if (src != &s || (!s.alive && t - s.diedAt > (s.zombie ? 3.5 : 2.0) && s.ciReady && s.ci.frameSkinned >= 0)) {}   // settled corpse: keep the last skin
    else {
      if (!s.ciReady) { s.ci.create(*models[s.model]); s.ciReady = true; }
      int every = dist > 140 ? 6 : dist > 70 ? 3 : 1;   // far: skin less often
      bool due = s.ci.frameSkinned < 0 || (s.skinRev != s.poseRev && (frame + s.slot) % every == 0);   // and never an unchanged pose
      if (due) { s.ci.skin(s.pose.skin); s.ci.frameSkinned = frame; s.skinRev = s.poseRev; }
    }
    for (int k = 0; k < (int)src->ci.view.parts.size(); ++k) if (!brHidePart(s, src->ci.view.parts[k].name)) app.R.worldModels.push_back({&src->ci.view, k, s.world, s.zombie ? rulesTint(*this, s) * vec4(enemyType(s.etype).tint, 1) : rulesTint(*this, s)});   // W1: protection shimmer
    if (!(s.localPlayer && s.alive)) brWornDraws(app, *this, s, s.world, app.R.worldModels, app.worldBoxDraws);   // BR3: helmet / vest / backpack; never in your own first person (the corpse and the death camera keep it)
    if (s.alive && s.weapon >= 0) {
      const WeaponDef& d = *app.weaponDefs[s.weapon];
      if (d.useModel()) {
        mat4 W = s.world * s.pose.weapon;
        for (int p = 0; p < 8; ++p) for (int k : d.worldParts()[p]) app.R.worldModels.push_back({d.worldModel(), k, W});
        brSightDraws(app, d, s.ws, W, app.R.worldModels, app.worldBoxDraws);   // BR3: the fitted optic
        lootAttachDraws(app, d, s.ws, W, app.R.worldModels, app.worldBoxDraws);   // BR10: muzzle, magazine, recoil pad
      }
    }
  }
  // thrown grenades: the host draws its own bodies (App::grenadeDrawItems); a LAN client draws the snapshot view
  if (isClient)
    for (auto& g : gren.live) {
      if (g.def < 0 || g.def >= (int)app.weaponDefs.size() || !app.weaponDefs[g.def]->useModel()) continue;
      const GpuModel& gm = *app.weaponDefs[g.def]->model;
      vec3 c = (gm.bmin + gm.bmax) * 0.5f;
      mat4 M = glm::translate(mat4(1), g.p) * glm::mat4_cast(g.rot) * glm::translate(mat4(1), -c);
      for (int k = 0; k < (int)gm.parts.size(); ++k) app.R.worldModels.push_back({&gm, k, M});
    }
}

bool Game::camera(vec3& e, vec3& f, float& fov) {
  if (active && aiDebugCamera(*this, e, f, fov)) return true;   // W6: AI debug follow camera
  Soldier* L = local();
  if (!active || (L && L->alive)) return false;
  if (!L) {   // LAN client waiting for its slot: watch a defender
    int w = -1; for (auto& s : sold) if (s.alive && s.role == CombatRole::Defender) { w = s.slot; break; }
    if (w < 0) return false;
    vec3 target = sold[w].feet + vec3(0, 1.6f, 0), back = vec3(std::sin(radians(sold[w].yaw)), 0.45f, std::cos(radians(sold[w].yaw))) * 3.2f;
    e = target + back; f = glm::normalize(-back); fov = 70; return true;
  }
  // death / spectator camera: orbit behind the watched soldier (teammate or killer), never through walls
  int w = spectate;
  if (w < 0 || w >= (int)sold.size() || !sold[w].alive) {
    w = -1;
    for (auto& s : sold) if (s.alive && s.team == L->team && &s != L) { w = s.slot; break; }   // Solo: none, so the killer or the body
    if (t - L->diedAt < 3.0 && L->killer >= 0 && sold[L->killer].alive && match.mode() != GameMode::SearchDestroy) w = L->killer;   // SD1: never an enemy
    spectate = w;
  }
  vec3 target = w >= 0 ? sold[w].feet + vec3(0, 1.6f, 0) : L->feet + vec3(0, 1.0f, 0);
  float yaw = w >= 0 ? sold[w].yaw : L->yaw;
  vec3 back = vec3(std::sin(radians(yaw)), 0.45f, std::cos(radians(yaw))) * 3.2f;
  Physics::Hit h; vec3 cpos = target + back;
  if (Physics::castRayEx(target, back, glm::length(back), h)) cpos = target + glm::normalize(back) * std::max(0.3f, h.t - 0.2f);
  e = cpos; f = glm::normalize(target - cpos); fov = 70;
  return true;
}
