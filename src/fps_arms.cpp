// FA2 first-person support arm (docs/fps-arms.md): ONE generated arm, the support hand under the weapon's fore-end
// and a plain cloth sleeve entering from the lower left, for every weapon (knife / grenades: the one hand on the item).
//  * Geometry is generated every frame (fixed topology, one dynamic vertex buffer per faction): every piece is a swept
//    elliptical tube whose cross-section keeps a fixed reference axis, so nothing rolls or twists - the sleeve is a
//    straight, slightly tapered tube with a cuff; the hand (palm, four curled fingers, thumb) only turns at the wrist.
//  * The hand wraps the fore-end measured on the weapon's own mesh (the cross-section at its support socket, as FA1):
//    palm under the fore-end, knuckle line along the barrel, fingers curled under and up the far side, thumb along the
//    near side. content/settings/fps_arms.json can override a weapon's grip point / radius.
//  * Sleeve material: the project's woven wool texture (wool_woven) recoloured per faction (olive drab / near-black
//    charcoal), a stitched seam along the sleeve and a stitched hem round the cuff painted into its albedo and normal
//    map; matte (roughness 0.9, metallic 0). Hand: skin, matte, no texture.
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
  json cfg = json::object();
};
ArmGen gA;

vec3 jv3(const json& j, const char* k, vec3 d) { return j.contains(k) && j[k].is_array() && j[k].size() == 3 ? vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()) : d; }

