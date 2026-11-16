# Blender (5.x) background script: bake the extra AI characters (zombie variants, operators) into game GLBs.
#   blender -b --factory-startup --disable-autoexec --python tools/bake_ai_characters.py -- <zombie.glb> <outdir> <what> <src...>
#   zombies <necromorph.fbx> <dead_man_animat.blend> <necro textures dir> <dead man textures dir>
#       -> zombie_necro.glb, zombie_deadman.glb: the supplied Mixamo rigs and meshes, with every clip of content/characters/
#          zombie.glb (RINONO) retargeted onto them (per bone: the source's world rotation from its rest, re-aimed so the
#          target bone's rest direction follows the source bone's; the hips' travel scaled by hip height). Same clip names,
#          so zombie.cpp plays them unchanged.
#   operators <stalker.fbx> <SWAT Operator Green.blend> <stalker textures dir> <SWAT textures dir>
#       -> operator_axis.glb (the stalker: Biped bone names renamed to the joint names), operator_allied.glb (the SWAT
#          operator: Mixamo, its Spine1 renamed Spine2, decimated to 20 k triangles, its rifle dropped). No clips: the
#          procedural soldier pose drives them (AI type Operators, content/ai/combat.json operator_models).
# The game reads content/ai/zombie.json (models). The sources are the
# user's downloads (docs/credits.md), extracted outside the repository.
# Output: metres, feet at 0, facing glTF +z, textures <= 1024 px JPEG. Source files are untrusted data: .blend files are
# opened with scripts disabled.
import bpy, sys, os, math
from mathutils import Vector, Matrix, Quaternion

argv = sys.argv[sys.argv.index('--') + 1:]
ZSRC, OUT, WHAT = argv[0], argv[1], argv[2]
SRCS = argv[3:]
TEX = 1024
FPS = 24
os.makedirs(OUT, exist_ok=True)
SEM = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand',
       'RightShoulder', 'RightArm', 'RightForeArm', 'RightHand', 'LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToeBase',
       'RightUpLeg', 'RightLeg', 'RightFoot', 'RightToeBase']
# content/characters/zombie.glb bone names (character.cpp aliasKey)
RINONO = {'hip': 'Hips', 'spine01': 'Spine', 'chest': 'Spine2', 'neck': 'Neck', 'head': 'Head',
          'shoulder_L': 'LeftShoulder', 'upper_arm_L': 'LeftArm', 'forearm_L': 'LeftForeArm', 'hand_L': 'LeftHand',
          'shoulder_R': 'RightShoulder', 'upper_arm_R': 'RightArm', 'forearm_R': 'RightForeArm', 'hand_R': 'RightHand',
          'thigh_L': 'LeftUpLeg', 'shin_L': 'LeftLeg', 'foot_L': 'LeftFoot', 'toe_L': 'LeftToeBase',
          'thigh_R': 'RightUpLeg', 'shin_R': 'RightLeg', 'foot_R': 'RightFoot', 'toe_R': 'RightToeBase'}

def strip_num(n):
    while '_' in n and n.rsplit('_', 1)[1].isdigit(): n = n.rsplit('_', 1)[0]
    return n

def sem_of(name):
    n = name.split(':')[-1]
    if n in SEM: return n
    return RINONO.get(strip_num(n))

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS

def objs_of(t): return [o for o in bpy.context.scene.objects if o.type == t]

def bbox(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    lo = Vector((1e9,) * 3); hi = -lo
    for o in objs:
        e = o.evaluated_get(dg)
        for v in e.data.vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi

def select(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]

def shrink_images():
    for img in bpy.data.images:
        w, h = img.size
        if w > TEX or h > TEX:
            k = TEX / max(w, h); img.scale(max(1, int(w * k)), max(1, int(h * k)))

def clear_anim(arm):
    if arm.animation_data:
        arm.animation_data.action = None
        for t in list(arm.animation_data.nla_tracks): arm.animation_data.nla_tracks.remove(t)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()

def normalise(arm, meshes, height):
    # one armature, its meshes parented with an armature modifier; scaled to height, feet at 0, centred, transforms applied
    for o in [arm] + meshes:
        m = o.matrix_world.copy(); o.parent = None; o.matrix_world = m
    for o in list(bpy.context.scene.objects):
        if o is not arm and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
    bpy.context.view_layer.update()
    lo, hi = bbox(meshes)
    s = height / (hi.z - lo.z)
    for o in [arm] + meshes:
        o.matrix_world = Matrix.Diagonal((s, s, s, 1)) @ o.matrix_world
    bpy.context.view_layer.update()
    lo, hi = bbox(meshes)
    off = Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z)))
    for o in [arm] + meshes: o.matrix_world = off @ o.matrix_world
    bpy.context.view_layer.update()
    select([arm] + meshes)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for mo in meshes:
        for md in mo.modifiers:
            if md.type == 'ARMATURE': md.object = arm
        mo.parent = arm; mo.matrix_parent_inverse = arm.matrix_world.inverted()
    bpy.context.view_layer.update()

