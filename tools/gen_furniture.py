"""Furniture family for walls, interiors and furniture (docs/interiors.md): original parametric assets.

Writes content/assets/furn_*.json (category "Furniture"). Deterministic and safe to rerun; it touches no other file.
Pieces are B1-kit parts (docs/asset-format.md#b1-geometry-kit): chamfered boxes ("bv", what a small rounding radius
becomes), rounded boxes for soft shapes, cylinders / frustums for turned legs, casks, pots, lamps and plates, arches for
bail handles, crests and aprons, wedges for brackets (P1, version 2). Wood uses the "joinery" pattern with grain -1 (grain follows each part's longest axis: legs, rails, boards, stiles).
Colliders are 1-3 invisible boxes per item; cosmetic props have none. The "prop" block drives damage and placement.

Usage: python -I tools/gen_furniture.py [repo root]
"""
import json
import math
import pathlib
import sys

MATS = {
    "oak": ([0.2, 0.12, 0.065], 0.58, "joinery", -1),
    "oak_dark": ([0.12, 0.075, 0.042], 0.55, "joinery", -1),
    "cherry": ([0.25, 0.115, 0.06], 0.5, "joinery", -1),
    "pine": ([0.34, 0.23, 0.13], 0.68, "joinery", -1),
    "paint_green": ([0.19, 0.25, 0.19], 0.6, "paintwood", -1),
    "paint_blue": ([0.25, 0.31, 0.34], 0.6, "paintwood", -1),
    "paint_cream": ([0.58, 0.54, 0.44], 0.62, "paintwood", -1),
    "iron": ([0.05, 0.048, 0.046], 0.55, "metal", 1),
    "brass": ([0.62, 0.44, 0.18], 0.32, "metal", 1),
    "glass": ([0.8, 0.85, 0.82], 0.05, "glass", 1),
    "bottle_green": ([0.035, 0.08, 0.045], 0.12, "plain", 1),
    "bottle_brown": ([0.09, 0.05, 0.02], 0.12, "plain", 1),
    "faience": ([0.74, 0.72, 0.64], 0.2, "ceramic", 1),
    "faience_red": ([0.74, 0.71, 0.62], 0.2, "ceramic", 2),
    "faience_dots": ([0.72, 0.7, 0.62], 0.2, "ceramic", 3),
    "stoneware": ([0.42, 0.33, 0.22], 0.35, "ceramic", 0),
    "linen": ([0.7, 0.67, 0.6], 0.95, "textile", 0),
    "ticking": ([0.62, 0.6, 0.53], 0.95, "textile", 1),
    "check": ([0.5, 0.22, 0.16], 0.95, "textile", 2),
    "blanket": ([0.3, 0.085, 0.05], 0.97, "textile", 3),
    "curtain": ([0.52, 0.47, 0.36], 0.95, "textile", 2),
    "rush": ([0.42, 0.33, 0.16], 0.9, "fabric", 1),
    "paper": ([0.7, 0.66, 0.55], 0.9, "paper", 1),
    "cover_red": ([0.2, 0.06, 0.04], 0.7, "fabric", 1),
    "cover_blue": ([0.06, 0.09, 0.15], 0.7, "fabric", 1),
    "cover_green": ([0.09, 0.12, 0.07], 0.7, "fabric", 1),
    "print": ([0.3, 0.33, 0.25], 0.8, "fabric", 1),
    "ashlar": ([0.5, 0.47, 0.4], 0.8, "ashlar", 1),
    "hearth": ([0.28, 0.27, 0.24], 0.85, "ashlar", 1),
    "firebrick": ([0.17, 0.08, 0.05], 0.9, "brickwork", 1),
    "soot": ([0.025, 0.022, 0.02], 0.97, "plain", 1),
    # V4 (dense interiors): materials of the second furniture set
    "marble": ([0.62, 0.6, 0.56], 0.25, "plain", 1),
    "zinc": ([0.36, 0.37, 0.37], 0.45, "metal", 1),
    "copper": ([0.45, 0.22, 0.12], 0.35, "metal", 1),
    "cast_iron": ([0.06, 0.058, 0.056], 0.6, "metal", 1),
    "enamel": ([0.55, 0.53, 0.47], 0.3, "ceramic", 0),
    "wicker": ([0.45, 0.34, 0.18], 0.9, "fabric", 1),
    "sacking": ([0.46, 0.39, 0.27], 0.97, "fabric", 1),
    "hay": ([0.55, 0.46, 0.22], 0.97, "fabric", 1),
    "bark": ([0.2, 0.14, 0.09], 0.95, "joinery", -1),
    "log_end": ([0.42, 0.32, 0.2], 0.9, "joinery", -1),
    "upholstery": ([0.22, 0.24, 0.14], 0.9, "textile", 2),
    "coat_wool": ([0.15, 0.14, 0.12], 0.95, "textile", 0),
    "coat_blue": ([0.12, 0.15, 0.22], 0.95, "textile", 0),
    "mirror": ([0.72, 0.74, 0.74], 0.05, "metal", 1),
    "jar_glass": ([0.75, 0.82, 0.78], 0.05, "glass", 1),
    "preserve_red": ([0.32, 0.05, 0.04], 0.4, "plain", 1),
    "preserve_amber": ([0.45, 0.25, 0.05], 0.4, "plain", 1),
    "preserve_green": ([0.22, 0.28, 0.08], 0.4, "plain", 1),
    "apple": ([0.42, 0.08, 0.05], 0.55, "plain", 1),
    "wax": ([0.8, 0.77, 0.66], 0.6, "plain", 1),
    "slate_black": ([0.07, 0.075, 0.08], 0.7, "plain", 1),
    "dial": ([0.78, 0.75, 0.66], 0.5, "paper", 1),
    # L3 lights (docs/lights.md): lamp glass, flames and embers carry "emissive" (EMISSIVE below)
    "lamp_glass": ([0.82, 0.8, 0.72], 0.05, "glass", 1),
    "bulb": ([0.9, 0.86, 0.74], 0.1, "plain", 1),
    "flame": ([0.95, 0.62, 0.22], 0.9, "plain", 1),
    "ember": ([0.32, 0.08, 0.02], 0.95, "plain", 1),
    "ash": ([0.2, 0.19, 0.18], 0.98, "plain", 1),
    "enamel_green": ([0.12, 0.2, 0.14], 0.3, "ceramic", 0),
    "enamel_white": ([0.68, 0.67, 0.62], 0.3, "ceramic", 0),
    "shade_silk": ([0.62, 0.5, 0.32], 0.85, "textile", 0),
    "porcelain": ([0.76, 0.74, 0.68], 0.25, "ceramic", 0),
}
# L3: linear HDR emissive multipliers (the lighting engine, L2, scales them by the light's on / time-of-day state)
# P1: each piece's furnishing envelope (placement, keep-clear, ceiling spots, culling) as its version 1 parts gave it,
# written as prop.bounds so a new look never changes where auto-furnish puts things (src/interior.cpp PropDef)
ENVELOPES = json.loads((pathlib.Path(__file__).resolve().parent / "prop_envelopes.json").read_text(encoding="utf-8"))
EMISSIVE = {
    "lamp_glass": [1.6, 1.15, 0.62],
    "bulb": [6.0, 4.6, 2.9],
    "flame": [7.0, 3.4, 0.9],
    "ember": [3.2, 0.9, 0.18],
}


class A:
    def __init__(self, id, name, desc, family, rounds, blast, blocking=True, wall=False, front=0.6, mount=0.0):
        self.id, self.name, self.desc = id, name, desc
        self.parts, self.groups, self.used = [], [], set()
        self.prop = {"family": family, "rounds": rounds, "blast": blast, "blocking": blocking, "wall": wall, "front": front}
        if mount:
            self.prop["mount"] = mount
        self.cols = []
        self.extra_mats = {}
        self.light = None   # L3: "light" block (dict or list of dicts), docs/lights.md

    def g(self, name):
        if name not in self.groups:
            self.groups.append(name)
        return self.groups.index(name)

    def r(self, c, s, m, grp, rad=0.006, rot=None):
        rad = min(rad, 0.5 * min(s) - 1e-4)
        p = {"t": "r" if rad > 0.0015 else "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "k": 0, "g": self.g(grp)}
        if p["t"] == "r" and rad <= 0.012:   # P1: small rounds become a mesh chamfer (smooth normals, ~108 tris instead of ~300)
            p["t"], p["bv"] = "b", round(rad, 4)
        elif p["t"] == "r":
            p["rad"] = round(rad, 4)
        if rot:
            p["r"] = [round(v, 3) for v in rot]
        self.parts.append(p)
        self.used.add(m)
        return p

    def cyl(self, c, d, h, m, grp, top=1.0, dz=None, n=0, seg=0, rot=None, t="c"):
        """P1 (B1 kit): cylinder / frustum along local y (d across x, dz across z; top = top / base section).
        rot [180, 0, 0] puts the base on top (a frustum widening upward); [90, 0, 0] lays it along z."""
        p = {"t": t, "c": [round(v, 4) for v in c], "s": [round(d, 4), round(h, 4), round(d if dz is None else dz, 4)], "m": m, "k": 0, "g": self.g(grp)}
        if n:
            p["n"] = n
        if seg:
            p["seg"] = seg
        if t == "k" or top != 1.0:
            p["top"] = round(top, 4)
        if rot:
            p["r"] = [round(v, 3) for v in rot]
        self.parts.append(p)
        self.used.add(m)
        return p

    def arch(self, c, s, m, grp, th, rot=None, fill=False):
        """P1: arch ring (bail handles, rope loops) or, with th past both radii, a solid half-disc; extruded along local z."""
        p = {"t": "a", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "th": round(th, 4), "k": 0, "g": self.g(grp)}
        if fill:
            p["fill"] = 1
        if rot:
            p["r"] = [round(v, 3) for v in rot]
        self.parts.append(p)
        self.used.add(m)
        return p

    def wedge(self, c, s, m, grp, top=0.0, rot=None):
        p = {"t": "w", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "k": 0, "g": self.g(grp)}
        if top:
            p["top"] = round(top, 4)
        if rot:
            p["r"] = [round(v, 3) for v in rot]
        self.parts.append(p)
        self.used.add(m)
        return p

    def b(self, c, s, m, grp, rot=None):
        return self.r(c, s, m, grp, 0.0, rot)

    def col(self, c, s):
        self.cols.append({"t": "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": "oak", "v": 0})

    def plate(self, c, d, t, m, grp, axis="y", tilt=None):
        """Round plate: one shallow frustum (the rim slopes in to the foot), 16 sides."""
        if axis == "y":
            self.cyl(c, d, t, m, grp, top=0.86, seg=16, rot=[180 + (tilt or [0, 0, 0])[0], (tilt or [0, 0, 0])[1], (tilt or [0, 0, 0])[2]])
        else:   # standing in a rack, face toward +z
            rx = (tilt or [0, 0, 0])[0]
            self.cyl(c, d, t, m, grp, top=0.86, seg=16, rot=[rx - 90, 0, 0])

    def write(self, root, footprint, fragments=(), keep=(), glass=None, surfaces=(), mats=None):
        materials = []
        for n in sorted(self.used | set(self.extra_mats)):
            if n in self.extra_mats:
                alb, rough, pat, grain = self.extra_mats[n]
            else:
                alb, rough, pat, grain = MATS[n]
            materials.append({"name": n, "albedo": alb, "roughness": rough, "pattern": pat, "metallic": 1.0 if n in ("brass", "copper", "mirror") else (0.8 if n == "zinc" else (0.15 if n in ("iron", "cast_iron") else 0)), "grain": grain})
            if n in EMISSIVE:
                materials[-1]["emissive"] = EMISSIVE[n]
        if not any(m["name"] == "oak" for m in materials) and self.cols:
            alb, rough, pat, grain = MATS["oak"]
            materials.append({"name": "oak", "albedo": alb, "roughness": rough, "pattern": pat, "metallic": 0, "grain": grain})
        self.prop["groups"] = self.groups
        self.prop["fragments"] = [self.groups.index(g) for g in fragments]
        self.prop["keep"] = [self.groups.index(g) for g in keep]
        if glass:
            self.prop["glass"] = self.groups.index(glass)
        self.prop["surfaces"] = [[round(v, 3) for v in s] for s in surfaces]
        if self.id in ENVELOPES:
            self.prop["bounds"] = ENVELOPES[self.id]
        doc = {"id": self.id, "version": 2, "name": self.name, "category": "Furniture", "description": self.desc,
               "floors": 0, "enterable": False, "footprint": [round(footprint[0], 3), round(footprint[1], 3)],
               "plinthDepth": 0, "maxGrade": 0.3, "noAutoPanels": True, "materials": materials,
               "parts": self.parts + self.cols, "prop": self.prop}
        if self.light is not None:
            doc["light"] = light_block(self, self.light)
        out = root / "content" / "assets" / (self.id + ".json")
        out.write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")), encoding="utf-8", newline="")
        return out


# ---------------------------------------------------------------- builders
def turned_leg(a, x, z, h, d, m, grp, block=0.0, y0=0.0, foot=True, taper=0.62):
    """P1: lathe-turned leg from y0 to y0 + h: a square block at the top (where the rails are tenoned in), a bead ring,
    a long taper, an ankle ring and a turned foot."""
    y = y0 + h
    if block > 0:
        a.r([x, y - block / 2, z], [d, block, d], m, grp, 0.004)
        y -= block
    a.cyl([x, y - 0.011, z], d * 0.98, 0.022, m, grp, seg=12)               # bead under the block
    y -= 0.022
    fh = 0.05 if foot else 0.0
    L = y - y0 - fh - 0.016
    a.cyl([x, y - L / 2, z], d * 0.84, L, m, grp, top=taper, seg=12, rot=[180, 0, 0])   # taper, narrowing downward
    y -= L
    a.cyl([x, y - 0.008, z], d * 0.66, 0.016, m, grp, seg=12)               # ankle ring
    if foot:
        a.cyl([x, y0 + fh / 2, z], d * 0.62, fh, m, grp, top=0.78, seg=12, rot=[180, 0, 0])


def leg_tapered(a, x, z, h, top, bot, m, grp):
    """Table leg: turned below a square block at apron height (was two stacked square sections)."""
    turned_leg(a, x, z, h, top, m, grp, block=0.13)


def dining_table(root, id, name, W, D, boards, wood):
    H = 0.76
    a = A(id, name, f"Farmhouse dining table, {W:.2f} x {D:.2f} m: {boards} separate top boards on an apron, square tapered legs. "
          "Breaks into a few boards and legs.", "wood", 22, 45)
    t = 0.035
    bw = D / boards
    for i in range(boards):
        z = -D / 2 + bw * (i + 0.5)
        a.r([0, H - t / 2, z], [W, t, bw - 0.004], wood, f"top{i}", 0.006)
    ah = 0.11
    a.r([0, H - t - ah / 2, D / 2 - 0.09], [W - 0.2, ah, 0.024], wood, "apron", 0.004)
    a.r([0, H - t - ah / 2, -D / 2 + 0.09], [W - 0.2, ah, 0.024], wood, "apron", 0.004)
    a.r([W / 2 - 0.1, H - t - ah / 2, 0], [0.024, ah, D - 0.2], wood, "apron", 0.004)
    a.r([-W / 2 + 0.1, H - t - ah / 2, 0], [0.024, ah, D - 0.2], wood, "apron", 0.004)
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        leg_tapered(a, sx * (W / 2 - 0.1), sz * (D / 2 - 0.09), H - t, 0.07, 0.05, wood, f"leg{i}")
    a.r([0, 0.12, 0], [W - 0.24, 0.05, 0.04], wood, "stretcher", 0.004)   # central stretcher
    a.col([0, H / 2, 0], [W, H, D])
    frags = ["top0", f"top{boards - 1}", "leg0", "apron"]
    return a.write(root, [W, D], fragments=frags, keep=["leg2", "leg3"], surfaces=[[-W / 2 + 0.08, -D / 2 + 0.08, W / 2 - 0.08, D / 2 - 0.08, H]])


def side_table(root):
    a = A("furn_side_table", "Side table with drawer", "Small cherry side table with a drawer and turned-look stepped legs.", "wood", 12, 30)
    W, D, H = 0.55, 0.42, 0.72
    a.r([0, H - 0.0125, 0], [W, 0.025, D], "cherry", "top", 0.006)
    a.r([0, H - 0.07, D / 2 - 0.04], [W - 0.1, 0.09, 0.02], "cherry", "drawer", 0.004)
    a.r([0, H - 0.07, D / 2 - 0.028], [0.03, 0.02, 0.012], "brass", "drawer", 0.004)
    a.r([0, H - 0.07, -D / 2 + 0.04], [W - 0.1, 0.09, 0.02], "cherry", "apron", 0.004)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.05), H - 0.07, 0], [0.02, 0.09, D - 0.1], "cherry", "apron", 0.004)
    a.r([0, H - 0.0285, 0], [W - 0.03, 0.008, D - 0.03], "cherry", "top", 0.003)   # moulded edge under the top
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        x, z = sx * (W / 2 - 0.05), sz * (D / 2 - 0.05)
        turned_leg(a, x, z, H - 0.025, 0.042, "cherry", f"leg{i}", block=0.1)
    a.r([0, 0.16, 0], [W - 0.12, 0.016, D - 0.12], "cherry", "apron", 0.003)   # undershelf
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "drawer", "leg0"], keep=["leg2", "leg3"], surfaces=[[-W / 2 + 0.04, -D / 2 + 0.04, W / 2 - 0.04, D / 2 - 0.04, H]])


def chair(root, id, name, ladder, wood):
    a = A(id, name, ("Ladder-back chair with a woven rush seat." if ladder else "Slat-back chair with a plank seat.") +
          " Thin square legs, rails and stretchers; the back and seat come off on a blast.", "wood", 8, 20, front=0.45)
    S, H = 0.44, 0.45   # seat size, seat height
    bh = 0.95
    for sx in (-1, 1):   # back legs rise into the back posts, raked slightly; turned front legs; finials on the posts
        a.r([sx * (S / 2 - 0.02), bh / 2, -S / 2 + 0.02], [0.04, bh, 0.04], wood, "back", 0.005, rot=[-4, 0, 0])
        turned_leg(a, sx * (S / 2 - 0.02), S / 2 - 0.02, H, 0.048, wood, "legs", block=0.06, foot=False, taper=0.85)
        fz = -S / 2 + 0.02 - math.sin(math.radians(4)) * bh / 2 - 0.002
        a.cyl([sx * (S / 2 - 0.02), bh + 0.004, fz], 0.03, 0.014, wood, "back", seg=10)                  # finial collar
        a.r([sx * (S / 2 - 0.02), bh + 0.026, fz], [0.034, 0.034, 0.034], wood, "back", 0.016)           # ball finial
    if ladder:
        for i in range(3):
            y = H + 0.17 + i * 0.13
            a.r([0, y, -S / 2 + 0.005 - 0.012 * (i + 1)], [S - 0.04, 0.065, 0.018], wood, "back", 0.005, rot=[-4, 0, 0])
        a.r([0, H - 0.012, 0.01], [S - 0.03, 0.03, S - 0.02], "rush", "seat", 0.008)
    else:
        a.r([0, H + 0.4, -S / 2 + 0.0], [S - 0.04, 0.06, 0.02], wood, "back", 0.005, rot=[-4, 0, 0])
        for i in range(4):
            x = -0.12 + i * 0.08
            a.r([x, H + 0.22, -S / 2 + 0.012], [0.045, 0.32, 0.014], wood, "back", 0.004, rot=[-4, 0, 0])
        a.r([0, H - 0.012, 0.01], [S, 0.026, S - 0.01], wood, "seat", 0.006)
    for sz in (-1, 1):   # seat rails and turned rungs (stretchers)
        a.r([0, H - 0.045, sz * (S / 2 - 0.02)], [S - 0.05, 0.035, 0.02], wood, "seat", 0.004)
        a.cyl([0, 0.16 + 0.04 * (sz > 0), sz * (S / 2 - 0.02)], 0.02, S - 0.03, wood, "legs", seg=8, rot=[0, 0, 90])
    for sx in (-1, 1):
        a.r([sx * (S / 2 - 0.02), H - 0.045, 0], [0.02, 0.035, S - 0.05], wood, "seat", 0.004)
        a.cyl([sx * (S / 2 - 0.02), 0.2, 0], 0.018, S - 0.03, wood, "legs", seg=8, rot=[90, 0, 0])
    a.col([0, H / 2, 0.0], [S, H, S])
    a.col([0, (H + bh) / 2, -S / 2 + 0.02], [S, bh - H, 0.06])
    return a.write(root, [S, S], fragments=["back", "seat"], keep=[])


def stool(root):
    a = A("furn_stool", "Milking stool", "Low three-legged stool: thick round pine seat, splayed round legs wedged through it.", "wood", 6, 16, front=0.3)
    a.cyl([0, 0.32, 0], 0.32, 0.04, "pine", "seat", top=0.94, seg=20)
    for i in range(3):
        ang = math.radians(90 + 120 * i)
        x, z = math.cos(ang) * 0.1, math.sin(ang) * 0.1
        a.cyl([x, 0.15, z], 0.036, 0.32, "pine", f"leg{i}", top=0.8, seg=10, rot=[-80 * z, 0, 80 * x])   # splayed outward, tapering
        a.cyl([x * 0.8, 0.342, z * 0.8], 0.03, 0.006, "oak_dark", f"leg{i}", seg=8)                    # wedged tenon end in the seat
    a.col([0, 0.17, 0], [0.32, 0.34, 0.32])
    return a.write(root, [0.34, 0.34], fragments=["seat", "leg0"], keep=[], surfaces=[[-0.12, -0.12, 0.12, 0.12, 0.34]])


