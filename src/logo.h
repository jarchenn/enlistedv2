// UI2 Battle Foundry logo (logo.cpp): emblem + wordmark at p, h px tall; returns the width drawn. content/ui/logo.png,
// when present, replaces the vector logo.
#pragma once
#include <imgui.h>
float drawLogo(ImDrawList* dl, ImVec2 p, float h, float alpha = 1.0f, bool wordmark = true);   // wordmark false: the mark only (content/ui/logo_mark.png)
float logoImageAspect();   // width / height of content/ui/logo.png, 0 when it is missing (the vector logo is drawn)
