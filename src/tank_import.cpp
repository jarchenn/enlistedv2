// --import-vehicle-models: bake the supplied tank GLBs into content/vehicles/models/<dir>/ with the shared weapon
// bake (model.cpp, bakeGlbModel), then a vehicle post-pass on the baked mesh:
//   1. re-origin: x / z centred on the hull part, y = 0 at the lowest running-gear vertex (ground contact);
//   2. "splits": connected triangle islands of one part lying entirely inside a box become a new part with their own
//      role (the Tiger's gun is modelled in one mesh with its turret; the gun must elevate on its own). Nothing is added
//      or reshaped: triangles only change which part they belong to.
// Source GLBs live in the git-ignored assets_src/vehicles/; only the baked derivatives are committed.
#include "tank.h"
#include "model.h"
#include <fstream>
#include <functional>
#include <tuple>
#include <map>
#include <set>

namespace {
struct Mesh { std::vector<TexVertex> V; std::vector<uint32_t> I; };
bool readMesh(const fs::path& p, Mesh& m) {
  std::ifstream f(p, std::ios::binary);
  uint32_t hdr[3] = {0, 0, 0};
  if (!f.read((char*)hdr, sizeof hdr) || hdr[0] != 0x31574D4Eu) return false;
  m.V.resize(hdr[1]); m.I.resize(hdr[2]);
  f.read((char*)m.V.data(), m.V.size() * sizeof(TexVertex)); f.read((char*)m.I.data(), m.I.size() * 4);
  return (bool)f;
}
void writeMesh(const fs::path& p, const Mesh& m) {
  std::ofstream f(p, std::ios::binary);
  uint32_t hdr[3] = {0x31574D4Eu, (uint32_t)m.V.size(), (uint32_t)m.I.size()};
  f.write((const char*)hdr, sizeof hdr);
  f.write((const char*)m.V.data(), m.V.size() * sizeof(TexVertex));
  f.write((const char*)m.I.data(), m.I.size() * 4);
}
}  // namespace

