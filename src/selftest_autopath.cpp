// --selftest-autopath: auto paths between houses (V9, docs/entities.md#auto-paths) and curved path pieces.
//  * curved pieces: the mesh ends where pathEnd says, at the right heading; snapPath continues a curved end along its
//    tangent; resizing keeps the bend; surface lookups follow the arc;
//  * Connect houses on Normandy Village Assault and Battle 1: every house gets aprons, every house is connected (an
//    independent check over the emitted pieces' end points, not the router's own bookkeeping), no piece crosses a
//    footprint (besides an apron leaving its own door), chains are continuous (joints meet the straights), routes are
//    written and wear the ground, re-running replaces (same pieces, nothing duplicated), the run is one undo step;
//  * the "Auto paths" toggle connects a newly placed house within the placement's undo step;
//  * screenshot of the village network from above (selftest_out/autopath_<map>.png).
#include "selftest.h"
#include "mptest.h"
#include "autopath.h"
#include "lights.h"   // L3: lamp posts along the routes
#include <set>
#include <cstdarg>
#include <functional>
#include <unordered_map>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[900];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[autopath] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
bool isAuto(const Instance& i) { return i.extra.is_object() && i.extra.contains("auto") && i.extra["auto"].is_string(); }
int autoCount(App& a) { int n = 0; for (auto& i : a.doc.instances) n += isAuto(i); return n; }
int autoRoutes(App& a) {
  int n = 0;
  if (a.doc.extra.is_object() && a.doc.extra.contains("routes")) for (auto& r : a.doc.extra["routes"]) n += r.is_object() && r.value("auto", false);
  return n;
}
uint64_t layoutHash(App& a) {   // auto pieces: asset, rounded pose and size (ids differ between runs)
  std::vector<std::string> v;
  for (auto& i : a.doc.instances) if (isAuto(i)) { char b[160]; vec2 wl = a.pathSize(i, a.assetFor(i)); PathShape s = pathShape(i);
    std::snprintf(b, sizeof b, "%.2f %.2f %.1f %.2f %.2f %.1f %s", i.pos.x, i.pos.z, i.yaw, wl.x, wl.y, s.bend, i.extra["auto"].get<std::string>().c_str()); v.push_back(b); }
  std::sort(v.begin(), v.end());
  uint64_t h = 1469598103934665603ull; for (auto& s : v) for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h ^ v.size();
}

