#pragma once
// Hooks from the shared core (app.cpp, editor.cpp, explore.cpp) into the W3 editor & terrain systems (SPEC 9.1-9.3):
// map size, chunked terrain, terrain cuts, editor camera, overview map, bookmarks and the drop-in explorer.
// Every hook is a single call so merges with the other workstreams stay small.
// Implemented in editor_nav.cpp (camera, overview, bookmarks, New Map dialog) and dropin.cpp (drop-in explorer).
#include "common.h"

class App;
struct Overlay;
struct TemplateInfo;

void w3ApplyTerrain(App& app, const TemplateInfo& t);   // applyTemplate: terrain at the document's size + chunk colliders
void w3Frame(App& app);                                 // every frame before rendering: terrain cuts, physics sync, camera flight
mat4 w3EditorProjection(App& app, float aspect);        // creative projection: near / far follow the camera height and map size
void w3EditorCamera(App& app, float dt, bool uiMouse, bool uiKey);   // wheel / MMB / RMB / WASD-QE / F / Home / M / bookmarks
void w3EditorWheel(App& app, float scroll);             // wheel while not placing or moving: zoom toward the cursor
void w3Toolbar(App& app);                               // toolbar: drop-in soldier figure
bool w3EnterExplore(App& app);                          // "Explore from start" (F5, menu, toolbar)
void w3Overlay(App& app, Overlay& ov);                  // build-area outline, drop-in ground marker and facing arrow
void w3EditorUI(App& app);                              // New Map dialog, overview map window, drop-in drag
bool w3ExploreKeys(App& app);                           // Explore: Esc back to the editor (creative sessions), P pause menu
const char* w3StatusHelp();                             // creative status-bar controls line
