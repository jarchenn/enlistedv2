# MG1 (docs/fps-weapons.md#mg1-animated): one modern-gun source -> a skinned, animated first-person viewmodel GLB
# (the source's own arms + the gun, every clip as one glTF animation) plus <out>.json (clip lengths, the gun joint,
# sight / muzzle / eye points in rig space). Run in Blender 5.2:
#   blender -b --factory-startup --python tools/modern_guns_anim.py -- <config>.json   (configs: modern_guns_anim_config.cjs)
# Rig space = the first-person camera frame in glTF terms: eye at the origin, looking down -z, +y up, metres.
# Cinema 4D exports animate the gun parts as objects: each kept unskinned mesh becomes a bone (baked per frame) so
# the whole viewmodel is one skin. One-timeline sources are cut into clips by frame ranges (NLA strips).
import bpy, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector
cfg = json.load(open(sys.argv[sys.argv.index("--") + 1]))
src, out, tex = cfg["src"], cfg["out"], cfg.get("texdir", "")
if src.endswith(".blend"): bpy.ops.wm.open_mainfile(filepath=src)
elif src.endswith(".glb"): bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=src)   # MG7: Sketchfab GLB sets
else: bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.fbx(filepath=src)
sc = bpy.context.scene
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
keep = [bpy.data.objects[n] for n in cfg["keep"]]
f0 = cfg.get("restFrame", 1)
log = {"warn": []}

# ---------------------------------------------------------------- MG7: a held object skinned to a hand bone
# cfg "boneAttach": [{"mesh", "bone", "frame"}]: the mesh where the source has it at that frame, moved into the bone's rest
# pose and weighted 1 to it (the knife rides an IK goal empty in its source; the baked arm bones follow that goal)
for ba in cfg.get("boneAttach", []):
    sc.frame_set(ba["frame"]); bpy.context.view_layer.update()
    o = bpy.data.objects[ba["mesh"]]; pb = arm.pose.bones[ba["bone"]]
    Mp = arm.matrix_world @ pb.matrix; Mr = arm.matrix_world @ pb.bone.matrix_local
    o.data = o.data.copy(); o.data.transform(arm.matrix_world.inverted() @ Mr @ Mp.inverted() @ o.matrix_world)
    o.animation_data_clear(); o.parent = arm; o.matrix_parent_inverse = Matrix(); o.matrix_basis = Matrix()
    for g in list(o.vertex_groups): o.vertex_groups.remove(g)
    vg = o.vertex_groups.new(name=pb.name); vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    if not any(m.type == "ARMATURE" for m in o.modifiers): md = o.modifiers.new("Armature", "ARMATURE"); md.object = arm
# ---------------------------------------------------------------- MG8: bone-parented meshes -> skinned to that bone
# (the M4 iron-sight pack's gun parts hang off bones; such children do not survive the rig re-parenting below in the
# glTF export). Each kept mesh whose parent chain reaches a bone of the armature becomes a mesh weighted 1 to that bone,
# in armature space at the rest pose.
def _bone_of(o):
    while o is not None:
        if o.parent == arm and o.parent_type == "BONE": return o.parent_bone
        o = o.parent
    return None
_bp = [(o, _bone_of(o)) for o in keep if o.type == "MESH" and not any(m.type == "ARMATURE" for m in o.modifiers)]
_bp = [(o, b) for o, b in _bp if b]
if _bp:
    arm.data.pose_position = "REST"; bpy.context.view_layer.update()
    _mw = {o.name: o.matrix_world.copy() for o, b in _bp}
    for o, b in _bp:
        o.data = o.data.copy(); o.data.transform(arm.matrix_world.inverted() @ _mw[o.name])
        o.animation_data_clear(); o.parent = arm; o.parent_type = "OBJECT"; o.matrix_parent_inverse = Matrix(); o.matrix_basis = Matrix()
        vg = o.vertex_groups.new(name=b); vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
        md = o.modifiers.new("Armature", "ARMATURE"); md.object = arm
    arm.data.pose_position = "POSE"; bpy.context.view_layer.update()
    log["boneParented"] = [[o.name, b] for o, b in _bp]
