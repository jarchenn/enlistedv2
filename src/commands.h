#pragma once
// CMD1: the in-game command line. In play (Explore) "/" opens a one-line input at the bottom: Enter runs, Esc cancels,
// Up / Down recall history, Tab completes command names, targets and weapon ids. Commands live in one table
// (src/commands.cpp kCommands: name, usage, help, handler). On a LAN client every command but /help answers "host only".
#include <string>
class App;

// CMD2 /admin (and /fly, /peace, /build): the host's own powers in play, the match running. Host / single player only.
struct AdminMode {
  bool on = false;         // /admin on: god, ignored, build, fly allowed
  bool god = false;        // no damage of any kind, no shimmer (Game::damage / killSoldier)
  bool ignored = false;    // no AI treats you as an enemy (Game::hostile); they keep fighting each other
  bool peace = false;      // /peace: no AI treats anyone as an enemy
  bool build = false;      // B opens the sandbox build palette in a match (builder.cpp), not the Engineer menu
  bool flyAllowed = false, fly = false;   // fly: Space up, Ctrl down, Shift fast; a double-tap of Space toggles it
  bool fallSafe = false;   // no fall damage until the next landing after flying
  bool allowKill = false;  // /kill on yourself still works
};
extern AdminMode gAdmin;

bool commandsOpen();                              // the line is open: game keys, mouse look and the pause key are off
bool commandsOwnKeys();                           // open, or closed within the last two frames (its Enter / Esc are not game input)
void commandsFrame(App& app);                     // each Explore frame inside the ImGui frame: "/" opens; draws the line and the result
std::string commandRun(App& app, const std::string& line);   // runs one line (leading "/" optional); returns the result or error text
bool commandsTakeCreative();                      // /creative asked for the editor: App::updateExplore switches (as F5), outside the ImGui frame
