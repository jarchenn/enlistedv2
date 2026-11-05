#include "assets.h"
#include "physics.h"
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <windows.h>

// ---------------------------------------------------------------- utilities
void logf(const char* fmt, ...) {
  static FILE* f = nullptr;
  if (!f) f = std::fopen((exeDir() / "normandy.log").string().c_str(), "w");
  va_list ap; va_start(ap, fmt);
  char buf[2048]; std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  if (f) { std::fputs(buf, f); std::fputc('\n', f); std::fflush(f); }
  std::fputs(buf, stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
std::string readTextFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary); if (!in) return {};
  std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
fs::path exeDir() {
  wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return fs::path(buf).parent_path();
}
fs::path contentDir() {
  fs::path p = exeDir() / "content";
  if (fs::exists(p)) return p;
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
void GpuMesh::upload(const MeshData& m) {
  destroy();
  count = (int)m.i.size();
  if (!count) return;
  glCreateVertexArrays(1, &vao);
  glCreateBuffers(1, &vbo); glCreateBuffers(1, &ebo);
  glNamedBufferData(vbo, m.v.size() * sizeof(Vertex), m.v.data(), GL_STATIC_DRAW);
  glNamedBufferData(ebo, m.i.size() * sizeof(uint32_t), m.i.data(), GL_STATIC_DRAW);
  glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
  glVertexArrayElementBuffer(vao, ebo);
  struct A { int n; size_t off; } attrs[] = {
    {3, offsetof(Vertex, pos)}, {3, offsetof(Vertex, nrm)}, {3, offsetof(Vertex, albedo)},
    {4, offsetof(Vertex, params)}, {3, offsetof(Vertex, pcoord)}, {3, offsetof(Vertex, pnrm)}};
  for (int k = 0; k < 6; ++k) {
    glEnableVertexArrayAttrib(vao, k);
    glVertexArrayAttribFormat(vao, k, attrs[k].n, GL_FLOAT, GL_FALSE, (GLuint)attrs[k].off);
    glVertexArrayAttribBinding(vao, k, 0);
  }
}
void GpuMesh::destroy() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ebo) glDeleteBuffers(1, &ebo);
  vao = vbo = ebo = 0; count = 0;
}
void GpuMesh::draw() const {
  if (!count) return;
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

// ---------------------------------------------------------------- mesh building
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
    v.params = vec4((float)mat.pattern, mat.roughness, mat.metallic, (float)mat.grainAxis);
    v.pcoord = lp[k];
    v.pnrm = ln;
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

void appendPart(MeshData& m, const Part& p, const Material& mat, const mat4& xf) {
  if (!p.visible) return;
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
    {"fabric", PAT_FABRIC}, {"brick", PAT_BRICK}, {"painted", PAT_PAINTED}, {"concrete", PAT_CONCRETE}};
  auto it = m.find(s); return it == m.end() ? PAT_PLAIN : it->second;
}

static Part parsePart(const json& j, const std::map<std::string, int>& mats) {
  Part p;
  std::string t = j.value("t", "b");
  auto mit = mats.find(j.value("m", ""));
  p.mat = mit == mats.end() ? 0 : mit->second;
  p.visible = j.value("v", 1) != 0;
  p.collide = j.value("k", 1) != 0;
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
    p.center = jvec3(j["c"]); p.size = jvec3(j["s"], vec3(1));
    if (j.contains("r")) {
      vec3 r = jvec3(j["r"]);
      p.rot = glm::quat(glm::radians(r));
    }
  }
  return p;
}

static bool isGlass(const AssetDef& a, const Part& p) { return a.materials[p.mat].pattern == PAT_GLASS; }

