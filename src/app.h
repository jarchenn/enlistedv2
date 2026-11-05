#pragma once
#include "assets.h"
#include "render.h"
#include "terrain.h"
#include "mapdoc.h"
#include "physics.h"
#include "audio.h"

struct GLFWwindow;

enum class Mode { Creative, Explore };
enum class Tool { Select, Place, Marker };

// Runtime (non-saved) state for one placed instance. Rebuilt whenever Instance::rev changes.
struct InstanceRT {
  uint64_t id = 0; uint32_t rev = 0;
  const AssetDef* asset = nullptr;
  uint32_t staticBody = 0xffffffffu;
  std::vector<std::vector<uint32_t>> leafBodies; // [interactable][leaf]
  std::vector<float> progress;                    // 0 closed .. 1 open
  std::vector<int> dir;                           // -1 closing, 0 idle, +1 opening
};

struct ExploreInput { vec2 move{0}; bool sprint = false, crouch = false, jump = false, interact = false; float dyaw = 0, dpitch = 0; };

struct Snapshot { std::vector<Instance> inst; StartMarker start; std::set<std::string> removedVeg; uint64_t selected = 0; };

struct PlaceCheck { bool ok = true; std::string reason; float y = 0; };

struct InteractTarget { bool valid = false; uint64_t inst = 0; int idx = -1; float t = 0; };

class SelfTest;

class App {
public:
  int run(int argc, char** argv);

  // ---- core
  GLFWwindow* win = nullptr;
  Renderer R;
  AssetLibrary lib;
  Terrain terrain;
  std::vector<TemplateInfo> templates;
  MapDoc doc;
  fs::path docPath;
  bool dirty = false;
  Mode mode = Mode::Creative;
  std::map<uint64_t, InstanceRT> rt;
  uint32_t revCounter = 1;
  Audio audio;
  double now = 0;
  float frameDt = 0.016f;
  bool quit = false;
  std::string notice; double noticeUntil = 0;
  void toast(const std::string& s, double secs = 4.0);

  // ---- camera (Creative)
  vec3 camPos{0, 40, 80}; float camYaw = 0, camPitch = -20, flySpeed = 20;
  bool looking = false;
  mat4 viewM{1}, projM{1};

  // ---- editor
  Tool tool = Tool::Select;
  const AssetDef* placing = nullptr;
  float placeYaw = 0;
  bool gridSnap = false, rotSnap = true, gridOverlay = false, editInteract = false, showBrowser = true, showStats = false;
  uint64_t selected = 0;
  bool dragging = false, dragPending = false; vec3 dragOffset{0}; vec2 dragStartMouse{0}; Instance dragOrig;
  std::vector<Snapshot> undoStack, redoStack;
  std::string browserFilter; int browserCategory = 0; bool browserEnterableOnly = false; int browserMinFloors = 0;
  bool showNewMap = false, showOpen = false, showSaveAs = false;
  char nameBuf[128] = "My Normandy map";
  char saveAsBuf[128] = "my_map";
  int newTemplateIdx = 0;
  PlaceCheck lastCheck;
  vec3 hoverGround{0}; bool hoverValid = false;

  // ---- explore
  float eyeYaw = 0, eyePitch = 0, eyeHeight = 1.62f;
  bool pauseMenu = false;
  vec3 exploreStartPos{0}; float exploreStartYaw = 0;
  InteractTarget target;
  float physAccum = 0;
  float stepTimer = 0;
  EditorCam savedCam;
  ExploreInput scriptedInput; bool useScripted = false;

  // ---- self test
  SelfTest* selftest = nullptr;

  // ---- runtime/doc management
  const AssetDef* assetFor(const Instance& i) const;
  mat4 instanceMatrix(const Instance& i) const;
  void syncRuntime();
  void destroyRT(InstanceRT& r);
  void buildRT(const Instance& i, InstanceRT& r);
  void resetInteractStates();
  bool authoredOpen(const Instance& i, const InteractDef& d) const;
  float leafAngle(const InteractDef& d, float p) const;
  mat4 leafMatrix(const Instance& i, const Leaf& l, const InteractDef& d, float p) const;
  void updateLeafBodies(const Instance& i, InstanceRT& r, int k);
  void clearRuntime();
  void buildDrawLists(std::vector<DrawItem>& op, std::vector<DrawItem>& gl);
  void bump(Instance& i) { i.rev = ++revCounter; dirty = true; }

  bool newMap(const TemplateInfo& t, const std::string& name);
  bool openMap(const fs::path& p);
  bool saveMapTo(const fs::path& p);
  void applyTemplate(const std::string& id);
  const TemplateInfo* findTemplate(const std::string& id) const;
  void makeThumbnails();

  // ---- editor ops
  void pushUndo();
  void undo();
  void redo();
  Snapshot snap() const;
  void restore(const Snapshot& s);
  PlaceCheck checkPlacement(const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId) const;
  uint64_t placeInstance(const AssetDef* a, vec3 pos, float yaw);
  bool duplicateSelected();
  void deleteSelected();
  uint64_t pickInstance(vec3 ro, vec3 rd, float& t) const;
  void cursorRay(vec3& ro, vec3& rd) const;
  bool startMarkerValid(vec3 pos) const;

  void updateCreative(float dt);
  void drawCreativeUI();
  void drawCreativeOverlay(Overlay& ov, std::vector<DrawItem>& ghosts);

  // ---- explore ops
  bool enterExplore();
  void exitExplore();
  void returnToStart();
  void updateExplore(float dt);
  void exploreStep(float dt, const ExploreInput& in);
  void drawExploreUI();
  void toggleInteract(uint64_t inst, int idx);
  bool leafBlocked(const Instance& i, const InteractDef& d, float p) const;
  InteractTarget findTarget(vec3 eye, vec3 dir) const;
  vec3 eyePos() const;
  static vec3 forward(float yawDeg, float pitchDeg);

  void screenshot(const fs::path& p);
  fs::path uiShot;   // when set, the next frame including UI is written here
};

extern App* gApp;
