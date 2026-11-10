#pragma once
// LN1 line features (plan Addendum 7; docs/lines.md): a contiguous feature (a trench, and from LN2 wire, fences, walls,
// hedges) is one line drawn with the lasso's Line mode, not a chain of snap pieces.
//
// Data model: a line is an ordinary Instance of a line asset, an asset whose "line" block names its line type
// (content/assets/line_<type>.json, {"line": {"type": "<type id>"}}). Its geometry is plain data in
// Instance::extra["line"]:
//   {"pts": [[x, z], [x, z, d], ...],   world xz control points; d (optional, default 1) scales the type's depth / height
//                                       at that point (a trench's 0 = ground level: an entry ramp)
//    "seed": n,                         per-line random seed (builders vary bays from it)
//    "params": {...},                   per-line overrides of the type's "params" in content/settings/line_types.json
//    "spans": [{"s": m, "w": m, "kind": "gap" | "field_gate" | "gate_piers"}]}   gates and gaps by arc length (LN2)
// Instance::pos is the first point (ground y), yaw 0: builders work in instance space (world minus pos).
// Save / load, undo, the map hash (LAN identity) and the runtime record (InstanceRT, with secHp / secCut: one section
// per bay) all come with the Instance, so damage events, save and replication need nothing new.
#include "app.h"

struct LinePoint { vec2 p{0}; float d = 1.0f; };
struct LineSpan { float s = 0, w = 3.6f; std::string kind = "gap"; };
struct LineData {
  std::vector<LinePoint> pts;
  uint32_t seed = 1;
  json params = json::object();
  std::vector<LineSpan> spans;
};

// content/settings/line_types.json: {"types": [{"id", "name", "asset", "builder", "spacing", "params": {...}}]}
struct LineType {
  std::string id, name, asset, builder;   // builder: a name registered with LINE_BUILDER ("trench" is GX1's network)
  float spacing = 4.0f;                   // nominal bay length (m): builders stretch bays to fit the line
  json params = json::object();           // defaults; a line's own "params" override them key by key
  json raw;                               // the whole entry (builders read their own keys)
};
const std::vector<LineType>& lineTypes();
const LineType* lineType(const std::string& id);
const LineType* lineTypeOf(const AssetDef* a);          // the asset's "line" block, or nullptr: not a line asset
inline bool isLineAsset(const AssetDef* a) { return lineTypeOf(a) != nullptr; }

bool lineGet(const Instance& i, LineData& out);          // false when the instance carries no line
void lineSet(Instance& i, const LineData& d);            // writes extra["line"], moves pos to the first point (ground y)
json lineParams(const LineType& t, const LineData& d);   // type defaults merged with the line's own params

// The line as a smooth curve: centripetal Catmull-Rom through the control points (corners kept where `sharp`),
// resampled every `step` m; each sample carries its arc length s and the interpolated d. World xz.
struct LineSample { vec2 p{0}; float s = 0, d = 1; vec2 t{1, 0}; };
std::vector<LineSample> lineSpline(const LineData& d, float step = 0.5f, bool sharp = false);
float lineLength(const std::vector<LineSample>& sp);

// ---------------------------------------------------------------- builders (one per line type family)
// A builder turns a line into instance-space geometry. The engine draws `mesh`, builds one static compound from
// `colliders` (a collider with section k vanishes once InstanceRT::secCut[k] is set), adds the nav areas and cover,
// and gives the line `sections` damage sections (secHp from the type's "sectionHp"). Interactables (gate leaves) are
// LN2's: `gates` lists their bays for the door system to come.
struct LineCollider { Part part; int section = -1; bool playerOnly = false; };
struct LineGate { float s = 0, w = 3.6f; std::string kind; vec2 a{0}, b{0}; };   // a gate's posts, instance space
struct LineBuildOut {
  MeshData mesh;                          // all materials batched (per-vertex albedo), instance space
  std::vector<LineCollider> colliders;    // instance space
  std::vector<NavAreaPoly> areas;         // instance space outlines
  std::vector<NavCoverPoint> cover;       // instance space
  std::vector<LineGate> gates;
  int sections = 0;
};
struct LineBuildCtx {
  App& app;
  const Instance& inst;
  const LineData& line;
  const LineType& type;
  json params;                            // lineParams(type, line)
  std::vector<LineSample> spline;         // lineSpline(line, 0.5)
  float ground(float x, float z) const;   // the terrain at world xz (stamped, cut and cratered ground)
  vec3 local(vec2 w, float y) const { return vec3(w.x - inst.pos.x, y - inst.pos.y, w.y - inst.pos.z); }
};
using LineBuilderFn = void (*)(const LineBuildCtx& ctx, LineBuildOut& out);
bool lineRegisterBuilder(const char* name, LineBuilderFn fn);
LineBuilderFn lineBuilder(const std::string& name);
#define LINE_BUILDER(name, fn) static const bool kLineBuilderReg_##fn = lineRegisterBuilder(name, fn)
