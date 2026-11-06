// Gore system (SPEC 7, workstream W7): wounds, spray, droplets, bleeding, pools, budgets and LAN replication.
// Replaces gore_stub.cpp. See gore.h for the model; decals.cpp draws the decals.
#include "gore.h"
#include "game.h"
#include "app.h"
#include "net.h"
#include "physics.h"
#include "rules.h"
#include <imgui.h>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

int gGoreHitsReported = 0;   // every FleshHit given to goreFleshHit (the ai suite checks the damage path reports them all)

namespace {
constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------- deterministic RNG (seed -> identical sequence)
struct GRng {
  uint32_t s;
  explicit GRng(uint32_t seed) {   // splitmix-style scramble so neighbouring seeds give unrelated sequences
    uint32_t z = seed + 0x9E3779B9u; z = (z ^ (z >> 16)) * 0x85EBCA6Bu; z = (z ^ (z >> 13)) * 0xC2B2AE35u; z ^= z >> 16;
    s = z ? z : 0x6D2B79F5u;
  }
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float u() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float range(float a, float b) { return a + (b - a) * u(); }
  float range(vec2 r) { return range(r.x, r.y); }
};
vec3 anyPerp(vec3 n) { vec3 a = std::fabs(n.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0); return glm::normalize(glm::cross(a, n)); }
vec3 coneDir(GRng& r, vec3 axis, float halfDeg) {   // uniform over the spherical cap of the given half angle
  float ct = 1.0f - r.u() * (1.0f - std::cos(radians(halfDeg)));
  float st = std::sqrt(std::max(0.0f, 1.0f - ct * ct)), ph = r.u() * 2 * kPi;
  vec3 x = anyPerp(axis), y = glm::cross(axis, x);
  return glm::normalize(axis * ct + (x * std::cos(ph) + y * std::sin(ph)) * st);
}
float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

GoreState gS;
App* gGoreApp = nullptr;
fs::path userFile() { return exeDir() / "gore_user.json"; }
const char* levelName(GoreLevel l) { return l == GoreLevel::Off ? "off" : l == GoreLevel::Reduced ? "reduced" : "full"; }
GoreLevel levelFrom(const std::string& s) { return s == "off" ? GoreLevel::Off : s == "reduced" ? GoreLevel::Reduced : GoreLevel::Full; }

void ensureLoaded() {
  if (gS.loaded) return;
  gS.loaded = true;
  try { json j = json::parse(readTextFile(contentDir() / "settings" / "gore.json")); gS.cfg.load(j); }
  catch (std::exception& e) { logf("gore: settings/gore.json: %s (defaults)", e.what()); }
  gS.level = gS.cfg.level;
  std::error_code ec;
  if (fs::exists(userFile(), ec)) {
    try { gS.level = levelFrom(json::parse(readTextFile(userFile())).value("level", std::string("full"))); } catch (...) {}
  }
  logf("gore: level %s, budgets %d world / %d per character / %d droplets, mist only beyond %.0f m", levelName(gS.level), gS.cfg.maxWorldDecals, gS.cfg.maxDecalsPerCharacter, gS.cfg.maxDroplets, gS.cfg.mistOnlyBeyond);
}

vec3 camPos(App& app) { return vec3(glm::inverse(app.viewM)[3]); }
bool waterAt(App& app, vec3 p) { float w = app.terrain.waterLevel; return w > -999.0f && p.y <= w + 0.01f && app.terrain.height(p.x, p.z) < w; }

// ---------------------------------------------------------------- posed character mesh queries
struct MeshIdx { std::vector<std::vector<uint32_t>> trisByJoint; };
std::map<const CharModel*, MeshIdx> gIdx;
const MeshIdx& meshIdx(const CharModel& m) {
  auto it = gIdx.find(&m); if (it != gIdx.end()) return it->second;
  MeshIdx& x = gIdx[&m];
  x.trisByJoint.assign(std::max(1, m.joints()), {});
  for (uint32_t t = 0; t + 2 < m.indices.size(); t += 3) {
    const auto& v = m.verts[m.indices[t]];
    int dj = 0; float bw = -1; for (int q = 0; q < 4; ++q) if (v.w[q] > bw) { bw = v.w[q]; dj = (int)v.j[q]; }
    if (dj >= 0 && dj < (int)x.trisByJoint.size()) x.trisByJoint[dj].push_back(t / 3);
  }
  return x;
}
vec3 skinP(const CharModel& m, const PoseResult& P, uint32_t vi) {
  const auto& v = m.verts[vi]; vec4 r(v.pos, 1); vec3 o(0);
  for (int q = 0; q < 4; ++q) if (v.w[q] > 0 && v.j[q] < P.skin.size()) o += v.w[q] * vec3(P.skin[v.j[q]] * r);
  return o;
}
vec3 skinN(const CharModel& m, const PoseResult& P, uint32_t vi) {
  const auto& v = m.verts[vi]; vec3 o(0);
  for (int q = 0; q < 4; ++q) if (v.w[q] > 0 && v.j[q] < P.skin.size()) o += v.w[q] * (glm::mat3(P.skin[v.j[q]]) * v.nrm);
  return glm::length(o) > 1e-6f ? glm::normalize(o) : vec3(0, 1, 0);
}
int dominantJoint(const CharModel& m, int tri, vec3 bary) {
  int k = bary.x >= bary.y && bary.x >= bary.z ? 0 : bary.y >= bary.z ? 1 : 2;
  const auto& v = m.verts[m.indices[tri * 3 + k]];
  int dj = 0; float bw = -1; for (int q = 0; q < 4; ++q) if (v.w[q] > bw) { bw = v.w[q]; dj = (int)v.j[q]; }
  return dj;
}
struct MeshHit { float t; int tri; vec3 bary; };
// every intersection of a world ray with the posed mesh (only triangles of joints near the ray), nearest first
void rayPosed(const Soldier& s, const CharModel& m, vec3 o, vec3 d, float maxT, std::vector<MeshHit>& hits, int* nearestTri = nullptr, vec3 nearTo = vec3(0)) {
  hits.clear();
  if (s.pose.skin.empty() || m.indices.empty()) return;
  mat4 inv = glm::inverse(s.world);
  vec3 om = vec3(inv * vec4(o, 1)), dm = glm::normalize(glm::mat3(inv) * d), near = vec3(inv * vec4(nearTo, 1));
  const MeshIdx& X = meshIdx(m);
  static std::vector<int> stamp; static std::vector<vec3> cache; static int gen = 0;
  if (stamp.size() != m.verts.size()) { stamp.assign(m.verts.size(), -1); cache.resize(m.verts.size()); }
  ++gen;
  auto P = [&](uint32_t vi) -> vec3 { if (stamp[vi] != gen) { stamp[vi] = gen; cache[vi] = skinP(m, s.pose, vi); } return cache[vi]; };
  float bestNear = 1e9f;
  for (int j = 0; j < m.joints() && j < (int)X.trisByJoint.size(); ++j) {
    if (X.trisByJoint[j].empty() || j >= (int)s.pose.delta.size()) continue;
    vec3 jh = vec3(s.pose.delta[j] * vec4(m.jointHead[j], 1));
    float tt = std::clamp(glm::dot(jh - om, dm), 0.0f, maxT);
    if (glm::length(om + dm * tt - jh) > 0.6f) continue;
    for (uint32_t tri : X.trisByJoint[j]) {
      vec3 a = P(m.indices[tri * 3]), b = P(m.indices[tri * 3 + 1]), c = P(m.indices[tri * 3 + 2]);
      if (nearestTri) { float dd = glm::length((a + b + c) / 3.0f - near); if (dd < bestNear) { bestNear = dd; *nearestTri = (int)tri; } }
      vec3 e1 = b - a, e2 = c - a, pv = glm::cross(dm, e2);
      float det = glm::dot(e1, pv); if (std::fabs(det) < 1e-10f) continue;
      float id = 1.0f / det; vec3 tv = om - a;
      float u = glm::dot(tv, pv) * id; if (u < 0 || u > 1) continue;
      vec3 qv = glm::cross(tv, e1); float v = glm::dot(dm, qv) * id; if (v < 0 || u + v > 1) continue;
      float t = glm::dot(e2, qv) * id; if (t < 0 || t > maxT) continue;
      hits.push_back({t, (int)tri, vec3(1 - u - v, u, v)});
    }
  }
  std::sort(hits.begin(), hits.end(), [](const MeshHit& x, const MeshHit& y) { return x.t < y.t; });
}
const Soldier* victimOf(int slot) { return gGame && slot >= 0 && slot < (int)gGame->sold.size() ? &gGame->sold[slot] : nullptr; }
const CharModel* modelOf(const Soldier& s) { return gGame && s.model >= 0 && s.model < (int)gGame->models.size() ? gGame->models[s.model].get() : nullptr; }

GoreAttach computeAttach(const FleshHit& h) {
  GoreAttach A;
  const Soldier* s = victimOf(h.victimSlot); if (!s) return A;
  const CharModel* m = modelOf(*s); if (!m) return A;
  A.model = s->model;
  std::vector<MeshHit> hits; int nearTri = -1;
  vec3 o = h.pos - h.dir * 0.6f;
  rayPosed(*s, *m, o, h.dir, 1.6f, hits, &nearTri, h.pos);
  if (!hits.empty()) {
    A.entryTri = hits[0].tri; A.entryBary = hits[0].bary;
    if (h.exits) for (int k = (int)hits.size() - 1; k > 0; --k) if (hits[k].t > hits[0].t + 0.04f && hits[k].t < hits[0].t + 0.8f) { A.exitTri = hits[k].tri; A.exitBary = hits[k].bary; break; }
  } else if (nearTri >= 0) { A.entryTri = nearTri; A.entryBary = vec3(1.0f / 3); ++gS.st.meshMisses; }
  return A;
}

// ---------------------------------------------------------------- budgets
template <class T> void capVec(std::vector<T>& v, int cap) { if (cap < 1) { v.clear(); return; } if ((int)v.size() > cap) v.erase(v.begin(), v.begin() + ((int)v.size() - cap)); }
void addWorld(WorldDecal d) {
  int cap = goreWorldCap();
  if (cap <= 0) return;
  while ((int)gS.world.size() >= cap) gS.world.erase(gS.world.begin());   // hard cap: the oldest goes (it is already fading)
  gS.world.push_back(std::move(d)); ++gS.st.worldDecals;
  gS.st.maxWorld = std::max(gS.st.maxWorld, gS.world.size());
}
SoldierGore& recOf(int slot) {
  SoldierGore& r = gS.sold[slot];
  const Soldier* s = victimOf(slot);
  if (s && r.lifeId != s->lifeId) { r = SoldierGore{}; r.lifeId = s->lifeId; r.model = s->model; }
  return r;
}
void addBone(int slot, BoneDecal d) {
  int cap = goreCharCap(); if (cap <= 0) return;
  SoldierGore& r = recOf(slot);
  while ((int)r.decals.size() >= cap) r.decals.erase(r.decals.begin());
  r.decals.push_back(d); ++gS.st.boneDecals;
  gS.st.maxChar = std::max(gS.st.maxChar, r.decals.size());
}
bool restPoint(const CharModel& m, int tri, vec3 b, vec3& p, vec3& n) {
  if (tri < 0 || (size_t)tri * 3 + 2 >= m.indices.size()) return false;
  const auto &A = m.verts[m.indices[tri * 3]], &B = m.verts[m.indices[tri * 3 + 1]], &C = m.verts[m.indices[tri * 3 + 2]];
  p = A.pos * b.x + B.pos * b.y + C.pos * b.z;
  vec3 nn = A.nrm * b.x + B.nrm * b.y + C.nrm * b.z;
  n = glm::length(nn) > 1e-6f ? glm::normalize(nn) : glm::normalize(glm::cross(B.pos - A.pos, C.pos - A.pos) + vec3(0, 1e-6f, 0));
  return true;
}

// ---------------------------------------------------------------- world decals
void addSpeck(vec3 pos, vec3 n, float radius, float aspect, vec3 tail, uint32_t burst) {
  // one decal per burst and surface patch: later droplets of the same burst become stamps of it
  for (int k = (int)gS.world.size() - 1, seen = 0; k >= 0 && seen < 24; --k, ++seen) {
    WorldDecal& d = gS.world[k];
    if (d.kind != GD_SPECKS || d.fadeStart >= 0 || (int)d.stamps.size() >= WorldDecal::kMaxStamps) continue;
    if (d.burst != burst && gS.t - d.born > 2.0) continue;
    if (glm::dot(d.n, n) < 0.85f) continue;
    vec3 rel = pos - d.pos;
    if (std::fabs(glm::dot(rel, d.n)) > 0.04f || glm::length(rel) > 0.3f) continue;
    vec3 b = glm::cross(d.n, d.t);
    GoreStamp s; s.off = vec2(glm::dot(rel, d.t), glm::dot(rel, b)); s.radius = radius; s.aspect = aspect;
    vec2 tl(glm::dot(tail, d.t), glm::dot(tail, b)); s.tail = glm::length(tl) > 1e-5f ? glm::normalize(tl) : vec2(1, 0);
    d.stamps.push_back(s);
    d.extent = std::max(d.extent, glm::length(s.off) + radius * (2 * aspect + 1) + 0.005f);
    return;
  }
  WorldDecal d; d.kind = GD_SPECKS; d.pos = pos; d.n = n; d.t = anyPerp(n); d.born = gS.t; d.burst = burst; d.seed = burst * 2654435761u;
  vec3 b = glm::cross(d.n, d.t);
  GoreStamp s; s.radius = radius; s.aspect = aspect; vec2 tl(glm::dot(tail, d.t), glm::dot(tail, b)); s.tail = glm::length(tl) > 1e-5f ? glm::normalize(tl) : vec2(1, 0);
  d.stamps.push_back(s);
  d.extent = radius * (2 * aspect + 1) + 0.01f; d.depth = 0.035f;
  addWorld(std::move(d));
}
void ripple(vec3 p) {
  GorePuff q; q.p = p + vec3(0, 0.005f, 0); q.flat = true; q.size = 0.01f; q.grow = 0.2f; q.life = q.maxLife = 0.6f; q.col = vec4(0.62f, 0.66f, 0.7f, 0.35f);
  gS.puffs.push_back(q); ++gS.st.ripples;
}
void landDroplet(App& app, vec3 pos, vec3 n, vec3 v, float r, uint32_t burst) {
  if (waterAt(app, pos)) { ripple(vec3(pos.x, app.terrain.waterLevel, pos.z)); return; }
  float aspect; vec3 tail;
  goreTeardrop(v, n, gS.cfg.maxAspect, aspect, tail);
  float rad = r * gS.cfg.stainScale * (1.0f + 0.08f * glm::length(v));
  addSpeck(pos + n * 0.002f, n, rad, aspect, tail, burst);
  ++gS.st.landed;
}

void puff(vec3 p, vec3 v, float size, float grow, float life, vec4 col) { GorePuff q; q.p = p; q.v = v; q.size = size; q.grow = grow; q.life = q.maxLife = life; q.col = col; gS.puffs.push_back(q); }
void mist(vec3 p, vec3 dir, float scale, float life, GRng& r) {
  for (int k = 0; k < 4; ++k) puff(p + dir * 0.03f, (dir * (0.6f + r.u()) + vec3(r.u() - 0.5f, r.u() * 0.3f, r.u() - 0.5f) * 0.4f) * scale, 0.05f * scale, 0.9f * scale, life * (0.8f + 0.4f * r.u()), vec4(0.15f, 0.018f, 0.016f, 0.34f));
}
int spawnDrops(const std::vector<GoreDropInit>& v, uint32_t burst) {
  int n = 0;
  for (auto& d : v) {
    if ((int)gS.drops.size() >= gS.cfg.maxDroplets) { ++gS.st.dropped; continue; }   // hard cap
    GoreDroplet q; q.p = d.p; q.v = d.v; q.r = d.r; q.burst = burst; gS.drops.push_back(q); ++n;
  }
  gS.st.spawned += n; gS.st.maxDrops = std::max(gS.st.maxDrops, gS.drops.size());
  return n;
}

json eventJson(const FleshHit& h, const GoreAttach& A) {
  json e = {{"t", "gore"}, {"p", tojson(h.pos)}, {"n", tojson(h.normal)}, {"d", tojson(h.dir)}, {"w", h.weapon}, {"b", h.bone}, {"s", h.seed},
            {"v", h.victimSlot}, {"z", h.zone}, {"e", h.energy}, {"x", h.exits}, {"h", h.helmet}, {"df", h.deflected}, {"m", A.model}};
  if (A.entryTri >= 0) { e["et"] = A.entryTri; e["eb"] = tojson(A.entryBary); }
  if (A.exitTri >= 0) { e["xt"] = A.exitTri; e["xb"] = tojson(A.exitBary); }
  return e;
}

// ---------------------------------------------------------------- one hit -> effects (host and clients alike)
void applyHit(App& app, const FleshHit& h, const GoreAttach& A) {
  ensureLoaded();
  ++gS.st.hits;
  GRng r(h.seed);
  if (h.helmet) {   // sparks, no gore (SPEC 7.4)
    if (gS.cfg.protectedFx) {
      app.fx.impact(h.pos, h.normal, "metal");
      vec3 refl = glm::reflect(h.dir, h.normal);
      for (int k = 0; k < 7; ++k) { vec3 d = coneDir(r, glm::normalize(refl + h.normal * 0.3f), 40); app.fx.streak(h.pos, h.pos + d * r.range(0.08f, 0.22f), 0.004f, r.range(0.04f, 0.09f), vec4(9.0f, 5.5f, 2.0f, 1)); }
    }
    ++gS.st.sparks; return;
  }
  if (h.deflected) {   // spawn protection: a deflect puff, no gore (SPEC 2.2 / 7.4)
    if (gS.cfg.protectedFx) rulesDeflectPuff(app, h.pos, h.normal, h.dir);   // W1's ward puff (rules.cpp), on every client
    ++gS.st.deflects; return;
  }
  if (gS.level == GoreLevel::Off) { app.fx.impact(h.pos, h.normal, "flesh"); ++gS.st.dustPuffs; return; }   // the pre-gore dust puff
  ++gS.st.goreHits;
  bool full = gS.level == GoreLevel::Full;
  const Soldier* s = victimOf(h.victimSlot);
  const CharModel* m = s ? modelOf(*s) : nullptr;
  bool attachOk = s && m && A.model == s->model;
  // wound points on the posed body (fall back to the capsule hit)
  vec3 entry = h.pos, en = -h.dir, exitP = h.pos + h.dir * 0.18f, xn = h.dir; bool hasExit = false;
  if (attachOk && A.entryTri >= 0) gorePosedPoint(h.victimSlot, A.entryTri, A.entryBary, entry, en);
  if (attachOk && A.exitTri >= 0) { hasExit = gorePosedPoint(h.victimSlot, A.exitTri, A.exitBary, exitP, xn); }
  // bone decals: entry, soak, exit
  if (attachOk && A.entryTri >= 0) {
    SoldierGore& rec = recOf(h.victimSlot);
    rec.wounds++; rec.lastWoundTri = A.entryTri; rec.lastWoundBary = A.entryBary;
    rec.dripUntil = gS.t + gS.cfg.dripDuration; rec.lastDrip = s->feet; rec.nextDrip = r.range(gS.cfg.dripSpacing);
    BoneDecal d; d.tri = A.entryTri; d.bary = A.entryBary; restPoint(*m, d.tri, d.bary, d.restPos, d.restN);
    d.joint = dominantJoint(*m, d.tri, d.bary); d.born = gS.t; d.seed = h.seed;
    d.kind = GD_ENTRY; d.radius = 0.5f * r.range(gS.cfg.entry); addBone(h.victimSlot, d);
    if (full) { BoneDecal k = d; k.kind = GD_SOAK; k.radius = 0.01f; k.growTo = r.range(gS.cfg.soak) * (0.7f + 0.3f * h.energy); k.growT = gS.cfg.soakGrow; k.seed = h.seed ^ 0x51u; addBone(h.victimSlot, k); }
    if (A.exitTri >= 0) {
      BoneDecal x; x.tri = A.exitTri; x.bary = A.exitBary; restPoint(*m, x.tri, x.bary, x.restPos, x.restN);
      x.joint = dominantJoint(*m, x.tri, x.bary); x.born = gS.t; x.seed = h.seed ^ 0xE1u; x.kind = GD_EXIT; x.radius = 0.5f * r.range(gS.cfg.exitW);
      addBone(h.victimSlot, x);
      if (full) { BoneDecal k = x; k.kind = GD_SOAK; k.radius = 0.015f; k.growTo = r.range(gS.cfg.soak); k.growT = gS.cfg.soakGrow; k.seed = h.seed ^ 0x77u; addBone(h.victimSlot, k); }
    }
    // spurting arteries: neck / upper thigh
    int z = goreZone(h);
    if ((z == 4 || z == 5) && (full || gS.cfg.redSpurt)) {
      GoreBleeder b; b.slot = h.victimSlot; b.lifeId = s->lifeId; b.tri = A.entryTri; b.bary = A.entryBary; b.t0 = gS.t; b.next = gS.t + 0.15;
      b.until = gS.t + r.range(gS.cfg.spurtDur); b.rng = h.seed * 747796405u + 1u;
      // bounded: a soldier spurts from at most spurtPerSoldier wounds, the world from spurtTotal (oldest stop first)
      int mine = 0; for (auto& o : gS.bleeders) mine += o.slot == b.slot;
      if (mine >= gS.cfg.spurtPerSoldier) for (size_t k = 0; k < gS.bleeders.size(); ++k) if (gS.bleeders[k].slot == b.slot) { gS.bleeders.erase(gS.bleeders.begin() + k); break; }
      while ((int)gS.bleeders.size() >= gS.cfg.spurtTotal && !gS.bleeders.empty()) gS.bleeders.erase(gS.bleeders.begin());
      if (gS.cfg.spurtPerSoldier > 0 && gS.cfg.spurtTotal > 0) gS.bleeders.push_back(b);
      gS.st.maxBleeders = std::max(gS.st.maxBleeders, (int)gS.bleeders.size());
    }
  }
  // spray: seeded droplets, or a mist puff only beyond mistOnlyBeyond
  bool localVictim = s && s->localPlayer && s->alive;
  float camD = glm::length(entry - camPos(app));
  std::vector<GoreDropInit> back, fwd;
  goreGenerateSpray(h, entry, exitP, hasExit || h.exits, gS.cfg, full ? 1.0f : gS.cfg.redDroplets, back, fwd);
  uint32_t burst = gS.nextBurst++;
  GRng mr(h.seed ^ 0xA5A5u);
  if (camD > gS.cfg.mistOnlyBeyond) {
    for (int k = 0; k < 3; ++k) puff(entry + h.dir * 0.1f * (float)k, h.dir * (0.8f + 0.6f * mr.u()) + vec3(0, 0.2f, 0), 0.12f, 1.1f, 0.35f, vec4(0.15f, 0.02f, 0.018f, 0.42f));
    ++gS.st.mistOnly;
  } else {
    if (!localVictim) { spawnDrops(back, burst); mist(entry, -h.dir, 1.0f, gS.cfg.back.mist, mr); }
    spawnDrops(fwd, burst);
    if (!localVictim) mist(hasExit ? exitP : entry + h.dir * 0.05f, h.dir, full ? 1.5f : 1.0f, gS.cfg.fwd.mist, mr);
  }
  // surface splatter: a ray 2.5 m along the bullet behind the target
  {
    vec3 o = (hasExit ? exitP : entry) + h.dir * 0.04f;
    Physics::Hit ph;
    if (Physics::castRayEx(o, h.dir, gS.cfg.splatterRay, ph)) {
      if (waterAt(app, ph.pos)) ripple(vec3(ph.pos.x, app.terrain.waterLevel, ph.pos.z));
      else {
        WorldDecal d; d.kind = GD_SPLAT; d.n = ph.normal; d.pos = ph.pos + ph.normal * 0.003f; d.born = gS.t; d.seed = h.seed ^ 0x5157u; d.burst = burst;
        vec3 tp = h.dir - ph.normal * glm::dot(h.dir, ph.normal);
        d.t = glm::length(tp) > 1e-3f ? glm::normalize(tp) : anyPerp(ph.normal);
        float frac = ph.t / gS.cfg.splatterRay;
        d.r1 = (0.07f + 0.2f * frac) * (0.6f + 0.4f * h.energy) * (full ? 1.0f : 0.7f);
        float asp; vec3 tail; goreTeardrop(h.dir, ph.normal, 3.0f, asp, tail);
        GoreStamp st; st.aspect = asp; st.radius = d.r1; d.stamps.push_back(st);
        d.extent = d.r1 * (1.6f + asp); d.depth = std::max(0.05f, d.r1 * 0.4f);
        addWorld(std::move(d)); ++gS.st.splats;
      }
    }
  }
}
}   // namespace

