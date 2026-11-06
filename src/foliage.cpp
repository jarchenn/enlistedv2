// Trees: asset build (stump / trunk split, leaf cards), runtime damage state machine and felling.
//   HP_bullet = 30 small-arms hits, HP_AT = 1 (AT / HE strike within 1.5 m of the trunk, or a direct hit).
//   When either reaches 0 the static tree is replaced by a static stump plus a dynamic fallen-trunk body
//   (canopy rides on the trunk), with an impulse pushing away from the hit.
// Leaf sway (vertex shader, render.cpp):  V_offset = D_wind * M * A * sin(f t + k_x X_world + k_z Z_world)
#include "app.h"
#include <algorithm>
#include <random>

void buildTreeAsset(AssetDef& a, const json& tj) {
  TreeDef& t = a.tree;
  t.on = true;
  t.hpBullet = tj.value("hpBullet", 30.0f); t.hpAT = tj.value("hpAT", 1.0f);
  t.stumpH = tj.value("stumpHeight", 0.4f); t.trunkR = tj.value("trunkRadius", 0.25f);
  t.mass = tj.value("mass", 300.0f); t.leafSize = tj.value("leafSize", 0.35f);
  t.leafCol = jvec3(tj.value("leafColor", json()), vec3(0.2f, 0.3f, 0.1f));
  for (auto& c : tj.value("canopy", json::array())) t.canopy.push_back(vec4(c[0], c[1], c[2], c[3]));
  // ---- split the trunk at the stump cut
  MeshData sm, tm; std::vector<Part> sc, tc;
  for (auto& p : a.parts) {
    float lo = p.center.y - p.size.y * 0.5f, hi = p.center.y + p.size.y * 0.5f;
    bool upright = std::fabs(glm::dot(p.rot * vec3(0, 1, 0), vec3(0, 1, 0))) > 0.999f;
    auto put = [&](Part q, bool stump) {
      if (!stump) q.center.y -= t.stumpH;
      appendPart(stump ? sm : tm, q, a.materials[q.mat], mat4(1));
      if (q.collide) (stump ? sc : tc).push_back(q);
    };
    if (hi <= t.stumpH + 1e-4f) put(p, true);
    else if (lo >= t.stumpH - 1e-4f || !upright || p.shape != Part::BOX) put(p, false);
    else {
      Part s = p; s.size.y = t.stumpH - lo; s.center.y = lo + s.size.y * 0.5f; put(s, true);
      Part u = p; u.size.y = hi - t.stumpH; u.center.y = t.stumpH + u.size.y * 0.5f; put(u, false);
    }
  }
  // the cut face: a pale disc of heartwood on the stump
  Material cutM{"cut", vec3(0.55f, 0.43f, 0.28f), 0.9f, 0.0f, PAT_WOOD, 0};
  Part cut; cut.center = vec3(0, t.stumpH - 0.005f, 0); cut.size = vec3(t.trunkR * 1.9f, 0.02f, t.trunkR * 1.9f); cut.collide = false;
  appendPart(sm, cut, cutM, mat4(1));
  t.stumpMesh.upload(sm); t.trunkMesh.upload(tm);
  t.stumpShape = Physics::buildShape(sc); t.trunkShape = Physics::buildShape(tc);
  // ---- leaf cards: deterministic per asset
  std::mt19937 rng(tj.value("seed", 1u));
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  int per = tj.value("cardsPerSphere", 60);
  float maxH = 0.1f; for (auto& c : t.canopy) maxH = std::max(maxH, glm::length(vec2(c.x, c.z)) + c.w);
  std::vector<float> v;
  t.top = t.stumpH;
  for (auto& c : t.canopy) {
    t.top = std::max(t.top, c.y + c.w);
    for (int n = 0; n < per; ++n) {
      float z = U(rng) * 2 - 1, ph = U(rng) * 6.2831853f, rr = std::sqrt(1 - z * z);
      vec3 dir(rr * std::cos(ph), z, rr * std::sin(ph));
      vec3 ctr = vec3(c) + dir * c.w * (0.45f + 0.55f * std::cbrt(U(rng)));
      vec3 nrm = glm::normalize(dir + vec3(U(rng) - 0.5f, U(rng) - 0.5f, U(rng) - 0.5f) * 1.2f);
      vec3 T = glm::normalize(glm::cross(std::fabs(nrm.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0), nrm)), B = glm::cross(nrm, T);
      float s = t.leafSize * (0.7f + 0.6f * U(rng)) * c.w * 0.9f;
      float mask = std::clamp(glm::length(vec2(ctr.x, ctr.z)) / maxH * 0.8f + 0.2f, 0.0f, 1.0f);   // 0 at the trunk axis, 1 at the outer tips
      vec3 q[4] = {ctr - T * s - B * s, ctr + T * s - B * s, ctr + T * s + B * s, ctr - T * s + B * s};
      vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      int idx[6] = {0, 1, 2, 0, 2, 3};
      for (int k : idx) { v.insert(v.end(), {q[k].x, q[k].y, q[k].z, dir.x, dir.y, dir.z, uv[k].x, uv[k].y, mask}); }
    }
  }
  t.cardVerts = (int)v.size() / 9;
  glCreateVertexArrays(1, &t.cardVao); glCreateBuffers(1, &t.cardVbo);
  glNamedBufferStorage(t.cardVbo, v.size() * 4, v.data(), 0);
  glVertexArrayVertexBuffer(t.cardVao, 0, t.cardVbo, 0, 36);
  int comps[4] = {3, 3, 2, 1}, off = 0;
  for (int k = 0; k < 4; ++k) { glEnableVertexArrayAttrib(t.cardVao, k); glVertexArrayAttribFormat(t.cardVao, k, comps[k], GL_FLOAT, GL_FALSE, off * 4); glVertexArrayAttribBinding(t.cardVao, k, 0); off += comps[k]; }
}

