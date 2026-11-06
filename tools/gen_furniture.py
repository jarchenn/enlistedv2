"""Furniture family for walls, interiors and furniture (docs/interiors.md): original parametric assets.

Writes content/assets/furn_*.json (category "Furniture"). Deterministic and safe to rerun; it touches no other file.
Every visible piece is a rounded box ("t": "r", 4-10 mm edge radius: small bevels, smooth normals) or a plain box;
wood uses the "joinery" pattern with grain -1 (grain follows each part's longest axis: legs, rails, boards, stiles).
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

    def g(self, name):
        if name not in self.groups:
            self.groups.append(name)
        return self.groups.index(name)

    def r(self, c, s, m, grp, rad=0.006, rot=None):
        rad = min(rad, 0.5 * min(s) - 1e-4)
        p = {"t": "r" if rad > 0.0015 else "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "k": 0, "g": self.g(grp)}
        if p["t"] == "r":
            p["rad"] = round(rad, 4)
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
        """Octagonal plate: two square slabs, one turned 45 degrees about the plate's normal."""
        rr = min(0.004, 0.5 * t - 2e-4)
        if axis == "y":
            self.r(c, [d * 0.83, t, d * 0.83], m, grp, rr, rot=tilt)
            self.r(c, [d * 0.83, t, d * 0.83], m, grp, rr, rot=[(tilt or [0, 0, 0])[0], 45 + (tilt or [0, 0, 0])[1], (tilt or [0, 0, 0])[2]])
        else:   # standing in a rack, facing +z
            rx = (tilt or [0, 0, 0])[0]
            self.r(c, [d * 0.83, d * 0.83, t], m, grp, rr, rot=[rx, 0, 0])
            self.r(c, [d * 0.83, d * 0.83, t], m, grp, rr, rot=[rx, 0, 45])

    def write(self, root, footprint, fragments=(), keep=(), glass=None, surfaces=(), mats=None):
        materials = []
        for n in sorted(self.used | set(self.extra_mats)):
            if n in self.extra_mats:
                alb, rough, pat, grain = self.extra_mats[n]
            else:
                alb, rough, pat, grain = MATS[n]
            materials.append({"name": n, "albedo": alb, "roughness": rough, "pattern": pat, "metallic": 1.0 if n == "brass" else (0.15 if n == "iron" else 0), "grain": grain})
        if not any(m["name"] == "oak" for m in materials) and self.cols:
            alb, rough, pat, grain = MATS["oak"]
            materials.append({"name": "oak", "albedo": alb, "roughness": rough, "pattern": pat, "metallic": 0, "grain": grain})
        self.prop["groups"] = self.groups
        self.prop["fragments"] = [self.groups.index(g) for g in fragments]
        self.prop["keep"] = [self.groups.index(g) for g in keep]
        if glass:
            self.prop["glass"] = self.groups.index(glass)
        self.prop["surfaces"] = [[round(v, 3) for v in s] for s in surfaces]
        doc = {"id": self.id, "version": 1, "name": self.name, "category": "Furniture", "description": self.desc,
               "floors": 0, "enterable": False, "footprint": [round(footprint[0], 3), round(footprint[1], 3)],
               "plinthDepth": 0, "maxGrade": 0.3, "noAutoPanels": True, "materials": materials,
               "parts": self.parts + self.cols, "prop": self.prop}
        out = root / "content" / "assets" / (self.id + ".json")
        out.write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")), encoding="utf-8", newline="")
        return out


# ---------------------------------------------------------------- builders
def leg_tapered(a, x, z, h, top, bot, m, grp):
    """Square leg tapering in two stacked bevelled sections."""
    a.r([x, h * 0.75, z], [top, h * 0.5, top], m, grp, 0.005)
    a.r([x, h * 0.25, z], [(top + bot) * 0.5, h * 0.5, (top + bot) * 0.5], m, grp, 0.005)


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
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        x, z = sx * (W / 2 - 0.05), sz * (D / 2 - 0.05)
        a.r([x, (H - 0.115) * 0.7, z], [0.04, (H - 0.115) * 0.6, 0.04], "cherry", f"leg{i}", 0.005)
        a.r([x, (H - 0.115) * 0.2, z], [0.032, (H - 0.115) * 0.4, 0.032], "cherry", f"leg{i}", 0.005)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "drawer", "leg0"], keep=["leg2", "leg3"], surfaces=[[-W / 2 + 0.04, -D / 2 + 0.04, W / 2 - 0.04, D / 2 - 0.04, H]])


