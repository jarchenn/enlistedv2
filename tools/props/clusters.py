"""PR4: ready-made prop clusters for the Search and Destroy maps (desert compound, container yard, town quarter).

Each cluster is a list of members: (asset id, x, z, yaw, y) in metres / degrees, y = the height it stands on (a crate on a
crate). Two outputs:
  - tools/props/clusters.json, read by mapgen: every cluster with its members, footprint, height and cover class; a
    cluster may mix Poly Haven models (ph_*) with in-house assets (sandbags, the guard tower, carts, rubble).
  - content/assets/cl_<name>.json, a placeable asset for every cluster made only of ph_* models: the members' baked
    models attached at their offsets (the asset "models" block, src/assets.cpp), their collider boxes turned into place
    (invisible parts), cover points on the four sides when the stack is at least 0.8 m tall.
docs/prop-clusters.md describes them; --capture-props shoots the cl_* assets for its contact sheet.

    python -I tools/props/clusters.py
"""
import json, math, os

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PH = json.load(open(os.path.join(HERE, "ph_props.json"), encoding="utf-8"))

# name: (theme, title, purpose, members)
C = {
 # ---------------------------------------------------------------- desert compound (Sun Fort)
 "desert_supply_stack": ("desert", "Supply stack", "Crates stacked two high against a wall or in the open: low and high cover.", [
   ("ph_wooden_military_crate", 0, 0, 0, 0), ("ph_wooden_military_crate", 0.05, 0.02, 4, 0.47), ("ph_old_military_crate", 1.1, 0.1, 85, 0),
   ("ph_wooden_crate_02", -0.95, 0.05, 90, 0), ("ph_cardboard_box_01", 1.1, 0.75, 20, 0), ("ph_plastic_crate_01", -0.9, 0.75, -15, 0)]),
 "desert_fuel_point": ("desert", "Fuel point", "Drums, jerrycans and gas bottles by a vehicle stand: low cover (and a reason not to shoot near it).", [
   ("ph_barrel_01", 0, 0, 0, 0), ("ph_barrel_02", 0.6, 0.05, 30, 0), ("ph_barrel_03", 0.3, -0.55, 0, 0), ("ph_barrel_03", -0.6, -0.1, 50, 0),
   ("ph_metal_jerrycan_green", 1.1, 0.3, 90, 0), ("ph_metal_jerrycan_green", 1.1, -0.05, 85, 0), ("ph_metal_jerrycan", -0.5, 0.6, 10, 0),
   ("ph_propane_tank", -1.1, 0.3, 0, 0), ("ph_small_lpg_tank", -1.15, -0.3, 0, 0)]),
 "desert_generator_corner": ("desert", "Generator corner", "Generator, fuel, toolbox and a work light: the compound's power point.", [
   ("ph_portable_generator", 0, 0, 0, 0), ("ph_metal_jerrycan", 0.65, 0.1, 80, 0), ("ph_metal_toolbox", -0.65, 0.15, -20, 0),
   ("ph_power_box_01", 0.1, -0.55, 0, 0), ("ph_old_tyre", -0.9, -0.45, 70, 0), ("ph_barrel_01", 1.15, -0.35, 0, 0)]),
 "desert_scrap_pile": ("desert", "Tyre and scrap pile", "Tyres, rims, a burnt drum and cans: low cover, a hiding place in the open.", [
   ("ph_old_tyre", 0, 0, 0, 0, (90, 0)), ("ph_old_tyre", 0.05, 0.03, 10, 0.165, (90, 0)), ("ph_old_tyre", 0.6, 0.3, 80, 0), ("ph_rusted_wheel_rim_01", -0.55, 0.25, 20, 0),
   ("ph_rusted_wheel_rim_02", -0.45, -0.35, 70, 0), ("ph_barrel_stove", 0.75, -0.45, 0, 0), ("ph_can_rusted", 0.2, 0.6, 0, 0)]),
 "desert_rocks_quiver": ("desert", "Rocks with quiver tree", "Boulders, a quiver tree and succulents: hard cover and a landmark.", [
   ("ph_namaqualand_boulder_03", 0, 0, 0, 0), ("ph_namaqualand_boulder_06", 1.7, 0.6, 40, 0), ("ph_namaqualand_boulders_01", -1.5, 0.9, 0, 0),
   ("ph_quiver_tree_01", -1.2, -1.4, 0, 0), ("ph_cheiridopsis_succulent", 1.2, -1.3, 0, 0)]),
 "desert_rocks_deadwood": ("desert", "Rocks and deadwood", "A boulder pile with a dead quiver trunk and branches: low and high cover.", [
   ("ph_namaqualand_boulder_02", 0, 0, 0, 0), ("ph_namaqualand_boulder_04", -2.0, 0.4, 60, 0), ("ph_namaqualand_rocks_01", 1.6, 0.9, 20, 0),
   ("ph_dead_quiver_trunk", 1.4, -0.9, 0, 0), ("ph_dead_quiver_branch_01", 0.8, 1.2, 40, 0), ("ph_othonna_cerarioides", -0.5, -1.8, 0, 0)]),
 "desert_sandbag_position": ("desert", "Sandbag position (prefab)", "Curved sandbag wall with ammunition and a jerrycan behind it.", [
   ("def_sandbag_curved", 0, 0, 0, 0), ("ph_wooden_military_crate", 0, -1.3, 0, 0), ("ph_metal_jerrycan_green", 0.9, -1.2, 80, 0)]),
 "desert_guard_post": ("desert", "Guard post (prefab)", "The steel guard tower with a supply stack and a work light at its foot.", [
   ("mod_guard_tower", 0, 0, 0, 0), ("ph_wooden_military_crate", 2.4, 0.2, 90, 0), ("ph_flood_lights", -2.6, 1.0, 30, 0)]),
 # ---------------------------------------------------------------- container yard (night)
 "yard_pipe_rack": ("yard", "Pipe rack", "The industrial pipe set with tools at its foot: high cover between containers.", [
   ("ph_modular_industrial_pipes_01", 0, 0, 0, 0), ("ph_metal_toolbox", 1.0, 0.4, 20, 0), ("ph_crowbar_01", 0.6, 0.6, 70, 0), ("ph_barrel_01", -1.0, 0.3, 0, 0)]),
 "yard_camera_pole": ("yard", "Camera pole", "An electricity pole with a security camera and a utility box: a landmark and a sightline hint.", [
   ("ph_modular_electricity_poles", 0, 0, 0, 0), ("ph_security_camera_01", 0.15, 0.25, 0, 4.4), ("ph_utility_box_01", 0.6, -0.4, 0, 0)]),
 "yard_tool_carts": ("yard", "Tool carts", "Tool cart, tool chest, welding cart and hand truck: low and medium cover.", [
   ("ph_tool_cart", 0, 0, 0, 0), ("ph_metal_tool_chest", 1.15, 0.05, -10, 0), ("ph_portable_welding_cart", -1.1, 0.1, 15, 0), ("ph_hand_truck", 0.4, 0.9, 160, 0)]),
 "yard_barrel_stack": ("yard", "Barrel stack", "Drums two deep with a plastic tote: low cover.", [
   ("ph_barrel_01", 0, 0, 0, 0), ("ph_barrel_01", 0.6, 0, 20, 0), ("ph_barrel_02", 0.3, 0.55, 0, 0), ("ph_barrel_03", -0.6, 0.1, 0, 0),
   ("ph_industrial_pastic_container", -0.4, 0.75, 30, 0)]),
 "yard_workshop_corner": ("yard", "Workshop corner", "Drill presses, a compressor and a storage cart: medium cover in a shed or under a lamp.", [
   ("ph_drill_press_01", 0, 0, 0, 0), ("ph_old_drill_press", 0.8, 0.05, -10, 0), ("ph_old_military_compressor", -1.3, 0.2, 90, 0),
   ("ph_industrial_storage_cart", 0.3, 1.2, 180, 0)]),
 "yard_cable_drums": ("yard", "Cable drums (prefab)", "Cable reels with barrels and a tote.", [
   ("tun_cable_reel", 0, 0, 0, 0), ("tun_cable_reel", 1.6, 0.3, 30, 0), ("ph_barrel_03", -1.2, 0.2, 0, 0), ("ph_plastic_container", 0.8, 1.2, 10, 0)]),
 # ---------------------------------------------------------------- town quarter (Normandy)
 "town_cafe_terrace": ("town", "Cafe terrace", "Two table-and-chair sets, a chalkboard and a planter on the pavement.", [
   ("ph_outdoor_table_chair_set_01", 0, 0, 90, 0), ("ph_outdoor_table_chair_set_01", 2.0, 0.1, 90, 0), ("ph_standing_chalkboard_01", -1.4, 0.6, 30, 0),
   ("ph_planter_box_01", 1.0, -1.0, 0, 0), ("ph_plastic_monobloc_chair_01", 3.2, 0.6, -40, 0)]),
 "town_market_stall": ("town", "Market stall", "A painted table with baskets and crates: low cover in a square.", [
   ("ph_painted_wooden_table", 0, 0, 0, 0), ("ph_wicker_basket_01", -0.6, 0.1, 0, 0.96), ("ph_wicker_basket_02", 0.5, 0.0, 30, 0.96),
   ("ph_wooden_crate_01", -0.8, 0.9, 10, 0), ("ph_wooden_crate_01", 0.4, 0.95, -5, 0), ("ph_wine_barrel_01", 1.6, 0.4, 0, 0)]),
 "town_doorstep": ("town", "Doorstep clutter", "Bin, bags, a crate, a watering can and a plant by a door.", [
   ("ph_metal_trash_can", 0, 0, 0, 0), ("ph_trashbag", 1.2, 0.2, 30, 0), ("ph_plastic_crate_02", -1.2, 0.2, 10, 0), ("ph_watering_can_metal_01", -1.3, -0.3, 60, 0),
   ("ph_potted_plant_02", 1.3, -0.3, 0, 0)]),
 "town_bench_corner": ("town", "Bench corner", "Bench, street lamp, bin and hydrant at a street corner.", [
   ("ph_painted_wooden_bench", 0, 0, 0, 0), ("ph_street_lamp_01", 1.1, -0.2, 0, 0), ("ph_metal_trash_can", -1.0, 0.1, 0, 0), ("ph_fire_hydrant", 1.4, 0.8, 0, 0)]),
 "town_roadblock": ("town", "Roadblock", "Concrete barriers, drums and a covered car across a street: hard and low cover.", [
   ("ph_concrete_road_barrier", 0, 0, 0, 0), ("ph_concrete_road_barrier_02", 1.65, 0.1, 5, 0), ("ph_barrel_01", -1.1, 0.4, 0, 0), ("ph_barrel_03", 2.8, 0.3, 0, 0)]),
 "town_rubble": ("town", "Rubble (prefab)", "Brick rubble with a tyre and a barrier: low cover in a shelled street.", [
   ("rub_brick_pile_m", 0, 0, 0, 0), ("ph_old_tyre", 1.4, 0.6, 30, 0), ("ph_concrete_road_barrier_02", -1.8, 0.3, 70, 0)]),
 "town_cart": ("town", "Cart with sacks (prefab)", "The farm cart with sacks and a barrel beside it.", [
   ("drs_cart", 0, 0, 0, 0), ("ph_cement_bag", 1.4, 0.6, 20, 0), ("ph_wine_barrel_01", -1.5, 0.5, 0, 0)]),
}


