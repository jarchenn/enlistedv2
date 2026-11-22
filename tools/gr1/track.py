# frame-by-frame tracking of the reference: camera shift (background) and the rear-sight block (shift + scale)
import numpy as np, glob, json, sys
from PIL import Image
from scipy import ndimage
fs = sorted(glob.glob(sys.argv[1] + '/f_*.png'))
def gray(f): return np.asarray(Image.open(f).convert('L'), np.float32)
def phase(a, b):   # shift of b relative to a (subpixel by parabola)
    A = np.fft.fft2(a - a.mean()); B = np.fft.fft2(b - b.mean())
    R = A.conj() * B; R /= np.abs(R) + 1e-6; r = np.real(np.fft.ifft2(R))
    y, x = np.unravel_index(np.argmax(r), r.shape)
    def sub(c, m, axis):
        i0, i1 = (c - 1) % m, (c + 1) % m
        v0, v1, v = (r[i0, x], r[i1, x], r[y, x]) if axis == 0 else (r[y, i0], r[y, i1], r[y, x])
        d = v0 - 2 * v + v1; return c + (0.5 * (v0 - v1) / d if abs(d) > 1e-9 else 0)
    dy, dx = sub(y, r.shape[0], 0), sub(x, r.shape[1], 1)
    if dy > r.shape[0] / 2: dy -= r.shape[0]
    if dx > r.shape[1] / 2: dx -= r.shape[1]
    return dx, dy
ref = gray(fs[7])   # frame 8: settled, before the first shot
# background patches clear of the gun: left wall/window post and the right window post (full-res coords)
BL = (slice(40, 520), slice(80, 560)); BR = (slice(40, 520), slice(1460, 1900))
# rear sight block template: frame 8 around the ears/aperture
TY0, TY1, TX0, TX1 = 500, 780, 720, 1220
tpl = ref[TY0:TY1, TX0:TX1]; tg = ndimage.gaussian_gradient_magnitude(tpl, 1.5)
out = []
for i, f in enumerate(fs):
    g = gray(f)
    lx, ly = phase(ref[BL], g[BL]); rx, ry = phase(ref[BR], g[BR])
    cam = {'dx': (lx + rx) / 2, 'dy': (ly + ry) / 2, 'roll_px': (ry - ly)}   # roll: right vs left vertical shift
    # gun: search scale 0.85..1.7 and shift: correlate the gradient template against the gradient image
    G = ndimage.gaussian_gradient_magnitude(g, 1.5)
    best = None
    for s in np.arange(0.85, 1.75, 0.025):
        t = ndimage.zoom(tg, s, order=1); th, tw = t.shape
        cy, cx = (TY0 + TY1) / 2, (TX0 + TX1) / 2
        y0 = int(cy - th / 2) - 160; x0 = int(cx - tw / 2) - 200
        y0 = max(0, y0); x0 = max(0, x0)
        win = G[y0:min(G.shape[0], y0 + th + 320), x0:min(G.shape[1], x0 + tw + 400)]
        if win.shape[0] < th or win.shape[1] < tw: continue
        # normalised cross-correlation via FFT
        from scipy.signal import fftconvolve
        tz = (t - t.mean()) / (t.std() + 1e-6)
        num = fftconvolve(win, tz[::-1, ::-1], mode='valid')
        s1 = fftconvolve(win, np.ones_like(t), mode='valid'); s2 = fftconvolve(win * win, np.ones_like(t), mode='valid')
        n = t.size; var = np.maximum(s2 - s1 * s1 / n, 1e-6)
        ncc = num / np.sqrt(var) / np.sqrt(n)
        k = np.unravel_index(np.argmax(ncc), ncc.shape); v = ncc[k]
        if best is None or v > best[0]: best = (v, s, x0 + k[1] + tw / 2, y0 + k[0] + th / 2)
    v, s, gx, gy = best
    out.append({'frame': i + 1, 'cam': cam, 'gun': {'scale': float(s), 'cx': float(gx), 'cy': float(gy), 'ncc': float(v)}})
    print(i + 1, 'cam %.1f %.1f roll %.1f' % (cam['dx'], cam['dy'], cam['roll_px']), 'gun s %.3f c %.0f %.0f ncc %.2f' % (s, gx, gy, v), flush=True)
json.dump(out, open(sys.argv[2], 'w'), indent=1)
