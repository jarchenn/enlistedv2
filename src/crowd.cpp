// GC1 / GC2 GPU crowd (crowd.h). GC1 drawing. Bake: each type's procedural poses (poseCharacter, the held gun's sockets) sampled once
// per clip into palettes, its mesh simplified per part to two LODs; frame: the units culled (last frame's view, sphere) and
// bucketed by type x LOD by projected height (LOD0 over 40 px, LOD1 down to 1.5 px, none below), uploaded once, drawn instanced.
#include "crowd.h"
#include "game.h"
#include "app.h"
#include "settings.h"
#include "simplify.h"
#include "frustum.h"
#include <map>
#include <unordered_map>
#include <queue>


bool crowdBake(CrowdType& t, const CharModel& m, const WeaponDef* w, std::string& err) {
  t = CrowdType{}; t.m = &m; t.J = m.joints(); t.gun = w ? w->worldModel() : nullptr;
  if (t.J <= 0 || m.verts.empty() || m.indices.empty()) { err = "no skin"; return false; }
  // ---- poses: clip -> frames of PoseParams
  struct C { int n; bool loop; float period; };
  const C cl[CC_COUNT] = {{1, true, 1}, {16, true, 16 / 15.0f}, {12, true, 12 / 15.0f}, {16, true, 16 / 15.0f}, {1, true, 1}, {4, true, 4 / 15.0f}, {12, false, 12 / 15.0f}, {1, true, 1}};
  PoseParams base; base.holdWeapon = w != nullptr;
  if (w) { base.gripR = w->gripR; base.gripL = w->gripL; base.magPos = w->gripL + vec3(0, -0.1f, -0.12f); base.holdOffset = w->holdOffset; base.holdWeld = w->holdWeld; base.holdKind = w->holdKind; base.leftHold = w->leftHold;
           base.gripC = w->gripC; base.gripAx = w->gripAx; base.gripRad = w->gripRad; base.guardC = w->guardC; base.guardRad = w->guardRad; base.sightR = w->rear; }
  std::vector<mat4> P; PoseResult pr;
  for (int c = 0; c < CC_COUNT; ++c) {
    t.clip[c] = glm::ivec4(t.frames, cl[c].n, cl[c].loop ? 1 : 0, 0); t.dur[c] = cl[c].period;
    for (int f = 0; f < cl[c].n; ++f) {
      PoseParams pp = base; const float u = (float)f / cl[c].n;
      switch (c) {
        case CC_WALK: pp.walkAmount = 0.5f; pp.walkPhase = 6.2831853f * u; break;
        case CC_RUN: pp.walkAmount = 1.0f; pp.walkPhase = 6.2831853f * u; pp.lowReady = 0.2f; break;
        case CC_CROUCH: pp.walkAmount = 0.4f; pp.walkPhase = 6.2831853f * u; pp.crouch = 1; break;
        case CC_PRONE: pp.prone = 1; break;
        case CC_FIRE: pp.recoil = 1.0f - u; break;
        case CC_DIE: pp.death = (float)f / std::max(1, cl[c].n - 1); break;
        case CC_DEAD: pp.death = 1; break;
        default: break;
      }
      poseCharacter(m, pp, pr);
      for (int j = 0; j < t.J; ++j) P.push_back(j < (int)pr.skin.size() ? pr.skin[j] : mat4(1));
      P.push_back(pr.weapon);
      ++t.frames;
    }
  }
  glCreateBuffers(1, &t.pal); glNamedBufferStorage(t.pal, P.size() * sizeof(mat4), P.data(), 0); t.bytes += P.size() * sizeof(mat4);
  // ---- LODs: each part simplified on its own (its material range kept); a corner keeps its source vertex's joints and weights
  const int target[2] = {3000, 300};
  const float total = (float)std::max(1, (int)m.indices.size() / 3);
  for (int L = 0; L < 2; ++L) {
    CrowdType::Lod& lod = t.lod[L]; std::vector<CharModel::SkinVert> rv; std::vector<uint32_t> ri;
    for (const ModelPart& pt : m.gpu.parts) {
      if (pt.role == "hidden" || pt.count < 3) continue;
      std::unordered_map<uint32_t, uint32_t> loc; std::vector<TexVertex> tv; std::vector<uint32_t> ti;
      for (uint32_t k = pt.first; k < pt.first + pt.count && k < m.indices.size(); ++k) {
        const uint32_t s = m.indices[k]; auto it = loc.find(s);
        if (it == loc.end()) { it = loc.emplace(s, (uint32_t)tv.size()).first; const auto& sv = m.verts[s]; tv.push_back({sv.pos, sv.nrm, vec4((float)s, 0, 0, 1), sv.uv}); }   // tan.x: the source vertex (exact below 2^24)
        ti.push_back(it->second);
      }
      const size_t want = std::max<size_t>(4, (size_t)std::lround(target[L] * (pt.count / 3) / total));
      simplifyMesh(tv, ti, want);
      ModelPart np = pt; np.first = (uint32_t)ri.size(); const uint32_t b0 = (uint32_t)rv.size();
      for (const TexVertex& v : tv) { CharModel::SkinVert sv = m.verts[(size_t)v.tan.x]; sv.pos = v.pos; rv.push_back(sv); }
      for (uint32_t i : ti) ri.push_back(b0 + i);
      np.count = (uint32_t)ri.size() - np.first; if (np.count) lod.parts.push_back(np);
    }
    lod.verts = (int)rv.size(); lod.tris = (int)ri.size() / 3;
    if (rv.empty()) { err = "empty LOD"; return false; }
    glCreateBuffers(1, &lod.rest); glNamedBufferStorage(lod.rest, rv.size() * sizeof(CharModel::SkinVert), rv.data(), 0);
    glCreateBuffers(1, &lod.ebo); glNamedBufferStorage(lod.ebo, ri.size() * 4, ri.data(), 0);
    glCreateVertexArrays(1, &lod.vao); glVertexArrayElementBuffer(lod.vao, lod.ebo);
    t.bytes += rv.size() * sizeof(CharModel::SkinVert) + ri.size() * 4;
  }
  logf("crowd: %s baked: %d frames x %d joints, LOD0 %d tris / %d verts, LOD1 %d tris / %d verts, %.1f MB", m.id.c_str(), t.frames, t.J, t.lod[0].tris, t.lod[0].verts, t.lod[1].tris, t.lod[1].verts, t.bytes / 1048576.0);
  return true;
}

