// Skinned characters: GLB load (own minimal reader), compute-shader skinning, procedural pose (see character.h).
#include "character.h"
#include <stb_image.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>
GLuint modelUploadBC(const uint8_t* rgba, int w, int h, int kind, size_t& bytes, const uint8_t* enc, size_t len);   // model.cpp (TK2, FSB2 cache)
GLuint modelBCFromCache(const uint8_t* enc, size_t len, int kind, size_t& bytes);

namespace {
struct Glb { json j; std::vector<uint8_t> bin; };
bool readGlb(const fs::path& p, Glb& g, std::string& err) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { err = "cannot open " + p.string(); return false; }
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &b[o], 4); return v; };
  if (b.size() < 20 || u32(0) != 0x46546C67u || u32(4) != 2) { err = "not a glTF 2 GLB"; return false; }
  for (size_t o = 12; o + 8 <= b.size();) {
    uint32_t len = u32(o), type = u32(o + 4);
    if (o + 8 + len > b.size()) { err = "truncated"; return false; }
    if (type == 0x4E4F534Au) g.j = json::parse(b.begin() + o + 8, b.begin() + o + 8 + len);
    else if (type == 0x004E4942u) g.bin.assign(b.begin() + o + 8, b.begin() + o + 8 + len);
    o += 8 + len;
  }
  return !g.j.is_null();
}
std::vector<float> acc(const Glb& g, int idx, int& comps) {
  const json& a = g.j["accessors"][idx];
  static const std::map<std::string, int> nc = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
  comps = nc.at(a["type"].get<std::string>());
  size_t count = a["count"].get<size_t>(); int ct = a["componentType"].get<int>(); bool norm = a.value("normalized", false);
  std::vector<float> out(count * comps, 0.0f);
  if (!a.contains("bufferView")) return out;
  const json& bv = g.j["bufferViews"][a["bufferView"].get<int>()];
  size_t base = bv.value("byteOffset", (size_t)0) + a.value("byteOffset", (size_t)0);
  int csz = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
  size_t stride = bv.value("byteStride", (size_t)0); if (!stride) stride = (size_t)csz * comps;
  for (size_t e = 0; e < count; ++e)
    for (int c = 0; c < comps; ++c) {
      const uint8_t* p = &g.bin[base + e * stride + (size_t)c * csz]; float v = 0;
      switch (ct) {
        case 5126: std::memcpy(&v, p, 4); break;
        case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = (float)x; break; }
        case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = norm ? x / 65535.0f : (float)x; break; }
        case 5122: { int16_t x; std::memcpy(&x, p, 2); v = norm ? std::max(x / 32767.0f, -1.0f) : (float)x; break; }
        case 5121: { v = norm ? *p / 255.0f : (float)*p; break; }
        case 5120: { v = norm ? std::max((int8_t)*p / 127.0f, -1.0f) : (float)(int8_t)*p; break; }
      }
      out[e * comps + c] = v;
    }
  return out;
}
mat4 localOf(const json& n) {
  if (n.contains("matrix")) { mat4 m; for (int k = 0; k < 16; ++k) glm::value_ptr(m)[k] = n["matrix"][k].get<float>(); return m; }
  vec3 t(0), s(1); quat r(1, 0, 0, 0);
  if (n.contains("translation")) t = vec3(n["translation"][0], n["translation"][1], n["translation"][2]);
  if (n.contains("rotation")) r = quat(n["rotation"][3].get<float>(), n["rotation"][0].get<float>(), n["rotation"][1].get<float>(), n["rotation"][2].get<float>());
  if (n.contains("scale")) s = vec3(n["scale"][0], n["scale"][1], n["scale"][2]);
  return glm::translate(mat4(1), t) * glm::mat4_cast(r) * glm::scale(mat4(1), s);
}
// bc: block-compress a map of 1024 px or more (kind 0 albedo BC1 / BC3, 1 data BC1, 2 normal BC5): the soldier skins; the
// first-person rigs (seen at arm's length) keep RGBA8
GLuint uploadImage(const Glb& g, int imageIdx, bool srgb, bool bc, int kind, bool bcBig = false) {
  const json& im = g.j["images"][imageIdx];
  if (!im.contains("bufferView")) return 0;
  const json& bv = g.j["bufferViews"][im["bufferView"].get<int>()];
  size_t off = bv.value("byteOffset", (size_t)0), len = bv["byteLength"].get<size_t>();
  const uint8_t* enc = g.bin.data() + off;
  if (bc || bcBig) { size_t bytes = 0; if (GLuint b = modelBCFromCache(enc, len, kind, bytes)) return b; }   // FSB2: compressed once, reused (FR3: only big rig images were ever cached)
  int w, h, c;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* d = stbi_load_from_memory(enc, (int)len, &w, &h, &c, 4);
  if (!d) return 0;
  if (!bc && bcBig && std::max(w, h) > 2048) bc = true;   // FR3 rigbc
  if (bc) { size_t bytes = 0; if (GLuint b = modelUploadBC(d, w, h, kind, bytes, enc, len)) { stbi_image_free(d); return b; } }   // FSB1: 2048 RGBA8 + mips 21 MB, BC1 2.7 MB, BC5 5.3 MB
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  glTextureStorage2D(t, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(t, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d);
  glGenerateTextureMipmap(t);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameterf(t, 0x84FE, 4.0f);
  stbi_image_free(d);
  return t;
}
std::string semanticKey(const std::string& raw) {
  std::string n = raw;
  size_t c = n.rfind(':'); if (c != std::string::npos) n = n.substr(c + 1);
  size_t u = n.rfind('_');
  if (u != std::string::npos && u + 1 < n.size() && std::all_of(n.begin() + u + 1, n.end(), ::isdigit)) n = n.substr(0, u);
  return n;
}
// G2 zombie (Sketchfab export, two numeric suffixes per name: hip_016_12): strip them all, then map the rig's names
std::string aliasKey(const std::string& raw) {
  std::string n = raw;
  for (size_t u; (u = n.rfind('_')) != std::string::npos && u + 1 < n.size() && std::all_of(n.begin() + u + 1, n.end(), ::isdigit);) n = n.substr(0, u);
  static const std::map<std::string, std::string> al = {{"hip", "Hips"}, {"spine01", "Spine"}, {"chest", "Spine2"}, {"neck", "Neck"}, {"head", "Head"},
    {"shoulder_L", "LeftShoulder"}, {"upper_arm_L", "LeftArm"}, {"forearm_L", "LeftForeArm"}, {"hand_L", "LeftHand"},
    {"shoulder_R", "RightShoulder"}, {"upper_arm_R", "RightArm"}, {"forearm_R", "RightForeArm"}, {"hand_R", "RightHand"},
    {"thigh_L", "LeftUpLeg"}, {"shin_L", "LeftLeg"}, {"foot_L", "LeftFoot"}, {"toe_L", "LeftToeBase"},
    {"thigh_R", "RightUpLeg"}, {"shin_R", "RightLeg"}, {"foot_R", "RightFoot"}, {"toe_R", "RightToeBase"}};
  auto it = al.find(n); return it == al.end() ? std::string() : it->second;
}
const char* kSemanticNames[CJ_COUNT] = {"Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
  "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm", "RightForeArm", "RightHand",
  "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"};

GLuint gSkinProg = 0; GLint gSkinCount = -1, gSkinKeep = -1;
const char* kSkinCS = R"(#version 450
layout(local_size_x = 64) in;
struct V { vec4 pos; vec4 nrm; vec4 uv; uvec4 j; vec4 w; };
layout(std430, binding = 0) readonly buffer Rest { V rv[]; };
layout(std430, binding = 1) readonly buffer Joints { mat4 J[]; };
layout(std430, binding = 2) buffer Out { float o[]; };
layout(std430, binding = 4) writeonly buffer PrevOut { vec4 po[]; };   // AIB1: each vertex's previous-frame position (TAA motion)
uniform uint uCount; uniform int uKeepPrev;   // 1: the position in Out is last frame's (copied out before it is overwritten)
void main() {
  uint i = gl_GlobalInvocationID.x; if (i >= uCount) return;
  V v = rv[i];
  mat4 M = J[v.j.x] * v.w.x + J[v.j.y] * v.w.y + J[v.j.z] * v.w.z + J[v.j.w] * v.w.w;
  vec3 p = (M * vec4(v.pos.xyz, 1.0)).xyz;
  vec3 n = normalize(mat3(M) * v.nrm.xyz);
  uint b = i * 12u;
  po[i] = vec4(uKeepPrev == 1 ? vec3(o[b], o[b + 1u], o[b + 2u]) : p, 1.0);
  o[b] = p.x; o[b + 1u] = p.y; o[b + 2u] = p.z;
  o[b + 3u] = n.x; o[b + 4u] = n.y; o[b + 5u] = n.z;
  o[b + 6u] = 1.0; o[b + 7u] = 0.0; o[b + 8u] = 0.0; o[b + 9u] = 1.0;
  o[b + 10u] = v.uv.x; o[b + 11u] = v.uv.y;
})";
}