def turn(x, z, yaw):
    a = math.radians(yaw); return x * math.cos(a) + z * math.sin(a), -x * math.sin(a) + z * math.cos(a)


assert all(abs(u - v) < 1e-9 for u, v in zip(turn(1, 0, 90), (0, -1)))   # yawQuat: +x turns to -z at 90 degrees (right-handed, y up)


def size_of(aid):
    """Model size [x, y, z] (m, ground pivot): the Poly Haven record, else any baked model.json (e.g. pr1_fuel_dispenser)."""
    if aid in PH: return PH[aid]["size"]
    m = json.load(open(os.path.join(ROOT, "content", "assets", "models", aid, "model.json"), encoding="utf-8"))
    return [m["bmax"][k] - m["bmin"][k] for k in range(3)]


def rot_box(size, pitch, roll):
    """Extents of the model's box turned by roll (about z) then pitch (about x), as the engine's attached tilt: (min, max) corners."""
    sx, sy, sz = size; lo, hi = [1e9] * 3, [-1e9] * 3
    cr, sr, cp, sp = math.cos(math.radians(roll)), math.sin(math.radians(roll)), math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    for x in (-sx / 2, sx / 2):
        for y in (0, sy):
            for z in (-sz / 2, sz / 2):
                x1, y1 = x * cr - y * sr, x * sr + y * cr            # roll about z
                y2, z2 = y1 * cp - z * sp, y1 * sp + z * cp          # pitch about x
                for k, v in enumerate((x1, y2, z2)): lo[k] = min(lo[k], v); hi[k] = max(hi[k], v)
    return lo, hi


