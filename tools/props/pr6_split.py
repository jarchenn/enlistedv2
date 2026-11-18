# PR6: split one supplied source (glb / fbx) into one clean GLB per game asset, in one Blender session.
# Run by tools/props/pr6_assets.py (Blender background, the source treated as data only):
#   blender -b --factory-startup --disable-autoexec --python tools/props/pr6_split.py -- <job.json>
# job: {"src", "tex" (a loose texture for an FBX without one), "scale", "out", "pieces": [{"id", "objects" | "clusters",
#       "chain": [[name, gap after]], "yaw", "height", "scale", "decimate", "stack": [[dz fraction, yaw]], "colliders": "box" | "parts"}]}
# Each piece: its meshes copied, transforms applied, scaled (piece scale or its real height), turned (yaw about up),
# origin on the ground under the middle, exported with its textures; a side JSON gives size, tris and colliders in game
# axes (x right, y up, z toward the viewer = Blender -y).
import bpy, bmesh, sys, os, json, math
from mathutils import Vector, Matrix

job = json.load(open(sys.argv[sys.argv.index("--") + 1:][0]))
bpy.ops.wm.read_factory_settings(use_empty=True)
src = job["src"]
if src.lower().endswith((".glb", ".gltf")): bpy.ops.import_scene.gltf(filepath=src)
else: bpy.ops.import_scene.fbx(filepath=src)
SRC =[o for o in bpy.context.scene.objects if o.type == "MESH"]
if job.get("tex"):   # the FBX packs ship their texture beside the model, unlinked
    img = bpy.data.images.load(job["tex"])
    for o in SRC:
        if not o.material_slots: o.data.materials.append(bpy.data.materials.new("mat"))
        for s in o.material_slots:
            m = s.material or bpy.data.materials.new("mat"); s.material = m; m.use_nodes = True
            N, L = m.node_tree.nodes, m.node_tree.links
            bs = next((n for n in N if n.type == "BSDF_PRINCIPLED"), None) or N.new("ShaderNodeBsdfPrincipled")
            t = N.new("ShaderNodeTexImage"); t.image = img; L.new(t.outputs[0], bs.inputs["Base Color"])
            if img.channels == 4: L.new(t.outputs[1], bs.inputs["Alpha"])
if job.get("matmap"):   # PR7: {material name: {"Base Color": file, "Normal": file}} in the texture folder
    td = job["texfolder"]
    for o in SRC:
        for s_ in o.material_slots:
            m = s_.material
            if not m or m.name not in job["matmap"]: continue
            m.use_nodes = True; N, L = m.node_tree.nodes, m.node_tree.links
            bs = next((n for n in N if n.type == "BSDF_PRINCIPLED"), None) or N.new("ShaderNodeBsdfPrincipled")
            for inp, f in job["matmap"][m.name].items():
                t = N.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(os.path.join(td, f), check_existing=True)
                t.image.colorspace_settings.name = "sRGB" if inp == "Base Color" else "Non-Color"
                if inp == "Normal":
                    nm = N.new("ShaderNodeNormalMap"); L.new(t.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
                else: L.new(t.outputs[0], bs.inputs[inp])
if job.get("texdir"):   # PR7: a full PBR set beside the FBX (base colour / roughness / metallic / normal, by file name)
    td = job["texdir"]; files = os.listdir(td)
    def find(*keys):
        return next((os.path.join(td, f) for f in files if any(k in f.lower() for k in keys)), None)
    maps = {"Base Color": find("base_color", "basecolor", "albedo"), "Roughness": find("rough"), "Metallic": find("metal"), "Normal": find("normal")}
    for o in SRC:
        for s in o.material_slots:
            m = s.material; m.use_nodes = True; N, L = m.node_tree.nodes, m.node_tree.links
            bs = next((n for n in N if n.type == "BSDF_PRINCIPLED"), None) or N.new("ShaderNodeBsdfPrincipled")
            for inp, p in maps.items():
                if not p: continue
                t = N.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(p, check_existing=True)
                t.image.colorspace_settings.name = "sRGB" if inp == "Base Color" else "Non-Color"
                if inp == "Normal":
                    nm = N.new("ShaderNodeNormalMap"); L.new(t.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
                else: L.new(t.outputs[0], bs.inputs[inp])
bpy.context.view_layer.update()
MW = {o.name: o.matrix_world.copy() for o in SRC}   # world transforms (the Sketchfab root's turn and unit scale included)
for o in SRC: o.parent = None; o.matrix_world = MW[o.name]   # no parents, so a piece can be cut from anywhere in the scene


def clusters(o):
    """Loose parts of a mesh merged when their boxes touch (1 cm): the pack's separate items, in a stable order."""
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table(); M = o.matrix_world
    seen, parts = set(), []
    for v0 in bm.verts:
        if v0.index in seen: continue
        st, ids = [v0], []; seen.add(v0.index)
        while st:
            v = st.pop(); ids.append(v.index)
            for e in v.link_edges:
                w = e.other_vert(v)
                if w.index not in seen: seen.add(w.index); st.append(w)
        pts = [M @ bm.verts[i].co for i in ids]
        parts.append([[min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)], set(ids)])
    e, merged = 0.01, True
    while merged:
        merged = False
        for i in range(len(parts)):
            for j in range(i + 1, len(parts)):
                A, B = parts[i], parts[j]
                if all(A[0][k] - e <= B[1][k] and B[0][k] - e <= A[1][k] for k in range(3)):
                    parts[i] = [[min(A[0][k], B[0][k]) for k in range(3)], [max(A[1][k], B[1][k]) for k in range(3)], A[2] | B[2]]; parts.pop(j); merged = True; break
            if merged: break
    bm.free()
    return [p[2] for p in sorted(parts, key=lambda p: (round(-p[1][2], 0), p[0][0]))]


CL = {}
def copy_of(o, keep=None):
    c = o.copy(); c.data = o.data.copy(); bpy.context.scene.collection.objects.link(c)
    if keep is not None:   # one cluster of a merged mesh: drop every other vertex
        bm = bmesh.new(); bm.from_mesh(c.data); bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context="VERTS"); bm.to_mesh(c.data); bm.free()
    return c


