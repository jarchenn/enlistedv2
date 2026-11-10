#pragma once
// Skinned soldier characters (content/characters/*.glb, baked by tools/bake_characters.py from the user's GLBs).
// The GLBs carry a skeleton but no animation clips, so poses are procedural (pose.cpp): rotations in model space
// about each joint, accumulated down the hierarchy, with two-bone arm IK onto the held weapon's grip sockets.
// Skinning runs in a compute shader that writes each soldier's own vertex buffer, which the existing model pass
// (Renderer::worldModels: shading + shadows) draws unchanged.
#include "model.h"
#include "anim_clip.h"

// Model space: metres, y up, the soldier faces +z, its left is +x, feet at y = 0.
enum CharJoint : int {
  CJ_HIPS = 0, CJ_SPINE, CJ_SPINE1, CJ_SPINE2, CJ_NECK, CJ_HEAD,
  CJ_LSHOULDER, CJ_LARM, CJ_LFOREARM, CJ_LHAND,
  CJ_RSHOULDER, CJ_RARM, CJ_RFOREARM, CJ_RHAND,
  CJ_LUPLEG, CJ_LLEG, CJ_LFOOT, CJ_LTOE,
  CJ_RUPLEG, CJ_RLEG, CJ_RFOOT, CJ_RTOE,
  CJ_COUNT
};

struct CharModel {
  std::string id;
  // skeleton (all scene nodes on the joint paths; joints index into these)
  std::vector<std::string> nodeName;
  std::vector<int> nodeParent;
  std::vector<mat4> nodeLocal, nodeWorld;   // rest
  std::vector<int> jointNode;               // skin joint -> node
  std::vector<mat4> invBind;
  std::vector<int> jointParent;             // skin joint -> parent skin joint (-1 root)
  std::vector<vec3> jointHead;              // rest head position (model space)
  int semantic[CJ_COUNT];                   // CharJoint -> skin joint (-1 missing)
  std::vector<int> nodeOrder;               // parents before children
  mat4 rootFix{1};                          // applied above the root nodes (load with a height: scale / recentre)
  std::vector<AnimClip> clips;              // G2: the GLB's own clips (anim_clip.h), empty for the soldiers
  // geometry
  struct SkinVert { vec3 pos; float pad0; vec3 nrm; float pad1; vec2 uv; vec2 pad2; uint32_t j[4]; vec4 w; };
  std::vector<SkinVert> verts;
  std::vector<uint32_t> indices;            // CPU copy of the index buffer (gore: mesh ray casts for wound decals)
  GpuModel gpu;                             // shared index buffer, textures, parts (vao/vbo hold the bind pose)
  GLuint restSsbo = 0;
  vec3 bmin{0}, bmax{0};
  bool load(const fs::path& glb, std::string& err, float height = 0, bool body = true);   // height > 0: scaled to it, feet at 0, centred; body false: no humanoid joints required (FA1 first-person arms)
  void destroy();
  int joints() const { return (int)jointNode.size(); }
};

// One skinned copy (one soldier): its own vertex buffer, sharing everything else with the model.
struct CharInstance {
  const CharModel* model = nullptr;
  GpuModel view;                            // vao + own vbo; ebo / mats / parts copied from the model
  GLuint jointSsbo = 0;
  int frameSkinned = -1;
  bool create(const CharModel& m);
  void destroy();
  void skin(const std::vector<mat4>& skinMats);   // joint skinning matrices (model space delta * rest * invBind)
};

// Procedural pose input (model space angles in radians unless noted).
struct PoseParams {
  float walkPhase = 0, walkAmount = 0;      // stride phase (rad) and blend 0..1 (speed / run speed)
  float crouch = 0, prone = 0;              // 0..1 blends
  float aimPitch = 0;                       // rad, + up
  float aimYawOffset = 0;                   // rad, upper body twist relative to the legs
  bool holdWeapon = true;
  float lowReady = 0;                       // 0 aimed .. 1 muzzle lowered (running, no target)
  vec3 gripR{0}, gripL{0};                  // weapon-space hand sockets
  vec3 magPos{0, -0.12f, 0.45f};            // weapon-space magazine (reload hand target)
  float reload = 0;                         // 0..1 progress of a reload motion (0 = none)
  float throwT = -1;                        // 0..1 grenade throw (right arm), < 0 = none
  float death = -1;                         // 0..1 fall progress, < 0 = alive
  int deathStyle = 0;                       // 0 backward, 1 forward, 2 side
  float recoil = 0;                         // 0..1 kick
  float climb = -1;                         // 0..1 mantle progress (hands on a ledge, weapon slung low), < 0 = none
  float climbW = 0, climbSpread = 0.24f;    // hands-on-ledge blend; half the distance between the hands (m)
  vec3 climbGrip{0, 1.9f, 0.45f};           // model-space ledge point between the hands (feet at the origin, +z forward)
  float swim = 0, swimPhase = 0;            // 0..1 swimming blend (body tilted forward, breaststroke, frog kick); stroke phase (rad)
};

struct PoseResult {
  std::vector<mat4> skin;                   // per skin joint
  std::vector<mat4> delta;                  // per skin joint: model-space delta (posed = delta * rest)
  vec3 joint[CJ_COUNT];                     // posed semantic joint heads (model space)
  mat4 weapon{1};                           // weapon -> model (after recoil / reload / throw adjustments)
};

void poseCharacter(const CharModel& m, const PoseParams& p, PoseResult& out);

bool initSkinning(std::string& err);       // compiles the compute program (once)
