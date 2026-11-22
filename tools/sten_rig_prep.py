# STR1 (Blender background): the Sten on the fps-animated-smg first-person rig (the arms and clips the user's MP5 uses).
# The MP5 pack's FBX (arms@mp5.fbx: DJMaesen's smg arms / timeline with the MP5 meshes hung on its objects) is opened, the
# MP5 meshes deleted, and the Sten's static parts (E:/nmk/ng3/out/modern_sten.glb, the bake behind models/sten) hung on the
# same animated objects: base and trigger on the gun ("smg45"), the bolt on "bolt", the magazine on "clip" (it moves with
# the reload). Placement: in weapon space (metres, +z forward) the Sten moves so its rightGrip socket lands on the MP5's
# (the right hand stays where the rig puts it); the bore then lies on the MP5's bore within ~2 mm.
#   blender -b --factory-startup --disable-autoexec --python tools/sten_rig_prep.py -- <arms@mp5.fbx> <modern_sten.glb> <out.blend>
# Weapon <-> glTF: w = (g - o) * s, o = the bbox centre (x, y) and min z, s = realLength / length (the Sten: its base part, as
# its bake's lengthNode). The rig's own mapping is the MP5's: the anim config shifts the Sten's sight points by the same grip
# offset (pointShift) and uses the MP5's static summary (stat), so the rig stays in the MP5 rig's metres.
import bpy, sys, json
from mathutils import Matrix, Vector
FBX, STEN, OUT = sys.argv[sys.argv.index('--') + 1:]
MP5 = dict(o=((-0.0349 + 0.0326) / 2, (-0.0844 + 0.2192) / 2, -0.2635), s=0.7 / (0.4843 + 0.2635), grip=(0.0019, 0.0, 0.3))   # models/modern_mp5_local (XB1 summary), JUST's sockets
STN = dict(o=((-1.7477 + 5.2072) / 2, (-6.4203 + 8.6819) / 2, -19.535), s=0.762 / (35.8153 + 19.535), grip=(-0.0238, -0.005, 0.22))   # ng3 modern_sten summary, sten.json
T = [a - b for a, b in zip(MP5['grip'], STN['grip'])]
def toBl(g): return Vector((g[0], -g[2], g[1]))
def toG(b): return (b[0], b[2], -b[1])
def mapPoint(b):   # a Sten static point (Blender coords of its glTF) -> the MP5 rig's world (Blender)
    g = toG(b); w = [(g[i] - STN['o'][i]) * STN['s'] + T[i] for i in range(3)]
    return toBl([w[i] / MP5['s'] + MP5['o'][i] for i in range(3)])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=FBX)
sc = bpy.context.scene; sc.frame_set(1); bpy.context.view_layer.update()
mp5 = [o for o in bpy.data.objects if o.type == 'MESH' and o.name.startswith('mp5_')]
P = [o.matrix_world @ Vector(c) for o in mp5 for c in o.bound_box]
print('STR mp5 world bounds (glTF):', [round(x, 4) for x in toG(Vector([min(p[k] for p in P) for k in range(3)]))], [round(x, 4) for x in toG(Vector([max(p[k] for p in P) for k in range(3)]))])
for o in mp5: bpy.data.objects.remove(o, do_unlink=True)
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=STEN); bpy.context.view_layer.update()
new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
host = {'base': 'smg45', 'trigger': 'smg45', 'bolt': 'bolt', 'mag': 'clip'}
for o in new:
    part = o.name.split('.')[0]; assert part in host, o.name
    me = o.data; W = o.matrix_world.copy()
    for v in me.vertices: v.co = mapPoint(W @ v.co)
    me.update(); o.parent = None; o.matrix_world = Matrix()
    h = bpy.data.objects[host[part]]; o.parent = h; o.matrix_parent_inverse = h.matrix_world.inverted()
    o.name = 'sten_' + part; me.name = 'sten_' + part
    print('STR', o.name, 'on', h.name, 'mats', [m.name for m in me.materials])
for o in [o for o in bpy.data.objects if o not in before and o.type != 'MESH']: bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print('wrote', OUT)
