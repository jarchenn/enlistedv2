# FK2 (Blender background): named rigid props of a GLB decimated to a triangle budget, each written as its own GLB holding
# only that mesh in its own (node-local) coordinates, for tools/enemies/take_cut.py to splice back into the source file
# (Blender's importer misplaces bone-parented props under a 0.01-scaled armature, so the whole model is not re-exported).
#   blender -b --factory-startup --disable-autoexec --python prop_decimate.py -- <in.glb> <out dir> <node>=<triangles> ...
import bpy, sys, os
from mathutils import Matrix
A = sys.argv[sys.argv.index('--') + 1:]; SRC, OUT = A[:2]; os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=SRC)
for spec in A[2:]:
    name, want = spec.split('='); o = bpy.data.objects[name]; o.data.calc_loop_triangles(); n0 = len(o.data.loop_triangles)
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    if n0 > int(want):
        md = o.modifiers.new('d', 'DECIMATE'); md.ratio = int(want) / n0; bpy.ops.object.modifier_apply(modifier='d')
    o.parent = None; o.matrix_world = Matrix.Identity(4); o.data.calc_loop_triangles()
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + '.glb'), export_format='GLB', use_selection=True, export_animations=False, export_yup=True, export_materials='PLACEHOLDER')
    print('PROP', name, n0, '->', len(o.data.loop_triangles))
