#pragma once
// Textured weapon models imported from glTF binary (.glb) files.
//
// Offline: bakeGlbModel() reads a .glb (meshes, node hierarchy, skins in bind pose, one PBR texture set,
// KHR spec-gloss factors, the first animation), applies the per-weapon import transform
// X = T(offset) * S(realLength / measured length) * R(rot) so the weapon lies along +z (butt at z = 0,
// muzzle at z = realLength, top +y), downsizes textures and writes content/weapons/models/<dir>/:
//   mesh.bin   - TexVertex[] then uint32 indices
//   model.json - parts (name, role, index range, material factors, bounds), texture files, animation tracks
//   albedo.jpg / mr.png / normal.png / ao.png (only those the source has)
// Runtime: GpuModel::load() reads that folder; nothing is decoded from the original .glb at startup.
#include "common.h"

struct TexVertex { vec3 pos, nrm; vec4 tan; vec2 uv; };

struct ModelPart {
  std::string name, role;           // role: body, bolt, mag, clip, handle, cover, bipod, scope, hidden, ...
  uint32_t first = 0, count = 0;    // index range
  vec4 base{1};                     // base colour factor (multiplies the texture when present)
  float metal = 1, rough = 1;       // factors (multiply the MR texture when present)
  bool textured = false;
  int mat = -1;                     // index into GpuModel::mats (-1: factors only)
  vec3 bmin{0}, bmax{0};            // weapon space
  std::vector<mat4> anim;           // optional: weapon-space delta per animation sample (rest = identity)
};

struct ImportSpec {
  std::string src, dir;             // source .glb file name, output folder name
  vec3 rot{0};                      // Euler degrees applied first
  float realLength = 1;             // metres along +z after rotation
  std::string lengthNode;           // node whose extent defines the length ("" = all visible parts)
  vec3 offset{0};                   // added after the automatic placement (butt at z = 0, centred in x, y)
  std::map<std::string, std::string> nodeParts;   // ancestor/joint name -> role
  std::vector<std::string> hide;    // ancestor names dropped entirely
  int maxTex = 1024;
  json matOverride = json::object();   // per source material name: {"base":[r,g,b,a], "metal":m, "rough":r, "dropTextures":bool, "roughScale":k}
  vec3 tint{1};                        // multiplies every base colour (palette correction)
};
ImportSpec importSpecFromJson(const json& j);

// Bake one model. Returns false with a reason in err.
bool bakeGlbModel(const fs::path& glb, const ImportSpec& spec, const fs::path& outDir, std::string& err, std::string& summary);

struct GpuModel {
  GLuint vao = 0, vbo = 0, ebo = 0;
  GLuint texAlb = 0, texMR = 0, texN = 0, texAO = 0;   // first material's set (legacy single-set bakes)
  bool aoInMR = false;              // AO packed in the red channel of the MR map
  struct MatTex { GLuint alb = 0, mr = 0, n = 0, ao = 0; bool aoInMR = false; std::string name; };
  std::vector<MatTex> mats;         // one texture set per source material (multi-material models: FG42, M2, Tommy gun)
  int textureCount = 0;
  std::vector<ModelPart> parts;
  float animDuration = 0;
  vec3 bmin{0}, bmax{0};
  size_t gpuBytes = 0;
  int tris = 0;
  bool load(const fs::path& dir, std::string& err);
  void destroy();
  int find(const std::string& name) const;
  mat4 animAt(int part, float u) const;   // u in [0,1] over the animation; identity without one
};

// --import-weapon-models: bake every content/weapons/*.json "model" block whose source .glb is in srcDir.
bool importWeaponModels(const fs::path& srcDir, const fs::path& contentRoot);
