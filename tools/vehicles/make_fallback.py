# VH1: part-built fallbacks (boxes and cylinders) for clones without the local-only supplied models:
#   jeep  - Willys MB (jeep_willys.json): hull 1.76 x 3.34 m centred, wheels r 0.355 m
#   truck - Opel Blitz trucks (truck_*.json, laid out on the cargo truck): hull 2.36 x 7.1 m, cab ahead, wheels r 0.5 m
# Same role nodes and positions as the baked supplied models: game axes (x left, y up, z forward; Blender: x,
# -y forward, z up), y = 0 at the tyres.
# usage: blender -b --factory-startup --python make_fallback.py -- jeep|truck <out.glb>
import bpy, sys, math
from mathutils import Vector, Matrix
kind, out = sys.argv[sys.argv.index('--') + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name, rgb, rough=0.85, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*rgb, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
    return m
OD, DARK, TYRE, METAL, CANVAS = mat('od', (0.16, 0.17, 0.09)), mat('dark', (0.05, 0.05, 0.04)), mat('tyre', (0.03, 0.03, 0.03), 0.95), mat('metal', (0.3, 0.3, 0.28), 0.5, 0.8), mat('canvas', (0.22, 0.2, 0.12))
groups = {}
def grp(role):
    if role not in groups:
        e = bpy.data.objects.new(role, None); bpy.context.scene.collection.objects.link(e); groups[role] = e
    return groups[role]
def g2b(p): return Vector((p[0], -p[2], p[1]))   # game (x, y, z) -> Blender
def box(role, lo, hi, m=OD):
    c = [(a + b) / 2 for a, b in zip(lo, hi)]; s = [b - a for a, b in zip(lo, hi)]
    bpy.ops.mesh.primitive_cube_add(size=1, location=g2b(c)); o = bpy.context.object
    o.scale = (s[0], s[2], s[1]); bpy.ops.object.transform_apply(scale=True)
    o.data.materials.append(m); o.parent = grp(role); return o
def cyl(role, c, axis, r, depth, m=OD, verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=g2b(c)); o = bpy.context.object
    o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(g2b(axis).normalized())
    bpy.ops.object.transform_apply(rotation=True); o.data.materials.append(m); o.parent = grp(role); return o
if kind == 'jeep':
    # hull: tub, bonnet, grille, fenders, dashboard, seats, bumpers; spans x +-0.88, z +-1.67 like the supplied mesh
    box('hull', (-0.70, 0.45, -1.45), (0.70, 0.98, 0.30))            # tub
    box('hull', (-0.46, 0.62, 0.30), (0.46, 1.02, 1.52))             # bonnet
    box('hull', (-0.46, 0.40, 1.52), (0.46, 1.00, 1.58), DARK)       # grille
    for s in (1, -1):
        box('hull', (min(s * 0.46, s * 0.88), 0.72, 0.55), (max(s * 0.46, s * 0.88), 0.78, 1.55))   # front fender
        box('hull', (min(s * 0.70, s * 0.88), 0.72, -1.30), (max(s * 0.70, s * 0.88), 0.78, -0.55))   # rear wheel arch
    box('hull', (-0.70, 0.95, 0.24), (0.70, 1.12, 0.36))             # dashboard / cowl
    box('hull', (-0.80, 0.30, 1.58), (0.80, 0.42, 1.67), METAL)      # front bumper
    box('hull', (-0.80, 0.30, -1.67), (0.80, 0.42, -1.45), METAL)    # rear
    for x in (0.36, -0.36):
        box('hull', (x - 0.22, 0.62, -0.75), (x + 0.22, 0.72, -0.30), CANVAS)   # seat
        box('hull', (x - 0.22, 0.72, -0.80), (x + 0.22, 1.10, -0.72), CANVAS)   # back rest
    box('hull', (-0.60, 0.62, -1.40), (0.60, 0.72, -0.95), CANVAS)   # rear bench
    box('hull', (-0.62, 0.55, -1.0), (0.62, 0.62, 0.9), DARK)        # chassis rails / floor
    cyl('hull', (0.36, 0.86, 0.17), (0, 0.497, -0.868), 0.025, 0.32, DARK, 8)   # steering column
    # steering wheel (rim + three spokes) on the supplied wheel's centre and axis
    bpy.ops.mesh.primitive_torus_add(major_radius=0.19, minor_radius=0.014, major_segments=32, minor_segments=8, location=g2b((0.36, 1.094, 0.01)))
    o = bpy.context.object; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(g2b((0, 0.497, -0.868)))
    bpy.ops.object.transform_apply(rotation=True); o.data.materials.append(DARK); o.parent = grp('steer')
    ax = Vector((0, 0.497, -0.868)); u = Vector((1, 0, 0)); v = ax.cross(u)
    for k in range(3):
        a = math.radians(90 + 120 * k); d = u * math.cos(a) + v * math.sin(a); c = Vector((0.36, 1.094, 0.01)) + d * 0.095
        cyl('steer', tuple(c), tuple(d), 0.01, 0.19, DARK, 6)
    # windshield frame (glassless), hinged at its foot on the cowl
    box('windshield', (-0.70, 1.02, 0.28), (0.70, 1.08, 0.34), METAL)
    box('windshield', (-0.70, 1.54, 0.28), (0.70, 1.60, 0.34), METAL)
    for s in (1, -1): box('windshield', (min(s * 0.64, s * 0.70), 1.02, 0.28), (max(s * 0.64, s * 0.70), 1.60, 0.34), METAL)
    # wheels (tyre + hub) at the supplied mesh's centres
    for role, x, z in (('wheel_fl', 0.63, 1.09), ('wheel_fr', -0.61, 1.09), ('wheel_rl', 0.63, -0.906), ('wheel_rr', -0.61, -0.906)):
        cyl(role, (x, 0.355, z), (1, 0, 0), 0.355, 0.16, TYRE, 20)
        cyl(role, (x + (0.085 if x > 0 else -0.085), 0.355, z), (1, 0, 0), 0.2, 0.02, OD, 12)
    # stowage: jerrycan on the rear panel, bags on the sides
    box('stowage', (0.15, 0.55, -1.71), (0.50, 1.00, -1.62), OD)
    for s in (1, -1): box('stowage', (min(s * 0.70, s * 0.86), 0.75, -1.20), (max(s * 0.70, s * 0.86), 1.05, -0.70), CANVAS)
else:
    # cab (with an open windshield frame), bonnet, flatbed with low sides, chassis, bumper; cargo-truck layout
    GR = mat('field_grey', (0.17, 0.18, 0.16))
    box('hull', (-0.48, 0.75, 2.30), (0.48, 1.45, 3.45), GR)               # bonnet
    box('hull', (-0.50, 0.60, 3.45), (0.50, 1.40, 3.55), DARK)             # grille
    for s_ in (1, -1):
        box('hull', (min(s_ * 0.48, s_ * 1.10), 1.0, 2.20), (max(s_ * 0.48, s_ * 1.10), 1.08, 3.40), GR)   # mudguard
        box('hull', (min(s_ * 1.0, s_ * 1.10), 1.05, 0.80), (max(s_ * 1.0, s_ * 1.10), 2.20, 2.10), GR)    # cab side
        box('hull', (min(s_ * 0.05, s_ * 1.10), 1.25, 2.04), (max(s_ * 0.05, s_ * 1.10), 1.35, 2.10), GR)  # cowl under the glass
    box('hull', (-1.10, 2.12, 0.80), (1.10, 2.22, 2.10), GR)                # cab roof
    box('hull', (-1.10, 1.05, 0.80), (1.10, 2.20, 0.88), GR)                # cab back
    box('hull', (-1.10, 1.05, 2.02), (1.10, 1.45, 2.10), GR)                # dash / bulkhead
    for x_ in (-1.10, -0.02, 1.04): box('hull', (x_, 1.45, 2.04), (x_ + 0.06, 2.14, 2.10), GR)   # windshield posts
    box('hull', (-1.0, 1.05, 1.05), (1.0, 1.40, 1.45), CANVAS)              # bench seat
    box('hull', (-1.18, 1.05, -3.55), (1.18, 1.20, 0.70), mat('bed', (0.3, 0.24, 0.15)))   # bed floor
    for s_ in (1, -1): box('hull', (min(s_ * 1.12, s_ * 1.18), 1.20, -3.55), (max(s_ * 1.12, s_ * 1.18), 1.70, 0.70), GR)
    box('hull', (-1.18, 1.20, -3.55), (1.18, 1.70, -3.49), GR); box('hull', (-1.18, 1.20, 0.64), (1.18, 1.70, 0.70), GR)
    box('hull', (-0.50, 0.55, -3.3), (0.50, 0.75, 3.3), DARK)               # chassis rails
    box('hull', (-1.05, 0.45, 3.45), (1.05, 0.60, 3.55), METAL)             # bumper
    bpy.ops.mesh.primitive_torus_add(major_radius=0.25, minor_radius=0.016, major_segments=32, minor_segments=8, location=g2b((0.42, 1.53, 1.59)))
    o = bpy.context.object; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(g2b((0, 0.627, -0.779)))
    bpy.ops.object.transform_apply(rotation=True); o.data.materials.append(DARK); o.parent = grp('steer')
    ax = Vector((0, 0.627, -0.779)); u = Vector((1, 0, 0)); v = ax.cross(u)
    for k in range(4):
        a = math.radians(45 + 90 * k); d = u * math.cos(a) + v * math.sin(a); c = Vector((0.42, 1.53, 1.59)) + d * 0.125
        cyl('steer', tuple(c), tuple(d), 0.012, 0.25, DARK, 6)
    cyl('hull', (0.42, 1.30, 1.88), (0, 0.627, -0.779), 0.03, 0.75, DARK, 8)   # column
    for role, x, z, w in (('wheel_fl', 0.82, 2.76, 0.28), ('wheel_fr', -0.80, 2.76, 0.28), ('wheel_rl', 0.88, -1.74, 0.42), ('wheel_rr', -0.89, -1.74, 0.42)):
        cyl(role, (x, 0.5, z), (1, 0, 0), 0.5, w, TYRE, 20)
        cyl(role, (x + (w / 2 + 0.01 if x > 0 else -w / 2 - 0.01), 0.5, z), (1, 0, 0), 0.28, 0.02, GR, 12)
for o in bpy.data.objects: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_yup=True, export_animations=False)
print('WROTE', out)
