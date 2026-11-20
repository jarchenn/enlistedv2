"""FL1: the user's plant and stone packs (LOLIPOP oak / maple / lilac / bush packs, DJMaesen's three tree packs, Studio
Lab's clovers, Spazio Silvestre's succulents, Isengrim's stone group, Georgeous' grass) as placeable assets.

    blender ... tools/fl1_flora_export.py -- <pack.glb> <key> <glb dir>/<key>     # per pack: plants anchored, LODs
    python -I tools/fl1_flora.py packs <glb dir>       # content/assets/fl_<key>_pack.json (hidden: the importer's bake spec)
    NormandyMapmaker --import-weapon-models <glb dir with the GLBs> content       # one shared bake per pack
    python -I tools/fl1_flora.py assets                # one asset per plant / stone, the TR1 tree variants re-pointed at the
                                                       # packs, the shrub aliases, the stone in the rock table, map remaps

Each pack is ONE bake (content/assets/models/fl_<key>/): its textures load once; each plant asset draws only its parts
("model": {"part": "<plant>__"}, src/assets.cpp), instanced with wind through the model-foliage path, its LOD the
pack's own LOD2 (or a decimated copy). Trees and bushes carry a "tree" block (felling, as TR1's model trees) and a
trunk box (collider; bushes none); stones a collider box. The packs with a texture set per plant (gnarly, bare,
succulent) bake at 512 px. grass (Sketchfab Standard) is local-only: its bake is
git-ignored and the asset falls back to a committed Poly Haven grass.
"""
import json, os, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SK = "https://sketchfab.com/3d-models/"
LOL = "LOLIPOP (sketchfab.com/lolipop_1707)"
PACKS = {   # key -> (name, credit, wind, lodDist, cullDist, maxTex, category, local only)
    "oak": ("Oak", "Oak trees pack (17var, LODs, seasons, gameready) by %s, CC-BY-4.0, %soak-trees-pack-17var-lods-seasons-gameready-a5e4e64f9f1d4089bdcc6170a9333393" % (LOL, SK), 1.0, 45, 420, 1024, "Vegetation", False),
    "maple": ("Maple", "Maple trees pack (lowpoly, game ready, LODs) by %s, CC-BY-4.0, %smaple-trees-pack-lowpoly-game-ready-lods-b5d2833c258f4054a01ee2b4ef85adf0" % (LOL, SK), 1.0, 45, 420, 1024, "Vegetation", False),
    "lilac": ("Lilac", "Lilac bush pack (12 vars, LODs, game ready) by %s, CC-BY-4.0, %slilac-bush-pack-12-vars-lods-game-ready-10312697ec994fc99355cb94f1963a2e" % (LOL, SK), 1.0, 30, 260, 1024, "Vegetation", False),
    "bush": ("Bush", "Bush models pack (gameready, LODs) by %s, CC-BY-4.0, %sbush-models-pack-gameready-lods-f2d9ffd3e6a94cf0b9464ccd66a4c2f8" % (LOL, SK), 1.0, 20, 180, 1024, "Vegetation", False),
    "gnarly": ("Gnarly tree", "Gnarly trees by DJMaesen, CC-BY-4.0, %sgnarly-trees-30e3921861124c3199e0b48549b53f83" % SK, 0.6, 50, 420, 512, "Vegetation", False),
    "bare": ("Bare tree", "Trees by DJMaesen, CC-BY-4.0, %strees-dbd3f20966de4cae9170413b9c6f891d" % SK, 1.0, 40, 400, 512, "Vegetation", False),
    "deadbranch": ("Dead-branch tree", "Trees by DJMaesen, CC-BY-4.0, %strees-eed7470843504aa592514554a6100fbc" % SK, 0.3, 50, 420, 1024, "Vegetation", False),
    "clover": ("Clover", "Clovers - Plants / Foliage by Studio Lab, CC-BY-4.0, %sclovers-plants-foliage-d68dc45e329e48e3bc83ecc5448a6c25" % SK, 0.6, 10, 60, 1024, "Vegetation", False),
    "succulent": ("Succulent", "Plant Series - Succulents by Spazio Silvestre, CC-BY-4.0, %splant-series-succulents-6a22ed22665743b6bc989978da95e32a" % SK, 0.1, 25, 220, 512, "Desert", False),
    "stone": ("Stone", "stone_group_1 by Isengrim, CC-BY-SA-4.0 (share-alike), %sstone-group-1-fb1bba28ee32463aae91c63a6b5951e1" % SK, 0.0, 30, 400, 1024, "Nature", False),
    "grass": ("Reed grass", "grass by Georgeous, Sketchfab Standard licence (not redistributable): local only", 1.0, 25, 120, 1024, "Vegetation", True),
}


