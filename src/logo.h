// UI2 Battle Foundry logo (logo.cpp): emblem + wordmark at p, h px tall; returns the width drawn. content/ui/logo.png,
// when present, replaces the vector logo.
#pragma once
#include <imgui.h>
float drawLogo(ImDrawList* dl, ImVec2 p, float h, float alpha = 1.0f, bool wordmark = true);
