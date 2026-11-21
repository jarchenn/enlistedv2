// BR3 Battle Royale models (br_models.h): the marine body, worn gear, optics on the guns, the inventory preview.
#include "br_models.h"
#include <stb_image.h>
#include "app.h"
#include "game.h"
#include "character.h"
#include <map>
#include <cstring>
#include <stb_image_write.h>

int brMarineModel(Game& G) {
  static int idx = -2;
  if (idx >= -1) return idx;
  idx = -1;
  if (G.modelIdx.count("br_marine")) return idx = G.modelIdx["br_marine"];
  auto cm = std::make_unique<CharModel>(); std::string e;
  if (!cm->load(contentDir() / "characters" / "br_marine.glb", e)) { logf("br: br_marine.glb not loaded (%s): the soldier body stays", e.c_str()); return idx; }
  idx = G.modelIdx["br_marine"] = (int)G.models.size(); G.models.push_back(std::move(cm));
  return idx;
}

static void addAsset(const AssetDef* a, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  if (a->model) { for (int k = 0; k < (int)a->model->parts.size(); ++k) md.push_back({a->model.get(), k, W}); }
  else if (a->opaque.count) boxes.push_back({&a->opaque, W});   // the committed stand-in box
}

static const char* wornId(int slot, int lvl) {
  static const char* ids[3][4] = {{"", "br_helmet_1", "br_helmet_2", "br_helmet_3"}, {"", "br_vest_1", "br_vest_2", "br_vest_3"}, {"", "br_backpack", "br_backpack_mil", "br_backpack_mil"}};
  return ids[slot][std::clamp(lvl, 0, 3)];
}

void brWornDraws(App& app, Game& G, const Soldier& s, const mat4& world, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  if (s.model < 0 || s.model >= (int)G.models.size() || s.pose.delta.empty()) return;
  const CharModel& m = *G.models[s.model];
  int lv[3] = {s.helmetLvl, s.vestLvl, s.backpack};
  float vestOut = 0;   // BR8: a backpack goes on top of the vest: out by how far the vest's back stands off the body
  if (s.vestLvl > 0) if (const AssetDef* v = app.lib.find(wornId(1, s.vestLvl))) if (v->raw.contains("worn")) vestOut = jkey(v->raw, "worn").value("backOut", 0.0f);
  for (int slot = 0; slot < 3; ++slot) {
    if (lv[slot] <= 0) continue;
    const AssetDef* a = app.lib.find(wornId(slot, lv[slot]));
    if (!a || !a->raw.contains("worn")) continue;
    const json& w = a->raw["worn"];
    int cj = w.value("joint", "Spine2") == "Head" ? CJ_HEAD : CJ_SPINE2, j = m.semantic[cj];
    if (j < 0 || j >= (int)s.pose.delta.size()) continue;
    vec3 pos = jvec3(jkey(w, "pos"), vec3(0)), sc = jvec3(jkey(w, "scale"), vec3(1)), rest = w.contains("rest") ? jvec3(w["rest"]) : m.jointHead[j];
    pos += m.jointHead[j] - rest;   // fitted to the marine; another body carries it from its own joint
    if (slot == 2) pos.z -= vestOut;   // the back is -z (the body faces +z)
    // AN1 step 4: a hand-tuned correction on top of the baked fit ("wornFix": {"pos": [m], "rot": [deg x, y, z]}, about the item's base),
    // and a vest rides between the two upper spine joints so a bent torso does not drive its lower edge into the belly
    mat4 J = s.pose.delta[j];
    if (slot == 1 && m.semantic[CJ_SPINE1] >= 0 && m.semantic[CJ_SPINE1] != j) {
      const mat4& J1 = s.pose.delta[m.semantic[CJ_SPINE1]];
      quat q = glm::slerp(glm::normalize(glm::quat_cast(glm::mat3(J1))), glm::normalize(glm::quat_cast(glm::mat3(J))), 0.5f);
      vec3 piv = pos;   // blend the two deltas as they act on the vest's base point
      vec3 p = glm::mix(vec3(J1 * vec4(piv, 1)), vec3(J * vec4(piv, 1)), 0.5f);
      J = glm::translate(mat4(1), p) * glm::mat4_cast(q) * glm::translate(mat4(1), -piv);
    }
    mat4 fixR(1);
    if (a->raw.contains("wornFix")) { const json& f = a->raw["wornFix"]; pos += jvec3(f.value("pos", json()), vec3(0)); vec3 r = jvec3(f.value("rot", json()), vec3(0)); fixR = glm::mat4_cast(glm::quat(glm::radians(r))); sc *= jvec3(f.value("scale", json()), vec3(1)); }
    addAsset(a, world * J * glm::translate(mat4(1), pos) * fixR * glm::scale(mat4(1), sc), md, boxes);
  }
}

