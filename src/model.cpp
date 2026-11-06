// glTF binary import (offline bake) and the runtime textured model. No third-party glTF library: the GLB
// container is a 12-byte header, a JSON chunk and a BIN chunk, and accessors are read directly.
#include "model.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>

// ------------------------------------------------------------------ import spec
ImportSpec importSpecFromJson(const json& j) {
  ImportSpec s;
  s.src = j.value("src", ""); s.dir = j.value("dir", "");
  s.rot = jvec3(j.value("rot", json()), vec3(0));
  s.realLength = j.value("realLength", 1.0f);
  s.lengthNode = j.value("lengthNode", "");
  s.offset = jvec3(j.value("offset", json()), vec3(0));
  json np = j.value("nodeParts", json::object()), hd = j.value("hide", json::array());   // keep the temporaries alive
  for (auto& [k, v] : np.items()) s.nodeParts[k] = v.get<std::string>();
  for (auto& h : hd) s.hide.push_back(h.get<std::string>());
  s.maxTex = j.value("maxTex", 1024);
  s.matOverride = j.value("matOverride", json::object());
  s.tint = jvec3(j.value("tint", json()), vec3(1));
  for (auto& e : j.value("meshEdits", json::array())) {
    ImportSpec::MeshEdit m;
    json box = e.value("islandsIn", json::array());
    if (box.size() == 2) { m.lo = jvec3(box[0], vec3(0)); m.hi = jvec3(box[1], vec3(0)); }
    m.axis = jvec3(e.value("axis", json()), vec3(1, 0, 0)); m.pivot = jvec3(e.value("pivot", json()), vec3(0));
    m.move = jvec3(e.value("translate", json()), vec3(0)); m.deg = e.value("deg", 0.0f); m.hide = e.value("hide", false);
    m.note = e.value("note", "");
    if (e.contains("radial")) {
      const json& r = e["radial"]; m.radial = true;
      m.centre = vec2(r["centre"][0].get<float>(), r["centre"][1].get<float>()); m.rMax = r.value("rMax", 0.0f); m.scale = r.value("scale", 1.0f);
    }
    s.meshEdits.push_back(m);
  }
  return s;
}

// ------------------------------------------------------------------ GLB reading
namespace {
struct Glb { json j; std::vector<uint8_t> bin; };

bool readGlb(const fs::path& p, Glb& g, std::string& err) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { err = "cannot open " + p.string(); return false; }
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &b[o], 4); return v; };
  if (b.size() < 20 || u32(0) != 0x46546C67u) { err = "not a GLB file"; return false; }
  if (u32(4) != 2) { err = "only glTF 2.0 GLB is supported"; return false; }
  size_t o = 12;
  while (o + 8 <= b.size()) {
    uint32_t len = u32(o), type = u32(o + 4);
    if (o + 8 + len > b.size()) { err = "truncated chunk"; return false; }
    if (type == 0x4E4F534Au) g.j = json::parse(b.begin() + o + 8, b.begin() + o + 8 + len);
    else if (type == 0x004E4942u) g.bin.assign(b.begin() + o + 8, b.begin() + o + 8 + len);
    o += 8 + len;
  }
  if (g.j.is_null()) { err = "no JSON chunk"; return false; }
  return true;
}

// Read an accessor as floats (normalised integers mapped to [0,1]); n components per element.
std::vector<float> readAccessor(const Glb& g, int idx, int& comps) {
  const json& a = g.j["accessors"][idx];
  static const std::map<std::string, int> nc = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
  comps = nc.at(a["type"].get<std::string>());
  size_t count = a["count"].get<size_t>();
  int ct = a["componentType"].get<int>();
  bool norm = a.value("normalized", false);
  std::vector<float> out(count * comps, 0.0f);
  if (!a.contains("bufferView")) return out;
  const json& bv = g.j["bufferViews"][a["bufferView"].get<int>()];
  size_t base = bv.value("byteOffset", (size_t)0) + a.value("byteOffset", (size_t)0);
  int csz = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
  size_t stride = bv.value("byteStride", (size_t)0); if (!stride) stride = (size_t)csz * comps;
  for (size_t e = 0; e < count; ++e)
    for (int c = 0; c < comps; ++c) {
      const uint8_t* p = &g.bin[base + e * stride + (size_t)c * csz];
      float v = 0;
      switch (ct) {
        case 5126: std::memcpy(&v, p, 4); break;
        case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = (float)x; break; }
        case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = norm ? x / 65535.0f : (float)x; break; }
        case 5122: { int16_t x; std::memcpy(&x, p, 2); v = norm ? std::max(x / 32767.0f, -1.0f) : (float)x; break; }
        case 5121: { uint8_t x = *p; v = norm ? x / 255.0f : (float)x; break; }
        case 5120: { int8_t x = (int8_t)*p; v = norm ? std::max(x / 127.0f, -1.0f) : (float)x; break; }
      }
      out[e * comps + c] = v;
    }
  return out;
}
std::vector<uint32_t> readIndices(const Glb& g, int idx) {
  int c; auto f = readAccessor(g, idx, c);
  std::vector<uint32_t> r(f.size());
  for (size_t k = 0; k < f.size(); ++k) r[k] = (uint32_t)f[k];
  return r;
}

