"""PR10: post-apocalyptic buildings and props from the user's batch.

    python -I tools/props/pr10_assets.py convert <incoming dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per asset
    python -I tools/props/pr10_assets.py write <incoming dir> <glb dir>     # asset JSONs, line type, credits rows, .gitignore
then bake: NormandyMapmaker --import-weapon-models <glb dir> content.

Every building is enterable the PR8 way (pr8_openings.py): door panels / boards in doorways deleted and game doors fitted,
window bays cut through the collision boxes, walkable floor colliders. The floors are colliders only, so the
auto-furnisher leaves each building to its five authored post-apocalyptic pieces ("companions", removable like any
placed piece). Measurements: E:/nmk/pr10scripts walls.py / zscan.py / down.py / inside.py (raycasts on the source),
source units, z up; a building's "scale" applies to its boxes too.

Licences (the coordinator, 2026-11-19): a GLB's own metadata (CC-BY-4.0, title / author / source) -> committed with a
credits row; the Sketchfab Standard shop -> local-only; the warehouse and box packs -> committed, "author not stated;
free to use per the project owner"; the rar house (no metadata) -> local-only, to be confirmed.
"""
import json, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pr8_openings

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
POSTAPOC, ALL = ["postapoc"], ["wwii", "postapoc", "modern", "desert"]
FREE = "author not stated; free to use per the project owner"


def pbr(folder, stem):   # the rar pack's per-material folders: <Mat>/LittleBuilding_LittleBuilding_<Mat>_<map>
    f = "%s/LittleBuilding_LittleBuilding_%s_" % (folder, stem)
    return {"Base Color": f + "BaseColor.jpg", "Normal": f + "Normal.jpg", "Roughness": f + "Rough.png"}


def wmap(base, rough=None, normal=None, metal=None):   # the warehouse's maps by their names on disk (the FBX's own names differ)
    return {k: v for k, v in (("Base Color", base), ("Roughness", rough), ("Normal", normal), ("Metallic", metal)) if v}


WH_MATS = {
    "Walkway": wmap("Walkway_Diffuse.jpg", "Walkway_Roughness.jpg", "Walkway_Normal.jpg"),
    "Rubber": wmap("synth-rubber-albedo.jpg", "synth-rubber-roughness.jpg", "synth-rubber-normal.jpg"),
    "factory_wall": wmap("Substance_graph_basecolor.jpg", None, "Substance_graph_normal.jpg"),
    "rusty metal plate corrugated": wmap("rusty_metal_plate_corrugated_albedo.jpg", "rusty_metal_plate_corrugated_roughness.jpg", "rusty_metal_plate_corrugated_normal.jpg", "rusty_metal_plate_corrugated_metallic.jpg"),
    "WarehouseMainConcrete": wmap("viyjde3_2K_Albedo.jpg", "viyjde3_2K_Roughness.jpg", "viyjde3_2K_Normal.jpg"),
    "WarehouseWindow": wmap("windowpaint_WarehouseWindow_BaseColor.jpg", "windowpaint_WarehouseWindow_Roughness.jpg"),
    "Office_Floor": wmap("office-carpet-fabric_albedo.jpg", "office-carpet-fabric_roughness.jpg", "office-carpet-fabric_normal-ogl.jpg"),
    "MetalRusted": wmap("Metal_iron_4_baseColor.jpeg", "Metal_iron_4_roughness.jpeg", "Metal_iron_4_normal.jpeg", "Metal_iron_4_metallic.jpeg"),
    "MetalWalkway": wmap("Substance_graph_basecolor.jpg", None, "Substance_graph_normal.jpg"),
    "GateRail": wmap("gatepaint_GateRail_BaseColor.jpg", "gatepaint_GateRail_Roughness.jpg"),
    "DoorMetal": wmap("DoorMetal_BaseColor.jpg", "DoorMetal_Roughness.jpg"),
    "OfficeDoorMaterial": wmap("officedoorpaint_OfficeDoorMaterial_BaseCol.jpg", "officedoorpaint_OfficeDoorMaterial_Roughne.jpg", "officedoorpaint_OfficeDoorMaterial_Normal.jpg"),
    "OfficeWindow": wmap("officewindowpaint_OfficeWindow_BaseColor.jpg", "officewindowpaint_OfficeWindow_Roughness.jpg"),
}


