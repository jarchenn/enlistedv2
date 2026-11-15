# VH1 Blender background converter: the supplied Sketchfab Willys MB GLB -> a cleaned GLB for --import-vehicle-models.
# - metres (the source is authored at 1/210 scale: 3.36 m long) and the game's axes (front +z after the glTF export)
# - meshes regrouped under role nodes the bake maps through "nodeParts": hull, wheel_fl/fr/rl/rr, pintle, steer (rim, spokes and
#   hub islands of the steering wheel only; the column and pedals stay hull), windshield, stowage (bags, jerrycans,
#   axe, shovel, rope)
# - dropped: the display plinth (base) and the two BLEND glass meshes (the vehicle bake has no transparent pass)
# - the tyres are decimated (14.5k triangles each in the source)
# usage: blender -b --factory-startup --python convert_jeep_glb.py -- <in.glb> <out.glb>
import bpy, sys, bmesh
from mathutils import Matrix, Vector
a = sys.argv[sys.argv.index('--') + 1:]
src, out = a[0], a[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
bpy.context.view_layer.update()
K = 3.36 / 0.016
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in meshes:   # bake the world transform (and the scale) into the data
    M = Matrix.Scale(K, 4) @ o.matrix_world
    o.data = o.data.copy(); o.data.transform(M)
    o.parent = None; o.matrix_basis = Matrix.Identity(4)
for o in list(bpy.data.objects):
    if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
def drop(o): bpy.data.objects.remove(o, do_unlink=True)
STOW = ('Bag_', 'Jerrycan', 'Jerican', 'jeep2nd_axe', 'jeep2nd_shovel', 'Rope')
groups = {}
def put(o, role):
    if role not in groups:
        e = bpy.data.objects.new(role, None); bpy.context.scene.collection.objects.link(e); groups[role] = e
    o.parent = groups[role]
for o in list(bpy.data.objects):
    if o.type != 'MESH': continue
    n = o.name
    if n.startswith('base_') or 'glassJeep' in n: drop(o); continue
    if n.startswith('Tire_') and not n.startswith('Tire_safe'):
        c = sum((v.co for v in o.data.vertices), Vector()) / len(o.data.vertices)   # Blender: -y front, +x left (LHD driver)
        put(o, 'wheel_' + ('f' if c.y < 0 else 'r') + ('l' if c.x > 0 else 'r'))
    elif n.startswith('Parebrise'): put(o, 'windshield')
    elif any(s in n for s in STOW): put(o, 'stowage')
    elif n.startswith('Volant'):
        # split: islands above 0.9 m (Blender z) are the wheel itself; the column / pedal islands below stay hull
        bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
        seen, rim = set(), set()
        for v in bm.verts:
            if v.index in seen: continue
            st, cur = [v], []; seen.add(v.index)
            while st:
                x = st.pop(); cur.append(x)
                for e in x.link_edges:
                    y = e.other_vert(x)
                    if y.index not in seen: seen.add(y.index); st.append(y)
            if min(x.co.z for x in cur) >= 0.9: rim.update(x.index for x in cur)
        r = o.copy(); r.data = o.data.copy(); r.name = 'steer_rim'; bpy.context.scene.collection.objects.link(r)
        for ob, keepRim in ((o, False), (r, True)):
            b2 = bmesh.new(); b2.from_mesh(ob.data); b2.verts.ensure_lookup_table()
            bmesh.ops.delete(b2, geom=[v for v in b2.verts if (v.index in rim) != keepRim], context='VERTS')
            b2.to_mesh(ob.data); b2.free()
        put(o, 'hull'); put(r, 'steer')
        print('STEER rim verts', len(rim))
    else: put(o, 'hull')
    if 'TIRE' in n:
        md = o.modifiers.new('dec', 'DECIMATE'); md.ratio = 0.3
        bpy.context.view_layer.objects.active = o; bpy.ops.object.modifier_apply(modifier='dec')
# added geometry (not in the source): the pintle post of the mounted-MG variant, between the front seats
# (pre-bake position; the bake shifts z by about -0.18 m)
bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.04, depth=1.0, location=(0, 0.02, 1.1))
pin = bpy.context.object; pin.name = 'pintle_post'; m = bpy.data.materials.new('pintle'); m.use_nodes = True
m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.12, 0.13, 0.08, 1); pin.data.materials.append(m); put(pin, 'pintle')
for o in bpy.data.objects: o.select_set(True)
print('GROUPS', {k: [c.name for c in v.children] for k, v in groups.items()})
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_yup=True, export_animations=False, export_image_format='AUTO')
print('WROTE', out)
