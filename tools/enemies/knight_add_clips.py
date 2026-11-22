# KN5: copy named animation clips from one glTF into another on the same skeleton (channels matched by node name), at the
# glTF level: Knight 5's axe pack has no death, so it takes knight_ss.glb's two sword-and-shield deaths (same Mixamo rig).
#   python -I tools/enemies/knight_add_clips.py <dst.glb> <src.glb> <out.glb> "<clip name>" ["<clip name>" ...]
# A channel whose node name the destination lacks is dropped (the source's sword / shield nodes).
import json, struct, sys
DST, SRC, OUT = sys.argv[1:4]; NAMES = sys.argv[4:]
def load(p):
    b = open(p, 'rb').read(); assert b[:4] == b'glTF'
    jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(DST); S, SB = load(SRC)
dn = {n.get('name'): i for i, n in enumerate(J['nodes'])}; sn = [n.get('name') for n in S['nodes']]
have = {a['name'] for a in J['animations']}
def copy_acc(ai):
    a = dict(S['accessors'][ai]); v = S['bufferViews'][a['bufferView']]
    assert 'byteStride' not in v or v['byteStride'] in (0, {'SCALAR': 4, 'VEC3': 12, 'VEC4': 16}[a['type']])
    o = v.get('byteOffset', 0) + a.pop('byteOffset', 0); n = a['count'] * {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']] * 4
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': n}); BIN.extend(SB[o:o + n])
    a['bufferView'] = len(J['bufferViews']) - 1; J['accessors'].append(a); return len(J['accessors']) - 1
for name in NAMES:
    assert name not in have, name + ' already in the destination'
    an = next(a for a in S['animations'] if a['name'] == name)
    new = {'name': name, 'channels': [], 'samplers': []}; dropped = 0
    for c in an['channels']:
        nm = sn[c['target']['node']]
        if nm not in dn: dropped += 1; continue
        s = an['samplers'][c['sampler']]
        new['samplers'].append({'input': copy_acc(s['input']), 'output': copy_acc(s['output']), 'interpolation': s.get('interpolation', 'LINEAR')})
        new['channels'].append({'sampler': len(new['samplers']) - 1, 'target': {'node': dn[nm], 'path': c['target']['path']}})
    J['animations'].append(new); print('added %s: %d channels (%d dropped: no such node)' % (name, len(new['channels']), dropped))
J['buffers'] = [{'byteLength': len(BIN)}]
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('wrote', OUT)
