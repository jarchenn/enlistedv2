"""VR1: the engine-generated rocks (nature.cpp "rock" boulders and outcrops, the part-built mod_rock_* / sd_dry_rock_small,
and the "scatter" stone patches) replaced by model rocks already in the repo (Poly Haven CC0 bakes), on every map.

    python -I tools/vr1_rocks.py      # writes content/assets/rk_*.json, content/settings/rock_aliases.json, the decorCull
                                      # rules of the small ones (content/settings/maps.json) and remaps the rock instances
                                      # of content/templates/*.nmap.json

Each rk_* asset draws one shared Poly Haven bake ("dir") at its own size ("drawScale", like TR1's model trees),
instanced (model foliage). Its collider is the bake's collider box scaled with it; the navmesh effect and the placement
grade are the generated rock's, so cover and routes keep their shape; each variant is sized so its collider stands as
tall as the rock it replaces. The scatter stone patches are dropped (REMOVED: no model fits; they never blocked anything).

The old ids stay loadable: rock_aliases.json maps each to its variants; the engine (mapdoc.cpp) and mapgen
(mapgen/output.py) pick one by the instance id (the TR1 hash). "removed" lists ids whose instances are dropped on load
(the stone patches today). To drop in new rocks later: bake them as assets, then
point the ids here (ALIASES) at them, or add a VARIANTS row (bake, scale) and rerun.
Cave and tunnel rock (cave_*, the caverns' dripstone, lids, ribbed walls, shelves, columns) is structure, not scatter:
it is not touched.
"""
import json, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# variant -> (name, Poly Haven bake asset, drawScale, collider); drawScale None: sized so the collider stands as tall as the
# generated rock it replaces did (its cover height), see HEIGHT
VARIANTS = {
    "rk_boulder_l_a": ("Boulder, large A", "ph_boulder_01", None, True),
    "rk_boulder_l_b": ("Boulder, large B", "ph_rock_moss_set_01_rock04", None, True),
    "rk_boulder_s_a": ("Boulder, small A", "ph_boulder_01", None, True),
    "rk_lime_l_a": ("Pale boulder, large A", "ph_boulder_01", None, True),
    "rk_lime_l_b": ("Pale boulder, large B", "ph_rock_face_01", None, True),
    "rk_lime_m_a": ("Pale boulder A", "ph_boulder_01", None, True),
    "rk_lime_m_b": ("Pale boulder B", "ph_rock_face_01", None, True),
    "rk_schist_m_a": ("Mossy boulder A", "ph_rock_moss_set_02_rock07", None, True),
    "rk_schist_m_b": ("Mossy boulder B", "ph_boulder_01", None, True),
    "rk_slab_a": ("Mossy rock slab A", "ph_rock_moss_set_02_rock10", None, True),
    "rk_slab_b": ("Mossy rock slab B", "ph_rock_moss_set_02_rock13", None, True),
    "rk_group_a": ("Rock group", "ph_coast_rocks_05", None, True),
    "rk_outcrop_l_a": ("Rock outcrop A", "ph_rock_face_01", None, True),
    "rk_outcrop_l_b": ("Rock outcrop B", "ph_rock_moss_set_01_rock04", None, True),
    "rk_outcrop_m_a": ("Rock outcrop, low A", "ph_coast_rocks_05", None, True),
    "rk_outcrop_m_b": ("Rock outcrop, low B", "ph_rock_moss_set_02_rock11", None, True),
    "rk_rock_l_a": ("Rock, large A", "ph_rock_moss_set_01_rock04", None, True),
    "rk_rock_l_b": ("Rock, large B", "ph_rock_face_01", None, True),
    "rk_rock_m_a": ("Rock, medium A", "ph_rock_moss_set_02_rock11", None, True),
    "rk_rock_m_b": ("Rock, medium B", "ph_boulder_01", None, True),
    "rk_rock_s_a": ("Rock, small A", "ph_boulder_01", None, True),
    "rk_rock_s_b": ("Rock, small B", "ph_rock_moss_set_02_rock08", None, True),
}
ALIASES = {
    "nat_boulder_granite_l": ["rk_boulder_l_a", "rk_boulder_l_b"],
    "nat_boulder_granite_s": ["rk_boulder_s_a"],
    "nat_boulder_limestone_l": ["rk_lime_l_a", "rk_lime_l_b"],
    "nat_boulder_limestone_m": ["rk_lime_m_a", "rk_lime_m_b"],
    "nat_boulder_schist_m": ["rk_schist_m_a", "rk_schist_m_b"],
    "nat_boulder_schist_slab": ["rk_slab_a", "rk_slab_b"],
    "nat_boulders_granite_group": ["rk_group_a"],
    "nat_outcrop_limestone": ["rk_outcrop_l_a", "rk_outcrop_l_b"],
    "nat_outcrop_schist": ["rk_outcrop_m_a", "rk_outcrop_m_b"],
    "mod_rock_large": ["rk_rock_l_a", "rk_rock_l_b"],
    "mod_rock_medium": ["rk_rock_m_a", "rk_rock_m_b"],
    "mod_rock_small": ["rk_rock_s_a", "rk_rock_s_b"],
    "sd_dry_rock_small": ["rk_rock_s_a"],
}
# the generated rocks' height above the ground (their "rock" pieces less the sink, or their part boxes), m
HEIGHT = {"nat_boulder_granite_l": 1.7, "nat_boulder_granite_s": 0.6, "nat_boulder_limestone_l": 1.9, "nat_boulder_limestone_m": 1.05,
          "nat_boulder_schist_m": 0.85, "nat_boulder_schist_slab": 0.95, "nat_boulders_granite_group": 1.0, "nat_outcrop_limestone": 2.4,
          "nat_outcrop_schist": 1.4, "mod_rock_large": 2.2, "mod_rock_medium": 0.93, "mod_rock_small": 0.39, "sd_dry_rock_small": 0.37}
