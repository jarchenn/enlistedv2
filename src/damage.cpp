// Local wall damage: per-copy panel health (Explore session overlay), replaceable wall collision,
// explosion resolution in each building's local frame.
#include "app.h"
#include "mpai.h"
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
  r.damaged = false;
  for (size_t k = 0; k < r.wf.size(); ++k) if (r.wf[k].active) r.damaged = true;
  ShapePtr s;
  if (!r.damaged) s = a->wallsIntact;   // shared across every undamaged copy
  else {
    std::vector<ShapePtr> v;   // damaged bays use their rebuilt collision (same boxes as the mesh); others the prefab
    for (size_t k = 0; k < a->panels.size(); ++k) {
      const ShapePtr& ps = k < r.wf.size() && r.wf[k].active ? r.pshape[k] : a->panels[k].shape[0];
      if (ps) v.push_back(ps);
    }
    s = Physics::buildCompound(v);       // this copy only; prefab shapes are never mutated
  }
  if (r.wallBody == 0xffffffffu) { if (s) r.wallBody = Physics::addStatic(s, i.pos, yawQuat(i.yaw), wallUD(i.id)); }
  else if (s) Physics::setShape(r.wallBody, s);
  else { Physics::remove(r.wallBody); r.wallBody = 0xffffffffu; }   // every bay fully destroyed
  r.lastRebuildMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
}

void App::resetDamage() {
  resetLeafDamage();
  resetTrees();
  interior.reset(*this);   // furniture back to its authored state
  for (auto& i : doc.instances) {   // session overlays of wire and concrete paths
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    if (!r.slabCut.empty()) { std::fill(r.slabCut.begin(), r.slabCut.end(), 0); r.pathGround = ~0u; }
    if (r.asset->defense.sections > 0) { r.secHp.assign(r.asset->defense.sections, r.asset->defense.sectionHp); r.secCut.assign(r.asset->defense.sections, 0); r.defDestroyed = false; rebuildSectionBodies(i, r); }
  }
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    if (r.asset->panels.empty()) continue;
    std::vector<WallEvent> authored; authoredWallEvents(i, r.asset, authored);
    if (authored.size() == r.wallEvents.size() && !r.wf.empty()) continue;   // nothing from this session
    for (auto& m : r.pmesh) m.destroy();
    replayWallDamage(i, r);   // back to the authored state exactly (same events, same seeds)
    rebuildWalls(i, r);
  }
}

App::Explosion App::explode(vec3 p, vec3 dir, float dmg, float radius, float craterScale, uint64_t ignoreUd, int wallKind) {
  Explosion ex; ex.pos = p;
  blastLeaves(p, radius);   // glass within 1.6 R shatters, shutters / doors within R are blown off
  blastTrees(p, radius);    // HP_AT = 1: a shell within 1.5 m of a trunk fells it
  blastDefenses(p, dmg, radius);
  blastPaths(p, dmg, radius);
  for (auto& i : doc.instances) {   // splash on AT guns: separately tuned, never classified as a direct hit
    auto it = rt.find(i.id); if (it == rt.end() || !isAtGun(it->second.asset) || it->second.st.wrecked) continue;
    float dist = glm::length(it->second.st.pos + vec3(0, 0.8f, 0) - p);
    if (dist > radius) continue;
    it->second.st.hp -= blastSplashGun * (1.0f - dist / radius); ++splashGunHits;
    if (it->second.st.hp <= 0) wreckStation(i.id, "splash");
  }
  blastPlayer(p - (glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, -1, 0)) * 0.06f, dmg, radius);   // exposure readout, sandbag occlusion x0.1
  vec3 d = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, -1, 0);
  // walls: one impact-centred stamp at the true contact point, applied to every bay it overlaps (wallfield.cpp)
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    const AssetDef* a = r.asset;
    if (a->panels.empty()) continue;
    float br = glm::length(a->bmax - a->bmin) * 0.5f;
    if (glm::length(p - (i.pos + yawQuat(i.yaw) * ((a->bmin + a->bmax) * 0.5f))) > br + 1.5f) continue;
    quat q = yawQuat(i.yaw);
    WallEvent e;
    e.p = glm::inverse(q) * (p - i.pos); e.d = glm::inverse(q) * d; e.kind = wallKind; e.dmg = dmg;
    e.seed = (uint32_t)(i.id * 2654435761u) ^ (uint32_t)(r.wallEvents.size() * 40503u + 17u);   // deterministic per copy / event
    if (r.wf.empty()) initWallFields(r);
    if (stampWall(r, e, true, &ex.hits, &ex.breached) > 0) { r.wallEvents.push_back(e); rebuildWalls(i, r); }
  }
  interior.blast(*this, p, d, dmg, radius, wallKind);   // furniture: shielded by walls, supports re-checked after the stamp
  if (craterScale > 0.0f) ex.crater = deformGround(p, craterScale);
  mpaiExplosion(*this, p, dir, dmg, radius, craterScale, wallKind, ex.breached, ex.hits.size());   // soldiers, AI hearing, nav update, LAN
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
