#pragma once
#include "assets.h"
#include "terrain.h"
#include "combat.h"
#include "grass.h"
#include "frustum.h"
#include "light_types.h"
#include <unordered_map>

// Per building copy parameters of the wall / floor finish patterns 17-22 (docs/interiors.md): no shader edits needed
// to vary wear, palette or room finishes. Rooms and sills are in asset space.
struct SurfaceParams {
  vec4 inst{0, 1, 1, 0};                 // seed (0..1), wear (0..2), palette variation (0..1), room count
  vec4 roomA[16]{}, roomB[16]{};         // (min x, min z, max x, max z) / (floor y, ceiling y, wall finish, floor finish)
  vec4 sillA[12]{}, sillB[12]{}; int sills = 0;   // (centre x, sill y, centre z, half width) / (outward normal x, z, -, -)
  vec4 roomDec[16]{}; vec4 extDec{0};    // GX5 static decals: (first, count) in Renderer::sdec per room / on the exterior
  glm::ivec2 roomX{0};                   // GX5: rooms past 16 (large buildings) as records in Renderer::sdec: first, count
};
struct StaticDecal { vec4 a, b, c; };    // GX5 (static_decals.h): centre + type, normal + seed, half size + strength + variant
struct DrawItem { const GpuMesh* mesh; mat4 model; vec4 tint{1, 1, 1, 1}; const SurfaceParams* surf = nullptr; vec4 apWorld{0}; float glow = 1.0f;   // apWorld.w = 1: world-space pattern coordinates (V6 snap.cpp); glow: emissive multiplier (0 = lamp off, L2)
  float castR = 0;          // R2 castercull: > 0 = casts sun shadows only within this many metres of the eye (small props; interior.cpp)
  bool mainHidden = false;  // R2 roomcull: not drawn in the camera pass (an interior seen through no opening); shadows and probes keep it
};
struct TreeDef;
// lod: the instance's leaf LOD (InstanceRT::leafLod, kept for hysteresis); cur: this frame's (render.cpp selectLeafLods)
struct FoliageDraw { const TreeDef* tree; mat4 m; uint8_t* lod = nullptr; uint8_t cur = 2; };
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
extern Overlay gAnnotOverlay;   // capture annotations (capture.cpp: the D-day overview), drawn with the editor overlay

struct RenderView {
  mat4 view, proj; vec3 camPos;
  bool editor = true, grid = false, autoExposure = false;
  float dt = 0.016f;
  // first-person weapon: own fixed-FOV projection (identity = use proj) and the depth-of-field focus
  mat4 vmProj{0};
  float dofFocus = 0, dofAmount = 0;   // metres from the eye; 0..1 (ADS blend)
  // optic eyepiece (W2): radius of the clear circle (fraction of the frame height; 0 = none) and its reticle:
  // 1 German post with side bars (ZF39), 2 crosshair with a lower post (M1C), 3 TZF gunner sight (PaK 40)
  float eyepieceR = 0; int reticle = 0;
  bool underwater = false;             // camera below the water surface (swim.cpp): dense fog, tint, vignette
  vec3 underFogCol{0.06f, 0.24f, 0.25f}; float underFogDens = 0.11f;   // movement.json "swim"
};

struct FrameStats { double cpuMs = 0, gpuMs = 0; int draws = 0, tris = 0; int culled = 0; double probeCpuMs = 0; int probes = 0;
                    // R2: sun-cascade casters skipped by castercull, camera-pass items hidden by roomcull, sun-cascade draws; GI probes
                    // waiting for a first capture / invalidated, the oldest capture among probes within 12 m (frames)
                    int casterCulled = 0, roomCulled = 0, shadowDraws = 0, probePending = 0, probeDirty = 0, probeStaleMax = 0; };   // culled: items / cards / parts skipped by the per-pass frustum tests (all passes); probeCpuMs / probes: CPU time and count of the GI probe captures (lighting.cpp)
