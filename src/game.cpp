#include <cstring>
#include <map>
// Match game layer: lifecycle, spawning, soldier weapons / grenades, damage, presentation. AI is in ai.cpp.
#include "sd_game.h"
#include "map_ambience.h"   // BTA
#include "game.h"
#include "classes.h"
#include "abilities.h"
#include "gore_api.h"
#include "rules.h"
#include "damage_model.h"
#include "app.h"
#include "settings.h"
#include "overrides.h"
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
    if (aiOverrides().contains("_soldier") && aiOverrides()["_soldier"].is_object()) combat.merge_patch(aiOverrides()["_soldier"]);   // UI3 (h): the user's soldier patch (overrides.h)
  } catch (std::exception& e) { err = std::string("content/ai data: ") + e.what(); return false; }
  json gj = archetypes.value("grenade", json::object());
  return true;
}

bool Game::ensureModels(std::string& err) {
  if (!initSkinning(err)) { err = "skinning shader: " + err; return false; }
  if (!models.empty()) return true;
  json m = combat.value("models", json::object());
  for (auto& [fac, list] : m.items()) {   // WSM2: a body that does not load is skipped; a faction left with none takes models_fallback
    json use = json::array();
    for (int pass = 0; pass < 2 && use.empty(); ++pass)
      for (auto& id : pass ? combat.value("models_fallback", json::object()).value(fac, json::array()) : list) {
        std::string s = id.get<std::string>();
        if (!modelIdx.count(s)) {
          auto cm = std::make_unique<CharModel>(); std::string e;
          if (!cm->load(contentDir() / "characters" / (s + ".glb"), e)) { logf("character %s: %s (skipped)", s.c_str(), e.c_str()); continue; }
          modelIdx[s] = (int)models.size(); models.push_back(std::move(cm));
        }
        use.push_back(s);
      }
    if (use.empty()) { err = "no character body loads for " + fac; return false; }
    combat["models"][fac] = use;
  }
  return !models.empty();
}

vec3 Game::soldierFeet(int s) const { return sold[s].feet; }
int Game::fragDef(Faction f) const { return app.weaponIndex(f == Faction::Axis ? "m24_grenade" : "mk2_grenade"); }
int Game::smokeDef() const { return app.weaponIndex("smoke_grenade"); }

// SND10: a bot's footstep, from its own (replicated) movement: the player's surface sets, louder and farther when
// running, quiet crouched, none prone; positional on the effects bus, softened through walls; only bots within range
// sound and at most maxPerSecond steps start (200 bots stay cheap). content/audio/custom/mix.json "botSteps"
int gBotSteps = 0;   // steps played (headless check)
static void botFootstep(App& app, const Soldier& s, float v) {
  static const json cfg = [] { try { return json::parse(readTextFile(contentDir() / "audio" / "custom" / "mix.json")).value("botSteps", json::object()); } catch (...) { return json::object(); } }();
  static const float runR = cfg.value("runRange_m", 20.0f), walkR = cfg.value("walkRange_m", 8.0f), crouchR = cfg.value("crouchRange_m", 5.0f);
  static const float runV = cfg.value("runVolume", 0.85f), walkV = cfg.value("walkVolume", 0.55f), crouchV = cfg.value("crouchVolume", 0.3f);
  static const float wallV = cfg.value("throughWallVolume", 0.35f), runSpeed = cfg.value("runSpeed_mps", 3.0f);
  static const int maxPerSec = cfg.value("maxPerSecond", 24);
  if (s.posture == Physics::PRONE || s.swim || v < 0.3f) return;
  const bool crouch = s.posture == Physics::CROUCH, run = !crouch && v >= runSpeed;
  float range = crouch ? crouchR : run ? runR : walkR, vol = crouch ? crouchV : run ? runV : walkV;
  vec3 ear = app.audio.listenerPos(), foot = s.feet + vec3(0, 0.05f, 0);
  float d = glm::length(foot - ear);
  if (d > range) return;
  static double bucket = 0, last = 0; double now = glfwGetTime();   // voice limit: a token bucket of maxPerSecond
  bucket = std::min((double)maxPerSec, bucket + (now - last) * maxPerSec); last = now;
  if (bucket < 1) return;
  Physics::Hit h;
  if (d > 1.0f && Physics::castRayEx(ear, glm::normalize(foot + vec3(0, 0.9f, 0) - ear), d, h) && glm::length(h.pos - ear) < d - 0.6f) {   // a wall between: muffled, shorter reach
    if (d > range * 0.6f) return;
    vol *= wallV;
  }
  bucket -= 1;
  std::string ev = s.kit == 3 && app.audio.hasSound("step_metal") ? "step_metal" : app.stepSound(s.feet, 0);   // the robot commando: a metallic step
  app.audio.play(ev, foot, vol, s.kit == 3 && ev != "step_metal" ? 0.8f : 0.9f + 0.2f * (float)((s.slot * 7 + (int)(now * 13)) % 10) / 10.0f);
  mapStepFeedback(app, ev, foot);   // BTA: splashes at the contact
  ++gBotSteps;
}

// the gait cycle (cycles per second: the planted foot travels back at the body speed); a bot's foot lands every half cycle
float gaitCadence(float v) { return 0.9f * std::min(1.75f, 0.75f + 0.25f * v); }   // AN2: 10 % fewer steps (longer strides) than before, so legs do not look hurried
float advanceStride(App& app, Soldier& s, float v, float dt, float cadIn) {
  float cad = cadIn > 0 ? cadIn : gaitCadence(v);
  const float ph0 = s.walkPhase;
  s.walkPhase += v * dt > 0 ? 6.2831853f * cad * dt : 0.0f;
  if (v * dt <= 0 && s.anStride > 0.01f && !s.zombie) {   // AN1 step 3b: stopping mid-stride finishes the step: on until both feet are down
    float u = s.walkPhase / 6.2831853f; u -= std::floor(u);
    if (!(u < 0.12f || (u >= 0.5f && u < 0.62f))) s.walkPhase += 6.2831853f * 1.0f * dt;
  }
  if (!s.localPlayer && s.alive && std::floor(s.walkPhase / 3.14159265f) != std::floor(ph0 / 3.14159265f)) botFootstep(app, s, v);   // SND10
  return cad;
}

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
static const char* kitName(int kit) { return kit == 2 ? "trooper" : kit == 3 ? "robot" : kit == 6 ? "desert" : kit == 7 ? "allied" : kit == 8 ? "ss" : kit == 9 ? "gasmask" : kit == 10 ? "gasmask2" : kit == 12 ? "kr714" : "delta"; }   // RB1: 12 the KR-714 robot (AI type Mechs, perf mechrobot)
// WSM2: a soldier skin on the Allied paratrooper's skeleton (combat.json hold_bodies: skin -> the model whose clips it plays): the
// clips are borrowed from that model by joint name (one clip file for every soldier). False: not a hold body, or no clips to play.
static int loadCharModel(Game& G, const std::string& id) {
  if (G.modelIdx.count(id)) return G.modelIdx[id];
  static std::set<std::string> failed; if (failed.count(id)) return -1;
  auto cm = std::make_unique<CharModel>(); std::string e;
  if (!cm->load(contentDir() / "characters" / (id + ".glb"), e)) { failed.insert(id); logf("hold bodies: model %s: %s", id.c_str(), e.c_str()); return -1; }
  G.modelIdx[id] = (int)G.models.size(); G.models.push_back(std::move(cm)); return G.modelIdx[id];
}
bool holdBodySetup(Game& G, int model) {
  if (model < 0 || model >= (int)G.models.size()) return false;
  const json hb = G.combat.value("hold_bodies", json::object());
  CharModel& m = *G.models[model];
  if (!hb.contains(m.id) || !hb[m.id].is_string()) return false;
  if (m.clips.empty()) {
    const int si = loadCharModel(G, hb[m.id].get<std::string>());
    if (si < 0 || si == model) return false;
    const CharModel& src = *G.models[si];   // (the load above may have moved the vector: m is re-read below)
    CharModel& d = *G.models[model];
    std::map<std::string, int> at; for (int k = 0; k < (int)d.nodeName.size(); ++k) at.emplace(d.nodeName[k], k);
    for (const AnimClip& c : src.clips) {
      AnimClip n = c; n.ch.clear();
      for (const auto& ch : c.ch) if (ch.node >= 0 && ch.node < (int)src.nodeName.size()) if (auto it = at.find(src.nodeName[ch.node]); it != at.end()) { auto x = ch; x.node = it->second; n.ch.push_back(std::move(x)); }
      d.clips.push_back(std::move(n));
    }
    analyzeClips(d);
    logf("hold bodies: %s plays %zu clips of %s", d.id.c_str(), d.clips.size(), src.id.c_str());
  }
  return !G.models[model]->clips.empty();
}
static bool clipKit(int kit) { return kit == 4 || kit == 6 || kit == 7 || kit == 8 || kit == 9 || kit == 10 || kit == 12; }   // WS1: the Delta Force path (a clip body, the game's guns in its hands): 4 Delta Force, 6 desert, 7 Allied, 8 SS   // combat.json kits.<name> (SW1; DF1 4: Delta Force)
static float bodyScale(const Soldier& s) { return s.eBody; }   // EN1: a troll stands taller, a spider lower; RB6: a soldier kit's "scale" (1 otherwise)
void Game::renderView(vec3& e, vec3& f) const {
  if (app.mode == Mode::Creative) { e = app.camPos; f = App::forward(app.camYaw, app.camPitch); return; }   // EGG1: the sandbox's AI posed / culled against the editor camera
  e = app.eyePos(); f = App::forward(app.eyeYaw, app.eyePitch); float fov = 70;
  if (specCam) { e = specEye; f = specFwd; return; }   // dead: the spectate orbit renders the frame (the body watched was culled as "behind" the dead eye)
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

int Game::soldierOfOwner(int owner) const { return owner >= 0 ? owner : owner == -1 ? localSlot : -1; }   // MT2: -2 = no soldier (an AI vehicle's round: nobody skipped, no kill credit)

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
  for (auto& s : sold) if (s.ciReady) { s.ci.destroy(); s.ciReady = false; }   // V0c: a match begun over one not ended frees the old skins first
  sold.clear(); sold.resize(match.slots.size());
  int aiGiven[2][kAiTypeCount] = {}, kindSeen[kAiTypeCount] = {}, germanSeen = 0;   // per side / per AI type: types and model variants go round in turn (same on LAN clients)
  if (const char* at = std::getenv("NMK_AI_TYPES")) {   // SW1 dev: NMK_AI_TYPES=<side 0 type>,<side 1 type> (AiType numbers): every AI of that side that type
    int t0 = -1, t1 = -1; std::sscanf(at, "%d,%d", &t0, &t1); int ts[2] = {t0, t1};
    for (int sd = 0; sd < 2; ++sd) if (ts[sd] >= 0 && ts[sd] < kAiTypeCount) { match.rules.aiTypes[sd] = aiBit((AiType)ts[sd]); for (int k = 0; k < kAiTypeCount; ++k) match.rules.aiCount[sd][k] = 0; }
  }
  int mechN[2] = {0, 0};   // MT1: Mech Tanks are vehicles, not roster soldiers: out of the slot mix, placed below (tank_mech.cpp)
  for (int sd = 0; sd < 2; ++sd) if (match.rules.aiTypes[sd] & aiBit(AiType::MechTank)) {
    mechN[sd] = std::max(1, match.rules.aiCount[sd][(int)AiType::MechTank]);
    match.rules.aiTypes[sd] &= ~aiBit(AiType::MechTank); match.rules.aiCount[sd][(int)AiType::MechTank] = 0;
  }
  for (auto& rs : match.slots) {
    int side = ffaMode(match.mode()) ? 0 : std::clamp(rs.team, 0, 1);
    AiType ait = rs.human ? AiType::Soldiers : aiTypeNext(match.rules.aiCount[side], match.rules.aiTypes[side], aiGiven[side]);   // AI3: the typed counts per type
    setupSlotSoldier(rs, ait, kindSeen[(int)ait]++, germanSeen);
  }
  pool.clear(); poolBuilt = false;
  sounds.clear(); gren.clear(); grenadeReserve.clear(); smokeReserve.clear(); defSpots.clear(); defSpotsStage = -1;
  t = 0; steps = 0; spectate = -1; specCam = false; hitMarker = killMarker = hurtFlash = 0;
  shotsByBots = hitsOnSoldiers = kills = doorOpens = smokeThrows = fragThrows = navRebuilds = 0;
  if (!isClient) match.start(); else { match.phase = MatchPhase::Running; for (auto& o : match.obj) o.state = ObjState::Locked; if (!match.obj.empty()) match.obj[0].state = ObjState::Active; }
  ghosts.clear(); doorSeen.clear();
  active = true;
  app.refreshMatchOnly();   // BT2: play-limit blockers on (explore-only routes closed during a match)
  deployOpenedAt = t; deployReady = app.selftest; deploySpawn = -1;   // GM1: the first spawn goes through the deploy screen too
  // the local player waits in limbo until its first spawn
  if (Soldier* L = local()) { vec3 p = app.exploreStartPos; Physics::setCharacterPos(vec3(p.x, p.y - kLimboDepth, p.z)); (void)L; }
  setupError.clear();
  lootBegin(*this);   // BR2: floor loot in the buildings
  sdBegin(*this);     // SD1: the round controller (Search and Destroy only)
  if (!isClient) { extern void mechSpawnForMatch(App&, Game&, const int*); mechSpawnForMatch(app, *this, mechN); }   // MT1
  logf("match: %zu slots, local slot %d, nav %d floors in %.0f ms", sold.size(), localSlot, nav.usedNodes, nav.buildMs);
  return true;
}