// ---------------------------------------------------------------- config
void GoreCfg::load(const json& j) {
  auto v2 = [](const json& a, vec2 d) { return a.is_array() && a.size() >= 2 ? vec2(a[0].get<float>(), a[1].get<float>()) : d; };
  level = levelFrom(j.value("level", std::string("full")));
  maxWorldDecals = j.value("maxWorldDecals", maxWorldDecals); maxDecalsPerCharacter = j.value("maxDecalsPerCharacter", maxDecalsPerCharacter); maxDroplets = j.value("maxDroplets", maxDroplets);
  mistOnlyBeyond = j.value("mistOnlyBeyond_m", mistOnlyBeyond); ageToBrown = j.value("decalAgeToBrown_s", ageToBrown); decalFade = j.value("decalFade_s", decalFade); drawDistance = j.value("decalDrawDistance_m", drawDistance);
  poolGrow = v2(j.value("poolGrow_s", json()), poolGrow); poolRadius = v2(j.value("poolRadius_m", json()), poolRadius); poolDelay = j.value("poolDelay_s", poolDelay);
  entry = v2(j.value("entryWound_m", json()), entry); exitW = v2(j.value("exitWound_m", json()), exitW); soak = v2(j.value("soak_m", json()), soak); soakGrow = j.value("soakGrow_s", soakGrow);
  auto spray = [&](const json& s, Spray& o) { if (!s.is_object()) return; o.count = v2(s.value("count", json()), o.count); o.cone = v2(s.value("coneDeg", json()), o.cone); o.speed = v2(s.value("speed", json()), o.speed); o.radiusMm = v2(s.value("radius_mm", json()), o.radiusMm); o.mist = s.value("mist_s", o.mist); };
  spray(j.value("backSpatter", json()), back); spray(j.value("forwardSpray", json()), fwd);
  splatterRay = j.value("splatterRay_m", splatterRay);
  if (j.contains("droplet")) { const json& d = j["droplet"]; gravity = d.value("gravity", gravity); airDensity = d.value("airDensity", airDensity); dragCd = d.value("dragCd", dragCd); dropLife = d.value("maxLife_s", dropLife); stainScale = d.value("stainScale", stainScale); maxAspect = d.value("teardropMaxAspect", maxAspect); }
  if (j.contains("spurt")) { const json& s = j["spurt"]; spurtInterval = s.value("interval_s", spurtInterval); spurtDur = v2(s.value("duration_s", json()), spurtDur); spurtCount = v2(s.value("count", json()), spurtCount); spurtSpeed = v2(s.value("speed", json()), spurtSpeed); spurtCone = s.value("coneDeg", spurtCone); spurtDecay = s.value("decay", spurtDecay); spurtPerSoldier = s.value("maxPerSoldier", spurtPerSoldier); spurtTotal = s.value("maxTotal", spurtTotal); }
  if (j.contains("drip")) { const json& s = j["drip"]; dripSpacing = v2(s.value("spacing_m", json()), dripSpacing); dripDuration = s.value("duration_s", dripDuration); dripMinSpeed = s.value("minSpeed", dripMinSpeed); }
  if (j.contains("reduced")) { const json& s = j["reduced"]; redDroplets = s.value("dropletScale", redDroplets); redWorld = s.value("worldDecalScale", redWorld); redChar = s.value("characterDecalScale", redChar); redPool = s.value("poolScale", redPool); redSpurt = s.value("spurt", redSpurt); redDrip = s.value("drip", redDrip); }
  protectedFx = j.value("protectedFx", protectedFx);
}

