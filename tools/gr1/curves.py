# per-shot ADS recoil curves from the tracks (two shots averaged, aligned on the first frame the receiver jumps)
import json, math, numpy as np, sys
R = sys.argv[1]
T = json.load(open(R + '/track.json')); P = json.load(open(R + '/track3.json')); C = json.load(open(R + '/track2.json'))
F_VM = 540 / math.tan(math.radians(30))   # our view-model projection at 1080p (vmFov 60)
F_W = 540 / math.tan(math.radians(29))    # our ADS world projection (worldFov 58)
D0, SR = 0.124, 1.0728 - 0.3712           # eye relief to the peep that gives the reference's 467 px knob-to-knob at 1080p; sight radius
DK = D0 - 0.0102                          # the knobs' near faces (what the scale tracker follows) are 10.2 mm in front of the peep
OFF = (-14.0, -104.0)
BAD_POST = {1, 2, 27, 35}                 # the post template latched onto the soldier / the house
def frame(i):
    g = T[i - 1]['gun']; s = g['scale']; back = DK * (1 - 1 / s); z = D0 - back
    rx, ry = g['cx'] + OFF[0] * s, g['cy'] + OFF[1] * s
    up = -(ry - 540) * z / F_VM; side = (rx - 960) * z / F_VM
    p = P[i - 1]
    if i in BAD_POST: a = b = None
    else:
        px, py = p['post']
        a = math.degrees(((-(py - 540)) * (z + SR) / F_VM - up) / SR)      # muzzle up about the rear aperture
        b = math.degrees((((px - 960)) * (z + SR) / F_VM - side) / SR)     # muzzle right
    cam = C[i - 1]['cam']
    cy = 0.5 * (cam[0][1] + cam[1][1]); cx = 0.5 * (cam[0][0] + cam[1][0])
    roll = math.degrees(math.atan2(cam[1][1] - cam[0][1], 550.0))          # the right patch dropping more: clockwise
    return dict(back=back, up=up, side=side, pitch=a, yaw=b, camP=math.degrees(math.atan(cy / F_W)), camY=-math.degrees(math.atan(cx / F_W)), camR=roll)
U = [9, 29]; K = 14
rest = {k: np.mean([frame(i)[k] for i in (U[0] + 12, U[0] + 13, U[1] + 12, U[1] + 13) if frame(i)[k] is not None]) for k in ('back', 'up', 'side', 'pitch', 'yaw')}
camRest = [{k: frame(b)[k] for k in ('camP', 'camY', 'camR')} for b in (8, 27)]   # each shot's camera just before it
rows = []
for k in range(K):
    vals = {}
    for n, u in enumerate(U):
        f = frame(u + k)
        for c in ('back', 'up', 'side', 'pitch', 'yaw'):
            if f[c] is not None: vals.setdefault(c, []).append(f[c] - rest[c])
        for c in ('camP', 'camY', 'camR'): vals.setdefault(c, []).append(f[c] - camRest[n][c])
    rows.append({c: float(np.mean(v)) for c, v in vals.items()} | {'t': (k + 0.5) / 30, 'raw': vals})
for r in rows: print('t %.3f  back %+.4f up %+.4f side %+.4f | pitch %+.2f yaw %+.2f | cam pitch %+.2f yaw %+.2f roll %+.2f' % (r['t'], r['back'], r['up'], r['side'], r.get('pitch', 0), r.get('yaw', 0), r['camP'], r['camY'], r['camR']))
print('raw pitch per shot:', [[round(x, 2) for x in r['raw'].get('pitch', [])] for r in rows])
print('raw camP per shot:', [[round(x, 2) for x in r['raw']['camP']] for r in rows])
json.dump(rows, open(R + '/curves.json', 'w'), indent=1)
