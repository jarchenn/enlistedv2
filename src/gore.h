#pragma once
// Gore system internals (SPEC 7, workstream W7). The contract with the rest of the game is gore_api.h; this header is
// the W7-side API: the one-line hooks (render pass, LAN event, options UI, self-test) and the state the decal renderer
// (decals.cpp) and the self-test (selftest_gore.cpp) read.
//
// Model:
//  - every flesh hit makes bone decals on the victim's skinned mesh (entry wound, exit wound, a soak stain), stored as a
//    triangle + barycentrics of the character mesh, so they follow animation and the death pose exactly; on the GPU they
//    are projected in the mesh's rest space (decals.cpp redraws the soldier's skinned vertex buffer with a decal shader);
//  - back-spatter / forward-spray droplets are simulated on the CPU (gravity, quadratic drag), seeded from FleshHit.seed
//    so every LAN client produces the same droplets from the one replicated event;
//  - landed droplets, the 2.5 m splatter ray, drips and corpse pools are world decals: boxes projected onto the depth
//    buffer (any surface), several droplet stains of one burst share one decal ("stamps");
//  - all colours are multiplicative over the lit scene (shadows and lighting stay correct), fresh red + gloss ageing to
//    maroon-brown over decalAgeToBrown_s.
#include "gore_api.h"

class Renderer;
struct RenderView;

enum class GoreLevel : int { Off = 0, Reduced = 1, Full = 2 };

struct GoreCfg {
  GoreLevel level = GoreLevel::Full;
  int maxWorldDecals = 256, maxDecalsPerCharacter = 24, maxDroplets = 1500;
  float mistOnlyBeyond = 60, ageToBrown = 60, decalFade = 1.5f, drawDistance = 150;
  vec2 poolGrow{10, 20}, poolRadius{0.6f, 1.0f}; float poolDelay = 1.2f;
  vec2 entry{0.02f, 0.04f}, exitW{0.04f, 0.08f}, soak{0.07f, 0.14f}; float soakGrow = 25;
  struct Spray { vec2 count, cone, speed, radiusMm; float mist; } back{{10, 25}, {30, 45}, {1, 4}, {0.5f, 1.6f}, 0.15f}, fwd{{20, 60}, {15, 25}, {3, 9}, {0.8f, 3.0f}, 0.2f};
  float splatterRay = 2.5f;
  float gravity = 9.81f, airDensity = 1.2f, dragCd = 0.47f, dropLife = 4, stainScale = 2.6f, maxAspect = 6;
  float spurtInterval = 0.8f; vec2 spurtDur{3, 6}, spurtCount{10, 18}, spurtSpeed{1.5f, 3.5f}; float spurtCone = 22, spurtDecay = 0.78f;
  vec2 dripSpacing{0.7f, 1.4f}; float dripDuration = 25, dripMinSpeed = 0.6f;
  float redDroplets = 0.35f, redWorld = 0.5f, redChar = 0.5f, redPool = 0.6f; bool redSpurt = false, redDrip = false;
  bool protectedFx = true;
  void load(const json& j);
};

enum GoreDecalKind : int { GD_SPECKS = 0, GD_SPLAT = 1, GD_POOL = 2, GD_ENTRY = 3, GD_EXIT = 4, GD_SOAK = 5 };

struct GoreStamp { vec2 off{0}; float radius = 0.005f, aspect = 1; vec2 tail{1, 0}; };   // decal-plane coordinates (m)
struct WorldDecal {
  vec3 pos{0}, n{0, 1, 0}, t{1, 0, 0};    // centre, surface normal, tangent (stamp x axis)
  float extent = 0.05f, depth = 0.05f;    // box half extents in the plane / along the normal
  double born = 0, fadeStart = -1;
  int kind = GD_SPECKS; uint32_t seed = 1, burst = 0;
  std::vector<GoreStamp> stamps;          // GD_SPECKS: up to kMaxStamps landed droplets
  float r0 = 0, r1 = 0, growT = 0;        // GD_POOL / GD_SPLAT: radius r0 -> r1 over growT s (splat: r1 only)
  static constexpr int kMaxStamps = 8;
};
struct BoneDecal {
  int tri = -1; vec3 bary{1.0f / 3};      // triangle of the character mesh + barycentrics (exact surface point)
  vec3 restPos{0}, restN{0, 1, 0};        // the same point in the mesh's rest (bind) space: the GPU projects it there
  int joint = -1;                         // dominant skin joint at the point (tests: decals follow their bone)
  float radius = 0.015f, growTo = 0, growT = 0;
  double born = 0, fadeStart = -1;
  int kind = GD_ENTRY; uint32_t seed = 1;
};
struct SoldierGore {
  int lifeId = -1, model = -1;
  std::vector<BoneDecal> decals;
  int wounds = 0; double dripUntil = -1; vec3 lastDrip{0}; float nextDrip = 1; bool pooled = false;
  int lastWoundTri = -1; vec3 lastWoundBary{1.0f / 3};
};
struct GoreDroplet { vec3 p{0}, v{0}; float r = 0.001f, age = 0; uint32_t burst = 0; };
struct GoreDropInit { vec3 p{0}, v{0}; float r = 0.001f; };
struct GorePuff { vec3 p{0}, v{0}; float size = 0.1f, grow = 0, life = 0.15f, maxLife = 0.15f; vec4 col{1}; bool flat = false; };
struct GoreBleeder { int slot = -1, lifeId = -1, tri = -1; vec3 bary{1.0f / 3}; double t0 = 0, until = 0, next = 0; float strength = 1; uint32_t rng = 1; };

