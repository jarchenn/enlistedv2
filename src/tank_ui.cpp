// Tanks: drawing (supplied GLB parts on the rig), gunner sights, HUD, Enter prompt, catalog thumbnails and the
// temporary synthesized tank sounds. docs/tanks.md
#include "vehicle_dash.h"
#include "app.h"
#include "settings.h"
#include <imgui.h>
#include <random>
#include <algorithm>

namespace {
const char* shellName(int s) { return s == SHELL_HE ? "HE" : s == SHELL_AP ? "AP" : "EMPTY"; }
}

// ============================================================ drawing
// TK2: the LOD by the hull's projected size: full above 260 px across, LOD1 above 80 px, LOD2 below; 15 % hysteresis
// so a tank at a threshold does not flicker. The occupied vehicle is always full (sights, first-person views); a wreck
// is at most LOD1
const GpuModel* TankSys::lodModel(const App& app, const TankRT& t) const {
  const TankDef& d = *t.def;
  if (!d.model || (!d.lods[0] && !d.lods[1]) || t.id == occupied) { t.lodLevel = 0; return d.model.get(); }
  float r = 0.5f * glm::length(d.model->bmax - d.model->bmin), dist = std::max(0.5f, glm::length(t.pos - app.eyePos()));
  float px = 2.0f * r / dist * app.projM[1][1] * 0.5f * std::max(1, app.R.height);
  int want = px > 260.0f ? 0 : px > 80.0f ? 1 : 2;
  int cur = t.lodLevel;
  if (want > cur && px > (cur == 0 ? 260.0f : 80.0f) * 0.85f) want = cur;   // getting smaller: only past 85 % of the threshold
  if (want < cur && px < (cur == 2 ? 80.0f : 260.0f) * 1.15f) want = cur;   // getting bigger: only past 115 %
  if (t.wrecked) want = std::max(want, 1);
  t.lodLevel = want;
  for (int L = want; L >= 1; --L) if (d.lods[L - 1]) return d.lods[L - 1].get();
  return d.model.get();
}

void TankSys::drawItems(App& app, const Instance& i, const InstanceRT& r) {
  (void)r;
  auto it = tanks.find(i.id);
  if (it == tanks.end() || !it->second.def || (!it->second.def->model && !it->second.def->partsAsset)) return;
  const TankRT& t = it->second; const TankDef& d = *t.def;
  const GpuModel* gm = lodModel(app, t);
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec4 tint = t.wrecked ? vec4(0.13f, 0.12f, 0.11f, 1) : vec4(1);   // burnt-out: darker than a live tank
  if (d.car) { drawCar(app, t, tint, h); return; }
  if (d.heli) { drawHeli(app, t, tint, h); return; }   // VH5
  bool sight = app.mode == Mode::Explore && occupied == t.id && t.hatchT < 0.5f;   // inside the optic: the own gun is not drawn
  auto put = [&](const std::vector<int>& ps, const mat4& M) { for (int k : ps) app.R.worldModels.push_back({gm, k, M, tint}); };
  put(d.hullParts, h);
  for (int s = 0; s < 2; ++s) { put(d.trackParts[s], h); put(d.wheelParts[s], h); }
  put(d.turretParts, tu);
  put(d.hatchParts, tu);
  if (!sight) put(d.gunParts, rc);
  if (!d.mgParts.empty()) put(d.mgParts, mgMatrix(t) * glm::translate(mat4(1), -d.mgPivot));
  else if (!d.mgWeapon.empty()) {
    int wi = app.weaponIndex(d.mgWeapon);
    if (wi >= 0 && app.weaponDefs[wi]->useModel()) {
      const WeaponDef& wd = *app.weaponDefs[wi];
      mat4 W = mgMatrix(t) * glm::translate(mat4(1), d.mgModelOffset);
      for (int p = 0; p < 8; ++p) for (int k : wd.modelParts[p]) app.R.worldModels.push_back({wd.model.get(), k, W, tint});
    }
  }
  if (d.mgPostPart >= 0 && d.model) {   // VH9: the raised pintle under the roof MG, turning with its traverse
    const ModelPart& p = d.model->parts[d.mgPostPart];
    vec3 pc = (p.bmin + p.bmax) * 0.5f, ps = glm::max(p.bmax - p.bmin, vec3(1e-3f));
    float h = std::max(0.02f, d.mgPivot.y - 0.03f - d.mgPostFrom);
    mat4 P = tu * glm::translate(mat4(1), vec3(d.mgPivot.x, d.mgPostFrom + h * 0.5f, d.mgPivot.z)) * glm::rotate(mat4(1), radians(t.mgYaw), vec3(0, 1, 0))
           * glm::scale(mat4(1), vec3(d.mgPostW, h, d.mgPostW) / ps) * glm::translate(mat4(1), -pc);
    app.R.worldModels.push_back({d.model.get(), d.mgPostPart, P, tint});
  }
}

// VH1 car: body, the variant's stowage / pintle, the windshield (folded about its hinge), the steering wheel turning
// about its column with the smoothed steering (steerTurns lock to lock), the front wheels steered (Ackermann: the inner
// wheel more) and every wheel rolling with the ground speed and dropping / rising on its suspension travel
void TankSys::carStaticParts(const TankDef& d, std::vector<int>& out) const {
  out.insert(out.end(), d.hullParts.begin(), d.hullParts.end());
  if (d.stowage) out.insert(out.end(), d.stowageParts.begin(), d.stowageParts.end());
  if (d.pintle) out.insert(out.end(), d.pintleParts.begin(), d.pintleParts.end());
}
void TankSys::drawCar(App& app, const TankRT& t, vec4 tint, const mat4& h) {
  const TankDef& d = *t.def; const GpuModel* gm = lodModel(app, t);
  vehicleDashDraw(app, t, h);   // VH6: the instrument panel seen from the driver's seat
  if (d.partsAsset) {   // BR7 adopted car: its part-built mesh (or its supplied model: car-pack copies) moves as one (the wheels do not turn)
    if (const GpuModel* pm = d.partsAsset->model.get()) {
      float cabinFloor = app.tanks.occupied == t.id ? 0.12f * vehicleCabinDay(app) : 0.0f;   // by day only: the night exposure would turn it pale
      const std::string& id = d.partsAsset->id; int dusty = id.size() > 6 && id.compare(id.size() - 6, 6, "_wreck") == 0 ? 2 : app.tanks.occupied == t.id ? 3 : 1;   // VH6d: clean from the driver's seat
      for (size_t k = 0; k < pm->parts.size(); ++k) {
        if (pm->parts[k].glass) app.R.worldModelsGlass.push_back({pm, (int)k, h, tint, 0.0f, dusty});
        else app.R.worldModels.push_back({pm, (int)k, h, tint, 0.0f, 0, cabinFloor});   // VH6d: the black supplied cabin as dark grey trim by day
      }
    }
    app.worldBoxDraws.push_back({&d.partsAsset->opaque, h, tint});   // shortcut: its glass is not drawn while adopted (the box list is opaque)
    return;
  }
  auto put = [&](const std::vector<int>& ps, const mat4& M) { for (int k : ps) app.R.worldModels.push_back({gm, k, M, tint}); };
  std::vector<int> st; carStaticParts(d, st); put(st, h);
  put(d.windshieldParts, h * glm::translate(mat4(1), d.windshieldPivot) * glm::rotate(mat4(1), radians(d.windshieldFold), vec3(1, 0, 0)) * glm::translate(mat4(1), -d.windshieldPivot));
  float rim = -t.steer * d.steerTurns * 0.5f * 6.2831853f;   // + steer (right) turns the rim clockwise as the driver sees it
  put(d.steerParts, h * glm::translate(mat4(1), d.steerC) * glm::rotate(mat4(1), rim, d.steerAxis) * glm::translate(mat4(1), -d.steerC));
  float delta = std::tan(radians(-t.steer * d.maxSteerDeg)), L = d.carL;
  for (int k = 0; k < d.nWheels; ++k) {
    vec3 c = d.wheelC[k];
    float yaw = 0;
    // turn centre at x = R on the unsteered axle line (8x8: the second axle steers less, by its distance to that line)
    if (TankDef::steeredWheel(k) && std::fabs(delta) > 1e-4f) { float R = L / delta; yaw = std::atan((c.z - d.steerPivotZ) / (R - c.x)); }
    put(d.wheelPart[k], h * glm::translate(mat4(1), c + vec3(0, t.wheelSusp[k], 0)) * glm::rotate(mat4(1), yaw, vec3(0, 1, 0)) *
                            glm::rotate(mat4(1), t.wheelSpin[k], vec3(1, 0, 0)) * glm::translate(mat4(1), -c));
  }
  if (d.pintle && !d.mgParts.empty()) {   // VH2 remote weapon station: the cradle traverses, the gun and its sensors also elevate
    mat4 P = h * glm::translate(mat4(1), d.mgPivot) * glm::rotate(mat4(1), radians(t.mgYaw), vec3(0, 1, 0));
    put(d.rwsParts, P * glm::translate(mat4(1), -d.mgPivot));
    put(d.mgParts, P * glm::rotate(mat4(1), radians(-t.mgElev), vec3(1, 0, 0)) * glm::translate(mat4(1), -d.mgPivot));
  }
  if (d.pintle && !d.mgWeapon.empty()) {
    int wi = app.weaponIndex(d.mgWeapon);
    if (wi >= 0 && app.weaponDefs[wi]->useModel()) {
      const WeaponDef& wd = *app.weaponDefs[wi];
      mat4 W = mgMatrix(t) * glm::translate(mat4(1), d.mgModelOffset);
      for (int p = 0; p < 8; ++p) for (int k : wd.modelParts[p]) app.R.worldModels.push_back({wd.model.get(), k, W, tint});
    }
  }
}

