"""SD2 Rue Haute: Norman town-house frontages that replace the lane walls (content/assets/sd_facade_*.json).

Each is one 6 m house bay on the wall's edge slot: the origin on the edge line, +z facing the lane, the front at
z = +0.31 (the old wall's face, so the play space and the timings are unchanged) and the body 2.9 m back into the solid
cell (two back to back across one cell leave a 0.2 m gap). Two or three storeys under a steep tile or slate roof that
rises away from the lane, a chimney, gutter and downpipe; doors, shuttered casements, shop fronts with awnings,
balconettes, timber framing with a jettied upper floor. Only the body collides: every detail on the lane side is k = 0,
so nothing adds a ledge, a step or a vault to the navmesh.

    <python> -I tools/mapgen/sd_town_facades.py"""
import json
import math
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
W, FRONT, BACK = 6.0, 0.31, -2.9
GF, FH = 3.3, 2.8            # ground floor and upper floor heights (m)
PITCH = math.tan(math.radians(52))   # Norman roofs are steep


def M(name, albedo, pattern, tex=None, rough=0.9, metal=0.0, grain=1, emis=None):
    m = {"name": name, "albedo": albedo, "roughness": rough, "pattern": pattern, "metallic": metal, "grain": grain}
    if tex:
        m["texture"] = tex
    if emis:
        m["emissive"] = emis
    return m


MATS = [M("render_cream", [0.74, 0.68, 0.55], "limewash", "render_cream"), M("render_ochre", [0.8, 0.58, 0.32], "limewash", "render_ochre"),
        M("render_white", [0.8, 0.78, 0.72], "limewash", "render_white"), M("render_grey", [0.62, 0.62, 0.6], "limewash", "render_lime"),
        M("plaster", [0.82, 0.78, 0.68], "limewash", "limewash"),
        M("stone", [0.74, 0.69, 0.6], "stone", "limestone_dressed"), M("dressing", [0.78, 0.74, 0.66], "stone", "limestone_smooth", 0.85),
        M("plinth", [0.42, 0.4, 0.36], "stone", "stone_coursed"),
        M("brick", [0.5, 0.27, 0.19], "brickwork", "brick_old", 0.88),
        M("oak", [0.17, 0.11, 0.07], "wood", "oak_aged", 0.75, grain=1), M("oak_v", [0.17, 0.11, 0.07], "wood", "oak_aged", 0.75, grain=1),
        M("tile", [0.6, 0.33, 0.22], "slate", "roof_tile", 0.75, grain=2), M("slate", [0.26, 0.28, 0.31], "slate", "slate", 0.6, grain=2),
        M("pane", [0.09, 0.11, 0.13], "plain", None, 0.08, 0.25), M("frame", [0.82, 0.8, 0.74], "painted", "window_paint", 0.55),
        M("sh_blue", [0.3, 0.42, 0.5], "painted", "shutter_paint", 0.66), M("sh_green", [0.2, 0.34, 0.27], "painted", "shutter_paint", 0.66),
        M("sh_red", [0.42, 0.13, 0.09], "painted", "shutter_paint", 0.66), M("sh_cream", [0.72, 0.68, 0.56], "painted", "shutter_paint", 0.66),
        M("door_green", [0.09, 0.17, 0.13], "painted", "paint_wood_scratched", 0.6), M("door_red", [0.3, 0.07, 0.05], "painted", "paint_wood_scratched", 0.6),
        M("door_blue", [0.1, 0.16, 0.26], "painted", "paint_wood_scratched", 0.6),
        M("iron", [0.05, 0.048, 0.046], "metal", None, 0.55, 0.15), M("zinc", [0.5, 0.52, 0.54], "metal", None, 0.5, 0.8),
        M("canvas_red", [0.5, 0.09, 0.07], "fabric"), M("canvas_cream", [0.78, 0.74, 0.62], "fabric"), M("canvas_green", [0.13, 0.3, 0.2], "fabric"),
        M("gilt", [0.72, 0.56, 0.22], "metal", None, 0.4, 0.7), M("pot", [0.55, 0.28, 0.17], "plain", None, 0.9),
        M("canvas_navy", [0.06, 0.1, 0.24], "fabric"), M("canvas_wine", [0.3, 0.05, 0.08], "fabric"),
        M("flower_red", [0.7, 0.06, 0.06], "plain", None, 0.8), M("flower_pink", [0.8, 0.3, 0.45], "plain", None, 0.8), M("leaf", [0.12, 0.26, 0.08], "plain", None, 0.85),
        M("slate_hung", [0.24, 0.26, 0.29], "slate", "slate_square", 0.55, grain=1), M("sign_board", [0.62, 0.55, 0.4], "painted", "paint_wood_scratched", 0.6)]


