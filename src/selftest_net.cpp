// --selftest-net <role> [port]: LAN test, one process per role on this PC (tools/run_net_test.sh starts them):
//   host  hosts the 3-point village, starts when client A is ready, damages a wall and starts smoke before B arrives,
//         checks validation counters and the clients' reported world state, then ends the session
//   a     joins the lobby with simulated 60 ms delay + 5 % loss, readies, plays (moves, fires, an illegal teleport and
//         over-rate fire), reports what it saw
//   b     joins late (mid-match): must receive the damaged wall, the smoke at its true age and the match state
//   c     joins with a different map: must be refused with the map message
//   d     joins when the lobby is full: must be refused with the full message
// Localhost windows do not test LAN discovery across machines, firewalls or real network quality (docs/multiplayer.md).
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "net.h"
#include <functional>
#include <cmath>

namespace {
std::string role = "host"; int port = 27970;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
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
  if (g && !g->gren.clouds.empty()) { j["cloudAge"] = g->t - g->gren.clouds.back().start; j["cloudStart"] = g->gren.clouds.back().start; j["cloudDense"] = g->gren.clouds.back().dense; }
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
      push("smoke before B", [this](App& a, float t) {
        if (gNet->players.size() < 3 && t < 60) return false;   // B is connecting: start the smoke so it is live at the late join
        gGame->gren.tun.smokeDense = 60;                       // long dense phase for the test window (data, not code)
        gGame->gren.addCloud(vec3(0, 0, 40), gGame->t, 99);
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
        bool haveB = ps.count(2) && ps[2].value("final", false), haveA = ps.count(1) && ps[1].value("final", false);
        if (t < 180 && !(haveA && haveB)) return false;
        if (t < 20) return false;
        check(n.players.size() == 3, "late joiner B is in the match (" + std::to_string(n.players.size()) + " players)");
        char b[240]; std::snprintf(b, sizeof b, "server-validated client requests: %d shots accepted, %d requests rejected (over-rate fire, teleport)", n.acceptedShots, n.rejectedRequests);
        check(n.acceptedShots > 5 && n.rejectedRequests > 0, b);
        auto stats = n.peerStats();
        std::string hw = std::to_string(wallHash(a));
        for (auto& [peer, js] : stats) {
          std::string nm = peer == 1 ? "A" : peer == 2 ? "B" : std::to_string(peer);
          check(js.value("wallHash", "") == hw, "client " + nm + " wall damage matches the host (replayed events, hash " + hw.substr(0, 8) + ")");
          if (js.contains("cloudStart") && !gGame->gren.clouds.empty()) {
            // the replicated cloud carries the host's start time and durations: its age on any PC is (host time - start)
            double hs = gGame->gren.clouds.back().start, cs = js["cloudStart"];
            std::snprintf(b, sizeof b, "client %s smoke cloud start %.3f s (host %.3f s), dense phase %.0f s (host %.0f s), age there %.1f s", nm.c_str(), cs, hs, js.value("cloudDense", 0.0), gGame->gren.clouds.back().dense, js.value("cloudAge", 0.0));
            check(std::fabs(cs - hs) < 1e-3 && std::fabs(js.value("cloudDense", 0.0) - gGame->gren.clouds.back().dense) < 1e-3, b);
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
        return true; });
      push("end", [this](App& a, float t) {
        gNet->leave("test over");
        check(gNet->role == Net::NONE, "host ended the session (clients are told and return to the menu)");
        a.exitExplore();
        return true; });
    } else {
      bool lag = role == "a";
      if (lag) { n.simLagMs = 60; n.simLoss = 0.05f; }
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
        push("spawned", [this](App& a, float t) {
          Soldier* L = gGame ? gGame->local() : nullptr;
          if (L && L->alive) { check(true, "spawned by the host (life " + std::to_string(L->lifeId) + ")"); return true; }
          if (t > 120) { check(false, "never spawned"); return true; }
          return false; });
        push("play", [this](App& a, float t) {
          // walk, fire at a wall a few times, then misbehave: over-rate fire and a teleport
          a.scriptedInput.move = vec2(0, t < 3 ? 1.0f : 0.0f);
          a.scriptedInput.fire = std::fmod(t, 0.7f) < 0.05f && t < 6;
          if (t > 6.0f && t < 6.02f) { for (int k = 0; k < 6; ++k) gNet->clientFire(a.eyePos(), App::forward(a.eyeYaw, -10), a.activeWeapon, false); }
          if (t > 7.0f && t < 7.02f) { auto c = Physics::character(); Physics::setCharacterPos(c.pos + vec3(30, 0, 0)); }
          if (t < 12) return false;
          a.scriptedInput = ExploreInput{};
          return true; });
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
