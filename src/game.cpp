// Match game layer: lifecycle, spawning, soldier weapons / grenades, damage, presentation. AI is in ai.cpp.
#include "game.h"
#include "gore_api.h"
#include "rules.h"
#include "damage_model.h"
#include "app.h"
#include "mpui.h"
#include "net.h"
#include "crumb.h"
#include "ai_hooks.h"
void netInterpolate(Game& g, double t);
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <chrono>

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
vec3 Game::eye(const Soldier& s) const {
  if (s.localPlayer && s.alive) return app.eyePos();
  float h = s.posture == Physics::PRONE ? 0.32f : s.posture == Physics::CROUCH ? 1.05f : 1.62f;
  return s.feet + vec3(0, h, 0);
}
vec3 Game::chest(const Soldier& s) const {
  float h = s.posture == Physics::PRONE ? 0.25f : s.posture == Physics::CROUCH ? 0.8f : 1.25f;
  return s.feet + vec3(0, h, 0);
}

int Game::soldierOfOwner(int owner) const { return owner >= 0 ? owner : localSlot; }

// ---------------------------------------------------------------- lifecycle
bool Game::begin(std::string& err) {
  pendingStart = false;
  if (!loadData(err) || !ensureModels(err)) { setupError = err; return false; }
  json scj = !gUi.matchScenario.is_null() ? gUi.matchScenario : app.doc.extra.is_object() ? app.doc.extra.value("scenario", json()) : json();
  Scenario sc = Scenario::fromJson(scj);
  auto issues = validateScenario(sc, false);
  for (auto& i : issues) if (i.error) { err = i.what; setupError = err; return false; }
  int humans = isClient ? 0 : std::max<int>(1, (int)humanPeers.size());
  if (!match.setup(sc, humans, err)) { setupError = err; return false; }
  if (!isClient) { int k = 0; for (auto& rs : match.slots) if (rs.human && k < (int)humanPeers.size()) { rs.peer = humanPeers[k].first; rs.name = humanPeers[k].second; ++k; } }
  else {   // the host's roster, exactly
    for (auto& sj : clientStart["slots"]) { int id = sj["id"]; if (id < 0 || id >= (int)match.slots.size()) continue; auto& rs = match.slots[id];
      rs.faction = (Faction)sj["f"].get<int>(); rs.role = (CombatRole)sj["r"].get<int>(); rs.human = sj["h"]; rs.peer = sj["p"]; rs.name = sj["n"]; rs.lane = sj.value("lane", 1); }
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
  float half = app.terrain.buildHalf;
  lo = glm::max(lo, vec3(-half)); hi = glm::min(hi, vec3(half));
  if (!isClient) aiMatchBegin(*this, lo, hi);   // W6: navmesh bake (Recast/Detour) and AI setup
  // soldiers per roster slot
  sold.clear(); sold.resize(match.slots.size());
  auto& arr = archetypes["archetypes"];
  for (auto& rs : match.slots) {
    Soldier& s = sold[rs.id];
    s.slot = rs.id; s.inUse = true; s.alive = false; s.faction = rs.faction; s.role = rs.role;
    s.human = rs.human; s.peer = rs.peer; s.localPlayer = rs.id == localSlot;
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
  }
  sounds.clear(); gren.clear(); grenadeReserve.clear(); smokeReserve.clear(); defSpots.clear(); defSpotsStage = -1;
  t = 0; steps = 0; spectate = -1; hitMarker = hurtFlash = 0;
  shotsByBots = hitsOnSoldiers = kills = doorOpens = smokeThrows = fragThrows = navRebuilds = 0;
  if (!isClient) match.start(); else { match.phase = MatchPhase::Running; for (auto& o : match.obj) o.state = ObjState::Locked; if (!match.obj.empty()) match.obj[0].state = ObjState::Active; }
  ghosts.clear(); doorSeen.clear();
  active = true;
  // the local player waits in limbo until its first spawn
  if (Soldier* L = local()) { vec3 p = app.exploreStartPos; Physics::setCharacterPos(vec3(p.x, p.y - kLimboDepth, p.z)); (void)L; }
  setupError.clear();
  logf("match: %zu slots, local slot %d, nav %d floors in %.0f ms", sold.size(), localSlot, nav.usedNodes, nav.buildMs);
  return true;
}

void Game::end() {
  aiMatchEnd(*this);   // W6
  for (auto& s : sold) { if (s.agent >= 0) Physics::destroyAgent(s.agent); s.agent = -1; if (s.ciReady) s.ci.destroy(); s.ciReady = false; }
  Physics::destroyAllAgents();
  sold.clear(); gren.clear(); sounds.clear();
  active = false; isClient = false;
  match.phase = MatchPhase::Setup;
}

// ---------------------------------------------------------------- spawning
void Game::applyLoadout(Soldier& s, bool newLife) {
  std::string fac = factionName(s.faction);
  const json& lo = combat["loadouts"][fac];
  if (!s.localPlayer && s.human) {   // remote player (host side): the faction pool, ammo kept per weapon
    s.wsBy.clear(); s.weapon = std::max(0, app.weaponIndex(lo["player"][0].get<std::string>()));
    const WeaponDef& d = *app.weaponDefs[s.weapon]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
    s.carried.clear();   // the same pool the client's own applyLoadout builds below
    for (auto& id : lo["player"]) { int w = app.weaponIndex(id.get<std::string>()); if (w >= 0) s.carried.push_back(w); }
    for (int d2 : {fragDef(s.faction), smokeDef(), app.weaponIndex("knife_bayonet")}) if (d2 >= 0) s.carried.push_back(d2);
  } else if (s.localPlayer) {
    app.resetWeapons();
    std::vector<int> pool;
    for (auto& id : lo["player"]) { int w = app.weaponIndex(id.get<std::string>()); if (w >= 0) pool.push_back(w); }
    if (!pool.empty()) {
      // the side's grenades (E3 items) and the knife travel with the firearms; counts follow the match rules
      for (int d : {fragDef(s.faction), smokeDef(), app.weaponIndex("knife_bayonet")}) if (d >= 0) pool.push_back(d);
      app.carried = pool; app.selectWeapon(pool[0]);
    }
    s.weapon = app.activeWeapon;
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

bool Game::trySpawn(int slot, std::string& why) {
  Soldier& s = sold[slot];
  auto marks = match.sc.spawnsFor(s.role, match.active);
  if (marks.empty()) { why = std::string("no ") + (s.role == CombatRole::Attacker ? "attacker" : "defender") + " spawn markers for this stage"; return false; }
  static const float ring[3] = {0.0f, 1.2f, 2.4f};
  int start = (slot * 3 + (int)steps) % (int)marks.size();
  for (size_t mi = 0; mi < marks.size(); ++mi) {
    const SpawnMarker& m = *marks[(start + mi) % marks.size()];
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
        s.feet = feet; s.yaw = m.yaw; s.pitch = 0; s.alive = true; s.hp = combat.value("health", 100.0f); s.killer = -1; s.posture = Physics::STAND;
        s.human = match.slots[slot].human || match.slots[slot].pendingPeer >= 0; s.peer = match.slots[slot].pendingPeer >= 0 ? match.slots[slot].pendingPeer : match.slots[slot].peer;
        ++s.lifeId;
        rulesOnSpawn(*this, s);   // W1: spawn protection starts (SPEC 2.2)
        if (s.localPlayer) {
          Physics::setCharacterPos(feet); app.eyeYaw = m.yaw; app.eyePitch = 0; app.exploreStartPos = feet; app.R.resetTemporal();
        } else {
          if (s.agent >= 0) Physics::destroyAgent(s.agent);
          s.agent = Physics::createAgent(feet);
        }
        applyLoadout(s, true);
        s.br = Brain{s.br.arch, s.br.reaction, s.br.aimTurn, s.br.sigma, s.br.sight, s.br.fov, s.br.aggression, s.br.burstMin, s.br.burstMax};
        s.br.lane = match.slots[slot].lane; s.br.aimYaw = m.yaw; s.br.grenadeCD = 3.0f + rnd(s) * 3;
        s.br.nextPerceive = (float)t + rnd(s) * 0.2f; s.br.nextTactic = (float)t + rnd(s);
        match.onSpawned(slot);
        if (s.localPlayer) spectate = -1;
        return true;
      }
  }
  why = "every marker for this stage is blocked or not walkable";
  return false;
}

void Game::killSoldier(int i, int killer, vec3 dir) {
  Soldier& s = sold[i];
  if (!s.alive) return;
  s.alive = false; s.diedAt = t; s.killer = killer; s.deathDir = dir; s.hp = 0;
  vec3 f = vec3(-std::sin(radians(s.yaw)), 0, -std::cos(radians(s.yaw)));
  s.deathStyle = glm::dot(f, dir) > 0.3f ? 1 : glm::dot(f, dir) < -0.3f ? 0 : 2;
  if (s.throwT >= 0 && !s.throwReleased) s.throwT = -1;   // cancelled before release: nothing spent
  if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; }   // the corpse has no collision
  if (s.localPlayer) {
    vec3 p = s.feet; Physics::setCharacterPos(vec3(p.x, p.y - kLimboDepth, p.z));
    spectate = killer >= 0 ? killer : -1;
  }
  match.onDeath(i, killer);
  if (killer >= 0 && killer != i) ++kills;
  for (auto& o : sold) if (o.br.target == i) o.br.target = -1;
  for (auto& d : defSpots) if (d.heldBy == i) d.heldBy = -1;
}

// ---------------------------------------------------------------- damage
bool Game::hitTest(const Soldier& s, vec3 a, vec3 d, float maxT, HitZone& hz) const {
  // coarse sphere around the body
  vec3 c = s.feet + vec3(0, 0.9f, 0);
  vec3 m = a - c; float b = glm::dot(m, d), cc = glm::dot(m, m) - 1.3f * 1.3f;
  if (cc > 0 && b > 0) return false;
  float disc = b * b - cc; if (disc < 0) return false;
  // posed parts (model space -> world)
  const vec3* J = s.pose.joint;
  auto W = [&](int j) { return vec3(s.world * vec4(J[j], 1)); };
  struct Cap { vec3 p, q; float r; int zone; };
  vec3 head = W(CJ_HEAD), neck = W(CJ_NECK);
  vec3 headTop = head + glm::normalize(head - neck + vec3(1e-4f)) * 0.12f;
  Cap caps[] = {
    {head, headTop, 0.11f, 0},
    {W(CJ_HIPS), W(CJ_NECK), 0.17f, 1},
    {W(CJ_LARM), W(CJ_LFOREARM), 0.06f, 2}, {W(CJ_LFOREARM), W(CJ_LHAND), 0.055f, 2},
    {W(CJ_RARM), W(CJ_RFOREARM), 0.06f, 2}, {W(CJ_RFOREARM), W(CJ_RHAND), 0.055f, 2},
    {W(CJ_LUPLEG), W(CJ_LLEG), 0.085f, 2}, {W(CJ_LLEG), W(CJ_LFOOT), 0.07f, 2},
    {W(CJ_RUPLEG), W(CJ_RLEG), 0.085f, 2}, {W(CJ_RLEG), W(CJ_RFOOT), 0.07f, 2},
  };
  float best = maxT; int zone = -1;
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
    if (tt < best) { best = tt; zone = k.zone; }
  }
  if (zone < 0) return false;
  hz.zone = zone; hz.t = best;
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
  if (rulesIgnoreDamage(*this, i)) return;   // W1: spawn-protected (bullets, blasts, falls)
  if (attacker >= 0 && attacker != i && sameTeam(attacker, i) && !match.rules.friendlyFire) return;
  s.hp -= dmg;
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
    raw *= occ * combat["explosion"].value("scale", 1.0f);
    if (raw > 1) damage((int)i, raw, owner, glm::normalize(c - p + vec3(0, 1e-3f, 0)), "blast");
  }
}

