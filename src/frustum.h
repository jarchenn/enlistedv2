#pragma once
// View-frustum culling shared by the renderer's passes (docs/rendering.md#frustum-culling): the main camera, both shadow
// cascades, sky occlusion, the terrain chunk stream, and later spot-shadow views and probe captures (L2).
//
// Planes come from a view-projection in the standard GL clip convention (-w <= x, y, z <= w). Build the main camera's from
// the standard-Z projection, before reverseZ(): the reverse-Z matrix's far plane is the w row alone and is lost.
// Planes are normalized (metres) and kept in double while extracting, so a 0.1 m .. 1e6 m editor projection still yields
// a usable far plane; a plane whose normal collapses (infinite far) is dropped.
//
// Tests are conservative: a box straddling a plane is visible, and an off-screen caster inside a cascade's ortho box is
// kept because that pass tests its own box. Vertex-shader displacement (leaf sway, thin-wire widening) is covered by the
// caller's margin.
#include <glm/glm.hpp>
#include <cmath>

struct Frustum {
  glm::vec4 pl[6]{};   // (n, w): a point p is inside when dot(n, p) + w >= 0; |n| = 1
  int n = 0;           // planes in use (0 = everything visible)

  static Frustum fromVP(const glm::dmat4& m) {
    Frustum f;
    glm::dvec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]), r1(m[0][1], m[1][1], m[2][1], m[3][1]),
               r2(m[0][2], m[1][2], m[2][2], m[3][2]), r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    const glm::dvec4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};   // left right bottom top near far
    for (const auto& p : raw) {
      double l = glm::length(glm::dvec3(p));
      if (!(l > 1e-12) || !std::isfinite(l)) continue;
      f.pl[f.n++] = glm::vec4(p / l);
    }
    return f;
  }
  static Frustum fromVP(const glm::mat4& vp) { return fromVP(glm::dmat4(vp)); }
  static Frustum fromProjView(const glm::mat4& proj, const glm::mat4& view) { return fromVP(glm::dmat4(proj) * glm::dmat4(view)); }

  // world-space axis-aligned box
  bool visible(glm::vec3 lo, glm::vec3 hi, float margin = 0) const {
    glm::vec3 c = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
    for (int k = 0; k < n; ++k) {
      glm::vec3 nn(pl[k]);
      if (glm::dot(nn, c) + pl[k].w + glm::dot(glm::abs(nn), e) + margin < 0) return false;
    }
    return true;
  }
  // box (lo, hi) in object space under an affine model matrix: the planes are taken into object space (p * model), so a
  // rotated wall is tested as its oriented box, not its world AABB. margin is in world metres.
  bool visible(glm::vec3 lo, glm::vec3 hi, const glm::mat4& model, float margin = 0) const {
    glm::vec3 c = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
    for (int k = 0; k < n; ++k) {
      glm::vec4 p = glm::vec4(pl[k].x, pl[k].y, pl[k].z, pl[k].w + margin) * model;
      glm::vec3 nn(p);
      if (glm::dot(nn, c) + p.w + glm::dot(glm::abs(nn), e) < 0) return false;
    }
    return true;
  }
  bool sphere(glm::vec3 c, float r) const {
    for (int k = 0; k < n; ++k) if (glm::dot(glm::vec3(pl[k]), c) + pl[k].w + r < 0) return false;
    return true;
  }
};
