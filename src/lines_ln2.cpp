// LN2 line types (docs/lines.md#ln2): wire, fences, walls, sandbags and hedgerows drawn as lines.
// A line is laid out in bays, each one copy of an existing piece (content/assets, the type's "pieces"), stretched to
// fit the line and the gates / gaps cut out of it:
//  * the piece's part geometry is mapped vertex by vertex along the spline (posts stay upright, rails, courses, banks
//    and strands follow the curve and the ground) and batched into the line's one mesh;
//  * a piece with a supplied model (the wire obstacles) or a canopy (hedgerows) is placed rigidly per bay on the chord
//    between the bay's ends: the model's own draw and the L1 species leaves, as the pieces drew them;
//  * colliders, nav areas and cover follow the bay; on damageable types (wire, sandbags) a bay is a damage section, so
//    a blast cuts it out (InstanceRT::secCut) and opens a gap.
// Gates are ordinary instances of the gate asset (field_gate, drs_gate_piers) owned by the line (extra.lineGate) and
// kept on its span by lineSyncGates, so doors, AI, nav and replication treat them as they treat every door.
#include "lines.h"
#include "settings.h"
#include <map>
#include <set>

namespace {
// a piece in its line frame: u along the line (centred), v across, y up
struct Src {
  const AssetDef* a = nullptr;
  int axis = 0;                    // the piece's long axis: 0 x, 2 z
  float len = 1;                   // its length along it (the bay length)
  MeshData mesh;                   // visible parts, asset space (end caps apart)
  MeshData cap[2];                 // snap end caps ("cap" parts): [0] the start end (-u), [1] the end; drawn at run ends only
  std::vector<int> partEnd;        // per part: -1, or the end (0 / 1) it caps
};
const Src* source(App& app, const std::string& id) {
  static std::map<std::string, Src> cache;
  auto it = cache.find(id);
  if (it != cache.end()) return it->second.a ? &it->second : nullptr;
  Src s; s.a = app.lib.find(id);
  if (s.a) {
    s.axis = s.a->footprint.x >= s.a->footprint.y ? 0 : 2;
    s.len = std::max(0.5f, s.axis == 0 ? s.a->footprint.x : s.a->footprint.y);
    const json& rp = s.a->raw.value("parts", json::array());
    const json& so = s.a->raw.contains("snap") ? s.a->raw["snap"] : json::array();
    for (size_t k = 0; k < s.a->parts.size(); ++k) {
      int e = -1;
      if (k < rp.size() && rp[k].contains("cap")) {   // which end the capped socket is on
        int sk = rp[k]["cap"].get<int>();
        float u = sk < (int)so.size() ? (s.axis == 0 ? so[sk]["p"][0].get<float>() : so[sk]["p"][2].get<float>()) : (sk ? 1.0f : -1.0f);
        e = u < 0 ? 0 : 1;
      }
      s.partEnd.push_back(e);
      const Part& p = s.a->parts[k];
      if (p.visible && p.mat >= 0 && p.mat < (int)s.a->materials.size()) appendPart(e < 0 ? s.mesh : s.cap[e], p, s.a->materials[p.mat], mat4(1));
    }
  } else logf("lines: line piece '%s' missing", id.c_str());
  return cache.emplace(id, std::move(s)).first->second.a ? &cache[id] : nullptr;
}
inline vec2 toUV(const Src& s, vec3 p) { return s.axis == 0 ? vec2(p.x, p.z) : vec2(p.z, -p.x); }   // a rotation (det +1)

struct Curve {
  const std::vector<LineSample>& sp;
  // position and tangent at arc length s (clamped)
  void at(float s, vec2& p, vec2& t) const {
    if (sp.size() < 2) { p = sp.empty() ? vec2(0) : sp[0].p; t = vec2(1, 0); return; }
    s = std::clamp(s, 0.0f, sp.back().s);
    size_t k = std::upper_bound(sp.begin(), sp.end(), s, [](float v, const LineSample& a) { return v < a.s; }) - sp.begin();
    k = std::clamp<size_t>(k, 1, sp.size() - 1);
    const LineSample& a = sp[k - 1]; const LineSample& b = sp[k];
    float f = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0.0f;
    p = glm::mix(a.p, b.p, f); t = glm::mix(a.t, b.t, f);
    float L = glm::length(t); t = L > 1e-6f ? t / L : vec2(1, 0);
  }
};
inline vec2 nrm2(vec2 t) { return vec2(-t.y, t.x); }   // across the line: the piece's +v (asset +z for x-axis pieces)

struct Bay { const Src* src; float s0, s1; int sec; bool end[2] = {false, false}; };   // end: this bay closes its run there (caps)

// bays over [a, b]: pieces chosen by the line seed (weights), then stretched together to fill the run exactly
void layRun(const LineBuildCtx& c, const std::vector<std::pair<const Src*, float>>& pieces, float a, float b, uint32_t& rng, int& sec, bool damage, std::vector<Bay>& out) {
  float R = b - a;
  if (R < 0.4f || pieces.empty()) return;
  float tot = 0; for (auto& p : pieces) tot += p.second;
  auto pick = [&]() {
    rng = rng * 1664525u + 1013904223u;
    float r = (rng >> 8) / 16777216.0f * tot;
    for (auto& p : pieces) { r -= p.second; if (r <= 0) return p.first; }
    return pieces.back().first;
  };
  std::vector<const Src*> seq; float sum = 0;
  while (sum < R - 1e-3f) {
    const Src* s = pick();
    if (sum > 0 && sum + s->len * 0.5f > R) break;   // the last piece would overshoot by more than half: stretch the rest
    seq.push_back(s); sum += s->len;
  }
  if (seq.empty()) { seq.push_back(pieces.front().first); sum = seq[0]->len; }
  float k = R / sum, s = a;
  size_t first = out.size();
  for (auto* p : seq) { float e = s + p->len * k; out.push_back({p, s, e, damage ? sec++ : -1}); s = e; }
  out[first].end[0] = true; out.back().end[1] = true;
}

void bayBuilder(const LineBuildCtx& c, LineBuildOut& out) {
  App& app = c.app;
  // pieces: [["id", weight], ...] or a single "piece"
  std::vector<std::pair<const Src*, float>> pieces;
  const json& raw = c.type.raw;
  if (raw.contains("pieces")) { for (auto& p : raw["pieces"]) if (const Src* s = source(app, p[0].get<std::string>())) pieces.push_back({s, p.size() > 1 ? p[1].get<float>() : 1.0f}); }
  else if (const Src* s = source(app, raw.value("piece", std::string()))) pieces.push_back({s, 1.0f});
  if (pieces.empty() || c.spline.size() < 2) return;
  const bool damage = raw.contains("sectionHp");
  Curve cv{c.spline};
  float L = lineLength(c.spline);
  // runs between the gates / gaps (span s = its centre)
  std::vector<std::pair<float, float>> holes;
  for (auto& sp : c.line.spans) holes.push_back({sp.s - sp.w * 0.5f, sp.s + sp.w * 0.5f});
  std::sort(holes.begin(), holes.end());
  std::vector<Bay> bays; uint32_t rng = c.line.seed * 2654435761u + 7u; int sec = 0;
  float a = 0;
  for (auto& h : holes) { layRun(c, pieces, a, std::min(h.first, L), rng, sec, damage, bays); a = std::max(a, h.second); }
  layRun(c, pieces, a, L, rng, sec, damage, bays);
  out.sections = sec;

  auto ground = [&](vec2 p) { return c.ground(p.x, p.y); };
  // a piece point (asset space) in bay b, instance space; the tangent frame there
  auto map = [&](const Bay& b, vec3 lp, vec2* T = nullptr) {
    vec2 uv = toUV(*b.src, lp);
    float s = b.s0 + (uv.x / b.src->len + 0.5f) * (b.s1 - b.s0);
    vec2 P, t; cv.at(s, P, t);
    if (T) *T = t;
    vec2 w = P + nrm2(t) * uv.y;
    return c.local(w, lp.y + ground(P));
  };
  auto mapDir = [&](const Src& s, vec3 d, vec2 t) {
    vec2 uv = toUV(s, d); vec2 w = t * uv.x + nrm2(t) * uv.y;
    return vec3(w.x, d.y, w.y);
  };
  for (const Bay& b : bays) {
    const Src& s = *b.src;
    // mesh: every vertex along the curve
    uint32_t base = (uint32_t)out.mesh.v.size(), first = (uint32_t)out.mesh.i.size();
    auto emit = [&](const MeshData& src) {
      uint32_t b0 = (uint32_t)out.mesh.v.size();
      for (Vertex v : src.v) {
        vec2 t; v.pos = map(b, v.pos, &t);
        v.nrm = glm::normalize(mapDir(s, v.nrm, t));
        out.mesh.v.push_back(v);
      }
      for (uint32_t i : src.i) out.mesh.i.push_back(b0 + i);
    };
    (void)base;
    emit(s.mesh);
    for (int e = 0; e < 2; ++e) if (b.end[e]) emit(s.cap[e]);
    if (b.sec >= 0) { if ((int)out.secIdx.size() <= b.sec) out.secIdx.resize(b.sec + 1); out.secIdx[b.sec] = {first, (uint32_t)out.mesh.i.size() - first}; }
    // colliders: each collide part's corners mapped, boxed in the curve's frame at the part centre
    for (size_t pk = 0; pk < s.a->parts.size(); ++pk) {
      const Part& p = s.a->parts[pk];
      if (!p.collide || (s.partEnd[pk] >= 0 && !b.end[s.partEnd[pk]])) continue;
      vec2 t; vec3 ctr = map(b, p.center, &t);
      vec3 X(t.x, 0, t.y), Z(nrm2(t).x, 0, nrm2(t).y);
      vec3 lo(1e9f), hi(-1e9f);
      for (int q = 0; q < 8; ++q) {
        vec3 cl = p.center + p.rot * (vec3((q & 1) ? 0.5f : -0.5f, (q & 2) ? 0.5f : -0.5f, (q & 4) ? 0.5f : -0.5f) * p.size);
        vec3 d = map(b, cl) - ctr, f(glm::dot(d, X), d.y, glm::dot(d, Z));
        lo = glm::min(lo, f); hi = glm::max(hi, f);
      }
      LineCollider lc; lc.part = Part(); lc.part.shape = Part::BOX; lc.part.visible = false; lc.part.collide = true; lc.part.playerOnly = p.playerOnly;
      vec3 m = (lo + hi) * 0.5f;
      lc.part.center = ctr + X * m.x + vec3(0, m.y, 0) + Z * m.z; lc.part.size = glm::max(hi - lo, vec3(0.02f));
      lc.part.rot = glm::quat_cast(glm::mat3(X, vec3(0, 1, 0), Z));
      lc.section = b.sec; lc.playerOnly = p.playerOnly;
      out.colliders.push_back(lc);
    }
    // nav areas and cover
    for (auto& ap : s.a->navMeta.areas) {
      NavAreaPoly q; q.area = ap.area;
      for (vec2 o : ap.outline) { vec3 w = map(b, vec3(o.x, 0, o.y)); q.outline.push_back(vec2(w.x, w.z)); }
      out.areas.push_back(q); out.areaSec.push_back(b.sec);
    }
    if (s.a->navMeta.areas.empty() && s.a->navMeta.area != NavArea::Default) {   // the whole piece is the area (minefield)
      NavAreaPoly q; q.area = s.a->navMeta.area;
      for (vec2 o : {vec2(s.a->bmin.x, s.a->bmin.z), vec2(s.a->bmax.x, s.a->bmin.z), vec2(s.a->bmax.x, s.a->bmax.z), vec2(s.a->bmin.x, s.a->bmax.z)}) { vec3 w = map(b, vec3(o.x, 0, o.y)); q.outline.push_back(vec2(w.x, w.z)); }
      out.areas.push_back(q); out.areaSec.push_back(b.sec);
    }
    for (auto cp : s.a->navMeta.cover) {
      vec2 t; cp.p = map(b, cp.p, &t);
      vec3 d = mapDir(s, vec3(std::sin(radians(cp.yaw)), 0, std::cos(radians(cp.yaw))), t);
      cp.yaw = degrees(std::atan2(d.x, d.z));
      out.cover.push_back(cp);
    }
    // a supplied model or a canopy: the whole piece placed rigidly on the bay's chord
    if (s.a->model || s.a->tree.on) {
      vec2 A, B, t; cv.at(b.s0, A, t); cv.at(b.s1, B, t);
      vec3 a3 = c.local(A, ground(A)), b3 = c.local(B, ground(B));
      vec3 d = b3 - a3; float chord = glm::length(d);
      vec3 X = chord > 1e-4f ? d / chord : vec3(1, 0, 0);
      vec3 Z = glm::normalize(glm::cross(X, vec3(0, 1, 0))), Y = glm::cross(Z, X);   // Z: the +v side (left of the line)
      mat4 F(1); F[0] = vec4(X, 0); F[1] = vec4(Y, 0); F[2] = vec4(Z, 0); F[3] = vec4((a3 + b3) * 0.5f, 1);
      mat4 toU = s.axis == 0 ? mat4(1) : glm::rotate(mat4(1), radians(90.0f), vec3(0, 1, 0));   // asset z onto the line (x onto -v, as toUV)
      mat4 S = glm::scale(mat4(1), s.axis == 0 ? vec3(chord / s.len, 1, 1) : vec3(1, 1, chord / s.len));
      out.rigid.push_back({s.a, F * toU * S, b.sec});
    }
  }
  // gates: their posts / piers come with the gate instance (lineSyncGates); listed for the door system and the UI
  for (auto& sp : c.line.spans) if (sp.kind != "gap") {
    LineGate g; g.s = sp.s; g.w = sp.w; g.kind = sp.kind;
    vec2 P, t; cv.at(sp.s - sp.w * 0.5f, P, t); g.a = vec2(c.local(P, 0).x, c.local(P, 0).z);
    cv.at(sp.s + sp.w * 0.5f, P, t); g.b = vec2(c.local(P, 0).x, c.local(P, 0).z);
    out.gates.push_back(g);
  }
}
LINE_BUILDER("bays", bayBuilder);
}   // namespace

