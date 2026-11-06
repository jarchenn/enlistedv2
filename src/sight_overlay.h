#pragma once
// W2 (MP & Creative update, spec 8.3): sight-alignment check for any weapon. Projects the camera centre and the
// calibrated sight points (rear aperture / notch, front tip) through the weapon projection and measures the roll of
// the drawn weapon about the view axis. Dev overlay: Loadout (L) -> "Sight calibration markers". See docs/fps-weapons.md.
#include "common.h"

class App;
struct ImDrawList;

struct SightCheck {
  bool ok = false;
  vec2 centre{0}, rear{0}, front{0};   // framebuffer pixels, origin top-left
  float rearErr = 0, frontErr = 0;     // px from the centre
  float rollDeg = 0;                   // weapon +y axis against the screen vertical (+ = leaning right / clockwise)
};

// active weapon, this frame (Explore only)
SightCheck sightCheck(const App& app);
// HUD layer: centre crosshair, lines from the centre to the projected rear / front points, roll and pixel readout
void drawSightOverlay(const App& app, ImDrawList* dl, float sx, float sy);
