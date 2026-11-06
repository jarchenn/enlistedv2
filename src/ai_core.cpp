// AI core lifecycle and shared services: navmesh setup (asset nav metadata -> area costs), per-step services (tile
// commits, path dispatch, world-change polling for live rebuilds), team knowledge with the sharing delay, threat
// viewsheds, route claims, metrics and the stuck-event log. See ai_core.h.
#include "ai_core.h"
#include "app.h"
#include "nav_geom.h"
#include "crumb.h"
#include <DetourNavMesh.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>

static AiCore* gAi = nullptr;
AiCore* aiCore() { return gAi; }

void aiMatchBegin(Game& g, vec3 lo, vec3 hi) {
  delete gAi; gAi = nullptr;
  gAi = new AiCore(g);
  gAi->begin(lo, hi);
}
void aiMatchEnd(Game& g) { (void)g; if (gAi) { gAi->end(); delete gAi; gAi = nullptr; } }

// ---------------------------------------------------------------- difficulty
const std::vector<std::string>& aiDifficultyNames() { static std::vector<std::string> n = {"recruit", "regular", "veteran"}; return n; }
std::string& aiDifficultySetting() {
  static std::string s;
  if (s.empty()) {
    s = "regular";
    try { json r = json::parse(readTextFile(contentDir() / "settings" / "rules.json")); s = r.value("difficulty", s); } catch (...) {}
  }
  return s;
}
AiDifficulty AiDifficulty::load(const json& f, const std::string& name) {
  AiDifficulty d; d.name = name;
  if (!f.is_object() || !f.contains("presets")) return d;
  std::string n = f["presets"].contains(name) ? name : f.value("default", std::string("regular"));
  if (!f["presets"].contains(n)) return d;
  const json& p = f["presets"][n];
  d.name = n;
  d.reaction = p.value("reaction_s", d.reaction); d.aimError = p.value("aimError_deg", d.aimError); d.aimSettle = p.value("aimSettle_s", d.aimSettle);
  d.boundMax = p.value("boundMax_m", d.boundMax); d.boundMaxUnderFire = p.value("boundMaxUnderFire_m", d.boundMaxUnderFire);
  d.threatCost = p.value("threatCost", d.threatCost); d.shareDelay = p.value("shareDelay_s", d.shareDelay);
  d.suppressionResist = p.value("suppressionResist", d.suppressionResist); d.grenadeCooldown = p.value("grenadeCooldown_s", d.grenadeCooldown);
  d.detectOpen = p.value("detectOpen_m", d.detectOpen);
  std::string ow = p.value("overwatch", std::string("yes"));
  d.overwatch = ow == "sometimes" ? 0 : ow == "tight" ? 2 : 1;
  return d;
}

const char* aiActionName(int a) {
  static const char* n[] = {"advance", "return fire", "suppress", "take cover", "reload", "grenade", "clear room", "hold", "evade", "overwatch"};
  return a >= 0 && a < A_COUNT ? n[a] : "?";
}
const char* AiProf::name(int c) { static const char* n[] = {"nav", "perception", "decisions", "aiming", "movement", "total"}; return c >= 0 && c < PROF_COUNT ? n[c] : "?"; }

float AiCore::tj(const char* sect, const char* key, float def) const {
  if (!tactics.contains(sect) || !tactics[sect].is_object()) return def;
  return tactics[sect].value(key, def);
}

// ---------------------------------------------------------------- viewsheds / crowd
bool Viewshed::sees(vec3 p) const {
  vec2 d(p.x - eye.x, p.z - eye.z); float L = glm::length(d);
  if (L > range) return false;
  if (L < 3) return true;
  float a = std::atan2(d.y, d.x); int i = (int)std::floor((a + 3.14159265f) / 6.2831853f * kAz) % kAz; if (i < 0) i += kAz;
  return L < dist[i];
}
int CrowdSnapshot::claims(vec3 p, int except) const {
  auto it = cells.find(key(p.x, p.z));
  if (it == cells.end()) return 0;
  int n = 0; for (int8_t s : it->second) if (s >= 0 && s != except) ++n;
  return n;
}

// ---------------------------------------------------------------- lifecycle
static json loadJson(const char* dir, const char* file) {
  try { return json::parse(readTextFile(contentDir() / dir / file)); } catch (std::exception& e) { logf("ai: %s/%s: %s", dir, file, e.what()); return json::object(); }
}

