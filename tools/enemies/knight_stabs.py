# KS spear knight: stab clips made from the sword-and-shield pack's lunges, at the glTF level. The pack has no thrust (each
# attack's tip moves across its blade at full reach), so each new clip copies a lunge's channels byte for byte (legs, body,
# timing, the left arm and shield) and replaces only the right arm's rotations: a two-bone IK drives the hand from a cocked
# spot by the hip straight forward to near full reach at the lunge's own furthest-reach frame, holds, draws back, and blends
# into the base clip at both ends; the hand turns so the weapon (KNB_RightHand_weapon, its head the wide end) points at a
# chest-height spot ahead. The elbow keeps the base clip's bend plane, pulled down and out at full weight.
#   python -I tools/enemies/knight_stabs.py <knight.glb> <out.glb>
import json, struct, sys
import numpy as np
SRC, OUT = sys.argv[1:3]
# new clip, base clip, strike frame (the base's furthest tip reach, measured; attack (4): its fastest hand), cocked / extended scale
SPEC = [('spear thrust', 'sword and shield attack (2)', 15, 1.0), ('spear thrust (2)', 'sword and shield attack (3)', 19, 1.0),
        ('spear thrust (3)', 'sword and shield slash', 17, 1.0), ('spear thrust (4)', 'sword and shield slash (3)', 21, 1.0),
        ('spear poke', 'sword and shield attack (4)', 11, 0.6)]
b = open(SRC, 'rb').read(); assert b[:4] == b'glTF'
jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
BIN = bytearray(b[28 + jl:28 + jl + bl]); N = J['nodes']
par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}; idx = {n.get('name'): i for i, n in enumerate(N)}
def acc(ai):
    a = J['accessors'][ai]; v = J['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
    assert a['componentType'] == 5126 and v.get('byteStride', 4 * k) == 4 * k
    return np.frombuffer(BIN, np.float32, a['count'] * k, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], k).astype(np.float64)
def q2m(q):
    x, y, z, w = q.T; return np.stack([np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], -1),
                                       np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], -1),
                                       np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], -1)], -2)
def m2q(R):
    R = R / np.linalg.norm(R, axis=-2, keepdims=True); out = []
    for m in R:
        t = np.trace(m)
        if t > 0: s = 2 * np.sqrt(t + 1); q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
        else:
            i = int(np.argmax(np.diag(m))); j, k = (i + 1) % 3, (i + 2) % 3; s = 2 * np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]); q = [0, 0, 0, 0]
            q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s; q[3] = (m[k, j] - m[j, k]) / s
        out.append(q)
    q = np.array(out)
    for i in range(1, len(q)):   # keep the sign continuous for the game's linear blend
        if q[i] @ q[i - 1] < 0: q[i] = -q[i]
    return q
def rot_between(a, b):   # per-frame minimal rotations taking a onto b (n x 3 -> n x 3 x 3)
    a = a / np.linalg.norm(a, axis=-1, keepdims=True); b = b / np.linalg.norm(b, axis=-1, keepdims=True)
    v = np.cross(a, b); c = np.sum(a * b, -1); K = np.zeros(a.shape[:-1] + (3, 3))
    K[..., 0, 1], K[..., 0, 2], K[..., 1, 0], K[..., 1, 2], K[..., 2, 0], K[..., 2, 1] = -v[..., 2], v[..., 1], v[..., 2], -v[..., 0], -v[..., 1], v[..., 0]
    return np.eye(3) + K + K @ K * (1 / (1 + np.maximum(c, -0.999)))[..., None, None]
def about(R, p):   # 4x4 rotations R about points p
    M = np.tile(np.eye(4), (len(R), 1, 1)); M[:, :3, :3] = R; M[:, :3, 3] = p - np.einsum('nij,nj->ni', R, p); return M
def static(n):
    M = np.eye(4)
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    M[:3, :3] = q2m(np.array([n.get('rotation', [0, 0, 0, 1])], float))[0] * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def rest_world(i):
    M = static(N[i])
    while i in par: i = par[i]; M = static(N[i]) @ M
    return M