def textured_material(name, color_png):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes.get('Principled BSDF')
    img = bpy.data.images.load(color_png); img.pack()
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img
    nt.links.new(tn.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.85; bsdf.inputs['Metallic'].default_value = 0.0
    return mat

def export(path, arm, meshes, actions):
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = None
    for t in list(ad.nla_tracks): ad.nla_tracks.remove(t)
    for a in actions:
        tr = ad.nla_tracks.new(); tr.name = a.name
        st = tr.strips.new(a.name, int(a.frame_range[0]), a)
        if hasattr(st, 'action_slot') and a.slots: st.action_slot = a.slots[0]
    clear_anim_pose(arm)
    select([arm] + meshes)
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_animations=True,
                              export_animation_mode='NLA_TRACKS', export_force_sampling=True, export_anim_slide_to_zero=True,
                              export_image_format='JPEG', export_jpeg_quality=85, export_yup=True, export_skins=True,
                              export_morph=False, export_extras=False, export_def_bones=False, export_reset_pose_bones=True)
    print('wrote', path, os.path.getsize(path))

def clear_anim_pose(arm):
    for pb in arm.pose.bones: pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()

def new_action(arm, name):
    a = bpy.data.actions.new(name); a.use_fake_user = True
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = a
    return a

def rest_info(arm):
    # armature-space rest matrices, rest directions (world) and the world matrix
    return {b.name: b.matrix_local.copy() for b in arm.data.bones}

def order(arm):
    out = []
    def walk(b):
        out.append(b)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    return out

def key_pose(arm, frame, rot, hips_loc=None, hips_name='Hips'):
    # rot: bone name -> armature-space posed rotation (Quaternion) for mapped bones; others keep their rest local.
    # Converts to local basis parent-first (P_b = P_p B_p^-1 B_b L_b) and keys rotation_quaternion (+ hips location).
    P = {}
    for b in order(arm):
        B = b.matrix_local
        if b.parent is None: Pp = Matrix(); Bp = Matrix()
        else: Pp = P[b.parent.name]; Bp = b.parent.matrix_local
        base = Pp @ Bp.inverted() @ B   # posed with an identity local
        L = Matrix()
        if b.name in rot:
            R = rot[b.name].to_matrix().to_4x4()
            want = Matrix.Translation(base.translation) @ R
            if b.parent is None and hips_loc is not None: want = Matrix.Translation(hips_loc) @ R
            L = base.inverted() @ want
        elif b.parent is None and hips_loc is not None:
            L = base.inverted() @ (Matrix.Translation(hips_loc) @ base.to_3x3().to_4x4())
        P[b.name] = base @ L
        pb = arm.pose.bones[b.name]
        loc, q, _ = L.decompose()
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = q
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        if b.parent is None:
            pb.location = loc; pb.keyframe_insert('location', frame=frame)
    return P

# ---------------------------------------------------------------- zombies: retarget RINONO's clips
# a bone's rest direction runs from its joint to the next joint of its chain (imported tails are guesses: the glTF
# importer can point a thigh's tail up); a chain end takes its parent's direction
NEXT = {'Hips': ['Spine'], 'Spine': ['Spine1', 'Spine2'], 'Spine1': ['Spine2'], 'Spine2': ['Neck'], 'Neck': ['Head']}
for _s in ('Left', 'Right'):
    NEXT.update({_s + 'Shoulder': [_s + 'Arm'], _s + 'Arm': [_s + 'ForeArm'], _s + 'ForeArm': [_s + 'Hand'],
                 _s + 'UpLeg': [_s + 'Leg'], _s + 'Leg': [_s + 'Foot'], _s + 'Foot': [_s + 'ToeBase']})
END = {'Head': 'Neck', 'LeftHand': 'LeftForeArm', 'RightHand': 'RightForeArm', 'LeftToeBase': 'LeftFoot', 'RightToeBase': 'RightFoot'}

