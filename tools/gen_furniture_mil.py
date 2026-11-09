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
    "blanket_grey": ([0.22, 0.22, 0.2], 0.97, "textile", 0),
    "straw_tick": ([0.46, 0.4, 0.27], 0.95, "textile", 1),
    "map_paper": ([0.56, 0.55, 0.42], 0.9, "paper", 1),
    "bakelite": ([0.05, 0.035, 0.025], 0.35, "plain", 1),
    "dial_face": ([0.7, 0.66, 0.52], 0.4, "paper", 1),
    "rubber": ([0.03, 0.03, 0.03], 0.9, "plain", 1),
    "pin_red": ([0.5, 0.04, 0.03], 0.5, "plain", 1),
    "pin_blue": ([0.05, 0.08, 0.3], 0.5, "plain", 1),
    "brass_case": ([0.55, 0.4, 0.17], 0.35, "metal", 1),
    "steel_bare": ([0.5, 0.5, 0.5], 0.4, "metal", 1),
    "stencil": ([0.62, 0.6, 0.52], 0.8, "plain", 1),
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
    a = mk("furn_mil_bunk", "Steel bunk (two-tier)", "Two-tier steel-tube bunk, 2.0 x 0.86 m: field-grey frame with a foot ladder, straw ticks and folded grey wool blankets, a pillow on each tier.",
           "metal", 24, 55, wall=True, front=0.7)
    W, D, H = 2.0, 0.86, 1.78
    for sx in (-1, 1):
        for sz in (-1, 1):
            tube(a, [sx * (W / 2 - 0.03), 0, sz * (D / 2 - 0.03)], [sx * (W / 2 - 0.03), H, sz * (D / 2 - 0.03)], 0.042, "field_grey", "frame")
    for y in (0.36, 1.26):
        for sz in (-1, 1):
            tube(a, [-W / 2 + 0.03, y, sz * (D / 2 - 0.03)], [W / 2 - 0.03, y, sz * (D / 2 - 0.03)], 0.036, "field_grey", "frame")
        for sx in (-1, 1):
            tube(a, [sx * (W / 2 - 0.03), y, -D / 2 + 0.03], [sx * (W / 2 - 0.03), y, D / 2 - 0.03], 0.036, "field_grey", "frame")
        a.r([0, y + 0.012, 0], [W - 0.1, 0.012, D - 0.1], "steel_bare", "frame", 0.004)                 # sprung base
    for sz in (-1, 1):
        tube(a, [-W / 2 + 0.03, H - 0.06, sz * (D / 2 - 0.03)], [W / 2 - 0.03, H - 0.06, sz * (D / 2 - 0.03)], 0.03, "field_grey", "frame")
    for y in (0.62, 0.92):   # ladder rungs at the foot end
        tube(a, [W / 2 - 0.03, y, -D / 2 + 0.03], [W / 2 - 0.03, y, D / 2 - 0.03], 0.03, "field_grey", "frame")
    for i, y in enumerate((0.36, 1.26)):
        tier = "lower" if i == 0 else "upper"
        a.r([0, y + 0.085, 0], [W - 0.14, 0.13, D - 0.14], "straw_tick", tier, 0.05)
        a.r([-W / 2 + 0.3, y + 0.19, 0], [0.34, 0.09, D - 0.3], "ticking", tier, 0.04)                   # pillow
        a.r([0.35, y + 0.17, 0.02 - 0.05 * i], [0.95, 0.06, D - 0.18], "blanket_grey", "blankets", 0.025)  # folded blanket
        a.r([0.62, y + 0.215, 0.0], [0.42, 0.035, D - 0.26], "blanket_grey", "blankets", 0.015)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["blankets"], keep=["frame", "lower", "upper"])


