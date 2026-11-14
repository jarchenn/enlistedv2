#pragma once
// GM1 player classes (content/settings/classes.json): what each class carries per faction, its ability and its limits.
// Data only here; the deploy screen, loadout and rules read it. GM2 (artillery, explosion packs) and GM3 (Engineer
// builds) key off `ability`, `packs` and `builds`.
#include "common.h"
#include <string>
#include <vector>

struct ClassSide {                       // one faction's view of a class
  std::string name;                      // shown name ("Ranger" / "Fallschirmjäger")
  std::vector<std::string> primary;      // weapon ids to choose from (first = default); empty: none
  std::vector<std::string> secondary;    // second slot (launcher / flamethrower); empty: none
  std::string pistol;                    // "" = no pistol
};
struct ClassDef {
  std::string id;                        // ranger, recon, heavy, engineer, mg, assault, tanker
  ClassSide us, de;
  int frags = 0, smokes = 0, packs = 0;  // grenades and explosion packs (GM2) carried at spawn
  std::string ability;                   // "" or "artillery" (GM2)
  bool builds = false;                   // Engineer build menu (GM3)
  bool drivesTanks = false;              // only these may enter / drive tanks
  float hpMul = 1, staminaMul = 1;       // Assaulter 1.25 / 1.5
  float botWeight = 1;                   // relative share among bots
  const ClassSide& side(const std::string& faction) const { return faction == "DE" ? de : us; }
};

bool loadClasses(const fs::path& file, std::string& err);   // replaces the table; false (table kept empty) on a bad file
const std::vector<ClassDef>& classDefs();
const ClassDef* classById(const std::string& id);
int classIndex(const std::string& id);                       // -1 if unknown