// independent connectivity over the emitted geometry: pieces joined where their ends meet (or an end lies on another
// piece), houses joined to the aprons leaving their doors
struct Net { int houses = 0, comps = 0, isolated = 0; std::string lonely; };
Net connectivity(App& a) {
  std::vector<const Instance*> P; for (auto& i : a.doc.instances) if (isAuto(i)) P.push_back(&i);
  std::vector<uint64_t> H; for (auto& i : a.doc.instances) if (autopathIsHouse(a.assetFor(i))) H.push_back(i.id);
  int n = (int)P.size(), m = (int)H.size();
  std::vector<int> uf(n + m); for (int k = 0; k < n + m; ++k) uf[k] = k;
  std::function<int(int)> f = [&](int x) { return uf[x] == x ? x : uf[x] = f(uf[x]); };
  auto join = [&](int x, int y) { uf[f(x)] = f(y); };
  std::vector<std::array<vec3, 2>> E(n);
  for (int k = 0; k < n; ++k) for (int s = 0; s < 2; ++s) { vec2 d; pathEnd(a, *P[k], a.assetFor(*P[k]), s ? 1.0f : -1.0f, E[k][s], d); }
  for (int x = 0; x < n; ++x) for (int y = x + 1; y < n; ++y) {
    bool j = false;
    for (int s = 0; s < 2 && !j; ++s) for (int t = 0; t < 2 && !j; ++t) j = glm::length(vec2(E[x][s].x - E[y][t].x, E[x][s].z - E[y][t].z)) < 0.4f;
    if (!j) { vec2 aa; for (int s = 0; s < 2 && !j; ++s) j = pathLocalCoords(a, *P[y], a.assetFor(*P[y]), E[x][s], aa) || pathLocalCoords(a, *P[x], a.assetFor(*P[x]), E[y][s], aa); }
    if (j) join(x, y);
  }
  for (int h = 0; h < m; ++h) {
    const Instance* hi = a.doc.find(H[h]); const AssetDef* ha = a.assetFor(*hi); quat q = yawQuat(hi->yaw);
    for (auto& e : ha->navMeta.entrances) {
      vec3 dp = hi->pos + q * vec3(e.p.x, 0, e.p.z);
      for (int k = 0; k < n; ++k) if (P[k]->extra["auto"] == "apron")
        for (int s = 0; s < 2; ++s) if (glm::length(vec2(E[k][s].x - dp.x, E[k][s].z - dp.z)) < 0.4f) join(n + h, k);
    }
  }
  Net r; r.houses = m; std::set<int> roots;
  for (int h = 0; h < m; ++h) {
    roots.insert(f(n + h));
    bool any = false; for (int k = 0; k < n && !any; ++k) any = f(k) == f(n + h);
    if (!any) { ++r.isolated; if (r.lonely.size() < 200) r.lonely += " " + a.doc.find(H[h])->assetId + "#" + std::to_string(H[h]); }
  }
  r.comps = (int)roots.size();
  if (r.comps > 1) {   // diagnostics: each network's size and one piece in it
    std::map<int, std::pair<int, int>> g; for (int k = 0; k < n; ++k) { auto& e = g[f(k)]; if (!e.first++) e.second = k; }
    for (auto& [root, e] : g) { char b[120]; std::snprintf(b, sizeof b, " [%d pieces near (%.0f, %.0f)]", e.first, P[e.second]->pos.x, P[e.second]->pos.z); if (r.lonely.size() < 600) r.lonely += b; }
  }
  return r;
}
// samples over each auto piece's surface inside any structure footprint (aprons may start inside their own house)
int footprintHits(App& a, std::string& what) {
  int hits = 0;
  for (auto& p : a.doc.instances) {
    if (!isAuto(p)) continue;
    const AssetDef* pa = a.assetFor(p); vec2 wl = a.pathSize(p, pa); PathShape sh = pathShape(p);
    bool apron = p.extra["auto"] == "apron";
    quat q = yawQuat(p.yaw);
    for (auto& o : a.doc.instances) {
      const AssetDef* b = a.assetFor(o);
      if (!b || b->path.on || b->nature.kind != NatureDef::NONE || b->tree.on || isVehicleAsset(b) || b->footprint.x <= 0) continue;
      if (b->category == "Trees" || b->category == "Bushes" || b->category == "Vegetation") continue;
      if (!b->defense.kind.empty() || b->category == "Defences" || b->category == "Beach obstacles" || b->category == "Dressing") continue;   // wire / fences / low walls: only the relaxed pass crosses them
      { NavEffect ne = resolveNavEffect(b->navMeta, b->enterable, b->bmin, b->bmax); if (ne == NavEffect::None || ne == NavEffect::Area) continue; }   // walkable areas
      if (const_cast<InteriorSys&>(a.interior).isProp(b)) continue;
      if (glm::length(vec2(o.pos.x - p.pos.x, o.pos.z - p.pos.z)) > glm::length(b->footprint) * 0.5f + wl.y + wl.x) continue;
      if (apron && autopathIsHouse(b)) {   // its own house: the door end touches it
        bool own = false; quat qo = yawQuat(o.yaw);
        for (auto& e : b->navMeta.entrances) { vec3 dp = o.pos + qo * vec3(e.p.x, 0, e.p.z); for (float s : {-1.0f, 1.0f}) { vec3 ep; vec2 d; pathEnd(a, p, pa, s, ep, d); own |= glm::length(vec2(ep.x - dp.x, ep.z - dp.z)) < 0.4f; } }
        if (own) continue;
      }
      quat qi = glm::inverse(yawQuat(o.yaw)); vec2 hf = b->footprint * 0.5f - vec2(0.02f);
      for (int k = 0; k <= 12; ++k) for (float x : {-0.46f, 0.0f, 0.46f}) {
        vec2 h; vec2 c = pathCentre(wl.y, sh.bend, -wl.y * 0.5f + wl.y * k / 12.0f, &h);
        vec2 l = c + vec2(h.y, -h.x) * (x * wl.x);
        vec3 w = p.pos + q * vec3(l.x, 0, l.y);
        vec3 lo = qi * (w - o.pos);
        if (std::fabs(lo.x) < hf.x && std::fabs(lo.z) < hf.y) {
          if (++hits <= 3) { char b2[160]; std::snprintf(b2, sizeof b2, " %s#%llu over %s#%llu at (%.1f, %.1f);", p.extra["auto"].get<std::string>().c_str(), (unsigned long long)p.id, o.assetId.c_str(), (unsigned long long)o.id, w.x, w.z); what += b2; }
          goto nextObst;
        }
      }
      nextObst:;
    }
  }
  return hits;
}
// chain continuity: every joint piece's two ends meet another piece's end
int looseJoints(App& a, int& joints) {
  std::vector<std::array<vec3, 2>> E; std::vector<const Instance*> P;
  for (auto& i : a.doc.instances) if (isAuto(i)) { P.push_back(&i); std::array<vec3, 2> e; vec2 d; pathEnd(a, i, a.assetFor(i), -1, e[0], d); pathEnd(a, i, a.assetFor(i), 1, e[1], d); E.push_back(e); }
  int loose = 0; joints = 0;
  for (size_t k = 0; k < P.size(); ++k) {
    if (std::fabs(pathShape(*P[k]).bend) < 1e-3f) continue;
    ++joints;
    for (int s = 0; s < 2; ++s) {
      bool met = false;
      for (size_t j = 0; j < P.size() && !met; ++j) if (j != k) for (int t = 0; t < 2 && !met; ++t) met = glm::length(vec2(E[k][s].x - E[j][t].x, E[k][s].z - E[j][t].z)) < 0.08f;
      loose += !met;
    }
  }
  return loose;
}