GoreState& goreState() { ensureLoaded(); return gS; }
GoreLevel goreLevel() { ensureLoaded(); return gS.level; }
int goreWorldCap() { ensureLoaded(); return gS.level == GoreLevel::Off ? 0 : gS.level == GoreLevel::Reduced ? (int)(gS.cfg.maxWorldDecals * gS.cfg.redWorld) : gS.cfg.maxWorldDecals; }
int goreCharCap() { ensureLoaded(); return gS.level == GoreLevel::Off ? 0 : gS.level == GoreLevel::Reduced ? (int)(gS.cfg.maxDecalsPerCharacter * gS.cfg.redChar) : gS.cfg.maxDecalsPerCharacter; }
void goreSetLevel(GoreLevel l, bool save) {
  ensureLoaded();
  gS.level = l;
  if (l == GoreLevel::Off) { gS.drops.clear(); gS.world.clear(); gS.sold.clear(); gS.bleeders.clear(); }
  capVec(gS.world, goreWorldCap());
  for (auto& [k, r] : gS.sold) capVec(r.decals, goreCharCap());
  if (save) { std::ofstream f(userFile()); f << json{{"level", levelName(l)}}.dump(2); }
}

// ---------------------------------------------------------------- pure helpers
void goreTeardrop(vec3 v, vec3 n, float maxAspect, float& aspect, vec3& tail) {
  float sp = glm::length(v);
  vec3 d = sp > 1e-6f ? v / sp : -n;
  float s = std::fabs(glm::dot(d, n));                 // sin of the angle between the path and the surface plane
  aspect = std::min(maxAspect, 1.0f / std::max(s, 1e-4f));
  vec3 tp = d - n * glm::dot(d, n);
  tail = glm::length(tp) > 1e-5f ? glm::normalize(tp) : anyPerp(n);
}

