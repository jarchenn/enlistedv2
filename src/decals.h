#pragma once
// Decal renderer (W7, SPEC 7): world blood decals projected onto the scene depth (any surface: ground, walls,
// sandbags, trees) and bone-attached decals on the GPU-skinned soldiers (the soldier's own skinned vertex buffer is
// drawn a second time; each fragment's rest-space position, read from the character's rest SSBO by vertex id, is
// tested against the decals, so they deform with animation and the death pose). Colours multiply the lit scene.
// Called once per frame from the gore render hook (gore.h: goreRenderPass -> decalsRender).
#include "gore.h"

struct DecalGpuStats { double gpuMs = 0; int worldDrawn = 0, charsDrawn = 0, boneDrawn = 0; };
const DecalGpuStats& decalsLastStats();
