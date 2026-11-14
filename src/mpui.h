#pragma once
// Play menu, Creative scenario panel / overlay and match HUD (mpui.cpp).
#include "scenario.h"

class App;
struct Overlay;

struct MpUi {
  bool showSetup = false, showJoin = false, showPanel = false, showOverlay = true, wantExit = false, exitToEditor = false;   // UI1: exitToEditor: the end screen's Exit (no setup dialog)
  int setupMode = 0;                       // 0 singleplayer, 1 host LAN
  Scenario sc; json lastSaved; bool loaded = false;
  std::vector<std::string> migration, worldIssues;
  std::string selObj, selSpawn;
  enum Tool { T_NONE, T_OBJECTIVE, T_ATK, T_DEF, T_POLY, T_SLOT, T_LINK, T_TEAM_A, T_TEAM_B } tool = T_NONE;
  enum Drag { D_NONE, D_OBJ, D_RADIUS, D_VERTEX, D_SPAWN } drag = D_NONE;
  int dragIndex = 0, slotKind = 0, slotPriority = 2; bool lmbPrev = false;
  std::vector<ScenarioIssue> lastValidated; bool validatedOnce = false;
  ScenarioRules rules; bool rulesInit = false;
  json matchScenario;                      // scenario + setup rules used by the next match (map defaults untouched)
  Scenario& scenario(App& app);
  void commit(App& app);
  void creativeMenus(App& app);
  void scenarioPanel(App& app);
  void creativeOverlay(App& app, Overlay& ov);
  bool creativeMouse(App& app);            // mouse tools + drag handles; true = consumed (editor skips its picking)
  bool beforeSave(App& app);               // save hook: refuses a scenario with spawns but no objective
  // editing helpers used by the panel, the mouse tools and the tests
  bool addObjectiveAt(App& app, vec3 p);
  std::string addSpawnAt(App& app, CombatRole role, vec3 p, int phase);   // places the team weapons rack beside it
  std::string addTeamSpawnAt(App& app, int team, vec3 p);   // Team Deathmatch marker for team 0 (A) / 1 (B), with a rack
  bool deleteSpawn(App& app, const std::string& id);                      // also removes its rack
  bool deleteObjective(App& app, const std::string& id);                  // spawns serving only it go too (with racks)
  uint64_t placeRack(App& app, const SpawnMarker& m);
  void removeRack(App& app, uint64_t id);
  void setupWindow(App& app);
  void startMatch(App& app);
  void joinWindow(App& app);
  void netLobby(App& app, bool blocked);   // net.cpp
  void netJoin(App& app);                  // net.cpp
};
extern MpUi gUi;
