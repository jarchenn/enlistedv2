# KN8: where a knight's strike sphere sits, for zombie.json "attacks" {"bone", "at"}: a point on the weapon, given in the
# local rest frame of the skin joint that carries it (glTF units), so the game poses it with that joint (zombie.cpp strikePoint).
#   python -I tools/enemies/strike_points.py <knight.glb> <weapon node> <carrying joint> head|blade|joint [fraction]
# head: the centroid of the weapon's far quarter (axe, mace, halberd: the end away from the right hand);
# blade: the point `fraction` (default 0.65) of the way from the hand to the far end along the weapon's axis;
# joint: the carrying joint's own origin (the flail ball's joint).
import json, struct, sys
import numpy as np
GLB, WNODE, BONE, MODE = sys.argv[1:5]; FRAC = float(sys.argv[5]) if len(sys.argv) > 5 else 0.65
b = open(GLB, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); BIN = b[28 + jl:]
N = J['nodes']; par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
def local(n):
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    M = np.eye(4); x, y, z, w = n.get('rotation', [0, 0, 0, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def world(i):
    M = local(N[i])
    while i in par: i = par[i]; M = local(N[i]) @ M
    return M
idx = {n.get('name'): i for i, n in enumerate(N)}
B = world(idx[BONE]); hand = world(idx['mixamorig:RightHand'])[:3, 3]
if MODE == 'joint': p = B[:3, 3]
else:
    wi = idx[WNODE]; W = world(wi); pts = []
    for pr in J['meshes'][N[wi]['mesh']]['primitives']:
        a = J['accessors'][pr['attributes']['POSITION']]; v = J['bufferViews'][a['bufferView']]
        P = np.frombuffer(BIN, np.float32, count=a['count'] * 3, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, 3).astype(float)
        if 'skin' in N[wi]: pts.append(P @ np.diag([1, 1, 1]))   # skinned: the mesh is in its bind space (rest world)
        else: pts.append((W[:3, :3] @ P.T).T + W[:3, 3])
    P = np.vstack(pts); c = P.mean(0); w_, V = np.linalg.eigh(np.cov((P - c).T)); ax = V[:, -1]; t = (P - hand) @ ax
    if abs(t.min()) > abs(t.max()): ax = -ax; t = -t   # the far end positive
    if MODE == 'head': p = P[t > t.max() - 0.25 * np.ptp(t)].mean(0)
    else: p = hand + ax * (FRAC * t.max())
loc = np.linalg.inv(B) @ np.append(p, 1)
print(json.dumps({'bone': BONE, 'at': [round(float(x), 4) for x in loc[:3]]}), '# %.2f m from the right hand at rest' % np.linalg.norm(p - hand))