void networkChecks(const std::string& tag) {
  static AutoPathReport r1; static uint64_t h1 = 0; static int n1 = 0, before = 0, routes0 = 0;
  push(tag + ": connect houses", [tag](App& a, float t) {
    if (t == 0) {
      before = (int)a.doc.instances.size(); routes0 = autoRoutes(a);
      size_t undo0 = a.undoStack.size();
      r1 = autopathConnectHouses(a, true); a.syncRuntime();
      note("%s: %s", tag.c_str(), r1.summary().c_str());
      chk(a.undoStack.size() == undo0 + 1, "%s: Connect houses is one undo step (%zu -> %zu)", tag.c_str(), undo0, a.undoStack.size());
      chk(r1.houses >= 10 && r1.aprons >= r1.houses && r1.links >= r1.houses - 1 && r1.failed == 0,
          "%s: %d houses, %d entrance aprons, %d links (MST %d + %d loops), %d spurs, %d not routable", tag.c_str(), r1.houses, r1.aprons, r1.links, r1.links - r1.loops, r1.loops, r1.spurs, r1.failed);
      n1 = autoCount(a); h1 = layoutHash(a);
      chk(n1 == r1.pieces && n1 > r1.aprons && r1.joints > 0, "%s: %d auto path pieces on the map (= emitted), %d curved joints", tag.c_str(), n1, r1.joints);
      Net net = connectivity(a);
      chk(net.isolated == 0 && net.comps == 1, "%s: every house connected: %d houses in %d network(s) by the pieces' own end points; isolated:%s", tag.c_str(), net.houses, net.comps, net.lonely.empty() ? " none" : net.lonely.c_str());
      std::string what; int hits = footprintHits(a, what);
      chk(hits == 0, "%s: no path piece crosses a footprint (%d crossings)%s", tag.c_str(), hits, what.c_str());
      int joints = 0, loose = looseJoints(a, joints);
      chk(joints > 0 && loose == 0, "%s: chains are continuous: %d curved joints, %d joint ends not meeting a straight", tag.c_str(), joints, loose);
      int rr = autoRoutes(a);
      chk(rr == r1.routes && rr > 0, "%s: %d ground-wear footpath routes in doc.extra.routes (tagged auto)", tag.c_str(), rr);
      // the routes wear the ground along a link
      a.updateSurface();
      float wear = 0; int nW = 0;
      for (auto& i : a.doc.instances) if (isAuto(i) && i.extra["auto"] == "link" && std::fabs(pathShape(i).bend) < 1e-3f) { wear += a.terrain.wearAt(i.pos.x, i.pos.z); if (++nW >= 40) break; }
      chk(nW > 0 && wear / nW > 0.3f, "%s: the footpath routes wear the ground (mean wear %.2f at %d link centres)", tag.c_str(), nW ? wear / nW : 0.0f, nW);
      chk(r1.ms < 6000, "%s: Connect houses took %.0f ms", tag.c_str(), r1.ms);
    }
    return t > 0.3f;
  });
  push(tag + ": re-run, undo, redo", [tag](App& a, float t) {
    if (t == 0) {
      AutoPathReport r2 = autopathConnectHouses(a, true); a.syncRuntime();
      chk(autoCount(a) == n1 && layoutHash(a) == h1 && autoRoutes(a) == r1.routes, "%s: re-running replaces the auto paths only: %d pieces (was %d), same layout, %d routes", tag.c_str(), autoCount(a), n1, autoRoutes(a));
      a.undo(); a.syncRuntime();
      chk(autoCount(a) == n1 && layoutHash(a) == h1, "%s: undo of the re-run: the first run's paths", tag.c_str());
      a.undo(); a.syncRuntime();
      chk(autoCount(a) == 0 && (int)a.doc.instances.size() == before && autoRoutes(a) == routes0, "%s: undo of the first run: no auto paths, %zu instances (was %d), auto routes %d",
          tag.c_str(), a.doc.instances.size(), before, autoRoutes(a));
      a.redo(); a.syncRuntime();
      chk(autoCount(a) == n1 && layoutHash(a) == h1 && autoRoutes(a) == r1.routes, "%s: redo brings them back (%d pieces, %d routes)", tag.c_str(), autoCount(a), autoRoutes(a));
      (void)r2;
    }
    return t > 0.2f;
  });
  push(tag + ": view", [tag](App& a, float t) {
    static vec3 c;
    if (t == 0) {
      vec2 mn(1e9f), mx(-1e9f); for (auto& i : a.doc.instances) if (isAuto(i)) { mn = glm::min(mn, vec2(i.pos.x, i.pos.z)); mx = glm::max(mx, vec2(i.pos.x, i.pos.z)); }
      c = vec3((mn.x + mx.x) * 0.5f, 0, (mn.y + mx.y) * 0.5f); c.y = a.terrain.height(c.x, c.z);
      float ext = std::max(mx.x - mn.x, mx.y - mn.y);
      a.camPos = c + vec3(0, ext * 0.75f + 20, ext * 0.45f); a.camYaw = 0; a.camPitch = -58; a.selected = 0; a.R.resetTemporal();
    }
    if (t > 1.6f) { pendingShot = outDir() / ("autopath_" + tag + ".png"); note("screenshot selftest_out/autopath_%s.png", tag.c_str()); return true; }
    return false;
  });
  push(tag + ": close view", [tag](App& a, float t) {
    if (t == 0) {   // the first curved joint, from eye height a few metres back
      for (auto& i : a.doc.instances) if (isAuto(i) && std::fabs(pathShape(i).bend) > 20) {
        vec3 f = yawQuat(i.yaw) * vec3(0, 0, -7);
        a.camPos = i.pos + f + vec3(0, 3.2f, 0); a.camYaw = degrees(std::atan2(f.x, f.z)); a.camPitch = -24; break;
      }
      a.R.resetTemporal();
    }
    if (t > 1.4f) { pendingShot = outDir() / ("autopath_" + tag + "_joint.png"); note("screenshot selftest_out/autopath_%s_joint.png", tag.c_str()); return true; }
    return false;
  });
}

