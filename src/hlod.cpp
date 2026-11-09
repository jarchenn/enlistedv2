// PF2 Phase B: building HLOD cells (hlod.h, docs/rendering.md#hlod).
#include "hlod.h"
#include "app.h"
#include "settings.h"
#include <chrono>
#include <unordered_map>

namespace {
constexpr float kCell = 128.0f;   // m
constexpr int kMaxRebuildsPerFrame = 2;
struct Cell {
  uint64_t sig = 0, builtSig = ~0ull;   // members this frame / members the meshes were built from
  vec2 lo{1e9f}, hi{-1e9f};             // members' ground footprint (xz), this frame
  GpuMesh mesh, glass; GLuint instVbo = 0;
  vec3 glassAt{0};                      // the glass mesh is relative to this (the glass pass sorts by the item origin)
  bool active = false, seen = false;
  void destroy() { mesh.destroy(); glass.destroy(); if (instVbo) glDeleteBuffers(1, &instVbo); instVbo = 0; builtSig = ~0ull; }
};
std::unordered_map<int64_t, Cell> gCells;
std::unordered_map<uint64_t, int64_t> gMember;   // instance id -> cell key, eligible copies this frame
HlodStats gStats;

int64_t cellKey(vec3 p) { return ((int64_t)std::floor(p.x / kCell) << 32) ^ (int64_t)(uint32_t)(int32_t)std::floor(p.z / kCell); }
uint64_t mix64(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h * 0x100000001B3ull; }

bool eligible(App& app, const Instance& i, const InstanceRT& r) {
  const AssetDef* a = r.asset;
  return a && a->hlodOpaque && !r.damaged && !r.defDestroyed && i.id != app.selected && !app.interior.prop(a);
}

// per-vertex extras of a cell mesh (VAO binding 1): the copy's SurfaceParams::inst (w = 2 marks it) and the asset-local
// position / normal, so kMainVS gives the patterns the same coordinates as the per-copy draw
struct Extra { vec4 inst; vec3 lp, ln; };
void build(App& app, Cell& c, int64_t key) {
  MeshData md, mg; std::vector<Extra> inst;
  c.glassAt = vec3((c.lo.x + c.hi.x) * 0.5f, 0.0f, (c.lo.y + c.hi.y) * 0.5f);
  for (auto& i : app.doc.instances) {
    auto g = gMember.find(i.id);
    if (g == gMember.end() || g->second != key) continue;
    const InstanceRT& r = app.rt.at(i.id);
    const AssetDef* a = r.asset;
    mat4 M = app.instanceMatrix(i); glm::mat3 N(M); mat4 invM = glm::inverse(M); glm::mat3 invN(invM);
    size_t v0 = md.v.size();
    const SurfaceParams* sp = app.interior.surfaceFor(app, i, a);
    vec4 in = sp ? sp->inst : SurfaceParams().inst; in.w = 2.0f;   // w > 1.5: kMainVS takes the per-vertex seed / wear / palette
    auto add = [&](const MeshData& src, MeshData& dst, bool withInst, vec3 origin) {
      uint32_t base = (uint32_t)dst.v.size();
      for (Vertex v : src.v) { v.pos = vec3(M * vec4(v.pos, 1.0f)) - origin; v.nrm = N * v.nrm; dst.v.push_back(v); }
      for (uint32_t k : src.i) dst.i.push_back(base + k);
    };
    add(*a->hlodOpaque, md, true, vec3(0));
    if (a->hlodGlass) add(*a->hlodGlass, mg, false, c.glassAt);
    for (size_t k = 0; k < a->interactables.size(); ++k) {   // doors / shutters in their current pose (the signature has it)
      const InteractDef& d = a->interactables[k];
      for (size_t l = 0; l < d.leaves.size(); ++l) {
        uint8_t st = k < r.leafSt.size() && l < r.leafSt[k].size() ? r.leafSt[k][l] : 0;
        if (st & 2) continue;   // detached: drawn from its rigid body
        mat4 L = app.leafMatrix(i, d.leaves[l], d, k < r.progress.size() ? r.progress[k] : 0.0f);
        for (const Part& p : d.leaves[l].parts) {
          bool glassP = a->materials[p.mat].pattern == PAT_GLASS;
          if (glassP && (st & 1)) continue;   // its glass is broken
          if (glassP) appendPart(mg, p, a->materials[p.mat], glm::translate(mat4(1), -c.glassAt) * L);
          else appendPart(md, p, a->materials[p.mat], L);
        }
      }
    }
    for (size_t k = v0; k < md.v.size(); ++k) inst.push_back({in, vec3(invM * vec4(md.v[k].pos, 1.0f)), invN * md.v[k].nrm});
  }
  c.destroy();
  c.mesh.upload(md); c.glass.upload(mg);
  if (c.mesh.vao && !inst.empty()) {   // attributes 7-9 from a second buffer (other VAOs leave them disabled: kMainVS uses uInst)
    glCreateBuffers(1, &c.instVbo);
    glNamedBufferData(c.instVbo, inst.size() * sizeof(Extra), inst.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(c.mesh.vao, 1, c.instVbo, 0, sizeof(Extra));
    const int n[3] = {4, 3, 3}; const size_t off[3] = {offsetof(Extra, inst), offsetof(Extra, lp), offsetof(Extra, ln)};
    for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(c.mesh.vao, 7 + k); glVertexArrayAttribFormat(c.mesh.vao, 7 + k, n[k], GL_FLOAT, GL_FALSE, (GLuint)off[k]); glVertexArrayAttribBinding(c.mesh.vao, 7 + k, 1); }
  }
  c.builtSig = c.sig;
}
}  // namespace

