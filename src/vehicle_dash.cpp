// VH6 vehicle interiors (vehicle_dash.h): one interior per vehicle model, each a small spec
// (content/vehicles/dash/<name>.json: palette + anchor offset + a list of items, optionally on a "base" spec) built from
// shared part builders into a kit once per spec, driver side and body colour. A kit holds the panel body, the parts that
// glow with the engine running (lamps, backlit buttons, screens, ambient strips; glow raised at night / underground), dark
// and dusty-cracked screen covers (parked and wrecked vehicles), the steering wheel for part-built cars, and the read-outs
// placed per frame (analogue needles; digital speed digits and rpm / fuel / temperature bars).
// Kit space: x toward the cab's middle (mirrored per driver side), y up, z forward; an item's "at" is the surface it is
// mounted on and it grows toward the driver (-z). Layers on a face are at least 0.6 mm apart (no z-fighting). Nothing is
// built or drawn for vehicles out of reach.
#include "vehicle_dash.h"
#include "app.h"
#include "tank.h"
#include "assets.h"
#include "lights.h"
#include "light_types.h"
#include "textures.h"
#include <glm/gtx/quaternion.hpp>
#include <map>

namespace {
enum Read { R_NONE = -1, R_SPEED = 0, R_RPM = 1, R_FUEL = 2, R_TEMP = 3 };
int readOf(const std::string& s) { return s == "speed" ? R_SPEED : s == "rpm" ? R_RPM : s == "fuel" ? R_FUEL : s == "temp" ? R_TEMP : R_NONE; }
struct Gauge { vec3 c; float r, sweep; int what; };   // an analogue dial: needle pivot (unmirrored kit space), radius, sweep (radians)
struct Bars { vec3 c; float w; int what; bool ring; };  // rpm round a ring, or a straight row (fuel / temp)
struct Kit {
  GpuMesh body, glow, dead, wreck, wheel, needle, seg, bar;
  std::vector<Gauge> gauges; std::vector<vec4> digits; std::vector<Bars> bars;   // digits: centre xyz, digit height
  bool hasWheel = false;
};
// ---------------------------------------------------------------- materials
Material M(const std::string& n, vec3 alb, float rough, float metal = 0, int pat = PAT_PLAIN, vec3 emi = vec3(0), bool tex = false) {
  Material m; m.name = n; m.albedo = alb; m.roughness = rough; m.metallic = metal; m.pattern = pat; m.emissive = emi; m.texCode = tex ? 0 : 1; return m;
}
Material E(const char* n, vec3 e) { return M(n, e * 0.1f, 0.3f, 0, PAT_PLAIN, e); }
vec3 jv3(const json& j, vec3 d) { return j.is_array() && j.size() == 3 ? vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : d; }
vec2 jv2(const json& j, vec2 d) { return j.is_array() && j.size() >= 2 ? vec2(j[0].get<float>(), j[1].get<float>()) : d; }
int patOf(const std::string& s) {
  return s == "metal" ? PAT_METAL : s == "fabric" ? PAT_FABRIC : s == "wood" ? PAT_WOOD : s == "painted" ? PAT_PAINTED : PAT_PLAIN;
}
struct Pal {
  std::map<std::string, Material> m;
  Pal(const json& p, vec3 paint) {
    m["black"] = M("black", vec3(0.025f), 0.6f, 0.1f); m["chrome"] = M("chrome", vec3(0.62f), 0.25f, 0.9f, PAT_METAL);
    m["steel"] = M("steel", vec3(0.26f, 0.26f, 0.25f), 0.5f, 0.75f, PAT_METAL, vec3(0), true); m["piano"] = M("piano", vec3(0.012f), 0.12f, 0.1f);
    m["face"] = M("face", vec3(0.045f, 0.047f, 0.05f), 0.5f); m["mark"] = M("mark", vec3(0.92f, 0.92f, 0.88f), 0.6f, 0, PAT_PLAIN, vec3(0.5f, 0.47f, 0.36f));
    m["stencil"] = M("stencil", vec3(0.85f, 0.84f, 0.78f), 0.8f); m["plate"] = M("plate", vec3(0.62f, 0.6f, 0.52f), 0.45f, 0.5f, PAT_METAL);
    m["ink"] = M("ink", vec3(0.05f), 0.8f); m["grime"] = M("grime", vec3(0.07f, 0.065f, 0.045f), 1.0f); m["red"] = M("red", vec3(0.55f, 0.06f, 0.04f), 0.55f, 0.1f, PAT_METAL, vec3(0), true);
    m["bakelite"] = M("bakelite", vec3(0.09f, 0.045f, 0.025f), 0.3f); m["rubber"] = M("rubber", vec3(0.03f), 0.9f);
    m["dead"] = M("dead", vec3(0.02f, 0.022f, 0.026f), 0.15f); m["dust"] = M("dust", vec3(0.22f, 0.2f, 0.17f), 0.95f); m["crack"] = M("crack", vec3(0.55f), 0.3f);
    m["lamp_off"] = M("lamp_off", vec3(0.14f, 0.05f, 0.04f), 0.3f); m["periglass"] = M("periglass", vec3(0.05f, 0.09f, 0.1f), 0.08f, 0.2f);
    m["seat"] = M("seat", vec3(0.08f), 0.85f, 0, PAT_FABRIC, vec3(0), true); m["door"] = M("door", vec3(0.1f), 0.7f); m["headliner"] = M("headliner", vec3(0.5f, 0.48f, 0.44f), 0.9f, 0, PAT_FABRIC, vec3(0), true);
    m["dash"] = M("dash", vec3(0.06f), 0.7f); m["wheel"] = M("wheel", vec3(0.03f), 0.65f);
    m["paint"] = M("paint", paint, 0.45f, 0.3f, PAT_PAINTED, vec3(0), true);
    if (p.is_object()) for (auto& [k, v] : p.items()) {
      if (v.is_array()) { m[k] = M(k, jv3(v, vec3(0.5f)), 0.7f); continue; }
      if (!v.is_object()) continue;
      vec3 rgb = jv3(v.value("rgb", json()), vec3(0.5f));
      if (v.contains("fromPaint")) rgb = glm::mix(rgb, paint * v.value("paintScale", 1.0f), v["fromPaint"].get<float>());   // follows the body colour
      m[k] = M(k, rgb, v.value("rough", 0.7f), v.value("metal", 0.0f), patOf(v.value("pat", std::string("plain"))), jv3(v.value("emi", json()), vec3(0)), v.value("tex", false));
      if (v.contains("texture")) m[k].texCode = tex::materialCode(k, m[k].pattern, v["texture"].get<std::string>());   // an image set (leather, veneer, plastic)
    }
  }
  const Material& operator()(const std::string& k) const { auto it = m.find(k); return it != m.end() ? it->second : m.at("black"); }
};
// ---------------------------------------------------------------- shared part builders
// seven segments a..g as fractions of (digit width, height), the driver's right being -x
const vec3 kSegPos[7] = {vec3(0, 0.5f, 0), vec3(-0.5f, 0.25f, 0), vec3(-0.5f, -0.25f, 0), vec3(0, -0.5f, 0), vec3(0.5f, -0.25f, 0), vec3(0.5f, 0.25f, 0), vec3(0)};
const bool kSegHoriz[7] = {true, false, false, true, false, false, true};
const uint8_t kSegs[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
vec3 dialDir(float th) { return vec3(std::sin(th), std::cos(th), 0); }   // th = sweep/2 at the scale's start (lower left as the driver sees it), 0 at the top
struct B {
  float in;
  void box(MeshData& m, vec3 c, vec3 s, const Material& mt, vec3 rotDeg = vec3(0)) {
    Part p; p.shape = Part::BOX; p.center = vec3(c.x * in, c.y, c.z); p.size = s; p.rot = glm::quat(glm::radians(vec3(rotDeg.x, rotDeg.y * in, rotDeg.z * in)));
    appendPart(m, p, mt, mat4(1));
  }
  void cyl(MeshData& m, vec3 c, float r, float depth, const Material& mt, vec3 axis = vec3(0, 0, 1), int sides = 0) {
    Part p; p.shape = Part::CYL; p.center = vec3(c.x * in, c.y, c.z); p.size = vec3(2 * r, depth, 2 * r); p.sides = sides;
    p.rot = glm::rotation(vec3(0, 1, 0), glm::normalize(vec3(axis.x * in, axis.y, axis.z)));
    appendPart(m, p, mt, mat4(1));
  }
  void ring(MeshData& m, vec3 c, float r, float w, float d, const Material& mt, int n = 24, float a0 = 0, float a1 = 360) {   // a ring of n boxes in the xy plane
    for (int i = 0; i < n; ++i) {
      float a = glm::radians(a0 + (a1 - a0) * (i + 0.5f) / n), len = 2 * r * std::sin(glm::radians((a1 - a0) / n) * 0.5f) + w * 0.6f;
      box(m, c + vec3(std::cos(a) * r, std::sin(a) * r, 0), vec3(len, w, d), mt, vec3(0, 0, glm::degrees(a) + 90));
    }
  }
  static void boxU(MeshData& m, vec3 c, vec3 s, const Material& mt, float rz = 0) {   // unmirrored (kit space already mirrored): dial marks, numerals
    Part p; p.shape = Part::BOX; p.center = c; p.size = s; p.rot = glm::angleAxis(rz, vec3(0, 0, 1)); appendPart(m, p, mt, mat4(1));
  }
  static void numeral(MeshData& m, vec3 c, float h, int n, const Material& mt) {   // seven-segment strokes, upright as the driver sees them (his right is -x)
    float w = h * 0.55f, t = h * 0.15f;
    auto one = [&](vec3 o, int d) {
      for (int s = 0; s < 7; ++s) if (kSegs[d] >> s & 1) boxU(m, o + kSegPos[s] * vec3(w, h, 0), kSegHoriz[s] ? vec3(w, t, 0.0008f) : vec3(t, h * 0.5f, 0.0008f), mt);
    };
    if (n < 10) one(c, n);
    else { one(c + vec3(w * 0.65f, 0, 0), n / 10 % 10); one(c - vec3(w * 0.65f, 0, 0), n % 10); }
  }
};

// ---------------------------------------------------------------- specs
const json& loadSpec(const std::string& name, int depth = 0) {   // cached (the reference stays valid); "base" first, then this spec over it ("add": items appended, "remove": item ids dropped)
  static std::map<std::string, json> cache;
  auto it = cache.find(name); if (it != cache.end()) return it->second;
  json j; try { j = json::parse(readTextFile(contentDir() / "vehicles" / "dash" / (name + ".json"))); } catch (...) {}
  if (!j.is_object()) { logf("dash: no interior spec %s", name.c_str()); return cache[name] = json::object(); }
  json r = j.contains("base") && depth < 4 ? loadSpec(j["base"].get<std::string>(), depth + 1) : json::object();
  if (j.contains("remove") && r.contains("items")) {   // drop the base's items by id first, so "add" can replace them
    json keep = json::array();
    for (auto& i : r["items"]) { bool drop = false; for (auto& t : j["remove"]) drop |= i.value("id", std::string()) == t.get<std::string>(); if (!drop) keep.push_back(i); }
    r["items"] = keep;
  }
  for (auto& [k, v] : j.items()) {   // ("remove" was applied above: the keys come sorted, "add" first)
    if (k == "base" || k == "remove") continue;
    if (k == "palette" && r.contains("palette")) { for (auto& [pk, pv] : v.items()) r["palette"][pk] = pv; }
    else if (k == "wheel" && r.contains("wheel")) { for (auto& [pk, pv] : v.items()) r["wheel"][pk] = pv; }
    else if (k == "add") { if (!r.contains("items")) r["items"] = json::array(); for (auto& i : v) r["items"].push_back(i); }
    else r[k] = v;
  }
  return cache[name] = r;
}

Kit buildKit(const json& spec, float in, vec3 paint) {
  Kit k; B b{in}; Pal pal(spec.value("palette", json::object()), paint);
  MeshData body, glow, dead, wreck, wheel;
  auto mat = [&](const json& i, const char* key, const char* def) -> const Material& { return pal(i.value(key, std::string(def))); };
  vec3 lampCol[5] = {vec3(2.6f, 0.18f, 0.08f), vec3(2.4f, 1.3f, 0.1f), vec3(0.3f, 2.2f, 0.45f), vec3(0.35f, 0.8f, 2.4f), vec3(2.6f, 0.18f, 0.08f)};
  // a screen face (front at at.z + dz - 0.002): live (glow), dark (parked), dusty and cracked (wreck)
  auto screen = [&](vec3 c, vec2 sz, const json& i, vec3 rot) {
    vec3 s(sz.x, sz.y, 0.002f);
    b.box(glow, c, s, E("screen", jv3(i.value("base", json()), vec3(0.03f, 0.06f, 0.13f))), rot);
    b.box(dead, c, s, pal("dead"), rot); b.box(wreck, c, s, pal("dust"), rot);
    mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot)));
    for (int n = 0; n < 3; ++n) b.box(wreck, c + vec3(R * vec4((-0.3f + n * 0.25f) * sz.x, 0, -0.0018f, 0)), vec3(0.0015f, sz.y * 0.9f, 0.0008f), pal("crack"), rot + vec3(0, 0, 30.0f - n * 25));
  };
  for (auto& i : spec.value("items", json::array())) {
    std::string t = i.value("type", std::string());
    vec3 at = jv3(i.value("at", json()), vec3(0)), rot = jv3(i.value("rot", json()), vec3(0));
    if (t == "box") b.box(body, at, jv3(i.value("size", json()), vec3(0.1f)), mat(i, "mat", "black"), rot);
    else if (t == "cyl") b.cyl(body, at, i.value("r", 0.02f), i.value("depth", 0.02f), mat(i, "mat", "black"), jv3(i.value("axis", json()), vec3(0, 0, 1)), i.value("sides", 0));
    else if (t == "glowbox") b.box(glow, at, jv3(i.value("size", json()), vec3(0.01f)), E("glowbox", jv3(i.value("glow", json()), vec3(1))), rot);
    else if (t == "gauge") {   // a raised bezel (round ring or square frame) round a recessed face with ticks and numerals, a needle cap
      float r = i.value("r", 0.045f), bw = i.value("bezelW", 0.008f), bd = i.value("bezelD", 0.016f); int n = i.value("n", 0);
      const Material& face = mat(i, "face", "face"); const Material& mk = mat(i, "mark", "mark"); const Material& bz = mat(i, "bezel", "chrome");
      if (i.value("square", false)) {
        float s = 2 * r + 2 * bw;
        for (int e = 0; e < 4; ++e) { bool hz = e < 2; b.box(body, at + vec3(hz ? 0 : (e == 2 ? 1 : -1) * (r + bw * 0.5f), hz ? (e == 0 ? 1 : -1) * (r + bw * 0.5f) : 0, -bd * 0.5f), hz ? vec3(s, bw, bd) : vec3(bw, s, bd), bz); }
        b.box(body, at + vec3(0, 0, -0.001f), vec3(2 * r + 0.002f, 2 * r + 0.002f, 0.002f), face);
      } else {
        b.ring(body, at + vec3(0, 0, -bd * 0.5f), r + bw * 0.5f, bw, bd, bz, 28);   // kit space: ring() mirrors
        b.cyl(body, at + vec3(0, 0, -0.001f), r + 0.002f, 0.002f, face, vec3(0, 0, 1), 28);
      }
      int nt = n ? n * 2 : 8; float sweep = glm::radians(i.value("sweep", 270.0f));
      vec3 cu(at.x * in, at.y, at.z - 0.0034f);   // the marks' plane, unmirrored: the scale runs clockwise from lower left as the driver sees it
      for (int s = 0; s <= nt; ++s) {
        float th = sweep * 0.5f - sweep * s / nt;
        B::boxU(body, cu + dialDir(th) * (r * 0.84f), vec3(s % 2 ? 0.0028f : 0.0038f, s % 2 ? r * 0.12f : r * 0.22f, 0.0008f), mk, -th);
        if (n && s % 2 == 0) B::numeral(body, cu + dialDir(th) * (r * 0.56f), r * 0.26f, i.value("num0", 0) + i.value("numStep", 1) * (s / 2), mk);
      }
      if (i.value("redline", 0.0f) > 0) for (int s = 0; s < 5; ++s) {   // a red arc over the last part of the scale
        float th = -sweep * 0.5f + sweep * i.value("redline", 0.0f) * (s + 0.5f) / 5;
        B::boxU(body, cu + dialDir(th) * (r * 0.93f), vec3(r * 0.07f, 0.004f, 0.0008f), pal("red"), -th);
      }
      b.cyl(body, at + vec3(0, 0, -0.0075f), 0.006f, 0.003f, pal("black"), vec3(0, 0, 1), 12);
      for (int c = 0; c < 2; ++c) b.box(wreck, at + vec3(-0.3f * r + c * 0.5f * r, 0.1f * r, -0.0105f), vec3(0.0012f, r * 1.5f, 0.0008f), pal("crack"), vec3(0, 0, 35.0f - c * 70));
      int w = readOf(i.value("read", std::string()));
      if (w != R_NONE) k.gauges.push_back({vec3(at.x * in, at.y, at.z - 0.0052f), r, sweep, w});   // the needle 1 mm over the marks
    } else if (t == "digital") {   // a digital cluster: the screen, optional rings, live speed digits and rpm / fuel / temperature bars
      vec2 sz = jv2(i.value("size", json()), vec2(0.3f, 0.07f));
      screen(at + vec3(0, 0, -0.001f), sz, i, vec3(0));
      vec3 f(at.x * in, at.y, at.z - 0.0026f);   // the read-outs' plane
      bool rings = i.value("rings", false);
      if (rings) for (int s = 0; s < 2; ++s)   // speed ring on the driver's left, rpm on his right
        b.ring(glow, at + vec3((s ? -1 : 1) * sz.x * 0.25f, 0, -0.0026f), sz.y * 0.4f, 0.0022f, 0.0008f, E("ring", s ? vec3(1.6f, 0.6f, 0.2f) : vec3(0.5f, 0.9f, 1.6f)), 28, -45, 225);
      k.digits.push_back(vec4(f + vec3((rings ? sz.x * 0.25f : 0.0f) * in, sz.y * 0.05f, 0), sz.y * 0.3f));
      if (rings) k.bars.push_back({f + vec3(-sz.x * 0.25f * in, 0, 0), sz.y * 0.4f, R_RPM, true});
      else k.bars.push_back({f + vec3(0, -sz.y * 0.3f, 0), sz.x * 0.6f, R_RPM, false});
      k.bars.push_back({f + vec3(sz.x * 0.42f * in, -sz.y * 0.36f, 0), sz.x * 0.1f, R_FUEL, false});
      k.bars.push_back({f + vec3(-sz.x * 0.42f * in, -sz.y * 0.36f, 0), sz.x * 0.1f, R_TEMP, false});
      int tt = i.value("telltales", 6);   // indicator icons along the top edge
      for (int n = 0; n < tt; ++n) b.box(glow, vec3(at.x + (n - (tt - 1) * 0.5f) * sz.x * 0.06f, at.y + sz.y * 0.38f, at.z - 0.0026f), vec3(sz.x * 0.025f, sz.y * 0.07f, 0.0008f), E("tell", lampCol[n % 5] * 0.6f));
    } else if (t == "screen") {   // a display with our own simple UI: map (navigation), mfd (vehicle systems), radio, camera
      vec2 sz = jv2(i.value("size", json()), vec2(0.25f, 0.15f));
      mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot)));
      auto off = [&](float x, float y, float z) { return vec3(R * vec4(x, y, z, 0)); };
      float dz = 0;
      if (i.contains("bezel")) { b.box(body, at + off(0, 0, -0.0015f), vec3(sz.x + 0.03f, sz.y + 0.025f, 0.003f), mat(i, "bezel", "piano"), rot); dz = -0.003f; }
      screen(at + off(0, 0, dz - 0.001f), sz, i, rot);
      auto el = [&](vec2 o, vec2 s, vec3 e, float rz = 0) { b.box(glow, at + off(o.x * sz.x, o.y * sz.y, dz - 0.0026f), vec3(s.x * sz.x, s.y * sz.y, 0.0008f), E("ui", e), rot + vec3(0, 0, rz)); };
      std::string ui = i.value("ui", std::string("map"));
      vec3 hi = jv3(i.value("accent", json()), vec3(0.25f, 0.8f, 2.4f)), dim(0.45f, 0.5f, 0.58f), road(0.55f, 0.6f, 0.68f);
      el(vec2(0, 0.44f), vec2(0.98f, 0.1f), dim * 0.6f);
      if (ui == "map") {
        el(vec2(-0.05f, 0), vec2(0.7f, 0.03f), road, 8); el(vec2(0.15f, 0.08f), vec2(0.014f, 0.6f), road);
        el(vec2(0.3f, -0.15f), vec2(0.4f, 0.025f), road, -20); el(vec2(0.32f, 0.2f), vec2(0.18f, 0.2f), vec3(0.2f, 0.7f, 0.3f));
        el(vec2(0.02f, 0), vec2(0.3f, 0.04f), hi, 8); el(vec2(0.15f, 0.16f), vec2(0.02f, 0.3f), hi);
        el(vec2(-0.12f, -0.03f), vec2(0.045f, 0.075f), vec3(2.2f), 45);
        for (int n = 0; n < 4; ++n) el(vec2(-0.45f, 0.22f - n * 0.17f), vec2(0.07f, 0.11f), dim);
      } else if (ui == "mfd") {   // a systems page: softkey labels down both sides, a grid, bar gauges, a caution box
        for (int n = 0; n < 5; ++n) { el(vec2(-0.45f, 0.28f - n * 0.15f), vec2(0.06f, 0.07f), dim); el(vec2(0.45f, 0.28f - n * 0.15f), vec2(0.06f, 0.07f), dim); }
        for (int n = 0; n < 3; ++n) el(vec2(-0.2f + n * 0.2f, -0.28f + 0.03f * n), vec2(0.12f, 0.08f + n * 0.06f), hi);
        el(vec2(0, 0.08f), vec2(0.6f, 0.012f), hi * 0.6f); el(vec2(0, 0.08f), vec2(0.012f, 0.45f), hi * 0.6f);
        el(vec2(0.18f, 0.2f), vec2(0.12f, 0.1f), vec3(2.2f, 0.9f, 0.1f));
      } else if (ui == "radio") {
        el(vec2(0, 0.08f), vec2(0.6f, 0.25f), hi);
        for (int n = 0; n < 6; ++n) el(vec2(-0.4f + n * 0.16f, -0.3f), vec2(0.12f, 0.14f), dim);
      } else if (ui == "camera") {   // a camera feed: grey ground, a horizon, guide lines
        el(vec2(0, -0.12f), vec2(0.95f, 0.55f), vec3(0.22f, 0.24f, 0.23f)); el(vec2(0, 0.2f), vec2(0.95f, 0.012f), vec3(0.6f));
        el(vec2(-0.25f, -0.2f), vec2(0.02f, 0.45f), vec3(2.0f, 1.6f, 0.2f), -15); el(vec2(0.25f, -0.2f), vec2(0.02f, 0.45f), vec3(2.0f, 1.6f, 0.2f), 15);
      }
    } else if (t == "lamps") {   // indicator / warning lamps on an optional plate: lit ones glow with the engine running
      int rows = i.value("rows", 2), cols = i.value("cols", 5); vec2 pt = jv2(i.value("pitch", json()), vec2(0.027f, 0.03f)); float r = i.value("r", 0.0072f);
      float pz = 0;
      if (i.contains("plate")) { b.box(body, at + vec3((cols - 1) * pt.x * 0.5f, -(rows - 1) * pt.y * 0.5f, -0.002f), vec3(cols * pt.x + 0.016f, rows * pt.y + 0.014f, 0.004f), mat(i, "plate", "red")); pz = -0.004f; }
      for (int rr = 0; rr < rows; ++rr) for (int c = 0; c < cols; ++c) {
        vec3 p = at + vec3(c * pt.x, -rr * pt.y, pz);
        b.cyl(body, p + vec3(0, 0, -0.003f), r + 0.0023f, 0.006f, mat(i, "bezel", "chrome"), vec3(0, 0, 1), 12);
        b.cyl(body, p + vec3(0, 0, -0.005f), r, 0.004f, pal("lamp_off"), vec3(0, 0, 1), 12);
        vec3 lc = i.contains("color") ? jv3(i["color"], vec3(1)) : lampCol[(c + rr) % 5];
        if ((rr * cols + c) % 3 != 2) b.cyl(glow, p + vec3(0, 0, -0.0068f), r * 0.95f, 0.002f, M("lamp", lc * 0.12f, 0.3f, 0, PAT_PLAIN, lc), vec3(0, 0, 1), 12);
      }
    } else if (t == "toggles") {   // bat toggles on a backing strip, optionally guarded, stencilled underneath
      int rows = i.value("rows", 1), cols = i.value("cols", 6); vec2 pt = jv2(i.value("pitch", json()), vec2(0.046f, 0.075f));
      for (int rr = 0; rr < rows; ++rr) {
        vec3 rowAt = at + vec3(0, -rr * pt.y, 0); float pz = 0;
        if (i.value("recess", false)) { b.box(body, rowAt + vec3((cols - 1) * pt.x * 0.5f, -0.006f, -0.0015f), vec3(cols * pt.x + 0.01f, 0.056f, 0.003f), mat(i, "strip", "black")); pz = -0.003f; }
        for (int c = 0; c < cols; ++c) {
          vec3 p = rowAt + vec3(c * pt.x, 0, pz);
          b.box(body, p + vec3(0, 0, -0.003f), vec3(0.018f, 0.024f, 0.006f), mat(i, "base", "steel"));
          if (i.value("guard", false)) for (float dx : {-0.0125f, 0.0125f}) b.box(body, p + vec3(dx, 0.002f, -0.01f), vec3(0.003f, 0.028f, 0.02f), mat(i, "guardMat", "steel"));
          b.cyl(body, p + vec3(0, (c + rr) % 3 == 0 ? -0.004f : 0.004f, -0.016f), 0.0028f, 0.024f, mat(i, "bat", "chrome"), vec3(0, ((c + rr) % 3 == 0 ? -0.55f : 0.55f), -1.0f), 8);
          if (i.value("stencil", true)) for (int l = 0; l < 3; ++l) b.box(body, p + vec3(-0.008f + l * 0.008f, -0.024f, -0.0006f), vec3(0.0055f, 0.0035f, 0.0008f), pal("stencil"));
        }
      }
    } else if (t == "knobs" || t == "pullknobs") {   // rotary knobs with a pointer line, or period pull switches on a short shaft
      int n = i.value("n", 4); vec2 pt = jv2(i.value("pitch", json()), vec2(0.06f, 0)); float r = i.value("r", 0.014f);
      for (int c = 0; c < n; ++c) {
        vec3 p = at + vec3(c * pt.x, c * pt.y, 0);
        if (t == "pullknobs") { b.cyl(body, p + vec3(0, 0, -0.01f), 0.004f, 0.02f, mat(i, "shaft", "chrome"), vec3(0, 0, 1), 8); b.cyl(body, p + vec3(0, 0, -0.026f), r, 0.014f, mat(i, "mat", "bakelite"), vec3(0, 0, 1), 0); }
        else {
          if (i.contains("ring")) b.cyl(body, p + vec3(0, 0, -0.0015f), r + 0.005f, 0.003f, mat(i, "ring", "steel"), vec3(0, 0, 1), 0);
          b.cyl(body, p + vec3(0, 0, -0.01f), r, 0.016f, mat(i, "mat", "black"), vec3(0, 0, 1), i.value("sides", 0));
          b.box(body, p + vec3(0, r * 0.55f, -0.0184f), vec3(0.0022f, r * 0.7f, 0.0008f), pal("mark"));
        }
      }
    } else if (t == "buttons") {   // push buttons whose legends glow (backlit)
      int rows = i.value("rows", 1), cols = i.value("cols", 4); vec2 pt = jv2(i.value("pitch", json()), vec2(0.04f, 0.03f)), s = jv2(i.value("size", json()), vec2(0.032f, 0.02f));
      vec3 g = jv3(i.value("glow", json()), vec3(1.0f, 1.0f, 0.95f)); bool full = i.value("full", false);
      for (int rr = 0; rr < rows; ++rr) for (int c = 0; c < cols; ++c) {
        vec3 p = at + vec3(c * pt.x, -rr * pt.y, 0);
        b.box(body, p + vec3(0, 0, -0.004f), vec3(s.x, s.y, 0.008f), mat(i, "mat", "black"));
        b.box(glow, p + vec3(0, 0, -0.0088f), vec3(s.x * (full ? 0.8f : 0.3f), s.y * (full ? 0.7f : 0.18f), 0.0008f), E("btn", g * (full && (c + rr) % 3 == 1 ? 0.3f : 1.0f)));
      }
    } else if (t == "plates") {   // riveted data plates with stamped lines
      int n = i.value("n", 3); vec2 pt = jv2(i.value("pitch", json()), vec2(0.07f, 0)), s = jv2(i.value("size", json()), vec2(0.06f, 0.036f));
      for (int c = 0; c < n; ++c) {
        vec3 p = at + vec3(c * pt.x, c * pt.y, 0);
        b.box(body, p + vec3(0, 0, -0.0015f), vec3(s.x, s.y, 0.002f), mat(i, "mat", "plate"));
        for (float dx : {-0.42f, 0.42f}) b.cyl(body, p + vec3(dx * s.x, 0.36f * s.y, -0.0035f), 0.0022f, 0.002f, pal("steel"), vec3(0, 0, 1), 6);
        for (int l = 0; l < 3; ++l) b.box(body, p + vec3(0, s.y * (0.12f - l * 0.24f), -0.0034f), vec3(s.x * (0.7f - l * 0.12f), 0.0025f, 0.0008f), mat(i, "ink", "ink"));
      }
    } else if (t == "radio") {   // a stacked radio set (at = the case centre): case, lit window, knobs, bolts
      int n = i.value("n", 1); vec3 s = jv3(i.value("size", json()), vec3(0.3f, 0.1f, 0.18f));
      for (int rr = 0; rr < n; ++rr) {
        vec3 p = at + vec3(0, -rr * (s.y + 0.015f), 0); float f = -s.z * 0.5f;
        b.box(body, p, s, mat(i, "mat", "steel"));
        b.box(body, p + vec3(-s.x * 0.17f, s.y * 0.15f, f - 0.001f), vec3(s.x * 0.41f, s.y * 0.32f, 0.002f), pal("black"));
        b.box(glow, p + vec3(-s.x * 0.17f, s.y * 0.15f, f - 0.0026f), vec3(s.x * 0.37f, s.y * 0.26f, 0.0008f), E("lcd", jv3(i.value("lcd", json()), vec3(0.9f, 1.8f, 0.3f))));
        for (int c = 0; c < 4; ++c) b.cyl(body, p + vec3(-s.x * 0.37f + c * s.x * 0.22f, -s.y * 0.25f, f - 0.008f), 0.011f, 0.016f, pal("black"), vec3(0, 0, 1), 0);
        for (int c = 0; c < 2; ++c) b.cyl(body, p + vec3(s.x * 0.3f, s.y * (0.22f - c * 0.3f), f - 0.006f), 0.006f, 0.012f, pal("chrome"), vec3(0, 0, 1), 8);
        for (int c = 0; c < 4; ++c) b.cyl(body, p + vec3((c % 2 ? 1 : -1) * s.x * 0.46f, (c / 2 ? 1 : -1) * s.y * 0.4f, f - 0.002f), 0.004f, 0.004f, pal("steel"), vec3(0, 0, 1), 6);
      }
    } else if (t == "vent") {   // a round vent: ring, dark bowl, slats, hub
      float r = i.value("r", 0.03f); const Material& rm = mat(i, "ring", "chrome");
      b.cyl(body, at + vec3(0, 0, -0.004f), r + 0.006f, 0.008f, rm, vec3(0, 0, 1), 0);
      b.cyl(body, at + vec3(0, 0, -0.005f), r, 0.008f, pal("piano"), vec3(0, 0, 1), 0);
      for (int s = -1; s <= 1; ++s) b.box(body, at + vec3(0, s * r * 0.42f, -0.0105f), vec3(r * 1.7f, 0.003f, 0.002f), rm);
      b.cyl(body, at + vec3(0, 0, -0.013f), r * 0.22f, 0.004f, rm, vec3(0, 0, 1), 0);
    } else if (t == "ventrect") {   // a rectangular vent with angled vanes
      vec2 s = jv2(i.value("size", json()), vec2(0.12f, 0.05f));
      b.box(body, at + vec3(0, 0, -0.003f), vec3(s.x + 0.012f, s.y + 0.012f, 0.006f), mat(i, "ring", "black"), rot);
      b.box(body, at + vec3(0, 0, -0.0045f), vec3(s.x, s.y, 0.008f), pal("piano"), rot);
      for (int v = 0; v < 4; ++v) b.box(body, at + vec3(0, -s.y * 0.38f + v * s.y * 0.25f, -0.01f), vec3(s.x * 0.96f, 0.003f, 0.006f), mat(i, "vane", "black"), rot + vec3(-20, 0, 0));
    } else if (t == "lever") {   // levers rising from the floor / tunnel (gear, transfer case, 4x4, handbrake) with knobs on top
      int n = i.value("n", 1); float pt = jv2(i.value("pitch", json()), vec2(0.055f, 0)).x, len = i.value("len", 0.3f);
      vec3 ax = jv3(i.value("axis", json()), vec3(0, 1, -0.25f));
      for (int c = 0; c < n; ++c) {
        vec3 base = at + vec3(c * pt, 0, 0), a = glm::normalize(ax + vec3(0, 0, -0.1f * c)), aw = vec3(a.x, a.y, a.z);
        b.cyl(body, base + aw * (len * 0.5f), 0.007f, len, mat(i, "mat", "steel"), a, 8);
        b.cyl(body, base + aw * len, i.value("knobR", 0.018f), 0.03f, c == 1 && i.contains("knob2") ? mat(i, "knob2", "red") : mat(i, "knob", "black"), a, 10);
        if (i.value("boot", true)) b.box(body, base, vec3(0.05f, 0.03f, 0.08f), mat(i, "bootMat", "rubber"));
      }
    } else if (t == "shifter") {   // an automatic selector (lit P R N D) or a manual knob on a boot (at = the console top)
      b.box(body, at + vec3(0, 0.006f, 0), vec3(0.07f, 0.012f, 0.14f), mat(i, "gate", "piano"));
      b.cyl(body, at + vec3(0, 0.05f, 0.01f), 0.007f, 0.08f, pal("chrome"), vec3(0, 1, -0.15f), 8);
      b.cyl(body, at + vec3(0, 0.095f, 0.004f), i.value("knobR", 0.022f), 0.05f, mat(i, "mat", "black"), vec3(0, 1, -0.15f), 0);
      if (i.value("prnd", true)) for (int n = 0; n < 4; ++n) b.box(glow, at + vec3(0.045f, 0.0128f, -0.05f + n * 0.03f), vec3(0.008f, 0.0008f, 0.01f), E("prnd", n == 3 ? vec3(2.0f) : vec3(0.4f)));
    } else if (t == "colshift") {   // a column shift stalk from the wheel's column toward the driver's right
      vec3 ax = jv3(i.value("axis", json()), vec3(1, -0.15f, -0.5f)); float len = i.value("len", 0.2f);
      b.cyl(body, at + glm::normalize(ax) * (len * 0.5f), 0.006f, len, mat(i, "mat", "chrome"), ax, 8);
      b.cyl(body, at + glm::normalize(ax) * len, 0.013f, 0.04f, mat(i, "knob", "black"), ax, 0);
      b.box(glow, at + vec3(0.08f, 0.04f, -0.003f), vec3(0.05f, 0.008f, 0.0008f), E("prnd", vec3(1.4f, 1.0f, 0.4f)));
    } else if (t == "bolts") {
      int n = i.value("n", 6); vec2 pt = jv2(i.value("pitch", json()), vec2(0.1f, 0));
      for (int c = 0; c < n; ++c) b.cyl(body, at + vec3(c * pt.x, c * pt.y, -0.003f), i.value("r", 0.0055f), 0.006f, mat(i, "mat", "steel"), vec3(0, 0, 1), 6);
    } else if (t == "stencil") {   // a stencilled label (dashes) or grime streak
      int n = i.value("n", 6); vec2 s = jv2(i.value("size", json()), vec2(0.012f, 0.004f));
      for (int c = 0; c < n; ++c) b.box(body, at + vec3(c * s.x * 1.6f, 0, -0.0006f), vec3(s.x, s.y, 0.0008f), mat(i, "mat", "stencil"));
    } else if (t == "strip") {   // an ambient light strip (glows with the engine running)
      b.box(glow, at, vec3(i.value("len", 1.0f), i.value("w", 0.0025f), 0.0008f), E("ambient", jv3(i.value("glow", json()), vec3(1.4f, 0.65f, 0.25f))), rot);
    } else if (t == "periscope") {   // an armoured vision block (at = its centre): steel frame, dark glass toward the driver
      vec3 s = jv3(i.value("size", json()), vec3(0.2f, 0.07f, 0.12f));
      b.box(body, at, s, mat(i, "mat", "steel"), rot);
      mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot)));
      b.box(body, at + vec3(R * vec4(0, 0, -s.z * 0.5f - 0.001f, 0)), vec3(s.x * 0.86f, s.y * 0.7f, 0.002f), pal("periglass"), rot);
    } else if (t == "grab") {   // a grab handle: a bar on two posts standing off the surface
      float len = i.value("len", 0.3f); const Material& gm = mat(i, "mat", "black");
      b.cyl(body, at + vec3(0, 0, -0.05f), 0.012f, len, gm, vec3(1, 0, 0), 10);
      for (float dx : {-0.45f, 0.45f}) b.cyl(body, at + vec3(dx * len, 0, -0.025f), 0.01f, 0.05f, gm, vec3(0, 0, 1), 8);
    } else if (t == "seat") {   // a seat (at = the cushion's front edge, top): cushion, back, headrest, runner
      float w = i.value("w", 0.5f); const Material& sm = mat(i, "mat", "seat");
      b.box(body, at + vec3(0, -0.07f, -0.25f), vec3(w, 0.14f, 0.5f), sm);
      b.box(body, at + vec3(0, 0.29f, -0.56f), vec3(w * 0.96f, 0.62f, 0.12f), sm, vec3(-12, 0, 0));
      if (i.value("headrest", true)) b.box(body, at + vec3(0, 0.68f, -0.64f), vec3(w * 0.5f, 0.16f, 0.09f), sm, vec3(-12, 0, 0));
      b.box(body, at + vec3(0, -0.2f, -0.25f), vec3(w * 0.5f, 0.12f, 0.4f), pal("black"));
    } else if (t == "doorcard") {   // a door's inner panel (at = its centre, x on the door): card, insert, armrest, chrome pull
      vec3 s = jv3(i.value("size", json()), vec3(0.03f, 0.5f, 1.0f)); float o = at.x > 0 ? -1.0f : 1.0f;   // toward the cab's middle
      b.box(body, at, s, mat(i, "mat", "door"));
      b.box(body, at + vec3(o * (s.x * 0.5f + 0.002f), -s.y * 0.1f, 0), vec3(0.004f, s.y * 0.4f, s.z * 0.8f), mat(i, "insert", "seat"));
      b.box(body, at + vec3(o * (s.x * 0.5f + 0.03f), -s.y * 0.05f + 0.02f, -0.05f), vec3(0.06f, 0.04f, s.z * 0.6f), mat(i, "mat", "door"));
      b.box(body, at + vec3(o * (s.x * 0.5f + 0.008f), s.y * 0.2f, s.z * 0.3f), vec3(0.012f, 0.02f, 0.1f), pal("chrome"));
    }
  }
  // the steering wheel for cars whose model has none (part-built cars): rim, spokes, hub, spoke buttons, column shroud;
  // local y is the column axis (toward the driver), as the asset's wheel marker
  if (spec.contains("wheel")) {
    const json& w = spec["wheel"]; float R = w.value("r", 0.185f), th = w.value("rim", 0.028f);
    const Material& rimM = pal(w.value("mat", std::string("wheel"))); const Material& hubM = pal(w.value("hubMat", std::string("wheel")));
    int segs = 28;
    for (int i = 0; i < segs; ++i) {
      float a = glm::radians(360.0f * (i + 0.5f) / segs), len = 2 * R * std::sin(glm::radians(180.0f / segs)) + th * 0.5f;
      Part p; p.shape = Part::BOX; p.center = vec3(std::cos(a) * R, 0, std::sin(a) * R); p.size = vec3(th, th * 1.05f, len); p.rot = glm::angleAxis(-a, vec3(0, 1, 0));
      appendPart(wheel, p, rimM, mat4(1));
    }
    for (auto& sa : w.value("spokes", json::array({0, 180, 270}))) {
      float r = glm::radians(sa.get<float>());
      Part p; p.shape = Part::BOX; p.center = vec3(std::cos(r) * R * 0.55f, 0.012f, std::sin(r) * R * 0.55f); p.size = vec3(R * 0.92f, 0.014f, w.value("spokeW", 0.034f)); p.rot = glm::angleAxis(-r, vec3(0, 1, 0));
      appendPart(wheel, p, hubM, mat4(1));
      if (w.value("buttons", false) && std::fabs(std::sin(r)) < 0.5f) for (int j = 0; j < 2; ++j) {
        Part q; q.shape = Part::BOX; q.center = vec3(std::cos(r) * R * (0.42f + j * 0.16f), 0.0198f, 0); q.size = vec3(0.022f, 0.002f, 0.018f); appendPart(wheel, q, pal("chrome"), mat4(1));
      }
    }
    float hr = w.value("hub", 0.06f);
    Part h; h.shape = Part::CYL; h.center = vec3(0, 0.02f, 0); h.size = vec3(hr * 2, 0.04f, hr * 1.7f); appendPart(wheel, h, hubM, mat4(1));
    if (w.contains("badge")) { Part g; g.shape = Part::CYL; g.center = vec3(0, 0.0406f, 0); g.size = vec3(hr * 0.7f, 0.0012f, hr * 0.6f); appendPart(wheel, g, pal(w["badge"].get<std::string>()), mat4(1)); }
    Part c; c.shape = Part::CYL; c.center = vec3(0, -0.07f, 0); c.size = vec3(0.06f, 0.1f, 0.06f); appendPart(wheel, c, pal("piano"), mat4(1));
    k.hasWheel = true;
  }
  k.body.upload(body); k.glow.upload(glow); k.dead.upload(dead); k.wreck.upload(wreck); k.wheel.upload(wheel);
  MeshData nd; B one{1}; vec3 nc = jv3(spec.value("needle", json()), vec3(0.95f, 0.93f, 0.88f));
  one.box(nd, vec3(0, 0.018f, 0), vec3(0.0036f, 0.04f, 0.0012f), M("needle", nc, 0.4f, 0, PAT_PLAIN, nc * 0.6f));
  one.box(nd, vec3(0, -0.006f, 0), vec3(0.004f, 0.012f, 0.0012f), M("needle_tail", nc, 0.4f));
  k.needle.upload(nd);
  MeshData sd; one.box(sd, vec3(0), vec3(1), E("seg", jv3(spec.value("digitGlow", json()), vec3(1.6f, 1.8f, 2.0f)))); k.seg.upload(sd);
  MeshData bd; one.box(bd, vec3(0), vec3(1), E("bar", jv3(spec.value("barGlow", json()), vec3(1.8f, 0.7f, 0.25f)))); k.bar.upload(bd);
  return k;
}

