# GR9: a copy of E:/nmk/boltguns/tools/boltgun_preview.py (BG4) with the Garand en-bloc reload changed (see gar_enbloc).
# BG4 (Blender background): reload previews for the Kar98k / Kar98k sniper / M1 Garand / M1 Garand sniper on the M24's
# first-person arms (preview only: nothing goes into the game). One timeline per gun:
#   Kar98k(s): the bolt cycle (the M24's fire clip, the hand on the Kar98k's knob), the reload not empty (bolt open, single
#   rounds pressed into the open action, bolt closed), the reload empty (bolt open, a stripper clip seated in the guide and its
#   rounds thumbed down, bolt closed: the empty clip flies out; the scoped rifle loads five single rounds: the scope blocks the
#   guide)
#   Garand(s): the reload not empty (the operating rod handle pulled back and held, single rounds pressed in on top of the clip,
#   released), the reload empty (bolt locked back, an en-bloc clip pressed down along the file's own clip path, the hand clear,
#   the bolt runs home)
# Arms: every bone copied from an M24 source frame; the rifle (the M24's gun empty) rolled toward the camera in the reloads; both
# wrists by analytic IK (rig_scene.arm_ik): the left kept on the forend, the right on keyed contact points (knob / handle,
# a pouch below right, the open action) with the M24's knob-hold rotation; rounds and clips keyed props at the right hand.
#   blender -b --factory-startup --disable-autoexec --python boltgun_preview.py -- <kar|kar_s|gar|gar_s> <out dir> [render 0|1] [stills frames]
import bpy, sys, math, json
from mathutils import Vector, Matrix, Quaternion
A = sys.argv[sys.argv.index('--') + 1:]; GUN, OUT = A[0], A[1]; RENDER = len(A) < 3 or A[2] == '1'; STILLS = [int(x) for x in A[3].split(',')] if len(A) > 3 else []
exec(open('E:/nmk/boltguns/tools/rig_scene.py').read())
gun, trig, fwd, up = setup_m24(); sc = bpy.context.scene; arm = bpy.data.objects['Armature']; ob = bpy.data.objects
right_w = fwd.cross(up)
# ---- the arms' own textures (the FBX references them by path)
TX = 'C:/Users/50bmg/Documents/MapMaker/incoming/modern_guns/sniper-animated/textures/'
am = bpy.data.materials.new('arms'); am.use_nodes = True; nt = am.node_tree; bs = nt.nodes['Principled BSDF']
ti = nt.nodes.new('ShaderNodeTexImage'); ti.image = bpy.data.images.load(TX + 'armColor.png'); nt.links.new(ti.outputs[0], bs.inputs['Base Color'])
tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = bpy.data.images.load(TX + 'armNormal.png'); tn.image.colorspace_settings.name = 'Non-Color'; nm = nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tn.outputs[0], nm.inputs['Color']); nt.links.new(nm.outputs[0], bs.inputs['Normal'])
bs.inputs['Roughness'].default_value = 0.8
for i in range(len(ob['armmesh'].data.materials)): ob['armmesh'].data.materials[i] = am
# ---- the M24's source frames: every bone's local pose, the gun empty, both wrists relative to the gun
SRCF = list(range(1, 50)) + [149, 29]
REC, RG, RW = {}, {}, {}
m24knob = ob['bolt']
def cen(o): return sum((o.matrix_world @ Vector(c) for c in o.bound_box), Vector()) / 8
KM = {}
# the M24's handle tip: the bolt mesh's vertices furthest to the right at rest, followed through the clips (its whole-bolt
# centre sits inside the receiver: the hand would stand off by the handle's length)
sc.frame_set(1); bpy.context.view_layer.update()
_Vw = [m24knob.matrix_world @ v.co for v in m24knob.data.vertices]; _xr = min(v.dot(right_w) * -1 for v in _Vw)
_rt = max(v.dot(right_w) for v in _Vw); TIP = [i for i, v in enumerate(_Vw) if v.dot(right_w) > _rt - 0.006]
def m24tip(): return sum((m24knob.matrix_world @ m24knob.data.vertices[i].co for i in TIP), Vector()) / len(TIP)
for sf in SRCF:
    sc.frame_set(sf); bpy.context.view_layer.update()
    REC[sf] = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}; RG[sf] = gun.matrix_world.copy()
    RW[sf] = {s: gun.matrix_world.inverted() @ (arm.matrix_world @ arm.pose.bones[s + '_wrist'].matrix) for s in 'LR'}
    KM[sf] = gun.matrix_world.inverted() @ m24tip()
    lb = gun.matrix_world.inverted() @ m24knob.matrix_world
    REC[sf]['_lift'] = min(1.0, abs(math.degrees(lb.to_euler().z)) / 90.0); REC[sf]['_slide'] = max(0.0, min(1.0, (10.844 - lb.translation.z) / 10.986))
# the right hand at the M24's knob (frame 29): its rotation relative to the gun and the wrist -> knob offset in hand space
R_KNOB = RW[29]['R'].to_3x3().normalized(); O_KNOB = R_KNOB.inverted() @ ((KM[29] - RW[29]['R'].translation) * RG[29].to_scale()[0])   # world units (the gun empty is scaled)
# ---- the rifle
KAR = GUN.startswith('kar'); SCOPED = GUN.endswith('_s')
def import_glb(p):
    before = set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=p); new = [o for o in bpy.data.objects if o not in before]; return new, next(o for o in new if o.parent is None)
SCOPE_PARTS = ['polySurface%d' % i for i in (3, 13, 23, 21, 42, 41, 32, 31, 43, 46, 20, 30, 53, 54, 29, 19, 45)]
TAKE = {}
KFREE = KAR and __import__('os').environ.get('KAR_OLD') != '1'   # KF1: the user's new Kar98k base (kar_free_prep.py); KAR_OLD=1: the old file
if KFREE:
    KFM = json.load(open('E:/nmk/kar_free/kar_free.json'))
    old_scope = []
    if SCOPED:   # the old file's scope, moved onto the new rifle: the same place over the bolt face, the same height over the bore
        onew, oroot = import_glb('E:/nmk/incoming_kar98k/kar98k.glb')
        for o in onew:
            if o.animation_data: o.animation_data_clear()
        bpy.context.view_layer.update()
        ob_ = {o.name: o for o in onew}
        bolt_ms = [o for o in ob_['zatvor'].children_recursive if o.type == 'MESH']; BV = [o.matrix_world @ v.co for o in bolt_ms for v in o.data.vertices]
        obf = max(p.x for p in BV); fr_ = [p for p in BV if p.x > obf - 3.0]; obore = Vector((0, (min(p.y for p in fr_) + max(p.y for p in fr_)) / 2, (min(p.z for p in fr_) + max(p.z for p in fr_)) / 2))
        dlt = Vector((KFM['bolt_front'] - obf, KFM['bore'][1] - obore.y, KFM['bore'][2] - obore.z))
        for o in onew:
            if o.type == 'MESH' and o.name.split('_phong')[0] in SCOPE_PARTS: W_ = o.matrix_world.copy(); o.parent = None; o.matrix_world = Matrix.Translation(dlt) @ W_; old_scope.append(o)
        for o in onew:
            if o not in old_scope: bpy.data.objects.remove(o, do_unlink=True)
        for o in old_scope: o.name = 'OLDSCOPE_' + o.name
        print('KF1 scope moved by', [round(x, 2) for x in dlt])
    with bpy.data.libraries.load('E:/nmk/kar_free/kar_free.blend') as (src, dst): dst.objects = list(src.objects)
    new = [o for o in dst.objects if o is not None]
    for o in new:
        if o.name not in sc.collection.objects: sc.collection.objects.link(o)
    root = ob['KF_ROOT']
    for o in old_scope: W_ = o.matrix_world.copy(); o.parent = root; o.matrix_world = W_; new.append(o)
    bpy.context.view_layer.update(); SRC0 = root.matrix_world.copy()
    place(root, ob['KF_TRIGGER'].matrix_world.translation.copy(), Vector((1, 0, 0)), Vector((0, 0, 1)), 0.01 * K, gun, trig, fwd, up)
    zat, g10 = ob['zatvor'], ob['group10']; knob = ob['KF_KNOB']; TRAVEL = KFM['travel']
    def set_bolt(u, v): zat.location.x = TRAVEL * (1 - v); g10.rotation_euler.x = math.radians(90 * (1 - u))
    def key_bolt(f): zat.keyframe_insert('location', frame=f); g10.keyframe_insert('rotation_euler', frame=f)
    CM = 1.0
    rnd = ob['KF_ROUND']   # the file's own cartridge, moved into the magazine's top (the model's 'top round')
    FWD_S, UP_S, RIGHT_S = Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0))
    PORT = Vector(KFM['feed'])
elif KAR:
    new, root = import_glb('E:/nmk/incoming_kar98k/kar98k.glb')
    for o in new:
        if o.animation_data: o.animation_data_clear()
        b = o.name.split('_phong')[0]
        if b == 'polySurface51' or (not SCOPED and b in SCOPE_PARTS): o.hide_render = True
    bpy.context.view_layer.update(); SRC0 = root.matrix_world.copy()
    place(root, cen(ob['polySurface50_phong1_0']), Vector((1, 0, 0)), Vector((0, 0, 1)), 0.01 * K, gun, trig, fwd, up)
    zat, g10 = ob['zatvor'], ob['group10']; knob = ob['polySurface39_phong1_0']
    def set_bolt(u, v): zat.location.x = 10.38 * (1 - v); g10.rotation_euler.x = math.radians(90 * (1 - u))
    def key_bolt(f): zat.keyframe_insert('location', frame=f); g10.keyframe_insert('rotation_euler', frame=f)
    CM = 1.0   # source units per cm
    rnd = ob['polySurface59_phong1_0']   # a magazine cartridge: the single round's mesh
    FWD_S, UP_S, RIGHT_S = Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0))
    PORT = Vector((6.84, 0.0, 0.2))   # the top of the magazine stack under the open bolt (source cm)
