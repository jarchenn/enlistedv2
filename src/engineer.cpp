// GM3 Engineer building: the item table, limits, ownership tags and outposts (engineer.h; the menu and the timed
// placement are in builder.cpp).
#include "engineer.h"
#include "classes.h"
#include "game.h"

namespace {
struct Table { std::vector<EngItem> items; float dismantle = 10; };
const Table& table() {
  static Table t = [] {
    Table r;
    json j = json::object();
    try { j = json::parse(readTextFile(contentDir() / "settings" / "engineer_builds.json")); } catch (std::exception& e) { logf("engineer: engineer_builds.json: %s", e.what()); }
    r.dismantle = j.value("dismantleTime", 10.0f);
    for (auto& e : j.value("items", json::array())) {
      EngItem it; it.id = e.value("id", ""); it.name = e.value("name", it.id); it.asset = e.value("asset", "");
      it.time = e.value("time", 5.0f); it.limit = e.value("limit", 1); it.perLife = e.value("per", std::string("life")) != "active";
      it.dismantle = e.value("dismantle", false);
      if (!it.asset.empty()) r.items.push_back(it);
    }
    return r;
  }();
  return t;
}
struct Life { int slot = -1, lifeId = -1; std::vector<int> n; };
Life gLife;   // the local engineer's placements this life
int localSlot() { return gGame && gGame->active ? gGame->localSlot : -1; }
const json* tagOf(const Instance& i) { return i.extra.is_object() && i.extra.contains("engineer") ? &i.extra["engineer"] : nullptr; }
}

const std::vector<EngItem>& engItems() { return table().items; }
float engDismantleTime() { return table().dismantle; }
bool engMatchActive() { return gGame && gGame->active; }

// the local soldier's GM1 class decides (classes.json "builds": the Engineer)
bool engineerCanBuild(App& app) {
  (void)app;
  const Soldier* s = engMatchActive() ? gGame->local() : nullptr;
  return s && s->cls >= 0 && s->cls < (int)classDefs().size() && classDefs()[s->cls].builds;
}

int engCount(App& app, int item) {
  const auto& it = engItems();
  if (item < 0 || item >= (int)it.size()) return 0;
  int slot = localSlot();
  if (!it[item].perLife) {   // standing pieces of this kind that this soldier owns
    int n = 0;
    for (auto& i : app.doc.instances) if (const json* t = tagOf(i)) n += t->value("owner", -2) == slot && t->value("kind", std::string()) == it[item].id;
    return n;
  }
  const Soldier* s = gGame ? gGame->local() : nullptr;
  if (!s || gLife.slot != slot || gLife.lifeId != s->lifeId) return 0;   // a new life starts at zero
  return item < (int)gLife.n.size() ? gLife.n[item] : 0;
}

void engPlaced(App& app, int item, uint64_t id) {
  const auto& it = engItems();
  Instance* i = app.doc.find(id);
  if (!i || item < 0 || item >= (int)it.size()) return;
  const Soldier* s = gGame ? gGame->local() : nullptr;
  int slot = localSlot();
  if (!i->extra.is_object()) i->extra = json::object();
  i->extra["engineer"] = json{{"owner", slot}, {"team", s ? s->team : -1}, {"kind", it[item].id}};
  if (s && (gLife.slot != slot || gLife.lifeId != s->lifeId)) { gLife.slot = slot; gLife.lifeId = s->lifeId; gLife.n.assign(it.size(), 0); }
  if (gLife.n.size() < it.size()) gLife.n.resize(it.size(), 0);
  ++gLife.n[item];
}

bool engOwnDismantlable(const App& app, const Instance& i) {
  (void)app;
  const json* t = tagOf(i);
  if (!t || t->value("owner", -2) != localSlot()) return false;
  std::string kind = t->value("kind", std::string());
  for (auto& e : engItems()) if (e.id == kind) return e.dismantle;
  return false;
}

std::vector<EngOutpost> engOutposts(const App& app, int team) {
  std::vector<EngOutpost> out;
  for (auto& i : app.doc.instances) {
    const json* t = tagOf(i);
    if (!t || t->value("kind", std::string()) != "outpost") continue;
    int tm = t->value("team", -1);
    if (team >= 0 && tm != team) continue;
    out.push_back({i.id, i.pos, i.yaw, tm, t->value("owner", -1)});
  }
  return out;
}