// ---------------------------------------------------------------- drawing the rigid bays (lineDrawItems)
void lineDrawRigid(App& app, const Instance& i, InstanceRT& r, const mat4& M) {
  if (!r.line) return;
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  for (auto& g : r.line->rigid) {
    if (g.section >= 0 && g.section < (int)r.secCut.size() && r.secCut[g.section]) continue;
    mat4 W = M * g.m;
    float cd = glm::length(vec3(W[3]) - eye);
    if (g.src->model) {
      if (cd > g.src->modelCull) continue;
      const GpuModel* gm = (g.src->modelLod && cd > g.src->modelLodDist) ? g.src->modelLod.get() : g.src->model.get();
      for (size_t k = 0; k < gm->parts.size(); ++k) app.R.worldModels.push_back({gm, (int)k, W, vec4(1), g.src->modelThinPx});
    }
    if (g.src->tree.on && cd < 900 * settings().drawDist()) app.R.foliage.push_back({&g.src->tree, W, &g.lod});
  }
}

// ---------------------------------------------------------------- gates: owned instances on the line's spans
// Every gate span ("field_gate", "gate_piers") has one instance of its gate asset, extra.lineGate = {line, k}, at the
// span's centre on the ground, facing along the line. Missing ones are created, moved ones follow, orphans go.
int lineSyncGates(App& app) {
  struct Want { uint64_t line; int k; std::string asset; vec3 pos; float yaw; };
  std::vector<Want> want;
  std::set<uint64_t> lines;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.lib.find(i.assetId);
    if (!isLineAsset(a)) continue;
    lines.insert(i.id);
    LineData d; if (!lineGet(i, d) || d.spans.empty()) continue;
    auto sp = lineSpline(d, 0.5f);   // the builder's curve
    Curve cv{sp};
    for (size_t k = 0; k < d.spans.size(); ++k) {
      const std::string& kd = d.spans[k].kind;
      std::string ga = kd == "field_gate" ? "field_gate" : kd == "gate_piers" ? "drs_gate_piers" : "";
      if (ga.empty() || !app.lib.find(ga)) continue;
      vec2 P, t; cv.at(d.spans[k].s, P, t);
      want.push_back({i.id, (int)k, ga, vec3(P.x, app.terrain.height(P.x, P.y), P.y), degrees(std::atan2(-t.y, t.x))});   // asset +x along the line
    }
  }
  int changed = 0;
  std::set<std::pair<uint64_t, int>> have;
  auto& in = app.doc.instances;
  for (auto it = in.begin(); it != in.end();) {
    if (!it->extra.is_object() || !it->extra.contains("lineGate")) { ++it; continue; }
    uint64_t L = it->extra["lineGate"].value("line", (uint64_t)0); int k = it->extra["lineGate"].value("k", -1);
    auto w = std::find_if(want.begin(), want.end(), [&](const Want& x) { return x.line == L && x.k == k && x.asset == it->assetId; });
    if (w == want.end() || have.count({L, k})) { it = in.erase(it); ++changed; continue; }
    have.insert({L, k});
    if (glm::length(it->pos - w->pos) > 0.01f || std::fabs(it->yaw - w->yaw) > 0.05f) { it->pos = w->pos; it->yaw = w->yaw; it->rev = ++app.revCounter; ++changed; }
    ++it;
  }
  for (auto& w : want) {
    if (have.count({w.line, w.k})) continue;
    const AssetDef* ga = app.lib.find(w.asset);
    Instance g; g.id = app.doc.newId(); g.assetId = ga->id; g.assetVersion = ga->version; g.pos = w.pos; g.yaw = w.yaw;
    g.extra = json{{"lineGate", {{"line", w.line}, {"k", w.k}}}};
    in.push_back(g); ++changed;
  }
  return changed;
}

