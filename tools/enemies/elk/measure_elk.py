# EK: numbers for zombie.json from the built models: head height, strike points ('at' in the hand joint's frame, where the strike
# lands at the swing), the foot knight's spawn point, the elk's ground speeds (stance feet)
import json, sys
import numpy as np
src = open('E:/nmk/wt-pf3/tools/enemies/retarget_clips.py').read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
Rig, q2m = g['Rig'], g['q2m']
def posedT(R, an, n=240):
    ch = {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
    T = np.linspace(0, max(R.acc(s['input'])[:, 0].max() for s in ch.values()), n); cache = {}
    def local(i):
        nd = R.N[i]
        if 'matrix' in nd: return np.tile(R.static(i), (n, 1, 1))
        tr = np.tile(np.array(nd.get('translation', [0, 0, 0]), float), (n, 1)); r = np.tile(np.array(nd.get('rotation', [0, 0, 0, 1]), float), (n, 1)); sc = np.tile(np.array(nd.get('scale', [1, 1, 1]), float), (n, 1))
        for p, arr in (('translation', tr), ('rotation', r), ('scale', sc)):
            if (i, p) in ch: ti, v = R.acc(ch[(i, p)]['input'])[:, 0], R.acc(ch[(i, p)]['output']); arr[:] = np.stack([np.interp(T, ti, v[:, k]) for k in range(v.shape[1])], -1)
        r /= np.linalg.norm(r, axis=1, keepdims=True); M = np.tile(np.eye(4), (n, 1, 1)); M[:, :3, :3] = q2m(r) * sc[:, None, :]; M[:, :3, 3] = tr; return M
    def W(i):
        if i not in cache: cache[i] = W(R.par[i]) @ local(i) if i in R.par else local(i)
        return cache[i]
    return T, W
def weapon_at(R, frac=0.85):   # the halberd's head: frac of the way from the butt (the end by the hand) to the tip, in the hand joint's frame
    sk = R.J['skins'][0]; hj = next(i for i, n in enumerate(R.N) if n.get('name') == 'mixamorig:RightHand'); jk = sk['joints'].index(hj)
    IB = R.acc(sk['inverseBindMatrices']) if False else None
    a = R.J['accessors'][sk['inverseBindMatrices']]; v = R.J['bufferViews'][a['bufferView']]
    IB = np.frombuffer(bytes(R.BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
    mats = [m.get('name', '') for m in R.J['materials']]; P = []
    for ni, n in enumerate(R.N):
        if 'mesh' not in n: continue
        for p in R.J['meshes'][n['mesh']]['primitives']:
            if mats[p['material']].startswith('weapon_'): P.append(R.acc(p['attributes']['POSITION']))
    P = np.concatenate(P); c = P.mean(0); _, _, V = np.linalg.svd(P - c, full_matrices=False); d = V[0]; t = (P - c) @ d
    hand = np.linalg.inv(IB[jk])[:3, 3]; e0, e1 = c + d * t.min(), c + d * t.max()
    butt, tip = (e0, e1) if np.linalg.norm(e0 - hand) < np.linalg.norm(e1 - hand) else (e1, e0)
    p = butt + (tip - butt) * frac; return (IB[jk] @ np.r_[p, 1])[:3], hj, butt, tip
mode = sys.argv[1]; R = Rig(*g['load'](sys.argv[2])); ix = lambda nm: next(i for i, x in enumerate(R.N) if x.get('name') == nm); an = lambda nm: next(a for a in R.J['animations'] if a['name'] == nm)
at, hj, butt, tip = weapon_at(R); print('weapon butt', butt.round(3), 'tip', tip.round(3), 'at (hand frame)', at.round(4).tolist())
if mode == 'mounted':
    T, W = posedT(R, an('idle')); print('head joint y (idle t0) %.4f' % W(ix('mixamorig:Head'))[0, 1, 3])
    T, W = posedT(R, an('horse_death')); h = W(ix('mixamorig:Hips'))[:, :3, 3]; print('horse_death: hips at end', h[-1].round(3).tolist(), 'lowest %.3f' % h[:, 1].min())
    for clip, sw in (('attack_charge', 0.489), ('attack_right', 0.27)):
        T, W = posedT(R, an(clip)); k = int(round(sw * (len(T) - 1))); p = W(hj)[k] @ np.r_[at, 1]
        print('%s: strike at the swing %s, horizontal %.3f -> range [%.3f, %.3f]' % (clip, p[:3].round(3), np.hypot(p[0], p[2]), np.hypot(p[0], p[2]) - 0.977, np.hypot(p[0], p[2]) + 0.977))
    for clip, foot in (('walk', 'RigLFLegAnkle'), ('gallop_scan', 'RigLFLegAnkle')):
        T, W = posedT(R, an(clip), 600); f = W(ix(foot))[:, :3, 3]; vz = np.gradient(f[:, 2], T); low = f[:, 1] < np.percentile(f[:, 1], 25)
        print('%s: stance foot speed %.2f m/s (median of the lowest quarter)' % (clip, -np.median(vz[low])))
else:
    T, W = posedT(R, an('sword and shield idle')); print('head joint y (idle t0) %.4f' % W(ix('mixamorig:Head'))[0, 1, 3])