// EGG1: one roster slot's soldier from its AI type (Game::begin per slot; the Creative spawn eggs per egg). variant: the
// model / kit variant in turn; etypeOverride >= 0: this enemy type for a melee type (an egg names it), else the type's own
void Game::setupSlotSoldier(const RosterSlot& rs, AiType ait, int variant, int& germanSeen, int etypeOverride) {
    auto& arr = archetypes["archetypes"];
    Soldier& s = sold[rs.id];
    s.slot = rs.id; s.inUse = true; s.alive = false; s.faction = rs.faction; s.role = rs.role; s.team = rs.team;
    s.human = rs.human; s.peer = rs.peer; s.localPlayer = rs.id == localSlot;
    s.zombie = !soldierBrain(ait);   // G2 melee brain (NA1: mechs are melee); the list: Game::soldierBrain (eggs.cpp)
    s.gmWeapon = -1;
    // GM1 / GM2: bodies with the gun built in: the gas-mask and desert types, and the Axis side's plain soldier bots (not in Battle
    // royale, where bots loot and change guns; never a human: the player's own body and first person are unchanged)
    // WSM2: no AI type uses the built-in-gun bodies any more (gunbody_models is empty): every human soldier type wears a skin on the
    // Allied paratrooper's skeleton (combat.json hold_bodies) with the game's guns in its stored per-gun holds
    const char* gunBodyKind = nullptr;
    s.gunBody = gunBodyKind != nullptr;
    s.op = !s.zombie && (ait == AiType::Operators || ait == AiType::OperatorF);   // OPF1: the Giant Operator is an Operator in everything but its body and size
    s.kit = ait == AiType::Mechs && !s.zombie ? 12 : ait == AiType::Troopers ? 2 : ait == AiType::Robots ? 3 : ait == AiType::Delta ? 4 : ait == AiType::Desert ? 6 : ait == AiType::AlliedPara || ait == AiType::Allied ? 7 : ait == AiType::SS ? 8 : s.op ? 1 : 0;   // RM2: the gas-mask kits (9 / 10) are gone   // WS1: 7 Allied paratroopers; WSM2: 7 also the Allied soldiers, 8 SS, 9 / 10 gas-mask 1 / 2   // SW1; DF1: 4 Delta Force; DSD1: 6 desert soldiers on the Delta Force path
    if (!s.zombie) {   // RB6: the kit's size and belt-gun stance (combat.json kits.<kit> "scale", "lmg_standing"); a zombie's come from its enemy type
      const json kj = combat.value("kits", json::object()).value(kitName(s.kit), json::object());
      s.eScale = s.eBody = std::clamp(kj.value("scale", 1.0f), 0.5f, 3.0f); s.eLean = 0; s.standFire = kj.value("lmg_standing", false); s.meleeKeep = std::clamp(kj.value("melee_keep_m", 0.0f), 0.0f, 40.0f);
      if (ait == AiType::OperatorF) s.eScale = s.eBody = std::clamp(combat.value("operator_f", json::object()).value("scale", 1.5f), 0.5f, 3.0f);   // OPF1: its own size (what a kit's "scale" drives)
    } else { s.standFire = false; s.meleeKeep = 0; }
    s.etypeMount = -1;   // EK4: a new match's slot: its own type, no mount to restore
    s.etype = !s.zombie ? 0 : etypeOverride >= 0 ? etypeOverride : ait == AiType::ZombiesHeavies ? enemyTypePick((uint32_t)rs.id)   // EN1: the mix by weight, per slot
            : ait == AiType::Brutes ? std::max(0, enemyTypeIndex("brute")) : ait == AiType::Spiders ? std::max(0, enemyTypeIndex("stalker"))
            : ait == AiType::Creatures ? std::max(0, enemyTypeIndex("creature"))
            : ait == AiType::Knights ? std::max(0, enemyTypeIndex("knight"))
            : ait == AiType::Knights2 ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "knight_v2.glb") ? "knight2" : "knight"))   // KN2: its model is local-only: Knight 1 without it
            : ait == AiType::Knights3 ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "knight_3.glb") ? "knight3" : "knight"))   // KN3: likewise (the flail is CC-BY-NC-ND)
            : ait == AiType::Knights4 ? std::max(0, enemyTypeIndex("knight4")) : ait == AiType::Knights5 ? std::max(0, enemyTypeIndex("knight5")) : ait == AiType::Knights6 ? std::max(0, enemyTypeIndex("knight6")) : ait == AiType::Knights7 ? std::max(0, enemyTypeIndex("knight7")) : ait == AiType::Knights8 ? std::max(0, enemyTypeIndex("knight8")) : ait == AiType::Knights9 ? std::max(0, enemyTypeIndex("knight9")) : ait == AiType::Archers1 ? std::max(0, enemyTypeIndex("archer1")) : ait == AiType::Knights10 ? std::max(0, enemyTypeIndex("knight10"))   // KN4 / KN5; AR1; K10
            : ait == AiType::KnightsMounted ? std::max(0, enemyTypeIndex(gHorseKnight && fs::exists(contentDir() / "characters" / "knight_10_mounted.glb") ? "knight10m" : "knight10"))   // HK1
            : ait == AiType::KnightsAssorted ? enemyTypeAssortedPick((uint32_t)rs.id)   // AK1: a knight per slot (AK2: no archers)
            : ait == AiType::FallenKnight2 ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "fallen_knight_2.glb") ? "fallen_knight2" : "fallen_knight1"))   // FK2: Knight 1 at the Fallen Knight's size were its model missing
            : ait == AiType::FallenKnight ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "knight.glb") ? "fallen_knight" : "fallen_knight1"))   // FK1: the Brother Knight is local-only: Knight 1 at its size without it
            : ait == AiType::OperatorF ? std::max(0, enemyTypeIndex("operator_f_idle"))   // BC1: the Giant Operator on the melee brain only with its local idle model (Game::soldierBrain)
            : ait == AiType::ElkKnight ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "knight_10_elk.glb") && fs::exists(contentDir() / "characters" / "knight_10_elk_foot.glb") ? "knight10e" : "knight10m"))   // EK: the Mounted Knight without its two models
            : ait == AiType::Boss ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "boss_choi.glb") ? "boss_choi" : "brute"))   // BC1: the boss is local-only: the Brute without it
            : ait == AiType::Dragon ? std::max(0, enemyTypeIndex("dragon"))   // DR1: the Mountain Dragon, ground only
            : ait == AiType::Civilians ? enemyTypeNpcPick((uint32_t)rs.id)   // NPC1: one of the civilian types per slot
            : ait == AiType::SpearKnight ? std::max(0, enemyTypeIndex("knight_spear"))   // KS: the Spear Knight (its build picked per spawn from "models")
            : ait == AiType::Archers2 ? std::max(0, enemyTypeIndex(fs::exists(contentDir() / "characters" / "archer_2.glb") ? "archer2" : "archer1"))   // AR2: its model is local-only (the tunic is CC-BY-NC-ND): Archer 1 without it
            : ait == AiType::Abominations ? std::max(0, enemyTypeIndex("abomination")) : ait == AiType::Golems ? std::max(0, enemyTypeIndex("golem"))
            : ait == AiType::Boglords ? std::max(0, enemyTypeIndex("boglord")) : ait == AiType::Mechs ? std::max(0, enemyTypeIndex("mech")) : 0;
    s.evariant = variant;
    s.npc = s.zombie ? (int8_t)enemyType(s.etype).npc : 0; s.npcFoe = -1; s.npcLife = -1; s.npcCalmAt = 0;   // NPC1
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
      // GP1: a body that is not a soldier rig (creatures, the alien...) is posed by the soldier pose code with its own
      // skeleton, so its joints (and the hit zones built from them) end up where no bullet goes: the player was unhittable.
      // Such a body keeps its look; its hits use the standard soldier volumes (hitTest)
      static const char* kSoldierBodies[] = {"allied_", "axis_", "operator_", "trooper_", "br_marine", "robot_commando"};
      bool soldierRig = false; for (const char* p : kSoldierBodies) soldierRig |= id.rfind(p, 0) == 0;
      s.genericHits = modelIdx.count(id) && !soldierRig;
      if (s.genericHits) logf("player body %s: not a soldier rig, hits use the standard soldier volumes", id.c_str());
    }
    const json kitj = s.kit >= 2 ? combat.value("kits", json::object()).value(kitName(s.kit), json::object()) : json::object();   // SW1
    if (s.kit >= 2) { s.kitSpeed = kitj.value("speed", 1.0f); b.aimTurn *= kitj.value("aimTurn", 1.0f); }
    s.kitSparks = s.zombie ? enemyType(s.etype).hitSparks : kitj.value("sparks", false);   // SW1 kit "sparks" / MK2 type "hit_fx": "sparks": hits throw metal sparks, no blood
    if (s.op || s.kit >= 2) {   // a soldier in the faction's operator body (combat.json operator_models; SW1: the kit's models, either side; missing: the soldier body stays)
      json ol = s.kit >= 2 ? kitj.value("models", json::array()) : ait == AiType::OperatorF ? combat.value("operator_f", json::object()).value("models", json::array()) : combat.value("operator_models", json::object()).value(fac, json::array());
      if (s.kit < 2) {   // OPF1: an operator body whose file is absent (a local-only one) is left out: the Giant Operator then wears the next one in its list (the operator body)
        json have = json::array(); for (auto& x : ol) if (fs::exists(contentDir() / "characters" / (x.get<std::string>() + ".glb"))) have.push_back(x);
        if (!have.empty()) ol = have;
      }
      if (!ol.empty()) {
        std::string id = ol[ait == AiType::OperatorF ? 0 : (size_t)s.evariant % ol.size()].get<std::string>();   // OPF1: its list is a preference order, not a turn
        static std::set<std::string> failed;
        if (!modelIdx.count(id) && !failed.count(id)) {
          auto cm = std::make_unique<CharModel>(); std::string e;
          if (cm->load(contentDir() / "characters" / (id + ".glb"), e)) { modelIdx[id] = (int)models.size(); models.push_back(std::move(cm)); }
          else { failed.insert(id); logf("operators: model %s: %s (soldier body kept)", id.c_str(), e.c_str()); }
        }
        if (modelIdx.count(id)) s.model = modelIdx[id];
      }
    }
    if (s.gunBody) {   // a body per weapon, the gun in its mesh (combat.json gunbody_models.<kind>: [model, weapon], in turn). Missing: the soldier body it had
      const json gl = combat.value("gunbody_models", json::object()).value(gunBodyKind, json::array());
      if (!gl.empty()) {
        const json& e = gl[(size_t)(std::string(gunBodyKind) == "german" ? germanSeen++ : s.evariant) % gl.size()];
        std::string id = e[0].get<std::string>();
        static std::set<std::string> failed;
        if (!modelIdx.count(id) && !failed.count(id)) {
          auto cm = std::make_unique<CharModel>(); std::string err;
          if (cm->load(contentDir() / "characters" / (id + ".glb"), err) && !cm->clips.empty()) { modelIdx[id] = (int)models.size(); models.push_back(std::move(cm)); }
          else { failed.insert(id); logf("%s bodies: model %s: %s (the soldier body instead)", gunBodyKind, id.c_str(), err.empty() ? "no clips" : err.c_str()); }
        }
        s.gmWeapon = app.weaponIndex(e[1].get<std::string>());
        if (s.gmWeapon < 0) logf("%s bodies: weapon %s missing (the soldier body instead)", gunBodyKind, e[1].get<std::string>().c_str());
        if (modelIdx.count(id) && s.gmWeapon >= 0) s.model = modelIdx[id]; else s.gmWeapon = -1;
      }
      if (s.gmWeapon < 0) s.gunBody = false;
    }
    s.holdBody = !rs.human && !s.zombie && holdBodySetup(*this, s.model);   // WSM2
}

