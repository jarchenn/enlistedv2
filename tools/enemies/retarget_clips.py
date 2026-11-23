# NPC2: every clip of one converted Mixamo character (src) onto another (dst) whose GLB came without clips, at the glTF level.
# Both are Mixamo rigs converted by convert_enemy_glb.py (mapped joints under the game's semantic names, the rest
# "b_mixamorig<N>_<bone>": matched with the prefix taken off). Both bind poses are Mixamo's T-pose, so a joint's turn away
# from its bind pose in world space is the same on both: dst world rotation = (src posed x src rest^-1) x dst rest, then
# taken into the parent's new world rotation for the local key. The hips also move: the src hips' travel from rest in
# world space, scaled by the dst / src hips height, onto the dst rest hips. Other translations and scales stay at rest.
#   python -I tools/enemies/retarget_clips.py <src.glb> <dst.glb> <out.glb>
#   python -I tools/enemies/retarget_clips.py --check <src.glb>   (src onto itself: every joint where it was, within 0.1 mm)
import json, struct, sys, re
import numpy as np
def load(p):
    b = open(p, 'rb').read(); assert b[:4] == b'glTF'
    jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return J, bytearray(b[28 + jl:28 + jl + bl])
def key(n): return re.sub(r'^(b_mixamorig\d*_|mixamorig\d*:)', '', n or '')   # (a raw Mixamo GLB's joints too: the knight's)
def q2m(q):
    x, y, z, w = np.moveaxis(q, -1, 0); return np.stack([np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], -1),
                                                    np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], -1),
                                                    np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], -1)], -2)
def m2q(R):
    out = []
    for m in R:
        t = np.trace(m)
        if t > 0: s = 2 * np.sqrt(t + 1); q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
        else:
            i = int(np.argmax(np.diag(m))); j, k = (i + 1) % 3, (i + 2) % 3; s = 2 * np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]); q = [0, 0, 0, 0]
            q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s; q[3] = (m[k, j] - m[j, k]) / s
        out.append(q)
    q = np.array(out); q /= np.linalg.norm(q, axis=1, keepdims=True)
    for i in range(1, len(q)):
        if q[i] @ q[i - 1] < 0: q[i] = -q[i]
    return q
