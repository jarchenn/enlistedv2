"""CP step 6 part 2 (corpus 08 s10.1): a shared grunge-mask library as texture-set layers in the existing arrays (no new
sampler). Twelve masks, three per set (R, G, B), four sets content/textures/mask_lib_0..3, built only from CC0 sets
already in content/textures (ambientCG overlays and stained / leaking / damaged materials; the Texturelabs images stay
local). Each mask: the source's luminance, high-passed for full materials (their structure, not their colour), stretched to
0..1 between its 2nd and 98th percentile, stored sRGB-encoded so the sRGB colour array decodes it back to the mask.
  python -I tools/props/mask_library.py"""
import json
import os

import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEX = os.path.join(ROOT, "content", "textures")
N = 1024
LIB = [  # (set, high-pass radius in px at 1024, 0 = the luminance as it is), three per output set
    [("surface_imperfections_003", 0), ("smear_008", 0), ("surface_imperfections_014", 0)],
    [("surface_imperfections_015", 0), ("surface_imperfections_017", 0), ("surface_imperfections_020", 0)],
    [("rb_metal_leaks", 48), ("rb_metal_leaks_b", 48), ("concrete_stained", 48)],
    [("render_stained", 48), ("brick_damaged", 32), ("rb_rust_patch", 48)],
]


def mask(name, hp):
    im = Image.open(os.path.join(TEX, name, "color.jpg")).convert("RGB").resize((N, N), Image.LANCZOS)
    a = np.asarray(im, np.float32) / 255.0
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    l = lin @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    if hp:   # structure only: minus a wide blur (wrap-around, the sets tile)
        b = l
        for _ in range(3):   # three box passes ~ a Gaussian; wrap-around (the sets tile)
            for ax in (0, 1):
                c = np.cumsum(np.concatenate([b.take(range(-hp, 0), axis=ax), b, b.take(range(0, hp + 1), axis=ax)], axis=ax), axis=ax)
                b = (c.take(range(2 * hp + 1, c.shape[ax]), axis=ax) - c.take(range(0, c.shape[ax] - 2 * hp - 1), axis=ax)) / (2 * hp + 1)
        l = l - b
    lo, hi = np.percentile(l, 2), np.percentile(l, 98)
    m = np.clip((l - lo) / max(hi - lo, 1e-4), 0, 1)
    return m if m.mean() <= 0.5 else 1.0 - m   # the marks (stains, leaks, cracks, smears) = 1, whichever way the source draws them


def srgb(v):
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * np.power(v, 1 / 2.4) - 0.055)


def main():
    for k, trio in enumerate(LIB):
        rgb = np.stack([mask(n, hp) for n, hp in trio], 2)
        out = os.path.join(TEX, "mask_lib_%d" % k)
        os.makedirs(out, exist_ok=True)
        Image.fromarray((np.clip(srgb(rgb), 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, "color.jpg"), quality=92)
        json.dump({"set": "mask_lib_%d" % k, "family": "mask library (CP step 6)", "size": [2.0, 2.0],
                   "license": "CC0 1.0 Universal", "credit": "derived from ambientCG CC0 sets: " + ", ".join(n for n, _ in trio),
                   "channels": {"r": trio[0][0], "g": trio[1][0], "b": trio[2][0]}, "maps": ["color.jpg"]},
                  open(os.path.join(out, "meta.json"), "w", encoding="utf-8"), indent=1)
        print("mask_lib_%d:" % k, ", ".join("%s %.2f" % (n, rgb[..., i].mean()) for i, (n, _) in enumerate(trio)))


if __name__ == "__main__":
    main()
