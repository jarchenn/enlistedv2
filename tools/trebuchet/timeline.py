# TRE1: the supplied trebuchet GLB -> the bake's input: its clips "fire" then "reload" laid end to end as ONE animation
# ("timeline": the bake samples only the first clip, model.cpp), each moved to start at 0 (the source's strips sit at 1.9 s
# and 2.0 s), the stone's scale held at 1 (the source shrinks it to 0 at the release; the game hides the part instead: a
# zero scale would not survive the bake's per-sample rotation / translation blend). Prints the segments for the asset JSON.
#   python -I tools/trebuchet/timeline.py <trebuchet.glb> <out.glb>       (run through Blender's python for a supplied file)
import json, struct, sys
import numpy as np
SRC, OUT = sys.argv[1:3]
b = open(SRC, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = bytearray(b[28 + jl:28 + jl + bl])
N = J['nodes']; nid = {n['name']: i for i, n in enumerate(N)}
NC = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}
def acc(i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; n = NC[a['type']]
    return np.frombuffer(bytes(BIN), np.float32, a['count'] * n, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, n).astype(np.float32)
def put(arr, typ):
    arr = np.ascontiguousarray(arr, np.float32)
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': arr.nbytes}); BIN.extend(arr.tobytes())
    x = {'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': int(arr.shape[0]), 'type': typ}
    if typ == 'SCALAR': x['min'] = [float(arr.min())]; x['max'] = [float(arr.max())]
    J['accessors'].append(x); return len(J['accessors']) - 1
A = {a['name']: a for a in J['animations']}
segs = []; chans = {}; t0 = 0.0
for name in ('fire', 'reload'):
    a = A[name]; ins = [acc(a['samplers'][c['sampler']]['input'])[:, 0] for c in a['channels']]
    keyed = [t for t in ins if len(t) > 2] or ins   # the clip's span: its keyed channels (the held 2-key ones start at the strip's 0)
    lo = min(float(t.min()) for t in keyed); hi = max(float(t.max()) for t in keyed); dur = hi - lo
    for c, t in zip(a['channels'], ins):
        nd, path = c['target']['node'], c['target']['path']; y = acc(a['samplers'][c['sampler']]['output'])
        if N[nd]['name'] == 'Stone' and path == 'scale': y = np.ones_like(y)
        tt = np.clip(t - lo, 0, dur) + t0
        if len(tt) == 2 and a['samplers'][c['sampler']].get('interpolation') == 'STEP': tt = np.array([t0, t0 + dur], np.float32); y = np.stack([y[0], y[0]])   # a held value over the whole clip
        k = (nd, path); T, Y = chans.get(k, (np.zeros(0, np.float32), np.zeros((0, y.shape[1]), np.float32)))
        if len(T) and tt[0] <= T[-1]: tt = tt + (T[-1] - tt[0] + 1e-4)   # strictly increasing at the seam
        chans[k] = (np.concatenate([T, tt]), np.concatenate([Y, y]))
    segs.append({'clip': name, 'start': round(t0, 4), 'dur': round(dur, 4)}); t0 += dur + 1.0 / 30
an = {'name': 'timeline', 'channels': [], 'samplers': []}
for (nd, path), (T, Y) in sorted(chans.items()):
    an['samplers'].append({'input': put(T, 'SCALAR'), 'output': put(Y, 'VEC4' if Y.shape[1] == 4 else 'VEC3'), 'interpolation': 'LINEAR'})
    an['channels'].append({'sampler': len(an['samplers']) - 1, 'target': {'node': nd, 'path': path}})
J['animations'] = [an]
J.setdefault('asset', {}).setdefault('extras', {})['nmkTimeline'] = segs
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('segments', json.dumps(segs), 'total %.3f s' % t0, '->', OUT)
