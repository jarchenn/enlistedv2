// glTF binary import (offline bake) and the runtime textured model. No third-party glTF library: the GLB
#include <set>
#include <atomic>
#include "user_cache.h"
#include "crumb.h"
// container is a 12-byte header, a JSON chunk and a BIN chunk, and accessors are read directly.
#include <chrono>
#include "model.h"
#include "simplify.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_dxt.h>   // TK2: block compression of large maps at load (the implementation is in textures.cpp)
#include <thread>
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
  s.scale = j.value("scale", 0.0f); s.axes = jvec3(j.value("axes", json()), vec3(1)); s.pivot = j.value("pivot", std::string("weapon"));
  s.lengthNode = j.value("lengthNode", "");
  s.offset = jvec3(j.value("offset", json()), vec3(0));
  json np = j.value("nodeParts", json::object()), hd = j.value("hide", json::array());   // keep the temporaries alive
  for (auto& [k, v] : np.items()) s.nodeParts[k] = v.get<std::string>();
  for (auto& h : hd) s.hide.push_back(h.get<std::string>());
  s.maxTex = j.value("maxTex", 1024); s.jpgMaps = j.value("jpgMaps", false);
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
  { json kb = j.value("keepIslandsIn", json::array()); if (kb.size() == 2) { s.keepOn = true; s.keepLo = jvec3(kb[0], vec3(0)); s.keepHi = jvec3(kb[1], vec3(0)); } }
  s.simplify = j.value("simplify", 1.0f); s.lod1 = j.value("lod1", 0.0f); s.lod1Tex = j.value("lod1Tex", 256);
  s.lod1Src = j.value("lod1Src", std::string());   // NT8: a hand-made LOD1 (its own GLB next to src), baked as is
  s.lod2 = j.value("lod2", 0.0f); s.lod2Tex = j.value("lod2Tex", 128);
  if (j.contains("maxTexBy") && j["maxTexBy"].is_object()) for (auto& [k, v] : jkey(j, "maxTexBy").items()) s.maxTexBy[k] = v.get<int>();
  if (j.contains("sections")) { s.sectionAxis = jkey(j, "sections").value("axis", 2); s.sectionCount = std::max(1, jkey(j, "sections").value("count", 1)); }
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
  struct Mat { vec4 base{1}; float metal = 1, rough = 1; int alb = -1, mr = -1, nrm = -1, ao = -1; std::string name;
               std::string eBase, eNrm, eRough, eMetal; float uvScale = 1; bool cutout = false; };   // e*: external texture files (matOverride "textures", relative to the GLB), packed at bake time
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
    x.cutout = m.value("alphaMode", std::string("OPAQUE")) != "OPAQUE";   // NT8: needles, grass, fern fronds (alpha tested)
    x.nrm = texImage(m.value("normalTexture", json()));
    x.ao = texImage(m.value("occlusionTexture", json()));
    x.name = m.value("name", "");
    // per-model material corrections (checked-in in the weapon JSON): repaint factor-only parts, tame gloss
    if (spec.matOverride.contains(x.name)) {
      const json& o = spec.matOverride[x.name];
      if (o.contains("base")) x.base = vec4(o["base"][0], o["base"][1], o["base"][2], o["base"].size() > 3 ? o["base"][3].get<float>() : 1.0f);
      x.metal = o.value("metal", x.metal); x.rough = o.value("rough", x.rough) * o.value("roughScale", 1.0f);
      if (o.value("dropTextures", false)) x.alb = x.mr = x.nrm = x.ao = -1;
      if (o.contains("textures")) {   // a GLB without images: an external PBR set (colour / normal GL / roughness / metalness), tiled by uvScale
        const json& t = o["textures"];
        x.eBase = t.value("base", ""); x.eNrm = t.value("normal", ""); x.eRough = t.value("rough", ""); x.eMetal = t.value("metal", ""); x.uvScale = t.value("uvScale", 1.0f);
        x.alb = x.mr = x.nrm = x.ao = -1; if (!o.contains("base")) x.base = vec4(1); x.metal = o.value("metal", 1.0f); x.rough = o.value("rough", 1.0f);
      }
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
        v.uv = UV.empty() ? vec2(0) : vec2(UV[e * 2], UV[e * 2 + 1]) * (q.mat >= 0 ? mats[q.mat].uvScale : 1.0f);
        q.v.push_back(v);
      }
      if (pr.contains("indices")) q.i = readIndices(g, pr["indices"].get<int>());
      else { q.i.resize(nv); for (size_t e = 0; e < nv; ++e) q.i[e] = (uint32_t)e; }
      if (glm::determinant(glm::mat3(W)) < 0 && JN.empty()) for (size_t e = 0; e + 2 < q.i.size(); e += 3) std::swap(q.i[e + 1], q.i[e + 2]);
      if (T.empty() && !UV.empty()) {   // no TANGENT attribute: derive it from the UVs (glTF v runs down the image) so a normal map works
        std::vector<vec3> ta(q.v.size(), vec3(0)); std::vector<float> sg(q.v.size(), 1.0f);
        for (size_t e = 0; e + 2 < q.i.size(); e += 3) {
          const TexVertex &a = q.v[q.i[e]], &b = q.v[q.i[e + 1]], &c = q.v[q.i[e + 2]];
          vec3 e1 = b.pos - a.pos, e2 = c.pos - a.pos; vec2 u1(b.uv.x - a.uv.x, a.uv.y - b.uv.y), u2(c.uv.x - a.uv.x, a.uv.y - c.uv.y);
          float det = u1.x * u2.y - u2.x * u1.y; if (std::fabs(det) < 1e-12f) continue;
          vec3 t = (e1 * u2.y - e2 * u1.y) / det, bt = (e2 * u1.x - e1 * u2.x) / det;
          float s = glm::dot(glm::cross(a.nrm, t), bt) < 0 ? -1.0f : 1.0f;
          for (int k = 0; k < 3; ++k) { ta[q.i[e + k]] += t; sg[q.i[e + k]] = s; }
        }
        for (size_t k = 0; k < q.v.size(); ++k) if (glm::dot(ta[k], ta[k]) > 1e-18f) q.v[k].tan = vec4(glm::normalize(ta[k]), sg[k]);
      }
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
  if (spec.keepOn) {
    int kept = 0, dropped = 0;
    for (auto& q : prims) {
      std::vector<int> par(q.v.size()); for (size_t k = 0; k < par.size(); ++k) par[k] = (int)k;
      std::function<int(int)> find = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
      auto uni = [&](int a, int b) { a = find(a); b = find(b); if (a != b) par[a] = b; };
      for (size_t t = 0; t + 2 < q.i.size(); t += 3) { uni(q.i[t], q.i[t + 1]); uni(q.i[t], q.i[t + 2]); }
      std::map<std::tuple<long, long, long>, int> at;   // coincident positions join islands (split UV seams)
      for (size_t k = 0; k < q.v.size(); ++k) {
        auto key = std::make_tuple(std::lround(q.v[k].pos.x * 1e5f), std::lround(q.v[k].pos.y * 1e5f), std::lround(q.v[k].pos.z * 1e5f));
        auto it = at.find(key); if (it == at.end()) at[key] = (int)k; else uni((int)k, it->second);
      }
      std::vector<char> bad(q.v.size(), 0);   // per island root: a vertex outside the box
      for (size_t k = 0; k < q.v.size(); ++k) {
        vec3 p = R * q.v[k].pos;
        if (!(glm::all(glm::greaterThanEqual(p, spec.keepLo)) && glm::all(glm::lessThanEqual(p, spec.keepHi)))) bad[find((int)k)] = 1;
      }
      std::vector<int> remap(q.v.size(), -1); std::vector<TexVertex> nv; std::vector<uint32_t> ni;
      for (size_t t = 0; t + 2 < q.i.size(); t += 3) {
        if (bad[find(q.i[t])]) { ++dropped; continue; }
        ++kept;
        for (int c = 0; c < 3; ++c) { int& r = remap[q.i[t + c]]; if (r < 0) { r = (int)nv.size(); nv.push_back(q.v[q.i[t + c]]); } ni.push_back((uint32_t)r); }
      }
      q.v = std::move(nv); q.i = std::move(ni);
    }
    prims.erase(std::remove_if(prims.begin(), prims.end(), [](const Prim& q) { return q.i.empty(); }), prims.end());
    logf("  keepIslandsIn: %d triangles kept, %d dropped", kept, dropped);
    if (prims.empty()) { err = "keepIslandsIn left no triangles"; return false; }
  }
  vec3 lmin(1e9f), lmax(-1e9f), amin(1e9f), amax(-1e9f);
  for (auto& q : prims) {
    bool lengthPart = spec.lengthNode.empty() || chainHas(q.node, spec.lengthNode) || q.name == spec.lengthNode;
    for (auto& v : q.v) { vec3 p = R * v.pos; amin = glm::min(amin, p); amax = glm::max(amax, p); if (lengthPart) { lmin = glm::min(lmin, p); lmax = glm::max(lmax, p); } }
  }
  if (lmax.z <= lmin.z) { err = "length node '" + spec.lengthNode + "' has no extent"; return false; }
  float S = spec.scale > 0 ? spec.scale : spec.realLength / (lmax.z - lmin.z);
  vec3 c = (lmin + lmax) * 0.5f;
  const vec3 SA = vec3(S) * spec.axes;   // PR3: per-axis stretch (1 for everything else)
  vec3 T = spec.pivot == "ground" ? vec3(-c.x, -lmin.y, -c.z) * SA + spec.offset   // map asset: on the ground, centred
                                  : vec3(-c.x, -c.y, -lmin.z) * SA + spec.offset;
  mat4 X = glm::translate(mat4(1), T) * glm::scale(mat4(1), SA) * glm::mat4_cast(R);
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
  size_t simpBefore = 0, simpAfter = 0;
  vec3 bmin(1e9f), bmax(-1e9f);
  for (size_t p = 0; p < prims.size(); ++p) {
    auto& q = prims[p];
    uint32_t base = (uint32_t)V.size();
    vec3 pmin(1e9f), pmax(-1e9f);
    std::vector<TexVertex> qv = q.v;
    for (auto& v : qv) { v.pos = vec3(X * vec4(v.pos, 1)); v.nrm = glm::normalize((R * v.nrm) / spec.axes); v.tan = vec4(glm::normalize((R * vec3(v.tan)) * spec.axes), v.tan.w); }   // PR3: normals by the inverse stretch
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
    std::vector<uint32_t> li; li.reserve(q.i.size());
    for (size_t t = 0; t + 2 < q.i.size(); t += 3) {
      if (dropV[q.i[t]] || dropV[q.i[t + 1]] || dropV[q.i[t + 2]]) continue;
      for (int c = 0; c < 3; ++c) li.push_back(q.i[t + c]);
    }
    if (spec.simplify < 0.999f && li.size() >= 3 * 64) {   // runtime budget for heavy sources (map assets)
      size_t before = li.size() / 3;
      simplifyMesh(qv, li, std::max<size_t>(32, (size_t)(before * spec.simplify)));
      simpBefore += before; simpAfter += li.size() / 3;
    }
    for (auto& v : qv) { pmin = glm::min(pmin, v.pos); pmax = glm::max(pmax, v.pos); V.push_back(v); }
    bmin = glm::min(bmin, pmin); bmax = glm::max(bmax, pmax);
    Mat m = q.mat >= 0 && q.mat < (int)mats.size() ? mats[q.mat] : Mat{};
    int nsec = spec.sectionCount, ax = spec.sectionAxis;
    float s0 = (amin[ax] * S + T[ax]), s1 = (amax[ax] * S + T[ax]);   // whole model's extent along the section axis
    for (int sec = 0; sec < nsec; ++sec) {
      uint32_t first = (uint32_t)I.size();
      vec3 smin(1e9f), smax(-1e9f);
      for (size_t t = 0; t + 2 < li.size(); t += 3) {
        if (nsec > 1) {
          float cpos = (qv[li[t]].pos[ax] + qv[li[t + 1]].pos[ax] + qv[li[t + 2]].pos[ax]) / 3;
          int k = std::clamp((int)std::floor((cpos - s0) / std::max(1e-6f, s1 - s0) * nsec), 0, nsec - 1);
          if (k != sec) continue;
        }
        for (int c = 0; c < 3; ++c) { I.push_back(base + li[t + c]); smin = glm::min(smin, qv[li[t + c]].pos); smax = glm::max(smax, qv[li[t + c]].pos); }
      }
      if (nsec > 1 && I.size() == first) continue;
      if (nsec == 1) { smin = pmin; smax = pmax; }
      json pj = {{"name", nsec > 1 ? q.name + "#" + std::to_string(sec) : q.name}, {"role", q.role}, {"first", first}, {"count", (uint32_t)I.size() - first},
                 {"base", {m.base.r, m.base.g, m.base.b, m.base.a}}, {"metal", m.metal}, {"rough", m.rough}, {"textured", m.alb >= 0}, {"mat", q.mat},
                 {"bmin", tojson(smin)}, {"bmax", tojson(smax)}};
      if (nsec > 1) pj["section"] = sec;
      if (!primAnim[p].empty()) {
        json a = json::array();
        for (auto& M : primAnim[p]) { json r = json::array(); for (int k = 0; k < 16; ++k) r.push_back(std::round(glm::value_ptr(M)[k] * 1e6f) / 1e6f); a.push_back(r); }
        pj["anim"] = a;
      }
      parts.push_back(pj);
    }
  }

  // ---- textures: one set per source material; each (image, role) is decoded, downsized and written once
  std::error_code ec; fs::create_directories(outDir, ec);
  for (auto& e : fs::directory_iterator(outDir, ec)) { auto x = e.path().extension().string(); if (x == ".png" || x == ".jpg") fs::remove(e.path(), ec); }   // no stale maps
  json tex = json::object(), matsJ = json::array();
  size_t texBytes = 0; int texFiles = 0;
  std::map<std::pair<int, int>, std::string> written;
  static const char* pre[5] = {"albedo", "mr", "normal", "ao", "albedoa"};
  int maxNow = spec.maxTex;   // TK2: raised for the albedo / normal of a material in maxTexBy
  auto emit = [&](const Img& a, int role, const std::string& file) -> std::string {   // downsize + write one map
    Img d = downsize(a, role == 0 || role == 2 || role == 4 ? maxNow : spec.maxTex);
    std::string fn = file;
    if (spec.jpgMaps && role != 0 && role != 4) fn = fs::path(file).replace_extension(".jpg").string();   // PR1: normal / MR as JPEG (a tenth of the size)
    fs::path pth = outDir / fn;
    if (role == 4) { stbi_write_png(pth.string().c_str(), d.w, d.h, 4, d.px.data(), d.w * 4); texBytes += fs::file_size(pth, ec); ++texFiles; return fn; }   // NT8 cutout albedo: RGBA
    if (role == 0 || spec.jpgMaps) { std::vector<uint8_t> rgb((size_t)d.w * d.h * 3); for (size_t k = 0; k < (size_t)d.w * d.h; ++k) std::memcpy(&rgb[k * 3], &d.px[k * 4], 3);
                     stbi_write_jpg(pth.string().c_str(), d.w, d.h, 3, rgb.data(), 92); }
    else stbi_write_png(pth.string().c_str(), d.w, d.h, 4, d.px.data(), d.w * 4);
    texBytes += fs::file_size(pth, ec); ++texFiles;
    return fn;
  };
  auto loadExt = [&](const std::string& rel, Img& out) -> bool {   // external texture file next to the GLB
    int w, h, c; stbi_uc* p = stbi_load((glbPath.parent_path() / rel).string().c_str(), &w, &h, &c, 4);
    if (!p) { logf("import: external texture %s not found", rel.c_str()); return false; }
    out.w = w; out.h = h; out.c = 4; out.px.assign(p, p + (size_t)w * h * 4); stbi_image_free(p); return true;
  };
  auto save = [&](int imgIdx, int role) -> std::string {   // role 0 albedo (sRGB JPEG), 1 metal-rough, 2 normal, 3 AO (linear PNG)
    if (imgIdx < 0) return "";
    auto key = std::make_pair(imgIdx, role);
    if (written.count(key)) return written[key];
    Img a; if (!decodeImage(g, imgIdx, a)) { logf("import: image %d failed to decode", imgIdx); return ""; }
    return written[key] = emit(a, role, std::string(pre[role]) + "_" + std::to_string(imgIdx) + (role == 0 ? ".jpg" : ".png"));   // 4: RGBA albedo (cutout)
  };
  std::set<int> usedMats; for (auto& q : prims) usedMats.insert(q.mat);   // only materials a kept part uses get textures
  for (size_t mi = 0; mi < mats.size(); ++mi) {
    const Mat& m = mats[mi];
    json mj = {{"name", m.name}};
    if (!usedMats.count((int)mi)) { matsJ.push_back(mj); continue; }
    { auto it = spec.maxTexBy.find(m.name); maxNow = it != spec.maxTexBy.end() ? std::max(spec.maxTex, it->second) : spec.maxTex; }
    std::string fa = m.cutout ? save(m.alb, 4) : save(m.alb, 0), fm = save(m.mr, 1), fn = save(m.nrm, 2);
    if (m.cutout) mj["cutout"] = true;
    if (!fa.empty()) mj["albedo"] = fa;
    if (!fm.empty()) mj["mr"] = fm;
    if (!fn.empty()) mj["normal"] = fn;
    if (!m.eBase.empty()) { Img a; if (loadExt(m.eBase, a)) mj["albedo"] = emit(a, 0, "albedo_x" + std::to_string(mi) + ".jpg"); }
    if (!m.eNrm.empty()) { Img a; if (loadExt(m.eNrm, a)) mj["normal"] = emit(a, 2, "normal_x" + std::to_string(mi) + ".png"); }
    if (!m.eRough.empty() || !m.eMetal.empty()) {   // glTF packing: G = roughness, B = metalness (R = 1, no AO)
      Img r, mt; bool hr = !m.eRough.empty() && loadExt(m.eRough, r), hm = !m.eMetal.empty() && loadExt(m.eMetal, mt);
      const Img& ref = hr ? r : mt; Img o; o.w = ref.w; o.h = ref.h; o.c = 4; o.px.assign((size_t)o.w * o.h * 4, 255);
      for (size_t k = 0; k < (size_t)o.w * o.h; ++k) {
        if (hr && r.px.size() == o.px.size()) o.px[k * 4 + 1] = r.px[k * 4]; else if (!hr) o.px[k * 4 + 1] = 255;
        if (hm && mt.px.size() == o.px.size()) o.px[k * 4 + 2] = mt.px[k * 4]; else if (!hm) o.px[k * 4 + 2] = 255;
      }
      mj["mr"] = emit(o, 1, "mr_x" + std::to_string(mi) + ".png");
    }
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
  if (simpBefore) summary += " (simplified " + std::to_string(simpBefore) + " -> " + std::to_string(simpAfter) + " tris)";
  if (spec.lod1 > 0 || !spec.lod1Src.empty()) {   // distance LOD: same placement, fewer triangles, small textures
    ImportSpec l = spec; l.simplify = spec.lod1Src.empty() ? spec.simplify * spec.lod1 : 1.0f; l.lod1 = 0; l.lod1Src.clear(); l.maxTex = std::min(spec.maxTex, spec.lod1Tex); l.meshEdits = spec.meshEdits;
    std::string e2, s2;
    l.lod2 = 0; l.maxTexBy.clear();
    if (!bakeGlbModel(spec.lod1Src.empty() ? glbPath : glbPath.parent_path() / spec.lod1Src, l, outDir / "lod1", e2, s2)) { err = "lod1: " + e2; return false; }
    summary += "; lod1 " + s2.substr(s2.find(':') + 2, s2.find(" tris") - s2.find(':') - 2) + " tris";
  }
  if (spec.lod2 > 0) {   // TK2: the far LOD
    ImportSpec l = spec; l.simplify = spec.simplify * spec.lod2; l.lod1 = 0; l.lod2 = 0; l.maxTex = std::min(spec.maxTex, spec.lod2Tex); l.maxTexBy.clear(); l.meshEdits = spec.meshEdits;
    std::string e2, s2;
    if (!bakeGlbModel(glbPath, l, outDir / "lod2", e2, s2)) { err = "lod2: " + e2; return false; }
    summary += "; lod2 " + s2.substr(s2.find(':') + 2, s2.find(" tris") - s2.find(':') - 2) + " tris";
  }
  return true;
}

