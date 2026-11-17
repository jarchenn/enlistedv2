"""GX6: military room furniture for the `mil_stronghold` (and any `roomTypes` mil_* room): original parametric props in
the gen_furniture.py format (docs/interiors.md, docs/asset-format.md "Furniture props"). Front faces +z; wall pieces
stand with their back at -z; wall-hung pieces (mount) start at y 0.

Writes content/assets/furn_mil_*.json only. Deterministic and safe to rerun.
Usage: python -I tools/gen_furniture_mil.py [repo root]
"""
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_furniture import A  # noqa: E402  (the shared builder; MATS covers the common materials)

MIL = {
    "field_grey": ([0.17, 0.18, 0.16], 0.58, "metal", 1),        # painted steel (dielectric paint)
    "olive_paint": ([0.12, 0.13, 0.07], 0.62, "paintwood", -1),
    "blanket_grey": ([0.2, 0.2, 0.19], 0.97, "textile", 0),
    "blanket_stripe": ([0.09, 0.09, 0.085], 0.97, "textile", 0),
    "straw_tick": ([0.55, 0.46, 0.3], 0.95, "textile", 2),       # PR4 batch 5: coarse jute with a faint check, not ticking stripes
    "tick_sag": ([0.4, 0.33, 0.21], 0.95, "textile", 0),
    "map_paper": ([0.56, 0.55, 0.42], 0.9, "paper", 1),
    "bakelite": ([0.05, 0.035, 0.025], 0.35, "plain", 1),
    "dial_face": ([0.7, 0.66, 0.52], 0.4, "paper", 1),
    "rubber": ([0.03, 0.03, 0.03], 0.9, "plain", 1),
    "pin_red": ([0.5, 0.04, 0.03], 0.5, "plain", 1),
    "pin_blue": ([0.05, 0.08, 0.3], 0.5, "plain", 1),
    "brass_case": ([0.55, 0.4, 0.17], 0.35, "metal", 1),
    "steel_bare": ([0.5, 0.5, 0.5], 0.4, "metal", 1),
    "stencil": ([0.62, 0.6, 0.52], 0.8, "plain", 1),
    # PR4 batch 6
    "field_grey_gloss": ([0.17, 0.18, 0.16], 0.35, "plain", 1),     # semi-gloss locker paint (plain: the "metal" pattern blotches it all over)
    "stencil_black": ([0.03, 0.03, 0.03], 0.8, "plain", 1),         # lot bands, block characters (no legible text)
    "mil_crate_paint": ([0.27, 0.29, 0.22], 0.65, "paintwood", -1),   # (not "crate_olive": textures.json maps that name to the teal, rusted paint_green set)
    "rope": ([0.52, 0.43, 0.28], 0.9, "textile", 0),
    "map_green": ([0.32, 0.42, 0.22], 0.85, "paper", 1),
    "map_blue": ([0.36, 0.5, 0.62], 0.85, "paper", 1),
    "map_brown": ([0.5, 0.36, 0.22], 0.85, "paper", 1),
    "map_black": ([0.1, 0.1, 0.1], 0.85, "paper", 1),
    "porcelain_w": ([0.86, 0.85, 0.8], 0.25, "plain", 1),
    "leather_liner": ([0.22, 0.13, 0.07], 0.6, "plain", 1),
    "yellow_plate": ([0.75, 0.6, 0.1], 0.5, "plain", 1),
    "mug_enamel": ([0.55, 0.55, 0.5], 0.35, "plain", 1),
}


def mk(*args, **kw):
    a = A(*args, **kw)
    a.extra_mats.update(MIL)
    return a


def tube(a, p0, p1, d, m, grp, seg=10):
    """steel tube from p0 to p1 (axis-aligned runs only)"""
    c = [(p0[i] + p1[i]) / 2 for i in range(3)]
    L = math.dist(p0, p1)
    ax = max(range(3), key=lambda i: abs(p1[i] - p0[i]))
    rot = {0: [0, 0, 90], 1: None, 2: [90, 0, 0]}[ax]
    a.cyl(c, d, L, m, grp, seg=seg, rot=rot)


