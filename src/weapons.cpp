// Personal weapons: definitions (content/weapons/*.json), container-based ammunition accounting,
// reload state machine, recoil springs, sights/scopes, first-person view model, crate resupply.
// HUD "loaded" counts every round in the weapon including the chambered one.
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <numeric>
#include <stb_image_write.h>

enum WPart { WP_BODY, WP_BOLT, WP_MAG, WP_COVER, WP_CLIP, WP_BIPOD, WP_SCOPE, WP_HANDLE, WP_COUNT };
static const char* kPartNames[WP_COUNT] = {"body", "bolt", "mag", "cover", "clip", "bipod", "scope", "handle"};

void App::buildWeaponMeshes() {
  weaponDefs.clear();
  fs::path dir = contentDir() / "weapons";
  if (!fs::exists(dir)) return;
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    json j;
    try { j = json::parse(readTextFile(f)); } catch (...) { continue; }
    auto d = std::make_unique<WeaponDef>();
    d->id = j.value("id", ""); d->name = j.value("name", d->id); d->side = j.value("side", "");
    d->action = j.value("action", "semi"); d->feed = j.value("feed", "magazine"); d->container = j.value("container", "");
    d->caliber = j.value("caliber", ""); d->note = j.value("note", "");
    d->cap = j.value("cap", 1); d->loaded = j.value("loaded", 1); d->reserve = j.value("reserve", 0); d->containerSize = j.value("containerSize", 1);
    d->tracerEvery = j.value("tracerEvery", 0);
    d->rpm = j.value("rpm", 60.0f); d->velocity = j.value("velocity", 800.0f); d->zero = j.value("zero", 100.0f);
    d->boltTime = j.value("boltTime", 0.0f);
    d->reloadEmpty = j["reload"].value("empty", 2.5f); d->reloadPartial = j["reload"].value("partial", d->reloadEmpty); d->reloadRound = j["reload"].value("round", 0.5f);
    d->rPitch = j["recoil"].value("pitch", 1.0f); d->rYaw = j["recoil"].value("yaw", 0.4f); d->rRecover = j["recoil"].value("recover", 8.0f);
    d->rClimb = j["recoil"].value("climb", 0.0f); d->rVisual = j["recoil"].value("visual", 0.05f); d->rSupported = j["recoil"].value("supported", 1.0f);
    d->rear = jvec3(j["sights"]["rear"]); d->front = jvec3(j["sights"]["front"]); d->muzzle = jvec3(j["muzzle"]);
    d->laterWar = j.value("laterWar", false);
    if (j.contains("scope")) { d->hasScope = true; d->scopeMag = j["scope"].value("mag", 2.5f); d->reticle = j["scope"].value("reticle", ""); d->scopeEye = jvec3(j["scope"]["eye"]); d->scopeIrons = j["scope"].value("irons", false); }
    std::map<std::string, int> mi;
    d->mats.push_back(Material{"default"});
    for (auto& mj : j.value("materials", json::array())) {
      Material m; m.name = mj.value("name", ""); m.albedo = jvec3(mj["albedo"], vec3(0.5f)); m.roughness = mj.value("roughness", 0.6f);
      m.metallic = mj.value("metallic", 0.0f); m.grainAxis = mj.value("grain", 1);
      static const std::map<std::string, int> pat = {{"plain", PAT_PLAIN}, {"wood", PAT_WOOD}, {"metal", PAT_METAL}, {"fabric", PAT_FABRIC}};
      auto it = pat.find(mj.value("pattern", "plain")); m.pattern = it == pat.end() ? PAT_PLAIN : it->second;
      mi[m.name] = (int)d->mats.size(); d->mats.push_back(m);
    }
    for (int p = 0; p < WP_COUNT; ++p) {
      MeshData m;
      for (auto& pj : j["parts"].value(kPartNames[p], json::array())) {
        Part q;
        q.center = jvec3(pj["c"]); q.size = jvec3(pj["s"], vec3(0.01f));
        if (pj.contains("r")) q.rot = glm::quat(glm::radians(jvec3(pj["r"])));
        auto it = mi.find(pj.value("m", "")); q.mat = it == mi.end() ? 0 : it->second;
        appendPart(m, q, d->mats[q.mat], mat4(1));
      }
      d->mesh[p].upload(m);
    }
    d->file = f;
    d->bulletMass = j.value("bulletMass", 10.0f);
    if (j.contains("sockets")) { d->gripR = jvec3(j["sockets"]["rightGrip"], d->gripR); d->gripL = jvec3(j["sockets"]["leftSupport"], d->gripL); }
    if (j.contains("model") && useImportedModels) {
      std::string dirName = j["model"].value("dir", "");
      auto it = modelCache.find(dirName);
      if (it == modelCache.end()) {
        auto gm = std::make_shared<GpuModel>();
        std::string err;
        if (!gm->load(dir / "models" / dirName, err)) { logf("weapon %s: model '%s' not loaded (%s); using box parts", d->id.c_str(), dirName.c_str(), err.c_str()); gm.reset(); }
        it = modelCache.emplace(dirName, gm).first;
      }
      d->model = it->second;
      if (d->model) {
        for (size_t k = 0; k < d->model->parts.size(); ++k) {
          const std::string& r = d->model->parts[k].role;
          int role = WP_BODY;
          for (int p = 0; p < WP_COUNT; ++p) if (r == kPartNames[p]) role = p;
          d->modelParts[role].push_back((int)k);
        }
      }
    }
    logf("weapon %-14s %s %s feed %-8s %d+%d", d->id.c_str(), d->action.c_str(), d->caliber.c_str(), d->feed.c_str(), d->loaded, d->reserve);
    weaponDefs.push_back(std::move(d));
  }
}

