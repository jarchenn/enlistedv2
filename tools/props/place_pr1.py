"""PR1: place the new modelled props on Newtown and the outposts without regenerating the maps (the committed templates
have been re-seated since generation, so a regeneration would not reproduce them).

Candidates come from the props already on the map: hydrants beside street furniture on the downtown pavements,
payphones beside bus shelters and mail boxes, traffic barrels at roadblocks, barrier lines and checkpoints, the old
slide and swing set in the park. A candidate is kept only when its footprint (plus a margin) overlaps no other piece's
footprint. The chosen placements are written to tools/mapgen/maps/pr1_props.json (the map generators append them on a
future regeneration) and appended to content/templates/<map>.nmap.json, seated on the template height map; run
--find-floaters and tools/mapgen/seat_from_floaters.py afterwards for the exact runtime ground.

    python -I tools/props/place_pr1.py [--write]
"""
import json, math, os, random, sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WRITE = "--write" in sys.argv
_assets = {}


def asset(aid):
    if aid not in _assets:
        try:
            with open(os.path.join(REPO, "content", "assets", aid + ".json"), encoding="utf-8") as f:
                _assets[aid] = json.load(f)
        except OSError:
            _assets[aid] = None
    return _assets[aid]


def obb(x, z, yaw, w, d):
    a = math.radians(yaw); c, s = math.cos(a), math.sin(a)
    ax = (c, -s); az = (s, c)   # the asset's local x and z in world xz (yaw about +y)
    return (x, z, ax, az, w / 2, d / 2)


def overlap(p, q):
    for ax in (p[2], p[3], q[2], q[3]):
        def r(o): return o[4] * abs(o[2][0] * ax[0] + o[2][1] * ax[1]) + o[5] * abs(o[3][0] * ax[0] + o[3][1] * ax[1])
        dist = abs((p[0] - q[0]) * ax[0] + (p[1] - q[1]) * ax[1])
        if dist > r(p) + r(q): return False
    return True


SKIP = ("parking", "plaza", "square", "puddle", "crater", "patch", "tufts", "brush", "glass_pile", "papers", "weeds", "ivy", "leaf", "decal")


def blockers(doc):
    out = []
    for i in doc["instances"]:
        if "line" in i or not isinstance(i.get("asset"), dict): continue
        aid = i["asset"]["id"]; a = asset(aid)
        if not a or any(k in aid for k in SKIP): continue
        if a.get("category", "") in ("Vegetation", "Water", "Terrain"): continue
        fp = a.get("footprint") or [1, 1]
        out.append(obb(i["position"][0], i["position"][2], i["yaw"], fp[0], fp[1]))
    return out


def height_fn(tid):
    from PIL import Image
    with open(os.path.join(REPO, "content", "templates", tid + ".json"), encoding="utf-8") as f:
        hm = json.load(f)["heightmap"]
    im = Image.open(os.path.join(REPO, "content", "templates", hm["file"]))
    mx = 65535.0 if im.mode.startswith("I") else 255.0
    px = im.load(); nx, nz = hm["nx"], hm["nz"]
    def h(x, z):
        u = (x - hm["org"][0]) / hm["cell"]; v = (z - hm["org"][1]) / hm["cell"]
        i0, j0 = max(0, min(nx - 2, int(u))), max(0, min(nz - 2, int(v))); fu, fv = min(1, max(0, u - i0)), min(1, max(0, v - j0))
        def g(i, j):
            p = px[i, j]; p = p[0] if isinstance(p, tuple) else p
            return hm["min"] + (hm["max"] - hm["min"]) * p / mx
        return (g(i0, j0) * (1 - fu) + g(i0 + 1, j0) * fu) * (1 - fv) + (g(i0, j0 + 1) * (1 - fu) + g(i0 + 1, j0 + 1) * fu) * fv
    return h


