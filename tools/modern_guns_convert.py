# MG1 (docs/fps-weapons.md#mg1): one modern-gun source -> a static gun-only GLB for --import-weapon-models.
# MG1: keep only the gun meshes of one source, bake transforms (armatures at rest), rebuild PBR materials from the
# folder's textures at <= 1K (ORM packed, smoothness inverted, DirectX normals flipped, spec-gloss -> metal-rough),
# export a static GLB, render side / top checks and write a summary JSON.
import bpy, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector
cfg = json.load(open(sys.argv[sys.argv.index("--") + 1]))
src, out, tex = cfg["src"], cfg["out"], cfg.get("texdir", "")
bpy.ops.wm.read_factory_settings(use_empty=True)
ext = os.path.splitext(src)[1].lower()
if ext == ".blend": bpy.ops.wm.open_mainfile(filepath=src)
elif ext == ".glb": bpy.ops.import_scene.gltf(filepath=src)
else: bpy.ops.import_scene.fbx(filepath=src)
if cfg.get("poseFrame") is not None:   # MG7: a rig whose rest pose is not the gun assembled (the SCAR-H GLB): bake it posed at this frame
    if cfg.get("poseAction"):   # of this action (the Glock 43 set animates bone scale: the gun's size depends on the action)
        for o in bpy.data.objects:
            if o.type == "ARMATURE":
                ad = o.animation_data or o.animation_data_create(); act = bpy.data.actions[cfg["poseAction"]]; ad.action = act
                if hasattr(ad, "action_slot") and act.slots: ad.action_slot = act.slots[0]
    bpy.context.scene.frame_set(cfg["poseFrame"])
else:
    for o in bpy.data.objects:
        if o.animation_data: o.animation_data_clear()
        if o.type == "ARMATURE": o.data.pose_position = "REST"
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
keep = cfg["keep"]            # source mesh name -> output part name
R = Matrix.Rotation(math.radians(cfg.get("rotZ", 0)), 4, "Z") @ Matrix.Rotation(math.radians(cfg.get("rotY", 0)), 4, "Y") @ Matrix.Rotation(math.radians(cfg.get("rotX", 0)), 4, "X")
col = bpy.data.collections.new("MG1"); 
newobjs = []; missing = []
for sname, pname in keep.items():
    o = bpy.data.objects.get(sname)
    if not o or o.type != "MESH": missing.append(sname); continue
    me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), preserve_all_data_layers=False, depsgraph=dg)
    me.transform(R @ o.matrix_world)
    dm = cfg.get("dropMats", {}).get(sname)   # NG1: faces of these materials left out (the Makarov pack's parked suppressor / optic bracket)
    if dm:
        import bmesh
        bm = bmesh.new(); bm.from_mesh(me); drop = [f for f in bm.faces if f.material_index < len(me.materials) and me.materials[f.material_index] and me.materials[f.material_index].name.split(".")[0] in dm]
        bmesh.ops.delete(bm, geom=drop, context="FACES_ONLY"); bm.to_mesh(me); bm.free()
    # split by dominant vertex group (one skinned mesh holding the bolt / mag on their own bones)
    nob = bpy.data.objects.new(pname, me); newobjs.append((nob, o))
scene = bpy.data.scenes.new("MG1"); scene.collection.children.link(col)
for nob, _ in newobjs: col.objects.link(nob)
bpy.context.window.scene = scene
# vertex-group split: groups listed in cfg["split"] {group: partname}
for nob, o in list(newobjs):
    sp = cfg.get("split", {}).get(o.name)
    if not sp: continue
    vg = [g.name for g in o.vertex_groups]
    import bmesh
    # dominant group per vertex from the source mesh (same vertex order: no modifiers that change topology)
    src_me = o.data
    dom = []
    for v in src_me.vertices:
        best, bw = None, 0
        for g in v.groups:
            if g.weight > bw: best, bw = vg[g.group], g.weight
        dom.append(best)
    if len(dom) != len(nob.data.vertices): print("MG1 split skipped (vertex count changed)", o.name); continue
    for gname, pname in sp.items():
        bm = bmesh.new(); bm.from_mesh(nob.data); bm.verts.ensure_lookup_table()
        sel = [f for f in bm.faces if all(dom[v.index] == gname for v in f.verts)]
        if not sel: bm.free(); continue
        bm2 = bmesh.new(); bm2.from_mesh(nob.data); bm2.verts.ensure_lookup_table(); bm2.faces.ensure_lookup_table()
        selset = set(f.index for f in sel)
        bmesh.ops.delete(bm2, geom=[f for f in bm2.faces if f.index not in selset], context="FACES")
        m2 = bpy.data.meshes.new(pname); bm2.to_mesh(m2); bm2.free()
        bmesh.ops.delete(bm, geom=sel, context="FACES"); bm.to_mesh(nob.data); bm.free()
        for mat in nob.data.materials: m2.materials.append(mat)
        o2 = bpy.data.objects.new(pname, m2); col.objects.link(o2)
