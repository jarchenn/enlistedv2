# ring (rear peep) + front post per frame, and the camera from the scene above the gun
import numpy as np, glob, json, sys
from PIL import Image
from scipy import ndimage
from scipy.signal import fftconvolve
fs = sorted(glob.glob(sys.argv[1] + '/f_*.png')); T = json.load(open(sys.argv[2]))
def gray(f): return np.asarray(Image.open(f).convert('L'), np.float32)
def ncc(win, t):
    tz = (t - t.mean()) / (t.std() + 1e-6); n = t.size
    num = fftconvolve(win, tz[::-1, ::-1], mode='valid')
    s1 = fftconvolve(win, np.ones_like(t), mode='valid'); s2 = fftconvolve(win * win, np.ones_like(t), mode='valid')
    return num / np.sqrt(np.maximum(s2 - s1 * s1 / n, 1e-6)) / np.sqrt(n)
def match(img, t, cx, cy, rad):
    th, tw = t.shape; x0 = int(cx - tw / 2 - rad); y0 = int(cy - th / 2 - rad)
    w = img[y0:y0 + th + 2 * rad, x0:x0 + tw + 2 * rad]; c = ncc(w, t)
    k = np.unravel_index(np.argmax(c), c.shape); y, x = k
    def sp(a, b, v):  # subpixel parabola
        d = a - 2 * v + b; return 0.5 * (a - b) / d if abs(d) > 1e-9 else 0
    dy = sp(c[y - 1, x], c[y + 1, x], c[y, x]) if 0 < y < c.shape[0] - 1 else 0
    dx = sp(c[y, x - 1], c[y, x + 1], c[y, x]) if 0 < x < c.shape[1] - 1 else 0
    return x0 + x + dx + tw / 2, y0 + y + dy + th / 2, float(c[y, x])
ref = gray(fs[7])
RING = (930.0, 535.0)   # frame 8 ring centre (full res), refined below
# refine: ring template = annulus region in frame 8
rt = ref[int(RING[1]) - 75:int(RING[1]) + 75, int(RING[0]) - 75:int(RING[0]) + 75]
# post template (frame 8): the dark blade with its wings, just under the ring centre
PT = None
out = []
# camera: two textured regions above the gun (the window post / house roof and the trees), gradient NCC
cams = [(700, 220, 100, 110), (1250, 220, 140, 110)]   # cx, cy, half-w, half-h in frame 8
camT = [ndimage.gaussian_gradient_magnitude(ref[cy - hh:cy + hh, cx - hw:cx + hw], 1.0) for cx, cy, hw, hh in cams]
for i, f in enumerate(fs):
    g = gray(f); G = ndimage.gaussian_gradient_magnitude(g, 1.0)
    s = T[i]['gun']['scale']
    # ring: scale the template by the block scale, search around the block-predicted centre
    pcx = T[i]['gun']['cx'] + (RING[0] - T[7]['gun']['cx']) * s; pcy = T[i]['gun']['cy'] + (RING[1] - T[7]['gun']['cy']) * s
    t = ndimage.zoom(rt, s, order=1); rx, ry, rv = match(g, t, pcx, pcy, 40)
    cam = []
    for (cx, cy, hw, hh), ct in zip(cams, camT):
        x, y, v = match(G, ct, cx, cy, 45); cam.append((x - cx, y - cy, v))
    out.append({'frame': i + 1, 'ring': [rx, ry, rv], 'ringR': 52 * s, 'cam': cam})
json.dump(out, open(sys.argv[3], 'w'), indent=1)
for o in out: print(o['frame'], 'ring %.1f %.1f (%.2f)' % tuple(o['ring']), ' cam ' + ' | '.join('%.1f %.1f (%.2f)' % c for c in o['cam']))
