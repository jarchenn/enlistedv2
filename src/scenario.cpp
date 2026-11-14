#include "scenario.h"
#include <algorithm>
#include <cmath>

vec3 attackForward(float yaw) { float y = radians(yaw); return vec3(-std::sin(y), 0, -std::cos(y)); }   // same convention as App::forward
vec3 attackRight(float yaw) { float y = radians(yaw); return vec3(std::cos(y), 0, -std::sin(y)); }

static const char* laneName(int l) { return l == LANE_LEFT ? "left" : l == LANE_RIGHT ? "right" : "main"; }
static int laneFrom(const std::string& s) { return s == "left" ? LANE_LEFT : s == "right" ? LANE_RIGHT : LANE_MAIN; }

static const char* kSlotNames[(int)SlotKind::Count] = {"position", "mg_nest", "trench_bay", "window", "foxhole", "embrasure"};
const char* slotKindName(SlotKind k) { int i = (int)k; return i >= 0 && i < (int)SlotKind::Count ? kSlotNames[i] : "position"; }
SlotKind slotKindFrom(const std::string& s) { for (int i = 0; i < (int)SlotKind::Count; ++i) if (s == kSlotNames[i]) return (SlotKind)i; return SlotKind::Position; }

// ---------------------------------------------------------------- objective shape
bool Objective::containsXZ(vec2 p) const {
  if (shape == CaptureShape::Polygon && polygon.size() >= 3) {
    bool in = false;   // even-odd ray crossing
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
      const vec2 &a = polygon[i], &b = polygon[j];
      if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
  }
  vec2 d(p.x - pos.x, p.y - pos.z);
  return glm::dot(d, d) <= radius * radius;
}
float Objective::extent() const {
  if (shape == CaptureShape::Polygon && polygon.size() >= 3) { float r = 0; for (auto& q : polygon) r = std::max(r, glm::length(q - vec2(pos.x, pos.z))); return r; }
  return radius;
}

float captureRateFor(const ScenarioRules& r, const Objective& o, int A, int D) {
  if (A <= 0) return 0.0f;
  if (r.captureModel == CaptureModel::Contest) {
    if (A <= D) return 0.0f;                               // tie or defender superiority freezes; no decay
    float advantage = float(A - D) / float(A + D);
    float presence = std::min(float(A) / std::max(r.capturePresenceFull, 1e-3f), 1.0f);
    return r.captureBase * advantage * presence;
  }
  if (D > 0) return 0.0f;                                  // [DECISION] any defender on the point pauses the capture
  float mult = std::min(float(A), std::max(1.0f, r.captureSpeedCap));
  return 100.0f / std::max(o.captureTime, 1.0f) * mult;
}

// ---------------------------------------------------------------- rules
json ScenarioRules::toJson() const {
  return {{"defenderSlots", defenderSlots}, {"attackerAI", attackerAI}, {"attackerRespawns", attackerRespawns},
          {"attackerWave_s", attackerWave}, {"defenderWave_s", defenderWave}, {"minRespawnDelay_s", minRespawnDelay},
          {"defendingFaction", factionName(defendingFaction)}, {"fragPerLife", fragPerLife},
          {"smokePerLifeAttacker", smokePerLifeAttacker}, {"smokePerLifeDefender", smokePerLifeDefender},
          {"captureModel", captureModel == CaptureModel::Contest ? "contest" : "timed"}, {"captureSpeedCap", captureSpeedCap},
          {"captureBase", captureBase}, {"capturePresenceFull", capturePresenceFull},
          {"lanes", {laneLeft, laneMain, laneRight}}, {"friendlyFire", friendlyFire},
          {"mode", mode == GameMode::Solo ? "solo" : mode == GameMode::TDM ? "tdm" : "assault"}, {"killCap", killCap},
          {"soloPlayers", soloPlayers}, {"teamPlayers", {teamPlayers[0], teamPlayers[1]}}, {"aiType", {(int)aiType[0], (int)aiType[1]}},
          {"humanTeam", humanTeam}, {"teamFaction", {factionName(teamFaction[0]), factionName(teamFaction[1])}}, {"respawnDelay_s", respawnDelay}, {"allowedClasses", allowedClasses}};
}

