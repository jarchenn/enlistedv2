// BF B1 moss field: the habitat grid and the CPU mirror of kCommon's mossField (moss.h).
#include "moss.h"
#include "app.h"
#include "assets.h"
#include "ground_paint.h"
#include "water_query.h"
#include "terrain.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <mutex>

namespace {
struct Grid { vec2 org{0}; float cell = 2; int nx = 0, nz = 0; std::vector<uint8_t> v; };   // RGBA8 per cell
std::shared_ptr<const Grid> gGrid;   // swapped whole: lookups on other threads keep the copy they took
std::mutex gGridM;
std::atomic<uint32_t> gRev{0};
uint64_t gSig = 0, gPendSig = 0; double gPendT = 0;
std::future<std::shared_ptr<Grid>> gJob; uint64_t gJobSig = 0;

inline float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float ss(float a, float b, float x) { float t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }

struct Inputs {   // everything the worker needs, gathered on the main thread
  Grid g; std::vector<float> ground, top, canopy, crownLo, crownHi; std::vector<uint8_t> water;
  std::vector<vec2> doors;
};

std::shared_ptr<Grid> buildGrid(Inputs in) {
  const int nx = in.g.nx, nz = in.g.nz, n = nx * nz; const float cs = in.g.cell;
  auto out = std::make_shared<Grid>(in.g); out->v.assign(size_t(n) * 4, 0);
  // x: distance to water (two-pass chamfer, metres), as nearness over 8 m
  std::vector<float> d(n, 1e9f);
  for (int k = 0; k < n; ++k) if (in.water[k]) d[k] = 0;
  const float a = cs, b = cs * 1.41421356f;
  for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) { float& c = d[z * nx + x];
    if (x > 0) c = std::min(c, d[z * nx + x - 1] + a); if (z > 0) c = std::min(c, d[(z - 1) * nx + x] + a);
    if (x > 0 && z > 0) c = std::min(c, d[(z - 1) * nx + x - 1] + b); if (x + 1 < nx && z > 0) c = std::min(c, d[(z - 1) * nx + x + 1] + b); }
  for (int z = nz - 1; z >= 0; --z) for (int x = nx - 1; x >= 0; --x) { float& c = d[z * nx + x];
    if (x + 1 < nx) c = std::min(c, d[z * nx + x + 1] + a); if (z + 1 < nz) c = std::min(c, d[(z + 1) * nx + x] + a);
    if (x + 1 < nx && z + 1 < nz) c = std::min(c, d[(z + 1) * nx + x + 1] + b); if (x > 0 && z + 1 < nz) c = std::min(c, d[(z + 1) * nx + x - 1] + b); }
  // y: sun hours over the day. The long-term path (not the map's hour): east to west through the south (+z; world
  // north is -z), 10 deg high at the ends and 40 deg at noon; terrain and buildings block, canopies pass 40 %
  const int kDirs = 9; vec3 dirs[kDirs];
  for (int i = 0; i < kDirs; ++i) { float az = glm::radians(-80.0f + 160.0f * i / (kDirs - 1)), el = glm::radians(10.0f + 30.0f * std::cos(az));
    dirs[i] = vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)); }
  const int steps = std::max(4, int(80.0f / cs));
  for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
    const int k = z * nx + x; const float h0 = in.ground[k] + 0.3f; float sun = 0;
    for (const vec3& dv : dirs) {
      float tr = 1; const float hor = std::sqrt(dv.x * dv.x + dv.z * dv.z), slope = dv.y / std::max(hor, 1e-3f);
      for (int s = 1; s <= steps && tr > 0.05f; ++s) {
        const float dd = s * cs; const int cx = int(std::floor(x + dv.x / hor * s + 0.5f)), cz = int(std::floor(z + dv.z / hor * s + 0.5f));
        if (cx < 0 || cz < 0 || cx >= nx || cz >= nz) break;
        const int c = cz * nx + cx; const float ry = h0 + dd * slope;
        if (in.top[c] > ry) { tr = 0; break; }
        if (in.canopy[c] > 0 && ry > in.crownLo[c] && ry < in.crownHi[c]) tr *= 1.0f - 0.6f * in.canopy[c];
      }
      sun += tr;
    }
    const float sunHours = 12.0f * sun / kDirs;
    uint8_t* o = &out->v[size_t(k) * 4];
    o[0] = uint8_t(std::lround(255 * sat(1 - d[k] / 8.0f)));
    o[1] = uint8_t(std::lround(255 * sat(1 - sunHours / 6.0f)));
    o[3] = uint8_t(std::lround(255 * sat(in.canopy[k])));
  }
  // z: no moss within ~1.2 m of a door or gate (0.8-1.5 m, reference 03 s3.1)
  for (vec2 p : in.doors) {
    const int r = int(std::ceil(1.6f / cs)), cx = int((p.x - in.g.org.x) / cs), cz = int((p.y - in.g.org.y) / cs);
    for (int z = cz - r; z <= cz + r; ++z) for (int x = cx - r; x <= cx + r; ++x) {
      if (x < 0 || z < 0 || x >= nx || z >= nz) continue;
      const float dd = glm::length(in.g.org + (vec2(x, z) + 0.5f) * cs - p);
      uint8_t& o = out->v[size_t(z * nx + x) * 4 + 2]; o = std::max<uint8_t>(o, uint8_t(255 * (1 - ss(0.8f, 1.5f, dd))));
    }
  }
  return out;
}

