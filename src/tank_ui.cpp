// Tanks: drawing (supplied GLB parts on the rig), gunner sights, HUD, Enter prompt, catalog thumbnails and the
// temporary synthesized tank sounds. docs/tanks.md
#include "app.h"
#include <imgui.h>
#include <random>
#include <algorithm>

namespace {
const char* shellName(int s) { return s == SHELL_HE ? "HE" : s == SHELL_AP ? "AP" : "EMPTY"; }
}

// ============================================================ drawing
void TankSys::drawItems(App& app, const Instance& i, const InstanceRT& r) {
  (void)r;
  auto it = tanks.find(i.id);
  if (it == tanks.end() || !it->second.def || !it->second.def->model) return;
  const TankRT& t = it->second; const TankDef& d = *t.def;
  const GpuModel* gm = d.model.get();
  mat4 h, tu, g, rc; rig(t, h, tu, g, rc);
  vec4 tint = t.wrecked ? vec4(0.13f, 0.12f, 0.11f, 1) : vec4(1);   // burnt-out: darker than a live tank
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
}

// Creative placement preview: every part of the baked model at the cursor, tinted green (valid) / red (refused)
void TankSys::ghostItems(App& app, const AssetDef* a, const mat4& M, vec4 tint) {
  const TankDef* d = defFor(a);
  if (!d) return;
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
    vec3 c = (d->model->bmin + d->model->bmax) * 0.5f; c.y = 1.4f;
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
    // ---- gunner sight: circular field, dark surround, original reticle (docs/tanks.md#optics)
    float R = io.DisplaySize.y * 0.45f;
    for (int k = 0; k < 72; ++k) {
      float a0 = k * 6.2831853f / 72, a1 = (k + 1) * 6.2831853f / 72;
      ImVec2 p0(c.x + std::cos(a0) * R, c.y + std::sin(a0) * R), p1(c.x + std::cos(a1) * R, c.y + std::sin(a1) * R);
      ImVec2 q0(c.x + std::cos(a0) * io.DisplaySize.x, c.y + std::sin(a0) * io.DisplaySize.x), q1(c.x + std::cos(a1) * io.DisplaySize.x, c.y + std::sin(a1) * io.DisplaySize.x);
      dl->AddQuadFilled(p0, q0, q1, p1, IM_COL32(5, 5, 4, 255));
    }
    dl->AddCircle(c, R, IM_COL32(25, 24, 20, 255), 128, 5);
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
    if (d.reticle == "tiger") {
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
    char b[96]; std::snprintf(b, sizeof b, "%s  %s", zoom ? "ZOOM" : "SIGHT", d.cannonName.c_str());
    dl->AddText(ImVec2(c.x - R * 0.62f, c.y - R * 0.78f), IM_COL32(205, 195, 160, 220), b);
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
  char b[480];
  std::snprintf(b, sizeof b,
    "%s  [%s]\nSPEED %3.0f km/h   TURRET %+4.0f   GUN %+4.1f\nDURABILITY %d/100   (front hits %d, side/rear hits %d)\n%s",
    d.name.c_str(), t.station == TS_HATCH ? "HATCH" : "GUNNER",
    std::fabs(t.speed) * 3.6f, t.tYaw, t.gElev, t.hp, t.hitsFront, t.hitsSide,
    t.station == TS_HATCH ? "LMB roof MG | Space roof + bow MG | RMB main gun | Ctrl back to the sight | R next shell | E get out"
                          : "LMB main gun | Space bow MG | RMB zoom | Ctrl open hatch | R next shell | E get out");
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
  float W = gunW + (d.bowOn ? bowW + pad : 0) + roofW + pad * 2;
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
  mgGroup(roofName.c_str(), hatch ? "[LMB / Space]" : "[hatch]", hatch, d.mgBelt, t.mgLoaded, t.mgReserve, t.mgReloadT, d.mgReload, roofW);
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