ScenarioRules ScenarioRules::fromJson(const json& j, int version) {
  ScenarioRules r;
  if (version < 2) { r.defendingFaction = Faction::Allies; r.captureModel = CaptureModel::Contest; }   // v1 behaviour
  if (!j.is_object()) return r;
  r.defenderSlots = j.value("defenderSlots", r.defenderSlots);
  r.attackerAI = j.value("attackerAI", r.attackerAI);
  r.attackerRespawns = j.value("attackerRespawns", r.attackerRespawns);
  r.attackerWave = j.value("attackerWave_s", r.attackerWave);
  r.defenderWave = j.value("defenderWave_s", r.defenderWave);
  r.minRespawnDelay = j.value("minRespawnDelay_s", r.minRespawnDelay);
  if (j.contains("defendingFaction")) r.defendingFaction = j.value("defendingFaction", std::string("Axis")) == "Allies" ? Faction::Allies : Faction::Axis;
  if (j.contains("captureModel")) r.captureModel = j.value("captureModel", std::string("timed")) == "contest" ? CaptureModel::Contest : CaptureModel::Timed;
  r.captureSpeedCap = j.value("captureSpeedCap", r.captureSpeedCap);
  r.fragPerLife = j.value("fragPerLife", r.fragPerLife);
  r.smokePerLifeAttacker = j.value("smokePerLifeAttacker", r.smokePerLifeAttacker);
  r.smokePerLifeDefender = j.value("smokePerLifeDefender", r.smokePerLifeDefender);
  r.captureBase = j.value("captureBase", r.captureBase);
  r.capturePresenceFull = j.value("capturePresenceFull", r.capturePresenceFull);
  if (j.contains("lanes") && j["lanes"].is_array() && j["lanes"].size() == 3) { r.laneLeft = j["lanes"][0]; r.laneMain = j["lanes"][1]; r.laneRight = j["lanes"][2]; }
  r.friendlyFire = j.value("friendlyFire", r.friendlyFire);
  { std::string m = j.value("mode", std::string("assault")); r.mode = m == "solo" ? GameMode::Solo : m == "tdm" ? GameMode::TDM : GameMode::Assault; }
  r.killCap = std::clamp(j.value("killCap", r.killCap), 1, 999);
  r.soloPlayers = std::clamp(j.value("soloPlayers", r.soloPlayers), 2, kMaxCombatants);
  if (j.contains("teamPlayers") && j["teamPlayers"].is_array() && j["teamPlayers"].size() == 2)
    for (int t = 0; t < 2; ++t) r.teamPlayers[t] = std::clamp(j["teamPlayers"][t].get<int>(), 1, kMaxCombatants / 2);
  if (j.contains("aiType") && j["aiType"].is_array() && j["aiType"].size() == 2)
    for (int t = 0; t < 2; ++t) r.aiType[t] = j["aiType"][t].get<int>() == 1 ? AiType::Zombies : AiType::Soldiers;
  r.humanTeam = std::clamp(j.value("humanTeam", r.humanTeam), 0, 1);
  if (j.contains("teamFaction") && j["teamFaction"].is_array() && j["teamFaction"].size() == 2)
    for (int t = 0; t < 2; ++t) r.teamFaction[t] = j["teamFaction"][t].get<std::string>() == "Axis" ? Faction::Axis : Faction::Allies;
  r.respawnDelay = std::clamp(j.value("respawnDelay_s", r.respawnDelay), 0.0f, 30.0f);
  if (j.contains("allowedClasses") && j["allowedClasses"].is_array()) for (auto& c : j["allowedClasses"]) if (c.is_string()) r.allowedClasses.push_back(c.get<std::string>());
  // clamp to the limits (setup UI enforces the same)
  r.defenderSlots = std::clamp(r.defenderSlots, 1, kMaxCombatants - 1);
  r.attackerAI = std::clamp(r.attackerAI, 1, kMaxCombatants - r.defenderSlots);
  r.attackerRespawns = std::clamp(r.attackerRespawns, 0, 99);
  r.fragPerLife = std::clamp(r.fragPerLife, 0, 9);
  r.smokePerLifeAttacker = std::clamp(r.smokePerLifeAttacker, 0, 9);
  r.smokePerLifeDefender = std::clamp(r.smokePerLifeDefender, 0, 9);
  r.captureSpeedCap = std::clamp(r.captureSpeedCap, 1.0f, 10.0f);
  return r;
}

// ---------------------------------------------------------------- scenario json
static json v2j(vec2 v) { return json::array({v.x, v.y}); }
static vec2 jv2(const json& j) { return j.is_array() && j.size() >= 2 ? vec2(j[0].get<float>(), j[1].get<float>()) : vec2(0); }

