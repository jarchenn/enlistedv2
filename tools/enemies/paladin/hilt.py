# the sword's guard and pommel in bind space (the sword node's primitives: axis ends, the guard the widest point)
import json, struct, sys
import numpy as np
b = open(sys.argv[1], 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); B = b[28 + jl:]; P = []
for n in J['nodes']:
    if n.get('name') == 'sword':
        for p in J['meshes'][n['mesh']]['primitives']:
            a = J['accessors'][p['attributes']['POSITION']]; v = J['bufferViews'][a['bufferView']]
            P.append(np.frombuffer(B, np.float32, a['count'] * 3, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, 3))
P = np.concatenate(P).astype(float); c = P.mean(0); _, _, V = np.linalg.svd(P - c, full_matrices=False); d = V[0]; t = (P - c) @ d
r = np.linalg.norm((P - c) - np.outer(t, d), axis=1); tg = t[np.argmax(r)]; tp = t.max() if abs(t.max() - tg) < abs(t.min() - tg) else t.min()
print(json.dumps((c + d * tg).round(4).tolist(), separators=(',', ':')), json.dumps((c + d * tp).round(4).tolist(), separators=(',', ':')))
