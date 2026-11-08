#pragma once
#include "assets.h"
#include "terrain.h"
#include "combat.h"
#include "grass.h"
#include "frustum.h"
#include "light_types.h"

// Per building copy parameters of the wall / floor finish patterns 17-22 (docs/interiors.md): no shader edits needed
// to vary wear, palette or room finishes. Rooms and sills are in asset space.
struct SurfaceParams {
  vec4 inst{0, 1, 1, 0};                 // seed (0..1), wear (0..2), palette variation (0..1), room count
  vec4 roomA[16]{}, roomB[16]{};         // (min x, min z, max x, max z) / (floor y, ceiling y, wall finish, floor finish)
  vec4 sillA[12]{}, sillB[12]{}; int sills = 0;   // (centre x, sill y, centre z, half width) / (outward normal x, z, -, -)
};
struct DrawItem { const GpuMesh* mesh; mat4 model; vec4 tint{1, 1, 1, 1}; const SurfaceParams* surf = nullptr; vec4 apWorld{0}; float glow = 1.0f;   // apWorld.w = 1: world-space pattern coordinates (V6 snap.cpp); glow: emissive multiplier (0 = lamp off, L2)
};
struct TreeDef;
struct FoliageDraw { const TreeDef* tree; mat4 m; };
// thinPx > 0: keep sub-pixel geometry (barbed wire) visible - vertices move out along their normals by thinPx pixels'
// worth of angle at their distance (capped at 3 cm), so a 2 mm strand stays about a pixel wide at range
struct ModelDraw { const GpuModel* model; int part; mat4 m; vec4 tint{1, 1, 1, 1}; float thinPx = 0; };
struct OverlayVert { vec3 p; vec4 c; };
struct NatPatchDraw { GLuint vbo = 0; int count = 0; vec3 mn{0}, mx{0}; float reach = 1; };   // plant patch (nature.cpp); reach x the grass radius

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
  // first-person weapon: own fixed-FOV projection (identity = use proj) and the depth-of-field focus
  mat4 vmProj{0};
  float dofFocus = 0, dofAmount = 0;   // metres from the eye; 0..1 (ADS blend)
  bool underwater = false;             // camera below the water surface (swim.cpp): dense fog, tint, vignette
  vec3 underFogCol{0.06f, 0.24f, 0.25f}; float underFogDens = 0.11f;   // movement.json "swim"
};

struct FrameStats { double cpuMs = 0, gpuMs = 0; int draws = 0, tris = 0; int culled = 0; };   // culled: items / cards / parts skipped by the per-pass frustum tests (all passes)
// water surface of a placed asset (water.cpp): drawn by the water pass after the opaque world
struct WaterDraw { GLuint vao = 0; int count = 0; mat4 model{1}; vec4 look{0}, tint{0}; };   // look: kind, clarity, waves
#define NMK_REVZ 1   // the world pass is reverse-Z (docs/rendering.md#depth-convention)