# buildings: walls / roof / floors / openings as boxes in the source ([x0, x1, y0, y1, z0, z1]); the door front is -y.
# "walls" are every static collider (floor slabs too); "floor" is the main floor (the threshold height of the doors).
BUILDINGS = {
    "pa_concrete_outbuilding": dict(
        name="Abandoned concrete outbuilding", lic="local", pack="abounded-house.zip (Sketchfab.rar: LittleBuilding.fbx)",
        src="abounded-house/source/rar_x/Sketchfab/LittleBuilding.fbx", texfolder="abounded-house/source/rar_x/Sketchfab",
        matmap={"LittleBuilding_" + m: pbr(m, m) for m in ("Concrete", "Door", "Metal", "Roof", "Wood")},
        delete_mats=["LittleBuilding_Door"], maxTex=2048,
        desc="A one-room concrete outbuilding with a corrugated roof, graffiti and a boarded back window: enterable through a steel door (E).",
        walls=[[-4.19, 4.19, -2.17, -1.86, 0.0, 3.13], [-4.19, 4.19, 1.96, 2.27, 0.0, 3.13],          # front, back
               [-4.19, -3.88, -1.86, 1.96, 0.0, 3.13], [3.88, 4.19, -1.86, 1.96, 0.0, 3.13],           # ends
               [-4.54, 4.54, -2.65, 2.65, 3.06, 3.6]],                                                   # roof slab
        doors={"door": ("door", "door_steel", 1, [-2.79, -1.59, -2.17, -2.11, 0.2, 2.46])},
        holes={"window_back": [-0.39, 2.61, 1.9, 2.33, 1.05, 2.4]},                                       # boarded bay: the boards stay
        floor=[-3.88, 3.88, -2.17, 1.96, 0.0, 0.2],
        furnish=[("ph_worn_metal_rack", 3.45, 0.2, 90), ("ph_old_bed_frame", -3.38, 0.6, 0), ("mod_debris_stove", 2.9, -1.3, 0),
                 ("furn_chair_soviet", 0.3, -0.6, 30), ("furn_debris_plaster", 1.3, 0.9, 0)]),
    # a single-storey wing (gable roof) and a two-storey block in front of it, joined by an inner doorway; the block's
    # upper floor has no stair (closed off). Doors 1.8 m in the source: x1.15 (doors 2.05 m, ceiling 3.05 m)
    "pa_military_house": dict(
        name="Abandoned military house", lic="glb", src="abounded_military_house/abounded_military_house.glb", scale=1.15, maxTex=2048,
        # the boards across the wing's doorway and the five ground-floor windows go (solid panels, black from inside): open bays
        delete_in=[["BigBuilding_Wood", -4.8, -4.0, 2.1, 3.6, 0.3, 2.3], ["BigBuilding_Wood", -3.0, -0.9, -0.3, 0.6, 1.0, 2.3],
                   ["BigBuilding_Wood", -4.8, -3.9, 0.55, 1.65, 1.0, 2.3], ["BigBuilding_Wood", -4.8, -3.9, 4.05, 5.15, 1.0, 2.3],
                   ["BigBuilding_Wood", -0.4, 0.7, -2.35, -0.65, 1.0, 2.3], ["BigBuilding_Wood", 3.0, 4.0, -2.35, -0.65, 1.0, 2.3]],                                 # the X-braced boards across the wing's doorway
        desc="A derelict two-part military house: a gabled wing and a two-storey block, broken-out windows below and boarded ones above, rusted roof; enterable through two doors (E), the wing and the block joined inside.",
        walls=[[-4.44, 3.93, 5.33, 5.64, 0.0, 2.75], [-4.44, -4.13, 0.05, 5.64, 0.0, 2.75], [3.62, 3.93, 0.05, 5.64, 0.0, 2.75],   # wing back, ends
               [-4.44, 3.93, 0.05, 0.36, 0.0, 2.75], [-4.44, 3.93, 0.05, 5.64, 2.66, 3.0],                                      # wing front, ceiling
               [0.07, 3.62, -3.13, -2.82, 0.0, 5.6], [0.07, 0.38, -3.13, 0.05, 0.0, 5.6], [3.31, 3.62, -3.13, 0.05, 0.0, 5.6],   # block front, sides
               [0.07, 3.62, -0.27, 0.12, 0.0, 5.6], [0.07, 3.62, -3.13, 0.12, 2.66, 2.87],                                      # block back (the shared wall), ceiling
               [0.38, 3.31, -3.13, 0.36, 0.0, 0.36]],                                                                           # the block's floor (to the doors)
        doors={"door_block": ("door", "door_steel", 1, [1.2, 2.3, -3.13, -3.07, 0.36, 2.15]),
               "door_wing": ("door", "door_wood", 1, [-4.44, -4.38, 2.15, 3.55, 0.36, 2.15])},
        double_mats=[],
        holes={"inner": [1.15, 2.25, -0.35, 0.45, 0.36, 2.15], "win_front": [-2.85, -1.05, 0.0, 0.42, 1.1, 2.15],
               "win_end_a": [-4.5, -4.07, 0.65, 1.55, 1.1, 2.15], "win_end_b": [-4.5, -4.07, 4.15, 5.05, 1.1, 2.15],
               "win_block_l": [0.0, 0.45, -2.25, -0.75, 1.1, 2.15], "win_block_r": [3.25, 3.7, -2.25, -0.75, 1.1, 2.15]},
        floor=[-4.44, 3.62, 0.36, 5.33, 0.0, 0.36],
        furnish=[("ph_worn_metal_rack", -1.0, 5.0, 0), ("ph_old_military_crate", -3.55, 4.75, 15), ("mod_barrel_steel_rust", 3.15, 4.85, 0),
                 ("pa_crate_slatted", 2.8, -2.3, 10), ("mod_debris_papers_scattered", 0.0, 2.6, 0)]),
    # an exterior shell (one-sided brick box; the shopfront glass and doors were faces of a fake interior box,
    # "store_building_inside", facing out): that box deleted (broken-out shopfront bays, open doorways), the brick
    # given inner faces, a concrete floor added. Its ground is z -1 (a foundation below the file's zero).
    "pa_abandoned_shop": dict(
        name="Abandoned shop", lic="glb", src="game-ready_an_abandoned_shop/game-ready_an_abandoned_shop.glb", maxTex=2048,
        delete_mats=["store_building_inside"], double_mats=["store_building"],
        add_boxes=[{"box": [-7.95, 7.95, 4.0, 16.0, -1.0, -0.97], "tex": "concrete_stained", "tile": 3.0}],
        desc="A gutted brick corner shop: broken-out shopfront, three side doors, an empty sales floor under a flat roof; enterable (E on the doors).",
        walls=[[-8.0, 8.0, 4.0, 4.25, -1.0, 6.0], [-8.0, 8.0, 15.75, 16.0, -1.0, 6.0], [-8.0, -7.75, 4.0, 16.0, -1.0, 6.0], [7.75, 8.0, 4.0, 16.0, -1.0, 6.0],
               [-8.0, 8.0, 4.0, 16.0, 5.38, 5.7]],
        doors={"door_front": ("door", "door_steel", 1, [-0.7, 0.7, 4.0, 4.06, -0.97, 1.45]),
               "door_west_a": ("door", "door_steel", 1, [-8.0, -7.94, 7.05, 8.05, -0.97, 1.45]),
               "door_west_b": ("door", "door_steel", 1, [-8.0, -7.94, 11.95, 12.95, -0.97, 1.45]),
               "door_east": ("door", "door_steel", 1, [7.94, 8.0, 7.05, 8.05, -0.97, 1.45])},
        holes={"bay_a": [-7.85, -3.7, 3.9, 4.35, -0.97, 1.45], "bay_b": [-3.6, -0.8, 3.9, 4.35, -0.97, 1.45],
               "bay_c": [0.8, 3.6, 3.9, 4.35, -0.97, 1.45], "bay_d": [3.7, 7.85, 3.9, 4.35, -0.97, 1.45]},
        floor=[-7.95, 7.95, 4.0, 16.0, -1.0, -0.97],
        furnish=[("ph_worn_metal_rack", -4.0, 15.35, 0), ("ph_wooden_bookshelf_worn", 3.5, 15.4, 0), ("furn_debris_glass", -5.2, 5.0, 0),
                 ("mod_debris_papers_scattered", -1.0, 9.0, 20), ("ph_old_tyre", 3.0, 8.5, 0)]),
    # a photo-scan of a caboose with its interior (plank floor 0.64 m up, blue board walls, beams): turned to run along x
    # (rail_rot/rail.glb, E:/nmk/pr10scripts/rotate.py: PCA of the roof, -139.1 deg); its open side doorway gets a game
    # door, the scanned ballast ledge outside it a step collider (0.33 + 0.31 m steps), the windows are the scan's holes
    "pa_rail_caboose": dict(
        name="Abandoned railroad caboose", lic="glb", src="rail_rot/rail.glb", meta_src="union_pacific_railroad_car_rhyolite/union_pacific_railroad_car_rhyolite.glb",
        maxTex=4096, cut_below=0.0, decimate=0.5,
        desc="A weathered wooden Union Pacific caboose on a patch of ballast, its blue-painted plank interior intact: enterable through the side door (E).",
        walls=[[-4.6, 4.15, -1.24, -1.12, 0.0, 2.8], [-4.6, 4.15, 1.29, 1.41, 0.0, 2.8], [-4.6, -4.46, -1.24, 1.41, 0.0, 2.8], [3.96, 4.15, -1.24, 1.41, 0.0, 2.8],
               [-4.6, 4.15, -1.3, 1.45, 2.78, 3.0], [0.1, 1.0, -1.75, -1.24, 0.0, 0.33]],
        doors={"door_side": ("door", "door_wood", 1, [0.15, 0.95, -1.24, -1.18, 0.64, 2.55])},
        holes={"win_a": [-3.55, -3.05, 1.2, 1.5, 1.5, 2.05], "win_b": [-2.05, -1.55, 1.2, 1.5, 1.5, 2.05]},
        floor=[-4.46, 3.96, -1.24, 1.29, 0.0, 0.64],
        furnish=[("mod_debris_stove", 2.4, 0.75, 0), ("pa_chest_wooden", -3.75, 0.75, 0), ("furn_chair_soviet", -1.0, 0.7, 200),
                 ("mod_debris_papers_pile", -2.2, -0.5, 30), ("pa_fruit_crate_mid", 3.35, -0.6, 90)]),
    # a game-ready warehouse (T plan, 41 x 48 m, trusses, walkways, a mezzanine office): colliders from its own walls and
    # floors (pr6_split auto_colliders), its closed door panels and painted window panes removed (open doorways: the
    # walls have real openings behind them; broken-out windows); docks at 1 m outside, the halls at ground level
    "pa_warehouse": dict(
        name="Abandoned warehouse", lic="free", pack="abandoned-warehouse.zip", src="abandoned-warehouse/source/warehouseupload2.fbx",
        texfolder="abandoned-warehouse/textures", matmap=WH_MATS, maxTex=2048, decimate=0.5,
        delete_mats=["DoorMetal", "OfficeDoorMaterial", "WarehouseWindow", "OfficeWindow"],
        auto_colliders={"exclude": ["GateRail", "Rubber"], "cell": 0.5, "min_wall": 4.0, "min_rect": 1.5, "min_floor": 1.0, "min_rect_floor": 0.25},   # (1112 boxes at 0.25 m stalled the asset load)
        desc="A derelict T-plan steel warehouse: two halls under a broken roof, trusses, walkways and a mezzanine office, loading docks; enterable through its open doorways and loading bay.",
        walls=[], doors={}, holes={}, floor=[-5.0, 5.0, 5.0, 10.0, -0.12, 0.0],
        furnish=[("pa_crate_slatted", -18.0, -18.0, 20), ("mod_barrel_steel_rust", -22.0, -10.0, 0), ("pa_chest_wooden", 3.0, -14.0, 90),
                 ("pa_fruit_crate_high", -17.2, -16.9, 35), ("mod_debris_papers_scattered", 1.0, 8.0, 0)]),
    # YN14R's destroyed log house (part 1; its "part2" is a separate house, they do not join): x0.6 (the storey 4.2 units:
    # doors 2 m, rooms 2.5 m). Closed all round on the ground storey (scanned at 0.75 / 1.5 / 2.3 m: walls but for a
    # front doorway, a side doorway and windows); colliders from its own geometry; the upper floor has no stair
    # YN14R's destroyed home (part 2; a different house from part 1): an L-plan stone-and-log house on a 0.75-unit plinth.
    # x0.62 (storey 3.9 units: 2.4 m). Its ground plate and the dead trees grown into it removed (the batch has its own
    # dead trees); colliders from its own geometry; a concrete step at the side door (the plinth is 0.47 m)
    "pa_stone_house_ruin": dict(
        name="Ruined stone-and-log house", lic="glb", src="old_destroyed_home_part2/old_destroyed_home_part2.glb", scale=0.62, maxTex=2048,
        delete_mats=["Plane__0", "bark"],
        add_boxes=[{"box": [9.14, 10.1, -8.7, -7.0, 0.0, 0.4], "tex": "concrete_aged", "tile": 2.0}],
        auto_colliders={"cell": 0.5, "min_wall": 2.0, "min_rect": 0.75, "min_floor": 0.5, "min_rect_floor": 0.25, "exclude": ["glass"]},
        desc="A wrecked L-plan house of stone and logs on a high plinth: a rusted iron roof, broken windows and a collapsing upper floor; enterable by the step at its side door.",
        walls=[], doors={}, holes={}, floor=[-4.0, -3.0, -1.0, 0.0, 0.7, 0.75],
        furnish=[("ph_old_bed_frame", -4.0, -1.0, 90), ("mod_barrel_steel_rust", 5.0, -3.0, 0), ("furn_debris_glass", 0.0, 0.0, 0),
                 ("pa_fruit_crate_low", 2.0, 1.0, 25), ("furn_chair_soviet", -1.0, -2.0, 140)]),
    # YN14R's destroyed building (part 1): a long concrete-and-plank shed, x0.6, its front torn out (x -6.6..6.7) and its
    # back missing beyond x -3.9: closed with weathered-plank walls to the eaves (a doorway left in the front); its
    # ground plate removed (the terrain is the floor); colliders from its own geometry and the added walls
    "pa_shed_ruin_long": dict(
        name="Ruined long shed", lic="glb", src="old_destroyed_building_part_1/old_destroyed_building_part_1.glb", scale=0.6, maxTex=2048,
        delete_mats=["Plane__0"],
        cut_below=-0.02,   # debris modelled down to -2.5 under its ground plate
        add_boxes=[{"box": [-6.6, 1.9, -7.08, -6.88, 0.0, 4.8], "tex": "boards_weathered", "tile": 2.5},
                   {"box": [3.6, 6.75, -7.08, -6.88, 0.0, 4.8], "tex": "boards_weathered", "tile": 2.5},
                   {"box": [1.9, 3.6, -7.08, -6.88, 3.4, 4.8], "tex": "boards_weathered", "tile": 2.5},
                   {"box": [-3.95, 13.55, 6.22, 6.42, 0.0, 4.8], "tex": "boards_weathered", "tile": 2.5}],
        auto_colliders={"cell": 0.5, "min_wall": 2.0, "min_rect": 1.2, "min_floor": 0.5, "min_rect_floor": 0.25},
        desc="A long, half-collapsed shed of concrete and weathered planks under a holed corrugated roof, its torn-out front and back boarded up again: enterable through the front doorway.",
        walls=[], doors={}, holes={}, floor=[-4.0, -3.0, -1.0, 0.0, -0.05, 0.0],
        furnish=[("pa_crate_slatted", -10.0, 4.0, 15), ("mod_barrel_steel_rust", -11.5, -5.0, 0), ("pa_chest_wooden", 8.0, 5.0, 180),
                 ("ph_old_tyre", 4.0, -3.0, 0), ("pa_fruit_crate_high", -8.0, 4.6, 70)]),
    "pa_log_house_ruin": dict(
        name="Ruined log house", lic="glb", src="old_destroyed_home_part_1/old_destroyed_home_part_1.glb", scale=0.6, maxTex=2048,
        auto_colliders={"cell": 0.5, "min_wall": 2.0, "min_rect": 0.75, "min_floor": 0.5, "min_rect_floor": 0.25},
        desc="A collapsing two-storey log house with a lean-to: plank and log walls, broken windows, a holed roof and an unreachable upper floor; enterable through its open doorways.",
        walls=[], doors={}, holes={}, floor=[-4.0, -3.0, -1.0, 0.0, 0.2, 0.25],
        furnish=[("ph_old_bed_frame", -6.5, 1.5, 0), ("furn_chair_soviet", -2.5, -0.5, 30), ("mod_debris_stove", 0.6, 3.1, 0),
                 ("furn_debris_plaster", -4.0, -2.0, 0), ("ph_wooden_bookshelf_worn", -3.0, 3.45, 180)]),
}

