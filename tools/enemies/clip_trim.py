# KG1: one clip of a GLB cut to its first `end` seconds and closed into a loop: the keys after `end` dropped (gallop_scan
# holds a frozen pose for its second half: the horse took one stride, then floated), the key at `end` set to the clip's
# first pose so the loop wraps without a pop. LINEAR / STEP samplers only; new accessors appended (shared ones untouched).
#   python -I tools/enemies/clip_trim.py <in.glb> <clip> <end s> <out.glb>
import json, struct, sys
import numpy as np
IN, CLIP, END, OUT = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
b = open(IN, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = bytearray(b[28 + jl:28 + jl + bl])
NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
def read(i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; assert a['componentType'] == 5126 and 'byteStride' not in v
    return np.frombuffer(bytes(BIN), np.float32, count=a['count'] * NUM[a['type']], offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], -1)
def add(arr, typ, mm=False):
    while len(BIN) % 4: BIN.append(0)
    d = np.ascontiguousarray(arr, np.float32).tobytes(); J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(d)}); BIN.extend(d)
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': 5126, 'count': len(arr), 'type': typ}
    if mm: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
an = next(a for a in J['animations'] if a['name'] == CLIP); done = {}
for s in an['samplers']:
    assert s.get('interpolation', 'LINEAR') in ('LINEAR', 'STEP')
    t = read(s['input'])[:, 0]; y = read(s['output']); assert len(y) == len(t)
    keep = t < END - 1e-4
    nt = np.r_[t[keep], END][:, None]; ny = np.r_[y[keep], y[:1]]
    if s['input'] not in done: done[s['input']] = add(nt, 'SCALAR', True)
    s['input'] = done[s['input']]; s['output'] = add(ny, J['accessors'][s['output']]['type'])
J['buffers'] = [{'byteLength': len(BIN)}]
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('trimmed %s to %.3f s (%d samplers) -> %s' % (CLIP, END, len(an['samplers']), OUT))