void goreGenerateSpray(const FleshHit& h, vec3 entry, vec3 exitPt, bool hasExit, const GoreCfg& c, float countScale,
                       std::vector<GoreDropInit>& back, std::vector<GoreDropInit>& fwd) {
  back.clear(); fwd.clear();
  GRng r(h.seed);
  float e = sat(h.energy);
  vec3 dir = glm::length(h.dir) > 1e-6f ? glm::normalize(h.dir) : vec3(0, 0, -1);
  // back-spatter: toward the shooter
  int nb = (int)std::lround(r.range(c.back.count) * countScale);
  float hb = r.range(c.back.cone);
  for (int k = 0; k < nb; ++k) {
    vec3 d = coneDir(r, -dir, hb);
    float sp = r.range(c.back.speed), rad = 0.001f * r.range(c.back.radiusMm);
    back.push_back({entry + d * 0.01f, d * sp, rad});
  }
  // forward spray: along the round, from the exit (or the entry); energy sets volume and speed
  float nf = c.fwd.count.x + (c.fwd.count.y - c.fwd.count.x) * e * r.range(0.6f, 1.0f);
  int nfw = (int)std::lround(std::clamp(nf, c.fwd.count.x, c.fwd.count.y) * countScale);
  float hf = r.range(c.fwd.cone);
  vec3 o = hasExit ? exitPt : entry + dir * 0.02f;
  for (int k = 0; k < nfw; ++k) {
    vec3 d = coneDir(r, dir, hf);
    float tt = r.u() * (0.35f + 0.65f * e);
    float sp = c.fwd.speed.x + (c.fwd.speed.y - c.fwd.speed.x) * tt;
    float u = r.u(); float rad = 0.001f * (c.fwd.radiusMm.x + (c.fwd.radiusMm.y - c.fwd.radiusMm.x) * u * std::sqrt(u) * (0.6f + 0.4f * e));
    fwd.push_back({o + d * 0.01f, d * sp, rad});
  }
}

