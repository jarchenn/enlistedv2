"""BR2: the backrooms wallpaper (content/textures/wallpaper_backrooms/color.png), generated: a pale yellow paper in
fine vertical stripes with a small pointed-arch / chevron motif repeating up each stripe, low contrast. One tile is
0.6 m (meta.json size); 512 px, 24 stripes. Original pattern in the style of a common striped damask paper.

    <python> -I tools/gen_wallpaper_backrooms.py"""
import math
import os
import struct
import zlib

N, STRIPES, MOTIF = 512, 24, 40   # px, stripes per tile, motif height (px)
OUT = os.path.join(os.path.dirname(__file__), "..", "content", "textures", "wallpaper_backrooms")


def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def shade(x, y):
    sw = N / STRIPES
    s = int(x // sw)
    u = (x - s * sw) / sw                       # 0..1 across the stripe
    v = ((y + (s % 2) * MOTIF / 2) % MOTIF) / MOTIF   # 0..1 up the motif, alternate stripes offset half a motif
    k = 0.0
    k += 0.045 if s % 2 else -0.015               # alternate stripe tone
    if u < 0.07 or u > 0.93:
        k -= 0.07                                # thin line between stripes
    # pointed arch: two arcs meeting at the top of the motif, an outline about 1.5 px wide
    cx = abs(u - 0.5) * 2                        # 0 centre .. 1 edge
    arch_h = 0.62 + 0.38 * math.sqrt(max(0.0, 1 - cx * cx))
    d = abs((1 - v) - (1 - arch_h)) * MOTIF
    if cx < 0.8 and d < 1.4:
        k -= 0.09
    if cx < 0.8 and v > arch_h:
        k += 0.02                                # a lighter fill inside the arch
    # chevron at the base of the motif
    cv = 0.12 + 0.18 * cx
    if abs(v - cv) * MOTIF < 1.1 and cx < 0.75:
        k -= 0.08
    # paper grain
    g = (math.sin(x * 12.9898 + y * 78.233) * 43758.5453) % 1.0
    k += (g - 0.5) * 0.03
    return k


def mottle(x, y):   # BR4: a soft tileable tonal variation (two wrapped sine fields), so the paper is not flat
    u, v = x / N * 2 * math.pi, y / N * 2 * math.pi
    return 0.025 * math.sin(u + 1.3 * math.sin(v)) + 0.018 * math.sin(2 * v + 0.7 * math.cos(u))


rows = []
for y in range(N):
    r = []
    for x in range(N):
        k = shade(x, y) + mottle(x, y)
        base = (0.86, 0.80, 0.42)
        r += [max(0, min(255, int(255 * (c + k)))) for c in base]
    rows.append(r)
os.makedirs(OUT, exist_ok=True)
png(os.path.join(OUT, "color.png"), N, N, rows)
with open(os.path.join(OUT, "meta.json"), "w", encoding="utf-8", newline="\n") as f:
    f.write('{\n "set": "wallpaper_backrooms",\n "id": "generated (tools/gen_wallpaper_backrooms.py)",\n "family": "interior (BR2)",\n "size": [0.6, 0.6],\n "license": "generated, original",\n "maps": ["color.png"]\n}\n')
print("ok")
