// --selftest-net <role> [port]: LAN test, one process per role on this PC (tools/run_net_test.sh starts them):
//   host  hosts the 3-point village, starts when client A is ready, damages a wall and starts smoke before B arrives,
//         checks validation counters and the clients' reported world state, then ends the session
//   a     joins the lobby with simulated 60 ms delay + 5 % loss, readies, plays (moves, fires, an illegal teleport and
//         over-rate fire); then crews an MG 42 the host placed beside it (traverses, fires, leaves), resupplies at a
//         crate, takes a rack weapon and places a sandbag; misbehaves (someone else's gun, a gun out of reach, a
//         placement out of reach, a placement and a gun while dead); reports what it saw
//   b     joins late (mid-match, once A is done): must receive the damaged wall, the smoke at its true age, the match
//         state, and A's gun, crate, rack weapon and sandbag
//   c     joins with a different map: must be refused with the map message
//   d     joins when the lobby is full: must be refused with the full message
// Localhost windows do not test LAN discovery across machines, firewalls or real network quality (docs/multiplayer.md).
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "net.h"
#include "mpai.h"
#include <functional>
#include <fstream>
#include <cmath>
static double gTestCloudStart = -1, gTestCloudDense = 0;   // host: the smoke cloud the test starts (bots may throw others)

namespace {
std::string role = "host"; int port = 27970;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
// A and B line up through flag files in the build dir's selftest_out (one PC, test only)
fs::path flagPath(const char* n) { return fs::path("selftest_out") / n; }
void touchFlag(const char* n) { std::error_code ec; fs::create_directories("selftest_out", ec); std::ofstream(flagPath(n)) << "1"; }
bool hasFlag(const char* n) { return fs::exists(flagPath(n)); }
json gLanFlags = json::object();   // sticky report fields (A: site / lanDone / wantDie, B: lanEarly)
float gLanGunYaw = 0; vec3 gLanBagPos{0}; float gLanBagYaw = 0;
bool isSessionPiece(const Instance& i) { return i.extra.is_object() && i.extra.value("session", false); }
uint64_t tagged(App& a, const char* tag) { for (auto& i : a.doc.instances) if (i.extra.is_object() && i.extra.value("test", std::string()) == tag) return i.id; return 0; }
int rackWeapon(App& a) { int w = a.weaponIndex("m1c_garand"); return w >= 0 ? w : a.weaponIndex("m1_carbine"); }   // a US rack weapon outside the player pool
// what this PC holds of the interactive world: session placements, crate stock, guns, every soldier's weapon
json lanView(App& app) {
  json pcs = json::array(), cr = json::object(), gn = json::object(), wp = json::array();
  for (auto& i : app.doc.instances) {
    if (!isSessionPiece(i)) continue;
    pcs.push_back(i.id);
    if (app.crateStock.count(i.id)) cr[std::to_string(i.id)] = app.crateStock[i.id];
    if (StationRT* s = app.station(i.id)) gn[std::to_string(i.id)] = {gNet && gNet->gunOcc.count(i.id) ? gNet->gunOcc[i.id] : -1, s->loaded, s->reserve};
  }
  if (gGame) for (auto& s : gGame->sold) wp.push_back(s.weapon);
  return {{"pieces", pcs}, {"crates", cr}, {"guns", gn}, {"weap", wp}};
}
uint64_t wallHash(App& app) {
  uint64_t h = 1469598103934665603ull;
  for (auto& i : app.doc.instances) { auto it = app.rt.find(i.id); if (it == app.rt.end()) continue; h ^= it->second.wallEvents.size() + i.id * 31; h *= 1099511628211ull; for (auto& e : it->second.wallEvents) { h ^= e.seed; h *= 1099511628211ull; } }
  return h;
}
}
uint64_t gNetGoreProbe = 0;   // W7: spray hash of the host's probe hit
void setNetTestRole(const std::string& r, int p) { role = r; port = p; }
json netClientStats(App& app) {
  Game* g = gGame;
  json j = {{"wallHash", std::to_string(wallHash(app))}, {"clouds", g ? (int)g->gren.clouds.size() : 0}};
  if (g && !g->gren.clouds.empty()) { j["cloudAge"] = g->t - g->gren.clouds.back().start; j["cloudStart"] = g->gren.clouds.back().start; j["cloudDense"] = g->gren.clouds.back().dense;
    json all = json::array(); for (auto& c : g->gren.clouds) all.push_back({c.start, c.dense}); j["cloudList"] = all; }
  j["lan"] = lanView(app); j.update(gLanFlags);
  j["gameActive"] = g && g->active; j["isClient"] = g && g->isClient; j["mode"] = (int)app.mode; j["netRole"] = gNet ? (int)gNet->role : -1;
  { extern json goreNetStats(); j["gore"] = goreNetStats(); }   // W7: gore events received from the host
  if (g) { int alive = 0; for (auto& s : g->sold) alive += s.alive; j["alive"] = alive; j["slots"] = (int)g->sold.size(); j["phase"] = (int)g->match.phase; j["active"] = g->match.active; j["progress"] = g->match.obj.empty() ? 0.0f : g->match.obj[0].progress; j["local"] = g->localSlot; j["t"] = g->t; }
  return j;
}

