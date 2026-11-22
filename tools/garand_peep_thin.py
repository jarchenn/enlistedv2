"""GR3 (the user): a thinner rear peep ring on the baked Garand (content/weapons/models/garand2): the hole widened to the
reference video's proportion (hole / outside 0.69, ours 0.50), the outside unchanged. The ring is a torus about the peep
centre (sights.rear x, y) at z 0.362-0.387 m; its vertices are moved radially in x-y: r' = ro - (ro - r) k, k = (ro - ri') / (ro - ri).
Like garand_oprod_split.py it edits the bake (the source GLB is not in the repo): re-run both after a re-import. Idempotent:
a ring already at the target proportion is left alone.
Usage: python tools/garand_peep_thin.py content/weapons/models/garand2 [hole/outside, default 0.69]"""
import struct, sys
import numpy as np
d = sys.argv[1]; target = float(sys.argv[2]) if len(sys.argv) > 2 else 0.69
raw = bytearray(open(d + '/mesh.bin', 'rb').read())
magic, nv, ni = struct.unpack('<3I', raw[:12]); assert magic == 0x31574D4E
V = np.frombuffer(bytes(raw[12:12 + nv * 48]), np.float32).reshape(nv, 12).copy()
cx, cy = -0.0005, 0.0956   # m1_garand.json sights.rear
P = V[:, :3]
near = (P[:, 2] > 0.36) & (P[:, 2] < 0.39) & (np.hypot(P[:, 0] - cx, P[:, 1] - cy) < 0.0047)
r = np.hypot(P[near, 0] - cx, P[near, 1] - cy)
ri, ro = r.min(), r.max()
assert 0.0015 < ri < 0.0035 and 0.0038 < ro < 0.0046 and near.sum() > 150, (ri, ro, near.sum())
if abs(ri / ro - target) < 0.01: print('already %.2f' % (ri / ro)); sys.exit(0)
k = (ro - target * ro) / (ro - ri)
nr = ro - (ro - r) * k
dx, dy = P[near, 0] - cx, P[near, 1] - cy
V[near, 0] = cx + dx / r * nr; V[near, 1] = cy + dy / r * nr
raw[12:12 + nv * 48] = V.astype(np.float32).tobytes()
open(d + '/mesh.bin', 'wb').write(raw)
print('ring verts %d: hole radius %.2f -> %.2f mm, outside %.2f mm (hole / outside %.2f -> %.2f)' % (near.sum(), ri * 1000, target * ro * 1000, ro * 1000, ri / ro, target))
