// UI2 liquid glass (ui_glass.h).
#include "ui_glass.h"
#include "mainmenu.h"
#include "app.h"
#include "settings.h"
#include <imgui_internal.h>
#include <unordered_map>
#include <algorithm>

namespace {
GLuint gFull = 0, gView = 0, gFbo = 0; int gW = 0, gH = 0; bool gHave = false;
const int kBlurLevel = 4;   // 1/16 size: each texel the mean of 16 x 16 screen pixels, magnified bilinearly
bool lowPreset() { const GraphicsOpts& g = settings().s.gfx; return g == presetOpts(GfxPreset::Low) || g == presetOpts(GfxPreset::UltraLow); }
inline ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::clamp(a * 255.0f, 0.0f, 255.0f)); }
}

void uiGlassCapture(int fbw, int fbh) {
  gHave = false;
  if (fbw < 64 || fbh < 64 || lowPreset()) return;
  if (!gFull || fbw != gW || fbh != gH) {
    if (gView) glDeleteTextures(1, &gView);
    if (gFull) glDeleteTextures(1, &gFull);
    if (gFbo) glDeleteFramebuffers(1, &gFbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &gFull); glTextureStorage2D(gFull, kBlurLevel + 1, GL_RGBA8, fbw, fbh);
    glCreateFramebuffers(1, &gFbo); glNamedFramebufferTexture(gFbo, GL_COLOR_ATTACHMENT0, gFull, 0);
    glGenTextures(1, &gView); glTextureView(gView, GL_TEXTURE_2D, gFull, GL_RGBA8, kBlurLevel, 1, 0, 1);
    glTextureParameteri(gView, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gView, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(gView, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gView, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gW = fbw; gH = fbh;
  }
  glBlitNamedFramebuffer(0, gFbo, 0, 0, fbw, fbh, 0, 0, fbw, fbh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glGenerateTextureMipmap(gFull);
  gHave = true;
}

void uiGlassPanel(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float r, float a) {
  if (mainMenuActive()) { menuGlass(dl, mn, mx, a); return; }   // over the menu: its own backdrop, blurred
  if (gHave) {
    const ImVec2 io = ImGui::GetIO().DisplaySize;
    auto uv = [&](ImVec2 p) { return ImVec2(p.x / io.x, 1.0f - p.y / io.y); };   // the copy is bottom-up
    dl->AddImageRounded((ImTextureID)(intptr_t)gView, mn, mx, uv(mn), uv(mx), rgba(222, 228, 232, a), r);
    dl->AddRectFilled(mn, mx, rgba(10, 13, 16, 0.40f * a), r);   // dark smoke tint
  } else dl->AddRectFilled(mn, mx, rgba(16, 19, 22, 0.84f * a), r);   // Low: a tinted panel
  dl->AddRectFilledMultiColor(ImVec2(mn.x + r * 0.6f, mn.y + 1), ImVec2(mx.x - r * 0.6f, mn.y + std::min(40.0f, (mx.y - mn.y) * 0.4f)),
                              rgba(255, 255, 255, 0.045f * a), rgba(255, 255, 255, 0.045f * a), rgba(255, 255, 255, 0), rgba(255, 255, 255, 0));   // gradient sheen
  dl->AddRect(ImVec2(mn.x + 0.5f, mn.y + 0.5f), ImVec2(mx.x - 0.5f, mx.y - 0.5f), rgba(255, 255, 255, 0.13f * a), r);   // bright inner edge
  dl->AddLine(ImVec2(mn.x + r, mn.y + 1.0f), ImVec2(mx.x - r, mn.y + 1.0f), rgba(255, 255, 255, 0.26f * a));
}

void uiGlassFrame() {
  ImGuiContext& g = *GImGui;
  static std::unordered_map<ImGuiID, double> born; static std::unordered_map<ImGuiID, int> seen; static int frame = 0;
  ++frame;
  const double now = ImGui::GetTime();
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  const ImGuiStyle& st = ImGui::GetStyle();
  for (ImGuiWindow* w : g.Windows) {
    if (!w->Active || w->Hidden) continue;
    ImGuiWindow* root = w->RootWindow ? w->RootWindow : w;
    if (w == root) { if (!born.count(w->ID)) born[w->ID] = now; seen[w->ID] = frame; }
    const float age = (float)(now - (born.count(root->ID) ? born[root->ID] : now));
    const float k = std::clamp(age / 0.16f, 0.0f, 1.0f), e = 1 - (1 - k) * (1 - k) * (1 - k);
    if (k < 1) for (ImDrawVert& v : w->DrawList->VtxBuffer) {   // fade and rise in over 160 ms
      v.col = (v.col & 0x00ffffffu) | ((ImU32)((float)(v.col >> 24) * e) << 24); v.pos.y += (1 - e) * 8.0f;
    }
    if (w != root || (w->Flags & ImGuiWindowFlags_NoBackground) || w->IsFallbackWindow) continue;   // the implicit "Debug" window: active, empty
    const float r = (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ? st.PopupRounding : (w->Flags & ImGuiWindowFlags_MenuBar) && w->Size.y < 60 ? 0.0f : w->WindowRounding;
    ImVec2 mn = w->Pos, mx(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
    mn.y += (1 - e) * 8.0f; mx.y += (1 - e) * 8.0f;
    if (!(w->Flags & ImGuiWindowFlags_Tooltip))
      for (int i = 1; i <= 3; ++i) { const float gr = i * 4.0f; bg->AddRectFilled(ImVec2(mn.x - gr, mn.y - gr + 6), ImVec2(mx.x + gr, mx.y + gr + 6), rgba(0, 0, 0, 0.07f * e * (4 - i) / 3.0f), r + gr); }   // soft shadow
    uiGlassPanel(bg, mn, mx, r, e);
  }
  for (auto it = seen.begin(); it != seen.end();) { if (it->second != frame) { born.erase(it->first); it = seen.erase(it); } else ++it; }   // closed: the next opening fades in again
}
