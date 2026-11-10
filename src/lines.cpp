// LN1 line features (lines.h; docs/lines.md): the line-type registry, the line data on an Instance, the spline.
#include "lines.h"
#include "terrain_blend.h"
#include <map>

namespace {
std::vector<LineType> loadTypes() {
  std::vector<LineType> out;
  json j;
  try { j = json::parse(readTextFile(contentDir() / "settings" / "line_types.json")); } catch (...) { logf("lines: content/settings/line_types.json missing or invalid"); return out; }
  for (auto& t : j.value("types", json::array())) {
    LineType L;
    L.id = t.value("id", std::string()); L.name = t.value("name", L.id); L.asset = t.value("asset", "line_" + L.id);
    L.builder = t.value("builder", L.id); L.spacing = t.value("spacing", 4.0f); L.params = t.value("params", json::object()); L.raw = t;
    if (!L.id.empty()) out.push_back(std::move(L));
  }
  return out;
}
std::map<std::string, LineBuilderFn>& builders() { static std::map<std::string, LineBuilderFn> m; return m; }
vec2 cr(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) {   // centripetal Catmull-Rom (alpha 0.5) between p1 and p2
  auto kn = [](vec2 a, vec2 b, float k) { return k + std::max(1e-4f, std::sqrt(glm::length(b - a))); };
  float t0 = 0, t1 = kn(p0, p1, t0), t2 = kn(p1, p2, t1), t3 = kn(p2, p3, t2), u = t1 + (t2 - t1) * t;
  vec2 a1 = (t1 - u) / (t1 - t0) * p0 + (u - t0) / (t1 - t0) * p1, a2 = (t2 - u) / (t2 - t1) * p1 + (u - t1) / (t2 - t1) * p2, a3 = (t3 - u) / (t3 - t2) * p2 + (u - t2) / (t3 - t2) * p3;
  vec2 b1 = (t2 - u) / (t2 - t0) * a1 + (u - t0) / (t2 - t0) * a2, b2 = (t3 - u) / (t3 - t1) * a2 + (u - t1) / (t3 - t1) * a3;
  return (t2 - u) / (t2 - t1) * b1 + (u - t1) / (t2 - t1) * b2;
}
}   // namespace

const std::vector<LineType>& lineTypes() { static std::vector<LineType> t = loadTypes(); return t; }
const LineType* lineType(const std::string& id) { for (auto& t : lineTypes()) if (t.id == id) return &t; return nullptr; }
const LineType* lineTypeOf(const AssetDef* a) {
  if (!a || !a->raw.is_object() || !a->raw.contains("line")) return nullptr;
  return lineType(a->raw["line"].value("type", std::string()));
}

bool lineGet(const Instance& i, LineData& out) {
  if (!i.extra.is_object() || !i.extra.contains("line") || !i.extra["line"].is_object()) return false;
  const json& L = i.extra["line"];
  out = LineData();
  for (auto& p : L.value("pts", json::array()))
    if (p.is_array() && p.size() >= 2) out.pts.push_back({vec2(p[0].get<float>(), p[1].get<float>()), p.size() > 2 ? p[2].get<float>() : 1.0f, p.size() > 3 && p[3].is_number() ? p[3].get<float>() : NAN});
  out.seed = L.value("seed", 1u);
  out.params = L.value("params", json::object());
  for (auto& s : L.value("spans", json::array())) out.spans.push_back({s.value("s", 0.0f), s.value("w", 3.6f), s.value("kind", std::string("gap"))});
  return out.pts.size() >= 2;
}
void lineSet(Instance& i, const LineData& d) {
  json pts = json::array();
  auto r3 = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
  for (auto& p : d.pts) { json q = {r3(p.p.x), r3(p.p.y)}; if (p.d != 1.0f || !std::isnan(p.y)) q.push_back(r3(p.d)); if (!std::isnan(p.y)) q.push_back(r3(p.y)); pts.push_back(q); }
  json spans = json::array();
  for (auto& s : d.spans) spans.push_back({{"s", r3(s.s)}, {"w", r3(s.w)}, {"kind", s.kind}});
  if (!i.extra.is_object()) i.extra = json::object();
  json L = {{"pts", pts}, {"seed", d.seed}};
  if (!d.params.empty()) L["params"] = d.params;
  if (!spans.empty()) L["spans"] = spans;
  i.extra["line"] = L;
  if (!d.pts.empty()) { i.pos.x = d.pts[0].p.x; i.pos.z = d.pts[0].p.y; }
  i.yaw = 0;
}
json lineParams(const LineType& t, const LineData& d) {
  json p = t.params.is_object() ? t.params : json::object();
  if (d.params.is_object()) for (auto it = d.params.begin(); it != d.params.end(); ++it) p[it.key()] = it.value();
  return p;
}