// Creative placement preview: every part of the baked model at the cursor, tinted green (valid) / red (refused)
void TankSys::ghostItems(App& app, const AssetDef* a, const mat4& M, vec4 tint) {
  const TankDef* d = defFor(a);
  if (!d) return;
  if (d->car) {
    std::vector<int> ps; carStaticParts(*d, ps);
    for (int w = 0; w < d->nWheels; ++w) ps.insert(ps.end(), d->wheelPart[w].begin(), d->wheelPart[w].end());
    ps.insert(ps.end(), d->steerParts.begin(), d->steerParts.end());
    if (d->pintle) for (auto* v : {&d->rwsParts, &d->mgParts}) ps.insert(ps.end(), v->begin(), v->end());
    mat4 Wf = M * glm::translate(mat4(1), d->windshieldPivot) * glm::rotate(mat4(1), radians(d->windshieldFold), vec3(1, 0, 0)) * glm::translate(mat4(1), -d->windshieldPivot);
    for (int k : ps) app.R.worldModels.push_back({d->model.get(), k, M, tint});
    for (int k : d->windshieldParts) app.R.worldModels.push_back({d->model.get(), k, Wf, tint});
    return;
  }
  for (size_t k = 0; k < d->model->parts.size(); ++k) app.R.worldModels.push_back({d->model.get(), (int)k, M, tint});
}

// ============================================================ thumbnails (catalog)
void TankSys::makeThumbnails(App& app) {
  for (auto& a : app.lib.assets) {
    const TankDef* d = defFor(a.get());
    if (!d) continue;
    std::vector<ModelDraw> md;
    auto add = [&](const std::vector<int>& ps) { for (int k : ps) md.push_back({d->model.get(), k, mat4(1)}); };
    add(d->hullParts); add(d->turretParts); add(d->gunParts); add(d->mgParts); add(d->hatchParts);
    for (int s = 0; s < 2; ++s) { add(d->trackParts[s]); add(d->wheelParts[s]); }
    if (d->car) {   // VH1: the variant's parts, its windshield folded as placed
      std::vector<int> ps; carStaticParts(*d, ps); md.clear(); add(ps); add(d->steerParts);
      for (int w = 0; w < d->nWheels; ++w) add(d->wheelPart[w]);
      if (d->pintle) { add(d->rwsParts); add(d->mgParts); }
      mat4 Wf = glm::translate(mat4(1), d->windshieldPivot) * glm::rotate(mat4(1), radians(d->windshieldFold), vec3(1, 0, 0)) * glm::translate(mat4(1), -d->windshieldPivot);
      for (int k : d->windshieldParts) md.push_back({d->model.get(), k, Wf});
    }
    vec3 c = (d->model->bmin + d->model->bmax) * 0.5f; c.y = d->car ? 0.8f : 1.4f;
    float R = glm::length(vec2(d->model->bmax.x - d->model->bmin.x, d->model->bmax.z - d->model->bmin.z)) * 0.5f;
    vec3 eye = c + glm::normalize(vec3(-1.0f, 0.55f, 1.1f)) * R * 2.3f;
    mat4 view = glm::lookAt(eye, c, vec3(0, 1, 0)), proj = glm::perspective(radians(30.0f), 1.0f, 0.1f, 100.0f);
    const int S = 256;
    std::vector<uint8_t> px;
    if (!app.R.renderModelSheet(md, {}, view, proj, eye, S, S, px)) continue;
    std::vector<uint8_t> flip(px.size());
    for (int y = 0; y < S; ++y) std::memcpy(&flip[(size_t)y * S * 4], &px[(size_t)(S - 1 - y) * S * 4], (size_t)S * 4);
    GLuint tex; glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, S, S);
    glTextureSubImage2D(tex, 0, 0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, flip.data());
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (a->thumbnail) glDeleteTextures(1, &a->thumbnail);
    a->thumbnail = tex;
  }
}

// ============================================================ HUD
// TK1: the vehicle's durability bar (every vehicle, any seat), bottom left: 15 segments (a heavy's blow each), the number,
// red when low; it flashes white on a damaging hit and reads DESTROYED once wrecked. Drawn after drawUI (over the sight mask)
void TankSys::grassBoxes(const App& app, vec3 cam, std::vector<vec4>& out) const {
  out.clear();
  std::vector<std::pair<float, const TankRT*>> near;
  vec3 eye = cam;   // the rendering camera (Creative or Explore)
  for (auto& [id, t] : tanks) {
    if (!t.def || t.def->hullBoxes.empty()) continue;
    if (t.def->heli && t.pos.y - app.terrain.height(t.pos.x, t.pos.z) > 1.0f) continue;   // a helicopter only on the ground
    float d = glm::length(vec2(t.pos.x - eye.x, t.pos.z - eye.z)); if (d < 160.0f) near.push_back({d, &t});
  }
  std::sort(near.begin(), near.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (size_t k = 0; k < near.size() && k < 16; ++k) {
    const TankRT& t = *near[k].second; float hx = 0, lo = 1e9f, hi = -1e9f;
    for (auto& b : t.def->hullBoxes) { hx = std::max(hx, std::fabs(b.c.x) + b.h.x); lo = std::min(lo, b.c.z - b.h.z); hi = std::max(hi, b.c.z + b.h.z); }
    float hz = 0.5f * (hi - lo), cz = 0.5f * (hi + lo), a = radians(t.yaw), c = std::cos(a), s = std::sin(a);
    vec2 ctr = vec2(t.pos.x, t.pos.z) + vec2(s, c) * cz;   // hull +z is world (sin, cos)
    out.push_back(vec4(ctr.x, ctr.y, c, s)); out.push_back(vec4(hx + 0.12f, hz + 0.12f, 0, 0));   // the shader takes world -> hull with (cos, sin)
  }
}

void TankSys::drawHealthBar(App& app) {
  (void)app;
  TankRT* tp = occupied ? get(occupied) : nullptr;
  if (!tp) return;
  TankRT& t = *tp; const TankDef& d = *t.def;
  ImGuiIO& io = ImGui::GetIO(); ImDrawList* dl = ImGui::GetForegroundDrawList();
  {
    t.hitFlash = std::max(0.0f, t.hitFlash - io.DeltaTime / 0.5f);
    const float W = 240, H = 14; ImVec2 o(30, io.DisplaySize.y - (d.car || d.heli ? 60 : 230));
    float u = std::clamp(t.hp / 100.0f, 0.0f, 1.0f), fl = t.hitFlash * t.hitFlash;
    ImU32 fill = t.wrecked ? IM_COL32(90, 30, 25, 230) : u > 0.5f ? IM_COL32(120, 200, 110, 235) : u > 0.25f ? IM_COL32(235, 190, 80, 240) : IM_COL32(235, 90, 70, 245);
    dl->AddRectFilled(ImVec2(o.x - 6, o.y - 22), ImVec2(o.x + W + 6, o.y + H + 6), IM_COL32(12, 12, 10, 190), 4);
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), IM_COL32(40, 38, 34, 230));
    dl->AddRectFilled(o, ImVec2(o.x + W * u, o.y + H), fill);
    if (fl > 0.01f) dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), IM_COL32(255, 250, 235, (int)(170 * fl)));
    for (int k = 1; k < 15; ++k) { float x = o.x + W * k / 15.0f; dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), IM_COL32(12, 12, 10, 200), 1); }
    char hb[64]; std::snprintf(hb, sizeof hb, t.wrecked ? "%s  DESTROYED" : "%s  %d / 100", d.car ? "VEHICLE" : d.heli ? "AIRFRAME" : "HULL", t.hp);
    dl->AddText(ImVec2(o.x, o.y - 18), t.wrecked || u <= 0.25f ? IM_COL32(255, 130, 110, 255) : IM_COL32(235, 225, 190, 255), hb);
  }
}

