// Decision layers (AI_SPEC 6, 8, 9):
//   commander (1 Hz): objective, fireteams of 3-4 per lane, the 30 s regroup after a capture (rules.json)
//   fireteam (3 Hz):  bounding overwatch (one pair moves, the other covers; "set" swaps them), target assignment,
//                     the room / bunker clearing sequence (stack, grenade, entry one at a time, corners, next room)
//   individual (5-10 Hz, 2 Hz beyond 300 m of every human): utility scores 0..1 per action from threat, cover,
//                     suppression, ammo, orders; the best wins with a small bonus for the current one.
#include "ai_core.h"
#include "app.h"
#include <algorithm>
#include <cmath>

namespace { vec3 xz(vec3 v) { return vec3(v.x, 0, v.z); } float lenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); } }

// ---------------------------------------------------------------- commander
void AiCore::commander(TeamBrain& tb) {
  const Objective* ob = G.match.activeObjective();
  if (tb.stage != G.match.active) {
    if (tb.stage >= 0 && tb.role == (int)CombatRole::Attacker && G.match.active > tb.stage && tb.stage < (int)G.match.sc.objectives.size()) {
      tb.regroupUntil = G.t + rules.value("regroupSeconds", 30.0f);                 // consolidate on the captured point
      tb.regroupAt = G.match.sc.objectives[tb.stage].pos;
    }
    tb.stage = G.match.active;
    for (auto& s : G.sold) if (s.alive && !s.human && (int)s.role == tb.role) { BotMind& m = mind(s); releaseCover(s); m.slot = -1; m.flankReached = false; m.needNewGoal = true; m.hasSubGoal = false; }
  }
  (void)ob;
  // fireteams of 3-4 per lane (stable while the alive set does not change)
  std::map<int, std::vector<int>> byLane;
  for (auto& s : G.sold) if (s.alive && !s.human && (int)s.role == tb.role) byLane[tb.role == (int)CombatRole::Attacker ? s.br.lane : 0].push_back(s.slot);
  bool same = true; size_t total = 0;
  for (auto& ft : tb.fts) total += ft.members.size();
  size_t alive = 0; for (auto& [l, v] : byLane) alive += v.size();
  if (total != alive) same = false;
  for (auto& ft : tb.fts) for (int sl : ft.members) if (!G.sold[sl].alive || G.sold[sl].human) same = false;
  if (!same) {
    std::vector<Fireteam> old; old.swap(tb.fts);
    for (auto& [lane, v] : byLane) {
      int n = (int)v.size(), k = std::max(1, (n + 3) / 4);
      std::vector<Fireteam> fts(k);
      for (int i = 0; i < n; ++i) fts[i % k].members.push_back(v[i]);
      for (auto& ft : fts) {
        ft.role = tb.role; ft.lane = lane; ft.id = (int)tb.fts.size(); ft.pairSince = G.t;
        for (auto& o : old) if (!o.members.empty() && !ft.members.empty() && o.members[0] == ft.members[0]) { ft.movingPair = o.movingPair; ft.pairSince = o.pairSince; ft.clearBuilding = o.clearBuilding; ft.clearRoom = o.clearRoom; ft.clearStage = o.clearStage; ft.clearT = o.clearT; ft.roomsCleared = o.roomsCleared; }
        tb.fts.push_back(ft);
      }
    }
    for (auto& ft : tb.fts) for (size_t i = 0; i < ft.members.size(); ++i) { BotMind& m = mind(G.sold[ft.members[i]]); m.ft = ft.id; m.ftIdx = (int)i; }
  }
}

