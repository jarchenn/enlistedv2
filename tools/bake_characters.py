# Blender (5.x) background script: bake the user's soldier GLBs into game-ready skinned characters.
#   blender -b --factory-startup --python tools/bake_characters.py -- <srcdir> <outdir> [axis|allied|all]
# axis:   ww2_panzergrenadier_2k_cc0.glb (skinned, Mixamo skeleton) -> axis_grenadier.glb
# allied: us_ranger_infantry_-_call_of_duty_2_hd.glb (six static variants, no skeleton) -> allied_ranger_<k>.glb
#         rigged with the panzergrenadier's Mixamo skeleton: the armature is scaled to the variant, the arm chains are
#         rotated about the shoulder to the variant's A-pose, and vertex weights are computed from the distance to the
#         bone segments (inverse 4th power, best 3 bones; fingers fold into the hand). Bone heat is not used: the
#         source meshes are not watertight.
# Output: metres, feet at y = 0, model facing glTF +z, textures <= 512 px JPEG, no animation clips.
import bpy, sys, os, math, mathutils
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT = argv[0], argv[1]
WHAT = argv[2] if len(argv) > 2 else 'all'
HEIGHT = 1.80
TEX = 512
os.makedirs(OUT, exist_ok=True)

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def bbox(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    lo = Vector((1e9,) * 3); hi = -lo
    for o in objs:
        e = o.evaluated_get(dg)
        for v in e.data.vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi

def apply_all(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

def shrink_images():
    for img in bpy.data.images:
        w, h = img.size
        if w > TEX or h > TEX:
            k = TEX / max(w, h)
            img.scale(max(1, int(w * k)), max(1, int(h * k)))

def export(path, objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_animations=False,
                              export_image_format='JPEG', export_jpeg_quality=85, export_yup=True, export_skins=True,
                              export_morph=False, export_extras=False)
    print('wrote', path, os.path.getsize(path))

def import_pg():
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, 'ww2_panzergrenadier_2k_cc0.glb'))
    new = [o for o in bpy.context.scene.objects if o not in before]
    arm = [o for o in new if o.type == 'ARMATURE'][0]
    meshes = [o for o in new if o.type == 'MESH']
    return arm, meshes

def unparent_keep(objs):
    for o in objs:
        m = o.matrix_world.copy(); o.parent = None; o.matrix_world = m

def normalise(arm, meshes):
    # flatten the hierarchy, scale to HEIGHT, feet on the ground, centred
    roots = [o for o in bpy.context.scene.objects if o.type == 'EMPTY']
    unparent_keep([arm] + meshes)
    for mo in meshes:
        for md in mo.modifiers:
            if md.type == 'ARMATURE': md.object = arm
        mo.parent = arm; mo.matrix_parent_inverse = arm.matrix_world.inverted()
    for e in roots: bpy.data.objects.remove(e, do_unlink=True)
    lo, hi = bbox(meshes)
    s = HEIGHT / (hi.z - lo.z)
    arm.scale = arm.scale * s
    bpy.context.view_layer.update()
    lo, hi = bbox(meshes)
    arm.location -= Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
    bpy.context.view_layer.update()
    apply_all([arm] + meshes)
    # transform_apply on the armature resets the meshes' parent inverse; keep the armature modifier binding
    return bbox(meshes)

def bake_axis():
    reset()
    arm, meshes = import_pg()
    lo, hi = normalise(arm, meshes)
    print('axis bbox', tuple(lo), tuple(hi))
    shrink_images()
    export(os.path.join(OUT, 'axis_grenadier.glb'), [arm] + meshes)

# ---------------------------------------------------------------- allied: rig the ranger variants
DEFORM_SKIP = ('Thumb', 'Index', 'Middle', 'Ring', 'Pinky', 'Toe_End', 'rootJoint')