json Scenario::toJson() const {
  json o = json::array();
  for (auto& b : objectives) {
    json routes = json::array();
    for (auto& r : b.routes) { json pts = json::array(); for (auto& p : r.points) pts.push_back(tojson(p)); routes.push_back({{"lane", laneName(r.lane)}, {"points", pts}}); }
    json slots = json::array();
    for (auto& d : b.defenseSlots) slots.push_back({{"pos", tojson(d.pos)}, {"yaw", d.yaw}, {"kind", slotKindName(d.kind)}, {"priority", d.priority}});
    json poly = json::array(); for (auto& q : b.polygon) poly.push_back(v2j(q));
    json linked = json::array(); for (auto id : b.linkedStructures) linked.push_back(id);
    json shape = b.shape == CaptureShape::Polygon ? json{{"type", "polygon"}, {"points", poly}} : json{{"type", "circle"}, {"radius", b.radius}};
    o.push_back({{"id", b.id}, {"label", std::string(1, b.letter())}, {"displayName", b.label}, {"order", b.order},
                 {"pos", tojson(b.pos)}, {"yaw", b.yaw}, {"captureShape", shape}, {"radius", b.radius},
                 {"yMin", b.yMin}, {"yMax", b.yMax}, {"captureTime_s", b.captureTime}, {"attackYaw", b.attackYaw},
                 {"routes", routes}, {"linkedStructures", linked}, {"defenseSlots", slots}});
  }
  json sp = json::array();
  for (auto& s : spawns)
    sp.push_back({{"id", s.id}, {"label", s.label}, {"role", s.role == CombatRole::Attacker ? "attacker" : "defender"},
                  {"phases", s.phases}, {"pos", tojson(s.pos)}, {"yaw", s.yaw}, {"rack", s.rack}, {"team", s.team}});
  return {{"version", version}, {"objectives", o}, {"spawns", sp}, {"rules", rules.toJson()}, {"devTestMap", devTestMap}, {"nextSerial", nextSerial}};
}

