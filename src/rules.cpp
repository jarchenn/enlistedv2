// Gameplay rules (W1): spawn protection (SPEC 2.2) and player fall damage (SPEC 4). See rules.h.
#include "rules.h"
#include "damage_model.h"
#include "game.h"
#include "app.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

RulesStats gRulesStats;

namespace {
RulesConfig gCfg;
bool gCfgLoaded = false;

bool projectPt(App& app, vec3 p, ImVec2& s) {
  vec4 c = app.projM * app.viewM * vec4(p, 1);
  if (c.w <= 0.05f) return false;
  ImGuiIO& io = ImGui::GetIO();
  s = ImVec2((c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (1 - (c.y / c.w * 0.5f + 0.5f)) * io.DisplaySize.y);
  return true;
}
float protectLeft(const Game& g, const Soldier& s) { return s.isProtected(g.t) ? (float)(s.protectUntil - g.t) : 0.0f; }

// fall tracker for the local player (its explore character)
struct Fall { bool air = false; float maxDown = 0; int life = -1, slot = -1; } gFall;
}

void rulesReload() {
  gCfgLoaded = true;
  gCfg = RulesConfig{};
  try {
    json j = json::parse(readTextFile(contentDir() / "settings" / "rules.json"));
    gCfg.protectSeconds = j.value("spawnProtectionSeconds", 3.0f);
    gCfg.endsOnFire = j.value("protectionEndsOnFire", true);
    gCfg.endsOnThrow = j.value("protectionEndsOnThrow", true);
    json v = j.value("protectionVisuals", json::object());
    gCfg.shimmerHz = v.value("shimmerHz", gCfg.shimmerHz); gCfg.tintDepth = v.value("tintDepth", gCfg.tintDepth); gCfg.iconRange = v.value("shieldIconRange_m", gCfg.iconRange);
    if (v.contains("tint") && v["tint"].is_array() && v["tint"].size() == 3) gCfg.tint = vec3(v["tint"][0].get<float>(), v["tint"][1].get<float>(), v["tint"][2].get<float>());
  } catch (std::exception& e) { gCfg.error = std::string("rules.json: ") + e.what(); }
  try {
    json m = json::parse(readTextFile(contentDir() / "settings" / "movement.json"));
    json f = m.value("fallDamage", json::object());
    gCfg.safeHeight = f.value("safeHeight_m", gCfg.safeHeight); gCfg.lethalHeight = f.value("lethalHeight_m", gCfg.lethalHeight);
    if (gCfg.lethalHeight <= gCfg.safeHeight) gCfg.lethalHeight = gCfg.safeHeight + 1.0f;
  } catch (std::exception& e) { gCfg.error += std::string(" movement.json: ") + e.what(); }
  if (!gCfg.error.empty()) logf("rules: %s (defaults kept)", gCfg.error.c_str());
}
RulesConfig& rulesConfig() { if (!gCfgLoaded) rulesReload(); return gCfg; }

// ---------------------------------------------------------------- spawn protection
void rulesOnSpawn(Game& g, Soldier& s) {
  float sec = rulesConfig().protectSeconds;
  s.protectUntil = sec > 0 ? g.t + sec : -1.0;
}

bool rulesIgnoreDamage(const Game& g, int victim) {
  return victim >= 0 && victim < (int)g.sold.size() && g.sold[victim].isProtected(g.t);
}

void rulesOnFire(Game& g, int slot) {
  if (!rulesConfig().endsOnFire || slot < 0 || slot >= (int)g.sold.size()) return;
  Soldier& s = g.sold[slot];
  if (s.isProtected(g.t)) { s.protectUntil = g.t; ++gRulesStats.endedByFire; }
}

void rulesOnThrow(Game& g, int slot) {
  if (!rulesConfig().endsOnThrow || slot < 0 || slot >= (int)g.sold.size()) return;
  Soldier& s = g.sold[slot];
  if (s.isProtected(g.t)) { s.protectUntil = g.t; ++gRulesStats.endedByThrow; }
}

void rulesNetProtect(Game& g, Soldier& s, bool on) {
  // the host's bit; the client runs its own countdown from the first snapshot that shows it
  if (on) { if (!(g.t < s.protectUntil)) s.protectUntil = g.t + rulesConfig().protectSeconds; }
  else s.protectUntil = -1;
}

vec4 rulesTint(const Game& g, const Soldier& s) {
  float left = protectLeft(g, s);
  if (left <= 0) return vec4(1);
  const RulesConfig& c = rulesConfig();
  float hz = left < 0.75f ? c.shimmerHz * 2.5f : c.shimmerHz;   // flickers faster as protection runs out
  float p = 0.5f + 0.5f * std::sin((float)g.t * 6.2831853f * hz + s.slot * 1.7f);
  float k = std::clamp(c.tintDepth * (0.55f + 0.45f * p), 0.0f, 1.0f);
  return vec4(glm::mix(vec3(1), c.tint, k), 1);
}

void rulesDeflectPuff(App& app, vec3 p, vec3 n, vec3 dir) {
  // a pale, bright ward flash instead of the flesh puff: no blood, no gore (SPEC 2.2, 7.4)
  vec3 r = glm::normalize(glm::reflect(dir, n) + n * 0.3f);
  app.fx.flash(p + n * 0.03f, 0.14f, 0.06f, vec4(3.5f, 4.5f, 7.0f, 1));
  for (int i = 0; i < 6; ++i)
    app.fx.smoke(p + n * 0.04f, (r * 1.6f + vec3(app.fx.rnd() - 0.5f, app.fx.rnd() * 0.6f, app.fx.rnd() - 0.5f)) * (0.6f + app.fx.rnd()), 0.05f + app.fx.rnd() * 0.05f, 0.35f + app.fx.rnd() * 0.25f,
                 vec4(0.82f, 0.88f, 1.0f, 0.55f), 1.6f, 0.4f);
  ++gRulesStats.deflects;
}

void rulesHud(Game& g) {
  gRulesStats.shieldIcons = 0; gRulesStats.ring = false; gRulesStats.ringLeft = 0;
  if (!g.active) return;
  App& app = g.app;
  const RulesConfig& c = rulesConfig();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  Soldier* L = g.local();
  vec3 cam = vec3(glm::inverse(app.viewM)[3]);
  // ---- small shield over the head of protected enemies (in line of sight only: never through walls)
  for (auto& s : g.sold) {
    if (!s.alive || s.localPlayer || !s.isProtected(g.t)) continue;
    if (L && s.role == L->role) continue;
    vec3 head = vec3(s.world * vec4(s.pose.joint[CJ_HEAD], 1)) + vec3(0, 0.42f, 0);
    float dist = glm::length(head - cam);
    if (dist > c.iconRange || !g.lineOfSight(cam, head - vec3(0, 0.3f, 0))) continue;
    ImVec2 sp; if (!projectPt(app, head, sp)) continue;
    float h = std::clamp(220.0f / std::max(dist, 1.0f), 6.0f, 13.0f), w = h * 0.8f;
    float left = protectLeft(g, s);
    int a = left < 0.75f ? (int)(150 + 105 * (0.5f + 0.5f * std::sin((float)g.t * 30.0f))) : 230;
    ImVec2 pts[6] = {ImVec2(sp.x - w, sp.y - h), ImVec2(sp.x, sp.y - h * 1.25f), ImVec2(sp.x + w, sp.y - h), ImVec2(sp.x + w * 0.85f, sp.y + h * 0.1f), ImVec2(sp.x, sp.y + h), ImVec2(sp.x - w * 0.85f, sp.y + h * 0.1f)};
    dl->AddConvexPolyFilled(pts, 6, IM_COL32(150, 190, 255, a * 2 / 3));
    dl->AddPolyline(pts, 6, IM_COL32(235, 245, 255, a), ImDrawFlags_Closed, 1.5f);
    ++gRulesStats.shieldIcons;
  }
  // ---- countdown ring for the protected local player
  if (L && L->alive && L->isProtected(g.t)) {
    ImGuiIO& io = ImGui::GetIO();
    float left = protectLeft(g, *L), frac = std::clamp(left / std::max(0.01f, c.protectSeconds), 0.0f, 1.0f);
    ImVec2 ctr(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    float R = 34;
    dl->AddCircle(ctr, R, IM_COL32(20, 30, 50, 120), 48, 4.0f);
    dl->PathArcTo(ctr, R, -1.5707963f, -1.5707963f + 6.2831853f * frac, 48);
    dl->PathStroke(IM_COL32(160, 200, 255, 230), 0, 3.0f);
    char b[64]; std::snprintf(b, sizeof b, "SPAWN PROTECTION %.1f s", left);
    ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y + R + 8), IM_COL32(200, 220, 255, 235), b);
    gRulesStats.ring = true; gRulesStats.ringLeft = left;
  }
}

