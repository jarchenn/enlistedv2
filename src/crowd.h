#pragma once
// GC1 GPU crowd: the far tiers of army mode (corpus 16; the 100,000-AI budget). A far unit is a 48 B record drawn instanced from a
// type's baked animation (procedural poses sampled once per clip into palettes) on a simplified mesh; the near ones stay full
// Soldiers. GC1 is the drawing (and a test source, NMK_CROWD=n); GC2 moves the units on the GPU, GC3 resolves their combat.
#include "character.h"
#include "render.h"
class Game; struct WeaponDef;

struct CrowdUnit { vec4 pos; vec4 prev; vec4 anim; };   // feet xyz + yaw (rad, the soldier's convention); last frame's; clip, time (s), tint, scale
enum CrowdClip { CC_IDLE, CC_WALK, CC_RUN, CC_CROUCH, CC_PRONE, CC_FIRE, CC_DIE, CC_DEAD, CC_COUNT };

struct CrowdType {
  const CharModel* m = nullptr; const GpuModel* gun = nullptr;
  int J = 0, frames = 0; float fps = 15;
  glm::ivec4 clip[8]{};          // first frame, frames, loop
  float dur[CC_COUNT] = {};      // a loop's period / a clip's length (s)
  GLuint pal = 0;                // frames x (J + 1) mat4: skin matrices, then the gun (weapon -> model)
  struct Lod { GLuint rest = 0, ebo = 0, vao = 0; int verts = 0, tris = 0; std::vector<ModelPart> parts; } lod[2];
  size_t bytes = 0;
};
// the poses: CharModel + the held gun's sockets -> palettes; the LODs (src/simplify.cpp per part, ~3000 and ~300 triangles)
bool crowdBake(CrowdType& t, const CharModel& m, const WeaponDef* w, std::string& err);
void crowdDestroy(CrowdType& t);

// GC2: the units live on the GPU (CrowdUnit + CrowdSim per unit); each frame compute passes hash them into a 2 m grid, move them
// down their side's flow field with separation, and cull them into per type x LOD lists drawn indirectly. The CPU only stages
// spawns and builds the flow fields (Dijkstra over the walkable grid from the navmesh) when a goal changes.
struct CrowdSim { vec4 a; glm::ivec4 b; vec4 c; };   // a: velocity x z, hp, wanted speed; b: team, type, state (0 alive, 1 dying, 2 dead, 3 gone), target; c (GC3): shot cooldown, firing, aim sigma (rad), range (m)
int crowdSpawn(Game& g, vec3 feet, float yaw, int team, int model, int gun, float speed, float hp = 100);   // -1: full or no such type
void crowdGoal(Game& g, int team, vec3 goal);    // team 0 / 1: the field its units walk down (rebuilt now)
bool crowdShadersBuild(std::string& err);         // --check-crowd: the compute programs build (err: the failing one; its log is in the log)
int crowdCount();                                // units spawned (alive or not)
int crowdAlive(int team);                        // GC3: spawned minus the deaths read back so far
int crowdDeaths(int team);
void crowdFrame(Game& g);                        // sim (dt from Game::t) + cull + the renderer's crowdDraws
// GC4 army mode (ScenarioRules::armyUnits, host only): the reserves spawned behind each side's spawn and sent at the objective
// (Assault) or the enemy's spawn (TDM); a crowd unit within a human's bubble (150 m, 350 m through a scope) becomes the full
// soldier of a parked roster slot of its side; an AI soldier beyond every bubble by 60 m for 3 s (not firing) goes into the crowd
// and parks its slot. Score: the crowd's dead count as the other side's kills; the attackers' crowd keeps the match going.
void crowdArmyStep(Game& g, float dt);
void crowdReset();                               // the match ended: types, units and buffers freed
