// Individual pathing and movement (AI_SPEC 3.4-3.6):
//   - PathService: a worker thread runs Detour sliced A* per request (time-sliced under the navmesh read lock);
//     the main thread starts at most N requests per step, highest priority first (bots under fire first).
//   - BotFilter: per-bot costs: area cost x threat (viewsheds of team knowledge, x2-5 by difficulty) x crowding
//     (teammates' claimed routes) x lane bias, + link traversal time, x50 near edges this bot got stuck on.
//   - move(): corridor following with Detour's funnel (findCorners) plus visibility / topology optimisation,
//     teammate separation (6 m while advancing), sampled velocity-obstacle avoidance (Detour's RVO) against
//     soldiers, navmesh walls and freshly probed geometry, clamped to the mesh (never off it, never into a forbidden
//     area), doors opened through the shared interaction, off-mesh links traversed with scripted player-like moves.
//   - stuckLadder(): progress monitor and the six-step recovery ladder, every event logged.
#include "ai_core.h"
#include "app.h"
#include "crumb.h"
#include <DetourNavMesh.h>
#include <DetourCommon.h>
#include <algorithm>
#include <cmath>

namespace {
bool isLeafUD(uint64_t u) { if (u == 0 || (u >> 60) == 0xF) return false; uint64_t l = u & 0xFFFF; return l != 0 && l != 0xFFFF && l != 0xFFF0; }
float yawOf(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
float angDiff(float a, float b) { return wrapDeg(a - b + 180.0f) - 180.0f; }
vec3 xz(vec3 v) { return vec3(v.x, 0, v.z); }
float lenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); }
}