assert abs(rot_box([0.2, 1, 0.2], 0, 90)[0][1] + 0.1) < 1e-9   # a drum rolled onto its side: half its width below the base (lifted by build)


def build(name, theme, title, purpose, members):
    model_only = all(m[0] in PH or os.path.exists(os.path.join(ROOT, "content", "assets", "models", m[0], "model.json")) for m in members)
    lo, hi, parts, models, mem, cover = [1e9, 1e9, 1e9], [-1e9, -1e9, -1e9], [], [], [], []
    for m in members:
        aid, x, z, yaw, y = m[:5]; tilt = m[5] if len(m) > 5 else None
        if aid in PH or model_only:
            size = size_of(aid)
            rlo, rhi = rot_box(size, *tilt) if tilt else ([-size[0] / 2, 0, -size[2] / 2], [size[0] / 2, size[1], size[2] / 2])
            y = y - rlo[1] if tilt else y                                   # rests on its lowest corner
            if tilt:   # the turned box centred where the author put it (a rolled pipe swings out to one side of its pivot)
                dx, dz = turn((rlo[0] + rhi[0]) / 2, (rlo[2] + rhi[2]) / 2, yaw); x, z = x - dx, z - dz
            top = y + rhi[1]
            ent = {"dir": aid, "pos": [round(x, 3), round(y, 3), round(z, 3)], "yaw": round(yaw, 1), "lodDist": PH.get(aid, {}).get("lodDist", 15)}
            if tilt: ent["tilt"] = [round(tilt[0], 1), round(tilt[1], 1)]
            models.append(ent)
            boxes = [[(rlo[0] + rhi[0]) / 2, (rlo[1] + rhi[1]) / 2, (rlo[2] + rhi[2]) / 2, rhi[0] - rlo[0], rhi[1] - rlo[1], rhi[2] - rlo[2]]] if tilt or aid not in PH                     else (PH[aid]["colliders"] or [[0, size[1] / 2, 0] + size])
            for cx, cy, cz, sx, sy, sz in boxes:
                dx, dz = turn(cx, cz, yaw); c = [x + dx, y + cy, z + dz]
                parts.append({"t": "b", "c": [round(v, 3) for v in c], "s": [round(sx, 3), round(sy, 3), round(sz, 3)], "r": [0, yaw, 0], "m": "collider", "v": 0})
                a = math.radians(yaw); ex = abs(sx * math.cos(a)) / 2 + abs(sz * math.sin(a)) / 2; ez = abs(sx * math.sin(a)) / 2 + abs(sz * math.cos(a)) / 2
                for k, (cc, e) in enumerate(((c[0], ex), (c[1], sy / 2), (c[2], ez))):
                    lo[k] = min(lo[k], cc - e); hi[k] = max(hi[k], cc + e)
            foot = (rhi[0] - rlo[0]) * (rhi[2] - rlo[2])
            cv = "stand" if top >= 1.3 and foot >= 0.25 else ("crouch" if top >= 0.8 and foot >= 0.25 else None)
            if cv:   # a cover point in front of and behind it, facing away from it
                d = (rhi[2] - rlo[2]) / 2 + 0.45
                for sgn, yw in ((1, 0), (-1, 180)):
                    dx, dz = turn(0, sgn * d, yaw)
                    cover.append({"p": [round(x + dx, 2), 0, round(z + dz, 2)], "yaw": round((yaw + yw) % 360, 1), "height": "high" if cv == "stand" else "low", "peek": ["over", "left", "right"]})
        else:
            top, cv = None, None
        mem.append({"asset": aid, "pos": [round(x, 3), round(y, 3), round(z, 3)], "yaw": round(yaw, 1), **({"tilt": list(tilt)} if tilt else {}), "cover": cv})
    h = hi[1] if model_only else max([size_of(m[0])[1] + m[4] for m in members if m[0] in PH] or [0])
    hc = max([1.3 if c["cover"] == "stand" else 0.8 if c["cover"] == "crouch" else 0 for c in mem] or [0])
    cov = "high" if hc >= 1.3 else ("low" if hc >= 0.8 else "none")
    entry = {"id": name, "theme": theme, "name": title, "purpose": purpose, "asset": "cl_" + name if model_only else None, "cover": cov,
             "height": round(h, 2), "footprint": [round(hi[0] - lo[0], 2), round(hi[2] - lo[2], 2)] if model_only else None, "count": len(members), "members": mem}
    if not model_only: return entry, None
    credits = sorted({PH[m[0]]["credit"] for m in members if m[0] in PH})
    asset = {"id": "cl_" + name, "version": 1, "name": title + " (cluster)", "category": "Clusters: " + {"desert": "Desert compound", "yard": "Container yard", "town": "Town quarter"}[theme],
             "description": purpose + " Poly Haven models (CC0): " + "; ".join(c.split(", Poly Haven")[0] for c in credits) + ".",
             "footprint": entry["footprint"], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5, "noAutoPanels": True,
             "models": models, "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": cover},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1}],
             "parts": parts, "furniture": [], "interactables": [], "panels": []}
    return entry, asset