const Kit& kitFor(const std::string& name, const json& spec, float in, vec3 paint) {
  static std::map<std::string, Kit> kits;
  char key[200]; std::snprintf(key, sizeof key, "%s|%d|%.2f,%.2f,%.2f", name.c_str(), in > 0 ? 1 : 0, paint.x, paint.y, paint.z);
  auto it = kits.find(key); if (it != kits.end()) return it->second;
  Kit& k = kits[key] = buildKit(spec, in, paint);
  tex::flushPending();   // image sets the spec's palette names (leather, veneer, plastic) load now, not with the library
  return k;
}

// cabin fill: sky light through the glass by day (the shell's shadow and the probes leave a closed cab near black), the
// screens' and dials' faint glow by night; one soft point light per interior drawn this frame, no shadow
struct CabinLight { vec3 pos{0}; float day = 0, glow = 0; int frame = 0; };
std::map<uint64_t, CabinLight> gCabin; int gFrame = 0;
void cabinLight(App& app, uint64_t id, const mat4& W, vec3 kitPos, bool running) {
  CabinLight& c = gCabin[id];
  c.pos = vec3(W * vec4(kitPos, 1)); c.frame = gFrame;
  c.day = app.R.wx.enclosed ? 0.0f : glm::smoothstep(0.3f, 0.8f, lightsDaylight(app));   // moonlight is no fill c.glow = running ? 1.0f : 0.0f;
}
float glowNow(App& app) { return 1.0f + 1.6f * (app.R.wx.enclosed ? 1.0f : 1.0f - std::clamp(lightsDaylight(app), 0.0f, 1.0f)); }