def chair(root, id, name, ladder, wood):
    a = A(id, name, ("Ladder-back chair with a woven rush seat." if ladder else "Slat-back chair with a plank seat.") +
          " Thin square legs, rails and stretchers; the back and seat come off on a blast.", "wood", 8, 20, front=0.45)
    S, H = 0.44, 0.45   # seat size, seat height
    bh = 0.95
    for sx in (-1, 1):   # back legs rise into the back posts, raked slightly
        a.r([sx * (S / 2 - 0.02), bh / 2, -S / 2 + 0.02], [0.034, bh, 0.034], wood, "back", 0.005, rot=[-4, 0, 0])
        a.r([sx * (S / 2 - 0.02), H / 2, S / 2 - 0.02], [0.032, H, 0.032], wood, "legs", 0.005)
    if ladder:
        for i in range(3):
            y = H + 0.17 + i * 0.13
            a.r([0, y, -S / 2 + 0.005 - 0.012 * (i + 1)], [S - 0.04, 0.05, 0.016], wood, "back", 0.005, rot=[-4, 0, 0])
        a.r([0, H - 0.012, 0.01], [S - 0.03, 0.03, S - 0.02], "rush", "seat", 0.008)
    else:
        a.r([0, H + 0.4, -S / 2 + 0.0], [S - 0.04, 0.06, 0.02], wood, "back", 0.005, rot=[-4, 0, 0])
        for i in range(4):
            x = -0.12 + i * 0.08
            a.r([x, H + 0.22, -S / 2 + 0.012], [0.035, 0.32, 0.012], wood, "back", 0.004, rot=[-4, 0, 0])
        a.r([0, H - 0.012, 0.01], [S, 0.026, S - 0.01], wood, "seat", 0.006)
    for sz in (-1, 1):   # seat rails and stretchers
        a.r([0, H - 0.045, sz * (S / 2 - 0.02)], [S - 0.05, 0.035, 0.02], wood, "seat", 0.004)
        a.r([0, 0.16 + 0.04 * (sz > 0), sz * (S / 2 - 0.02)], [S - 0.05, 0.02, 0.02], wood, "legs", 0.004)
    for sx in (-1, 1):
        a.r([sx * (S / 2 - 0.02), H - 0.045, 0], [0.02, 0.035, S - 0.05], wood, "seat", 0.004)
        a.r([sx * (S / 2 - 0.02), 0.2, 0], [0.018, 0.018, S - 0.05], wood, "legs", 0.004)
    a.col([0, H / 2, 0.0], [S, H, S])
    a.col([0, (H + bh) / 2, -S / 2 + 0.02], [S, bh - H, 0.06])
    return a.write(root, [S, S], fragments=["back", "seat"], keep=[])


