// UI2 Battle Foundry logo. content/ui/logo.png (the user's artwork, keyed to alpha; logo_mark.png = the shield alone)
// is drawn when present; otherwise this vector recreation: the emblem (an off-white shield outline round an anvil, a flame above it with an orange core)
// and the wordmark between two thin rules. This file is the whole logo: swap it, or drop the artwork in as
// content/ui/logo.png (transparent, emblem + wordmark side by side) and that image is drawn instead.
#include "logo.h"
#include "app.h"
#include <stb_image.h>
#include <cstring>

namespace {
const ImU32 kInk = IM_COL32(236, 232, 224, 255);   // off-white

struct Png { GLuint tex = 0; int w = 0, h = 0; bool tried = false; };
Png gFull, gMark;
bool loadPng(Png& p, const char* file) {
  if (p.tried) return p.tex != 0;
  p.tried = true;
  int w, h, c; stbi_uc* d = stbi_load((contentDir() / "ui" / file).string().c_str(), &w, &h, &c, 4);
  if (!d) return false;
  glCreateTextures(GL_TEXTURE_2D, 1, &p.tex); glTextureStorage2D(p.tex, 1, GL_RGBA8, w, h);
  glTextureSubImage2D(p.tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d);
  glTextureParameteri(p.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(p.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(p.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(p.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  stbi_image_free(d); p.w = w; p.h = h;
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
  // flame (UI3): harsh and angular like the artwork: straight facets, sharp asymmetric tongues, taller than wide,
  // a small hot orange-to-red diamond core; no curves, no glow
  {
    const ImVec2 F[] = {P(51, 67), P(37, 63), P(33, 51), P(39, 42), P(37, 33), P(43, 37), P(45, 20), P(51, 31), P(55, 26), P(57, 38), P(63, 30), P(66, 45), P(64, 57), P(58, 65)};
    for (const ImVec2& q : F) dl->PathLineTo(q);
    dl->PathFillConcave(fade(kInk, a));
    const ImVec2 H[] = {P(51, 62), P(43, 57), P(43, 49), P(47, 44), P(48, 37), P(52, 44), P(55, 41), P(58, 50), P(57, 58)};
    for (const ImVec2& q : H) dl->PathLineTo(q);
    dl->PathFillConcave(fade(IM_COL32(10, 12, 14, 255), a));   // the hollow between outline and core (the artwork's dark gap)
    dl->AddQuadFilled(P(51, 46), P(55.5f, 53), P(51, 60), P(46.5f, 53), fade(IM_COL32(242, 112, 26, 255), a));   // the hot diamond core
    dl->AddQuadFilled(P(51, 52), P(53, 55.5f), P(51, 60), P(49, 55.5f), fade(IM_COL32(205, 48, 22, 255), a));    // red at its root
  }
}

float drawLogo(ImDrawList* dl, ImVec2 p, float h, float a, bool wordmark) {
  Png& img = wordmark ? gFull : gMark;
  if (loadPng(img, wordmark ? "logo.png" : "logo_mark.png")) {   // the user's artwork (keyed to alpha) replaces the vector logo
    const float w = h * img.w / (float)img.h;
    dl->AddImage((ImTextureID)(intptr_t)img.tex, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255 * a)));
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

float logoImageAspect() { return loadPng(gFull, "logo.png") ? gFull.w / (float)gFull.h : 0.0f; }