def bunk(root):
    a = mk("furn_mil_bunk", "Steel bunk (two-tier)", "Two-tier steel-tube bunk, 2.0 x 0.86 m: field-grey 3 cm tube frame with a four-rung foot ladder, lumpy jute straw ticks, folded grey wool blankets striped at the ends, a pillow on each tier.",
           "metal", 24, 55, wall=True, front=0.7)
    W, D, H = 2.0, 0.86, 1.78
    for sx in (-1, 1):
        for sz in (-1, 1):
            tube(a, [sx * (W / 2 - 0.03), 0, sz * (D / 2 - 0.03)], [sx * (W / 2 - 0.03), H, sz * (D / 2 - 0.03)], 0.034, "field_grey", "frame")
    for y in (0.36, 1.26):
        for sz in (-1, 1):
            tube(a, [-W / 2 + 0.03, y, sz * (D / 2 - 0.03)], [W / 2 - 0.03, y, sz * (D / 2 - 0.03)], 0.03, "field_grey", "frame")
        for sx in (-1, 1):
            tube(a, [sx * (W / 2 - 0.03), y, -D / 2 + 0.03], [sx * (W / 2 - 0.03), y, D / 2 - 0.03], 0.03, "field_grey", "frame")
        a.r([0, y + 0.012, 0], [W - 0.1, 0.012, D - 0.1], "steel_bare", "frame", 0.004)                 # sprung base
    for sz in (-1, 1):
        tube(a, [-W / 2 + 0.03, H - 0.06, sz * (D / 2 - 0.03)], [W / 2 - 0.03, H - 0.06, sz * (D / 2 - 0.03)], 0.03, "field_grey", "frame")
    for y in (0.56, 0.76, 0.96, 1.16):   # four ladder rungs at the foot end
        tube(a, [W / 2 - 0.03, y, -D / 2 + 0.03], [W / 2 - 0.03, y, D / 2 - 0.03], 0.026, "field_grey", "frame")
    L = W - 0.14
    for i, y in enumerate((0.36, 1.26)):
        tier = "lower" if i == 0 else "upper"
        a.soft("s", [0, y + 0.085, 0], [L, 0.13, D - 0.14], "straw_tick", tier, puff=0.12, sag=0.15, rad=0.04, wr=0.009, fold=0.22)   # PR4: a lumpy straw tick, sagging in the middle
        a.soft("s", [-W / 2 + 0.3, y + 0.19, 0], [0.34, 0.09, D - 0.3], "straw_tick", tier, puff=0.5, dent=0.3, rad=0.03, wr=0.004, fold=0.12)   # pillow
        bx = 0.35
        a.soft("s", [bx, y + 0.18, 0.02 - 0.05 * i], [0.95, 0.06, D - 0.18], "blanket_grey", "blankets", puff=0.15, rad=0.025, wr=0.004, fold=0.15)    # folded blanket
        a.soft("s", [0.62, y + 0.225, 0.0], [0.42, 0.035, D - 0.26], "blanket_grey", "blankets", puff=0.2, rad=0.015, wr=0.003, fold=0.12)
        for xs in (bx - 0.4, bx - 0.33, bx + 0.33, bx + 0.4):   # two darker stripes near each end
            a.r([xs, y + 0.181, 0.02 - 0.05 * i], [0.03, 0.062, D - 0.175], "blanket_stripe", "blankets", 0.01)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["blankets"], keep=["frame", "lower", "upper"])


def locker(root):
    a = mk("furn_mil_locker", "Steel locker (double)", "Double steel kit locker, 0.9 x 0.5 x 1.9 m, semi-gloss field grey: two doors with six pressed louvres top and bottom, drop handles, a hasp and padlock, card holders, paint worn to bare steel on the edges and round the handles; a steel helmet on top.",
           "metal", 20, 50, wall=True, front=0.7)
    W, D, H = 0.9, 0.5, 1.9
    a.r([0, H / 2, 0], [W, H, D], "field_grey_gloss", "carcass", 0.008)
    for sx in (-1, 1):
        x = sx * W / 4
        a.r([x, H / 2 + 0.04, D / 2 + 0.004], [W / 2 - 0.03, H - 0.16, 0.012], "field_grey_gloss", "doors", 0.004)
        for y0 in (0.22, H - 0.42):   # six pressed louvres: a raised lip over a dark slot
            for k in range(6):
                y = y0 + k * 0.03
                a.r([x, y, D / 2 + 0.0105], [0.2, 0.012, 0.006], "rubber", "doors", 0.002)
                a.r([x, y + 0.008, D / 2 + 0.013], [0.2, 0.008, 0.008], "field_grey_gloss", "doors", 0.003, rot=[-30, 0, 0])
        hx = x - sx * 0.17   # drop handle on a pivot plate, the paint worn off round it
        a.r([hx, 1.0, D / 2 + 0.0115], [0.06, 0.16, 0.002], "steel_bare", "doors", 0.0008)
        a.cyl([hx, 1.04, D / 2 + 0.02], 0.025, 0.014, "steel_bare", "doors", seg=10, rot=[90, 0, 0])
        a.r([hx, 0.98, D / 2 + 0.03], [0.018, 0.12, 0.012], "steel_bare", "doors", 0.005)
        a.r([x, 1.45, D / 2 + 0.0125], [0.07, 0.05, 0.003], "brass", "doors", 0.001)                      # card holder
        a.r([x, 1.45, D / 2 + 0.0145], [0.06, 0.04, 0.002], "paper", "doors", 0.0005)
        for ey in (0.13, H - 0.07):   # worn top and bottom door edges
            a.r([x, ey, D / 2 + 0.0105], [W / 2 - 0.05, 0.006, 0.0012], "steel_bare", "doors", 0.0004)
    a.r([0, 1.0, D / 2 + 0.035], [0.05, 0.02, 0.012], "steel_bare", "doors", 0.003)                          # hasp
    a.r([0, 0.955, D / 2 + 0.045], [0.04, 0.05, 0.02], "brass", "doors", 0.005)                              # padlock
    a.arch([0, 0.985, D / 2 + 0.045], [0.03, 0.03, 0.006], "steel_bare", "doors", th=0.005)
    a.cyl([0.12, H + 0.06, 0], 0.3, 0.12, "field_grey", "helmet", top=0.55, seg=16)
    a.cyl([0.12, H + 0.012, 0], 0.36, 0.024, "field_grey", "helmet", top=0.9, seg=16)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["doors", "helmet"], keep=["carcass"])


