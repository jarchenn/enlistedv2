"""BR4: generated textures for the Backrooms kit (all original, nothing copied from any model):
  carpet_backrooms      cut-pile carpet: fine pile noise, soft tonal mottling, faint lighter tufts (1.5 m tile)
  ceiling_backrooms     acoustic ceiling tile: fissured speckle (small dark pits and worm marks) (1.2 m tile)
  pooltile_backrooms    small square pool tiles with grout lines and a slight per-tile tone (1.2 m tile, 12 x 12)
  wallpaper_backrooms   (tools/gen_wallpaper_backrooms.py) is the striped paper
Each set: content/textures/<set>/color.png + meta.json. Tileable (the noise wraps). Stdlib only.

    <python> -I tools/gen_backrooms_textures.py"""
import math
import os
import random
import struct
import zlib

ROOT = os.path.join(os.path.dirname(__file__), "..", "content", "textures")


def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(px[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def value_noise(n, cells, seed):
    """Tileable smooth value noise, n x n, `cells` lattice cells across."""
    R = random.Random(seed)
    g = [[R.random() for _ in range(cells)] for _ in range(cells)]
    out = [0.0] * (n * n)
    for y in range(n):
        fy = y * cells / n
        y0 = int(fy); ty = fy - y0; ty = ty * ty * (3 - 2 * ty)
        for x in range(n):
            fx = x * cells / n
            x0 = int(fx); tx = fx - x0; tx = tx * tx * (3 - 2 * tx)
            a, b = g[y0 % cells][x0 % cells], g[y0 % cells][(x0 + 1) % cells]
            c, d = g[(y0 + 1) % cells][x0 % cells], g[(y0 + 1) % cells][(x0 + 1) % cells]
            out[y * n + x] = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty
    return out


def write_set(name, size_m, n, shade, note):
    px = bytearray(n * n * 3)
    for i, v in enumerate(shade):
        c = max(0, min(255, int(255 * v)))
        px[i * 3:i * 3 + 3] = bytes((c, c, c))
    d = os.path.join(ROOT, name)
    os.makedirs(d, exist_ok=True)
    png(os.path.join(d, "color.png"), n, n, px)
    with open(os.path.join(d, "meta.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n "set": "%s",\n "id": "generated (tools/gen_backrooms_textures.py)",\n "family": "interior (BR4)",\n "size": [%s, %s],\n "license": "generated, original",\n "note": "%s",\n "maps": ["color.png"]\n}\n' % (name, size_m, size_m, note))
    print(name, n)


def carpet():
    n = 512
    R = random.Random(11)
    mott, mid = value_noise(n, 6, 1), value_noise(n, 24, 2)
    out = []
    for i in range(n * n):
        pile = R.random()
        v = 0.62 + (mott[i] - 0.5) * 0.16 + (mid[i] - 0.5) * 0.08 + (pile - 0.5) * 0.16
        if pile > 0.985:
            v += 0.08   # a light tuft
        out.append(v)
    write_set("carpet_backrooms", 1.5, n, out, "cut pile, tonal mottling")


def ceiling():
    n = 512
    R = random.Random(21)
    base = value_noise(n, 10, 3)
    out = [0.78 + (b - 0.5) * 0.06 + (R.random() - 0.5) * 0.05 for b in base]
    for _ in range(2600):   # pits
        x, y, r = R.randrange(n), R.randrange(n), R.choice((1, 1, 1, 2))
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r:
                    out[((y + dy) % n) * n + (x + dx) % n] -= 0.16
    for _ in range(260):   # short worm marks
        x, y, a = R.uniform(0, n), R.uniform(0, n), R.uniform(0, math.pi * 2)
        for k in range(R.randint(4, 10)):
            a += R.uniform(-0.7, 0.7); x += math.cos(a); y += math.sin(a)
            out[int(y) % n * n + int(x) % n] -= 0.12
    write_set("ceiling_backrooms", 1.2, n, out, "fissured acoustic tile speckle")


def pooltile():
    n, k = 480, 12   # 12 x 12 tiles of 40 px
    R = random.Random(31)
    tone = [[R.uniform(-0.03, 0.03) for _ in range(k)] for _ in range(k)]
    out = []
    for y in range(n):
        for x in range(n):
            tx, ty = x % (n // k), y % (n // k)
            if tx < 2 or ty < 2:
                out.append(0.62)   # grout
            else:
                edge = min(tx, ty, n // k - 1 - tx, n // k - 1 - ty)
                out.append(0.9 + tone[y // (n // k)][x // (n // k)] + (0.03 if edge < 4 else 0) + (R.random() - 0.5) * 0.02)
    write_set("pooltile_backrooms", 1.2, n, out, "square pool tiles, grout")


if __name__ == "__main__":
    carpet()
    ceiling()
    pooltile()
