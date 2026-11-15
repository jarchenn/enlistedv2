# VH1 Blender background converter for the supplied vehicle models -> cleaned GLBs for --import-vehicle-models.
# Every output is in metres and the game's axes (front +z after the glTF export: Blender -y front, z up), with the
# meshes grouped under role nodes that the vehicle JSON maps through "nodeParts": hull, wheel_fl / fr / rl / rr,
# steer (the steering wheel only, so it can turn), windshield, stowage. Glass is dropped (the vehicle bake has no
# transparent pass), so the driver looks through empty frames.
# usage (each source is untrusted data: run with --factory-startup --disable-autoexec, scripts kept elsewhere):
#   blender -b --factory-startup --python convert_vehicles.py -- willys  <jeep_willys_mb...glb> <out.glb>
#   blender -b --factory-startup --python convert_vehicles.py -- ww2jeep <ww2_jeep.glb> <out.glb>
#   blender -b --factory-startup --disable-autoexec "<all5 together.blend>" --python convert_vehicles.py -- opel "<collection>" <out.glb>
import bpy, sys, bmesh
import numpy as np
from mathutils import Matrix, Vector
a = sys.argv[sys.argv.index('--') + 1:]
mode = a[0]
groups = {}
def put(o, role):
    if role not in groups:
        e = bpy.data.objects.new(role, None); bpy.context.scene.collection.objects.link(e); groups[role] = e
    o.parent = groups[role]
def bake_world(objs, scale):
    for o in objs:   # world transform (and scale) into the data; unparented, identity
        M = Matrix.Scale(scale, 4) @ o.matrix_world
        o.data = o.data.copy(); o.data.transform(M)
        o.parent = None; o.matrix_basis = Matrix.Identity(4)
def centre(o): return sum((v.co for v in o.data.vertices), Vector()) / max(1, len(o.data.vertices))
def islands(bm):
    seen, out = set(), []
    for v in bm.verts:
        if v.index in seen: continue
        st, cur = [v], []; seen.add(v.index)
        while st:
            x = st.pop(); cur.append(x)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y.index not in seen: seen.add(y.index); st.append(y)
        out.append(cur)
    return out
def split_off(o, keep_idx, name):
    """new object `name` with the vertices in keep_idx (their faces); o keeps the rest"""
    r = o.copy(); r.data = o.data.copy(); r.name = name; bpy.context.scene.collection.objects.link(r)
    for ob, inside in ((o, False), (r, True)):
        b = bmesh.new(); b.from_mesh(ob.data); b.verts.ensure_lookup_table()
        bmesh.ops.delete(b, geom=[v for v in b.verts if (v.index in keep_idx) != inside], context='VERTS')
        b.to_mesh(ob.data); b.free()
    return r
def drop_faces(o, matnames):
    names = {i for i, s in enumerate(o.material_slots) if s.material and s.material.name in matnames}
    if not names: return 0
    b = bmesh.new(); b.from_mesh(o.data)
    f = [p for p in b.faces if p.material_index in names]
    bmesh.ops.delete(b, geom=f, context='FACES'); b.to_mesh(o.data); b.free()
    return len(f)
def wheel_role(c, cx, cy): return 'wheel_' + ('f' if c.y < cy else 'r') + ('l' if c.x > cx else 'r')   # Blender: -y front, +x left (LHD)

if mode == 'willys':
    # uchronia Willys MB: 1/210 scale; named nodes (Tire_*, Volant, Parebrise, bags / jerrycans / tools)
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=a[1]); bpy.context.view_layer.update()
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    bake_world(meshes, 3.36 / 0.016)
    for o in list(bpy.data.objects):
        if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    STOW = ('Bag_', 'Jerrycan', 'Jerican', 'jeep2nd_axe', 'jeep2nd_shovel', 'Rope')
    for o in list(bpy.data.objects):
        n = o.name
        if n.startswith('base_') or 'glassJeep' in n: bpy.data.objects.remove(o, do_unlink=True); continue
        if n.startswith('Tire_') and not n.startswith('Tire_safe'): put(o, wheel_role(centre(o), 0, 0))
        elif n.startswith('Parebrise'): put(o, 'windshield')
        elif any(s in n for s in STOW): put(o, 'stowage')
        elif n.startswith('Volant'):   # islands above 0.9 m are the wheel; column / pedals stay hull
            b = bmesh.new(); b.from_mesh(o.data); b.verts.ensure_lookup_table()
            rim = {x.index for isl in islands(b) if min(x.co.z for x in isl) >= 0.9 for x in isl}; b.free()
            put(split_off(o, rim, 'steer_rim'), 'steer'); put(o, 'hull')
        else: put(o, 'hull')
        if 'TIRE' in n:
            md = o.modifiers.new('dec', 'DECIMATE'); md.ratio = 0.3
            bpy.context.view_layer.objects.active = o; bpy.ops.object.modifier_apply(modifier='dec')