def bounds(objs):
    vs = [o.matrix_world @ v.co for o in objs for v in o.data.vertices]
    return Vector([min(v[k] for v in vs) for k in range(3)]), Vector([max(v[k] for v in vs) for k in range(3)])


def join(objs):
    for o in bpy.context.selected_objects: o.select_set(False)
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(objs) > 1: bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def crop_textures(ob):
    """A pack shares one big atlas between its pieces: give this piece its own copy of each texture cropped to the
    rectangle its UVs use (2 % margin) and remap the UVs, so a sign does not ship the whole sheet."""
    import numpy as np
    me = ob.data; uv = me.uv_layers.active
    if not uv: return
    for mi, slot in enumerate(ob.material_slots):
        m = slot.material
        if not m or not m.use_nodes: continue
        loops = [li for p in me.polygons if p.material_index == mi for li in p.loop_indices]
        if not loops: continue
        us = [uv.data[li].uv[0] for li in loops]; vs = [uv.data[li].uv[1] for li in loops]
        u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
        if u0 < -0.001 or v0 < -0.001 or u1 > 1.001 or v1 > 1.001 or (u1 - u0) * (v1 - v0) > 0.6:   # tiled, or most of the sheet: no crop
            if not m.get("pr6_low"): continue
            u0, u1, v0, v1 = 0.0, 1.0, 0.0, 1.0   # (a low-detail part still gets its own, smaller copy)
        mg = 0.02 * max(u1 - u0, v1 - v0) + 0.004
        u0, u1, v0, v1 = max(0, u0 - mg), min(1, u1 + mg), max(0, v0 - mg), min(1, v1 + mg)
        m2 = m.copy(); slot.material = m2; rect = None
        for n in m2.node_tree.nodes:
            if n.type != "TEX_IMAGE" or not n.image or not n.image.size[0]: continue
            im = n.image; w, h = im.size
            x0, x1, y0, y1 = int(u0 * w), max(int(u0 * w) + 4, int(np.ceil(u1 * w))), int(v0 * h), max(int(v0 * h) + 4, int(np.ceil(v1 * h)))
            x1, y1 = min(x1, w), min(y1, h)
            px = np.empty(w * h * 4, dtype=np.float32); im.pixels.foreach_get(px); px = px.reshape(h, w, 4)[y0:y1, x0:x1].copy()
            ni = bpy.data.images.new(im.name + "_" + ob.name, x1 - x0, y1 - y0, alpha=True, float_buffer=False)
            ni.colorspace_settings.name = im.colorspace_settings.name; ni.alpha_mode = im.alpha_mode
            ni.pixels.foreach_set(px.ravel())
            lim = m.get("pr6_low")
            if lim and max(ni.size) > lim: k = lim / max(ni.size); ni.scale(max(4, int(ni.size[0] * k)), max(4, int(ni.size[1] * k)))
            ni.pack(); n.image = ni
            rect = (x0 / w, y0 / h, (x1 - x0) / w, (y1 - y0) / h)   # the same for every image of the material (to the pixel)
        if not rect: continue
        cu0, cv0, su, sv = rect
        for li in loops:
            a = uv.data[li].uv; uv.data[li].uv = ((a[0] - cu0) / su, (a[1] - cv0) / sv)


