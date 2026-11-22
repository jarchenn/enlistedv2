# GR6 (the user): one take on the Garand arms rig (E:/nmk/boltguns/gar.blend, BG4): the empty en-bloc reload (the rig's own
# frames 108-194, its camera), the raise to ADS (the rifle brought onto the fixed camera's line, eased; FOV to the game's
# ADS FOV), a settled aim with a slow breathing sway, three shots on the GR1 fireCurve (gun, camera kick / roll, the rod and
# bolt stroke), the settle. The rig's keys play through NLA strips (held after their end); the raise, shots and stroke are on
# extra objects (a carrier over the rig, offset empties over the rod and bolt), so the render keeps them. Preview only.
#   blender -b --factory-startup --python arms_full.py -- <gar.blend> <m1_garand.json> <out dir> [first-last frames] [save.blend]
import bpy, sys, json, math, random
import numpy as np
from mathutils import Vector, Matrix, Quaternion
A = sys.argv[sys.argv.index('--') + 1:]; BLEND, WJ, OUT = A[0], A[1], A[2]
RANGE = A[3] if len(A) > 3 else None; SAVE = A[4] if len(A) > 4 else None
W = json.load(open(WJ)); FC = W['fireCurve']; ROWS = np.array(FC['frames'], float); FT = np.arange(len(ROWS)) / FC['fps']
bpy.ops.wm.open_mainfile(filepath=BLEND)
import os; PEEP = json.load(open(BLEND + '.peep.json'))['centre'] if os.path.exists(BLEND + '.peep.json') else (-0.0031, -9.1615)   # GR7: the rebuilt ring's centre
sc = bpy.context.scene; ob = bpy.data.objects; K = 1.221 / 1.092
SRC0, SRC1 = 108, 194                      # gar_segments.json: the empty reload 108-180, then the rig settles to 194
N_RL = SRC1 - SRC0 + 1                     # output 1..87
RAISE0, RAISE_N = N_RL + 1, 12             # 88-99: the raise (0.4 s)
AIM0 = RAISE0 + RAISE_N                    # 100: on the sights
SHOTS = [AIM0 + 30.5, AIM0 + 50.5, AIM0 + 70.5]   # three shots 0.667 s apart (the reference's spacing)
END = AIM0 + 105
SEAT_F, ROD_F = 33, 49   # GR11: output frames of the clip driven home (source 140) and the rod slammed forward (source 156)
fpcam = ob['fpcam']
NEUTRAL = math.radians(60.0)   # the game's first-person (hip) view-model FOV
# ---- sample the rig's camera over the reload, and the hold pose's sight line at its end
CAMW, CAMA = {}, {}
for s in range(SRC0, SRC1 + 1):
    sc.frame_set(s); CAMW[s] = fpcam.matrix_world.copy(); CAMA[s] = fpcam.data.angle
sc.frame_set(SRC1); bpy.context.view_layer.update()
g = ob['Garand001']; G = g.matrix_world.copy()
REAR, FRONT = G @ Vector((PEEP[0], PEEP[1], 8.31)), G @ Vector((0.0, -9.15, -21.10))   # peep centre, blade tip (Garand001 local; muzzle -z, up -y)
L = (FRONT - REAR).normalized(); UPg = (G.to_3x3() @ Vector((0, -1, 0))).normalized(); UPg = (UPg - L * UPg.dot(L)).normalized()
relief = W['ads']['eyeRelief'] * K
C_ads = Matrix.Translation(REAR - L * relief) @ Matrix((L.cross(UPg), UPg, -L)).transposed().to_4x4()   # the eye on the sights, rig at rest
C_cam = CAMW[SRC1]                                                                                      # the head stays here
D = C_cam @ C_ads.inverted()                                                                            # the rig onto the head's line
# the rod / bolt stroke direction in each one's parent space, at the hold
def handle_dir(o): P = o.parent.matrix_world if o.parent else Matrix.Identity(4); return P.to_3x3().inverted() @ (-L)
# ---- the rig's keys through NLA strips: output 1 = source 108, held after the end
for o in list(ob) + [o.data for o in ob if o.type == 'CAMERA']:
    ad = getattr(o, 'animation_data', None)
    if not ad or not ad.action: continue
    act = ad.action; tr = ad.nla_tracks.new(); st = tr.strips.new(act.name, 1, act)
    st.action_frame_start, st.action_frame_end = SRC0, SRC1; st.frame_start, st.frame_end = 1, N_RL
    st.extrapolation = 'HOLD'
    ad.action = None
