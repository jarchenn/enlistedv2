// SD2 --sd-timings [map]: route timings of a Search and Destroy map (docs/search-and-destroy.md section 7), measured
// as navmesh path lengths, against the spec's targets in seconds. The judged time is the realistic rush: sprint (the
// player's sprint speed) until the stamina runs out, then the jog (run speed); the jog-only time is reported too.
// Speeds x NMK_SD_SPEED_MUL (the mode's movement multiplier, default 1) x NMK_SD_WEAPON_SPEED (the weapon in hand,
// default 1: a rifle; the knife is 1.3). Spawn to site is measured from every spawn marker. Optional, in the scenario's rules.sd:
//   "timingPoints": {"contest": [[x, y, z], ...]}      the attackers' first contested boundaries
//   "timingRoutes": [{"name", "kind": "rotation" | "mid" | "fallback" | "attackMid", "points": [[x, y, z], ...]}]
//                   a route through these points in order (A to B, or attacker spawn to a site through mid)
// Also checks that no attacker spawn sees a defender spawn (eye to eye, 1.6 m).
#include "selftest.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include <algorithm>

namespace {
struct Band { const char* what; float lo, hi; };
const Band kDefCover{"defenders spawn -> site cover", 6, 9}, kAttContest{"attackers spawn -> first contested boundary", 10, 14},
    kAttPlant{"attackers spawn -> plant area, shortest", 15, 20}, kAttMid{"attackers through mid -> site", 18, 25},
    kRotation{"site to site, defender rotation", 10, 14}, kMid{"A to B through mid", 8, 11}, kFallback{"A to B, attacker fallback", 18, 24};

vec3 v3(const json& p) { return vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()); }
}  // namespace

