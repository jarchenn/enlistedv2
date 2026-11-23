# EK9 preview helper: props parented to bone nodes (rigid) turned into skinned meshes on that bone (weight 1), for viewers
# that misplace bone-parented nodes (Blender's importer). Exact when the prop's matrix x the bone's bind world is the identity
# (the props are authored in bind space): that is checked and printed. glTF level.
#   python -I props_skin.py <in.glb> <out.glb>
import json, struct, sys
import numpy as np
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(sys.argv[1]); sk = J['skins'][0]; joints = sk['joints']
a = J['accessors'][sk['inverseBindMatrices']]; v = J['bufferViews'][a['bufferView']]
IB = np.frombuffer(bytes(BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
par = {c: i for i, n in enumerate(J['nodes']) for c in n.get('children', [])}
def add(arr, typ, comp):
    data = np.ascontiguousarray(arr).tobytes()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data), 'target': 34962}); BIN.extend(data)
    J['accessors'].append({'bufferView': len(J['bufferViews']) - 1, 'componentType': comp, 'count': len(arr), 'type': typ}); return len(J['accessors']) - 1
def local(n):
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    x, y, z, w = n.get('rotation', [0, 0, 0, 1]); R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4); M[:3, :3] = R * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
roots = J['scenes'][J.get('scene', 0)]['nodes']
for i, n in enumerate(J['nodes']):
    if 'mesh' not in n or 'skin' in n or i not in par or par[i] not in joints: continue
    jk = joints.index(par[i]); E = np.linalg.inv(IB[jk]) @ local(n)
    print('%-28s on %-24s bind error %.4f' % (n.get('name'), J['nodes'][par[i]].get('name'), np.abs(E - np.eye(4)).max()))
    for p in J['meshes'][n['mesh']]['primitives']:
        cnt = J['accessors'][p['attributes']['POSITION']]['count']
        if np.abs(E - np.eye(4)).max() > 1e-3:   # not authored in bind space: its vertices taken there (skinned: J IB v' = J L v)
            def get(ai, k):
                aa = J['accessors'][ai]; vv = J['bufferViews'][aa['bufferView']]; st = vv.get('byteStride', 4 * k); o = vv.get('byteOffset', 0) + aa.get('byteOffset', 0)
                return np.array([np.frombuffer(bytes(BIN[o + r * st: o + r * st + 4 * k]), np.float32) for r in range(aa['count'])], float)
            P = get(p['attributes']['POSITION'], 3); P = (np.c_[P, np.ones(cnt)] @ E.T)[:, :3]
            p['attributes']['POSITION'] = add(P.astype(np.float32), 'VEC3', 5126); J['accessors'][-1]['min'] = P.min(0).tolist(); J['accessors'][-1]['max'] = P.max(0).tolist()
            if 'NORMAL' in p['attributes']:
                N = get(p['attributes']['NORMAL'], 3) @ np.linalg.inv(E[:3, :3]).T; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
                p['attributes']['NORMAL'] = add(N.astype(np.float32), 'VEC3', 5126)
            p['attributes'].pop('TANGENT', None)
        p['attributes']['JOINTS_0'] = add(np.tile(np.array([jk, 0, 0, 0], np.uint16), (cnt, 1)), 'VEC4', 5123)
        p['attributes']['WEIGHTS_0'] = add(np.tile(np.array([1, 0, 0, 0], np.float32), (cnt, 1)), 'VEC4', 5126)
    J['nodes'][par[i]]['children'].remove(i); nn = {'name': n.get('name'), 'mesh': n['mesh'], 'skin': 0}; del n['mesh']
    J['nodes'].append(nn); roots.append(len(J['nodes']) - 1)
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(sys.argv[2], 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
