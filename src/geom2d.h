#pragma once
// Small 2-D polygon helpers in the world xz plane (vec2.x = world x, vec2.y = world z). Used by the lasso tool
// (lasso.cpp, lasso_water.cpp, ground_paint.cpp) and the terrain cuts.
#include "common.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace geom2d {

// even-odd point-in-polygon: self-intersecting freehand loops are tolerated
inline bool inside(const std::vector<vec2>& p, vec2 q) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
    if (((p[i].y > q.y) != (p[j].y > q.y)) && q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y + 1e-30f) + p[i].x) in = !in;
  return p.size() >= 3 && in;
}

inline float segDist(vec2 q, vec2 a, vec2 b) {
  vec2 ab = b - a; float t = glm::clamp(glm::dot(q - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
  return glm::length(q - (a + ab * t));
}

// distance to the outline (unsigned)
inline float edgeDist(const std::vector<vec2>& p, vec2 q) {
  float d = 1e30f;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) d = std::min(d, segDist(q, p[j], p[i]));
  return d;
}

// signed distance: positive inside, negative outside
inline float signedDist(const std::vector<vec2>& p, vec2 q) { float d = edgeDist(p, q); return inside(p, q) ? d : -d; }

// distance from q to the polygon edge when q is inside, 0 when outside (terrain cuts' feather convention)
inline float insideDistance(const std::vector<vec2>& p, vec2 q) { return inside(p, q) ? edgeDist(p, q) : 0.0f; }

inline void bounds(const std::vector<vec2>& p, vec2& lo, vec2& hi) {
  lo = vec2(1e30f); hi = vec2(-1e30f);
  for (vec2 v : p) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
}

inline float signedArea(const std::vector<vec2>& p) {
  float s = 0;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) s += (p[j].x - p[i].x) * (p[j].y + p[i].y);
  return s * 0.5f;
}

inline vec2 centroid(const std::vector<vec2>& p) {
  double a = 0, cx = 0, cz = 0;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    double c = (double)p[j].x * p[i].y - (double)p[i].x * p[j].y;
    a += c; cx += (p[j].x + p[i].x) * c; cz += (p[j].y + p[i].y) * c;
  }
  if (std::fabs(a) < 1e-9) { vec2 s(0); for (vec2 v : p) s += v; return p.empty() ? s : s / (float)p.size(); }
  return vec2((float)(cx / (3 * a)), (float)(cz / (3 * a)));
}

// a regular n-gon (circle lasso)
inline std::vector<vec2> circle(vec2 c, float r, int n = 64) {
  std::vector<vec2> out; out.reserve(n);
  for (int i = 0; i < n; ++i) { float a = 6.2831853f * i / n; out.push_back(c + r * vec2(std::cos(a), std::sin(a))); }
  return out;
}

inline vec2 rotate(vec2 v, vec2 c, float rad) {
  float s = std::sin(rad), co = std::cos(rad); vec2 d = v - c;
  return c + vec2(d.x * co - d.y * s, d.x * s + d.y * co);
}

// Douglas-Peucker on a closed loop: keeps the two points farthest apart and recurses on both halves
inline void dpRange(const std::vector<vec2>& p, size_t a, size_t b, float tol, std::vector<char>& keep) {
  if (b <= a + 1) return;
  float best = -1; size_t bi = a;
  for (size_t i = a + 1; i < b; ++i) { float d = segDist(p[i], p[a], p[b % p.size()]); if (d > best) { best = d; bi = i; } }
  if (best > tol) { keep[bi] = 1; dpRange(p, a, bi, tol, keep); dpRange(p, bi, b, tol, keep); }
}
inline std::vector<vec2> simplifyLoop(const std::vector<vec2>& p, float tol) {
  if (p.size() <= 8) return p;
  size_t far = 0; float fd = -1;
  for (size_t i = 1; i < p.size(); ++i) { float d = glm::length(p[i] - p[0]); if (d > fd) { fd = d; far = i; } }
  std::vector<char> keep(p.size(), 0); keep[0] = keep[far] = 1;
  dpRange(p, 0, far, tol, keep); dpRange(p, far, p.size(), tol, keep);
  std::vector<vec2> out;
  for (size_t i = 0; i < p.size(); ++i) if (keep[i]) out.push_back(p[i]);
  return out.size() >= 3 ? out : p;
}

// ear clipping of a simple polygon: triangles (3 points each), counter-clockwise in xz
inline std::vector<vec2> triangulate(std::vector<vec2> P) {
  std::vector<vec2> out;
  if (P.size() < 3) return out;
  if (signedArea(P) < 0) std::reverse(P.begin(), P.end());
  auto cross = [](vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
  size_t guard = P.size() * P.size() + 8;
  while (P.size() > 3 && guard--) {
    bool cut = false;
    for (size_t i = 0; i < P.size(); ++i) {
      vec2 a = P[(i + P.size() - 1) % P.size()], b = P[i], c = P[(i + 1) % P.size()];
      if (cross(a, b, c) <= 1e-9f) continue;
      bool ear = true;
      for (size_t k = 0; k < P.size() && ear; ++k) {
        vec2 q = P[k];
        if (q == a || q == b || q == c) continue;
        if (cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0) ear = false;
      }
      if (!ear) continue;
      out.insert(out.end(), {a, b, c});
      P.erase(P.begin() + (long)i);
      cut = true; break;
    }
    if (!cut) break;   // degenerate: give up on the rest
  }
  if (P.size() == 3) out.insert(out.end(), {P[0], P[1], P[2]});
  return out;
}

}   // namespace geom2d
