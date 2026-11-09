// Per-weapon bullet damage (SPEC 6, W1): the table in content/ai/damage.json, the falloff formula, hit zones (head /
// upper torso / lower torso / limbs, plus the neck and upper-thigh sub-zones the gore system spurts from), helmet
// strikes, true travelled range, and the hit on a soldier (puff, damage, FleshHit report). See damage_model.h.
#include "damage_model.h"
#include "rules.h"
#include "game.h"
#include "app.h"
#include <algorithm>
#include <cmath>

DmLastHit gDmLastHit;

namespace {
DamageTable gTable;
bool gLoaded = false;

float num(const json& j, const char* k, float def) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<float>() : def; }

bool parseRow(const json& r, const std::string& id, const DamageTable& T, const json& decisions, WeaponDamage& w, std::string& err) {
  if (!r.is_object()) { err = "row " + id + " is not an object"; return false; }
  w = WeaponDamage{}; w.id = id;
  if (!r.contains("near") || !r.contains("far")) { err = "row " + id + " needs near and far"; return false; }
  w.nearDmg = num(r, "near", 0); w.farDmg = num(r, "far", 0);
  w.falloffStart = num(r, "falloffStart_m", 0); w.falloffEnd = num(r, "falloffEnd_m", w.falloffStart);
  w.rpm = num(r, "rpm", 0);
  w.dataOnly = r.value("dataOnly", false);
  (void)decisions;
  if (r.value("profileDecision", std::string()) == "springfieldProfile" && T.springfieldProfile == "kar98k") {
    w.farDmg = w.nearDmg; w.falloffStart = w.falloffEnd = 0;   // the Kar98k's no-falloff profile
  }
  if (w.nearDmg <= 0 || w.farDmg <= 0 || w.falloffEnd < w.falloffStart) { err = "row " + id + ": damage must be > 0 and falloffEnd >= falloffStart"; return false; }
  return true;
}
}

bool parseDamageTable(const json& j, DamageTable& T, std::string& err) {
  T = DamageTable{};
  if (!j.is_object()) { err = "damage.json is not an object"; return false; }
  T.health = num(j, "health", 100);
  json z = j.value("zones", json::object());
  T.head = num(z, "head", 1.3f); T.torso = num(z, "torso", 1.0f); T.arms = num(z, "arms", 1.0f); T.legs = num(z, "legs", 0.6f);
  if (T.head <= 0 || T.torso <= 0 || T.arms <= 0 || T.legs <= 0) { err = "zones: multipliers must be > 0"; return false; }
  T.lowerTorsoSplit = num(j, "lowerTorsoSplit", 0.5f); T.neckFrom = num(j, "neckFrom", 0.9f); T.upperThighTo = num(j, "upperThighTo", 0.5f);
  T.helmetAbove = num(j, "helmetAboveHeadJoint_m", 0.12f);
  T.helmetReducesDamage = j.value("helmetReducesDamage", false);
  json dec = j.value("decisions", json::object());
  T.springfieldProfile = dec.value("springfieldProfile", std::string("brief"));
  if (T.springfieldProfile != "brief" && T.springfieldProfile != "kar98k") { err = "decisions.springfieldProfile must be 'brief' or 'kar98k'"; return false; }
  if (j.contains("default") && !parseRow(j["default"], "default", T, dec, T.def, err)) return false;
  const json rows = j.value("weapons", json::object());   // a named copy: items() of a temporary would dangle
  for (auto& [id, r] : rows.items()) {
    WeaponDamage w;
    if (!parseRow(r, id, T, dec, w, err)) return false;
    T.weapons[id] = w;
  }
  return true;
}

bool damageTableReload() {
  gLoaded = true;
  std::string err;
  try {
    json j = json::parse(readTextFile(contentDir() / "ai" / "damage.json"));
    if (parseDamageTable(j, gTable, err)) return true;
  } catch (std::exception& e) { err = e.what(); }
  gTable = DamageTable{}; gTable.error = "content/ai/damage.json: " + err;
  logf("damage model: %s (built-in defaults)", gTable.error.c_str());
  return false;
}