void Game::end() {
  sdEnd();   // SD1
  gAdmin = AdminMode{};   // CMD2: /admin ends with the match
  aiZombieLoopsStop(app);   // SND2: no creature breathing on after the match
  aiMatchEnd(*this);   // W6
  for (auto& s : sold) { if (s.agent >= 0) Physics::destroyAgent(s.agent); s.agent = -1; if (s.ciReady) s.ci.destroy(); s.ciReady = false; }
  for (auto& h : horseCorpses) h->ci.destroy(); horseCorpses.clear();   // HK2
  Physics::destroyAllAgents();
  sold.clear(); gren.clear(); sounds.clear();
  active = false; isClient = false;
  app.refreshMatchOnly();   // BT2: and off again
  match.phase = MatchPhase::Setup;
}

// ---------------------------------------------------------------- spawning
void Game::chooseClass(Soldier& s) {
  const auto& C = classDefs();
  if (C.empty() || s.zombie || s.kit >= 2 || s.gunBody) { s.cls = -1; return; }   // GM1: the gas-mask soldier's gun is its body's   // SW1: kit soldiers carry only their blasters (no class kit, no explosion pack)
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
  if (s.gunBody && s.gmWeapon >= 0) {   // GM1: only the weapon its body holds (no sidearm; grenades only for the Axis bots, as before: thrown without a clip)
    const int w = s.gmWeapon; const WeaponDef& d = *app.weaponDefs[w];
    s.wsBy.clear(); s.weapon = w; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
    s.ws.seed = s.rng | 1; s.carried = {w};
    if (newLife) {   // GM2: the German bots keep their grenades (gas-mask / desert soldiers carry none)
      const std::string& mid = models[s.model]->id;
      const bool axis = mid.rfind("german_", 0) == 0 || mid.rfind("ss_", 0) == 0 || mid.rfind("gasmask2_", 0) == 0 || mid.rfind("allied_", 0) == 0 && mid.rfind("allied_ranger", 0) != 0;   // NA1: the SS, gas-mask 2 and Allied bodies have throw clips
      s.frags = axis ? match.rules.fragPerLife : 0; s.smokes = axis ? (s.role == CombatRole::Attacker ? match.rules.smokePerLifeAttacker : match.rules.smokePerLifeDefender) : 0;
    }
    s.reload = 0; s.throwT = -1; s.throwReleased = false;
    return;
  }
  if (s.kit >= 2 && !s.human && !s.localPlayer) {
    const json& kj = combat.value("kits", json::object()).value(kitName(s.kit), json::object());
    std::vector<std::pair<int, int>> pool; int tot = 0;
    const bool needModel = kj.contains("ai_fallback");   // RB6: a kit with a fallback takes its guns only with their models (the KR-714's M249 -> the BAR)
    for (const char* key : {"ai", "ai_fallback"}) {
      if (!pool.empty()) break;
      for (auto& e : kj.value(key, json::array())) { int w = app.weaponIndex(e[0].get<std::string>()); if (w >= 0 && (!needModel || app.weaponDefs[w]->model)) { pool.push_back({w, e[1].get<int>()}); tot += e[1].get<int>(); } }
    }
    if (!pool.empty()) {
      int w = pool[0].first, pick = tot > 0 ? (int)((s.slot * 7 + s.lifeId * 3) % tot) : 0;
      for (auto& [i, n] : pool) { if (pick < n) { w = i; break; } pick -= n; }
      s.wsBy.clear(); s.weapon = w;
      const WeaponDef& d = *app.weaponDefs[w]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; s.ws.reserveMax = d.loaded + d.reserve;
      for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize));
      s.ws.seed = s.rng | 1;
      s.carried = {w};
      if (const json sa = kj.value("sidearms", json::array()); !sa.empty()) {   // DF1: a sidearm (in turn per slot), drawn when the primary runs dry
        int p = app.weaponIndex(sa[(size_t)(s.slot + s.lifeId) % sa.size()][0].get<std::string>()); if (p >= 0 && p != w) s.carried.push_back(p);
      }
      if (const json la = kj.value("launchers", json::array()); !la.empty() && (s.slot + s.lifeId) % std::max(1, kj.value("launcherEvery", 4)) == 0) {   // DF1: one in launcherEvery also carries a launcher (in turn)
        int l = app.weaponIndex(la[(size_t)(s.slot / std::max(1, kj.value("launcherEvery", 4))) % la.size()][0].get<std::string>()); if (l >= 0 && l != w) s.carried.push_back(l);
      }
      if (newLife) { s.frags = kj.value("frags", 0); s.smokes = kj.value("smokes", 0); s.hp *= kj.value("hp", 1.0f); }
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

// EGG1: a soldier placed alive at feet facing yaw (trySpawn's success; the Creative spawn eggs): agent, class, loadout, a fresh brain
void Game::placeSoldier(int slot, vec3 feet, float yaw) {
  Soldier& s = sold[slot];
  if (s.zombie && s.etypeMount >= 0) { s.etype = s.etypeMount; aiZombieAssign(*this, s); if (s.ciReady) s.ci.destroy(); s.ciReady = false; }   // EK4: a knight whose mount fell respawns mounted (the full horse_hp below)
  s.etypeMount = -1;
  s.feet = feet; s.yaw = yaw; s.pitch = 0; s.alive = true; s.flat = false; s.hp = s.zombie ? enemyType(s.etype).hp : combat.value("health", 100.0f); s.horseHp = s.zombie ? enemyType(s.etype).horseHp : 0.0f; s.mountDeath = -1; s.hitHorse = false; s.killer = -1; s.staggerUntil = 0; s.posture = Physics::STAND;
  s.human = match.slots[slot].human || match.slots[slot].pendingPeer >= 0; s.peer = match.slots[slot].pendingPeer >= 0 ? match.slots[slot].pendingPeer : match.slots[slot].peer;
  ++s.lifeId;
  aiForget(*this, slot);   // a fresh life: no stale last-known position of it anywhere
  rulesOnSpawn(*this, s);   // W1: spawn protection starts (SPEC 2.2)
  if (s.localPlayer) {
    Physics::setCharacterPos(feet); app.eyeYaw = yaw; app.eyePitch = 0; app.exploreStartPos = feet; app.R.resetTemporal();
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
  s.br.lane = match.slots[slot].lane; s.br.aimYaw = yaw; s.br.grenadeCD = 3.0f + rnd(s) * 3;
  s.br.nextPerceive = (float)t + rnd(s) * 0.2f; s.br.nextTactic = (float)t + rnd(s);
  match.onSpawned(slot);
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
        placeSoldier(slot, feet, m.yaw);
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
  s.deathRunning = s.speed > 3.0f;   // before the corpse stops: the death pick reads it
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
  if (killer == localSlot && killer != i && hostile(sold[killer], s)) killMarker = 0.6f;   // red marker: enemy down
  if (!rosterAlive(s)) {   // HK5: not for a horse going down (the roster queued a respawn 3 s into the 3.2 s horse death: the dying pair jumped to a spawn point)
  match.onDeath(i, s.npc ? -1 : killer);   // NPC8: a civilian's death is no one's kill: no kill, score or team-kill penalty (AI now hunt them: the kill cap would fill with bystanders)
  if (match.mode() == GameMode::SearchDestroy) sdOnKill(*this, i, killer, s.lastHow);
  }   // SD1: rewards, the dropped bomb
  if (killer >= 0 && killer != i) ++kills;
  for (auto& o : sold) if (o.br.target == i) o.br.target = -1;
  for (auto& d : defSpots) if (d.heldBy == i) d.heldBy = -1;
}

// ---------------------------------------------------------------- damage
bool Game::coarseHit(const Soldier& s, vec3 a, vec3 d) const {   // CP: hitTest's bounding sphere alone
  float bs = bodyScale(s), zs = s.zombie ? bs * (s.eFallback ? 1.0f : enemyType(s.etype).zoneScale) : 1.0f;
  vec3 m = a - (s.feet + vec3(0, 0.9f * bs, 0)); float b = glm::dot(m, d), R0 = 1.3f * std::max(bs, zs), cc = glm::dot(m, m) - R0 * R0;
  return !(cc > 0 && b > 0) && b * b - cc >= 0;
}
bool Game::hitTest(const Soldier& s, vec3 a, vec3 d, float maxT, HitZone& hz) const {
  // coarse sphere around the body
  float bs = bodyScale(s), zs = s.zombie ? bs * (s.eFallback ? 1.0f : enemyType(s.etype).zoneScale) : bs;   // EN1: the type's size and hit-zone scale (RB6: a scaled soldier's limbs too)
  vec3 c = s.feet + vec3(0, 0.9f * bs, 0);
  vec3 m = a - c; float b = glm::dot(m, d), R0 = 1.3f * std::max(bs, zs), cc = glm::dot(m, m) - R0 * R0;
  if (cc > 0 && b > 0) return false;
  float disc = b * b - cc; if (disc < 0) return false;
  // posed parts (model space -> world)
  const vec3* J = s.pose.joint;
  auto W = [&](int j) { return vec3(s.world * vec4(J[j], 1)); };
  struct Cap { vec3 p, q; float r; int zone; };
  std::vector<Cap> caps;
  if (s.genericHits) {   // GP1: the standard soldier volumes from the feet, posture and facing (a non-soldier body: its joints are not a soldier's)
    const vec3 f(-std::sin(radians(s.yaw)), 0, -std::cos(radians(s.yaw))), rt(std::cos(radians(s.yaw)), 0, -std::sin(radians(s.yaw))), F = s.feet;
    if (s.posture == Physics::PRONE) {
      caps = {{F + f * 0.72f + vec3(0, 0.24f, 0), F + f * 0.86f + vec3(0, 0.26f, 0), 0.11f, 0}, {F + f * 0.05f + vec3(0, 0.2f, 0), F + f * 0.6f + vec3(0, 0.22f, 0), 0.17f, 1},
              {F + rt * 0.12f + vec3(0, 0.12f, 0), F - f * 0.9f + rt * 0.14f + vec3(0, 0.1f, 0), 0.085f, 2}, {F - rt * 0.12f + vec3(0, 0.12f, 0), F - f * 0.9f - rt * 0.14f + vec3(0, 0.1f, 0), 0.085f, 2}};
    } else {
      const float H = s.posture == Physics::CROUCH ? 1.2f : 1.75f;
      caps = {{F + vec3(0, H - 0.2f, 0), F + vec3(0, H - 0.04f, 0), 0.11f, 0}, {F + vec3(0, H * 0.52f, 0), F + vec3(0, H - 0.3f, 0), 0.17f, 1},
              {F + rt * 0.24f + vec3(0, H - 0.34f, 0), F + rt * 0.26f + f * 0.2f + vec3(0, H * 0.62f, 0), 0.06f, 2}, {F - rt * 0.24f + vec3(0, H - 0.34f, 0), F - rt * 0.26f + f * 0.2f + vec3(0, H * 0.62f, 0), 0.06f, 2},
              {F + rt * 0.11f + vec3(0, 0.05f, 0), F + rt * 0.1f + vec3(0, H * 0.5f, 0), 0.085f, 2}, {F - rt * 0.11f + vec3(0, 0.05f, 0), F - rt * 0.1f + vec3(0, H * 0.5f, 0), 0.085f, 2}};
    }
  } else {
    vec3 head = W(CJ_HEAD), neck = W(CJ_NECK);
    vec3 headTop = head + glm::normalize(head - neck + vec3(1e-4f)) * 0.12f * bs;
    caps = {
      {head, headTop, 0.11f, 0},
      {W(CJ_HIPS), W(CJ_NECK), 0.17f, 1},
      {W(CJ_LARM), W(CJ_LFOREARM), 0.06f, 2}, {W(CJ_LFOREARM), W(CJ_LHAND), 0.055f, 2},
      {W(CJ_RARM), W(CJ_RFOREARM), 0.06f, 2}, {W(CJ_RFOREARM), W(CJ_RHAND), 0.055f, 2},
      {W(CJ_LUPLEG), W(CJ_LLEG), 0.085f, 2}, {W(CJ_LLEG), W(CJ_LFOOT), 0.07f, 2},
      {W(CJ_RUPLEG), W(CJ_RLEG), 0.085f, 2}, {W(CJ_RLEG), W(CJ_RFOOT), 0.07f, 2},
    };
  }
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
    if (s.poseAcc > 0 && coarseHit(s, a, d)) poseNow(s);   // CP: a held pose is brought up to date first
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
    if (s.poseAcc > 0 && coarseHit(s, a, d)) poseNow(s);   // CP: a held (rate-LOD) pose is brought up to date before its zones are tested
    if (hitTest(s, a, d, bt, hz) && hz.t < bt) { bt = hz.t; best = (int)i; bz = hz; s.hitHorse = false; }
    if (s.zombie) { int zn = 1; float th = aiHorseHit(*this, s, a, d, bt, &zn); if (th >= 0 && th < bt) { bt = th; best = (int)i; bz = HitZone{s.horseHp > 0 ? 1 : zn, th}; s.hitHorse = s.horseHp > 0; } }   // HK3: the horse met first; DR1: a type's own capsules (the dragon) on its one health pool
  }
  if (best >= 0) tHit = bt;
  return dmBulletHit(*this, p, best, bz, a, d, tHit, shooter);   // W1 damage model (damage_model.cpp): table, zones, helmet, protection, FleshHit
}

int gPlayerDmgBlock[8] = {};   // GP1 diagnostics: hits on the local player dropped by: 0 protection, 1 admin god, 2 tank melee, 3 tank crew, 4 friendly fire, 5 SD freeze, 6 SD armor, 7 landed
void Game::damage(int i, float dmg, int attacker, vec3 dir, const char* how) {
  Soldier& s = sold[i];
  if (!s.alive || dmg <= 0) return;
  auto drop = [&](int k) { if (s.localPlayer) ++gPlayerDmgBlock[k]; };
  if (rulesIgnoreDamage(*this, i)) { drop(0); return; }
  if (gAdmin.god && s.localPlayer) { drop(1); return; }   // CMD2 /admin: nothing hurts you (bullets, blasts, melee, flame, falls, drowning, storm)
  // TK1: melee on the crew of an armoured tank lands on the tank: a heavy's blow wears it down (1/15 each), the rest do
  // nothing; the crew is hurt only once the tank is destroyed
  if (s.localPlayer && app.tanks.occupied && how && std::strcmp(how, "melee") == 0) {
    auto it = app.tanks.tanks.find(app.tanks.occupied);
    if (it != app.tanks.tanks.end() && !it->second.def->car && !it->second.def->heli && !it->second.wrecked) {
      if (attacker >= 0 && attacker < (int)sold.size() && sold[attacker].zombie && enemyType(sold[attacker].etype).tankDamage) app.tanks.meleeBlow(app, it->first, sold[attacker].feet);
      drop(2); return;
    }
  }
  // buttoned up in a tank (hatch closed): the crew takes no damage until the tank is knocked out (destroy() bails them out).
  // Head out of the hatch stays exposed.
  if (s.localPlayer && app.tanks.occupied) {
    auto it = app.tanks.tanks.find(app.tanks.occupied);
    if (it != app.tanks.tanks.end() && !it->second.def->car && !it->second.wrecked && it->second.station == TS_GUNNER && it->second.hatchT == 0) { drop(3); return; }
  }
  if (attacker >= 0 && attacker != i && sameTeam(attacker, i) && !match.rules.friendlyFire) { drop(4); return; }
  if (match.mode() == GameMode::SearchDestroy) {   // SD1: no damage in freeze / breaks; the mode's armor (spec §8)
    if (sdFrozen()) { drop(5); return; }
    dmg = sdArmor(*this, i, dmg, how, attacker); s.lastHow = how ? how : "";
    if (dmg <= 0) { drop(6); return; }
  }
  drop(7);
  if (s.zombie && s.horseHp > 0 && !isClient) {   // HK3: the mounted knight: the horse's own health
    const std::string k = how ? how : "";
    const bool area = k == "blast" || k == "shell" || k == "flame" || k == "burn" || k == "vehicle", toHorse = s.hitHorse || area || k == "melee";
    s.hitHorse = false;
    if (toHorse) {
      s.horseHp -= dmg; s.br.hurtFrom = attacker >= 0 ? sold[attacker].feet : s.feet - dir * 10.0f; s.br.hurtAt = t;
      aiHorseHurt(*this, s, s.horseHp <= 0);   // HS1: a neigh, or its death whinny
      if (s.horseHp <= 0) { s.mountDeath = 1; s.riderHpKeep = s.hp; s.lastHow = k; s.hp = 0; killSoldier(i, attacker, dir); return; }   // the horse down: horse_death, on foot after
      if (!area) { s.lastHow = k; return; }   // a blast reaches the rider too
    }
  }
  if (!(s.zombie && enemyType(s.etype).invuln)) s.hp -= dmg;   // BC1: an invulnerable type never loses health
  if (!s.localPlayer && !s.zombie && s.hp > 0 && !isClient) {   // SND13: soldiers had no pain sound: the hurt grunt at their head, quieter, at most one per 0.6 s each
    static std::unordered_map<int, double> last; static std::mt19937 rng{1313};
    double& l = last[i];
    if (t - l >= 0.6 || t < l) { l = t; std::uniform_real_distribution<float> u(-1, 1); app.audio.play("player_hurt", eye(s), 0.45f * (1.0f + 0.15f * u(rng)), 1.0f + 0.04f * u(rng)); }
  }
  if (s.zombie) { const EnemyType& e = enemyType(s.etype); if (e.staggerS > 0 && dmg >= e.staggerMin) s.staggerUntil = t + e.staggerS; }   // EN1: only a heavy hit staggers
  s.br.hurtFrom = attacker >= 0 ? sold[attacker].feet : s.feet - dir * 10.0f; s.br.hurtAt = t;
  s.br.suppressed = std::min(1.0f, s.br.suppressed + 0.5f);
  if (s.localPlayer) { hurtFlash = std::min(1.0f, hurtFlash + dmg / 60.0f); lastHurtDir = -dir; }
  s.lastHow = how ? how : "";   // the hit's zone / kind ("head", "upper torso", "blast", "melee", ...): the death pick reads it
  if (!isClient) npcHurt(*this, i, attacker);   // NPC1: civilians fight back or flee
  if (s.hp <= 0) killSoldier(i, attacker, dir);
}

void Game::explosionDamage(vec3 p, float dmg, float radius, int owner, bool grenade) {
  if (!active || isClient) return;
  float R = grenade ? gren.tun.fragMaxR : radius * jkey(combat, "explosion").value("personnelRadiusFactor", 3.0f);
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
    raw *= occ * jkey(combat, "explosion").value("scale", 1.0f);
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
  if (w.sinceShot >= 0.4f) { w.sinceShot = 0; hearSound(muzzle, (int)s.role, jkey(archetypes, "perception").value("hearShot_m", 70.0f), s.slot, 0); ++shotsByBots; }
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
    rp.blastDmg = d.rocketDmg; rp.blastR = d.rocketR; rp.craterScale = d.rocketCrater; rp.splashGun = d.rocketSplashGun; rp.atKill = d.rocketAtKill; rp.gravScale = d.rocketGrav; rp.armourMult = d.rocketArmour; rp.owner = s.slot;
    if (d.rocketBackblast) app.fx.backblast(muzzle - dd * 1.4f, -dd);
    tracer = 0;
  } else
  app.fireRounds(d, start, dd, tracer, (uint32_t)s.slot * 131u + (uint32_t)w.shotIdx * 7919u, s.slot);   // MG1: a shotgun's pellets
  if (s.kit == 4) for (size_t k = 0, n = std::min(app.projectiles.size(), (size_t)std::max(1, d.pellets)); k < n; ++k) app.projectiles[app.projectiles.size() - 1 - k].trailSkip = 0.9f * d.flashScale * std::clamp(0.45f + 0.6f * d.muzzle.z, 0.55f, 1.3f);   // DF1: the trail starts past the fireball
  if (s.kit == 4 && !d.blaster) app.fx.muzzleFireball(muzzle + dd * 0.01f, dd, d.flashScale * std::clamp(0.45f + 0.6f * d.muzzle.z, 0.55f, 1.3f), d.suppressed || lootSuppressedShot(d, w), muzzle, d.caliber.c_str());   // DF1: the fireball flash (fx.cpp)
  else app.fx.muzzleFlash(muzzle + dd * 0.02f, dd, d.flashScale * 1.2f, d.suppressed, muzzle, d.blaster ? blasterColour(d.blaster) : vec3(-1), d.caliber.c_str(), nullptr, d.smoke > 0);   // WS2: a period gun's flash always shows   // WP4: the same flash and light as the player's
  app.fx.smoke(muzzle, dd * 2.0f + vec3(0, 0.3f, 0), 0.05f, 1.2f, vec4(0.6f, 0.58f, 0.55f, 0.35f), 0.6f, 0.2f);
  {   // WS1; WS3 (the user: "only give closest AI gun smoke"): of the AI, only the living bot nearest the camera leaves smoke (found once per step)
    static uint64_t at = ~0ull; static int near = -1;
    if (at != steps) { at = steps; near = -1; float bd = 1e9f; const vec3 eye = app.eyePos(); for (auto& o : sold) if (o.alive && !o.human && !o.zombie) { const float dd2 = glm::length(o.feet - eye); if (dd2 < bd) { bd = dd2; near = o.slot; } } }
    if (s.slot == near && !(d.suppressed || lootSuppressedShot(d, w))) app.fx.gunSmoke(muzzle, dd, d.smoke, s.slot);
  }
  app.audio.play(lootSuppressedShot(d, w) && app.audio.hasEvent("shot_suppressed_rifle") ? std::string("shot_suppressed_rifle") : "shot_" + d.id + (d.action == "auto" && std::string(app.fireModeName(d, w)) == "SEMI" ? "_semi" : ""), muzzle, 1.0f);   // SND15; SND18: as weapons.cpp
  if (!s.localPlayer) app.shotCasing(d, s.feet, s.slot);   // SND4
  s.recoil = d.action == "launcher" ? 3.0f : 1.0f; s.lastShot = t;   // RC2: a launcher rocks the shooter's arms and weapon back (pose.cpp)
  hearSound(muzzle, (int)s.role, jkey(archetypes, "perception").value("hearShot_m", 70.0f), s.slot, 0);
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
    float gl = glm::length(e - g.p); vec3 da = g.off * visFade(g.trav), db = g.off * visFade(g.trav + gl); g.trav += gl;   // AT4: from the shooter's muzzle onto the path
    if (g.mesh) {} else if (g.tracer) app.fx.bullet(g.p + da, e + db, g.tracer); else app.fx.streak(g.p + da, e + db, 0.007f, 0.025f, vec4(0.7f, 0.68f, 0.62f, 1));
    g.p = e; g.v.y -= 9.81f * dt; g.life += dt;
    if (hit || g.life > 3) { ghosts.erase(ghosts.begin() + k); continue; }
    ++k;
  }
  gren.expire(t);
  if (app.R.perf.wxDrift) gren.drift(app.R.wx.der.windXZ, dt);   // weather: clouds creep downwind (host and client alike, from the same state)
  gren.particles(app.fx, t, dt);
  if (Soldier* L = local(); L && L->alive) localGrenadeCounts(*L, false);
}