def joint_dirs(r):
    out = {}
    for s, (qq, _, h) in r.items():
        nx = [n for n in NEXT.get(s, []) if n in r]
        out[s] = [qq, (r[nx[0]][2] - h).normalized() if nx else None, h]
    for s, e in END.items():
        if s in out and e in out: out[s][1] = out[e][1]
    return {s: tuple(v) for s, v in out.items() if v[1] is not None}

def load_source_clips():
    reset()
    # rest = the nodes' default pose, as the game has it (the guessed bind pose turns the hips 90 degrees off the clips)
    bpy.ops.import_scene.gltf(filepath=ZSRC, guess_original_bind_pose=False)
    arm = objs_of('ARMATURE')[0]
    M = arm.matrix_world
    bones = {sem_of(b.name): b for b in arm.data.bones if sem_of(b.name)}
    clear_anim(arm)
    rest = {}
    for s, b in bones.items():
        W = M @ b.matrix_local
        rest[s] = (W.to_quaternion(), None, M @ b.head_local)
    rest = joint_dirs(rest)
    hipH = rest['Hips'][2].z - min(rest['LeftFoot'][2].z, rest['RightFoot'][2].z)
    print('source facing (hips->toe):', (rest['LeftToeBase'][2] - rest['LeftFoot'][2]).normalized())
    clips = {}
    for a in bpy.data.actions:
        if a.name in ('Default Take',) or 'on_all_four' in a.name or a.name in ('eat', 'down', 'caution'): continue
        ad = arm.animation_data; ad.action = a
        if hasattr(ad, 'action_slot') and ad.action_slot is None and a.slots: ad.action_slot = a.slots[0]
        f0, f1 = int(a.frame_range[0]), int(round(a.frame_range[1]))
        frames = []
        for f in range(f0, f1 + 1):
            bpy.context.scene.frame_set(f)
            fr = {}
            for s, b in bones.items():
                W = M @ arm.pose.bones[b.name].matrix
                fr[s] = (W.to_quaternion(), W.translation.copy())
            frames.append(fr)
        clips[a.name] = frames
        print('clip', a.name, len(frames))
    return rest, hipH, clips

def retarget(arm, src_rest, src_hipH, clips):
    clear_anim(arm)
    M = arm.matrix_world; Mq = M.to_quaternion()
    tb = {sem_of(b.name): b for b in arm.data.bones if sem_of(b.name)}
    trest = {}
    for s, b in tb.items():
        trest[s] = ((M @ b.matrix_local).to_quaternion(), None, M @ b.head_local)
    trest = joint_dirs(trest)
    hipH = trest['Hips'][2].z - min(trest['LeftFoot'][2].z, trest['RightFoot'][2].z)
    k = hipH / src_hipH
    print('target facing:', (trest['LeftToeBase'][2] - trest['LeftFoot'][2]).normalized(), 'hip ratio', k)
    acts = []
    for name, frames in clips.items():
        a = new_action(arm, name)
        h0 = src_rest['Hips'][2]
        for i, fr in enumerate(frames):
            rot = {}
            for s, b in tb.items():
                if s not in fr or s not in src_rest: continue
                sq0, sd0, _ = src_rest[s]; tq0, td0, _ = trest[s]
                A = td0.rotation_difference(sd0)               # target rest direction -> source rest direction
                D = fr[s][0] @ sq0.inverted()                   # source world rotation since rest
                rot[b.name] = Mq.inverted() @ (D @ A @ tq0)
            hl = None
            if 'Hips' in fr:
                hw = trest['Hips'][2] + (fr['Hips'][1] - h0) * k
                hl = M.inverted() @ hw
            key_pose(arm, i, rot, hl, tb['Hips'].name)
        acts.append(a)
    return acts

def decimate(o, ratio):
    select([o]); d = o.modifiers.new('dec', 'DECIMATE'); d.ratio = ratio
    bpy.ops.object.modifier_move_to_index(modifier='dec', index=0)
    bpy.ops.object.modifier_apply(modifier='dec')

