# EK2: Knight 10's rider clips (knight10_rider.glb, seated on the horse) moved onto the elk's saddle, at the glTF level.
# The rider's own motion is kept; only his hips (which carry the seat) are re-based: per clip and frame
#   hips'(t) = Ej(t) Ej0^-1 T(seat_e - seat_h) Hj0 Hj(t)^-1 hips(t)
# Hj = the horse's seat joint, Ej = the elk's, both world; *0 = idle at t=0; seat_h = the horse's saddle top at idle t=0
# (the highest leather vertex under the rider's hips, skinned), seat_e = the elk's (elk_seat.json, written by elk_build.py).
#   python -I elk_rider.py <retarget_clips.py> <horse.glb> <rider.glb> <elk.glb> <elk_seat.json> <out rider.glb>
import json, sys
import numpy as np
RT, HORSE, RIDER, ELK, SEATJ, OUT = sys.argv[1:7]
src = open(RT).read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
load, Rig, q2m, m2q, write = g['load'], g['Rig'], g['q2m'], g['m2q'], g['write']
H, R, E = Rig(*load(HORSE)), Rig(*load(RIDER)), Rig(*load(ELK))
def node(rig, name): return next(i for i, n in enumerate(rig.N) if n.get('name') == name)
def anim(rig, name): return next(a for a in rig.J['animations'] if a['name'] == name)
hj, ej, hips = node(H, 'BN_Spine_01_03_03'), node(E, json.load(open(SEATJ))['seat_bone']), node(R, 'mixamorig:Hips')
def rigid(M):   # world matrices (n,4,4) -> rotation (columns normalised) and translation
    out = np.tile(np.eye(4), (len(M), 1, 1)); out[:, :3, :3] = M[:, :3, :3] / np.linalg.norm(M[:, :3, :3], axis=-2, keepdims=True); out[:, :3, 3] = M[:, :3, 3]; return out
def resample(T, M, Tn):   # rigid matrices on grid T -> grid Tn (translation linear, rotation nlerp), clamped at the ends
    q = m2q(M[:, :3, :3]); out = np.tile(np.eye(4), (len(Tn), 1, 1))
    for k, t in enumerate(np.clip(Tn, T[0], T[-1])):
        i = min(int(np.searchsorted(T, t, 'right')) - 1, len(T) - 2) if len(T) > 1 else 0; u = 0 if len(T) == 1 else (t - T[i]) / max(T[i + 1] - T[i], 1e-9)
        qq = q[i] * (1 - u) + q[min(i + 1, len(T) - 1)] * u; qq /= np.linalg.norm(qq)
        out[k, :3, :3] = q2m(qq); out[k, :3, 3] = M[i, :3, 3] * (1 - u) + M[min(i + 1, len(T) - 1), :3, 3] * u
    return out
def world(rig, name, i):
    T, W = rig.posed(anim(rig, name)); return T, W(i)
# the horse's saddle top at idle t=0: leather vertices, skinned with the idle pose
Th, Wh = H.posed(anim(H, 'idle')); Tr, Wr = R.posed(anim(R, 'idle'))
hip0 = Wr(hips)[0, :3, 3]; skin = H.J['skins'][0]; ia = H.J['accessors'][skin['inverseBindMatrices']]; iv = H.J['bufferViews'][ia['bufferView']]
IB = np.frombuffer(H.BIN, np.float32, ia['count'] * 16, iv.get('byteOffset', 0) + ia.get('byteOffset', 0)).reshape(-1, 4, 4).transpose(0, 2, 1).astype(np.float64)
cand = []
for mi, n in ((n['mesh'], n) for n in H.N if 'mesh' in n and n.get('skin') == 0):
    for p in H.J['meshes'][mi]['primitives']:
        if 'leather' not in H.J['materials'][p.get('material', 0)].get('name', ''): continue
        P = H.acc(p['attributes']['POSITION']); a = H.J['accessors'][p['attributes']['JOINTS_0']]; v = H.J['bufferViews'][a['bufferView']]
        jt = {5121: np.uint8, 5123: np.uint16}[a['componentType']]
        JI = np.frombuffer(H.BIN, jt, a['count'] * 4, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, 4)[:, 0]   # rigid pieces: weight 1 on the first joint
        for j in np.unique(JI):
            M = Wh(skin['joints'][j])[0] @ IB[j]; Q = (M @ np.c_[P[JI == j], np.ones((JI == j).sum())].T).T[:, :3]; cand.append(Q)