struct TRS { vec3 t{0}; quat r{1, 0, 0, 0}; vec3 s{1}; bool hasMatrix = false; mat4 m{1}; };
TRS nodeTRS(const json& n) {
  TRS x;
  if (n.contains("matrix")) { x.hasMatrix = true; auto& m = n["matrix"]; for (int k = 0; k < 16; ++k) glm::value_ptr(x.m)[k] = m[k].get<float>(); }
  if (n.contains("translation")) x.t = vec3(n["translation"][0], n["translation"][1], n["translation"][2]);
  if (n.contains("rotation")) x.r = quat(n["rotation"][3].get<float>(), n["rotation"][0].get<float>(), n["rotation"][1].get<float>(), n["rotation"][2].get<float>());
  if (n.contains("scale")) x.s = vec3(n["scale"][0], n["scale"][1], n["scale"][2]);
  return x;
}
mat4 trsMatrix(const TRS& x) {
  if (x.hasMatrix) return x.m;
  return glm::translate(mat4(1), x.t) * glm::mat4_cast(x.r) * glm::scale(mat4(1), x.s);
}

// ---- texture downsizing: box filter by an integer factor (sources are powers of two)
struct Img { int w = 0, h = 0, c = 0; std::vector<uint8_t> px; };
bool decodeImage(const Glb& g, int imageIdx, Img& out) {
  const json& im = g.j["images"][imageIdx];
  if (!im.contains("bufferView")) return false;
  const json& bv = g.j["bufferViews"][im["bufferView"].get<int>()];
  size_t off = bv.value("byteOffset", (size_t)0), len = bv["byteLength"].get<size_t>();
  int w, h, c;
  stbi_uc* d = stbi_load_from_memory(&g.bin[off], (int)len, &w, &h, &c, 4);
  if (!d) return false;
  out.w = w; out.h = h; out.c = 4; out.px.assign(d, d + (size_t)w * h * 4);
  stbi_image_free(d);
  return true;
}
Img downsize(const Img& s, int maxDim) {
  int f = 1;
  while (std::max(s.w, s.h) / f > maxDim) f *= 2;
  if (f == 1) return s;
  Img d; d.w = s.w / f; d.h = s.h / f; d.c = 4; d.px.resize((size_t)d.w * d.h * 4);
  for (int y = 0; y < d.h; ++y)
    for (int x = 0; x < d.w; ++x)
      for (int c = 0; c < 4; ++c) {
        uint32_t sum = 0;
        for (int j = 0; j < f; ++j) for (int i = 0; i < f; ++i) sum += s.px[((size_t)(y * f + j) * s.w + (x * f + i)) * 4 + c];
        d.px[((size_t)y * d.w + x) * 4 + c] = (uint8_t)(sum / (f * f));
      }
  return d;
}
}  // namespace