# ---------------------------------------------------------------- PR4: the large tier (6-10 m, 15-30 objects, real height)
import random


class Lay:
    """Members of a large cluster: stacks, rows, leaning pieces and ground clutter, with seeded jitter (no grid look)."""
    def __init__(self, seed): self.m = []; self.r = random.Random(seed)
    def j(self, a): return self.r.uniform(-a, a)
    def put(self, aid, x, z, yaw=0, y=0, tilt=None, jit=8):
        self.m.append((aid, x, z, yaw + self.j(jit), y) + ((tilt,) if tilt else ()))
        if not tilt: return size_of(aid)[1]
        lo, hi = rot_box(size_of(aid), *tilt); return hi[1] - lo[1]
    def stack(self, aid, x, z, n, yaw=0, jit=6, off=0.04, tilt=None):   # n high, each a little off the one below (tyres: flat)
        y = 0
        for k in range(n): y += self.put(aid, x + self.j(off), z + self.j(off), yaw, y, tilt, jit=jit)
        return y
    def row(self, aid, x0, z, n, dx, yaw=0, jit=8, dz=0):
        for k in range(n): self.put(aid, x0 + k * dx + self.j(0.05), z + k * dz + self.j(0.05), yaw, jit=jit)
    def lean(self, aid, x, z, yaw, pitch=14):   # tipped back against whatever stands behind it
        self.put(aid, x, z, yaw, 0, (-pitch, self.j(3)), jit=4)
    def lie(self, aid, x, z, yaw):              # on its side: tipped about its long axis, so a table lies on its edge, not its end
        sx, _, sz = size_of(aid)
        self.put(aid, x, z, yaw, 0, (90, 0) if sx >= sz else (0, 90), jit=20)
    def litter(self, ids, x0, z0, x1, z1, n):   # ground clutter
        for k in range(n): self.put(self.r.choice(ids), self.r.uniform(x0, x1), self.r.uniform(z0, z1), self.r.uniform(0, 360), jit=0)


DESERT_LITTER = ["ph_can_rusted", "ph_oil_tin", "ph_plastic_bottle_gallon", "ph_cardboard_box_01", "ph_rusted_wheel_rim_02", "ph_metal_jerrycan"]
YARD_LITTER = ["ph_can_rusted", "ph_oil_tin", "ph_spray_paint_bottles", "ph_pipe_wrench", "ph_crowbar_01", "ph_cardboard_box_01", "ph_lubricant_spray"]
TOWN_LITTER = ["ph_trashbag", "ph_can_rusted", "ph_cardboard_box_01", "ph_plastic_crate_02", "ph_wooden_bowl_01", "ph_bleach_bottle"]