std::vector<LineSample> lineSpline(const LineData& d, float step, bool sharp) {
  std::vector<LineSample> out;
  const size_t n = d.pts.size();
  if (n < 2) return out;
  std::vector<float> du;   // control-point parameter of each dense point
  std::vector<std::pair<vec2, float>> dense;   // fine polyline (a quarter step or finer, 20 per span at least), then resampled at `step`
  for (size_t k = 0; k + 1 < n; ++k) {
    vec2 p1 = d.pts[k].p, p2 = d.pts[k + 1].p;
    vec2 p0 = k > 0 ? d.pts[k - 1].p : p1 * 2.0f - p2, p3 = k + 2 < n ? d.pts[k + 2].p : p2 * 2.0f - p1;
    if (sharp) { p0 = p1 * 2.0f - p2; p3 = p2 * 2.0f - p1; }   // straight spans between the control points
    const int N = std::clamp((int)std::ceil(glm::length(p2 - p1) / std::max(0.05f, step * 0.25f)), 20, 4000);
    for (int s = 0; s < N; ++s) { float t = s / float(N); dense.push_back({cr(p0, p1, p2, p3, t), glm::mix(d.pts[k].d, d.pts[k + 1].d, t)}); du.push_back(k + t); }
  }
  dense.push_back({d.pts.back().p, d.pts.back().d}); du.push_back(float(n - 1));
  float acc = 0, next = 0;
  for (size_t k = 0; k < dense.size(); ++k) {
    if (k) acc += glm::length(dense[k].first - dense[k - 1].first);
    bool last = k + 1 == dense.size();
    if (acc + 1e-4f >= next || last) {
      vec2 a = dense[k > 0 ? k - 1 : 0].first, b = dense[last ? k : k + 1].first;
      vec2 t = glm::length(b - a) > 1e-6f ? glm::normalize(b - a) : vec2(1, 0);
      out.push_back({dense[k].first, acc, dense[k].second, t, du[k]});
      next = acc + step;
    }
  }
  return out;
}
uint64_t lineCutSig(const std::vector<uint8_t>& cut) { uint64_t h = 1469598103934665603ull ^ cut.size(); for (uint8_t c : cut) h = (h ^ c) * 1099511628211ull; return h; }
float lineLength(const std::vector<LineSample>& sp) { return sp.empty() ? 0.0f : sp.back().s; }

float LineBuildCtx::ground(float x, float z) const { return app.terrain.height(x, z); }

bool lineRegisterBuilder(const char* name, LineBuilderFn fn) { builders()[name] = fn; return true; }
LineBuilderFn lineBuilder(const std::string& name) { auto it = builders().find(name); return it == builders().end() ? nullptr : it->second; }

