#include "scenario.h"
#include <algorithm>
#include <cmath>

vec3 attackForward(float yaw) { float y = radians(yaw); return vec3(-std::sin(y), 0, -std::cos(y)); }   // same convention as App::forward
vec3 attackRight(float yaw) { float y = radians(yaw); return vec3(std::cos(y), 0, -std::sin(y)); }

static const char* laneName(int l) { return l == LANE_LEFT ? "left" : l == LANE_RIGHT ? "right" : "main"; }
static int laneFrom(const std::string& s) { return s == "left" ? LANE_LEFT : s == "right" ? LANE_RIGHT : LANE_MAIN; }

json ScenarioRules::toJson() const {
  return {{"defenderSlots", defenderSlots}, {"attackerAI", attackerAI}, {"attackerRespawns", attackerRespawns},
          {"attackerWave_s", attackerWave}, {"defenderWave_s", defenderWave}, {"minRespawnDelay_s", minRespawnDelay},
          {"defendingFaction", factionName(defendingFaction)}, {"fragPerLife", fragPerLife},
          {"smokePerLifeAttacker", smokePerLifeAttacker}, {"smokePerLifeDefender", smokePerLifeDefender},
          {"captureBase", captureBase}, {"capturePresenceFull", capturePresenceFull},
          {"lanes", {laneLeft, laneMain, laneRight}}, {"friendlyFire", friendlyFire}};
}

ScenarioRules ScenarioRules::fromJson(const json& j) {
  ScenarioRules r;
  if (!j.is_object()) return r;
  r.defenderSlots = j.value("defenderSlots", r.defenderSlots);
  r.attackerAI = j.value("attackerAI", r.attackerAI);
  r.attackerRespawns = j.value("attackerRespawns", r.attackerRespawns);
  r.attackerWave = j.value("attackerWave_s", r.attackerWave);
  r.defenderWave = j.value("defenderWave_s", r.defenderWave);
  r.minRespawnDelay = j.value("minRespawnDelay_s", r.minRespawnDelay);
  r.defendingFaction = j.value("defendingFaction", std::string("Allies")) == "Axis" ? Faction::Axis : Faction::Allies;
  r.fragPerLife = j.value("fragPerLife", r.fragPerLife);
  r.smokePerLifeAttacker = j.value("smokePerLifeAttacker", r.smokePerLifeAttacker);
  r.smokePerLifeDefender = j.value("smokePerLifeDefender", r.smokePerLifeDefender);
  r.captureBase = j.value("captureBase", r.captureBase);
  r.capturePresenceFull = j.value("capturePresenceFull", r.capturePresenceFull);
  if (j.contains("lanes") && j["lanes"].is_array() && j["lanes"].size() == 3) { r.laneLeft = j["lanes"][0]; r.laneMain = j["lanes"][1]; r.laneRight = j["lanes"][2]; }
  r.friendlyFire = j.value("friendlyFire", r.friendlyFire);
  // clamp to the milestone limits (setup UI enforces the same)
  r.defenderSlots = std::clamp(r.defenderSlots, 1, kMaxCombatants - 1);
  r.attackerAI = std::clamp(r.attackerAI, 1, kMaxCombatants - r.defenderSlots);
  r.attackerRespawns = std::clamp(r.attackerRespawns, 0, 99);
  r.fragPerLife = std::clamp(r.fragPerLife, 0, 9);
  r.smokePerLifeAttacker = std::clamp(r.smokePerLifeAttacker, 0, 9);
  r.smokePerLifeDefender = std::clamp(r.smokePerLifeDefender, 0, 9);
  return r;
}

json Scenario::toJson() const {
  json o = json::array();
  for (auto& b : objectives) {
    json routes = json::array();
    for (auto& r : b.routes) { json pts = json::array(); for (auto& p : r.points) pts.push_back(tojson(p)); routes.push_back({{"lane", laneName(r.lane)}, {"points", pts}}); }
    json dp = json::array(); for (auto& p : b.defendPoints) dp.push_back(tojson(p));
    o.push_back({{"id", b.id}, {"label", b.label}, {"order", b.order}, {"pos", tojson(b.pos)}, {"yaw", b.yaw}, {"radius", b.radius},
                 {"yMin", b.yMin}, {"yMax", b.yMax}, {"attackYaw", b.attackYaw}, {"routes", routes}, {"defendPoints", dp}});
  }
  json sp = json::array();
  for (auto& s : spawns) sp.push_back({{"id", s.id}, {"label", s.label}, {"role", s.role == CombatRole::Attacker ? "attacker" : "defender"}, {"stage", s.stage}, {"pos", tojson(s.pos)}, {"yaw", s.yaw}});
  return {{"version", version}, {"objectives", o}, {"spawns", sp}, {"rules", rules.toJson()}, {"devTestMap", devTestMap}, {"nextSerial", nextSerial}};
}