Q = np.concatenate(cand); near = Q[np.hypot(Q[:, 0] - hip0[0], Q[:, 2] - hip0[2]) < 0.15]; seat_h = near[np.argmax(near[:, 1])]
seat_e = np.array(json.load(open(SEATJ))['seat_gltf'])
print('seat horse %s  elk %s  rider hips %s' % (seat_h.round(3), seat_e.round(3), hip0.round(3)))
Hj0 = rigid(Wh(hj)[:1])[0]; Ej0 = rigid(world(E, 'idle', ej)[1][:1])[0]
Tm = np.eye(4); Tm[:3, 3] = seat_e - seat_h; C = np.linalg.inv(Ej0) @ Tm @ Hj0
DAMP = {'walk': (0.25, 0.6)}
BODY_KEEP = {'Spine': 0.0, 'Spine1': 0.0, 'Spine2': 0.0, 'Neck': 0.4, 'Head': 0.8}   # share of his own swing kept per bone at the walk   # clip: (share of the saddle's rotation kept, share of its bounce kept)
IDLE_BODY = {'walk': 3.0}   # EK7 (user): at the walk his body moves as in his idle (breathing, settling) over a loop as long as the idle
BODY_BONES = ('Spine', 'Spine1', 'Spine2', 'Neck', 'Head')
def unscale(M): S = np.linalg.norm(M[:, :3, :3], axis=-2); Mr = M.copy(); Mr[:, :3, :3] /= S[:, None, :]; return Mr, S
anI = anim(R, 'idle'); TI, WI = R.posed(anI); thI, whI = world(H, 'idle', hj)
XI, XS = unscale(np.linalg.inv(resample(thI, rigid(whI), TI)) @ WI(hips))   # his hips in the horse's seat frame, through the idle
chI = {(c['target']['node'], c['target']['path']): anI['samplers'][c['sampler']] for c in anI['channels']}
# EK12 (user): after a stab he looks down and around for the next enemies: 'look_around', his idle with the head, neck and
# upper spine turned down-left, down-right, then down ahead (the mount stands as in its idle)
import copy
if not any(a['name'] == 'look_around' for a in R.J['animations']):
    la = copy.deepcopy(anim(R, 'idle')); la['name'] = 'look_around'; R.J['animations'].append(la)
HSRC = {'look_around': 'idle'}
LOOK = [(0.0, 0, 0), (0.7, 45, 22), (1.5, -45, 22), (2.3, 0, 30), (3.0, 0, 0)]   # seconds, yaw (left +), pitch (down +), the whole turn
LOOK_SHARE = {'Head': 0.55, 'Neck': 0.25, 'Spine2': 0.2}
def qmul(a, b):
    x1, y1, z1, w1 = np.moveaxis(a, -1, 0); x2, y2, z2, w2 = np.moveaxis(b, -1, 0)
    return np.stack([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2], -1)
def look_q(T, share):
    tk = [k[0] for k in LOOK]; yaw = np.radians(np.interp(T, tk, [k[1] for k in LOOK])) * share; pit = np.radians(np.interp(T, tk, [k[2] for k in LOOK])) * share
    qy = np.stack([0 * yaw, np.sin(yaw / 2), 0 * yaw, np.cos(yaw / 2)], -1); qp = np.stack([np.sin(pit / 2), 0 * pit, 0 * pit, np.cos(pit / 2)], -1)
    return qmul(qy, qp)   # local frame of a Mixamo bone: y along the bone (yaw), x across it (pitch)
