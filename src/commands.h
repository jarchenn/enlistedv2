#pragma once
// CMD1: the in-game command line. In play (Explore) "/" opens a one-line input at the bottom: Enter runs, Esc cancels,
// Up / Down recall history, Tab completes command names, targets and weapon ids. Commands live in one table
// (src/commands.cpp kCommands: name, usage, help, handler). On a LAN client every command but /help answers "host only".
#include <string>
class App;

bool commandsOpen();                              // the line is open: game keys, mouse look and the pause key are off
bool commandsOwnKeys();                           // open, or closed within the last two frames (its Enter / Esc are not game input)
void commandsFrame(App& app);                     // each Explore frame inside the ImGui frame: "/" opens; draws the line and the result
std::string commandRun(App& app, const std::string& line);   // runs one line (leading "/" optional); returns the result or error text
bool commandsTakeCreative();                      // /creative asked for the editor: App::updateExplore switches (as F5), outside the ImGui frame
