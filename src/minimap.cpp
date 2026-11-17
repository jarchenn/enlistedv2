// GM2 2D minimap (minimap.h, docs/minimap.md); UI1b redesign (user 2026-11-16): the ground in its real colours (terrain,
// ground paint, water; trees and vegetation are only the ground colour under them), roads and paths as light lines,
// buildings as dark footprints (ruins lighter), all baked into one texture per map; on top: an edge grid (letters along
// the top, numbers down the side), objectives as rhombi with their letter, spawns as flags and vehicle spawns as tank
// icons (blue yours, red the enemy's), teammates, the player's arrow and view cone, the BR storm, a scale bar.
#include "minimap.h"
#include "ui_glass.h"
#include "royale.h"
#include "app.h"
#include "game.h"
#include "editor_nav.h"
#include "ground_paint.h"
#include "lines.h"
#include "snap.h"
#include "terrain_blend.h"
#include "settings.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

uint64_t placementSig(const std::vector<Instance>& v, bool withPose);   // app.cpp

namespace {
// ---------------------------------------------------------------- the baked static layers
struct MapTex { GLuint tex = 0; int w = 0, h = 0; uint64_t sig = 0; double built = -1; };
MapTex gMap;
uint64_t staticSig(const App& app) {
  const Terrain& t = app.terrain;
  return placementSig(app.doc.instances, true) ^ ((uint64_t)(t.generation ^ (t.revision * 2654435761u) ^ (uint32_t)t.surfRev * 40503u) << 20) ^ (uint64_t)app.doc.paintSig * 0x9E3779B97F4A7C15ull;
}
struct Raster {
  int W, H; std::vector<vec3> c; const Terrain& t;
  vec2 px(vec2 w) const { return vec2((w.x - t.org.x) / t.size * W, (w.y - t.org.y) / t.sizeZ * H); }
  // a convex quad (world xz corners, in order) blended over the picture
  void quad(const vec2 (&wq)[4], vec3 col, float a) {
    vec2 q[4]; vec2 lo(1e9f), hi(-1e9f);
    for (int k = 0; k < 4; ++k) { q[k] = px(wq[k]); lo = glm::min(lo, q[k]); hi = glm::max(hi, q[k]); }
    float area = 0; for (int k = 0; k < 4; ++k) { vec2 a0 = q[k], b0 = q[(k + 1) % 4]; area += a0.x * b0.y - b0.x * a0.y; }
    float sgn = area >= 0 ? 1.0f : -1.0f;
    int x0 = std::max(0, (int)std::floor(lo.x)), x1 = std::min(W - 1, (int)std::ceil(hi.x)), y0 = std::max(0, (int)std::floor(lo.y)), y1 = std::min(H - 1, (int)std::ceil(hi.y));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
      vec2 p(x + 0.5f, y + 0.5f); bool in = true;
      for (int k = 0; k < 4 && in; ++k) { vec2 e = q[(k + 1) % 4] - q[k], d = p - q[k]; in = sgn * (e.x * d.y - e.y * d.x) >= -0.35f * glm::length(e); }   // half a pixel of grace: thin roads stay continuous
      if (in) c[(size_t)y * W + x] = glm::mix(c[(size_t)y * W + x], col, a);
    }
  }
  void segment(vec2 a, vec2 b, float width, vec3 col, float alpha) {
    vec2 d = b - a; float L = glm::length(d); if (L < 1e-3f) return;
    vec2 n = vec2(-d.y, d.x) / L * (width * 0.5f), e = d / L * (width * 0.5f);   // square caps overlap at the joints
    const vec2 wq[4] = {a - e + n, b + e + n, b + e - n, a - e - n};
    quad(wq, col, alpha);
  }
};
float roadWidth(const std::string& type) {
  return type.find("sidewalk") != std::string::npos ? 2.2f : type.find("4lane") != std::string::npos ? 14.0f : type.find("road") != std::string::npos ? 7.5f : 0.0f;
}
bool isBuilding(const AssetDef* a) { return a && !a->path.on && (a->enterable || crushBuilding(a) || blendPinned(a)); }
bool isRuin(const AssetDef* a) { return a->id.find("_ruin") != std::string::npos || a->id.rfind("ruin_", 0) == 0 || a->category == "Ruins"; }