def B(p, c, s, m, k=0, bv=0.0, lod=0, r=None):
    q = {"t": "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "k": k}
    if bv:
        q["bv"] = bv
    if lod:
        q["lod"] = 1
    if r:
        q["r"] = r
    p.append(q)


def flowers(p, x, y, front, w, seed):
    """A window box of geraniums on the sill: a planter, leaves, red or pink heads."""
    B(p, [x, y + 0.09, front + 0.2], [w, 0.18, 0.2], "pot", lod=1)
    B(p, [x, y + 0.22, front + 0.2], [w - 0.06, 0.12, 0.2], "leaf", lod=1)
    col = "flower_red" if seed % 3 else "flower_pink"
    for k in range(5):
        p.append({"t": "r", "c": [round(x - w / 2 + 0.1 + k * (w - 0.2) / 4, 4), round(y + 0.32 + 0.03 * ((k + seed) % 2), 4), round(front + 0.2 + 0.04 * ((k * 7 + seed) % 3 - 1), 4)],
                  "s": [0.16, 0.12, 0.16], "rad": 0.06, "m": col, "k": 0, "lod": 1})


def bracket_sign(p, x, front, board, seed):
    """A painted board hanging from a wrought-iron bracket over the lane, 3 m up."""
    B(p, [x, 3.35, front + 0.45], [0.03, 0.03, 0.9], "iron")
    B(p, [x, 3.05, front + 0.12], [0.03, 0.6, 0.03], "iron", lod=1)
    B(p, [x, 3.2, front + 0.3], [0.02, 0.02, 0.5], "iron", r=[35, 0, 0], lod=1)
    B(p, [x, 2.95, front + 0.62], [0.04, 0.52, 0.66], board)
    B(p, [x, 2.95, front + 0.62], [0.05, 0.36, 0.5], "gilt" if seed % 2 else "canvas_cream", lod=1)


def window(p, x, y0, w, h, front, shutter, surround, balcony=False, frame="frame"):
    """A French casement: dark panes, a centre mullion and two transoms, a raised surround, a sill, open shutters."""
    yc = y0 + h / 2
    B(p, [x, yc, front + 0.01], [w, h, 0.02], "pane")
    for dx in (-w / 2 + 0.035, w / 2 - 0.035, 0.0):
        B(p, [x + dx, yc, front + 0.03], [0.06, h, 0.03], frame, lod=1)
    for fy in (1 / 3, 2 / 3):
        B(p, [x, y0 + h * fy, front + 0.03], [w, 0.04, 0.03], frame, lod=1)
    B(p, [x, y0 + h - 0.035, front + 0.03], [w, 0.06, 0.03], frame, lod=1)
    s = 0.14
    B(p, [x - w / 2 - s / 2, yc, front + 0.035], [s, h + 0.02, 0.07], surround)
    B(p, [x + w / 2 + s / 2, yc, front + 0.035], [s, h + 0.02, 0.07], surround)
    B(p, [x, y0 + h + 0.11, front + 0.045], [w + 2 * s + 0.08, 0.22, 0.09], surround)
    B(p, [x, y0 - 0.05, front + 0.09], [w + 0.34, 0.08, 0.18], "dressing")
    if shutter:
        for sx in (-1, 1):
            cx = x + sx * (w / 2 + s + w / 4 + 0.02)
            B(p, [cx, yc, front + 0.025], [w / 2, h, 0.035], shutter)
            for fy in (0.18, 0.82):   # hinge straps
                B(p, [cx, y0 + h * fy, front + 0.046], [w / 2 - 0.04, 0.035, 0.01], "iron", lod=1)
            B(p, [cx, yc, front + 0.045], [0.03, h - 0.1, 0.01], "frame" if shutter != "sh_cream" else "iron", lod=1)
    if balcony:
        bw, z = w + 0.5, front + 0.24
        B(p, [x, y0 - 0.02, front + 0.14], [bw, 0.06, 0.3], "dressing")
        B(p, [x, y0 + 0.92, z], [bw, 0.04, 0.04], "iron")
        B(p, [x, y0 + 0.1, z], [bw, 0.03, 0.03], "iron", lod=1)
        n = 9
        for k in range(n):
            B(p, [x - bw / 2 + 0.02 + k * (bw - 0.04) / (n - 1), y0 + 0.5, z], [0.018, 0.84, 0.018], "iron", lod=1)
        for sx in (-1, 1):
            B(p, [x + sx * (bw / 2 - 0.01), y0 + 0.5, front + 0.13], [0.02, 0.84, 0.24], "iron", lod=1)


def door(p, x, front, paint, surround, w=1.1, h=2.35, step=True):
    B(p, [x, h / 2, front + 0.015], [w, h, 0.03], paint)
    for py in (0.55, 1.45):   # raised panels
        for px in (-w / 4, w / 4):
            B(p, [x + px, py, front + 0.035], [w / 2 - 0.16, 0.7, 0.02], paint, lod=1)
    B(p, [x + w * 0.36, 1.05, front + 0.06], [0.05, 0.05, 0.05], "gilt", lod=1)
    B(p, [x, h + 0.25, front + 0.01], [w, 0.4, 0.02], "pane")
    B(p, [x, h + 0.04, front + 0.03], [w, 0.06, 0.03], paint, lod=1)
    B(p, [x, h + 0.25, front + 0.03], [0.04, 0.4, 0.03], paint, lod=1)
    s = 0.17
    for sx in (-1, 1):
        B(p, [x + sx * (w / 2 + s / 2), (h + 0.45) / 2, front + 0.04], [s, h + 0.45, 0.08], surround)
    B(p, [x, h + 0.56, front + 0.05], [w + 2 * s + 0.1, 0.24, 0.1], surround)
    B(p, [x, h + 0.56, front + 0.1], [0.22, 0.3, 0.06], surround, lod=1)   # keystone
    if step:
        B(p, [x, 0.08, front + 0.2], [w + 0.5, 0.16, 0.4], "plinth", bv=0.02)


def shopfront(p, x0, x1, front, paint, awning, sign=True):
    """A painted timber shop front: pilasters, a stall riser, a mullioned display window, a fascia and a striped awning."""
    w = x1 - x0
    xc = (x0 + x1) / 2
    for x in (x0 + 0.14, x1 - 0.14):
        B(p, [x, 1.5, front + 0.07], [0.28, 3.0, 0.14], paint)
        B(p, [x, 3.05, front + 0.1], [0.36, 0.14, 0.2], paint, lod=1)
    B(p, [xc, 2.82, front + 0.08], [w, 0.5, 0.16], paint)                       # fascia
    if sign:
        B(p, [xc, 2.82, front + 0.165], [w - 0.9, 0.3, 0.01], "gilt", lod=1)    # the gilt sign panel
        B(p, [xc, 2.82, front + 0.168], [w - 1.0, 0.22, 0.01], paint, lod=1)
    B(p, [xc, 3.11, front + 0.13], [w + 0.1, 0.08, 0.26], paint, lod=1)        # cornice
    dw = 1.0
    gx0, gx1 = x0 + 0.28, x1 - 0.28 - dw - 0.1
    B(p, [(gx0 + gx1) / 2, 0.3, front + 0.05], [gx1 - gx0, 0.6, 0.1], paint)   # stall riser
    B(p, [(gx0 + gx1) / 2, 1.5, front + 0.01], [gx1 - gx0, 1.8, 0.02], "pane")
    n = max(2, int((gx1 - gx0) / 0.8))
    for k in range(n + 1):
        B(p, [gx0 + k * (gx1 - gx0) / n, 1.5, front + 0.04], [0.06, 1.8, 0.05], paint, lod=1)
    B(p, [(gx0 + gx1) / 2, 2.43, front + 0.04], [gx1 - gx0, 0.06, 0.05], paint, lod=1)
    B(p, [(gx0 + gx1) / 2, 0.62, front + 0.06], [gx1 - gx0 + 0.04, 0.05, 0.12], paint, lod=1)
    dx = x1 - 0.28 - dw / 2 - 0.05
    B(p, [dx, 1.2, front + 0.015], [dw, 2.4, 0.03], paint)                      # glazed shop door
    B(p, [dx, 1.5, front + 0.035], [dw - 0.24, 1.2, 0.01], "pane")
    B(p, [dx, 0.08, front + 0.2], [dw + 0.3, 0.16, 0.4], "plinth", bv=0.02)
    if awning:
        a, b = awning
        depth, ang = 1.5, 22.0
        y = 2.45 - math.sin(math.radians(ang)) * depth / 2
        z = front + 0.2 + math.cos(math.radians(ang)) * depth / 2
        n = int(w / 0.45)
        for k in range(n):
            B(p, [x0 + 0.1 + (k + 0.5) * (w - 0.2) / n, y, z], [(w - 0.2) / n, 0.025, depth], a if k % 2 == 0 else b, r=[ang, 0, 0])
        zf = front + 0.2 + math.cos(math.radians(ang)) * depth
        yf = 2.45 - math.sin(math.radians(ang)) * depth
        for k in range(n):
            B(p, [x0 + 0.1 + (k + 0.5) * (w - 0.2) / n, yf - 0.12, zf], [(w - 0.2) / n, 0.24, 0.02], a if k % 2 == 0 else b, lod=1)
        for sx in (x0 + 0.12, x1 - 0.12):   # the iron arms
            B(p, [sx, (2.45 + yf) / 2, (front + 0.2 + zf) / 2], [0.025, 0.025, depth], "iron", r=[ang, 0, 0], lod=1)


def quoins(p, x, top, front, mat, sign):
    y, k = 0.6, 0
    while y < top - 0.1:
        w = 0.5 if k % 2 == 0 else 0.32
        B(p, [x - sign * w / 2, y + 0.15, front + 0.02], [w, 0.28, 0.04], mat, lod=1)
        y += 0.31
        k += 1


def timber(p, y0, y1, front, xs_open):
    """Colombage: oak posts at 0.55 m clear of the openings, a sill and a head rail, braces at the ends."""
    B(p, [0, y0 + 0.09, front + 0.03], [W, 0.18, 0.06], "oak")
    B(p, [0, y1 - 0.09, front + 0.03], [W, 0.18, 0.06], "oak")
    B(p, [0, y0 + 0.95, front + 0.025], [W, 0.12, 0.05], "oak", lod=1)
    x = -W / 2 + 0.09
    while x < W / 2 - 0.05:
        if not any(a - 0.12 < x < b + 0.12 for a, b in xs_open):
            B(p, [x, (y0 + y1) / 2, front + 0.025], [0.16, y1 - y0, 0.05], "oak_v")
        x += 0.55
    h = y1 - y0 - 0.36
    for sx in (-1, 1):
        L = math.hypot(0.9, h)
        B(p, [sx * (W / 2 - 0.62), (y0 + y1) / 2, front + 0.02], [0.13, L, 0.04], "oak_v", lod=1, r=[0, 0, sx * math.degrees(math.atan2(0.9, h))])


def house(aid, name, desc, body, floors, roof, shut, gf, upper="window", frame_timber=False, jetty=0.0, dormer=False, chimney=1,
          surround="dressing", quoin=None, balconies=False, door_paint="door_green", awning=None, boxes=False, clad=None):
    p = []
    eave = GF + FH * (floors - 1)
    # the collider body: the ground floor on the wall's face, the upper floors jettied forward
    B(p, [0, GF / 2, (FRONT + BACK) / 2], [W, GF, FRONT - BACK], body if not frame_timber else ("stone" if gf != "shop" else body), k=1)
    fu = FRONT + jetty
    B(p, [0, (GF + eave) / 2, (fu + BACK) / 2], [W, eave - GF, fu - BACK], clad or (body if not frame_timber else "plaster"), k=1)
    if jetty:
        for k in range(9):   # joist ends under the jetty
            B(p, [-W / 2 + 0.35 + k * (W - 0.7) / 8, GF - 0.08, FRONT + jetty / 2], [0.14, 0.16, jetty], "oak", lod=1)
        B(p, [0, GF + 0.1, fu + 0.04], [W, 0.2, 0.08], "oak")
    B(p, [0, 0.3, FRONT + 0.03], [W, 0.6, 0.06], "plinth")
    # ground floor
    if gf == "shop":
        shopfront(p, -2.85, 1.0, FRONT, door_paint, awning or ("canvas_red", "canvas_cream"))
        door(p, 2.0, FRONT, door_paint, surround, w=0.95)
        bracket_sign(p, -2.75, FRONT, door_paint, len(aid))
    elif gf == "cafe":
        shopfront(p, -2.85, 2.85, FRONT, "door_red", awning or ("canvas_green", "canvas_cream"))
        bracket_sign(p, 2.75, FRONT, "door_red", 1)
    elif gf == "carriage":   # a closed oak carriage door under a stone arch
        B(p, [-0.6, 1.45, FRONT + 0.02], [2.6, 2.9, 0.04], "oak")
        for k in range(8):
            B(p, [-1.8 + k * 0.343 + 0.17, 1.45, FRONT + 0.045], [0.04, 2.8, 0.02], "oak_v", lod=1)
        B(p, [-0.6, 1.45, FRONT + 0.055], [0.05, 2.9, 0.02], "iron", lod=1)
        for sx in (-1, 1):
            B(p, [-0.6 + sx * 1.42, 1.5, FRONT + 0.05], [0.24, 3.0, 0.1], surround)
        B(p, [-0.6, 3.05, FRONT + 0.06], [3.1, 0.3, 0.12], surround)
        B(p, [-0.6, 3.05, FRONT + 0.12], [0.3, 0.42, 0.06], surround, lod=1)
        window(p, 1.95, 1.0, 0.9, 1.6, FRONT, shut, surround)
    else:   # a door and a window
        door(p, 1.7, FRONT, door_paint, surround)
        window(p, -1.3, 1.0, 1.0, 1.65, FRONT, shut, surround)
    # upper floors
    for f in range(1, floors):
        y = GF + FH * (f - 1)
        h = 1.7 if f == 1 else 1.45
        ws = (-1.45, 1.45)
        if frame_timber:
            timber(p, y, y + FH, fu, [(x - 0.62, x + 0.62) for x in ws])
        else:
            B(p, [0, y + 0.07, fu + 0.035], [W, 0.14, 0.07], surround if body != "brick" else "dressing")   # string course
        for x in ws:
            window(p, x, y + 0.55, 0.95, h, fu, shut, "oak" if frame_timber else ("frame" if clad else surround), balcony=balconies and f == 1)
            if boxes and not (balconies and f == 1) and (f + int(x > 0) + len(aid)) % 2 == 0:
                flowers(p, x, y + 0.55, fu, 1.0, f + len(aid))
    # quoins, cornice, gutter, downpipe
    if quoin:
        quoins(p, -W / 2, eave, FRONT, quoin, -1)
        quoins(p, W / 2, eave, FRONT, quoin, 1)
    B(p, [0, eave - 0.14, fu + 0.12], [W + 0.04, 0.28, 0.24], "oak" if frame_timber else surround)
    B(p, [0, eave + 0.02, fu + 0.4], [0.14, W, 0.14], "zinc", r=[0, 0, 90], lod=1)
    dp = 2.86 if gf != "carriage" else -2.86
    B(p, [dp, eave / 2, FRONT + 0.1], [0.1, eave, 0.1], "zinc", lod=1)
    B(p, [dp, eave - 0.2, fu + 0.25], [0.2, 0.25, 0.25], "zinc", lod=1)
    # the roof: a wedge rising from the eaves to the ridge over the back wall; gable ends close it
    zf, zb = fu + 0.5, BACK
    rh = (zf - zb) * PITCH
    p.append({"t": "w", "c": [0, round(eave + rh / 2, 4), round((zf + zb) / 2, 4)], "s": [W + 0.08, round(rh, 4), round(zf - zb, 4)], "m": roof, "k": 0, "top": 0})
    B(p, [0, eave + rh + 0.06, zb + 0.12], [W + 0.08, 0.14, 0.26], roof, lod=1)   # ridge
    if dormer:
        d = 1.1
        yb = eave + d * PITCH - 0.4
        B(p, [0, yb + 0.85, zf - d - 0.55], [1.5, 1.7, 1.1], body if not frame_timber else "plaster")
        window(p, 0, yb + 0.45, 0.8, 1.0, zf - d, shut if not frame_timber else None, "oak" if frame_timber else surround)
        p.append({"t": "p", "axis": 0, "a": [-0.85, 0.85], "tri": [[zf - d + 0.1, yb + 1.7], [zf - d - 1.2, yb + 1.7], [zf - d - 0.55, yb + 2.5]], "m": roof, "k": 0})
    for c in range(chimney):
        cx = (2.35 if c == 0 else -2.35) * (1 if gf != "shop" else -1)
        top = eave + rh + 1.1
        B(p, [cx, (eave + top) / 2, zb + 0.45], [0.75, top - eave, 0.55], "brick" if body != "stone" else "stone")
        B(p, [cx, top + 0.05, zb + 0.45], [0.85, 0.1, 0.65], "dressing", lod=1)
        for px in (-0.17, 0.17):
            p.append({"t": "c", "c": [cx + px, top + 0.28, zb + 0.45], "s": [0.17, 0.36, 0.17], "m": "pot", "seg": 8, "k": 0, "lod": 1})
    return {"id": aid, "version": 1, "name": name, "category": "Modern: Military", "description": desc, "footprint": [W, FRONT - BACK],
            "floors": 0, "enterable": False, "plinthDepth": 0.4, "maxGrade": 3, "noAutoPanels": True, "materials": MATS, "parts": p,
            "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


VARIANTS = [
    ("sd_facade_render_a", "Town house, cream render", dict(body="render_cream", floors=4, roof="tile", shut="sh_blue", gf="door", quoin="dressing", balconies=True, door_paint="door_blue", boxes=True)),
    ("sd_facade_render_b", "Town house with a shop, ochre render", dict(body="render_ochre", floors=3, roof="slate", shut="sh_green", gf="shop", dormer=True, quoin="dressing", door_paint="door_blue", awning=("canvas_navy", "canvas_navy"), boxes=True)),
    ("sd_facade_timber_a", "Half-timbered town house", dict(body="plaster", floors=3, roof="tile", shut=None, gf="door", frame_timber=True, jetty=0.32, door_paint="door_red", boxes=True)),
    ("sd_facade_timber_b", "Half-timbered shop", dict(body="plaster", floors=3, roof="tile", shut=None, gf="shop", frame_timber=True, jetty=0.3, dormer=True, door_paint="door_red", awning=("canvas_wine", "canvas_wine"))),
    ("sd_facade_stone", "Limestone town house with a carriage door", dict(body="stone", floors=4, roof="slate", shut="sh_cream", gf="carriage", balconies=True, chimney=2)),
    ("sd_facade_brick", "Brick town house", dict(body="brick", floors=2, roof="slate", shut="sh_red", gf="door", dormer=True, quoin="dressing", door_paint="door_red")),
    ("sd_facade_cafe", "Cafe frontage", dict(body="render_white", floors=3, roof="tile", shut="sh_green", gf="cafe", dormer=True, quoin="dressing", boxes=True)),
    ("sd_facade_slate", "Slate-hung town house", dict(body="render_grey", floors=4, roof="slate", shut=None, gf="shop", dormer=True, clad="slate_hung", door_paint="door_green", awning=("canvas_navy", "canvas_cream"))),
    ("sd_facade_render_c", "Town house, grey render", dict(body="render_grey", floors=2, roof="slate", shut="sh_blue", gf="door", door_paint="door_blue")),
]


def monument():
    """The square's monument aux morts: a stepped limestone base on the old well's 1.9 m footprint (the same cover), a
    pedestal with bronze plaques, an obelisk with a gilt cross; only the base and pedestal collide."""
    p = []
    for k, (w, h) in enumerate(((1.9, 0.22), (1.6, 0.22), (1.3, 0.22))):
        B(p, [0, 0.11 + 0.22 * k, 0], [w, h, w], "dressing", k=1, bv=0.02)
    B(p, [0, 0.66 + 0.6, 0], [0.95, 1.2, 0.95], "stone", k=1)
    B(p, [0, 1.92, 0], [1.1, 0.14, 1.1], "dressing", bv=0.02)
    for r in range(4):
        a = math.radians(90 * r)
        B(p, [0.48 * math.sin(a), 1.25, 0.48 * math.cos(a)], [0.55 if r % 2 == 0 else 0.02, 0.6, 0.02 if r % 2 == 0 else 0.55], "zinc", lod=1)
    p.append({"t": "c", "c": [0, 3.55, 0], "s": [0.62, 3.1, 0.62], "m": "stone", "n": 4, "top": 0.55, "r": [0, 45, 0], "k": 0})
    B(p, [0, 5.25, 0], [0.07, 0.6, 0.07], "gilt", lod=1)
    B(p, [0, 5.35, 0], [0.32, 0.07, 0.07], "gilt", lod=1)
    return {"id": "sd_monument", "version": 1, "name": "Monument aux morts", "category": "Modern: Military",
            "description": "Search and Destroy (Rue Haute): the market square's war memorial, a limestone obelisk on a stepped base (1.9 m, the old well's footprint).",
            "footprint": [1.9, 1.9], "floors": 0, "enterable": False, "plinthDepth": 0.2, "maxGrade": 3, "noAutoPanels": True, "materials": MATS, "parts": p,
            "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


def market_lean_to():
    """The square's covered market against a divider wall (6 m bay, its origin on the wall's edge line, +z into the
    square): oak posts 1.7 m out at the bay ends, a beam and a pent tile roof from the wall top down to 2.7 m, rafters
    under it. Only the posts collide (a 1.5 m walk between them and the wall)."""
    p = []
    zp = 0.31 + 1.7
    for x in (-2.9, 2.9):
        B(p, [x, 1.35, zp], [0.2, 2.7, 0.2], "oak", k=1, bv=0.02)
        B(p, [x, 0.08, zp], [0.34, 0.16, 0.34], "plinth", bv=0.02)
        B(p, [x, 2.45, zp - 0.3], [0.1, 0.1, 0.7], "oak", r=[-40, 0, 0], lod=1)   # brace
    B(p, [0, 2.8, zp], [6.0, 0.22, 0.2], "oak")
    y0, y1, z0, z1 = 3.3, 2.75, 0.31, zp + 0.45
    import math as _m
    L, ang = _m.hypot(z1 - z0, y0 - y1), _m.degrees(_m.atan2(y0 - y1, z1 - z0))
    B(p, [0, (y0 + y1) / 2 + 0.1, (z0 + z1) / 2], [6.06, 0.06, L], "tile", r=[ang, 0, 0])
    for x in (-2.25, -0.75, 0.75, 2.25):
        B(p, [x, (y0 + y1) / 2 + 0.02, (z0 + z1) / 2], [0.1, 0.1, L], "oak", r=[ang, 0, 0], lod=1)
    B(p, [0, 3.35, 0.36], [6.0, 0.12, 0.12], "oak", lod=1)   # the wall plate
    return {"id": "sd_market_lean_to", "version": 1, "name": "Covered market bay", "category": "Modern: Military",
            "description": "Search and Destroy (Rue Haute): a 6 m covered market bay against a wall of the square: oak posts and a pent tile roof.",
            "footprint": [6.0, 2.4], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "materials": MATS, "parts": p,
            "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


def washing_line():
    """A washing line across a 6 m lane between two house fronts (5.3 m, its origin on the line): an iron bracket at each
    end, linen, shirts and a blanket pegged along it, sagging a little. Nothing collides."""
    import random
    R = random.Random(41)
    p = []
    L = 5.36
    for sx in (-1, 1):
        B(p, [sx * (L / 2 - 0.12), 0.05, 0], [0.24, 0.04, 0.04], "iron")
        B(p, [sx * (L / 2 - 0.02), -0.15, 0], [0.04, 0.4, 0.04], "iron", lod=1)
    for k in range(6):   # the sagging line in six straight pieces
        x0, x1 = -L / 2 + 0.2 + k * (L - 0.4) / 6, -L / 2 + 0.2 + (k + 1) * (L - 0.4) / 6
        sag = lambda x: -0.28 * (1 - (2 * x / (L - 0.4)) ** 2)
        y0, y1 = sag(x0), sag(x1)
        B(p, [(x0 + x1) / 2, (y0 + y1) / 2, 0], [math.hypot(x1 - x0, y1 - y0), 0.012, 0.012], "iron", r=[0, 0, math.degrees(math.atan2(y1 - y0, x1 - x0))])
    x = -L / 2 + 0.45
    cols = ["canvas_cream", "plaster", "sh_blue", "canvas_red", "canvas_cream", "sh_green", "render_white", "canvas_cream"]
    while x < L / 2 - 0.6:
        w, h = R.uniform(0.35, 0.85), R.uniform(0.45, 0.95)
        y = -0.28 * (1 - (2 * (x + w / 2) / (L - 0.4)) ** 2)
        B(p, [x + w / 2, y - h / 2, 0], [w, h, 0.012], R.choice(cols))
        x += w + R.uniform(0.12, 0.4)
    return {"id": "sd_washing_line", "version": 1, "name": "Washing line", "category": "Modern: Military",
            "description": "Search and Destroy (Rue Haute): a washing line across a narrow lane, hung with linen (no collision).",
            "footprint": [5.4, 0.1], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "materials": MATS, "parts": p,
            "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


def main():
    n = 0
    out = [house(aid, name, "Search and Destroy (Rue Haute): a 6 m Norman town-house frontage on a lane edge, not enterable; " + name.lower() + ".", **kw)
           for aid, name, kw in VARIANTS] + [monument(), washing_line(), market_lean_to()]
    for a in out:
        with open(os.path.join(ROOT, "content", "assets", a["id"] + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(a, f, separators=(",", ":"))
        n += 1
        print(a["id"], len(a["parts"]), "parts")
    print("sd facades written:", n)


if __name__ == "__main__":
    main()
