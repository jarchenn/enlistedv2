// GM2 2D minimap (minimap.h, docs/minimap.md)
#include "minimap.h"
#include "app.h"
#include "game.h"
#include "editor_nav.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
// the terrain relief, rebuilt when the terrain changes (shaded heights, water, worn ground; outside the build area dimmed)
struct MapTex { GLuint tex = 0; int w = 0, h = 0; uint32_t gen = 0; };
MapTex gMap;
uint32_t terrainGen(const Terrain& t) { return t.generation ^ (t.revision * 2654435761u) ^ (uint32_t)t.surfRev * 40503u ^ 0x5A5Au; }
void buildTex(const Terrain& t) {
  const int S = 512;
  int W = t.size >= t.sizeZ ? S : std::max(16, (int)(S * t.size / t.sizeZ)), H = t.size >= t.sizeZ ? std::max(16, (int)(S * t.sizeZ / t.size)) : S;
  std::vector<uint8_t> px((size_t)W * H * 4);
  vec3 sun = glm::normalize(vec3(-0.5f, 0.8f, -0.35f));
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float wx = t.org.x + (x + 0.5f) / W * t.size, wz = t.org.y + (y + 0.5f) / H * t.sizeZ;
      float hh = t.height(wx, wz);
      float shade = std::clamp(0.55f + 0.8f * (glm::dot(t.normal(wx, wz), sun) - 0.75f) * 3.0f, 0.3f, 1.2f);
      vec3 c = hh < t.waterLevel + 0.05f ? vec3(0.16f, 0.28f, 0.40f) : glm::mix(vec3(0.22f, 0.32f, 0.15f), vec3(0.40f, 0.40f, 0.24f), std::clamp((hh + 1.0f) / 8.0f, 0.0f, 1.0f));
      c = glm::mix(c, vec3(0.45f, 0.38f, 0.27f), t.wearAt(wx, wz) * 0.8f);
      if (!t.inBuildArea(wx, wz)) c *= 0.55f;
      c *= shade;
      uint8_t* p = &px[((size_t)y * W + x) * 4];
      for (int k = 0; k < 3; ++k) p[k] = (uint8_t)std::clamp(c[k] * 255.0f, 0.0f, 255.0f);
      p[3] = 255;
    }
  if (gMap.tex && (gMap.w != W || gMap.h != H)) { glDeleteTextures(1, &gMap.tex); gMap.tex = 0; }
  if (!gMap.tex) {
    glCreateTextures(GL_TEXTURE_2D, 1, &gMap.tex);
    glTextureStorage2D(gMap.tex, 1, GL_RGBA8, W, H);
    glTextureParameteri(gMap.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gMap.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(gMap.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gMap.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(gMap.tex, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  gMap.w = W; gMap.h = H; gMap.gen = terrainGen(t);
}
ImVec2 iv(vec2 v) { return ImVec2(v.x, v.y); }
// screen-space marker shapes (yaw: deg, 0 = up on the map (-z), clockwise)
void shape(ImDrawList* dl, vec2 c, int kind, float s, float yaw, uint32_t col) {
  const uint32_t edge = IM_COL32(10, 10, 8, 220);
  switch (kind) {
    case 1: dl->AddRectFilled(iv(c - vec2(s)), iv(c + vec2(s)), col); dl->AddRect(iv(c - vec2(s)), iv(c + vec2(s)), edge); break;
    case 2: {
      float a = radians(yaw); vec2 f(std::sin(a), -std::cos(a)), r(-f.y, f.x);
      ImVec2 p0 = iv(c + f * (s * 1.6f)), p1 = iv(c - f * s + r * s), p2 = iv(c - f * (s * 0.4f)), p3 = iv(c - f * s - r * s);
      dl->AddTriangleFilled(p0, p1, p2, col); dl->AddTriangleFilled(p0, p2, p3, col);
      ImVec2 ol[4] = {p0, p1, p2, p3}; dl->AddPolyline(ol, 4, edge, ImDrawFlags_Closed, 1.0f);
    } break;
    case 3:
      dl->AddCircle(iv(c), s, col, 24, 2.0f);
      dl->AddLine(iv(c - vec2(s * 1.6f, 0)), iv(c - vec2(s * 0.4f, 0)), col, 2.0f); dl->AddLine(iv(c + vec2(s * 0.4f, 0)), iv(c + vec2(s * 1.6f, 0)), col, 2.0f);
      dl->AddLine(iv(c - vec2(0, s * 1.6f)), iv(c - vec2(0, s * 0.4f)), col, 2.0f); dl->AddLine(iv(c + vec2(0, s * 0.4f)), iv(c + vec2(0, s * 1.6f)), col, 2.0f);
      break;
    case 4: dl->AddCircle(iv(c), s, col, 24, 2.0f); break;
    default: dl->AddCircleFilled(iv(c), s, col); dl->AddCircle(iv(c), s, edge); break;
  }
}
}   // namespace

MinimapView drawMinimap(App& app, ImDrawList* dl, vec2 p0, vec2 p1, vec2 centre, float metres, unsigned layers, const std::vector<MinimapMarker>& markers) {
  const Terrain& t = app.terrain;
  MinimapView v; v.p0 = p0; v.p1 = p1;
  vec2 sz = glm::max(p1 - p0, vec2(1));
  if (metres <= 0) { v.centre = t.org + vec2(t.size, t.sizeZ) * 0.5f; v.scale = std::min(sz.x / t.size, sz.y / t.sizeZ); }
  else { v.centre = centre; v.scale = std::min(sz.x, sz.y) / metres; }
  if (!dl) return v;
  dl->PushClipRect(iv(p0), iv(p1), true);
  dl->AddRectFilled(iv(p0), iv(p1), IM_COL32(16, 18, 14, 235));
  if (layers & MM_TERRAIN) {
    if (!gMap.tex || gMap.gen != terrainGen(t)) buildTex(t);
    dl->AddImage((ImTextureID)(intptr_t)gMap.tex, iv(v.toScreen(t.org)), iv(v.toScreen(t.org + vec2(t.size, t.sizeZ))));
  }
  vec2 lo = v.toWorld(p0) - vec2(30), hi = v.toWorld(p1) + vec2(30);   // world box on screen (cull)
  auto onMap = [&](vec2 w) { return w.x >= lo.x && w.y >= lo.y && w.x <= hi.x && w.y <= hi.y; };
  if (layers & (MM_STRUCTURES | MM_CRATES)) {
    for (auto& i : app.doc.instances) {
      const AssetDef* a = app.assetFor(i);
      if (!a || a->tree.on || !onMap(vec2(i.pos.x, i.pos.z))) continue;
      bool crate = !a->crate.is_null() && !a->crate.empty();
      if (crate) {
        if (layers & MM_CRATES) shape(dl, v.toScreen(vec2(i.pos.x, i.pos.z)), 1, 3.5f, 0, IM_COL32(230, 200, 60, 240));
        continue;
      }
      if (!(layers & MM_STRUCTURES)) continue;
      vec2 hf = a->footprint * 0.5f;
      if (std::max(hf.x, hf.y) < 1.0f || std::max(hf.x, hf.y) * v.scale < 0.8f) continue;   // small props: no outline
      quat q = yawQuat(i.yaw);
      ImVec2 p[4]; vec2 cn[4] = {{-hf.x, -hf.y}, {hf.x, -hf.y}, {hf.x, hf.y}, {-hf.x, hf.y}};
      for (int k = 0; k < 4; ++k) { vec3 r = q * vec3(cn[k].x, 0, cn[k].y); p[k] = iv(v.toScreen(vec2(i.pos.x + r.x, i.pos.z + r.z))); }
      dl->AddConvexPolyFilled(p, 4, IM_COL32(170, 165, 150, 220));
      dl->AddPolyline(p, 4, IM_COL32(60, 58, 50, 220), ImDrawFlags_Closed, 1.0f);
    }
  }
  // the scenario: the running match's (objective states) or the map's own
  Scenario docSc; const Scenario* sc = nullptr; const Match* m = nullptr;
  if (gGame && gGame->active) { m = &gGame->match; sc = &m->sc; }
  else if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) { try { docSc = Scenario::fromJson(app.doc.extra["scenario"]); sc = &docSc; } catch (...) {} }
  if (sc && (layers & MM_SPAWNS))
    for (auto& s : sc->spawns) shape(dl, v.toScreen(vec2(s.pos.x, s.pos.z)), 1, 4.0f, 0, s.role == CombatRole::Attacker ? IM_COL32(110, 170, 255, 230) : IM_COL32(255, 120, 100, 230));
  if (sc && (layers & MM_OBJECTIVES)) {
    for (size_t k = 0; k < sc->objectives.size(); ++k) {
      const Objective& o = sc->objectives[k];
      int st = m && k < m->obj.size() ? (int)m->obj[k].state : 1;
      uint32_t col = st == (int)ObjState::Captured ? IM_COL32(120, 170, 255, 240) : st == (int)ObjState::Active ? IM_COL32(255, 205, 70, 250) : IM_COL32(200, 190, 160, 200);
      vec2 c = v.toScreen(vec2(o.pos.x, o.pos.z));
      if (o.shape == CaptureShape::Polygon && o.polygon.size() >= 3) {
        std::vector<ImVec2> pts; for (auto& q : o.polygon) pts.push_back(iv(v.toScreen(q)));
        dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 2.0f);
      } else dl->AddCircle(iv(c), std::max(5.0f, o.radius * v.scale), col, 40, 2.0f);
      dl->AddCircleFilled(iv(c), 8, IM_COL32(30, 26, 12, 230));
      char L[2] = {o.letter(), 0};
      dl->AddText(ImVec2(c.x - 4, c.y - 7), col, L);
    }
  }
  // teammates (the local player's team), then the player
  Soldier* me = gGame && gGame->active ? gGame->local() : nullptr;
  if ((layers & MM_TEAMMATES) && me)
    for (auto& s : gGame->sold)
      if (s.inUse && s.alive && !s.localPlayer && s.team == me->team && onMap(vec2(s.feet.x, s.feet.z)))
        shape(dl, v.toScreen(vec2(s.feet.x, s.feet.z)), 0, 3.5f, 0, IM_COL32(110, 200, 255, 240));
  if (layers & MM_PLAYER) {
    vec3 e = app.eyePos(), f = camForward(app.eyeYaw, 0);
    shape(dl, v.toScreen(vec2(e.x, e.z)), 2, 6.0f, degrees(std::atan2(f.x, -f.z)), IM_COL32(255, 255, 255, 250));
  }
  for (auto& mk : markers) {
    vec2 c = v.toScreen(mk.pos);
    shape(dl, c, mk.shape, mk.size, mk.yaw, mk.col);
    if (!mk.label.empty()) dl->AddText(ImVec2(c.x + mk.size + 4, c.y - 7), mk.col, mk.label.c_str());
  }
  dl->AddRect(iv(p0), iv(p1), IM_COL32(200, 195, 170, 200), 0, 0, 1.5f);
  dl->PopClipRect();
  return v;
}

void minimapHud(App& app, bool large) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  vec3 e = app.eyePos();
  if (large) {   // the whole terrain, centred on screen
    float s = std::min(io.DisplaySize.x, io.DisplaySize.y) * 0.86f;
    vec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    drawMinimap(app, dl, c - vec2(s * 0.5f), c + vec2(s * 0.5f), vec2(0), 0, MM_ALL);
    dl->AddText(ImVec2(c.x - s * 0.5f + 8, c.y - s * 0.5f + 6), IM_COL32(240, 235, 210, 230), "Map (M)");
    return;
  }
  float s = std::clamp(io.DisplaySize.y * 0.22f, 140.0f, 320.0f);   // the corner map: 200 m across, centred on the player
  vec2 p0(14, io.DisplaySize.y - s - 14);
  drawMinimap(app, dl, p0, p0 + vec2(s), vec2(e.x, e.z), 200.0f, MM_HUD);
}