void TankSys::drawWorldBars(App& app) {
  if (!settings().s.vehicleBars || app.pauseMenu) return;
  ImGuiIO& io = ImGui::GetIO(); ImDrawList* dl = ImGui::GetBackgroundDrawList();   // under the HUD panels
  const mat4 vp = app.projM * app.viewM; const vec3 eye = app.eyePos();
  for (auto& [id, t] : tanks) {
    if (!t.def || t.wrecked || id == occupied) continue;   // the occupied one has the HUD bar; wrecks none
    float dist = glm::length(t.pos - eye);
    if (dist > 95.0f) continue;
    float top = 0; for (auto* bs : {&t.def->hullBoxes, &t.def->turretBoxes}) for (auto& b : *bs) top = std::max(top, b.c.y + b.h.y);
    vec3 p = t.pos + vec3(0, top + 0.7f, 0);
    vec4 q = vp * vec4(p, 1); if (q.w <= 0.1f) continue;
    ImVec2 s((q.x / q.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - q.y / q.w * 0.5f) * io.DisplaySize.y);
    if (s.x < -50 || s.y < -20 || s.x > io.DisplaySize.x + 50 || s.y > io.DisplaySize.y + 20) continue;
    Physics::Hit h;   // hidden behind something solid (its own body does not count)
    if (Physics::castRayEx(eye, p - eye, dist - 0.3f, h, id << 16)) continue;
    float a = std::clamp((95.0f - dist) / 20.0f, 0.0f, 1.0f), W = std::clamp(1400.0f / std::max(dist, 1.0f), 34.0f, 90.0f), H = std::max(4.0f, W * 0.09f);
    float u = std::clamp(t.hp / 100.0f, 0.0f, 1.0f);
    ImU32 fill = u > 0.5f ? IM_COL32(120, 200, 110, (int)(230 * a)) : u > 0.25f ? IM_COL32(235, 190, 80, (int)(235 * a)) : IM_COL32(235, 90, 70, (int)(240 * a));
    ImVec2 o(s.x - W * 0.5f, s.y - H * 0.5f);
    dl->AddRectFilled(ImVec2(o.x - 2, o.y - 2), ImVec2(o.x + W + 2, o.y + H + 2), IM_COL32(10, 10, 8, (int)(170 * a)), 2);
    dl->AddRectFilled(o, ImVec2(o.x + W * u, o.y + H), fill);
  }
}