else:
    new, root = import_glb('E:/nmk/incoming_realistic_m1_garand/realistic_m1_garand.glb')
    sc.render.fps = 30
    parts = {n: ob[n] for n in ('Charging_Handle', 'Bolt', 'Recieve', 'Clip', 'Bullet')}
    for tt in range(0, 201):   # GR9: to 200: the file's 24 fps take lands at x1.25 in this 30 fps scene (its bolt is home at 190, not 152)   # the file's own take (24 fps after import): the parts' local poses per take frame
        sc.frame_set(tt); bpy.context.view_layer.update(); TAKE[tt] = {n: o.matrix_basis.copy() for n, o in parts.items()}
    for o in new:
        if o.animation_data: o.animation_data_clear()
        if o.name.startswith('Casing') or o.name.startswith('ClipEmpty'): o.hide_render = True
    bpy.context.view_layer.update(); SRC0 = root.matrix_world.copy()
    body = ob['Garand001_03 - Default_0']; V = [body.matrix_world @ v.co for v in body.data.vertices]
    clipc = cen(ob['Clip_02 - Default_0'])
    near = [v for v in V if abs(v.y - (clipc.y - 0.06)) < 0.05]; zmin = min(v.z for v in near)
    gtrig = Vector((clipc.x, clipc.y - 0.07, zmin + 0.035))   # the trigger: ~7 cm behind the clip, over the guard's bottom
    SG = 1.107 / 1.26   # metres per source unit (the game's own scale for this file: realLength over Garand001)
    if SCOPED:   # the Kar98k file's scope, offset left over the receiver as on the M1C
        kn, kroot = import_glb('E:/nmk/incoming_kar98k/kar98k.glb')
        for o in kn:
            if o.animation_data: o.animation_data_clear()
        sp = [o for o in kn if o.type == 'MESH' and o.name.split('_phong')[0] in SCOPE_PARTS[:12]]
        for o in kn:
            if o.type == 'MESH' and o not in sp: o.hide_render = True
        bpy.context.view_layer.update(); kc = sum((cen(o) for o in sp), Vector()) / len(sp)
        Rk = Matrix.Rotation(math.radians(90), 4, 'Z')   # Kar +x (muzzle) -> Garand +y
        boltc = cen(ob['Bolt_02 - Default_0'])
        at = Vector((clipc.x - 0.032 / SG, clipc.y + 0.06 / SG, boltc.z + 0.062 / SG))
        kroot.matrix_world = Matrix.Translation(at) @ Rk @ Matrix.Scale(0.01 / SG, 4) @ Matrix.Translation(-kc) @ kroot.matrix_world
        bpy.context.view_layer.update(); mw = kroot.matrix_world.copy(); kroot.parent = root; kroot.matrix_world = mw
    for n_, o_ in parts.items(): o_.matrix_basis = TAKE[128][n_]
    bpy.context.view_layer.update()
    _seat = cen(ob['Clip_02 - Default_0']); TF0 = 128
    for tt in range(48, 129):   # RL2: the first take frame with the clip within 4 cm of its seat: the hand carries it there, the take does the push
        parts['Clip'].matrix_basis = TAKE[tt]['Clip']; bpy.context.view_layer.update()
        if (cen(ob['Clip_02 - Default_0']) - _seat).length < 0.04 / SG: TF0 = tt; break
    parts['Clip'].matrix_basis = TAKE[128]['Clip']; bpy.context.view_layer.update(); print('RL2 Garand clip hand-over at take frame', TF0)
    PORT_S = cen(ob['Clip_02 - Default_0']) + Vector((0, 0, 1)) * (2.5 * 0.01 / SG)   # source coords: over the seated clip's top
    BUL_ON_CLIP = parts['Clip'].matrix_world.inverted() @ parts['Bullet'].matrix_world   # the take turns the Bullet on its own: kept on the clip
    FEED_SM = ob['Bullet_02 - Default_0'].matrix_world.copy(); FEED_S = cen(ob['Bullet_02 - Default_0'])   # RL4: a loaded round's spot: the seated clip's top round
    GCLIP_SM = ob['Clip_02 - Default_0'].matrix_world.copy(); GCLIP_C = cen(ob['Clip_02 - Default_0'])   # RL5: the static clip (the user: the clip's static model, not the take's): its seated pose
    GCLIP_TOP = max(max((o_.matrix_world @ v_.co).z for v_ in o_.data.vertices) for o_ in (ob['Clip_02 - Default_0'], ob['Bullet_02 - Default_0']))   # its top with the rounds (source up = z)
    # RL6 (the user: the clip stood proud and a round sat apart): the take's clip at 128 is one clip height above the file's seat
    # (its top round, FEED_SM, is at the real seat). The seat is set by the receiver's rim at the opening: the clip's top 1 mm under it
    _C = [ob['Clip_02 - Default_0'].matrix_world @ v_.co for v_ in ob['Clip_02 - Default_0'].data.vertices]
    _x0, _x1 = min(p_.x for p_ in _C), max(p_.x for p_ in _C); _y0, _y1 = min(p_.y for p_ in _C), max(p_.y for p_ in _C)
    _G = ob['Garand001_03 - Default_0']; _rim = {}
    for v_ in _G.data.vertices:
        p_ = _G.matrix_world @ v_.co
        if _y0 < p_.y < _y1 and (_x0 - 0.004 < p_.x < _x0 or _x1 < p_.x < _x1 + 0.004): s_ = 'l' if p_.x < _x0 else 'r'; _rim[s_] = max(_rim.get(s_, -1e9), p_.z)
    RIM_TOP = min(_rim.values())
    GSEAT_DROP = max(0.0, GCLIP_TOP - (RIM_TOP - 0.1 * 0.01 / SG))
    GSEAT_CM = GSEAT_DROP / (0.01 / SG)
    GCLIP_SM = Matrix.Translation((0, 0, -GSEAT_DROP)) @ GCLIP_SM; GCLIP_C = GCLIP_C - Vector((0, 0, GSEAT_DROP)); GCLIP_TOP -= GSEAT_DROP
    print('RL6 Garand seat: rim at the opening %s, the clip lowered %.2f cm onto it (its top %.2f cm under the lower rim)' % ({k: round(v, 4) for k, v in _rim.items()}, GSEAT_DROP * SG * 100, (RIM_TOP - GCLIP_TOP) * SG * 100))
    place(root, gtrig, Vector((0, 1, 0)), Vector((0, 0, 1)), SG * K, gun, trig, fwd, up)
    def set_take(tt, clip_from=None):
        tt = max(0, min(200, int(round(tt))))
        for n, o in parts.items():
            if n == 'Bullet': continue
            o.matrix_basis = TAKE[clip_from][n] if (n == 'Clip' and clip_from is not None) else TAKE[tt][n]
        bpy.context.view_layer.update(); parts['Bullet'].matrix_world = parts['Clip'].matrix_world @ BUL_ON_CLIP
    def key_take(f):
        for o in parts.values(): o.keyframe_insert('location', frame=f); o.keyframe_insert('rotation_quaternion' if o.rotation_mode == 'QUATERNION' else 'rotation_euler', frame=f)
    knob = ob['Charging_Handle_02 - Default_0']
    CM = 0.01 / SG
    rnd = ob['Bullet_02 - Default_0']
    FWD_S, UP_S, RIGHT_S = Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0))
    PORT = PORT_S
SRCI = SRC0.inverted()
def src2w(p): return root.matrix_world @ SRCI @ p   # a point in the rifle's source coordinates -> world, now
def dir2w(d): return (root.matrix_world @ SRCI).to_3x3() @ d
# ---- props: single rounds (copies of the magazine round), the stripper clip (Kar98k), the Garand's own clip
def prop_copy(o, name):
    c = o.copy(); c.data = o.data.copy(); c.name = name; sc.collection.objects.link(c); c.parent = None; c.rotation_mode = 'XYZ'; c.matrix_world = o.matrix_world.copy();   # RL5: Euler (the keys are rotation_euler; a glTF copy came quaternion: its rotation never animated)
    c.hide_render = True; return c
ROUNDS = [prop_copy(rnd, 'round%d' % i) for i in range(5)]
if not KAR:   # RL5: a static copy of the en-bloc clip (its rounds are in its mesh) and of its top round, moved by the hand only
    GCLIP = prop_copy(ob['Clip_02 - Default_0'], 'gclip_static'); GBUL = prop_copy(ob['Bullet_02 - Default_0'], 'gbul_static')
    TOPOFF_DROP = 2.0   # cm: a loose round (no clip) lies this much lower than a full clip's top round: on the follower (the user: it floated 2 cm high)
    def gclip_seat(): return root.matrix_world @ SRCI @ GCLIP_SM
H_SET = 4.0   # cm over its seat where the hand sets the Garand clip on the receiver
def gdepth(i): return H_SET if i <= 22 else H_SET * (1 - ss((i - 22) / 8.0)) if i < 30 else 0.0   # RL5: the thumb's push 22-30, the clip with it
if KAR:   # RL4: the Kar98k's own magazine cartridge (the top round as the model has it) is where every pressed round lands
    _G2S = SRC0 @ root.matrix_world.inverted(); FEED_SM = _G2S @ rnd.matrix_world; FEED_S = _G2S @ cen(rnd)
def feed_world(): return root.matrix_world @ SRCI @ FEED_SM
CURF = 0; HIDE_RND = set()   # RL4: frames the Kar98k's own cartridge is hidden (empty magazine, or a pressed round lies in its place)
RND_M = {r: rnd.matrix_world.copy() for r in ROUNDS}
PROPS = list(ROUNDS) + ([GCLIP, GBUL] if not KAR else [])
if KAR and not SCOPED:
    # RL2: the user's clip (Echonium's "Clip of 7.62x54R", CC-BY-4.0; tools/clip762_prep.py: the clip / the rounds, cm, the top round's middle at 0)
    with bpy.data.libraries.load('E:/nmk/boltguns/clip762_split.blend') as (src, dst): dst.objects = [n for n in src.objects]
    STRIP, CASES, TIPS = (next(o for o in dst.objects if o.name == m) for m in ('STRIP', 'CASES', 'TIPS'))
    CLIPMETA = json.load(open('E:/nmk/boltguns/clip762_split.json'))
    for o in (STRIP, CASES, TIPS): sc.collection.objects.link(o); o.hide_render = True
    PROPS += [STRIP, CASES, TIPS]
