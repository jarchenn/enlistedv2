// GC1 GPU crowd, drawing (crowd.h). Bake: each type's procedural poses (poseCharacter, the held gun's sockets) sampled once
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

namespace {
std::map<int, CrowdType> gTypes;                 // by Game model index
std::vector<CrowdUnit> gUnits; std::vector<int> gUnitModel, gUnitGun;   // GC1: the test source fills these (GC2 moves them on the GPU)
GLuint gBuf = 0; size_t gCap = 0; double gLastT = -1;
}

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

void crowdReset() {
  for (auto& [k, t] : gTypes) crowdDestroy(t);
  gTypes.clear(); gUnits.clear(); gUnitModel.clear(); gUnitGun.clear(); gLastT = -1;
  if (gBuf) glDeleteBuffers(1, &gBuf); gBuf = 0; gCap = 0;
}

// GC1 test source (NMK_CROWD=n): two armies of n / 2 on a grid across the build area, marching in slow circles, the first of
// each faction's body list and its rifle (m1_garand / kar98k when present). GC2 replaces it with the real far units.
static void testSource(Game& g, float dt) {
  static const int n = std::getenv("NMK_CROWD") ? std::atoi(std::getenv("NMK_CROWD")) : 0;
  if (n <= 0) return;
  if (gUnits.empty()) {
    auto model = [&](const char* fac) { const json& l = g.combat.value("models", json::object()).value(fac, json::array()); if (l.empty()) return -1; auto it = g.modelIdx.find(l[0].get<std::string>()); return it == g.modelIdx.end() ? -1 : it->second; };
    auto gun = [&](const char* id) { for (int k = 0; k < (int)g.app.weaponDefs.size(); ++k) if (g.app.weaponDefs[k]->id == id) return k; return -1; };
    const int mA = model("Allies"), mX = model("Axis"), gA = gun("m1_garand"), gX = gun("kar98k");
    const float hx = g.app.terrain.buildHalfX * 0.9f, hz = g.app.terrain.buildHalfZ * 0.9f; const int side = (int)std::ceil(std::sqrt((double)n));
    for (int i = 0; i < n; ++i) {
      const float x = -hx + 2 * hx * ((i % side) + 0.5f) / side, z = -hz + 2 * hz * ((i / side) + 0.5f) / side; const bool ax = x > 0;
      CrowdUnit u; u.pos = vec4(x, g.app.terrain.height(x, z), z, 0); u.prev = u.pos; u.anim = vec4((float)(i % 7 == 0 ? CC_RUN : CC_WALK), (i * 0.137f), 0, 1);
      gUnits.push_back(u); gUnitModel.push_back(ax ? mX : mA); gUnitGun.push_back(ax ? gX : gA);
    }
    logf("crowd: test source, %d units", n);
  }
  for (size_t i = 0; i < gUnits.size(); ++i) {
    CrowdUnit& u = gUnits[i]; u.prev = u.pos;
    const float sp = u.anim.x == CC_RUN ? 4.0f : 1.4f, r = 25.0f + (i % 13) * 3.0f, w = sp / r;   // a circle of radius r at speed sp
    u.pos.w += w * dt; const vec3 d(std::sin(u.pos.w), 0, std::cos(u.pos.w));   // yaw: the soldier faces +z at 0 (world = T * R(yaw + pi))
    u.pos.x -= d.x * sp * dt; u.pos.z -= d.z * sp * dt; u.pos.y = g.app.terrain.height(u.pos.x, u.pos.z);
    u.anim.y += dt;
  }
}

void crowdFrame(Game& g) {
  Renderer& R = g.app.R; R.crowdDraws.clear();
  if (!R.perf.crowd) return;
  const float dt = gLastT < 0 ? 0.0f : (float)std::clamp(g.t - gLastT, 0.0, 0.1); gLastT = g.t;
  testSource(g, dt);
  if (gUnits.empty()) return;
  // types on first use
  std::vector<int> need; for (size_t i = 0; i < gUnits.size(); ++i) { const int k = gUnitModel[i] * 4096 + std::max(-1, gUnitGun[i]) + 1; if (!gTypes.count(k)) need.push_back(k); }
  for (int k : need) if (!gTypes.count(k)) {
    const int mi = k / 4096, gi = k % 4096 - 1; std::string err; CrowdType& t = gTypes[k];
    if (mi < 0 || mi >= (int)g.models.size() || !crowdBake(t, *g.models[mi], gi >= 0 ? g.app.weaponDefs[gi].get() : nullptr, err)) logf("crowd: type %d: %s", mi, err.c_str());
  }
  // cull and bucket by type x LOD (projected height at the display size, the hip FOV)
  const Frustum fr = Frustum::fromVP(R.lastViewProj()); const vec3 eye = g.app.eyePos();
  const float H = (float)std::max(1, R.displayH), k = H * 1.8f / (2.0f * std::tan(glm::radians(settings().fov()) * 0.5f));
  std::map<std::pair<int, int>, std::vector<int>> bucket;
  for (size_t i = 0; i < gUnits.size(); ++i) {
    const CrowdUnit& u = gUnits[i]; const vec3 c = vec3(u.pos) + vec3(0, 0.9f, 0);
    if (!fr.sphere(c, 1.3f)) continue;
    const float px = k * u.anim.w / std::max(0.5f, glm::length(c - eye));
    if (px < 1.5f) continue;
    bucket[{gUnitModel[i] * 4096 + std::max(-1, gUnitGun[i]) + 1, px > 40.0f ? 0 : 1}].push_back((int)i);
  }
  std::vector<CrowdUnit> up; up.reserve(gUnits.size());
  for (auto& [key, ids] : bucket) {
    auto it = gTypes.find(key.first); if (it == gTypes.end() || !it->second.pal) continue;
    const CrowdType& t = it->second; const CrowdType::Lod& l = t.lod[key.second];
    CrowdDraw d; d.look = &t.m->gpu; d.parts = &l.parts; d.vao = l.vao; d.rest = l.rest; d.pal = t.pal; d.J = t.J; d.base = (int)up.size(); d.count = (int)ids.size(); d.fps = t.fps;
    for (int c = 0; c < 8; ++c) d.clips[c] = t.clip[c];
    for (int i : ids) up.push_back(gUnits[i]);
    R.crowdDraws.push_back(d);
    if (t.gun && t.gun->vao) { CrowdDraw gd = d; gd.look = t.gun; gd.parts = &t.gun->parts; gd.vao = t.gun->vao; gd.rest = 0; gd.gun = true; R.crowdDraws.push_back(gd); }
  }
  if (up.empty()) { R.crowdDraws.clear(); return; }
  const size_t bytes = up.size() * sizeof(CrowdUnit);
  if (!gBuf || bytes > gCap) { if (gBuf) glDeleteBuffers(1, &gBuf); glCreateBuffers(1, &gBuf); gCap = std::max(bytes, (size_t)1 << 20) * 3 / 2; glNamedBufferData(gBuf, gCap, nullptr, GL_STREAM_DRAW); }
  glInvalidateBufferData(gBuf); glNamedBufferSubData(gBuf, 0, bytes, up.data());
  for (auto& d : R.crowdDraws) d.units = gBuf;
}
