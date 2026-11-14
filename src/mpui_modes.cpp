// G1: Solo / Team Deathmatch HUD - the persistent mode banner, the scoreboard (held Tab and the end screen).
#include "game.h"
#include "app.h"
#include "mpui.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>

static ImU32 teamCol(Faction f) { return f == Faction::Allies ? IM_COL32(150, 190, 255, 255) : IM_COL32(255, 160, 130, 255); }

// top-centre banner. Solo: "First to N  |  Leader: name K  |  You: k". TDM: "First to N" + both teams' kills.
void Game::drawModeBanner() {
  if (match.mode() == GameMode::Assault) return;
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
    dl->AddRectFilled(ImVec2(cx - ts.x * 0.5f - 12, y), ImVec2(cx + ts.x * 0.5f + 12, y + 28), IM_COL32(12, 12, 10, 200), 5);
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
  dl->AddRectFilled(ImVec2(cx - 190, y), ImVec2(cx + 190, y + 50), IM_COL32(12, 12, 10, 205), 5);
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
  const Soldier* L = local();
  const int kTop = 8;
  auto rows = [&](int team) {   // team -1: everyone (Solo)
    std::vector<const RosterSlot*> v;
    for (auto& s : match.slots) if (team < 0 || s.team == team) v.push_back(&s);
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->score != b->score ? a->score > b->score : a->kills != b->kills ? a->kills > b->kills : a->deaths < b->deaths; });
    if (team >= 0 && !v.empty()) {
      int k = 0, d = 0, sc = 0; for (auto* s : v) { k += s->kills; d += s->deaths; sc += s->score; }
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(teamCol(v[0]->faction)), "%s %s - %zu players   kills %d   deaths %d   score %d",
                         match.mode() == GameMode::TDM ? (team == 0 ? "Team A" : "Team B") : (v[0]->role == CombatRole::Defender ? "Defenders" : "Attackers"),
                         factionName(v[0]->faction), v.size(), k, d, sc);
    }
    bool scroll = full && v.size() > 12;
    if (scroll) ImGui::BeginChild(team < 0 ? "sbS" : team == 0 ? "sbSA" : "sbSB", ImVec2(460, 12 * ImGui::GetTextLineHeightWithSpacing() + 30), false);
    if (ImGui::BeginTable(team < 0 ? "sb" : team == 0 ? "sbA" : "sbB", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("#", 0, 28); ImGui::TableSetupColumn("Player", 0, 220); ImGui::TableSetupColumn("Kills", 0, 56);
      ImGui::TableSetupColumn("Deaths", 0, 56); ImGui::TableSetupColumn("Score", 0, 64);
      ImGui::TableHeadersRow();
      int rank = 0, hiddenN = 0;
      for (auto* s : v) {
        ++rank;
        bool me = L && s->id == L->slot;
        if (!full && rank > kTop && !me) { ++hiddenN; continue; }
        ImGui::TableNextRow();
        if (me) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(90, 80, 40, 150));
        ImGui::TableNextColumn(); ImGui::Text("%d", rank);
        ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(teamCol(s->faction)), "%s%s", s->name.c_str(), s->human ? "" : "  (AI)");
        ImGui::TableNextColumn(); ImGui::Text("%d", s->kills);
        ImGui::TableNextColumn(); ImGui::Text("%d", s->deaths);
        ImGui::TableNextColumn(); ImGui::Text("%d", s->score);
      }
      ImGui::EndTable();
      if (hiddenN) ImGui::TextDisabled("+ %d more", hiddenN);
    }
    if (scroll) ImGui::EndChild();
  };
  if (match.mode() == GameMode::Solo) rows(-1);
  else for (int t = 0; t < 2; ++t) { rows(t); if (t == 0) ImGui::Spacing(); }
}

// held Tab during a match (not while building: build mode owns Tab for its palette)
void Game::drawScoreboardOverlay() {
  bool held = showScoreboard || (!app.selftest && app.win && !app.build.active && app.bk("scoreboard") >= 0 && glfwGetKey(app.win, app.bk("scoreboard")) == GLFW_PRESS);
  if (!held || match.phase == MatchPhase::Ended) return;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("Scoreboard", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::Text("%s  -  first to %d kills   (%.0f s)", gameModeName(match.mode()), match.rules.killCap, match.time);
  ImGui::Separator();
  scoreboardTable();
  ImGui::End();
}

// Solo / TDM side panel and respawn countdown (Assault keeps its own forces / lives panel)
void Game::drawModePanel() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Soldier* L = local();
  char b[220];
  int alive = 0; for (auto& s : sold) alive += s.alive;
  std::snprintf(b, sizeof b, "%s: %d of %zu alive   [Tab] scoreboard", gameModeName(match.mode()), alive, sold.size());
  dl->AddText(ImVec2(14, io.DisplaySize.y - 132), IM_COL32(220, 215, 195, 255), b);
  if (L) {
    const RosterSlot& rs = match.slots[L->slot];
    std::snprintf(b, sizeof b, "You: %s   Kills %d   Deaths %d   Score %d   Health %.0f   Frag %d   Smoke %d", factionName(L->faction), rs.kills, rs.deaths, rs.score, std::max(0.0f, L->hp), L->frags, L->smokes);
    dl->AddText(ImVec2(14, io.DisplaySize.y - 112), IM_COL32(240, 235, 215, 255), b);
    float hp = std::clamp(L->hp / 100.0f, 0.0f, 1.0f);
    dl->AddRectFilled(ImVec2(14, io.DisplaySize.y - 94), ImVec2(214, io.DisplaySize.y - 86), IM_COL32(50, 20, 20, 220));
    dl->AddRectFilled(ImVec2(14, io.DisplaySize.y - 94), ImVec2(14 + 200 * hp, io.DisplaySize.y - 86), IM_COL32(200, 60, 50, 255));
    if (!L->alive && match.phase == MatchPhase::Running) {
      float left = std::max(0.0f, match.rules.respawnDelay - rs.deadFor);
      if (L->lifeId == 0) std::snprintf(b, sizeof b, "Deploying...");
      else std::snprintf(b, sizeof b, "You were killed%s%s.   Respawning in %.0f s", L->killer >= 0 ? " by " : "", L->killer >= 0 ? match.slots[L->killer].name.c_str() : "", left);
      ImVec2 ts = ImGui::CalcTextSize(b);
      dl->AddRectFilled(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f - 14, io.DisplaySize.y * 0.7f - 10), ImVec2(io.DisplaySize.x * 0.5f + ts.x * 0.5f + 14, io.DisplaySize.y * 0.7f + 24), IM_COL32(10, 8, 8, 200), 5);
      dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y * 0.7f), IM_COL32(255, 220, 200, 255), b);
    }
  }
  if (!match.spawnProblem.empty()) dl->AddText(ImVec2(14, io.DisplaySize.y - 152), IM_COL32(255, 120, 90, 255), match.spawnProblem.c_str());
}