# ---- the timeline
T = {}   # frame -> dict: sf (source frame), tilt, w (right hand on the contact path), c (contact, source coords or 'knob'),
         # bolt (u, v) or take, props [(obj, world matrix or None)]
def lerp(a, b, t): return a + (b - a) * t
def ss(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)
class Seg:
    def __init__(s, start): s.f0 = start; s.keys = []   # (frame, contact, w, extra)
def path_value(keys, f):   # keyed values with smoothstep between: keys [(frame, value)]
    if f <= keys[0][0]: return keys[0][1]
    for (fa, a), (fb, b) in zip(keys, keys[1:]):
        if fa <= f <= fb: return lerp(a, b, ss((f - fa) / max(1e-6, fb - fa))) if not isinstance(a, str) and not isinstance(b, str) else (a if f < fb else b)
    return keys[-1][1]
POUCH = None
frames = []
def add_cycle(f0):   # the M24 fire clip 1-49 with the Kar98k bolt
    for i in range(49):
        sf = i + 1; f = f0 + i
        T[f] = dict(sf=sf, tilt=0.0, cycle=True, bolt=(REC[sf]['_lift'], REC[sf]['_slide']), props=[])
    return f0 + 49
def hold(f0, n, bolt):
    for i in range(n): T[f0 + i] = dict(sf=149, tilt=0.0, w=0.0, bolt=bolt, props=[])
    return f0 + n
def reload_seg(f0, plan, length):
    # plan: dict of keyed tracks over 0..length: tilt, w (hand on the contact path), contact (source point or 'knob'),
    # bolt u / v (Kar) or take (Garand), and prop functions
    for i in range(length + 1):
        f = f0 + i; d = dict(sf=149, tilt=path_value(plan['tilt'], i), w=path_value(plan['w'], i), cpath=plan['contact'], ci=i, props=[])
        if KAR: d['bolt'] = (path_value(plan['u'], i), path_value(plan['v'], i))
        else: d['take'] = path_value(plan['take'], i); d['clip_from'] = plan.get('clip_from', lambda i: None)(i)
        d['propfn'] = plan.get('props'); d['jolt'] = path_value(plan.get('jolt', [(0, 0.0)]), i); T[f] = d
    return f0 + length + 1
# contact helpers (source coordinates)
def knob_c(): return 'knob'
def above(p, h): return p + UP_S * (h * CM)
POUCH_S = None
def pouch():   # a belt pouch: below and right of the grip, out of the frame
    return 'pouch'
# ---- plans
def kar_single(n, open_first=True, empty=False):
    # RL2: each round taken from the pouch pinched between thumb and index, brought over the open action, laid in front of the
    # stack and pressed down with the thumb; the round in the fingers the whole way (from the pouch, out of frame)
    c = [(0, 'grip'), (8, 'knob'), (20, 'knob')]; w = [(0, 0.0), (8, 1.0)]; u = [(0, 0.0), (8, 0.0), (14, 1.0)]; v = [(0, 0.0), (14, 0.0), (20, 1.0)]
    ev = []; marks = {}; jolt = [(0, 0.0)]
    t = 20
    for k in range(n):
        c += [(t + 7, 'pouch'), (t + 13, ('above', k)), (t + 17, ('in', k)), (t + 19, ('press', k)), (t + 22, ('press', k))]; ev.append((k, t + 6, t + 19)); jolt += [(t + 17, 0.0), (t + 19, 0.55), (t + 22, 0.0)]
        if k == 0: marks.update({'round in fingers': t + 12, 'round at the action': t + 17, 'pushed into the action': t + 20})
        t += 22
    c += [(t + 6, 'knob'), (t + 18, 'knob'), (t + 26, 'grip')]; marks['hand clear'] = t + 4; marks['round chambered by the bolt'] = t + 10
    u += [(t + 12, 1.0), (t + 18, 0.0)]; v += [(t + 6, 1.0), (t + 12, 0.0)]; w += [(t + 18, 1.0), (t + 26, 0.0)]
    tilt = [(0, 0.0), (10, 1.0), (t + 20, 1.0), (t + 30, 0.0)]
    def props(i, Pc, Rh):   # RL4: each round pushed by the fingers into the loaded spot, replacing the one there (the model's own, then the last pressed); the last chambered by the bolt
        out = []; fr_last = 1.0 - path_value(v, i)
        if (empty and fr_last < 0.5) or (ev[0][2] <= i and fr_last < 0.5): HIDE_RND.add(CURF)
        for k, ta, tb in ev:
            r = ROUNDS[k]; nxt = ev[k + 1][2] if k + 1 < len(ev) else None
            if ta <= i < tb: out.append((r, carried(Pc, Rh)))
            elif i >= tb and (nxt is None or i < nxt):
                fr = fr_last if nxt is None else 0.0
                out.append((r, chambered(fr) if fr < 0.92 else None))
            elif nxt is not None and i == nxt: out.append((r, None))
        return out
    return dict(tilt=tilt, w=w, contact=c, u=u, v=v, props=props, carry='pinch', marks=marks, jolt=jolt), t + 30
def kar_stripper():
    # RL2: the clip already in the right hand in a C grip (fingers over the rounds, thumb behind) when it comes into view, set in
    # the guide, then one firm thumb push down; the empty clip stands in the guide until the closing bolt knocks it out
    c = [(0, 'grip'), (8, 'knob'), (20, 'knob'), (28, 'pouch'), (38, 'clip_above'), (44, 'clip_seat'), (47, 'thumb_seat'), (55, 'clip_down'), (59, 'clip_down'), (64, 'knob'), (74, 'knob'), (84, 'grip')]
    w = [(0, 0.0), (8, 1.0), (74, 1.0), (84, 0.0)]; u = [(0, 0.0), (8, 0.0), (14, 1.0), (68, 1.0), (74, 0.0)]; v = [(0, 0.0), (14, 0.0), (20, 1.0), (62, 1.0), (68, 0.0)]
    tilt = [(0, 0.0), (10, 1.0), (76, 1.0), (88, 0.0)]
    def props(i, Pc, Rh):
        seat = clip_seat_world(); out = []
        if 1.0 - path_value(v, i) < 0.5: HIDE_RND.add(CURF)   # RL4: the magazine is empty until the clip's rounds are in (the model's own round hidden)
        if 27 <= i < 44: M = carried(Pc, Rh, clip=True); out += [(STRIP, M), (CASES, M), (TIPS, M)]
        elif 44 <= i < 56:
            k = ss((i - 47) / 8.0) if i >= 47 else 0.0; down = Matrix.Translation(dir2w(-UP_S * DROP_CM * CM) * k)   # RL4: the thumb pushes the rounds the whole way (47-55), the contact path's own easing
            if i < 56: out += [(STRIP, seat), (CASES, down @ seat), (TIPS, down @ seat)]
            else: out += [(STRIP, seat), (CASES, None), (TIPS, None)]
        elif 56 <= i < 66: out.append((STRIP, seat))
        if 56 <= i < 74:   # RL4: the top round in its loaded spot, then the closing bolt pushes it into the chamber
            fr = 1.0 - path_value(v, i); out.append((ROUNDS[0], chambered(fr) if fr < 0.92 else None))
        elif 66 <= i < 74:   # the bolt closing knocks the empty clip out: up and to the right, turning
            k = (i - 66) / 8.0; sp = src2w(Vector()) ; M = Matrix.Translation(dir2w(UP_S * 9 + RIGHT_S * 7) * k * (0.01 * K / root.matrix_world.to_scale()[0]) * root.matrix_world.to_scale()[0]) @ seat @ Matrix.Rotation(k * 2.5, 4, 'Y'); out.append((STRIP, M))
        elif i == 74: out.append((STRIP, None))
        return out
    return dict(tilt=tilt, w=w, contact=c, u=u, v=v, props=props, carry='cgrip', jolt=[(0, 0.0), (47, 0.0), (55, 0.8), (58, 0.1), (61, 0.0)],
                marks={'clip in hand (C grip)': 37, 'clip set in the guide': 45, 'thumb pushing': 51, 'rounds seated, thumb on them': 56, 'round chambered by the bolt': 66, 'hand clear, clip knocked out': 70}), 88
def gar_topoff(n):
    # RL2: no clip anywhere: the op rod back, the action open and empty-looking, loose rounds pinched and pressed in one at a time
    c = [(0, 'grip'), (8, 'knob'), (20, 'knob')]; w = [(0, 0.0), (8, 1.0)]; take = [(0, 0.0), (8, 0.0), (20, 32.0)]
    ev = []; t = 20; marks = {}; jolt = [(0, 0.0)]
    for k in range(n):
        c += [(t + 7, 'pouch'), (t + 13, ('above', k)), (t + 17, ('in', k)), (t + 19, ('press', k)), (t + 22, ('press', k))]; ev.append((k, t + 6, t + 19)); jolt += [(t + 17, 0.0), (t + 19, 0.55), (t + 22, 0.0)]
        if k == 0: marks.update({'round in fingers': t + 12, 'round at the action': t + 17, 'thumb pressing': t + 19})
        t += 22
    c += [(t + 6, 'knob'), (t + 9, 'knob'), (t + 14, 'clear'), (t + 24, 'grip')]; marks['hand clear'] = t + 16; marks['round chambered by the bolt'] = t + 13
    take += [(t + 9, 32.0), (t + 14, 152.0)]; w += [(t + 9, 1.0), (t + 24, 0.0)]
    tilt = [(0, 0.0), (10, 1.0), (t + 16, 1.0), (t + 28, 0.0)]
    def props(i, Pc, Rh):   # RL3: as the Kar98k's: on the stack until the next round, the last chambered as the bolt runs home (take 136-152)
        out = []
        for k, ta, tb in ev:
            r = ROUNDS[k]; nxt = ev[k + 1][2] if k + 1 < len(ev) else None
            if ta <= i < tb: out.append((r, carried(Pc, Rh)))
            elif i >= tb and (nxt is None or i < nxt):
                fr = min(1.0, max(0.0, (path_value(take, i) - 136.0) / 16.0)) if nxt is None else 0.0
                out.append((r, chambered(fr, TOPOFF_DROP) if fr < 0.92 else None))
            elif nxt is not None and i == nxt: out.append((r, None))
        return out
    return dict(tilt=tilt, w=w, contact=c, take=take, props=props, carry='pinch', marks=marks, noclip=True, jolt=jolt), t + 28