# props: piece = pr6_split options; group = the palette group; extra = asset JSON fields
PROPS = {
    "pa_crate_slatted": dict(name="Slatted crate", lic="free", pack="box.zip (Cinema 4D export)", src="box/source/box.zip_x/box/box.fbx",
                             texfolder="box/textures", matmap={"box_mat": {"Base Color": "wood97.jpeg", "Normal": "wood97_normal.jpeg"}},
                             piece={"height": 0.9, "colliders": "box"}, group="Small objects", maxTex=1024,
                             desc="A weathered slatted wooden crate with X-braced sides and a loose lid"),
    "pa_chest_wooden": dict(name="Wooden chest", lic="glb", src="chest_part2/chest_part2.glb", piece={"length": 1.0, "colliders": "box"},
                            group="Small objects", maxTex=1024, desc="A weathered plank chest with an iron hasp"),
    **{"pa_fruit_crate_" + k: dict(name="Fruit crate, " + n, lic="glb", src="wooden_box/wooden_box.glb", objects=[o], piece={"colliders": "box"},
                                   group="Small objects", maxTex=512, desc="An empty slatted fruit crate, " + n)
       for k, n, o in (("low", "shallow", "wooden_box3_wooden_box_mat_0"), ("mid", "medium", "wooden_box2_wooden_box_mat_0"), ("high", "deep", "wooden_box1_wooden_box_mat_0"))},
    # one bay of the old picket fence, post to post (posts 9 units apart): the line type repeats it
    "pa_fence_picket_bay": dict(name="Old picket fence, bay", lic="glb", src="old_wood_fence/old_wood_fence.glb",
                                objects=["Cube_1_3_wood_mat1_0", "Cube_2_2_wood_mat2_0", "Cylinder_4_2_wood_bark_mat_0", "Cylinder_4_2_wood_top_mat_0"],
                                piece={"keep_box": [-6.0, -4.9, -3.35, 5.66], "height": 1.6, "yaw": 90, "colliders": "box"}, group=None, category="Modern: Fences",
                                maxTex=1024, part={"k": 2}, nav="carve", desc="One 3.4 m bay of a weathered picket fence with its post (draw it as a line: Lasso, Line)"),
    **{tid: dict(name=n, lic="glb", src=f, objects=objs, piece={"height": h, "decimate": dec, "colliders": "trunk"}, group=None, category="Trees",
                 themes=ALL, maxTex=1024, nav="carve", lodDist=60, cullDist=900, desc="A dead, leafless tree, about %.0f m tall" % h)
       for tid, n, f, objs, h, dec in (
           ("pa_tree_dead_a", "Dead tree A", "dead_tree/dead_tree.glb", None, 10.0, 0.15),
           ("pa_tree_dead_b", "Dead tree B", "dead_tree_1/dead_tree (1).glb", None, 12.0, 0.35),
           ("pa_tree_dead_c", "Dead tree C", "dead_tree_2/dead_tree (2).glb", ["dead_tree_1_Mat.1_0", "dead_tree_1_Mat.1_0.001"], 9.0, 0.4),
           ("pa_tree_dead_d", "Dead tree D", "dead_tree_2/dead_tree (2).glb", ["dead_tree_2_Mat.1_0", "dead_tree_2_Mat.1_0.001"], 11.0, 0.35),
           ("pa_tree_dead_e", "Dead tree E", "dead_tree_3/dead_tree (3).glb", None, 10.0, 0.25))},
    "pa_tree_fallen": dict(name="Fallen tree", lic="glb", src="tree_part13/tree_part13.glb", objects=["Sweep_NURBS_17_bark17_0"], piece={"length": 11.0, "colliders": "parts"},
                           group=None, category="Trees", themes=ALL, maxTex=1024, nav="carve", lodDist=60, cullDist=900,
                           desc="A fallen tree on its side, roots in the air (the model's leaf cards left out: their cut-out did not carry over)"),
}
LINE = ("pa_fence_picket", "Old picket fence (model)", "pa_fence_picket_bay")


