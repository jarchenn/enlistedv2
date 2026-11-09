// --selftest-structures (W4, SPEC 3.6): the new Atlantic Wall / beach / bocage / village catalogue assets.
//  * every catalogue asset carries "nav" metadata (the bunkers' is W2's: reported as pending until it lands);
//  * each W4 asset (an asset with a "tour") validates its nav block (effect, rooms <-> doors / entrances, area
//    outlines, terrain cuts) and its colliders (static shape, a free standing spot in every room, clear sight from
//    embrasure / MG / firestep firing points, cover points with something solid in front of them);
//  * each is placed on a Blank Slate at a rotation, its terrain cut applied (a stand-in for W3's terrain code),
//    companions / a joining trench piece placed, framed for a screenshot, then walked through its tour with the
//    real character controller (same step vocabulary as "walkthrough": goto / interact / look / shot);
//  * "climb" probes: the character is driven at a face with jumps and vault input and must stay below it (AT ditch).
#include "selftest.h"
#include "physics.h"
#include <cstdlib>
#include <cstring>
#if __has_include("editor_nav.h")   // W3 terrain cuts present (claude/changes): the suite uses them, not its stand-in
#include "editor_nav.h"
#define W4_REAL_CUTS 1
#else
#define W4_REAL_CUTS 0
#endif

