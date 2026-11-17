"""SD2 Sun Fort art pass 2: the warm desert kit (content/assets/sd_*.json).

  sd_mud_wall_a/b/c   6 m bay of an ochre-rendered mud-brick yard wall, 3.1 m: a mud-brick plinth, a dark stain band at the
                      foot, a rounded parapet, a buttress at one end, patches of chipped render showing the brick, a drain
                      spout on some; an invisible collider to 3.9 m (not climbable, the layout's timings hold)
  sd_mud_wall_door    the bay with a 1.8 m doorway under a timber lintel, a steel door leaf standing open
  sd_compound_wall_warm, sd_compound_wall_warm_plain   the precast perimeter wall (and its inner bay) in sun-bleached tan
  sd_shade_canopy     a 6 x 4 m corrugated shade roof on four steel posts, lightly sloped (walk under)
  sd_stencil_a / _b   site letters stencil-painted on a wall face: 1.4 m strokes with stencil bridges, orange / teal

    <python> -I tools/mapgen/sd_desert_assets.py"""
import json
import math
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
A = os.path.join(ROOT, "content", "assets")


def mat(name, albedo, rough=0.85, pattern="plain", metal=0.0, tex=None):
    m = {"name": name, "albedo": albedo, "roughness": rough, "pattern": pattern, "metallic": metal, "grain": 1}
    if tex:
        m["texture"] = tex
    return m