def glb_meta(p):
    with open(p, "rb") as f:
        f.read(12); n, _ = struct.unpack("<II", f.read(8)); return json.loads(f.read(n)).get("asset", {}).get("extras", {}) or {}


def licence(dl, E):
    """(committed, credit text, credits row cells: model, author, licence, source)"""
    if E["lic"] == "glb":
        x = glb_meta(os.path.join(dl, E.get("meta_src", E["src"])))   # a turned copy keeps no metadata: the original's
        title, author, lic, url = x.get("title", ""), x.get("author", ""), x.get("license", ""), x.get("source", "")
        committed = lic.startswith("CC-BY") or lic.startswith("CC0")
        return committed, "%s by %s, %s (%s)%s" % (title, author.split(" (")[0], lic.split(" (")[0], url, "" if committed else ": local only"), (title, author, lic, url)
    if E["lic"] == "free":
        return True, "%s supplied by the user; %s" % (E["pack"], FREE), (E["pack"], FREE, "free to use per the project owner", "-")
    return False, "%s supplied by the user; licence to be confirmed: local only" % E["pack"], (E["pack"], "to be confirmed", "to be confirmed", "-")


def mark(x, y, z=0.2):
    return [x - 0.01, x + 0.01, y - 0.01, y + 0.01, z, z + 0.01]


