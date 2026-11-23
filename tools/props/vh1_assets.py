"""VH1: the eight houses of the user's "House Village" GLB (remib74, Sketchfab, CC-BY-4.0) as enterable game buildings.

    python -I tools/props/vh1_assets.py convert <aligned dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per house
    python -I tools/props/vh1_assets.py write <aligned dir> <glb dir>     # asset JSONs (vh_*), credits rows
then bake: NormandyMapmaker --import-weapon-models <glb dir> content.

The source is one scene of closed low-poly shells whose doors and windows are only painted on. Each house was turned
square, centred and set on z 0 first (E:/nmk/village/py/align.py: its walls' dominant direction; <aligned dir>/<key>.glb,
source units, 0.29 m each: the painted doors ~2 m, windows ~1.6 m). Here, per house:
  * the ground-floor doors and windows are cut through the shell (pr6_split "cut_out") and fitted with the game's
    openable doors / glazed casements (pr8_openings); the upper floors keep their painted windows (no upstairs inside);
  * the inside is lined (pr10_assets.lining: boarded floor, plaster walls, a ceiling under the eaves) and walled with
    colliders from the wall outline (E:/nmk/village/py/slice.py: a horizontal slice at 0.6 m);
  * basic furniture as the building's companions (bed, table and chairs, stove, cupboard, chest...).
Openings and outlines: E:/nmk/village/py/openings.py (gridded elevations, el2/sheet_*.jpg) and slices.json.
"""
import json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pr8_openings
from pr10_assets import BLENDER, HERE, ROOT, run

S = 0.29   # metres per source unit
# VH1c (the user: "undo that change, it looks worse now. the previous exterior stone should be what it looks like both exterior
# and interior. the roofs should not be reflective. Some of the roofs should use the bricklike texture but some should use this
# strawlike one. roofs also need to be thicker and not just a flat plane"):
#  * the outside keeps the model's own textures (VH1b's re-texture undone), made matte (roughness 0.92, no metal);
#  * the inner walls are lined with each house's own outside stone: a plain square of its wall cut from a flat render of the
#    original (E:/nmk/village/py/patch3.py -> <VH_TEX>/<asset>_stone/color.jpg, mirrored 2 x 2; its tile in stone_tiles.json);
#  * the roofs are solidified (0.12 m tiles, 0.35 m thatch); four houses thatched (ambientCG ThatchedRoof001B / 002B, CC0).
VH_TEX = os.environ.get("VH1_TEX", "E:/nmk/village/tex")
ROOF = {"vh_cottage_shutters": ("thatch_001b", 0.35), "vh_cottage_tall": ("thatch_002b", 0.35), "vh_longhouse_blue": ("thatch_002b", 0.35),
        "vh_barn_cottage": ("thatch_001b", 0.35), "vh_manor_tower": (None, 0.12), "vh_stone_house_l": (None, 0.12), "vh_townhouse": (None, 0.12),
        "vh_timber_house": (None, 0.12)}
THATCH_TILE = 2.0   # m


def stone(bid):
    tiles = json.load(open(os.path.join(VH_TEX, "stone_tiles.json")))
    return os.path.join(VH_TEX, bid + "_stone"), tiles[bid]["tile_m"]
CREDIT = ("House Village", "remib74", "CC-BY-4.0", "https://sketchfab.com/3d-models/house-village-a5d6dae1ff424adca8978a143769ab2d")
T = 0.5    # wall collider thickness inward (source units, 15 cm)
U = 1 / S  # source units per metre

# furniture footprints (m, width along the wall x depth) and how far from the wall
FURN = {"furn_bed_rustic": (1.0, 2.0), "furn_table_dining": (1.6, 0.9), "furn_chair_ladder": (0.45, 0.45), "furn_stove": (0.8, 0.6),
        "furn_armoire": (1.2, 0.6), "furn_dresser": (1.2, 0.5), "furn_chest": (1.0, 0.5), "furn_washstand": (0.7, 0.45), "furn_sideboard": (1.5, 0.5),
        "furn_fireplace": (1.5, 0.6), "furn_bench": (1.4, 0.4), "furn_hutch": (1.2, 0.5), "furn_shelf": (1.0, 0.35), "furn_table_small": (0.8, 0.8)}