uint64_t goreSprayHash(const FleshHit& h) {
  ensureLoaded();
  std::vector<GoreDropInit> b, f;
  goreGenerateSpray(h, h.pos, h.pos + h.dir * 0.25f, h.exits, gS.cfg, 1.0f, b, f);
  uint64_t x = 1469598103934665603ull;
  auto mix = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); x ^= u; x *= 1099511628211ull; };
  for (auto* L : {&b, &f}) { x ^= L->size(); x *= 1099511628211ull; for (auto& d : *L) { mix(d.p.x); mix(d.p.y); mix(d.p.z); mix(d.v.x); mix(d.v.y); mix(d.v.z); mix(d.r); } }
  return x;
}

vec3 goreAgeTint(float age, float ageToBrown) {   // same curve as the decal shaders
  float k = sat(age / std::max(1.0f, ageToBrown)); k = k * k * (3 - 2 * k);
  return glm::mix(vec3(0.62f, 0.035f, 0.03f), vec3(0.30f, 0.13f, 0.075f), k);
}

int goreZone(const FleshHit& h) {
  if (h.zone == 4 || h.zone == 5) return h.zone;
  if (h.bone == CJ_NECK) return 4;
  if ((h.bone == CJ_LUPLEG || h.bone == CJ_RUPLEG) && h.zone == 2) return 5;
  return h.zone;
}