int WeaponState::reserveRounds() const { return std::accumulate(containers.begin(), containers.end(), 0); }

void App::resetWeapons() {
  wstate.clear();
  for (size_t k = 0; k < weaponDefs.size(); ++k) {
    auto& d = *weaponDefs[k];
    WeaponState s;
    s.loaded = d.loaded;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.containers.push_back(std::min(r, d.containerSize));
    s.reserveMax = d.loaded + d.reserve;   // allowance = the preset total (loaded rounds included)
    s.seed = 1234567u + (uint32_t)k * 7919u;   // deterministic shot-to-shot variation
    wstate[d.id] = s;
  }
  if (activeWeapon >= (int)weaponDefs.size()) activeWeapon = 0;
  adsT = 0; weaponMsg.clear(); { ViewModelState keep = vm; vm = ViewModelState{}; vm.procedural = keep.procedural; vm.showArms = keep.showArms; vm.vmFov = keep.vmFov; }
  viewKickPitch = viewKickYaw = 0;
}

static float wrand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s % 100000u) / 100000.0f; }

int App::weaponIndex(const std::string& id) const { for (size_t k = 0; k < weaponDefs.size(); ++k) if (weaponDefs[k]->id == id) return (int)k; return -1; }

void App::selectWeapon(int idx) {
  if (idx < 0 || idx >= (int)weaponDefs.size()) return;
  if (strictDDay && weaponDefs[idx]->laterWar) { toast(weaponDefs[idx]->name + " is hidden by the strict 6 June 1944 preset."); return; }
  WeaponState& cur = wstate[weaponDefs[activeWeapon]->id];
  if (cur.reload > 0) { cur.reload = 0; cur.reloadKind = 0; weaponMsg = "Reload interrupted (no rounds moved)"; }   // nothing is committed until a reload completes
  activeWeapon = idx; adsT = 0; vm.snap = true;
}

mat4 App::viewModelMatrix(const WeaponDef& d, const WeaponState& s) const {
  vec3 e = eyePos(), f = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  mat4 cam = glm::inverse(glm::lookAt(e, e + f, vec3(0, 1, 0)));
  return cam * vmLocal(d, s);   // viewmodel.cpp: ADS alignment, spring, sway, bob, kick, reload dip
}

vec3 App::weaponMuzzleWorld(const WeaponDef& d, const WeaponState& s) const { return vec3(viewModelMatrix(d, s) * vec4(d.muzzle, 1)); }

bool App::weaponCamera(float& fov) const {
  if (occupied || weaponDefs.empty() || mode != Mode::Explore) return false;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  if (adsT < 0.85f) return false;
  if (d.hasScope && !(d.scopeIrons && scopeIronsMode)) { fov = 72.0f / d.scopeMag; return true; }   // real magnification via the projection
  fov = 58.0f;
  return true;
}

