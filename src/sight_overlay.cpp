// W2 sight-alignment check and dev overlay (spec 8.3). The rear / front anchors go through the same weapon
// projection the first-person pass uses (vmFovNow, framebuffer aspect), so the pixel offsets here are what the
// player sees; the roll is the drawn weapon's +y axis in view space against the screen vertical.
#include "sight_overlay.h"
#include "app.h"
#include "vm_anim.h"
#include "br_models.h"
#include <imgui.h>

SightCheck sightCheck(const App& a) {
  SightCheck c;
  if (a.occupied || a.weaponDefs.empty() || a.mode != Mode::Explore || a.R.width <= 0) return c;
  const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
  auto it = a.wstate.find(d.id); if (it == a.wstate.end()) return c;
  const WeaponState& s = it->second;
  mat4 view; float wf; a.fpCamera(view, wf);
  float aspect = (float)a.R.width / std::max(1, a.R.height);
  mat4 vp = glm::perspective(radians(a.vmFovNow()), aspect, 0.01f, 6.0f) * view;
  auto px = [&](vec3 w) { vec4 q = vp * vec4(w, 1); return vec2((q.x / q.w * 0.5f + 0.5f) * a.R.width, (0.5f - q.y / q.w * 0.5f) * a.R.height); };
  vec3 Rw, Fw; a.sightLineWorld(d, s, Rw, Fw);   // the sight points the pose was built on (scope eye when scoped)
  c.centre = vec2(a.R.width * 0.5f, a.R.height * 0.5f);
  c.rear = px(Rw); c.front = px(Fw);
  c.rearErr = glm::length(c.rear - c.centre); c.frontErr = glm::length(c.front - c.centre);
  vec3 up = glm::mat3(view) * glm::mat3(a.viewModelMatrix(d, s)) * vec3(0, 1, 0);   // weapon +y in view space
  c.rollDeg = degrees(std::atan2(up.x, up.y));
  c.ok = true;
  return c;
}

void drawSightOverlay(const App& a, ImDrawList* dl, float sx, float sy) {
  if (!a.sightMarkers) return;
  SightCheck c = sightCheck(a);
  if (!c.ok) return;
  auto S = [&](vec2 p) { return ImVec2(p.x * sx, p.y * sy); };
  ImU32 mag = IM_COL32(255, 60, 255, 230), k = IM_COL32(255, 255, 255, 230);
  ImVec2 o = S(c.centre);
  // camera centre: a gapped crosshair so the sight picture under it stays visible
  for (int sgn : {-1, 1}) {
    dl->AddLine(ImVec2(o.x + sgn * 10, o.y), ImVec2(o.x + sgn * 40, o.y), mag, 1.0f);
    dl->AddLine(ImVec2(o.x, o.y + sgn * 10), ImVec2(o.x, o.y + sgn * 40), mag, 1.0f);
  }
  dl->AddLine(o, S(c.rear), IM_COL32(0, 208, 255, 200), 1.0f);
  dl->AddLine(o, S(c.front), IM_COL32(48, 255, 48, 200), 1.0f);
  char b[200];
  std::snprintf(b, sizeof b, "centre (%.1f, %.1f)  rear (%+.2f, %+.2f) px  front (%+.2f, %+.2f) px  roll %+.3f deg  [%dx%d]",
                c.centre.x, c.centre.y, c.rear.x - c.centre.x, c.rear.y - c.centre.y, c.front.x - c.centre.x, c.front.y - c.centre.y, c.rollDeg, a.R.width, a.R.height);
  bool pass = c.frontErr <= 1.0f && c.rearErr <= 2.0f && std::fabs(c.rollDeg) < 0.05f;
  dl->AddText(ImVec2(20, ImGui::GetIO().DisplaySize.y - 80), pass ? k : IM_COL32(255, 90, 90, 255), b);
}

