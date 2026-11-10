// Trees: asset build (stump / trunk split, leaf cards), runtime damage state machine and felling.
//   HP_bullet = 30 small-arms hits, HP_AT = 1 (AT / HE strike within 1.5 m of the trunk, or a direct hit).
//   When either reaches 0 the static tree is replaced by a static stump plus a dynamic fallen-trunk body
//   (canopy rides on the trunk), with an impulse pushing away from the hit.
// Leaf sway (vertex shader, render.cpp):  V_offset = D_wind * M * A * sin(f t + k_x X_world + k_z Z_world)
#include "app.h"
#include "snap.h"
#include <algorithm>
#include <cfloat>
#include <random>
#include <map>
#include <cstring>
#include <stb_image.h>

// ---------------------------------------------------------------- L1 leaf library (docs/leaves.md)
// content/textures/leaves (tools/gen_leaf_textures.mjs): an atlas of 288 px layers (species leaf variants + sprig
// cards) -> one GL_TEXTURE_2D_ARRAY on unit kLeafUnit, mips built here with the alpha scaled per level so the
// alpha-tested coverage of level 0 is kept (distant canopies do not thin out). Sprig meshes: one shared VBO / IBO.
std::vector<LeafMeshRange> gLeafSprigMesh;
static GLuint gLeafTex = 0, gLeafVbo = 0, gLeafIbo = 0;
static bool gLeafTried = false;
struct LeafSpecies { vec3 base{1}; std::vector<int> sprigs; };
static std::map<std::string, LeafSpecies> gLeafSpecies;
static json gLeafAssets;
bool leafLibReady() { return gLeafTex != 0; }