// ------------------------------------------------------------------ bake
bool bakeGlbModel(const fs::path& glbPath, const ImportSpec& spec, const fs::path& outDir, std::string& err, std::string& summary) {
  Glb g;
  if (!readGlb(glbPath, g, err)) return false;
  const json& J = g.j;
  int nn = (int)J["nodes"].size();
  std::vector<int> parent(nn, -1);
  for (int k = 0; k < nn; ++k) for (auto& c : J["nodes"][k].value("children", json::array())) parent[c.get<int>()] = k;
  std::vector<TRS> rest(nn);
  for (int k = 0; k < nn; ++k) rest[k] = nodeTRS(J["nodes"][k]);
  auto worldOf = [&](const std::vector<TRS>& pose, int k) { mat4 m(1); for (int n = k; n >= 0; n = parent[n]) m = trsMatrix(pose[n]) * m; return m; };
  auto nameOf = [&](int k) { return J["nodes"][k].value("name", std::string()); };
  auto chainHas = [&](int k, const std::string& nm) { for (int n = k; n >= 0; n = parent[n]) if (nameOf(n) == nm) return true; return false; };
  auto roleOf = [&](int k) -> std::string {
    for (int n = k; n >= 0; n = parent[n]) { auto it = spec.nodeParts.find(nameOf(n)); if (it != spec.nodeParts.end()) return it->second; }
    return "body";
  };
  auto hidden = [&](int k) { for (auto& h : spec.hide) if (chainHas(k, h)) return true; return false; };

  // ---- materials
  struct Mat { vec4 base{1}; float metal = 1, rough = 1; int alb = -1, mr = -1, nrm = -1, ao = -1; std::string name; };
  std::vector<Mat> mats;
  auto texImage = [&](const json& t) -> int { if (!t.is_object() || !t.contains("index")) return -1; return J["textures"][t["index"].get<int>()].value("source", -1); };
  for (auto& m : J.value("materials", json::array())) {
    Mat x;
    if (m.contains("pbrMetallicRoughness")) {
      auto& p = m["pbrMetallicRoughness"];
      if (p.contains("baseColorFactor")) x.base = vec4(p["baseColorFactor"][0], p["baseColorFactor"][1], p["baseColorFactor"][2], p["baseColorFactor"][3]);
      x.metal = p.value("metallicFactor", 1.0f); x.rough = p.value("roughnessFactor", 1.0f);
      x.alb = texImage(p.value("baseColorTexture", json())); x.mr = texImage(p.value("metallicRoughnessTexture", json()));
    }
    if (m.contains("extensions") && m["extensions"].contains("KHR_materials_pbrSpecularGlossiness")) {
      // spec-gloss -> metal-rough: metallic when the specular colour is bright, roughness = 1 - glossiness
      auto& e = m["extensions"]["KHR_materials_pbrSpecularGlossiness"];
      if (e.contains("diffuseFactor")) x.base = vec4(e["diffuseFactor"][0], e["diffuseFactor"][1], e["diffuseFactor"][2], e["diffuseFactor"][3]);
      vec3 sp(1); if (e.contains("specularFactor")) sp = vec3(e["specularFactor"][0], e["specularFactor"][1], e["specularFactor"][2]);
      x.metal = glm::dot(sp, vec3(0.2126f, 0.7152f, 0.0722f)) > 0.5f ? 1.0f : 0.0f;
      x.rough = 1.0f - e.value("glossinessFactor", 1.0f);
      x.alb = texImage(e.value("diffuseTexture", json()));
    }
    x.nrm = texImage(m.value("normalTexture", json()));
    x.ao = texImage(m.value("occlusionTexture", json()));
    x.name = m.value("name", "");
    // per-model material corrections (checked-in in the weapon JSON): repaint factor-only parts, tame gloss
    if (spec.matOverride.contains(x.name)) {
      const json& o = spec.matOverride[x.name];
      if (o.contains("base")) x.base = vec4(o["base"][0], o["base"][1], o["base"][2], o["base"].size() > 3 ? o["base"][3].get<float>() : 1.0f);
      x.metal = o.value("metal", x.metal); x.rough = o.value("rough", x.rough) * o.value("roughScale", 1.0f);
      if (o.value("dropTextures", false)) x.alb = x.mr = x.nrm = x.ao = -1;
    }
    x.base = vec4(vec3(x.base) * spec.tint, x.base.a);
    mats.push_back(x);
  }

  // ---- geometry in glTF world space, one part per (node, primitive)
  struct Prim { std::string name, role; int mat = -1; std::vector<TexVertex> v; std::vector<uint32_t> i; int node = -1; bool hasTan = false; };
  std::vector<Prim> prims;
  for (int k = 0; k < nn; ++k) {
    const json& n = J["nodes"][k];
    if (!n.contains("mesh") || hidden(k)) continue;
    const json& mesh = J["meshes"][n["mesh"].get<int>()];
    bool skinned = n.contains("skin");
    std::vector<mat4> jm;
    std::vector<int> joints;
    if (skinned) {
      const json& sk = J["skins"][n["skin"].get<int>()];
      for (auto& x : sk["joints"]) joints.push_back(x.get<int>());
      std::vector<float> ib; int c = 16;
      if (sk.contains("inverseBindMatrices")) ib = readAccessor(g, sk["inverseBindMatrices"].get<int>(), c);
      for (size_t q = 0; q < joints.size(); ++q) {
        mat4 inv(1); if (!ib.empty()) std::memcpy(glm::value_ptr(inv), &ib[q * 16], 64);
        jm.push_back(worldOf(rest, joints[q]) * inv);
      }
    }
    mat4 W = worldOf(rest, k);
    for (auto& pr : mesh["primitives"]) {
      if (pr.value("mode", 4) != 4) continue;
      auto& at = pr["attributes"];
      int cp, cn = 3, ct = 4, cu = 2, cj = 4, cw = 4;
      auto P = readAccessor(g, at["POSITION"].get<int>(), cp);
      std::vector<float> N, T, UV, JN, WT;
      if (at.contains("NORMAL")) N = readAccessor(g, at["NORMAL"].get<int>(), cn);
      if (at.contains("TANGENT")) T = readAccessor(g, at["TANGENT"].get<int>(), ct);
      if (at.contains("TEXCOORD_0")) UV = readAccessor(g, at["TEXCOORD_0"].get<int>(), cu);
      if (skinned && at.contains("JOINTS_0") && at.contains("WEIGHTS_0")) { JN = readAccessor(g, at["JOINTS_0"].get<int>(), cj); WT = readAccessor(g, at["WEIGHTS_0"].get<int>(), cw); }
      size_t nv = P.size() / 3;
      Prim q; q.node = k; q.mat = pr.value("material", -1); q.hasTan = !T.empty();
      std::map<int, float> jointWeight;
      for (size_t e = 0; e < nv; ++e) {
        mat4 M = W;
        if (!JN.empty()) {
          M = mat4(0); float ws = 0;
          for (int c = 0; c < 4; ++c) { float w = WT[e * 4 + c]; if (w <= 0) continue; int ji = (int)JN[e * 4 + c]; M += jm[ji] * w; ws += w; jointWeight[joints[ji]] += w; }
          if (ws < 1e-4f) M = jm[(int)JN[e * 4]];   // unweighted vertex: follow its first joint
          else M = M * (1.0f / ws);
        }
        glm::mat3 Nm = glm::transpose(glm::inverse(glm::mat3(M)));
        TexVertex v;
        v.pos = vec3(M * vec4(P[e * 3], P[e * 3 + 1], P[e * 3 + 2], 1));
        v.nrm = N.empty() ? vec3(0, 1, 0) : glm::normalize(Nm * vec3(N[e * 3], N[e * 3 + 1], N[e * 3 + 2]));
        if (!T.empty()) { vec3 t = glm::normalize(glm::mat3(M) * vec3(T[e * 4], T[e * 4 + 1], T[e * 4 + 2])); v.tan = vec4(t, T[e * 4 + 3] * (glm::determinant(glm::mat3(M)) < 0 ? -1.0f : 1.0f)); }
        else v.tan = vec4(1, 0, 0, 1);
        v.uv = UV.empty() ? vec2(0) : vec2(UV[e * 2], UV[e * 2 + 1]);
        q.v.push_back(v);
      }
      if (pr.contains("indices")) q.i = readIndices(g, pr["indices"].get<int>());
      else { q.i.resize(nv); for (size_t e = 0; e < nv; ++e) q.i[e] = (uint32_t)e; }
      if (glm::determinant(glm::mat3(W)) < 0 && JN.empty()) for (size_t e = 0; e + 2 < q.i.size(); e += 3) std::swap(q.i[e + 1], q.i[e + 2]);
      int roleNode = k;
      if (!jointWeight.empty()) roleNode = std::max_element(jointWeight.begin(), jointWeight.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
      q.role = roleOf(roleNode);
      // part name: nearest named ancestor (skip generic "Object_N"/"defaultMaterial" wrappers)
      q.name = nameOf(roleNode);
      for (int a = roleNode; a >= 0; a = parent[a]) { std::string nm = nameOf(a); if (!nm.empty() && nm.rfind("Object_", 0) != 0 && nm != "defaultMaterial") { q.name = nm; break; } }
      if (q.role == "hidden") continue;
      prims.push_back(std::move(q));
    }
  }
  if (prims.empty()) { err = "no visible triangles"; return false; }

  // ---- import transform: rotate, measure, scale to real length, place butt at z = 0, centre x/y
  quat R = glm::quat(glm::radians(spec.rot));
  vec3 lmin(1e9f), lmax(-1e9f), amin(1e9f), amax(-1e9f);
  for (auto& q : prims) {
    bool lengthPart = spec.lengthNode.empty() || chainHas(q.node, spec.lengthNode) || q.name == spec.lengthNode;
    for (auto& v : q.v) { vec3 p = R * v.pos; amin = glm::min(amin, p); amax = glm::max(amax, p); if (lengthPart) { lmin = glm::min(lmin, p); lmax = glm::max(lmax, p); } }
  }
  if (lmax.z <= lmin.z) { err = "length node '" + spec.lengthNode + "' has no extent"; return false; }
  float S = spec.realLength / (lmax.z - lmin.z);
  vec3 c = (lmin + lmax) * 0.5f;
  vec3 T = vec3(-c.x * S, -c.y * S, -lmin.z * S) + spec.offset;
  mat4 X = glm::translate(mat4(1), T) * glm::scale(mat4(1), vec3(S)) * glm::mat4_cast(R);
  mat4 Xi = glm::inverse(X);

  // ---- animation (first clip): weapon-space delta per sample for every part under an animated node
  json animJ = json::object();
  float duration = 0;
  std::vector<std::vector<mat4>> primAnim(prims.size());
  if (J.contains("animations") && !J["animations"].empty()) {
    const json& an = J["animations"][0];
    struct Ch { int node; std::string path; std::vector<float> t, v; int comps; std::string interp; };
    std::vector<Ch> chs;
    for (auto& ch : an["channels"]) {
      Ch x; x.node = ch["target"]["node"].get<int>(); x.path = ch["target"]["path"].get<std::string>();
      const json& sm = an["samplers"][ch["sampler"].get<int>()];
      int c1; x.t = readAccessor(g, sm["input"].get<int>(), c1); x.v = readAccessor(g, sm["output"].get<int>(), x.comps);
      x.interp = sm.value("interpolation", "LINEAR");
      if (!x.t.empty()) duration = std::max(duration, x.t.back());
      if (x.path == "translation" || x.path == "rotation" || x.path == "scale") chs.push_back(std::move(x));
    }
    if (duration > 0) {
      const int NS = 96;
      std::set<int> animated; for (auto& ch : chs) animated.insert(ch.node);
      for (size_t p = 0; p < prims.size(); ++p) {
        bool any = false; for (int a = prims[p].node; a >= 0; a = parent[a]) if (animated.count(a)) any = true;
        if (!any) continue;
        mat4 restW = worldOf(rest, prims[p].node);
        for (int si = 0; si < NS; ++si) {
          float t = duration * si / (NS - 1);
          std::vector<TRS> pose = rest;
          for (auto& ch : chs) {
            size_t n = ch.t.size(); size_t i1 = 0; while (i1 < n && ch.t[i1] < t) ++i1;
            size_t i0 = i1 == 0 ? 0 : i1 - 1; if (i1 >= n) i1 = n - 1;
            float u = (ch.t[i1] > ch.t[i0]) ? (t - ch.t[i0]) / (ch.t[i1] - ch.t[i0]) : 0.0f;
            if (ch.interp == "STEP") u = 0;
            int cs = ch.comps, mul = ch.interp == "CUBICSPLINE" ? 3 : 1, off = ch.interp == "CUBICSPLINE" ? cs : 0;
            auto val = [&](size_t i, int c) { return ch.v[(i * mul) * cs + off + c]; };
            if (ch.path == "rotation") {
              quat a(val(i0, 3), val(i0, 0), val(i0, 1), val(i0, 2)), b(val(i1, 3), val(i1, 0), val(i1, 1), val(i1, 2));
              pose[ch.node].r = glm::normalize(glm::slerp(a, b, u)); pose[ch.node].hasMatrix = false;
            } else {
              vec3 a(val(i0, 0), val(i0, 1), val(i0, 2)), b(val(i1, 0), val(i1, 1), val(i1, 2));
              (ch.path == "translation" ? pose[ch.node].t : pose[ch.node].s) = glm::mix(a, b, u); pose[ch.node].hasMatrix = false;
            }
          }
          primAnim[p].push_back(X * worldOf(pose, prims[p].node) * glm::inverse(restW) * Xi);
        }
      }
    }
  }

  // ---- merge into one vertex/index buffer (weapon space)
  std::vector<TexVertex> V; std::vector<uint32_t> I;
  json parts = json::array();
  vec3 bmin(1e9f), bmax(-1e9f);
  for (size_t p = 0; p < prims.size(); ++p) {
    auto& q = prims[p];
    uint32_t base = (uint32_t)V.size(), first = (uint32_t)I.size();
    vec3 pmin(1e9f), pmax(-1e9f);
    std::vector<TexVertex> qv = q.v;
    for (auto& v : qv) { v.pos = vec3(X * vec4(v.pos, 1)); v.nrm = glm::normalize(R * v.nrm); v.tan = vec4(glm::normalize(R * vec3(v.tan)), v.tan.w); }
    std::vector<uint8_t> dropV(qv.size(), 0);
    if (!spec.meshEdits.empty()) {
      // islands: union-find over vertices, joined by triangles and by coincident positions (split UV seams)
      std::vector<int> par(qv.size()); for (size_t k = 0; k < par.size(); ++k) par[k] = (int)k;
      std::function<int(int)> find = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
      auto uni = [&](int a, int b) { a = find(a); b = find(b); if (a != b) par[a] = b; };
      for (size_t t = 0; t + 2 < q.i.size(); t += 3) { uni(q.i[t], q.i[t + 1]); uni(q.i[t], q.i[t + 2]); }
      std::map<std::tuple<long, long, long>, int> at;
      for (size_t k = 0; k < qv.size(); ++k) {
        auto key = std::make_tuple(std::lround(qv[k].pos.x * 1e5f), std::lround(qv[k].pos.y * 1e5f), std::lround(qv[k].pos.z * 1e5f));
        auto it = at.find(key); if (it == at.end()) at[key] = (int)k; else uni((int)k, it->second);
      }
      for (auto& me : spec.meshEdits) {
        std::map<int, bool> inside;   // island root -> every vertex inside the box
        for (size_t k = 0; k < qv.size(); ++k) {
          const vec3& p = qv[k].pos; bool in = glm::all(glm::greaterThanEqual(p, me.lo)) && glm::all(glm::lessThanEqual(p, me.hi));
          auto r = inside.emplace(find((int)k), in); if (!in) r.first->second = false;
        }
        mat4 E = glm::translate(mat4(1), me.move + me.pivot) * glm::rotate(mat4(1), glm::radians(me.deg), glm::normalize(me.axis)) * glm::translate(mat4(1), -me.pivot);
        glm::mat3 En(E);
        int moved = 0;
        for (size_t k = 0; k < qv.size(); ++k) {
          if (dropV[k] || !inside[find((int)k)]) continue;
          ++moved;
          if (me.hide) { dropV[k] = 1; continue; }
          if (me.radial) {   // positions only: the ring's normals stay radial / axial
            vec2 o = vec2(qv[k].pos) - me.centre; float r = glm::length(o);
            if (r < me.rMax && r > 1e-7f) { o *= me.scale; qv[k].pos.x = me.centre.x + o.x; qv[k].pos.y = me.centre.y + o.y; } else --moved;
            continue;
          }
          qv[k].pos = vec3(E * vec4(qv[k].pos, 1)); qv[k].nrm = glm::normalize(En * qv[k].nrm); qv[k].tan = vec4(glm::normalize(En * vec3(qv[k].tan)), qv[k].tan.w);
        }
        if (moved) logf("  mesh edit on '%s': %d vertices %s (%s)", q.name.c_str(), moved, me.hide ? "hidden" : "re-posed", me.note.c_str());
      }
    }
    for (auto& v : qv) { pmin = glm::min(pmin, v.pos); pmax = glm::max(pmax, v.pos); V.push_back(v); }
    for (size_t t = 0; t + 2 < q.i.size(); t += 3) {
      if (dropV[q.i[t]] || dropV[q.i[t + 1]] || dropV[q.i[t + 2]]) continue;
      for (int c = 0; c < 3; ++c) I.push_back(base + q.i[t + c]);
    }
    bmin = glm::min(bmin, pmin); bmax = glm::max(bmax, pmax);
    Mat m = q.mat >= 0 && q.mat < (int)mats.size() ? mats[q.mat] : Mat{};
    json pj = {{"name", q.name}, {"role", q.role}, {"first", first}, {"count", (uint32_t)I.size() - first},
               {"base", {m.base.r, m.base.g, m.base.b, m.base.a}}, {"metal", m.metal}, {"rough", m.rough}, {"textured", m.alb >= 0}, {"mat", q.mat},
               {"bmin", tojson(pmin)}, {"bmax", tojson(pmax)}};
    if (!primAnim[p].empty()) {
      json a = json::array();
      for (auto& M : primAnim[p]) { json r = json::array(); for (int k = 0; k < 16; ++k) r.push_back(std::round(glm::value_ptr(M)[k] * 1e6f) / 1e6f); a.push_back(r); }
      pj["anim"] = a;
    }
    parts.push_back(pj);
  }

  // ---- textures: one set per source material; each (image, role) is decoded, downsized and written once
  std::error_code ec; fs::create_directories(outDir, ec);
  for (auto& e : fs::directory_iterator(outDir, ec)) { auto x = e.path().extension().string(); if (x == ".png" || x == ".jpg") fs::remove(e.path(), ec); }   // no stale maps
  json tex = json::object(), matsJ = json::array();
  size_t texBytes = 0; int texFiles = 0;
  std::map<std::pair<int, int>, std::string> written;
  auto save = [&](int imgIdx, int role) -> std::string {   // role 0 albedo (sRGB JPEG), 1 metal-rough, 2 normal, 3 AO (linear PNG)
    if (imgIdx < 0) return "";
    auto key = std::make_pair(imgIdx, role);
    if (written.count(key)) return written[key];
    Img a; if (!decodeImage(g, imgIdx, a)) { logf("import: image %d failed to decode", imgIdx); return ""; }
    Img d = downsize(a, spec.maxTex);
    static const char* pre[4] = {"albedo", "mr", "normal", "ao"};
    std::string file = std::string(pre[role]) + "_" + std::to_string(imgIdx) + (role == 0 ? ".jpg" : ".png");
    fs::path pth = outDir / file;
    if (role == 0) { std::vector<uint8_t> rgb((size_t)d.w * d.h * 3); for (size_t k = 0; k < (size_t)d.w * d.h; ++k) std::memcpy(&rgb[k * 3], &d.px[k * 4], 3);
                     stbi_write_jpg(pth.string().c_str(), d.w, d.h, 3, rgb.data(), 92); }
    else stbi_write_png(pth.string().c_str(), d.w, d.h, 4, d.px.data(), d.w * 4);
    texBytes += fs::file_size(pth, ec); ++texFiles;
    return written[key] = file;
  };
  for (auto& m : mats) {
    json mj = {{"name", m.name}};
    std::string fa = save(m.alb, 0), fm = save(m.mr, 1), fn = save(m.nrm, 2);
    if (!fa.empty()) mj["albedo"] = fa;
    if (!fm.empty()) mj["mr"] = fm;
    if (!fn.empty()) mj["normal"] = fn;
    if (m.ao >= 0 && m.ao == m.mr) mj["aoInMR"] = true;
    else { std::string fo = save(m.ao, 3); if (!fo.empty()) mj["ao"] = fo; }
    matsJ.push_back(mj);
  }
  {
    std::ofstream f(outDir / "mesh.bin", std::ios::binary);
    uint32_t hdr[3] = {0x31574D4Eu, (uint32_t)V.size(), (uint32_t)I.size()};   // "NMW1"
    f.write((const char*)hdr, sizeof hdr);
    f.write((const char*)V.data(), V.size() * sizeof(TexVertex));
    f.write((const char*)I.data(), I.size() * 4);
  }
  json out = {{"source", glbPath.filename().string()}, {"scale", S}, {"rot", tojson(spec.rot)}, {"realLength", spec.realLength},
              {"bmin", tojson(bmin)}, {"bmax", tojson(bmax)}, {"textures", tex}, {"materials", matsJ}, {"animDuration", duration}, {"parts", parts},
              {"vertices", V.size()}, {"triangles", I.size() / 3}};
  { std::ofstream f(outDir / "model.json"); f << out.dump(1); }
  char b[320];
  std::snprintf(b, sizeof b, "%s: %zu verts %zu tris %zu parts, scale %.5f, bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f), %zu materials, %d texture files %.1f MB, anim %.2f s",
                glbPath.filename().string().c_str(), V.size(), I.size() / 3, prims.size(), S, bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z, mats.size(), texFiles, texBytes / 1048576.0, duration);
  summary = b;
  return true;
}

// ------------------------------------------------------------------ runtime
static GLuint loadTex(const fs::path& p, bool srgb, size_t& bytes) {
  int w, h, c;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &c, 4);
  if (!d) return 0;
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  glTextureStorage2D(t, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(t, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d);
  glGenerateTextureMipmap(t);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
  stbi_image_free(d);
  bytes += (size_t)w * h * 4 * 4 / 3;
  return t;
}

bool GpuModel::load(const fs::path& dir, std::string& err) {
  destroy();
  json j;
  try { j = json::parse(readTextFile(dir / "model.json")); } catch (...) { err = "missing or bad " + (dir / "model.json").string(); return false; }
  std::ifstream f(dir / "mesh.bin", std::ios::binary);
  uint32_t hdr[3] = {0, 0, 0};
  if (!f.read((char*)hdr, sizeof hdr) || hdr[0] != 0x31574D4Eu) { err = "missing or bad mesh.bin"; return false; }
  std::vector<TexVertex> V(hdr[1]); std::vector<uint32_t> I(hdr[2]);
  f.read((char*)V.data(), V.size() * sizeof(TexVertex)); f.read((char*)I.data(), I.size() * 4);
  if (!f) { err = "truncated mesh.bin"; return false; }
  for (auto& pj : j["parts"]) {
    ModelPart p;
    p.name = pj.value("name", ""); p.role = pj.value("role", "body");
    p.first = pj.value("first", 0u); p.count = pj.value("count", 0u);
    auto& b = pj["base"]; p.base = vec4(b[0], b[1], b[2], b[3]);
    p.metal = pj.value("metal", 1.0f); p.rough = pj.value("rough", 1.0f); p.textured = pj.value("textured", false);
    p.bmin = jvec3(pj["bmin"]); p.bmax = jvec3(pj["bmax"]);
    p.mat = pj.value("mat", -1);
    for (auto& a : pj.value("anim", json::array())) { mat4 m; for (int k = 0; k < 16; ++k) glm::value_ptr(m)[k] = a[k].get<float>(); p.anim.push_back(m); }
    parts.push_back(std::move(p));
  }
  animDuration = j.value("animDuration", 0.0f);
  bmin = jvec3(j["bmin"]); bmax = jvec3(j["bmax"]);
  tris = (int)I.size() / 3;
  glCreateVertexArrays(1, &vao); glCreateBuffers(1, &vbo); glCreateBuffers(1, &ebo);
  glNamedBufferStorage(vbo, V.size() * sizeof(TexVertex), V.data(), 0);
  glNamedBufferStorage(ebo, I.size() * 4, I.data(), 0);
  gpuBytes = V.size() * sizeof(TexVertex) + I.size() * 4;
  glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(TexVertex));
  glVertexArrayElementBuffer(vao, ebo);
  auto attr = [&](int loc, int n, size_t off) { glEnableVertexArrayAttrib(vao, loc); glVertexArrayAttribFormat(vao, loc, n, GL_FLOAT, GL_FALSE, (GLuint)off); glVertexArrayAttribBinding(vao, loc, 0); };
  attr(0, 3, offsetof(TexVertex, pos)); attr(1, 3, offsetof(TexVertex, nrm)); attr(2, 4, offsetof(TexVertex, tan)); attr(3, 2, offsetof(TexVertex, uv));
  std::map<std::string, GLuint> cache;
  auto tex = [&](const json& mj, const char* key, bool srgb) -> GLuint {
    if (!mj.contains(key)) return 0;
    std::string f = mj[key].get<std::string>();
    auto it = cache.find(f); if (it != cache.end()) return it->second;
    GLuint t = loadTex(dir / f, srgb, gpuBytes); if (t) ++textureCount;
    return cache[f] = t;
  };
  if (j.contains("materials")) {
    for (auto& mj : j["materials"]) {
      MatTex m; m.name = mj.value("name", "");
      m.alb = tex(mj, "albedo", true); m.mr = tex(mj, "mr", false); m.n = tex(mj, "normal", false); m.ao = tex(mj, "ao", false);
      m.aoInMR = mj.value("aoInMR", false);
      mats.push_back(m);
    }
  } else {   // legacy single-set bake
    auto& t = j["textures"];
    MatTex m; m.alb = tex(t, "albedo", true); m.mr = tex(t, "mr", false); m.n = tex(t, "normal", false); m.ao = tex(t, "ao", false); m.aoInMR = t.value("aoInMR", false);
    mats.push_back(m);
    for (auto& p : parts) if (p.textured) p.mat = 0;
  }
  if (!mats.empty()) { texAlb = mats[0].alb; texMR = mats[0].mr; texN = mats[0].n; texAO = mats[0].ao; aoInMR = mats[0].aoInMR; }
  for (auto& m : mats) if (!texAlb && m.alb) texAlb = m.alb;
  return true;
}

