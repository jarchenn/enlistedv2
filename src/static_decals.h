#pragma once
// GX5 static decals (docs/graphics-overhaul.md#gx5-decals): per building, in asset space, scattered from the building's
// rooms, hearths and windows with the densities in content/settings/decals.json. The main pass evaluates them in the
// material (kMainFS sdecals()), so they change albedo, roughness, relief and occlusion and are lit like the wall under
// them. One SSBO holds every building's list; SurfaceParams carries each room's range and the exterior range.
#include "render.h"

struct RoomInfo;

// StaticDecal (render.h): a = centre (asset space) + type; b = unit normal (out of the surface) + seed 0..1;
// c = half width, half height (m), strength 0..1, variant
enum StaticDecalType { SD_CRACK = 0, SD_STAIN, SD_SOOT, SD_SCUFF, SD_POCK, SD_STREAK, SD_GHOST, SD_DAMP, SD_TYPES };

// builds the copy's decals (asset space) into R's buffer and sets sp.roomDec / sp.extDec. Deterministic from the
// instance id; called again when the copy changes (its old range is reused or released)
// xA / xB: the finishes of rooms 16+ (sp holds the first 16); they go into the block as records ahead of the decals
void staticDecalsFor(Renderer& R, uint64_t instId, const RoomInfo& ri, SurfaceParams& sp, float wear, bool damaged,
                     const std::vector<vec4>& xA, const std::vector<vec4>& xB);
// the decal type names, for the docs and tests
const char* staticDecalName(int type);
