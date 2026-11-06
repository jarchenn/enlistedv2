// Local wall damage: per-copy panel health (Explore session overlay), replaceable wall collision,
// explosion resolution in each building's local frame.
#include "app.h"
#include <imgui.h>
#include <chrono>
#include <algorithm>

uint8_t App::panelStateFor(float hp, float maxHp) {
  if (hp <= 0.0f) return PANEL_BREACHED;
  if (hp < 0.6f * maxHp) return PANEL_DAMAGED;
  return PANEL_INTACT;
}

int App::findPanel(const AssetDef* a, const std::string& id) const {
  for (size_t k = 0; k < a->panels.size(); ++k) if (a->panels[k].id == id) return (int)k;
  return -1;
}

static uint64_t wallUD(uint64_t id) { return (id << 16) | 0xFFFFull; }

void App::rebuildWalls(const Instance& i, InstanceRT& r) {
  auto t0 = std::chrono::high_resolution_clock::now();
  const AssetDef* a = r.asset;
  r.damaged = std::any_of(r.state.begin(), r.state.end(), [](uint8_t s) { return s != PANEL_INTACT; });
  ShapePtr s;
  if (!r.damaged) s = a->wallsIntact;   // shared across every undamaged copy
  else {
    std::vector<ShapePtr> v;
    for (size_t k = 0; k < a->panels.size(); ++k) v.push_back(a->panels[k].shape[r.state[k]]);
    s = Physics::buildCompound(v);       // this copy only; prefab shapes are never mutated
  }
  if (r.wallBody == 0xffffffffu) { if (s) r.wallBody = Physics::addStatic(s, i.pos, yawQuat(i.yaw), wallUD(i.id)); }
  else if (s) Physics::setShape(r.wallBody, s);
  r.lastRebuildMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
}

void App::resetDamage() {
  resetLeafDamage();
  resetTrees();
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    bool changed = false;
    for (size_t k = 0; k < r.asset->panels.size(); ++k) {
      auto& p = r.asset->panels[k];
      auto ov = i.panelHp.find(p.id);
      float hp = ov != i.panelHp.end() ? ov->second : p.hp;
      uint8_t st = panelStateFor(hp, p.hp);
      if (st != r.state[k]) changed = true;
      r.hp[k] = hp; r.state[k] = st;
    }
    if (changed) rebuildWalls(i, r);
  }
}

App::Explosion App::explode(vec3 p, vec3 dir, float dmg, float radius, float craterScale, uint64_t ignoreUd) {
  Explosion ex; ex.pos = p;
  blastLeaves(p, radius);   // glass within 1.6 R shatters, shutters / doors within R are blown off
  blastTrees(p, radius);    // HP_AT = 1: a shell within 1.5 m of a trunk fells it
  blastDefenses(p, dmg, radius);
  for (auto& i : doc.instances) {   // splash on AT guns: separately tuned, never classified as a direct hit
    auto it = rt.find(i.id); if (it == rt.end() || !isAtGun(it->second.asset) || it->second.st.wrecked) continue;
    float dist = glm::length(it->second.st.pos + vec3(0, 0.8f, 0) - p);
    if (dist > radius) continue;
    it->second.st.hp -= blastSplashGun * (1.0f - dist / radius); ++splashGunHits;
    if (it->second.st.hp <= 0) wreckStation(i.id, "splash");
  }
  blastPlayer(p - (glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, -1, 0)) * 0.06f, dmg, radius);   // exposure readout, sandbag occlusion x0.1
  vec3 d = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, -1, 0);
  vec3 origin = p - d * 0.06f;   // just in front of the struck surface
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    const AssetDef* a = r.asset;
    if (a->panels.empty()) continue;
    float br = glm::length(a->bmax - a->bmin) * 0.5f;
    if (glm::length(p - (i.pos + yawQuat(i.yaw) * ((a->bmin + a->bmax) * 0.5f))) > br + radius) continue;
    quat q = yawQuat(i.yaw);
    vec3 lp = glm::inverse(q) * (origin - i.pos);   // impact in the building's local frame
    bool rebuild = false;
    for (size_t k = 0; k < a->panels.size(); ++k) {
      auto& pn = a->panels[k];
      if (r.state[k] == PANEL_BREACHED) continue;
      vec3 cp = glm::clamp(lp, pn.bmin, pn.bmax);
      float dist = glm::length(cp - lp);
      if (dist > radius) continue;
      if (dist > 0.12f) {   // solid cover: something other than this copy's walls in between blocks the blast
        vec3 wc = i.pos + q * cp;
        Physics::Hit h;
        float len = glm::length(wc - origin);
        if (len > 0.05f && Physics::castRayEx(origin, wc - origin, len, h, ignoreUd) && h.t < len - 0.15f && h.ud != wallUD(i.id)) continue;
      }
      float dmgK = dmg * (1.0f - dist / radius);
      r.hp[k] -= dmgK;
      uint8_t st = panelStateFor(r.hp[k], pn.hp);
      ex.hits.push_back(pn.id);
      if (st != r.state[k]) {
        rebuild = true;
        if (st == PANEL_BREACHED) ++ex.breached;
        r.state[k] = st;
      }
    }
    if (rebuild) rebuildWalls(i, r);
  }
  if (craterScale > 0.0f) ex.crater = deformGround(p, craterScale);
  lastExplosion = ex;
  return ex;
}

