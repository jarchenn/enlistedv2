"""Lasso ground paint for authored maps (src/ground_paint.cpp, MapDoc groundPaint in src/mapdoc.cpp).

0.5 m texels in 32 x 32-texel chunks (16 m), keyed by chunk (cx, cz) = floor(world / 16 m). Each texel is 4 bytes:
top type, top weight, under type, under weight (types in content/ground_paint.json order). Chunks are written like
mapdoc.cpp paintEncode: runs of identical texels as (count <= 255, 4 bytes), then standard base64."""
import base64
import math

from .geom import in_poly, poly_bounds, seg_dist, polyline_dist, fbm, smoothstep

TYPES = ["grass", "marsh", "dirt", "mud", "gravel", "sand", "cobbles", "rock", "dry_grass", "ploughed", "leaf_litter",
         "scorched", "pebbles"]
TEXEL, CHUNK = 0.5, 32


class GroundPaint:
    def __init__(self):
        self.chunks = {}

    def _texel(self, i, j):
        cx, cz = i // CHUNK, j // CHUNK
        c = self.chunks.get((cx, cz))
        if c is None:
            c = self.chunks[(cx, cz)] = bytearray(CHUNK * CHUNK * 4)
        return c, ((j - cz * CHUNK) * CHUNK + (i - cx * CHUNK)) * 4

    def dab(self, i, j, t, w):
        """One texel, blended as the lasso stroke does: same type strengthens, a full-weight stroke replaces, a partial
        one goes over the old layer (which shows through)."""
        if w <= 0.0:
            return
        c, k = self._texel(i, j)
        tt, tw = c[k], c[k + 1]
        q = int(round(min(1.0, w) * 255))
        if tt == t or tw == 0:
            a = tw / 255.0 if tt == t else 0.0
            c[k], c[k + 1] = t, max(tw if tt == t else 0, int(round((a + w * (1 - a)) * 255)))
            if c[k + 2] == t:
                c[k + 2] = c[k + 3] = 0
        elif w > 0.996:
            c[k:k + 4] = bytes((t, 255, 0, 0))
        else:
            c[k + 2], c[k + 3], c[k], c[k + 1] = tt, tw, t, q

    def poly(self, kind, poly, weight=1.0, edge=1.5, ragged=0.0, seed=0, mask=None):
        """Fill a polygon with `kind`, fading in over `edge` m inside its outline; `ragged` (m) wobbles the edge;
        mask(x, z) -> False leaves a texel unpainted."""
        t = TYPES.index(kind)
        x0, z0, x1, z1 = poly_bounds(poly)
        n = len(poly)
        for j in range(int(math.floor(z0 / TEXEL)), int(math.ceil(z1 / TEXEL)) + 1):
            z = (j + 0.5) * TEXEL
            for i in range(int(math.floor(x0 / TEXEL)), int(math.ceil(x1 / TEXEL)) + 1):
                x = (i + 0.5) * TEXEL
                if not in_poly((x, z), poly) or (mask is not None and not mask(x, z)):
                    continue
                w = weight
                if edge > 0 or ragged > 0:
                    d = min(seg_dist((x, z), poly[a], poly[(a + 1) % n])[0] for a in range(n))
                    d += ragged * fbm(x / 6.0, z / 6.0, seed, 2)
                    w *= smoothstep(0.0, max(edge, 1e-3), d)
                self.dab(i, j, t, w)

    def line(self, kind, pts, width, weight=1.0, edge=0.8, mask=None):
        t = TYPES.index(kind)
        x0, z0, x1, z1 = poly_bounds(pts)
        h = width * 0.5
        for j in range(int(math.floor((z0 - h) / TEXEL)), int(math.ceil((z1 + h) / TEXEL)) + 1):
            z = (j + 0.5) * TEXEL
            for i in range(int(math.floor((x0 - h) / TEXEL)), int(math.ceil((x1 + h) / TEXEL)) + 1):
                x = (i + 0.5) * TEXEL
                d = polyline_dist((x, z), pts)[0]
                if d < h and (mask is None or mask(x, z)):
                    self.dab(i, j, t, weight * smoothstep(0.0, max(edge, 1e-3), h - d))

    def to_json(self):
        out = []
        for (cx, cz), c in sorted(self.chunks.items()):
            if not any(c[k + 1] or c[k + 3] for k in range(0, len(c), 4)):
                continue
            r = bytearray()
            k = 0
            while k < len(c):
                m = k + 4
                while m < len(c) and m - k < 255 * 4 and c[m:m + 4] == c[k:k + 4]:
                    m += 4
                r.append((m - k) // 4)
                r += c[k:k + 4]
                k = m
            out.append({"c": [cx, cz], "d": base64.b64encode(bytes(r)).decode("ascii")})
        return {"texel": TEXEL, "chunk": CHUNK, "chunks": out}


def _selfcheck():
    """Round trip against mapdoc.cpp paintDecode: runs of 5 bytes expand to exactly 32 x 32 texels."""
    p = GroundPaint()
    p.poly("ploughed", [(0, 0), (20, 0), (20, 9), (0, 9)], edge=1.0)
    p.dab(-3, -5, TYPES.index("mud"), 0.5)
    for ch in p.to_json()["chunks"]:
        r = base64.b64decode(ch["d"])
        n = sum(r[k] for k in range(0, len(r), 5))
        assert len(r) % 5 == 0 and n == CHUNK * CHUNK, (ch["c"], len(r), n)
    c, k = p._texel(4, 4)
    assert c[k] == TYPES.index("ploughed") and c[k + 1] == 255
    return True


_selfcheck()