# generated ids with no model rock: their instances are dropped on load. The stone patches (a scatter of pebbles, no
# collider) have no fitting model here (Poly Haven's sand_rocks_small brings its own sand bed): point them at the
# user's rocks when they come (ALIASES) and take them out of this list.
REMOVED = ["nat_stones_patch", "nat_stones_granite_patch", "nat_scree_patch"]
SMALL = {"rk_rock_s_a", "rk_rock_s_b"}   # drawn to 35-55 m like the ids they replace


def pick(aid, iid):
    al = ALIASES.get(aid)
    if not al: return aid
    h = (iid * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF; h ^= h >> 29
    return al[h % len(al)]


def load(aid):
    return json.load(open(os.path.join(ROOT, "content", "assets", aid + ".json"), encoding="utf-8"))


def asset(vid, v, old, height):
    name, bake, s, collide = v
    src = load(bake); m = src["model"]
    if s is None:   # the bake's collider top -> the old rock's height
        s = round(height / max(p["c"][1] + 0.5 * p["s"][1] for p in src["parts"]), 3)
    mj = json.load(open(os.path.join(ROOT, "content", "assets", "models", m["dir"], "model.json")))
    lo, hi = mj["bmin"], mj["bmax"]
    if "part" in m:   # FL1: one stone of a pack bake: its own parts' bounds
        ps = [p for p in mj["parts"] if p["name"].startswith(m["part"])]
        lo = [min(p["bmin"][k] for p in ps) for k in range(3)]; hi = [max(p["bmax"][k] for p in ps) for k in range(3)]
    ext = [(hi[k] - lo[k]) * s for k in range(3)]
    parts = []
    if collide:
        for p in src.get("parts", []):
            q = dict(p); q["c"] = [round(c * s, 3) for c in p["c"]]; q["s"] = [round(c * s, 3) for c in p["s"]]; parts.append(q)
    model = {k: m[k] for k in ("dir", "part", "pivot", "jpgMaps", "lodDist", "cullDist", "foliage", "wind", "credit") if k in m}
    model.update({"scale": 1, "drawScale": s})
    return {"id": vid, "version": 1, "name": name, "category": "Nature",
            "description": "%s, about %.1f x %.1f m and %.1f m high (%s)." % (name, ext[0], ext[2], ext[1], m.get("credit", bake)),
            "footprint": [round(ext[0], 2), round(ext[2], 2)], "floors": 0, "enterable": False, "plinthDepth": 0,
            "maxGrade": old.get("maxGrade", 30), "noAutoPanels": True, "model": model,
            "nav": old.get("nav", {"effect": "block"}) if collide else {"effect": "none"},
            "materials": src.get("materials", []) if collide else [], "parts": parts, "furniture": [], "interactables": [], "panels": []}


def main():
    olds = {aid: load(aid) for aid in ALIASES}
    first_old, first_h = {}, {}
    for aid, vs in ALIASES.items():
        for v in vs: first_old.setdefault(v, olds[aid]); first_h.setdefault(v, HEIGHT.get(aid, 1.0))
    for vid, v in VARIANTS.items():
        open(os.path.join(ROOT, "content", "assets", vid + ".json"), "w", encoding="utf-8", newline="\n").write(json.dumps(asset(vid, v, first_old[vid], first_h[vid]), separators=(",", ":")))
    json.dump({"_doc": "VR1 (tools/vr1_rocks.py): engine-generated rock ids -> model-rock variants; a map placing an old id loads each such rock as one of its variants, picked by the instance id (as tree_aliases.json). \"removed\": ids dropped on load. Point an id at new rock assets here to swap them in.",
               "aliases": ALIASES, "removed": REMOVED}, open(os.path.join(ROOT, "content", "settings", "rock_aliases.json"), "w", encoding="utf-8", newline="\n"), indent=1)
    # draw distance of the small ones: as the ids they replace (decorCull keys by asset id)
    mp = os.path.join(ROOT, "content", "settings", "maps.json"); s = open(mp, encoding="utf-8", newline="").read(); j = json.loads(s)
    A = j["decorCull"]["assets"]
    for vid in sorted(SMALL): A[vid] = dict(A["mod_rock_small"])
    open(mp, "w", encoding="utf-8", newline="").write(json.dumps(j, indent=2, ensure_ascii=False).replace("\n", "\r\n" if "\r\n" in s else "\n") + ("\r\n" if s.endswith("\r\n") else "\n" if s.endswith("\n") else ""))
    T = os.path.join(ROOT, "content", "templates"); changed = {}
    for f in sorted(os.listdir(T)):
        if not f.endswith(".nmap.json"): continue
        p = os.path.join(T, f); s = open(p, encoding="utf-8", newline="").read(); j = json.loads(s); n = 0; per = {}
        keep = []
        for i in j.get("instances", []):
            a = i.get("asset")
            if isinstance(a, dict) and a.get("id") in REMOVED: per[a["id"] + " (removed)"] = per.get(a["id"] + " (removed)", 0) + 1; n += 1; continue
            if isinstance(a, dict) and a.get("id") in ALIASES:
                per[a["id"]] = per.get(a["id"], 0) + 1
                a["id"] = pick(a["id"], i["id"]); a["version"] = 1; n += 1
            keep.append(i)
        j["instances"] = keep
        if n:
            indent = 1 if s.startswith("{\n ") and not s.startswith("{\n  ") else (2 if s.startswith("{\n  ") else None)
            out = json.dumps(j, indent=indent, ensure_ascii=False, separators=None if indent else (",", ":"))
            if indent and "\r\n" in s[:200]: out = out.replace("\n", "\r\n")
            open(p, "w", encoding="utf-8", newline="").write(out + (s[len(s.rstrip()):] if s != s.rstrip() else ""))
            changed[f] = per
    for f, per in changed.items(): print("%-28s %s" % (f, ", ".join("%s %d" % kv for kv in sorted(per.items()))))
    print(len(VARIANTS), "variants;", sum(sum(p.values()) for p in changed.values()), "instances remapped")


if __name__ == "__main__":
    assert pick("mod_rock_small", 1) in ALIASES["mod_rock_small"] and pick("x", 1) == "x" and not set(REMOVED) & set(ALIASES)
    assert all(v in VARIANTS for vs in ALIASES.values() for v in vs)
    main()