def box(c, s, m, **kw):
    p = {"t": "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m}
    p.update(kw)
    return p


def asset(aid, name, desc, fp, parts, mats, cat="Modern: Military", nav="carve"):
    return {"id": aid, "version": 1, "name": name, "category": cat, "description": desc, "footprint": fp, "floors": 0, "enterable": False,
            "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "materials": mats, "parts": parts,
            "nav": {"effect": nav, "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
            "furniture": [], "interactables": [], "panels": []}


WALL_MATS = [mat("render", [0.84, 0.69, 0.5], 0.9, "plaster", tex="sd_render_aged"),
             mat("render_b", [0.78, 0.63, 0.45], 0.9, "plaster", tex="sd_render_aged"),
             mat("mudbrick", [0.66, 0.5, 0.36], 0.92, "brick", tex="sandstone"),
             mat("stain", [0.42, 0.34, 0.26], 0.95, "plaster", tex="sd_plaster_flaking"),
             mat("timber", [0.36, 0.26, 0.17], 0.8, "wood"),
             mat("steel", [0.33, 0.36, 0.32], 0.55, "metal", 0.5, tex="paint_metal_worn"),
             mat("air", [0.5, 0.5, 0.5], 1.0)]


def wall_body(x0, x1, top=3.1, seed=0):
    L, cx = x1 - x0, (x0 + x1) / 2
    p = [box([cx, 0.25, 0], [L, 0.5, 0.62], "mudbrick", bv=0.02),                       # plinth
         box([cx, (0.5 + top) / 2, 0], [L, top - 0.5, 0.48], "render" if seed % 2 == 0 else "render_b"),
         box([cx, 0.62, 0], [L + 0.01, 0.26, 0.5], "stain", k=0, lod=1),                 # damp / dust band at the foot
         box([cx, top + 0.07, 0], [L + 0.04, 0.14, 0.56], "render", bv=0.06)]           # rounded parapet
    # chipped render: the mud brick showing in a few patches on both faces (deterministic per seed)
    for k in range(3):
        u = ((seed * 7 + k * 13) % 10) / 10.0
        w = 0.5 + ((seed + k) % 3) * 0.3
        px = x0 + 0.4 + u * (L - 0.8 - w)
        py = 1.0 + ((seed * 3 + k * 5) % 7) * 0.25
        side = 1 if (seed + k) % 2 else -1
        if px + w < x1 - 0.2:
            p.append(box([px + w / 2, py, side * 0.243], [w, 0.35 + 0.1 * (k % 2), 0.02], "mudbrick", k=0, lod=1))
    return p


def mud_walls():
    out = []
    for v, seed in (("a", 1), ("b", 2), ("c", 3)):
        p = wall_body(-3.0, 3.0, seed=seed)
        bx = 2.7 if seed != 2 else -2.7                         # a buttress on both faces at one end
        p += [box([bx, 1.5, s * 0.36], [0.6, 3.0, 0.26], "render_b", bv=0.03) for s in (-1, 1)]
        if seed == 3:   # a drain spout through the parapet
            p.append(box([0.6, 2.95, 0.42], [0.14, 0.12, 0.5], "timber", k=0))
        p.append(box([0, 3.6, 0], [6.0, 0.8, 0.5], "air", v=0))
        out.append(asset("sd_mud_wall_" + v, "Mud-brick yard wall bay %s (6 m)" % v.upper(),
                         "Search and Destroy (Sun Fort): a 6 m bay of ochre-rendered mud-brick wall, 3.1 m, with a buttress, a dust-stained foot and chipped render. Not climbable.",
                         [6.0, 0.8], p, WALL_MATS))
    p = wall_body(-3.0, -0.9, seed=4) + wall_body(0.9, 3.0, seed=5)
    p += [box([0, 2.65, 0], [1.8, 0.9, 0.48], "render"), box([0, 2.27, 0], [2.3, 0.16, 0.56], "timber"), box([0, 3.17, 0], [1.84, 0.14, 0.56], "render", bv=0.06),
          box([1.0, 1.05, 0.85], [0.06, 2.1, 0.9], "steel"), box([0, 3.6, 0], [6.0, 0.8, 0.5], "air", v=0)]
    out.append(asset("sd_mud_wall_door", "Mud-brick wall bay with a doorway",
                     "Search and Destroy (Sun Fort): the 6 m mud-brick bay with a 1.8 m doorway under a timber lintel (2.2 m clear), its steel door leaf standing open.",
                     [6.0, 1.8], p, WALL_MATS))
    return out


def warm_copy(src, dst, name):
    a = json.load(open(os.path.join(A, src + ".json"), encoding="utf-8"))
    for m in a["materials"]:
        if m["name"].startswith(("precast", "conc")):
            l = sum(m["albedo"]) / 3.0
            m["albedo"] = [round(min(1.0, l * 1.22), 3), round(l * 1.04, 3), round(l * 0.8, 3)]   # sun-bleached tan
    a["id"], a["name"] = dst, name
    a["description"] = a["description"] + " Sun-bleached tan (Search and Destroy, Sun Fort)."
    return a


WARM_MASSES = ["mod_nt_barracks", "mod_nt_motor_pool", "mod_nt_hq", "mod_nt_workshop", "mod_nt_armory", "mod_nt_pump_station", "mod_mil_hut", "mod_nt_site_office"]


def warm_mass(src):
    """A building in the desert palette: its pale, unsaturated materials (render, concrete, panels, block) turned sand /
    tan / ochre by lightness; glass, metal, wood and dark trim kept."""
    a = json.load(open(os.path.join(A, src + ".json"), encoding="utf-8"))
    for k, m in enumerate(a["materials"]):
        r, g, b_ = m["albedo"][:3]
        l = (r + g + b_) / 3.0
        sat = max(r, g, b_) - min(r, g, b_)
        if m.get("metallic", 0) > 0.3 or "glass" in m["name"] or "window" in m["name"] or sat > 0.12 or l < 0.22:
            continue
        hue = ([1.24, 1.03, 0.78], [1.2, 0.98, 0.72], [1.16, 1.0, 0.8])[k % 3]
        m["albedo"] = [round(min(1.0, l * hue[0]), 3), round(min(1.0, l * hue[1]), 3), round(min(1.0, l * hue[2]), 3)] + m["albedo"][3:]
    a["id"] = "sd_warm_" + src[4:]
    a["name"] = a["name"] + " (desert)"
    a["description"] = a["description"] + " Desert palette (Search and Destroy, Sun Fort)."
    a["category"] = "Modern: Military"
    return a


def canopy():
    p = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            h = 2.85 if sz > 0 else 2.6
            p.append({"t": "c", "c": [sx * 2.85, h / 2, sz * 1.85], "s": [0.1, h, 0.1], "m": "steel", "seg": 8})
    for sz, h in ((-1.85, 2.6), (1.85, 2.85)):
        p.append(box([0, h + 0.05, sz], [5.9, 0.1, 0.1], "steel"))
    for k in range(9):   # corrugated sheets as ribs on a thin deck, sloping 0.25 m across
        z = -2.0 + k * 0.5
        p.append(box([0, 2.7 + 0.25 * (z + 2.0) / 4.0 + 0.05, z], [6.2, 0.03, 0.52], "sheet", r=[3.6, 0, 0]))
        p.append(box([0, 2.73 + 0.25 * (z + 2.0) / 4.0 + 0.05, z], [6.2, 0.03, 0.08], "sheet", k=0, lod=1, r=[3.6, 0, 0]))
    mats = [mat("steel", [0.4, 0.42, 0.4], 0.55, "metal", 0.5, tex="paint_metal_worn"), mat("sheet", [0.58, 0.55, 0.5], 0.6, "metal", 0.6, tex="steel_scratched")]
    return asset("sd_shade_canopy", "Shade canopy (6 x 4 m)", "Search and Destroy (Sun Fort): a corrugated shade roof on four steel posts, 2.6-2.85 m, over a vehicle bay.",
                 [6.2, 4.2], p, mats, nav="none")


def stencil(letter, colour):
    """Stroke letters painted on a wall: the asset's back at z = 0, the paint 6 mm proud, facing +z; 1.4 m tall, centred 1.9 m up."""
    w, H, cy, z = 0.16, 1.4, 1.9, 0.006
    p = []
    s = lambda c, sz, r=0.0: p.append(box([c[0], c[1], z], [sz[0], sz[1], 0.012], "paint", k=0, lod=1, r=[0, 0, r]))
    if letter == "A":
        ang = math.degrees(math.atan2(0.42, H))
        for sx in (-1, 1):   # two legs, split by a stencil bridge
            for seg, (y0, y1) in enumerate(((0.0, 0.62), (0.7, H))):
                ym = cy - H / 2 + (y0 + y1) / 2
                xm = sx * 0.42 * (1 - (y0 + y1) / 2 / H)
                s([xm, ym], [w, (y1 - y0) / math.cos(math.radians(ang))], sx * ang)
        s([0.0, cy - H / 2 + 0.5], [0.42, w])                       # crossbar (bridged from the legs)
    else:   # B: a stem and two bowls of straight strokes with chamfered corners, stencil bridges in the stem
        x0 = -0.36
        for y0, y1 in ((0.0, 0.62), (0.7, H)):
            s([x0, cy - H / 2 + (y0 + y1) / 2], [w, y1 - y0])
        for y in (0.0, 0.66, H):                                    # bottom, middle, top bars
            s([x0 + 0.27, cy - H / 2 + y + (w / 2 if y == 0.0 else -w / 2 if y == H else 0.0)], [0.38, w])
        for ya, yb, xr in ((0.08, 0.58, 0.36), (0.74, 1.32, 0.3)):   # the bowls' right sides
            s([xr, cy - H / 2 + (ya + yb) / 2], [w, yb - ya - 0.16])
        for yc, xr in ((0.08, 0.36), (0.58, 0.36), (0.74, 0.3), (1.32, 0.3)):   # chamfers
            s([xr - 0.07, cy - H / 2 + yc], [0.2, w], 45.0 if yc in (0.58, 1.32) else -45.0)
    return asset("sd_stencil_%s" % letter.lower(), "Site %s stencil (wall)" % letter,
                 "Search and Destroy: a 1.4 m site letter %s stencil-painted on a wall face (back at the origin, facing +z)." % letter,
                 [1.2, 0.05], p, [mat("paint", colour, 0.7, "plaster", tex="sd_render_stained")], nav="none")


def main():
    out = mud_walls()
    out.append(warm_copy("mod_nt_compound_wall", "sd_compound_wall_warm", "Compound wall bay, tan (6 m)"))
    out.append(warm_copy("mod_nt_compound_wall_plain", "sd_compound_wall_warm_plain", "Compound wall bay, inner, tan (6 m)"))
    out.append(warm_copy("mod_nt_compound_wall_breach", "sd_compound_wall_warm_breach", "Compound wall bay, breached, tan"))
    out.append(canopy())
    out += [warm_mass(m) for m in WARM_MASSES]
    out.append(stencil("A", [0.93, 0.47, 0.09]))
    out.append(stencil("B", [0.05, 0.6, 0.62]))
    for a in out:
        with open(os.path.join(A, a["id"] + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(a, f, separators=(",", ":"))
    print("sd desert assets written:", len(out))


if __name__ == "__main__":
    main()
