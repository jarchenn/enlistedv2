# EN1/EN2 Blender background converter: FBX (armature + skinned mesh + takes) -> skinned GLB for the game's CharModel.
# - only the armature and the named meshes are kept (effect empties, alpha hair cards dropped)
# - the meshes are re-based into the armature's space (data transformed, parented, identity local) so the skinned
#   vertices stand where the bones are (the troll's mesh carried a 90 deg turn of its own)
# - object-level curves (root motion and the FBX parent chain's transform) are dropped from the takes: the armature
#   object keeps its import transform and the clips play in place; only pose.bones curves stay
# - the model turned to face glTF +z (the game's clip analysis assumes it), one action per take (armature only)
# - bone names are kept in Blender (renaming there does not reach the curves of inactive actions) and renamed in the
#   exported GLB: mapped bones to the game's semantic joint names (src/character.cpp kSemanticNames), others sanitized
# - textures: albedo + normal from the given PNGs, at most 1024 px
# - a model without a death take may get one (7th argument: the bone at its feet): a 1 s topple backward from its first
#   idle frame, pivoting on that bone (the creature, SCP-096 remodel)
# - CH1: an optional spec JSON (8th argument; the 7th is then '-' for no synthesized death):
#     height_m: the rest-pose mesh scaled to this height (sources in cm or odd units: the game's foot analysis is in metres)
#     jpeg: textures exported as JPEG (85) instead of PNG
#     no_scale: bone scale keys dropped from the cut clips (every clip at the bind pose's size)
#     rest_pose: a clip name: its first frame becomes the bind pose (meshes deformed into it, every clip re-keyed against it).
#       For a source whose bind pose lies on its side and whose takes stand it up: the game sizes and grounds the bind pose
#     forward: "+x" / "-x" / "+y" / "-y" or an angle in degrees: the model's front in the imported scene, instead of judging it
#       mid-walk (that judgement only tells +y from -y)
#     clips: [{name, take, range: [f0, f1], inplace}]: a clip cut from a take (a long mocap take holding many moves),
#       inplace: the hips' horizontal travel from the first frame to the last taken off linearly (the game plays clips
#       in place); drop_takes: [take name parts]: takes removed once cut
#     synth_walk: {name, base, legs: [[thigh, knee, foot], ...], frames, swing_deg, knee_deg}: a walk cycle for a model
#       without one, from the first frame of the take `base`: the thighs swing (legs in opposite phase), the knee lifts
#       the foot on the forward swing and the foot stays level
# - <alb.png> may instead be a .json {source material name: [albedo, normal or null]} (file names in the directory given
#   as <nrm.png>): the mesh keeps its materials
# usage: blender -b --factory-startup --python convert_enemy_glb.py -- <fbx> <out.glb> <alb.png> <nrm.png> <keep_mesh,...> <map.json> [<death pivot bone> | -] [<spec.json>]
import bpy, sys, json, re, math, os, struct
from mathutils import Matrix, Quaternion, Vector
a = sys.argv[sys.argv.index('--') + 1:]
src, out, alb, nrm, keep, mapf = a[0], a[1], a[2], a[3], a[4].split(','), a[5]
spec = json.load(open(a[7])) if len(a) > 7 else {}
bmap = json.load(open(mapf))   # semantic name -> source bone name
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.name in keep]
assert meshes, [o.name for o in bpy.data.objects if o.type == 'MESH']
missing = [v for v in bmap.values() if v not in arm.data.bones]
assert not missing, missing
bpy.context.view_layer.update()
# armature: unparent with its import (frame 1) world transform kept; its takes lose every non-bone curve
aw = arm.matrix_world.copy(); arm.parent = None; arm.matrix_world = aw
for act in list(bpy.data.actions):
    parts = act.name.split('|')
    if parts[0] != arm.name or act.frame_range[1] - act.frame_range[0] < 1: bpy.data.actions.remove(act); continue   # other objects' takes, one-frame stubs
    names = [p for p in parts[1:] if p != arm.name and not p.startswith('Base Layer')]   # "Arm|Walk" or "Arm|Arm|Arm|C.096_DWlk.F|Base Layer"
    act.name = names[0] if names else parts[-1]
    act.use_fake_user = True
    for l in act.layers:
        for s in l.strips:
            for cb in s.channelbags:
                for fc in [f for f in cb.fcurves if not f.data_path.startswith('pose.bones')]: cb.fcurves.remove(fc)