def drawer_front(a, cx, cy, z, w, h, m, grp):
    """P1: drawer front with a raised field (fielded panel) 8 mm proud of the front board."""
    a.r([cx, cy, z], [w, h, 0.022], m, grp, 0.005)
    a.r([cx, cy, z + 0.008], [w - 0.05, h - 0.045, 0.012], m, grp, 0.006)


def bail(a, x, y, z, grp, w=0.07, m="brass"):
    """P1: brass bail pull on the face at depth z: two round rosettes and a hanging half-ring handle."""
    for sx in (-1, 1):
        a.cyl([x + sx * w / 2, y, z + 0.003], 0.022, 0.006, m, grp, seg=10, rot=[90, 0, 0])
    a.arch([x, y - 0.014, z + 0.009], [w, 0.028, 0.006], m, grp, th=0.006, rot=[180, 0, 0])


def knob(a, x, y, z, grp, d=0.026, m="brass"):
    """P1: turned knob on the face at depth z: a short neck and a flattened ball."""
    a.cyl([x, y, z + 0.006], d * 0.55, 0.012, m, grp, seg=8, rot=[90, 0, 0])
    a.r([x, y, z + 0.018], [d, d, d * 0.7], m, grp, d * 0.34)


def framed_door(a, cx, cy, z, w, h, wood, grp, panel=True, glazed=False):
    """Frame-and-panel door in the xz plane at depth z (front face toward +z)."""
    st = 0.055
    a.r([cx - w / 2 + st / 2, cy, z], [st, h, 0.022], wood, grp, 0.004)
    a.r([cx + w / 2 - st / 2, cy, z], [st, h, 0.022], wood, grp, 0.004)
    a.r([cx, cy + h / 2 - st / 2, z], [w - 2 * st, st, 0.022], wood, grp, 0.004)
    a.r([cx, cy - h / 2 + st / 2, z], [w - 2 * st, st, 0.022], wood, grp, 0.004)
    if glazed:
        a.b([cx, cy, z - 0.002], [w - 2 * st + 0.01, h - 2 * st + 0.01, 0.004], "glass", "glass")
        a.r([cx, cy, z], [0.018, h - 2 * st, 0.016], wood, grp, 0.003)   # glazing bar
    elif panel:
        a.r([cx, cy, z - 0.004], [w - 2 * st + 0.01, h - 2 * st + 0.01, 0.014], wood, grp, 0.004)
        a.r([cx, cy, z + 0.002], [w - 2 * st - 0.05, h - 2 * st - 0.05, 0.012], wood, grp, 0.01)   # raised field


def hutch(root, glazed):
    id = "furn_hutch_glazed" if glazed else "furn_hutch"
    a = A(id, "Glazed hutch" if glazed else "Vaisselier (hutch)",
          "Norman vaisselier: lower cupboard with framed doors and two drawers, upper " +
          ("cabinet with glazed doors" if glazed else "open plate rack") + " holding faience on its shelves. Glass and crockery break first.",
          "wood", 26, 55, wall=True, front=0.8)
    W, D, H1 = 1.3, 0.5, 0.9
    wood = "oak"
    a.r([0, 0.05, 0], [W - 0.04, 0.1, D - 0.04], wood, "carcass", 0.006)   # plinth
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.015), H1 / 2 + 0.02, 0], [0.03, H1 - 0.04, D], wood, "carcass", 0.006)
    a.r([0, H1 - 0.015, 0.01], [W + 0.04, 0.03, D + 0.03], wood, "carcass", 0.008)   # worktop
    a.r([0, H1 / 2, -D / 2 + 0.01], [W - 0.02, H1 - 0.04, 0.02], wood, "carcass", 0.004)
    for i, sx in enumerate((-1, 1)):   # drawers
        drawer_front(a, sx * W / 4, H1 - 0.1, D / 2 - 0.01, W / 2 - 0.05, 0.12, wood, f"drawer{i}")
        bail(a, sx * W / 4, H1 - 0.09, D / 2 + 0.007, f"drawer{i}")
    for i, sx in enumerate((-1, 1)):
        framed_door(a, sx * W / 4, 0.42, D / 2 - 0.01, W / 2 - 0.05, 0.56, wood, f"door{i}")
        a.r([sx * 0.04, 0.48, D / 2 + 0.003], [0.016, 0.06, 0.004], "brass", f"door{i}", 0.002)   # escutcheon plate
        knob(a, sx * 0.04, 0.44, D / 2 + 0.001, f"door{i}", 0.022)
    a.r([0, 0.1, 0.006], [W - 0.03, 0.02, D - 0.035], wood, "carcass", 0.006)   # plinth moulding
    # upper part
    UD, UH, y0 = 0.28, 1.0, H1
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.035), y0 + UH / 2, -D / 2 + UD / 2], [0.03, UH, UD], wood, "upper", 0.006)
    a.r([0, y0 + UH / 2, -D / 2 + 0.01], [W - 0.08, UH, 0.016], wood, "upper", 0.003)
    a.r([0, y0 + UH + 0.03, -D / 2 + UD / 2 + 0.02], [W - 0.02, 0.06, UD + 0.05], wood, "upper", 0.01)   # cornice
    a.r([0, y0 + UH - 0.01, -D / 2 + UD / 2 + 0.03], [W - 0.04, 0.035, UD + 0.03], wood, "upper", 0.008)
    if not glazed:   # shaped apron under the cornice: a flat arch cut from a board
        a.arch([0, y0 + UH - 0.075, -D / 2 + UD - 0.012], [W - 0.1, 0.09, 0.02], wood, "upper", th=0.03, fill=True)
    shelves = [y0 + 0.36, y0 + 0.7]
    for y in shelves:
        a.r([0, y, -D / 2 + UD / 2], [W - 0.1, 0.022, UD - 0.02], wood, "upper", 0.004)
        a.r([0, y + 0.04, -D / 2 + UD - 0.03], [W - 0.1, 0.014, 0.012], wood, "upper", 0.003)   # plate rail
    k = 0
    for y in [y0 + 0.02] + shelves:   # plates standing on the shelves, a few bowls
        for i in range(5):
            x = -W / 2 + 0.17 + i * 0.24
            m = ["faience", "faience_dots", "faience_red"][(i + k) % 3]
            a.plate([x, y + 0.13, -D / 2 + UD - 0.06], 0.22, 0.012, m, "plates", axis="z", tilt=[-12, 0, 0])
        k += 1
    if glazed:
        for i, sx in enumerate((-1, 1)):
            framed_door(a, sx * (W / 2 - 0.05) / 2, y0 + UH / 2, -D / 2 + UD + 0.005, (W - 0.1) / 2 - 0.01, UH - 0.08, wood, f"gdoor{i}", glazed=True)
    a.col([0, H1 / 2, 0], [W, H1, D])
    a.col([0, y0 + UH / 2, -D / 2 + UD / 2], [W, UH + 0.06, UD + 0.02])
    frags = ["door0", "drawer1", "plates"] + (["gdoor0"] if glazed else [])
    return a.write(root, [W + 0.04, D + 0.03], fragments=frags[:4], keep=["carcass", "upper"], glass="glass" if glazed else None,
                   surfaces=[[-W / 2 + 0.05, D / 2 - 0.2, W / 2 - 0.05, D / 2 - 0.02, H1]])


def armoire(root):
    a = A("furn_armoire", "Armoire normande", "Tall wedding wardrobe: two panelled doors with raised fields, moulded cornice, "
          "brass escutcheons, short feet. Doors tear off on a blast; the carcass stays.", "wood", 30, 70, wall=True, front=0.8)
    W, D, H = 1.3, 0.6, 2.15
    wood = "cherry"
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.02), H / 2 + 0.06, 0], [0.04, H - 0.12, D], wood, "carcass", 0.008)
        for sz in (-1, 1):   # turned bun feet
            a.cyl([sx * (W / 2 - 0.05), 0.045, sz * (D / 2 - 0.05)], 0.085, 0.05, wood, "carcass", top=0.7, seg=14)
            a.cyl([sx * (W / 2 - 0.05), 0.01, sz * (D / 2 - 0.05)], 0.085, 0.02, wood, "carcass", top=0.75, seg=14, rot=[180, 0, 0])
            a.cyl([sx * (W / 2 - 0.05), 0.075, sz * (D / 2 - 0.05)], 0.06, 0.012, wood, "carcass", seg=14)
    a.r([0, 0.11, 0], [W, 0.06, D], wood, "carcass", 0.006)
    a.r([0, H / 2 + 0.06, -D / 2 + 0.01], [W - 0.04, H - 0.12, 0.02], wood, "carcass", 0.004)
    a.r([0, H - 0.04, 0.01], [W + 0.06, 0.06, D + 0.05], wood, "cornice", 0.012)
    a.r([0, H - 0.09, 0.005], [W + 0.02, 0.04, D + 0.02], wood, "cornice", 0.01)
    a.r([0, H + 0.0, 0.0], [W + 0.1, 0.03, D + 0.07], wood, "cornice", 0.012)
    for i, sx in enumerate((-1, 1)):
        cx = sx * (W / 4 - 0.005)
        framed_door(a, cx, 1.17, D / 2 - 0.01, W / 2 - 0.05, 1.08, wood, f"door{i}")
        framed_door(a, cx, 0.42, D / 2 - 0.01, W / 2 - 0.05, 0.42, wood, f"door{i}")
        a.r([sx * 0.03, 1.0, D / 2 + 0.008], [0.03, 0.09, 0.008], "brass", f"door{i}", 0.003)
        knob(a, sx * 0.03, 0.93, D / 2 + 0.004, f"door{i}", 0.022)
        for hy in (0.5, 1.55):   # iron hinge knuckles on the outer stiles
            a.cyl([sx * (W / 2 - 0.045), hy, D / 2 + 0.006], 0.014, 0.07, "iron", "carcass", seg=8)
    a.arch([0, H - 0.17, D / 2 - 0.004], [W - 0.12, 0.08, 0.02], wood, "cornice", th=0.025, fill=True)   # arched frieze under the cornice
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.1, D + 0.07], fragments=["door0", "door1", "cornice"], keep=["carcass"])


def commode(root):
    a = A("furn_commode", "Commode (chest of drawers)", "Three-drawer chest in painted pine: drawer fronts with brass pulls, top, plinth feet.",
          "painted", 16, 40, wall=True, front=0.7)
    W, D, H = 1.05, 0.5, 0.85
    w = "paint_blue"
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.015), H / 2, 0], [0.03, H - 0.03, D], w, "carcass", 0.006)
    a.r([0, 0.05, 0], [W - 0.03, 0.1, D - 0.02], w, "carcass", 0.006)
    a.r([0, H / 2, -D / 2 + 0.01], [W - 0.03, H - 0.06, 0.02], w, "carcass", 0.004)
    a.r([0, H - 0.015, 0.01], [W + 0.04, 0.03, D + 0.03], "oak", "top", 0.008)
    for i in range(3):
        y = 0.22 + i * 0.21
        drawer_front(a, 0, y, D / 2 - 0.01, W - 0.07, 0.18, w, f"drawer{i}")
        for sx in (-1, 1):
            bail(a, sx * 0.25, y + 0.025, D / 2 + 0.007, f"drawer{i}")
        a.cyl([0, y + 0.02, D / 2 + 0.008], 0.018, 0.004, "brass", f"drawer{i}", seg=8, rot=[90, 0, 0])   # keyhole escutcheon
    a.r([0, 0.1, 0.006], [W + 0.005, 0.02, D - 0.005], w, "carcass", 0.006)   # plinth moulding
    a.r([0, H - 0.04, 0.0], [W + 0.005, 0.02, D + 0.005], w, "carcass", 0.006)   # moulding under the top
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.04, D + 0.03], fragments=["top", "drawer2", "drawer0"], keep=["carcass", "drawer1"],
                   surfaces=[[-W / 2 + 0.04, -D / 2 + 0.05, W / 2 - 0.04, D / 2 - 0.03, H]])


def blanket_chest(root):
    a = A("furn_chest", "Blanket chest", "Oak coffer on bracket feet, iron straps and hasp; lid boards.", "wood", 18, 45, wall=True, front=0.6)
    W, D, H = 1.0, 0.5, 0.55
    a.r([0, H / 2 + 0.03, 0], [W, H - 0.1, D], "oak_dark", "body", 0.008)
    a.r([0, H - 0.025, 0.005], [W + 0.03, 0.04, D + 0.03], "oak_dark", "lid", 0.01)
    for sx in (-1, 1):
        a.r([sx * 0.3, H / 2 + 0.05, D / 2 + 0.003], [0.035, H - 0.06, 0.008], "iron", "body", 0.003)
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.05), 0.04, sz * (D / 2 - 0.05)], [0.08, 0.08, 0.08], "oak_dark", "body", 0.01)
    a.r([0, H - 0.08, D / 2 + 0.008], [0.05, 0.08, 0.01], "iron", "lid", 0.003)
    for sx in (-1, 1):   # the straps carry on over the lid and its front lip; nail heads; ring handles on the ends
        a.r([sx * 0.3, H - 0.003, 0.005], [0.035, 0.006, D + 0.034], "iron", "lid", 0.002)
        a.r([sx * 0.3, H - 0.03, D / 2 + 0.019], [0.035, 0.05, 0.006], "iron", "lid", 0.002)
        for ny in (0.12, 0.25, 0.38):
            a.cyl([sx * 0.3, ny, D / 2 + 0.008], 0.012, 0.004, "iron", "body", seg=6, rot=[90, 0, 0])
        a.cyl([sx * (W / 2 + 0.004), H * 0.62, 0], 0.04, 0.008, "iron", "body", seg=10, rot=[0, 0, 90])
        a.arch([sx * (W / 2 + 0.012), H * 0.62 - 0.05, 0], [0.09, 0.06, 0.012], "iron", "body", th=0.01, rot=[180, 90, 0])
    a.r([0, H - 0.115, D / 2 + 0.014], [0.06, 0.06, 0.006], "iron", "body", 0.002)   # lock plate under the hasp
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.03, D + 0.03], fragments=["lid"], keep=["body"], surfaces=[[-W / 2 + 0.05, -D / 2 + 0.05, W / 2 - 0.05, D / 2 - 0.05, H]])


def bed(root):
    a = A("furn_bed", "Bed", "Period bed: turned-look posts, panelled head- and footboard, ticking mattress, linen sheet and bolster, "
          "folded red wool blanket.", "wood", 24, 55, wall=True, front=0.6)
    W, L = 1.4, 2.0
    for sx in (-1, 1):   # turned posts: square where the boards and rails are tenoned in, turned above and below, ball finials
        x = sx * (W / 2 - 0.03)
        for z, ph, grp in ((-L / 2 + 0.03, 1.1, "head"), (L / 2 - 0.03, 0.76, "foot")):
            a.cyl([x, 0.06, z], 0.055, 0.12, "oak", grp, top=0.75, seg=12, rot=[180, 0, 0])            # turned foot
            sq = ph - 0.2 if grp == "head" else ph - 0.14
            a.r([x, 0.12 + sq / 2, z], [0.06, sq, 0.06], "oak", grp, 0.008)                             # square section
            a.cyl([x, 0.12 + sq + 0.012, z], 0.064, 0.024, "oak", grp, seg=12)                         # collar
            top = 0.12 + sq + 0.024
            a.cyl([x, (top + ph) / 2, z], 0.05, ph - top, "oak", grp, top=0.7, seg=12)                 # turned top
            a.r([x, ph + 0.03, z], [0.06, 0.06, 0.06], "oak", grp, 0.029)                              # ball finial
        a.r([x, 0.3, 0], [0.04, 0.16, L - 0.1], "oak", "frame", 0.006)
    framed_door(a, 0, 0.72, -L / 2 + 0.03, W - 0.1, 0.62, "oak", "head")
    framed_door(a, 0, 0.5, L / 2 - 0.03, W - 0.1, 0.4, "oak", "foot")
    a.r([0, 1.06, -L / 2 + 0.03], [W - 0.1, 0.05, 0.07], "oak", "head", 0.012)                      # crest rail between the posts
    a.arch([0, 1.085, -L / 2 + 0.03], [W - 0.4, 0.1, 0.04], "oak", "head", th=0.04, fill=False)      # arched crest
    a.r([0, 0.5, 0.0], [W - 0.1, 0.2, L - 0.12], "ticking", "mattress", 0.06)
    a.r([0, 0.6, 0.12], [W - 0.08, 0.025, L - 0.38], "linen", "mattress", 0.012)
    a.r([0, 0.66, -L / 2 + 0.2], [W - 0.18, 0.14, 0.26], "linen", "mattress", 0.06)
    a.r([0, 0.64, L / 2 - 0.36], [W - 0.2, 0.06, 0.42], "blanket", "blanket", 0.02)
    a.r([0, 0.61, L / 2 - 0.36], [W - 0.12, 0.03, 0.46], "blanket", "blanket", 0.012)
    a.col([0, 0.3, 0], [W, 0.6, L])
    a.col([0, 0.85, -L / 2 + 0.03], [W, 0.5, 0.08])
    return a.write(root, [W, L], fragments=["foot", "blanket"], keep=["frame", "mattress", "head"])


def etagere(root):
    a = A("furn_shelf", "Shelf unit", "Standing pine shelf unit: uprights with notched supports, four shelves of varied lengths, "
          "back rails.", "wood", 14, 35, wall=True, front=0.6)
    W, D, H = 1.0, 0.32, 1.6
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.025), H / 2, sz * (D / 2 - 0.025)], [0.045, H, 0.045], "pine", "frame", 0.006)
        a.r([sx * (W / 2 - 0.025), 0.12, 0], [0.03, 0.04, D - 0.05], "pine", "frame", 0.004)
    ys = [0.12, 0.52, 0.92, 1.32]
    for i, y in enumerate(ys):
        lw = W - (0.0 if i % 2 == 0 else 0.06)
        for j, (z, d) in enumerate([(-0.065, 0.13), (0.07, 0.14)]):   # two boards per shelf, butt-jointed
            a.r([0 + (0.015 if j else -0.01), y + 0.03, z], [lw - 0.02, 0.024, d - 0.004], "pine", f"shelf{i}", 0.004)
    for y in (0.7, 1.5):
        a.r([0, y, -D / 2 + 0.01], [W - 0.06, 0.06, 0.02], "pine", "frame", 0.004)
    a.col([0, H / 2, 0], [W, H, D])
    surf = [[-W / 2 + 0.06, -D / 2 + 0.04, W / 2 - 0.06, D / 2 - 0.03, y + 0.042] for y in ys[1:]]
    return a.write(root, [W, D], fragments=["shelf3", "shelf2"], keep=["frame", "shelf0", "shelf1"], surfaces=surf)


def bench(root):
    a = A("furn_bench", "Bench", "Kitchen bench: one thick seat plank on two trestle legs with a through stretcher and wedges.", "wood", 12, 30, front=0.45)
    W = 1.4
    a.r([0, 0.43, 0], [W, 0.045, 0.3], "oak", "seat", 0.008)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.15), 0.2, 0], [0.045, 0.4, 0.26], "oak", f"trestle{(sx + 1) // 2}", 0.006)
        a.r([sx * (W / 2 - 0.15), 0.03, 0], [0.07, 0.06, 0.3], "oak", f"trestle{(sx + 1) // 2}", 0.006)
        a.r([sx * (W / 2 - 0.08), 0.18, 0], [0.04, 0.06, 0.03], "oak", "stretcher", 0.004)
    a.r([0, 0.18, 0], [W - 0.2, 0.05, 0.035], "oak", "stretcher", 0.005)
    a.col([0, 0.225, 0], [W, 0.45, 0.3])
    return a.write(root, [W, 0.3], fragments=["seat", "trestle0"], keep=["trestle1"], surfaces=[[-W / 2 + 0.05, -0.1, W / 2 - 0.05, 0.1, 0.453]])


def fireplace(root):
    a = A("furn_fireplace", "Fireplace surround", "Stone fireplace: dressed jambs, mantel shelf, hearth slab, sooted firebox with "
          "a firebrick back and iron fire-dogs. Masonry: blasts chip it and can bring down the mantel; it does not explode like a wooden prop.",
          "masonry", 0, 400, wall=True, front=0.9)
    W, D, H = 1.6, 0.5, 1.25
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.13), H / 2, 0], [0.26, H - 0.06, D], "ashlar", "jambs", 0.012)
    a.r([0, H - 0.16, 0], [W - 0.5, 0.26, D], "ashlar", "jambs", 0.012)       # lintel
    a.r([0, H + 0.03, 0.03], [W + 0.12, 0.08, D + 0.1], "oak_dark", "mantel", 0.012)
    a.r([0, 0.03, 0.2], [W + 0.2, 0.06, D + 0.4], "hearth", "hearth", 0.01)
    a.r([0, (H - 0.3) / 2 + 0.06, -D / 2 + 0.06], [W - 0.52, H - 0.3, 0.1], "firebrick", "jambs", 0.004)
    a.b([0, 0.062, -0.02], [W - 0.54, 0.004, D - 0.12], "soot", "jambs")
    a.b([0, (H - 0.3) / 2 + 0.06, -D / 2 + 0.115], [W - 0.54, H - 0.32, 0.004], "soot", "jambs")
    for sx in (-1, 1):   # fire-dogs: a turned upright with a ball top, the log bar running back, splayed feet
        a.cyl([sx * 0.25, 0.14, 0.05], 0.03, 0.16, "iron", "dogs", top=0.7, seg=10)
        a.r([sx * 0.25, 0.235, 0.05], [0.042, 0.042, 0.042], "iron", "dogs", 0.02)
        a.r([sx * 0.25, 0.08, -0.08], [0.025, 0.025, 0.3], "iron", "dogs", 0.006)
        a.r([sx * 0.25, 0.075, 0.05], [0.12, 0.016, 0.02], "iron", "dogs", 0.004, rot=[0, 0, 0])
    for sx in (-1, 1):
        a.col([sx * (W / 2 - 0.13), H / 2, 0], [0.26, H, D])
    a.col([0, H - 0.1, 0], [W, 0.36, D])
    return a.write(root, [W + 0.2, D + 0.4], fragments=["mantel"], keep=["jambs", "hearth", "dogs"],
                   surfaces=[[-W / 2, -D / 2 + 0.05, W / 2, D / 2 + 0.05, H + 0.07]])