Scenario Scenario::fromJson(const json& j, std::vector<std::string>* notes) {
  Scenario s;
  if (!j.is_object()) { if (notes) notes->push_back("no scenario in this map: empty scenario with default rules"); return s; }
  int v = j.value("version", 0);
  if (v < 1 && notes) notes->push_back("scenario version 0 -> 1: default radius 15 m, vertical bounds -2..+4 m, lanes 20/60/20");
  if (v > kScenarioVersion && notes) notes->push_back("scenario written by a newer build (version " + std::to_string(v) + "): unknown fields ignored");
  s.version = kScenarioVersion;
  s.devTestMap = j.value("devTestMap", false);
  s.nextSerial = j.value("nextSerial", (uint64_t)1);
  if (j.contains("objectives")) for (auto& o : j["objectives"]) {
    Objective b;
    b.id = o.value("id", ""); b.label = o.value("label", ""); b.order = o.value("order", (int)s.objectives.size());
    b.pos = jvec3(o.value("pos", json())); b.yaw = o.value("yaw", 0.0f);
    b.radius = std::clamp(o.value("radius", 15.0f), 2.0f, 60.0f);
    b.yMin = o.value("yMin", -2.0f); b.yMax = o.value("yMax", 4.0f);
    b.attackYaw = o.value("attackYaw", 0.0f);
    if (o.contains("routes")) for (auto& r : o["routes"]) { ApproachRoute ar; ar.lane = laneFrom(r.value("lane", "main")); if (r.contains("points")) for (auto& p : r["points"]) ar.points.push_back(jvec3(p)); b.routes.push_back(ar); }
    if (o.contains("defendPoints")) for (auto& p : o["defendPoints"]) b.defendPoints.push_back(jvec3(p));
    if (b.id.empty()) b.id = s.newId("obj");
    s.objectives.push_back(b);
  }
  if (j.contains("spawns")) for (auto& o : j["spawns"]) {
    SpawnMarker m;
    m.id = o.value("id", ""); m.label = o.value("label", "");
    m.role = o.value("role", std::string("attacker")) == "defender" ? CombatRole::Defender : CombatRole::Attacker;
    m.stage = o.value("stage", 0); m.pos = jvec3(o.value("pos", json())); m.yaw = o.value("yaw", 0.0f);
    if (m.id.empty()) m.id = s.newId("spawn");
    s.spawns.push_back(m);
  }
  s.rules = ScenarioRules::fromJson(j.value("rules", json()));
  s.sortByOrder();
  return s;
}

void Scenario::sortByOrder() { std::stable_sort(objectives.begin(), objectives.end(), [](const Objective& a, const Objective& b) { return a.order < b.order; }); }
void Scenario::renumber() { sortByOrder(); for (size_t k = 0; k < objectives.size(); ++k) objectives[k].order = (int)k; }
Objective* Scenario::find(const std::string& id) { for (auto& o : objectives) if (o.id == id) return &o; return nullptr; }
const Objective* Scenario::find(const std::string& id) const { for (auto& o : objectives) if (o.id == id) return &o; return nullptr; }
std::string Scenario::newId(const char* prefix) {
  for (;;) {
    std::string id = std::string(prefix) + "_" + std::to_string(nextSerial++);
    bool used = find(id) != nullptr;
    for (auto& m : spawns) if (m.id == id) used = true;
    if (!used) return id;
  }
}

// Reorder keeps stable ids; spawn markers follow their objective (their stage index moves with it).
static bool swapOrder(Scenario& s, size_t a, size_t b) {
  if (a >= s.objectives.size() || b >= s.objectives.size()) return false;
  int oa = s.objectives[a].order, ob = s.objectives[b].order;
  for (auto& m : s.spawns) { if (m.stage == oa) m.stage = -1000 - ob; else if (m.stage == ob) m.stage = -1000 - oa; }
  for (auto& m : s.spawns) if (m.stage <= -1000) m.stage = -1000 - m.stage;
  std::swap(s.objectives[a].order, s.objectives[b].order);
  s.sortByOrder();
  return true;
}
bool Scenario::moveEarlier(const std::string& id) {
  renumber();
  for (size_t k = 0; k < objectives.size(); ++k) if (objectives[k].id == id) return k > 0 && swapOrder(*this, k, k - 1);
  return false;
}
bool Scenario::moveLater(const std::string& id) {
  renumber();
  for (size_t k = 0; k < objectives.size(); ++k) if (objectives[k].id == id) return k + 1 < objectives.size() && swapOrder(*this, k, k + 1);
  return false;
}

std::vector<const SpawnMarker*> Scenario::spawnsFor(CombatRole r, int stage) const {
  std::vector<const SpawnMarker*> out;
  for (auto& m : spawns) if (m.role == r && m.stage == stage) out.push_back(&m);
  return out;
}