void buildTex(const App& app) {
  const Terrain& t = app.terrain;
  const int S = std::clamp((int)std::max(t.size, t.sizeZ), 256, 1024);   // ~1 m a texel on ordinary maps
  int W = t.size >= t.sizeZ ? S : std::max(16, (int)(S * t.size / t.sizeZ)), H = t.size >= t.sizeZ ? std::max(16, (int)(S * t.sizeZ / t.size)) : S;
  Raster R{W, H, std::vector<vec3>((size_t)W * H), t};
  const vec3 sun = glm::normalize(vec3(-0.5f, 0.8f, -0.35f));
  const auto& types = groundPaintTypes();
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float wx = t.org.x + (x + 0.5f) / W * t.size, wz = t.org.y + (y + 0.5f) / H * t.sizeZ;
      float hh = t.height(wx, wz); vec3 n = t.normal(wx, wz);
      float lw = lassoWaterAtCurrent(wx, wz);
      vec3 c;
      if (hh < t.waterLevel + 0.05f || (lw > -1e8f && hh < lw)) c = vec3(0.20f, 0.33f, 0.42f);   // water
      else {
        // the ground as it is drawn: muted grass, earth on slopes, sand by the water, the painted ground over it
        c = glm::mix(vec3(0.33f, 0.39f, 0.24f), vec3(0.40f, 0.42f, 0.28f), std::clamp(hh / 30.0f, 0.0f, 1.0f));
        c = glm::mix(c, vec3(0.44f, 0.41f, 0.35f), std::clamp((0.86f - n.y) / 0.2f, 0.0f, 1.0f));
        c = glm::mix(c, vec3(0.64f, 0.59f, 0.45f), std::clamp((t.waterLevel + 1.2f - hh) / 1.0f, 0.0f, 1.0f));
        float w[kPaintTypes]; groundPaintWeights(app.doc, wx, wz, w);
        vec3 pc(0); float ws = 0; for (int k = 0; k < kPaintTypes && k < (int)types.size(); ++k) { pc += types[k].swatch * w[k]; ws += w[k]; }
        if (ws > 1e-3f) c = glm::mix(c, pc / ws, std::min(ws, 1.0f) * 0.9f);
        c = glm::mix(c, vec3(0.55f, 0.50f, 0.40f), t.wearAt(wx, wz) * 0.7f);   // worn tracks
      }
      c *= std::clamp(0.82f + 0.9f * (glm::dot(n, sun) - 0.78f), 0.65f, 1.12f);   // soft hillshade
      if (!t.inBuildArea(wx, wz)) c *= 0.6f;
      R.c[(size_t)y * W + x] = c;
    }
  // roads and paths, then buildings over them
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i); if (!a) continue;
    if (const LineType* lt = lineTypeOf(a)) {
      float w = roadWidth(lt->id); if (w <= 0) continue;
      LineData d; if (!lineGet(i, d) || d.pts.size() < 2) continue;
      vec3 col = lt->id.find("sidewalk") != std::string::npos ? vec3(0.70f, 0.69f, 0.65f) : vec3(0.60f, 0.60f, 0.58f);
      if (lt->id.find("_ruin") != std::string::npos) col *= 0.9f;
      for (size_t k = 0; k + 1 < d.pts.size(); ++k) R.segment(d.pts[k].p, d.pts[k + 1].p, w, col, 0.95f);
    } else if (a->path.on) {
      vec2 hf = a->footprint * 0.5f; quat q = yawQuat(i.yaw);
      vec2 wq[4]; vec2 cn[4] = {{-hf.x, -hf.y}, {hf.x, -hf.y}, {hf.x, hf.y}, {-hf.x, hf.y}};
      for (int k = 0; k < 4; ++k) { vec3 r = q * vec3(cn[k].x, 0, cn[k].y); wq[k] = vec2(i.pos.x + r.x, i.pos.z + r.z); }
      R.quad(wq, glm::mix(a->path.albedo, vec3(0.62f, 0.58f, 0.50f), 0.5f) * 1.1f, 0.85f);
    }
  }
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!isBuilding(a) || isLineAsset(a)) continue;
    vec2 hf = a->footprint * 0.5f; quat q = yawQuat(i.yaw);
    vec2 wq[4]; vec2 cn[4] = {{-hf.x, -hf.y}, {hf.x, -hf.y}, {hf.x, hf.y}, {-hf.x, hf.y}};
    for (int k = 0; k < 4; ++k) { vec3 r = q * vec3(cn[k].x, 0, cn[k].y); wq[k] = vec2(i.pos.x + r.x, i.pos.z + r.z); }
    R.quad(wq, isRuin(a) ? vec3(0.38f, 0.36f, 0.33f) : vec3(0.15f, 0.15f, 0.16f), isRuin(a) ? 0.8f : 0.95f);
  }
  std::vector<uint8_t> px((size_t)W * H * 4);
  for (size_t k = 0; k < R.c.size(); ++k) { for (int j = 0; j < 3; ++j) px[k * 4 + j] = (uint8_t)std::clamp(R.c[k][j] * 255.0f, 0.0f, 255.0f); px[k * 4 + 3] = 255; }   // display colours already
  if (gMap.tex && (gMap.w != W || gMap.h != H)) { glDeleteTextures(1, &gMap.tex); gMap.tex = 0; }
  if (!gMap.tex) {
    glCreateTextures(GL_TEXTURE_2D, 1, &gMap.tex);
    glTextureStorage2D(gMap.tex, 1, GL_RGBA8, W, H);
    glTextureParameteri(gMap.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gMap.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(gMap.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gMap.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(gMap.tex, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  gMap.w = W; gMap.h = H;
}

// ---------------------------------------------------------------- vector icons (screen space)
ImVec2 iv(vec2 v) { return ImVec2(v.x, v.y); }
const uint32_t kBlue = IM_COL32(70, 150, 255, 255), kRed = IM_COL32(235, 70, 60, 255), kWhite = IM_COL32(235, 235, 228, 255), kInk = IM_COL32(12, 14, 16, 235);
void rhombus(ImDrawList* dl, vec2 c, float s, uint32_t fill, char letter) {
  ImVec2 p[4] = {iv(c + vec2(0, -s)), iv(c + vec2(s, 0)), iv(c + vec2(0, s)), iv(c + vec2(-s, 0))};
  dl->AddConvexPolyFilled(p, 4, fill); dl->AddPolyline(p, 4, kInk, ImDrawFlags_Closed, 1.5f);
  char L[2] = {letter, 0}; ImVec2 ts = ImGui::CalcTextSize(L);
  dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), fill == kWhite ? kInk : IM_COL32(255, 255, 255, 255), L);
}
void flag(ImDrawList* dl, vec2 c, float s, uint32_t col) {   // a pole and a pennant, foot at c
  dl->AddLine(iv(c + vec2(-s * 0.5f, s * 0.9f)), iv(c + vec2(-s * 0.5f, -s * 0.9f)), kInk, 3.0f);
  dl->AddLine(iv(c + vec2(-s * 0.5f, s * 0.9f)), iv(c + vec2(-s * 0.5f, -s * 0.9f)), IM_COL32(230, 230, 220, 255), 1.4f);
  ImVec2 f[4] = {iv(c + vec2(-s * 0.45f, -s * 0.9f)), iv(c + vec2(s * 0.8f, -s * 0.55f)), iv(c + vec2(-s * 0.45f, -s * 0.15f)), iv(c + vec2(-s * 0.45f, -s * 0.9f))};
  dl->AddTriangleFilled(f[0], f[1], f[2], col); dl->AddPolyline(f, 3, kInk, ImDrawFlags_Closed, 1.2f);
}
void tankIcon(ImDrawList* dl, vec2 c, float s, uint32_t col) {   // hull, turret, gun (side-on silhouette)
  dl->AddRectFilled(iv(c + vec2(-s, -s * 0.05f)), iv(c + vec2(s, s * 0.55f)), col, s * 0.25f);
  dl->AddRect(iv(c + vec2(-s, -s * 0.05f)), iv(c + vec2(s, s * 0.55f)), kInk, s * 0.25f, 0, 1.2f);
  dl->AddRectFilled(iv(c + vec2(-s * 0.45f, -s * 0.5f)), iv(c + vec2(s * 0.35f, -s * 0.05f)), col, s * 0.15f);
  dl->AddRect(iv(c + vec2(-s * 0.45f, -s * 0.5f)), iv(c + vec2(s * 0.35f, -s * 0.05f)), kInk, s * 0.15f, 0, 1.2f);
  dl->AddLine(iv(c + vec2(s * 0.35f, -s * 0.3f)), iv(c + vec2(s * 1.25f, -s * 0.3f)), kInk, 3.0f);
  dl->AddLine(iv(c + vec2(s * 0.35f, -s * 0.3f)), iv(c + vec2(s * 1.2f, -s * 0.3f)), col, 1.5f);
}
// legacy marker shapes (deploy screen, artillery map): 0 dot, 1 square, 2 arrow, 3 crosshair, 4 ring
void shape(ImDrawList* dl, vec2 c, int kind, float s, float yaw, uint32_t col) {
  switch (kind) {
    case 1: dl->AddRectFilled(iv(c - vec2(s)), iv(c + vec2(s)), col); dl->AddRect(iv(c - vec2(s)), iv(c + vec2(s)), kInk); break;
    case 2: {
      float a = radians(yaw); vec2 f(std::sin(a), -std::cos(a)), r(-f.y, f.x);
      ImVec2 p0 = iv(c + f * (s * 1.6f)), p1 = iv(c - f * s + r * s), p2 = iv(c - f * (s * 0.4f)), p3 = iv(c - f * s - r * s);
      dl->AddTriangleFilled(p0, p1, p2, col); dl->AddTriangleFilled(p0, p2, p3, col);
      ImVec2 ol[4] = {p0, p1, p2, p3}; dl->AddPolyline(ol, 4, kInk, ImDrawFlags_Closed, 1.2f);
    } break;
    case 3:
      dl->AddCircle(iv(c), s, col, 24, 2.0f);
      dl->AddLine(iv(c - vec2(s * 1.6f, 0)), iv(c - vec2(s * 0.4f, 0)), col, 2.0f); dl->AddLine(iv(c + vec2(s * 0.4f, 0)), iv(c + vec2(s * 1.6f, 0)), col, 2.0f);
      dl->AddLine(iv(c - vec2(0, s * 1.6f)), iv(c - vec2(0, s * 0.4f)), col, 2.0f); dl->AddLine(iv(c + vec2(0, s * 0.4f)), iv(c + vec2(0, s * 1.6f)), col, 2.0f);
      break;
    case 4: dl->AddCircle(iv(c), s, col, 24, 2.0f); break;
    default: dl->AddCircleFilled(iv(c), s, col); dl->AddCircle(iv(c), s, kInk, 0, 1.2f); break;
  }
}
float niceStep(float span) { for (float s : {25.0f, 50.0f, 100.0f, 200.0f, 250.0f, 500.0f, 1000.0f}) if (span / s <= 10.0f) return s; return 2000.0f; }
}   // namespace

