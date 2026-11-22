# knight2: the user's finished knight (Knight_sword_shield.glb: Mixamo rig, 51 sword-and-shield clips, the sword a rigid child of
# mixamorig:RightHand, the shield of mixamorig:LeftForeArm) -> content/characters/knight_ss.glb for the Knights enemy type.
#   python -I tools/enemies/knight2_prep.py <Knight_sword_shield.glb> <out.glb>
# - root motion off: a clip whose hips travel more than 20 cm across the floor (or swing more than 40 cm) has the hips' floor
#   coordinates pinned to its first key (the AI moves the body; the game plays clips in place). The deaths keep their fall.
# - asset.extras.nmkNormals: the game loads the normal maps (the armour's plates and chain mail); the credits go in
#   asset.extras. Everything else is byte for byte the file as supplied.
import json, struct, sys
import numpy as np
SRC, OUT = sys.argv[1], sys.argv[2]   # optional 3rd argument: the credits text (KN3: Knight_3.glb)
CREDITS = sys.argv[3] if len(sys.argv) > 3 else None
b = open(SRC, 'rb').read()
assert b[:4] == b'glTF'
jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
BIN = bytearray(b[28 + jl:28 + jl + bl])
nodes = J['nodes']; hips = next(i for i, n in enumerate(nodes) if n.get('name', '').endswith(':Hips'))
# the hips' floor axes: the two that the walk clip's travel and the strafe's travel lie on (Mixamo under a Z-up armature: x, y)
FLOOR = (0, 1)
def view(acc):
    a = J['accessors'][acc]; v = J['bufferViews'][a['bufferView']]; n = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
    assert a['componentType'] == 5126 and v.get('byteStride', 4 * n) == 4 * n
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    return np.frombuffer(BIN, np.float32, count=a['count'] * n, offset=off).reshape(-1, n), a
used = {}
for an in J['animations']:
    for c in an['channels']:
        s = an['samplers'][c['sampler']]; used[s['output']] = used.get(s['output'], 0) + 1
pinned = []
for an in J['animations']:
    if 'death' in an['name']: continue
    for c in an['channels']:
        if c['target'].get('node') != hips or c['target']['path'] != 'translation': continue
        acc = an['samplers'][c['sampler']]['output']; assert used[acc] == 1, 'shared accessor'
        V, a = view(acc); H = V[:, FLOOR]
        travel = float(np.linalg.norm(H[-1] - H[0])); swing = float(np.linalg.norm(H.max(0) - H.min(0)))
        if travel > 20 or swing > 40:   # centimetres (the armature node scales by 0.01)
            V[:, FLOOR] = H[0]; pinned.append('%s (%.0f cm)' % (an['name'], travel))
            a['min'] = [float(x) for x in V.min(0)]; a['max'] = [float(x) for x in V.max(0)]
J['asset'].setdefault('extras', {}).update({'nmkNormals': True,
    'credits': CREDITS} if CREDITS else {'nmkNormals': True,
    'credits': 'Knight (Final) by Daniel Gryningstjerna (sketchfab.com/dangry), Chevalier Sword by rubenve (sketchfab.com/rubenve), '
               'Medieval Shield by Seth Santos (sketchfab.com/Seth7Santos): CC-BY-4.0; the sword after a concept by Guillem Dauden; clips: Adobe Mixamo; assembled by the user'})
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
BIN += b'\0' * (-len(BIN) % 4)
out = b'glTF' + struct.pack('<II', 2, 12 + 8 + len(js) + 8 + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN)
open(OUT, 'wb').write(out)
# check: every non-death clip with travel left now moves the hips less than 20 cm across the floor
for an in J['animations']:
    if 'death' in an['name']: continue
    for c in an['channels']:
        if c['target'].get('node') == hips and c['target']['path'] == 'translation':
            V, _ = view(an['samplers'][c['sampler']]['output']); H = V[:, FLOOR]
            assert np.linalg.norm(H[-1] - H[0]) <= 20, an['name']
print('pinned %d clips: %s' % (len(pinned), ', '.join(pinned)))
print('wrote', OUT, len(out) // 1024, 'KB')
