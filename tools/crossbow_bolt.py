# XB7 (Blender background): the crossbow's bolt from BeerP's "Arrow" (Sketchfab, CC-BY-4.0; E:/nmk/incoming_arrow): the full
# arrow (5.82 units, head +x, fletching -x) cut down to a bolt without thinning it: at the real-arrow scale (0.8 m) the head
# and its ferrule (x >= 1.6) stay, the shaft (-2.0 .. 1.6) is shortened, the fletching (x < -2.0) shortened to 60 % (a bolt's
# short vanes); whole bolt 0.42 m, then baked by --import-weapon-models (modern_crossbow.json "models": crossbow_bolt).
#   blender -b --factory-startup --disable-autoexec --python tools/crossbow_bolt.py -- <arrow.glb> <out.glb>
import bpy, sys
from mathutils import Vector
IN, OUT = sys.argv[sys.argv.index('--') + 1:]
ARROW_M, BOLT_M = 0.8, 0.42
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=IN)
ms = [o for o in bpy.data.objects if o.type == 'MESH']
for o in ms:
    M = o.matrix_world.copy(); o.parent = None; o.matrix_world = M
    o.data.transform(M); o.matrix_world.identity()
pts = [v.co.x for o in ms for v in o.data.vertices]; lo, hi = min(pts), max(pts); L0 = hi - lo   # 5.82: head at hi
H0, T0 = 1.6, -2.0                       # the head zone starts, the tail zone ends (source units)
k = ARROW_M / L0; tail = (T0 - lo) * 0.6; head = hi - H0; shaft = BOLT_M / k - tail - head
assert shaft > 0.5, shaft
def remap(x):
    if x >= H0: return x
    if x >= T0: return H0 - (H0 - x) * shaft / (H0 - T0)
    return H0 - shaft - (T0 - x) * 0.6
for o in ms:
    for v in o.data.vertices: v.co.x = remap(v.co.x)
    o.data.transform(__import__('mathutils').Matrix.Scale(k, 4)); o.data.update()
for o in [o for o in bpy.data.objects if o.type != 'MESH']: bpy.data.objects.remove(o)
pts = [v.co for o in ms for v in o.data.vertices]
print('BOLT length %.3f m, shaft dia %.1f mm' % (max(p.x for p in pts) - min(p.x for p in pts), 1000 * (max(p.z for p in pts if abs(p.x) < 0.05) - min(p.z for p in pts if abs(p.x) < 0.05))))
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_apply=True, export_extras=True)