void TankSys::drawUI(App& app) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  auto panel = [&](ImVec2 p, const char* txt, ImU32 col) {
    ImVec2 ts = ImGui::CalcTextSize(txt);
    dl->AddRectFilled(ImVec2(p.x - 10, p.y - 6), ImVec2(p.x + ts.x + 10, p.y + ts.y + 6), IM_COL32(12, 12, 10, 200), 4);
    dl->AddText(p, col, txt);
  };
  if (!occupied) {
    if (target && !app.pauseMenu) {
      TankRT* t = get(target);
      if (t) {
        std::string s = t->wrecked ? t->def->name + " - destroyed" : "E  -  Enter " + t->def->name;
        ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        panel(ImVec2(c.x - ts.x * 0.5f, c.y + 28), s.c_str(), IM_COL32(245, 238, 210, 255));
      }
    }
    return;
  }
  TankRT* tp = get(occupied);
  if (!tp) return;
  TankRT& t = *tp; const TankDef& d = *t.def;
  if (d.car) { drawCarUI(app, t); return; }
  if (d.heli) { drawHeliUI(app, t); return; }   // VH5
  vec3 eye, fwd; float fov;
  camera(app, eye, fwd, fov);
  float fpx = (io.DisplaySize.y * 0.5f) / std::tan(radians(fov) * 0.5f);   // pixels per unit tangent (vertical FOV)
  mat4 vpm = app.projM * app.viewM;
  auto project = [&](vec3 w, ImVec2& out) {
    vec4 q = vpm * vec4(w, 1);
    if (q.w < 0.1f) return false;
    out = ImVec2((q.x / q.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - q.y / q.w * 0.5f) * io.DisplaySize.y);
    return true;
  };
  auto milPx = [&](float mil) { return fpx * std::tan(mil / 1000.0f); };
  if (t.hatchT < 0.5f) {
    // ---- gunner sight: circular field, dark surround, original reticle (docs/tanks.md#optics). VH7: the Abrams' is a
    // full-screen rectangular view with a soft vignette; the Bradley's a round eyepiece (80 % of the height) with a red reticle
    const bool rectSight = d.reticle == "abrams";
    const float W = io.DisplaySize.x, Hs = io.DisplaySize.y;
    float R = Hs * (d.reticle == "bradley" ? 0.40f : 0.45f);
    if (rectSight) R = std::max(W, Hs);   // TK3: its vignette is in the tone pass (RenderView::rectVignette)
    for (int k = 0; k < 72 && !rectSight; ++k) {
      float a0 = k * 6.2831853f / 72, a1 = (k + 1) * 6.2831853f / 72;
      ImVec2 p0(c.x + std::cos(a0) * R, c.y + std::sin(a0) * R), p1(c.x + std::cos(a1) * R, c.y + std::sin(a1) * R);
      ImVec2 q0(c.x + std::cos(a0) * io.DisplaySize.x, c.y + std::sin(a0) * io.DisplaySize.x), q1(c.x + std::cos(a1) * io.DisplaySize.x, c.y + std::sin(a1) * io.DisplaySize.x);
      dl->AddQuadFilled(p0, q0, q1, p1, IM_COL32(5, 5, 4, 255));
    }
    if (!rectSight) dl->AddCircle(c, R, IM_COL32(25, 24, 20, 255), 128, 5);
    ImU32 rc = IM_COL32(12, 10, 8, 240), lc = IM_COL32(20, 18, 14, 230);
    // range marks calibrated to this game's ballistics (no drag): the bore is superelevated for the zero range; the
    // point at range R sits (a(R) - a(zero)) below the aiming mark, a(R) = atan(g R / 2 v^2)
    auto rangeOff = [&](float v, float Rm) { float a = std::atan(9.81f * Rm / (2 * v * v)), a0 = std::atan(9.81f * d.zeroRange / (2 * v * v)); return fpx * std::tan(a - a0); };
    auto scale = [&](float v, float x, const char* lab, bool right) {
      ImVec2 ls = ImGui::CalcTextSize(lab);
      dl->AddText(ImVec2(right ? x + 4 : x - ls.x - 4, c.y - ls.y - milPx(1.0f) - 4), lc, lab);
      float lastLabel = -1e9f;
      for (int Rm = 400; Rm <= 2400; Rm += 200) {
        float y = c.y + rangeOff(v, (float)Rm);
        if (y > c.y + R * 0.9f) break;
        bool major = Rm % 400 == 0;
        float w = major ? 9.0f : 5.0f;
        dl->AddLine(ImVec2(x - (right ? 0 : w), y), ImVec2(x + (right ? w : 0), y), rc, 1.4f);
        if (major && Rm > 400 && y - lastLabel >= 14.0f) { char b[8]; std::snprintf(b, sizeof b, "%d", Rm / 100); dl->AddText(ImVec2(right ? x + 12 : x - 12 - ImGui::CalcTextSize(b).x, y - 7), lc, b); lastLabel = y; }
      }
    };
    ImFont* font = ImGui::GetFont();
    const float lased = glm::length(aimPoint(app, t) - eye);
    if (d.reticle == "abrams") {   // VH7: the user's reference layout, our own drawing: full-width / full-height black lines,
      // a small centre circle, a mil scale (ticks every 4, numbered 8 .. 32), a range ladder below, a range-bar block that
      // slides with the lased range, the range read out large above the centre and as "Distance:" at upper left
      ImU32 bk = IM_COL32(12, 12, 12, 235), bks = IM_COL32(12, 12, 12, 170);
      dl->AddLine(ImVec2(0, c.y), ImVec2(W, c.y), bk, 1.3f); dl->AddLine(ImVec2(c.x, 0), ImVec2(c.x, Hs), bk, 1.3f);
      dl->AddCircle(c, milPx(1.3f), bk, 24, 1.3f);
      const float U = W * 0.44f / 32.0f;   // one scale unit: 32 sits 44 % of the width out (the reference's spread)
      for (int k = 1; k <= 8; ++k) {
        float x = U * 4.0f * k, h = k % 2 == 0 ? 40.0f : 22.0f;
        for (float sx : {-1.0f, 1.0f}) {
          dl->AddLine(ImVec2(c.x + sx * x, c.y - h), ImVec2(c.x + sx * x, c.y + h), bk, 1.3f);
          if (k % 2 == 0) { char n[8]; std::snprintf(n, sizeof n, "%d", k * 4); ImVec2 ns = font->CalcTextSizeA(30, FLT_MAX, 0, n); dl->AddText(font, 30, ImVec2(c.x + sx * x - ns.x * 0.5f, c.y - h - ns.y - 6), bk, n); }
        }
      }
      for (int k = 1; k <= 22; ++k) { float y = c.y + 9.0f * k, w = k % 3 == 0 ? 16.0f : 10.0f; dl->AddLine(ImVec2(c.x - w, y), ImVec2(c.x + w, y), bks, 1.2f); }   // the ladder
      {   // range bars: hectometres 4 .. 56, positioned by the AP drop relative to the lased range (the block slides)
        float x0 = c.x - U * 21.0f, x1 = c.x - U * 15.0f;   // left of the centre, clear of the dial
        auto dropPx = [&](float Rm) { return Rm / 100.0f * 8.0f; };   // 8 px a hectometre: the block slides with the lased range
        float base = dropPx(std::clamp(lased, 0.0f, 6000.0f));
        static const int lab[] = {4, 16, 28, 40, 48, 56};
        for (int hm = 4; hm <= 56; hm += 2) {
          float y = c.y + dropPx(hm * 100.0f) - base + milPx(1.0f);
          if (y < c.y - milPx(2) || y > Hs * 0.9f) continue;
          bool L = std::find(std::begin(lab), std::end(lab), hm) != std::end(lab);
          dl->AddLine(ImVec2(L ? x0 - 18 : x0, y), ImVec2(x1, y), bk, L ? 1.6f : 1.1f);
          if (L) { char n[8]; std::snprintf(n, sizeof n, "%d", hm); ImVec2 ns = font->CalcTextSizeA(30, FLT_MAX, 0, n); dl->AddText(font, 30, ImVec2(x0 - 24 - ns.x, y - ns.y * 0.5f), bk, n); }
        }
      }
      char rb[32]; std::snprintf(rb, sizeof rb, "%d", (int)std::min(lased, 9999.0f));
      dl->AddText(font, 46, ImVec2(c.x + 12, c.y - Hs * 0.40f), bk, rb);
      char db[48]; std::snprintf(db, sizeof db, "Distance: %d", (int)std::min(lased, 9999.0f));
      dl->AddText(font, 30, ImVec2(15, Hs * 0.26f + 1), IM_COL32(230, 225, 210, 150), db); dl->AddText(font, 30, ImVec2(14, Hs * 0.26f), bk, db);   // a light shadow over the vignette
      {   // bottom left: the orientation dial (hull, turret line), STAB, gear, RPM, speed
        ImVec2 o(120, Hs - 400); float r = 95;   // clear of the durability bar
        dl->AddCircleFilled(o, r, IM_COL32(20, 34, 54, 150), 48); dl->AddCircle(o, r, IM_COL32(170, 190, 210, 200), 48, 2); dl->AddCircle(o, r * 0.72f, IM_COL32(170, 190, 210, 120), 48, 1.5f);
        dl->AddText(ImVec2(o.x - 18, o.y - r + 8), IM_COL32(230, 235, 240, 255), "STAB");
        dl->AddRect(ImVec2(o.x - 18, o.y - 42), ImVec2(o.x + 18, o.y + 42), IM_COL32(225, 205, 120, 235), 3, 0, 2);
        float ta = radians(t.tYaw); dl->AddCircle(o, 11, IM_COL32(200, 215, 255, 240), 16, 2);
        dl->AddLine(o, ImVec2(o.x - std::sin(ta) * 60, o.y - std::cos(ta) * 60), IM_COL32(200, 215, 255, 255), 3);
        char gb[96]; std::snprintf(gb, sizeof gb, "GEAR %s\nRPM %d\nSPD %d km/h", t.speed < -0.2f ? "R" : std::fabs(t.speed) < 0.2f ? "N" : "D", (int)(t.rpm * 2600), (int)std::fabs(t.speed * 3.6f));
        dl->AddText(ImVec2(o.x - r + 6, o.y + r + 6), IM_COL32(230, 235, 240, 255), gb);
      }
    } else if (d.reticle == "bradley") {   // VH7: the round red sight (our own drawing of the user's description)
      ImU32 rd = IM_COL32(235, 40, 30, 235), rdd = IM_COL32(235, 40, 30, 160);
      dl->AddCircle(c, milPx(1.6f), rd, 24, 1.4f); dl->AddCircleFilled(c, 1.8f, rd);
      for (int row = 0; row < 2; ++row) for (int k = 1; k <= 5; ++k) for (float sx : {-1.0f, 1.0f}) {   // two rows of lead / stadia dashes
        float x = c.x + sx * milPx(3.0f + 4.0f * k), y = c.y + row * milPx(3.0f);
        dl->AddLine(ImVec2(x - milPx(1.0f), y), ImVec2(x + milPx(1.0f), y), row ? rdd : rd, 1.4f);
      }
      for (float sx : {-1.0f, 1.0f}) dl->AddLine(ImVec2(c.x + sx * milPx(7.0f), c.y - R * 0.92f), ImVec2(c.x + sx * milPx(2.4f), c.y - milPx(4.0f)), rd, 1.4f);   // the converging verticals
      float by = c.y + R * 0.52f;
      dl->AddLine(ImVec2(c.x - R * 0.62f, by), ImVec2(c.x + R * 0.18f, by), rd, 1.4f);
      dl->AddLine(ImVec2(c.x + R * 0.18f, by), ImVec2(c.x + R * 0.66f, by - R * 0.24f), rd, 1.4f);   // the range line
      for (int k = 1; k <= 4; ++k) { float u = k / 5.0f; ImVec2 p0(c.x + R * (0.18f + 0.48f * u), by - R * 0.24f * u); dl->AddLine(p0, ImVec2(p0.x, p0.y + 8), rdd, 1.2f); }
    } else if (d.reticle == "tiger") {
      // central triangle with its apex on the aim point, three smaller triangles each side every 4 mil (lead marks)
      auto tri = [&](float x, float s) { dl->AddTriangle(ImVec2(x, c.y), ImVec2(x - s * 0.55f, c.y + s * 1.4f), ImVec2(x + s * 0.55f, c.y + s * 1.4f), rc, 1.6f); };
      tri(c.x, milPx(4.0f));
      for (int k = 1; k <= 3; ++k) { tri(c.x - milPx(4.0f * k), milPx(2.0f)); tri(c.x + milPx(4.0f * k), milPx(2.0f)); }
      dl->AddLine(ImVec2(c.x - R, c.y + milPx(6)), ImVec2(c.x - milPx(14), c.y + milPx(6)), rc, 1.2f);
      dl->AddLine(ImVec2(c.x + milPx(14), c.y + milPx(6)), ImVec2(c.x + R, c.y + milPx(6)), rc, 1.2f);
      scale(d.velAP, c.x - R * 0.55f, "PzGr", false);
      scale(d.velHE, c.x + R * 0.55f, "Sprgr", true);
    } else {
      // vertical post from below with a gap, small open chevron at the aim point, horizontal line with lead ticks
      dl->AddLine(ImVec2(c.x, c.y + milPx(3)), ImVec2(c.x, c.y + R), rc, 1.6f);
      dl->AddLine(ImVec2(c.x - milPx(1.5f), c.y + milPx(1.6f)), ImVec2(c.x, c.y), rc, 1.6f);
      dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x + milPx(1.5f), c.y + milPx(1.6f)), rc, 1.6f);
      dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x - milPx(3), c.y), rc, 1.4f);
      dl->AddLine(ImVec2(c.x + milPx(3), c.y), ImVec2(c.x + R, c.y), rc, 1.4f);
      for (int k = 1; k <= 8; ++k) {
        float x = milPx(4.0f * k), hgt = k % 2 == 0 ? milPx(1.6f) : milPx(0.9f);
        dl->AddLine(ImVec2(c.x - x, c.y), ImVec2(c.x - x, c.y - hgt), rc, 1.3f);
        dl->AddLine(ImVec2(c.x + x, c.y), ImVec2(c.x + x, c.y - hgt), rc, 1.3f);
      }
      scale(d.velAP, c.x - milPx(2), "AP", false);
      scale(d.velHE, c.x + milPx(2), "HE", true);
    }
    // desired aim (mouse) vs the actual gun line: a hollow ring where the turret is being driven to
    vec3 want = App::forward(app.eyeYaw, app.eyePitch);
    mat4 vp = app.projM * app.viewM;
    vec4 cc = vp * vec4(eye + want * 1000.0f, 1);
    if (cc.w > 0) {
      ImVec2 s((cc.x / cc.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - cc.y / cc.w * 0.5f) * io.DisplaySize.y);
      float dist = std::hypot(s.x - c.x, s.y - c.y);
      if (dist > 3) {
        if (dist > R - 12) { s.x = c.x + (s.x - c.x) / dist * (R - 12); s.y = c.y + (s.y - c.y) / dist * (R - 12); }
        dl->AddCircle(s, 9, IM_COL32(230, 180, 60, 220), 20, 2);
      }
    }
    if (d.reticle == "bradley") {   // VH7 read-out panel: selected round, its count, the other type's, the heading; the hull /
      // turret icon at the left; the weapon at the lower right; TOWs ready
      ImU32 rd = IM_COL32(255, 45, 35, 255);
      bool he = (t.loaded != SHELL_NONE ? t.loaded : t.selected) == SHELL_HE;
      int sel = he ? t.reserveHE : t.reserveAP, oth = he ? t.reserveAP : t.reserveHE;
      if (t.loaded != SHELL_NONE) { if ((t.loaded == SHELL_HE) == he) ++sel; else ++oth; }
      vec3 gd; muzzleWorld(t, gd); int hdg = ((int)std::lround(degrees(std::atan2(gd.x, -gd.z))) + 360) % 360;
      ImVec2 p0(c.x - 210, c.y + R * 0.80f), p1(c.x + 210, c.y + R * 0.80f + 58);
      dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 245), 4);
      char rb[64];
      dl->AddText(font, 34, ImVec2(p0.x + 14, p0.y + 11), rd, he ? "HE" : "AP");
      std::snprintf(rb, sizeof rb, "%04d", sel); dl->AddText(font, 40, ImVec2(p0.x + 78, p0.y + 7), rd, rb);
      std::snprintf(rb, sizeof rb, "%03d", oth); dl->AddText(font, 26, ImVec2(p0.x + 200, p0.y + 16), IM_COL32(255, 45, 35, 200), rb);
      std::snprintf(rb, sizeof rb, "%03d", hdg); dl->AddText(font, 22, ImVec2(p0.x + 278, p0.y + 19), IM_COL32(255, 45, 35, 200), rb);
      std::snprintf(rb, sizeof rb, "TOW %d", t.towLeft); dl->AddText(font, 22, ImVec2(p0.x + 334, p0.y + 19), t.towLeft ? rd : IM_COL32(150, 40, 30, 200), rb);
      ImVec2 hi(c.x - R * 0.80f, c.y + R * 0.40f);   // the hull / turret icon
      dl->AddRect(ImVec2(hi.x - 9, hi.y - 18), ImVec2(hi.x + 9, hi.y + 18), rd, 2, 0, 1.6f);
      float ta = radians(t.tYaw); dl->AddLine(hi, ImVec2(hi.x - std::sin(ta) * 26, hi.y - std::cos(ta) * 26), rd, 2);
      dl->AddText(font, 20, ImVec2(c.x + R * 0.52f, c.y + R * 0.62f), rd, "25mm");
      if (zoom) dl->AddText(font, 20, ImVec2(c.x + R * 0.62f, c.y - R * 0.72f), rd, "NFOV");
    } else if (!rectSight) {
      char b[96]; std::snprintf(b, sizeof b, "%s  %s", zoom ? "ZOOM" : "SIGHT", d.cannonName.c_str());
      dl->AddText(ImVec2(c.x - R * 0.62f, c.y - R * 0.78f), IM_COL32(205, 195, 160, 220), b);
    }
  } else {
    // hatch: open-sight crosshair for the roof MG (it converges on the aim point)
    dl->AddCircle(c, 7, IM_COL32(240, 240, 230, 210), 20, 1.5f);
    dl->AddCircleFilled(c, 1.8f, IM_COL32(240, 240, 230, 220));
    // where the main gun is laid (RMB fires it): a gun marker on its line at the aim distance
    vec3 gd; vec3 gm = muzzleWorld(t, gd);
    float gdist = std::max(30.0f, glm::length(aimPoint(app, t) - gm));
    ImVec2 gs;
    if (project(gm + gd * gdist, gs)) {
      ImU32 gc = t.loaded != SHELL_NONE ? IM_COL32(240, 200, 80, 235) : IM_COL32(170, 150, 110, 180);
      dl->AddTriangle(ImVec2(gs.x, gs.y - 9), ImVec2(gs.x - 8, gs.y + 6), ImVec2(gs.x + 8, gs.y + 6), gc, 2);
      dl->AddText(ImVec2(gs.x + 11, gs.y - 7), gc, t.loaded != SHELL_NONE ? "RMB" : "...");
    }
  }
  // bow MG: a small diamond where the ball mount points (Space), in both stations
  if (d.bowOn) {
    vec3 bd; vec3 bm = bowMuzzleWorld(t, bd);
    float bdist = std::max(20.0f, glm::length(aimPoint(app, t) - bm));
    ImVec2 bs;
    if (project(bm + bd * bdist, bs)) {
      ImU32 bc = t.bowReloadT > 0 ? IM_COL32(150, 170, 190, 150) : IM_COL32(140, 210, 255, 230);
      dl->AddQuad(ImVec2(bs.x, bs.y - 6), ImVec2(bs.x + 6, bs.y), ImVec2(bs.x, bs.y + 6), ImVec2(bs.x - 6, bs.y), bc, 1.6f);
    }
  }
  // ---- status panel (ammunition: the strip at the bottom)
  // VH8: the keys this vehicle really has (no bow MG on the Abrams / M4, no roof gun on the Bradley)
  const std::string bowK = d.bowOn ? std::string(" | Space ") + (d.bowCoax ? "coax" : "bow MG") : std::string();
  std::string keys;
  if (t.station == TS_HATCH) keys = d.mgName.empty() ? "Head out: look around | RMB main gun" + bowK + " | Ctrl duck back in | E get out"
                                                     : std::string("LMB roof MG | Space roof") + (d.bowOn ? " + bow" : "") + " MG | RMB main gun | Ctrl back to the sight | R next shell | E get out";
  else if (d.towCount) keys = "LMB 25 mm (hold) | R AP / HE" + bowK + " | Q TOW | RMB zoom | Ctrl pop out of the hatch | E get out";
  else keys = "LMB main gun" + bowK + " | RMB zoom | Ctrl open hatch | R next shell | E get out";
  char b[480];
  std::snprintf(b, sizeof b,
    "%s  [%s]\nSPEED %3.0f km/h   TURRET %+4.0f   GUN %+4.1f\nDURABILITY %d/100   (front hits %d, side/rear hits %d)\n%s",
    d.name.c_str(), t.station == TS_HATCH ? "HATCH" : "GUNNER",
    std::fabs(t.speed) * 3.6f, t.tYaw, t.gElev, t.hp, t.hitsFront, t.hitsSide, keys.c_str());
  ImVec2 ts = ImGui::CalcTextSize(b);
  ImVec2 p(io.DisplaySize.x - ts.x - 30, io.DisplaySize.y - ts.y - 30 - 108);   // above the ammunition strip
  panel(p, b, t.hp <= 50 ? IM_COL32(255, 170, 120, 255) : IM_COL32(235, 225, 190, 255));
  drawAmmoStrip(app, t);
  // ---- pushing an obstacle: how much of its break energy has gone in (hold W)
  if (t.ramKey && t.ramNeed > 0) {
    float u = std::clamp(t.ramWork / t.ramNeed, 0.0f, 1.0f);
    ImVec2 o2(170, io.DisplaySize.y - 150);
    char rb[120]; std::snprintf(rb, sizeof rb, "PUSHING %s  %.0f%%  (hold W)", t.ramName.c_str(), u * 100);
    dl->AddText(o2, IM_COL32(255, 210, 140, 255), rb);
    dl->AddRectFilled(ImVec2(o2.x, o2.y + 18), ImVec2(o2.x + 220, o2.y + 26), IM_COL32(40, 38, 34, 220));
    dl->AddRectFilled(ImVec2(o2.x, o2.y + 18), ImVec2(o2.x + 220 * u, o2.y + 26), IM_COL32(230, 160, 70, 240));
  }
  // ---- hull / turret orientation (top view: hull up the screen, turret line at the achieved traverse)
  ImVec2 o(90, io.DisplaySize.y - 110);
  float hl = 34, hw = 18;
  dl->AddRect(ImVec2(o.x - hw, o.y - hl), ImVec2(o.x + hw, o.y + hl), IM_COL32(200, 200, 180, 220), 3, 0, 2);
  dl->AddCircle(o, 12, IM_COL32(200, 200, 180, 220), 20, 2);
  float a = radians(t.tYaw);
  dl->AddLine(o, ImVec2(o.x - std::sin(a) * 46, o.y - std::cos(a) * 46), IM_COL32(240, 210, 120, 255), 3);
  if (t.station == TS_HATCH) { float m = radians(t.tYaw + t.mgYaw); dl->AddLine(o, ImVec2(o.x - std::sin(m) * 30, o.y - std::cos(m) * 30), IM_COL32(150, 220, 255, 255), 2); }
  if (!t.blockedBy.empty()) dl->AddText(ImVec2(o.x - 60, o.y + hl + 10), IM_COL32(255, 140, 110, 255), ("blocked: " + t.blockedBy).c_str());
}

