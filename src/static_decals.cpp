// GX5 static decals (static_decals.h, docs/graphics-overhaul.md#gx5-decals): scatter per building and the shared buffer.
#include "static_decals.h"
#include "interior.h"
#include <algorithm>

namespace {
struct Cfg {
  float density = 1.0f;                 // global multiplier (Settings: Material detail Low turns the pass off instead)
  float stainPerWall = 0.9f, stainCeiling = 0.5f;   // per wall (per 6 m) / per room; scaled by the building's wear
  float sootHearth = 1.0f;              // a soot plume over each hearth
  float crackPerWindow = 0.6f, crackPerWall = 0.45f;
  float scuffPerWall = 0.6f;            // per 5 m of wall
  float ghostPerWall = 0.5f;            // the clean outline of a picture that hung there, on walls over 2.5 m
  float dampPerRoom = 0.7f;            // ground floor corners
  float pockDamaged = 1.2f;             // bullet pock clusters per wall, damaged variants only
  float streakPerSill = 0.8f;           // exterior rain streaks under sills
  int maxPerRoom = 40, maxExterior = 24;
};
const Cfg& cfg() {
  static Cfg c = [] {
    Cfg k; json j;
    try { j = json::parse(readTextFile(contentDir() / "settings" / "decals.json")); } catch (...) { logf("decals: content/settings/decals.json missing or invalid (defaults)"); return k; }
    k.density = j.value("density", k.density);
    json t = j.value("rates", json::object());
    k.stainPerWall = t.value("stainPerWall", k.stainPerWall); k.stainCeiling = t.value("stainCeiling", k.stainCeiling);
    k.sootHearth = t.value("sootHearth", k.sootHearth); k.crackPerWindow = t.value("crackPerWindow", k.crackPerWindow);
    k.crackPerWall = t.value("crackPerWall", k.crackPerWall); k.scuffPerWall = t.value("scuffPerWall", k.scuffPerWall);
    k.ghostPerWall = t.value("ghostPerWall", k.ghostPerWall); k.dampPerRoom = t.value("dampPerRoom", k.dampPerRoom);
    k.pockDamaged = t.value("pockDamaged", k.pockDamaged); k.streakPerSill = t.value("streakPerSill", k.streakPerSill);
    k.maxPerRoom = std::clamp(j.value("maxPerRoom", k.maxPerRoom), 0, 32); k.maxExterior = std::clamp(j.value("maxExterior", k.maxExterior), 0, 64);
    return k;
  }();
  return c;
}
uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
struct Rng {
  uint32_t s;
  float f() { s = mix32(s + 0x9E3779B9u); return (s & 0xFFFFFF) / 16777216.0f; }
  float r(float a, float b) { return a + (b - a) * f(); }
  int count(float expect) { int n = (int)expect; return n + (f() < expect - n ? 1 : 0); }   // expected value = expect
};
// a wall of a room rectangle: a point on it (pos along = 0), the direction along it, its inward normal, its length
struct Wall { vec3 p0, along, n; float len; };
void roomWalls(const RoomInfo::Room& R, Wall w[4]) {
  float y = R.y0;
  w[0] = {vec3(R.mn.x, y, R.mn.y), vec3(0, 0, 1), vec3(1, 0, 0), R.mx.y - R.mn.y};
  w[1] = {vec3(R.mx.x, y, R.mn.y), vec3(0, 0, 1), vec3(-1, 0, 0), R.mx.y - R.mn.y};
  w[2] = {vec3(R.mn.x, y, R.mn.y), vec3(1, 0, 0), vec3(0, 0, 1), R.mx.x - R.mn.x};
  w[3] = {vec3(R.mn.x, y, R.mx.y), vec3(1, 0, 0), vec3(0, 0, -1), R.mx.x - R.mn.x};
}
StaticDecal mk(int type, vec3 c, vec3 n, float hw, float hh, float strength, Rng& g) {
  return {vec4(c, (float)type), vec4(n, g.f()), vec4(hw, hh, strength, (float)(int)(g.f() * 4.0f))};
}
}  // namespace

