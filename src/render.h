#pragma once
#include "assets.h"
#include "terrain.h"
#include "combat.h"

struct DrawItem { const GpuMesh* mesh; mat4 model; vec4 tint{1, 1, 1, 1}; };
struct OverlayVert { vec3 p; vec4 c; };

struct Overlay {
  std::vector<OverlayVert> lines, tris, linesTop;
  void clear() { lines.clear(); tris.clear(); linesTop.clear(); }
  void line(vec3 a, vec3 b, vec4 c, bool top = false) { auto& L = top ? linesTop : lines; L.push_back({a, c}); L.push_back({b, c}); }
  void tri(vec3 a, vec3 b, vec3 c, vec4 col) { tris.push_back({a, col}); tris.push_back({b, col}); tris.push_back({c, col}); }
  void box(const mat4& m, vec3 mn, vec3 mx, vec4 c, bool top = false);
  void capsule(vec3 feet, float h, float r, vec4 c);
};

struct RenderView {
  mat4 view, proj; vec3 camPos;
  bool editor = true, grid = false, autoExposure = false;
  float dt = 0.016f;
};

struct FrameStats { double cpuMs = 0, gpuMs = 0; int draws = 0, tris = 0; };

class Renderer {
public:
  bool init(int w, int h);
  void resize(int w, int h);
  void render(const RenderView& v, const Terrain& t, const std::vector<DrawItem>& opaque,
              const std::vector<DrawItem>& glass, const std::vector<DrawItem>& ghosts, const Overlay& ov);
  GLuint renderThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, const Terrain* t = nullptr, const mat4* customView = nullptr);
  void resetExposure(float e = 1.0f);
  bool readPixels(std::vector<uint8_t>& rgba, int& w, int& h);
  FrameStats stats;
  std::vector<FxVert> fxAlpha, fxAdd;   // set by the game each frame (smoke / flashes, streaks)
  std::vector<DrawItem> viewModel;      // first-person weapon: drawn last over a cleared depth buffer
  vec3 sunDir = glm::normalize(vec3(-0.45f, 0.62f, 0.38f));
  int width = 0, height = 0;
private:
  GLuint compile(const char* vs, const char* fs, const char* name);
  void createTargets();
  void drawItems(GLuint prog, const std::vector<DrawItem>& items, bool shadowPass);
  void setLighting(GLuint prog, const RenderView& v);
  GLuint progFx = 0, fxVao = 0, fxVbo = 0;
  GLuint progMain = 0, progShadow = 0, progTerrain = 0, progSky = 0, progGlass = 0, progLine = 0, progTone = 0, progExpo = 0;
  GLuint shadowFbo = 0, shadowTex = 0, occFbo = 0, occTex = 0;
  GLuint hdrFbo = 0, hdrColor = 0, depthTex = 0, ldrFbo = 0, ldrColor = 0;
  GLuint expoFbo[2]{}, expoTex[2]{}; int expoIdx = 0; float expoReset = -1;
  GLuint emptyVao = 0, lineVao = 0, lineVbo = 0;
  GLuint thumbHdrFbo = 0, thumbHdr = 0, thumbDepth = 0;
  GLuint queries[2]{}; int qidx = 0; bool qvalid = false;
  mat4 lightVP{1}, occVP{1};
  float occTop = 0, occRange = 1;
  bool thumbMode = false;
  float time = 0;
  int shadowSize = 4096, occSize = 2048;
};