// Where a hit lands on the character mesh (host: mesh ray cast; LAN client: replicated).
struct GoreAttach { int model = -1; int entryTri = -1; vec3 entryBary{1.0f / 3}; int exitTri = -1; vec3 exitBary{1.0f / 3}; };

struct GoreState {
  GoreCfg cfg;
  GoreLevel level = GoreLevel::Full;
  bool loaded = false;
  double t = 0;                              // gore clock (s, advanced by goreStep)
  std::vector<GoreDroplet> drops;
  std::vector<WorldDecal> world;             // oldest first
  std::map<int, SoldierGore> sold;           // by roster slot
  std::vector<GoreBleeder> bleeders;
  std::vector<GorePuff> puffs;
  uint32_t nextBurst = 1;
  // statistics (tests / profiling)
  struct Stats {
    int hits = 0, goreHits = 0, dustPuffs = 0, sparks = 0, deflects = 0, mistOnly = 0, ripples = 0, landed = 0, spawned = 0, dropped = 0;
    int boneDecals = 0, worldDecals = 0, pools = 0, splats = 0, spurtPulses = 0, drips = 0, meshMisses = 0;
    size_t maxDrops = 0, maxWorld = 0, maxChar = 0;
    double hitMs = 0; int hitN = 0; double stepMs = 0; int stepN = 0; double gpuMs = 0, cpuRenderMs = 0; int gpuN = 0;
    int netSent = 0, netReceived = 0;
  } st;
  std::map<uint32_t, uint64_t> sprayHashBySeed;   // canonical spray hash per hit seed (LAN test: host and clients agree)
};
extern int gGoreHitsReported;     // every FleshHit given to goreFleshHit (gore.cpp)
GoreState& goreState();
void goreSetLevel(GoreLevel l, bool save = false);
GoreLevel goreLevel();
int goreWorldCap();
int goreCharCap();

// Pure helpers (unit-tested)
// teardrop of a droplet landing with velocity v on a surface with normal n: aspect = 1/sin(impact angle) clamped to
// maxAspect, tail = travel direction projected on the surface
void goreTeardrop(vec3 v, vec3 n, float maxAspect, float& aspect, vec3& tail);
// back-spatter and forward-spray droplets of one hit, from its seed only (plus the entry / exit points used)
void goreGenerateSpray(const FleshHit& h, vec3 entry, vec3 exitPt, bool hasExit, const GoreCfg& c, float countScale,
                       std::vector<GoreDropInit>& back, std::vector<GoreDropInit>& fwd);
uint64_t goreSprayHash(const FleshHit& h);      // canonical (Full level, event fields only)
vec3 goreAgeTint(float age, float ageToBrown); // multiplicative tint of a decal of this age (mirrors the shader)
int goreZone(const FleshHit& h);                // 4 neck / 5 upper thigh derived from the bone when W1 does not set it
// posed position / normal of a mesh point (triangle + barycentrics) of soldier `slot` (world space)
bool gorePosedPoint(int slot, int tri, vec3 bary, vec3& pos, vec3& nrm);

// Hooks (one line each in the shared core)
void goreRenderPass(Renderer& r, const RenderView& v, const mat4& vp, unsigned depthTex);   // render.cpp: after opaque
void goreNetEvent(App& app, const json& ev);    // net.cpp: client applies a replicated "gore" world event
void goreOptionsUI();                            // weapons.cpp: Loadout window (Gore: Off / Reduced / Full)
bool goreSelfTest(App& app, float dt);           // selftest.cpp: --selftest-gore drives the frame (true = handled)
json goreNetStats();                             // selftest_net.cpp: what a client received
uint64_t goreTestInjectHit(App& app);            // selftest_net.cpp (host): one real hit on a bot with the probe seed
constexpr uint32_t kGoreProbeSeed = 0xC0FFEEu;
// self-test helpers
void goreTestDroplet(vec3 p, vec3 v, float r);   // one free droplet (its own burst)
json goreEventJsonFor(const FleshHit& h);         // the LAN event the host would send for this hit

// decals.cpp
void decalsRender(Renderer& r, const RenderView& v, const mat4& vp, unsigned depthTex);
void decalsShutdown();