# ---------------------------------------------------------------- C4D: animated gun objects -> bones
rig = cfg.get("rigidify", [])
if rig:
    if arm.animation_data is None or arm.animation_data.action is None: raise SystemExit("MG1 no armature action")
    fa, fb = cfg["timeline"]
    objs = [bpy.data.objects[n] for n in rig]
    W, A = {o.name: {} for o in objs}, {}
    for f in range(fa, fb + 1):
        sc.frame_set(f); A[f] = arm.matrix_world.copy()
        for o in objs: W[o.name][f] = o.matrix_world.copy()
    nrm = lambda m: Matrix.LocRotScale(m.to_translation(), m.to_quaternion(), None)
    bpy.context.view_layer.objects.active = arm; bpy.ops.object.mode_set(mode="EDIT")
    for o in objs:
        eb = arm.data.edit_bones.new("gun_" + o.name); eb.head = (0, 0, 0); eb.tail = (0, 0.05, 0)
        eb.matrix = nrm(A[f0].inverted() @ W[o.name][f0]); eb.use_deform = True
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in objs:
        pb = arm.pose.bones["gun_" + o.name]; pb.rotation_mode = "QUATERNION"
        for f in range(fa, fb + 1):
            pb.matrix = nrm(A[f].inverted() @ W[o.name][f])
            for p in ("location", "rotation_quaternion", "scale"): pb.keyframe_insert(p, frame=f)
    for o in objs:   # bind: the mesh in world space at the rest frame, weight 1 on its bone
        o.animation_data_clear()
        o.data = o.data.copy(); o.data.transform(W[o.name][f0]); o.parent = None; o.matrix_world = Matrix()
        o.parent = arm; o.matrix_parent_inverse = A[f0].inverted()
        vg = o.vertex_groups.new(name="gun_" + o.name); vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
        md = o.modifiers.new("Armature", "ARMATURE"); md.object = arm
sc.frame_set(f0)

# ---------------------------------------------------------------- the gun joint and the points (rest pose, world)
gun = bpy.data.objects[cfg["gun"]]
if rig: gunBone = "gun_" + gun.name
else:
    cnt = {}
    vgn = [g.name for g in gun.vertex_groups]
    for v in gun.data.vertices:
        if v.groups: b = max(v.groups, key=lambda g: g.weight); cnt[vgn[b.group]] = cnt.get(vgn[b.group], 0) + 1
    gunBone = cfg.get("gunBone") or max(cnt, key=cnt.get)
# weapon space (static bake) -> static glTF -> Blender world at rest: p_g = p_w / s + o, world = (x, -z, y)
FR = cfg.get("frame")   # MG7 (the knife): no sights: an explicit camera frame in Blender world units at rest (eye, a point ahead, up, scale)
if FR:
    s = FR["scale"]; o_ = [0, 0, 0]
    def w2b(p): return Vector(p)
    pts = {"rear": Vector(FR["rear"]), "front": Vector(FR["front"]), "muzzle": Vector(FR["front"])}
else:
    st = cfg["static"]; s = st["s"]; o_ = st["o"]
    def w2b(p): g = [p[0] / s + o_[0], p[1] / s + o_[1], p[2] / s + o_[2]]; return Vector((g[0], -g[2], g[1]))
    pts = {k: w2b(v) for k, v in cfg["points"].items()}
    pts["_up"] = w2b([cfg["points"]["rear"][0], cfg["points"]["rear"][1] + 0.05, cfg["points"]["rear"][2]])   # the framing's up reference
    if cfg.get("staticPoseFrame") is not None:   # MG7: the static bake was posed at this frame (the SCAR-H): its points back to the rest pose
        if cfg.get("staticPoseAction"):
            _ad = arm.animation_data or arm.animation_data_create(); _act = bpy.data.actions[cfg["staticPoseAction"]]; _ad.action = _act
            if hasattr(_ad, "action_slot") and _act.slots: _ad.action_slot = _act.slots[0]
        sc.frame_set(cfg["staticPoseFrame"]); bpy.context.view_layer.update()
        _gb = cfg.get("gunBone"); _pb = arm.pose.bones[_gb]
        _D = ((arm.matrix_world @ _pb.matrix) @ (arm.matrix_world @ _pb.bone.matrix_local).inverted()).inverted()
        pts = {k: _D @ v for k, v in pts.items()}