def use(act):
    ad = arm.animation_data or arm.animation_data_create(); ad.action = act
    if act.slots: ad.action_slot = act.slots[0]
def take(part): return next(x for x in bpy.data.actions if part in x.name)
def key_all(f):
    for p in arm.pose.bones:
        for path in ('location', 'rotation_quaternion', 'scale'): p.keyframe_insert(path, frame=f)
fps = bpy.context.scene.render.fps; step = max(1, round(fps / 30))   # 60 fps mocap keyed every other frame
roots = [p for p in arm.pose.bones if not p.parent]
rest_hips = arm.matrix_world @ arm.data.bones[bmap['Hips']].head_local
for c in spec.get('clips', []):
    src_act = take(c['take']); f0, f1 = c['range']
    samples = []
    for f in range(f0, f1 + 1, step):
        use(src_act); bpy.context.scene.frame_set(f)
        basis = {p.name: p.matrix_basis.copy() for p in arm.pose.bones}
        if spec.get('no_scale'):   # bone scale keys dropped: one take scaling its root 1.77x made the model grow in that clip
            for k, m in basis.items(): l, r, _ = m.decompose(); basis[k] = Matrix.LocRotScale(l, r, Vector((1, 1, 1)))
        samples.append((basis, {p.name: p.matrix.copy() for p in roots}, arm.matrix_world @ arm.pose.bones[bmap['Hips']].head))
    drift = samples[-1][2] - samples[0][2]; drift.z = 0
    start = samples[0][2] - rest_hips; start.z = 0   # a mocap take keeps where on the stage it was: each clip starts over the rest hips
    act = bpy.data.actions.new(c['name']); act.use_fake_user = True; use(act)
    for i, (basis, rootm, _) in enumerate(samples):
        for p in arm.pose.bones: p.rotation_mode = 'QUATERNION'; p.matrix_basis = basis[p.name]
        d = arm.matrix_world.inverted().to_3x3() @ (start + (drift * (i / max(1, len(samples) - 1)) if c.get('inplace') else Vector()))
        for p in roots: m = rootm[p.name].copy(); m.translation -= d; p.matrix = m
        key_all(i * step + 1)
    print('CLIP', c['name'], f0, f1, 'travel taken off' if c.get('inplace') else 'travel kept', round(drift.length, 3), 'start offset taken off', round(start.length, 3))
for part in spec.get('drop_takes', []):
    for x in [x for x in bpy.data.actions if part in x.name]: bpy.data.actions.remove(x)
if arm.animation_data: arm.animation_data.action = None
arm.matrix_world = aw
# meshes: vertices into the armature's space, parented to it with an identity local transform. The FBX import can
# leave a mesh turned against its bones (the troll's lay on its back, mirrored), so the placement is fitted: of the
# 24 axis-aligned turns of the mesh data (scaled like the imported placement), the one that brings each vertex
# nearest the bone it is mostly weighted to wins.
import itertools
def seg(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(1e-12, ab.dot(ab)))); return (p - (a + ab * t)).length
turns = []
for perm in itertools.permutations(range(3)):
    for sg in itertools.product((1, -1), repeat=3):
        R = Matrix.Identity(4)
        for r in range(3):
            for c in range(3): R[r][c] = sg[r] if perm[r] == c else 0
        if R.to_3x3().determinant() > 0: turns.append(R)
