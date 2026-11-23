# FK3: one clip of a GLB closed into a seamless loop, for a short cut of raw mocap whose last pose is not its first (the
# Fallen Knight's 0.83 s idle popped 18 cm at the head every loop). Per channel: the first-to-last difference is taken
# off along the clip on a smoothstep (no pop, no speed step at the seam), the curve is smoothed with a periodic Gaussian
# (sigma `smooth` keys: single-frame mocap glitches), and the key times are stretched x `stretch` (a calmer idle).
# LINEAR samplers (STEP ones are only re-timed); new accessors appended (shared ones untouched).
#   python -I tools/enemies/clip_loop.py <in.glb> <clip> <out.glb> [stretch=1] [smooth=2]
#   python -I tools/enemies/clip_loop.py --seam <in.glb> <clip>     (prints the largest first-to-last key difference)
import json, struct, sys
import numpy as np
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
NUM = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}
def read(J, BIN, i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; assert a['componentType'] == 5126 and 'byteStride' not in v
    return np.frombuffer(bytes(BIN), np.float32, a['count'] * NUM[a['type']], v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], -1).astype(np.float64)
def seam(J, BIN, clip):
    an = next(a for a in J['animations'] if a['name'] == clip); worst = 0
    for s in an['samplers']:
        y = read(J, BIN, s['output']); d = y[-1] - y[0]
        if y.shape[1] == 4: d = min(np.abs(y[-1] - y[0]).max(), np.abs(y[-1] + y[0]).max())   # q and -q are one rotation
        worst = max(worst, float(np.abs(d).max()))
    return worst, read(J, BIN, an['samplers'][0]['input'])[-1, 0]
if sys.argv[1] == '--seam':
    J, BIN = load(sys.argv[2]); w, T = seam(J, BIN, sys.argv[3]); print('seam %.5f over %.2f s' % (w, T)); sys.exit(0)
IN, CLIP, OUT = sys.argv[1:4]; STRETCH = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0; SM = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
J, BIN = load(IN); print('before: seam %.5f over %.2f s' % seam(J, BIN, CLIP))
def add(arr, typ, mm=False):
    while len(BIN) % 4: BIN.append(0)
    d = np.ascontiguousarray(arr, np.float32).tobytes(); J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(d)}); BIN.extend(d)
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}
    if mm: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
an = next(a for a in J['animations'] if a['name'] == CLIP); times = {}
for s in an['samplers']:
    ip = s.get('interpolation', 'LINEAR'); assert ip in ('LINEAR', 'STEP')
    t = read(J, BIN, s['input'])[:, 0]; y = read(J, BIN, s['output']); n = len(t); quat = y.shape[1] == 4
    if ip == 'STEP':   # held values: only re-timed, the last key set to the first
        y[-1] = y[0]
        if s['input'] not in times: times[s['input']] = add(((t - t[0]) * STRETCH)[:, None], 'SCALAR', True)
        s['input'] = times[s['input']]; s['output'] = add(y, 'VEC4' if quat else 'VEC3'); continue
    if quat:
        for i in range(1, n):   # one sign along the clip, so the components can be blended
            if y[i] @ y[i - 1] < 0: y[i] = -y[i]
    u = (t - t[0]) / (t[-1] - t[0]); y = y - (u * u * (3 - 2 * u))[:, None] * (y[-1] - y[0])
    if SM > 0 and n > 8:   # periodic: keys 0 .. n-2 are one period (key n-1 repeats key 0)
        k = np.arange(-int(3 * SM), int(3 * SM) + 1); g = np.exp(-0.5 * (k / SM) ** 2); g /= g.sum(); p = y[:-1]
        p = sum(gw * np.roll(p, -kk, axis=0) for kk, gw in zip(k, g)); y = np.r_[p, p[:1]]
    if quat: y /= np.linalg.norm(y, axis=1, keepdims=True)
    if s['input'] not in times: times[s['input']] = add(((t - t[0]) * STRETCH)[:, None], 'SCALAR', True)
    s['input'] = times[s['input']]; s['output'] = add(y, 'VEC4' if quat else 'VEC3')
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(OUT, 'wb').write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN)) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(BIN), 0x004E4942) + BIN)
J, BIN = load(OUT); w, T = seam(J, BIN, CLIP); print('after: seam %.5f over %.2f s' % (w, T)); assert w < 1e-4