def stool(root):
    a = A("furn_stool", "Milking stool", "Low three-legged stool: thick pine seat, splayed legs.", "wood", 6, 16, front=0.3)
    a.r([0, 0.32, 0], [0.32, 0.04, 0.32], "pine", "seat", 0.012)
    for i in range(3):
        ang = math.radians(90 + 120 * i)
        x, z = math.cos(ang) * 0.1, math.sin(ang) * 0.1
        a.r([x, 0.15, z], [0.035, 0.32, 0.035], "pine", f"leg{i}", 0.006, rot=[-80 * z, 0, 80 * x])   # splayed outward
    a.col([0, 0.17, 0], [0.32, 0.34, 0.32])
    return a.write(root, [0.34, 0.34], fragments=["seat", "leg0"], keep=[], surfaces=[[-0.12, -0.12, 0.12, 0.12, 0.34]])


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
        a.r([sx * W / 4, H1 - 0.1, D / 2 - 0.01], [W / 2 - 0.05, 0.12, 0.022], wood, f"drawer{i}", 0.005)
        a.r([sx * W / 4, H1 - 0.1, D / 2 + 0.006], [0.07, 0.022, 0.016], "brass", f"drawer{i}", 0.005)
    for i, sx in enumerate((-1, 1)):
        framed_door(a, sx * W / 4, 0.42, D / 2 - 0.01, W / 2 - 0.05, 0.56, wood, f"door{i}")
        a.r([sx * 0.04, 0.48, D / 2 + 0.006], [0.016, 0.05, 0.016], "brass", f"door{i}", 0.005)
    # upper part
    UD, UH, y0 = 0.28, 1.0, H1
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.035), y0 + UH / 2, -D / 2 + UD / 2], [0.03, UH, UD], wood, "upper", 0.006)
    a.r([0, y0 + UH / 2, -D / 2 + 0.01], [W - 0.08, UH, 0.016], wood, "upper", 0.003)
    a.r([0, y0 + UH + 0.03, -D / 2 + UD / 2 + 0.02], [W - 0.02, 0.06, UD + 0.05], wood, "upper", 0.01)   # cornice
    a.r([0, y0 + UH - 0.01, -D / 2 + UD / 2 + 0.03], [W - 0.04, 0.035, UD + 0.03], wood, "upper", 0.008)
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
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.05), 0.04, sz * (D / 2 - 0.05)], [0.07, 0.08, 0.07], wood, "carcass", 0.012)   # feet
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
        a.r([0, y, D / 2 - 0.01], [W - 0.07, 0.18, 0.022], w, f"drawer{i}", 0.006)
        for sx in (-1, 1):
            a.r([sx * 0.25, y, D / 2 + 0.008], [0.06, 0.018, 0.014], "brass", f"drawer{i}", 0.005)
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
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.03, D + 0.03], fragments=["lid"], keep=["body"], surfaces=[[-W / 2 + 0.05, -D / 2 + 0.05, W / 2 - 0.05, D / 2 - 0.05, H]])


def bed(root):
    a = A("furn_bed", "Bed", "Period bed: turned-look posts, panelled head- and footboard, ticking mattress, linen sheet and bolster, "
          "folded red wool blanket.", "wood", 24, 55, wall=True, front=0.6)
    W, L = 1.4, 2.0
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.03), 0.55, -L / 2 + 0.03], [0.06, 1.1, 0.06], "oak", "head", 0.008)
        a.r([sx * (W / 2 - 0.03), 0.38, L / 2 - 0.03], [0.06, 0.76, 0.06], "oak", "foot", 0.008)
        a.r([sx * (W / 2 - 0.03), 0.3, 0], [0.04, 0.16, L - 0.1], "oak", "frame", 0.006)
    framed_door(a, 0, 0.72, -L / 2 + 0.03, W - 0.1, 0.62, "oak", "head")
    framed_door(a, 0, 0.5, L / 2 - 0.03, W - 0.1, 0.4, "oak", "foot")
    a.r([0, 1.12, -L / 2 + 0.03], [W + 0.02, 0.05, 0.08], "oak", "head", 0.012)
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
    for sx in (-1, 1):
        a.r([sx * 0.25, 0.14, 0.05], [0.03, 0.16, 0.03], "iron", "dogs", 0.006)
        a.r([sx * 0.25, 0.08, -0.08], [0.025, 0.025, 0.3], "iron", "dogs", 0.006)
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
    a.r([0, 1.95, 0.155], [0.3, 0.3, 0.01], "brass", "hood", 0.004)
    a.b([0, 1.95, 0.163], [0.32, 0.32, 0.004], "glass", "glass")
    a.r([0, 1.1, 0.135], [0.1, 0.5, 0.008], "brass", "case", 0.004)
    a.b([0, 1.1, 0.141], [0.16, 0.6, 0.004], "glass", "glass")
    a.col([0, 1.1, 0], [0.5, 2.2, 0.32])
    return a.write(root, [0.5, 0.32], fragments=["hood"], keep=["case"], glass="glass")


