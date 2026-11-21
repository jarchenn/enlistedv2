#pragma once
// VFX rev3 spec 4.4 / reference 04 s4.9: the camera-anchored ground deformation buffer (tdeform). One RG16F texture,
// toroidally scrolled with the camera: R = depression depth in metres (0..0.35), G = disturbance 0..1 (clears grass,
// moss and litter). Visual only: no collision, nothing returned to gameplay. Persistent craters stay in the terrain's
// deformation tiles; this holds the transient marks (tracks, footprints, small craters, spade trenches) and the
// vegetation-kill ring round the big craters.
//
// Write (main thread; queued, flushed once per frame by the renderer; a stamp outside the current window is dropped):
//   deformStamp(p0, p1, radius, depth, disturbance)   a soft capsule: depth * (1 - (d/r)^2)^1.6, d = distance to the
//                                                      p0-p1 segment in xz; combined with max(), so repeat passes do not
//                                                      dig past `depth`; depth 0 with disturbance 1 is a kill ring
// Read (GLSL, every lit program through Renderer::setLighting; texture unit tu::kDeform):
//   uniform sampler2D uDefTex; uniform vec4 uDefMap;   // xy window origin (m, world xz), z 1 / window size (m), w texel (m)
//   uniform int uDefOn;                                  // 0: off (the tier, the switch or no map)
//   vec2 v = texture(uDefTex, xz * uDefMap.z).rg       // GL_REPEAT: the window wraps; outside it reads 0
// Decay: R *= exp(-dt / tauR), G *= exp(-dt / tauG); tauR 300 s (soil, spec 120-600), tauG 1800 s.
// Tiers (spec 8.2): Ultra Low / Low off; Medium 8 cm over 48 m; High 4 cm over 64 m; Ultra 4 cm over 96 m.
#include "common.h"

void deformStamp(vec3 p0, vec3 p1, float radius, float depth, float disturbance);
float deformDepthAtCPU(float x, float z);   // 0 (the buffer is GPU-only); kept so callers have one place to ask later