// ------------------------------------------------------------------ runtime
// NT8: an alpha-tested (cutout) albedo: each mip's alpha scaled so the share of texels over the 0.5 cutoff stays level
// 0's (L3 stable alpha, as foliage.cpp leafMips): needles and grass blades do not thin out or shimmer with distance
static void cutoutMips(GLuint t, int w, int h, int levels, const stbi_uc* d) {
  std::vector<uint8_t> px(d, d + (size_t)w * h * 4);
  size_t n0 = (size_t)w * h; int cov0 = 0; for (size_t k = 0; k < n0; ++k) cov0 += px[k * 4 + 3] > 127;
  const float frac0 = (float)cov0 / n0;
  std::vector<float> raw(n0); for (size_t k = 0; k < n0; ++k) raw[k] = px[k * 4 + 3] / 255.0f;
  for (int L = 1; L < levels; ++L) {
    int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
    std::vector<uint8_t> q((size_t)w2 * h2 * 4); std::vector<float> r2((size_t)w2 * h2);
    for (int j = 0; j < h2; ++j) for (int i = 0; i < w2; ++i) {
      float c[4] = {0, 0, 0, 0};
      for (int e = 0; e < 4; ++e) { int ii = std::min(w - 1, i * 2 + (e & 1)), jj = std::min(h - 1, j * 2 + (e >> 1)); size_t k = (size_t)jj * w + ii;
        for (int m = 0; m < 3; ++m) c[m] += px[k * 4 + m]; c[3] += raw[k]; }
      size_t o = (size_t)j * w2 + i; for (int m = 0; m < 3; ++m) q[o * 4 + m] = (uint8_t)(c[m] * 0.25f + 0.5f); r2[o] = c[3] * 0.25f;
    }
    float lo = 1, hi = 16;
    for (int it = 0; it < 14; ++it) { float m = (lo + hi) * 0.5f; size_t n = 0; for (float a : r2) n += a * m > 0.5f; if ((float)n / r2.size() < frac0) lo = m; else hi = m; }
    float k = (lo + hi) * 0.5f;
    for (size_t o = 0; o < r2.size(); ++o) q[o * 4 + 3] = (uint8_t)std::min(255.0f, r2[o] * k * 255.0f + 0.5f);
    glTextureSubImage2D(t, L, 0, 0, w2, h2, GL_RGBA, GL_UNSIGNED_BYTE, q.data());
    px.swap(q); raw.swap(r2); w = w2; h = h2;
  }
}
// TK2: a map of 2048 px (MEM1: 1024) or more is block-compressed on load (CPU mips, stb_dxt): albedo BC1 sRGB (BC3 with alpha),
// metal-rough / AO BC1, normal BC5 (RG; the shader rebuilds z when the blue channel reads 0). A 4096 RGBA8 map with mips
// is 85 MB of VRAM, BC1 11 MB, BC5 22 MB. kind: 0 albedo, 1 linear data, 2 normal
// FOL1: a cutout albedo (cut) compresses too, BC3, its mips' alpha scaled to keep level 0's coverage at the 0.5 cutoff
// (as cutoutMips): the 2048 needle atlases cost less than the old 1024 RGBA8 ones
static int gBc = -1;   // s3tc + sRGB support: asked once on the GL thread (bcSupported)
static bool bcSupported() {
  if (gBc < 0) { gBc = 0; GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n); int got = 0;
    for (GLint i = 0; i < n; ++i) { const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i); if (!std::strcmp(e, "GL_EXT_texture_compression_s3tc")) got |= 1; if (!std::strcmp(e, "GL_EXT_texture_sRGB")) got |= 2; }
    gBc = got == 3; }
  return gBc == 1;
}
// CACHE1: the CPU half (no GL: worker threads may run it; threads: the row workers of one map)
static bool compressBC(const uint8_t* d, int w, int h, int kind, bool cut, std::vector<std::vector<uint8_t>>& lvOut, GLenum& fmtOut, unsigned threads) {
  if (!bcSupported() || w < 1024 || h < 1024 || (w & 3) || (h & 3)) return false;   // MEM1: 1024 maps too (RGBA8 + mips 5.6 MB each, BC1 0.7 MB): weapons, vehicles, props
  bool alpha = cut; if (kind == 0) for (size_t k = 0; k < (size_t)w * h && !alpha; ++k) alpha = d[k * 4 + 3] < 250;
  std::vector<float> raw; float frac0 = 0;   // cut: the unscaled alpha of the current level, level 0's coverage
  if (cut) { raw.resize((size_t)w * h); size_t c0 = 0; for (size_t k = 0; k < raw.size(); ++k) { raw[k] = d[k * 4 + 3] / 255.0f; c0 += d[k * 4 + 3] > 127; } frac0 = (float)c0 / raw.size(); }
  const GLenum fmt = kind == 2 ? 0x8DBD /* RG_RGTC2 */ : kind == 0 ? (alpha ? 0x8C4F /* SRGB_ALPHA_S3TC_DXT5 */ : 0x8C4C /* SRGB_S3TC_DXT1 */) : 0x83F0 /* RGB_S3TC_DXT1 */;
  const int bsz = kind == 2 || alpha ? 16 : 8;
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  std::vector<uint8_t> cur(d, d + (size_t)w * h * 4), next, out;
  unsigned nt = std::max(1u, threads);
  for (int L = 0, lw = w, lh = h; L < levels; ++L) {
    int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
    out.assign((size_t)bw * bh * bsz, 0);
    auto rows = [&](int b0, int b1) {
      uint8_t blk[64], rg[32];
      for (int by = b0; by < b1; ++by) for (int bx = 0; bx < bw; ++bx) {
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
          const uint8_t* s = &cur[((size_t)std::min(by * 4 + y, lh - 1) * lw + std::min(bx * 4 + x, lw - 1)) * 4];
          std::memcpy(blk + (y * 4 + x) * 4, s, 4); rg[(y * 4 + x) * 2] = s[0]; rg[(y * 4 + x) * 2 + 1] = s[1];
        }
        uint8_t* o = &out[((size_t)by * bw + bx) * bsz];
        if (kind == 2) stb_compress_bc5_block(o, rg); else stb_compress_dxt_block(o, blk, alpha ? 1 : 0, STB_DXT_NORMAL);
      }
    };
    std::vector<std::thread> th; int per = (bh + (int)nt - 1) / (int)nt;
    for (unsigned k = 0; k < nt; ++k) { int b0 = (int)k * per, b1 = std::min(bh, b0 + per); if (b0 < b1) th.emplace_back(rows, b0, b1); }
    for (auto& x : th) x.join();
    lvOut.push_back(out);
    if (L + 1 < levels) {   // the next mip: a 2x2 box
      int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2); next.assign((size_t)nw * nh * 4, 0);
      for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int c = 0; c < 4; ++c) {
        int x0 = std::min(2 * x, lw - 1), x1 = std::min(2 * x + 1, lw - 1), y0 = std::min(2 * y, lh - 1), y1 = std::min(2 * y + 1, lh - 1);
        next[((size_t)y * nw + x) * 4 + c] = (uint8_t)((cur[((size_t)y0 * lw + x0) * 4 + c] + cur[((size_t)y0 * lw + x1) * 4 + c] + cur[((size_t)y1 * lw + x0) * 4 + c] + cur[((size_t)y1 * lw + x1) * 4 + c] + 2) / 4);
      }
      if (cut) {   // coverage-preserving alpha (the box-filtered raw alpha, scaled so the share over 0.5 stays level 0's)
        std::vector<float> r2((size_t)nw * nh);
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) {
          int x0 = std::min(2 * x, lw - 1), x1 = std::min(2 * x + 1, lw - 1), y0 = std::min(2 * y, lh - 1), y1 = std::min(2 * y + 1, lh - 1);
          r2[(size_t)y * nw + x] = 0.25f * (raw[(size_t)y0 * lw + x0] + raw[(size_t)y0 * lw + x1] + raw[(size_t)y1 * lw + x0] + raw[(size_t)y1 * lw + x1]);
        }
        float lo = 1, hi = 16;
        for (int it = 0; it < 14; ++it) { float m = (lo + hi) * 0.5f; size_t n = 0; for (float a : r2) n += a * m > 0.5f; if ((float)n / r2.size() < frac0) lo = m; else hi = m; }
        const float k = (lo + hi) * 0.5f;
        for (size_t o = 0; o < r2.size(); ++o) next[o * 4 + 3] = (uint8_t)std::min(255.0f, r2[o] * k * 255.0f + 0.5f);
        raw.swap(r2);
      }
      cur.swap(next); lw = nw; lh = nh;
    }
  }
  fmtOut = fmt;
  return true;
}
static GLuint uploadBC(int w, int h, GLenum fmt, const std::vector<std::vector<uint8_t>>& lv, size_t& bytes) {
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  glTextureStorage2D(t, (GLsizei)lv.size(), fmt, w, h);
  for (int L = 0, lw = w, lh = h; L < (int)lv.size(); ++L, lw = std::max(1, lw / 2), lh = std::max(1, lh / 2)) { glCompressedTextureSubImage2D(t, L, 0, 0, lw, lh, fmt, (GLsizei)lv[L].size(), lv[L].data()); bytes += lv[L].size(); }
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
  return t;
}
static GLuint loadTexBC(const uint8_t* d, int w, int h, int kind, size_t& bytes, bool cut = false, std::vector<std::vector<uint8_t>>* keep = nullptr, GLenum* fmtOut = nullptr) {
  std::vector<std::vector<uint8_t>> lv; GLenum fmt = 0;
  if (!compressBC(d, w, h, kind, cut, lv, fmt, std::clamp(std::thread::hardware_concurrency(), 1u, 16u))) return 0;
  GLuint t = uploadBC(w, h, fmt, lv, bytes);
  if (fmtOut) *fmtOut = fmt;
  if (keep) *keep = std::move(lv);
  return t;
}