// ---------------------------------------------------------------- mesh builder (fixed topology)
struct Builder {
  std::vector<TexVertex> V; std::vector<uint32_t> I; bool idx = true;
  // a tube through P (rings of N + 1 vertices, the seam duplicated for the uv), elliptical cross-section rx along the
  // reference axis ref (projected off the tangent: no roll), ry across; uv: u = round the tube x uRep, v = length / vLen
  void sweep(const std::vector<vec3>& P, const std::vector<vec2>& R, vec3 ref, int N, float uRep, float v0, float vLen) {
    uint32_t base = (uint32_t)V.size();
    float s = 0;
    for (size_t i = 0; i < P.size(); ++i) {
      vec3 t = glm::normalize(P[std::min(i + 1, P.size() - 1)] - P[i > 0 ? i - 1 : 0]);
      vec3 e1 = ref - t * glm::dot(ref, t);
      if (glm::dot(e1, e1) < 1e-8f) e1 = glm::cross(t, std::fabs(t.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0));
      e1 = glm::normalize(e1); vec3 e2 = glm::cross(t, e1);
      if (i > 0) s += glm::length(P[i] - P[i - 1]);
      for (int k = 0; k <= N; ++k) {
        float a = 2 * kPi * k / N, c = std::cos(a), sn = std::sin(a);
        vec3 p = P[i] + e1 * (c * R[i].x) + e2 * (sn * R[i].y);
        vec3 n = glm::normalize(e1 * (c / std::max(R[i].x, 1e-4f)) + e2 * (sn / std::max(R[i].y, 1e-4f)));
        vec3 tu = glm::normalize(-e1 * (sn * R[i].x) + e2 * (c * R[i].y));
        float w = glm::dot(glm::cross(n, tu), t) > 0 ? 1.0f : -1.0f;
        V.push_back({p, n, vec4(tu, w), vec2(uRep * k / N, v0 + s / vLen)});
      }
    }
    if (!idx) return;
    for (size_t i = 0; i + 1 < P.size(); ++i)
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
    float inA = 1.0f - glm::smoothstep(len * 0.5f - 0.004f, len * 0.5f, std::fabs(f - 0.5f) * pitch);
    float inW = 1.0f - glm::smoothstep(width * 0.5f - 0.0006f, width * 0.5f + 0.0004f, std::fabs(across));
    return inA * inW;
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
    // the seam: a darker fold, a stitch row 4 mm either side (stitch 3 mm, pitch 5 mm, thread 1.2 mm)
    fold = 1.0f - glm::smoothstep(0.0005f, 0.0016f, std::fabs(mu));
    for (float off : {-0.004f, 0.004f}) st = std::max(st, stitch(mv, mu - off, 0.005f, 0.003f, 0.0012f));
    if (cuff) {   // the hem: two rows round the cuff, 6 and 13 mm from its edge
      float mvE = v * kTexLen;   // the cuff part starts its v at the edge
      for (float row : {0.006f, 0.013f}) st = std::max(st, stitch(u * kTexLen, mvE - row, 0.005f, 0.003f, 0.0012f));
      fold = std::max(fold, 1.0f - glm::smoothstep(0.0006f, 0.0018f, std::fabs(mvE - 0.0185f)));   // the turned-up hem's fold
    }
    h = st * 0.8f - fold * 0.6f;
    vec3 col = cloth * L * (1.0f - 0.35f * fold);
    col = glm::mix(col, thread * (0.85f + 0.3f * L), st * 0.9f);
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
    n = glm::normalize(vec3(n.x - dx * 1.5f, n.y - dy * 1.5f, std::max(0.2f, n.z)));
    size_t o = ((size_t)y * S + x) * 4;
    nrm[o] = (uint8_t)std::lround((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)std::lround((n.y * 0.5f + 0.5f) * 255); nrm[o + 2] = (uint8_t)std::lround((n.z * 0.5f + 0.5f) * 255); nrm[o + 3] = 255;
  }
  m.alb = upload(alb, S, S, true); m.n = upload(nrm, S, S, false); m.mr = 0;
}

// ---------------------------------------------------------------- the arm's shape
struct Pose {
  mat4 Mv{1};          // weapon -> camera
  vec3 anchor{0};      // weapon space: the bottom of the held section (the fore-end, a handle)
  float r = 0.022f;    // the held section's radius
  float curl = 1;      // 1 wrapped round it, 0 relaxed
  bool right = false;  // mirrored: a right hand (knife / grenade), its sleeve from the lower right
};
// builds all pieces into B in camera space; out: the wrist point (camera space)
void buildArm(Builder& B, const Pose& p, vec3& wristOut) {
  const glm::mat3 R3(p.Mv);
  auto W = [&](vec3 w) { return vec3(p.Mv * vec4(w, 1)); };
  const float m = p.right ? -1.0f : 1.0f;   // weapon +x is the shooter's left
  const vec3 A(0, 0, 1);                     // the barrel / handle axis (weapon space)
  const float r = p.r;
  vec3 C = p.anchor + vec3(0, r, 0);
  auto rad = [&](float deg) { float a = radians(deg); return vec3(m * std::cos(a), std::sin(a), 0); };
  // wrist and sleeve direction: back, down and out from the hand's heel (camera space, blended toward the lower
  // left / right of the view so the sleeve always enters from the bottom corner)
  vec3 dirW = glm::normalize(vec3(m * 0.45f, -0.72f, -0.55f));
  vec3 heel = C + rad(-12) * (r + 0.036f) + A * (-0.008f);
  vec3 dC = glm::normalize(glm::mix(glm::normalize(R3 * dirW), glm::normalize(vec3(-0.32f * m, -0.85f, 0.42f)), 0.55f));
  vec3 heelC = W(heel), wristEnd = heelC + dC * 0.052f;
  wristOut = heelC;
  // ---- sleeve (parts 0 body, 1 cuff): cuff edge 3.5 cm up the wrist, cuff 4 cm, then the body off the bottom of the view
  vec3 up = vec3(0, 1, 0);
  vec3 cuffEdge = heelC + dC * 0.035f, cuffStart = cuffEdge + dC * 0.04f, S = cuffStart + dC * 0.55f;
  {   // body: tapered, 0.044 m at the cuff to 0.053 m off-screen
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 12; ++i) { float t = i / 12.0f; P.push_back(glm::mix(cuffStart, S, t)); float rr = 0.044f + 0.009f * t; Rr.push_back(vec2(rr, rr * 0.94f)); }
    B.sweep(P, Rr, up, 20, 1.0f, 0.04f / kTexLen, kTexLen);
  }
  if (B.idx) { gA.partCount[0] = (uint32_t)B.I.size(); gA.partFirst[1] = (uint32_t)B.I.size(); }
  {   // cuff: a slightly fuller band, v from its edge
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 3; ++i) { float t = i / 3.0f; P.push_back(glm::mix(cuffEdge, cuffStart, t)); float rr = 0.0465f - 0.0025f * t; Rr.push_back(vec2(rr, rr * 0.94f)); }
    B.sweep(P, Rr, up, 20, 1.0f, 0.0f, kTexLen);
  }
  if (B.idx) { gA.partCount[1] = (uint32_t)B.I.size() - gA.partFirst[1]; gA.partFirst[2] = (uint32_t)B.I.size(); }
  // ---- skin (part 2): wrist, palm, fingers, thumb; reference axes are the weapon axis / the radial direction (no roll)
  vec3 Ac = glm::normalize(R3 * A);
  {   // wrist: from inside the sleeve to the heel of the hand
    std::vector<vec3> P = {wristEnd, glm::mix(wristEnd, heelC, 0.5f), heelC}; std::vector<vec2> Rr = {{0.029f, 0.023f}, {0.028f, 0.022f}, {0.030f, 0.020f}};
    B.sweep(P, Rr, Ac, 12, 1, 0, 1);
  }
  const float z0 = p.anchor.z;
  {   // palm: a flat pad round the bottom of the section, heel lifted off it
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 6; ++i) {   // round the near-lower side (the camera sees it below the fore-end's near edge)
      float t = i / 6.0f, ph = glm::mix(-14.0f, -98.0f, t);
      float lift = 0.024f * (1 - glm::smoothstep(0.0f, 0.5f, t));
      P.push_back(W(vec3(C.x, C.y, z0) + rad(ph) * (r + 0.017f + lift)));
      Rr.push_back(vec2(0.043f - 0.005f * t, glm::mix(0.022f, 0.016f, t)));   // the thumb-side heel is the fuller part
    }
    B.sweep(P, Rr, Ac, 12, 1, 0, 1);
  }
  // fingers: index (front, toward the muzzle) to little finger, curled under and up the far side
  const float fz[4] = {0.030f, 0.010f, -0.010f, -0.029f}, fl[4] = {0.078f, 0.085f, 0.080f, 0.064f}, fw[4] = {0.0105f, 0.0110f, 0.0104f, 0.0092f};
  for (int f = 0; f < 4; ++f) {
    std::vector<vec3> P; std::vector<vec2> Rr;
    float rf = r + fw[f], span = std::min(fl[f] / rf, radians(132.0f)) * glm::mix(0.35f, 1.0f, p.curl);
    for (int i = 0; i <= 7; ++i) {
      float t = i / 7.0f, ph = -94.0f - degrees(span) * t;
      float open = (1 - p.curl) * 0.03f * t;   // relaxed: the fingers lift off the section
      P.push_back(W(vec3(C.x, C.y, z0 + fz[f] + 0.006f * t * (f < 2 ? 1.0f : -1.0f)) + rad(ph) * (rf + open)));
      float rr = glm::mix(fw[f], fw[f] * 0.72f, t) * (i == 7 ? 0.45f : 1.0f);
      Rr.push_back(vec2(rr, rr * 0.9f));
    }
    B.sweep(P, Rr, Ac, 8, 1, 0, 1);
  }
  {   // thumb: from the side of the palm forward along the near side of the section
    std::vector<vec3> P; std::vector<vec2> Rr;
    for (int i = 0; i <= 6; ++i) {
      float t = i / 6.0f, ph = glm::mix(-20.0f, glm::mix(8.0f, 34.0f, p.curl), t), rr = r + glm::mix(0.034f, 0.0135f, t);
      P.push_back(W(vec3(C.x, C.y, z0 + glm::mix(-0.032f, 0.052f, t)) + rad(ph) * rr));
      float tr = glm::mix(0.0175f, 0.0110f, t) * (i == 6 ? 0.55f : 1.0f);
      Rr.push_back(vec2(tr, tr * 0.88f));
    }
    vec3 ref = glm::normalize(R3 * rad(-10));
    B.sweep(P, Rr, ref, 8, 1, 0, 1);
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
  Builder B; Pose p; vec3 w;
  buildArm(B, p, w);
  gA.I = B.I; gA.nV = B.V.size(); gA.partFirst[0] = 0;
  const json& col = gA.cfg.value("sleeve", json::object());
  vec3 cloth[2] = {jv3(col, "allies", vec3(0.105f, 0.100f, 0.048f)), jv3(col, "axis", vec3(0.030f, 0.030f, 0.032f))};          // linear: olive drab, charcoal
  vec3 thread[2] = {jv3(col, "alliesThread", vec3(0.24f, 0.22f, 0.12f)), jv3(col, "axisThread", vec3(0.10f, 0.10f, 0.105f))};   // a little lighter than the cloth
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
    g.mats = {body, cuff}; g.texAlb = body.alb; g.texN = body.n; g.textureCount = 4;
    const char* names[3] = {"sleeve", "cuff", "hand"};
    for (int k = 0; k < 3; ++k) {
      ModelPart pt; pt.name = names[k]; pt.role = "arm"; pt.first = gA.partFirst[k]; pt.count = gA.partCount[k];
      pt.metal = 0; pt.rough = k < 2 ? 0.9f : 0.82f; pt.textured = k < 2; pt.mat = k < 2 ? k : -1;
      pt.base = k < 2 ? vec4(1) : vec4(skin, 1);
      g.parts.push_back(pt);
    }
    g.tris = (int)gA.I.size() / 3;
  }
  logf("fps arms: generated support arm (%zu verts, %zu tris; sleeve, cuff, hand)", gA.nV, gA.I.size() / 3);
  return gA.ok = true;
}