elif mode == 'ww2jeep':
    # leoxx300 "ww2 jeep": arbitrary units (0.44 m each: 0.73 m tyres, 1.96 m wheelbase, 1.67 m wide); unnamed
    # Cube / Cylinder parts. Tyres: the four 3836-triangle cylinders at the axles (Cylinder.019 is the spare);
    # hubs: Cylinder.003 / 008 / 016 / 018; steering wheel rim: Cylinder.002; glass: the BLEND material (Cube.026)
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=a[1]); bpy.context.view_layer.update()
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    bake_world(meshes, 0.44)
    for o in list(bpy.data.objects):
        if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    allv = np.array([tuple(v.co) for o in bpy.data.objects for v in list(o.data.vertices)[::7]])
    cx = (allv[:, 0].min() + allv[:, 0].max()) / 2
    wheels = {'Cylinder', 'Cylinder.007', 'Cylinder.009', 'Cylinder.017', 'Cylinder.003', 'Cylinder.008', 'Cylinder.016', 'Cylinder.018'}
    axle_y = sorted({round(centre(o).y, 2) for o in bpy.data.objects if o.name.split('_Material')[0] in wheels})
    cy = (axle_y[0] + axle_y[-1]) / 2
    for o in list(bpy.data.objects):
        base = o.name.split('_Material')[0]   # imported mesh objects are named <node>_<material>_0
        if any(s.material and s.material.name == 'Material.002' for s in o.material_slots):   # the BLEND glass
            bpy.data.objects.remove(o, do_unlink=True); continue
        if base in wheels: put(o, wheel_role(centre(o), cx, cy))
        elif base == 'Cylinder.002': put(o, 'steer')
        else: put(o, 'hull')
    print('WW2 centre x %.3f axle mid y %.3f' % (cx, cy))

elif mode == 'opel':
    # Opel Blitz based truck collection (one .blend, five trucks, metres): each body is one merged mesh; the tyres /
    # hubs are separate cylinders. Kept: the objects of the named collection. The steering wheel is cut out of the
    # body: the ring island in the cab (radius ~0.26 m, thin, ahead of the bed) plus the islands inside its disc.
    # the images point at folders on the author's machine; the archive ships some of them in ../textures (spaces -> _)
    import os
    tex = os.path.join(os.path.dirname(bpy.data.filepath), '..', 'textures')
    have = {f.lower(): f for f in os.listdir(tex)} if os.path.isdir(tex) else {}
    relinked = 0
    for im in bpy.data.images:
        base = im.filepath.replace(chr(92), '/').split('/')[-1].lower()
        for cand in (base, base.replace(' ', '_')):
            if cand in have: im.filepath = os.path.join(tex, have[cand]); im.reload(); relinked += 1; break
    print('RELINKED', relinked, 'of', len(bpy.data.images), 'images')
    coll = bpy.data.collections[a[1]]
    keep = set(coll.all_objects)
    for o in list(bpy.data.objects):
        if o not in keep: bpy.data.objects.remove(o, do_unlink=True)
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    for o in list(bpy.data.objects):
        if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    bake_world(meshes, 1.0)
    body = max(meshes, key=lambda o: len(o.data.polygons) if o.name.startswith('Cube') else 0)
    print('GLASS faces dropped', drop_faces(body, {'Glass', 'dirty_glass', 'dirty windshield', 'Glass_Window_Neutral'}))
    bb = np.array([tuple(v.co) for v in body.data.vertices])
    cx = (bb[:, 0].min() + bb[:, 0].max()) / 2
    wheels = [o for o in meshes if o is not body and centre(o).z < 0.6]
    wy = [centre(o).y for o in wheels]; cy = (min(wy) + max(wy)) / 2
    b = bmesh.new(); b.from_mesh(body.data); b.verts.ensure_lookup_table()
    isl = islands(b); ring = None
    for i in isl:
        q = np.array([tuple(v.co) for v in i]); c = q.mean(0)
        if len(i) < 100 or not (1.2 < c[2] < 1.7) or c[1] > cy: continue   # cab: ahead of the axle midpoint
        u, s, vt = np.linalg.svd(q - c, full_matrices=False); s = s / np.sqrt(len(i))
        if 0.15 < s[0] < 0.21 and s[1] > 0.9 * s[0] and s[2] < 0.1 * s[1]: ring = (c, vt[2], i)
    assert ring, 'no steering wheel ring'
    c, ax, _ = ring
    steer = set()
    for i in isl:
        q = np.array([tuple(v.co) for v in i]) - c
        if np.all(np.linalg.norm(q, axis=1) < 0.29) and np.all(np.abs(q @ ax) < 0.09): steer.update(v.index for v in i)
    b.free()
    print('STEER centre', np.round(c, 3), 'axis', np.round(ax, 3), 'verts', len(steer))
    put(split_off(body, steer, 'steer_wheel'), 'steer'); put(body, 'hull')
    for o in wheels: put(o, wheel_role(centre(o), cx, cy))
    for o in meshes:
        if o.parent is None: put(o, 'hull')
else:
    raise SystemExit('unknown mode ' + mode)

for o in bpy.data.objects: o.select_set(True)
print('GROUPS', {k: len(v.children) for k, v in groups.items()})
bpy.ops.export_scene.gltf(filepath=a[-1], export_format='GLB', use_selection=True, export_yup=True, export_animations=False, export_image_format='AUTO')
print('WROTE', a[-1])
