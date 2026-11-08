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
            materials.append({"name": n, "albedo": alb, "roughness": rough, "pattern": pat, "metallic": 1.0 if n in ("brass", "copper", "mirror") else (0.8 if n == "zinc" else (0.15 if n in ("iron", "cast_iron") else 0)), "grain": grain})
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
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.04), 0.04, sz * (D / 2 - 0.04)], [0.06, 0.08, 0.06], w, "carcass", 0.01)
    a.r([0, 0.1, 0], [W - 0.04, 0.04, D - 0.02], w, "carcass", 0.005)
    a.r([0, H / 2 + 0.04, -D / 2 + 0.01], [W - 0.04, H - 0.1, 0.02], w, "carcass", 0.004)
    a.r([0, H - 0.015, 0.012], [W + 0.05, 0.035, D + 0.035], w, "top", 0.01)
    for i, sx in enumerate((-1, 1)):
        a.r([sx * W / 4, H - 0.12, D / 2 - 0.01], [W / 2 - 0.05, 0.12, 0.022], w, f"drawer{i}", 0.005)
        a.r([sx * W / 4, H - 0.12, D / 2 + 0.006], [0.06, 0.02, 0.014], "brass", f"drawer{i}", 0.005)
        framed_door(a, sx * W / 4, 0.46, D / 2 - 0.01, W / 2 - 0.05, 0.6, w, f"door{i}")
        a.r([sx * 0.05, 0.52, D / 2 + 0.006], [0.014, 0.05, 0.014], "brass", f"door{i}", 0.005)
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
        a.r([cx, H - 0.1, D / 2 - 0.01], [W / 3 - 0.06, 0.1, 0.022], w, "drawers", 0.005)
        a.r([cx, H - 0.1, D / 2 + 0.006], [0.05, 0.018, 0.012], "brass", "drawers", 0.005)
        framed_door(a, cx, 0.42, D / 2 - 0.01, W / 3 - 0.06, 0.56, w, f"door{i}")
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
        a.r([sx * (W / 2 - 0.03), H / 2, sz * (D / 2 - 0.03)], [0.04, H, 0.04], w, "frame", 0.006)
    a.r([0, 0.16, 0], [W - 0.06, 0.02, D - 0.06], w, "frame", 0.004)   # lower shelf
    a.r([0, H - 0.08, D / 2 - 0.03], [W - 0.1, 0.1, 0.02], w, "drawer", 0.004)
    a.r([0, H - 0.08, D / 2 - 0.016], [0.05, 0.016, 0.012], "brass", "drawer", 0.004)
    a.r([0, H - 0.08, -D / 2 + 0.03], [W - 0.1, 0.1, 0.02], w, "frame", 0.004)
    a.r([0, H + 0.015, 0], [W + 0.02, 0.03, D + 0.02], "marble", "top", 0.006)
    a.r([0, H + 0.13, -D / 2 + 0.01], [W, 0.2, 0.02], "marble", "top", 0.005)
    for sx in (-1, 1):
        a.r([sx * (W / 2 + 0.03), H - 0.1, 0], [0.018, 0.018, 0.018], w, "frame", 0.004)
    a.r([0, H - 0.1, 0], [0.016, 0.016, D - 0.1], w, "frame", 0.004)   # towel rail along a side
    a.r([0, H - 0.18, D / 2 + 0.01], [W - 0.1, 0.2, 0.006], "linen", "towel", 0.003)
    a.r([0, H + 0.065, 0.02], [0.36, 0.07, 0.3], "faience", "basin", 0.03)
    a.r([0.13, H + 0.15, -0.05], [0.12, 0.2, 0.12], "faience_red", "basin", 0.045)
    a.col([0, H / 2, 0], [W, H + 0.05, D])
    return a.write(root, [W + 0.06, D + 0.02], fragments=["basin", "top", "drawer", "towel"], keep=["frame"])


