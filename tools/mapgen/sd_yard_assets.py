"""SD2 Night Yard: the container terminal's dressing (content/assets/sd_box_*, sd_mark_*, sd_flood_mast, ...).

* sd_box_<colour>: the 20 ft container in other liveries (copies of mod_container_20, its weed parts dropped);
* ground markings: painted lane lines, hazard stripes, numbered container bays, slab joints, crane rails, drain grates.
  Thin flat geometry (12 mm, k = 0): read as paint, never a step on the navmesh;
* sd_flood_mast: a 14 m floodlight mast, two sodium-white floods aimed out over the lane with visible shafts;
* sd_work_light: a clamp-on work light for a container top; sd_beacon_red: a crane's red obstruction light.

    <python> -I tools/mapgen/sd_yard_assets.py"""
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ADIR = os.path.join(ROOT, "content", "assets")
DIG = {"0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."], "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
       "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"], "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
       "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."], "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
       "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."], "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
       "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."], "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."]}
LIVERY = {"blue": ([0.12, 0.24, 0.44], "corrugated_blue_rusty"), "green": ([0.14, 0.3, 0.2], "corrugated_grey"), "grey": ([0.42, 0.43, 0.44], "corrugated_grey"),
          "white": ([0.72, 0.72, 0.68], "corrugated_grey"), "yellow": ([0.62, 0.46, 0.08], "corrugated_grey"), "maroon": ([0.3, 0.08, 0.07], "corrugated_grey"),
          "orange": ([0.62, 0.26, 0.06], "corrugated_grey")}


def M(name, albedo, pattern="painted", rough=0.7, metal=0.0, tex=None, emis=None):
    m = {"name": name, "albedo": albedo, "roughness": rough, "pattern": pattern, "metallic": metal, "grain": 1}
    if tex:
        m["texture"] = tex
    if emis:
        m["emissive"] = emis
    return m


MATS = [M("yellow", [0.78, 0.6, 0.08], tex="paint_metal_worn"), M("white", [0.8, 0.8, 0.76], tex="paint_metal_worn"), M("black", [0.03, 0.03, 0.03]),
        M("joint", [0.05, 0.05, 0.05], "plain", 0.9), M("rail", [0.42, 0.42, 0.43], "metal", 0.35, 0.8, "steel_scratched"),
        M("galv", [0.55, 0.57, 0.58], "metal", 0.5, 0.6, "steel_scratched"), M("head", [0.18, 0.19, 0.2], "metal", 0.6, 0.4),
        M("lens", [1.0, 0.92, 0.78], "plain", 0.3, emis=3.0), M("red_lens", [1.0, 0.08, 0.04], "plain", 0.3, emis=4.0), M("grate", [0.08, 0.08, 0.085], "metal", 0.6, 0.5), M("wet", [0.025, 0.026, 0.03], "plain", 0.03), M("pallet_wood", [0.55, 0.42, 0.28], "wood", 0.8), M("lit", [1.0, 0.82, 0.55], "plain", 0.4, emis=1.6), M("lit_cool", [0.75, 0.85, 1.0], "plain", 0.4, emis=1.3)]


def B(p, c, s, m, k=0, lod=0, r=None):
    q = {"t": "b", "c": [round(v, 4) for v in c], "s": [round(v, 4) for v in s], "m": m, "k": k}
    if lod:
        q["lod"] = 1
    if r:
        q["r"] = r
    p.append(q)


def asset(aid, name, desc, fp, parts, light=None):
    a = {"id": aid, "version": 1, "name": name, "category": "Modern: Military", "description": "Search and Destroy (Night Yard): " + desc, "footprint": fp,
         "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "materials": MATS, "parts": parts,
         "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": [], "areas": []},
         "furniture": [], "interactables": [], "panels": []}
    if light:
        a["light"] = light
    return a


Y = 0.006   # paint sits 6 mm proud of the asphalt


def number(p, txt, cx, cz, px=0.2):
    """Stencil digits flat on the ground, reading from -z (the lane side): px per pixel."""
    w = len(txt) * 6 * px
    for n, ch in enumerate(txt):
        ox = cx - w / 2 + n * 6 * px + 2.5 * px
        for r, row in enumerate(DIG[ch]):
            for c, v in enumerate(row):
                if v == "#":
                    B(p, [ox + (c - 2) * px, Y, cz + (3 - r) * px], [px * 0.9, 0.012, px * 0.9], "white")


