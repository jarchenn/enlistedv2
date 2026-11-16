// UI2 Battle Foundry logo: the emblem (an off-white shield outline round an anvil, a flame above it with an orange core)
// and the wordmark between two thin rules. This file is the whole logo: swap it, or drop the artwork in as
// content/ui/logo.png (transparent, emblem + wordmark side by side) and that image is drawn instead.
#include "logo.h"
#include "app.h"
#include <stb_image.h>
#include <cstring>

namespace {
const ImU32 kInk = IM_COL32(236, 232, 224, 255);   // off-white

GLuint gPng = 0; int gPngW = 0, gPngH = 0; bool gPngTried = false;
bool pngLogo() {
  if (gPngTried) return gPng != 0;
  gPngTried = true;
  int w, h, c; stbi_uc* p = stbi_load((contentDir() / "ui" / "logo.png").string().c_str(), &w, &h, &c, 4);
  if (!p) return false;
  glCreateTextures(GL_TEXTURE_2D, 1, &gPng); glTextureStorage2D(gPng, 1, GL_RGBA8, w, h);
  glTextureSubImage2D(gPng, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
  glTextureParameteri(gPng, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gPng, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(p); gPngW = w; gPngH = h;
  return true;
}
ImU32 fade(ImU32 c, float a) { return (c & 0x00ffffffu) | ((ImU32)((float)(c >> 24) * a) << 24); }
}

// emblem in a 100 x 120 unit box at p, h px tall
static void emblem(ImDrawList* dl, ImVec2 p, float h, float a) {
  const float u = h / 120.0f;
  auto P = [&](float x, float y) { return ImVec2(p.x + x * u, p.y + y * u); };
  auto shield = [&](float s, float th, ImU32 c) {   // scaled about (50, 62)
    auto Q = [&](float x, float y) { return P(50 + (x - 50) * s, 62 + (y - 62) * s); };
    dl->PathLineTo(Q(6, 11)); dl->PathLineTo(Q(50, 3)); dl->PathLineTo(Q(94, 11)); dl->PathLineTo(Q(94, 60));
    dl->PathBezierCubicCurveTo(Q(94, 88), Q(72, 104), Q(50, 117));
    dl->PathBezierCubicCurveTo(Q(28, 104), Q(6, 88), Q(6, 60));
    dl->PathStroke(c, ImDrawFlags_Closed, th);
  };
  shield(1.0f, 5.0f * u, fade(kInk, a));
  shield(0.83f, 1.4f * u, fade(kInk, 0.5f * a));
  // anvil: horn, face, waist, foot
  dl->PathLineTo(P(15, 71)); dl->PathLineTo(P(31, 69)); dl->PathLineTo(P(31, 77)); dl->PathBezierQuadraticCurveTo(P(23, 76), P(15, 71)); dl->PathFillConcave(fade(kInk, a));
  dl->AddRectFilled(P(30, 69), P(77, 77), fade(kInk, a), 0.8f * u);
  dl->AddQuadFilled(P(39, 77), P(68, 77), P(62, 86), P(45, 86), fade(kInk, a));
  dl->AddQuadFilled(P(45, 85.5f), P(62, 85.5f), P(73, 94), P(34, 94), fade(kInk, a));
  // flame: off-white tongue, orange core
  auto flame = [&](float s, ImU32 c) {   // scaled about the base (51, 66)
    auto Q = [&](float x, float y) { return P(51 + (x - 51) * s, 66 + (y - 66) * s); };
    dl->PathLineTo(Q(51, 66));
    dl->PathBezierCubicCurveTo(Q(37, 66), Q(32, 54), Q(38, 44));
    dl->PathBezierCubicCurveTo(Q(41, 38), Q(43, 32), Q(41, 24));
    dl->PathBezierCubicCurveTo(Q(50, 30), Q(54, 37), Q(52, 45));
    dl->PathBezierCubicCurveTo(Q(57, 41), Q(59, 35), Q(57, 28));
    dl->PathBezierCubicCurveTo(Q(68, 38), Q(70, 55), Q(62, 62));
    dl->PathBezierCubicCurveTo(Q(59, 65), Q(55, 66), Q(51, 66));
    dl->PathFillConcave(c);
  };
  flame(1.0f, fade(kInk, a));
  flame(0.62f, fade(IM_COL32(240, 118, 32, 255), a));
  flame(0.30f, fade(IM_COL32(255, 196, 104, 255), a));
}

float drawLogo(ImDrawList* dl, ImVec2 p, float h, float a, bool wordmark) {
  if (pngLogo()) {
    float w = h * gPngW / (float)gPngH * (wordmark ? 1.0f : 1.0f);
    dl->AddImage((ImTextureID)(intptr_t)gPng, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255 * a)));
    return w;
  }
  emblem(dl, p, h, a);
  float x = p.x + h * 0.86f;
  if (!wordmark) return x - p.x;
  x += h * 0.16f;
  ImFont* f = uiFontDisplay();
  const char* txt = "BATTLE FOUNDRY";
  const float px = h * 0.40f, track = px * 0.05f;
  float w = 0; for (const char* c = txt; *c; ++c) w += f->CalcTextSizeA(px, 1e9f, 0, c, c + 1).x + track;
  w -= track;
  const float y0 = p.y + h * 0.5f - px * 0.62f;
  const float r0 = p.y + h * 0.22f, r1 = p.y + h * 0.80f;
  dl->AddRectFilled(ImVec2(x, r0), ImVec2(x + w, r0 + std::max(1.0f, h * 0.014f)), fade(kInk, 0.85f * a));
  dl->AddRectFilled(ImVec2(x, r1), ImVec2(x + w, r1 + std::max(1.0f, h * 0.014f)), fade(kInk, 0.85f * a));
  float cx = x; const int v0 = dl->VtxBuffer.Size;
  for (const char* c = txt; *c; ++c) { dl->AddText(f, px, ImVec2(cx, y0), fade(kInk, a), c, c + 1); cx += f->CalcTextSizeA(px, 1e9f, 0, c, c + 1).x + track; }
  // a slight top-to-bottom fall-off in the letters (bright cap, smokier foot)
  for (int k = v0; k < dl->VtxBuffer.Size; ++k) {
    ImDrawVert& v = dl->VtxBuffer[k]; float t = std::clamp((v.pos.y - y0) / (px * 1.2f), 0.0f, 1.0f);
    int g = (int)(255 - 46 * t); ImU32 c = v.col; int al = c >> 24;
    v.col = IM_COL32(((c & 0xff) * g) / 255, (((c >> 8) & 0xff) * g) / 255, (((c >> 16) & 0xff) * (g - 8 * t)) / 255, al);
  }
  return cx - p.x;
}