def stove(root):
    a = A("furn_stove", "Cooking range", "Cast-iron cuisinière: enamelled oven doors, brass rail, hot plate rings, a flue pipe to the wall, a pot on top.",
          "metal", 0, 160, wall=True, front=0.9)
    W, D, H = 1.0, 0.6, 0.82
    a.r([0, H / 2 + 0.05, 0], [W, H - 0.1, D], "cast_iron", "body", 0.012)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.06), 0.03, sz * (D / 2 - 0.06)], [0.05, 0.06, 0.05], "cast_iron", "body", 0.01)
    a.r([0, H + 0.01, 0], [W + 0.02, 0.025, D + 0.02], "cast_iron", "top", 0.006)
    for x, z in ((-0.25, 0.1), (0.05, 0.1), (-0.25, -0.13)):
        a.r([x, H + 0.026, z], [0.2, 0.008, 0.2], "iron", "top", 0.003)
    a.r([0, H - 0.06, D / 2 + 0.04], [W - 0.06, 0.02, 0.02], "brass", "rail", 0.008)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.05), H - 0.06, D / 2 + 0.02], [0.02, 0.02, 0.05], "brass", "rail", 0.006)
    for i, x in enumerate((-0.24, 0.24)):
        a.r([x, 0.42, D / 2 + 0.006], [0.4, 0.42, 0.02], "enamel", f"door{i}", 0.01)
        a.r([x, 0.6, D / 2 + 0.02], [0.12, 0.018, 0.018], "brass", f"door{i}", 0.006)
    a.r([0.38, H + 0.5, -D / 2 + 0.1], [0.13, 1.0, 0.13], "cast_iron", "flue", 0.05)
    a.r([0.08, H + 0.1, 0.1], [0.22, 0.15, 0.22], "cast_iron", "pot", 0.06)
    a.r([0.08, H + 0.18, 0.1], [0.2, 0.02, 0.2], "cast_iron", "pot", 0.02)
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
            if (i * 6 + j) % 5 == 4:   # a stoneware crock
                a.r([x, y + 0.012 + 0.08, 0.0], [0.13, 0.16, 0.13], "stoneware", "crocks", 0.04)
                continue
            a.r([x, y + 0.012 + h / 2 - 0.01, 0.02], [0.085, h - 0.03, 0.085], mats[(i + j) % 3], "jars", 0.03)
            a.b([x, y + 0.012 + h / 2, 0.02], [0.1, h, 0.1], "jar_glass", "glass")
            a.r([x, y + 0.012 + h + 0.008, 0.02], [0.09, 0.016, 0.09], "zinc", "jars", 0.006)
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
        n = 8
        for k in range(n):   # staves around the axis (octagonal barrel, bulging middle)
            ang = 360.0 * k / n
            ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
            if along == "y":
                off = [ca * R * 0.93, 0, sa * R * 0.93]
                a.r([c[0] + off[0], c[1], c[2] + off[2]], [R * 0.78, L, 0.03], "oak", grp, 0.008, rot=[0, -ang + 90, 0])
            else:
                off = [0, ca * R * 0.93, sa * R * 0.93]
                a.r([c[0], c[1] + off[1], c[2] + off[2]], [L, R * 0.78, 0.03], "oak", grp, 0.008, rot=[ang - 90, 0, 0])
        for t in (-0.38, 0.0, 0.38):
            if along == "y":
                a.r([c[0], c[1] + t * L, c[2]], [R * 2.06, 0.03, R * 2.06], "iron", grp, 0.01)
            else:
                a.r([c[0] + t * L, c[1], c[2]], [0.03, R * 2.06, R * 2.06], "iron", grp, 0.01)
        if along == "y":
            a.r([c[0], c[1] + L / 2 - 0.01, c[2]], [R * 1.75, 0.025, R * 1.75], "oak_dark", grp, 0.006)
        else:
            a.r([c[0] + L / 2 - 0.01, c[1], c[2]], [0.025, R * 1.75, R * 1.75], "oak_dark", grp, 0.006)
            a.r([c[0] + L / 2 + 0.03, c[1] - R * 0.5, c[2]], [0.06, 0.03, 0.03], "brass", grp, 0.008)
    if rack:
        for sx in (-1, 1):
            a.r([sx * 0.3, 0.08, 0], [0.08, 0.16, 1.15], "oak_dark", "cradle", 0.01)
        a.r([0, 0.04, 0], [0.7, 0.06, 0.08], "oak_dark", "cradle", 0.006)
        cask([0.0, 0.42, -0.28], "x", 0.75, 0.27, "cask0")
        cask([0.0, 0.42, 0.28], "x", 0.75, 0.27, "cask1")
        a.col([0, 0.35, 0], [0.8, 0.7, 1.15])
        return a.write(root, [0.85, 1.15], fragments=["cask1", "cask0"], keep=["cradle"],
                       surfaces=[[-0.3, -0.45, 0.3, 0.45, 0.705]])
    cask([0, 0.42, 0], "y", 0.84, 0.28, "cask")
    a.r([0, 0.86, 0], [0.5, 0.03, 0.5], "oak_dark", "lid", 0.008)
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
    a.r([W / 2 - 0.08, H - 0.12, D / 2 - 0.02], [0.3, 0.2, 0.06], "oak_dark", "vice", 0.01)
    a.r([W / 2 - 0.08, H - 0.12, D / 2 + 0.08], [0.03, 0.03, 0.2], "oak_dark", "vice", 0.01)
    a.r([W / 2 - 0.08, H - 0.12, D / 2 + 0.18], [0.2, 0.025, 0.025], "oak_dark", "vice", 0.01)
    a.r([-0.4, H + 0.03, 0.02], [0.3, 0.06, 0.07], "oak", "tools", 0.01)   # plane
    a.r([-0.4, H + 0.07, 0.02], [0.04, 0.04, 0.03], "iron", "tools", 0.006)
    a.r([0.15, H + 0.03, 0.1], [0.12, 0.06, 0.06], "oak_dark", "tools", 0.01, rot=[0, 30, 0])   # mallet
    a.r([0.25, H + 0.02, 0.15], [0.28, 0.025, 0.025], "oak", "tools", 0.008, rot=[0, 30, 0])
    for i in range(3):
        a.r([-0.1 + i * 0.06, H - 0.04, -D / 2 + 0.06], [0.02, 0.012, 0.2], "iron", "tools", 0.004)
    a.r([0.55, H + 0.006, -0.05], [0.3, 0.01, 0.2], "log_end", "tools", 0.003)   # shavings / an offcut
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W + 0.04, D + 0.2], fragments=["tools", "vice", "top"], keep=["legs"],
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.14, W / 2 - 0.4, D / 2 - 0.08, H]])