def locker(root):
    a = mk("furn_mil_locker", "Steel locker (double)", "Double steel kit locker, 0.9 x 0.5 x 1.9 m, field grey: two doors with louvre vents, handles, a padlock and card holders; a steel helmet on top.",
           "metal", 20, 50, wall=True, front=0.7)
    W, D, H = 0.9, 0.5, 1.9
    a.r([0, H / 2, 0], [W, H, D], "field_grey", "carcass", 0.008)
    for sx in (-1, 1):
        x = sx * W / 4
        a.r([x, H / 2 + 0.04, D / 2 + 0.004], [W / 2 - 0.03, H - 0.16, 0.012], "field_grey", "doors", 0.004)
        for y0 in (0.25, H - 0.35):
            for k in range(5):
                a.r([x, y0 + k * 0.035, D / 2 + 0.012], [0.22, 0.012, 0.01], "rubber", "doors", 0.002)
        a.r([x - sx * 0.17, 1.0, D / 2 + 0.03], [0.02, 0.14, 0.03], "steel_bare", "doors", 0.004)
        a.r([x, 1.45, D / 2 + 0.013], [0.1, 0.06, 0.004], "stencil", "doors", 0.001)                      # name card
    a.r([-0.02, 1.0, D / 2 + 0.05], [0.04, 0.05, 0.02], "brass", "doors", 0.004)                           # padlock
    a.cyl([0.12, H + 0.06, 0], 0.3, 0.12, "field_grey", "helmet", top=0.55, seg=16)
    a.cyl([0.12, H + 0.012, 0], 0.36, 0.024, "field_grey", "helmet", top=0.9, seg=16)
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["doors", "helmet"], keep=["carcass"])


def radio(root):
    a = mk("furn_mil_radio", "Radio desk", "Plank radio desk, 1.5 x 0.65 m: two field-grey transceivers with dials and knobs, headphones, a Morse key, a field telephone, a log book and wiring; a battery box underneath.",
           "metal", 18, 45, wall=True, front=0.8)
    W, D, H = 1.5, 0.65, 0.76
    a.r([0, H - 0.02, 0], [W, 0.04, D], "pine", "desk", 0.004)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.05), (H - 0.04) / 2, sz * (D / 2 - 0.05)], [0.06, H - 0.04, 0.06], "pine", "desk", 0.004)
    a.r([0, 0.12, -0.05], [W - 0.2, 0.03, D - 0.15], "pine", "desk", 0.003)                                 # shelf
    a.r([0.35, 0.27, -0.08], [0.45, 0.27, 0.35], "olive_paint", "battery", 0.008)
    for i, (x, w) in enumerate(((-0.4, 0.46), (0.22, 0.4))):
        y = H + 0.17
        a.r([x, y, -0.12], [w, 0.34, 0.36], "field_grey", f"set{i}", 0.01)
        a.r([x, y + 0.06, 0.065], [w - 0.06, 0.13, 0.01], "dial_face", f"set{i}", 0.002)
        for k in range(3):
            a.cyl([x - w / 2 + 0.09 + k * (w - 0.18) / 2, y - 0.08, 0.075], 0.035, 0.03, "bakelite", f"set{i}", seg=10, rot=[90, 0, 0])
        a.r([x, y + 0.18, -0.12], [w + 0.01, 0.02, 0.37], "field_grey", f"set{i}", 0.004)
    a.arch([-0.75 + 0.95, H + 0.06, 0.18], [0.2, 0.12, 0.03], "bakelite", "phones", th=0.02, rot=[90, 0, 0])   # headphones
    for sx in (-1, 1):
        a.cyl([0.2 + sx * 0.1, H + 0.02, 0.18], 0.07, 0.03, "bakelite", "phones", seg=12)
    a.r([-0.15, H + 0.015, 0.22], [0.12, 0.03, 0.06], "brass", "phones", 0.004)                              # Morse key
    a.r([0.6, H + 0.06, 0.12], [0.22, 0.12, 0.17], "bakelite", "phones", 0.01)                              # field telephone
    a.r([0.6, H + 0.135, 0.12], [0.2, 0.03, 0.05], "bakelite", "phones", 0.01)
    a.r([-0.55, H + 0.008, 0.2], [0.24, 0.012, 0.17], "paper", "phones", 0.002)                             # log book
    for x in (-0.4, 0.22):
        a.cyl([x, H + 0.3, -0.3 - 0.03], 0.012, 0.5, "rubber", "desk", seg=6)
    a.col([0, (H + 0.35) / 2, 0], [W, H + 0.35, D])
    return a.write(root, [W, D], fragments=["set1", "phones"], keep=["desk", "set0", "battery"])


