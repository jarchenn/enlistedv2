// UI4 flat "options" language: see ui_flat.h.
#include "ui_flat.h"
#include "ui_glass.h"
#include "app.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace flat {
namespace {
ImU32 col(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::clamp(a * 255.0f, 0.0f, 255.0f)); }
std::string upper(const char* s) {   // the visible part of an ImGui label ("Text##id"), in capitals
  std::string t(s); size_t h = t.find("##"); if (h != std::string::npos) t.resize(h);
  for (auto& c : t) c = (char)std::toupper((unsigned char)c);
  return t;
}
float rowH() { return std::round(ImGui::GetFontSize() * 2.0f); }
float labelPx() { return ImGui::GetFontSize() * 0.92f; }
// the row's label at the left; returns the control column [x0, x1)
void rowLabel(const char* label, ImVec2 p, float& x0, float& x1) {
  const float W = ImGui::GetContentRegionAvail().x, h = rowH();
  x0 = p.x + std::max(W * 0.48f, 180.0f); x1 = p.x + W;
  std::string t = upper(label);
  if (!t.empty()) { ImFont* f = ImGui::GetFont(); const float px = labelPx(); text(ImGui::GetWindowDrawList(), f, px, ImVec2(p.x, p.y + (h - px) * 0.5f), ink(0.92f), t.c_str(), 0.09f); }
}
void chevron(ImDrawList* dl, ImVec2 c, float s, bool right, ImU32 cl) {
  const float d = right ? 1.0f : -1.0f;
  dl->AddLine(ImVec2(c.x - d * s * 0.35f, c.y - s * 0.6f), ImVec2(c.x + d * s * 0.35f, c.y), cl, 1.6f);
  dl->AddLine(ImVec2(c.x + d * s * 0.35f, c.y), ImVec2(c.x - d * s * 0.35f, c.y + s * 0.6f), cl, 1.6f);
}
// "<  VALUE  >": the arrows step, a click on the value steps on; true when it changed
bool stepper(const char* label, const char* value, int& dir) {
  ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return false;
  ImDrawList* dl = w->DrawList; const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = rowH();
  float x0, x1; rowLabel(label, p, x0, x1);
  ImGui::PushID(label); dir = 0;
  ImGui::SetCursorScreenPos(ImVec2(x0, p.y)); ImGui::InvisibleButton("##l", ImVec2(h, h)); const bool hl = ImGui::IsItemHovered(); if (ImGui::IsItemClicked()) dir = -1;
  ImGui::SetCursorScreenPos(ImVec2(x1 - h, p.y)); ImGui::InvisibleButton("##r", ImVec2(h, h)); const bool hr = ImGui::IsItemHovered(); if (ImGui::IsItemClicked()) dir = 1;
  ImGui::SetCursorScreenPos(ImVec2(x0 + h, p.y)); ImGui::InvisibleButton("##v", ImVec2(std::max(1.0f, x1 - x0 - 2 * h), h)); const bool hv = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) dir = 1; else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) dir = -1;
  ImGui::PopID();
  const float cy = p.y + h * 0.5f, s = ImGui::GetFontSize() * 0.55f;
  chevron(dl, ImVec2(x0 + h * 0.5f, cy), s, false, hl ? gold() : ink(0.75f));
  chevron(dl, ImVec2(x1 - h * 0.5f, cy), s, true, hr ? gold() : ink(0.75f));
  std::string v = upper(value); ImFont* f = ImGui::GetFont(); const float px = labelPx();
  const float tw = textW(f, px, v.c_str(), 0.08f);
  text(dl, f, px, ImVec2((x0 + x1) * 0.5f - tw * 0.5f, cy - px * 0.5f), hv ? gold() : ink(), v.c_str(), 0.08f);
  return dir != 0;
}
}

ImU32 ink(float a) { return col(234, 230, 222, a); }
ImU32 grey(float a) { return col(150, 148, 142, a); }
ImU32 rust(float a) { return col(166, 92, 52, a); }
ImU32 gold(float a) { return col(206, 172, 118, a); }
ImFont* titleFont() { return uiFontTitle(); }

void backdrop(ImDrawList* dl, ImVec2 mn, ImVec2 mx) { uiGlassFlat(dl, mn, mx, 0.62f); }