class Renderer {
public:
  bool init(int w, int h);
  void resize(int w, int h);
  void render(const RenderView& v, const Terrain& t, const std::vector<DrawItem>& opaque,
              const std::vector<DrawItem>& glass, const std::vector<DrawItem>& ghosts, const Overlay& ov);
  GLuint renderThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, const Terrain* t = nullptr, const mat4* customView = nullptr);
  GLuint renderAssetThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, float thinPx, const std::function<const GpuMesh*(float)>& thinBoxes, struct ThumbInfo* info);   // thumbs.cpp (W2)
  void resetExposure(float e = 1.0f);
  std::string hdrProbe() const;   // debug: top-mip average and count of non-finite HDR pixels
  float exposure() const { float e = 0; glGetTextureImage(expoTex[expoIdx], 0, GL_RED, GL_FLOAT, sizeof e, &e); return e; }   // debug / tests (stalls)
  bool readPixels(std::vector<uint8_t>& rgba, int& w, int& h);
  bool readVmDist(std::vector<float>& d, int& w, int& h);   // first-person eye distance per pixel (0 = world), rows bottom-up
  FrameStats stats;
  vec4 fogNow{0};   // fog colour (rgb) and density of the last frame's world pass (tests)
  std::vector<FxVert> fxAlpha, fxAdd;   // set by the game each frame (smoke / flashes, streaks)
  const Grass* grass = nullptr;         // instanced pasture grass (owned by the app)
  std::vector<DrawItem> viewModel;      // first-person weapon: drawn last over a cleared depth buffer
  std::vector<ModelDraw> viewModelTex;  // textured (imported) parts of the first-person weapon
  bool dofEnabled = false;   // sight depth of field: removed from play (kept only as a code path, off)
  std::vector<FoliageDraw> foliage;
  std::vector<NatPatchDraw> natPatches;   // flower / tall-grass patches (nature.cpp), drawn with the grass program
  int natDraws = 0; size_t natTufts = 0;   // last frame
  std::vector<ModelDraw> worldModels;   // imported models in the world (rack displays, the M2HB station)     // tree leaf cards (alpha-tested, wind sway, shadow casting)
  float wind = 1.0f;
  // sky clouds: fbm on a plane at 1500 m scrolling with the wind
  bool clouds = true; float cloudCover = 0.62f;
  // render quality / look (docs/rendering.md). legacy = the pre-BSL-pass look (fallback)
  struct Quality {
    bool legacy = false;
    bool farCascade = true;     // second shadow cascade out to ~240 m
    float shadowSoft = 2.2f;    // PCF radius (near texels); 0 = legacy 3x3
    bool cloudShadows = true;
    bool shafts = true; float shaftStrength = 0.35f;
    float hazeDensity = 0.0010f, hazeHeight = 70.0f;   // aerial perspective (1/m at ground level, scale height m)
    bool ssao = true; float ssaoStrength = 0.85f, ssaoRadius = 0.6f;
    bool bloom = true; float bloomStrength = 0.045f;
    bool ssr = true; float ssrStrength = 1.0f;
    bool taa = true; float taaSharpen = 0.30f, taaJitter = 0.5f;   // V1: sharpen 0.40 -> 0.30 (facade shimmer A/B, docs/rendering.md 9.5)
    int taaPhases = 8;          // Halton (2,3) jitter phases (settings: High uses 16)
  } q;
  // render targets set by the settings presets (settings.cpp, docs/settings.md). width / height are the internal
  // render size = display size x resolution scale; the final blit upscales to displayW x displayH.
  void setShadowSizes(int nearSize, int farSize);   // recreates the shadow maps when a size changes
  void setResolutionScale(float s);                  // 0.5 .. 1
  void setSsaoFull(bool full);                       // SSAO at full instead of half resolution
  int shadowNearPx() const { return shadowSize; }
  int shadowFarPx() const { return shadowFarSize; }
  float resolutionScale() const { return resScale; }
  bool ssaoFull() const { return aoDiv == 1; }
  int displayW = 0, displayH = 0;
  void resetTemporal() { taaValid = false; }   // camera cut / teleport / mode switch
  // offscreen orthographic/perspective render of textured parts (weapon calibration sheets, tests)
  bool renderModelSheet(const std::vector<ModelDraw>& items, const std::vector<DrawItem>& boxes, const mat4& view, const mat4& proj, vec3 camPos, int w, int h, std::vector<uint8_t>& rgba);
  vec3 sunDir = glm::normalize(vec3(-0.45f, 0.62f, 0.38f));   // the Day sun; inside render() the key light of the time of day (lighting.cpp)
  // ---- lighting engine (src/lighting.cpp, docs/lighting.md): time of day, clustered local lights, light shadows, probe GI
  struct LightQ {
    float hour = 14.0f;         // time of day, 0..24 (14:00 = Day: exactly the pre-L2 sun, sky and exposure)
    int maxLights = 256;        // local lights uploaded per frame (nearest first)
    int shadowLights = 4;       // nearest spot / point lights with a shadow map (0, 4, 8)
    int gi = 1;                 // probe GI: 0 off (the old fill term), 1 near volume, 2 near + far volumes, faster updates
    float emissive = 1.0f;      // global multiplier on material emission
    int probeBudget = 0;        // > 0: probes captured per frame (tests); 0 = the preset rate
  } lt;
  std::vector<LightDesc> lights;        // filled every frame (lightsGather, src/lights.h) before render()
  std::vector<LightDesc> testLights;    // appended to lights every frame (self-tests, no L3 data)
  struct RoomBox { mat4 m{1}; vec3 half{0}; };   // building room volumes in world space (probe placement)
  std::vector<RoomBox> rooms;
  struct LightingSys* lsys = nullptr;   // lighting.cpp state
  friend struct LightingPass;
  int width = 0, height = 0;
  int shaderErrors = 0;                 // compile failures since start (self-tests require 0)
  int fboErrors = 0;                    // incomplete framebuffers since start (self-tests require 0)
  // Depth convention (docs/rendering.md#depth-convention). The world camera passes of render() are reverse-Z: clip depth
  // 0..1 (glClipControl), near = 1, far = 0, cleared to 0, GL_GREATER. Shadow cascades, sky occlusion, thumbnails and
  // offscreen sheets keep the standard GL convention. Code drawing inside render() picks its depth state from these.
  int wallRelief = 1;                   // A/B (--selftest-render): 0 = wall patterns 17-22 without their relief bump / light term
  bool revZ = false;                    // true once render() ran: the world camera passes are reverse-Z
  GLenum zLess() const { return revZ ? GL_GREATER : GL_LESS; }
  GLenum zLEqual() const { return revZ ? GL_GEQUAL : GL_LEQUAL; }
  float zToward() const { return revZ ? 1.0f : -1.0f; }   // polygon-offset sign that pulls a fragment toward the eye
  std::vector<WaterDraw> water;         // set by the app each frame (water.cpp), consumed by render()
  friend struct WaterPass;
