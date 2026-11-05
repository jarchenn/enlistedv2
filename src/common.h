#pragma once
// Shared includes and small helpers.
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;
using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat4; using glm::quat;

void logf(const char* fmt, ...);
std::string readTextFile(const fs::path& p);
fs::path exeDir();
fs::path contentDir();
fs::path userMapsDir();

inline float radians(float d) { return d * 0.017453292519943295f; }
inline float degrees(float r) { return r * 57.29577951308232f; }
inline float wrapDeg(float d) { d = std::fmod(d, 360.0f); if (d < 0) d += 360.0f; return d; }
inline quat yawQuat(float deg) { return glm::angleAxis(radians(deg), vec3(0, 1, 0)); }
inline vec3 jvec3(const json& j, vec3 def = vec3(0)) {
  if (!j.is_array() || j.size() < 3) return def;
  return vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}
inline json tojson(vec3 v) { return json::array({v.x, v.y, v.z}); }

// Ray / box helpers (OBB given by center, half extents, rotation).
bool rayOBB(vec3 ro, vec3 rd, vec3 c, vec3 he, const quat& q, float& tOut);
// Distance from segment (a,b) to an OBB; returns squared distance.
float segOBBDist2(vec3 a, vec3 b, vec3 c, vec3 he, const quat& q);