def world_frames(an, nodes):   # posed world matrices of `nodes` (and their ancestors) on the clip's own time grid
    ch = {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
    T = acc(ch[(idx['mixamorig:RightArm'], 'rotation')]['input'])[:, 0]; nf = len(T); cache = {}
    def local(i):
        n = N[i]; M = np.tile(static(n), (nf, 1, 1)); tr = np.tile(np.array(n.get('translation', [0, 0, 0]), float), (nf, 1))
        r = np.tile(np.array(n.get('rotation', [0, 0, 0, 1]), float), (nf, 1)); s = np.tile(np.array(n.get('scale', [1, 1, 1]), float), (nf, 1))
        if 'matrix' in n: return M
        for path, arr in (('translation', tr), ('rotation', r), ('scale', s)):
            sm = ch.get((i, path))
            if sm is None: continue
            ti, v = acc(sm['input'])[:, 0], acc(sm['output']); arr[:] = np.stack([np.interp(T, ti, v[:, k]) for k in range(v.shape[1])], -1)
        r /= np.linalg.norm(r, axis=1, keepdims=True); M[:, :3, :3] = q2m(r) * s[:, None, :]; M[:, :3, 3] = tr; return M
    def world(i):
        if i not in cache: cache[i] = (world(par[i]) @ local(i)) if i in par else local(i)
        return cache[i]
    return T, {k: world(idx[k]) for k in nodes}
# the weapon's axis in the hand's rest frame: its vertices are in world rest coordinates under a node holding the hand's inverse rest
wn = idx['KNB_RightHand_weapon']; HR = rest_world(idx['mixamorig:RightHand'])
P = np.vstack([acc(p['attributes']['POSITION']) for p in J['meshes'][N[wn]['mesh']]['primitives']])
c = P.mean(0); ax = np.linalg.eigh(np.cov((P - c).T))[1][:, -1]; t = (P - c) @ ax; L = np.ptp(t)
perp = np.linalg.norm((P - c) - np.outer(t, ax), axis=1)
if perp[t > t.max() - 0.25 * L].max() < perp[t < t.min() + 0.25 * L].max(): ax = -ax   # toward the head (the wide end)
a_loc = np.linalg.inv(HR[:3, :3]) @ ax; a_loc /= np.linalg.norm(a_loc)
R0 = rest_world(idx['mixamorig:Hips'])[:3, :3]; up = np.array([0.0, 1.0, 0.0])
side_rest = rest_world(idx['mixamorig:RightArm'])[:3, 3] - rest_world(idx['mixamorig:LeftArm'])[:3, 3]; side_rest[1] = 0; side_rest /= np.linalg.norm(side_rest)
fwd_rest = np.cross(side_rest, up)   # Mixamo faces +z with its right hand at -x
if fwd_rest[2] < 0: fwd_rest = -fwd_rest
def smooth(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)
have = {a['name'] for a in J['animations']}
def add_acc(arr, typ):
    data = np.ascontiguousarray(arr, np.float32).tobytes()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['accessors'].append({'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}); return len(J['accessors']) - 1
ARM = ['mixamorig:RightShoulder', 'mixamorig:RightArm', 'mixamorig:RightForeArm', 'mixamorig:RightHand', 'mixamorig:Hips']
for name, base, k, amp in SPEC:
    assert name not in have, name + ' already in the file'
    an = next(a for a in J['animations'] if a['name'] == base)
    T, W = world_frames(an, ARM); nf = len(T); k = min(k, nf - 3)
    S, E0, H0 = (W[n][:, :3, 3] for n in ARM[1:4]); L1 = np.linalg.norm(E0[0] - S[0]); L2 = np.linalg.norm(H0[0] - E0[0])
    Rh = W['mixamorig:Hips'][:, :3, :3]; Rh = Rh / np.linalg.norm(Rh, axis=-2, keepdims=True); D = Rh @ np.linalg.inv(R0 / np.linalg.norm(R0, axis=0))
    F = D @ fwd_rest; F[:, 1] = 0; F /= np.linalg.norm(F, axis=1, keepdims=True); Rt = D @ side_rest; Rt[:, 1] = 0; Rt /= np.linalg.norm(Rt, axis=1, keepdims=True)
    f = np.arange(nf, dtype=float)
    w = smooth(f / max(2, k - 6)) * (1 - smooth((f - (k + 8)) / max(2, nf - 1 - (k + 8))))   # in over the wind-up, out after the draw-back
    s_ = np.where(f < k - 3, 0, np.where(f <= k, 1 - (1 - (f - (k - 3)) / 3) ** 2, np.where(f <= k + 2, 1, 1 - smooth((f - (k + 2)) / 6))))
    reach = (L1 + L2) * (0.55 + 0.40 * amp)
    Pc = S - F * 0.10 + Rt * 0.12 - up * 0.30
    Pe = S + F * reach + Rt * -0.05 - up * 0.08
    Tg = H0 + w[:, None] * ((Pc + s_[:, None] * (Pe - Pc)) - H0)
    v = Tg - S; d = np.clip(np.linalg.norm(v, axis=1), abs(L1 - L2) + 1e-3, 0.995 * (L1 + L2)); n_ = v / np.linalg.norm(v, axis=1, keepdims=True); Tg = S + n_ * d[:, None]
    pole0 = E0 - S; pole = pole0 / np.linalg.norm(pole0, axis=1, keepdims=True) * (1 - w[:, None]) + w[:, None] * (Rt * 0.6 - up * 0.8)
    m_ = pole - n_ * np.sum(pole * n_, 1, keepdims=True); m_ /= np.linalg.norm(m_, axis=1, keepdims=True)
    a_ = (L1 ** 2 - L2 ** 2 + d ** 2) / (2 * d); E = S + n_ * a_[:, None] + m_ * np.sqrt(np.maximum(L1 ** 2 - a_ ** 2, 0))[:, None]
    R1 = rot_between(E0 - S, E - S); M1 = about(R1, S)
    R2 = rot_between(np.einsum('nij,nj->ni', R1, H0 - E0), Tg - E); M2 = about(R2, E) @ M1
    Wh1 = M2 @ W['mixamorig:RightHand']; cur = np.einsum('nij,j->ni', Wh1[:, :3, :3], a_loc); cur /= np.linalg.norm(cur, axis=1, keepdims=True)
    aim = (S + F * 2.5 - up * 0.15) - Tg; aim /= np.linalg.norm(aim, axis=1, keepdims=True)
    want = cur * (1 - w[:, None]) + aim * w[:, None]
    Wh = about(rot_between(cur, want), Tg) @ Wh1
    Wu = M1 @ W['mixamorig:RightArm']; Wf = M2 @ W['mixamorig:RightForeArm']
    # checks: the hand reaches its target, the weapon points at the aim at full weight, the base pose is kept at zero weight
    assert np.abs(Wh[:, :3, 3] - Tg).max() < 1e-3, np.abs(Wh[:, :3, 3] - Tg).max()
    pt = np.einsum('nij,j->ni', Wh[:, :3, :3], a_loc); pt /= np.linalg.norm(pt, axis=1, keepdims=True)
    assert np.sum(pt[k] * aim[k]) > 0.99 and w[0] < 1e-6 and np.abs(Wh[0] - W['mixamorig:RightHand'][0]).max() < 1e-4
    loc = {'mixamorig:RightArm': np.linalg.inv(W['mixamorig:RightShoulder']) @ Wu, 'mixamorig:RightForeArm': np.linalg.inv(Wu) @ Wf,
           'mixamorig:RightHand': np.linalg.inv(Wf) @ Wh}
    new = {'name': name, 'channels': [], 'samplers': []}; tin = None
    for cc in an['channels']:
        sm = an['samplers'][cc['sampler']]; nm = N[cc['target']['node']].get('name')
        if cc['target']['path'] == 'rotation' and nm in loc:
            if tin is None: tin = add_acc(T[:, None], 'SCALAR'); J['accessors'][tin].update(min=[float(T.min())], max=[float(T.max())])
            sm = {'input': tin, 'output': add_acc(m2q(loc[nm][:, :3, :3]), 'VEC4'), 'interpolation': 'LINEAR'}
        new['samplers'].append(dict(sm)); new['channels'].append({'sampler': len(new['samplers']) - 1, 'target': dict(cc['target'])})
    J['animations'].append(new)
    print('%-18s from %-28s %2d frames, strike %2d, hand %.2f m ahead of the shoulder at the strike' % (name, base, nf, k, float((Tg[k] - S[k]) @ F[k])))
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN)
js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as fo:
    fo.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(BIN))); fo.write(struct.pack('<II', len(js), 0x4E4F534A)); fo.write(js)
    fo.write(struct.pack('<II', len(BIN), 0x004E4942)); fo.write(BIN)
print('wrote', OUT, len(J['animations']), 'clips')