# drop loose islands hanging lowest (a sling): every island whose lowest point is within dropLow of the mesh's lowest
if cfg.get("dropLow"):
    import bmesh
    zmin = min(v.co.z for o in col.objects for v in o.data.vertices)
    for o in list(col.objects):
        bm = bmesh.new(); bm.from_mesh(o.data); bm.faces.ensure_lookup_table()
        seen = set(); drop = []
        for f in bm.faces:
            if f.index in seen: continue
            isl, st = [], [f]; seen.add(f.index)
            while st:
                g = st.pop(); isl.append(g)
                for e in g.edges:
                    for h in e.link_faces:
                        if h.index not in seen: seen.add(h.index); st.append(h)
            if min(v.co.z for g in isl for v in g.verts) < zmin + cfg["dropLow"]: drop += isl
        print("MG1 dropLow", o.name, len(drop), "faces")
        bmesh.ops.delete(bm, geom=drop, context="FACES"); bm.to_mesh(o.data); bm.free()
# ------------------------------------------------------------------ materials
MAXT = cfg.get("maxTex", 1024)
def load(name):
    if not name: return None
    p = os.path.join(tex, name)
    if not os.path.exists(p): print("MG1 missing texture", p); return None
    im = bpy.data.images.load(p, check_existing=False)
    w, h = im.size
    if max(w, h) > MAXT: s = MAXT / max(w, h); im.scale(max(1, int(w * s)), max(1, int(h * s)))
    return im
def px(im):
    a = np.empty(im.size[0] * im.size[1] * 4, np.float32); im.pixels.foreach_get(a); return a.reshape(-1, 4)
def newimg(name, a, w, h, alpha=False):
    im = bpy.data.images.new(name, w, h, alpha=alpha); im.pixels.foreach_set(a.ravel()); im.file_format = "PNG"; im.pack(); return im
