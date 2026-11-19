"""TR1: the generated trees (procedural trunk + leaf cards) replaced by model trees (Poly Haven CC0 broadleaf bakes and
YN14R's dead trees), on every map.

    python -I tools/tr1_trees.py            # writes content/assets/tr_*.json, content/settings/tree_aliases.json and
                                            # remaps the tree instances of content/templates/*.nmap.json

Each tr_* asset draws one shared bake (model "dir") at its own size ("drawScale", assets.cpp), instanced with wind
(model foliage); it keeps the generated trees' "tree" block (bullet / AT hp, stump, mass), so a hit tree is felled:
the whole model topples about the stump cut over a procedural stump (foliage.cpp treeDrawItems). Its trunk is a box
part (the static collider, and the stump when felled). Category "Vegetation": the same draw-distance rule
(content/settings/maps.json decorCull) as the trees they replace.

The old ids stay loadable: tree_aliases.json maps each to its variants; the engine (mapdoc.cpp) and mapgen
(mapgen/output.py) pick a variant by the instance id (h = id * 0x9E3779B97F4A7C15, h ^= h >> 29, h % n), so a wood
stays mixed and every load gives the same tree; the palette hides the old ids.
"""
import json, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# model bakes: dir -> (height m, half crown width m, wind, lodDist, cullDist, foliage)
BAKES = {
    "ph_island_tree_01": (8.09, 3.36, 1.2, 45, 420, True),
    "ph_island_tree_02": (6.62, 3.95, 1.2, 45, 420, True),
    "ph_island_tree_03": (4.09, 2.33, 1.2, 45, 420, True),
    "ph_tree_small_02": (6.21, 2.88, 1.3, 40, 380, True),
    "pa_tree_dead_b": (12.0, 2.85, 0.3, 60, 420, True),
    "pa_tree_dead_c": (9.0, 3.1, 0.3, 60, 420, True),
    "pa_tree_dead_d": (11.0, 3.0, 0.3, 60, 420, True),
    "pa_tree_dead_e": (10.0, 4.2, 0.3, 60, 420, True),
}
# variant -> (name, bake, drawScale, hpBullet, mass, trunk radius, stump height)
VARIANTS = {
    "tr_oak_a": ("Broad oak-like tree A", "ph_island_tree_02", 1.15, 30, 1747, 0.38, 0.4),
    "tr_oak_b": ("Broad oak-like tree B", "ph_island_tree_02", 1.3, 30, 1747, 0.4, 0.4),
    "tr_oak_c": ("Broad oak-like tree C", "ph_island_tree_03", 1.8, 30, 1500, 0.36, 0.4),
    "tr_ash_a": ("Tall broadleaf tree A", "ph_island_tree_01", 0.85, 30, 352, 0.22, 0.4),
    "tr_ash_b": ("Tall broadleaf tree B", "ph_island_tree_01", 1.0, 30, 420, 0.24, 0.4),
    "tr_ash_c": ("Tall broadleaf tree C", "ph_tree_small_02", 1.15, 30, 352, 0.2, 0.4),
    "tr_apple_a": ("Small orchard tree A", "ph_island_tree_03", 0.85, 30, 97, 0.14, 0.35),
    "tr_apple_b": ("Small orchard tree B", "ph_tree_small_02", 0.6, 30, 97, 0.13, 0.35),
    "tr_apple_c": ("Small orchard tree C", "ph_island_tree_03", 1.0, 30, 110, 0.15, 0.35),
    "tr_poplar_a": ("Tall tree A (poplar rows)", "ph_island_tree_01", 1.45, 30, 1189, 0.28, 0.4),
    "tr_poplar_b": ("Tall tree B (poplar rows)", "ph_island_tree_01", 1.6, 30, 1300, 0.3, 0.4),
    "tr_sapling_a": ("Young tree A", "ph_tree_small_02", 0.4, 4, 15, 0.04, 0.15),
    "tr_sapling_b": ("Young tree B", "ph_island_tree_03", 0.6, 4, 15, 0.05, 0.15),
    "tr_street_a": ("Street tree A", "ph_tree_small_02", 0.9, 18, 220, 0.13, 0.35),
    "tr_street_b": ("Street tree B", "ph_island_tree_03", 0.9, 18, 160, 0.14, 0.35),
    "tr_street_c": ("Street tree C", "ph_island_tree_01", 0.8, 18, 240, 0.12, 0.35),
    "tr_dead_a": ("Dead tree A", "pa_tree_dead_b", 0.6, 30, 352, 0.2, 0.4),
    "tr_dead_b": ("Dead tree B", "pa_tree_dead_c", 0.75, 30, 352, 0.22, 0.4),
    "tr_dead_c": ("Dead tree C", "pa_tree_dead_d", 0.65, 30, 600, 0.25, 0.4),
    "tr_dead_d": ("Dead tree D", "pa_tree_dead_e", 0.7, 30, 600, 0.25, 0.4),
}
ALIASES = {
    "tree_oak": ["tr_oak_a", "tr_oak_b", "tr_oak_c"], "nt3_tree_forest_oak": ["tr_oak_a", "tr_oak_b", "tr_oak_c"],
    "tree_ash": ["tr_ash_a", "tr_ash_b", "tr_ash_c"], "nt3_tree_forest_ash": ["tr_ash_a", "tr_ash_b", "tr_ash_c"],
    "tree_apple": ["tr_apple_a", "tr_apple_b", "tr_apple_c"],
    "tree_poplar": ["tr_poplar_a", "tr_poplar_b"], "nt3_tree_forest_poplar": ["tr_poplar_a", "tr_poplar_b"],
    "nat_sapling_oak": ["tr_sapling_a", "tr_sapling_b"], "nat_sapling_ash": ["tr_sapling_a", "tr_sapling_b"],
    "mod_sapling_paving": ["tr_sapling_a", "tr_sapling_b"],
    "mod_tree_ornamental": ["tr_street_a", "tr_street_b", "tr_street_c"], "mod_tree_columnar": ["tr_street_c", "tr_street_a"],
    "mod_tree_blossom": ["tr_street_b", "tr_street_a"],
    "nt3_tree_dead_oak": ["tr_dead_a", "tr_dead_b", "tr_dead_c", "tr_dead_d"], "nt3_tree_dead_ash": ["tr_dead_a", "tr_dead_b", "tr_dead_c", "tr_dead_d"],
    "nt3_tree_dry_ash": ["tr_dead_a", "tr_dead_b", "tr_dead_c", "tr_dead_d"],
}


