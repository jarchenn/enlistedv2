// Decor already saved in maps (the old V8 "Beautify" tool, removed in UI2: its dialog, generator and settings are gone).
// Placements tagged extra.beautify (open-ground scatter) and extra.decor (rows of buildings, hedges and trees beyond the
// world border) keep loading and drawing as before: decor may stand outside the build area (decorMayLeaveBorder), is
// never furnished and is drawn cheaply at range (decorDraw).
#include "terrain_tools.h"

bool isDecor(const Instance& i) { return i.extra.is_object() && i.extra.contains("decor") && i.extra["decor"].is_boolean() && i.extra["decor"].get<bool>(); }
static bool isBeautify(const Instance& i) { return i.extra.is_object() && i.extra.contains("beautify"); }

bool decorMayLeaveBorder(const App& app, uint64_t id, vec3 w) {
  if (!id) return false;
  const Instance* i = nullptr;
  for (auto& x : app.doc.instances) if (x.id == id) { i = &x; break; }
  if (!i || !isDecor(*i)) return false;
  const Terrain& T = app.terrain;
  return w.x > T.org.x + 2 && w.z > T.org.y + 2 && w.x < T.org.x + T.size - 2 && w.z < T.org.y + T.sizeZ - 2;
}

bool decorDraw(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  if (!r.asset) return false;
  const bool decor = isDecor(i);
  if (!decor && !isBeautify(i)) return false;
  const float cull = 950.0f, nearD = 140.0f, glassD = 320.0f, treeCull = 480.0f, smallCull = 300.0f, lowH = 1.3f, lowCull = 170.0f;   // m
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  float dist = glm::length(i.pos - eye);
  if (decor && dist > cull) return true;   // beyond the horizon band: not drawn at all
  const AssetDef* a = r.asset;
  // the scene draws every opaque item in the main and all shadow passes without frustum culling; decor beyond the
  // near band is skipped when its bounds (+30 m, so off-screen shadow casters near the view stay) are outside the view
  if (dist > nearD) {
    mat4 vp = app.projM * app.viewM;
    vec3 c = vec3(M * vec4((a->bmin + a->bmax) * 0.5f, 1.0f));
    float rad = glm::length(a->bmax - a->bmin) * 0.5f + 30.0f;
    for (int k = 0; k < 4; ++k) {   // left, right, bottom, top planes (Gribb-Hartmann)
      int row = k / 2; float sgn = (k % 2) ? -1.0f : 1.0f;
      vec4 pl(vp[0][3] + sgn * vp[0][row], vp[1][3] + sgn * vp[1][row], vp[2][3] + sgn * vp[2][row], vp[3][3] + sgn * vp[3][row]);
      float len = glm::length(vec3(pl));
      if (len > 1e-6f && (glm::dot(vec3(pl), c) + pl.w) / len < -rad) return true;
    }
  }
  if (!decor) return a->bmax.y - a->bmin.y < lowH && dist > lowCull;   // scatter inside the border: low dressing (ground cover, stones, rubble, small shrubs) is culled at range
  if (!a->enterable) {   // ring dressing: trees, hedges, rocks, patches. Culled well inside their usual distances
    float c = a->tree.on ? treeCull : a->category == "Vegetation" && a->footprint.x >= 6.0f ? treeCull : smallCull;
    return dist > c;     // otherwise the ordinary path
  }
  // near, damaged, furnished, modelled or non-building decor: the ordinary path
  if (dist < nearD || r.damaged || a->model || a->nature.kind != NatureDef::NONE || a->tree.on || a->path.on || app.interior.hideBuiltin(i)) return false;
  op.push_back({&app.assetLodMesh(r, a, a->opaque, dist), M});   // PF2 asset LOD
  op.push_back({&a->panelsIntact, M});
  if (dist < glassD && a->glass.count) gl.push_back({&a->glass, M});
  return true;   // doors and shutters (separate leaf meshes) are skipped at range: 1-3 draws per building
}

