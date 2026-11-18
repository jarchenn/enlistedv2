"""RB1 test map: the three reference buildings recreated (docs/rb1-buildings.md) on flat ground along a road, a spawn
in front of each, so they can be walked from Play (user maps, team deathmatch) and opened in the editor.

    <python> -I tools/mapgen/build.py rb1_test"""
import os

from mapgen import Grid, Frame, MapBuilder, ScenarioBuilder, GroundPaint, write_map, write_template, write_summary, rng, look_yaw

TID, NAME, SEED = "rb1_test", "RB1 Reference Buildings", 4401
W = D = 160.0
# (asset, x, z): fronts face +z. The shop recreation and the reference model side by side (same orientation, about 7 m
# apart: the recreation's verandah side to the reference's low side), the other two recreations further back
BUILDINGS = [("rb1_weapon_shop", -9.0, 0.0), ("rb1_ref_shop", 9.0, 0.0), ("rb1_container_cabin", -28.0, -40.0), ("rb1_log_house", 16.0, -40.0)]


def build(ctx):
    frame = Frame(W, D)
    grid = Grid(W, D, margin=40, base=10.0)
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    y0 = b.ground(0.0, 0.0)
    paint = GroundPaint()
    paint.poly("dirt", [(-W / 2 - 40, -D / 2 - 40), (W / 2 + 40, -D / 2 - 40), (W / 2 + 40, D / 2 + 40), (-W / 2 - 40, D / 2 + 40)], weight=1.0, edge=0.5)
    paint.poly("grass", [(-W / 2 - 40, -D / 2 - 40), (W / 2 + 40, -D / 2 - 40), (W / 2 + 40, -12), (-W / 2 - 40, -12)], weight=0.8, edge=3.0, ragged=2.0, seed=3)
    paint.poly("rock", [(-W / 2, 13.0), (W / 2, 13.0), (W / 2, 19.0), (-W / 2, 19.0)], weight=1.0, edge=0.4)   # the road (asphalt slot)
    placed = []
    adir = os.path.join(ctx.repo, "content", "assets")
    for aid, x, z in BUILDINGS:
        if os.path.exists(os.path.join(adir, aid + ".json")):   # built one at a time (tools/catalog_rb1.mjs)
            placed.append(b.place(aid, x, z, 0.0, check=False, cls="none", y=y0, tags={"rb1"}))
    sc = ScenarioBuilder(b)
    sc.objective("Reference pair", 0.0, 6.0, attack_from=(0.0, 16.0))   # the editor saves a scenario only with an objective
    sc.spawn("attacker", [0], 0.0, 16.0, label="the pair: recreation (left) and reference (right)", face=(0.0, 0.0), rack=None)
    sc.spawn("defender", [0], 2.0, 16.0, label="the pair, second spawn", face=(0.0, 0.0), rack=None)
    for aid, x, z in BUILDINGS[2:]:
        sc.spawn("attacker", [0], x, z + 9.5, label="%s front" % aid, face=(x, z), rack=None)
    for s in sc.spawns:
        s["pos"][1] = y0
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    cam = ([0.0, 30.0, 52.0], look_yaw(0.0, -1.0), -28.0)   # yaw 0 faces -z: towards the building fronts
    views = [{"name": "overview", "pos": [0.0, 26.0, 46.0], "yaw": look_yaw(0.0, -1.0), "pitch": -24.0}]
    for aid, x, z in BUILDINGS:
        views.append({"name": aid + "_front", "pos": [x + 2.0, y0 + 1.7, z + 13.0], "yaw": look_yaw(-2.0, -13.0), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=1, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra={"mode": "tdm"}), explore_start=((0.0, y0, 16.0), look_yaw(0.0, -1.0)), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 15.0}, "look": {"paintSets": {"rock": "yard_asphalt_paint"}}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=31, version=1, seed=SEED,
                   description="RB1: the weapon shop recreation (left) beside the reference model itself (right, by YadroGames, CC BY 4.0), same orientation, a spawn in front of both; the site cabin and log house recreations further back.",
                   width=W, depth=D, margin=40, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0, map_file=TID + ".nmap.json",
                   camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=([0.0, 14.0, 40.0], look_yaw(0.0, -1.0), -16.0), playable=True,
                   extra={"userMap": True, "selftest": "authored", "views": views})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), {"instances": len(b.inst)})
    print("instances %d" % len(b.inst))