uint64_t mix64(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; }
}   // namespace

uint32_t mossHabitatRev() { return gRev.load(); }

vec4 mossHabitat(float x, float z) {
  std::shared_ptr<const Grid> g; { std::lock_guard<std::mutex> l(gGridM); g = gGrid; }
  if (!g || g->nx < 2) return vec4(0);
  const float fx = std::clamp((x - g->org.x) / g->cell - 0.5f, 0.0f, g->nx - 1.001f), fz = std::clamp((z - g->org.y) / g->cell - 0.5f, 0.0f, g->nz - 1.001f);
  const int ix = int(fx), iz = int(fz); const float tx = fx - ix, tz = fz - iz;
  auto at = [&](int xx, int zz) { const uint8_t* p = &g->v[size_t(zz * g->nx + xx) * 4]; return vec4(p[0], p[1], p[2], p[3]) / 255.0f; };
  return glm::mix(glm::mix(at(ix, iz), at(ix + 1, iz), tx), glm::mix(at(ix, iz + 1), at(ix + 1, iz + 1), tx), tz);
}

void mossHabitatSync(App& app) {
  if (gJob.valid() && gJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    auto g = gJob.get(); std::shared_ptr<const Grid> old;
    { std::lock_guard<std::mutex> l(gGridM); old = gGrid; gGrid = g; } gSig = gJobSig; ++gRev;
    static bool reg = false; if (!reg) { terrainSetHabitat(mossHabitat); reg = true; }   // the terrain mesh's aHab (MP2, terrain.h)
    // re-fill the terrain's habitat attribute where the grid changed (the whole grid the first time or on a new layout)
    vec2 lo(1e9f), hi(-1e9f);
    const bool same = old && old->nx == g->nx && old->nz == g->nz && old->cell == g->cell && old->org == g->org && old->v.size() == g->v.size();
    for (int z = 0; z < g->nz; ++z) for (int x = 0; x < g->nx; ++x) {
      bool ch = !same;
      if (same) { const size_t k = size_t(z * g->nx + x) * 4; for (int c = 0; c < 4 && !ch; ++c) ch = std::abs(int(g->v[k + c]) - int(old->v[k + c])) > 2; }
      if (ch) { const vec2 w = g->org + vec2(x, z) * g->cell; lo = glm::min(lo, w); hi = glm::max(hi, w + vec2(g->cell)); }
    }
    if (lo.x <= hi.x) terrainHabitatDirty(lo - vec2(g->cell), hi + vec2(g->cell));
    logf("moss: habitat grid %d x %d at %.1f m (terrain refill %.0f x %.0f m)", g->nx, g->nz, g->cell, std::max(0.0f, hi.x - lo.x), std::max(0.0f, hi.y - lo.y));
  }
  const Terrain& t = app.terrain;
  uint64_t is = app.doc.instances.size();
  for (const Instance& i : app.doc.instances) is = mix64(is, i.id * 31 + i.rev);   // placed, moved, removed
  uint64_t sig = mix64(mix64(mix64(is, app.groundRev), lassoGroundRev()), uint64_t(t.size * 16) ^ (uint64_t(t.sizeZ * 16) << 20) ^ (uint64_t(t.generation) << 40));
  if (sig == gSig || (gJob.valid() && sig == gJobSig)) return;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (sig != gPendSig) { gPendSig = sig; gPendT = now; }
  if (gGrid && now - gPendT < 1.0) return;   // editing: wait until the map holds still for a second
  if (gJob.valid()) return;                  // one build at a time; the next sync picks up the newer signature
  auto in = std::make_shared<Inputs>();
  Grid& g = in->g; g.cell = std::max(2.0f, std::max(t.size, t.sizeZ) / 512.0f);
  g.org = t.org; g.nx = std::max(2, int(std::ceil(t.size / g.cell))); g.nz = std::max(2, int(std::ceil(t.sizeZ / g.cell)));
  const int n = g.nx * g.nz;
  in->ground.resize(n); in->top.resize(n); in->canopy.assign(n, 0); in->crownLo.assign(n, 1e9f); in->crownHi.assign(n, -1e9f); in->water.assign(n, 0);
  for (int z = 0; z < g.nz; ++z) for (int x = 0; x < g.nx; ++x) {
    const vec2 w = g.org + (vec2(x, z) + 0.5f) * g.cell; const int k = z * g.nx + x;
    const float h = t.height(w.x, w.y); in->ground[k] = in->top[k] = h;
    const float wl = waterAt(app, w.x, w.y);
    in->water[k] = (!waterNone(wl) && wl > h - 0.02f) || groundPaintWeight(w.x, w.y, 1) > 0.5f;   // 1: marsh / shallow water
  }
  for (const Instance& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !it->second.asset) continue;
    const AssetDef* a = it->second.asset; const quat q = yawQuat(i.yaw);
    for (const auto& d : a->interactables) for (const auto& lf : d.leaves) { vec3 w = i.pos + q * lf.hinge; in->doors.push_back(vec2(w.x, w.z)); }
    const bool tree = a->tree.on || a->modelFoliage;
    if (!tree && a->footprint.x * a->footprint.y < 1.0f) continue;
    const vec3 mn = a->bmin, mx = a->bmax;
    if (tree) {   // crown disc: half the larger span, from a third of the height up
      const float r = 0.5f * std::max(mx.x - mn.x, mx.z - mn.z), lo = i.pos.y + mn.y + 0.35f * (mx.y - mn.y), hi = i.pos.y + mx.y;
      const int rc = int(std::ceil(r / g.cell)), cx = int((i.pos.x - g.org.x) / g.cell), cz = int((i.pos.z - g.org.y) / g.cell);
      for (int z = cz - rc; z <= cz + rc; ++z) for (int x = cx - rc; x <= cx + rc; ++x) {
        if (x < 0 || z < 0 || x >= g.nx || z >= g.nz) continue;
        const float dd = glm::length(g.org + (vec2(x, z) + 0.5f) * g.cell - vec2(i.pos.x, i.pos.z)); if (dd > r) continue;
        const int k = z * g.nx + x; in->canopy[k] = std::min(1.0f, in->canopy[k] + 0.8f * (1 - ss(0.6f * r, r, dd)));
        in->crownLo[k] = std::min(in->crownLo[k], lo); in->crownHi[k] = std::max(in->crownHi[k], hi);
      }
    } else {      // building / wall: its footprint box blocks the sun up to its top
      const float topY = i.pos.y + mx.y; const quat qi = glm::inverse(q);
      const float rr = glm::length(vec2(std::max(-mn.x, mx.x), std::max(-mn.z, mx.z)));
      const int rc = int(std::ceil(rr / g.cell)), cx = int((i.pos.x - g.org.x) / g.cell), cz = int((i.pos.z - g.org.y) / g.cell);
      for (int z = cz - rc; z <= cz + rc; ++z) for (int x = cx - rc; x <= cx + rc; ++x) {
        if (x < 0 || z < 0 || x >= g.nx || z >= g.nz) continue;
        const vec2 w = g.org + (vec2(x, z) + 0.5f) * g.cell; const vec3 l = qi * (vec3(w.x, 0, w.y) - vec3(i.pos.x, 0, i.pos.z));
        if (l.x < mn.x || l.x > mx.x || l.z < mn.z || l.z > mx.z) continue;
        float& tp = in->top[z * g.nx + x]; tp = std::max(tp, topY);
      }
    }
  }
  gJobSig = sig;
  gJob = std::async(std::launch::async, [in] { try { return buildGrid(std::move(*in)); } catch (...) { return std::make_shared<Grid>(); } });
}