def wall_map(root):
    a = mk("furn_mil_map", "Situation map", "Situation map on a plywood board, 1.3 x 0.9 m, with coloured pins, a red front line, grid squares and a clipboard of orders.",
           "paper", 2, 6, blocking=False, wall=True, front=0.0, mount=1.15)
    a.r([0, 0.45, 0.01], [1.3, 0.9, 0.02], "pine", "board", 0.004)
    a.r([0, 0.45, 0.022], [1.2, 0.8, 0.003], "map_paper", "map", 0.0)
    for i in range(1, 6):
        a.b([-0.6 + i * 0.2, 0.45, 0.0245], [0.003, 0.8, 0.0015], "rubber", "map")
    for j in range(1, 4):
        a.b([0, 0.05 + j * 0.2, 0.0245], [1.2, 0.003, 0.0015], "rubber", "map")
    for k, (x0, y0, x1, y1) in enumerate(((-0.5, 0.25, -0.15, 0.4), (-0.15, 0.4, 0.2, 0.33), (0.2, 0.33, 0.52, 0.55))):
        L = math.hypot(x1 - x0, y1 - y0)
        a.b([(x0 + x1) / 2, (y0 + y1) / 2, 0.026], [L, 0.008, 0.002], "pin_red", "map", rot=[0, 0, math.degrees(math.atan2(y1 - y0, x1 - x0))])
    pins = [(-0.4, 0.6, "pin_blue"), (-0.1, 0.62, "pin_blue"), (0.25, 0.7, "pin_blue"), (0.1, 0.2, "pin_red"), (-0.3, 0.15, "pin_red"), (0.42, 0.3, "pin_red")]
    for x, y, m in pins:
        a.cyl([x, y, 0.03], 0.016, 0.014, m, "map", seg=8, rot=[90, 0, 0])
    a.r([0.78, 0.35, 0.02], [0.22, 0.3, 0.012], "pine", "orders", 0.003)
    a.r([0.78, 0.33, 0.028], [0.2, 0.26, 0.002], "paper", "orders", 0.0)
    return a.write(root, [1.5, 0.04], fragments=["orders"], keep=["board", "map"])


def ammo_rack(root):
    a = mk("furn_mil_ammo_rack", "Ammunition rack", "Timber ammunition rack, 1.6 x 0.5 x 1.8 m, on four shelves: steel MG belt boxes, rope-handled shell crates and grenade boxes.",
           "wood", 22, 60, wall=True, front=0.8)
    W, D, H = 1.6, 0.5, 1.8
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.r([sx * (W / 2 - 0.04), H / 2, sz * (D / 2 - 0.04)], [0.07, H, 0.07], "pine", "frame", 0.005)
    ys = (0.08, 0.52, 0.96, 1.4)
    for y in ys:
        a.r([0, y, 0], [W, 0.035, D], "pine", "frame", 0.004)
    a.b([0, 1.6, -D / 2 + 0.02], [W, 0.06, 0.02], "pine", "frame")
    for i, y in enumerate(ys):
        g = f"load{i}"
        if i % 2 == 0:
            for k in range(5):
                x = -W / 2 + 0.18 + k * 0.31
                a.r([x, y + 0.13, 0.0], [0.28, 0.2, 0.11], "field_grey", g, 0.006)
                a.r([x, y + 0.245, 0.0], [0.1, 0.02, 0.03], "steel_bare", g, 0.004)
                a.r([x, y + 0.13, 0.058], [0.08, 0.05, 0.004], "stencil", g, 0.001)
        else:
            for k in range(2):
                x = -W / 2 + 0.4 + k * 0.78
                a.r([x, y + 0.13, 0.0], [0.72, 0.22, 0.4], "olive_paint", g, 0.008)
                a.r([x, y + 0.13, 0.203], [0.5, 0.06, 0.004], "stencil", g, 0.001)
                for sx in (-1, 1):
                    a.arch([x + sx * 0.37, y + 0.17, 0.0], [0.14, 0.05, 0.016], "sacking", g, th=0.012, rot=[0, 90, 0])
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["load0", "load1"], keep=["frame", "load2", "load3"], surfaces=[[-0.75, -0.2, 0.75, 0.2, 1.42]])


