#pragma once
// L4 texture layer (docs/textures.md): curated ambientCG CC0 sets at 1K, sampled triplanar in the main and terrain
// shaders on top of (or, per slot, instead of) the procedural patterns.
//
//  content/textures/<set>/{color,normal,rma}.jpg + meta.json   one set per folder (tools/fetch_ambientcg.py)
//  content/textures/textures.json                              slots: how a pattern / material / terrain layer uses a set
//
// GPU: three GL_TEXTURE_2D_ARRAYs, one layer per set, full mip chains, 8x anisotropy, texture units 20-22:
//  unit 20  colour (sRGB) + ambient occlusion in alpha        BC3 (DXT5), RGBA8 when S3TC is missing
//  unit 21  tangent-space normal xy (NormalGL)                 BC5 (RGTC2)
//  unit 22  roughness, height                                  BC5 (RGTC2)
// Missing files leave their slots empty: the shaders keep the procedural pattern. --legacy-look stays procedural.
#include "common.h"

namespace tex {

constexpr int kMaxSlots = 64, kMaxLayers = 64, kUnitBase = 20;

// Load content/textures/textures.json (slot table) and upload every set found under content/textures (GL context
// required). Safe to call when nothing is there. Logs what it loaded.
void init(const fs::path& texRoot);
// GLSL: declarations + helpers shared by the main and terrain fragment shaders, and the main-pass hooks.
const char* glslCommon();
const char* glslMain();
// Upload the sampler units and slot tables to a program (main or terrain). Programs are remembered, so later slot
// changes (materials naming a set that has no slot yet) reach them too.
void attach(GLuint prog);
// Vertex code for a material (Vertex::params.x = pattern + 100 * code): 0 = the pattern's default slot,
// 1 = no texture, 2 + slot otherwise. explicitSet = the material JSON's "texture" field ("none" disables).
int materialCode(const std::string& matName, int pattern, const std::string& explicitSet);

bool active();                     // at least one set on the GPU
void setEnabled(bool on);          // A/B and tests: off = procedural everywhere (uniform uTexOn)
bool enabled();
struct Info { int sets = 0, slots = 0, slotsLive = 0, unused = 0; size_t bytes = 0; double ms = 0; bool compressed = false; std::vector<std::string> missing; };
const Info& info();
int slotIndex(const std::string& slotName);   // -1 if unknown
int slotLayer(int slot);                      // -1: the slot's set is not loaded
int patternSlot(int pattern);
std::string stateKey();                       // what the rendered look depends on (thumbnail cache keys)

// offline tools (no GL): pack one raw ambientCG folder (color/normal/roughness/ao/height.jpg) into a set folder,
// and write simple procedural placeholder sets (engine bring-up and the fallback test)
int packSet(const fs::path& src, const fs::path& dst);
int writePlaceholders(const fs::path& texRoot, const std::vector<std::string>& sets);

}  // namespace tex