bool brHidePart(const Soldier& s, const std::string& partName) { return s.helmetLvl > 0 && partName.find("Hats") != std::string::npos; }

// ---------------------------------------------------------------- optics
static const char* sightId(int k) { static const char* ids[8] = {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x", "br_sight_holo", "br_sight_thermal", "br_sight_pistol"}; return ids[std::clamp(k, 0, 7)]; }   // AT1: 5 the holographic sight; TH1: 6 the thermal scope
// AT1: an optic's sight block: the local bake's own ("modelSight") when its model is loaded, else the committed stand-in's
static const json& sightOf(const AssetDef& a) { return a.model && a.raw.contains("modelSight") ? a.raw["modelSight"] : a.raw["sight"]; }

struct Rail { vec3 p{0}; float scale = 1; };
// AT7: the gun's top surface (weapon space): per 2 mm x / z cell the highest point of its drawn model's triangles
struct TopMap { float x0 = 0, z0 = 0; int nx = 0, nz = 0; std::vector<float> y; float at(int i, int k) const { return y[(size_t)k * nx + i]; } };
struct Riser { float x, z0, z1, y0, w; };   // AT7: a steel block from the gun's surface up to a foot over a lower part of the top
struct Seat { vec3 base{0}; float s = 1; std::vector<Riser> risers; };
static std::map<const WeaponDef*, TopMap> gTopCache;
static std::map<std::pair<const WeaponDef*, int>, Seat> gSeatCache;
void brRailCacheClear() { gTopCache.clear(); gSeatCache.clear(); }   // keyed by definition: cleared when the weapons reload (ED2)
static Rail railOf(const WeaponDef& d) {   // the gun JSON "rail" / "railScale" (AT7: as loaded, local overrides included); none: just under the rear sight
  Rail r; r.p = d.hasRail ? d.rail : d.rear + vec3(0, -0.012f, 0.05f); r.scale = d.railScale; return r;
}
static const TopMap& topOf(const WeaponDef& d) {
  auto it = gTopCache.find(&d); if (it != gTopCache.end()) return it->second;
  TopMap& t = gTopCache[&d];
  if (!d.useModel() || d.model->cpuIdx.empty()) return t;
  const GpuModel& g = *d.model; const float C = 0.002f;
  t.x0 = g.bmin.x - C; t.z0 = g.bmin.z - C; t.nx = (int)((g.bmax.x - g.bmin.x) / C) + 3; t.nz = (int)((g.bmax.z - g.bmin.z) / C) + 3;
  t.y.assign((size_t)t.nx * t.nz, -1e9f);
  for (int r = 0; r < 8; ++r) for (int pk : d.modelParts[r]) {   // the parts the gun draws (hidden ones out: AT5's SMG-9 sight)
    if (std::find(g.ironParts.begin(), g.ironParts.end(), pk) != g.ironParts.end() || std::find(g.opticParts.begin(), g.opticParts.end(), pk) != g.opticParts.end()) continue;   // its sights fold away, its factory optic comes off (AT8)
    const ModelPart& p = g.parts[pk];
    for (uint32_t q = p.first; q + 2 < p.first + p.count; q += 3) {
      vec3 A = g.cpuPos[g.cpuIdx[q]], B = g.cpuPos[g.cpuIdx[q + 1]], Cc = g.cpuPos[g.cpuIdx[q + 2]];
      float den = (B.z - Cc.z) * (A.x - Cc.x) + (Cc.x - B.x) * (A.z - Cc.z);
      int i0 = std::max(0, (int)((std::min({A.x, B.x, Cc.x}) - t.x0) / C)), i1 = std::min(t.nx - 1, (int)((std::max({A.x, B.x, Cc.x}) - t.x0) / C));
      int k0 = std::max(0, (int)((std::min({A.z, B.z, Cc.z}) - t.z0) / C)), k1 = std::min(t.nz - 1, (int)((std::max({A.z, B.z, Cc.z}) - t.z0) / C));
      for (int k = k0; k <= k1; ++k) for (int i = i0; i <= i1; ++i) {
        float x = t.x0 + (i + 0.5f) * C, z = t.z0 + (k + 0.5f) * C, y;
        if (std::fabs(den) < 1e-12f) {   // seen edge-on from above: its highest corner in the cell
          if (glm::length(vec2(x, z) - vec2(A.x, A.z)) > C && glm::length(vec2(x, z) - vec2(B.x, B.z)) > C && glm::length(vec2(x, z) - vec2(Cc.x, Cc.z)) > C) continue;
          y = std::max({A.y, B.y, Cc.y});
        } else {
          float a = ((B.z - Cc.z) * (x - Cc.x) + (Cc.x - B.x) * (z - Cc.z)) / den, b = ((Cc.z - A.z) * (x - Cc.x) + (A.x - Cc.x) * (z - Cc.z)) / den, c = 1 - a - b;
          if (a < -1e-4f || b < -1e-4f || c < -1e-4f) continue;
          y = a * A.y + b * B.y + c * Cc.y;
        }
        float& m = t.y[(size_t)k * t.nx + i]; m = std::max(m, y);
      }
    }
  }
  return t;
}
// AT7: the optic's mount feet (asset space): its lowest vertices (within 3 mm of the bottom) in runs along z, each with its half width
struct Foot { float z0, z1, hw; };
static std::vector<Foot> feetOf(const AssetDef& a) {
  std::vector<Foot> f;
  if (a.model && !a.model->cpuPos.empty()) {
    std::vector<vec3> b; for (const vec3& p : a.model->cpuPos) if (p.y < a.model->bmin.y + 0.003f) b.push_back(p);
    std::sort(b.begin(), b.end(), [](const vec3& u, const vec3& v) { return u.z < v.z; });
    for (const vec3& p : b) {
      if (f.empty() || p.z > f.back().z1 + 0.005f) f.push_back({p.z, p.z, std::fabs(p.x)});
      else { f.back().z1 = p.z; f.back().hw = std::max(f.back().hw, std::fabs(p.x)); }
    }
  }
  if (f.empty()) { vec3 lo = a.model ? a.model->bmin : a.opaque.bmin, hi = a.model ? a.model->bmax : a.opaque.bmax; f.push_back({lo.z, hi.z, 0.5f * (hi.x - lo.x)}); }   // the stand-in box: its whole bottom
  return f;
}
// AT7: optic k seated on the gun: every foot over the gun's top surface (TopMap, within +-8 mm of the rail's centre line), slid up to
// 4 cm along the rail to where the feet's surfaces are most even (a flat dust cover or rail rather than a sight block), its base on
// the highest surface under its feet (none sinks into the gun; on a stepped top the lower foot stands off by the step);
// "scale" in the optic's sight block sizes the model
static const Seat& seatOf(const WeaponDef& d, int k, const AssetDef& a) {
  auto key = std::make_pair(&d, k); auto it = gSeatCache.find(key); if (it != gSeatCache.end()) return it->second;
  Seat& st = gSeatCache[key]; const Rail r = railOf(d);
  st.s = r.scale * sightOf(a).value("scale", 1.0f); st.base = r.p;
  const TopMap& t = topOf(d); if (t.nx == 0) return st;
  const std::vector<Foot> feet = feetOf(a); const float C = 0.002f;
  float best = 1e9f, bestY = r.p.y, bestDz = 0, bestSpread = 0; int bestUns = 0; std::vector<float> bestH;
  for (float dz = -0.04f; dz <= 0.0401f; dz += C) {
    float hmax = -1e9f, hmin = 1e9f; int uns = 0; std::vector<float> hs;
    for (const Foot& f : feet) {
      float za = r.p.z + dz + f.z0 * st.s, zb = r.p.z + dz + f.z1 * st.s, hw = std::min(f.hw * st.s, 0.008f), h = -1e9f; int cells = 0, hit = 0;
      for (int kk = std::max(0, (int)((za - t.z0) / C)); kk <= std::min(t.nz - 1, (int)((zb - t.z0) / C)); ++kk)
        for (int i = std::max(0, (int)((r.p.x - hw - t.x0) / C)); i <= std::min(t.nx - 1, (int)((r.p.x + hw - t.x0) / C)); ++i) { ++cells; if (t.at(i, kk) > -1e8f) { ++hit; h = std::max(h, t.at(i, kk)); } }
      hs.push_back(hit * 3 < cells || hit == 0 ? -1e9f : h);
      if (hit * 3 < cells || hit == 0) { ++uns; continue; }   // no gun under this foot
      hmax = std::max(hmax, h); hmin = std::min(hmin, h);
    }
    if (hmax < -1e8f) continue;
    float cost = (hmax - hmin) + 0.04f * uns + 0.01f * std::fabs(dz);   // 1 cm of slide costs as much as 0.1 mm of step
    if (cost < best) { best = cost; bestY = hmax; bestDz = dz; bestSpread = hmax - hmin; bestUns = uns; bestH = hs; }
  }
  if (best < 1e8f) {
    st.base.y = bestY; st.base.z = r.p.z + bestDz;
    for (size_t i = 0; i < feet.size(); ++i) if (bestH[i] > -1e8f && bestY - bestH[i] > 0.0008f)   // a foot standing off a lower part of the top: a riser under it
      st.risers.push_back({r.p.x, st.base.z + feet[i].z0 * st.s, st.base.z + feet[i].z1 * st.s, bestH[i] - 0.001f, std::min(2 * feet[i].hw * st.s, 0.022f)});
  }
  logf("optic seat: %-20s %-18s rail (%.4f %.4f) -> (%.4f %.4f), %d feet, step under them %.1f mm, %d unsupported", d.id.c_str(), a.id.c_str(), r.p.y, r.p.z, st.base.y, st.base.z, (int)feet.size(), bestSpread * 1000, bestUns);
  return st;
}

// AT7 audit (selftest NMK_OPTIC_DUMP): each foot against the gun's top at a mount; BEFORE = the committed JSON rail as railOf
// read it (no seat), the ACOG as baked before (half a turn: its feet mirrored), the thermal at full size; AFTER = the seat
json brOpticAudit(const WeaponDef& d, int k) {
  const AssetDef* a = k > 0 ? gApp->lib.find(sightId(k)) : nullptr;
  if (!a || !a->raw.contains("sight") || d.hasScope) return json();
  const TopMap& t = topOf(d); const float C = 0.002f;
  auto eval = [&](vec3 base, float s, const std::vector<Foot>& feet) {
    float hover = -1e9f, clip = 0, step = 0, hmax = -1e9f, hmin = 1e9f; int uns = 0;
    for (const Foot& f : feet) {
      float za = base.z + f.z0 * s, zb = base.z + f.z1 * s, hw = std::min(f.hw * s, 0.008f), h = -1e9f; int cells = 0, hit = 0;
      for (int kk = std::max(0, (int)((za - t.z0) / C)); kk <= std::min(t.nz - 1, (int)((zb - t.z0) / C)); ++kk)
        for (int i = std::max(0, (int)((base.x - hw - t.x0) / C)); i <= std::min(t.nx - 1, (int)((base.x + hw - t.x0) / C)); ++i) { ++cells; if (t.at(i, kk) > -1e8f) { ++hit; h = std::max(h, t.at(i, kk)); } }
      if (hit * 3 < cells || hit == 0) { ++uns; continue; }
      hover = std::max(hover, base.y - h); clip = std::max(clip, h - base.y); hmax = std::max(hmax, h); hmin = std::min(hmin, h);
    }
    if (hmax > -1e8f) step = hmax - hmin;
    vec3 lo = a->model ? a->model->bmin : a->opaque.bmin, hi = a->model ? a->model->bmax : a->opaque.bmax;
    return json{{"hover_mm", hover > -1e8f ? hover * 1000 : -1}, {"clip_mm", clip * 1000}, {"step_mm", step * 1000}, {"feet", feet.size()}, {"unsupported", uns}, {"len_cm", (hi.z - lo.z) * s * 100}, {"y", base.y}, {"z", base.z}};
  };
  std::vector<Foot> feet = feetOf(*a), fb = feet;
  if (k == 2) for (Foot& f : fb) { float z0 = f.z0; f.z0 = -f.z1; f.z1 = -z0; }
  Rail rb; rb.p = d.rear + vec3(0, -0.012f, 0.05f);
  try { json j = json::parse(readTextFile(d.file)); if (j.contains("rail")) rb.p = jvec3(jkey(j, "rail"), rb.p); rb.scale = j.value("railScale", 1.0f); } catch (...) {}
  const Seat& st = seatOf(d, k, *a);
  json after = eval(st.base, st.s, feet); after["risers"] = st.risers.size();
  float gl = d.useModel() ? d.model->bmax.z - d.model->bmin.z : 0;
  return json{{"before", eval(rb.p, rb.scale * (k == 6 ? 1.0f : sightOf(*a).value("scale", 1.0f)), fb)}, {"after", after}, {"gunLen_cm", gl * 100}, {"acogTurned", k == 2}};
}

// weapon-space mount of optic k (asset space -> weapon space)
static bool sightMount(const WeaponDef& d, int k, const AssetDef*& a, mat4& M, vec3& eyeW) {
  if (d.hasScope) return false;   // scoped rifles keep their own model: a fitted optic changes only the magnification and reticle (loot.cpp lootScope)
  a = k > 0 ? gApp->lib.find(sightId(k)) : nullptr;
  if (!a || !a->raw.contains("sight")) return false;
  const Seat& st = seatOf(d, k, *a);
  vec3 eye = jvec3(sightOf(*a)["eye"], vec3(0, 0.04f, 0)) * st.s;
  M = glm::translate(mat4(1), st.base) * glm::scale(mat4(1), vec3(st.s));
  eyeW = st.base + eye;
  return true;
}

// AT2: the red dot's mark and the holographic reticle are drawn on the HUD at their angular size (sight_overlay.cpp
// drawReflexReticle): a mesh mark sized for 2 MOA is under a pixel and shimmers; the holo keeps its model's layout

void brSightDraws(App& app, const WeaponDef& d, const WeaponState& ws, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  (void)app; const AssetDef* a; mat4 M; vec3 e;
  if (!sightMount(d, fittedSight(d, ws), a, M, e)) return;   // AT5: a gun's default optic too
  for (const GpuModel* g : {d.model.get(), d.modelLod.get()}) if (g && (!g->ironParts.empty() || (ws.att[0] && !g->opticParts.empty())))   // AT7: the gun's own sights fold away under an optic (whole pieces); AT8: a fitted sight replaces the factory optic
    md.erase(std::remove_if(md.begin(), md.end(), [&](const ModelDraw& x) { return x.model == g && (std::find(g->ironParts.begin(), g->ironParts.end(), x.part) != g->ironParts.end() || (ws.att[0] && std::find(g->opticParts.begin(), g->opticParts.end(), x.part) != g->opticParts.end())); }), md.end());
  size_t n0 = md.size(); addAsset(a, W * M, md, boxes);
  for (size_t i = n0; i < md.size(); ++i) md[i].optic = true;   // AT6: the window clip leaves the optic itself whole
  static GpuMesh riser;   // AT7: a unit block (standing on y = 0), dark parkerized steel
  if (!riser.count) { MeshData m; Part p; p.center = vec3(0, 0.5f, 0); p.size = vec3(1); Material mt; mt.albedo = vec3(0.05f, 0.05f, 0.048f); mt.roughness = 0.6f; mt.metallic = 0.6f; mt.pattern = PAT_METAL; appendPart(m, p, mt, mat4(1)); riser.upload(m); }
  const vec3 base = vec3(M[3]);
  for (const Riser& rs : seatOf(d, fittedSight(d, ws), *a).risers)
    boxes.push_back({&riser, W * glm::translate(mat4(1), vec3(rs.x, rs.y0, 0.5f * (rs.z0 + rs.z1))) * glm::scale(mat4(1), vec3(rs.w, base.y - rs.y0, std::max(0.004f, rs.z1 - rs.z0)))});
}

// AT2: the fitted red dot / holographic sight's window (weapon space): its centre on the eye line, its radius; the mount
// (asset -> weapon space) and the asset's bounds for the scale check
bool brSightWindow(const WeaponDef& d, const WeaponState& ws, vec3& centre, float& radius, mat4* mount, vec3* bmin, vec3* bmax) {
  const AssetDef* a; mat4 M; vec3 e;
  if (!sightMount(d, fittedSight(d, ws), a, M, e)) return false;
  const json& s = sightOf(*a);
  vec3 eye = jvec3(jkey(s, "eye"), vec3(0, 0.035f, -0.04f)); float L = s.value("length", 0.085f);
  centre = vec3(M * vec4(eye.x, eye.y, eye.z + 0.6f * L, 1));
  radius = (fittedSight(d, ws) == 5 ? 0.013f : fittedSight(d, ws) == 7 ? 0.007f : 0.010f)   // PS1: the pistol dot's small window
           * glm::length(vec3(M[0]));   // the open window around the eye line
  if (mount) *mount = M;
  if (bmin && bmax) { if (a->model) { *bmin = a->model->bmin; *bmax = a->model->bmax; } else { *bmin = a->opaque.bmin; *bmax = a->opaque.bmax; } }
  return true;
}

bool brSightLine(const WeaponDef& d, const WeaponState& ws, vec3& R, vec3& F, float& relief) {
  const AssetDef* a; mat4 M; vec3 e;
  if (!sightMount(d, fittedSight(d, ws), a, M, e)) return false;
  R = e; F = e + vec3(0, 0, 0.25f);
  relief = fittedSight(d, ws) == 7 ? 0.35f : fittedSight(d, ws) == 1 || fittedSight(d, ws) == 5 ? 0.28f : 0.09f;   // CP: a red dot / holo 0.28 m from the eye (at 0.16 m its box filled ~25 deg of the aimed view)   // PS1: a pistol dot at arm's length   // red dot / holographic: held further back; magnified optics: at the eyepiece
  return true;
}

bool brSightLineActive(const WeaponDef& d, vec3& R, vec3& F, float& relief) {
  auto it = gApp->wstate.find(d.id);
  return it != gApp->wstate.end() && brSightLine(d, it->second, R, F, relief);
}

// ---------------------------------------------------------------- inventory preview
static CharInstance gInst; static const CharModel* gInstOf = nullptr;
// RGBA, top row first; cy / halfH > 0: a close-up centred at that height showing +-halfH (the worn-gear captures)
static bool renderPreview(Soldier& L, float yaw, int w, int h, std::vector<uint8_t>& px, float cy = 0, float halfH = 0) {
  App& app = *gApp; Game& G = *gGame;
  const CharModel& m = *G.models[L.model];
  if (gInstOf != &m) { if (gInstOf) gInst.destroy(); gInst.create(m); gInstOf = &m; }
  gInst.skin(L.pose.skin);
  std::vector<ModelDraw> md; std::vector<DrawItem> boxes;
  mat4 world = glm::rotate(mat4(1), glm::radians(yaw), vec3(0, 1, 0));
  for (int k = 0; k < (int)gInst.view.parts.size(); ++k) if (!brHidePart(L, gInst.view.parts[k].name)) md.push_back({&gInst.view, k, world});
  brWornDraws(app, G, L, world, md, boxes);
  // far enough for 2.1 m of height and 0.85 m of width (a narrow frame may crop the hands of a held rifle while turning)
  float tv = std::tan(glm::radians(17.0f)), dist = std::max(1.05f / tv, 0.42f / (tv * (float)w / h)) + 0.4f;
  vec3 c(0, 0.95f, 0), eye(0, 1.1f, dist);
  if (halfH > 0) { dist = halfH / tv + 0.3f; c = vec3(0, cy, 0); eye = vec3(0, cy + 0.05f, dist); }
  mat4 view = glm::lookAt(eye, c, vec3(0, 1, 0)), proj = glm::perspective(glm::radians(34.0f), (float)w / h, 0.1f, 40.0f);
  return app.R.renderModelSheet(md, boxes, view, proj, eye, w, h, px);
}

bool brInventoryPreviewPng(const fs::path& out, float yaw, float cy, float halfH) {   // --capture-br-models / -worn
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L || L->model < 0 || L->pose.skin.empty()) return false;
  const int w = halfH > 0 ? 420 : 360, h = halfH > 0 ? 420 : 560; std::vector<uint8_t> px;
  if (!renderPreview(*L, yaw, w, h, px, cy, halfH)) return false;
  std::vector<uint8_t> flip(px);   // renderModelSheet reads back top row first
  return stbi_write_png(out.string().c_str(), w, h, 4, flip.data(), w * 4) != 0;
}

void drawInventoryCharacter(ImVec2 mn, ImVec2 mx) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 40), 6);
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L || L->model < 0 || L->model >= (int)gGame->models.size() || L->pose.skin.empty()) return;
  static GLuint tex = 0; static int tw = 0, th = 0;
  static float yaw = 20; static double nextAt = 0;   // starts facing the viewer
  // turn: drag inside the frame, else a slow spin
  ImVec2 keep = ImGui::GetCursorScreenPos();   // the caller's layout cursor stays where it was
  ImGui::SetCursorScreenPos(mn); ImGui::InvisibleButton("##brchar", ImVec2(std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)));
  ImGui::SetCursorScreenPos(keep);
  if (ImGui::IsItemActive()) yaw += ImGui::GetIO().MouseDelta.x * 0.6f; else yaw += ImGui::GetIO().DeltaTime * 12.0f;
  int w = std::clamp((int)(mx.x - mn.x), 16, 768), h = std::clamp((int)(mx.y - mn.y), 16, 1200);
  double now = ImGui::GetTime();
  if (now >= nextAt || !tex || w != tw || h != th) {   // renderModelSheet makes an FBO per call: ~15 refreshes a second
    nextAt = now + 0.066;
    std::vector<uint8_t> px;
    if (renderPreview(*L, yaw, w, h, px)) {
      if (!tex || w != tw || h != th) {
        if (tex) glDeleteTextures(1, &tex);
        glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, w, h);
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        tw = w; th = h;
      }
      glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
  }
  if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, mn, ImVec2(mn.x + tw, mn.y + th));
}
