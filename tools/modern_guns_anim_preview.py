# MG1: re-import a viewmodel GLB (modern_guns_anim.py output) and render clips from the eye (60 deg vertical FOV),
# plus the posed gun bounds in rig space. blender -b --factory-startup --python <this> -- <vm.glb> <out prefix> [clip ...]
import bpy, sys, json, math
from mathutils import Matrix
a = sys.argv[sys.argv.index("--") + 1:]; glb, pre = a[0], a[1]; want = a[2:] or ["idle", "fire", "reload"]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=glb)
sc = bpy.context.scene; arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
camd = bpy.data.cameras.new("c"); camd.sensor_fit = "VERTICAL"; camd.angle = math.radians(60); camd.clip_start = 0.01
co = bpy.data.objects.new("c", camd); sc.collection.objects.link(co); sc.camera = co
co.matrix_world = Matrix.Rotation(math.radians(90), 4, "X")   # glTF -z forward = Blender +y
sc.render.engine = "BLENDER_WORKBENCH"; sc.render.resolution_x = 640; sc.render.resolution_y = 360
ims = sc.render.image_settings
if hasattr(ims, "media_type"): ims.media_type = "IMAGE"
ims.file_format = "PNG"
ad = arm.animation_data or arm.animation_data_create()
for tr in list(ad.nla_tracks): ad.nla_tracks.remove(tr)
for name in want:
    act = next((x for x in bpy.data.actions if x.name.split("_Armature")[0] == name or x.name.startswith(name + "_") or x.name == name), None)
    if not act: print("MG1PV missing", name); continue
    ad.action = act
    if hasattr(ad, "action_slot") and act.slots: ad.action_slot = act.slots[0]
    f0, f1 = act.frame_range
    for tag, f in (("a", f0), ("b", (f0 + f1) / 2)):
        sc.frame_set(int(f)); bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get(); lo = [9] * 3; hi = [-9] * 3
        for o in bpy.data.objects:
            if o.type != "MESH": continue
            e = o.evaluated_get(dg); m = e.to_mesh()
            for v in m.vertices:
                w = e.matrix_world @ v.co; lo = [min(lo[i], w[i]) for i in range(3)]; hi = [max(hi[i], w[i]) for i in range(3)]
            e.to_mesh_clear()
        print("MG1PV", name, tag, int(f), [round(x, 3) for x in lo], [round(x, 3) for x in hi])
        sc.render.filepath = "%s_%s_%s.png" % (pre, name, tag); bpy.ops.render.render(write_still=True)