def crates(root):
    a = mk("furn_mil_crates", "Ammunition crates", "Stack of three olive-painted ammunition crates with rope handles, iron corner straps and stencilled lot bands.",
           "wood", 14, 40, wall=True, front=0.4)
    for i, (x, y, z, yaw) in enumerate(((-0.42, 0.0, 0.0, 0), (0.42, 0.0, 0.0, 0), (0.0, 0.46, 0.02, 7))):
        g = f"crate{i}"
        a.r([x, y + 0.22, z], [0.8, 0.44, 0.42], "olive_paint", g, 0.01, rot=[0, yaw, 0] if yaw else None)
        a.r([x, y + 0.22, z + 0.213], [0.5, 0.07, 0.004], "stencil", g, 0.001, rot=[0, yaw, 0] if yaw else None)
        for sx in (-1, 1):
            a.r([x + sx * 0.37, y + 0.22, z], [0.03, 0.45, 0.43], "iron", g, 0.003, rot=[0, yaw, 0] if yaw else None)
    a.col([0, 0.45, 0], [1.64, 0.9, 0.44])
    return a.write(root, [1.64, 0.44], fragments=["crate2"], keep=["crate0", "crate1"], surfaces=[[-0.38, -0.18, 0.38, 0.18, 0.9]])


def map_table(root):
    a = mk("furn_mil_table", "Map table (trestle)", "Plank map table on two trestles, 1.8 x 0.9 m, with a spread map, dividers, a compass, a set square and pencils.",
           "wood", 20, 45, front=0.6)
    W, D, H = 1.8, 0.9, 0.78
    for k in range(4):
        a.r([0, H - 0.02, -D / 2 + 0.1125 + k * 0.225], [W, 0.04, 0.215], "pine", "top", 0.004)
    for sx in (-1, 1):
        x = sx * (W / 2 - 0.25)
        for sz in (-1, 1):
            a.b([x, (H - 0.05) / 2, sz * 0.22], [0.05, H - 0.05, 0.05], "pine", "trestle", rot=[sz * 14, 0, 0])
        a.r([x, H - 0.065, 0], [0.07, 0.05, D - 0.1], "pine", "trestle", 0.004)
        a.r([x, 0.25, 0], [0.04, 0.04, 0.5], "pine", "trestle", 0.003)
    a.r([-0.1, H + 0.002, 0.02], [1.1, 0.002, 0.74], "map_paper", "map", 0.0)
    for i in range(1, 5):
        a.b([-0.65 + i * 0.22, H + 0.0035, 0.02], [0.003, 0.0012, 0.74], "rubber", "map")
    a.b([0.1, H + 0.004, 0.0], [0.5, 0.005, 0.006], "pin_red", "map", rot=[0, 25, 0])
    a.r([0.62, H + 0.008, -0.2], [0.22, 0.012, 0.16], "paper", "tools", 0.002)
    a.b([0.55, H + 0.006, 0.2], [0.22, 0.004, 0.012], "steel_bare", "tools", rot=[0, 30, 0])
    a.b([0.55, H + 0.006, 0.2], [0.22, 0.004, 0.012], "steel_bare", "tools", rot=[0, 48, 0])
    a.cyl([0.72, H + 0.012, 0.05], 0.07, 0.02, "brass", "tools", seg=14)
    a.b([-0.62, H + 0.006, -0.25], [0.15, 0.006, 0.006], "pin_blue", "tools", rot=[0, 70, 0])
    a.col([0, H / 2, 0], [W, H, D])
    return a.write(root, [W, D], fragments=["top", "tools"], keep=["trestle"], surfaces=[[0.5, -0.4, 0.85, 0.4, H]])