void crowdDestroy(CrowdType& t) {
  if (t.pal) glDeleteBuffers(1, &t.pal);
  for (auto& l : t.lod) { if (l.rest) glDeleteBuffers(1, &l.rest); if (l.ebo) glDeleteBuffers(1, &l.ebo); if (l.vao) glDeleteVertexArrays(1, &l.vao); }
  t = CrowdType{};
}

// ---------------------------------------------------------------- GC2: units on the GPU
namespace {
constexpr int kCap = 131072, kMaxTypes = 8, kSlots = 8;   // units; body x gun types (buckets = types x 2 LODs); units a hash cell holds
struct Grid { vec2 org{0}; float cell = 2; int nx = 0, nz = 0; std::vector<float> h; std::vector<uint8_t> walk; };
struct GpuCrowd {
  std::vector<int> typeKey;                 // type index -> key (model * 4096 + gun + 1)
  std::map<int, CrowdType> types;           // by key
  int n = 0;                                // units spawned
  std::vector<CrowdUnit> stageU; std::vector<CrowdSim> stageS; int stageFrom = 0;   // spawns not yet uploaded
  GLuint units = 0, sim = 0, cnt = 0, idx = 0, height = 0, costBuf = 0, vis = 0, visCnt = 0, cmd = 0, cmdBucket = 0;
  GLuint progHash = 0, progMove = 0, progCull = 0, progCmd = 0;
  Grid g; bool gridOk = false; int nCmd = 0; std::vector<int> cmdFirstOf;   // per bucket: its first command
  bool costDirty[2] = {false, false}; std::vector<float> cost[2]; double lastT = -1;
} C;

const char* kCrowdCommon = R"(#version 450
layout(local_size_x = 128) in;
struct U { vec4 pos; vec4 prev; vec4 anim; };
struct S { vec4 a; ivec4 b; };
layout(std430, binding = 0) buffer Units { U u[]; };
layout(std430, binding = 1) buffer Sims { S sm[]; };
layout(std430, binding = 2) buffer Cnt { uint cnt[]; };
layout(std430, binding = 3) buffer Idx { uint idx[]; };
layout(std430, binding = 4) readonly buffer Height { float hgt[]; };
layout(std430, binding = 5) readonly buffer Cost { float cst[]; };   // [team][cell]
uniform int uN, uNx, uNz; uniform vec2 uOrg; uniform float uCell, uDt;
ivec2 cellOf(vec2 p) { return clamp(ivec2(floor((p - uOrg) / uCell)), ivec2(0), ivec2(uNx - 1, uNz - 1)); }
float H(vec2 p) {   // bilinear terrain height
  vec2 g = (p - uOrg) / uCell - 0.5; ivec2 c = clamp(ivec2(floor(g)), ivec2(0), ivec2(uNx - 2, uNz - 2)); vec2 f = clamp(g - vec2(c), 0.0, 1.0);
  float a = hgt[c.y * uNx + c.x], b = hgt[c.y * uNx + c.x + 1], d = hgt[(c.y + 1) * uNx + c.x], e = hgt[(c.y + 1) * uNx + c.x + 1];
  return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}
float K(int team, ivec2 c) { c = clamp(c, ivec2(0), ivec2(uNx - 1, uNz - 1)); return cst[team * uNx * uNz + c.y * uNx + c.x]; }
)";
const char* kHashCS = R"(
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN) || sm[i].b.z == 2) return;
  ivec2 c = cellOf(u[i].pos.xz); uint ci = uint(c.y * uNx + c.x); uint k = atomicAdd(cnt[ci], 1u); if (k < 8u) idx[ci * 8u + k] = i; })";
