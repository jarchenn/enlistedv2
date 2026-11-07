// Path entities (complete update E4): gravel, dirt and concrete paths as placed, editable map instances.
//
// Each path is an ordinary instance; its length and width live in Instance::extra["path"] (defaults from the asset's
// "path" block, clamped to its limits), so they save, load, undo and apply like any other placement. The surface is
// a grid mesh in the instance's frame, draped onto the current terrain a few centimetres above it (no collider: you
// walk on the ground itself, so there are no steps, floating edges or vault obstacles). Whenever the ground changes
// (craters, F5 reset, applied damage) every path re-drapes, so dirt and gravel follow fresh craters instead of
// floating over them, and a path can never restore cratered ground. Concrete is laid in slabs (asset "slab" m
// along the path); a blast breaks the slabs it reaches - they disappear and the crater shows - while the rest
// survive. Material detail is procedural in the main shader (patterns 12 gravel, 13 dirt, 14 concrete slab) in path
// coordinates (metres along / across), so resizing never stretches it; the per-vertex edge factor (0 at the border)
// drives the soil / grass transition at the margins. Grass is cleared inside the path with a narrow verge.
// V9: a piece may also follow a circular arc ("bend", the curved joints of chained auto paths) and offset its
// along-path coordinate ("u0") so a chain's surface pattern runs on unbroken; see autopath.h.
#include "app.h"
#include "autopath.h"
#include "nature.h"
#include <algorithm>
#include <functional>

vec2 App::pathSize(const Instance& i, const AssetDef* a) const {
  const PathDef& p = a->path;
  float L = p.length, W = p.width;
  if (i.extra.is_object() && i.extra.contains("path")) { L = i.extra["path"].value("length", L); W = i.extra["path"].value("width", W); }
  return vec2(std::clamp(W, p.minW, p.maxW), std::clamp(L, p.minL, p.maxL));
}

void App::setPathSize(Instance& i, vec2 wl) {
  const AssetDef* a = assetFor(i);
  if (!a || !a->path.on) return;
  if (!i.extra.is_object()) i.extra = json::object();
  if (!i.extra.contains("path") || !i.extra["path"].is_object()) i.extra["path"] = json::object();   // bend / u0 are kept
  i.extra["path"]["width"] = std::clamp(wl.x, a->path.minW, a->path.maxW); i.extra["path"]["length"] = std::clamp(wl.y, a->path.minL, a->path.maxL);
  i.rev++;
}