def radio(root):
    a = mk("furn_mil_radio", "Radio desk", "Rough plank radio desk on trestles, 1.5 x 0.65 m: a field-grey transceiver with two large tuning dials in scale windows, four knobs and jacks, its battery box beside it, headphones, a brass Morse key on a black base, a field telephone, log sheets and cables.",
           "metal", 18, 45, wall=True, front=0.8)
    W, D, H = 1.5, 0.65, 0.76
    for k in range(3):   # rough planks, 4 cm
        a.r([0, H - 0.02, -D / 2 + D / 6 + k * D / 3], [W, 0.04, D / 3 - 0.006], "pine", "desk", 0.004)
    for sx in (-1, 1):   # trestles: A-frame legs with a cross brace
        x = sx * (W / 2 - 0.12)
        for sz in (-1, 1):
            a.r([x, (H - 0.04) / 2, sz * 0.17], [0.05, H - 0.02, 0.05], "pine", "desk", 0.006, rot=[sz * 12, 0, 0])
        a.r([x, H - 0.06, 0], [0.07, 0.04, D - 0.08], "pine", "desk", 0.004)
        a.r([x, 0.22, 0], [0.04, 0.04, 0.38], "pine", "desk", 0.003)
    # the transceiver (Torn.Fu-style): 0.36 x 0.30 x 0.20 in field-grey crackle paint
    x0, y0 = -0.3, H + 0.15
    a.r([x0, y0, -0.12], [0.36, 0.3, 0.2], "field_grey", "set0", 0.01)
    a.r([x0, y0, -0.019], [0.33, 0.27, 0.006], "field_grey", "set0", 0.004)                               # front panel
    for k, dx in enumerate((-0.08, 0.08)):   # two tuning dials in scale windows
        a.r([x0 + dx, y0 + 0.07, -0.014], [0.11, 0.05, 0.004], "dial_face", "set0", 0.001)
        a.cyl([x0 + dx, y0 + 0.0, -0.01], 0.06, 0.02, "bakelite", "set0", seg=16, rot=[90, 0, 0])
        a.r([x0 + dx, y0 + 0.025, -0.0], [0.006, 0.012, 0.004], "stencil", "set0", 0.0008)
    for k in range(4):
        a.cyl([x0 - 0.12 + k * 0.08, y0 - 0.09, -0.01], 0.025, 0.018, "bakelite", "set0", seg=10, rot=[90, 0, 0])
    for k in range(3):
        a.cyl([x0 + 0.1 + k * 0.03, y0 - 0.115, -0.014], 0.01, 0.01, "steel_bare", "set0", seg=8, rot=[90, 0, 0])   # jacks
    a.r([x0 + 0.33, H + 0.1, -0.12], [0.22, 0.2, 0.2], "olive_paint", "battery", 0.008)                    # battery box
    a.r([x0 + 0.33, H + 0.205, -0.12], [0.06, 0.012, 0.03], "steel_bare", "battery", 0.003)
    # headphones: two cups on a band, lying on the desk
    a.arch([0.25, H + 0.002, 0.16], [0.18, 0.1, 0.016], "bakelite", "phones", th=0.014, rot=[90, 0, 0])
    for sx in (-1, 1):
        a.cyl([0.25 + sx * 0.085, H + 0.02, 0.16], 0.07, 0.035, "bakelite", "phones", seg=14)
    a.r([0.0, H + 0.01, 0.2], [0.12, 0.02, 0.08], "bakelite", "phones", 0.004)                             # Morse key: black base
    a.r([0.0, H + 0.026, 0.2], [0.09, 0.008, 0.012], "brass", "phones", 0.002, rot=[0, 0, 4])              # brass lever
    a.cyl([0.04, H + 0.036, 0.2], 0.02, 0.012, "bakelite", "phones", seg=10)                               # knob
    a.r([0.58, H + 0.06, 0.12], [0.22, 0.12, 0.17], "bakelite", "phones", 0.01)                             # field telephone
    a.r([0.58, H + 0.135, 0.12], [0.2, 0.03, 0.05], "bakelite", "phones", 0.01)
    for k, (x, z, yaw) in enumerate(((-0.55, 0.15, 6), (-0.5, 0.2, -4))):   # log / message sheets
        a.r([x, H + 0.002 + 0.001 * k, z], [0.21, 0.002, 0.3], "paper", "phones", 0.0005, rot=[0, yaw, 0])
    a.cyl([x0, H + 0.33, -0.27], 0.012, 0.5, "rubber", "desk", seg=6)
    tube(a, [x0 + 0.18, H + 0.06, -0.12], [x0 + 0.22, H + 0.06, -0.12], 0.012, "rubber", "desk", seg=6)
    a.col([0, (H + 0.35) / 2, 0], [W, H + 0.35, D])
    return a.write(root, [W, D], fragments=["battery", "phones"], keep=["desk", "set0"])


