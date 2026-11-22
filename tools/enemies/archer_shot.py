# AR1: Archer 1's shot as one attack clip, at the glTF level (the enemy AI plays one clip per attack, its blow at "swing"):
# "standing draw arrow" (nock and draw), the full draw held `hold` s (its last key repeated), then "standing aim recoil" (the
# release and follow-through), appended as the new clip "longbow shot". Channels are matched by (node, path); both clips
# key the same 201 channels (LINEAR, or STEP where Blender wrote a constant channel). The two source clips stay.
#   python -I tools/enemies/archer_shot.py <in.glb> <out.glb> [hold_s]
import json, struct, sys
import numpy as np
SRC, OUT = sys.argv[1:3]; HOLD = float(sys.argv[3]) if len(sys.argv) > 3 else 0.4
b = open(SRC, 'rb').read(); assert b[:4] == b'glTF'
jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
BIN = bytearray(b[28 + jl:28 + jl + bl])
N = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}
def read(i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; assert a['componentType'] == 5126 and v.get('byteStride', 0) in (0, 4 * N[a['type']])
    return np.frombuffer(bytes(BIN), np.float32, count=a['count'] * N[a['type']], offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, N[a['type']]).copy()
def write(arr, typ, mm=False):
    arr = np.ascontiguousarray(arr, np.float32)
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': arr.nbytes}); BIN.extend(arr.tobytes())
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}
    if mm: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
A = {a['name']: a for a in J['animations']}
assert 'longbow shot' not in A
draw, rec = A['standing draw arrow'], A['standing aim recoil']
def chans(an): return {(c['target']['node'], c['target']['path']): an['samplers'][c['sampler']] for c in an['channels']}
D, R = chans(draw), chans(rec); assert D.keys() == R.keys(), 'the two clips key different channels'
t_end = max(float(read(s['input']).max()) for s in D.values())
new = {'name': 'longbow shot', 'channels': [], 'samplers': []}; jump = 0.0
for k, sd in D.items():
    ti, vi = read(sd['input']).ravel(), read(sd['output']); tr, vr = read(R[k]['input']).ravel(), read(R[k]['output'])
    ip = 'STEP' if sd.get('interpolation') == 'STEP' and R[k].get('interpolation') == 'STEP' else 'LINEAR'   # STEP: Blender's constant channels
    if k[1] == 'rotation' and np.dot(vi[-1], vr[0]) < 0: vr = -vr   # the same rotation, the near hemisphere
    t = np.concatenate([ti, [t_end + HOLD], tr + t_end + HOLD]); v = np.vstack([vi, vi[-1:], vr])
    jump = max(jump, float(np.abs(vr[0] - vi[-1]).max()) if k[1] == 'rotation' else jump)
    typ = J['accessors'][sd['output']]['type']
    new['samplers'].append({'input': write(t.reshape(-1, 1), 'SCALAR', True), 'output': write(v, typ), 'interpolation': ip})
    new['channels'].append({'sampler': len(new['samplers']) - 1, 'target': {'node': k[0], 'path': k[1]}})
J['animations'].append(new)
dur = t_end + HOLD + max(float(read(s['input']).max()) for s in R.values())
J['buffers'] = [{'byteLength': len(BIN)}]
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('longbow shot: draw %.3f s + hold %.2f s + recoil -> %.3f s; release (recoil start) at %.3f s = %.3f of the clip; largest rotation step at the join %.3f (quaternion units)' % (t_end, HOLD, dur, t_end + HOLD, (t_end + HOLD) / dur, jump))