def big_clusters():
    B = {}
    L = Lay(11)   # desert supply dump: crates two and three high, sacks, a hand truck against the stack
    L.stack("ph_wooden_military_crate", -2.2, -0.6, 3, 0); L.stack("ph_wooden_military_crate", -0.9, -0.6, 2, 0); L.stack("ph_old_military_crate", -2.2, 0.6, 2, 90)
    L.stack("ph_wooden_crate_02", 0.5, -0.7, 3, 90); L.stack("ph_wooden_crate_01", 1.5, -0.7, 2, 0); L.stack("ph_wooden_crate_01", 2.5, -0.6, 3, 5)
    L.row("ph_cement_bag", -1.6, 1.6, 4, 0.55, 90, jit=20); L.put("ph_compost_bags", 1.2, 1.4, 20); L.stack("ph_cardboard_box_01", 0.2, 0.9, 2, 30, jit=20)
    L.lean("ph_hand_truck", -0.2, -0.05, 180); L.lean("ph_cardboard_box_01", 3.2, 0.3, 270, 10); L.put("ph_plastic_crate_01", 2.6, 1.0, 40); L.put("ph_wooden_military_crate", 3.4, -1.6, 70)
    L.litter(DESERT_LITTER, -3.4, -2.2, 3.6, 2.4, 6)
    B["desert_supply_dump"] = ("desert", "Supply dump (large)", "Crate stacks two and three high, sacks and loose boxes round a hand truck: standing cover behind the stacks, crouch cover behind the rows.", L.m)
    L = Lay(12)   # desert fuel dump: rows of drums, jerrycan rows, a pump, a barrier bund
    L.row("ph_concrete_road_barrier", -3.6, -2.4, 5, 1.55, 0, jit=3); L.row("ph_concrete_road_barrier_02", -4.6, -0.6, 2, 0, 90, jit=3, dz=1.6)
    for k, b in enumerate(("ph_barrel_01", "ph_barrel_02", "ph_barrel_03")): L.row(b, -3.4, -1.2 + k * 0.62, 6, 0.62, 0, jit=40)
    L.lie("ph_barrel_01", 1.0, 1.3, 80); L.lie("ph_barrel_03", 1.9, 1.8, 20); L.row("ph_metal_jerrycan_green", -3.2, 1.2, 7, 0.36, 90, jit=6)
    L.put("pr1_fuel_dispenser", 2.8, -0.6, 270, jit=0); L.row("ph_propane_tank", 2.2, 1.0, 2, 0.4, 0); L.put("ph_small_lpg_tank", 3.0, 1.1, 0)
    L.litter(DESERT_LITTER, -4.0, 1.6, 3.6, 2.6, 5)
    B["desert_fuel_dump"] = ("desert", "Fuel dump (large)", "Three rows of drums behind a barrier bund, jerrycan rows, a fuel pump and gas bottles: crouch cover along the drums, standing cover at the pump.", L.m)
    L = Lay(13)   # desert motor pool corner: tyre stacks, tools, oil drums, a bench, a compressor
    T = (90, 0); L.stack("ph_old_tyre", -3.0, -1.6, 6, 0, jit=25, off=0.05, tilt=T); L.stack("ph_old_tyre", -2.3, -1.7, 5, 0, jit=25, off=0.05, tilt=T); L.stack("ph_old_tyre", -2.6, -1.0, 3, 0, jit=25, tilt=T)
    L.lean("ph_old_tyre", -1.7, -1.9, 0, 18); L.row("ph_rusted_wheel_rim_01", -3.1, -0.2, 3, 0.45, 0, jit=40)
    L.put("ph_tool_cart", 0.0, -1.7, 0); L.put("ph_metal_tool_chest", 1.4, -1.8, 5); L.put("ph_metal_toolbox", 0.1, -1.7, 15, 0.97); L.put("ph_portable_generator", 2.6, -1.6, -10)
    L.put("ph_old_military_compressor", 2.9, 0.2, 90); L.row("ph_barrel_01", -0.8, 1.6, 3, 0.62, 0, jit=40); L.put("ph_barrel_stove", 1.4, 1.4, 0)
    L.put("ph_painted_wooden_table", -1.4, 0.3, 90); L.put("ph_metal_toolbox", -1.5, 0.0, 70, 0.96); L.put("ph_hand_truck", 0.6, 0.5, 200)
    L.row("ph_metal_jerrycan", 2.2, 1.5, 3, 0.36, 90); L.litter(DESERT_LITTER + ["ph_pipe_wrench", "ph_crowbar_01"], -3.4, -2.2, 3.4, 2.2, 7)
    B["desert_motor_pool"] = ("desert", "Motor-pool corner (large)", "Tyre stacks, a tool cart and chests, a generator and compressor, oil drums and a work table: crouch cover at the tyre stacks, standing cover at the compressor.", L.m)
    L = Lay(14)   # desert guard point: an L of barriers, crate seats, radio, water cans
    L.row("ph_concrete_road_barrier_02", -2.4, 2.0, 3, 1.58, 0, jit=2); L.row("ph_concrete_road_barrier_02", -3.2, 1.2, 3, 0, 90, jit=2, dz=-1.58)
    L.put("ph_wooden_military_crate", -0.8, 1.0, 0); L.put("ph_vintage_radio_transceiver", -0.8, 1.0, 180, 0.465); L.put("ph_old_military_crate", 0.8, 0.9, 10)
    L.put("ph_wooden_crate_01", -2.0, -0.4, 90); L.put("ph_wooden_crate_01", -2.0, 0.6, 85); L.row("ph_metal_jerrycan_green", 1.6, 1.3, 3, 0.36, 90)
    L.put("ph_Lantern_01" if "ph_Lantern_01" in PH else "ph_lantern_01", 0.8, 0.9, 0, 0.3); L.put("ph_medical_box", -1.1, -0.6, 30); L.put("ph_old_gas_mask", 0.2, -0.5, 60)
    L.put("ph_plastic_bottle_gallon", 1.9, 0.4, 0); L.put("ph_binoculars", -0.6, 1.2, 40, 0.465); L.litter(DESERT_LITTER, -2.4, -1.6, 2.4, 1.0, 5)
    B["desert_guard_point"] = ("desert", "Guard point (large)", "An L of concrete barriers with crate seats, a radio, water cans and a lantern: standing cover along the L.", L.m)
    L = Lay(21)   # yard pipe yard: pipes on racks three tiers high, drums, crates
    for x in (-3.0, 0.2):
        L.put("ph_steel_frame_shelves_03", x, -1.5, 0, jit=2)
        for t in range(4):   # a pyramid of pipe in front of the rack (5-4-3-2), lying along it
            for k in range(5 - t): L.put("ph_modular_pipes_straight_2m", x + L.j(0.08), -0.75 + (k - (4 - t) / 2) * 0.1, 0, t * 0.087, (0, 90), jit=1)
    L.put("ph_modular_industrial_pipes_01", 2.8, -1.4, 0); L.row("ph_modular_pipes_straight_1m", -3.0, 0.6, 5, 0.25, 0, jit=10)
    for k in range(4): L.lie("ph_modular_pipes_straight_2m", -0.8 + k * 0.18, 0.8, 0)
    L.stack("ph_wooden_crate_01", 1.6, 0.9, 2, 0); L.row("ph_barrel_01", 2.6, 0.6, 2, 0.62, 0, jit=30); L.put("ph_plastic_container", 2.9, 1.5, 20)
    L.litter(YARD_LITTER, -3.6, 1.3, 3.4, 2.4, 6)
    B["yard_pipe_yard"] = ("yard", "Pipe yard (large)", "Two steel racks with pyramids of pipe in front, loose pipe runs, the pipe manifold, crates and drums: standing cover behind the racks.", L.m)
    L = Lay(22)   # yard workshop bay: shelving, compressor, welding cart, drill presses, benches, bins
    L.row("ph_steel_frame_shelves_01", -3.4, -1.9, 3, 1.15, 0, jit=2); L.put("ph_worn_metal_rack", 0.4, -1.9, 0, jit=2)
    L.put("ph_old_military_compressor", 2.2, -1.6, 90); L.put("ph_portable_welding_cart", 3.2, -0.4, 200); L.put("ph_drill_press_01", -3.0, 0.3, 90)
    L.put("ph_old_drill_press", -3.0, 1.2, 80); L.put("ph_tool_cart", -0.8, 0.2, 0); L.put("ph_bench_vice_01", -0.5, 0.2, 0, 0.965); L.put("ph_metal_tool_chest", 0.6, 0.0, -5)
    L.put("ph_industrial_storage_cart", 1.4, 1.3, 180); L.put("ph_metal_trash_can", 2.6, 1.6, 0); L.row("ph_plastic_crate_03", -1.8, 1.7, 3, 0.5, 0, jit=15)
    L.put("ph_barrel_02", 3.3, 1.0, 0); L.put("ph_hand_truck", 0.4, 1.6, 150); L.litter(YARD_LITTER, -3.4, -0.8, 3.4, 2.2, 7)
    B["yard_workshop_bay"] = ("yard", "Workshop bay (large)", "Steel shelving along the back, a compressor, welding cart, drill presses, a tool cart with a vice, storage cart and bins: standing cover at the shelving and compressor.", L.m)
    L = Lay(23)   # yard loading area: crate stacks, hand trucks, barriers, a floodlight
    L.stack("ph_wooden_crate_01", -3.0, -1.8, 3, 0); L.stack("ph_wooden_crate_01", -2.1, -1.8, 3, 5); L.stack("ph_wooden_crate_01", -3.0, -1.0, 2, 0)
    L.stack("ph_wooden_military_crate", -0.4, -1.8, 2, 0); L.stack("ph_industrial_pastic_container", 1.0, -1.7, 3, 0, jit=10); L.stack("ph_plastic_container", 2.0, -1.7, 2, 0, jit=10)
    L.row("ph_concrete_road_barrier", -3.4, 2.2, 4, 1.6, 0, jit=3); L.lean("ph_hand_truck", -1.4, -1.1, 180); L.put("ph_hand_truck", 1.5, 0.2, 60)
    L.put("ph_flood_lights", 3.4, -0.8, 230, jit=0); L.row("ph_cardboard_box_01", -0.6, 0.4, 3, 0.5, 15, jit=25); L.stack("ph_cardboard_box_01", 0.6, 0.6, 2, 20, jit=20)
    L.row("ph_barrel_03", 2.6, 1.0, 2, 0.65, 0, jit=30); L.litter(YARD_LITTER, -3.4, 0.2, 3.4, 1.6, 6)
    B["yard_loading_area"] = ("yard", "Loading area (large)", "Crate and tote stacks three high, hand trucks, a barrier line, a floodlight and loose boxes: standing cover behind the stacks, crouch cover at the barriers.", L.m)
    L = Lay(31)   # town market row: four stalls (tables) with baskets, crates and barrels
    for k, (tbl, x) in enumerate((("ph_painted_wooden_table", -3.4), ("ph_woodentable_01", -0.9), ("ph_wooden_table_02", 1.4), ("ph_gallinera_table", 3.3))):
        h = L.put(tbl, x, 0, 0, jit=4)
        L.put(L.r.choice(["ph_wicker_basket_01", "ph_wicker_basket_02", "ph_wooden_bowl_01"]), x - 0.3, 0.05, 0, h); L.put(L.r.choice(["ph_wicker_basket_02", "ph_brass_pot_01", "ph_ceramic_pot"]), x + 0.35, 0.0, 0, h)
        L.stack("ph_wooden_crate_01", x - 0.4, 0.95, 1 + k % 2, 10); L.put(L.r.choice(["ph_wine_barrel_01", "ph_wooden_bucket_02", "ph_plastic_crate_01"]), x + 0.6, 1.0, 0)
    L.put("ph_standing_chalkboard_01", -4.6, 0.7, 30); L.put("ph_planter_box_02", 0.3, 1.8, 0); L.put("ph_wooden_barrels_01", 4.6, 1.4, 90, jit=4)
    L.litter(TOWN_LITTER, -4.6, 1.4, 4.0, 2.4, 5)
    B["town_market_row"] = ("town", "Market row (large)", "Four stalls of tables with baskets, pots and crates, barrels and a chalkboard: crouch cover behind the tables and crates. (No awning model exists yet.)", L.m)
    L = Lay(32)   # town cafe terrace: tables and chairs, some overturned, planters, a signboard
    for k, (x, z) in enumerate(((-3.0, -0.8), (-1.0, -0.6), (1.0, -0.9), (3.0, -0.7))): L.put("ph_outdoor_table_chair_set_01", x, z, 90 + L.j(20))
    L.put("ph_round_wooden_table_02", -2.0, 1.0, 0); L.put("ph_round_wooden_table_02", 2.0, 1.1, 0)
    L.lie("ph_plastic_monobloc_chair_01", -1.2, 1.4, 40); L.put("ph_plastic_monobloc_chair_01", -2.6, 1.6, 200); L.put("ph_plastic_monobloc_chair_01", 1.2, 1.8, 160); L.lie("ph_plastic_monobloc_chair_01", 2.8, 1.9, 300)
    L.row("ph_planter_box_01", -3.6, -2.0, 4, 2.4, 0, jit=2); L.put("ph_standing_chalkboard_01", 4.2, 0.6, 250); L.put("ph_potted_plant_02", 4.0, -1.8, 0)
    L.put("ph_metal_trash_can", -4.2, 0.9, 0); L.litter(TOWN_LITTER, -4.0, 0.2, 4.0, 2.4, 5)
    B["town_cafe_terrace_large"] = ("town", "Cafe terrace (large)", "Four table-and-chair sets and two round tables, chairs knocked over, planters along the kerb, a signboard: crouch cover behind the planters, little else.", L.m)
    L = Lay(33)   # town barricade across a street: barriers, furniture, drums, tyres, bags
    L.row("ph_concrete_road_barrier", -4.2, 0.0, 3, 1.6, 0, jit=6); L.lie("ph_painted_wooden_table", 0.9, 0.1, 0); L.lie("ph_gothiccabinet_01", 2.6, -0.1, 0)
    L.put("ph_sofa_01", 4.3, 0.2, 10, 0, (-60, 0), jit=4); L.lie("ph_metal_trash_can", -0.4, 0.9, 30); L.row("ph_barrel_01", 0.0, -0.8, 3, 0.62, 0, jit=40)
    L.stack("ph_old_tyre", -4.6, -1.0, 4, 0, jit=25, tilt=(90, 0)); L.row("ph_cement_bag", -3.6, -0.9, 4, 0.55, 0, jit=25); L.row("ph_trashbag", 1.8, 0.9, 3, 0.7, 0, jit=40)
    L.put("ph_wooden_crate_02", 3.6, -0.9, 80); L.lie("ph_plastic_monobloc_chair_01", 5.0, 1.0, 70); L.litter(TOWN_LITTER + ["ph_old_tyre"], -4.6, 1.0, 5.0, 2.2, 6)
    B["town_barricade"] = ("town", "Street barricade (large)", "A street-wide barricade of barriers, a tipped table, cabinet and sofa, drums, tyres and sandbag-like cement sacks: standing and crouch cover along its length.", L.m)
    return B