MinimapView drawMinimap(App& app, ImDrawList* dl, vec2 p0, vec2 p1, vec2 centre, float metres, unsigned layers, const std::vector<MinimapMarker>& markers) {
  const Terrain& t = app.terrain;
  MinimapView v; v.p0 = p0; v.p1 = p1;
  vec2 sz = glm::max(p1 - p0, vec2(1));
  if (metres <= 0) { v.centre = t.org + vec2(t.size, t.sizeZ) * 0.5f; v.scale = std::min(sz.x / t.size, sz.y / t.sizeZ); }
  else { v.centre = centre; v.scale = std::min(sz.x, sz.y) / metres; }
  if (layers & MM_ROTATE) v.rot = radians(app.eyeYaw);   // heading up: the view direction points to the top
  if (!dl) return v;
  const float now = (float)ImGui::GetTime();
  dl->PushClipRect(iv(p0), iv(p1), true);
  dl->AddRectFilled(iv(p0), iv(p1), IM_COL32(20, 22, 20, 240));
  if (layers & (MM_TERRAIN | MM_STRUCTURES)) {
    uint64_t sig = staticSig(app);
    if (!gMap.tex || (gMap.sig != sig && now - gMap.built > 0.5)) { buildTex(app); gMap.sig = sig; gMap.built = now; }   // edits: at most twice a second
    vec2 a = t.org, b = t.org + vec2(t.size, t.sizeZ);
    dl->AddImageQuad((ImTextureID)(intptr_t)gMap.tex, iv(v.toScreen(a)), iv(v.toScreen(vec2(b.x, a.y))), iv(v.toScreen(b)), iv(v.toScreen(vec2(a.x, b.y))));
  }
  vec2 lo(1e9f), hi(-1e9f); for (vec2 c : {p0, p1, vec2(p0.x, p1.y), vec2(p1.x, p0.y)}) { lo = glm::min(lo, v.toWorld(c)); hi = glm::max(hi, v.toWorld(c)); }
  lo -= vec2(30); hi += vec2(30);
  auto onMap = [&](vec2 w) { return w.x >= lo.x && w.y >= lo.y && w.x <= hi.x && w.y <= hi.y; };
  // grid (north up only): faint lines, letters along the top edge, numbers down the left
  const float cell = niceStep(std::max(t.size, t.sizeZ));
  if ((layers & MM_GRID) && v.rot == 0) {
    for (float x = t.org.x + cell; x < t.org.x + t.size - 1; x += cell) { vec2 s = v.toScreen(vec2(x, 0)); if (s.x > p0.x && s.x < p1.x) dl->AddLine(ImVec2(s.x, p0.y), ImVec2(s.x, p1.y), IM_COL32(255, 255, 255, 28)); }
    for (float z = t.org.y + cell; z < t.org.y + t.sizeZ - 1; z += cell) { vec2 s = v.toScreen(vec2(0, z)); if (s.y > p0.y && s.y < p1.y) dl->AddLine(ImVec2(p0.x, s.y), ImVec2(p1.x, s.y), IM_COL32(255, 255, 255, 28)); }
  }
  // the scenario: the running match's (objective states) or the map's own
  Scenario docSc; const Scenario* sc = nullptr; const Match* m = nullptr;
  if (gGame && gGame->active) { m = &gGame->match; sc = &m->sc; }
  else if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) { try { docSc = Scenario::fromJson(app.doc.extra["scenario"]); sc = &docSc; } catch (...) {} }
  Soldier* me = gGame && gGame->active ? gGame->local() : nullptr;
  const CombatRole myRole = me ? me->role : CombatRole::Defender;   // in the editor: the side humans play (defenders)
  // crates (ammo resupply)
  if (layers & MM_CRATES)
    for (auto& i : app.doc.instances) {
      const AssetDef* a = app.assetFor(i);
      if (a && !a->crate.is_null() && !a->crate.empty() && onMap(vec2(i.pos.x, i.pos.z))) { vec2 c = v.toScreen(vec2(i.pos.x, i.pos.z)); dl->AddRectFilled(iv(c - vec2(3)), iv(c + vec2(3)), IM_COL32(230, 200, 60, 240), 1.5f); dl->AddRect(iv(c - vec2(3)), iv(c + vec2(3)), kInk, 1.5f); }
    }
  if (sc && (layers & MM_SPAWNS))
    for (auto& s : sc->spawns) {
      vec2 c = v.toScreen(vec2(s.pos.x, s.pos.z));
      flag(dl, c, 8.0f, s.role == myRole ? kBlue : kRed);
    }
  if (sc && (layers & MM_SPAWNS))   // VH7 / UI3: vehicle spawns from Scenario::vehicleSpawns (team -1: neutral, drawn as ours)
    for (const VehicleSpawn& vs : sc->vehicleSpawns) {
      if (!onMap(vec2(vs.pos.x, vs.pos.z))) continue;
      const bool mine = vs.team < 0 || vs.team == (me ? me->team : 0);
      tankIcon(dl, v.toScreen(vec2(vs.pos.x, vs.pos.z)), 7.0f, mine ? kBlue : kRed);
    }
  if (sc && (layers & MM_OBJECTIVES)) {
    for (size_t k = 0; k < sc->objectives.size(); ++k) {
      const Objective& o = sc->objectives[k];
      ObjState st = m && k < m->obj.size() ? m->obj[k].state : ObjState::Locked;
      // held by: captured -> attackers; the active point -> defenders until taken; later points -> nobody yet
      int holder = st == ObjState::Captured ? (int)CombatRole::Attacker : st == ObjState::Active ? (int)CombatRole::Defender : -1;
      uint32_t col = holder >= 0 && holder != (int)myRole ? kRed : kBlue;   // the user: points are blue rhombi; red once the enemy holds one
      bool contested = m && k < m->obj.size() && st == ObjState::Active && m->obj[k].A > 0;
      if (contested) { float pz = 0.5f + 0.5f * std::sin(now * 6.0f); col = (col & 0x00FFFFFFu) | ((uint32_t)(150 + 105 * pz) << 24); }
      vec2 c = v.toScreen(vec2(o.pos.x, o.pos.z));
      uint32_t ring = (col & 0x00FFFFFFu) | 0x50000000u;
      if (o.shape == CaptureShape::Polygon && o.polygon.size() >= 3) { std::vector<ImVec2> pts; for (auto& q : o.polygon) pts.push_back(iv(v.toScreen(q))); dl->AddPolyline(pts.data(), (int)pts.size(), ring, ImDrawFlags_Closed, 2.0f); }
      else dl->AddCircle(iv(c), std::max(6.0f, o.radius * v.scale), ring, 40, 2.0f);
      rhombus(dl, c, 10.0f, col, o.letter());
    }
  }
  if ((layers & MM_TEAMMATES) && me)
    for (auto& s : gGame->sold)
      if (s.inUse && s.alive && !s.localPlayer && s.team == me->team && onMap(vec2(s.feet.x, s.feet.z))) {
        vec2 c = v.toScreen(vec2(s.feet.x, s.feet.z)); dl->AddCircleFilled(iv(c), 3.2f, kBlue); dl->AddCircle(iv(c), 3.2f, kInk, 0, 1.0f);
      }
  if (layers & MM_PLAYER) {
    vec3 e = app.eyePos(); float yaw = app.eyeYaw;
    if (app.tanks.occupied) if (TankRT* tr = app.tanks.get(app.tanks.occupied)) yaw = tr->yaw + tr->tYaw;   // a tank: where the gun points
    vec2 c = v.toScreen(vec2(e.x, e.z));
    float aspect = ImGui::GetIO().DisplaySize.x / std::max(1.0f, ImGui::GetIO().DisplaySize.y);
    float half = std::atan(std::tan(radians(app.worldFov) * 0.5f) * aspect);   // the horizontal field of view (narrows in ADS / sights)
    float L = metres > 0 ? std::min(sz.x, sz.y) * 0.32f : 60.0f * v.scale, h0 = radians(yaw);   // the whole map: a 60 m cone
    std::vector<ImVec2> cone{iv(c)};
    for (int k = 0; k <= 12; ++k) {
      float a = h0 - half + 2 * half * k / 12.0f;
      vec2 d = vec2(-std::sin(a), -std::cos(a));   // world forward of App::forward at this yaw
      vec2 s = v.toScreen(vec2(e.x, e.z) + d) - c; s = glm::length(s) > 1e-4f ? glm::normalize(s) : vec2(0, -1);
      cone.push_back(iv(c + s * L));
    }
    dl->AddConvexPolyFilled(cone.data(), (int)cone.size(), IM_COL32(255, 255, 240, 46));
    vec2 f = v.toScreen(vec2(e.x, e.z) + vec2(-std::sin(h0), -std::cos(h0))) - c;
    shape(dl, c, 2, 6.5f, degrees(std::atan2(f.x, -f.y)), kWhite);
  }
  royaleMinimap(dl, v);   // BR1: the storm's current and next ring, the bomber
  for (auto& mk : markers) {
    vec2 c = v.toScreen(mk.pos);
    shape(dl, c, mk.shape, mk.size, mk.yaw, mk.col);
    if (!mk.label.empty()) dl->AddText(ImVec2(c.x + mk.size + 4, c.y - 7), mk.col, mk.label.c_str());
  }
  // edge labels and the scale bar
  if ((layers & MM_GRID) && v.rot == 0) {
    const uint32_t tc = IM_COL32(230, 228, 215, 220), bg = IM_COL32(12, 14, 16, 150);
    float fh = ImGui::GetTextLineHeight();
    dl->AddRectFilled(iv(p0), ImVec2(p1.x, p0.y + fh + 2), bg); dl->AddRectFilled(iv(p0), ImVec2(p0.x + fh + 4, p1.y), bg);
    int col = 0; for (float x = t.org.x; x < t.org.x + t.size - 1; x += cell, ++col) {
      vec2 s = v.toScreen(vec2(x + cell * 0.5f, 0)); char b[4]; std::snprintf(b, sizeof b, "%c", 'A' + col % 26);
      if (s.x > p0.x + fh + 4 && s.x < p1.x - 4) dl->AddText(ImVec2(s.x - ImGui::CalcTextSize(b).x * 0.5f, p0.y + 1), tc, b);
    }
    int row = 1; for (float z = t.org.y; z < t.org.y + t.sizeZ - 1; z += cell, ++row) {
      vec2 s = v.toScreen(vec2(0, z + cell * 0.5f)); char b[4]; std::snprintf(b, sizeof b, "%d", row);
      if (s.y > p0.y + fh + 2 && s.y < p1.y - 4) dl->AddText(ImVec2(p0.x + 2 + (fh + 2 - ImGui::CalcTextSize(b).x) * 0.5f, s.y - fh * 0.5f), tc, b);
    }
  }
  if (layers & MM_SCALE) {
    float want = (p1.x - p0.x) * 0.28f / v.scale, step = 10; for (float s : {10.f, 20.f, 25.f, 50.f, 100.f, 200.f, 250.f, 500.f, 1000.f}) if (s <= want) step = s;
    float L = step * v.scale; vec2 a(p1.x - 10 - L, p1.y - 10);
    dl->AddLine(iv(a), iv(a + vec2(L, 0)), kInk, 4.0f); dl->AddLine(iv(a), iv(a + vec2(L, 0)), kWhite, 2.0f);
    char b[24]; std::snprintf(b, sizeof b, "%.0f m", step); ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddText(ImVec2(a.x + (L - ts.x) * 0.5f + 1, a.y - ts.y - 1), kInk, b); dl->AddText(ImVec2(a.x + (L - ts.x) * 0.5f, a.y - ts.y - 2), kWhite, b);
  }
  dl->PopClipRect();
  dl->AddRect(iv(p0 - vec2(1)), iv(p1 + vec2(1)), IM_COL32(12, 14, 16, 230), 3.0f, 0, 2.0f);
  dl->AddRect(iv(p0), iv(p1), IM_COL32(210, 205, 185, 150), 2.0f, 0, 1.0f);
  return v;
}

