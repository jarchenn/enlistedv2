# K10: Knight 10's cloak, physics baked into its clips, at the glTF level (numpy; the body, parts and clips stay as they are).
#   python -I tools/enemies/k10_cape.py <knight glb> <cloak glb> <out.glb>
# The cape is the coordinator's preview cape (k3combo.py's "# cape: a grid of hanging points" block) run per clip:
# - a 13 x 9 grid of points fitted through the cloak (gaussian-weighted vertex averages over the cloak's u = x, v = height),
#   the top 2 rows pinned to Spine2; gravity, damping 0.975, 8 substeps at 30 fps, 5 constraint passes (vertical, skip-one
#   vertical, horizontal), pushed out of capsules on Hips-Spine2, Spine2-Neck, both upper / lower legs and upper arms (a point
#   never deeper than it rests in the cloak's own shape: the preview's radii put the shoulder mantle inside the neck capsule), the floor
#   at 3 cm (glTF: y up)
# - each clip starts settled: 1 s easing from the rest pose into its first pose, then 1 s held (a clip that starts with the torso
#   turned would otherwise drop the grid into the leg capsules and the push-out fling it); point speed capped at 8 m/s; a looping clip (idle / walk / run / strafe / crouching) is simulated twice
#   and its second pass blended over its last quarter into the first pass, so its last frame is its first
# - 5 x 7 cape joints ("cape_r<row>_c<col>", children of the Armature node) sit on the grid surface (position + the surface's
#   frame: u tangent, v tangent, normal), keyed at 30 fps in every clip; the cloak is skinned to them (bilinear in its u, v:
#   four joints a vertex), so it rides the simulated surface with its rest offset
# - the cloak's triangles are doubled with the reverse winding (the game culls back faces and reads no doubleSided); its
#   material keeps the base colour only (the source's normal / metal-rough maps sit on TEXCOORD_1 / 2, which the game does not
#   read): cloth, metallic 0, roughness 0.9; the gold cross is its own primitive, skinned the same way
import json, struct, sys
import numpy as np
SRC, CLOAK, OUT = sys.argv[1:4]
NC, NR, PINR, JC, JR, FPS, SUB, ITER, DAMP = 9, 13, 2, 5, 7, 30, 8, 5, 0.975
CAPS = [('Hips', 'Spine2', 0.21), ('Spine2', 'Neck', 0.20), ('LeftUpLeg', 'LeftLeg', 0.13), ('LeftLeg', 'LeftFoot', 0.10), ('RightUpLeg', 'RightLeg', 0.13),
        ('RightLeg', 'RightFoot', 0.10), ('LeftArm', 'LeftForeArm', 0.09), ('RightArm', 'RightForeArm', 0.09)]
LOOP = ('idle', 'walk', 'run', 'strafe', 'crouching')
def load(p):
    b = open(p, 'rb').read(); assert b[:4] == b'glTF'
    jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(SRC); Cj, CB = load(CLOAK)
NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}; CT = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
def read(G, B, i):
    a = G['accessors'][i]; v = G['bufferViews'][a['bufferView']]; n = NUM[a['type']]; dt = np.dtype(CT[a['componentType']])
    assert v.get('byteStride', dt.itemsize * n) == dt.itemsize * n
    return np.frombuffer(bytes(B), dt, count=a['count'] * n, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, n).copy()
def write(arr, typ, ctype=5126, target=None, mm=False):
    arr = np.ascontiguousarray(arr, CT[ctype])
    while len(BIN) % 4: BIN.append(0)
    v = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': arr.nbytes}
    if target: v['target'] = target
    J['bufferViews'].append(v); BIN.extend(arr.tobytes())
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': ctype, 'count': len(arr) if arr.ndim > 1 else arr.size, 'type': typ}
    if mm: a['min'] = [float(x) for x in arr.reshape(a['count'], -1).min(0)]; a['max'] = [float(x) for x in arr.reshape(a['count'], -1).max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
# ---------------------------------------------------------------- the knight's nodes, rest and posed
N = J['nodes']; par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}; idx = {n.get('name'): i for i, n in enumerate(N)}
def qmat(q):
    x, y, z, w = q; return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
def trs(t, r, s): M = np.eye(4); M[:3, :3] = qmat(r) * np.array(s); M[:3, 3] = t; return M
REST = {i: (np.array(n.get('translation', [0, 0, 0]), float), np.array(n.get('rotation', [0, 0, 0, 1]), float), np.array(n.get('scale', [1, 1, 1]), float)) for i, n in enumerate(N)}
order = []; seen = set()
def visit(i):
    if i in seen: return
    if i in par: visit(par[i])
    seen.add(i); order.append(i)