# check: the gun body's rest bbox must match the static export's (same rest pose)
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
ev = gun.evaluated_get(dg); me = ev.to_mesh()
vs = [ev.matrix_world @ v.co for v in me.vertices]; ev.to_mesh_clear()
bb = [min(v[i] for v in vs) for i in range(3)] + [max(v[i] for v in vs) for i in range(3)]
log["gunRestBlender"] = [round(x, 4) for x in bb]; log["staticGunBlender"] = None if FR else st.get("gunBlender")
arm.data.pose_position = "POSE"

# ---------------------------------------------------------------- MG3: attach an accessory mesh to the gun (the red dot on the 870)
# cfg "attach": {"src": fbx, "mesh": name, "dropMats": [lens material], "anchor": [x,y,z] in its own world units (meets "at"),
# "at": [x,y,z] static weapon space}; its +y follows the gun's forward, +z its up; real size (1/s in source units);
# weighted 1 to the gun joint and joined into the gun mesh (rest frame), so it moves with the gun in every clip
if cfg.get("attach"):
    at_ = cfg["attach"]; before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=at_["src"]); new = [o for o in bpy.data.objects if o not in before]
    acc = next(o for o in new if o.name.startswith(at_["mesh"]))
    p0 = w2b(at_["at"]); fw = (w2b([at_["at"][0], at_["at"][1], at_["at"][2] + 0.1]) - p0).normalized(); upv = (w2b([at_["at"][0], at_["at"][1] + 0.1, at_["at"][2]]) - p0).normalized()
    rt_ = fw.cross(upv); Rm = Matrix((rt_, fw, upv)).transposed().to_4x4()
    T = Matrix.Translation(p0) @ Rm @ Matrix.Scale(1.0 / s, 4) @ Matrix.Translation(-Vector(at_.get("anchor", [0, 0, 0])))
    acc.data = acc.data.copy(); acc.data.transform(T @ acc.matrix_world); acc.parent = None; acc.matrix_world = Matrix()
    import bmesh
    drop = [k for k, m in enumerate(acc.data.materials) if m and m.name in at_.get("dropMats", [])]
    bm = bmesh.new(); bm.from_mesh(acc.data); bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in drop], context="FACES"); bm.to_mesh(acc.data); bm.free()
    vg = acc.vertex_groups.new(name=gunBone); vg.add(list(range(len(acc.data.vertices))), 1.0, "REPLACE")
    for o in new:
        if o != acc: bpy.data.objects.remove(o, do_unlink=True)
    for o in bpy.context.view_layer.objects: o.select_set(False)
    acc.select_set(True); gun.select_set(True); bpy.context.view_layer.objects.active = gun; bpy.ops.object.join()
    log["attached"] = at_["mesh"]

# ---------------------------------------------------------------- hip framing -> rig space (+ metres)
# The sources' cameras differ (Cinema 4D exports carry none), so every rig is framed the engine's way: at the first
# idle frame the sight line (rear -> front) looks straight ahead, gun-up is up, and the rear sight sits at the hip pose
# (view (0.12, -0.07, -0.15) m: viewmodel.cpp T_hip with the default hipOffset; cfg "hipRear" overrides).
idle = cfg["clips"]["idle"]
ad0 = arm.animation_data or arm.animation_data_create()
if isinstance(idle, list): fr = idle[0]
else:
    act = bpy.data.actions[idle["action"] if isinstance(idle, dict) else idle]; ad0.action = act
    if hasattr(ad0, "action_slot") and act.slots: ad0.action_slot = act.slots[0]
    fr = idle["range"][0] if isinstance(idle, dict) else act.frame_range[0]