// ---------------------------------------------------------------- fireteam
void AiCore::fireteamStep(TeamBrain& tb, Fireteam& ft) {
  if (ft.members.empty()) return;
  vec3 cen(0); int n = 0; for (int sl : ft.members) if (G.sold[sl].alive) { cen += G.sold[sl].feet; ++n; }
  if (!n) return;
  cen /= (float)n;
  std::vector<const Known*> threats;
  for (auto& [e, k] : tb.known) if (k.conf > 0.3f && glm::length(k.pos - cen) < 260) threats.push_back(&k);
  bool regroup = G.t < tb.regroupUntil;
  bool attacker = tb.role == (int)CombatRole::Attacker;
  // bounding overwatch (AI_SPEC 6.2)
  bool use = diff.overwatch != 0 || ((int)(G.t / 20.0) + ft.id) % 2 == 0;   // recruits: sometimes
  ft.overwatch = attacker && !regroup && !threats.empty() && use && n >= 2 && ft.clearBuilding < 0;
  if (ft.overwatch) {
    bool allSet = true; int movers = 0;
    for (size_t i = 0; i < ft.members.size(); ++i) {
      if ((int)(i % 2) != ft.movingPair) continue;
      Soldier& s = G.sold[ft.members[i]]; if (!s.alive) continue;
      ++movers; BotMind& m = mind(s);
      if (!m.set) allSet = false;
    }
    float maxT = diff.overwatch == 2 ? 9.0f : 13.0f;
    if ((movers == 0 || allSet || G.t - ft.pairSince > maxT) && G.t - ft.pairSince > 1.5) { ft.movingPair ^= 1; ft.pairSince = G.t; }   // "set": the other pair moves
    for (size_t i = 0; i < ft.members.size(); ++i) { BotMind& m = mind(G.sold[ft.members[i]]); m.bounding = (int)(i % 2) == ft.movingPair; }
  } else for (int sl : ft.members) mind(G.sold[sl]).bounding = true;
  // target assignment: spread fire over the visible enemies
  ft.targetFor.clear();
  std::vector<int> vis; for (auto* k : threats) if (G.t - k->seen < 1.0 && !k->heard) vis.push_back(k->enemy);
  if (!vis.empty()) for (size_t i = 0; i < ft.members.size(); ++i) ft.targetFor[ft.members[i]] = vis[i % vis.size()];
  // room / bunker clearing (AI_SPEC 8.2)
  if (!attacker) return;
  const Objective* ob = G.match.activeObjective();
  if (ft.clearBuilding < 0) {
    int bestB = -1, bestR = -1; float bd = 1e9f;
    for (size_t bi = 0; bi < buildings.size(); ++bi) {
      const AiBuilding& b = buildings[bi];
      if (b.entrances.empty()) continue;
      vec3 bc = (b.bmin + b.bmax) * 0.5f; float d = lenXZ(bc - cen);
      if (d > 70) continue;
      for (size_t ri = 0; ri < b.rooms.size(); ++ri) {
        std::string key = std::to_string(b.inst) + ":" + b.rooms[ri].id;
        if (ft.roomsCleared.count(key)) continue;
        bool occupied = false; for (auto* k : threats) if (b.roomAt(k->pos) == (int)ri && G.t - k->seen < 20) occupied = true;
        bool onObjective = (ob && (ob->contains(bc) || glm::length(vec2(bc.x - ob->pos.x, bc.z - ob->pos.z)) < ob->extent() + 4)) || (regroup && lenXZ(bc - tb.regroupAt) < 40);   // consolidation clears the captured strongpoint
        bool entrance = false; for (auto& e : b.entrances) if (e.room == b.rooms[ri].id) entrance = true;
        if (!entrance || !(occupied || (onObjective && d < 45))) continue;
        float sc = d - (occupied ? 30.0f : 0.0f);
        if (sc < bd) { bd = sc; bestB = (int)bi; bestR = (int)ri; }
      }
    }
    if (bestB >= 0) { ft.clearBuilding = bestB; ft.clearRoom = bestR; ft.clearStage = 1; ft.clearT = G.t; ft.clearEntered = 0; }
  }
  if (ft.clearBuilding < 0) return;
  if (ft.clearBuilding >= (int)buildings.size() || G.t - ft.clearT > 75) { ft.clearBuilding = -1; return; }
  const AiBuilding& b = buildings[ft.clearBuilding];
  const AiBuilding::Room& room = b.rooms[ft.clearRoom];
  std::string key = std::to_string(b.inst) + ":" + room.id;
  bool occupied = false; for (auto* k : threats) if (b.roomAt(k->pos) == ft.clearRoom && G.t - k->seen < 20) occupied = true;
  int ready = 0;
  for (size_t i = 0; i < ft.members.size(); ++i) { Soldier& s = G.sold[ft.members[i]]; if (s.alive && mind(s).set) ++ready; }
  switch (ft.clearStage) {
    case 1: if (ready >= std::min(2, n) || G.t - ft.clearT > 22) { ft.clearStage = occupied ? 2 : 3; ft.clearT = G.t; } break;          // stacked
    case 2: if (G.t - ft.clearT > 3.2) { ft.clearStage = 3; ft.clearT = G.t; ft.clearEntered = 0; } break;                                 // grenade, blast
    case 3: if (G.t - ft.clearT > 1.3 * (ft.clearEntered + 1) && ft.clearEntered < n) ++ft.clearEntered;                                // one at a time
            if (ft.clearEntered >= n && (ready >= n || G.t - ft.clearT > 14)) { ft.clearStage = 4; ft.clearT = G.t; } break;
    case 4: {   // room clear: next room along the room graph, else done
      ft.roomsCleared.insert(key); clearedRooms.insert(key);
      int next = -1;
      for (auto& d : b.doors) {
        std::string other = d.a == room.id ? d.b : d.b == room.id ? d.a : "";
        if (other.empty() || other == "outside") continue;
        for (size_t ri = 0; ri < b.rooms.size(); ++ri) if (b.rooms[ri].id == other && !ft.roomsCleared.count(std::to_string(b.inst) + ":" + other)) next = (int)ri;
      }
      if (next >= 0) { ft.clearRoom = next; ft.clearStage = 3; ft.clearT = G.t; ft.clearEntered = 0; }
      else ft.clearBuilding = -1;
      break; }
  }
}

