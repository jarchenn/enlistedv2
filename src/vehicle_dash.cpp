// VH6 vehicle interiors (vehicle_dash.h). Each kit is built once per style and driver side from parts (boxes and
// cylinders) into three meshes: the panel body, the glowing parts (lamps, backlit buttons, screens: drawn only with the
// engine running, their glow raised at night) and the dead screens (parked civilian cars). Needles, digits and bar
// segments are small unit meshes placed per frame.
#include "vehicle_dash.h"
#include "app.h"
#include "tank.h"
#include "assets.h"
#include "lights.h"
#include <glm/gtx/quaternion.hpp>
#include <map>

namespace {
enum Style { MIL = 0, CIV = 1 };
struct Gauge { vec3 c; float r; int what; };   // what: 0 speed, 1 rpm, 2 fuel, 3 temperature
struct Kit {
  GpuMesh body, glow, dead, wreck, needle, seg, bar;
  std::vector<Gauge> gauges;
  vec3 digits{0}; float digitW = 0.016f, digitH = 0.026f;   // civilian: the speed read-out (right edge of three digits)
  vec3 barAt{0}; int bars = 12;                             // civilian: the rpm bar row
};
Material M(const char* n, vec3 alb, float rough, float metal = 0, int pat = PAT_PLAIN, vec3 emi = vec3(0)) { Material m; m.name = n; m.albedo = alb; m.roughness = rough; m.metallic = metal; m.pattern = pat; m.emissive = emi; m.texCode = 1; return m; }
struct B {
  float in;   // +1 / -1: the direction from the driver toward the middle of the cab (x)
  void box(MeshData& m, vec3 c, vec3 s, const Material& mt, vec3 rotDeg = vec3(0)) {
    Part p; p.shape = Part::BOX; p.center = vec3(c.x * in, c.y, c.z); p.size = s; p.rot = glm::quat(glm::radians(vec3(rotDeg.x, rotDeg.y * in, rotDeg.z * in)));
    appendPart(m, p, mt, mat4(1));
  }
  // a cylinder facing the driver (its axis along z) unless `axis` says otherwise
  void cyl(MeshData& m, vec3 c, float r, float depth, const Material& mt, vec3 axis = vec3(0, 0, 1), int sides = 0, float top = 1) {
    Part p; p.shape = Part::CYL; p.center = vec3(c.x * in, c.y, c.z); p.size = vec3(2 * r, depth, 2 * r); p.sides = sides; p.top = top;
    p.rot = glm::rotation(vec3(0, 1, 0), glm::normalize(vec3(axis.x * in, axis.y, axis.z)));
    appendPart(m, p, mt, mat4(1));
  }
};

Kit buildMilitary(float in) {
  Kit k; B b{in}; MeshData body, glow;
  Material od = M("od", vec3(0.19f, 0.21f, 0.11f), 0.72f, 0.25f, PAT_METAL), odDk = M("od_dk", vec3(0.13f, 0.14f, 0.08f), 0.8f, 0.2f, PAT_METAL);
  Material blk = M("black", vec3(0.025f), 0.6f, 0.1f), face = M("face", vec3(0.015f), 0.45f), chrome = M("chrome", vec3(0.55f), 0.28f, 0.9f, PAT_METAL);
  Material steel = M("steel", vec3(0.24f, 0.24f, 0.23f), 0.5f, 0.75f, PAT_METAL), tick = M("tick", vec3(0.85f, 0.83f, 0.75f), 0.6f, 0, PAT_PLAIN, vec3(0.9f, 0.75f, 0.45f));   // backlit dial markings
  Material plate = M("plate", vec3(0.62f, 0.6f, 0.52f), 0.45f, 0.5f, PAT_METAL), ink = M("ink", vec3(0.05f), 0.8f), grime = M("grime", vec3(0.08f, 0.075f, 0.05f), 1.0f);
  Material radio = M("radio", vec3(0.12f, 0.14f, 0.09f), 0.7f, 0.2f, PAT_METAL), lampOff = M("lamp_off", vec3(0.12f, 0.05f, 0.04f), 0.3f);
  // the driver's panel and its glare hood, the centre stack beside it
  b.box(body, vec3(0, -0.115f, 0.035f), vec3(0.64f, 0.23f, 0.07f), od);
  b.box(body, vec3(0, 0.006f, -0.03f), vec3(0.52f, 0.016f, 0.13f), blk);
  b.box(body, vec3(0.47f, -0.15f, 0.05f), vec3(0.34f, 0.30f, 0.07f), odDk, vec3(-8, 0, 0));
  for (float x : {-0.30f, 0.30f}) for (float y : {-0.015f, -0.215f}) b.cyl(body, vec3(x, y, -0.002f), 0.005f, 0.006f, steel, vec3(0, 0, 1), 6);   // bolt heads
  // gauges: speed and rpm in the middle (large), fuel and temperature outside (small)
  struct G { float x, y, r; int w; } gs[4] = {{-0.205f, -0.09f, 0.034f, 2}, {-0.075f, -0.09f, 0.046f, 0}, {0.075f, -0.09f, 0.046f, 1}, {0.205f, -0.09f, 0.034f, 3}};
  for (auto& g : gs) {
    b.cyl(body, vec3(g.x, g.y, -0.005f), g.r + 0.007f, 0.012f, chrome, vec3(0, 0, 1), 24);
    b.cyl(body, vec3(g.x, g.y, -0.0115f), g.r, 0.002f, face, vec3(0, 0, 1), 24);
    for (int t = 0; t <= 8; ++t) {   // ticks along the 270 deg sweep
      float th = glm::radians(-135.0f + 270.0f * t / 8), r = g.r * 0.8f;
      vec3 d(-std::sin(th), std::cos(th), 0);   // as the needle: + rotation about z, seen from the driver: clockwise
      b.box(body, vec3(g.x + d.x * r * in, g.y + d.y * r, -0.0128f), vec3(0.0024f, t % 2 ? 0.006f : 0.010f, 0.001f), tick, vec3(0, 0, glm::degrees(th) * in));
    }
    b.cyl(body, vec3(g.x, g.y, -0.0145f), 0.0055f, 0.003f, blk, vec3(0, 0, 1), 12);   // the needle cap
    k.gauges.push_back({vec3(g.x * in, g.y, -0.0132f), g.r, g.w});
  }
  // warning-light panel under the cluster: 2 x 5 lamps, some lit when the engine runs
  vec3 lampCol[5] = {vec3(2.6f, 0.18f, 0.08f), vec3(2.4f, 1.3f, 0.1f), vec3(0.3f, 2.2f, 0.45f), vec3(0.35f, 0.8f, 2.4f), vec3(2.6f, 0.18f, 0.08f)};
  for (int r = 0; r < 2; ++r) for (int c = 0; c < 5; ++c) {
    vec3 p(-0.27f + c * 0.028f, -0.17f - r * 0.026f, -0.003f);
    b.box(body, p + vec3(0, 0, 0.002f), vec3(0.022f, 0.02f, 0.004f), blk);
    b.cyl(body, p + vec3(0, 0, -0.001f), 0.0075f, 0.004f, lampOff, vec3(0, 0, 1), 12);
    if ((r * 5 + c) % 3 != 2) b.cyl(glow, p + vec3(0, 0, -0.0035f), 0.007f, 0.002f, M("lamp", lampCol[c] * 0.12f, 0.3f, 0, PAT_PLAIN, lampCol[c]), vec3(0, 0, 1), 12);
  }
  // data plates (lower right of the panel): riveted plates with stamped lines
  for (int i = 0; i < 3; ++i) {
    vec3 p(0.12f + i * 0.062f, -0.19f, -0.002f);
    b.box(body, p, vec3(0.054f, 0.032f, 0.0015f), plate);
    for (int l = 0; l < 3; ++l) b.box(body, p + vec3(0, 0.009f - l * 0.009f, -0.001f), vec3(0.04f - l * 0.008f, 0.0025f, 0.0006f), ink);
  }
  // the centre stack: two rows of guarded toggle switches with labels, a row of knobs, backlit push buttons
  for (int r = 0; r < 2; ++r) for (int c = 0; c < 6; ++c) {
    vec3 p(0.345f + c * 0.05f, -0.035f - r * 0.06f, 0.012f - r * 0.008f);
    b.box(body, p, vec3(0.02f, 0.026f, 0.006f), steel);
    b.cyl(body, p + vec3(0, 0.006f, -0.012f), 0.0028f, 0.024f, chrome, vec3(0, ((c + r) % 3 == 0 ? -0.55f : 0.55f), -1.0f), 8);   // the bat lever, up or down
    b.box(body, p + vec3(0, -0.02f, 0.001f), vec3(0.028f, 0.008f, 0.001f), plate);
  }
  for (int c = 0; c < 4; ++c) {
    vec3 p(0.36f + c * 0.07f, -0.165f, -0.004f);
    b.cyl(body, p, 0.014f, 0.018f, blk, vec3(0, 0, 1), 0); b.cyl(body, p + vec3(0, 0, 0.006f), 0.019f, 0.004f, steel, vec3(0, 0, 1), 0);
    b.box(body, p + vec3(0, 0.008f, -0.0095f), vec3(0.002f, 0.009f, 0.001f), tick);
  }
  vec3 btnCol[3] = {vec3(2.2f, 1.2f, 0.12f), vec3(0.3f, 2.0f, 0.4f), vec3(2.4f, 0.2f, 0.1f)};
  for (int r = 0; r < 2; ++r) for (int c = 0; c < 3; ++c) {
    vec3 p(0.40f + c * 0.06f, -0.225f - r * 0.032f, -0.003f);
    b.box(body, p, vec3(0.034f, 0.024f, 0.008f), blk);
    b.box(glow, p + vec3(0, 0, -0.0045f), vec3(0.026f, 0.016f, 0.001f), M("btn", btnCol[c] * 0.1f, 0.3f, 0, PAT_PLAIN, btnCol[c] * (r ? 0.5f : 1.0f)));
  }
  // radio below the stack: green case, a lit frequency window, knobs, the handset on its hook
  b.box(body, vec3(0.47f, -0.36f, 0.0f), vec3(0.26f, 0.10f, 0.17f), radio);
  b.box(glow, vec3(0.43f, -0.345f, -0.0855f), vec3(0.10f, 0.024f, 0.002f), M("radio_lcd", vec3(0.05f, 0.1f, 0.02f), 0.2f, 0, PAT_PLAIN, vec3(0.9f, 1.8f, 0.3f)));
  for (int c = 0; c < 4; ++c) b.cyl(body, vec3(0.38f + c * 0.06f, -0.385f, -0.09f), 0.011f, 0.016f, blk, vec3(0, 0, 1), 0);
  b.box(body, vec3(0.63f, -0.33f, -0.04f), vec3(0.04f, 0.11f, 0.05f), blk, vec3(0, 0, 10));
  // transfer-case and range levers rising from the floor, knobs on top
  for (int i = 0; i < 2; ++i) {
    vec3 base(0.30f + i * 0.07f, -0.62f, -0.18f);
    b.cyl(body, base + vec3(0, 0.12f, -0.03f), 0.007f, 0.26f, steel, vec3(0, 1, -0.25f), 8);
    b.cyl(body, base + vec3(0, 0.25f, -0.065f), 0.017f, 0.03f, blk, vec3(0, 1, 0), 10);
    b.box(body, base, vec3(0.05f, 0.03f, 0.08f), blk);
  }
  // worn paint and grime: darker patches round the switches and along the lower edge
  b.box(body, vec3(0, -0.226f, -0.0005f), vec3(0.6f, 0.012f, 0.001f), grime);
  b.box(body, vec3(0.47f, -0.05f, -0.008f), vec3(0.06f, 0.05f, 0.0008f), grime, vec3(-8, 0, 0));
  b.box(body, vec3(-0.26f, -0.03f, -0.0005f), vec3(0.05f, 0.035f, 0.0008f), grime);
  k.body.upload(body); k.glow.upload(glow);
  MeshData nd; B one{1}; one.box(nd, vec3(0, 0.017f, 0), vec3(0.0022f, 0.036f, 0.0012f), M("needle", vec3(0.9f, 0.4f, 0.1f), 0.4f, 0, PAT_PLAIN, vec3(1.6f, 0.55f, 0.1f))); k.needle.upload(nd);
  return k;
}

Kit buildCivilian(float in) {
  Kit k; B b{in}; MeshData body, glow, dead, wreck;
  Material dash = M("dash", vec3(0.05f, 0.05f, 0.055f), 0.55f), piano = M("piano", vec3(0.012f), 0.12f, 0.1f), silver = M("silver", vec3(0.6f, 0.6f, 0.62f), 0.25f, 0.9f, PAT_METAL);
  Material stitch = M("stitch", vec3(0.35f, 0.3f, 0.25f), 0.8f), deadGlass = M("dead", vec3(0.02f, 0.022f, 0.026f), 0.15f), dust = M("dust", vec3(0.2f, 0.19f, 0.17f), 0.95f), crack = M("crack", vec3(0.5f, 0.5f, 0.5f), 0.3f);
  auto emi = [](const char* n, vec3 e) { return M(n, e * 0.1f, 0.3f, 0, PAT_PLAIN, e); };
  // the kit stands on the car's own dash (anchor: its top, at its rear face): a soft fascia strip across the cab with the
  // vents and buttons, a binnacle with the digital cluster in front of the driver, a freestanding centre screen
  b.box(body, vec3(0.35f, 0.035f, 0.03f), vec3(1.30f, 0.07f, 0.05f), dash);
  b.box(body, vec3(0.35f, 0.071f, 0.006f), vec3(1.28f, 0.003f, 0.003f), stitch);   // the stitched seam
  b.box(body, vec3(0, 0.075f, 0.03f), vec3(0.36f, 0.13f, 0.07f), piano);
  b.box(body, vec3(0, 0.15f, -0.005f), vec3(0.38f, 0.02f, 0.09f), dash);   // the cluster's hood
  // digital cluster: the screen (live / dark / cracked); speed digits, rpm bars and icons are placed per frame
  vec3 cs(0, 0.075f, -0.0065f), csz(0.30f, 0.105f, 0.002f);
  b.box(glow, cs, csz, emi("cluster", vec3(0.04f, 0.09f, 0.2f)));
  b.box(dead, cs, csz, deadGlass); b.box(wreck, cs, csz, dust);
  for (int i = 0; i < 3; ++i) b.box(wreck, cs + vec3((-0.06f + i * 0.05f), 0.005f * i, -0.0015f), vec3(0.0015f, 0.09f, 0.0006f), crack, vec3(0, 0, 25.0f - i * 30));
  // the screen's contents read the same in left- and right-hand drive: x given in screen terms (+x = the driver's left), un-mirrored
  b.box(glow, vec3(0.09f * in, 0.04f, -0.0078f), vec3(0.008f, 0.012f, 0.0008f), emi("gear_d", vec3(0.9f, 1.0f, 1.1f)));   // "D"
  for (int i = 0; i < 4; ++i) b.box(glow, vec3((-0.11f + i * 0.012f) * in, 0.036f, -0.0078f), vec3(0.008f, 0.004f, 0.0008f), emi("fuel", vec3(0.4f, 1.4f, 0.5f)));   // fuel bar
  b.box(glow, vec3(0.13f * in, 0.075f, -0.0078f), vec3(0.008f, 0.008f, 0.0008f), emi("icon", vec3(0.2f, 1.2f, 0.4f)));   // lights-on icon
  k.digits = vec3(-0.045f, 0.07f, -0.0082f); k.barAt = vec3(0.12f, 0.108f, -0.0082f);
  // centre screen, tilted toward the driver: our own map (roads, a route, the position arrow) over a dark navy base, a top bar, a button row
  vec3 sc(0.40f, 0.16f, 0.006f), rot(-12, 0, 0);
  b.box(body, sc + vec3(0, 0, 0.012f), vec3(0.28f, 0.18f, 0.02f), piano, rot);
  b.box(glow, sc, vec3(0.26f, 0.16f, 0.002f), emi("screen", vec3(0.03f, 0.06f, 0.13f)), rot);
  b.box(dead, sc, vec3(0.26f, 0.16f, 0.002f), deadGlass, rot); b.box(wreck, sc, vec3(0.26f, 0.16f, 0.002f), dust, rot);
  for (int i = 0; i < 3; ++i) b.box(wreck, sc + vec3(-0.05f + i * 0.045f, 0.01f, -0.0015f), vec3(0.0015f, 0.15f, 0.0006f), crack, rot + vec3(0, 0, 35.0f - i * 25));
  auto onScreen = [&](vec3 off, vec3 size, const Material& m, float rz = 0) {   // a flat element on the tilted screen
    mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot))); vec3 p = sc + vec3(R * vec4(off + vec3(0, 0, -0.0015f), 0));
    b.box(glow, p, size, m, rot + vec3(0, 0, rz));
  };
  Material road = emi("road", vec3(0.55f, 0.6f, 0.68f)), route = emi("route", vec3(0.25f, 0.8f, 2.4f)), park = emi("park", vec3(0.2f, 0.7f, 0.3f)), ui = emi("ui", vec3(0.45f, 0.5f, 0.58f)), me = emi("me", vec3(2.2f, 2.2f, 2.0f));
  onScreen(vec3(0, 0.068f, 0), vec3(0.256f, 0.018f, 0.0008f), ui);   // top bar
  for (int i = 0; i < 5; ++i) onScreen(vec3(-0.1f + i * 0.05f, -0.066f, 0), vec3(0.036f, 0.016f, 0.0008f), ui);   // buttons
  onScreen(vec3(-0.04f, 0.0f, 0), vec3(0.17f, 0.005f, 0.0008f), road, 8); onScreen(vec3(0.03f, 0.02f, 0), vec3(0.004f, 0.10f, 0.0008f), road);
  onScreen(vec3(0.07f, -0.02f, 0), vec3(0.11f, 0.004f, 0.0008f), road, -20); onScreen(vec3(-0.07f, 0.03f, 0), vec3(0.004f, 0.07f, 0.0008f), road, 15);
  onScreen(vec3(0.085f, 0.035f, 0), vec3(0.04f, 0.03f, 0.0008f), park);
  onScreen(vec3(-0.01f, 0.0f, -0.0002f), vec3(0.08f, 0.006f, 0.0008f), route, 8); onScreen(vec3(0.03f, 0.03f, -0.0002f), vec3(0.006f, 0.06f, 0.0008f), route);
  onScreen(vec3(-0.05f, -0.005f, -0.0004f), vec3(0.012f, 0.012f, 0.0008f), me, 45);
  // round vents with chrome rings and slats, buttons under the screen, climate knobs
  for (float x : {-0.30f, 0.20f, 0.60f, 1.0f}) {
    b.cyl(body, vec3(x, 0.035f, -0.004f), 0.03f, 0.006f, silver, vec3(0, 0, 1), 0);
    b.cyl(body, vec3(x, 0.035f, -0.007f), 0.025f, 0.006f, piano, vec3(0, 0, 1), 0);
    for (int s = -1; s <= 1; ++s) b.box(body, vec3(x, 0.035f + s * 0.011f, -0.0105f), vec3(0.044f, 0.003f, 0.002f), dash);
  }
  for (int i = 0; i < 6; ++i) {
    vec3 p(0.29f + i * 0.044f, 0.035f, -0.004f);
    b.box(body, p, vec3(0.036f, 0.016f, 0.006f), piano);
    b.box(glow, p + vec3(0, 0, -0.0035f), vec3(0.008f, 0.003f, 0.0008f), emi("btn", vec3(1.0f, 1.0f, 0.95f)));
  }
  // centre console: the gear selector with its lit P R N D, two cup holders, the armrest
  b.box(body, vec3(0.40f, -0.43f, -0.20f), vec3(0.22f, 0.16f, 0.55f), dash);
  b.box(body, vec3(0.40f, -0.345f, -0.12f), vec3(0.20f, 0.012f, 0.30f), piano);
  b.box(body, vec3(0.40f, -0.32f, -0.10f), vec3(0.03f, 0.045f, 0.05f), piano);
  b.cyl(body, vec3(0.40f, -0.29f, -0.10f), 0.018f, 0.03f, silver, vec3(0, 1, 0), 0);
  for (int i = 0; i < 4; ++i) b.box(glow, vec3(0.44f, -0.337f, -0.16f + i * 0.02f), vec3(0.008f, 0.001f, 0.01f), emi("prnd", i == 3 ? vec3(2.0f, 2.0f, 2.0f) : vec3(0.4f)));
  for (int i = 0; i < 2; ++i) b.cyl(body, vec3(0.36f + i * 0.08f, -0.345f, -0.22f), 0.035f, 0.012f, M("cup", vec3(0.01f), 0.6f), vec3(0, 1, 0), 0);
  b.box(body, vec3(0.40f, -0.33f, -0.42f), vec3(0.20f, 0.06f, 0.24f), M("leather", vec3(0.09f, 0.07f, 0.06f), 0.7f, 0, PAT_FABRIC));
  k.body.upload(body); k.glow.upload(glow); k.dead.upload(dead); k.wreck.upload(wreck);
  MeshData sd; B one{1}; one.box(sd, vec3(0), vec3(1), emi("seg", vec3(1.6f, 1.8f, 2.0f))); k.seg.upload(sd);   // scaled per segment
  MeshData bd; one.box(bd, vec3(0), vec3(1), emi("bar", vec3(0.3f, 1.2f, 2.4f))); k.bar.upload(bd);
  return k;
}