// ---------------------------------------------------------------- runtime
static constexpr uint64_t kTrunkUD = 0xFFF1u;

void App::buildTreeRT(const Instance& i, InstanceRT& r) {
  const TreeDef& t = r.asset->tree;
  r.treeHits = 0; r.treeFelled = false; r.trunkBody = 0xffffffffu;
  if (!i.extra.contains("tree") || !i.extra["tree"].value("felled", false)) return;
  // applied (persistent) felled state: stump plus the trunk lying where it came to rest, both static
  Physics::remove(r.staticBody);
  r.staticBody = Physics::addStatic(t.stumpShape, i.pos, yawQuat(i.yaw), i.id << 16);
  auto& tr = i.extra["tree"]["trunk"];
  vec3 p = i.pos + yawQuat(i.yaw) * jvec3(tr["p"]);
  quat q = yawQuat(i.yaw) * quat(tr["q"][3].get<float>(), tr["q"][0].get<float>(), tr["q"][1].get<float>(), tr["q"][2].get<float>());
  r.trunkBody = Physics::addStatic(t.trunkShape, p, q, (i.id << 16) | kTrunkUD);
  r.treeFelled = true; r.trunkApplied = true;
}

void App::fellTree(Instance& i, InstanceRT& r, vec3 at, vec3 push) {
  if (r.treeFelled) return;
  const TreeDef& t = r.asset->tree;
  Physics::remove(r.staticBody);
  r.staticBody = Physics::addStatic(t.stumpShape, i.pos, yawQuat(i.yaw), i.id << 16);
  vec3 base = i.pos + vec3(0, t.stumpH, 0);
  r.trunkBody = Physics::addDynamic(t.trunkShape, base + vec3(0, 0.02f, 0), yawQuat(i.yaw), (i.id << 16) | kTrunkUD, t.mass);
  vec3 h = vec3(push.x, 0, push.z);
  if (glm::dot(h, h) < 1e-6f) h = vec3(at.x - i.pos.x, 0, at.z - i.pos.z) * -1.0f;
  if (glm::dot(h, h) < 1e-6f) h = vec3(1, 0, 0);
  h = glm::normalize(h);
  // topple: a horizontal shove high on the trunk (impulse from the hit side) plus a little lift off the cut
  float lever = std::max(1.0f, (t.top - t.stumpH) * 0.6f);
  Physics::addImpulse(r.trunkBody, h * t.mass * 1.6f, base + vec3(0, lever, 0));
  Physics::addImpulse(r.trunkBody, vec3(0, t.mass * 0.6f, 0), base);
  r.treeFelled = true; r.trunkApplied = false;
  ++treesFelled;
  audio.play("tree_fall", at, 1.0f);
  for (int n = 0; n < 14; ++n) fx.smoke(base + vec3(fx.rnd() - 0.5f, fx.rnd() * 0.3f, fx.rnd() - 0.5f) * 0.6f, vec3(fx.rnd() - 0.5f, 0.6f, fx.rnd() - 0.5f), 0.25f, 1.6f, vec4(0.45f, 0.4f, 0.3f, 0.4f));
}

