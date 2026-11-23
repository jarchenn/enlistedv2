# RA1: an archer GLB on a UE4-mannequin-named skeleton (pelvis, spine_01.., upperarm_l, hand_l ..., a "bow_string" under hand_l
# and a root-level "arrow" bone whose world transform is baked into every clip) -> the game's enemy GLB, at the glTF level:
#   - the joints renamed to Mixamo's ("mixamorig:Hips" ...: the game finds its 22 joints by those names; bow_string -> BowString,
#     arrow -> Arrow, as Archer 1's); twist, ik and root bones keep their names
#   - root motion: every clip but the deaths has the hips' floor position pinned to its first key (the AI moves the body); the
#     arrow, a root-level bone, moves with the hips
#   - asset.extras.nmkNormals: the game loads the normal maps
# Then tools/enemies/archer_shot.py adds the "longbow shot" clip the archers' attack plays.
#   python -I tools/enemies/ue4_archer_prep.py <in.glb> <out.glb>
import json, struct, sys, os
import numpy as np
SRC, OUT = sys.argv[1:3]
_rc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'retarget_clips.py')).read().split("if sys.argv[1] == '--check'")[0]; R = {}; exec(_rc, R)   # its Rig / load / write
M = {'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_02': 'Spine1', 'spine_03': 'Spine2', 'neck_01': 'Neck', 'head': 'Head', 'bow_string': 'BowString', 'arrow': 'Arrow'}
for c, s in (('l', 'Left'), ('r', 'Right')):
    M.update({'clavicle_' + c: s + 'Shoulder', 'upperarm_' + c: s + 'Arm', 'lowerarm_' + c: s + 'ForeArm', 'hand_' + c: s + 'Hand', 'thigh_' + c: s + 'UpLeg', 'calf_' + c: s + 'Leg', 'foot_' + c: s + 'Foot', 'ball_' + c: s + 'ToeBase'})
    for uf, mf in (('thumb', 'Thumb'), ('index', 'Index'), ('middle', 'Middle'), ('ring', 'Ring'), ('pinky', 'Pinky')):
        for i in (1, 2, 3): M['%s_%02d_%s' % (uf, i, c)] = '%sHand%s%d' % (s, mf, i)
D = R['Rig'](*R['load'](SRC)); N = D.N; joints = set(D.J['skins'][0]['joints']); hit = 0
for i in joints:
    if N[i].get('name') in M: N[i]['name'] = 'mixamorig:' + M[N[i]['name']]; hit += 1
assert hit == len(M), (hit, len(M), sorted(set(M) - {n.get('name') for n in N}))
hd = next(i for i in joints if N[i]['name'] == 'mixamorig:Hips'); ar = next(i for i in joints if N[i]['name'] == 'mixamorig:Arrow')
Ph = D.rest(D.par[hd]) if hd in D.par else np.eye(4); Pa = D.rest(D.par[ar]) if ar in D.par else np.eye(4)
anims = []
for a in D.J['animations']:
    T = max((D.acc(a['samplers'][ch['sampler']]['input'])[:, 0] for ch in a['channels']), key=len); new = {}
    for ch in a['channels']:
        s = a['samplers'][ch['sampler']]; t = D.acc(s['input'])[:, 0]; y = D.acc(s['output'])
        if len(t) != len(T):   # a constant or stepped channel: held values on the clip's key grid
            idx = np.clip(np.searchsorted(t, T, side='right') - 1, 0, len(t) - 1); y = y[idx] if s.get('interpolation') == 'STEP' else np.stack([np.interp(T, t, y[:, q]) for q in range(y.shape[1])], 1)
        new.setdefault(ch['target']['node'], {})[ch['target']['path']] = y
    if 'death' not in a['name'] and hd in new and 'translation' in new[hd]:
        w = (Ph @ np.c_[new[hd]['translation'], np.ones(len(T))].T).T[:, :3]; d = w - w[0]; d[:, 1] = 0; travel = float(np.linalg.norm(d, axis=1).max())
        if travel > 0.05:
            new[hd]['translation'] = (np.linalg.inv(Ph) @ np.c_[w - d, np.ones(len(T))].T).T[:, :3]
            if ar in new and 'translation' in new[ar]:
                kw = (Pa @ np.c_[new[ar]['translation'], np.ones(len(T))].T).T[:, :3] - d; new[ar]['translation'] = (np.linalg.inv(Pa) @ np.c_[kw, np.ones(len(T))].T).T[:, :3]
            print('PIN  %-36s floor travel taken off: %.2f m' % (a['name'], travel))
    anims.append((a['name'], T - T[0], new))
D.J.setdefault('asset', {}).setdefault('extras', {})['nmkNormals'] = True
R['write'](D, anims, OUT); print('wrote', OUT, len(anims), 'clips,', hit, 'joints renamed')