// CP (corpus 12 s12.4): poses are evaluated once per drawn frame (posesFrame), not once per fixed step; --rq-off poseframe
// restores the old per-step call (A/B)
bool gPosePerStep = false, gPoseLodOff = false, gPoseSerial = false, gStepCap8 = false;   // CP: Renderer::Perf poseframe / poselod / posejobs / stepinterp, copied each frame (explore.cpp)
void Game::step(float dt) {
  if (!active || paused) return;
  for (auto& s : sold) s.prevFeet = s.feet;   // CP: the drawn position lies between this and the step's result
  hitMarker = std::max(0.0f, hitMarker - dt); killMarker = std::max(0.0f, killMarker - dt); hurtFlash = std::max(0.0f, hurtFlash - dt * 0.6f);
  if (isClient) { clientStep(dt); if (gPosePerStep) { drawAlpha = 1; posesStep(dt); } return; }
  auto c0 = std::chrono::steady_clock::now();
  struct Timer { Game& g; std::chrono::steady_clock::time_point c; ~Timer() { g.stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c).count(); ++g.stepCalls; } } timer{*this, c0};
  t += dt; ++steps;
  for (auto& s : sold) {   // never a non-finite position (it reached Terrain::height via the camera / draws: a crash): restore the last good one, log the first
    if (std::isfinite(s.feet.x) && std::isfinite(s.feet.y) && std::isfinite(s.feet.z) && std::isfinite(s.yaw)) { s.lastGoodFeet = s.feet; continue; }
    static int logged = 0;
    if (logged++ < 3) logf("game: soldier %d (%s, alive %d, agent %d, local %d) has a non-finite position (%f %f %f) yaw %f at t %.1f%s; restored to (%.1f %.1f %.1f)", s.slot, s.human ? "human" : "bot", (int)s.alive, s.agent, (int)s.localPlayer,
                           s.feet.x, s.feet.y, s.feet.z, s.yaw, t, sdActive() ? " (SD)" : "", s.lastGoodFeet.x, s.lastGoodFeet.y, s.lastGoodFeet.z);
    s.feet = s.lastGoodFeet; if (!std::isfinite(s.yaw)) s.yaw = 0; s.vel = vec3(0);
  }
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
  for (auto& s : sold) match.reportAlive(s.slot, rosterAlive(s), s.feet);
  sdStep(*this, dt);   // SD1: rounds, the bomb, the economy (respawns at a round start go through stepRest)
  if (gPosePerStep) { crumb("game: poses"); drawAlpha = 1; posesStep(dt); }
  crumb("game: doors");
  // doors moved on the host (players, bots, client requests) -> clients
  if (gNet && gNet->role == Net::HOST)
    for (auto& [id, r] : app.rt) for (size_t k = 0; k < r.dir.size(); ++k) {
      uint64_t key = (id << 8) | (uint64_t)k; int seen = doorSeen.count(key) ? doorSeen[key] : 0;
      if (r.dir[k] != seen) { doorSeen[key] = r.dir[k]; gNet->hostWorldEvent(json{{"t", "door"}, {"i", id}, {"k", k}, {"dir", r.dir[k]}, {"p", r.progress[k]}}); }
    }
  for (auto& s : sold) {
    if (!s.alive && s.zombie && !isClient) aiZombieDead(*this, s);   // HK2: a mounted knight's horse falls, he goes on on foot
    if (!s.alive) continue;
    crumb("game: soldier weapon"); soldierWeaponStep(s, dt);
    crumb("game: throw"); throwStep(s, dt);
    crumb("game: ai"); if (!s.human && !royaleAirborne(s.slot)) aiStep(s, dt);   // BR1: aboard / falling: the drop moves it
  }
  crumb("game: loot"); lootStep(*this, dt);   // BR2: strip new lives, heal, bots pick up
  crumb("game: burn"); burnStep(dt);
  crumb("game: rest"); stepRest(dt); crumb("game: done");
}

