# curves.json (curves.py) -> m1_garand.json fireCurve: KF1 rows + GR1 columns at 60 fps (linear between the 30 fps samples)
import json, sys
import numpy as np
R = json.load(open(sys.argv[1])); p = sys.argv[2]
t = np.array([0.0] + [r['t'] for r in R])
ch = lambda k, lim=None: np.array([0.0] + [(r.get(k) or 0.0) if lim is None or i < lim else 0.0 for i, r in enumerate(R)])
back, up, pitch, yaw = ch('back'), ch('up'), ch('pitch'), ch('yaw')
camP = ch('camP', 5)                                    # the kick and its snap back; after it the recorder's own pull-down
roll = np.array([0.0] + [r['camR'] if 0 < i < 11 else (0.3 if i == 0 else 0.0) for i, r in enumerate(R)])   # frame 0's reading is flash noise
handle = np.zeros_like(t); handle[1:3] = 0.08          # the rod back on the flash frame and the next
fps = 60; n = int(round(t[-1] * fps)) + 1; rows = []
for k in range(n):
    x = k / fps; v = lambda a: float(np.interp(x, t, a))
    rows.append([round(v(back), 4), round(v(up), 4), 0.0, 0.0, round(v(camP), 2), 0.0, round(v(pitch), 2), round(v(yaw), 2), round(v(roll), 2), round(v(handle), 4)])
rows[-1] = [0.0] * 10
j = json.load(open(p, encoding='utf-8')); j['fireCurve']['fps'] = fps; j['fireCurve']['frames'] = rows
open(p, 'w', newline='', encoding='utf-8').write(json.dumps(j, ensure_ascii=False, separators=(',', ':')) + '\n')
print(n, 'rows; peak back %.4f pitch %.2f camera %.2f' % (max(r[0] for r in rows), max(r[6] for r in rows), max(r[4] for r in rows)))