def bake_zombies():
    necro_fbx, dead_blend, necro_tex, dead_tex = SRCS[:4]
    src_rest, src_hipH, clips = load_source_clips()
    # ---- the necromorph slasher (Mixamo FBX, 3 k vertices, textures beside it)
    reset()
    bpy.ops.import_scene.fbx(filepath=necro_fbx)
    arm = objs_of('ARMATURE')[0]; meshes = objs_of('MESH')
    clear_anim(arm)
    normalise(arm, meshes, 1.85)
    mat = textured_material('necro', os.path.join(necro_tex, 'monster_LP_DefaultMaterial_BaseColor.png'))
    for mo in meshes: mo.data.materials.clear(); mo.data.materials.append(mat)
    shrink_images()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    acts = retarget(arm, src_rest, src_hipH, clips)
    export(os.path.join(OUT, 'zombie_necro.glb'), arm, meshes, acts)
    # ---- the dead man (Mixamo rig in a .blend, opened without scripts)
    bpy.ops.wm.open_mainfile(filepath=dead_blend, load_ui=False, use_scripts=False)
    bpy.context.scene.render.fps = FPS
    arm = objs_of('ARMATURE')[0]; meshes = [o for o in objs_of('MESH') if any(m.type == 'ARMATURE' for m in o.modifiers)]
    clear_anim(arm)
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH' and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
    for mo in meshes:
        if len(mo.data.polygons) > 20000: decimate(mo, 15000 / len(mo.data.polygons))
    normalise(arm, meshes, 1.85)
    mat = textured_material('deadman', os.path.join(dead_tex, 'lambert1_BaseColor.png'))
    for mo in meshes: mo.data.materials.clear(); mo.data.materials.append(mat)
    for img in list(bpy.data.images):
        if img.users == 0: bpy.data.images.remove(img)
    shrink_images()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    acts = retarget(arm, src_rest, src_hipH, clips)
    export(os.path.join(OUT, 'zombie_deadman.glb'), arm, meshes, acts)

# ---------------------------------------------------------------- operators: the supplied rigs, renamed to the joint names
BIP = {'pelvis': 'Hips', 'spine': 'Spine', 'spine1': 'Spine1', 'spine2': 'Spine2', 'neck': 'Neck', 'head': 'Head',
       'l_clavicle': 'LeftShoulder', 'l_upperarm': 'LeftArm', 'l_forearm': 'LeftForeArm', 'l_hand': 'LeftHand',
       'r_clavicle': 'RightShoulder', 'r_upperarm': 'RightArm', 'r_forearm': 'RightForeArm', 'r_hand': 'RightHand',
       'l_thigh': 'LeftUpLeg', 'l_calf': 'LeftLeg', 'l_foot': 'LeftFoot', 'l_toe0': 'LeftToeBase',
       'r_thigh': 'RightUpLeg', 'r_calf': 'RightLeg', 'r_foot': 'RightFoot', 'r_toe0': 'RightToeBase'}

def face_front(arm):
    # the soldier pose expects glTF +z forward (Blender -y): turn a rig whose toes point +y around
    b = {sem_of(x.name): x for x in arm.data.bones if sem_of(x.name)}
    d = arm.matrix_world.to_3x3() @ (b['LeftToeBase'].head_local - b['LeftFoot'].head_local)
    if d.y > 0:
        arm.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ arm.matrix_world
        bpy.context.view_layer.update()
    print('facing', d)

def bake_operators():
    global TEX
    TEX = 512
    stalker_fbx, swat_blend, stalker_tex, swat_tex = SRCS[:4]
    # ---- Axis: the stalker (FBX, 3ds Max Biped names, one material per part, textures beside it as .tga.png)
    reset()
    bpy.ops.import_scene.fbx(filepath=stalker_fbx)
    arm = objs_of('ARMATURE')[0]; meshes = objs_of('MESH')
    clear_anim(arm)
    for bn in arm.data.bones:
        k = bn.name[len('bip01_'):] if bn.name.startswith('bip01_') else None
        if k in BIP: bn.name = BIP[k]   # vertex groups follow the rename
    face_front(arm)
    normalise(arm, meshes, 1.80)
    mats = {}
    for mo in meshes:
        for sl in mo.material_slots:
            base = sl.material.name.replace(chr(92), '/').split('/')[-1]   # 'act\newloner\<texture>'
            png = os.path.join(stalker_tex, base + '.tga.png')
            if base not in mats and os.path.exists(png): mats[base] = textured_material(base, png)
            if base in mats: sl.material = mats[base]
    shrink_images()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    export(os.path.join(OUT, 'operator_axis.glb'), arm, meshes, [])
    # ---- Allies: the SWAT operator (.blend opened without scripts; Mixamo rig with a two-bone spine; its rifle dropped)
    bpy.ops.wm.open_mainfile(filepath=swat_blend, load_ui=False, use_scripts=False)
    bpy.context.scene.render.fps = FPS
    arm = objs_of('ARMATURE')[0]
    meshes = [o for o in objs_of('MESH') if any(m.type == 'ARMATURE' for m in o.modifiers)]
    clear_anim(arm)
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH' and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
    sp = arm.data.bones.get('mixamorig:Spine1')
    if sp and not arm.data.bones.get('mixamorig:Spine2'): sp.name = 'mixamorig:Spine2'   # the game needs a Spine2 joint
    face_front(arm)
    for mo in meshes:
        if len(mo.data.polygons) > 24000: decimate(mo, 20000 / len(mo.data.polygons))
    normalise(arm, meshes, 1.80)
    tex = {'Body': 'Body_Base_color.png', 'HeadGear': 'HeadGear_Base_color.png', 'Eyelashes': 'eyelashes01.png'}
    for mo in meshes:
        for sl in mo.material_slots:
            base = sl.material.name.split('.')[0]
            if base in tex: sl.material = textured_material(base, os.path.join(swat_tex, tex[base]))
    for img in list(bpy.data.images):
        if img.users == 0: bpy.data.images.remove(img)
    shrink_images()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    export(os.path.join(OUT, 'operator_allied.glb'), arm, meshes, [])