// The weapon's cross-section at its support socket, from its baked mesh (weapon space; as FA1): the bottom of the
// fore-end under the socket, centred, and the section's width
struct GripGeo { bool ok = false; vec3 gL{0}; float wL = 0.044f; };
const GripGeo& gripGeo(const WeaponDef& d) {
  static std::map<std::string, GripGeo> cache;
  auto it = cache.find(d.id);
  if (it != cache.end()) return it->second;
  GripGeo g; g.gL = d.gripL;
  try {
    json j = json::parse(readTextFile(d.file));
    std::string dir = j.contains("model") ? j["model"].value("dir", std::string()) : std::string();
    std::ifstream f(contentDir() / "weapons" / "models" / dir / "mesh.bin", std::ios::binary);
    uint32_t hdr[3] = {0, 0, 0};
    if (!dir.empty() && f.read((char*)hdr, sizeof hdr) && hdr[0] == 0x31574D4Eu) {
      std::vector<TexVertex> V(hdr[1]); std::vector<uint32_t> I(hdr[2]);
      f.read((char*)V.data(), V.size() * sizeof(TexVertex)); f.read((char*)I.data(), I.size() * 4);
      vec3 s = d.gripL, lo(1e9f), hi(-1e9f); int n = 0;
      for (size_t t = 0; f && t + 2 < I.size(); t += 3) {   // every triangle edge cut by the plane z = s.z
        vec3 q[3] = {V[I[t]].pos, V[I[t + 1]].pos, V[I[t + 2]].pos};
        for (int e = 0; e < 3; ++e) {
          vec3 a = q[e], b = q[(e + 1) % 3];
          if ((a.z - s.z) * (b.z - s.z) > 0 || a.z == b.z) continue;
          vec3 p = glm::mix(a, b, (s.z - a.z) / (b.z - a.z));
          if (std::fabs(p.x - s.x) < 0.07f && p.y < s.y + 0.04f && p.y > s.y - 0.06f) { lo = glm::min(lo, p); hi = glm::max(hi, p); ++n; }
        }
      }
      if (n > 4) { g.gL = vec3((lo.x + hi.x) * 0.5f, lo.y, s.z); g.wL = hi.x - lo.x; g.ok = true; }
    }
  } catch (...) {}
  logf("fps arms: %s support grip (%.3f, %.3f, %.3f) section width %.3f%s", d.id.c_str(), g.gL.x, g.gL.y, g.gL.z, g.wL, g.ok ? "" : " (socket: no mesh)");
  return cache.emplace(d.id, g).first->second;
}
}   // namespace

