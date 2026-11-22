# GR13 (Blender background): checks on a baked first-person rig (models/<dir>/vm.glb + vm.json), no game launch:
#  - moving joints (the frozen-rig bug, FR7): the bake's movingJoints per clip at least the given minimum
#  - the sight line is clear in ADS: the rig at its idle pose, placed as vm_anim.cpp places it in ADS (the rear sight on the
#    view axis at the eye relief, the front sight straight ahead, the gun's up straight up); rays from the eye through the
#    rear sight (its centre and a ring of points inside the hole) must reach the front sight's distance without hitting
#    anything (the arms, hands, props); the ray at the blade's tip may hit only the blade itself, at the front's distance
#   blender -b --factory-startup --python rig_check.py -- <models dir> <eyeRelief m> <hole radius m> '{"fire": 1, "reload": 10, "reload_empty": 10}'
import bpy, sys, json, math
from mathutils import Vector, Matrix, Quaternion
from mathutils.bvhtree import BVHTree
A = sys.argv[sys.argv.index('--') + 1:]; D, RELIEF, HOLE, MINJ = A[0], float(A[1]), float(A[2]), json.loads(A[3])
meta = json.load(open(D + '/vm.json')); fails = []
mv = meta['log'].get('movingJoints', {})
for c, n in MINJ.items():
    ok = mv.get(c, 0) >= n; print('[rig] %s: moving joints %s: %d (min %d)' % ('ok' if ok else 'FAIL', c, mv.get(c, 0), n))
    if not ok: fails.append('moving ' + c)
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=D + '/vm.glb')
sc = bpy.context.scene; arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
idle = next((a for a in bpy.data.actions if a.name.split('|')[0].lower().startswith('idle') or a.name.lower() == 'idle'), None)
assert idle, [a.name for a in bpy.data.actions]
arm.animation_data_create(); arm.animation_data.action = idle
if hasattr(arm.animation_data, 'action_slot') and idle.slots: arm.animation_data.action_slot = idle.slots[0]
sc.frame_set(int(idle.frame_range[0])); bpy.context.view_layer.update()
# rig space (glTF): x right, y up, -z forward; Blender's import turns it (Y up -> Z up): rig (x, y, z) -> Blender (x, -z, y)
g2b = lambda p: Vector((p[0], -p[2], p[1]))
gun = arm.pose.bones.get(meta['gunJoint']); Aw = arm.matrix_world
Gm = Aw @ gun.matrix @ gun.bone.matrix_local.inverted() @ Aw.inverted()   # the gun joint's motion from rest, in world space (g2b already is)
R, F = Gm @ g2b(meta['points']['rear']), Gm @ g2b(meta['points']['front'])
fwd = (F - R).normalized(); up = (Gm.to_3x3() @ Vector((0, 0, 1))); up = (up - fwd * up.dot(fwd)).normalized(); right = fwd.cross(up)
eye = R - fwd * RELIEF
dg = bpy.context.evaluated_depsgraph_get(); verts, polys, owner = [], [], []
for o in bpy.data.objects:
    if o.type != 'MESH' or not any(m.type == 'ARMATURE' for m in o.modifiers): continue   # the rig's skinned meshes only (Blender's glTF import adds an unskinned Icosphere bone shape)
    oe = o.evaluated_get(dg); me = oe.to_mesh(); base = len(verts)
    verts += [oe.matrix_world @ v.co for v in me.vertices]
    for p in me.polygons: polys.append([base + i for i in p.vertices]); owner.append(o.name)
    oe.to_mesh_clear()
bvh = BVHTree.FromPolygons(verts, polys)
gv = [verts[i] for k, pl in enumerate(polys) if owner[k] == 'gar_body' for i in pl]
near = min((v - R).length for v in gv) if gv else 1e9   # self-test: the rear point sits in the peep's hole, its rim about HOLE away
print('[rig] %s: the rear point %.1f mm from the rifle (the hole radius %.1f mm)' % ('ok' if 0.4 * HOLE < near < 2.5 * HOLE else 'FAIL', near * 1000, HOLE * 1000))
if not 0.4 * HOLE < near < 2.5 * HOLE: fails.append('rear point not in the peep')
dF = (F - eye).length
rays = [('tip', F)] + [('hole %d' % k, R + (right * math.cos(a) + up * math.sin(a)) * HOLE * 0.6) for k, a in enumerate([i * math.pi / 4 for i in range(8)])]
for name, P in rays:
    d = (P - eye).normalized(); far = dF - (0.004 if name == 'tip' else 0.02)
    hit, nrm, idx, dist = bvh.ray_cast(eye + d * 1e-4, d, far)
    ok = hit is None
    print('[rig] %s: sight line %s %s' % ('ok' if ok else 'FAIL', name, 'clear for %.3f m (the front sight at %.3f)' % (far, dF) if ok else 'blocked by %s at %.3f m' % (owner[idx], dist)))
    if not ok: fails.append('sight ' + name)
print('[rig] ' + ('all ok' if not fails else 'FAILED: ' + ', '.join(fails)))
sys.exit(1 if fails else 0)