void App::weaponDrawItems(std::vector<DrawItem>& op) {
  R.viewModel.clear(); R.viewModelTex.clear();
  if (occupied || weaponDefs.empty() || mode != Mode::Explore) return;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  const WeaponState& s = wstate[d.id];
  bool scopedView = d.hasScope && !(d.scopeIrons && scopeIronsMode) && adsT > 0.85f;
  if (scopedView) return;   // looking through the optic: the scope mask replaces the model
  mat4 M = viewModelMatrix(d, s);
  bool tex = d.useModel();
  // reload progress drives an imported reload clip (Garand: clip seats, follower and operating rod cycle)
  float animU = 1.0f;
  if (s.reload > 0 && s.reloadKind && s.reloadTotal > 0) animU = std::clamp(1 - s.reload / s.reloadTotal, 0.0f, 1.0f);
  auto add = [&](int p, const mat4& m) {
    if (tex && p != WP_SCOPE) {
      for (int k : d.modelParts[p]) {
        const ModelPart& mp = d.model->parts[k];
        if (p == WP_CLIP && s.loaded == 0 && s.reload <= 0) continue;   // en-bloc clip ejected
        R.viewModelTex.push_back({d.model.get(), k, mp.anim.empty() ? m : M * d.model->animAt(k, animU)});
      }
      return;
    }
    if (d.mesh[p].count) R.viewModel.push_back({&d.mesh[p], m});
  };
  add(WP_BODY, M); add(WP_SCOPE, M);
  // bolt: bolt actions lift and draw the handle; automatics move with each shot
  if (d.action == "bolt") {
    float u = d.boltTime > 0 ? std::clamp(1 - s.bolt / d.boltTime, 0.0f, 1.0f) : 1;
    float phase = s.bolt > 0 ? std::sin(u * 3.14159f) : 0;
    if (s.reload > 0 && s.reloadKind) phase = 1;
    mat4 b = M * glm::translate(mat4(1), vec3(0, 0.028f, 0)) * glm::mat4_cast(glm::angleAxis(radians(70.0f * std::min(1.0f, phase * 2)), vec3(0, 0, 1))) * glm::translate(mat4(1), vec3(0, -0.028f, -0.09f * phase));
    add(WP_BOLT, b);
  } else {
    float cyc = std::clamp(s.cooldown * (d.rpm / 60.0f), 0.0f, 1.0f);
    add(WP_BOLT, M * glm::translate(mat4(1), vec3(0, 0, -0.05f * std::sin(cyc * 3.14159f))));
  }
  float hz = s.cooldown > 0 && d.action == "semi" ? 0.08f * std::sin(std::clamp(s.cooldown * (d.rpm / 60.0f), 0.0f, 1.0f) * 3.14159f) : 0;
  if (s.reload > 0 && d.feed == "enbloc") hz = 0.09f;
  add(WP_HANDLE, M * glm::translate(mat4(1), vec3(0, 0, -hz)));
  // magazine: drops out and a fresh one comes up during a magazine reload
  if (d.feed == "magazine") {
    float y = 0;
    if (s.reload > 0) { float u = 1 - s.reload / s.reloadTotal; y = u < 0.5f ? -0.35f * (u / 0.5f) : -0.35f * (1 - (u - 0.5f) / 0.5f); }
    if (s.loaded > 0 || s.reload > 0) add(WP_MAG, M * glm::translate(mat4(1), vec3(0, y, 0)));
  }
  // MG 42 top cover opens for a belt change; bipod unfolds when supported
  if (d.feed == "belt") {
    float a = 0;
    if (s.reload > 0) { float u = 1 - s.reload / s.reloadTotal; a = u < 0.25f ? u / 0.25f : u > 0.75f ? (1 - u) / 0.25f : 1; }
    add(WP_COVER, M * glm::translate(mat4(1), vec3(0, 0.065f, 0.58f)) * glm::mat4_cast(glm::angleAxis(radians(-75.0f * a), vec3(1, 0, 0))) * glm::translate(mat4(1), vec3(0, -0.065f, -0.58f)));
    float leg = supported ? 1.0f : 0.0f;
    add(WP_BIPOD, M * glm::translate(mat4(1), vec3(0, 0.02f, 1.1f)) * glm::mat4_cast(glm::angleAxis(radians(-80.0f * leg), vec3(1, 0, 0))) * glm::translate(mat4(1), vec3(0, -0.02f, -1.1f)));
  }
  // en-bloc / stripper clip visible while it is being loaded (imported en-bloc clip: always seated, animated)
  if (tex && d.feed == "enbloc") add(WP_CLIP, M);
  else if (s.reload > 0 && (s.reloadKind == 1 || s.reloadKind == 2) && (d.feed == "enbloc" || d.feed == "stripper")) {
    float u = 1 - s.reload / s.reloadTotal;
    float y = u < 0.6f ? 0.12f * (1 - u / 0.6f) : 0;
    if (u < 0.85f) add(WP_CLIP, M * glm::translate(mat4(1), vec3(0, y, 0)));
  }
  armDrawItems(M, d, s);
  (void)op;
}