def main():
    out = []
    # liveries
    base = json.load(open(os.path.join(ADIR, "mod_container_20.json"), encoding="utf-8"))
    for col, (alb, tex) in LIVERY.items():
        a = json.loads(json.dumps(base))
        a["id"], a["name"] = "sd_box_" + col, "Shipping container, 20 ft (%s)" % col
        a["description"] = "Search and Destroy (Night Yard): the 20 ft container in a %s livery." % col
        for m in a["materials"]:
            if m["name"] == "c_red":
                m["albedo"], m["texture"] = alb, tex
        a["parts"] = [q for q in a["parts"] if q.get("m") not in ("weeds", "leaves")]
        out.append(a)
    # markings (each 6 m long along local x, centred)
    p = []
    B(p, [0, Y, 0], [6.0, 0.012, 0.15], "yellow")
    out.append(asset("sd_mark_line", "Lane line (yellow)", "a 6 m solid yellow lane line, painted.", [6.0, 0.15], p))
    p = []
    for k in range(3):
        B(p, [-2.0 + k * 2.0, Y, 0], [1.2, 0.012, 0.15], "white")
    out.append(asset("sd_mark_dash", "Lane line (white, dashed)", "a 6 m dashed white centre line, painted.", [6.0, 0.15], p))
    p = []
    for k in range(10):   # hazard stripes: diagonal yellow and black bars in a 0.5 m band
        B(p, [-2.7 + k * 0.6, Y, 0], [0.3, 0.012, 0.62], "yellow" if k % 2 == 0 else "black", r=[0, 45, 0])
    B(p, [0, Y - 0.001, 0], [6.0, 0.012, 0.5], "black")
    for sz in (-1, 1):
        B(p, [0, Y + 0.001, sz * 0.27], [6.0, 0.012, 0.06], "yellow")
    out.append(asset("sd_mark_hazard", "Hazard stripes", "a 6 m band of yellow and black hazard stripes, painted.", [6.0, 0.6], p))
    for n in range(1, 13):
        p = []
        for sx in (-1, 1):
            B(p, [sx * 1.35, Y, 0], [0.1, 0.012, 6.3], "white")
        B(p, [0, Y, -3.1], [2.8, 0.012, 0.1], "white")
        number(p, "%02d" % n, 0.0, -1.6, 0.13)
        out.append(asset("sd_mark_bay_%02d" % n, "Container bay %02d" % n, "a numbered 20 ft container bay outline (2.7 x 6.3 m), painted.", [2.8, 6.3], p))
    p = []
    for v in (-1.5, 1.5):
        B(p, [v, Y, 0], [0.03, 0.012, 6.0], "joint")
        B(p, [0, Y, v], [6.0, 0.012, 0.03], "joint")
    for v in (-3.0, 3.0):
        B(p, [v, Y, 0], [0.04, 0.012, 6.0], "joint")
        B(p, [0, Y, v], [6.0, 0.012, 0.04], "joint")
    out.append(asset("sd_slab_joints", "Concrete slab joints (6 m)", "the sawn joints of a 6 m concrete apron, four slabs a side.", [6.0, 6.0], p))
    p = []
    for sz in (-0.06, 0.06):
        B(p, [0, 0.008, sz], [6.0, 0.016, 0.06], "rail")
    B(p, [0, Y, 0], [6.0, 0.012, 0.42], "joint")
    out.append(asset("sd_crane_rail", "Crane rail (6 m)", "a flush crane rail set in the yard surface.", [6.0, 0.42], p))
    p = []
    B(p, [0, Y, 0], [0.62, 0.012, 0.62], "grate")
    for k in range(7):
        B(p, [-0.27 + k * 0.09, Y + 0.004, 0], [0.03, 0.012, 0.56], "rail", lod=1)
    out.append(asset("sd_drain", "Drain grate", "a steel drain grate in the yard surface.", [0.62, 0.62], p))
    # reflective puddles: the stock puddle cluster's water surfaces lifted 8 mm onto the asphalt, without its terrain cut
    # (the cut lowered the ground under the walls next to it)
    w = json.load(open(os.path.join(ADIR, "wat_puddles.json"), encoding="utf-8"))
    w["id"], w["name"] = "sd_puddle_yard", "Puddles (on asphalt)"
    w["description"] = "Search and Destroy (Night Yard): five rain puddles lying on the asphalt (no terrain cut), catching the floodlights."
    w.pop("terrainCut", None)
    for srf in w["water"]["surfaces"]:
        srf["level"] = 0.008
    for q in w["parts"]:
        if q.get("t") == "p":
            q["a"] = [0.0, 0.008]
    out.append(w)
    # a counterbalance forklift (2.4 t), forks to +z, parked with a pallet on the forks; the chassis collides
    p = []
    B(p, [0, 0.75, -0.2], [1.15, 0.9, 1.9], "yellow", k=1)          # body
    B(p, [0, 0.85, -1.25], [1.15, 1.0, 0.4], "head", k=1)           # counterweight
    B(p, [0, 1.25, -0.45], [0.6, 0.12, 0.5], "black")               # seat
    for sx in (-0.52, 0.52):
        for sz in (-0.85, 0.55):
            p.append({"t": "c", "c": [sx, 0.33, sz], "s": [0.66, 0.22, 0.66], "m": "black", "seg": 14, "r": [0, 0, 90], "k": 1})
        B(p, [sx, 1.55, -0.25], [0.07, 1.4, 0.07], "black", lod=1)  # overhead guard posts
        B(p, [sx * 0.6, 1.5, 0.8], [0.1, 2.9, 0.12], "head", k=1)   # mast
    B(p, [0, 2.27, -0.25], [1.1, 0.06, 1.2], "black", lod=1)        # guard roof
    B(p, [0, 0.55, 0.9], [0.95, 0.5, 0.06], "head")                 # carriage
    for sx in (-0.32, 0.32):
        B(p, [sx, 0.12, 1.5], [0.12, 0.05, 1.15], "head", k=1)     # forks
    B(p, [0, 0.25, 1.55], [1.1, 0.14, 1.1], "pallet_wood", k=1)
    B(p, [0, 0.62, 1.55], [1.0, 0.6, 1.0], "white", k=1)            # a wrapped load
    out.append(asset("sd_forklift", "Forklift", "a yellow counterbalance forklift with a wrapped pallet on its forks.", [1.2, 3.2], p))
    # the floodlight mast: +z faces the lane; two floods aimed down and out
    p = []
    p.append({"t": "c", "c": [0, 7.0, 0], "s": [0.36, 14.0, 0.36], "m": "galv", "seg": 10, "top": 0.55, "k": 1})
    B(p, [0, 0.03, 0], [0.9, 0.06, 0.9], "galv", k=1)
    B(p, [0, 13.7, 0.2], [2.4, 0.12, 0.12], "galv")
    B(p, [0, 14.1, -0.05], [0.12, 0.9, 0.12], "galv", lod=1)
    for sx in (-0.75, 0.75):
        B(p, [sx, 13.55, 0.42], [0.62, 0.48, 0.3], "head", r=[-35, 0, 0])
        B(p, [sx, 13.42, 0.57], [0.52, 0.38, 0.02], "lens", r=[-35, 0, 0])
    flood = [{"offset": [sx, 13.4, 0.6], "color": [1.0, 0.72, 0.42], "intensity": 140.0, "radius": 40.0, "cone": [22, 40], "dir": [0, -0.62, 0.78],
              "group": 2, "use": "street", "shadow": 1 if sx < 0 else 0, "fx": "shaft", "fxSize": 22.0} for sx in (-0.75, 0.75)]
    out.append(asset("sd_flood_mast", "Floodlight mast (14 m)", "a 14 m galvanised floodlight mast with two floods aimed over the lane.", [0.9, 0.9], p, flood))
    # a container-top work light (its base on the box roof, +z over the lane edge)
    p = []
    B(p, [0, 0.04, 0], [0.3, 0.08, 0.3], "head")
    B(p, [0, 0.35, 0.05], [0.05, 0.6, 0.05], "head")
    B(p, [0, 0.68, 0.18], [0.36, 0.26, 0.18], "yellow", r=[-30, 0, 0])
    B(p, [0, 0.63, 0.28], [0.3, 0.2, 0.02], "lens", r=[-30, 0, 0])
    out.append(asset("sd_work_light", "Work light", "a clamp-on work light on a container roof, aimed down into the lane.", [0.4, 0.4], p,
                     {"offset": [0, 0.62, 0.32], "color": [1.0, 0.95, 0.85], "intensity": 30.0, "radius": 16.0, "cone": [35, 70], "dir": [0, -0.7, 0.7], "group": 2, "use": "street"}))
    p = []
    B(p, [0, 0.05, 0], [0.22, 0.1, 0.22], "head")
    p.append({"t": "c", "c": [0, 0.2, 0], "s": [0.18, 0.2, 0.18], "m": "red_lens", "seg": 10, "k": 0})
    out.append(asset("sd_beacon_red", "Red obstruction light", "a red obstruction beacon on a crane or gantry top.", [0.25, 0.25], p,
                     {"offset": [0, 0.22, 0], "color": [1.0, 0.1, 0.05], "intensity": 3.0, "radius": 9.0, "group": 2, "use": "street"}))
    for a in out:
        with open(os.path.join(ADIR, a["id"] + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(a, f, separators=(",", ":"))
    print("sd yard assets written:", len(out))


if __name__ == "__main__":
    main()