def clock(root):
    a = A("furn_clock", "Long-case clock", "Comtoise-style long-case clock in painted pine: waisted trunk, glazed hood, brass dial and pendulum window.",
          "painted", 12, 35, wall=True, front=0.6)
    w = "paint_cream"
    a.r([0, 0.15, 0], [0.48, 0.3, 0.32], w, "case", 0.01)
    a.r([0, 1.0, 0], [0.4, 1.4, 0.26], w, "case", 0.01)
    a.r([0, 1.95, 0.0], [0.5, 0.5, 0.3], w, "hood", 0.012)
    a.arch([0, 2.2, 0.0], [0.5, 0.1, 0.28], w, "hood", th=0.3)                       # bonnet: a solid half-disc on the hood
    a.r([0, 1.95, 0.155], [0.34, 0.34, 0.008], "brass", "hood", 0.004)               # repoussé brass surround
    a.cyl([0, 1.95, 0.161], 0.26, 0.006, "dial", "hood", seg=24, rot=[90, 0, 0])     # round enamel dial
    a.r([0, 1.97, 0.166], [0.008, 0.09, 0.003], "iron", "hood", 0.001)               # hands
    a.r([0.03, 1.95, 0.166], [0.07, 0.007, 0.003], "iron", "hood", 0.001)
    a.b([0, 1.95, 0.17], [0.32, 0.32, 0.004], "glass", "glass")
    a.r([0, 1.25, 0.135], [0.012, 0.6, 0.006], "brass", "case", 0.002)              # pendulum rod
    a.cyl([0, 0.94, 0.137], 0.13, 0.008, "brass", "case", seg=20, rot=[90, 0, 0])    # lenticle bob
    a.b([0, 1.1, 0.143], [0.16, 0.6, 0.004], "glass", "glass")
    a.r([0, 1.72, 0.0], [0.46, 0.04, 0.29], w, "case", 0.008)
    a.r([0, 0.32, 0.0], [0.46, 0.04, 0.29], w, "case", 0.008)
    a.col([0, 1.1, 0], [0.5, 2.2, 0.32])
    return a.write(root, [0.5, 0.32], fragments=["hood"], keep=["case"], glass="glass")


# ---------------------------------------------------------------- small props (cosmetic: no blocking collision)
def crockery(root):
    a = A("furn_crockery", "Crockery stack", "Stacked faience plates, a bowl and a cup.", "ceramic", 1, 6, blocking=False, front=0.0)
    for i in range(5):
        a.plate([0, 0.006 + i * 0.013, 0], 0.23, 0.011, "faience" if i % 2 else "faience_dots", "plates")
    bowl(a, 0.2, 0.0, 0.02, 0.15, 0.07, "faience_red", "bowl")
    a.cyl([0.15, 0.035, -0.14], 0.075, 0.07, "faience", "bowl", top=0.82, seg=14, rot=[180, 0, 0])   # cup
    a.arch([0.195, 0.035, -0.14], [0.03, 0.04, 0.008], "faience", "bowl", th=0.007, rot=[0, 0, -90])
    return a.write(root, [0.42, 0.32], fragments=[], keep=[])


def bowl(a, x, y, z, d, h, m, grp):
    """P1: a bowl: a flaring frustum on a foot ring."""
    a.cyl([x, y + 0.006, z], d * 0.45, 0.012, m, grp, seg=14)
    a.cyl([x, y + 0.012 + (h - 0.012) / 2, z], d, h - 0.012, m, grp, top=0.55, seg=18, rot=[180, 0, 0])


def jug(root):
    a = A("furn_jug_bowl", "Jug and basin", "Stoneware jug standing in a faience basin.", "ceramic", 1, 6, blocking=False, front=0.0)
    bowl(a, 0, 0, 0, 0.36, 0.07, "faience", "basin")
    a.cyl([0, 0.11, 0], 0.15, 0.08, "stoneware", "jug", top=0.9, seg=18, rot=[180, 0, 0])   # belly
    a.cyl([0, 0.19, 0], 0.15, 0.08, "stoneware", "jug", top=0.62, seg=18)                   # shoulder
    a.cyl([0, 0.26, 0], 0.095, 0.06, "stoneware", "jug", top=0.85, seg=16, rot=[180, 0, 0])   # neck, flaring to the rim
    a.wedge([0, 0.28, 0.055], [0.04, 0.02, 0.05], "stoneware", "jug", top=0.2)              # pouring lip
    a.arch([0.075, 0.18, 0], [0.13, 0.05, 0.02], "stoneware", "jug", th=0.016, rot=[0, 0, -90])   # handle
    return a.write(root, [0.36, 0.36], fragments=[], keep=[])


def bottle(a, x, z, m, grp, h=0.2, d=0.075):
    """P1: a wine / cider bottle: body, rounded shoulder, neck, lip ring."""
    a.cyl([x, h * 0.36, z], d, h * 0.72, m, grp, seg=14)
    a.cyl([x, h * 0.72 + 0.02, z], d, 0.04, m, grp, top=0.42, seg=14)
    a.cyl([x, h * 0.72 + 0.075, z], d * 0.38, 0.07, m, grp, top=0.9, seg=10)
    a.cyl([x, h * 0.72 + 0.11, z], d * 0.44, 0.012, m, grp, seg=10)


def bottles(root):
    a = A("furn_bottles", "Bottles", "Three cider and wine bottles, green and brown glass.", "glass", 1, 4, blocking=False, front=0.0)
    for i, (x, z, m) in enumerate([(-0.07, 0.0, "bottle_green"), (0.03, 0.04, "bottle_brown"), (0.08, -0.05, "bottle_green")]):
        bottle(a, x, z, m, f"b{i}")
    a.cyl([0.03, 0.264, 0.04], 0.024, 0.02, "log_end", "b1", seg=8)   # a cork in the cider
    return a.write(root, [0.26, 0.2], fragments=[], keep=[])


def books(root):
    a = A("furn_books", "Book stack", "Four cloth-bound books, stacked a little askew.", "paper", 3, 6, blocking=False, front=0.0)
    covers = ["cover_red", "cover_blue", "cover_green", "cover_red"]
    y = 0
    for i, cv in enumerate(covers):
        t = 0.03 + 0.008 * (i % 2)
        w, d = 0.24 - 0.02 * (i % 3), 0.17 - 0.01 * i
        rot = [0, (i * 7) % 13 - 6, 0]
        a.r([0, y + t / 2, 0], [w, t, d], cv, f"book{i}", 0.004, rot=rot)
        a.b([0.004, y + t / 2, 0], [w - 0.012, t - 0.008, d + 0.002], "paper", f"book{i}", rot=rot)
        y += t
    return a.write(root, [0.26, 0.2], fragments=[], keep=[])


def lamp(root):
    a = A("furn_lamp", "Oil lamp", "Brass oil lamp with a glass chimney.", "glass", 1, 6, blocking=False, front=0.0)
    a.cyl([0, 0.015, 0], 0.12, 0.03, "brass", "base", top=0.7, seg=16)               # stepped foot
    a.cyl([0, 0.08, 0], 0.035, 0.1, "brass", "base", top=0.8, seg=12, rot=[180, 0, 0])
    a.cyl([0, 0.145, 0], 0.11, 0.04, "brass", "base", top=0.55, seg=16, rot=[180, 0, 0])   # font, lower half
    a.cyl([0, 0.18, 0], 0.11, 0.03, "brass", "base", top=0.6, seg=16)                       # font, upper half
    a.cyl([0, 0.2, 0], 0.06, 0.012, "brass", "base", seg=12)                                # burner collar
    a.cyl([0, 0.27, 0], 0.06, 0.13, "glass", "glass", top=0.6, seg=14)                      # chimney: bulb...
    a.cyl([0, 0.36, 0], 0.036, 0.05, "glass", "glass", seg=12)                              # ...and stack
    a.r([0, 0.225, 0], [0.012, 0.03, 0.006], "flame", "glass", 0.002)   # L3: the wick's flame inside the chimney
    a.col([0, 0.2, 0], [0.1, 0.3, 0.1])   # L3: rounds can shoot it out (invisible: the navmesh skips it)
    a.light = {"color": [1.0, 0.68, 0.36], "intensity": 0.45, "radius": 4.0, "offset": [0, 0.24, 0], "flicker": 0.12, "group": "glass", "use": "interior"}
    return a.write(root, [0.12, 0.12], fragments=[], keep=["base"], glass="glass")


def crate(root):
    a = A("furn_crate", "Crate", "Nailed pine crate with slatted sides.", "wood", 8, 20, front=0.3)
    S = 0.5
    for i in range(3):
        y = 0.07 + i * 0.16
        for sz in (-1, 1):
            a.r([0, y, sz * (S / 2 - 0.01)], [S, 0.12, 0.02], "pine", f"slats{i}", 0.003)
        for sx in (-1, 1):
            a.r([sx * (S / 2 - 0.01), y, 0], [0.02, 0.12, S - 0.04], "pine", f"slats{i}", 0.003)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (S / 2 - 0.03), 0.22, sz * (S / 2 - 0.03)], [0.04, 0.44, 0.04], "pine", "corners", 0.004)
    a.r([0, 0.012, 0], [S - 0.04, 0.02, S - 0.04], "pine", "corners", 0.003)
    for sx in (-1, 1):   # rope handles through the end slats, nail heads at the corners
        a.arch([sx * (S / 2 + 0.004), 0.36, 0], [0.16, 0.05, 0.018], "sacking", "corners", th=0.014, rot=[0, 90, 0])
        for sz in (-1, 1):
            for y in (0.07, 0.23, 0.39):
                a.cyl([sx * (S / 2 - 0.03), y, sz * (S / 2 + 0.001)], 0.008, 0.003, "iron", f"slats{int((y - 0.07) / 0.16 + 0.5)}", seg=6, rot=[90, 0, 0])
    a.col([0, 0.22, 0], [S, 0.44, S])
    return a.write(root, [S, S], fragments=["slats2", "slats0"], keep=["corners"], surfaces=[[-0.2, -0.2, 0.2, 0.2, 0.45]])


def papers(root):
    a = A("furn_papers", "Scattered papers", "A small cluster of loose sheets, letters and a folded newspaper, two with curled corners.",
          "paper", 0, 2, blocking=False, front=0.0)
    sheets = [(0, 0, 8, 0), (0.12, 0.05, -21, 1), (-0.1, 0.09, 33, 0), (0.05, -0.12, 64, 2), (-0.16, -0.07, -48, 0), (0.2, -0.04, 12, 0)]
    for i, (x, z, yaw, curl) in enumerate(sheets):
        y = 0.002 + i * 0.0012
        if curl:   # a curled corner: the sheet in two parts, the corner lifted
            a.b([x - 0.02, y, z], [0.17, 0.0015, 0.297], "paper", "sheets", rot=[0, yaw, 0])
            a.b([x + 0.08, y + 0.011, z + 0.11], [0.06, 0.0015, 0.08], "paper", "sheets", rot=[15, yaw, 0])   # lifted corner, resting on its edge
        else:
            a.b([x, y, z], [0.21, 0.0015, 0.297], "paper", "sheets", rot=[0, yaw, 0])
    a.b([0.0, 0.01, 0.26], [0.29, 0.004, 0.2], "paper", "sheets", rot=[0, 4, 0])   # folded newspaper
    return a.write(root, [0.6, 0.6], fragments=[], keep=[])


def picture(root):
    a = A("furn_picture", "Picture frame", "Framed landscape print, hung on a wall at eye height.", "wood", 2, 6, blocking=False, wall=True, front=0.0, mount=1.45)
    a.r([0, 0.22, 0.012], [0.5, 0.36, 0.012], "print", "print", 0.002)
    for sy in (-1, 1):
        a.r([0, 0.22 + sy * 0.19, 0.016], [0.56, 0.04, 0.032], "oak_dark", "frame", 0.006)
    for sx in (-1, 1):
        a.r([sx * 0.26, 0.22, 0.016], [0.04, 0.42, 0.032], "oak_dark", "frame", 0.006)
    return a.write(root, [0.56, 0.04], fragments=["frame"], keep=[])


def curtains(root):
    a = A("furn_curtains", "Curtains", "Pair of checked cotton curtains on an iron rod, gathered at the sides of a window.",
          "cloth", 0, 8, blocking=False, wall=True, front=0.0, mount=0.7)
    a.cyl([0, 1.5, 0.06], 0.02, 1.5, "iron", "rod", seg=8, rot=[0, 0, 90])
    for sx in (-1, 1):
        a.r([sx * 0.765, 1.5, 0.06], [0.036, 0.036, 0.036], "iron", "rod", 0.017)              # ball finials
        a.r([sx * 0.7, 1.5, 0.03], [0.012, 0.012, 0.06], "iron", "rod", 0.003)                # wall brackets
        for i in range(5):   # gathered folds: flattened round pleats, deeper toward the tie-back
            x = sx * (0.64 - i * 0.04)
            a.cyl([x, 0.78, 0.06 + 0.01 * (i % 2)], 0.05, 1.44, "curtain", f"side{(sx + 1) // 2}", dz=0.022 + 0.004 * i, seg=8)
        a.cyl([sx * 0.56, 0.62, 0.075], 0.2, 0.025, "curtain", f"side{(sx + 1) // 2}", dz=0.05, seg=10)   # tie-back band
    return a.write(root, [1.5, 0.08], fragments=[], keep=["rod"])


def rug(root, id, name, W, D, variant, col):
    a = A(id, name, f"Woven wool rug, {W:.1f} x {D:.1f} m: border bands, " + ("kilim stripes and lozenges." if variant == 1 else "a muted medallion field.") +
          " Lies flat 2 mm above the floor (no z-fighting); no collision.", "cloth", 0, 30, blocking=False, front=0.0)
    grain = variant + 4 * (int(round(W * 50)) + 256 * int(round(D * 50)))
    m = f"rug_{id}"
    a.extra_mats[m] = (col, 0.97, "rug", grain)
    a.r([0, 0.002 + 0.005, 0], [W, 0.01, D], m, "rug", 0.004)
    if id not in ENVELOPES:   # GX2 rugs (the hall runner): the envelope is the rug
        a.prop["bounds"] = parts_bounds(a.parts)
    return a.write(root, [W, D], fragments=[], keep=[])


# ---------------------------------------------------------------- V4: the second set (dense, lived-in interiors)
def books_row(a, x0, x1, y, z, depth, grp, seed):
    """A row of upright books on a shelf at height y, spines facing +z (a few leaning)."""
    covers = ["cover_red", "cover_blue", "cover_green", "paper", "cover_red", "cover_green"]
    x, i = x0, 0
    while True:
        w = 0.025 + 0.012 * ((seed + i * 7) % 4)
        h = 0.17 + 0.03 * ((seed + i * 5) % 3)
        if x + w > x1:
            break
        lean = 8 if (seed + i) % 9 == 0 else 0
        a.r([x + w / 2, y + h / 2, z], [w, h, depth], covers[(seed + i) % len(covers)], grp, 0.003, rot=[0, 0, lean] if lean else None)
        x += w + 0.002 + (0.03 if lean else 0)
        i += 1


def dresser(root):
    a = A("furn_dresser", "Buffet bas (dresser)", "Low Norman buffet: two panelled doors under two drawers, moulded top, short feet. Things stand on its top.",
          "wood", 18, 45, wall=True, front=0.7)
    W, D, H = 1.25, 0.52, 0.95
    w = "oak"
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.02), H / 2 + 0.04, 0], [0.04, H - 0.08, D], w, "carcass", 0.006)
        for sz in (-1, 1):   # turned feet
            a.cyl([sx * (W / 2 - 0.04), 0.04, sz * (D / 2 - 0.04)], 0.065, 0.08, w, "carcass", top=0.6, seg=12, rot=[180, 0, 0])
    a.r([0, 0.1, 0], [W - 0.04, 0.04, D - 0.02], w, "carcass", 0.005)
    a.arch([0, 0.055, D / 2 - 0.03], [W - 0.1, 0.05, 0.02], w, "carcass", th=0.02, fill=True)   # shaped apron between the feet
    a.r([0, H / 2 + 0.04, -D / 2 + 0.01], [W - 0.04, H - 0.1, 0.02], w, "carcass", 0.004)
    a.r([0, H - 0.015, 0.012], [W + 0.05, 0.035, D + 0.035], w, "top", 0.01)
    a.r([0, H - 0.04, 0.008], [W + 0.015, 0.016, D + 0.02], w, "top", 0.006)   # moulding under the top
    for i, sx in enumerate((-1, 1)):
        drawer_front(a, sx * W / 4, H - 0.12, D / 2 - 0.01, W / 2 - 0.05, 0.12, w, f"drawer{i}")
        bail(a, sx * W / 4, H - 0.11, D / 2 + 0.007, f"drawer{i}")
        framed_door(a, sx * W / 4, 0.46, D / 2 - 0.01, W / 2 - 0.05, 0.6, w, f"door{i}")
        a.r([sx * 0.05, 0.52, D / 2 + 0.003], [0.016, 0.06, 0.004], "brass", f"door{i}", 0.002)
        knob(a, sx * 0.05, 0.48, D / 2 + 0.001, f"door{i}", 0.022)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.05, D + 0.035], fragments=["top", "door0", "door1", "drawer0"], keep=["carcass", "drawer1"],
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.06, W / 2 - 0.05, D / 2 - 0.02, H + 0.003]])


def sideboard(root):
    a = A("furn_sideboard", "Sideboard", "Long painted enfilade: three doors, a drawer row, plinth. Crockery and lamps stand on it.",
          "painted", 18, 45, wall=True, front=0.7)
    W, D, H = 1.8, 0.48, 0.9
    w = "paint_green"
    for x in (-W / 2 + 0.02, -W / 6, W / 6, W / 2 - 0.02):
        a.r([x, H / 2 + 0.03, 0], [0.035, H - 0.06, D], w, "carcass", 0.006)
    a.r([0, 0.05, 0], [W - 0.02, 0.1, D - 0.02], w, "carcass", 0.006)
    a.r([0, H / 2, -D / 2 + 0.01], [W - 0.04, H - 0.08, 0.02], w, "carcass", 0.004)
    a.r([0, H - 0.015, 0.01], [W + 0.04, 0.03, D + 0.03], "oak", "top", 0.008)
    for i in range(3):
        cx = -W / 3 + i * W / 3
        drawer_front(a, cx, H - 0.1, D / 2 - 0.01, W / 3 - 0.06, 0.1, w, "drawers")
        bail(a, cx, H - 0.09, D / 2 + 0.007, "drawers", 0.06)
        framed_door(a, cx, 0.42, D / 2 - 0.01, W / 3 - 0.06, 0.56, w, f"door{i}")
        knob(a, cx + (W / 3 - 0.06) / 2 - 0.05, 0.46, D / 2 + 0.001, f"door{i}", 0.024)
    a.r([0, 0.1, 0.006], [W - 0.01, 0.02, D - 0.03], w, "carcass", 0.006)   # plinth moulding
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.04, D + 0.03], fragments=["top", "door0", "door2", "drawers"], keep=["carcass", "door1"],
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.05, W / 2 - 0.05, D / 2 - 0.02, H]])


def bookcase(root):
    a = A("furn_bookcase", "Bookcase", "Tall oak bookcase, five shelves of upright cloth-bound books, cornice. Books spill when it breaks.",
          "wood", 22, 50, wall=True, front=0.7)
    W, D, H = 1.0, 0.34, 2.0
    w = "oak_dark"
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.015), H / 2, 0], [0.03, H, D], w, "carcass", 0.006)
    a.r([0, H / 2, -D / 2 + 0.008], [W - 0.03, H, 0.016], w, "carcass", 0.003)
    a.r([0, H + 0.02, 0.01], [W + 0.06, 0.05, D + 0.04], w, "carcass", 0.012)
    a.r([0, 0.05, 0], [W - 0.02, 0.1, D - 0.01], w, "carcass", 0.005)
    ys = [0.1, 0.48, 0.86, 1.24, 1.62]
    for i, y in enumerate(ys):
        a.r([0, y + 0.012, 0.0], [W - 0.04, 0.024, D - 0.03], w, "shelves" if i < 3 else "upper", 0.004)
        books_row(a, -W / 2 + 0.05, W / 2 - 0.08 - 0.06 * (i % 2), y + 0.024, 0.01, 0.2, "books" if i % 2 == 0 else "books2", i * 3)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.06, D + 0.04], fragments=["upper", "books", "books2"], keep=["carcass", "shelves"])