const Kit& kit(int style, float in) {
  static std::map<int, Kit> kits;
  int key = style * 2 + (in > 0 ? 1 : 0);
  auto it = kits.find(key); if (it != kits.end()) return it->second;
  return kits[key] = style == MIL ? buildMilitary(in) : buildCivilian(in);
}

float glowNow(App& app) { return 1.0f + 1.6f * (app.R.wx.enclosed ? 1.0f : 1.0f - std::clamp(lightsDaylight(app), 0.0f, 1.0f)); }

// one interior: W = the anchor frame (hull space at the dash anchor, scaled); state 0 parked, 1 running, 2 wreck
void drawKit(App& app, int style, float in, const mat4& W, int state, float speedKmh, float speedU, float rpm) {
  const Kit& k = kit(style, in);
  float glow = glowNow(app);
  app.worldBoxDraws.push_back({&k.body, W});
  if (state == 1) { DrawItem g{&k.glow, W}; g.glow = glow; app.worldBoxDraws.push_back(g); }
  else if (style == CIV) app.worldBoxDraws.push_back({state == 2 ? &k.wreck : &k.dead, W});
  if (style == MIL) {
    for (auto& g : k.gauges) {
      float v = g.what == 0 ? speedU : g.what == 1 ? rpm : g.what == 2 ? 0.68f : (state == 1 ? 0.52f : 0.0f);
      if (state != 1 && g.what != 2) v = 0;   // engine off: only the fuel gauge reads
      float th = glm::radians(glm::mix(-135.0f, 135.0f, std::clamp(v, 0.0f, 1.0f)));
      DrawItem n{&k.needle, W * glm::translate(mat4(1), g.c) * glm::rotate(mat4(1), th, vec3(0, 0, 1)) * glm::scale(mat4(1), vec3(1, g.r / 0.04f, 1))};
      n.glow = glow; app.worldBoxDraws.push_back(n);
    }
  } else if (state == 1) {
    // seven-segment speed (km/h, right-aligned, three digits) and the rpm bar row
    static const uint8_t segs[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   // bits a..g
    int v = std::clamp((int)std::lround(speedKmh), 0, 999);
    float w = k.digitW, h = k.digitH, t = 0.0032f;
    for (int d = 0; d < 3; ++d) {
      int digit = (v / (d == 0 ? 1 : d == 1 ? 10 : 100)) % 10;
      if (d > 0 && v < (d == 1 ? 10 : 100)) break;
      vec3 o = k.digits + vec3(d * (w + 0.007f), 0, 0);   // tens / hundreds further to the driver's left (+x)
      // segment centres (a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle) in screen terms
      vec3 pos[7] = {vec3(0, h * 0.5f, 0), vec3(-w * 0.5f, h * 0.25f, 0), vec3(-w * 0.5f, -h * 0.25f, 0), vec3(0, -h * 0.5f, 0), vec3(w * 0.5f, -h * 0.25f, 0), vec3(w * 0.5f, h * 0.25f, 0), vec3(0)};
      for (int s = 0; s < 7; ++s) if (segs[digit] >> s & 1) {
        bool horiz = s == 0 || s == 3 || s == 6;
        vec3 p = o + vec3(pos[s].x, pos[s].y, 0), sz = horiz ? vec3(w, t, 0.0008f) : vec3(t, h * 0.5f, 0.0008f);
        DrawItem it{&k.seg, W * glm::translate(mat4(1), p) * glm::scale(mat4(1), sz)}; it.glow = glow; app.worldBoxDraws.push_back(it);
      }
    }
    int lit = (int)std::lround(std::clamp(rpm, 0.0f, 1.0f) * k.bars);
    for (int i = 0; i < lit; ++i) {
      vec3 p = k.barAt - vec3(i * 0.0075f, 0, 0);   // filling to the right as seen from the seat
      DrawItem it{&k.bar, W * glm::translate(mat4(1), p) * glm::scale(mat4(1), vec3(0.005f, 0.006f + i * 0.0012f, 0.0008f))}; it.glow = glow; app.worldBoxDraws.push_back(it);
    }
  }
}

// a part-built civilian car prop: its "dash" part and whether it is a car (wheels with rims)
struct CarInfo { bool car = false, wreck = false; vec3 anchor{0}; };
const CarInfo& carInfo(const AssetDef* a) {
  static std::map<const AssetDef*, CarInfo> cache;
  auto it = cache.find(a); if (it != cache.end()) return it->second;
  CarInfo c; bool rims = false, dash = false; vec3 dc(0), ds(0); float wheelX = -0.37f;
  for (auto& p : a->parts) {
    const std::string& mn = p.mat >= 0 && p.mat < (int)a->materials.size() ? a->materials[p.mat].name : std::string();
    if (mn == "rim") rims = true;
    if (mn == "dash") { dash = true; dc = p.center; ds = p.size; }
    if (mn == "glass_crack" || mn == "rust_h") c.wreck = true;
  }
  for (auto& p : a->parts) {   // the steering wheel: a thin rubber ring at dash height, inside the body (not a tyre)
    const std::string& mn = p.mat >= 0 && p.mat < (int)a->materials.size() ? a->materials[p.mat].name : std::string();
    if (mn == "rubber" && p.shape == Part::CYL && p.size.y < 0.06f && p.size.x < 0.5f && std::fabs(p.center.y - dc.y) < 0.4f) wheelX = p.center.x;
  }
  c.car = rims && dash && !a->raw.contains("vehicle");
  c.anchor = vec3(wheelX, dc.y + ds.y * 0.5f, dc.z - ds.z * 0.5f - 0.02f);   // on the dash top, 2 cm proud of its rear face
  return cache[a] = c;
}
}   // namespace