def pick(aid, iid):
    al = ALIASES.get(aid)
    if not al: return aid
    h = (iid * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF; h ^= h >> 29
    return al[h % len(al)]


def asset(vid, v):
    name, bake, s, hp, mass, tr, stump = v
    H, hw, wind, lod, cull, fol = BAKES[bake]
    src = json.load(open(os.path.join(ROOT, "content", "assets", bake + ".json"), encoding="utf-8"))
    h, w = H * s, hw * s
    trunkH = round(min(max(h * 0.45, 1.0), 5.0), 2)
    return {"id": vid, "version": 1, "name": name, "category": "Vegetation", "description": "%s, about %.0f m tall (%s)." % (name, h, src["model"].get("credit", bake)),
            "footprint": [round(2 * w, 2), round(2 * w, 2)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 30, "noAutoPanels": True,
            "model": {"dir": bake, "pivot": "ground", "scale": 1, "drawScale": s, "jpgMaps": True, "lodDist": lod, "cullDist": cull, "foliage": fol, "wind": wind,
                      "credit": src["model"].get("credit", "")},
            "tree": {"hpBullet": hp, "hpAT": 1, "stumpHeight": stump, "trunkRadius": tr, "mass": mass, "seed": 1, "cardsPerSphere": 1,
                     "canopy": [[0, round(h * 0.62, 2), 0, round(min(w, h * 0.45), 2)]]},   # sight / rounds through the crown; no leaf cards drawn
            "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
            "materials": [{"name": "bark", "albedo": [0.24, 0.2, 0.16], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 1}],
            "parts": [{"t": "b", "c": [0, trunkH / 2, 0], "s": [round(2 * tr, 3), trunkH, round(2 * tr, 3)], "m": "bark"}],   # the trunk: collider, stump when felled
            "furniture": [], "interactables": [], "panels": []}


def main():
    for vid, v in VARIANTS.items():
        open(os.path.join(ROOT, "content", "assets", vid + ".json"), "w", encoding="utf-8", newline="\n").write(json.dumps(asset(vid, v), separators=(",", ":")))
    json.dump({"_doc": "TR1 (tools/tr1_trees.py): generated tree ids -> model-tree variants; a map placing an old id loads each such tree as one of its variants, picked by the instance id.",
               "aliases": ALIASES}, open(os.path.join(ROOT, "content", "settings", "tree_aliases.json"), "w", encoding="utf-8", newline="\n"), indent=1)
    T = os.path.join(ROOT, "content", "templates"); changed = {}
    for f in sorted(os.listdir(T)):
        if not f.endswith(".nmap.json"): continue
        p = os.path.join(T, f); s = open(p, encoding="utf-8", newline="").read(); j = json.loads(s); n = 0
        for i in j.get("instances", []):
            a = i.get("asset")
            if isinstance(a, dict) and a.get("id") in ALIASES:
                a["id"] = pick(a["id"], i["id"]); a["version"] = 1; n += 1
        if n:
            indent = 1 if s.startswith("{\n ") and not s.startswith("{\n  ") else (2 if s.startswith("{\n  ") else None)
            open(p, "w", encoding="utf-8", newline="").write(json.dumps(j, indent=indent, ensure_ascii=False, separators=None if indent else (",", ":")) + ("\n" if s.endswith("\n") else ""))
            changed[f] = n
    for f, n in changed.items(): print("%-28s %5d trees" % (f, n))
    print(len(VARIANTS), "variants;", sum(changed.values()), "instances remapped")


if __name__ == "__main__":
    assert pick("tree_oak", 1) in ALIASES["tree_oak"] and pick("x", 1) == "x"
    main()