// VH1 car HUD: speed, gear, handbrake, durability and the controls (the cab itself is the view); at the pintle MG an
// open crosshair and the belt
void TankSys::drawCarUI(App& app, TankRT& t) {
  (void)app;
  const TankDef& d = *t.def;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  bool gun = t.station == TS_HATCH && t.hatchT >= 1.0f;
  if (gun && d.rwsSight) {   // VH2 remote weapon station sight: dark surround, crosshair with a centre gap, stadia ticks, zoom readout
    float W = io.DisplaySize.x, Hh = io.DisplaySize.y, r = Hh * 0.46f;
    ImU32 blk = IM_COL32(0, 0, 0, 255), ln = IM_COL32(40, 255, 120, 230), dim = IM_COL32(40, 255, 120, 140);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(c.x - r * 1.25f, Hh), blk); dl->AddRectFilled(ImVec2(c.x + r * 1.25f, 0), ImVec2(W, Hh), blk);
    dl->AddRect(ImVec2(c.x - r * 1.25f, c.y - r), ImVec2(c.x + r * 1.25f, c.y + r), dim, 0, 0, 2);
    float g = 14;
    dl->AddLine(ImVec2(c.x - r * 1.2f, c.y), ImVec2(c.x - g, c.y), ln, 1.5f); dl->AddLine(ImVec2(c.x + g, c.y), ImVec2(c.x + r * 1.2f, c.y), ln, 1.5f);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.95f), ImVec2(c.x, c.y - g), ln, 1.5f); dl->AddLine(ImVec2(c.x, c.y + g), ImVec2(c.x, c.y + r * 0.95f), ln, 1.5f);
    dl->AddCircleFilled(c, 1.6f, ln);
    for (int k = 1; k <= 4; ++k) {   // drop ticks below the centre and mil ticks across
      float y = c.y + g + k * r * 0.07f; dl->AddLine(ImVec2(c.x - 10 + k, y), ImVec2(c.x + 10 - k, y), ln, 1.2f);
      float x = r * 0.12f * k; for (float sx : {-1.0f, 1.0f}) dl->AddLine(ImVec2(c.x + sx * x, c.y - 6), ImVec2(c.x + sx * x, c.y + 6), dim, 1.2f);
    }
    char z[96]; float fovNow = glm::mix(d.rwsFov, d.rwsFovZoom, zoomT * zoomT * (3 - 2 * zoomT));
    std::snprintf(z, sizeof z, "RWS  %s  x%.1f   AZ %+04.0f  EL %+03.0f", zoom ? "NFOV" : "WFOV", 75.0f / fovNow, t.mgYaw, t.mgElev);
    dl->AddText(ImVec2(c.x - r * 1.2f + 10, c.y - r + 8), ln, z);
  }
  else if (gun) { dl->AddCircle(c, 7, IM_COL32(240, 240, 230, 210), 20, 1.5f); dl->AddCircleFilled(c, 1.8f, IM_COL32(240, 240, 230, 220)); }
  char b[400];
  const char* gear = t.speed < -0.2f ? "R" : std::fabs(t.speed) < 0.2f ? "N" : "D";
  if (gun) std::snprintf(b, sizeof b, "%s  [%s]\n%s  belt %d  spare %d%s\nDURABILITY %d/100\nLMB / Space fire%s | Ctrl back to the %s | E get out",
                         d.name.c_str(), d.rwsSight ? "GUNNER - RWS" : "PINTLE MG", d.mgName.c_str(), t.mgLoaded, t.mgReserve, t.mgReloadT > 0 ? "  (changing belt)" : "", t.hp,
                         d.rwsSight ? " | RMB zoom" : "", d.steerParts.empty() ? "driver" : "wheel");
  else std::snprintf(b, sizeof b, "%s\nSPEED %3.0f km/h   %s%s\nDURABILITY %d/100\nW/S throttle-brake-reverse | A/D steer | Space handbrake%s | E get out",
                     d.name.c_str(), std::fabs(t.speed) * 3.6f, gear, t.handbrake ? "   HANDBRAKE" : "", t.hp, d.carGun() ? (d.rwsSight ? " | Ctrl gunner" : " | Ctrl MG") : "");
  ImVec2 ts = ImGui::CalcTextSize(b);
  ImVec2 p(io.DisplaySize.x - ts.x - 30, io.DisplaySize.y - ts.y - 30);
  dl->AddRectFilled(ImVec2(p.x - 10, p.y - 6), ImVec2(p.x + ts.x + 10, p.y + ts.y + 6), IM_COL32(12, 12, 10, 170), 4);
  dl->AddText(p, t.hp <= 50 ? IM_COL32(255, 170, 120, 255) : IM_COL32(235, 225, 190, 255), b);
  if (!t.blockedBy.empty()) dl->AddText(ImVec2(p.x, p.y - 26), IM_COL32(255, 140, 110, 255), ("blocked: " + t.blockedBy).c_str());
}