def seg_dist(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (a + ab * t - p).length

def bake_allied():
    reset()
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, 'us_ranger_infantry_-_call_of_duty_2_hd.glb'))
    rmesh = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    unparent_keep(rmesh)
    for e in [o for o in bpy.context.scene.objects if o.type == 'EMPTY']: bpy.data.objects.remove(e, do_unlink=True)
    apply_all(rmesh)
    # cluster the meshes into variants by their x centre
    cent = []
    for o in rmesh:
        lo, hi = bbox([o]); cent.append(((lo.x + hi.x) / 2, o))
    cent.sort(key=lambda c: c[0])
    groups, cur, last = [], [], None
    for x, o in cent:
        if last is not None and x - last > 1.5: groups.append(cur); cur = []
        cur.append(o); last = x
    if cur: groups.append(cur)
    print('ranger variants:', len(groups), [len(g) for g in groups])
    # the skeleton source
    arm0, pgm = import_pg()
    for m in pgm: bpy.data.objects.remove(m, do_unlink=True)
    for e in [o for o in bpy.context.scene.objects if o.type == 'EMPTY']:
        if e is not arm0: pass
    unparent_keep([arm0])
    for e in [o for o in bpy.context.scene.objects if o.type == 'EMPTY']: bpy.data.objects.remove(e, do_unlink=True)
    apply_all([arm0])
    shrink_images()
    keep = {int(k) for k in os.environ.get('NMK_RANGER_VARIANTS', '0,2,4').split(',')}
    for gi, grp in enumerate(groups):
        if gi not in keep: continue
        for o in bpy.context.scene.objects: o.hide_set(False)
        # join the variant into one mesh (materials kept as slots)
        bpy.ops.object.select_all(action='DESELECT')
        for o in grp: o.select_set(True)
        bpy.context.view_layer.objects.active = grp[0]
        if len(grp) > 1: bpy.ops.object.join()
        body = bpy.context.view_layer.objects.active
        lo, hi = bbox([body])
        s = HEIGHT / (hi.z - lo.z)
        body.scale = (s, s, s); bpy.context.view_layer.update()
        lo, hi = bbox([body]); body.location -= Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z)); bpy.context.view_layer.update()
        apply_all([body])
        lo, hi = bbox([body])
        # armature copy fitted to this variant
        arm = arm0.copy(); arm.data = arm0.data.copy(); bpy.context.scene.collection.objects.link(arm)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        eb = arm.data.edit_bones
        # uniform scale of the skeleton to the variant height (helmet top -> sole), feet at 0, centred
        alo = Vector((1e9,) * 3); ahi = -alo
        for b in eb:
            for p in (b.head, b.tail): alo = Vector(map(min, alo, p)); ahi = Vector(map(max, ahi, p))
        head = [b for b in eb if b.name.split(':')[-1].startswith('Head')][0]
        top_bone = head.tail.z
        k = (HEIGHT * 0.94) / max(top_bone - alo.z, 1e-6)   # head bone tip sits ~6 % under the helmet top
        for b in eb:
            b.head = Vector(((b.head.x) * k, (b.head.y) * k, (b.head.z - alo.z) * k))
            b.tail = Vector(((b.tail.x) * k, (b.tail.y) * k, (b.tail.z - alo.z) * k))
        # arms: rotate each arm chain about its shoulder joint so the wrist meets the variant's hand
        verts = [body.matrix_world @ v.co for v in body.data.vertices]
        for side, sgn in (('Left', 1), ('Right', -1)):
            upper = [b for b in eb if b.name.split(':')[-1].startswith(side + 'Arm_')][0]
            fore = [b for b in eb if b.name.split(':')[-1].startswith(side + 'ForeArm')][0]
            hand = [b for b in eb if b.name.split(':')[-1].startswith(side + 'Hand_')][0]
            # glTF +x is the model's left when it faces +z (Blender -y): the variant's left hand has the largest x
            band = [v for v in verts if 0.35 * HEIGHT < v.z < 0.75 * HEIGHT and v.x * sgn > 0]
            tip = max(band, key=lambda v: v.x * sgn)
            sh = upper.head.copy()
            cur = (hand.tail - sh); want = (tip - sh)
            q = cur.rotation_difference(want)
            scale = want.length / max(cur.length, 1e-6)
            chain = [upper, fore, hand] + [b for b in eb if b.name.split(':')[-1].startswith(side + 'Hand') and b not in (hand,)]
            for b in chain:
                b.head = sh + (q @ (b.head - sh)) * scale
                b.tail = sh + (q @ (b.tail - sh)) * scale
        bpy.ops.object.mode_set(mode='OBJECT')
        # weights: inverse 4th power of the distance to each deforming bone segment, best 3
        bones = [(b.name, arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local) for b in arm.data.bones
                 if not any(t in b.name for t in DEFORM_SKIP)]
        vg = {n: body.vertex_groups.new(name=n) for n, _, _ in bones}
        for i, v in enumerate(body.data.vertices):
            p = body.matrix_world @ v.co
            ds = sorted(((seg_dist(p, a, b), n) for n, a, b in bones))[:3]
            ws = [(1.0 / (d ** 4 + 1e-6), n) for d, n in ds]
            tot = sum(w for w, _ in ws)
            for w, n in ws:
                if w / tot > 0.02: vg[n].add([i], w / tot, 'REPLACE')
        mod = body.modifiers.new('Armature', 'ARMATURE'); mod.object = arm
        body.parent = arm
        path = os.path.join(OUT, 'allied_ranger_%d.glb' % gi)
        export(path, [arm, body])
        # leave the scene clean for the next variant
        bpy.data.objects.remove(body, do_unlink=True); bpy.data.objects.remove(arm, do_unlink=True)

if WHAT in ('axis', 'all'): bake_axis()
if WHAT in ('allied', 'all'): bake_allied()
