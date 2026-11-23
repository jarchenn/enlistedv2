# SAM1: the samurai GLB (a Mixamo-named skeleton in an A-pose rest, a skinned katana on a root-level "Katana" bone whose
# world transform is baked into each of its six great-sword clips) -> the game's enemy GLB, at the glTF level:
#   - the clips it lacks (run, walk, hit reactions, deaths) come from the great-sword knight (knight_8.glb: the same Mixamo
#     Great Sword Pack), retargeted joint by joint. retarget_clips.py assumes two T-pose rests; this rest is an A-pose, so each
#     limb joint first gets the turn that lays its rest bone along the knight's (the arms up to the T-pose), inherited by the
#     joints below it: world rotation = (knight posed x knight rest^-1) x that turn x samurai rest
#   - in the new clips the katana is carried by the right hand as it sits there in the first key of its own idle clip
#   - root motion: every clip but the deaths has the hips' floor position pinned to its first key (the AI moves the body);
#     the katana, a root-level bone, moves with the hips
#   - asset.extras.nmkNormals: the game loads the normal maps
#   python -I tools/enemies/samurai_build.py <Samurai_katana.glb> <knight_8.glb> <out.glb>
import json, struct, sys, os
import numpy as np
SRC, KN, OUT = sys.argv[1:4]
_rc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'retarget_clips.py')).read().split("if sys.argv[1] == '--check'")[0]; R = {}; exec(_rc, R)   # its Rig / load / q2m / m2q / unit / write
Rig, load, q2m, m2q, unit, key = R['Rig'], R['load'], R['q2m'], R['m2q'], R['unit'], R['key']
TAKE = {'great sword run (2)': 'great sword run', 'great sword walk': 'great sword walk', 'great sword impact': 'great sword impact', 'great sword impact (2)': 'great sword impact (2)',
        'great sword impact (3)': 'great sword impact (3)', 'two handed sword death': 'two handed sword death', 'two handed sword death (2)': 'two handed sword death (2)'}
CHAIN = {'Hips': 'Spine', 'Spine': 'Spine1', 'Spine1': 'Spine2', 'Spine2': 'Neck', 'Neck': 'Head'}
for s_ in ('Left', 'Right'):
    CHAIN.update({s_ + 'Shoulder': s_ + 'Arm', s_ + 'Arm': s_ + 'ForeArm', s_ + 'ForeArm': s_ + 'Hand', s_ + 'UpLeg': s_ + 'Leg', s_ + 'Leg': s_ + 'Foot', s_ + 'Foot': s_ + 'ToeBase'})
D = Rig(*load(SRC)); S = Rig(*load(KN)); N = D.N
def between(a, b):   # the rotation matrix taking direction a onto b
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b); v = np.cross(a, b); c = float(a @ b); s = np.linalg.norm(v)
    if s < 1e-8: return np.eye(3)
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]]); return np.eye(3) + K + K @ K * ((1 - c) / (s * s))
order = sorted(D.joints, key=lambda i: (lambda f: f(f, i))(lambda f, i: 0 if i not in D.par else 1 + f(f, D.par[i])))
Q = {}
for i in order:
    k = key(N[i].get('name')); p = D.par.get(i); Q[i] = Q.get(p, np.eye(3))
    c = CHAIN.get(k)
    if c and c in D.byKey and k in S.byKey and c in S.byKey:
        dd = D.rest(D.byKey[c])[:3, 3] - D.rest(i)[:3, 3]; ds = S.rest(S.byKey[c])[:3, 3] - S.rest(S.byKey[k])[:3, 3]
        if np.linalg.norm(dd) > 1e-4 and np.linalg.norm(ds) > 1e-4: Q[i] = between(dd, ds)
        if k.endswith('Arm'): print('ALIGN %-14s %.1f deg' % (k, np.degrees(np.arccos(np.clip((np.trace(Q[i]) - 1) / 2, -1, 1)))))
hd, hs = D.byKey['Hips'], S.byKey['Hips']; kh = D.rest(hd)[1, 3] / S.rest(hs)[1, 3]
kat = next(i for i, n in enumerate(N) if n.get('name') == 'Katana'); rh = D.byKey['RightHand']; Pk = D.rest(D.par[kat]) if kat in D.par else np.eye(4); Ph = D.rest(D.par[hd])
def fk(new, nf):   # world matrices of the samurai under `new` ({joint: {'rotation', 'translation'}}), per frame
    cache = {}
    def w(j):
        if j not in cache:
            M = np.tile(D.static(j), (nf, 1, 1)); n = N[j]
            if j in new and 'rotation' in new[j]: M[:, :3, :3] = q2m(new[j]['rotation']) * np.array(n.get('scale', [1, 1, 1]))
            if j in new and 'translation' in new[j]: M[:, :3, 3] = new[j]['translation']
            cache[j] = w(D.par[j]) @ M if j in D.par else M
        return cache[j]
    return w