void Game::posesFrame(float dt, float alpha) {
  if (!active || paused || gPosePerStep) return;
  drawAlpha = std::clamp(alpha, 0.0f, 1.0f); ++poseFrameNo;
  if (isClient) { void netDrawPositions(Game&); netDrawPositions(*this); }   // CP: remote soldiers at this frame's time
  crumb("game: poses"); posesStep(dt);
}

void Game::poseNow(Soldier& s) {
  if (s.poseAcc <= 0 || s.pose.delta.empty() || gPosePerStep) return;   // posed this frame already
  ++posesOnDemand; poseOnly = &s; posesStep(0); poseOnly = nullptr;
}

void Game::posesStep(float dtFrame) {
  // CP (corpus 12 s12.4): rate LOD at any soldier count, staggered by slot: every frame within 25 m, then every 2nd / 3rd / 4th frame
  // to 50 / 100 m / beyond (20 % hysteresis); not evaluated at all while in no view (the camera's, or past 30 m the sun shadow's
  // reach), except once a second. A soldier not evaluated keeps its pose and moves rigidly with its feet; the next evaluation runs
  // with all the time it missed. (Replaces AI2's bands, which only applied over 200 combatants.)
  vec3 cam, camF; renderView(cam, camF);   // the actual camera (vehicle chase / sight cameras too)
  const float cosView = std::cos(radians(0.5f * 70.0f * 1.9f) + 0.25f);   // the 70 deg vertical view at 16:9 is ~106 deg across: a cone of half its diagonal plus a margin
  const bool lodOff = gPoseLodOff, kPoseSerial = gPoseSerial || poseOnly;   // A/B (poseNow: on this thread)
  const auto pt0 = std::chrono::steady_clock::now();
  struct PoseTimer { Game& g; std::chrono::steady_clock::time_point c; ~PoseTimer() { g.poseMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c).count(); } } poseTimer{*this, pt0};   // CP: main-thread pose time
  struct PoseJob { Soldier* s; PoseParams pp; vec3 drawFeet; float aimYawDraw, dt; };
  static std::vector<PoseJob> jobs; jobs.clear();
  auto finish = [&](Soldier& s, const PoseParams& pp, vec3 drawFeet, float aimYawDraw) {   // CP: the drawn transform (after the pose: dfYawOff)
    const CharModel& m = *models[s.model]; (void)m;
    float yawUse = s.localPlayer ? app.eyeYaw : (s.gunBody || clipKit(s.kit) || s.holdBody) && s.alive && (s.br.target >= 0 || t - s.lastShot < 1.5) ? aimYawDraw : s.yaw;   // GM1: the clips cannot twist to the aim: engaged, the whole body turns to it
    if (s.alive && !s.localPlayer) yawUse -= s.dfYawOff;   // 0 unless a clip body (gunbody.cpp deltaBodyPose)   // DF1: the clip holds its gun turned off its body's forward (gunbody.cpp): the body turns so the gun meets the aim
    s.world = glm::translate(mat4(1), drawFeet) * glm::mat4_cast(glm::angleAxis(radians(yawUse) + 3.14159265f, vec3(0, 1, 0)));   // CP: drawn between the last two steps
    if (s.zombie) {   // EN1: the type's draw scale, and a forward lean about the feet (the stalker on the zombie rig)
      if (s.eLean != 0 && s.alive) s.world = glm::rotate(s.world, radians(s.eLean), vec3(1, 0, 0));
      if (s.eScale != 1) s.world = glm::scale(s.world, vec3(s.eScale));
    } else if (s.eScale != 1) s.world = glm::scale(s.world, vec3(s.eScale));   // RB6: a soldier kit's "scale" (the KR-714): body, held gun and muzzle with it
    if (static const char* ft = std::getenv("NMK_FOOT_TRACE"); ft && !s.localPlayer && !s.zombie && s.alive && s.slot < 24 && !s.pose.delta.empty()) {   // AN1 step 3b: planted-foot slip (foottrace.csv)
      static FILE* f = nullptr; if (!f) { std::error_code ec; fs::create_directories(exeDir() / "selftest_out", ec); f = std::fopen((exeDir() / "selftest_out" / "foottrace.csv").string().c_str(), "w"); if (f) std::fprintf(f, "t,slot,model,speed,stanceL,stanceR,lx,lz,rx,rz,gapL,gapR\n"); }
      auto st = [&](int leg) { float u = (pp.walkPhase + (leg ? 3.14159265f : 0.0f)) / 6.2831853f; u -= std::floor(u); float du = std::clamp(pp.duty, 0.2f, 0.8f); return u > 0.1f * du && u < 0.9f * du ? 1 : 0; };
      vec3 L = vec3(s.world * vec4(s.pose.joint[CJ_LFOOT], 1)), R = vec3(s.world * vec4(s.pose.joint[CJ_RFOOT], 1));
      auto gap = [&](vec3 F, int sf) { Physics::Hit h; return Physics::castRayEx(F + vec3(0, 0.6f, 0), vec3(0, -1, 0), 1.5f, h) ? F.y - m.jointHead[sf].y - h.pos.y : 9.0f; };   // ankle over the ground minus its rest height (+ hover, - sink)
      if (f) std::fprintf(f, "%.4f,%d,%s,%.2f,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", t, s.slot, m.id.c_str(), s.speed, st(0), st(1), L.x, L.z, R.x, R.z, gap(L, m.semantic[CJ_LFOOT]), gap(R, m.semantic[CJ_RFOOT]));
    }
    if (s.flat && !s.alive) {   // squashed: the whole body pressed into a thin, slightly spread layer on the ground, within 0.12 s
      float k = std::min(1.0f, (float)(t - s.diedAt) / 0.12f);
      s.world = glm::translate(mat4(1), drawFeet) * glm::scale(mat4(1), vec3(1.0f + 0.25f * k, 1.0f - 0.96f * k, 1.0f + 0.25f * k)) * glm::translate(mat4(1), -drawFeet) * s.world;
    }
  };
  for (auto& s : sold) {
    if (!s.inUse || (!s.alive && t - s.diedAt > (s.zombie || s.gunBody || clipKit(s.kit) || s.holdBody ? 3.5 : 2.0))) continue;   // G2: the zombie death clip runs ~3 s (GM1: 2.4 s)
    const vec3 drawFeet = glm::length(s.feet - s.prevFeet) < 2.0f ? glm::mix(s.prevFeet, s.feet, drawAlpha) : s.feet;   // CP: between the last two steps (a jump of 2 m+ is a teleport: no streak)
    s.poseAcc += dtFrame;
    {
      const float d = glm::length(s.feet - cam);
      const float lo[3] = {25, 50, 100}; int r = 1;
      for (int k = 0; k < 3; ++k) if (d > lo[k] * (s.poseRate > k + 1 ? 0.8f : 1.0f)) r = k + 2;   // hysteresis: a slower band is kept until 20 % nearer
      s.poseRate = r;
      const vec3 c = s.feet + vec3(0, 0.9f, 0); const float dc = glm::length(c - cam);
      const bool inView = dc < 4.0f || glm::dot((c - cam) / std::max(dc, 1e-3f), camF) > cosView || d < 30.0f;   // near ones may cast a shadow into view
      // a melee strike lands where the posed joint is (zombie.cpp) and a throw leaves from the posed hand: those are posed every frame
      // a change of stance, death, a swim or a new life is posed at once whatever the band (no one is shot in a pose from before it)
      const int key = (int)s.posture | (s.alive ? 8 : 0) | (s.swim ? 16 : 0) | (s.flat ? 32 : 0) | ((int)(s.lifeId & 0xffff) << 6);
      const bool changed = key != s.poseKey; s.poseKey = key;
      const bool settling = !s.alive || std::fabs(s.anProne - (s.posture == Physics::PRONE ? 1.0f : 0.0f)) > 0.02f || std::fabs(s.crouchBlend - (s.posture == Physics::CROUCH ? 1.0f : 0.0f)) > 0.02f;   // a stance blend or a death in progress
      bool eval = s.localPlayer || lodOff || changed || settling || s.meleeWind >= 0 || s.throwT >= 0 || (inView ? (poseFrameNo + s.slot) % r == 0 : (poseFrameNo + s.slot) % 60 == 0);
      if (dtFrame <= 0 || s.pose.delta.empty()) eval = dtFrame > 0 || s.pose.delta.empty();
      if (poseOnly) { if (&s != poseOnly) continue; eval = true; }   // poseNow: this soldier only, now
      if (!eval) {   // held pose: rigid motion only (AIB1: it is not re-skinned, so it writes no skinning motion)
        if (!s.flat && !s.pose.delta.empty()) s.world[3] = vec4(drawFeet, 1.0f);
        continue;
      }
    }
    const float dt = std::min(s.poseAcc, 0.5f); s.poseAcc = 0;   // all the time since its last evaluation (capped: a soldier held off-view for long)
    const CharModel& m = *models[s.model];
    PoseParams pp;
    pp.walkAmount = std::min(1.0f, s.speed / 3.0f);
    float wantCrouch = s.posture == Physics::CROUCH ? 1.0f : 0.0f;
    s.crouchBlend += (wantCrouch - s.crouchBlend) * (1.0f - std::exp(-dt / 0.15f));   // AN1 step 3: ~0.4 s to settle
    s.anProne += ((s.posture == Physics::PRONE ? 1.0f : 0.0f) - s.anProne) * (1.0f - std::exp(-dt / 0.12f));   // lying down / getting up over ~0.35 s
    { float e = s.anProne; pp.crouch = s.crouchBlend; pp.prone = e * e * (3 - 2 * e); }
    pp.aimPitch = radians(s.localPlayer ? app.eyePitch : s.br.aimPitch);
    // AN1 step 3: a bot's drawn gun follows the brain's aim through a critically damped spring with a turn-rate cap (the brain's aim
    // jumps between the path and a threat, and its body turns with the path), so the weapon swings smoothly; the shots keep the brain's aim
    float aimYawDraw = s.br.aimYaw, aimPitchDraw = s.localPlayer ? app.eyePitch : s.br.aimPitch;
    if (!s.localPlayer && !s.zombie && !s.human) {
      bool hot = s.br.target >= 0 || t - s.lastShot < 1.5;
      if (!s.anGunInit || !s.alive) { s.anGunYaw = s.br.aimYaw; s.anGunPitch = s.br.aimPitch; s.anGunVel = s.anGunPVel = 0; s.anGunInit = s.alive; }
      const float w = hot ? 11.0f : 6.0f, vmax = hot ? 400.0f : 160.0f;   // rad/s natural frequency; deg/s cap
      auto follow = [&](float& x, float& v, float target, bool wrap) {   // CP: sub-stepped at <= 1/60 s (dt is a whole frame or more now)
        const int n = std::max(1, (int)std::ceil(dt * 60.0f - 1e-3f)); const float h = dt / n;
        for (int i = 0; i < n; ++i) {
          float e = wrap ? wrapDeg(target - x + 180.0f) - 180.0f : target - x;
          v += (w * w * e - 2 * w * v) * h; v = std::clamp(v, -vmax, vmax); x += v * h; if (wrap) x = wrapDeg(x);
        }
      };
      follow(s.anGunYaw, s.anGunVel, s.br.aimYaw, true); follow(s.anGunPitch, s.anGunPVel, s.br.aimPitch, false);
      aimYawDraw = s.anGunYaw; aimPitchDraw = s.anGunPitch;
    }
    pp.aimPitch = radians(aimPitchDraw);
    float yawOff = s.localPlayer ? 0.0f : std::clamp(radians(wrapDeg(aimYawDraw - s.yaw + 180.0f) - 180.0f), -0.9f, 0.9f);
    if (s.zombie) { s.walkPhase += s.speed * dt * 2.6f; pp.aimYawOffset = yawOff; }
    else {   // AN1: kneel (still, crouched), carry (moving, not engaged), moveAim (moving, engaged); ~0.25 s blends
      float v = s.speed, k = 1.0f - std::exp(-dt / 0.18f);   // AN1 step 3: posture blends settle in ~0.5 s (0.08 s ended in a visible snap)
      float duty = std::clamp(0.62f - 0.30f * (v - 1.4f), 0.32f, 0.62f);   // AN3: the planted share falls faster with speed (jog ~0.45, sprint ~0.32, as running does)
      // AN3: upright travel. Out of contact the hips ride at 97 % (walk) .. 94 % (run, sprint) of their standing height, in contact 90 %;
      // the half-stride is capped to what the leg reaches from there (the cadence rises instead), so the hips need not drop to reach
      const bool engaged0 = (s.br.target >= 0 && !s.human && !s.localPlayer) || t - s.lastShot < 1.5 || s.reload > 0 || s.throwT >= 0 || (isClient && s.netEngaged);
      // AN4: the pelvis rides at one height per speed band (walk 95 %, run 93 %, sprint 91.5 %, in contact 92 %): low enough that the
      // capped stride below is always within the leg's reach, so the hips no longer dip at each footfall (AN3's vaulting gait read as a lunge)
      const float hipFrac = s.posture != Physics::STAND ? 0.0f : engaged0 ? 0.92f : v < 3.0f ? glm::mix(0.95f, 0.93f, std::clamp((v - 1.2f) / 0.8f, 0.0f, 1.0f)) : 0.915f;
      float cadUse = gaitCadence(v);
      if (hipFrac > 0 && v > 0.3f) {
        int su = m.semantic[CJ_LUPLEG], sk = m.semantic[CJ_LLEG], sf = m.semantic[CJ_LFOOT], sh = m.semantic[CJ_HIPS];
        if (su >= 0 && sk >= 0 && sf >= 0 && sh >= 0) {
          float L = 0.975f * (glm::length(m.jointHead[sk] - m.jointHead[su]) + glm::length(m.jointHead[sf] - m.jointHead[sk]));
          float vert = m.jointHead[su].y - (1 - hipFrac) * m.jointHead[sh].y - m.jointHead[sf].y;
          float Amax = std::max(0.12f, std::min(0.42f * L, 0.97f * std::sqrt(std::max(0.0f, L * L - vert * vert))));   // AN4: half-stride within the leg's reach from the fixed pelvis (vert: hip to ankle); past it the cadence rises
          cadUse = std::max(cadUse, 0.5f * duty * v / Amax);
        }
      }
      float cad = advanceStride(app, s, v, dt, cadUse);
      const float kf = 1.0f - std::exp(-dt / 0.08f);   // stride, travel direction and slope stay quick (the feet are planted on them)
      s.anStride += (0.5f * duty * v / cad - s.anStride) * kf;
      pp.gait = true; pp.stride = s.anStride; pp.duty = duty;
      float yawUse = s.localPlayer ? app.eyeYaw : s.yaw;
      quat bodyQ = glm::angleAxis(radians(yawUse) + 3.14159265f, vec3(0, 1, 0));
      vec3 mv = glm::inverse(bodyQ) * vec3(s.vel.x, 0, s.vel.z);
      if (glm::length(mv) > 0.3f) s.anDir = glm::normalize(glm::mix(s.anDir, glm::normalize(mv), kf));
      else s.anDir = glm::normalize(glm::mix(s.anDir, vec3(0, 0, 1), kf));
      pp.moveDir = s.anDir;
      bool moving = v > 0.5f;
      bool engaged = (s.br.target >= 0 && !s.human && !s.localPlayer) || t - s.lastShot < 1.5 || s.reload > 0 || s.throwT >= 0
                  || (isClient && s.netEngaged);   // AN1b: a LAN client has no brains: the host's "has a target" bit (net.cpp)
      s.anKneel += ((s.posture == Physics::CROUCH && !moving ? 1.0f : 0.0f) - s.anKneel) * k;
      // AI5: bots never carry the rifle low on the move: shouldered, muzzle forward, sights up whether engaged or not
      bool bot = !s.human && !s.localPlayer;
      s.anCarry += ((moving && !engaged && !bot ? 1.0f : 0.0f) - s.anCarry) * k;
      s.anMoveAim += ((moving && engaged ? 1.0f : 0.0f) - s.anMoveAim) * k;   // AN3: the glide only in contact (was every moving bot)
      pp.hunch = bot && !moving ? (s.kit == 2 ? 0.25f : 1.0f) : 0.0f;   // AN3: no hunch on the move (the combat crouch is hipFrac / leanMove)
      pp.hipFrac = moving ? hipFrac : 0.0f;
      pp.leanMove = moving && s.posture == Physics::STAND ? (engaged ? 0.21f : v < 1.6f ? 0.07f : v < 3.0f ? 0.20f : 0.30f) : -1.0f;   // AN3: spine pitch (rad): 4 / 9 / 13 deg, 11 in contact   // AN1 step 2b: troopers as upright as the soldiers look (their armour reads deeper)   // SW3: blaster troopers aim and fire half as hunched (taller stance); cover crouching unchanged
      const bool frozenBot = bot && sdFrozen();   // SD7: buy time: shouldered, muzzle level and forward (as on the move), not the relaxed low ready
      s.anLow += ((!moving && !engaged && !frozenBot ? (s.posture == Physics::CROUCH ? 0.6f : 1.0f) : moving && !engaged && bot ? 0.6f : 0.0f) - s.anLow) * k;   // AN3: a bot on the move out of contact: low ready   // relaxed ready: butt in the shoulder, muzzle ~35 deg down (AN1 step 2)
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
        s.anSlope += (vec2(gm.x, gm.z) - s.anSlope) * kf;
        pp.slope = s.anSlope;
      }
    }
    pp.walkPhase = s.walkPhase;
    if (s.weapon >= 0) { const WeaponDef& d = *app.weaponDefs[s.weapon]; pp.gripR = d.gripR; pp.gripL = d.gripL; pp.magPos = clipKit(s.kit) || s.holdBody ? weaponPartCentre(d, 2, d.gripL + vec3(0, -0.1f, -0.12f)) : d.gripL + vec3(0, -0.1f, -0.12f); pp.holdOffset = d.holdOffset; pp.holdWeld = d.holdWeld; pp.holdKind = d.holdKind; pp.leftHold = d.leftHold; pp.gripPose = s.holdBody && d.gripPose.has ? &d.gripPose : nullptr;
      if (d.leftHold == 1) pp.gripL = weaponPartCentre(d, 2, d.gripL);   /* WS1: the support hand on the magazine itself (before the scale below: a larger-drawn gun's magazine moves with it) */
      if (pp.gripPose && d.gripPose.scale != 1) { pp.gripL = pp.gripR + (pp.gripL - pp.gripR) * d.gripPose.scale; pp.magPos = pp.gripR + (pp.magPos - pp.gripR) * d.gripPose.scale; }   /* WS1: a gun drawn larger about its grip: its sockets with it */   /* WS1: the butt plate clear in front of the shoulder as approved (the stored wrist sits behind the grip: against the shoulder the firing elbow has nowhere to go but up) */
      pp.gripC = d.gripC; pp.gripAx = d.gripAx; pp.gripRad = d.gripRad; pp.guardC = d.guardC; pp.guardRad = d.guardRad; pp.sightR = d.rear; }   // AN1 step 2b
    pp.reload = s.reload > 0 && s.reloadTotal > 0 ? 1.0f - s.reload / s.reloadTotal : 0;
    pp.throwT = s.throwT; pp.recoil = s.recoil;
    if (static const char* tr = std::getenv("NMK_AIM_TRACE"); tr && !s.human && !s.localPlayer && s.alive && s.slot < 16) {   // AN1 step 3: the bots' body / gun facing per frame (aimtrace.csv; step 7: zombie types too)
      static FILE* f = nullptr; if (!f) { std::error_code ec; fs::create_directories(exeDir() / "selftest_out", ec); f = std::fopen((exeDir() / "selftest_out" / "aimtrace.csv").string().c_str(), "w"); if (f) std::fprintf(f, "t,slot,bodyYaw,gunYaw,gunPitch,speed,engaged\n"); }
      if (f) std::fprintf(f, "%.4f,%d,%.2f,%.2f,%.2f,%.2f,%d\n", t, s.slot, s.yaw, wrapDeg(s.yaw + degrees(pp.aimYawOffset)), degrees(pp.aimPitch), s.speed, s.anMoveAim > 0.5f ? 1 : 0);
    }
    pp.lowReady = s.zombie ? 0.0f : std::max(s.anLow, !s.human && !s.localPlayer && s.speed > 4.0f ? 0.2f : 0.0f);   // AI5: a sprinting bot dips the muzzle a little, weapon still forward   // AN1: the run carry (pp.carry) replaces the lowered muzzle on the move
    if (s.localPlayer) app.mantlePose(s.feet, pp);   // hands on the ledge while mantling (mantle.cpp)
    s.swimBlend += ((s.swim && s.alive ? 1.0f : 0.0f) - s.swimBlend) * std::min(1.0f, dt * 5);
    pp.swim = s.swimBlend; pp.swimPhase = (float)t * 5.3f + (float)s.slot;   // breaststroke ~0.85 Hz
    if (s.localPlayer) app.swimPose(pp);   // swim.cpp: the player's own stroke phase
    if (!s.alive) { pp.death = std::min(1.0f, (float)(t - s.diedAt) / 0.8f); pp.deathStyle = s.deathStyle; }
    const bool skipPose = false;   // CP: the rate decision is made above
    // AN1 step 3b: foot planting. A foot that comes down is held at its world point for its stance (turns, speed changes, strafes and
    // stops no longer drag it); it lets go at lift-off, when the leg would overreach, or (standing) when it ends 14 cm off its rest spot
    // (turning in place), blending from that point to its stepping path over a third of the swing (a standing re-step lifts it)
    if (!noFootLock && !s.zombie && s.alive && pp.gait && pp.prone < 0.5f && pp.kneel < 0.5f && pp.swim < 0.5f && pp.climb < 0 && !s.pose.delta.empty() && !skipPose) {
      float yawNow = s.localPlayer ? app.eyeYaw : s.yaw;
      mat4 Wn = glm::translate(mat4(1), drawFeet) * glm::mat4_cast(glm::angleAxis(radians(yawNow) + 3.14159265f, vec3(0, 1, 0))), Wi = glm::inverse(Wn);   // CP: the drawn feet
      bool moving = s.speed > 0.3f; float du = std::clamp(pp.duty, 0.2f, 0.8f), cadN = gaitCadence(s.speed);
      for (int leg = 0; leg < 2; ++leg) {
        int sf = m.semantic[leg ? CJ_RFOOT : CJ_LFOOT], su = m.semantic[leg ? CJ_RUPLEG : CJ_LUPLEG], sk = m.semantic[leg ? CJ_RLEG : CJ_LLEG];
        if (sf < 0 || su < 0 || sk < 0) continue;
        float u = (pp.walkPhase + (leg ? 3.14159265f : 0.0f)) / 6.2831853f; u -= std::floor(u);
        bool stance = u < du;
        vec3 cur = vec3(s.world * vec4(s.pose.joint[leg ? CJ_RFOOT : CJ_LFOOT], 1));
        float reach = 0.97f * (glm::length(m.jointHead[sk] - m.jointHead[su]) + glm::length(m.jointHead[sf] - m.jointHead[sk]));
        if (s.anFootLock[leg]) {
          vec3 pin = vec3(Wi * vec4(s.anFootW[leg], 1)), rest = m.jointHead[sf];
          bool over = glm::length(vec2(pin.x - rest.x, pin.z - rest.z)) > std::min(reach * 0.75f, 2.2f * pp.stride + 0.16f);   // left behind beyond a stride (or the leg's reach)
          bool other = s.anFootRel[1 - leg] >= 0;   // one standing re-step at a time
          float ang = std::fabs(std::atan2(pin.x * rest.z - pin.z * rest.x, pin.x * rest.x + pin.z * rest.z));   // turned in place about the body's centre
          bool far = !moving && !other && (glm::length(vec2(pin.x - rest.x, pin.z - rest.z)) > 0.10f || ang > 0.5f);
          if (over || far || (moving && !stance)) {
            s.anFootLock[leg] = false; s.anFootRel[leg] = 0; s.anFootFrom[leg] = s.anFootW[leg]; s.anFootStep[leg] = !moving && !over;
            s.anFootDur[leg] = moving ? std::max(0.08f, 0.35f * (1 - du) / cadN) : 0.32f;
          }
        } else if (stance && s.anFootRel[leg] < 0) {
          s.anFootLock[leg] = true; s.anFootW[leg] = cur; s.anFootN[leg] = vec3(0);
          // AN1 step 3c: touchdown probe: the anchor on the real ground under it (a stair tread, a slope), the ankle its rest height above
          Physics::Hit h;
          static const bool probeOff = std::getenv("NMK_NO_FOOTPROBE") != nullptr;   // captures: the before measurement
          if (!probeOff && Physics::castRayEx(cur + vec3(0, 0.45f, 0), vec3(0, -1, 0), 0.9f, h) && h.normal.y > 0.5f) {
            s.anFootW[leg].y = h.pos.y + m.jointHead[sf].y; s.anFootN[leg] = h.normal;
          }
        }
        if (s.anFootLock[leg]) { pp.footPin[leg] = vec3(Wi * vec4(s.anFootW[leg], 1)); pp.footPinW[leg] = 1; if (s.anFootN[leg] != vec3(0)) pp.footN[leg] = glm::normalize(vec3(Wi * vec4(s.anFootN[leg], 0))); }
        else if (s.anFootRel[leg] >= 0) {
          s.anFootRel[leg] += dt; float k = s.anFootRel[leg] / s.anFootDur[leg];
          if (k >= 1) s.anFootRel[leg] = -1;
          else { float e = k * k * (3 - 2 * k); pp.footPin[leg] = vec3(Wi * vec4(s.anFootFrom[leg], 1)); pp.footPinW[leg] = 1 - e; if (s.anFootStep[leg]) pp.footLift[leg] = 0.07f * std::sin(3.14159265f * k); }
        }
      }
    } else if (!s.alive || pp.prone >= 0.5f || pp.kneel >= 0.5f || pp.swim >= 0.5f) { s.anFootLock[0] = s.anFootLock[1] = false; s.anFootRel[0] = s.anFootRel[1] = -1; }
    // CP: soldiers on the clip or procedural path are evaluated on the job threads after this loop (no GL, no shared writes: the
    // lazy rig caches are locked, gunbody.cpp); built-in-gun bodies and zombies (sounds, pack leaders) stay here
    if (!s.gunBody && !(s.zombie && !m.clips.empty()) && !kPoseSerial) { jobs.push_back({&s, pp, drawFeet, aimYawDraw, dt}); continue; }
    if (s.gunBody) { if (!(skipPose ? false : deltaBodyPose(*this, s, dt, pp))) gunBodyPose(*this, s, dt); }   // GM1: its own clips, the gun in its hands (NA1: an 8-way clip set: gunbody.cpp deltaBodyPose, its built-in gun mode)
    else if ((clipKit(s.kit) || s.holdBody) && !skipPose && deltaBodyPose(*this, s, dt, pp)) {}   // DF1: the clips' body, the arms on the held gun (DSD1: the desert soldiers too)
    else if (s.zombie && !m.clips.empty()) aiZombiePose(*this, s, dt); else if (!skipPose) { poseCharacter(m, pp, s.pose); s.poseFrom = -1; ++s.poseRev; }   // G2: clips
    finish(s, pp, drawFeet, aimYawDraw);
  }
  Physics::parallelFor((int)jobs.size(), [&](int i) {
    PoseJob& j = jobs[i]; Soldier& s = *j.s; const CharModel& m = *models[s.model];
    if (!((clipKit(s.kit) || s.holdBody) && deltaBodyPose(*this, s, j.dt, j.pp))) { poseCharacter(m, j.pp, s.pose); s.poseFrom = -1; ++s.poseRev; }
  });
  for (PoseJob& j : jobs) finish(*j.s, j.pp, j.drawFeet, j.aimYawDraw);
}

