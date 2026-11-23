# LT1 check: a character GLB posed at <clip> <time s> as the game draws it (node hierarchy, joint matrices x inverse binds
# for skinned meshes, rigid parts riding their joint), written as an OBJ + MTL with the base-colour images beside it, for a
# render that does not depend on an importer's reading of bone-parented parts (Blender misplaced the knight's sword by 7 m)
#   python -I tools/enemies/glb_pose_obj.py <in.glb> <clip | -> <time s> <out.obj>
import json, struct, sys, os
import numpy as np
GLB, CLIP, TIME, OUT = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
b = open(GLB, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = b[28 + jl:28 + jl + bl]
N = J['nodes']; par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
CT = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}; NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
def acc(i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; n = NC[a['type']]; dt = np.dtype(CT[a['componentType']])
    st = v.get('byteStride', 0); off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    if st and st != n * dt.itemsize:
        raw = np.frombuffer(BIN, np.uint8, a['count'] * st, off).reshape(a['count'], st)[:, :n * dt.itemsize]; out = np.frombuffer(raw.tobytes(), dt).reshape(a['count'], n)
    else: out = np.frombuffer(BIN, dt, a['count'] * n, off).reshape(a['count'], n)
    out = out.astype(np.float64)
    if a.get('normalized') and dt != np.float32: out /= np.iinfo(dt).max
    return out
def q2m(q):
    x, y, z, w = q; return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
trs = {i: [np.array(n.get('translation', [0, 0, 0]), float), np.array(n.get('rotation', [0, 0, 0, 1]), float), np.array(n.get('scale', [1, 1, 1]), float)] for i, n in enumerate(N) if 'matrix' not in n}
if CLIP != '-':
    an = next(a for a in J['animations'] if a['name'] == CLIP)
    for c in an['channels']:
        s = an['samplers'][c['sampler']]; t = acc(s['input'])[:, 0]; v = acc(s['output']); tt = min(max(TIME, t[0]), t[-1])
        k = int(np.clip(np.searchsorted(t, tt) - 1, 0, len(t) - 2)) if len(t) > 1 else 0; f = 0.0 if len(t) < 2 else (tt - t[k]) / max(t[k + 1] - t[k], 1e-9)
        x = v[k] * (1 - f) + v[min(k + 1, len(v) - 1)] * f
        if c['target']['path'] == 'rotation': x = x / np.linalg.norm(x)
        trs[c['target']['node']][{'translation': 0, 'rotation': 1, 'scale': 2}[c['target']['path']]] = x
def local(i):
    if 'matrix' in N[i]: return np.array(N[i]['matrix'], float).reshape(4, 4).T
    t, r, s = trs[i]; M = np.eye(4); M[:3, :3] = q2m(r) * s; M[:3, 3] = t; return M
W = {}
def world(i):
    if i not in W: W[i] = world(par[i]) @ local(i) if i in par else local(i)
    return W[i]
d = os.path.dirname(os.path.abspath(OUT)); base = os.path.splitext(os.path.basename(OUT))[0]
obj = open(OUT, 'w'); mtl = open(os.path.join(d, base + '.mtl'), 'w'); obj.write('mtllib %s.mtl\n' % base); vo = 1; done = set()
for i, n in enumerate(N):
    if 'mesh' not in n: continue
    if 'skin' in n:
        sk = J['skins'][n['skin']]; ib = acc(sk['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1); JM = np.stack([world(j) @ ib[k] for k, j in enumerate(sk['joints'])])
    for p in J['meshes'][n['mesh']]['primitives']:
        P = acc(p['attributes']['POSITION']); P4 = np.c_[P, np.ones(len(P))]
        if 'skin' in n and 'JOINTS_0' in p['attributes']:
            jn = acc(p['attributes']['JOINTS_0']).astype(int); wt = acc(p['attributes']['WEIGHTS_0']); wt /= np.maximum(wt.sum(1, keepdims=True), 1e-9)
            Q = np.zeros((len(P), 4))
            for c in range(4): Q += wt[:, c:c + 1] * np.einsum('nij,nj->ni', JM[jn[:, c]], P4)
        else: Q = (world(i) @ P4.T).T
        UV = acc(p['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in p['attributes'] else np.zeros((len(P), 2))
        I = acc(p['indices']).astype(int).reshape(-1, 3)
        mi = p.get('material', -1); mname = 'm%d' % mi
        if mi not in done:
            done.add(mi); mtl.write('newmtl %s\nKd 1 1 1\n' % mname)
            if mi >= 0:
                pb = J['materials'][mi].get('pbrMetallicRoughness', {})
                if 'baseColorTexture' in pb:
                    im = J['images'][J['textures'][pb['baseColorTexture']['index']]['source']]; v = J['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
                    ext = '.jpg' if im.get('mimeType') == 'image/jpeg' else '.png'; fn = '%s_tex%d%s' % (base, mi, ext); open(os.path.join(d, fn), 'wb').write(BIN[o:o + v['byteLength']]); mtl.write('map_Kd %s\n' % fn)
                elif 'baseColorFactor' in pb: mtl.write('Kd %f %f %f\n' % tuple(pb['baseColorFactor'][:3]))
        obj.write('o %s_%d\nusemtl %s\n' % (n.get('name', 'n'), i, mname))
        obj.write(''.join('v %f %f %f\n' % tuple(q[:3]) for q in Q)); obj.write(''.join('vt %f %f\n' % (u, 1 - v) for u, v in UV))
        obj.write(''.join('f %d/%d %d/%d %d/%d\n' % (a + vo, a + vo, b + vo, b + vo, c + vo, c + vo) for a, b, c in I)); vo += len(P)
obj.close(); mtl.close(); print('wrote', OUT, vo - 1, 'vertices')