bool gorePosedPoint(int slot, int tri, vec3 bary, vec3& pos, vec3& nrm) {
  const Soldier* s = victimOf(slot); if (!s) return false;
  const CharModel* m = modelOf(*s); if (!m || s->pose.skin.empty() || tri < 0 || (size_t)tri * 3 + 2 >= m->indices.size()) return false;
  uint32_t a = m->indices[tri * 3], b = m->indices[tri * 3 + 1], c = m->indices[tri * 3 + 2];
  vec3 p = skinP(*m, s->pose, a) * bary.x + skinP(*m, s->pose, b) * bary.y + skinP(*m, s->pose, c) * bary.z;
  vec3 n = skinN(*m, s->pose, a) * bary.x + skinN(*m, s->pose, b) * bary.y + skinN(*m, s->pose, c) * bary.z;
  pos = vec3(s->world * vec4(p, 1));
  nrm = glm::normalize(glm::mat3(s->world) * (glm::length(n) > 1e-6f ? n : vec3(0, 1, 0)));
  return true;
}

// ---------------------------------------------------------------- contract (gore_api.h)
void goreFleshHit(App& app, const FleshHit& h) {
  ++gGoreHitsReported;
  gGoreApp = &app;
  ensureLoaded();
  double c0 = nowMs();
  GoreAttach A = (h.helmet || h.deflected) ? GoreAttach{} : computeAttach(h);
  if (gNet && gNet->role == Net::HOST && gGame && gGame->active && !gGame->isClient) { gNet->hostWorldEvent(eventJson(h, A)); ++gS.st.netSent; }
  if (h.seed == kGoreProbeSeed) gS.sprayHashBySeed[h.seed] = goreSprayHash(h);
  applyHit(app, h, A);
  gS.st.hitMs += nowMs() - c0; ++gS.st.hitN;
}

void goreNetEvent(App& app, const json& ev) {
  gGoreApp = &app;
  ensureLoaded();
  FleshHit h;
  h.pos = jvec3(ev.value("p", json())); h.normal = jvec3(ev.value("n", json()), vec3(0, 1, 0)); h.dir = jvec3(ev.value("d", json()), vec3(0, 0, -1));
  h.weapon = ev.value("w", -1); h.bone = ev.value("b", -1); h.seed = ev.value("s", 0u); h.victimSlot = ev.value("v", -1); h.zone = ev.value("z", 1);
  h.energy = ev.value("e", 1.0f); h.exits = ev.value("x", false); h.helmet = ev.value("h", false); h.deflected = ev.value("df", false);
  GoreAttach A; A.model = ev.value("m", -1);
  if (ev.contains("et")) { A.entryTri = ev["et"]; A.entryBary = jvec3(ev["eb"], vec3(1.0f / 3)); }
  if (ev.contains("xt")) { A.exitTri = ev["xt"]; A.exitBary = jvec3(ev["xb"], vec3(1.0f / 3)); }
  const Soldier* s = victimOf(h.victimSlot);
  if (s && A.model != s->model && !h.helmet && !h.deflected) A = computeAttach(h);   // different character model here: place it locally
  ++gS.st.netReceived;
  if (h.seed == kGoreProbeSeed || gS.sprayHashBySeed.size() < 64) gS.sprayHashBySeed[h.seed] = goreSprayHash(h);
  applyHit(app, h, A);
}

void goreReset(App& app) {
  (void)app;
  gS.drops.clear(); gS.world.clear(); gS.sold.clear(); gS.bleeders.clear(); gS.puffs.clear();
}

