// UI2 liquid glass (ui_glass.h).
#include "ui_glass.h"
#include "mainmenu.h"
#include "app.h"
#include "settings.h"
#include <imgui_internal.h>
#include <unordered_map>
#include <algorithm>
#include <cstring>

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
    r = std::min(r, 2.0f);   // UI4: flat, square panels (1-2 px), darker smoke, no sheen or bright edge
    dl->AddImageRounded((ImTextureID)(intptr_t)gView, mn, mx, uv(mn), uv(mx), rgba(205, 200, 194, a), r);
    dl->AddRectFilled(mn, mx, rgba(8, 8, 8, 0.62f * a), r);
  } else dl->AddRectFilled(mn, mx, rgba(12, 12, 12, 0.88f * a), std::min(r, 2.0f));   // Low: a tinted panel
  dl->AddRect(mn, mx, rgba(255, 255, 255, 0.06f * a), std::min(r, 2.0f));   // a faint hairline, not an edge light
}

// UI4: a window's glass goes under its own content, in its own draw list (prepended), so a window in front covers the
// windows behind it instead of letting them show through its translucent background
static void prependGlass(ImDrawList* dst, ImVec2 mn, ImVec2 mx, float r, float a) {
  static ImDrawList* tmp = nullptr;
  if (!tmp) tmp = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
  tmp->_ResetForNewFrame(); tmp->PushTextureID(ImGui::GetIO().Fonts->TexID); tmp->PushClipRectFullScreen();
  uiGlassPanel(tmp, mn, mx, r, a);
  tmp->PopClipRect(); tmp->PopTextureID();
  while (!tmp->CmdBuffer.empty() && tmp->CmdBuffer.back().ElemCount == 0) tmp->CmdBuffer.pop_back();
  if (tmp->CmdBuffer.empty()) return;
  const unsigned nv = (unsigned)tmp->VtxBuffer.Size, ni = (unsigned)tmp->IdxBuffer.Size;
  for (ImDrawCmd& c : dst->CmdBuffer) { c.VtxOffset += nv; c.IdxOffset += ni; }   // the window's own commands move up
  dst->VtxBuffer.resize(dst->VtxBuffer.Size + (int)nv); std::memmove(dst->VtxBuffer.Data + nv, dst->VtxBuffer.Data, (size_t)(dst->VtxBuffer.Size - nv) * sizeof(ImDrawVert)); std::memcpy(dst->VtxBuffer.Data, tmp->VtxBuffer.Data, nv * sizeof(ImDrawVert));
  dst->IdxBuffer.resize(dst->IdxBuffer.Size + (int)ni); std::memmove(dst->IdxBuffer.Data + ni, dst->IdxBuffer.Data, (size_t)(dst->IdxBuffer.Size - ni) * sizeof(ImDrawIdx)); std::memcpy(dst->IdxBuffer.Data, tmp->IdxBuffer.Data, ni * sizeof(ImDrawIdx));
  dst->CmdBuffer.reserve(dst->CmdBuffer.Size + tmp->CmdBuffer.Size);
  for (int k = tmp->CmdBuffer.Size - 1; k >= 0; --k) dst->CmdBuffer.push_front(tmp->CmdBuffer[k]);
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
      v.col = (v.col & 0x00ffffffu) | ((ImU32)((float)(v.col >> 24) * e) << 24);   // UI4: a plain fade (no rise / bounce)
    }
    if (w != root || (w->Flags & ImGuiWindowFlags_NoBackground) || w->IsFallbackWindow) continue;   // the implicit "Debug" window: active, empty
    const float r = (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ? st.PopupRounding : (w->Flags & ImGuiWindowFlags_MenuBar) && w->Size.y < 60 ? 0.0f : w->WindowRounding;
    ImVec2 mn = w->Pos, mx(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
    if (!(w->Flags & ImGuiWindowFlags_Tooltip))
      {}   // UI4: no drop shadow (flat)
    prependGlass(w->DrawList, mn, mx, r, e);
  }
  for (auto it = seen.begin(); it != seen.end();) { if (it->second != frame) { born.erase(it->first); it = seen.erase(it); } else ++it; }   // closed: the next opening fades in again
}

void uiGlassFlat(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float darkness) {
  const ImVec2 io = ImGui::GetIO().DisplaySize;
  if (mainMenuActive()) { if (unsigned t = menuBackdropTex(true)) dl->AddImage((ImTextureID)(intptr_t)t, mn, mx, ImVec2(mn.x / io.x, mn.y / io.y), ImVec2(mx.x / io.x, mx.y / io.y), rgba(214, 206, 196, 1)); else dl->AddRectFilled(mn, mx, rgba(10, 10, 10, 1)); }
  else if (gHave) dl->AddImage((ImTextureID)(intptr_t)gView, mn, mx, ImVec2(mn.x / io.x, 1.0f - mn.y / io.y), ImVec2(mx.x / io.x, 1.0f - mx.y / io.y), rgba(214, 206, 196, 1));
  else dl->AddRectFilled(mn, mx, rgba(14, 13, 12, 1));
  dl->AddRectFilled(mn, mx, rgba(8, 7, 6, darkness));
}