# ---------------------------------------------------------------- SW1 kits: blaster troopers (three looks) and the robot commando
#   kits <trooper.fbx> <looks: name=armour textures dir,...> <undersuit textures dir> <robot.fbx> <robot texture.png>
#       -> trooper_<name>.glb (each look: the supplied mesh and rig, already on the joint names; 1.83 m), robot_commando.glb
#          (Mixamo rig; only the full-detail body and its lights kept, the LOD meshes dropped; 1.90 m). No clips: the procedural
#          soldier pose drives them (AI types Blaster troopers / Robot commandos, content/ai/combat.json kits). Local-only output.
def flat_material(name, rgb, emit=0.0):
    mat = bpy.data.materials.new(name); mat.use_nodes = True; b = mat.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (*rgb, 1); b.inputs['Roughness'].default_value = 0.4
    if emit > 0 and 'Emission Color' in b.inputs: b.inputs['Emission Color'].default_value = (*rgb, 1); b.inputs['Emission Strength'].default_value = emit
    return mat

def bake_kits():
    global TEX
    TEX = 1024
    trooper_fbx, looks, under_dir, robot_fbx, robot_png = SRCS[:5]
    for look in looks.split(','):
        name, armour_dir = look.split('=', 1)
        reset()
        bpy.ops.import_scene.fbx(filepath=trooper_fbx)
        arm = objs_of('ARMATURE')[0]; meshes = [o for o in objs_of('MESH') if any(m.type == 'ARMATURE' for m in o.modifiers)]
        clear_anim(arm)
        face_front(arm)
        normalise(arm, meshes, 1.83)
        tex = {'Clone_P2_Armor': os.path.join(armour_dir, 'Clone Trooper (Phase2)_Clone_P2_Armor_BaseColor.png'),
               'Phase2_Helmet': os.path.join(armour_dir, 'Clone Trooper (Phase2)_Phase2_Helmet_BaseColor.png'),
               'Undersuit': os.path.join(under_dir, 'Clone Trooper (Phase2)_Undersuit_BaseColor.png')}
        mats = {}
        for mo in meshes:
            for sl in mo.material_slots:
                base = sl.material.name.split('.')[0] if sl.material else mo.name
                if base in tex and os.path.exists(tex[base]):
                    if base not in mats: mats[base] = textured_material(base, tex[base])
                    sl.material = mats[base]
        for img in list(bpy.data.images):
            if img.users == 0: bpy.data.images.remove(img)
        shrink_images()
        for a in list(bpy.data.actions): bpy.data.actions.remove(a)
        export(os.path.join(OUT, 'trooper_' + name + '.glb'), arm, meshes, [])
    reset()
    bpy.ops.import_scene.fbx(filepath=robot_fbx)
    arm = objs_of('ARMATURE')[0]
    keep = {'M_body', 'M_Glow_red', 'M_Glow_white'}   # the full-detail meshes (M_body1-5, M_Glow_*1-3 are lower LODs)
    meshes = [o for o in objs_of('MESH') if o.name in keep]
    clear_anim(arm)
    face_front(arm)
    normalise(arm, meshes, 1.90)
    body = textured_material('robot', robot_png)
    for mo in meshes:
        m = body if mo.name == 'M_body' else flat_material(mo.name, (1.0, 0.15, 0.05) if 'red' in mo.name else (0.9, 0.9, 0.85), 4.0)
        mo.data.materials.clear(); mo.data.materials.append(m)
    shrink_images()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    export(os.path.join(OUT, 'robot_commando.glb'), arm, meshes, [])

if WHAT == 'zombies': bake_zombies()
if WHAT == 'operators': bake_operators()
if WHAT == 'kits': bake_kits()