def washstand(root):
    a = A("furn_washstand", "Washstand", "Marble-topped washstand with a splashback, towel rail, drawer and shelf; a faience basin and ewer stand on it.",
          "wood", 12, 32, wall=True, front=0.6)
    W, D, H = 0.85, 0.45, 0.78
    w = "cherry"
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        turned_leg(a, sx * (W / 2 - 0.03), sz * (D / 2 - 0.03), H, 0.042, w, "frame", block=0.13)
    a.r([0, 0.16, 0], [W - 0.06, 0.02, D - 0.06], w, "frame", 0.004)   # lower shelf
    drawer_front(a, 0, H - 0.08, D / 2 - 0.03, W - 0.1, 0.1, w, "drawer")
    knob(a, 0, H - 0.08, D / 2 - 0.011, "drawer", 0.024)
    a.r([0, H - 0.08, -D / 2 + 0.03], [W - 0.1, 0.1, 0.02], w, "frame", 0.004)
    a.r([0, H + 0.015, 0], [W + 0.02, 0.03, D + 0.02], "marble", "top", 0.006)
    a.r([0, H + 0.13, -D / 2 + 0.01], [W, 0.2, 0.02], "marble", "top", 0.005)
    for sx in (-1, 1):
        a.r([sx * (W / 2 + 0.03), H - 0.1, 0], [0.018, 0.018, 0.018], w, "frame", 0.004)
    a.cyl([0, H - 0.1, 0], 0.016, D - 0.1, w, "frame", seg=8, rot=[90, 0, 0])   # towel rail along a side
    a.r([0, H - 0.18, D / 2 + 0.01], [W - 0.1, 0.2, 0.006], "linen", "towel", 0.003)
    bowl(a, 0, H + 0.03, 0.02, 0.36, 0.09, "faience", "basin")
    a.cyl([0.13, H + 0.1, -0.05], 0.12, 0.1, "faience_red", "basin", top=0.85, seg=16, rot=[180, 0, 0])   # ewer belly
    a.cyl([0.13, H + 0.2, -0.05], 0.12, 0.1, "faience_red", "basin", top=0.55, seg=16)                    # shoulder and neck
    a.cyl([0.13, H + 0.27, -0.05], 0.08, 0.04, "faience_red", "basin", top=0.82, seg=14, rot=[180, 0, 0])
    a.arch([0.2, H + 0.17, -0.05], [0.14, 0.05, 0.02], "faience_red", "basin", th=0.016, rot=[0, 0, -90])   # handle
    a.arch([0, H + 0.268, -D / 2 + 0.01], [W, 0.08, 0.02], "marble", "top", th=0.3)   # rounded crest on the splashback
    a.col([0, H / 2, 0], [W, H + 0.05, D])
    return a.write(root, [W + 0.06, D + 0.02], fragments=["basin", "top", "drawer", "towel"], keep=["frame"])


def stove(root):
    a = A("furn_stove", "Cooking range", "Cast-iron cuisinière: enamelled oven doors, brass rail, hot plate rings, a flue pipe to the wall, a pot on top.",
          "metal", 0, 160, wall=True, front=0.9)
    W, D, H = 1.0, 0.6, 0.82
    a.r([0, H / 2 + 0.05, 0], [W, H - 0.1, D], "cast_iron", "body", 0.012)
    for sx in (-1, 1):
        for sz in (-1, 1):   # cast cabriole-ish feet: a tapering turned foot
            a.cyl([sx * (W / 2 - 0.06), 0.03, sz * (D / 2 - 0.06)], 0.06, 0.06, "cast_iron", "body", top=0.65, seg=10, rot=[180, 0, 0])
    a.r([0, H + 0.01, 0], [W + 0.02, 0.025, D + 0.02], "cast_iron", "top", 0.006)
    for x, z in ((-0.25, 0.1), (0.05, 0.1), (-0.25, -0.13)):   # hot plates: concentric lids in their rings
        a.cyl([x, H + 0.026, z], 0.22, 0.008, "iron", "top", seg=20)
        a.cyl([x, H + 0.031, z], 0.15, 0.004, "cast_iron", "top", seg=18)
        a.cyl([x, H + 0.035, z], 0.06, 0.004, "iron", "top", seg=12)
    a.cyl([0, H - 0.06, D / 2 + 0.04], 0.02, W - 0.06, "brass", "rail", seg=10, rot=[0, 0, 90])
    for sx in (-1, 1):
        a.cyl([sx * (W / 2 - 0.05), H - 0.06, D / 2 + 0.02], 0.018, 0.05, "brass", "rail", seg=8, rot=[90, 0, 0])
    for i, x in enumerate((-0.24, 0.24)):
        a.r([x, 0.42, D / 2 + 0.006], [0.4, 0.42, 0.02], "enamel", f"door{i}", 0.01)
        a.r([x, 0.42, D / 2 + 0.018], [0.34, 0.36, 0.006], "enamel", f"door{i}", 0.003)   # raised panel
        a.cyl([x, 0.6, D / 2 + 0.03], 0.018, 0.12, "brass", f"door{i}", seg=8, rot=[0, 0, 90])   # handle bar
        for sx in (-1, 1):
            a.cyl([x + sx * 0.055, 0.6, D / 2 + 0.022], 0.014, 0.016, "brass", f"door{i}", seg=8, rot=[90, 0, 0])
    a.cyl([0.38, H + 0.5, -D / 2 + 0.1], 0.13, 1.0, "cast_iron", "flue", seg=16)
    a.cyl([0.38, H + 0.04, -D / 2 + 0.1], 0.16, 0.05, "cast_iron", "flue", seg=16)   # flue collar
    a.cyl([0.38, H + 0.6, -D / 2 + 0.1], 0.14, 0.02, "cast_iron", "flue", seg=16)    # pipe joint
    a.cyl([0.08, H + 0.1, 0.1], 0.22, 0.15, "cast_iron", "pot", top=0.92, seg=18)
    a.cyl([0.08, H + 0.18, 0.1], 0.21, 0.016, "cast_iron", "pot", seg=18)
    a.r([0.08, H + 0.2, 0.1], [0.04, 0.025, 0.02], "cast_iron", "pot", 0.008)
    for x in (-0.06, 0.0, 0.06):   # L3: the glowing slots of the firebox draught plate
        a.r([0.3 + x, 0.14, D / 2 + 0.002], [0.025, 0.05, 0.004], "ember", "grate", 0.0018)
    a.light = {"color": [1.0, 0.45, 0.16], "intensity": 0.3, "radius": 2.5, "offset": [0.3, 0.16, D / 2 + 0.12], "flicker": 0.35, "group": "grate", "use": "fire"}
    a.col([0, H / 2, 0], [W, H + 0.03, D])
    return a.write(root, [W + 0.02, D + 0.05], fragments=["pot", "door0", "flue"], keep=["body", "top", "rail", "door1"])


def pantry(root):
    a = A("furn_pantry", "Pantry shelves", "Open pine shelves crowded with preserving jars, crocks, a cheese and folded cloths. Jars shatter first.",
          "wood", 14, 30, wall=True, front=0.6)
    W, D, H = 1.1, 0.36, 1.85
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.025), H / 2, sz * (D / 2 - 0.025)], [0.045, H, 0.045], "pine", "frame", 0.006)
    ys = [0.15, 0.6, 1.05, 1.5]
    mats = ["preserve_red", "preserve_amber", "preserve_green"]
    for i, y in enumerate(ys):
        a.r([0, y, 0], [W - 0.02, 0.024, D - 0.01], "pine", "frame", 0.004)
        for j in range(6):
            x = -W / 2 + 0.11 + j * 0.17
            h = 0.16 if (i + j) % 3 else 0.22
            if (i * 6 + j) % 5 == 4:   # a stoneware crock with a lid
                a.cyl([x, y + 0.012 + 0.07, 0.0], 0.13, 0.14, "stoneware", "crocks", top=0.88, seg=16)
                a.cyl([x, y + 0.012 + 0.145, 0.0], 0.115, 0.012, "stoneware", "crocks", seg=16)
                continue
            jar(a, x, y + 0.012, 0.02, h, 0.1, mats[(i + j) % 3])
    a.r([0.2, 1.5 + 0.06, -0.06], [0.24, 0.1, 0.18], "linen", "cloths", 0.03)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["jars", "crocks", "cloths"], keep=["frame"], glass="glass")


def sacks(root):
    a = A("furn_sacks", "Grain sacks", "Three tied hessian sacks of grain and flour slumped against each other.", "cloth", 30, 60, wall=True, front=0.4)
    for i, (x, z, h, tilt) in enumerate([(-0.25, 0.0, 0.62, 4), (0.12, 0.04, 0.55, -6), (-0.05, -0.28, 0.5, 9)]):
        a.r([x, h / 2, z], [0.42, h, 0.32], "sacking", f"sack{i}", 0.12, rot=[0, 15 * i, tilt])
        a.r([x, h + 0.04, z], [0.1, 0.1, 0.08], "sacking", f"sack{i}", 0.04, rot=[0, 15 * i, tilt])
    a.col([-0.07, 0.3, -0.1], [0.85, 0.6, 0.65])
    return a.write(root, [0.9, 0.7], fragments=["sack1", "sack2"], keep=["sack0"])


def barrel(root, id, name, rack):
    a = A(id, name, ("Two oak casks on their sides in a timber cradle, taps in the heads." if rack else
                     "Standing oak cask with iron hoops and a lid."), "wood", 20 if rack else 14, 55 if rack else 40, wall=rack, front=0.6 if rack else 0.4)
    def cask(c, along, L, R, grp):
        """P1: a coopered cask: two frustums bulging to the bilge (staves = the joinery grain along the axis), two end
        hoops and two bilge hoops, heads set in below the chime."""
        Rh = R * 0.84   # radius at the heads
        def at(t):   # point on the axis, t in -0.5..0.5 of L
            return [c[0], c[1] + t * L, c[2]] if along == "y" else [c[0] + t * L, c[1], c[2]]
        up, down = ([0, 0, 0], [180, 0, 0]) if along == "y" else ([0, 0, -90], [0, 0, 90])
        a.cyl(at(0.25), 2 * R, L / 2, "oak", grp, top=Rh / R, seg=20, rot=up)
        a.cyl(at(-0.25), 2 * R, L / 2, "oak", grp, top=Rh / R, seg=20, rot=down)
        for t in (-0.43, -0.3, 0.3, 0.43):
            rr = R - (R - Rh) * abs(t) / 0.5 + 0.005
            a.cyl(at(t), 2 * rr, 0.035, "iron", grp, seg=20, rot=up)
        for t in (-0.5, 0.5):   # heads, 2 cm inside the chime
            a.cyl(at(t - math.copysign(0.025, t)), 2 * Rh - 0.02, 0.012, "oak_dark", grp, rot=up)
        if along != "y":
            a.cyl([c[0] + L / 2 + 0.02, c[1] - Rh * 0.5, c[2]], 0.03, 0.08, "brass", grp, seg=10, rot=[0, 0, 90])   # tap
            a.r([c[0] + L / 2 + 0.055, c[1] - Rh * 0.5 + 0.03, c[2]], [0.014, 0.04, 0.03], "brass", grp, 0.005)    # tap key
            a.cyl([c[0], c[1] + R - 0.004, c[2]], 0.035, 0.012, "oak_dark", grp, seg=10)                            # bung
    if rack:
        for sx in (-1, 1):   # cradle rails with saddles cut for the casks
            a.r([sx * 0.3, 0.06, 0], [0.08, 0.12, 1.15], "oak_dark", "cradle", 0.01)
            for z in (-0.28, 0.28):
                for sz in (-1, 1):
                    a.wedge([sx * 0.3, 0.16, z + sz * 0.2], [0.08, 0.08, 0.14], "oak_dark", "cradle", top=0.0, rot=[0, 90 + 90 * sz, 0])
        a.r([0, 0.04, 0], [0.7, 0.06, 0.08], "oak_dark", "cradle", 0.006)
        cask([0.0, 0.42, -0.28], "x", 0.75, 0.27, "cask0")
        cask([0.0, 0.42, 0.28], "x", 0.75, 0.27, "cask1")
        a.col([0, 0.35, 0], [0.8, 0.7, 1.15])
        return a.write(root, [0.85, 1.15], fragments=["cask1", "cask0"], keep=["cradle"],
                       surfaces=[[-0.3, -0.45, 0.3, 0.45, 0.705]])
    cask([0, 0.42, 0], "y", 0.84, 0.28, "cask")
    a.cyl([0, 0.855, 0], 0.5, 0.03, "oak_dark", "lid", top=0.96, seg=20)   # loose lid
    a.r([0, 0.885, 0], [0.3, 0.03, 0.04], "oak_dark", "lid", 0.008)          # lid batten / handle
    a.col([0, 0.43, 0], [0.56, 0.86, 0.56])
    return a.write(root, [0.6, 0.6], fragments=["lid", "cask"], keep=[], surfaces=[[-0.18, -0.18, 0.18, 0.18, 0.875]])


def workbench(root):
    a = A("furn_workbench", "Workbench", "Carpenter's bench: thick beech top with a wooden vice and bench dogs, a tool well holding a plane, mallet and chisels.",
          "wood", 26, 60, wall=True, front=0.8)
    W, D, H = 1.9, 0.65, 0.86
    a.r([0, H - 0.04, 0], [W, 0.08, D - 0.12], "pine", "top", 0.008)
    a.r([0, H - 0.07, -D / 2 + 0.06], [W, 0.02, 0.12], "pine", "top", 0.004)   # tool well
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.12), (H - 0.08) / 2, sz * (D / 2 - 0.08)], [0.08, H - 0.08, 0.08], "oak", "legs", 0.008)
        a.r([sx * (W / 2 - 0.12), 0.16, 0], [0.07, 0.07, D - 0.1], "oak", "legs", 0.006)
    a.r([0, 0.16, 0], [W - 0.3, 0.07, 0.07], "oak", "legs", 0.006)
    for sx in (-1, 1):   # through-tenons of the long stretcher, wedged
        a.r([sx * (W / 2 - 0.07), 0.16, 0], [0.03, 0.06, 0.06], "oak", "legs", 0.004)
        a.r([sx * (W / 2 - 0.058), 0.16, 0], [0.008, 0.075, 0.018], "oak_dark", "legs", 0.002)
    a.r([W / 2 - 0.08, H - 0.12, D / 2 - 0.02], [0.3, 0.2, 0.06], "oak_dark", "vice", 0.01)
    a.cyl([W / 2 - 0.08, H - 0.12, D / 2 + 0.08], 0.034, 0.2, "oak_dark", "vice", seg=10, rot=[90, 0, 0])   # vice screw
    a.cyl([W / 2 - 0.08, H - 0.12, D / 2 + 0.03], 0.07, 0.04, "oak_dark", "vice", seg=12, rot=[90, 0, 0])   # screw boss
    a.cyl([W / 2 - 0.08, H - 0.12, D / 2 + 0.18], 0.022, 0.22, "oak_dark", "vice", seg=8, rot=[0, 0, 90])   # tommy bar
    for x in (-0.3, 0.1, 0.5):   # bench dogs standing proud of the top
        a.r([x, H + 0.012, 0.18], [0.025, 0.03, 0.025], "oak_dark", "top", 0.003)
    a.r([-0.4, H + 0.03, 0.02], [0.3, 0.06, 0.07], "oak", "tools", 0.01)   # plane
    a.r([-0.4, H + 0.07, 0.02], [0.04, 0.04, 0.03], "iron", "tools", 0.006)
    a.cyl([-0.31, H + 0.075, 0.02], 0.04, 0.03, "oak", "tools", seg=10, rot=[90, 0, 0])   # front knob
    a.cyl([0.15, H + 0.035, 0.1], 0.07, 0.13, "oak_dark", "tools", seg=12, rot=[90, 120, 0])   # mallet head
    a.cyl([0.25, H + 0.02, 0.15], 0.026, 0.28, "oak", "tools", top=0.8, seg=8, rot=[90, 120, 0])
    for i in range(3):   # chisels: blade and turned handle
        a.r([-0.1 + i * 0.06, H - 0.04, -D / 2 + 0.03], [0.02, 0.006, 0.1], "iron", "tools", 0.002)
        a.cyl([-0.1 + i * 0.06, H - 0.04, -D / 2 + 0.12], 0.026, 0.11, "oak", "tools", top=0.75, seg=8, rot=[90, 0, 0])
    a.r([0.55, H + 0.006, -0.05], [0.3, 0.01, 0.2], "log_end", "tools", 0.003)   # shavings / an offcut
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.04, D + 0.2], fragments=["tools", "vice", "top"], keep=["legs"],
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.14, W / 2 - 0.4, D / 2 - 0.08, H]])


def armchair(root):
    a = A("furn_armchair", "Armchair", "Upholstered fauteuil: carved cherry frame, padded seat and back in green check, scrolled arms.",
          "wood", 12, 30, front=0.5)
    W, D = 0.7, 0.68
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        turned_leg(a, sx * (W / 2 - 0.04), sz * (D / 2 - 0.05), 0.35, 0.05, "cherry", "frame", block=0.06)
    a.r([0, 0.4, 0.03], [W - 0.06, 0.12, D - 0.12], "upholstery", "seat", 0.04)
    a.r([0, 0.32, 0.03], [W - 0.04, 0.06, D - 0.08], "cherry", "frame", 0.008)
    a.arch([0, 0.265, D / 2 - 0.02], [W - 0.14, 0.045, 0.025], "cherry", "frame", th=0.02, fill=True)   # carved front apron
    a.r([0, 0.74, -D / 2 + 0.07], [W - 0.12, 0.62, 0.1], "upholstery", "back", 0.04, rot=[-8, 0, 0])
    a.r([0, 1.06, -D / 2 + 0.03], [W - 0.08, 0.05, 0.06], "cherry", "back", 0.015, rot=[-8, 0, 0])
    a.arch([0, 1.105, -D / 2 + 0.024], [0.26, 0.06, 0.05], "cherry", "back", th=0.2, rot=[-8, 0, 0])   # carved crest
    for i, sx in enumerate((-1, 1)):
        a.r([sx * (W / 2 - 0.045), 0.62, 0.0], [0.07, 0.05, D - 0.1], "cherry", f"arm{i}", 0.02)
        a.cyl([sx * (W / 2 - 0.045), 0.62, D / 2 - 0.05], 0.07, 0.06, "cherry", f"arm{i}", seg=14, rot=[0, 0, 90])   # scroll end
        a.cyl([sx * (W / 2 - 0.045), 0.5, D / 2 - 0.08], 0.04, 0.2, "cherry", f"arm{i}", top=0.75, seg=10)          # turned arm support
        a.cyl([sx * (W / 2 - 0.045), 0.59, D / 2 - 0.08], 0.05, 0.02, "cherry", f"arm{i}", seg=10)
    a.col([0, 0.33, 0.02], [W, 0.66, D - 0.04])
    a.col([0, 0.8, -D / 2 + 0.07], [W, 0.6, 0.14])
    return a.write(root, [W, D], fragments=["back", "seat", "arm0", "arm1"], keep=["frame"])


def desk(root):
    a = A("furn_desk", "Writing desk", "Oak bureau plat: leather-look writing top, three drawers in the apron, square legs; an inkwell and blotter on it.",
          "wood", 16, 40, wall=True, front=0.8)
    W, D, H = 1.15, 0.62, 0.76
    a.r([0, H - 0.015, 0], [W, 0.03, D], "oak", "top", 0.006)
    a.r([0, H + 0.001, 0.02], [W - 0.2, 0.004, D - 0.16], "cover_green", "top", 0.001)
    for i, x in enumerate((-W / 3, 0, W / 3)):
        drawer_front(a, x, H - 0.08, D / 2 - 0.03, W / 3 - 0.06, 0.09, "oak", "drawers")
        bail(a, x, H - 0.072, D / 2 - 0.013, "drawers", 0.055)
    a.r([0, H - 0.08, -D / 2 + 0.03], [W - 0.08, 0.1, 0.02], "oak", "apron", 0.004)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), H - 0.08, 0], [0.02, 0.1, D - 0.08], "oak", "apron", 0.004)
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        leg_tapered(a, sx * (W / 2 - 0.04), sz * (D / 2 - 0.04), H - 0.03, 0.05, 0.035, "oak", f"leg{i}")
    a.cyl([0.35, H + 0.04, -0.15], 0.06, 0.05, "glass", "glass", top=0.85, seg=12)
    a.cyl([0.35, H + 0.07, -0.15], 0.03, 0.012, "brass", "inkwell", seg=10)
    a.r([0.35, H + 0.009, -0.15], [0.11, 0.014, 0.09], "brass", "inkwell", 0.005)   # inkstand tray
    a.cyl([0.28, H + 0.012, -0.15], 0.012, 0.16, "oak_dark", "inkwell", top=0.6, seg=6, rot=[90, 75, 0])   # pen
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "drawers", "leg0", "inkwell"], keep=["apron", "leg2", "leg3", "leg1"], glass="glass",
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.05, W / 2 - 0.25, D / 2 - 0.05, H + 0.003]])


def pew(root):
    a = A("furn_pew", "Church pew", "Oak pew with a panelled back, book ledge and scrolled ends; seats three.", "wood", 24, 50, front=0.55)
    W, D = 2.1, 0.55
    for i, sx in enumerate((-1, 1)):
        a.r([sx * (W / 2 - 0.03), 0.48, 0], [0.05, 0.96, D], "oak_dark", f"end{i}", 0.012)
        a.arch([sx * (W / 2 - 0.03), 0.95, 0.1], [0.3, 0.11, 0.05], "oak_dark", f"end{i}", th=0.4, rot=[0, 90, 0])   # rounded poppyhead
        a.cyl([sx * (W / 2 - 0.03), 1.035, 0.1], 0.07, 0.07, "oak_dark", f"end{i}", top=0.4, seg=10)                 # finial
        a.r([sx * (W / 2 - 0.03), 0.5, 0.0], [0.065, 0.6, D - 0.12], "oak_dark", f"end{i}", 0.01)                     # fielded panel
    a.r([0, 0.44, 0.06], [W - 0.06, 0.04, 0.38], "oak", "seat", 0.008)
    a.r([0, 0.36, 0.22], [W - 0.06, 0.1, 0.025], "oak", "seat", 0.004)
    a.r([0, 0.72, -D / 2 + 0.06], [W - 0.06, 0.52, 0.03], "oak", "back", 0.006, rot=[-6, 0, 0])
    for k in range(3):
        a.r([-0.6 + k * 0.6, 0.72, -D / 2 + 0.078], [0.4, 0.38, 0.012], "oak_dark", "back", 0.004, rot=[-6, 0, 0])
    a.r([0, 0.96, -D / 2 + 0.03], [W - 0.06, 0.04, 0.12], "oak", "ledge", 0.008)   # book ledge for the row behind
    a.r([0, 0.08, 0.0], [W - 0.06, 0.04, 0.06], "oak", "seat", 0.004)
    a.col([0, 0.25, 0.06], [W, 0.5, 0.42])
    a.col([0, 0.75, -D / 2 + 0.06], [W, 0.5, 0.12])
    return a.write(root, [W, D], fragments=["back", "seat", "ledge", "end0"], keep=["end1"])


