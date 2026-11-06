// AI debug tools (AI_SPEC 12.1), shown with the match debug overlay (F6):
//   overlays: navmesh near the camera coloured by area (forbidden red, crouch dark), off-mesh links by kind, each
//   bot's planned route, its action with the top 3 utility scores, suppression meter, chosen and candidate cover,
//   known threats per team, stuck events; the "AI debug" window: follow camera on any bot, freeze / step, spawn a bot
//   at the crosshair, force the difficulty, overlay toggles and the profiler (nav / perception / decisions / aiming /
//   movement per step).
#include "ai_core.h"
#include "app.h"
#include <DetourNavMesh.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
bool proj(App& app, vec3 p, ImVec2& s) {
  vec4 c = app.projM * app.viewM * vec4(p, 1);
  if (c.w <= 0.05f) return false;
  ImGuiIO& io = ImGui::GetIO();
  s = ImVec2((c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (1 - (c.y / c.w * 0.5f + 0.5f)) * io.DisplaySize.y);
  return true;
}
void line(App& app, ImDrawList* dl, vec3 a, vec3 b, ImU32 col, float w = 1.0f) { ImVec2 sa, sb; if (proj(app, a, sa) && proj(app, b, sb)) dl->AddLine(sa, sb, col, w); }
ImU32 areaColour(uint8_t a) {
  if (navarea::isLink(a)) return IM_COL32(255, 255, 255, 200);
  NavArea n = navarea::toNav(a);
  if (navAreaCost(n) < 0) return IM_COL32(255, 40, 40, 170);
  ImU32 c = n == NavArea::Road ? IM_COL32(200, 200, 200, 110) : n == NavArea::Brush ? IM_COL32(80, 160, 60, 120) : n == NavArea::Marsh || n == NavArea::Water ? IM_COL32(60, 120, 220, 130)
          : n == NavArea::Sand ? IM_COL32(230, 210, 120, 120) : n == NavArea::Trench ? IM_COL32(160, 110, 60, 150) : IM_COL32(90, 220, 255, 80);
  if (navarea::crouch(a)) c = (c & 0x00FFFFFF) | 0xC8000000;
  return c;
}
const ImU32 kLinkCol[4] = {IM_COL32(255, 160, 40, 230), IM_COL32(255, 255, 80, 230), IM_COL32(120, 255, 120, 230), IM_COL32(255, 90, 255, 230)};
}

void aiDrawDebug(Game& g) { if (AiCore* A = aiCore()) A->drawDebug(); else (void)g; }