const char* kMoveCS = R"(
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN)) return;
  U x = u[i]; S s = sm[i]; x.prev = x.pos;
  if (s.b.z != 0) { x.anim.y += uDt; if (s.b.z == 1 && x.anim.y > 0.8) { s.b.z = 2; x.anim.x = 7.0; x.anim.y = 0.0; } u[i] = x; sm[i] = s; return; }   // dying -> dead
  vec2 p = x.pos.xz; ivec2 c = cellOf(p); int t = clamp(s.b.x, 0, 1);
  // down the flow field: the cheapest of the 8 neighbours (unwalkable = huge), blended with the cost gradient
  float k0 = K(t, c); vec2 best = vec2(0); float bk = k0;
  for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) { if (dx == 0 && dz == 0) continue; float k = K(t, c + ivec2(dx, dz)); if (k < bk) { bk = k; best = normalize(vec2(dx, dz)); } }
  vec2 want = k0 < 6.0 || bk >= k0 ? vec2(0) : best;   // at the goal (or boxed in): hold
  // separation from the 3 x 3 cells' units
  vec2 push = vec2(0);
  for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
    ivec2 q = c + ivec2(dx, dz); if (q.x < 0 || q.y < 0 || q.x >= uNx || q.y >= uNz) continue; uint ci = uint(q.y * uNx + q.x), m = min(cnt[ci], 8u);
    for (uint k = 0u; k < m; ++k) { uint j = idx[ci * 8u + k]; if (j == i) continue; vec2 d = p - u[j].pos.xz; float l = length(d); if (l > 1e-3 && l < 0.9) push += d / l * (0.9 - l); }
  }
  vec2 v = s.a.xy, tv = want * s.a.w + push * 2.5;
  v += (tv - v) * clamp(uDt * 4.0, 0.0, 1.0);
  float sp = length(v); if (sp > s.a.w * 1.3) { v *= s.a.w * 1.3 / sp; sp = s.a.w * 1.3; }
  p += v * uDt; ivec2 nc = cellOf(p); if (K(t, nc) > 1e8) { p = x.pos.xz; v = vec2(0); sp = 0.0; }   // never onto an unwalkable cell
  x.pos.xz = p; x.pos.y = H(p);
  if (sp > 0.2) { float yw = atan(-v.x, -v.y); float d = yw - x.pos.w; d = mod(d + 3.14159265, 6.2831853) - 3.14159265; x.pos.w += d * clamp(uDt * 6.0, 0.0, 1.0); }
  float clip = sp > 2.6 ? 2.0 : sp > 0.35 ? 1.0 : 0.0; if (clip != x.anim.x && x.anim.x <= 2.0) x.anim.x = clip;
  x.anim.y += uDt * (clip == 1.0 ? sp / 1.4 : clip == 2.0 ? sp / 4.0 : 1.0);
  s.a.xy = v; u[i] = x; sm[i] = s; })";