def wall_item(room, side, t, asset, along_bed=False):
    """A piece with its back to one wall of a room box [x0, x1, y0, y1]: side N / S / E / W, t = 0..1 along that wall.
    Returns (asset, x, y, yaw) in source units; yaw 0 faces -y (back to the +y wall), 90 faces -x."""
    x0, x1, y0, y1 = room; w, d = FURN[asset]; d = (w if along_bed else d) * U / 2 + 0.15; r = 90 if along_bed else 0   # along_bed: long side to the wall
    if side == "N": return (asset, x0 + (x1 - x0) * t, y1 - d, 0 + r)
    if side == "S": return (asset, x0 + (x1 - x0) * t, y0 + d, 180 + r)
    if side == "E": return (asset, x1 - d, y0 + (y1 - y0) * t, 90 + r)
    return (asset, x0 + d, y0 + (y1 - y0) * t, 270 + r)


def free_item(asset, x, y, yaw=0): return (asset, x, y, yaw)


# outline: the outer wall face, a counter-clockwise polygon (source units); Hc: the ground floor's ceiling; rooms: lining
# boxes [x0, x1, y0, y1] (inside the walls); inner: doorways between the rooms of an L / two-block plan ([x0, x1, y0, y1]);
# open: (id, kind, side, plane, a0, a1, z0, z1): side "x" = a wall along x at y = plane, "y" = along y at x = plane;
# sign: which way is inside from the plane (+1 / -1)
H = {
    "vh_cottage_shutters": dict(key="h1_box024", name="Village cottage (shuttered)", Hc=8.2,
        outline=[(-6.65, -8.13), (6.93, -8.13), (6.93, 8.34), (-6.65, 8.34)],
        open=[("door", "door", "y", -6.3, 1, -2.0, 1.93, 0.1, 6.9), ("win_s", "window", "x", -8.13, 1, -2.16, 2.36, 3.15, 7.75),
              ("win_n", "window", "x", 8.34, -1, -2.24, 2.3, 3.15, 7.61), ("win_e", "window", "y", 6.93, -1, -2.73, 2.82, 3.12, 7.72)],
        rooms=[[-6.15, 6.43, -7.63, 7.84]]),
    "vh_cottage_tall": dict(key="h2_box030", name="Village cottage (tall gable)", Hc=9.0,
        outline=[(-6.7, -8.65), (6.88, -8.65), (6.88, 7.86), (-6.7, 7.86)],
        open=[("door", "door", "y", 6.52, -1, -2.02, 2.02, 0.1, 6.9), ("win_s", "window", "x", -8.65, 1, -2.1, 2.32, 3.64, 8.05),
              ("win_w", "window", "y", -6.7, 1, -2.76, 2.65, 3.09, 7.68), ("win_n", "window", "x", 7.86, -1, -2.21, 2.21, 3.27, 7.5)],
        rooms=[[-6.2, 6.02, -8.15, 7.36]]),
    "vh_longhouse_blue": dict(key="h3_cube1", name="Village long house (blue shutters)", Hc=10.2, cut=1.9,
        outline=[(-7.95, -15.45), (7.3, -15.45), (7.3, 15.43), (-7.95, 15.43), (-7.95, 4.77), (-6.53, 4.77), (-6.53, -0.17), (-7.95, -0.17)],
        open=[("door", "door", "y", -6.53, 1, -0.27, 4.71, 0.1, 8.1), ("win_s", "window", "x", -15.45, 1, -2.14, 1.34, 2.53, 8.68),
              ("win_n", "window", "x", 15.43, -1, -0.8, 2.78, 2.53, 8.41), ("win_w1", "window", "y", -7.9, 1, 8.18, 11.93, 1.99, 7.34),
              ("win_w2", "window", "y", -7.95, 1, -9.74, -5.99, 1.99, 7.34), ("win_e1", "window", "y", 7.25, -1, -9.9, -6.05, 3.86, 9.9),
              ("win_e2", "window", "y", 7.3, -1, 2.57, 6.53, 3.86, 9.9)],
        rooms=[[-7.45, 6.8, -14.95, 14.93]]),
    "vh_manor_tower": dict(key="h4_zbrush", name="Village manor (tower)", Hc=10.0,
        outline=[(-5.3, -16.95), (12.57, -16.95), (12.57, 4.55), (-5.3, 4.55)],   # the main block (the low west and north wings stay closed)
        open=[("door", "door", "y", 12.57, -1, -6.9, -4.4, 0.1, 7.2), ("win_s1", "window", "x", -16.95, 1, -2.14, 2.54, 3.16, 8.81),
              ("win_s2", "window", "x", -16.95, 1, 6.77, 11.18, 2.49, 9.54), ("win_n", "window", "x", 4.55, -1, 7.22, 10.84, 3.33, 9.15),
              ("win_w", "window", "y", -5.3, 1, -14.39, -8.75, 2.49, 8.02), ("win_e1", "window", "y", 12.57, -1, -14.11, -9.03, 3.05, 7.68),
              ("win_e2", "window", "y", 12.57, -1, -3.1, 0.28, 3.05, 7.68)],
        rooms=[[-4.8, 12.07, -16.45, 4.05]]),
    "vh_stone_house_l": dict(key="h5_maison7", name="Village stone house (L plan)", Hc=9.2,
        outline=[(-12.52, -10.57), (-0.08, -10.57), (-0.08, -0.21), (12.34, -0.21), (12.34, 2.55), (11.25, 2.55), (11.25, 8.53), (12.34, 8.53),
                 (12.34, 10.71), (-12.52, 10.71)],
        open=[("door", "door", "y", 11.25, -1, 2.06, 8.59, 0.1, 9.0), ("win_s1", "window", "x", -10.57, 1, -11.7, -7.1, 3.1, 8.4),
              ("win_s2", "window", "x", -10.57, 1, -5.5, -1.0, 3.0, 8.4), ("win_s3", "window", "x", -0.21, 1, 2.0, 10.4, 2.9, 9.0),
              ("win_n1", "window", "x", 10.71, -1, 7.23, 11.02, 3.17, 8.45), ("win_n2", "window", "x", 10.71, -1, -5.16, -1.32, 3.17, 8.45),
              ("win_w1", "window", "y", -12.52, 1, 1.03, 4.0, 3.38, 8.45), ("win_w2", "window", "y", -12.52, 1, -9.5, -6.61, 2.96, 8.25)],
        rooms=[[-12.02, -0.58, -10.07, 10.21], [-0.58, 10.75, 0.29, 10.21]], inner=[[-0.95, -0.2, 0.9, 9.6]]),
    "vh_townhouse": dict(key="h6_maison", name="Village town house", Hc=8.6,
        outline=[(-10.23, -4.58), (10.23, -4.58), (10.23, 4.73), (-10.23, 4.73)],
        open=[("door", "door", "y", 10.23, -1, -1.63, 1.85, 0.1, 8.3), ("win_s1", "window", "x", -4.58, 1, -6.97, -3.14, 2.97, 8.26),
              ("win_s2", "window", "x", -4.58, 1, 3.14, 7.1, 2.97, 8.26), ("win_n1", "window", "x", 4.73, -1, 3.44, 6.8, 3.27, 8.05),
              ("win_n2", "window", "x", 4.73, -1, -6.67, -3.35, 3.27, 8.05), ("win_w", "window", "y", -10.23, 1, -1.85, 1.94, 3.19, 8.3)],
        rooms=[[-9.73, 9.73, -4.08, 4.23]]),
    "vh_timber_house": dict(key="h7_poutre", name="Village timber-framed house", Hc=9.8,
        outline=[(-3.77, -14.75), (13.8, -14.75), (13.8, 14.95), (-13.97, 14.95), (-13.97, 0.03), (-3.77, 0.03)],
        open=[("door", "door", "x", -14.75, 1, 3.0, 5.8, 0.1, 7.2), ("win_n1", "window", "x", 14.95, -1, 6.56, 10.6, 1.96, 8.12),
              ("win_n2", "window", "x", 14.95, -1, -0.5, 3.53, 1.96, 8.12), ("win_n3", "window", "x", 14.95, -1, -8.99, -4.95, 1.96, 8.12),
              ("win_w", "window", "y", -13.97, 1, 5.15, 9.09, 2.57, 8.88)],
        rooms=[[-3.27, 13.3, -14.25, 14.45], [-13.47, -3.27, 0.53, 14.45]], inner=[[-3.65, -2.9, 1.5, 13.4]]),
    "vh_barn_cottage": dict(key="h8_toit", name="Village low cottage", Hc=7.6,
        outline=[(-14.0, -6.6), (13.4, -6.6), (13.4, 6.96), (-14.0, 6.96)],
        open=[("door", "door", "x", -6.6, 1, 5.59, 8.7, 0.1, 6.9), ("win_n", "window", "x", 6.96, -1, -9.56, -5.26, 3.83, 7.99),
              ("win_w", "window", "y", -14.0, 1, -1.82, 2.53, 4.07, 7.4), ("win_e", "window", "y", 13.4, -1, -2.01, 2.39, 3.31, 7.4)],
        rooms=[[-13.5, 12.9, -6.1, 6.46]]),
}