def school_desk(root):
    a = A("furn_school_desk", "School desk", "Twin pupils' desk with a sloped lid, inkwell holes and a fixed bench on an iron frame.", "wood", 16, 40, front=0.4)
    W, D = 1.1, 0.8
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), 0.35, 0.0], [0.04, 0.05, D - 0.05], "cast_iron", "frame", 0.01)
        a.r([sx * (W / 2 - 0.04), 0.35, -0.22], [0.04, 0.7, 0.04], "cast_iron", "frame", 0.01)
        a.r([sx * (W / 2 - 0.04), 0.2, 0.22], [0.04, 0.4, 0.04], "cast_iron", "frame", 0.01)
        a.r([sx * (W / 2 - 0.04), 0.02, 0.0], [0.05, 0.04, D - 0.05], "cast_iron", "frame", 0.01)
    a.r([0, 0.72, -0.22], [W, 0.025, 0.4], "pine", "lid", 0.005, rot=[-8, 0, 0])
    a.r([0, 0.62, -0.25], [W - 0.06, 0.18, 0.36], "pine", "box", 0.005)
    for sx in (-1, 1):   # inkwells: a black well in a brass ring, the pen groove
        a.cyl([sx * 0.3, 0.745, -0.4], 0.045, 0.012, "brass", "lid", seg=12)
        a.cyl([sx * 0.3, 0.749, -0.4], 0.03, 0.008, "slate_black", "lid", seg=12)
        a.r([sx * 0.12, 0.744, -0.4], [0.2, 0.006, 0.014], "oak_dark", "lid", 0.002)
    a.r([0, 0.42, 0.24], [W, 0.03, 0.26], "pine", "bench", 0.006)
    a.r([0, 0.62, 0.36], [W, 0.12, 0.025], "pine", "bench", 0.005, rot=[-6, 0, 0])
    a.col([0, 0.37, -0.22], [W, 0.74, 0.42])
    a.col([0, 0.3, 0.26], [W, 0.6, 0.3])
    return a.write(root, [W, D], fragments=["lid", "bench", "box"], keep=["frame"])


def wood_pile(root):
    a = A("furn_wood_pile", "Firewood stack", "Split logs of firewood stacked against a wall, a few kindling sticks on top.", "wood", 30, 45, wall=True, front=0.4)
    k = 0
    for row in range(4):
        for i in range(5 - (row % 2)):
            x = -0.36 + i * 0.18 + (0.09 if row % 2 else 0)
            grp, y, z = "logs" if row < 2 else "top", 0.08 + row * 0.14, -0.02 * (i % 2)
            split = (k * 7) % 3 == 0   # every third log split: a hexagonal billet turned at random about its axis
            roll = (k * 53) % 60
            a.cyl([x, y, z], 0.15, 0.42, "bark", grp, dz=0.14, n=6 if split else 0, seg=12, rot=[90, 0, roll])
            a.cyl([x, y, z + 0.211], 0.125, 0.004, "log_end", grp, dz=0.115, n=6 if split else 0, seg=12, rot=[90, 0, roll])
            k += 1
    for i in range(3):
        a.cyl([-0.1 + i * 0.12, 0.63, 0.02], 0.03, 0.4, "pine", "top", seg=6, rot=[90, 10 * i - 10, 0])
    a.col([0, 0.31, 0], [0.9, 0.62, 0.44])
    return a.write(root, [0.92, 0.46], fragments=["top", "logs"], keep=[])


def hay_bales(root):
    a = A("furn_hay_bales", "Hay bales", "Two stacked bales of hay with twine, loose hay at the foot.", "cloth", 40, 70, wall=True, front=0.5)
    a.r([0, 0.2, 0], [0.9, 0.4, 0.5], "hay", "bale0", 0.05)
    a.r([0.06, 0.6, 0.02], [0.9, 0.4, 0.5], "hay", "bale1", 0.05, rot=[0, 6, 0])
    for z in (-0.15, 0.15):
        a.r([0, 0.2, z], [0.92, 0.41, 0.012], "sacking", "bale0", 0.004)
        a.r([0.06, 0.6, z + 0.02], [0.92, 0.41, 0.012], "sacking", "bale1", 0.004, rot=[0, 6, 0])
    a.r([0.2, 0.015, 0.35], [0.6, 0.03, 0.3], "hay", "loose", 0.012)
    a.col([0, 0.4, 0], [0.95, 0.8, 0.55])
    return a.write(root, [1.0, 0.8], fragments=["bale1", "loose"], keep=["bale0"])


# wall-hung (mount: height of the piece's origin above the floor; back at z = 0, front toward +z)
def coat_rack(root):
    a = A("furn_coat_rack", "Coat pegs", "Peg rail by the door with a wool coat, a work jacket and a cap.", "cloth", 4, 10, blocking=False, wall=True, front=0.0, mount=1.55)
    a.r([0, 0.0, 0.012], [0.9, 0.09, 0.024], "oak_dark", "rail", 0.006)
    for i in range(4):   # turned pegs angled up, a knob on each
        a.cyl([-0.33 + i * 0.22, 0.008, 0.06], 0.024, 0.08, "oak_dark", "rail", top=0.8, seg=8, rot=[70, 0, 0])
        a.r([-0.33 + i * 0.22, 0.022, 0.098], [0.03, 0.03, 0.03], "oak_dark", "rail", 0.014)
    a.r([-0.33, -0.45, 0.07], [0.42, 0.85, 0.09], "coat_wool", "coats", 0.04)
    a.r([0.11, -0.36, 0.07], [0.38, 0.7, 0.08], "coat_blue", "coats", 0.04)
    a.r([0.33, 0.02, 0.09], [0.2, 0.06, 0.2], "coat_wool", "cap", 0.03)
    return a.write(root, [0.9, 0.12], fragments=["coats", "cap"], keep=["rail"])


def wall_clock(root):
    a = A("furn_wall_clock", "Wall clock", "Cartel clock in a painted case with a brass pendulum.", "painted", 2, 8, blocking=False, wall=True, front=0.0, mount=1.6)
    a.r([0, -0.02, 0.05], [0.32, 0.34, 0.1], "paint_cream", "case", 0.03)
    a.arch([0, 0.15, 0.05], [0.32, 0.1, 0.1], "paint_cream", "case", th=0.3)                  # arched hood
    a.cyl([0, 0.215, 0.05], 0.05, 0.04, "brass", "case", top=0.3, seg=10)                         # finial
    a.cyl([0, 0.03, 0.103], 0.26, 0.008, "brass", "case", seg=24, rot=[90, 0, 0])                # bezel
    a.cyl([0, 0.03, 0.106], 0.22, 0.006, "dial", "case", seg=24, rot=[90, 0, 0])
    a.r([0, 0.055, 0.11], [0.007, 0.06, 0.003], "iron", "case", 0.001)                           # hands
    a.r([0.025, 0.03, 0.11], [0.05, 0.006, 0.003], "iron", "case", 0.001)
    a.cyl([0, 0.03, 0.113], 0.25, 0.004, "glass", "glass", seg=24, rot=[90, 0, 0])
    a.r([0, -0.3, 0.04], [0.06, 0.25, 0.04], "paint_cream", "case", 0.01)
    a.r([0, -0.27, 0.062], [0.006, 0.18, 0.004], "brass", "case", 0.002)                         # pendulum rod
    a.cyl([0, -0.38, 0.065], 0.08, 0.01, "brass", "case", seg=16, rot=[90, 0, 0])               # bob
    return a.write(root, [0.32, 0.11], fragments=["case"], keep=[], glass="glass")


def utensil_rack(root):
    a = A("furn_utensil_rack", "Utensil rack", "Iron rail with hanging copper pans, a ladle, a skimmer and a colander.", "metal", 0, 25, blocking=False, wall=True, front=0.0, mount=1.45)
    a.cyl([0, 0.0, 0.04], 0.02, 1.0, "iron", "rail", seg=8, rot=[0, 0, 90])
    for sx in (-1, 1):
        a.r([sx * 0.48, 0.0, 0.02], [0.02, 0.06, 0.04], "iron", "rail", 0.006)
        a.cyl([sx * 0.48, 0.0, 0.003], 0.05, 0.006, "iron", "rail", seg=10, rot=[90, 0, 0])   # wall rosette
    for i, (x, d, drop) in enumerate([(-0.35, 0.22, 0.16), (-0.08, 0.18, 0.13), (0.38, 0.2, 0.15)]):
        a.cyl([x, -drop - 0.05, 0.05], d, 0.05, "copper", "pans", top=0.8, seg=18, rot=[90, 0, 0])   # pan seen from below: base to the wall
        a.r([x, -0.05, 0.05], [0.025, 0.12, 0.012], "copper", "pans", 0.005)
        a.arch([x, 0.004, 0.05], [0.03, 0.03, 0.006], "iron", "pans", th=0.005, rot=[180, 0, 0])      # S-hook over the rail
    a.cyl([0.15, -0.18, 0.05], 0.016, 0.32, "iron", "pans", seg=6)                                      # ladle
    bowl(a, 0.15, -0.395, 0.06, 0.09, 0.05, "iron", "pans")
    return a.write(root, [1.0, 0.1], fragments=["pans"], keep=["rail"])


def tool_rack(root):
    a = A("furn_tool_rack", "Tool board", "Pine board with a bow saw, hammer, auger, square and a coil of rope on pegs.", "wood", 6, 15, blocking=False, wall=True, front=0.0, mount=1.1)
    a.r([0, 0.35, 0.01], [1.2, 0.7, 0.02], "pine", "board", 0.004)
    a.r([-0.35, 0.4, 0.04], [0.5, 0.025, 0.02], "oak", "tools", 0.006)   # bow saw
    a.r([-0.35, 0.53, 0.04], [0.5, 0.02, 0.012], "iron", "tools", 0.003)
    for sx in (-1, 1):
        a.r([-0.35 + sx * 0.25, 0.47, 0.04], [0.025, 0.16, 0.02], "oak", "tools", 0.006)
    a.cyl([0.1, 0.35, 0.04], 0.03, 0.32, "oak", "tools", dz=0.024, top=0.85, seg=10, rot=[180, 0, 0])   # hammer handle
    a.cyl([0.13, 0.5, 0.045], 0.035, 0.07, "iron", "tools", seg=10, rot=[0, 0, 90])                       # head and claw
    a.wedge([0.065, 0.5, 0.045], [0.06, 0.03, 0.03], "iron", "tools", top=0.2, rot=[0, 90, 0])
    a.cyl([0.3, 0.35, 0.04], 0.018, 0.45, "iron", "tools", seg=6)   # auger
    a.cyl([0.3, 0.58, 0.04], 0.03, 0.2, "oak", "tools", top=0.8, seg=8, rot=[0, 0, 90])
    a.cyl([0.48, 0.3, 0.05], 0.22, 0.06, "sacking", "rope", seg=16, rot=[90, 0, 0])                       # coil of rope
    a.cyl([0.48, 0.3, 0.083], 0.1, 0.008, "pine", "rope", seg=12, rot=[90, 0, 0])
    for x in (-0.35, 0.1, 0.3, 0.48):   # pegs
        a.cyl([x, 0.56 if x != 0.48 else 0.42, 0.03], 0.018, 0.04, "oak_dark", "board", seg=6, rot=[90, 0, 0])
    return a.write(root, [1.2, 0.08], fragments=["tools", "rope"], keep=["board"])


def mirror(root):
    a = A("furn_mirror", "Mirror", "Gilt-framed looking glass.", "glass", 1, 6, blocking=False, wall=True, front=0.0, mount=1.3)
    a.r([0, 0.3, 0.012], [0.44, 0.56, 0.006], "mirror", "glass", 0.002)
    for sy in (-1, 1):
        a.r([0, 0.3 + sy * 0.3, 0.016], [0.52, 0.05, 0.03], "brass", "frame", 0.01)
    for sx in (-1, 1):
        a.r([sx * 0.25, 0.3, 0.016], [0.05, 0.62, 0.03], "brass", "frame", 0.01)
    return a.write(root, [0.52, 0.04], fragments=["frame"], keep=[])


def portrait(root):
    a = A("furn_portrait", "Portrait", "Small framed family photograph portrait.", "wood", 1, 4, blocking=False, wall=True, front=0.0, mount=1.5)
    a.r([0, 0.15, 0.01], [0.22, 0.28, 0.008], "paper", "print", 0.002)
    a.r([0, 0.15, 0.006], [0.3, 0.36, 0.014], "oak_dark", "frame", 0.006)
    return a.write(root, [0.3, 0.03], fragments=[], keep=[])


def wall_shelf(root):
    a = A("furn_wall_shelf", "Wall shelf", "A single pine board on two shaped brackets; small things stand on it (the board is its collider).", "wood", 3, 10, wall=True, front=0.0, mount=1.45)
    a.r([0, 0.0, 0.13], [0.9, 0.025, 0.26], "pine", "board", 0.004)
    for sx in (-1, 1):   # shaped brackets: tall at the wall, sweeping out under the board
        a.wedge([sx * 0.36, -0.1, 0.07], [0.025, 0.18, 0.14], "pine", "brackets", top=0.2, rot=[180, 180, 0])
    a.col([0, 0.0, 0.13], [0.9, 0.025, 0.26])
    return a.write(root, [0.9, 0.26], fragments=["board"], keep=["brackets"], surfaces=[[-0.43, 0.02, 0.43, 0.25, 0.0125]])


def plate_rack(root):
    a = A("furn_plate_rack", "Plate rack", "Wall-hung oak plate rack: faience plates standing on two rails, a shelf on top.", "wood", 4, 12, wall=True, front=0.0, mount=1.35)
    W = 1.0
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.015), 0.25, 0.08], [0.025, 0.55, 0.16], "oak", "frame", 0.005)
    for y in (0.0, 0.28):
        a.r([0, y, 0.08], [W - 0.03, 0.02, 0.16], "oak", "frame", 0.004)
        a.r([0, y + 0.05, 0.13], [W - 0.03, 0.012, 0.012], "oak", "frame", 0.003)
        for i in range(4):
            a.plate([-W / 2 + 0.14 + i * 0.24, y + 0.12, 0.06], 0.21, 0.011, ["faience", "faience_dots", "faience_red"][(i + int(y * 10)) % 3], "plates", axis="z", tilt=[-10, 0, 0])
    a.r([0, 0.53, 0.09], [W, 0.022, 0.18], "oak", "frame", 0.004)
    a.col([0, 0.27, 0.08], [W, 0.56, 0.16])
    return a.write(root, [W, 0.18], fragments=["plates"], keep=["frame"], surfaces=[[-W / 2 + 0.05, 0.02, W / 2 - 0.05, 0.17, 0.541]])


# small props (cosmetic)
def jars(root):
    a = A("furn_jars", "Preserving jars", "Three glass jars of preserves with zinc lids.", "glass", 1, 4, blocking=False, front=0.0)
    for i, (x, z, h, m) in enumerate([(-0.06, 0.0, 0.16, "preserve_red"), (0.05, 0.03, 0.2, "preserve_amber"), (0.0, -0.08, 0.14, "preserve_green")]):
        jar(a, x, 0.0, z, h, 0.09, m)
    return a.write(root, [0.22, 0.2], fragments=[], keep=[], glass="glass")


def jar(a, x, y, z, h, d, m):
    """P1: preserving jar standing at y: the preserve, the glass with a shoulder, a zinc screw lid with a rim."""
    a.cyl([x, y + (h - 0.03) / 2 + 0.004, z], d * 0.88, h - 0.03, m, "jars", seg=14)
    a.cyl([x, y + (h - 0.02) / 2, z], d, h - 0.02, "jar_glass", "glass", seg=14)
    a.cyl([x, y + h - 0.012, z], d, 0.016, "jar_glass", "glass", top=0.82, seg=14)
    a.cyl([x, y + h + 0.002, z], d * 0.86, 0.014, "zinc", "jars", seg=14)
    a.cyl([x, y + h + 0.0095, z], d * 0.8, 0.003, "zinc", "jars", seg=14)


def bucket(root):
    a = A("furn_bucket", "Bucket", "Coopered wooden pail with iron hoops and a rope handle.", "wood", 2, 8, blocking=False, front=0.0)
    a.cyl([0, 0.15, 0], 0.29, 0.3, "oak", "pail", top=0.82, seg=18, rot=[180, 0, 0])   # coopered body, widening upward
    for y in (0.06, 0.26):
        rr = 0.29 * (0.82 + 0.18 * y / 0.3) + 0.008
        a.cyl([0, y, 0], rr, 0.022, "iron", "pail", seg=18)
    a.cyl([0, 0.2995, 0], 0.255, 0.004, "oak_dark", "pail", seg=16)   # the dark inside seen over the rim
    for sx in (-1, 1):   # ears through which the rope runs
        a.r([sx * 0.145, 0.32, 0], [0.025, 0.05, 0.04], "oak", "pail", 0.006)
    a.arch([0, 0.335, 0], [0.3, 0.13, 0.016], "sacking", "pail", th=0.014)   # rope bail
    return a.write(root, [0.3, 0.3], fragments=[], keep=[])


def basket(root):
    a = A("furn_basket", "Basket of apples", "Wicker basket with a bow handle, full of red apples.", "cloth", 2, 6, blocking=False, front=0.0)
    a.cyl([0, 0.1, 0], 0.42, 0.2, "wicker", "basket", dz=0.3, top=0.8, seg=20, rot=[180, 0, 0])   # oval, flaring to the rim
    a.cyl([0, 0.196, 0], 0.43, 0.022, "wicker", "basket", dz=0.31, seg=20)                          # rolled rim
    a.cyl([0, 0.012, 0], 0.32, 0.024, "wicker", "basket", dz=0.22, seg=16)                          # foot ring
    a.arch([0, 0.2, 0], [0.4, 0.18, 0.025], "wicker", "basket", th=0.02)                              # bow handle
    for i, (x, z) in enumerate([(-0.1, -0.05), (0.02, 0.06), (0.12, -0.04), (-0.04, 0.03), (0.08, 0.08)]):
        a.r([x, 0.21 + 0.01 * (i % 2), z], [0.075, 0.07, 0.075], "apple", "apples", 0.033)
        a.cyl([x, 0.248 + 0.01 * (i % 2), z], 0.006, 0.014, "bark", "apples", seg=4)                     # stalk
    return a.write(root, [0.42, 0.3], fragments=[], keep=[])


def pot(root):
    a = A("furn_pot", "Cooking pot", "Cast-iron marmite on three short legs, with a bail, a lid and a ladle.", "metal", 0, 20, blocking=False, front=0.0)
    a.cyl([0, 0.075, 0], 0.26, 0.09, "cast_iron", "pot", top=0.78, seg=18, rot=[180, 0, 0])   # belly
    a.cyl([0, 0.155, 0], 0.26, 0.07, "cast_iron", "pot", top=0.88, seg=18)                     # shoulder
    a.cyl([0, 0.195, 0], 0.235, 0.012, "cast_iron", "pot", seg=18)                             # rim
    for k in range(3):
        ang = math.radians(90 + 120 * k)
        a.cyl([math.cos(ang) * 0.08, 0.018, math.sin(ang) * 0.08], 0.022, 0.036, "cast_iron", "pot", top=0.6, seg=6, rot=[180, 0, 0])
    a.arch([0, 0.2, 0], [0.27, 0.12, 0.01], "iron", "pot", th=0.008)                          # bail
    a.cyl([0, 0.207, 0], 0.22, 0.014, "cast_iron", "lid", top=0.8, seg=18)
    a.cyl([0, 0.222, 0], 0.035, 0.018, "cast_iron", "lid", top=0.7, seg=10)
    a.cyl([0.11, 0.25, 0], 0.016, 0.2, "iron", "lid", seg=6, rot=[0, 0, -30])                # ladle handle
    return a.write(root, [0.3, 0.26], fragments=[], keep=[])


def candlesticks(root):
    a = A("furn_candlesticks", "Candlesticks", "A pair of brass candlesticks with half-burnt candles.", "metal", 0, 6, blocking=False, front=0.0)
    for i, x in enumerate((-0.07, 0.07)):
        a.cyl([x, 0.012, 0], 0.09, 0.024, "brass", "sticks", top=0.6, seg=14)                       # domed foot
        a.cyl([x, 0.09, 0], 0.022, 0.14, "brass", "sticks", top=0.8, seg=10)                         # stem
        a.r([x, 0.07, 0], [0.034, 0.03, 0.034], "brass", "sticks", 0.014)                            # knop
        a.cyl([x, 0.168, 0], 0.05, 0.016, "brass", "sticks", top=1.0, seg=12)                        # drip pan
        a.cyl([x, 0.24 - 0.03 * i, 0], 0.022, 0.12 - 0.06 * i, "wax", "sticks", seg=10)
        top = 0.24 - 0.03 * i + (0.12 - 0.06 * i) / 2
        a.r([x, top + 0.016, 0], [0.008, 0.026, 0.004], "flame", "flames", 0.0018)   # L3
    a.light = {"color": [1.0, 0.62, 0.3], "intensity": 0.22, "radius": 3.0, "offset": [0, 0.28, 0], "flicker": 0.28, "group": "flames", "use": "interior"}
    return a.write(root, [0.24, 0.1], fragments=[], keep=[])


# ---------------------------------------------------------------- L3 lights (docs/lights.md)
def light_block(a, light):
    """The asset's "light" block: group names become prop group indices."""
    out = []
    for l in (light if isinstance(light, list) else [light]):
        l = dict(l)
        if isinstance(l.get("group"), str):
            l["group"] = a.groups.index(l["group"])
        for k in ("offset", "dir", "color"):
            if k in l:
                l[k] = [round(v, 4) for v in l[k]]
        out.append(l)
    return out[0] if len(out) == 1 else out