def gar_enbloc():
    # RL2: the en-bloc clip in the right hand in a C grip, set on the receiver, thumbed down along the take's own insertion path;
    # the hand snaps clear as the bolt runs forward
    # GR9 (the user): after the thumb seats the clip the right hand goes to the operating-rod handle and pushes it home (the
    # bolt no longer runs forward on its own), then clears to the grip
    c = [(0, 'grip'), (10, 'pouch'), (20, 'gclip_hold'), (22, 'gclip_thumb'), (30, 'gclip_thumb'), (33, 'gclip_thumb'), (38, 'knob'), (41, 'knob'), (48, 'knob'), (52, 'clear'), (62, 'grip')]
    w = [(0, 0.0), (6, 1.0), (52, 1.0), (62, 0.0)]
    take = [(0, 40.0), (30, 40.0), (31, 128.0), (39, 128.0), (40, 165.0), (41, 165.0), (48, 190.0)]   # the rod held back until the hand is on it (the take's release, 165), then pushed home over 7 frames (190: the take's bolt closed)   # RL5: the take only for the bolt / op rod (its clip hidden): open while the thumb seats the static clip, then forward as the hand clears
    tilt = [(0, 0.0), (10, 1.0), (54, 1.0), (66, 0.0)]
    def props(i, Pc, Rh):   # RL5: the static clip in the C grip (10-20), then on the well at gdepth(i) under the thumb; its top round chambered as the bolt runs home
        S0 = gclip_seat(); out = []
        if i < 10: return out
        if i < 20:
            cw_ = (root.matrix_world @ SRCI) @ (GCLIP_C + UP_S * 1.6 * CM); M = Matrix.Translation(Pc - cw_) @ S0
        else: M = Matrix.Translation(dir2w(UP_S * gdepth(i) * CM)) @ S0
        out.append((GCLIP, M))
        fr = min(1.0, max(0.0, (path_value(take, i) - 136.0) / 16.0))
        gfeed = Matrix.Translation(dir2w(UP_S * -GSEAT_CM * CM)) @ feed_world()   # RL6: the top round in the seated clip (the take's round sits on its high clip)
        out.append((GBUL, M @ S0.inverted() @ gfeed))   # GR9 (the user): the top round stays seated on the clip (no slide into the chamber)
        return out
    def clip_from(i): return None
    return dict(tilt=tilt, w=w, contact=c, take=take, props=props, clip_from=clip_from, carry='cgrip', marks={'clip in hand (C grip)': 17, 'clip at the opening': 21, 'half way, thumb pushing': 26, 'fully seated, thumb still on it': 31, 'hand on the op rod': 41, 'op rod pushed home': 48, 'hand clear': 52}, jolt=[(0, 0.0), (22, 0.0), (30, 0.8), (33, 0.1), (36, 0.0)]), 72   # (to 72: the seated clip stays shown through the end)
# ---- per-frame contact resolution
RND_BACK = 2.6   # cm: the pinch this far behind a round's middle (near the case head)
REST_S = None
def chambered(fr, drop=0.0):   # RL4: a round from its loaded spot (fr 0: the model's own top round; drop cm lower: the Garand top-off) forward into the chamber with the bolt face (fr 1)
    return Matrix.Translation(dir2w((FWD_S * 6.5 + UP_S * (0.3 + drop)) * CM) * ss(fr) - dir2w(UP_S * drop * CM)) @ feed_world()
SIDE = 1.0 if (KAR and SCOPED) else 0.0   # RL3: under the scope the rounds come in from the right side, canted
HOLD = {}   # carried prop offsets (hand space), set on the first carried frame
def carried(Pc, Rh, clip=False):
    key = 'clip' if clip else 'round'
    if key not in HOLD:
        if clip: HOLD[key] = Matrix.Translation((0, 0, 0))
        else: HOLD[key] = Matrix.Translation((0, 0, 0))
    o = Rh.to_4x4(); o.translation = Pc
    # the prop lies along the rifle (rounds forward), at the contact point
    R = (root.matrix_world @ SRCI).to_3x3().normalized()
    M = Matrix.Translation(Pc + (dir2w(UP_S * 1.6 * CM) if clip else dir2w(FWD_S * RND_BACK * CM))) @ R.to_4x4() @ (Matrix.Rotation(math.radians(-40 * SIDE), 4, FWD_S) if not clip else Matrix())   # a round pinched near its base: the bullet out ahead of the fingers   # RL2: a clip's top round 1.6 cm over the C grip's middle
    return M @ prop_local(clip)
def prop_local(clip):   # the prop's own frame -> source-aligned frame centred on its gripping point
    if clip: return CLIP_L
    return RND_L
def clip_seat_world():
    return Matrix.Translation(src2w(CLIP_SEAT_S)) @ (root.matrix_world @ SRCI).to_3x3().normalized().to_4x4() @ CLIP_L
# the round prop: centred on its middle, axis along the rifle's forward (the magazine cartridge already is)
c0 = cen(rnd); RND_L = (root.matrix_world @ SRCI).to_3x3().normalized().to_4x4().inverted() @ Matrix.Translation(-c0) @ rnd.matrix_world
RND_L = Matrix.Scale(1.0, 4) @ RND_L
if KAR and not SCOPED:
    # the stripper clip: rounds along its +x, stacked on +z; real size (its 14.2 units = the 7.9 cm cartridge); its rims at the
    # guide, the lowest round 1.2 cm over the stack top; held by the top round's middle
    s_clip = 7.9 / 14.2 * root.matrix_world.to_scale()[0] / root.matrix_world.to_scale()[0]
    sw = (root.matrix_world @ SRCI).to_scale()[0]   # world per source unit
    CLIP_L = Matrix.Scale(sw / CM, 4)   # the prop is in cm, the top round's middle at its origin
    CLIP_SEAT_S = Vector((FEED_S.x, 0.0, FEED_S.z + 0.9 + CLIPMETA['stack_h_cm']))   # RL4: seated over the magazine: the stack's bottom just over the loaded spot
    DROP_CM = CLIP_SEAT_S.z - FEED_S.z   # the thumb takes the top round down to the loaded spot
    CLIP_DOWN_S = CLIP_SEAT_S - UP_S * DROP_CM
else:
    CLIP_L = Matrix(); CLIP_SEAT_S = PORT; CLIP_DOWN_S = PORT
def contact_world(spec, f, d):
    if spec == 'knob': return cen(knob)
    if spec == 'pouch': return src2w(Vector(KNOB_REST_S) + (-FWD_S * 18 - RIGHT_S * -22 - UP_S * 30) * CM) if False else src2w(GRIP_S + (RIGHT_S * 24 - UP_S * 32 - FWD_S * 8) * CM)
    if spec == 'clear': return cen(knob) + dir2w(RIGHT_S * 5 * CM + UP_S * 4 * CM)
    if spec == 'clip_above':
        if KAR: return src2w(CLIP_SEAT_S + UP_S * 6 * CM + RIGHT_S * 3 * CM)
        return GAR_CLIP48_W()   # the take's first insertion pose: the hand hands the clip over where the take takes it
    if spec == 'clip_seat': return src2w(CLIP_SEAT_S)
    if spec == 'thumb_seat': return src2w(CLIP_SEAT_S - FWD_S * 2.5 * CM + UP_S * 0.6 * CM)   # the thumb flat on the top round near its base
    if spec == 'clip_down': return src2w(CLIP_DOWN_S - FWD_S * 2.5 * CM + UP_S * 0.6 * CM)
    if spec == 'gclip_hold': return (root.matrix_world @ SRCI) @ (GCLIP_C + UP_S * (H_SET + 1.6) * CM)   # RL5: the C grip's middle with the clip H_SET over its seat
    if spec == 'gclip_thumb': return (root.matrix_world @ SRCI) @ Vector((GCLIP_C.x, GCLIP_C.y, GCLIP_TOP + (gdepth(d.get('ci', 0)) + 0.2) * CM))   # the thumb tip on the clip's top at the push's depth
    if spec == 'clip_top':   # RL4: the clip's real top (its highest point with its rounds, along the rifle's up) + 2 mm: the thumb on it
        uw = dir2w(UP_S).normalized(); c0 = cen(ob['Clip_02 - Default_0']); top = c0.dot(uw)
        for o_ in (ob['Clip_02 - Default_0'], rnd):
            M_ = o_.matrix_world; top = max(top, max((M_ @ v_.co).dot(uw) for v_ in o_.data.vertices))
        return c0 + uw * (top - c0.dot(uw)) + dir2w(UP_S * 0.2 * CM)
    if isinstance(spec, tuple):
        tag, k = spec
        side = {'above': 5.0, 'in': 1.5, 'press': 0.0}[tag] * SIDE   # RL3: the scoped Kar98k: in from the right side under the scope
        drop = 0.0 if KAR else TOPOFF_DROP   # RL5: the Garand top-off's round all the way down onto the follower
        return src2w(FEED_S - FWD_S * (RND_BACK * CM) + RIGHT_S * (side * CM) + UP_S * (({'above': 6.0, 'in': 2.2, 'press': 0.0}[tag] - (3.0 if tag == 'above' else 0.0) * SIDE - drop) * CM))   # RL4: the fingers take the round down into its loaded spot (the pinch behind its middle)
    return None
def GAR_CLIP48_W():   # the Garand clip's top (+1.6 cm) at take frame 48, on the current gun pose (the clip's own pose restored)
    o = parts['Clip']; m0 = o.matrix_basis.copy(); o.matrix_basis = TAKE[TF0]['Clip']; bpy.context.view_layer.update()
    p = cen(ob['Clip_02 - Default_0']) + dir2w(UP_S * 1.6 * CM); o.matrix_basis = m0; bpy.context.view_layer.update(); return p