// ---------------------------------------------------------------- runtime (app.cpp buildRT / buildDrawLists, builder.cpp)
// A line asset's InstanceRT carries the builder's output: the mesh is uploaded here, the bays become the damage sections
// (secHp / secCut, so the wire cut events, saving and replication are the existing ones) and the colliders are merged
// into the instance's static / player-only bodies by App::rebuildSectionBodies. The trench builder emits nothing: GX1
// carves and dresses trench lines (terrain_blend.cpp).
bool lineBuildRT(App& app, const Instance& i, InstanceRT& r) {
  const LineType* t = lineTypeOf(r.asset);
  if (!t) return false;
  LineData d;
  LineBuilderFn fn = lineBuilder(t->builder);
  auto out = std::make_shared<LineBuildOut>();
  if (fn && lineGet(i, d)) {
    LineBuildCtx ctx{app, i, d, *t, lineParams(*t, d), lineSpline(d, 0.5f)};
    fn(ctx, *out);
  }
  r.line = out;
  float hp = t->raw.value("sectionHp", 60.0f);
  r.secHp.assign(out->sections, hp); r.secCut.assign(out->sections, 0);
  r.lineMesh.destroy();
  if (!out->mesh.i.empty()) r.lineMesh.upload(out->mesh);
  out->cutSig = lineCutSig(r.secCut);
  r.lineGround = app.groundRev;
  return true;
}
void lineColliders(const InstanceRT& r, std::vector<Part>& solid, std::vector<Part>& player) {
  if (!r.line) return;
  for (auto& c : r.line->colliders) {
    if (c.section >= 0 && c.section < (int)r.secCut.size() && r.secCut[c.section]) continue;
    (c.playerOnly ? player : solid).push_back(c.part);
  }
}
bool lineDrawItems(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op) {
  if (!r.line) return false;
  if (r.lineGround != app.groundRev) {   // the ground moved under the line (a crater, a sculpt): rebuild what conforms to it
    LineData d;
    float reach = 0;
    if (lineGet(i, d)) for (auto& p : d.pts) reach = std::max(reach, glm::length(p.p - vec2(i.pos.x, i.pos.z)));
    if (app.groundChangedNear(r.lineGround, vec2(i.pos.x, i.pos.z), reach + 4.0f)) {
      auto hp = r.secHp; auto cut = r.secCut;   // LN2: the bays a blast cut stay cut (the same layout)
      app.buildRT(i, r);
      if (cut.size() == r.secCut.size() && lineCutSig(cut) != r.line->cutSig) { r.secHp = hp; r.secCut = cut; app.rebuildSectionBodies(i, r); }
    } else r.lineGround = app.groundRev;
  }
  if (!r.line->secIdx.empty() && lineCutSig(r.secCut) != r.line->cutSig) {   // LN2: cut bays leave the mesh
    r.line->cutSig = lineCutSig(r.secCut);
    MeshData m; m.v = r.line->mesh.v;
    std::vector<uint8_t> drop(r.line->mesh.i.size(), 0);
    for (size_t s = 0; s < r.line->secIdx.size() && s < r.secCut.size(); ++s) if (r.secCut[s]) std::fill_n(drop.begin() + r.line->secIdx[s].x, r.line->secIdx[s].y, 1);
    for (size_t k = 0; k < drop.size(); ++k) if (!drop[k]) m.i.push_back(r.line->mesh.i[k]);
    r.lineMesh.destroy(); if (!m.i.empty()) r.lineMesh.upload(m);
  }
  { extern void lineDrawRigid(App&, const Instance&, InstanceRT&, const mat4&); lineDrawRigid(app, i, r, M); }   // LN2 wire models, hedge canopies
  if (r.lineMesh.count) op.push_back({&r.lineMesh, M});
  blendDrawItems(app, i, r, M, op);   // a trench line's generated revetment (GX1)
  return true;
}

