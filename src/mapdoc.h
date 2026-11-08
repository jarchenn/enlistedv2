#pragma once
#include "common.h"

constexpr int kMapSchemaVersion = 1;

// A placed copy of an asset definition. Everything the author can change lives here.
struct Instance {
  uint64_t id = 0;
  std::string assetId;
  int assetVersion = 1;
  vec3 pos{0};
  float yaw = 0;                              // degrees about +Y
  std::map<std::string, bool> openDefaults;   // authored initial state overrides per interactable id
  json extra;                                 // unknown fields preserved verbatim (forward compat / missing assets)
  std::map<std::string, float> panelHp;       // applied (persistent) wall damage by stable panel id; unknown ids kept
  std::map<std::string, int> leafState;       // applied opening damage "interactable/leaf" -> 1 glass broken, 2 leaf gone
  uint32_t rev = 1;                           // bumps on every edit; runtime rebuilds colliders on change
  bool operator==(const Instance& o) const {
    return id == o.id && assetId == o.assetId && pos == o.pos && yaw == o.yaw && openDefaults == o.openDefaults && panelHp == o.panelHp && leafState == o.leafState;
  }
};

// Lasso water fill (docs/lasso.md): a flat surface at `level` over a world-space xz outline (V1 water surface schema).
struct WaterBody {
  uint64_t id = 0;
  std::vector<vec2> outline;   // world xz, simple polygon
  float level = 0;             // world y of the surface (the hollow's spill height)
  vec2 seed{0};                // the lowest point the fill started from
  std::string kind = "pond";
  bool operator==(const WaterBody& o) const { return id == o.id && outline == o.outline && level == o.level && seed == o.seed && kind == o.kind; }
};

struct StartMarker { bool set = false; vec3 pos{0}; float yaw = 0; };
struct EditorCam { bool set = false; vec3 pos{0, 40, 80}; float yaw = 0, pitch = -20; };

struct MapDoc {
  std::string name = "Untitled map";
  std::string templateId = "blank_slate";
  int templateVersion = 1;
  uint32_t seed = 1;
  float width = 0, depth = 0;   // buildable area in metres (SPEC 9.1, saved as mapSize); 0 = the template's size
  std::vector<Instance> instances;
  std::set<std::string> removedVegetation;
  StartMarker start;
  EditorCam camera;
  json cameraBookmarks = json::object();   // editor view bookmarks "1".."9" (saved under editorCamera, like the view)
  std::map<int, std::vector<float>> terrainDeltas;   // applied ground damage: tile key -> per-sample metres
  int terrainTemplateVersion = 0;
  std::map<int, std::vector<float>> terrainSculpt;   // V8 sculpted ground (terrain_brush.cpp): tile key -> per-sample metres
  json terrainGen = json::object();                  // V8 generator layers: outer ring, generated hills (Terrain::gen)
  std::vector<WaterBody> waterBodies;                // lasso water fill (lasso_water.cpp)
  // lasso ground paint (ground_paint.cpp): world-anchored 16 m chunks of 32 x 32 texels (0.5 m), RGBA8 per texel
  // (top type, top weight, under type, under weight); key = groundPaintKey(cx, cz)
  std::map<int64_t, std::vector<uint8_t>> groundPaint;
  uint64_t paintSig = 0;                             // runtime: changes whenever groundPaint does (not saved)
  uint64_t nextId = 1;
  json extra;

  Instance* find(uint64_t id) { for (auto& i : instances) if (i.id == id) return &i; return nullptr; }
  uint64_t newId() { return nextId++; }
};

bool saveMap(const fs::path& path, const MapDoc& doc, std::string& err);
bool loadMap(const fs::path& path, MapDoc& doc, std::string& err);
json mapToJson(const MapDoc& doc);
