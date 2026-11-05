#pragma once
#include "common.h"

// Surface pattern IDs understood by the main shader.
enum Pattern : int {
  PAT_PLAIN = 0, PAT_STONE = 1, PAT_WOOD = 2, PAT_PLASTER = 3, PAT_SLATE = 4, PAT_TILE = 5,
  PAT_METAL = 6, PAT_GLASS = 7, PAT_FABRIC = 8, PAT_BRICK = 9, PAT_PAINTED = 10, PAT_CONCRETE = 11
};

struct Material {
  std::string name;
  vec3 albedo{0.6f};
  float roughness = 0.8f, metallic = 0.0f;
  int pattern = PAT_PLAIN;
  int grainAxis = 1; // part-local axis that wood grain / slate courses follow
};

// A primitive part. Boxes and triangular prisms, in a parent space.
struct Part {
  enum Shape { BOX, PRISM } shape = BOX;
  vec3 center{0}, size{1};
  quat rot{1, 0, 0, 0};
  int mat = 0;
  bool visible = true, collide = true;
  // PRISM: triangle in (u,v) plane extruded along `axis` from a0..a1.
  int axis = 0; float a0 = 0, a1 = 0; vec2 tri[3]{};
};

struct Vertex {
  vec3 pos, nrm, albedo;
  vec4 params;  // pattern, roughness, metallic, grain axis
  vec3 pcoord;  // part-local position (for rotated-part patterns)
  vec3 pnrm;    // part-local normal
};

struct MeshData { std::vector<Vertex> v; std::vector<uint32_t> i; };

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
  void upload(const MeshData& m);
  void destroy();
  void draw() const;
};

struct ShapeHandle;                       // physics shape (defined in physics.cpp)
using ShapePtr = std::shared_ptr<ShapeHandle>;

struct Leaf {
  vec3 hinge{0};            // pivot in asset space
  float sign = 1;           // rotation direction multiplier
  std::vector<Part> parts;  // in hinge-relative space
  GpuMesh opaque, glass;
  ShapePtr shape;           // collision, hinge-relative
  std::vector<Part> colliders; // hinge-relative boxes (also used for obstruction tests)
};

struct InteractDef {
  std::string id, kind, label, sound;
  vec3 axis{0, 1, 0};
  float openAngle = 95, duration = 0.9f;
  bool defaultOpen = false;
  bool apAlways = false;   // aim volume stays active when open (flaps worked through a slit)
  std::vector<Leaf> leaves;
  vec3 apMin{0}, apMax{0}; // aim volume (asset space)
};

struct AssetDef {
  std::string id, name, category, description, file;
  int version = 1, floors = 1;
  bool enterable = false;
  vec2 footprint{4, 4};
  float plinthDepth = 1.0f, maxGrade = 1.0f;
  std::vector<Material> materials;
  std::vector<Part> parts;       // static parts incl. furniture
  std::vector<std::string> furnitureNames;
  std::vector<InteractDef> interactables;
  GpuMesh opaque, glass;
  ShapePtr shape;                 // static collision
  vec3 bmin{0}, bmax{0};
  GLuint thumbnail = 0;
  json walkthrough;               // scripted traversal test (asset-local)
  json raw;
  bool placeholder = false;
  int findInteract(const std::string& id) const;
};

class AssetLibrary {
public:
  bool loadAll(const fs::path& dir);
  const AssetDef* find(const std::string& id) const;
  AssetDef* placeholder() { return &placeholder_; }
  std::vector<std::unique_ptr<AssetDef>> assets;
private:
  bool loadOne(const fs::path& p);
  AssetDef placeholder_;
};

void appendPart(MeshData& m, const Part& p, const Material& mat, const mat4& xf);
void buildPlaceholder(AssetDef& a);