float textW(ImFont* f, float px, const char* s, float trackEm) {
  float w = 0; const float tr = px * trackEm; for (const char* c = s; *c; ++c) w += f->CalcTextSizeA(px, 1e9f, 0, c, c + 1).x + tr;
  return *s ? w - tr : 0;
}
float text(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 c, const char* s, float trackEm) {
  const float tr = px * trackEm, x0 = p.x;
  for (const char* ch = s; *ch; ++ch) { dl->AddText(f, px, p, c, ch, ch + 1); p.x += f->CalcTextSizeA(px, 1e9f, 0, ch, ch + 1).x + tr; }
  return p.x - x0 - (*s ? tr : 0);
}
void rule(ImDrawList* dl, float x0, float x1, float y) { dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1.0f), rust(0.85f)); }

bool tabs(const char* const* names, int n, int& cur) {
  ImDrawList* dl = ImGui::GetWindowDrawList(); ImFont* f = uiFontHead(); const float px = ImGui::GetFontSize() * 1.25f;
  ImVec2 p = ImGui::GetCursorScreenPos(); const float h = px * 1.6f; bool changed = false;
  for (int k = 0; k < n; ++k) {
    std::string t = upper(names[k]); const float w = textW(f, px, t.c_str(), 0.06f);
    ImGui::SetCursorScreenPos(p); ImGui::PushID(k); ImGui::InvisibleButton("##tab", ImVec2(w, h)); ImGui::PopID();
    const bool hov = ImGui::IsItemHovered(); if (ImGui::IsItemClicked() && cur != k) { cur = k; changed = true; }
    text(dl, f, px, ImVec2(p.x, p.y + (h - px) * 0.45f), k == cur ? gold() : hov ? ink(0.9f) : grey(), t.c_str(), 0.06f);
    if (k == cur) dl->AddRectFilled(ImVec2(p.x, p.y + h - 2), ImVec2(p.x + w, p.y + h - 1), gold());
    p.x += w;
    if (k + 1 < n) { const float sw = textW(f, px, "  //  ", 0.0f); text(dl, f, px, ImVec2(p.x, p.y + (h - px) * 0.45f), grey(0.55f), "  //  ", 0.0f); p.x += sw; }
  }
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorStartPos().x + ImGui::GetWindowPos().x - ImGui::GetScrollX(), p.y)); ImGui::Dummy(ImVec2(1, h));
  return changed;
}

void section(const char* name) {
  ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() * 0.5f));
  ImDrawList* dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); const float px = ImGui::GetFontSize() * 0.8f;
  std::string t = upper(name); const float w = text(dl, uiFontHead(), px, p, grey(), t.c_str(), 0.14f);
  dl->AddRectFilled(ImVec2(p.x + w + 12, p.y + px * 0.55f), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + px * 0.55f + 1), grey(0.25f));
  ImGui::Dummy(ImVec2(1, px * 1.3f));
}

bool combo(const char* label, int* cur, const char* const items[], int n) {
  if (n <= 0) return false;
  const int c = std::clamp(*cur, 0, n - 1); int dir;
  if (!stepper(label, items[c], dir)) return false;
  *cur = (c + dir + n) % n; return true;
}
bool checkbox(const char* label, bool* v) {
  int dir;
  if (!stepper(label, *v ? "On" : "Off", dir)) return false;
  *v = !*v; return true;
}

