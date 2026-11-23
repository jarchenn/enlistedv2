#include "user_cache.h"
#include <chrono>
#include "assets.h"
#include "sandbags.h"   // HB1b
#include "crumb.h"
#include "loading.h"
#include "model.h"
#include "textures.h"
int blendPartMode(const json& asset, const json& pj);   // GX1 (terrain_blend.cpp)
#include <functional>
#include "physics.h"
#include "nature.h"
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cfloat>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif

// ---------------------------------------------------------------- utilities
void logf(const char* fmt, ...) {
  static FILE* f = nullptr;
  if (!f) f = std::fopen((exeDir() / "normandy.log").string().c_str(), "w");
  va_list ap; va_start(ap, fmt);
  char buf[2048]; std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  static const bool memlog = std::getenv("NMK_MEMLOG") != nullptr;   // MEM1: every line stamped with commit / working set (MB)
  if (memlog && f) { static const auto t0 = std::chrono::steady_clock::now(); ProcMem m = processMemory(); std::fprintf(f, "[c%6.0f w%6.0f t%7.2f] ", m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
  if (f) { std::fputs(buf, f); std::fputc('\n', f); std::fflush(f); }
  std::fputs(buf, stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
std::string readTextFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary); if (!in) return {};
  std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
static fs::path binaryDir() {   // the folder of the running executable
#ifdef _WIN32
  wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return fs::path(buf).parent_path();
#elif defined(__APPLE__)
  char buf[4096]; uint32_t n = sizeof buf; std::error_code ec;
  if (_NSGetExecutablePath(buf, &n) != 0) return fs::current_path();
  return fs::weakly_canonical(buf, ec).parent_path();
#else
  std::error_code ec; return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}
// where the game writes (log, settings, maps, caches, selftest_out): next to the executable, except inside a macOS .app,
// which is signed and may run from a read-only translocated copy: there ~/Library/Application Support/NormandyMapmaker
fs::path exeDir() {
  static const fs::path dir = [] {
    fs::path b = binaryDir();
#ifdef __APPLE__
    if (b.filename() == "MacOS" && b.parent_path().filename() == "Contents") {
      const char* home = std::getenv("HOME");
      fs::path u = fs::path(home ? home : "/tmp") / "Library" / "Application Support" / "NormandyMapmaker";
      std::error_code ec; fs::create_directories(u, ec);
      return u;
    }
#endif
    return b;
  }();
  return dir;
}
ProcMem processMemory() {
  ProcMem m;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc);
  m.WorkingSetSize = pmc.WorkingSetSize; m.PeakWorkingSetSize = pmc.PeakWorkingSetSize; m.PrivateBytes = pmc.PrivateUsage;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info{}; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) == KERN_SUCCESS) { m.WorkingSetSize = info.resident_size; m.PeakWorkingSetSize = info.resident_size_max; }
#else
  rusage u{}; getrusage(RUSAGE_SELF, &u); m.WorkingSetSize = m.PeakWorkingSetSize = (size_t)u.ru_maxrss * 1024;
#endif
  return m;
}
fs::path contentDir() {
  fs::path p = binaryDir() / "content";
  if (fs::exists(p)) return p;
#ifdef __APPLE__
  if (fs::exists(binaryDir() / "../Resources/content")) return binaryDir() / "../Resources/content";   // NormandyMapmaker.app/Contents/MacOS
#endif
  return fs::current_path() / "content";
}
fs::path userMapsDir() {
  fs::path p = exeDir() / "maps";
  std::error_code ec; fs::create_directories(p, ec);
  return p;
}

bool rayOBB(vec3 ro, vec3 rd, vec3 c, vec3 he, const quat& q, float& tOut) {
  quat iq = glm::inverse(q);
  vec3 o = iq * (ro - c), d = iq * rd;
  float tmin = -1e30f, tmax = 1e30f;
  for (int a = 0; a < 3; ++a) {
    if (std::fabs(d[a]) < 1e-8f) { if (o[a] < -he[a] || o[a] > he[a]) return false; continue; }
    float t1 = (-he[a] - o[a]) / d[a], t2 = (he[a] - o[a]) / d[a];
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
    if (tmin > tmax) return false;
  }
  if (tmax < 0) return false;
  tOut = tmin >= 0 ? tmin : 0.0f; return true;
}
static float pointBoxDist2(vec3 p, vec3 he) {
  vec3 d = glm::max(glm::abs(p) - he, vec3(0)); return glm::dot(d, d);
}
float segOBBDist2(vec3 a, vec3 b, vec3 c, vec3 he, const quat& q) {
  quat iq = glm::inverse(q);
  vec3 la = iq * (a - c), lb = iq * (b - c);
  // distance to a convex set along a segment is convex: ternary search
  float lo = 0, hi = 1;
  for (int it = 0; it < 40; ++it) {
    float m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
    if (pointBoxDist2(glm::mix(la, lb, m1), he) < pointBoxDist2(glm::mix(la, lb, m2), he)) hi = m2; else lo = m1;
  }
  return pointBoxDist2(glm::mix(la, lb, (lo + hi) * 0.5f), he);
}

