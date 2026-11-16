// CMD1: the in-game command line (see commands.h). One table, kCommands; each handler gets the arguments after the
// command name and returns its result text. Targets: "me" / "host" (you: commands run on the host), "@a" (every soldier
// in the match) or a player name from the match roster; an omitted target is you. Outside a match only you exist.
#include "commands.h"
#include "app.h"
#include "game.h"
#include "enemy_types.h"
#include "lighting.h"
#include "loot.h"
#include "physics.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

void settingsUiWeather(App& app, int w);   // settings.cpp: the Weather combo (live; a LAN host's weather reaches clients)
AdminMode gAdmin;   // CMD2 (commands.h)

namespace {
bool gToCreative = false;
std::string lower(std::string s);
std::string onOff(std::vector<std::string>& a, bool cur, bool& ok) { ok = true; if (a.empty()) return cur ? "off" : "on"; std::string v = lower(a[0]); ok = v == "on" || v == "off"; return v; }
using Args = std::vector<std::string>;
struct Cmd { const char* name; const char* usage; const char* help; std::function<std::string(App&, Args&)> run; };

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
bool num(const std::string& s, float& v) { char* e = nullptr; v = std::strtof(s.c_str(), &e); return !s.empty() && e && *e == 0 && std::isfinite(v); }
bool matchOn() { return gGame && gGame->active; }

// ---- targets: -1 = you outside a match; otherwise roster slots
bool isTarget(const std::string& w) {
  std::string l = lower(w);
  if (l == "me" || l == "host" || l == "@a") return true;
  if (matchOn()) for (auto& rs : gGame->match.slots) if (lower(rs.name) == l) return true;
  return false;
}
std::vector<int> targets(const std::string& w, std::string& err) {
  std::string l = lower(w);
  int me = matchOn() ? gGame->localSlot : -1;
  if (l.empty() || l == "me" || l == "host") return {me};
  if (l == "@a") { if (!matchOn()) return {me}; std::vector<int> v; for (auto& s : gGame->sold) if (s.inUse) v.push_back(s.slot); return v; }
  if (matchOn()) for (size_t k = 0; k < gGame->match.slots.size(); ++k) if (lower(gGame->match.slots[k].name) == l) return {(int)k};
  err = "No player named \"" + w + "\""; return {};
}
// an optional leading target: taken when it names one and more arguments follow (so "/give m1" is a weapon, not a player)
std::string takeTarget(Args& a, size_t need) { if (a.size() > need && isTarget(a[0])) { std::string t = a[0]; a.erase(a.begin()); return t; } return ""; }
const char* whoName(int slot) { return slot < 0 || !matchOn() ? "you" : gGame->match.slots[slot].name.empty() ? "a bot" : gGame->match.slots[slot].name.c_str(); }
Soldier* soldier(int slot) { return slot >= 0 && matchOn() && slot < (int)gGame->sold.size() ? &gGame->sold[slot] : nullptr; }

// ---- weapons: id or display name, case-insensitive; exact, then a unique prefix; else close matches listed
int findWeapon(App& app, const std::string& q, std::string& err) {
  std::string l = lower(q); std::vector<int> pre, sub;
  for (int i = 0; i < (int)app.weaponDefs.size(); ++i) {
    std::string id = lower(app.weaponDefs[i]->id), nm = lower(app.weaponDefs[i]->name);
    if (id == l || nm == l) return i;
    if (id.rfind(l, 0) == 0 || nm.rfind(l, 0) == 0) pre.push_back(i);
    else if (id.find(l) != std::string::npos || nm.find(l) != std::string::npos) sub.push_back(i);
  }
  if (pre.size() == 1) return pre[0];
  std::vector<int>& c = pre.empty() ? sub : pre;
  if (c.size() == 1) return c[0];
  err = c.empty() ? "No weapon matches \"" + q + "\"" : "\"" + q + "\" matches " + std::to_string(c.size()) + ":";
  for (size_t k = 0; k < c.size() && k < 6; ++k) err += (k ? ", " : " ") + app.weaponDefs[c[k]]->id;
  if (c.size() > 6) err += ", ...";
  return -1;
}
void fillAmmo(const WeaponDef& d, WeaponState& s, int reserve) {   // full magazine and reserve (the spawn loadout's rule, game.cpp)
  s.loaded = d.loaded; s.containers.clear();
  int r = reserve >= 0 ? reserve : d.reserve; s.reserveMax = d.loaded + r;
  for (int left = r; left > 0; left -= std::max(1, d.containerSize)) s.containers.push_back(std::min(left, std::max(1, d.containerSize)));
}

// ---- time of day: day / noon / night / dawn / dusk / HH:MM / hours
bool parseHour(const std::string& w, float& h) {
  std::string l = lower(w);
  if (l == "day") { h = todPresetHour(TodPreset::Day); return true; }
  if (l == "noon" || l == "midday") { h = 12; return true; }
  if (l == "night" || l == "midnight") { h = todPresetHour(TodPreset::Night); return true; }
  if (l == "dawn" || l == "sunrise") { h = todPresetHour(TodPreset::Dawn); return true; }
  if (l == "dusk" || l == "sunset") { h = todPresetHour(TodPreset::Dusk); return true; }
  int hh = 0, mm = 0; char c = 0;
  if (std::sscanf(l.c_str(), "%d:%d%c", &hh, &mm, &c) == 2 && hh >= 0 && hh <= 24 && mm >= 0 && mm < 60) { h = hh + mm / 60.0f; return true; }
  return num(l, h) && h >= 0 && h <= 24;
}

std::string cmdHelp(App&, Args&);
const std::vector<Cmd>& table() {
  static const std::vector<Cmd> t = {
    {"help", "/help", "this list", cmdHelp},
    {"creative", "/creative [me] [on|off]", "to Creative (the editor: fly and build), as F5; F5 comes back",
     [](App& app, Args& a) -> std::string {
       std::string tg = takeTarget(a, 0); std::string err;
       if (!tg.empty() && lower(tg) != "me" && lower(tg) != "host") return "Creative is for you only (a player cannot be put into the editor)";
       std::string v = a.empty() ? "on" : lower(a[0]);
       if (v != "on" && v != "off") return "usage: /creative [me] [on|off]";
       if (v == "off") return "Already playing (in Creative, F5 returns to play)";
       if (matchOn() && !gGame->isClient && gGame->match.slots.size() > 1) app.toast("The match ends in Creative", 4);
       gToCreative = true; return "Creative";
     }},
    {"give", "/give [target] <weapon> [ammo]", "a weapon with full ammunition (id or name, unique prefix); in Battle Royale also a loot item id, dropped at the feet",
     [](App& app, Args& a) -> std::string {
       std::string tg = takeTarget(a, 1), err;
       if (a.empty()) return "usage: /give [target] <weapon> [ammo]";
       float n = -1; if (a.size() > 1 && !num(a[1], n)) return "ammo must be a number";
       std::vector<int> who = targets(tg, err); if (who.empty()) return err;
       if (lootActive()) if (const ItemDef* it = lootItem(lower(a[0]))) {   // BR loot: on the floor at their feet, picked up by the usual rules
         bool armour = it->kind == ItemKind::Helmet || it->kind == ItemKind::Vest || it->kind == ItemKind::Armor1;
         for (int s : who) { Soldier* so = soldier(s); if (so) lootDropItem(so->feet + vec3(0, 0.1f, 0), it->id, n > 0 ? (int)n : armour ? 250 : 1); }
         return it->name + " dropped at the feet of " + (who.size() > 1 ? "everyone" : whoName(who[0]));
       }
       int w = findWeapon(app, a[0], err); if (w < 0) return err;
       const WeaponDef& d = *app.weaponDefs[w]; std::string out;
       for (int s : who) {
         Soldier* so = soldier(s);
         if (!so || so->localPlayer) {   // you: into your hands (two main slots; a third replaces the one in hand)
           std::string msg; if (!app.equipWeapon(w, msg)) return msg;
           fillAmmo(d, app.wstate[d.id], n >= 0 ? (int)n : -1); out = "Gave " + d.name + " (" + std::to_string(app.wstate[d.id].loaded) + " + " + std::to_string(app.wstate[d.id].reserveRounds()) + ")";
         } else if (so->human) { if (who.size() == 1) return std::string("Cannot give to a LAN player (") + whoName(s) + "): not replicated"; }
         else if (!so->zombie) { so->weapon = w; so->ws = WeaponState{}; fillAmmo(d, so->ws, n >= 0 ? (int)n : -1); if (out.empty()) out = "Gave " + d.name + " to " + whoName(s); }
       }
       if (who.size() > 1) out = "Gave " + d.name + " to you and every bot soldier (LAN players and enemies skipped)";
       return out.empty() ? "Nobody to give it to (enemies carry no guns)" : out;
     }},
    {"time", "/time set <day|noon|night|dawn|dusk|HH:MM|0-24>", "the time of day, live (on this machine only: LAN clients keep theirs)",
     [](App& app, Args& a) -> std::string {
       char b[64];
       if (a.empty() || lower(a[0]) == "query") { std::snprintf(b, sizeof b, "Time %02d:%02d", (int)app.R.lt.hour, (int)(std::fmod(app.R.lt.hour, 1.0f) * 60)); return b; }
       if (lower(a[0]) == "set") a.erase(a.begin());
       float h; if (a.empty() || !parseHour(a[0], h)) return "usage: /time set <day|noon|night|dawn|dusk|HH:MM|0-24>";
       if (app.doc.extra.is_object() && app.doc.extra.contains("lighting") && app.doc.extra["lighting"].is_object() && app.doc.extra["lighting"].contains("hour"))
         return "This map fixes its hour (extra.lighting.hour)";
       app.R.lt.hour = std::fmod(h, 24.0f);
       std::snprintf(b, sizeof b, "Time set to %02d:%02d", (int)app.R.lt.hour, (int)(std::fmod(app.R.lt.hour, 1.0f) * 60)); return b;
     }},
    {"weather", "/weather <clear|cloudy>", "the weather (8 s transition; a LAN host's reaches the clients)",
     [](App& app, Args& a) -> std::string {
       std::string v = a.empty() ? "" : lower(a[0]);
       if (v != "clear" && v != "cloudy") return "usage: /weather <clear|cloudy>";
       settingsUiWeather(app, v == "cloudy" ? 1 : 0); return v == "cloudy" ? "Weather: cloudy" : "Weather: clear";
     }},
    {"tp", "/tp <x> <z>  |  /tp <player>", "teleport you to a ground point or to a player",
     [](App& app, Args& a) -> std::string {
       if (!Physics::hasCharacter()) return "Not on foot";
       float x, z; vec3 to;
       if (a.size() >= 2 && num(a[0], x) && num(a[1], z)) to = vec3(x, app.terrain.height(x, z) + 0.05f, z);
       else if (a.size() == 1) { std::string err; std::vector<int> w = targets(a[0], err); Soldier* so = w.empty() ? nullptr : soldier(w[0]); if (!so) return err.empty() ? "No such player" : err; to = so->feet + vec3(1.0f, 0.05f, 0); }
       else return "usage: /tp <x> <z>  |  /tp <player>";
       Physics::setCharacterPos(to); char b[64]; std::snprintf(b, sizeof b, "Teleported to %.0f %.0f", to.x, to.z); return b;
     }},
    {"admin", "/admin [on|off|peace]", "stay in play with: B builds (the asset palette, free), double-tap Space flies, nothing hurts you, no AI attacks you; peace: no AI fights at all; ends with the match",
     [](App&, Args& a) -> std::string {
       std::string v = a.empty() ? (gAdmin.on ? "off" : "on") : lower(a[0]);
       if (v == "off") { bool flew = gAdmin.fly; gAdmin = AdminMode{}; gAdmin.fallSafe = flew; return "Admin off"; }
       if (v != "on" && v != "peace") return "usage: /admin [on|off|peace]";
       gAdmin.on = gAdmin.god = gAdmin.ignored = gAdmin.build = gAdmin.flyAllowed = true; if (v == "peace") gAdmin.peace = true;
       return std::string("Admin on: B builds (asset palette), double-tap Space flies, nothing hurts you, no AI attacks you") + (gAdmin.peace ? "; peace: no AI fights" : "");
     }},
    {"fly", "/fly [on|off]", "flying: Space up, Ctrl down, Shift fast; double-tap Space toggles it while allowed",
     [](App&, Args& a) -> std::string { bool ok; std::string v = onOff(a, gAdmin.fly, ok); if (!ok) return "usage: /fly [on|off]";
       gAdmin.flyAllowed = true; gAdmin.fly = v == "on"; if (!gAdmin.fly) gAdmin.fallSafe = true; return gAdmin.fly ? "Flying" : "Flying off"; }},
    {"peace", "/peace [on|off]", "no AI attacks anyone",
     [](App&, Args& a) -> std::string { bool ok; std::string v = onOff(a, gAdmin.peace, ok); if (!ok) return "usage: /peace [on|off]"; gAdmin.peace = v == "on"; return gAdmin.peace ? "Peace: no AI fights" : "Peace off"; }},
    {"build", "/build [on|off]", "B opens the free build palette (every asset) in a match",
     [](App&, Args& a) -> std::string { bool ok; std::string v = onOff(a, gAdmin.build, ok); if (!ok) return "usage: /build [on|off]"; gAdmin.build = v == "on"; return gAdmin.build ? "Build: B opens the asset palette" : "Build off"; }},
    {"kill", "/kill [target]", "kills (a match only)",
     [](App&, Args& a) -> std::string {
       if (!matchOn()) return "No match running";
       std::string err; std::vector<int> w = targets(a.empty() ? "" : a[0], err); if (w.empty()) return err;
       gAdmin.allowKill = true;   // CMD2: /kill reaches you through /admin too
       int n = 0; for (int s : w) if (Soldier* so = soldier(s)) if (so->alive) { so->protectUntil = -1; gGame->killSoldier(s, -1, vec3(0, 0, 1)); ++n; }
       gAdmin.allowKill = false;
       return "Killed " + std::to_string(n);
     }},
    {"heal", "/heal [target]", "full health (a match only)",
     [](App&, Args& a) -> std::string {
       if (!matchOn()) return "No match running";
       std::string err; std::vector<int> w = targets(a.empty() ? "" : a[0], err); if (w.empty()) return err;
       int n = 0; for (int s : w) if (Soldier* so = soldier(s)) if (so->alive) { so->hp = so->zombie ? enemyType(so->etype).hp : 100.0f; ++n; }
       return "Healed " + std::to_string(n);
     }},
    {"god", "/god [target] [on|off]", "takes no damage (the spawn protection, kept on: it shimmers)",
     [](App&, Args& a) -> std::string {
       if (!matchOn()) return "No match running (outside a match nothing hurts you)";
       std::string tg = takeTarget(a, 0), err; std::vector<int> w = targets(tg, err); if (w.empty()) return err;
       Soldier* first = soldier(w[0]); if (!first) return "No such player";
       bool on = a.empty() ? first->protectUntil < 1e11 : lower(a[0]) == "on";
       if (!a.empty() && lower(a[0]) != "on" && lower(a[0]) != "off") return "usage: /god [target] [on|off]";
       for (int s : w) if (Soldier* so = soldier(s)) so->protectUntil = on ? 1e12 : -1;
       return std::string("God mode ") + (on ? "on" : "off");
     }},
  };
  return t;
}
std::string cmdHelp(App&, Args&) { std::string s; for (auto& c : table()) s += std::string(c.usage) + "  " + c.help + "\n"; return s; }

// ---- the line
struct State { bool open = false, focus = false, toEnd = false; int closedFrames = 0; char buf[256] = {}; std::vector<std::string> hist; int histPos = -1; std::string result; double resultUntil = 0; } st;

void complete(App& app, ImGuiInputTextCallbackData* d) {
  std::string line(d->Buf, d->BufTextLen); size_t ws = line.find_last_of(' ');
  bool first = ws == std::string::npos; std::string word = first ? line.substr(line[0] == '/' ? 1 : 0) : line.substr(ws + 1), l = lower(word);
  std::vector<std::string> cand;
  if (first) { for (auto& c : table()) if (std::string(c.name).rfind(l, 0) == 0) cand.push_back(c.name); }
  else {
    std::string cmd = lower(line.substr(line[0] == '/' ? 1 : 0, line.find(' ') - (line[0] == '/' ? 1 : 0)));
    std::vector<std::string> pool = {"me", "host", "@a"};
    if (matchOn()) for (auto& rs : gGame->match.slots) if (!rs.name.empty()) pool.push_back(rs.name);
    if (cmd == "give") for (auto& w : app.weaponDefs) pool.push_back(w->id);
    if (cmd == "time") for (const char* k : {"set", "day", "noon", "night", "dawn", "dusk"}) pool.push_back(k);
    if (cmd == "weather") pool = {"clear", "cloudy"};
    if (cmd == "creative" || cmd == "god") { pool.push_back("on"); pool.push_back("off"); }
    for (auto& p : pool) if (lower(p).rfind(l, 0) == 0) cand.push_back(p);
  }
  if (cand.empty()) return;
  std::string common = cand[0];
  for (auto& c : cand) { size_t k = 0; while (k < common.size() && k < c.size() && std::tolower((unsigned char)common[k]) == std::tolower((unsigned char)c[k])) ++k; common.resize(k); }
  if (cand.size() == 1) common += " ";
  int start = first ? (line[0] == '/' ? 1 : 0) : (int)ws + 1;
  d->DeleteChars(start, d->BufTextLen - start); d->InsertChars(start, common.c_str());
  if (cand.size() > 1) { st.result.clear(); for (size_t k = 0; k < cand.size() && k < 12; ++k) st.result += (k ? "  " : "") + cand[k]; if (cand.size() > 12) st.result += "  ..."; st.resultUntil = ImGui::GetTime() + 4; }
}
int callback(ImGuiInputTextCallbackData* d) {
  App& app = *(App*)d->UserData;
  if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways) { if (st.toEnd) { st.toEnd = false; d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen; } return 0; }   // focusing selects all: the "/" would be typed over
  if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) complete(app, d);
  else if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory && !st.hist.empty()) {
    int n = (int)st.hist.size();
    if (d->EventKey == ImGuiKey_UpArrow) st.histPos = st.histPos < 0 ? n - 1 : std::max(0, st.histPos - 1);
    else if (d->EventKey == ImGuiKey_DownArrow) st.histPos = st.histPos < 0 || st.histPos >= n - 1 ? -1 : st.histPos + 1;
    d->DeleteChars(0, d->BufTextLen); d->InsertChars(0, st.histPos < 0 ? "/" : st.hist[st.histPos].c_str());
  }
  return 0;
}
}   // namespace

