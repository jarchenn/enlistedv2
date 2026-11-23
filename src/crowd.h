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

// GC1 frame: the crowd's units (Game::crowd) culled and sorted per type x LOD into the renderer's crowdDraws
void crowdFrame(Game& g);
void crowdReset();               // the match ended: types and buffers freed