GRIP_S = None
MARKS = []   # (case name, {label: absolute frame})
def build(plans):
    f = 1; segs = []
    if KAR:
        f = add_cycle(f); segs.append(('bolt cycle', 1, f - 1)); f = hold(f, 12, (0, 0))
    for name, (plan, L) in plans:
        s = f; f = reload_seg(f, plan, L); segs.append((name, s, f - 1)); f = hold(f, 14, (0, 0) if KAR else None)
        MARKS.append((name, {k: s + v for k, v in plan.get('marks', {}).items()}))
        for ff in range(s, f): T[ff]['_carry'] = plan.get('carry', 'pinch'); T[ff]['_noclip'] = plan.get('noclip', False)
    return segs, f - 1
if KAR:
    plans = [('reload, not empty (2 rounds)', kar_single(2))]
    plans.append(('reload, empty (5 single rounds: the scope blocks the clip guide)', kar_single(5, empty=True)) if SCOPED else ('reload, empty (stripper clip)', kar_stripper()))
else:
    plans = [('reload, not empty (op rod back, 2 rounds topped up)', gar_topoff(2)), ('reload, empty (en-bloc clip)', gar_enbloc())]
segs, LAST = build(plans)
if KAR:   # KR2 (the user: "integrate the ads into this blend"): aimed: in, held, the shot and the bolt cycle on the sights, out
    def ads_seg(f0):
        f = hold(f0, 6, (0, 0)); s = f
        for i in range(8): T[f + i] = dict(sf=149, tilt=0.0, w=0.0, bolt=(0, 0), props=[], ads=ss((i + 1) / 8.0))
        f += 8
        for i in range(15): T[f + i] = dict(sf=149, tilt=0.0, w=0.0, bolt=(0, 0), props=[], ads=1.0)
        f += 15; c0 = f; f = add_cycle(f)
        for ff in range(c0, f):   # the user's clip: on the sights for the shot, the rifle off the eye while the bolt is worked (the knob and hand
            sf_ = ff - c0 + 1      # would come within 3 cm of the eye), back on the sights at the end
            T[ff]['ads'] = 1.0 - 0.75 * (ss((sf_ - 12) / 8.0) if sf_ < 30 else 1.0 - ss((sf_ - 38) / 9.0))
        for i in range(12): T[f + i] = dict(sf=149, tilt=0.0, w=0.0, bolt=(0, 0), props=[], ads=1.0)
        f += 12
        for i in range(8): T[f + i] = dict(sf=149, tilt=0.0, w=0.0, bolt=(0, 0), props=[], ads=1.0 - ss((i + 1) / 8.0))
        f += 8; f = hold(f, 6, (0, 0)); return s, c0, f
    _s, _c0, _f = ads_seg(LAST + 1); segs.append(('aimed (ADS): aim in, the shot and the bolt cycle on the sights, aim out', _s, _f - 1)); LAST = _f - 1
    MARKS.append(('aimed (ADS)', {'aimed': _s + 14, 'shot': _c0 + 2, 'bolt back': _c0 + 30, 'aimed again': _c0 + 52}))
    ADS_SEG = (_s, _c0, _f - 1)
print('BG4 segments', segs, 'last', LAST)
# ---- RL2: the right hand's poses for rounds and clips, solved on the M24's knob-grip hand (frame 29): per finger its flex axis
# (the local axis that brings the tip toward the wrist), then a PINCH (thumb and index tips one cartridge apart: 1.5 cm, the
# other fingers tucked) and a C GRIP (index..pink curled over a clip's rounds, the thumb behind: tips 4.2 cm apart). Offsets
# from the wrist (its own rotation) to the pinch point, the C grip's middle and the thumb tip (the press).
RF = {'thumb': ['R_thumb1', 'R_thumb2', 'R_thumb3'], 'point': ['R_point1', 'R_point2', 'R_point3'], 'middle': ['R_middle1', 'R_middle2', 'R_middle3'], 'ring': ['R_ring1', 'R_ring2', 'R_ring3'], 'pink': ['R_pink1', 'R_pink2', 'R_pink3']}
RFB = [b for v in RF.values() for b in v]
def set_basis(P):
    for b, M in P.items(): arm.pose.bones[b].matrix_basis = M
    bpy.context.view_layer.update()
AMESH = ob['armmesh']; _gi = {g.name: g.index for g in AMESH.vertex_groups}
def _grp(name): gi = _gi.get(name); return [v.index for v in AMESH.data.vertices if v.groups and max(v.groups, key=lambda x: x.weight).group == gi]
TIPV = {fn: _grp(RF[fn][2]) for fn in RF}; PALMV = _grp('R_palm') or _grp('R_wrist')
def _meshco():
    dg = bpy.context.evaluated_depsgraph_get(); e = AMESH.evaluated_get(dg); me = e.to_mesh(); M = AMESH.matrix_world
    return M, me, e
def tipw(fn):   # the glove's own fingertip: the last segment's vertex furthest from that segment's head (bone tails sit inside the glove)
    M, me, e = _meshco(); h = arm.matrix_world @ arm.pose.bones[RF[fn][2]].head
    best = max((M @ me.vertices[i].co for i in TIPV[fn]), key=lambda p: (p - h).length); e.to_mesh_clear(); return best
def palmw():
    M, me, e = _meshco(); c = sum((M @ me.vertices[i].co for i in PALMV), Vector()) / max(1, len(PALMV)); e.to_mesh_clear(); return c
def wristM(): M = arm.matrix_world @ arm.pose.bones['R_wrist'].matrix; return M.translation.copy(), M.to_3x3().normalized()
BASE29 = {b: REC[29][b].copy() for b in RFB}
def curled(P, fn, ax, ang): Q = dict(P); [Q.__setitem__(b, P[b] @ Matrix.Rotation(ang, 4, ax)) for b in RF[fn]]; return Q
set_basis(BASE29)
FLEX = {}
for fn in RF:
    ref = arm.matrix_world @ (arm.pose.bones['R_point1'].head if fn == 'thumb' else arm.pose.bones['R_wrist'].head)
    d0 = (tipw(fn) - ref).length; best = None
    for ax in (Vector((1, 0, 0)), Vector((-1, 0, 0)), Vector((0, 0, 1)), Vector((0, 0, -1))):
        set_basis(curled(BASE29, fn, ax, math.radians(12))); dd = (tipw(fn) - ref).length
        if best is None or dd < best[1]: best = (ax, dd)
    FLEX[fn] = best[0]; set_basis(BASE29)
def solve(target_cm, others_deg, fingers):
    best = None
    for ai in range(-40, 81, 10):
        for at in range(-40, 81, 10):
            P = dict(BASE29)
            for fn in fingers: P = curled(P, fn, FLEX[fn], math.radians(ai))
            P = curled(P, 'thumb', FLEX['thumb'], math.radians(at))
            for fn in ('middle', 'ring', 'pink'):
                if fn not in fingers: P = curled(P, fn, FLEX[fn], math.radians(others_deg))
            set_basis(P); tt, ti = tipw('thumb'), tipw('point'); dd = (tt - ti).length / K * 100
            out = ((tt + ti) / 2 - palmw()).length / K * 100   # the pinch out in front of the palm, not in the fist
            cost = abs(dd - target_cm) + 0.01 * (abs(ai) + abs(at)) / 5 - 0.15 * min(out, 9.0)
            if best is None or cost < best[0]: best = (cost, ai, at, P, dd)
    return best
def offsets(P):
    set_basis(P); wp, wr = wristM(); Ri = wr.inverted()
    mid = (tipw('thumb') + tipw('point')) / 2; cg = (tipw('thumb') + (tipw('point') + tipw('middle')) / 2) / 2
    return Ri @ (mid - wp), Ri @ (cg - wp), Ri @ (tipw('thumb') - wp)
bp = solve(1.5, 40, ['point']); POSE_PINCH = bp[3]; O_PINCH = offsets(POSE_PINCH)[0]
bc = solve(4.2, 0, ['point', 'middle', 'ring', 'pink']); POSE_C = bc[3]; O_C, O_THUMB = offsets(POSE_C)[1], offsets(POSE_C)[2]
print('RL2 pinch: index %d deg, thumb %d deg, tips %.1f cm; C grip: fingers %d deg, thumb %d deg, tips %.1f cm' % (bp[1], bp[2], bp[4], bc[1], bc[2], bc[4]))
def O_PINCH_W(Rk): return O_PINCH
def O_C_W(Rk): return O_C
def O_TH_W(Rk): return O_THUMB
PCF = {}
POSES = {'grip': {b: REC[149][b] for b in RFB}, 'knob': BASE29, 'pinch': POSE_PINCH, 'cgrip': POSE_C, 'thumb': POSE_C}
def mode_of(spec, carry):
    if spec == 'grip': return 'grip'
    if spec in ('knob', 'clear'): return 'knob'
    if spec == 'pouch': return carry
    if isinstance(spec, tuple): return 'pinch'
    if spec in ('clip_above', 'clip_seat', 'gclip_hold'): return 'cgrip'
    return 'thumb'   # thumb_seat, clip_down, clip_top: the thumb on the rounds
# ---- bake: the arms and the gun empty per output frame from their source frames, the reload tilt, the rifle's moving parts
arm.animation_data.action = bpy.data.actions.new('preview_arms'); gun.animation_data.action = bpy.data.actions.new('preview_gun')
for p in ('animation_data',):
    pass
sc.frame_start, sc.frame_end = 1, LAST
G0rest = RG[149]
import os
# KR2: the aimed pose: the eye of the preview camera (as placed below), the sight line (rear -> front, measured on the rifle:
# KF_ROOT local cm) on its axis, the rifle's up its up, the rear sight (the ZF39: the eyepiece) eye relief ahead
_rear0 = trig + fwd * (-0.02 * K) + up * (0.075 * K); _eye0 = _rear0 - fwd * (0.15 * K) + right_w * (float(os.environ.get('BG_LAT', '-0.12')) * K) + up * (0.07 * K)
CAM_LOC = _eye0 + up * (0.04 * K) - fwd * (0.05 * K); CAM_Q = (fwd - up * 0.12).normalized().to_track_quat('-Z', 'Y')
if KAR:
    SIGHT_L = ((-26.0, 0.55, 6.06), (1.38, 0.55, 6.06), 0.08) if SCOPED else ((4.2, 0.65, 3.32), (53.6, 0.65, 3.32), 0.34)   # rear, front, eye relief (m)
    sc.frame_set(1); bpy.context.view_layer.update(); _REL = gun.matrix_world.inverted() @ root.matrix_world
