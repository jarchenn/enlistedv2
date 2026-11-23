# OPF1 (Blender background): a rigged GLB re-exported in metres, y up, with the root transforms a Sketchfab export carries
# applied to the armature and the meshes, in its rest pose and without animation (for tools/cc_to_mixamo_names.py and
# tools/bake_gunbody.py --skin, which read joint and vertex positions as they are in the file).
#   blender -b --factory-startup --disable-autoexec --python tools/glb_normalize_rig.py -- <in.glb> <out.glb>
import bpy, sys
from mathutils import Matrix
SRC, OUT = sys.argv[sys.argv.index('--') + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=SRC)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.material_slots]
for a in list(bpy.data.actions): bpy.data.actions.remove(a)
arm.animation_data_clear()
for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()
aw = arm.matrix_world.copy(); arm.parent = None; arm.matrix_world = aw
for m in meshes:
    mw = m.matrix_world.copy(); m.parent = None; m.data.transform(mw); m.matrix_world = Matrix.Identity(4)
    if m.data.shape_keys: m.shape_key_clear()
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for m in meshes: m.parent = arm; m.matrix_parent_inverse = Matrix.Identity(4)
for o in list(bpy.data.objects):
    if o is not arm and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
zs = [(m.matrix_world @ v.co).z for m in meshes for v in m.data.vertices]; print('HEIGHT %.3f .. %.3f' % (min(zs), max(zs)))
for o in [arm] + meshes: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=False, export_skins=True, export_yup=True)
print('wrote', OUT)
