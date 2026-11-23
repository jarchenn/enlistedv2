# OPF1: a Character Creator ("CC_Base_*") rigged GLB with its joints renamed to Mixamo's, so tools/bake_gunbody.py --skin can
# map it onto the soldier skeleton by name. Twist / share / face / toe bones keep their names (--skin sends them to their
# nearest mapped ancestor). Run tools/glb_normalize_rig.py first (--skin reads joint and vertex positions as the file has
# them: metres, y up, rest pose). Meshes whose material name contains one of [drop] are
# removed (lash cards: the game draws no alpha); every material is made OPAQUE; animations are dropped.
#   python -I tools/cc_to_mixamo_names.py <in.glb> <out.glb> [drop,drop...]
import json, struct, sys, re
IN, OUT = sys.argv[1:3]; DROP = [d.lower() for d in sys.argv[3].split(',')] if len(sys.argv) > 3 else []
b = open(IN, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); rest = b[20 + jl:]
M = {'Hip': 'Hips', 'Waist': 'Spine', 'Spine01': 'Spine1', 'Spine02': 'Spine2', 'NeckTwist01': 'Neck', 'Head': 'Head'}
for c, s in (('L', 'Left'), ('R', 'Right')):
    M.update({c + '_Clavicle': s + 'Shoulder', c + '_Upperarm': s + 'Arm', c + '_Forearm': s + 'ForeArm', c + '_Hand': s + 'Hand',
              c + '_Thigh': s + 'UpLeg', c + '_Calf': s + 'Leg', c + '_Foot': s + 'Foot', c + '_ToeBase': s + 'ToeBase'})
    for cf, mf in (('Thumb', 'Thumb'), ('Index', 'Index'), ('Mid', 'Middle'), ('Ring', 'Ring'), ('Pinky', 'Pinky')):
        for i in (1, 2, 3): M['%s_%s%d' % (c, cf, i)] = '%sHand%s%d' % (s, mf, i)
N = J['nodes']; hit = 0
for n in N:
    m = re.fullmatch(r'CC_Base_(.+?)(_\d+)?', n.get('name', ''))
    if m and m.group(1) in M: n['name'] = 'mixamorig:' + M[m.group(1)]; hit += 1
assert hit == len(M), (hit, len(M))
J.pop('animations', None)
keep = lambda mi: not any(d in (J['materials'][mi].get('name') or '').lower() for d in DROP)
for m in J['meshes']: m['primitives'] = [p for p in m['primitives'] if keep(p.get('material', 0))]
for n in N:
    if 'mesh' in n and not J['meshes'][n['mesh']]['primitives']: n.pop('mesh'); n.pop('skin', None)
for m in J['materials']: m.pop('alphaMode', None); m.pop('extensions', None)
J.pop('extensionsUsed', None); J.pop('extensionsRequired', None)
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(OUT, 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + len(rest)) + struct.pack('<II', len(js), 0x4E4F534A) + js + rest)
print('wrote', OUT, 'renamed', hit, 'joints; meshes kept', sum(1 for n in N if 'mesh' in n))