private:
  GLuint compile(const char* vs, const char* fs, const char* name);
  void createTargets();
  void createShadowTargets();
  float resScale = 1.0f; int aoDiv = 2;
  // fr: the pass's own frustum (src/frustum.h, docs/rendering.md#frustum-culling); null = draw everything (thumbnails,
  // model sheets, the first-person weapon). sortSurf: stable sort by building finish parameters (main opaque pass)
  void drawItems(GLuint prog, const std::vector<DrawItem>& items, bool shadowPass, const Frustum* fr = nullptr, bool sortSurf = false);
  void setLighting(GLuint prog, const RenderView& v);
  void drawModels(const std::vector<ModelDraw>& items, const Frustum* fr = nullptr);
  void drawFoliage(GLuint prog, const mat4& vp, const Frustum* fr = nullptr);
  void drawModelsDepth(GLuint prog, const std::vector<ModelDraw>& items, const Frustum* fr = nullptr);
  // uniform locations of the draw loops, looked up once per program (cleared by compile())
  struct DrawLocs { GLint model = -1, tint = -1, inst = -1, roomA = -1, roomB = -1, sills = -1, sillA = -1, sillB = -1,
                    vp = -1, time = -1, wind = -1, leafCol = -1, base = -1, metal = -1, rough = -1, thin = -1,
                    alb = -1, mr = -1, nrm = -1, ao = -1, hasAlb = -1, hasMR = -1, hasNrm = -1, hasAO = -1, aoInMR = -1, snapAp = -1, glow = -1; };
  std::vector<std::pair<GLuint, DrawLocs>> locCache;
  DrawLocs drawLocs(GLuint prog);   // by value: the cache may grow while a loop holds it
  std::vector<uint32_t> drawIdx;   // scratch: visible items of the current drawItems call
  GLuint progLeaf = 0, progLeafShadow = 0;
  GLuint progModel = 0, progDof = 0, progShaft = 0, progComposite = 0;
  GLuint sceneCopy = 0, compFbo = 0, shaftTex = 0, shaftFbo = 0;   // world composite (before the weapon pass)
  void worldComposite(const RenderView& v, const mat4& vp);
  GLuint ambTex = 0, aoTex = 0, aoFbo = 0, progSsao = 0, progBloomDown = 0, progBloomUp = 0;
  static const int kBloomLevels = 6;
  GLuint bloomTex[kBloomLevels]{}, bloomFbo[kBloomLevels]{}; int bloomW[kBloomLevels]{}, bloomH[kBloomLevels]{};
  void bloomPass();
  GLuint velTex = 0, cloudShTex = 0, cloudShFbo = 0, progCloudSh = 0; vec3 cloudShBox{0, 0, 1500};
  GLuint nrTex = 0, progTaa = 0, histTex[2]{}, taaFbo[2]{}; int histIdx = 0; bool taaValid = false;
  mat4 prevVP{1}; vec3 prevCam{0}; vec2 jitterPx{0};
  void taaResolve(const mat4& vpJ, const mat4& vpU, const RenderView& v);
  GLuint vmDistTex = 0, dofTex = 0, dofFbo = 0;
  GLuint progGrass = 0;
  GLuint progFx = 0, fxVao = 0, fxVbo = 0;
  GLuint progMain = 0, progShadow = 0, progTerrain = 0, progSky = 0, progGlass = 0, progLine = 0, progTone = 0, progExpo = 0;
  GLuint shadowFbo = 0, shadowTex = 0, occFbo = 0, occTex = 0;
  GLuint shadowFarFbo = 0, shadowFarTex = 0; int shadowFarSize = 2048;
  mat4 lightVPFar{1}; float nearR = 40.0f, farR = 240.0f; int frame = 0;
  GLuint hdrFbo = 0, hdrColor = 0, depthTex = 0, ldrFbo = 0, ldrColor = 0;
  GLuint expoFbo[2]{}, expoTex[2]{}; int expoIdx = 0; float expoReset = -1;
  GLuint emptyVao = 0, lineVao = 0, lineVbo = 0;
  GLuint thumbHdrFbo = 0, thumbHdr = 0, thumbDepth = 0;
  GLuint queries[2]{}; int qidx = 0; bool qvalid = false;
  mat4 lightVP{1}, occVP{1};
  float occTop = 0, occRange = 1;
  bool thumbMode = false;
  float pxAngle = 0.001f;   // radians per framebuffer pixel of the current main view (thin-line widening)
  float time = 0;
  int shadowSize = 4096, occSize = 2048;
};