# furniture per house: a wish list, each piece put at the first free spot against a room wall (source units): inside the
# room, off the other pieces, clear of the doors (1.2 m in front of each, 0.3 m either side), tall pieces clear of the
# windows; a dining table and two chairs in the middle of the largest room (a small table where a room is narrow)
WISH = {
    "vh_cottage_shutters": [("furn_bed_rustic", 0), ("furn_stove", 0), ("furn_dresser", 0), ("furn_chest", 0)],
    "vh_cottage_tall": [("furn_bed_rustic", 0), ("furn_stove", 0), ("furn_dresser", 0), ("furn_chest", 0)],
    "vh_longhouse_blue": [("furn_bed_rustic", 0), ("furn_armoire", 0), ("furn_stove", 0), ("furn_dresser", 0), ("furn_chest", 0), ("furn_bench", 0)],
    "vh_manor_tower": [("furn_fireplace", 0), ("furn_bed_rustic", 0), ("furn_dresser", 0), ("furn_washstand", 0), ("furn_chest", 0), ("furn_bench", 0)],
    "vh_stone_house_l": [("furn_stove", 0), ("furn_bed_rustic", 1), ("furn_armoire", 0), ("furn_sideboard", 1), ("furn_washstand", 1), ("furn_chest", 0)],
    "vh_townhouse": [("furn_bed_rustic", 0), ("furn_stove", 0), ("furn_dresser", 0), ("furn_chest", 0)],
    "vh_timber_house": [("furn_fireplace", 0), ("furn_bed_rustic", 1), ("furn_armoire", 1), ("furn_sideboard", 0), ("furn_dresser", 0), ("furn_chest", 0)],
    "vh_barn_cottage": [("furn_bed_rustic", 0), ("furn_stove", 0), ("furn_armoire", 0), ("furn_chest", 0), ("furn_bench", 0)],
}
BACK_PLUS_Z = {"furn_sideboard"}
TALL = {"furn_armoire", "furn_hutch", "furn_fireplace", "furn_sideboard"}   # kept off the windows (a stove's flue pipe is thin)   # pieces modelled with their back on local +z (the rest: back on -z, front +z)
_dims = {}
def dims(aid):
    """(width along the wall, depth back->front, back extent from the origin, height) in metres, from the asset's parts."""
    if aid not in _dims:
        P = json.load(open(os.path.join(ROOT, "content", "assets", aid + ".json")))["parts"]
        xs = [p["c"][0] + k * p["s"][0] / 2 for p in P for k in (-1, 1)]; zs = [p["c"][2] + k * p["s"][2] / 2 for p in P for k in (-1, 1)]
        h = max(p["c"][1] + p["s"][1] / 2 for p in P)
        back = max(zs) if aid in BACK_PLUS_Z else -min(zs)
        _dims[aid] = (max(xs) - min(xs), max(zs) - min(zs), back, h)
    return _dims[aid]