// ---- AT2: reflex (red dot) and holographic reticles. A collimated mark sits on the shot line, not on the glass: drawn here
// on the HUD (after TAA, so it never swims or smears), placed sub-pixel exactly where the round goes at the gun's zero and
// sized in angle (2 MOA dot; the holographic sight's layout from its model image: 1 MOA dot, 65 MOA ring, four ticks).
ReflexReticle reflexReticle(const App& a) {
  ReflexReticle r;
  if (a.occupied || a.weaponDefs.empty() || a.mode != Mode::Explore || a.R.width <= 0 || a.scopeView()) return r;
  const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
  auto it = a.wstate.find(d.id); if (it == a.wstate.end() || d.hasScope) return r;
  const WeaponState& s = it->second;
  vec3 col(1, 0.03f, 0.015f); float ring = 0;
  const int fs = fittedSight(d, s);   // AT5: a default optic (the SMG-9's holographic sight) as a fitted one
  if (fs == 1 || fs == 7) r.kind = 1; else if (fs == 5) r.kind = 2;   // PS1: the pistol red dot draws the 2 MOA dot
  else if (fs == 0 && animVmDot(d, col, ring)) r.kind = 1;
  if (!r.kind) return r;
  r.col = col; r.ring = ring;
  r.alpha = glm::smoothstep(0.85f, 0.97f, a.adsT);
  mat4 view; float wf; a.fpCamera(view, wf);
  float W = (float)a.R.width, H = (float)a.R.height, aspect = W / std::max(1.0f, H);
  mat4 wP = glm::perspective(radians(wf), aspect, 0.03f, 4000.0f);
  vec3 Rw, Fw; a.sightLineWorld(d, s, Rw, Fw);
  // the shot line (weapons.cpp): an animated rig fires along the camera axis, a static gun along its sight line; the mark
  // goes where that line meets the zero range (where the round crosses it)
  vec3 dir = d.anim ? App::forward(a.eyeYaw + a.viewKickYaw, a.eyePitch + a.viewKickPitch) : glm::normalize(Fw - Rw);
  vec4 q = wP * view * vec4(Fw + dir * std::max(5.0f, d.zero), 1);
  if (q.w <= 0) return r;
  r.pos = vec2((q.x / q.w * 0.5f + 0.5f) * W, (0.5f - q.y / q.w * 0.5f) * H);
  r.pxMoa = wP[1][1] * H * 0.5f * 2.908882e-4f;
  // the mark only inside the sight's window: it is clipped at the frame, never drawn over the housing (the rig's own sight:
  // 11 mm round its lens centre; a fitted red dot / holographic sight: its window on the eye line, weapon space)
  auto window = [&](vec3 cw, float rw) {
    mat4 vP = glm::perspective(radians(a.vmFovNow()), aspect, 0.01f, 6.0f);
    vec4 v = view * vec4(cw, 1), c = vP * v;
    if (c.w <= 0 || -v.z < 0.01f) { r.alpha = 0; return; }
    vec2 w((c.x / c.w * 0.5f + 0.5f) * W, (0.5f - c.y / c.w * 0.5f) * H);
    float winR = vP[1][1] * H * 0.5f * rw / -v.z;
    r.alpha *= 1.0f - glm::smoothstep(0.8f * winR, winR, glm::length(w - r.pos));
  };
  if (fs == 0) { if (d.anim) window(Fw, 0.011f); }
  else {
    vec3 cw; float rw; mat4 Wm;
    if (brSightWindow(d, s, cw, rw) && (d.anim ? animVmWeaponMatrix(a, d, Wm) : (Wm = a.viewModelMatrix(d, s), true)))
      window(vec3(Wm * vec4(cw, 1)), rw * glm::length(vec3(Wm[0])));
  }
  r.ok = r.alpha > 0.01f;
  return r;
}