// MEM3: block-compressed model maps cached per user (%LOCALAPPDATA%/BattleFoundry/texbc): a hit skips the JPEG / PNG decode
// and the CPU compression (most of the weapons' and vehicles' startup time). Keyed by the source's path, size and time.
static fs::path texCacheFile(const fs::path& p, int kind) {
  const std::string st = userCacheContentStamp(p); if (st.empty()) return {};   // CACHE2: by content: every build with this source shares it
  std::string key = "bc2|" + st + "|" + std::to_string(kind);
  uint64_t x = 1469598103934665603ull; for (unsigned char c : key) { x ^= c; x *= 1099511628211ull; }
  fs::path dir = userCacheDir("texbc", "NMK_TEXBC_CACHE");
  char nm[32]; std::snprintf(nm, sizeof nm, "%016llx.bc", (unsigned long long)x);
  return dir / nm;
}
static void writeTexCache(const fs::path& cf, int w, int h, GLenum fmt, const std::vector<std::vector<uint8_t>>& lv) {   // via a temp name: a half-written file never reads as a hit
  std::error_code ec; fs::create_directories(cf.parent_path(), ec);
  fs::path tmp = cf; tmp += "." + std::to_string((unsigned long long)(uintptr_t)&lv ^ (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";   // unique per writer (threads, several game instances)
  { std::ofstream o(tmp, std::ios::binary); uint32_t hd[4] = {0x31434258u, (uint32_t)w, (uint32_t)h, (uint32_t)fmt}; o.write((const char*)hd, sizeof hd);
    for (auto& l : lv) { uint32_t n = (uint32_t)l.size(); o.write((const char*)&n, 4); o.write((const char*)l.data(), n); } }
  fs::rename(tmp, cf, ec);
}
static GLuint texFromCache(const fs::path& f, size_t& bytes) {
  std::ifstream in(f, std::ios::binary); if (!in) return 0;
  uint32_t hd[4]; if (!in.read((char*)hd, sizeof hd) || hd[0] != 0x31434258u) return 0;   // "XBC1", w, h, format
  int w = (int)hd[1], h = (int)hd[2]; GLenum fmt = hd[3];
  if (w < 4 || h < 4 || w > 16384 || h > 16384) return 0;
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  std::vector<std::vector<uint8_t>> lv(levels);
  for (int L = 0; L < levels; ++L) { uint32_t n = 0; if (!in.read((char*)&n, 4) || n > (64u << 20)) return 0; lv[L].resize(n); if (!in.read((char*)lv[L].data(), n)) return 0; }
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  glTextureStorage2D(t, levels, fmt, w, h);
  for (int L = 0, lw = w, lh = h; L < levels; ++L, lw = std::max(1, lw / 2), lh = std::max(1, lh / 2)) { glCompressedTextureSubImage2D(t, L, 0, 0, lw, lh, fmt, (GLsizei)lv[L].size(), lv[L].data()); bytes += lv[L].size(); }
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
  return t;
}

static GLuint loadTex(const fs::path& p, bool srgb, size_t& bytes, int kind = -1, bool cutout = false) {
  int w, h, c;
  fs::path cf = kind >= 0 || cutout ? texCacheFile(p, cutout ? 3 : kind) : fs::path();
  if (!cf.empty()) if (GLuint ct = texFromCache(cf, bytes)) { userCacheTouch(cf); return ct; }
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &c, 4);
  if (!d) return 0;
  std::vector<std::vector<uint8_t>> lv; GLenum fmt = 0;
  if (kind >= 0 || cutout) if (GLuint bt = loadTexBC(d, w, h, cutout ? 0 : kind, bytes, cutout, cf.empty() ? nullptr : &lv, &fmt)) {
    stbi_image_free(d);
    if (!lv.empty()) writeTexCache(cf, w, h, fmt, lv);
    return bt;
  }
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  glTextureStorage2D(t, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(t, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d);
  if (cutout) cutoutMips(t, w, h, levels, d); else glGenerateTextureMipmap(t);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
  stbi_image_free(d);
  bytes += (size_t)w * h * 4 * 4 / 3;
  return t;
}

// CACHE1: a cold cache: every block-compressed map of these baked models decoded and compressed in parallel (one map per
// worker) into the texture cache; their loads then read the cache. Callers with many models (weapons, vehicles) pass them
// all at once so the workers have enough to share.
void prefetchModelTextures(const std::vector<fs::path>& dirs) {
  struct Job { fs::path f, cf; int kind; bool cut; };
  std::vector<Job> jobs; std::set<std::string> seen;
  if (!bcSupported()) return;
  for (const fs::path& dir : dirs) {
    json j; try { j = json::parse(readTextFile(dir / "model.json")); } catch (...) { continue; }
    if (!j.is_object()) continue;
    auto want = [&](const json& mj, const char* key, bool srgb) {
      if (!mj.contains(key) || !mj[key].is_string()) return;
      fs::path full = dir / mj[key].get<std::string>(); if (!seen.insert(full.lexically_normal().string()).second) return;
      const bool cut = srgb && mj.value("cutout", false); const int kind = srgb ? 0 : std::string(key) == "normal" ? 2 : 1;
      fs::path cf = texCacheFile(full, cut ? 3 : kind); std::error_code ec;
      if (!cf.empty() && !fs::exists(cf, ec)) jobs.push_back({full, cf, kind, cut});
    };
    if (j.contains("materials")) { for (auto& mj : j["materials"]) for (auto [k, sr] : {std::pair{"albedo", true}, {"mr", false}, {"normal", false}, {"ao", false}}) want(mj, k, sr); }
    else if (j.contains("textures")) for (auto [k, sr] : {std::pair{"albedo", true}, {"mr", false}, {"normal", false}, {"ao", false}}) want(j["textures"], k, sr);
  }
  if (jobs.size() < 2) return;
  stbi_set_flip_vertically_on_load(0);
  std::atomic<size_t> next{0}; std::vector<std::thread> th;
  unsigned nt = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
  for (unsigned t = 0; t < nt && t < jobs.size(); ++t) th.emplace_back([&] {
    for (size_t i; (i = next++) < jobs.size();) {
      int w, h, c; stbi_uc* d = stbi_load(jobs[i].f.string().c_str(), &w, &h, &c, 4); if (!d) continue;
      std::vector<std::vector<uint8_t>> lv; GLenum fmt = 0;
      if (compressBC(d, w, h, jobs[i].kind, jobs[i].cut, lv, fmt, 1)) writeTexCache(jobs[i].cf, w, h, fmt, lv);
      stbi_image_free(d);
    }
  });
  for (auto& t : th) t.join();
  logf("textures: %zu model maps compressed into the cache on %u threads", jobs.size(), std::min<unsigned>(nt, (unsigned)jobs.size()));
}

void prefetchModelTree(const fs::path& root) {
  std::vector<fs::path> dirs; std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    if (it->path().filename() == "model.json") dirs.push_back(it->path().parent_path());
  prefetchModelTextures(dirs);
}
bool GpuModel::load(const fs::path& dir, std::string& err, bool turn180) {
  destroy();
  json j;
  try { j = json::parse(readTextFile(dir / "model.json")); } catch (...) { err = "missing or bad " + (dir / "model.json").string(); return false; }
  std::ifstream f(dir / "mesh.bin", std::ios::binary);
  uint32_t hdr[3] = {0, 0, 0};
  if (!f.read((char*)hdr, sizeof hdr) || hdr[0] != 0x31574D4Eu) { err = "missing or bad mesh.bin"; return false; }
  std::vector<TexVertex> V(hdr[1]); std::vector<uint32_t> I(hdr[2]);
  f.read((char*)V.data(), V.size() * sizeof(TexVertex)); f.read((char*)I.data(), I.size() * 4);
  if (!f) { err = "truncated mesh.bin"; return false; }
  auto turn = [&](vec3 v) { return turn180 ? vec3(-v.x, v.y, -v.z) : v; };
  if (turn180) for (auto& v : V) { v.pos = turn(v.pos); v.nrm = turn(v.nrm); v.tan = vec4(turn(vec3(v.tan)), v.tan.w); }
  if (keepPos) { cpuPos.resize(V.size()); for (size_t k = 0; k < V.size(); ++k) cpuPos[k] = V[k].pos; cpuIdx = I; }
  for (auto& pj : j["parts"]) {
    ModelPart p;
    p.name = pj.value("name", ""); p.role = pj.value("role", "body");
    p.first = pj.value("first", 0u); p.count = pj.value("count", 0u);
    auto& b = pj["base"]; p.base = vec4(b[0], b[1], b[2], b[3]);
    p.metal = pj.value("metal", 1.0f); p.rough = pj.value("rough", 1.0f); p.textured = pj.value("textured", false);
    p.bmin = jvec3(pj["bmin"]); p.bmax = jvec3(pj["bmax"]); p.section = pj.value("section", -1);
    if (turn180) { vec3 a0 = turn(p.bmin), a1 = turn(p.bmax); p.bmin = glm::min(a0, a1); p.bmax = glm::max(a0, a1); }
    p.mat = pj.value("mat", -1);
    if (p.mat >= 0 && j.contains("materials") && p.mat < (int)j["materials"].size()) {   // glass by its material's name (car windows, cockpit canopies)
      std::string mn = j["materials"][p.mat].value("name", std::string()); for (auto& ch : mn) ch = (char)std::tolower((unsigned char)ch);
      p.glass = mn.find("glass") != std::string::npos || mn.find("window") != std::string::npos || mn.find("windshield") != std::string::npos;
    }
    for (auto& a : pj.value("anim", json::array())) { mat4 m; for (int k = 0; k < 16; ++k) glm::value_ptr(m)[k] = a[k].get<float>(); p.anim.push_back(m); }
    parts.push_back(std::move(p));
  }
  animDuration = j.value("animDuration", 0.0f);
  bmin = jvec3(j["bmin"]); bmax = jvec3(j["bmax"]);
  if (turn180) { vec3 a0 = turn(bmin), a1 = turn(bmax); bmin = glm::min(a0, a1); bmax = glm::max(a0, a1); }
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
    crumb("model textures");   // UI5: the launch splash presents between texture loads
    const bool cut = srgb && mj.value("cutout", false);   // NT8: cutout albedo keeps its coverage mips (no BC: box mips thin the needles)
    GLuint t = loadTex(dir / f, srgb, gpuBytes, cut ? -1 : srgb ? 0 : std::string(key) == "normal" ? 2 : 1, cut); if (t) ++textureCount;
    return cache[f] = t;
  };
  prefetchModelTextures({dir});   // CACHE1: a cold cache fills in parallel (every map of this model)
  if (j.contains("materials")) {
    for (auto& mj : j["materials"]) {
      MatTex m; m.name = mj.value("name", ""); m.cutout = mj.value("cutout", false);
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
  for (auto& p : parts) p.cutout = p.mat >= 0 && p.mat < (int)mats.size() && mats[p.mat].cutout;   // NT8 foliage cards
  if (!mats.empty()) { texAlb = mats[0].alb; texMR = mats[0].mr; texN = mats[0].n; texAO = mats[0].ao; aoInMR = mats[0].aoInMR; }
  for (auto& m : mats) if (!texAlb && m.alb) texAlb = m.alb;
  return true;
}

void GpuModel::destroy() {
  if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ebo); }
  std::set<GLuint> ts;
  for (auto& m : mats) for (GLuint x : {m.alb, m.mr, m.n, m.ao}) if (x) ts.insert(x);
  for (GLuint x : ts) if (!borrowed.count(x)) glDeleteTextures(1, &x);
  borrowed.clear();
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

json furnitureModelFor(const std::string& id);   // assets.cpp (PR2)
bool importWeaponModels(const fs::path& srcDir, const fs::path& contentRoot) {
  std::map<std::string, json> done;   // output dir -> the model block that baked it
  int ok = 0, bad = 0;
  std::vector<fs::path> files;   // sorted: the directory order differs between file systems
  for (const char* sub : {"weapons", "assets"}) {   // first-person weapons, then map assets (wire, Flak, ...)
    std::vector<fs::path> fl; std::error_code ec0;
    for (auto& e : fs::directory_iterator(contentRoot / sub, ec0)) if (e.path().extension() == ".json") fl.push_back(e.path());
    std::sort(fl.begin(), fl.end()); files.insert(files.end(), fl.begin(), fl.end());
  }
  for (auto& f : files) {
    fs::path wdir = f.parent_path();
    json j; try { j = json::parse(readTextFile(f)); } catch (...) { continue; }
    if (j.is_object() && !j.contains("model")) { json fm = furnitureModelFor(j.value("id", f.stem().string())); if (!fm.is_null()) j["model"] = fm; }   // PR2 replaced furniture
    if (j.contains("models") && j["models"].is_array()) for (auto& mj : j["models"]) {   // PR1 attached modelled pieces: <dir>.glb, ground pivot, metres
      json b = mj.value("bake", json::object());
      std::string d = mj.value("dir", std::string());
      if (d.empty() || done.count(d)) continue;
      b["dir"] = d; if (!b.contains("src")) b["src"] = d + ".glb";
      if (!b.contains("pivot")) b["pivot"] = "ground";
      if (!b.contains("scale")) b["scale"] = 1;
      if (!b.contains("lod1")) b["lod1"] = 0.3;
      ImportSpec s = importSpecFromJson(b);
      fs::path glb = srcDir / s.src;
      if (!mj.contains("bake") || !fs::exists(glb)) continue;   // PR4: a plain reference (a prop cluster reusing a ph_* bake) neither bakes nor claims the folder
      done[d] = b;
      std::string err, sum;
      if (bakeGlbModel(glb, s, wdir / "models" / s.dir, err, sum)) { logf("import %-12s %s", s.dir.c_str(), sum.c_str()); ++ok; }
      else { logf("import %-12s FAILED: %s", s.dir.c_str(), err.c_str()); ++bad; }
    }
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