os.makedirs(job["out"], exist_ok=True)
for P in job["pieces"]:
    byname = {o.name: o for o in SRC}
    objs = []
    if "chain" in P:   # laid along +x, end to end with the given gaps (a wall bay and its pillar)
        x = 0.0
        for name, gap in P["chain"]:
            c = copy_of(byname[name]); lo, hi = bounds([c])
            c.location += Vector((x - lo.x, -(lo.y + hi.y) / 2, -lo.z)); bpy.context.view_layer.update()
            x += (hi.x - lo.x) + gap; objs.append(c)
    elif "multi" in P:   # PR7: clusters from several meshes (a pump: its box from one mesh, its details from another)
        for mname, idx in P["multi"]:
            o = byname[mname]
            if o.name not in CL: CL[o.name] = clusters(o)
            objs.append(copy_of(o, set().union(*[CL[o.name][i] for i in idx])))
    elif "clusters" in P:
        o = byname[P["mesh"]]
        if o.name not in CL: CL[o.name] = clusters(o)
        objs = [copy_of(o, CL[o.name][i]) for i in P["clusters"]]
    else:
        objs = [copy_of(byname[n]) for n in P["objects"]]
    for mname, idx in P.get("delete_clusters", []):   # PR8: a model's own door leaf / glass goes (the game fits openable ones)
        o = byname[mname]
        if o.name not in CL: CL[o.name] = clusters(o)
        gone = set().union(*[CL[o.name][i] for i in idx])
        for c in objs:
            if c.data.name.startswith(o.data.name) and len(c.data.vertices) == len(o.data.vertices):
                bm = bmesh.new(); bm.from_mesh(c.data); bm.verts.ensure_lookup_table()
                bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index in gone], context="VERTS"); bm.to_mesh(c.data); bm.free()
    ob = join(objs)
    if P.get("delete_mats"):   # PR8: faces of these materials removed (door panels, window glass): the openings behind them open up
        bm = bmesh.new(); bm.from_mesh(ob.data)
        kill = {i for i, sl in enumerate(ob.material_slots) if sl.material and any(sl.material.name.startswith(n) for n in P["delete_mats"])}
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in kill], context="FACES"); bm.to_mesh(ob.data); bm.free()
    if P.get("shrink"):   # PR8: scale the vertices inside a footprint about its foot (the gas station's dispensers)
        for x0, x1, y0, y1, f in P["shrink"]:
            vs = [v for v in ob.data.vertices if x0 <= v.co.x <= x1 and y0 <= v.co.y <= y1]
            if not vs: continue
            cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, min(v.co.z for v in vs)
            for v in vs: v.co = Vector((cx + (v.co.x - cx) * f, cy + (v.co.y - cy) * f, cz + (v.co.z - cz) * f))
    if P.get("solidify"):   # PR8: single-sided walls get their inside faces (walk-in), thickness inward
        md = ob.modifiers.new("sol", "SOLIDIFY"); md.thickness = P["solidify"]; md.offset = -1; md.use_rim = True
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.modifier_apply(modifier=md.name)
    if P.get("decimate"):
        md = ob.modifiers.new("dec", "DECIMATE"); md.ratio = P["decimate"]
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.modifier_apply(modifier=md.name)
    lo, hi = bounds([ob])
    s = P.get("scale", job.get("scale", 1.0))
    if P.get("height"): s = P["height"] / max(1e-6, hi.z - lo.z)
    ob.rotation_mode = "XYZ"; ob.scale = (s, s, s); ob.rotation_euler = (0, 0, math.radians(P.get("yaw", 0)))
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    lo, hi = bounds([ob]); c = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
    for v in ob.data.vertices: v.co -= c
    def xform(bx):
        R = Matrix.Rotation(math.radians(P.get("yaw", 0)), 3, "Z"); x0, x1, y0, y1, z0, z1 = bx
        cs = [R @ (Vector((x, y, z)) * s) - c for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
        l = Vector([min(q[k] for q in cs) for k in range(3)]); h = Vector([max(q[k] for q in cs) for k in range(3)])
        return [(l.x + h.x) / 2, (l.z + h.z) / 2, -(l.y + h.y) / 2, h.x - l.x, h.z - l.z, h.y - l.y]
    marks = {k: xform(bx) for k, bx in P.get("marks", {}).items()}   # PR8: openings, in game axes like the colliders
    src_boxes = []
    if P.get("boxes"):   # PR7: collision boxes measured in the source ([x0, x1, y0, y1, z0, z1], Blender axes): same scale, turn, recentring
        R = Matrix.Rotation(math.radians(P.get("yaw", 0)), 3, "Z")
        for x0, x1, y0, y1, z0, z1 in P["boxes"]:
            src_boxes.append(xform([x0, x1, y0, y1, z0, z1]))
    for dzf, yaw in P.get("stack", []):   # a second course on top (the high sandbag wall), turned for variety
        h = hi.z - lo.z
        c2 = ob.copy(); c2.data = ob.data.copy(); bpy.context.scene.collection.objects.link(c2)
        c2.rotation_mode = "XYZ"; c2.rotation_euler = (0, 0, math.radians(yaw)); c2.location = (0, 0, h * dzf)
        ob = join([ob, c2])
    lo, hi = bounds([ob])
    size = [hi.x - lo.x, hi.z - lo.z, hi.y - lo.y]
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    boxes = []
    if src_boxes: boxes = src_boxes
    elif P.get("colliders") == "post":   # a sign: the post only (the vertices in the lower half), up to the plate
        low = [v.co for v in ob.data.vertices if v.co.z < (hi.z - lo.z) * 0.45]
        if low:
            px = [v.x for v in low]; py = [v.y for v in low]; ph = (hi.z - lo.z) * 0.9
            boxes = [[(min(px) + max(px)) / 2, ph / 2, -(min(py) + max(py)) / 2, max(max(px) - min(px), 0.05), ph, max(max(py) - min(py), 0.05)]]
    elif P.get("colliders", "parts") == "box":
        boxes = [[(lo.x + hi.x) / 2, (lo.z + hi.z) / 2, -(lo.y + hi.y) / 2, size[0], size[1], size[2]]]
    else:   # one box per connected piece (the largest 12; specks dropped)
        bm = bmesh.new(); bm.from_mesh(ob.data); bm.verts.ensure_lookup_table(); seen = set()
        for v0 in bm.verts:
            if v0.index in seen: continue
            st, pts = [v0], []; seen.add(v0.index)
            while st:
                v = st.pop(); pts.append(v.co.copy())
                for e in v.link_edges:
                    w = e.other_vert(v)
                    if w.index not in seen: seen.add(w.index); st.append(w)
            l = Vector([min(p[k] for p in pts) for k in range(3)]); h = Vector([max(p[k] for p in pts) for k in range(3)]); d = h - l
            if d.length < 0.12: continue
            boxes.append([(l.x + h.x) / 2, (l.z + h.z) / 2, -(l.y + h.y) / 2, max(d.x, 0.04), max(d.z, 0.04), max(d.y, 0.04)])
        bm.free(); boxes.sort(key=lambda b: -b[3] * b[4] * b[5]); boxes = boxes[:12]
    if P.get("sign"):   # a sign: the plate's face keeps its material; its back and the rest (post, edges) get low-detail copies
        hz = max(v.co.z for v in ob.data.vertices)
        for mi in range(len(ob.material_slots)):   # three crops: the plate's face, its back, the rest (post, edges)
            m = ob.material_slots[mi].material; mb = m.copy(); mb["pr6_low"] = 512; mr = m.copy(); mr["pr6_low"] = 256   # the back and the pole need less
            ob.data.materials.append(mb); ob.data.materials.append(mr)
            back, rest = len(ob.material_slots) - 2, len(ob.material_slots) - 1
            uvl = ob.data.uv_layers.active
            for p in ob.data.polygons:
                if p.material_index != mi: continue
                vs = [uvl.data[li].uv[1] for li in p.loop_indices] if uvl else [0]
                if max(vs) - min(vs) > 0.5 or (P.get("colliders") == "post" and p.center.z < hz * 0.6) or abs(p.normal.y) < 0.5: p.material_index = rest   # the post: a tiled strip down the sheet
                elif p.normal.y > 0.5: p.material_index = back
    crop_textures(ob)
    for o in bpy.context.selected_objects: o.select_set(False)
    ob.select_set(True)
    out = os.path.join(job["out"], P["id"] + ".glb")
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_apply=True, export_yup=True)
    json.dump({"size": [round(v, 3) for v in size], "tris": tris, "colliders": [[round(x, 3) for x in b] for b in boxes], "marks": {k: [round(x, 3) for x in v] for k, v in marks.items()}}, open(out[:-4] + ".json", "w"))
    print("PR6 %-34s size %.2f x %.2f x %.2f  tris %d  boxes %d" % (P["id"], size[0], size[1], size[2], tris, len(boxes)))
    bpy.data.objects.remove(ob, do_unlink=True)