// one interior: W = the anchor frame; state 0 parked, 1 running, 2 wreck; v = speed (dial fraction), rpm, fuel, temp; wheelM = the drawn wheel's frame
void drawKit(App& app, const Kit& k, const mat4& W, int state, const float v[4], float kmh, const mat4* wheelM) {
  float glow = glowNow(app);
  DrawItem bi{&k.body, W}; bi.glow = state == 1 ? glow : 0.0f; app.worldBoxDraws.push_back(bi);   // the dial marks are backlit only with the engine on
  if (state == 1) { DrawItem g{&k.glow, W}; g.glow = glow; app.worldBoxDraws.push_back(g); }
  else app.worldBoxDraws.push_back({state == 2 ? &k.wreck : &k.dead, W});
  if (k.hasWheel && wheelM) app.worldBoxDraws.push_back({&k.wheel, *wheelM});
  for (auto& g : k.gauges) {
    float x = state == 1 || g.what == R_FUEL ? v[g.what] : 0.0f;   // engine off: only the fuel gauge reads
    float th = g.sweep * 0.5f - g.sweep * std::clamp(x, 0.0f, 1.0f);
    DrawItem n{&k.needle, W * glm::translate(mat4(1), g.c) * glm::rotate(mat4(1), -th, vec3(0, 0, 1)) * glm::scale(mat4(1), vec3(1, g.r / 0.048f, 1))};
    n.glow = state == 1 ? glow : 0.0f; app.worldBoxDraws.push_back(n);
  }
  if (state != 1) return;
  int val = std::clamp((int)std::lround(kmh), 0, 999);
  for (auto& d : k.digits) {   // km/h, up to three digits centred on the tens; the ones on the driver's right (-x)
    float h = d.w, w = h * 0.55f, t = h * 0.14f, pitch = w + h * 0.3f;
    for (int n = 0; n < 3; ++n) {
      if (n > 0 && val < (n == 1 ? 10 : 100)) break;
      int digit = (val / (n == 0 ? 1 : n == 1 ? 10 : 100)) % 10;
      vec3 o = vec3(d) + vec3((n - 1) * pitch, 0, 0);
      for (int s = 0; s < 7; ++s) if (kSegs[digit] >> s & 1) {
        DrawItem it{&k.seg, W * glm::translate(mat4(1), o + kSegPos[s] * vec3(w, h, 0)) * glm::scale(mat4(1), kSegHoriz[s] ? vec3(w, t, 0.0008f) : vec3(t, h * 0.5f, 0.0008f))};
        it.glow = glow; app.worldBoxDraws.push_back(it);
      }
    }
  }
  for (auto& br : k.bars) {
    float x = std::clamp(v[br.what], 0.0f, 1.0f);
    if (br.ring) {   // round the ring, clockwise from lower left as the driver sees it
      int lit = (int)std::lround(x * 14);
      for (int n = 0; n < lit; ++n) {
        float th = glm::radians(135.0f - 270.0f * (n + 0.5f) / 14);
        DrawItem it{&k.bar, W * glm::translate(mat4(1), br.c + dialDir(th) * br.w * 0.78f) * glm::rotate(mat4(1), -th, vec3(0, 0, 1)) * glm::scale(mat4(1), vec3(br.w * 0.12f, br.w * 0.26f, 0.0008f))};
        it.glow = glow; app.worldBoxDraws.push_back(it);
      }
    } else {   // a straight row filling from the left as the driver sees it
      int nb = br.what == R_RPM ? 16 : 6, lit = (int)std::lround(x * nb); float step = br.w / nb;
      for (int n = 0; n < lit; ++n) {
        DrawItem it{&k.bar, W * glm::translate(mat4(1), br.c + vec3(br.w * 0.5f - (n + 0.5f) * step, 0, 0)) * glm::scale(mat4(1), vec3(step * 0.7f, br.what == R_RPM ? 0.006f : 0.004f, 0.0008f))};
        it.glow = glow; app.worldBoxDraws.push_back(it);
      }
    }
  }
}