// ============================================================ ammunition strip (bottom centre)
// Main gun: AP / HE stowage with shell icons, the chambered round and the loader's progress; bow MG and roof MG: the
// belt in the gun drawn round by round, spare rounds and the belt change in progress. The active controls are named
// on each group and the weapons usable from the current station are lit.
void TankSys::drawAmmoStrip(App& app, const TankRT& t) {
  (void)app;
  const TankDef& d = *t.def;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  bool hatch = t.station == TS_HATCH;
  const float H = 86, pad = 12;
  ImU32 txt = IM_COL32(235, 225, 190, 255), dim = IM_COL32(150, 145, 125, 200), hot = IM_COL32(255, 205, 90, 255);
  // a shell, standing: brass case, painted projectile (AP black, HE olive with a yellow band)
  auto shell = [&](float x, float yb, float h, bool ap, bool lit) {
    float w = h * 0.3f;
    ImU32 cs = lit ? IM_COL32(200, 160, 72, 255) : IM_COL32(92, 82, 56, 200);
    ImU32 pr = ap ? (lit ? IM_COL32(38, 38, 36, 255) : IM_COL32(50, 50, 46, 170)) : (lit ? IM_COL32(112, 120, 72, 255) : IM_COL32(70, 74, 52, 170));
    dl->AddRectFilled(ImVec2(x, yb - h * 0.56f), ImVec2(x + w, yb), cs, 1.5f);
    dl->AddRectFilled(ImVec2(x + w * 0.06f, yb - h * 0.84f), ImVec2(x + w * 0.94f, yb - h * 0.56f), pr);
    dl->AddTriangleFilled(ImVec2(x + w * 0.06f, yb - h * 0.84f), ImVec2(x + w * 0.94f, yb - h * 0.84f), ImVec2(x + w * 0.5f, yb - h), pr);
    if (!ap) dl->AddRectFilled(ImVec2(x + w * 0.06f, yb - h * 0.74f), ImVec2(x + w * 0.94f, yb - h * 0.70f), lit ? IM_COL32(230, 200, 60, 255) : IM_COL32(120, 110, 50, 170));
    else dl->AddRectFilled(ImVec2(x + w * 0.06f, yb - h * 0.62f), ImVec2(x + w * 0.94f, yb - h * 0.59f), lit ? IM_COL32(200, 40, 30, 255) : IM_COL32(100, 40, 30, 170));
  };
  // one cartridge of a belt
  auto round = [&](float x, float yb, bool full, bool lit) {
    ImU32 c = full ? (lit ? IM_COL32(214, 172, 80, 255) : IM_COL32(150, 125, 70, 220)) : IM_COL32(60, 56, 46, 160);
    dl->AddRectFilled(ImVec2(x, yb - 9), ImVec2(x + 3, yb), c);
    if (full) dl->AddRectFilled(ImVec2(x, yb - 12), ImVec2(x + 3, yb - 9), lit ? IM_COL32(150, 110, 60, 255) : IM_COL32(110, 85, 55, 200));
  };
  auto beltW = [&](int belt) { int perRow = std::min(belt, 50); return perRow * 4.0f; };
  auto belt = [&](float x, float yb, int belt, int loaded, bool lit) {   // rows of 50 (yb: bottom of the last row)
    int per = std::max(1, (belt + 99) / 100); belt = (belt + per - 1) / per; loaded = (loaded + per - 1) / per;   // TK1: a long belt (the minigun's 500): one mark per few rounds, two rows at most
    int perRow = std::min(belt, 50), rows = (belt + perRow - 1) / perRow;
    for (int k = 0; k < belt; ++k) {
      int row = k / perRow, col = k % perRow;
      round(x + col * 4.0f, yb - (rows - 1 - row) * 14.0f, k < loaded, lit);
    }
  };
  // widths of the groups
  const float gunW = 230;
  float bowW = d.bowOn ? std::max(170.0f, beltW(d.bowBelt) + 16) : 0;
  float roofW = std::max(170.0f, beltW(d.mgBelt) + 16);
  const bool roofOn = !d.mgName.empty();   // VH8: a vehicle without a roof gun (the Bradley) shows none
  if (!roofOn) roofW = 0;
  float W = gunW + (d.bowOn ? bowW + pad : 0) + (roofOn ? roofW + pad : 0) + pad;
  float x0 = io.DisplaySize.x * 0.5f - W * 0.5f, y0 = io.DisplaySize.y - H - 12;
  dl->AddRectFilled(ImVec2(x0 - pad, y0), ImVec2(x0 + W, y0 + H), IM_COL32(12, 12, 10, 205), 6);
  float x = x0;
  // ---- main gun
  {
    bool lit = true;
    char hd[96]; std::snprintf(hd, sizeof hd, "MAIN GUN  %s", hatch ? "[RMB]" : "[LMB]");
    dl->AddText(ImVec2(x, y0 + 5), lit ? hot : dim, hd);
    // stowage: AP and HE with counts; the next round to load is framed
    for (int k = 0; k < 2; ++k) {
      bool ap = k == 0;
      int n = ap ? t.reserveAP : t.reserveHE;
      float sx = x + k * 72;
      shell(sx + 4, y0 + H - 8, 40, ap, n > 0);
      char cb[24]; std::snprintf(cb, sizeof cb, "%s\n%d", ap ? "AP" : "HE", n);
      dl->AddText(ImVec2(sx + 22, y0 + 28), n > 0 ? txt : dim, cb);
      if (t.selected == (ap ? SHELL_AP : SHELL_HE)) dl->AddRect(ImVec2(sx - 1, y0 + 22), ImVec2(sx + 62, y0 + H - 4), IM_COL32(230, 200, 110, 220), 3, 0, 1.5f);
    }
    // the breech: chambered round, or the loader at work
    float bx = x + 150;
    dl->AddRect(ImVec2(bx - 2, y0 + 22), ImVec2(bx + 74, y0 + H - 4), IM_COL32(90, 86, 72, 220), 3);
    if (t.loaded != SHELL_NONE) {
      shell(bx + 4, y0 + H - 8, 40, t.loaded == SHELL_AP, true);
      dl->AddText(ImVec2(bx + 22, y0 + 28), IM_COL32(150, 235, 140, 255), t.loaded == SHELL_AP ? "AP\nREADY" : "HE\nREADY");
    } else if (t.reload > 0) {
      float u = 1.0f - t.reload / d.reloadTime;
      char lb[32]; std::snprintf(lb, sizeof lb, "LOAD %s\n%.1f s", t.selected == SHELL_AP ? "AP" : "HE", t.reload);
      dl->AddText(ImVec2(bx + 4, y0 + 26), hot, lb);
      dl->AddRectFilled(ImVec2(bx + 4, y0 + H - 14), ImVec2(bx + 68, y0 + H - 9), IM_COL32(40, 40, 36, 230));
      dl->AddRectFilled(ImVec2(bx + 4, y0 + H - 14), ImVec2(bx + 4 + 64 * u, y0 + H - 9), IM_COL32(220, 190, 90, 240));
    } else dl->AddText(ImVec2(bx + 6, y0 + 32), IM_COL32(255, 120, 90, 255), "EMPTY");
    x += gunW + pad;
  }
  auto mgGroup = [&](const char* title, const char* keys, bool lit, int beltN, int loaded, int reserve, float reloadT, float reloadTotal, float w) {
    char hd[96]; std::snprintf(hd, sizeof hd, "%s  %s", title, keys);
    dl->AddText(ImVec2(x, y0 + 5), lit ? hot : dim, hd);
    belt(x, y0 + (beltN > 50 ? 56.0f : 50.0f), beltN, loaded, lit);
    char cb[64];
    if (reloadT > 0) std::snprintf(cb, sizeof cb, "BELT CHANGE %.1f s   +%d", reloadT, reserve);
    else std::snprintf(cb, sizeof cb, "%d / %d   +%d", loaded, beltN, reserve);
    dl->AddText(ImVec2(x, y0 + H - 22), reloadT > 0 ? hot : (loaded == 0 && reserve == 0 ? IM_COL32(255, 120, 90, 255) : (lit ? txt : dim)), cb);
    if (reloadT > 0) {
      float u = 1.0f - reloadT / std::max(0.1f, reloadTotal);
      dl->AddRectFilled(ImVec2(x, y0 + H - 26), ImVec2(x + (w - 16), y0 + H - 23), IM_COL32(40, 40, 36, 230));
      dl->AddRectFilled(ImVec2(x, y0 + H - 26), ImVec2(x + (w - 16) * u, y0 + H - 23), IM_COL32(220, 190, 90, 240));
    }
    x += w + pad;
  };
  if (d.bowOn) mgGroup(d.bowShort.c_str(), "[Space]", true, d.bowBelt, t.bowLoaded, t.bowReserve, t.bowReloadT, d.bowReload, bowW);
  std::string roofName = d.mgName.substr(0, d.mgName.find(" ("));
  if (roofName.size() > 18) roofName = "Roof MG";
  if (roofOn) mgGroup(roofName.c_str(), hatch ? "[LMB / Space]" : "[hatch]", hatch, d.mgBelt, t.mgLoaded, t.mgReserve, t.mgReloadT, d.mgReload, roofW);
}