void build(App&) {
  push("setup", [](App& a, float t) {
    if (t == 0) { const TemplateInfo* tp = a.findTemplate("blank_slate"); chk(tp && a.newMap(*tp, "Autopath test"), "Blank Slate map"); }
    return t > 0.3f;
  });
  // ---------------------------------------------------------------- curved pieces
  push("curved path pieces", [](App& a, float) {
    const AssetDef* d = a.lib.find("path_dirt");
    if (!chk(d && d->path.on, "path_dirt asset")) return true;
    uint64_t id = a.placeInstance(d, vec3(0, 0, 0), 30); a.syncRuntime();
    Instance* i = a.doc.find(id);
    a.setPathSize(*i, vec2(1.8f, 6.0f)); setPathShape(*i, PathShape{60.0f, 2.5f}); a.bump(*i); a.syncRuntime();
    InstanceRT& r = a.rt[id]; a.rebuildPathMesh(*i, r);
    // mesh ends vs pathEnd: the first / last vertex rows' centres
    MeshData m; buildPathMeshDataEx(m, d->path, a.pathSize(*i, d), i->pos, i->yaw, [&a](float x, float z) { return a.terrain.height(x, z); }, nullptr, pathShape(*i));
    int nx = std::max(2, (int)std::ceil(1.8f / 0.5f) + 1);
    vec3 m0 = (m.v[0].pos + m.v[nx - 1].pos) * 0.5f, m1 = (m.v[m.v.size() - nx].pos + m.v.back().pos) * 0.5f;
    mat4 M = a.instanceMatrix(*i); vec3 w0 = vec3(M * vec4(m0, 1)), w1 = vec3(M * vec4(m1, 1));
    vec3 e0, e1; vec2 d0, d1; pathEnd(a, *i, d, -1, e0, d0); pathEnd(a, *i, d, 1, e1, d1);
    float err = std::max(glm::length(vec2(w0.x - e0.x, w0.z - e0.z)), glm::length(vec2(w1.x - e1.x, w1.z - e1.z)));
    float turn = degrees(std::acos(std::clamp(glm::dot(-d0, d1), -1.0f, 1.0f)));
    chk(err < 0.05f && std::fabs(turn - 60) < 1.0f, "a 60 deg curved piece: mesh ends within %.3f m of pathEnd, the end headings turn %.1f deg", err, turn);
    chk(std::fabs(m.v[0].pcoord.z - (2.5f - 3.0f)) < 1e-3f && std::fabs(m.v.back().pcoord.z - (2.5f + 3.0f)) < 1e-3f, "along-path coordinate runs u0 - L/2 .. u0 + L/2 (%.2f .. %.2f)", m.v[0].pcoord.z, m.v.back().pcoord.z);
    // resizing keeps the bend; the surface lookup follows the arc (the arc's middle is off the straight chord)
    a.setPathSize(*i, vec2(2.0f, 6.0f));
    chk(std::fabs(pathShape(*i).bend - 60) < 1e-3f && std::fabs(pathShape(*i).u0 - 2.5f) < 1e-3f, "resizing a curved piece keeps bend and u0");
    vec2 c = pathCentre(6.0f, 60.0f, 1.5f); vec3 on = i->pos + yawQuat(i->yaw) * vec3(c.x, 0, c.y);
    vec3 chordMid = (e1 + i->pos) * 0.5f;   // the chord from the middle to an end runs inside the curve
    std::string s1 = a.pathSurfaceAt(on), s2 = a.pathSurfaceAt(i->pos + yawQuat(i->yaw) * vec3(-1.6f, 0, 0));
    chk(s1 == "dirt" && s2.empty(), "surface lookup follows the arc: on it '%s', 1.6 m outside its outer side '%s'", s1.c_str(), s2.c_str()); (void)chordMid;
    // snapping a straight piece onto the curved end continues along the tangent
    vec3 hp = e1 + vec3(d1.x, 0, d1.y) * 0.4f; float yaw = 0;
    bool sn = a.snapPath(d, hp, yaw);
    vec3 fw = yawQuat(yaw) * vec3(0, 0, 1);
    float along = glm::dot(vec2(fw.x, fw.z), d1);
    vec3 back = hp - fw * (d->path.length * 0.5f) * (along > 0 ? 1.0f : -1.0f);
    chk(sn && std::fabs(along) > 0.999f && glm::length(vec2(back.x - e1.x, back.z - e1.z)) < 0.02f, "snapPath onto a curved end: in line with its tangent (|cos| %.4f), the new piece starts at the end (%.3f m)",
        std::fabs(along), glm::length(vec2(back.x - e1.x, back.z - e1.z)));
    // the grass footprint covers the arc's box
    auto fp = a.grassFootprints(); bool cov = false;
    for (auto& f : fp) if (glm::length(f.pos - vec2(i->pos.x, i->pos.z)) < 1e-3f) { vec2 mn, mx; pathLocalBounds(a, *i, d, mn, mx); cov = f.bmax.x > 0.5f && std::fabs(f.bmin.x - (mn.x + 0.06f)) < 1e-3f; }
    chk(cov, "the grass footprint of a curved piece is its arc's box");
    a.selected = id; a.deleteSelected(); a.syncRuntime();
    return true;
  });
  // ---------------------------------------------------------------- the two village maps
  push("open village_assault", [](App& a, float t) {
    if (t == 0) { const TemplateInfo* tp = a.findTemplate("village_assault"); chk(tp && a.newMap(*tp, "Autopath village"), "Normandy Village Assault map"); a.syncRuntime(); }
    return t > 1.0f;
  });
  networkChecks("village_assault");
  push("auto paths toggle", [](App& a, float t) {
    static int undo0 = 0, n0 = 0; static uint64_t H = 0;
    if (t == 0) {
      gAutoPaths = true; n0 = autoCount(a); undo0 = (int)a.undoStack.size();
      // a cottage on open ground near the network's edge: try spots on a ring around the village centre
      const AssetDef* d = a.lib.find("house_stone_cottage");
      vec2 mn(1e9f), mx(-1e9f); for (auto& i : a.doc.instances) if (isAuto(i)) { mn = glm::min(mn, vec2(i.pos.x, i.pos.z)); mx = glm::max(mx, vec2(i.pos.x, i.pos.z)); }
      vec2 c = (mn + mx) * 0.5f; float R = glm::length(mx - mn) * 0.35f;
      H = 0;
      for (int k = 0; k < 48 && !H && d; ++k) {
        float ang = k * 0.4f, rr = R * (0.6f + 0.05f * (k % 9));
        vec3 p(c.x + std::cos(ang) * rr, 0, c.y + std::sin(ang) * rr);
        if (a.checkPlacement(d, p, 0, 0).ok) H = a.placeInstance(d, p, degrees(-ang) + 90);
      }
      a.syncRuntime();
      chk(H != 0, "a cottage placed on open ground with Auto paths on");
      Net net = connectivity(a);
      chk(H && (int)a.undoStack.size() == undo0 + 1 && autoCount(a) > n0 && net.isolated == 0 && net.comps == 1,
          "Auto paths: the new house is connected in the placement's own undo step (%d -> %d pieces, %d network(s), isolated:%s)", n0, autoCount(a), net.comps, net.lonely.empty() ? " none" : net.lonely.c_str());
      a.undo(); a.syncRuntime();
      chk(!a.doc.find(H) && autoCount(a) == n0, "one undo removes the house and restores the previous paths (%d pieces)", autoCount(a));
      gAutoPaths = false;
    }
    return t > 0.2f;
  });
  push("open battle1", [](App& a, float t) {
    if (t == 0) { const TemplateInfo* tp = a.findTemplate("battle1"); chk(tp && a.newMap(*tp, "Autopath battle1"), "Battle 1 map"); a.syncRuntime(); }
    return t > 1.0f;
  });
  networkChecks("battle1");
  // ---------------------------------------------------------------- L3: lamp posts lining the routes (docs/lights.md)
  push("battle1: lamp posts along the auto paths", [](App& a, float t) {
    if (t != 0) return t > 0.3f;
    gAutoPathLamps = true;
    size_t undo0 = a.undoStack.size();
    AutoPathReport r = autopathConnectHouses(a, true); a.syncRuntime();
    gAutoPathLamps = false;
    std::vector<const Instance*> lamps; int paths = 0;
    for (auto& i : a.doc.instances) if (isAuto(i)) { if (i.extra["auto"] == "lamp") lamps.push_back(&i); else ++paths; }
    note("battle1 with lamp posts: %s", r.summary().c_str());
    chk(a.undoStack.size() == undo0 + 1 && r.lamps >= 6 && (int)lamps.size() == r.lamps && paths == r.pieces,
        "Connect houses with \"Line auto paths with lamp posts\": %d lamp posts (extra.auto = \"lamp\", report %d) beside %d path pieces, one undo step", (int)lamps.size(), r.lamps, paths);
    float minD = 1e9f, worstOff = 0; int onPath = 0, wrongAsset = 0;
    std::vector<LampLine> lines = lightsRoutesOf(a.doc.extra.is_object() ? a.doc.extra.value("routes", json::array()) : json::array());
    for (size_t x = 0; x < lamps.size(); ++x) {
      for (size_t y = x + 1; y < lamps.size(); ++y) minD = std::min(minD, glm::length(vec2(lamps[x]->pos.x - lamps[y]->pos.x, lamps[x]->pos.z - lamps[y]->pos.z)));
      onPath += !a.pathSurfaceAt(lamps[x]->pos).empty();
      wrongAsset += lamps[x]->assetId != "lamp_lantern_post";
      float best = 1e9f; vec2 p(lamps[x]->pos.x, lamps[x]->pos.z);
      for (auto& l : lines) for (size_t k = 0; k + 1 < l.pts.size(); ++k) {
        vec2 a0 = l.pts[k], d = l.pts[k + 1] - a0; float L2 = glm::dot(d, d);
        float u = L2 > 1e-6f ? std::clamp(glm::dot(p - a0, d) / L2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::length(p - (a0 + d * u)));
      }
      worstOff = std::max(worstOff, best);
    }
    chk(minD >= 21.9f && onPath == 0 && wrongAsset == 0 && worstOff < 3.5f, "lamp posts: at least %.1f m apart (spacing 22), %d standing on a path, all within %.2f m of their route, %d of another asset",
        minD, onPath, worstOff, wrongAsset);
    lightsSetDaylight(0.0f);
    std::vector<LightDesc> L; lightsGather(a, L);
    std::set<uint64_t> ids; for (auto& l : L) ids.insert(l.id / 16);
    int litLamps = 0; for (auto* l : lamps) litLamps += ids.count(l->id) > 0;
    lightsSetDaylight(1.0f); L.clear(); lightsGather(a, L);
    std::set<uint64_t> dayIds; for (auto& l : L) dayIds.insert(l.id / 16);
    int dayLamps = 0; for (auto* l : lamps) dayLamps += dayIds.count(l->id) > 0;
    lightsSetDaylight(-1.0f);
    chk(litLamps == (int)lamps.size() && dayLamps == 0, "the lamp posts light at night (%d of %d) and are dark by day (%d)", litLamps, (int)lamps.size(), dayLamps);
    // re-running replaces them; Remove auto paths takes them away with the paths
    gAutoPathLamps = true; AutoPathReport r2 = autopathConnectHouses(a, true); a.syncRuntime(); gAutoPathLamps = false;
    int again = 0; for (auto& i : a.doc.instances) again += isAuto(i) && i.extra["auto"] == "lamp";
    chk(again == r.lamps && r2.lamps == r.lamps, "re-running replaces the lamp posts: %d (was %d), nothing duplicated", again, r.lamps);
    int removed = autopathRemove(a); a.syncRuntime();
    int left = 0; for (auto& i : a.doc.instances) left += isAuto(i);
    chk(left == 0 && removed == r.pieces + r.lamps, "Remove auto paths: %d pieces removed (paths and lamp posts), %d auto instances left", removed, left);
    a.undo(); a.syncRuntime();
    return false;
  });
  push("finish", [](App& a, float t) {
    if (t < 0.3f) return false;
    chk(a.R.shaderErrors == 0, "shader errors: %d", a.R.shaderErrors);
    return true;
  });
}
}  // namespace

bool autopathSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-autopath") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("autopath"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 300) { T->check(false, "autopath test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