// ---- CPU mirror of kCommon (render.cpp). Keep every constant identical.
static uint32_t mossHash(int x, int y, int z) {
  uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h;
}
float mossVnCpu(vec3 p) {
  const vec3 i = glm::floor(p); vec3 f = p - i; f = f * f * (3.0f - 2.0f * f);
  const int cx = int(i.x), cy = int(i.y), cz = int(i.z);
  auto H = [&](int x, int y, int z) { return float(mossHash(cx + x, cy + y, cz + z) & 0xffffu) * (1.0f / 65535.0f); };
  auto L = [](float a, float b, float t) { return a + (b - a) * t; };
  return L(L(L(H(0, 0, 0), H(1, 0, 0), f.x), L(H(0, 1, 0), H(1, 1, 0), f.x), f.y),
           L(L(H(0, 0, 1), H(1, 0, 1), f.x), L(H(0, 1, 1), H(1, 1, 1), f.x), f.y), f.z);
}
float mossMoistCpu(float y, float drip, float cav, float distW) {
  return sat(0.40f * std::pow(sat(1 - y / 1.4f), 1.8f) + 0.25f * drip + 0.20f * cav + 0.15f * sat(1 - distW / 8.0f));
}
float mossShelterCpu(float sh0, vec3 n, vec3 sunDir) {
  const vec3 ds = -glm::normalize(vec3(sunDir.x, 0, sunDir.z) + vec3(1e-4f, 0, 0));
  return 0.60f * sat(sh0) + 0.40f * sat(glm::dot(n, ds) * 0.5f + 0.5f);
}
vec3 mossFieldCpu(vec3 p, float upDot, float wallAttach, float groundRecv, float moist, float shelter, float por, float root,
                  float traffic, float excl, float snow, float site, float lichenAge, float nearK, float bias) {
  const float attach = std::max(ss(0.22f, 0.85f, upDot) * groundRecv, wallAttach);
  const float hab = attach * sat(0.42f * moist + 0.30f * shelter + 0.18f * por + 0.10f * root);
  const float pt = ss(0.36f, 0.65f, hab + (attach > 0.001f ? bias : 0.0f) + 0.22f * (mossVnCpu(p / 0.6f) - 0.5f) + 0.17f * (mossVnCpu(p / 0.08f + 31.0f) - 0.5f) * nearK);
  const float M = pt * (1 - ss(0.18f, 0.62f, traffic)) * (1 - excl) * (1 - snow);
  const float L = sat((1 - shelter) * lichenAge * (1 - moist) * 2.0f) * ss(0.55f, 0.9f, por) * (1 - M) * (1 - snow) * (1 - excl)
                * ss(0.5f, 0.68f, mossVnCpu(p / 0.09f + 7.0f) * 0.7f + mossVnCpu(p / 0.7f + 3.0f) * 0.3f);
  return vec3(M, ss(0.50f, 0.83f, M) * site, L);
}
