#include "navmeta.h"

static const char* kAreaNames[(int)NavArea::Count] = {"default", "road", "grass", "brush", "marsh", "sand", "trench", "minefield", "wire", "water"};
// AI_SPEC 3.1 table (default = grass-like open ground; water = shallow water, like marsh)
static const float kAreaCost[(int)NavArea::Count] = {1.2f, 1.0f, 1.2f, 1.6f, 3.0f, 1.3f, 0.9f, -1.0f, -1.0f, 3.0f};

const char* navAreaName(NavArea a) { int i = (int)a; return i >= 0 && i < (int)NavArea::Count ? kAreaNames[i] : "default"; }
NavArea navAreaFrom(const std::string& s) { for (int i = 0; i < (int)NavArea::Count; ++i) if (s == kAreaNames[i]) return (NavArea)i; return NavArea::Default; }
float navAreaCost(NavArea a) { int i = (int)a; return i >= 0 && i < (int)NavArea::Count ? kAreaCost[i] : 1.2f; }

static vec2 v2(const json& j) { return j.is_array() && j.size() >= 2 ? vec2(j[0].get<float>(), j[1].get<float>()) : vec2(0); }
static std::vector<vec2> outline(const json& j) { std::vector<vec2> o; if (j.is_array()) for (auto& p : j) o.push_back(v2(p)); return o; }
static json outlineJ(const std::vector<vec2>& o) { json a = json::array(); for (auto& p : o) a.push_back({p.x, p.y}); return a; }

void parseNavMeta(const json& j, NavMeta& m) {
  m = NavMeta();
  if (j.contains("terrainCut") && j["terrainCut"].is_array())
    for (auto& c : j["terrainCut"]) { TerrainCut t; t.outline = outline(c.value("outline", json())); t.depth = c.value("depth", 1.0f); t.bank = c.value("bank", 0.3f); if (t.outline.size() >= 3) m.terrainCuts.push_back(t); }
  if (!j.contains("nav") || !j["nav"].is_object()) return;
  const json& n = j["nav"];
  m.authored = true;
  std::string e = n.value("effect", "auto");
  m.effect = e == "block" ? NavEffect::Block : e == "carve" ? NavEffect::Carve : e == "area" ? NavEffect::Area : e == "none" ? NavEffect::None : NavEffect::Auto;
  m.area = navAreaFrom(n.value("area", "default"));
  for (auto& a : n.value("areas", json::array())) m.areas.push_back({outline(a.value("outline", json())), navAreaFrom(a.value("area", "default"))});
  for (auto& r : n.value("rooms", json::array())) m.rooms.push_back({r.value("id", ""), jvec3(r.value("min", json())), jvec3(r.value("max", json()))});
  for (auto& d : n.value("doors", json::array())) m.doors.push_back({d.value("a", ""), d.value("b", "outside"), jvec3(d.value("p", json()))});
  for (auto& d : n.value("entrances", json::array())) m.entrances.push_back({jvec3(d.value("p", json())), jvec3(d.value("dir", json()), vec3(0, 0, 1)), d.value("room", "")});
  for (auto& d : n.value("corners", json::array())) m.corners.push_back({jvec3(d.value("p", json())), d.value("room", "")});
  for (auto& d : n.value("firingPoints", json::array())) m.firingPoints.push_back({jvec3(d.value("p", json())), d.value("yaw", 0.0f), d.value("kind", "window")});
  for (auto& d : n.value("cover", json::array())) {
    NavCoverPoint c; c.p = jvec3(d.value("p", json())); c.yaw = d.value("yaw", 0.0f); c.high = d.value("height", "low") == "high";
    for (auto& pk : d.value("peek", json::array())) { std::string s = pk.get<std::string>(); c.peekLeft |= s == "left"; c.peekRight |= s == "right"; c.peekOver |= s == "over"; }
    m.cover.push_back(c);
  }
}

json navMetaToJson(const NavMeta& m) {
  static const char* eff[] = {"auto", "block", "carve", "area", "none"};
  json n = {{"effect", eff[(int)m.effect]}, {"area", navAreaName(m.area)}};
  json a = json::array(); for (auto& x : m.areas) a.push_back({{"outline", outlineJ(x.outline)}, {"area", navAreaName(x.area)}}); n["areas"] = a;
  json r = json::array(); for (auto& x : m.rooms) r.push_back({{"id", x.id}, {"min", tojson(x.min)}, {"max", tojson(x.max)}}); n["rooms"] = r;
  json d = json::array(); for (auto& x : m.doors) d.push_back({{"a", x.a}, {"b", x.b}, {"p", tojson(x.p)}}); n["doors"] = d;
  json en = json::array(); for (auto& x : m.entrances) en.push_back({{"p", tojson(x.p)}, {"dir", tojson(x.dir)}, {"room", x.room}}); n["entrances"] = en;
  json co = json::array(); for (auto& x : m.corners) co.push_back({{"p", tojson(x.p)}, {"room", x.room}}); n["corners"] = co;
  json fp = json::array(); for (auto& x : m.firingPoints) fp.push_back({{"p", tojson(x.p)}, {"yaw", x.yaw}, {"kind", x.kind}}); n["firingPoints"] = fp;
  json cv = json::array();
  for (auto& x : m.cover) { json pk = json::array(); if (x.peekLeft) pk.push_back("left"); if (x.peekRight) pk.push_back("right"); if (x.peekOver) pk.push_back("over"); cv.push_back({{"p", tojson(x.p)}, {"yaw", x.yaw}, {"height", x.high ? "high" : "low"}, {"peek", pk}}); }
  n["cover"] = cv;
  json tc = json::array(); for (auto& c : m.terrainCuts) tc.push_back({{"outline", outlineJ(c.outline)}, {"depth", c.depth}, {"bank", c.bank}});
  return {{"nav", n}, {"terrainCut", tc}};
}

NavEffect resolveNavEffect(const NavMeta& m, bool enterable, vec3 bmin, vec3 bmax) {
  if (m.effect != NavEffect::Auto) return m.effect;
  if (enterable || bmax.y - bmin.y > 1.6f) return NavEffect::Block;
  return NavEffect::Carve;
}
