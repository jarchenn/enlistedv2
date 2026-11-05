#pragma once
#include "assets.h"

// Thin wrapper over Jolt so the rest of the game never includes Jolt headers.
// Body user data encodes (instance id << 16) | (leaf slot + 1); 0 = terrain.
class Physics {
public:
  static bool init();
  static void shutdown();
  static ShapePtr buildShape(const std::vector<Part>& boxes);

  static void setTerrain(const std::vector<float>& heights, int n, float size);
  static uint32_t addStatic(const ShapePtr& s, vec3 pos, quat rot, uint64_t userData);
  static uint32_t addKinematic(const ShapePtr& s, vec3 pos, quat rot, uint64_t userData);
  static void setTransform(uint32_t body, vec3 pos, quat rot);
  static void remove(uint32_t body);
  static int bodyCount();
  static void step(float dt);

  static bool castRay(vec3 o, vec3 dir, float maxDist, float& t, uint64_t& userData);
  static bool capsuleBlocked(vec3 feet, float height, float radius);

  // Character controller (Explore)
  struct CharState { vec3 pos; bool grounded; bool crouched; uint64_t groundUser; };
  static void createCharacter(vec3 feet);
  static void destroyCharacter();
  static bool hasCharacter();
  static void updateCharacter(float dt, vec3 desiredHorizVel, bool jump, bool wantCrouch);
  static void setCharacterPos(vec3 feet);
  static CharState character();
  static float standHeight() { return 1.8f; }
  static float crouchHeight() { return 1.2f; }
  static float radius() { return 0.3f; }
};