void goreStep(App& app, float dt) {
  gGoreApp = &app;
  ensureLoaded();
  if (dt <= 0) return;
  double c0 = nowMs();
  gS.t += dt;
  const GoreCfg& c = gS.cfg;
  // droplets: ballistic with gravity and quadratic drag (a = -k |v| v, k = 3 rho_air Cd / (8 rho_water r))
  const float kDrag = 3.0f * c.airDensity * c.dragCd / (8.0f * 1000.0f);
  for (size_t i = 0; i < gS.drops.size();) {
    GoreDroplet& d = gS.drops[i];
    d.age += dt;
    bool dead = d.age > c.dropLife;
    if (!dead) {
      float sp = glm::length(d.v);
      d.v += (vec3(0, -c.gravity, 0) - (kDrag / d.r) * sp * d.v) * dt;
      vec3 np = d.p + d.v * dt, seg = np - d.p; float len = glm::length(seg);
      Physics::Hit ph;
      if (len > 1e-6f && Physics::castRayEx(d.p, seg / len, len, ph)) { landDroplet(app, ph.pos, ph.normal, d.v, d.r, d.burst); dead = true; }
      else if (waterAt(app, np)) { ripple(vec3(np.x, app.terrain.waterLevel, np.z)); dead = true; }
      else if (np.y < app.terrain.height(np.x, np.z) - 0.02f) { vec3 g(np.x, app.terrain.height(np.x, np.z), np.z); landDroplet(app, g, app.terrain.normal(g.x, g.z), d.v, d.r, d.burst); dead = true; }
      else d.p = np;
    }
    if (dead) { gS.drops[i] = gS.drops.back(); gS.drops.pop_back(); } else ++i;
  }
  // puffs / ripples
  for (auto& q : gS.puffs) { q.life -= dt; q.p += q.v * dt; q.v *= std::exp(-3.0f * dt); q.size = std::max(0.005f, q.size + q.grow * dt); }
  gS.puffs.erase(std::remove_if(gS.puffs.begin(), gS.puffs.end(), [](const GorePuff& q) { return q.life <= 0; }), gS.puffs.end());
  if (gS.puffs.size() > 600) gS.puffs.erase(gS.puffs.begin(), gS.puffs.begin() + (gS.puffs.size() - 600));
  bool full = gS.level == GoreLevel::Full;
  if (gS.level != GoreLevel::Off && gGame && gGame->active) {
    Game& G = *gGame;
    // spurting: a pulse every ~0.8 s (heart rate), weakening
    for (size_t i = 0; i < gS.bleeders.size();) {
      GoreBleeder& b = gS.bleeders[i];
      const Soldier* s = victimOf(b.slot);
      if (!s || !s->inUse || s->lifeId != b.lifeId || gS.t > b.until) { gS.bleeders.erase(gS.bleeders.begin() + i); continue; }
      if (gS.t >= b.next) {
        GRng r(b.rng); b.rng = r.next();
        vec3 p, n;
        if (gorePosedPoint(b.slot, b.tri, b.bary, p, n)) {
          int cnt = (int)std::lround(r.range(c.spurtCount) * b.strength * (full ? 1.0f : c.redDroplets));
          std::vector<GoreDropInit> v;
          vec3 axis = glm::normalize(n + vec3(0, 0.25f, 0));
          for (int k = 0; k < cnt; ++k) { vec3 d = coneDir(r, axis, c.spurtCone); v.push_back({p + n * 0.01f, d * r.range(c.spurtSpeed) * (0.5f + 0.5f * b.strength) + s->vel * 0.8f, 0.001f * r.range(1.0f, 2.5f)}); }
          spawnDrops(v, gS.nextBurst++);
          if (glm::length(p - camPos(app)) < c.mistOnlyBeyond) mist(p, axis, 0.6f * b.strength, 0.12f, r);
          ++gS.st.spurtPulses;
        }
        b.strength *= c.spurtDecay;
        b.next += c.spurtInterval * r.range(0.92f, 1.08f);
      }
      ++i;
    }
    for (auto& s : G.sold) {
      if (!s.inUse || s.lifeId == 0) continue;
      auto it = gS.sold.find(s.slot); if (it == gS.sold.end()) continue;
      SoldierGore& rec = it->second;
      if (rec.lifeId != s.lifeId) { rec = SoldierGore{}; rec.lifeId = s.lifeId; rec.model = s.model; continue; }   // new life: old decals went with the corpse
      // dripping trail while a wounded soldier moves
      if (s.alive && rec.wounds > 0 && gS.t < rec.dripUntil && (full || c.redDrip) && s.speed > c.dripMinSpeed && !(s.localPlayer)) {
        if (glm::length(vec2(s.feet.x - rec.lastDrip.x, s.feet.z - rec.lastDrip.z)) > rec.nextDrip) {
          GRng r((uint32_t)(s.slot * 7919 + rec.wounds * 104729) ^ (uint32_t)(gS.t * 1000));
          vec3 p, n;
          if (gorePosedPoint(s.slot, rec.lastWoundTri, rec.lastWoundBary, p, n)) {
            spawnDrops({{p + n * 0.02f, s.vel * 0.85f + vec3(0, -0.2f, 0), 0.001f * r.range(1.8f, 2.8f)}}, gS.nextBurst++);
            ++gS.st.drips;
          }
          rec.lastDrip = s.feet; rec.nextDrip = r.range(c.dripSpacing);
        }
      }
      // blood pool spreading under a corpse
      if (!s.alive && rec.wounds > 0 && !rec.pooled && G.t - s.diedAt > c.poolDelay) {
        rec.pooled = true;
        vec3 a = vec3(s.world * vec4(s.pose.joint[CJ_HIPS], 1)), b = vec3(s.world * vec4(s.pose.joint[CJ_SPINE2], 1));
        vec3 ctr = (a + b) * 0.5f;
        Physics::Hit ph;
        if (Physics::castRayEx(ctr + vec3(0, 0.6f, 0), vec3(0, -1, 0), 3.0f, ph) && !waterAt(app, ph.pos)) {
          GRng r((uint32_t)s.slot * 2246822519u ^ (uint32_t)s.lifeId * 3266489917u);
          WorldDecal d; d.kind = GD_POOL; d.pos = ph.pos; d.n = ph.normal; d.t = anyPerp(ph.normal); d.born = gS.t; d.seed = r.next();
          d.r0 = 0.06f; d.r1 = r.range(c.poolRadius) * (full ? 1.0f : c.redPool); d.growT = r.range(c.poolGrow);
          d.extent = d.r1 * 1.25f; d.depth = 0.3f;
          addWorld(std::move(d)); ++gS.st.pools;
        }
      }
    }
  }
  // oldest fades first: past 85 % of a budget the oldest decals fade out (the hard cap removes them at once if needed)
  auto fade = [&](auto& v, int cap) {
    int soft = (int)(cap * 0.85f);
    for (int k = 0; k < (int)v.size() - soft; ++k) if (v[k].fadeStart < 0) v[k].fadeStart = gS.t;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& d) { return d.fadeStart >= 0 && gS.t - d.fadeStart > c.decalFade; }), v.end());
    capVec(v, cap);
  };
  fade(gS.world, goreWorldCap());
  for (auto& [k, r] : gS.sold) fade(r.decals, goreCharCap());
  gS.st.stepMs += nowMs() - c0; ++gS.st.stepN;
}

