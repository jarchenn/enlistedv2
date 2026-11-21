#pragma once
// VFX rev3 (spec 4.3 / 4.4): the map's authored road cross-sections and pools, for placement and shading that must agree
// with the road the ground was sculpted from (tools/mapgen relief.fine_track / track_soil). Loaded from the map file's
// "vfxRoads" ([{pts: [[x, z]...], w, trackOffset, rutSigma}]) and "vfxPools" ([{x, z, r, waterZ}]); empty elsewhere.
//
// CPU:
//   RoadQ roadLateral(x, z)   lat: signed lateral offset (m, + = left of travel) to the nearest road's centreline;
//                             w: its half-width; rutDist: distance to the nearer rut centre (at +-trackOffset);
//                             traffic: 1 - ss(0.53 w, 1.07 w, |lat|) (the spec's trail term); s: arc length (m)
//                             No road: lat = rutDist = 1e3, traffic 0.
//   float poolAt(x, z, groundY)   depth of standing water above groundY (m), 0 outside every pool.
// GLSL (kCommon, every lit program; the first road only, up to 64 points, and up to 16 pools):
//   vec4 roadLateral(vec2 xz)  -> (lat, rutDist, traffic, w); (1e3, 1e3, 0, 0) outside the road's box or with none.
//                              It loops over the road's segments: call it per vertex / per instance, not per pixel.
//   float poolAt(vec2 xz, float groundY)
#include "common.h"

struct RoadQ { float lat = 1e3f, w = 0.0f, rutDist = 1e3f, traffic = 0.0f, s = 0.0f; };
void roadsLoad(const json& mapExtra);
RoadQ roadLateral(float x, float z);
float poolAt(float x, float z, float groundY);
void roadsUniforms(GLuint prog);   // Renderer::setLighting