def topo_map(a, cx, cy, z, w, h, grp, flat=False):
    """A printed topographic map look (no legible text): woods, a coast, contours, roads, grid squares, a pencil front line.
    Drawn in its own plane: x across, the second axis up (wall) or along -z (flat on a table)."""
    def P(u, v, dz): return [cx + u, cy + dz, z - v] if flat else [cx + u, cy + v, z + dz]
    def R(u, v, su, sv, m, dz, rot=0.0, t=0.002):
        if flat: a.r(P(u, v, dz), [su, t, sv], m, grp, 0.0, rot=[0, rot, 0] if rot else None)
        else: a.r(P(u, v, dz), [su, sv, t], m, grp, 0.0, rot=[0, 0, rot] if rot else None)
    R(0, 0, w, h, "map_paper", 0.0, t=0.003)
    for (u, v, su, sv, r) in ((-0.3, 0.15, 0.3, 0.2, 12), (-0.18, 0.2, 0.18, 0.14, -20), (0.25, -0.2, 0.26, 0.16, 8), (0.1, 0.28, 0.14, 0.1, 30)):
        R(u * w, v * h, su * w, su * h * 1.4, "map_green", 0.0018, r)                                        # woods
    R(-0.42 * w, -0.25 * h, 0.16 * w, 0.5 * h, "map_blue", 0.0018, 4)                                        # the sea along one edge
    for k in range(5):   # contour lines round a hill
        R(0.18 * w, 0.1 * h, (0.08 + 0.05 * k) * w, 0.0035, "map_brown", 0.0022 + 0.0001 * k, 18 * k - 30)
        R(0.18 * w, 0.1 * h, 0.0035, (0.06 + 0.04 * k) * h, "map_brown", 0.0022 + 0.0001 * k, 18 * k - 30)
    for (u0, v0, u1, v1, m) in ((-0.5, -0.05, 0.5, 0.12, "pin_red"), (-0.1, -0.5, 0.05, 0.5, "map_black"), (0.2, -0.5, 0.35, 0.1, "pin_red")):   # roads
        L = math.hypot((u1 - u0) * w, (v1 - v0) * h)
        R((u0 + u1) / 2 * w, (v0 + v1) / 2 * h, L, 0.006, m, 0.0026, math.degrees(math.atan2((v1 - v0) * h, (u1 - u0) * w)) * (-1 if flat else 1))
    for i in range(1, 6):   # grid squares
        R(-w / 2 + i * w / 6, 0, 0.0025, h * 0.98, "map_black", 0.0028)
    for j in range(1, 4):
        R(0, -h / 2 + j * h / 4, w * 0.98, 0.0025, "map_black", 0.0028)
    for k, (u0, v0, u1, v1, m) in enumerate(((-0.38, -0.3, -0.1, -0.02, "pin_red"), (-0.1, -0.02, 0.15, 0.06, "pin_red"), (0.15, 0.06, 0.4, 0.32, "pin_red"),
                                             (-0.36, -0.22, -0.08, 0.06, "pin_blue"), (-0.08, 0.06, 0.18, 0.14, "pin_blue"))):   # pencil front lines
        L = math.hypot((u1 - u0) * w, (v1 - v0) * h)
        R((u0 + u1) / 2 * w, (v0 + v1) / 2 * h, L, 0.007, m, 0.0032, math.degrees(math.atan2((v1 - v0) * h, (u1 - u0) * w)) * (-1 if flat else 1))


def wall_map(root):
    a = mk("furn_mil_map", "Situation map", "Situation map on a plywood board, 1.3 x 0.9 m: a printed topographic sheet (woods, coast, contours, roads, grid), coloured pins along red and blue pencil front lines, curling tacked corners, a clipboard of orders hung at the corner.",
           "paper", 2, 6, blocking=False, wall=True, front=0.0, mount=1.15)
    a.r([0, 0.45, 0.01], [1.3, 0.9, 0.02], "pine", "board", 0.004)
    topo_map(a, 0, 0.45, 0.021, 1.2, 0.8, "map")
    pins = [(-0.4, 0.3, "pin_red"), (-0.15, 0.43, "pin_red"), (0.1, 0.5, "pin_red"), (0.36, 0.64, "pin_red"), (-0.38, 0.38, "pin_blue"), (-0.12, 0.51, "pin_blue"), (0.14, 0.58, "pin_blue")]
    for x, y, m in pins:   # map pins: a head on a short steel shank
        a.cyl([x, y, 0.031], 0.003, 0.012, "steel_bare", "map", seg=6, rot=[90, 0, 0])
        a.cyl([x, y, 0.039], 0.016, 0.01, m, "map", seg=8, rot=[90, 0, 0])
    for sx in (-1, 1):   # tacked corners curling off the board
        for sy in (0, 1):
            x, y = sx * 0.6, 0.05 + sy * 0.8
            a.wedge([x - sx * 0.025, y + (0.025 if sy == 0 else -0.025), 0.03], [0.05, 0.05, 0.012], "map_paper", "map", top=0.0, rot=[0, 0, 0 if sy == 0 else 180])
            a.cyl([x - sx * 0.03, y + (0.03 if sy == 0 else -0.03), 0.034], 0.012, 0.006, "brass", "map", seg=8, rot=[90, 0, 0])
    a.cyl([0.78, 0.53, 0.03], 0.012, 0.012, "steel_bare", "orders", seg=6, rot=[90, 0, 0])                 # the nail
    a.r([0.78, 0.35, 0.025], [0.22, 0.32, 0.012], "pine", "orders", 0.003)                                    # clipboard
    a.r([0.78, 0.49, 0.034], [0.1, 0.03, 0.008], "steel_bare", "orders", 0.003)
    for k in range(3):
        a.r([0.78 + 0.004 * k, 0.33 - 0.004 * k, 0.033 + 0.001 * k], [0.2, 0.27, 0.0015], "paper", "orders", 0.0)
    return a.write(root, [1.5, 0.04], fragments=["orders"], keep=["board", "map"])


def mg_box(a, x, y, z, g, yaw=0):
    """Patronenkasten 34: 0.28 long x 0.10 wide x 0.20 tall, field grey, a hinged lid with a latch and a top carry handle."""
    r = [0, yaw, 0] if yaw else None
    a.r([x, y + 0.095, z], [0.1, 0.19, 0.28], "field_grey", g, 0.006, rot=r)
    a.r([x, y + 0.197, z], [0.105, 0.016, 0.285], "field_grey", g, 0.004, rot=r)                         # the lid
    a.arch([x, y + 0.205, z], [0.03, 0.1, 0.012], "steel_bare", g, th=0.008, rot=[0, 90 + yaw, 0])      # carry handle
    a.r([x, y + 0.16, z + 0.143], [0.03, 0.04, 0.008], "steel_bare", g, 0.002, rot=r)                   # latch
    a.r([x, y + 0.205, z + 0.12], [0.1, 0.004, 0.02], "steel_bare", g, 0.001, rot=r)                    # chipped lid edge