sc.frame_set(int(fr)); bpy.context.view_layer.update()
pb = arm.pose.bones[gunBone]
D = (arm.matrix_world @ pb.matrix) @ (arm.matrix_world @ pb.bone.matrix_local).inverted()
if FR: D = Matrix()   # the camera frame is fixed in the scene, not on the gun
R_, F_ = D @ pts["rear"], D @ pts["front"]
U_ = (R_ + Vector(FR["up"])) if FR else D @ pts.pop("_up")
fwd = (F_ - R_).normalized(); up = (U_ - R_); up = (up - fwd * up.dot(fwd)).normalized(); rt = fwd.cross(up)
B = Matrix((rt, fwd, up)).to_4x4()   # rows: world -> rig frame (x right, y forward, z up)
hip = Vector(cfg.get("hipRear", [0.12, 0.15, -0.07])) / s
root = Matrix.Translation(hip - B @ R_) @ B
root = Matrix.Scale(s, 4) @ root
log["idleFrame"] = fr
def b2g(v): v = root @ v; return [round(v.x, 5), round(v.z, 5), round(-v.y, 5)]

# ---------------------------------------------------------------- drop a hanging sling (islands lowest at rest)
if cfg.get("dropLow"):
    import bmesh
    arm.data.pose_position = "REST"; bpy.context.view_layer.update()
    zmin = min((gun.matrix_world @ v.co).z for v in gun.data.vertices)
    for o in [gun]:   # the gun mesh only (arms rest in their bind pose)
        bm = bmesh.new(); bm.from_mesh(o.data); bm.faces.ensure_lookup_table(); seen, drop = set(), []
        for f in bm.faces:
            if f.index in seen: continue
            isl, stk = [], [f]; seen.add(f.index)
            while stk:
                g = stk.pop(); isl.append(g)
                for e in g.edges:
                    for h in e.link_faces:
                        if h.index not in seen: seen.add(h.index); stk.append(h)
            if min((o.matrix_world @ v.co).z for g in isl for v in g.verts) < zmin + cfg["dropLow"]: drop += isl
        if drop: log.setdefault("dropped", {})[o.name] = len(drop); bmesh.ops.delete(bm, geom=drop, context="FACES"); bm.to_mesh(o.data)
        bm.free()
    arm.data.pose_position = "POSE"

