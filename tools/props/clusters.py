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
   ("ph_old_tyre", 0, 0, 0, 0), ("ph_old_tyre", 0.05, 0.03, 10, 0.17), ("ph_old_tyre", 0.6, 0.3, 80, 0), ("ph_rusted_wheel_rim_01", -0.55, 0.25, 20, 0),
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


def build(name, theme, title, purpose, members):
    model_only = all(m[0].startswith("ph_") and m[0] in PH for m in members)
    lo, hi, parts, models = [1e9, 1e9, 1e9], [-1e9, -1e9, -1e9], [], []
    for aid, x, z, yaw, y in members:
        p = PH.get(aid)
        if not p: continue
        models.append({"dir": aid, "pos": [x, y, z], "yaw": yaw, "lodDist": p.get("lodDist", 15)})
        for cx, cy, cz, sx, sy, sz in p["colliders"] or [[0, p["size"][1] / 2, 0] + p["size"]]:
            dx, dz = turn(cx, cz, yaw); c = [x + dx, y + cy, z + dz]
            parts.append({"t": "b", "c": [round(v, 3) for v in c], "s": [round(sx, 3), round(sy, 3), round(sz, 3)], "r": [0, yaw, 0], "m": "collider", "v": 0})
            a = math.radians(yaw); ex = abs(sx * math.cos(a)) / 2 + abs(sz * math.sin(a)) / 2; ez = abs(sx * math.sin(a)) / 2 + abs(sz * math.cos(a)) / 2
            for k, (cc, e) in enumerate(((c[0], ex), (c[1], sy / 2), (c[2], ez))):
                lo[k] = min(lo[k], cc - e); hi[k] = max(hi[k], cc + e)
    h = hi[1] if model_only else max([PH[m[0]]["size"][1] + m[4] for m in members if m[0] in PH] or [0])
    # cover from the members a body can hide behind (footprint >= 0.25 m2): a lamp post or a pole is no wall
    hc = max([PH[m[0]]["size"][1] + m[4] for m in members if m[0] in PH and PH[m[0]]["size"][0] * PH[m[0]]["size"][2] >= 0.25] or [0])
    cov = "high" if hc >= 1.3 else ("low" if hc >= 0.8 else "none")
    entry = {"id": name, "theme": theme, "name": title, "purpose": purpose, "asset": "cl_" + name if model_only else None, "cover": cov,
             "height": round(h, 2), "footprint": [round(hi[0] - lo[0], 2), round(hi[2] - lo[2], 2)] if model_only else None,
             "members": [{"asset": m[0], "pos": [m[1], m[4], m[2]], "yaw": m[3]} for m in members]}
    if not model_only: return entry, None
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2   # cover points just outside each side, facing out
    cover = [] if cov == "none" else [{"p": [round(px, 2), 0, round(pz, 2)], "yaw": yw, "height": cov, "peek": ["over", "left", "right"]} for px, pz, yw in
             ((cx, hi[2] + 0.45, 0), (cx, lo[2] - 0.45, 180), (hi[0] + 0.45, cz, 90), (lo[0] - 0.45, cz, 270))]
    credits = sorted({PH[m[0]]["credit"] for m in members})
    asset = {"id": "cl_" + name, "version": 1, "name": title + " (cluster)", "category": "Clusters: " + {"desert": "Desert compound", "yard": "Container yard", "town": "Town quarter"}[theme],
             "description": purpose + " Poly Haven models (CC0): " + "; ".join(c.split(", Poly Haven")[0] for c in credits) + ".",
             "footprint": entry["footprint"], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5, "noAutoPanels": True,
             "models": models, "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": cover},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1}],
             "parts": parts, "furniture": [], "interactables": [], "panels": []}
    return entry, asset


def main():
    out = []
    for name, (theme, title, purpose, members) in C.items():
        for m in members:
            assert m[0] in PH or os.path.exists(os.path.join(ROOT, "content", "assets", m[0] + ".json")), name + ": no asset " + m[0]
        e, a = build(name, theme, title, purpose, members); out.append(e)
        if a: open(os.path.join(ROOT, "content", "assets", a["id"] + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(a, separators=(",", ":")))
        print("%-26s %-6s %s %s h %.2f cover %s" % (name, theme, "asset " + e["asset"] if e["asset"] else "prefab only", e["footprint"] or "", e["height"], e["cover"]))
    json.dump({"_doc": "PR4 prop clusters (tools/props/clusters.py, docs/prop-clusters.md): members' pos [x, y, z] and yaw (degrees) relative to the cluster origin; 'asset' places the whole cluster as one prop (models + colliders + cover), null = place the members one by one.",
               "clusters": out}, open(os.path.join(HERE, "clusters.json"), "w", encoding="utf-8"), indent=1)


if __name__ == "__main__":
    main()