RLOW = float(os.environ.get('BG_RLOW', '0.03' if (KAR and not SCOPED) else '0'))   # KR3: the right hand this much lower on the stock's wrist (m): from behind the sights its knuckles covered the rear sight (the game's ADS, kar7)
def rlow(Gw): return ((Gw @ _REL).to_3x3() @ Vector((0, 0, -1))).normalized() * (RLOW * K) if KAR else Vector()
def ads_pose(G, a):
    Rr = G @ _REL; rw, fw_ = Rr @ Vector(SIGHT_L[0]), Rr @ Vector(SIGHT_L[1]); uw = (Rr.to_3x3() @ Vector((0, 0, 1))).normalized()
    f_ = (fw_ - rw).normalized(); u_ = (uw - f_ * uw.dot(f_)).normalized(); x_ = f_.cross(u_)
    Mr = Matrix((x_, u_, -f_)).transposed().to_4x4(); Mr.translation = rw
    Cm = CAM_Q.to_matrix(); Mt = Cm.to_4x4(); Mt.translation = CAM_LOC + (Cm @ Vector((0, 0, -1))) * (SIGHT_L[2] * K)
    l1, q1_, _ = (Mt @ Mr.inverted()).decompose()   # the rig moved as one piece (the game's own aiming: arms and rifle together)
    return Matrix.LocRotScale(Vector().lerp(l1, a), Quaternion().slerp(q1_, a), Vector((1, 1, 1)))
import os
TILT = [float(x) for x in os.environ.get('BG_TILT', '-35,6,-12,0.02,-0.02,-0.02').split(',')]
if KAR and 'BG_TILT' not in os.environ: TILT[0] = float(os.environ.get('BG_SCOPEROLL', os.environ.get('BG_KARROLL', '40')))   # KR2 (the user): the action turned to the right hand, rounds in from the right (was -60: the hand crossed to the left)   # RL3: canted further: the rounds go in at an angle under the scope   # RL2: lowered to the chest and pushed out (the hand reaches the action from the right, not across the eye)   # roll, pitch, yaw (deg), right, up, forward (m)
for f in range(1, LAST + 1):
    d = T[f]; sf = d['sf']
    sc.frame_set(f)
    for pb in arm.pose.bones: pb.matrix_basis = REC[sf][pb.name]
    for pb in arm.pose.bones:
        pb.keyframe_insert('location', frame=f); pb.keyframe_insert('rotation_quaternion' if pb.rotation_mode == 'QUATERNION' else 'rotation_euler', frame=f)
    G = RG[sf]; a = d['tilt']
    if a > 0:
        piv = G @ (G0rest.inverted() @ trig)
        Rt = Matrix.Rotation(math.radians(TILT[0] * a), 4, fwd) @ Matrix.Rotation(math.radians(TILT[1] * a), 4, right_w) @ Matrix.Rotation(math.radians(TILT[2] * a), 4, up)
        G = Matrix.Translation((right_w * TILT[3] + up * TILT[4] + fwd * TILT[5]) * K * a) @ Matrix.Translation(piv) @ Rt @ Matrix.Translation(-piv) @ G   # the action turned up to the camera, kept under the right arm's path
    G = Matrix.Translation(-up * (0.012 * K * d.get('jolt', 0.0))) @ G   # RL3: the rifle gives under a hard thumb push and springs back
    gun.matrix_world = G; gun.keyframe_insert('location', frame=f); gun.keyframe_insert('rotation_euler', frame=f)
bpy.context.view_layer.update()
sc.frame_set(1); bpy.context.view_layer.update()
GRIP_S = SRC0 @ (root.matrix_world.inverted() @ (gun.matrix_world @ RW[149]['R'].translation))
GRIP_S = Vector(GRIP_S)
# ---- the left hand seated on this rifle's forend as tight as on the M24's (no air): the M24 idle grip's contact (the hand
# vertices within 1 cm of the M24 stock and their gaps) matched on this rifle's surface by one wrist shift (gun space, least
# squares on the surface normals), then each finger curled on until it touches
from mathutils.bvhtree import BVHTree
LFING = {'point': ['L_point1', 'L_point2', 'L_point3'], 'middle': ['L_middle1', 'L_middle2', 'L_middle3'], 'ring': ['L_ring1', 'L_ring2', 'L_ring3'], 'pink': ['L_pink1', 'L_pink2', 'L_pink3']}
LHAND = {'L_palm', 'L_wrist', 'L_thumb1', 'L_thumb2', 'L_thumb3'} | {b for v in LFING.values() for b in v}
am_ = ob['armmesh']; gidx = {g.index: g.name for g in am_.vertex_groups}
def vgroup(v): g = max(v.groups, key=lambda x: x.weight, default=None); return gidx.get(g.group) if g else None
HV = {i: vgroup(v) for i, v in enumerate(am_.data.vertices)}; HV = {i: g for i, g in HV.items() if g in LHAND}
def bvh_of(objs):
    Gi = gun.matrix_world.inverted(); dg = bpy.context.evaluated_depsgraph_get(); V, F = [], []
    for o in objs:
        e = o.evaluated_get(dg); me = e.to_mesh(); M = Gi @ o.matrix_world; n0 = len(V)
        V += [M @ v.co for v in me.vertices]; F += [[n0 + i for i in pl.vertices] for pl in me.polygons]; e.to_mesh_clear()
    return BVHTree.FromPolygons(V, F)
def hand_pts():
    dg = bpy.context.evaluated_depsgraph_get(); e = am_.evaluated_get(dg); me = e.to_mesh(); M = gun.matrix_world.inverted() @ am_.matrix_world
    P = {i: M @ me.vertices[i].co for i in HV}; e.to_mesh_clear(); return P
def seat_pose(t, curl):
    sc.frame_set(1); bpy.context.view_layer.update()   # frame 1's keyed pose and gun; the left wrist by IK, the curl on top
    arm_ik(arm, 'L', 1, gun.matrix_world @ Matrix.Translation(t) @ RW[T[1]['sf'] if T[1].get('cycle') else 149]['L'].translation, (gun.matrix_world @ RW[T[1]['sf'] if T[1].get('cycle') else 149]['L']).to_3x3().normalized(), key=False)
    for fn, (ax, ang) in curl.items():
        for b in LFING[fn]: arm.pose.bones[b].matrix_basis = arm.pose.bones[b].matrix_basis @ Matrix.Rotation(ang, 4, ax)
    bpy.context.view_layer.update()
sc.frame_set(1); bpy.context.view_layer.update()
B24 = bvh_of([ob['base']])
rifle = [o for o in root.children_recursive if o.type == 'MESH' and not o.hide_render and o not in PROPS]
BNEW = bvh_of(rifle)
seat_pose(Vector(), {}); H0 = hand_pts()
REF = {}
for i, p in H0.items():
    loc, nrm, _, dist = B24.find_nearest(p)
    if loc is not None and dist < 0.01 / gun.matrix_world.to_scale()[0]: REF[i] = (p - loc).dot(nrm)   # the M24 contact: signed gap along the stock's normal
t = Vector()
for it in range(6):
    seat_pose(t, {}); H = hand_pts(); A = Matrix(((0, 0, 0), (0, 0, 0), (0, 0, 0))); bb = Vector()
    for i, g0 in REF.items():
        loc, nrm, _, _ = BNEW.find_nearest(H[i]); r = g0 - (H[i] - loc).dot(nrm)
        A += Matrix(((nrm.x * nrm.x, nrm.x * nrm.y, nrm.x * nrm.z), (nrm.y * nrm.x, nrm.y * nrm.y, nrm.y * nrm.z), (nrm.z * nrm.x, nrm.z * nrm.y, nrm.z * nrm.z))); bb += nrm * r
    A += Matrix.Identity(3) * (1e-3 * len(REF))   # damped: the hand stays put along the forend
    dt = A.inverted() @ bb; t += dt
    if dt.length * gun.matrix_world.to_scale()[0] < 2e-4: break
U = gun.matrix_world.to_scale()[0]
print('LGRIP contact verts %d, wrist shift %.1f mm (gun-space %s)' % (len(REF), t.length * U * 1000, tuple(round(c * U * 1000, 1) for c in t)))
CURL = {}
for fn, bones in LFING.items():
    vs = [i for i, g in HV.items() if g in bones]
    def gap(c):
        seat_pose(t, c); H = hand_pts(); return min(((H[i] - BNEW.find_nearest(H[i])[0]).dot(BNEW.find_nearest(H[i])[1]) for i in vs))
    g0 = gap(CURL); best = None
    for ax in (Vector((1, 0, 0)), Vector((-1, 0, 0)), Vector((0, 0, 1)), Vector((0, 0, -1))):
        c = dict(CURL); c[fn] = (ax, math.radians(4)); gg = gap(c)
        if gg < g0 - 1e-4 and (best is None or gg < best[1]): best = (ax, gg)
    if not best or g0 * U < 0.002: CURL_LOG = 'none'; print('LGRIP %s gap %.1f mm, no curl' % (fn, g0 * U * 1000)); continue
    ang = 0.0
    for k in range(1, 16):   # up to 30 deg per joint, 2 deg steps, until the finger meets the wood
        c = dict(CURL); c[fn] = (best[0], math.radians(2 * k)); gg = gap(c)
        if gg * U < 0.0015: ang = math.radians(2 * k); break
        ang = math.radians(2 * k)
    CURL[fn] = (best[0], ang); print('LGRIP %s gap %.1f mm -> curl %.0f deg per joint, gap %.1f mm' % (fn, g0 * U * 1000, math.degrees(ang), gap(CURL) * U * 1000))