# ---------------------------------------------------------------- MG2: cut opaque optic lenses (the engine has no glass on rigs)
# cfg "cutLens": [{"a": [x,y,z], "b": [x,y,z], "r": m}] in static weapon space: gun faces whose vertices all lie within r
# of the segment a-b (the lens discs inside the tube) are deleted; the tube walls lie outside r
if cfg.get("cutLens"):
    import bmesh
    for c in cfg["cutLens"]:
        A_, B_ = w2b(c["a"]), w2b(c["b"]); ab = B_ - A_; L2 = ab.dot(ab); r = c["r"] / s
        def near(p):
            t = max(0.0, min(1.0, (p - A_).dot(ab) / L2)); return (p - (A_ + ab * t)).length <= r
        for ob in keep:   # every kept mesh (the M4's optic is its own part)
            if ob.type != "MESH": continue
            bm = bmesh.new(); bm.from_mesh(ob.data); mw = ob.matrix_world
            axn = ab.normalized(); m3 = mw.to_3x3()
            # lens discs only: faces facing along the sight line (the tube walls and hoods face sideways and stay)
            facing = lambda f: abs((m3 @ f.normal).normalized().dot(axn)) > 0.85
            centred = lambda f: near(mw @ f.calc_center_median())
            # lens discs (and with "all": the inner rims) facing along the line are deleted; with "black" the sideways faces
            # inside r (tube walls, lens edges) are kept, flat black: the rim reads solid black and the housing stays closed
            if c.get("islands"):   # MG4 (the MP7): whole connected pieces inside r only (the lens discs); nothing attached to the tube
                bm.faces.ensure_lookup_table(); seen, cut = set(), []
                for f0 in bm.faces:
                    if f0.index in seen: continue
                    isl, stk = [], [f0]; seen.add(f0.index)
                    while stk:
                        g = stk.pop(); isl.append(g)
                        for e in g.edges:
                            for h in e.link_faces:
                                if h.index not in seen: seen.add(h.index); stk.append(h)
                    if all(near(mw @ v.co) for g in isl for v in g.verts): cut += isl
            else: cut = [f for f in bm.faces if facing(f) and (centred(f) if c.get("all") else all(near(mw @ v.co) for v in f.verts))]
            if cut: log.setdefault("lensFaces", []).append([ob.name, len(cut)])
            if c.get("black"):
                blk = bpy.data.materials.get("mg1_black") or bpy.data.materials.new("mg1_black")
                blk.use_nodes = True; bs = blk.node_tree.nodes["Principled BSDF"]
                bs.inputs["Base Color"].default_value = (0.004, 0.004, 0.004, 1); bs.inputs["Roughness"].default_value = 1.0; bs.inputs["Metallic"].default_value = 0.0
                if blk.name not in [m.name for m in ob.data.materials if m]: ob.data.materials.append(blk)
                bi = [m.name if m else "" for m in ob.data.materials].index(blk.name); cs = set(cut); nb = 0
                for f in bm.faces:
                    if f in cs or facing(f): continue
                    cw = mw @ f.calc_center_median(); t = max(0.0, min(1.0, (cw - A_).dot(ab) / L2)); rad = cw - (A_ + ab * t)
                    if rad.length <= c.get("blackR", c["r"]) / s and (m3 @ f.normal).dot(rad) < 0:   # inward-facing (the bore; "blackR" may reach wider than the cut); the outer housing keeps its finish
                        f.material_index = bi; nb += 1
                log.setdefault("blackFaces", []).append([ob.name, nb])
                if c.get("widen"):   # MG5: the bore (every vertex of a black face; the rims' inner edges move with it) pushed out radially: a thinner black rim, a larger clear circle; capped at widenMax, the outer housing's own vertices untouched
                    bset = set(f for f in bm.faces if f.material_index == bi); mwi = mw.inverted(); nv = 0
                    for v in set(v for f in bset for v in f.verts):
                        w = mw @ v.co; t = max(0.0, min(1.0, (w - A_).dot(ab) / L2)); o = A_ + ab * t; rv = w - o
                        if rv.length < 1e-9: continue
                        rn = min(rv.length * c["widen"], c.get("widenMax", 1.0) / s); v.co = mwi @ (o + rv.normalized() * rn); nv += 1
                    log.setdefault("widened", []).append([ob.name, nv])
            bmesh.ops.delete(bm, geom=cut, context="FACES"); bm.to_mesh(ob.data); bm.free()
# ---------------------------------------------------------------- MG2: decimate heavy meshes (before the armature; weights kept)
for name, ratio in cfg.get("decimate", {}).items():
    ob = bpy.data.objects[name]; bpy.context.view_layer.objects.active = ob
    md = ob.modifiers.new("mg2_dec", "DECIMATE"); md.ratio = ratio
    while ob.modifiers[0] != md: bpy.ops.object.modifier_move_up(modifier=md.name)
    n0 = len(ob.data.polygons); bpy.ops.object.modifier_apply(modifier=md.name); log.setdefault("decimated", {})[name] = [n0, len(ob.data.polygons)]
