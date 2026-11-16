// UI2 main menu (mainmenu.cpp): shown at launch over a graded still of our own map, with the logo, the left menu
// (Create Map, Play, Character, Settings, Guns, Vehicles, Credits, Quit) and its right-hand glass panels (ED2: Guns / Vehicles
// are the stat editors, stat_editor.cpp).
#pragma once
struct App;
void mainMenuOpen(App& app);    // launch, File > Main menu, the pause menu's Main menu
bool mainMenuActive();
void mainMenuFrame(App& app);   // every frame after ImGui::NewFrame while active: draws it and takes its input
// glass panel over the menu backdrop (blurred backdrop + smoke tint + bright inner edge + soft shadow); also used by the loading screen
struct ImDrawList; struct ImVec2;
void menuGlass(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha);
void mainMenuGo(App& app, int item);   // captures: select left item 0..7 and open it (-1: back to the plain menu)