const char* kCullCS = R"(
layout(std430, binding = 6) buffer Vis { uint vis[]; };
layout(std430, binding = 7) buffer VisCnt { uint vcnt[]; };
uniform vec4 uPl[6]; uniform vec3 uEye; uniform float uK, uCapB;
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN)) return;
  vec3 c = u[i].pos.xyz + vec3(0, 0.9, 0);
  for (int k = 0; k < 6; ++k) if (dot(uPl[k].xyz, c) + uPl[k].w < -1.3) return;
  float px = uK * u[i].anim.w / max(0.5, length(c - uEye)); if (px < 1.5) return;
  uint b = uint(sm[i].b.y) * 2u + (px > 40.0 ? 0u : 1u); uint k = atomicAdd(vcnt[b], 1u); if (k < uint(uCapB)) vis[b * uint(uCapB) + k] = i; })";
const char* kCmdCS = R"(#version 450
layout(local_size_x = 64) in;
struct Cmd { uint count, inst, first, baseV, baseI; };
layout(std430, binding = 7) readonly buffer VisCnt { uint vcnt[]; };
layout(std430, binding = 8) buffer Cmds { Cmd cmd[]; };
layout(std430, binding = 9) readonly buffer CmdB { uint cb[]; };
uniform int uNCmd; uniform uint uCapB;
void main() { uint k = gl_GlobalInvocationID.x; if (k >= uint(uNCmd)) return; cmd[k].inst = min(vcnt[cb[k]], uCapB); })";

GLuint compileCS(const std::string& src, const char* name) {
  GLuint s = glCreateShader(GL_COMPUTE_SHADER); const char* p = src.c_str(); glShaderSource(s, 1, &p, nullptr); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("crowd: %s: %s", name, log); glDeleteShader(s); return 0; }
  GLuint pr = glCreateProgram(); glAttachShader(pr, s); glLinkProgram(pr); glDeleteShader(s); glGetProgramiv(pr, GL_LINK_STATUS, &ok);
  if (!ok) { logf("crowd: %s link failed", name); glDeleteProgram(pr); return 0; }
  return pr;
}
GLuint ssbo(size_t bytes, const void* data = nullptr) { GLuint b; glCreateBuffers(1, &b); glNamedBufferStorage(b, std::max<size_t>(bytes, 16), data, GL_DYNAMIC_STORAGE_BIT); return b; }
constexpr int kCapB = kCap;   // a bucket's visible list (worst case: every unit one type and LOD)