Scenario Scenario::fromJson(const json& j, std::vector<std::string>* notes) {
  Scenario s;
  if (!j.is_object()) { if (notes) notes->push_back("no scenario in this map: empty scenario with default rules"); return s; }
  int v = j.value("version", 0);
  if (v < 1 && notes) notes->push_back("scenario version 0 -> 1: default radius 15 m, vertical bounds -2..+4 m, lanes 20/60/20");
  if (v < 2 && notes) notes->push_back("scenario version " + std::to_string(v) + " -> 2: keeps its capture rule (contest formula), defending side and radii; "
                                       "labels become display names (letters A-J follow the order); defender positions become defense slots");
  if (v > kScenarioVersion && notes) notes->push_back("scenario written by a newer build (version " + std::to_string(v) + "): unknown fields ignored");
  s.version = kScenarioVersion;
  s.devTestMap = j.value("devTestMap", false);
  s.nextSerial = j.value("nextSerial", (uint64_t)1);
  if (j.contains("objectives")) for (auto& o : j["objectives"]) {
    Objective b;
    b.id = o.value("id", ""); b.order = o.value("order", (int)s.objectives.size());
    b.label = v >= 2 ? o.value("displayName", "") : o.value("label", "");
    b.pos = jvec3(o.value("pos", json())); b.yaw = o.value("yaw", 0.0f);
    b.radius = std::clamp(o.value("radius", v >= 2 ? 25.0f : 15.0f), 2.0f, 200.0f);
    if (o.contains("captureShape") && o["captureShape"].is_object()) {
      auto& cs = o["captureShape"];
      if (cs.value("type", "circle") == "polygon" && cs.contains("points")) {
        for (auto& q : cs["points"]) b.polygon.push_back(jv2(q));
        if (b.polygon.size() >= 3) b.shape = CaptureShape::Polygon;
      } else if (cs.contains("radius")) b.radius = std::clamp(cs.value("radius", b.radius), 2.0f, 200.0f);
    }
    b.yMin = o.value("yMin", -2.0f); b.yMax = o.value("yMax", 4.0f);
    b.captureTime = std::clamp(o.value("captureTime_s", 45.0f), 1.0f, 600.0f);
    b.attackYaw = o.value("attackYaw", 0.0f);
    if (o.contains("routes")) for (auto& r : o["routes"]) { ApproachRoute ar; ar.lane = laneFrom(r.value("lane", "main")); if (r.contains("points")) for (auto& p : r["points"]) ar.points.push_back(jvec3(p)); b.routes.push_back(ar); }
    if (o.contains("linkedStructures")) for (auto& id : o["linkedStructures"]) if (id.is_number_unsigned() || id.is_number_integer()) b.linkedStructures.push_back(id.get<uint64_t>());
    if (o.contains("defenseSlots")) for (auto& d : o["defenseSlots"]) {
      DefenseSlot ds; ds.pos = jvec3(d.value("pos", json())); ds.yaw = d.value("yaw", 0.0f);
      ds.kind = slotKindFrom(d.value("kind", "position")); ds.priority = d.value("priority", 1);
      b.defenseSlots.push_back(ds);
    }
    if (o.contains("defendPoints")) for (auto& p : o["defendPoints"]) { DefenseSlot ds; ds.pos = jvec3(p); ds.yaw = b.attackYaw + 180.0f; b.defenseSlots.push_back(ds); }   // v1
    if (b.id.empty()) b.id = s.newId("obj");
    s.objectives.push_back(b);
  }
  if (j.contains("spawns")) for (auto& o : j["spawns"]) {
    SpawnMarker m;
    m.id = o.value("id", ""); m.label = o.value("label", "");
    m.role = o.value("role", std::string("attacker")) == "defender" ? CombatRole::Defender : CombatRole::Attacker;
    m.phases.clear();
    if (o.contains("phases") && o["phases"].is_array()) for (auto& p : o["phases"]) m.phases.push_back(p.get<int>());
    else m.phases.push_back(o.value("stage", 0));   // v1: one stage
    std::sort(m.phases.begin(), m.phases.end()); m.phases.erase(std::unique(m.phases.begin(), m.phases.end()), m.phases.end());
    m.pos = jvec3(o.value("pos", json())); m.yaw = o.value("yaw", 0.0f);
    m.rack = o.value("rack", (uint64_t)0);
    m.team = std::clamp(o.value("team", -1), -1, 1);
    if (m.team >= 0) m.phases.clear();   // TDM markers serve no Assault phase
    if (m.id.empty()) m.id = s.newId("spawn");
    s.spawns.push_back(m);
  }
  s.rules = ScenarioRules::fromJson(j.value("rules", json()), v);
  s.sortByOrder();
  return s;
}

// ---------------------------------------------------------------- ordering
void Scenario::sortByOrder() { std::stable_sort(objectives.begin(), objectives.end(), [](const Objective& a, const Objective& b) { return a.order < b.order; }); }
void Scenario::renumber() {
  sortByOrder();
  std::vector<int> remap;   // old order -> new order (spawn phases follow)
  for (auto& o : objectives) { if (o.order >= (int)remap.size()) remap.resize(o.order + 1, -1); }
  for (size_t k = 0; k < objectives.size(); ++k) if (objectives[k].order >= 0) remap[objectives[k].order] = (int)k;
  bool same = true; for (size_t k = 0; k < remap.size(); ++k) if (remap[k] != (int)k && remap[k] != -1) same = false;
  if (!same) for (auto& m : spawns) for (auto& p : m.phases) if (p >= 0 && p < (int)remap.size() && remap[p] >= 0) p = remap[p];
  for (size_t k = 0; k < objectives.size(); ++k) objectives[k].order = (int)k;
}
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

// Move keeps stable ids; spawn markers follow their objective (their phase indices move with it).
bool Scenario::moveTo(const std::string& id, int to) {
  renumber();
  int from = -1;
  for (size_t k = 0; k < objectives.size(); ++k) if (objectives[k].id == id) from = (int)k;
  if (from < 0) return false;
  to = std::clamp(to, 0, (int)objectives.size() - 1);
  if (to == from) return false;
  std::vector<int> perm(objectives.size());   // old index -> new index
  std::vector<int> seq; for (int k = 0; k < (int)objectives.size(); ++k) if (k != from) seq.push_back(k);
  seq.insert(seq.begin() + to, from);
  for (int n = 0; n < (int)seq.size(); ++n) perm[seq[n]] = n;
  for (auto& m : spawns) { for (auto& p : m.phases) if (p >= 0 && p < (int)perm.size()) p = perm[p]; std::sort(m.phases.begin(), m.phases.end()); }
  for (int k = 0; k < (int)objectives.size(); ++k) objectives[k].order = perm[k];
  sortByOrder();
  return true;
}
bool Scenario::moveEarlier(const std::string& id) { const Objective* o = find(id); return o && o->order > 0 && moveTo(id, o->order - 1); }
bool Scenario::moveLater(const std::string& id) { renumber(); const Objective* o = find(id); return o && moveTo(id, o->order + 1); }