void App::drawDamageDebug() {
  if (!panelDebug) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  mat4 vp = projM * viewM;
  vec3 eye = mode == Mode::Explore ? eyePos() : camPos;
  auto project = [&](vec3 w, vec2& s) {
    vec4 c = vp * vec4(w, 1);
    if (c.w < 0.1f) return false;
    s = vec2((c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - c.y / c.w * 0.5f) * io.DisplaySize.y);
    return s.x > 0 && s.y > 0 && s.x < io.DisplaySize.x && s.y < io.DisplaySize.y;
  };
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    quat q = yawQuat(i.yaw);
    for (size_t k = 0; k < r.asset->panels.size(); ++k) {
      auto& pn = r.asset->panels[k];
      vec3 c = i.pos + q * ((pn.bmin + pn.bmax) * 0.5f + pn.normal * 0.4f);
      if (glm::length(c - eye) > 30) continue;
      vec2 s;
      if (!project(c, s)) continue;
      static const char* names[] = {"intact", "damaged", "BREACHED"};
      char b[160];
      std::snprintf(b, sizeof b, "%s\n%.0f/%.0f %s\n%s", pn.id.c_str(), std::max(0.0f, r.hp[k]), pn.hp, pn.family.c_str(), names[r.state[k]]);
      ImU32 col = r.state[k] == 2 ? IM_COL32(255, 90, 70, 255) : r.state[k] == 1 ? IM_COL32(255, 200, 90, 255) : IM_COL32(150, 230, 150, 230);
      ImVec2 ts = ImGui::CalcTextSize(b);
      dl->AddRectFilled(ImVec2(s.x - 4, s.y - 3), ImVec2(s.x + ts.x + 4, s.y + ts.y + 3), IM_COL32(0, 0, 0, 150), 3);
      dl->AddText(ImVec2(s.x, s.y), col, b);
    }
  }
  vec2 s;
  if (!lastExplosion.hits.empty() && project(lastExplosion.pos, s)) {
    dl->AddCircle(ImVec2(s.x, s.y), 10, IM_COL32(255, 60, 40, 255), 16, 2);
    dl->AddText(ImVec2(s.x + 12, s.y - 6), IM_COL32(255, 120, 90, 255), "last impact");
  }
  char b[200];
  int total = 0, broken = 0;
  for (auto& [id, r] : rt) for (auto st : r.state) { ++total; if (st == PANEL_BREACHED) ++broken; }
  std::snprintf(b, sizeof b, "PANEL DEBUG (F4): %d panels, %d breached | last impact hit %zu panels, %d breached%s",
                total, broken, lastExplosion.hits.size(), lastExplosion.breached, lastExplosion.crater ? ", cratered" : "");
  dl->AddText(ImVec2(14, io.DisplaySize.y - 60), IM_COL32(255, 220, 140, 255), b);
}