// the trench type: GX1 (terrain_blend.cpp) carves and dresses trench lines (floor, walls, parapets, revetment, sandbags,
// duckboards, end walls, ramps). The builder adds what a heightfield cannot hold: the firestep, a 0.45 m packed-earth
// step 0.33 m high against the fire-side wall (render and collision), wherever the trench is at full depth and clear of
// any other trench line it meets.
static void trenchLineBuilder(const LineBuildCtx& c, LineBuildOut& out) {
  std::string fire = c.params.value("fire", std::string("left"));
  if (fire != "left" && fire != "right") return;
  const float side = fire == "left" ? 1.0f : -1.0f, half = c.app.terrain.sp.floorHalf;
  const AssetDef* a = c.app.lib.find(c.type.asset);
  Material earth; if (a) for (auto& m : a->materials) if (m.name == "mud") earth = m;
  earth.albedo = vec3(0.11f, 0.088f, 0.066f);
  std::vector<std::vector<vec2>> others;   // the other trench lines' centrelines (no step across a junction)
  for (auto& o : c.app.doc.instances) {
    if (o.id == c.inst.id) continue;
    const LineType* t = lineTypeOf(c.app.assetFor(o)); LineData od;
    if (!t || t->builder != "trench" || !lineGet(o, od)) continue;
    std::vector<vec2> pl; for (auto& q : lineSpline(od, 2.0f)) pl.push_back(q.p);
    others.push_back(pl);
  }
  auto nearOther = [&](vec2 p) {
    for (auto& pl : others) for (size_t k = 0; k + 1 < pl.size(); ++k) {
      vec2 ab = pl[k + 1] - pl[k]; float L2 = glm::dot(ab, ab), t = L2 > 1e-9f ? std::clamp(glm::dot(p - pl[k], ab) / L2, 0.0f, 1.0f) : 0.0f;
      if (glm::length(p - (pl[k] + ab * t)) < half + 2.2f) return true;
    }
    return false;
  };
  for (size_t k = 0; k + 1 < c.spline.size(); k += 2) {   // ~1 m pieces
    const LineSample &s0 = c.spline[k], &s1 = c.spline[std::min(k + 2, c.spline.size() - 1)];
    if (s0.d < 0.95f || s1.d < 0.95f || s0.s < 4.5f || lineLength(c.spline) - s1.s < 4.5f) continue;   // not on the ramps, not at the ends
    vec2 mid = (s0.p + s1.p) * 0.5f, t = s1.p - s0.p; float len = glm::length(t);
    if (len < 0.2f) continue;
    t /= len; vec2 n(-t.y, t.x);
    if (nearOther(mid)) continue;
    vec2 cxz = mid + n * side * (half - 0.24f);
    float floorY = c.ground(mid.x, mid.y);
    Part p; p.shape = Part::BOX; p.center = c.local(cxz, floorY + 0.15f); p.size = vec3(len + 0.04f, 0.48f, 0.45f);
    p.rot = glm::angleAxis(std::atan2(t.x, t.y) - 1.5707963f, vec3(0, 1, 0));   // local +x along the trench
    appendPart(out.mesh, p, earth, mat4(1));
    out.colliders.push_back({p, -1, false});
  }
}
LINE_BUILDER("trench", trenchLineBuilder);

