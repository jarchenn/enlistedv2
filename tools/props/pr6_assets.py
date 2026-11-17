"""PR6: the user's supplied asset packs (Downloads, Sketchfab and two loose zip packs) -> placeable game assets.

    python -I tools/props/pr6_assets.py convert <incoming dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per asset
    python -I tools/props/pr6_assets.py write <incoming dir> <glb dir>     # asset JSONs, line types, credits, .gitignore
then bake: NormandyMapmaker --import-weapon-models <glb dir> content (only these assets' sources are in that folder).

Licences come from each GLB's Sketchfab metadata (asset.extras). The two zip packs (air conditioners, billboards) carry no
licence or author: they are LOCAL-ONLY (bakes git-ignored, the committed game shows box stand-ins, "mf" parts).
Scale: checked against real sizes (sandbags 0.6 m long, compound wall 2.8 m, billboard 6.6 m wide, AC units 0.7-1.0 m);
a pack whose units were off is rescaled as a whole.
"""
import json, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"

FP = "Modern: Industrial"
FACADE = "Modern: Facade & Roof"
# source key -> file, loose texture, pack scale, licence override, pieces
# piece: (id, name, category, selection, options)
SRC = {
 "sandbag": ("sandbag_wall_-_24mb/sandbag_wall_-_24mb.glb", None, 0.62, None, [
   ("def_sandbag_bags_low", "Sandbag wall, low (model)", "Defences", {"objects": [("Cube_%s_Mat_0" % k if k else "Cube_Mat_0") for k in ["", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "23", "44", "45"]]}, {"decimate": 0.12, "colliders": "box", "sandbag": True}),
   ("def_sandbag_bags_high", "Sandbag wall, high (model)", "Defences", {"objects": [("Cube_%s_Mat_0" % k if k else "Cube_Mat_0") for k in ["", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "23", "44", "45"]], "stack": [[0.9, 180]]}, {"decimate": 0.12, "colliders": "box", "sandbag": True}),
   ("def_sandbag_single", "Sandbag (single, model)", "Defences", {"objects": ["Cube_16_Mat_0"]}, {"decimate": 0.2, "colliders": "box", "small": True}),
 ]),
 "cement": ("wall_cement_fence_wall_building/wall_cement_fence_wall_building.glb", None, 1.0, None, [
   ("mod_wall_cement_bay", "Cement fence wall, bay (model)", "Modern: Fences", {"chain": [["01_2_Mat_0", 0], ["02_Mat_0", 0]]}, {"colliders": "box", "line": "mod_wall_cement"}),
   ("mod_wall_cement_pillar", "Cement fence wall, pillar (model)", "Modern: Fences", {"objects": ["02_Mat_0"]}, {"colliders": "box"}),
 ]),
 "retain": ("fence_retaining_wall/fence_retaining_wall.glb", None, 1.0, None, [
   ("mod_wall_brick_rail_bay", "Brick wall with railing, bay (model)", "Modern: Fences", {"chain": [["01_3__0", 0], ["02__0", 0]]}, {"colliders": "parts", "line": "mod_wall_brick_rail"}),
   ("mod_wall_brick_rail_pillar", "Brick wall with railing, pillar (model)", "Modern: Fences", {"objects": ["02__0"]}, {"colliders": "box"}),
 ]),
 "compound": ("wall_door_-_19mb/wall_door_-_19mb.glb", None, 0.8, None, [
   ("mod_wall_compound_bay", "Compound wall, bay (model)", "Modern: Fences", {"chain": [["wall_wall_0", 0], ["wall_2_wall_0", 0]]}, {"colliders": "parts", "line": "mod_wall_compound"}),
   ("mod_wall_compound_doorway", "Compound wall, doorway (model)", "Modern: Fences", {"chain": [["wall_2_2_wall_0", 1.74], ["wall_3_wall_0", 0]]}, {"colliders": "parts", "doorway": True}),
   ("mod_wall_compound_pillar", "Compound wall, pillar (model)", "Modern: Fences", {"objects": ["wall_5_wall_0"]}, {"colliders": "box"}),
 ]),
 "pc": ("pc_pack/pc_pack.glb", None, 1.0, None, [
   ("furn_mod_monitor_16x9", "Monitor, 16:9 (model)", "Furniture", {"objects": ["Object_7", "Object_8"]}, {"furniture": True}),
   ("furn_mod_monitor_4x3", "Monitor, 4:3 (model)", "Furniture", {"objects": ["Object_15", "Object_16"]}, {"furniture": True}),
   ("furn_mod_monitor_superwide", "Monitor, super-ultrawide (model)", "Furniture", {"objects": ["Object_23", "Object_24"]}, {"furniture": True}),
   ("furn_mod_monitor_curved", "Monitor, curved ultrawide (model)", "Furniture", {"objects": ["Object_32", "Object_33"]}, {"furniture": True}),
   ("furn_mod_monitor_ultrawide", "Monitor, ultrawide (model)", "Furniture", {"objects": ["Object_41", "Object_42"]}, {"furniture": True}),
 ]),
 "factory": ("factory_props/factory_props.glb", None, 1.0, None, [
   ("mod_fp_container_closed", "Shipping container, closed (model)", FP, {"objects": ["Container_01_Container_02_0"]}, {"colliders": "box"}),
   ("mod_fp_container_open", "Shipping container, doors open (model)", FP, {"objects": ["Container_02_Container_01_0"]}, {"colliders": "parts"}),
   ("mod_fp_generator_big", "Generator set, large (model)", FP, {"objects": ["Generator_02_Big Generator_0"]}, {"colliders": "box"}),
   ("mod_fp_generator_small", "Generator, portable (model)", FP, {"objects": ["Generator_01_Misc_01_0", "Generator_01.001_Misc_01_0", "Generator_01.002_Misc_01_0", "Generator_01.003_Misc_01_0"]}, {"colliders": "box"}),
   ("mod_fp_light_tower", "Work light on a tripod (model)", FP, {"objects": ["IndusLight_01_Misc_01_0", "IndusLight_02_Misc_01_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_pallet", "Pallet (model)", FP, {"objects": ["Pallet_01_Misc_01_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_sack", "Sack (model)", FP, {"objects": ["Pallet_02_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_cone", "Traffic cone (model)", "Modern: Barriers", {"objects": ["Cone_01_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_bollard", "Bollard, striped (model)", "Modern: Barriers", {"objects": ["Pole_01_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_caution_sign", "Caution sign, A-frame (model)", FP, {"objects": ["Sign_02_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_sign_post", "Safety signs on a post (model)", FP, {"objects": ["Sign_01_Misc_02_0", "Sign_03_Misc_02_0", "Sign_04_Misc_02_0", "Sign_05_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_planks", "Planks, pair (model)", FP, {"objects": ["Plank_01_Misc_02_0", "Plank_02_Misc_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_door_frame", "Steel door frame (model)", "Modern: Interior", {"objects": ["WC_kit_02_WC_01_0", "WC_kit_05_WC_01_0"]}, {"colliders": "parts"}),
   ("mod_fp_door_a", "Steel door leaf (model)", "Modern: Interior", {"objects": ["WC_kit_01_WC_02_0"]}, {"colliders": "box"}),
   ("mod_fp_door_b", "Steel door leaf with handle (model)", "Modern: Interior", {"objects": ["WC_kit_04_WC_02_0"]}, {"colliders": "box"}),
   ("mod_fp_partition", "Cubicle partition panel (model)", "Modern: Interior", {"objects": ["WC_kit_03_WC_01_0"]}, {"colliders": "box"}),
   ("mod_fp_locker_single", "Locker, single (model)", "Modern: Interior", {"objects": ["Locker_01_Lockers_0", "Locker_04_Lockers_0"]}, {"colliders": "box"}),
   ("mod_fp_locker_tall", "Locker, tall (model)", "Modern: Interior", {"objects": ["Locker_02_Lockers_0", "Locker_03_Lockers_0"]}, {"colliders": "box"}),
   ("mod_fp_washbasin", "Washbasin with mirror (model)", "Modern: Interior", {"objects": ["Sink_01_Bathroom_0", "Glass_01_Bathroom_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_wc", "WC (model)", "Modern: Interior", {"objects": ["WC_01_Bathroom_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_racking_tall", "Steel racking, tall (model)", FP, {"objects": ["Shelves_01_Shelves_0"]}, {"colliders": "box"}),
   ("mod_fp_racking_low", "Steel racking, low (model)", FP, {"objects": ["Shelves_02_Shelves_0"]}, {"colliders": "box"}),
   ("mod_fp_worktable_a", "Steel work table (model)", FP, {"objects": ["Plane.093_Tables_0"]}, {"colliders": "box"}),
   ("mod_fp_worktable_b", "Steel work table, braced (model)", FP, {"objects": ["Plane.095_Tables_0"]}, {"colliders": "box"}),
   ("mod_fp_platform_trolley", "Platform trolley (model)", FP, {"objects": ["Forklift_01_Carts_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_sack_truck", "Sack truck (model)", FP, {"objects": ["Forklift_02_Carts_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_vent_fan", "Wall fan grille (model)", FACADE, {"objects": ["Vent_01_Vent + chair_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_vent_louvre", "Louvred vent grille (model)", FACADE, {"objects": ["Vent_02_Vent + chair_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_folding_chair", "Folding chair (model)", "Modern: Interior", {"objects": ["Chair_01_Vent + chair_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_ladder", "Ladder, straight (model)", FP, {"objects": ["Ladder_01_Ladders_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_stepladder", "Stepladder (model)", FP, {"objects": ["Ladder_02_Ladders_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_crate_wood", "Wooden crate (model)", FP, {"objects": ["Crate_01_Crate_0"]}, {"colliders": "box"}),
   ("mod_fp_crate_steel", "Crate under a tarp (model)", FP, {"objects": ["Crate_03_Crate_0"]}, {"colliders": "box"}),
   ("mod_fp_light_batten", "Fluorescent batten light (model)", "Modern: Interior", {"objects": ["Light_01_Lamps_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_light_batten_twin", "Fluorescent batten light, twin (model)", "Modern: Interior", {"objects": ["Light_02_Lamps_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_light_pendant", "Industrial pendant lamp (model)", "Modern: Interior", {"objects": ["Light_03_Lamps_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_drum_a", "Oil drum (model)", FP, {"objects": ["Cylinder.015_Barrel + tools_0"]}, {"colliders": "box", "small": True, "height": 0.88}),
   ("mod_fp_drum_b", "Oil drum, dented (model)", FP, {"objects": ["Cylinder.016_Barrel + tools_0"]}, {"colliders": "box", "small": True, "height": 0.88}),
   ("mod_fp_gas_cylinder", "Gas cylinder (model)", FP, {"objects": ["Cylinder.017_Barrel + tools_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_extinguisher", "Fire extinguisher (model)", FP, {"objects": ["Cylinder.014_Barrel + tools_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_box_open", "Cardboard box, open (model)", FP, {"objects": ["Cube.083_Barrel + tools_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_box_closed", "Cardboard box (model)", FP, {"objects": ["Cube.092_Barrel + tools_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_box_small", "Cardboard box, small (model)", FP, {"objects": ["Cube.081_Barrel + tools_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_tools_scatter", "Hand tools on the floor (model)", FP, {"objects": ["Cube.101_Barrel + tools_0", "Cube.102_Barrel + tools_0", "Cylinder.029_Barrel + tools_0", "Cylinder.030_Barrel + tools_0", "Cylinder.031_Barrel + tools_0", "Point_Barrel + tools_0", "Point.001_Barrel + tools_0"]}, {"colliders": "box", "small": True, "flat": True}),
   ("mod_fp_pipe_long", "Pipe, 7.7 m (model)", FP, {"objects": ["Pipes_01_pipes_01_0"]}, {"colliders": "box"}),
   ("mod_fp_pipe_short", "Pipe, 3.9 m (model)", FP, {"objects": ["Pipes_02_pipes_01_0"]}, {"colliders": "box"}),
   ("mod_fp_pipe_bend", "Pipe, sweeping bend (model)", FP, {"objects": ["Pipes_03_pipes_01_0"]}, {"colliders": "parts"}),
   ("mod_fp_pipe_flange", "Pipe flange ring (model)", FP, {"objects": ["Pipes_04_pipes_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_pipe_elbow", "Pipe elbow (model)", FP, {"objects": ["Pipes_05_pipes_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_pipe_tee", "Pipe tee (model)", FP, {"objects": ["Pipes_06_pipes_02_0"]}, {"colliders": "box", "small": True}),
   ("mod_fp_pipe_valve", "Pipe valve (model)", FP, {"objects": ["Pipes_07_pipes_02_0"]}, {"colliders": "box", "small": True}),
 ]),
 # the two zip packs: no licence, no author in the files: local only
 "ac": ("air-conditioner-pack/source/Air conditioner.fbx", "air-conditioner-pack/textures/air conditioner.png", 0.65, "air-conditioner-pack.zip supplied by the user; author, source and licence not in the files", [
   (aid, name, FACADE, {"mesh": "Plane.006", "clusters": [ci]}, {"colliders": "box", "small": True}) for aid, name, ci in [
     ("mod_ac_unit_small", "AC outdoor unit, small (model)", 0), ("mod_ac_unit_tall", "AC outdoor unit, twin fan (model)", 1),
     ("mod_ac_unit_box", "AC outdoor unit, box (model)", 5), ("mod_ac_unit_wide", "AC outdoor unit, wide (model)", 10),
     ("mod_ac_rack_a", "AC units on a wall rack A (model)", 7), ("mod_ac_rack_b", "AC units on a wall rack B (model)", 8),
     ("mod_ac_rack_c", "AC units on a wall rack C (model)", 11), ("mod_ac_rack_d", "AC units on a wall rack D (model)", 13),
     ("mod_window_frame_wood", "Window frame, old wood (model)", 2), ("mod_window_frame_white", "Window frame, white (model)", 3),
     ("mod_window_wide_a", "Window, wide, old frame A (model)", 4), ("mod_window_wide_b", "Window, wide, old frame B (model)", 12),
     ("mod_vent_grille_a", "Vent grille A (model)", 6), ("mod_vent_grille_b", "Vent grille B (model)", 9)]]),
 "billboard": ("abandoned-billboard-pack/source_x/Billboard.fbx", "abandoned-billboard-pack/source_x/Billboard.png", 0.5, "abandoned-billboard-pack.zip supplied by the user; author, source and licence not in the files", [
   ("mod_billboard_%02d" % (i + 1), "Faded billboard %d (model)" % (i + 1), FACADE, {"objects": [c]}, {"colliders": "parts", "yaw": -90})
   for i, c in enumerate(["Cube.001", "Cube.002", "Cube.003", "Cube.009", "Cube.010", "Cube.011", "Cube.025", "Cube.026", "Cube.027", "Cube.017", "Cube.018", "Cube.019", "Cube.024", "Cube.023", "Cube.021"])]),
}
# road signs (road_signs.glb): 48 designs; the weathered set: one sign on its post (the model's own post; collider on the
# post only) and the face alone (wall / existing post mounting, no collider). The Exit 6 tab has no post.
SIGNS = [
 ('merging_traffic', 'Merging traffic', 'Object_286', 'Object_320'),
 ('reverse_turn', 'Reverse turn', 'Object_280', 'Object_326'),
 ('u_turn_ahead', 'U-turn ahead', 'Object_282', 'Object_324'),
 ('roundabout_ahead', 'Roundabout ahead', 'Object_284', 'Object_322'),
 ('road_narrows', 'Road narrows', 'Object_230', 'Object_354'),
 ('winding_road', 'Winding road', 'Object_276', 'Object_294'),
 ('yield_ahead', 'Yield ahead', 'Object_202', 'Object_300'),
 ('slippery_road', 'Slippery road', 'Object_236', 'Object_298'),
 ('curve_right', 'Curve right', 'Object_240', 'Object_308'),
 ('two_way_traffic', 'Two-way traffic', 'Object_234', 'Object_296'),
 ('traffic_signal_ahead', 'Traffic signal ahead', 'Object_242', 'Object_342'),
 ('guide_sign', 'Guide sign (Brookfield / Green Bay)', 'Object_260', 'Object_384'),
 ('exit_6_tab', 'Exit 6 tab', 'Object_262', None),
 ('large_arrow', 'Large arrow', 'Object_268', 'Object_378'),
 ('chevron', 'Chevron', 'Object_290', 'Object_314'),
 ('speed_limit_55', 'Speed limit 55', 'Object_204', 'Object_312'),
 ('interstate_95', 'Interstate 95', 'Object_256', 'Object_306'),
 ('one_way', 'One way', 'Object_218', 'Object_380'),
 ('wrong_way', 'Wrong way', 'Object_270', 'Object_372'),
 ('airport', 'Airport', 'Object_232', 'Object_374'),
 ('route_66', 'Route 66', 'Object_258', 'Object_352'),
 ('dead_end', 'Dead end', 'Object_210', 'Object_340'),
 ('railroad_crossing', 'Railroad crossing', 'Object_208', 'Object_376'),
 ('school_crossing', 'School crossing', 'Object_216', 'Object_364'),
 ('bike_lane_wrong_way', 'Bike lane wrong way', 'Object_220', 'Object_304'),
 ('disabled_parking', 'Disabled parking', 'Object_206', 'Object_336'),
 ('stop', 'Stop', 'Object_214', 'Object_362'),
 ('pedestrian_crossing', 'Pedestrian crossing', 'Object_238', 'Object_356'),
 ('parking', 'Parking', 'Object_212', 'Object_368'),
 ('bicycle_crossing', 'Bicycle crossing', 'Object_226', 'Object_348'),
 ('no_parking_any_time', 'No parking any time', 'Object_250', 'Object_292'),
 ('tow_away_zone', 'Tow-away zone', 'Object_200', 'Object_344'),
 ('detour', 'Detour', 'Object_254', 'Object_338'),
 ('detour_arrow', 'Detour arrow', 'Object_224', 'Object_382'),
 ('under_construction', 'Under construction', 'Object_198', 'Object_350'),
 ('caution_slow_down', 'Caution slow down', 'Object_272', 'Object_370'),
 ('road_work_ahead', 'Road work ahead', 'Object_244', 'Object_346'),
 ('slow', 'Slow', 'Object_278', 'Object_334'),
 ('men_at_work', 'Men at work', 'Object_222', 'Object_360'),
 ('construction_zone', 'Construction zone', 'Object_228', 'Object_310'),
 ('no_right_turn', 'No right turn', 'Object_264', 'Object_332'),
 ('no_u_turn', 'No U-turn', 'Object_246', 'Object_330'),
 ('no_left_turn', 'No left turn', 'Object_266', 'Object_328'),
 ('no_bicycles', 'No bicycles', 'Object_248', 'Object_358'),
 ('left_turn_only', 'Left turn only', 'Object_288', 'Object_318'),
 ('left_or_straight', 'Left or straight', 'Object_274', 'Object_316'),
 ('do_not_enter', 'Do not enter', 'Object_196', 'Object_302'),
 ('yield', 'Yield', 'Object_252', 'Object_366'),
]
SRC["signs"] = ("road_signs/road_signs.glb", None, 0.25, None,
  [("mod_sign_" + slug, "Road sign: " + name, "Modern: Signs", {"objects": [post]}, {"colliders": "post", "small": True, "maxTex": 2048, "lod1": 0, "sign": True}) for slug, name, face, post in SIGNS if post]
  + [("mod_sign_face_" + slug, "Sign face: " + name, "Modern: Signs", {"objects": [face]}, {"colliders": "box", "small": True, "flat": True, "maxTex": 2048, "lod1": 0, "sign": True}) for slug, name, face, post in SIGNS])
# line types (lasso Line mode): bays of the wall piece laid end to end; the compound wall takes its doorway as a gate
LINES = {
 "mod_wall_cement": ("Cement fence wall (model)", "mod_wall_cement_bay", None),
 "mod_wall_brick_rail": ("Brick wall with railing (model)", "mod_wall_brick_rail_bay", None),
 "mod_wall_compound": ("Compound wall, 2.8 m (model)", "mod_wall_compound_bay", "mod_wall_compound_doorway"),
 "def_sandbag_bags_low": ("Sandbag wall, low (model)", "def_sandbag_bags_low", None),
 "def_sandbag_bags_high": ("Sandbag wall, high (model)", "def_sandbag_bags_high", None),
}
DESC = {
 "def_sandbag_bags_low": "A crouch-high wall of filled sandbags, two to three courses; stops rifle rounds.",
 "def_sandbag_bags_high": "A chest-high sandbag breastwork, two stacked courses; stops rifle rounds.",
 "mod_wall_compound_doorway": "A doorway in the compound wall: two pillars with a 1.4 m opening (the model has no door leaf).",
}


def glb_meta(p):
    with open(p, "rb") as f:
        f.read(12); n, _ = struct.unpack("<II", f.read(8)); return json.loads(f.read(n))


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True); jd = os.path.join(glbs, "_jobs"); os.makedirs(jd, exist_ok=True)
    for key, (f, tex, scale, lic, pieces) in SRC.items():
        todo = [p for p in pieces if not os.path.exists(os.path.join(glbs, p[0] + ".glb"))]
        if not todo: continue
        job = {"src": os.path.join(dl, f), "tex": os.path.join(dl, tex) if tex else None, "scale": scale, "out": glbs,
               "pieces": [{"id": aid, **sel, **{k: v for k, v in opt.items() if k in ("decimate", "colliders", "yaw", "height", "sign")}} for aid, _, _, sel, opt in todo]}
        jp = os.path.join(jd, key + ".json"); json.dump(job, open(jp, "w"))
        r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True)
        out = [l for l in r.stdout.splitlines() if l.startswith("PR6")]
        print("\n".join(out) if out else key + " FAILED " + (r.stdout + r.stderr)[-800:])


def cover_pts(sx, sy, sz):
    if sy < 0.6 or max(sx, sz) < 0.6: return []
    h = "high" if sy >= 1.3 else "low"
    xs = [0.0] if sx < 2.5 else [-sx / 4, sx / 4]
    return [{"p": [round(x, 2), 0, round(s * (sz / 2 + 0.45), 2)], "yaw": 180 if s > 0 else 0, "height": h, "peek": ["over", "left", "right"] if h == "low" else ["left", "right"]} for x in xs for s in (1, -1)]


def write(dl, glbs):
    reg, credits, lines = {}, [], []
    gi_add = []
    for key, (f, tex, scale, licnote, pieces) in SRC.items():
        if f.endswith(".glb"):
            ex = glb_meta(os.path.join(dl, f)).get("asset", {}).get("extras", {}) or {}
            lic = (ex.get("license") or "unknown").split(" (")[0]; author = (ex.get("author") or "unknown").split(" (")[0]
            title, url = ex.get("title", key), ex.get("source", "")
        else:
            lic, author, title, url = "unknown", "unknown", os.path.basename(f), ""
        local = lic not in ("CC0", "CC-BY-4.0", "CC-BY-3.0")
        credit = ("%s by %s, Sketchfab (%s), %s" % (title, author, url, lic)) if not local else ("%s (%s: local only)" % (title, licnote))
        for aid, name, cat, sel, opt in pieces:
            side = json.load(open(os.path.join(glbs, aid + ".json")))
            sx, sy, sz = side["size"]; cols = side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]]
            small = opt.get("small", False)
            big = max(sx, sy, sz) > 4
            model = {"pivot": "ground", "maxTex": opt.get("maxTex", 2048 if big or max(sx, sz) > 1.5 else 1024), "simplify": 0.5 if side["tris"] > 20000 else 1, "lod1": opt.get("lod1", 0.3),
                     "lodDist": 40 if big else 15, "cullDist": 600 if big else 300, "src": aid + ".glb", "dir": aid, "jpgMaps": True, "scale": 1, "credit": credit}
            parts = [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", **({"mf": 1} if local else {"v": 0})} for c in cols]
            if opt.get("flat"): parts = [{"t": "b", "c": [0, sy / 2, 0], "s": [sx, sy, sz], "m": "collider", **({"mf": 1} if local else {"v": 0}), "collide": False}]
            A = {"id": aid, "version": 1, "name": name, "category": cat,
                 "description": DESC.get(aid, "%s, %.2f x %.2f x %.2f m: %s by %s%s." % (name.replace(" (model)", ""), sx, sz, sy, title, author, "" if not local else " (local only: the committed game shows a box stand-in)")),
                 "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5 if not opt.get("line") else 3, "noAutoPanels": True,
                 "model": model,
                 "nav": {"effect": "none" if (small or opt.get("furniture")) else "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [],
                         "cover": [] if (small or opt.get("furniture")) else cover_pts(sx, sy, sz)},
                 "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
                 "parts": parts, "furniture": [], "interactables": [], "panels": []}
            if opt.get("sandbag"):
                A["defense"] = {"kind": "sandbag", "hp": 360, "stopsBullets": True, "blastOcclusion": 0.12, "maxSlopeDeg": 35}
            if opt.get("line") or opt.get("doorway") or opt.get("sandbag"):
                A["snap"] = [{"p": [round(-sx / 2, 3), 0, 0], "dir": [-1, 0], "kind": key}, {"p": [round(sx / 2, 3), 0, 0], "dir": [1, 0], "kind": key}]
            if opt.get("furniture"):   # a desk-top piece for the furnisher (src/interior.cpp)
                A["category"] = "Furniture"; A["maxGrade"] = 0.3
                A["prop"] = {"family": "glass", "rounds": 2, "blast": 6, "blocking": False, "wall": False, "front": 0.0, "mount": 0, "groups": ["body"], "fragments": [], "keep": [0], "surfaces": [],
                             "bounds": [[round(-sx / 2, 3), 0, round(-sz / 2, 3)], [round(sx / 2, 3), round(sy, 3), round(sz / 2, 3)]]}
                A["parts"] = [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "bv": round(min(0.004, min(c[3:]) * 0.2), 4), "k": 0, "g": 0, "mf": 1} for c in cols]
            open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
            reg[aid] = {"source": key, "file": f, "title": title, "author": author, "license": lic, "url": url, "local": local, "size": side["size"], "tris": side["tris"], "category": A["category"]}
            if local: gi_add.append("content/assets/models/%s/" % aid)
        credits.append("| %s | %s | %s | %s | %s | %d assets | %s |" % (os.path.basename(f), title, author, lic, url or "-", len(pieces), "no (local-only)" if local else "yes"))
    # line carriers + types
    lt_path = os.path.join(ROOT, "content", "settings", "line_types.json"); LT = json.load(open(lt_path, encoding="utf-8"))
    LT["types"] = [t for t in LT["types"] if t["id"] not in LINES]
    for lid, (name, piece, gate) in LINES.items():
        sx = reg[piece]["size"][0]
        t = {"id": lid, "name": name, "asset": "line_" + lid, "builder": "bays", "spacing": round(sx, 3), "pieces": [[piece, 1]], "params": {}}
        if gate: t["gates"] = [gate]
        if lid.startswith("def_sandbag"): t["sectionHp"] = 360
        LT["types"].append(t)
        L = {"id": "line_" + lid, "version": 1, "name": name + " (line)", "category": "Lines", "hidden": True,
             "description": "%s drawn as a line (lasso Line mode): bays of %s along the line; gates and gaps as spans." % (name, piece),
             "floors": 0, "enterable": False, "footprint": [1, 1], "plinthDepth": 0, "maxGrade": 3, "noAutoPanels": True, "line": {"type": lid},
             "materials": [], "parts": [], "furniture": [], "interactables": [], "panels": [], "nav": {"effect": "carve"}}
        if lid.startswith("def_sandbag"): L["defense"] = {"kind": "sandbag", "hp": 360, "stopsBullets": True, "blastOcclusion": 0.12, "maxSlopeDeg": 35}
        open(os.path.join(ROOT, "content", "assets", "line_" + lid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(L, separators=(",", ":")))
    open(lt_path, "w", encoding="utf-8", newline="").write(json.dumps(LT, indent=2, ensure_ascii=False) + "\n")
    json.dump(reg, open(os.path.join(HERE, "pr6_assets.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    # docs/credits.md
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- pr6_assets: written by tools/props/pr6_assets.py -->", "<!-- /pr6_assets -->"
    table = a + "\nAsset packs the user supplied (PR6; licence, author and source read from each file's own metadata). A local-only pack is not in the repository: the committed game shows box stand-ins.\n\n| File | Model | Author | Licence | Source | Assets | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(credits) + "\n" + b
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\r\n", "\n").replace("\n", nl))   # keep the file's own line endings
    gi = os.path.join(ROOT, ".gitignore"); g = open(gi, encoding="utf-8").read()
    for line in gi_add:
        if line not in g: g = g.rstrip("\n") + "\n# PR6: local-only model (no licence in the pack), see docs/credits.md\n" + line + "\n"
    open(gi, "w", encoding="utf-8", newline="").write(g)
    print(len(reg), "assets;", sum(r["local"] for r in reg.values()), "local-only;", len(LINES), "line types")


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