def table_lamp(root):
    a = A("furn_lamp_table", "Table lamp", "Electric table lamp: turned porcelain base, brass stem and a pleated silk shade over one bulb. "
          "Towns and larger farms had mains electricity by 1944.", "ceramic", 1, 6, blocking=False, front=0.0)
    a.cyl([0, 0.012, 0], 0.13, 0.024, "brass", "base", top=0.8, seg=16)
    a.cyl([0, 0.06, 0], 0.115, 0.07, "porcelain", "base", top=0.8, seg=16, rot=[180, 0, 0])   # turned vase base
    a.cyl([0, 0.115, 0], 0.115, 0.04, "porcelain", "base", top=0.45, seg=16)
    a.cyl([0, 0.18, 0], 0.016, 0.12, "brass", "base", seg=8)
    a.r([0, 0.255, 0], [0.04, 0.05, 0.04], "bulb", "bulb", 0.018)
    a.cyl([0, 0.29, 0], 0.26, 0.16, "shade_silk", "shade", top=0.55, n=12)   # pleated (12-sided) tapering shade
    a.cyl([0, 0.212, 0], 0.262, 0.006, "brass", "shade", n=12)              # rim wire
    a.light = {"color": [1.0, 0.76, 0.48], "intensity": 0.9, "radius": 5.5, "offset": [0, 0.27, 0], "group": "bulb", "use": "interior"}
    a.col([0, 0.19, 0], [0.22, 0.36, 0.22])
    return a.write(root, [0.24, 0.24], fragments=["shade"], keep=["base"], glass="bulb")


def candle(root):
    a = A("furn_candle", "Candle", "A tallow candle in an enamelled chamberstick with a finger ring, half burnt.", "ceramic", 1, 4, blocking=False, front=0.0)
    a.cyl([0, 0.008, 0], 0.13, 0.016, "enamel_white", "holder", top=1.0, seg=18)                        # dish
    a.cyl([0, 0.019, 0], 0.11, 0.006, "enamel_white", "holder", seg=18)                                  # rim
    a.arch([0.07, 0.008, 0], [0.04, 0.035, 0.008], "enamel_white", "holder", th=0.008, rot=[0, 0, -90])  # finger ring
    a.cyl([0, 0.03, 0], 0.04, 0.03, "enamel_white", "holder", top=0.85, seg=12)
    a.cyl([0, 0.085, 0], 0.024, 0.08, "wax", "candle", seg=10)
    a.r([0.008, 0.11, 0.011], [0.008, 0.03, 0.006], "wax", "candle", 0.003)                               # a drip
    a.r([0, 0.138, 0], [0.008, 0.026, 0.004], "flame", "candle", 0.0018)
    a.col([0, 0.07, 0], [0.1, 0.14, 0.1])
    a.light = {"color": [1.0, 0.62, 0.3], "intensity": 0.12, "radius": 2.5, "offset": [0, 0.14, 0], "flicker": 0.3, "group": "candle", "use": "interior"}
    return a.write(root, [0.13, 0.12], fragments=[], keep=["holder"])


def candelabra(root):
    a = A("furn_candelabra", "Candelabra", "Five-branch brass altar candelabra with beeswax candles.", "metal", 0, 10, blocking=False, front=0.0)
    a.cyl([0, 0.02, 0], 0.18, 0.04, "brass", "frame", top=0.55, seg=16)
    a.cyl([0, 0.2, 0], 0.028, 0.36, "brass", "frame", top=0.75, seg=10)
    for y in (0.12, 0.27):   # knops on the stem
        a.r([0, y, 0], [0.045, 0.035, 0.045], "brass", "frame", 0.02)
    a.arch([0, 0.345, 0], [0.48, 0.07, 0.016], "brass", "frame", th=0.014, rot=[180, 0, 0])   # curved arms
    a.cyl([0, 0.38, 0], 0.018, 0.5, "brass", "frame", seg=8, rot=[0, 0, 90])
    for x in (-0.24, -0.12, 0.0, 0.12, 0.24):
        y = 0.38 + (0.06 if x == 0 else 0.0)
        a.cyl([x, y + 0.01, 0], 0.045, 0.02, "brass", "frame", top=1.0, seg=12)   # drip pan
        a.cyl([x, y + 0.08, 0], 0.022, 0.12, "wax", "candles", seg=10)
        a.r([x, y + 0.155, 0], [0.008, 0.026, 0.004], "flame", "candles", 0.0018)
    a.light = {"color": [1.0, 0.64, 0.32], "intensity": 0.55, "radius": 4.5, "offset": [0, 0.56, 0], "flicker": 0.25, "group": "candles", "use": "interior"}
    return a.write(root, [0.52, 0.18], fragments=[], keep=["frame"])


def ceiling_lamp(root):
    a = A("furn_lamp_ceiling", "Ceiling lamp", "Pendant lamp: a green enamelled steel shade on a cord from a ceiling rose, one bulb. Hangs from a ceiling or beam (origin at the top).",
          "metal", 0, 10, blocking=False, front=0.0)
    a.prop["hang"] = True
    a.cyl([0, -0.012, 0], 0.09, 0.024, "porcelain", "rose", top=0.7, seg=14, rot=[180, 0, 0])
    a.cyl([0, -0.22, 0], 0.008, 0.4, "iron", "rose", seg=6)
    a.cyl([0, -0.43, 0], 0.05, 0.04, "brass", "shade", top=0.7, seg=12)
    a.cyl([0, -0.475, 0], 0.32, 0.07, "enamel_green", "shade", top=0.2, seg=24)       # conical enamel shade
    a.cyl([0, -0.511, 0], 0.325, 0.006, "enamel_white", "shade", seg=24)               # white enamel rim
    a.r([0, -0.52, 0], [0.06, 0.06, 0.06], "bulb", "bulb", 0.026)
    a.light = {"color": [1.0, 0.77, 0.5], "intensity": 1.6, "radius": 8.0, "offset": [0, -0.53, 0], "cone": [70, 105], "dir": [0, -1, 0], "shadow": 1, "group": "bulb", "use": "interior"}
    a.col([0, -0.49, 0], [0.3, 0.12, 0.3])
    return a.write(root, [0.32, 0.32], fragments=["shade"], keep=["rose"], glass="bulb")


def caged_lamp(root):
    a = A("furn_lamp_caged", "Caged bulkhead lamp", "Electric bunker lamp: cast-iron ceiling mount, a glass globe and a steel wire guard (field-grey). Flush on concrete ceilings; also the low-ceiling lamp.",
          "metal", 0, 25, blocking=False, front=0.0)
    a.prop["hang"] = True
    a.extra_mats["bunker_grey"] = ([0.2, 0.21, 0.19], 0.6, "metal", 1)
    a.used.add("bunker_grey")
    a.cyl([0, -0.015, 0], 0.2, 0.03, "bunker_grey", "mount", top=0.85, seg=18, rot=[180, 0, 0])
    a.cyl([0, -0.035, 0], 0.15, 0.014, "bunker_grey", "mount", seg=16)                  # globe collar
    a.r([0, -0.09, 0], [0.13, 0.12, 0.13], "lamp_glass", "globe", 0.05)
    a.r([0, -0.08, 0], [0.04, 0.05, 0.04], "bulb", "globe", 0.018)
    for rot in (0, 60, 120):   # wire guard: three bent hoops over the globe
        a.arch([0, -0.105, 0], [0.16, 0.13, 0.006], "bunker_grey", "cage", th=0.005, rot=[180, rot, 0])
    a.cyl([0, -0.11, 0], 0.165, 0.006, "bunker_grey", "cage", seg=16)                  # equator ring
    a.col([0, -0.09, 0], [0.18, 0.16, 0.18])
    a.cyl([0, -0.17, 0], 0.05, 0.012, "bunker_grey", "cage", seg=10)
    a.light = {"color": [1.0, 0.85, 0.64], "intensity": 1.3, "radius": 7.0, "offset": [0, -0.1, 0], "shadow": 1, "group": "globe", "use": "electric"}
    return a.write(root, [0.2, 0.2], fragments=["cage"], keep=["mount"], glass="globe")


def hearth_fire(root):
    a = A("furn_fire_hearth", "Fireplace fire", "A log fire on the hearth: split oak logs over a bed of embers and ash, flames licking up the flue.",
          "masonry", 0, 400, blocking=False, front=0.0)
    a.r([0, 0.01, 0], [0.56, 0.02, 0.3], "ash", "ash", 0.008)
    a.r([0, 0.026, 0.0], [0.44, 0.02, 0.22], "ember", "fire", 0.008)
    for i, (x, z, yaw, L) in enumerate(((-0.06, 0.05, 8, 0.5), (0.05, -0.05, -10, 0.46), (0.0, 0.0, 85, 0.3))):
        y = 0.07 if i < 2 else 0.14
        a.cyl([x, y, z], 0.1, L, "bark", "logs", seg=12, rot=[90, yaw + 90, 0])                  # round log along its yaw
        a.cyl([x, y, z], 0.075, L + 0.006, "log_end", "logs", seg=12, rot=[90, yaw + 90, 0])     # the sawn ends
        a.cyl([x, y - 0.03, z], 0.07, L * 0.6, "ember", "fire", seg=10, rot=[90, yaw + 90, 0])   # glowing underside
    for x, h, rot in ((-0.1, 0.22, 12), (0.02, 0.3, -6), (0.12, 0.2, 20), (0.05, 0.16, 70)):
        a.r([x, 0.08 + h / 2, 0.0], [0.07, h, 0.01], "flame", "fire", 0.003, rot=[0, rot, 0])
    a.light = {"color": [1.0, 0.52, 0.2], "intensity": 1.8, "radius": 7.0, "offset": [0, 0.28, 0.12], "flicker": 0.8, "shadow": 1, "group": "fire", "use": "fire", "fx": "flame", "fxSize": 0.16}
    return a.write(root, [0.56, 0.3], fragments=[], keep=["ash", "logs"])


def potbelly_stove(root):
    a = A("furn_stove_potbelly", "Pot-bellied stove", "Small cast-iron heating stove with a glowing draught grate and a flue pipe; the Kanonenofen of dugouts, guard rooms and offices.",
          "metal", 0, 120, wall=True, front=0.6)
    a.cyl([0, 0.05, 0], 0.42, 0.04, "cast_iron", "body", top=0.9, seg=20)                       # base plate
    for k in range(4):   # cabriole-ish feet
        ang = math.radians(45 + 90 * k)
        a.cyl([math.cos(ang) * 0.17, 0.025, math.sin(ang) * 0.17], 0.045, 0.05, "cast_iron", "body", top=0.6, seg=8, rot=[180, 0, 0])
    a.cyl([0, 0.12, 0], 0.3, 0.1, "cast_iron", "body", seg=20)                                    # ash pit
    a.cyl([0, 0.27, 0], 0.38, 0.2, "cast_iron", "body", top=0.82, seg=20, rot=[180, 0, 0])        # the belly
    a.cyl([0, 0.47, 0], 0.38, 0.2, "cast_iron", "body", top=0.8, seg=20)
    a.cyl([0, 0.37, 0], 0.4, 0.025, "cast_iron", "body", seg=20)                                  # belly band
    a.cyl([0, 0.6, 0], 0.3, 0.06, "cast_iron", "body", top=0.85, seg=20)                          # firebox top
    a.cyl([0, 0.65, 0], 0.32, 0.04, "cast_iron", "body", top=0.6, seg=20)                         # domed lid
    a.cyl([0, 0.68, 0], 0.05, 0.03, "cast_iron", "body", top=0.6, seg=10)                         # lid knob
    a.cyl([0, 1.25, -0.05], 0.12, 1.1, "cast_iron", "flue", seg=14)
    a.cyl([0, 0.72, -0.05], 0.15, 0.04, "cast_iron", "flue", seg=14)                              # flue collar
    a.cyl([0, 1.1, -0.05], 0.13, 0.02, "cast_iron", "flue", seg=14)                               # damper ring
    a.r([0, 0.42, 0.2], [0.12, 0.1, 0.02], "cast_iron", "body", 0.006)                            # fuel door
    a.cyl([0.04, 0.42, 0.214], 0.02, 0.012, "cast_iron", "body", seg=8, rot=[90, 0, 0])
    for y in (0.2, 0.24, 0.28):
        a.r([0, y, 0.18], [0.16, 0.018, 0.006], "ember", "grate", 0.002)
    a.light = {"color": [1.0, 0.46, 0.17], "intensity": 0.5, "radius": 3.5, "offset": [0, 0.26, 0.32], "flicker": 0.4, "group": "grate", "use": "fire"}
    a.col([0, 0.35, 0], [0.42, 0.7, 0.42])
    return a.write(root, [0.42, 0.42], fragments=["flue"], keep=["body", "grate"])


# ---------------------------------------------------------------- GX2: lived-in interiors (graphics brief section 3)
# Iron bedstead, gramophone, wireless set, wall lamps (electric sconce, candle sconce, wall oil lamp), a single
# candlestick, a hall pendant, coat stand and coat hooks, suitcases, half-drawn curtains, a pan rail, broken glass and
# plaster debris, a hall runner and a set of framed paintings. Light colours here are placeholders: the fixtures'
# colour temperature, intensity and radius come from content/settings/interior_lights.json (src/lights.cpp).
GX2_MATS = {
    "iron_enamel": ([0.035, 0.034, 0.033], 0.42, "metal", 1),
    "leather_brown": ([0.16, 0.085, 0.045], 0.62, "fabric", 1),
    "leather_tan": ([0.3, 0.19, 0.1], 0.6, "fabric", 1),
    "walnut": ([0.13, 0.07, 0.04], 0.45, "joinery", -1),
    "grille_cloth": ([0.36, 0.3, 0.2], 0.95, "textile", 1),
    "bakelite": ([0.05, 0.03, 0.02], 0.3, "plain", 1),
    "felt_green": ([0.06, 0.13, 0.07], 0.95, "textile", 0),
    "record": ([0.012, 0.012, 0.012], 0.25, "plain", 1),
    "opal_glass": ([0.86, 0.84, 0.78], 0.15, "glass", 1),
    "gilt": ([0.55, 0.4, 0.17], 0.38, "metal", 1),
    "canvas_sky": ([0.42, 0.47, 0.5], 0.85, "fabric", 1),
    "canvas_dusk": ([0.62, 0.45, 0.22], 0.85, "fabric", 1),
    "canvas_field": ([0.24, 0.27, 0.12], 0.9, "fabric", 1),
    "canvas_tree": ([0.08, 0.11, 0.05], 0.9, "fabric", 1),
    "canvas_sea": ([0.17, 0.24, 0.27], 0.8, "fabric", 1),
    "canvas_sand": ([0.5, 0.43, 0.3], 0.9, "fabric", 1),
    "canvas_dark": ([0.06, 0.05, 0.04], 0.9, "fabric", 1),
    "canvas_skin": ([0.55, 0.4, 0.3], 0.8, "fabric", 1),
    "canvas_sky_hi": ([0.3, 0.36, 0.42], 0.85, "fabric", 1),
    "canvas_sky_lo": ([0.6, 0.58, 0.5], 0.85, "fabric", 1),
    "canvas_cloud": ([0.72, 0.71, 0.66], 0.85, "fabric", 1),
    "canvas_hill": ([0.2, 0.26, 0.27], 0.9, "fabric", 1),
    "canvas_field_lit": ([0.4, 0.38, 0.17], 0.9, "fabric", 1),
    "canvas_tree_lit": ([0.17, 0.21, 0.08], 0.9, "fabric", 1),
    "canvas_sea_lit": ([0.32, 0.42, 0.43], 0.8, "fabric", 1),
    "canvas_dusk_hi": ([0.32, 0.14, 0.08], 0.85, "fabric", 1),
    "canvas_dusk_lo": ([0.85, 0.62, 0.28], 0.85, "fabric", 1),
    "canvas_sun": ([1.0, 0.86, 0.52], 0.85, "fabric", 1),
    "canvas_hair": ([0.08, 0.05, 0.03], 0.9, "fabric", 1),
    "canvas_glow": ([0.2, 0.16, 0.11], 0.9, "fabric", 1),
    "plaster_chunk": ([0.6, 0.57, 0.5], 0.95, "plain", 1),
    "plaster_dust": ([0.5, 0.48, 0.43], 0.98, "plain", 1),
    "lath": ([0.36, 0.27, 0.17], 0.9, "joinery", -1),
    "brick_red": ([0.33, 0.14, 0.08], 0.9, "plain", 1),
    "shard": ([0.7, 0.76, 0.74], 0.04, "glass", 1),
    "blanket_grey": ([0.24, 0.23, 0.21], 0.97, "textile", 3),
    "felt_hat": ([0.1, 0.09, 0.08], 0.95, "textile", 0),
    "scarf": ([0.32, 0.08, 0.06], 0.95, "textile", 2),
    "umbrella": ([0.04, 0.04, 0.05], 0.7, "textile", 0),
}
MATS.update(GX2_MATS)
EMISSIVE["opal_glass"] = [2.4, 1.85, 1.2]


def quat_mat(r):
    """glm::quat(glm::radians(r)) as a 3x3 row-major matrix (src/assets.cpp part rotation)."""
    hx, hy, hz = (math.radians(v) * 0.5 for v in r)
    cx, cy, cz, sx, sy, sz = math.cos(hx), math.cos(hy), math.cos(hz), math.sin(hx), math.sin(hy), math.sin(hz)
    w = cx * cy * cz + sx * sy * sz
    x = sx * cy * cz - cx * sy * sz
    y = cx * sy * cz + sx * cy * sz
    z = cx * cy * sz - sx * sy * cz
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]]