PathShape pathShape(const Instance& i) {
  PathShape s;
  if (i.extra.is_object() && i.extra.contains("path") && i.extra["path"].is_object()) { s.bend = i.extra["path"].value("bend", 0.0f); s.u0 = i.extra["path"].value("u0", 0.0f); }
  return s;
}
void setPathShape(Instance& i, PathShape s) {
  if (!i.extra.is_object()) i.extra = json::object();
  if (!i.extra.contains("path") || !i.extra["path"].is_object()) i.extra["path"] = json::object();
  if (std::fabs(s.bend) > 1e-3f) i.extra["path"]["bend"] = s.bend; else i.extra["path"].erase("bend");
  if (std::fabs(s.u0) > 1e-4f) i.extra["path"]["u0"] = s.u0; else i.extra["path"].erase("u0");
  i.rev++;
}
// the inner edge of an arc keeps at least 0.2 m of radius: |bend| <= L / (W/2 + 0.2) rad
static float clampBend(float bendDeg, vec2 wl) { float lim = degrees(wl.y / (wl.x * 0.5f + 0.2f)); return std::clamp(bendDeg, -lim, lim); }
vec2 pathCentre(float L, float bendDeg, float s, vec2* heading) {
  float k = radians(bendDeg) / std::max(L, 1e-3f);   // curvature: heading turns k rad per metre
  float ph = k * s;
  if (heading) *heading = vec2(std::sin(ph), std::cos(ph));
  if (std::fabs(k) < 1e-5f) return vec2(0, s);
  return vec2((1.0f - std::cos(ph)) / k, std::sin(ph) / k);
}
void pathEnd(const App& app, const Instance& i, const AssetDef* a, float sgn, vec3& pos, vec2& outDir) {
  vec2 wl = app.pathSize(i, a); float bend = clampBend(pathShape(i).bend, wl);
  vec2 h; vec2 c = pathCentre(wl.y, bend, sgn * wl.y * 0.5f, &h);
  quat q = yawQuat(i.yaw);
  vec3 w = i.pos + q * vec3(c.x, 0, c.y), d = q * vec3(h.x * sgn, 0, h.y * sgn);
  pos = w; outDir = glm::normalize(vec2(d.x, d.z));
}
void pathLocalBounds(const App& app, const Instance& i, const AssetDef* a, vec2& mn, vec2& mx) {
  vec2 wl = app.pathSize(i, a); float bend = clampBend(pathShape(i).bend, wl);
  if (std::fabs(bend) < 1e-3f) { mn = -wl * 0.5f; mx = wl * 0.5f; return; }
  mn = vec2(1e9f); mx = vec2(-1e9f);
  for (int k = 0; k <= 16; ++k) {
    vec2 h; vec2 c = pathCentre(wl.y, bend, -wl.y * 0.5f + wl.y * k / 16.0f, &h); vec2 n(h.y, -h.x);
    for (float sd : {-0.5f, 0.5f}) { vec2 p = c + n * (wl.x * sd); mn = glm::min(mn, p); mx = glm::max(mx, p); }
  }
}
bool pathLocalCoords(const App& app, const Instance& i, const AssetDef* a, vec3 w, vec2& aa) {
  vec2 wl = app.pathSize(i, a); float bend = clampBend(pathShape(i).bend, wl);
  vec3 lp = glm::inverse(yawQuat(i.yaw)) * (w - i.pos);
  float k = radians(bend) / std::max(wl.y, 1e-3f);
  if (std::fabs(k) < 1e-5f) aa = vec2(lp.x, lp.z);
  else {   // about the centre of curvature (1/k, 0): across = sgn (R - r), along = angle / k
    float sg = k > 0 ? 1.0f : -1.0f; vec2 d(lp.x - 1.0f / k, lp.z);
    aa = vec2(sg * (1.0f / std::fabs(k) - glm::length(d)), std::atan2(sg * d.y, -sg * d.x) / k);
  }
  return std::fabs(aa.x) <= wl.x * 0.5f && std::fabs(aa.y) <= wl.y * 0.5f;
}

static float hash2(float x, float z) { float h = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f; return h - std::floor(h); }

// Builds `m` for a path of size wl lying on height(x, z) (world), centred at `pos` with `yaw`; slabs listed in cut
// are left out. Used for placed paths (terrain heights) and the catalogue preview (flat).
void buildPathMeshDataEx(MeshData& m, const PathDef& p, vec2 wl, vec3 pos, float yaw, const std::function<float(float, float)>& height,
                         const std::vector<uint8_t>* cut, PathShape shape);