def gasmask_hooks(root):
    a = mk("furn_mil_gasmask_hooks", "Gas-mask and helmet pegs", "Wall rail with five fluted gas-mask canisters on their straps and two steel helmets.",
           "cloth", 4, 10, blocking=False, wall=True, front=0.0, mount=1.38)
    a.r([0, 0.0, 0.02], [1.3, 0.08, 0.025], "pine", "rail", 0.004)
    for k in range(5):
        x = -0.52 + k * 0.26
        a.cyl([x, 0.0, 0.05], 0.015, 0.06, "iron", "rail", seg=6, rot=[90, 0, 0])
        a.b([x, -0.12, 0.06], [0.02, 0.22, 0.004], "sacking", "masks")
        for f in range(6):   # fluted canister: a ring of thin ribs round a drum
            ang = f * 60
            a.r([x + 0.055 * math.cos(math.radians(ang)), -0.42, 0.09 + 0.055 * math.sin(math.radians(ang))], [0.025, 0.25, 0.025], "field_grey", "masks", 0.008)
        a.cyl([x, -0.42, 0.09], 0.1, 0.26, "field_grey", "masks", seg=12)
        a.cyl([x, -0.285, 0.09], 0.11, 0.02, "field_grey", "masks", seg=12)
    for x in (-0.65, 0.65):
        a.cyl([x, -0.1, 0.13], 0.28, 0.16, "field_grey", "helmets", top=0.6, seg=16, rot=[-80, 0, 0])
        a.cyl([x, -0.1, 0.06], 0.33, 0.02, "field_grey", "helmets", seg=16, rot=[-80, 0, 0])
    return a.write(root, [1.4, 0.22], fragments=["masks", "helmets"], keep=["rail"])


def rifle_rack(root):
    a = mk("furn_mil_rifle_rack", "Rifle rack", "Timber floor rack for six bolt-action rifles, stocks in a notched base, barrels in a notched rail.",
           "wood", 14, 35, wall=True, front=0.5)
    W, D = 1.3, 0.35
    a.r([0, 0.05, 0], [W, 0.1, D], "pine", "rack", 0.005)
    a.r([0, 1.0, -D / 2 + 0.04], [W, 0.07, 0.08], "pine", "rack", 0.005)
    for sx in (-1, 1):
        a.r([sx * (W / 2 - 0.04), 0.6, -D / 2 + 0.04], [0.07, 1.1, 0.07], "pine", "rack", 0.005)
    for k in range(6):
        x = -W / 2 + 0.13 + k * 0.21
        g = f"rifle{k % 3}"
        a.b([x, 0.38, 0.03], [0.05, 0.62, 0.11], "oak", g, rot=[-8, 0, 0])            # stock and fore-end
        a.b([x, 0.19, 0.055], [0.045, 0.2, 0.15], "oak", g, rot=[-8, 0, 0])
        a.cyl([x, 1.0, -0.05], 0.018, 0.55, "iron", g, seg=8, rot=[-8, 0, 0])          # barrel
        a.cyl([x + 0.03, 0.68, 0.05], 0.012, 0.05, "iron", g, seg=6, rot=[0, 0, 90])    # bolt handle
    a.col([0, 0.65, 0], [W, 1.3, D])
    return a.write(root, [W, D], fragments=["rifle1", "rifle2"], keep=["rack", "rifle0"])


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
            for s in (-1, 1):
                a.b([x + 0.088, 0.24, 0], [0.006, 0.36, 0.022], "olive_paint", g, rot=[s * 36, 0, 0])
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
    return a.write(root, [0.3, 0.2], fragments=[], keep=["body"])


def fusebox(root):
    a = mk("furn_mil_fusebox", "Fuse box and conduits", "Wall-mounted steel fuse box with a rotary switch, two conduits running up to the ceiling and a cable gland below.",
           "metal", 4, 10, blocking=False, wall=True, front=0.0, mount=1.25)
    a.r([0, 0.25, 0.08], [0.45, 0.5, 0.16], "field_grey", "box", 0.01)
    a.cyl([0.12, 0.3, 0.17], 0.08, 0.02, "bakelite", "box", seg=12, rot=[90, 0, 0])
    a.r([0.12, 0.3, 0.185], [0.012, 0.07, 0.015], "bakelite", "box", 0.003)
    a.r([-0.08, 0.35, 0.162], [0.12, 0.06, 0.004], "stencil", "box", 0.001)
    for x in (-0.12, 0.05):
        a.cyl([x, 0.5 + 0.6, 0.04], 0.03, 1.2, "field_grey", "box", seg=8)
    a.cyl([0, -0.06, 0.08], 0.05, 0.12, "rubber", "box", seg=8)
    return a.write(root, [0.45, 0.2], fragments=[], keep=["box"])


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    for o in (bunk(root), locker(root), radio(root), wall_map(root), ammo_rack(root), crates(root), map_table(root), gasmask_hooks(root),
              rifle_rack(root), generator(root), jerrycans(root), phone(root), fusebox(root)):
        print("wrote", o.name)


if __name__ == "__main__":
    main()