// a piece some line type migrates (content/settings/line_types.json "migrate"): hidden from the Assets palette
bool lineReplacedPiece(const std::string& id) {
  static std::set<std::string> ids = [] { std::set<std::string> s; for (auto& t : lineTypes()) { json m = t.raw.value("migrate", json::object()); for (auto it = m.begin(); it != m.end(); ++it) s.insert(it.key()); } return s; }();
  return ids.count(id) > 0;
}

// insert a gate / gap on a line at the point nearest `w` (lasso Line mode click); returns false when no line is close
bool lineInsertSpan(App& app, uint64_t lineId, vec2 w, const std::string& kind, float width) {
  for (auto& i : app.doc.instances) {
    if (i.id != lineId) continue;
    LineData d; if (!lineGet(i, d)) return false;
    auto sp = lineSpline(d, 0.25f);
    float best = 1e9f, s = 0;
    for (auto& q : sp) { float e = glm::length(q.p - w); if (e < best) { best = e; s = q.s; } }
    if (best > 3.0f) return false;
    float L = lineLength(sp), hw = width * 0.5f;
    s = std::clamp(s, hw, std::max(hw, L - hw));
    for (auto& o : d.spans) if (std::fabs(o.s - s) < (o.w + width) * 0.5f) return false;   // overlaps a span
    d.spans.push_back({s, width, kind});
    std::sort(d.spans.begin(), d.spans.end(), [](const LineSpan& a, const LineSpan& b) { return a.s < b.s; });
    lineSet(i, d); i.rev = ++app.revCounter;
    lineSyncGates(app);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- migration: chains of pieces -> lines
// content/settings/line_types.json "migrate": {"<piece id>": {"path": [[x, z], ...] (asset space, default the long
// axis end to end), "gap": [s0, s1] (a gateway in the piece)}}. Pieces of a type whose ends meet (within 0.6 m) chain
// into one line, through every two-way joint; gate pieces (field_gate, drs_gate_piers) chain into the types that list
// them under "gates" and become gate spans. Free-standing pieces become short lines unless the type says "minChain".
int lineMigratePieces(App& app) {
  struct Piece { uint64_t id; int type; std::vector<vec2> p; std::vector<std::pair<float, float>> gaps; std::string gate; };
  std::vector<Piece> ps;
  std::map<std::string, std::pair<int, json>> byId;   // piece id -> (type index, migrate entry)
  std::map<std::string, std::vector<int>> gateTypes;   // gate piece id -> types that take it
  const auto& T = lineTypes();
  for (size_t k = 0; k < T.size(); ++k) {
    const json& m = T[k].raw.value("migrate", json::object());
    for (auto it = m.begin(); it != m.end(); ++it) byId[it.key()] = {(int)k, it.value()};
    for (auto& g : T[k].raw.value("gates", json::array())) gateTypes[g.get<std::string>()].push_back((int)k);
  }
  if (byId.empty()) return 0;
  for (auto& i : app.doc.instances) {
    if (i.extra.is_object() && i.extra.contains("lineGate")) continue;
    bool gate = gateTypes.count(i.assetId) > 0;
    auto b = byId.find(i.assetId);
    if (b == byId.end() && !gate) continue;
    const AssetDef* a = app.lib.find(i.assetId);
    if (!a) continue;
    const Src* s = source(app, i.assetId);
    if (!s) continue;
    json m = b != byId.end() ? b->second.second : json::object();
    Piece P{i.id, b != byId.end() ? b->second.first : -1, {}, {}, gate ? i.assetId : ""};
    quat q = yawQuat(i.yaw);
    auto W = [&](vec3 l) { vec3 w = i.pos + q * l; return vec2(w.x, w.z); };
    if (m.contains("path")) for (auto& p : m["path"]) P.p.push_back(W(vec3(p[0].get<float>(), 0, p[1].get<float>())));
    else if (s->axis == 0) { P.p = {W(vec3(-s->len * 0.5f, 0, 0)), W(vec3(s->len * 0.5f, 0, 0))}; }
    else { P.p = {W(vec3(0, 0, -s->len * 0.5f)), W(vec3(0, 0, s->len * 0.5f))}; }
    if (m.contains("gap")) P.gaps.push_back({m["gap"][0].get<float>(), m["gap"][1].get<float>()});
    ps.push_back(P);
  }
  if (ps.empty()) return 0;
  // nodes: piece ends merged within 0.6 m
  std::vector<vec2> nodes;
  auto node = [&](vec2 p) { for (size_t k = 0; k < nodes.size(); ++k) if (glm::length(nodes[k] - p) < 0.6f) return (int)k; nodes.push_back(p); return (int)nodes.size() - 1; };
  std::vector<std::pair<int, int>> ends(ps.size());
  for (size_t k = 0; k < ps.size(); ++k) ends[k] = {node(ps[k].p.front()), node(ps[k].p.back())};
  // a gate piece takes the type of a line piece it touches
  for (size_t k = 0; k < ps.size(); ++k) if (ps[k].type < 0) {
    for (size_t m = 0; m < ps.size() && ps[k].type < 0; ++m) {
      if (ps[m].type < 0) continue;
      auto& ok = gateTypes[ps[k].gate];
      if (std::find(ok.begin(), ok.end(), ps[m].type) == ok.end()) continue;
      if (ends[m].first == ends[k].first || ends[m].first == ends[k].second || ends[m].second == ends[k].first || ends[m].second == ends[k].second) ps[k].type = ps[m].type;
    }
  }
  std::vector<int> deg(nodes.size(), 0);   // per type: joints only between pieces of the same line type
  std::map<std::pair<int, int>, int> degT;
  for (size_t k = 0; k < ps.size(); ++k) if (ps[k].type >= 0) { ++degT[{ends[k].first, ps[k].type}]; ++degT[{ends[k].second, ps[k].type}]; }
  std::vector<bool> used(ps.size(), false);
  std::set<uint64_t> gone;
  int made = 0;
  for (size_t k0 = 0; k0 < ps.size(); ++k0) {
    if (used[k0] || ps[k0].type < 0) continue;
    int ty = ps[k0].type;
    auto two = [&](int n) { return degT[{n, ty}] == 2; };
    auto other = [&](size_t k, int n) -> size_t { for (size_t m = 0; m < ps.size(); ++m) if (m != k && !used[m] && ps[m].type == ty && (ends[m].first == n || ends[m].second == n)) return m; return SIZE_MAX; };
    // back to a chain end
    size_t k = k0; bool rev = false;
    for (size_t g = 0; g < ps.size(); ++g) {
      int n = rev ? ends[k].second : ends[k].first;
      if (!two(n)) break;
      size_t nk = SIZE_MAX; for (size_t m = 0; m < ps.size(); ++m) if (m != k && ps[m].type == ty && (ends[m].first == n || ends[m].second == n)) nk = m;
      if (nk == SIZE_MAX || nk == k0) break;
      rev = ends[nk].first == n; k = nk;
    }
    LineData d; float s = 0;
    std::vector<uint64_t> srcs;
    for (size_t g = 0; g < ps.size(); ++g) {
      used[k] = true; srcs.push_back(ps[k].id);
      std::vector<vec2> seg = ps[k].p; if (rev) std::reverse(seg.begin(), seg.end());
      float segL = 0; for (size_t q = 1; q < seg.size(); ++q) segL += glm::length(seg[q] - seg[q - 1]);
      if (!ps[k].gate.empty()) d.spans.push_back({s + segL * 0.5f, segL, ps[k].gate == "field_gate" ? "field_gate" : "gate_piers"});
      for (auto gp : ps[k].gaps) { float a = rev ? segL - gp.second : gp.first, b = rev ? segL - gp.first : gp.second; d.spans.push_back({s + (a + b) * 0.5f, b - a, "gap"}); }
      for (size_t q = d.pts.empty() ? 0 : 1; q < seg.size(); ++q) d.pts.push_back({seg[q], 1.0f});
      s += segL;
      int n = rev ? ends[k].first : ends[k].second;
      if (!two(n)) break;
      size_t nk = other(k, n);
      if (nk == SIZE_MAX) break;
      rev = ends[nk].second == n; k = nk;
    }
    const LineType& lt = T[ty];
    if (d.pts.size() < 2 || (int)srcs.size() < lt.raw.value("minChain", 1)) continue;   // e.g. knife rests: single ones stay obstacles
    const AssetDef* la = app.lib.find(lt.asset);
    if (!la) continue;
    d.seed = (uint32_t)(srcs.front() * 2654435761u);
    Instance L; L.id = app.doc.newId(); L.assetId = la->id; L.assetVersion = la->version;
    lineSet(L, d);
    L.pos.y = app.terrain.groundBase(L.pos.x, L.pos.z);
    app.doc.instances.push_back(L);
    gone.insert(srcs.begin(), srcs.end());
    ++made;
  }
  auto& in = app.doc.instances;
  in.erase(std::remove_if(in.begin(), in.end(), [&](const Instance& i) { return gone.count(i.id) > 0; }), in.end());
  if (made) { logf("lines: %zu fence / wire / wall / hedge pieces migrated to %d lines", gone.size(), made); lineSyncGates(app); }
  return made;
}
