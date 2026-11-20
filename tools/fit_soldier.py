# Fit a Mixamo-rigged soldier to the reference build (the Allied soldier's): one uniform scale so the shoulders stand at the
# reference height, then the upper arms and forearms lengthened so shoulder -> wrist matches. Hands, head, torso untouched.
#   blender -b --python fit.py -- in.fbx out.glb SHOULDER_Z ARM_REACH [check.png]
import bpy, sys, math, os
from mathutils import Vector
IN, OUT, SH_Z, REACH = sys.argv[sys.argv.index("--") + 1:][:4]
SH_Z, REACH = float(SH_Z), float(REACH)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=IN, automatic_bone_orientation=False)
A = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
if A.animation_data: A.animation_data_clear()
for i in bpy.data.images:                                  # Mixamo's once-embedded shared texture (see gun_in_clip.py)
    if i.size[0] == 0 and i.filepath:
        import os
        same = [j for j in bpy.data.images if j.size[0] and os.path.basename(j.filepath) == os.path.basename(i.filepath)]
        if same: i.user_remap(same[0])
M = "mixamorig:"
for pb in A.pose.bones: pb.matrix_basis.identity()
bpy.context.view_layer.update()
head = lambda n: A.matrix_world @ A.data.bones[M + n].head_local
k = SH_Z / head("LeftArm").z
A.scale = A.scale * k                                      # 1: overall size
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='SELECT'); bpy.context.view_layer.objects.active = A
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
reach0 = (head("LeftArm") - head("LeftForeArm")).length + (head("LeftForeArm") - head("LeftHand")).length
ka = REACH / reach0
print("FIT scale %.3f  arm reach %.3f -> %.3f (x%.3f)" % (k, reach0, REACH, ka))
if abs(ka - 1) > 0.01:                                     # 2: arm length, as a pose baked into mesh and rest
    bpy.ops.object.mode_set(mode='EDIT')
    for s in ("Left", "Right"):
        for n in ("ForeArm", "Hand"): A.data.edit_bones[M + s + n].inherit_scale = 'NONE'
    bpy.ops.object.mode_set(mode='POSE')
    for s in ("Left", "Right"):
        for n in ("Arm", "ForeArm"): A.pose.bones[M + s + n].scale = (1, ka, 1)
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in [o for o in bpy.data.objects if o.type == 'MESH']:
        bpy.context.view_layer.objects.active = o
        for md in [m for m in o.modifiers if m.type == 'ARMATURE']: bpy.ops.object.modifier_apply(modifier=md.name)
    bpy.context.view_layer.objects.active = A
    bpy.ops.object.mode_set(mode='POSE'); bpy.ops.pose.armature_apply(selected=False); bpy.ops.object.mode_set(mode='EDIT')
    for s in ("Left", "Right"):
        for n in ("ForeArm", "Hand"): A.data.edit_bones[M + s + n].inherit_scale = 'FULL'
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in [o for o in bpy.data.objects if o.type == 'MESH']:
        md = o.modifiers.new("Armature", 'ARMATURE'); md.object = A
# 3: the wrist joint down to the cuff. Mixamo set it ~6.5 cm up the forearm on these meshes, so the wrist folded the sleeve.
# The Hand bone slides along its own axis (its turn at rest unchanged, the fingers stay); the sleeve strip it held goes to the forearm.
WR = float(os.environ.get("FIT_WRIST", 0.065))
if WR > 0:
    import mathutils
    bpy.context.view_layer.objects.active = A
    bpy.ops.object.mode_set(mode='EDIT')
    old = {}
    for s in ("Left", "Right"):
        hb = A.data.edit_bones[M + s + "Hand"]
        for c in hb.children: c.use_connect = False
        hb.use_connect = False
        ax = hb.y_axis.copy(); old[s] = (hb.head.copy(), ax)
        mid = A.data.edit_bones[M + s + "HandMiddle1"].head - hb.head
        print("FIT wrist", s, "bone axis vs wrist->knuckle %.2f, wrist->knuckle %.3f -> %.3f" % (ax.dot(mid.normalized()), mid.length, (mid - ax * WR).length))
        hb.head = hb.head + ax * WR; hb.tail = hb.tail + ax * WR
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in [o for o in bpy.data.objects if o.type == 'MESH']:
        for s in ("Left", "Right"):
            gh, gf = o.vertex_groups.get(M + s + "Hand"), o.vertex_groups.get(M + s + "ForeArm")
            if gh is None: continue
            if gf is None: gf = o.vertex_groups.new(name=M + s + "ForeArm")
            p0, ax = old[s]; n = 0
            for v in o.data.vertices:
                w = next((g.weight for g in v.groups if g.group == gh.index), 0.0)
                if w <= 0: continue
                f = min(1.0, max(0.0, ((v.co - p0).dot(ax) / WR - 0.7) / 0.5))      # 0: still sleeve, 1: hand
                if f < 1:
                    gh.add([v.index], w * f, 'REPLACE'); gf.add([v.index], w * (1 - f), 'ADD'); n += 1
            print("FIT wrist", o.name[:20], s, "sleeve vertices to the forearm:", n)
bpy.context.view_layer.update()
print("FIT result shoulder z %.3f reach %.3f shoulderW %.3f hips %.3f" % (head("LeftArm").z, (head("LeftArm") - head("LeftForeArm")).length + (head("LeftForeArm") - head("LeftHand")).length, (head("LeftArm") - head("RightArm")).length, head("Hips").z))
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_animations=False, export_yup=True, export_skins=True, export_image_format='JPEG', export_jpeg_quality=88, export_tangents=False)
chk = sys.argv[sys.argv.index("--") + 1:][4:5]
if chk:
    sc = bpy.context.scene
    cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); sc.collection.objects.link(cam); sc.camera = cam
    cam.location = (0, -4.6, 1.05); cam.rotation_euler = (math.radians(90), 0, 0)
    sun = bpy.data.objects.new("s", bpy.data.lights.new("s", 'SUN')); sc.collection.objects.link(sun); sun.rotation_euler = (math.radians(60), 0, math.radians(20)); sun.data.energy = 4
    sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.5, 0.5, 0.5)
    sc.render.resolution_x, sc.render.resolution_y = 900, 800; sc.render.filepath = chk[0]; bpy.ops.render.render(write_still=True)