bool Scenario::removeObjective(const std::string& id, std::vector<uint64_t>* orphanRacks) {
  renumber();
  int ord = -1; for (auto& o : objectives) if (o.id == id) ord = o.order;
  if (ord < 0) return false;
  for (auto& m : spawns) {
    m.phases.erase(std::remove(m.phases.begin(), m.phases.end(), ord), m.phases.end());
    for (auto& p : m.phases) if (p > ord) --p;
  }
  for (auto& m : spawns) if (m.phases.empty() && m.rack && orphanRacks) orphanRacks->push_back(m.rack);
  spawns.erase(std::remove_if(spawns.begin(), spawns.end(), [](const SpawnMarker& m) { return m.phases.empty(); }), spawns.end());
  objectives.erase(std::remove_if(objectives.begin(), objectives.end(), [&](const Objective& o) { return o.id == id; }), objectives.end());
  for (auto& o : objectives) if (o.order > ord) --o.order;
  return true;
}

std::vector<const SpawnMarker*> Scenario::spawnsFor(CombatRole r, int phase) const {
  std::vector<const SpawnMarker*> out;
  for (auto& m : spawns) if (m.role == r && m.activeIn(phase)) out.push_back(&m);
  return out;
}

vec2 Scenario::attackerStart() const {
  vec2 c(0); int n = 0;
  for (auto& m : spawns) if (m.role == CombatRole::Attacker && m.activeIn(0)) { c += vec2(m.pos.x, m.pos.z); ++n; }
  if (n) return c / float(n);
  if (objectives.empty()) return vec2(0);
  const Objective& o = objectives.front(); vec3 f = attackForward(o.attackYaw);
  return vec2(o.pos.x, o.pos.z) - vec2(f.x, f.z) * 100.0f;
}
vec2 Scenario::attackAxis() const {
  if (objectives.empty()) return vec2(0, -1);
  vec2 st = attackerStart(), end(objectives.back().pos.x, objectives.back().pos.z);
  vec2 d = end - st;
  if (glm::dot(d, d) < 1e-4f) { vec3 f = attackForward(objectives.front().attackYaw); return glm::normalize(vec2(f.x, f.z)); }
  return glm::normalize(d);
}