// ---------------------------------------------------------------- soldier weapons
bool Game::soldierFire(Soldier& s, vec3 dir) {
  if (s.weapon < 0 || s.throwT >= 0) return false;
  const WeaponDef& d = *app.weaponDefs[s.weapon];
  WeaponState& w = s.ws;
  if (w.cooldown > 0 || s.reload > 0 || w.bolt > 0) return false;
  if (w.loaded <= 0) return false;
  w.loaded--; w.cooldown = 60.0f / std::max(1.0f, d.rpm); if (d.action == "bolt") w.bolt = d.boltTime; ++w.shotIdx; ++s.shots;
  vec3 e = eye(s);
  vec3 muzzle = vec3(s.world * s.pose.weapon * vec4(d.muzzle, 1));
  Physics::Hit hb;
  vec3 start = muzzle;
  if (Physics::castRayEx(e, muzzle - e, glm::length(muzzle - e) + 1e-3f, hb)) start = e;   // muzzle inside a wall: fire from the eye
  float tz = d.zero / std::max(1.0f, d.velocity), drop = 0.5f * 9.81f * tz * tz;
  vec3 dd = glm::normalize(dir + vec3(0, drop / std::max(1.0f, d.zero), 0));
  bool tracer = d.tracerEvery > 0 && w.shotIdx % d.tracerEvery == 0;
  app.fireProjectile(start, dd, d.velocity, PROJ_BULLET, 0, ~0ull, tracer, d.container);
  app.projectiles.back().mass = d.bulletMass * 0.001f;
  app.projectiles.back().owner = s.slot;
  app.fx.flash(muzzle + dd * 0.05f, d.action == "auto" ? 0.12f : 0.16f, 0.04f, vec4(8, 5.5f, 2.5f, 1));
  app.fx.smoke(muzzle, dd * 2.0f + vec3(0, 0.3f, 0), 0.05f, 1.2f, vec4(0.6f, 0.58f, 0.55f, 0.35f), 0.6f, 0.2f);
  app.audio.play("shot_" + d.id, muzzle, 1.0f);
  s.recoil = 1.0f; s.lastShot = t;
  hearSound(muzzle, (int)s.role, archetypes["perception"].value("hearShot_m", 70.0f), s.slot, 0);
  ++shotsByBots;
  if (gNet && gNet->role == Net::HOST) gNet->hostWorldEvent(json{{"t", "shot"}, {"p", tojson(start)}, {"d", tojson(dd)}, {"w", s.weapon}, {"tr", tracer}, {"by", s.slot}});
  return true;
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
  netInterpolate(*this, t);
  if (Soldier* L = local(); L && L->alive) {
    auto c = Physics::character();
    L->vel = (c.pos - L->feet) / std::max(dt, 1e-4f); L->feet = c.pos; L->yaw = app.eyeYaw; L->pitch = app.eyePitch; L->posture = c.posture; L->weapon = app.activeWeapon;
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
    app.fx.streak(g.p, e, g.tracer ? 0.025f : 0.007f, g.tracer ? 0.06f : 0.025f, g.tracer ? vec4(9.0f, 2.2f, 0.8f, 1) : vec4(0.7f, 0.68f, 0.62f, 1));
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
  hitMarker = std::max(0.0f, hitMarker - dt); hurtFlash = std::max(0.0f, hurtFlash - dt * 0.6f);
  if (isClient) { clientStep(dt); posesStep(dt); return; }
  auto c0 = std::chrono::steady_clock::now();
  struct Timer { Game& g; std::chrono::steady_clock::time_point c; ~Timer() { g.stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c).count(); ++g.stepCalls; } } timer{*this, c0};
  t += dt; ++steps;
  // local player mirror
  if (Soldier* L = local(); L && L->alive) {
    localGrenadeCounts(*L, false);   // the E3 items are the local inventory (spent at release)
    auto c = Physics::character();
    L->vel = (c.pos - L->feet) / std::max(dt, 1e-4f); L->feet = c.pos; L->yaw = app.eyeYaw; L->pitch = app.eyePitch; L->posture = c.posture;
    L->weapon = app.activeWeapon;
    L->speed = glm::length(vec2(L->vel.x, L->vel.z));
  }
  // occupancy sample at step start (capture precedence uses it)
  for (auto& s : sold) match.reportAlive(s.slot, s.alive, s.feet);
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
    crumb("game: ai"); if (!s.human) aiStep(s, dt);
  }
  crumb("game: rest"); stepRest(dt); crumb("game: done");
}

