# KD1 (user: "for knights vary death animations"): death clips from a Mixamo-rigged model added to another Mixamo-rigged model,
# keeping every clip it has. retarget_clips.py's world-turn transfer (a joint's turn from its bind pose, src -> dst), with each
# joint's bind direction (toward its child) first lined up with the source's, so a dst bound in an A-pose (the Samurai) takes
# a T-pose source's clips without its arms dropping. Raw (mixamorig:) and converted (b_mixamorig) names both match.
#   python -I tools/enemies/death_add.py <src.glb> <dst.glb> <out.glb> "<clip>=<new name>" ...
import json, struct, sys, re, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, 'retarget_clips.py')).read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
load, Rig, q2m, m2q = g['load'], g['Rig'], g['q2m'], g['m2q']
def key(n): return re.sub(r'^(b_mixamorig\d*_|mixamorig\d*:)', '', n or '')
def unit(M): return M / np.linalg.norm(M, axis=-2, keepdims=True)
def rot_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b); c = np.cross(a, b); s = np.linalg.norm(c); d = float(a @ b)
    if s < 1e-9: return np.eye(3)
    k = c / s; K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]]); return np.eye(3) + s * K + (1 - d) * K @ K
SRC, DST, OUT = sys.argv[1:4]; PAIRS = [a.split('=', 1) for a in sys.argv[4:]]
S, D = Rig(*load(SRC)), Rig(*load(DST))
S.byKey = {key(S.N[i].get('name')): i for i in S.joints}; D.byKey = {key(D.N[i].get('name')): i for i in D.joints}
first_child = lambda R, i: next((c for c in R.N[i].get('children', []) if c in R.joints), None)
order = sorted(D.joints, key=lambda i: (lambda f: f(f, i))(lambda f, j: 0 if j not in D.par else 1 + f(f, D.par[j])))
# bind alignment per joint: dst bind direction (to its first child) turned onto the source's
Q = {}
for i in order:
    kk = key(D.N[i].get('name')); c = first_child(D, i)
    if kk in S.byKey and c is not None and key(D.N[c].get('name')) in S.byKey:
        si = S.byKey[kk]; sc = S.byKey[key(D.N[c].get('name'))]
        dd = D.rest(c)[:3, 3] - D.rest(i)[:3, 3]; ds = S.rest(sc)[:3, 3] - S.rest(si)[:3, 3]
        Q[i] = rot_between(dd, ds) if np.linalg.norm(dd) > 1e-6 and np.linalg.norm(ds) > 1e-6 else np.eye(3)
    else: Q[i] = np.eye(3)
hs, hd = S.byKey['Hips'], D.byKey['Hips']; k = D.rest(hd)[1, 3] / S.rest(hs)[1, 3]
anims = []
for name, new_name in PAIRS:
    an = next(a for a in S.J['animations'] if a['name'] == name); T, W = S.posed(an); nf = len(T); new = {}; Rw = {}
    for i in order:
        kk = key(D.N[i].get('name')); p = D.par.get(i)
        Rp = Rw[p] if p in Rw else np.tile(unit(D.rest(p)[:3, :3]) if p is not None else np.eye(3), (nf, 1, 1))
        if kk in S.byKey:
            si = S.byKey[kk]; Rw[i] = unit(W(si)[:, :3, :3]) @ np.linalg.inv(unit(S.rest(si)[:3, :3])) @ Q[i] @ unit(D.rest(i)[:3, :3])
            new[i] = {'rotation': m2q(np.linalg.inv(Rp) @ Rw[i])}
        else: Rw[i] = Rp @ q2m(np.array(D.N[i].get('rotation', [0, 0, 0, 1]), float))
    Pw = D.rest(D.par[hd]) if hd in D.par else np.eye(4)
    pos = D.rest(hd)[:3, 3] + k * (W(hs)[:, :3, 3] - S.rest(hs)[:3, 3])
    new[hd]['translation'] = (np.linalg.inv(Pw) @ np.c_[pos, np.ones(nf)].T).T[:, :3]
    anims.append((new_name, T, new))
