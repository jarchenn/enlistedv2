// W6b AI tuning harness (AI_SPEC 12.2-12.5). Stages 2900+. Usage and outputs: ai_harness.h; thresholds:
// content/ai/test_thresholds.json; the whole set on a list of maps: tools/run_ai_harness.sh.
//   course   AI_SPEC 12.2: bots cross the obstacle course and clear the building and bunker; fails on any unrecovered
//            stuck event (every event is listed with map, position, duration and the ladder step that resolved it)
//   rebuild  AI_SPEC 12.4: a wire belt, a bunker and a wall dropped across moving bots' routes (any map)
//   soak     AI_SPEC 12.3 / 12.5: N bots for M minutes (matches restart until the time is used), stuck events per bot
//            per minute, advance speed, exposure under fire, spacing, grenades, deaths per minute, objective fall
//            times, frame / AI / match-step time; compared with the thresholds and the saved baseline
// The harness only reads the AI (AiCore, AiMetrics, StuckEvent); it never changes AI behaviour.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "ai_core.h"
#include "ai_harness.h"
#include "scenario.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>

namespace {
struct Opts {
  std::string suite = "soak"; std::vector<std::string> maps;
  float minutes = 20; int bots = -1, attackers = -1, respawns = -1;
  std::string difficulty; uint32_t seed = 1944; float size = 560; int objectives = 3, structures = -1;
  std::string thresholds, baseline, saveBaseline, tag; bool saveMap = false, fullThink = false;
  int stepsPerFrame = 1;   // soak only: simulation steps per rendered frame (the GPU is shared; rendering is not part of the AI)
} O;

const char* kStepName[7] = {"none", "re-plan", "step back and re-plan", "nearby link", "blocking edge expensive for this bot",
                            "different goal", "snap to navmesh (unseen)"};

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == ',' || c == ' ') { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; }
  if (!cur.empty()) v.push_back(cur);
  return v;
}
std::string sanitize(std::string s) { for (char& c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_'; return s; }
std::string num(double v) { char b[32]; if (v == std::floor(v) && std::fabs(v) < 1e9) { std::snprintf(b, sizeof b, "%.0f", v); return b; } std::snprintf(b, sizeof b, std::fabs(v) >= 100 ? "%.0f" : std::fabs(v) >= 10 ? "%.1f" : "%.3g", v); return b; }
json stats(std::vector<double> v) {
  if (v.empty()) return {{"n", 0}, {"mean", 0.0}, {"p50", 0.0}, {"p95", 0.0}, {"p99", 0.0}, {"max", 0.0}};
  std::sort(v.begin(), v.end()); double s = 0; for (double x : v) s += x;
  auto p = [&](double q) { return v[std::min(v.size() - 1, (size_t)(q * v.size()))]; };
  return {{"n", v.size()}, {"mean", s / v.size()}, {"p50", p(0.5)}, {"p95", p(0.95)}, {"p99", p(0.99)}, {"max", v.back()}};
}
double meanOf(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0.0 : s / v.size(); }
fs::path outDirH() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }

// ---------------------------------------------------------------- thresholds
json loadThresholds(std::string& err) {
  fs::path p = O.thresholds.empty() ? contentDir() / "ai" / "test_thresholds.json" : fs::path(O.thresholds);
  try { return json::parse(readTextFile(p)); } catch (const std::exception& e) { err = "thresholds " + p.string() + ": " + e.what(); return json(); }
}
json rulesFor(const json& TH, const std::string& suite, const std::string& tag) {
  json r = TH.value(suite, json::object());
  if (TH.contains("maps") && TH["maps"].contains(tag) && TH["maps"][tag].contains(suite))
    for (auto& [k, v] : TH["maps"][tag][suite].items()) r[k] = v;
  return r;
}
const json* at(const json& j, const std::string& path) {
  const json* p = &j; size_t s = 0;
  while (true) {
    size_t d = path.find('.', s); std::string key = path.substr(s, d == std::string::npos ? std::string::npos : d - s);
    if (!p->is_object() || !p->contains(key)) return nullptr;
    p = &(*p)[key];
    if (d == std::string::npos) return p;
    s = d + 1;
  }
}
struct Verdict { std::string key, rule, note; double v = 0; bool ok = true, warn = false, info = false; };
json verdictJson(const Verdict& v) { return {{"metric", v.key}, {"value", v.v}, {"rule", v.rule}, {"ok", v.ok}, {"severity", v.info ? "info" : v.warn ? "warn" : "fail"}, {"note", v.note}}; }

// rules: {"metric.path": {"lt"|"max"|"gt"|"min": number or "jog", "severity": "fail"|"warn"|"off", "note": ...}}
std::vector<Verdict> judge(const json& rules, const json& res, double jog) {
  std::vector<Verdict> out;
  for (auto& [key, r] : rules.items()) {
    if (key.empty() || key[0] == '_' || !r.is_object() || key == "objectiveFall_s" || key == "drops") continue;
    std::string sev = r.value("severity", "fail");
    if (sev == "off") continue;
    if (!r.contains("lt") && !r.contains("max") && !r.contains("gt") && !r.contains("min")) continue;   // a setting, not a threshold
    Verdict v; v.key = key; v.warn = sev == "warn"; v.note = r.value("note", "");
    const json* x = at(res, key);
    if (!x || !x->is_number()) { v.ok = false; v.rule = "metric missing from the result"; out.push_back(v); continue; }
    v.v = x->get<double>();
    auto bound = [&](const json& t, std::string& label) { if (t.is_string() && t.get<std::string>() == "jog") { label = "jog " + num(jog); return jog; } label = num(t.get<double>()); return t.get<double>(); };
    std::string lab;
    if (r.contains("lt")) { double b = bound(r["lt"], lab); v.ok = v.ok && v.v < b; v.rule += (v.rule.empty() ? "" : ", ") + std::string("< ") + lab; }
    if (r.contains("max")) { double b = bound(r["max"], lab); v.ok = v.ok && v.v <= b; v.rule += (v.rule.empty() ? "" : ", ") + std::string("<= ") + lab; }
    if (r.contains("gt")) { double b = bound(r["gt"], lab); v.ok = v.ok && v.v > b; v.rule += (v.rule.empty() ? "" : ", ") + std::string("> ") + lab; }
    if (r.contains("min")) { double b = bound(r["min"], lab); v.ok = v.ok && v.v >= b; v.rule += (v.rule.empty() ? "" : ", ") + std::string(">= ") + lab; }
    out.push_back(v);
  }
  return out;
}
// AI_SPEC 12.5: every objective falls within [min, max] s of its phase start; a phase held past max is a miss too
void judgeFalls(const json& rules, const json& falls, std::vector<Verdict>& out) {
  if (!rules.contains("objectiveFall_s")) return;
  const json& r = rules["objectiveFall_s"]; std::string sev = r.value("severity", "fail");
  if (sev == "off") return;
  double lo = r.value("min", 0.0), hi = r.value("max", 1e9);
  bool any = false;
  for (auto& f : falls) {
    Verdict v; v.key = "objectiveFall_s " + f.value("label", std::string("?")) + " (match " + std::to_string(f.value("match", 0)) + ")";
    v.v = f.value("seconds", 0.0); v.warn = sev == "warn"; v.note = f.value("outcome", std::string());
    if (f.value("outcome", std::string()) == "captured") { v.ok = v.v >= lo && v.v <= hi; v.rule = num(lo) + " .. " + num(hi) + " s"; any = true; }
    else if (v.v > hi) { v.ok = false; v.rule = "held past " + num(hi) + " s"; any = true; }
    else { v.info = true; v.rule = "held " + num(v.v) + " s when the run ended (no verdict before " + num(hi) + " s)"; }
    out.push_back(v);
  }
  if (!any) { Verdict v; v.key = "objectiveFall_s"; v.info = true; v.rule = "no objective fell and no phase ran past " + num(hi) + " s: run longer for a verdict"; out.push_back(v); }
}

fs::path findBaseline(const json& TH, std::string& why) {
  std::string rel = TH.contains("baseline") ? TH["baseline"].value("file", std::string("docs/baseline/ai_baseline.json")) : std::string("docs/baseline/ai_baseline.json");
  if (!O.baseline.empty()) return O.baseline;
  if (const char* src = std::getenv("NMK_SRC")) if (fs::exists(fs::path(src) / rel)) return fs::path(src) / rel;
  for (fs::path start : {exeDir(), fs::current_path()}) for (fs::path p = start; !p.empty(); p = p.parent_path()) { if (fs::exists(p / rel)) return p / rel; if (p == p.parent_path()) break; }
  why = rel + " not found (pass --baseline <file> or set NMK_SRC to the repo)";
  return {};
}
// AI_SPEC 12.5: compare with the recorded baseline (deaths per minute for attacking bots must fall clearly)
void judgeBaseline(const json& TH, json& res, std::vector<Verdict>& out) {
  if (!TH.contains("baseline")) return;
  const json& B = TH["baseline"];
  std::string why; fs::path p = findBaseline(TH, why);
  json base; if (!p.empty()) try { base = json::parse(readTextFile(p)); } catch (...) { why = "unreadable baseline " + p.string(); }
  if (base.is_object() && !base.contains("attackerDeathsPerBotPerMin") && base.contains("attackerDeathsPerMin") && base.value("attackingBots", 0) > 0)
    base["attackerDeathsPerBotPerMin"] = base["attackerDeathsPerMin"].get<double>() / base["attackingBots"].get<double>();
  json cmp = json::object();
  const json rep = B.value("report", json::array()), cmpRules = B.value("compare", json::object());   // named: items() of a temporary dangles
  for (auto& k : rep) { std::string key = k; const json* c = at(res, key); if (base.is_object() && base.contains(key) && c && c->is_number()) cmp[key] = {{"baseline", base[key]}, {"now", *c}, {"delta", c->get<double>() - base[key].get<double>()}}; }
  res["baseline"] = {{"file", p.string()}, {"map", base.is_object() ? base.value("map", std::string()) : std::string()}, {"aiVersion", base.is_object() ? base.value("aiVersion", std::string()) : std::string()}, {"compare", cmp}};
  for (auto& [key, r] : cmpRules.items()) {
    if (key[0] == '_' || r.value("severity", std::string("fail")) == "off") continue;
    Verdict v; v.key = key + " vs baseline"; v.warn = r.value("severity", std::string("fail")) == "warn"; v.note = r.value("note", std::string());
    const json* c = at(res, key);
    if (!base.is_object() || !base.contains(key) || !c || !c->is_number()) { v.ok = false; v.rule = base.is_object() ? "metric missing" : why; out.push_back(v); continue; }
    v.v = c->get<double>(); double b = base[key].get<double>();
    if (r.contains("maxRatio")) { v.ok = v.v <= b * r["maxRatio"].get<double>(); v.rule = "<= " + num(r["maxRatio"].get<double>()) + " x baseline " + num(b); }
    if (r.contains("minRatio")) { v.ok = v.ok && v.v >= b * r["minRatio"].get<double>(); v.rule += (v.rule.empty() ? "" : ", ") + std::string(">= ") + num(r["minRatio"].get<double>()) + " x baseline " + num(b); }
    cmp[key]["ratio"] = b != 0 ? v.v / b : 0.0; res["baseline"]["compare"] = cmp;
    out.push_back(v);
  }
}

// ---------------------------------------------------------------- maps and matches
bool openMapSpec(App& a, const std::string& spec, std::string& tag, std::string& err) {
  if (spec == "village") { tag = "village"; if (a.openMap(contentDir() / "examples" / "village_assault_5.nmap.json")) return true; err = "village_assault_5 missing"; return false; }
  if (spec == "course") { tag = "course"; if (a.openMap(contentDir() / "examples" / "ai_obstacle_course.nmap.json")) return true; err = "ai_obstacle_course missing"; return false; }
  if (spec.rfind("random", 0) == 0) {
    std::vector<std::string> parts; { std::string s = spec; size_t p; while ((p = s.find(':')) != std::string::npos) { parts.push_back(s.substr(0, p)); s = s.substr(p + 1); } parts.push_back(s); }
    uint32_t seed = parts.size() > 1 ? (uint32_t)std::strtoul(parts[1].c_str(), nullptr, 10) : O.seed;
    float size = parts.size() > 2 ? (float)std::atof(parts[2].c_str()) : O.size;
    tag = parts.size() > 1 ? "random_" + std::to_string(seed) : "random";
    if (!buildLargeRandomMap(a, seed, size, O.objectives, O.structures, err)) return false;
    if (O.saveMap) a.saveMapTo(userMapsDir() / ("ai_random_" + std::to_string(seed) + ".nmap.json"));
    return true;
  }
  std::vector<fs::path> cands = {spec, spec + ".nmap.json", contentDir() / "examples" / spec, contentDir() / "examples" / (spec + ".nmap.json"), userMapsDir() / spec, userMapsDir() / (spec + ".nmap.json"),
                                contentDir() / "templates" / (spec + ".nmap.json")};   // default maps shipped as templates (W5: omaha_easy_red)
  {   // short names for the default maps: the first template map whose name contains the spec ("omaha" -> omaha_easy_red)
    std::error_code ec; std::vector<fs::path> tm;
    for (auto& e : fs::directory_iterator(contentDir() / "templates", ec)) { std::string n = e.path().filename().string(); if (n.size() > 10 && n.substr(n.size() - 10) == ".nmap.json" && n.find(spec) != std::string::npos) tm.push_back(e.path()); }
    std::sort(tm.begin(), tm.end()); for (auto& c : tm) cands.push_back(c);
  }
  for (auto& c : cands) if (fs::exists(c) && fs::is_regular_file(c)) {
    std::string st = c.filename().string(); size_t d = st.find(".nmap"); tag = sanitize(d == std::string::npos ? c.stem().string() : st.substr(0, d));
    if (a.openMap(c)) return true;
    err = "could not open " + c.string(); return false;
  }
  err = "no map called " + spec + " (village, course, random[:seed[:size]], a name in content/examples, maps/ or content/templates, or a path)";
  return false;
}

bool startMatch(App& a, const std::string& tag, int attackers, int defenders, int respawns, bool fullThink, std::string& err) {
  if (!a.doc.extra.is_object() || !a.doc.extra.contains("scenario")) { err = "map has no scenario"; return false; }
  Scenario sc = Scenario::fromJson(a.doc.extra["scenario"]);
  if (attackers >= 0) sc.rules.attackerAI = attackers;
  if (defenders >= 1) sc.rules.defenderSlots = defenders;
  if (respawns >= 0) sc.rules.attackerRespawns = respawns;
  if (sc.rules.attackerAI + sc.rules.defenderSlots > ScenarioRules::kMaxCombatants) sc.rules.attackerAI = ScenarioRules::kMaxCombatants - sc.rules.defenderSlots;
  if (tag == "village") sc.rules.captureModel = CaptureModel::Timed;   // the v1 test map stores the contest model; W6's soak runs it timed too
  if (!O.difficulty.empty()) aiDifficultySetting() = O.difficulty;
  gUi.matchScenario = sc.toJson(); gUi.loaded = false;
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active && aiCore();
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  if (!ok) { err = "match did not start: " + gGame->setupError; return false; }
  if (fullThink) aiCore()->tactics["budget"]["lodDistance_m"] = 2000;   // no human nearby: every bot thinks at full rate
  return true;
}

int attackerDeaths(const Game& G) { int d = 0; for (auto& rs : G.match.slots) if (rs.role == CombatRole::Attacker) d += rs.deaths; return d; }

// One suite on one map: per-frame sampling plus the AiMetrics of every match, summed over the matches of the run.
struct Rec {
  std::string suite, spec, map; double simT = 0, jog = 3.1625; int matches = 0, bots = 0, attackers = 0, shortMatches = 0;
  double advT = 0, advSpeedSum = 0, nnSum = 0, fireT = 0, exposedT = 0, coverNear5T = 0, boundExp = 0, attackerT = 0; long advN = 0, nnN = 0;
  int bounds = 0, stuck = 0, over5 = 0, unrec = 0, resolvedBy[7]{}, gren = 0, grenHurt = 0, deaths = 0, linkTrav = 0, linkAborts = 0, snaps = 0, replans = 0, linkKinds[4]{};
  int openAtEnd = 0, openOver5 = 0, abandoned = 0, rebuildTiles = 0, caps = 0; double rebuildMsMax = 0;
  json stuckList = json::array(), falls = json::array(), minutes = json::array();
  std::map<int, int> stuckPerSlot; std::map<int, int> slotRole;
  std::vector<double> frameMs, aiMs, stepMs;
  // per-match state
  double matchT0 = 0, phaseStart = 0; std::vector<bool> fell; long profFrames = 0; double stepMs0 = 0; int stepCalls0 = 0;
  // per-minute bucket
  double bucketT0 = 0; int bStuck0 = 0, bDeaths0 = 0; std::vector<double> bFrame, bAi;

  void beginMatch(Game& G, AiCore& A) {
    matchT0 = simT; phaseStart = G.match.time; fell.assign(G.match.obj.size(), false);
    for (size_t k = 0; k < G.match.obj.size(); ++k) fell[k] = G.match.obj[k].state == ObjState::Captured;
    profFrames = A.prof.frames; stepMs0 = G.stepMs; stepCalls0 = G.stepCalls; jog = A.vJog;
    bots = 0; attackers = 0; for (auto& rs : G.match.slots) { bots += !rs.human; attackers += !rs.human && rs.role == CombatRole::Attacker; slotRole[rs.id] = (int)rs.role; }
    lastMatchTime = G.match.time; framesInMatch = 0;
  }
  double lastMatchTime = 0; int framesInMatch = 0;
  void tick(Game& G, AiCore& A, double frameWallMs) {
    simT += std::max(0.0, G.match.time - lastMatchTime); lastMatchTime = G.match.time;
    ++framesInMatch;   // the first frames of a match include the navmesh bake
    if (framesInMatch > 30 && frameWallMs > 0 && frameWallMs < 2000) { frameMs.push_back(frameWallMs); bFrame.push_back(frameWallMs); }
    if (A.prof.frames != profFrames && !A.prof.totals.empty()) { aiMs.push_back(A.prof.totals.back()); bAi.push_back(A.prof.totals.back()); profFrames = A.prof.frames; }
    if (G.stepCalls > stepCalls0) { stepMs.push_back((G.stepMs - stepMs0) / (G.stepCalls - stepCalls0)); stepMs0 = G.stepMs; stepCalls0 = G.stepCalls; }
    for (size_t k = 0; k < G.match.obj.size() && k < fell.size(); ++k)
      if (!fell[k] && G.match.obj[k].state == ObjState::Captured) {
        fell[k] = true;
        falls.push_back({{"match", matches + 1}, {"index", (int)k}, {"label", G.match.obj[k].label}, {"seconds", G.match.time - phaseStart}, {"matchTime", G.match.time}, {"outcome", "captured"}});
        phaseStart = G.match.time;
      }
    if (simT - bucketT0 >= 60.0) flushMinute(G, A);
  }
  void flushMinute(Game& G, AiCore& A) {
    int st = stuck + A.metrics.stuckEvents, de = deaths + attackerDeaths(G);
    int alive = 0; for (auto& s : G.sold) alive += s.alive && !s.human && s.role == CombatRole::Attacker;
    minutes.push_back({{"minute", (int)minutes.size() + 1}, {"stuckEvents", st - bStuck0}, {"stuckPerBot", (st - bStuck0) / (double)std::max(1, bots)},
                       {"attackerDeaths", de - bDeaths0}, {"frameMsMean", meanOf(bFrame)}, {"aiMsMean", meanOf(bAi)}, {"aliveAttackers", alive}});
    bucketT0 = simT; bStuck0 = st; bDeaths0 = de; bFrame.clear(); bAi.clear();
  }
  void endMatch(Game& G, AiCore& A, const std::string& why) {
    AiMetrics& M = A.metrics;
    { std::string s; for (int k = 0; k < A_COUNT; ++k) if (M.exposedBy[k] > 0) { char b[64]; std::snprintf(b, sizeof b, " %s %.0f s", aiActionName(k), M.exposedBy[k]); s += b; }
      logf("aiharness: exposed under fire %.0f of %.0f s (set at a cover point %.0f s):%s; side-steps %d", M.exposedT, M.fireT, M.exposedAtCover, s.c_str(), M.sidesteps);
      s.clear(); for (int k = 0; k < A_COUNT; ++k) if (M.deathsBy[k] > 0) { char b[64]; std::snprintf(b, sizeof b, " %s %d", aiActionName(k), M.deathsBy[k]); s += b; }
      logf("aiharness: attacker deaths by action:%s; set at cover %d, inside the objective %d", s.c_str(), M.deathsInCover, M.deathsInObjective); }
    advT += M.advT; advSpeedSum += M.advSpeedSum; advN += M.advN; nnSum += M.nnSum; nnN += M.nnN; fireT += M.fireT; exposedT += M.exposedT; coverNear5T += M.coverNear5T; attackerT += M.attackerT;
    bounds += M.bounds; boundExp += M.boundExposure; stuck += M.stuckEvents; over5 += M.stuckOver5; unrec += M.stuckUnrecovered; gren += M.grenadesThrown; grenHurt += M.grenadesHurtTeammate;
    linkTrav += M.linkTraversals; linkAborts += M.linkAborts; snaps += M.snaps; replans += M.rebuildReplans;
    for (int k = 0; k < 7; ++k) resolvedBy[k] += M.stuckResolvedBy[k];
    for (int k = 0; k < 4; ++k) linkKinds[k] += M.linkKinds[k];
    rebuildTiles += G.nav.rebuilds; rebuildMsMax = std::max(rebuildMsMax, (double)G.nav.rebuildMsMax);
    deaths += attackerDeaths(G);   // the next match's counters start at 0, so the minute buckets continue unchanged
    for (size_t i = 0; i < A.stuck.size(); ++i) {
      const StuckEvent& e = A.stuck[i];
      std::string state; double dur = e.t1 - e.t0;
      if (!e.open) state = e.resolved ? "recovered" : "UNRECOVERED";
      else {
        bool live = e.slot >= 0 && e.slot < (int)G.sold.size() && G.sold[e.slot].alive && e.slot < (int)A.minds.size() && A.minds[e.slot] && A.minds[e.slot]->eventIdx == (int)i;
        if (live) { state = "open at end"; dur = G.t - e.t0; ++openAtEnd; if (dur > 5) ++openOver5; } else { state = "abandoned (bot died)"; dur = -1; ++abandoned; }
      }
      ++stuckPerSlot[e.slot];
      if (stuckList.size() < 3000)
        stuckList.push_back({{"map", map}, {"match", matches + 1}, {"slot", e.slot}, {"role", slotRole.count(e.slot) && slotRole[e.slot] == (int)CombatRole::Attacker ? "attacker" : "defender"},
                             {"t0", e.t0}, {"duration_s", dur}, {"pos", {e.pos.x, e.pos.y, e.pos.z}}, {"state", state},
                             {"step", e.step}, {"resolvedBy", e.resolved ? kStepName[std::clamp(e.step, 0, 6)] : "-"}, {"why", e.why},
                             {"doing", e.doing}, {"goal", {e.goal.x, e.goal.y, e.goal.z}}, {"next", {e.next.x, e.next.y, e.next.z}}});
    }
    for (size_t k = 0; k < fell.size(); ++k) if (!fell[k] && (int)k == G.match.active) {
      falls.push_back({{"match", matches + 1}, {"index", (int)k}, {"label", G.match.obj[k].label}, {"seconds", G.match.time - phaseStart}, {"matchTime", G.match.time}, {"outcome", "held"}, {"why", why}});
      break;
    }
    if (G.match.time < 1.0) ++shortMatches;
    ++matches;
  }
  json result(const std::string& difficulty) const {
    double mins = std::max(1.0 / 60.0, simT / 60.0);
    int capN = 0; for (auto& f : falls) capN += f.value("outcome", std::string()) == "captured";
    json rb = json::object(); for (int k = 1; k < 7; ++k) rb[kStepName[k]] = resolvedBy[k];
    std::vector<std::pair<int, int>> per(stuckPerSlot.begin(), stuckPerSlot.end());
    std::sort(per.begin(), per.end(), [](auto& x, auto& y) { return x.second > y.second; });
    json perBot = json::array(); for (size_t k = 0; k < per.size() && k < 12; ++k) perBot.push_back({{"slot", per[k].first}, {"events", per[k].second}, {"perMin", per[k].second / mins}});
    return {{"suite", suite}, {"map", map}, {"mapSpec", spec}, {"seconds", simT}, {"minutes", simT / 60.0}, {"matches", matches}, {"bots", bots}, {"attackingBots", attackers}, {"difficulty", difficulty},
            {"stuckEvents", stuck}, {"stuckPerBotPerMin", stuck / (double)std::max(1, bots) / mins}, {"stuckPerBotPer10Min", stuck / (double)std::max(1, bots) / mins * 10},
            {"stuckOver5s", over5 + openOver5}, {"stuckUnrecovered", unrec}, {"stuckOpenAtEnd", openAtEnd}, {"stuckAbandonedByDeath", abandoned}, {"stuckResolvedBy", rb},
            {"stuckWorstBots", perBot},
            {"exposedUnderFireFraction", fireT > 0 ? exposedT / fireT : 0.0}, {"underFireSeconds", fireT}, {"exposedUnderFireSeconds", exposedT},
            {"exposedUnderFireShareOfTime", attackerT > 0 ? exposedT / attackerT : 0.0}, {"attackerBotSeconds", attackerT},
            {"boundExposureMean_s", bounds ? boundExp / bounds : 0.0}, {"bounds", bounds},
            {"nearestTeammateWhileAdvancing_m", nnN ? nnSum / nnN : 0.0}, {"advanceSpeedMean_mps", advN ? advSpeedSum / advN : 0.0}, {"advancingBotSeconds", advT}, {"jogSpeed_mps", jog},
            {"grenadesThrown", gren}, {"grenadesHurtTeammate", grenHurt},
            {"attackerDeaths", deaths}, {"attackerDeathsPerMin", deaths / mins}, {"attackerDeathsPerBotPerMin", deaths / mins / std::max(1, attackers)},
            {"objectivesTaken", capN}, {"objectiveFalls", falls},
            {"frameMs", stats(frameMs)}, {"aiMainThreadMs", stats(aiMs)}, {"matchStepMs", stats(stepMs)},
            {"navRebuildTiles", rebuildTiles}, {"navRebuildMsMax", rebuildMsMax}, {"rebuildReplans", replans},
            {"linkTraversals", linkTrav}, {"linkAborts", linkAborts}, {"links", {{"drop", linkKinds[0]}, {"mantle", linkKinds[1]}, {"vault", linkKinds[2]}, {"jump", linkKinds[3]}}}, {"snaps", snaps},
            {"perMinute", minutes}, {"stuckEventsList", stuckList}};
  }
};

struct Ctx { std::function<void(bool, const std::string&)> check; json* summary; json TH; };

// verdicts -> checks, JSON and the text report
bool report(Ctx& cx, json& res, std::vector<Verdict>& vs, const std::string& headline, const std::vector<std::string>& extra) {
  bool pass = true; json vj = json::array();
  for (auto& v : vs) { vj.push_back(verdictJson(v)); if (!v.ok && !v.warn && !v.info) pass = false; }
  res["verdicts"] = vj; res["pass"] = pass;
  std::string base = "aiharness_" + res.value("suite", std::string()) + "_" + res.value("map", std::string()) + (O.tag.empty() || O.tag == res.value("map", std::string()) ? "" : "_" + O.tag);
  std::ofstream(outDirH() / (base + ".json")) << res.dump(2);
  std::ofstream t(outDirH() / (base + ".txt"));
  t << headline << "\n\n";
  for (auto& v : vs) {
    char b[600]; std::snprintf(b, sizeof b, "  %-5s %-48s %10s   %s%s%s", v.info ? "info" : v.ok ? "PASS" : v.warn ? "WARN" : "MISS", v.key.c_str(), v.info ? "" : num(v.v).c_str(), v.rule.c_str(), v.note.empty() ? "" : "   -- ", v.note.c_str());
    t << b << "\n";
  }
  for (auto& e : extra) t << e << "\n";
  const json& sl = res["stuckEventsList"];
  t << "\nStuck events (" << res.value("stuckEvents", 0) << "): map, match, bot, t0, duration, position, state, resolving step, why\n";
  for (size_t k = 0; k < sl.size() && k < 200; ++k) {
    auto& e = sl[k]; char b[500];
    std::snprintf(b, sizeof b, "  %s m%d bot %d (%s) t %.1f  %.1f s  (%.1f %.1f %.1f)  %s  %s  %s", e["map"].get<std::string>().c_str(), e["match"].get<int>(), e["slot"].get<int>(), e["role"].get<std::string>().c_str(), e["t0"].get<double>(), e["duration_s"].get<double>(),
                  e["pos"][0].get<double>(), e["pos"][1].get<double>(), e["pos"][2].get<double>(), e["state"].get<std::string>().c_str(), e["resolvedBy"].get<std::string>().c_str(), e["why"].get<std::string>().c_str());
    t << b << "\n";
  }
  if (sl.size() > 200) t << "  ... " << sl.size() - 200 << " more in " << base << ".json\n";
  // every miss is its own failed check (exit code 1); warnings and info lines pass
  for (auto& v : vs) cx.check(v.ok || v.warn || v.info, std::string(v.info ? "info " : v.ok ? "" : v.warn ? "WARN " : "MISS ") + res.value("suite", std::string()) + " " + res.value("map", std::string()) + ": " + v.key + " = " + (v.info ? "" : num(v.v) + " ") + "(" + v.rule + ")");
  cx.check(true, headline + " -> " + base + ".json / .txt");
  (*cx.summary)[res.value("suite", std::string()) + "_" + res.value("map", std::string())] = res;
  return pass;
}

double frameWall() {
  static auto last = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now(); double ms = std::chrono::duration<double, std::milli>(now - last).count(); last = now; return ms;
}

// ---------------------------------------------------------------- 12.3 soak
bool runSoak(App& a, const std::string& spec, float t, Ctx& cx) {
  static Rec R; static std::string err;
  const double total = O.minutes * 60.0;
  json rules;
  double fw = frameWall();
  if (t == 0) { R = Rec{}; R.suite = "soak"; R.spec = spec; }
  if (!gGame || !gGame->active) {
    if (R.simT < total && R.shortMatches < 3) {
      std::string tag;
      if (!openMapSpec(a, spec, tag, err)) { cx.check(false, "soak: " + err); return true; }
      R.map = tag;
      int att = O.attackers >= 0 ? O.attackers : (int)std::lround((O.bots > 0 ? O.bots : 32) * 0.625);
      int def = (O.bots > 0 ? O.bots : 32) - att;
      if (!startMatch(a, tag, att, std::max(1, def), O.respawns, O.fullThink, err)) { cx.check(false, "soak " + tag + ": " + err); return true; }
      R.beginMatch(*gGame, *aiCore());
      if (R.matches == 0) logf("aiharness: soak on %s for %.1f min, %d bots (%d attacking)", tag.c_str(), O.minutes, R.bots, R.attackers);
      return false;
    }
  } else {
    Game& G = *gGame; AiCore* A = aiCore();
    if (!A) { cx.check(false, "soak: AI core missing during the match"); return true; }
    R.tick(G, *A, fw);
    if (R.simT < total && G.match.phase == MatchPhase::Running) return false;
    R.endMatch(G, *A, R.simT >= total ? "soak time up" : "match ended: " + G.match.result);
    if (R.simT >= total && R.simT - R.bucketT0 > 20) R.flushMinute(G, *A);
    a.exitExplore();
    if (R.simT < total) return false;
  }
  rules = rulesFor(cx.TH, "soak", R.map);
  json res = R.result(aiDifficultySetting());
  std::vector<Verdict> vs = judge(rules, res, R.jog);
  judgeFalls(rules, res["objectiveFalls"], vs);
  judgeBaseline(cx.TH, res, vs);
  if (R.shortMatches >= 3) { Verdict v; v.key = "matches"; v.ok = false; v.rule = "3 matches ended within 1 s: scenario broken?"; vs.push_back(v); }
  char h[400]; std::snprintf(h, sizeof h, "AI harness soak on %s: %.1f min, %d bots (%d attacking), %s, %d matches", R.map.c_str(), R.simT / 60.0, R.bots, R.attackers, aiDifficultySetting().c_str(), R.matches);
  std::vector<std::string> extra;
  extra.push_back(""); extra.push_back("Objective falls:");
  for (auto& f : res["objectiveFalls"]) { char b[200]; std::snprintf(b, sizeof b, "  match %d  %-28s %-9s %.0f s", f["match"].get<int>(), f["label"].get<std::string>().c_str(), f["outcome"].get<std::string>().c_str(), f["seconds"].get<double>()); extra.push_back(b); }
  extra.push_back(""); extra.push_back("Per minute: stuck events, per bot, attacker deaths, frame ms, AI ms, attackers alive");
  for (auto& m : res["perMinute"]) { char b[200]; std::snprintf(b, sizeof b, "  %3d  %3d  %.3f  %3d  %6.2f  %5.2f  %d", m["minute"].get<int>(), m["stuckEvents"].get<int>(), m["stuckPerBot"].get<double>(), m["attackerDeaths"].get<int>(), m["frameMsMean"].get<double>(), m["aiMsMean"].get<double>(), m["aliveAttackers"].get<int>()); extra.push_back(b); }
  if (!O.saveBaseline.empty()) {
    json b = {{"aiVersion", "W6 Recast/Detour AI (saved by --ai-harness soak)"}, {"map", R.map}, {"seconds", R.simT}, {"attackingBots", R.attackers}, {"bots", R.bots}};
    for (const char* k : {"advanceSpeedMean_mps", "advancingBotSeconds", "attackerDeathsPerMin", "attackerDeathsPerBotPerMin", "exposedUnderFireFraction", "nearestTeammateWhileAdvancing_m", "objectivesTaken", "stuckEvents", "stuckPerBotPerMin", "stuckOver5s", "underFireSeconds", "boundExposureMean_s"}) b[k] = res[k];
    b["definitions"] = "AI_SPEC 12.3 metrics from AiMetrics (src/ai_core.cpp); harness: src/selftest_ai_harness.cpp";
    std::ofstream(O.saveBaseline) << b.dump(2);
    extra.push_back("baseline saved to " + O.saveBaseline);
  }
  report(cx, res, vs, h, extra);
  return true;
}

// ---------------------------------------------------------------- 12.2 obstacle course
bool runCourse(App& a, const std::string& spec, float t, Ctx& cx) {
  static Rec R; static std::set<int> reached, spawned, forbiddenBots; static int forbidden = 0; static double tReach = -1; static std::string err;
  json rules = rulesFor(cx.TH, "course", R.map.empty() ? spec : R.map);
  double fw = frameWall();
  if (t == 0) {
    R = Rec{}; R.suite = "course"; R.spec = spec; reached.clear(); spawned.clear(); forbiddenBots.clear(); forbidden = 0; tReach = -1;
    std::string tag;
    if (!openMapSpec(a, spec, tag, err)) { cx.check(false, "course: " + err); return true; }
    R.map = tag;
    if (!startMatch(a, tag, O.attackers >= 0 ? O.attackers : (O.bots > 0 ? O.bots - 1 : -1), O.bots > 0 ? 1 : -1, O.respawns, true, err)) { cx.check(false, "course " + tag + ": " + err); return true; }
    R.beginMatch(*gGame, *aiCore());
    return false;
  }
  if (!gGame || !gGame->active || !aiCore()) { cx.check(false, "course: the match ended unexpectedly"); return true; }
  Game& G = *gGame; AiCore& A = *aiCore();
  R.tick(G, A, fw);
  const Objective& oa = G.match.sc.objectives[0];
  vec3 fwd = attackForward(oa.attackYaw);
  for (auto& s : G.sold) {
    if (!s.alive || s.human || s.role != CombatRole::Attacker) continue;
    int key = s.slot * 100 + s.lifeId; spawned.insert(key);
    float along = glm::dot(vec3(s.feet.x - oa.pos.x, 0, s.feet.z - oa.pos.z), fwd);   // > 0 beyond the point's centre
    if (oa.contains(s.feet) || along > -0.45f * oa.radius) reached.insert(key);           // reached the far side (the point's front half or beyond)
    BotMind& m = A.mind(s);
    uint8_t ar = G.nav.areaAt(s.feet, 0.3f);
    if (!m.trav.on && ar != 0) { NavArea na = navarea::toNav(ar); if (na == NavArea::Minefield || na == NavArea::Wire) { ++forbidden; forbiddenBots.insert(s.slot); } }
  }
  int cleared = 0; for (auto& bd : A.buildings) { bool all = !bd.rooms.empty(); for (auto& r : bd.rooms) if (!A.clearedRooms.count(std::to_string(bd.inst) + ":" + r.id)) all = false; if (all) ++cleared; }
  double needFrac = rules.contains("reachedFraction") ? rules["reachedFraction"].value("min", 0.8) : 0.8;
  int needCleared = rules.contains("buildingsCleared") ? rules["buildingsCleared"].value("min", 2) : 2;
  bool reachedEnough = !spawned.empty() && reached.size() >= needFrac * spawned.size();
  if (reachedEnough && tReach < 0) tReach = t;
  bool done = reachedEnough && cleared >= needCleared && G.match.active >= 1;
  float timeout = rules.value("timeout_s", 420.0f);
  if (!done && t < timeout && G.match.phase == MatchPhase::Running) return false;
  R.endMatch(G, A, done ? "course done" : t >= timeout ? "timeout" : "match ended");
  json res = R.result(aiDifficultySetting());
  res["lives"] = spawned.size(); res["reachedLives"] = reached.size(); res["reachedFraction"] = spawned.empty() ? 0.0 : reached.size() / (double)spawned.size();
  res["secondsToReach"] = tReach < 0 ? -1.0 : tReach; res["buildings"] = A.buildings.size(); res["buildingsCleared"] = cleared;
  res["objectiveACaptured"] = G.match.active >= 1 ? 1 : 0; res["forbiddenSamples"] = forbidden; res["forbiddenBots"] = forbiddenBots.size();
  res["navLinks"] = {{"drop", G.nav.linkCount[0]}, {"mantle", G.nav.linkCount[1]}, {"vault", G.nav.linkCount[2]}, {"jump", G.nav.linkCount[3]}};
  a.exitExplore();
  std::vector<Verdict> vs = judge(rules, res, R.jog);
  char h[400]; std::snprintf(h, sizeof h, "AI harness obstacle course on %s: %zu of %zu attacker lives reached the far side (%.0f s), %d of %d buildings cleared, objective A %s, %d stuck events (%d unrecovered)",
                             R.map.c_str(), reached.size(), spawned.size(), tReach < 0 ? t : tReach, cleared, res["buildings"].get<int>(), G.match.active >= 1 ? "captured" : "NOT captured", R.stuck, R.unrec);
  report(cx, res, vs, h, {});
  return true;
}

// ---------------------------------------------------------------- 12.4 creative live rebuild
bool runRebuild(App& a, const std::string& spec, float t, Ctx& cx) {
  struct Box { vec3 c; vec2 he; float yaw; };
  struct Drop { json cfg; double T = -1; std::string what; bool enterable = false; std::vector<Box> boxes; std::map<int, double> aff; std::set<int> nearAtDrop, walked, stuckAt; };
  static Rec R; static std::vector<Drop> drops; static std::string err; static json rules;
  double fw = frameWall();
  if (t == 0) {
    R = Rec{}; R.suite = "rebuild"; R.spec = spec; drops.clear();
    std::string tag;
    if (!openMapSpec(a, spec, tag, err)) { cx.check(false, "rebuild: " + err); return true; }
    R.map = tag; rules = rulesFor(cx.TH, "rebuild", tag);
    const json dropCfg = rules.value("drops", json::array());
    for (auto& d : dropCfg) { Drop dr; dr.cfg = d; dr.what = d.value("what", std::string("object")); dr.enterable = d.value("enterable", false); drops.push_back(dr); }
    int att = O.attackers >= 0 ? O.attackers : O.bots > 0 ? O.bots - 1 : 9;
    if (!startMatch(a, tag, att, 1, O.respawns >= 0 ? O.respawns : 3, true, err)) { cx.check(false, "rebuild " + tag + ": " + err); return true; }
    R.beginMatch(*gGame, *aiCore());
    return false;
  }
  if (!gGame || !gGame->active || !aiCore()) { cx.check(false, "rebuild: the match ended unexpectedly"); return true; }
  Game& G = *gGame; AiCore& A = *aiCore();
  R.tick(G, A, fw);
  auto local2 = [](const Box& b, vec3 p) { vec3 d = p - b.c; float c = std::cos(radians(b.yaw)), s = std::sin(radians(b.yaw)); return vec2(d.x * c - d.z * s, d.x * s + d.z * c); };
  auto segHits = [&](const Box& b, vec3 p0, vec3 p1, float pad) {
    vec2 a0 = local2(b, p0), a1 = local2(b, p1), e = b.he + vec2(pad); float t0 = 0, t1 = 1; vec2 d = a1 - a0;
    for (int k = 0; k < 2; ++k) { if (std::fabs(d[k]) < 1e-6f) { if (std::fabs(a0[k]) > e[k]) return false; continue; } float u0 = (-e[k] - a0[k]) / d[k], u1 = (e[k] - a0[k]) / d[k]; if (u0 > u1) std::swap(u0, u1); t0 = std::max(t0, u0); t1 = std::min(t1, u1); if (t0 > t1) return false; }
    return true; };
  auto routeHits = [&](const Drop& dr, Soldier& s, float pad) {
    BotMind& m = A.mind(s); if (m.route.size() < 2) return false;
    size_t k0 = 0; float bd = 1e9f; for (size_t k = 0; k < m.route.size(); ++k) { float d = glm::length(m.route[k] - s.feet); if (d < bd) { bd = d; k0 = k; } }
    vec3 prev = s.feet; float L = 0;
    for (size_t k = k0; k < m.route.size() && L < 45; ++k) { for (auto& b : dr.boxes) if (segHits(b, prev, m.route[k], pad)) return true; L += glm::length(m.route[k] - prev); prev = m.route[k]; }
    return false; };
  float first = rules.value("firstDropAt_s", 5.0f), every = rules.value("dropInterval_s", 8.0f), settle = rules.value("settle_s", 15.0f), ahead = rules.value("dropAhead_m", 9.0f);
  for (size_t k = 0; k < drops.size(); ++k) {
    Drop& dr = drops[k];
    if (dr.T >= 0 || t < first + every * k) continue;
    // the moving attacker with the longest route left: the object goes across its route, `ahead` m in front of it
    vec3 p(0), h(0); bool found = false; float bestRem = 0; int target = -1;
    for (auto& s : G.sold) {
      if (!s.alive || s.human || s.role != CombatRole::Attacker || s.speed < 1.0f) continue;
      BotMind& m = A.mind(s); if (m.route.size() < 2 || m.trav.on) continue;
      size_t k0 = 0; float bd = 1e9f; for (size_t q = 0; q < m.route.size(); ++q) { float d = glm::length(m.route[q] - s.feet); if (d < bd) { bd = d; k0 = q; } }
      float L = 0; vec3 prev = s.feet;
      for (size_t q = k0 + 1; q < m.route.size(); ++q) {
        float seg = glm::length(m.route[q] - prev);
        if (L + seg >= ahead && seg > 0.1f) {
          float rem = 0; vec3 pp = m.route[q]; for (size_t r = q + 1; r < m.route.size(); ++r) { rem += glm::length(m.route[r] - pp); pp = m.route[r]; }
          if (rem + seg > bestRem) { bestRem = rem + seg; p = glm::mix(prev, m.route[q], (ahead - L) / seg); h = glm::normalize(vec3(m.route[q].x - prev.x, 0, m.route[q].z - prev.z) + vec3(1e-4f)); found = true; target = s.slot; }
          break;
        }
        L += seg; prev = m.route[q];
      }
    }
    if (!found) { if (t > first + every * k + 30) dr.T = -2; continue; }   // nobody moving for 30 s: give up on this drop
    vec3 side(-h.z, 0, h.x); float hy = degrees(std::atan2(h.x, h.z));
    int n = dr.cfg.value("count", 1); float spacing = dr.cfg.value("spacing_m", 0.0f), off = dr.cfg.value("ahead_m", 0.0f), yawOff = dr.cfg.value("yaw", 0.0f);
    std::string asset = dr.cfg.value("asset", std::string());
    const AssetDef* ad = a.lib.find(asset);
    if (!ad) { dr.T = -3; continue; }
    dr.T = G.t;
    for (int i = 0; i < n; ++i) {
      vec3 q = p + h * off + side * (spacing * (i - (n - 1) * 0.5f));
      Instance in; in.id = a.doc.newId(); in.assetId = asset; in.pos = vec3(q.x, a.terrain.height(q.x, q.z), q.z); in.yaw = hy + yawOff;
      a.doc.instances.push_back(in);
      vec3 lc = vec3((ad->bmin.x + ad->bmax.x) * 0.5f, 0, (ad->bmin.z + ad->bmax.z) * 0.5f); if (ad->tree.on) lc = vec3(0);
      Box b; b.c = in.pos + yawQuat(in.yaw) * lc; b.he = ad->footprint * 0.5f; b.yaw = in.yaw; dr.boxes.push_back(b);
    }
    a.syncRuntime();
    for (auto& s : G.sold) if (s.alive && !s.human && s.role == CombatRole::Attacker) {
      bool nearB = false; for (auto& b : dr.boxes) { vec2 l = local2(b, s.feet); if (std::fabs(l.x) < b.he.x + 0.6f && std::fabs(l.y) < b.he.y + 0.6f) nearB = true; }
      if (nearB) dr.nearAtDrop.insert(s.slot); else if (routeHits(dr, s, 0.3f)) dr.aff[s.slot] = -1;
    }
    logf("aiharness: %s dropped on bot %d's route at %.1f s (%zu bots affected)", dr.what.c_str(), target, G.t, dr.aff.size());
  }
  for (auto& dr : drops) {
    if (dr.T < 0) continue;
    for (auto& [slot, rt] : dr.aff) { Soldier& s = G.sold[slot]; if (rt < 0 && s.alive && A.mind(s).pathAt > dr.T && (dr.enterable || !routeHits(dr, s, 0.0f))) rt = G.t - dr.T; }
    if (G.t - dr.T > 0.3 && !dr.enterable) for (auto& s : G.sold) if (s.alive && !s.human && s.role == CombatRole::Attacker && !dr.nearAtDrop.count(s.slot))
      for (auto& b : dr.boxes) { vec2 l = local2(b, s.feet); if (std::fabs(l.x) < b.he.x - 0.15f && std::fabs(l.y) < b.he.y - 0.15f) dr.walked.insert(s.slot); }
    for (auto& e : A.stuck) if (e.t0 > dr.T + 0.3 && e.t0 < dr.T + 15 && !dr.nearAtDrop.count(e.slot))
      for (auto& b : dr.boxes) { vec2 l = local2(b, e.pos); if (std::fabs(l.x) < b.he.x + 0.8f && std::fabs(l.y) < b.he.y + 0.8f) dr.stuckAt.insert(e.slot); }
  }
  float endAt = first + every * std::max(0, (int)drops.size() - 1) + settle;
  if (t < endAt && G.match.phase == MatchPhase::Running) return false;
  R.endMatch(G, A, "rebuild test done");
  json res = R.result(aiDifficultySetting());
  double within = rules.contains("rerouteWithin_s") ? rules["rerouteWithin_s"].value("max", 1.0) : 1.0;
  json dj = json::array(); std::vector<Verdict> vs;
  int maxWalk = rules.contains("walkedIn") ? rules["walkedIn"].value("max", 0) : 0, maxStuck = rules.contains("stuckAgainst") ? rules["stuckAgainst"].value("max", 0) : 0;
  int minAff = rules.contains("affectedBots") ? rules["affectedBots"].value("min", 1) : 1;
  for (auto& dr : drops) {
    int n = 0, ok = 0; double worst = 0; for (auto& [slot, rt] : dr.aff) { ++n; if (rt >= 0 && rt <= within) ++ok; worst = std::max(worst, rt < 0 ? 99.0 : rt); }
    dj.push_back({{"what", dr.what}, {"t", dr.T}, {"affected", n}, {"reroutedInTime", ok}, {"slowest_s", worst > 90 ? -1.0 : worst}, {"walkedIn", dr.walked.size()}, {"stuckAgainst", dr.stuckAt.size()},
                  {"status", dr.T == -2 ? "no moving bot found" : dr.T == -3 ? "asset missing" : dr.T < 0 ? "not dropped" : "dropped"}});
    Verdict v; v.key = dr.what + ": affected bots re-routed within " + num(within) + " s"; v.v = ok;
    v.ok = dr.T >= 0 && n >= minAff && ok == n; v.rule = dr.T < 0 ? dj.back()["status"].get<std::string>() : num(ok) + " of " + num(n) + " (slowest " + (worst > 90 ? std::string("never") : num(worst) + " s") + ")";
    vs.push_back(v);
    Verdict w; w.key = dr.what + ": bots that walked into it"; w.v = dr.walked.size(); w.ok = (int)dr.walked.size() <= maxWalk; w.rule = "<= " + num(maxWalk) + (dr.enterable ? " (enterable: not checked)" : ""); vs.push_back(w);
    Verdict s; s.key = dr.what + ": bots stuck against it"; s.v = dr.stuckAt.size(); s.ok = (int)dr.stuckAt.size() <= maxStuck; s.rule = "<= " + num(maxStuck); vs.push_back(s);
  }
  res["drops"] = dj;
  json other = rules; other.erase("drops"); other.erase("rerouteWithin_s"); other.erase("walkedIn"); other.erase("stuckAgainst"); other.erase("affectedBots");
  for (auto& v : judge(other, res, R.jog)) vs.push_back(v);
  a.exitExplore();
  char h[300]; std::snprintf(h, sizeof h, "AI harness live rebuild on %s: %zu objects dropped across moving bots, %d tile rebuilds (slowest %.0f ms)", R.map.c_str(), drops.size(), res["navRebuildTiles"].get<int>(), res["navRebuildMsMax"].get<double>());
  report(cx, res, vs, h, {});
  return true;
}

// ---------------------------------------------------------------- random map helpers
float smooth01(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
void sculptMap(App& app, const std::function<float(float, float)>& f, vec2 lo, vec2 hi) {
  Terrain& T = app.terrain;
  std::map<int, std::vector<float>> deltas;
  float fs = T.fineSpacing(), ts = T.tileSize(); int n = T.tilesN();
  for (int tz = 0; tz < n; ++tz) for (int tx = 0; tx < n; ++tx) {
    vec2 o = T.tileOrigin(tx, tz);
    if (o.x > hi.x || o.x + ts < lo.x || o.y > hi.y || o.y + ts < lo.y) continue;
    std::vector<float> dv((size_t)Terrain::kFineN * Terrain::kFineN, 0.0f); bool any = false;
    for (int j = 0; j < Terrain::kFineN; ++j) for (int i = 0; i < Terrain::kFineN; ++i) { float v = f(o.x + i * fs, o.y + j * fs); if (std::fabs(v) > 1e-4f) { dv[j * Terrain::kFineN + i] = v; any = true; } }
    if (any) deltas[T.key(tx, tz)] = dv;
  }
  app.doc.terrainDeltas = deltas;
  app.doc.terrainTemplateVersion = app.doc.templateVersion;
  app.resetGround();
}
}  // namespace

// ---------------------------------------------------------------- public
bool aiHarnessArg(const std::string& a, int argc, char** argv, int& k, bool& runSelf) {
  if (a != "--ai-harness") return false;
  runSelf = true;
  if (k + 1 < argc && argv[k + 1][0] != '-') O.suite = argv[++k];
  while (k + 1 < argc) {
    std::string o = argv[k + 1];
    static const std::set<std::string> withVal = {"--map", "--maps", "--minutes", "--bots", "--attackers", "--respawns", "--difficulty", "--seed", "--size", "--objectives", "--structures", "--thresholds", "--baseline", "--save-baseline", "--tag"};
    if (o == "--save-map") { O.saveMap = true; ++k; continue; }
    if (o == "--full-think") { O.fullThink = true; ++k; continue; }
    if (o == "--steps-per-frame" && k + 2 < argc) { O.stepsPerFrame = std::clamp(std::atoi(argv[k + 2]), 1, 16); k += 2; continue; }
    if (!withVal.count(o) || k + 2 >= argc) break;
    k += 2; std::string v = argv[k];
    if (o == "--map" || o == "--maps") O.maps = splitList(v);
    else if (o == "--minutes") O.minutes = std::max(0.05f, (float)std::atof(v.c_str()));
    else if (o == "--bots") O.bots = std::clamp(std::atoi(v.c_str()), 2, ScenarioRules::kMaxCombatants);
    else if (o == "--attackers") O.attackers = std::max(1, std::atoi(v.c_str()));
    else if (o == "--respawns") O.respawns = std::max(0, std::atoi(v.c_str()));
    else if (o == "--difficulty") O.difficulty = v;
    else if (o == "--seed") O.seed = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
    else if (o == "--size") O.size = (float)std::atof(v.c_str());
    else if (o == "--objectives") O.objectives = std::clamp(std::atoi(v.c_str()), 1, 10);
    else if (o == "--structures") O.structures = std::max(0, std::atoi(v.c_str()));
    else if (o == "--thresholds") O.thresholds = v;
    else if (o == "--baseline") O.baseline = v;
    else if (o == "--save-baseline") O.saveBaseline = v;
    else if (o == "--tag") O.tag = sanitize(v);
  }
  return true;
}

bool buildLargeRandomMap(App& app, uint32_t seed, float size, int nObj, int structures, std::string& err) {
  const TemplateInfo* tpl = app.findTemplate("blank_slate");
  extern bool navNewMapSized(App&, const TemplateInfo&, const std::string&, vec2);   // W3 (editor_nav.cpp): 400-4000 m maps
  std::string nm = "AI harness random map (seed " + std::to_string(seed) + ")";
  if (!tpl || !(size + 24 > 600 ? navNewMapSized(app, *tpl, nm, vec2(size + 24)) : app.newMap(*tpl, nm))) { err = "newMap(blank_slate) failed"; return false; }
  float half = std::clamp(size * 0.5f, 120.0f, std::min(app.terrain.buildHalfX, app.terrain.buildHalfZ) - 12.0f);   // inside the build area
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(-1.0f, 1.0f);
  nObj = std::clamp(nObj, 1, 10);
  // objectives from the attackers' side (+z) to the far side (-z); attackers push toward -z (attack yaw 0)
  std::vector<vec3> objs;
  for (int k = 0; k < nObj; ++k) { float z = nObj == 1 ? 0.0f : half * 0.55f - k * (half * 1.15f) / (nObj - 1); objs.push_back(vec3(U(rng) * half * 0.3f, 0, z)); }
  std::vector<vec3> atk, def;
  for (int k = 0; k < nObj; ++k) for (int m = 0; m < 3; ++m) atk.push_back(vec3(objs[k].x - 12.0f + m * 12.0f, 0, std::min(objs[k].z + 140.0f, half - 8.0f)));
  for (int k = 0; k < nObj; ++k) for (int m = 0; m < 2; ++m) def.push_back(vec3(objs[k].x - 6.0f + m * 12.0f, 0, std::max(objs[k].z - 60.0f, -half + 8.0f)));
  auto clearOf = [&](vec3 p, float r) { for (auto& q : atk) if (glm::length(vec2(q.x - p.x, q.z - p.z)) < r) return false; for (auto& q : def) if (glm::length(vec2(q.x - p.x, q.z - p.z)) < r) return false; return true; };
  // structure pool: the classic set plus any bunker / trench assets in the library (W4 adds more)
  std::vector<std::string> pool;
  for (const char* id : {"house_stone_cottage", "house_farmhouse_2story", "small_storehouse", "small_stone_dairy", "stone_wall_low", "hedgerow_bocage", "def_sandbag_straight",
                         "def_tank_trap", "wire_concertina", "bunker_field_shelter", "bunker_mg_casemate", "tree_oak", "tree_ash", "shrub_hazel", "large_stone_warehouse"})
    if (app.lib.find(id)) pool.push_back(id);
  for (auto& ad : app.lib.assets) if ((ad->id.rfind("bunker_", 0) == 0 || ad->id.find("trench") != std::string::npos) && std::max(ad->footprint.x, ad->footprint.y) < 30 && std::find(pool.begin(), pool.end(), ad->id) == pool.end()) pool.push_back(ad->id);
  if (pool.empty()) { err = "no structure assets in the library"; return false; }
  auto place = [&](const std::string& id, vec3 p, float yaw) { Instance in; in.id = app.doc.newId(); in.assetId = id; in.pos = p; in.yaw = yaw; app.doc.instances.push_back(in); };
  int nStruct = structures >= 0 ? structures : (int)((2 * half) * (2 * half) / 2100.0f);
  std::vector<vec3> placed;
  for (int k = 0, tries = 0; k < nStruct && tries < nStruct * 6; ++tries) {
    vec3 p(U(rng) * (half - 6), 0, U(rng) * (half - 6));
    if (!clearOf(p, 12)) continue;
    bool close = false; for (auto& q : placed) if (glm::length(vec2(q.x - p.x, q.z - p.z)) < 9) close = true;
    if (close) continue;
    placed.push_back(p); place(pool[rng() % pool.size()], p, (float)(rng() % 360)); ++k;
  }
  // lines across the attack axis between objectives: hedgerows and wire belts with 2-3 gaps (gap finding, links)
  bool hasHedge = app.lib.find("hedgerow_bocage"), hasWire = app.lib.find("wire_concertina");
  for (int k = 0; k + 1 <= nObj; ++k) {
    float zFrom = k == 0 ? std::min(objs[0].z + 70.0f, half - 30.0f) : (objs[k - 1].z + objs[k].z) * 0.5f;
    std::vector<float> gaps; int ng = 2 + (int)(rng() % 2); for (int g = 0; g < ng; ++g) gaps.push_back(U(rng) * (half - 30));
    auto inGap = [&](float x, float w) { for (float g : gaps) if (std::fabs(x - g) < w) return true; return false; };
    if (hasHedge) for (float x = -half + 10; x <= half - 10; x += 8) { vec3 p(x, 0, zFrom); if (inGap(x, 4.5f) || !clearOf(p, 10)) continue; place("hedgerow_bocage", p, 0); }
    if (hasWire) for (float x = -half + 10; x <= half - 10; x += 3.8f) { vec3 p(x, 0, zFrom - 18); if (inGap(x, 3.5f) || !clearOf(p, 10)) continue; place("wire_concertina", p, 90); }
  }
  // shell craters
  std::vector<vec4> cr; int nCr = (int)((2 * half) * (2 * half) / 10000.0f);
  for (int k = 0; k < nCr; ++k) cr.push_back(vec4(U(rng) * (half - 10), U(rng) * (half - 10), 1.5f + std::fabs(U(rng)) * 3.5f, 0.5f + std::fabs(U(rng)) * 1.2f));
  sculptMap(app, [&](float x, float z) { float d = 0; for (auto& c : cr) { float q = ((x - c.x) * (x - c.x) + (z - c.y) * (z - c.y)) / (c.z * c.z); if (q < 1) d -= c.w * (1 - q) * (1 - q); } return d; }, vec2(-half, -half), vec2(half, half));
  for (auto& in : app.doc.instances) in.pos.y = app.terrain.height(in.pos.x, in.pos.z);
  app.syncRuntime();
  Scenario sc;
  for (int k = 0; k < nObj; ++k) { Objective o; o.id = sc.newId("obj"); o.label = std::string("Point ") + char('A' + k); o.order = k; o.pos = objs[k]; o.pos.y = app.terrain.height(o.pos.x, o.pos.z); o.radius = 24; o.attackYaw = 0; o.yMin = -3; o.yMax = 5; sc.objectives.push_back(o); }
  for (int k = 0; k < nObj; ++k) {
    for (int m = 0; m < 3; ++m) { SpawnMarker s; s.id = sc.newId("spawn"); s.role = CombatRole::Attacker; s.phases = {k}; s.pos = atk[k * 3 + m]; s.yaw = 0; s.label = "Attack"; sc.spawns.push_back(s); }
    for (int m = 0; m < 2; ++m) { SpawnMarker d; d.id = sc.newId("spawn"); d.role = CombatRole::Defender; d.phases = {k}; d.pos = def[k * 2 + m]; d.yaw = 180; d.label = "Defend"; sc.spawns.push_back(d); }
  }
  sc.rules.defenderSlots = 12; sc.rules.attackerAI = 20; sc.rules.attackerRespawns = 10; sc.rules.defendingFaction = Faction::Axis;
  if (!app.doc.extra.is_object()) app.doc.extra = json::object();
  app.doc.extra["scenario"] = sc.toJson();
  app.doc.start = {true, objs[0] + vec3(0, 0, -60), 180};
  logf("aiharness: random map seed %u, %.0f m, %d objectives, %zu instances, %d craters", seed, 2 * half, nObj, app.doc.instances.size(), nCr);
  return true;
}

void SelfTest::aiHarnessUpdate(App& app, float dt) {
  static std::vector<std::string> jobs; static size_t ji = 0; static float jt = 0; static Ctx cx;
  if (stage == 2900) {
    stage = 2901;
    reportName = "aiharness_" + O.suite + (O.tag.empty() ? "" : "_" + O.tag);
    cx.check = [this](bool ok, const std::string& s) { check(ok, s); }; cx.summary = &summary;
    std::string err; cx.TH = loadThresholds(err);
    if (!err.empty()) { check(false, err); finish(app); return; }
    if (O.suite != "course" && O.suite != "rebuild" && O.suite != "soak") { check(false, "unknown --ai-harness suite '" + O.suite + "' (course, rebuild, soak)"); finish(app); return; }
    if (O.maps.empty()) O.maps = {O.suite == "soak" ? "village" : "course"};
    jobs = O.maps; ji = 0; jt = 0;
    check(true, "AI harness " + O.suite + " on " + std::to_string(jobs.size()) + " map(s); thresholds " + (O.thresholds.empty() ? (contentDir() / "ai" / "test_thresholds.json").string() : O.thresholds));
    return;
  }
  if (ji >= jobs.size()) { app.useScripted = false; finish(app); return; }
  const std::string& m = jobs[ji];
  // soak fast mode: extra fixed simulation steps without rendering them (metrics run on match time and per AI step)
  if (O.suite == "soak" && O.stepsPerFrame > 1 && app.mode == Mode::Explore && gGame && gGame->active && gGame->match.phase == MatchPhase::Running)
    for (int k = 1; k < O.stepsPerFrame; ++k) app.updateExplore(1.0f / 60.0f);
  bool done = O.suite == "course" ? runCourse(app, m, jt, cx) : O.suite == "rebuild" ? runRebuild(app, m, jt, cx) : runSoak(app, m, jt, cx);
  jt += dt;
  if (done) { if (app.mode == Mode::Explore) app.exitExplore(); ++ji; jt = 0; }
}
