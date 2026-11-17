"""NT8: content/assets/ph_*.json for the Poly Haven forest models (CC0) baked by tools/bake_ph_forest.py.

    <python> -I tools/catalog_ph_forest.py <bake_report.json> <polyhaven info.json>

Every asset draws through the engine's instanced model-foliage path ("model": {"foliage": true, "wind": k}): needles,
blades and fronds alpha-tested with wind sway; rocks, logs and roots the same path at wind 0. Colliders only on trunks,
logs, stumps and rocks (an invisible box); plants and saplings are walked through. The model credit names the Poly Haven
asset and its authors; docs/credits.md lists them all."""
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# Poly Haven id -> (category, kind, wind, lodDist, cullDist, name)
KIND = {
    "pine_tree_01": ("Trees", "tree", 1.0, 32, 420, "Scots pine"),
    "fir_tree_01": ("Trees", "tree", 1.0, 32, 420, "Fir"),
    "island_tree_01": ("Trees", "tree", 1.2, 45, 420, "Broadleaf tree"),
    "pine_sapling_medium": ("Trees", "sapling", 1.2, 30, 260, "Young pine"),
    "fir_sapling_medium": ("Trees", "sapling", 1.2, 30, 260, "Young fir"),
    "fir_sapling": ("Nature", "sapling", 1.4, 12, 80, "Fir sapling"),
    "pine_sapling_small": ("Nature", "sapling", 1.4, 10, 70, "Pine seedling"),
    "fern_02": ("Nature", "plant", 1.6, 8, 48, "Fern"),
    "shrub_03": ("Nature", "plant", 1.3, 10, 60, "Shrub"),
    "grass_medium_01": ("Nature", "plant", 2.0, 7, 38, "Grass clump"),
    "dry_branches_medium_01": ("Nature", "debris", 0.0, 12, 90, "Dry branches"),
    "dead_tree_trunk": ("Nature", "log", 0.0, 25, 300, "Fallen trunk"),
    "dead_tree_trunk_02": ("Nature", "log", 0.0, 25, 300, "Fallen trunk"),
    "pine_roots": ("Nature", "roots", 0.0, 20, 200, "Pine roots"),
    "root_cluster_01": ("Nature", "roots", 0.0, 20, 200, "Root cluster"),
    "root_cluster_02": ("Nature", "roots", 0.0, 20, 200, "Root cluster"),
    "rock_moss_set_01": ("Nature", "rock", 0.0, 15, 220, "Mossy rock"),
    "rock_moss_set_02": ("Nature", "rock", 0.0, 15, 220, "Mossy rock"),
    "boulder_01": ("Nature", "rock", 0.0, 25, 400, "Boulder"),
    "stone_01": ("Nature", "rock", 0.0, 15, 220, "Stone"),
    "rock_09": ("Nature", "rock", 0.0, 12, 160, "Rock"),
    "rock_face_01": ("Nature", "rock", 0.0, 30, 420, "Rock face"),
    "coastal_cliff_02": ("Nature", "rock", 0.0, 40, 500, "Cliff block"),
    "coastal_cliff_04": ("Nature", "rock", 0.0, 40, 500, "Cliff block"),
    "coast_rocks_05": ("Nature", "rock", 0.0, 35, 450, "Rock pile"),
    "sand_rocks_small_01": ("Nature", "rock", 0.0, 25, 300, "Small rocks"),
}


def pid_of(aid):
    base = aid[3:]
    for k in sorted(KIND, key=len, reverse=True):
        if base == k or base.startswith(k + "_"):
            return k, base[len(k) + 1:]
    return None, None


def main():
    rep = json.load(open(sys.argv[1], encoding="utf-8"))
    info = json.load(open(sys.argv[2], encoding="utf-8"))
    n = 0
    for aid, r in sorted(rep.items()):
        pid, var = pid_of(aid)
        if pid is None:
            continue
        cat, kind, wind, lod, cull, name = KIND[pid]
        sx, sy, sz = r["size"]
        authors = ", ".join(info[pid]["info"].get("authors", {}).keys())
        title = info[pid]["info"].get("name", pid)
        parts = []
        if kind == "tree":   # the trunk: an invisible box round the base (the crown is walked under, shot through)
            parts.append({"t": "b", "c": [0, min(sy, 8.0) / 2, 0], "s": [0.55, min(sy, 8.0), 0.55], "m": "collider", "v": 0})
        elif kind in ("rock", "log") and sy >= 0.6:
            parts.append({"t": "b", "c": [0, sy * 0.42, 0], "s": [round(sx * 0.82, 3), round(sy * 0.84, 3), round(sz * 0.82, 3)], "m": "collider", "v": 0})
        else:   # plants, saplings, roots, branches: nothing to collide with (a stand-in marker only)
            parts.append({"t": "b", "c": [0, sy / 2, 0], "s": [round(max(sx, 0.05), 3), round(max(sy, 0.05), 3), round(max(sz, 0.05), 3)], "m": "collider", "v": 0, "k": 0})
        model = {"src": aid + ".glb", "lod1Src": aid + "_lod1.glb", "dir": aid, "pivot": "ground", "scale": 1,
                 "maxTex": 1024, "lod1Tex": 512 if kind in ("tree", "sapling") else 256,
                 "jpgMaps": True, "lodDist": lod, "cullDist": cull, "foliage": True, "wind": wind,
                 **({"maxTexBy": {aid + "_needles": 3072 if pid in ("fir_tree_01", "pine_tree_01") else 2048}} if kind in ("tree", "sapling") else {}),   # FOL1: the needle atlas at its baked size (512 px tiles; BC3 at load)
                 "credit": "%s by %s, Poly Haven (polyhaven.com/a/%s), CC0" % (title, authors, pid)}
        a = {"id": aid, "version": 1, "name": "%s%s (Poly Haven)" % (name, " " + var.upper() if var else ""), "category": cat,
             "description": "%s from Poly Haven's %s (CC0), baked for the game: %s." % (title, pid,
                 "a decimated trunk and needle cards rendered from the tree itself" if kind in ("tree", "sapling") else
                 "alpha-tested blades with wind" if kind == "plant" else "decimated, with a distance LOD"),
             "footprint": [round(max(sx, 0.2), 2), round(max(sz, 0.2), 2)], "floors": 0, "enterable": False, "plinthDepth": 0,
             "maxGrade": 30, "noAutoPanels": True, "model": model,
             "nav": {"effect": "carve" if kind == "tree" or (kind in ("rock", "log") and sy >= 0.6) else "none",   # low stones and thin logs are stepped over "rooms": [], "doors": [], "entrances": [],
                     "corners": [], "firingPoints": [], "areas": [], "cover": []},
             "materials": [{"name": "collider", "albedo": [0.3, 0.28, 0.24], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1}],
             "parts": parts, "furniture": [], "interactables": [], "panels": []}
        with open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(a, f, separators=(",", ":"))
        n += 1
    print("ph forest assets written:", n)


if __name__ == "__main__":
    main()