// ============================================================ Creative selection panel
void TankSys::drawEditorPanel(App& app, Instance& inst) {
  const TankDef* d = defFor(app.assetFor(inst));
  if (!d || !ImGui::CollapsingHeader("Tank: spawn configuration", ImGuiTreeNodeFlags_DefaultOpen)) return;
  json o = inst.extra.is_object() && inst.extra.contains("tank") ? inst.extra["tank"] : json::object();
  int ap = o.value("reserveAP", d->reserveAP), he = o.value("reserveHE", d->reserveHE), hp = o.value("hp", 100);
  float ty = o.value("turretYaw", 0.0f);
  bool wreck = o.value("wrecked", false);
  ImGui::TextDisabled("%s", d->identity.substr(0, 90).c_str());
  bool edit = false, act = false;
  ImGui::SliderInt("AP rounds", &ap, 0, d->reserveAP * 2); act |= ImGui::IsItemActivated(); edit |= ImGui::IsItemEdited();
  ImGui::SliderInt("HE rounds", &he, 0, d->reserveHE * 2); act |= ImGui::IsItemActivated(); edit |= ImGui::IsItemEdited();
  ImGui::SliderFloat("Turret (deg)", &ty, -180, 180, "%.0f"); act |= ImGui::IsItemActivated(); edit |= ImGui::IsItemEdited();
  static bool undoPushed = false;
  if (act && !undoPushed) { app.pushUndo(); undoPushed = true; }
  if (!ImGui::IsAnyItemActive()) undoPushed = false;
  Instance* s = app.doc.find(inst.id);
  if (edit && s) {
    if (!s->extra.is_object()) s->extra = json::object();
    s->extra["tank"]["reserveAP"] = ap; s->extra["tank"]["reserveHE"] = he; s->extra["tank"]["turretYaw"] = ty;
    app.bump(*s);
  }
  ImGui::Text("Applied state: %s, durability %d/100", wreck ? "WRECK" : "intact", hp);
  if ((wreck || hp < 100) && ImGui::Button("Repair (clear applied damage)") && s) {
    app.pushUndo(); s = app.doc.find(inst.id);
    if (s && s->extra.is_object() && s->extra.contains("tank")) for (const char* k : {"hp", "wrecked", "hitsFront", "hitsSide"}) s->extra["tank"].erase(k);
    if (s) app.bump(*s);
  }
}