bool postProcessVehicleModel(const fs::path& dir, const json& spec, std::string& report) {
  Mesh m; json mj;
  if (!readMesh(dir / "mesh.bin", m)) { report = "cannot read mesh.bin"; return false; }
  try { mj = json::parse(readTextFile(dir / "model.json")); } catch (...) { report = "cannot read model.json"; return false; }
  json& parts = mj["parts"];
  auto roleIn = [&](const json& p, const json& list) { for (auto& r : list) if (p.value("role", "") == r.get<std::string>()) return true; return false; };
  // ---- 1. re-origin
  json groundRoles = spec.value("groundRoles", json::array({"track", "wheels"}));
  std::string centreRole = spec.value("centreRole", "hull");
  vec3 hmin(1e9f), hmax(-1e9f); float gy = 1e9f;
  for (auto& p : parts) {
    uint32_t first = p["first"], count = p["count"];
    for (uint32_t k = first; k < first + count; ++k) {
      const vec3& v = m.V[m.I[k]].pos;
      if (p.value("role", "") == centreRole) { hmin = glm::min(hmin, v); hmax = glm::max(hmax, v); }
      if (roleIn(p, groundRoles)) gy = std::min(gy, v.y);
    }
  }
  if (hmax.x < hmin.x || gy > 1e8f) { report = "no hull / running-gear parts to place the origin"; return false; }
  vec3 shift(-(hmin.x + hmax.x) * 0.5f, -gy, -(hmin.z + hmax.z) * 0.5f);
  shift += jvec3(spec.value("originOffset", json()), vec3(0));
  for (auto& v : m.V) v.pos += shift;
  // ---- 2. splits (islands entirely inside a box move to a new part)
  int splitTris = 0;
  for (auto& sp : spec.value("splits", json::array())) {
    std::string from = sp.value("part", ""), name = sp.value("name", "split"), role = sp.value("role", "body");
    vec3 lo = jvec3(sp["islandsIn"][0]), hi = jvec3(sp["islandsIn"][1]);
    int pi = -1;
    for (size_t k = 0; k < parts.size(); ++k) if (parts[k].value("name", "") == from) pi = (int)k;
    if (pi < 0) { report += " split: no part '" + from + "';"; continue; }
    uint32_t first = parts[pi]["first"], count = parts[pi]["count"];
    // islands over the part's vertices: joined by triangles and by coincident positions (UV seams)
    std::map<uint32_t, uint32_t> par;
    std::function<uint32_t(uint32_t)> find = [&](uint32_t a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
    for (uint32_t k = first; k < first + count; ++k) par.emplace(m.I[k], m.I[k]);
    auto uni = [&](uint32_t a, uint32_t b) { a = find(a); b = find(b); if (a != b) par[a] = b; };
    for (uint32_t k = first; k + 2 < first + count; k += 3) { uni(m.I[k], m.I[k + 1]); uni(m.I[k], m.I[k + 2]); }
    std::map<std::tuple<long, long, long>, uint32_t> at;
    for (auto& [v, _] : par) {
      auto key = std::make_tuple(std::lround(m.V[v].pos.x * 1e4f), std::lround(m.V[v].pos.y * 1e4f), std::lround(m.V[v].pos.z * 1e4f));
      auto it = at.find(key); if (it == at.end()) at[key] = v; else uni(v, it->second);
    }
    // optional "notIn" box: an island with any vertex inside it stays (keeps neighbouring parts out of the split)
    bool hasNot = sp.contains("notIn"); vec3 nlo(0), nhi(0);
    if (hasNot) { nlo = jvec3(sp["notIn"][0]); nhi = jvec3(sp["notIn"][1]); }
    std::map<uint32_t, bool> inside;
    for (auto& [v, _] : par) {
      const vec3& p = m.V[v].pos; bool in = glm::all(glm::greaterThanEqual(p, lo)) && glm::all(glm::lessThanEqual(p, hi));
      if (hasNot && glm::all(glm::greaterThanEqual(p, nlo)) && glm::all(glm::lessThanEqual(p, nhi))) in = false;
      auto r = inside.emplace(find(v), in); if (!in) r.first->second = false;
    }
    std::vector<uint32_t> keep, moved;
    for (uint32_t k = first; k + 2 < first + count; k += 3) {
      auto& dst = inside[find(m.I[k])] ? moved : keep;
      dst.insert(dst.end(), {m.I[k], m.I[k + 1], m.I[k + 2]});
    }
    if (moved.empty()) { report += " split '" + name + "': no islands inside the box;"; continue; }
    // the part keeps the front of its range; the new part takes the rest (same material)
    std::copy(keep.begin(), keep.end(), m.I.begin() + first);
    std::copy(moved.begin(), moved.end(), m.I.begin() + first + keep.size());
    json np = parts[pi];
    parts[pi]["count"] = (uint32_t)keep.size();
    np["name"] = name; np["role"] = role; np["first"] = first + (uint32_t)keep.size(); np["count"] = (uint32_t)moved.size();
    parts.insert(parts.begin() + pi + 1, np);
    splitTris += (int)moved.size() / 3;
  }
  // ---- bounds
  vec3 bmin(1e9f), bmax(-1e9f);
  for (auto& p : parts) {
    vec3 pmin(1e9f), pmax(-1e9f);
    uint32_t first = p["first"], count = p["count"];
    for (uint32_t k = first; k < first + count; ++k) { pmin = glm::min(pmin, m.V[m.I[k]].pos); pmax = glm::max(pmax, m.V[m.I[k]].pos); }
    if (count == 0) pmin = pmax = vec3(0);
    p["bmin"] = tojson(pmin); p["bmax"] = tojson(pmax);
    bmin = glm::min(bmin, pmin); bmax = glm::max(bmax, pmax);
  }
  mj["bmin"] = tojson(bmin); mj["bmax"] = tojson(bmax);
  mj["vehicleOrigin"] = {{"shift", tojson(shift)}, {"note", "x/z centred on the hull, y = 0 at the running gear's lowest point"}};
  writeMesh(dir / "mesh.bin", m);
  { std::ofstream f(dir / "model.json"); f << mj.dump(1); }
  char b[256];
  std::snprintf(b, sizeof b, " origin shift (%.3f %.3f %.3f), %d triangles split, bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f)", shift.x, shift.y, shift.z, splitTris, bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);
  report += b;
  return true;
}

bool importVehicleModels(const fs::path& srcDir, const fs::path& contentRoot) {
  fs::path vdir = contentRoot / "vehicles";
  int ok = 0, bad = 0;
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(vdir)) if (e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    json j; try { j = json::parse(readTextFile(f)); } catch (...) { continue; }
    if (!j.contains("model")) continue;
    ImportSpec s = importSpecFromJson(j["model"]);
    if (s.src.empty() || s.dir.empty()) continue;
    fs::path glb = srcDir / s.src;
    if (!fs::exists(glb)) { logf("import vehicle %-10s skipped: %s not found", s.dir.c_str(), glb.string().c_str()); continue; }
    std::string err, sum, rep;
    fs::path out = vdir / "models" / s.dir;
    if (!bakeGlbModel(glb, s, out, err, sum)) { logf("import vehicle %-10s FAILED: %s", s.dir.c_str(), err.c_str()); ++bad; continue; }
    if (!postProcessVehicleModel(out, j["model"], rep)) { logf("import vehicle %-10s post-pass FAILED: %s", s.dir.c_str(), rep.c_str()); ++bad; continue; }
    logf("import vehicle %-10s %s |%s", s.dir.c_str(), sum.c_str(), rep.c_str());
    ++ok;
  }
  logf("import vehicles: %d baked, %d failed", ok, bad);
  return bad == 0;
}