static void partCorners(const Part& p, std::vector<vec3>& out) {
  if (p.shape == Part::BOX) {
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

bool AssetLibrary::loadOne(const fs::path& path) {
  json j;
  try { j = json::parse(readTextFile(path)); } catch (std::exception& e) { logf("asset %s: parse error %s", path.string().c_str(), e.what()); return false; }
  auto a = std::make_unique<AssetDef>();
  a->file = path.filename().string();
  a->raw = j;
  a->id = j.value("id", path.stem().string());
  a->version = j.value("version", 1);
  a->name = j.value("name", a->id);
  a->category = j.value("category", "Misc");
  a->description = j.value("description", "");
  a->floors = j.value("floors", 1);
  a->enterable = j.value("enterable", false);
  if (j.contains("footprint")) a->footprint = vec2(j["footprint"][0].get<float>(), j["footprint"][1].get<float>());
  a->plinthDepth = j.value("plinthDepth", 1.0f);
  a->maxGrade = j.value("maxGrade", 1.0f);
  std::map<std::string, int> mats;
  a->materials.push_back(Material{"default"});
  for (auto& mj : j.value("materials", json::array())) {
    Material m;
    m.name = mj.value("name", "");
    m.albedo = jvec3(mj["albedo"], vec3(0.6f));
    m.roughness = mj.value("roughness", 0.8f);
    m.metallic = mj.value("metallic", 0.0f);
    m.pattern = patternFromName(mj.value("pattern", "plain"));
    m.grainAxis = mj.value("grain", 1);
    mats[m.name] = (int)a->materials.size();
    a->materials.push_back(m);
  }
  for (auto& pj : j.value("parts", json::array())) a->parts.push_back(parsePart(pj, mats));
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
    d.axis = glm::normalize(jvec3(ij["axis"], vec3(0, 1, 0)));
    d.openAngle = ij.value("openAngle", 95.0f);
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
  if (j.contains("walkthrough")) a->walkthrough = j["walkthrough"];

  // meshes
  MeshData op, gl;
  std::vector<vec3> pts;
  std::vector<Part> col;
  for (auto& p : a->parts) {
    appendPart(isGlass(*a, p) ? gl : op, p, a->materials[p.mat], mat4(1));
    if (p.visible) partCorners(p, pts);
    if (p.collide) col.push_back(p);
  }
  a->opaque.upload(op); a->glass.upload(gl);
  a->shape = Physics::buildShape(col);
  for (auto& d : a->interactables)
    for (auto& l : d.leaves) {
      MeshData lo, lg;
      for (auto& p : l.parts) {
        appendPart(isGlass(*a, p) ? lg : lo, p, a->materials[p.mat], mat4(1));
        Part q = p; q.center += l.hinge; partCorners(q, pts);
      }
      l.opaque.upload(lo); l.glass.upload(lg);
      l.shape = Physics::buildShape(l.colliders);
    }
  // destructible wall panels: per-state meshes and shapes, plus a merged intact mesh/compound
  MeshData allIntact;
  std::vector<ShapePtr> intactShapes;
  for (auto& pj : j.value("panels", json::array())) {
    Panel pn;
    pn.id = pj.value("id", "");
    pn.family = pj.value("family", "masonry");
    pn.hp = pj.value("hp", 200.0f);
    pn.bmin = jvec3(pj["min"]); pn.bmax = jvec3(pj["max"]); pn.normal = jvec3(pj["normal"], vec3(0, 0, 1));
    auto& sts = pj["states"];
    for (int s = 0; s < 3 && s < (int)sts.size(); ++s) {
      MeshData m; std::vector<Part> c;
      for (auto& qj : sts[s]) {
        Part p = parsePart(qj, mats);
        pn.parts[s].push_back(p);
        appendPart(m, p, a->materials[p.mat], mat4(1));
        if (s == 0) { appendPart(allIntact, p, a->materials[p.mat], mat4(1)); if (p.visible) partCorners(p, pts); }
        if (p.collide) c.push_back(p);
      }
      pn.mesh[s].upload(m);
      pn.shape[s] = Physics::buildShape(c);
    }
    if (pn.shape[0]) intactShapes.push_back(pn.shape[0]);
    a->panels.push_back(std::move(pn));
  }
  a->panelsIntact.upload(allIntact);
  a->wallsIntact = Physics::buildCompound(intactShapes);
  a->bmin = vec3(1e9f); a->bmax = vec3(-1e9f);
  for (auto& q : pts) { a->bmin = glm::min(a->bmin, q); a->bmax = glm::max(a->bmax, q); }
  if (pts.empty()) { a->bmin = vec3(-1); a->bmax = vec3(1); }
  logf("asset %-28s %5zu parts %3zu interactables %3zu panels %6d tris", a->id.c_str(), a->parts.size(), a->interactables.size(), a->panels.size(), (int)((op.i.size() + allIntact.i.size()) / 3));
  assets.push_back(std::move(a));
  return true;
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
  for (auto& f : files) loadOne(f);
  buildPlaceholder(placeholder_);
  return !assets.empty();
}

const AssetDef* AssetLibrary::find(const std::string& id) const {
  for (auto& a : assets) if (a->id == id) return a.get();
  return nullptr;
}
