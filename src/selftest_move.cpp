// --selftest-move: run / sprint / crouch / prone speeds and distances, diagonal normalisation, frame-rate
// independence, player-only gravity and jump, the posture state machine (Ctrl hold, Z toggle, Ctrl+Z,
// blocked stand / prone exit, prone turning clearance), camera heights, no sprint while lowered, jump
// reliability (press on a frame with no fixed step, buffered press before landing, coyote time off an edge, no
// double jump, jump right after releasing crouch, every press jumps on a slope), mantle (mantle.cpp: out of the
// straight / corner / zig-zag trenches from the floor and the firestep, onto a 2 m platform, over a 2.1 m wall,
// refused at 2.4 m; weapon lowered, hands on the ledge, camera arc; movement.json limits vs the bots' links).
#include "selftest.h"
#include "editor_nav.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> mseq; size_t mcur = 0; float mt = 0;
uint32_t ceilBody = 0xffffffffu, lowBody = 0xffffffffu, wallBody = 0xffffffffu;
vec3 P0(0, 0, 0);
void put(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z), p.z)); }
float flat(vec3 a, vec3 b) { return glm::length(vec2(a.x - b.x, a.z - b.z)); }
uint32_t slab(vec3 c, vec3 size) { Part p; p.center = vec3(0); p.size = size; return Physics::addStatic(Physics::buildShape({p}), c, quat(1, 0, 0, 0), 0xEEEEull << 16); }
}