void Game::posesStep(float dt) {
  for (auto& s : sold) {
    if (!s.inUse || (!s.alive && t - s.diedAt > 2.0)) continue;
    const CharModel& m = *models[s.model];
    PoseParams pp;
    pp.walkAmount = std::min(1.0f, s.speed / 3.0f); s.walkPhase += s.speed * dt * 2.6f; pp.walkPhase = s.walkPhase;
    float wantCrouch = s.posture == Physics::CROUCH ? 1.0f : 0.0f;
    s.crouchBlend += (wantCrouch - s.crouchBlend) * std::min(1.0f, dt * 8);
    pp.crouch = s.crouchBlend; pp.prone = s.posture == Physics::PRONE ? 1.0f : 0.0f;
    pp.aimPitch = radians(s.localPlayer ? app.eyePitch : s.br.aimPitch);
    pp.aimYawOffset = s.localPlayer ? 0.0f : std::clamp(radians(wrapDeg(s.br.aimYaw - s.yaw + 180.0f) - 180.0f), -0.9f, 0.9f);
    if (s.weapon >= 0) { const WeaponDef& d = *app.weaponDefs[s.weapon]; pp.gripR = d.gripR; pp.gripL = d.gripL; pp.magPos = d.gripL + vec3(0, -0.1f, -0.12f); }
    pp.reload = s.reload > 0 && s.reloadTotal > 0 ? 1.0f - s.reload / s.reloadTotal : 0;
    pp.throwT = s.throwT; pp.recoil = s.recoil;
    pp.lowReady = (!s.localPlayer && s.br.target < 0 && s.speed > 2.5f) ? 1.0f : 0.0f;
    if (s.localPlayer) app.mantlePose(s.feet, pp);   // hands on the ledge while mantling (mantle.cpp)
    if (!s.alive) { pp.death = std::min(1.0f, (float)(t - s.diedAt) / 0.8f); pp.deathStyle = s.deathStyle; }
    poseCharacter(m, pp, s.pose);
    float yawUse = s.localPlayer ? app.eyeYaw : s.yaw;
    s.world = glm::translate(mat4(1), s.feet) * glm::mat4_cast(glm::angleAxis(radians(yawUse) + 3.14159265f, vec3(0, 1, 0)));
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
  crumb("game: draw");
  vec3 cam = app.eyePos(); vec3 fwd = App::forward(app.eyeYaw, app.eyePitch);
  static int frame = 0; ++frame;
  for (auto& s : sold) {
    if (!s.inUse || s.lifeId == 0) continue;
    if (!s.alive && t - s.diedAt > combat["corpses"].value("seconds", 40.0)) continue;
    if (s.localPlayer && s.alive) continue;                 // first person: the viewmodel is drawn instead
    vec3 c = s.feet + vec3(0, 0.9f, 0);
    float dist = glm::length(c - cam);
    if (dist > 300) continue;
    if (dist > 4 && glm::dot(glm::normalize(c - cam), fwd) < -0.2f) continue;   // behind the camera (shadows of close ones kept)
    if (!s.alive && t - s.diedAt > 2.0 && s.ciReady && s.ci.frameSkinned >= 0) {}   // settled corpse: keep the last skin
    else {
      if (!s.ciReady) { s.ci.create(*models[s.model]); s.ciReady = true; }
      bool far = dist > 70;
      if (!far || (frame + s.slot) % 3 == 0 || s.ci.frameSkinned < 0) { s.ci.skin(s.pose.skin); s.ci.frameSkinned = frame; }
    }
    for (int k = 0; k < (int)s.ci.view.parts.size(); ++k) app.R.worldModels.push_back({&s.ci.view, k, s.world, rulesTint(*this, s)});   // W1: protection shimmer
    if (s.alive && s.weapon >= 0) {
      const WeaponDef& d = *app.weaponDefs[s.weapon];
      if (d.useModel()) {
        mat4 W = s.world * s.pose.weapon;
        for (int p = 0; p < 8; ++p) for (int k : d.modelParts[p]) app.R.worldModels.push_back({d.model.get(), k, W});
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
    for (auto& s : sold) if (s.alive && s.role == L->role) { w = s.slot; break; }
    if (t - L->diedAt < 3.0 && L->killer >= 0 && sold[L->killer].alive) w = L->killer;
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
