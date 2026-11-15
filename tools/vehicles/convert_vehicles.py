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
elif mode == 'humvee':
    # VH2 low-poly Humvee (FBX + one 1K texture, metres, 0.74 of a real M1114: scaled x1.36 to 4.6 m long, 1.8 m tall).
    # Its cab is a closed shell with the windows painted on: the windshield and side window faces are replaced by real
    # frames with openings, the shell gets a 2.5 cm wall (back faces are culled in game: the inside needs its own faces),
    # and the cab is furnished: floor, the wide drive tunnel, dash with gauges and switch panel, steering wheel (its own
    # "steer" role) on its column, pedals, four bucket seats, gear and transfer levers, radio stack, grab bar, armrests.
    # usage: ... -- humvee <Humvee.fbx> <texture.png> <out.glb>
    import math
    K = 1.36
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.fbx(filepath=a[1]); bpy.context.view_layer.update()
    tex = bpy.data.images.load(a[2])
    for m in bpy.data.materials:   # the FBX references its texture by a path from the author's machine
        m.use_nodes = True; nt = m.node_tree; b = nt.nodes.get('Principled BSDF')
        if b:
            for l in list(b.inputs['Base Color'].links): nt.links.remove(l)
            t = nt.nodes.new('ShaderNodeTexImage'); t.image = tex; nt.links.new(t.outputs['Color'], b.inputs['Base Color'])
            b.inputs['Roughness'].default_value = 0.8; b.inputs['Metallic'].default_value = 0.0
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    bake_world(meshes, K)
    for o in list(bpy.data.objects):
        if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    body = bpy.data.objects['Body']
    # the painted glass goes: the two windshield halves and each side's window band over the cab (unscaled numbers x K)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    gone = []
    for f in bm.faces:
        c = f.calc_center_median() / K; n = f.normal; zs = [v.co.z / K for v in f.verts]; ys = [v.co.y / K for v in f.verts]
        if n.y < -0.9 and -0.75 < c.y < -0.62 and min(zs) > 0.9 and max(zs) < 1.32: gone.append(f)                 # windshield
        elif abs(n.x) > 0.95 and min(zs) > 0.88 and max(zs) < 1.29 and min(ys) > -0.75 and max(ys) < 0.76: gone.append(f)   # side windows
    print('HUMVEE glass faces removed', len(gone))
    bmesh.ops.delete(bm, geom=gone, context='FACES_ONLY'); bm.to_mesh(body.data); bm.free()
    inner = bpy.data.materials.new('hv_inner'); inner.use_nodes = True   # the inside of the shell: plain interior paint, not the camo
    inner.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.12, 0.13, 0.09, 1); inner.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.9
    body.data.materials.append(inner)
    sol = body.modifiers.new('wall', 'SOLIDIFY'); sol.thickness = 0.025 * K; sol.offset = -1.0; sol.use_rim = True
    sol.material_offset = len(body.data.materials) - 1; sol.material_offset_rim = len(body.data.materials) - 1
    bpy.context.view_layer.objects.active = body; bpy.ops.object.modifier_apply(modifier='wall')
    def mat(name, rgb, rough=0.85, metal=0.0):
        m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
        b.inputs['Base Color'].default_value = (*rgb, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
        return m
    PAINT, DARK, BLACK, SEAT, METAL, GAUGE, RUBBER = mat('hv_paint', (0.11, 0.13, 0.08)), mat('hv_dash', (0.05, 0.05, 0.05), 0.7), mat('hv_black', (0.02, 0.02, 0.02), 0.6), mat('hv_seat', (0.15, 0.14, 0.09), 0.95), mat('hv_metal', (0.35, 0.35, 0.33), 0.45, 0.8), mat('hv_gauge', (0.75, 0.74, 0.66), 0.3), mat('hv_floor', (0.04, 0.04, 0.035), 0.95)
    def box(role, lo, hi, m, rot=None, pivot=None):   # unscaled Blender coords (x left, y back, z up)
        lo = [v * K for v in lo]; hi = [v * K for v in hi]
        bpy.ops.mesh.primitive_cube_add(size=1, location=[(p + q) / 2 for p, q in zip(lo, hi)]); o = bpy.context.object
        o.scale = [q - p for p, q in zip(lo, hi)]; bpy.ops.object.transform_apply(scale=True)
        if rot is not None:   # (axis char, deg) about pivot (unscaled)
            pv = Vector(pivot) * K; R = Matrix.Rotation(math.radians(rot[1]), 4, rot[0])
            o.data.transform(Matrix.Translation(pv) @ R @ Matrix.Translation(o.location - pv)); o.location = (0, 0, 0)
        o.data.materials.append(m); put(o, role); return o
    def cyl(role, c, axis, r, depth, m, verts=16):
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r * K, depth=depth * K, location=Vector(c) * K); o = bpy.context.object
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized())
        bpy.ops.object.transform_apply(rotation=True); o.data.materials.append(m); put(o, role); return o
    # ---- frames where the glass was: windshield (two panes, centre post) and both sides (A, B, C pillars, sill, header)
    for lo, hi in (((-0.66, -0.72, 0.92), (0.66, -0.66, 0.97)), ((-0.66, -0.71, 1.26), (0.66, -0.65, 1.31)),
                   ((-0.68, -0.72, 0.92), (-0.62, -0.66, 1.31)), ((0.62, -0.72, 0.92), (0.68, -0.66, 1.31)), ((-0.03, -0.72, 0.95), (0.03, -0.66, 1.28))):
        box('hull', lo, hi, PAINT)
    for sx in (1, -1):
        x0, x1 = (0.66, 0.71) if sx > 0 else (-0.71, -0.66)
        for lo, hi in (((x0, -0.72, 0.89), (x1, 0.73, 0.95)), ((x0, -0.72, 1.24), (x1, 0.73, 1.30)),
                       ((x0, -0.72, 0.89), (x1, -0.64, 1.30)), ((x0, -0.05, 0.89), (x1, 0.05, 1.30)), ((x0, 0.64, 0.89), (x1, 0.73, 1.30))):
            box('hull', lo, hi, PAINT)
    # ---- floor, the drive tunnel and the dash (driver on the left, +x)
    box('hull', (-0.67, -0.70, 0.40), (0.67, 0.70, 0.45), RUBBER)                                   # floor pan
    box('hull', (-0.18, -0.70, 0.45), (0.18, 0.70, 0.72), PAINT)                                    # the wide drive tunnel between the seats
    box('hull', (-0.67, -0.71, 0.74), (0.67, -0.56, 0.92), DARK)                                    # dash
    box('hull', (-0.67, -0.72, 0.90), (0.67, -0.50, 0.94), BLACK)                                   # dash top / defroster lip
    box('hull', (0.20, -0.57, 0.78), (0.58, -0.555, 0.91), BLACK)                                    # instrument panel face
    for i, gx in enumerate((0.25, 0.33, 0.47, 0.55)):                                                 # gauges (speedo / tach bigger in the middle)
        r = 0.045 if i in (1, 2) else 0.03
        cyl('hull', (gx if i not in (1, 2) else (0.35 if i == 1 else 0.47), -0.548, 0.855), (0, 1, 0), r, 0.012, GAUGE, 20)
    box('hull', (-0.16, -0.57, 0.76), (0.16, -0.555, 0.90), BLACK)                                   # switch panel over the tunnel
    for k in range(6): cyl('hull', (-0.12 + k * 0.048, -0.55, 0.83), (0, 1, 0), 0.008, 0.02, METAL, 8)   # toggle switches
    box('hull', (-0.14, -0.55, 0.72), (0.14, -0.30, 0.86), BLACK)                                    # radio stack on the tunnel
    for k in range(3): box('hull', (-0.12, -0.30, 0.745 + k * 0.035), (0.12, -0.295, 0.77 + k * 0.035), DARK)   # radio faces
    for x in (0.06, 0.11): cyl('hull', (x, -0.20, 0.80), (0, -0.25, 1), 0.008, 0.16, METAL, 8); cyl('hull', (x, -0.22, 0.88), (0, 0, 1), 0.016, 0.03, BLACK, 10)   # gear and transfer levers
    cyl('hull', (-0.42, -0.56, 0.93), (1, 0, 0), 0.012, 0.36, METAL, 8)                               # passenger grab bar
    for x in (0.34, 0.44, 0.52): box('hull', (x - 0.03, -0.62, 0.47), (x + 0.03, -0.58, 0.52), METAL)   # pedals
    # ---- four bucket seats (base + backrest leaning back), armrests on the doors
    for sx in (1, -1):
        cx = 0.43 * sx
        for y0 in (-0.12, 0.38):   # front row, rear row
            box('hull', (cx - 0.19, y0, 0.45), (cx + 0.19, y0 + 0.34, 0.58), SEAT)
            box('hull', (cx - 0.19, y0 + 0.30, 0.56), (cx + 0.19, y0 + 0.37, 1.02), SEAT, ('X', -12), (cx, y0 + 0.33, 0.57))
        box('hull', (0.62 * sx - 0.03, -0.55, 0.70), (0.62 * sx + 0.03, -0.10, 0.74), DARK)          # door armrests
        box('hull', (0.62 * sx - 0.03, 0.05, 0.70), (0.62 * sx + 0.03, 0.55, 0.74), DARK)
    # ---- steering wheel ("steer": turns with the steering) on its column (hull)
    WC = Vector((0.42, -0.40, 0.97)); WN = Vector((0, 0.87, 0.5)).normalized()   # rim centre; axis toward the driver (back and up)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.155 * K, minor_radius=0.012 * K, major_segments=36, minor_segments=8, location=WC * K)
    o = bpy.context.object; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(WN)
    bpy.ops.object.transform_apply(rotation=True); o.data.materials.append(BLACK); put(o, 'steer')
    u = Vector((1, 0, 0)); v = WN.cross(u)
    for k in range(3):
        an = math.radians(90 + 120 * k); d = u * math.cos(an) + v * math.sin(an)
        cyl('steer', tuple(WC + d * 0.077), tuple(d), 0.008, 0.155, BLACK, 6)
    cyl('steer', tuple(WC), tuple(WN), 0.03, 0.03, BLACK, 12)                                         # hub
    cyl('hull', tuple(WC - WN * 0.16), tuple(WN), 0.022, 0.30, DARK, 10)                              # column into the dash
    print('HUMVEE steering wheel centre (game axes, scaled)', round(WC.x * K, 3), round(WC.z * K, 3), round(-WC.y * K, 3), 'axis', round(WN.x, 3), round(WN.z, 3), round(-WN.y, 3))
    # ---- roles: the four wheel objects by their place (front -y, left +x), body and suspensions hull
    wh = [o for o in meshes if o.name != 'Body' and 'Suspension' not in o.name]
    print('HUMVEE objects', [o.name for o in meshes])
    assert len(wh) == 4, [o.name for o in wh]
    cy = sum(centre(o).y for o in wh) / 4
    for o in meshes: put(o, wheel_role(centre(o), 0, cy) if o in wh else 'hull')
