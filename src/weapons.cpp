// Personal weapons (Milestone 4). Stub so the station milestone builds and tests on its own.
#include "app.h"

void App::resetWeapons() {}
void App::stepWeapons(float, const ExploreInput&) {}
void App::drawWeaponUI() {}
void App::weaponDrawItems(std::vector<DrawItem>&) {}
bool App::weaponCamera(float&) const { return false; }
void App::buildWeaponMeshes() {}
std::string App::resupplyPersonal(uint64_t) { return ""; }