# ---- a carrier over the rig (identity = the rig as keyed), offsets over the rod and bolt
E = ob.new('rig_carrier', None) if hasattr(ob, 'new') else bpy.data.objects.new('rig_carrier', None); sc.collection.objects.link(E)
for o in list(ob):
    if o is E or o.parent is not None or o.type in ('CAMERA', 'LIGHT'): continue
    o.parent = E; o.matrix_parent_inverse = Matrix.Identity(4)
OFF = []
for n in ('Charging_Handle_02 - Default_0', 'Bolt_02 - Default_0'):
    if n not in ob: continue
    m = ob[n]; p = m.parent; d = handle_dir(m)
    e = bpy.data.objects.new('stroke_' + n.split('_')[0], None); sc.collection.objects.link(e)
    e.parent = p; e.matrix_parent_inverse = Matrix.Identity(4)
    mpi = m.matrix_parent_inverse.copy(); m.parent = e; m.matrix_parent_inverse = mpi
    OFF.append((e, d))
cam = bpy.data.objects.new('takecam', bpy.data.cameras.new('takecam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.sensor_fit = 'VERTICAL'; cam.data.clip_start = 0.005; fpcam.hide_render = True
# ---- a world to see the camera move against: sky and a checkered ground under the head
sc.world = sc.world or bpy.data.worlds.new('w'); sc.world.use_nodes = True
bgn = sc.world.node_tree.nodes['Background']; bgn.inputs['Color'].default_value = (0.42, 0.55, 0.72, 1); bgn.inputs['Strength'].default_value = 0.9
UPw = Vector((0, 0, 1)); fwd_h = (C_cam.to_3x3() @ Vector((0, 0, -1))); fwd_h.z = 0; fwd_h.normalize()
bpy.ops.mesh.primitive_plane_add(size=400 * K, location=C_cam.translation - UPw * 1.6 * K); gp = bpy.context.object; gp.name = 'take_ground'
gm = bpy.data.materials.new('ground'); gm.use_nodes = True; nt = gm.node_tree; ck = nt.nodes.new('ShaderNodeTexChecker'); ck.inputs['Scale'].default_value = 400
ck.inputs['Color1'].default_value = (0.32, 0.36, 0.22, 1); ck.inputs['Color2'].default_value = (0.22, 0.26, 0.15, 1); nt.links.new(ck.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
nt.nodes['Principled BSDF'].inputs['Roughness'].default_value = 1.0; gp.data.materials.append(gm)
tl = bpy.data.materials.new('treeline'); tl.use_nodes = True; tn = tl.node_tree; tns = tn.nodes.new('ShaderNodeTexNoise'); tns.inputs['Scale'].default_value = 6.0; tns.inputs['Detail'].default_value = 8.0
tcr = tn.nodes.new('ShaderNodeValToRGB'); tcr.color_ramp.elements[0].color = (0.035, 0.06, 0.03, 1); tcr.color_ramp.elements[1].color = (0.12, 0.17, 0.08, 1)
tn.links.new(tns.outputs['Fac'], tcr.inputs['Fac']); tn.links.new(tcr.outputs['Color'], tn.nodes['Principled BSDF'].inputs['Base Color']); tn.nodes['Principled BSDF'].inputs['Roughness'].default_value = 1.0
bpy.ops.mesh.primitive_cube_add(size=1, location=C_cam.translation + fwd_h * (20 * K) + UPw * (1.4 * K)); tw = bpy.context.object; tw.name = 'take_treeline'   # GR10: a dark tree line (the video's smoke shows against its trees)
tw.rotation_euler = (0, 0, math.atan2(fwd_h.y, fwd_h.x) + math.pi / 2); tw.scale = (60 * K, 0.5 * K, 6 * K); tw.data.materials.append(tl)
for k in range(-6, 7):   # posts down range: something upright to read yaw and roll against
    bpy.ops.mesh.primitive_cube_add(size=1, location=C_cam.translation + fwd_h * (25 * K) + fwd_h.cross(UPw) * (k * 2.5 * K) - UPw * 0.6 * K); c = bpy.context.object
    c.scale = (0.08 * K, 0.08 * K, 2.0 * K); c.data.materials.append(gm)
# ---- the take, frame by frame
def ss(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)
def row(t):
    if t <= 0 or t >= FT[-1]: return np.zeros(10)
    return np.array([np.interp(t, FT, ROWS[:, k]) for k in range(10)])
Dl, Dq = D.to_translation(), D.to_quaternion()
# GR11 (the user): no rock-still reload: the head and the hands each wander on their own (smooth sums of slow sines, a few
# per channel, unrelated phases), with a small jolt when the thumb drives the clip home and when the hand slams the rod
# forward; full through the reload, easing to a quarter on the sights (a held aim's sway)
sys.path.insert(0, __import__('os').path.dirname(__file__)); import gr_handheld   # the shared wander (also the game rig's, gar_prep.py)
def handheld(f, w): return gr_handheld.handheld(f / 30.0, w, [(SEAT_F / 30.0, 1.0), (ROD_F / 30.0, 1.6)], K)
def rig_shake(Cf, Rr, Tr): return Cf @ Matrix.Translation(Tr) @ Rr @ Cf.inverted()   # the hands about the eye
log = []
for f in range(1, END + 1):
    if f <= N_RL:   # the reload: the rig as keyed, the rig's camera
        s = SRC0 + f - 1; Ch, Rr, Tr = handheld(f, 1.0); Cf = CAMW[s] @ Ch; ang = NEUTRAL; Ef = rig_shake(Cf, Rr, Tr) @ Matrix.Identity(4)   # GR8 (the user): no zoom in the reload (the rig zoomed to 30 deg); its look-down kept
        r = np.zeros(10); h = 0.0; a = 0.0
    else:
        a = ss((f - RAISE0 + 1) / RAISE_N)                                   # 0 -> 1 over the raise
        Ba = Matrix.Translation(Dl * a) @ Quaternion().slerp(Dq, a).to_matrix().to_4x4()
        ang = NEUTRAL + (math.radians(W['ads'].get('vmFov') or 60) - NEUTRAL) * a
        t = (f - AIM0) / 30.0; bw = 0.0   # GR12 (the user): no idle sway on the sights (only the player's own movement would move them)
        sway = Matrix.Rotation(math.radians(0.06 * math.sin(2 * math.pi * 0.28 * t) * bw), 4, 'X') @ Matrix.Rotation(math.radians(0.04 * math.sin(2 * math.pi * 0.17 * t + 1.1) * bw), 4, 'Y')
        ages = [(f - s) / 30 for s in SHOTS if f > s]
        r = sum((row(x) for x in ages), np.zeros(10)); h = max([row(x)[9] for x in ages] + [0.0])
        kick = Matrix.Rotation(math.radians(r[4]), 4, 'X') @ Matrix.Rotation(math.radians(-r[5]), 4, 'Y') @ Matrix.Rotation(math.radians(r[8]), 4, 'Z')
        hw = 1.0 - a; Ch, Rr, Tr = handheld(f, hw)   # GR12 (the user): the reload's handheld motion fades out over the raise: on the sights, still
        Cf0 = C_cam @ sway @ kick; Cf = C_cam @ Ch @ sway @ kick
        Chf = handheld(f, hw * a)[0] if a < 1 else Ch   # the shouldered rifle takes the head's motion in step with the raise (no pop at its start)
        piv = Vector((0, 0, -relief))
        Rg = (Quaternion((0, 1, 0), math.radians(-r[7])) @ Quaternion((1, 0, 0), math.radians(r[6])) @ Quaternion((0, 0, 1), math.radians(r[3]))).to_matrix().to_4x4()
        Gm = Matrix.Translation(Vector((r[2], r[1], r[0])) * K) @ Matrix.Translation(piv) @ Rg @ Matrix.Translation(-piv)
        # the rig on the head: at rest Ba (the raise); on the sights the shot's gun motion in the camera's frame
        Ef = rig_shake(Cf, Rr, Tr) @ (C_cam @ Chf @ C_cam.inverted()) @ Cf0 @ Gm @ C_cam.inverted() @ Ba
    cam.matrix_world = Cf; cam.keyframe_insert('location', frame=f); cam.keyframe_insert('rotation_euler', frame=f)
    cam.data.angle = ang; cam.data.keyframe_insert('lens', frame=f)
    E.matrix_world = Ef; E.keyframe_insert('location', frame=f); E.keyframe_insert('rotation_euler', frame=f); E.keyframe_insert('scale', frame=f)
    for e, d in OFF: e.location = d * (h * K); e.keyframe_insert('location', frame=f)
    log.append(dict(frame=f, raise_=a, cam_pitch_deg=float(r[4]), back_m=float(r[0]), pitch_deg=float(r[6]), handle_m=float(h)))
for o in [cam, cam.data, E] + [e for e, _ in OFF]:
    ad = o.animation_data
    if ad and ad.action:
        for fc in getattr(ad.action, 'fcurves', []):
            for kp in fc.keyframe_points: kp.interpolation = 'LINEAR'
# ---- GR10 (the user): muzzle smoke and flash as in the reference clip (read from its frames 9-27 / 28-43): on the shot frame
# an orange-white glow ~330 px across just above / right of the sight; then a light-grey translucent plume right of the
# receiver and a little above the line, rising and drifting right, thin by ~0.2 s and gone by ~0.45 s. World-fixed puffs
# (they do not ride the rifle) spawned at the muzzle, thrown right / up / forward, slowing, swelling and fading.
import random
def mat_card(name, col, emit, flash=False):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; N = nt.nodes; Lk = nt.links
    for n in list(N): N.remove(n)
    out = N.new('ShaderNodeOutputMaterial'); mix = N.new('ShaderNodeMixShader'); tr = N.new('ShaderNodeBsdfTransparent'); em = N.new('ShaderNodeEmission')
    em.inputs['Color'].default_value = col; em.inputs['Strength'].default_value = emit
    tc = N.new('ShaderNodeTexCoord'); gr = N.new('ShaderNodeTexGradient'); gr.gradient_type = 'SPHERICAL'
    oi = N.new('ShaderNodeObjectInfo'); ns = N.new('ShaderNodeTexNoise'); ns.inputs['Scale'].default_value = 3.0 if not flash else 1.5; ns.inputs['Detail'].default_value = 4.0
    m1 = N.new('ShaderNodeMath'); m1.operation = 'MULTIPLY'; m2 = N.new('ShaderNodeMath'); m2.operation = 'MULTIPLY'; pw = N.new('ShaderNodeMath'); pw.operation = 'POWER'; pw.inputs[1].default_value = 1.6
    sep = N.new('ShaderNodeSeparateColor') if hasattr(bpy.types, 'ShaderNodeSeparateColor') else N.new('ShaderNodeSeparateRGB')
    Lk.new(tc.outputs['Object'], gr.inputs['Vector']); Lk.new(gr.outputs['Fac'], pw.inputs[0])
    Lk.new(tc.outputs['Object'], ns.inputs['Vector'])
    ns.noise_dimensions = '4D'; sc_ = N.new('ShaderNodeSeparateColor'); Lk.new(oi.outputs['Color'], sc_.inputs[0]); Lk.new(sc_.outputs[0], ns.inputs['W'])   # GR11: the smoke's texture boils (W = the puff's age)
    wp = N.new('ShaderNodeMath'); wp.operation = 'POWER'; wp.inputs[1].default_value = 1.0 if flash else 2.6   # GR11: wisps (the noise's contrast up)
    wm = N.new('ShaderNodeMath'); wm.operation = 'MULTIPLY'; wm.inputs[1].default_value = 1.0 if flash else 2.4
    if not flash: ns.inputs['Distortion'].default_value = 0.9; ns.inputs['Scale'].default_value = 2.2; ns.inputs['Detail'].default_value = 6.0
    Lk.new(ns.outputs['Fac'], wp.inputs[0]); Lk.new(wp.outputs[0], wm.inputs[0])
    Lk.new(pw.outputs[0], m1.inputs[0]); Lk.new(wm.outputs[0], m1.inputs[1])
    Lk.new(oi.outputs['Alpha'], m2.inputs[0]); Lk.new(m1.outputs[0], m2.inputs[1])
    Lk.new(m2.outputs[0], mix.inputs['Fac']); Lk.new(tr.outputs[0], mix.inputs[1]); Lk.new(em.outputs[0], mix.inputs[2]); Lk.new(mix.outputs[0], out.inputs['Surface'])
    if hasattr(m, 'surface_render_method'): m.surface_render_method = 'BLENDED'
    else: m.blend_method = 'BLEND'
    return m
SMOKE = mat_card('gr_smoke', (0.78, 0.78, 0.76, 1), 0.9); FLASH = mat_card('gr_flash', (1.0, 0.62, 0.32, 1), 6.0, flash=True)
def card(name, mat):
    me = bpy.data.meshes.new(name); me.from_pydata([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)], [], [(0, 1, 2, 3)]); me.materials.append(mat)
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); return o
MUZ = Vector((0.0, -7.8, -22.5))   # Garand001 local: the muzzle (bore centre at the crown)
CAMS = {}
for f in range(1, END + 1): sc.frame_set(f); CAMS[f] = cam.matrix_world.copy()
rng = random.Random(7)
for n_shot, S in enumerate(SHOTS):
    f0 = int(math.ceil(S)); sc.frame_set(f0); bpy.context.view_layer.update()
    m0 = ob['Garand001'].matrix_world @ MUZ
    Rc = CAMS[f0].to_3x3(); right, upv, fwdv = Rc @ Vector((1, 0, 0)), Rc @ Vector((0, 1, 0)), Rc @ Vector((0, 0, -1))
    fl = card('gr_flash_%d' % n_shot, FLASH)
    for f in range(f0 - 1, f0 + 2):   # one frame: the shot's
        on = f == f0; Rq = CAMS[f].to_quaternion()
        fl.location = m0 + upv * (0.02 * K) + right * (0.015 * K); fl.rotation_mode = 'QUATERNION'; fl.rotation_quaternion = Rq
        r_ = (0.11 * K) if on else 0.0; fl.scale = (r_, r_, r_); fl.color = (1, 1, 1, 1.0 if on else 0.0)
        for pth in ('location', 'rotation_quaternion', 'scale', 'color'): fl.keyframe_insert(pth, frame=f)
    for k in range(10):   # GR11 (the user): close to the rifle (out along the bore, round the front sight), changing shape as it goes
        pu = card('gr_smoke_%d_%d' % (n_shot, k), SMOKE); pu.rotation_mode = 'QUATERNION'
        dl = rng.uniform(0.0, 3.0) / 30.0                                            # released over the first three frames
        v = (right * rng.uniform(0.15, 0.5) + upv * rng.uniform(-0.05, 0.35) + fwdv * rng.uniform(0.2, 0.9)) * K   # m/s, slowing
        p0 = m0 + (right * rng.uniform(-0.02, 0.03) + upv * rng.uniform(-0.015, 0.02) + fwdv * rng.uniform(0.0, 0.10)) * K
        s0, grow, a0, tau = rng.uniform(0.04, 0.07), rng.uniform(0.3, 0.5), rng.uniform(0.11, 0.19), rng.uniform(0.10, 0.16)
        spin, spin_v, w0 = rng.uniform(0, 6.283), rng.uniform(-2.0, 2.0), rng.uniform(0, 50)
        st = [(rng.uniform(4, 9), rng.uniform(0, 6.283), rng.uniform(0.25, 0.5)) for _ in range(2)]   # its stretch: rate, phase, depth (x, y)
        for f in range(f0 - 1, min(END, f0 + 24) + 1):
            age = (f - S) / 30.0 - dl
            if age <= 0: a_ = 0.0; sz = 0.0; pos = p0; sx = sy = 0.0
            else:
                travel = 0.25 * (1 - math.exp(-age / 0.25))
                pos = p0 + v * travel + upv * (0.06 * K * age); sz = (s0 + grow * travel + 0.2 * age) * K
                sx = sz * (1 + st[0][2] * math.sin(st[0][0] * age + st[0][1])); sy = sz * (1 + st[1][2] * math.sin(st[1][0] * age + st[1][1]))
                a_ = a0 * min(1.0, age / 0.03) * (math.exp(-age / tau) + 0.06 * math.exp(-age / 0.35))
            pu.location = pos; pu.rotation_quaternion = CAMS[f].to_quaternion() @ Quaternion((0, 0, 1), spin + spin_v * max(age, 0))
            pu.scale = (sx, sy, 1.0); pu.color = (w0 + 1.8 * max(age, 0), 1, 1, a_)
            for pth in ('location', 'rotation_quaternion', 'scale', 'color'): pu.keyframe_insert(pth, frame=f)
json.dump(log, open(OUT + '/take.json', 'w'), indent=1)
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 1920, 1080, 100
sc.render.film_transparent = False; sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGB'
sc.render.fps = 30; f0, f1 = (1, END) if not RANGE else (int(RANGE.split('-')[0]), int(RANGE.split('-')[-1]))
sc.frame_start, sc.frame_end = f0, f1; sc.render.filepath = OUT + '/t_'
if SAVE: bpy.ops.wm.save_as_mainfile(filepath=SAVE, copy=True)
bpy.ops.render.render(animation=True)
print('TAKE', f0, f1, 'of', END, 'raise', RAISE0, 'aim', AIM0, 'shots', SHOTS)