for i in range(len(N)): visit(i)
def worlds(local):   # node -> world, parents first
    W = {}
    for i in order:
        L = trs(*local[i]); W[i] = W[par[i]] @ L if i in par else L
    return W
W0 = worlds(REST)
def bone(n): return idx['mixamorig:' + n]
ARM = par[bone('Hips')]; WA = W0[ARM]; WAi = np.linalg.inv(WA); AS = float(np.cbrt(np.linalg.det(WA[:3, :3])))
# ---------------------------------------------------------------- the cloak in the knight's rest (glTF world) coordinates
cn = next(i for i, n in enumerate(Cj['nodes']) if 'mesh' in n)
def cworld(i):
    M = trs(*[np.array(Cj['nodes'][i].get(k, d), float) for k, d in (('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]), ('scale', [1, 1, 1]))])
    cp = {c: k for k, x in enumerate(Cj['nodes']) for c in x.get('children', [])}
    while i in cp: i = cp[i]; M = trs(*[np.array(Cj['nodes'][i].get(k, d), float) for k, d in (('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]), ('scale', [1, 1, 1]))]) @ M
    return M
CW = cworld(cn); prims = []
for pr in Cj['meshes'][Cj['nodes'][cn]['mesh']]['primitives']:
    P = read(Cj, CB, pr['attributes']['POSITION']).astype(float) @ CW[:3, :3].T + CW[:3, 3]
    Nn = read(Cj, CB, pr['attributes']['NORMAL']).astype(float) @ np.linalg.inv(CW[:3, :3]).T; Nn /= np.linalg.norm(Nn, axis=1)[:, None] + 1e-12
    UV = read(Cj, CB, pr['attributes']['TEXCOORD_0']); I = read(Cj, CB, pr['indices']).ravel().astype(np.uint32)
    prims.append(dict(P=P, N=Nn, UV=UV, I=I, mat=pr.get('material')))
cl = max(prims, key=lambda q: len(q['P']))   # the cloak itself (the cross is a small plate on it)
V0 = cl['P']; x0, x1 = V0[:, 0].min(), V0[:, 0].max(); yt, yb = V0[:, 1].max(), V0[:, 1].min()
def uv_of(P): return np.clip((P[:, 0] - x0) / (x1 - x0), 0, 1), np.clip((yt - P[:, 1]) / (yt - yb), 0, 1)
U, Vv = uv_of(V0)
gu, gv = np.linspace(0, 1, NC), np.linspace(0, 1, NR); G0 = np.zeros((NR, NC, 3))
for r in range(NR):
    for c in range(NC):
        w = np.exp(-((U - gu[c]) ** 2 / (2 * 0.07 ** 2) + (Vv - gv[r]) ** 2 / (2 * 0.05 ** 2))); G0[r, c] = (V0 * w[:, None]).sum(0) / w.sum()
def interp(G, u, v):
    fu = np.clip(u * (NC - 1), 0, NC - 1.001); fv = np.clip(v * (NR - 1), 0, NR - 1.001); c0 = fu.astype(int); r0 = fv.astype(int); a = (fu - c0)[:, None]; b = (fv - r0)[:, None]
    return (G[r0, c0] * (1 - a) + G[r0, c0 + 1] * a) * (1 - b) + (G[r0 + 1, c0] * (1 - a) + G[r0 + 1, c0 + 1] * a) * b
def frames(G, u, v):   # columns: u tangent, v tangent (in-surface), normal
    e = 0.04; tu = interp(G, np.clip(u + e, 0, 1), v) - interp(G, np.clip(u - e, 0, 1), v); tv = interp(G, u, np.clip(v + e, 0, 1)) - interp(G, u, np.clip(v - e, 0, 1))
    tu /= np.linalg.norm(tu, axis=1)[:, None] + 1e-9; n = np.cross(tu, tv); n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9; return np.stack([tu, np.cross(n, tu), n], axis=2)
JU = np.tile(np.linspace(0, 1, JC), JR); JV = np.repeat(np.linspace(0, 1, JR), JC)   # joint k = r * JC + c
def jointMats(G):   # world scale 1 / the Armature's (0.01): unit local scale under it, like the Mixamo bones (an importer may mishandle
    # a 100x bone scale); the inverse binds carry the same scale, so the skinning is unchanged
    S = interp(G, JU, JV); B = frames(G, JU, JV); M = np.zeros((len(JU), 4, 4)); M[:, :3, :3] = B * AS; M[:, :3, 3] = S; M[:, 3, 3] = 1; return M
Lv = np.linalg.norm(G0[1:] - G0[:-1], axis=2); Lh = np.linalg.norm(G0[:, 1:] - G0[:, :-1], axis=2); Lv2 = np.linalg.norm(G0[2:] - G0[:-2], axis=2)
SP2 = bone('Spine2'); PINB = np.linalg.inv(W0[SP2])
def segdist(F, a, b):
    ab = b - a; t = np.clip(((F - a) @ ab) / (ab @ ab + 1e-9), 0, 1); return np.linalg.norm(F - (a + t[:, None] * ab), axis=1)
# a point's radius per capsule: the capsule's, or less where the cloak itself rests inside it (its shoulder mantle lies inside
# the Spine2-Neck capsule: pushed out, it folded the cape up over the shoulders)
RK = np.array([np.minimum(r, np.maximum(0.0, segdist(G0.reshape(-1, 3), W0[bone(a)][:3, 3], W0[bone(b)][:3, 3]) - 0.005)) for a, b, r in CAPS])   # capsule, point
VMAX = 8.0   # m/s: a cap on a point's speed (a push out of a capsule must not fling the cloth)
def pose_of(Wt): return Wt[SP2] @ PINB, [(Wt[bone(a)][:3, 3], Wt[bone(b)][:3, 3], r) for a, b, r in CAPS]
def sim_step(P, Q, pz):
    M, caps = pz; pin = (G0[:PINR].reshape(-1, 3) @ M[:3, :3].T + M[:3, 3]).reshape(PINR, NC, 3); dt = 1.0 / FPS / SUB
    for _ in range(SUB):
        v = (P - Q) * DAMP; sp = np.linalg.norm(v, axis=2, keepdims=True); v *= np.minimum(1.0, VMAX * dt / (sp + 1e-12))
        Nw = P + v + np.array((0, -9.8, 0)) * dt * dt; Q = P; P = Nw; P[:PINR] = pin
        for _ in range(ITER):
            for sa, sb, L in ((np.s_[:-1], np.s_[1:], Lv), (np.s_[:-2], np.s_[2:], Lv2)):
                d = P[sb] - P[sa]; l = np.linalg.norm(d, axis=2) + 1e-9; c = (d * ((l - L) / l)[..., None]) * 0.5; P[sa] += c; P[sb] -= c
            d = P[:, 1:] - P[:, :-1]; l = np.linalg.norm(d, axis=2) + 1e-9; c = (d * ((l - Lh) / l)[..., None]) * 0.5; P[:, :-1] += c; P[:, 1:] -= c
            P[:PINR] = pin; F = P.reshape(-1, 3)
            for ci, (a2, b2, r2) in enumerate(caps):
                r2 = RK[ci]; ab = b2 - a2; t = np.clip(((F - a2) @ ab) / (ab @ ab + 1e-9), 0, 1); cp = a2 + t[:, None] * ab; d = F - cp; l = np.linalg.norm(d, axis=1) + 1e-9; m = l < r2
                F[m] = cp[m] + d[m] / l[m, None] * r2[m, None]
            F[:, 1] = np.maximum(F[:, 1], 0.03); P = F.reshape(NR, NC, 3); P[:PINR] = pin
    return P, Q
# ---------------------------------------------------------------- clip sampling
def sample(an, t):
    loc = {i: (REST[i][0].copy(), REST[i][1].copy(), REST[i][2].copy()) for i in REST}
    for c in an['channels']:
        s = an['samplers'][c['sampler']]; ti = SAMP[(id(an), c['sampler'])]; T, O = ti
        k = {'translation': 0, 'rotation': 1, 'scale': 2}[c['target']['path']]
        if s.get('interpolation') == 'STEP' or len(T) == 1: v = O[max(0, np.searchsorted(T, t, side='right') - 1)]
        else:
            j = int(np.clip(np.searchsorted(T, t) - 1, 0, len(T) - 2)); a = float(np.clip((t - T[j]) / max(T[j + 1] - T[j], 1e-9), 0, 1)); o0, o1 = O[j], O[j + 1]
            if k == 1 and o0 @ o1 < 0: o1 = -o1
            v = o0 * (1 - a) + o1 * a
            if k == 1: v = v / np.linalg.norm(v)
        loc[c['target']['node']] = tuple(v if q == k else loc[c['target']['node']][q] for q in range(3))
    return worlds(loc)
SAMP = {}
for an in J['animations']:
    for si, s in enumerate(an['samplers']): SAMP[(id(an), si)] = (read(J, BIN, s['input']).ravel().astype(float), read(J, BIN, s['output']).astype(float))
def rot2q(R):
    t = np.trace(R)
    if t > 0: s = np.sqrt(t + 1) * 2; q = [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s]
    else:
        i = int(np.argmax(np.diag(R))); j, k = (i + 1) % 3, (i + 2) % 3; s = np.sqrt(1 + R[i, i] - R[j, j] - R[k, k]) * 2
        q = [0, 0, 0, 0]; q[i] = 0.25 * s; q[j] = (R[j, i] + R[i, j]) / s; q[k] = (R[k, i] + R[i, k]) / s; q[3] = (R[k, j] - R[j, k]) / s
    q = np.array(q); return q / np.linalg.norm(q)
def toLocal(M):   # joint world -> TRS under the Armature node
    L = WAi @ M; sc = np.linalg.norm(L[:3, :3], axis=0); return L[:3, 3], rot2q(L[:3, :3] / sc), sc
# ---------------------------------------------------------------- the cape joints (rest: the fitted grid)
M0 = jointMats(G0); jn0 = len(N); names = []
for k in range(len(M0)):
    t, q, s = toLocal(M0[k]); nm = 'cape_r%d_c%d' % (k // JC, k % JC); names.append(nm)
    N.append({'name': nm, 'translation': [float(x) for x in t], 'rotation': [float(x) for x in q], 'scale': [float(x) for x in s]})
N[ARM].setdefault('children', []).extend(range(jn0, jn0 + len(M0)))
skin = J['skins'][0]; j0 = len(skin['joints']); skin['joints'].extend(range(jn0, jn0 + len(M0)))
IB = read(J, BIN, skin['inverseBindMatrices']).reshape(-1, 4, 4)
IBn = np.concatenate([IB, np.linalg.inv(M0).transpose(0, 2, 1).astype(np.float32)]); skin['inverseBindMatrices'] = write(IBn.reshape(-1, 16), 'MAT4')
# ---------------------------------------------------------------- bake every clip
stats = []
for an in J['animations']:
    tmin = min(SAMP[(id(an), si)][0].min() for si in range(len(an['samplers']))); tmax = max(SAMP[(id(an), si)][0].max() for si in range(len(an['samplers'])))
    times = np.arange(tmin, tmax + 1e-6, 1.0 / FPS); n = len(times); Ws = [sample(an, t) for t in times]
    P = G0.copy(); Q = P.copy(); (M1, C1), (M0_, C0_) = pose_of(Ws[0]), pose_of(W0)
    q1 = rot2q(M1[:3, :3])
    for k in range(2 * FPS):   # settled: 1 s easing from the rest pose into the first pose (pin slerped, capsules lerped), 1 s held
        a = min(1.0, k / FPS); a = a * a * (3 - 2 * a); qa = np.array([0, 0, 0, 1.0]) * (1 - a) + q1 * a if q1[3] >= 0 else np.array([0, 0, 0, 1.0]) * (1 - a) - q1 * a
        Ma = np.eye(4); Ma[:3, :3] = qmat(qa / np.linalg.norm(qa)); Ma[:3, 3] = M1[:3, 3] * a
        P, Q = sim_step(P, Q, (Ma, [(p0 * (1 - a) + p1 * a, e0 * (1 - a) + e1 * a, r) for (p0, e0, r), (p1, e1, _) in zip(C0_, C1)]))
    loop = any(w in an['name'].lower() for w in LOOP); passes = []
    for _ in range(2 if loop else 1):
        out = []
        for W in Ws: P, Q = sim_step(P, Q, pose_of(W)); out.append(P.copy())
        passes.append(out)
    F = passes[-1]
    if loop:   # the second pass blends into the first over its last quarter: its last frame is the first pass's last = its own start
        k = max(3, n // 4)
        F = [P2 if i < n - k else P2 * (1 - (i - (n - k - 1)) / k) + passes[0][i] * ((i - (n - k - 1)) / k) for i, P2 in enumerate(F)]
    JM = np.array([jointMats(G) for G in F])   # frame, joint, 4, 4
    tt = write(times.reshape(-1, 1).astype(np.float32), 'SCALAR', mm=True)
    for k in range(len(M0)):
        T_, R_, S_ = zip(*[toLocal(JM[f, k]) for f in range(n)]); R_ = np.array(R_)
        for f in range(1, n):
            if R_[f] @ R_[f - 1] < 0: R_[f] = -R_[f]
        for path, arr, typ in (('translation', np.array(T_), 'VEC3'), ('rotation', R_, 'VEC4'), ('scale', np.array(S_), 'VEC3')):
            an['samplers'].append({'input': tt, 'output': write(arr.astype(np.float32), typ), 'interpolation': 'LINEAR'})
            an['channels'].append({'sampler': len(an['samplers']) - 1, 'target': {'node': jn0 + k, 'path': path}})
    sw = float(np.abs(np.array(F)[:, -1, :, :] - np.array(F)[:, -1, :, :].mean(0)).max())
    stats.append((an['name'], n, loop, round(sw, 3), round(float(np.abs(F[-1] - F[0]).max()), 3)))
# ---------------------------------------------------------------- the cloak mesh, skinned, both sides
ib0 = len(J.get('images', [])); tx0 = len(J.get('textures', [])); sm0 = len(J.get('samplers', [])); mt0 = len(J['materials'])
def copy_view(i):
    v = Cj['bufferViews'][i]; data = CB[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data); return len(J['bufferViews']) - 1
J.setdefault('samplers', []).extend(Cj.get('samplers', [])); imap = {}
mats = []
for mi, m in enumerate(Cj['materials']):
    pbr = m.get('pbrMetallicRoughness', {}); nm = {'name': 'K10_' + m.get('name', 'cloak'), 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': pbr.get('roughnessFactor', 0.9) if 'baseColorTexture' not in pbr else 0.9}}
    if 'baseColorFactor' in pbr: nm['pbrMetallicRoughness']['baseColorFactor'] = pbr['baseColorFactor']
    if 'baseColorTexture' in pbr:
        t = Cj['textures'][pbr['baseColorTexture']['index']]; src = t['source']
        if src not in imap:
            im = dict(Cj['images'][src]); im['bufferView'] = copy_view(im['bufferView']); im['name'] = 'K10_' + im.get('name', 'cloak'); J.setdefault('images', []).append(im); imap[src] = len(J['images']) - 1
        tex = {'source': imap[src]}
        if 'sampler' in t: tex['sampler'] = t['sampler'] + sm0
        J.setdefault('textures', []).append(tex); nm['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(J['textures']) - 1}
    J['materials'].append(nm); mats.append(len(J['materials']) - 1)
fu = lambda u: np.clip(u * (JC - 1), 0, JC - 1.001); fv = lambda v: np.clip(v * (JR - 1), 0, JR - 1.001)
mesh = {'name': 'K10_cloak', 'primitives': []}; nverts = 0
for q in prims:
    u, v = uv_of(q['P']); a = fu(u); b = fv(v); c0 = a.astype(int); r0 = b.astype(int); a -= c0; b -= r0
    JO = np.stack([r0 * JC + c0, r0 * JC + c0 + 1, (r0 + 1) * JC + c0, (r0 + 1) * JC + c0 + 1], 1) + j0
    WE = np.stack([(1 - a) * (1 - b), a * (1 - b), (1 - a) * b, a * b], 1)
    nv = len(q['P']); P2 = np.vstack([q['P'], q['P']]); N2 = np.vstack([q['N'], -q['N']]); UV2 = np.vstack([q['UV'], q['UV']])
    I3 = q['I'].reshape(-1, 3); I2 = np.concatenate([I3, I3[:, ::-1] + nv]).ravel().astype(np.uint32)
    assert JO.max() < 256
    mesh['primitives'].append({'attributes': {'POSITION': write(P2, 'VEC3', target=34962, mm=True), 'NORMAL': write(N2, 'VEC3', target=34962), 'TEXCOORD_0': write(UV2.astype(np.float32), 'VEC2', target=34962),
        'JOINTS_0': write(np.vstack([JO, JO]).astype(np.uint8), 'VEC4', 5121, target=34962), 'WEIGHTS_0': write(np.vstack([WE, WE]), 'VEC4', target=34962)},
        'indices': write(I2, 'SCALAR', 5125, target=34963), 'material': mats[q['mat']] if q['mat'] is not None else None})
    if mesh['primitives'][-1]['material'] is None: del mesh['primitives'][-1]['material']
    nverts += 2 * nv
J['meshes'].append(mesh); N.append({'name': 'K10_cloak', 'mesh': len(J['meshes']) - 1, 'skin': 0}); N[ARM]['children'].append(len(N) - 1)
# ---------------------------------------------------------------- write
J['buffers'] = [{'byteLength': len(BIN)}]
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('cape: %d joints (skin %d -> %d), cloak %d vertices (both sides) in %d primitives; %d clips baked' % (len(M0), j0, len(skin['joints']), nverts, len(prims), len(stats)))
for s in stats: print('  %-36s %3d frames loop %-5s hem sway %.3f m, end-start %.3f m' % s)
print('wrote %s (%d KB)' % (OUT, (28 + len(js) + len(BIN)) // 1024))
