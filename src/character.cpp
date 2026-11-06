// Skinned characters: GLB load (own minimal reader), compute-shader skinning, procedural pose (see character.h).
#include "character.h"
#include <stb_image.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

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
GLuint uploadImage(const Glb& g, int imageIdx, bool srgb) {
  const json& im = g.j["images"][imageIdx];
  if (!im.contains("bufferView")) return 0;
  const json& bv = g.j["bufferViews"][im["bufferView"].get<int>()];
  size_t off = bv.value("byteOffset", (size_t)0), len = bv["byteLength"].get<size_t>();
  int w, h, c;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* d = stbi_load_from_memory(g.bin.data() + off, (int)len, &w, &h, &c, 4);
  if (!d) return 0;
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
const char* kSemanticNames[CJ_COUNT] = {"Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
  "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm", "RightForeArm", "RightHand",
  "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"};

GLuint gSkinProg = 0; GLint gSkinCount = -1;
const char* kSkinCS = R"(#version 450
layout(local_size_x = 64) in;
struct V { vec4 pos; vec4 nrm; vec4 uv; uvec4 j; vec4 w; };
layout(std430, binding = 0) readonly buffer Rest { V rv[]; };
layout(std430, binding = 1) readonly buffer Joints { mat4 J[]; };
layout(std430, binding = 2) writeonly buffer Out { float o[]; };
uniform uint uCount;
void main() {
  uint i = gl_GlobalInvocationID.x; if (i >= uCount) return;
  V v = rv[i];
  mat4 M = J[v.j.x] * v.w.x + J[v.j.y] * v.w.y + J[v.j.z] * v.w.z + J[v.j.w] * v.w.w;
  vec3 p = (M * vec4(v.pos.xyz, 1.0)).xyz;
  vec3 n = normalize(mat3(M) * v.nrm.xyz);
  uint b = i * 12u;
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
  gSkinCount = glGetUniformLocation(gSkinProg, "uCount");
  return true;
}

bool CharModel::load(const fs::path& path, std::string& err) {
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
  std::function<mat4(int)> world = [&](int k) -> mat4 { return nodeParent[k] < 0 ? nodeLocal[k] : world(nodeParent[k]) * nodeLocal[k]; };
  for (size_t k = 0; k < nn; ++k) nodeWorld[k] = world((int)k);
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
  for (int s = 0; s < CJ_COUNT; ++s) { semantic[s] = -1; for (int k = 0; k < nj; ++k) if (semanticKey(nodeName[jointNode[k]]) == kSemanticNames[s]) { semantic[s] = k; break; } }
  for (int s : {CJ_HIPS, CJ_SPINE2, CJ_HEAD, CJ_LARM, CJ_LFOREARM, CJ_LHAND, CJ_RARM, CJ_RFOREARM, CJ_RHAND, CJ_LUPLEG, CJ_LLEG, CJ_LFOOT, CJ_RUPLEG, CJ_RLEG, CJ_RFOOT})
    if (semantic[s] < 0) { err = std::string("skeleton has no ") + kSemanticNames[s] + " joint"; return false; }
  // ---- geometry: every mesh primitive (skinned: model space; unskinned: node world, bound to its nearest joint)
  std::vector<uint32_t> I;
  bmin = vec3(1e9f); bmax = vec3(-1e9f);
  // materials -> textures
  std::map<int, GLuint> texCache;
  auto tex = [&](int t, bool srgb) -> GLuint {
    if (t < 0) return 0;
    int key = t * 2 + (srgb ? 1 : 0);
    auto it = texCache.find(key); if (it != texCache.end()) return it->second;
    int src = J["textures"][t].value("source", -1);
    return texCache[key] = src >= 0 ? uploadImage(g, src, srgb) : 0;
  };
  if (J.contains("materials")) for (auto& m : J["materials"]) {
    GpuModel::MatTex mt; mt.name = m.value("name", "");
    const json pbr = m.value("pbrMetallicRoughness", json::object());
    if (pbr.contains("baseColorTexture")) mt.alb = tex(pbr["baseColorTexture"]["index"].get<int>(), true);
    if (pbr.contains("metallicRoughnessTexture")) mt.mr = tex(pbr["metallicRoughnessTexture"]["index"].get<int>(), false);
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
        part.textured = gpu.mats[mi].alb != 0;
        if (!gpu.mats[mi].mr) { part.metal = std::min(part.metal, 0.05f); part.rough = std::max(part.rough, 0.75f); }   // cloth default
      }
      part.bmin = bmin; part.bmax = bmax;
      gpu.parts.push_back(part);
    }
  }
  if (verts.empty()) { err = "no geometry"; return false; }
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
  logf("character %s: %zu verts, %d tris, %d joints, %zu materials", id.c_str(), verts.size(), gpu.tris, nj, gpu.mats.size());
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
  glCreateVertexArrays(1, &view.vao); glCreateBuffers(1, &view.vbo); glCreateBuffers(1, &jointSsbo);
  glNamedBufferStorage(view.vbo, m.verts.size() * sizeof(TexVertex), nullptr, 0);
  glNamedBufferStorage(jointSsbo, std::max(1, m.joints()) * sizeof(mat4), nullptr, GL_DYNAMIC_STORAGE_BIT);
  glVertexArrayVertexBuffer(view.vao, 0, view.vbo, 0, sizeof(TexVertex));
  glVertexArrayElementBuffer(view.vao, view.ebo);
  auto attr = [&](int loc, int n, size_t off) { glEnableVertexArrayAttrib(view.vao, loc); glVertexArrayAttribFormat(view.vao, loc, n, GL_FLOAT, GL_FALSE, (GLuint)off); glVertexArrayAttribBinding(view.vao, loc, 0); };
  attr(0, 3, offsetof(TexVertex, pos)); attr(1, 3, offsetof(TexVertex, nrm)); attr(2, 4, offsetof(TexVertex, tan)); attr(3, 2, offsetof(TexVertex, uv));
  return true;
}

void CharInstance::destroy() {
  if (view.vao) glDeleteVertexArrays(1, &view.vao);
  if (view.vbo) glDeleteBuffers(1, &view.vbo);
  if (jointSsbo) glDeleteBuffers(1, &jointSsbo);
  view.vao = view.vbo = 0; jointSsbo = 0; view.ebo = 0; view.mats.clear(); view.parts.clear(); model = nullptr;
}

void CharInstance::skin(const std::vector<mat4>& S) {
  if (!model || !gSkinProg || S.empty()) return;
  glNamedBufferSubData(jointSsbo, 0, std::min<size_t>(S.size(), model->joints()) * sizeof(mat4), S.data());
  glUseProgram(gSkinProg);
  glUniform1ui(gSkinCount, (GLuint)model->verts.size());
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, model->restSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, jointSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, view.vbo);
  glDispatchCompute((GLuint)((model->verts.size() + 63) / 64), 1, 1);
  glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT);
  glUseProgram(0);
}