// the walkable 2 m grid over the build area: the navmesh where it is baked (a polygon within 1.5 m of the ground), else slope
bool buildGrid(Game& g) {
  const Terrain& T = g.app.terrain; Grid& G = C.g;
  const float hx = T.buildHalfX, hz = T.buildHalfZ; G.cell = std::max(2.0f, std::max(hx, hz) * 2.0f / 1024.0f);
  G.nx = std::max(2, (int)std::ceil(2 * hx / G.cell)); G.nz = std::max(2, (int)std::ceil(2 * hz / G.cell)); G.org = vec2(-hx, -hz);
  G.h.assign((size_t)G.nx * G.nz, 0); G.walk.assign((size_t)G.nx * G.nz, 1);
  for (int z = 0; z < G.nz; ++z) for (int x = 0; x < G.nx; ++x) { const float px = G.org.x + (x + 0.5f) * G.cell, pz = G.org.y + (z + 0.5f) * G.cell; G.h[(size_t)z * G.nx + x] = T.height(px, pz); }
  const bool nav = g.nav.valid(); int walk = 0;
  for (int z = 0; z < G.nz; ++z) for (int x = 0; x < G.nx; ++x) {
    const size_t c = (size_t)z * G.nx + x; const float px = G.org.x + (x + 0.5f) * G.cell, pz = G.org.y + (z + 0.5f) * G.cell, h = G.h[c];
    bool ok = h > T.waterLevel + 0.3f;
    if (ok && nav) { NavRef r = 0; vec3 on; ok = g.nav.closest(vec3(px, h, pz), vec3(G.cell * 0.6f, 1.5f, G.cell * 0.6f), r, on) && std::fabs(on.y - h) < 1.5f; }
    else if (ok) { const float sx = x + 1 < G.nx ? std::fabs(G.h[c + 1] - h) : 0, sz = z + 1 < G.nz ? std::fabs(G.h[c + G.nx] - h) : 0; ok = std::max(sx, sz) < G.cell * 0.7f; }
    G.walk[c] = ok; walk += ok;
  }
  logf("crowd: grid %d x %d cells of %.1f m (%s), %d walkable", G.nx, G.nz, G.cell, nav ? "navmesh" : "slope", walk);
  return walk > 0;
}
// Dijkstra over 8 neighbours from the goal's cell (or the walkable cell nearest it); unwalkable 1e9
void buildCost(int team, vec3 goal) {
  Grid& G = C.g; const int N = G.nx * G.nz; std::vector<float>& d = C.cost[team]; d.assign(N, 1e9f);
  int gx = std::clamp((int)((goal.x - G.org.x) / G.cell), 0, G.nx - 1), gz = std::clamp((int)((goal.z - G.org.y) / G.cell), 0, G.nz - 1);
  if (!G.walk[(size_t)gz * G.nx + gx]) { int best = -1; float bd = 1e30f; for (int c = 0; c < N; ++c) if (G.walk[c]) { float dx = (float)(c % G.nx - gx), dz = (float)(c / G.nx - gz), e = dx * dx + dz * dz; if (e < bd) { bd = e; best = c; } } if (best < 0) return; gx = best % G.nx; gz = best / G.nx; }
  using Q = std::pair<float, int>; std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
  d[(size_t)gz * G.nx + gx] = 0; q.push({0.0f, gz * G.nx + gx});
  while (!q.empty()) {
    auto [dc, c] = q.top(); q.pop(); if (dc > d[c]) continue;
    const int cx = c % G.nx, cz = c / G.nx;
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dz) continue; const int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= G.nx || z >= G.nz) continue;
      const int n = z * G.nx + x; if (!G.walk[n]) continue;
      if (dx && dz && (!G.walk[cz * G.nx + x] || !G.walk[z * G.nx + cx])) continue;   // no corner cutting
      const float nd = dc + G.cell * (dx && dz ? 1.41421356f : 1.0f) * (1.0f + 0.5f * std::fabs(G.h[n] - G.h[c]) / G.cell);   // climbs cost more
      if (nd < d[n]) { d[n] = nd; q.push({nd, n}); }
    }
  }
  C.costDirty[team] = true;
}
bool ensureGpu(Game& g) {
  if (C.units) return C.progMove != 0;
  if (!C.gridOk) C.gridOk = buildGrid(g);
  const std::string com = kCrowdCommon;
  C.progHash = compileCS(com + kHashCS, "hash"); C.progMove = compileCS(com + kMoveCS, "move"); C.progCull = compileCS(com + kCullCS, "cull"); C.progCmd = compileCS(kCmdCS, "commands");
  const size_t cells = (size_t)C.g.nx * C.g.nz;
  C.units = ssbo(kCap * sizeof(CrowdUnit)); C.sim = ssbo(kCap * sizeof(CrowdSim)); C.cnt = ssbo(cells * 4); C.idx = ssbo(cells * kSlots * 4);
  C.height = ssbo(cells * 4, C.g.h.data()); C.costBuf = ssbo(cells * 2 * 4);
  C.vis = ssbo((size_t)kMaxTypes * 2 * kCapB * 4); C.visCnt = ssbo(kMaxTypes * 2 * 4);
  for (int t = 0; t < 2; ++t) if (C.cost[t].empty()) { C.cost[t].assign(cells, 0.0f); for (size_t c = 0; c < cells; ++c) if (!C.g.walk[c]) C.cost[t][c] = 1e9f; C.costDirty[t] = true; }   // no goal yet: stand
  logf("crowd: GPU buffers %.1f MB for %d units", (kCap * (sizeof(CrowdUnit) + sizeof(CrowdSim)) + cells * (4 + kSlots * 4 + 4 + 8) + (size_t)kMaxTypes * 2 * kCapB * 4) / 1048576.0, kCap);
  return C.progHash && C.progMove && C.progCull && C.progCmd;
}
int typeIndex(Game& g, int model, int gun) {
  const int key = model * 4096 + std::max(-1, gun) + 1;
  for (int k = 0; k < (int)C.typeKey.size(); ++k) if (C.typeKey[k] == key) return k;
  if ((int)C.typeKey.size() >= kMaxTypes || model < 0 || model >= (int)g.models.size()) return -1;
  std::string err; CrowdType& t = C.types[key];
  if (!crowdBake(t, *g.models[model], gun >= 0 && gun < (int)g.app.weaponDefs.size() ? g.app.weaponDefs[gun].get() : nullptr, err)) { logf("crowd: type %d: %s", model, err.c_str()); C.types.erase(key); return -1; }
  C.typeKey.push_back(key);
  // indirect commands: per type x LOD, the body's parts then the gun's
  std::vector<uint32_t> cmds, cb; C.cmdFirstOf.clear(); C.nCmd = 0;
  for (int ti = 0; ti < (int)C.typeKey.size(); ++ti) for (int L = 0; L < 2; ++L) {
    const CrowdType& ct = C.types[C.typeKey[ti]]; const int b = ti * 2 + L;
    C.cmdFirstOf.push_back(C.nCmd);
    for (const ModelPart& p : ct.lod[L].parts) { uint32_t c[5] = {p.count, 0, p.first, 0, 0}; cmds.insert(cmds.end(), c, c + 5); cb.push_back(b); ++C.nCmd; }
    C.cmdFirstOf.push_back(C.nCmd);
    if (ct.gun) for (const ModelPart& p : ct.gun->parts) { uint32_t c[5] = {p.count, 0, p.first, 0, 0}; cmds.insert(cmds.end(), c, c + 5); cb.push_back(b); ++C.nCmd; }
  }
  if (C.cmd) { glDeleteBuffers(1, &C.cmd); glDeleteBuffers(1, &C.cmdBucket); }
  glCreateBuffers(1, &C.cmd); glNamedBufferStorage(C.cmd, std::max<size_t>(16, cmds.size() * 4), cmds.data(), 0);
  C.cmdBucket = ssbo(cb.size() * 4, cb.data());
  return (int)C.typeKey.size() - 1;
}
void testSource(Game& g) {   // NMK_CROWD=n: two armies of n / 2 on their halves of the build area, each walking to the other's
  static const int n = std::getenv("NMK_CROWD") ? std::atoi(std::getenv("NMK_CROWD")) : 0;
  if (n <= 0 || C.n > 0) return;
  auto model = [&](const char* fac) { const json& l = g.combat.value("models", json::object()).value(fac, json::array()); if (l.empty()) return -1; auto it = g.modelIdx.find(l[0].get<std::string>()); return it == g.modelIdx.end() ? -1 : it->second; };
  auto gun = [&](const char* id) { for (int k = 0; k < (int)g.app.weaponDefs.size(); ++k) if (g.app.weaponDefs[k]->id == id) return k; return -1; };
  const int mA = model("Allies"), mX = model("Axis"), gA = gun("m1_garand"), gX = gun("kar98k");
  const float hx = g.app.terrain.buildHalfX * 0.85f, hz = g.app.terrain.buildHalfZ * 0.85f; const int side = (int)std::ceil(std::sqrt((double)n));
  for (int i = 0; i < n; ++i) {
    const float x = -hx + 2 * hx * ((i % side) + 0.5f) / side, z = -hz + 2 * hz * ((i / side) + 0.5f) / side; const int team = x > 0 ? 1 : 0;
    crowdSpawn(g, vec3(x, 0, z), team ? 1.5708f : -1.5708f, team, team ? mX : mA, team ? gX : gA, i % 7 == 0 ? 4.0f : 1.4f);
  }
  crowdGoal(g, 0, vec3(hx, 0, 0)); crowdGoal(g, 1, vec3(-hx, 0, 0));
  logf("crowd: test source, %d units", C.n);
}
}