const char* staticDecalName(int t) {
  static const char* n[] = {"crack", "water stain", "soot", "scuff", "bullet pocks", "rain streak", "picture outline", "damp"};
  return t >= 0 && t < SD_TYPES ? n[t] : "?";
}

void staticDecalsFor(Renderer& R, uint64_t instId, const RoomInfo& ri, SurfaceParams& sp, float wear, bool damaged,
                     const std::vector<vec4>& xA, const std::vector<vec4>& xB) {
  const Cfg& C = cfg();
  std::vector<StaticDecal> out;
  auto wkOf = [&](float w) { return std::clamp(0.35f + 0.65f * w, 0.2f, 2.4f) * C.density; };   // wear 0 (kept) .. 2 (derelict)
  float wk = wkOf(wear);
  int nr = std::min((int)ri.rooms.size(), 16 + (int)xA.size());
  std::vector<vec4> dec(nr, vec4(0));   // per room: first decal (index into out), count
  for (int k = 0; k < nr; ++k) {
    const auto& Rm = ri.rooms[k];
    Rng g{mix32((uint32_t)instId * 2654435761u ^ (uint32_t)(k + 1) * 40503u)};
    size_t first = out.size();
    float roomW = 0.4f + 1.2f * g.f();   // per-room wear factor: rooms of one house range from kept to ruined (kMainFS reads it too)
    dec[k].z = roomW; wk = wkOf(wear * roomW);
    float H = Rm.y1 - Rm.y0;
    if (H < 1.6f || Rm.area < 2.0f) continue;
    Wall w[4]; roomWalls(Rm, w);
    auto onWall = [&](const Wall& W, float along, float y) { return W.p0 + W.along * along + vec3(0, y, 0); };
    bool barnish = Rm.type == ROOM_BARN || Rm.type == ROOM_WORKSHOP || Rm.type == ROOM_RUIN;
    // soot over the hearth: on the wall the hearth stands against
    if (Rm.hearth && C.sootHearth > 0) {
      int best = 0; float bd = 1e9f;
      for (int s = 0; s < 4; ++s) { float d = std::fabs(glm::dot(Rm.hearthPos - w[s].p0, w[s].n)); if (d < bd) { bd = d; best = s; } }
      float along = glm::dot(Rm.hearthPos - w[best].p0, w[best].along);
      float y0 = std::max(Rm.hearthPos.y - Rm.y0 + 0.75f, 0.9f), y1 = H - 0.02f;
      if (y1 - y0 > 0.4f) out.push_back(mk(SD_SOOT, onWall(w[best], along, (y0 + y1) * 0.5f), w[best].n, g.r(0.42f, 0.6f), (y1 - y0) * 0.5f, std::min(1.0f, 0.75f * C.sootHearth), g));
    }
    for (int s = 0; s < 4; ++s) {
      const Wall& W = w[s];
      if (W.len < 0.9f) continue;
      // water stains hanging from the ceiling
      for (int q = g.count(C.stainPerWall * wk * W.len / 6.0f); q > 0; --q) {
        float hw = g.r(0.22f, 0.6f), hh = g.r(0.28f, std::min(0.9f, H * 0.35f));
        out.push_back(mk(SD_STAIN, onWall(W, g.r(hw, std::max(hw, W.len - hw)), H - hh * 0.85f), W.n, hw, hh, g.r(0.55f, 1.0f), g));
      }
      // scuffs and knocks along the skirting
      if (!barnish) for (int q = g.count(C.scuffPerWall * wk * W.len / 5.0f); q > 0; --q) {
        float hw = g.r(0.25f, 0.7f);
        out.push_back(mk(SD_SCUFF, onWall(W, g.r(hw, std::max(hw, W.len - hw)), g.r(0.12f, 0.26f)), W.n, hw, g.r(0.1f, 0.18f), g.r(0.5f, 1.0f), g));
      }
      // a picture that hung here once: a cleaner outline and its nail
      if (!barnish && W.len > 2.5f && g.f() < C.ghostPerWall * C.density) {
        float hw = g.r(0.17f, 0.34f);
        out.push_back(mk(SD_GHOST, onWall(W, g.r(hw + 0.3f, W.len - hw - 0.3f), std::min(g.r(1.35f, 1.75f), H - 0.5f)), W.n, hw, hw * g.r(0.7f, 1.3f), g.r(0.6f, 1.0f), g));
      }
      // settlement cracks down from the ceiling
      if (g.f() < C.crackPerWall * wk) {
        float hh = g.r(0.4f, std::min(1.2f, H * 0.45f));
        out.push_back(mk(SD_CRACK, onWall(W, g.r(0.4f, std::max(0.4f, W.len - 0.4f)), H - hh), W.n, g.r(0.15f, 0.3f), hh, g.r(0.6f, 1.0f), g));
      }
      if (damaged) for (int q = g.count(C.pockDamaged * W.len / 5.0f); q > 0; --q)
        out.push_back(mk(SD_POCK, onWall(W, g.r(0.6f, std::max(0.6f, W.len - 0.6f)), g.r(0.8f, std::min(2.0f, H - 0.4f))), W.n, g.r(0.35f, 0.6f), g.r(0.35f, 0.6f), 1.0f, g));
    }
    // cracks from the top corners of this room's windows (sills whose inner face lies on one of its walls)
    for (const auto& S : ri.sills) {
      vec3 sn(-S.n.x, 0, -S.n.y);   // inward
      for (int s = 0; s < 4; ++s) {
        const Wall& W = w[s];
        if (glm::dot(W.n, sn) < 0.9f) continue;
        float off = glm::dot(S.c - W.p0, W.n);
        float along = glm::dot(S.c - W.p0, W.along);
        if (off > 0.05f || off < -1.0f || along < -0.2f || along > W.len + 0.2f || S.c.y < Rm.y0 - 0.1f || S.c.y > Rm.y1) continue;
        if (g.f() >= C.crackPerWindow * wk) continue;
        float side = g.f() < 0.5f ? -1.0f : 1.0f, top = S.top > S.c.y ? S.top : S.c.y + 1.2f;
        float hh = std::min(g.r(0.3f, 0.7f), (Rm.y1 - top) * 0.5f + 0.25f);
        out.push_back(mk(SD_CRACK, onWall(W, along + side * (S.halfW + 0.12f), top - Rm.y0 + hh * 0.7f), W.n, g.r(0.14f, 0.24f), hh, g.r(0.6f, 1.0f), g));
        out.back().c.w += 8;   // marks a window crack for the priority sort (stripped after it)
      }
    }
    // a water stain on the ceiling
    if (g.f() < C.stainCeiling * wk) {
      float r = g.r(0.3f, 0.75f);
      vec3 c(g.r(Rm.mn.x + r, std::max(Rm.mn.x + r, Rm.mx.x - r)), Rm.y1, g.r(Rm.mn.y + r, std::max(Rm.mn.y + r, Rm.mx.y - r)));
      out.push_back(mk(SD_STAIN, c, vec3(0, -1, 0), r, r * g.r(0.7f, 1.2f), g.r(0.5f, 0.9f), g));
    }
    // rising damp in a ground-floor corner
    if (Rm.storey == 0 && g.f() < C.dampPerRoom * wk) {
      const Wall& W = w[(int)(g.f() * 3.99f)];
      float hw = g.r(0.35f, 0.6f), end = g.f() < 0.5f ? hw * 0.8f : W.len - hw * 0.8f;
      out.push_back(mk(SD_DAMP, onWall(W, end, g.r(0.35f, 0.55f)), W.n, hw, g.r(0.35f, 0.6f), g.r(0.6f, 1.0f), g));
    }
    // keep the most telling ones when the room has more than the cap (a 39 m corridor): hearth soot, window cracks,
    // stains, picture outlines, damp, then the rest; scuffs last. Stable, so the scatter order breaks ties.
    static const int prio[SD_TYPES] = {3, 2, 0, 7, 6, 8, 4, 5};   // crack, stain, soot, scuff, pock, streak, ghost, damp
    std::stable_sort(out.begin() + first, out.end(), [&](const StaticDecal& a, const StaticDecal& b) {
      int ta = (int)(a.a.w + 0.5f), tb = (int)(b.a.w + 0.5f);
      int pa = ta == SD_CRACK && a.c.w >= 8 ? 1 : prio[ta], pb = tb == SD_CRACK && b.c.w >= 8 ? 1 : prio[tb];   // window cracks (variant + 8) first
      return pa < pb; });
    size_t n = std::min(out.size() - first, (size_t)C.maxPerRoom);
    out.resize(first + n);
    for (size_t q = first; q < out.size(); ++q) out[q].c.w = std::fmod(out[q].c.w, 8.0f);
    dec[k] = vec4((float)first, (float)n, roomW, 0);
  }
  // exterior: rain streaks under the sills, pocks on damaged copies
  {
    Rng g{mix32((uint32_t)instId * 2246822519u + 99u)};
    wk = wkOf(wear);
    size_t first = out.size();
    for (const auto& S : ri.sills) {
      if (g.f() >= C.streakPerSill * wk) continue;
      float hh = g.r(0.45f, 1.1f);
      out.push_back(mk(SD_STREAK, S.c - vec3(0, hh + 0.03f, 0), vec3(S.n.x, 0, S.n.y), S.halfW * g.r(1.0f, 1.25f), hh, g.r(0.6f, 1.0f), g));
    }
    size_t n = std::min(out.size() - first, (size_t)C.maxExterior);
    out.resize(first + n);
    sp.extDec = vec4((float)first, (float)n, 0, 0);
  }
  // the block: rooms past 16 as records (A, B, decal range), then the decals
  int nx = std::max(0, nr - 16);
  std::vector<StaticDecal> blk(nx);
  for (int j = 0; j < nx; ++j) blk[j] = {xA[j], xB[j], vec4(0)};
  blk.insert(blk.end(), out.begin(), out.end());
  out.swap(blk);
  // into the shared buffer: this copy's block, reused in place when it still fits (first fit from the free list otherwise)
  int need = (int)out.size();
  auto it = R.sdecRange.find(instId);
  int base = -1;
  if (it != R.sdecRange.end()) {
    if (it->second.y >= need) base = it->second.x;
    else { R.sdecFree.push_back(it->second); R.sdecRange.erase(it); }
  }
  if (base < 0 && need > 0) {
    for (size_t f = 0; f < R.sdecFree.size(); ++f) if (R.sdecFree[f].y >= need) { base = R.sdecFree[f].x; R.sdecRange[instId] = R.sdecFree[f]; R.sdecFree.erase(R.sdecFree.begin() + f); break; }
    if (base < 0) { base = (int)R.sdec.size(); R.sdec.resize(R.sdec.size() + need); R.sdecRange[instId] = glm::ivec2(base, need); }
  }
  auto at = [&](vec4 d) { return vec4(d.y > 0 ? d.x + (float)(base + nx) : 0.0f, d.y, d.z, 0); };   // decal index -> buffer; z = room wear
  for (int k = 0; k < 16; ++k) sp.roomDec[k] = k < nr ? at(dec[k]) : vec4(0);
  for (int j = 0; j < nx; ++j) out[j].c = at(dec[16 + j]);
  sp.extDec = at(sp.extDec);
  sp.roomX = glm::ivec2(base, nx);
  for (int k = 0; k < need; ++k) R.sdec[base + k] = out[k];
  R.sdecDirty = true;
  if (std::getenv("NMK_GX5_DEBUG")) {
    int tot = 0; for (int k = 0; k < nr; ++k) tot += (int)dec[k].y;
    logf("sdecals #%llu: %d rooms (%d past 16), %d interior + %d exterior decals, block %d+%d", (unsigned long long)instId, nr, nx, tot, (int)sp.extDec.y, base, need);
  }
}
