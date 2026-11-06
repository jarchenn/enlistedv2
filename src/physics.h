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
  static uint32_t addPlayerOnly(const ShapePtr& s, vec3 pos, quat rot, uint64_t userData);   // blocks only the player (wire)
  static uint32_t addKinematic(const ShapePtr& s, vec3 pos, quat rot, uint64_t userData);
  static void setTransform(uint32_t body, vec3 pos, quat rot);
  // dynamic rigid bodies (detached shutters / doors, felled trunks)
  static uint32_t addDynamic(const ShapePtr& s, vec3 pos, quat rot, uint64_t userData, float massKg);
  static void addImpulse(uint32_t body, vec3 impulse, vec3 atPoint);
  static void setMaterial(uint32_t body, float friction, float restitution, bool continuous);   // thrown objects: bounce, CCD
  static void setVelocity(uint32_t body, vec3 linear, vec3 angular);
  static void setDamping(uint32_t body, float linear, float angular);
  static void addAngularImpulse(uint32_t body, vec3 angular);
  static bool getTransform(uint32_t body, vec3& pos, quat& rot);
  static bool isActive(uint32_t body);
  static vec3 velocity(uint32_t body);
  static void remove(uint32_t body);
  static int bodyCount();
  static void step(float dt);

  static bool castRay(vec3 o, vec3 dir, float maxDist, float& t, uint64_t& userData);
  struct Hit { float t = 0; vec3 pos{0}, normal{0, 1, 0}; uint64_t ud = 0; };
  // nearest solid contact; bodies whose user data == ignoreUd are skipped (e.g. the firing gun)
  static bool castRayEx(vec3 o, vec3 dir, float maxDist, Hit& hit, uint64_t ignoreUd = ~0ull);
  // ray against bodies with exactly this user data only (exit-point search for penetration)
  static bool castRayOnly(vec3 o, vec3 dir, float maxDist, uint64_t onlyUd, Hit& hit);
  static bool capsuleBlocked(vec3 feet, float height, float radius);
  static bool boxBlocked(vec3 center, vec3 halfExtents, quat rot, uint64_t ignoreUd = ~0ull, bool ignoreTerrain = true);
  // every body overlapping an oriented box, one entry per body (its deepest contact point, world space); tanks use it
  // to tell what they ran into (props to knock over, walls to push through, debris to drive over)
  struct Overlap { uint64_t ud = 0; uint32_t body = 0; vec3 pos{0}; float depth = 0; };
  static int boxOverlaps(vec3 center, vec3 halfExtents, quat rot, Overlap* out, int maxOut, uint64_t ignoreUd = ~0ull, bool ignoreTerrain = true);
  static ShapePtr buildCompound(const std::vector<ShapePtr>& shapes);
  static void setShape(uint32_t body, const ShapePtr& s);
  // dynamic terrain patches: holes in the base heightfield + per-tile high-res heightfields
  // base ground is one collision body per deformation tile; a deformed tile swaps it for a patch
  static void setBaseTile(int tx, int tz, bool on);
  static uint32_t addHeightPatch(const std::vector<float>& h, int n, vec3 origin, float spacing, uint64_t ud);

  // Character controller (Explore)
  enum Posture : int { STAND = 0, CROUCH = 1, PRONE = 2 };
  struct CharState { vec3 pos; bool grounded; bool crouched; uint64_t groundUser; int posture = STAND; float bodyYaw = 0; vec3 vel{0}; };
  // player-only tuning (global rigid-body / projectile gravity is separate and unchanged)
  // jumpBuffer: a press this long before landing still jumps on touchdown; coyote: a press this long after the
  // last real (settled, walkable) ground contact still jumps (slope / step-up ground flicker, walking off an edge)
  struct MoveTuning { float gravity = 9.81f * 0.8f, jumpVelocity = 4.2f * 0.894427f, jumpBuffer = 0.15f, coyote = 0.12f; };
  static MoveTuning tuning;
  static void createCharacter(vec3 feet);
  static void destroyCharacter();
  static bool hasCharacter();
  static void updateCharacter(float dt, vec3 desiredHorizVel, bool jump, bool wantCrouch);
  // posture-aware update: tries the requested posture (stays in the current one when the target shape does
  // not fit), turns the prone body toward bodyYawWanted only when the rotated body fits
  // returns true on the step a jump impulse is applied (buffered press or coyote window included)
  static bool updateCharacter(float dt, vec3 desiredHorizVel, bool jump, int wantPosture, float bodyYawWanted);
  static int jumpCount();   // jump impulses applied since the character was created (self-tests)
  static bool postureFits(int posture, float bodyYaw);
  static bool shapeFitsAt(int posture, vec3 feet, bool withTerrain = true);   // posture body at an arbitrary position
  static uint64_t shapeHitUD(int posture, vec3 feet, bool withTerrain = true);  // debug: user data of a body it overlaps (0 = none)
  static float proneHalfLength() { return 0.85f; }
  static float proneRadius() { return 0.25f; }
  static void setCharacterPos(vec3 feet);
  static CharState character();
  static float standHeight() { return 1.8f; }
  static float crouchHeight() { return 1.2f; }
  static float radius() { return 0.3f; }

  // ---- agents (multiplayer + AI, physics_agents block in physics.cpp): one CharacterVirtual per bot / remote player
  // with the player's shapes and step rules; agents and the player collide with each other.
  static int createAgent(vec3 feet);
  static void destroyAgent(int h);
  static void destroyAllAgents();
  static void updateAgent(int h, float dt, vec3 desiredHorizVel, bool jump, int wantPosture, float bodyYaw);
  static CharState agent(int h);
  static void setAgentPos(int h, vec3 feet);
  // every hit along a ray, nearest first (bodies, terrain); skipLeaves drops movable door / window leaves
  static int castRayAll(vec3 o, vec3 dir, float maxDist, Hit* out, int maxHits, bool skipLeaves);
};
