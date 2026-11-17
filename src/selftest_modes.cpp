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
#include "physics.h"
#include "loot.h"
#include "settings.h"
#include "ai_core.h"
#include "mainmenu.h"
#include "editor_nav.h"
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
    ScenarioRules r = dmRules(GameMode::TDM); r.killCap = 42; r.teamPlayers[0] = 30; r.teamPlayers[1] = 50; r.aiTypes[1] = aiBit(AiType::Zombies) | aiBit(AiType::Fighters); r.humanTeam = 1; r.teamFaction[0] = Faction::Axis;
    ScenarioRules b = ScenarioRules::fromJson(r.toJson());
    chk(b.mode == GameMode::TDM && b.killCap == 42 && b.teamPlayers[1] == 50 && b.aiTypes[1] == (aiBit(AiType::Zombies) | aiBit(AiType::Fighters)) && b.humanTeam == 1 && b.teamFaction[0] == Faction::Axis,
        "rules round trip: mode, kill cap, team sizes, AI types, host team, factions");
    Scenario sc; sc.rules = dmRules(GameMode::Solo);
    bool err = false; for (auto& i : validateScenario(sc, false)) err |= i.error;
    Scenario tdm; tdm.rules = dmRules(GameMode::TDM);
    int warns = 0; bool terr = false; for (auto& i : validateScenario(tdm, false)) { terr |= i.error; warns += !i.error; }
    SpawnMarker m; m.team = 1; m.id = "tA"; tdm.spawns.push_back(m);
    Scenario back = Scenario::fromJson(tdm.toJson());
    chk(!err && !terr && warns == 2 && back.spawns.size() == 1 && back.spawns[0].team == 1 && back.spawns[0].phases.empty(),
        "Solo / TDM need no objectives; TDM without team markers warns (%d); team markers round trip", warns);
    chk(ScenarioRules::kMaxAI == 1000 && dmRules(GameMode::Solo).kMaxCombatants >= 1001, "up to 1000 AI (kMaxCombatants %d)", ScenarioRules::kMaxCombatants);
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
    // G4: ally head markers: stand 6 m from the nearest living teammate (this map has no TDM team markers, so the
    // far-from-the-enemy fallback spawns teammates up to ~300 m apart, beyond the markers' 90 m), look at it, then
    // every marked soldier must be a friend
    static bool g4done, g4moved; if (t == 0) g4done = g4moved = false;
    if (Soldier* L = G.local(); L && L->alive && !g4done && t > 6) {
      Soldier* best = nullptr; float bd = 1e9f;
      for (auto& o : G.sold) if (o.alive && &o != L && !Game::hostile(*L, o)) { float d = glm::length(o.feet - L->feet); if (d < bd) { bd = d; best = &o; } }
      if (best && !g4moved) {
        vec2 dir = glm::length(vec2(L->feet.x - best->feet.x, L->feet.z - best->feet.z)) > 0.1f ? glm::normalize(vec2(L->feet.x - best->feet.x, L->feet.z - best->feet.z)) : vec2(0, 1);
        vec3 q(best->feet.x + dir.x * 6, 0, best->feet.z + dir.y * 6); q.y = a.terrain.height(q.x, q.z) + 0.05f;
        Physics::setCharacterPos(q); L->feet = q; g4moved = true;
      }
      if (best && t < 7.0f) { vec3 e = a.eyePos(), p = best->feet + vec3(0, 1.6f, 0); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
      if (t > 7.0f) {
        int friends = 0, foes = 0;
        for (int sl : G.markedSlots) for (auto& o : G.sold) if (o.slot == sl) (Game::hostile(*L, o) ? foes : friends)++;
        int nearN = 0; float bdEye = 1e9f; vec2 ndc(9); float w = 0;
        for (auto& o : G.sold) if (o.alive && &o != L && !Game::hostile(*L, o)) {
          float d = glm::length(o.feet + vec3(0, 2.2f, 0) - a.eyePos()); nearN += d < 90;
          if (d < bdEye) { bdEye = d; vec4 c = a.projM * a.viewM * vec4(o.feet + vec3(0, 2.2f, 0), 1); w = c.w; ndc = c.w > 0 ? vec2(c) / c.w : vec2(9); }
        }
        chk(friends >= 1 && foes == 0, "G4: ally head markers on teammates (%d marked), none on enemies (%d); teammates within 90 m: %d, nearest %.0f m at ndc %.2f, %.2f (w %.1f), eye yaw %.0f pitch %.0f",
            friends, foes, nearN, bdEye, ndc.x, ndc.y, w, a.eyeYaw, a.eyePitch);
        pendingShot = outDir() / "modes_g4_ally_markers.png"; g4done = true;
      }
    }
    if (G.match.phase != MatchPhase::Ended && t < 150) return false;
    int k0 = G.match.teamKills(0), k1 = G.match.teamKills(1);
    chk(k0 + k1 >= 3, "TDM: teams score (A %d, B %d) in %.0f s%s", k0, k1, t, G.match.phase == MatchPhase::Ended ? (" - " + G.match.result).c_str() : "");
    return true; });
  }
  // ---- D-day sides: the Allies land on the beach (z ~ 106) and attack, the Axis hold the village houses (z < -85)
  for (GameMode gm : {GameMode::Assault, GameMode::TDM})
    push(gm == GameMode::TDM ? "dday sides tdm" : "dday sides assault", [gm](App& a, float t) {
      const char* nm = gm == GameMode::TDM ? "TDM" : "Assault";
      if (t == 0) { chk(startMode(a, gm, 8, 99, 4, 4, contentDir() / "templates" / "dday.nmap.json"), "D-day %s starts (%s)", nm, gGame->setupError.c_str()); return false; }
      Game& G = *gGame;
      int beach = 0, house = 0, wrong = 0;
      for (auto& s : G.sold) {
        if (!s.alive) continue;
        bool allies = s.faction == Faction::Allies;
        if (allies && s.role == CombatRole::Attacker && s.feet.z > 80) ++beach;
        else if (!allies && s.role == CombatRole::Defender && s.feet.z < -85) ++house;
        else ++wrong;
      }
      if ((!beach || !house) && t < 20) return false;
      const auto& r = G.match.rules;
      bool names = gm == GameMode::TDM ? r.teamFaction[0] == Faction::Axis && r.teamFaction[1] == Faction::Allies : r.defendingFaction == Faction::Axis;
      chk(names && beach >= 1 && house >= 1 && wrong == 0, "D-day %s: Axis defend (%s), %d Allied attackers on the beach, %d Axis defenders in the village houses, %d elsewhere",
          nm, gm == GameMode::TDM ? "team A Axis, team B Allies" : "defending side Axis", beach, house, wrong);
      return true; });
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
  // ---- 200 AI (PF1): Solo, 201 combatants, 40 s; frame time, AI ms and path requests. AI2: 500 and 1000 AI the same
  // way, only with NMK_MODES_AI_SCALE (measurement runs, Medium preset; not part of the suite)
  for (int n : {200, 500, 1000})
    push(std::to_string(n) + " ai", [n](App& a, float t) {
      static CrowdBench cb;
      const bool scale = std::getenv("NMK_MODES_AI_SCALE") != nullptr;
      if (n > 200 && !scale) return true;
      if (t == 0) {
        if (scale) settings().applyPreset(a, GfxPreset::Medium);
        chk(startMode(a, GameMode::Solo, n + 1, 999), "%d-AI Solo match starts (%s)", n, gGame->setupError.c_str()); cb.begin(); return false;
      }
      Game& G = *gGame;
      cb.frame(G, t, 3.0f);
      if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; }
      if (t < 40) return false;
      int alive = 0; for (auto& s : G.sold) alive += s.alive;
      int kills = 0; for (auto& s : G.match.slots) kills += s.kills;
      chk((int)G.sold.size() == n + 1 && alive >= n * 3 / 4, "%d AI: %zu combatants, %d alive, %d kills in 40 s", n, G.sold.size(), alive, kills);
      if (n == 200) chk(cb.over150() == 0, "200 AI: %s", cb.line(G).c_str());
      else T->note(std::to_string(n) + " AI: " + cb.line(G));
      if (!cb.stalls.empty()) T->note("stalls:" + cb.stalls);
      pendingShot = outDir() / ("modes_" + std::to_string(n) + "ai.png");
      return true; });
  // BR2 capture (NMK_MODES_ONLY_BR2): a Solo start switched to Battle Royale, a filled kit, the inventory screen
  // AI3 capture (NMK_AI3_SHOT): the setup window in TDM with typed per-type counts; the spawn order hands out exactly
  // those counts, interleaved (aiTypeNext)
  // UI1b polish captures (NMK_UI1_POLISH): the editor's Open dialog, match setup, Settings, then a TDM match: HUD, pause
  // menu, held scoreboard, the end table -> selftest_out/ui1p_*.png
  push("ui1p screens", [](App& a, float t) {
    if (!std::getenv("NMK_UI1_POLISH")) return true;
    static int ph = 0; static float t0 = 0; static double r0 = 0;   // r0: real time of the last phase (UI fades run on real time; tests run faster)
    auto shot = [&](const char* n) { a.uiShot = outDir() / (std::string("ui1p_") + n + ".png"); };
    auto at = [&](float d) { return t > t0 + d && glfwGetTime() > r0 + std::min(d, 0.6f); };   // sim time and real time (the UI fades)
    if (t == 0) { ph = 0; t0 = 0; r0 = glfwGetTime(); if (a.mode == Mode::Explore) a.exitExplore(); a.openMap(contentDir() / "templates" / "chateau_town.nmap.json"); a.showOpen = true; return false; }
    switch (ph) {
      case 0: if (at(1.5f)) { shot("1_editor_open"); ph = 1; t0 = t; r0 = glfwGetTime(); } break;
      case 1: if (at(0.3f)) { a.showOpen = false; gUi.showSetup = true; gUi.setupMode = 0; ph = 2; t0 = t; r0 = glfwGetTime(); } break;
      case 2: if (at(0.8f)) { shot("2_setup"); ph = 3; t0 = t; r0 = glfwGetTime(); } break;
      case 3: if (at(0.3f)) { gUi.showSetup = false; settings().open = true; ph = 4; t0 = t; r0 = glfwGetTime(); } break;
      case 4: if (at(0.8f)) { shot("3_settings"); ph = 5; t0 = t; r0 = glfwGetTime(); } break;
      case 5: if (at(0.3f)) { settings().open = false; startMode(a, GameMode::TDM, 24, 999, 12, 12, contentDir() / "templates" / "chateau_town.nmap.json"); ph = 6; t0 = t; r0 = glfwGetTime(); } break;
      case 6: if (Soldier* L = gGame->local()) if (L->alive) { L->hp = 74; L->protectUntil = gGame->t + 1; }
              if (at(7.0f)) { shot("4_hud"); ph = 7; t0 = t; r0 = glfwGetTime(); } break;
      case 7: if (at(0.3f)) { a.pauseMenu = true; ph = 8; t0 = t; r0 = glfwGetTime(); } break;
      case 8: if (at(0.6f)) { shot("5_pause"); ph = 9; t0 = t; r0 = glfwGetTime(); } break;
      case 9: if (at(0.3f)) { a.pauseMenu = false; gGame->showScoreboard = true; ph = 10; t0 = t; r0 = glfwGetTime(); } break;
      case 10: if (at(0.6f)) { shot("6_scoreboard"); ph = 11; t0 = t; r0 = glfwGetTime(); } break;
      case 11: if (at(0.3f)) { gGame->showScoreboard = false; gGame->match.phase = MatchPhase::Ended; ph = 12; t0 = t; r0 = glfwGetTime(); } break;
      case 12: if (at(0.8f)) { shot("7_end"); ph = 13; t0 = t; r0 = glfwGetTime(); } break;
      case 13: if (at(0.3f)) return true; break;
    }
    return t > 60; });
  // UI1b captures (NMK_UI1_SHOTS): a small TDM on four maps; the HUD with the corner map, then the full map (M)
  for (const char* mp : {"dday", "chateau_town", "newtown", "outpost_desert"})
    push(std::string("ui1 ") + mp, [mp](App& a, float t) {
      if (!std::getenv("NMK_UI1_SHOTS")) return true;
      if (t == 0) { chk(startMode(a, GameMode::TDM, 24, 999, 12, 12, contentDir() / "templates" / (std::string(mp) + ".nmap.json")), "UI1 %s: TDM starts (%s)", mp, gGame->setupError.c_str()); a.mapLarge = false; return false; }
      if (Soldier* L = gGame->local()) if (L->alive) { L->hp = 100; L->protectUntil = gGame->t + 1; }
      if (t < 7.0f) return false;
      if (t < 7.05f) { a.uiShot = outDir() / (std::string("ui1_") + mp + "_hud.png"); return false; }
      if (t < 7.6f) { a.mapLarge = true; return false; }
      if (t < 7.65f) { a.uiShot = outDir() / (std::string("ui1_") + mp + "_map.png"); return false; }
      if (t < 8.0f) return false;
      a.mapLarge = false; return true; });
  // UI2 menu backdrop (NMK_UI2_BACKDROP): a street of our own map at late dusk under heavy cloud and dense fog, no
  // viewmodel; one frozen friendly soldier stands in front -> ui2_backdrop_bot.png, then without him -> ui2_backdrop_empty.png.
  // NMK_UI2_MAP (default newtown), NMK_UI2_VIEW "x,z,yaw,pitch" (else the spawn), NMK_UI2_SURVEY=1: four headings instead
  push("ui2 backdrop", [](App& a, float t) {
    if (!std::getenv("NMK_UI2_BACKDROP")) return true;
    static float yaw0 = 0, pitch0 = 0; static int bot = -1;
    const bool survey = std::getenv("NMK_UI2_SURVEY") != nullptr;
    if (t == 0) {
      const char* m = std::getenv("NMK_UI2_MAP");
      a.openMap(contentDir() / "templates" / (std::string(m && *m ? m : "newtown") + ".nmap.json"));
      if (const char* v = std::getenv("NMK_UI2_VIEW")) {   // UI3 (d): battle damage along the street ahead of the view (rubble, timber, craters, puddles, a collapsed wall)
        float x, z, yw, pt; if (sscanf(v, "%f,%f,%f,%f", &x, &z, &yw, &pt) == 4) {
          const vec3 f = App::forward(yw, 0), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
          struct R { const char* id; float d, lat, yaw; };
          static const R rub[] = {{"wat_puddles", 3.2f, 1.3f, 70}, {"rub_debris_patch", 4.6f, 2.2f, 20}, {"rub_brick_pile_s", 4.2f, 3.4f, 150}, {"mod_rubble_small", 4.8f, -3.4f, 60}, {"wat_puddles", 8, -1.4f, 10}, {"rub_brick_pile_s", 6.5f, 2.9f, 40}, {"mod_glass_pile", 7, -4.2f, 0}, {"rub_debris_patch", 9.5f, 0.6f, 75},
            {"rub_brick_pile_l", 11, -4.6f, 200}, {"mod_rubble_small", 12.5f, -2.0f, 15}, {"mod_asphalt_crater", 15, 1.4f, 0}, {"rub_beam_tile_heap", 17, -3.9f, 120},
            {"rub_brick_pile_m", 19, 3.6f, 300}, {"mod_wall_chunks", 21, -5.2f, 30}, {"wat_puddles", 23, 2.4f, 160}, {"rub_brick_pile_s", 26, -3.1f, 90},
            {"mod_collapsed_slab", 29, 6.0f, 80}, {"rub_debris_patch", 31, -0.8f, 10}, {"mod_rubble_large", 35, 4.2f, 220}, {"rub_collapsed_wall", 40, -6.3f, 0},
            {"mod_asphalt_crater", 44, -1.5f, 50}, {"wat_crater_pool", 50, 1.2f, 0}, {"rub_brick_pile_l", 55, -4.0f, 140}};
          int n = 0;
          for (const R& q : rub) {
            if (!a.lib.find(q.id)) continue;
            vec3 p = vec3(x, 0, z) + f * q.d + r * q.lat;
            Instance in; in.id = a.doc.newId(); in.assetId = q.id; in.pos = vec3(p.x, a.terrain.height(p.x, p.z), p.z); in.yaw = yw + q.yaw;
            a.doc.instances.push_back(in); ++n;
          }
          a.syncRuntime(); logf("UI2 backdrop: %d rubble pieces in the street", n);
        }
      }
      chk(startMode(a, GameMode::TDM, 24, 999, 2, 1, fs::path()), "UI2 backdrop: TDM starts (%s)", gGame->setupError.c_str());   // the map opened above (with its rubble)
      a.shotsNoViewModel = true; a.R.wx.forced = 1; a.R.wx.fogForced = 3; a.R.wx.snap();
      settings().s.hour = 19.6f; settings().apply(a);
      return false;
    }
    Soldier* L = gGame->local(); if (!L) return t > 20;
    L->hp = 100; L->protectUntil = gGame->t + 1;
    if (t < 3.0f) return false;
    if (AiCore* A = aiCore()) A->frozen = true;
    if (bot < 0 && t < 4.5f) {
      if (const char* v = std::getenv("NMK_UI2_VIEW")) { float x, z; if (sscanf(v, "%f,%f,%f,%f", &x, &z, &yaw0, &pitch0) == 4) Physics::setCharacterPos(vec3(x, a.terrain.height(x, z) + 0.05f, z)); }
      else { yaw0 = a.eyeYaw; pitch0 = 2; }
      for (auto& s : gGame->sold) if (s.alive && s.slot != gGame->localSlot && s.team == L->team) { bot = s.slot; break; }
      if (bot < 0) for (auto& s : gGame->sold) if (s.alive && s.slot != gGame->localSlot && !s.zombie) { bot = s.slot; break; }   // frozen: an enemy stands in as well
      logf("UI2 backdrop: soldier slot %d of %zu", bot, gGame->sold.size());
    }
    a.eyeYaw = yaw0; a.eyePitch = pitch0;
    if (survey) {
      for (int k = 0; k < 4; ++k) if (t > 5 + k && t < 5.05f + k) { a.eyeYaw = yaw0 + 90.0f * k; a.uiShot = outDir() / ("ui2_survey_" + std::to_string(k) + ".png"); }
      for (int k = 0; k < 4; ++k) if (t > 4.5f + k && t < 5.0f + k) a.eyeYaw = yaw0 + 90.0f * k;
      if (t < 9.5f) return false;
    } else if (std::getenv("NMK_UI2_MICRO")) {   // Settings > Microcontrast at 0 / 50 / 100 % on the same frame (no soldier moved)
      a.hideUi = true;
      const float lv[3] = {0.0f, 0.5f, 1.0f};
      static int fr = 0; if (t < 6.0f) fr = 0;
      if (t >= 6.0f && fr < 3) { a.R.q.microcontrast = lv[fr]; a.uiShot = outDir() / ("ui2_micro_" + std::to_string((int)(lv[fr] * 100)) + ".png"); ++fr; }   // three consecutive frames: the same view
      if (fr < 3 || t < 6.2f) return false;
      a.R.q.microcontrast = settings().s.microcontrast;
    } else {
      if (bot >= 0) {   // centre-left of the frame, 3.4 m out, turned three quarters to the camera
        Soldier& B = gGame->sold[bot]; const vec3 e = a.eyePos(); const vec3 f = App::forward(yaw0, 0), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
        vec3 p = t < 6.6f ? e + f * 3.4f - r * 0.6f : e + f * 60.0f + vec3(0, -400, 0);
        p.y = t < 6.6f ? a.terrain.height(p.x, p.z) : p.y; B.feet = p; if (B.agent >= 0) Physics::setAgentPos(B.agent, p);   // frozen bots read their feet back from the agent B.vel = vec3(0); B.yaw = yaw0 + 25.0f; B.protectUntil = gGame->t + 1;
      }
      a.hideUi = true;
      if (t > 6.0f && t < 6.05f) a.uiShot = outDir() / "ui2_backdrop_bot.png";
      if (t > 7.2f && t < 7.25f) a.uiShot = outDir() / "ui2_backdrop_empty.png";
      if (t < 7.6f) return false;
    }
    a.hideUi = false; a.shotsNoViewModel = false; if (AiCore* A = aiCore()) A->frozen = false; bot = -1;
    return true; });
  // UI2 main menu captures (NMK_UI2_MENU): the menu as launched, Play, Character, Credits, Settings over it -> ui2_menu_*.png
  push("ui2 menu", [](App& a, float t) {
    if (!std::getenv("NMK_UI2_MENU")) return true;
    static int ph = 0; static float t0 = 0; static double r0 = 0;   // r0: real time of the last phase (UI fades run on real time; tests run faster)
    auto shot = [&](const char* n) { a.uiShot = outDir() / (std::string("ui2_menu_") + n + ".png"); };
    auto at = [&](float d) { return t > t0 + d && glfwGetTime() > r0 + std::min(d, 0.6f); };   // sim time and real time (the UI fades)
    if (t == 0) { ph = 0; t0 = 0; r0 = glfwGetTime(); if (a.mode == Mode::Explore) a.exitExplore(); mainMenuOpen(a); return false; }
    switch (ph) {
      case 0: if (at(2.5f)) { shot("1_main"); ph = 1; t0 = t; r0 = glfwGetTime(); } break;
      case 1: if (at(0.2f)) { mainMenuGo(a, 1); ph = 2; t0 = t; r0 = glfwGetTime(); } break;
      case 2: if (at(0.8f)) { shot("2_play"); ph = 3; t0 = t; r0 = glfwGetTime(); } break;
      case 3: if (at(0.2f)) { mainMenuGo(a, 2); ph = 4; t0 = t; r0 = glfwGetTime(); } break;
      case 4: if (at(1.5f)) { shot("3_character"); ph = 40; t0 = t; r0 = glfwGetTime(); } break;
      case 40: if (at(0.2f)) { settings().s.character = "troll"; ph = 41; t0 = t; r0 = glfwGetTime(); } break;   // UI3 (h): a big body is framed whole
      case 41: if (at(1.2f)) { shot("3b_character_big"); ph = 43; t0 = t; r0 = glfwGetTime(); } break;   // (a shot is of this frame: state changes wait for the next phase)
      case 43: if (at(0.2f)) { settings().s.character.clear(); mainMenuCharTab(1); ph = 42; t0 = t; r0 = glfwGetTime(); } break;
      case 42: if (at(1.5f)) { shot("3c_ai_types"); ph = 44; t0 = t; r0 = glfwGetTime(); } break;
      case 44: if (at(0.2f)) { mainMenuCharTab(0); ph = 5; t0 = t; r0 = glfwGetTime(); } break;
      case 5: if (at(0.2f)) { mainMenuGo(a, 6); ph = 6; t0 = t; r0 = glfwGetTime(); } break;
      case 6: if (at(0.8f)) { shot("4_credits"); ph = 7; t0 = t; r0 = glfwGetTime(); } break;
      case 7: if (at(0.2f)) { mainMenuGo(a, -1); mainMenuGo(a, 4); ph = 70; t0 = t; r0 = glfwGetTime(); } break;   // UI4: the Guns page
      case 70: if (at(1.0f)) { shot("4b_guns"); ph = 71; t0 = t; r0 = glfwGetTime(); } break;
      case 71: if (at(0.2f)) { mainMenuGo(a, -1); mainMenuGo(a, 3); ph = 8; t0 = t; r0 = glfwGetTime(); } break;
      case 8: if (at(0.8f)) { shot(("5_settings_" + std::to_string(gSettingsTab)).c_str()); ph = gSettingsTab < 4 ? 80 : 9; t0 = t; r0 = glfwGetTime(); } break;   // UI4: every tab
      case 80: if (at(0.2f)) { ++gSettingsTab; ph = 8; t0 = t; r0 = glfwGetTime(); } break;
      case 9: if (at(0.2f)) { gSettingsTab = 0; settings().open = false; mainMenuGo(a, 0); ph = 10; t0 = t; r0 = glfwGetTime(); } break;
      case 10: if (at(0.5f)) { chk(!mainMenuActive() && a.mode == Mode::Creative, "UI2: Create Map closes the menu into Creative"); mainMenuOpen(a); mainMenuGo(a, 1); gUi.showSetup = true; gUi.setupMode = 0; ph = 11; t0 = t; r0 = glfwGetTime(); } break;
      case 11: if (at(0.3f)) {   // UI3 (a): Play > Singleplayer setup > a default map picked, as w5PlayMapPicker does it
                 const TemplateInfo* tp = a.findTemplate("newtown");
                 chk(tp && mainMenuActive(), "UI3: menu up on Play with the setup window before the load");
                 if (tp) { navNewMapSized(a, *tp, tp->name, vec2(tp->width, tp->depth)); }
                 chk(!mainMenuActive() && a.mode == Mode::Creative && a.doc.templateId == "newtown" && gUi.showSetup, "UI3: loading a map from Play leaves the menu: the editor shows the map, the setup window stays to start it (%s)", a.doc.templateId.c_str());
                 gUi.showSetup = false; return true; } break;
    }
    return t > 600; });   // sim time races ahead under the menu (no 3D render); the phases wait on real time too
  // UI4 loading stills (NMK_UI4_STILLS): every ready-to-play map from its own editor camera, no UI ->
  // selftest_out/ui4_still_<id>.png (made into content/templates/<id>.loading.jpg for the loading screen)
  push("ui4 stills", [](App& a, float t) {
    if (!std::getenv("NMK_UI4_STILLS")) return true;
    static std::vector<std::string> ids; static size_t k = 0; static double r0 = 0; static int ph = 0; static bool all = false;
    if (t == 0) {
      ids.clear(); k = 0; ph = 0;
      all = std::string(std::getenv("NMK_UI4_STILLS")) == "all";   // UI6: else only the maps still missing one
      for (auto& tp : a.templates) if (tp.available && tp.raw.value("playable", false) && fs::exists(contentDir() / "templates" / (tp.id + ".nmap.json")) && (all || !fs::exists(contentDir() / "templates" / (tp.id + ".loading.jpg")))) ids.push_back(tp.id);
      if (a.mode == Mode::Explore) a.exitExplore();
      gUi.showOverlay = false;   // no scenario lines / markers in the stills
    }
    if (k >= ids.size()) { a.hideUi = false; gUi.showOverlay = true; return true; }
    if (ph == 0) { a.openMap(contentDir() / "templates" / (ids[k] + ".nmap.json")); a.hideUi = true; r0 = glfwGetTime(); ph = 1; if (!all) navFrameMap(a); return false; }   // UI6: new maps from the Home view (some saved cameras face the haze)
    if (ph == 1 && glfwGetTime() > r0 + 4.5) { a.uiShot = outDir() / ("ui4_still_" + ids[k] + ".png"); ph = 2; return false; }
    if (ph == 2) { ++k; ph = 0; }
    return t > 900; });
  // UI7 loading stills: an eye-level shot inside each playable map (NMK_UI7_STILLS=1, or a comma list of map ids).
  // The pose: NMK_UI7_POSES (tools/ui7_still_poses.json) {id: {eye:[x,y,z], at:[x,y,z], hour, weather}} where given,
  // else the start marker (or the first spawn) at 1.7 m looking at the nearest objective (else the map centre); late
  // afternoon sun (18:00), clear, environmental fog off; eyeG / atG: [x, z, height above the terrain], no UI / overlays -> ui7_still_<id>.png (the pose is logged)
  push("ui7 stills", [](App& a, float t) {
    if (!std::getenv("NMK_UI7_STILLS")) return true;
    static std::vector<std::string> ids; static size_t k = 0; static double r0 = 0; static int ph = 0; static json poses;
    if (t == 0) {
      ids.clear(); k = 0; ph = 0; poses = json::object();
      if (const char* pf = std::getenv("NMK_UI7_POSES")) { try { poses = json::parse(readTextFile(pf)); } catch (std::exception& e) { logf("ui7: poses %s: %s", pf, e.what()); } }
      const std::string only = std::getenv("NMK_UI7_STILLS");
      for (auto& tp : a.templates) if (tp.available && tp.raw.value("playable", false) && fs::exists(contentDir() / "templates" / (tp.id + ".nmap.json")))
        if (only == "1" || ("," + only + ",").find("," + tp.id + ",") != std::string::npos) ids.push_back(tp.id);
      if (a.mode == Mode::Explore) a.exitExplore();
      gUi.showOverlay = false;
    }
    if (k >= ids.size()) { a.hideUi = false; a.hideEditorOverlays = false; gUi.showOverlay = true; return true; }
    if (ph == 0) {
      const std::string& id = ids[k];
      a.openMap(contentDir() / "templates" / (id + ".nmap.json"));
      a.hideUi = true; a.hideEditorOverlays = true; gUi.showOverlay = false;
      const json p = poses.contains(id) ? poses[id] : json::object();
      auto v3 = [](const json& j, vec3 d) { return j.is_array() && j.size() == 3 ? vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : d; };
      Scenario& sc = gUi.scenario(a);
      vec3 eye = a.doc.start.set ? a.doc.start.pos : !sc.spawns.empty() ? sc.spawns[0].pos : vec3(0);
      eye.y = a.terrain.height(eye.x, eye.z) > eye.y - 0.5f ? std::max(eye.y, a.terrain.height(eye.x, eye.z)) : eye.y;
      eye.y += 1.7f;
      vec3 at = vec3(0, eye.y, 0); float best = 1e9f;
      for (auto& o : sc.objectives) { float d = glm::length(vec2(o.pos.x - eye.x, o.pos.z - eye.z)); if (d > 30 && d < best) { best = d; at = o.pos + vec3(0, 2, 0); } }
      eye = v3(p.value("eye", json()), eye); at = v3(p.value("at", json()), at);
      auto g3 = [&](const json& j, vec3 d) { return j.is_array() && j.size() == 3 ? vec3(j[0].get<float>(), a.terrain.height(j[0].get<float>(), j[1].get<float>()) + j[2].get<float>(), j[1].get<float>()) : d; };
      eye = g3(p.value("eyeG", json()), eye); at = g3(p.value("atG", json()), at);   // [x, z, metres above the terrain]
      vec3 d = glm::normalize(at - eye);
      a.camPos = eye; a.camYaw = glm::degrees(std::atan2(-d.x, -d.z)); a.camPitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
      a.R.lt.hour = p.value("hour", 18.0f); a.R.wx.target = p.value("weather", 0); a.R.wx.fog = 0; a.R.wx.userSet = true;
      logf("ui7 still %s: eye %.1f %.1f %.1f at %.1f %.1f %.1f (yaw %.1f pitch %.1f) hour %.1f", id.c_str(), eye.x, eye.y, eye.z, at.x, at.y, at.z, a.camYaw, a.camPitch, a.R.lt.hour);
      r0 = glfwGetTime(); ph = 1; return false;
    }
    if (ph == 1 && glfwGetTime() > r0 + 1.5 && poses.contains(ids[k]) && poses[ids[k]].contains("faceSun")) {   // UI7: turn relative to the sun (0: into it, 90: side light)
      const vec3 s = a.R.sunDir; const float off = poses[ids[k]]["faceSun"].get<float>();
      a.camYaw = glm::degrees(std::atan2(-s.x, -s.z)) + off; a.camPitch = poses[ids[k]].value("pitch", 3.0f);
      logf("ui7 still %s: facing the sun %+.0f deg (yaw %.1f, sun %.2f %.2f %.2f)", ids[k].c_str(), off, a.camYaw, s.x, s.y, s.z);
      poses[ids[k]].erase("faceSun");
    }
    if (ph == 1 && glfwGetTime() > r0 + 5.0) { a.uiShot = outDir() / ("ui7_still_" + ids[k] + ".png"); ph = 2; return false; }
    if (ph == 2) { ++k; ph = 0; }
    return t > 1200; });
  push("ai3 setup", [](App& a, float t) {
    if (!std::getenv("NMK_AI3_SHOT")) return true;
    if (t == 0) {
      gUi.rules = ScenarioRules{}; gUi.rulesInit = true; gUi.rules.mode = GameMode::TDM;
      auto& c = gUi.rules.aiCount; c[0][(int)AiType::Soldiers] = 40; c[0][(int)AiType::Golems] = 5; c[1][(int)AiType::Zombies] = 200; c[1][(int)AiType::Abominations] = 1;
      gUi.showSetup = true; gUi.setupMode = 0;
      int given[kAiTypeCount] = {}, first[4] = {}; int n0[2] = {}, tot = 0;
      for (int k = 0; k < 201; ++k) { AiType x = aiTypeNext(c[1], 1, given); if (k < 4) first[k] = (int)x; }
      n0[0] = given[(int)AiType::Zombies]; n0[1] = given[(int)AiType::Abominations]; for (int k = 0; k < kAiTypeCount; ++k) tot += given[k];
      chk(n0[0] == 200 && n0[1] == 1 && tot == 201, "AI3: 200 zombies + 1 abomination typed -> %d zombies, %d abominations of %d spawned (first four types %d %d %d %d)", n0[0], n0[1], tot, first[0], first[1], first[2], first[3]);
      int g2[kAiTypeCount] = {}; for (int k = 0; k < 90; ++k) aiTypeNext(c[0], 1, g2);   // twice the counted AI: proportions kept
      chk(g2[(int)AiType::Soldiers] == 80 && g2[(int)AiType::Golems] == 10, "AI3: 90 AI from 40 soldiers / 5 golems keep the share: %d / %d", g2[(int)AiType::Soldiers], g2[(int)AiType::Golems]);
      return false;
    }
    if (t < 1.0f) return false;
    if (t < 1.1f) { a.uiShot = outDir() / "ai3_setup.png"; return false; }
    chk(gUi.rules.teamPlayers[0] == 46 && gUi.rules.teamPlayers[1] == 201, "AI3: TDM team sizes from the counts: %d (45 AI + you) / %d (201 AI)", gUi.rules.teamPlayers[0], gUi.rules.teamPlayers[1]);
    gUi.showSetup = false;
    return true; });
  push("br2 inventory", [](App& a, float t) {
    if (!std::getenv("NMK_MODES_ONLY_BR2")) return true;
    if (t == 0) { chk(startMode(a, GameMode::Solo, 4, 99), "BR2: match starts"); gGame->match.rules.mode = GameMode::BattleRoyale; lootBegin(*gGame); return false; }
    Soldier* L = gGame->local();
    if (t < 3 || !L || !L->alive) return t > 30 ? (chk(false, "BR2: local player spawned"), true) : false;
    static bool filled = false;
    if (!filled) {
      filled = true;
      Kit& k = *kitOf(L->slot);
      k.ammo[AM_762] = 90; k.ammo[AM_9MM] = 64; k.items["br_bandage"] = 3; k.items["br_medkit"] = 1; k.items["br_sight_4x"] = 1;
      k.helmet = 2; k.helmetDur = 150; k.vest = 1; k.vestDur = 200; k.backpack = 1;
      for (const char* id : {"kar98k", "mp40", "luger", "mk2_grenade", "molotov"}) { int w = a.weaponIndex(id); if (w >= 0) a.carried.push_back(w); }
      a.capCarried(a.carried);
      a.wstate["kar98k"].att[ATT_SIGHT] = 2; a.wstate["kar98k"].att[ATT_STOCK] = 1; a.wstate["mp40"].att[ATT_MUZZLE] = 1; a.wstate["mp40"].att[ATT_MAG] = 1;
      a.wstate["mk2_grenade"].loaded = 1; a.wstate["mk2_grenade"].containers = {1}; a.wstate["molotov"].loaded = 1;
      a.selectWeapon(a.weaponIndex("kar98k"));
      chk(lootFloor().size() > 0, "BR2: %zu floor items placed on the village", lootFloor().size());
      // BR5: a geared bot dies; its case comes to the player's feet and opens
      Soldier* B = nullptr; for (auto& s : gGame->sold) if (s.alive && !s.localPlayer) { B = &s; break; }
      if (!B) { chk(false, "BR5: a bot to kill"); return true; }
      Kit& bk = *kitOf(B->slot);
      B->weapon = a.weaponIndex("stg44"); if (B->weapon < 0) B->weapon = a.weaponIndex("mp40");
      B->ws = WeaponState{}; B->ws.loaded = 30; B->ws.containers = {30, 30}; B->ws.att[ATT_SIGHT] = 1; B->frags = 2;
      int pistol = a.weaponIndex("luger"); B->carried = {B->weapon}; if (pistol >= 0) { B->carried.push_back(pistol); B->wsBy[pistol].loaded = 8; }   // BR6: a second gun
      bk.helmet = 3; bk.helmetDur = 200; bk.vest = 2; bk.vestDur = 180; bk.backpack = 2; bk.items["br_bandage"] = 2; bk.items["br_medkit"] = 1; bk.items["br_suppressor"] = 1; bk.items["br_sight_reddot"] = 1;
      size_t before = lootCases().size();
      gGame->killSoldier(B->slot, L->slot, vec3(0, 0, 1));
      const LootCase* dc = lootCases().size() == before + 1 ? &lootCases().back() : nullptr;
      int guns = 0, rounds = 0; if (dc) for (auto& f : dc->items) { guns += f.weapon >= 0; if (f.id == "ammo:" + std::to_string(AM_556)) rounds = f.count; }
      chk(dc && guns == (pistol >= 0 ? 2 : 1) && rounds == 50 && kitOf(B->slot)->helmet == 0,
          "BR6: the dead bot's crate holds %zu items: %d guns, %d rounds for the StG it held (want 50), its kit is empty", dc ? dc->items.size() : 0, guns, rounds);
      LootCase& lc = lootCases().back(); lc.pos = L->feet + App::forward(a.eyeYaw, 0) * 1.2f; lc.yaw = a.eyeYaw;
      lootOpenCase(lc.uid);
      return false;
    }
    if (t > 4 && t < 4.05f) a.uiShot = outDir() / "br6_crate.png";   // uiShot: after ImGui, so the window is in the picture
    if (t < 5) return false;
    static bool taken = false;
    if (!taken) {   // the rules on the crate's items, then the floor labels
      taken = true;
      LootCase& lc = lootCases().back(); Kit& k = *kitOf(L->slot); std::string msg;
      auto uidOf = [&](const char* id) { for (auto& f : lc.items) if (f.id == id) return f.uid; return 0u; };
      bool took = lootTakeFromCase(a, uidOf("br_helmet_3"), true, msg);
      bool bagged = std::any_of(k.gear.begin(), k.gear.end(), [](const FloorItem& g) { return g.id == "br_helmet_2"; });
      chk(took && k.helmet == 3 && bagged, "BR6: a level-3 helmet is worn at once, the old level-2 one goes into the bag (%s)", msg.c_str());
      int sight0 = a.wstate["kar98k"].att[ATT_SIGHT], dot0 = k.items.count("br_sight_reddot") ? k.items["br_sight_reddot"] : 0;
      lootTakeFromCase(a, uidOf("br_sight_reddot"), true, msg);
      lootTakeFromCase(a, uidOf("br_suppressor"), true, msg);
      chk(a.wstate["kar98k"].att[ATT_SIGHT] == sight0 && k.items["br_sight_reddot"] == dot0 + 1 && k.items["br_suppressor"] >= 1,
          "BR6: a sight and a suppressor go into the bag; the held gun keeps its sight (%d)", a.wstate["kar98k"].att[ATT_SIGHT]);
      size_t n0 = lc.items.size();
      std::vector<uint32_t> uids; for (auto& f : lc.items) uids.push_back(f.uid);
      for (uint32_t u : uids) lootTakeFromCase(a, u, false, msg);
      chk(lc.items.size() < n0 && kitWeight(&a, k, &a.carried) <= kitCapacity(k) + 0.01f,
          "BR6: Take all moved %zu of %zu items, weight %.0f / %.0f", n0 - lc.items.size(), n0, kitWeight(&a, k, &a.carried), kitCapacity(k));
      lootOpenCase(0);
      // four items lying together in front of the player, the crosshair on the middle one
      vec3 f = App::forward(a.eyeYaw, 0), r(-f.z, 0, f.x), at = L->feet + f * 1.5f;
      const char* ids[4] = {"br_medkit", "ammo:3", "br_sight_4x", "br_bandage"};
      vec3 off[4] = {vec3(0), r * 0.35f, -r * 0.3f + f * 0.25f, r * 0.15f - f * 0.3f};
      for (int i = 0; i < 4; ++i) { lootDropItem(at, ids[i], i == 1 ? 30 : 1); lootFloor().back().pos = at + off[i] + vec3(0, 0.02f, 0); }
      for (auto& c : lootCases()) c.pos = L->feet - f * 30.0f;   // the crate out of the way
      vec3 e = a.eyePos(); a.eyePitch = degrees(std::atan2(at.y + 0.1f - e.y, glm::length(vec2(at.x - e.x, at.z - e.z))));
      return false;
    }
    if (t > 6 && t < 6.05f) a.uiShot = outDir() / "br6_labels.png";
    if (t < 6.5f) return false;
    // BR9: the inventory with its icons, then one of every floor item laid out on bare ground (grass off for the shot)
    static int br9 = 0;
    if (br9 == 0) { br9 = 1; lootSetInventory(true); return false; }
    if (br9 == 1) { if (t > 7.2f && t < 7.25f) a.uiShot = outDir() / "br9_inventory.png"; if (t < 7.5f) return false; br9 = 2; lootSetInventory(false);
      for (int i = 0; i < 4; ++i) lootFloor().pop_back();   // the cluster
      std::vector<std::string> ids;   // the small things nearest (labels show within 3.5 m), armour at the back
      for (const char* id : {"br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x", "br_mag_ext", "br_suppressor", "br_muzzle_brake", "br_recoil_pad", "br_frag", "br_smoke", "br_molotov", "br_bandage", "br_medkit"}) ids.push_back(id);
      for (int k = 0; k < AM_COUNT; ++k) ids.push_back("ammo:" + std::to_string(k));
      for (const char* id : {"br_vest_1", "br_helmet_1", "br_vest_l1", "br_helmet_2", "br_vest_2", "br_helmet_3", "br_vest_3", "br_backpack", "br_backpack_mil"}) ids.push_back(id);
      vec3 f = App::forward(a.eyeYaw, 0), r(-f.z, 0, f.x);
      const int cols = 6;
      for (size_t i = 0; i < ids.size(); ++i) {
        vec3 p = L->feet + f * (1.3f + 0.55f * (float)(i / cols)) + r * (0.5f * ((float)(i % cols) - (cols - 1) * 0.5f));
        lootDropItem(p, ids[i], ids[i].rfind("ammo:", 0) == 0 ? ammoBox(std::atoi(ids[i].c_str() + 5)) : 1);
        FloorItem& it = lootFloor().back(); it.pos = vec3(p.x, a.terrain.height(p.x, p.z) + 0.01f, p.z); it.yaw = a.eyeYaw + 90;
      }
      chk(ids.size() == 29, "BR9: %zu floor item types laid out", ids.size());
      a.grass.quality = GrassQuality::Off;
      vec3 e = a.eyePos(), mid = L->feet + f * 2.4f; a.eyePitch = degrees(std::atan2(mid.y - e.y, glm::length(vec2(mid.x - e.x, mid.z - e.z))));
      return false; }
    if (t > 8.5f && t < 8.55f) a.uiShot = outDir() / "br9_floor.png";
    if (t < 9) return false;
    a.grass.quality = GrassQuality::Medium;
    if (std::getenv("NMK_BR10_RAILFIT")) { lootRailFit(a, outDir() / "br10_rail_fit.json"); return true; }   // the rail calibration only
    if (t < 10) { lootAttachSheetUi(a); if (t > 9.6f && t < 9.65f) a.uiShot = outDir() / "br10_attach_sheet.png"; return false; }   // BR10: every gun with its attachments
    if (t < 11) { lootAttachSheetUi(a, true); if (t > 10.6f && t < 10.65f) a.uiShot = outDir() / "br10_attach_zoom.png"; return false; }   // and close on the optic
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
    else if (std::getenv("NMK_UI7_STILLS")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ui7 stills" && s.name != "done"; }), seq.end());
    else if (std::getenv("NMK_UI4_STILLS")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ui4 stills" && s.name != "done"; }), seq.end());
    else if (std::getenv("NMK_UI2_MENU")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ui2 menu" && s.name != "done" && !(s.name == "ui2 backdrop" && std::getenv("NMK_UI2_BACKDROP")) && !(s.name == "ui1p screens" && std::getenv("NMK_UI1_POLISH")); }), seq.end());   // with NMK_UI2_BACKDROP too: one run for both (GPU budget)
    else if (std::getenv("NMK_UI2_BACKDROP")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ui2 backdrop" && s.name != "done"; }), seq.end());
    else if (std::getenv("NMK_UI1_POLISH")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ui1p screens" && s.name != "done" && !(s.name == "ui2 menu" && std::getenv("NMK_UI2_MENU")); }), seq.end());   // UI2: with the menu pages in one run
    else if (std::getenv("NMK_UI1_SHOTS")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name.rfind("ui1 ", 0) != 0 && s.name != "done"; }), seq.end());
    else if (std::getenv("NMK_AI3_SHOT")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "ai3 setup" && s.name != "done"; }), seq.end());
    else if (std::getenv("NMK_MODES_ONLY_BR2")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "br2 inventory" && s.name != "done"; }), seq.end());   // BR2 inventory capture
    else if (std::getenv("NMK_MODES_ONLY_100")) seq.erase(std::remove_if(seq.begin(), seq.end(), [](const Step& s) { return s.name != "100 ai" && s.name != "200 ai" && s.name != "500 ai" && s.name != "1000 ai" && s.name != "done"; }), seq.end());   // just the crowds
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