def ammo_rack(root):
    a = mk("furn_mil_ammo_rack", "Ammunition rack", "Rough pine ammunition rack, 1.6 x 0.5 x 1.8 m, braced at the back: rows of field-grey MG belt boxes (Patronenkasten 34) with lids, latches and carry handles, rope-handled shell crates with black lot bands, and a row of stick-grenade crates.",
           "wood", 22, 60, wall=True, front=0.8)
    W, D, H = 1.6, 0.5, 1.8
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.04), H / 2, sz * (D / 2 - 0.04)], [0.08, H, 0.08], "pine", "frame", 0.005)
    ys = (0.08, 0.52, 0.96, 1.4)
    for y in ys:
        a.r([0, y, 0], [W, 0.03, D], "pine", "frame", 0.004)
    L = math.hypot(W - 0.16, 1.3)
    a.r([0, 0.75, -D / 2 + 0.02], [L, 0.06, 0.02], "pine", "frame", 0.004, rot=[0, 0, math.degrees(math.atan2(1.3, W - 0.16))])   # diagonal brace
    a.b([0, 1.6, -D / 2 + 0.02], [W, 0.06, 0.02], "pine", "frame")
    # shelf 0 and 3: MG belt boxes, narrow side out
    for i, y in ((0, ys[0]), (3, ys[3])):
        for k in range(10):
            mg_box(a, -W / 2 + 0.16 + k * 0.142, y + 0.015, 0.05, "load%d" % i)
    # shelf 1: two long shell crates with rope handles and a black lot band
    for k in range(2):
        x, y = -W / 2 + 0.42 + k * 0.76, ys[1] + 0.015
        a.r([x, y + 0.11, 0.0], [0.72, 0.22, 0.4], "mil_crate_paint", "load1", 0.008)
        for j in range(3):
            a.r([x, y + 0.04 + j * 0.07, 0.201], [0.72, 0.003, 0.003], "pine", "load1", 0.0)               # plank seams
        a.r([x, y + 0.13, 0.203], [0.36, 0.05, 0.003], "stencil_black", "load1", 0.0005)
        for c in range(5):
            a.r([x - 0.12 + c * 0.06, y + 0.06, 0.203], [0.03, 0.04, 0.003], "stencil_black", "load1", 0.0005)   # block characters
        for sx in (-1, 1):
            a.arch([x + sx * 0.37, y + 0.15, 0.0], [0.14, 0.05, 0.016], "rope", "load1", th=0.012, rot=[0, 90, 0])
    # shelf 2: stick-grenade crates
    for k in range(3):
        x, y = -W / 2 + 0.28 + k * 0.52, ys[2] + 0.015
        a.r([x, y + 0.1, 0.0], [0.48, 0.2, 0.36], "mil_crate_paint", "load2", 0.008)
        a.r([x, y + 0.205, 0.0], [0.5, 0.02, 0.37], "mil_crate_paint", "load2", 0.004)
        a.r([x, y + 0.1, 0.181], [0.24, 0.04, 0.003], "stencil_black", "load2", 0.0005)
        for sx in (-1, 1):
            a.arch([x + sx * 0.25, y + 0.13, 0.0], [0.1, 0.04, 0.014], "rope", "load2", th=0.01, rot=[0, 90, 0])
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["load0", "load1"], keep=["frame", "load2", "load3"], surfaces=[[-0.75, -0.2, 0.75, 0.2, 1.42]])


def crates(root):
    a = mk("furn_mil_crates", "Ammunition crates", "Stack of three olive-painted plank ammunition crates: plank seams, iron corner straps, rope handles, a black stencilled lot band, paint worn back to the wood on the corners and lid edges.",
           "wood", 14, 40, wall=True, front=0.4)
    for i, (x, y, z, yaw) in enumerate(((-0.42, 0.0, 0.0, 0), (0.42, 0.0, 0.0, 0), (0.0, 0.46, 0.02, 7))):
        g = f"crate{i}"; r = [0, yaw, 0] if yaw else None
        q = math.radians(yaw)
        def at(dx, dz): return [x + dx * math.cos(q) + dz * math.sin(q), None, z - dx * math.sin(q) + dz * math.cos(q)]
        a.r([x, y + 0.22, z], [0.8, 0.44, 0.42], "mil_crate_paint", g, 0.01, rot=r)
        for j in range(1, 4):   # plank seams on the front
            c = at(0, 0.211); a.r([c[0], y + j * 0.11, c[2]], [0.78, 0.005, 0.003], "pine", g, 0.0, rot=r)
        c = at(0, 0.213); a.r([c[0], y + 0.26, c[2]], [0.4, 0.07, 0.004], "stencil_black", g, 0.001, rot=r)
        for k in range(4):
            c = at(-0.15 + k * 0.1, 0.213); a.r([c[0], y + 0.15, c[2]], [0.05, 0.05, 0.004], "stencil_black", g, 0.001, rot=r)
        for sx in (-1, 1):
            c = at(sx * 0.37, 0); a.r([c[0], y + 0.22, c[2]], [0.03, 0.45, 0.43], "iron", g, 0.003, rot=r)       # corner straps
            c = at(sx * 0.405, 0); a.arch([c[0], y + 0.26, c[2]], [0.12, 0.05, 0.015], "rope", g, th=0.015, rot=[0, 90 + yaw, 0])   # rope handles
            for sz in (-1, 1):   # worn corners
                c = at(sx * 0.395, sz * 0.205); a.r([c[0], y + 0.22, c[2]], [0.012, 0.42, 0.012], "pine", g, 0.004, rot=r)
        c = at(0, 0.205); a.r([c[0], y + 0.437, c[2]], [0.76, 0.008, 0.012], "pine", g, 0.002, rot=r)              # worn lid edge
    a.col([0, 0.45, 0], [1.64, 0.9, 0.44])
    return a.write(root, [1.64, 0.44], fragments=["crate2"], keep=["crate0", "crate1"], surfaces=[[-0.38, -0.18, 0.38, 0.18, 0.9]])