static bool sliderCore(const char* label, double& v, double lo, double hi, const char* valueText, bool logScale, bool integer) {
  ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return false;
  ImDrawList* dl = w->DrawList; const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = rowH();
  float x0, x1; rowLabel(label, p, x0, x1);
  ImFont* f = ImGui::GetFont(); const float px = labelPx();
  const float valW = std::max(ImGui::GetFontSize() * 5.0f, textW(f, px, valueText, 0.04f) + 10), tx0 = x0 + valW, tx1 = x1 - 4;
  ImGui::SetCursorScreenPos(ImVec2(x0, p.y)); ImGui::PushID(label); ImGui::InvisibleButton("##s", ImVec2(std::max(1.0f, x1 - x0), h)); ImGui::PopID();
  const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive(); bool changed = false;
  auto toT = [&](double x) { return logScale && lo > 0 ? std::log(x / lo) / std::log(hi / lo) : (x - lo) / (hi - lo); };
  auto fromT = [&](double t) { return logScale && lo > 0 ? lo * std::pow(hi / lo, t) : lo + t * (hi - lo); };
  if (act && ImGui::GetIO().MousePos.x >= tx0 - 8) {
    double t = std::clamp((double)(ImGui::GetIO().MousePos.x - tx0) / std::max(1.0f, tx1 - tx0), 0.0, 1.0), nv = fromT(t);
    if (integer) nv = std::round(nv);
    if (nv != v) { v = nv; changed = true; ImGui::MarkItemEdited(ImGui::GetItemID()); }
  }
  if (hov && !act && ImGui::GetIO().MouseWheel != 0) {   // the wheel nudges by 1 %
    double nv = fromT(std::clamp(toT(v) + ImGui::GetIO().MouseWheel * 0.01, 0.0, 1.0)); if (integer) nv = std::round(nv);
    if (nv != v) { v = nv; changed = true; }
  }
  const float cy = p.y + h * 0.5f, t = (float)std::clamp(toT(v), 0.0, 1.0), hx = tx0 + (tx1 - tx0) * t, hs = 5.0f;
  text(dl, f, px, ImVec2(x0, cy - px * 0.5f), act || hov ? gold() : ink(), valueText, 0.04f);
  dl->AddRectFilled(ImVec2(tx0, cy - 0.5f), ImVec2(tx1, cy + 0.5f), ink(0.35f));
  dl->AddRectFilled(ImVec2(tx0, cy - 0.5f), ImVec2(hx, cy + 0.5f), ink(0.85f));
  dl->AddRectFilled(ImVec2(hx - hs, cy - hs), ImVec2(hx + hs, cy + hs), act ? gold() : ink());
  return changed;
}
bool sliderFloat(const char* label, float* v, float lo, float hi, const char* fmt, ImGuiSliderFlags flags) {
  char buf[64]; std::snprintf(buf, sizeof buf, fmt && *fmt ? fmt : "%.2f", *v);
  double d = *v; const bool ch = sliderCore(label, d, lo, hi, buf, (flags & ImGuiSliderFlags_Logarithmic) != 0, false);
  if (ch) *v = (float)d; return ch;
}
bool sliderInt(const char* label, int* v, int lo, int hi, const char* fmt) {
  char buf[64]; std::snprintf(buf, sizeof buf, fmt && *fmt ? fmt : "%d", *v);
  double d = *v; const bool ch = sliderCore(label, d, lo, hi, buf, false, true);
  if (ch) *v = (int)d; return ch;
}

bool keyRow(const char* label, const char* key, bool waiting) {
  ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return false;
  const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = rowH(); float x0, x1; rowLabel(label, p, x0, x1);
  ImGui::SetCursorScreenPos(ImVec2(x0, p.y)); ImGui::PushID(label); ImGui::InvisibleButton("##k", ImVec2(std::max(1.0f, x1 - x0), h)); ImGui::PopID();
  const bool hov = ImGui::IsItemHovered(), hit = ImGui::IsItemClicked();
  std::string v = waiting ? std::string("PRESS A KEY") : upper(key); ImFont* f = ImGui::GetFont(); const float px = labelPx();
  const float tw = textW(f, px, v.c_str(), 0.08f), cx = (x0 + x1) * 0.5f, cy = p.y + h * 0.5f;
  text(w->DrawList, f, px, ImVec2(cx - tw * 0.5f, cy - px * 0.5f), waiting || hov ? gold() : ink(), v.c_str(), 0.08f);
  w->DrawList->AddRectFilled(ImVec2(x0 + h, p.y + h - 6), ImVec2(x1 - h, p.y + h - 5), waiting ? gold(0.8f) : ink(hov ? 0.5f : 0.18f));
  return hit;
}
bool action(const char* label) {
  ImDrawList* dl = ImGui::GetWindowDrawList(); ImFont* f = uiFontHead(); const float px = ImGui::GetFontSize() * 1.15f;
  std::string t = upper(label); const float w = textW(f, px, t.c_str(), 0.08f), h = px * 1.5f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(label, ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered(), hit = ImGui::IsItemClicked();
  text(dl, f, px, ImVec2(p.x, p.y + (h - px) * 0.4f), hov ? gold() : ink(), t.c_str(), 0.08f);
  if (hov) dl->AddRectFilled(ImVec2(p.x, p.y + h - 2), ImVec2(p.x + w, p.y + h - 1), gold());
  return hit;
}
void note(const char* s) {
  ImDrawList* dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); const float px = ImGui::GetFontSize() * 0.82f;
  std::string t = upper(s); text(dl, uiFontHead(), px, p, grey(), t.c_str(), 0.1f); ImGui::Dummy(ImVec2(1, px * 1.4f));
}
}  // namespace flat
