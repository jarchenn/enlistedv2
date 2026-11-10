// FA2 first-person support arm (docs/fps-arms.md): ONE generated arm, the support hand under the weapon's fore-end
// and a plain cloth sleeve entering from the lower left, for every weapon (knife / grenades: the one hand on the item).
//  * Geometry is generated every frame (fixed topology, one dynamic vertex buffer per faction): every piece is a swept
//    elliptical tube whose cross-section keeps a fixed reference axis, so nothing rolls or twists - the sleeve is a
//    straight, slightly tapered tube with a cuff; the hand (palm, four curled fingers, thumb) only turns at the wrist.
//  * The hand (real size: palm 8.5 cm wide, about 18 cm to the middle fingertip) wraps the fore-end measured on the
//    weapon's own mesh (the cross-section at its support socket: width and height, an ellipse): palm under it, knuckle
//    line along the far-bottom edge, four separate fingers up the far side with their tips over the top-far edge, a
//    short thumb lying along the near side. The forearm aims at the view's lower left corner. content/settings/
//    fps_arms.json can measure a weapon further along its fore-end (gripZ) or override its grip / section.
//  * Sleeve material: the project's woven wool texture (wool_woven) recoloured per faction (olive drab / near-black
//    charcoal), a stitched seam along the sleeve and a stitched hem round the cuff painted into its albedo and normal
//    map; matte (roughness 0.9, metallic 0). Hand: a generated skin texture (colour variation, darker creases between
//    the fingers, at the joints and the wrist; creases as grooves in its normal map), every tube end capped.
//  * Safety (as FA1): in ADS the hand drops until nothing of it is in a 6 degree cone round the sight line; an arm that
//    still is, or that comes within 12 cm (25 cm in ADS) of the eye near the view centre, is not drawn.
#include "app.h"
#include "viewmodel.h"
#include <stb_image.h>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>

namespace {
constexpr float kPi = 3.14159265f;
constexpr int kTex = 512;              // sleeve texture: one turn round the sleeve (u) x kTexLen metres of its length (v)
constexpr float kTexLen = 0.25f;       // the cloth source repeats once per tile (seamless): a fine wool weave
constexpr float kBodyLen = 0.75f;      // the sleeve body's tile: 3 x kTexLen along the arm (elbow wear at a fixed place)
constexpr float kElbowS = 0.267f;      // the elbow's distance from the body tube's start (forearm 0.27 m, fixed)

struct ArmGen {
  bool tried = false, ok = false;
  GpuModel model[2];                   // 0 Allies (olive drab), 1 Axis (charcoal): parts 0 sleeve, 1 cuff, 2 hand / wrist
  std::vector<uint32_t> I; size_t nV = 0;
  uint32_t partFirst[5] = {0, 0, 0, 0, 0}, partCount[5] = {0, 0, 0, 0, 0};
  vec2 handLo{0}, handHi{0}, sleeve15{0};   // the last drawn arm's framing (NDC): hand bounds, 15 cm up the sleeve past the cuff
  vec3 jS{0}, jE{0}, jW{0}, jT{0};          // the last drawn arm's joints (camera space): shoulder, elbow, wrist, wrist target
  size_t sleeveV0 = 0, sleeveV1 = 0, handV1 = 0; uint32_t sleeveI1 = 0, handI1 = 0;   // smoothed-normal ranges
  std::vector<std::pair<uint32_t, uint32_t>> weld;   // the sleeve's uv seam vertex pairs
  json cfg = json::object();
};
ArmGen gA;

vec3 jv3(const json& j, const char* k, vec3 d) { return j.contains(k) && j[k].is_array() && j[k].size() == 3 ? vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()) : d; }

// ---------------------------------------------------------------- mesh builder (fixed topology)
struct Builder {
  std::vector<TexVertex> V; std::vector<uint32_t> I; bool idx = true;
  std::vector<std::pair<uint32_t, uint32_t>>* weld = nullptr;   // when set: record each ring's uv seam pair (same position)
  using RMod = std::function<float(size_t, float, vec3)>;      // radius factor for (ring, angle round it, its direction)
  // a tube through P (rings of N + 1 vertices, the seam duplicated for the uv), elliptical cross-section rx along the
  // reference axis ref (projected off the tangent: no roll), ry across; uv: u = round the tube x uRep, v = length / vLen
  // cap0 / cap1: close that end with a rounded cap (three rings of a half ellipsoid, normals leaning along the tube),
  // so no open tube end shows its inside
  void sweep(const std::vector<vec3>& P, const std::vector<vec2>& R, vec3 ref, int N, float uRep, float v0, float vLen, bool cap0 = false, bool cap1 = false, const RMod& rmod = nullptr, const RMod& smod = nullptr) {
    struct Ring { vec3 c, t, e1, e2; vec2 r; float v, lean; int src; };
    std::vector<Ring> rings;
    float s = 0;
    for (size_t i = 0; i < P.size(); ++i) {
      vec3 t = glm::normalize(P[std::min(i + 1, P.size() - 1)] - P[i > 0 ? i - 1 : 0]);
      vec3 e1 = ref - t * glm::dot(ref, t);
      if (glm::dot(e1, e1) < 1e-8f) e1 = glm::cross(t, std::fabs(t.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0));
      e1 = glm::normalize(e1);
      if (i > 0) s += glm::length(P[i] - P[i - 1]);
      rings.push_back({P[i], t, e1, glm::cross(t, e1), R[i], v0 + s / vLen, 0, (int)i});
    }
    auto cap = [&](const Ring& e, float sign) {
      std::vector<Ring> out; float L = std::min(e.r.x, e.r.y) * 0.9f;
      for (float a : {0.75f, 1.25f, 1.5707f}) { Ring q = e; q.c = e.c + e.t * (sign * L * std::sin(a)); q.r = e.r * std::cos(a); q.lean = sign * a; q.src = -1; out.push_back(q); }
      return out;
    };
    if (cap0) { auto c = cap(rings.front(), -1); rings.insert(rings.begin(), c.rbegin(), c.rend()); }
    if (cap1) { auto c = cap(rings.back(), 1); rings.insert(rings.end(), c.begin(), c.end()); }
    uint32_t base = (uint32_t)V.size();
    for (const Ring& q : rings) {
      if (weld) weld->push_back({(uint32_t)V.size(), (uint32_t)(V.size() + N)});
      for (int k = 0; k <= N; ++k) {
        float a = 2 * kPi * k / N, c = std::cos(a), sn = std::sin(a);
        float km = rmod && q.src >= 0 ? rmod((size_t)q.src, a, q.e1 * c + q.e2 * sn) : 1.0f;
        vec3 p = q.c + q.e1 * (c * q.r.x * km) + q.e2 * (sn * q.r.y * km);
        if (smod && q.src >= 0) p += q.t * smod((size_t)q.src, a, q.e1 * c + q.e2 * sn);
        vec3 ne = glm::normalize(q.e1 * (c / std::max(q.r.x, 1e-4f)) + q.e2 * (sn / std::max(q.r.y, 1e-4f)));
        vec3 n = glm::normalize(ne * std::cos(q.lean) + q.t * std::sin(q.lean));
        vec3 tu = -q.e1 * sn + q.e2 * c;
        float w = glm::dot(glm::cross(ne, tu), q.t) > 0 ? 1.0f : -1.0f;
        V.push_back({p, n, vec4(tu, w), vec2(uRep * k / N, q.v)});
      }
    }
    if (!idx) return;
    for (size_t i = 0; i + 1 < rings.size(); ++i)
      for (int k = 0; k < N; ++k) {
        uint32_t a = base + (uint32_t)(i * (N + 1) + k), b = a + 1, c2 = a + (uint32_t)(N + 1), d = c2 + 1;
        I.insert(I.end(), {a, b, c2, b, d, c2});   // counter-clockwise seen from outside
      }
  }
};