void SelfTest::moveUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { mseq.push_back({n, f}); };
  if (stage == 1500) {
    reportName = "move";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Movement range");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 0), 0};
    check(app.enterExplore(), "F5"); app.useScripted = true; app.vm.procedural = false;
    mseq.clear(); mcur = 0; mt = 0;
    push("settings", [this](App& a, float) {
      auto& m = a.move;
      char b[360]; std::snprintf(b, sizeof b, "tuning (content/settings/movement.json): run %.4f m/s (2.875 x1.1), sprint %.3f (5.75 x1.1), crouch %.2f (1.2 x1.1), prone %.2f (0.6 x1.1), player gravity %.3f m/s^2 (real life: 9.81), jump %.3f m/s (real life 3.1; was SPEC 4: 3.757 x1.6), buffer %.2f s, coyote %.2f s",
                    m.run, m.sprint, m.crouch, m.prone, m.gravity, m.jumpVelocity, m.jumpBuffer, m.coyote);
      check(std::fabs(m.run - 3.1625f) < 1e-3f && std::fabs(m.sprint - 6.325f) < 1e-3f && std::fabs(m.crouch - 1.32f) < 1e-3f && std::fabs(m.prone - 0.66f) < 1e-3f &&
            std::fabs(m.gravity - 9.81f) < 1e-3f && std::fabs(m.jumpVelocity - 3.1f) < 2e-3f && m.jumpBuffer >= 0.1f && m.jumpBuffer <= 0.2f && m.coyote >= 0.08f && m.coyote <= 0.15f, b);
      summary["movement"] = {{"run", m.run}, {"sprint", m.sprint}, {"crouch", m.crouch}, {"prone", m.prone}, {"gravity", m.gravity}, {"jump", m.jumpVelocity}};
      return true; });
    auto speedTest = [this](const char* name, vec2 mv, bool sprint, int pose, float want, bool slow) {
      return [this, name, mv, sprint, pose, want, slow](App& a, float t) {
        static vec3 p0;
        if (t == 0) { put(a, vec3(0, 0, 0)); a.eyeYaw = 0; a.scriptedInput = ExploreInput{}; a.proneToggle = pose == Physics::PRONE; slowFrames = slow; return false; }
        a.scriptedInput.crouch = pose == Physics::CROUCH; a.scriptedInput.sprint = sprint;
        if (t < 0.8f) { a.scriptedInput.move = vec2(0); return false; }   // settle into the posture
        static float t0 = -1;
        if (t0 < 0) { p0 = Physics::character().pos; t0 = t; a.scriptedInput.move = mv; return false; }
        a.scriptedInput.move = mv;
        if (t - t0 < 2.0f) return false;
        float d = flat(Physics::character().pos, p0), v = d / (t - t0);
        t0 = -1;
        a.scriptedInput = ExploreInput{}; slowFrames = false; a.proneToggle = false;
        char b[200]; std::snprintf(b, sizeof b, "%s: %.2f m over the timed window = %.3f m/s (want %.3f)%s", name, d, v, want, slow ? " at 10 fps" : "");
        check(std::fabs(v - want) / want < 0.03f, b);
        return true; };
    };
    push("run", speedTest("run", vec2(0, 1), false, Physics::STAND, 3.1625f, false));
    push("run diagonal", speedTest("diagonal W+D (normalised)", vec2(1, 1), false, Physics::STAND, 3.1625f, false));
    push("run 10fps", speedTest("run", vec2(0, 1), false, Physics::STAND, 3.1625f, true));
    push("sprint", speedTest("sprint", vec2(0, 1), true, Physics::STAND, 6.325f, false));
    push("crouch", speedTest("crouch (sprint held: ignored)", vec2(0, 1), true, Physics::CROUCH, 1.32f, false));
    push("prone", speedTest("prone (sprint held: ignored)", vec2(0, 1), true, Physics::PRONE, 0.66f, false));
    push("stamina", [this](App& a, float t) {   // sprint drains, a jump costs a chunk, rest regenerates; empty blocks sprint / jump
      static float s0, afterSprint = 1e9f, beforeJump, afterJump, sprintBlocked; static int j0, lowJumps;
      const auto& m = a.move;
      if (t == 0) {
        // the arithmetic, independent of frame timing
        float keep = a.stamina, keepIdle = a.staminaIdle;
        a.stamina = m.staminaMax; a.staminaIdle = 0;
        for (int k = 0; k < 120; ++k) a.tickStamina(1.0f / 60.0f, true);       // 2 s sprint
        float r1 = a.stamina; a.spendStamina(m.staminaJump); float r2 = a.stamina;
        for (int k = 0; k < 30; ++k) a.tickStamina(1.0f / 60.0f, false);      // 0.5 s: still inside the pause
        float r3 = a.stamina;
        for (int k = 0; k < 60; ++k) a.tickStamina(1.0f / 60.0f, false);      // +1 s
        float r4 = a.stamina;
        char b[300]; std::snprintf(b, sizeof b, "stamina math: 2 s sprint %.1f (want %.0f), jump -> %.1f, 0.5 s rest %.1f (pause), +1 s %.1f (regen %.0f/s)", r1, m.staminaMax - 2 * m.staminaSprint, r2, r3, r4, m.staminaRegen);
        check(std::fabs(r1 - (m.staminaMax - 2 * m.staminaSprint)) < 0.2f && std::fabs(r1 - r2 - m.staminaJump) < 0.01f && std::fabs(r3 - r2) < 0.01f && r4 > r3 + m.staminaRegen * 0.85f, b);
        a.stamina = keep; a.staminaIdle = keepIdle;
        put(a, vec3(20, 0, -10)); a.scriptedInput = ExploreInput{}; return false;
      }
      if (t < 1.4f) { s0 = a.stamina; return false; }
      if (t < 3.4f) { a.scriptedInput.move = vec2(0, 1); a.scriptedInput.sprint = true; return false; }   // sprint
      if (t < 4.4f) { afterSprint = std::min(afterSprint, a.stamina); a.scriptedInput = ExploreInput{}; a.stamina = m.staminaMax; a.staminaIdle = 0; beforeJump = a.stamina; j0 = Physics::jumpCount(); return false; }   // stand still, rested
      if (t < 4.42f) { a.scriptedInput.jump = true; return false; }
      if (t < 4.7f) { a.scriptedInput.jump = false; afterJump = a.stamina; return false; }   // inside the 0.6 s pause
      if (t < 5.9f) { if (t < 5.72f) { lowJumps = 0; a.stamina = m.staminaJump - 1; a.staminaIdle = 0; j0 = Physics::jumpCount(); } a.scriptedInput.jump = t < 5.74f && t >= 5.72f; lowJumps = Physics::jumpCount() - j0; return false; }
      if (t < 7.0f) { a.stamina = 0.5f; a.staminaIdle = 0; a.scriptedInput.move = vec2(0, 1); a.scriptedInput.sprint = true; sprintBlocked = a.vm.speed; return false; }   // held empty
      a.scriptedInput = ExploreInput{};
      if (t < 9.5f) return false;   // rest
      char b[400]; std::snprintf(b, sizeof b, "stamina in play: rested %.0f, sprinting -> %.1f; a standing jump costs %.1f (want %.0f); below the jump cost %d jumps; empty: %.2f m/s (run %.2f, no sprint); 2.5 s rest -> %.1f",
                    s0, afterSprint, beforeJump - afterJump, m.staminaJump, lowJumps, sprintBlocked, m.run, a.stamina);
      check(s0 >= m.staminaMax - 0.5f && afterSprint < m.staminaMax - 10 && std::fabs(beforeJump - afterJump - m.staminaJump) < 1.0f && lowJumps == 0 &&
            sprintBlocked < m.run + 0.3f && sprintBlocked > m.run * 0.5f && a.stamina > 20.0f, b);
      return true; });
    push("gravity + jump", [this](App& a, float t) {
      static float y0, apex, tAir; static bool air;
      if (t == 0) { put(a, vec3(20, 0, 0)); a.scriptedInput = ExploreInput{}; apex = -1; air = false; tAir = 0; return false; }
      if (t < 1.4f) { y0 = Physics::character().pos.y; return false; }   // after the previous prone test: wait out the 1.1 s get-up
      if (t < 1.42f) { a.scriptedInput.jump = true; return false; }
      auto c = Physics::character();
      apex = std::max(apex, c.pos.y - y0);
      if (c.pos.y - y0 > 0.02f) { air = true; tAir += 1.0f / 60.0f; }
      if (t < 2.9f) return false;
      float want = a.move.jumpVelocity * a.move.jumpVelocity / (2 * a.move.gravity), wantT = 2 * a.move.jumpVelocity / a.move.gravity;
      char b[200]; std::snprintf(b, sizeof b, "jump apex %.3f m (real life about 0.5 m; v^2/2g = %.3f), airtime %.2f s (2v/g = %.2f)", apex, want, tAir, wantT);
      check(std::fabs(apex - want) < 0.05f && std::fabs(tAir - wantT) < 0.08f, b);
      // a falling rigid body still uses the world gravity
      Part p; p.size = vec3(0.3f); uint32_t body = Physics::addDynamic(Physics::buildShape({p}), vec3(20, 30, 20), quat(1, 0, 0, 0), 0xEEEFull << 16, 5);
      a.vm.procedural = false;
      static uint32_t sb; sb = body; (void)sb;
      Physics::step(0.5f);
      vec3 v = Physics::velocity(body);
      Physics::remove(body);
      check(std::fabs(v.y + 9.81f * 0.5f) < 0.3f, "rigid bodies (shells, doors, trunks) keep 9.81 m/s^2: v_y " + std::to_string(v.y).substr(0, 5) + " after 0.5 s");
      return true; });
    // ---- jump reliability: every press on the ground jumps
    push("jump: press on a frame with no fixed step", [this](App& a, float t) {
      // a 240 Hz frame banks 1/240 s, less than one 1/60 s step: the press used to be consumed by that frame and lost
      static int ph, j0; static float y0, apex;
      if (t == 0) { ph = 0; apex = 0; put(a, vec3(20, 0, 10)); a.scriptedInput = ExploreInput{}; return false; }
      auto c = Physics::character();
      if (ph == 0 && t > 0.6f) {
        y0 = c.pos.y; j0 = Physics::jumpCount();
        a.physAccum = 0; a.scriptedInput.jump = true; a.updateExplore(1.0f / 240.0f);
        check(Physics::jumpCount() == j0 && a.edgeCarry.jump, "a Space press on a frame that runs no fixed step is carried, not dropped");
        ph = 1; return false;
      }
      if (ph == 1) {
        apex = std::max(apex, c.pos.y - y0);
        if (t < 1.9f) return false;
        char b[200]; std::snprintf(b, sizeof b, "...and jumps on the next step (%d impulse, apex %.2f m)", Physics::jumpCount() - j0, apex);
        check(Physics::jumpCount() == j0 + 1 && apex > 0.35f, b);   // real-life jump: apex about 0.49 m
        return true;
      }
      return t > 5; });
    push("jump: press just before landing", [this](App& a, float t) {
      static int ph, j0; static float y0, tPress, hPress, apex2;
      if (t == 0) { ph = 0; apex2 = 0; put(a, vec3(20, 0, 20)); a.scriptedInput = ExploreInput{}; return false; }
      auto c = Physics::character();
      if (ph == 0 && t > 0.5f) { y0 = c.pos.y; j0 = Physics::jumpCount(); a.scriptedInput.jump = true; ph = 1; return false; }
      if (ph == 1) {   // falling back, still airborne, 5-30 cm above the ground: press (buffered until touchdown)
        if (c.vel.y < -1.0f && !c.grounded && c.pos.y - y0 < 0.3f && c.pos.y - y0 > 0.05f) { a.scriptedInput.jump = true; tPress = t; hPress = c.pos.y - y0; ph = 2; }
        return t > 3;
      }
      if (ph == 2) {
        if (t > tPress + 0.25f) apex2 = std::max(apex2, c.pos.y - y0);
        if (t < tPress + 1.2f) return false;
        char b[220]; std::snprintf(b, sizeof b, "Space pressed %.2f m above the ground while falling is buffered and jumps on touchdown (%d impulses, second apex %.2f m)", hPress, Physics::jumpCount() - j0, apex2);
        check(Physics::jumpCount() == j0 + 2 && apex2 > 0.4f, b);
        return true;
      }
      return t > 5; });
    push("jump: coyote time off an edge, no double jump", [this](App& a, float t) {
      static int ph, j0, nAir; static uint32_t plat; static float vAfter, tMid;
      vec3 base(40, a.terrain.height(40, 40), 40);
      if (t == 0) {
        ph = 0; nAir = 0; vAfter = 0;
        plat = slab(base + vec3(0, 0.5f, 0), vec3(4, 1.0f, 4));   // 1 m high platform, top at +1.0
        Physics::setCharacterPos(base + vec3(0, 1.05f, 0)); a.eyeYaw = 0; a.scriptedInput = ExploreInput{}; return false;
      }
      auto c = Physics::character();
      if (ph == 0 && t > 0.5f) { j0 = Physics::jumpCount(); a.scriptedInput.move = vec2(0, 1); ph = 1; return false; }   // walk toward -z, off the edge
      if (ph == 1) {
        if (!c.grounded && c.pos.y < base.y + 1.0f) ++nAir;
        if (nAir == 3) { a.scriptedInput.jump = true; ph = 2; }   // 3 frames (0.05 s) after leaving the edge
        return t > 4;
      }
      if (ph == 2) { vAfter = c.vel.y; tMid = t; ph = 3; return false; }
      if (ph == 3 && t > tMid + 0.25f) { a.scriptedInput.jump = true; ph = 4; return false; }   // mid-air press: no second jump
      if (ph == 4 && t > tMid + 0.45f) {
        a.scriptedInput = ExploreInput{};
        char b[220]; std::snprintf(b, sizeof b, "Space 0.05 s after walking off a 1 m ledge still jumps (v_y %.2f m/s); a second press mid-air does nothing (%d impulse)", vAfter, Physics::jumpCount() - j0);
        check(vAfter > 2.5f && Physics::jumpCount() == j0 + 1, b);
        Physics::remove(plat);
        return true;
      }
      return t > 6; });
    push("jump: right after releasing crouch", [this](App& a, float t) {
      static int ph, j0; static float y0, apex;
      if (t == 0) { ph = 0; apex = 0; put(a, vec3(20, 0, 30)); a.scriptedInput = ExploreInput{}; a.scriptedInput.crouch = true; return false; }
      auto c = Physics::character();
      if (ph == 0 && t > 0.8f) { a.scriptedInput.crouch = false; ph = 1; return false; }
      if (ph == 1) { y0 = c.pos.y; j0 = Physics::jumpCount(); a.scriptedInput.jump = true; ph = 2; return false; }   // next frame: the 0.45 s eye rise is still running
      if (ph == 2) {
        apex = std::max(apex, c.pos.y - y0);
        if (t < 2.4f) return false;
        char b[200]; std::snprintf(b, sizeof b, "Space one frame after releasing Ctrl (crouch -> stand rise running) jumps (apex %.2f m)", apex);
        check(Physics::jumpCount() == j0 + 1 && apex > 0.35f, b);   // real-life jump: apex about 0.49 m
        return true;
      }
      return t > 5; });
    push("jump: every press jumps on a slope", [this](App& a, float t) {
      // a 14 deg ramp, walked up and then down, Space pressed every 1.25 s, alternate presses while sprinting
      static int ph, j0, presses; static uint32_t ramp; static float tNext;
      vec3 base(-80, a.terrain.height(-80, 40), 40);
      if (t == 0) {
        ph = 0; presses = 0;
        Part pr; pr.center = vec3(0); pr.size = vec3(5, 0.4f, 40);
        ramp = Physics::addStatic(Physics::buildShape({pr}), base + vec3(0, 4.6f, 0), glm::angleAxis(radians(14.0f), vec3(1, 0, 0)), 0xEEEDull << 16);   // rises toward -z
        Physics::Hit h; vec3 o = base + vec3(0, 20, 16);
        if (Physics::castRayEx(o, vec3(0, -1, 0), 40, h)) Physics::setCharacterPos(h.pos + vec3(0, 0.05f, 0));
        a.eyeYaw = 0; a.scriptedInput = ExploreInput{}; return false;
      }
      if (ph == 0 && t > 0.6f) { j0 = Physics::jumpCount(); a.scriptedInput.move = vec2(0, 1); tNext = t + 0.4f; ph = 1; return false; }
      if (ph == 1) {
        a.scriptedInput.sprint = presses % 2 == 1;
        a.stamina = a.move.staminaMax;   // this step tests jump reliability, not stamina
        if (t > 5.6f) a.scriptedInput.move = vec2(0, -1);   // turn round: back down the ramp
        if (t >= tNext && presses < 8) { a.scriptedInput.jump = true; ++presses; tNext = t + 1.25f; }
        if (presses < 8 || t < tNext - 0.2f) return false;
        a.scriptedInput = ExploreInput{};
        int n = Physics::jumpCount() - j0;
        char b[200]; std::snprintf(b, sizeof b, "on a 14 deg ramp (up and down, run and sprint) %d of %d Space presses jumped", n, presses);
        check(n == presses, b);
        Physics::remove(ramp);
        return true;
      }
      return t > 16; });
    push("ctrl hold", [this](App& a, float t) {
      if (t == 0) { put(a, vec3(0, 0, 10)); a.scriptedInput = ExploreInput{}; return false; }
      a.scriptedInput.crouch = t < 1.0f;
      if (t > 0.6f && t < 0.62f) check(Physics::character().posture == Physics::CROUCH && std::fabs(a.eyeHeight - a.move.eyeCrouch) < 0.03f, "hold Ctrl: crouched (collision 1.2 m, eye " + std::to_string(a.eyeHeight).substr(0, 4) + " m)");
      if (t < 1.6f) return false;
      check(Physics::character().posture == Physics::STAND && std::fabs(a.eyeHeight - a.move.eyeStand) < 0.03f, "release Ctrl: standing again (eye " + std::to_string(a.eyeHeight).substr(0, 4) + " m)");
      return true; });
    push("low ceiling", [this](App& a, float t) {
      static int ph;
      if (t == 0) { ph = 0; put(a, vec3(30, 0, 30)); a.scriptedInput = ExploreInput{}; a.scriptedInput.crouch = true; return false; }
      vec3 base(30, a.terrain.height(30, 30), 30);
      if (ph == 0 && t > 0.6f) { ceilBody = slab(base + vec3(0, 1.45f, 0), vec3(3, 0.1f, 3)); a.scriptedInput.crouch = false; ph = 1; return false; }   // 1.4 m ceiling over a crouched player
      if (ph == 1 && t > 1.4f) {
        check(Physics::character().posture == Physics::CROUCH && a.postureWanted == Physics::STAND, "Ctrl released under a 1.4 m ceiling: stays crouched with the stand request pending");
        check(a.eyeHeight < 1.35f, "camera never rises into the ceiling (" + std::to_string(a.eyeHeight).substr(0, 4) + " m)");
        a.scriptedInput.move = vec2(0, 1); ph = 2; return false;
      }
      if (ph == 2 && t > 4.4f) {
        a.scriptedInput = ExploreInput{};
        check(Physics::character().posture == Physics::STAND, "walking out from under it resolves the pending stand");
        Physics::remove(ceilBody); ceilBody = 0xffffffffu;
        return true;
      }
      return t > 8; });
    push("z toggle + ctrl", [this](App& a, float t) {
      static int ph;
      if (t == 0) { ph = 0; put(a, vec3(-20, 0, 0)); a.scriptedInput = ExploreInput{}; a.scriptedInput.prone = true; return false; }
      auto c = Physics::character();
      if (ph == 0 && t > 0.8f) {
        check(c.posture == Physics::PRONE && std::fabs(a.eyeHeight - a.move.eyeProne) < 0.04f, "Z: prone (1.7 m body capsule, eye " + std::to_string(a.eyeHeight).substr(0, 4) + " m)");
        a.scriptedInput.crouch = true; ph = 1; return false;
      }
      if (ph == 1 && t > 1.4f) { check(c.posture == Physics::PRONE, "while prone, Ctrl does not exit prone"); a.scriptedInput.prone = true; ph = 2; return false; }   // Ctrl+Z
      if (ph == 2 && t > 2.2f) {
        check(c.posture == Physics::CROUCH, "Z with Ctrl held leaves prone into crouch");
        a.scriptedInput.crouch = false; ph = 3; return false;
      }
      if (ph == 3 && t > 2.8f) { check(c.posture == Physics::STAND, "then releasing Ctrl stands"); a.scriptedInput.prone = true; ph = 4; return false; }
      if (ph == 4 && t > 3.6f) { a.scriptedInput.prone = true; ph = 5; return false; }
      if (ph == 5 && t > 4.4f) { check(c.posture == Physics::STAND, "Z twice: prone and back to standing (Ctrl not held)"); return true; }
      return t > 8; });
    push("space from prone + rise times", [this](App& a, float t) {
      // Space while prone stands up (no jump on that press); getting up is timed: prone -> stand move.proneToStand,
      // crouch -> stand move.crouchToStand (shorter), eased; the lower posture's speed until done
      static int ph; static float t0, yMax, eyeMid;
      if (t == 0) { ph = 0; put(a, vec3(-30, 0, 20)); a.scriptedInput = ExploreInput{}; a.scriptedInput.prone = true; yMax = -1e9f; return false; }
      auto c = Physics::character();
      if (ph == 0 && t > 1.0f) {
        check(c.posture == Physics::PRONE, "prone before pressing Space");
        a.scriptedInput.jump = true; t0 = t; yMax = c.pos.y; ph = 1; return false;
      }
      if (ph == 1) {
        yMax = std::max(yMax, c.pos.y);
        if (t > t0 + a.move.proneToStand * 0.5f && eyeMid == 0) eyeMid = a.eyeHeight;
        if (t < t0 + a.move.proneToStand + 0.15f) return false;
        char b[260]; std::snprintf(b, sizeof b, "Space while prone stands up without jumping (feet rose %.3f m); halfway through the %.2f s rise the eye is at %.2f m, then %.2f m standing", yMax - c.pos.y, a.move.proneToStand, eyeMid, a.eyeHeight);
        check(c.posture == Physics::STAND && yMax - c.pos.y < 0.05f && eyeMid > a.move.eyeProne + 0.3f && eyeMid < a.move.eyeStand - 0.3f && std::fabs(a.eyeHeight - a.move.eyeStand) < 0.03f, b);
        check(a.move.proneToStand > a.move.crouchToStand && a.move.crouchToStand >= 0.4f, "getting up: prone -> stand " + std::to_string(a.move.proneToStand).substr(0, 4) + " s, crouch -> stand " + std::to_string(a.move.crouchToStand).substr(0, 4) + " s (both were ~0.3 s)");
        a.scriptedInput = ExploreInput{}; a.scriptedInput.crouch = true; ph = 2; t0 = t; eyeMid = 0; return false;
      }
      if (ph == 2 && t > t0 + 0.8f) { a.scriptedInput.crouch = false; t0 = t; ph = 3; return false; }
      if (ph == 3) {
        if (t > t0 + a.move.crouchToStand * 0.5f && eyeMid == 0) eyeMid = a.eyeHeight;
        if (t < t0 + a.move.crouchToStand + 0.15f) return false;
        char b[200]; std::snprintf(b, sizeof b, "crouch -> stand eased over %.2f s: eye %.2f m halfway, %.2f m after", a.move.crouchToStand, eyeMid, a.eyeHeight);
        check(eyeMid > a.move.eyeCrouch + 0.1f && eyeMid < a.move.eyeStand - 0.1f && std::fabs(a.eyeHeight - a.move.eyeStand) < 0.03f, b);
        eyeMid = 0; a.scriptedInput = ExploreInput{};
        return true;
      }
      return t > 10; });
    push("prone exit blocked", [this](App& a, float t) {
      static int ph;
      if (t == 0) { ph = 0; put(a, vec3(-40, 0, 0)); a.scriptedInput = ExploreInput{}; a.scriptedInput.prone = true; return false; }
      vec3 base(-40, a.terrain.height(-40, 0), 0);
      if (ph == 0 && t > 0.8f) { lowBody = slab(base + vec3(0, 0.75f, 0), vec3(4, 0.1f, 4)); a.scriptedInput.prone = true; ph = 1; return false; }   // 0.7 m crawl space
      if (ph == 1 && t > 1.6f) {
        check(Physics::character().posture == Physics::PRONE, "Z under a 0.7 m crawl space: stays prone (neither crouch nor stand fits)");
        a.scriptedInput.move = vec2(0, 1); ph = 2; return false;
      }
      if (ph == 2 && t > 7.6f) {   // crawl out (0.6 m/s): the pending stand resolves
        a.scriptedInput = ExploreInput{};
        check(Physics::character().posture == Physics::STAND, "crawling out from under it stands up (pending request, no penetration)");
        Physics::remove(lowBody); lowBody = 0xffffffffu;
        return true;
      }
      return t > 12; });
    push("prone turning clearance", [this](App& a, float t) {
      static int ph; static float yaw0;
      if (t == 0) { ph = 0; put(a, vec3(-60, 0, 0)); a.eyeYaw = 90; a.scriptedInput = ExploreInput{}; a.scriptedInput.prone = true; return false; }
      vec3 base(-60, a.terrain.height(-60, 0), 0);
      if (ph == 0 && t > 0.8f) {
        yaw0 = Physics::character().bodyYaw;
        wallBody = slab(base + vec3(0, 1.0f, 0.9f), vec3(6, 2, 0.2f));   // wall 0.8 m behind / beside: a body turned to face +x would cross it
        a.eyeYaw = 0; ph = 1; return false;
      }
      if (ph == 1 && t > 1.4f) {
        float by = Physics::character().bodyYaw;
        check(std::fabs(wrapDeg(by - yaw0 + 180) - 180) < 1.0f, "prone body does not swing through a wall (body yaw " + std::to_string((int)by) + " deg kept)");
        Physics::remove(wallBody); wallBody = 0xffffffffu;
        ph = 2; return false;
      }
      if (ph == 2 && t > 2.0f) {
        check(std::fabs(wrapDeg(Physics::character().bodyYaw + 180) - 180) < 1.0f, "with the wall gone the body turns to the view (" + std::to_string((int)Physics::character().bodyYaw) + " deg)");
        a.scriptedInput.prone = true; return true;
      }
      return t > 6; });
    push("no sprint lowered, jump rules", [this](App& a, float t) {
      static float y0, apex;
      if (t == 0) { put(a, vec3(50, 0, -20)); a.scriptedInput = ExploreInput{}; apex = 0; return false; }
      a.scriptedInput.crouch = true;
      if (t < 0.6f) { y0 = Physics::character().pos.y; return false; }
      if (t < 0.62f) { a.scriptedInput.jump = true; return false; }
      apex = std::max(apex, Physics::character().pos.y - y0);
      if (t < 1.5f) return false;
      a.scriptedInput = ExploreInput{};
      check(apex < 0.02f, "no jump while crouched (existing rule kept)");
      return true; });
    push("crater + stairs", [this](App& a, float t) {
      static vec3 P(80, 0, 0);
      if (t == 0) { float h = a.terrain.height(P.x, P.z); a.blastAT = true; a.deformGround(vec3(P.x, h, P.z), 1.0f); a.blastAT = false; put(a, P); a.eyeYaw = -90; a.scriptedInput = ExploreInput{}; a.proneToggle = true; return false; }
      a.scriptedInput.move = vec2(0, 1);
      if (t < 9.0f) return false;
      a.scriptedInput = ExploreInput{}; a.proneToggle = false;
      check(flat(Physics::character().pos, P) > App::kATCraterR + 0.2f, "crawls out of an AT crater while prone (" + std::to_string(flat(Physics::character().pos, P)).substr(0, 4) + " m from the centre)");
      return true; });
    // ---- mantle (mantle.cpp): out of trenches (floor and firestep, front and rear walls), onto a 2 m platform,
    // over a 2.1 m wall; refused at 2.4 m; settings from movement.json; bot links inside the player's range
    push("mantle: settings + bot link limits", [this](App& a, float) {
      auto& m = a.mantleCfg; auto& v = a.vaultCfg;
      json nav; try { nav = json::parse(readTextFile(contentDir() / "ai" / "navigation.json")); } catch (...) {}
      const json& L = nav.contains("links") ? nav["links"] : json::object();
      float bm = L.value("mantleMax_m", 9.0f), bv = L.value("vaultMaxHeight_m", 9.0f);
      char b[400]; std::snprintf(b, sizeof b, "movement.json: vault %.2f-%.2f m (depth %.1f m, hold %.2f s); mantle %.2f-%.2f m in %.1f-%.1f s; bot links (navigation.json) mantle <= %.2f m, vault <= %.2f m stay inside the player's ranges",
                                 v.minH, v.maxH, v.maxDepth, v.hold, m.minH, m.maxH, m.durMin, m.durMax, bm, bv);
      check(std::fabs(v.maxH - 1.35f) < 1e-3f && std::fabs(v.hold - 0.16f) < 1e-3f && m.minH <= v.maxH && m.maxH >= 2.1f && m.maxH <= 2.2f && m.durMin >= 0.9f - 1e-3f && m.durMax <= 1.3f + 1e-3f && bm <= m.maxH && bv <= v.maxH, b);
      summary["mantle"] = {{"minH", m.minH}, {"maxH", m.maxH}, {"durMin", m.durMin}, {"durMax", m.durMax}, {"botMantleMax", bm}};
      return true; });
    struct Exit { const char* asset; vec3 at; vec3 dir; const char* what; };
    static const Exit kExits[] = {
      {"aw_trench_straight", {0, 0, 0}, {0, 0, 1}, "floor, front parapet"},
      {"aw_trench_straight", {1.5f, 0, 0}, {0, 0, -1}, "floor, parados"},
      {"aw_trench_straight", {-1, 0, 0.375f}, {0, 0, 1}, "firestep"},
      {"aw_trench_corner", {-3, 0, 0}, {0, 0, 1}, "floor, outer wall"},
      {"aw_trench_corner", {-3, 0, 0}, {0, 0, -1}, "floor, inner wall"},
      {"aw_trench_corner", {-1, 0, 0.375f}, {0, 0, 1}, "firestep"},
      {"aw_trench_corner", {0, 0, 3}, {1, 0, 0}, "second leg floor"},
      {"aw_trench_zigzag", {-5, 0, 0}, {0, 0, -1}, "floor, rear wall"},
      {"aw_trench_zigzag", {-5, 0, 0.375f}, {0, 0, 1}, "firestep"},
      {"aw_trench_zigzag", {0, 0, 1.8f}, {0, 0, 1}, "fire bay floor"},
      {"aw_trench_zigzag", {-0.6f, 0, 2.175f}, {0, 0, 1}, "fire bay firestep"},
    };
    static std::map<std::string, uint64_t> trenchIds;
    push("mantle: place trenches", [this](App& a, float t) {
      if (t == 0) {
        trenchIds.clear();
        float z = -60;
        for (const char* id : {"aw_trench_straight", "aw_trench_corner", "aw_trench_zigzag"}) {
          const AssetDef* ad = a.lib.find(id);
          uint64_t iid = ad ? a.placeInstance(ad, vec3(130, 0, z), 0) : 0;
          check(iid != 0, std::string("mantle course: ") + id + " placed");
          if (iid) trenchIds[id] = iid;
          z += 30;
        }
        navSyncCuts(a); a.syncRuntime();
        return false;
      }
      return t > 0.5f; });   // chunk colliders follow the cut over the next frames (w3Frame)
    for (const Exit& e : kExits) {
      push(std::string("mantle: ") + e.asset + " " + e.what, [this, &e](App& a, float t) {
        static int c0, ph; static float tStart, tEnd, yMax; static vec3 p0;
        Instance* in = trenchIds.count(e.asset) ? a.doc.find(trenchIds[e.asset]) : nullptr;
        if (!in) return true;
        quat q = yawQuat(in->yaw);
        vec3 dir = q * e.dir;
        if (t == 0) {
          vec3 w = in->pos + q * e.at;
          Physics::Hit h; if (Physics::castRayEx(vec3(w.x, in->pos.y + 2.0f, w.z), vec3(0, -1, 0), 6.0f, h)) w.y = h.pos.y + 0.03f; else w.y = a.terrain.height(w.x, w.z);
          Physics::setCharacterPos(w);
          a.eyeYaw = degrees(std::atan2(-dir.x, -dir.z)); a.eyePitch = 0; a.scriptedInput = ExploreInput{};
          c0 = a.mantle.count; ph = 0; tStart = tEnd = -1; yMax = -1e9f; p0 = w;
          return false;
        }
        auto c = Physics::character();
        if (ph == 0 && t > 0.4f) { p0 = c.pos; a.scriptedInput.jump = true; a.scriptedInput.jumpHeld = true; a.scriptedInput.move = vec2(0, 1); ph = 1; return false; }
        if (ph == 1) {
          if (tEnd < 0) { a.scriptedInput.jumpHeld = true; a.scriptedInput.move = vec2(0, 1); }   // let go once it is done
          if (a.mantle.active && tStart < 0) tStart = t;
          if (tStart >= 0 && !a.mantle.active && tEnd < 0) { tEnd = t; a.scriptedInput = ExploreInput{}; }
          yMax = std::max(yMax, c.pos.y);
          if (tEnd < 0 && t < 3.0f) return false;
          if (tEnd >= 0 && t < tEnd + 0.4f) return false;
          a.scriptedInput = ExploreInput{};
          vec3 lp = glm::inverse(q) * (c.pos - in->pos);
          float out = glm::dot(vec3(lp.x, 0, lp.z) - vec3(e.at.x, 0, e.at.z), e.dir);
          float rise = c.pos.y - p0.y, dur = tEnd > 0 ? tEnd - tStart : -1;
          char b[420]; std::snprintf(b, sizeof b, "%s, %s: hold Space at the wall: %d mantle (%s), %.2f s, feet %.2f m above the start and %.2f m above the ground outside (%.2f m past the start, h %.2f m)",
                                     e.asset, e.what, a.mantle.count - c0, a.mantle.why.c_str(), dur, rise, c.pos.y - in->pos.y, out, a.mantle.h);
          check(a.mantle.count == c0 + 1 && dur >= 0.85f && dur <= 1.35f && c.pos.y > in->pos.y - 0.15f && out > 0.5f && !a.mantle.active, b);
          return true;
        }
        return t > 8; });
    }
    push("mantle: remove trenches", [this](App& a, float) {
      for (auto& [k, id] : trenchIds) { uint64_t x = id; a.doc.instances.erase(std::remove_if(a.doc.instances.begin(), a.doc.instances.end(), [x](const Instance& i) { return i.id == x; }), a.doc.instances.end()); }
      trenchIds.clear(); navSyncCuts(a); a.syncRuntime();
      return true; });
    struct Block { const char* what; float h, depth; bool want, over; };
    static const Block kBlocks[] = {
      {"2.1 m garden wall (0.35 m thick): over it", 2.1f, 0.35f, true, true},
      {"2.0 m platform (2 m deep): onto it", 2.0f, 2.0f, true, false},
      {"1.2 m deep platform (too deep to vault): onto it", 1.2f, 2.0f, true, false},
      {"2.4 m wall: out of reach", 2.4f, 0.35f, false, false},
    };
    for (const Block& bk : kBlocks) {
      push(std::string("mantle: ") + bk.what, [this, &bk](App& a, float t) {
        static uint32_t body; static int c0, ph; static float tStart, tEnd, y0; static vec3 base;
        if (t == 0) {
          base = vec3(170, a.terrain.height(170, 40), 40);
          body = slab(base + vec3(0, bk.h * 0.5f, -1.0f - bk.depth * 0.5f), vec3(4, bk.h, bk.depth));   // face 1.0 m in front (-z)
          Physics::setCharacterPos(base + vec3(0, 0.03f, 0)); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{};
          c0 = a.mantle.count; ph = 0; tStart = tEnd = -1;
          return false;
        }
        auto c = Physics::character();
        if (ph == 0 && t > 0.4f) { y0 = c.pos.y; a.scriptedInput.jump = true; a.scriptedInput.jumpHeld = true; a.scriptedInput.move = vec2(0, 1); ph = 1; return false; }
        if (ph == 1) {
          if (tEnd < 0) { a.scriptedInput.jumpHeld = true; a.scriptedInput.move = vec2(0, 1); }   // let go once it is done
          if (a.mantle.active && tStart < 0) tStart = t;
          if (tStart >= 0 && !a.mantle.active && tEnd < 0) { tEnd = t; a.scriptedInput = ExploreInput{}; }
          if (tEnd < 0 && t < 3.0f) return false;
          if (tEnd >= 0 && t < tEnd + 0.4f) return false;
          a.scriptedInput = ExploreInput{};
          float past = -(c.pos.z - base.z) - 1.0f;   // metres beyond the near face
          char b[360]; std::snprintf(b, sizeof b, "%s: %d mantle (%s%s), %.2f s, feet %.2f m above the ground, %.2f m past the face",
                                     bk.what, a.mantle.count - c0, a.mantle.why.c_str(), a.mantle.over ? ", over" : "", tEnd > 0 ? tEnd - tStart : -1.0f, c.pos.y - y0, past);
          bool ok;
          if (!bk.want) ok = a.mantle.count == c0 && c.pos.y - y0 < 1.2f;
          else if (bk.over) ok = a.mantle.count == c0 + 1 && a.mantle.over && past > bk.depth + 0.2f && c.pos.y - y0 < 0.3f;
          else ok = a.mantle.count == c0 + 1 && !a.mantle.over && std::fabs(c.pos.y - y0 - bk.h) < 0.15f && past > 0.3f;
          check(ok, b);
          Physics::remove(body); body = 0xffffffffu;
          return true;
        }
        return t > 8; });
    }
    push("mantle: weapon lowered, hands on the ledge, camera arc", [this](App& a, float t) {
      static uint32_t body; static int ph; static float lowMax, wMax, eyeMin, eyeMax, pitchMin, pitch0; static vec3 base; static int shots0;
      if (t == 0) {
        base = vec3(170, a.terrain.height(170, 70), 70);
        body = slab(base + vec3(0, 0.95f, -2.0f), vec3(4, 1.9f, 2.0f));
        Physics::setCharacterPos(base + vec3(0, 0.03f, 0)); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{};
        ph = 0; lowMax = wMax = 0; eyeMin = 1e9f; eyeMax = -1e9f; pitchMin = 0; shots0 = 0;
        return false;
      }
      if (ph == 0 && t > 0.4f) { a.scriptedInput.jump = true; a.scriptedInput.jumpHeld = true; pitch0 = a.eyePitch; ph = 1; return false; }
      if (ph == 1) {
        a.scriptedInput.jumpHeld = true;
        if (a.mantle.active) {
          shots0 = a.shotsFired; a.scriptedInput.fire = true;   // LMB held from here: no shot while both hands are on the ledge
          vec3 l, r; float w = 0; a.mantleGrips(l, r, w);
          lowMax = std::max(lowMax, a.mantleLower()); wMax = std::max(wMax, w);
          float ey = a.eyePos().y - a.mantle.ledge.y; eyeMin = std::min(eyeMin, ey); eyeMax = std::max(eyeMax, ey);
          pitchMin = std::min(pitchMin, a.eyePitch - pitch0);
          ph = 2;
        }
        return t > 3;
      }
      if (ph == 2) {
        if (a.mantle.active) {
          a.scriptedInput.fire = true;
          vec3 l, r; float w = 0; a.mantleGrips(l, r, w);
          lowMax = std::max(lowMax, a.mantleLower()); wMax = std::max(wMax, w);
          float ey = a.eyePos().y - a.mantle.ledge.y; eyeMin = std::min(eyeMin, ey); eyeMax = std::max(eyeMax, ey);
          pitchMin = std::min(pitchMin, a.eyePitch - pitch0);
          return false;
        }
        a.scriptedInput = ExploreInput{};
        char b[420]; std::snprintf(b, sizeof b, "during the mantle the weapon is lowered (%.2f) and holds fire with LMB held (%d shots), both hands reach the ledge (weight %.2f), the eye rides %.2f-%.2f m over the lip, the view pitches forward %.1f deg and is level again after (%.1f deg)",
                                   lowMax, a.shotsFired - shots0, wMax, eyeMin, eyeMax, -pitchMin, a.eyePitch - pitch0);
        check(lowMax > 0.95f && wMax > 0.95f && a.shotsFired == shots0 && eyeMax > 0.6f && pitchMin < -8.0f && std::fabs(a.eyePitch - pitch0) < 0.5f, b);
        Physics::remove(body); body = 0xffffffffu;
        return true;
      }
      return t > 8; });
    push("reset", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });
  }
  stage = 1501;
  if (mcur >= mseq.size()) { finish(app); return; }
  if (mt == 0) { logf("[selftest] step: %s", mseq[mcur].name.c_str()); app.stamina = app.move.staminaMax; app.staminaIdle = 0; }   // each step starts rested (the stamina step tests the drain)
  bool d2 = mseq[mcur].fn(app, mt);
  mt += dt;
  if (mt > 60) { check(false, "step timed out: " + mseq[mcur].name); d2 = true; }
  if (d2) { ++mcur; mt = 0; }
}
