# XB1 (Blender background): crossbow_animated.glb (DJMaesen) -> a .blend for tools/modern_guns_anim.py. The GLB imports as
# two armatures: the arms and the bow (limbs and string flex on their own bones). The anim exporter takes one armature, so
# the bow's two skinned meshes are frozen at the rest pose (the bow drawn, as at the idle), re-parented to the bow
# armature's own parent (they follow the crossbow's animated root), and the bow armature and the importer's bone-shape
# spheres are deleted. The gun parts (bolt, puller, loader, trigger) stay on their animated empties: the exporter's
# "rigidify" bakes every part per frame. Limitation: the limbs and string do not flex in the fire / reload clips.
#   blender -b --factory-startup --disable-autoexec --python tools/crossbow_prep.py -- <crossbow_animated.glb> <out.blend>
import bpy, sys
SRC, OUT = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=SRC)
sc = bpy.context.scene; sc.frame_set(0); bpy.context.view_layer.update()
bow = bpy.data.objects['Object_58']
for nm in ('Object_61', 'Object_63'):
    o = bpy.data.objects[nm]
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    for m in list(o.modifiers):
        if m.type == 'ARMATURE': bpy.ops.object.modifier_apply(modifier=m.name)
    mw = o.matrix_world.copy(); o.parent = bow.parent; o.matrix_world = mw
    for g in list(o.vertex_groups): o.vertex_groups.remove(g)
for o in [bow] + [x for x in bpy.data.objects if x.name.startswith('Icosphere')]: bpy.data.objects.remove(o, do_unlink=True)
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
assert len(arms) == 1, [o.name for o in arms]
print('XBPREP armature', arms[0].name, 'bow parent', bpy.data.objects['Object_61'].parent and bpy.data.objects['Object_61'].parent.name)
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print('wrote', OUT)
