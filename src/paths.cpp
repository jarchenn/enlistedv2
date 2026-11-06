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
#include "app.h"
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
  i.extra["path"] = {{"width", std::clamp(wl.x, a->path.minW, a->path.maxW)}, {"length", std::clamp(wl.y, a->path.minL, a->path.maxL)}};
  i.rev++;
}

static float hash2(float x, float z) { float h = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f; return h - std::floor(h); }

// Builds `m` for a path of size wl lying on height(x, z) (world), centred at `pos` with `yaw`; slabs listed in cut
// are left out. Used for placed paths (terrain heights) and the catalogue preview (flat).
void buildPathMeshData(MeshData& m, const PathDef& p, vec2 wl, vec3 pos, float yaw, const std::function<float(float, float)>& height,
                       const std::vector<uint8_t>* cut) {
  m.v.clear(); m.i.clear();
  float W = wl.x, L = wl.y, step = 0.5f;
  int nx = std::max(2, (int)std::ceil(W / step) + 1), nz = std::max(2, (int)std::ceil(L / step) + 1);
  quat q = yawQuat(yaw);
  bool slabs = p.kind == "concrete";
  int pat = p.kind == "gravel" ? 12 : p.kind == "dirt" ? 13 : 14;
  auto local = [&](int ix, int iz) {
    float x = -W * 0.5f + W * ix / (nx - 1), z = -L * 0.5f + L * iz / (nz - 1);
    if (!slabs && (ix == 0 || ix == nx - 1)) x += (hash2(z * 1.7f, (float)ix) - 0.5f) * 0.16f;   // irregular trodden edge
    return vec2(x, z);
  };
  std::vector<vec3> P((size_t)nx * nz);
  for (int iz = 0; iz < nz; ++iz)
    for (int ix = 0; ix < nx; ++ix) {
      vec2 l = local(ix, iz);
      vec3 w = pos + q * vec3(l.x, 0, l.y);
      float edge = std::min({std::fabs(l.x - W * 0.5f), std::fabs(l.x + W * 0.5f)});
      float lift = slabs ? 0.045f : (edge < 0.3f ? 0.015f + 0.02f * edge / 0.3f : 0.035f);   // concrete a little proud; soft paths sink in at the margin
      P[(size_t)iz * nx + ix] = vec3(l.x, height(w.x, w.z) - pos.y + lift, l.y);
    }
  quat iq = glm::inverse(q);
  for (int iz = 0; iz < nz; ++iz)
    for (int ix = 0; ix < nx; ++ix) {
      const vec3& c = P[(size_t)iz * nx + ix];
      vec3 dx = P[(size_t)iz * nx + std::min(ix + 1, nx - 1)] - P[(size_t)iz * nx + std::max(ix - 1, 0)];
      vec3 dz = P[(size_t)std::min(iz + 1, nz - 1) * nx + ix] - P[(size_t)std::max(iz - 1, 0) * nx + ix];
      vec3 n = glm::normalize(glm::cross(dz, dx)); if (n.y < 0) n = -n;
      float edge = std::min(c.x + W * 0.5f, W * 0.5f - c.x);
      Vertex v; v.pos = c; v.nrm = n; v.albedo = p.albedo;
      v.params = vec4((float)pat, p.rough, 0.0f, std::clamp(edge / 0.45f, 0.0f, 1.0f));   // w: edge factor (soil / verge blend)
      v.pcoord = c; v.pnrm = n;
      m.v.push_back(v);
      (void)iq;
    }
  for (int iz = 0; iz + 1 < nz; ++iz) {
    if (slabs && cut) {
      float zc = (P[(size_t)iz * nx].z + P[(size_t)(iz + 1) * nx].z) * 0.5f + L * 0.5f;
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
  buildPathMeshData(m, a->path, wl, i.pos, i.yaw, [this](float x, float z) { return terrain.height(x, z); }, &r.slabCut);
  r.pathMesh.destroy(); r.pathMesh.upload(m);
  r.pathGround = groundRev; r.pathRev = i.rev;
}

void App::blastPaths(vec3 p, float dmg, float radius) {
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
    vec2 wl = pathSize(i, r.asset);
    vec3 lp = glm::inverse(yawQuat(i.yaw)) * (w - i.pos);
    if (std::fabs(lp.x) > wl.x * 0.5f || std::fabs(lp.z) > wl.y * 0.5f) continue;
    if (r.asset->path.kind == "concrete") {
      int s = (int)std::floor((lp.z + wl.y * 0.5f) / r.asset->path.slab);
      if (s >= 0 && s < (int)r.slabCut.size() && r.slabCut[s]) return "";   // broken slab: bare ground
    }
    return r.asset->path.kind;
  }
  return "";
}

// end-to-end joins: a new path within reach of a free end of a placed one continues it in line
bool App::snapPath(const AssetDef* a, vec3& pos, float& yaw) const {
  if (!a->path.on) return false;
  float L = a->path.length, best = 1.2f; bool hit = false;
  for (auto& o : doc.instances) {
    const AssetDef* b = assetFor(o); if (!b || !b->path.on) continue;
    vec2 wl = pathSize(o, b);
    for (float sgn : {-1.0f, 1.0f}) {
      vec3 dir = yawQuat(o.yaw) * vec3(0, 0, sgn);
      vec3 end = o.pos + dir * (wl.y * 0.5f);
      float d = glm::length(vec2(end.x - pos.x, end.z - pos.z));
      if (d < best) { best = d; hit = true; vec3 c = end + dir * (L * 0.5f); pos.x = c.x; pos.z = c.z; yaw = o.yaw; }
    }
  }
  if (hit) pos.y = terrain.height(pos.x, pos.z);
  return hit;
}
