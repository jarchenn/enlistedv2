# GR5 (the user): the GR1 ADS shot on the first-person arms rig (E:/nmk/boltguns/gar.blend: the M24 arms holding the
# Realistic M1 Garand, BG4). The arms and rifle are frozen at the rig's hold frame and carried by one empty at an ADS
# camera on the rifle's own sight line (peep centre -> blade tip, eye relief and FOV from m1_garand.json); the carrier
# plays the fireCurve rows (gun back / up / right, muzzle pitch / yaw about the peep), the camera the kick and roll, the
# operating rod and bolt the handle stroke, two shots at the reference clip's times. Preview only (Blender -b).
#   blender -b --factory-startup --python arms_ads.py -- <gar.blend> <m1_garand.json> <out dir> [frames e.g. 8 or 1-43] [save.blend]
import bpy, sys, json, math
import numpy as np
from mathutils import Vector, Matrix, Quaternion
A = sys.argv[sys.argv.index('--') + 1:]; BLEND, WJ, OUT = A[0], A[1], A[2]
FR = A[3] if len(A) > 3 else '1-43'; SAVE = A[4] if len(A) > 4 else None
W = json.load(open(WJ)); FC = W['fireCurve']; ROWS = np.array(FC['frames'], float); FT = np.arange(len(ROWS)) / FC['fps']
bpy.ops.wm.open_mainfile(filepath=BLEND)
import os; PEEP = json.load(open(BLEND + '.peep.json'))['centre'] if os.path.exists(BLEND + '.peep.json') else (-0.0031, -9.1615)   # GR7: the rebuilt ring's centre
sc = bpy.context.scene; ob = bpy.data.objects
K = 1.221 / 1.092                      # rig units per metre (rig_scene.py)
F0 = 1                                 # the rig's hold frame
sc.frame_set(F0); bpy.context.view_layer.update()
# ---- freeze everything at F0 (object transforms and the armature's pose), then drop the animation
MW = {o.name: o.matrix_world.copy() for o in ob}
arm = ob['Armature']; BASIS = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}
for o in ob:
    if o.animation_data: o.animation_data.action = None
    if o.type == 'CAMERA' and o.data.animation_data: o.data.animation_data.action = None
for pb in arm.pose.bones: pb.matrix_basis = BASIS[pb.name]
def chain_len(o):
    n = 0; p = o.parent
    while p: n += 1; p = p.parent
    return n
for o in sorted(ob, key=chain_len): o.matrix_world = MW[o.name]; bpy.context.view_layer.update()
# ---- the rifle's sight line (Garand001 local: muzzle -z, up -y; peep centre and blade tip measured on the mesh)
g = ob['Garand001']; G = g.matrix_world
REAR, FRONT = G @ Vector((PEEP[0], PEEP[1], 8.31)), G @ Vector((0.0, -9.15, -21.10))
L = (FRONT - REAR).normalized(); UPg = (G.to_3x3() @ Vector((0, -1, 0))).normalized(); UPg = (UPg - L * UPg.dot(L)).normalized()
relief = W['ads']['eyeRelief'] * K
Rc = Matrix((L.cross(UPg), UPg, -L)).transposed()          # camera columns: x right, y up, z back
C0 = Matrix.Translation(REAR - L * relief) @ Rc.to_4x4()
cam = ob.new('adscam', bpy.data.cameras.new('adscam')) if hasattr(ob, 'new') else bpy.data.objects.new('adscam', bpy.data.cameras.new('adscam'))
sc.collection.objects.link(cam); sc.camera = cam
cam.data.sensor_fit = 'VERTICAL'; cam.data.angle = math.radians(W['ads'].get('vmFov') or 60); cam.data.clip_start = 0.005
for o in ob:   # the rig's own cameras and helper meshes out of the picture
    if o.type == 'CAMERA' and o != cam: o.hide_render = True
