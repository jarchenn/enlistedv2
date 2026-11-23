# the game's height fit (character.cpp): the skinned vertices in the rest pose (node world x inverse bind), their vertical extent
import json, struct, sys
import numpy as np
src = open('E:/nmk/wt-pf3/tools/enemies/retarget_clips.py').read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
for f in sys.argv[1:]:
    R = g['Rig'](*g['load'](f)); sk = R.J['skins'][0]; a = R.J['accessors'][sk['inverseBindMatrices']]; v = R.J['bufferViews'][a['bufferView']]
    IB = np.frombuffer(bytes(R.BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
    S = np.array([R.rest(j) @ IB[k] for k, j in enumerate(sk['joints'])]); lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    par = R.par; jset = {j: k for k, j in enumerate(sk['joints'])}
    for ni, n in enumerate(R.N):
        if 'mesh' not in n: continue
        if 'skin' not in n:   # rigid: in rest model space, weight 1 on the nearest joint above it (as character.cpp)
            bj, q = 0, ni
            while q is not None:
                if q in jset: bj = jset[q]; break
                q = par.get(q)
            for p in R.J['meshes'][n['mesh']]['primitives']:
                P = R.acc(p['attributes']['POSITION']); Q = (np.c_[P, np.ones(len(P))] @ (S[bj] @ R.rest(ni)).T)[:, :3]
                lo = np.minimum(lo, Q.min(0)); hi = np.maximum(hi, Q.max(0))
            continue
        for p in R.J['meshes'][n['mesh']]['primitives']:
            A = p['attributes']
            if 'JOINTS_0' not in A: continue
            P = R.acc(A['POSITION']); Jt = R.acc(A['JOINTS_0']).astype(int) if False else None
            aj = R.J['accessors'][A['JOINTS_0']]; vj = R.J['bufferViews'][aj['bufferView']]; dt = {5121: np.uint8, 5123: np.uint16}[aj['componentType']]
            JI = np.frombuffer(bytes(R.BIN[vj.get('byteOffset', 0) + aj.get('byteOffset', 0):][:aj['count'] * 4 * np.dtype(dt).itemsize]), dt).reshape(-1, 4).astype(int)
            W = R.acc(A['WEIGHTS_0']); M = np.einsum('vk,vkij->vij', W, S[JI]); Q = np.einsum('vij,vj->vi', M, np.c_[P, np.ones(len(P))])[:, :3]
            lo = np.minimum(lo, Q.min(0)); hi = np.maximum(hi, Q.max(0))
    print('%-28s rest extent y %.4f  (x %.2f..%.2f, z %.2f..%.2f)' % (f.split('/')[-1], hi[1] - lo[1], lo[0], hi[0], lo[2], hi[2]))