std::string App::resupplyPersonal(uint64_t crateId) {
  if (weaponDefs.empty()) return "";
  const WeaponDef& d = *weaponDefs[activeWeapon];
  WeaponState& s = wstate[d.id];
  auto& stock = crateStock[crateId];
  if (!stock.count(d.container)) return d.name + ": this crate holds no " + d.container + " (wrong feed type).";
  bool unlimited = crateUnlimited[crateId];
  int headroom = s.reserveMax - (s.loaded + s.reserveRounds());   // total carried may never exceed the preset allowance
  if (headroom <= 0) return d.name + " already at its allowance (" + std::to_string(s.loaded + s.reserveRounds()) + "/" + std::to_string(s.reserveMax) + "). Crate untouched.";
  int fits = headroom / d.containerSize;     // whole clips / magazines / belt segments only: partial containers are kept as they are
  if (fits <= 0) return d.name + ": no room for another full " + d.container + " (" + std::to_string(headroom) + " rounds of headroom).";
  int n = unlimited ? fits : std::min(fits, stock[d.container]);
  if (n <= 0) return d.name + ": crate has no " + d.container + " left.";
  for (int k = 0; k < n; ++k) s.containers.push_back(d.containerSize);
  if (!unlimited) stock[d.container] -= n;
  audio.play("crate", doc.find(crateId)->pos);
  return d.name + " reserve +" + std::to_string(n * d.containerSize) + " rounds (" + std::to_string(n) + " x " + d.container + "), now " + std::to_string(s.reserveRounds()) + " reserve, total " + std::to_string(s.loaded + s.reserveRounds()) + "/" + std::to_string(s.reserveMax) +
         ". Loaded still " + std::to_string(s.loaded) + ": press R to reload.";
}