# ---- one carrier for the arms and the rifle
E = bpy.data.objects.new('ads_carrier', None); sc.collection.objects.link(E); E.matrix_world = C0; bpy.context.view_layer.update()
for o in list(ob):
    if o in (cam, E) or o.parent is not None or o.type in ('CAMERA', 'LIGHT'): continue
    mw = o.matrix_world.copy(); o.parent = E; o.matrix_parent_inverse = E.matrix_world.inverted(); o.matrix_world = mw
bpy.context.view_layer.update()
E0i = C0.inverted()
MOV = [ob[n] for n in ('Charging_Handle_02 - Default_0', 'Bolt_02 - Default_0') if n in ob]
for o in MOV:   # straight onto the carrier: their stroke keyed in carrier space
    mw = o.matrix_world.copy(); o.parent = E; o.matrix_parent_inverse = Matrix.Identity(4); o.matrix_basis = E0i @ mw
BASE = {o.name: o.matrix_basis.copy() for o in MOV}
Lc = (E0i.to_3x3() @ L).normalized()
# ---- the shot, frame by frame (the engine's fireCurveSum: shots add, the rod at its furthest back)
SHOTS = [8.5, 28.5]
def row(t):
    if t <= 0 or t >= FT[-1]: return np.zeros(10)
    return np.array([np.interp(t, FT, ROWS[:, k]) for k in range(10)])
f0, f1 = (int(FR.split('-')[0]), int(FR.split('-')[-1]))
log = []
for f in range(f0, f1 + 1):
    ages = [(f - s) / 30 for s in SHOTS if f > s]
    r = sum((row(a) for a in ages), np.zeros(10)); h = max([row(a)[9] for a in ages] + [0.0])
    Cf = C0 @ Matrix.Rotation(math.radians(r[4]), 4, 'X') @ Matrix.Rotation(math.radians(-r[5]), 4, 'Y') @ Matrix.Rotation(math.radians(r[8]), 4, 'Z')
    piv = Vector((0, 0, -relief))
    Rg = (Quaternion((0, 1, 0), math.radians(-r[7])) @ Quaternion((1, 0, 0), math.radians(r[6])) @ Quaternion((0, 0, 1), math.radians(r[3]))).to_matrix().to_4x4()
    Gm = Matrix.Translation(Vector((r[2], r[1], r[0])) * K) @ Matrix.Translation(piv) @ Rg @ Matrix.Translation(-piv)
    cam.matrix_world = Cf; cam.keyframe_insert('location', frame=f); cam.keyframe_insert('rotation_euler', frame=f)
    E.matrix_world = Cf @ Gm; E.keyframe_insert('location', frame=f); E.keyframe_insert('rotation_euler', frame=f)
    for o in MOV:
        M = Matrix.Translation(-Lc * h * K) @ BASE[o.name]; o.matrix_basis = M; o.keyframe_insert('location', frame=f)
    log.append(dict(frame=f, cam_pitch_deg=float(r[4]), cam_roll_deg=float(r[8]), back_m=float(r[0]), pitch_deg=float(r[6]), handle_m=float(h)))
for o in [cam, E] + MOV:
    if o.animation_data and o.animation_data.action:
        for fc in getattr(o.animation_data.action, 'fcurves', []):
            for kp in fc.keyframe_points: kp.interpolation = 'LINEAR'
json.dump(log, open(OUT + '/arms_ads.json', 'w'), indent=1)
# ---- render: transparent over the composite's grid
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 1920, 1080, 100
sc.render.film_transparent = True; sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
sc.frame_start, sc.frame_end = f0, f1; sc.render.fps = 30
sc.render.filepath = OUT + '/a_'
if SAVE: bpy.ops.wm.save_as_mainfile(filepath=SAVE, copy=True)
bpy.ops.render.render(animation=True)
print('ARMS_ADS', f0, f1, 'relief %.4f units, L %s' % (relief, tuple(round(v, 3) for v in L)))