void drawReflexReticle(const App& a, ImDrawList* dl, float sx, float sy) {
  ReflexReticle r = reflexReticle(a);
  if (!r.ok) return;
  const float S = a.reticleSize, B = a.reticleBright, pm = r.pxMoa * S;
  const float dotR = std::max(0.75f * std::max(0.67f, S), (r.kind == 1 ? 1.0f : 0.5f) * pm);   // never below ~1.5 px across (1 px at 50 %)
  const float lw = 0.5f * std::max(1.0f, S);                                                    // line half width: 1 px lines
  const float ringR = r.kind == 2 ? std::max(3.0f, 32.5f * pm) : r.ring > 0 ? std::max(3.5f * S, r.ring * pm) : 0.0f;
  const float tickR = r.kind == 2 ? ringR * 1.25f : 0.0f;   // the model's ticks run out from the ring (shortened a little)
  auto ink = [&](vec2 p, float grow) {
    float dd = glm::length(p), k = std::clamp(dotR + grow + 0.5f - dd, 0.0f, 1.0f);
    if (grow == 0) k = std::max(k, 0.25f * std::clamp(dotR + 1.5f - dd, 0.0f, 1.0f));   // a faint 1 px soft edge
    if (ringR > 0) k = std::max(k, std::clamp(lw + grow + 0.5f - std::fabs(dd - ringR), 0.0f, 1.0f));
    if (tickR > 0) {
      vec2 q = glm::abs(p);
      float tx = std::max(q.y - lw - grow, std::max(ringR - q.x, q.x - tickR - grow)), ty = std::max(q.x - lw - grow, std::max(ringR - q.y, q.y - tickR - grow));
      k = std::max(k, std::clamp(0.5f - std::min(tx, ty), 0.0f, 1.0f));
    }
    return k;
  };
  auto srgb = [](float v) { return (int)std::lround(255.0f * std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f)); };
  vec3 c = glm::mix(r.col, vec3(1.0f), 0.25f * std::max(0.0f, B - 1.0f));   // above 100 %: a hotter core
  int cr = srgb(c.x), cg = srgb(c.y), cb = srgb(c.z);
  float aInk = std::min(1.0f, 0.95f * B) * r.alpha, aOut = 0.3f * r.alpha;   // a thin dark outline at 30 % for bright backgrounds
  float ext = std::max(dotR, tickR > 0 ? tickR : ringR + lw) + 2.5f;
  for (int y = (int)std::floor(r.pos.y - ext); y <= (int)std::ceil(r.pos.y + ext); ++y)
    for (int x = (int)std::floor(r.pos.x - ext); x <= (int)std::ceil(r.pos.x + ext); ++x) {
      vec2 p = vec2(x + 0.5f, y + 0.5f) - r.pos;
      float k = ink(p, 0), o = ink(p, 1.0f) * (1.0f - k);
      ImVec2 p0(x * sx, y * sy), p1((x + 1) * sx, (y + 1) * sy);
      if (o > 0.01f) dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, (int)(255 * aOut * o)));
      if (k > 0.01f) dl->AddRectFilled(p0, p1, IM_COL32(cr, cg, cb, (int)(255 * aInk * k)));
    }
}

// AT6: aimed through a fitted optic (or a gun's default one): its window on screen (uv, top-left origin) and radius (frame heights,
// 15 % over the window), else z = 0. The first-person pass leaves the gun's own parts there undrawn (render.cpp uVmClip): its irons,
// posts and risers never stand in the optic's view; removing the optic brings them back
vec3 opticClipWindow(const App& a) {
  if (a.occupied || a.weaponDefs.empty() || a.mode != Mode::Explore || a.R.width <= 0 || a.scopeView() || a.adsT < 0.6f) return vec3(0);
  const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
  auto it = a.wstate.find(d.id); if (it == a.wstate.end() || !fittedSight(d, it->second)) return vec3(0);
  const WeaponState& s = it->second;
  vec3 cw; float rw; mat4 Wm;
  if (!brSightWindow(d, s, cw, rw) || !(d.anim ? animVmWeaponMatrix(a, d, Wm) : (Wm = a.viewModelMatrix(d, s), true))) return vec3(0);
  mat4 view; float wf; a.fpCamera(view, wf);
  mat4 vP = glm::perspective(radians(a.vmFovNow()), (float)a.R.width / std::max(1, a.R.height), 0.01f, 6.0f);
  vec4 v = view * vec4(vec3(Wm * vec4(cw, 1)), 1), c = vP * v;
  if (c.w <= 0 || -v.z < 0.01f) return vec3(0);
  return vec3(c.x / c.w * 0.5f + 0.5f, 0.5f - c.y / c.w * 0.5f, 1.15f * vP[1][1] * 0.5f * rw * glm::length(vec3(Wm[0])) / -v.z);
}