// ---------------------------------------------------------------- fall damage (player only)
float rulesImpactSpeed(float h, float gravity) { return std::sqrt(2.0f * std::max(0.0f, gravity) * std::max(0.0f, h)); }

float rulesFallDamage(float v, float gravity) {
  const RulesConfig& c = rulesConfig();
  float vs = rulesImpactSpeed(c.safeHeight, gravity), vl = rulesImpactSpeed(c.lethalHeight, gravity);
  if (v <= vs) return 0.0f;
  return damageTable().health * std::clamp((v - vs) / std::max(1e-3f, vl - vs), 0.0f, 1.0f);
}

void rulesAfterMove(App& app, const Physics::CharState& before, const Physics::CharState& after, float dt) {
  Game* g = gGame;
  Soldier* L = g && g->active && !g->isClient ? g->local() : nullptr;   // LAN client: the host owns damage (not replicated yet)
  if (!L || !L->alive) { gFall = Fall{}; return; }
  if (L->lifeId != gFall.life || L->slot != gFall.slot) { gFall = Fall{}; gFall.life = L->lifeId; gFall.slot = L->slot; }
  if (!after.grounded) { gFall.air = true; gFall.maxDown = std::max(gFall.maxDown, -after.vel.y); return; }
  if (!gFall.air) return;
  // landed: the fastest downward speed of the fall, plus the half step of gravity the last airborne step did not see
  float v = std::max(gFall.maxDown, -before.vel.y);
  gFall.air = false; gFall.maxDown = 0;
  if (v <= 0) return;
  v += 0.5f * app.move.gravity * dt;
  float dmg = rulesFallDamage(v, app.move.gravity);
  gRulesStats.lastFallSpeed = v; gRulesStats.lastFallDamage = dmg; ++gRulesStats.falls;
  if (dmg >= 0.5f) g->damage(L->slot, dmg, -1, vec3(0, -1, 0), "fall");   // spawn protection applies (Game::damage)
}
