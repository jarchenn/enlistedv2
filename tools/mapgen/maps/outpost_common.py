"""NT3: two small Battle Royale maps around an abandoned military outpost, 126 x 140 m (1/8 of Newtown), in the
NT2 abandoned / overgrown look. One generator, two thoroughly different variants (outpost_desert.py, outpost_woods.py):

  Desert: a HESCO forward base on a low rise above a dry wadi, a ruined roadside hamlet with a petrol station on an
          old highway, a rocky mesa with a radar and an observation bunker. Sand, scrub, dead trees; dust haze.
  Woods:  a palisaded camp in a forest clearing, a radar on the ridge, a sawmill on the logging track, cabins by a
          pond fed by a creek; dense dark woodland, ferns and bracken; mist.

Every enterable building carries extra.loot {size, district[, military]} (military: level-3 armour, scoped rifles);
scenario.districts names the landing spots; extra.royale scales the match (storm, cars, bot landing spread);
scenario rules default to 16 players.

    <python> -I tools/mapgen/build.py outpost_desert | outpost_woods
"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, stamp_valley, stamp_flatten_poly, catmull_rom,
                    write_map, write_template, write_summary, asset_usage, rect, rng, point_at, polyline_length,
                    polyline_dist, asset_yaw_facing, asset_yaw_along, look_yaw, local_to_world, fbm, circle_poly,
                    smoothstep, lerp, clamp)
from maps.backrooms import update_usage_doc
from maps.newtown import cover_catalogue, stamp_road

W, D, MARGIN = 126, 140, 60
HX, HZ = W / 2, D / 2
PCARS = ["compact", "hatchback", "minivan", "offroad", "pickup", "sedan", "sport", "suv", "wagon"]
WRECKS = ["mod_pcar_%s_wreck" % k for k in PCARS] + ["mod_pcar_%s" % k for k in PCARS] + ["mod_car_shell_burned", "mod_sedan_burned"]
ROYALE = {   # the small-map match (src/royale.cpp extra.royale): short circles, a tight final, two cars
    "storm": {"wait_s": [60, 45, 35, 25], "shrink_s": [30, 25, 20, 15], "dps": [1, 2, 4, 8], "radiusFrac": [0.45, 0.28, 0.16],
              "finalRadius_m": 9, "driftPause_s": 10, "driftMove_s": 15, "driftMin_m": 6, "driftMax_m": 14},
    "vehicles": 2, "vehicleSpacing_m": 25, "landSpread_m": 40}
PLAYERS = 16


def ruin_place(b, cat, seed):
    """Every placement through the abandoned substitutions (as Newtown): <id>_ruin variants, clean cars -> wrecks."""
    RW = rng(seed + 7)
    _place = b.place
    intact = {"mod_sedan", "mod_hatchback", "mod_suv", "mod_pickup"}

    def place_r(aid, *a, **k):
        if aid in intact:
            aid = RW.choice(WRECKS)
        elif aid + "_ruin" in cat:
            aid = aid + "_ruin"
        return _place(aid, *a, **k)
    b.place = place_r


def build_variant(ctx, V):
    cat = ctx.catalog
    R = rng(V["seed"])
    grid = Grid(W, D, margin=MARGIN)
    grid.apply(lambda x, z, h: V["height"](x, z))
    for fn in V.get("stamps", []):
        fn(grid)
    for pts, half in V.get("roads", []):
        stamp_road(grid, pts, half)
    for aid, x, z, yaw in V["sites"]:   # level the building pads
        a = cat[aid]
        hx, hz = a.half[0] + 1.5, a.half[1] + 1.5
        poly = [local_to_world((x, z), yaw, lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=5.0)
    for poly, h in V.get("flats", []):
        stamp_flatten_poly(grid, poly, height=h if h is not None else grid.sample(*poly[0]), falloff=8.0)
    grid.quantise()

    b = MapBuilder(cat, grid, Frame(W, D), seed=V["seed"], water_level=V["water"])
    ruin_place(b, cat, V["seed"])
    paint = GroundPaint()
    b.paint = paint
    rep = {"missed": {}, "lines": {}, "sites": 0}
    seedc = [1]

    def line(tid, pts, tags=()):
        ln = {"pts": [[round(p[0], 3), round(p[1], 3)] for p in pts], "seed": (seedc[0] * 2654435761) % 4294967296}
        seedc[0] += 1
        rep["lines"][tid] = rep["lines"].get(tid, 0) + 1
        return b.place("line_" + tid, pts[0][0], pts[0][1], 0.0, check=False, cls="none", extra={"line": ln}, tags={"line", tid} | set(tags),
                       y=b.ground(pts[0][0], pts[0][1]))

    def site(aid, x, z, yaw, district, military=False, tags=()):
        if aid not in cat:
            rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
            return None
        e = b.place(aid, x, z, yaw, tags=set(tags) | {"building"}, check=False)
        if e is None:
            rep["missed"]["%s@%d,%d" % (aid, x, z)] = 1
            return None
        a = cat[e["asset"]]
        if a.enterable and not aid.startswith(("mod_guard_tower",)):
            loot = {"size": "large" if a.fw * a.fd >= 280 or int(a.raw.get("floors", 1) or 0) >= 3 else "small", "district": district}
            if military:
                loot["military"] = True
            e["extra"]["br"] = dict(loot)
            e["extra"]["loot"] = dict(loot)
        rep["sites"] += 1
        return e

    def prop(aid, x, z, yaw, tags=(), **kw):
        if aid not in cat:
            rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
            return None
        return b.try_place(aid, x, z, yaw, R, tags=set(tags), **kw)

    def drop(aid, x, z, yaw, dy=0.0, cls="flat", ok=("flat", "none", "veg")):
        if aid not in cat or not grid.in_build(x, z) or b.ground(x, z) < V["water"] + 0.25:
            return None
        if any(math.hypot(x - p[0], z - p[1]) < V.get("views_clear", 3.0) + max(cat[aid].half) for _, p, _ in V["views"]):
            return None
        if b.overlaps(cat[aid], x, z, yaw, set(ok)):
            return None
        return b.place(aid, x, z, yaw, check=False, cls=cls, dy=dy, tags={"dress"})

    # keep-outs first: roads, tracks, the clearings that must stay open
    for pts, half in V.get("roads", []):
        b.keepout_line(pts, half + 1.0)
    for pts, w in V.get("tracks", []):
        b.keepout_line(pts, w * 0.5 + 0.6)
    for p in V["tdm"].values():
        b.keepout_poly(circle_poly(p[0], p[1], 6.0))
    for name, p, t in V["views"]:   # the audit / selftest eyes stand in the open
        b.keepout_poly(circle_poly(p[0], p[1], V.get("views_clear", 3.0)))

    # paint, lines, buildings, then the variant's own dressing
    for args in V.get("paint", []):
        getattr(paint, args[0])(*args[1:-1], **args[-1])
    for pts, w in V.get("tracks", []):
        paint.line(V["track_paint"], pts, w, weight=0.9, edge=1.2)
    for pts, half in V.get("roads", []):
        line("mod_road_2lane", pts, tags={"road"})
    for row in V["sites"]:
        aid, x, z, yaw = row
        info = V["site_info"].get((aid, x, z), {})
        site(aid, x, z, yaw, info.get("d", 1), military=info.get("mil", False), tags=info.get("tags", ()))
    V["dress"](b, R, prop, drop, line, paint, rep)

    # weeds, rubble and litter against every building (the overgrown look), the variant's mix
    for e in [e for e in b.inst if "building" in e["tags"]]:
        a = cat[e["asset"]]
        hx, hz = a.half
        for q in range(R.randint(3, 6)):
            side = R.randrange(4)
            u = R.uniform(-0.45, 0.45)
            lx, lz = ((u * 2 * hx, -hz - 0.7), (u * 2 * hx, hz + 0.7), (-hx - 0.7, u * 2 * hz), (hx + 0.7, u * 2 * hz))[side]
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)
            aid = R.choice(V["wall_dress"])
            drop(aid, wx, wz, R.uniform(0, 360), cls="solid" if aid.startswith(("mod_rubble", "mod_wall", "mod_glass")) else "veg")

    # vegetation fields and the decor ring
    for row in V["scatter"]:   # (pool, region, density, min_dist[, place kwargs]); ground cover may touch other plants
        pool, region, dens, md = row[:4]
        b.scatter(pool, region, dens, R, bounds=(-HX, -HZ, HX, HZ), tags={"veg"}, min_dist=md, **({"cls": "veg"} | (row[4] if len(row) > 4 else {})))
    nt = 0
    for _ in range(4000):
        x, z = R.uniform(-HX - 45, HX + 45), R.uniform(-HZ - 45, HZ + 45)
        if (abs(x) < HX + 4 and abs(z) < HZ + 4) or nt >= V["decor_n"]:
            continue
        aid = R.choice(V["decor"])
        if b.overlaps(cat[aid], x, z, 0.0, {"flat", "none"}):
            continue
        nt += b.place(aid, x, z, R.uniform(0, 360), check=False, extra={"beautify": True, "decor": True}, tags={"decor"}) is not None
    rep["decor"] = nt

    # scenario: Battle Royale (drop, no spawns), TDM team spawns, one Assault objective (the validator needs one)
    sc = ScenarioBuilder(b, defending="Axis")
    sc.rules.update({"mode": "royale", "lootModern": True, "teamFaction": ["Axis", "Allies"], "royalePlayers": PLAYERS, "soloPlayers": 10,
                     "teamPlayers": [6, 6]})
    ox, oz = V["objective"]
    mil = [e for e in b.inst if (e.get("extra") or {}).get("loot", {}).get("military")]
    sc.objective(V["objective_name"], ox, oz, radius=18.0, capture_time=40.0, attack_from=V["attack_from"], linked=[e["id"] for e in mil])
    sc.spawn("attacker", [0], *V["attack_from"], label="Attackers", rack=None)
    sc.spawn("defender", [0], *V["defend_at"], label="Defenders", rack=None)
    for team, key, fac in ((0, "A", "Axis"), (1, "B", "Allies")):
        x, z = V["tdm"][key]
        m = sc.spawn("defender" if team == 0 else "attacker", [0], x, z, face=(0.0, 0.0), faction=fac, label="Team %s spawn" % key, rack="auto")
        m["team"], m["phases"] = team, []
        yw = math.radians(m["yaw"])
        b.try_place("crate_mixed_supply", x + math.sin(yw) * 3.0, z + math.cos(yw) * 3.0, m["yaw"], R, tags={"supply"}, radius=3.0)
    sj = sc.to_json()
    sj["districts"] = [{"id": k + 1, "name": n, "pos": [c[0], round(b.ground(*c), 2), c[1]], "radius": 22.0} for k, (n, c) in enumerate(V["districts"])]

    # output
    tid = V["tid"]
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, tid + ".height.png"))
    eye = lambda x, z: [round(x, 2), round(b.ground(x, z) + 1.7, 2), round(z, 2)]
    views = [{"name": "overview_high", "pos": [0.0, 120.0, 130.0], "yaw": 0.0, "pitch": -45.0}]
    for name, (x, z), (tx, tz) in V["views"]:
        views.append({"name": name, "pos": eye(x, z), "yaw": look_yaw(tx - x, tz - z), "pitch": 2.0, "gameplay": True})
    cam = ([0.0, 90.0, 120.0], 0.0, -38.0)
    sx, sz = V["start"][0]
    write_map(os.path.join(tdir, tid + ".nmap.json"), name=V["name"], template_id=tid, template_version=1, seed=V["seed"], width=W, depth=D,
              builder=b, scenario_json=sj, explore_start=((sx, b.ground(sx, sz), sz), look_yaw(*V["start"][1])), camera=cam,
              bookmarks={str(k + 1): {"position": [c[0], b.ground(*c) + 40.0, c[1] + 45.0], "yaw": 0.0, "pitch": -40} for k, (n, c) in enumerate(V["districts"])},
              extra={"groundPaint": paint.to_json(), "waterBodies": V.get("water_bodies", []), "look": V["look"], "lighting": V["lighting"],
                     "royale": ROYALE})
    write_template(os.path.join(tdir, tid + ".json"), tid=tid, name=V["name"], order=V["order"], version=1, seed=V["seed"],
                   description=V["description"], width=W, depth=D, margin=MARGIN, heightmap=grid.meta(tid + ".height.png"),
                   water_level=V["water"], map_file=tid + ".nmap.json", camera=cam, thumbnail=tid + ".thumb.png",
                   thumb_camera=V["thumb_cam"], extra={"userMap": True, "views": views})
    use = asset_usage(b)
    doc = os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md")
    update_usage_doc(doc, use, V["name"], "- **%s:** *%s* (NT3, `tools/mapgen/maps/%s.py`): placed counts; \"not used\" = not on this map." % (V["name"], V["name"], tid))
    cover_catalogue(doc, cat)
    rep.update({"instances": len(b.inst), "use": dict(sorted(use.items())), "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())},
                "districts": sj["districts"]})
    write_summary(os.path.join(ctx.out, tid + ".summary.json"), rep)
    print("instances %d | sites %d | decor %d | lines %s | missed %s" % (len(b.inst), rep["sites"], rep["decor"], rep["lines"], rep["missed"]))
