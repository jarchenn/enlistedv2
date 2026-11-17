// Bot AI entry points on Game (W6, AI_SPEC 3-11). The AI itself lives in the AiCore (ai_core.h):
//   ai_core.cpp    lifecycle, per-step services, team knowledge, threat viewsheds, live-rebuild polling, metrics
//   ai_path.cpp    path worker + per-bot cost filter, corridor following, avoidance, link moves, stuck ladder
//   ai_cover.cpp   cover generation / scoring, defense slots, building annotations
//   ai_layers.cpp  commander, fireteams, individual utility decisions and actions
//   ai_combat.cpp  perception, aim model and firing, grenades
//   ai_debug.cpp   overlays, follow camera, freeze / step, debug window, difficulty selection
// This replaces the AI/LAN milestone state machine (see docs/ai-inventory.md for the decision).
#include "sd_game.h"
#include "game.h"
#include "app.h"
#include "ai_core.h"
#include "crumb.h"
#include <algorithm>

namespace {
bool isLeaf(uint64_t u) { if (u == 0 || (u >> 60) == 0xF) return false; uint64_t l = u & 0xFFFF; return l != 0 && l != 0xFFFF; }
}

// line of sight for perception: glazed windows are see-through; doors, shutters and walls block; smoke opacity is
// integrated along the line
bool Game::lineOfSight(vec3 a, vec3 b, float* smokeT) const {
  vec3 d = b - a; float L = glm::length(d);
  if (L < 1e-3f) { if (smokeT) *smokeT = 1; return true; }
  Physics::Hit h[8];
  int n = Physics::castRayAll(a, d, L, h, 8, false);
  for (int k = 0; k < n; ++k) {
    if (app.tanks.occupied && h[k].ud == (app.tanks.occupied << 16)) continue;   // TK1: the occupied vehicle's own hull (its crew is the target inside)
    if (isLeaf(h[k].ud)) {
      uint64_t inst = h[k].ud >> 16; int idx = (int)(h[k].ud & 0xFFFF) - 1;
      auto it = app.rt.find(inst);
      if (it != app.rt.end() && idx >= 0 && idx < (int)it->second.asset->interactables.size() && it->second.asset->interactables[idx].kind == "window") continue;
    }
    return false;
  }
  if (smokeT) *smokeT = gren.transmittance(a, b, t);
  return true;
}

void Game::aiStep(Soldier& s, float dt) {
  AiCore* A = aiCore();
  if (!A || s.agent < 0) return;
  if (A->lastFrameStep != steps) {
    A->lastFrameStep = steps;
    A->stepRun = A->stepFrames > 0; if (A->stepRun) --A->stepFrames;
    A->frame();
  }
  BotMind& m = A->mind(s);
  Brain& b = s.br;
  b.modeT += dt; b.grenadeCD -= dt;
  if ((A->frozen && !A->stepRun) || sdFrozen() || (sdActive() && sdBotHolding(*this, s))) {   // debug freeze / SD1 freeze time, planting, defusing: bots stand still
    // SD7: in the SD freeze (buy time) a bot on the ground is pinned: no agent update at all (with a zero velocity the
    // controller still slid it on slopes and pushed it out of its neighbours at the spawn); only one still in the air settles
    const bool pinned = sdFrozen() && !s.human && Physics::agent(s.agent).grounded;
    if (!pinned) Physics::updateAgent(s.agent, dt, vec3(0), false, s.posture, s.yaw);
    auto st = Physics::agent(s.agent); s.feet = st.pos; s.vel = vec3(0); s.speed = 0;
    if (sdFrozen() && !s.human) { b.aimYaw = s.yaw; b.aimPitch = 0; b.target = -1; b.yawVel = 0; b.faceVel = vec2(0); }   // no turn left over into the live round   // SD7: the gun level along the facing (out of the spawn)
    return;
  }
  if (s.zombie) { AiProf::Scope sc(A->prof, PROF_MOVEMENT); crumb("ai: zombie"); aiZombieStep(*this, s, dt); return; }   // G2: no perception, decisions, cover, aim or grenades
  float hd = A->nearestHumanDist(s.feet);
  bool lod = hd < 1e8f && hd > A->tj("budget", "lodDistance_m", 300.0f);
  // G1 crowds (up to 100 AI): beyond 60 m from every human, perception / decisions slow down with the bot count;
  // AI2: up to 1000 AI the slow-down keeps growing past 200 (x2.5 at 100-200, x6 at 1000: far bots think ~every 0.6-1.2 s)
  float crowd = hd > 60.0f ? std::clamp((float)sold.size() / 40.0f, 1.0f, 2.5f) * std::max(1.0f, (float)sold.size() / 400.0f) : 1.0f;
  {
    AiProf::Scope sc(A->prof, PROF_PERCEPTION);
    crumb("ai: perceive");
    if (t >= b.nextPerceive && A->rayFree()) { A->perceive(s); b.nextPerceive = (float)t + (lod ? 0.5f : 0.2f) * crowd; }
  }
  {
    AiProf::Scope sc(A->prof, PROF_DECISIONS);
    crumb("ai: think");
    if (t >= m.nextThink) {
      A->think(s);
      A->act(s, (float)std::min(1.0, t - m.lastAct));
      m.lastAct = t;
      m.nextThink = t + (lod ? 0.5 : 0.1 + rnd(s) * 0.1) * crowd;
    }
  }
  { AiProf::Scope sc(A->prof, PROF_AIMING); crumb("ai: aim"); A->aim(s, dt); }
  { AiProf::Scope sc(A->prof, PROF_MOVEMENT); crumb("ai: move"); A->move(s, dt); }
}

void Game::assignLanes() {
  std::vector<int> atk; for (auto& s : sold) if (s.role == CombatRole::Attacker) atk.push_back(s.slot);
  auto l = allocateLanes((int)atk.size(), match.rules.laneLeft, match.rules.laneMain, match.rules.laneRight);
  for (size_t k = 0; k < atk.size(); ++k) sold[atk[k]].br.lane = (int)k < l[0] ? LANE_LEFT : (int)k < l[0] + l[1] ? LANE_MAIN : LANE_RIGHT;
}