def map_table(root):
    a = mk("furn_mil_table", "Map table (trestle)", "Plank map table on two A-frame trestles, 1.8 x 0.9 m: a topographic map with pencil front lines and curled corners, weighted by a compass and an enamel mug; dividers, a set square and pencils.",
           "wood", 20, 45, front=0.6)
    W, D, H = 1.8, 0.9, 0.78
    for k in range(4):
        a.r([0, H - 0.02, -D / 2 + 0.1125 + k * 0.225], [W, 0.04, 0.215], "pine", "top", 0.004)
    for sx in (-1, 1):
        x = sx * (W / 2 - 0.25)
        for sz in (-1, 1):
            a.r([x, (H - 0.05) / 2, sz * 0.22], [0.05, H - 0.05, 0.05], "pine", "trestle", 0.008, rot=[sz * 14, 0, 0])
        a.r([x, H - 0.065, 0], [0.07, 0.05, D - 0.1], "pine", "trestle", 0.004)
        a.r([x, 0.25, 0], [0.04, 0.04, 0.5], "pine", "trestle", 0.003)
    topo_map(a, -0.1, H + 0.0015, 0.02, 1.0, 0.7, "map", flat=True)
    for sx in (-1, 1):   # corners curling up
        for sz in (-1, 1):
            a.wedge([-0.1 + sx * 0.48, H + 0.008, 0.02 + sz * 0.33], [0.04, 0.012, 0.04], "map_paper", "map", top=0.0, rot=[0, 90 * (sx < 0) + 180 * (sz < 0), 0])
    a.cyl([0.33, H + 0.012, -0.28], 0.07, 0.02, "brass", "tools", seg=14)                                    # compass
    a.cyl([0.33, H + 0.023, -0.28], 0.06, 0.002, "dial_face", "tools", seg=14)
    a.cyl([-0.55, H + 0.045, 0.3], 0.08, 0.09, "mug_enamel", "tools", seg=14)                               # enamel mug
    a.arch([-0.505, H + 0.05, 0.3], [0.04, 0.05, 0.008], "mug_enamel", "tools", th=0.008, rot=[0, 90, -90])
    a.r([0.62, H + 0.008, -0.2], [0.22, 0.012, 0.16], "paper", "tools", 0.002)
    a.r([0.55, H + 0.006, 0.2], [0.22, 0.004, 0.012], "steel_bare", "tools", 0.0018, rot=[0, 30, 0])         # dividers
    a.r([0.55, H + 0.006, 0.2], [0.22, 0.004, 0.012], "steel_bare", "tools", 0.0018, rot=[0, 48, 0])
    a.wedge([0.7, H + 0.003, 0.05], [0.2, 0.004, 0.14], "map_paper", "tools", top=0.0)                       # set square
    for k, m in enumerate(("pin_blue", "pin_red")):   # pencils
        a.r([0.3 + 0.05 * k, H + 0.006, 0.28], [0.17, 0.007, 0.007], m, "tools", 0.0025, rot=[0, 70 + 12 * k, 0])
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "tools"], keep=["trestle"], surfaces=[[0.5, -0.4, 0.85, 0.4, H]])


def gasmask_hooks(root):
    a = mk("furn_mil_gasmask_hooks", "Gas-mask and helmet pegs", "Wall rail with five fluted GM-30/38 gas-mask canisters hanging by their canvas straps and two M35 helmets (stepped, flared rim, leather liner inside).",
           "cloth", 4, 10, blocking=False, wall=True, front=0.0, mount=1.38)
    a.r([0, 0.0, 0.02], [1.3, 0.08, 0.025], "pine", "rail", 0.004)
    for k in range(5):
        x = -0.52 + k * 0.26
        a.cyl([x, 0.0, 0.05], 0.015, 0.06, "iron", "rail", seg=6, rot=[90, 0, 0])
        for sx in (-1, 1):   # two canvas straps
            a.b([x + sx * 0.03, -0.12, 0.06 + 0.004 * sx], [0.02, 0.22, 0.004], "sacking", "masks", rot=[0, 0, -sx * 8])
        a.cyl([x, -0.39, 0.09], 0.11, 0.27, "field_grey", "masks", n=10)                                      # fluted drum: 10 flutes
        a.cyl([x, -0.39, 0.09], 0.1, 0.271, "field_grey", "masks", seg=20)
        a.cyl([x, -0.248, 0.09], 0.115, 0.016, "field_grey", "masks", seg=16)                                # the hinged lid
        a.r([x, -0.248, 0.152], [0.02, 0.014, 0.012], "steel_bare", "masks", 0.003)                         # its hinge
        a.r([x, -0.24, 0.03], [0.03, 0.012, 0.015], "steel_bare", "masks", 0.003)                           # catch
    for x in (-0.65, 0.65):   # M35: dome, a stepped flared skirt, the rim, a leather liner
        a.cyl([x, -0.06, 0.17], 0.24, 0.13, "field_grey", "helmets", top=0.55, seg=18, rot=[-80, 0, 0])
        a.cyl([x, -0.065, 0.085], 0.28, 0.05, "field_grey", "helmets", top=0.86, seg=18, rot=[-80, 0, 0])
        a.cyl([x, -0.068, 0.05], 0.33, 0.03, "field_grey", "helmets", top=0.85, seg=18, rot=[-80, 0, 0])   # flared skirt
        a.cyl([x, -0.07, 0.035], 0.335, 0.008, "field_grey", "helmets", seg=18, rot=[-80, 0, 0])            # rolled rim
        a.cyl([x, -0.07, 0.04], 0.24, 0.02, "leather_liner", "helmets", seg=16, rot=[-80, 0, 0])            # liner
    return a.write(root, [1.4, 0.22], fragments=["masks", "helmets"], keep=["rail"])