PADS = {"pad_concrete_12": ("Concrete apron 12 m", "concrete", "concrete_floor", [0.5, 0.49, 0.47]), "pad_dirt_12": ("Packed dirt 12 m", "plain", "dirt_compacted", [0.42, 0.34, 0.25])}


def write_pads():
    for pid, (name, pat, tex, alb) in PADS.items():
        a = {"id": pid, "version": 1, "name": name, "category": "Ground pads", "description": name + ": a flat 12 x 12 m ground surface (4 cm) for yards and compounds, and the hard ground under prop captures.",
             "footprint": [12, 12], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5, "noAutoPanels": True,
             "materials": [{"name": "ground", "albedo": alb, "roughness": 0.9, "pattern": pat, "metallic": 0, "grain": 1, "texture": tex}],
             "parts": [{"t": "b", "c": [0, 0.02, 0], "s": [12, 0.04, 12], "m": "ground"}], "furniture": [], "interactables": [], "panels": [],
             "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []}}
        open(os.path.join(ROOT, "content", "assets", pid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(a, separators=(",", ":")))


def write_doc(out):
    T = {"desert": "Desert compound (Sun Fort, sd_desert)", "yard": "Container yard (night)", "town": "Town quarter (Normandy)"}
    L = ["# Prop clusters for the Search and Destroy maps (PR4)", "",
         "Ready-made groups of the imported Poly Haven props for dressing the three Search and Destroy maps, written by `tools/props/clusters.py` (run `python -I tools/props/clusters.py`) from `tools/props/ph_props.json`.", "",
         "**Two tiers.** *Small* clusters (1.5-4 m, 3-9 objects) for doorsteps and corners; *large* clusters (6-10 m, 22-43 objects, stacks up to 2.4 m) to dress a bomb site or a yard: stacks two to four high, rows, pieces leaning on each other or lying on their side (attached-model `tilt`), seeded jitter and ground clutter so nothing sits on a grid.", "",
         "**Two ways to use them.**",
         "- **One placeable asset** (`cl_*`, editor categories *Clusters: Desert compound / Container yard / Town quarter*): the members' baked models attached at their offsets (with tilt), collider boxes turned into place, and nav cover points in front of and behind every member a body can hide behind (marked *stand* when it is at least 1.3 m tall, *crouch* from 0.8 m). Turn the whole asset with the instance yaw; nav carves it.",
         "- **Member list** in `tools/props/clusters.json` (for mapgen): `members[]` with `asset`, `pos` [x, y, z] (y = standing height), `yaw`, optional `tilt` [pitch, roll] and `cover` (stand / crouch / null); plus `footprint` [x, z] m, `height`, `cover`, `count`. Members with a tilt only keep it inside the cluster asset (map instances have a yaw only), so place the large clusters as their asset. Clusters marked *prefab* mix in-house pieces (sandbag walls, the guard tower, cable reels, the farm cart, brick rubble) and exist only as member lists.", "",
         "Member list to instances: world = cluster position + R(cluster yaw) * pos, member yaw + cluster yaw (yaw turns +x toward -z, as `yawQuat`).", "",
         "**Ground pads** for yards and compounds (and the hard ground under these captures): `pad_concrete_12`, `pad_dirt_12` (12 x 12 m, 4 cm).", "",
         "![large clusters on concrete](prop-clusters-large.jpg)", "", "![small clusters](prop-clusters.jpg)", ""]
    for th in ("desert", "yard", "town"):
        L += ["## " + T[th], "", "| Cluster | Tier | Use | Asset | Footprint (m) | Height | Objects | Stand / crouch cover pieces |", "|---|---|---|---|---|---|---|---|"]
        for c in out:
            if c["theme"] != th: continue
            st = sum(1 for m in c["members"] if m.get("cover") == "stand"); cr = sum(1 for m in c["members"] if m.get("cover") == "crouch")
            fp = "%.1f x %.1f" % tuple(c["footprint"]) if c["footprint"] else "-"
            L.append("| %s | %s | %s | %s | %s | %.1f m | %d | %d / %d |" % (c["name"], "large" if c["count"] >= 15 else "small", c["purpose"], "`%s`" % c["asset"] if c["asset"] else "prefab", fp, c["height"], c["count"], st, cr))
        L.append("")
    L += ["## Adding a cluster", "", "Small: an entry in `C` (members: asset id, x, z, yaw, y[, (pitch, roll)]). Large: a function in `big_clusters()` built with `Lay` (put, stack, row, lean, lie, litter). A cluster asset reuses the members' existing bakes (`content/assets/models/<dir>`): its `models` entries carry no `bake` block, so `--import-weapon-models` never re-bakes or claims those folders."]
    open(os.path.join(ROOT, "docs", "prop-clusters.md"), "w", encoding="utf-8").write(chr(10).join(L) + chr(10))


def main():
    out = []
    write_pads()
    for name, (theme, title, purpose, members) in list(C.items()) + list(big_clusters().items()):
        for m in members:
            assert m[0] in PH or os.path.exists(os.path.join(ROOT, "content", "assets", m[0] + ".json")) or os.path.exists(os.path.join(ROOT, "content", "assets", "models", m[0], "model.json")), name + ": no asset " + m[0]
        e, a = build(name, theme, title, purpose, members); out.append(e)
        if a: open(os.path.join(ROOT, "content", "assets", a["id"] + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(a, separators=(",", ":")))
        print("%-26s %-6s %s %s h %.2f cover %s n %d" % (name, theme, "asset " + e["asset"] if e["asset"] else "prefab only", e["footprint"] or "", e["height"], e["cover"], e["count"]))
    json.dump({"_doc": "PR4 prop clusters (tools/props/clusters.py, docs/prop-clusters.md): members' pos [x, y, z] and yaw (degrees) relative to the cluster origin; 'asset' places the whole cluster as one prop (models + colliders + cover), null = place the members one by one.",
               "clusters": out}, open(os.path.join(HERE, "clusters.json"), "w", encoding="utf-8"), indent=1)
    write_doc(out)


if __name__ == "__main__":
    main()
