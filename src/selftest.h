#pragma once
#include "app.h"

// Scripted end-to-end milestone test that drives the real editor, physics and
// character controller (see docs/testing.md). Runs with a visible window.
class SelfTest {
public:
  explicit SelfTest(bool benchmark) : bench(benchmark) {}
  void update(App& app, float dt);  // called once per frame before mode updates
  bool done = false;
  bool slowFrames = false;   // simulate 10 fps frames (frame-drop tests)
  bool realTime = false;     // wall-clock frame time (LAN tests: processes must share one clock)
  int exitCode = 0;
private:
  bool bench;
  int stage = 0;
  double stageTime = 0;
  std::vector<json> steps;
  size_t stepIdx = 0;
  double stepTime = 0;
  uint64_t houseId = 0;
  std::string curAsset = "house_farmhouse_2story";
  size_t extraIdx = 0;
  int pass = 0;
  std::vector<std::string> results;
  int failures = 0;
  void check(bool ok, const std::string& what);
  void beginWalkthrough(App& app);
  bool runWalkthrough(App& app, float dt); // returns true when finished
  vec3 toWorld(App& app, vec3 local) const;
  float worldYaw(App& app, float localYaw) const;
  void finish(App& app);
  std::vector<std::string> shotQueue;
  int benchFrames = 0; std::vector<double> frameTimes; std::string benchName;
  json benchOut = json::object();
  // destruction / ground / weapons integration tests (selftest_damage.cpp)
  std::string reportName;
  void damageUpdate(App& app, float dt);
  void pakUpdate(App& app, float dt);
  void weaponsUpdate(App& app, float dt);
  void captureUpdate(App& app, float dt);
  void grassUpdate(App& app, float dt);
  void recordUpdate(App& app, float dt);
  void fpsUpdate(App& app, float dt);    // selftest_fps.cpp (1100)
  void envUpdate(App& app, float dt);    // selftest_env.cpp (1200)
  void moveUpdate(App& app, float dt);      // selftest_move.cpp (1500)
  void sightsUpdate(App& app, float dt);
  void updateSuite(App& app, float dt);
  void entitiesUpdate(App& app, float dt);  // selftest_entities.cpp (1900)     // selftest_update.cpp (1700)    // selftest_sights.cpp (1600); dt < 0 = post-render read-back
  void aiUpdate(App& app, float dt);        // selftest_ai.cpp (2000): multiplayer + AI integration
  void netUpdate(App& app, float dt);       // selftest_net.cpp (2100): LAN roles host / a / b / c / d
  void arsenalUpdate(App& app, float dt);   // selftest_arsenal.cpp (1400)
  void tanksUpdate(App& app, float dt);     // selftest_tanks.cpp (2600)
  void interiorsUpdate(App& app, float dt); // selftest_interiors.cpp (2800): walls, interiors and furniture
  void rulesUpdate(App& app, float dt);     // selftest_rules.cpp (2700): W1 gameplay rules
  void visualsUpdate(App& app, float dt);   // selftest_visuals.cpp (3250): W2 thumbnails, footprint labels, bunkers
  void editorUpdate(App& app, float dt);    // selftest_editor.cpp (3300): map size, chunks, cuts, camera, drop-in
  void villageUpdate(App& app, float dt);   // selftest_village.cpp (1300 build, 1350 benchmark)
  void structuresUpdate(App& app, float dt);   // selftest_structures.cpp (3800): W4 structure assets
  void mapsUpdate(App& app, float dt);         // selftest_maps.cpp (4100): W5 default maps
  bool terrainBaseChecks(App& app, const TemplateInfo& t, const json& mj);   // selftest_maps.cpp: W5b terrain-only template
  void authoredChecks(App& app, const TemplateInfo& t, const json& mj);     // selftest_maps.cpp: a saved map promoted to a template
  void beginCustom(App& app, const std::string& asset, uint64_t inst, const json& steps);
  json summary = json::object();
};