// ---------------------------------------------------------------- goals
vec3 AiCore::objectiveGoal(Soldier& s) {
  BotMind& m = mind(s);
  const Objective* ob = G.match.activeObjective();
  TeamBrain& tb = teams[(int)s.role];
  if (!ob) return s.feet;
  if (s.role == CombatRole::Attacker) {
    if (G.t < tb.regroupUntil) { float a = s.slot * 2.39996f; return tb.regroupAt + vec3(std::cos(a), 0, std::sin(a)) * (6.0f + (s.slot % 4) * 3.0f); }
    if (m.flanker && !m.flankReached) {
      // a position 70-110 degrees off the main attack axis (SPEC 5.3), on the lane's side
      float side = s.br.lane == LANE_LEFT ? -1.0f : 1.0f;
      float R = std::max(ob->extent() + tj("commander", "flankStandoff_m", 28.0f), 42.0f);
      float ang = radians(80.0f + 20.0f * ((s.slot * 37 % 10) / 10.0f));
      vec3 fw = attackForward(ob->attackYaw), rt = attackRight(ob->attackYaw);
      vec3 p = ob->pos + (rt * side * std::sin(ang) - fw * std::cos(ang)) * R;
      NavRef r; vec3 on; if (G.nav.closest(p, 8.0f, r, on)) p = on;
      m.flankPos = p;
      if (lenXZ(s.feet - p) < 10.0f) m.flankReached = true;
      return p;
    }
    float a = s.slot * 2.39996f, rr = ob->extent() * (0.2f + 0.4f * ((s.slot * 37) % 10) / 10.0f);
    return ob->pos + vec3(std::cos(a) * rr, 0, std::sin(a) * rr);
  }
  if (m.slot < 0 || m.slot >= (int)defSlots.size() || defSlots[m.slot].heldBy != s.slot) pickDefSlot(s);
  return m.slot >= 0 && m.slot < (int)defSlots.size() ? defSlots[m.slot].p : ob->pos;
}