// ---------------------------------------------------------------- migration: trench pieces -> trench lines
// On map load (App::syncRuntime's first pass over a document): every aw_trench_* piece's centreline (its trench path,
// and a junction's arms) becomes a polyline; polylines whose ends meet (within 0.3 m, as GX1 joined the pieces) are
// chained through every two-way joint into one line, so a run of pieces becomes one trench line and a junction becomes
// the meeting point of its lines. The pieces are removed; objectives linking a piece link its line instead.
int lineMigrateTrenches(App& app) {
  const AssetDef* la = app.lib.find("line_trench");
  if (!la) return 0;
  struct Poly { std::vector<vec3> p; uint64_t src; int fire; };   // p: world x, d, z
  std::vector<Poly> polys;
  std::vector<uint64_t> gone;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a || !a->raw.is_object() || !a->raw.contains("trench") || isLineAsset(a)) continue;
    const json& tj = a->raw["trench"];
    auto W = [&](const json& q) { vec3 l = yawQuat(i.yaw) * vec3(q[0].get<float>(), 0, q[1].get<float>()); return vec3(i.pos.x + l.x, q.size() > 2 ? q[2].get<float>() : 1.0f, i.pos.z + l.z); };
    Poly P{{}, i.id, 0};
    for (auto& q : tj.value("path", json::array())) P.p.push_back(W(q));
    if (P.p.size() >= 2) {   // the fire side: the piece's local +z, relative to its first leg (as GX1 reads it)
      vec2 d = glm::normalize(vec2(P.p[1].x - P.p[0].x, P.p[1].z - P.p[0].z));
      vec3 lz = yawQuat(i.yaw) * vec3(0, 0, 1);
      float f = glm::dot(vec2(-d.y, d.x), vec2(lz.x, lz.z));
      P.fire = f > 0.3f ? 1 : f < -0.3f ? -1 : 0;
      polys.push_back(P);
    }
    for (auto& br : tj.value("branches", json::array())) {   // junction arms (3 m or longer); niches stay in the carve
      if (br.size() < 2) continue;
      Poly B{{W(br[0]), W(br[br.size() - 1])}, i.id, 0};
      if (glm::length(vec2(B.p[1].x - B.p[0].x, B.p[1].z - B.p[0].z)) >= 3.0f) polys.push_back(B);
    }
    gone.push_back(i.id);
  }
  if (polys.empty()) return 0;
  // nodes: polyline ends merged within 0.3 m
  std::vector<vec2> nodes; std::vector<int> deg;
  auto node = [&](vec3 p) {
    for (size_t k = 0; k < nodes.size(); ++k) if (glm::length(nodes[k] - vec2(p.x, p.z)) < 0.3f) return (int)k;
    nodes.push_back(vec2(p.x, p.z)); deg.push_back(0); return (int)nodes.size() - 1;
  };
  std::vector<std::pair<int, int>> ends(polys.size());
  for (size_t k = 0; k < polys.size(); ++k) { ends[k] = {node(polys[k].p.front()), node(polys[k].p.back())}; ++deg[ends[k].first]; ++deg[ends[k].second]; }
  std::vector<bool> used(polys.size(), false);
  std::map<uint64_t, uint64_t> remap;
  int made = 0;
  for (size_t k0 = 0; k0 < polys.size(); ++k0) {
    if (used[k0]) continue;
    // walk back to a chain end (a node of degree != 2), then forward along the chain
    size_t k = k0; bool rev = false;
    for (size_t guard = 0; guard < polys.size(); ++guard) {
      int n = rev ? ends[k].second : ends[k].first;
      if (deg[n] != 2) break;
      size_t nk = SIZE_MAX;
      for (size_t m = 0; m < polys.size(); ++m) if (m != k && (ends[m].first == n || ends[m].second == n)) nk = m;
      if (nk == SIZE_MAX || nk == k0) break;
      rev = ends[nk].first == n;   // continue backwards through nk
      k = nk;
    }
    std::vector<vec3> pts; std::vector<uint64_t> srcs; int fire = 0;
    for (size_t guard = 0; guard < polys.size(); ++guard) {
      used[k] = true;
      std::vector<vec3> seg = polys[k].p;
      if (rev) std::reverse(seg.begin(), seg.end());
      if (!fire && polys[k].fire) fire = rev ? -polys[k].fire : polys[k].fire;
      pts.insert(pts.end(), pts.empty() ? seg.begin() : seg.begin() + 1, seg.end());
      srcs.push_back(polys[k].src);
      int n = rev ? ends[k].first : ends[k].second;
      if (deg[n] != 2) break;
      size_t nk = SIZE_MAX;
      for (size_t m = 0; m < polys.size(); ++m) if (m != k && !used[m] && (ends[m].first == n || ends[m].second == n)) nk = m;
      if (nk == SIZE_MAX) break;
      rev = ends[nk].second == n;
      k = nk;
    }
    LineData d;
    for (auto& p : pts) if (d.pts.empty() || glm::length(d.pts.back().p - vec2(p.x, p.z)) > 0.05f) d.pts.push_back({vec2(p.x, p.z), p.y});
    if (d.pts.size() < 2) continue;
    d.seed = (uint32_t)(srcs.front() * 2654435761u);
    d.params = {{"fire", fire > 0 ? "left" : fire < 0 ? "right" : "none"}, {"ramps", "none"}, {"sharp", true}};
    Instance L; L.id = app.doc.newId(); L.assetId = la->id; L.assetVersion = la->version;
    lineSet(L, d);
    L.pos.y = app.terrain.groundBase(L.pos.x, L.pos.z);
    app.doc.instances.push_back(L);
    for (uint64_t s : srcs) remap[s] = L.id;
    ++made;
  }
  auto& in = app.doc.instances;
  in.erase(std::remove_if(in.begin(), in.end(), [&](const Instance& i) { return std::find(gone.begin(), gone.end(), i.id) != gone.end(); }), in.end());
  if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) {   // objectives keep their links (the line now)
    for (auto& o : app.doc.extra["scenario"]["objectives"]) {
      if (!o.contains("linkedStructures")) continue;
      std::vector<uint64_t> ids;
      for (auto& v : o["linkedStructures"]) { uint64_t id = v.get<uint64_t>(); auto it = remap.find(id); id = it == remap.end() ? id : it->second; if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id); }
      o["linkedStructures"] = ids;
    }
  }
  logf("lines: %zu trench pieces migrated to %d trench lines", gone.size(), made);
  return made;
}
