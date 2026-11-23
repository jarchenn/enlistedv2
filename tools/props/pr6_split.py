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
elif src.lower().endswith(".obj"): bpy.ops.wm.obj_import(filepath=src)   # PR9: a Collada source converted first (Blender 5 has no .dae importer)
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
        objs = [copy_of(o) for o in SRC] if P.get("objects") is None else [copy_of(byname[n]) for n in P["objects"]]   # PR10: None = every mesh
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
    if P.get("keep_box"):   # PR10: crop to a box (one bay of a fence run): bisect on its four sides, the outside dropped
        x0, x1, y0, y1 = P["keep_box"][:4]
        bm = bmesh.new(); bm.from_mesh(ob.data)
        for co, no in (((x0, 0, 0), (-1, 0, 0)), ((x1, 0, 0), (1, 0, 0)), ((0, y0, 0), (0, -1, 0)), ((0, y1, 0), (0, 1, 0))):
            bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=co, plane_no=no, clear_outer=True)
        bm.to_mesh(ob.data); bm.free()
    for pfx, x0, x1, y0, y1, z0, z1 in P.get("delete_in", []):   # PR10: one material's faces inside a box (the boards across a doorway)
        bm = bmesh.new(); bm.from_mesh(ob.data)
        mi = {i for i, sl in enumerate(ob.material_slots) if sl.material and sl.material.name.startswith(pfx)}
        gone = [f for f in bm.faces if f.material_index in mi and x0 <= f.calc_center_median().x <= x1 and y0 <= f.calc_center_median().y <= y1 and z0 <= f.calc_center_median().z <= z1]
        bmesh.ops.delete(bm, geom=gone, context="FACES"); bm.to_mesh(ob.data); bm.free()
    for x0, x1, y0, y1, z0, z1 in P.get("cut_out", []):   # VH1: a hole through a one-sided shell where a window / door is only painted on: the faces near the box split on its four sides across the wall, the inside dropped
        bm = bmesh.new(); bm.from_mesh(ob.data)
        along = 0 if (x1 - x0) >= (y1 - y0) else 1
        lo, hi = Vector((x0, y0, z0)), Vector((x1, y1, z1))
        def near(f, m=0.05):
            vs = [v.co for v in f.verts]
            return all(min(v[k] for v in vs) <= hi[k] + m and max(v[k] for v in vs) >= lo[k] - m for k in range(3))
        for co, no in ((lo, along), (hi, along), (lo, 2), (hi, 2)):
            g = [f for f in bm.faces if near(f)]
            if not g: continue
            n = [0, 0, 0]; n[no] = 1
            gs = {e for f in g for e in f.edges} | {v for f in g for v in f.verts} | set(g)
            bmesh.ops.bisect_plane(bm, geom=list(gs), plane_co=co, plane_no=n)
        gone = [f for f in bm.faces if all(lo[k] - 1e-4 <= f.calc_center_median()[k] <= hi[k] + 1e-4 for k in range(3))]
        bmesh.ops.delete(bm, geom=gone, context="FACES"); bm.to_mesh(ob.data); bm.free()
    FE = {}
    if P.get("plaster") or P.get("frame"):   # VH1d: the House Village restyle (tools/props/vh1_style.py)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import vh1_style
        if P.get("plaster"): FE = vh1_style.plaster(ob, P["plaster"], job["out"])
        if P.get("frame"): ob, nfr = vh1_style.frame(ob, P["frame"], FE); print("PR6 frame %s: %d members" % (P["id"], nfr))
    if P.get("matte"):   # VH1c: the model's own materials dull (its roofs read as wet / reflective): no roughness / metal maps, rough, not metal
        for sl in ob.material_slots:
            m = sl.material
            if not m or not m.use_nodes: continue
            bs = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
            if not bs: continue
            for k in ("Roughness", "Metallic"):
                for l in list(bs.inputs[k].links): m.node_tree.links.remove(l)
            bs.inputs["Roughness"].default_value = P["matte"]; bs.inputs["Metallic"].default_value = 0.0
    if P.get("roof"):   # VH1c: a one-sided roof given its thickness (the up-facing faces copied out, solidified downward with rims, joined
        # back), optionally re-textured (thatch) with UVs along the slope at real size
        RF = P["roof"]; me = ob.data
        from mathutils.bvhtree import BVHTree
        nz0, z0_ = RF.get("min_nz", 0.3), RF.get("min_z", 1.0)
        bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
        ups = [f for f in bm.faces if f.normal.z > nz0 and f.calc_center_median().z > z0_]
        downs = [f for f in bm.faces if f.normal.z < -nz0 and f.calc_center_median().z > z0_]
        if ups and downs:   # a down-facing roof face with a top surface just above it is that roof's underside (inside the
            # thickened roof: dropped); one without is a top modelled facing down (turned up)
            vs = [v.co.copy() for v in bm.verts]; tree = BVHTree.FromPolygons(vs, [[v.index for v in f.verts] for f in ups])
            under = [f for f in downs if tree.ray_cast(f.calc_center_median() + Vector((0, 0, 0.01)), Vector((0, 0, 1)), RF.get("under", 0.8))[0] is not None]
            us = set(under); flip = [f for f in downs if f not in us]
            if flip: bmesh.ops.reverse_faces(bm, faces=flip)
            bmesh.ops.delete(bm, geom=under, context="FACES")
            print("PR6 roof %s: %d undersides dropped, %d tops turned up" % (P["id"], len(under), len(flip)))
        bm.to_mesh(me); bm.free(); me.update()
        roof = [p.index for p in me.polygons if p.normal.z > nz0 and p.center.z > z0_]
        if roof:
            if RF.get("tex"):
                mat = bpy.data.materials.new("vh1_roof"); mat.use_nodes = True; N, L = mat.node_tree.nodes, mat.node_tree.links
                bs = next(n for n in N if n.type == "BSDF_PRINCIPLED"); bs.inputs["Roughness"].default_value = 0.95; bs.inputs["Metallic"].default_value = 0.0
                ti = N.new("ShaderNodeTexImage"); ti.image = bpy.data.images.load(os.path.join(RF["tex"], "color.jpg"), check_existing=True); L.new(ti.outputs[0], bs.inputs["Base Color"])
                if os.path.exists(os.path.join(RF["tex"], "normal.jpg")):
                    nt = N.new("ShaderNodeTexImage"); nt.image = bpy.data.images.load(os.path.join(RF["tex"], "normal.jpg"), check_existing=True); nt.image.colorspace_settings.name = "Non-Color"
                    nm = N.new("ShaderNodeNormalMap"); L.new(nt.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
                me.materials.append(mat); rmi = len(me.materials) - 1; t = RF["tile"]; uvl = me.uv_layers.active
                for i in roof:
                    p = me.polygons[i]; p.material_index = rmi; n = p.normal
                    r = n.cross(Vector((0, 0, 1))); r = r.normalized() if r.length > 1e-6 else Vector((1, 0, 0)); s_ = n.cross(r).normalized()
                    for li in p.loop_indices:
                        co = me.vertices[me.loops[li].vertex_index].co; uvl.data[li].uv = (co.dot(r) / t, co.dot(s_) / t)
            bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table(); rs = set(roof)
            keep = [f for f in bm.faces if f.index not in rs]
            rb = bm.copy(); rb.faces.ensure_lookup_table()
            bmesh.ops.delete(rb, geom=[f for f in rb.faces if f.index not in rs], context="FACES")
            bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index in rs], context="FACES"); bm.to_mesh(me); bm.free()
            if RF.get("sweep"):   # VH1d: eaves turned up
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import vh1_style
                print("PR6 roof %s: %d eave verts swept" % (P["id"], vh1_style.sweep(rb, *RF["sweep"])))
            rme = bpy.data.meshes.new("roof"); rb.to_mesh(rme); rb.free()
            for m in me.materials: rme.materials.append(m)
            ro = bpy.data.objects.new("roof", rme); bpy.context.scene.collection.objects.link(ro)
            md = ro.modifiers.new("sol", "SOLIDIFY"); md.thickness = RF["thick"]; md.offset = -1; md.use_rim = True; md.use_even_offset = True
            if RF.get("rim_tex") and RF.get("tex"):   # VH1d: the roof's edges and underside in wood (barge boards, soffits)
                rme.materials.append(vh1_style.material("vh1_oak", RF["rim_tex"])); md.material_offset = md.material_offset_rim = len(rme.materials) - 1 - rmi
            for o_ in bpy.context.selected_objects: o_.select_set(False)
            bpy.context.view_layer.objects.active = ro; ro.select_set(True); bpy.ops.object.modifier_apply(modifier=md.name)
            ob = join([ob, ro])
            print("PR6 roof %s: %d faces, %.2f thick%s" % (P["id"], len(roof), RF["thick"], (", " + os.path.basename(RF["tex"])) if RF.get("tex") else ""))
    if P.get("add_boxes"):   # PR10: walls / floors / ceilings a model lacks: textured boxes (source units), UVs at world scale
        add = []
        for i, B in enumerate(P["add_boxes"]):
            x0, x1, y0, y1, z0, z1 = B["box"]; t = B.get("tile", 2.0)
            mat = bpy.data.materials.get("pr10_" + os.path.basename(B["tex"]))
            if not mat:
                mat = bpy.data.materials.new("pr10_" + os.path.basename(B["tex"])); mat.use_nodes = True; N, L = mat.node_tree.nodes, mat.node_tree.links
                bs = next(n for n in N if n.type == "BSDF_PRINCIPLED"); bs.inputs["Roughness"].default_value = 0.85
                im = N.new("ShaderNodeTexImage"); im.image = bpy.data.images.load(os.path.join(B["tex"], "color.jpg"), check_existing=True); L.new(im.outputs[0], bs.inputs["Base Color"])
                if os.path.exists(os.path.join(B["tex"], "normal.jpg")):
                    nt = N.new("ShaderNodeTexImage"); nt.image = bpy.data.images.load(os.path.join(B["tex"], "normal.jpg"), check_existing=True); nt.image.colorspace_settings.name = "Non-Color"
                    nm = N.new("ShaderNodeNormalMap"); L.new(nt.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
            me = bpy.data.meshes.new("pr10box%d" % i); bm = bmesh.new()
            V = [bm.verts.new((x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]   # index = x + 2y + 4z
            for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):   # -z, +z, -y, +y, -x, +x (outward)
                bm.faces.new([V[k] for k in q])
            uv = bm.loops.layers.uv.new(ob.data.uv_layers.active.name if ob.data.uv_layers.active else "UVMap"); bm.normal_update()   # the model's UV name: join keeps one layer
            for f in bm.faces:
                n = f.normal; ax = max(range(3), key=lambda k: abs(n[k])); u, v = [k for k in range(3) if k != ax]
                for l in f.loops: l[uv].uv = (l.vert.co[u] / t, l.vert.co[v] / t)
            bm.to_mesh(me); bm.free(); o = bpy.data.objects.new("pr10box%d" % i, me); o.data.materials.append(mat)
            bpy.context.scene.collection.objects.link(o); add.append(o)
        if add: ob = join([ob] + add)
    if P.get("double_mats"):   # PR10: one-sided boards read black from inside: a flipped copy of their faces, 4 mm behind
        bm = bmesh.new(); bm.from_mesh(ob.data)
        mi = {i for i, sl in enumerate(ob.material_slots) if sl.material and any(sl.material.name.startswith(n) for n in P["double_mats"])}
        src_f = [f for f in bm.faces if f.material_index in mi]
        dup = bmesh.ops.duplicate(bm, geom=src_f)["geom"]
        nf = [g for g in dup if isinstance(g, bmesh.types.BMFace)]; nset = set(nf)
        for v in {v for f in nf for v in f.verts}:   # each new vertex once, along its faces' mean normal
            n = sum((f.normal for f in v.link_faces if f in nset), Vector()); v.co -= n.normalized() * 0.004 if n.length else n
        bmesh.ops.reverse_faces(bm, faces=nf)
        bm.to_mesh(ob.data); bm.free()
    if P.get("shrink"):   # PR8: scale the vertices inside a footprint about its foot (the gas station's dispensers)
        for x0, x1, y0, y1, f in P["shrink"]:
            vs = [v for v in ob.data.vertices if x0 <= v.co.x <= x1 and y0 <= v.co.y <= y1]
            if not vs: continue
            cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, min(v.co.z for v in vs)
            for v in vs: v.co = Vector((cx + (v.co.x - cx) * f, cy + (v.co.y - cy) * f, cz + (v.co.z - cz) * f))
    if P.get("cut_below") is not None:   # PR10: a scan's ground dips below its base: everything under this height dropped
        bm = bmesh.new(); bm.from_mesh(ob.data)
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, P["cut_below"]), plane_no=(0, 0, 1), clear_inner=True)
        bm.to_mesh(ob.data); bm.free()
    if P.get("cut_above") is not None:   # PR9: everything above this source height cut off and the cut capped (a stove's long flue)
        bm = bmesh.new(); bm.from_mesh(ob.data)
        r = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, P["cut_above"]), plane_no=(0, 0, 1), clear_outer=True)
        bmesh.ops.holes_fill(bm, edges=[e for e in r["geom_cut"] if isinstance(e, bmesh.types.BMEdge)], sides=0)
        bm.to_mesh(ob.data); bm.free()
    if P.get("solidify"):   # PR8: single-sided walls get their inside faces (walk-in), thickness inward
        md = ob.modifiers.new("sol", "SOLIDIFY"); md.thickness = P["solidify"]; md.offset = -1; md.use_rim = True
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.modifier_apply(modifier=md.name)
    auto_boxes = []
    if P.get("auto_colliders"):   # PR10: colliders from the model's own walls and floors (source units): its axis-aligned faces
        # grouped by plane, rasterised on a grid and merged into rectangles; vertical faces -> thin walls, up-facing ->
        # slabs under them (floors, treads, walkways); down-facing and sloped faces ignored. Holes in the faces (doorways,
        # windows, breaches) stay open by construction; materials in "exclude" (glass, boards, debris) give none.
        AC = P["auto_colliders"]; cell = AC.get("cell", 0.25); th = AC.get("thick", 0.12)
        excl = {i for i, sl in enumerate(ob.material_slots) if sl.material and any(sl.material.name.startswith(n) for n in AC.get("exclude", []))}
        planes = {}
        me = ob.data
        for p in me.polygons:
            if p.material_index in excl: continue
            n = p.normal; k = max(range(3), key=lambda a: abs(n[a]))
            if abs(n[k]) < 0.97 or (k == 2 and n[2] < 0): continue
            u, v = [a for a in range(3) if a != k]
            d = round(p.center[k] / 0.05) * 0.05
            pts = [(me.vertices[i].co[u], me.vertices[i].co[v]) for i in p.vertices]
            us = [q[0] for q in pts]; vs = [q[1] for q in pts]
            cells = planes.setdefault((k, d), set())
            for i in range(int(min(us) // cell), int(max(us) // cell) + 1):
                cu = (i + 0.5) * cell
                if cu < min(us) or cu > max(us): continue
                for j in range(int(min(vs) // cell), int(max(vs) // cell) + 1):
                    cv = (j + 0.5) * cell; inside = False
                    if cv < min(vs) or cv > max(vs): continue
                    for a in range(len(pts)):   # point in polygon (even-odd)
                        (x1, y1), (x2, y2) = pts[a], pts[a - 1]
                        if (y1 > cv) != (y2 > cv) and cu < (x2 - x1) * (cv - y1) / (y2 - y1) + x1: inside = not inside
                    if inside: cells.add((i, j))
        for (k, d), cells in planes.items():
            if len(cells) * cell * cell < (AC.get("min_floor", 0.25) if k == 2 else AC.get("min_wall", 1.0)): continue
            left = set(cells)
            while left:   # greedy rectangles: the lowest cell, run along u, then whole rows along v
                i0, j0 = min(left, key=lambda c: (c[1], c[0])); i1 = i0
                while (i1 + 1, j0) in left: i1 += 1
                j1 = j0
                while all((i, j1 + 1) in left for i in range(i0, i1 + 1)): j1 += 1
                for i in range(i0, i1 + 1):
                    for j in range(j0, j1 + 1): left.discard((i, j))
                if (i1 - i0 + 1) * (j1 - j0 + 1) * cell * cell < (AC.get("min_rect_floor", 0.12) if k == 2 else AC.get("min_rect", 0.12)): continue
                lo3, hi3 = [0, 0, 0], [0, 0, 0]
                u, v = [a for a in range(3) if a != k]
                lo3[u], hi3[u] = i0 * cell, (i1 + 1) * cell; lo3[v], hi3[v] = j0 * cell, (j1 + 1) * cell
                lo3[k], hi3[k] = (d - th, d) if k == 2 else (d - th / 2, d + th / 2)
                auto_boxes.append([lo3[0], hi3[0], lo3[1], hi3[1], lo3[2], hi3[2]])
        print("AUTO colliders", len(auto_boxes), "from", len(planes), "planes")
    if P.get("decimate"):
        md = ob.modifiers.new("dec", "DECIMATE"); md.ratio = P["decimate"]
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.modifier_apply(modifier=md.name)
    lo, hi = bounds([ob])
    s = P.get("scale", job.get("scale", 1.0))
    if P.get("height"): s = P["height"] / max(1e-6, hi.z - lo.z)
    if P.get("length"): s = P["length"] / max(1e-6, max(hi - lo))   # PR9: a hand tool lying flat, by its real overall length
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
    src_boxes += [xform(b) for b in auto_boxes]   # PR10
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
    elif P.get("colliders") == "trunk":   # PR10: a tree: the trunk only (the vertices in its lowest 1.2 m), up to 3 m
        low = [v.co for v in ob.data.vertices if v.co.z < 1.2]
        if low:
            px = [v.x for v in low]; py = [v.y for v in low]; th = min(3.0, hi.z - lo.z)
            boxes = [[(min(px) + max(px)) / 2, th / 2, -(min(py) + max(py)) / 2, max(min(max(px) - min(px), 1.2), 0.15), th, max(min(max(py) - min(py), 1.2), 0.15)]]
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