void GpuModel::destroy() {
  if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ebo); }
  std::set<GLuint> ts;
  for (auto& m : mats) for (GLuint x : {m.alb, m.mr, m.n, m.ao}) if (x) ts.insert(x);
  for (GLuint x : ts) glDeleteTextures(1, &x);
  mats.clear(); textureCount = 0;
  vao = vbo = ebo = texAlb = texMR = texN = texAO = 0;
  parts.clear(); gpuBytes = 0;
}

int GpuModel::find(const std::string& name) const { for (size_t k = 0; k < parts.size(); ++k) if (parts[k].name == name) return (int)k; return -1; }

mat4 GpuModel::animAt(int part, float u) const {
  if (part < 0 || part >= (int)parts.size() || parts[part].anim.empty()) return mat4(1);
  auto& a = parts[part].anim;
  float f = std::clamp(u, 0.0f, 1.0f) * (a.size() - 1);
  size_t i = (size_t)f; if (i >= a.size() - 1) return a.back();
  float w = f - i;
  // matrices from the same rigid motion: blend translation and rotation separately
  quat q0 = glm::quat_cast(glm::mat3(a[i])), q1 = glm::quat_cast(glm::mat3(a[i + 1]));
  mat4 m = glm::mat4_cast(glm::slerp(q0, q1, w));
  m[3] = glm::mix(a[i][3], a[i + 1][3], w);
  return m;
}