sc.frame_set(1); bpy.context.view_layer.update()
LSHIFT = t
if os.environ.get('BG_M24') == '1' or os.environ.get('BG_NOSEAT') == '1': LSHIFT = Vector(); CURL = {}   # the reference / the old placement
# ---- per frame: the rifle's parts, the hands, the props
prev = {}; CAMT = {}; REACH = []; THUMBGAP = []
for f in range(1, LAST + 1):
    d = T[f]; sc.frame_set(f); bpy.context.view_layer.update()
    if KAR: set_bolt(*d['bolt']); key_bolt(f)
    else:
        tk = d.get('take'); cf = d.get('clip_from')
        if tk is None: tk = 0.0
        set_take(tk, cf); key_take(f)
        if 'clip_from' in d and d.get('ci') is not None and plans[-1][1][0] is d.get('_plan'): pass
    bpy.context.view_layer.update()
    Gw = gun.matrix_world
    CAMT[f] = (src2w(PORT), d.get('tilt', 0.0))   # RL2: the camera's reload zoom aims at the magazine opening
    # left wrist: where it sits on the gun in the source frame (the forend: the idle's for the reloads)
    lsf = d['sf'] if d.get('cycle') else 149
    TL = Gw @ RW[lsf]['L']; arm_ik(arm, 'L', f, Gw @ (Matrix.Translation(LSHIFT) @ RW[lsf]['L']).translation, TL.to_3x3().normalized())
    for fn, (ax, ang) in CURL.items():   # the fingers curled onto this forend (on the frame's own finger pose)
        for b in LFING[fn]:
            pb = arm.pose.bones[b]; pb.matrix_basis = REC[d['sf']][b] @ Matrix.Rotation(ang, 4, ax)
            pb.keyframe_insert('rotation_quaternion' if pb.rotation_mode == 'QUATERNION' else 'rotation_euler', frame=f); pb.keyframe_insert('location', frame=f)
    # right wrist
    if d.get('cycle'):
        sf = d['sf']; Hm = Gw @ RW[sf]['R']; wgt = max(0.0, min(1.0, (sf - 13) / 8.0)) if sf < 30 else max(0.0, min(1.0, (47 - sf) / 8.0))
        P = Hm.translation + rlow(Gw) * (1 - wgt) + (cen(knob) - Gw @ KM[sf]) * wgt; arm_ik(arm, 'R', f, P, Hm.to_3x3().normalized())
        continue
    Hg = Matrix.Translation(rlow(Gw)) @ Gw @ RW[149]['R']; w = d['w']
    keys = d.get('cpath') or [(0, 'grip')]; i = d.get('ci', 0)   # a hold: the hand on the grip
    pa = pb_ = None
    for (fa, a_), (fb, b_) in zip(keys, keys[1:]):
        if fa <= i <= fb: pa, pb_, t = a_, b_, ss((i - fa) / max(1e-6, fb - fa)); break
    if pa is None: pa = pb_ = keys[-1][1]; t = 1.0
    def cw(spec):
        if spec == 'grip': return None
        return contact_world(spec, f, d)
    ca, cb = cw(pa), cw(pb_)
    Rk0 = Gw.to_3x3().normalized() @ R_KNOB
    carry = d.get('_carry', 'pinch'); ma, mb = mode_of(pa, carry), mode_of(pb_, carry)
    HY = float(os.environ.get('BG_HANDYAW', '45'))   # RL2: rounds and clips: the hand turned about the up axis (the forearm out to the right of the view, not across it)
    Rk = Matrix.Rotation(math.radians(HY), 3, up) @ Rk0 if (ma in ('pinch', 'cgrip', 'thumb') or mb in ('pinch', 'cgrip', 'thumb')) else Rk0
    OFF = {'knob': O_KNOB, 'pinch': O_PINCH_W(Rk), 'cgrip': O_C_W(Rk), 'thumb': O_TH_W(Rk)}
    Oa = OFF.get(ma, O_KNOB); Ob = OFF.get(mb, O_KNOB)
    _RA = Matrix.Rotation(math.radians(HY), 3, up) @ Rk0 if ma in ('pinch', 'cgrip', 'thumb') else Rk0
    _RB = Matrix.Rotation(math.radians(HY), 3, up) @ Rk0 if mb in ('pinch', 'cgrip', 'thumb') else Rk0
    wa = (Hg.translation if ca is None else ca - _RA @ Oa); wb = (Hg.translation if cb is None else cb - _RB @ Ob)
    P = lerp(wa, wb, t)
    Rg = Hg.to_3x3().normalized()
    RkA = Matrix.Rotation(math.radians(HY), 3, up) @ Rk0 if ma in ('pinch', 'cgrip', 'thumb') else Rk0
    RkB = Matrix.Rotation(math.radians(HY), 3, up) @ Rk0 if mb in ('pinch', 'cgrip', 'thumb') else Rk0
    qa = (Rg if ca is None else RkA).to_quaternion(); qb = (Rg if cb is None else RkB).to_quaternion()
    Rh = qa.slerp(qb, t).to_matrix()
    pw = lerp(Vector((0, 0, 0)), (-up * 1.0 + right_w * 0.45), max(d.get('tilt', 0.0), 0.0))   # reloads: the elbow down and out (the forearm up from below-right, the action seen over it)
    _err = arm_ik(arm, 'R', f, P, Rh, pole_world=pw if pw.length > 0.2 else None)
    if _err / K > 0.01: REACH.append((f, round(_err / K * 100, 1)))
    Fa, Fb = POSES[ma], POSES[mb]   # RL2: the fingers blend between the two keys' hand shapes
    for b in RFB:
        pbn = arm.pose.bones[b]; qa_, qb_ = Fa[b].to_quaternion(), Fb[b].to_quaternion(); M = qa_.slerp(qb_, t).to_matrix().to_4x4(); M.translation = lerp(Fa[b].translation, Fb[b].translation, t)
        pbn.matrix_basis = M; pbn.keyframe_insert('rotation_quaternion' if pbn.rotation_mode == 'QUATERNION' else 'rotation_euler', frame=f); pbn.keyframe_insert('location', frame=f)
    # props: on the hand's own holding point (the pinch, the C grip's middle; the knob-grip point otherwise)
    Pc = P + Rh @ lerp(Oa, Ob, t)
    PCF[f] = Pc.copy()
    if pa == 'gclip_thumb' and pb_ == 'gclip_thumb':   # RL5: the thumb tip to the clip's top, every pushing frame (cm)
        bpy.context.view_layer.update(); tgt = contact_world('gclip_thumb', f, d) - dir2w(UP_S * 0.2 * CM)
        THUMBGAP.append((f, round((tipw('thumb') - tgt).length / K * 100, 2)))
    shown = {}
    CURF = f
    if d.get('propfn'):
        for o, M in d['propfn'](d['ci'], Pc, Rh):
            if M is not None: o.matrix_world = M; o.keyframe_insert('location', frame=f); o.keyframe_insert('rotation_euler', frame=f); o.keyframe_insert('scale', frame=f); shown[o.name] = True
    for o in PROPS:   # every prop's visibility keyed on every frame (constant interpolation)
        o.hide_render = not shown.get(o.name, False); o.keyframe_insert('hide_render', frame=f)
if KAR:   # RL4: the Kar98k's own cartridge hidden while the magazine is empty or a pressed round lies in its place
    for f in range(1, LAST + 1): rnd.hide_render = f in HIDE_RND or T[f]['bolt'][1] < 0.5; rnd.keyframe_insert('hide_render', frame=f)   # KR2 (the user): the top round never shows through the closed bolt
    # KR2: aiming moves the whole rig (arms, rifle, props) as one piece onto the camera's axis, as the game does (raising only
    # the rifle on the fixed shoulders pushed the right upper arm into the view)
    AR = bpy.data.objects.new('ads_root', None); sc.collection.objects.link(AR); AR.rotation_mode = 'QUATERNION'
    for o in [arm, gun] + [p_ for p_ in PROPS if p_.parent is None]: o.parent = AR; o.matrix_parent_inverse = Matrix()
    for f in range(1, LAST + 1):
        sc.frame_set(f); bpy.context.view_layer.update(); a = T[f].get('ads', 0.0)
        M = ads_pose(gun.matrix_basis.copy(), a) if a > 0 else Matrix()
        AR.matrix_basis = M
        for k in ('location', 'rotation_quaternion'): AR.keyframe_insert(k, frame=f)
# ---- the Garand's en-bloc clip: hidden before the hand brings it (empty reload); it follows the take's path once in
if not KAR:
    s0, s1 = segs[-1][1], segs[-1][2]
    for f in range(1, LAST + 1):   # RL2: no clip anywhere before the en-bloc reload brings one (the top-off loads loose rounds into an empty-looking action)
        i = f - s0
        for n in ('Clip', 'Bullet'):   # render visibility is not inherited: every mesh under the clip and its rounds
            for o in [parts[n]] + list(parts[n].children_recursive): o.hide_render = True; o.keyframe_insert('hide_render', frame=f)   # RL5: the static copies stand in
    for f in range(s0, s1 + 1):
        i = f - s0
        if False:   # (RL5: no carry of the take's clip any more)
            sc.frame_set(f); bpy.context.view_layer.update()
            Pc = PCF[f]
            parts['Clip'].matrix_basis = TAKE[TF0]['Clip']; bpy.context.view_layer.update()
            top = cen(ob['Clip_02 - Default_0']) + dir2w(UP_S * 1.6 * CM); dlt = Pc - top
            parts['Clip'].matrix_world = Matrix.Translation(dlt) @ parts['Clip'].matrix_world; bpy.context.view_layer.update()
            parts['Bullet'].matrix_world = parts['Clip'].matrix_world @ BUL_ON_CLIP
            for n in ('Clip', 'Bullet'): parts[n].keyframe_insert('location', frame=f); parts[n].keyframe_insert('rotation_quaternion' if parts[n].rotation_mode == 'QUATERNION' else 'rotation_euler', frame=f)
