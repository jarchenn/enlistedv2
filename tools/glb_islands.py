# per material: triangles and connected islands (vertices welded by position, 0.01 mm) in a GLB
import json, struct, sys
import numpy as np
CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); BIN = b[20 + jl + 8:]
    def acc(i):
        a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; n = NC[a['type']]; dt = np.dtype(CT[a['componentType']])
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0); st = v.get('byteStride', dt.itemsize * n)
        raw = np.frombuffer(BIN, np.uint8, count=st * (a['count'] - 1) + dt.itemsize * n, offset=off)
        return np.lib.stride_tricks.as_strided(raw.view(dt) if st % dt.itemsize == 0 else raw, shape=(a['count'], n), strides=(st, dt.itemsize)).copy() if st % dt.itemsize == 0 else None
    out = {}
    for m in J['meshes']:
        for pr in m['primitives']:
            mat = J['materials'][pr['material']]['name'] if 'material' in pr else '-'
            if 'rifle' in mat: continue
            P = acc(pr['attributes']['POSITION']); I = acc(pr['indices']).ravel() if 'indices' in pr else np.arange(len(P))
            key = np.round(P / 1e-5).astype(np.int64); _, wid = np.unique(key, axis=0, return_inverse=True); wid = wid.ravel()
            par = np.arange(wid.max() + 1)
            def f(x):
                r = x
                while par[r] != r: r = par[r]
                while par[x] != r: par[x], x = r, par[x]
                return r
            T = wid[I].reshape(-1, 3)
            for a, b_, c in T:
                ra, rb, rc = f(a), f(b_), f(c)
                par[rb] = ra; par[f(rc)] = ra
            used = np.unique(T); isl = len({f(x) for x in used})
            t, i = out.get(mat, (0, 0)); out[mat] = (t + len(T), i + isl)
    return out
res = [load(p) for p in sys.argv[1:]]
mats = sorted(set().union(*res))
print('%-34s' % 'material' + ''.join('%22s' % p.split('/')[-1][:20] for p in sys.argv[1:]))
for m in mats: print('%-34s' % m[:34] + ''.join('%14s tri %5s' % (r.get(m, ('-', '-'))[0], r.get(m, ('-', '-'))[1]) for r in res))
print('%-34s' % 'TOTAL' + ''.join('%14d tri %5d' % (sum(v[0] for v in r.values()), sum(v[1] for v in r.values())) for r in res))
