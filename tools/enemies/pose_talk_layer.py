# BC5: a held pose brought to life: one joint pose (the first frame of <pose clip> in <pose.glb>) plus, layered on top,
# - the head / neck / spine motion of another character's talking idle (<talk.glb> <talk clip>), as each joint's turn
#   away from that clip's mean, carried across in armature space through both rigs' rest frames (the rigs need not share
#   bone axes or bind poses: both GLBs face glTF +z, as convert_enemy_glb.py writes them),
# - a slow breath (Spine1 / Spine2), small hand shifts and a slow leg shake, on whole fractions of the loop,
# - a nod down by <nod deg> (split neck 40 % / head 60 %) about each bone's own left-right axis.
# The talking layer is made seamless by cross-fading its last <fade s> into its start; the breath and hands are periodic.
# The result replaces <pose clip> in place (same name), sampled at 30 fps.
#   python -I tools/enemies/pose_talk_layer.py <pose.glb> <pose clip> <talk.glb> <talk clip> <out.glb> <nod deg> [<fade s>]
import json, struct, sys
import numpy as np
P_GLB, P_CLIP, T_GLB, T_CLIP, OUT, NOD = sys.argv[1:7]; NOD = float(NOD); FADE = float(sys.argv[7]) if len(sys.argv) > 7 else 1.5
FPS = 30.0
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return J, bytearray(b[28 + jl:28 + jl + bl])
def qmul(a, b):
    x1, y1, z1, w1 = np.moveaxis(a, -1, 0); x2, y2, z2, w2 = np.moveaxis(b, -1, 0)
    return np.stack([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2], -1)
def qinv(q): return q * np.array([-1, -1, -1, 1.0])
def qlog(q):   # rotation vector
    q = np.where(q[..., 3:4] < 0, -q, q); v = q[..., :3]; s = np.linalg.norm(v, axis=-1, keepdims=True); a = 2 * np.arctan2(s, q[..., 3:4])
    return np.where(s > 1e-9, v / np.maximum(s, 1e-12) * a, 2 * v)
def qexp(r):
    a = np.linalg.norm(r, axis=-1, keepdims=True); s = np.where(a > 1e-9, np.sin(a / 2) / np.maximum(a, 1e-12), 0.5)
    return np.concatenate([r * s, np.cos(a / 2)], -1)
def m2q(m):
    t = np.trace(m)
    if t > 0: s = 2 * np.sqrt(t + 1); return np.array([(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4])
    i = int(np.argmax(np.diag(m))); j, k = (i + 1) % 3, (i + 2) % 3; s = 2 * np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]); q = np.zeros(4)
    q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s; q[3] = (m[k, j] - m[j, k]) / s; return q
def q2m(q):
    x, y, z, w = q; return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