def rifle_rack(root):
    a = mk("furn_mil_rifle_rack", "Rifle rack", "Timber floor rack for six Kar98k rifles (the game's own rifle model): butts in a notched base rail, muzzles in a notched upper rail at 0.9 m.",
           "wood", 14, 35, wall=True, front=0.5)
    W, D = 1.3, 0.35
    a.r([0, 0.05, 0], [W, 0.1, D], "pine", "rack", 0.005)
    a.r([0, 0.12, 0.05], [W, 0.08, 0.1], "pine", "rack", 0.005)                                              # notched base rail
    a.r([0, 0.95, -D / 2 + 0.05], [W, 0.07, 0.1], "pine", "rack", 0.005)                                     # notched upper rail
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), 0.6, -D / 2 + 0.04], [0.07, 1.1, 0.07], "pine", "rack", 0.005)
    for k in range(6):
        x = -W / 2 + 0.13 + k * 0.21
        for dx in (-0.035, 0.035):   # the notches either side of each rifle
            a.r([x + dx, 0.965, -D / 2 + 0.1], [0.012, 0.04, 0.02], "pine", "rack", 0.002)
            a.r([x + dx, 0.17, 0.1], [0.012, 0.03, 0.02], "pine", "rack", 0.002)
        # the rifle: standing, butt down in the base, leaning back 8 degrees into the upper rail
        a.models.append({"dir": "../../weapons/models/kar98k", "pos": [round(x, 3), 0.1, 0.08], "yaw": 0, "tilt": [-82, 0], "lodDist": 12})
    a.col([0, 0.65, 0], [W, 1.3, D])
    return a.write(root, [W, D], fragments=[], keep=["rack"])


def generator(root):
    a = mk("furn_mil_generator", "Diesel generator set", "Field-grey diesel generator set on a steel skid, 1.7 x 0.75 m: engine block and head, flywheel, radiator with grille, the alternator drum, a control box with gauges and an exhaust stub.",
           "metal", 40, 90, wall=True, front=0.8)
    a.r([0, 0.06, 0], [1.7, 0.12, 0.75], "steel_bare", "skid", 0.01)
    a.r([-0.25, 0.48, 0], [0.75, 0.62, 0.55], "field_grey", "engine", 0.02)
    a.r([-0.25, 0.86, 0], [0.7, 0.16, 0.42], "field_grey", "engine", 0.02)
    for k in range(3):
        a.r([-0.5 + k * 0.25, 0.97, 0], [0.14, 0.06, 0.3], "field_grey", "engine", 0.01)
    a.r([-0.78, 0.55, 0], [0.18, 0.8, 0.62], "field_grey", "engine", 0.01)       # radiator
    for k in range(9):
        a.b([-0.875, 0.25 + k * 0.07, 0], [0.006, 0.02, 0.52], "rubber", "engine")
    a.cyl([0.2, 0.45, 0], 0.62, 0.09, "steel_bare", "engine", seg=24, rot=[0, 0, 90])   # flywheel
    a.cyl([0.55, 0.45, 0], 0.46, 0.5, "field_grey", "alternator", seg=20, rot=[0, 0, 90])
    a.r([0.55, 0.85, 0.2], [0.3, 0.3, 0.12], "field_grey", "alternator", 0.01)
    for k in range(2):
        a.cyl([0.48 + k * 0.14, 0.88, 0.262], 0.08, 0.01, "dial_face", "alternator", seg=12, rot=[90, 0, 0])
    a.cyl([-0.1, 1.15, -0.15], 0.08, 0.35, "steel_bare", "engine", seg=10)
    for k in range(7):   # PR3: cooling fins on the cylinder block
        a.b([-0.25, 0.62 + k * 0.03, 0.29], [0.66, 0.008, 0.04], "field_grey", "engine")
    a.cyl([-0.1, 1.33, -0.07], 0.07, 0.18, "steel_bare", "engine", seg=10, rot=[60, 0, 0])   # PR3: exhaust bend
    a.col([0, 0.6, 0], [1.7, 1.2, 0.75])
    return a.write(root, [1.7, 0.75], fragments=["alternator"], keep=["skid", "engine"])


