// G1: Solo / Team Deathmatch HUD - the persistent mode banner, the scoreboard (held Tab and the end screen).
#include "game.h"
#include "ui_glass.h"   // UI2: glass panel backgrounds
#include "settings.h"
#include "app.h"
#include "ui_flat.h"   // UI5: scoreboard type and colours
#include "mpui.h"


#include "royale.h"
#include "loot.h"   // BR2 Battle Royale loot
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>

static ImU32 teamCol(Faction f) { return f == Faction::Allies ? IM_COL32(150, 190, 255, 255) : IM_COL32(255, 160, 130, 255); }

// top-centre banner. Solo: "First to N  |  Leader: name K  |  You: k". TDM: "First to N" + both teams' kills.
void Game::drawModeBanner() {
  if (match.mode() == GameMode::Assault) return;
  if (match.mode() == GameMode::BattleRoyale) { royaleHud(app, *this); return; }   // BR1: alive, storm timers, placement
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Soldier* L = local();
  char b[220];
  float cx = io.DisplaySize.x * 0.5f, y = 10;
  if (match.mode() == GameMode::Solo) {
    int ls = -1, top = match.topKills(&ls);
    int mine = L ? match.slots[L->slot].kills : 0;
    std::snprintf(b, sizeof b, "SOLO  -  first to %d kills   |   Leader: %s %d / %d   |   You: %d", match.rules.killCap,
                  ls >= 0 ? match.slots[ls].name.c_str() : "-", top, match.rules.killCap, mine);
    ImVec2 ts = ImGui::CalcTextSize(b);
    uiGlassPanel(dl, ImVec2(cx - ts.x * 0.5f - 12, y), ImVec2(cx + ts.x * 0.5f + 12, y + 28), std::max(8.0f, (float)5));
    dl->AddText(ImVec2(cx - ts.x * 0.5f, y + 6), IM_COL32(245, 235, 200, 255), b);
    float f = std::clamp(top / (float)std::max(1, match.rules.killCap), 0.0f, 1.0f);   // leader progress bar
    dl->AddRectFilled(ImVec2(cx - 150, y + 31), ImVec2(cx + 150, y + 35), IM_COL32(60, 60, 55, 220), 2);
    dl->AddRectFilled(ImVec2(cx - 150, y + 31), ImVec2(cx - 150 + 300 * f, y + 35), IM_COL32(230, 170, 70, 255), 2);
    return;
  }
  // TDM
  int k0 = match.teamKills(0), k1 = match.teamKills(1);
  Faction f0 = match.rules.teamFaction[0], f1 = match.rules.teamFaction[1];
  std::snprintf(b, sizeof b, "TEAM DEATHMATCH  -  FIRST TO %d", match.rules.killCap);
  ImVec2 ts = ImGui::CalcTextSize(b);
  uiGlassPanel(dl, ImVec2(cx - 190, y), ImVec2(cx + 190, y + 50), std::max(8.0f, (float)5));
  dl->AddText(ImVec2(cx - ts.x * 0.5f, y + 4), IM_COL32(245, 235, 200, 255), b);
  char l[64], r[64];
  std::snprintf(l, sizeof l, "%s (A)  %d", factionName(f0), k0);
  std::snprintf(r, sizeof r, "%d  %s (B)", k1, factionName(f1));
  ImVec2 rs = ImGui::CalcTextSize(r);
  dl->AddText(ImVec2(cx - 180, y + 26), teamCol(f0), l);
  dl->AddText(ImVec2(cx + 180 - rs.x, y + 26), teamCol(f1), r);
  float cap = (float)std::max(1, match.rules.killCap);   // two bars meeting in the middle
  dl->AddRectFilled(ImVec2(cx - 4 - 120 * std::min(1.0f, k0 / cap), y + 44), ImVec2(cx - 4, y + 47), teamCol(f0), 1);
  dl->AddRectFilled(ImVec2(cx + 4, y + 44), ImVec2(cx + 4 + 120 * std::min(1.0f, k1 / cap), y + 47), teamCol(f1), 1);
  if (L) {   // which side you are on
    const char* me = L->team == 0 ? "You are team A" : "You are team B";
    ImVec2 ms = ImGui::CalcTextSize(me);
    dl->AddText(ImVec2(cx - ms.x * 0.5f, y + 54), teamCol(L->faction), me);
  }
}

