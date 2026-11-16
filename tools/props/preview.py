# PR1: a quick look at converted GLBs (Eevee, sun + sky light, 3/4 view from the front +z), one PNG per model.
#   blender -b --factory-startup --disable-autoexec --python tools/props/preview.py -- <out dir> <a.glb> [<b.glb> ...]
import bpy, sys, os, math
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]; out = a[0]
for p in a[1:]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=p)
    ms = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    vs = [o.matrix_world @ v.co for o in ms for v in o.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs))); hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    c = (lo + hi) / 2; r = max((hi - lo).length * 0.5, 0.3)
    sc = bpy.context.scene; sc.render.engine = "BLENDER_EEVEE"; sc.render.resolution_x, sc.render.resolution_y = 900, 700
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    d = Vector((math.sin(math.radians(35)), -math.cos(math.radians(35)), 0.45)).normalized()   # front (-Blender y = +z game), from the right
    cam.location = c + d * r * 2.6; cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3; sun.rotation_euler = (0.8, 0.2, 0.6); sc.collection.objects.link(sun)
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.6, 0.65, 0.7, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 0.8
    sc.render.filepath = os.path.join(out, os.path.splitext(os.path.basename(p))[0] + ".png")
    bpy.ops.render.render(write_still=True)
    print("PR1 preview", sc.render.filepath)