// ============================================================ sounds (temporary, synthesized; no tank recordings supplied)
void buildTankSounds(Audio& au) {
  const int SR = 44100;
  std::mt19937 g(4242); std::uniform_real_distribution<float> U(-1, 1);
  auto norm = [](std::vector<float>& b, float peak) { float m = 1e-6f; for (float v : b) m = std::max(m, std::fabs(v)); for (float& v : b) v *= peak / m; };
  auto loopFix = [&](std::vector<float>& b) {   // crossfade the noise so the loop seam is inaudible
    size_t xf = SR / 20; for (size_t i = 0; i < xf; ++i) { float a = (float)i / xf; b[i] = b[i] * a + b[b.size() - xf + i] * (1 - a); } b.resize(b.size() - xf);
  };
  // engines: harmonic firing series on an integer number of cycles + rough low-passed noise; Sherman lighter / smoother,
  // Tiger lower and rougher. Pitch and volume follow rpm at runtime.
  auto engine = [&](float f0, int harm, float rough, float secs) {
    std::vector<float> b((size_t)(secs * SR));
    float lp = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR, s = 0;
      for (int h = 1; h <= harm; ++h) s += std::sin(6.2831853f * f0 * h * t + h * 0.7f) / (float)h;
      float firing = std::pow(0.5f + 0.5f * std::sin(6.2831853f * f0 * 0.5f * t), 6.0f);
      lp += (U(g) - lp) * 0.04f;
      b[i] = s * (0.6f + 0.4f * firing) + lp * rough * 3.0f;
    }
    loopFix(b); norm(b, 0.8f); return b;
  };
  au.addSound("engine_sherman", engine(32.0f, 7, 0.35f, 2.05f));
  au.addSound("engine_tiger", engine(26.0f, 10, 0.55f, 2.05f));
  { // tracks: link clank pulses (9 / s) with a metallic ring over a low rumble
    std::vector<float> b((size_t)(1.05f * SR)); float lp = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR, ph = std::fmod(t * 9.0f, 1.0f);
      float clank = std::exp(-ph * 40.0f) * (std::sin(6.2831853f * 1400 * t) * 0.5f + U(g) * 0.6f);
      lp += (U(g) - lp) * 0.02f;
      b[i] = clank + lp * 2.0f;
    }
    loopFix(b); norm(b, 0.7f); au.addSound("tank_tracks", b);
  }
  { // turret drive: motor whine
    std::vector<float> b((size_t)(1.05f * SR));
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR; b[i] = std::sin(6.2831853f * 180 * t) * 0.6f + std::sin(6.2831853f * 360 * t) * 0.3f + std::sin(6.2831853f * 540 * t) * 0.1f + U(g) * 0.05f; }
    loopFix(b); norm(b, 0.5f); au.addSound("turret_drive", b);
  }
  auto cannon = [&](float crack, float boom, float tail, float len) {
    std::vector<float> b((size_t)(len * SR)); float lp = 0, lp2 = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      lp += (U(g) - lp) * std::min(1.0f, crack / SR * 6.28f); lp2 += (U(g) - lp2) * 0.015f;
      b[i] = lp * std::exp(-t * 60.0f) * 1.2f + std::sin(6.2831853f * boom * t) * std::exp(-t * 7.0f) + lp2 * 4.0f * std::exp(-t * tail);
    }
    norm(b, 0.95f); return b;
  };
  au.addSound("tank_17pdr", cannon(6500, 52, 2.0f, 2.6f));
  au.addSound("tank_88", cannon(5200, 40, 1.5f, 3.0f));
  { // MG 34 (temporary; distinct from the MG 42 crack)
    std::vector<float> b((size_t)(0.45f * SR)); float lp = 0, lp2 = 0;
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR; lp += (U(g) - lp) * 0.6f; lp2 += (U(g) - lp2) * 0.1f; b[i] = lp * std::exp(-t * 90) + std::sin(6.2831853f * 90 * t) * std::exp(-t * 25) * 0.8f + lp2 * 2.0f * std::exp(-t * 14); }
    norm(b, 0.9f); au.addSound("shot_mg34", b);
  }
}