# ---------------------------------------------------------------- NG1: faces of these materials left out of a kept mesh ("dropMats": {mesh: [material]})
for _mn, _ml in cfg.get("dropMats", {}).items():
    import bmesh
    _o = bpy.data.objects[_mn]; _o.data = _o.data.copy(); _bm = bmesh.new(); _bm.from_mesh(_o.data)
    _dr = [f for f in _bm.faces if f.material_index < len(_o.data.materials) and _o.data.materials[f.material_index] and _o.data.materials[f.material_index].name.split(".")[0] in _ml]
    bmesh.ops.delete(_bm, geom=_dr, context="FACES_ONLY"); _bm.to_mesh(_o.data); _bm.free(); log.setdefault("droppedFaces", {})[_mn] = len(_dr)
# ---------------------------------------------------------------- NG2: faces skinned to these bones left out ("dropBones": {mesh: [bone]}): the upper
# arms / shoulders, which a pistol rig made for a closer camera puts across the aimed view (a face goes when any of its
# vertices' strongest bone is listed); the forearms end at the elbow, below the view
for _mn, _bl in cfg.get("dropBones", {}).items():
    import bmesh
    _o = bpy.data.objects[_mn]; _o.data = _o.data.copy(); _gi = {g.index for g in _o.vertex_groups if g.name in _bl}
    _hit = {v.index for v in _o.data.vertices if v.groups and max(v.groups, key=lambda g: g.weight).group in _gi}
    _bm = bmesh.new(); _bm.from_mesh(_o.data); _dr = [f for f in _bm.faces if any(v.index in _hit for v in f.verts)]
    bmesh.ops.delete(_bm, geom=_dr, context="FACES"); _bm.to_mesh(_o.data); _bm.free(); log.setdefault("droppedBoneFaces", {})[_mn] = len(_dr)
    if not _dr: log["warn"].append("dropBones: nothing on " + _mn)
# ---------------------------------------------------------------- drop everything else, move the rig
for ob in [arm] + keep:   # MG2: a parent about to go (the Unreal sets' armature under an empty) must not move the rig
    if ob.parent is not None and ob.parent != arm and ob.parent not in keep:
        mw = ob.matrix_world.copy(); ob.parent = None; ob.matrix_world = mw
bpy.context.view_layer.update()
for ob in list(bpy.data.objects):
    if ob != arm and ob not in keep: bpy.data.objects.remove(ob, do_unlink=True)
for ob in keep:
    if ob.parent is None:   # unparented skinned meshes (the pistol): parent to the armature in place
        mw = ob.matrix_world.copy(); ob.parent = arm; ob.matrix_world = mw
# MG7: glTF skinning ignores a skinned mesh's own node transform: bake it (relative to the armature) into the mesh data
# (the Glock 43 set's gun mesh carries a x4 object scale; identity, so a no-op, for the other rigs)
for ob in keep:
    if ob.type != "MESH" or not any(m.type == "ARMATURE" for m in ob.modifiers): continue
    M = arm.matrix_world.inverted() @ ob.matrix_world
    if max(abs(M[i][j] - (1.0 if i == j else 0.0)) for i in range(4) for j in range(4)) < 1e-5: continue
    ob.data = ob.data.copy(); ob.data.transform(M); ob.parent = arm; ob.matrix_parent_inverse = Matrix(); ob.matrix_basis = Matrix()
    log.setdefault("bakedMeshTransforms", []).append(ob.name)
# the armature's own clips may animate its object transform, so the frame goes on a parent node, not on the armature
rootObj = bpy.data.objects.new("vm_root", None); sc.collection.objects.link(rootObj); rootObj.matrix_world = root
arm.parent = rootObj; arm.matrix_parent_inverse = Matrix()
bpy.context.view_layer.update()

# ---------------------------------------------------------------- materials (as modern_guns_convert.py)
MAXT = cfg.get("maxTex", 1024)
def load(name):
    if not name: return None
    p = name if os.path.isabs(name) else os.path.join(tex, name)
    if not os.path.exists(p): log["warn"].append("missing texture " + name); return None
    im = bpy.data.images.load(p, check_existing=False); w, h = im.size
    if max(w, h) > MAXT: k = MAXT / max(w, h); im.scale(max(1, int(w * k)), max(1, int(h * k)))
    return im