void buildPathMeshData(MeshData& m, const PathDef& p, vec2 wl, vec3 pos, float yaw, const std::function<float(float, float)>& height,
                       const std::vector<uint8_t>* cut) {
  buildPathMeshDataEx(m, p, wl, pos, yaw, height, cut, PathShape{});
}
void buildPathMeshDataEx(MeshData& m, const PathDef& p, vec2 wl, vec3 pos, float yaw, const std::function<float(float, float)>& height,
                         const std::vector<uint8_t>* cut, PathShape shape) {
  m.v.clear(); m.i.clear();
  float W = wl.x, L = wl.y, step = 0.5f;
  float bend = p.kind == "concrete" ? 0.0f : clampBend(shape.bend, wl);   // slabs stay straight
  int nx = std::max(2, (int)std::ceil(W / step) + 1), nz = std::max(2, (int)std::ceil(L / step) + 1);
  quat q = yawQuat(yaw);
  bool slabs = p.kind == "concrete";
  int pat = p.kind == "gravel" ? 12 : p.kind == "dirt" ? 13 : 14;
  auto local = [&](int ix, int iz) {
    float x = -W * 0.5f + W * ix / (nx - 1), z = -L * 0.5f + L * iz / (nz - 1);
    if (!slabs && (ix == 0 || ix == nx - 1)) x += (hash2(z * 1.7f, (float)ix) - 0.5f) * 0.16f;   // irregular trodden edge
    return vec2(x, z);
  };
  bool curved = std::fabs(bend) > 1e-3f;
  if (curved) nz = std::max(nz, (int)std::ceil(std::fabs(bend) / 6.0f) + 1);   // at most 6 deg per row
  std::vector<vec3> P((size_t)nx * nz);
  std::vector<vec2> PA((size_t)nx * nz);   // (across, along) path coordinates
  for (int iz = 0; iz < nz; ++iz)
    for (int ix = 0; ix < nx; ++ix) {
      vec2 l = local(ix, iz);   // x across, y along
      vec2 h; vec2 c = pathCentre(L, bend, l.y, &h);
      vec2 xz = curved ? c + vec2(h.y, -h.x) * l.x : l;
      vec3 w = pos + q * vec3(xz.x, 0, xz.y);
      float edge = std::min({std::fabs(l.x - W * 0.5f), std::fabs(l.x + W * 0.5f)});
      float lift = slabs ? 0.045f : (edge < 0.3f ? 0.015f + 0.02f * edge / 0.3f : 0.035f);   // concrete a little proud; soft paths sink in at the margin
      P[(size_t)iz * nx + ix] = vec3(xz.x, height(w.x, w.z) - pos.y + lift, xz.y);
      PA[(size_t)iz * nx + ix] = l;
    }
  quat iq = glm::inverse(q);
  for (int iz = 0; iz < nz; ++iz)
    for (int ix = 0; ix < nx; ++ix) {
      const vec3& c = P[(size_t)iz * nx + ix];
      const vec2& pa = PA[(size_t)iz * nx + ix];
      vec3 dx = P[(size_t)iz * nx + std::min(ix + 1, nx - 1)] - P[(size_t)iz * nx + std::max(ix - 1, 0)];
      vec3 dz = P[(size_t)std::min(iz + 1, nz - 1) * nx + ix] - P[(size_t)std::max(iz - 1, 0) * nx + ix];
      vec3 n = glm::normalize(glm::cross(dz, dx)); if (n.y < 0) n = -n;
      float edge = std::min(pa.x + W * 0.5f, W * 0.5f - pa.x);
      Vertex v; v.pos = c; v.nrm = n; v.albedo = p.albedo;
      v.params = vec4((float)pat, p.rough, 0.0f, std::clamp(edge / 0.45f, 0.0f, 1.0f));   // w: edge factor (soil / verge blend)
      v.pcoord = vec3(pa.x, c.y, pa.y + shape.u0); v.pnrm = n;   // path coordinates: across, along (+ the chain offset)
      m.v.push_back(v);
      (void)iq;
    }
  for (int iz = 0; iz + 1 < nz; ++iz) {
    if (slabs && cut) {
      float zc = (PA[(size_t)iz * nx].y + PA[(size_t)(iz + 1) * nx].y) * 0.5f + L * 0.5f;
      int s = (int)std::floor(zc / p.slab);
      if (s >= 0 && s < (int)cut->size() && (*cut)[s]) continue;   // broken slab: the ground (crater) shows
    }
    for (int ix = 0; ix + 1 < nx; ++ix) {
      uint32_t a = (uint32_t)(iz * nx + ix), b = a + 1, c = a + nx, d = c + 1;
      m.i.insert(m.i.end(), {a, c, b, b, c, d});
    }
  }
}

