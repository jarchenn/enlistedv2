// --selftest-move: run / sprint / crouch / prone speeds and distances, diagonal normalisation, frame-rate
// independence, player-only gravity and jump, the posture state machine (Ctrl hold, Z toggle, Ctrl+Z,
// blocked stand / prone exit, prone turning clearance), camera heights, no sprint while lowered.
#include "selftest.h"
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
      char b[300]; std::snprintf(b, sizeof b, "tuning (content/settings/movement.json): run %.3f m/s (old 2.3, x1.25), sprint %.2f (old 4.6), crouch %.2f, prone %.2f, player gravity %.3f m/s^2 (old 9.81, x0.8), jump %.3f m/s (old 4.2 x sqrt 0.8)",
                    m.run, m.sprint, m.crouch, m.prone, m.gravity, m.jumpVelocity);
      check(std::fabs(m.run - 2.875f) < 1e-3f && std::fabs(m.sprint - 5.75f) < 1e-3f && std::fabs(m.gravity - 7.848f) < 1e-3f && std::fabs(m.jumpVelocity - 3.757f) < 2e-3f, b);
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
    push("run", speedTest("run", vec2(0, 1), false, Physics::STAND, 2.875f, false));
    push("run diagonal", speedTest("diagonal W+D (normalised)", vec2(1, 1), false, Physics::STAND, 2.875f, false));
    push("run 10fps", speedTest("run", vec2(0, 1), false, Physics::STAND, 2.875f, true));
    push("sprint", speedTest("sprint", vec2(0, 1), true, Physics::STAND, 5.75f, false));
    push("crouch", speedTest("crouch (sprint held: ignored)", vec2(0, 1), true, Physics::CROUCH, 1.2f, false));
    push("prone", speedTest("prone (sprint held: ignored)", vec2(0, 1), true, Physics::PRONE, 0.6f, false));
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
      char b[200]; std::snprintf(b, sizeof b, "jump apex %.3f m (old 0.899 m; v^2/2g = %.3f), airtime %.2f s (2v/g = %.2f)", apex, want, tAir, wantT);
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
    push("reset", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });
  }
  stage = 1501;
  if (mcur >= mseq.size()) { finish(app); return; }
  if (mt == 0) logf("[selftest] step: %s", mseq[mcur].name.c_str());
  bool d2 = mseq[mcur].fn(app, mt);
  mt += dt;
  if (mt > 60) { check(false, "step timed out: " + mseq[mcur].name); d2 = true; }
  if (d2) { ++mcur; mt = 0; }
}
