"""VOT T2: the stone pieces the terraced relief needs (writes content/assets/<id>.json).

  drs_retaining_wall   4 m rubble-limestone retaining wall standing on the lower level: 1.95 m to the coping, so a
                       1.0-1.5 m terrace step leaves a 0.45-0.95 m parapet above the upper ground; chains end to end
  drs_town_steps       a 2.4 m wide flight of seven 0.186 m risers (1.3 m) between cheek walls, bottom at -z

    python -I tools/mapgen/vot_t2_assets.py"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MATS = [{"name": "stone", "albedo": [0.4, 0.36, 0.285], "roughness": 0.88, "pattern": "rubble", "metallic": 0, "grain": 1},
        {"name": "stone_dressed", "albedo": [0.52, 0.48, 0.38], "roughness": 0.82, "pattern": "ashlar", "metallic": 0, "grain": 1},
        {"name": "stone_plinth", "albedo": [0.27, 0.255, 0.22], "roughness": 0.9, "pattern": "rubble", "metallic": 0, "grain": 1},
        {"name": "weep", "albedo": [0.06, 0.06, 0.055], "roughness": 0.95, "metallic": 0}]
NAV = {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []}


def box(c, s, m, bv=None):
    p = {"t": "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m}
    if bv:
        p["bv"] = bv
    return p


def retaining_wall():
    parts = [box((0, 0.125, 0), (4, 0.25, 0.8), "stone_plinth", 0.03),
             box((0, 0.7, 0.01), (4, 0.9, 0.62), "stone"),
             box((0, 1.475, 0.04), (4, 0.65, 0.56), "stone"),
             box((0, 1.875, 0.03), (4, 0.15, 0.68), "stone_dressed", 0.02)]
    for x in (-1.3, 0.1, 1.5):   # weep holes on the open face
        parts.append(box((x, 0.42, -0.305), (0.12, 0.12, 0.02), "weep"))
    return {"version": 1, "category": "Dressing", "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 0.6,
            "id": "drs_retaining_wall", "name": "Stone retaining wall (4 m)",
            "footprint": [4, 0.8],
            "description": "4 m run of rubble-limestone retaining wall on a dark footing, 1.95 m to its dressed coping: it holds a "
                           "1-1.5 m terrace step and leaves a low parapet above the upper ground. Stands on the lower level, open "
                           "face to -z; chains end to end (VOT T2 terraces).",
            "ivy": {"auto": True, "density": 0.45}, "nav": NAV,
            "snap": [{"p": [-2, 0, 0], "dir": [-1, 0], "kind": "retaining_wall"}, {"p": [2, 0, 0], "dir": [1, 0], "kind": "retaining_wall"}],
            "materials": MATS, "parts": parts, "furniture": [], "interactables": [], "panels": []}


def town_steps():
    rise, tread, n, w = 0.186, 0.32, 7, 2.4
    z0 = -1.3
    parts = []
    for k in range(n):   # solid blocks: step k from its nosing to the landing
        za = z0 + tread * k
        parts.append(box((0, (k + 1) * rise / 2, (za + 1.3) / 2), (w, (k + 1) * rise, 1.3 - za), "stone_dressed" if k == n - 1 else "stone_plinth", 0.01))
    top = n * rise
    for sx in (-1, 1):   # cheek walls, stepped down toward the foot
        parts.append(box((sx * (w / 2 + 0.175), (top + 0.45) / 2, 0.35), (0.35, top + 0.45, 1.9), "stone"))
        parts.append(box((sx * (w / 2 + 0.175), 0.35, -0.95), (0.35, 0.7, 0.7), "stone"))
        parts.append(box((sx * (w / 2 + 0.175), top + 0.5, 0.35), (0.42, 0.1, 1.9), "stone_dressed", 0.02))
    return {"version": 1, "category": "Dressing", "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 0.4,
            "id": "drs_town_steps", "name": "Town steps, stone (1.3 m)",
            "footprint": [w + 0.7, 2.6],
            "description": "A flight of seven 18.6 cm stone steps (1.3 m) between rubble cheek walls, 2.4 m wide: the stair "
                           "between two terrace levels. Foot at -z on the lower level, landing at +z level with the upper "
                           "ground (VOT T2 terraces).",
            "nav": dict(NAV, effect="none"), "materials": MATS, "parts": parts, "furniture": [], "interactables": [], "panels": []}


if __name__ == "__main__":
    for a in (retaining_wall(), town_steps()):
        p = os.path.join(ROOT, "content", "assets", a["id"] + ".json")
        with open(p, "w", newline="\n") as f:
            json.dump(a, f, indent=1)
        print("wrote", os.path.normpath(p), len(a["parts"]), "parts")
