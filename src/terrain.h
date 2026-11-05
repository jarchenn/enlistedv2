#pragma once
#include "common.h"

struct TemplateInfo {
  std::string id, name, description, file;
  int version = 1;
  bool available = true;
  float size = 800, buildHalf = 300, waterLevel = -1000;
  uint32_t seed = 1;
  vec3 camPos{0, 40, 80}; float camYaw = 0, camPitch = -20;
  json raw;
  GLuint thumbnail = 0;
};

std::vector<TemplateInfo> loadTemplates(const fs::path& dir);

class Terrain {
public:
  void generate(const TemplateInfo& t);
  float height(float x, float z) const;
  vec3 normal(float x, float z) const;
  bool raycast(vec3 o, vec3 d, float maxT, vec3& hit) const;
  bool inBuildArea(float x, float z) const { return std::fabs(x) <= buildHalf && std::fabs(z) <= buildHalf; }
  void draw() const;
  void destroy();
  int n = 256; float size = 800, buildHalf = 300, waterLevel = -1000;
  std::vector<float> h;
private:
  GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
};