def jerrycans(root):
    a = mk("furn_mil_jerrycans", "Jerrycans", "Three pressed-steel jerrycans with the X stamping and triple handles, one lying down.",
           "metal", 6, 25, wall=True, front=0.3)
    for i, (x, rot) in enumerate(((-0.24, None), (0.0, None), (0.36, [0, 0, 90]))):
        g = f"can{i}"
        if rot:
            a.r([x, 0.09, 0], [0.47, 0.17, 0.35], "olive_paint", g, 0.02)
        else:
            a.r([x, 0.235, 0], [0.17, 0.47, 0.35], "olive_paint", g, 0.02)
            for fx in (-0.088, 0.088):   # PR3: the X pressing on both faces, a rim weld round the edge
                for s in (-1, 1):
                    a.b([x + fx, 0.24, 0], [0.006, 0.36, 0.022], "olive_paint", g, rot=[s * 36, 0, 0])
            a.b([x, 0.235, 0.176], [0.18, 0.48, 0.006], "olive_paint", g)
            a.b([x, 0.235, -0.176], [0.18, 0.48, 0.006], "olive_paint", g)
            a.cyl([x, 0.5, 0.12], 0.045, 0.04, "steel_bare", g, seg=10)   # PR3: cam-lever spout cap
            for k in range(3):
                a.r([x, 0.49, -0.1 + k * 0.08], [0.03, 0.03, 0.05], "olive_paint", g, 0.008)
    a.col([0.05, 0.25, 0], [0.8, 0.5, 0.36])
    return a.write(root, [0.8, 0.36], fragments=["can0"], keep=["can1", "can2"])


def phone(root):
    a = mk("furn_mil_phone", "Field telephone", "Bakelite field telephone in its case, handset on the cradle, crank on the side.",
           "ceramic", 1, 4, blocking=False, front=0.0)
    a.r([0, 0.06, 0], [0.27, 0.12, 0.2], "bakelite", "body", 0.012)
    a.r([0, 0.14, 0], [0.24, 0.04, 0.06], "bakelite", "body", 0.015)
    for s in (-1, 1):
        a.cyl([s * 0.11, 0.14, 0], 0.06, 0.05, "bakelite", "body", seg=10)
    a.cyl([0.145, 0.07, 0], 0.012, 0.06, "steel_bare", "body", seg=6, rot=[0, 0, 90])
    a.r([0.175, 0.04, 0.03], [0.012, 0.07, 0.012], "steel_bare", "body", 0.003)   # PR3: crank arm and knob
    a.cyl([0.18, 0.005, 0.03], 0.02, 0.03, "bakelite", "body", seg=8, rot=[90, 0, 0])
    a.r([0, 0.19, -0.11], [0.27, 0.15, 0.012], "bakelite", "body", 0.006, rot=[-100, 0, 0])   # PR3: the case lid, open
    a.arch([-0.06, 0.13, 0.1], [0.12, 0.05, 0.008], "rubber", "body", th=0.006, rot=[0, 0, 180])   # PR3: handset cord
    return a.write(root, [0.3, 0.2], fragments=[], keep=["body"])


def fusebox(root):
    a = mk("furn_mil_fusebox", "Fuse box and conduits", "Pressed-steel fuse box (0.30 x 0.40 x 0.15 m), field grey: a hinged door with a recessed latch and a lightning-flash plate, a rotary switch with a handle, two ceramic fuse holders seen through a vent slot, a cable gland below and two conduits rising to the ceiling in saddle clips.",
           "metal", 4, 10, blocking=False, wall=True, front=0.0, mount=1.25)
    a.r([0, 0.25, 0.075], [0.3, 0.4, 0.15], "field_grey", "box", 0.008)
    a.r([-0.02, 0.25, 0.152], [0.25, 0.37, 0.006], "field_grey", "box", 0.003)                               # the door
    for y in (0.13, 0.37):
        a.cyl([0.112, y, 0.152], 0.012, 0.03, "steel_bare", "box", seg=8)                                    # hinges
    a.r([-0.12, 0.25, 0.155], [0.02, 0.05, 0.004], "rubber", "box", 0.001)                                  # recessed latch
    a.r([-0.03, 0.34, 0.156], [0.09, 0.08, 0.002], "yellow_plate", "box", 0.0008)                           # warning plate
    for k, (dx, dy, r) in enumerate(((-0.012, 0.02, -25), (0.006, 0.0, 30), (-0.006, -0.02, -25))):         # lightning flash
        a.r([-0.03 + dx, 0.34 + dy, 0.158], [0.008, 0.03, 0.002], "stencil_black", "box", 0.0005, rot=[0, 0, r])
    a.r([-0.03, 0.18, 0.156], [0.15, 0.03, 0.002], "rubber", "box", 0.0008)                                 # vent slot
    for dx in (-0.06, 0.0):
        a.cyl([-0.03 + dx + 0.03, 0.18, 0.154], 0.018, 0.02, "porcelain_w", "box", seg=10, rot=[90, 0, 0])   # fuse holders
    a.cyl([0.2, 0.3, 0.09], 0.06, 0.03, "bakelite", "box", seg=14, rot=[90, 0, 0])                          # rotary switch at the side
    a.r([0.2, 0.3, 0.11], [0.012, 0.07, 0.016], "bakelite", "box", 0.003)
    for x in (-0.08, 0.06):   # conduits to the ceiling, clipped every 0.4 m
        a.cyl([x, 0.45 + 0.6, 0.035], 0.025, 1.2, "field_grey", "box", seg=8)
        for k in range(3):
            a.r([x, 0.6 + k * 0.4, 0.024], [0.045, 0.02, 0.012], "steel_bare", "box", 0.002)
    a.cyl([0, 0.03, 0.075], 0.04, 0.06, "steel_bare", "box", seg=8)                                         # cable gland
    a.cyl([0, -0.06, 0.075], 0.022, 0.14, "rubber", "box", seg=8)
    return a.write(root, [0.45, 0.2], fragments=[], keep=["box"])


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    for o in (bunk(root), locker(root), radio(root), wall_map(root), ammo_rack(root), crates(root), map_table(root), gasmask_hooks(root),
              rifle_rack(root), generator(root), jerrycans(root), phone(root), fusebox(root)):
        print("wrote", o.name)


if __name__ == "__main__":
    main()