int crowdCount() { return C.n; }

int crowdSpawn(Game& g, vec3 feet, float yaw, int team, int model, int gun, float speed) {
  if (C.n + (int)C.stageU.size() >= kCap) return -1;
  if (!ensureGpu(g)) return -1;
  const int ti = typeIndex(g, model, gun); if (ti < 0) return -1;
  if (C.stageU.empty()) C.stageFrom = C.n + (int)C.stageU.size();
  CrowdUnit u; u.pos = vec4(feet.x, g.app.terrain.height(feet.x, feet.z), feet.z, yaw); u.prev = u.pos; u.anim = vec4(0, (float)((C.n * 37) % 100) * 0.0137f, (float)team, 1);
  CrowdSim s; s.a = vec4(0, 0, 100, speed); s.b = glm::ivec4(team, ti, 0, -1);
  C.stageU.push_back(u); C.stageS.push_back(s);
  return C.stageFrom + (int)C.stageU.size() - 1;
}

void crowdGoal(Game& g, int team, vec3 goal) { if (team < 0 || team > 1) return; if (!C.gridOk) C.gridOk = buildGrid(g); if (C.gridOk) buildCost(team, goal); }

void crowdReset() {
  for (auto& [k, t] : C.types) crowdDestroy(t);
  for (GLuint b : {C.units, C.sim, C.cnt, C.idx, C.height, C.costBuf, C.vis, C.visCnt, C.cmd, C.cmdBucket}) if (b) glDeleteBuffers(1, &b);
  for (GLuint p : {C.progHash, C.progMove, C.progCull, C.progCmd}) if (p) glDeleteProgram(p);
  C = GpuCrowd{};
}