void minimapHud(App& app, bool large) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  vec3 e = app.eyePos();
  if (large) {   // the whole terrain, north up, centred on screen
    float s = std::min(io.DisplaySize.x, io.DisplaySize.y) * 0.86f;
    vec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 110));
    uiGlassPanel(dl, ImVec2(c.x - s * 0.5f - 12, c.y - s * 0.5f - 12), ImVec2(c.x + s * 0.5f + 12, c.y + s * 0.5f + 12), 16);   // UI2: the glass frame
    drawMinimap(app, dl, c - vec2(s * 0.5f), c + vec2(s * 0.5f), vec2(0), 0, MM_ALL & ~MM_ROTATE);
    return;
  }
  float s = std::clamp(io.DisplaySize.y * 0.24f, 160.0f, 340.0f);   // the corner map: 220 m across around the player
  vec2 p0(io.DisplaySize.x - s - 16, 16);
  unsigned layers = MM_HUD | MM_SPAWNS | MM_GRID | MM_SCALE | (settings().s.minimapRotate ? MM_ROTATE : 0u);
  uiGlassPanel(dl, ImVec2(p0.x - 7, p0.y - 7), ImVec2(p0.x + s + 7, p0.y + s + 7), 12);   // UI2: the glass frame
  drawMinimap(app, dl, p0, p0 + vec2(s), vec2(e.x, e.z), 220.0f, layers);
}