def zones(bid):
    """Door clearances (everything keeps off) and window clearances (tall pieces keep off), source units [x0, x1, y0, y1]."""
    B = H[bid]; dz, wz = [], []
    for oid, kind, side, plane, sg, a0, a1, z0, z1 in B["open"]:
        m, deep = (0.3 * U, 1.0 * U) if kind == "door" else (0.1 * U, 0.8 * U)
        lo, hi = sorted((plane, plane + sg * deep))
        r = [a0 - m, a1 + m, lo, hi] if side == "x" else [lo, hi, a0 - m, a1 + m]
        (dz if kind == "door" else wz).append(r)
    for b in B.get("inner", []): dz.append([b[0] - 1.0 * U, b[1] + 1.0 * U, b[2], b[3]] if (b[1] - b[0]) < (b[3] - b[2]) else [b[0], b[1], b[2] - 1.0 * U, b[3] + 1.0 * U])
    return dz, wz


def hit(a, b, m=0.0): return a[0] < b[1] + m and b[0] < a[1] + m and a[2] < b[3] + m and b[2] < a[3] + m


def furnish(bid):
    B = H[bid]; rooms = B["rooms"]; dz, wz = zones(bid); placed, out = [], []
    def free(rect, room, tall, gap=0.12 * U):
        x0, x1, y0, y1 = room
        if rect[0] < x0 + 0.03 or rect[1] > x1 - 0.03 or rect[2] < y0 + 0.03 or rect[3] > y1 - 0.03: return False
        if any(hit(rect, z) for z in dz) or (tall and any(hit(rect, z) for z in wz)): return False
        return not any(hit(rect, q, gap) for q in placed)
    # the table and two chairs: the largest room's middle (dining table across the room, or a small one where it is narrow)
    big = max(rooms, key=lambda r: (r[1] - r[0]) * (r[3] - r[2])); cx, cy = (big[0] + big[1]) / 2, (big[2] + big[3]) / 2
    narrow = min(big[1] - big[0], big[3] - big[2]) * S < 3.2
    tab = "furn_table_small" if narrow else "furn_table_dining"; tw, td, _, _ = dims(tab)
    alongx = (big[1] - big[0]) >= (big[3] - big[2])   # the table's length along the room's
    for dx, dy in sorted(((i * 0.5 * U, j * 0.5 * U) for i in range(-4, 5) for j in range(-4, 5)), key=lambda q: q[0] ** 2 + q[1] ** 2):
        tx, ty = cx + dx, cy + dy
        hw, hd = (tw / 2 * U, td / 2 * U) if alongx else (td / 2 * U, tw / 2 * U)
        cd = (td / 2 + 0.45) * U   # chairs either side of the long edges
        ch = [(tx, ty - cd, 0), (tx, ty + cd, 180)] if alongx else [(tx - cd, ty, 270), (tx + cd, ty, 90)]
        mg = (td / 2 + 0.75) * U - (hd if alongx else hw)   # the chairs and room to pull them out
        rect = [tx - hw - (0 if alongx else mg), tx + hw + (0 if alongx else mg), ty - hd - (mg if alongx else 0), ty + hd + (mg if alongx else 0)]
        if free(rect, big, False, 0.3 * U):
            placed.append(rect); out.append((tab, tx, ty, 0 if alongx else 90)); out += [("furn_chair_ladder", x, y, yw) for x, y, yw in ch]; break
    for aid, ri in WISH[bid]:
        w, d, back, h = dims(aid); tall = aid in TALL; along = aid == "furn_bed_rustic" and narrow
        room = rooms[min(ri, len(rooms) - 1)]; x0, x1, y0, y1 = room; ok = False
        for side in ("N", "E", "S", "W"):
            for t in [0.5] + [k / 20 for k in range(1, 20)]:
                ww, dd, bb = (d, w, w / 2) if along else (w, d, back)   # along: the bed's long side to the wall
                off = (bb + 0.03) * U   # the origin from the wall: its back extent (plus 3 cm)
                if side in ("N", "S"):
                    px = x0 + (x1 - x0) * t; py = y1 - off if side == "N" else y0 + off
                    yw = {"N": 0, "S": 180}[side]
                    rect = [px - ww / 2 * U, px + ww / 2 * U, (y1 - dd * U - 0.03 * U) if side == "N" else y0 + 0.03 * U, y1 - 0.03 * U if side == "N" else (y0 + dd * U + 0.03 * U)]
                else:
                    py = y0 + (y1 - y0) * t; px = x1 - off if side == "E" else x0 + off
                    yw = {"E": 90, "W": 270}[side]
                    rect = [(x1 - dd * U - 0.03 * U) if side == "E" else x0 + 0.03 * U, x1 - 0.03 * U if side == "E" else (x0 + dd * U + 0.03 * U), py - ww / 2 * U, py + ww / 2 * U]
                if along: yw += 90
                if aid in BACK_PLUS_Z: yw += 180
                if free(rect, room, tall):
                    # the piece's origin is not its footprint's middle: from the back at `off`, as measured
                    placed.append(rect); out.append((aid, px, py, yw % 360)); ok = True; break
            if ok: break
        if not ok: print("  %s: no room for %s" % (bid, aid))
    return out