// ---------------------------------------------------------------- validation (SPEC 1.3 / 2.1)
std::vector<ScenarioIssue> validateScenario(const Scenario& s, bool forPublish) {
  (void)forPublish;
  std::vector<ScenarioIssue> out;
  auto err = [&](const std::string& w) { out.push_back({true, w}); };
  auto warn = [&](const std::string& w) { out.push_back({false, w}); };
  size_t n = s.objectives.size();
  auto& r0 = s.rules;
  if (r0.mode != GameMode::Assault) {   // G1: Solo / TDM need no objectives; spawns are team markers or found by the game
    std::set<std::string> sids;
    for (auto& m : s.spawns) if (!sids.insert(m.id).second) err("Duplicate spawn id '" + m.id + "'.");
    if (r0.mode == GameMode::TDM) for (int t = 0; t < 2; ++t) {
      bool any = false; for (auto& m : s.spawns) any |= m.team == t;
      if (!any) warn(std::string("Team Deathmatch: no spawn markers for team ") + (t == 0 ? "A" : "B") + " (Scenario panel): its soldiers spawn far from the enemy instead.");
    }
    int total = r0.mode == GameMode::Solo ? r0.soloPlayers : r0.teamPlayers[0] + r0.teamPlayers[1];
    if (total > ScenarioRules::kMaxCombatants) err("Roster exceeds " + std::to_string(ScenarioRules::kMaxCombatants) + " combatants.");
    return out;
  }
  if (n == 0) err("No objectives: place 1 to 10 capture points (Scenario panel, Add objective).");
  if ((int)n > kMaxObjectives) err(std::to_string(n) + " objectives: a map holds at most " + std::to_string(kMaxObjectives) + " (A to J).");
  std::set<int> orders; std::set<std::string> ids;
  for (auto& o : s.objectives) {
    std::string nm = std::string(1, o.letter()) + " '" + o.displayName() + "'";
    if (!ids.insert(o.id).second) err("Duplicate objective id '" + o.id + "'.");
    if (!orders.insert(o.order).second) err("Two objectives share sequence " + std::to_string(o.order + 1) + ".");
    if (o.yMax <= o.yMin) err("Objective " + nm + ": vertical bounds are empty (top must be above bottom).");
    if (o.shape == CaptureShape::Circle && o.radius < 2) err("Objective " + nm + ": radius below 2 m.");
    if (o.shape == CaptureShape::Polygon && o.polygon.size() < 3) err("Objective " + nm + ": a polygon capture area needs at least 3 points.");
  }
  for (size_t k = 0; k < n; ++k) if (!orders.count((int)k)) { err("Objective sequence is not contiguous (missing " + std::to_string(k + 1) + ")."); break; }
  vec2 start = s.attackerStart(), axis = s.attackAxis();
  for (size_t k = 0; k < n; ++k) {
    auto& o = s.objectives[k];
    std::string nm = std::string(1, o.letter()) + " '" + o.displayName() + "'";
    auto atk = s.spawnsFor(CombatRole::Attacker, o.order), def = s.spawnsFor(CombatRole::Defender, o.order);
    if (atk.empty()) err("Phase " + nm + " has no attacker spawn.");
    if (def.empty()) err("Phase " + nm + " has no defender spawn.");
    vec2 op(o.pos.x, o.pos.z);
    if (k > 0) {
      auto& p = s.objectives[k - 1]; vec2 pp(p.pos.x, p.pos.z);
      float d = glm::length(op - pp);
      if (d < 150.0f) { char b[200]; std::snprintf(b, sizeof b, "Objective %s is %.0f m from the previous one (recommended at least 150 m).", nm.c_str(), d); warn(b); }
      if (glm::dot(op - start, axis) <= glm::dot(pp - start, axis)) warn("Objective " + nm + " is not further along the attack axis than " + std::string(1, p.letter()) + ".");
    }
    for (auto* m : atk) { float d = glm::length(vec2(m->pos.x, m->pos.z) - op); if (d < 150.0f) { char b[220]; std::snprintf(b, sizeof b, "Attacker spawn '%s' is %.0f m from its objective %c (recommended at least 150 m).", m->label.c_str(), d, o.letter()); warn(b); } }
    for (auto* m : def) {
      vec2 mp(m->pos.x, m->pos.z);
      float d = glm::length(mp - op);
      if (d < 80.0f) { char b[220]; std::snprintf(b, sizeof b, "Defender spawn '%s' is %.0f m from its objective %c (recommended at least 80 m).", m->label.c_str(), d, o.letter()); warn(b); }
      for (auto* a : atk) { float da = glm::length(mp - vec2(a->pos.x, a->pos.z)); if (da < 200.0f) { char b[240]; std::snprintf(b, sizeof b, "Defender spawn '%s' is %.0f m from attacker spawn '%s' in phase %c (recommended at least 200 m).", m->label.c_str(), da, a->label.c_str(), o.letter()); warn(b); } }
    }
  }
  std::set<std::string> sids;
  for (auto& m : s.spawns) {
    if (!sids.insert(m.id).second) err("Duplicate spawn id '" + m.id + "'.");
    if (m.team >= 0) continue;   // a TDM marker: no Assault phase
    if (m.phases.empty()) err("Spawn '" + (m.label.empty() ? m.id : m.label) + "' is not active in any phase.");
    for (int p : m.phases) if (p < 0 || p >= (int)std::max<size_t>(n, 1)) { err("Spawn '" + (m.label.empty() ? m.id : m.label) + "' refers to phase " + std::to_string(p + 1) + ", which has no objective."); break; }
  }
  for (size_t a = 0; a < s.spawns.size(); ++a)
    for (size_t b = a + 1; b < s.spawns.size(); ++b)
      if (glm::length(s.spawns[a].pos - s.spawns[b].pos) < 0.7f) warn("Spawn markers '" + s.spawns[a].id + "' and '" + s.spawns[b].id + "' overlap (closer than 0.7 m): only one soldier fits there at a time.");
  auto& r = s.rules;
  if (r.defenderSlots + r.attackerAI > ScenarioRules::kMaxCombatants) err("Roster exceeds " + std::to_string(ScenarioRules::kMaxCombatants) + " combatants (" + std::to_string(r.defenderSlots + r.attackerAI) + ").");
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