def px(im): a = np.empty(im.size[0] * im.size[1] * 4, np.float32); im.pixels.foreach_get(a); return a.reshape(-1, 4)
def newimg(name, a, w, h): im = bpy.data.images.new(name, w, h, alpha=False); im.pixels.foreach_set(a.ravel()); im.file_format = "PNG"; im.pack(); return im
def resample(a, w, h, W_, H_):
    if (w, h) == (W_, H_): return a
    a = a.reshape(h, w, 4); return a[(np.arange(H_) * h // H_)][:, (np.arange(W_) * w // W_)].reshape(-1, 4)
for mname, m in cfg.get("mats", {}).items():
    mat = bpy.data.materials.new("mg1_" + mname); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes["Principled BSDF"]
    if "factor" in m:
        bsdf.inputs["Base Color"].default_value = m["factor"] + [1]; bsdf.inputs["Metallic"].default_value = m.get("metal_f", 0); bsdf.inputs["Roughness"].default_value = m.get("rough_f", 0.7)
    alb = load(m.get("albedo")); W_ = H_ = MAXT
    if alb:
        a = px(alb); W_, H_ = alb.size; a[:, 3] = 1
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = newimg("alb_" + mname, a, W_, H_); nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    def chan(key, ch=0, inv=False):
        im = load(m.get(key)) if isinstance(m.get(key), str) else None
        if not im: return None
        a = resample(px(im), im.size[0], im.size[1], W_, H_)
        v = a[:, ch] if ch < 4 else a[:, :3] @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        return 1 - v if inv else v
    if m.get("normal") and cfg.get("normals"):   # WP2: a tangent-space (OpenGL) normal map; opt-in per rig ("normals": true)
        nim = load(m["normal"])
        if nim:
            nim.colorspace_settings.name = "Non-Color"
            t = nt.nodes.new("ShaderNodeTexImage"); t.image = nim; nm = nt.nodes.new("ShaderNodeNormalMap")
            nt.links.new(t.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    rough = chan("rough") if "rough" in m else chan("smooth", 0, True) if "smooth" in m else chan("gloss", 4, True) if "gloss" in m else None
    metal = chan("metal") if "metal" in m else None
    if "spec" in m: sp = chan("spec", 4); metal = (sp > 0.5).astype(np.float32) if sp is not None else None
    if m.get("matte"):   # NG2: cloth / skin: the source's roughness (or rough_f) into [lo, hi], never metallic (the arm maps ran 0.3-0.8: a sheen on sleeves)
        lo, hi = m["matte"]; rough = lo + (hi - lo) * (rough if rough is not None else np.full(W_ * H_, m.get("rough_f", 0.9), np.float32)); metal = None; m = dict(m, metal_f=0.0)
    if rough is None and metal is None:   # NG2: no maps: the material's own factors (Blender's 0.5 default was exported)
        bsdf.inputs["Roughness"].default_value = m.get("rough_f", 0.75); bsdf.inputs["Metallic"].default_value = m.get("metal_f", 0.0)
    if rough is not None or metal is not None:
        orm = np.ones((W_ * H_, 4), np.float32)
        orm[:, 1] = rough if rough is not None else m.get("rough_f", 0.6); orm[:, 2] = metal if metal is not None else m.get("metal_f", 0.0)
        oi = newimg("orm_" + mname, orm, W_, H_); oi.colorspace_settings.name = "Non-Color"
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = oi
        sep = nt.nodes.new("ShaderNodeSeparateColor"); nt.links.new(t.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"]); nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    for ob in keep:
        for k, sl in enumerate(ob.data.materials):
            if sl and (sl.name == mname or sl.name.split(".")[0] == mname): ob.data.materials[k] = mat
for im in bpy.data.images:   # MG7: textures embedded in a GLB source (not rebuilt above) are capped at maxTex too, never upscaled
    if im.size[0] > MAXT or im.size[1] > MAXT:
        k = MAXT / max(im.size); w0 = list(im.size); im.scale(max(1, int(im.size[0] * k)), max(1, int(im.size[1] * k))); log.setdefault("scaledImages", []).append([im.name, w0, list(im.size)])
for ob in keep:   # anything still without a rebuilt material: a neutral factor (no source texture)
    for k, sl in enumerate(ob.data.materials):
        if sl and not sl.name.startswith("mg1_"): log["warn"].append("untextured material %s on %s" % (sl.name, ob.name))

# ---------------------------------------------------------------- clips -> NLA tracks (one glTF animation each)
fps = sc.render.fps / sc.render.fps_base
ad = arm.animation_data or arm.animation_data_create()
base = ad.action
for tr in list(ad.nla_tracks): ad.nla_tracks.remove(tr)
clips = {}
for name, c in cfg["clips"].items():
    if isinstance(c, list) and c and isinstance(c[0], dict):   # MG2: actions played back to back in one clip (fire + pump)
        tr = ad.nla_tracks.new(); tr.name = name; t0 = None; tot = 0
        for k, part in enumerate(c):
            act = bpy.data.actions.get(part["action"])
            if act is None: log["warn"].append("no action " + part["action"]); continue
            a0, a1 = part.get("range", act.frame_range)
            if t0 is None: t0 = a0
            stp = tr.strips.new(name + str(k), int(t0 + tot), act)
            if hasattr(stp, "action_slot") and act.slots: stp.action_slot = act.slots[0]
            stp.action_frame_start = a0; stp.action_frame_end = a1; tot += a1 - a0
        clips[name] = round(tot / fps, 4); continue
    if isinstance(c, list): act, (a0, a1) = base, c
    elif isinstance(c, dict):   # MG2: a frame range of a named action (the AR-15 set: its idle = the draw's last frame)
        act = bpy.data.actions.get(c["action"]); a0, a1 = c["range"]
        if act is None: log["warn"].append("no action " + c["action"]); continue
    else:
        act = bpy.data.actions.get(c)
        if act is None: log["warn"].append("no action " + c); continue
        a0, a1 = act.frame_range
    if a1 - a0 < 1: a1 = a0 + 1   # a one-frame pose (Idle)
    tr = ad.nla_tracks.new(); tr.name = name
    stp = tr.strips.new(name, int(a0), act)
    if hasattr(stp, "action_slot") and act.slots: stp.action_slot = act.slots[0]
    stp.action_frame_start = a0; stp.action_frame_end = a1
    clips[name] = round((a1 - a0) / fps, 4)
ad.action = None

# ---------------------------------------------------------------- export
for ob in bpy.context.view_layer.objects: ob.select_set(False)
arm.select_set(True); rootObj.select_set(True)
for ob in keep:   # XD1: a source may lock its meshes against selection (hide_select): select_set then does nothing and the
    ob.hide_select = False; ob.hide_set(False); ob.select_set(True)   # selection-only export drops them (the XD's arms were lost this way)
missed = [ob.name for ob in keep if not ob.select_get()]
if missed: raise SystemExit("MG1 kept meshes not selected for export: " + ", ".join(missed))
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_skins=True, export_animations=True,
                          export_animation_mode="NLA_TRACKS", export_force_sampling=True, export_apply=False,
                          export_cameras=False, export_lights=False, export_image_format="AUTO", export_def_bones=cfg.get("defOnly", False))   # MG2: Rigify rigs keep only their deform bones
meta = {"gunJoint": gunBone, "clips": clips, "fps": fps, "points": {k: b2g(v) for k, v in pts.items()}, "log": log,
        "tris": sum(len(p.vertices) - 2 for ob in keep for p in ob.data.polygons)}
json.dump(meta, open(os.path.splitext(out)[0] + ".json", "w"), indent=1)
print("MG1ANIM", json.dumps(meta))