bool importWeaponModels(const fs::path& srcDir, const fs::path& contentRoot) {
  fs::path wdir = contentRoot / "weapons";
  std::map<std::string, json> done;   // output dir -> the model block that baked it
  int ok = 0, bad = 0;
  std::vector<fs::path> files;   // sorted: the directory order differs between file systems
  for (auto& e : fs::directory_iterator(wdir)) if (e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    json j; try { j = json::parse(readTextFile(f)); } catch (...) { continue; }
    if (!j.contains("model")) continue;
    ImportSpec s = importSpecFromJson(j["model"]);
    if (s.src.empty() || s.dir.empty()) continue;
    if (done.count(s.dir)) {   // weapons sharing one baked model (M1 / M1C Garand) must agree on how it is baked
      json a = done[s.dir], b = j["model"]; a.erase("overallLength"); b.erase("overallLength");
      if (a != b) logf("import %-12s WARNING: %s has a different model block for the shared folder; the first one was baked", s.dir.c_str(), f.filename().string().c_str());
      continue;
    }
    done[s.dir] = j["model"];
    fs::path glb = srcDir / s.src;
    if (!fs::exists(glb)) { logf("import %-12s skipped: %s not found", s.dir.c_str(), glb.string().c_str()); continue; }
    std::string err, sum;
    if (bakeGlbModel(glb, s, wdir / "models" / s.dir, err, sum)) { logf("import %-12s %s", s.dir.c_str(), sum.c_str()); ++ok; }
    else { logf("import %-12s FAILED: %s", s.dir.c_str(), err.c_str()); ++bad; }
  }
  logf("import: %d baked, %d failed", ok, bad);
  return bad == 0;
}
