#pragma once
// W3 editor navigation, overview map, bookmarks, New Map sizing and the drop-in explorer: shared state and the
// functions the self-test (selftest_editor.cpp) drives directly. Hooks into the core are in w3.h.
#include "w3.h"
#include "terrain.h"

class App;

struct NavCamSettings {
  float speedBase = 6, speedPerMetre = 0.9f, speedMin = 4, speedMax = 900, shiftMult = 3;
  float zoomStep = 0.18f, zoomMinDist = 1.5f, orbitDegPerPixel = 0.25f, minClearance = 0.6f, frameMargin = 1.15f;
  float flightSeconds = 0.35f, homePitch = -55.0f;
  int overviewTexture = 1024; float overviewFraction = 0.78f;
  float facingMinDrag = 0.8f, markerRadius = 0.7f, arrowLength = 3.0f;
  float cutEdgePad = -1;   // terrain-cut outline dilation (m); < 0 = one deformation-tile grid step
};
const NavCamSettings& navSettings();

struct CamPose { vec3 pos{0}; float yaw = 0, pitch = 0; };

struct NavState {
  // mouse gestures
  bool orbiting = false, panning = false;
  vec3 pivot{0}, panAnchor{0}; float panPlaneY = 0;
  double lastMx = 0, lastMy = 0;
  float speedMult = 1.0f;                       // Alt+wheel
  // camera flight (F, Home, bookmarks, overview jumps)
  bool flying = false; CamPose from, to; float flightT = 0, flightDur = 0.35f;
  // overview map (M)
  bool overview = false;
  vec2 ovCentre{0}; float ovScale = 0;          // world x/z at the canvas centre, pixels per metre (0 = fit on open)
  uint32_t ovTexGen = ~0u; GLuint ovTex = 0; int ovTexW = 0, ovTexH = 0;
  bool ovDragging = false; vec2 ovPressPos{0};
  // drop-in explorer
  bool dropDrag = false, dropValid = false; vec3 dropPos{0}; float dropYaw = 0; vec3 dropFacePos{0}; bool dropFaceSet = false;
  bool creativeSession = false;                 // Explore entered from Creative (drop-in / Explore from start): Esc returns
  bool escReturn = false;                       // Esc pressed in such a session: return once it is released
  CamPose dropSavedCam;                         // editor camera at the drop (checked against the restore)
  vec2 pendingSize{0};                          // New Map: size consumed by the next w3ApplyTerrain
  int cutsApplied = 0;                          // tiles re-cut by the last cut sync (tests)
  // V6 Select-tool LMB gesture (edSelectPointer, snap.h): 0 none, 1 pressed (click until it moves 4 px), 2 orbit, 3 move
  int lmbGesture = 0; bool lmbPrev = false, pressOnSel = false, pressGround = false;
  uint64_t pressId = 0; vec2 pressPx{0}, lastPx{0}; vec3 pressGroundPt{0}, lpivot{0}; float moveYaw = 0, appliedYaw = 0;
};
NavState& navState();

// camera maths (pure: no input)
vec3 camForward(float yaw, float pitch);
float yawOfDir(vec3 d);                         // inverse of App::forward
float navFlySpeed(const App& app);              // m/s at the current height (before the Shift / Alt multipliers)
void navZoomAlongRay(App& app, float scroll, vec3 ro, vec3 rd);   // dolly toward the ground point on the ray
void navOrbit(App& app, vec3 pivot, float dyawDeg, float dpitchDeg);   // rigid rotation of the camera about pivot
void navPanTo(App& app, vec3 anchor, float planeY, vec3 ro, vec3 rd);  // move so the anchor sits on the ray again
// camera placed on yaw / pitch so the box fits the view (aspect = width / height); returns the position
vec3 navFrameBox(vec3 lo, vec3 hi, float yaw, float pitch, float aspect, float fovYDeg, float margin);
bool navBoxInView(vec3 lo, vec3 hi, CamPose cam, float aspect, float fovYDeg, float* maxNdc = nullptr);
bool navFrameSelection(App& app);               // F (flight)
void navFrameMap(App& app);                     // Home (flight)
CamPose navHomePose(const App& app, float aspect);
void navFlyTo(App& app, CamPose to);
void navFinishFlight(App& app);                 // jump to the flight's end (tests)
void navStepFlight(App& app, float dt);
float navAspect(const App& app);
// bookmarks: MapDoc::cameraBookmarks["1".."9"] (saved under editorCamera.bookmarks)
void navSaveBookmark(App& app, int n);
bool navRecallBookmark(App& app, int n);        // starts a flight; false when slot n is empty
bool navBookmark(const App& app, int n, CamPose& out);
// overview map
void navOverviewJump(App& app, vec2 worldXZ);
// terrain cuts from the placed instances (also run every frame by w3Frame)
int navSyncCuts(App& app);
// New Map
bool navNewMapSized(App& app, const TemplateInfo& t, const std::string& name, vec2 size);
// drop-in explorer (dropin.cpp)
bool dropinCommit(App& app, vec3 groundPos, float yaw);   // saves the explore start and enters first person there
bool dropinReturn(App& app);                              // back to the editor camera exactly as it was
void dropinUI(App& app);
void dropinOverlay(App& app, Overlay& ov);
void dropinDragUpdate(App& app, vec3 hit, bool valid);    // facing from the drag direction over the ground