bool initSkinning(std::string& err) {
  if (gSkinProg) return true;
  GLuint s = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(s, 1, &kSkinCS, nullptr); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, nullptr, log); err = log; glDeleteShader(s); return false; }
  gSkinProg = glCreateProgram(); glAttachShader(gSkinProg, s); glLinkProgram(gSkinProg); glDeleteShader(s);
  glGetProgramiv(gSkinProg, GL_LINK_STATUS, &ok);
  if (!ok) { err = "skinning program link failed"; return false; }
  gSkinCount = glGetUniformLocation(gSkinProg, "uCount"); gSkinKeep = glGetUniformLocation(gSkinProg, "uKeepPrev");
  return true;
}

bool CharModel::load(const fs::path& path, std::string& err, float height, bool body) {
  Glb g;
  if (!readGlb(path, g, err)) return false;
  id = path.stem().string();
  const json& J = g.j;
  if (!J.contains("skins") || J["skins"].empty()) { err = "no skin in " + path.string(); return false; }
  size_t nn = J["nodes"].size();
  nodeName.assign(nn, ""); nodeParent.assign(nn, -1); nodeLocal.assign(nn, mat4(1)); nodeWorld.assign(nn, mat4(1));
  for (size_t k = 0; k < nn; ++k) {
    const json& n = J["nodes"][k];
    nodeName[k] = n.value("name", ""); nodeLocal[k] = localOf(n);
    if (n.contains("children")) for (auto& c : n["children"]) nodeParent[c.get<int>()] = (int)k;
  }
  std::function<mat4(int)> world = [&](int k) -> mat4 { return nodeParent[k] < 0 ? rootFix * nodeLocal[k] : world(nodeParent[k]) * nodeLocal[k]; };
  for (size_t k = 0; k < nn; ++k) nodeWorld[k] = world((int)k);
  std::vector<int> depth(nn, 0); nodeOrder.clear();
  for (size_t k = 0; k < nn; ++k) { for (int p = nodeParent[k]; p >= 0; p = nodeParent[p]) ++depth[k]; nodeOrder.push_back((int)k); }
  std::stable_sort(nodeOrder.begin(), nodeOrder.end(), [&](int a, int b) { return depth[a] < depth[b]; });
  const json& sk = J["skins"][0];
  for (auto& x : sk["joints"]) jointNode.push_back(x.get<int>());
  int nj = (int)jointNode.size();
  invBind.assign(nj, mat4(1));
  if (sk.contains("inverseBindMatrices")) { int c; auto f = acc(g, sk["inverseBindMatrices"].get<int>(), c); for (int k = 0; k < nj; ++k) std::memcpy(glm::value_ptr(invBind[k]), &f[k * 16], 64); }
  jointParent.assign(nj, -1); jointHead.assign(nj, vec3(0));
  std::map<int, int> nodeToJoint; for (int k = 0; k < nj; ++k) nodeToJoint[jointNode[k]] = k;
  for (int k = 0; k < nj; ++k) {
    for (int p = nodeParent[jointNode[k]]; p >= 0; p = nodeParent[p]) if (nodeToJoint.count(p)) { jointParent[k] = nodeToJoint[p]; break; }
    jointHead[k] = vec3(nodeWorld[jointNode[k]][3]);
  }
  for (int s = 0; s < CJ_COUNT; ++s) { semantic[s] = -1; for (int k = 0; k < nj; ++k) if (semanticKey(nodeName[jointNode[k]]) == kSemanticNames[s] || aliasKey(nodeName[jointNode[k]]) == kSemanticNames[s]) { semantic[s] = k; break; } }
  if (semantic[CJ_SPINE1] < 0) semantic[CJ_SPINE1] = semantic[CJ_SPINE2] >= 0 ? semantic[CJ_SPINE2] : semantic[CJ_SPINE];   // a two-bone spine (zombie)
  if (body) for (int s : {CJ_HIPS, CJ_SPINE2, CJ_HEAD, CJ_LARM, CJ_LFOREARM, CJ_LHAND, CJ_RARM, CJ_RFOREARM, CJ_RHAND, CJ_LUPLEG, CJ_LLEG, CJ_LFOOT, CJ_RUPLEG, CJ_RLEG, CJ_RFOOT})
    if (semantic[s] < 0) { err = std::string("skeleton has no ") + kSemanticNames[s] + " joint"; return false; }
  // ---- geometry: every mesh primitive (skinned: model space; unskinned: node world, bound to its nearest joint)
  std::vector<uint32_t> I;
  bmin = vec3(1e9f); bmax = vec3(-1e9f);
  // materials -> textures
  std::map<int, GLuint> texCache;
  const bool noBC = J.value("asset", json::object()).value("extras", json::object()).value("nmkNoBC", false);   // BC4: a file that asks keeps its textures uncompressed (RGBA8 + mips), as its source shows them
  auto tex = [&](int t, bool srgb, int kind = -1) -> GLuint {   // kind: 0 albedo, 1 data, 2 normal (-1: by srgb)
    if (t < 0) return 0;
    if (kind < 0) kind = srgb ? 0 : 1;
    int key = t * 4 + kind;
    auto it = texCache.find(key); if (it != texCache.end()) return it->second;
    int src = J["textures"][t].value("source", -1);
    return texCache[key] = src >= 0 ? uploadImage(g, src, srgb, !wantNormals && !noBC, kind, bcBig && !noBC) : 0;   // FSB1: soldier skins block-compressed
  };
  if (J.contains("materials")) for (auto& m : J["materials"]) {
    GpuModel::MatTex mt; mt.name = m.value("name", "");
    const json pbr = m.value("pbrMetallicRoughness", json::object());
    if (pbr.contains("baseColorTexture")) mt.alb = tex(pbr["baseColorTexture"]["index"].get<int>(), true);
    if (pbr.contains("metallicRoughnessTexture")) mt.mr = tex(pbr["metallicRoughnessTexture"]["index"].get<int>(), false);
    if ((wantNormals || J.value("asset", json::object()).value("extras", json::object()).value("nmkNormals", false)) && m.contains("normalTexture")) mt.n = tex(m["normalTexture"]["index"].get<int>(), false, 2);   // FSB1: a soldier skin baked with its normal maps (bake_gunbody.py --skin SKIN_KEEP_NORMALS)
    mt.cutout = J.value("asset", json::object()).value("extras", json::object()).value("nmkCutout", false) && m.value("alphaMode", std::string("OPAQUE")) == "MASK";   // BC2: alpha-tested hair cards and lashes, for a file that asks (asset.extras.nmkCutout)
    gpu.mats.push_back(mt);
  }
  for (size_t ni = 0; ni < nn; ++ni) {
    const json& n = J["nodes"][ni];
    if (!n.contains("mesh")) continue;
    bool skinned = n.contains("skin");
    int bindJoint = 0;
    if (!skinned) for (int p = (int)ni; p >= 0; p = nodeParent[p]) if (nodeToJoint.count(p)) { bindJoint = nodeToJoint[p]; break; }
    for (auto& pr : J["meshes"][n["mesh"].get<int>()]["primitives"]) {
      if (pr.value("mode", 4) != 4 || !pr.contains("indices")) continue;
      const json& at = pr["attributes"];
      int c3, c2, cj, cw;
      auto P = acc(g, at["POSITION"].get<int>(), c3);
      std::vector<float> N, UV, JN, WT;
      if (at.contains("NORMAL")) N = acc(g, at["NORMAL"].get<int>(), c3);
      if (at.contains("TEXCOORD_0")) UV = acc(g, at["TEXCOORD_0"].get<int>(), c2);
      if (skinned && at.contains("JOINTS_0") && at.contains("WEIGHTS_0")) { JN = acc(g, at["JOINTS_0"].get<int>(), cj); WT = acc(g, at["WEIGHTS_0"].get<int>(), cw); }
      size_t base = verts.size(), nv = P.size() / 3;
      mat4 W = skinned ? mat4(1) : nodeWorld[ni];
      // unskinned geometry follows its joint: store it in rest model space, weight 1 on bindJoint
      for (size_t e = 0; e < nv; ++e) {
        SkinVert v{};
        v.pos = vec3(W * vec4(P[e * 3], P[e * 3 + 1], P[e * 3 + 2], 1));
        v.nrm = N.empty() ? vec3(0, 1, 0) : glm::normalize(glm::mat3(W) * vec3(N[e * 3], N[e * 3 + 1], N[e * 3 + 2]));
        v.uv = UV.empty() ? vec2(0) : vec2(UV[e * 2], UV[e * 2 + 1]);
        if (!JN.empty()) {
          float ws = 0; for (int q = 0; q < 4; ++q) { v.j[q] = (uint32_t)JN[e * 4 + q]; v.w[q] = WT[e * 4 + q]; ws += v.w[q]; }
          if (ws > 1e-6f) v.w /= ws; else { v.w = vec4(1, 0, 0, 0); v.j[0] = 0; }
          for (int q = 0; q < 4; ++q) if (v.j[q] >= (uint32_t)nj) { v.j[q] = 0; v.w[q] = 0; }
        } else { v.j[0] = (uint32_t)bindJoint; v.w = vec4(1, 0, 0, 0); }
        bmin = glm::min(bmin, v.pos); bmax = glm::max(bmax, v.pos);
        verts.push_back(v);
      }
      int c; auto idx = acc(g, pr["indices"].get<int>(), c);
      ModelPart part; part.name = nodeName[ni]; part.first = (uint32_t)I.size(); part.count = (uint32_t)idx.size();
      for (float f : idx) I.push_back((uint32_t)(base + (size_t)f));
      int mi = pr.value("material", -1);
      part.mat = mi;
      if (mi >= 0) {
        const json& m = J["materials"][mi]; const json pbr = m.value("pbrMetallicRoughness", json::object());
        if (pbr.contains("baseColorFactor")) { auto& b = pbr["baseColorFactor"]; part.base = vec4(b[0], b[1], b[2], b[3]); }
        part.metal = pbr.value("metallicFactor", 1.0f); part.rough = pbr.value("roughnessFactor", 1.0f);
        part.textured = gpu.mats[mi].alb != 0; part.cutout = gpu.mats[mi].cutout;
        if (!gpu.mats[mi].mr) { part.metal = std::min(part.metal, 0.05f); part.rough = std::max(part.rough, 0.75f); }   // cloth default
        if (auto ef = m.find("emissiveFactor"); ef != m.end() && ef->is_array() && ef->size() == 3) {   // RB6: a glowing part (the KR-714's eyes)
          const vec3 e((*ef)[0].get<float>(), (*ef)[1].get<float>(), (*ef)[2].get<float>());
          float k = 1; if (m.contains("extensions") && m["extensions"].contains("KHR_materials_emissive_strength")) k = m["extensions"]["KHR_materials_emissive_strength"].value("emissiveStrength", 1.0f);
          if (e != vec3(1)) part.emis = glm::max(e, vec3(0)) * std::clamp(k, 0.0f, 100.0f);   // an exactly white factor is an exporter's default (the guns' flash sprites, stray RootNode materials in 30+ character files): no glow
        }
      }
      part.bmin = bmin; part.bmax = bmax;
      gpu.parts.push_back(part);
    }
  }
  if (verts.empty()) { err = "no geometry"; return false; }
  if (height > 0) {
    // an unbaked source (G2 zombie): scale the rest pose to the height, feet at 0, centred. The skinned vertices move
    // into that model space (bind pose seen from the root joint) and the inverse binds compensate, so skin = world * invBind
    // still holds; rootFix sits above the root nodes, so clips that animate the hierarchy keep it.
    std::vector<mat4> S(nj); for (int k = 0; k < nj; ++k) S[k] = nodeWorld[jointNode[k]] * invBind[k];
    vec3 lo(1e9f), hi(-1e9f);
    for (auto& v : verts) { mat4 M(0.0f); for (int q = 0; q < 4; ++q) if (v.w[q] > 0) M += S[v.j[q]] * v.w[q]; vec3 p = vec3(M * vec4(v.pos, 1)); lo = glm::min(lo, p); hi = glm::max(hi, p); }
    float sc = height / std::max(1e-4f, hi.y - lo.y);
    rootFix = glm::scale(mat4(1), vec3(sc)) * glm::translate(mat4(1), -vec3((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f));
    mat4 A = rootFix * S[0], Ai = glm::inverse(A);
    bmin = vec3(1e9f); bmax = vec3(-1e9f);
    for (auto& v : verts) { v.pos = vec3(A * vec4(v.pos, 1)); v.nrm = glm::normalize(glm::mat3(A) * v.nrm); bmin = glm::min(bmin, v.pos); bmax = glm::max(bmax, v.pos); }
    for (auto& ib : invBind) ib = ib * Ai;
    for (size_t k = 0; k < nn; ++k) nodeWorld[k] = world((int)k);
    for (int k = 0; k < nj; ++k) jointHead[k] = vec3(nodeWorld[jointNode[k]][3]);
    for (auto& p : gpu.parts) { p.bmin = bmin; p.bmax = bmax; }
  }
  // AN1 step 2: classify the finger joints by name (Mixamo Thumb / Index / Middle / Ring / Pinky, Biped and NPC FingerN digits, Finger_A..D)
  finger.assign(nj, -1); fingerSeg.assign(nj, 0); fingerAx.assign(nj, vec3(0, 0, 1));
  auto fingerOf = [&](int k) -> int {
    std::string n = nodeName[jointNode[k]]; for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    if (n.find("palm") != std::string::npos || n.find("metacarpal") != std::string::npos) return -1;
    if (n.find("thumb") != std::string::npos) return 0;
    if (n.find("index") != std::string::npos) return 1;
    if (n.find("middle") != std::string::npos) return 2;
    if (n.find("ring") != std::string::npos) return 3;
    if (n.find("pinky") != std::string::npos || n.find("little") != std::string::npos) return 4;
    size_t f = n.rfind("finger"); if (f == std::string::npos) return -1;
    f += 6; if (f < n.size() && n[f] == '_') ++f;
    if (f >= n.size()) return -1;
    if (n[f] >= '0' && n[f] <= '4') return n[f] - '0';
    if (n[f] >= 'a' && n[f] <= 'd') return 1 + (n[f] - 'a');
    return -1;
  };
  // rest palm normals from the joints (not every rig's rest palm faces down): fingers f from the hand to the knuckles, thumb side t;
  // a right palm faces cross(t, f), a left one cross(f, t)
  int handOfJ[2] = {semantic[CJ_LHAND], semantic[CJ_RHAND]};
  auto handOfK = [&](int k) { for (int a = jointParent[k], d = 0; a >= 0 && d < 5; a = jointParent[a], ++d) { if (a == handOfJ[0]) return 0; if (a == handOfJ[1]) return 1; } return -1; };
  for (int h = 0; h < 2 && handOfJ[0] >= 0 && handOfJ[1] >= 0; ++h) {
    vec3 base[5]; bool has[5] = {};
    for (int k = 0; k < nj; ++k) if (handOfK(k) == h) { int f = fingerOf(k); if (f >= 0 && !has[f] && (jointParent[k] < 0 || fingerOf(jointParent[k]) != f)) { base[f] = jointHead[k]; has[f] = true; } }
    int outer = has[4] ? 4 : has[3] ? 3 : has[2] ? 2 : -1;
    if (!has[1] || outer < 0) continue;
    vec3 hp = jointHead[handOfJ[h]], f = glm::normalize(0.5f * (base[1] + base[outer]) - hp);
    fingerDir[h] = f;
    vec3 t = has[0] ? base[0] - hp : base[1] - base[outer];
    t -= f * glm::dot(t, f);
    if (glm::length(t) < 1e-5f) continue;
    vec3 n = h ? glm::cross(t, f) : glm::cross(f, t);
    if (glm::length(n) > 1e-6f) palmN[h] = glm::normalize(n);
    // a rig with pre-curled fingers (the robot's claws): the side the fingers curl toward is the palm, whatever the thumb says
    vec3 bend(0); float curlAng = 0; int nc = 0;
    for (int k = 0; k < nj; ++k) if (handOfK(k) == h && fingerOf(k) >= 1) {
      int c = -1; for (int q = 0; q < nj; ++q) if (jointParent[q] == k && fingerOf(q) == fingerOf(k)) { c = q; break; }
      int g = -1; if (c >= 0) for (int q = 0; q < nj; ++q) if (jointParent[q] == c && fingerOf(q) == fingerOf(k)) { g = q; break; }
      if (c < 0 || g < 0) continue;
      vec3 b1 = glm::normalize(jointHead[c] - jointHead[k]), b2 = glm::normalize(jointHead[g] - jointHead[c]);
      vec3 perp = b2 - b1 * glm::dot(b2, b1); bend += perp; curlAng += std::acos(std::clamp(glm::dot(b1, b2), -1.0f, 1.0f)); ++nc;
    }
    if (glm::length(bend) > 1e-4f && glm::dot(palmN[h], bend) < 0) palmN[h] = -palmN[h];   // the thumb rule's sign follows the fingers' rest curl (Biped, trooper rigs)
    if (nc > 0 && curlAng / nc > 0.2f && glm::length(bend) > 1e-4f) {
      vec3 nb = bend - f * glm::dot(bend, f);
      if (glm::length(nb) > 1e-4f) palmN[h] = glm::normalize(nb);
    }
  }
  for (int k = 0; k < nj; ++k) {
    bool underHand = false;
    for (int a = jointParent[k], d = 0; a >= 0 && d < 5; a = jointParent[a], ++d) if (a == semantic[CJ_LHAND] || a == semantic[CJ_RHAND]) { underHand = semantic[CJ_LHAND] >= 0; break; }
    if (!underHand) continue;
    int f = fingerOf(k); if (f < 0) continue;
    int seg = 1; for (int a = jointParent[k]; a >= 0 && fingerOf(a) == f; a = jointParent[a]) ++seg;
    finger[k] = (signed char)f; fingerSeg[k] = (signed char)std::min(seg, 4);
    vec3 bone(0); for (int c = 0; c < nj; ++c) if (jointParent[c] == k) { bone = jointHead[c] - jointHead[k]; break; }
    if (glm::length(bone) < 1e-5f && jointParent[k] >= 0) bone = jointHead[k] - jointHead[jointParent[k]];
    int hk = handOfK(k);
    vec3 ax = glm::cross(bone, palmN[hk < 0 ? 0 : hk]);   // + curls toward the palm
    if (glm::length(ax) > 1e-6f) fingerAx[k] = glm::normalize(ax);
  }
  // AN1 step 2b: finger chains (base -> tip), each base's rest spread from the middle finger, the pad radius
  fingerNext.assign(nj, -1); fingerTip.assign(nj, vec3(0)); fingerSpread.assign(nj, 0.0f);
  for (int k = 0; k < nj; ++k) if (finger[k] >= 0) for (int c = 0; c < nj; ++c) if (jointParent[c] == k && finger[c] == finger[k]) { fingerNext[k] = c; break; }
  for (int k = 0; k < nj; ++k) {
    if (finger[k] < 0 || fingerSeg[k] != 1) continue;
    int a = k, prev = -1; while (fingerNext[a] >= 0) { prev = a; a = fingerNext[a]; }
    fingerTip[k] = prev >= 0 && fingerSeg[a] >= 4 ? jointHead[a] : prev >= 0 ? jointHead[a] + (jointHead[a] - jointHead[prev]) * 0.8f : jointHead[a];
  }
  for (int h = 0; h < 2 && handOfJ[0] >= 0; ++h) {
    int mid = -1; for (int k = 0; k < nj; ++k) if (handOfK(k) == h && fingerSeg[k] == 1 && finger[k] == 2) mid = k;
    if (mid < 0) for (int k = 0; k < nj; ++k) if (handOfK(k) == h && fingerSeg[k] == 1 && finger[k] == 1) mid = k;
    if (mid < 0) continue;
    vec3 n = palmN[h], hp = jointHead[handOfJ[h]];
    auto inPlane = [&](vec3 v) { v -= n * glm::dot(v, n); return glm::length(v) > 1e-6f ? glm::normalize(v) : v; };
    vec3 md = inPlane(fingerTip[mid] - jointHead[mid]);
    for (int k = 0; k < nj; ++k) if (handOfK(k) == h && fingerSeg[k] == 1 && finger[k] >= 1) {
      vec3 fd = inPlane(fingerTip[k] - jointHead[k]);
      fingerSpread[k] = std::atan2(glm::dot(glm::cross(md, fd), n), glm::dot(md, fd));
    }
    if (h == 1) { fingerR = std::clamp(0.1f * glm::length(jointHead[mid] - hp), 0.006f, 0.02f); handScale = glm::length(jointHead[mid] - hp) / 0.09f; }
  }
  // AN1 step 2d: hands whose fingers are modelled closed and never weighted to the finger bones (the Rangers): where the fist's centre
  // sits from the wrist, so the hold puts the grip through it (the finger solve cannot show on them)
  if (semantic[CJ_LHAND] >= 0 && semantic[CJ_RHAND] >= 0 && !finger.empty()) {
    int onFingers = 0;
    for (auto& v : verts) for (int i = 0; i < 4; ++i) if (v.w[i] > 0.3f && (int)v.j[i] < nj && finger[v.j[i]] >= 0) { ++onFingers; break; }
    if (onFingers == 0) {
      fingerless = true;
      for (int h = 0; h < 2; ++h) {
        int hj = semantic[h ? CJ_RHAND : CJ_LHAND]; vec3 c(0); int n = 0;
        for (auto& v : verts) { int best = -1; float bw = 0; for (int i = 0; i < 4; ++i) if (v.w[i] > bw) { bw = v.w[i]; best = (int)v.j[i]; } if (best == hj && bw > 0.5f) { c += v.pos; ++n; } }
        if (n < 10 || fingerDir[h] == vec3(0)) continue;
        vec3 d = c / (float)n - jointHead[hj];
        fistF[h] = glm::dot(d, fingerDir[h]); fistN[h] = glm::dot(d, palmN[h]);
      }
      logf("character %s: modelled closed hands (no finger weights): fist centre L %.3f / %.3f, R %.3f / %.3f m (along the fingers / the palm normal)", id.c_str(), fistF[0], fistN[0], fistF[1], fistN[1]);
    }
  }
  // AN1 step 2b: per rig, the palm's thickness (hand-weighted vertices: how far the palm side reaches past the hand's bone line, 95th
  // percentile) and the finger pad radius (finger-weighted vertices: median distance from their joint's bone)
  if (semantic[CJ_LHAND] >= 0 && semantic[CJ_RHAND] >= 0 && !finger.empty()) {
    std::vector<float> pads;
    for (int h = 0; h < 2; ++h) {
      int hj = semantic[h ? CJ_RHAND : CJ_LHAND]; std::vector<float> pr;
      for (auto& v : verts) {
        int best = -1; float bw = 0; for (int i = 0; i < 4; ++i) if (v.w[i] > bw) { bw = v.w[i]; best = (int)v.j[i]; }
        if (best < 0 || bw < 0.5f) continue;
        if (best == hj) pr.push_back(glm::dot(v.pos - jointHead[hj], palmN[h]));
        else if (best < nj && finger[best] >= 1 && fingerNext[best] >= 0) {
          vec3 a = jointHead[best], b = jointHead[fingerNext[best]], ab = b - a; float t = std::clamp(glm::dot(v.pos - a, ab) / std::max(1e-8f, glm::dot(ab, ab)), 0.0f, 1.0f);
          pads.push_back(glm::length(v.pos - (a + ab * t)));
        }
      }
      if (pr.size() > 20) { std::sort(pr.begin(), pr.end()); palmThick[h] = std::clamp(pr[pr.size() * 80 / 100], 0.012f, 0.02f); }   // a palm's skin sits 1-2 cm off its bone line   // sleeves / cuffs weighted to the hand inflate the top end
    }
    if (pads.size() > 20) { std::nth_element(pads.begin(), pads.begin() + pads.size() / 2, pads.end()); fingerR = std::clamp(pads[pads.size() / 2], 0.006f, 0.016f); }
    logf("character %s: palm thickness L %.3f R %.3f, finger pad %.3f m", id.c_str(), palmThick[0], palmThick[1], fingerR);
  }
  // AN1 step 2b: per-rig head pitch fix (the robot commando's long head hangs at rest; raise it to just above level)
  { static const std::map<std::string, float> kHeadFix = {{"robot_commando", 0.35f}}; auto it = kHeadFix.find(id); if (it != kHeadFix.end()) headPitchFix = it->second; }
  // GPU: shared index buffer + rest vertices (SSBO for skinning; a bind-pose VBO for the shared vao)
  std::vector<TexVertex> tv(verts.size());
  for (size_t k = 0; k < verts.size(); ++k) tv[k] = {verts[k].pos, verts[k].nrm, vec4(1, 0, 0, 1), verts[k].uv};
  glCreateVertexArrays(1, &gpu.vao); glCreateBuffers(1, &gpu.vbo); glCreateBuffers(1, &gpu.ebo); glCreateBuffers(1, &restSsbo);
  glNamedBufferStorage(gpu.vbo, tv.size() * sizeof(TexVertex), tv.data(), 0);
  glNamedBufferStorage(gpu.ebo, I.size() * 4, I.data(), 0);
  indices = I;   // gore: wound placement on the posed mesh
  glNamedBufferStorage(restSsbo, verts.size() * sizeof(SkinVert), verts.data(), 0);
  glVertexArrayVertexBuffer(gpu.vao, 0, gpu.vbo, 0, sizeof(TexVertex));
  glVertexArrayElementBuffer(gpu.vao, gpu.ebo);
  auto attr = [&](GLuint vao, int loc, int n, size_t off) { glEnableVertexArrayAttrib(vao, loc); glVertexArrayAttribFormat(vao, loc, n, GL_FLOAT, GL_FALSE, (GLuint)off); glVertexArrayAttribBinding(vao, loc, 0); };
  attr(gpu.vao, 0, 3, offsetof(TexVertex, pos)); attr(gpu.vao, 1, 3, offsetof(TexVertex, nrm)); attr(gpu.vao, 2, 4, offsetof(TexVertex, tan)); attr(gpu.vao, 3, 2, offsetof(TexVertex, uv));
  gpu.bmin = bmin; gpu.bmax = bmax; gpu.tris = (int)I.size() / 3;
  clipsFromGltf(J, [&](int a) { int c; return acc(g, a, c); }, clips);
  analyzeClips(*this);
  logf("character %s: %zu verts, %d tris, %d joints, %zu materials, %zu clips", id.c_str(), verts.size(), gpu.tris, nj, gpu.mats.size(), clips.size());
  return true;
}

void CharModel::destroy() {
  gpu.destroy();
  if (restSsbo) glDeleteBuffers(1, &restSsbo);
  restSsbo = 0; verts.clear();
}

bool CharInstance::create(const CharModel& m) {
  model = &m;
  view.mats = m.gpu.mats; view.parts = m.gpu.parts; view.ebo = m.gpu.ebo; view.bmin = m.gpu.bmin; view.bmax = m.gpu.bmax; view.tris = m.gpu.tris;
  glCreateVertexArrays(1, &view.vao); glCreateBuffers(1, &view.vbo); glCreateBuffers(1, &jointSsbo); glCreateBuffers(1, &prevBuf);
  glNamedBufferStorage(view.vbo, m.verts.size() * sizeof(TexVertex), nullptr, 0);
  glNamedBufferStorage(jointSsbo, std::max(1, m.joints()) * sizeof(mat4), nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(prevBuf, std::max<size_t>(1, m.verts.size()) * sizeof(vec4), nullptr, 0);   // AIB1: 16 B a vertex
  glVertexArrayVertexBuffer(view.vao, 0, view.vbo, 0, sizeof(TexVertex));
  glVertexArrayElementBuffer(view.vao, view.ebo);
  auto attr = [&](int loc, int n, size_t off) { glEnableVertexArrayAttrib(view.vao, loc); glVertexArrayAttribFormat(view.vao, loc, n, GL_FLOAT, GL_FALSE, (GLuint)off); glVertexArrayAttribBinding(view.vao, loc, 0); };
  attr(0, 3, offsetof(TexVertex, pos)); attr(1, 3, offsetof(TexVertex, nrm)); attr(2, 4, offsetof(TexVertex, tan)); attr(3, 2, offsetof(TexVertex, uv));
  glVertexArrayVertexBuffer(view.vao, 1, prevBuf, 0, sizeof(vec4));   // AIB1: aPrev (kModelVS location 4)
  glEnableVertexArrayAttrib(view.vao, 4); glVertexArrayAttribFormat(view.vao, 4, 3, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(view.vao, 4, 1);
  lastFrame = -10;
  return true;
}

void CharInstance::destroy() {
  if (view.vao) glDeleteVertexArrays(1, &view.vao);
  if (view.vbo) glDeleteBuffers(1, &view.vbo);
  if (jointSsbo) glDeleteBuffers(1, &jointSsbo);
  if (prevBuf) glDeleteBuffers(1, &prevBuf);
  prevBuf = 0;
  view.vao = view.vbo = 0; jointSsbo = 0; view.ebo = 0; view.mats.clear(); view.parts.clear(); model = nullptr;
}

void CharInstance::skin(const std::vector<mat4>& S, int frame) {
  if (!model || !gSkinProg || S.empty()) return;
  const size_t nb = std::min<size_t>(S.size(), model->joints()) * sizeof(mat4);
  glNamedBufferSubData(jointSsbo, 0, nb, S.data());
  // AIB1: the previous position is last frame's pose only when that was skinned the frame before; after a gap (far, skinned every
  // few frames) or the first skin: this pose (no limb motion, the body's own motion still comes from the previous model matrix)
  const bool cont = frame >= 0 && lastFrame == frame - 1;
  prevValid = cont; lastFrame = frame;
  glUseProgram(gSkinProg);
  glUniform1ui(gSkinCount, (GLuint)model->verts.size()); glUniform1i(gSkinKeep, prevValid ? 1 : 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, model->restSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, jointSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, view.vbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, prevBuf);
  glDispatchCompute((GLuint)((model->verts.size() + 63) / 64), 1, 1);
  glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT);
  glUseProgram(0);
}