bool commandsOpen() { return st.open; }
bool commandsTakeCreative() { bool v = gToCreative; gToCreative = false; return v; }
bool commandsOwnKeys() { return st.open || st.closedFrames > 0; }

std::string commandRun(App& app, const std::string& line) {
  std::string s = line; size_t b = s.find_first_not_of("/ "); s = b == std::string::npos ? "" : s.substr(b);
  Args a; for (size_t i = 0; i < s.size();) { size_t j = s.find(' ', i); if (j == std::string::npos) j = s.size(); if (j > i) a.push_back(s.substr(i, j - i)); i = j + 1; }
  if (a.empty()) return "";
  std::string name = lower(a[0]); a.erase(a.begin());
  for (auto& c : table()) if (name == c.name) {
    if (name != "help" && gGame && gGame->active && gGame->isClient) return "Commands are host only";
    return c.run(app, a);
  }
  return "Unknown command /" + name + " (/help lists them)";
}

void commandsFrame(App& app) {
  ImGuiIO& io = ImGui::GetIO();
  if (st.closedFrames > 0) --st.closedFrames;
  if (!st.open && !app.pauseMenu && !io.WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Slash, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false))) {
    st.open = true; st.focus = true; st.histPos = -1; std::snprintf(st.buf, sizeof st.buf, "/");
  }
  const float W = std::min(720.0f, io.DisplaySize.x - 32), y = io.DisplaySize.y - 200;   // above the match status lines at the bottom left
  if (st.open) {
    ImGui::SetNextWindowPos(ImVec2(16, y)); ImGui::SetNextWindowSize(ImVec2(W, 0)); ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##commandline", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
    if (st.focus) { ImGui::SetKeyboardFocusHere(); st.focus = false; st.toEnd = true; }
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##line", st.buf, sizeof st.buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways, callback, &app);
    bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::End();
    if (enter || esc) {
      std::string line = st.buf;
      st.open = false; st.closedFrames = 2;
      if (enter && line.find_first_not_of("/ ") != std::string::npos) {
        if (st.hist.empty() || st.hist.back() != line) st.hist.push_back(line);
        st.result = commandRun(app, line); st.resultUntil = ImGui::GetTime() + (st.result.find('\n') != std::string::npos ? 10 : 5);
      }
      app.syncCursor();
    }
  }
  if (gAdmin.on || gAdmin.fly || gAdmin.peace || gAdmin.build) {   // CMD2: the HUD tag
    std::string tag = gAdmin.on ? "ADMIN" : ""; auto add = [&](bool b, const char* s) { if (b) tag += (tag.empty() ? "" : "  ") + std::string(s); };
    add(gAdmin.fly, "FLY"); add(gAdmin.peace, "PEACE"); add(!gAdmin.on && gAdmin.build, "BUILD");
    ImVec2 ts = ImGui::CalcTextSize(tag.c_str()), p(io.DisplaySize.x * 0.5f - ts.x * 0.5f, 96);
    ImDrawList* dl = ImGui::GetForegroundDrawList(); dl->AddRectFilled(ImVec2(p.x - 8, p.y - 3), ImVec2(p.x + ts.x + 8, p.y + ts.y + 3), IM_COL32(0, 0, 0, 140), 4);
    dl->AddText(p, IM_COL32(255, 170, 40, 255), tag.c_str());
  }
  if (!st.result.empty() && ImGui::GetTime() < st.resultUntil) {
    ImGui::SetNextWindowPos(ImVec2(16, y - 8), ImGuiCond_Always, ImVec2(0, 1)); ImGui::SetNextWindowSize(ImVec2(0, 0)); ImGui::SetNextWindowBgAlpha(0.6f);   // refit each frame (a long /help leaves no tall box behind)
    ImGui::Begin("##commandresult", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(st.result.c_str());
    ImGui::End();
  }
}