void vehicleDashDraw(App& app, const TankRT& t, const mat4& hull) {
  if (!t.def || !t.def->car) return;
  const TankDef& d = *t.def;
  vec3 eyeW = vec3(hull * vec4(d.driverEye, 1));
  if (glm::length(eyeW - app.eyePos()) > 14.0f) return;   // the interior is only seen up close
  json dj = d.raw.is_object() ? d.raw.value("dash", json::object()) : json::object();
  int style = d.partsAsset ? CIV : dj.value("style", std::string("military")) == "civilian" ? CIV : MIL;
  vec3 off(0, -0.24f, 0.72f); float sc = dj.value("scale", 1.0f);   // the panel's top ~18 deg under the eye: below the windscreen
  if (dj.contains("offset") && dj["offset"].is_array() && dj["offset"].size() == 3) off = vec3(dj["offset"][0].get<float>(), dj["offset"][1].get<float>(), dj["offset"][2].get<float>());
  vec3 at = d.driverEye + off;
  if (d.partsAsset) { const CarInfo& c = carInfo(d.partsAsset); if (c.car) at = c.anchor; }   // a part-built car: on its own dash
  float in = at.x > 0 ? -1.0f : 1.0f;
  bool running = app.tanks.occupied == t.id && !t.wrecked;
  int state = t.wrecked ? 2 : running ? 1 : 0;
  float kmh = std::fabs(t.speed) * 3.6f, top = std::max(1.0f, d.maxFwd * 3.6f * 1.15f);
  drawKit(app, style, in, hull * glm::translate(mat4(1), at) * glm::scale(mat4(1), vec3(sc)), state, kmh, kmh / top, running ? std::clamp(t.rpm, 0.0f, 1.0f) : 0.0f);
}

void vehicleDashFrame(App& app) {   // parked civilian car props within reach: dark (or dusty, cracked) screens and the panel
  vec3 eye = app.eyePos();
  for (auto& i : app.doc.instances) {
    vec3 dv = i.pos - eye; if (dv.x * dv.x + dv.z * dv.z > 12.0f * 12.0f) continue;
    auto rt = app.rt.find(i.id); if (rt == app.rt.end() || !rt->second.asset || app.tanks.get(i.id)) continue;   // a driven / adopted car draws its own
    const CarInfo& c = carInfo(rt->second.asset);
    if (!c.car) continue;
    mat4 W = glm::translate(mat4(1), i.pos) * glm::mat4_cast(yawQuat(i.yaw)) * glm::translate(mat4(1), c.anchor);
    drawKit(app, CIV, c.anchor.x > 0 ? -1.0f : 1.0f, W, c.wreck ? 2 : 0, 0, 0, 0);
  }
}