// ---------------------------------------------------------------- sleeve textures (generated once per faction)
struct Img { int w = 0, h = 0; std::vector<uint8_t> px; };
Img loadImg(const fs::path& p) {
  Img im; int c;
  stbi_set_flip_vertically_on_load(0);
  if (stbi_uc* d = stbi_load(p.string().c_str(), &im.w, &im.h, &c, 4)) { im.px.assign(d, d + (size_t)im.w * im.h * 4); stbi_image_free(d); }
  return im;
}
GLuint upload(const std::vector<uint8_t>& px, int w, int h, bool srgb) {
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t);
  int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
  glTextureStorage2D(t, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(t, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glGenerateTextureMipmap(t);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
  return t;
}

// skin: one 256 px tile shared by every skin piece, each mapped to a band of v (wrist 0-0.15, palm 0.15-0.45,
// fingers and thumb 0.5 root - 1 tip): a little colour variation (two octaves of value noise, redder in places),
// darker where the wrist goes into the cuff, a wrist crease, darker finger roots and the creases at the finger joints,
// and darker sides between the fingers; the creases are grooves in the normal map
float hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f; }
float vnoise(float x, float y, int per) {   // tileable value noise, period per cells
  int ix = (int)std::floor(x), iy = (int)std::floor(y); float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  auto h = [&](int a, int b) { return hash2(((a % per) + per) % per, ((b % per) + per) % per); };
  return glm::mix(glm::mix(h(ix, iy), h(ix + 1, iy), fx), glm::mix(h(ix, iy + 1), h(ix + 1, iy + 1), fx), fy);
}
// sa / sn / sr: the photographic skin set (content/textures/skin_hand: albedo, OpenGL normal, rma = roughness + AO),
// tiled at pore scale over the hand's bands; its albedo is normalised to its mean and tinted to `skin`, so the tone
// stays the configured one while the pores, blotches and fine lines come from the photo
void skinTex(vec3 skin, const Img& sa, const Img& sn, const Img& sr, GpuModel::MatTex& m) {
  const int S = 1024;
  std::vector<uint8_t> alb((size_t)S * S * 4), nrm((size_t)S * S * 4), mr((size_t)S * S * 4);
  std::vector<float> hgt((size_t)S * S, 0.0f);
  auto bil = [](const Img& im, float u, float v, int ch) {
    float x = u * im.w - 0.5f, y = v * im.h - 0.5f; int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0;
    auto P = [&](int xx, int yy) { xx = ((xx % im.w) + im.w) % im.w; yy = ((yy % im.h) + im.h) % im.h; return (float)im.px[((size_t)yy * im.w + xx) * 4 + ch]; };
    return glm::mix(glm::mix(P(x0, y0), P(x0 + 1, y0), fx), glm::mix(P(x0, y0 + 1), P(x0 + 1, y0 + 1), fx), fy);
  };
  auto lin = [](float c) { return std::pow(c / 255.0f, 2.2f); };
  vec3 mean(0); { size_t n = 0; for (size_t k = 0; k + 3 < sa.px.size(); k += 4 * 13, ++n) mean += vec3(lin(sa.px[k]), lin(sa.px[k + 1]), lin(sa.px[k + 2])); mean = glm::max(mean / float(std::max<size_t>(n, 1)), vec3(1e-3f)); }
  const float ku = 0.57f, kv = 1.5f;   // hand uv -> source tiles: the source tile is about 15 cm of skin (pore scale)
  auto line = [](float v, float at, float w) { return std::exp(-std::pow((v - at) / w, 2.0f)); };
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
    float u = (x + 0.5f) / S, v = (y + 0.5f) / S;
    float n1 = vnoise(u * 8, v * 8, 8) * 0.6f + vnoise(u * 32, v * 32, 32) * 0.4f, n2 = vnoise(u * 5 + 17, v * 5 + 3, 5);
    float shade = 0.96f + 0.06f * n1, crease = 0;
    float su = u * ku, sv = v * kv;
    vec3 photo = glm::clamp(vec3(lin(bil(sa, su, sv, 0)), lin(bil(sa, su, sv, 1)), lin(bil(sa, su, sv, 2))) / mean, vec3(0.5f), vec3(1.6f));
    photo = glm::pow(photo, vec3(1.8f));   // more of the photo's blotches, pores and lines
    if (v < 0.15f) {   // wrist: from inside the cuff (v 0) to the heel of the hand (0.15)
      shade *= glm::mix(0.7f, 1.0f, glm::smoothstep(0.0f, 0.06f, v));   // inside the cuff only; no crease line at the wrist (one continuous surface)
    } else if (v >= 0.5f) {   // a finger / the thumb, root (0.5) to tip (1)
      float f = (v - 0.5f) * 2;
      shade *= glm::mix(0.88f, 1.0f, glm::smoothstep(0.0f, 0.12f, f));   // the root, between the fingers (soft)
      crease = line(f, 0.44f, 0.014f) * 0.3f + line(f, 0.73f, 0.012f) * 0.22f;
      float side = std::fabs(u * 4 - std::round(u * 4)) / 4;   // u runs across the hand, a finger per quarter: its sides face the next finger
      shade *= 1.0f - 0.07f * std::exp(-std::pow(side / 0.03f, 2.0f));
    } else if (v > 0.15f) {
      crease = line(v, 0.44f, 0.008f) * 0.2f;   // the knuckle line at the palm's far end
    }
    shade *= 1.0f - 0.25f * crease;
    vec3 col = skin * shade * photo * vec3(1.0f + 0.04f * (n2 - 0.5f), 1.0f, 1.0f);
    size_t o = ((size_t)y * S + x) * 4;
    mr[o] = (uint8_t)std::lround(std::clamp(bil(sr, su, sv, 1) / 255.0f * (1 - 0.3f * crease), 0.0f, 1.0f) * 255); mr[o + 1] = (uint8_t)std::lround((0.74f + 0.26f * bil(sr, su, sv, 0) / 255.0f) * 255);   // matte skin: roughness 0.74-1 mr[o + 2] = 0; mr[o + 3] = 255;
    for (int q = 0; q < 3; ++q) alb[o + q] = (uint8_t)std::clamp(std::lround(std::pow(std::clamp(col[q], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f), 0L, 255L);
    alb[o + 3] = 255;
    hgt[(size_t)y * S + x] = n1 * 0.15f - crease;
  }
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
    auto H = [&](int xx, int yy) { return hgt[(size_t)std::clamp(yy, 0, S - 1) * S + (xx + S) % S]; };
    float su = (x + 0.5f) / S * ku, sv = (y + 0.5f) / S * kv;
    vec3 np(bil(sn, su, sv, 0) / 127.5f - 1, bil(sn, su, sv, 1) / 127.5f - 1, bil(sn, su, sv, 2) / 127.5f - 1);   // the photo's pores (OpenGL normal)
    np.x *= 2.0f; np.y *= 2.0f;   // stronger pore relief
    vec3 n = glm::normalize(vec3(np.x - (H(x + 1, y) - H(x - 1, y)) * 2.0f, np.y - (H(x, y + 1) - H(x, y - 1)) * 2.0f, std::max(0.2f, np.z)));
    size_t o = ((size_t)y * S + x) * 4;
    nrm[o] = (uint8_t)std::lround((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)std::lround((n.y * 0.5f + 0.5f) * 255); nrm[o + 2] = (uint8_t)std::lround((n.z * 0.5f + 0.5f) * 255); nrm[o + 3] = 255;
  }
  m.alb = upload(alb, S, S, true); m.n = upload(nrm, S, S, false); m.mr = upload(mr, S, S, false); m.aoInMR = true;
}

// one sleeve tile from the cloth set (cloth_grey: ambientCG Fabric030 1K, colour / normal / rma = roughness, AO): the
// weave's luminance x the faction colour, a low-frequency grime mask, lighter worn patches (the cuff's edge; the elbow on
// the body tile), uneven roughness; a stitched seam along v and, on the cuff, a stitched hem near its edge (v = 0).
// Stitches: thin thread a little lighter than the cloth, low relief. The cuff tile is kTexLen long; the body tile covers
// kBodyLen (the whole forearm and elbow: the elbow sits at a fixed distance, the bones never change length).
void sleeveTex(const Img& wc, const Img& wn, const Img& wr, vec3 cloth, vec3 thread, bool cuff, GpuModel::MatTex& m) {
  const int SW = kTex, SH = cuff ? kTex : kTex * 3;
  const float len = cuff ? kTexLen : kBodyLen, reps = len / kTexLen;   // the cloth source repeats once per kTexLen
  std::vector<uint8_t> alb((size_t)SW * SH * 4), nrm((size_t)SW * SH * 4), mr((size_t)SW * SH * 4);
  std::vector<float> hgt((size_t)SW * SH, 0.0f);
  auto bil = [](const Img& im, float u, float v, int ch) {
    float x = u * im.w - 0.5f, y = v * im.h - 0.5f; int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0;
    auto P = [&](int xx, int yy) { xx = ((xx % im.w) + im.w) % im.w; yy = ((yy % im.h) + im.h) % im.h; return (float)im.px[((size_t)yy * im.w + xx) * 4 + ch]; };
    return glm::mix(glm::mix(P(x0, y0), P(x0 + 1, y0), fx), glm::mix(P(x0, y0 + 1), P(x0 + 1, y0 + 1), fx), fy);
  };
  double mean = 0; for (size_t k = 0; k < wc.px.size(); k += 4) mean += 0.2126 * wc.px[k] + 0.7152 * wc.px[k + 1] + 0.0722 * wc.px[k + 2];
  mean = std::max(1.0, mean / std::max<size_t>(1, wc.px.size() / 4));
  const float seamU = 0.10f;
  auto stitch = [&](float along, float across, float pitch, float sl, float width) {   // 0..1: inside a stitch dash
    float f = along / pitch; f -= std::floor(f);
    float inA = 1.0f - glm::smoothstep(sl * 0.5f - 0.001f, sl * 0.5f, std::fabs(f - 0.5f) * pitch);
    float inW = 1.0f - glm::smoothstep(width * 0.3f, width * 0.5f + 0.0002f, std::fabs(across));
    return inA * inW;
  };
  for (int y = 0; y < SH; ++y) for (int x = 0; x < SW; ++x) {
    float u = (x + 0.5f) / SW, v = (y + 0.5f) / SH, sv = v * reps;   // sv: source v
    float L = (float)((0.2126 * bil(wc, u, sv, 0) + 0.7152 * bil(wc, u, sv, 1) + 0.0722 * bil(wc, u, sv, 2)) / mean);
    L = std::pow(std::clamp(L, 0.2f, 2.5f), 1.3f);
    float rough0 = bil(wr, u, sv, 0) / 255.0f, ao = bil(wr, u, sv, 1) / 255.0f;
    float mu = (u - seamU) * kTexLen, mv = v * len;
    float st = 0, fold = 1.0f - glm::smoothstep(0.0006f, 0.002f, std::fabs(mu));
    for (float off : {-0.005f, 0.005f}) st = std::max(st, stitch(mv, mu - off, 0.009f, 0.006f, 0.0012f));
    if (cuff) {   // the hem: two rows round the cuff, 7 and 15 mm from its edge; the turned-up hem's fold
      for (float row : {0.007f, 0.015f}) st = std::max(st, stitch(u * kTexLen, mv - row, 0.009f, 0.006f, 0.0012f));
      fold = std::max(fold, 1.0f - glm::smoothstep(0.0008f, 0.0022f, std::fabs(mv - 0.021f)));
    }
    // low-frequency masks (tile with the texture): grime, roughness breakup, wear
    float n1 = vnoise(u * 4, sv * 4, 4) * 0.65f + vnoise(u * 11 + 3, sv * 11 + 7, 11) * 0.35f;
    float n2 = vnoise(u * 7 + 5, sv * 7 + 2, 7);
    float grime = glm::smoothstep(0.5f, 0.85f, n1) * 0.28f;
    float wear = cuff ? (1 - glm::smoothstep(0.0f, 0.014f, mv)) * (0.55f + 0.45f * n2)                      // the cuff's edge
                      : std::exp(-std::pow((mv - kElbowS) / 0.05f, 2.0f)) * glm::smoothstep(0.35f, 0.7f, n2);   // the elbow
    wear = std::max(wear, glm::smoothstep(0.78f, 0.95f, n2) * 0.35f);   // a few scuffs anywhere
    float wsh = cuff ? 0.0f : glm::smoothstep(0.25f, 1.0f, std::sin(2 * kPi * (2 * u + sv * 3) + 5.0f * n1)) * 0.3f + glm::smoothstep(0.4f, 1.0f, std::sin(2 * kPi * (3 * u - sv * 2) + 4.0f * n2)) * 0.18f;
    ao *= 1.0f - wsh;
    vec3 col = cloth * L * (1.0f - 0.45f * fold) * (1.0f - grime) * (1.0f - wsh);
    col = glm::mix(col, col * 1.7f + vec3(0.008f), wear * 0.55f);   // worn: faded, lighter
    col = glm::mix(col, thread * (0.95f + 0.1f * L), st * 0.75f);
    float rough = std::clamp(rough0 * (0.85f + 0.3f * n2) - wear * 0.12f + grime * 0.08f, 0.35f, 1.0f);
    size_t o = ((size_t)y * SW + x) * 4;
    for (int q = 0; q < 3; ++q) alb[o + q] = (uint8_t)std::clamp(std::lround(std::pow(std::clamp(col[q], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f), 0L, 255L);
    alb[o + 3] = 255;
    mr[o] = (uint8_t)std::lround(std::clamp(ao * (1 - 0.3f * fold), 0.0f, 1.0f) * 255); mr[o + 1] = (uint8_t)std::lround(rough * 255); mr[o + 2] = 0; mr[o + 3] = 255;
    hgt[(size_t)y * SW + x] = st * 0.6f - fold * 0.6f;
  }
  for (int y = 0; y < SH; ++y) for (int x = 0; x < SW; ++x) {   // the cloth's own normal map, plus the stitch / fold relief
    float u = (x + 0.5f) / SW, sv = (y + 0.5f) / SH * reps;
    vec3 n(bil(wn, u, sv, 0) / 127.5f - 1, bil(wn, u, sv, 1) / 127.5f - 1, bil(wn, u, sv, 2) / 127.5f - 1);
    auto H = [&](int xx, int yy) { return hgt[(size_t)((yy + SH) % SH) * SW + (xx + SW) % SW]; };
    float dx = H(x + 1, y) - H(x - 1, y), dy = H(x, y + 1) - H(x, y - 1);
    n = glm::normalize(vec3(n.x - dx * 1.5f, n.y - dy * 1.5f, std::max(0.2f, n.z)));
    size_t o = ((size_t)y * SW + x) * 4;
    nrm[o] = (uint8_t)std::lround((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)std::lround((n.y * 0.5f + 0.5f) * 255); nrm[o + 2] = (uint8_t)std::lround((n.z * 0.5f + 0.5f) * 255); nrm[o + 3] = 255;
  }
  m.alb = upload(alb, SW, SH, true); m.n = upload(nrm, SW, SH, false); m.mr = upload(mr, SW, SH, false); m.aoInMR = true;
}

// ---------------------------------------------------------------- the arm's shape
// The arm is a fixed body: upper arm and forearm never change length. A farther or nearer grip changes the pose (a
// two-bone solve from a shoulder below-left of the eye, the elbow on a pole), never the bones.
constexpr float kUpper = 0.30f, kFore = 0.27f;   // shoulder - elbow, elbow - wrist (m)
constexpr int kMf = 3, kRW = 2 * (4 * kMf + 1);   // hand ring: 4 finger blocks of kMf segments across, dorsal + palmar
constexpr int kFR = 8, kFN = 2 * kMf + 2;         // per finger: rings after its root, vertices per finger ring

struct Pose {
  mat4 Mv{1};               // weapon -> camera
  ArmGrip g;                // the grip profile (weapon space): held section, its axis / up, hand shaping, elbow pole
  float curl = 1;           // 1 wrapped round it, 0 relaxed (times the profile's curl)
  bool right = false;       // mirrored: a right hand (knife / grenade), its arm from the lower right
};
struct ArmOut { vec3 shoulder{0}, elbow{0}, wrist{0}, target{0}, sleeve15{0}; size_t handV0 = 0; bool reached = true; };
float plen(const std::vector<vec3>& P) { float s = 0; for (size_t i = 1; i < P.size(); ++i) s += glm::length(P[i] - P[i - 1]); return std::max(s, 1e-4f); }
float sgn(float v) { return v < 0 ? -1.0f : 1.0f; }

// builds all pieces into B in camera space (fixed topology); records index / vertex ranges on the indexed build
void buildArm(Builder& B, const Pose& p, ArmOut& o) {
  const glm::mat3 R3(p.Mv);
  auto W = [&](vec3 w) { return vec3(p.Mv * vec4(w, 1)); };
  const float m = p.right ? -1.0f : 1.0f;
  const ArmGrip& g = p.g;
  // the held section's frame (weapon space): A its axis (the index finger's side), U its "top" (where the fingertips
  // end), Nw the near side (the thumb's side, mirrored for the right hand); an ellipse rx along Nw, ry along U
  const vec3 A = glm::normalize(g.axis), U = glm::normalize(g.up - A * glm::dot(g.up, A)), Nw = glm::cross(U, A) * m;
  const float rx = g.section.x, ry = g.section.y, curl = std::clamp(p.curl * g.curl, 0.0f, 1.0f);
  const vec3 C = g.grip + U * ry;
  // a point d metres off the section at angle deg round it (0 near side, -90 the bottom / palm side, +-180 far side,
  // 90 the top), z metres along its axis
  auto S = [&](float deg, float d, float z) { float a = radians(deg); return W(C + Nw * ((rx + d) * std::cos(a)) + U * ((ry + d) * std::sin(a)) + A * z); };
  auto outward = [&](float deg) { float a = radians(deg); return glm::normalize(R3 * (Nw * (std::cos(a) / std::max(rx, 1e-3f)) + U * (std::sin(a) / std::max(ry, 1e-3f)))); };
  const vec3 Ac = glm::normalize(R3 * A);

  // ---- the palm path (heel on the near side round the bottom to the knuckle line under the far edge), the wrist
  // point behind the heel, then the arm: a two-bone solve to that wrist
  const int nP = 7;
  float phH = -34.0f + g.palm, phK = -128.0f;   // heel low on the near side (its near face and thenar show from the eye), knuckles low on the far side   // heel under the near-bottom edge, knuckles low on the far side: the hand under the fore-end
  vec3 PP[nP]; float pw[nP], pt[nP];
  for (int i = 0; i < nP; ++i) {
    float t = i / float(nP - 1);
    pt[i] = glm::mix(0.0148f, 0.0105f, glm::smoothstep(0.0f, 0.8f, t)); pw[i] = glm::mix(0.034f, 0.039f, t);
    PP[i] = S(glm::mix(phH, phK, t), pt[i] + 0.010f * (1 - glm::smoothstep(0.0f, 0.5f, t)) + 0.006f, 0.0f);   // +6 mm: the bend round the stock stays wider than the palm is thick (no fold)
  }
  // the hand's long axis at the heel (from the knuckles through the heel, and the palm's last turn): the wrist stays
  // near neutral, the forearm continuing straight into the back of the hand
  vec3 handAx = glm::normalize(glm::normalize(PP[0] - PP[nP - 1]) + glm::normalize(PP[0] - PP[1]));
  // the forearm runs back along the weapon and down from under it, a little to the near side: on screen it rises
  // steeply from the bottom centre-left alongside the weapon (the same for every weapon: relative to its axis)
  vec3 nearC = glm::normalize(R3 * Nw);
  vec3 dirF = glm::normalize(-Ac * 0.55f + vec3(0, -0.70f, 0) + nearC * 0.62f);   // diagonal from the lower left on screen
  vec3 wdir = glm::normalize(dirF * 0.92f + handAx * 0.08f);   // the forearm leads straight to the base of the hand   // the wrist nearly straight: the hand continues the forearm
  vec3 wristT = PP[0] + wdir * 0.045f;   // the wrist on the line of the forearm (no kink at the heel), below the fore-end
  vec3 Sh = vec3(-0.24f * m, -0.36f, -0.02f) + vec3(g.shoulder.x * m, g.shoulder.y, g.shoulder.z);   // below, a little left of the eye, not at the cheek
  const float maxR = (kUpper + kFore) * 0.985f;   // a small bend is always kept: no straight-arm flip
  float L = glm::length(wristT - Sh);
  if (L > maxR) Sh += (wristT - Sh) / L * std::min(L - maxR, 0.07f);   // the shoulder reaches forward (protraction), up to 7 cm
  L = glm::length(wristT - Sh);
  o.reached = L <= maxR + 1e-4f;
  vec3 E, Wr;
  if (!o.reached) { vec3 u = glm::normalize(wristT - Sh); wristT = Sh + u * maxR; }   // out of reach: the wrist stops at full reach (bones never stretch)
  // elbow pole: toward where the forearm would put it in line with the hand (wrist neutral), biased by the profile
  vec3 Ed = wristT + dirF * kFore;
  solveTwoBone(Sh, wristT, kUpper, kFore, glm::normalize(Ed - Sh) + vec3(g.elbow.x * m, g.elbow.y, g.elbow.z) * 0.25f, E, Wr);
  o.shoulder = Sh; o.elbow = E; o.wrist = Wr; o.target = wristT;
  const vec3 f = glm::normalize(E - Wr), uDir = glm::normalize(Sh - E);   // forearm (wrist -> elbow), upper arm (elbow -> shoulder)
  vec3 bendN = glm::cross(f, uDir); bendN = glm::dot(bendN, bendN) > 1e-6f ? glm::normalize(bendN) : glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  vec3 inner = glm::normalize(uDir + f);   // the inside of the elbow bend (where the cloth compresses)

  // ---- sleeve (part 0 body, part 1 cuff): one path from the cuff's inner lip up the forearm, round the elbow, up the
  // upper arm past the shoulder; shaped (wider toward the elbow, irregular ellipse) with a few broad folds
  vec3 cuffEdge = Wr - f * 0.012f;   // the cuff covers the wrist up to the heel
  vec3 backC = vec3(-0.4f, 1, -0.2f) - f * glm::dot(vec3(-0.4f, 1, -0.2f), f); backC = glm::dot(backC, backC) > 1e-6f ? glm::normalize(backC) : bendN;   // the back of the hand's side
  std::vector<vec3> P; std::vector<float> sAt;
  // cuff (part 1): inner lip (the cloth turns in: thickness), the edge, 4.5 cm of cuff
  std::vector<vec3> Pc = {cuffEdge + f * 0.014f, cuffEdge + f * 0.002f, cuffEdge, cuffEdge + f * 0.015f, cuffEdge + f * 0.030f, cuffEdge + f * 0.045f};
  std::vector<vec2> Rc = {{0.0372f, 0.0322f}, {0.041f, 0.0355f}, {0.0428f, 0.0368f}, {0.0437f, 0.0377f}, {0.044f, 0.0379f}, {0.0442f, 0.0381f}};
  vec3 cuffEnd = Pc.back();
  o.sleeve15 = cuffEnd + f * 0.15f;
  // body (part 0)
  float lenF = glm::length(E - cuffEnd);
  vec3 bodyStart = cuffEnd - f * 0.012f;   // tucked inside the cuff: the cuff overlaps it, nothing shows between them
  for (int i = 0; i <= 16; ++i) P.push_back(glm::mix(bodyStart, E - f * 0.07f, i / 16.0f));
  for (int i = 1; i <= 6; ++i) { float t = i / 7.0f; vec3 a = E - f * 0.07f, c = E + uDir * 0.07f; P.push_back((1 - t) * (1 - t) * a + 2 * (1 - t) * t * E + t * t * c); }
  for (int i = 0; i <= 5; ++i) P.push_back(glm::mix(E + uDir * 0.07f, Sh + uDir * 0.06f, i / 5.0f));
  float sE = lenF + 0.012f;   // arc length from the body's start to the elbow (about)
  std::vector<vec2> Rb;
  { float s = 0; for (size_t i = 0; i < P.size(); ++i) {
      if (i) s += glm::length(P[i] - P[i - 1]); sAt.push_back(s);
      float r = glm::mix(0.0405f, 0.0442f, glm::smoothstep(0.010f, 0.024f, s)) + 0.016f * glm::smoothstep(0.03f, 0.19f, s) + 0.003f * glm::smoothstep(sE - 0.05f, sE + 0.05f, s);
      Rb.push_back(vec2(r, r * 0.87f));
  } }
  // radius modulation round each ring: an irregular ellipse, soft long folds on the forearm, compression folds on the
  // inside of the elbow, a soft roll above the cuff (geometry only where it changes the silhouette)
  const vec3 refS = bendN;
  auto foldBody = [&](size_t i, float th, vec3 dir) {
    float s = sAt[i], k = 1 + (0.022f * std::sin(2 * th + 0.7f) + 0.014f * std::sin(3 * th + 2.1f)) * glm::smoothstep(0.0f, 0.02f, s);
    float fm = glm::smoothstep(0.02f, 0.07f, s);   // broad diagonal wrinkles spiralling up the forearm (two crossing sets, uneven)
    k += fm * (0.045f * std::sin(2 * th + s * 62.0f + 0.9f) + 0.03f * std::sin(3 * th - s * 47.0f + 2.3f) + 0.02f * std::sin(th + s * 95.0f));
    float in = std::max(0.0f, glm::dot(dir, inner)), near = std::exp(-std::pow((s - sE) / 0.07f, 2.0f));
    k += 0.12f * in * in * near * (0.5f + 0.5f * std::sin((s - sE) / 0.024f * kPi));
    k += 0.07f * std::exp(-std::pow((s - 0.05f) / 0.02f, 2.0f));   // the cloth bunching above the cuff
    return k;
  };
  auto foldCuff = [&](size_t i, float th, vec3) { return 1 + 0.015f * std::sin(2 * th + 0.7f) + (i >= 3 ? 0.012f * std::sin(4 * th + 0.3f) : 0.0f); };
  if (B.idx) gA.sleeveV0 = B.V.size();
  for (vec3& q : P) q.z = std::min(q.z, -0.12f);   // the upper arm never comes behind or onto the near plane (it would smear across the view)
  B.sweep(P, Rb, refS, 24, 1.0f, 0.0f, kBodyLen, false, false, foldBody);
  if (B.idx) { gA.partCount[0] = (uint32_t)B.I.size(); gA.partFirst[1] = (uint32_t)B.I.size(); }
  auto slant = [&](size_t i, float, vec3 dir) { float w = glm::smoothstep(-0.4f, 1.0f, glm::dot(dir, backC)); float fade = i <= 2 ? 1.0f : i == 3 ? 0.55f : i == 4 ? 0.2f : 0.0f; return -0.022f * w * fade; };   // the opening cut on a slant: 2.2 cm longer over the back of the hand
  B.sweep(Pc, Rc, refS, 24, 1.0f, -0.016f / kTexLen, kTexLen, false, false, foldCuff, slant);
  if (B.idx) { gA.partCount[1] = (uint32_t)B.I.size() - gA.partFirst[1]; gA.partFirst[2] = (uint32_t)B.I.size(); gA.sleeveV1 = B.V.size(); gA.sleeveI1 = (uint32_t)B.I.size(); }

  // ---- hand (part 2): ONE continuous surface: forearm end (inside the cuff) -> wrist -> heel -> palm -> knuckle ring,
  // which splits into four finger tubes (each finger's root loop is a block of the knuckle ring, neighbours share the
  // web edge between them); rounded fingertips; the thumb is a separate capped tube rooted inside the thenar pad
  o.handV0 = B.V.size();
  // ring frames: e1 along the weapon axis (the hand's width), eR outward; sA keeps (e1, eR, t) right-handed with eR
  // pointing away from the section, the same for every hand ring (so finger blocks map to the same fingers)
  vec3 tMid = glm::normalize(PP[4] - PP[2]);
  float sA = sgn(glm::dot(glm::cross(tMid, Ac), outward(glm::mix(phH, phK, 0.5f))));
  auto frame = [&](vec3 t, vec3& e1, vec3& eR) { e1 = Ac - t * glm::dot(Ac, t); e1 = glm::dot(e1, e1) > 1e-8f ? glm::normalize(e1) * sA : glm::normalize(glm::cross(t, vec3(0, 1, 0))); eR = glm::normalize(glm::cross(t, e1)); };
  float profE = 2.4f;   // cross-section squareness: 2 = an ellipse (the wrist rings), 2.4 = a little fuller (palm, fingers)
  auto prof = [&](float u) { return std::sqrt(std::max(0.02f, 1.0f - std::pow(std::fabs(u) / 1.04f, profE))); };
  auto put = [&](vec3 pos, vec3 n, vec3 tu, vec3 t, float u, float v) { float w = glm::dot(glm::cross(n, tu), t) > 0 ? 1.0f : -1.0f; B.V.push_back({pos, n, vec4(tu, w), vec2(u, v)}); return (uint32_t)(B.V.size() - 1); };
  auto quads = [&](const std::vector<uint32_t>& L0, const std::vector<uint32_t>& L1) {
    if (!B.idx) return; size_t n = L0.size();
    for (size_t q = 0; q < n; ++q) { uint32_t a = L0[q], b = L0[(q + 1) % n], c2 = L1[q], d = L1[(q + 1) % n]; B.I.insert(B.I.end(), {a, b, c2, b, d, c2}); }
  };
  // a hand ring of kRW vertices: dorsal (j 0..4kMf: x from +w to -w, outward) then palmar (back to +w); arch: the palm hollows
  auto handRing = [&](vec3 c, vec3 t, float w, float th, float arch, float v) {
    vec3 e1, eR; frame(t, e1, eR); std::vector<uint32_t> L;
    const int J = 4 * kMf;
    for (int k = 0; k < kRW; ++k) {
      bool dors = k <= J; int j = dors ? k : J - (k - J - 1);
      float x = w * (1 - 2.0f * j / J), y = dors ? th * prof(x / w) : -th * prof(x / w) + arch * th * (1 - (x / w) * (x / w));
      L.push_back(put(c + e1 * x + eR * y, glm::normalize(e1 * (x / w) * 0.3f + eR * (dors ? 1.0f : -1.0f)), e1, t, (float)j / J, v));
    }
    return L;
  };
  std::vector<std::vector<uint32_t>> rings;
  vec3 R0 = Wr + f * 0.10f;   // deep inside the sleeve
  // ONE lofted surface: forearm end, wrist, then a smooth curve (quadratic Bezier through the heel) into the palm
  // path, with every ring's width / thickness / v blended along it: the frames turn gradually (no seam), the
  // sections are ellipses (no boxy block), and the back of the hand arches between the cuff and the gun
  std::vector<vec3> ctr = {R0, Wr}; std::vector<float> rw = {0.029f, 0.029f}, rt = {0.022f, 0.022f}, rv = {0.02f, 0.08f};
  for (int k = 1; k <= 3; ++k) {
    float s = k / 4.0f, b = glm::smoothstep(0.0f, 1.0f, s);
    ctr.push_back((1 - s) * (1 - s) * Wr + 2 * (1 - s) * s * PP[0] + s * s * PP[2]);
    rw.push_back(glm::mix(0.029f, pw[2], b)); rt.push_back(glm::mix(0.022f, pt[2], b) + 0.003f * std::sin(kPi * s)); rv.push_back(glm::mix(0.08f, 0.15f + 0.10f, s));
  }
  for (int i = 2; i < nP; ++i) { ctr.push_back(PP[i]); rw.push_back(pw[i]); rt.push_back(pt[i]); rv.push_back(0.15f + 0.30f * i / float(nP - 1)); }
  const int nR = (int)ctr.size();
  for (int i = 0; i < nR; ++i) {
    vec3 t = glm::normalize(ctr[std::min(i + 1, nR - 1)] - ctr[std::max(i - 1, 0)]);
    profE = 2.0f;   // elliptical sections throughout
    float arch = 0.18f * glm::smoothstep(4.0f, 7.0f, float(i));   // the palm's hollow fades in under the fingers
    rings.push_back(handRing(ctr[i], t, rw[i], rt[i], arch, rv[i]));
  }
  for (size_t i = 0; i + 1 < rings.size(); ++i) quads(rings[i], rings[i + 1]);
  const std::vector<uint32_t>& KR = rings.back();   // the knuckle ring
  // fingers: lengths / radii by finger (index .. little); block b of the knuckle ring -> finger (sA > 0 ? b : 3 - b)
  const float fl[4] = {0.074f, 0.081f, 0.076f, 0.060f}, fw[4] = {0.0087f, 0.0091f, 0.0086f, 0.0075f}, splay[4] = {0.004f, 0.0012f, -0.0012f, -0.004f};
  for (int b = 0; b < 4; ++b) {
    int fi = sA > 0 ? b : 3 - b;
    float fr = fw[fi] * 1.12f, zc = sA * pw[nP - 1] * (1 - 2.0f * (kMf * b + kMf * 0.5f) / (4 * kMf)), spread = splay[fi] * 0.4f * (1 + g.spread);
    std::vector<uint32_t> root;
    for (int q = 0; q <= kMf; ++q) root.push_back(KR[kMf * b + q]);
    for (int q = kMf; q >= 0; --q) root.push_back(KR[4 * kMf + 1 + (4 * kMf - (kMf * b + q))]);
    // march round the section at the finger's own offset until its length is used (or the top-far stop)
    float want = fl[fi] * glm::mix(0.55f, 1.0f, curl), arc = 0, ph0 = phK, ph1 = ph0;
    auto at = [&](float deg) { float a = radians(deg); return vec2((rx + fr) * std::cos(a), (ry + fr) * std::sin(a)); };
    while (arc < want && ph1 > -176.0f) { arc += glm::length(at(ph1 - 1) - at(ph1)); ph1 -= 1; }
    std::vector<vec3> FP;
    for (int i = 0; i <= kFR; ++i) {
      float t = i / float(kFR);
      FP.push_back(S(glm::mix(ph0, ph1, t), glm::mix(pt[nP - 1], fr, glm::smoothstep(0.0f, 0.3f, t)) + (1 - curl) * 0.03f * t * t, zc + spread * t));
    }
    std::vector<uint32_t> prev = root; vec3 tLast(0); float rLast = fr;
    for (int i = 1; i <= kFR; ++i) {
      float t = i / float(kFR);
      vec3 tt = glm::normalize(FP[std::min(i + 1, kFR)] - FP[i - 1]), e1, eR; frame(tt, e1, eR);
      float hw = glm::mix(pw[nP - 1] / 4, fr, std::min(1.0f, t * 3)) * glm::mix(1.0f, 0.80f, t), ht = glm::mix(pt[nP - 1], fr * 0.85f, std::min(1.0f, t * 3)) * glm::mix(1.0f, 0.82f, t);
      std::vector<uint32_t> L;
      for (int q = 0; q < kFN; ++q) {
        bool dors = q <= kMf; float x = dors ? hw * (1 - 2.0f * q / kMf) : hw * (-1 + 2.0f * (q - kMf - 1) / kMf);
        float y = (dors ? 1.0f : -1.0f) * ht * prof(x / hw);
        float u = (b + (dors ? (float)q : (float)(kFN - 1 - q)) / kMf) / 4.0f;
        L.push_back(put(FP[i] + e1 * x + eR * y, glm::normalize(e1 * (x / hw) * 0.5f + eR * (dors ? 1.0f : -1.0f)), e1, tt, u, 0.5f + 0.5f * t * 0.97f));
      }
      quads(prev, L); prev = L; tLast = tt; rLast = hw;
    }
    // the rounded tip: one more small ring and a tip vertex
    { vec3 e1, eR; frame(tLast, e1, eR); std::vector<uint32_t> L; vec3 c = FP[kFR] + tLast * (rLast * 0.45f);
      for (int q = 0; q < kFN; ++q) { bool dors = q <= kMf; float x = dors ? rLast * 0.6f * (1 - 2.0f * q / kMf) : rLast * 0.6f * (-1 + 2.0f * (q - kMf - 1) / kMf);
        float y = (dors ? 1.0f : -1.0f) * rLast * 0.5f * prof(x / (rLast * 0.6f));
        L.push_back(put(c + e1 * x + eR * y, glm::normalize(e1 * x + eR * y + tLast * rLast * 0.5f), e1, tLast, (b + 0.5f) / 4.0f, 0.99f)); }
      quads(prev, L);
      uint32_t tip = put(FP[kFR] + tLast * (rLast * 0.80f), tLast, e1, tLast, (b + 0.5f) / 4.0f, 1.0f);
      if (B.idx) for (int q = 0; q < kFN; ++q) B.I.insert(B.I.end(), {L[q], L[(q + 1) % kFN], tip});
    }
  }
  if (B.idx) { gA.handI1 = (uint32_t)B.I.size(); gA.handV1 = B.V.size(); }
  B.weld = nullptr;   // (the sleeve's seams only)
  {   // thumb (v 0.5-1): rooted inside the thenar pad at the heel's index side, forward along the near side, opposing
      // the fingers across the section
    std::vector<vec3> TP; std::vector<vec2> TR;
    for (int i = 0; i <= 6; ++i) {
      float t = i / 6.0f;
      TP.push_back(S(glm::mix(-44.0f, glm::mix(-30.0f, -24.0f, curl) + g.thumb, t), glm::mix(0.011f, 0.0084f, t) + (1 - curl) * 0.012f * t, glm::mix(0.006f, 0.080f, t)));
      float tr = glm::mix(0.0118f, 0.0083f, t) * (t < 0.2f ? 1.0f + (0.2f - t) * 1.5f : 1.0f);   // a wider root buried in the palm
      TR.push_back(vec2(tr * 0.88f, tr));
    }
    size_t tv0 = B.V.size();
    B.sweep(TP, TR, outward(-14.0f), 10, 1, 0.62f, plen(TP) / 0.38f, true, true);   // v from 0.62: no finger-root darkening at its base
    vec3 nb = outward(-40.0f);   // the root (cap and first rings) takes the palm's normal there: no dark crease where it enters the palm
    for (size_t v = tv0; v < std::min(B.V.size(), tv0 + 5 * 11); ++v) { float k = 1.0f - float(v - tv0) / (5 * 11); B.V[v].nrm = glm::normalize(glm::mix(B.V[v].nrm, nb, 0.8f * k)); }
  }
  if (B.idx) { gA.partCount[2] = (uint32_t)B.I.size() - gA.partFirst[2]; gA.partFirst[3] = (uint32_t)B.I.size(); }
  // ---- cuff tab (part 3, cuff cloth) and its button (part 4): a flap with thickness lying along the cuff on its
  // upper / near side, buttoned near the edge
  vec3 want = glm::normalize(vec3(0, 1, 0.5f)), dT = want - f * glm::dot(want, f);
  dT = glm::dot(dT, dT) > 1e-6f ? glm::normalize(dT) : bendN;
  vec3 across = glm::normalize(glm::cross(f, dT));
  {
    std::vector<vec3> TP; std::vector<vec2> TR;
    for (int i = 0; i <= 4; ++i) { float t = i / 4.0f; TP.push_back(cuffEdge + f * glm::mix(0.006f, 0.052f, t) + dT * 0.0478f); TR.push_back(vec2(glm::mix(0.0105f, 0.012f, t), 0.0026f)); }
    B.sweep(TP, TR, across, 12, 1, 0.0f, kTexLen, true, true);
  }
  if (B.idx) { gA.partCount[3] = (uint32_t)B.I.size() - gA.partFirst[3]; gA.partFirst[4] = (uint32_t)B.I.size(); }
  {
    vec3 bc = cuffEdge + f * 0.016f + dT * 0.0498f;
    std::vector<vec3> BP = {bc, bc + dT * 0.0015f, bc + dT * 0.003f}; std::vector<vec2> BR = {{0.0068f, 0.0068f}, {0.0072f, 0.0072f}, {0.0066f, 0.0066f}};
    B.sweep(BP, BR, f, 14, 1, 0, 1, true, true);
  }
  if (B.idx) gA.partCount[4] = (uint32_t)B.I.size() - gA.partFirst[4];
}

// smooth normals (area-weighted face normals) over an index range; the sleeve's uv seam vertices are welded
void smoothNormals(std::vector<TexVertex>& V, uint32_t i0, uint32_t i1, size_t v0, size_t v1, const std::vector<std::pair<uint32_t, uint32_t>>& weld) {
  for (size_t v = v0; v < v1; ++v) V[v].nrm = vec3(0);
  for (uint32_t t = i0; t + 3 <= i1 && t + 3 <= gA.I.size(); t += 3) {
    uint32_t a = gA.I[t], b = gA.I[t + 1], c = gA.I[t + 2];
    vec3 n = glm::cross(V[b].pos - V[a].pos, V[c].pos - V[a].pos);
    V[a].nrm += n; V[b].nrm += n; V[c].nrm += n;
  }
  for (auto& w : weld) { vec3 s = V[w.first].nrm + V[w.second].nrm; V[w.first].nrm = V[w.second].nrm = s; }
  for (size_t v = v0; v < v1; ++v) {
    vec3 n = V[v].nrm; n = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : vec3(0, 0, 1); V[v].nrm = n;
    vec3 tu = vec3(V[v].tan); tu -= n * glm::dot(n, tu); if (glm::dot(tu, tu) > 1e-12f) V[v].tan = vec4(glm::normalize(tu), V[v].tan.w);
  }
}

bool armsLoad() {
  if (gA.tried) return gA.ok;
  gA.tried = true;
  try { gA.cfg = json::parse(readTextFile(contentDir() / "settings" / "fps_arms.json")); } catch (...) { gA.cfg = json::object(); }
  const std::string cloth = gA.cfg.value("cloth", std::string("cloth_grey"));
  Img wc = loadImg(contentDir() / "textures" / cloth / "color.jpg"), wn = loadImg(contentDir() / "textures" / cloth / "normal.jpg"), wr = loadImg(contentDir() / "textures" / cloth / "rma.jpg");
  Img sa = loadImg(contentDir() / "textures" / "skin_hand" / "albedo.png"), sn = loadImg(contentDir() / "textures" / "skin_hand" / "normal.png"), sr = loadImg(contentDir() / "textures" / "skin_hand" / "rma.png");
  if (sa.px.empty()) { sa.w = sa.h = 2; sa.px.assign(16, 180); }   // no skin set: flat stand-ins (the generated creases still paint)
  if (sn.px.empty()) { sn.w = sn.h = 2; sn.px.assign(16, 128); for (int k = 0; k < 4; ++k) sn.px[k * 4 + 2] = 255; }
  if (sr.px.empty()) { sr.w = sr.h = 2; sr.px.assign(16, 255); for (int k = 0; k < 4; ++k) sr.px[k * 4] = 150; }
  if (wr.px.empty()) { wr.w = wr.h = 2; wr.px.assign(16, 255); for (int k = 0; k < 4; ++k) wr.px[k * 4] = 210; }   // no rma: roughness 0.82, AO 1
  if (wc.px.empty() || wn.px.empty()) {   // no cloth set: flat 2x2 stand-ins (the stitches still paint)
    wc.w = wc.h = wn.w = wn.h = 2; wc.px.assign(16, 128); wn.px.assign(16, 128); for (int k = 0; k < 4; ++k) wn.px[k * 4 + 2] = 255;
  }
  Builder B; Pose p; ArmOut w;
  B.weld = &gA.weld;
  buildArm(B, p, w);
  B.weld = nullptr;
  gA.I = B.I; gA.nV = B.V.size(); gA.partFirst[0] = 0;
  const json& col = gA.cfg.value("sleeve", json::object());
  vec3 clothC[2] = {jv3(col, "allies", vec3(0.105f, 0.100f, 0.048f)), jv3(col, "axis", vec3(0.030f, 0.030f, 0.032f))};          // linear: olive drab, charcoal
  vec3 thread[2] = {jv3(col, "alliesThread", vec3(0.15f, 0.14f, 0.075f)), jv3(col, "axisThread", vec3(0.065f, 0.065f, 0.07f))};   // a little lighter than the cloth
  vec3 skin = jv3(gA.cfg, "skin", vec3(0.44f, 0.25f, 0.155f));
  for (int f = 0; f < 2; ++f) {
    GpuModel& g = gA.model[f];
    glCreateVertexArrays(1, &g.vao); glCreateBuffers(1, &g.vbo); glCreateBuffers(1, &g.ebo);
    glNamedBufferStorage(g.vbo, gA.nV * sizeof(TexVertex), B.V.data(), GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferStorage(g.ebo, gA.I.size() * 4, gA.I.data(), 0);
    glVertexArrayVertexBuffer(g.vao, 0, g.vbo, 0, sizeof(TexVertex)); glVertexArrayElementBuffer(g.vao, g.ebo);
    auto attr = [&](int loc, int n, size_t off) { glEnableVertexArrayAttrib(g.vao, loc); glVertexArrayAttribFormat(g.vao, loc, n, GL_FLOAT, GL_FALSE, (GLuint)off); glVertexArrayAttribBinding(g.vao, loc, 0); };
    attr(0, 3, offsetof(TexVertex, pos)); attr(1, 3, offsetof(TexVertex, nrm)); attr(2, 4, offsetof(TexVertex, tan)); attr(3, 2, offsetof(TexVertex, uv));
    GpuModel::MatTex body, cuff; body.name = "sleeve"; cuff.name = "cuff";
    sleeveTex(wc, wn, wr, clothC[f], thread[f], false, body); sleeveTex(wc, wn, wr, clothC[f], thread[f], true, cuff);
    GpuModel::MatTex hand; hand.name = "skin"; skinTex(skin, sa, sn, sr, hand);
    g.mats = {body, cuff, hand}; g.texAlb = body.alb; g.texN = body.n; g.textureCount = 4;
    const char* names[5] = {"sleeve", "cuff", "hand", "cuff tab", "button"};
    for (int k = 0; k < 5; ++k) {
      ModelPart pt; pt.name = names[k]; pt.role = "arm"; pt.first = gA.partFirst[k]; pt.count = gA.partCount[k];
      pt.metal = 0; pt.rough = 1.0f; pt.textured = k < 4; pt.mat = k == 3 ? 1 : k < 3 ? k : -1;   // cloth roughness: its MR texture
      pt.base = vec4(1);
      if (k == 4) { pt.base = f ? vec4(0.11f, 0.11f, 0.115f, 1) : vec4(0.06f, 0.045f, 0.025f, 1); pt.metal = f ? 0.6f : 0.0f; pt.rough = f ? 0.45f : 0.35f; }   // Axis: grey metal; Allies: brown plastic
      g.parts.push_back(pt);
    }
    g.tris = (int)gA.I.size() / 3;
  }
  logf("fps arms: generated support arm (%zu verts, %zu tris; sleeve %u, cuff %u, hand %u, tab %u, button %u tris; 5 draws)", gA.nV, gA.I.size() / 3,
       gA.partCount[0] / 3, gA.partCount[1] / 3, gA.partCount[2] / 3, gA.partCount[3] / 3, gA.partCount[4] / 3);
  return gA.ok = true;
}

// The weapon's cross-section at its support socket (or gripZ), from its baked mesh (weapon space): the bottom of the
// fore-end under the socket, centred, the width of its lowest 4 cm and its height
struct GripGeo { bool ok = false; vec3 gL{0}; float wL = 0.044f, hL = 0.044f; };
const GripGeo& gripGeo(const WeaponDef& d) {
  static std::map<std::string, GripGeo> cache;
  auto it = cache.find(d.id);
  if (it != cache.end()) return it->second;
  GripGeo g; g.gL = d.gripL;
  const json& wj = gA.cfg.value("weapons", json::object());
  if (wj.contains(d.id)) g.gL.z = wj[d.id].value("gripZ", g.gL.z);   // measured further along the fore-end
  try {
    json j = json::parse(readTextFile(d.file));
    std::string dir = j.contains("model") ? j["model"].value("dir", std::string()) : std::string();
    std::ifstream f(contentDir() / "weapons" / "models" / dir / "mesh.bin", std::ios::binary);
    uint32_t hdr[3] = {0, 0, 0};
    if (!dir.empty() && f.read((char*)hdr, sizeof hdr) && hdr[0] == 0x31574D4Eu) {
      std::vector<TexVertex> V(hdr[1]); std::vector<uint32_t> I(hdr[2]);
      f.read((char*)V.data(), V.size() * sizeof(TexVertex)); f.read((char*)I.data(), I.size() * 4);
      vec3 s = g.gL, lo(1e9f), hi(-1e9f); std::vector<vec3> pts;
      for (size_t t = 0; f && t + 2 < I.size(); t += 3) {   // every triangle edge cut by the plane z = s.z
        vec3 q[3] = {V[I[t]].pos, V[I[t + 1]].pos, V[I[t + 2]].pos};
        for (int e = 0; e < 3; ++e) {
          vec3 a = q[e], b = q[(e + 1) % 3];
          if ((a.z - s.z) * (b.z - s.z) > 0 || a.z == b.z) continue;
          vec3 p = glm::mix(a, b, (s.z - a.z) / (b.z - a.z));
          if (std::fabs(p.x - s.x) < 0.07f && p.y < s.y + 0.06f && p.y > s.y - 0.06f) { lo = glm::min(lo, p); hi = glm::max(hi, p); pts.push_back(p); }
        }
      }
      if (pts.size() > 4) {   // the width of the lowest 4 cm (where the palm is), the height up to the band's top
        float x0 = 1e9f, x1 = -1e9f; for (vec3 q : pts) if (q.y < lo.y + 0.04f) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); }
        g.gL = vec3((x0 + x1) * 0.5f, lo.y, s.z); g.wL = x1 - x0; g.hL = std::min(hi.y - lo.y, 0.075f); g.ok = true;
      }
    }
  } catch (...) {}
  logf("fps arms: %s support grip (%.3f, %.3f, %.3f) section %.3f x %.3f%s", d.id.c_str(), g.gL.x, g.gL.y, g.gL.z, g.wL, g.hL, g.ok ? "" : " (socket: no mesh)");
  return cache.emplace(d.id, g).first->second;
}
// a grip profile's fields from json (any subset)
void applyProfile(const json& j, ArmGrip& g) {
  auto v2 = [&](const char* k, vec2 d) { return j.contains(k) && j[k].is_array() && j[k].size() == 2 ? vec2(j[k][0].get<float>(), j[k][1].get<float>()) : d; };
  g.grip = jv3(j, "grip", g.grip); g.axis = jv3(j, "axis", g.axis); g.up = jv3(j, "up", g.up); g.section = v2("section", g.section);
  g.palm = j.value("palm", g.palm); g.curl = j.value("curl", g.curl); g.spread = j.value("spread", g.spread); g.thumb = j.value("thumb", g.thumb);
  g.elbow = jv3(j, "elbow", g.elbow); g.shoulder = jv3(j, "shoulder", g.shoulder); g.ads = jv3(j, "ads", g.ads); g.slide = j.value("slide", g.slide);
}
}   // namespace

bool fpsArmsReady() { return armsLoad(); }
bool fpsArmsIsModel(const GpuModel* g) { return g && (g == &gA.model[0] || g == &gA.model[1]); }   // self-tests: an arm draw
int fpsArmsFaction(const GpuModel* g) { return g == &gA.model[0] ? 0 : g == &gA.model[1] ? 1 : -1; }   // 0 Allies, 1 Axis
// a weapon's grip profile: the measured fore-end (its bottom, the width of its lowest 4 cm, its height; the section
// axis is the weapon's +z), then fps_arms.json "default", then fps_arms.json "weapons"."<id>" (docs/fps-arms.md)
void fpsArmsGrip(const WeaponDef& d, ArmGrip& g) {
  armsLoad();
  const GripGeo& m = gripGeo(d);
  float hw = std::clamp(m.wL * 0.5f, 0.012f, 0.045f);
  g = ArmGrip{}; g.grip = m.gL; g.section = vec2(hw, std::clamp(m.hL * 0.5f, hw, 0.04f));
  if (gA.cfg.contains("default")) applyProfile(gA.cfg["default"], g);
  const json& w = gA.cfg.value("weapons", json::object());
  if (w.contains(d.id)) applyProfile(w[d.id], g);
}
void fpsArmsLastFraming(vec2& handLo, vec2& handHi, vec2& sleeve15) { handLo = gA.handLo; handHi = gA.handHi; sleeve15 = gA.sleeve15; }
void fpsArmsLastJoints(vec3& shoulder, vec3& elbow, vec3& wrist, vec3& target) { shoulder = gA.jS; elbow = gA.jE; wrist = gA.jW; target = gA.jT; }

// Draws the support arm holding the grip profile g (weapon space) for the weapon-to-camera transform weaponToCam;
// cam = camera-to-world. curl 1 = wrapped round the section, 0 relaxed. rightHand: the mirrored hand, its arm from the
// lower right (knife / grenades). Out: the wrist (camera space). False: not drawn (no generated arm, or the safety hid
// it: in the ADS sight cone / at the eye).
bool fpsSupportHandDraw(App& app, const mat4& cam, const mat4& weaponToCam, const ArmGrip& grip, float curl, bool rightHand, bool axis, vec3* wristOut) {
  if (!armsLoad()) return false;
  float ads = app.adsT * app.adsT * (3 - 2 * app.adsT);
  const float cone = std::tan(radians(gA.cfg.value("sightConeDeg", 6.0f))), nearZ = gA.cfg.value("nearClear", 0.12f), nearAds = gA.cfg.value("nearClearAds", 0.25f);
  Pose p; p.Mv = weaponToCam; p.g = grip; p.g.grip += grip.ads * ads; p.curl = std::clamp(curl, 0.0f, 1.0f); p.right = rightHand;
  const vec3 A = glm::normalize(grip.axis);
  Builder B; B.idx = false; ArmOut out;
  // out of reach: the hand slides back along the held section (toward the shooter) up to the profile's slide
  for (int it = 0; it < 4; ++it) {
    B.V.clear(); buildArm(B, p, out);
    if (out.reached) break;
    float miss = glm::length(out.target - out.wrist), slid = glm::length(p.g.grip - grip.grip - grip.ads * ads);
    if (slid >= grip.slide - 1e-4f) break;
    p.g.grip -= A * std::min(miss * 1.3f + 0.005f, grip.slide - slid);
  }
  int inCone = 0, tooNear = 0;
  for (int it = 0; it < 7; ++it) {
    if (it) { B.V.clear(); buildArm(B, p, out); }
    inCone = tooNear = 0;
    for (size_t v = 0; v < B.V.size(); v += 3) {
      vec3 q = B.V[v].pos; float fwd = -q.z, rr = glm::length(vec2(q.x, q.y));
      if (fwd <= 0.005f) continue;
      if (ads > 0.3f && rr < fwd * cone) ++inCone;
      if (fwd < (ads > 0.3f ? nearAds : nearZ) && rr < fwd * 0.5f) ++tooNear;   // a sleeve end-on / a wrist at the cheek
    }
    if (inCone == 0) break;
    p.g.grip.y -= 0.012f;   // the hand drops below the sight line
  }
  gA.jS = out.shoulder; gA.jE = out.elbow; gA.jW = out.wrist; gA.jT = out.target;
  if (wristOut) *wristOut = out.wrist;
  float ty = std::tan(radians(app.vmFovNow()) * 0.5f); vec2 tanHalf(ty * app.R.width / std::max(1, app.R.height), ty);
  auto ndc = [&](vec3 q) { float z = std::max(1e-3f, -q.z); return vec2(q.x / (z * tanHalf.x), q.y / (z * tanHalf.y)); };
  gA.handLo = vec2(1e9f); gA.handHi = vec2(-1e9f);
  for (size_t v = out.handV0; v < B.V.size(); ++v) { vec2 c = ndc(B.V[v].pos); gA.handLo = glm::min(gA.handLo, c); gA.handHi = glm::max(gA.handHi, c); }
  gA.sleeve15 = ndc(out.sleeve15);
  bool bad = false;   // a non-finite vertex, or any of the hand / sleeve near the eye or behind it: not drawn (never a smear)
  for (size_t v = 0; v < B.V.size() && !bad; ++v) { const vec3& q = B.V[v].pos; bad = !std::isfinite(q.x + q.y + q.z) || (v >= out.handV0 && -q.z < 0.06f); }
  if (inCone > 0 || tooNear > 0 || bad || B.V.size() != gA.nV) {
    static int lastWhy = -1; int why = (inCone > 0) + 2 * (tooNear > 0) + 4 * bad + 8 * (B.V.size() != gA.nV);
    if (why != lastWhy) { lastWhy = why; logf("fps arms: not drawn (cone %d, near %d, bad %d, size %d) wrist (%.2f %.2f %.2f) elbow (%.2f %.2f %.2f) shoulder (%.2f %.2f %.2f)", inCone, tooNear, (int)bad, (int)(B.V.size() != gA.nV), out.wrist.x, out.wrist.y, out.wrist.z, out.elbow.x, out.elbow.y, out.elbow.z, out.shoulder.x, out.shoulder.y, out.shoulder.z); }
    return false;
  }
  smoothNormals(B.V, 0, gA.sleeveI1, gA.sleeveV0, gA.sleeveV1, gA.weld);              // the folded sleeve and cuff
  smoothNormals(B.V, gA.partFirst[2], gA.handI1, gA.sleeveV1, gA.handV1, {});         // the continuous hand
  GpuModel& g = gA.model[axis ? 1 : 0];
  glNamedBufferSubData(g.vbo, 0, B.V.size() * sizeof(TexVertex), B.V.data());
  for (int k = 0; k < 5; ++k) app.R.viewModelTex.push_back({&g, k, cam});
  return true;
}
