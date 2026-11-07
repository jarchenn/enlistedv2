#pragma once
#include <glm/glm.hpp>
#include <cstdint>
// Local light shared by the light data (lights.cpp, L3) and the lighting engine (lighting.cpp, L2).
struct LightDesc {
  glm::vec3 pos{0}; float radius = 6.0f;                       // m: falls to zero at radius
  glm::vec3 color{1.0f, 0.75f, 0.45f}; float intensity = 1.0f; // linear RGB; 1 = a 60 W bulb seen at 1 m
  glm::vec3 dir{0, -1, 0}; float cosInner = -2.0f, cosOuter = -2.0f;   // spot cone; cosOuter <= -1: point light
  float flicker = 0.0f;   // 0 steady .. 1 strong fire flicker
  int shadow = 0;         // 1 = may get a shadow map
  uint32_t id = 0;        // stable per light (instance id * 16 + light index)
};