def armchair(root):
    a = A("furn_armchair", "Armchair", "Upholstered fauteuil: carved cherry frame, padded seat and back in green check, scrolled arms.",
          "wood", 12, 30, front=0.5)
    W, D = 0.7, 0.68
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        a.r([sx * (W / 2 - 0.04), 0.18, sz * (D / 2 - 0.05)], [0.05, 0.36, 0.05], "cherry", "frame", 0.008)
    a.r([0, 0.4, 0.03], [W - 0.06, 0.12, D - 0.12], "upholstery", "seat", 0.04)
    a.r([0, 0.32, 0.03], [W - 0.04, 0.06, D - 0.08], "cherry", "frame", 0.008)
    a.r([0, 0.74, -D / 2 + 0.07], [W - 0.12, 0.62, 0.1], "upholstery", "back", 0.04, rot=[-8, 0, 0])
    a.r([0, 1.06, -D / 2 + 0.03], [W - 0.08, 0.05, 0.06], "cherry", "back", 0.015, rot=[-8, 0, 0])
    for i, sx in enumerate((-1, 1)):
        a.r([sx * (W / 2 - 0.045), 0.62, 0.0], [0.07, 0.05, D - 0.1], "cherry", f"arm{i}", 0.02)
        a.r([sx * (W / 2 - 0.045), 0.52, D / 2 - 0.08], [0.045, 0.2, 0.045], "cherry", f"arm{i}", 0.01)
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
        a.r([x, H - 0.08, D / 2 - 0.03], [W / 3 - 0.06, 0.09, 0.02], "oak", "drawers", 0.004)
        a.r([x, H - 0.08, D / 2 - 0.016], [0.04, 0.014, 0.012], "brass", "drawers", 0.004)
    a.r([0, H - 0.08, -D / 2 + 0.03], [W - 0.08, 0.1, 0.02], "oak", "apron", 0.004)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), H - 0.08, 0], [0.02, 0.1, D - 0.08], "oak", "apron", 0.004)
    for i, (sx, sz) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        leg_tapered(a, sx * (W / 2 - 0.04), sz * (D / 2 - 0.04), H - 0.03, 0.05, 0.035, "oak", f"leg{i}")
    a.r([0.35, H + 0.03, -0.15], [0.06, 0.06, 0.06], "glass", "glass", 0.01)
    a.r([0.35, H + 0.012, -0.15], [0.07, 0.02, 0.07], "brass", "inkwell", 0.006)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "drawers", "leg0", "inkwell"], keep=["apron", "leg2", "leg3", "leg1"], glass="glass",
                   surfaces=[[-W / 2 + 0.05, -D / 2 + 0.05, W / 2 - 0.25, D / 2 - 0.05, H + 0.003]])