namespace {
std::vector<std::string> sIds;
size_t sIdx = 0;
uint64_t sJoin = 0;
size_t sClimb = 0;
float sFloor = 0;

bool inPoly(vec2 p, const std::vector<vec2>& poly) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    if (((poly[i].y > p.y) != (poly[j].y > p.y)) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
  return in;
}
float edgeDist(vec2 p, const std::vector<vec2>& poly) {
  float d = 1e9f;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    vec2 a = poly[j], b = poly[i], ab = b - a;
    float t = std::clamp(glm::dot(p - a, ab) / std::max(1e-6f, glm::dot(ab, ab)), 0.0f, 1.0f);
    d = std::min(d, glm::length(p - (a + ab * t)));
  }
  return d;
}
// Terrain cuts. With W3's terrain (editor_nav.h: navSyncCuts, applied every frame by w3Frame) the suite uses the real
// cuts and only probes them; without it, applyCuts() is a stand-in that lowers the ground inside each outline to
// (instance y - depth), sloping over `bank` inside the edge, with W3's outline pad of one grid step, so the suite can
// still walk below-grade assets and sees the same cut as W3 makes.
// Never both: the stand-in's deltas would be applied on top of W3's cut base and the ground would sink twice.
struct CutProbe { vec3 p; float want; };
#if W4_REAL_CUTS
std::vector<CutProbe> cutProbes(App& app, const Instance& in, const AssetDef* a) {   // deepest grid point per cut
  std::vector<CutProbe> probes;
  quat q = yawQuat(in.yaw);
  float fs = app.terrain.fineSpacing();
  for (auto& c : a->navMeta.terrainCuts) {
    std::vector<vec2> w; vec2 lo(1e9f), hi(-1e9f);
    for (auto& p : c.outline) { vec3 v = in.pos + q * vec3(p.x, 0, p.y); w.push_back(vec2(v.x, v.z)); lo = glm::min(lo, w.back()); hi = glm::max(hi, w.back()); }
    CutProbe best{vec3(0), 0}; float bestD = -1;
    for (float z = lo.y; z <= hi.y; z += fs * 0.5f)
      for (float x = lo.x; x <= hi.x; x += fs * 0.5f) {
        vec2 p(x, z);
        if (!inPoly(p, w)) continue;
        float d = edgeDist(p, w);
        if (d > bestD) { bestD = d; best = {vec3(p.x, 0, p.y), in.pos.y - c.depth}; }
      }
    if (bestD > 0.05f && bestD >= c.bank) probes.push_back(best);   // padded by fs: every grid sample within fs is at full depth
  }
  return probes;
}
#else
std::vector<CutProbe> applyCuts(App& app, const Instance& in, const AssetDef* a) {
  std::vector<CutProbe> probes;
  Terrain& T = app.terrain;
  quat q = yawQuat(in.yaw);
  float ts = T.tileSize(), fs = T.fineSpacing();
  for (auto& c : a->navMeta.terrainCuts) {
    std::vector<vec2> w; vec2 lo(1e9f), hi(-1e9f);
    for (auto& p : c.outline) { vec3 v = in.pos + q * vec3(p.x, 0, p.y); w.push_back(vec2(v.x, v.z)); lo = glm::min(lo, w.back()); hi = glm::max(hi, w.back()); }
    const float pad = fs;   // W3: outlines padded outward by one fine-grid step (content/settings/editor.json edgePad -1)
    lo -= vec2(pad); hi += vec2(pad);
    int tx0 = (int)std::floor((lo.x + T.size * 0.5f) / ts), tx1 = (int)std::floor((hi.x + T.size * 0.5f) / ts);
    int tz0 = (int)std::floor((lo.y + T.size * 0.5f) / ts), tz1 = (int)std::floor((hi.y + T.size * 0.5f) / ts);
    CutProbe best{vec3(0), 0}; float bestD = -1;
    for (int tz = tz0; tz <= tz1; ++tz)
      for (int tx = tx0; tx <= tx1; ++tx) {
        Terrain::Tile* t = T.activate(tx, tz);
        if (!t) continue;
        vec2 o = T.tileOrigin(tx, tz);
        for (int j = 0; j < Terrain::kFineN; ++j)
          for (int i = 0; i < Terrain::kFineN; ++i) {
            vec2 p(o.x + i * fs, o.y + j * fs);
            float d = (inPoly(p, w) ? 1.0f : -1.0f) * edgeDist(p, w), dp = d + pad;
            if (dp <= 0) continue;
            float k = c.bank > 1e-3f ? std::min(1.0f, dp / c.bank) : 1.0f;
            float& h = t->h[j * Terrain::kFineN + i];
            h = std::min(h, in.pos.y - c.depth * k);
            if (d > bestD) { bestD = d; best = {vec3(p.x, 0, p.y), in.pos.y - c.depth}; }
          }
      }
    if (bestD > 0.05f && bestD >= c.bank) probes.push_back(best);   // padded by fs: every grid sample within fs is at full depth
  }
  return probes;
}
#endif
bool w2Owned(const std::string& id) { return id.rfind("bunker_", 0) == 0 || id == "gun_mg42_embrasure"; }
// ---- coplanar overlaps (z-fighting), the rule of tools/zfight_check.mjs: two visible box faces of different parts
// (static parts, every wall bay's intact state, the closed door / window leaves) that face the same way, lie within
// 6 mm and overlap in area, unless a third solid sits within 3 mm in front of the whole overlap (never visible).
struct ZBox { vec3 c, ax[3], h; };
struct ZFace { int pi; vec3 n, fc, u, v; float d, hu, hv; };
float zArea(const std::vector<vec2>& P) { float s = 0; for (size_t i = 0; i < P.size(); ++i) { vec2 a = P[i], b = P[(i + 1) % P.size()]; s += a.x * b.y - b.x * a.y; } return s / 2; }
std::vector<vec2> zClip(const std::vector<vec2>& poly, vec2 a, vec2 b) {
  std::vector<vec2> out; auto side = [&](vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
  for (size_t i = 0; i < poly.size(); ++i) {
    vec2 p = poly[i], q = poly[(i + 1) % poly.size()]; float sp = side(p), sq = side(q);
    if (sp >= 0) out.push_back(p);
    if ((sp >= 0) != (sq >= 0)) out.push_back(p + (q - p) * (sp / (sp - sq)));
  }
  return out;
}
int coplanarOverlaps(const json& j, std::string& why) {
  std::vector<ZBox> B;
  auto push = [&](const json& list) {
    for (auto& p : list) {
      std::string t = p.value("t", std::string("b"));
      if (p.value("v", 1) == 0 || (t != "b" && t != "r")) continue;
      quat q(1, 0, 0, 0);
      if (p.contains("q")) q = glm::normalize(quat(p["q"][3].get<float>(), p["q"][0].get<float>(), p["q"][1].get<float>(), p["q"][2].get<float>()));
      else if (p.contains("r")) q = quat(glm::radians(jvec3(p["r"])));
      ZBox b; b.c = jvec3(p["c"]); b.h = jvec3(p["s"]) * 0.5f;
      for (int i = 0; i < 3; ++i) { vec3 e(0); e[i] = 1; b.ax[i] = q * e; }
      B.push_back(b);
    }
  };
  push(j.value("parts", json::array()));
  for (auto& pn : j.value("panels", json::array())) if (pn.contains("states") && !pn["states"].empty()) push(pn["states"][0]);
  for (auto& it : j.value("interactables", json::array())) for (auto& L : it.value("leaves", json::array())) push(L.value("parts", json::array()));
  std::vector<ZFace> F;
  for (int pi = 0; pi < (int)B.size(); ++pi) for (int i = 0; i < 3; ++i) for (float s : {-1.0f, 1.0f}) {
    const ZBox& b = B[pi]; ZFace f; f.pi = pi; f.n = b.ax[i] * s; f.fc = b.c + f.n * b.h[i]; f.d = glm::dot(f.n, f.fc);
    f.u = b.ax[(i + 1) % 3]; f.v = b.ax[(i + 2) % 3]; f.hu = b.h[(i + 1) % 3]; f.hv = b.h[(i + 2) % 3]; F.push_back(f);
  }
  const float EPS = 0.006f;
  std::map<std::string, std::vector<int>> buckets;
  auto nkey = [](vec3 n) { char k[64]; std::snprintf(k, sizeof k, "%d,%d,%d", (int)std::lround(n.x * 200), (int)std::lround(n.y * 200), (int)std::lround(n.z * 200)); return std::string(k); };
  for (int fi = 0; fi < (int)F.size(); ++fi) buckets[nkey(F[fi].n) + "|" + std::to_string((long)std::lround(F[fi].d / EPS))].push_back(fi);
  auto inside = [&](const ZBox& b, vec3 p) { vec3 d = p - b.c; for (int i = 0; i < 3; ++i) if (std::fabs(glm::dot(d, b.ax[i])) >= b.h[i] - 1e-4f) return false; return true; };
  std::set<std::pair<int, int>> seen; int n = 0;
  for (auto& [key, list] : buckets) {
    size_t bar = key.find('|'); std::string nk = key.substr(0, bar); long dk = std::stol(key.substr(bar + 1));
    std::vector<int> cand = list;
    for (long dd : {-1L, 1L}) { auto it = buckets.find(nk + "|" + std::to_string(dk + dd)); if (it != buckets.end()) cand.insert(cand.end(), it->second.begin(), it->second.end()); }
    for (int fa : list) for (int fb : cand) {
      if (fb <= fa) continue;
      const ZFace &A = F[fa], &Bf = F[fb];
      if (A.pi == Bf.pi || glm::dot(A.n, Bf.n) < 0.9999f || std::fabs(A.d - Bf.d) > EPS) continue;
      auto pk = std::make_pair(std::min(A.pi, Bf.pi), std::max(A.pi, Bf.pi)); if (seen.count(pk)) continue;
      auto to2 = [&](vec3 p) { vec3 d = p - A.fc; return vec2(glm::dot(d, A.u), glm::dot(d, A.v)); };
      std::vector<vec2> poly;
      for (auto [su, sv] : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) poly.push_back(to2(Bf.fc + Bf.u * (su * Bf.hu) + Bf.v * (sv * Bf.hv)));
      if (zArea(poly) < 0) std::reverse(poly.begin(), poly.end());
      vec2 R[4] = {{-A.hu, -A.hv}, {A.hu, -A.hv}, {A.hu, A.hv}, {-A.hu, A.hv}};
      for (int e = 0; e < 4 && !poly.empty(); ++e) poly = zClip(poly, R[e], R[(e + 1) % 4]);
      if (poly.size() < 3 || std::fabs(zArea(poly)) < 1e-4f) continue;
      vec2 c(0); for (auto& p : poly) c += p; c /= (float)poly.size();
      std::vector<vec2> pts{c};
      for (auto& p : poly) { vec2 d = p - c; float l = std::max(glm::length(d), 1e-6f); pts.push_back(p - d / l * std::min(0.005f, l * 0.5f)); }
      float front = std::max(A.d, Bf.d) - A.d + 0.003f;
      bool hidden = true;
      for (auto& q : pts) {
        vec3 w = A.fc + A.u * q.x + A.v * q.y + A.n * front; bool in = false;
        for (int pi = 0; pi < (int)B.size() && !in; ++pi) if (pi != A.pi && pi != Bf.pi && inside(B[pi], w)) in = true;
        if (!in) { hidden = false; break; }
      }
      if (hidden) continue;
      seen.insert(pk); ++n;
      if (why.size() < 160) { char b[96]; vec3 at = A.fc + A.u * c.x + A.v * c.y; std::snprintf(b, sizeof b, " (%.2f,%.2f,%.2f)", at.x, at.y, at.z); why += b; }
    }
  }
  return n;
}
// per asset: overlaps accepted today (assets this check found them in when it was added; GX4's exterior pass resolves
// the B2 / B3 buildings in tools/exterior.mjs unfight()). Any asset above its number, or any other enterable asset with
// one, fails.
const std::map<std::string, int> kZAllowed = {
  {"aw_dugout_personnel", 5}, {"aw_r612_unfinished", 11}, {"aw_r667_casemate", 8}, {"aw_tobruk_mg", 12}, {"aw_tobruk_turret", 4},
  {"bch_lcvp_beached", 27}, {"bch_lcvp_intact", 27}, {"bch_lcvp_wrecked", 32}, {"bunker_command_shelter", 15}, {"bunker_field_shelter", 5},
  {"bunker_large_casemate", 31}, {"bunker_mg_casemate", 18}, {"bunker_observation_post", 11}, {"bunker_pak_casemate", 20},
  {"ruin_barn_burnt", 7}, {"ruin_cottage_corner", 18}, {"ruin_farmhouse_gutted", 35}, {"ruin_house_shelled", 79}, {"ruin_shop_row", 47},
  {"ruin_townhouse_facade", 228}, {"vil_chateau_keep", 28}, {"vil_church_ruined", 21},
  {"house_farmhouse_2story", 4}, {"large_stone_warehouse", 2}, {"large_village_inn", 5}, {"vil_chateau_grand", 17}, {"vil_longhouse_thatched", 9},
  {"vil_rectory", 1}, {"vil_villa_deauville", 5},
};

const char* effName(NavEffect e) { static const char* n[] = {"auto", "block", "carve", "area", "none"}; return n[(int)e]; }
float yawToCam(vec3 from, vec3 to) { vec3 d = to - from; return degrees(std::atan2(-d.x, -d.z)); }
vec3 facing(float yawDeg) { return vec3(std::sin(radians(yawDeg)), 0, std::cos(radians(yawDeg))); }   // asset yaw convention
}

