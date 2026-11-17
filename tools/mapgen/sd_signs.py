"""SD2: Search and Destroy site signs (content/assets/sd_sign_*.json), warm orange for A and teal for B (spec 12:
readable A / B signage, A warm, B cool). The letters and arrows are a 5 x 7 pixel font of small raised squares on a
painted steel board, so they read at range without a texture. Variants per site: "wall" (a board fixed flat on a wall
face, its back at z = 0, facing +z) and "post" (the board on two posts); an "arrow" board (letter plus an arrow
pointing to its +x) for junctions.

    <python> -I tools/mapgen/sd_signs.py"""
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GLYPH = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    ">": ["..#..", "...#.", "....#", "#####", "....#", "...#.", "..#.."],
}
COL = {"a": ([0.86, 0.42, 0.08], "A"), "b": ([0.05, 0.52, 0.55], "B")}


def mats(paint):
    return [{"name": "paint", "albedo": paint, "roughness": 0.6, "pattern": "metal", "metallic": 0.1, "grain": 1, "texture": "paint_metal_worn"},
            {"name": "white", "albedo": [0.9, 0.9, 0.86], "roughness": 0.5, "pattern": "plain", "metallic": 0, "grain": 1},
            {"name": "galv", "albedo": [0.6, 0.62, 0.63], "roughness": 0.5, "pattern": "metal", "metallic": 0.55, "grain": 1, "texture": "steel_scratched"},
            {"name": "bolt", "albedo": [0.2, 0.2, 0.2], "roughness": 0.6, "pattern": "metal", "metallic": 0.6, "grain": 1}]


def glyph(parts, ch, cx, cy, z, px):
    rows = GLYPH[ch]
    for r, row in enumerate(rows):
        for c, v in enumerate(row):
            if v == "#":
                parts.append({"t": "b", "c": [round(cx + (c - 2) * px, 4), round(cy + (3 - r) * px, 4), round(z, 4)],
                              "s": [round(px * 0.92, 4), round(px * 0.92, 4), 0.012], "m": "white", "k": 0, "lod": 1})


def board(parts, w, h, cy, z0):
    """A board w x h centred at height cy, its back at z0, 4 cm thick, a white border and four bolts."""
    parts.append({"t": "b", "c": [0, cy, z0 + 0.02], "s": [w, h, 0.04], "m": "paint", "bv": 0.008})
    for sx, sy, ww, hh in ((0, h / 2 - 0.03, w - 0.06, 0.03), (0, -h / 2 + 0.03, w - 0.06, 0.03), (w / 2 - 0.03, 0, 0.03, h - 0.06), (-w / 2 + 0.03, 0, 0.03, h - 0.06)):
        parts.append({"t": "b", "c": [sx, cy + sy, z0 + 0.046], "s": [ww, hh, 0.012], "m": "white", "k": 0, "lod": 1})
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append({"t": "c", "c": [sx * (w / 2 - 0.08), cy + sy * (h / 2 - 0.08), z0 + 0.045], "s": [0.03, 0.012, 0.03], "m": "bolt", "r": [90, 0, 0], "seg": 6, "k": 0, "lod": 2})


