# BC2: seat a posed model on the ground: every clip's hips translation keys raised by LIFT metres along glTF +y (world), taken into
# the hips' parent space. For a pose whose lowest point sits below the bind pose's ground line (the game grounds the bind pose).
#   python -I tools/enemies/gltf_lift_hips.py <in.glb> <out.glb> <lift m>
import json, struct, sys, numpy as np
src, out, LIFT = sys.argv[1], sys.argv[2], float(sys.argv[3])
b = open(src, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = bytearray(b[28 + jl:28 + jl + bl])
N = J['nodes']; par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
def q2m(q):
    x, y, z, w = q; return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
def static(i):
    n = N[i]
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    M = np.eye(4); M[:3, :3] = q2m(n.get('rotation', [0, 0, 0, 1])) * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def world(i):
    M = static(i)
    while i in par: i = par[i]; M = static(i) @ M
    return M
hips = next(i for i, n in enumerate(N) if n.get('name') == 'Hips')
d = np.linalg.inv(world(par[hips])[:3, :3]) @ np.array([0, LIFT, 0]) if hips in par else np.array([0, LIFT, 0])
done = 0
for an in J['animations']:
    for c in an['channels']:
        if c['target']['node'] == hips and c['target']['path'] == 'translation':
            a = J['accessors'][an['samplers'][c['sampler']]['output']]; v = J['bufferViews'][a['bufferView']]; off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
            arr = np.frombuffer(BIN, np.float32, a['count'] * 3, off).reshape(-1, 3) + d; arr = arr.astype(np.float32); BIN[off:off + arr.nbytes] = arr.tobytes()
            if 'min' in a: a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
            done += 1
assert done, 'no hips translation channel'
js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(out, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('lifted', done, 'channel(s) by', LIFT, 'local', d.round(4))
