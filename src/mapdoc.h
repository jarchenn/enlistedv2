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
  uint32_t rev = 1;                           // bumps on every edit; runtime rebuilds colliders on change
  bool operator==(const Instance& o) const {
    return id == o.id && assetId == o.assetId && pos == o.pos && yaw == o.yaw && openDefaults == o.openDefaults && panelHp == o.panelHp;
  }
};

struct StartMarker { bool set = false; vec3 pos{0}; float yaw = 0; };
struct EditorCam { bool set = false; vec3 pos{0, 40, 80}; float yaw = 0, pitch = -20; };

struct MapDoc {
  std::string name = "Untitled map";
  std::string templateId = "blank_slate";
  int templateVersion = 1;
  uint32_t seed = 1;
  std::vector<Instance> instances;
  std::set<std::string> removedVegetation;
  StartMarker start;
  EditorCam camera;
  std::map<int, std::vector<float>> terrainDeltas;   // applied ground damage: tile key -> per-sample metres
  int terrainTemplateVersion = 0;
  uint64_t nextId = 1;
  json extra;

  Instance* find(uint64_t id) { for (auto& i : instances) if (i.id == id) return &i; return nullptr; }
  uint64_t newId() { return nextId++; }
};

bool saveMap(const fs::path& path, const MapDoc& doc, std::string& err);
bool loadMap(const fs::path& path, MapDoc& doc, std::string& err);
json mapToJson(const MapDoc& doc);