anims = []
for an in R.J['animations']:
    T, W = R.posed(an); nm = an['name']; Tsrc = T
    if nm in IDLE_BODY: T = np.round(np.arange(0, IDLE_BODY[nm] + 1e-6, 1 / 24), 6); Tsrc = np.mod(T, Tsrc[-1])   # the walk's own channels loop inside
    th, wh = world(H, HSRC.get(nm, nm), hj); te, we = world(E, nm, ej)
    Hjt = resample(th, rigid(wh), T); Ejt = resample(te, rigid(we), T)
    if nm in DAMP:   # EK6 (user): the rider sways less at the walk: the saddle's pitch / roll damped about the seat (he stays on it), its bounce partly
        ar, av = DAMP[nm]; Lp = np.linalg.inv(Ej0) @ np.r_[seat_e, 1.0]
        st = (Ejt @ Lp)[:, :3]; q = m2q(Ejt[:, :3, :3] @ Ej0[:3, :3].T); qm = q.mean(0); qm /= np.linalg.norm(qm)
        qd = qm * (1 - ar) + q * ar; qd /= np.linalg.norm(qd, axis=1, keepdims=True); Rd = q2m(qd) @ Ej0[:3, :3]
        st[:, 1] = st[:, 1].mean() + av * (st[:, 1] - st[:, 1].mean())   # glTF y up
        Ejt = Ejt.copy(); Ejt[:, :3, :3] = Rd; Ejt[:, :3, 3] = st - (Rd @ Lp[:3])
    if nm in IDLE_BODY:
        X = resample(TI, XI, T); X[:, :3, :3] *= XS[0][None, None, :]
    else: X = np.linalg.inv(Hjt) @ W(hips)   # his hips in the horse's seat frame
    if nm in DAMP and nm not in IDLE_BODY:   # EK6: held at their mean (no bounce or rock of his own in the saddle)
        qx = m2q(X[:, :3, :3] / np.linalg.norm(X[:, :3, :3], axis=-2, keepdims=True)); qx = qx.mean(0); qx /= np.linalg.norm(qx)
        Xm = np.eye(4); Xm[:3, :3] = q2m(qx) * np.linalg.norm(X[0, :3, :3], axis=-2); Xm[:3, 3] = X[:, :3, 3].mean(0); X = np.tile(Xm, (len(T), 1, 1))
    newW = Ejt @ C @ X
    if nm == 'horse_death':   # EK13: he jumps clear as the mount goes down: from the elk's saddle onto his own path off the horse (moved by the
        # seat offset), blended over the take-off (the first 8 frames at 24 fps), so he lands standing beside it, not under it
        Wo = W(hips).copy(); Wo[:, :3, 3] += (seat_e - seat_h); w = np.clip(T / (8 / 24), 0, 1); w = w * w * (3 - 2 * w)
        Mr, S_ = unscale(newW); Or, _ = unscale(Wo); qa, qb = m2q(Mr[:, :3, :3]), m2q(Or[:, :3, :3]); qb = qb * np.sign(np.sum(qa * qb, 1))[:, None]
        q_ = qa * (1 - w)[:, None] + qb * w[:, None]; q_ /= np.linalg.norm(q_, axis=1, keepdims=True)
        newW = np.tile(np.eye(4), (len(T), 1, 1)); newW[:, :3, :3] = q2m(q_) * S_[:, None, :]; newW[:, :3, 3] = Mr[:, :3, 3] * (1 - w)[:, None] + Or[:, :3, 3] * w[:, None]
    Pw = W(R.par[hips]); Pw = np.tile(Pw[0], (len(T), 1, 1)) if len(Pw) != len(T) else Pw; loc = np.linalg.inv(Pw) @ newW
    sc = np.linalg.norm(loc[:, :3, :3], axis=-2)
    new = {}   # every channel of the clip kept; the hips' translation and rotation replaced
    ch = {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
    for (i, p), s in ch.items():
        if i == hips and p in ('translation', 'rotation'): continue
        ti, v = R.acc(s['input'])[:, 0], R.acc(s['output']); tq = Tsrc
        if nm in IDLE_BODY and p == 'rotation' and R.N[i].get('name', '').split(':')[-1] in BODY_BONES and (i, p) in chI:
            s = chI[(i, p)]; ti, v = R.acc(s['input'])[:, 0], R.acc(s['output']); tq = T
        new.setdefault(i, {})[p] = np.stack([np.interp(tq, ti, v[:, k]) for k in range(v.shape[1])], -1)
        kb = BODY_KEEP.get(R.N[i].get('name', '').split(':')[-1])
        if nm in DAMP and nm not in IDLE_BODY and p == 'rotation' and kb is not None:   # EK6 (user): at the walk his body holds still on the saddle, his head moves a little
            q = new[i][p]; q = q * np.sign((q @ q[0]))[:, None]; qm = q.mean(0); qm /= np.linalg.norm(qm)
            q = qm * (1 - kb) + q * kb; new[i][p] = q / np.linalg.norm(q, axis=1, keepdims=True)
    if nm == 'look_around':
        for i_, d_ in new.items():
            sh_ = LOOK_SHARE.get(R.N[i_].get('name', '').split(':')[-1])
            if sh_ and 'rotation' in d_: q_ = qmul(d_['rotation'], look_q(T, sh_)); d_['rotation'] = q_ / np.linalg.norm(q_, axis=1, keepdims=True)
    new.setdefault(hips, {})['translation'] = loc[:, :3, 3]; new[hips]['rotation'] = m2q(loc[:, :3, :3] / sc[:, None, :])
    anims.append((nm, T, new)); print('CLIP %-18s %d frames, hips moved %.3f m at t0' % (nm, len(T), np.linalg.norm(newW[0, :3, 3] - W(hips)[0, :3, 3])))
write(R, anims, OUT); print('wrote', OUT)