void App::stepWeapons(float dt, const ExploreInput& in) {
  if (weaponDefs.empty()) return;
  if (in.nextWeapon) {
    int idx = activeWeapon;
    for (size_t k = 0; k < weaponDefs.size(); ++k) { idx = (idx + 1) % (int)weaponDefs.size(); if (!(strictDDay && weaponDefs[idx]->laterWar)) break; }
    selectWeapon(idx);
  }
  const WeaponDef& d = *weaponDefs[activeWeapon];
  WeaponState& s = wstate[d.id];
  bool modal = pauseMenu || showLoadout || choiceFor;
  bool fireHeld = in.fire && !modal && (glfwGetWindowAttrib(win, GLFW_FOCUSED) || selftest);
  adsT = std::clamp(adsT + (in.aim && !modal ? 6.0f : -6.0f) * dt, 0.0f, 1.0f);
  stepViewModel(dt, in, d, s);
  auto ch = Physics::character();
  supported = d.feed == "belt" && ch.crouched && glm::length(in.move) < 0.01f;
  s.cooldown = std::max(0.0f, s.cooldown - dt);
  s.bolt = std::max(0.0f, s.bolt - dt);
  // recoil recovery springs (mouse can compensate meanwhile: the player's own look is separate)
  s.recoilP -= s.recoilP * std::min(1.0f, d.rRecover * dt);
  s.recoilY -= s.recoilY * std::min(1.0f, d.rRecover * dt);
  s.kick -= s.kick * std::min(1.0f, 14.0f * dt);
  if (!fireHeld) { s.triggerReleased = true; s.burst = 0; }
  viewKickPitch = s.recoilP + vm.breathPitch; viewKickYaw = s.recoilY + vm.breathYaw;   // breathing moves the whole sight picture
  // ---- reload start
  if (in.reload && s.reload <= 0) {
    if (s.loaded >= d.cap) weaponMsg = "Already full";
    else if (s.reserveRounds() == 0) weaponMsg = "No reserve " + d.container + " - find a supply crate";
    else if (d.feed == "single" || (d.feed == "stripper" && s.loaded > 0)) { s.reloadKind = 3; s.reload = s.reloadTotal = d.reloadRound; audio.play("bolt", eyePos(), 0.6f); }
    else { s.reloadKind = s.loaded == 0 ? 1 : 2; s.reload = s.reloadTotal = s.loaded == 0 ? d.reloadEmpty : d.reloadPartial; audio.play(d.feed == "belt" ? "belt" : d.feed == "magazine" ? "mag_out" : "bolt", eyePos(), 0.7f); }
  }
  // ---- reload progress / completion: rounds move between locations, never appear
  if (s.reload > 0) {
    if (s.reloadKind == 3 && fireHeld && s.loaded > 0) { s.reload = 0; s.reloadKind = 0; }   // fire interrupts single-round loading
    s.reload -= dt;
    if (s.reload <= 0 && s.reloadKind) {
      auto fullest = [&]() { return std::max_element(s.containers.begin(), s.containers.end()); };
      if (s.reloadKind == 3) {
        auto it = std::min_element(s.containers.begin(), s.containers.end(), [](int a, int b) { return (a == 0 ? 1 << 30 : a) < (b == 0 ? 1 << 30 : b); });
        if (it != s.containers.end() && *it > 0) { --*it; ++s.loaded; }
        s.containers.erase(std::remove(s.containers.begin(), s.containers.end(), 0), s.containers.end());
        audio.play("clip_in", eyePos(), 0.5f);
        if (s.loaded < d.cap && s.reserveRounds() > 0) { s.reload = s.reloadTotal = d.reloadRound; }   // keep loading singly
        else { s.reload = 0; s.reloadKind = 0; }
      } else if (d.feed == "stripper") {
        auto it = fullest(); int n = std::min(d.cap - s.loaded, *it); *it -= n; s.loaded += n;
        s.containers.erase(std::remove(s.containers.begin(), s.containers.end(), 0), s.containers.end());
        s.reload = 0; s.reloadKind = 0; s.bolt = 0.3f; audio.play("clip_in", eyePos(), 0.7f);
      } else {
        // en-bloc clip / magazine / belt: the partial one in the gun is retained in reserve, the fullest goes in
        int retained = s.loaded;
        auto it = fullest(); int n = *it; s.containers.erase(it);
        if (retained > 0) { s.containers.push_back(retained); weaponMsg = std::to_string(retained) + (d.feed == "enbloc" ? " rounds in the ejected clip kept in reserve" : " rounds in the partial " + d.container + " kept"); }
        s.loaded = n;
        s.reload = 0; s.reloadKind = 0;
        audio.play(d.feed == "enbloc" ? "clip_in" : d.feed == "belt" ? "belt" : "mag_in", eyePos(), 0.8f);
      }
    }
  }
  // ---- fire
  bool canCycle = s.cooldown <= 0 && s.bolt <= 0 && s.reload <= 0;
  bool semi = d.action != "auto";
  if (fireHeld && canCycle && (!semi || s.triggerReleased)) {
    s.triggerReleased = false;
    if (s.loaded <= 0) { if (!s.clicked) { audio.play("click", eyePos(), 0.6f); s.clicked = true; } }
    else {
      s.clicked = false;
      s.loaded -= 1; ++s.shotIdx; ++s.burst;
      s.cooldown = 60.0f / d.rpm;
      if (d.action == "bolt") s.bolt = d.boltTime;
      vec3 eye = eyePos();
      vec3 aimDir = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
      float spread = radians(adsT > 0.85f ? 0.08f : (d.action == "auto" ? 1.6f : 1.0f));
      if (d.feed == "belt" && !supported) spread *= 2.5f;
      vec3 rt = glm::normalize(glm::cross(aimDir, vec3(0, 1, 0))), up = glm::cross(rt, aimDir);
      float a = wrand(s.seed) * 6.2831853f, r = std::sqrt(wrand(s.seed)) * std::tan(spread);
      aimDir = glm::normalize(aimDir + rt * std::cos(a) * r + up * std::sin(a) * r);
      // aim from the eye: find the point under the sight line
      Physics::Hit h;
      vec3 P = Physics::castRayEx(eye, aimDir, 600, h) ? h.pos : eye + aimDir * 600.0f;
      // ...but sweep from the real muzzle: a wall beside the barrel stops the shot
      vec3 muzzle = weaponMuzzleWorld(d, s);
      vec3 start = muzzle;
      Physics::Hit hb;
      if (Physics::castRayEx(eye, muzzle - eye, glm::length(muzzle - eye), hb)) start = eye + glm::normalize(muzzle - eye) * std::max(0.0f, hb.t - 0.01f);
      vec3 dir = glm::normalize(P - start);
      if (glm::dot(dir, aimDir) < 0.5f) dir = aimDir;
      // Aimed fire: the round leaves the front sight tip F along the sight line V = normalize(F - R), where R is the
      // rear aperture, both in world space from the posed weapon (sway / breathing move the sights and the shot together).
      if (adsT > 0.85f && !vmScoped(d)) {
        vec3 Rw, Fw; sightLineWorld(d, s, Rw, Fw);
        vec3 V = glm::normalize(Fw - Rw);
        vec3 vr = glm::normalize(glm::cross(V, vec3(0, 1, 0))), vu = glm::cross(vr, V);
        V = glm::normalize(V + vr * std::cos(a) * r + vu * std::sin(a) * r);   // same dispersion draw as the hip path
        Physics::Hit hf;
        start = Fw;
        if (Physics::castRayEx(eye, Fw - eye, glm::length(Fw - eye), hf)) start = eye + glm::normalize(Fw - eye) * std::max(0.0f, hf.t - 0.01f);
        dir = V; lastSightR = Rw; lastSightF = Fw; lastSightV = glm::normalize(Fw - Rw);
      }
      // declared zero: tilt up by the drop at the zero range
      float tz = d.zero / d.velocity, drop = 0.5f * 9.81f * tz * tz;
      dir = glm::normalize(dir + vec3(0, drop / d.zero, 0));
      bool tracer = d.tracerEvery > 0 && s.shotIdx % d.tracerEvery == 0;
      fireProjectile(start, dir, d.velocity, PROJ_BULLET, 0, ~0ull, tracer, d.container);
      lastShotStart = start; lastShotDir = dir; lastShotMuzzle = muzzle;
      vec3 fxm = vmApparent(muzzle);   // effects are drawn with the world projection
      fx.flash(fxm + aimDir * 0.05f, d.action == "auto" ? 0.12f : 0.16f, 0.04f, vec4(8, 5.5f, 2.5f, 1));
      fx.smoke(fxm, aimDir * 2.0f + vec3(0, 0.3f, 0), 0.05f, 1.2f, vec4(0.6f, 0.58f, 0.55f, 0.35f), 0.6f, 0.2f);
      audio.play("shot_" + d.id, muzzle, 1.0f);
      // recoil: profile + deterministic variation; supported MG fire is far easier to hold
      float sc = d.feed == "belt" ? (supported ? d.rSupported : 1.0f) : 1.0f;
      if (adsT > 0.85f) sc *= 0.85f;
      s.recoilP = std::min(9.0f, s.recoilP + sc * (d.rPitch * (0.85f + 0.3f * wrand(s.seed)) + d.rClimb * std::min(s.burst, 12)));
      s.recoilY = std::clamp(s.recoilY + sc * d.rYaw * (wrand(s.seed) * 2 - 1), -4.0f, 4.0f);
      s.kick = std::min(0.08f, s.kick + d.rVisual);
      if (d.feed == "enbloc" && s.loaded == 0) { audio.play("ping", muzzle, 0.8f); weaponMsg = "Clip ejected (ping)"; s.clipPings++; }
    }
  }
}