void Game::stepRest(float dt) {
  // grenades
  crumb("game: grenades");
  // grenades: bodies, fuses and blasts are the shared core (App::stepGrenades); smoke clouds and the AI view here
  syncGrenadeView();
  if (app.R.perf.wxDrift) gren.drift(app.R.wx.der.windXZ, dt);   // weather: clouds creep downwind (host and client alike, from the same state)
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
  int maxC = jkey(combat, "corpses").value("max", 12); double life = jkey(combat, "corpses").value("seconds", 40.0);
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
  app.R.smokeSigma = 0;
  if (!active) return;
  app.R.smokeSigma = gren.sigmaAt(app.eyePos(), t);   // weather 7: the fog inside a smoke cloud (the near sprites fade out)
  double life = jkey(combat, "corpses").value("seconds", 40.0);
  drops.erase(std::remove_if(drops.begin(), drops.end(), [&](const Drop& d) { return t - d.t > life; }), drops.end());
  for (auto& d : drops) {   // GM1: lying on its side next to the body
    const WeaponDef& w = *app.weaponDefs[d.weapon];
    mat4 W = glm::translate(mat4(1), d.pos) * glm::rotate(mat4(1), radians(d.yaw + 90.0f), vec3(0, 1, 0)) * glm::rotate(mat4(1), radians(90.0f), vec3(0, 0, 1)) * glm::translate(mat4(1), vec3(0, 0, -0.4f));
    if (w.useModel()) { for (int p = 0; p < 8; ++p) for (int k : w.worldParts()[p]) app.R.worldModels.push_back({w.worldModel(), k, W}); }
    else for (int p = 0; p < 8; ++p) if (w.mesh[p].count) app.worldBoxDraws.push_back({&w.mesh[p], W});
    if (w.useModel()) brSightDraws(app, w, d.ws, W, app.R.worldModels, app.worldBoxDraws);   // TH1: a dropped gun keeps its fitted optic
  }
  lootDraw(*this);   // BR2 floor items
  crumb("game: draw");
  vec3 cam, fwd; renderView(cam, fwd);   // cull against the camera actually rendering (a vehicle's chase / sight camera), not the seated body
  static int frame = 0; ++frame;
  for (auto& h : horseCorpses) for (int p : h->parts) { app.R.worldModels.push_back({&h->ci.view, p, h->world}); app.R.worldModels.back().prevM = h->world; app.R.worldModels.back().vel = 1; }   // HK2: fallen horses
  for (auto& s : sold) {
    if (!s.inUse || s.lifeId == 0) continue;
    if (!s.alive && t - s.diedAt > jkey(combat, "corpses").value("seconds", 40.0)) continue;
    // first person: the viewmodel is drawn instead; bodyshadow: the body (the pose the others see, posed once above) goes to the
    // sun cascades only (Renderer::shadowModels), in direct sun (wantBodyShadow: checked before any skinning), never in a vehicle seat
    const bool selfShadow = s.localPlayer && s.alive;
    if (selfShadow && (!app.R.wantBodyShadow() || app.tanks.occupied)) continue;
    float bs = bodyScale(s);   // giants (large draw scales) reach far above and around their feet
    vec3 c = s.feet + vec3(0, 0.9f * bs, 0);
    float dist = glm::length(c - cam);
    if (dist > 300 + 2 * bs) continue;
    if (!selfShadow && dist > 4 * bs && glm::dot(glm::normalize(c - cam), fwd) < -0.2f) continue;   // behind the camera (shadows of close ones kept)
    s.drawnAt = t;
    struct SelfShadowMove {   // everything pushed for the live local player below moves to the cascade-only list (head and helmet left out:
      App& a; bool on; size_t m0, b0, h0;   // they sit at the eye and would shade the viewmodel); its box-mesh and thermal draws are dropped
      ~SelfShadowMove() {
        if (!on) return;
        for (size_t i = m0; i < a.R.worldModels.size(); ++i) {
          const ModelDraw& d = a.R.worldModels[i];
          const std::string& nm = d.model && d.part >= 0 && d.part < (int)d.model->parts.size() ? d.model->parts[d.part].name : std::string();
          std::string lo = nm; for (char& ch : lo) ch = (char)std::tolower((unsigned char)ch);
          if (lo.find("head") != std::string::npos || lo.find("helmet") != std::string::npos) continue;
          a.R.shadowModels.push_back(d);
        }
        a.R.worldModels.resize(m0); a.worldBoxDraws.resize(b0); a.R.heatDraws.resize(h0);
      }
    } selfMove{app, selfShadow, app.R.worldModels.size(), app.worldBoxDraws.size(), app.R.heatDraws.size()};
    // PF1: a far pack follower in step with its leader draws the leader's skinned mesh (zombie.cpp aiZombiePose)
    Soldier* src = s.poseFrom >= 0 && s.poseFrom < (int)sold.size() && sold[s.poseFrom].ciReady && sold[s.poseFrom].ci.frameSkinned >= 0 ? &sold[s.poseFrom] : &s;
    if (src != &s || (!s.alive && t - s.diedAt > (s.zombie || s.gunBody ? 3.5 : 2.0) && s.ciReady && s.ci.frameSkinned >= 0)) {}   // settled corpse: keep the last skin
    else {
      if (!s.ciReady) { s.ci.create(*models[s.model], app.R.perf.vsSkin); s.ciReady = true; }   // V1 vsskin
      int every = dist > 140 ? 6 : dist > 70 ? 3 : 1;   // far: skin less often
      bool due = s.ci.frameSkinned < 0 || (s.skinRev != s.poseRev && (frame + s.slot) % every == 0);   // and never an unchanged pose
      if (due) { s.ci.skin(s.pose.skin, frame); s.ci.frameSkinned = frame; s.skinRev = s.poseRev; }
    }
    // AIB1: TAA motion: the body's previous world matrix (last frame's, if drawn then) and, skinned this frame, each vertex's last position
    const int vel = src->ci.frameSkinned == frame && src->ci.prevValid ? 2 : 1;
    const mat4 prevW = s.velFrame == frame - 1 ? s.velWorld : s.world; s.velWorld = s.world; s.velFrame = frame;
    for (int k = 0; k < (int)src->ci.view.parts.size(); ++k) if (!brHidePart(s, src->ci.view.parts[k].name)) {
      app.R.worldModels.push_back({&src->ci.view, k, s.world, s.zombie ? rulesTint(*this, s) * vec4(enemyType(s.etype).tint, 1) : rulesTint(*this, s)});
      app.R.worldModels.back().prevM = prevW; app.R.worldModels.back().vel = vel;
    }   // W1: protection shimmer
    vec4 heatObj(0.0f);   // TH2: the body's centre and reach, for its gun too
    if (app.R.thermalWanted) {   // TH1: body heat through a thermal scope: men hot (head / face hottest), monsters a little cooler, robots cool, the dead cooling over a minute
      const float h = s.alive ? (s.kit == 3 ? 0.45f : s.zombie ? 0.85f : 0.95f) : std::max(0.08f, 0.95f * (1.0f - (float)(t - s.diedAt) / 60.0f));
      // TH2: one even heat; the shading is the figure's own depth (the thermal pass): its centre and reach
      const float bs = bodyScale(s), up = s.posture == Physics::PRONE || !s.alive ? 0.25f : s.posture == Physics::CROUCH ? 0.6f : 0.95f;
      const vec4 obj(s.feet + vec3(0, up * bs, 0), (s.posture == Physics::PRONE || !s.alive ? 1.0f : 0.6f) * bs);
      heatObj = obj;
      for (int k = 0; k < (int)src->ci.view.parts.size(); ++k) {
        if (brHidePart(s, src->ci.view.parts[k].name)) continue;
        app.R.heatDraws.push_back({{&src->ci.view, k, s.world}, h, obj});
      }
    }
    if (!(s.localPlayer && s.alive) || selfShadow) {
      const size_t w0 = app.R.worldModels.size();
      brWornDraws(app, *this, s, s.world, app.R.worldModels, app.worldBoxDraws);
      const mat4 bodyMove = prevW * glm::inverse(s.world);   // AIB1b: worn gear moves with the body (its joint's own sway left out)
      for (size_t i = w0; i < app.R.worldModels.size(); ++i) { app.R.worldModels[i].prevM = bodyMove * app.R.worldModels[i].m; app.R.worldModels[i].vel = 1; }
    }   // BR3: helmet / vest / backpack; never in your own first person (the corpse and the death camera keep it)
    if (s.alive && s.weapon >= 0 && !s.gunBody) {   // GM1: the gas-mask soldier's gun is part of its body
      const WeaponDef& d = *app.weaponDefs[s.weapon];
      if (d.useModel()) {
        mat4 W = s.world * s.pose.weapon, magW = W;
        const float gk = s.holdBody && d.gripPose.has ? d.gripPose.scale : 1.0f;   // WS1: the gun drawn larger in these soldiers' hands, about its grip socket
        const mat4 Sk = gk != 1 ? glm::translate(mat4(1), d.gripR) * glm::scale(mat4(1), vec3(gk)) * glm::translate(mat4(1), -d.gripR) : mat4(1);
        W = W * Sk; magW = W;
        {   // DF1: a Delta Force reload: the magazine leaves the gun in the left hand (reload 25 %) and goes back in (80 %)
          const float r = (clipKit(s.kit) || s.holdBody) && s.reload > 0 && s.reloadTotal > 0 ? 1.0f - s.reload / s.reloadTotal : 0.0f;
          const int lh = models[s.model]->semantic[CJ_LHAND];
          if (r > s.dfMagWin.x && r < s.dfMagWin.y && lh >= 0 && lh < (int)s.pose.delta.size()) {   // the span by its feed (gunbody.cpp: box 25-80 %, belt 35-65 %)
            if (!s.dfMagHeld) { s.dfMagK = glm::inverse(s.pose.delta[lh]) * s.pose.weapon; s.dfMagHeld = true; }
            magW = s.world * s.pose.delta[lh] * s.dfMagK * Sk;
          } else s.dfMagHeld = false;
        }
        const mat4 boltW = (clipKit(s.kit) || s.holdBody) && s.dfBoltBack > 0 ? W * glm::translate(mat4(1), vec3(0, 0, -s.dfBoltBack)) : W;   // DF1: the bolt drawn back / the pump slid back
        const mat4 pumpW = (clipKit(s.kit) || s.holdBody) && s.dfPumpBack > 0 ? W * glm::translate(mat4(1), vec3(0, 0, -s.dfPumpBack)) : W;
        const mat4 prevGun = s.velGunFrame == frame - 1 ? s.velGun : W; s.velGun = W; s.velGunFrame = frame;   // AIB1: TAA motion of the held gun (its parts move with W)
        for (int p = 0; p < 8; ++p) for (int k : d.worldParts()[p]) { app.R.worldModels.push_back({d.worldModel(), k, p == 2 ? magW : p == 1 ? boltW : p == 7 && d.action != "launcher" ? pumpW : W}); app.R.worldModels.back().prevM = prevGun; app.R.worldModels.back().vel = 1; }   // WPart (weapons.cpp): 1 bolt, 2 mag, 7 handle
        if (app.R.thermalWanted) {   // TH1: a gun warms with firing and cools over ~15 s
          const float gh = 0.18f + 0.6f * std::exp(-(float)(t - s.lastShot) / 15.0f);
          for (int p = 0; p < 8; ++p) for (int k : d.worldParts()[p]) app.R.heatDraws.push_back({{d.worldModel(), k, W}, gh, heatObj});   // TH2: shaded with its holder
        }
        const size_t o0 = app.R.worldModels.size();
        brSightDraws(app, d, s.ws, W, app.R.worldModels, app.worldBoxDraws);   // BR3: the fitted optic
        lootAttachDraws(app, d, s.ws, W, app.R.worldModels, app.worldBoxDraws);   // BR10: muzzle, magazine, recoil pad
        const mat4 gunMove = prevGun * glm::inverse(W);   // AIB1b: the optic and attachments move with the gun
        for (size_t i = o0; i < app.R.worldModels.size(); ++i) { app.R.worldModels[i].prevM = gunMove * app.R.worldModels[i].m; app.R.worldModels[i].vel = 1; }
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

static bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Game::camera(vec3& e, vec3& f, float& fov) {
  specCam = cameraOrbit(e, f, fov);
  if (specCam && (!finite3(e) || !finite3(f) || glm::dot(f, f) < 1e-6f)) {   // never a non-finite camera (it indexed the terrain grid out of range): the last good one
    static bool logged = false; if (!logged) { logged = true; logf("spectate: non-finite camera (%f %f %f), the last valid one kept", e.x, e.y, e.z); }
    if (finite3(specEye) && finite3(specFwd) && glm::dot(specFwd, specFwd) > 1e-6f) { e = specEye; f = specFwd; fov = 70; } else specCam = false;
    return specCam;
  }
  if (specCam) { specEye = e; specFwd = f; }
  return specCam;
}
bool Game::cameraOrbit(vec3& e, vec3& f, float& fov) {
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
  // a watchable soldier: alive, a teammate (a side swap or a round reset can leave a stale index), a finite position
  auto watchable = [&](int k) { return k >= 0 && k < (int)sold.size() && &sold[k] != L && sold[k].alive && finite3(sold[k].feet) && std::isfinite(sold[k].yaw); };
  int w = spectate;
  if (!watchable(w) || (sold[w].team != L->team && !(t - L->diedAt < 3.0 && w == L->killer && match.mode() != GameMode::SearchDestroy))) {
    w = -1;
    for (auto& s : sold) if (s.team == L->team && watchable(s.slot)) { w = s.slot; break; }   // Solo: none, so the killer or the body
    if (t - L->diedAt < 3.0 && watchable(L->killer) && match.mode() != GameMode::SearchDestroy) w = L->killer;   // SD1: never an enemy
    spectate = w;
  }
  if (w < 0 && !finite3(L->feet)) return false;   // nobody to watch and no body to orbit: the caller keeps the last valid camera
  vec3 target = w >= 0 ? sold[w].feet + vec3(0, 1.6f, 0) : L->feet + vec3(0, 1.0f, 0);
  float yaw = w >= 0 ? sold[w].yaw : (std::isfinite(L->yaw) ? L->yaw : 0.0f);
  vec3 back = vec3(std::sin(radians(yaw)), 0.45f, std::cos(radians(yaw))) * 3.2f;
  // the wall test starts outside the watched body (from its head it hit its own capsule and put the camera inside the
  // model: an invisible body); never closer than 1 m to the head: a wall that close lifts the camera over the shoulder
  const vec3 nb = glm::normalize(back); const float L0 = 0.6f;
  Physics::Hit h; vec3 cpos = target + back;
  if (Physics::castRayEx(target + nb * L0, nb, glm::length(back) - L0, h)) {
    float d = L0 + h.t - 0.2f;
    if (d >= 1.0f) cpos = target + nb * d;
    else { vec3 up = target + vec3(0, 1.4f, 0) + nb * 0.6f; Physics::Hit h2; cpos = Physics::castRayEx(target, glm::normalize(up - target), glm::length(up - target), h2) ? target + vec3(0, 1.0f, 0) + nb * 0.3f : up; }   // never straight above the head: a view along the up axis made lookAt NaN (the terrain-guard hits)
  }
  e = cpos; f = glm::normalize(target - cpos); fov = 70;
  return true;
}

// SND13: the local player's low-health heartbeat and hurt grunt, every frame (host and LAN client alike: from the
// player's health, so bullets, melee, blasts, fire, falls, drowning and the storm all count). The heartbeat loops under 30 %
// of 100 health (faster and up to +2 dB toward 10 %), stops on death, at respawn and outside a match; the grunt plays on
// each hit (the killing blow too: no death sound exists), at most one per 0.6 s unless a much bigger hit lands (0.2 s)
void playerAudioFrame(App& app) {
  static float lastHp = -1; static int lastLife = -1; static bool lastAlive = false; static double lastHurt = -10; static float lastAmt = 0;
  static std::mt19937 rng{4242}; std::uniform_real_distribution<float> u(-1, 1);
  Soldier* L = gGame && gGame->active && app.mode == Mode::Explore ? gGame->local() : nullptr;
  float level = 0, rate = 1;
  if (L && L->alive && settings().s.heartbeat) {
    const float f = L->hp / 100.0f;
    if (f < 0.30f) { const float k = std::clamp((0.30f - f) / 0.20f, 0.0f, 1.0f); level = 1.0f + 0.26f * k; rate = 1.0f + 0.10f * k; }
  }
  app.audio.setHeartbeat(level, rate);
  if (!L) { lastHp = -1; lastLife = -1; lastAlive = false; return; }
  const double now = glfwGetTime();
  if (L->lifeId != lastLife || lastHp < 0) { lastLife = L->lifeId; lastHp = L->hp; lastAlive = L->alive; return; }   // a new life: no grunt for the respawn
  const float drop = lastHp - L->hp;
  if (drop > 0.5f && (lastAlive || L->alive)) {
    const bool big = drop >= 25.0f || drop > 2.0f * lastAmt;
    if (now - lastHurt >= 0.6 || (big && now - lastHurt >= 0.2)) {
      app.audio.play("player_hurt", app.audio.listenerPos(), 1.0f + 0.15f * u(rng), 1.0f + 0.04f * u(rng)); ++app.audio.hurtPlays;
      lastHurt = now; lastAmt = drop;
    }
  }
  lastHp = L->hp; lastAlive = L->alive;
}