# joints of the dst the source lacks but the dst's own clips move (the Samurai's Katana, a root-level joint keyed to follow his hand):
# held where they sit against the right hand at the start of the dst's reference clip (env HELD_REF, default its first death)
ref = os.environ.get('HELD_REF') or next((a['name'] for a in D.J['animations'] if 'death' in a['name'].lower()), None)
held = []
if ref:
    ra = next(a for a in D.J['animations'] if a['name'] == ref); keyed = {c['target']['node'] for c in ra['channels']}
    def anchored(i):   # an ancestor the source animates (other than the hips): it follows that, rest-local (capes, hand weapons)
        j = D.par.get(i)
        while j is not None:
            if j in D.joints and key(D.N[j].get('name')) in S.byKey and key(D.N[j].get('name')) != 'Hips': return True
            j = D.par.get(j)
        return False
    held = [i for i in D.joints if key(D.N[i].get('name')) not in S.byKey and i in keyed and not anchored(i)]
if held:
    Tr, Wr = D.posed(ra); matched = [j for j in D.joints if key(D.N[j].get('name')) in S.byKey]
    def top(i):
        while D.par.get(i) in held: i = D.par[i]
        return i
    anchor = {}
    for i in held:   # a chain (the cape's rows, the katana) rides on the body joint nearest its top at the reference's start
        t_ = top(i)
        if t_ not in anchor:   # nearest a hand or finger: that (a weapon); else the trunk (a cape hangs from the back, not an arm)
            p0 = Wr(t_)[0, :3, 3]; trunk = [D.byKey[h] for h in ('Spine', 'Spine1', 'Spine2', 'Neck', 'Head') if h in D.byKey]
            near = lambda js: min(js, key=lambda j: np.linalg.norm(Wr(j)[0, :3, 3] - p0)); hn = near(matched)
            anchor[t_] = hn if re.match(r'(Right|Left)Hand', key(D.N[hn].get('name'))) else near(trunk)
        anchor[i] = anchor[t_]
    off = {i: np.linalg.inv(Wr(anchor[i])[0]) @ Wr(i)[0] for i in held}
    for name, T, new in anims:
        nf = len(T); cache = {}
        def wl(i):
            if i in cache: return cache[i]
            M = np.tile(D.static(i), (nf, 1, 1)); n = D.N[i]
            if i in new and 'rotation' in new[i]: M[:, :3, :3] = q2m(new[i]['rotation']) * np.array(n.get('scale', [1, 1, 1]))
            if i in new and 'translation' in new[i]: M[:, :3, 3] = new[i]['translation']
            cache[i] = wl(D.par[i]) @ M if i in D.par else M; return cache[i]
        for i in held:
            Wi = wl(anchor[i]) @ off[i]; Pw_ = wl(D.par[i]) if i in D.par else np.tile(np.eye(4), (nf, 1, 1)); L = np.linalg.inv(Pw_) @ Wi
            sc = np.linalg.norm(L[:, :3, :3], axis=-2); new[i] = {'rotation': m2q(L[:, :3, :3] / sc[:, None, :]), 'translation': L[:, :3, 3]}
    print('held:', sorted({(D.N[top(i)]['name'], D.N[anchor[i]]['name']) for i in held}), '(from', ref + ')')
# write: the dst's own clips kept, these appended (a clip of the same name replaced)
J, BIN = D.J, D.BIN
def add(arr, typ):
    data = np.ascontiguousarray(arr, np.float32).tobytes()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['accessors'].append({'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}); return len(J['accessors']) - 1
names = {n for n, _, _ in anims}; J['animations'] = [a for a in J.get('animations', []) if a['name'] not in names]
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
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote %s: +%d death clips (%s), hips x%.3f' % (OUT, len(anims), ', '.join(n for n, _, _ in anims), k))