void App::drawWeaponUI() {
  if (occupied || weaponDefs.empty()) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const WeaponDef& d = *weaponDefs[activeWeapon];
  const WeaponState& s = wstate[d.id];
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  bool scoped = d.hasScope && !(d.scopeIrons && scopeIronsMode) && adsT > 0.85f;
  if (scoped) {
    float R = io.DisplaySize.y * 0.46f;
    for (int k = 0; k < 64; ++k) {
      float a0 = k * 6.2831853f / 64, a1 = (k + 1) * 6.2831853f / 64;
      ImVec2 p0(c.x + std::cos(a0) * R, c.y + std::sin(a0) * R), p1(c.x + std::cos(a1) * R, c.y + std::sin(a1) * R);
      ImVec2 q0(c.x + std::cos(a0) * io.DisplaySize.x, c.y + std::sin(a0) * io.DisplaySize.x), q1(c.x + std::cos(a1) * io.DisplaySize.x, c.y + std::sin(a1) * io.DisplaySize.x);
      dl->AddQuadFilled(p0, q0, q1, p1, IM_COL32(3, 3, 3, 255));
    }
    ImU32 k = IM_COL32(8, 8, 8, 245);
    if (d.reticle == "de_absehen1") {   // German no. 1: pointed centre post, two side posts, gap
      dl->AddTriangleFilled(ImVec2(c.x, c.y), ImVec2(c.x - 5, c.y + 26), ImVec2(c.x + 5, c.y + 26), k);
      dl->AddRectFilled(ImVec2(c.x - 5, c.y + 26), ImVec2(c.x + 5, c.y + R), k);
      dl->AddRectFilled(ImVec2(c.x - R, c.y - 3), ImVec2(c.x - R * 0.18f, c.y + 3), k);
      dl->AddRectFilled(ImVec2(c.x + R * 0.18f, c.y - 3), ImVec2(c.x + R, c.y + 3), k);
    } else {                            // US M81/M82 style: crosshair with a tapered lower post
      dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), k, 1.2f);
      dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y), k, 1.2f);
      dl->AddTriangleFilled(ImVec2(c.x, c.y), ImVec2(c.x - 4, c.y + R), ImVec2(c.x + 4, c.y + R), k);
    }
  }
  char b[256];
  std::string st = s.reload > 0 ? (s.reloadKind == 3 ? "loading rounds" : "reloading") : s.bolt > 0 ? "working the bolt" : s.loaded == 0 ? "EMPTY - R" : "";
  int nC = (int)s.containers.size();
  std::snprintf(b, sizeof b, "%s  %s\nloaded %d (incl. chamber) | reserve %d in %d x %s  (allowance %d)\n%s %s", d.name.c_str(), d.caliber.c_str(), s.loaded, s.reserveRounds(), nC, d.container.c_str(), s.reserveMax,
                st.c_str(), d.feed == "belt" ? (supported ? "[bipod supported]" : "[unsupported - crouch & stand still]") : "");
  ImVec2 ts = ImGui::CalcTextSize(b);
  ImVec2 p(io.DisplaySize.x - ts.x - 30, io.DisplaySize.y - ts.y - 30);
  dl->AddRectFilled(ImVec2(p.x - 10, p.y - 8), ImVec2(p.x + ts.x + 10, p.y + ts.y + 8), IM_COL32(12, 12, 10, 190), 4);
  dl->AddText(p, IM_COL32(235, 228, 200, 255), b);
  if (!weaponMsg.empty()) dl->AddText(ImVec2(p.x, p.y - 26), IM_COL32(255, 210, 130, 230), weaponMsg.c_str());
  if (showLoadout) {
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Loadout / range (L)", &showLoadout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Checkbox("Strict 6 June 1944 preset (hides later-war M1C)", &strictDDay);
    ImGui::Checkbox("Range presentation: show every trajectory", &fx.rangeMode);
    ImGui::Checkbox("Reduced motion (keeps aim displacement)", &reducedMotion);
    { int q = (int)grass.quality; const char* names[] = {"Off", "Low", "Medium", "High"};
      if (ImGui::Combo("Grass", &q, names, 4)) grass.quality = (GrassQuality)q; }
    ImGui::Separator();
    for (size_t k = 0; k < weaponDefs.size(); ++k) {
      auto& w = *weaponDefs[k];
      if (strictDDay && w.laterWar) continue;
      auto& ws = wstate[w.id];
      char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s%s  (%d + %d)", (int)k == activeWeapon ? "> " : "  ", w.name.c_str(), ws.loaded, ws.reserveRounds());
      if (ImGui::Selectable(lbl, (int)k == activeWeapon)) selectWeapon((int)k);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", w.note.c_str());
    }
    if (ImGui::Selectable("  Browning M2HB (mounted) - go to a placed gun")) {
      uint64_t best = 0; float bd = 1e9f;
      for (auto& i : doc.instances) if (assetFor(i)->station.kind == "m2hb") { float dd = glm::length(i.pos - eyePos()); if (dd < bd) { bd = dd; best = i.id; } }
      if (!best) toast("Place a Browning M2HB on the map to crew it.");
      else { showLoadout = false; occupyStation(best, ST_GUNNER); }
    }
    ImGui::End();
  }
}

