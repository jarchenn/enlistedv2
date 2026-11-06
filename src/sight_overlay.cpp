// W2 sight-alignment check and dev overlay (spec 8.3). The rear / front anchors go through the same weapon
// projection the first-person pass uses (vmFovNow, framebuffer aspect), so the pixel offsets here are what the
// player sees; the roll is the drawn weapon's +y axis in view space against the screen vertical.
#include "sight_overlay.h"
#include "app.h"
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