def parts_bounds(parts):
    """AABB of the visible parts, rotated parts by their rotated box (src/interior.cpp partAabb)."""
    mn, mx = [1e9] * 3, [-1e9] * 3
    for p in parts:
        if p.get("v", 1) == 0:
            continue
        R = quat_mat(p["r"]) if "r" in p else [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        h = [v / 2 for v in p["s"]]
        for i in range(3):
            e = abs(R[i][0]) * h[0] + abs(R[i][1]) * h[1] + abs(R[i][2]) * h[2]
            mn[i] = min(mn[i], p["c"][i] - e)
            mx[i] = max(mx[i], p["c"][i] + e)
    return [[round(v, 4) for v in mn], [round(v, 4) for v in mx]]


def gx2_write(a, root, footprint, **kw):
    """GX2 pieces: their furnishing envelope (prop.bounds) is the visible bounds as designed."""
    a.prop["bounds"] = parts_bounds(a.parts)
    return a.write(root, footprint, **kw)


def tube(a, p0, p1, d, m, grp, seg=10):
    """A round bar from p0 to p1 (any direction): a cylinder whose y axis is turned onto the segment (rot [pitch, yaw, 0])."""
    dx, dy, dz = (p1[i] - p0[i] for i in range(3))
    L = math.sqrt(dx * dx + dy * dy + dz * dz)
    c = [(p0[i] + p1[i]) / 2 for i in range(3)]
    return a.cyl(c, d, L, m, grp, seg=seg, rot=[math.degrees(math.atan2(math.hypot(dx, dz), dy)), math.degrees(math.atan2(dx, dz)), 0])


def bed_iron(root):
    a = A("furn_bed_iron", "Iron bedstead", "Single black-enamelled iron bedstead: tubular posts with brass knobs and collars, spindled head- and footboards "
          "under bowed top rails, angle-iron side rails, a ticking mattress, a linen sheet turned down over a grey wool blanket draped over the sides, a plump pillow.",
          "metal", 0, 70, wall=True, front=0.55)
    W, L = 1.0, 2.0
    for z, ph, grp in ((-L / 2 + 0.02, 1.18, "head"), (L / 2 - 0.02, 0.9, "foot")):
        for sx in (-1, 1):
            x = sx * (W / 2 - 0.02)
            a.cyl([x, ph / 2, z], 0.034, ph, "iron_enamel", grp, seg=12)                      # post
            a.cyl([x, 0.012, z], 0.05, 0.024, "iron_enamel", grp, top=0.7, seg=12)             # foot cup
            a.r([x, ph + 0.025, z], [0.056, 0.056, 0.056], "brass", grp, 0.027)                # brass knob
            a.cyl([x, ph - 0.01, z], 0.046, 0.02, "brass", grp, seg=12)                        # collar under it
            a.cyl([x, ph * 0.52, z], 0.044, 0.026, "brass", grp, seg=12)                       # mid collar
        top, low = ph - 0.1, 0.46 if grp == "head" else 0.44
        a.cyl([0, top, z], 0.026, W - 0.04, "iron_enamel", grp, seg=10, rot=[0, 0, 90])     # top rail
        a.cyl([0, low, z], 0.022, W - 0.04, "iron_enamel", grp, seg=10, rot=[0, 0, 90])     # lower rail
        a.arch([0, top + 0.003, z], [W - 0.12, 0.12 if grp == "head" else 0.07, 0.022], "iron_enamel", grp, th=0.02)   # bowed top
        n = 7 if grp == "head" else 6
        for i in range(n):   # spindles, the middle ones with a brass ring
            x = -W / 2 + 0.02 + (W - 0.04) * (i + 1) / (n + 1)
            a.cyl([x, (top + low) / 2, z], 0.014, top - low, "iron_enamel", grp, seg=8)
            if i in (n // 2 - 1, n // 2):
                a.cyl([x, (top + low) / 2, z], 0.026, 0.03, "brass", grp, seg=8)
        a.cyl([0, (top + low) / 2 + 0.05, z], 0.07, 0.012, "brass", grp, seg=14, rot=[90, 0, 0])   # centre medallion
    for sx in (-1, 1):   # angle-iron side rails and spring links
        a.b([sx * (W / 2 - 0.04), 0.4, 0], [0.035, 0.035, L - 0.06], "iron_enamel", "frame")
        a.b([sx * (W / 2 - 0.06), 0.385, 0], [0.006, 0.006, L - 0.1], "iron", "frame")
    for z in (-0.6, 0.0, 0.6):
        a.b([0, 0.395, z], [W - 0.1, 0.008, 0.02], "iron", "frame")
    # bedding: mattress with a rolled edge, sheet turned down over the blanket, the blanket's sides hanging in folds
    a.r([0, 0.5, 0.02], [W - 0.08, 0.16, L - 0.1], "ticking", "mattress", 0.05)
    a.r([0, 0.585, -0.1], [W - 0.06, 0.02, L - 0.4], "blanket_grey", "blanket", 0.01)
    a.r([0, 0.598, -0.42], [W - 0.06, 0.016, 0.22], "linen", "blanket", 0.008)                  # sheet turn-down
    a.cyl([0, 0.6, -0.31], 0.035, W - 0.05, "linen", "blanket", dz=0.025, seg=10, rot=[0, 0, 90])   # rolled edge of the turn-down
    for sx in (-1, 1):
        for i, (z, rx) in enumerate(((-0.5, 3), (-0.05, -2), (0.4, 4))):   # side drape: three hanging folds
            a.r([sx * (W / 2 - 0.02), 0.47, z], [0.02, 0.24, 0.48], "blanket_grey", "blanket", 0.009, rot=[rx, 0, sx * 6])
            a.cyl([sx * (W / 2 - 0.015), 0.47, z + 0.22], 0.03, 0.22, "blanket_grey", "blanket", dz=0.02, seg=8, rot=[0, 0, sx * 6])
    for z, rz in ((0.15, 1.5), (0.45, -2.0), (0.65, 1.0)):   # creases across the blanket
        a.cyl([0.03, 0.597, z], 0.016, W - 0.2, "blanket_grey", "blanket", dz=0.008, seg=6, rot=[0, rz, 90])
    a.r([0, 0.64, -L / 2 + 0.24], [W - 0.22, 0.12, 0.34], "linen", "mattress", 0.055)            # pillow
    a.r([0.04, 0.705, -L / 2 + 0.25], [W - 0.4, 0.02, 0.2], "linen", "mattress", 0.01, rot=[0, 4, 0])   # its dent
    a.col([0, 0.3, 0], [W, 0.6, L])
    a.col([0, 0.85, -L / 2 + 0.02], [W, 0.6, 0.05])
    return gx2_write(a, root, [W, L], fragments=["foot", "blanket"], keep=["frame", "mattress", "head"])


def gramophone(root):
    a = A("furn_gramophone", "Gramophone", "Horn gramophone: an oak cabinet with a felt turntable, a shellac record, a nickel tonearm and soundbox, "
          "a winding crank and a fluted brass horn on its elbow.", "wood", 3, 8, blocking=False, front=0.0)
    a.r([0, 0.07, 0], [0.34, 0.14, 0.34], "oak", "case", 0.008)
    a.r([0, 0.145, 0], [0.36, 0.014, 0.36], "oak", "case", 0.005)                               # moulded lid edge
    a.r([0, 0.07, 0.172], [0.26, 0.08, 0.006], "oak_dark", "case", 0.003)                      # front panel
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.cyl([sx * 0.15, 0.009, sz * 0.15], 0.03, 0.018, "oak_dark", "case", top=0.7, seg=8, rot=[180, 0, 0])
    a.cyl([0.02, 0.158, 0.02], 0.27, 0.012, "felt_green", "deck", seg=24)
    a.cyl([0.02, 0.166, 0.02], 0.25, 0.004, "record", "deck", seg=24)
    a.cyl([0.02, 0.169, 0.02], 0.08, 0.002, "cover_red", "deck", seg=16)                        # label
    a.cyl([0.02, 0.175, 0.02], 0.008, 0.02, "zinc", "deck", seg=6)                               # spindle
    a.cyl([-0.12, 0.17, -0.12], 0.04, 0.03, "zinc", "arm", seg=10)                               # tonearm pivot
    tube(a, [-0.12, 0.185, -0.12], [0.05, 0.19, 0.05], 0.018, "zinc", "arm")
    a.cyl([0.06, 0.18, 0.06], 0.05, 0.016, "zinc", "arm", seg=12, rot=[90, 45, 0])              # soundbox
    a.cyl([0.19, 0.08, 0.0], 0.03, 0.03, "zinc", "case", seg=8, rot=[0, 0, 90])                   # crank
    a.r([0.21, 0.06, 0.0], [0.012, 0.06, 0.012], "zinc", "case", 0.004)
    a.cyl([0.21, 0.035, 0.025], 0.018, 0.04, "oak_dark", "case", seg=8, rot=[90, 0, 0])
    # elbow from the back corner, then the horn flaring up and forward (axis tilted 52 degrees toward +z)
    tube(a, [-0.12, 0.19, -0.13], [-0.12, 0.3, -0.13], 0.03, "brass", "horn")
    ax, base = [0, math.sin(math.radians(52)), math.cos(math.radians(52))], [-0.12, 0.31, -0.12]
    t, d = 0.0, 0.035
    for i, (h, grow) in enumerate(((0.06, 1.6), (0.07, 1.5), (0.08, 1.45), (0.08, 1.4), (0.07, 1.35), (0.05, 1.25))):
        c = [base[k] + ax[k] * (t + h / 2) for k in range(3)]
        a.cyl(c, d * grow, h, "brass", "horn", top=1.0 / grow, n=12, rot=[180 + 52, 0, 0])
        t += h
        d *= grow
    c = [base[k] + ax[k] * (t + 0.008) for k in range(3)]
    a.cyl(c, d * 1.04, 0.012, "brass", "horn", n=12, rot=[52, 0, 0])                            # rolled rim
    c2 = [base[k] + ax[k] * (t - 0.004) for k in range(3)]
    a.cyl(c2, d * 0.9, 0.006, "record", "horn", n=12, rot=[52, 0, 0])                           # the dark throat inside the bell
    c3 = [base[k] + ax[k] * (t - 0.03) for k in range(3)]
    a.cyl(c3, d * 0.55, 0.006, "record", "horn", n=12, rot=[52, 0, 0])
    return gx2_write(a, root, [0.36, 0.36], fragments=["horn"], keep=["case", "deck"])


def radio(root):
    a = A("furn_radio", "Wireless set", "Walnut cathedral-top wireless receiver (1930s): arched cabinet, fretwork over the speaker cloth, "
          "an amber dial and three bakelite knobs.", "wood", 2, 8, blocking=False, front=0.0)
    W, D = 0.34, 0.2
    a.r([0, 0.12, 0], [W, 0.24, D], "walnut", "case", 0.01)
    a.arch([0, 0.24, 0], [W, 0.15, D], "walnut", "case", th=0.3, fill=True)                       # cathedral top
    a.r([0, 0.012, 0], [W + 0.02, 0.024, D + 0.02], "walnut", "case", 0.006)                      # plinth
    a.r([0, 0.22, D / 2 + 0.002], [0.22, 0.14, 0.004], "grille_cloth", "front", 0.002)
    a.arch([0, 0.29, D / 2 + 0.002], [0.22, 0.09, 0.004], "grille_cloth", "front", th=0.3, fill=True)
    for x in (-0.055, 0.0, 0.055):   # fretwork bars
        a.r([x, 0.26, D / 2 + 0.006], [0.012, 0.2, 0.008], "walnut", "front", 0.003)
    a.arch([0, 0.29, D / 2 + 0.006], [0.18, 0.07, 0.008], "walnut", "front", th=0.012)
    a.r([0, 0.09, D / 2 + 0.003], [0.11, 0.04, 0.006], "dial", "front", 0.004)                   # dial window
    a.r([0, 0.09, D / 2 + 0.006], [0.115, 0.045, 0.003], "glass", "glass", 0.004)
    a.r([0.01, 0.092, D / 2 + 0.007], [0.002, 0.03, 0.002], "cover_red", "front", 0.0005)         # needle
    for x in (-0.1, 0.0, 0.1):
        a.cyl([x, 0.035, D / 2 + 0.012], 0.032, 0.02, "bakelite", "front", seg=12, rot=[90, 0, 0])
    return gx2_write(a, root, [W + 0.02, D + 0.02], fragments=["front"], keep=["case"], glass="glass")


def sconce(root):
    a = A("furn_sconce", "Wall sconce", "Electric wall light: a brass back plate, a scrolled arm and a frosted tulip shade over one bulb.",
          "glass", 1, 6, blocking=False, wall=True, front=0.0, mount=1.68)
    a.prop["hang"] = False
    a.cyl([0, 0.02, 0.008], 0.11, 0.016, "brass", "plate", seg=16, rot=[90, 0, 0])
    a.cyl([0, 0.02, 0.018], 0.06, 0.012, "brass", "plate", seg=12, rot=[90, 0, 0])
    tube(a, [0, 0.02, 0.02], [0, -0.04, 0.07], 0.014, "brass", "plate")
    tube(a, [0, -0.04, 0.07], [0, 0.06, 0.13], 0.014, "brass", "plate")
    a.cyl([0, 0.07, 0.13], 0.04, 0.03, "brass", "plate", top=0.75, seg=12, rot=[180, 0, 0])        # bulb holder cup
    a.r([0, 0.1, 0.13], [0.045, 0.05, 0.045], "bulb", "glass", 0.02)
    a.cyl([0, 0.135, 0.13], 0.14, 0.1, "opal_glass", "glass", top=0.45, n=10, rot=[180, 0, 0])     # fluted tulip, wide at the top
    a.cyl([0, 0.186, 0.13], 0.142, 0.006, "opal_glass", "glass", n=10)                             # its crimped rim
    a.light = {"color": [1.0, 0.72, 0.42], "intensity": 0.6, "radius": 4.5, "offset": [0, 0.11, 0.14], "group": "glass", "use": "interior"}
    return gx2_write(a, root, [0.15, 0.2], fragments=[], keep=["plate"], glass="glass")


def sconce_candle(root):
    a = A("furn_sconce_candle", "Candle sconce", "Brass girandole: a repoussé back plate with two scrolled arms, drip pans and tallow candles.",
          "metal", 0, 6, blocking=False, wall=True, front=0.0, mount=1.55)
    a.r([0, 0.12, 0.006], [0.12, 0.26, 0.012], "brass", "plate", 0.05)
    a.cyl([0, 0.12, 0.014], 0.08, 0.008, "brass", "plate", seg=14, rot=[90, 0, 0])
    a.r([0, 0.24, 0.012], [0.05, 0.05, 0.016], "brass", "plate", 0.022)                            # crest
    for sx in (-1, 1):
        tube(a, [0, 0.07, 0.016], [sx * 0.11, 0.06, 0.08], 0.012, "brass", "plate")
        tube(a, [sx * 0.11, 0.06, 0.08], [sx * 0.13, 0.11, 0.1], 0.012, "brass", "plate")
        a.cyl([sx * 0.13, 0.118, 0.1], 0.06, 0.012, "brass", "plate", top=1.0, seg=12)               # drip pan
        h = 0.11 if sx < 0 else 0.07
        a.cyl([sx * 0.13, 0.124 + h / 2, 0.1], 0.022, h, "wax", "candles", seg=10)
        a.r([sx * 0.13, 0.124 + h + 0.014, 0.1], [0.008, 0.026, 0.004], "flame", "candles", 0.0018)
    a.light = {"color": [1.0, 0.62, 0.3], "intensity": 0.2, "radius": 3.0, "offset": [0, 0.25, 0.11], "flicker": 0.28, "group": "candles", "use": "interior"}
    return gx2_write(a, root, [0.32, 0.12], fragments=[], keep=["plate"])


def wall_oil_lamp(root):
    a = A("furn_lamp_wall_oil", "Wall oil lamp", "Kerosene wall lamp: a cast-iron bracket holding a glass font with a brass burner and chimney "
          "in front of a silvered reflector.", "glass", 1, 6, blocking=False, wall=True, front=0.0, mount=1.5)
    a.cyl([0, 0.2, 0.012], 0.2, 0.012, "mirror", "reflector", seg=20, rot=[90, 0, 0])               # reflector disc
    a.cyl([0, 0.2, 0.006], 0.215, 0.012, "brass", "reflector", seg=20, rot=[90, 0, 0])
    a.wedge([0, 0.02, 0.05], [0.03, 0.06, 0.09], "cast_iron", "bracket", top=0.25, rot=[180, 180, 0])
    a.cyl([0, 0.055, 0.12], 0.1, 0.014, "cast_iron", "bracket", seg=14)                             # holder ring
    a.cyl([0, 0.1, 0.12], 0.09, 0.07, "glass", "glass", top=0.7, seg=14, rot=[180, 0, 0])          # font
    a.cyl([0, 0.145, 0.12], 0.05, 0.016, "brass", "glass", seg=12)                                  # burner
    a.r([0, 0.165, 0.12], [0.012, 0.03, 0.006], "flame", "glass", 0.002)
    a.cyl([0, 0.2, 0.12], 0.055, 0.09, "glass", "glass", top=0.6, seg=14)                           # chimney bulb
    a.cyl([0, 0.27, 0.12], 0.034, 0.06, "glass", "glass", seg=12)
    # no collider: it hangs beside stairs and doorways at head height (cosmetic, like the sconces)
    a.light = {"color": [1.0, 0.66, 0.34], "intensity": 0.45, "radius": 4.0, "offset": [0, 0.18, 0.13], "flicker": 0.1, "group": "glass", "use": "interior"}
    return gx2_write(a, root, [0.22, 0.2], fragments=[], keep=["bracket", "reflector"], glass="glass")


def candlestick(root):
    a = A("furn_candlestick", "Candlestick", "Tall turned brass candlestick with a half-burnt beeswax candle.", "metal", 0, 6, blocking=False, front=0.0)
    a.cyl([0, 0.01, 0], 0.11, 0.02, "brass", "stick", top=0.7, seg=16)
    a.cyl([0, 0.035, 0], 0.06, 0.03, "brass", "stick", top=0.6, seg=14)
    for y, d in ((0.06, 0.04), (0.13, 0.036), (0.2, 0.034)):   # stem with knops
        a.cyl([0, y + 0.035, 0], 0.02, 0.07, "brass", "stick", top=0.85, seg=10)
        a.r([0, y, 0], [d, 0.026, d], "brass", "stick", 0.012)
    a.cyl([0, 0.27, 0], 0.06, 0.012, "brass", "stick", seg=14)
    a.cyl([0, 0.33, 0], 0.024, 0.12, "wax", "candle", seg=10)
    a.r([0.009, 0.36, 0.01], [0.008, 0.04, 0.006], "wax", "candle", 0.003)
    a.r([0, 0.404, 0], [0.008, 0.026, 0.004], "flame", "candle", 0.0018)
    a.light = {"color": [1.0, 0.62, 0.3], "intensity": 0.14, "radius": 2.6, "offset": [0, 0.41, 0], "flicker": 0.3, "group": "candle", "use": "interior"}
    return gx2_write(a, root, [0.11, 0.11], fragments=[], keep=["stick"])


def hall_pendant(root):
    a = A("furn_lamp_pendant_glass", "Hall pendant", "Hall pendant lamp: a brass ceiling cup, a short chain, a pierced brass gallery and an opal-glass "
          "bowl round one bulb. Hangs from a ceiling or beam (origin at the top).", "glass", 0, 10, blocking=False, front=0.0)
    a.prop["hang"] = True
    a.cyl([0, -0.015, 0], 0.11, 0.03, "brass", "rose", top=0.6, seg=16, rot=[180, 0, 0])
    for i in range(7):   # chain links, alternating planes
        a.arch([0, -0.04 - i * 0.034, 0], [0.024, 0.04, 0.006], "brass", "rose", th=0.006, rot=[0, 90 * (i % 2), 0])
    a.cyl([0, -0.29, 0], 0.05, 0.03, "brass", "gallery", top=0.6, seg=12, rot=[180, 0, 0])
    a.cyl([0, -0.33, 0], 0.2, 0.05, "brass", "gallery", top=0.55, n=12)                             # pierced gallery (12 sides)
    a.cyl([0, -0.36, 0], 0.26, 0.016, "brass", "gallery", seg=24)
    a.cyl([0, -0.41, 0], 0.27, 0.09, "opal_glass", "glass", top=0.82, seg=24)                        # the bowl: upper...
    a.cyl([0, -0.48, 0], 0.22, 0.05, "opal_glass", "glass", top=0.6, seg=24, rot=[180, 0, 0])        # ...and its rounded base
    a.r([0, -0.405, 0], [0.06, 0.06, 0.06], "bulb", "glass", 0.026)
    a.cyl([0, -0.515, 0], 0.03, 0.03, "brass", "gallery", top=0.4, seg=8, rot=[180, 0, 0])           # finial
    a.col([0, -0.43, 0], [0.26, 0.14, 0.26])
    a.light = {"color": [1.0, 0.75, 0.48], "intensity": 1.3, "radius": 7.0, "offset": [0, -0.43, 0], "cone": [55, 100], "dir": [0, -1, 0], "shadow": 1, "group": "glass", "use": "interior"}
    return gx2_write(a, root, [0.27, 0.27], fragments=["glass"], keep=["rose", "gallery"], glass="glass")


def coat_stand(root):
    a = A("furn_coat_stand", "Coat stand", "Bentwood hall stand by the door: four splayed feet, a drip ring, a turned pole crowned with hooks; a greatcoat, "
          "a jacket, a felt hat and a furled umbrella.", "wood", 10, 30, wall=True, front=0.3)
    a.cyl([0, 0.95, 0], 0.045, 1.8, "walnut", "pole", top=0.75, seg=12)
    a.r([0, 1.86, 0], [0.06, 0.06, 0.06], "walnut", "pole", 0.028)
    for k in range(4):   # splayed bentwood feet and the drip ring
        ang = math.radians(45 + 90 * k)
        tube(a, [0, 0.3, 0], [math.cos(ang) * 0.27, 0.02, math.sin(ang) * 0.27], 0.03, "walnut", "pole")
        a.r([math.cos(ang) * 0.28, 0.01, math.sin(ang) * 0.28], [0.04, 0.02, 0.04], "walnut", "pole", 0.009)
    a.cyl([0, 0.42, 0], 0.4, 0.02, "walnut", "pole", seg=20)
    a.cyl([0, 0.42, 0], 0.36, 0.022, "walnut", "pole", seg=20)
    for k in range(4):   # bentwood hooks at the crown
        yaw = 90 * k
        a.arch([math.sin(math.radians(yaw)) * 0.1, 1.72, math.cos(math.radians(yaw)) * 0.1], [0.14, 0.1, 0.02], "walnut", "pole", th=0.02, rot=[0, yaw + 90, 0])
    # greatcoat hanging off the front hook: shoulders, a long body in three folds, sleeves
    a.r([0, 1.6, 0.16], [0.42, 0.14, 0.16], "coat_wool", "coats", 0.06)
    for i, (x, rz) in enumerate(((-0.12, 3), (0.0, -1), (0.12, -3))):
        a.r([x, 1.02, 0.17 + 0.012 * (i % 2)], [0.16, 1.05, 0.09], "coat_wool", "coats", 0.04, rot=[3, 0, rz])
    for sx in (-1, 1):
        a.r([sx * 0.2, 1.25, 0.15], [0.08, 0.6, 0.08], "coat_wool", "coats", 0.035, rot=[0, 0, sx * 4])
    a.r([0.0, 1.58, 0.24], [0.1, 0.12, 0.04], "coat_wool", "coats", 0.018)                           # collar
    # a jacket on the side hook
    a.r([0.17, 1.38, -0.02], [0.12, 0.6, 0.3], "coat_blue", "coats", 0.04, rot=[0, 0, -5])
    a.r([0.15, 1.6, -0.02], [0.13, 0.1, 0.3], "coat_blue", "coats", 0.04)
    # felt hat on top, umbrella in the ring
    a.cyl([0, 1.893, 0], 0.3, 0.012, "felt_hat", "hat", seg=20)
    a.cyl([0, 1.945, 0], 0.17, 0.1, "felt_hat", "hat", top=0.85, seg=16)
    a.cyl([0, 1.912, 0], 0.175, 0.025, "scarf", "hat", seg=16)
    tube(a, [-0.12, 0.43, -0.1], [-0.17, 1.15, -0.13], 0.045, "umbrella", "umbrella")
    a.arch([-0.175, 1.18, -0.13], [0.06, 0.05, 0.014], "walnut", "umbrella", th=0.014, rot=[0, 30, 0])
    a.col([0, 0.95, 0], [0.5, 1.9, 0.5])
    return gx2_write(a, root, [0.56, 0.56], fragments=["coats", "hat", "umbrella"], keep=["pole"])


def coat_hooks(root):
    a = A("furn_coat_hooks", "Coat hooks", "Oak hook board with three iron double hooks: a long greatcoat in folds, a work jacket and a knitted scarf.",
          "cloth", 4, 10, blocking=False, wall=True, front=0.0, mount=1.6)
    a.r([0, 0.0, 0.012], [0.8, 0.1, 0.024], "oak_dark", "board", 0.008)
    a.r([0, 0.055, 0.016], [0.82, 0.016, 0.032], "oak_dark", "board", 0.004)                         # moulded top edge
    for x in (-0.27, 0.0, 0.27):
        a.cyl([x, 0.0, 0.026], 0.03, 0.006, "iron", "board", seg=10, rot=[90, 0, 0])
        tube(a, [x, -0.01, 0.026], [x, 0.03, 0.09], 0.01, "iron", "board", seg=6)
        tube(a, [x, -0.01, 0.026], [x, -0.03, 0.07], 0.01, "iron", "board", seg=6)
        a.r([x, 0.032, 0.092], [0.016, 0.016, 0.016], "iron", "board", 0.007)
    for i, (x, rz) in enumerate(((-0.33, 4), (-0.27, 0), (-0.2, -3))):   # greatcoat, three folds
        a.r([x, -0.52, 0.065 + 0.012 * (i % 2)], [0.13, 0.95, 0.08], "coat_wool", "coats", 0.035, rot=[2, 0, rz])
    a.r([-0.27, -0.06, 0.07], [0.3, 0.1, 0.1], "coat_wool", "coats", 0.04)
    a.r([0.0, -0.38, 0.07], [0.32, 0.68, 0.09], "coat_blue", "coats", 0.04, rot=[0, 0, 2])          # jacket
    a.r([0.0, -0.06, 0.07], [0.3, 0.1, 0.1], "coat_blue", "coats", 0.04)
    for sx, h in ((-1, 0.55), (1, 0.4)):   # scarf over the right hook, both ends hanging
        a.r([0.27 + sx * 0.035, -h / 2 + 0.02, 0.08], [0.06, h, 0.014], "scarf", "scarf", 0.006, rot=[0, 0, sx * 3])
    a.cyl([0.27, 0.025, 0.08], 0.05, 0.08, "scarf", "scarf", dz=0.03, seg=8, rot=[0, 0, 90])
    return gx2_write(a, root, [0.82, 0.12], fragments=["coats", "scarf"], keep=["board"])


def suitcases(root):
    a = A("furn_suitcases", "Suitcases", "Two leather suitcases stacked: brass corners, straps and handles; the smaller one set askew on the larger.",
          "cloth", 6, 20, front=0.3)
    for i, (W, H, D, y, yaw, m) in enumerate(((0.7, 0.2, 0.44, 0.1, 0, "leather_brown"), (0.56, 0.16, 0.36, 0.28, 11, "leather_tan"))):
        g = f"case{i}"
        R = [0, yaw, 0]
        a.r([0, y, 0], [W, H, D], m, g, 0.025, rot=R)
        q = quat_mat(R)
        rotp = lambda v: [sum(q[r][c] * v[c] for c in range(3)) for r in range(3)]
        for sx in (-1, 1):
            o = rotp([sx * W * 0.28, 0, 0])
            a.r([o[0], y, o[2]], [0.035, H + 0.006, D + 0.006], "leather_brown" if m == "leather_tan" else "leather_tan", g, 0.006, rot=R)   # strap
            for sz in (-1, 1):
                for sy in (-1, 1):
                    o2 = rotp([sx * (W / 2 - 0.02), sy * (H / 2 - 0.02), sz * (D / 2 - 0.02)])
                    a.r([o2[0], y + o2[1], o2[2]], [0.05, 0.05, 0.05], "brass", g, 0.012, rot=R)
        oh = rotp([0, 0, D / 2 + 0.012])
        a.arch([oh[0], y + 0.015, oh[2]], [0.12, 0.04, 0.02], "leather_brown", g, th=0.018, rot=[90, yaw, 0])   # handle
        a.r([0, y + H / 2 - 0.02, 0], [W - 0.04, 0.004, D + 0.004], "brass" if i == 0 else "leather_brown", g, 0.002, rot=R)   # lid seam
    a.col([0, 0.18, 0], [0.72, 0.36, 0.46])
    return gx2_write(a, root, [0.72, 0.46], fragments=["case1"], keep=["case0"], surfaces=[[-0.2, -0.12, 0.2, 0.12, 0.36]])


def curtains_half(root):
    a = A("furn_curtains_half", "Curtains (half-drawn)", "Checked cotton curtains on an iron rod: one drawn half across the window in deep pleats, "
          "the other gathered to the side with a tie-back.", "cloth", 0, 8, blocking=False, wall=True, front=0.0, mount=0.7)
    a.cyl([0, 1.5, 0.06], 0.02, 1.5, "iron", "rod", seg=8, rot=[0, 0, 90])
    for sx in (-1, 1):
        a.r([sx * 0.765, 1.5, 0.06], [0.036, 0.036, 0.036], "iron", "rod", 0.017)
        a.r([sx * 0.7, 1.5, 0.03], [0.012, 0.012, 0.06], "iron", "rod", 0.003)
    for i in range(5):   # left: gathered with a tie-back
        x = -(0.64 - i * 0.04)
        a.cyl([x, 0.78, 0.06 + 0.01 * (i % 2)], 0.05, 1.44, "curtain", "side0", dz=0.022 + 0.004 * i, seg=8)
    a.cyl([-0.56, 0.62, 0.075], 0.2, 0.025, "curtain", "side0", dz=0.05, seg=10)
    for i in range(10):   # right: drawn across to the middle, loose pleats, the leading edge swinging out a little
        x = 0.7 - i * 0.075
        a.cyl([x, 0.775, 0.06 + 0.014 * (i % 2)], 0.085, 1.45, "curtain", "side1", dz=0.026 + 0.006 * (i % 3), seg=8, rot=[0, 0, 0.6 * (i - 5) * 0.3])
        a.cyl([x, 1.47, 0.062], 0.02, 0.025, "iron", "rod", seg=6, rot=[0, 0, 90])                    # rod rings
    return gx2_write(a, root, [1.5, 0.08], fragments=[], keep=["rod"])


def pan_rail(root):
    a = A("furn_pan_rail", "Pan rail", "Oak rail over the range with S-hooks: two cast-iron skillets, a saucepan, a copper stockpot lid, a ladle and a sieve.",
          "metal", 0, 25, blocking=False, wall=True, front=0.0, mount=1.55)
    W = 1.2
    a.r([0, 0.0, 0.015], [W, 0.08, 0.03], "oak_dark", "rail", 0.008)
    tube(a, [-W / 2 + 0.04, -0.02, 0.06], [W / 2 - 0.04, -0.02, 0.06], 0.016, "iron", "rail")
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), -0.02, 0.04], [0.02, 0.03, 0.04], "iron", "rail", 0.005)
    items = [(-0.44, "skillet", 0.3), (-0.13, "skillet", 0.24), (0.14, "saucepan", 0.18), (0.36, "lid", 0.26), (0.52, "ladle", 0.0)]
    for x, kind, d in items:
        a.arch([x, -0.035, 0.06], [0.03, 0.03, 0.005], "iron", "pans", th=0.005, rot=[180, 0, 0])      # S-hook
        if kind in ("skillet", "saucepan"):
            hl = 0.22 if kind == "skillet" else 0.16
            a.r([x, -0.06 - hl / 2, 0.06], [0.024, hl, 0.01], "cast_iron" if kind == "skillet" else "copper", "pans", 0.005)   # handle
            cy = -0.06 - hl - d / 2
            a.cyl([x, cy, 0.05], d, 0.045 if kind == "skillet" else 0.1, "cast_iron" if kind == "skillet" else "copper", "pans",
                  top=0.82, seg=20, rot=[-90, 0, 0])
        elif kind == "lid":
            a.cyl([x, -0.06 - d / 2, 0.035], d, 0.02, "copper", "pans", top=0.4, seg=20, rot=[-90, 0, 0])
            a.r([x, -0.06 - d / 2, 0.06], [0.05, 0.02, 0.02], "brass", "pans", 0.008)
        else:
            a.cyl([x, -0.2, 0.06], 0.014, 0.3, "zinc", "pans", seg=6)
            bowl(a, x, -0.42, 0.06, 0.09, 0.05, "zinc", "pans")
    return gx2_write(a, root, [W, 0.14], fragments=["pans"], keep=["rail"])


