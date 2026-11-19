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
  bool wantNormals = false;   // WP2: load the materials' normal maps (animated first-person rigs; tangents from screen derivatives)
  // skeleton (all scene nodes on the joint paths; joints index into these)
  std::vector<std::string> nodeName;
  std::vector<int> nodeParent;
  std::vector<mat4> nodeLocal, nodeWorld;   // rest
  std::vector<int> jointNode;               // skin joint -> node
  std::vector<mat4> invBind;
  std::vector<int> jointParent;             // skin joint -> parent skin joint (-1 root)
  std::vector<vec3> jointHead;              // rest head position (model space)
  int semantic[CJ_COUNT];                   // CharJoint -> skin joint (-1 missing)
  // AN1 step 2: per skin joint under a hand, which finger (0 thumb, 1 index .. 4 little; -1 not a classified finger),
  // its segment (1 proximal .. 3 distal) and its rest curl axis (bone x rest palm normal, so + curls toward the palm)
  std::vector<signed char> finger, fingerSeg;
  std::vector<vec3> fingerAx;
  vec3 fingerDir[2] = {vec3(0), vec3(0)};   // AN1 step 2b: rest direction wrist -> knuckles per hand (0: unknown; the forearm line then)
  vec3 palmN[2] = {vec3(0, -1, 0), vec3(0, -1, 0)};   // rest palm normal (out of the palm) of the left / right hand, from the finger joints
  std::vector<int> fingerNext;              // AN1 step 2b: the next joint of the same finger (-1 at the chain's end)
  std::vector<vec3> fingerTip;              // per finger base joint: the fingertip (rest; the end joint's head or the last bone extended)
  std::vector<float> fingerSpread;          // per finger base joint: its rest angle from the middle finger in the palm plane (rad, about palmN)
  float headPitchFix = 0;                   // AN1 step 2b: rad, + raises a long head's rest snout to just above level (robot commando)
  float palmThick[2] = {0.022f, 0.022f};    // AN1 step 2b: hand joint line -> palm skin along the palm normal (m), measured from the hand's vertices
  bool fingerless = false; float fistF[2] = {0, 0}, fistN[2] = {0, 0};   // AN1 step 2d: hands with no vertex on a finger bone (the Rangers' modelled closed grip): the fist's centre from the wrist, along the fingers and the palm normal
  float handScale = 1;                      // wrist -> middle knuckle over a human's 9 cm (the robot's claws ~2)
  float fingerR = 0.009f;                   // finger pad radius (m), a tenth of the hand length (wrist to the middle knuckle)
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
  float lean = 0;                           // MV3: -1 left .. +1 right sideways bend through the waist and spine (rad of bend at full lean: leanBend)
  float leanBend = 0.28f;
  bool holdWeapon = true;
  float lowReady = 0;                       // 0 aimed .. 1 muzzle lowered (running, no target)
  vec3 gripR{0}, gripL{0};                  // weapon-space hand sockets
  vec3 holdOffset{0}; float holdWeld = 1; int holdKind = -1;   // SW2: the weapon's own hold (WeaponDef::holdOffset)
  vec3 magPos{0, -0.12f, 0.45f};            // weapon-space magazine (reload hand target)
  vec3 gripC{0}, gripAx{0, -1, 0}, guardC{0}; float gripRad = 0, guardRad = 0;   // AN1 step 2b: the weapon's grip / handguard capsules (WeaponDef; radius 0 = none)
  vec3 sightR{0};                           // AN1 step 2b: weapon-space rear sight (0: none; aim then puts it at the eye for the cheek weld)
  float reload = 0;                         // 0..1 progress of a reload motion (0 = none)
  float throwT = -1;                        // 0..1 grenade throw (right arm), < 0 = none
  float death = -1;                         // 0..1 fall progress, < 0 = alive
  int deathStyle = 0;                       // 0 backward, 1 forward, 2 side
  float recoil = 0;                         // 0..1 kick
  float climb = -1;                         // 0..1 mantle progress (hands on a ledge, weapon slung low), < 0 = none
  float climbW = 0, climbSpread = 0.24f;    // hands-on-ledge blend; half the distance between the hands (m)
  vec3 climbGrip{0, 1.9f, 0.45f};           // model-space ledge point between the hands (feet at the origin, +z forward)
  float swim = 0, swimPhase = 0;            // 0..1 swimming blend (body tilted forward, breaststroke, frog kick); stroke phase (rad)
  // AN1 soldier postures (0..1 blends, set by Game::posesStep): kneel (still and crouched: right knee down, left foot forward),
  // carry (moving, not engaged: rifle low across the body), moveAim (moving while engaged: knees bent, rifle at eye level)
  float kneel = 0, carry = 0, moveAim = 0;
  float hipFrac = 0, leanMove = -1;           // AN3: moving: the hips' height over standing (0: the old drop) and the spine pitch (rad; < 0: the old lean sum)
  float hunch = 0;                          // AI5: combat hunch (soldier bots): torso forward ~11 deg more, knees bent, head down to the sights
  bool gait = false;                        // stance / swing feet: planted feet travel back at the body speed (walkPhase: 2 pi per cycle)
  float stride = 0, duty = 0.6f;            // half the planted-foot travel (m); stance fraction of the cycle
  vec3 moveDir{0, 0, 1};                    // model-space travel direction (unit, y = 0)
  vec2 slope{0};                            // AN1b: ground gradient under the body in model space (rise per metre along x, z); feet and the kneel follow it
  vec3 footPin[2] = {vec3(0), vec3(0)};     // AN1 step 3b: model-space ankle pins (a planted foot held where it landed, in the world) and their weights;
  float footPinW[2] = {0, 0}, footLift[2] = {0, 0};
  // DF1 (Delta Force; 0 / -1 elsewhere): the reload by family and the action cycled between shots (weapon-space points)
  int reloadStyle = 0;                      // 0 box magazine (magPos), 1 rounds / a clip / a rocket into portPos, 2 belt: the top cover (portPos) up, the box (magPos) out and in
  int reloadRounds = 1;                     // style 1: hand trips from the pouch to the port
  vec3 portPos{0}, boltPos{0};              // the loading port / top cover; the bolt handle (0: none)
  bool boltReload = false;                  // the right hand opens the bolt at the start of the reload and closes it at the end
  float cycle = -1; int cycleKind = 0;      // 0..1 through the cycle after a shot (< 0 none); 1 bolt (right hand), 2 pump (the support hand slides back)
  bool rightArmClip = false;                // the right arm from base too (a clip's grenade throw)
  bool clipGun = true; float pistolRaise = 0;   // clipHold: the gun placed from the clip's hands (false: the soldier placement, launchers on the shoulder); an aimed pistol raised toward the eyes (0..1)
  bool clipHold = false;                    // the gun in the clip's own hands: the grip in its right palm, the grip -> handguard line along its hands (a pistol: shoulders -> hands), the elbows, hands and fingers the clip's; the support hand slides to the gun's own handguard
  const std::vector<mat4>* base = nullptr;   // DF1: clip deltas (model space, per skin joint): every joint outside the arms takes these; the arms, hands and weapon stay procedural on the clip's chest
  vec3 footN[2] = {vec3(0), vec3(0)};       // AN1 step 3c: model-space ground normal under a planted foot (0: the body's slope plane)   // footLift: extra ankle height (m) of a standing re-step
};

struct PoseResult {
  std::vector<mat4> skin;                   // per skin joint
  std::vector<mat4> delta;                  // per skin joint: model-space delta (posed = delta * rest)
  vec3 joint[CJ_COUNT];                     // posed semantic joint heads (model space)
  mat4 weapon{1};                           // weapon -> model (after recoil / reload / throw adjustments)
  float fingerGap[2] = {0, 0}, fingerPen[2] = {0, 0};   // AN1 step 2b: per hand on a capsule, the worst fingertip pad gap off the surface and the deepest pad penetration (m)
  int fingersOn[2] = {0, 0};                // fingers solved against the capsule
  float fingerDiag[2][5][3] = {};           // per hand and finger: the closing angle (rad), the tip pad gap and the starting clearance of the pads (m)
};

void poseCharacter(const CharModel& m, const PoseParams& p, PoseResult& out);

bool initSkinning(std::string& err);       // compiles the compute program (once)