// a part-built car (BR car / car-pack copy): its dash (the biggest "dash" part), the steering wheel marker, paint, whether
// it is a car and wrecked, the interior spec its id names; a car-pack copy (drawn as its model, which has its own seats
// and dash) anchors where its spec's "model" fit says
struct CarInfo { bool car = false, wreck = false; vec3 anchor{0}, wheelC{0}, paint{0.5f}; quat wheelQ{1, 0, 0, 0}; std::string spec; };
std::string specForAsset(const std::string& id) {   // mod_pcar_<kind>[_wreck] -> pcar_<kind>; mod_<body>[_red|_green|_wrecked|_burned] -> car_<body>
  std::string s = id.rfind("mod_", 0) == 0 ? id.substr(4) : id;
  for (const char* suf : {"_wreck", "_wrecked", "_burned", "_red", "_green"}) { size_t n = std::strlen(suf); if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) s = s.substr(0, s.size() - n); }
  return s.rfind("pcar_", 0) == 0 ? s : "car_" + s;
}
const CarInfo& carInfo(const AssetDef* a) {
  static std::map<const AssetDef*, CarInfo> cache;
  auto it = cache.find(a); if (it != cache.end()) return it->second;
  CarInfo& c = cache[a]; bool rims = false, dash = false, wheel = false; vec3 dc(0), ds(0); float wheelX = -0.37f;
  auto matName = [&](const Part& p) -> std::string { return p.mat >= 0 && p.mat < (int)a->materials.size() ? a->materials[p.mat].name : std::string(); };
  for (auto& p : a->parts) {
    std::string mn = matName(p);
    if (mn == "rim") rims = true;
    if (mn == "dash" && p.size.x * p.size.y * p.size.z > ds.x * ds.y * ds.z) { dash = true; dc = p.center; ds = p.size; }
    if (mn == "glass_crack" || mn == "rust_h" || mn == "burnt") c.wreck = true;
  }
  for (auto& p : a->parts)
    if (matName(p) == "rubber" && p.shape == Part::CYL && p.size.y < 0.06f && p.size.x < 0.5f && std::fabs(p.center.y - dc.y) < 0.4f) { wheelX = p.center.x; c.wheelC = p.center; c.wheelQ = p.rot; wheel = true; }
  for (auto& m : a->materials) if (m.name.rfind("car_", 0) == 0) { c.paint = m.albedo; break; }   // the body colour
  c.car = rims && dash && !a->raw.contains("vehicle");
  c.anchor = vec3(wheelX, dc.y + ds.y * 0.5f, dc.z - ds.z * 0.5f - 0.005f);   // on the dash top, 5 mm behind its rear face
  if (!wheel) { c.wheelC = c.anchor + vec3(0, -0.1f, -0.3f); c.wheelQ = glm::angleAxis(glm::radians(-62.0f), vec3(1, 0, 0)); }
  c.spec = specForAsset(a->id);
  if (c.car && a->model) {   // drawn as its supplied model: dash anchor (and the wheel with it) on the model's own dash
    const json& fit = loadSpec(c.spec).value("model", json::object());
    if (fit.contains("dash")) { vec2 yz = jv2(fit["dash"], vec2(c.anchor.y, c.anchor.z)); vec3 d(0, yz.x - c.anchor.y, yz.y - c.anchor.z); c.anchor += d; c.wheelC += d; }
    if (fit.contains("wheelY")) c.wheelC.y = fit["wheelY"].get<float>();   // the rim's centre height: its top under the eye line
  }
  return c;
}
}   // namespace