def asset(aid, name, desc, fp, parts, paint):
    return {"id": aid, "version": 1, "name": name, "category": "Modern: Military", "description": desc, "footprint": fp, "floors": 0,
            "enterable": False, "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "materials": mats(paint), "parts": parts,
            "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


def main():
    out = []
    for k, (paint, L) in COL.items():
        # wall board: 1.6 x 1.6 m, centred 2.1 m up, letter 1.1 m tall
        p = []
        board(p, 1.6, 1.6, 2.1, 0.0)
        glyph(p, L, 0.0, 2.1, 0.058, 0.16)
        out.append(asset("sd_sign_%s_wall" % k, "Site %s sign (wall)" % L, "Search and Destroy: a %s steel board with a white %s, bolted flat to a wall face (back at the asset origin, facing +z)." % ("orange" if k == "a" else "teal", L), [1.6, 0.1], p, paint))
        # post sign: the same board on two galvanised posts
        p = [{"t": "b", "c": [sx * 0.6, 1.25, -0.06], "s": [0.08, 2.5, 0.08], "m": "galv"} for sx in (-1, 1)]
        board(p, 1.6, 1.6, 2.1, -0.02)
        glyph(p, L, 0.0, 2.1, 0.038, 0.16)
        out.append(asset("sd_sign_%s_post" % k, "Site %s sign (post)" % L, "Search and Destroy: a %s steel board with a white %s on two galvanised posts." % ("orange" if k == "a" else "teal", L), [1.6, 0.3], p, paint))
        # arrow board: letter and an arrow to +x, 2.2 x 0.9 m, wall mounted 1.9 m up
        p = []
        board(p, 2.2, 0.9, 1.9, 0.0)
        glyph(p, L, -0.55, 1.9, 0.058, 0.1)
        glyph(p, ">", 0.45, 1.9, 0.058, 0.1)
        out.append(asset("sd_sign_%s_arrow" % k, "Site %s arrow (wall)" % L, "Search and Destroy: a %s board with a white %s and an arrow pointing along +x, bolted to a wall face (back at the origin, facing +z)." % ("orange" if k == "a" else "teal", L), [2.2, 0.1], p, paint))
    # SD2 connector roof: a 6.6 m precast slab on two steel beams, laid on the 3 m compound walls over one 6 m bay
    roof = [{"t": "b", "c": [0, 3.2, 0], "s": [6.6, 0.18, 6.6], "m": "slab", "bv": 0.02},
            {"t": "b", "c": [0, 3.32, 0], "s": [6.7, 0.06, 6.7], "m": "felt", "k": 0, "lod": 1}]
    for sx in (-1, 1):
        roof.append({"t": "b", "c": [sx * 1.6, 3.02, 0], "s": [0.16, 0.18, 6.5], "m": "beam"})
    roof_mats = [{"name": "slab", "albedo": [0.62, 0.6, 0.55], "roughness": 0.85, "pattern": "plain", "metallic": 0, "grain": 1, "texture": "concrete_formwork"},
                 {"name": "felt", "albedo": [0.2, 0.19, 0.18], "roughness": 0.9, "pattern": "plain", "metallic": 0, "grain": 1},
                 {"name": "beam", "albedo": [0.3, 0.31, 0.32], "roughness": 0.55, "pattern": "metal", "metallic": 0.6, "grain": 1, "texture": "steel_scratched"}]
    r = asset("sd_roof_bay", "Roof slab (6 m bay)", "Search and Destroy: a 6.6 m precast concrete roof slab on two steel beams, resting on 3 m compound walls to roof one 6 m bay (a connector room).", [6.6, 6.6], roof, [0.5, 0.5, 0.5])
    r["materials"] = roof_mats
    out.append(r)
    # SD2 Night Yard: a sodium yard light, a 6 m galvanised mast on a base plate with an arm and a lantern; its +z faces
    # the lane (the arm reaches 0.9 m out), the light warm orange, wide enough to pool on the asphalt
    mast = [{"t": "c", "c": [0, 3.0, 0], "s": [0.12, 6.0, 0.12], "m": "galv", "seg": 8},
            {"t": "b", "c": [0, 0.02, 0], "s": [0.4, 0.04, 0.4], "m": "galv"},
            {"t": "b", "c": [0, 5.95, 0.45], "s": [0.06, 0.06, 0.95], "m": "galv", "k": 0},
            {"t": "b", "c": [0, 5.85, 0.9], "s": [0.32, 0.14, 0.5], "m": "head", "k": 0},
            {"t": "b", "c": [0, 5.77, 0.9], "s": [0.26, 0.02, 0.42], "m": "glow", "k": 0, "lod": 1}]
    m = asset("sd_sodium_light", "Sodium yard light", "Search and Destroy: a 6 m galvanised mast with a sodium lantern on a short arm, lighting the lane in warm orange.", [0.5, 1.3], mast, [0.5, 0.5, 0.5])
    m["materials"] = [{"name": "galv", "albedo": [0.55, 0.57, 0.58], "roughness": 0.5, "pattern": "metal", "metallic": 0.6, "grain": 1, "texture": "steel_scratched"},
                      {"name": "head", "albedo": [0.25, 0.26, 0.27], "roughness": 0.6, "pattern": "metal", "metallic": 0.4, "grain": 1},
                      {"name": "glow", "albedo": [1.0, 0.72, 0.38], "roughness": 0.3, "pattern": "plain", "metallic": 0, "grain": 1, "emissive": 2.0}]
    m["light"] = {"offset": [0, 5.7, 0.9], "color": [1.0, 0.62, 0.3], "intensity": 8.0, "radius": 18.0, "use": "electric"}
    out.append(m)
    for a in out:
        with open(os.path.join(ROOT, "content", "assets", a["id"] + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(a, f, separators=(",", ":"))
    print("sd signs written:", len(out))


if __name__ == "__main__":
    main()