// --weapon-sheets <dir>: orthographic side and rear views of every weapon on a centimetre grid, with the
// configured sights (rear red, front green, muzzle blue) and IK sockets (right yellow, left cyan) marked.
void App::writeWeaponSheets(const fs::path& outDir) {
  std::error_code ec; fs::create_directories(outDir, ec);
  for (auto& dp : weaponDefs) {
    const WeaponDef& d = *dp;
    std::vector<ModelDraw> md; std::vector<DrawItem> bx;
    float L = 1.0f; vec3 bmin(-0.1f, -0.15f, 0), bmax(0.1f, 0.15f, 1.0f);
    if (d.useModel()) {
      for (int p = 0; p < WP_COUNT; ++p) for (int k : d.modelParts[p]) md.push_back({d.model.get(), k, mat4(1)});
      bmin = d.model->bmin; bmax = d.model->bmax;
    } else {
      for (int p = 0; p < WP_COUNT; ++p) if (d.mesh[p].count) bx.push_back({&d.mesh[p], mat4(1)});
    }
    if (d.hasScope) bx.push_back({&d.mesh[WP_SCOPE], mat4(1)});
    L = bmax.z;
    struct Mark { vec3 p; uint32_t c; };
    std::vector<Mark> marks = {{d.rear, 0xff2020ffu}, {d.front, 0xff20c020u}, {d.muzzle, 0xffff4020u}, {d.gripR, 0xff20e0ffu}, {d.gripL, 0xffffe020u}};
    auto sheet = [&](const char* tag, int W, int H, vec3 eye, vec3 at, vec3 up, float x0, float x1, float y0, float y1, int axH, int axV, float h0, float v0, float hs = 1.0f) {
      mat4 view = glm::lookAt(eye, at, up);
      mat4 proj = glm::ortho(x0, x1, y0, y1, 0.01f, 10.0f);
      std::vector<uint8_t> px;
      R.renderModelSheet(md, bx, view, proj, eye, W, H, px);
      float sx = W / (x1 - x0), sy = H / (y1 - y0);
      auto put = [&](int x, int y, uint32_t c, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        uint8_t* q = &px[((size_t)y * W + x) * 4];
        for (int k = 0; k < 3; ++k) q[k] = (uint8_t)(q[k] * (1 - a) + ((c >> (8 * k)) & 255) * a);
      };
      for (int cm = -100; cm <= 200; ++cm) {   // vertical lines: horizontal axis value h0 + cm/100
        float v = cm / 100.0f;
        int x = (int)std::lround((v - h0 - x0) * sx);
        float a = cm % 10 == 0 ? 0.55f : cm % 5 == 0 ? 0.3f : 0.12f;
        if (x >= 0 && x < W) for (int y = 0; y < H; ++y) put(x, y, 0xff000000u, a);
        int y = (int)std::lround(H - 1 - (v - v0 - y0) * sy);
        if (y >= 0 && y < H) for (int xx = 0; xx < W; ++xx) put(xx, y, 0xff000000u, a);
      }
      for (auto& m : marks) {
        float h = m.p[axH] * hs, v = m.p[axV];
        int cx = (int)std::lround((h - h0 - x0) * sx), cy = (int)std::lround(H - 1 - (v - v0 - y0) * sy);
        for (int k = -9; k <= 9; ++k) { put(cx + k, cy, m.c, 1); put(cx, cy + k, m.c, 1); }
      }
      fs::path p = outDir / (d.id + "_" + tag + ".png");
      stbi_write_png(p.string().c_str(), W, H, 4, px.data(), W * 4);
    };
    // side: camera on -x looking +x, +z to the right, +y up
    float pad = 0.04f, cy = (bmin.y + bmax.y) * 0.5f;
    float spanZ = L + 2 * pad, scale = 2400.0f / spanZ, halfY = 450.0f / scale;
    sheet("side", 2400, 900, vec3(-2, cy, L * 0.5f), vec3(0, cy, L * 0.5f), vec3(0, 1, 0), -spanZ * 0.5f, spanZ * 0.5f, -halfY, halfY, 2, 1, L * 0.5f, cy);
    // rear: camera behind the butt looking +z; screen right = -x
    sheet("rear", 800, 800, vec3(0, cy, -2), vec3(0, cy, 0), vec3(0, 1, 0), -0.16f, 0.16f, -0.16f, 0.16f, 0, 1, 0, cy, -1.0f);
    logf("sheet %s: bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f)", d.id.c_str(), bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);
  }
}