def plan(tid, doc, rng):
    blk = blockers(doc); placed = []
    def anchors(*ids, region=None):
        out = []
        for i in doc["instances"]:
            if "line" in i or not isinstance(i.get("asset"), dict) or i["asset"]["id"] not in ids: continue
            x, z = i["position"][0], i["position"][2]
            if region and not region(x, z): continue
            out.append((x, z, i["yaw"], i["asset"]["id"]))
        out.sort(key=lambda a: (round(a[0], 1), round(a[1], 1)))
        return out
    def free(aid, x, z, yaw, margin=0.35):
        fp = asset(aid)["footprint"]; o = obb(x, z, yaw, fp[0] + margin, fp[1] + margin)
        return not any(overlap(o, b) for b in blk)
    def put(aid, x, z, yaw, tags):
        fp = asset(aid)["footprint"]; blk.append(obb(x, z, yaw, fp[0], fp[1]))
        placed.append({"asset": aid, "x": round(x, 2), "z": round(z, 2), "yaw": round(yaw, 1), "tags": tags})
    def beside(a, dx, dz):
        r = math.radians(a[2]); c, s = math.cos(r), math.sin(r)
        return a[0] + c * dx + s * dz, a[1] - s * dx + c * dz
    def far(aid, x, z, d):
        return all((p["x"] - x) ** 2 + (p["z"] - z) ** 2 > d * d for p in placed if p["asset"] == aid)
    if tid == "newtown":
        downtown = lambda x, z: abs(x) < 95 and abs(z) < 105
        for a in anchors("mod_trash_bin", "mod_bench", "mod_bike_rack", "mod_parking_meter", "mod_mailbox", "mod_bus_shelter", region=downtown):
            for dx in (2.2, -2.2, 3.0, -3.0):
                x, z = beside(a, dx, 0)
                if far("mod_hydrant_cast", x, z, 22) and free("mod_hydrant_cast", x, z, a[2]): put("mod_hydrant_cast", x, z, a[2], ["street"]); break
        for a in anchors("mod_bus_shelter", "mod_mailbox", "mod_newspaper_box"):
            for dx in (2.8, -2.8, 3.5, -3.5):
                x, z = beside(a, dx, 0.2)
                if far("mod_payphone", x, z, 15) and free("mod_payphone", x, z, a[2]): put("mod_payphone", x, z, a[2], ["street"]); break
        for a in anchors("mod_roadblock", "mod_jersey_barrier", "mod_jersey_barrier_damaged", "mod_water_barrier_red", "mod_sandbag_position"):
            n = 0
            for dx, dz in ((2.1, 1.0), (-2.1, 1.0), (2.1, -1.0), (-2.1, -1.0), (0.0, 1.4), (0.0, -1.4)):
                x, z = beside(a, dx, dz)
                if far("mod_traffic_barrel", x, z, 1.6) and free("mod_traffic_barrel", x, z, 0, 0.2):
                    put("mod_traffic_barrel", x, z, rng.uniform(0, 360), ["roadworks"]); n += 1
                if n >= (3 if a[3] == "mod_roadblock" else 1): break
            if sum(1 for p in placed if p["asset"] == "mod_traffic_barrel") >= 18: break
        cx, cz = -131.0, 64.0   # the park
        for aid in ("mod_play_slide_old", "mod_play_swings_old"):
            done = False
            for r in (8.0, 11.0, 14.0, 17.0):
                for k in range(24):
                    t = 2 * math.pi * k / 24; x, z = cx + r * math.cos(t), cz + r * math.sin(t); yaw = (k * 15) % 180
                    if free(aid, x, z, yaw, 1.0): put(aid, x, z, yaw, ["park"]); done = True; break
                if done: break
    else:
        for a in anchors("mod_jersey_barrier", "mod_jersey_barrier_damaged", "mod_sandbag_position", "mod_hesco_3x1")[:14]:
            for dx, dz in ((2.0, 1.1), (-2.0, 1.1), (2.0, -1.1), (-2.0, -1.1)):
                x, z = beside(a, dx, dz)
                if far("mod_traffic_barrel", x, z, 6) and free("mod_traffic_barrel", x, z, 0, 0.2):
                    put("mod_traffic_barrel", x, z, rng.uniform(0, 360), ["checkpoint"]); break
    return placed


def main():
    out = {}
    for tid in ("newtown", "outpost_desert", "outpost_woods"):
        mp = os.path.join(REPO, "content", "templates", tid + ".nmap.json")
        with open(mp, encoding="utf-8") as f: doc = json.load(f)
        if any(isinstance(i.get("asset"), dict) and i["asset"]["id"] in ("mod_hydrant_cast", "mod_payphone", "mod_traffic_barrel", "mod_play_slide_old", "mod_play_swings_old") for i in doc["instances"]):
            print(tid, "already has PR1 props: skipped"); continue
        placed = plan(tid, doc, random.Random(4021)); out[tid] = placed
        h = height_fn(tid)
        counts = {}
        for p in placed:
            counts[p["asset"]] = counts.get(p["asset"], 0) + 1
            doc["instances"].append({"id": doc["nextId"], "asset": {"id": p["asset"], "version": asset(p["asset"]).get("version", 1)},
                                     "position": [p["x"], round(h(p["x"], p["z"]), 4), p["z"]], "yaw": p["yaw"], "overrides": {"interactables": {}}})
            doc["nextId"] += 1
        print(tid, counts)
        if WRITE:
            with open(mp, "w", encoding="utf-8", newline="") as f: json.dump(doc, f, separators=(",", ":"))
    if WRITE and out:
        with open(os.path.join(REPO, "tools", "mapgen", "maps", "pr1_props.json"), "w", encoding="utf-8") as f: json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
