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

#define AT2_HUD_RETICLE 1
// AT2: the reflex (red dot) / holographic reticle of the weapon in hand: framebuffer pixel (sub-pixel, origin top-left)
// where the shot line meets the zero range, pixels per MOA there, kind 1 dot / 2 holographic, ring in dot radii (AK-12)
struct ReflexReticle { bool ok = false; int kind = 0; vec2 pos{0}; float pxMoa = 0, alpha = 0, ring = 0; vec3 col{1, 0, 0}; };
ReflexReticle reflexReticle(const App& app);
void drawReflexReticle(const App& app, ImDrawList* dl, float sx, float sy);   // after TAA, sized in angle (Settings > Gameplay)