# ---- camera, light, output
cam = first_person_camera(60)
sc.frame_set(1); bpy.context.view_layer.update()
import os
LAT = float(os.environ.get('BG_LAT', '-0.12'))
rear = trig + fwd * (-0.02 * K) + up * (0.075 * K); eye = rear - fwd * (0.15 * K) + right_w * (LAT * K) + up * (0.07 * K)   # the eye left of the rear sight: the rifle low right
cam.location = eye + up * (0.04 * K) - fwd * (0.05 * K); cam.rotation_euler = (fwd - up * 0.12).normalized().to_track_quat('-Z', 'Y').to_euler()
ZOOM = float(os.environ.get('BG_ZOOM', '30'))   # RL2: in the reloads the eye stays, the view narrows onto the action (fingers on the round / clip readable)
q0 = (fwd - up * 0.12).normalized().to_track_quat('-Z', 'Y'); cam.data.sensor_fit = 'HORIZONTAL'
for f in range(1, LAST + 1):
    pt, a = CAMT[f]; a = ss(a)
    q1 = (pt - cam.location).normalized().to_track_quat('-Z', 'Y'); cam.rotation_euler = q0.slerp(q1, a).to_euler(); cam.keyframe_insert('rotation_euler', frame=f)
    cam.data.angle = math.radians(lerp(60.0, ZOOM, a)); cam.data.keyframe_insert('lens', frame=f)
sc.render.engine = 'BLENDER_EEVEE' if 'BLENDER_EEVEE' in {e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items} else 'BLENDER_EEVEE_NEXT'
# RL2: a sky over a dark ground (the metal reflects something to read as metal, not a flat grey-blue), a warm sun, true colours
wd = bpy.data.worlds.new('w'); wd.use_nodes = True; nt = wd.node_tree; bg = nt.nodes['Background']
tc = nt.nodes.new('ShaderNodeTexCoord'); sx = nt.nodes.new('ShaderNodeSeparateXYZ'); mr = nt.nodes.new('ShaderNodeMapRange'); mx = nt.nodes.new('ShaderNodeMix'); mx.data_type = 'RGBA'
mr.inputs['From Min'].default_value, mr.inputs['From Max'].default_value = -0.15, 0.35
mx.inputs['A'].default_value = (0.10, 0.09, 0.08, 1); mx.inputs['B'].default_value = (0.42, 0.52, 0.66, 1)
nt.links.new(tc.outputs['Generated'], sx.inputs[0]); nt.links.new(sx.outputs['Z'], mr.inputs['Value']); nt.links.new(mr.outputs['Result'], mx.inputs['Factor']); nt.links.new(mx.outputs['Result'], bg.inputs[0]); bg.inputs[1].default_value = 0.8; sc.world = wd
L = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); L.data.energy = 3.2; L.data.color = (1.0, 0.95, 0.88); sc.collection.objects.link(L); L.rotation_euler = (math.radians(40), math.radians(-20), math.radians(30))
sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Punchy'; sc.view_settings.exposure = float(os.environ.get('BG_EXPO', '0.8'))   # bright steel rolls off instead of clipping white; Punchy keeps the colours
if KAR:   # RL2: the Kar98k's steel as supplied is bright, fully metallic and smooth (it mirrored the sky: chrome with white blotches): blued steel instead
    for m in {m for o_ in root.children_recursive if o_.type == 'MESH' for m in o_.data.materials if m and m.node_tree}:
        nt_ = m.node_tree; b = next((x for x in nt_.nodes if x.type == 'BSDF_PRINCIPLED'), None)
        if not b or not b.inputs['Base Color'].is_linked: continue
        bc = b.inputs['Base Color'].links[0].from_socket; mt = b.inputs['Metallic'].links[0].from_socket if b.inputs['Metallic'].is_linked else None
        mul = nt_.nodes.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'; mul.inputs['B'].default_value = (0.24, 0.25, 0.28, 1)
        nt_.links.new(bc, mul.inputs['A']); nt_.links.new(mul.outputs['Result'], b.inputs['Base Color'])
        hsv = nt_.nodes.new('ShaderNodeSeparateColor'); hsv.mode = 'HSV'; nt_.links.new(bc, hsv.inputs[0])   # only the grey (steel, the texture's white patches): the saturated wood keeps its colour
        mr_ = nt_.nodes.new('ShaderNodeMapRange'); mr_.inputs['From Min'].default_value, mr_.inputs['From Max'].default_value = 0.18, 0.40
        mr_.inputs['To Min'].default_value, mr_.inputs['To Max'].default_value = 1.0, 0.0; nt_.links.new(hsv.outputs[1], mr_.inputs['Value']); nt_.links.new(mr_.outputs['Result'], mul.inputs['Factor'])
        if b.inputs['Roughness'].is_linked:
            rs = b.inputs['Roughness'].links[0].from_socket; mx_ = nt_.nodes.new('ShaderNodeMath'); mx_.operation = 'MAXIMUM'; mx_.inputs[1].default_value = 0.42
            nt_.links.new(rs, mx_.inputs[0]); nt_.links.new(mx_.outputs[0], b.inputs['Roughness'])
        if os.environ.get('BG_NONORMAL', '0') == '1' and b.inputs['Normal'].is_linked: nt_.links.remove(b.inputs['Normal'].links[0])   # the supplied normal map lights the bolt's flat faces pure white
        if os.environ.get('BG_DEBUGMAT') == '1':
            for l in list(b.inputs['Base Color'].links): nt_.links.remove(l)
            b.inputs['Base Color'].default_value = (0, 1, 0, 1)
        print('RL2 Kar98k steel darkened:', m.name)
sc.render.resolution_x, sc.render.resolution_y = 960, 540; sc.render.resolution_percentage = 100; sc.render.fps = 30
for o in bpy.data.objects:   # visibility keys hold (no blend between shown and hidden)
    ad = o.animation_data
    if ad and ad.action:
        for fc in getattr(ad.action, 'fcurves', []):
            if fc.data_path == 'hide_render':
                for kp in fc.keyframe_points: kp.interpolation = 'CONSTANT'
if os.environ.get('BG_SEATDBG') == '1' and not KAR:   # RL6: the clip copy's top (with its top round) against the receiver's rim at the opening, source z (up), per visible frame
    import numpy as _np
    for f in range(1, LAST + 1):
        sc.frame_set(f); bpy.context.view_layer.update()
        if GCLIP.hide_render: continue
        Mi = (root.matrix_world @ SRCI).inverted()
        zc = max((Mi @ (GCLIP.matrix_world @ v.co)).z for v in GCLIP.data.vertices); zb = max((Mi @ (GBUL.matrix_world @ v.co)).z for v in GBUL.data.vertices) if not GBUL.hide_render else float('nan')
        print('SEAT f%d clip top %.4f round top %.4f (rim right -9.4669 left -9.4540): clip over the right rim by %.2f cm' % (f, zc, zb, (max(zc, zb if zb == zb else zc) + 9.4669) * SG * 100))
print('REACH misses (frame, cm):', len(REACH), REACH[:12], 'max', max([e for _, e in REACH], default=0))
print('THUMB to clip top (frame, cm):', THUMBGAP, 'max', max([e for _, e in THUMBGAP], default=0))
if os.environ.get('BG_REACHONLY') == '1': sys.exit(0)
bpy.ops.wm.save_as_mainfile(filepath=OUT + '/%s.blend' % GUN)
json.dump({'segments': segs, 'last': LAST}, open(OUT + '/%s_segments.json' % GUN, 'w'))
CLOSE = os.environ.get('BG_CLOSE') == '1'
if os.environ.get('BG_M24') == '1':   # reference: the M24 itself in its idle grip (its meshes back, this rifle hidden, no seat)
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.parent is not None and o.name in ('base', 'bolt', 'boltrear', 'cheekrest', 'clip', 'scope', 'silencer', 'trigger', 'bullet', 'glass'): o.hide_render = False
    for o in root.children_recursive: o.hide_render = True
DBG = os.environ.get('BG_DEBUGCAM')
for fr in STILLS:
    sc.frame_set(fr)
    if DBG:   # a side camera on the magazine opening: from the right, in front and above (where the hand and the prop are)
        bpy.context.view_layer.update(); pt, _ = CAMT[fr]; dv = [float(x) for x in DBG.split(',')]
        try: cam.animation_data_clear(); cam.data.animation_data_clear()
        except Exception: pass
        cam.location = pt + (right_w * dv[0] + up * dv[1] + fwd * dv[2]) * K; cam.rotation_euler = (pt - cam.location).to_track_quat('-Z', 'Y').to_euler(); cam.data.angle = math.radians(40)
    if CLOSE:   # a close-up of the left hand on the forend, from the left and a little below
        bpy.context.view_layer.update(); pal = arm.matrix_world @ arm.pose.bones['L_palm'].head
        cam.location = pal - right_w * (0.20 * K) - up * (0.03 * K) + fwd * (0.14 * K); cam.rotation_euler = (pal - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = OUT + '/%s_still_%03d%s.png' % (GUN, fr, '_close' if CLOSE else '_dbg' if DBG else ''); bpy.ops.render.render(write_still=True)
if RENDER or os.environ.get('BG_STRIPS') == '1':   # RL2: the key moments of each reload case (tiled by tools/strips.py)
    SJ = []
    for ci, (name, mk) in enumerate(MARKS):
        for k, (lab, fr) in enumerate(mk.items()):
            sc.frame_set(fr); fn = OUT + '/strip/%s_%d_%d.png' % (GUN, ci + 1, k); sc.render.filepath = fn; bpy.ops.render.render(write_still=True); SJ.append([GUN, ci + 1, name, lab, fr, fn])
    json.dump(SJ, open(OUT + '/strip/%s.json' % GUN, 'w'))
if RENDER:
    try: sc.render.image_settings.media_type = 'VIDEO'
    except Exception: pass
    sc.render.image_settings.file_format = 'FFMPEG'; sc.render.ffmpeg.format = 'MPEG4'; sc.render.ffmpeg.codec = 'H264'; sc.render.ffmpeg.constant_rate_factor = 'HIGH'
    sc.render.filepath = OUT + '/%s_reload.mp4' % GUN; bpy.ops.render.render(animation=True)
