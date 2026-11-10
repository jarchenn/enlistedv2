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
#include <map>

namespace {
constexpr float kPi = 3.14159265f;
constexpr int kTex = 512;              // sleeve texture: one turn round the sleeve (u) x kTexLen metres of its length (v)
constexpr float kTexLen = 0.28f;

struct ArmGen {
  bool tried = false, ok = false;
  GpuModel model[2];                   // 0 Allies (olive drab), 1 Axis (charcoal): parts 0 sleeve, 1 cuff, 2 hand / wrist
  std::vector<uint32_t> I; size_t nV = 0;
  uint32_t partFirst[3] = {0, 0, 0}, partCount[3] = {0, 0, 0};
  vec2 handLo{0}, handHi{0}, sleeve15{0};   // the last drawn arm's framing (NDC): hand bounds, 15 cm up the sleeve past the cuff
  json cfg = json::object();
};
ArmGen gA;

vec3 jv3(const json& j, const char* k, vec3 d) { return j.contains(k) && j[k].is_array() && j[k].size() == 3 ? vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()) : d; }

// ---------------------------------------------------------------- mesh builder (fixed topology)
struct Builder {
  std::vector<TexVertex> V; std::vector<uint32_t> I; bool idx = true;
  // a tube through P (rings of N + 1 vertices, the seam duplicated for the uv), elliptical cross-section rx along the
  // reference axis ref (projected off the tangent: no roll), ry across; uv: u = round the tube x uRep, v = length / vLen
  // cap0 / cap1: close that end with a rounded cap (three rings of a half ellipsoid, normals leaning along the tube),
  // so no open tube end shows its inside
  void sweep(const std::vector<vec3>& P, const std::vector<vec2>& R, vec3 ref, int N, float uRep, float v0, float vLen, bool cap0 = false, bool cap1 = false) {
    struct Ring { vec3 c, t, e1, e2; vec2 r; float v, lean; };
    std::vector<Ring> rings;
    float s = 0;
    for (size_t i = 0; i < P.size(); ++i) {
      vec3 t = glm::normalize(P[std::min(i + 1, P.size() - 1)] - P[i > 0 ? i - 1 : 0]);
      vec3 e1 = ref - t * glm::dot(ref, t);
      if (glm::dot(e1, e1) < 1e-8f) e1 = glm::cross(t, std::fabs(t.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0));
      e1 = glm::normalize(e1);
      if (i > 0) s += glm::length(P[i] - P[i - 1]);
      rings.push_back({P[i], t, e1, glm::cross(t, e1), R[i], v0 + s / vLen, 0});
    }
    auto cap = [&](const Ring& e, float sign) {
      std::vector<Ring> out; float L = std::min(e.r.x, e.r.y) * 0.9f;
      for (float a : {0.75f, 1.25f, 1.5707f}) { Ring q = e; q.c = e.c + e.t * (sign * L * std::sin(a)); q.r = e.r * std::cos(a); q.lean = sign * a; out.push_back(q); }
      return out;
    };
    if (cap0) { auto c = cap(rings.front(), -1); rings.insert(rings.begin(), c.rbegin(), c.rend()); }
    if (cap1) { auto c = cap(rings.back(), 1); rings.insert(rings.end(), c.begin(), c.end()); }
    uint32_t base = (uint32_t)V.size();
    for (const Ring& q : rings)
      for (int k = 0; k <= N; ++k) {
        float a = 2 * kPi * k / N, c = std::cos(a), sn = std::sin(a);
        vec3 p = q.c + q.e1 * (c * q.r.x) + q.e2 * (sn * q.r.y);
        vec3 ne = glm::normalize(q.e1 * (c / std::max(q.r.x, 1e-4f)) + q.e2 * (sn / std::max(q.r.y, 1e-4f)));
        vec3 n = glm::normalize(ne * std::cos(q.lean) + q.t * std::sin(q.lean));
        vec3 tu = -q.e1 * sn + q.e2 * c;
        float w = glm::dot(glm::cross(ne, tu), q.t) > 0 ? 1.0f : -1.0f;
        V.push_back({p, n, vec4(tu, w), vec2(uRep * k / N, q.v)});
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
// albedo + tangent-space normal of one sleeve tile: woven wool (luminance x the faction colour, its own normal map),
// a stitched seam along v at u = seamU (two rows of short stitches either side of a darker fold), and for the cuff a
// stitched hem: two rows of stitches round the sleeve near its edge (v = 0). Stitches: lighter thread, raised a little.
void sleeveTex(const Img& wc, const Img& wn, vec3 cloth, vec3 thread, bool cuff, GpuModel::MatTex& m) {
  const int S = kTex;
  std::vector<uint8_t> alb((size_t)S * S * 4), nrm((size_t)S * S * 4);
  std::vector<float> hgt((size_t)S * S, 0.0f);
  const float wScale = kTexLen / 0.3f;   // wool_woven tile: 0.3 m; this tile: kTexLen along v (and about one sleeve turn along u)
  double mean = 0; for (size_t k = 0; k < wc.px.size(); k += 4) mean += 0.2126 * wc.px[k] + 0.7152 * wc.px[k + 1] + 0.0722 * wc.px[k + 2];
  mean = std::max(1.0, mean / std::max<size_t>(1, wc.px.size() / 4));
  const float seamU = 0.10f, px = 1.0f / S;
  auto stitch = [&](float along, float across, float pitch, float len, float width) {   // 0..1: inside a stitch dash
    float f = along / pitch; f -= std::floor(f);
    float inA = 1.0f - glm::smoothstep(len * 0.5f - 0.0012f, len * 0.5f, std::fabs(f - 0.5f) * pitch);   // a dash with rounded ends
    float inW = 1.0f - glm::smoothstep(width * 0.25f, width * 0.5f + 0.0003f, std::fabs(across));
    return inA * inW * (0.75f + 0.25f * std::sqrt(std::max(0.0f, 1.0f - std::pow(across / (width * 0.5f + 1e-4f), 2.0f))));   // thread: rounded across
  };
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
    float u = (x + 0.5f) * px, v = (y + 0.5f) * px;
    int sx = (int)(u * wScale * wc.w) % wc.w, sy = (int)(v * wScale * wc.h) % wc.h;
    const uint8_t* c = &wc.px[((size_t)sy * wc.w + sx) * 4];
    float L = (float)((0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / mean);
    L = std::pow(std::clamp(L, 0.2f, 2.5f), 0.9f);
    // metres on the sleeve: across = round the tube (u x circumference ~ kTexLen), along = v x kTexLen
    float mu = (u - seamU) * kTexLen, mv = v * kTexLen;
    float h = 0, st = 0, fold = 0;
    // the seam: a darker fold, a stitch row 6 mm either side (stitch 8 mm, pitch 11 mm, thread 2 mm)
    fold = 1.0f - glm::smoothstep(0.0006f, 0.0022f, std::fabs(mu));
    for (float off : {-0.006f, 0.006f}) st = std::max(st, stitch(mv, mu - off, 0.011f, 0.008f, 0.002f));
    if (cuff) {   // the hem: two rows round the cuff, 6 and 13 mm from its edge
      float mvE = v * kTexLen;   // the cuff part starts its v at the edge
      for (float row : {0.007f, 0.016f}) st = std::max(st, stitch(u * kTexLen, mvE - row, 0.011f, 0.008f, 0.002f));
      fold = std::max(fold, 1.0f - glm::smoothstep(0.0008f, 0.0024f, std::fabs(mvE - 0.023f)));   // the turned-up hem's fold
    }
    h = st * 1.6f - fold * 0.9f;
    vec3 col = cloth * L * (1.0f - 0.45f * fold);
    col = glm::mix(col, thread * (0.9f + 0.2f * L), st);
    size_t o = ((size_t)y * S + x) * 4;
    for (int q = 0; q < 3; ++q) alb[o + q] = (uint8_t)std::clamp(std::lround(std::pow(std::clamp(col[q], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f), 0L, 255L);
    alb[o + 3] = 255;
    hgt[(size_t)y * S + x] = h;
  }
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {   // the wool's own normal map, plus the stitch / fold relief
    float u = (x + 0.5f) * px, v = (y + 0.5f) * px;
    int sx = (int)(u * wScale * wn.w) % wn.w, sy = (int)(v * wScale * wn.h) % wn.h;
    const uint8_t* c = &wn.px[((size_t)sy * wn.w + sx) * 4];
    vec3 n(c[0] / 127.5f - 1, c[1] / 127.5f - 1, c[2] / 127.5f - 1);
    auto H = [&](int xx, int yy) { return hgt[(size_t)((yy + S) % S) * S + (xx + S) % S]; };
    float dx = H(x + 1, y) - H(x - 1, y), dy = H(x, y + 1) - H(x, y - 1);
    n = glm::normalize(vec3(n.x - dx * 3.0f, n.y - dy * 3.0f, std::max(0.2f, n.z)));
    size_t o = ((size_t)y * S + x) * 4;
    nrm[o] = (uint8_t)std::lround((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)std::lround((n.y * 0.5f + 0.5f) * 255); nrm[o + 2] = (uint8_t)std::lround((n.z * 0.5f + 0.5f) * 255); nrm[o + 3] = 255;
  }
  m.alb = upload(alb, S, S, true); m.n = upload(nrm, S, S, false); m.mr = 0;
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
void skinTex(vec3 skin, GpuModel::MatTex& m) {
  const int S = 256;
  std::vector<uint8_t> alb((size_t)S * S * 4), nrm((size_t)S * S * 4);
  std::vector<float> hgt((size_t)S * S, 0.0f);
  auto line = [](float v, float at, float w) { return std::exp(-std::pow((v - at) / w, 2.0f)); };
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
    float u = (x + 0.5f) / S, v = (y + 0.5f) / S;
    float n1 = vnoise(u * 8, v * 8, 8) * 0.6f + vnoise(u * 32, v * 32, 32) * 0.4f, n2 = vnoise(u * 5 + 17, v * 5 + 3, 5);
    float shade = 0.90f + 0.16f * n1, crease = 0;
    if (v < 0.15f) {   // wrist: from inside the cuff (v 0) to the heel of the hand (0.15)
      shade *= glm::mix(0.55f, 1.0f, glm::smoothstep(0.0f, 0.07f, v));
      crease = line(v, 0.125f, 0.004f) * 0.8f;
    } else if (v >= 0.5f) {   // a finger / the thumb, root (0.5) to tip (1)
      float f = (v - 0.5f) * 2;
      shade *= glm::mix(0.62f, 1.0f, glm::smoothstep(0.0f, 0.12f, f));   // the root, between the fingers
      crease = line(f, 0.44f, 0.012f) * 0.7f + line(f, 0.73f, 0.010f) * 0.55f;
      float side = std::min({std::fabs(u), std::fabs(u - 0.5f), std::fabs(u - 1)});   // u 0 / 0.5: the sides facing the next finger
      shade *= 1.0f - 0.22f * std::exp(-std::pow(side / 0.07f, 2.0f));
    } else if (v > 0.15f) {
      crease = line(v, 0.44f, 0.006f) * 0.5f;   // the knuckle line at the palm's far end
    }
    shade *= 1.0f - 0.35f * crease;
    vec3 col = skin * shade * vec3(1.0f + 0.07f * (n2 - 0.5f), 1.0f - 0.03f * (n2 - 0.5f), 1.0f - 0.04f * (n2 - 0.5f));
    size_t o = ((size_t)y * S + x) * 4;
    for (int q = 0; q < 3; ++q) alb[o + q] = (uint8_t)std::clamp(std::lround(std::pow(std::clamp(col[q], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f), 0L, 255L);
    alb[o + 3] = 255;
    hgt[(size_t)y * S + x] = n1 * 0.15f - crease;
  }
  for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
    auto H = [&](int xx, int yy) { return hgt[(size_t)std::clamp(yy, 0, S - 1) * S + (xx + S) % S]; };
    vec3 n = glm::normalize(vec3(-(H(x + 1, y) - H(x - 1, y)) * 1.2f, -(H(x, y + 1) - H(x, y - 1)) * 1.2f, 1.0f));
    size_t o = ((size_t)y * S + x) * 4;
    nrm[o] = (uint8_t)std::lround((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)std::lround((n.y * 0.5f + 0.5f) * 255); nrm[o + 2] = (uint8_t)std::lround((n.z * 0.5f + 0.5f) * 255); nrm[o + 3] = 255;
  }
  m.alb = upload(alb, S, S, true); m.n = upload(nrm, S, S, false); m.mr = 0;
}

// ---------------------------------------------------------------- the arm's shape
struct Pose {
  mat4 Mv{1};               // weapon -> camera
  vec3 anchor{0};           // weapon space: the bottom of the held section (the fore-end, a handle)
  vec2 sec{0.022f, 0.022f}; // the held section's half width, half height (an ellipse round the weapon axis)
  float curl = 1;           // 1 wrapped round it, 0 relaxed
  bool right = false;       // mirrored: a right hand (knife / grenade), its sleeve from the lower right
  vec2 tanHalf{1.026f, 0.577f};   // the view's half extent per metre of depth (x, y): the sleeve aims at its lower corner
};
struct ArmOut { vec3 wrist{0}, sleeve15{0}; size_t handV0 = 0; };
float plen(const std::vector<vec3>& P) { float s = 0; for (size_t i = 1; i < P.size(); ++i) s += glm::length(P[i] - P[i - 1]); return std::max(s, 1e-4f); }
// builds all pieces into B in camera space. Real sizes: palm 8.5 cm wide, about 9 cm wrist crease to knuckles, fingers
// 6-8 cm and 1.5-1.8 cm thick (so they show gaps), thumb 6.5 cm; overall about 18 cm wrist to middle fingertip
void buildArm(Builder& B, const Pose& p, ArmOut& o) {
  const glm::mat3 R3(p.Mv);
  auto W = [&](vec3 w) { return vec3(p.Mv * vec4(w, 1)); };
  const float m = p.right ? -1.0f : 1.0f;   // weapon +x is the shooter's left
  const float rx = p.sec.x, ry = p.sec.y;
  const vec3 C = p.anchor + vec3(0, ry, 0);
  // a point d metres off the section's surface at angle deg round it (0 the near side, -90 the bottom, +-180 the far
  // side, 90 the top), z metres along the weapon axis from the grip
  auto S = [&](float deg, float d, float z) { float a = radians(deg); return W(C + vec3(m * (rx + d) * std::cos(a), (ry + d) * std::sin(a), z)); };
  const vec3 Ac = glm::normalize(R3 * vec3(0, 0, 1));
  // the heel of the hand, and the forearm aimed on screen from it toward the lower left (lower right for the mirrored
  // hand), a little nearer the eye, so the sleeve always enters from the lower left: about 30 degrees below the
  // horizontal, raised (continuously: a bisection on the slant) as far as needed to keep the cuff and 15 cm of sleeve
  // in view (to about 13 degrees)
  vec3 heelC = S(-18, 0.021f, -0.004f);
  vec2 hN(heelC.x / (-heelC.z * p.tanHalf.x), heelC.y / (-heelC.z * p.tanHalf.y));
  float kx = 1.4f * std::clamp((hN.x * m + 0.75f) / 0.65f, 0.35f, 1.0f);   // a hand already toward the left edge: the sleeve runs steeper, not off the side
  auto aimAt = [&](float k) { return vec3((hN.x - kx * m) * p.tanHalf.x, (hN.y - k) * p.tanHalf.y, -1.0f) * (-heelC.z * 0.85f); };
  auto s15y = [&](float k) { vec3 q = heelC + glm::normalize(aimAt(k) - heelC) * 0.215f; return q.y / (std::max(1e-3f, -q.z) * p.tanHalf.y); };
  float kLo = 0.55f, kHi = 1.4f;
  if (s15y(kHi) < -0.9f) { for (int it = 0; it < 10; ++it) { float k = 0.5f * (kLo + kHi); (s15y(k) < -0.9f ? kHi : kLo) = k; } } else kLo = kHi;
  vec3 aim = aimAt(kLo), dC = glm::normalize(aim - heelC);
  float sleeveLen = std::max(0.25f, glm::length(aim - heelC) + 0.08f);   // ends well outside the view, never at the eye
  o.wrist = heelC;
  // ---- sleeve (parts 0 body, 1 cuff): cuff edge 2.5 cm up the wrist, cuff 4 cm, then the body off the view
  vec3 up = vec3(0, 1, 0);
  vec3 cuffEdge = heelC + dC * 0.025f, cuffStart = cuffEdge + dC * 0.04f, Send = heelC + dC * sleeveLen;
  o.sleeve15 = cuffStart + dC * 0.15f;
  {   // body: tapered, 0.044 m at the cuff to 0.053 m off-screen
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 12; ++i) { float t = i / 12.0f; P.push_back(glm::mix(cuffStart, Send, t)); float rr = 0.044f + 0.009f * t; Rr.push_back(vec2(rr, rr * 0.94f)); }
    B.sweep(P, Rr, up, 20, 1.0f, 0.04f / kTexLen, kTexLen);
  }
  if (B.idx) { gA.partCount[0] = (uint32_t)B.I.size(); gA.partFirst[1] = (uint32_t)B.I.size(); }
  {   // cuff: a slightly fuller band, v from its edge
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 3; ++i) { float t = i / 3.0f; P.push_back(glm::mix(cuffEdge, cuffStart, t)); float rr = 0.0465f - 0.0025f * t; Rr.push_back(vec2(rr, rr * 0.94f)); }
    B.sweep(P, Rr, up, 20, 1.0f, 0.0f, kTexLen);
  }
  if (B.idx) { gA.partCount[1] = (uint32_t)B.I.size() - gA.partFirst[1]; gA.partFirst[2] = (uint32_t)B.I.size(); }
  // ---- skin (part 2): wrist, palm, fingers, thumb; every end capped. Cross-sections keep the weapon axis as their
  // reference (wide along it for the palm and fingers), so nothing rolls
  {   // wrist: from inside the cuff to the heel of the hand (v 0-0.15)
    vec3 w0 = heelC + dC * 0.065f;
    std::vector<vec3> P = {w0, glm::mix(w0, heelC, 0.5f), heelC}; std::vector<vec2> Rr = {{0.027f, 0.020f}, {0.027f, 0.019f}, {0.030f, 0.017f}};
    B.sweep(P, Rr, Ac, 12, 1, 0, plen(P) / 0.15f, false, true);
  }
  o.handV0 = B.V.size();   // the hand proper (the framing check): palm, fingers, thumb
  {   // palm (v 0.15-0.45): from the heel on the near side round the bottom to the knuckle line at the far-bottom edge,
      // 8.5 cm wide along the weapon axis, 2.5 cm thick at the heel to 2 cm at the knuckles
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 6; ++i) {
      float t = i / 6.0f;
      P.push_back(S(glm::mix(-18.0f, -146.0f, t), glm::mix(0.021f, 0.0105f, glm::smoothstep(0.0f, 0.6f, t)), -0.004f + 0.004f * t));
      Rr.push_back(vec2(glm::mix(0.037f, 0.0425f, t), glm::mix(0.0125f, 0.0102f, t)));
    }
    B.sweep(P, Rr, Ac, 14, 1, 0.15f, plen(P) / 0.30f, true, true);
  }
  // fingers (v 0.5-1): index (toward the muzzle) to little finger, from the knuckle line under the far-bottom edge up the
  // far side, the tips curling over the top-far edge (stopped at 112 degrees, over the top). Relaxed: shorter span, lifted.
  const float fz[4] = {0.0310f, 0.0105f, -0.0105f, -0.0300f}, fl[4] = {0.074f, 0.081f, 0.076f, 0.060f}, fw[4] = {0.0087f, 0.0091f, 0.0086f, 0.0075f}, splay[4] = {0.004f, 0.0012f, -0.0012f, -0.004f};
  for (int f = 0; f < 4; ++f) {
    // march round the section at the finger's own offset until its length is used (or the top-far stop is reached)
    float want = fl[f] * glm::mix(0.55f, 1.0f, p.curl), arc = 0, ph0 = -136.0f, ph1 = ph0;
    auto at = [&](float deg) { float a = radians(deg); return vec2((rx + fw[f]) * std::cos(a), (ry + fw[f]) * std::sin(a)); };
    while (arc < want && ph1 > -248.0f) { arc += glm::length(at(ph1 - 1) - at(ph1)); ph1 -= 1; }
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 7; ++i) {
      float t = i / 7.0f;
      P.push_back(S(glm::mix(ph0, ph1, t), fw[f] + (1 - p.curl) * 0.03f * t * t, fz[f] + splay[f] * t));
      float rr = glm::mix(fw[f], fw[f] * 0.80f, t);
      Rr.push_back(vec2(rr, rr * 0.86f));
    }
    B.sweep(P, Rr, Ac, 8, 1, 0.5f, plen(P) / 0.5f, true, true);
  }
  {   // thumb (v 0.5-1): short, from the heel forward along the near side, lying on it a little below mid height
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 6; ++i) {
      float t = i / 6.0f;
      P.push_back(S(glm::mix(-36.0f, glm::mix(-24.0f, -12.0f, p.curl), t), glm::mix(0.015f, 0.0088f, t) + (1 - p.curl) * 0.012f * t, glm::mix(0.006f, 0.054f, t)));
      float tr = glm::mix(0.0108f, 0.0082f, t);
      Rr.push_back(vec2(tr * 0.88f, tr));
    }
    B.sweep(P, Rr, glm::normalize(R3 * vec3(m * std::cos(radians(-10.0f)), std::sin(radians(-10.0f)), 0)), 8, 1, 0.5f, plen(P) / 0.5f, true, true);
  }
  if (B.idx) gA.partCount[2] = (uint32_t)B.I.size() - gA.partFirst[2];
}

bool armsLoad() {
  if (gA.tried) return gA.ok;
  gA.tried = true;
  try { gA.cfg = json::parse(readTextFile(contentDir() / "settings" / "fps_arms.json")); } catch (...) { gA.cfg = json::object(); }
  Img wc = loadImg(contentDir() / "textures" / "wool_woven" / "color.jpg"), wn = loadImg(contentDir() / "textures" / "wool_woven" / "normal.jpg");
  if (wc.px.empty() || wn.px.empty()) {   // no cloth set: flat 2x2 stand-ins (the stitches still paint)
    wc.w = wc.h = wn.w = wn.h = 2; wc.px.assign(16, 128); wn.px.assign(16, 128); for (int k = 0; k < 4; ++k) wn.px[k * 4 + 2] = 255;
  }
  Builder B; Pose p; ArmOut w;
  buildArm(B, p, w);
  gA.I = B.I; gA.nV = B.V.size(); gA.partFirst[0] = 0;
  const json& col = gA.cfg.value("sleeve", json::object());
  vec3 cloth[2] = {jv3(col, "allies", vec3(0.105f, 0.100f, 0.048f)), jv3(col, "axis", vec3(0.030f, 0.030f, 0.032f))};          // linear: olive drab, charcoal
  vec3 thread[2] = {jv3(col, "alliesThread", vec3(0.42f, 0.38f, 0.22f)), jv3(col, "axisThread", vec3(0.24f, 0.24f, 0.25f))};   // a clear step lighter than the cloth
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
    sleeveTex(wc, wn, cloth[f], thread[f], false, body); sleeveTex(wc, wn, cloth[f], thread[f], true, cuff);
    GpuModel::MatTex hand; hand.name = "skin"; skinTex(skin, hand);
    g.mats = {body, cuff, hand}; g.texAlb = body.alb; g.texN = body.n; g.textureCount = 4;
    const char* names[3] = {"sleeve", "cuff", "hand"};
    for (int k = 0; k < 3; ++k) {
      ModelPart pt; pt.name = names[k]; pt.role = "arm"; pt.first = gA.partFirst[k]; pt.count = gA.partCount[k];
      pt.metal = 0; pt.rough = k < 2 ? 0.9f : 0.82f; pt.textured = true; pt.mat = k;
      pt.base = vec4(1);
      g.parts.push_back(pt);
    }
    g.tris = (int)gA.I.size() / 3;
  }
  logf("fps arms: generated support arm (%zu verts, %zu tris; sleeve, cuff, hand)", gA.nV, gA.I.size() / 3);
  return gA.ok = true;
}

// The weapon's cross-section at its support socket, from its baked mesh (weapon space; as FA1): the bottom of the
// fore-end under the socket, centred, and the section's width
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
}   // namespace

bool fpsArmsReady() { return armsLoad(); }
bool fpsArmsIsModel(const GpuModel* g) { return g && (g == &gA.model[0] || g == &gA.model[1]); }   // self-tests: an arm draw
int fpsArmsFaction(const GpuModel* g) { return g == &gA.model[0] ? 0 : g == &gA.model[1] ? 1 : -1; }   // 0 Allies, 1 Axis
// the support grip of a weapon (weapon space): the bottom of the held section and its half width / half height
// (fps_arms.json "weapons": {"<id>": {"gripZ": z}} measures the fore-end further along; "grip": [x, y, z] and
// "section": [half width, half height] win over the measurement; the socket when unmeasured)
void fpsArmsGrip(const WeaponDef& d, vec3& grip, vec2& section) {
  armsLoad();
  const GripGeo& g = gripGeo(d);
  float hw = std::clamp(g.wL * 0.5f, 0.012f, 0.045f);
  grip = g.gL; section = vec2(hw, std::clamp(g.hL * 0.5f, hw, 0.04f));
  const json& w = gA.cfg.value("weapons", json::object());
  if (w.contains(d.id)) { grip = jv3(w[d.id], "grip", grip); if (w[d.id].contains("section")) section = vec2(w[d.id]["section"][0].get<float>(), w[d.id]["section"][1].get<float>()); }
}
void fpsArmsLastFraming(vec2& handLo, vec2& handHi, vec2& sleeve15) { handLo = gA.handLo; handHi = gA.handHi; sleeve15 = gA.sleeve15; }

// Draws the support arm holding a section of radius `radius` whose bottom is at `grip` (weapon space; the section's
// axis is weapon +z), for the weapon-to-camera transform weaponToCam; cam = camera-to-world. curl 1 = wrapped round
// it, 0 relaxed. rightHand: the mirrored hand, its sleeve from the lower right (knife / grenades). Any weapon may call
// this (a flamethrower's tube: its support grip and tube radius). Out: the wrist (camera space). False: not drawn
// (no generated arm, or the safety hid it: in the ADS sight cone / at the eye).
bool fpsSupportHandDraw(App& app, const mat4& cam, const mat4& weaponToCam, vec3 grip, vec2 section, float curl, bool rightHand, bool axis, vec3* wristOut) {
  if (!armsLoad()) return false;
  float ads = app.adsT * app.adsT * (3 - 2 * app.adsT);
  const float cone = std::tan(radians(gA.cfg.value("sightConeDeg", 6.0f))), nearZ = gA.cfg.value("nearClear", 0.12f), nearAds = gA.cfg.value("nearClearAds", 0.25f);
  Pose p; p.Mv = weaponToCam; p.anchor = grip; p.sec = section; p.curl = std::clamp(curl, 0.0f, 1.0f); p.right = rightHand;
  float ty = std::tan(radians(app.vmFovNow()) * 0.5f); p.tanHalf = vec2(ty * app.R.width / std::max(1, app.R.height), ty);
  Builder B; B.idx = false; ArmOut wrist;
  int inCone = 0, tooNear = 0;
  for (int it = 0; it < 7; ++it) {
    B.V.clear(); buildArm(B, p, wrist);
    inCone = tooNear = 0;
    for (size_t v = 0; v < B.V.size(); v += 3) {
      vec3 q = B.V[v].pos; float fwd = -q.z, rr = glm::length(vec2(q.x, q.y));
      if (fwd <= 0.005f) continue;
      if (ads > 0.3f && rr < fwd * cone) ++inCone;
      if (fwd < (ads > 0.3f ? nearAds : nearZ) && rr < fwd * 0.5f) ++tooNear;   // a sleeve end-on / a wrist at the cheek
    }
    if (inCone == 0) break;
    p.anchor.y -= 0.012f;   // the hand drops below the sight line
  }
  if (wristOut) *wristOut = wrist.wrist;
  auto ndc = [&](vec3 q) { float z = std::max(1e-3f, -q.z); return vec2(q.x / (z * p.tanHalf.x), q.y / (z * p.tanHalf.y)); };
  gA.handLo = vec2(1e9f); gA.handHi = vec2(-1e9f);
  for (size_t v = wrist.handV0; v < B.V.size(); ++v) { vec2 c = ndc(B.V[v].pos); gA.handLo = glm::min(gA.handLo, c); gA.handHi = glm::max(gA.handHi, c); }
  gA.sleeve15 = ndc(wrist.sleeve15);
  if (inCone > 0 || tooNear > 0 || B.V.size() != gA.nV) return false;
  GpuModel& g = gA.model[axis ? 1 : 0];
  glNamedBufferSubData(g.vbo, 0, B.V.size() * sizeof(TexVertex), B.V.data());
  for (int k = 0; k < 3; ++k) app.R.viewModelTex.push_back({&g, k, cam});
  return true;
}