// GPU time per render pass from timestamp queries (Renderer::gpuMark, docs/rendering.md#gpu-budget), in frame order:
// a pass runs from its mark to the next mark in GPU time; passes that did not run read 0
enum GpuPass : int { GP_CloudShadow, GP_ShadowNear, GP_ShadowFar, GP_SkyOcc, GP_LightShadows, GP_Probes, GP_Clusters,
                     GP_Clears, GP_Sky, GP_Terrain, GP_Opaque, GP_Models, GP_Trees, GP_Grass, GP_Nature, GP_MsaaResolve, GP_Water, GP_Gore,
                     GP_Glass, GP_Composite, GP_Taa, GP_Bloom, GP_Fx, GP_Weapon, GP_Post, GP_UI, GP_Count };
const char* gpuPassName(int p);
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
    bool ssao = true; float ssaoStrength = 0.95f, ssaoRadius = 0.5f;   // GX5: 0.85 / 0.6 -> 0.95 / 0.5 (contact shadows tuned for interiors, brief §8)
    bool bloom = true; float bloomStrength = 0.045f;
    bool ssr = true; float ssrStrength = 1.0f;
    bool taa = true; float taaSharpen = 0.30f, taaJitter = 0.5f;   // V1: sharpen 0.40 -> 0.30 (facade shimmer A/B, docs/rendering.md 9.5)
    int taaPhases = 8;          // Halton (2,3) jitter phases (settings: High uses 16)
    int matLayer = 1;           // GX5 layered materials: 0 off (Low), 1 wear / grime / seams / detail, 2 + High extras
    // P3 per-preset costs (settings.cpp apply, by shadowQuality / gi). Defaults = Medium; High: 40 m, 16, 2, 12 m
    float shadowNearR = 32.0f;  // near sun cascade radius while walking (m)
    int shadowTaps = 6;         // cap on the sun PCF taps per cascade lookup (the shaders ask for 4-8)
    int farEvery = 4;           // the far cascade is redrawn every n-th frame (perf.farCache; or when its focus moves)
    float leafShadowLodM = 8;   // R2 leaf shadow LOD: full cards inside this distance from the eye (near cascade)
    int ssaoDiv = 4;            // SSAO resolution divisor when not full resolution
    bool probeHalf = true;      // probe GI capture budget halved (Medium, near-probe GI)
  } q;
  // render targets set by the settings presets (settings.cpp, docs/settings.md). width / height are the internal
  // render size = display size x resolution scale; the final blit upscales to displayW x displayH.
  void setShadowSizes(int nearSize, int farSize);   // recreates the shadow maps when a size changes
  void setResolutionScale(float s);                  // 0.5 .. 1
  void setSsaoFull(bool full);                       // SSAO at full instead of half resolution
  void setMsaa(int samples);                         // B1: 4 = 4x MSAA on the opaque world (High), 0 = off
  // per-pass GPU timers: gpuMark(p) starts pass p (render() marks its passes, the app marks GP_UI's end with GP_Count).
  // Results are read without stalling, 2-3 frames late. Off (all 0) when timestamp queries are unsupported.
  void gpuMark(int pass);
  double gpuPassMs[GP_Count] = {};
  bool gpuTimers = true;
  // PF2 GPU-cost switches (docs/rendering.md#gpu-budget). --rq-off <name> / --rq-on <name> flip one for A/B;
  // legacy() is the pre-PF2 renderer (the maps suite's interleaved A/B, NMK_PERF_AB).
  struct Perf {
    bool syncGpuQuery = false;   // asyncq (off = true): read the frame's GL_TIME_ELAPSED query back next frame, waiting for the GPU
    bool farCache = true;        // farcache: the far shadow cascade is redrawn every 2nd frame (or when its focus moves)
    bool occCache = true;        // occcache: the sky-occlusion map is redrawn every 16 frames (or when the camera moves 4 m)
    bool smallCasters = true;    // smallcasters: the far cascade and sky occlusion skip casters smaller than ~1 texel
    bool probeCull = true;       // probecull: GI probe captures skip items under ~1 capture texel (lighting.cpp)
    bool leafPrepass = true;     // leafprepass: canopy depth prepass, then GL_EQUAL shading (no overdraw shading)
    bool skyLast = true;         // skylast: the sky after the opaque world (early-Z skips it under geometry)
    bool terrainLast = true;     // terrainlast: the terrain after solids, models and canopies (the ground they hide is never shaded)
    bool probeRefresh = true;    // proberefresh: full probe budget only while probes wait for a first capture, then 2 / 1 per frame
    bool hlod = false;           // hlod (--rq-on): buildings merged per 128 m cell (hlod.cpp); off: on a 2080 Ti it saved 4-12 % of draws but cost +0.2-0.5 ms (rendering.md 12.4)
    bool assetLod = true;        // assetlod: part-built assets drawn with their LOD meshes by screen size; sub-pixel props skipped
    bool probeCapture = true;    // probecapture (measurement): off = no probe captures this frame (GI frozen); A/B = the live capture cost
    bool matLayer = true;        // matlayer (GX5): the wear / grime / seam / detail material layer
    bool staticDecals = true;    // sdecals (GX5): the static wall decals (stains, soot, cracks, scuffs, ...)
    bool grade = true;           // grade (GX5): the parameterised grade, vignette, sharpen (off = the old fixed grade)
    bool casterCull = true;      // castercull (R2): small props cast sun shadows only near the eye (DrawItem::castR)
    bool leafShadowLod = true;   // leafshadowlod (R2): canopies past 12 m (all in the far cascade / sky occlusion) cast from their shadow card LOD
    bool probeBudgetOn = true;   // probebudget (R2): GI captures budgeted by cost (a far-volume probe = 4 near ones), near / invalidated first
    bool roomCull = false;       // roomcull (R2, off: hid ~20 draws a view for +0.15 ms of buildDrawLists, A/B 2026-11-09): furnished interiors seen through no opening are skipped in the camera pass (interior.cpp)
    bool propDist = false;       // propdist (R2, off until its A/B is in): small decorative (non-blocking) props drawn to a size-aware distance (interior.cpp)
    bool terrainCache = true;    // terraincache (R1): kTerrainFS reads its stable low / mid-frequency noise from a baked world-space cache (rendering.md 13)
    bool matLod = true;          // matlod (R1): base kMainFS pattern noise fades to its mean by projected footprint (near / mid / far detail)
    bool opqSort = true;         // opqsort (R1): opaque building groups submitted front to back (by their nearest copy)
    bool zPrepass = false;       // zprepass (R1 experiment): depth-only pass of the building groups, then shading at equal depth
    bool grassShadow = true;     // grassshadow (G1, measurement): off = pasture grass casts no sun shadow; A/B = its share of the near cascade
    bool leafTex = true;         // leaftex (L1): species leaf textures + leaf meshes with LOD (off = the procedural oval cards)
    bool taau = true;            // taau (P1): below scale 1 the world renders at the resolution scale and TAA reconstructs at output size (rendering.md 14); off = native resolution
    static Perf legacy() { Perf p; p.syncGpuQuery = true; p.farCache = p.occCache = p.smallCasters = p.probeCull = p.leafPrepass = p.skyLast = p.probeRefresh = p.terrainLast = p.assetLod = p.hlod = false;
                           p.casterCull = p.leafShadowLod = p.probeBudgetOn = p.roomCull = p.propDist = p.taau = false; return p; }
    bool set(const std::string& n, bool on) {   // false: no such switch
      bool* b = n == "farcache" ? &farCache : n == "occcache" ? &occCache : n == "smallcasters" ? &smallCasters : n == "probecull" ? &probeCull
              : n == "leafprepass" ? &leafPrepass : n == "skylast" ? &skyLast : n == "terrainlast" ? &terrainLast : n == "probecapture" ? &probeCapture : n == "proberefresh" ? &probeRefresh : n == "assetlod" ? &assetLod : n == "hlod" ? &hlod : n == "matlayer" ? &matLayer : n == "sdecals" ? &staticDecals : n == "grade" ? &grade
              : n == "terraincache" ? &terrainCache : n == "matlod" ? &matLod : n == "opqsort" ? &opqSort : n == "zprepass" ? &zPrepass : n == "grassshadow" ? &grassShadow : n == "leaftex" ? &leafTex : n == "taau" ? &taau
              : n == "castercull" ? &casterCull : n == "leafshadowlod" ? &leafShadowLod : n == "probebudget" ? &probeBudgetOn : n == "roomcull" ? &roomCull : n == "propdist" ? &propDist : nullptr;
      if (n == "r2") { casterCull = leafShadowLod = probeBudgetOn = roomCull = propDist = on; return true; }   // R2: every switch together
      if (n == "asyncq") { syncGpuQuery = !on; return true; }
      if (n == "gx5") { matLayer = staticDecals = grade = on; return true; }   // GX5: every material / decal / grade switch together
      if (b) *b = on;
      return b != nullptr;
    }
  } perf;
  float leafLodScale = 1.0f;   // L1 leaf LOD distances (kLeafNearIn ...) x this; settings.cpp: 0.6 at grass quality Low / Off (the Low preset)
  // GX5 grade (content/settings/grade.json): saturation, cool shadows / warm highlights, S-curve, vignette, sharpen
  struct Grade { bool on = true; float saturation = 0.91f, contrast = 0.08f, vignette = 0.22f, sharpen = 0.22f, chromatic = 0.0f;
                 vec3 shadowTint{0.96f, 0.99f, 1.05f}, highlightTint{1.04f, 1.0f, 0.95f}; } grade;
  // GX5 static decals (static_decals.cpp): every building's list in one SSBO (binding 10), a block per copy
  std::vector<StaticDecal> sdec; std::unordered_map<uint64_t, glm::ivec2> sdecRange; std::vector<glm::ivec2> sdecFree;
  bool sdecDirty = false; GLuint sdecBuf = 0;
  void uploadStaticDecals();   // before any main-shader pass of the frame (render.cpp)
  int msaa() const { return msaaSamples; }
  bool msaaActive() const { return msFbo != 0; }   // the multisampled world target exists (complete)
  int shadowNearPx() const { return shadowSize; }
  int shadowTier = 1;         // P3: the settings' shadowQuality the maps were sized for (Settings::capture reads it back)
  int shadowFarPx() const { return shadowFarSize; }
  float resolutionScale() const { return resScale; }
  bool ssaoFull() const { return aoDiv == 1; }
  int displayW = 0, displayH = 0;
  void resetTemporal() { taaValid = false; }   // camera cut / teleport / mode switch
  // Mac hook (MetalFX temporal scaler, rendering.md 14.4): when set and it returns true, it replaced the GL TAAU shader
  // exposure: the engine's 1x1 R32F exposure (last frame's; the tonemap multiplies the HDR colour by it)
  struct TemporalUpscale { GLuint color, depth, motion, outColor, outDepth, exposure; int inW, inH, outW, outH; vec2 jitterPx; mat4 invVP, prevVP; bool reset; };
  // The platform installs it at start-up (normandy-mac mac_window.mm: the MetalFX temporal scaler); hookOn switches it
  // per renderer (A/B, captures). With a hook, motion is the full per-pixel motion in input pixels toward the previous
  // position (RG16F, motionTex: velTex where animated, else the camera reprojection), and the engine writes outDepth.
  static inline bool (*temporalUpscaler)(const TemporalUpscale&) = nullptr;
  bool hookOn = true, hookWas = false;
  vec2 hookSign{1.0f, 1.0f};   // sign checks (NMK_TAAU_SHOTS): x multiplies the jitter handed over, y the motion's y
  // Upscaling (settings "upscaling"): 0 Temporal (TAAU, or the platform's temporal hook), 1 Spatial (the pre-P1 path:
  // everything at the internal size, then the final blit upscales; MetalFX spatial on the Mac), 2 Off (native)
  int upscaleMode = 0;
  GLuint progMotion = 0, progDepthUp = 0, motionTex = 0, motionFbo = 0; int motionW = 0, motionH = 0;
  // offscreen orthographic/perspective render of textured parts (weapon calibration sheets, tests)
  bool renderModelSheet(const std::vector<ModelDraw>& items, const std::vector<DrawItem>& boxes, const mat4& view, const mat4& proj, vec3 camPos, int w, int h, std::vector<uint8_t>& rgba);
  vec3 sunDir = glm::normalize(vec3(-0.45f, 0.62f, 0.38f));   // the Day sun; inside render() the key light of the time of day (lighting.cpp)
  // ---- lighting engine (src/lighting.cpp, docs/lighting.md): time of day, clustered local lights, light shadows, probe GI
  struct LightQ {
    float hour = 14.0f;         // time of day, 0..24 (14:00 = Day: exactly the pre-L2 sun, sky and exposure)
    int maxLights = 256;        // local lights uploaded per frame (nearest first)
    int shadowLights = 2;       // nearest spot / point lights with a shadow map (Low 0, Medium 2, High 8)
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
  void createMsaa(); void resolveMsaa();   // B1 MSAA world target (render.cpp)
  void createShadowTargets();
  float resScale = 1.0f; int aoDiv = 4;   // P3: Medium AO at quarter resolution
  // fr: the pass's own frustum (src/frustum.h, docs/rendering.md#frustum-culling); null = draw everything (thumbnails,
  // model sheets, the first-person weapon). sortSurf: stable sort by building finish parameters (main opaque pass)
  void drawItems(GLuint prog, const std::vector<DrawItem>& items, bool shadowPass, const Frustum* fr = nullptr, bool sortSurf = false);
  void setLighting(GLuint prog, const RenderView& v);
  void drawModels(const std::vector<ModelDraw>& items, const Frustum* fr = nullptr);
  void drawFoliage(GLuint prog, const mat4& vp, const Frustum* fr = nullptr, bool view = false, float lodFrom = -1);   // view: the main view's leaf LODs (else the cards); R2 lodFrom >= 0: trees this far from castEye cast from their shadow LOD
  void selectLeafLods(const RenderView& v);
  // L1 leaf LOD distances (m at a 70 degree FOV): enter near / leave near, enter mid / leave mid (docs/leaves.md)
  static constexpr float kLeafNearIn = 10, kLeafNearOut = 12, kLeafMidIn = 22, kLeafMidOut = 26;
  void drawModelsDepth(GLuint prog, const std::vector<ModelDraw>& items, const Frustum* fr = nullptr);
  // uniform locations of the draw loops, looked up once per program (cleared by compile())
  struct DrawLocs { GLint model = -1, tint = -1, inst = -1, roomA = -1, roomB = -1, sills = -1, sillA = -1, sillB = -1, roomDec = -1, extDec = -1, roomX = -1,
                    vp = -1, time = -1, wind = -1, leafCol = -1, base = -1, metal = -1, rough = -1, thin = -1,
                    alb = -1, mr = -1, nrm = -1, ao = -1, hasAlb = -1, hasMR = -1, hasNrm = -1, hasAO = -1, aoInMR = -1, snapAp = -1, glow = -1; };
  std::vector<std::pair<GLuint, DrawLocs>> locCache;
  DrawLocs drawLocs(GLuint prog);   // by value: the cache may grow while a loop holds it
  std::vector<uint32_t> drawIdx;   // scratch: visible items of the current drawItems call
  GLuint progLeaf = 0, progLeafShadow = 0, progLeafI = 0, progLeafIShadow = 0;   // I: L1 instanced species leaves
  GLuint progModel = 0, progDof = 0, progShaft = 0, progComposite = 0, progEyepiece = 0;
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
  // P1 TAAU (rendering.md 14): the post targets at output size. After the TAA resolve, render() swaps them with the
  // internal ones (hdr, depth, first-person distance, DoF, LDR, history), so fx, the weapon, DoF, exposure, the bloom
  // source and the tonemap run at output resolution; the end of render() swaps back.
  struct UpTargets { GLuint hdrFbo = 0, hdrColor = 0, depthTex = 0, vmDistTex = 0, dofTex = 0, dofFbo = 0, ldrFbo = 0, ldrColor = 0, hist[2]{}, taaFbo[2]{}; int w = 0, h = 0; } up;
  bool upActive = false;     // the last frame ended in the output-size set (readPixels reads it)
  bool histUp = false;       // the TAA history holds output-size (TAAU) frames
  void createUpTargets();
  void swapUp();
  void upDepth();            // TAAU output depth (kDepthUpFS): the nearest internal depth, for effects and overlays             // exchanges the internal and output-size post targets (and width / height)
  float effScale() const { return perf.taau && upscaleMode != 2 ? resScale : 1.0f; }   // taau off / mode Off: native resolution
  GLuint progGrass = 0, progGrassShadow = 0;   // GX3: near pasture clumps into the sun map
  GLuint progFx = 0, fxVao = 0, fxVbo = 0;
  // R1 terrain material cache (rendering.md 13): 2 RGBA8 layers of world-space noise over the terrain extent, mipmapped
  GLuint tcTex = 0, tcProg = 0; vec2 tcOrg{0}, tcExt{0}; int tcW = 0, tcH = 0; float tcTexel = 0.5f; double tcBakeMs = 0, tcMB = 0;
  void terrainCacheBind(const Terrain& t, GLuint prog);   // (re)bakes when the terrain extent changes; sets uTCOn
  GLuint progDepth = 0; bool depthSurfOnly = false;   // R1 zprepass experiment: kMainVS's transform, no shading; building items only
  vec3 sortEye{0};        // R1 opqsort: the main view's eye for drawItems
  GLuint progMain = 0, progShadow = 0, progTerrain = 0, progSky = 0, progGlass = 0, progLine = 0, progTone = 0, progExpo = 0;
  GLuint shadowFbo = 0, shadowTex = 0, occFbo = 0, occTex = 0;
  GLuint shadowFarFbo = 0, shadowFarTex = 0; int shadowFarSize = 2048;
  mat4 lightVPFar{1}; float nearR = 40.0f, farR = 240.0f; int frame = 0;
  GLuint hdrFbo = 0, hdrColor = 0, depthTex = 0, ldrFbo = 0, ldrColor = 0;
  int msaaSamples = 0;
  GLuint msFbo = 0, msRb[5] = {};   // B1 MSAA world target: colour, indirect, normal, velocity, depth (0 = off)
  GLuint expoFbo[2]{}, expoTex[2]{}; int expoIdx = 0; float expoReset = -1;
  GLuint emptyVao = 0, lineVao = 0, lineVbo = 0;
  GLuint thumbHdrFbo = 0, thumbHdr = 0, thumbDepth = 0;
  GLuint queries[3]{}; int qidx = 0; bool qvalid = false; bool qUsed[3]{};   // GL_TIME_ELAPSED ring (fallback when timestamps are missing)
  int farFrame = -1000, occFrame = -1000; vec3 farFocus{0}, farSun{0}, occCam{0}; float minCaster = 0;   // PF2 depth-pass caches
  bool castCull = false; vec3 castEye{0};   // R2 castercull: on during the sun cascades; castEye: the eye the caster ranges are measured from
  GLuint gq[3][GP_Count + 1]{}; bool gqSet[3][GP_Count + 1]{}; int gqSlot = 0;
  void gpuFrameBegin();
  mat4 lightVP{1}, occVP{1};
  float occTop = 0, occRange = 1;
  bool thumbMode = false;
  float pxAngle = 0.001f;   // radians per framebuffer pixel of the current main view (thin-line widening)
  float time = 0;
  int shadowSize = 2048, occSize = 2048;   // P3: Medium near cascade 2048
};