class Rig:
    def __init__(s, J, B):
        s.J, s.B, s.N = J, B, J['nodes']; s.par = {c: i for i, n in enumerate(s.N) for c in n.get('children', [])}
        s.byName = {s.N[j].get('name'): j for j in J['skins'][0]['joints']}
    def acc(s, ai):
        a = s.J['accessors'][ai]; v = s.J['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
        return np.frombuffer(s.B, np.float32, a['count'] * k, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], k).astype(np.float64)
    def restR(s, i):   # rest rotation in armature space (columns normalised: no scale)
        R = np.eye(3)
        while True:
            n = s.N[i]; L = q2m(n.get('rotation', [0, 0, 0, 1])) if 'matrix' not in n else np.array(n['matrix']).reshape(4, 4).T[:3, :3]
            R = L @ R
            if i not in s.par: break
            i = s.par[i]
        return R / np.linalg.norm(R, axis=0, keepdims=True)
    def channels(s, clip):
        an = next(a for a in s.J['animations'] if a['name'] == clip)
        return an, {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
PJ, PB = load(P_GLB); P = Rig(PJ, PB); TJ, TB = load(T_GLB); T = Rig(TJ, TB)
pan, pch = P.channels(P_CLIP); tan, tch = T.channels(T_CLIP)
# the talking layer: per joint, the turn away from the clip's mean, in armature space
GAIN = {'Spine': 0.2, 'Spine1': 0.2, 'Spine2': 0.3}   # the back sways little: the arms hang off it and the hands are planted on the ground
tt = None; layer = {}
for nm in ('Spine', 'Spine1', 'Spine2', 'Neck', 'Head'):
    ti = T.byName[nm]; smp = tch.get((ti, 'rotation'))
    if smp is None: continue
    t = T.acc(smp['input'])[:, 0]; q = T.acc(smp['output'])
    if tt is None: tt = np.arange(t[0], t[-1], 1 / FPS)
    q = np.stack([np.interp(tt, t, q[:, k]) for k in range(4)], -1); q /= np.linalg.norm(q, axis=1, keepdims=True)
    mean = qexp(qlog(q).mean(0)); d = qlog(qmul(qinv(mean)[None], q))   # bone-local turn from the mean
    d *= GAIN.get(nm, 1.0)
    Rt = T.restR(ti); layer[nm] = d @ Rt.T                               # into armature space (a rotation vector turns with the frame)
n = len(tt); K = int(round(FADE * FPS)); L = n - K
for nm, d in layer.items():   # seamless: the last K frames cross-faded into the first K, the clip shortened by K
    out = d[:L].copy(); w = (np.arange(K) / K)[:, None]; out[:K] = d[L:L + K] * (1 - w) + d[:K] * w; layer[nm] = out
time = np.arange(L) / FPS; dur = L / FPS; ph = 2 * np.pi * time / dur
X = np.array([1.0, 0, 0])   # the left-right axis in armature space (the model faces +z)
extra = {   # armature-space rotation vectors on top: breath and nod (about the lateral axis), hands' small shifts
    'Spine1': np.outer(np.radians(0.8) * np.sin(ph * 6), X), 'Spine2': np.outer(np.radians(1.2) * np.sin(ph * 6 + 0.4), X),
    'LeftHand': np.outer(np.radians(2.0) * np.sin(ph * 2 + 1.0), np.array([0, 0, 1.0])) + np.outer(np.radians(0.5) * np.sin(ph * 3), X),
    'RightHand': np.outer(np.radians(2.0) * np.sin(ph * 2 + 2.6), np.array([0, 0, 1.0])) + np.outer(np.radians(0.5) * np.sin(ph * 3 + 1.9), X),
    # a slow leg shake (about 1.7 s a swing): the raised left leg swings and bobs at the knee
    'LeftUpLeg': np.outer(np.radians(3.0) * np.sin(ph * 12), X) + np.outer(np.radians(1.5) * np.sin(ph * 12 + 1.2), np.array([0, 0, 1.0])),
    'LeftLeg': np.outer(np.radians(4.0) * np.sin(ph * 12 - 0.8), X),
    'RightUpLeg': np.outer(np.radians(0.5) * np.sin(ph * 12 + 2.2), X) + np.outer(np.radians(1.5) * np.sin(ph * 12 + 3.4), np.array([0, 0, 1.0])),   # the leg along the ground: a sideways sway, hardly any lift (its heel stays out of the ground)
    'RightLeg': np.outer(np.radians(0.6) * np.sin(ph * 12 + 1.4), X)}
nod = {'Neck': 0.4, 'Head': 0.6}
anim = {'name': P_CLIP, 'channels': [], 'samplers': []}
def add(arr, typ):
    data = np.ascontiguousarray(arr, np.float32).tobytes()
    while len(PB) % 4: PB.append(0)
    PJ['bufferViews'].append({'buffer': 0, 'byteOffset': len(PB), 'byteLength': len(data)}); PB.extend(data)
    PJ['accessors'].append({'bufferView': len(PJ['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}); return len(PJ['accessors']) - 1
ti_ = add(time[:, None], 'SCALAR'); PJ['accessors'][ti_].update(min=[0.0], max=[float(time[-1])])
moved = []
for (node, path), smp in pch.items():
    v0 = P.acc(smp['output'])[0]
    if path == 'rotation':
        nm = P.N[node].get('name'); q = np.tile(v0 / np.linalg.norm(v0), (L, 1)); Rp = P.restR(node); r = np.zeros((L, 3))
        if nm in layer: r += layer[nm]
        if nm in extra: r += extra[nm]
        if nm in nod: r += np.radians(NOD * nod[nm]) * X
        if np.abs(r).max() > 0: q = qmul(q, qexp(r @ Rp)); moved.append(nm)   # armature-space turn into the bone's frame, after its pose
        arr, typ = q, 'VEC4'
    else: arr, typ = np.tile(v0, (L, 1)), 'VEC3' if len(v0) == 3 else 'VEC4'
    anim['samplers'].append({'input': ti_, 'output': add(arr, typ), 'interpolation': 'LINEAR'}); anim['channels'].append({'sampler': len(anim['samplers']) - 1, 'target': {'node': node, 'path': path}})
for nm in list(layer) + list(extra) + list(nod):
    assert nm in moved, ('joint not keyed by the pose clip', nm)
PJ['animations'] = [a for a in PJ['animations'] if a['name'] != P_CLIP] + [anim]
while len(PB) % 4: PB.append(0)
PJ['buffers'][0]['byteLength'] = len(PB); js = json.dumps(PJ, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(PB))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(PB), 0x004E4942)); f.write(PB)
worst = 0.0   # seam: last frame to first no bigger than the largest step inside the loop (x1.5)
for smp in anim['samplers']:
    a = PJ['accessors'][smp['output']]
    if a['type'] != 'VEC4': continue
    v = PJ['bufferViews'][a['bufferView']]; q = np.frombuffer(PB, np.float32, a['count'] * 4, v['byteOffset']).reshape(-1, 4).astype(np.float64)
    step = np.linalg.norm(qlog(qmul(qinv(q[:-1]), q[1:])), axis=1).max(); end = np.linalg.norm(qlog(qmul(qinv(q[-1:]), q[:1])))
    assert end <= 1.5 * step + 1e-5, ('loop seam', end, step); worst = max(worst, float(end))
print('loop seam: largest last-to-first turn %.4f deg' % np.degrees(worst))
print('wrote %s: %s, %.1f s loop, %d frames, joints moved %s' % (OUT, P_CLIP, dur, L, sorted(set(moved))))