void crowdFrame(Game& g) {
  Renderer& R = g.app.R; R.crowdDraws.clear();
  if (!R.perf.crowd) return;
  const float dt = C.lastT < 0 ? 0.0f : (float)std::clamp(g.t - C.lastT, 0.0, 0.1); C.lastT = g.t;
  testSource(g);
  if (!C.units || (C.n == 0 && C.stageU.empty()) || !C.progMove) return;
  // spawns and fields to the GPU
  if (!C.stageU.empty()) {
    glNamedBufferSubData(C.units, (GLintptr)C.stageFrom * sizeof(CrowdUnit), C.stageU.size() * sizeof(CrowdUnit), C.stageU.data());
    glNamedBufferSubData(C.sim, (GLintptr)C.stageFrom * sizeof(CrowdSim), C.stageS.size() * sizeof(CrowdSim), C.stageS.data());
    C.n = C.stageFrom + (int)C.stageU.size(); C.stageU.clear(); C.stageS.clear();
  }
  const size_t cells = (size_t)C.g.nx * C.g.nz;
  for (int t = 0; t < 2; ++t) if (C.costDirty[t] && C.cost[t].size() == cells) { glNamedBufferSubData(C.costBuf, (GLintptr)(t * cells * 4), cells * 4, C.cost[t].data()); C.costDirty[t] = false; }
  // bind and run: hash, move, cull, commands
  const GLuint bufs[10] = {C.units, C.sim, C.cnt, C.idx, C.height, C.costBuf, C.vis, C.visCnt, C.cmd, C.cmdBucket};
  for (int k = 0; k < 10; ++k) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, k, bufs[k]);
  const GLuint groups = (GLuint)((C.n + 127) / 128); const uint32_t zero = 0;
  auto common = [&](GLuint p) { glUseProgram(p); glUniform1i(glGetUniformLocation(p, "uN"), C.n); glUniform1i(glGetUniformLocation(p, "uNx"), C.g.nx); glUniform1i(glGetUniformLocation(p, "uNz"), C.g.nz);
                               glUniform2fv(glGetUniformLocation(p, "uOrg"), 1, glm::value_ptr(C.g.org)); glUniform1f(glGetUniformLocation(p, "uCell"), C.g.cell); glUniform1f(glGetUniformLocation(p, "uDt"), dt); };
  if (dt > 0) {
    glClearNamedBufferData(C.cnt, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    common(C.progHash); glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    common(C.progMove); glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  }
  glClearNamedBufferData(C.visCnt, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
  const Frustum fr = Frustum::fromVP(R.lastViewProj()); const vec3 eye = g.app.eyePos();
  const float K = (float)std::max(1, R.displayH) * 1.8f / (2.0f * std::tan(glm::radians(settings().fov()) * 0.5f));
  common(C.progCull);
  vec4 pl[6]; for (int k = 0; k < 6; ++k) pl[k] = k < fr.n ? fr.pl[k] : vec4(0, 0, 0, 1);
  glUniform4fv(glGetUniformLocation(C.progCull, "uPl"), 6, glm::value_ptr(pl[0])); glUniform3fv(glGetUniformLocation(C.progCull, "uEye"), 1, glm::value_ptr(eye));
  glUniform1f(glGetUniformLocation(C.progCull, "uK"), K); glUniform1f(glGetUniformLocation(C.progCull, "uCapB"), (float)kCapB);
  glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(C.progCmd); glUniform1i(glGetUniformLocation(C.progCmd, "uNCmd"), C.nCmd); glUniform1ui(glGetUniformLocation(C.progCmd, "uCapB"), (GLuint)kCapB);
  glDispatchCompute((GLuint)((C.nCmd + 63) / 64), 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
  glUseProgram(0);
  { static double nextLog = 0; if (g.t >= nextLog || g.t + 10 < nextLog) { nextLog = g.t + 5;   // a status line every 5 s (a small read back)
      std::vector<uint32_t> vc(kMaxTypes * 2); glGetNamedBufferSubData(C.visCnt, 0, vc.size() * 4, vc.data()); std::string b;
      for (int k = 0; k < (int)C.typeKey.size() * 2; ++k) b += (k ? " " : "") + std::to_string(vc[k]);
      logf("[crowd] t %.0f s: %d units, %d types, visible per type x LOD: %s", g.t, C.n, (int)C.typeKey.size(), b.c_str()); } }
  // the draws: per type x LOD, the body and its gun, their instance counts the cull pass's
  for (int ti = 0; ti < (int)C.typeKey.size(); ++ti) for (int L = 0; L < 2; ++L) {
    const CrowdType& t = C.types[C.typeKey[ti]]; const CrowdType::Lod& l = t.lod[L]; const int b = ti * 2 + L;
    CrowdDraw d; d.look = &t.m->gpu; d.parts = &l.parts; d.vao = l.vao; d.rest = l.rest; d.pal = t.pal; d.J = t.J; d.base = b * kCapB; d.fps = t.fps;
    d.units = C.units; d.vis = C.vis; d.cmd = C.cmd; d.cmdFirst = C.cmdFirstOf[(size_t)b * 2];
    for (int c = 0; c < 8; ++c) d.clips[c] = t.clip[c];
    R.crowdDraws.push_back(d);
    if (t.gun && t.gun->vao) { CrowdDraw gd = d; gd.look = t.gun; gd.parts = &t.gun->parts; gd.vao = t.gun->vao; gd.rest = 0; gd.gun = true; gd.cmdFirst = C.cmdFirstOf[(size_t)b * 2 + 1]; R.crowdDraws.push_back(gd); }
  }
}