// simple ear clipping (asset area outlines may be concave)
static void triangulate(const std::vector<vec2>& poly, std::vector<std::array<vec2, 3>>& out) {
  std::vector<vec2> p = poly;
  float area = 0; for (size_t i = 0; i < p.size(); ++i) { vec2 a = p[i], b = p[(i + 1) % p.size()]; area += a.x * b.y - b.x * a.y; }
  if (area < 0) std::reverse(p.begin(), p.end());
  auto cross = [](vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
  int guard = 0;
  while (p.size() > 3 && guard++ < 1000) {
    bool cut = false;
    for (size_t i = 0; i < p.size(); ++i) {
      vec2 a = p[(i + p.size() - 1) % p.size()], b = p[i], c = p[(i + 1) % p.size()];
      if (cross(a, b, c) <= 1e-6f) continue;
      bool inside = false;
      for (size_t k = 0; k < p.size() && !inside; ++k) { vec2 q = p[k]; if (q == a || q == b || q == c) continue; if (cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0) inside = true; }
      if (inside) continue;
      out.push_back({a, b, c}); p.erase(p.begin() + i); cut = true; break;
    }
    if (!cut) break;
  }
  if (p.size() == 3) out.push_back({p[0], p[1], p[2]});
}

void AiCore::begin(vec3 lo, vec3 hi) {
  App& app = G.app;
  // rectangular maps (SPEC 9.1): the caller clamps to the larger half; the navmesh stays inside the build area on each axis
  lo.x = std::max(lo.x, -app.terrain.buildHalfX); hi.x = std::min(hi.x, app.terrain.buildHalfX);
  lo.z = std::max(lo.z, -app.terrain.buildHalfZ); hi.z = std::min(hi.z, app.terrain.buildHalfZ);
  tactics = loadJson("ai", "tactics.json"); navj = loadJson("ai", "navigation.json"); diffj = loadJson("ai", "difficulty.json");
  rules = loadJson("settings", "rules.json");
  json mv = loadJson("settings", "movement.json");
  vSprint = mv.value("sprint_mps", vSprint); vJog = mv.value("run_mps", vJog); vCrouch = mv.value("crouch_mps", vCrouch); vProne = mv.value("prone_mps", vProne);
  gravity = mv.value("gravity_mps2", gravity); jumpV = mv.value("jumpVelocity_mps", jumpV);
  vWalk = tj("movement", "walk_mps", 1.6f);
  diff = AiDifficulty::load(diffj, aiDifficultySetting());
  rayBudget = (int)tj("budget", "raysPerFrame", 200); pathStartsPerFrame = (int)tj("budget", "pathStartsPerFrame", 4);
  mapName = app.doc.name.empty() ? std::string("untitled") : app.doc.name;
  // ---- navmesh: geometry classes and area volumes from the placed instances' nav metadata
  Nav& nav = G.nav;
  nav.cfg = NavConfig{}; nav.cfg.load(navj);
  nav.waterLevel = app.terrain.waterLevel;
  nav.classify = [this](uint64_t ud) -> int {
    if (ud == 0 || (ud >> 60) == 0xF) return NT_SOLID;                 // terrain tiles and height patches
    auto it = G.app.rt.find(ud >> 16);
    if (it == G.app.rt.end() || !it->second.asset) return NT_SOLID;
    const AssetDef& a = *it->second.asset;
    if (a.defense.unsafeVault || a.navMeta.area == NavArea::Wire) return NT_WIRE;
    switch (resolveNavEffect(a.navMeta, a.enterable, a.bmin, a.bmax)) {
      case NavEffect::None: {   // decor; but crates, tripods and signs still stop a body: rasterise anything taller than a step that collides
        static std::unordered_map<const AssetDef*, bool> solid;
        auto sit = solid.find(&a);
        if (sit == solid.end()) { bool any = false; if (a.bmax.y - a.bmin.y > G.nav.cfg.climb) for (auto& p : a.parts) if (p.collide && !p.playerOnly && p.visible) { any = true; break; } sit = solid.emplace(&a, any).first; }   // not flat sheets (sea shallows)
        return sit->second ? NT_SOLID : NT_SKIP; }
      case NavEffect::Area: return NT_SOLID;   // trenches, shingle, fences: their own floors / walls / mounds are the ground; the area is painted on top
      case NavEffect::Carve: return NT_CARVE;
      default: return NT_SOLID;
    }
  };
  nav.areaMarks = [this](int tx, int tz, NavAreaMarks& out) {
    const float T = G.nav.cfg.tile, pad = 4.0f;
    vec2 tlo(tx * T - pad, tz * T - pad), thi((tx + 1) * T + pad, (tz + 1) * T + pad);
    for (auto& in : G.app.doc.instances) {
      auto it = G.app.rt.find(in.id);
      if (it == G.app.rt.end() || !it->second.asset) continue;
      const AssetDef& a = *it->second.asset;
      const NavMeta& m = a.navMeta;
      bool wireSlow = a.defense.unsafeVault && !it->second.defDestroyed;   // every wire obstacle: its uncut stretches are forbidden ground
      bool hedge = a.tree.on && a.tree.hedge && resolveNavEffect(m, a.enterable, a.bmin, a.bmax) != NavEffect::None;   // bank + woody stems: a wall (AI_SPEC 3.1)
      // lattice obstacles (Belgian gates, hedgehogs, abatis): their struts rasterise as walkable ledges; the footprint is no-go
      bool lattice = false; if (navj.contains("footprintNull")) for (auto& id : navj["footprintNull"]) if (id.is_string() && id.get<std::string>() == a.id) lattice = true;
      if (m.areas.empty() && m.area == NavArea::Default && !wireSlow && !hedge && !lattice) continue;
      float r = glm::length(vec2(std::max(std::fabs(a.bmin.x), std::fabs(a.bmax.x)), std::max(std::fabs(a.bmin.z), std::fabs(a.bmax.z)))) + 1;
      if (in.pos.x + r < tlo.x || in.pos.x - r > thi.x || in.pos.z + r < tlo.y || in.pos.z - r > thi.y) continue;
      quat q = yawQuat(in.yaw);
      auto W = [&](vec2 l) { vec3 w = in.pos + q * vec3(l.x, 0, l.y); return vec2(w.x, w.z); };
      float h0 = in.pos.y + a.bmin.y - 1.0f, h1 = in.pos.y + a.bmax.y + 1.0f;
      auto addPoly = [&](const std::vector<vec2>& local, NavArea ar) {
        std::vector<std::array<vec2, 3>> tris; triangulate(local, tris);
        // forbidden ground (minefield) grows by the agent radius + a margin: an "area" asset's own fence posts are not
        // rasterised into the mesh, so the walkable edge would otherwise run through them
        float grow = navAreaCost(ar) < 0 && ar != NavArea::Wire ? G.nav.cfg.radius + 0.25f : 0.0f;
        for (auto& t : tris) {
          std::array<vec2, 3> v = {W(t[0]), W(t[1]), W(t[2])};
          if (grow > 0) { vec2 c = (v[0] + v[1] + v[2]) / 3.0f; for (auto& p : v) { vec2 d = p - c; float L = glm::length(d); if (L > 1e-3f) p += d / L * grow * 2.0f; } }   // ~grow on every edge of a fan triangle
          NavAreaMark mk; mk.poly = {v[0], v[1], v[2]}; mk.hmin = h0; mk.hmax = h1; mk.area = navarea::fromNav(ar); out.marks.push_back(mk);
        }
      };
      if (wireSlow) {   // the wire parts' footprints are forbidden (thin coils leave walkable ground between the rings)
        const auto& cut = it->second.secCut;
        for (auto& p : a.parts) if (p.playerOnly) {
          if (p.sec >= 0 && p.sec < (int)cut.size() && cut[p.sec]) continue;   // a stretch cut by a blast is a gap
          vec3 hs = p.size * 0.5f; std::vector<vec2> fp;
          for (vec2 c : {vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1)}) { vec3 l = p.center + p.rot * vec3(c.x * (hs.x + 0.15f), 0, c.y * (hs.z + 0.15f)); fp.push_back(vec2(l.x, l.z)); }
          addPoly(fp, NavArea::Wire);
        }
      }
      if (lattice) {
        float pd = navj.value("footprintNullPad_m", 0.2f) + G.nav.cfg.radius;   // marks land after erosion
        std::vector<std::array<vec2, 3>> tris; triangulate({vec2(a.bmin.x - pd, a.bmin.z - pd), vec2(a.bmax.x + pd, a.bmin.z - pd), vec2(a.bmax.x + pd, a.bmax.z + pd), vec2(a.bmin.x - pd, a.bmax.z + pd)}, tris);
        for (auto& t : tris) { NavAreaMark mk; mk.poly = {W(t[0]), W(t[1]), W(t[2])}; mk.hmin = h0; mk.hmax = h1; mk.nullify = true; out.marks.push_back(mk); }
      }
      if (hedge) {
        // the bank and the leaning stems as built (every colliding part's rotated box, up to head height), plus a pad
        vec2 lo2(-a.footprint * 0.5f), hi2(a.footprint * 0.5f);
        for (auto& p : a.parts) {
          if (!p.collide || p.playerOnly) continue;
          for (int c = 0; c < 8; ++c) {
            vec3 l = p.center + p.rot * (vec3((c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f) * p.size);
            if (l.y > 2.0f) continue;
            lo2 = glm::min(lo2, vec2(l.x, l.z)); hi2 = glm::max(hi2, vec2(l.x, l.z));
          }
        }
        float hp = navj.value("hedgePad_m", 0.1f) + G.nav.cfg.radius; lo2 -= vec2(hp); hi2 += vec2(hp);   // marks land after erosion: add the agent radius
        std::vector<std::array<vec2, 3>> tris; triangulate({vec2(lo2.x, lo2.y), vec2(hi2.x, lo2.y), vec2(hi2.x, hi2.y), vec2(lo2.x, hi2.y)}, tris);
        for (auto& t : tris) { NavAreaMark mk; mk.poly = {W(t[0]), W(t[1]), W(t[2])}; mk.hmin = h0; mk.hmax = h1; mk.nullify = true; out.marks.push_back(mk); }
      }
      if (!m.areas.empty()) { for (auto& ap : m.areas) if (ap.outline.size() >= 3) addPoly(ap.outline, ap.area); }
      else if (m.area != NavArea::Default) addPoly({vec2(a.bmin.x, a.bmin.z), vec2(a.bmax.x, a.bmin.z), vec2(a.bmax.x, a.bmax.z), vec2(a.bmin.x, a.bmax.z)}, m.area);
    }
    // map-level painted areas (doc.extra["navAreas"]: [{"area": "marsh", "outline": [[x, z], ...]}], world x / z)
    const json& ex = G.app.doc.extra;
    if (ex.is_object() && ex.contains("navAreas") && ex["navAreas"].is_array()) for (auto& na : ex["navAreas"]) {
      std::vector<vec2> poly; for (auto& p : na.value("outline", json::array())) if (p.is_array() && p.size() >= 2) poly.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
      if (poly.size() < 3) continue;
      vec2 mn(1e9f), mx(-1e9f); for (auto& p : poly) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
      if (mx.x < tlo.x || mn.x > thi.x || mx.y < tlo.y || mn.y > thi.y) continue;
      std::vector<std::array<vec2, 3>> tris; triangulate(poly, tris);
      uint8_t ar = navarea::fromNav(navAreaFrom(na.value("area", std::string("default"))));
      for (auto& t : tris) { NavAreaMark mk; mk.poly = {t[0], t[1], t[2]}; mk.area = ar; out.marks.push_back(mk); }
    }
  };
  crumb("ai: navmesh bake");
  nav.build(lo, hi);
  markNavTraps();
  logf("ai: %d navmesh polygons in dead-end pockets (no walk back to an objective or spawn)", navTraps);
  // ---- cover points for every tile (parallel: the physics world is idle while the match starts)
  {
    auto t0 = std::chrono::steady_clock::now();
    nav.forEachTile([&](int tx, int tz) { coverTodo.push_back({tx, tz}); });
    while (!coverTodo.empty()) { auto t = coverTodo.back(); coverTodo.pop_back(); generateCoverTile(t.first, t.second, 1 << 30); }
    addAuthoredCover();
    logf("ai: %zu cover points in %.0f ms", cover.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  scanBuildings();
  // ---- lanes: 25-35 % flankers (AI_SPEC 9 / SPEC 5.3), committed from spawn (kept per roster slot across lives)
  {
    std::vector<int> atk; for (auto& rs : G.match.slots) if (rs.role == CombatRole::Attacker && !rs.human) atk.push_back(rs.id);
    float fr = std::clamp(tj("commander", "flankFraction", 0.3f), 0.25f, 0.35f);
    int n = (int)atk.size();
    int nf = (int)std::lround(n * fr);
    nf = std::clamp(nf, (int)std::ceil(n * 0.25f - 1e-4f), std::max(0, (int)std::floor(n * 0.35f + 1e-4f)));
    if (n < 3) nf = 0;
    float wl = std::max(0.01f, G.match.rules.laneLeft), wr = std::max(0.01f, G.match.rules.laneRight);
    int nl = (int)std::lround(nf * wl / (wl + wr)), nr = nf - nl;
    if (nf >= 2 && (nl == 0 || nr == 0)) { nl = nf / 2 + nf % 2; nr = nf / 2; }
    for (int k = 0; k < n; ++k) G.match.slots[atk[k]].lane = k < nl ? LANE_LEFT : k < nl + nr ? LANE_RIGHT : LANE_MAIN;
    // spread flankers through the roster order so fireteams mix spawn markers evenly
    logf("ai: %d attacking bots: %d left flank, %d main, %d right flank (difficulty %s)", n, nl, n - nf, nr, diff.name.c_str());
  }
  for (int r = 0; r < 2; ++r) { teams[r] = TeamBrain{}; teams[r].role = r; }
  paths.start(&G.nav);
  oaq = dtAllocObstacleAvoidanceQuery(); oaq->init(10, 32);
  oaParams = dtObstacleAvoidanceParams{0.4f, 2.0f, 0.75f, 0.75f, 2.5f, 2.5f, 33, 7, 2, 5};
  metrics = AiMetrics{};
  // fingerprints of the world as baked (later differences trigger live rebuilds)
  pollWorld(false); rebuildLog.clear();
  std::error_code ec; fs::create_directories(exeDir() / "selftest_out", ec);
  std::ofstream(exeDir() / "selftest_out" / "ai_stuck.log") << "# stuck events (AI_SPEC 3.6): t_start t_end map x y z slot resolved_step resolved\n";
}

void AiCore::end() {
  paths.stop();
  if (oaq) { dtFreeObstacleAvoidanceQuery(oaq); oaq = nullptr; }
  for (auto& e : stuck) if (e.open) { e.open = false; e.t1 = G.t; logStuck(e); }
}

BotMind& AiCore::mind(Soldier& s) {
  if ((int)minds.size() <= s.slot) minds.resize(s.slot + 1);
  auto& up = minds[s.slot];
  if (!up) { up = std::make_unique<BotMind>(); up->corridor = std::make_unique<dtPathCorridor>(); up->corridor->init(256); }
  if (up->life != s.lifeId) {
    auto c = std::move(up->corridor);
    up = std::make_unique<BotMind>();   // a fresh mind per life (Detour helpers are not assignable)
    up->corridor = std::move(c);
  }
  BotMind& m = *up;
  if (m.life != s.lifeId) {
    m.life = s.lifeId;
    m.corridor->reset(0, &s.feet.x);
    m.boundary.reset();
    m.team = (int)s.role;
    m.flanker = s.role == CombatRole::Attacker && s.br.lane != LANE_MAIN;
    m.progAnchor = s.feet; m.boundFrom = s.feet;
    m.nextThink = G.t + G.rnd(s) * 0.2;
    m.actionSince = G.t;
    paths.cancelSlot(s.slot);
  }
  return m;
}

bool AiCore::ray(vec3 o, vec3 d, float len, Physics::Hit* h) {
  ++raysThisFrame; ++raysTotal;
  Physics::Hit tmp;
  if (len < 1e-4f) return false;
  return Physics::castRayEx(o, d, len, h ? *h : tmp);
}

float AiCore::nearestHumanDist(vec3 p) const {
  float best = 1e9f;
  for (auto& o : G.sold) if (o.alive && o.human) best = std::min(best, glm::length(o.feet - p));
  return best;
}

bool AiCore::humanSees(const Soldier& s) {
  for (auto& o : G.sold) {
    if (!o.alive || !o.human) continue;
    vec3 e = G.eye(o), c = G.chest(s);
    vec3 d = c - e; float L = glm::length(d);
    if (L > 400) continue;
    float yaw = o.localPlayer ? G.app.eyeYaw : o.yaw;
    vec3 f = App::forward(yaw, 0);
    if (L > 3 && glm::dot(glm::normalize(vec3(d.x, 0, d.z)), f) < std::cos(radians(55))) continue;
    if (G.lineOfSight(e, c)) return true;
  }
  return false;
}

void AiCore::logStuck(const StuckEvent& e) {
  std::ofstream f(exeDir() / "selftest_out" / "ai_stuck.log", std::ios::app);
  f << e.t0 << " " << e.t1 << " \"" << mapName << "\" " << e.pos.x << " " << e.pos.y << " " << e.pos.z << " " << e.slot << " " << e.step << " " << (e.resolved ? "yes" : "NO") << " " << e.why << "\n";
  logf("ai: stuck bot %d at (%.1f %.1f %.1f) on %s for %.1f s: %s (ladder step %d) | %s, goal (%.1f %.1f %.1f), next (%.1f %.1f %.1f)", e.slot, e.pos.x, e.pos.y, e.pos.z, mapName.c_str(), e.t1 - e.t0, e.resolved ? "recovered" : "UNRECOVERED", e.step,
       e.doing.c_str(), e.goal.x, e.goal.y, e.goal.z, e.next.x, e.next.y, e.next.z);
}

// ---------------------------------------------------------------- world changes (creative live rebuild, breaches)
void AiCore::pollWorld(bool rebuild) {
  App& app = G.app;
  std::map<uint64_t, std::pair<uint64_t, std::pair<vec3, vec3>>> now;
  for (auto& in : app.doc.instances) {
    auto it = app.rt.find(in.id);
    if (it == app.rt.end() || !it->second.asset) continue;
    const InstanceRT& r = it->second; const AssetDef& a = *r.asset;
    if (resolveNavEffect(a.navMeta, a.enterable, a.bmin, a.bmax) == NavEffect::None && !a.defense.unsafeVault) continue;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    auto mixf = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); };
    mix(r.rev); mixf(in.pos.x); mixf(in.pos.y); mixf(in.pos.z); mixf(in.yaw); mix(r.staticBody); mix(r.wallBody); mix(r.playerBody);
    mix(r.defDestroyed); mix(r.treeFelled); mix(r.trunkBody); for (uint8_t s : r.state) mix(s); for (uint8_t c : r.secCut) mix(c + 7);
    for (auto& sh : r.pshape) mix(sh ? (uint64_t)(uintptr_t)sh.get() : 3);
    // world bounds (yawed box)
    vec3 lo(1e9f), hi(-1e9f); quat q = yawQuat(in.yaw);
    for (int c = 0; c < 8; ++c) { vec3 l((c & 1) ? a.bmax.x : a.bmin.x, (c & 2) ? a.bmax.y : a.bmin.y, (c & 4) ? a.bmax.z : a.bmin.z); vec3 w = in.pos + q * l; lo = glm::min(lo, w); hi = glm::max(hi, w); }
    now[in.id] = {h, {lo, hi}};
  }
  if (rebuild) {
    std::vector<std::pair<vec3, vec3>> dirty;
    for (auto& [id, v] : now) { auto it = instPrint.find(id); if (it == instPrint.end()) dirty.push_back(v.second); else if (it->second.first != v.first) { dirty.push_back(v.second); dirty.push_back(it->second.second); } }
    for (auto& [id, v] : instPrint) if (!now.count(id)) dirty.push_back(v.second);
    for (auto& d : dirty) { G.nav.rebuildArea(d.first, d.second); rebuildLog.push_back({G.t, d.first, d.second}); if (rebuildLog.size() <= 12) logf("ai: world changed at (%.0f %.0f)-(%.0f %.0f): navmesh tiles rebuild", d.first.x, d.first.z, d.second.x, d.second.z); }
  }
  instPrint.swap(now);
  // terrain deformation (craters, cuts): only when the ground revision moved, tiles whose heights changed
  if (!rebuild || app.groundRev != lastGroundRev) {
    lastGroundRev = app.groundRev;
    for (auto& [k, tile] : app.terrain.tiles) {
      double sum = 0; for (float hv : tile.h) sum += hv;
      uint32_t sig = (uint32_t)(int64_t)std::llround(sum * 100.0) ^ (uint32_t)tile.h.size();
      auto it = terrainRev.find(k);
      if (rebuild && (it == terrainRev.end() || it->second != sig)) {
        vec2 o = app.terrain.tileOrigin(tile.tx, tile.tz); float ts = app.terrain.tileSize();
        G.nav.rebuildArea(vec3(o.x, -100, o.y), vec3(o.x + ts, 100, o.y + ts));
      }
      terrainRev[k] = sig;
    }
  }
}

void AiCore::markNavTraps() {
  std::vector<vec3> anchors;
  for (auto& o : G.match.sc.objectives) anchors.push_back(o.pos);
  for (auto& s : G.match.sc.spawns) anchors.push_back(s.pos);
  navTraps = G.nav.markTraps(anchors);
}

void AiCore::onTilesChanged() {
  if (G.nav.changed.empty()) return;
  if (trapsDueAt < 0) trapsDueAt = G.t + 0.5;   // committed tiles come without the pocket flags; re-mark soon (batched)
  lastCommitAt = G.t; committedTiles += (int)G.nav.changed.size();
  std::set<std::pair<int, int>> ch(G.nav.changed.begin(), G.nav.changed.end());
  // bots whose corridor crosses a rebuilt tile re-plan now (AI_SPEC 3.3: within 1 s)
  int nRe = 0;
  for (auto& s : G.sold) {
    if (!s.alive || s.human || s.slot >= (int)minds.size() || !minds[s.slot]) continue;
    BotMind& m = *minds[s.slot];
    bool hit = false;
    if (m.trav.on && ch.count({G.nav.tileOf(m.trav.end.x), G.nav.tileOf(m.trav.end.z)}) && G.nav.nearest(m.trav.end, 0.25f) < 0) {
      m.trav.on = false; ++metrics.linkAborts;   // the landing is gone (something was built on it): stop where the body is
      Physics::setAgentPos(s.agent, s.feet); hit = true;
    }
    for (auto& t : m.pathTiles) if (ch.count(t)) { hit = true; break; }
    if (!hit && ch.count({G.nav.tileOf(s.feet.x), G.nav.tileOf(s.feet.z)})) hit = true;
    if (hit && (m.hasPath || m.wantPath)) { requestPath(s, m.hasSubGoal ? m.subGoal : m.goal, m.goalRadius, 10.0f, true); ++metrics.rebuildReplans; ++nRe; }
  }
  if (++commitLogs <= 20) logf("ai: %zu navmesh tiles committed at %.2f s, %d bots re-plan", ch.size(), G.t, nRe);
  // regenerate cover for those tiles; defense slots that came from cover are rebuilt
  for (auto& t : ch) {
    for (auto& c : cover) if (!c.authored && c.tx == t.first && c.tz == t.second) c.serial = 0;   // tombstone
    coverTodo.push_back(t);
  }
  // defense slots: only when the point itself changed, at most every 10 s (wall chips from small-arms fire rebuild
  // tiles all the time; re-assigning everyone each time makes defenders shuffle instead of holding)
  if (const Objective* ob = G.match.activeObjective(); ob && G.t - lastSlotRebuild > 10.0) {
    float R = ob->extent() + 4;
    for (auto& t : ch) {
      float T = G.nav.cfg.tile;
      if (ob->pos.x + R < t.first * T || ob->pos.x - R > (t.first + 1) * T || ob->pos.z + R < t.second * T || ob->pos.z - R > (t.second + 1) * T) continue;
      defStage = -2; break;
    }
  }
}

// ---------------------------------------------------------------- knowledge (AI_SPEC 4)
void AiCore::shareKnowledge() {
  for (auto& s : G.sold) {
    if (!s.alive || s.human) continue;
    TeamBrain& tb = teams[(int)s.role];
    for (auto& m : s.br.mem) {
      if (m.lastSeen < G.t - 0.25) continue;                 // only fresh perceptions are reported
      Known& k = tb.known[m.enemy];
      if (m.lastSeen >= k.seen) { k.enemy = m.enemy; k.pos = m.lastPos; k.seen = m.lastSeen; k.heard = m.heardOnly; k.by = s.slot; k.conf = m.heardOnly ? 0.5f : 1.0f; }
    }
  }
  for (auto& tb : teams) {
    for (auto it = tb.known.begin(); it != tb.known.end();) {
      Known& k = it->second;
      k.conf = (k.heard ? 0.5f : 1.0f) * std::exp(-(float)(G.t - k.seen) / tj("perception", "memoryDecay_s", 10.0f));
      bool dead = k.enemy < 0 || k.enemy >= (int)G.sold.size() || !G.sold[k.enemy].alive;
      if (k.conf < 0.05f || dead) it = tb.known.erase(it); else ++it;
    }
  }
}

void AiCore::updateViewsheds() {
  const float range = tj("threat", "viewRange_m", 250.0f);
  static const float rad[] = {6, 14, 28, 50, 90, 150, 250};
  int raysLeft = std::max(16, rayBudget / 4);
  for (auto& tb : teams) {
    bool changed = false;
    // drop viewsheds of enemies no longer known
    tb.shedBuild.erase(std::remove_if(tb.shedBuild.begin(), tb.shedBuild.end(), [&](const Viewshed& v) { auto it = tb.known.find(v.enemy); if (it == tb.known.end() || it->second.conf < 0.25f) { changed = true; return true; } return false; }), tb.shedBuild.end());
    for (auto& [e, k] : tb.known) {
      if (k.conf < 0.3f || raysLeft <= 0) continue;
      Viewshed* v = nullptr; for (auto& x : tb.shedBuild) if (x.enemy == e) v = &x;
      vec3 eye = k.pos + vec3(0, 1.55f, 0);
      if (v && glm::length(v->eye - eye) < 4.0f && G.t - v->t < 12) { v->conf = k.conf; continue; }
      Viewshed nv; nv.enemy = e; nv.eye = eye; nv.t = G.t; nv.range = range; nv.conf = k.conf;
      for (int az = 0; az < Viewshed::kAz; ++az) {
        float a = -3.14159265f + (az + 0.5f) * 6.2831853f / Viewshed::kAz;
        vec2 dir(std::cos(a), std::sin(a));
        float vis = 3;
        for (float r : rad) {
          if (r > range) break;
          vec3 q(eye.x + dir.x * r, 0, eye.z + dir.y * r);
          q.y = G.app.terrain.height(q.x, q.z) + 1.2f;
          vec3 d = q - eye; float L = glm::length(d);
          --raysLeft;
          if (ray(eye, d, L)) break;
          vis = r * 1.15f;
        }
        nv.dist[az] = vis;
      }
      if (v) *v = nv; else tb.shedBuild.push_back(nv);
      changed = true;
    }
    if (changed) { auto snap = std::make_shared<ThreatSnapshot>(); snap->v = tb.shedBuild; tb.threat = snap; }
  }
}

float AiCore::exposure(int role, vec3 p) const {
  float e = 0;
  for (auto& v : teams[role].threat->v) if (v.sees(p)) e = std::max(e, v.conf);
  return e;
}

void AiCore::updateCrowd(TeamBrain& tb) {
  auto snap = std::make_shared<CrowdSnapshot>();
  for (auto& s : G.sold) {
    if (!s.alive || s.human || (int)s.role != tb.role || s.slot >= (int)minds.size() || !minds[s.slot]) continue;
    BotMind& m = *minds[s.slot];
    if (!m.hasPath) continue;
    std::vector<vec3> pts;
    const dtPathCorridor& c = *m.corridor;
    // straight path of the corridor (cheap: string pulling over its polygons)
    float sp[32 * 3]; unsigned char sf[32]; dtPolyRef sr[32]; int ns = 0;
    G.nav.query()->findStraightPath(c.getPos(), c.getTarget(), c.getPath(), c.getPathCount(), sp, sf, sr, &ns, 32, 0);
    for (int k = 0; k + 1 < ns; ++k) {
      vec3 a(sp[k * 3], sp[k * 3 + 1], sp[k * 3 + 2]), b(sp[k * 3 + 3], sp[k * 3 + 4], sp[k * 3 + 5]);
      float L = glm::length(b - a); int n = std::max(1, (int)(L / 2.0f));
      for (int i = 0; i <= n; ++i) {
        vec3 p = glm::mix(a, b, i / (float)n);
        auto& cell = snap->cells[CrowdSnapshot::key(p.x, p.z)];
        if (cell[0] == 0 && cell[1] == 0 && cell[2] == 0 && cell[3] == 0) cell.fill(-1);
        bool have = false; for (int8_t x : cell) if (x == s.slot) have = true;
        if (have) continue;
        for (auto& x : cell) if (x < 0) { x = (int8_t)s.slot; break; }
      }
    }
  }
  tb.crowd = snap;
}

// ---------------------------------------------------------------- per-step services
void AiCore::frame() {
  // roll the previous step's profile
  double tot = 0; for (int c = 0; c < PROF_TOTAL; ++c) tot += prof.frame[c];
  prof.frame[PROF_TOTAL] = tot;
  if (G.steps > 1) {
    for (int c = 0; c < PROF_COUNT; ++c) { prof.sum[c] += prof.frame[c]; prof.max[c] = std::max(prof.max[c], prof.frame[c]); }
    ++prof.frames; prof.totals.push_back(tot); if (prof.totals.size() > 20000) prof.totals.erase(prof.totals.begin(), prof.totals.begin() + 10000);
  }
  for (double& f : prof.frame) f = 0;
  raysThisFrame = 0;
  frameT = G.t;
  {
    AiProf::Scope sc(prof, PROF_NAV);
    crumb("ai: nav commit");
    G.nav.update();
    onTilesChanged();
    if (trapsDueAt >= 0 && G.t >= trapsDueAt) { trapsDueAt = -1; markNavTraps(); }
    if (!coverTodo.empty()) { auto t = coverTodo.back(); coverTodo.pop_back(); generateCoverTile(t.first, t.second, 120); }
    if (G.t >= nextWorldPoll) { nextWorldPoll = G.t + 0.1; pollWorld(); }
    receivePaths();
    dispatchPaths();
  }
  {
    AiProf::Scope sc(prof, PROF_PERCEPTION);
    crumb("ai: near misses");
    nearMisses();
    watchGrenades();
    shareKnowledge();
    updateViewsheds();
  }
  {
    AiProf::Scope sc(prof, PROF_DECISIONS);
    crumb("ai: commander");
    if (defStage != G.match.active) buildDefSlots();
    for (auto& tb : teams) {
      if (G.t >= tb.nextCommand) { tb.nextCommand = G.t + 1.0; commander(tb); }
      if (G.t >= tb.nextFireteam) { tb.nextFireteam = G.t + 0.33; for (auto& ft : tb.fts) fireteamStep(tb, ft); }
      if (G.t >= tb.nextCrowd) { tb.nextCrowd = G.t + 0.5; updateCrowd(tb); }
    }
  }
  // ---- metrics (AI_SPEC 12.3, attackers)
  float dt = 1.0f / 60.0f;
  metrics.t += dt;
  for (auto& s : G.sold) {
    if (!s.alive || s.human || s.role != CombatRole::Attacker || s.slot >= (int)minds.size() || !minds[s.slot]) continue;
    BotMind& m = *minds[s.slot];
    bool advancing = m.action == A_ADVANCE && !m.set && m.hasPath && !arrived(s) && !m.trav.on;   // AI_SPEC 12.3 "while advancing": bounding, not stopped to fire / reload
    if (advancing) {
      metrics.advT += dt; metrics.advSpeedSum += s.speed; ++metrics.advN;
      float nn = 1e9f; for (auto& o : G.sold) if (&o != &s && o.alive && o.role == s.role) nn = std::min(nn, glm::length(o.feet - s.feet));
      if (nn < 1e8f) { metrics.nnSum += nn; ++metrics.nnN; }
    }
    bool underFire = G.t - s.br.hurtAt < 2.0 || s.br.suppressed > 0.3f || G.t - m.nearMissAt < 1.0;
    if (underFire) {
      metrics.fireT += dt;
      vec3 from = G.t - s.br.hurtAt < 2.0 ? s.br.hurtFrom : m.nearMissFrom;
      bool covered = false;
      int64_t cx = (int64_t)std::floor(s.feet.x / 8), cz = (int64_t)std::floor(s.feet.z / 8);
      for (int64_t dz = -1; dz <= 1 && !covered; ++dz) for (int64_t dx = -1; dx <= 1 && !covered; ++dx) {
        auto it = coverGrid.find(((cx + dx) << 32) ^ (uint32_t)(cz + dz)); if (it == coverGrid.end()) continue;
        for (int ci : it->second) { const CoverPoint& c = cover[ci]; if (!c.serial || glm::length(c.p - s.feet) > 5) continue; vec3 td = from - c.p; td.y = 0; if (c.natural || glm::length(td) < 1 || glm::dot(glm::normalize(td), c.n) > 0.25f) { covered = true; break; } }   // craters cover all round (coverProtects)
      }
      if (!covered) metrics.exposedT += dt; else metrics.coverNear5T += dt;
    }
  }
  if (metrics.attackerDeaths0 < 0) { metrics.attackerDeaths0 = 0; for (auto& rs : G.match.slots) if (rs.role == CombatRole::Attacker) metrics.attackerDeaths0 += rs.deaths; }
}

json AiMetrics::toJson(const Game& G) const {
  int deaths = -std::max(0, attackerDeaths0); for (auto& rs : G.match.slots) if (rs.role == CombatRole::Attacker) deaths += rs.deaths;
  int bots = 0; for (auto& rs : G.match.slots) bots += rs.role == CombatRole::Attacker && !rs.human;
  float mins = std::max(1.0f, (float)t) / 60.0f;
  json j = {{"seconds", t}, {"attackingBots", bots},
            {"stuckEvents", stuckEvents}, {"stuckOver5s", stuckOver5}, {"stuckUnrecovered", stuckUnrecovered},
            {"stuckPerBotPer10Min", stuckEvents / std::max(1, bots) / mins * 10.0},
            {"advanceSpeedMean_mps", advN ? advSpeedSum / advN : 0.0}, {"advancingBotSeconds", advT},
            {"underFireSeconds", fireT}, {"exposedUnderFireFraction", fireT > 0 ? exposedT / fireT : 0.0},
            {"boundExposureMean_s", bounds ? boundExposure / bounds : 0.0}, {"bounds", bounds}, {"sidesteps", sidesteps},
            {"nearestTeammateWhileAdvancing_m", nnN ? nnSum / nnN : 0.0},
            {"attackerDeathsPerMin", deaths / mins}, {"grenadesThrown", grenadesThrown}, {"grenadesHurtTeammate", grenadesHurtTeammate},
            {"rebuildReplans", rebuildReplans}, {"linkTraversals", linkTraversals}, {"linkAborts", linkAborts}, {"snaps", snaps}};
  json rs = json::array(); for (int k = 0; k < 7; ++k) rs.push_back(stuckResolvedBy[k]); j["stuckResolvedByStep"] = rs;
  return j;
}