// ---------------------------------------------------------------- hooks
void goreRenderPass(Renderer& r, const RenderView& v, const mat4& vp, unsigned depthTex) {
  ensureLoaded();
  // droplets in flight (motion streaks), mist and ripples go into the effect lists drawn after TAA
  mat4 inv = glm::inverse(v.view);
  vec3 right = vec3(inv[0]), up = vec3(inv[1]), eye = v.camPos, fwd = -vec3(inv[2]);
  float pxA = 2.0f / (std::max(1e-3f, v.proj[1][1]) * std::max(1, r.height));
  auto quad = [](std::vector<FxVert>& out, vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec4 col, float uvy) {
    out.push_back({p0, col, {0, uvy < 0 ? 0.0f : uvy}}); out.push_back({p1, col, {1, uvy < 0 ? 0.0f : uvy}}); out.push_back({p2, col, {1, uvy < 0 ? 1.0f : uvy}});
    out.push_back({p0, col, {0, uvy < 0 ? 0.0f : uvy}}); out.push_back({p2, col, {1, uvy < 0 ? 1.0f : uvy}}); out.push_back({p3, col, {0, uvy < 0 ? 1.0f : uvy}});
  };
  for (auto& d : gS.drops) {
    vec3 to = d.p - eye; float dist = glm::length(to);
    if (dist > 80 || glm::dot(to, fwd) < 0) continue;
    vec3 a = d.p, b = d.p - d.v * 0.02f;
    vec3 ax = b - a; if (glm::length(ax) < 1e-4f) ax = up * 0.002f;
    float w = std::max(d.r, 0.7f * pxA * dist);
    vec3 side = glm::cross(ax, to); side = glm::length(side) > 1e-8f ? glm::normalize(side) * w : right * w;
    float fadeA = std::min(1.0f, 2 * d.r / std::max(1e-6f, w));   // sub-pixel droplets thin out instead of growing
    quad(r.fxAlpha, a - side, a + side, b + side, b - side, vec4(0.16f, 0.007f, 0.006f, 0.95f * std::max(0.35f, fadeA)), 0.5f);
  }
  for (auto& q : gS.puffs) {
    float f = std::clamp(q.life / q.maxLife, 0.0f, 1.0f);
    vec4 cc = q.col; cc.a *= q.flat ? f : std::min(1.0f, f * 2.0f);
    if (q.flat) { vec3 x(q.size, 0, 0), z(0, 0, q.size); quad(r.fxAlpha, q.p - x - z, q.p + x - z, q.p + x + z, q.p - x + z, cc, -1); }
    else quad(r.fxAlpha, q.p - right * q.size - up * q.size, q.p + right * q.size - up * q.size, q.p + right * q.size + up * q.size, q.p - right * q.size + up * q.size, cc, -1);
  }
  if (gS.level == GoreLevel::Off) return;
  decalsRender(r, v, vp, depthTex);
}

void goreOptionsUI() {
  ensureLoaded();
  int l = (int)gS.level; const char* names[] = {"Off", "Reduced", "Full"};
  ImGui::SetNextItemWidth(140);
  if (ImGui::Combo("Gore", &l, names, 3)) goreSetLevel((GoreLevel)l, true);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blood impacts, spray, wounds and pools (SPEC 7).\nOff: hits show a dust puff only. Reduced: fewer droplets, smaller budgets, no spurting.");
}

json goreNetStats() {
  ensureLoaded();
  json j = {{"received", gS.st.netReceived}, {"sent", gS.st.netSent}, {"goreHits", gS.st.goreHits}, {"boneDecals", gS.st.boneDecals}, {"worldDecals", (int)gS.world.size()}, {"droplets", gS.st.spawned}};
  auto it = gS.sprayHashBySeed.find(kGoreProbeSeed);
  if (it != gS.sprayHashBySeed.end()) j["probe"] = std::to_string(it->second);
  return j;
}

void goreTestDroplet(vec3 p, vec3 v, float r) { ensureLoaded(); spawnDrops({{p, v, r}}, gS.nextBurst++); }
json goreEventJsonFor(const FleshHit& h) { ensureLoaded(); return eventJson(h, (h.helmet || h.deflected) ? GoreAttach{} : computeAttach(h)); }

uint64_t goreTestInjectHit(App& app) {
  if (!gGame || !gGame->active) return 0;
  Game& G = *gGame;
  for (auto& s : G.sold) {
    if (!s.alive || s.localPlayer || s.human || s.pose.skin.empty()) continue;
    vec3 chest = vec3(s.world * vec4(s.pose.joint[CJ_SPINE1], 1));
    vec3 dir = glm::normalize(vec3(std::sin(radians(s.yaw)), -0.05f, std::cos(radians(s.yaw))));
    vec3 o = chest - dir * 3.0f;
    HitZone hz;
    if (!G.hitTest(s, o, dir, 6.0f, hz)) continue;
    FleshHit h; h.pos = o + dir * hz.t; h.normal = -dir; h.dir = dir; h.victimSlot = s.slot; h.zone = hz.zone; h.energy = 1; h.exits = true; h.seed = kGoreProbeSeed;
    float bd = 1e9f; for (int j = 0; j < CJ_COUNT; ++j) { float dd = glm::length(vec3(s.world * vec4(s.pose.joint[j], 1)) - h.pos); if (dd < bd) { bd = dd; h.bone = j; } }
    goreFleshHit(app, h);
    return goreSprayHash(h);
  }
  return 0;
}
