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
else: bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.fbx(filepath=src)
sc = bpy.context.scene
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
keep = [bpy.data.objects[n] for n in cfg["keep"]]
f0 = cfg.get("restFrame", 1)
log = {"warn": []}

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
st = cfg["static"]; s = st["s"]; o_ = st["o"]
def w2b(p): g = [p[0] / s + o_[0], p[1] / s + o_[1], p[2] / s + o_[2]]; return Vector((g[0], -g[2], g[1]))
pts = {k: w2b(v) for k, v in cfg["points"].items()}
# check: the gun body's rest bbox must match the static export's (same rest pose)
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
ev = gun.evaluated_get(dg); me = ev.to_mesh()
vs = [ev.matrix_world @ v.co for v in me.vertices]; ev.to_mesh_clear()
bb = [min(v[i] for v in vs) for i in range(3)] + [max(v[i] for v in vs) for i in range(3)]
log["gunRestBlender"] = [round(x, 4) for x in bb]; log["staticGunBlender"] = st.get("gunBlender")
arm.data.pose_position = "POSE"

# ---------------------------------------------------------------- hip framing -> rig space (+ metres)
# The sources' cameras differ (Cinema 4D exports carry none), so every rig is framed the engine's way: at the first
# idle frame the sight line (rear -> front) looks straight ahead, gun-up is up, and the rear sight sits at the hip pose
# (view (0.12, -0.07, -0.15) m: viewmodel.cpp T_hip with the default hipOffset; cfg "hipRear" overrides).
idle = cfg["clips"]["idle"]
ad0 = arm.animation_data or arm.animation_data_create()
if isinstance(idle, list): fr = idle[0]
else:
    act = bpy.data.actions[idle]; ad0.action = act
    if hasattr(ad0, "action_slot") and act.slots: ad0.action_slot = act.slots[0]
    fr = act.frame_range[0]
sc.frame_set(int(fr)); bpy.context.view_layer.update()
pb = arm.pose.bones[gunBone]
D = (arm.matrix_world @ pb.matrix) @ (arm.matrix_world @ pb.bone.matrix_local).inverted()
R_, F_ = D @ pts["rear"], D @ pts["front"]
U_ = D @ w2b([cfg["points"]["rear"][0], cfg["points"]["rear"][1] + 0.05, cfg["points"]["rear"][2]])
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

# ---------------------------------------------------------------- drop everything else, move the rig
for ob in list(bpy.data.objects):
    if ob != arm and ob not in keep: bpy.data.objects.remove(ob, do_unlink=True)
for ob in keep:
    if ob.parent is None:   # unparented skinned meshes (the pistol): parent to the armature in place
        mw = ob.matrix_world.copy(); ob.parent = arm; ob.matrix_world = mw
# the armature's own clips may animate its object transform, so the frame goes on a parent node, not on the armature
rootObj = bpy.data.objects.new("vm_root", None); sc.collection.objects.link(rootObj); rootObj.matrix_world = root
arm.parent = rootObj; arm.matrix_parent_inverse = Matrix()
bpy.context.view_layer.update()

# ---------------------------------------------------------------- materials (as modern_guns_convert.py)
MAXT = cfg.get("maxTex", 1024)
def load(name):
    if not name: return None
    p = os.path.join(tex, name)
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
    rough = chan("rough") if "rough" in m else chan("smooth", 0, True) if "smooth" in m else chan("gloss", 4, True) if "gloss" in m else None
    metal = chan("metal") if "metal" in m else None
    if "spec" in m: sp = chan("spec", 4); metal = (sp > 0.5).astype(np.float32) if sp is not None else None
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
    if isinstance(c, list): act, (a0, a1) = base, c
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
for ob in keep: ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_skins=True, export_animations=True,
                          export_animation_mode="NLA_TRACKS", export_force_sampling=True, export_apply=False,
                          export_cameras=False, export_lights=False, export_image_format="AUTO")
meta = {"gunJoint": gunBone, "clips": clips, "fps": fps, "points": {k: b2g(v) for k, v in pts.items()}, "log": log,
        "tris": sum(len(p.vertices) - 2 for ob in keep for p in ob.data.polygons)}
json.dump(meta, open(os.path.splitext(out)[0] + ".json", "w"), indent=1)
print("MG1ANIM", json.dumps(meta))