// ---------------------------------------------------------------- filter
float BotFilter::getCost(const float* pa, const float* pb, const dtPolyRef prevRef, const dtMeshTile* prevTile, const dtPoly* prevPoly,
                         const dtPolyRef curRef, const dtMeshTile* curTile, const dtPoly* curPoly,
                         const dtPolyRef nextRef, const dtMeshTile* nextTile, const dtPoly* nextPoly) const {
  float base = dtQueryFilter::getCost(pa, pb, prevRef, prevTile, prevPoly, curRef, curTile, curPoly, nextRef, nextTile, nextPoly);
  if (curPoly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
    int k = curPoly->getArea() - navarea::kLinkDrop;
    float c = base + (k >= 0 && k < 4 ? linkExtra[k] : 2.0f);
    for (auto& p : penalties) if (glm::length(vec2(pa[0] - p.p.x, pa[2] - p.p.z)) < p.r && std::fabs(pa[1] - p.p.y) < 2.5f) c *= 50.0f;   // a link that blocked this bot
    return c;
  }
  vec3 mid((pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f);
  float mul = 1.0f;
  if (threat) {
    float e = 0;
    for (auto& v : threat->v) if (v.conf > e && v.sees(mid)) e = v.conf;
    if (e > 0) mul *= 1.0f + (threatCost - 1.0f) * e;
  }
  if (crowd) { int c = crowd->claims(mid, self); if (c) mul *= 1.0f + crowdCost * (float)std::min(c, 3); }
  if (laneOn) {
    vec2 r(mid.x - laneOrigin.x, mid.z - laneOrigin.y);
    float along = glm::dot(r, laneAxis), lat = glm::dot(r, laneRight);
    if (along < 0) {                                       // only on the approach: lanes converge at the objective
      float dev = std::fabs(lat - laneOffset) / laneHalfWidth;
      mul *= dev < 1 ? laneGain : std::min(1.5f, laneGain + 0.25f * (dev - 1.0f));
    }
  }
  for (auto& p : penalties) if (glm::length(vec2(mid.x - p.p.x, mid.z - p.p.z)) < p.r && std::fabs(mid.y - p.p.y) < 2.5f) mul *= 50.0f;
  return base * mul;
}

// ---------------------------------------------------------------- path service
PathService::~PathService() { stop(); }
void PathService::start(Nav* nav) {
  stop();
  nav_ = nav; quit_ = false;
  th_.emplace_back([this] { loop(); });
}
void PathService::stop() {
  { std::lock_guard<std::mutex> l(m_); quit_ = true; q_.clear(); }
  cv_.notify_all();
  for (auto& t : th_) t.join();
  th_.clear();
  done_.clear();
}
int PathService::submit(int slot, int life, NavRef s, vec3 sp, NavRef e, vec3 ep, std::unique_ptr<BotFilter> f, float pri) {
  std::lock_guard<std::mutex> l(m_);
  for (auto it = q_.begin(); it != q_.end();) { if (it->slot == slot) it = q_.erase(it); else ++it; }   // newest wins
  Req r{next_++, slot, life, s, e, sp, ep, std::move(f), pri};
  auto pos = std::find_if(q_.begin(), q_.end(), [&](const Req& o) { return o.pri < pri; });
  int id = r.id;
  q_.insert(pos, std::move(r));
  cv_.notify_one();
  return id;
}
void PathService::cancelSlot(int slot) {
  std::lock_guard<std::mutex> l(m_);
  for (auto it = q_.begin(); it != q_.end();) { if (it->slot == slot) it = q_.erase(it); else ++it; }
  done_.erase(std::remove_if(done_.begin(), done_.end(), [&](const PathResult& r) { return r.slot == slot; }), done_.end());
}
std::vector<PathResult> PathService::poll() { std::lock_guard<std::mutex> l(m_); std::vector<PathResult> out; out.swap(done_); return out; }
int PathService::inFlight() const { std::lock_guard<std::mutex> l(m_); return (int)q_.size() + running_; }

void PathService::loop() {
  dtNavMeshQuery* q = dtAllocNavMeshQuery();
  dtNavMesh* bound = nullptr;
  std::vector<dtPolyRef> path(256);
  for (;;) {
    Req r;
    {
      std::unique_lock<std::mutex> l(m_);
      cv_.wait(l, [&] { return quit_ || !q_.empty(); });
      if (quit_) break;
      r = std::move(q_.front()); q_.pop_front(); ++running_;
    }
    auto t0 = std::chrono::steady_clock::now();
    PathResult res; res.id = r.id; res.slot = r.slot; res.life = r.life; res.target = r.ep;
    for (int attempt = 0; attempt < 3; ++attempt) {
      std::shared_lock<std::shared_mutex> rl(nav_->sharedLock());
      dtNavMesh* mesh = nav_->mesh();
      if (!mesh) break;
      if (mesh != bound) { q->init(mesh, 8192); bound = mesh; }
      uint32_t stamp = nav_->stamp();
      if (!mesh->isValidPolyRef(r.s) || !mesh->isValidPolyRef(r.e)) break;
      dtStatus st = q->initSlicedFindPath(r.s, r.e, &r.sp.x, &r.ep.x, r.f.get());
      bool restart = false;
      while (dtStatusInProgress(st)) {
        int it = 0;
        st = q->updateSlicedFindPath(384, &it);
        if (dtStatusInProgress(st)) {
          rl.unlock();                                         // let the main thread commit tiles between slices
          std::this_thread::yield();
          rl.lock();
          if (nav_->mesh() != mesh || nav_->stamp() != stamp) { restart = true; break; }
        }
      }
      if (restart) continue;
      if (dtStatusFailed(st)) break;
      int n = 0;
      st = q->finalizeSlicedFindPath(path.data(), &n, (int)path.size());
      if (dtStatusFailed(st) || n == 0) break;
      res.ok = true; res.partial = path[n - 1] != r.e || dtStatusDetail(st, DT_PARTIAL_RESULT);
      res.polys.assign(path.begin(), path.begin() + n);
      res.navStamp = stamp;
      break;
    }
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::lock_guard<std::mutex> l(m_);
    --running_;
    (res.ok ? completed : failed)++; msSum += res.ms; msMax = std::max(msMax, res.ms);
    done_.push_back(std::move(res));
  }
  dtFreeNavMeshQuery(q);
}

// ---------------------------------------------------------------- requests
std::unique_ptr<BotFilter> AiCore::makeFilter(Soldier& s) {
  BotMind& m = mind(s);
  auto f = std::make_unique<BotFilter>();
  const dtQueryFilter& d = G.nav.defaultFilter();
  f->setIncludeFlags(d.getIncludeFlags()); f->setExcludeFlags(d.getExcludeFlags());
  for (int a = 0; a < DT_MAX_AREAS; ++a) f->setAreaCost(a, d.getAreaCost(a));
  TeamBrain& tb = teams[(int)s.role];
  f->threat = tb.threat; f->threatCost = diff.threatCost;
  f->crowd = tb.crowd; f->crowdCost = tj("pathing", "crowdCost", 0.6f); f->self = s.slot;
  for (int k = 0; k < 4; ++k) f->linkExtra[k] = G.nav.cfg.linkCost[k];
  for (auto& p : m.penalties) if (p.until > G.t) f->penalties.push_back(p);
  const Objective* ob = G.match.activeObjective();
  if (ob && s.role == CombatRole::Attacker) {
    f->laneOn = true;
    vec3 fw = attackForward(ob->attackYaw), rt = attackRight(ob->attackYaw);
    f->laneAxis = glm::normalize(vec2(fw.x, fw.z)); f->laneRight = glm::normalize(vec2(rt.x, rt.z));
    f->laneOrigin = vec2(ob->pos.x, ob->pos.z) - f->laneAxis * (ob->extent() + 6.0f);
    float off = std::max(tj("pathing", "flankOffset_m", 35.0f), ob->extent() * 1.6f);
    f->laneOffset = s.br.lane == LANE_LEFT ? -off : s.br.lane == LANE_RIGHT ? off : 0.0f;
    f->laneHalfWidth = tj("pathing", "laneHalfWidth_m", 18.0f);
    f->laneGain = s.br.lane == LANE_MAIN ? tj("pathing", "mainLaneGain", 0.9f) : tj("pathing", "flankLaneGain", 0.7f);
  }
  return f;
}

void AiCore::requestPath(Soldier& s, vec3 goal, float radius, float priority, bool force) {
  BotMind& m = mind(s);
  bool same = glm::length(goal - m.goal) < 0.75f;
  if (!force && same && (m.hasPath || m.wantPath || m.pendingReq) && !m.partial && G.t - m.lastReplan < 6.0) return;
  if (!force && same && m.pendingReq) return;
  m.goal = goal; m.goalRadius = radius; m.wantPath = true; m.priority = priority;
  if (!same) ++m.goalSerial;
}

void AiCore::dispatchPaths() {
  if (!G.nav.valid()) return;
  std::vector<std::pair<float, int>> want;
  for (auto& s : G.sold) {
    if (!s.alive || s.human || s.agent < 0 || s.slot >= (int)minds.size() || !minds[s.slot]) continue;
    BotMind& m = *minds[s.slot];
    if (!m.wantPath || m.trav.on) continue;
    float pri = m.priority + (G.t - s.br.hurtAt < 3 || s.br.suppressed > 0.3f ? 5.0f : 0.0f) + (float)std::min(3.0, G.t - m.lastReplan);
    want.push_back({pri, s.slot});
  }
  std::sort(want.begin(), want.end(), [](auto& a, auto& b) { return a.first > b.first; });
  int started = 0;
  for (auto& [pri, slot] : want) {
    if (started >= pathStartsPerFrame) break;
    Soldier& s = G.sold[slot]; BotMind& m = *minds[slot];
    NavRef sr = 0, er = 0; vec3 sp, ep;
    const dtQueryFilter& f = G.nav.defaultFilter();
    bool okS = G.nav.closest(s.feet + vec3(0, 0.3f, 0), vec3(1.0f, 1.6f, 1.0f), sr, sp, &f) || G.nav.closest(s.feet + vec3(0, 0.3f, 0), vec3(3.0f, 3.0f, 3.0f), sr, sp, &f);
    vec3 g = m.hasSubGoal ? m.subGoal : m.goal;
    bool okE = G.nav.closest(g + vec3(0, 0.3f, 0), vec3(2.0f, 3.0f, 2.0f), er, ep, &f) || G.nav.closest(g + vec3(0, 0.3f, 0), vec3(6.0f, 6.0f, 6.0f), er, ep, &f);
    m.wantPath = false; m.lastReplan = G.t;
    ++started;
    if (!okS || !okE) { m.hasPath = false; ++m.pathFails; m.pathFailAt = G.t; s.br.why = !okS ? "off the navmesh" : "goal off the navmesh"; continue; }
    m.pendingReq = paths.submit(slot, s.lifeId, sr, sp, er, ep, makeFilter(s), pri);
    ++G.pathCalls;
  }
}

void AiCore::receivePaths() {
  dtNavMesh* mesh = G.nav.mesh();
  for (auto& r : paths.poll()) {
    G.pathMs += r.ms;
    if (r.slot < 0 || r.slot >= (int)G.sold.size() || r.slot >= (int)minds.size() || !minds[r.slot]) continue;
    Soldier& s = G.sold[r.slot]; BotMind& m = *minds[r.slot];
    if (!s.alive || s.lifeId != r.life || m.pendingReq != r.id) continue;
    m.pendingReq = 0;
    if (!r.ok || !mesh || !mesh->isValidPolyRef(r.polys.front())) { m.hasPath = false; ++m.pathFails; m.pathFailAt = G.t; s.br.why = "no route"; continue; }
    dtPathCorridor& c = *m.corridor;
    c.reset(r.polys.front(), &s.feet.x);
    c.setCorridor(&r.target.x, r.polys.data(), (int)r.polys.size());
    // the bot moved while the query ran: re-anchor the corridor at the current position
    c.movePosition(&s.feet.x, G.nav.query(), &G.nav.defaultFilter());
    m.hasPath = true; m.partial = r.partial; m.pathAt = G.t; m.pathNavStamp = r.navStamp; m.pathFails = r.partial ? m.pathFails + 0.5f : 0;
    m.pathTiles.clear();
    for (NavRef p : r.polys) {
      const dtMeshTile* t = nullptr; const dtPoly* pp = nullptr;
      if (dtStatusFailed(mesh->getTileAndPolyByRef(p, &t, &pp))) continue;
      if (t && t->header) { auto k = std::make_pair(t->header->x, t->header->y); if (m.pathTiles.empty() || m.pathTiles.back() != k) m.pathTiles.push_back(k); }
    }
    // straight route (overlay, exposure estimate)
    float sp[64 * 3]; unsigned char sf[64]; dtPolyRef srf[64]; int ns = 0;
    G.nav.query()->findStraightPath(c.getPos(), c.getTarget(), c.getPath(), c.getPathCount(), sp, sf, srf, &ns, 64, 0);
    m.route.clear(); for (int k = 0; k < ns; ++k) m.route.push_back(vec3(sp[k * 3], sp[k * 3 + 1], sp[k * 3 + 2]));
  }
}

bool AiCore::arrived(Soldier& s, float r) const {
  if (s.slot >= (int)minds.size() || !minds[s.slot]) return true;
  const BotMind& m = *minds[s.slot];
  if (m.hasSubGoal) return false;
  float rr = r > 0 ? r : m.goalRadius;
  return lenXZ(s.feet - m.goal) < rr && std::fabs(s.feet.y - m.goal.y) < 2.0f;
}

float AiCore::pathRemaining(Soldier& s) const {
  if (s.slot >= (int)minds.size() || !minds[s.slot]) return 0;
  const BotMind& m = *minds[s.slot];
  if (!m.hasPath || m.route.empty()) return lenXZ(m.goal - s.feet);
  float L = 0; vec3 p = s.feet; size_t start = 0; float bd = 1e9f;
  for (size_t k = 0; k < m.route.size(); ++k) { float d = glm::length(m.route[k] - s.feet); if (d < bd) { bd = d; start = k; } }
  for (size_t k = start; k < m.route.size(); ++k) { L += glm::length(m.route[k] - p); p = m.route[k]; }
  return L;
}

// ---------------------------------------------------------------- link traversal (player-like moves)
void AiCore::startTraversal(Soldier& s, BotMind& m, NavRef offRef, vec3 sp, vec3 ep) {
  BotMind::Trav& tr = m.trav;
  tr = BotMind::Trav{}; tr.on = true; tr.t = 0;
  const dtOffMeshConnection* con = G.nav.mesh()->getOffMeshConnectionByRef(offRef);
  const NavLink* l = con ? G.nav.linkById(con->userId) : nullptr;
  tr.link = con ? con->userId : 0;
  unsigned char area = 0; G.nav.mesh()->getPolyArea(offRef, &area);
  tr.kind = navarea::isLink(area) ? area - navarea::kLinkDrop : LINK_MANTLE;
  vec3 a = s.feet, b = ep;
  { NavRef r; vec3 on; if (G.nav.closest(b + vec3(0, 0.2f, 0), vec3(0.7f, 1.0f, 0.7f), r, on)) b = on; }   // land on the mesh as it is now (links predate area marks)
  float top = l ? l->top : std::max(a.y, b.y);
  float h = std::fabs(b.y - a.y), len = lenXZ(b - a);
  switch (tr.kind) {
    case LINK_DROP:
      tr.p[0] = a; tr.p[1] = vec3(glm::mix(a.x, b.x, 0.45f), a.y + 0.05f, glm::mix(a.z, b.z, 0.45f)); tr.p[2] = vec3(b.x, a.y + 0.02f, b.z); tr.p[3] = b; tr.np = 4;
      tr.dur = 0.3f + std::sqrt(2 * h / std::max(1.0f, gravity)); break;
    case LINK_VAULT: {
      float hi = std::max(top, std::max(a.y, b.y)) + 0.1f;
      tr.p[0] = a; tr.p[1] = vec3(glm::mix(a.x, b.x, 0.25f), hi, glm::mix(a.z, b.z, 0.25f)); tr.p[2] = vec3(glm::mix(a.x, b.x, 0.88f), hi, glm::mix(a.z, b.z, 0.88f)); tr.p[3] = b; tr.np = 4;   // over, then down once the body has cleared the far face
      tr.dur = std::clamp((len + 2 * (hi - a.y)) / 2.6f, 0.45f, 1.1f); break; }
    case LINK_JUMP:
      tr.p[0] = a; tr.p[1] = b; tr.np = 2;
      tr.dur = std::max(0.45f, 2.0f * jumpV / std::max(1.0f, gravity)); break;
    default:   // mantle: up = rise in place then over; down = over then lower
      if (b.y > a.y) { tr.p[0] = a; tr.p[1] = vec3(a.x, b.y + 0.12f, a.z); tr.p[2] = vec3(b.x, b.y + 0.08f, b.z); tr.p[3] = b; tr.np = 4; tr.dur = 0.55f + 0.5f * h; }
      else { tr.p[0] = a; tr.p[1] = vec3(b.x, a.y + 0.08f, b.z); tr.p[2] = b; tr.np = 3; tr.dur = 0.45f + 0.35f * h; }
      break;
  }
  tr.end = b;
  ++m.linksTaken; ++metrics.linkTraversals; ++metrics.linkKinds[std::clamp(tr.kind, 0, 3)];
  s.br.why = std::string("traversing: ") + navLinkName(tr.kind);
}

bool AiCore::stepTraversal(Soldier& s, BotMind& m, float dt) {
  BotMind::Trav& tr = m.trav;
  tr.t += dt;
  float u = std::clamp(tr.t / tr.dur, 0.0f, 1.0f);
  vec3 p;
  if (tr.kind == LINK_JUMP) {
    float H = jumpV * jumpV / (2 * std::max(1.0f, gravity));
    p = glm::mix(tr.p[0], tr.p[1], u); p.y += 4 * H * u * (1 - u);
  } else {
    float L = 0, seg[3]; for (int k = 0; k + 1 < tr.np; ++k) { seg[k] = glm::length(tr.p[k + 1] - tr.p[k]); L += seg[k]; }
    float d = (tr.kind == LINK_DROP && u > 0.4f ? 0.4f + (u - 0.4f) * (u - 0.4f) / 0.6f : u) * L;   // drop: accelerate down
    p = tr.p[tr.np - 1];
    for (int k = 0; k + 1 < tr.np; ++k) { if (d <= seg[k]) { p = glm::mix(tr.p[k], tr.p[k + 1], seg[k] > 1e-4f ? d / seg[k] : 1.0f); break; } d -= seg[k]; }
  }
  int post = tr.kind == LINK_MANTLE || tr.kind == LINK_VAULT ? Physics::CROUCH : Physics::STAND;
  bool fits = Physics::shapeFitsAt(post, p + vec3(0, 0.04f, 0), true);
  if (!fits && u < 0.98f) {
    // someone / something in the way: stop where the body still fits, re-plan
    tr.on = false; ++metrics.linkAborts;
    m.penalties.push_back({tr.p[0], 1.2f, G.t + tj("stuck", "penalty_s", 20.0f)});   // this bot avoids that link for a while
    if (metrics.linkAborts <= 15) logf("ai: bot %d %s link blocked at (%.1f %.1f %.1f) -> (%.1f %.1f %.1f), u %.2f", s.slot, navLinkName(tr.kind), tr.p[0].x, tr.p[0].y, tr.p[0].z, tr.end.x, tr.end.y, tr.end.z, u);
    requestPath(s, m.hasSubGoal ? m.subGoal : m.goal, m.goalRadius, 6.0f, true);
    s.br.why = "link blocked: re-planning";
    return false;
  }
  vec3 prev = s.feet;
  if (fits) Physics::setAgentPos(s.agent, p);
  s.feet = fits ? p : s.feet;
  s.vel = (s.feet - prev) / std::max(dt, 1e-4f); s.speed = lenXZ(s.vel);
  s.posture = post;
  vec3 dir = tr.end - tr.p[0]; if (lenXZ(dir) > 0.1f) s.yaw = wrapDeg(yawOf(xz(dir)));
  if (u >= 1.0f) {
    tr.on = false;
    Physics::setAgentPos(s.agent, tr.end + vec3(0, 0.03f, 0)); s.feet = tr.end;
    m.progAnchor = s.feet; m.progT = 0;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- movement (every step)
void AiCore::move(Soldier& s, float dt) {
  BotMind& m = mind(s);
  Brain& b = s.br;
  if (m.trav.on) { stepTraversal(s, m, dt); return; }
  dtNavMeshQuery* q = G.nav.query();
  const dtQueryFilter* f = &G.nav.defaultFilter();
  vec3 pos = s.feet;
  vec3 desired(0); bool wantsMove = false; bool crouchArea = false; bool offMesh = false;
  float vmax = m.speed == SPD_SPRINT ? vSprint : m.speed == SPD_JOG ? vJog : m.speed == SPD_WALK ? vWalk : m.speed == SPD_CROUCH ? vCrouch : m.speed == SPD_CRAWL ? vProne : 0.0f;
  if (m.doorWait > 0) m.doorWait -= dt;
  if (m.hasPath && q && m.speed != SPD_STOP && m.doorWait <= 0) {
    dtPathCorridor& c = *m.corridor;
    c.movePosition(&pos.x, q, f);
    vec3 cpos(c.getPos()[0], c.getPos()[1], c.getPos()[2]);
    offMesh = lenXZ(cpos - pos) > 0.35f;   // standing where the mesh no longer is (an object dropped on the bot): walk back onto it
    if (G.t >= m.validT) {
      m.validT = G.t + 0.5;
      if (!c.isValid(16, q, f)) requestPath(s, m.hasSubGoal ? m.subGoal : m.goal, m.goalRadius, 7.0f, true);
    }
    float cv[4 * 3]; unsigned char cf[4]; dtPolyRef cp[4];
    int nc = c.findCorners(cv, cf, cp, 4, q, f);
    if (nc > 0 && G.t >= m.optT) { m.optT = G.t + 0.3; c.optimizePathVisibility(&cv[std::min(nc - 1, 2) * 3], 30 * G.nav.cfg.radius, q, f); }
    if (G.t >= m.topoT) { m.topoT = G.t + 1.0; c.optimizePathTopology(q, f); }
    unsigned short pf = 0; G.nav.mesh()->getPolyFlags(c.getFirstPoly(), &pf); crouchArea = (pf & NF_CROUCH) != 0;
    if (nc > 0) {
      vec3 c0(cv[0], cv[1], cv[2]), last(cv[(nc - 1) * 3], cv[(nc - 1) * 3 + 1], cv[(nc - 1) * 3 + 2]);
      bool offmesh = (cf[nc - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;
      if (offmesh && nc == 1 && lenXZ(last - pos) < 0.6f) {
        dtPolyRef refs[2]; float sp[3], ep[3];
        if (c.moveOverOffmeshConnection(cp[nc - 1], refs, sp, ep, q)) { startTraversal(s, m, cp[nc - 1], vec3(sp[0], sp[1], sp[2]), vec3(ep[0], ep[1], ep[2])); return; }
      }
      vec3 tgt = offMesh ? cpos : c0;
      if (!offMesh && lenXZ(c0 - pos) < 0.15f && nc > 1 && !(cf[0] & DT_STRAIGHTPATH_OFFMESH_CONNECTION)) tgt = vec3(cv[3], cv[4], cv[5]);
      vec3 d = xz(tgt - pos); float L = glm::length(d);
      bool end = !offmesh && (cf[nc - 1] & DT_STRAIGHTPATH_END);
      float distEnd = lenXZ(last - pos);
      if (L > 0.02f) {
        float sp = vmax;
        if (end && nc == 1) sp *= std::clamp(distEnd / 1.2f, 0.25f, 1.0f);
        desired = d / L * sp;
      }
      wantsMove = !(end && nc == 1 && distEnd < std::max(0.25f, std::min(m.goalRadius, 0.8f)));
      if (!wantsMove && m.hasSubGoal && lenXZ(pos - m.subGoal) < 1.2f) { m.hasSubGoal = false; requestPath(s, m.goal, m.goalRadius, 6.0f, true); }
    }
    if (glm::length(desired) > 0.1f) m.moveDir = glm::normalize(desired);
  } else if (m.wantPath || m.pendingReq) wantsMove = G.t - m.lastReplan > 1.5;   // waiting long for a route counts as stuck
  // stuck recovery step 2: back off along the incoming direction
  if (G.t < m.backUntil) { vec3 d = xz(m.backTarget - pos); if (glm::length(d) > 0.2f) desired = glm::normalize(d) * vJog; wantsMove = true; }
  else if (m.replanAfterBack) { m.replanAfterBack = false; requestPath(s, m.hasSubGoal ? m.subGoal : m.goal, m.goalRadius, 8.0f, true); }
  // separation: teammates closer than 6 m push sideways while advancing (AI_SPEC 3.5, SPEC 5.2)
  float sepR = tj("movement", "separation_m", 6.0f);
  if (glm::length(desired) > 0.5f && (m.action == A_ADVANCE || m.action == A_TAKE_COVER) && !m.trav.on) {
    vec3 fwd = glm::normalize(desired), side(0);
    for (auto& o : G.sold) {
      if (&o == &s || !o.alive || o.role != s.role) continue;
      vec3 d = xz(pos - o.feet); float L = glm::length(d);
      if (L < 0.05f || L > sepR) continue;
      vec3 lat = d - fwd * glm::dot(d, fwd);                 // sideways only: never slows the advance
      if (glm::length(lat) < 0.05f) lat = vec3(-fwd.z, 0, fwd.x) * (((s.slot + o.slot) & 1) ? 1.0f : -1.0f);
      side += glm::normalize(lat) * (1.0f - L / sepR);
    }
    if (glm::length(side) > 0.01f) desired = glm::normalize(desired + side * tj("movement", "separationGain", 0.45f)) * glm::length(desired);
  }
  vec3 vel = desired;
  // velocity-obstacle avoidance (Detour RVO sampling) against soldiers, mesh walls and probed new geometry
  if (glm::length(desired) > 0.1f && oaq && q && m.hasPath) {
    dtPathCorridor& c = *m.corridor;
    if (glm::length(pos - m.boundaryPos) > 0.6f || G.t - m.boundaryT > 0.3) { m.boundary.update(c.getFirstPoly(), &pos.x, 2.5f, q, f); m.boundaryPos = pos; m.boundaryT = G.t; }
    if (G.t >= m.probeT) {   // geometry not in the mesh yet (rebuild pending, debris, felled trees)
      m.probeT = G.t + 0.2; m.probeSegs.clear();
      vec3 fd = glm::normalize(desired);
      for (float ang : {0.0f, 0.5f, -0.5f}) {
        vec3 dd = glm::angleAxis(ang, vec3(0, 1, 0)) * fd; Physics::Hit h;
        if (ray(pos + vec3(0, 0.55f, 0), dd, 1.4f, &h) && !isLeafUD(h.ud) && std::fabs(h.normal.y) < 0.6f) {
          vec3 tn = glm::normalize(vec3(-h.normal.z, 0, h.normal.x)) * 0.7f;
          m.probeSegs.push_back(vec4(h.pos.x - tn.x, h.pos.z - tn.z, h.pos.x + tn.x, h.pos.z + tn.z));
        }
      }
    }
    if (((int)G.steps + s.slot) % 2 == 0 || glm::length(m.avoidVel) < 0.01f) {
      oaq->reset();
      for (auto& o : G.sold) {
        if (&o == &s || !o.alive) continue;
        vec3 d = o.feet - pos; if (glm::length(d) > 5.0f || std::fabs(d.y) > 1.8f) continue;
        vec3 ov = o.vel; ov.y = 0; vec3 dv = ov;
        oaq->addCircle(&o.feet.x, G.nav.cfg.radius + tj("movement", "avoidPad_m", 0.3f), &ov.x, &dv.x);
      }
      for (int k = 0; k < m.boundary.getSegmentCount(); ++k) { const float* sg = m.boundary.getSegment(k); oaq->addSegment(sg, sg + 3); }
      for (auto& sg : m.probeSegs) { float a[3] = {sg.x, pos.y, sg.y}, bb[3] = {sg.z, pos.y, sg.w}; oaq->addSegment(a, bb); }
      vec3 cur = xz(s.vel), nv(0);
      oaq->sampleVelocityAdaptive(&pos.x, G.nav.cfg.radius, std::max(0.5f, glm::length(desired)), &cur.x, &desired.x, &nv.x, &oaParams);
      m.avoidVel = nv;
    }
    vel = m.avoidVel;
  }
  // stay on the mesh: the move is clamped along the surface (never off it, never into a forbidden polygon)
  if (glm::length(vel) > 0.05f && q && m.hasPath && !offMesh) {
    const float look = 0.35f;
    vec3 tgt = pos + vel * look, res; dtPolyRef vis[16]; int nvis = 0;
    if (dtStatusSucceed(q->moveAlongSurface(m.corridor->getFirstPoly(), &pos.x, &tgt.x, f, &res.x, vis, &nvis, 16))) {
      vec3 v2 = xz(res - pos) / look; float L = glm::length(v2), V = glm::length(vel);
      vel = L > V ? v2 / L * V : v2;
      if (glm::length(desired) > 0.5f && L < 0.25f * V) vel = vel * 0.5f + xz(desired) * 0.15f;   // pressed into a wall: keep a little push so the monitor sees it
    }
  }
  // doors on the way: open them through the shared interaction, then wait for the leaf
  if (glm::length(vel) > 0.3f && ((int)G.steps + s.slot) % 8 == 0) {
    vec3 o = pos + vec3(0, 1.0f, 0); Physics::Hit h[4];
    int n = Physics::castRayAll(o, glm::normalize(vel), 1.2f, h, 4, false); ++raysThisFrame;
    for (int k = 0; k < n; ++k) {
      if (!isLeafUD(h[k].ud)) break;
      uint64_t inst = h[k].ud >> 16; int idx = (int)(h[k].ud & 0xFFFF) - 1;
      auto it = G.app.rt.find(inst);
      if (it != G.app.rt.end() && idx >= 0 && idx < (int)it->second.dir.size() && it->second.asset->interactables[idx].kind != "window" && it->second.dir[idx] <= 0 && it->second.progress[idx] < 0.9f) {
        G.app.toggleInteract(inst, idx); ++G.doorOpens; m.doorWait = 1.1f; b.why = "opening a door";
      }
      break;
    }
  }
  // body facing: toward movement when moving fast, else toward the aim
  float bodyWant = glm::length(vel) > 1.0f ? yawOf(vel) : b.aimYaw;
  if (std::fabs(angDiff(b.aimYaw, bodyWant)) > 70.0f && m.speed != SPD_SPRINT) bodyWant = b.aimYaw;
  float turn = 400.0f * dt;
  s.yaw = wrapDeg(s.yaw + std::clamp(angDiff(bodyWant, s.yaw), -turn, turn));
  int posture = m.posture;
  if (crouchArea && posture == Physics::STAND) posture = Physics::CROUCH;
  if (posture == Physics::CROUCH && glm::length(vel) > vCrouch + 0.2f) vel = glm::normalize(vel) * vCrouch;
  if (posture == Physics::PRONE && glm::length(vel) > vProne + 0.1f) vel = glm::normalize(vel) * vProne;
  Physics::updateAgent(s.agent, dt, vel, false, posture, s.yaw);
  auto st = Physics::agent(s.agent);
  s.vel = (st.pos - s.feet) / std::max(dt, 1e-4f); s.feet = st.pos; s.posture = st.posture;
  s.speed = lenXZ(s.vel);
  m.lastVel = vel;
  if (s.feet.y < -60) { s.hp = 0; G.killSoldier(s.slot, -1, vec3(0, -1, 0)); return; }
  stuckLadder(s, m, dt, wantsMove && m.speed != SPD_STOP);
}

// ---------------------------------------------------------------- stuck ladder (AI_SPEC 3.6)
void AiCore::stuckLadder(Soldier& s, BotMind& m, float dt, bool wantsMove) {
  auto close = [&](bool ok) {
    StuckEvent& e = stuck[m.eventIdx];
    e.open = false; e.t1 = G.t; e.resolved = ok; e.step = ok ? m.ladder : 0;
    if (e.t1 - e.t0 > 5.0) ++metrics.stuckOver5;
    if (ok) ++metrics.stuckResolvedBy[std::clamp(m.ladder, 0, 6)]; else ++metrics.stuckUnrecovered;
    logStuck(e);
    m.eventIdx = -1; m.ladder = 0; m.progT = 0; m.progAnchor = s.feet;
  };
  if (m.eventIdx >= 0) {
    StuckEvent& e = stuck[m.eventIdx];
    if (lenXZ(s.feet - e.pos) > 1.0f || std::fabs(s.feet.y - e.pos.y) > 0.8f) { close(true); return; }
    if (!wantsMove && !(G.t < m.backUntil)) { e.why = "stopped moving (new decision)"; close(true); return; }
    static const float stepTime[7] = {0, 2.0f, 2.5f, 3.0f, 2.5f, 3.0f, 1.5f};
    if (G.t - m.ladderT < stepTime[std::clamp(m.ladder, 1, 6)]) return;
    if (m.ladder >= 6) { e.why = "every recovery step failed"; close(false); return; }
    ++m.ladder; m.ladderT = G.t;
  } else {
    if (!wantsMove) { m.progT = 0; m.progAnchor = s.feet; return; }
    m.progT += dt;
    if (m.progT < 1.5f) return;
    bool stuckNow = lenXZ(s.feet - m.progAnchor) < 0.5f && std::fabs(s.feet.y - m.progAnchor.y) < 0.5f;
    m.progT = 0; m.progAnchor = s.feet;
    if (!stuckNow) return;
    StuckEvent e; e.slot = s.slot; e.t0 = G.t - 1.5; e.pos = s.feet;
    stuck.push_back(e); m.eventIdx = (int)stuck.size() - 1; ++metrics.stuckEvents;
    m.ladder = 1; m.ladderT = G.t; m.stuckPos = s.feet;
  }
  vec3 goal = m.hasSubGoal ? m.subGoal : m.goal;
  StuckEvent& e = stuck[m.eventIdx];
  switch (m.ladder) {
    case 1: e.why = "re-plan"; requestPath(s, goal, m.goalRadius, 9.0f, true); s.br.why = "stuck: re-planning"; break;
    case 2: {   // step back 1-2 m along the incoming direction, then re-plan
      vec3 back = -m.moveDir; back.y = 0; if (glm::length(back) < 0.1f) back = vec3(std::sin(radians(s.yaw)), 0, std::cos(radians(s.yaw)));
      back = glm::normalize(back);
      vec3 tgt = s.feet + back * 1.6f, res = tgt;
      if (m.hasPath) { dtPolyRef vis[16]; int nv = 0; G.nav.query()->moveAlongSurface(m.corridor->getFirstPoly(), &s.feet.x, &tgt.x, &G.nav.defaultFilter(), &res.x, vis, &nv, 16); }
      m.backTarget = res; m.backUntil = G.t + 1.0; m.replanAfterBack = true;
      e.why = "step back"; s.br.why = "stuck: stepping back"; break; }
    case 3: {   // a nearby link (mantle / vault / jump / drop) toward the goal
      auto L = G.nav.linksNear(s.feet, 3.0f);
      float best = lenXZ(goal - s.feet) - 1.0f; vec3 pick(0); bool have = false;
      for (auto& l : L) for (int side = 0; side < (l.bidir ? 2 : 1); ++side) {
        vec3 from = side ? l.b : l.a, to = side ? l.a : l.b;
        if (glm::length(from - s.feet) > 3.0f) continue;
        float d = lenXZ(goal - to);
        if (d < best) { best = d; pick = to; have = true; }
      }
      e.why = have ? "nearby link" : "nearby link (none)";
      if (have) { m.subGoal = pick; m.hasSubGoal = true; requestPath(s, pick, 0.8f, 9.0f, true); s.br.why = "stuck: taking a nearby link"; }
      else { m.ladderT -= 2.0; }   // nothing to try: move on quickly
      break; }
    case 4: {   // this bot only: the blocking edge becomes very expensive for 20 s
      m.hasSubGoal = false;
      m.penalties.erase(std::remove_if(m.penalties.begin(), m.penalties.end(), [&](const EdgePenalty& p) { return p.until < G.t; }), m.penalties.end());
      vec3 ahead = s.feet + m.moveDir * 0.8f;
      m.penalties.push_back({ahead, 2.0f, G.t + tj("stuck", "penalty_s", 20.0f)});
      e.why = "penalise edge"; requestPath(s, m.goal, m.goalRadius, 9.0f, true); s.br.why = "stuck: avoiding this spot"; break; }
    case 5: {   // a different goal (the decision layer picks another cover / route)
      m.needNewGoal = true;
      vec3 side = glm::normalize(vec3(-m.moveDir.z, 0, m.moveDir.x) + vec3(1e-4f)) * (G.rnd(s) < 0.5f ? 7.0f : -7.0f);
      vec3 alt = m.goal + side; NavRef r; vec3 on;
      if (G.nav.closest(alt, 4.0f, r, on)) alt = on;
      m.hasSubGoal = true; m.subGoal = alt;
      e.why = "alternate goal"; requestPath(s, alt, 1.5f, 9.0f, true); s.br.why = "stuck: different route"; break; }
    case 6: {   // last resort: the nearest valid mesh point within 3 m, only when no human sees the bot
      e.why = "snap";
      if (humanSees(s)) { e.why = "snap refused (seen by a player)"; break; }
      vec3 target(0); bool have = false;
      if (m.hasPath) {
        float cv[4 * 3]; unsigned char cf[4]; dtPolyRef cp[4];
        int nc = m.corridor->findCorners(cv, cf, cp, 4, G.nav.query(), &G.nav.defaultFilter());
        for (int k = nc - 1; k >= 0 && !have; --k) { vec3 c(cv[k * 3], cv[k * 3 + 1], cv[k * 3 + 2]); vec3 d = c - s.feet; if (glm::length(d) <= 3.0f && glm::length(d) > 0.6f) { target = c; have = true; } }
        if (!have && nc > 0) { vec3 c(cv[0], cv[1], cv[2]); vec3 d = c - s.feet; if (glm::length(d) > 0.3f) { target = s.feet + glm::normalize(d) * std::min(3.0f, glm::length(d)); NavRef r; vec3 on; if (G.nav.closest(target, 1.0f, r, on)) { target = on; have = true; } } }
      }
      if (!have) { NavRef r; vec3 on; if (G.nav.closest(s.feet, 3.0f, r, on) && glm::length(on - s.feet) > 0.3f) { target = on; have = true; } }
      if (have && Physics::shapeFitsAt(Physics::STAND, target + vec3(0, 0.05f, 0), true)) {
        Physics::setAgentPos(s.agent, target + vec3(0, 0.03f, 0)); s.feet = target; ++metrics.snaps;
        m.progAnchor = target;
        requestPath(s, goal, m.goalRadius, 9.0f, true);
        // a snap counts as recovered once the bot moves on from the new spot
        e.pos = target;
        s.br.why = "stuck: repositioned";
      } else e.why = "snap (no valid point)";
      break; }
  }
}