def run(job, glbs, key):
    jp = os.path.join(glbs, "_job_%s.json" % key); json.dump(job, open(jp, "w"))
    r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = [l for l in r.stdout.splitlines() if l.startswith("PR6")]
    print("\n".join(out) if out else key + " FAILED " + (r.stdout + r.stderr)[-1500:])


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True)
    for bid, B in BUILDINGS.items():
        if os.path.exists(os.path.join(glbs, bid + ".glb")): continue
        marks = {**{k: v[3] for k, v in B["doors"].items()}, **B["holes"], "floor": B["floor"],
                 **{"f%d" % i: mark(x, y, B["floor"][5]) for i, (_, x, y, _) in enumerate(B["furnish"])}}
        piece = {"id": bid, "objects": None, "boxes": B["walls"], "marks": marks, "delete_mats": B.get("delete_mats", []), "delete_in": B.get("delete_in", []), "double_mats": B.get("double_mats", []), "cut_below": B.get("cut_below"), "decimate": B.get("decimate"), "auto_colliders": B.get("auto_colliders"),
                 "add_boxes": [{**b, "tex": os.path.join(ROOT, "content", "textures", b["tex"])} for b in B.get("add_boxes", [])],
                 "scale": B.get("scale", 1.0), "yaw": B.get("yaw", 0)}
        job = {"src": os.path.join(dl, B["src"]), "out": glbs, "pieces": [piece]}
        if B.get("matmap"): job.update(texfolder=os.path.join(dl, B["texfolder"]), matmap=B["matmap"])
        run(job, glbs, bid)
    for pid, P in PROPS.items():
        if os.path.exists(os.path.join(glbs, pid + ".glb")): continue
        job = {"src": os.path.join(dl, P["src"]), "out": glbs, "pieces": [{"id": pid, "objects": P.get("objects"), **P["piece"]}]}
        if P.get("matmap"): job.update(texfolder=os.path.join(dl, P["texfolder"]), matmap=P["matmap"])
        run(job, glbs, pid)