// ---------------------------------------------------------------- --capture-commands: a look, with result checks
// TDM on village_assault_3 (you and a bot v a bot). "/" is pressed and "/give modern_a" typed through ImGui's input
// queue, Tab completes it, Enter runs it; then every command runs through commandRun. UI screenshots (line, help, give,
// night) and a report: selftest_out/cmd_*.png, report_commands.txt.
#include "selftest.h"
#include "mpui.h"
#include "mptest.h"
#include "scenario.h"
#include <cstdlib>
namespace {
struct Cap { MpTest* T = nullptr; int step = 0; float t = 0; bool on = false, checked = false; float y0 = 0, hp0 = 0; } cap;
fs::path capDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void expect(App& app, const std::string& line, const std::string& want) {
  std::string r = commandRun(app, line);
  cap.T->check(r.find(want) != std::string::npos, line + " -> " + r.substr(0, r.find('\n')));
}
}   // namespace

bool commandsSelfTest(App& app, float dt) {
  if (!cap.checked) { cap.checked = true; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-commands") cap.on = true; }
  if (!cap.on) return false;
  if (dt < 0) return true;
  if (!cap.T) cap.T = new MpTest("commands");
  MpTest& T = *cap.T; ImGuiIO& io = ImGui::GetIO();
  cap.t += dt;
  auto next = [&] { ++cap.step; cap.t = 0; };
  switch (cap.step) {
    case 0: {
      app.openMap(contentDir() / "examples/village_assault_3.nmap.json");
      json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
      Scenario sc = Scenario::fromJson(scj);
      sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.teamPlayers[0] = 2; sc.rules.teamPlayers[1] = 1; sc.rules.respawnDelay = 2;
      sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(app);
      gGame->pendingStart = true; app.doc.start = {true, vec3(0, 0, 30), 180};
      T.check(app.enterExplore() && gGame->active, "TDM started");
      app.useScripted = true; app.scriptedInput = ExploreInput{}; next(); break;
    }
    case 1: if (cap.t > 2.0f) { io.AddKeyEvent(ImGuiKey_Slash, true); next(); } break;   // "/" opens the line
    case 2: io.AddKeyEvent(ImGuiKey_Slash, false); next(); break;
    case 3: if (cap.t > 0.2f) { T.check(commandsOpen() && app.wantCursor() && std::string(st.buf) == "/", std::string("\"/\" opens the line with \"") + st.buf + "\" and frees the cursor"); io.AddInputCharactersUTF8("give modern_a"); next(); } break;
    case 4: if (cap.t > 0.2f) { io.AddKeyEvent(ImGuiKey_Tab, true); next(); } break;   // Tab completes the weapon id
    case 5: io.AddKeyEvent(ImGuiKey_Tab, false); if (cap.t > 0.3f) {   // three ids share "modern_a": Tab lists them; one more letter and Tab completes
        T.check(std::string(st.buf) == "/give modern_a" && st.result.find("modern_ar15") != std::string::npos, std::string("Tab lists the matches: \"") + st.result + "\"");
        app.uiShot = capDir() / "cmd_line.png"; io.AddInputCharactersUTF8("r"); next(); } break;

    case 6: if (cap.t > 0.2f && cap.t < 0.25f) io.AddKeyEvent(ImGuiKey_Tab, true); else if (cap.t >= 0.25f && cap.t < 0.5f) io.AddKeyEvent(ImGuiKey_Tab, false);
      else if (cap.t > 0.6f) { T.check(std::string(st.buf) == "/give modern_ar15 ", std::string("Tab completes a unique prefix: \"") + st.buf + "\""); io.AddKeyEvent(ImGuiKey_Enter, true); next(); } break;
    case 7: io.AddKeyEvent(ImGuiKey_Enter, false); if (cap.t > 0.3f) {
        T.check(!commandsOpen() && !app.wantCursor() && st.result.rfind("Gave", 0) == 0, "Enter runs and closes (" + st.result + ")");
        app.uiShot = capDir() / "cmd_give.png"; cap.step = 40; cap.t = 0; } break;
    // SND1: enemy rounds past the head (injected): one 1 m beside it whizzes, one 8 m out does not, a 30-round burst 1 m out
    // gives at most four in its 0.3 s
    case 40: case 41: case 42: if (cap.t > 0.4f) { gAdmin.peace = true;   // no real fire near the head while the injected rounds are counted
        static int before = 0; int enemy = -1; Soldier* L = gGame->local();
        for (auto& s : gGame->sold) if (L && s.inUse && s.team != L->team) enemy = s.slot;
        if (cap.step > 40) {
          int got = app.audio.eventPlays["bulletwhiz"] - before;
          if (cap.step == 41) T.check(got == 1, "a round 1 m from the head whizzes once (" + std::to_string(got) + ")");
          if (cap.step == 42) T.check(got == 0, "a round 8 m away is silent (" + std::to_string(got) + ")");
        }
        before = app.audio.eventPlays["bulletwhiz"];
        float off = cap.step == 40 ? 1.0f : 8.0f; vec3 eye = app.eyePos(), f = App::forward(app.eyeYaw, 0), side(-f.z, 0, f.x);
        Projectile q; q.kind = PROJ_BULLET; q.owner = enemy; q.life = 0.5f; q.dmg = 0; q.p = eye + f * 30.0f + side * off; q.v = -f * 600.0f;
        app.projectiles.push_back(q); next(); } break;
    case 43: if (cap.t > 0.4f) {
        static int before = 0; Soldier* L = gGame->local(); int enemy = -1;
        for (auto& s : gGame->sold) if (L && s.inUse && s.team != L->team) enemy = s.slot;
        if (cap.t < 0.45f) {
          before = app.audio.eventPlays["bulletwhiz"]; vec3 eye = app.eyePos(), f = App::forward(app.eyeYaw, 0), side(-f.z, 0, f.x);
          for (int k = 0; k < 30; ++k) { Projectile q; q.kind = PROJ_BULLET; q.owner = enemy; q.life = 0.5f; q.dmg = 0; q.p = eye + f * (30.0f + 0.25f * k) + side * 1.0f; q.v = -f * 600.0f; app.projectiles.push_back(q); }
        } else if (cap.t > 1.2f) { int got = app.audio.eventPlays["bulletwhiz"] - before; T.check(got >= 1 && got <= 4, "a 30-round burst 1 m out: " + std::to_string(got) + " whizzes (cap 4)"); gAdmin.peace = false; cap.step = 50; cap.t = 0; }
      } break;
    // CMD2: /admin, /fly (rise on Space, land without fall damage), /build (B in a match: the sandbox builder)
    case 50: if (cap.t > 0.3f) {
        expect(app, "/admin on", "Admin on");
        Soldier* L = gGame->local(); int en = -1, mate = -1; for (auto& s : gGame->sold) { if (s.team != L->team) en = s.slot; else if (!s.localPlayer) mate = s.slot; }
        float hp0 = L->hp; gGame->damage(L->slot, 60, en, vec3(0, 0, 1), "blast"); gGame->killSoldier(L->slot, en, vec3(0, 0, 1));
        T.check(L->alive && L->hp == hp0 && L->protectUntil < 1e11, "/admin: no damage, no death, no protection shimmer");
        T.check(en >= 0 && !Game::hostile(gGame->sold[en], *L) && Game::hostile(*L, gGame->sold[en]), "/admin: the enemy does not take you for an enemy; you can still fight it");
        T.check(mate >= 0 && Game::hostile(gGame->sold[en], gGame->sold[mate]), "/admin: the AI keeps fighting each other");
        expect(app, "/peace on", "Peace"); T.check(!Game::hostile(gGame->sold[en], gGame->sold[mate]), "/peace: no AI takes anyone for an enemy"); expect(app, "/peace off", "off");
        expect(app, "/fly on", "Flying"); cap.y0 = Physics::character().pos.y; app.scriptedInput = ExploreInput{}; app.scriptedInput.jumpHeld = true; next(); } break;
    case 51: if (cap.t > 1.0f) {
        float y1 = Physics::character().pos.y; T.check(y1 - cap.y0 > 4.0f, "flying: Space rises (" + std::to_string(y1 - cap.y0) + " m in 1 s)");
        app.uiShot = capDir() / "cmd_admin.png"; app.scriptedInput.jumpHeld = false; cap.step = 60; cap.t = 0; } break;   // the ADMIN  FLY tag, in the air
    case 60: if (cap.t > 0.3f) { expect(app, "/admin off", "off"); cap.hp0 = gGame->local()->hp; cap.step = 52; cap.t = 0; } break;
    case 52: if (cap.t > 3.0f) {
        T.check(Physics::character().grounded && !gAdmin.fly && gGame->local()->hp == cap.hp0, "after flying: landed, no fall damage (" + std::to_string(cap.hp0) + " -> " + std::to_string(gGame->local()->hp) + ")");
        expect(app, "/build on", "Build"); app.scriptedInput.build = true; next(); } break;
    case 53: if (cap.t > 0.4f) {
        T.check(app.build.active && !app.build.engineer, "/build: B in a match opens the sandbox builder (not the Engineer menu)");
        app.scriptedInput.build = true; expect(app, "/build off", "off"); cap.step = 8; cap.t = 0; } break;
    case 8: if (cap.t > 0.5f) {
        expect(app, "/give thompson", "Gave"); expect(app, "/give zzz_nothing", "No weapon"); expect(app, "/give m", "matches");
        expect(app, "/time set night", "Time set to 00:00"); expect(app, "/time set 18:30", "18:30"); expect(app, "/time set noon", "12:00"); expect(app, "/time set 25", "usage");
        expect(app, "/weather cloudy", "cloudy"); expect(app, "/tp 10 20", "Teleported"); expect(app, "/heal", "Healed 1"); expect(app, "/god on", "on");
        T.check(gGame->local() && gGame->local()->protectUntil > 1e11, "/god on keeps you protected");
        expect(app, "/god off", "off"); expect(app, "/kill @a", "Killed"); expect(app, "/nonsense", "Unknown"); expect(app, "/creative bob", "usage");
        st.result = commandRun(app, "/help"); st.resultUntil = ImGui::GetTime() + 10; next(); } break;
    case 9: if (cap.t > 0.5f) { app.uiShot = capDir() / "cmd_help.png"; next(); } break;   // the box has fitted its text by now
    case 10: if (cap.t > 0.3f) { commandRun(app, "/time set night"); next(); } break;
    case 11: if (cap.t > 1.0f) { st.result = "Time set to 00:00"; st.resultUntil = ImGui::GetTime() + 5; next(); } break;
    case 12: if (cap.t > 0.3f) { app.uiShot = capDir() / "cmd_night.png"; next(); } break;
    case 13: if (cap.t > 0.5f) { expect(app, "/creative", "Creative"); next(); } break;
    case 14: if (cap.t > 0.5f) { T.check(app.mode == Mode::Creative, "/creative goes to Creative (as F5)"); if (app.selftest && !app.selftest->done) { app.selftest->exitCode = T.finish(); app.selftest->done = true; } } break;
  }
  return true;
}