elif mode == 'boxer':
    # VH2 Boxer 8x8 APC with an HMG remote weapon station (FBX, metres, front +x, z up; BMP textures beside it). Named
    # parts: hull, wheel..wheel8 (odd = left), mount (RWS cradle, traverses: "rws") with its ammo box, launchers and
    # smoke grenades; weapon (L111A1 .50) with sensors and clip (traverse + elevation: "mg"); hatches / ramp, interior,
    # seats (hull); the driver's vision-block glass is dropped (no transparent pass: the blocks are open slits).
    # Pivots are all at the origin in the file: the rig points go in boxer_apc.json.
    # usage: ... -- boxer <boxer-apc-hmg.fbx> <out.glb>
    import math
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.fbx(filepath=a[1]); bpy.context.view_layer.update()
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    bake_world(meshes, 1.0)
    R = Matrix.Rotation(math.radians(-90), 4, 'Z')   # front +x -> -y (the converter's convention), left +y -> +x
    for o in meshes: o.data.transform(R)
    for o in list(bpy.data.objects):
        if o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    wheels = sorted([o for o in bpy.data.objects if o.name.startswith('wheel')], key=lambda o: centre(o).y)
    assert len(wheels) == 8, [o.name for o in wheels]
    names = ['fl', 'f2l', 'r2l', 'rl']   # axles front to back
    for i, o in enumerate(wheels):
        put(o, 'wheel_' + names[i // 2][:-1] + ('l' if centre(o).x > 0 else 'r'))
    RWS = ('mount', 'ammobox', 'launcher', 'smoke')
    for o in list(bpy.data.objects):
        n = o.name
        if n.startswith('wheel'): continue
        if n == 'glass': bpy.data.objects.remove(o, do_unlink=True); continue
        if n in ('weapon', 'sensors', 'clip'): put(o, 'mg')
        elif n.startswith(RWS): put(o, 'rws')
        else: put(o, 'hull')
    print('BOXER roles', {k: len(v.children) for k, v in groups.items()})
else:
    raise SystemExit('unknown mode ' + mode)

for o in bpy.data.objects: o.select_set(True)
print('GROUPS', {k: len(v.children) for k, v in groups.items()})
bpy.ops.export_scene.gltf(filepath=a[-1], export_format='GLB', use_selection=True, export_yup=True, export_animations=False, export_image_format='AUTO')
print('WROTE', a[-1])