def unit(M): return M / np.linalg.norm(M, axis=-2, keepdims=True)   # rotation part (columns normalised)
class Rig:
    def __init__(s, J, BIN):
        s.J, s.BIN, s.N = J, BIN, J['nodes']; s.par = {c: i for i, n in enumerate(s.N) for c in n.get('children', [])}
        s.joints = J['skins'][0]['joints']; s.byKey = {key(s.N[i].get('name')): i for i in s.joints}
    def acc(s, ai):
        a = s.J['accessors'][ai]; v = s.J['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
        return np.frombuffer(s.BIN, np.float32, a['count'] * k, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], k).astype(np.float64)
    def static(s, i):
        n = s.N[i]
        if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
        M = np.eye(4); M[:3, :3] = q2m(np.array(n.get('rotation', [0, 0, 0, 1]), float)) * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
    def rest(s, i):
        M = s.static(i)
        while i in s.par: i = s.par[i]; M = s.static(i) @ M
        return M
    def posed(s, an):   # world matrices of every node on the joints' paths, per frame of the clip's own grid
        ch = {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
        T = s.acc(next(iter(ch.values()))['input'])[:, 0]; nf = len(T); cache = {}
        def local(i):
            n = s.N[i]
            if 'matrix' in n: return np.tile(s.static(i), (nf, 1, 1))
            tr = np.tile(np.array(n.get('translation', [0, 0, 0]), float), (nf, 1)); r = np.tile(np.array(n.get('rotation', [0, 0, 0, 1]), float), (nf, 1)); sc = np.tile(np.array(n.get('scale', [1, 1, 1]), float), (nf, 1))
            for p, arr in (('translation', tr), ('rotation', r), ('scale', sc)):
                if (i, p) in ch:
                    ti, v = s.acc(ch[(i, p)]['input'])[:, 0], s.acc(ch[(i, p)]['output']); arr[:] = np.stack([np.interp(T, ti, v[:, k]) for k in range(v.shape[1])], -1)
            r /= np.linalg.norm(r, axis=1, keepdims=True); M = np.tile(np.eye(4), (nf, 1, 1)); M[:, :3, :3] = q2m(r) * sc[:, None, :]; M[:, :3, 3] = tr; return M
        def world(i):
            if i not in cache: cache[i] = world(s.par[i]) @ local(i) if i in s.par else local(i)
            return cache[i]
        return T, world
def retarget(S, D):
    hs, hd = S.byKey['Hips'], D.byKey['Hips']
    k = D.rest(hd)[1, 3] / S.rest(hs)[1, 3]   # hips heights (y up)
    order = [i for i in D.joints]; depth = lambda i: 0 if i not in D.par else 1 + depth(D.par[i]); order.sort(key=depth)
    anims = []
    for an in S.J.get('animations', []):
        T, W = S.posed(an); nf = len(T); new = {}; Rw = {}
        for i in order:
            kk = key(D.N[i].get('name')); p = D.par.get(i)
            Rp = Rw[p] if p in Rw else np.tile(unit(D.rest(p)[:3, :3]) if p is not None else np.eye(3), (nf, 1, 1))
            if kk in S.byKey:
                si = S.byKey[kk]; Ws = unit(W(si)[:, :3, :3]); Rs0 = unit(S.rest(si)[:3, :3])
                Rw[i] = Ws @ np.linalg.inv(Rs0) @ unit(D.rest(i)[:3, :3])
                new[i] = {'rotation': m2q(np.linalg.inv(Rp) @ Rw[i])}
            else: Rw[i] = Rp @ q2m(np.array(D.N[i].get('rotation', [0, 0, 0, 1]), float))
        Pw = D.rest(D.par[hd]) if hd in D.par else np.eye(4)
        pos = D.rest(hd)[:3, 3] + k * (W(hs)[:, :3, 3] - S.rest(hs)[:3, 3])
        new[hd]['translation'] = (np.linalg.inv(Pw) @ np.c_[pos, np.ones(nf)].T).T[:, :3]
        anims.append((an['name'], T, new))
    return anims
def write(D, anims, out):
    J, BIN = D.J, D.BIN
    def add(arr, typ):
        data = np.ascontiguousarray(arr, np.float32).tobytes()
        while len(BIN) % 4: BIN.append(0)
        J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
        J['accessors'].append({'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}); return len(J['accessors']) - 1
    J['animations'] = []
    for name, T, new in anims:
        ti = add(T[:, None], 'SCALAR'); J['accessors'][ti].update(min=[float(T.min())], max=[float(T.max())]); a = {'name': name, 'channels': [], 'samplers': []}
        for i, d in new.items():
            for p, v in d.items():
                a['samplers'].append({'input': ti, 'output': add(v, 'VEC4' if p == 'rotation' else 'VEC3'), 'interpolation': 'LINEAR'})
                a['channels'].append({'sampler': len(a['samplers']) - 1, 'target': {'node': i, 'path': p}})
        J['animations'].append(a)
    while len(BIN) % 4: BIN.append(0)
    J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
    while len(js) % 4: js += b' '
    with open(out, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
if sys.argv[1] == '--check':
    S = Rig(*load(sys.argv[2])); D = Rig(*load(sys.argv[2])); anims = retarget(S, D); worst = 0
    for name, T, new in anims:
        _, W = S.posed(next(a for a in S.J['animations'] if a['name'] == name)); h = S.byKey['LeftHand']
        Rq = {i2: q2m(d['rotation']) for i2, d in new.items()}
        def wd(j):
            n = D.N[j]; M = np.tile(D.static(j), (len(T), 1, 1))
            if j in Rq: M[:, :3, :3] = Rq[j] * np.array(n.get('scale', [1, 1, 1]))
            if 'translation' in new.get(j, {}): M[:, :3, 3] = new[j]['translation']
            return wd(D.par[j]) @ M if j in D.par else M
        worst = max(worst, float(np.abs(wd(h)[:, :3, 3] - W(h)[:, :3, 3]).max()))
    print('self-retarget worst joint error %.6f m over %d clips' % (worst, len(anims))); assert worst < 1e-4
else:
    S = Rig(*load(sys.argv[1])); D = Rig(*load(sys.argv[2])); anims = retarget(S, D); write(D, anims, sys.argv[3])
    print('wrote', sys.argv[3], len(anims), 'clips, hips x%.3f' % (D.rest(D.byKey['Hips'])[1, 3] / S.rest(S.byKey['Hips'])[1, 3]))