void App::rebuildPathMesh(const Instance& i, InstanceRT& r) {
  const AssetDef* a = r.asset;
  vec2 wl = pathSize(i, a);
  if (a->path.kind == "concrete") { int n = std::max(1, (int)std::ceil(wl.y / a->path.slab)); if ((int)r.slabCut.size() != n) r.slabCut.assign(n, 0); }
  MeshData m;
  buildPathMeshDataEx(m, a->path, wl, i.pos, i.yaw, [this](float x, float z) { return terrain.height(x, z); }, &r.slabCut, pathShape(i));
  r.pathMesh.destroy(); r.pathMesh.upload(m);
  r.pathGround = groundRev; r.pathRev = i.rev;
}

void App::blastPaths(vec3 p, float dmg, float radius) {
  natureBlast(*this, p, dmg, radius);   // wheat within reach is laid flat, radially (nature.cpp)
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end() || !it->second.asset->path.on || it->second.asset->path.kind != "concrete") continue;
    InstanceRT& r = it->second;
    vec2 wl = pathSize(i, r.asset);
    vec3 lp = glm::inverse(yawQuat(i.yaw)) * (p - i.pos);
    bool any = false;
    for (int s = 0; s < (int)r.slabCut.size(); ++s) {
      if (r.slabCut[s]) continue;
      float z0 = -wl.y * 0.5f + s * r.asset->path.slab, z1 = std::min(z0 + r.asset->path.slab, wl.y * 0.5f);
      vec3 lo(-wl.x * 0.5f, -0.3f, z0), hi(wl.x * 0.5f, 0.3f, z1);
      float d = glm::length(glm::clamp(lp, lo, hi) - lp);
      // a slab breaks when the blast is strong where it sits (HE / AT / launcher), not from a distant grenade
      if (d < radius * 0.6f && dmg * (1.0f - d / std::max(radius, 0.1f)) > r.asset->path.slabHp) { r.slabCut[s] = 1; any = true; }
    }
    if (any) { r.pathGround = ~0u; fx.impact(p, vec3(0, 1, 0), "stone"); }
  }
}

std::string App::pathSurfaceAt(vec3 w) const {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end() || !it->second.asset->path.on) continue;
    const InstanceRT& r = it->second;
    vec2 wl = pathSize(i, r.asset), aa;
    if (!pathLocalCoords(*this, i, r.asset, w, aa)) continue;
    if (r.asset->path.kind == "concrete") {
      int s = (int)std::floor((aa.y + wl.y * 0.5f) / r.asset->path.slab);
      if (s >= 0 && s < (int)r.slabCut.size() && r.slabCut[s]) return "";   // broken slab: bare ground
    }
    return r.asset->path.kind;
  }
  return "";
}

// end-to-end joins: a new path within reach of a free end of a placed one continues it in line (curved pieces: along
// the tangent at that end). Signature kept stable for src/snap.cpp (V6).
bool App::snapPath(const AssetDef* a, vec3& pos, float& yaw) const {
  if (!a->path.on) return false;
  float L = a->path.length, best = 1.2f; bool hit = false;
  for (auto& o : doc.instances) {
    const AssetDef* b = assetFor(o); if (!b || !b->path.on) continue;
    bool curved = std::fabs(pathShape(o).bend) > 1e-3f;
    for (float sgn : {-1.0f, 1.0f}) {
      vec3 end; vec2 od; pathEnd(*this, o, b, sgn, end, od);
      float d = glm::length(vec2(end.x - pos.x, end.z - pos.z));
      if (d < best) {
        best = d; hit = true; vec3 c = end + vec3(od.x, 0, od.y) * (L * 0.5f); pos.x = c.x; pos.z = c.z;
        yaw = curved ? wrapDeg(degrees(std::atan2(od.x, od.y))) : o.yaw;
      }
    }
  }
  if (hit) pos.y = terrain.height(pos.x, pos.z);
  return hit;
}