def pew(root):
    a = A("furn_pew", "Church pew", "Oak pew with a panelled back, book ledge and scrolled ends; seats three.", "wood", 24, 50, front=0.55)
    W, D = 2.1, 0.55
    for i, sx in enumerate((-1, 1)):
        a.r([sx * (W / 2 - 0.03), 0.48, 0], [0.05, 0.96, D], "oak_dark", f"end{i}", 0.012)
        a.r([sx * (W / 2 - 0.03), 0.98, 0.1], [0.05, 0.08, 0.2], "oak_dark", f"end{i}", 0.03)
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
    for sx in (-1, 1):
        a.r([sx * 0.3, 0.745, -0.4], [0.035, 0.012, 0.035], "slate_black", "lid", 0.004)
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
            a.r([x, 0.08 + row * 0.14, -0.02 * (i % 2)], [0.15, 0.14, 0.42], "bark", "logs" if row < 2 else "top", 0.05)
            a.r([x, 0.08 + row * 0.14, 0.212], [0.12, 0.11, 0.004], "log_end", "logs" if row < 2 else "top", 0.002)
            k += 1
    for i in range(3):
        a.r([-0.1 + i * 0.12, 0.63, 0.02], [0.03, 0.03, 0.4], "pine", "top", 0.008, rot=[0, 10 * i - 10, 0])
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
    for i in range(4):
        a.r([-0.33 + i * 0.22, 0.0, 0.06], [0.025, 0.025, 0.08], "oak_dark", "rail", 0.008, rot=[-20, 0, 0])
    a.r([-0.33, -0.45, 0.07], [0.42, 0.85, 0.09], "coat_wool", "coats", 0.04)
    a.r([0.11, -0.36, 0.07], [0.38, 0.7, 0.08], "coat_blue", "coats", 0.04)
    a.r([0.33, 0.02, 0.09], [0.2, 0.06, 0.2], "coat_wool", "cap", 0.03)
    return a.write(root, [0.9, 0.12], fragments=["coats", "cap"], keep=["rail"])


def wall_clock(root):
    a = A("furn_wall_clock", "Wall clock", "Cartel clock in a painted case with a brass pendulum.", "painted", 2, 8, blocking=False, wall=True, front=0.0, mount=1.6)
    a.r([0, 0.0, 0.05], [0.32, 0.38, 0.1], "paint_cream", "case", 0.03)
    a.r([0, 0.03, 0.102], [0.24, 0.24, 0.006], "dial", "case", 0.002)
    a.b([0, 0.03, 0.108], [0.25, 0.25, 0.004], "glass", "glass")
    a.r([0, -0.3, 0.04], [0.06, 0.25, 0.04], "paint_cream", "case", 0.01)
    a.r([0, -0.38, 0.065], [0.08, 0.08, 0.01], "brass", "case", 0.004)
    return a.write(root, [0.32, 0.11], fragments=["case"], keep=[], glass="glass")


def utensil_rack(root):
    a = A("furn_utensil_rack", "Utensil rack", "Iron rail with hanging copper pans, a ladle, a skimmer and a colander.", "metal", 0, 25, blocking=False, wall=True, front=0.0, mount=1.45)
    a.r([0, 0.0, 0.04], [1.0, 0.02, 0.02], "iron", "rail", 0.008)
    for sx in (-1, 1):
        a.r([sx * 0.48, 0.0, 0.02], [0.02, 0.06, 0.04], "iron", "rail", 0.006)
    for i, (x, d, drop) in enumerate([(-0.35, 0.22, 0.16), (-0.08, 0.18, 0.13), (0.38, 0.2, 0.15)]):
        a.r([x, -drop - 0.05, 0.05], [d, d, 0.05], "copper", "pans", 0.02)
        a.r([x, -0.05, 0.05], [0.025, 0.12, 0.012], "copper", "pans", 0.005)
    a.r([0.15, -0.18, 0.05], [0.02, 0.32, 0.012], "iron", "pans", 0.004)
    a.r([0.15, -0.36, 0.06], [0.08, 0.05, 0.06], "iron", "pans", 0.02)
    return a.write(root, [1.0, 0.1], fragments=["pans"], keep=["rail"])


