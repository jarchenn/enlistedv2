# the heavy armour FBX -> GLB (textures relinked from the zip's folder by file name), for the Paladin pipeline
import bpy, sys, os
A = sys.argv[sys.argv.index('--') + 1:]; FBX, TEXD, OUT = A[:3]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.fbx(filepath=FBX)
for im in bpy.data.images:
    fn = os.path.join(TEXD, os.path.basename(im.filepath.replace(chr(92), '/')) or im.name)
    if os.path.exists(fn): im.filepath = fn; im.reload()
    else: print('MISSING', im.name, im.filepath)
for o in list(bpy.data.objects):
    if o.name == 'MetaHuman_Face': bpy.data.objects.remove(o, do_unlink=True)   # under the closed helm; 35k vertices, unrigged
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_skins=True, export_animations=False, export_yup=True, export_image_format='AUTO')
print('wrote', OUT)
