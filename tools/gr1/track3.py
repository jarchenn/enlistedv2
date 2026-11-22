# the front post (blade + wings) per frame, against the rear peep ring (rigid with the receiver block from track.json)
import numpy as np, glob, json, sys
from PIL import Image
from scipy.signal import fftconvolve
fs = sorted(glob.glob(sys.argv[1] + '/f_*.png')); T = json.load(open(sys.argv[2]))
def gray(f): return np.asarray(Image.open(f).convert('L'), np.float32)
def ncc(win, t):
    tz = (t - t.mean()) / (t.std() + 1e-6); n = t.size
    num = fftconvolve(win, tz[::-1, ::-1], mode='valid')
    s1 = fftconvolve(win, np.ones_like(t), mode='valid'); s2 = fftconvolve(win * win, np.ones_like(t), mode='valid')
    return num / np.sqrt(np.maximum(s2 - s1 * s1 / n, 1e-6)) / np.sqrt(n)
P0 = gray(fs[12])[515:561, 945:979]            # frame 13: wings top 515, blade 535-555
PTOP = (962.0, 535.0)                           # blade top centre in that crop's frame
OFF = (-14.0, -104.0)                           # ring centre from the block centre at scale 1 (frame 8)
out = []
for i, f in enumerate(fs):
    g = gray(f); b = T[i]['gun']; s = b['scale']
    rx, ry = b['cx'] + OFF[0] * s, b['cy'] + OFF[1] * s
    R = int(70 * s); x0, y0 = int(rx - R), int(ry - R)
    w = g[y0:y0 + 2 * R + P0.shape[0], x0:x0 + 2 * R + P0.shape[1]]
    c = ncc(w, P0); k = np.unravel_index(np.argmax(c), c.shape)
    px = x0 + k[1] + (PTOP[0] - 945); py = y0 + k[0] + (PTOP[1] - 515)
    out.append({'frame': i + 1, 'ring': [rx, ry], 'post': [px, py], 'ncc': float(c[k]), 'rel': [(px - rx) / s, (py - ry) / s]})
    print(i + 1, 'ring %.0f %.0f post %.0f %.0f ncc %.2f  post-ring %.1f %.1f (block px at scale 1)' % (rx, ry, px, py, c[k], (px - rx) / s, (py - ry) / s))
json.dump(out, open(sys.argv[3], 'w'), indent=1)