def jw(p, j, compact=True):
    open(p, "w", encoding="utf-8", newline="\n").write(json.dumps(j, separators=(",", ":")) if compact else json.dumps(j, indent=1))


def packs(glb):
    for key, (name, credit, wind, lod, cull, tex, cat, local) in PACKS.items():
        b = json.load(open(os.path.join(glb, key, key + "_bake.json")))
        jw(os.path.join(ROOT, "content", "assets", "fl_%s_pack.json" % key), {
            "id": "fl_%s_pack" % key, "version": 1, "name": "%s pack (bake)" % name, "category": cat, "hidden": True,
            "description": "FL1: the shared bake of the %s pack; its plants are the fl_%s_* assets (tools/fl1_flora.py). Not placed by itself." % (name.lower(), key),
            "footprint": [1, 1], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 30, "noAutoPanels": True,
            "model": {"src": key + ".glb", "lod1Src": key + "_lod1.glb", "dir": "fl_" + key, "pivot": "ground", "scale": 1, "offset": b["offset"],
                      "maxTex": tex, "lod1Tex": 512, "jpgMaps": True, "lodDist": lod, "cullDist": cull, "foliage": True, "wind": wind, "credit": credit},
            "nav": {"effect": "none"}, "materials": [], "parts": [], "furniture": [], "interactables": [], "panels": []}, compact=False)
        print("pack", key, b["offset"])


def parts_of(key):
    """plant id -> (lo, hi) of its parts in the pack bake (asset space, y up), from the baked model.json"""
    m = json.load(open(os.path.join(ROOT, "content", "assets", "models", "fl_" + key, "model.json")))
    out = {}
    for p in m["parts"]:
        if "__" not in p["name"]: continue
        pid = p["name"].split("__")[0]; lo, hi = out.get(pid, ([1e9] * 3, [-1e9] * 3))
        out[pid] = ([min(a, b) for a, b in zip(lo, p["bmin"])], [max(a, b) for a, b in zip(hi, p["bmax"])])
    return out


KIND = {"oak": "tree", "maple": "tree", "gnarly": "tree", "bare": "tree", "deadbranch": "tree", "lilac": "bush", "bush": "bush",
        "clover": "cover", "grass": "cover", "succulent": "succulent", "stone": "stone"}
NICE = {"big": "big", "large": "large", "med": "medium", "medium": "medium", "small": "small", "sapling": "sapling", "bush": "bush",
        "single_tree": "small tree", "small_bush": "small bush", "tree": "tree", "patch": "patch", "stone": "stone", "group": "group"}


def nice(key, pid):
    base, _, n = pid.rpartition("_")
    if not n.isdigit(): base, n = pid, ""
    return ("%s, %s %s" % (PACKS[key][0], NICE.get(base, base.replace("_", " ")), n)).strip()