bool App::treeImpact(uint64_t iid, vec3 at, vec3 dir, bool shell) {
  Instance* i = doc.find(iid);
  auto it = rt.find(iid);
  if (!i || it == rt.end() || !it->second.asset->tree.on) return false;
  InstanceRT& r = it->second;
  if (r.treeFelled) return true;
  if (shell) { fellTree(*i, r, at, dir); return true; }
  r.treeHits += 1;
  fx.impact(at, -dir, "wood");
  if (r.treeHits >= r.asset->tree.hpBullet) fellTree(*i, r, at, dir);
  return true;
}

void App::blastTrees(vec3 p, float radius) {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    const TreeDef& t = r.asset->tree;
    if (!t.on || r.treeFelled) continue;
    // distance from the blast to the trunk axis (stump cut .. first fork)
    float y = std::clamp(p.y - i.pos.y, 0.0f, std::max(t.stumpH, t.top * 0.5f));
    float d = glm::length(p - (i.pos + vec3(0, y, 0))) - t.trunkR;
    if (d <= std::min(1.5f, radius)) fellTree(i, r, p, i.pos - p);
  }
}

void App::resetTrees() {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    if (!r.asset->tree.on) continue;
    bool applied = i.extra.contains("tree") && i.extra["tree"].value("felled", false);
    if (r.treeHits != 0 || r.treeFelled != applied || (r.treeFelled && !r.trunkApplied)) buildRT(i, r);
  }
}

void App::treeDrawItems(const Instance& i, const InstanceRT& r, std::vector<DrawItem>& op) {
  const TreeDef& t = r.asset->tree;
  mat4 M = instanceMatrix(i);
  if (!r.treeFelled) {
    op.push_back({&r.asset->opaque, M});
    R.foliage.push_back({&t, M});
    return;
  }
  op.push_back({&t.stumpMesh, M});
  vec3 p; quat q;
  if (Physics::getTransform(r.trunkBody, p, q)) {
    mat4 T = glm::translate(mat4(1), p) * glm::mat4_cast(q);
    op.push_back({&t.trunkMesh, T});
    R.foliage.push_back({&t, T * glm::translate(mat4(1), vec3(0, -t.stumpH, 0))});
  }
}

json App::treeApplyState(const Instance& i, const InstanceRT& r) const {
  vec3 p; quat q;
  if (!Physics::getTransform(r.trunkBody, p, q)) return json();
  quat inv = glm::inverse(yawQuat(i.yaw));
  vec3 lp = inv * (p - i.pos); quat lq = inv * q;
  return json{{"felled", true}, {"trunk", {{"p", tojson(lp)}, {"q", {lq.x, lq.y, lq.z, lq.w}}}}};
}
