"""VH1 village: the eight House Village houses (tools/props/vh1_assets.py, the user's GLB by remib74, CC-BY-4.0) laid out
as in the GLB's own scene: each at its original centre and turn (E:/nmk/village/centres.json: 0.29 m per source unit,
the scene's cobbled square as a cobbles patch), on flat grass; doors and ground-floor windows open, furnished inside.

    <python> -I tools/mapgen/build.py vh1_village"""
import os

from mapgen import Grid, Frame, MapBuilder, ScenarioBuilder, GroundPaint, write_map, write_template, write_summary, look_yaw

TID, NAME, SEED = "vh1_village", "House Village", 5101
W = D = 140.0
S = 0.29                      # metres per source unit
OX, OY = 22.89, -11.39        # the source scene's ground square centre (200 x 200 units)
# (asset, source centre x, y, turn deg): yaw = the turn (the asset was squared by -turn; local +Z = source -y)
HOUSES = [("vh_cottage_shutters", 45.279, -18.236, -38.62), ("vh_cottage_tall", 63.07, 6.978, 41.4), ("vh_longhouse_blue", -3.045, -8.676, -43.38),
          ("vh_manor_tower", 64.762, -43.329, -41.49), ("vh_stone_house_l", 72.997, 45.908, 1.98), ("vh_townhouse", 6.801, -40.976, -34.5),
          ("vh_timber_house", -24.75, 39.63, -32.49), ("vh_barn_cottage", 29.169, 33.675, 23.28)]


def world(x, y): return (S * (x - OX), -S * (y - OY))


def build(ctx):
    frame = Frame(W, D)
    grid = Grid(W, D, margin=40, base=10.0)
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    y0 = b.ground(0.0, 0.0)
    paint = GroundPaint()
    h = S * 100 + 2.0   # the cobbled square, half size
    paint.poly("cobbles", [(-h, -h), (h, -h), (h, h), (-h, h)], weight=1.0, edge=1.5, ragged=1.5, seed=7)
    for aid, x, y, turn in HOUSES:
        wx, wz = world(x, y)
        b.place(aid, wx, wz, turn, check=False, cls="none", y=y0, tags={"vh1"})
    sc = ScenarioBuilder(b)
    sc.objective("Village square", 0.0, 0.0, attack_from=(0.0, -h - 20.0))   # the editor saves a scenario only with an objective
    sc.spawn("attacker", [0], 0.0, -h - 14.0, label="south road", face=(0.0, 0.0), rack=None)
    sc.spawn("defender", [0], 0.0, h + 14.0, label="north field", face=(0.0, 0.0), rack=None)
    for s in sc.spawns:
        s["pos"][1] = y0
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    cam = ([0.0, 38.0, 58.0], look_yaw(0.0, -1.0), -32.0)
    views = [{"name": "overview", "pos": [0.0, 34.0, 52.0], "yaw": look_yaw(0.0, -1.0), "pitch": -30.0}]
    for aid, x, y, turn in HOUSES:
        wx, wz = world(x, y)
        views.append({"name": aid, "pos": [wx + 6.0, y0 + 1.7, wz + 9.0], "yaw": look_yaw(-6.0, -9.0), "pitch": -4.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=1, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra={"mode": "tdm"}), explore_start=((0.0, y0, -h - 10.0), look_yaw(0.0, 1.0)), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 14.0}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=32, version=1, seed=SEED,
                   description="VH1: the House Village set (remib74, CC BY 4.0): eight stone and timber houses round a cobbled square as in the original scene; every door and ground-floor window opens, each house furnished.",
                   width=W, depth=D, margin=40, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0, map_file=TID + ".nmap.json",
                   camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=([0.0, 20.0, 44.0], look_yaw(0.0, -1.0), -22.0), playable=True,
                   extra={"userMap": True, "selftest": "authored", "views": views})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), {"instances": len(b.inst)})
    print("instances %d" % len(b.inst))