def resample(a, w, h, W, H):
    if (w, h) == (W, H): return a
    a = a.reshape(h, w, 4); yi = (np.arange(H) * h // H); xi = (np.arange(W) * w // W); return a[yi][:, xi].reshape(-1, 4)
def gltf_group():
    g = bpy.data.node_groups.get("glTF Material Output")
    if g: return g
    g = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
    g.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    g.nodes.new("NodeGroupInput")
    return g
texinfo = {}
for mname, m in cfg.get("mats", {}).items():
    mat = bpy.data.materials.new("mg1_" + mname); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes["Principled BSDF"]
    info = {}
    if "factor" in m:
        bsdf.inputs["Base Color"].default_value = m["factor"] + [1]; bsdf.inputs["Metallic"].default_value = m.get("metal", 0); bsdf.inputs["Roughness"].default_value = m.get("rough", 0.5)
    alb = load(m.get("albedo"))
    if alb:
        a = px(alb); w, h = alb.size
        if m.get("specgloss"):   # convert later with spec / gloss
            pass
        a[:, 3] = 1; ai = newimg("alb_" + mname, a, w, h)
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = ai; nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"]); info["albedo"] = [w, h]
        W, H = w, h
    else: W = H = MAXT
    def chan(key, ch=0, invert=False, default=None):
        im = load(m.get(key)) if isinstance(m.get(key), str) else None
        if not im: return None
        a = resample(px(im), im.size[0], im.size[1], W, H)
        v = a[:, ch] if ch < 4 else a[:, :3] @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        return 1 - v if invert else v
    rough = chan("rough") if "rough" in m else chan("smooth", m.get("smoothCh", 0), True) if "smooth" in m else chan("gloss", 4, True) if "gloss" in m else None
    metal = chan("metal", m.get("metalCh", 0)) if "metal" in m else None
    if "spec" in m:
        s = chan("spec", 4); metal = (s > 0.5).astype(np.float32) if s is not None else None
    ao = chan("ao") if "ao" in m else None
    if m.get("dull"):   # NG4: a gun's own finish less glossy: roughness into a range per surface (bare metal by the metallic map, else
        # polymer / coated parts), metallic scaled (a pack with one metallic factor for the whole gun)
        _d = m["dull"]; _n = W * H; _r = rough if rough is not None else np.full(_n, m.get("rough_f", 0.5), np.float32)
        _m = (metal if metal is not None else np.full(_n, m.get("metal_f", 0.0), np.float32)) * _d.get("metalScale", 1.0)
        _ml, _mh = _d.get("metal", [0.55, 0.7]); _ol, _oh = _d.get("other", [0.7, 0.85])
        rough = np.where(_m >= 0.5, _ml + (_mh - _ml) * _r, _ol + (_oh - _ol) * _r).astype(np.float32); metal = _m.astype(np.float32)
    if rough is not None or metal is not None or ao is not None:
        orm = np.ones((W * H, 4), np.float32)
        orm[:, 0] = ao if ao is not None else 1
        orm[:, 1] = rough if rough is not None else m.get("rough_f", 0.6)
        orm[:, 2] = metal if metal is not None else m.get("metal_f", 0.0)
        oi = newimg("orm_" + mname, orm, W, H)
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = oi; oi.colorspace_settings.name = "Non-Color"
        sep = nt.nodes.new("ShaderNodeSeparateColor"); nt.links.new(t.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"]); nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
        if ao is not None:
            gn = nt.nodes.new("ShaderNodeGroup"); gn.node_tree = gltf_group(); nt.links.new(sep.outputs["Red"], gn.inputs["Occlusion"])
        info["orm"] = [W, H, ao is not None, rough is not None, metal is not None]
    nim = load(m.get("normal"))
    if nim:
        a = px(nim)
        if m.get("normalDX"): a[:, 1] = 1 - a[:, 1]
        ni = newimg("nrm_" + mname, a, nim.size[0], nim.size[1]); ni.colorspace_settings.name = "Non-Color"
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = ni
        nm = nt.nodes.new("ShaderNodeNormalMap"); nt.links.new(t.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
        info["normal"] = list(nim.size)
    texinfo[mname] = info
    for o in col.objects:
        for k, s in enumerate(o.data.materials):
            if s and (s.name == mname or s.name.split(".")[0] == mname): o.data.materials[k] = mat
# scale (case to real size)
if cfg.get("scale"):
    for o in col.objects: o.data.transform(Matrix.Scale(cfg["scale"], 4))
# ------------------------------------------------------------------ export + summary
for o in list(bpy.data.objects):
    if o.name not in col.objects: bpy.data.objects.remove(o)
for o in col.objects:
    if o.name.endswith(".001"): o.name = o.name[:-4]
summ = {"parts": [], "missing": missing, "tex": texinfo}
tot = 0
for o in col.objects:
    me = o.data; t = sum(len(p.vertices) - 2 for p in me.polygons); tot += t
    vs = [v.co for v in me.vertices]
    if not vs: continue
    # glTF space: (x, z, -y)
    mn = [min(v[i] for v in vs) for i in range(3)]; mx = [max(v[i] for v in vs) for i in range(3)]
    summ["parts"].append({"name": o.name, "tris": t, "mats": [m.name if m else None for m in me.materials],
                          "gltf_min": [round(mn[0], 4), round(mn[2], 4), round(-mx[1], 4)], "gltf_max": [round(mx[0], 4), round(mx[2], 4), round(-mn[1], 4)]})
summ["tris"] = tot
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_animations=False, export_skins=False, export_apply=True, export_image_format="AUTO", export_cameras=False, export_lights=False)
# side and top renders (Workbench, orthographic) for the orientation check
sc = scene; sc.render.engine = "BLENDER_WORKBENCH"; sc.render.resolution_x = 1200; sc.render.resolution_y = 500
sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "OBJECT"
import random
for k, o in enumerate(col.objects): random.seed(k); o.color = (0.3 + 0.7 * random.random(), 0.3 + 0.7 * random.random(), 0.3 + 0.7 * random.random(), 1)
allv = [o.matrix_world @ v.co for o in col.objects for v in o.data.vertices]
c = sum(allv, Vector()) / max(1, len(allv)); ext_ = max((max(v[i] for v in allv) - min(v[i] for v in allv)) for i in range(3))
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); col.objects.link(cam); sc.camera = cam
cam.data.type = "ORTHO"; cam.data.ortho_scale = ext_ * 1.1
for nm, loc, rot in [("side", c + Vector((ext_ * 3, 0, 0)), (math.pi / 2, 0, math.pi / 2)), ("top", c + Vector((0, 0, ext_ * 3)), (0, 0, 0))]:
    cam.location = loc; cam.rotation_euler = rot
    sc.render.filepath = os.path.splitext(out)[0] + "_" + nm + ".png"; bpy.ops.render.render(write_still=True)
json.dump(summ, open(os.path.splitext(out)[0] + "_summary.json", "w"), indent=1)
print("MG1 done", out, tot)
