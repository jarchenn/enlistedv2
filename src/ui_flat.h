// UI4 flat "options" language (ui_flat.cpp): sober full-page layouts instead of rounded cards. Off-white on near-black,
// one muted copper accent, the active state gold-tan; heavy condensed uppercase titles, wide-tracked uppercase labels;
// rows with the label at the left and the control in a fixed column: "<  VALUE  >" steppers for choices and on / off,
// a value then a thin line track with a small square handle for sliders; "A // B // C" tabs; text actions with a hover
// underline. The widgets keep ImGui's call shapes (return true on change, the control is the last item, so tooltips
// and IsItemDeactivatedAfterEdit still work).
#pragma once
#include <imgui.h>

namespace flat {
ImU32 ink(float a = 1.0f);    // off-white text
ImU32 grey(float a = 1.0f);   // inactive / hints
ImU32 rust(float a = 1.0f);   // the muted copper accent (rules)
ImU32 gold(float a = 1.0f);   // the active state
ImFont* titleFont();          // Barlow Condensed Bold, large
void backdrop(ImDrawList* dl, ImVec2 mn, ImVec2 mx);   // the page's dark, blurred scene (the menu's backdrop or the frame's blur)
float text(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 c, const char* s, float trackEm);   // tracked uppercase; returns the width
float textW(ImFont* f, float px, const char* s, float trackEm);
void rule(ImDrawList* dl, float x0, float x1, float y);   // a thin copper rule
bool tabs(const char* const* names, int n, int& cur, float scale = 1.9f);   // at the cursor; true when it changed (scale: text px / font size)
void section(const char* name);                          // a small grey heading over a group of rows
bool combo(const char* label, int* cur, const char* const items[], int n);
bool checkbox(const char* label, bool* v);
bool sliderFloat(const char* label, float* v, float lo, float hi, const char* fmt, ImGuiSliderFlags flags = 0);
bool sliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d");
bool keyRow(const char* label, const char* key, bool waiting);   // a keybinding row: the key in the control column; true when clicked
bool action(const char* label);                          // a text button (BACK, APPLY SETTINGS ...); SameLine between
void note(const char* s);                                // a grey line of small caps
}