static void leafMips(GLuint tex, int layer, int S, int levels, std::vector<uint8_t> px) {
  int cov0 = 0; for (int k = 0; k < S * S; ++k) cov0 += px[k * 4 + 3] > 127;
  std::vector<float> raw(S * S); for (int k = 0; k < S * S; ++k) raw[k] = px[k * 4 + 3] / 255.0f;
  glTextureSubImage3D(tex, 0, 0, 0, layer, S, S, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  float frac0 = (float)cov0 / (S * S);
  for (int L = 1, s = S; L < levels; ++L) {
    int s2 = std::max(1, s / 2);
    std::vector<uint8_t> q(s2 * s2 * 4); std::vector<float> r2(s2 * s2);
    for (int j = 0; j < s2; ++j) for (int i = 0; i < s2; ++i) {
      float c[4] = {0, 0, 0, 0};
      for (int d = 0; d < 4; ++d) { int ii = std::min(s - 1, i * 2 + (d & 1)), jj = std::min(s - 1, j * 2 + (d >> 1)), k = jj * s + ii;
        for (int n = 0; n < 3; ++n) c[n] += px[k * 4 + n]; c[3] += raw[k]; }
      for (int n = 0; n < 3; ++n) q[(j * s2 + i) * 4 + n] = (uint8_t)(c[n] * 0.25f + 0.5f);
      r2[j * s2 + i] = c[3] * 0.25f;
    }
    float lo = 1, hi = 16;   // alpha scale that keeps level 0's share of texels over the 0.5 cutoff
    for (int it = 0; it < 14; ++it) { float m = (lo + hi) * 0.5f; int n = 0; for (float a : r2) n += a * m > 0.5f; if ((float)n / (s2 * s2) < frac0) lo = m; else hi = m; }
    float k = (lo + hi) * 0.5f;
    for (int n = 0; n < s2 * s2; ++n) q[n * 4 + 3] = (uint8_t)std::min(255.0f, r2[n] * k * 255.0f + 0.5f);
    glTextureSubImage3D(tex, L, 0, 0, layer, s2, s2, 1, GL_RGBA, GL_UNSIGNED_BYTE, q.data());
    px.swap(q); raw.swap(r2); s = s2;
  }
}

static bool loadLeafLib() {
  if (gLeafTried) return gLeafTex != 0;
  gLeafTried = true;
  fs::path dir = contentDir() / "textures" / "leaves";
  json j;
  try { j = json::parse(readTextFile(dir / "leaves.json")); } catch (...) { logf("leaves: %s missing or invalid: procedural oval leaves", (dir / "leaves.json").string().c_str()); return false; }
  int S = j["layerSize"], gut = j["gutter"], cols = j["cols"], n = j["layers"];
  int w, h, c;
  stbi_uc* d = stbi_load((dir / j["atlas"].get<std::string>()).string().c_str(), &w, &h, &c, 4);
  if (!d || w < cols * S || h < (n + cols - 1) / cols * S) { if (d) stbi_image_free(d); logf("leaves: atlas missing or too small: procedural oval leaves"); return false; }
  int levels = 1; while ((S >> levels) > 0) ++levels;
  glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &gLeafTex);
  glTextureStorage3D(gLeafTex, levels, GL_SRGB8_ALPHA8, S, S, n);   // shortcut: RGBA8 (~20 MB); BC3 if leaf memory matters
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  std::vector<uint8_t> px(S * S * 4);
  for (int l = 0; l < n; ++l) {
    int cx = l % cols * S, cy = l / cols * S;
    for (int r = 0; r < S; ++r) memcpy(&px[r * S * 4], d + ((size_t)(cy + S - 1 - r) * w + cx) * 4, S * 4);   // file rows top-down; texel row 0 = leaf base
    leafMips(gLeafTex, l, S, levels, px);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  stbi_image_free(d);
  glTextureParameteri(gLeafTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(gLeafTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(gLeafTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gLeafTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameterf(gLeafTex, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 4.0f);
  glBindTextureUnit(kLeafUnit, gLeafTex);
  // sprig meshes: card space x, y in [-1, 1] (the card's T, B half extents), z along the card normal (same units)
  std::vector<float> V; std::vector<uint32_t> I;
  const json& hulls = j["hulls"];
  float g = (float)gut / S;
  auto cardOf = [&](const json& lf, float u, float v) {   // leaf layer uv -> card xy (gen_leaf_textures.mjs cardOf)
    float len = lf["len"], a = lf["ang"], lx = (u - 0.5f) * len, ly = (v - g) * len, s = std::sin(a), co = std::cos(a);
    return vec2(lf["x"].get<float>() + lx * co + ly * s, lf["y"].get<float>() - lx * s + ly * co) * 2.0f - 1.0f;
  };
  int base = 0;
  auto put = [&](vec3 p, vec3 nrm, vec2 uv, float layer) { V.insert(V.end(), {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, uv.x, uv.y, layer}); return (uint32_t)(V.size() / 9 - 1 - base); };
  for (auto& [id, sj] : j["species"].items()) {
    LeafSpecies sp; sp.base = vec3(sj["base"][0], sj["base"][1], sj["base"][2]);
    for (auto& sg : sj["sprigs"]) {
      sp.sprigs.push_back((int)gLeafSprigMesh.size() / 3);
      float sl = sg["layer"];
      for (int lod = 0; lod < 3; ++lod) {
        LeafMeshRange r; base = r.baseVertex = (int)V.size() / 9; r.firstIndex = (int)I.size();
        if (lod == 2) {   // the card itself
          uint32_t q[4] = {put({-1, -1, 0}, {0, 0, 1}, {0, 0}, sl), put({1, -1, 0}, {0, 0, 1}, {1, 0}, sl), put({1, 1, 0}, {0, 0, 1}, {1, 1}, sl), put({-1, 1, 0}, {0, 0, 1}, {0, 1}, sl)};
          I.insert(I.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
        } else {
          // twig: a strip over the sprig layer's own twig texels
          const json& t = sg["twig"]; vec2 a(t[0], t[1]), b(t[2], t[3]); float hw = t[4].get<float>() * 1.6f + 1.0f / S;
          vec2 dd = glm::normalize(b - a) * hw, pp(-dd.y, dd.x); vec2 cs[4] = {a - dd - pp, a - dd + pp, b + dd + pp, b + dd - pp};
          uint32_t q[4]; for (int k = 0; k < 4; ++k) q[k] = put(vec3(cs[k] * 2.0f - 1.0f, 0), {0, 0, 1}, cs[k], sl);
          I.insert(I.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
          int order = 0;
          for (auto& lf : sg["leaves"]) {
            const json& h = hulls[lf["layer"].get<int>()]; float ll = lf["layer"], Ls = lf["len"].get<float>() * 2.0f, zo = 0.006f * ++order, vt = h["vt"];
            if (lod == 1) {   // diamond: base, left, tip, right
              const json& m = h["mid"]; float vb = m[0], vm = m[1], l = m[2], r2 = m[3];
              vec2 uv[4] = {{0.5f, vb}, {0.5f - l, vm}, {0.5f, vt}, {0.5f + r2, vm}};
              uint32_t q2[4]; for (int k = 0; k < 4; ++k) q2[k] = put(vec3(cardOf(lf, uv[k].x, uv[k].y), zo), {0, 0, 1}, uv[k], ll);
              I.insert(I.end(), {q2[0], q2[1], q2[2], q2[0], q2[2], q2[3]});
              continue;
            }
            // near: hexagon fanned from a midrib vertex M, folded: the midrib rides above the wings, the tip arches down
            const json& nh = h["near"]; float vb = nh[0], v1 = nh[1], v2 = nh[2], l1 = nh[3], l2 = nh[4], r1 = nh[5], r2 = nh[6];
            vec2 uv[7] = {{0.5f, vb}, {0.5f, (v1 + v2) * 0.5f}, {0.5f, vt}, {0.5f - l1, v1}, {0.5f - l2, v2}, {0.5f + r1, v1}, {0.5f + r2, v2}};
            float z[7] = {0, 0.06f, -0.04f, -0.02f, -0.03f, -0.02f, -0.03f};
            vec3 p[7]; for (int k = 0; k < 7; ++k) p[k] = vec3(cardOf(lf, uv[k].x, uv[k].y), zo + z[k] * Ls);
            const int tri[18] = {0, 3, 1, 1, 3, 4, 1, 4, 2, 0, 1, 5, 1, 6, 5, 1, 2, 6};
            vec3 nn[7] = {};
            for (int k = 0; k < 18; k += 3) { vec3 f = glm::cross(p[tri[k + 1]] - p[tri[k]], p[tri[k + 2]] - p[tri[k]]); if (f.z < 0) f = -f; for (int e = 0; e < 3; ++e) nn[tri[k + e]] += f; }
            uint32_t q7[7]; for (int k = 0; k < 7; ++k) q7[k] = put(p[k], glm::normalize(nn[k] + vec3(0, 0, 1e-6f)), uv[k], ll);
            for (int k : tri) I.push_back(q7[k]);
          }
        }
        r.count = (int)I.size() - r.firstIndex;
        gLeafSprigMesh.push_back(r);
      }
    }
    gLeafSpecies[id] = sp;
  }
  gLeafAssets = j["assets"];
  glCreateBuffers(1, &gLeafVbo); glNamedBufferStorage(gLeafVbo, V.size() * 4, V.data(), 0);
  glCreateBuffers(1, &gLeafIbo); glNamedBufferStorage(gLeafIbo, I.size() * 4, I.data(), 0);
  logf("leaves: %d layers (%dx%d, %d mips), %zu species, %zu sprig meshes (%zu verts)", n, S, S, levels, gLeafSpecies.size(), gLeafSprigMesh.size() / 3, V.size() / 9);
  return true;
}

// the leaf species of an asset (leaves.json "assets": exact name or "prefix*"); empty: procedural ovals
static std::vector<const LeafSpecies*> leafSpeciesFor(const std::string& asset) {
  std::vector<const LeafSpecies*> out;
  for (auto& [k, v] : gLeafAssets.items()) {
    bool hit = !k.empty() && k.back() == '*' ? asset.compare(0, k.size() - 1, k, 0, k.size() - 1) == 0 : asset == k;
    if (!hit) continue;
    for (auto& s : v) { auto it = gLeafSpecies.find(s.get<std::string>()); if (it != gLeafSpecies.end()) out.push_back(&it->second); }
    break;
  }
  return out;
}
static uint32_t leafHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

void buildTreeAsset(AssetDef& a, const json& tj) {
  TreeDef& t = a.tree;
  t.on = true;
  t.hpBullet = tj.value("hpBullet", 30.0f); t.hpAT = tj.value("hpAT", 1.0f);
  t.stumpH = tj.value("stumpHeight", 0.4f); t.trunkR = tj.value("trunkRadius", 0.25f);
  t.mass = tj.value("mass", 300.0f); t.leafSize = tj.value("leafSize", 0.35f);
  t.leafCol = jvec3(tj.value("leafColor", json()), vec3(0.2f, 0.3f, 0.1f));
  for (auto& c : tj.value("canopy", json::array())) t.canopy.push_back(vec4(c[0], c[1], c[2], c[3]));
  // ---- split the trunk at the stump cut
  MeshData sm, tm; std::vector<Part> sc, tc;
  for (auto& p : a.parts) {
    float lo = p.center.y - p.size.y * 0.5f, hi = p.center.y + p.size.y * 0.5f;
    bool upright = std::fabs(glm::dot(p.rot * vec3(0, 1, 0), vec3(0, 1, 0))) > 0.999f;
    auto put = [&](Part q, bool stump) {
      if (!stump) q.center.y -= t.stumpH;
      appendPart(stump ? sm : tm, q, a.materials[q.mat], mat4(1));
      if (q.collide) (stump ? sc : tc).push_back(q);
    };
    if (hi <= t.stumpH + 1e-4f) put(p, true);
    else if (lo >= t.stumpH - 1e-4f || !upright || p.shape != Part::BOX) put(p, false);
    else {
      Part s = p; s.size.y = t.stumpH - lo; s.center.y = lo + s.size.y * 0.5f; put(s, true);
      Part u = p; u.size.y = hi - t.stumpH; u.center.y = t.stumpH + u.size.y * 0.5f; put(u, false);
    }
  }
  // the cut face: a pale disc of heartwood on the stump
  Material cutM{"cut", vec3(0.55f, 0.43f, 0.28f), 0.9f, 0.0f, PAT_WOOD, 0};
  Part cut; cut.center = vec3(0, t.stumpH - 0.005f, 0); cut.size = vec3(t.trunkR * 1.9f, 0.02f, t.trunkR * 1.9f); cut.collide = false;
  appendPart(sm, cut, cutM, mat4(1));
  t.stumpMesh.upload(sm); t.trunkMesh.upload(tm);
  t.stumpShape = Physics::buildShape(sc); t.trunkShape = Physics::buildShape(tc);
  // ---- leaf cards: deterministic per asset
  std::mt19937 rng(tj.value("seed", 1u));
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  int per = tj.value("cardsPerSphere", 60);
  float dens = tj.value("cardDensity", 0.0f);   // v2: cards per m^2 of cluster surface (clusters of different size stay equally dense)
  t.hedge = tj.value("hedge", false);
  float maxH = 0.1f; for (auto& c : t.canopy) maxH = std::max(maxH, glm::length(vec2(c.x, c.z)) + c.w);
  std::vector<float> v;
  t.top = t.stumpH;
  // L1: the same cards as instances of a species sprig (own hash, not the rng: card placement stays as it was)
  std::vector<const LeafSpecies*> species = loadLeafLib() ? leafSpeciesFor(a.id) : std::vector<const LeafSpecies*>{};
  std::vector<std::vector<float>> inst(gLeafSprigMesh.size() / 3);
  std::vector<vec3> sprigTint(inst.size(), vec3(1));
  uint32_t cardNo = 0, sphereNo = 0;
  // hedge banks: a skirt of leaf clusters over both bank faces, below the hedge on top, so the bank reads as overgrown
  // instead of a bare earth slab (the clusters are card sources only; t.canopy, which sight and bullets use, is unchanged)
  std::vector<vec4> spheres = t.canopy;
  if (t.hedge && !t.canopy.empty()) {
    float bankTop = 1e9f, x0 = 1e9f, x1 = -1e9f;
    for (auto& c : t.canopy) { bankTop = std::min(bankTop, c.y - c.w); x0 = std::min(x0, c.x); x1 = std::max(x1, c.x); }
    if (bankTop > 0.9f) {   // canopies that already reach the ground (older hedges) need none
      float r = std::max(0.45f, bankTop * 0.38f), z = a.footprint.y * 0.5f * 0.62f;
      std::mt19937 srng(tj.value("seed", 1u) + 7919u);   // own stream: the canopy's cards stay as they were
      for (float x = x0; x <= x1 + 1e-3f; x += 0.9f)
        for (float side : {-1.0f, 1.0f}) spheres.push_back(vec4(x + (U(srng) - 0.5f) * 0.4f, bankTop * 0.5f, side * z, r));
    }
  }
  for (auto& c : spheres) {
    t.top = std::max(t.top, c.y + c.w);
    const LeafSpecies* sp = species.empty() ? nullptr : species[leafHash(++sphereNo * 2654435761u + 17u) % species.size()];   // hedges: one species per cluster
    int cards = dens > 0 ? std::max(12, (int)(dens * c.w * c.w * 3.0f)) : per;
    for (int n = 0; n < cards; ++n) {
      float z = U(rng) * 2 - 1, ph = U(rng) * 6.2831853f, rr = std::sqrt(1 - z * z);
      vec3 dir(rr * std::cos(ph), z, rr * std::sin(ph));
      vec3 ctr = vec3(c) + dir * c.w * (0.45f + 0.55f * std::cbrt(U(rng)));
      vec3 nrm = glm::normalize(dir + vec3(U(rng) - 0.5f, U(rng) - 0.5f, U(rng) - 0.5f) * 1.2f);
      vec3 T = glm::normalize(glm::cross(std::fabs(nrm.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0), nrm)), B = glm::cross(nrm, T);
      float s = t.leafSize * (0.7f + 0.6f * U(rng)) * c.w * 0.9f;
      float mask = std::clamp(glm::length(vec2(ctr.x, ctr.z)) / maxH * 0.8f + 0.2f, 0.0f, 1.0f);   // 0 at the trunk axis, 1 at the outer tips
      // canopy self-shading: cards deep inside a cluster and on the lower crown are darker
      float rad = glm::length(ctr - vec3(c)) / c.w;
      float shadeV = std::clamp(0.45f + 0.45f * rad + 0.15f * (ctr.y - t.stumpH) / std::max(1.0f, t.top), 0.35f, 1.0f);
      vec3 q[4] = {ctr - T * s - B * s, ctr + T * s - B * s, ctr + T * s + B * s, ctr - T * s + B * s};
      vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      int idx[6] = {0, 1, 2, 0, 2, 3};
      for (int k : idx) { v.insert(v.end(), {q[k].x, q[k].y, q[k].z, dir.x, dir.y, dir.z, uv[k].x, uv[k].y, mask, shadeV}); }
      if (sp) {
        uint32_t hsh = leafHash(++cardNo * 747796405u + 2891336453u);
        int sg = sp->sprigs[hsh % sp->sprigs.size()];
        vec3 Ts = T * s, Bs = B * s;
        inst[sg].insert(inst[sg].end(), {ctr.x, ctr.y, ctr.z, Ts.x, Ts.y, Ts.z, Bs.x, Bs.y, Bs.z, dir.x, dir.y, dir.z, mask, shadeV, (float)(hsh >> 8 & 1023), 0.0f});
        sprigTint[sg] = glm::clamp(t.leafCol / glm::max(sp->base, vec3(1e-3f)), vec3(0.2f), vec3(4.0f));
      }
    }
  }
  const size_t sphereEnd = v.size();   // R2 shadow LOD: the cluster cards end here (styled cards follow)
  // ---- styled cards (docs/nature.md): fern / bracken fronds and nettle stems with paired leaves. The card's uv.x is
  // offset by 2 (frond), 4 (nettle leaf) or 6 (plain stem strip) so leafAlpha (render.cpp) draws that silhouette.
  auto quad = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, float u0, float v0, float v1, float m0, float m1, float shade) {
    vec3 q[4] = {a, b, c, d}; vec2 uv[4] = {{u0, v0}, {u0 + 1, v0}, {u0 + 1, v1}, {u0, v1}}; float mk[4] = {m0, m0, m1, m1};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k : idx) v.insert(v.end(), {q[k].x, q[k].y, q[k].z, n.x, n.y, n.z, uv[k].x, uv[k].y, mk[k], shade});
  };
  if (tj.contains("fronds")) {   // radiating, arching fronds from a crown at the base
    auto& fj = tj["fronds"];
    int n = fj.value("count", 12); float L0 = fj["len"][0], L1 = fj["len"][1], W = fj.value("width", 0.22f), e0 = fj["elev"][0], e1 = fj["elev"][1];
    float droop = fj.value("droop", 0.5f), rad = fj.value("radius", 0.1f), stalk = fj.value("stalk", 0.0f);
    for (int k = 0; k < n; ++k) {
      float az = k * 2.39996f + (U(rng) - 0.5f) * 0.5f, el = radians(e0 + (e1 - e0) * U(rng)), L = L0 + (L1 - L0) * U(rng);
      vec3 hd(std::cos(az), 0, std::sin(az)), sd(-hd.z, 0, hd.x);
      vec3 base = hd * rad * U(rng) + vec3(0, 0.02f, 0);
      if (stalk > 0) {   // bracken: a bare upright stalk, the frond spreads from its top
        vec3 top = base + vec3(0, stalk * (0.8f + 0.4f * U(rng)), 0) + hd * 0.06f;
        quad(base - sd * 0.008f, base + sd * 0.008f, top + sd * 0.008f, top - sd * 0.008f, glm::normalize(glm::cross(sd, top - base)), 6.0f, 0, 1, 0.0f, 0.2f, 0.7f);
        base = top;
      }
      const int S = 4;
      for (int s = 0; s < S; ++s) {
        float a0 = (float)s / S, a1 = (float)(s + 1) / S;
        auto P = [&](float a) { return base + hd * std::cos(el) * a * L + vec3(0, std::sin(el) * a * L - droop * a * a * L, 0); };
        vec3 p0 = P(a0), p1 = P(a1), tg = glm::normalize(p1 - p0), nn = glm::normalize(glm::cross(sd, tg));
        if (nn.y < 0) nn = -nn;
        float w = W * 0.5f;
        quad(p0 - sd * w, p0 + sd * w, p1 + sd * w, p1 - sd * w, nn, 2.0f, a0, a1, 0.15f + 0.85f * a0, 0.15f + 0.85f * a1, 0.75f + 0.25f * a1);
        t.top = std::max(t.top, p1.y);
      }
    }
  }
  if (tj.contains("nettles")) {   // upright stems with opposite, decussate drooping leaves
    auto& nj = tj["nettles"];
    int n = nj.value("stems", 10); float H0 = nj["height"][0], H1 = nj["height"][1], rad = nj.value("radius", 0.35f), lw = nj.value("leaf", 0.09f);
    for (int k = 0; k < n; ++k) {
      float a = U(rng) * 6.2831853f, r = rad * std::sqrt(U(rng)), H = H0 + (H1 - H0) * U(rng);
      vec3 base(std::cos(a) * r, 0, std::sin(a) * r), lean(U(rng) - 0.5f, 0, U(rng) - 0.5f);
      vec3 top = base + vec3(0, H, 0) + lean * 0.15f * H;
      vec3 ax = glm::normalize(top - base), sd = glm::normalize(glm::cross(ax, vec3(0.3f, 0, 1)));
      quad(base - sd * 0.007f, base + sd * 0.007f, top + sd * 0.007f, top - sd * 0.007f, glm::normalize(glm::cross(sd, ax)), 6.0f, 0, 1, 0.0f, 1.0f, 0.8f);
      int nodes = std::max(3, (int)(H / 0.09f));
      for (int nd = 1; nd <= nodes; ++nd) {
        float f = (float)nd / (nodes + 1); vec3 at = glm::mix(base, top, f);
        float ls = lw * (1.15f - 0.6f * f) * (0.8f + 0.4f * U(rng));
        float rot = nd * 1.5708f + a;
        for (int pr = 0; pr < 2; ++pr) {
          vec3 out(std::cos(rot + pr * 3.14159f), 0, std::sin(rot + pr * 3.14159f)), sdl(-out.z, 0, out.x);
          vec3 tip = at + out * ls * 1.6f + vec3(0, -ls * (0.3f + 0.4f * U(rng)), 0);
          vec3 tg = glm::normalize(tip - at), nn = glm::normalize(glm::cross(sdl, tg)); if (nn.y < 0) nn = -nn;
          float m = std::clamp(0.3f + f * 0.7f, 0.0f, 1.0f);
          quad(at - sdl * ls * 0.5f, at + sdl * ls * 0.5f, tip + sdl * ls * 0.5f, tip - sdl * ls * 0.5f, nn, 4.0f, 0, 1, m * 0.8f, m, 0.65f + 0.35f * f);
        }
      }
      t.top = std::max(t.top, top.y);
    }
  }
  t.cardVerts = (int)v.size() / 10;
  // R2 shadow card LOD, appended after the full set (render.cpp drawFoliage, Perf::leafShadowLod): every 2nd cluster card,
  // grown 1.25x about its centre (same uv, so the depth pass cuts the same leaf alpha) - half the cards, ~80 % of the leaf
  // area, so the canopy still casts dappled light rather than a blob; styled cards (fronds, nettles) are kept as they are
  {
    const size_t q = 60, nq = sphereEnd / q, full = v.size();
    if (nq >= 8) {
      t.shadowFirst = t.cardVerts;
      for (size_t c = 0; c < nq; c += 2) {
        vec3 m(0); for (int k = 0; k < 6; ++k) m += vec3(v[c * q + k * 10], v[c * q + k * 10 + 1], v[c * q + k * 10 + 2]) / 6.0f;
        for (int k = 0; k < 6; ++k) {
          size_t o = c * q + k * 10; float w[10]; std::copy(v.begin() + o, v.begin() + o + 10, w);
          vec3 p = m + (vec3(w[0], w[1], w[2]) - m) * 1.25f; w[0] = p.x; w[1] = p.y; w[2] = p.z;
          v.insert(v.end(), w, w + 10);
        }
      }
      std::vector<float> styled(v.begin() + sphereEnd, v.begin() + full);
      v.insert(v.end(), styled.begin(), styled.end());
      t.shadowVerts = (int)v.size() / 10 - t.shadowFirst;
    }
  }
  t.cardMin = vec3(FLT_MAX); t.cardMax = vec3(-FLT_MAX);   // culling bounds (render.cpp drawFoliage): the full set and the shadow LOD
  for (size_t k = 0; k + 2 < v.size(); k += 10) { vec3 p(v[k], v[k + 1], v[k + 2]); t.cardMin = glm::min(t.cardMin, p); t.cardMax = glm::max(t.cardMax, p); }
  if (t.cardMin.x > t.cardMax.x) t.cardMin = t.cardMax = vec3(0);
  if (!species.empty()) {   // L1 instanced leaves: one VAO over the shared sprig meshes (binding 0) and this tree's cards (binding 1)
    std::vector<float> all;
    for (size_t sg = 0; sg < inst.size(); ++sg) if (!inst[sg].empty()) {
      t.leafGroups.push_back({(int)sg, (int)(all.size() / 16), (int)(inst[sg].size() / 16), sprigTint[sg]});
      all.insert(all.end(), inst[sg].begin(), inst[sg].end());
    }
    // R2 shadow LOD of the instances (Perf::leafShadowLod): every 2nd sprig of each group with its axes grown 1.25x,
    // appended to the same buffer - the same sprig card and alpha, half the instances
    const size_t full = all.size();
    for (auto g : std::vector<TreeDef::LeafGroup>(t.leafGroups)) {
      if (g.count < 8) { t.leafShadowGroups.push_back(g); continue; }
      TreeDef::LeafGroup s = g; s.first = (int)(all.size() / 16);
      for (int k = 0; k < g.count; k += 2) {
        float w[16]; std::copy(all.begin() + (size_t)(g.first + k) * 16, all.begin() + (size_t)(g.first + k + 1) * 16, w);
        for (int c = 3; c < 9; ++c) w[c] *= 1.25f;
        all.insert(all.end(), w, w + 16);
      }
      s.count = (int)(all.size() / 16) - s.first;
      t.leafShadowGroups.push_back(s);
    }
    t.leafCards = (int)full / 16;   // the full set (the shadow LOD follows it in the buffer)
    if (t.leafCards) {
      glCreateBuffers(1, &t.leafInst); glNamedBufferStorage(t.leafInst, all.size() * 4, all.data(), 0);
      glCreateVertexArrays(1, &t.leafVao);
      glVertexArrayVertexBuffer(t.leafVao, 0, gLeafVbo, 0, 36); glVertexArrayVertexBuffer(t.leafVao, 1, t.leafInst, 0, 64);
      glVertexArrayBindingDivisor(t.leafVao, 1, 1); glVertexArrayElementBuffer(t.leafVao, gLeafIbo);
      const int mc[4] = {3, 3, 2, 1}, mo[4] = {0, 3, 6, 8}, io[5] = {0, 3, 6, 9, 12};
      for (int k = 0; k < 4; ++k) { glEnableVertexArrayAttrib(t.leafVao, k); glVertexArrayAttribFormat(t.leafVao, k, mc[k], GL_FLOAT, GL_FALSE, mo[k] * 4); glVertexArrayAttribBinding(t.leafVao, k, 0); }
      for (int k = 0; k < 5; ++k) { glEnableVertexArrayAttrib(t.leafVao, 4 + k); glVertexArrayAttribFormat(t.leafVao, 4 + k, 3, GL_FLOAT, GL_FALSE, io[k] * 4); glVertexArrayAttribBinding(t.leafVao, 4 + k, 1); }
    }
  }
  glCreateVertexArrays(1, &t.cardVao); glCreateBuffers(1, &t.cardVbo);
  glNamedBufferStorage(t.cardVbo, v.size() * 4, v.data(), 0);
  glVertexArrayVertexBuffer(t.cardVao, 0, t.cardVbo, 0, 40);
  int comps[5] = {3, 3, 2, 1, 1}, off = 0;
  for (int k = 0; k < 5; ++k) { glEnableVertexArrayAttrib(t.cardVao, k); glVertexArrayAttribFormat(t.cardVao, k, comps[k], GL_FLOAT, GL_FALSE, off * 4); glVertexArrayAttribBinding(t.cardVao, k, 0); off += comps[k]; }
}

// ---------------------------------------------------------------- runtime
static constexpr uint64_t kTrunkUD = 0xFFF1u;

void App::buildTreeRT(const Instance& i, InstanceRT& r) {
  const TreeDef& t = r.asset->tree;
  r.treeHits = 0; r.treeFelled = false; r.trunkBody = 0xffffffffu;
  if (!i.extra.contains("tree") || !i.extra["tree"].value("felled", false)) return;
  // applied (persistent) felled state: stump plus the trunk lying where it came to rest, both static
  Physics::remove(r.staticBody);
  r.staticBody = Physics::addStatic(t.stumpShape, i.pos, yawQuat(i.yaw), i.id << 16);
  auto& tr = i.extra["tree"]["trunk"];
  vec3 p = i.pos + yawQuat(i.yaw) * jvec3(tr["p"]);
  quat q = yawQuat(i.yaw) * quat(tr["q"][3].get<float>(), tr["q"][0].get<float>(), tr["q"][1].get<float>(), tr["q"][2].get<float>());
  r.trunkBody = Physics::addStatic(t.trunkShape, p, q, (i.id << 16) | kTrunkUD);
  r.treeFelled = true; r.trunkApplied = true;
}

void App::fellTree(Instance& i, InstanceRT& r, vec3 at, vec3 push) {
  if (r.treeFelled) return;
  const TreeDef& t = r.asset->tree;
  Physics::remove(r.staticBody);
  r.staticBody = Physics::addStatic(t.stumpShape, i.pos, yawQuat(i.yaw), i.id << 16);
  vec3 base = i.pos + vec3(0, t.stumpH, 0);
  r.trunkBody = Physics::addDynamic(t.trunkShape, base + vec3(0, 0.02f, 0), yawQuat(i.yaw), (i.id << 16) | kTrunkUD, t.mass);
  vec3 h = vec3(push.x, 0, push.z);
  if (glm::dot(h, h) < 1e-6f) h = vec3(at.x - i.pos.x, 0, at.z - i.pos.z) * -1.0f;
  if (glm::dot(h, h) < 1e-6f) h = vec3(1, 0, 0);
  h = glm::normalize(h);
  // topple: a horizontal shove high on the trunk (impulse from the hit side) plus a little lift off the cut
  float lever = std::max(1.0f, (t.top - t.stumpH) * 0.6f);
  Physics::addImpulse(r.trunkBody, h * t.mass * 1.6f, base + vec3(0, lever, 0));
  Physics::addImpulse(r.trunkBody, vec3(0, t.mass * 0.6f, 0), base);
  r.treeFelled = true; r.trunkApplied = false;
  ++treesFelled;
  audio.play("tree_fall", at, 1.0f);
  for (int n = 0; n < 14; ++n) fx.smoke(base + vec3(fx.rnd() - 0.5f, fx.rnd() * 0.3f, fx.rnd() - 0.5f) * 0.6f, vec3(fx.rnd() - 0.5f, 0.6f, fx.rnd() - 0.5f), 0.25f, 1.6f, vec4(0.45f, 0.4f, 0.3f, 0.4f));
}

bool App::treeImpact(uint64_t iid, vec3 at, vec3 dir, bool shell) {
  Instance* i = doc.find(iid);
  auto it = rt.find(iid);
  if (!i || it == rt.end() || !it->second.asset->tree.on) return false;
  InstanceRT& r = it->second;
  if (r.treeFelled) return true;
  if (r.asset->tree.hedge) { fx.impact(at, -dir, "wood"); return true; }   // banks and hedges stop rounds, never fall
  if (shell) { fellTree(*i, r, at, dir); return true; }
  r.treeHits += 1;
  fx.impact(at, -dir, "wood");
  if (r.treeHits >= r.asset->tree.hpBullet) fellTree(*i, r, at, dir);
  return true;
}

void App::blastTrees(vec3 p, float radius) {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    const TreeDef& t = r.asset->tree;
    if (!t.on || r.treeFelled || t.hedge) continue;
    // distance from the blast to the trunk axis (stump cut .. first fork)
    float y = std::clamp(p.y - i.pos.y, 0.0f, std::max(t.stumpH, t.top * 0.5f));
    float d = glm::length(p - (i.pos + vec3(0, y, 0))) - t.trunkR;
    if (d <= std::min(1.5f, radius)) fellTree(i, r, p, i.pos - p);
  }
}

void App::resetTrees() {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    if (!r.asset->tree.on) continue;
    bool applied = i.extra.contains("tree") && i.extra["tree"].value("felled", false);
    if (r.treeHits != 0 || r.treeFelled != applied || (r.treeFelled && !r.trunkApplied)) buildRT(i, r);
  }
}

void App::treeDrawItems(const Instance& i, const InstanceRT& r, std::vector<DrawItem>& op) {
  const TreeDef& t = r.asset->tree;
  mat4 M = instanceMatrix(i);
  if (!r.treeFelled) {
    op.push_back({&r.asset->opaque, M}); snapDraw(*this, i, op.back());   // V6: hedgerow banks: joined ends without caps (snap.cpp)
    R.foliage.push_back({&t, M, &r.leafLod});
    return;
  }
  op.push_back({&t.stumpMesh, M});
  vec3 p; quat q;
  if (Physics::getTransform(r.trunkBody, p, q)) {
    mat4 T = glm::translate(mat4(1), p) * glm::mat4_cast(q);
    op.push_back({&t.trunkMesh, T});
    R.foliage.push_back({&t, T * glm::translate(mat4(1), vec3(0, -t.stumpH, 0)), &r.leafLod});
  }
}

json App::treeApplyState(const Instance& i, const InstanceRT& r) const {
  vec3 p; quat q;
  if (!Physics::getTransform(r.trunkBody, p, q)) return json();
  quat inv = glm::inverse(yawQuat(i.yaw));
  vec3 lp = inv * (p - i.pos); quat lq = inv * q;
  return json{{"felled", true}, {"trunk", {{"p", tojson(lp)}, {"q", {lq.x, lq.y, lq.z, lq.w}}}}};
}
