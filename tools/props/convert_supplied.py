# PR1: supplied / downloaded models -> one clean GLB per prop for the game's importer (--import-weapon-models).
# Imports the source (glb / gltf / fbx), wires loose PBR textures to the materials of an FBX that has none, applies the
# transforms, scales to metres (`--height <m>`: the real height of the object; or `--scale <k>`), rotates so its front
# faces the game's +z (`--yaw <deg>` about the up axis), drops the parts a pack ships but the prop does not need
# (`--keep <name substring>` keeps only matching meshes), sets the origin at the ground under the middle and prints the
# result's size. Run from Blender with the source treated as data only:
#   blender -b --factory-startup --disable-autoexec --python tools/props/convert_supplied.py -- <src> <out.glb> [--height h | --scale k] [--yaw d] [--tex <dir>] [--keep s]
import bpy, sys, os, math
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
opt = {}
k = 2
while k < len(a):
    opt[a[k].lstrip("-")] = a[k + 1]; k += 2
bpy.ops.wm.read_factory_settings(use_empty=True)
ext = os.path.splitext(src)[1].lower()
if ext in (".glb", ".gltf"): bpy.ops.import_scene.gltf(filepath=src)
elif ext == ".fbx": bpy.ops.import_scene.fbx(filepath=src)
else: raise SystemExit("unknown source " + src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if "keep" in opt: meshes = [o for o in meshes if opt["keep"].lower() in o.name.lower()]
for o in list(bpy.context.scene.objects):
    if o.type == "MESH" and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
# loose textures (FBX packs): base / roughness / metallic / normal / ao by file name
if "tex" in opt:
    td = opt["tex"]; files = os.listdir(td)
    def find(*keys):
        for f in files:
            l = f.lower()
            if any(kk in l for kk in keys): return os.path.join(td, f)
    paths = {"base": find("basecolor", "albedo", "diff", "color"), "rough": find("rough"), "metal": find("metal"), "normal": find("normal", "nor_gl", "nor")}
    for o in meshes:
        if not o.material_slots: o.data.materials.append(bpy.data.materials.new("mat"))
        for slot in o.material_slots:
            m = slot.material or bpy.data.materials.new("mat"); slot.material = m; m.use_nodes = True
            N, L = m.node_tree.nodes, m.node_tree.links
            bs = next((n for n in N if n.type == "BSDF_PRINCIPLED"), None) or N.new("ShaderNodeBsdfPrincipled")
            def img(p, colour):
                t = N.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(p); t.image.colorspace_settings.name = "sRGB" if colour else "Non-Color"; return t
            if paths["base"]: L.new(img(paths["base"], True).outputs[0], bs.inputs["Base Color"])
            if paths["rough"]: L.new(img(paths["rough"], False).outputs[0], bs.inputs["Roughness"])
            if paths["metal"]: L.new(img(paths["metal"], False).outputs[0], bs.inputs["Metallic"])
            if paths["normal"]:
                nm = N.new("ShaderNodeNormalMap"); L.new(img(paths["normal"], False).outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
# one object, transforms applied
for o in bpy.context.selected_objects: o.select_set(False)
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
for o in meshes:
    if o.parent: o.matrix_world = o.matrix_world.copy()
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.join(); ob = bpy.context.object
for o in list(bpy.context.scene.objects):
    if o != ob: bpy.data.objects.remove(o, do_unlink=True)
def bounds():
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs))); hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return lo, hi
lo, hi = bounds()
s = float(opt["scale"]) if "scale" in opt else (float(opt["height"]) / max(1e-6, hi.z - lo.z) if "height" in opt else 1.0)
ob.scale = (s, s, s); ob.rotation_euler = (0, 0, math.radians(float(opt.get("yaw", 0))))
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
lo, hi = bounds()
c = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
for v in ob.data.vertices: v.co -= c
lo, hi = bounds()
print("PR1 size x %.3f y(up) %.3f z %.3f  scale %.5f  tris %d" % (hi.x - lo.x, hi.z - lo.z, hi.y - lo.y, s, sum(len(p.vertices) - 2 for p in ob.data.polygons)))
# simple collision: one box per connected piece (the largest 12; specks dropped), game axes [cx, cy, cz, sx, sy, sz]
import bmesh, json
bm = bmesh.new(); bm.from_mesh(ob.data); bm.verts.ensure_lookup_table()
seen, boxes = set(), []
for v0 in bm.verts:
    if v0.index in seen: continue
    stack, pts = [v0], []
    seen.add(v0.index)
    while stack:
        v = stack.pop(); pts.append(v.co.copy())
        for e in v.link_edges:
            w = e.other_vert(v)
            if w.index not in seen: seen.add(w.index); stack.append(w)
    l = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts))); h = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    d = h - l
    if d.length < 0.12: continue
    boxes.append([(l.x + h.x) / 2, (l.z + h.z) / 2, -(l.y + h.y) / 2, max(d.x, 0.04), max(d.z, 0.04), max(d.y, 0.04)])
bm.free()
boxes.sort(key=lambda b: -b[3] * b[4] * b[5]); boxes = [[round(x, 3) for x in b] for b in boxes[:12]]
os.makedirs(os.path.dirname(out), exist_ok=True)
json.dump({"size": [round(hi.x - lo.x, 3), round(hi.z - lo.z, 3), round(hi.y - lo.y, 3)], "colliders": boxes}, open(os.path.splitext(out)[0] + ".json", "w"))
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=False, export_apply=True, export_yup=True)
print("PR1 wrote", out)
