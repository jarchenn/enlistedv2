// UI2 "liquid glass" (ui_glass.cpp): frosted translucent panels for every ImGui window and the custom HUD panels.
// The finished scene of the frame is copied to a mipmapped texture after the 3D render; panels show its 1/16-size
// level (a box-filtered blur, steady while the view moves) under a smoky tint, a bright inner edge and a soft shadow.
// Over the main menu the menu's own blurred backdrop is used. Low / Ultra Low presets: tinted panels, no copy.
#pragma once
#include <imgui.h>
void uiGlassCapture(int fbw, int fbh);   // after the 3D render, before the UI is drawn (the frame's blur source)
void uiGlassFrame();                     // just before ImGui::Render: glass under every window, 160 ms fade-in of new windows
void uiGlassPanel(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float alpha = 1.0f);   // a custom panel's background