// ---------------------------------------------------------------- GPU mesh
namespace {   // MEM1: GpuVertex packing (the bundled GLM lacks gtc/packing's sources)
uint16_t toHalf(float f) {
  uint32_t x; std::memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000u; int e = (int)((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffffu;
  if (((x >> 23) & 0xff) == 0xff) return (uint16_t)(sign | 0x7c00u | (m ? 0x200u : 0));   // inf / nan
  if (e >= 31) return (uint16_t)(sign | 0x7c00u);                                         // overflow: inf
  if (e <= 0) { if (e < -10) return (uint16_t)sign; m |= 0x800000u; uint32_t sh = (uint32_t)(14 - e); return (uint16_t)(sign | ((m + (1u << (sh - 1))) >> sh)); }   // subnormal
  uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
  if (m & 0x1000u) ++h;   // round half up (carries into the exponent correctly)
  return (uint16_t)h;
}
uint32_t snorm10x3(vec3 n) {   // GL_INT_2_10_10_10_REV, normalized: x bits 0-9, y 10-19, z 20-29, w 0
  auto c = [](float v) { int q = (int)std::lround(std::clamp(v, -1.0f, 1.0f) * 511.0f); return (uint32_t)q & 0x3ffu; };
  return c(n.x) | (c(n.y) << 10) | (c(n.z) << 20);
}
}
bool gpuVertexPackCheck(std::string& why) {   // MEM1: known half / snorm encodings (the probe runs it)
  struct H { float f; uint16_t h; } hs[] = {{0.0f, 0}, {1.0f, 0x3C00}, {-2.0f, 0xC000}, {0.5f, 0x3800}, {65504.0f, 0x7BFF}, {1e6f, 0x7C00}, {5.9604645e-8f, 0x0001}, {0.333333f, 0x3555}, {8.0f, 0x4800}};
  for (auto& x : hs) if (toHalf(x.f) != x.h) { char b[96]; std::snprintf(b, sizeof b, "half(%g) = %04x, want %04x", x.f, toHalf(x.f), x.h); why = b; return false; }
  uint32_t n = snorm10x3(vec3(1, -1, 0));
  if ((n & 0x3ff) != 511 || ((n >> 10) & 0x3ff) != (uint32_t)(-511 & 0x3ff) || (n >> 20) != 0) { why = "snorm10 packing"; return false; }
  return true;
}
void GpuMesh::upload(const MeshData& m) {
  destroy();
  count = (int)m.i.size();
  if (!count) return;
  bmin = vec3(FLT_MAX); bmax = vec3(-FLT_MAX);
  for (uint32_t k : m.i) if (k < m.v.size()) { bmin = glm::min(bmin, m.v[k].pos); bmax = glm::max(bmax, m.v[k].pos); }
  if (bmin.x > bmax.x) bmin = bmax = vec3(0);
  glCreateVertexArrays(1, &vao);
  glCreateBuffers(1, &vbo); glCreateBuffers(1, &ebo);
  std::vector<GpuVertex> pv(m.v.size());
  auto h3 = [](uint16_t* o, vec3 c) { o[0] = toHalf(c.x); o[1] = toHalf(c.y); o[2] = toHalf(c.z); o[3] = 0; };
  for (size_t k = 0; k < m.v.size(); ++k) {
    const Vertex& v = m.v[k]; GpuVertex& g = pv[k];
    g.pos = v.pos; g.nrm = snorm10x3(v.nrm); h3(g.albedo, v.albedo); g.params = v.params;
    g.pcoord = v.pcoord; g.pnrm = snorm10x3(v.pnrm); h3(g.emis, v.emis);
  }
  glNamedBufferData(vbo, pv.size() * sizeof(GpuVertex), pv.data(), GL_STATIC_DRAW);
  glNamedBufferData(ebo, m.i.size() * sizeof(uint32_t), m.i.data(), GL_STATIC_DRAW);
  glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(GpuVertex));
  glVertexArrayElementBuffer(vao, ebo);
  struct A { int n; GLenum type; GLboolean norm; size_t off; } attrs[] = {
    {3, GL_FLOAT, GL_FALSE, offsetof(GpuVertex, pos)}, {4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(GpuVertex, nrm)}, {3, GL_HALF_FLOAT, GL_FALSE, offsetof(GpuVertex, albedo)},
    {4, GL_FLOAT, GL_FALSE, offsetof(GpuVertex, params)}, {3, GL_FLOAT, GL_FALSE, offsetof(GpuVertex, pcoord)}, {4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(GpuVertex, pnrm)}, {3, GL_HALF_FLOAT, GL_FALSE, offsetof(GpuVertex, emis)}};
  for (int k = 0; k < 7; ++k) {
    glEnableVertexArrayAttrib(vao, k);
    glVertexArrayAttribFormat(vao, k, attrs[k].n, attrs[k].type, attrs[k].norm, (GLuint)attrs[k].off);
    glVertexArrayAttribBinding(vao, k, 0);
  }
}
// P2 leafmerge: every leaf of the asset at rest - interactable k closed (open[k] = 0) or fully open (1) - as one opaque
// and one glass mesh in asset space (the matrix App::leafMatrix gives at progress 0 / 1, without the copy's own). Each
// vertex keeps its leaf-local position / normal in attributes 8 / 9 (attribute 7 w = 1.3), so kMainVS gives the patterns
// the same coordinates as the per-leaf draw.
void buildLeafPose(const AssetDef& a, const std::vector<uint8_t>& open, LeafPose& out) {
  struct LX { vec4 inst; vec3 lp, ln; };
  MeshData mo, mg; std::vector<LX> xo, xg;
  for (size_t k = 0; k < a.interactables.size(); ++k) {
    const InteractDef& d = a.interactables[k];
    for (auto& l : d.leaves) {
      float ang = k < open.size() && open[k] ? l.sign * d.openAngle : 0.0f;
      mat4 M = glm::translate(mat4(1), l.hinge) * glm::mat4_cast(glm::angleAxis(radians(ang), d.axis)); glm::mat3 N(M);
      auto add = [&](const MeshData* s, MeshData& dst, std::vector<LX>& x) {
        if (!s) return;
        uint32_t base = (uint32_t)dst.v.size();
        for (Vertex v : s->v) { x.push_back({vec4(0, 0, 0, 1.3f), v.pos, v.nrm}); v.pos = vec3(M * vec4(v.pos, 1.0f)); v.nrm = N * v.nrm; dst.v.push_back(v); }
        for (uint32_t i : s->i) dst.i.push_back(base + i);
      };
      add(l.cpuO.get(), mo, xo); add(l.cpuG.get(), mg, xg);
    }
  }
  auto local = [&](GpuMesh& m, const std::vector<LX>& x, GLuint& buf) {   // attributes 7-9 from a second buffer (binding 1), as the HLOD cells do
    if (!m.vao || x.empty()) return;
    glCreateBuffers(1, &buf); glNamedBufferData(buf, x.size() * sizeof(LX), x.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(m.vao, 1, buf, 0, sizeof(LX));
    const int n[3] = {4, 3, 3}; const size_t off[3] = {offsetof(LX, inst), offsetof(LX, lp), offsetof(LX, ln)};
    for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(m.vao, 7 + k); glVertexArrayAttribFormat(m.vao, 7 + k, n[k], GL_FLOAT, GL_FALSE, (GLuint)off[k]); glVertexArrayAttribBinding(m.vao, 7 + k, 1); }
  };
  out.destroy();
  out.opaque.upload(mo); out.glass.upload(mg);
  local(out.opaque, xo, out.xbuf[0]); local(out.glass, xg, out.xbuf[1]);
}
void LeafPose::destroy() { opaque.destroy(); glass.destroy(); for (GLuint& b : xbuf) { if (b) glDeleteBuffers(1, &b); b = 0; } }

void GpuMesh::destroy() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (ivao) glDeleteVertexArrays(1, &ivao);
  ivao = 0;
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ebo) glDeleteBuffers(1, &ebo);
  vao = vbo = ebo = 0; count = 0; bmin = bmax = vec3(0);
}
void GpuMesh::draw() const {
  if (!count) return;
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

// ---------------------------------------------------------------- mesh building
// grain axis written to the vertex: the material's, or (grain -1) the part's longest local axis, so furniture
// grain follows legs, rails, doors and boards (docs/interiors.md)
static float grainOf(const Part& p, const Material& mat) {
  if (mat.grainAxis >= 0) return (float)mat.grainAxis;
  return p.size.x >= p.size.y && p.size.x >= p.size.z ? 0.0f : p.size.y >= p.size.z ? 1.0f : 2.0f;
}
static void pushQuad(MeshData& m, const Part& p, const Material& mat, const mat4& xf,
                     const vec3 lp[4], vec3 ln, bool prismSpace) {
  uint32_t base = (uint32_t)m.v.size();
  glm::mat3 nx = glm::mat3(xf);
  for (int k = 0; k < 4; ++k) {
    Vertex v;
    vec3 ap = prismSpace ? lp[k] : p.center + p.rot * lp[k];
    v.pos = vec3(xf * vec4(ap, 1));
    v.nrm = glm::normalize(nx * (prismSpace ? ln : p.rot * ln));
    v.albedo = mat.albedo;
    v.params = vec4((float)(mat.pattern + 100 * mat.texCode), mat.roughness, mat.metallic, grainOf(p, mat)); v.emis = mat.emissive;
    v.pcoord = lp[k];
    v.pnrm = prismSpace ? ln : ln * 2.0f;   // MA1b: |pnrm| = 2 marks a box face (kMainFS: edge distance from the provoking corner)
    m.v.push_back(v);
  }
  // ensure CCW winding w.r.t. normal
  vec3 a = m.v[base].pos, b = m.v[base + 1].pos, c = m.v[base + 2].pos;
  bool flip = glm::dot(glm::cross(b - a, c - a), m.v[base].nrm) < 0;
  uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
  if (flip) { idx[1] = 2; idx[2] = 1; idx[4] = 3; idx[5] = 2; }
  for (uint32_t k : idx) m.i.push_back(base + k);
}

static vec3 prismPoint(int axis, float a, vec2 uv) {
  if (axis == 0) return vec3(a, uv.y, uv.x);
  if (axis == 2) return vec3(uv.x, uv.y, a);
  return vec3(uv.x, a, uv.y);
}

// Rounded box: the cube's six faces as grids that are dense only near the edges, each grid point projected onto the
// box shrunk by `rad` plus a sphere of radius `rad` (smooth normals). Samples follow equal angles over each 45-degree
// half of an edge, so the round reads evenly; 2-4 segments per half by radius.
static void appendRound(MeshData& m, const Part& p, const Material& mat, const mat4& xf) {
  vec3 h = p.size * 0.5f;
  bool bevel = p.shape == Part::BOX;   // "bv": one 45-degree facet per half edge (a soft chamfer)
  float r = std::min(bevel ? p.bv : p.rad, std::min({h.x, h.y, h.z}));
  vec3 e = h - vec3(r);
  int S = bevel ? 1 : r < 0.12f ? 2 : r < 0.5f ? 3 : 4;
  auto samples = [&](float ex) {
    std::vector<float> s;
    for (int k = S; k >= 1; --k) s.push_back(-ex - r * std::tan(glm::radians(45.0f) * k / S));
    s.push_back(-ex); if (ex > 1e-4f) s.push_back(ex);
    for (int k = 1; k <= S; ++k) s.push_back(ex + r * std::tan(glm::radians(45.0f) * k / S));
    return s;
  };
  glm::mat3 nx = glm::mat3(xf);
  for (int ax = 0; ax < 3; ++ax)
    for (int sg = -1; sg <= 1; sg += 2) {
      int u = (ax + 1) % 3, w = (ax + 2) % 3;
      std::vector<float> su = samples(e[u]), sw = samples(e[w]);
      uint32_t base = (uint32_t)m.v.size();
      for (float b : sw)
        for (float a : su) {
          vec3 c(0); c[ax] = sg * h[ax]; c[u] = a; c[w] = b;
          vec3 in = glm::clamp(c, -e, e), d = c - in;
          vec3 n = glm::length(d) > 1e-6f ? glm::normalize(d) : vec3(0);
          if (glm::length(d) <= 1e-6f) n[ax] = (float)sg;
          vec3 lp = in + n * r;
          Vertex v;
          v.pos = vec3(xf * vec4(p.center + p.rot * lp, 1));
          v.nrm = glm::normalize(nx * (p.rot * n));
          v.albedo = mat.albedo;
          v.params = vec4((float)(mat.pattern + 100 * mat.texCode), mat.roughness, mat.metallic, grainOf(p, mat)); v.emis = mat.emissive;
          v.pcoord = lp; v.pnrm = n;
          m.v.push_back(v);
        }
      int nu = (int)su.size(), nw = (int)sw.size();
      vec3 fn(0); fn[ax] = (float)sg;
      vec3 wn = glm::normalize(nx * (p.rot * fn));
      for (int j = 0; j + 1 < nw; ++j)
        for (int i = 0; i + 1 < nu; ++i) {
          uint32_t q[4] = {base + j * nu + i, base + j * nu + i + 1, base + (j + 1) * nu + i + 1, base + (j + 1) * nu + i};
          vec3 a = m.v[q[0]].pos, b = m.v[q[1]].pos, c = m.v[q[2]].pos;
          bool flip = glm::dot(glm::cross(b - a, c - a), wn) < 0;
          uint32_t idx[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
          if (flip) { idx[1] = q[2]; idx[2] = q[1]; idx[4] = q[3]; idx[5] = q[2]; }
          for (uint32_t k : idx) m.i.push_back(k);
        }
    }
}

// ---------------------------------------------------------------- curved and sloped parts (CYL, ARCH, WEDGE)
// Built in the part's local frame (box centred on the origin). Curved surfaces carry smooth normals; caps, ends and
// faces flat ones. partHulls gives the same outlines as convex pieces for the collider.
namespace {
struct LocalMesh {
  MeshData& m; const Part& p; const Material& mat; const mat4& xf; glm::mat3 nx;
  LocalMesh(MeshData& m_, const Part& p_, const Material& mat_, const mat4& xf_) : m(m_), p(p_), mat(mat_), xf(xf_), nx(glm::mat3(xf_)) {}
  uint32_t v(vec3 lp, vec3 ln) {
    Vertex q;
    q.pos = vec3(xf * vec4(p.center + p.rot * lp, 1));
    q.nrm = glm::normalize(nx * (p.rot * ln));
    q.albedo = mat.albedo;
    q.params = vec4((float)(mat.pattern + 100 * mat.texCode), mat.roughness, mat.metallic, grainOf(p, mat)); q.emis = mat.emissive;
    q.pcoord = lp; q.pnrm = ln;
    m.v.push_back(q);
    return (uint32_t)m.v.size() - 1;
  }
  // a triangle wound counter-clockwise about the local outward direction `out` (in world space, as pushQuad)
  void tri(uint32_t a, uint32_t b, uint32_t c, vec3 out) {
    vec3 A = m.v[a].pos, B = m.v[b].pos, C = m.v[c].pos;
    float d = glm::dot(glm::cross(B - A, C - A), nx * (p.rot * out));
    if (d == 0) return;   // degenerate (a cusp)
    if (d < 0) std::swap(b, c);
    m.i.insert(m.i.end(), {a, b, c});
  }
  void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, vec3 out) { tri(a, b, c, out); tri(a, c, d, out); }
  void poly(const std::vector<vec3>& pts, vec3 n) {   // flat convex polygon
    uint32_t b = v(pts[0], n);
    for (size_t k = 1; k < pts.size(); ++k) v(pts[k], n);
    for (uint32_t k = 1; k + 1 < (uint32_t)pts.size(); ++k) tri(b, b + k, b + k + 1, n);
  }
};
int curveSegs(const Part& p, float len, int lo, int hi) {   // ~0.2 m chords, a multiple of 4
  if (p.seg > 0) return std::clamp(p.seg, 3, 128);
  return std::clamp(((int)std::ceil(len / 0.2f) + 3) / 4 * 4, lo, hi);
}
// CYL bottom ring (x, z). n-gon: flats facing the axes and touching the box (n a multiple of 4); round: the ellipse.
std::vector<vec2> cylRing(const Part& p, int maxSeg = 48) {
  vec3 h = p.size * 0.5f;
  bool flat = p.sides >= 3;
  int N = flat ? p.sides : curveSegs(p, glm::pi<float>() * (h.x + h.z), 12, maxSeg);
  float off = flat ? glm::pi<float>() / N : 0.0f, k = flat ? 1.0f / std::cos(glm::pi<float>() / N) : 1.0f;
  std::vector<vec2> r;
  for (int i = 0; i < N; ++i) { float t = off + glm::two_pi<float>() * i / N; r.push_back(vec2(h.x * k * std::cos(t), h.z * k * std::sin(t))); }
  return r;
}
// ARCH profile in (x, y): intrados I and extrados O at N + 1 angles from the springing (+x) over the crown to -x; the
// arch stands on the box's bottom face. Ring: both ellipses (outer = the box). Fill: O is the box top above each I.
void archProfile(const Part& p, int N, std::vector<vec2>& I, std::vector<vec2>& O, std::vector<vec2>& nI, std::vector<vec2>& nO) {
  vec3 h = p.size * 0.5f;
  float H = p.size.y, th = std::clamp(p.th, 0.01f, 0.999f * std::min(p.fill ? H : h.x, H));
  float rix = p.fill ? h.x : h.x - th, riy = H - th;
  if (!p.fill && p.th >= std::min(h.x, H)) { rix = 0.001f * h.x; riy = 0.001f * H; }   // th past both radii: a solid half-disc (fanlights)
  for (int k = 0; k <= N; ++k) {
    float t = glm::pi<float>() * k / N, c = k == N ? -1.0f : std::cos(t), s = k == 0 || k == N ? 0.0f : std::sin(t);
    I.push_back(vec2(rix * c, -h.y + riy * s));
    nI.push_back(-glm::normalize(vec2(c / rix, s / riy)));
    if (p.fill) { O.push_back(vec2(rix * c, h.y)); nO.push_back(vec2(0, 1)); }
    else { O.push_back(vec2(h.x * c, -h.y + H * s)); nO.push_back(glm::normalize(vec2(c / h.x, s / H))); }
  }
}
// WEDGE profile in (z, y): back bottom, front bottom, front top (front height = top * height), back top
std::vector<vec2> wedgeProfile(const Part& p) {
  vec3 h = p.size * 0.5f;
  std::vector<vec2> q{{-h.z, -h.y}, {h.z, -h.y}};
  if (p.top > 1e-3f) q.push_back(vec2(h.z, -h.y + p.size.y * std::min(p.top, 1.0f)));
  q.push_back(vec2(-h.z, h.y));
  return q;
}
}  // namespace

static void appendCurved(MeshData& m, const Part& p, const Material& mat, const mat4& xf) {
  LocalMesh L(m, p, mat, xf);
  vec3 h = p.size * 0.5f;
  if (p.shape == Part::CYL) {
    std::vector<vec2> ring = cylRing(p);
    int N = (int)ring.size();
    float tp = std::clamp(p.top, 0.0f, 1.0f);
    bool flat = p.sides >= 3, apex = tp < 1e-3f;
    auto bot = [&](int i) { vec2 r = ring[i % N]; return vec3(r.x, -h.y, r.y); };
    auto upp = [&](int i) { vec2 r = ring[i % N] * tp; return vec3(r.x, h.y, r.y); };
    // side normal from the slant (bottom to top) and the tangent along the ring, pointing away from the axis
    auto sideN = [&](vec2 r, vec2 tan) {
      vec3 slant(r.x * (tp - 1), p.size.y, r.y * (tp - 1)), n = glm::cross(slant, vec3(tan.x, 0, tan.y));
      if (glm::dot(n, vec3(r.x, 0, r.y)) < 0) n = -n;
      return glm::normalize(n);
    };
    auto roundN = [&](float t) { return sideN(vec2(h.x * std::cos(t), h.z * std::sin(t)), vec2(-h.x * std::sin(t), h.z * std::cos(t))); };
    for (int i = 0; i < N; ++i) {
      int j = i + 1;
      vec3 out = vec3(0.5f * (bot(i).x + bot(j).x), 0, 0.5f * (bot(i).z + bot(j).z));
      vec3 ni, nj, nm;
      if (flat) ni = nj = nm = sideN(0.5f * (ring[i] + ring[j % N]), ring[j % N] - ring[i]);
      else { float t = glm::two_pi<float>() / N; ni = roundN(t * i); nj = roundN(t * j); nm = roundN(t * (i + 0.5f)); }
      uint32_t a = L.v(bot(i), ni), b = L.v(bot(j), nj);
      if (apex) L.tri(a, b, L.v(vec3(0, h.y, 0), nm), out);
      else L.quad(a, b, L.v(upp(j), nj), L.v(upp(i), ni), out);
    }
    std::vector<vec3> cap;
    for (int i = 0; i < N; ++i) cap.push_back(bot(i));
    L.poly(cap, vec3(0, -1, 0));
    if (!apex) { cap.clear(); for (int i = 0; i < N; ++i) cap.push_back(upp(i)); L.poly(cap, vec3(0, 1, 0)); }
  } else if (p.shape == Part::ARCH) {
    int N = curveSegs(p, glm::half_pi<float>() * (p.size.x + p.size.y), 8, 32);
    std::vector<vec2> I, O, nI, nO;
    archProfile(p, N, I, O, nI, nO);
    auto P3 = [&](vec2 q, float z) { return vec3(q.x, q.y, z); };
    auto N3 = [](vec2 n) { return vec3(n.x, n.y, 0); };
    for (int k = 0; k < N; ++k) {
      vec3 oi = N3(glm::normalize(nI[k] + nI[k + 1])), oo = N3(glm::normalize(nO[k] + nO[k + 1]));
      L.quad(L.v(P3(I[k], -h.z), N3(nI[k])), L.v(P3(I[k + 1], -h.z), N3(nI[k + 1])), L.v(P3(I[k + 1], h.z), N3(nI[k + 1])), L.v(P3(I[k], h.z), N3(nI[k])), oi);
      L.quad(L.v(P3(O[k], -h.z), N3(nO[k])), L.v(P3(O[k + 1], -h.z), N3(nO[k + 1])), L.v(P3(O[k + 1], h.z), N3(nO[k + 1])), L.v(P3(O[k], h.z), N3(nO[k])), oo);
      for (float z : {-h.z, h.z}) L.poly({P3(I[k], z), P3(I[k + 1], z), P3(O[k + 1], z), P3(O[k], z)}, vec3(0, 0, z > 0 ? 1.0f : -1.0f));
    }
    for (int k : {0, N}) {   // ends: the springing beds (ring) or the sides of the spandrel (fill)
      vec3 n = p.fill ? vec3(k == 0 ? 1.0f : -1.0f, 0, 0) : vec3(0, -1, 0);
      L.poly({P3(I[k], -h.z), P3(O[k], -h.z), P3(O[k], h.z), P3(I[k], h.z)}, n);
    }
  } else {   // WEDGE: the profile extruded along x
    std::vector<vec2> q = wedgeProfile(p);
    int n = (int)q.size();
    for (int k = 0; k < n; ++k) {
      vec2 a = q[k], b = q[(k + 1) % n], d = b - a, o = glm::normalize(vec2(d.y, -d.x));   // the profile runs counter-clockwise in (z, y)
      L.poly({vec3(-h.x, a.y, a.x), vec3(-h.x, b.y, b.x), vec3(h.x, b.y, b.x), vec3(h.x, a.y, a.x)}, vec3(0, o.y, o.x));
    }
    for (float x : {-h.x, h.x}) { std::vector<vec3> side; for (auto& v2 : q) side.push_back(vec3(x, v2.y, v2.x)); L.poly(side, vec3(x > 0 ? 1.0f : -1.0f, 0, 0)); }
  }
}

void partHulls(const Part& p, std::vector<std::vector<vec3>>& out) {
  vec3 h = p.size * 0.5f;
  if (p.shape == Part::CYL) {
    std::vector<vec2> ring = cylRing(p, 24);
    float tp = std::clamp(p.top, 0.0f, 1.0f);
    std::vector<vec3> pts;
    for (auto& r : ring) pts.push_back(vec3(r.x, -h.y, r.y));
    if (tp < 1e-3f) pts.push_back(vec3(0, h.y, 0));
    else for (auto& r : ring) pts.push_back(vec3(r.x * tp, h.y, r.y * tp));
    out.push_back(pts);
  } else if (p.shape == Part::ARCH) {   // one convex voussoir per coarse segment
    int N = std::clamp(curveSegs(p, glm::half_pi<float>() * (p.size.x + p.size.y), 8, 32) / 2, 4, 10);
    std::vector<vec2> I, O, nI, nO;
    archProfile(p, N, I, O, nI, nO);
    for (int k = 0; k < N; ++k) {
      std::vector<vec3> pts;
      for (float z : {-h.z, h.z}) for (vec2 q : {I[k], I[k + 1], O[k], O[k + 1]}) pts.push_back(vec3(q.x, q.y, z));
      out.push_back(pts);
    }
  } else if (p.shape == Part::WEDGE) {
    std::vector<vec3> pts;
    for (auto& q : wedgeProfile(p)) for (float x : {-h.x, h.x}) pts.push_back(vec3(x, q.y, q.x));
    out.push_back(pts);
  } else if (p.shape == Part::SOFT) {   // cloth collides as its box
    std::vector<vec3> pts;
    for (int c = 0; c < 8; ++c) pts.push_back(vec3(c & 1 ? h.x : -h.x, c & 2 ? h.y : -h.y, c & 4 ? h.z : -h.z));
    out.push_back(pts);
  }
}

// ---------------------------------------------------------------- PR4 soft parts (cloth, upholstery)
static float softHash(int x, int y, int z, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2246822519u + s * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f * 2.0f - 1.0f;
}
static float softNoise(vec3 q, uint32_t s) {   // value noise, smooth, about -1..1
  vec3 i = glm::floor(q), f = q - i; f = f * f * (3.0f - 2.0f * f);
  float r = 0;
  for (int c = 0; c < 8; ++c) {
    int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
    float w = (dx ? f.x : 1 - f.x) * (dy ? f.y : 1 - f.y) * (dz ? f.z : 1 - f.z);
    r += w * softHash((int)i.x + dx, (int)i.y + dy, (int)i.z + dz, s);
  }
  return r;
}
// Puff: a point c on the box surface (half extents h) -> the soft surface (before the fit into the box).
static vec3 softPuff(const Part& p, vec3 c, vec3 h) {
  const auto& S = p.soft;
  float hy0 = h.y / (1.0f + S.puff);   // the dome adds puff * hy0 at the centre: the result fills h.y
  vec3 hb(h.x, hy0, h.z), e = glm::max(hb - vec3(p.rad), vec3(1e-4f));
  vec3 cb(c.x, c.y / h.y * hy0, c.z);
  vec3 in = glm::clamp(cb, -e, e), d = cb - in; float dl = glm::length(d);
  vec3 n = dl > 1e-6f ? d / dl : vec3(0, cb.y >= 0 ? 1.0f : -1.0f, 0);
  vec3 q = in + n * p.rad;
  float u = std::clamp(q.x / h.x, -1.0f, 1.0f), w = std::clamp(q.z / h.z, -1.0f, 1.0f), bell = (1 - u * u) * (1 - w * w);
  float top = std::clamp(q.y / hy0, -1.0f, 1.0f);   // -1 bottom .. 1 top
  q.y += top * S.puff * hy0 * bell;                                                   // domed faces
  if (top > 0) {
    q.y -= top * S.dent * h.y * std::exp(-(u * u + w * w) / 0.12f);                   // a dent in the middle (a pillow's head hollow)
    q.y -= top * S.sag * h.y * bell;                                                  // the middle sags
    if (S.tuft > 0 && S.tx > 0 && S.tz > 0)                                           // buttoned tufting: dimples on a grid
      for (int a = 0; a < S.tx; ++a) for (int b = 0; b < S.tz; ++b) {
        float bu = -1 + (2.0f * a + 1) / S.tx + ((b & 1) ? 1.0f / S.tx : 0.0f), bw = -1 + (2.0f * b + 1) / S.tz;
        if (bu > 0.999f) continue;
        float du = (u - bu) * h.x, dw = (w - bw) * h.z;
        q.y -= top * S.tuft * std::exp(-(du * du + dw * dw) / 0.0016f);
      }
  }
  vec3 side(q.x, 0, q.z); float sl = glm::length(side);
  if (S.pipe > 0 && sl > 1e-5f) q += side / sl * S.pipe * std::exp(-(q.y * q.y) / (0.0009f + 0.01f * hy0 * hy0)) * std::min(1.0f, std::fabs(n.x) + std::fabs(n.z));   // piped seam round the middle
  if (S.wr > 0) {   // wrinkles: low-frequency noise along the surface normal, stretched across x (creases)
    float k = 1.0f / S.fold;
    q += n * S.wr * (softNoise(vec3(q.x * k * 0.5f, q.y * k, q.z * k), S.seed) + 0.5f * softNoise(vec3(q.x, q.y, q.z) * (k * 2.3f), S.seed + 7));
  }
  return q;
}
// Drape: (s, t) on the unfolded sheet -> the outer surface. The sheet lies on a support whose top is ytop and whose
// half extents are (ax, az); a side in `sides` (1 -x, 2 +x, 4 -z, 8 +z) falls over a rounded edge to the hem.
static vec3 softDrape(const Part& p, float s, float t, float ax, float az, float ytop, float drop) {
  const auto& S = p.soft; float rb = std::max(0.01f, p.rad), arc = 1.5708f * rb;
  auto hang = [&](float o, float& out, float& down) {
    if (o <= 0) { out = 0; down = 0; return; }
    if (o < arc) { float f = o / rb; out = rb * std::sin(f); down = rb * (1 - std::cos(f)); }
    else { out = rb + 0.06f * (o - arc); down = rb + (o - arc); }
  };
  bool hx = s < 0 ? (S.sides & 1) : (S.sides & 2), hz = t < 0 ? (S.sides & 4) : (S.sides & 8);
  float ox = hx ? std::max(0.0f, std::fabs(s) - ax) : 0.0f, oz = hz ? std::max(0.0f, std::fabs(t) - az) : 0.0f;
  float wx = ox + oz > 1e-6f ? ox / (ox + oz) : 0.5f, wz = 1.0f - wx;   // through a corner the two sides blend (no seam)
  auto hemOf = [&](float along) { return 1.0f - 0.12f * (0.5f + 0.5f * softNoise(vec3(along * 3.1f, 0.5f, 0.0f), S.seed + 3)); };
  float k = wx * hemOf(t) + wz * hemOf(s);   // an irregular hem: the hang is shortened, not clipped (no flat rows)
  float outx, outz, down, dd; hang(ox * k, outx, dd); hang(oz * k, outz, dd); hang(std::max(ox, oz) * k, dd, down);
  vec3 q(std::copysign(std::min(std::fabs(s), ax), s) + std::copysign(outx, s), ytop - down, std::copysign(std::min(std::fabs(t), az), t) + std::copysign(outz, t));
  float hangF = std::clamp(down / std::max(drop, 1e-3f), 0.0f, 1.0f);
  if (hangF > 0 && S.wr > 0) {   // folds down the hanging sides, deeper toward the hem, at an irregular pitch
    auto fold = [&](float along, uint32_t sd) { return S.wr * hangF * (std::sin(along / S.fold * 6.2832f + 2.0f * softNoise(vec3(along * 1.7f, 0, 0), sd)) + 0.35f * softNoise(vec3(along * 9.0f, down * 4.0f, 0), sd + 5)); };
    q.x += std::copysign(wx * fold(t, S.seed + 11), s); q.z += std::copysign(wz * fold(s, S.seed + 17), t);
  }
  float topF = 1.0f - std::clamp(down / 0.04f, 0.0f, 1.0f);   // on the top (fading out over the edge)
  if (S.wr > 0) q.y += topF * 0.7f * S.wr * (softNoise(vec3(q.x / S.fold * 1.2f, 0.3f, q.z / S.fold * 0.6f), S.seed + 13) + 0.4f * softNoise(vec3(q.x / S.fold * 3.0f, 0.7f, q.z / S.fold * 3.0f), S.seed + 19));   // rumples
  if (S.puff > 0) { float u = std::clamp(q.x / ax, -1.0f, 1.0f), w = std::clamp(q.z / az, -1.0f, 1.0f); q.y += topF * S.puff * (1 - u * u) * (1 - w * w); }   // a filled duvet: puffed on top
  if (S.pleat > 0) {   // curtains: pleats across x (a sheet turned upright hangs in vertical folds), uneven pitch, deeper toward local +z (the hem)
    float w = std::clamp(0.5f + 0.5f * t / std::max(az, 1e-3f), 0.0f, 1.0f);
    q.y += topF * S.pleat * (0.7f + 0.3f * w) * std::sin(q.x / S.fold * 6.2832f + 1.4f * softNoise(vec3(q.x * 1.3f, 0.2f, t * 0.8f), S.seed + 23));
  }
  return q;
}
static void appendSoft(MeshData& m, const Part& p, const Material& mat, const mat4& xf) {
  vec3 h = p.size * 0.5f;
  struct G { std::vector<vec3> P, N; int nu, nw; vec3 out; };
  std::vector<G> grids;
  auto steps = [](float len, float st, int lo, int hi) { return std::clamp((int)std::ceil(len / st), lo, hi); };
  if (!p.soft.drape) {
    for (int ax = 0; ax < 3; ++ax)
      for (int sg = -1; sg <= 1; sg += 2) {
        int u = (ax + 1) % 3, w = (ax + 2) % 3;
        G g; g.nu = steps(2 * h[u], 0.035f, 4, 28); g.nw = steps(2 * h[w], 0.035f, 4, 28); g.out = vec3(0); g.out[ax] = (float)sg;
        auto F = [&](float a, float b) { vec3 c(0); c[ax] = sg * h[ax]; c[u] = a; c[w] = b; return softPuff(p, c, h); };
        for (int j = 0; j <= g.nw; ++j) for (int i = 0; i <= g.nu; ++i) {
          float a = -h[u] + 2 * h[u] * i / g.nu, b = -h[w] + 2 * h[w] * j / g.nw, eu = 1e-3f * h[u], ew = 1e-3f * h[w];
          vec3 du = F(a + eu, b) - F(a - eu, b), dw = F(a, b + ew) - F(a, b - ew), n = glm::cross(du, dw);
          if (glm::dot(n, g.out) < 0) n = -n;
          g.P.push_back(F(a, b)); g.N.push_back(glm::length(n) > 1e-12f ? glm::normalize(n) : g.out);
        }
        grids.push_back(std::move(g));
      }
  } else {
    const auto& S = p.soft;
    float drop = std::max(0.0f, S.drop > 0 ? S.drop : p.size.y - S.th), ax = std::max(0.02f, h.x - (S.sides & 3 ? p.rad + 0.02f : 0.0f)), az = std::max(0.02f, h.z - (S.sides & 12 ? p.rad + 0.02f : 0.0f));
    float s0 = -(ax + ((S.sides & 1) ? drop + 0.6f * p.rad : 0.0f)), s1 = ax + ((S.sides & 2) ? drop + 0.6f * p.rad : 0.0f);
    float t0 = -(az + ((S.sides & 4) ? drop + 0.6f * p.rad : 0.0f)), t1 = az + ((S.sides & 8) ? drop + 0.6f * p.rad : 0.0f);
    G g; g.nu = steps(s1 - s0, 0.045f, 6, 40); g.nw = steps(t1 - t0, 0.045f, 6, 40); g.out = vec3(0, 1, 0);
    auto F = [&](float a, float b) { return softDrape(p, a, b, ax, az, h.y, drop); };
    std::vector<vec3> P, N;
    for (int j = 0; j <= g.nw; ++j) for (int i = 0; i <= g.nu; ++i) {
      float a = s0 + (s1 - s0) * i / g.nu, b = t0 + (t1 - t0) * j / g.nw, e = 1e-3f;
      vec3 n = glm::cross(F(a, b + e) - F(a, b - e), F(a + e, b) - F(a - e, b));
      P.push_back(F(a, b)); N.push_back(glm::length(n) > 1e-12f ? glm::normalize(n) : vec3(0, 1, 0));
    }
    G in = g; in.out = vec3(0, -1, 0);   // the underside, the cloth's thickness inward
    for (size_t k = 0; k < P.size(); ++k) { g.P.push_back(P[k]); g.N.push_back(N[k]); in.P.push_back(P[k] - N[k] * S.th); in.N.push_back(-N[k]); }
    grids.push_back(std::move(g)); grids.push_back(std::move(in));
  }
  // fit: every point mapped into the part's box (so the mesh fills it exactly: colliders and envelopes stay the box)
  vec3 lo(1e9f), hi(-1e9f);
  for (auto& g : grids) for (auto& q : g.P) { lo = glm::min(lo, q); hi = glm::max(hi, q); }
  vec3 sc = (2.0f * h) / glm::max(hi - lo, vec3(1e-5f)), mid = 0.5f * (lo + hi);
  LocalMesh L(m, p, mat, xf);
  for (auto& g : grids) {
    uint32_t base = (uint32_t)m.v.size();
    for (size_t k = 0; k < g.P.size(); ++k) L.v((g.P[k] - mid) * sc, glm::normalize(g.N[k] / sc));
    for (int j = 0; j < g.nw; ++j) for (int i = 0; i < g.nu; ++i) {
      uint32_t a = base + j * (g.nu + 1) + i, b = a + 1, c = a + g.nu + 2, d = a + g.nu + 1;
      vec3 out = glm::normalize(m.v[a].nrm + m.v[c].nrm);   // world-space: wind by the vertex normals
      auto tri = [&](uint32_t x, uint32_t y, uint32_t z) { vec3 X = m.v[x].pos; if (glm::dot(glm::cross(m.v[y].pos - X, m.v[z].pos - X), out) < 0) std::swap(y, z); m.i.insert(m.i.end(), {x, y, z}); };
      tri(a, b, c); tri(a, c, d);
    }
  }
  if (p.soft.drape) {   // the hem / edges: a strip joining the outer sheet to its underside round the border
    const G& g = grids[0]; const G& in = grids[1];
    uint32_t bo = (uint32_t)(m.v.size() - g.P.size() - in.P.size()), bi = bo + (uint32_t)g.P.size();
    auto edge = [&](int i0, int j0, int i1, int j1, vec3 outLocal) {
      vec3 pa = m.v[bo + j0 * (g.nu + 1) + i0].pcoord, pb = m.v[bo + j1 * (g.nu + 1) + i1].pcoord, pc = m.v[bi + j1 * (g.nu + 1) + i1].pcoord, pd = m.v[bi + j0 * (g.nu + 1) + i0].pcoord;
      vec3 n = glm::cross(pb - pa, pd - pa); n = glm::length(n) > 1e-12f ? glm::normalize(n) : glm::normalize(outLocal);   // part-local
      if (glm::dot(n, outLocal) < 0) n = -n;
      L.quad(L.v(pa, n), L.v(pb, n), L.v(pc, n), L.v(pd, n), n);
    };
    for (int i = 0; i < g.nu; ++i) { edge(i, 0, i + 1, 0, vec3(0, -0.3f, -1)); edge(i, g.nw, i + 1, g.nw, vec3(0, -0.3f, 1)); }
    for (int j = 0; j < g.nw; ++j) { edge(0, j, 0, j + 1, vec3(-1, -0.3f, 0)); edge(g.nu, j, g.nu, j + 1, vec3(1, -0.3f, 0)); }
  }
}

void appendPart(MeshData& m, const Part& p, const Material& mat, const mat4& xf) {
  if (!p.visible || p.bag) return;   // HB1b: sandbags draw as bag meshes (src/sandbags.cpp)
  if (p.shape == Part::SOFT) { appendSoft(m, p, mat, xf); return; }
  if (p.shape == Part::ROUND || (p.shape == Part::BOX && p.bv > 0)) { appendRound(m, p, mat, xf); return; }
  if (p.shape == Part::CYL || p.shape == Part::ARCH || p.shape == Part::WEDGE) { appendCurved(m, p, mat, xf); return; }
  if (p.shape == Part::BOX) {
    vec3 h = p.size * 0.5f;
    for (int ax = 0; ax < 3; ++ax)
      for (int s = -1; s <= 1; s += 2) {
        vec3 n(0); n[ax] = (float)s;
        int u = (ax + 1) % 3, w = (ax + 2) % 3;
        vec3 q[4];
        float su[4] = {-1, 1, 1, -1}, sw[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; ++k) {
          vec3 c(0); c[ax] = s * h[ax]; c[u] = su[k] * h[u]; c[w] = sw[k] * h[w]; q[k] = c;
        }
        pushQuad(m, p, mat, xf, q, n, false);
      }
  } else {
    vec3 A[3], B[3];
    for (int k = 0; k < 3; ++k) { A[k] = prismPoint(p.axis, p.a0, p.tri[k]); B[k] = prismPoint(p.axis, p.a1, p.tri[k]); }
    vec3 cen = (A[0] + A[1] + A[2] + B[0] + B[1] + B[2]) / 6.0f;
    auto face = [&](vec3 q0, vec3 q1, vec3 q2, vec3 q3) {
      vec3 n = glm::normalize(glm::cross(q1 - q0, q2 - q0));
      if (glm::dot(n, (q0 + q2) * 0.5f - cen) < 0) n = -n;
      vec3 q[4] = {q0, q1, q2, q3};
      pushQuad(m, p, mat, xf, q, n, true);
    };
    face(A[0], A[1], A[2], A[2]);
    face(B[0], B[1], B[2], B[2]);
    for (int k = 0; k < 3; ++k) { int j = (k + 1) % 3; face(A[k], A[j], B[j], B[k]); }
  }
}

static int patternFromName(const std::string& s) {
  static const std::map<std::string, int> m = {
    {"plain", PAT_PLAIN}, {"stone", PAT_STONE}, {"wood", PAT_WOOD}, {"plaster", PAT_PLASTER},
    {"slate", PAT_SLATE}, {"tile", PAT_TILE}, {"metal", PAT_METAL}, {"glass", PAT_GLASS},
    {"fabric", PAT_FABRIC}, {"brick", PAT_BRICK}, {"painted", PAT_PAINTED}, {"concrete", PAT_CONCRETE}, {"bunker", PAT_BUNKER}, {"net", PAT_NET},
    {"limewash", PAT_LIMEWASH}, {"brickwork", PAT_BRICKWORK}, {"ashlar", PAT_ASHLAR}, {"interior", PAT_INTERIOR}, {"floorfin", PAT_FLOORFIN},
    {"rubble", PAT_RUBBLE}, {"rug", PAT_RUG}, {"ceramic", PAT_CERAMIC}, {"joinery", PAT_JOINERY}, {"paintwood", PAT_PAINTWOOD},
    {"paper", PAT_PAPER}, {"textile", PAT_TEXTILE}, {"rock", PAT_ROCK}};
  auto it = m.find(s); return it == m.end() ? PAT_PLAIN : it->second;
}

// MOD2: parts marked "mf" are the stand-in for an imported model: hidden once its bake is on disk (a local-only
// model keeps a part-built fallback of the same footprint in the committed asset)
static thread_local bool gModelBaked = false;
static bool modelBakedFor(const json& j, const std::string& id) {
  if (!j.is_object() || !j.contains("model") || !j["model"].is_object()) return false;
  std::error_code ec; return fs::exists(contentDir() / "assets" / "models" / jkey(j, "model").value("dir", id) / "mesh.bin", ec);
}
static Part parsePart(const json& j, const std::map<std::string, int>& mats);
std::vector<Part> assetRawParts(const AssetDef& a) {
  std::map<std::string, int> mats;
  for (size_t k = 0; k < a.materials.size(); ++k) mats[a.materials[k].name] = (int)k;
  std::vector<Part> out;
  gModelBaked = modelBakedFor(a.raw, a.id);
  if (a.raw.is_object()) for (auto& pj : a.raw.value("parts", json::array())) out.push_back(parsePart(pj, mats));
  gModelBaked = false;
  return out;
}
static Part parsePart(const json& j, const std::map<std::string, int>& mats) {
  Part p;
  std::string t = j.value("t", "b");
  auto mit = mats.find(j.value("m", ""));
  p.mat = mit == mats.end() ? 0 : mit->second;
  p.visible = j.value("v", 1) != 0 && !(gModelBaked && j.value("mf", 0) != 0);
  p.standIn = j.value("v", 1) != 0 && !p.visible;
  if (j.contains("q")) p.rot = glm::normalize(quat(j["q"][3].get<float>(), j["q"][0].get<float>(), j["q"][1].get<float>(), j["q"][2].get<float>()));   // explicit quaternion (x, y, z, w)
  p.collide = j.value("k", 1) != 0; p.playerOnly = j.value("k", 1) == 2; p.sec = j.value("sec", -1); p.grp = j.value("g", -1);
  p.lodDetail = j.value("lod", 0) >= 1;
  if (t == "p") {
    p.shape = Part::PRISM;
    p.axis = j.value("axis", 0);
    p.a0 = j["a"][0]; p.a1 = j["a"][1];
    for (int k = 0; k < 3; ++k) p.tri[k] = vec2(j["tri"][k][0].get<float>(), j["tri"][k][1].get<float>());
    // center/size used for bounds
    vec3 mn(1e9f), mx(-1e9f);
    for (int k = 0; k < 3; ++k) for (float a : {p.a0, p.a1}) { vec3 q = prismPoint(p.axis, a, p.tri[k]); mn = glm::min(mn, q); mx = glm::max(mx, q); }
    p.center = (mn + mx) * 0.5f; p.size = mx - mn;
  } else {
    p.center = jvec3(j["c"]); p.size = jvec3(jkey(j, "s"), vec3(1));
    float half = 0.5f * std::min({p.size.x, p.size.y, p.size.z});
    if (t == "r") { p.shape = Part::ROUND; p.rad = std::clamp(j.value("rad", 0.1f), 0.0f, half); }
    else if (t == "c" || t == "k") { p.shape = Part::CYL; p.sides = j.value("n", 0); p.top = std::clamp(j.value("top", t == "k" ? 0.0f : 1.0f), 0.0f, 1.0f); p.seg = j.value("seg", 0); }
    else if (t == "a") { p.shape = Part::ARCH; p.th = j.value("th", 0.25f); p.fill = j.value("fill", 0) != 0; p.seg = j.value("seg", 0); }
    else if (t == "w") { p.shape = Part::WEDGE; p.top = std::clamp(j.value("top", 0.0f), 0.0f, 1.0f); }
    else if (t == "s" || t == "d") {   // PR4 soft parts
      p.shape = Part::SOFT; auto& S = p.soft; S.drape = t == "d";
      p.rad = std::clamp(j.value("rad", S.drape ? 0.03f : 0.25f * half), 0.0f, half);
      S.puff = j.value("puff", 0.0f); S.dent = j.value("dent", 0.0f); S.sag = j.value("sag", 0.0f); S.tuft = j.value("tuft", 0.0f);
      S.pipe = j.value("pipe", 0.0f); S.wr = j.value("wr", 0.0f); S.drop = j.value("drop", 0.0f); S.th = j.value("th", 0.02f);
      S.fold = std::max(0.08f, j.value("fold", 0.3f)); S.pleat = j.value("pleat", 0.0f); S.tx = j.value("tx", 0); S.tz = j.value("tz", 0); S.sides = j.value("sides", 15);
      S.seed = j.value("seed", (uint32_t)(std::fabs(p.center.x * 977.0f + p.center.y * 131.0f + p.center.z * 613.0f)));
    }
    else p.bv = std::clamp(j.value("bv", 0.0f), 0.0f, half);
    if (j.contains("r")) {
      vec3 r = jvec3(j["r"]);
      p.rot = glm::quat(glm::radians(r));
    }
    if ((p.shape == Part::BOX || p.shape == Part::ROUND) && p.visible) p.bag = (uint8_t)sandbagKind(j.value("m", std::string()), p.size);   // HB1b
  }
  return p;
}

static bool isGlass(const AssetDef& a, const Part& p) { return a.materials[p.mat].pattern == PAT_GLASS; }
static void partCorners(const Part& p, std::vector<vec3>& out);
// PF2 asset LOD meshes (AssetDef::lodMesh). Same parts, materials and vertex format as the opaque mesh, so the shaders
// and patterns are unchanged; only what is drawn far away gets lighter. A level is kept only if it saves >= 25 % of the
// triangles of the finer one.
static void buildAssetLods(AssetDef& a, size_t fullTris, const MeshData& walls) {
  if (a.path.on || a.model || a.parts.size() < 12) return;
  const float aDiag = glm::length(a.bmax - a.bmin);
  // BT2: lodKeep (m): the smallest part LOD2 keeps (default 12 % of the diagonal: on a 170 m scenic landmark that dropped
  // its towers' upper stages, caps and window bays, the silhouette, from 300 m)
  const float keep2 = a.raw.is_object() ? a.raw.value("lodKeep", std::max(1.5f, 0.12f * aDiag)) : std::max(1.5f, 0.12f * aDiag);
  const size_t nStatic = a.furnitureNames.empty() ? a.parts.size() : std::min(a.furnFirst, a.parts.size());
  MeshData m[2], full, glass; std::vector<vec3> cs;
  for (size_t k = 0; k < nStatic; ++k) {
    const Part& p = a.parts[k];
    if (!p.visible) continue;
    if (isGlass(a, p)) { appendPart(glass, p, a.materials[p.mat], mat4(1)); continue; }
    appendPart(full, p, a.materials[p.mat], mat4(1));
    cs.clear(); partCorners(p, cs);
    vec3 lo(1e9f), hi(-1e9f); for (auto& c : cs) { lo = glm::min(lo, c); hi = glm::max(hi, c); }
    float d = cs.empty() ? 0.0f : glm::length(hi - lo);
    Part q = p;   // simplified: plain boxes instead of bevels / rounding, few curve segments
    q.bv = 0; if (q.shape == Part::ROUND || q.shape == Part::SOFT) q.shape = Part::BOX;
    if (q.shape == Part::CYL && q.sides == 0) q.sides = 8;
    if (q.shape == Part::ARCH) q.seg = std::min(q.seg > 0 ? q.seg : 6, 6);
    if (!p.lodDetail && d >= 0.35f) appendPart(m[0], q, a.materials[p.mat], mat4(1));
    if (!p.lodDetail && d >= keep2) appendPart(m[1], q, a.materials[p.mat], mat4(1));
  }
  size_t t1 = m[0].i.size() / 3, t2 = m[1].i.size() / 3;
  if (t1 > 0 && t1 * 4 <= fullTris * 3) a.lodMesh[0].upload(m[0]); else t1 = fullTris;
  if (t2 > 0 && t2 * 4 <= t1 * 3) a.lodMesh[1].upload(m[1]);
  // HLOD source (hlod.cpp): buildings and walls that draw on the ordinary path (no snap chains, stations, models)
  if ((a.enterable || !a.panels.empty()) && a.station.kind.empty() && !a.tree.on && a.nature.kind == NatureDef::NONE && !a.raw.contains("snap")) {
    auto farMesh = std::make_shared<MeshData>(a.lodMesh[0].count ? m[0] : full);   // LOD1: HLOD starts at the LOD1 range
    uint32_t base = (uint32_t)farMesh->v.size();
    farMesh->v.insert(farMesh->v.end(), walls.v.begin(), walls.v.end());
    for (uint32_t k : walls.i) farMesh->i.push_back(base + k);
    if (!farMesh->i.empty()) a.hlodOpaque = farMesh;
    if (!glass.i.empty()) a.hlodGlass = std::make_shared<MeshData>(std::move(glass));
  }
}

static void partCorners(const Part& p, std::vector<vec3>& out) {
  if (p.shape != Part::PRISM) {
    vec3 h = p.size * 0.5f;
    for (int k = 0; k < 8; ++k) out.push_back(p.center + p.rot * vec3(k & 1 ? h.x : -h.x, k & 2 ? h.y : -h.y, k & 4 ? h.z : -h.z));
  } else {
    for (int k = 0; k < 3; ++k) { out.push_back(prismPoint(p.axis, p.a0, p.tri[k])); out.push_back(prismPoint(p.axis, p.a1, p.tri[k])); }
  }
}

int AssetDef::findInteract(const std::string& iid) const {
  for (size_t k = 0; k < interactables.size(); ++k) if (interactables[k].id == iid) return (int)k;
  return -1;
}

// Structural wall / floor slabs that were authored as plain parts (piers between openings, corners and quoins,
// lintels, sills, upper floors) become damage-field bays, so every part of a wall takes impact-centred damage.
// Parts sharing one face rectangle and touching along the thin axis are stacked as the layers of one bay
// (stone + plaster skin). Furniture and foundations are left alone.
static void extractTrim(AssetDef& a, const json& j, std::vector<Panel>& out, vec3 mid);
static void extractRoofPanels(AssetDef& a, std::vector<Panel>& out);
static void extractWallPanels(AssetDef& a, std::vector<Panel>& out, const json& j) {
  auto matName = [&](const Part& p) { return p.mat >= 0 && p.mat < (int)a.materials.size() ? a.materials[p.mat].name : std::string(); };
  auto isWallMat = [](const std::string& m) {
    static const char* k[] = {"stone", "stone_dressed", "plaster", "plaster_smoke", "render", "concrete", "concrete_wash", "brick", "plank_y", "plank_x", "plank_z", "log_x", "log_z"};
    for (auto* s : k) if (m == s) return true;
    return false;
  };
  auto isFloorMat = [](const std::string& m) { return m == "floor_x" || m == "concrete_floor" || m == "plank_x" || m == "plank_z"; };
  vec3 lo(1e9f), hi(-1e9f);
  for (auto& p : a.parts) { lo = glm::min(lo, p.center - p.size * 0.5f); hi = glm::max(hi, p.center + p.size * 0.5f); }
  vec3 mid = (lo + hi) * 0.5f;
  struct Cand { size_t idx; int n; float u0, u1, v0, v1, d0, d1; };
  std::vector<Cand> cands;
  for (size_t i = 0; i < a.parts.size(); ++i) {
    const Part& p = a.parts[i];
    if (p.shape != Part::BOX || p.bv > 0 || std::fabs(std::fabs(p.rot.w) - 1.0f) > 1e-4f || !p.collide || !p.visible) continue;   // bevelled = trim (kit rule), never a bay
    std::string m = matName(p);
    int n = p.size.x <= p.size.y && p.size.x <= p.size.z ? 0 : p.size.y <= p.size.z ? 1 : 2;
    int u = n == 1 ? 0 : (n == 2 ? 0 : 2), v = n == 1 ? 2 : 1;
    if (p.size[n] > 1.2f || p.size[u] < 0.25f || p.size[v] < 0.25f) continue;
    if (n == 1) { if (!isFloorMat(m) || p.center.y - p.size.y * 0.5f < 0.5f) continue; }   // upper floors only, never foundations
    else if (!isWallMat(m)) continue;
    cands.push_back({i, n, p.center[u] - p.size[u] * 0.5f, p.center[u] + p.size[u] * 0.5f, p.center[v] - p.size[v] * 0.5f, p.center[v] + p.size[v] * 0.5f,
                     p.center[n] - p.size[n] * 0.5f, p.center[n] + p.size[n] * 0.5f});
  }
  std::vector<bool> taken(cands.size(), false), remove(a.parts.size(), false);
  auto same = [](float x, float y) { return std::fabs(x - y) < 0.012f; };
  for (size_t c = 0; c < cands.size(); ++c) {
    if (taken[c]) continue;
    std::vector<size_t> grp{c}; taken[c] = true;
    float d0 = cands[c].d0, d1 = cands[c].d1;
    for (bool grew = true; grew;) {   // stack layers that share the face rectangle and touch along the thin axis
      grew = false;
      for (size_t o = 0; o < cands.size(); ++o) {
        if (taken[o] || cands[o].n != cands[c].n || !same(cands[o].u0, cands[c].u0) || !same(cands[o].u1, cands[c].u1) || !same(cands[o].v0, cands[c].v0) || !same(cands[o].v1, cands[c].v1)) continue;
        if (!(same(cands[o].d1, d0) || same(cands[o].d0, d1))) continue;
        taken[o] = true; grp.push_back(o); d0 = std::min(d0, cands[o].d0); d1 = std::max(d1, cands[o].d1); grew = true;
      }
    }
    Panel pn;
    pn.id = "auto/" + std::to_string(cands[c].idx);
    float thick = 0; std::string fam = "masonry";
    for (size_t g : grp) {
      const Part& p = a.parts[cands[g].idx];
      pn.parts[0].push_back(p); remove[cands[g].idx] = true;
      if (p.size[cands[c].n] > thick) {
        thick = p.size[cands[c].n]; std::string m = matName(p);
        fam = m.rfind("concrete", 0) == 0 ? "concrete" : (m.rfind("plank", 0) == 0 || m.rfind("log", 0) == 0 || m == "floor_x") ? "timber" : "masonry";
      }
    }
    pn.family = fam; pn.hp = fam == "concrete" ? 450.0f : fam == "timber" ? 90.0f : 200.0f;
    pn.bmin = vec3(1e9f); pn.bmax = vec3(-1e9f);
    for (auto& p : pn.parts[0]) { pn.bmin = glm::min(pn.bmin, p.center - p.size * 0.5f); pn.bmax = glm::max(pn.bmax, p.center + p.size * 0.5f); }
    int n = cands[c].n;
    pn.normal = vec3(0); pn.normal[n] = n == 1 ? 1.0f : ((pn.bmin[n] + pn.bmax[n]) * 0.5f >= mid[n] ? 1.0f : -1.0f);   // walls face outward
    out.push_back(std::move(pn));
  }
  std::vector<Part> keep;
  for (size_t i = 0; i < a.parts.size(); ++i) if (!remove[i]) keep.push_back(a.parts[i]);
  a.parts.swap(keep);
  extractRoofPanels(a, out);
  extractTrim(a, j, out, mid);
}

// GM3: pitched roofs are destructible from above. Every pitched roof slab (a tilted box or rounded box of a roof
// material, thin and broad) becomes a rotated bay (Panel::rot): the damage field works in the slab's own frame
// (wallfield.cpp), so shells and blasts landing on a roof punch through it like a wall. The roof courses laid on the
// slab (the GX4 tile / slate strips, detail only) go into the same bay and are cut with it, so nothing floats over a
// hole. Hip roofs (pyramids) and spires stay static.
static void extractRoofPanels(AssetDef& a, std::vector<Panel>& out) {
  auto matName = [&](const Part& p) { return p.mat >= 0 && p.mat < (int)a.materials.size() ? a.materials[p.mat].name : std::string(); };
  auto roofMat = [&](const Part& p) { std::string m = matName(p); return m == "slate" || m == "tile" || m == "thatch" || m.rfind("roof", 0) == 0; };
  std::vector<bool> remove(a.parts.size(), false);
  for (size_t i = 0; i < a.parts.size(); ++i) {
    const Part& s = a.parts[i];
    if ((s.shape != Part::BOX && s.shape != Part::ROUND) || !s.collide || !s.visible || !roofMat(s)) continue;
    vec3 up = s.rot * vec3(0, 1, 0);
    if (std::fabs(std::fabs(s.rot.w) - 1.0f) < 1e-4f || up.y < 0.2f || up.y > 0.985f) continue;   // pitched, not flat or vertical
    if (s.size.y > 0.6f || s.size.x < 0.8f || s.size.z < 0.8f) continue;                          // a slab: thin, broad
    Panel pn;
    pn.id = "roof/" + std::to_string(i); pn.family = "timber"; pn.hp = 90.0f;
    pn.rot = true; pn.q = s.rot; pn.o = s.center;
    pn.lmin = s.center - s.size * 0.5f; pn.lmax = s.center + s.size * 0.5f;
    pn.parts[0].push_back(s); remove[i] = true;
    for (size_t k = 0; k < a.parts.size(); ++k) {   // the courses on it: same material, detail only, on its top face
      const Part& c = a.parts[k];
      if (k == i || remove[k] || c.collide || c.shape != Part::BOX || matName(c) != matName(s)) continue;
      if (std::fabs(glm::dot(c.rot, s.rot)) < 0.98f) continue;
      vec3 l = panelToLocal(pn, c.center);
      if (l.x < pn.lmin.x || l.x > pn.lmax.x || l.z < pn.lmin.z || l.z > pn.lmax.z || l.y < pn.lmax.y - 0.06f || l.y > pn.lmax.y + 0.15f) continue;
      pn.parts[0].push_back(c); remove[k] = true;
      pn.lmax.y = std::max(pn.lmax.y, l.y + c.size.y * 0.5f);
    }
    pn.normal = s.rot * vec3(0, 1, 0);
    pn.bmin = vec3(1e9f); pn.bmax = vec3(-1e9f);
    for (int c = 0; c < 8; ++c) {
      vec3 l((c & 1) ? pn.lmax.x : pn.lmin.x, (c & 2) ? pn.lmax.y : pn.lmin.y, (c & 4) ? pn.lmax.z : pn.lmin.z), w = panelToAsset(pn, l);
      pn.bmin = glm::min(pn.bmin, w); pn.bmax = glm::max(pn.bmax, w);
    }
    out.push_back(std::move(pn));
  }
  std::vector<Part> keep;
  for (size_t i = 0; i < a.parts.size(); ++i) if (!remove[i]) keep.push_back(a.parts[i]);
  a.parts.swap(keep);
}

// Opening trim (docs/destruction.md#trim): the frame, jambs, head, sills, lintels, hood moulds and threshold of every
// door / window interactable are static parts outside the bays. They are found by adjacency to the opening (the
// interactables' aperture boxes give its rectangle in the wall plane, the bays around it the wall slab) and by
// material, and become one trim bay per opening that breaks whole (wallfield.cpp: blasts and shells within its radius,
// the bays under or beside it giving way, a tank ramming it). Iron doors and steel shutters keep their frames.
static void extractTrim(AssetDef& a, const json& j, std::vector<Panel>& out, vec3 mid) {
  auto matName = [&](const Part& p) { return p.mat >= 0 && p.mat < (int)a.materials.size() ? a.materials[p.mat].name : std::string(); };
  auto trimMat = [&](const Part& p) {
    if (p.mat < 0 || p.mat >= (int)a.materials.size() || a.materials[p.mat].pattern == PAT_GLASS) return false;
    std::string m = matName(p);
    static const char* no[] = {"terracotta", "floor_x", "concrete_floor", "slate", "ridge_tile", "stone_plinth", "zinc", "soot", "hearthstone", "tread_x"};
    for (auto* s : no) if (m == s) return false;
    return m.rfind("concrete", 0) != 0 && m.rfind("steel", 0) != 0 && m.find("bunker") == std::string::npos;
  };
  struct Op { std::vector<int> ks; int an; float u0, u1, v0, v1, e0, e1; };   // e: aperture depth along the normal
  std::vector<Op> ops;
  const json ij = j.value("interactables", json::array());
  for (size_t k = 0; k < ij.size(); ++k) {
    const json& d = ij[k];
    if (d.value("sound", "wood") == "steel" || !d.contains("aperture")) continue;
    vec3 mn = jvec3(d["aperture"]["min"]), mx = jvec3(d["aperture"]["max"]), s = mx - mn;
    if (s.y < 0.3f || s.y < std::min(s.x, s.z)) continue;   // hatches and flaps lying flat: not a wall opening
    int an = s.x < s.z ? 0 : 2, au = an == 0 ? 2 : 0;
    Op o{{(int)k}, an, mn[au], mx[au], mn.y, mx.y, mn[an], mx[an]};
    bool merged = false;
    for (auto& q : ops) {   // a window's casement and its shutters are one opening
      if (q.an != an || o.u0 > q.u1 + 0.05f || o.u1 < q.u0 - 0.05f || o.v0 > q.v1 + 0.05f || o.v1 < q.v0 - 0.05f) continue;
      if (std::fabs(0.5f * (o.e0 + o.e1) - 0.5f * (q.e0 + q.e1)) > 1.3f) continue;
      q.ks.push_back((int)k); q.u0 = std::min(q.u0, o.u0); q.u1 = std::max(q.u1, o.u1); q.v0 = std::min(q.v0, o.v0); q.v1 = std::max(q.v1, o.v1);
      q.e0 = std::min(q.e0, o.e0); q.e1 = std::max(q.e1, o.e1); merged = true; break;
    }
    if (!merged) ops.push_back(o);
  }
  if (ops.empty()) return;
  // wall bays (authored and converted) as boxes, for the wall slab each opening is cut through
  std::vector<std::pair<vec3, vec3>> bays;
  for (auto& pn : out) bays.push_back({pn.bmin, pn.bmax});
  for (auto& pj : j.value("panels", json::array())) bays.push_back({jvec3(pj["min"]), jvec3(pj["max"])});
  std::vector<bool> taken(a.parts.size(), false);
  for (auto& o : ops) {
    int an = o.an, au = an == 0 ? 2 : 0;
    float d0 = 1e9f, d1 = -1e9f;
    for (auto& [bmn, bmx] : bays) {
      vec3 s = bmx - bmn;
      if (!(s[an] <= s[au] && s[an] <= s.y) || s[an] > 1.3f) continue;   // a wall facing along this normal
      if (bmn[au] > o.u1 + 0.1f || bmx[au] < o.u0 - 0.1f || bmn.y > o.v1 + 0.1f || bmx.y < o.v0 - 0.1f) continue;
      if (bmx[an] < o.e0 - 0.7f || bmn[an] > o.e1 + 0.7f) continue;
      d0 = std::min(d0, bmn[an]); d1 = std::max(d1, bmx[an]);
    }
    if (d0 > d1) { d0 = o.e0 - 0.3f; d1 = o.e1 + 0.3f; }
    Panel pn;
    pn.id = "trim/" + ij[o.ks[0]].value("id", std::string("opening"));
    pn.family = "trim"; pn.hp = 60.0f; pn.trim = (int)a.trims.size();
    pn.bmin = vec3(1e9f); pn.bmax = vec3(-1e9f);
    for (size_t i = 0; i < a.parts.size(); ++i) {
      const Part& p = a.parts[i];
      if (taken[i] || (!p.visible && !p.collide) || p.playerOnly || !trimMat(p)) continue;
      std::vector<vec3> cs; partCorners(p, cs);
      vec3 mn(1e9f), mx(-1e9f); for (auto& c : cs) { mn = glm::min(mn, c); mx = glm::max(mx, c); }
      if (mx[an] < d0 - 0.08f || mn[an] > d1 + 0.08f) continue;                                   // in the wall slab (sill noses, hood moulds)
      if (mn[au] < o.u0 - 0.45f || mx[au] > o.u1 + 0.45f || mn.y < o.v0 - 0.45f || mx.y > o.v1 + 0.45f) continue;   // not a wall-sized slab
      if (mx[au] < o.u0 - 0.12f || mn[au] > o.u1 + 0.12f || mx.y < o.v0 - 0.1f || mn.y > o.v1 + 0.12f) continue;   // touches the opening
      taken[i] = true;
      pn.parts[0].push_back(p);
      pn.bmin = glm::min(pn.bmin, mn); pn.bmax = glm::max(pn.bmax, mx);
    }
    if (pn.parts[0].empty()) continue;
    pn.normal = vec3(0); pn.normal[an] = 0.5f * (d0 + d1) >= mid[an] ? 1.0f : -1.0f;
    TrimGroup g;
    g.interact = o.ks; g.an = an; g.u0 = o.u0; g.u1 = o.u1; g.v0 = o.v0; g.v1 = o.v1; g.d0 = d0; g.d1 = d1;
    g.bmin = pn.bmin; g.bmax = pn.bmax; g.parts = (int)pn.parts[0].size();
    a.trims.push_back(g);
    out.push_back(std::move(pn));
  }
  std::vector<Part> keep;
  for (size_t i = 0; i < a.parts.size(); ++i) if (!taken[i]) keep.push_back(a.parts[i]);
  a.parts.swap(keep);
}

json furnitureModelFor(const std::string& id) {   // PR2: content/settings/furniture_models.json, id -> model block ("_ref": notes, dropped)
  static json over = [] { json o; try { o = json::parse(readTextFile(contentDir() / "settings" / "furniture_models.json")); } catch (...) {} return o.is_object() ? o : json::object(); }();
  auto f = over.find(id);
  if (f == over.end() || !f->is_object()) return json();
  json m = *f; m.erase("_ref"); return m;
}

// the catalogue fields every asset has before (and after) its geometry is built: from raw
static void readHeader(AssetDef& a) {
  const json& j = a.raw;
  a.vehicle = j.is_object() && j.contains("vehicle");
  a.version = j.value("version", 1);
  a.name = j.value("name", a.id);
  a.category = j.value("category", "Misc");
  a.description = j.value("description", "");
  a.floors = j.value("floors", 1);
  a.enterable = j.value("enterable", false);
  if (j.contains("footprint")) a.footprint = vec2(j["footprint"][0].get<float>(), j["footprint"][1].get<float>());
  a.plinthDepth = j.value("plinthDepth", 1.0f);
  a.maxGrade = j.value("maxGrade", 1.0f);
}
// MEM2: back to the header only (GPU meshes and buffers, CPU geometry, collision freed); the next find builds it again.
// Only for an asset nothing uses (the palette's tile renders): placed copies, the placing ghost and caches hold its parts.
void AssetLibrary::release(AssetDef& a) const {
  if (!a.built || a.vehicle || a.placeholder || a.raw.contains("rack")) return;
  if (a.tree.on || !a.ivy.blocks.empty()) return;   // card foliage: a rebuild came back bare (shortcut: the leaf cards are a few MB; find what keeps the old cards before releasing them). Model foliage (NT8) is released: 586 MB on Outpost Woods
  for (GpuMesh* m : {&a.opaque, &a.glass, &a.colliderGhost, &a.defBrokenMesh, &a.panelsIntact, &a.panelsLod[0], &a.panelsLod[1], &a.noFurnOpaque, &a.noFurnGlass,
                     &a.lodMesh[0], &a.lodMesh[1], &a.station.yawMesh, &a.station.elevMesh, &a.station.recoilMesh, &a.tree.stumpMesh, &a.tree.trunkMesh}) m->destroy();
  for (auto& it : a.interactables) for (auto& l : it.leaves) { l.opaque.destroy(); l.glass.destroy(); }
  for (auto& pn : a.panels) for (auto& m : pn.mesh) m.destroy();
  for (GLuint* b : {&a.tree.cardVbo, &a.tree.cardToneVbo, &a.tree.leafInst}) if (*b) { glDeleteBuffers(1, b); *b = 0; }
  for (GLuint* v : {&a.tree.cardVao, &a.tree.leafVao}) if (*v) { glDeleteVertexArrays(1, v); *v = 0; }
  for (auto* m : {&a.model, &a.modelLod}) if (*m && m->use_count() == 1) (*m)->destroy();   // attached / shared models stay (their cache)
  AssetDef h; h.file = a.file; h.id = a.id; h.raw = std::move(a.raw); h.thumbnail = a.thumbnail;
  a = std::move(h);
  readHeader(a);
}
int AssetLibrary::releaseAll() const {
  int n = 0; for (auto& a : assets) if (a->built && !a->vehicle && !a->raw.contains("rack")) { crumb("asset release"); release(*a); n += !a->built; }   // UI5: the loading screen presents between them
  return n;
}
bool AssetLibrary::loadOne(const fs::path& path, const json* parsed) {
  json j;
  if (parsed) j = *parsed;
  else try { j = json::parse(readTextFile(path)); } catch (std::exception& e) { logf("asset %s: parse error %s", path.string().c_str(), e.what()); return false; }
  auto a = std::make_unique<AssetDef>();
  a->file = path.filename().string();
  a->id = j.is_object() ? j.value("id", path.stem().string()) : path.stem().string();
  {   // PR2: content/settings/furniture_models.json: furniture drawn by an imported model, its parts kept as stand-ins
    json f = furnitureModelFor(a->id);
    if (!f.is_null() && j.is_object() && !j.contains("model") && j.contains("parts")) {
      j["model"] = f;
      for (auto& pj : j["parts"]) pj["mf"] = 1;
    }
  }
  a->raw = j;
  readHeader(*a);
  AssetDef* p = a.get();
  index_.emplace(a->id, p);   // the first asset of an id wins, as the old linear find did
  assets.push_back(std::move(a));
  // MEM1: catalogue assets are built (geometry, GPU buffers, collision) on first use (find / build), not all at startup:
  // building every one committed ~13 GB. Vehicles and racks stay eager (TankSys / the arsenal walk them at startup).
  if (!lazy || p->vehicle || j.contains("rack")) build(*p);
  return true;
}
void AssetDef::modelRange(const GpuModel* g, int& k0, int& k1) const {   // FL1
  bool l = g && g == modelLod.get(); int f = l ? modelLodFirst : modelFirst, c = l ? modelLodCount : modelCount;
  k0 = c < 0 ? 0 : f; k1 = c < 0 ? (g ? (int)g->parts.size() : 0) : f + c;
}
void AssetLibrary::build(AssetDef& ad) const {
  if (ad.built) return;
  ad.built = true;
  AssetDef* a = &ad; const json& j = ad.raw;
  std::map<std::string, int> mats;
  a->materials.push_back(Material{"default"});
  for (auto& mj : j.value("materials", json::array())) {
    Material m;
    m.name = mj.value("name", "");
    m.albedo = jvec3(jkey(mj, "albedo"), vec3(0.6f));
    m.roughness = mj.value("roughness", 0.8f);
    m.metallic = mj.value("metallic", 0.0f);
    m.pattern = patternFromName(mj.value("pattern", "plain"));
    m.grainAxis = mj.value("grain", 1);
    m.emissive = glm::max(jvec3(mj.value("emissive", json()), vec3(0)), vec3(0));   // L2 (docs/lighting.md)
    m.texCode = tex::materialCode(m.name, m.pattern, mj.value("texture", ""));   // L4: "texture": "<set>" (docs/textures.md)
    mats[m.name] = (int)a->materials.size();
    a->materials.push_back(m);
  }
  gModelBaked = modelBakedFor(j, a->id);   // MOD2
  for (auto& pj : j.value("parts", json::array())) {   // GX1: slabs the terrain / revetment replace (terrain_blend.cpp)
    int mode = blendPartMode(j, pj);
    if (mode == 1) continue;
    a->parts.push_back(parsePart(pj, mats));
    if (mode == 2) a->parts.back().visible = false;   // the generated revetment is drawn; this stays the collider
  }
  std::vector<Panel> autoPanels;
  if (!j.value("noAutoPanels", false)) extractWallPanels(*a, autoPanels, j);   // piers, corners, lintels, upper floors, opening trim: destructible too
  a->furnFirst = a->parts.size();
  for (auto& fj : j.value("furniture", json::array())) {
    a->furnitureNames.push_back(fj.value("name", "item"));
    for (auto& pj : fj.value("parts", json::array())) a->parts.push_back(parsePart(pj, mats));
  }
  for (auto& ij : j.value("interactables", json::array())) {
    InteractDef d;
    d.id = ij.value("id", "");
    d.kind = ij.value("kind", "door");
    d.label = ij.value("label", d.kind);
    d.sound = ij.value("sound", "wood");
    d.axis = glm::normalize(jvec3(jkey(ij, "axis"), vec3(0, 1, 0)));
    d.openAngle = ij.value("openAngle", 95.0f);
    d.hp = ij.value("hp", -1.0f);
    d.duration = ij.value("duration", 0.9f);
    d.defaultOpen = ij.value("defaultOpen", false);
    d.apAlways = ij.value("apertureAlways", false);
    d.apMin = jvec3(ij["aperture"]["min"]); d.apMax = jvec3(ij["aperture"]["max"]);
    for (auto& lj : ij.value("leaves", json::array())) {
      Leaf l;
      l.hinge = jvec3(lj["hinge"]);
      l.sign = lj.value("sign", 1.0f);
      for (auto& pj : lj.value("parts", json::array())) {
        Part p = parsePart(pj, mats);
        p.center -= l.hinge;
        if (p.shape == Part::PRISM) continue; // leaves are boxes only
        l.parts.push_back(p);
        if (p.collide) l.colliders.push_back(p);
      }
      d.leaves.push_back(std::move(l));
    }
    a->interactables.push_back(std::move(d));
  }
  std::vector<vec3> pts;
  if (j.contains("walkthrough")) a->walkthrough = j["walkthrough"];
  parseNavMeta(j, a->navMeta);   // navmeta.h (AI / terrain contract)
  if (j.contains("crate")) a->crate = j["crate"];
  if (j.contains("station")) {
    auto& sj = j["station"];
    auto& st = a->station;
    st.kind = sj.value("kind", "");
    st.yawPivot = jvec3(sj["yawPivot"]); st.elevPivot = jvec3(sj["elevPivot"]);
    st.muzzle = jvec3(sj["muzzle"]); st.optic = jvec3(sj["optic"]);
    st.gunner = jvec3(sj["gunner"]); st.handler = jvec3(sj["handler"]);
    if (sj.contains("traverse")) { st.yawMin = sj["traverse"][0]; st.yawMax = sj["traverse"][1]; }
    if (sj.contains("elevation")) { st.elevMin = sj["elevation"][0]; st.elevMax = sj["elevation"][1]; }
    st.traverseRate = sj.value("traverseRate", 14.0f); st.elevateRate = sj.value("elevateRate", 8.0f);
    st.recoil = sj.value("recoil", 0.6f); st.reloadTime = sj.value("reloadTime", 4.0f);
    st.loaded = sj.value("loaded", 1); st.reserve = sj.value("reserve", 5); st.ammo = sj.value("ammo", ""); st.reserveAT = sj.value("reserveAT", 0); st.ammoAT = sj.value("ammoAT", "");
    st.moveHalf = jvec3(jkey(sj, "moveHalf"), vec3(1, 0.5f, 1.6f)); st.serviceRadius = sj.value("serviceRadius", 6.0f);
    st.label = sj.value("label", st.kind == "pak40" ? std::string("PaK 40") : st.kind); st.heName = sj.value("heName", st.heName); st.atName = sj.value("atName", st.atName);
    st.sightFov = sj.value("sightFov", st.kind == "pak40" ? 9.0f : 55.0f); st.muzzleScale = sj.value("muzzleScale", 1.0f);
    if (sj.contains("shellHE")) { auto& h = sj["shellHE"]; st.heVelocity = h.value("velocity", 550.0f); st.heDamage = h.value("damage", 120.0f); st.heRadius = h.value("radius", 2.2f); st.heCrater = h.value("crater", 1.0f); }
    if (sj.contains("shellAT")) st.atVelocity = jkey(sj, "shellAT").value("velocity", 790.0f);
    st.fireSound = sj.value("fireSound", std::string("pak_fire")); st.fixed = sj.value("fixed", false);
    st.vent = jvec3(jkey(sj, "vent"), st.muzzle); st.fuseTip = jvec3(jkey(sj, "fuseTip"), st.vent); st.fuseTime = sj.value("fuseTime", 1.0f); st.pushSpeed = sj.value("pushSpeed", 0.9f);   // CAN1
    st.heGravity = jkey(sj, "shellHE").value("gravity", 1.0f);
    if (sj.contains("ball")) { auto gm = std::make_shared<GpuModel>(); std::string e; if (gm->load(contentDir() / "assets" / "models" / sj["ball"].get<std::string>(), e)) st.ballModel = gm; else logf("asset %s: ball model: %s (a trail instead)", a->id.c_str(), e.c_str()); }
    auto load = [&](const char* key, std::vector<Part>& parts, GpuMesh& mesh, vec3 pivot) {
      MeshData m;
      for (auto& pj : sj.value(key, json::array())) {
        Part p = parsePart(pj, mats);
        parts.push_back(p);
        appendPart(m, p, a->materials[p.mat], mat4(1));
        Part q = p; q.center += pivot; partCorners(q, pts);
      }
      mesh.upload(m);
    };
    load("yawParts", st.yawParts, st.yawMesh, st.yawPivot);
    load("elevParts", st.elevParts, st.elevMesh, st.elevPivot);
    load("recoilParts", st.recoilParts, st.recoilMesh, st.elevPivot);
  }

  // meshes
  MeshData op, gl;
  std::vector<Part> col, pcol;
  for (auto& p : a->parts) {
    appendPart(isGlass(*a, p) ? gl : op, p, a->materials[p.mat], mat4(1));
    if (p.visible) partCorners(p, pts);
    if (p.playerOnly) pcol.push_back(p); else if (p.collide) col.push_back(p);
  }
  a->opaque.upload(op); a->glass.upload(gl);
  a->shape = Physics::buildShape(col);
  if (!a->furnitureNames.empty()) {   // the same building without its built-in furniture (furnished by room templates)
    MeshData nop, ngl; std::vector<Part> ncol;
    for (size_t k = 0; k < a->furnFirst && k < a->parts.size(); ++k) {
      const Part& p = a->parts[k];
      appendPart(isGlass(*a, p) ? ngl : nop, p, a->materials[p.mat], mat4(1));
      if (!p.playerOnly && p.collide) ncol.push_back(p);
    }
    a->noFurnOpaque.upload(nop); a->noFurnGlass.upload(ngl);
    a->noFurnShape = Physics::buildShape(ncol);
  }
  a->playerShape = Physics::buildShape(pcol);
  for (auto& d : a->interactables)
    for (auto& l : d.leaves) {
      MeshData lo, lg;
      for (auto& p : l.parts) {
        appendPart(isGlass(*a, p) ? lg : lo, p, a->materials[p.mat], mat4(1));
        Part q = p; q.center += l.hinge; partCorners(q, pts);
      }
      l.opaque.upload(lo); l.glass.upload(lg);
      l.shape = Physics::buildShape(l.colliders);
      l.cpuO = std::make_shared<MeshData>(std::move(lo)); l.cpuG = std::make_shared<MeshData>(std::move(lg));   // P2 leafmerge (buildLeafPose)
    }
  // destructible wall panels: per-state meshes and shapes, plus a merged intact mesh/compound
  MeshData allIntact;
  // DR1 walllod: the intact walls again without their small parts, for copies drawn at asset LOD 1 / 2 (App::buildDrawLists).
  // [0]: parts under 0.35 m dropped (the rule of the opaque LOD1: under ~3.5 px where LOD1 starts); [1]: the opening
  // trim (frames, sills, lintels: under a pixel wide where LOD2 starts, ~300 m) dropped as well. Wall bays always stay.
  MeshData wallLod[2];
  auto addWallLod = [&](const Part& p, bool trim) {
    if (!p.visible) return;
    std::vector<vec3> cs; partCorners(p, cs);
    vec3 lo(1e9f), hi(-1e9f); for (auto& c : cs) { lo = glm::min(lo, c); hi = glm::max(hi, c); }
    if (cs.empty() || glm::length(hi - lo) < 0.35f) return;
    appendPart(wallLod[0], p, a->materials[p.mat], mat4(1));
    if (!trim) appendPart(wallLod[1], p, a->materials[p.mat], mat4(1));
  };
  std::vector<ShapePtr> intactShapes;
  for (auto& pj : j.value("panels", json::array())) {
    Panel pn;
    pn.id = pj.value("id", "");
    pn.family = pj.value("family", "masonry");
    pn.hp = pj.value("hp", 200.0f);
    pn.bmin = jvec3(pj["min"]); pn.bmax = jvec3(pj["max"]); pn.normal = jvec3(jkey(pj, "normal"), vec3(0, 0, 1));
    auto& sts = pj["states"];
    for (int s = 0; s < 3 && s < (int)sts.size(); ++s) {
      MeshData m; std::vector<Part> c;
      for (auto& qj : sts[s]) {
        Part p = parsePart(qj, mats); p.bv = 0;   // wall bays stay plain boxes (the damage field re-meshes them)
        pn.parts[s].push_back(p);
        appendPart(m, p, a->materials[p.mat], mat4(1));
        if (s == 0) { appendPart(allIntact, p, a->materials[p.mat], mat4(1)); addWallLod(p, false); if (p.visible) partCorners(p, pts); }
        if (p.collide) c.push_back(p);
      }
      pn.mesh[s].upload(m);
      pn.shape[s] = Physics::buildShape(c);
    }
    if (pn.shape[0]) intactShapes.push_back(pn.shape[0]);
    a->panels.push_back(std::move(pn));
  }
  for (auto& pn : autoPanels) {   // structural slabs converted at load (same boxes, so intact mesh / collision are unchanged)
    MeshData m; std::vector<Part> c;
    for (auto& p : pn.parts[0]) {
      appendPart(m, p, a->materials[p.mat], mat4(1)); appendPart(allIntact, p, a->materials[p.mat], mat4(1));
      addWallLod(p, pn.trim >= 0);
      if (p.visible) partCorners(p, pts);
      if (p.collide) c.push_back(p);
    }
    pn.mesh[0].upload(m);
    pn.shape[0] = Physics::buildShape(c);
    if (pn.shape[0]) intactShapes.push_back(pn.shape[0]);
    a->panels.push_back(std::move(pn));
  }
  for (size_t k = 0; k < a->panels.size(); ++k) if (a->panels[k].trim >= 0) a->trims[a->panels[k].trim].panel = (int)k;
  a->panelsIntact.upload(allIntact);
  if (wallLod[0].i.size() * 10 <= allIntact.i.size() * 9) a->panelsLod[0].upload(wallLod[0]);   // only where it saves >= 10 %
  if (wallLod[1].i.size() * 10 <= (a->panelsLod[0].count ? wallLod[0].i.size() : allIntact.i.size()) * 9) a->panelsLod[1].upload(wallLod[1]);
  a->wallsIntact = Physics::buildCompound(intactShapes);
  a->bmin = vec3(1e9f); a->bmax = vec3(-1e9f);
  if (j.contains("ivy")) {   // B1 ivy (src/ivy.cpp)
    const json& iv = j["ivy"];
    if (iv.is_string() && iv.get<std::string>() == "auto") a->ivy.mode = IvyDef::AUTO;
    else if (iv.is_object() && iv.value("auto", false)) {
      a->ivy.mode = IvyDef::AUTO; a->ivy.density = iv.value("density", 1.0f);
      a->ivy.type = iv.value("type", std::string()); a->ivy.pattern = iv.value("pattern", std::string()); a->ivy.age = iv.value("age", -1.0f); a->ivy.share = iv.value("share", -1.0f); a->ivy.overgrown = iv.value("overgrown", false);   // IV1
    }
    else if (iv.is_array()) {
      a->ivy.mode = IvyDef::BLOCKS;
      for (auto& b : iv) {
        std::string f = b.value("face", std::string("+z"));
        IvyDef::Block k;
        k.axis = f.size() > 1 && f[1] == 'x' ? 0 : 2; k.sign = f[0] == '-' ? -1.0f : 1.0f;
        k.a0 = b.value("a0", b.value("x0", 0.0f)); k.a1 = b.value("a1", b.value("x1", 0.0f)); k.y0 = b.value("y0", 0.0f); k.y1 = b.value("y1", 3.0f); k.density = b.value("density", 1.0f);
        k.type = b.value("type", std::string()); k.pattern = b.value("pattern", std::string()); k.age = b.value("age", -1.0f); k.overgrown = b.value("overgrown", false);   // IV1
        a->ivy.blocks.push_back(k);
      }
    }
  }
  if (j.contains("ivyPieces") && j["ivyPieces"].is_array()) {   // VR1: an ivy prop drawn from the ivy pack (src/ivy.cpp)
    a->ivy.mode = IvyDef::PIECES;
    for (auto& q : j["ivyPieces"]) {
      IvyDef::Piece p; p.id = q.value("piece", std::string("wall_1")); p.pos = jvec3(q.value("pos", json()), vec3(0)); p.yaw = q.value("yaw", 0.0f);
      p.scale = jvec3(q.value("scale", json()), vec3(1)); p.hang = q.value("hang", false);
      a->ivy.pieces.push_back(p);
      float r = 1.8f * std::max({p.scale.x, p.scale.y, p.scale.z}); pts.push_back(p.pos - vec3(r, p.hang ? 3.3f * p.scale.y : 0.0f, r)); pts.push_back(p.pos + vec3(r, p.hang ? 0.0f : 3.3f * p.scale.y, r));
    }
  }
  if (j.contains("tree")) { buildTreeAsset(*a, j["tree"]); for (auto& c : a->tree.canopy) { pts.push_back(vec3(c) - vec3(c.w)); pts.push_back(vec3(c) + vec3(c.w)); } }
  if (j.contains("rock") || j.contains("scatter")) buildNatureAsset(*a, j, pts);   // boulders / patches (nature.cpp)
  a->faction = j.value("faction", "");
  if (!a->station.kind.empty()) {   // direct-hit volume of a gun: its cradle, barrel and shield plus raised carriage parts
    vec3 mn(1e9f), mx(-1e9f);
    auto addParts = [&](const std::vector<Part>& ps, vec3 off) { for (auto& p : ps) { glm::mat3 rm = glm::mat3_cast(p.rot); vec3 hs = p.size * 0.5f; vec3 h = glm::abs(rm[0]) * hs.x + glm::abs(rm[1]) * hs.y + glm::abs(rm[2]) * hs.z; mn = glm::min(mn, p.center + off - h); mx = glm::max(mx, p.center + off + h); } };
    addParts(a->station.yawParts, a->station.yawPivot); addParts(a->station.elevParts, a->station.elevPivot); addParts(a->station.recoilParts, a->station.elevPivot);
    std::vector<Part> raised; for (auto& p : a->parts) if (p.visible && p.center.y > 0.3f) raised.push_back(p);
    addParts(raised, vec3(0));
    a->gunHitMin = mn; a->gunHitMax = mx;
  }
  if (j.contains("rack")) {
    auto& rj = j["rack"]; a->rack = true; a->rackFaction = rj.value("faction", "US");
    for (auto& dj : rj.value("display", json::array())) {
      AssetDef::RackDisplay d; d.weapon = dj.value("weapon", ""); d.pose = dj.value("pose", "lean"); d.p = jvec3(dj["p"]);
      d.lean = dj.value("lean", 10.0f); d.dir = dj.value("dir", 1.0f); d.yaw = dj.value("yaw", 0.0f);
      a->rackDisplay.push_back(d);
    }
    for (auto& s : rj.value("deploy", json::array())) a->rackDeploy.push_back(s.get<std::string>());
  }
  for (auto& p : a->parts) if (p.bag == 1 || p.bag == 3) a->hasBags = true;   // HB1b
  if (j.contains("defense")) {
    auto& dj = j["defense"]; DefenseDef& d = a->defense;
    d.kind = dj.value("kind", ""); d.hp = dj.value("hp", 300.0f); d.smallArms = dj.value("smallArmsMultiplier", 1.0f);
    d.occlusion = dj.value("blastOcclusion", 1.0f); d.maxSlopeDeg = dj.value("maxSlopeDeg", 35.0f);
    d.blocksVehicles = dj.value("blocksVehicles", false); d.stopsBullets = dj.value("stopsBullets", false);
    d.unsafeVault = dj.value("unsafeVault", false); d.traversal = dj.value("traversal", std::string("block")); d.slowFactor = dj.value("slowFactor", 0.35f);
    d.sections = dj.value("sections", 0); d.sectionHp = dj.value("sectionHp", 60.0f);
    for (auto& s : dj.value("sockets", json::array())) d.sockets.push_back({jvec3(s["p"]), jvec3(jkey(s, "t"), vec3(1, 0, 0))});
    // destroyed state: sandbags slump into a low spill (~0.25 m), the hedgehog is knocked onto its side
    MeshData bm; std::vector<Part> bc;
    float pivotY = a->parts.empty() ? 0.43f : a->parts[0].center.y;   // the hedgehog's crossing (all beams share it)
    if (d.kind != "wire") for (auto& p : a->parts) {
      Part q = p;
      if (d.kind == "sandbag") { q.center.y *= 0.3f; q.size.y *= 0.55f; q.center.z *= 1.35f; }
      else { quat t = glm::angleAxis(radians(58.0f), vec3(1, 0, 0)); q.center = t * (q.center - vec3(0, pivotY, 0)) + vec3(0, pivotY * 0.93f, 0); q.rot = t * q.rot; }
      if (q.visible) appendPart(bm, q, a->materials[q.mat], mat4(1));
      if (q.collide) bc.push_back(q);
    }
    a->defBrokenMesh.upload(bm); a->defBrokenShape = Physics::buildShape(bc);
  }
  if (j.contains("path")) {   // path entity: the catalogue mesh is a flat strip of the default size (thumbnail, ghost)
    auto& pj = j["path"]; PathDef& p = a->path; p.on = true;
    p.kind = pj.value("kind", "dirt"); p.width = pj.value("width", 2.5f); p.length = pj.value("length", 8.0f);
    p.minW = pj.value("minWidth", 0.8f); p.maxW = pj.value("maxWidth", 8.0f); p.minL = pj.value("minLength", 1.0f); p.maxL = pj.value("maxLength", 60.0f);
    p.albedo = jvec3(jkey(pj, "albedo"), vec3(0.5f)); p.rough = pj.value("roughness", 0.85f); p.slab = pj.value("slab", 2.0f); p.slabHp = pj.value("slabHp", 60.0f);
    extern void buildPathMeshData(MeshData&, const PathDef&, vec2, vec3, float, const std::function<float(float, float)>&, const std::vector<uint8_t>*);
    MeshData pm; buildPathMeshData(pm, p, vec2(p.width, p.length), vec3(0), 0.0f, [](float, float) { return 0.0f; }, nullptr);
    a->opaque.destroy(); a->opaque.upload(pm);
    pts.push_back(vec3(-p.width * 0.5f, 0, -p.length * 0.5f)); pts.push_back(vec3(p.width * 0.5f, 0.05f, p.length * 0.5f));
  }
  if (j.contains("models") && j["models"].is_array()) {   // PR1 attached modelled pieces, one GPU copy per folder
    static std::map<std::string, std::pair<std::shared_ptr<GpuModel>, std::shared_ptr<GpuModel>>> cache;
    for (auto& mj : j["models"]) {
      std::string d = mj.value("dir", std::string());
      auto it = cache.find(d);
      if (it == cache.end()) {
        fs::path dir = contentDir() / "assets" / "models" / d;
        auto gm = std::make_shared<GpuModel>(), lod = std::make_shared<GpuModel>(); std::string e1, e2;
        if (!gm->load(dir, e1)) { logf("asset %s: attached model %s: %s", a->id.c_str(), d.c_str(), e1.c_str()); gm.reset(); }
        if (!fs::exists(dir / "lod1" / "mesh.bin") || !lod->load(dir / "lod1", e2)) lod.reset();
        it = cache.emplace(d, std::make_pair(gm, lod)).first;
      }
      if (!it->second.first) continue;
      AssetDef::Attached at; at.model = it->second.first; at.lod = it->second.second;
      at.local = glm::translate(mat4(1), jvec3(jkey(mj, "pos"), vec3(0))) * glm::mat4_cast(yawQuat(mj.value("yaw", 0.0f)));
      if (mj.contains("tilt") && mj["tilt"].is_array() && mj["tilt"].size() >= 2) { vec2 t(mj["tilt"][0].get<float>(), mj["tilt"][1].get<float>()); at.local = at.local * glm::rotate(mat4(1), glm::radians(t.x), vec3(1, 0, 0)) * glm::rotate(mat4(1), glm::radians(t.y), vec3(0, 0, 1)); }   // PR4: [pitch about x, roll about z] (a drum on its side, a tipped table)
      at.lodDist = mj.value("lodDist", 15.0f);
      a->attached.push_back(at);
    }
  }
  if (j.contains("model")) {   // imported model (baked; a missing bake leaves the collider boxes only, logged)
    auto& mj = j["model"];
    a->modelLodDist = mj.value("lodDist", 30.0f); a->modelCull = mj.value("cullDist", 400.0f); a->modelThinPx = mj.value("thinLinesPx", 0.0f);
    a->modelFoliage = mj.value("foliage", false); a->modelWind = mj.value("wind", 0.0f);   // NT8: instanced model foliage
    if (a->raw.is_object() && a->raw.contains("tree") && a->raw["tree"].is_object() && a->raw["tree"].value("dead", false)) a->modelWind = 0.0f;   // dead / bare trees stand still (user)
    {   // user: "3d tree models shouldnt move at all". A model tree is model foliage in the "Trees" category or with a tree
        // block whose stump is >= 0.3 m (every tree 0.35-0.4; bushes and saplings 0.12-0.2), so bushes, saplings, grass keep wind
        const json* tb = (j.contains("tree") && j["tree"].is_object()) ? &j["tree"] : nullptr;
        if (a->modelFoliage && (j.value("category", std::string()) == "Trees" || (tb && tb->value("stumpHeight", 0.0f) >= 0.3f))) a->modelWind = 0.0f;
    }
    a->modelSink = std::clamp(mj.value("sink", 0.0f), 0.0f, 3.0f);   // BT2: drawn this far down (m): a tree's root flare buried, not stood on
    a->modelScale = mj.value("drawScale", 1.0f);
    // SD2: "root": "vehicles" reads a vehicle's bake (content/vehicles/models) for a static prop of it; "fallbackDir" is
    // tried when the main bake is missing (a licence-restricted bake kept local: the public tree shows the box fallback)
    const fs::path base = mj.value("root", std::string()) == "vehicles" ? contentDir() / "vehicles" / "models" : contentDir() / "assets" / "models";
    fs::path dir = base / mj.value("dir", a->id);
    const bool fellBack = mj.contains("fallbackDir") && !fs::exists(dir / "mesh.bin");
    if (fellBack) { dir = base / mj["fallbackDir"].get<std::string>(); a->modelScale = mj.value("fallbackScale", a->modelScale); }   // DF1: "fallbackPart" / "fallbackScale": one plant of a committed pack
    auto gm = std::make_shared<GpuModel>(); std::string err; gm->keepPos = j.contains("sight");   // AT7: an optic keeps its vertices (its mount feet seat it on the gun)
    bool turn = mj.value("yaw", 0.0f) == 180.0f;   // "yaw": 180 - the bake faces -z (the car pack), the asset's parts +z
    // TR1: model foliage loads once per bake (the sized tree variants share it: one GPU copy, one instanced batch)
    static std::map<std::string, std::pair<std::shared_ptr<GpuModel>, std::shared_ptr<GpuModel>>> folCache;
    auto fc = a->modelFoliage ? folCache.find(dir.string() + (turn ? "|t" : "")) : folCache.end();
    // FL1: "part": a prefix of part names (one plant of a pack bake); its bounds are those parts'
    auto partRange = [&]() {
      vec3 lo = a->model->bmin, hi = a->model->bmax;
      if (mj.contains("part")) {
        const std::string pre = (fellBack && mj.contains("fallbackPart") ? mj["fallbackPart"] : mj["part"]).get<std::string>();
        auto rng = [&](const GpuModel& g, int& f, int& c) { f = 0; c = 0; int first = -1; for (int k = 0; k < (int)g.parts.size(); ++k) if (g.parts[k].name.rfind(pre, 0) == 0) { if (first < 0) first = k; f = first; c = k - first + 1; } };
        rng(*a->model, a->modelFirst, a->modelCount);
        if (a->modelLod) { rng(*a->modelLod, a->modelLodFirst, a->modelLodCount); if (!a->modelLodCount) a->modelLod.reset(); }
        if (!a->modelCount) { logf("asset %s: no parts named %s* in its model %s: the whole model drawn", a->id.c_str(), pre.c_str(), dir.string().c_str()); a->modelCount = -1; }   // a fallback bake
        else { lo = vec3(FLT_MAX); hi = vec3(-FLT_MAX); for (int k = a->modelFirst; k < a->modelFirst + a->modelCount; ++k) { lo = glm::min(lo, a->model->parts[k].bmin); hi = glm::max(hi, a->model->parts[k].bmax); } }
      }
      pts.push_back(lo * a->modelScale); pts.push_back(hi * a->modelScale);
    };
    if (fc != folCache.end()) { a->model = fc->second.first; a->modelLod = fc->second.second; partRange(); }
    else if (gm->load(dir, err, turn)) {
      a->model = gm;
      auto lod = std::make_shared<GpuModel>(); std::string e2;
      if (fs::exists(dir / "lod1" / "mesh.bin") && lod->load(dir / "lod1", e2, turn)) {
        // FOL1: the LOD1 cards sample the same atlas layout as LOD0 (one bake): they draw LOD0's full-size cutout albedo
        // instead of their own (512, 128 px tiles on 1.3 m cards: the blurred, blocky canopy overhead); no extra memory
        for (auto& lm : lod->mats) if (lm.cutout) for (auto& m0 : gm->mats)
          if (m0.cutout && m0.alb && m0.name == lm.name && lm.alb != m0.alb) { if (lm.alb) glDeleteTextures(1, &lm.alb); lm.alb = m0.alb; lod->borrowed.insert(m0.alb); }
        a->modelLod = lod;
      }
      if (a->modelFoliage) folCache[dir.string() + (turn ? "|t" : "")] = {a->model, a->modelLod};
      partRange();
      if (mj.contains("stationRoles")) {   // part-name prefix -> station role: base / traverse / elevation / recoil / wheel / fuse (CAN1)
        static const std::map<std::string, int> rn = {{"base", 0}, {"traverse", 1}, {"elevation", 2}, {"recoil", 3}, {"wheel", 4}, {"fuse", 5}};
        for (auto& p : gm->parts) {
          int role = 0;
          for (auto& [pre, rv] : jkey(mj, "stationRoles").items()) if (p.name.rfind(pre, 0) == 0) { auto f = rn.find(rv.get<std::string>()); if (f != rn.end()) role = f->second; }
          a->station.modelRole.push_back(role);
        }
      }
    } else logf("asset %s: model %s not baked (%s)", a->id.c_str(), dir.string().c_str(), err.c_str());
    MeshData gh;
    Material ghostMat{"ghost"};
    const bool anyCol = std::any_of(a->parts.begin(), a->parts.end(), [](const Part& p) { return p.collide; });   // UI13: collider-less props (cash register) still get a ghost
    for (auto& p : a->parts) if (p.collide || !anyCol) { Part q = p; q.visible = true; appendPart(gh, q, ghostMat, mat4(1)); partCorners(q, pts); }
    a->colliderGhost.upload(gh);
  }
  for (auto& q : pts) { a->bmin = glm::min(a->bmin, q); a->bmax = glm::max(a->bmax, q); }
  if (pts.empty()) { a->bmin = vec3(-1); a->bmax = vec3(1); }
  buildAssetLods(*a, op.i.size() / 3, allIntact);
  logf("asset %-28s %5zu parts %3zu interactables %3zu panels %3zu trims %6d tris, walls %d / %d / %d (LOD1 %d, LOD2 %d)", a->id.c_str(), a->parts.size(), a->interactables.size(), a->panels.size(), a->trims.size(), (int)((op.i.size() + allIntact.i.size()) / 3), (int)(allIntact.i.size() / 3), a->panelsLod[0].count / 3, a->panelsLod[1].count / 3, a->lodMesh[0].count / 3, a->lodMesh[1].count / 3);
}

void buildPlaceholder(AssetDef& a) {
  a.id = "__missing__"; a.name = "Missing asset"; a.category = "Missing"; a.placeholder = true;
  a.materials = {Material{"default"}, Material{"missing", vec3(0.8f, 0.1f, 0.6f), 0.6f, 0.0f, PAT_PLAIN, 1}};
  Part p; p.center = vec3(0, 1.5f, 0); p.size = vec3(3); p.mat = 1; p.collide = false;
  a.parts = {p};
  MeshData m; appendPart(m, p, a.materials[1], mat4(1)); a.opaque.upload(m);
  a.bmin = vec3(-1.5f, 0, -1.5f); a.bmax = vec3(1.5f, 3, 1.5f); a.footprint = vec2(3, 3);
}

bool AssetLibrary::loadAll(const fs::path& dir) {
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  // CACHE1: the parsed catalogue in one CBOR file (texbc cache folder, pruned with it): parsing 1600 JSON texts took ~1.8 s
  // a start; an entry is reused while its file's size and time match, and the file is rewritten when any changed
  uint64_t dh = 1469598103934665603ull; for (unsigned char c : dir.lexically_normal().string()) { dh ^= c; dh *= 1099511628211ull; }
  char cn[48]; std::snprintf(cn, sizeof cn, "catalog-%016llx.cbor", (unsigned long long)dh);
  const fs::path cf = userCacheDir("texbc", "NMK_TEXBC_CACHE") / cn;
  json cache = json::object(), fresh = json::object(); bool miss = false;
  try { std::ifstream in(cf, std::ios::binary); if (in) { std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); cache = json::from_cbor(b); userCacheTouch(cf); } } catch (...) { cache = json::object(); }
  for (size_t k = 0; k < files.size(); ++k) {
    crumb("asset catalogue");
    if (gLoadingDepth && k % 16 == 0) loadingProgress("Scanning the asset catalogue", (float)k / files.size());   // UI5: the boot splash keeps presenting
    const fs::path& f = files[k];
    std::error_code ec; std::string key = f.filename().string();
    std::string stamp = std::to_string(fs::file_size(f, ec)) + ":" + std::to_string((long long)fs::last_write_time(f, ec).time_since_epoch().count());
    auto it = cache.find(key);
    if (it != cache.end() && it->is_object() && it->value("s", std::string()) == stamp && it->contains("j")) { loadOne(f, &(*it)["j"]); fresh[key] = std::move(*it); continue; }
    miss = true;
    json j; try { j = json::parse(readTextFile(f)); } catch (std::exception& e) { logf("asset %s: parse error %s", f.string().c_str(), e.what()); continue; }
    loadOne(f, &j);
    fresh[key] = {{"s", stamp}, {"j", std::move(j)}};
  }
  if (miss || fresh.size() != cache.size()) {
    std::error_code ec; fs::create_directories(cf.parent_path(), ec);
    fs::path tmp = cf; tmp += "." + std::to_string((unsigned long long)(uintptr_t)&fresh) + ".tmp";
    { std::vector<uint8_t> b = json::to_cbor(fresh); std::ofstream o(tmp, std::ios::binary); o.write((const char*)b.data(), (std::streamsize)b.size()); }
    fs::rename(tmp, cf, ec);
  }
  buildPlaceholder(placeholder_);
  { int b = 0; for (auto& a : assets) b += a->built; logf("assets: %zu in the catalogue, %d built now (lazy %d)", assets.size(), b, (int)lazy); }
  return !assets.empty();
}

const AssetDef* AssetLibrary::find(const std::string& id) const {   // per instance per frame in several places: hashed
  auto it = index_.find(id);
  if (it == index_.end()) return nullptr;
  if (!it->second->built) build(const_cast<AssetDef&>(*it->second));   // MEM1: first use builds it (main thread: GL)
  return it->second;
}