// ---------------------------------------------------------------- individual: utility (AI_SPEC 9)
void AiCore::think(Soldier& s) {
  BotMind& m = mind(s);
  Brain& b = s.br;
  TeamBrain& tb = teams[(int)s.role];
  const Objective* ob = G.match.activeObjective();
  const WeaponDef& wd = *G.app.weaponDefs[s.weapon];
  Fireteam* ft = m.ft >= 0 && m.ft < (int)tb.fts.size() ? &tb.fts[m.ft] : nullptr;
  bool attacker = s.role == CombatRole::Attacker;
  bool visible = b.target >= 0;
  bool underFire = G.t - b.hurtAt < 2.0 || b.suppressed > 0.35f || G.t - m.nearMissAt < 1.5;
  bool inCover = m.cover >= 0 && m.cover < (int)cover.size() && cover[m.cover].serial == m.coverSerial && lenXZ(s.feet - m.coverPos) < 1.3f;
  float ammo = wd.cap > 0 ? s.ws.loaded / (float)wd.cap : 1.0f;
  bool canReload = !s.ws.containers.empty();
  bool regroup = attacker && G.t < tb.regroupUntil;
  bool insideObj = ob && ob->contains(s.feet);
  bool autoW = wd.action == "auto";
  bool recent = false; for (auto& [e, k] : tb.known) if (!k.heard && G.t - k.seen < 6 && (k.by == s.slot || (ft && std::find(ft->members.begin(), ft->members.end(), k.by) != ft->members.end()) || G.t - k.seen >= diff.shareDelay)) recent = true;
  float sup = b.suppressed * (1.0f - 0.6f * diff.suppressionResist);
  bool clearing = ft && ft->clearBuilding >= 0;
  auto& u = m.util; u.fill(0);
  u[A_EVADE] = G.t < b.evadeUntil ? 1.0f : 0.0f;
  if (s.reload > 0) u[A_RELOAD] = 0.97f;
  else if (canReload && (ammo <= 0 || ammo < 0.4f)) u[A_RELOAD] = (ammo <= 0 ? 0.88f : 0.5f) * (inCover || !underFire ? 1.0f : 0.35f) * (visible && ammo > 0 ? 0.6f : 1.0f);
  if (underFire && !inCover) u[A_TAKE_COVER] = std::min(0.97f, 0.72f + 0.3f * sup + (G.t - b.hurtAt < 1.0 ? 0.1f : 0.0f));
  if (visible) u[A_RETURN_FIRE] = (inCover ? 0.8f : 0.56f) * (m.flanker && !m.flankReached && !underFire ? 0.3f : 1.0f) * (clearing ? 1.1f : 1.0f);
  if (!visible && recent && ammo > 0.25f && (autoW || wd.feed == "belt") && ((ft && ft->overwatch && !m.bounding) || !attacker)) u[A_SUPPRESS] = 0.6f;
  if (attacker && !regroup && !insideObj) {
    float a = m.bounding ? 0.62f : 0.22f;
    if (underFire && !inCover) a *= 0.45f;
    if (m.set && G.t < m.peekUntil) a *= 0.55f;                              // peek and fire first
    if (sup > 0.75f) a *= 0.4f;                                              // pinned
    u[A_ADVANCE] = a;
  }
  if (attacker && ft && ft->overwatch && !m.bounding && !regroup) u[A_OVERWATCH] = 0.6f;
  if (!attacker) u[A_HOLD] = 0.66f;
  else if (regroup || insideObj) u[A_HOLD] = 0.8f;
  if (clearing && ft->clearStage >= 1) u[A_CLEAR] = 0.92f;   // above holding the point (consolidation includes clearing it)
  // grenades: an enemy behind cover / in a room at 8-35 m (checked about once a second)
  if (s.frags > 0 && b.grenadeCD <= 0 && s.throwT < 0 && ((int)(G.t * 2) + s.slot) % 2 == 0) {
    vec3 vel; vec3 tgt(0); bool ok = false;
    for (auto& [e, k] : tb.known) {
      if (k.heard || G.t - k.seen > 5) continue;
      float d = lenXZ(k.pos - s.feet); if (d < 8 || d > 35) continue;
      bool hidden = true; for (auto& mm : b.mem) if (mm.enemy == e && mm.visible) hidden = false;
      bool inRoom = false; for (auto& bd : buildings) if (bd.roomAt(k.pos) >= 0) inRoom = true;
      uint8_t ar = G.nav.areaAt(k.pos, 1.0f); bool trench = navarea::toNav(ar) == NavArea::Trench;
      if (!hidden && !inRoom && !trench) continue;
      if (planGrenade(s, k.pos, vel)) { ok = true; tgt = k.pos; break; }
    }
    if (ok) { u[A_GRENADE] = (inCover || !underFire) ? 0.86f : 0.6f; s.throwTarget = tgt; s.throwVel = vel; }
  }
  // weights and inertia
  if (tactics.contains("utility")) for (int a = 0; a < A_COUNT; ++a) u[a] *= tactics["utility"].value(aiActionName(a), 1.0f);
  u[m.action] += tj("decisions", "inertia", 0.08f);
  int best = 0; for (int a = 1; a < A_COUNT; ++a) if (u[a] > u[best]) best = a;
  if (u[best] <= 0.01f) best = attacker ? A_ADVANCE : A_HOLD;
  if (best != m.action) { m.action = best; m.actionSince = G.t; }
}