bool vehicleDashModelEye(const AssetDef* a, vec3& eye) {
  if (!a || !a->model) return false;
  const CarInfo& c = carInfo(a); if (!c.car) return false;
  const json& fit = loadSpec(c.spec).value("model", json::object());
  if (!fit.contains("eye")) return false;
  vec2 yz = jv2(fit["eye"], vec2(eye.y, eye.z)); eye.y = yz.x; eye.z = yz.y;
  if (fit["eye"].size() > 2) eye.x = (eye.x < 0 ? -1.0f : 1.0f) * fit["eye"][2].get<float>();   // [y, z, |x|]: inboard of a narrow shell's A-pillar
  return true;
}
static void dashDraw(App& app, const TankRT& t, const mat4& hull);
static void dashFrame(App& app);
void vehicleDashDraw(App& app, const TankRT& t, const mat4& hull) {   // a bad spec (content data) is logged, never fatal
  try { dashDraw(app, t, hull); } catch (const std::exception& e) { static int n = 0; if (n++ < 5) logf("dash: %s: %s", t.def ? t.def->id.c_str() : "?", e.what()); }
}
void vehicleDashLights(App& app, std::vector<LightDesc>& out) {
  (void)app;
  for (auto it = gCabin.begin(); it != gCabin.end();) {
    if (it->second.frame < gFrame - 1) { it = gCabin.erase(it); continue; }   // not drawn last frame: out of reach
    const CabinLight& c = it->second;
    LightDesc L; L.pos = c.pos; L.radius = 1.7f; L.id = 0xCAB00000u + (uint32_t)(it->first & 0xFFFFF);
    L.color = glm::mix(vec3(0.45f, 0.6f, 1.0f), vec3(0.85f, 0.9f, 1.0f), c.day);
    L.intensity = 1.6f * c.day + 0.04f * c.glow;
    if (L.intensity > 0.01f) out.push_back(L);
    ++it;
  }
}
void vehicleDashFrame(App& app) {
  ++gFrame;
  try { dashFrame(app); } catch (const std::exception& e) { static int n = 0; if (n++ < 5) logf("dash: parked: %s", e.what()); }
}
static void dashDraw(App& app, const TankRT& t, const mat4& hull) {
  if (!t.def || !t.def->car) return;
  const TankDef& d = *t.def;
  if (glm::length(vec3(hull * vec4(d.driverEye, 1)) - app.eyePos()) > 14.0f) return;   // the interior is only seen up close
  json dj = d.raw.is_object() ? d.raw.value("dash", json::object()) : json::object();
  const CarInfo* ci = d.partsAsset ? &carInfo(d.partsAsset) : nullptr;
  if (ci && !ci->car) ci = nullptr;
  std::string name = ci ? ci->spec : dj.value("spec", d.id);
  const json& spec = loadSpec(name);
  if (spec.empty() || spec.value("style", std::string()) == "none") return;
  vec3 at = ci ? ci->anchor : d.driverEye + jv3(dj.value("offset", spec.value("offset", json())), vec3(0, -0.16f, 0.72f));
  const Kit& k = kitFor(name, spec, at.x > 0 ? -1.0f : 1.0f, ci ? ci->paint : vec3(0.5f));
  bool running = app.tanks.occupied == t.id && !t.wrecked;
  int state = t.wrecked ? 2 : running ? 1 : 0;
  float kmh = std::fabs(t.speed) * 3.6f, top = std::max(1.0f, d.maxFwd * 3.6f * 1.15f);
  float v[4] = {kmh / top, running ? std::clamp(t.rpm, 0.0f, 1.0f) : 0.0f, 0.68f, running ? 0.52f : 0.0f};
  mat4 wm; const mat4* wp = nullptr;
  if (ci) {   // the wheel turns about its column with the steering (as drawCar turns a model's rim)
    float rim = -t.steer * d.steerTurns * 0.5f * 6.2831853f;
    wm = hull * glm::translate(mat4(1), ci->wheelC) * glm::mat4_cast(ci->wheelQ) * glm::rotate(mat4(1), rim, vec3(0, 1, 0)); wp = &wm;
  }
  mat4 W = hull * glm::translate(mat4(1), at) * glm::scale(mat4(1), vec3(spec.value("scale", 1.0f)));
  drawKit(app, k, W, state, v, kmh, wp);
  if (spec.value("cabinLight", true)) cabinLight(app, t.id, W, jv3(spec.value("light", json()), vec3(0.37f * (at.x > 0 ? -1.0f : 1.0f), -0.06f, -0.5f)), state == 1);
}