def debris_glass(root):
    a = A("furn_debris_glass", "Broken glass", "Shards of window glass and a split glazing bar on the floor.", "glass", 0, 1, blocking=False, front=0.0)
    shards = [(0.0, 0.0, 10, 0.12), (0.15, 0.08, 75, 0.08), (-0.12, 0.1, 140, 0.1), (0.08, -0.14, 220, 0.07), (-0.2, -0.06, 300, 0.09),
              (0.25, -0.05, 35, 0.05), (-0.05, 0.22, 190, 0.06), (0.3, 0.15, 260, 0.04), (-0.28, 0.18, 110, 0.05)]
    for x, z, yaw, s in shards:
        a.wedge([x, 0.003, z], [s, s * 0.7, 0.004], "shard", "glass", top=0.1, rot=[90, yaw, 0])
    a.r([0.05, 0.012, -0.02], [0.32, 0.022, 0.03], "pine", "bar", 0.004, rot=[0, 28, 0])
    return gx2_write(a, root, [0.6, 0.5], fragments=[], keep=[])


def debris_plaster(root):
    a = A("furn_debris_plaster", "Plaster debris", "Fallen ceiling plaster: chunks of lime plaster with the lath still in them, a broken brick and a "
          "dust patch spread across the boards.", "masonry", 0, 1, blocking=False, front=0.0)
    a.r([0, 0.002, 0], [1.1, 0.004, 0.8], "plaster_dust", "dust", 0.0015, rot=[0, 12, 0])
    a.r([0.25, 0.002, 0.25], [0.5, 0.003, 0.4], "plaster_dust", "dust", 0.0012, rot=[0, -30, 0])
    chunks = [(0.0, 0.0, 0.16, 0.025, 0.11, 15, 8), (0.18, 0.12, 0.1, 0.02, 0.08, 70, -6), (-0.2, 0.15, 0.13, 0.03, 0.09, 140, 4),
              (-0.12, -0.18, 0.08, 0.018, 0.07, 200, 10), (0.3, -0.12, 0.07, 0.02, 0.05, 260, -3), (-0.35, -0.05, 0.06, 0.02, 0.05, 320, 6),
              (0.1, 0.3, 0.05, 0.016, 0.04, 30, 12), (-0.05, 0.08, 0.04, 0.015, 0.035, 85, 0)]
    for x, z, w, h, d, yaw, tilt in chunks:
        a.r([x, h / 2 + 0.002 + 0.5 * d * abs(math.sin(math.radians(tilt))), z], [w, h, d], "plaster_chunk", "chunks", 0.006, rot=[tilt, yaw, 0])
    for x, z, yaw in ((-0.02, 0.02, 20), (0.05, -0.04, 25), (-0.25, 0.1, 160)):   # lath strips
        a.b([x, 0.02, z], [0.36, 0.008, 0.03], "lath", "chunks", rot=[0, yaw, 3])
    a.r([0.22, 0.035, -0.22], [0.22, 0.065, 0.1], "brick_red", "chunks", 0.01, rot=[0, 35, 0])
    a.r([0.1, 0.036, -0.3], [0.1, 0.05, 0.1], "brick_red", "chunks", 0.012, rot=[8, 70, 12])
    return gx2_write(a, root, [1.1, 0.8], fragments=[], keep=[])


def framed_painting(root, id, name, W, H, mount, scene, frame="gilt"):
    """A framed oil painting W x H (outer) hung with its centre near 1.6 m: a moulded frame (outer bead, flat, inner
    slip), the canvas, and the scene laid on it as thin painted shapes (sky, land or sea, trees, a figure)."""
    a = A(id, name, f"Framed oil painting ({scene}), {W:.2f} x {H:.2f} m, hung on a wall.", "wood", 2, 6, blocking=False, wall=True, front=0.0, mount=mount)
    cy = H / 2 + 0.02
    fw = 0.06 if W > 0.6 else 0.045
    for sy in (-1, 1):   # frame: outer bead, flat and inner slip on the four sides
        a.r([0, cy + sy * (H / 2 - fw / 2), 0.02], [W, fw, 0.04], frame, "frame", 0.01)
        a.r([0, cy + sy * (H / 2 - fw - 0.006), 0.026], [W - 2 * fw, 0.012, 0.02], "oak_dark" if frame == "gilt" else "gilt", "frame", 0.004)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - fw / 2), cy, 0.02], [fw, H, 0.04], frame, "frame", 0.01)
        a.r([sx * (W / 2 - fw - 0.006), cy, 0.026], [0.012, H - 2 * fw, 0.02], "oak_dark" if frame == "gilt" else "gilt", "frame", 0.004)
    cw, ch = W - 2 * fw - 0.02, H - 2 * fw - 0.02
    z0 = 0.012
    # the scene: layers of thin painted shapes, each a little in front of the last (brush texture from the canvas
    # materials' fabric pattern): graded sky, clouds, distant hills, land or sea, tree clumps in two shades
    L = [z0]
    def lay():
        L[0] += 0.00045   # every layer stays behind the frame slip (canvas at 1.2 cm, slip at 3.6 cm)
        return L[0]
    x0, x1, y0, y1 = -cw / 2, cw / 2, cy - ch / 2, cy + ch / 2
    hz = cy - ch * 0.1   # the horizon
    def band(ya, yb, m):
        a.b([0, (ya + yb) / 2, lay()], [cw, yb - ya, 0.002], m, "canvas")
    def blob(x, y, w, h, m):   # a soft elliptical dab, kept inside the canvas
        w = min(w, 2 * min(x - x0, x1 - x) - 0.004); h = min(h, 2 * min(y - y0, y1 - y) - 0.004)
        if w > 0.004 and h > 0.004:
            a.cyl([x, y, lay()], w, 0.002, m, "canvas", dz=h, seg=16, rot=[90, 0, 0])
    if scene == "portrait":
        band(y0, y1, "canvas_dark")
        blob(0, cy + ch * 0.08, cw * 0.8, ch * 0.75, "canvas_glow")                     # lit background behind the sitter
        a.arch([0, y0, lay()], [cw * 0.86, ch * 0.34, 0.002], "coat_blue", "canvas", th=1.0, fill=True)   # shoulders
        blob(0, y0 + ch * 0.33, cw * 0.18, ch * 0.08, "linen")                          # collar
        blob(0, cy + ch * 0.12, cw * 0.34, ch * 0.34, "canvas_skin")                    # face
        blob(0, cy + ch * 0.25, cw * 0.38, ch * 0.16, "canvas_hair")                    # hair
        blob(-cw * 0.06, cy + ch * 0.14, cw * 0.04, ch * 0.025, "canvas_hair")         # eyes
        blob(cw * 0.06, cy + ch * 0.14, cw * 0.04, ch * 0.025, "canvas_hair")
    elif scene == "dusk":
        band(hz, y1, "canvas_dusk_hi"); band(hz, hz + ch * 0.38, "canvas_dusk"); band(hz, hz + ch * 0.14, "canvas_dusk_lo")
        blob(cw * 0.12, hz + ch * 0.08, cw * 0.16, cw * 0.16, "canvas_sun")
        for u, v, w in ((-0.25, 0.32, 0.5), (0.2, 0.4, 0.4)):
            blob(u * cw, hz + v * ch, w * cw, ch * 0.05, "canvas_dusk_lo")
        band(y0, hz + 0.004, "canvas_tree"); band(hz - ch * 0.1, hz + 0.004, "canvas_field")
        blob(-cw * 0.2, hz, cw * 0.9, ch * 0.1, "canvas_hill")                           # a low hill
        for u, s in ((-0.32, 0.26), (0.3, 0.2)):                                         # trees against the glow
            a.b([u * cw, hz + s * ch * 0.05, lay()], [cw * 0.012, s * ch * 0.4, 0.002], "canvas_dark", "canvas")
            blob(u * cw, hz + s * ch * 0.45, s * cw * 0.7, s * ch * 0.9, "canvas_tree")
            blob(u * cw + s * cw * 0.12, hz + s * ch * 0.6, s * cw * 0.35, s * ch * 0.45, "canvas_dusk")
    else:
        sea = scene == "sea"
        band(hz, y1, "canvas_sky_hi"); band(hz, hz + ch * 0.4, "canvas_sky"); band(hz, hz + ch * 0.15, "canvas_sky_lo")
        for u, v, w in ((-0.25, 0.3, 0.42), (0.18, 0.38, 0.5), (0.3, 0.22, 0.3)):       # clouds
            blob(u * cw, hz + v * ch, w * cw, ch * 0.08, "canvas_cloud")
        if sea:
            band(y0, hz, "canvas_sea"); band(hz - ch * 0.12, hz, "canvas_sea_lit")
            band(y0, y0 + ch * 0.14, "canvas_sand")
            a.wedge([cw * 0.16, hz - ch * 0.05, lay()], [cw * 0.14, ch * 0.04, 0.002], "canvas_dark", "canvas", top=0.6, rot=[0, 0, 180])   # hull
            a.wedge([cw * 0.16, hz + ch * 0.06, lay()], [cw * 0.08, ch * 0.18, 0.002], "paper", "canvas", top=0.0)                          # sail
        else:
            blob(-cw * 0.25, hz, cw * 0.8, ch * 0.18, "canvas_hill"); blob(cw * 0.3, hz, cw * 0.7, ch * 0.12, "canvas_hill")
            band(y0, hz, "canvas_field"); band(y0 + ch * 0.12, hz - ch * 0.08, "canvas_field_lit")
            a.b([cw * 0.06, y0 + ch * 0.18, lay()], [cw * 0.55, ch * 0.05, 0.002], "canvas_sand", "canvas", rot=[0, 0, -8])   # a lane
            for u, s in ((-0.32, 0.24), (-0.14, 0.3), (0.3, 0.26)):                        # tree clumps: trunk, dark crown, lit side
                a.b([u * cw, hz + s * ch * 0.08, lay()], [cw * 0.014, s * ch * 0.35, 0.002], "canvas_dark", "canvas")
                blob(u * cw, hz + s * ch * 0.45, s * cw * 0.7, s * ch * 0.75, "canvas_tree")
                blob(u * cw - s * cw * 0.12, hz + s * ch * 0.55, s * cw * 0.45, s * ch * 0.5, "canvas_tree")
                blob(u * cw + s * cw * 0.1, hz + s * ch * 0.55, s * cw * 0.35, s * ch * 0.4, "canvas_tree_lit")
    if scene != "portrait":   # brush strokes: small seeded dabs in the scene's own tones, more in the foreground
        seed = sum(ord(c) for c in id)
        tones_sky = ["canvas_sky_lo", "canvas_cloud"] if scene != "dusk" else ["canvas_dusk", "canvas_dusk_lo"]
        tones_low = ["canvas_sea_lit", "canvas_sea"] if scene == "sea" else ["canvas_field_lit", "canvas_field"]
        for k in range(14):   # long, flat strokes in tones next to the band under them (no dots)
            r1 = ((seed * 9301 + k * 49297) % 233280) / 233280.0
            r2 = ((seed * 4111 + k * 7919 + 17) % 104729) / 104729.0
            sky = k % 2 == 0
            x = (r1 - 0.5) * cw * 0.8
            y = (hz + (y1 - hz) * (0.15 + 0.7 * r2)) if sky else (y0 + (hz - y0) * (0.1 + 0.7 * r2))
            t = (tones_sky if sky else tones_low)[k % 2]
            blob(x, y, cw * (0.12 + 0.1 * r2), ch * (0.01 + 0.008 * r1), t)
    return gx2_write(a, root, [W, 0.05], fragments=["frame"], keep=[])


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    outs = [
        dining_table(root, "furn_table_dining", "Dining table", 1.7, 0.86, 4, "oak"),
        dining_table(root, "furn_table_small", "Kitchen table", 1.1, 0.7, 3, "pine"),
        side_table(root),
        chair(root, "furn_chair_ladder", "Ladder-back chair", True, "oak"),
        chair(root, "furn_chair_slat", "Slat-back chair", False, "cherry"),
        stool(root), hutch(root, False), hutch(root, True), armoire(root), commode(root), blanket_chest(root), bed(root),
        etagere(root), bench(root), fireplace(root), clock(root),
        crockery(root), jug(root), bottles(root), books(root), lamp(root), crate(root), papers(root), picture(root), curtains(root),
        rug(root, "furn_rug_medallion", "Rug (medallion)", 2.2, 1.5, 0, [0.3, 0.1, 0.07]),
        rug(root, "furn_rug_kilim", "Rug (kilim)", 1.6, 0.9, 1, [0.36, 0.25, 0.14]),
        # V4 second set
        dresser(root), sideboard(root), bookcase(root), washstand(root), stove(root), pantry(root), sacks(root),
        barrel(root, "furn_barrel", "Cask", False), barrel(root, "furn_barrel_rack", "Casks on a cradle", True),
        workbench(root), armchair(root), desk(root), pew(root), school_desk(root), wood_pile(root), hay_bales(root),
        coat_rack(root), wall_clock(root), utensil_rack(root), tool_rack(root), mirror(root), portrait(root),
        wall_shelf(root), plate_rack(root),
        jars(root), bucket(root), basket(root), pot(root), candlesticks(root),
        # L3 lights (docs/lights.md)
        table_lamp(root), candle(root), candelabra(root), ceiling_lamp(root), caged_lamp(root), hearth_fire(root), potbelly_stove(root),
        # GX2 (graphics brief section 3: lived-in interiors)
        bed_iron(root), gramophone(root), radio(root), sconce(root), sconce_candle(root), wall_oil_lamp(root), candlestick(root),
        hall_pendant(root), coat_stand(root), coat_hooks(root), suitcases(root), curtains_half(root), pan_rail(root),
        debris_glass(root), debris_plaster(root),
        rug(root, "furn_rug_runner", "Hall runner", 0.85, 3.2, 1, [0.32, 0.11, 0.07]),
        framed_painting(root, "furn_frame_landscape", "Painting (landscape)", 0.9, 0.62, 1.27, "field"),
        framed_painting(root, "furn_frame_seascape", "Painting (seascape)", 0.7, 0.5, 1.33, "sea"),
        framed_painting(root, "furn_frame_dusk", "Painting (evening)", 0.5, 0.75, 1.2, "dusk", "walnut"),
        framed_painting(root, "furn_frame_portrait", "Painting (portrait)", 0.42, 0.55, 1.3, "portrait", "oak_dark"),
        framed_painting(root, "furn_frame_small", "Painting (small)", 0.36, 0.28, 1.44, "field", "walnut"),
    ]
    for o in outs:
        print("wrote", o.name)


if __name__ == "__main__":
    main()
