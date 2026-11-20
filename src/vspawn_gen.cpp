// VH7 --gen-vehicle-spawns: proposes each shipped template's vehicle spawns ([vspawn] JSON lines; tools/vehicles/apply_vspawns.py
// writes them into content/templates/<id>.nmap.json). Per team: from the centroid of its infantry spawns (TDM team markers,
// else the Assault markers of the first phase) step back, away from the enemy's centroid, from 120 m (x map size / 600,
// at least 30 m) toward 30 m and sideways in 8 m steps; the first spot inside the map (12 m margin) on gentle ground
// (rise < 0.3 over 3 m) where a tank fits (checkPlacement of the Abrams' footprint) and with a clear line 1.5 m up back
// toward the infantry spawn (a route in) wins; then a second one at least 14 m from it. Underground maps and maps with no
// team spawns get none.
#include "selftest.h"
#include "editor_nav.h"
#include "scenario.h"

namespace {
const char* kMaps[] = {"battle1", "chateau_town", "dday", "newtown", "omaha_easy_red", "outpost_desert", "outpost_woods", "village_assault",
                       "tunnels", "backrooms", "normandy_forest", "tank_range", "gx_test_scene"};
const char* kUnder[] = {"tunnels", "backrooms"};
int idx = 0;
}

bool vehicleSpawnGenSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--gen-vehicle-spawns") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (idx >= (int)(sizeof kMaps / sizeof kMaps[0])) { a.selftest->exitCode = 0; a.selftest->done = true; return true; }
  std::string id = kMaps[idx++];
  for (const char* u : kUnder) if (id == u) { logf("[vspawn] %s none (underground: no drivable space)", id.c_str()); return true; }
  const TemplateInfo* tp = a.findTemplate(id);
  if (!tp) { logf("[vspawn] %s none (no template)", id.c_str()); return true; }
  navNewMapSized(a, *tp, "vspawn " + id, vec2(tp->width, tp->depth));
  Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json()) : json());
  vec3 cen[2] = {vec3(0), vec3(0)}; int n[2] = {0, 0};
  for (auto& s : sc.spawns) {
    int t = s.team >= 0 ? s.team : (s.activeIn(0) ? (int)s.role : -1);   // Defender 0 = team A, Attacker 1 = team B
    if (t < 0) continue;
    cen[t] += s.pos; ++n[t];
  }
  if (!n[0] || !n[1]) { logf("[vspawn] %s none (no team spawns: Solo / sandbox map)", id.c_str()); return true; }
  for (int t = 0; t < 2; ++t) cen[t] /= (float)n[t];
  const AssetDef* tank = a.lib.find("tank_abrams_m1a2");
  float W = tp->width, D = tp->depth, scale = std::clamp(std::min(W, D) / 600.0f, 0.25f, 1.0f);
  json out = json::array();
  for (int t = 0; t < 2; ++t) {
    vec3 S = cen[t], E = cen[1 - t]; vec3 back = S - E; back.y = 0; back = glm::length(back) > 1 ? glm::normalize(back) : vec3(0, 0, 1);
    vec3 side = glm::normalize(glm::cross(back, vec3(0, 1, 0)));
    float yaw = degrees(std::atan2(-back.x, -back.z));   // facing into the map (toward the enemy)
    // every spot on a 5 m grid within 200 m of the spawns that is behind them (farther from the enemy), inside the map,
    // gentle and free for a tank; the ones with a clear line back toward the infantry spawn first, then the distance back
    // nearest the target (120 m x map scale, at least 30 m)
    struct Cand { vec3 p; float score; bool route; };
    std::vector<Cand> cands; std::vector<vec3> got; int rb = 0, rs = 0, rp = 0; std::string why0;
    const float target = std::max(30.0f, 120.0f * scale), dSE = glm::length(vec2(S.x - E.x, S.z - E.z));
    for (float gx = -200; gx <= 200; gx += 5) for (float gz = -200; gz <= 200; gz += 5) {
      vec3 p = S + vec3(gx, 0, gz);
      if (glm::length(vec2(p.x - E.x, p.z - E.z)) < dSE + 10.0f) continue;   // not toward the fighting
      if (std::fabs(p.x) > W * 0.5f - 8 || std::fabs(p.z) > D * 0.5f - 8) { ++rb; continue; }
      p.y = a.terrain.height(p.x, p.z);
      float rise = 0; for (vec2 o : {vec2(3, 0), vec2(-3, 0), vec2(0, 3), vec2(0, -3)}) rise = std::max(rise, std::fabs(a.terrain.height(p.x + o.x, p.z + o.y) - p.y) / 3.0f);
      if (rise > 0.3f) { ++rs; continue; }
      if (tank) { auto pc = a.checkPlacement(tank, p, yaw, 0); if (!pc.ok) { ++rp; if (why0.empty()) why0 = pc.reason; continue; } }
      vec3 to = S - p; float L = std::min(glm::length(to), 40.0f); Physics::Hit h;
      bool route = !(L > 1 && Physics::castRayEx(p + vec3(0, 1.5f, 0), glm::normalize(to), L, h));
      float back = glm::length(vec2(p.x - S.x, p.z - S.z));
      cands.push_back({p, std::fabs(back - target) + std::fabs(glm::dot(p - S, side)) * 0.3f, route});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.route != y.route ? x.route : x.score < y.score; });
    for (auto& c : cands) {
      if (got.size() >= 2) break;
      bool apart = true; for (auto& q : got) if (glm::length(q - c.p) < 14.0f) apart = false;
      if (!apart) continue;
      got.push_back(c.p);
      out.push_back({{"team", t}, {"pos", {std::round(c.p.x * 100) / 100, std::round(c.p.y * 100) / 100, std::round(c.p.z * 100) / 100}}, {"yaw", std::round(yaw * 10) / 10},
                     {"back_m", std::round(glm::length(vec2(c.p.x - S.x, c.p.z - S.z)))}, {"route", c.route}});
    }
    if (got.empty()) logf("[vspawn] %s team %d: no clear spot behind its spawns (S %.0f %.0f, map %.0f x %.0f; rejected: bounds %d, slope %d, placement %d [%s])", id.c_str(), t, S.x, S.z, W, D, rb, rs, rp, why0.c_str());
  }
  logf("[vspawn] %s %s", id.c_str(), out.dump().c_str());
  return true;
}
