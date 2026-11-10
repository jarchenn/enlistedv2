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
  int section = -1;                 // destruction section (ImportSpec sections), -1 = none
  std::vector<mat4> anim;           // optional: weapon-space delta per animation sample (rest = identity)
};

struct ImportSpec {
  std::string src, dir;             // source .glb file name, output folder name
  vec3 rot{0};                      // Euler degrees applied first
  float realLength = 1;             // metres along +z after rotation (scale = realLength / extent)
  float scale = 0;                  // > 0: absolute scale instead of realLength (map assets authored in metres)
  std::string pivot = "weapon";     // "weapon": butt at z = 0, centred x / y; "ground": centred x / z, lowest point at y = 0
  std::string lengthNode;           // node whose extent defines the length ("" = all visible parts)
  vec3 offset{0};                   // added after the automatic placement (butt at z = 0, centred in x, y)
  std::map<std::string, std::string> nodeParts;   // ancestor/joint name -> role
  std::vector<std::string> hide;    // ancestor names dropped entirely
  int maxTex = 1024;
  json matOverride = json::object();   // per source material name: {"base":[r,g,b,a], "metal":m, "rough":r, "dropTextures":bool, "roughScale":k}
  vec3 tint{1};                        // multiplies every base colour (palette correction)
  // Edits of the supplied mesh, applied at bake in weapon space (after rot/scale/placement). Each edit selects the
  // connected triangle islands lying entirely inside box [lo, hi] and either drops them ("hide") or re-poses them:
  // rotate by deg about axis through pivot, then translate; or ("radial") push the vertices closer than rMax to the
  // z-parallel axis through centre outward by scale (opens a peep-sight hole; the outer ring is untouched). Used to
  // fold sub-parts the source authored raised (BAR ladder leaf) and open the Garand aperture. No geometry is added.
  // docs/fps-weapons.md#mesh-edits
  struct MeshEdit { vec3 lo{0}, hi{0}, axis{1, 0, 0}, pivot{0}, move{0}; float deg = 0; bool hide = false;
                    bool radial = false; vec2 centre{0}; float rMax = 0, scale = 1; std::string note; };
  std::vector<MeshEdit> meshEdits;
  // keepIslandsIn [lo, hi] (FT1): in the rotated source space, before measuring and scaling, only the connected islands
  // lying entirely inside the box stay (a flamethrower's gun out of a scene with its tanks, hose and ground plane)
  bool keepOn = false; vec3 keepLo{0}, keepHi{0};
  // Runtime budget (map assets from heavy sources): simplify keeps this fraction of each part's triangles
  // (quadric error, src/simplify.cpp); lod1 > 0 also bakes <dir>/lod1 at simplify * lod1 with 256 px textures.
  float simplify = 1, lod1 = 0;
  // Destruction sections: split every part into `sectionCount` slabs along `sectionAxis` (by triangle centroid) so a
  // blast can clear one stretch of a wire obstacle; parts are named "<name>#<k>" with "section": k.
  int sectionAxis = 2, sectionCount = 1;
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