bool fpsArmsReady() { return armsLoad(); }
bool fpsArmsIsModel(const GpuModel* g) { return g && (g == &gA.model[0] || g == &gA.model[1]); }   // self-tests: an arm draw
int fpsArmsFaction(const GpuModel* g) { return g == &gA.model[0] ? 0 : g == &gA.model[1] ? 1 : -1; }   // 0 Allies, 1 Axis
// the support grip of a weapon (weapon space): the bottom of the held section and its radius (fps_arms.json
// "weapons": {"<id>": {"grip": [x, y, z], "radius": r}} wins over the measured fore-end; the socket when unmeasured)
void fpsArmsGrip(const WeaponDef& d, vec3& grip, float& radius) {
  armsLoad();
  const GripGeo& g = gripGeo(d);
  grip = g.gL; radius = std::clamp(g.wL * 0.5f, 0.012f, 0.045f);
  const json& w = gA.cfg.value("weapons", json::object());
  if (w.contains(d.id)) { grip = jv3(w[d.id], "grip", grip); radius = w[d.id].value("radius", radius); }
}

// Draws the support arm holding a section of radius `radius` whose bottom is at `grip` (weapon space; the section's
// axis is weapon +z), for the weapon-to-camera transform weaponToCam; cam = camera-to-world. curl 1 = wrapped round
// it, 0 relaxed. rightHand: the mirrored hand, its sleeve from the lower right (knife / grenades). Any weapon may call
// this (a flamethrower's tube: its support grip and tube radius). Out: the wrist (camera space). False: not drawn
// (no generated arm, or the safety hid it: in the ADS sight cone / at the eye).
bool fpsSupportHandDraw(App& app, const mat4& cam, const mat4& weaponToCam, vec3 grip, float radius, float curl, bool rightHand, bool axis, vec3* wristOut) {
  if (!armsLoad()) return false;
  float ads = app.adsT * app.adsT * (3 - 2 * app.adsT);
  const float cone = std::tan(radians(gA.cfg.value("sightConeDeg", 6.0f))), nearZ = gA.cfg.value("nearClear", 0.12f), nearAds = gA.cfg.value("nearClearAds", 0.25f);
  Pose p; p.Mv = weaponToCam; p.anchor = grip; p.r = radius; p.curl = std::clamp(curl, 0.0f, 1.0f); p.right = rightHand;
  Builder B; B.idx = false; vec3 wrist;
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
  if (wristOut) *wristOut = wrist;
  if (inCone > 0 || tooNear > 0 || B.V.size() != gA.nV) return false;
  GpuModel& g = gA.model[axis ? 1 : 0];
  glNamedBufferSubData(g.vbo, 0, B.V.size() * sizeof(TexVertex), B.V.data());
  for (int k = 0; k < 3; ++k) app.R.viewModelTex.push_back({&g, k, cam});
  return true;
}