void SelfTest::netUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); };
  if (stage == 2100) {
    reportName = "net_" + role;
    realTime = true;   // every process runs on the wall clock, like real play
    if (!gNet) gNet = new Net();
    Net& n = *gNet;
    fs::path map = contentDir() / "examples" / (role == "c" ? "range.nmap.json" : "village_assault_3.nmap.json");
    check(app.openMap(map), "opened " + map.filename().string());
    gUi.loaded = false;
    stage = 2101; seq.clear(); cur = 0; st = total = 0;
    if (role == "host") {
      ScenarioRules r = gUi.scenario(app).rules; r.defenderSlots = 3; r.attackerAI = 6; gUi.rules = r; gUi.rulesInit = true;   // host + A + B; D finds it full
      std::string err; check(n.host(app, err, port), "hosting on port " + std::to_string(port) + " " + err);
      push("lobby", [this](App& a, float t) {
        Net& n = *gNet;
        if (n.players.size() >= 2 && n.allReady()) { check(true, "client A joined the lobby and is ready after " + std::to_string((int)t) + " s"); return true; }
        if (t > 60) { check(false, "no ready client within 60 s"); finish(a); return true; }
        return false; });
      push("start", [this](App& a, float t) {
        gNet->startMatch(a); a.useScripted = true; a.scriptedInput = ExploreInput{};
        check(gGame && gGame->active && gNet->matchRunning, "LAN match started (" + (gGame ? gGame->setupError : std::string("no game")) + ")");
        int humans = 0; for (auto& s : gGame->match.slots) humans += s.human;
        check(humans == 2, "roster: 2 human defenders (host + A), " + std::to_string(gGame->match.slots.size() - humans) + " AI");
        return true; });
      push("world changes before B", [this](App& a, float t) {
        if (t < 6) return false;
        Physics::Hit h; vec3 o(-24, 1.2f, 20);
        if (Physics::castRayEx(o, vec3(0, 0, -1), 30, h)) a.explode(h.pos, vec3(0, 0, -1), 320, 0.9f, 1.0f, ~0ull, WALL_AP);
        check(true, "host breached the town hall wall (late joiners must see it)");
        return true; });
      push("gore probe hit", [this](App& a, float t) {   // W7: one real flesh hit with a known seed, replicated as a "gore" event
        extern uint64_t goreTestInjectHit(App&); extern uint64_t gNetGoreProbe;
        if (t < 1) return false;
        gNetGoreProbe = goreTestInjectHit(a);
        check(gNetGoreProbe != 0, "host made a flesh hit on a bot with the probe seed (spray hash " + std::to_string(gNetGoreProbe).substr(0, 8) + ")");
        return true; });
      push("props for A", [this](App& a, float t) {   // the host's own placements replicate: two MG 42s by A, one far off, a rack, a crate
        auto ps = gNet->peerStats();
        if (!(ps.count(1) && ps[1].value("site", false))) { if (t > 120) { check(false, "client A never asked for its test site"); return true; } return false; }
        int slotA = gGame->match.slotOfPeer(1);
        if (slotA < 0 || !gGame->sold[slotA].alive) return t > 120;
        vec3 feet = gGame->sold[slotA].feet;
        auto place = [&](const char* asset, float r0, float r1, const char* tag, vec3 feet) -> uint64_t {
          const AssetDef* ad = a.lib.find(asset); if (!ad) return 0;
          for (float r = r0; r <= r1; r += 0.5f) for (int k = 0; k < 12; ++k) {
            float ang = k * 30.0f; vec3 q = feet + vec3(std::sin(radians(ang)) * r, 0, std::cos(radians(ang)) * r); q.y = a.terrain.height(q.x, q.z);
            if (a.groundSlopeDeg(q) > 18 || !a.buildCheckAt(ad, q, ang, false, feet).empty()) continue;
            if (!ad->station.kind.empty()) { vec3 an = q + yawQuat(ang) * ad->station.gunner; an.y = a.terrain.height(an.x, an.z); if (Physics::capsuleBlocked(an + vec3(0, 0.1f, 0), Physics::standHeight() - 0.1f, Physics::radius())) continue; }
            auto ids = a.buildSessionPiece(ad, q, ang, false);   // the gun faces away from A: its gunner stands on A's side
            a.doc.find(ids[0])->extra["test"] = tag;
            mpaiBuildChanged(a, ids, {});
            return ids[0];
          }
          return 0;
        };
        uint64_t G1 = place("gun_mg42_embrasure", 2.0f, 3.5f, "G1", feet);
        vec3 seat = G1 ? a.stationAnchor(G1, ST_GUNNER) : feet;   // A dismounts here: the rack and crate go round it
        uint64_t R = place("rack_allied", 2.0f, 3.5f, "R", seat), C = place("crate_mixed_supply", 2.0f, 3.5f, "C", seat);
        uint64_t G2 = place("gun_mg42_embrasure", 5.0f, 9.0f, "G2", feet), G3 = place("gun_mg42_embrasure", 30.0f, 45.0f, "G3", feet);
        check(G1 && G2 && G3 && R && C, "host placed the test pieces round client A (MG 42 " + std::to_string(G1) + ", rack " + std::to_string(R) + ", crate " + std::to_string(C) + ", second MG 42 " + std::to_string(G2) + ", far MG 42 " + std::to_string(G3) + ")");
        bool own = G2 && a.occupyStation(G2, ST_GUNNER);
        check(own, "host crews the second MG 42 itself (A must be refused it)");
        return true; });
      push("A interacts", [this](App& a, float t) {
        auto ps = gNet->peerStats();
        bool done = ps.count(1) && ps[1].value("lanDone", false);
        for (int sl : {gGame->match.slotOfPeer(1), gGame->localSlot}) if (sl >= 0 && gGame->sold[sl].alive) { gGame->sold[sl].hp = 100; gGame->sold[sl].protectUntil = gGame->t + 1; }   // test only: bots must not end A's steps or the host's gun early
        if (!done && t < 180) return false;
        check(done, "client A finished its gun / crate / rack / build steps");
        Net& n = *gNet; int slotA = gGame->match.slotOfPeer(1);
        uint64_t G1 = tagged(a, "G1"), C = tagged(a, "C");
        StationRT* g1 = a.station(G1);
        auto rj = [&](const char* k) { return n.rejects.count(k) ? n.rejects[k] : 0; };
        char b[300];
        std::snprintf(b, sizeof b, "host fired A's gun rounds: %d accepted (%d refused for ammo / rate), MG 42 belt %d of 200 left", n.acceptedGunShots, rj("gfire: ammo or rate"), g1 ? g1->loaded : -1);
        check(n.acceptedGunShots >= 5 && g1 && g1->loaded <= 200 - n.acceptedGunShots, b);
        float ay = ps[1].value("gunYaw", -999.0f);
        std::snprintf(b, sizeof b, "A's traverse reached the host's gun: %.2f deg here, %.2f deg on A", g1 ? g1->yaw : -999.0f, ay);
        check(g1 && std::fabs(g1->yaw - ay) < 1.0f && std::fabs(ay) > 2.0f, b);
        check(rj("gun: taken") >= 1, "A was refused the gun the host crews (" + std::to_string(rj("gun: taken")) + ")");
        check(rj("gun: reach") >= 1, "A was refused the gun 30+ m away (" + std::to_string(rj("gun: reach")) + ")");
        const WeaponDef& gar = *a.weaponDefs[a.weaponIndex("m1_garand")];
        int stock0 = a.lib.find("crate_mixed_supply")->crate["supplies"].value(gar.container, 0);
        std::snprintf(b, sizeof b, "A resupplied at the crate: %s %d -> %d on the host; the second request (at the allowance) was refused (%d)", gar.container.c_str(), stock0, a.crateStock[C][gar.container], rj("crate: nothing moved"));
        check(a.crateStock[C][gar.container] < stock0 && rj("crate: nothing moved") >= 1, b);
        Soldier& sa = gGame->sold[slotA]; int rw = rackWeapon(a);
        check(sa.weapon == rw && std::find(sa.carried.begin(), sa.carried.end(), rw) != sa.carried.end(), "A took the " + a.weaponDefs[rw]->name + " from the rack: in hand and in its loadout on the host (" + (sa.weapon >= 0 ? a.weaponDefs[sa.weapon]->id : std::string("none")) + ")");
        int bags = 0; for (auto& i : a.doc.instances) if (isSessionPiece(i) && i.assetId == "def_sandbag_straight") ++bags;
        check(bags == 1 && rj("build: reach") >= 1, "A's sandbag stands on the host (" + std::to_string(bags) + "); its placement 40 m off was refused (" + std::to_string(rj("build: reach")) + ")");
        return true; });
      push("smoke before B", [this](App& a, float t) {
        if (gNet->players.size() < 3 && t < 60) return false;   // B is connecting: start the smoke so it is live at the late join
        gGame->gren.tun.smokeDense = 60;                       // long dense phase for the test window (data, not code)
        gGame->gren.addCloud(vec3(0, 0, 40), gGame->t, 99);
        gTestCloudStart = gGame->gren.clouds.back().start; gTestCloudDense = gGame->gren.clouds.back().dense;
        { auto& c = gGame->gren.clouds.back(); gNet->hostWorldEvent(json{{"t", "cloud"}, {"p", tojson(c.p)}, {"st", c.start}, {"seed", c.seed}, {"r", c.radius}, {"b", c.build}, {"dn", c.dense}, {"f", c.fade}}); }
        check(true, "host started a smoke cloud as B joins (A gets it live, B through the late-join state)");
        return true; });
      push("play", [this](App& a, float t) {
        Net& n = *gNet;
        static bool freed = false;
        if (t == 0) freed = false;
        // the late joiner waits for an AI defender slot to come free through the normal respawn flow: free one now
        if (!freed && n.players.size() >= 3) {
          for (auto& s : gGame->match.slots) if (s.pendingPeer >= 0 && gGame->sold[s.id].alive) { gGame->killSoldier(s.id, -1, vec3(0, 0, 1)); freed = true; }
          if (!freed) for (auto& s : gGame->match.slots) if (s.pendingPeer >= 0) freed = true;
        }
        auto ps = n.peerStats();
        static bool killedA = false; if (t == 0) killedA = false;
        if (!killedA) for (int sl : {gGame->match.slotOfPeer(1), gGame->localSlot}) if (sl >= 0 && gGame->sold[sl].alive) { gGame->sold[sl].hp = 100; gGame->sold[sl].protectUntil = gGame->t + 1; }   // test only (as above) until B has looked
        if (!killedA && ps.count(1) && ps[1].value("wantDie", false)) {   // A tests requests while dead
          int sa = gGame->match.slotOfPeer(1);
          if (sa >= 0 && gGame->sold[sa].alive) { gGame->killSoldier(sa, -1, vec3(0, 0, 1)); killedA = true; }
        }
        bool haveB = ps.count(2) && ps[2].value("final", false), haveA = ps.count(1) && ps[1].value("final", false);
        if (t < 240 && !(haveA && haveB && n.refusedJoins >= 2)) return false;   // C (map) and D (full) refused too: D joins last, so stay up for it
        if (t < 20) return false;
        check(n.players.size() == 3, "late joiner B is in the match (" + std::to_string(n.players.size()) + " players)");
        char b[240]; std::snprintf(b, sizeof b, "server-validated client requests: %d shots accepted, %d requests rejected (over-rate fire, teleport)", n.acceptedShots, n.rejectedRequests);
        check(n.acceptedShots > 5 && n.rejectedRequests > 0, b);
        auto stats = n.peerStats();
        std::string hw = std::to_string(wallHash(a));
        for (auto& [peer, js] : stats) {
          std::string nm = peer == 1 ? "A" : peer == 2 ? "B" : std::to_string(peer);
          check(js.value("wallHash", "") == hw, "client " + nm + " wall damage matches the host (replayed events, hash " + hw.substr(0, 8) + ")");
          if (js.contains("cloudList") && gTestCloudStart >= 0) {
            // the replicated cloud carries the host's start time and durations: its age on any PC is (host time - start).
            // Bots throw smoke too, so look for the host's test cloud among all the client's clouds.
            double cs = -1, cd = 0;
            try { for (auto& c : js["cloudList"]) if (c.is_array() && c.size() >= 2 && c[0].is_number() && std::fabs(c[0].get<double>() - gTestCloudStart) < 1e-3) { cs = c[0].get<double>(); cd = c[1].get<double>(); } } catch (const std::exception& e) { logf("net test: bad clouds report: %s", e.what()); }
            std::snprintf(b, sizeof b, "client %s has the host's test smoke cloud: start %.3f s (host %.3f s), dense phase %.0f s (host %.0f s); %zu clouds there", nm.c_str(), cs, gTestCloudStart, cd, gTestCloudDense, js["cloudList"].size());
            check(cs >= 0 && std::fabs(cd - gTestCloudDense) < 1e-3, b);
          } else check(false, "client " + nm + " reported no smoke cloud");
          if (peer == 1) {   // W7: A was in the match for the probe hit (B joins later and is not sent past events)
            extern uint64_t gNetGoreProbe; json gj = js.value("gore", json::object());
            std::snprintf(b, sizeof b, "client %s received %d gore events; probe hit's droplets simulated from the seed: hash %s (host %s)", nm.c_str(), gj.value("received", 0), gj.value("probe", std::string("none")).substr(0, 8).c_str(), std::to_string(gNetGoreProbe).substr(0, 8).c_str());
            check(gj.value("received", 0) >= 1 && gj.value("probe", std::string()) == std::to_string(gNetGoreProbe), b);
          }
          int sa = js.value("alive", 0), ha = 0; for (auto& s : gGame->sold) ha += s.alive;
          check(std::abs(sa - ha) <= 2, "client " + nm + " sees " + std::to_string(sa) + " living soldiers (host " + std::to_string(ha) + ")");
        }
        check(stats.size() >= 2, std::to_string(stats.size()) + " clients reported their world state");
        {   // guns, racks, crates, field defences
          json hv = lanView(a);
          for (auto& [peer, js] : stats) {
            std::string nm = peer == 1 ? "A" : peer == 2 ? "B" : std::to_string(peer);
            json lv = js.value("lan", json::object());
            check(lv.value("pieces", json()) == hv["pieces"], "client " + nm + " has the host's " + std::to_string(hv["pieces"].size()) + " placements with the host's ids " + lv.value("pieces", json()).dump());
            check(lv.value("crates", json()) == hv["crates"], "client " + nm + " crate stock matches the host " + lv.value("crates", json()).dump());
            check(lv.value("guns", json()) == hv["guns"], "client " + nm + " guns (occupant, belt, reserve) match the host " + lv.value("guns", json()).dump() + " / " + hv["guns"].dump());
          }
          if (stats.count(2)) {
            json e = stats[2].value("lanEarly", json::object()); int sa = gGame->match.slotOfPeer(1), rw = rackWeapon(a);
            bool w = e.contains("weap") && sa >= 0 && sa < (int)e["weap"].size() && e["weap"][sa] == rw;
            check(w, "late joiner B saw A holding the rack weapon on arrival");
          }
          auto rj = [&](const char* k) { return n.rejects.count(k) ? n.rejects[k] : 0; };
          check(rj("build: dead") >= 1 && rj("gun: dead") >= 1, "requests from dead A refused: placement " + std::to_string(rj("build: dead")) + ", gun " + std::to_string(rj("gun: dead")));
        }
        return true; });
      push("end", [this](App& a, float t) {
        gNet->leave("test over");
        check(gNet->role == Net::NONE, "host ended the session (clients are told and return to the menu)");
        a.exitExplore();
        return true; });
    } else {
      bool lag = role == "a";
      if (lag) { n.simLagMs = 60; n.simLoss = 0.05f; }
      if (role == "b" || role == "d") push("wait", [this](App& a, float t) {   // B arrives once A has used the gun, crate, rack and builder; D once B is in (lobby full)
        const char* f = role == "b" ? "net_a_lan.flag" : "net_b_seen.flag";
        if (hasFlag(f)) { check(true, role == "b" ? "client A's interactions are done: joining late" : "B is in the match: joining a full lobby"); return true; }
        if (t > 450) { check(false, std::string("never saw ") + f); return true; }
        return false; });
      push("join", [this, lag](App& a, float t) {
        Net& n = *gNet;
        if (t == 0) { std::string err; n.join(a, "127.0.0.1", "Client " + role, err, port); return false; }
        if (role == "c" || role == "d") {
          if (n.role == Net::NONE) {
            bool ok = role == "c" ? n.status.find("Map differs") != std::string::npos : (n.status.find("full") != std::string::npos);
            check(ok, "refused with a usable message: \"" + n.status + "\"");
            return true;
          }
          if (t > 20) { check(false, "expected a refusal, got: " + n.status); return true; }
          return false;
        }
        if (n.connected) { check(true, std::string("connected to the host lobby") + (lag ? " with 60 ms simulated delay and 5 % loss" : "")); n.setReady(true); return true; }
        if (t > 30) { check(false, "could not connect: " + n.status); return true; }
        return false; });
      if (role == "a" || role == "b") {
        push("in match", [this](App& a, float t) {
          if (gGame && gGame->active && gGame->isClient) {
            check(true, "received START and entered the match as a client (slot " + std::to_string(gGame->localSlot) + ")");
            a.useScripted = true; a.scriptedInput = ExploreInput{};
            return true;
          }
          if (t > 60) { check(false, "match never started on this client"); return true; }
          return false; });
        if (role == "b") push("late view", [this](App& a, float t) {   // the world log and snapshots carry A's doings
          if (t < 4) return false;
          if (!gGame || !gGame->active) { check(false, "late join: not in the match"); return true; }
          gLanFlags["lanEarly"] = lanView(a);
          int sa = -1; for (auto& s : gGame->match.slots) if (s.human && s.peer == 1) sa = s.id;
          int rw = rackWeapon(a), bags = 0; for (auto& i : a.doc.instances) if (isSessionPiece(i) && i.assetId == "def_sandbag_straight") ++bags;
          StationRT* g1 = a.station(tagged(a, "G1"));
          check(sa >= 0 && gGame->sold[sa].weapon == rw, "late join: A holds the rack weapon here (" + std::string(sa >= 0 && gGame->sold[sa].weapon >= 0 ? a.weaponDefs[gGame->sold[sa].weapon]->id : "?") + ")");
          check(bags == 1, "late join: A's sandbag is here (" + std::to_string(bags) + ")");
          check(g1 && g1->loaded < 200, "late join: A's MG 42 shows the rounds it fired (" + std::to_string(g1 ? g1->loaded : -1) + " left)");
          gNet->sendStats(netClientStats(a));
          touchFlag("net_b_seen.flag");
          return true; });
        push("spawned", [this](App& a, float t) {
          Soldier* L = gGame ? gGame->local() : nullptr;
          if (L && L->alive) { check(true, "spawned by the host (life " + std::to_string(L->lifeId) + ")"); return true; }
          if (t > 120) { check(false, "never spawned"); return true; }
          return false; });
        push("play", [this](App& a, float t) {
          // walk, fire at a wall a few times, then misbehave: over-rate fire and a teleport
          a.scriptedInput.move = vec2(0, t < 3 ? 1.0f : 0.0f);
          a.scriptedInput.fire = std::fmod(t, 0.7f) < 0.05f && t < 6;
          static bool burst = false, jump = false; if (t == 0) burst = jump = false;   // one-shots: a slow frame must not skip them
          if (t > 6.0f && !burst) { burst = true; for (int k = 0; k < 6; ++k) gNet->clientFire(a.eyePos(), App::forward(a.eyeYaw, -10), a.activeWeapon, false); }
          if (t > 7.0f && !jump) { jump = true; auto c = Physics::character(); Physics::setCharacterPos(c.pos + vec3(30, 0, 0)); }
          if (t < 12) return false;
          a.scriptedInput = ExploreInput{};
          return true; });
        if (role == "a") {
          push("lan site", [this](App& a, float t) {   // ask the host for the test pieces round this soldier
            static float last = -1; if (t == 0) { last = -1; gLanFlags["site"] = true; }
            if (t - last > 1) { last = t; gNet->sendStats(netClientStats(a)); }
            if (tagged(a, "G1") && tagged(a, "G2") && tagged(a, "G3") && tagged(a, "R") && tagged(a, "C")) {
              check(isSessionPiece(*a.doc.find(tagged(a, "G1"))), "the host's placements (MG 42s, rack, crate) arrived here with the host's ids");
              return true;
            }
            if (t > 150) { check(false, "the host's test placements never arrived"); return true; }
            return false; });
          push("lan gun", [this](App& a, float t) {
            uint64_t G1 = tagged(a, "G1");
            if (t == 0) {
              gNet->clientRequest(json{{"t", "gun"}, {"i", tagged(a, "G2")}, {"on", true}});   // the host's own gun: refused
              gNet->clientRequest(json{{"t", "gun"}, {"i", tagged(a, "G3")}, {"on", true}});   // 30+ m away: refused
              a.occupyStation(G1, ST_GUNNER);                                                 // through the client hook: a request
              check(a.occupied == 0, "E on a gun asks the host instead of crewing it locally");
              return false;
            }
            if (a.occupied == G1 && t > 1) { check(true, "the host gave this client the MG 42 (" + std::to_string(t).substr(0, 4) + " s)"); return true; }
            if (t > 15) { check(false, "the host never gave the gun (occupied " + std::to_string(a.occupied) + ")"); return true; }
            return false; });
          push("lan fire", [this](App& a, float t) {
            StationRT* s = a.station(a.occupied);
            if (!s) { check(false, "lost the gun before firing"); return true; }
            static int loaded0 = 0; if (t == 0) loaded0 = s->loaded;
            auto& in = a.scriptedInput; in = ExploreInput{};
            if (t < 1.0f) in.dyaw = 20.0f * a.frameDt;   // traverse 20 deg/s for 1 s: the host turns its gun at the mount's rate
            else if (t < 2.5f) in.fire = true;     // a burst, every round a request
            if (t < 5.0f) return false;
            gLanGunYaw = s->yaw;
            char b[200]; std::snprintf(b, sizeof b, "fired the MG 42: %d of %d rounds left here once the host answered, traverse %.1f deg", s->loaded, loaded0, s->yaw);
            check(s->loaded < loaded0, b);
            return true; });
          push("lan leave", [this](App& a, float t) {
            uint64_t G1 = tagged(a, "G1");
            if (t == 0) { a.leaveStation(); return false; }
            if (t < 1.5f) return false;
            int occ = gNet->gunOcc.count(G1) ? gNet->gunOcc[G1] : -1;
            check(a.occupied == 0 && occ != gGame->localSlot, "left the gun; the host's snapshot shows it free (occupant " + std::to_string(occ) + ")");
            return true; });
          push("lan crate", [this](App& a, float t) {
            static int phase = 0, stock0 = 0, r0 = 0; static float t1 = 0;
            uint64_t C = tagged(a, "C");
            if (t == 0) phase = 0;
            if (t < 4) { a.scriptedInput.fire = !a.scriptedInput.fire; return false; }   // spend rounds (press / release every frame, any frame rate): room for a clip
            a.scriptedInput = ExploreInput{};
            const WeaponDef& d = *a.weaponDefs[a.activeWeapon]; WeaponState& ws = a.wstate[d.id];
            if (phase == 0) { if (t < 7) return false; stock0 = a.crateStock[C][d.container]; r0 = ws.reserveRounds(); a.crateTransaction(C); phase = 1; t1 = t; return false; }
            if (phase == 1) {
              if (a.crateStock[C][d.container] >= stock0 && t - t1 < 10) return false;
              char b[200]; std::snprintf(b, sizeof b, "resupplied at the crate through the host: %s %d -> %d in the crate, reserve %d -> %d", d.container.c_str(), stock0, a.crateStock[C][d.container], r0, ws.reserveRounds());
              check(a.crateStock[C][d.container] < stock0 && ws.reserveRounds() >= r0, b);
              a.crateTransaction(C); phase = 2; t1 = t;   // again, at the allowance now: the host must refuse
              return false;
            }
            return t - t1 > 2; });
          push("lan rack", [this](App& a, float t) {
            int w = rackWeapon(a);
            if (t == 0) {
              a.openRack(tagged(a, "R"));
              auto ars = a.rackArsenal("US"); for (size_t k = 0; k < ars.size(); ++k) if (ars[k] == w) a.rackSel = (int)k;
              a.rackActivate();   // through the client hook: a request
              return false;
            }
            Soldier* L = gGame->local();
            if (t > 1 && a.activeWeapon == w && L && L->weapon == w) { check(true, "took the " + a.weaponDefs[w]->name + " from the rack: in hand here and on the host (snapshot)"); return true; }
            if (t > 12) { check(false, "rack swap never confirmed (in hand " + a.weaponDefs[a.activeWeapon]->id + ")"); return true; }
            return false; });
          push("lan build", [this](App& a, float t) {
            const AssetDef* sb = a.lib.find("def_sandbag_straight");
            auto bags = [&] { int n = 0; for (auto& i : a.doc.instances) if (isSessionPiece(i) && i.assetId == "def_sandbag_straight") ++n; return n; };
            if (t == 0) {
              vec3 feet = Physics::character().pos; bool found = false;
              for (float r = 3; r <= 9 && !found; r += 1) for (int k = 0; k < 12 && !found; ++k) {
                float ang = k * 30.0f; vec3 q = feet + vec3(std::sin(radians(ang)) * r, 0, std::cos(radians(ang)) * r); q.y = a.terrain.height(q.x, q.z);
                if (a.groundSlopeDeg(q) > 25 || !a.buildCheckAt(sb, q, ang, false, feet).empty()) continue;
                gLanBagPos = q; gLanBagYaw = ang; found = true;
              }
              if (!found) { check(false, "no free spot for a sandbag near this client"); return true; }
              vec3 far = feet + vec3(40, 0, 0); far.y = a.terrain.height(far.x, far.z);
              mpaiBuildRequest(a, sb, far, 0);                    // out of reach: refused
              mpaiBuildRequest(a, sb, gLanBagPos, gLanBagYaw);   // the build mode's LMB request
              return false;
            }
            if (bags() == 1) { check(true, "placed a sandbag: the host re-checked it and it arrived here with the host's id"); return true; }
            if (t > 12) { check(false, "the sandbag never appeared (" + std::to_string(bags()) + ")"); return true; }
            return false; });
          push("lan done", [this](App& a, float t) {
            gLanFlags["lanDone"] = true; gLanFlags["gunYaw"] = gLanGunYaw;
            gNet->sendStats(netClientStats(a));
            touchFlag("net_a_lan.flag");
            return true; });
          push("lan dead", [this](App& a, float t) {
            static float last = -1, t2 = 0; static int phase = 0; if (t == 0) { last = -1; phase = 0; }
            if (t - last > 1) { last = t; gNet->sendStats(netClientStats(a)); }
            Soldier* L = gGame->local();
            if (phase == 0) {   // B has seen the world as A left it: now die
              if (!hasFlag("net_b_seen.flag") && t < 200) return false;
              check(hasFlag("net_b_seen.flag"), "late joiner B reported its view");
              gLanFlags["wantDie"] = true; gNet->sendStats(netClientStats(a)); phase = 1; return false;
            }
            if (phase == 1) {
              if (L && !L->alive) {
                gNet->clientRequest(json{{"t", "build"}, {"a", "def_sandbag_straight"}, {"p", tojson(gLanBagPos + vec3(0, 0, 1.5f))}, {"y", gLanBagYaw}});
                gNet->clientRequest(json{{"t", "gun"}, {"i", tagged(a, "G1")}, {"on", true}});
                phase = 2; t2 = t; return false;
              }
              if (t > 260) { check(false, "the host never killed A"); return true; }
              return false;
            }
            if (t - t2 < 1.5f) return false;
            check(true, "dead: sent a placement and a gun request (the host must refuse both)");
            return true; });
        }
        push("report", [this](App& a, float t) {
          static float last = -1; if (t == 0) last = -1;
          if (t - last > 1.0f) { last = t; gNet->sendStats(netClientStats(a)); }   // once a second
          if (t < 6) return false;
          json s = netClientStats(a);
          s["final"] = true;
          gNet->sendStats(s);
          char b[400]; std::snprintf(b, sizeof b, "client view: %d soldiers alive of %d, objective 1 at %.1f %%, %d smoke clouds, net in %llu B / out %llu B, %llu resends [%s]",
                                     s.value("alive", 0), s.value("slots", 0), s.value("progress", 0.0f), s.value("clouds", 0), (unsigned long long)gNet->bytesIn, (unsigned long long)gNet->bytesOut, (unsigned long long)gNet->resends, s.dump().c_str());
          check(s.value("alive", 0) > 3, b);
          return true; });
        push("host ends", [this](App& a, float t) {
          static float last = -1; if (t == 0) last = -1;
          if (t - last > 1.0f && gNet->role == Net::CLIENT) { last = t; json s = netClientStats(a); s["final"] = true; gNet->sendStats(s); }
          if (gNet->role == Net::NONE) { check(gNet->status.find("ended the session") != std::string::npos || gNet->status.find("lost") != std::string::npos, "host departure handled: \"" + gNet->status + "\" and back in the menu (" + (a.mode == Mode::Creative ? "Creative" : "Explore") + ")"); return true; }
          if (t > 240) { check(false, "host never ended the session"); return true; }
          return false; });
      }
    }
    return;
  }
  if (cur >= seq.size()) { app.useScripted = false; if (gNet && gNet->role != Net::NONE) gNet->leave("test done"); finish(app); return; }
  bool done = seq[cur].fn(app, st);
  st += dt; total += dt;
  if (done) { ++cur; st = 0; }
  if (total > 600) { check(false, "net test timed out in step '" + seq[cur < seq.size() ? cur : 0].name + "'"); cur = seq.size(); }
}