const DamageTable& damageTable() { if (!gLoaded) damageTableReload(); return gTable; }

const WeaponDamage& dmRow(const std::string& id) {
  const DamageTable& T = damageTable();
  auto it = T.weapons.find(id);
  return it != T.weapons.end() ? it->second : T.def;
}

float dmSmoothstep(float a, float b, float x) {
  if (b <= a) return 0.0f;   // start == end: no falloff
  float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
float dmBaseDamage(const WeaponDamage& w, float r) { return w.nearDmg + (w.farDmg - w.nearDmg) * dmSmoothstep(w.falloffStart, w.falloffEnd, r); }
float dmZoneMult(const DamageTable& T, int zone) {
  switch (zone) {
    case DZ_HEAD: return T.head;
    case DZ_UPPER_TORSO: case DZ_LOWER_TORSO: case DZ_NECK: return T.torso;
    case DZ_ARM: return T.arms;
    default: return T.legs;   // legs below the hip, upper thigh, feet
  }
}
float dmDamage(const WeaponDamage& w, float r, int zone) { return dmBaseDamage(w, r) * dmZoneMult(damageTable(), zone); }
int dmShotsToKill(const WeaponDamage& w, float r, int zone) {
  float d = dmDamage(w, r, zone);
  return d <= 0 ? 9999 : (int)std::ceil(damageTable().health / d - 1e-4f);
}

static const char* zoneName(int z) { return z == DZ_HEAD ? "head" : z == DZ_UPPER_TORSO ? "upper torso" : z == DZ_LOWER_TORSO ? "lower torso" : z == DZ_NECK ? "neck" : z == DZ_UPPER_THIGH ? "upper thigh" : z == DZ_ARM ? "arm" : "leg"; }

bool dmBulletHit(Game& g, Projectile& p, int victim, const HitZone& hz, vec3 a, vec3 d, float tHit, int shooter) {
  if (victim < 0) { p.travelled += tHit; return false; }   // no soldier on this segment: the round flew on (or struck the world)
  App& app = g.app;
  const DamageTable& T = damageTable();
  // the weapon: the shooter's own gun when it fires this ammunition, else the first weapon that does (emplaced guns)
  int weaponIdx = -1;
  if (shooter >= 0 && shooter < (int)g.sold.size()) {
    int own = g.sold[shooter].localPlayer ? app.activeWeapon : g.sold[shooter].weapon;
    if (own >= 0 && own < (int)app.weaponDefs.size() && app.weaponDefs[own]->container == p.ammo) weaponIdx = own;
  }
  if (weaponIdx < 0) for (size_t k = 0; k < app.weaponDefs.size(); ++k) if (app.weaponDefs[k]->container == p.ammo) { weaponIdx = (int)k; break; }
  std::string wid = weaponIdx >= 0 ? app.weaponDefs[weaponIdx]->id : std::string();
  std::string caliber = weaponIdx >= 0 ? app.weaponDefs[weaponIdx]->caliber : std::string();
  const WeaponDamage& W = dmRow(wid);
  float range = p.travelled + tHit;   // the distance the round actually travelled (SPEC 6.1)
  Soldier& v = g.sold[victim];
  vec3 at = a + d * tHit;
  auto J = [&](int j) { return vec3(v.world * vec4(v.pose.joint[j], 1)); };
  auto outward = [&](vec3 axisPt) { vec3 n = at - axisPt; float l = glm::length(n); return l > 1e-4f ? n / l : -d; };
  // ---- zone refinement on the posed body
  int zone = hz.zone == 0 ? DZ_HEAD : hz.zone == 1 ? DZ_UPPER_TORSO : DZ_LIMB;
  bool helmet = false; vec3 normal = -d;
  if (hz.zone == 0) {
    vec3 head = J(CJ_HEAD), neck = J(CJ_NECK), up = glm::normalize(head - neck + vec3(1e-4f));
    float ax = glm::dot(at - head, up);
    helmet = ax > T.helmetAbove;   // cranium: the helmet dome (face and jaw below it)
    normal = outward(head + up * std::clamp(ax, 0.0f, 0.12f));
  } else if (hz.zone == 1) {
    vec3 h = J(CJ_HIPS), n = J(CJ_NECK), ax = n - h;
    float u = glm::dot(at - h, ax) / std::max(glm::dot(ax, ax), 1e-6f);
    zone = u < T.lowerTorsoSplit ? DZ_LOWER_TORSO : u > T.neckFrom ? DZ_NECK : DZ_UPPER_TORSO;
    normal = outward(h + ax * std::clamp(u, 0.0f, 1.0f));
  } else {
    static const int segs[8][2] = {{CJ_LARM, CJ_LFOREARM}, {CJ_LFOREARM, CJ_LHAND}, {CJ_RARM, CJ_RFOREARM}, {CJ_RFOREARM, CJ_RHAND},
                                   {CJ_LUPLEG, CJ_LLEG}, {CJ_LLEG, CJ_LFOOT}, {CJ_RUPLEG, CJ_RLEG}, {CJ_RLEG, CJ_RFOOT}};
    float best = 1e9f;
    for (auto& s : segs) {
      vec3 p0 = J(s[0]), ax = J(s[1]) - p0;
      float u = std::clamp(glm::dot(at - p0, ax) / std::max(glm::dot(ax, ax), 1e-6f), 0.0f, 1.0f);
      float dd = glm::length(at - (p0 + ax * u));
      bool arm = s[0] == CJ_LARM || s[0] == CJ_LFOREARM || s[0] == CJ_RARM || s[0] == CJ_RFOREARM;
      if (dd < best) { best = dd; normal = outward(p0 + ax * u); zone = arm ? DZ_ARM : (s[0] == CJ_LUPLEG || s[0] == CJ_RUPLEG) && u < T.upperThighTo ? DZ_UPPER_THIGH : DZ_LIMB; }
    }
  }
  float dmg = dmBaseDamage(W, range) * dmZoneMult(T, zone);
  bool prot = v.isProtected(g.t);
  ++g.hitsOnSoldiers;
  // ---- hit effect: the visuals (deflect puff, helmet sparks, blood / dust puff) are drawn by gore (gore.cpp, W7); the helmet sound stays here
  if (!prot && helmet && glm::length(at - app.eyePos()) < 120.0f) app.audio.play("impact_metal", at, 0.6f);
  float hpBefore = v.hp;
  g.damage(victim, dmg, shooter, d, zoneName(zone));   // protection is enforced there too (rules.cpp hook)
  // ---- gore contract (gore_api.h): every flesh hit, after damage
  FleshHit fh; fh.pos = at; fh.normal = normal; fh.dir = d; fh.victimSlot = victim; fh.shooterSlot = shooter; fh.weapon = weaponIdx; fh.zone = zone;
  fh.damage = hpBefore - v.hp;
  auto has = [&](const char* k) { return caliber.find(k) != std::string::npos; };
  bool rifle = has("x57") || has("30-06") || has(".50");
  fh.energy = rifle ? 1.0f : has("Kurz") || has("Carbine") ? 0.7f : 0.4f; fh.exits = rifle && !helmet && !prot;
  fh.helmet = helmet; fh.deflected = prot;
  float bd = 1e9f; for (int j = 0; j < CJ_COUNT; ++j) { float dd = glm::length(J(j) - at); if (dd < bd) { bd = dd; fh.bone = j; } }
  fh.seed = (uint32_t)(g.hitsOnSoldiers * 2654435761u) ^ (uint32_t)(victim * 40503u);
  goreFleshHit(app, fh);
  if (v.localPlayer && !prot && fh.damage > 0) app.hitFlinch(fh.seed);   // suppression flinch (the LAN client: net.cpp "gore")
  gDmLastHit.fh = fh; gDmLastHit.range = range; gDmLastHit.weaponId = wid; gDmLastHit.valid = true;
  if (shooter == g.localSlot && !prot) g.hitMarker = 0.25f;
  return true;
}