def tool_rack(root):
    a = A("furn_tool_rack", "Tool board", "Pine board with a bow saw, hammer, auger, square and a coil of rope on pegs.", "wood", 6, 15, blocking=False, wall=True, front=0.0, mount=1.1)
    a.r([0, 0.35, 0.01], [1.2, 0.7, 0.02], "pine", "board", 0.004)
    a.r([-0.35, 0.4, 0.04], [0.5, 0.025, 0.02], "oak", "tools", 0.006)   # bow saw
    a.r([-0.35, 0.53, 0.04], [0.5, 0.02, 0.012], "iron", "tools", 0.003)
    for sx in (-1, 1):
        a.r([-0.35 + sx * 0.25, 0.47, 0.04], [0.025, 0.16, 0.02], "oak", "tools", 0.006)
    a.r([0.1, 0.35, 0.04], [0.03, 0.32, 0.03], "oak", "tools", 0.008)   # hammer
    a.r([0.1, 0.5, 0.045], [0.12, 0.04, 0.04], "iron", "tools", 0.008)
    a.r([0.3, 0.35, 0.04], [0.02, 0.45, 0.02], "iron", "tools", 0.004)   # auger
    a.r([0.3, 0.58, 0.04], [0.2, 0.03, 0.03], "oak", "tools", 0.008)
    a.r([0.48, 0.3, 0.05], [0.2, 0.2, 0.06], "sacking", "rope", 0.08)
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
    for sx in (-1, 1):
        a.r([sx * 0.36, -0.1, 0.07], [0.025, 0.18, 0.14], "pine", "brackets", 0.004)
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
        a.r([x, h / 2 - 0.005, z], [0.08, h - 0.02, 0.08], m, "jars", 0.03)
        a.b([x, h / 2, z], [0.09, h, 0.09], "jar_glass", "glass")
        a.r([x, h + 0.006, z], [0.085, 0.012, 0.085], "zinc", "jars", 0.005)
    return a.write(root, [0.22, 0.2], fragments=[], keep=[], glass="glass")


def bucket(root):
    a = A("furn_bucket", "Bucket", "Coopered wooden pail with iron hoops and a rope handle.", "wood", 2, 8, blocking=False, front=0.0)
    for k in range(8):
        ang = 45.0 * k
        a.r([math.cos(math.radians(ang)) * 0.13, 0.15, math.sin(math.radians(ang)) * 0.13], [0.105, 0.3, 0.02], "oak", "pail", 0.005, rot=[0, -ang + 90, 0])
    for y in (0.06, 0.26):
        a.r([0, y, 0], [0.29, 0.02, 0.29], "iron", "pail", 0.008)
    a.r([0, 0.012, 0], [0.24, 0.02, 0.24], "oak", "pail", 0.005)
    a.r([0, 0.36, 0], [0.3, 0.015, 0.015], "sacking", "pail", 0.006)
    return a.write(root, [0.3, 0.3], fragments=[], keep=[])


def basket(root):
    a = A("furn_basket", "Basket of apples", "Wicker basket with a bow handle, full of red apples.", "cloth", 2, 6, blocking=False, front=0.0)
    a.r([0, 0.1, 0], [0.42, 0.2, 0.3], "wicker", "basket", 0.05)
    a.r([0, 0.32, 0], [0.4, 0.025, 0.025], "wicker", "basket", 0.01)
    for sx in (-1, 1):
        a.r([sx * 0.19, 0.25, 0], [0.025, 0.14, 0.025], "wicker", "basket", 0.01)
    for i, (x, z) in enumerate([(-0.1, -0.05), (0.02, 0.06), (0.12, -0.04), (-0.04, 0.03), (0.08, 0.08)]):
        a.r([x, 0.21 + 0.01 * (i % 2), z], [0.075, 0.07, 0.075], "apple", "apples", 0.033)
    return a.write(root, [0.42, 0.3], fragments=[], keep=[])


def pot(root):
    a = A("furn_pot", "Cooking pot", "Cast-iron marmite with a lid and a ladle.", "metal", 0, 20, blocking=False, front=0.0)
    a.r([0, 0.1, 0], [0.26, 0.2, 0.26], "cast_iron", "pot", 0.07)
    a.r([0, 0.2, 0], [0.24, 0.02, 0.24], "cast_iron", "lid", 0.02)
    a.r([0, 0.22, 0], [0.04, 0.03, 0.04], "cast_iron", "lid", 0.01)
    a.r([0.12, 0.25, 0], [0.02, 0.2, 0.015], "iron", "lid", 0.004, rot=[0, 0, -30])
    return a.write(root, [0.3, 0.26], fragments=[], keep=[])


def candlesticks(root):
    a = A("furn_candlesticks", "Candlesticks", "A pair of brass candlesticks with half-burnt candles.", "metal", 0, 6, blocking=False, front=0.0)
    for i, x in enumerate((-0.07, 0.07)):
        a.r([x, 0.012, 0], [0.09, 0.024, 0.09], "brass", "sticks", 0.01)
        a.r([x, 0.1, 0], [0.025, 0.16, 0.025], "brass", "sticks", 0.008)
        a.r([x, 0.24 - 0.03 * i, 0], [0.022, 0.12 - 0.06 * i, 0.022], "wax", "sticks", 0.008)
    return a.write(root, [0.24, 0.1], fragments=[], keep=[])


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
    ]
    for o in outs:
        print("wrote", o.name)


if __name__ == "__main__":
    main()