def write(dl, glbs):
    gi_add, rows = [], []
    def common(aid, E, side, desc, model):
        committed, credit, cells = licence(dl, E)
        sx, sy, sz = side["size"]
        if not committed: gi_add.append("content/assets/models/%s/" % aid)
        rows.append("| `%s` | %s | %s | %s | %s | modified: %s | %s |" % ((aid,) + cells + (E.get("changes", "baked / textures resized"), "yes" if committed else "no (local-only)")))
        return committed, {"id": aid, "version": 1, "name": E["name"], "category": E.get("category", "Small buildings"), "themes": E.get("themes", POSTAPOC),
                           "description": desc + ("" if committed else " (local only: the committed game shows box stand-ins)"),
                           "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "noAutoPanels": True,
                           "model": {"pivot": "ground", "scale": 1, "jpgMaps": True, "lod1": 0.3, "src": aid + ".glb", "dir": aid, "maxTex": E["maxTex"], "credit": credit, **model},
                           "nav": {"effect": E.get("nav", "none"), "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
                           "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
                           "furniture": [], "interactables": [], "panels": []}
    for bid, B in BUILDINGS.items():
        side = json.load(open(os.path.join(glbs, bid + ".json"))); sx, sy, sz = side["size"]; m = side["marks"]
        B.setdefault("changes", "openings cut, colliders, baked / textures resized")
        committed, A = common(bid, B, side, "%s %.1f x %.1f m, %.1f m high." % (B["desc"], sx, sz, sy), {"lodDist": 40, "cullDist": 600})
        A["maxGrade"] = 2; A["nav"]["effect"] = "carve"
        if B.get("auto_colliders"): A["dressing"] = "none"   # foundation dressing stalls on hundreds of collider parts (foundation.cpp)
        flag = {"v": 0} if committed else {"mf": 1}
        A["parts"] = [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", **flag} for c in side["colliders"]]
        pr8_openings.apply(A, m, {k: v[:3] + v[4:] for k, v in B["doors"].items()}, holes=list(B["holes"]), floor_visible=False)
        A["companions"] = [{"asset": a, "pos": [round(m["f%d" % i][0], 3), round(m["f%d" % i][1] - 0.005, 3), round(m["f%d" % i][2], 3)], "yaw": yaw}
                           for i, (a, _, _, yaw) in enumerate(B["furnish"])]
        assert len(A["companions"]) <= 5
        A["nav"]["cover"] = [{"p": [round(x, 2), 0, round(s * (sz / 2 + 0.5), 2)], "yaw": 180 if s > 0 else 0, "height": "high", "peek": ["left", "right"]} for x in (-sx / 4, sx / 4) for s in (1, -1)]
        open(os.path.join(ROOT, "content", "assets", bid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        print("%-28s %.2f x %.2f x %.2f m  tris %d  parts %d  doors %d  holes %d  furnish %d  %s" % (bid, sx, sz, sy, side["tris"], len(A["parts"]), len(A["interactables"]), len(B["holes"]), len(A["companions"]), "committed" if committed else "local"))
    for pid, P in PROPS.items():
        side = json.load(open(os.path.join(glbs, pid + ".json"))); sx, sy, sz = side["size"]
        committed, A = common(pid, P, side, "%s, %.2f x %.2f x %.2f m." % (P["desc"], sx, sz, sy),
                              {"simplify": 1, "lodDist": P.get("lodDist", 15), "cullDist": P.get("cullDist", 300)})
        A["category"] = P.get("category", "Town clutter"); A["name"] = P["name"] + " (model)"; A["maxGrade"] = 5
        if P.get("group"): A["group"] = P["group"]
        flag = {"v": 0} if committed else {"mf": 1}
        cols = [c for c in (side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]]) if c[1] + c[4] / 2 <= P.get("col_max_top", 1e9)]   # the fallen tree: trunk and roots, not its leaf clumps
        A["parts"] = [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", **flag, **P.get("part", {})} for c in cols]
        if pid == LINE[2]:   # snaps end to end; drawn as a line (lasso Line mode) by the line type below
            A["snap"] = [{"p": [round(-sx / 2, 3), 0, 0], "dir": [-1, 0], "kind": LINE[0]}, {"p": [round(sx / 2, 3), 0, 0], "dir": [1, 0], "kind": LINE[0]}]
            bay = sx
        open(os.path.join(ROOT, "content", "assets", pid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        print("%-28s %.2f x %.2f x %.2f m  tris %d  colliders %d  %s" % (pid, sx, sz, sy, side["tris"], len(A["parts"]), "committed" if committed else "local"))
    # the fence as a line type (lasso Line mode): bays of the fence piece end to end
    lt_path = os.path.join(ROOT, "content", "settings", "line_types.json"); LT = json.load(open(lt_path, encoding="utf-8"))
    LT["types"] = [t for t in LT["types"] if t["id"] != LINE[0]] + [{"id": LINE[0], "name": LINE[1], "asset": "line_" + LINE[0], "builder": "bays", "spacing": round(bay, 3), "pieces": [[LINE[2], 1]], "params": {}}]
    open(lt_path, "w", encoding="utf-8", newline="").write(json.dumps(LT, indent=2, ensure_ascii=False) + "\n")
    L = {"id": "line_" + LINE[0], "version": 1, "name": LINE[1] + " (line)", "category": "Lines", "hidden": True, "themes": POSTAPOC,
         "description": "%s drawn as a line (lasso Line mode): bays of %s along the line; gates and gaps as spans." % (LINE[1], LINE[2]),
         "floors": 0, "enterable": False, "footprint": [1, 1], "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "line": {"type": LINE[0]},
         "materials": [], "parts": [], "furniture": [], "interactables": [], "panels": [], "nav": {"effect": "carve"}}
    open(os.path.join(ROOT, "content", "assets", "line_" + LINE[0] + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(L, separators=(",", ":")))
    gi = os.path.join(ROOT, ".gitignore"); g = open(gi, encoding="utf-8").read()
    keep = {"content/assets/models/%s/" % k for k in list(BUILDINGS) + list(PROPS)}
    lines = [l for l in g.split("\n") if l not in keep or l in gi_add]   # committed ones leave the ignore list
    g = "\n".join(lines); new = [l for l in gi_add if l not in g]
    if new: g = g.rstrip("\n") + "\n# PR10: local-only models\n" + "\n".join(new) + "\n"
    open(gi, "w", encoding="utf-8", newline="").write(g)
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- pr10_assets: written by tools/props/pr10_assets.py -->", "<!-- /pr10_assets -->"
    table = (a + "\nPR10: the user's post-apocalyptic batch (title, author, licence and source as each file's own metadata gives them).\n\n"
             "| Asset | Model | Author | Licence | Source | Changes | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n" + b)
    doc = doc.replace("\r\n", "\n")
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\n", nl))


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
