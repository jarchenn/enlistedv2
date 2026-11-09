// --selftest-modes (G1): Solo / Team Deathmatch rules (headless) and live matches on the shipped 3-point village:
// Solo spawns spread apart, bots fight the closest combatant and score, the scoreboard draws; TDM teams split and
// score; 100-AI and 200-AI Solo matches run without long stalls.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "match.h"
#include "crumb.h"
#include "selftest_crowd.h"
#include "nav.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <set>
#include <cstdlib>
#include <cstdarg>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr;
int mode = -1; bool started = false;
fs::path pendingShot;
char B[700];
bool chk(bool ok, const char* fmt, ...) { va_list a; va_start(a, fmt); std::vsnprintf(B, sizeof B, fmt, a); va_end(a); return T->check(ok, B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }

ScenarioRules dmRules(GameMode m) { ScenarioRules r; r.mode = m; return r; }

void headless() {
  // ---- rosters
  {
    Scenario sc; sc.rules = dmRules(GameMode::Solo); sc.rules.soloPlayers = 6;
    Match m; std::string err;
    bool ok = m.setup(sc, 1, err);
    std::set<int> teams; for (auto& s : m.slots) teams.insert(s.team);
    chk(ok && m.slots.size() == 6 && teams.size() == 6 && m.slots[0].human && !m.slots[1].human, "Solo roster: 6 slots, 6 teams, the host in slot 0 (%s)", err.c_str());
  }
  {
    Scenario sc; sc.rules = dmRules(GameMode::TDM); sc.rules.teamPlayers[0] = 4; sc.rules.teamPlayers[1] = 3; sc.rules.humanTeam = 1;
    Match m; std::string err; bool ok = m.setup(sc, 1, err);
    int n[2] = {0, 0}; int hostTeam = -1; for (auto& s : m.slots) { ++n[s.team]; if (s.human) hostTeam = s.team; }
    chk(ok && n[0] == 4 && n[1] == 3 && hostTeam == 1 && m.slots[0].role == CombatRole::Defender && m.slots.back().role == CombatRole::Attacker,
        "TDM roster: 4 v 3, the host on team B as chosen, team A = Defender role (%s)", err.c_str());
    m.start();
    int j = m.humanJoin(7, "Joiner");
    chk(j >= 0 && m.slots[j].team == 0, "a LAN joiner balances onto team A (slot %d, team %d)", j, j >= 0 ? m.slots[j].team : -1);
  }
  // ---- scoring and the kill cap
  {
    Scenario sc; sc.rules = dmRules(GameMode::TDM); sc.rules.teamPlayers[0] = sc.rules.teamPlayers[1] = 2; sc.rules.killCap = 3;
    Match m; std::string err; m.setup(sc, 0, err); m.start();
    for (auto& s : m.slots) { s.pending = false; s.alive = true; }
    m.onDeath(2, 0);                                   // team A kills team B
    m.slots[2].alive = true; m.onDeath(1, 0);          // a team kill
    chk(m.slots[0].kills == 1 && m.slots[0].score == 0 && m.teamKills(0) == 1, "TDM: an enemy kill scores +100, a team kill -100 and no kill (kills %d, score %d)", m.slots[0].kills, m.slots[0].score);
    for (int k = 0; k < 2; ++k) { m.slots[3].alive = true; m.onDeath(3, 0); }
    m.update(1.0f / 60);
    chk(m.phase == MatchPhase::Ended && m.winner == Winner::Defenders, "TDM: team A reaches the kill cap of 3 and wins (%s)", m.result.c_str());
  }
  {
    Scenario sc; sc.rules = dmRules(GameMode::Solo); sc.rules.soloPlayers = 4; sc.rules.killCap = 2; sc.rules.respawnDelay = 1.0f;
    Match m; std::string err; m.setup(sc, 0, err); m.start();
    for (auto& s : m.slots) { s.pending = false; s.alive = true; }
    m.onDeath(1, 2);
    for (int k = 0; k < 70; ++k) m.update(1.0f / 60);
    chk(m.phase == MatchPhase::Running && m.slots[1].pending, "Solo: the dead respawn after the delay (pending after 1.17 s)");
    m.slots[3].alive = true; m.onDeath(3, 2); m.update(1.0f / 60);
    chk(m.phase == MatchPhase::Ended && m.winnerSlot == 2, "Solo: the first to 2 kills wins (winner slot %d, %s)", m.winnerSlot, m.result.c_str());
  }
  // ---- data round trip and validation
  {
    ScenarioRules r = dmRules(GameMode::TDM); r.killCap = 42; r.teamPlayers[0] = 30; r.teamPlayers[1] = 50; r.aiType[1] = AiType::Zombies; r.humanTeam = 1; r.teamFaction[0] = Faction::Axis;
    ScenarioRules b = ScenarioRules::fromJson(r.toJson());
    chk(b.mode == GameMode::TDM && b.killCap == 42 && b.teamPlayers[1] == 50 && b.aiType[1] == AiType::Zombies && b.humanTeam == 1 && b.teamFaction[0] == Faction::Axis,
        "rules round trip: mode, kill cap, team sizes, AI type, host team, factions");
    Scenario sc; sc.rules = dmRules(GameMode::Solo);
    bool err = false; for (auto& i : validateScenario(sc, false)) err |= i.error;
    Scenario tdm; tdm.rules = dmRules(GameMode::TDM);
    int warns = 0; bool terr = false; for (auto& i : validateScenario(tdm, false)) { terr |= i.error; warns += !i.error; }
    SpawnMarker m; m.team = 1; m.id = "tA"; tdm.spawns.push_back(m);
    Scenario back = Scenario::fromJson(tdm.toJson());
    chk(!err && !terr && warns == 2 && back.spawns.size() == 1 && back.spawns[0].team == 1 && back.spawns[0].phases.empty(),
        "Solo / TDM need no objectives; TDM without team markers warns (%d); team markers round trip", warns);
    chk(ScenarioRules::kMaxAI == 200 && dmRules(GameMode::Solo).kMaxCombatants >= 201, "up to 200 AI (kMaxCombatants %d)", ScenarioRules::kMaxCombatants);
  }
}

// a match on the open map (`map` empty) or on `map` (default: the shipped 3-point village)
bool startMode(App& a, GameMode m, int players, int cap, int teamA = 4, int teamB = 4, const fs::path& map = contentDir() / "examples" / "village_assault_3.nmap.json") {
  if (a.mode == Mode::Explore) a.exitExplore();
  const char* mapOverride = std::getenv("NMK_MODES_MAP");   // dev hook: run the default-map matches on another map file (e.g. templates/chateau_town.nmap.json)
  fs::path open = map;
  if (mapOverride && *mapOverride && map == contentDir() / "examples" / "village_assault_3.nmap.json") open = contentDir() / mapOverride;
  if (!open.empty()) a.openMap(open);
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = m; sc.rules.killCap = cap; sc.rules.soloPlayers = players; sc.rules.teamPlayers[0] = teamA; sc.rules.teamPlayers[1] = teamB;
  sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0;
  if (std::getenv("NMK_MODES_NOFRAG")) sc.rules.fragPerLife = 0;   // diagnostics
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
float minPairDist(const Game& G, bool sameTeamToo) {
  float d = 1e9f;
  for (auto& a : G.sold) for (auto& b : G.sold) if (&a < &b && a.alive && b.alive && (sameTeamToo || a.team != b.team)) d = std::min(d, glm::length(vec2(a.feet.x - b.feet.x, a.feet.z - b.feet.z)));
  return d;
}

void build(App&) {
  push("headless rules", [](App&, float) { headless(); return true; });
  // the navmesh bake cache: a repeat start of the same Solo map loads the tiles instead of baking them again. The counts
  // are taken right after each start: once the match runs, doors and late colliders rebuild tiles (PF1b: M1 saw
  // 19,444 polygons loaded against 19,413 counted a second into the first match)
  push("nav bake cache", [](App& a, float t) {
    static int tiles0, polys0, links0; static double ms0; static std::vector<char> blob0;
    if (t == 0) {
      std::error_code ec; fs::remove_all(exeDir() / "cache" / "nav", ec);
      _putenv_s("NMK_NAV_CACHE", "1");
      chk(startMode(a, GameMode::Solo, 4, 99), "Solo start for the bake cache (%s)", gGame->setupError.c_str());
      const Nav& N = gGame->nav;
      tiles0 = N.tilesBuilt; polys0 = N.usedNodes; links0 = N.links; ms0 = N.buildMs; blob0 = N.blob;
      chk(!N.fromCache && tiles0 > 0 && fs::exists(N.cachePath), "first start bakes %d tiles, %d polygons in %.0f ms and writes %s", tiles0, polys0, ms0, fs::path(N.cachePath).filename().string().c_str());
      return false;
    }
    if (t < 1.0f) return false;
    chk(startMode(a, GameMode::Solo, 4, 99), "Solo restart on the same map (%s)", gGame->setupError.c_str());
    const Nav& N = gGame->nav;
    chk(N.fromCache && N.tilesBuilt == tiles0 && N.usedNodes == polys0 && N.links == links0 && N.blob == blob0 && N.buildMs < ms0 * 0.5,
        "second start loads exactly the baked navmesh from the cache: %d tiles, %d polygons (baked %d), %d links, same %zu bytes, in %.0f ms (baked: %.0f ms)",
        N.tilesBuilt, N.usedNodes, polys0, N.links, N.blob.size(), N.buildMs, ms0);
    blob0.clear();
    if (!std::getenv("NMK_MODES_MAP")) _putenv_s("NMK_NAV_CACHE", "");   // another map (dev hook): keep the bake for the matches below
    return true; });
  // PF1: the navmesh saved with the map: <map>.nav beside a user map, one entry per bake bounds, never a stale one
  push("nav sidecar", [](App& a, float t) {
    static int stage; static int tiles0, polys0; static double ms0; static fs::path map, side;
    auto entries = [&] { std::vector<NavSidecarEntry> e; navSidecarRead(side, e); return e.size(); };
    std::error_code ec;
    if (t == 0) {
      _putenv_s("NMK_NAV_CACHE", "1"); stage = 0;
      map = userMapsDir() / "pf1_sidecar_test.nmap.json"; side = navSidecarOf(map);
      fs::remove(side, ec); fs::remove_all(exeDir() / "cache" / "nav", ec);
      if (a.mode == Mode::Explore) a.exitExplore();
      a.openMap(contentDir() / "examples" / "village_assault_3.nmap.json");
      a.doc.instances.pop_back(); a.syncRuntime();   // not the village the previous step played (its bake would carry over)
      chk(a.saveMapTo(map) && !fs::exists(side), "a user map saved before any match on it has no sidecar yet");
      chk(startMode(a, GameMode::Solo, 4, 99, 4, 4, map), "Solo on the saved map (%s)", gGame->setupError.c_str());
      Game& G = *gGame;
      tiles0 = G.nav.tilesBuilt; polys0 = G.nav.usedNodes; ms0 = G.nav.buildMs;
      chk(!G.nav.fromSidecar && !G.nav.fromCache && fs::exists(side) && entries() == 1, "first play bakes (%d tiles, %.0f ms) and writes %s with 1 entry (%zu)",
          tiles0, ms0, side.filename().string().c_str(), entries());
      return false;
    }
    Game& G = *gGame;
    if (stage == 0 && t > 0.5f) {
      stage = 1; fs::remove_all(exeDir() / "cache" / "nav", ec);
      chk(startMode(a, GameMode::Solo, 4, 99, 4, 4, map), "Solo again, bake cache emptied");
      chk(G.nav.fromSidecar && G.nav.tilesBuilt == tiles0 && G.nav.usedNodes == polys0 && G.nav.buildMs < ms0 * 0.5,
          "the second start loads the navmesh from the map's sidecar: %d tiles, %d polygons in %.0f ms (baked %.0f ms)", G.nav.tilesBuilt, G.nav.usedNodes, G.nav.buildMs, ms0);
      return false;
    }
    if (stage == 1 && t > 1.0f) {
      stage = 2;
      chk(startMode(a, GameMode::Assault, 4, 99, 4, 4, map), "Assault on the same map (%s)", gGame->setupError.c_str());
      chk(!G.nav.fromSidecar && entries() == 2, "Assault bakes its objective area and adds a second entry (%zu entries; bounds %.0f x %.0f m)", entries(), G.nav.hi.x - G.nav.lo.x, G.nav.hi.z - G.nav.lo.z);
      return false;
    }
    if (stage == 2 && t > 1.5f) {
      stage = 3;
      if (a.mode == Mode::Explore) a.exitExplore();
      a.openMap(map);
      a.saveMapTo(map);
      chk(entries() == 2, "saving the unchanged map keeps both entries (%zu)", entries());
      a.doc.instances.pop_back(); a.syncRuntime(); a.saveMapTo(map);
      chk(!fs::exists(side), "an edited map's save drops the stale entries (sidecar removed: %d)", (int)!fs::exists(side));
      fs::remove_all(exeDir() / "cache" / "nav", ec);
      chk(startMode(a, GameMode::Solo, 4, 99, 4, 4, map), "Solo on the edited map");
      chk(!G.nav.fromSidecar && !G.nav.fromCache && entries() == 1, "the edited map bakes afresh and gets its own entry (%zu)", entries());
      if (a.mode == Mode::Explore) a.exitExplore();
      fs::remove(map, ec); fs::remove(side, ec);
      _putenv_s("NMK_NAV_CACHE", "");
      return true;
    }
    return t > 60 ? (chk(false, "sidecar steps finished (stage %d)", stage), true) : false; });
  // PF1 maintenance (NMK_NAV_PREBAKE=1): bake every template's Solo / TDM and Assault navmesh into
  // content/templates/<map>.nav of this build (copy them to the repository's content/templates)
  if (std::getenv("NMK_NAV_PREBAKE")) push("prebake templates", [](App& a, float t) {
    _putenv_s("NMK_NAV_CACHE", "1");
    const char* only = std::getenv("NMK_NAV_PREBAKE_ONLY");   // e.g. omaha_easy_red,chateau_town (timing runs)
    for (auto& tp : a.templates) {
      std::string mf = tp.raw.value("map", "");
      if (mf.empty() || (only && *only && ("," + std::string(only) + ",").find("," + tp.id + ",") == std::string::npos)) continue;
      if (a.mode == Mode::Explore) a.exitExplore();
      a.newMap(tp, tp.name);
      Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
      for (GameMode m : {GameMode::Solo, GameMode::Assault}) {
        if (m == GameMode::Assault && sc.objectives.empty()) continue;
        double t0 = glfwGetTime();
        bool ok = startMode(a, m, 2, 99, 1, 1, fs::path());
        double startS = glfwGetTime() - t0;
        fs::path side = navSidecarOf(contentDir() / "templates" / mf); std::error_code ec;
        const Nav& N = gGame->nav;
        chk(ok && N.valid(), "%s %s: match start %.1f s (bake %.1f s: input %.1f s, %ld triangles), %d tiles, %d polygons, %s %.1f MB", tp.id.c_str(), gameModeName(m),
            startS, N.buildMs / 1000, N.collectMs / 1000, N.inputTris, N.tilesBuilt, N.usedNodes, side.filename().string().c_str(), fs::exists(side, ec) ? fs::file_size(side, ec) / 1048576.0 : 0.0);
      }
    }
    _putenv_s("NMK_NAV_CACHE", "");
    return true; });
  push("team spawn tool", [](App& a, float) {   // Scenario panel: "Team A spawn" / "Team B spawn"
    if (a.mode == Mode::Explore) a.exitExplore();
    a.openMap(contentDir() / "examples" / "village_assault_3.nmap.json");
    size_t n0 = a.doc.instances.size();
    vec3 pa(0, a.terrain.height(0, 60), 60), pb(0, a.terrain.height(0, -60), -60);
    std::string ia = gUi.addTeamSpawnAt(a, 0, pa), ib = gUi.addTeamSpawnAt(a, 1, pb);
    const SpawnMarker *A = nullptr, *B = nullptr;
    for (auto& m : gUi.scenario(a).spawns) { if (m.id == ia) A = &m; if (m.id == ib) B = &m; }
    const Instance* ra = A ? a.doc.find(A->rack) : nullptr;
    chk(A && B && A->team == 0 && B->team == 1 && A->phases.empty() && ra && a.doc.instances.size() >= n0 + 2,
        "Team A / B spawn tools place TDM markers (teams %d / %d, no Assault phase) with a team rack (%s)", A ? A->team : -9, B ? B->team : -9, ra ? ra->assetId.c_str() : "none");
    return true; });
  const bool only100 = std::getenv("NMK_MODES_ONLY_100") != nullptr;   // diagnostics: just the 100- and 200-AI runs (modesSelfTest drops the rest)
  if (!only100) {
  // ---- Solo: 8 combatants, first to 4
  push("solo start", [](App& a, float t) {
    if (t == 0) { chk(startMode(a, GameMode::Solo, 8, 4), "Solo match starts on the village (%s)", gGame->setupError.c_str()); return false; }
    if (t < 2.0f) return false;
    Game& G = *gGame;
    int alive = 0; std::set<int> teams; for (auto& s : G.sold) { alive += s.alive; teams.insert(s.team); }
    float md = minPairDist(G, true);
    chk(G.match.mode() == GameMode::Solo && G.sold.size() == 8 && teams.size() == 8 && alive == 8 && md > 20.0f,
        "Solo: 8 combatants on 8 teams all spawned, nearest pair %.0f m apart (spawns far from everyone)", md);
    return true; });
  push("solo fight", [](App& a, float t) {
    Game& G = *gGame;
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }   // keep the local player out of it
    int kills = 0; for (auto& s : G.match.slots) kills += s.kills;
    if (t > 20 && t < 20.05f) { G.showScoreboard = true; pendingShot = outDir() / "modes_solo_scoreboard.png"; }
    if (G.match.phase != MatchPhase::Ended && t < 150) return false;
    G.showScoreboard = false;
    int ws = -1; int top = G.match.topKills(&ws);
    chk(kills >= 3, "Solo: bots hunt the closest combatant and score (%d kills in %.0f s, leader %d)", kills, t, top);
    if (G.match.phase == MatchPhase::Ended) { chk(ws == G.match.winnerSlot && top >= 4, "Solo: %s", G.match.result.c_str()); pendingShot = outDir() / "modes_solo_end.png"; }
    else T->note("Solo: no winner within 150 s (kill cap 4)");
    return true; });
  // ---- TDM: 4 v 4, first to 6, no team markers on this map (the far-from-the-enemy fallback)
  push("tdm start", [](App& a, float t) {
    if (t == 0) { chk(startMode(a, GameMode::TDM, 8, 6), "TDM match starts (%s)", gGame->setupError.c_str()); return false; }
    if (t < 2.0f) return false;
    Game& G = *gGame;
    int n[2] = {0, 0}; for (auto& s : G.sold) ++n[s.team];
    Soldier* L = G.local();
    float md = minPairDist(G, false);
    chk(n[0] == 4 && n[1] == 4 && L && L->team == 0 && md > 15.0f, "TDM: 4 v 4, you on team A, nearest enemies %.0f m apart at spawn", md);
    return true; });
  push("tdm fight", [](App& a, float t) {
    Game& G = *gGame;
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
    if (t > 15 && t < 15.05f) pendingShot = outDir() / "modes_tdm_banner.png";
    // G4: ally head markers: look at the nearest living teammate, then every marked soldier must be a friend
    static bool g4done; if (t == 0) g4done = false;
    if (Soldier* L = G.local(); L && L->alive && !g4done && t > 6) {
      Soldier* best = nullptr; float bd = 1e9f;
      for (auto& o : G.sold) if (o.alive && &o != L && !Game::hostile(*L, o)) { float d = glm::length(o.feet - L->feet); if (d < bd) { bd = d; best = &o; } }
      if (best && t < 6.6f) { vec3 e = a.eyePos(), p = best->feet + vec3(0, 1.6f, 0); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
      if (t > 6.6f) {
        int friends = 0, foes = 0;
        for (int sl : G.markedSlots) for (auto& o : G.sold) if (o.slot == sl) (Game::hostile(*L, o) ? foes : friends)++;
        chk(friends >= 1 && foes == 0, "G4: ally head markers on teammates (%d marked), none on enemies (%d)", friends, foes);
        pendingShot = outDir() / "modes_g4_ally_markers.png"; g4done = true;
      }
    }
    if (G.match.phase != MatchPhase::Ended && t < 150) return false;
    int k0 = G.match.teamKills(0), k1 = G.match.teamKills(1);
    chk(k0 + k1 >= 3, "TDM: teams score (A %d, B %d) in %.0f s%s", k0, k1, t, G.match.phase == MatchPhase::Ended ? (" - " + G.match.result).c_str() : "");
    return true; });
  }
  // ---- 100 AI: Solo, 101 combatants, 40 s; frame-time stalls
  push("100 ai", [](App& a, float t) {
    static double last; static int frames, stalls; static float worst, sum; static std::string when;
    if (t == 0) { chk(startMode(a, GameMode::Solo, 101, 999), "100-AI Solo match starts (%s)", gGame->setupError.c_str()); last = glfwGetTime(); frames = stalls = 0; worst = sum = 0; when.clear(); return false; }
    double now = glfwGetTime(); float ft = (float)(now - last); last = now;
    if (t > 3) { ++frames; sum += ft; worst = std::max(worst, ft); stalls += ft > 0.15f; if (ft > 0.15f && when.size() < 400) { char w[64]; std::snprintf(w, sizeof w, " t%.1f:%.0fms(%s)", t, ft * 1000, gCrumb ? gCrumb : "?"); when += w; } }
    Game& G = *gGame;
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
    if (t > 10 && t < 10.05f) chk(G.markedSlots.empty(), "G4: no ally markers in Solo (%zu)", G.markedSlots.size());
    if (t < 40) return false;
    int alive = 0; for (auto& s : G.sold) alive += s.alive;
    int kills = 0; for (auto& s : G.match.slots) kills += s.kills;
    chk(G.sold.size() == 101 && alive >= 80, "100 AI: %zu combatants, %d alive, %d kills in 40 s", G.sold.size(), alive, kills);
    chk(stalls <= 3, "100 AI: frames over 150 ms: %d (worst %.0f ms, average %.1f ms over %d frames)", stalls, worst * 1000, frames ? sum / frames * 1000 : 0.0f, frames);
    T->note("stalls:" + when);
    pendingShot = outDir() / "modes_100ai.png";
    return true; });
  // ---- 200 AI (PF1): Solo, 201 combatants, 40 s; frame time, AI ms and path requests
  push("200 ai", [](App& a, float t) {
    static CrowdBench cb;
    if (t == 0) { chk(startMode(a, GameMode::Solo, 201, 999), "200-AI Solo match starts (%s)", gGame->setupError.c_str()); cb.begin(); return false; }
    Game& G = *gGame;
    cb.frame(G, t, 3.0f);
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
    if (t < 40) return false;
    int alive = 0; for (auto& s : G.sold) alive += s.alive;
    int kills = 0; for (auto& s : G.match.slots) kills += s.kills;
    chk(G.sold.size() == 201 && alive >= 150, "200 AI: %zu combatants, %d alive, %d kills in 40 s", G.sold.size(), alive, kills);
    chk(cb.over150() == 0, "200 AI: %s", cb.line(G).c_str());
    if (!cb.stalls.empty()) T->note("stalls:" + cb.stalls);
    pendingShot = outDir() / "modes_200ai.png";
    return true; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

bool modesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-modes") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) {
    started = true; T = new MpTest("modes"); build(app); cur = 0; st = 0;
    if (std::getenv("NMK_NAV_PREBAKE")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "prebake templates" && s.name != "done"; }), seq.end());   // just the bake
    else if (std::getenv("NMK_MODES_ONLY_CACHE")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "nav bake cache" && s.name != "done"; }), seq.end());   // just the cache round trip (with NMK_MODES_MAP: on another map)
    else if (std::getenv("NMK_MODES_ONLY_100")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "100 ai" && s.name != "200 ai" && s.name != "done"; }), seq.end());   // just the crowds
  }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 600) { T->check(false, "modes test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