// ---------------------------------------------------------------- individual: actions
void AiCore::act(Soldier& s, float dt) {
  (void)dt;
  BotMind& m = mind(s);
  Brain& b = s.br;
  TeamBrain& tb = teams[(int)s.role];
  const Objective* ob = G.match.activeObjective();
  const WeaponDef& wd = *G.app.weaponDefs[s.weapon];
  Fireteam* ft = m.ft >= 0 && m.ft < (int)tb.fts.size() ? &tb.fts[m.ft] : nullptr;
  bool underFire = G.t - b.hurtAt < 2.0 || b.suppressed > 0.35f || G.t - m.nearMissAt < 1.5;
  bool attacker = s.role == CombatRole::Attacker;
  vec3 fireFrom = G.t - b.hurtAt < 3 ? b.hurtFrom : G.t - m.nearMissAt < 3 ? m.nearMissFrom : b.target >= 0 ? G.sold[b.target].feet : vec3(0);
  bool coverValid = m.cover >= 0 && m.cover < (int)cover.size() && cover[m.cover].serial == m.coverSerial;
  bool atCover = coverValid && lenXZ(s.feet - m.coverPos) < 1.0f;
  if (atCover && !m.set) {
    m.set = true; m.setAt = G.t; m.peekUntil = G.t + 1.5 + G.rnd(s) * 2.5;
    if (m.boundStart > 0) { ++metrics.bounds; metrics.boundExposure += m.exposedThisBound; m.boundStart = -100; }
  }
  if (!atCover && m.set && lenXZ(s.feet - m.coverPos) > 2.0f) m.set = false;
  auto lowPosture = [&] { return atCover && !cover[m.cover].high ? Physics::CROUCH : Physics::STAND; };
  auto speedFor = [&](vec3 goal) {
    // sprint across ground exposed to known threats and whenever under fire; jog otherwise (AI_SPEC 6.1)
    float e = exposure((int)s.role, s.feet + vec3(0, 1.2f, 0));
    vec3 ahead = s.feet + glm::normalize(xz(goal - s.feet) + vec3(1e-4f)) * std::min(12.0f, lenXZ(goal - s.feet));
    e = std::max(e, exposure((int)s.role, ahead + vec3(0, 1.2f, 0)));
    return underFire || e > 0.25f ? SPD_SPRINT : SPD_JOG;
  };
  auto boundTo = [&](vec3 goal, float maxLen) {
    // the next cover toward goal, at most maxLen away; none: straight toward the goal
    if (!coverValid || (m.set && G.t >= m.peekUntil) || m.needNewGoal || (!m.set && G.t - m.boundStart > 25)) {
      int c = pickCover(s, goal, maxLen, 0.12f, false, vec3(0));
      if (c >= 0) { requestPath(s, cover[c].p, 0.6f, 3.0f); m.boundStart = G.t; m.boundFrom = s.feet; m.exposedThisBound = 0; coverValid = true; }
      else { releaseCover(s); vec3 g = s.feet + glm::normalize(xz(goal - s.feet) + vec3(1e-4f)) * std::min(maxLen, lenXZ(goal - s.feet)); NavRef r; vec3 on; if (G.nav.closest(g, 6.0f, r, on)) g = on; requestPath(s, lenXZ(goal - s.feet) < maxLen ? goal : g, 1.5f, 2.0f); m.boundStart = G.t; m.exposedThisBound = 0; }
    } else if (coverValid && !m.set) requestPath(s, m.coverPos, 0.6f, 3.0f);
  };
  m.posture = Physics::STAND; b.crouch = false;
  switch (m.action) {
    case A_EVADE: {
      vec3 best = s.feet; float bs = -1e9f;
      for (int k = 0; k < 10; ++k) {
        float a = k * 0.6283f; vec3 q = s.feet + vec3(std::cos(a), 0, std::sin(a)) * 9.0f;
        NavRef r; vec3 on; if (!G.nav.closest(q, 2.5f, r, on)) continue;
        float sc = glm::length(on - b.evadeFrom);
        if (!ray(b.evadeFrom + vec3(0, 0.5f, 0), on + vec3(0, 0.6f, 0) - b.evadeFrom - vec3(0, 0.5f, 0), glm::length(on - b.evadeFrom))) sc -= 4; else sc += 3;
        if (s.role == CombatRole::Defender && ob && ob->contains(on)) sc += 3;
        if (sc > bs) { bs = sc; best = on; }
      }
      requestPath(s, best, 0.8f, 9.0f); m.speed = SPD_SPRINT; b.mode = Brain::M_EVADE; b.why = "evading a grenade"; break; }
    case A_RELOAD:
      if (s.reload <= 0 && !s.ws.containers.empty() && s.ws.loaded < wd.cap) {
        s.reloadTotal = s.ws.loaded == 0 ? wd.reloadEmpty : wd.reloadPartial;
        if (wd.feed == "single" || (wd.feed == "stripper" && s.ws.loaded > 0)) s.reloadTotal = wd.reloadRound * (wd.cap - s.ws.loaded);
        s.reload = s.reloadTotal;
        G.app.audio.play(wd.feed == "magazine" ? "mag_out" : "bolt", s.feet + vec3(0, 1.2f, 0), 0.5f);
      }
      m.speed = atCover || !m.hasPath ? SPD_STOP : SPD_CROUCH; m.posture = Physics::CROUCH; b.crouch = true;
      b.mode = Brain::M_RELOAD; b.why = atCover ? "reloading in cover" : "reloading"; break;
    case A_TAKE_COVER: {
      if (!coverValid || !coverProtects(cover[m.cover], fireFrom) || m.needNewGoal) {
        int c = pickCover(s, attacker && ob ? ob->pos : s.feet, tj("cover", "reactRadius_m", 12.0f), -1.0f, true, fireFrom);
        if (c < 0) c = pickCover(s, s.feet, 20.0f, -1.0f, false, fireFrom);
        coverValid = c >= 0;
        if (c >= 0) { requestPath(s, cover[c].p, 0.6f, 8.0f, true); m.boundStart = G.t; m.exposedThisBound = 0; }
      }
      if (coverValid) { if (!atCover) requestPath(s, m.coverPos, 0.6f, 8.0f); m.speed = SPD_SPRINT; m.posture = atCover ? lowPosture() : Physics::STAND; b.why = "taking cover (under fire)"; }
      else { m.speed = SPD_CRAWL; m.posture = Physics::PRONE; b.why = "no cover: prone"; }
      b.mode = Brain::M_COVER; b.hasCover = coverValid; break; }
    case A_RETURN_FIRE: case A_SUPPRESS: {
      bool close = b.target >= 0 && glm::length(G.sold[b.target].feet - s.feet) < 20.0f;
      bool mobile = (wd.action == "auto" && wd.feed != "belt") && close;   // SMGs / BAR fire on the move at close range
      if (!mobile) m.speed = SPD_STOP;
      else m.speed = SPD_JOG;
      m.posture = atCover ? (cover[m.cover].high ? Physics::STAND : Physics::STAND) : (wd.feed == "belt" ? Physics::CROUCH : Physics::STAND);
      if (!atCover && s.posture == Physics::PRONE) m.posture = Physics::PRONE;
      b.mode = Brain::M_ENGAGE; b.why = m.action == A_SUPPRESS ? "suppressing" : atCover ? "returning fire from cover" : "returning fire"; break; }
    case A_GRENADE:
      if (s.throwT < 0 && s.frags > 0 && G.startThrow(s, GK_FRAG, s.throwVel, s.throwTarget)) {
        b.grenadeCD = diff.grenadeCooldown * (0.8f + 0.4f * G.rnd(s)); ++metrics.grenadesThrown;
      }
      m.speed = SPD_STOP; m.posture = Physics::STAND; b.mode = Brain::M_THROW; b.why = "frag at a hidden enemy"; break;
    case A_OVERWATCH:
      if (!coverValid) { int c = pickCover(s, s.feet, 10.0f, -0.5f, false, vec3(0)); if (c >= 0) requestPath(s, cover[c].p, 0.6f, 4.0f); }
      else if (!atCover) requestPath(s, m.coverPos, 0.6f, 4.0f);
      m.speed = atCover ? SPD_STOP : SPD_SPRINT; m.posture = atCover ? lowPosture() : Physics::STAND; b.crouch = m.posture == Physics::CROUCH;
      b.mode = Brain::M_COVER; b.why = "overwatch: covering the moving pair"; break;
    case A_ADVANCE: {
      vec3 goal = objectiveGoal(s);
      float maxLen = (b.suppressed > 0.2f || G.t - b.hurtAt < 5 || !tb.known.empty()) ? diff.boundMaxUnderFire : diff.boundMax;
      if (diff.overwatch == 2) maxLen *= 0.85f;
      if (tb.known.empty() && !m.flanker) maxLen = std::max(maxLen, 25.0f);
      boundTo(goal, maxLen);
      m.speed = speedFor(goal);
      m.posture = Physics::STAND;
      b.mode = Brain::M_ADVANCE;
      b.why = m.flanker && !m.flankReached ? (b.lane == LANE_LEFT ? "flanking (left): cover to cover" : "flanking (right): cover to cover") : m.set ? "set: peeking" : "bounding to cover";
      if (m.set && atCover) { m.speed = SPD_STOP; m.posture = lowPosture(); }
      break; }
    case A_HOLD: {
      vec3 goal;
      if (attacker) {
        goal = objectiveGoal(s);
        if (!coverValid || m.needNewGoal) { int c = pickCover(s, goal, 14.0f, -0.6f, false, vec3(0)); if (c >= 0 && (!ob || ob->contains(cover[c].p) || G.t < tb.regroupUntil)) goal = cover[c].p; else releaseCover(s); }
        else goal = m.coverPos;
        b.why = G.t < tb.regroupUntil ? "regrouping on the captured point" : "holding inside the objective";
      } else {
        goal = objectiveGoal(s);
        if (m.slot >= 0 && G.t - b.hurtAt < 1.5) {   // hit from an uncovered side: shift slots
          vec3 hd = xz(b.hurtFrom - s.feet);
          if (glm::length(hd) > 1 && !ray(s.feet + vec3(0, 0.8f, 0), glm::normalize(hd), 1.6f)) { defSlots[m.slot].suppressedAt = G.t; m.needNewGoal = true; pickDefSlot(s); goal = objectiveGoal(s); b.why = "repositioning: flanked"; }
        }
        if (m.slot >= 0 && b.suppressed > 0.8f) { defSlots[m.slot].suppressedAt = G.t; }
        if (b.why.rfind("repositioning", 0) != 0) b.why = "holding a defense slot";
      }
      bool there = lenXZ(s.feet - goal) < 1.2f;
      if (!there) { requestPath(s, goal, 0.8f, attacker ? 3.0f : 4.0f); m.speed = underFire ? SPD_SPRINT : SPD_JOG; m.posture = Physics::STAND; b.mode = Brain::M_RETURN; }
      else {
        m.speed = SPD_STOP; b.mode = Brain::M_HOLD;
        int kind = !attacker && m.slot >= 0 && m.slot < (int)defSlots.size() ? defSlots[m.slot].kind : 0;
        m.posture = kind == (int)SlotKind::Window || kind == (int)SlotKind::Embrasure ? Physics::STAND : Physics::CROUCH;
        if (b.target >= 0) m.posture = Physics::STAND;
        b.crouch = m.posture == Physics::CROUCH;
      }
      b.holdPos = goal; break; }
    case A_CLEAR: {
      if (!ft || ft->clearBuilding < 0) { m.action = A_ADVANCE; break; }
      const AiBuilding& bd = buildings[ft->clearBuilding];
      const AiBuilding::Room& room = bd.rooms[ft->clearRoom];
      const AiBuilding::Entrance* ent = nullptr; float ed = 1e9f;
      for (auto& e : bd.entrances) if (e.room == room.id || ent == nullptr) { float d = glm::length(e.p - s.feet); if (e.room == room.id && d < ed) { ed = d; ent = &e; } }
      if (!ent && !bd.entrances.empty()) ent = &bd.entrances[0];
      vec3 dir = ent ? glm::normalize(xz(ent->dir) + vec3(1e-4f)) : vec3(0, 0, 1), side(-dir.z, 0, dir.x);
      int idx = m.ftIdx;
      if (ft->clearStage <= 2 || idx >= ft->clearEntered) {
        // stack on the wall beside the entrance, not in front of the doorway
        float sd = (idx % 2 ? 1.0f : -1.0f) * (1.1f + 0.7f * (idx / 2));
        vec3 st = ent ? ent->p - dir * 0.9f + side * sd : s.feet;
        NavRef r; vec3 on; if (G.nav.closest(st, 1.2f, r, on)) st = on;
        requestPath(s, st, 0.5f, 5.0f);
        if (glm::length(st - m.clearGoal) > 0.5f) { m.clearGoal = st; m.clearGoalT = G.t; }
        bool late = G.t - m.clearGoalT > 9.0;   // a stack point it cannot reach: stack where it stands
        if (lenXZ(s.feet - st) < 0.8f || late) { m.set = true; if (m.setAt < ft->clearT) m.setAt = G.t; } else m.set = false;
        m.speed = lenXZ(s.feet - st) > 6 ? SPD_JOG : SPD_WALK; m.posture = Physics::CROUCH;
        b.why = "stacking at the entrance";
        if (ft->clearStage == 2 && idx == 0 && s.frags > 0 && s.throwT < 0 && b.grenadeCD <= 0 && ent) {
          vec3 tgt = (room.mn + room.mx) * 0.5f; tgt.y = room.mn.y + 0.2f; vec3 vel;
          if (planGrenade(s, tgt, vel) && G.startThrow(s, GK_FRAG, vel, tgt)) { b.grenadeCD = diff.grenadeCooldown; ++metrics.grenadesThrown; b.why = "grenade into the room"; }
        }
      } else {
        // entered: each bot takes a different corner / sector in turn (fast walk, weapon up)
        std::vector<vec3> cs; for (auto& c : bd.corners) if (c.room == room.id) cs.push_back(c.p);
        if (cs.empty()) cs.push_back((room.mn + room.mx) * 0.5f);
        vec3 tgt = cs[(idx + m.clearCorner) % cs.size()];
        if (glm::length(tgt - m.clearGoal) > 0.5f) { m.clearGoal = tgt; m.clearGoalT = G.t; }
        if (lenXZ(s.feet - tgt) < 0.9f) { m.set = true; if (G.t - m.setAt > 1.5) { ++m.clearCorner; m.setAt = G.t; } }
        else if (G.t - m.clearGoalT > 7.0) { ++m.clearCorner; m.set = true; }   // unreachable corner: the next one
        else m.set = false;
        requestPath(s, tgt, 0.6f, 5.0f);
        m.speed = SPD_WALK; m.posture = Physics::STAND;
        b.why = bd.bunker ? "clearing the bunker" : "clearing the room";
      }
      b.mode = Brain::M_INVESTIGATE; break; }
  }
  // route exposure during the current bound (AI_SPEC 12.3 metric)
  if (m.boundStart > 0 && !m.set && attacker && exposure((int)s.role, s.feet + vec3(0, 1.2f, 0)) > 0.2f) m.exposedThisBound += dt;
}