def plant_asset(aid, key, pid, lo, hi, s=1.0, name=None, tree=None, nav=None, extra_desc=""):
    """one plant of a pack bake, drawn at size s; tree: the "tree" block (None: none)"""
    name_, credit, wind, lod, cull, tex, cat, local = PACKS[key]
    kind = KIND[key]
    w, h, d = [(hi[k] - lo[k]) * s for k in range(3)]
    if pid.startswith("sapling"): kind = "sapling" if kind == "tree" else kind
    model = {"dir": "fl_" + key, "part": pid + "__", "pivot": "ground", "scale": 1, "jpgMaps": True, "lodDist": round(min(max(2.6 * h, 18.0), 40.0), 1) if kind in ("tree",) else min(lod, 20),   # the pack's LOD2 from ~26 m (a 10 m tree)
             "cullDist": cull, "foliage": True, "wind": wind, "credit": credit}
    if s != 1.0: model["drawScale"] = round(s, 3)
    if local: model["fallbackDir"] = "ph_grass_medium_01_tall_a"
    parts, mats = [], [{"name": "bark", "albedo": [0.24, 0.2, 0.16], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 1}]
    if kind in ("tree", "sapling"):
        tr = round(min(max(0.012 * h, 0.04), 0.45), 3); th = round(min(max(h * 0.45, 0.6), 5.0), 2)
        parts = [{"t": "b", "c": [0, th / 2, 0], "s": [2 * tr, th, 2 * tr], "m": "bark"}]
        tree = tree or {"hpBullet": 30 if h > 5 else 6, "hpAT": 1, "stumpHeight": 0.4 if h > 5 else 0.12, "trunkRadius": tr, "mass": round(max(15.0, 1.6 * h ** 3)), "seed": 1, "cardsPerSphere": 1}
    elif kind == "bush":
        parts = [{"t": "b", "c": [0, 0.2, 0], "s": [0.1, 0.4, 0.1], "m": "bark", "k": 0}]   # the stump when cut; walk-through
        tree = tree or {"hpBullet": 12, "hpAT": 1, "stumpHeight": 0.15, "trunkRadius": 0.05, "mass": 40, "seed": 1, "cardsPerSphere": 1}
    elif kind in ("stone", "succulent"):
        if kind == "stone" or h > 1.2:
            mats = [{"name": "collider", "albedo": [0.3, 0.28, 0.24], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1}]
            parts = [{"t": "b", "c": [round(0.5 * (lo[0] + hi[0]) * s, 3), round(0.45 * h, 3), round(0.5 * (lo[2] + hi[2]) * s, 3)], "s": [round(0.8 * w, 3), round(0.9 * h, 3), round(0.8 * d, 3)], "m": "collider", "v": 0}]
    else:
        mats = []
    if tree is not None:
        tree = dict(tree); tree["canopy"] = [[0, round(h * 0.62, 2), 0, round(max(0.2, min(0.5 * max(w, d), h * 0.45)), 2)]]
    j = {"id": aid, "version": 1, "name": name or nice(key, pid), "category": cat,
         "description": "%s, about %.1f m tall and %.1f x %.1f m (%s).%s" % (name or nice(key, pid), h, w, d, credit, extra_desc),
         "footprint": [round(max(w, 0.3), 2), round(max(d, 0.3), 2)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 30, "noAutoPanels": True,
         "model": model, "nav": nav or ({"effect": "block"} if kind == "stone" else {"effect": "carve", "cover": []} if kind in ("tree",) else {"effect": "none"}),
         "materials": mats, "parts": parts, "furniture": [], "interactables": [], "panels": []}
    if tree is not None: j["tree"] = tree
    jw(os.path.join(ROOT, "content", "assets", aid + ".json"), j)
    return j


SKIP = {("stone", "stone_2"), ("stone", "stone_4")}   # open shells split off the stone group (a sheet, a bowl), not stones

# TR1's generated-tree variants re-pointed at the packs: variant -> (pack, plant, drawScale). Heights about as before
# (oaks / ashes 10-11 m, poplars 14, orchard 4.5-5, street 7-8, saplings 2.5); oaks, ashes (the maple shapes), apples,
# poplars and street trees from the medium plants (the big ones' distance LOD is 3-5k triangles: D-day's 900 trees); two dead trees from DJMaesen's packs, two stay YN14R's.
TREES = {
    "tr_oak_a": ("oak", "med_1", 1.0), "tr_oak_b": ("oak", "med_2", 1.05), "tr_oak_c": ("oak", "med_3", 0.85),
    "tr_ash_a": ("maple", "medium_2", 0.8), "tr_ash_b": ("maple", "medium_3", 1.05), "tr_ash_c": ("maple", "medium_1", 0.85),
    "tr_apple_a": ("oak", "small_1", 0.85), "tr_apple_b": ("oak", "small_2", 0.85), "tr_apple_c": ("oak", "small_3", 0.9),
    "tr_poplar_a": ("maple", "medium_2", 1.1), "tr_poplar_b": ("oak", "med_3", 1.15),
    "tr_sapling_a": ("maple", "sapling_1", 0.7), "tr_sapling_b": ("maple", "sapling_3", 0.75),
    "tr_street_a": ("maple", "small_1", 1.0), "tr_street_b": ("lilac", "single_tree_1", 0.95), "tr_street_c": ("maple", "medium_3", 0.75),
    "tr_dead_c": ("deadbranch", "tree_1", 0.65), "tr_dead_d": ("gnarly", "tree_3", 0.65),
}
# the generated shrubs -> bush variants (pack, plant, drawScale); the shrub's own "tree" block (hp, mass) and nav kept
BUSHES = {
    "shrub_hazel": [("bs_hazel_a", "lilac", "bush_1", 0.6), ("bs_hazel_b", "lilac", "small_bush_1", 0.7), ("bs_hazel_c", "lilac", "bush_3", 0.75)],
    "nat_hawthorn_bush": [("bs_hawthorn_a", "lilac", "small_bush_3", 0.75), ("bs_hawthorn_b", "bush", "big_4", 1.55), ("bs_hawthorn_c", "bush", "big_1", 1.55)],
    "nat_blackthorn_low": [("bs_blackthorn_a", "bush", "big_2", 0.9), ("bs_blackthorn_b", "bush", "big_5", 0.9)],
    "shrub_bramble": [("bs_bramble_a", "bush", "medium_1", 1.0), ("bs_bramble_b", "bush", "medium_5", 0.9), ("bs_bramble_c", "bush", "small_3", 1.4)],
    "nat_bramble_thicket": [("bs_thicket_a", "bush", "big_3", 0.95), ("bs_thicket_b", "lilac", "small_bush_2", 0.6)],
    "nat_gorse_shrub": [("bs_gorse_a", "bush", "big_5", 0.85), ("bs_gorse_b", "bush", "big_2", 0.85)],
}
DESERT = {"outpost_desert": {"nat_gorse_shrub": ["fl_succulent_columns", "fl_succulent_ball", "fl_succulent_agave", "fl_succulent_prickly_pear"]}}   # succulents: the desert maps only
# stone_group_1 (Isengrim): the first of the user's rocks, into VR1's rock table next to the Poly Haven variants
STONES = {"nat_outcrop_limestone": ("rk_stone_outcrop", "stone_1"), "nat_boulder_limestone_l": ("rk_stone_boulder", "stone_3"),
          "nat_boulder_granite_l": ("rk_stone_boulder_g", "stone_3"), "mod_rock_large": ("rk_stone_rock_l", "stone_1"), "nat_outcrop_schist": ("rk_stone_outcrop_m", "stone_6")}


def pick(al, iid):
    h = (iid * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF; h ^= h >> 29
    return al[h % len(al)]


def load(aid): return json.load(open(os.path.join(ROOT, "content", "assets", aid + ".json"), encoding="utf-8"))


def remap(fn):
    """fn(asset id, instance id, map name) -> new id or None for every template instance; writes the changed maps"""
    T = os.path.join(ROOT, "content", "templates"); out = {}
    for f in sorted(os.listdir(T)):
        if not f.endswith(".nmap.json"): continue
        p = os.path.join(T, f); s = open(p, encoding="utf-8", newline="").read(); j = json.loads(s); n = {}
        for i in j.get("instances", []):
            a = i.get("asset")
            if not isinstance(a, dict): continue
            new = fn(a["id"], i["id"], f[:-10])
            if new and new != a["id"]: n[a["id"]] = n.get(a["id"], 0) + 1; a["id"] = new; a["version"] = 1
        if n:
            indent = 1 if s.startswith("{\n ") and not s.startswith("{\n  ") else (2 if s.startswith("{\n  ") else None)
            o = json.dumps(j, indent=indent, ensure_ascii=False, separators=None if indent else (",", ":"))
            if indent and "\r\n" in s[:200]: o = o.replace("\n", "\r\n")
            open(p, "w", encoding="utf-8", newline="").write(o + s[len(s.rstrip()):])
            out[f] = n
    return out


def assets():
    made = []
    for key in PACKS:
        for pid, (lo, hi) in sorted(parts_of(key).items()):
            if (key, pid) in SKIP: continue
            aid = "fl_%s_%s" % (key, pid)
            plant_asset(aid, key, pid, lo, hi); made.append(aid)
    print(len(made), "plant assets")
    # TR1 variants: same ids (no map changes), new plant and size; their hp / mass / trunk box kept
    P = {k: parts_of(k) for k in PACKS}
    for vid, (key, pid, sc) in TREES.items():
        old = load(vid); lo, hi = P[key][pid]
        j = plant_asset(vid, key, pid, lo, hi, s=sc, name=old["name"], tree={k: v for k, v in old["tree"].items() if k != "canopy"}, nav=old["nav"])
        j["parts"] = old["parts"]; j["description"] = j["description"].replace(".", " (FL1).", 1)
        jw(os.path.join(ROOT, "content", "assets", vid + ".json"), j)
    print(len(TREES), "tree variants re-pointed")
    # shrub variants + aliases (content/settings/tree_aliases.json: the engine / mapgen load old ids as these)
    ta_p = os.path.join(ROOT, "content", "settings", "tree_aliases.json"); ta = json.load(open(ta_p, encoding="utf-8"))
    for sid, vs in BUSHES.items():
        old = load(sid)
        for vid, key, pid, sc in vs:
            lo, hi = P[key][pid]
            tree = {k: old["tree"][k] for k in ("hpBullet", "hpAT", "stumpHeight", "trunkRadius", "mass", "seed") if k in old["tree"]}; tree["cardsPerSphere"] = 1
            plant_asset(vid, key, pid, lo, hi, s=sc, name="%s (%s)" % (old["name"], vid[-1].upper()), tree=tree, nav=old["nav"])
        ta["aliases"][sid] = [v[0] for v in vs]
    ta["_doc"] = ta["_doc"].rstrip(".") + "; FL1 (tools/fl1_flora.py): the generated shrubs -> the bush packs' variants."
    jw(ta_p, ta, compact=False)
    # stones into the rock table (vr1_rocks.py sizes them to the old rock's height)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vr1_rocks as V
    ra_p = os.path.join(ROOT, "content", "settings", "rock_aliases.json"); ra = json.load(open(ra_p, encoding="utf-8"))
    back = {}
    for oid, vs in ra["aliases"].items():
        for v in vs: back.setdefault(v, oid)
    for oid, (vid, pid) in STONES.items():
        j = V.asset(vid, ("Stone (%s)" % pid.replace("_", " "), "fl_stone_" + pid, None, True), V.load(oid) if os.path.exists(os.path.join(ROOT, "content", "assets", oid + ".json")) else {}, V.HEIGHT[oid])
        jw(os.path.join(ROOT, "content", "assets", vid + ".json"), j)
        if vid not in ra["aliases"][oid]: ra["aliases"][oid].append(vid)
        back.setdefault(vid, oid)
    ra["_doc"] = ra["_doc"].rstrip(".") + "; FL1: stone_group_1's stones (rk_stone_*) added as the first of the user's rocks."
    jw(ra_p, ra, compact=False)
    bush_of = {sid: [v[0] for v in vs] for sid, vs in BUSHES.items()}
    def fn(aid, iid, m):
        if m in DESERT and aid in DESERT[m]: return pick(DESERT[m][aid], iid)
        if aid in bush_of: return pick(bush_of[aid], iid)
        o = back.get(aid)
        if o in STONES: return pick(ra["aliases"][o], iid)   # the rock's original id over its list with the stone in it
        return None
    for f, n in remap(fn).items(): print("%-28s %s" % (f, ", ".join("%s %d" % kv for kv in sorted(n.items()))))
    return made


if __name__ == "__main__":
    if sys.argv[1] == "packs": packs(sys.argv[2])
    if sys.argv[1] == "assets": assets()