// the scoreboard: name, kills, deaths, score; sorted by score, then kills; TDM shows each team under its total.
// Drawn inside the current ImGui window (the end-of-match window) or as its own overlay window (held Tab).
// UI1: per team with the team's totals on top; compact (top 8 + your own row) while many are playing, the full list
// (scrollable) at the end of the match. Solo is one list.
void Game::scoreboardTable(bool full) {
  // UI5: larger type, the teams side by side, each under a header in its colour with its totals, striped rows
  const Soldier* L = local();
  const int kTop = 8; const float fs = ImGui::GetFontSize();
  auto rows = [&](int team) {   // team -1: everyone (Solo)
    std::vector<const RosterSlot*> v;
    for (auto& s : match.slots) if (team < 0 || s.team == team) v.push_back(&s);
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->score != b->score ? a->score > b->score : a->kills != b->kills ? a->kills > b->kills : a->deaths < b->deaths; });
    {   // the header: side name, faction, totals; a rule in the team colour
      ImDrawList* dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); const float w = ImGui::GetContentRegionAvail().x, hp = fs * 1.55f;
      const ImU32 tc = v.empty() ? flat::ink() : teamCol(v[0]->faction);
      char nm[96], tot[96]; int k = 0, d = 0, sc = 0; for (auto* s : v) { k += s->kills; d += s->deaths; sc += s->score; }
      if (team < 0) std::snprintf(nm, sizeof nm, "ALL PLAYERS");
      else std::snprintf(nm, sizeof nm, "%s  %s", match.mode() == GameMode::TDM ? (team == 0 ? "TEAM A" : "TEAM B") : (!v.empty() && v[0]->role == CombatRole::Defender ? "DEFENDERS" : "ATTACKERS"), v.empty() ? "" : factionName(v[0]->faction));
      for (char* c = nm; *c; ++c) *c = (char)std::toupper((unsigned char)*c);
      std::snprintf(tot, sizeof tot, "%zu PLAYERS   %d K   %d D   SCORE %d", v.size(), k, d, sc);
      flat::text(dl, uiFontHead(), hp, p, tc, nm, 0.04f);
      const float tw = flat::textW(ImGui::GetFont(), fs * 0.95f, tot, 0.06f); flat::text(dl, ImGui::GetFont(), fs * 0.95f, ImVec2(p.x + w - tw, p.y + hp - fs * 1.05f), flat::grey(), tot, 0.06f);
      dl->AddRectFilled(ImVec2(p.x, p.y + hp + 6), ImVec2(p.x + w, p.y + hp + 8), tc);
      ImGui::Dummy(ImVec2(w, hp + 14));
    }
    bool scroll = full && v.size() > 12;
    if (scroll) ImGui::BeginChild(team < 0 ? "sbS" : team == 0 ? "sbSA" : "sbSB", ImVec2(0, 12.5f * (fs + 12) + fs * 1.6f), false);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(fs * 0.5f, 6));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, IM_COL32(255, 255, 255, 6)); ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginTable(team < 0 ? "sb" : team == 0 ? "sbA" : "sbB", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 1.8f); ImGui::TableSetupColumn("PLAYER", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthFixed, fs * 3.0f); ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, fs * 3.0f); ImGui::TableSetupColumn("SCORE", ImGuiTableColumnFlags_WidthFixed, fs * 4.2f);
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(flat::grey())); ImGui::TableHeadersRow(); ImGui::PopStyleColor();
      int hiddenN = 0;
      auto row = [&](int rank, const RosterSlot* s) {
        bool me = L && s->id == L->slot;
        ImGui::TableNextRow(0, fs + 12);
        if (me) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(206, 172, 118, 60));   // you: the gold-tan active colour
        ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(flat::grey()), "%d", rank);
        ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(me ? flat::gold() : flat::ink()), "%s%s", s->name.c_str(), s->human ? "" : "  (AI)");
        ImGui::TableNextColumn(); ImGui::Text("%d", s->kills);
        ImGui::TableNextColumn(); ImGui::Text("%d", s->deaths);
        ImGui::TableNextColumn(); ImGui::Text("%d", s->score);
      };
      if (full) { ImGuiListClipper clip; clip.Begin((int)v.size()); while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) row(i + 1, v[i]); }   // AI2: 1000 rows, only the visible ones drawn
      else for (int i = 0; i < (int)v.size(); ++i) { if (i >= kTop && !(L && v[i]->id == L->slot)) { ++hiddenN; continue; } row(i + 1, v[i]); }
      ImGui::EndTable();
      if (hiddenN) ImGui::TextDisabled("+ %d more", hiddenN);
    }
    ImGui::PopStyleColor(3); ImGui::PopStyleVar();
    if (scroll) ImGui::EndChild();
  };
  if (ffaMode(match.mode())) rows(-1);
  else if (ImGui::BeginTable("##sbteams", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("gap", ImGuiTableColumnFlags_WidthFixed, fs * 2.5f); ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextColumn(); rows(0); ImGui::TableNextColumn(); ImGui::TableNextColumn(); rows(1);
    ImGui::EndTable();
  }
}