bool sdTimingsTest(App& app, float dt) {
  static int mode = -1, frames = 0; static bool started = false; static MpTest* T = nullptr;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--sd-timings") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sd_timings");
    std::string map = "templates/chateau_town.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--sd-timings" && __argv[k + 1][0] != '-') map = __argv[k + 1];
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / map);
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.teamPlayers[t] = 1; }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    T->check(app.enterExplore() && gGame->active, "the map starts in Search and Destroy (" + map + ")");
    return true;
  }
  if (++frames < 20) return true;   // the navmesh is baked at load: give it a few frames
  Game& G = *gGame;
  json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
  const json rj = scj.value("rules", json::object()), sdj = rj.value("sd", json::object());
  std::vector<vec3> att, def; std::vector<std::string> attN, defN;
  for (auto& s : scj.value("spawns", json::array())) {
    std::string r = s.value("role", std::string());
    if (r == "attacker") { att.push_back(v3(s["pos"])); attN.push_back(s.value("label", std::string())); }
    if (r == "defender") { def.push_back(v3(s["pos"])); defN.push_back(s.value("label", std::string())); }
  }
  std::vector<vec3> site; std::vector<std::vector<vec3>> cover;
  for (auto& o : scj.value("objectives", json::array())) {
    site.push_back(v3(o["pos"])); cover.emplace_back();
    for (auto& d : o.value("defenseSlots", json::array())) if (d.contains("pos")) cover.back().push_back(v3(d["pos"]));
  }
  auto envf = [](const char* k, float d) { const char* e = std::getenv(k); return e ? (float)std::atof(e) : d; };
  const float mul = envf("NMK_SD_SPEED_MUL", 1.0f) * envf("NMK_SD_WEAPON_SPEED", 1.0f);
  const float jog = app.move.run * mul, spr = app.move.sprint * mul, tSpr = app.move.staminaMax / std::max(0.1f, app.move.staminaSprint);
  auto secs = [&](float l) { return l <= spr * tSpr ? l / spr : tSpr + (l - spr * tSpr) / jog; };   // sprint while the stamina lasts, then jog
  T->note("rush: sprint " + std::to_string(spr).substr(0, 4) + " m/s for " + std::to_string(tSpr).substr(0, 4) + " s, then jog " + std::to_string(jog).substr(0, 4) + " m/s");
  auto len = [&](vec3 a, vec3 b, bool& ok) { std::vector<vec3> pts; float l = 0;
    ok = G.nav.pathSync(a, b, pts, nullptr, nullptr, &l) && !pts.empty() && glm::length(pts.back() - b) < 2.5f; return l; };
  auto chain = [&](const std::vector<vec3>& p, bool& ok) { float s = 0; ok = true; for (size_t k = 1; k < p.size(); ++k) { bool o; s += len(p[k - 1], p[k], o); ok = ok && o; } return s; };
  char buf[256];
  auto band = [&](const Band& b, float lo_s, float hi_s, bool ok) {
    snprintf(buf, sizeof buf, "%s: %.1f-%.1f s (target %.0f-%.0f s)", b.what, lo_s, hi_s, b.lo, b.hi);
    T->check(ok && lo_s >= b.lo - 0.5f && hi_s <= b.hi + 0.5f, buf);
  };
  T->check(att.size() >= 5 && def.size() >= 5 && site.size() == 2, "5+ spawns per side, two sites (" + std::to_string(att.size()) + " / " + std::to_string(def.size()) + ", " + std::to_string(site.size()) + ")");
  if (site.size() != 2 || att.empty() || def.empty()) { app.selftest->exitCode = T->finish(); app.selftest->done = true; return true; }
  for (int s = 0; s < 2; ++s) {
    const char* L = s ? "B" : "A";
    float lo = 1e9f, hi = 0; bool allOk = true;
    for (size_t k = 0; k < def.size(); ++k) {   // to the nearest cover slot of the site (its centre when it has none)
      float best = 1e9f; bool any = false;
      for (const vec3& c : cover[s].empty() ? std::vector<vec3>{site[s]} : cover[s]) { bool o; float l = len(def[k], c, o); if (o) { best = std::min(best, l); any = true; } }
      allOk = allOk && any; lo = std::min(lo, secs(best)); hi = std::max(hi, secs(best));
      snprintf(buf, sizeof buf, "%s -> %s cover %.0f m %.1f s (jog %.1f s)", defN[k].c_str(), L, best, secs(best), best / jog); T->note(buf);
    }
    Band b = kDefCover; std::string w = std::string(b.what) + " " + L; b.what = w.c_str(); band(b, lo, hi, allOk);
    lo = 1e9f; hi = 0; allOk = true;
    for (size_t k = 0; k < att.size(); ++k) {
      bool o; float l = len(att[k], site[s], o); allOk = allOk && o; lo = std::min(lo, secs(l)); hi = std::max(hi, secs(l));
      snprintf(buf, sizeof buf, "%s -> %s plant %.0f m %.1f s (jog %.1f s)", attN[k].c_str(), L, l, secs(l), l / jog); T->note(buf);
    }
    Band b2 = kAttPlant; std::string w2 = std::string(b2.what) + " " + L; b2.what = w2.c_str(); band(b2, lo, hi, allOk);
  }
  { bool o; float l = len(site[0], site[1], o); snprintf(buf, sizeof buf, "A plant -> B plant, shortest: %.0f m %.1f s", l, secs(l)); T->check(o, buf); }
  if (sdj.contains("timingPoints") && sdj["timingPoints"].contains("contest")) {
    float lo = 1e9f, hi = 0; bool allOk = true;
    for (auto& a : att) { float best = 1e9f; for (auto& c : sdj["timingPoints"]["contest"]) { bool o; float l = len(a, v3(c), o); if (o) best = std::min(best, l); } allOk = allOk && best < 1e8f; lo = std::min(lo, secs(best)); hi = std::max(hi, secs(best)); }
    band(kAttContest, lo, hi, allOk);
  }
  for (auto& r : sdj.value("timingRoutes", json::array())) {
    std::vector<vec3> p; for (auto& q : r["points"]) p.push_back(v3(q));
    std::string kind = r.value("kind", std::string()), name = r.value("name", kind);
    const Band* b = kind == "rotation" ? &kRotation : kind == "mid" ? &kMid : kind == "fallback" ? &kFallback : kind == "attackMid" ? &kAttMid : nullptr;
    bool ok; float l = chain(p, ok);
    snprintf(buf, sizeof buf, "route %s: %.0f m, rush %.1f s, jog %.1f s", name.c_str(), l, secs(l), l / jog); T->note(buf);
    if (!b) { T->check(ok, buf); continue; }
    Band bb = *b; std::string w = std::string(b->what) + " (" + name + ")"; bb.what = w.c_str(); band(bb, secs(l), secs(l), ok);
  }
  int seen = 0;   // spawn to spawn sightlines
  for (auto& a : att) for (auto& d : def) {
    vec3 e0 = a + vec3(0, 1.6f, 0), e1 = d + vec3(0, 1.6f, 0); Physics::Hit h;
    if (!Physics::castRayEx(e0, glm::normalize(e1 - e0), glm::length(e1 - e0) - 0.2f, h)) ++seen;
  }
  T->check(seen == 0, "no attacker spawn sees a defender spawn (" + std::to_string(seen) + " open sightlines)");
  app.selftest->exitCode = T->finish(); app.selftest->done = true;
  return true;
}
