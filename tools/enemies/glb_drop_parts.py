# BC6: take named mesh nodes out of a GLB (the node keeps its place in the hierarchy, it just draws nothing): her glasses.
# The mesh data stays in the buffer unreferenced (the game loads only meshes that nodes use).
#   python -I tools/enemies/glb_drop_parts.py <in.glb> <out.glb> <node name,...>
import json, struct, sys
SRC, OUT, NAMES = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
b = open(SRC, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); rest = b[20 + jl:]
for n in J['nodes']:
    if n.get('name') in NAMES: n.pop('mesh', None); n.pop('skin', None)
left = [n.get('name') for n in J['nodes'] if n.get('name') in NAMES and 'mesh' in n]
assert not left and all(any(n.get('name') == x for n in J['nodes']) for x in NAMES), ('missing or kept', NAMES)
js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
open(OUT, 'wb').write(struct.pack('<III', 0x46546C67, 2, 20 + len(js) + len(rest)) + struct.pack('<II', len(js), 0x4E4F534A) + js + rest)
print('dropped', NAMES, '->', OUT)
