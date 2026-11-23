# EK9: clips copied from one model to another on the same rig and bind pose, by joint name, at the glTF level
#   python -I clip_copy.py <from.glb> <to.glb> <out.glb> <clip> ...
import json, struct, sys
import numpy as np
SRC, DST, OUT = sys.argv[1:4]; CLIPS = sys.argv[4:]
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
SJ, SB = load(SRC); J, BIN = load(DST)
byname = {n.get('name'): i for i, n in enumerate(J['nodes'])}
def copy_acc(ai):
    a = SJ['accessors'][ai]; v = SJ['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0); data = bytes(SB[off: off + a['count'] * k * 4])
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    a2 = {kk: vv for kk, vv in a.items() if kk not in ('bufferView', 'byteOffset')}; a2['bufferView'] = len(J['bufferViews']) - 1
    J['accessors'].append(a2); return len(J['accessors']) - 1
for name in CLIPS:
    an = next(a for a in SJ['animations'] if a['name'] == name); new = {'name': name, 'channels': [], 'samplers': []}; done = {}; miss = 0
    for c in an['channels']:
        tn = SJ['nodes'][c['target']['node']].get('name')
        if tn not in byname: miss += 1; continue
        s = an['samplers'][c['sampler']]
        if c['sampler'] not in done:
            new['samplers'].append({'input': copy_acc(s['input']), 'output': copy_acc(s['output']), 'interpolation': s.get('interpolation', 'LINEAR')}); done[c['sampler']] = len(new['samplers']) - 1
        new['channels'].append({'sampler': done[c['sampler']], 'target': {'node': byname[tn], 'path': c['target']['path']}})
    J['animations'] = [a for a in J['animations'] if a['name'] != name] + [new]; print('clip', name, len(new['channels']), 'channels', miss, 'skipped (no such joint)')
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', OUT)