void hlodFrame(App& app) {
  gStats.active = gStats.membersSkipped = gStats.rebuilt = 0; gStats.rebuildMs = 0;
  gMember.clear();
  for (auto& [k, c] : gCells) { c.sig = 0; c.lo = vec2(1e9f); c.hi = vec2(-1e9f); c.active = c.seen = false; }
  if (!app.R.perf.hlod || app.lodPxPerM <= 0) return;   // off: nothing active (built cells are kept for when it is back on)
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !eligible(app, i, it->second)) continue;
    int64_t key = cellKey(i.pos);
    Cell& c = gCells[key];
    c.seen = true; c.sig = mix64(mix64(mix64(c.sig, i.id), i.rev), (uint64_t)(uintptr_t)it->second.asset);
    for (float p : it->second.progress) c.sig = mix64(c.sig, (uint64_t)(int64_t)std::lround(p * 1000.0f));   // a door moving rebuilds the cell
    for (auto& ls : it->second.leafSt) for (uint8_t b : ls) c.sig = mix64(c.sig, b);
    vec3 e = (it->second.asset->bmax - it->second.asset->bmin) * 0.5f; float ext = std::max(e.x, e.z) * 1.42f;   // any yaw
    c.lo = glm::min(c.lo, vec2(i.pos.x, i.pos.z) - ext); c.hi = glm::max(c.hi, vec2(i.pos.x, i.pos.z) + ext);
    gMember[i.id] = key;
  }
  for (auto it = gCells.begin(); it != gCells.end();) { if (!it->second.seen) { it->second.destroy(); it = gCells.erase(it); } else ++it; }
  gStats.cells = (int)gCells.size();
  // a cell is drawn merged only when every point of it is in the LOD1 range (10 px/m, + the 15 % hysteresis band) and
  // some of it is inside the render distance; otherwise its copies take the ordinary per-copy path
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  const float farD = app.lodPxPerM / 10.0f * 1.15f, cut = settings().renderDist();
  auto t0 = std::chrono::steady_clock::now();
  for (auto& [key, c] : gCells) {
    vec2 q = glm::clamp(vec2(eye.x, eye.z), c.lo, c.hi);
    float dn = glm::length(vec2(eye.x, eye.z) - q);
    bool want = dn > farD && dn < cut;
    if (!want) continue;
    if (c.builtSig != c.sig) {
      if (gStats.rebuilt >= kMaxRebuildsPerFrame) continue;   // until rebuilt, its copies draw one by one
      build(app, c, key); ++gStats.rebuilt;
    }
    c.active = c.mesh.count > 0 || c.glass.count > 0;
  }
  gStats.rebuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  int act = 0; for (auto& [k, c] : gCells) act += c.active;
  static int lastCells = -1, lastAct = -1; static size_t lastElig = 0;
  if (gStats.cells != lastCells || act != lastAct || gMember.size() != lastElig) {   // log only when the cell picture changes
    lastCells = gStats.cells; lastAct = act; lastElig = gMember.size();
    logf("hlod: %d cells (%d active), %zu eligible copies, far from %.0f m, cut %.0f m, rebuilt %d (%.2f ms)", gStats.cells, act, gMember.size(), farD, cut, gStats.rebuilt, gStats.rebuildMs);
  }
}

bool hlodSkip(const Instance& i) {
  auto g = gMember.find(i.id);
  if (g == gMember.end()) return false;
  auto c = gCells.find(g->second);
  if (c == gCells.end() || !c->second.active) return false;
  ++gStats.membersSkipped;
  return true;
}

void hlodDraw(std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  for (auto& [k, c] : gCells) {
    if (!c.active) continue;
    ++gStats.active;
    if (c.mesh.count) op.push_back({&c.mesh, mat4(1)});
    if (c.glass.count) gl.push_back({&c.glass, glm::translate(mat4(1), c.glassAt)});
  }
}

const HlodStats& hlodStats() { return gStats; }
