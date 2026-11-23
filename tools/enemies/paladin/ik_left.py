# PM2 (user: "his left hand is not holding the sword"): the left hand put on the hilt in every clip frame where it is meant to be
# there. Retargeted from Knight 4, whose arms differ, the left palm misses the hilt by up to ~15 cm. Per frame: the target is the
# hilt point nearest the palm (kept on the hilt and below the right hand); a two-bone IK (LeftArm, LeftForeArm, the elbow in its own
# plane) brings the wrist there, the hand keeping its turn. Blended by how near the palm already was (a clip that lets go keeps it).
#   python -I ik_left.py <retarget_mx.py> <in.glb> <out.glb> <guard xyz> <pommel xyz>   (the hilt in bind space)
import json, sys
import numpy as np
RT, IN, OUT = sys.argv[1:4]; GUARD = np.array(json.loads(sys.argv[4])); POMMEL = np.array(json.loads(sys.argv[5]))
src = open(RT).read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
Rig, load, q2m, m2q, write = g['Rig'], g['load'], g['q2m'], g['m2q'], g['write']
R = Rig(*load(IN)); sk = R.J['skins'][0]
a = R.J['accessors'][sk['inverseBindMatrices']]; v = R.J['bufferViews'][a['bufferView']]
IB = np.frombuffer(bytes(R.BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
ix = lambda nm: next(i for i, x in enumerate(R.N) if x.get('name') == 'mixamorig:' + nm)
iW, iSh, iA, iF, iH, iM, iRH, iRM = (ix(n) for n in ('Weapon', 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand', 'LeftHandMiddle1', 'RightHand', 'RightHandMiddle1'))
kW = sk['joints'].index(iW); HL = np.linalg.norm(POMMEL - GUARD)
def unit(M): return M / np.linalg.norm(M, axis=-2, keepdims=True)
def rot_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b); c = np.cross(a, b); s = np.linalg.norm(c); d = float(a @ b)
    if s < 1e-9: return np.eye(3)
    k = c / s; K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]]); return np.eye(3) + s * K + (1 - d) * K @ K
anims = []; report = []
for an in R.J['animations']:
    T, W = R.posed(an); n = len(T)
    ch = {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
    new = {}
    for (i, p), s_ in ch.items():
        ti, vv = R.acc(s_['input'])[:, 0], R.acc(s_['output']); new.setdefault(i, {})[p] = np.stack([np.interp(T, ti, vv[:, k]) for k in range(vv.shape[1])], -1)
    Mw = W(iW) @ IB[kW]; Gw = (Mw @ np.r_[GUARD, 1])[:, :3]; Pw = (Mw @ np.r_[POMMEL, 1])[:, :3]; d = (Pw - Gw) / HL
    Sh, A_, F_, H_ = W(iSh), W(iA), W(iF), W(iH); mid = W(iM)[:, :3, 3]; rp = (W(iRH)[:, :3, 3] + W(iRM)[:, :3, 3]) / 2
    qa, qf = np.zeros((n, 4)), np.zeros((n, 4)); worst0 = worst1 = 0; used = 0
    for k in range(n):
        S, E, Wr = A_[k, :3, 3], F_[k, :3, 3], H_[k, :3, 3]; palm = (Wr + mid[k]) / 2
        al_r = (rp[k] - Gw[k]) @ d[k]; al = np.clip((palm - Gw[k]) @ d[k], al_r + 0.11, HL - 0.03); tgt = Gw[k] + d[k] * al   # on the hilt, a hand's width below the right
        off0 = np.linalg.norm(palm - tgt); wgt = float(np.clip((0.32 - off0) / 0.12, 0, 1))   # a palm far off is letting go on purpose
        RA, RF = unit(A_[k, :3, :3]), unit(F_[k, :3, :3])
        if any(x in an['name'] for x in ('death', 'cast', 'spell')): wgt = 0   # he lets go of it in these
        if wgt > 0:
            tgt = palm + (tgt - palm) * wgt
            for _ in range(3):   # the wrist goes where the palm must be, less the palm's offset; the elbow keeps its side
                Wt = tgt - (palm - Wr); a1, b1 = np.linalg.norm(E - S), np.linalg.norm(Wr - E); c1 = min(np.linalg.norm(Wt - S), 0.999 * (a1 + b1))
                u = (Wt - S) / np.linalg.norm(Wt - S); pole = (E - S) - u * ((E - S) @ u); pole /= np.linalg.norm(pole) + 1e-9
                ca = (a1 * a1 + c1 * c1 - b1 * b1) / (2 * a1 * c1); En = S + a1 * (ca * u + np.sqrt(max(0, 1 - ca * ca)) * pole)
                R1 = rot_between(E - S, En - S); R2 = rot_between(R1 @ (Wr - E), (S + u * c1) - En)
                RA = R1 @ RA; RF = R2 @ R1 @ RF; off = palm - Wr; E = En; Wr = S + u * c1; palm = Wr + R2 @ R1 @ off
            used += 1
        worst0 = max(worst0, off0 * (wgt > 0)); worst1 = max(worst1, np.linalg.norm(palm - tgt) * (wgt > 0))
        qa[k] = m2q((unit(Sh[k, :3, :3]).T @ RA)[None])[0]; qf[k] = m2q((RA.T @ RF)[None])[0]
    for q in (qa, qf):
        for k in range(1, n):
            if q[k] @ q[k - 1] < 0: q[k] = -q[k]
    new.setdefault(iA, {})['rotation'] = qa; new.setdefault(iF, {})['rotation'] = qf
    anims.append((an['name'], T, new)); report.append((an['name'], used, n, worst0, worst1))
write(R, anims, OUT)
for r in report: print('%-28s IK on %3d of %3d frames, palm off the hilt %.3f -> %.3f m' % r)
print('wrote', OUT)