# ---------------------------------------------------------------- small props (cosmetic: no blocking collision)
def crockery(root):
    a = A("furn_crockery", "Crockery stack", "Stacked faience plates, a bowl and a cup.", "ceramic", 1, 6, blocking=False, front=0.0)
    for i in range(5):
        a.plate([0, 0.006 + i * 0.013, 0], 0.23, 0.011, "faience" if i % 2 else "faience_dots", "plates")
    a.r([0.2, 0.04, 0.02], [0.15, 0.07, 0.15], "faience_red", "bowl", 0.035)
    a.r([0.15, 0.035, -0.14], [0.08, 0.07, 0.08], "faience", "bowl", 0.025)
    return a.write(root, [0.42, 0.32], fragments=[], keep=[])


def jug(root):
    a = A("furn_jug_bowl", "Jug and basin", "Stoneware jug standing in a faience basin.", "ceramic", 1, 6, blocking=False, front=0.0)
    a.r([0, 0.035, 0], [0.36, 0.07, 0.36], "faience", "basin", 0.03)
    a.r([0, 0.16, 0], [0.14, 0.18, 0.14], "stoneware", "jug", 0.05)
    a.r([0, 0.27, 0], [0.09, 0.06, 0.09], "stoneware", "jug", 0.03)
    a.r([0.085, 0.19, 0], [0.025, 0.12, 0.02], "stoneware", "jug", 0.008)
    return a.write(root, [0.36, 0.36], fragments=[], keep=[])


def bottles(root):
    a = A("furn_bottles", "Bottles", "Three cider and wine bottles, green and brown glass.", "glass", 1, 4, blocking=False, front=0.0)
    for i, (x, z, m) in enumerate([(-0.07, 0.0, "bottle_green"), (0.03, 0.04, "bottle_brown"), (0.08, -0.05, "bottle_green")]):
        a.r([x, 0.1, z], [0.075, 0.2, 0.075], m, f"b{i}", 0.03)
        a.r([x, 0.24, z], [0.03, 0.09, 0.03], m, f"b{i}", 0.012)
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
    a.r([0, 0.015, 0], [0.12, 0.03, 0.12], "brass", "base", 0.01)
    a.r([0, 0.08, 0], [0.04, 0.1, 0.04], "brass", "base", 0.01)
    a.r([0, 0.16, 0], [0.11, 0.07, 0.11], "brass", "base", 0.03)
    a.b([0, 0.27, 0], [0.06, 0.16, 0.06], "glass", "glass")
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
    a.r([0, 1.5, 0.06], [1.5, 0.02, 0.02], "iron", "rod", 0.008)
    for sx in (-1, 1):
        for i in range(4):   # pleats
            x = sx * (0.62 - i * 0.045)
            a.r([x, 0.78, 0.06 + 0.012 * (i % 2)], [0.05, 1.44, 0.018], "curtain", f"side{(sx + 1) // 2}", 0.008)
    return a.write(root, [1.5, 0.08], fragments=[], keep=["rod"])


def rug(root, id, name, W, D, variant, col):
    a = A(id, name, f"Woven wool rug, {W:.1f} x {D:.1f} m: border bands, " + ("kilim stripes and lozenges." if variant == 1 else "a muted medallion field.") +
          " Lies flat 2 mm above the floor (no z-fighting); no collision.", "cloth", 0, 30, blocking=False, front=0.0)
    grain = variant + 4 * (int(round(W * 50)) + 256 * int(round(D * 50)))
    m = f"rug_{id}"
    a.extra_mats[m] = (col, 0.97, "rug", grain)
    a.r([0, 0.002 + 0.005, 0], [W, 0.01, D], m, "rug", 0.004)
    return a.write(root, [W, D], fragments=[], keep=[])


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
    ]
    for o in outs:
        print("wrote", o.name)


if __name__ == "__main__":
    main()