// held Tab during a match (not while building: build mode owns Tab for its palette)
void Game::drawScoreboardOverlay() {
  if (lootActive()) return;   // BR2: Tab is the inventory in Battle Royale
  bool held = showScoreboard || (!app.selftest && app.win && !app.build.active && app.bk("scoreboard") >= 0 && glfwGetKey(app.win, app.bk("scoreboard")) == GLFW_PRESS);
  if (!held || match.phase == MatchPhase::Ended) return;
  ImGuiIO& io = ImGui::GetIO();
  // UI5: a large centred panel (about 62 % of the width), the type a quarter larger
  const float sc = io.DisplaySize.y / 1080.0f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x * 0.62f, 1240 * sc), 0));
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("Scoreboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::SetWindowFontScale(1.25f);
  { ImDrawList* dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); const float fs = ImGui::GetFontSize();
    std::string m = gameModeName(match.mode()); for (auto& c : m) c = (char)std::toupper((unsigned char)c);
    flat::text(dl, flat::titleFont(), fs * 2.2f, p, flat::ink(), m.c_str(), 0.01f);
    char r[96]; std::snprintf(r, sizeof r, "FIRST TO %d KILLS   %d:%02d", match.rules.killCap, (int)match.time / 60, (int)match.time % 60);
    const float w = ImGui::GetContentRegionAvail().x, tw = flat::textW(ImGui::GetFont(), fs, r, 0.08f); flat::text(dl, ImGui::GetFont(), fs, ImVec2(p.x + w - tw, p.y + fs * 1.0f), flat::grey(), r, 0.08f);
    flat::rule(dl, p.x, p.x + w, p.y + fs * 2.5f); ImGui::Dummy(ImVec2(w, fs * 3.1f)); }
  scoreboardTable();
  ImGui::End();
}

// Solo / TDM side panel and respawn countdown (Assault keeps its own forces / lives panel)
// UI1b: the player's vitals, bottom left: health (bar and number) and grenades, one compact panel in every mode
void Game::drawVitals() {
  const Soldier* L = local(); if (!L || !L->alive) return;
  ImGuiIO& io = ImGui::GetIO(); ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float w = 230, x = 20, y = io.DisplaySize.y - 70, fh = ImGui::GetTextLineHeight();
  uiGlassPanel(dl, ImVec2(x - 10, y - 8), ImVec2(x + w + 10, y + fh * 2 + 18), std::max(8.0f, (float)6));
  float hp = std::clamp(L->hp / 100.0f, 0.0f, 1.0f); char b[64];
  dl->AddText(ImVec2(x, y), IM_COL32(190, 192, 185, 230), "HEALTH");
  std::snprintf(b, sizeof b, "%.0f", std::max(0.0f, L->hp)); ImVec2 ts = ImGui::CalcTextSize(b);
  dl->AddText(ImVec2(x + w - ts.x, y), IM_COL32(245, 245, 238, 255), b);
  uint32_t col = hp > 0.5f ? IM_COL32(120, 200, 120, 255) : hp > 0.25f ? IM_COL32(230, 180, 70, 255) : IM_COL32(225, 70, 55, 255);
  dl->AddRectFilled(ImVec2(x, y + fh + 3), ImVec2(x + w, y + fh + 10), IM_COL32(255, 255, 255, 30), 3);
  dl->AddRectFilled(ImVec2(x, y + fh + 3), ImVec2(x + w * hp, y + fh + 10), col, 3);
  std::snprintf(b, sizeof b, "Frag %d  [G]     Smoke %d  [H]", L->frags, L->smokes);
  dl->AddText(ImVec2(x, y + fh + 14), IM_COL32(190, 192, 185, 220), b);
}

void Game::drawModePanel() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Soldier* L = local();
  char b[220];
  if (settings().s.devOverlay) {   // UI1b: the old status lines, developer overlay only
    int alive = 0; for (auto& s : sold) alive += s.alive;
    std::snprintf(b, sizeof b, "%s: %d of %zu alive", gameModeName(match.mode()), alive, sold.size());
    dl->AddText(ImVec2(14, io.DisplaySize.y - 132), IM_COL32(220, 215, 195, 200), b);
  }
  drawVitals();
  if (L) {
    const RosterSlot& rs = match.slots[L->slot];
    if (!L->alive && match.phase == MatchPhase::Running && match.mode() != GameMode::BattleRoyale) {   // BR1: no respawn (royaleHud shows the placement)
      float left = std::max(0.0f, match.rules.respawnDelay - rs.deadFor);
      if (L->lifeId == 0) std::snprintf(b, sizeof b, "Deploying...");
      else std::snprintf(b, sizeof b, "You were killed%s%s.   Respawning in %.0f s", L->killer >= 0 ? " by " : "", L->killer >= 0 ? match.slots[L->killer].name.c_str() : "", left);
      ImVec2 ts = ImGui::CalcTextSize(b);
      uiGlassPanel(dl, ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f - 14, io.DisplaySize.y * 0.7f - 10), ImVec2(io.DisplaySize.x * 0.5f + ts.x * 0.5f + 14, io.DisplaySize.y * 0.7f + 24), std::max(8.0f, (float)5));
      dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y * 0.7f), IM_COL32(255, 220, 200, 255), b);
    }
  }
  if (!match.spawnProblem.empty()) dl->AddText(ImVec2(14, io.DisplaySize.y - 152), IM_COL32(255, 120, 90, 255), match.spawnProblem.c_str());
}
