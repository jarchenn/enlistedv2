// CP0 look-dev props (corpus plan step 0): grey (albedo 0.18), white (0.90) and chrome spheres, a 6 x 4 colour chart and
// five horizontal grey cards in a row, set in front of an eye, raised 0.5 m clear of the grass. Drawn with the opaque
// list (Renderer::lookdev); the selftest (--selftest-lookdev) reads the cards; NMK_LOOKDEV_PROPS=1 sets them in play.
#include "render.h"
#include <cmath>
#include <functional>

namespace {
GpuMesh gSph[3], gChart, gCard;
SurfaceParams gSurf;   // wear 0, no palette variation: the albedo as authored
float lin(float s) { s /= 255.0f; return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); }
Vertex vert(vec3 p, vec3 n, vec3 alb, float rough, float metal) { Vertex v{}; v.pos = p; v.nrm = n; v.albedo = alb; v.params = vec4(0, rough, metal, 0); v.pcoord = p; v.pnrm = n; return v; }
void quad(MeshData& m, vec3 c, vec3 u, vec3 w, vec3 alb) {   // c centre, u / w half extents; normal u x w
  vec3 n = glm::normalize(glm::cross(u, w)); uint32_t b = (uint32_t)m.v.size();
  for (vec2 s : {vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1)}) m.v.push_back(vert(c + u * s.x + w * s.y, n, alb, 0.9f, 0.0f));
  for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) m.i.push_back(b + k);
}
void make() {
  const vec3 alb[3] = {vec3(0.18f), vec3(0.90f), vec3(0.95f, 0.93f, 0.88f)}; const float rough[3] = {1.0f, 1.0f, 0.05f}, metal[3] = {0, 0, 1};
  for (int s = 0; s < 3; ++s) {   // unit UV sphere
    MeshData m; const int nu = 32, nv = 16;
    for (int j = 0; j <= nv; ++j) for (int i = 0; i <= nu; ++i) {
      float th = 3.14159265f * j / nv, ph = 6.2831853f * i / nu; vec3 n(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
      m.v.push_back(vert(n, n, alb[s], rough[s], metal[s]));
    }
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) { uint32_t a = j * (nu + 1) + i, b = a + nu + 1; for (uint32_t k : {a, a + 1, b, a + 1, b + 1, b}) m.i.push_back(k); }
    gSph[s].upload(m);
  }
  // ColorChecker 24 (BabelColor sRGB), rows top to bottom, in the board's x (right) / y (up), facing +z
  static const unsigned char cc[24][3] = {{115, 82, 68}, {194, 150, 130}, {98, 122, 157}, {87, 108, 67}, {133, 128, 177}, {103, 189, 170},
    {214, 126, 44}, {80, 91, 166}, {193, 90, 99}, {94, 60, 108}, {157, 188, 64}, {224, 163, 46},
    {56, 61, 150}, {70, 148, 73}, {175, 54, 60}, {231, 199, 31}, {187, 86, 149}, {8, 133, 161},
    {243, 243, 242}, {200, 200, 200}, {160, 160, 160}, {122, 122, 121}, {85, 85, 85}, {52, 52, 52}};
  MeshData c; quad(c, vec3(0, 0, -0.005f), vec3(0.34f, 0, 0), vec3(0, 0.23f, 0), vec3(0.02f));   // black board behind
  for (int k = 0; k < 24; ++k) quad(c, vec3(-0.25f + 0.1f * (k % 6), 0.15f - 0.1f * (k / 6), 0), vec3(0.045f, 0, 0), vec3(0, 0.045f, 0), vec3(lin(cc[k][0]), lin(cc[k][1]), lin(cc[k][2])));
  gChart.upload(c);
  MeshData d; quad(d, vec3(0), vec3(0, 0, 1), vec3(1, 0, 0), vec3(0.18f)); gCard.upload(d);   // unit card in xz, normal +y
}
}

// eye, yaw (the game's convention: forward (-sin, 0, -cos)); groundH(x, z); cards: the five card centres (probe points)
void lookdevPlace(Renderer& R, vec3 eye, float yawDeg, const std::function<float(float, float)>& groundH, std::vector<vec3>* cards) {
  static bool made = false; if (!made) { make(); made = true; }
  R.lookdev.clear();
  float y = glm::radians(yawDeg); vec3 f(-std::sin(y), 0, -std::cos(y)), r(std::cos(y), 0, -std::sin(y)), up(0, 1, 0);
  mat4 basis(vec4(r, 0), vec4(up, 0), vec4(-f, 0), vec4(0, 0, 0, 1));   // local +z toward the eye
  auto at = [&](float d, float side, float lift) { vec3 p = eye + f * d + r * side; p.y = groundH(p.x, p.z) + lift; return p; };
  for (int s = 0; s < 3; ++s) { vec3 p = at(3.5f, -0.7f + 0.7f * s, 0.7f); R.lookdev.push_back({&gSph[s], glm::translate(mat4(1), p) * glm::scale(mat4(1), vec3(0.2f)), vec4(1), &gSurf}); }
  R.lookdev.push_back({&gChart, glm::translate(mat4(1), at(3.5f, 1.8f, 0.75f)) * basis, vec4(1), &gSurf});
  if (cards) cards->clear();
  for (int k = 0; k < 5; ++k) {
    vec3 p = at(5.5f, -3.0f + 1.5f * k, 0.6f);
    R.lookdev.push_back({&gCard, glm::translate(mat4(1), p) * basis * glm::scale(mat4(1), vec3(0.3f, 1.0f, 0.4f)), vec4(1), &gSurf});
    if (cards) cards->push_back(p + vec3(0, 0.002f, 0));
  }
  gSurf.inst = vec4(0, 0, 0, 0);
}