fits = {}
for m in meshes:
    rel = aw.inverted() @ m.matrix_world; sc = Matrix.Diagonal(rel.to_scale().to_4d())
    gname = {g.index: g.name for g in m.vertex_groups}
    samp = []
    for v in list(m.data.vertices)[::max(1, len(m.data.vertices) // 3000)]:   # (Blender 5 collections take no slice step)
        g = max(v.groups, key=lambda x: x.weight, default=None)
        b = arm.data.bones.get(gname.get(g.group, '')) if g else None
        if b: samp.append((v.co.copy(), b.head_local, b.tail_local))
    fits[m.name] = (sc, [sum(seg(R @ sc @ co, h, t) for co, h, t in samp) / max(1, len(samp)) for R in turns], len(samp))
# spec same_turn: one turn for all the meshes (a near-symmetric part can tie between turns on its own)
allfit = [sum(f[1][i] * f[2] for f in fits.values()) for i in range(len(turns))]
for m in meshes:
    sc, fit, _ = fits[m.name]
    k = allfit.index(min(allfit)) if spec.get('same_turn') else fit.index(min(fit)); M = turns[k] @ sc
    print('MESH', m.name, 'turn', [[round(x) for x in r] for r in turns[k].to_3x3()], 'mean dist to bone', round(fit[k], 4), 'next best', round(sorted(fit)[1], 4))
    m.data.transform(M)
    m.parent = arm; m.parent_type = 'OBJECT'; m.matrix_parent_inverse = Matrix.Identity(4); m.matrix_basis = Matrix.Identity(4)
    for md in m.modifiers:
        if md.type == 'ARMATURE': md.object = arm
for o in list(bpy.data.objects):
    if o not in [arm] + meshes: bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
# facing: the head ahead of the hips along +y (Blender) -> turn 180 deg so it faces -y (glTF +z). Judged mid-walk:
# the rest pose can face elsewhere (the spider's bind pose faces opposite its clips; the troll's stands near vertical)
walk = next((x for x in bpy.data.actions if ('walk' in x.name.lower() or 'wlk' in x.name.lower()) and 'back' not in x.name.lower()), None)
if walk:
    arm.animation_data_create(); arm.animation_data.action = walk
    if walk.slots: arm.animation_data.action_slot = walk.slots[0]
    bpy.context.scene.frame_set(int(sum(walk.frame_range) / 2)); bpy.context.view_layer.update()
pb = arm.pose.bones
fy = (arm.matrix_world @ pb[bmap['Head']].head).y - (arm.matrix_world @ pb[bmap['Hips']].head).y
if walk: arm.animation_data.action = None; bpy.context.scene.frame_set(1); arm.matrix_world = aw
if 'forward' in spec:   # turn that front to -y (glTF +z)
    fv = spec['forward']   # an axis, or the front's angle in degrees (atan2 y, x: a rig stepping off an axis)
    fw = (math.cos(math.radians(fv)), math.sin(math.radians(fv))) if isinstance(fv, (int, float)) else {'+x': (1, 0), '-x': (-1, 0), '+y': (0, 1), '-y': (0, -1)}[fv]
    arm.matrix_world = Matrix.Rotation(-math.pi / 2 - math.atan2(fw[1], fw[0]), 4, 'Z') @ arm.matrix_world
    print('FACING from spec', spec['forward'])
else:
    if fy > 0: arm.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ arm.matrix_world
    print('FACING', 'turned' if fy > 0 else 'kept', round(fy, 5))
if spec.get('rest_pose'):
    pbs = arm.pose.bones; acts = list(bpy.data.actions); rec = {}
    for x in acts:   # each clip as armature-space bone matrices at its keyed frames
        use(x); fr = sorted({int(round(k.co[0])) for l in x.layers for s in l.strips for cb in s.channelbags for fc in cb.fcurves[:1] for k in fc.keyframe_points})
        rec[x.name] = []
        for f in fr:
            bpy.context.scene.frame_set(f); rec[x.name].append((f, {p.name: p.matrix.copy() for p in pbs}))
    use(take(spec['rest_pose'])); bpy.context.scene.frame_set(int(take(spec['rest_pose']).frame_range[0])); bpy.context.view_layer.update()
    for m in meshes:   # the meshes take that pose as their shape, then follow the armature again
        bpy.ops.object.select_all(action='DESELECT'); m.select_set(True); bpy.context.view_layer.objects.active = m
        md = next(d for d in m.modifiers if d.type == 'ARMATURE'); nm0 = md.name
        bpy.ops.object.modifier_apply(modifier=nm0)
        m.modifiers.new(nm0, 'ARMATURE').object = arm
    arm.animation_data.action = None
    bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='POSE'); bpy.ops.pose.armature_apply(selected=False); bpy.ops.object.mode_set(mode='OBJECT')
    restm = {b.name: b.matrix_local.copy() for b in arm.data.bones}
    Mw = arm.matrix_world; feet = [bmap[k] for k in ('LeftFoot', 'RightFoot') if k in bmap]
    rest_ank = min((Mw @ restm[b].translation).z for b in feet)
    for x in acts:   # re-keyed: basis = (rest_parent^-1 rest)^-1 pose_parent^-1 pose
        name = x.name; bpy.data.actions.remove(x)
        if not name.startswith('death'):   # grounded: its lowest ankle over the clip at the bind pose's (a charge keyed with the body raised)
            dz = rest_ank - min((Mw @ pm[b].translation).z for _, pm in rec[name] for b in feet)
            d = Mw.inverted().to_3x3() @ Vector((0, 0, dz))
            for _, pm in rec[name]:
                for m in pm.values(): m.translation += d
            if abs(dz) > 1e-6: print('GROUNDED', name, round(dz / Mw.to_scale().z, 4), '(armature units)')
        a2 = bpy.data.actions.new(name); a2.use_fake_user = True; use(a2)
        for f, pm in rec[name]:
            for p in pbs:
                par = p.parent
                L = restm[p.name] if not par else restm[par.name].inverted() @ restm[p.name]
                P0 = pm[par.name] if par else Matrix.Identity(4)
                p.rotation_mode = 'QUATERNION'; p.matrix_basis = L.inverted() @ P0.inverted() @ pm[p.name]
            key_all(f)
    arm.animation_data.action = None
    print('REST POSE from', spec['rest_pose'], len(acts), 'clips re-keyed')
if 'height_m' in spec:   # the rest mesh (undeformed vertices) to that height
    bpy.context.view_layer.update()
    zs = [(m.matrix_world @ v.co).z for m in meshes for v in m.data.vertices]
    k = spec['height_m'] / (max(zs) - min(zs))
    arm.matrix_world = Matrix.Scale(k, 4) @ arm.matrix_world; bpy.context.view_layer.update()
    print('SCALED', round(k, 5))
sw = spec.get('synth_walk')
if sw:
    use(take(sw['base'])); bpy.context.scene.frame_set(int(take(sw['base']).frame_range[0])); bpy.context.view_layer.update()
    base = {p.name: p.matrix_basis.copy() for p in arm.pose.bones}
    act = bpy.data.actions.new(sw['name']); act.use_fake_user = True; use(act)
    n = sw.get('frames', 32); A = math.radians(sw.get('swing_deg', 22)); K = math.radians(sw.get('knee_deg', 35))
    Mw = arm.matrix_world
    def turn(name, th):   # about the world x axis (the model's side to side: it faces -y) through the bone's head
        p = arm.pose.bones[name]; h = p.head.copy()
        Ra = (Mw.inverted() @ Matrix.Rotation(th, 4, 'X') @ Mw).to_3x3().normalized().to_4x4()
        p.matrix = Matrix.Translation(h) @ Ra @ Matrix.Translation(-h) @ p.matrix; bpy.context.view_layer.update()
    for i in range(n + 1):
        for p in arm.pose.bones: p.rotation_mode = 'QUATERNION'; p.matrix_basis = base[p.name]
        bpy.context.view_layer.update()
        for k, (thigh, knee, foot) in enumerate(sw['legs']):
            q = 2 * math.pi * i / n + k * math.pi
            sa, kb = A * math.sin(q), K * max(0.0, math.cos(q))   # forward swing; the knee lifts the foot while it swings forward
            turn(thigh, -sa); turn(knee, kb); turn(foot, sa - kb)
        key_all(i + 1)
    print('SYNTH WALK', sw['name'], n, 'frames')
if len(a) > 6 and a[6] != '-' and not any('death' in x.name.lower() or 'die' in x.name.lower() for x in bpy.data.actions):
    idle = next(x for x in bpy.data.actions if 'idle' in x.name.lower())
    arm.animation_data_create(); arm.animation_data.action = idle
    if idle.slots: arm.animation_data.action_slot = idle.slots[0]
    bpy.context.scene.frame_set(int(idle.frame_range[0])); bpy.context.view_layer.update()
    base = {p.name: p.matrix_basis.copy() for p in arm.pose.bones}
    death = bpy.data.actions.new('death'); death.use_fake_user = True; arm.animation_data.action = death
    pb = arm.pose.bones[a[6]]; Mw = arm.matrix_world; Ra = Mw.to_quaternion(); Bq = pb.bone.matrix_local.to_quaternion()
    tall = (Mw @ arm.data.bones[bmap['Head']].head_local).z - (Mw @ pb.bone.head_local).z
    fps = bpy.context.scene.render.fps; n = fps   # one second
    for f in range(0, n + 1, 2):
        w = (f / n) ** 2   # falls ever faster
        for p in arm.pose.bones:
            p.rotation_mode = 'QUATERNION'; p.matrix_basis = base[p.name]
        qw = Quaternion((1, 0, 0), math.radians(-88 * w))   # world: the top goes back (+y; she faces -y)
        loc, rot, sca = base[pb.name].decompose()
        rot = Bq.inverted() @ Ra.inverted() @ qw @ Ra @ Bq @ rot
        loc = loc + Bq.inverted() @ (Mw.inverted().to_3x3() @ Vector((0, 0, 0.06 * tall * w)))   # the back rests on the ground, not through it
        pb.matrix_basis = Matrix.LocRotScale(loc, rot, sca)
        for p in arm.pose.bones:
            for path in ('location', 'rotation_quaternion', 'scale'): p.keyframe_insert(path, frame=f + 1)
    arm.animation_data.action = None; bpy.context.scene.frame_set(1)
print('ACTIONS', len(bpy.data.actions), sorted((x.name, tuple(x.frame_range)) for x in bpy.data.actions))
# materials: one principled material from the two textures
def img(p):
    i = bpy.data.images.load(p)
    if max(i.size) > 1024: i.scale(1024 * i.size[0] // max(i.size), 1024 * i.size[1] // max(i.size))
    return i
def material(name, albp, nrmp):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes['Principled BSDF']
    ta = nt.nodes.new('ShaderNodeTexImage'); ta.image = img(albp); nt.links.new(ta.outputs['Color'], bsdf.inputs['Base Color'])
    if nrmp:
        tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img(nrmp); tn.image.colorspace_settings.name = 'Non-Color'
        nm = nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tn.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    bsdf.inputs['Roughness'].default_value = 0.8; bsdf.inputs['Metallic'].default_value = 0.0
    return mat
if alb.endswith('.json'):   # per source material
    texmap = json.load(open(alb)); made = {}
    for m in meshes:
        for sl in m.material_slots:
            n0 = sl.material.name if sl.material else ''
            assert n0 in texmap, ('no textures for material', n0, m.name)
            if n0 not in made: made[n0] = material(n0, *[os.path.join(nrm, t) if t else None for t in texmap[n0]])
            sl.material = made[n0]
else:
    mat = material('body', alb, nrm)
    for m in meshes:
        m.data.materials.clear(); m.data.materials.append(mat)
        for p in m.data.polygons: p.material_index = 0
for o in bpy.data.objects: o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_animations=True, export_animation_mode='ACTIONS',
                          export_skins=True, export_yup=True, export_image_format='JPEG' if spec.get('jpeg') else 'AUTO', export_jpeg_quality=85, export_force_sampling=True)
# GLB node names: semantic for the mapped bones, sanitized for the rest (the JSON chunk is rewritten, the binary kept)
f = open(out, 'rb').read(); n = struct.unpack('<I', f[12:16])[0]; J = json.loads(f[20:20 + n]); rest = f[20 + n:]
inv = {v: k for k, v in bmap.items()}
for nd in J['nodes']:
    nm0 = nd.get('name', '')
    nd['name'] = inv[nm0] if nm0 in inv else ('b_' + re.sub(r'[^A-Za-z0-9]+', '_', nm0).strip('_'))
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(out, 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + len(rest)) + struct.pack('<II', len(js), 0x4E4F534A) + js + rest)
print('WROTE', out, os.path.getsize(out))