# the katana in the right hand: from the first key of its own idle
idle = next(a for a in D.J['animations'] if a['name'] == 'great sword idle'); _, Wi = D.posed(idle); REL = np.linalg.inv(Wi(rh)[0]) @ Wi(kat)[0]
def trs(M):   # per-frame matrices -> translation, rotation (xyzw), scale
    sc = np.linalg.norm(M[:, :3, :3], axis=1); return M[:, :3, 3], m2q(M[:, :3, :3] / sc[:, None, :]), sc
anims = []   # (name, T, {node: {path: array}})
for a in D.J['animations']:   # its own clips, as keyed
    T = None; new = {}
    for ch in a['channels']:
        s = a['samplers'][ch['sampler']]; t = D.acc(s['input'])[:, 0]; T = t if T is None or len(t) > len(T) else T
    for ch in a['channels']:
        s = a['samplers'][ch['sampler']]; t = D.acc(s['input'])[:, 0]; y = D.acc(s['output'])
        if len(t) != len(T): y = np.stack([np.interp(T, t, y[:, q]) for q in range(y.shape[1])], 1)
        new.setdefault(ch['target']['node'], {})[ch['target']['path']] = y
    anims.append((a['name'], T - T[0], new))
for src_name, name in TAKE.items():
    an = next(a for a in S.J['animations'] if a['name'] == src_name); T, W = S.posed(an); nf = len(T); new = {}; Rw = {}
    for i in order:
        k = key(N[i].get('name')); p = D.par.get(i)
        Rp = Rw[p] if p in Rw else np.tile(unit(D.rest(p)[:3, :3]) if p is not None else np.eye(3), (nf, 1, 1))
        if k in S.byKey:
            si = S.byKey[k]; Rw[i] = unit(W(si)[:, :3, :3]) @ np.linalg.inv(unit(S.rest(si)[:3, :3])) @ Q[i] @ unit(D.rest(i)[:3, :3])
            new[i] = {'rotation': m2q(np.linalg.inv(Rp) @ Rw[i])}
        else: Rw[i] = Rp @ q2m(np.array(N[i].get('rotation', [0, 0, 0, 1]), float))
    pos = D.rest(hd)[:3, 3] + kh * (W(hs)[:, :3, 3] - S.rest(hs)[:3, 3])
    new[hd]['translation'] = (np.linalg.inv(Ph) @ np.c_[pos, np.ones(nf)].T).T[:, :3]
    Wd = fk(new, nf); K = np.linalg.inv(Pk) @ Wd(rh) @ REL; kt, kr, ks = trs(K); new[kat] = {'translation': kt, 'rotation': kr, 'scale': ks}
    anims.append((name, T - T[0], new)); print('CLIP %-28s %.2f s from the knight (hips x%.3f)' % (name, T[-1] - T[0], kh))
# root motion: the hips' floor position pinned (not the deaths'); the katana with them
for name, T, new in anims:
    if 'death' in name or hd not in new or 'translation' not in new[hd]: continue
    w = (Ph @ np.c_[new[hd]['translation'], np.ones(len(T))].T).T[:, :3]; d = w - w[0]; d[:, 1] = 0
    travel = float(np.linalg.norm(d, axis=1).max())
    new[hd]['translation'] = (np.linalg.inv(Ph) @ np.c_[w - d, np.ones(len(T))].T).T[:, :3]
    if kat in new and 'translation' in new[kat]:
        kw = (Pk @ np.c_[new[kat]['translation'], np.ones(len(T))].T).T[:, :3] - d; new[kat]['translation'] = (np.linalg.inv(Pk) @ np.c_[kw, np.ones(len(T))].T).T[:, :3]
    print('PIN  %-28s floor travel taken off: %.2f m' % (name, travel))
D.J.setdefault('asset', {}).setdefault('extras', {})['nmkNormals'] = True
R['write'](D, anims, OUT)
print('wrote', OUT, len(anims), 'clips')