void SelfTest::structuresUpdate(App& app, float dt) {
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  case 3800: {
    reportName = "structures";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    if (!t) { check(false, "Blank Slate template"); finish(app); return; }
    app.newMap(*t, "Structures (W4)");
    // ---- catalogue-wide: nav metadata on every asset
    int authored = 0, pending = 0;
    for (auto& up : app.lib.assets) {
      const AssetDef* a = up.get();
      if (a->navMeta.authored) {
        ++authored;
        check(true, a->id + ": nav " + effName(a->navMeta.effect) + (a->navMeta.area != NavArea::Default ? std::string(" / ") + navAreaName(a->navMeta.area) : "") +
                    (a->navMeta.hasAnnotations() ? " + annotations" : "") + (a->navMeta.terrainCuts.empty() ? "" : " + terrain cut"));
      } else if (w2Owned(a->id)) { ++pending; check(true, a->id + ": nav pending (bunker metadata is W2's)"); }
      else check(false, a->id + ": has no nav metadata");
      const char* only = std::getenv("NMK_STRUCTURES_ONLY");   // comma-separated asset ids: walk only those (B3 dev runs)
      if (a->raw.contains("tour") && (!only || std::strstr(only, a->id.c_str()))) sIds.push_back(a->id);
    }
    {   // ---- no coplanar overlaps (z-fighting) on any enterable asset (GX4, tools/zfight_check.mjs)
      int total = 0, bad = 0; std::string worst;
      for (auto& up : app.lib.assets) {
        const AssetDef* a = up.get();
        if (!a->enterable) continue;
        std::string why; int n = coplanarOverlaps(a->raw, why); total += n;
        auto it = kZAllowed.find(a->id); int allow = it == kZAllowed.end() ? 0 : it->second;
        if (n > allow) { ++bad; if (worst.size() < 400) worst += " " + a->id + " " + std::to_string(n) + " > " + std::to_string(allow) + why + ";"; }
      }
      summary["zfight"] = {{"overlaps", total}, {"assetsOver", bad}};
      check(bad == 0, "no new coplanar overlaps (same-facing faces within 6 mm): " + std::to_string(total) + " in all enterable assets, all within the accepted list" + (bad ? ";" + worst : ""));
      if (std::getenv("NMK_ZFIGHT_ONLY")) { finish(app); return; }
    }
    summary["catalogue"] = {{"assets", app.lib.assets.size()}, {"navAuthored", authored}, {"navPendingW2", pending}, {"w4Assets", sIds.size()}};
    check(sIds.size() >= 30, "W4 structure assets found: " + std::to_string(sIds.size()));
    // ---- per W4 asset: nav block and collider sanity (asset space)
    for (auto& id : sIds) {
      const AssetDef* a = app.lib.find(id);
      const NavMeta& m = a->navMeta;
      const json& tour = a->raw["tour"];
      bool ok = m.effect != NavEffect::Auto;
      check(ok, id + ": explicit nav effect (" + effName(m.effect) + ")");
      if (m.effect == NavEffect::Area) check(m.area != NavArea::Default || !m.areas.empty(), id + ": area effect names an area type");
      std::set<std::string> rooms;
      bool roomsOk = true;
      for (auto& r : m.rooms) { rooms.insert(r.id); roomsOk &= r.max.x > r.min.x + 0.5f && r.max.y > r.min.y + 1.0f && r.max.z > r.min.z + 0.5f; }
      if (a->enterable) check(!m.rooms.empty() && !m.entrances.empty() && !m.doors.empty() && !m.corners.empty(), id + ": enterable: rooms, doors, entrances and corners annotated");
      bool linksOk = true;
      for (auto& d : m.doors) linksOk &= rooms.count(d.a) && (d.b == "outside" || rooms.count(d.b));
      for (auto& e : m.entrances) linksOk &= rooms.count(e.room) > 0;
      for (auto& c : m.corners) linksOk &= rooms.count(c.room) > 0;
      if (!m.rooms.empty()) check(roomsOk && linksOk, id + ": " + std::to_string(m.rooms.size()) + " rooms, doors / entrances / corners reference them");
      bool polyOk = true; for (auto& p : m.areas) polyOk &= p.outline.size() >= 3; for (auto& c : m.terrainCuts) polyOk &= c.outline.size() >= 3 && c.depth > 0.1f;
      check(polyOk, id + ": area outlines and terrain cuts well formed (" + std::to_string(m.areas.size()) + " areas, " + std::to_string(m.terrainCuts.size()) + " cuts)");
      if (tour.value("cut", false)) check(!m.terrainCuts.empty(), id + ": below grade: carries a terrainCut");
      vec3 ext = a->bmax - a->bmin;
      bool bOk = std::isfinite(ext.x) && std::isfinite(ext.y) && std::isfinite(ext.z) && ext.x > 0.1f && ext.y > 0.05f && ext.z > 0.1f && std::max({ext.x, ext.y, ext.z}) < 60.0f;
      int solidParts = 0; for (auto& p : a->raw.value("parts", json::array())) solidParts += p.value("k", 1) != 0;
      bool shapeOk = a->shape != nullptr || a->tree.stumpShape != nullptr || a->tree.trunkShape != nullptr || (solidParts == 0 && (m.effect == NavEffect::Area || m.effect == NavEffect::None));
      check(shapeOk && bOk, id + ": " + (solidParts ? "static collision shape" : "no colliders by design (walk-through area)") + ", bounds " + std::to_string(ext.x).substr(0, 5) + " x " + std::to_string(ext.y).substr(0, 5) + " x " + std::to_string(ext.z).substr(0, 5) + " m");
    }
    sIdx = 0;
    go(3801);
    break;
  }
  case 3801: {   // place the next asset (plus join / companions / cut) and run its in-world collider checks
    if (sIdx >= sIds.size()) { finish(app); return; }
    const AssetDef* a = app.lib.find(sIds[sIdx]);
    const json& tour = a->raw["tour"];
    size_t k = sIdx;
    float yaw = wrapDeg(23.0f + 67.0f * (float)k);
    vec3 pos(-260.0f + 52.0f * (float)(k % 11), 0, -240.0f + 64.0f * (float)(k / 11));
    houseId = app.placeInstance(a, pos, yaw);
    check(houseId != 0, a->id + ": placed at yaw " + std::to_string((int)yaw));
    if (!houseId) { ++sIdx; break; }
    curAsset = a->id; pass = 0; sJoin = 0;
    Instance in = *app.doc.find(houseId);
    std::vector<std::pair<Instance, const AssetDef*>> cutList{{in, a}};
    if (tour.contains("join")) {
      const json& jn = tour["join"];
      const AssetDef* ja = app.lib.find(jn.value("asset", ""));
      vec3 jp = toWorld(app, vec3(jn["pos"][0].get<float>(), 0, jn["pos"][1].get<float>()));
      sJoin = ja ? app.placeInstance(ja, jp, worldYaw(app, jn.value("yaw", 0.0f))) : 0;
      check(sJoin != 0, a->id + ": joining " + jn.value("asset", "") + " placed end to end");
      if (sJoin) { app.doc.find(sJoin)->pos.y = in.pos.y; app.bump(*app.doc.find(sJoin)); cutList.push_back({*app.doc.find(sJoin), ja}); }
    }
    if (tour.value("companions", false)) { int n = app.placeCompanions(houseId); check(n > 0, a->id + ": " + std::to_string(n) + " companion(s) placed"); }
    if (tour.value("cut", false)) {
      std::vector<CutProbe> probes;
#if W4_REAL_CUTS
      navSyncCuts(app);   // W3's cuts now rather than at the next frame, so the probes below see them
      for (auto& [ci, ca] : cutList) { auto p = cutProbes(app, ci, ca); probes.insert(probes.end(), p.begin(), p.end()); }
#else
      for (auto& [ci, ca] : cutList) { auto p = applyCuts(app, ci, ca); probes.insert(probes.end(), p.begin(), p.end()); }
      app.doc.terrainDeltas = app.terrain.deltas();
      app.resetGround();
#endif
      bool ok = !probes.empty();
      float worst = 0;
      for (auto& p : probes) { float e = std::fabs(app.terrain.height(p.p.x, p.p.z) - p.want); worst = std::max(worst, e); ok &= e < 0.12f; }
      check(ok, a->id + ": terrain cut applied (" + std::to_string(probes.size()) + " cuts, worst depth error " + std::to_string(worst).substr(0, 4) + " m)");
    }
    app.syncRuntime();
    // rooms: at least one spot where a standing man fits
    quat q = yawQuat(in.yaw);
    for (auto& r : a->navMeta.rooms) {
      bool free = false;
      for (float x = r.min.x + 0.35f; x <= r.max.x - 0.35f && !free; x += 0.25f)
        for (float z = r.min.z + 0.35f; z <= r.max.z - 0.35f && !free; z += 0.25f)
          free = !Physics::capsuleBlocked(in.pos + q * vec3(x, r.min.y + 0.06f, z), std::min(1.75f, r.max.y - r.min.y - 0.05f), 0.28f);
      check(free, a->id + ": room '" + r.id + "' has free standing room");
    }
    // firing points: a clear line of sight 4 m out at eye height (embrasure / mg / firestep / unglazed openings;
    // glazed windows have casements, mortar positions fire indirect)
    const json& rawFp = a->raw["nav"].value("firingPoints", json::array());
    for (size_t f = 0; f < a->navMeta.firingPoints.size(); ++f) {
      auto& fp = a->navMeta.firingPoints[f];
      bool unglazed = f < rawFp.size() && rawFp[f].value("unglazed", false);
      if ((fp.kind == "window" && !unglazed) || (f < rawFp.size() && rawFp[f].value("indirect", false))) continue;
      float eye = fp.kind == "mg" || fp.kind == "embrasure" || fp.kind == "firestep" ? 1.6f : 1.5f;
      vec3 o = in.pos + q * (fp.p + vec3(0, eye, 0)), d = q * facing(fp.yaw);
      Physics::Hit h;
      bool hit = Physics::castRayEx(o, d, 4.0f, h);
      check(!hit, a->id + ": " + fp.kind + " firing point " + std::to_string(f) + " sees out" + (hit ? " (blocked at " + std::to_string(h.t).substr(0, 4) + " m)" : ""));
    }
    // cover points: standing ground under them and something solid toward the threat within 2.5 m
    for (size_t c = 0; c < a->navMeta.cover.size(); ++c) {
      auto& cv = a->navMeta.cover[c];
      vec3 p = in.pos + q * cv.p, d = q * facing(cv.yaw);
      Physics::Hit g, h;
      bool ground = Physics::castRayEx(p + vec3(0, 1.0f, 0), vec3(0, -1, 0), 2.2f, g);
      bool solid = Physics::castRayEx(p + vec3(0, cv.high ? 1.45f : 0.75f, 0), d, 2.5f, h);
      check(ground && solid, a->id + ": cover point " + std::to_string(c) + (cv.high ? " (high)" : " (low)") + (ground ? "" : " has no ground") + (solid ? "" : " has nothing in front"));
    }
    // tour "probes" (B1 part colliders: cylinders, cones, arches, wedges, bevelled boxes): a ray from o along d, in asset
    // space, hits at `hit` m (within tol) or, without `hit`, runs clear for `len` m
    for (auto& pr : tour.value("probes", json::array())) {
      vec3 o = in.pos + q * jvec3(pr["o"]), d = q * glm::normalize(jvec3(pr["d"]));
      float want = pr.value("hit", -1.0f), tol = pr.value("tol", 0.03f);
      Physics::Hit h;
      bool hit = Physics::castRayEx(o, d, want > 0 ? want + 1.0f : pr.value("len", 3.0f), h);
      bool ok = want > 0 ? hit && std::fabs(h.t - want) <= tol : !hit;
      char b[300];
      std::snprintf(b, sizeof b, "%s: collider probe '%s': %s (want %s%.3f m, tol %.3f)", a->id.c_str(), pr.value("note", std::string()).c_str(),
                    hit ? ("hit at " + std::to_string(h.t).substr(0, 5) + " m").c_str() : "clear", want > 0 ? "a hit at " : "clear for ", want > 0 ? want : pr.value("len", 3.0f), tol);
      check(ok, b);
    }
    // frame the asset for its screenshot
    float R = std::max({a->footprint.x, a->footprint.y, a->bmax.y - a->bmin.y, 4.0f});
    vec3 ctr = in.pos + q * ((a->bmin + a->bmax) * 0.5f);
    app.camPos = in.pos + q * vec3(R * 0.7f, 0, R * 0.95f) + vec3(0, R * 0.45f + 2.0f, 0);
    app.camYaw = yawToCam(app.camPos, ctr);
    app.camPitch = degrees(std::atan2(ctr.y - app.camPos.y, glm::length(vec2(ctr.x - app.camPos.x, ctr.z - app.camPos.z))));
    app.selected = 0;
    summary["assets"][a->id] = {{"name", a->name}, {"category", a->category}, {"screenshot", "selftest_out/mpu_" + a->id + ".png"}, {"enterable", a->enterable},
                                {"tourSteps", tour.value("steps", json::array()).size()}, {"cut", tour.value("cut", false)}};
    go(3802);
    break;
  }
  case 3802: {
    if (stageTime > 0.6f && stageTime < 0.6f + dt * 1.5f) shotQueue.push_back("mpu_" + curAsset);
    if (stageTime < 0.9f) break;
    const AssetDef* a = app.lib.find(curAsset);
    const json& tour = a->raw["tour"];
    if (!tour.contains("steps") || tour["steps"].empty()) { ++sIdx; go(3801); break; }
    json st = tour["start"];
    app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.enterExplore(), curAsset + ": F5 enters Explore at the tour start"); app.useScripted = true;
    steps.clear(); for (auto& s : tour["steps"]) steps.push_back(s);
    stepIdx = 0; stepTime = 0; ++pass;
    go(3803);
    break;
  }
  case 3803:
    if (runWalkthrough(app, dt)) {
      const json& tour = app.lib.find(curAsset)->raw["tour"];
      if (tour.contains("climb")) { sClimb = 0; go(3804); break; }
      app.exitExplore(); ++sIdx; go(3801);
    }
    break;
  case 3804: {   // climb probes: from a floor point, drive at the face with sprint, jumps and vault input
    const json& cl = app.lib.find(curAsset)->raw["tour"]["climb"];
    if (sClimb >= cl.size()) { app.exitExplore(); ++sIdx; go(3801); break; }
    const json& c = cl[sClimb];
    Instance* in = app.doc.find(houseId);
    sFloor = c.value("floorY", 0.0f);
    vec3 tgt = toWorld(app, vec3(c["to"][0].get<float>(), 0, c["to"][1].get<float>()));
    if (stageTime <= dt * 1.5f) Physics::setCharacterPos(toWorld(app, vec3(c["from"][0].get<float>(), 0, c["from"][1].get<float>())) + vec3(0, sFloor + 0.05f, 0));
    auto ch = Physics::character();
    app.scriptedInput = ExploreInput{};
    app.eyeYaw = yawToCam(ch.pos, tgt); app.eyePitch = 0;
    app.scriptedInput.move = vec2(0, 1); app.scriptedInput.sprint = true;
    int fr = (int)(stageTime * 60.0f);
    app.scriptedInput.jump = fr % 40 < 2; app.scriptedInput.jumpHeld = fr % 40 < 20;
    if (stageTime > 6.0f) {
      float ly = ch.pos.y - in->pos.y;
      char b[200]; std::snprintf(b, sizeof b, "%s: %s: still in the cut after 6 s of sprint / jump / vault at the face (feet %.2f m, face top 0.0 m)", curAsset.c_str(), c.value("note", std::string("face")).c_str(), ly);
      check(ly < sFloor + 1.4f, b);
      ++sClimb; stageTime = 0;
    }
    break;
  }
  }
}