void AiCore::drawDebug() {
  App& app = G.app;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  vec3 cam = app.eyePos();
  // ---- navmesh and links near the camera
  if (overlayNav && G.nav.mesh()) {
    const dtNavMesh* mesh = G.nav.mesh();
    int tx0 = G.nav.tileOf(cam.x - 40), tx1 = G.nav.tileOf(cam.x + 40), tz0 = G.nav.tileOf(cam.z - 40), tz1 = G.nav.tileOf(cam.z + 40);
    for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
      const dtMeshTile* t = mesh->getTileAt(tx, tz, 0);
      if (!t || !t->header) continue;
      for (int i = 0; i < t->header->polyCount; ++i) {
        const dtPoly& p = t->polys[i];
        if (p.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
        const float* v0 = &t->verts[p.verts[0] * 3];
        if (glm::length(vec3(v0[0], v0[1], v0[2]) - cam) > 45) continue;
        ImU32 col = areaColour(p.getArea());
        for (int k = 0; k < p.vertCount; ++k) {
          const float* a = &t->verts[p.verts[k] * 3]; const float* b = &t->verts[p.verts[(k + 1) % p.vertCount] * 3];
          bool wall = p.neis[k] == 0;
          line(app, dl, vec3(a[0], a[1] + 0.06f, a[2]), vec3(b[0], b[1] + 0.06f, b[2]), wall ? (col | 0xFF000000) : col, wall ? 2.0f : 1.0f);
        }
      }
    }
    for (auto& l : G.nav.linksNear(cam, 45)) {
      vec3 mid = (l.a + l.b) * 0.5f + vec3(0, 0.6f, 0);
      line(app, dl, l.a + vec3(0, 0.1f, 0), mid, kLinkCol[l.kind], 2); line(app, dl, mid, l.b + vec3(0, 0.1f, 0), kLinkCol[l.kind], 2);
      ImVec2 s; if (!l.bidir && proj(app, l.b + vec3(0, 0.1f, 0), s)) dl->AddCircleFilled(s, 3, kLinkCol[l.kind]);
    }
  }
  // ---- cover points
  if (overlayCover) for (auto& c : cover) {
    if (!c.serial || glm::length(c.p - cam) > 40) continue;
    ImVec2 s; if (!proj(app, c.p + vec3(0, 0.3f, 0), s)) continue;
    ImU32 col = c.heldBy >= 0 ? IM_COL32(255, 220, 60, 230) : c.high ? IM_COL32(120, 200, 255, 160) : IM_COL32(120, 255, 200, 160);
    dl->AddCircle(s, c.high ? 5 : 3.5f, col, 8, 1.5f);
    if (glm::length(c.n) > 0.1f) line(app, dl, c.p + vec3(0, 0.3f, 0), c.p + vec3(0, 0.3f, 0) + c.n * 0.7f, col);
  }
  // ---- bots
  for (auto& s : G.sold) {
    if (!s.alive || s.human || s.slot >= (int)minds.size() || !minds[s.slot]) continue;
    BotMind& m = *minds[s.slot];
    if (glm::length(s.feet - cam) > 160) continue;
    ImU32 tc = s.role == CombatRole::Attacker ? IM_COL32(255, 140, 120, 255) : IM_COL32(140, 180, 255, 255);
    if (overlayPaths) {
      vec3 prev = s.feet + vec3(0, 0.2f, 0); bool started = false;
      for (auto& p : m.route) { if (!started && glm::length(p - s.feet) > 60) continue; started = true; line(app, dl, prev, p + vec3(0, 0.2f, 0), IM_COL32(120, 255, 140, 170), 1.5f); prev = p + vec3(0, 0.2f, 0); }
      if (m.cover >= 0 && m.cover < (int)cover.size()) { ImVec2 c; if (proj(app, m.coverPos + vec3(0, 0.5f, 0), c)) dl->AddCircleFilled(c, 5, m.set ? IM_COL32(60, 255, 120, 220) : IM_COL32(120, 220, 255, 200)); }
    }
    ImVec2 sp;
    if (!proj(app, s.feet + vec3(0, 2.75f, 0), sp)) continue;
    // top 3 utilities
    std::array<int, A_COUNT> ord; for (int k = 0; k < A_COUNT; ++k) ord[k] = k;
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { return m.util[a] > m.util[b]; });
    char buf[320];
    std::snprintf(buf, sizeof buf, "%s #%d %s%s | %s\n%s %.2f  %s %.2f  %s %.2f\nsup %.2f  %s%s%s", s.role == CombatRole::Attacker ? "ATK" : "DEF", s.slot, aiActionName(m.action),
                  m.flanker ? (s.br.lane == LANE_LEFT ? " [L]" : " [R]") : "", s.br.why.c_str(), aiActionName(ord[0]), m.util[ord[0]], aiActionName(ord[1]), m.util[ord[1]], aiActionName(ord[2]), m.util[ord[2]],
                  s.br.suppressed, m.ladder ? "STUCK step " : "", m.ladder ? std::to_string(m.ladder).c_str() : "", m.trav.on ? (std::string(" ") + navLinkName(m.trav.kind)).c_str() : "");
    dl->AddText(sp, tc, buf);
    dl->AddRectFilled(ImVec2(sp.x, sp.y - 5), ImVec2(sp.x + 40 * s.br.suppressed, sp.y - 2), IM_COL32(255, 80, 40, 220));
  }
  // ---- known threats (team knowledge)
  if (overlayThreats) for (auto& tb : teams) for (auto& [e, k] : tb.known) {
    ImVec2 s; if (!proj(app, k.pos + vec3(0, 1.0f, 0), s)) continue;
    ImU32 col = tb.role == (int)CombatRole::Attacker ? IM_COL32(255, 120, 60, (int)(80 + 170 * k.conf)) : IM_COL32(80, 160, 255, (int)(80 + 170 * k.conf));
    dl->AddCircle(s, 7, col, 12, 2); if (k.heard) dl->AddText(ImVec2(s.x + 8, s.y - 6), col, "?");
  }
  // ---- stuck events
  for (auto& e : stuck) {
    if (!e.open && G.t - e.t1 > 30) continue;
    ImVec2 s; if (!proj(app, e.pos + vec3(0, 0.5f, 0), s)) continue;
    ImU32 col = e.open ? IM_COL32(255, 30, 30, 255) : e.resolved ? IM_COL32(255, 200, 0, 200) : IM_COL32(255, 0, 255, 255);
    dl->AddLine(ImVec2(s.x - 6, s.y - 6), ImVec2(s.x + 6, s.y + 6), col, 2); dl->AddLine(ImVec2(s.x - 6, s.y + 6), ImVec2(s.x + 6, s.y - 6), col, 2);
  }
  // ---- debug window
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 380, 60), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
  if (ImGui::Begin("AI debug")) {
    int di = 0; auto& names = aiDifficultyNames(); for (size_t k = 0; k < names.size(); ++k) if (names[k] == diff.name) di = (int)k;
    if (ImGui::Combo("Difficulty", &di, "Recruit\0Regular\0Veteran\0")) diff = AiDifficulty::load(diffj, names[di]);
    ImGui::Checkbox("Freeze AI", &frozen); ImGui::SameLine(); if (ImGui::Button("Step 1")) stepFrames = 1; ImGui::SameLine(); if (ImGui::Button("Step 30")) stepFrames = 30;
    ImGui::InputInt("Follow bot (-1 off)", &follow); follow = std::clamp(follow, -1, (int)G.sold.size() - 1);
    static int spawnRole = 1;
    ImGui::RadioButton("Attacker", &spawnRole, 1); ImGui::SameLine(); ImGui::RadioButton("Defender", &spawnRole, 0); ImGui::SameLine();
    if (ImGui::Button("Spawn bot at crosshair")) {
      Physics::Hit h; vec3 e = app.eyePos(), f = App::forward(app.eyeYaw, app.eyePitch);
      NavRef r; vec3 on;
      if (Physics::castRayEx(e, f, 300, h) && G.nav.closest(h.pos, 3.0f, r, on))
        for (auto& s : G.sold) if (!s.alive && !s.human && (int)s.role == spawnRole) { std::string why; if (G.trySpawn(s.slot, why)) { Physics::setAgentPos(s.agent, on + vec3(0, 0.05f, 0)); s.feet = on; } break; }
    }
    ImGui::Checkbox("Navmesh", &overlayNav); ImGui::SameLine(); ImGui::Checkbox("Paths", &overlayPaths); ImGui::SameLine(); ImGui::Checkbox("Cover", &overlayCover); ImGui::SameLine(); ImGui::Checkbox("Threats", &overlayThreats);
    ImGui::Separator();
    ImGui::Text("Navmesh: %d tiles, %d polygons, %d links (drop %d mantle %d vault %d jump %d)", G.nav.tilesBuilt, G.nav.usedNodes, G.nav.links, G.nav.linkCount[0], G.nav.linkCount[1], G.nav.linkCount[2], G.nav.linkCount[3]);
    ImGui::Text("Bake %.0f ms; rebuilds %d (last %.1f ms, max %.1f ms)%s", G.nav.buildMs, G.nav.rebuilds, G.nav.lastRebuildMs, G.nav.rebuildMsMax, G.nav.busy() ? " [rebuilding]" : "");
    ImGui::Text("Paths: %d ok, %d failed, %.2f ms mean on the worker, %d in flight", paths.completed, paths.failed, paths.completed + paths.failed ? paths.msSum / (paths.completed + paths.failed) : 0.0, paths.inFlight());
    long n = std::max(1L, prof.frames);
    for (int c = 0; c < PROF_COUNT; ++c) ImGui::Text("%-11s %.3f ms mean, %.2f ms max", AiProf::name(c), prof.sum[c] / n, prof.max[c]);
    ImGui::Text("Rays this step %d / %d; cover points %zu; stuck events %d (%d unrecovered)", raysThisFrame, rayBudget, cover.size(), metrics.stuckEvents, metrics.stuckUnrecovered);
  }
  ImGui::End();
}

bool aiDebugCamera(Game& g, vec3& eye, vec3& fwd, float& fov) {
  AiCore* A = aiCore();
  if (!A || A->follow < 0 || A->follow >= (int)g.sold.size() || !g.sold[A->follow].alive) return false;
  const Soldier& s = g.sold[A->follow];
  vec3 target = s.feet + vec3(0, 1.6f, 0);
  vec3 back = vec3(std::sin(radians(s.yaw)), 0.5f, std::cos(radians(s.yaw))) * 3.5f;
  Physics::Hit h; vec3 c = target + back;
  if (Physics::castRayEx(target, back, glm::length(back), h)) c = target + glm::normalize(back) * std::max(0.3f, h.t - 0.2f);
  eye = c; fwd = glm::normalize(target - c); fov = 70;
  return true;
}

void aiDifficultyCombo() {
  std::string& d = aiDifficultySetting();
  int di = d == "recruit" ? 0 : d == "veteran" ? 2 : 1;
  if (ImGui::Combo("Bot difficulty", &di, "Recruit\0Regular\0Veteran\0")) d = aiDifficultyNames()[di];
}