def boxes(bid):
    """Colliders, openings, cut-outs and lining for one house (source units)."""
    B = H[bid]; Hc = B["Hc"]; P = B["outline"]; n = len(P); walls = []
    for i in range(n):   # each edge: a box inward (left of the counter-clockwise edge), the floor to above the ceiling
        (ax, ay), (bx, by) = P[i], P[(i + 1) % n]
        if abs(ay - by) < 1e-6:   # along x
            inside = 1 if bx > ax else -1
            y0, y1 = sorted((ay, ay + inside * T)); walls.append([min(ax, bx), max(ax, bx), y0, y1, 0.0, Hc + 0.6])
        else:
            inside = -1 if by > ay else 1
            x0, x1 = sorted((ax, ax + inside * T)); walls.append([x0, x1, min(ay, by), max(ay, by), 0.0, Hc + 0.6])
    xs = [p[0] for p in P]; ys = [p[1] for p in P]
    for r in B["rooms"]: walls.append([r[0] - T, r[1] + T, r[2] - T, r[3] + T, Hc, Hc + 0.5])   # the ceiling slab
    floor = [min(xs) + T, max(xs) - T, min(ys) + T, max(ys) - T, 0.0, 0.1]
    doors, cuts, marks = {}, [], {}
    for oid, kind, side, plane, sg, a0, a1, z0, z1 in B["open"]:
        z1 = min(z1, Hc - 0.15); pin = plane + sg * 0.15   # the leaves 4 cm inside the face
        m = [a0, a1, pin - 0.03, pin + 0.03, z0, z1] if side == "x" else [pin - 0.03, pin + 0.03, a0, a1, z0, z1]
        c = [a0, a1, plane - 2.2, plane + 2.2, z0, z1] if side == "x" else [plane - 2.2, plane + 2.2, a0, a1, z0, z1]   # deep: a painted window can sit in a recess
        if kind == "door": doors[oid] = ("door", "door_wood", 2 if (a1 - a0) * S > 1.5 else 1, m)
        else: doors[oid] = ("window", None, 2, m)
        cuts.append(c)
    holes = {"inner%d" % k: [b[0], b[1], b[2], b[3], 0.1, Hc - 1.0] for k, b in enumerate(B.get("inner", []))}
    return walls, floor, doors, cuts, holes