std::vector<ScenarioIssue> validateScenario(const Scenario& s, bool forPublish) {
  std::vector<ScenarioIssue> out;
  auto err = [&](const std::string& w) { out.push_back({true, w}); };
  auto warn = [&](const std::string& w) { out.push_back({false, w}); };
  size_t n = s.objectives.size();
  if (n == 0) err("No objectives: place 3 to 5 capture points (Scenario panel, Add objective).");
  else if (n == 1 && !s.devTestMap) err("Only one objective: published maps need 3 to 5. Tick 'developer test map' to play a one-point test.");
  else if (n < 3 && !s.devTestMap) { std::string m_ = "Only " + std::to_string(n) + " objectives: published maps need 3 to 5."; if (forPublish) err(m_); else warn(m_); }
  if (n > 5) { std::string m_ = std::to_string(n) + " objectives: published maps allow at most 5."; if (forPublish) err(m_); else warn(m_); }
  std::set<int> orders; std::set<std::string> ids;
  for (auto& o : s.objectives) {
    if (!ids.insert(o.id).second) err("Duplicate objective id '" + o.id + "'.");
    if (!orders.insert(o.order).second) err("Two objectives share sequence " + std::to_string(o.order + 1) + ".");
    if (o.yMax <= o.yMin) err("Objective '" + o.label + "': vertical bounds are empty (top must be above bottom).");
    if (o.radius < 2) err("Objective '" + o.label + "': radius below 2 m.");
  }
  for (size_t k = 0; k < n; ++k) if (!orders.count((int)k)) { err("Objective sequence is not contiguous (missing " + std::to_string(k + 1) + ")."); break; }
  for (size_t k = 0; k < n; ++k) {
    auto& o = s.objectives[k];
    std::string nm = "'" + (o.label.empty() ? o.id : o.label) + "' (" + std::to_string(o.order + 1) + ")";
    if (s.spawnsFor(CombatRole::Attacker, o.order).empty()) err("Objective " + nm + " has no attacker spawn markers for its stage.");
    if (s.spawnsFor(CombatRole::Defender, o.order).empty()) err("Objective " + nm + " has no defender spawn markers for its stage.");
    bool lanes[3] = {false, false, false};
    for (auto& r : o.routes) if (!r.points.empty()) lanes[r.lane] = true;
    if (!lanes[LANE_LEFT] || !lanes[LANE_RIGHT]) warn("Objective " + nm + ": no " + std::string(!lanes[LANE_LEFT] ? "left" : "right") + " approach route; flank bots will use generated detours.");
  }
  std::set<std::string> sids;
  for (auto& m : s.spawns) {
    if (!sids.insert(m.id).second) err("Duplicate spawn id '" + m.id + "'.");
    if (m.stage < 0 || m.stage >= (int)std::max<size_t>(n, 1)) err("Spawn '" + (m.label.empty() ? m.id : m.label) + "' refers to stage " + std::to_string(m.stage + 1) + ", which has no objective.");
  }
  for (size_t a = 0; a < s.spawns.size(); ++a)
    for (size_t b = a + 1; b < s.spawns.size(); ++b)
      if (glm::length(s.spawns[a].pos - s.spawns[b].pos) < 0.7f) warn("Spawn markers '" + s.spawns[a].id + "' and '" + s.spawns[b].id + "' overlap (closer than 0.7 m): only one soldier fits there at a time.");
  auto& r = s.rules;
  if (r.defenderSlots + r.attackerAI > ScenarioRules::kMaxCombatants) err("Roster exceeds 20 combatants (" + std::to_string(r.defenderSlots + r.attackerAI) + ").");
  if (r.attackerAI < 1) err("At least one attacking AI is required.");
  return out;
}

std::vector<int> allocateLanes(int n, float l, float m, float r, int minMain) {
  std::vector<int> out(3, 0);
  if (n <= 0) return out;
  float w[3] = {std::max(0.0f, l), std::max(0.0f, m), std::max(0.0f, r)};
  float sum = w[0] + w[1] + w[2];
  if (sum <= 1e-6f) { w[0] = 0; w[1] = 1; w[2] = 0; sum = 1; }
  float q[3]; int used = 0;
  for (int k = 0; k < 3; ++k) { q[k] = n * w[k] / sum; out[k] = (int)std::floor(q[k] + 1e-6f); used += out[k]; }
  // largest remainder; ties broken main, left, right (deterministic)
  int prio[3] = {1, 0, 2};
  while (used < n) {
    int best = -1; float br = -1;
    for (int pk : prio) { float rem = q[pk] - out[pk]; if (rem > br + 1e-6f) { br = rem; best = pk; } }
    out[best]++; used++;
  }
  minMain = std::min(minMain, n);
  while (out[1] < minMain) { int take = out[0] >= out[2] ? 0 : 2; if (out[take] == 0) take = 2 - take; if (out[take] == 0) break; out[take]--; out[1]++; }
  return out;
}
