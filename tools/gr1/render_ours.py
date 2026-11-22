# Our Garand in ADS as the engine poses it (viewmodel.cpp vmLocal + the fireCurve), rendered per reference frame (Blender -b)
import bpy, bmesh, json, math, struct, sys
import numpy as np
from mathutils import Matrix, Quaternion, Vector
a = sys.argv[sys.argv.index('--') + 1:]
MD, WJ, OUT = a[0], a[1], a[2]
W = json.load(open(WJ)); FC = W['fireCurve']; FR = np.array(FC['frames'], float); FT = np.arange(len(FR)) / FC['fps']
COL = {'back_m': 0, 'up_m': 1, 'cam_pitch_deg': 4, 'pitch_deg': 6, 'yaw_deg': 7, 'cam_roll_deg': 8, 'handle_m': 9}
rear = Vector(W['sights']['rear']); front = Vector(W['sights']['front']); relief = W['ads']['eyeRelief']
raw = open(MD + '/mesh.bin', 'rb').read(); _, nv, ni = struct.unpack('<3I', raw[:12])
V = np.frombuffer(raw[12:12 + nv * 48], np.float32).reshape(nv, 12); I = np.frombuffer(raw[12 + nv * 48:12 + nv * 48 + ni * 4], np.uint32)
M = json.load(open(MD + '/model.json'))
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
img = bpy.data.images.load(MD + '/' + M['materials'][0]['albedo'])
mat = bpy.data.materials.new('g'); mat.use_nodes = True
nt = mat.node_tree; bsdf = nt.nodes['Principled BSDF']; tx = nt.nodes.new('ShaderNodeTexImage'); tx.image = img
nt.links.new(tx.outputs['Color'], bsdf.inputs['Base Color']); bsdf.inputs['Metallic'].default_value = 0.6; bsdf.inputs['Roughness'].default_value = 0.5
objs = {}
for p in M['parts']:
    tri = I[p['first']:p['first'] + p['count']].reshape(-1, 3)
    used, inv = np.unique(tri, return_inverse=True)
    me = bpy.data.meshes.new(p['role']); me.from_pydata(V[used, :3].tolist(), [], inv.reshape(-1, 3).tolist())
    uvl = me.uv_layers.new(); uv = V[used][:, 10:12]
    loops = inv.reshape(-1)
    uvl.data.foreach_set('uv', np.column_stack([uv[loops, 0], 1 - uv[loops, 1]]).ravel())
    me.materials.append(mat); me.shade_smooth() if hasattr(me, 'shade_smooth') else None
    o = bpy.data.objects.new(p['role'], me); sc.collection.objects.link(o); objs[p['role']] = o
# sight frame (viewmodel.cpp sightFrame): L -> +z, then Ry(180): weapon +z -> view -z
L = (front - rear).normalized()
q = L.rotation_difference(Vector((0, 0, 1))); Q = Quaternion((0, 1, 0), math.pi) @ q
P0 = Vector((0, 0, -relief)) - Q @ rear
cam = bpy.data.cameras.new('c'); cam.sensor_fit = 'VERTICAL'; cam.angle = math.radians(60); cam.clip_start = 0.01
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co); sc.camera = co   # at the origin looking down -z, +y up: view space
sun = bpy.data.lights.new('s', 'SUN'); sun.energy = 3.0; so = bpy.data.objects.new('s', sun); so.rotation_euler = (math.radians(50), 0, math.radians(-30)); sc.collection.objects.link(so)
sc.world = bpy.data.worlds.new('w'); sc.world.use_nodes = True; sc.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.6
sc.render.engine = 'BLENDER_EEVEE' if 'BLENDER_EEVEE' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE_NEXT'
sc.render.resolution_x, sc.render.resolution_y = 1920, 1080; sc.render.film_transparent = True
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
def at(ch, x):   # the committed fireCurve (KF1 rows + GR1 columns), linear between rows as weapons.cpp fireCurveAt
    if x <= 0 or x >= FT[-1]: return 0.0
    return float(np.interp(x, FT, FR[:, COL[ch]]))
SHOTS = [8.5, 28.5]   # the reference's shots in frame units (the first receiver jump is half a frame after)
log = []
for i in range(1, 44):
    ages = [(i - s) / 30 for s in SHOTS if i - s > 0]
    ch = {c: sum(at(c, x) for x in ages) for c in ('back_m', 'up_m', 'pitch_deg', 'yaw_deg', 'cam_pitch_deg', 'cam_roll_deg')}
    ch['handle_m'] = max([at('handle_m', x) for x in ages] + [0.0])
    p = P0 + Vector((0, ch['up_m'], ch['back_m']))
    m = Matrix.Translation(p) @ Q.to_matrix().to_4x4()
    piv = p + Q @ rear
    R = (Quaternion((0, 1, 0), math.radians(-ch['yaw_deg'])) @ Quaternion((1, 0, 0), math.radians(ch['pitch_deg']))).to_matrix().to_4x4()
    m = Matrix.Translation(piv) @ R @ Matrix.Translation(-piv) @ m
    objs['body'].matrix_world = m
    objs['handle'].matrix_world = m @ Matrix.Translation((0, 0, -ch['handle_m']))
    sc.render.filepath = OUT + '/o_%04d.png' % i
    bpy.ops.render.render(write_still=True)
    log.append(dict(frame=i, **ch))
json.dump(log, open(OUT + '/ours.json', 'w'), indent=1)
print('RENDERED', len(log))