def lining(bid, doors, holes):
    """Floor / plaster walls / ceiling for each room (pr10_assets.lining, the cuts through the walls deeper: a shell's
    face can sit a long way off the room box, the long house's recessed door)."""
    B = H[bid]; Hc = B["Hc"]; dep = B.get("cut", 1.2)
    cuts = []
    for b in [v[3] for v in doors.values()] + list(holes.values()):
        lo, hi = [b[0], b[2], b[4]], [b[1], b[3], b[5]]
        k = 0 if (b[1] - b[0]) < (b[3] - b[2]) else 1
        lo[k] -= dep; hi[k] += dep; cuts.append((lo, hi))
    out = []
    for r in B["rooms"]:
        x0, x1, y0, y1 = r; z0, z1 = 0.1, Hc; t = 0.06
        out.append({"box": [x0, x1, y0, y1, z0, z0 + 0.05], "tex": "floor_boards_old", "tile": 6.0})
        out.append({"box": [x0, x1, y0, y1, z1 - 0.05, z1], "tex": "beam_weathered", "tile": 6.0})
        for w in ([x0, x1, y0, y0 + t], [x0, x1, y1 - t, y1], [x0, x0 + t, y0, y1], [x1 - t, x1, y0, y1]):
            bx = [([w[0], w[2], z0], [w[1], w[3], z1])]
            for c in cuts: bx = [q for b in bx for q in pr8_openings.subtract(b, c)]
            st, tm = stone(bid)
            out += [{"box": [lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]], "tex": st, "tile": tm / S} for lo, hi in bx]
    return out


