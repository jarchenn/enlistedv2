"""Toolkit example and smoke test: a 600 x 1000 m two-objective map exercising every toolkit feature.
Writes to tools/mapgen/out/ only (not shipped):  <python> -I tools/mapgen/build.py example_small"""
import os

from mapgen import (Grid, Frame, MapBuilder, ScenarioBuilder, sculpt_zones, stamp_valley, stamp_crater, stamp_flatten,
                    split_fields, hedgerow, orchard, sunken_lane, cut_gaps, site_mg_nests, build_mg_nest,
                    interlock_report, slots_from_structures, catmull_rom, write_map, write_template, write_summary,
                    asset_usage, rect, rng)

W, D, TID = 600, 1000, "example_small"


def build(ctx):
    R = rng(7)
    frame = Frame(W, D)
    grid = Grid(W, D, margin=100)
    # zone table along the attack axis (s = metres from the attacker edge)
    sculpt_zones(grid, frame, [
        {"s": (-200, 300), "height": (0.0, 2.0), "noise": (0.6, 60)},
        {"s": (300, 450), "height": (2.0, 22.0)},                    # a ridge
        {"s": (450, 1200), "height": (22.0, 26.0), "noise": (1.5, 90)},
    ])
    draw = frame.pts([(-40, 250), (-30, 380), (0, 520)])
    stamp_valley(grid, draw, floor=lambda u: 2.0 + 20 * u, top_half=lambda u: 60 - 30 * u, floor_half=lambda u: 10.0)
    for k in range(12):
        x, z = frame.w(R.uniform(-250, 250), R.uniform(80, 280))
        stamp_crater(grid, x, z, R.uniform(3, 6), R.uniform(0.8, 1.6))
    stamp_flatten(grid, *frame.w(150, 760), radius=25, falloff=15)
    grid.quantise()

    b = MapBuilder(ctx.catalog, grid, frame, seed=7)
    road = catmull_rom(frame.pts([(-35, -480), (-35, 250), (-25, 400), (0, 560), (60, 700), (150, 760), (160, 980)]))
    b.route("lane", road, 4.0)
    # bocage on the upper plateau with the road cutting through the hedges
    fields, lines = split_fields(frame.pts([(-290, 470), (290, 470), (290, 990), (-290, 990)]), R)
    grp = b.new_group()
    for ln in lines:
        hedgerow(b, ln, R, group=grp, gaps=cut_gaps(ln, [road], 6.0))
    orchard(b, fields[0], R)
    sunken_lane(b, frame.pts([(200, 480), (210, 700)]))
    # obstacle belt with gaps, wire, scattered brush
    b.belt(["def_tank_trap", "bch_belgian_gate"], frame.pts([(-280, 200), (280, 200)]), 9.0, R, gaps=[(150, 190), (400, 430)],
           jitter_across=2.0, yaw_jitter=15)
    b.chain("wire_concertina", frame.pts([(-280, 330), (280, 330)]), along="z", gaps=[(270, 290)])
    b.scatter([("shrub_bramble", 1), ("shrub_hazel", 1)], lambda x, z: 320 < frame.s_of(z) < 450, 1 / 40.0, R,
              bounds=(-300, frame.w(0, 320)[1], 300, frame.w(0, 450)[1]))
    # defence: a trench, an objective with interlocking MGs
    obj = frame.w(-20, 480)
    trench = b.chain(["aw_trench_straight", "aw_trench_zigzag"], frame.pts([(-120, 470), (80, 470)]), front=frame.w(0, 0))
    lanes = [frame.pts([(-35, 50), (-30, 380), (-20, 480)]), frame.pts([(-200, 60), (-150, 400)]), frame.pts([(200, 60), (150, 400)])]
    nests = site_mg_nests(b, obj, lanes, 3, R, accept=lambda x, z: grid.slope_deg(x, z) < 22)
    slots, nest_list = [], []
    for pos, bore in nests:
        ins, sl = build_mg_nest(b, pos, bore, rng=R)
        if sl:
            slots.append(sl)
            nest_list.append((pos, bore))
    slots += slots_from_structures(b, trench)
    sc = ScenarioBuilder(b)
    sc.objective("Ridge trench", *obj, linked=[e["id"] for e in trench], slots=slots, attack_from=frame.w(0, 0))
    sc.objective("Farm", *frame.w(150, 760), attack_from=obj)
    sc.spawn("attacker", [0], *frame.w(0, 30))
    sc.spawn("attacker", [1], *frame.w(-60, 380))
    sc.spawn("defender", [0], *frame.w(-60, 760))
    sc.spawn("defender", [1], *frame.w(150, 980))
    err, warn = sc.validate()
    out = ctx.out
    grid.write_png(os.path.join(out, TID + ".height.png"))
    write_map(os.path.join(out, TID + ".nmap.json"), name="Example", template_id=TID, template_version=1, seed=7, width=W, depth=D,
              builder=b, scenario_json=sc.to_json())
    write_template(os.path.join(out, TID + ".json"), tid=TID, name="Example", description="toolkit smoke test", order=99, version=1,
                   seed=7, width=W, depth=D, margin=100, heightmap=grid.meta(TID + ".height.png"), water_level=-1000,
                   map_file=TID + ".nmap.json", camera=([0, 60, -D / 2 - 40], 180, -15))
    rep = {"instances": len(b.inst), "use": asset_usage(b), "rejects": {"%s/%s" % k: v for k, v in b.rejects.items()},
           "errors": err, "warnings": warn, "interlock": interlock_report(b, nest_list, lanes, obj),
           "coverGapsMain": [g for g in b.cover_gaps(lanes[0]) if g[2] > 50]}
    write_summary(os.path.join(out, TID + ".summary.json"), rep)
    print("instances %d, errors %s, interlock %s" % (len(b.inst), err, rep["interlock"]))
