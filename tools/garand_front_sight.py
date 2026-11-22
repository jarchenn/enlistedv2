"""GR3 (the user): the Garand's front sight in the reference video's proportions to the peep ring (docs/garand_ads_fire_reference.md):
the wings and the block 3.9 mm lower (the wing tips ~5 px over the blade tip, the block top 11 px under it, at the ADS
zoom), the block narrowed to 0.78 (16 px) easing back to full width at the wing tips (their span already matched), and the
blade stretched down into the block and 1.3x wider. The blade tip stays at sights.front (the aim is unchanged); the base
clamp around the barrel stays put. Edits the bake like garand_oprod_split.py / garand_peep_thin.py (re-run after a
re-import); idempotent (model.json gr3_front).
Usage: python tools/garand_front_sight.py content/weapons/models/garand2"""
import json, struct, sys
import numpy as np
d = sys.argv[1]
M = json.load(open(d + '/model.json'))
if M.get('gr3_front'): print('already reshaped'); sys.exit(0)
raw = bytearray(open(d + '/mesh.bin', 'rb').read())
magic, nv, ni = struct.unpack('<3I', raw[:12]); assert magic == 0x31574D4E
V = np.frombuffer(bytes(raw[12:12 + nv * 48]), np.float32).reshape(nv, 12).copy(); P = V[:, :3]
X0, TIP = -0.0004, 0.0974          # sights.front: the blade tip
fs = (P[:, 2] > 1.06) & (P[:, 2] < 1.081) & (P[:, 1] > 0.045) & (np.abs(P[:, 0] - X0) < 0.011)   # the front-sight island (z 1.062-1.0805)
post = fs & (np.abs(P[:, 0] - X0) < 0.0009) & (P[:, 1] > 0.0915)
body = fs & ~post
yb = P[post, 1].min(); assert 0.091 < yb < 0.094 and abs(P[post, 1].max() - TIP) < 0.0003 and post.sum() >= 8, (yb, P[post, 1].max(), post.sum())
DROP, SQ = 0.0039, 0.78
y = P[body, 1]
w = np.clip((y - 0.070) / 0.015, 0, 1)                                   # the base clamp stays, the sight above it moves
sx = 1 - (1 - SQ) * np.clip((y - 0.075) / 0.010, 0, 1) * (1 - np.clip((y - 0.0985) / 0.0064, 0, 1))   # narrow the block, not the wing tips
V[body, 0] = X0 + (P[body, 0] - X0) * sx; V[body, 1] = y - DROP * w
nb = yb - DROP - 0.0016                                                  # the blade's foot, inside the lowered block
V[post, 1] = TIP - (TIP - P[post, 1]) * (TIP - nb) / (TIP - yb); V[post, 0] = X0 + (P[post, 0] - X0) * 1.3
raw[12:12 + nv * 48] = V.astype(np.float32).tobytes(); open(d + '/mesh.bin', 'wb').write(raw)
M['gr3_front'] = 'front sight reshaped by tools/garand_front_sight.py'; json.dump(M, open(d + '/model.json', 'w'), indent=1)
print('front sight: %d body verts lowered %.1f mm, block x %.2f; %d blade verts from %.4f to %.4f..%.4f' % (body.sum(), DROP * 1000, SQ, post.sum(), yb, nb, TIP))