def mark(x, y, z=0.1): return [x - 0.03, x + 0.03, y - 0.03, y + 0.03, z, z + 0.03]


def convert(src_dir, glbs):
    os.makedirs(glbs, exist_ok=True)
    for bid, B in H.items():
        if os.path.exists(os.path.join(glbs, bid + ".glb")): continue
        walls, floor, doors, cuts, holes = boxes(bid)
        F = furnish(bid)
        marks = {**{k: v[3] for k, v in doors.items()}, **holes, "floor": floor, **{"f%d" % i: mark(f[1], f[2], floor[5]) for i, f in enumerate(F)}}
        piece = {"id": bid, "objects": None, "boxes": walls, "marks": marks, "cut_out": cuts,
                 "add_boxes": [{**b, "tex": os.path.join(ROOT, "content", "textures", b["tex"])} for b in lining(bid, doors, holes)],
                 "scale": S, "yaw": 0}
        rt, th = ROOF[bid]
        piece["matte"] = 0.92
        piece["roof"] = {"thick": th / S, "min_nz": 0.3, "min_z": 4.0, **({"tex": os.path.join(VH_TEX, rt), "tile": THATCH_TILE / S} if rt else {})}
        run({"src": os.path.join(src_dir, B["key"] + ".glb"), "out": glbs, "pieces": [piece]}, glbs, bid)


def write(src_dir, glbs):
    rows = []
    for bid, B in H.items():
        side = json.load(open(os.path.join(glbs, bid + ".json"))); sx, sy, sz = side["size"]; m = side["marks"]
        walls, floor, doors, cuts, holes = boxes(bid)
        credit = "%s by %s, %s (%s)" % CREDIT
        A = {"id": bid, "version": 1, "name": B["name"], "category": "Houses", "themes": ["wwii", "postapoc", "modern"],
             "description": "%s from the House Village set: stone walls, tiled roof, enterable through its door (E), the ground-floor windows open; furnished. %.1f x %.1f m, %.1f m high." % (B["name"], sx * 1, sz, sy),
             "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "noAutoPanels": True, "maxGrade": 2,
             "model": {"pivot": "ground", "scale": 1, "jpgMaps": True, "lod1": 0.3, "src": bid + ".glb", "dir": bid, "maxTex": 2048, "credit": credit, "lodDist": 40, "cullDist": 600},
             "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
             "furniture": [], "interactables": [], "panels": []}
        A["parts"] = [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "v": 0} for c in side["colliders"]]
        pr8_openings.apply(A, m, {k: v[:3] for k, v in doors.items()}, holes=list(holes), floor_visible=False)
        A["companions"] = [{"asset": f[0], "pos": [round(m["f%d" % i][0], 3), round(m["f%d" % i][1] - 0.015, 3), round(m["f%d" % i][2], 3)], "yaw": f[3]}
                           for i, f in enumerate(furnish(bid))]
        A["companionsOwned"] = True
        open(os.path.join(ROOT, "content", "assets", bid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        rows.append("| `%s` | %s | %s | %s | %s | modified: turned square, ground-floor doors and windows cut and fitted, lined inside, colliders, baked / textures resized | yes |" % ((bid,) + CREDIT))
        print("%-22s %.2f x %.2f x %.2f m  tris %d  parts %d  doors+windows %d  furniture %d" % (bid, sx, sz, sy, side["tris"], len(A["parts"]), len(A["interactables"]), len(A["companions"])))
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- vh1_assets: written by tools/props/vh1_assets.py -->", "<!-- /vh1_assets -->"
    table = (a + "\nVH1: the user's House Village set, one asset per house.\n\n"
             "| Asset | Model | Author | Licence | Source | Changes | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n" + b)
    doc = doc.replace("\r\n", "\n")
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\n", nl))


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