static void dashFrame(App& app) {   // parked part-built cars within reach: their interior, dark (or dusty, cracked) screens
  vec3 eye = app.mode == Mode::Creative ? app.camPos : app.eyePos();   // the editor camera looks in too
  for (auto& i : app.doc.instances) {
    vec3 dv = i.pos - eye; if (dv.x * dv.x + dv.z * dv.z > 12.0f * 12.0f) continue;
    auto rt = app.rt.find(i.id); if (rt == app.rt.end() || !rt->second.asset || app.tanks.get(i.id)) continue;   // a driven / adopted car draws its own
    const CarInfo& c = carInfo(rt->second.asset);
    if (!c.car) continue;
    const json& spec = loadSpec(c.spec);
    if (spec.empty() || spec.value("style", std::string()) == "none") continue;
    const Kit& k = kitFor(c.spec, spec, c.anchor.x > 0 ? -1.0f : 1.0f, c.paint);
    mat4 I = glm::translate(mat4(1), i.pos) * glm::mat4_cast(yawQuat(i.yaw));
    mat4 wm = I * glm::translate(mat4(1), c.wheelC) * glm::mat4_cast(c.wheelQ);
    float v[4] = {0, 0, c.wreck ? 0.0f : 0.3f, 0};
    drawKit(app, k, I * glm::translate(mat4(1), c.anchor), c.wreck ? 2 : 0, v, 0, &wm);
    if (dv.x * dv.x + dv.z * dv.z < 6.0f * 6.0f) cabinLight(app, i.id, I * glm::translate(mat4(1), c.anchor), jv3(spec.value("light", json()), vec3(0.37f * (c.anchor.x > 0 ? -1.0f : 1.0f), -0.06f, -0.5f)), false);
  }
}
