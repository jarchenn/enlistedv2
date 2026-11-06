#pragma once
// Play menu, Creative scenario panel / overlay and match HUD (mpui.cpp).
#include "scenario.h"

class App;
struct Overlay;

struct MpUi {
  bool showSetup = false, showJoin = false, showPanel = false, showOverlay = true, wantExit = false;
  int setupMode = 0;                       // 0 singleplayer, 1 host LAN
  Scenario sc; json lastSaved; bool loaded = false;
  std::vector<std::string> migration, worldIssues;
  std::string selObj;
  ScenarioRules rules; bool rulesInit = false;
  json matchScenario;                      // scenario + setup rules used by the next match (map defaults untouched)
  Scenario& scenario(App& app);
  void commit(App& app);
  void creativeMenus(App& app);
  void scenarioPanel(App& app);
  void creativeOverlay(App& app, Overlay& ov);
  void setupWindow(App& app);
  void startMatch(App& app);
  void joinWindow(App& app);
  void netLobby(App& app, bool blocked);   // net.cpp
  void netJoin(App& app);                  // net.cpp
};
extern MpUi gUi;
