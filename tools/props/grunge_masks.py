# TX6: the wall wear masks for the game's shader (src/textures.cpp loadGrunge, unit 28): one tileable 1024 x 1024 RGBA
# PNG, R = paint chips (1 = paint gone), G = dirt, B = cracks, each stretched to 0..1 between its 2nd and 98th percentile.
#   blender -b --factory-startup --python tools/props/grunge_masks.py -- local <texturelabs dir>   -> content/textures/grunge_masks/local.png
#   blender -b --factory-startup --python tools/props/grunge_masks.py -- cc0                       -> content/textures/grunge_masks/cc0.png
# local: the user's Texturelabs grunge images (chips: Grunge_331 / inverted 338; dirt: inverted 308; cracks: inverted 312).
#   Their licence allows use in the game, not passing the files on: local.png is git-ignored and never committed.
# cc0: the committed stand-in from ambientCG CC0 sets already in content/textures (chips: where PaintedPlaster013's paint
#   is gone, by its saturation; dirt: SurfaceImperfections003; cracks: SurfaceImperfections015), so a clone still has masks.
# The Texturelabs images are photos, not tiles: each is made seamless by blending it with its half-offset copy at the borders.
import bpy, sys, os
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
mode = a[0]
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join(ROOT, "content", "textures")
OUT = os.path.join(TEX, "grunge_masks"); os.makedirs(OUT, exist_ok=True)
N = 1024


def load(path):
    im = bpy.data.images.load(path); im.colorspace_settings.name = "Non-Color"
    im.scale(N, N)
    px = np.empty(N * N * 4, np.float32); im.pixels.foreach_get(px)
    return px.reshape(N, N, 4)[:, :, :3]


def lum(rgb): return rgb @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def stretch(x):
    lo, hi = np.percentile(x, 2), np.percentile(x, 98)
    return np.clip((x - lo) / max(hi - lo, 1e-4), 0, 1)


def seamless(x):
    b = np.roll(np.roll(x, N // 2, 0), N // 2, 1)
    t = np.abs(np.arange(N, dtype=np.float32) / (N - 1) - 0.5) * 2.0
    w = np.maximum(t[None, :], t[:, None])
    w = np.clip((w - 0.55) / 0.45, 0, 1); w = w * w * (3 - 2 * w)
    return x * (1 - w) + b * w


if mode == "local":
    d = a[1]
    g = lambda n: lum(load(os.path.join(d, "Texturelabs_Grunge_%sM.jpg" % n)))
    chip = np.maximum(stretch(g("331")), 1.0 - stretch(g("338")))
    dirt = 1.0 - stretch(g("308"))
    crack = 1.0 - stretch(g("312"))
    chip, dirt, crack = seamless(chip), seamless(dirt), seamless(crack)
    out = os.path.join(OUT, "local.png")
else:
    pp = load(os.path.join(TEX, "painted_plaster_013", "color.jpg"))
    sat = pp.max(2) - pp.min(2)
    chip = 1.0 - stretch(sat)
    dirt = 1.0 - stretch(lum(load(os.path.join(TEX, "surface_imperfections_003", "color.jpg"))))
    crack = 1.0 - stretch(lum(load(os.path.join(TEX, "surface_imperfections_015", "color.jpg"))))
    out = os.path.join(OUT, "cc0.png")
    def match(x, m):   # the same coverage as the Texturelabs masks (their means), by a power curve
        lo, hi = 0.05, 20.0
        for _ in range(40):
            p = (lo * hi) ** 0.5
            if (x ** p).mean() > m: lo = p
            else: hi = p
        return x ** p
    chip, dirt, crack = match(stretch(chip), 0.49), match(stretch(dirt), 0.26), match(stretch(crack), 0.15)
rgba = np.stack([chip if mode != "local" else stretch(chip), dirt if mode != "local" else stretch(dirt), crack if mode != "local" else stretch(crack), np.ones((N, N), np.float32)], 2)
im = bpy.data.images.new("grunge", N, N, alpha=True); im.colorspace_settings.name = "Non-Color"
im.pixels.foreach_set(rgba.astype(np.float32).ravel())
im.filepath_raw = out; im.file_format = "PNG"; im.save()
print("grunge masks:", out, "chip mean %.2f dirt mean %.2f crack mean %.2f" % (rgba[..., 0].mean(), rgba[..., 1].mean(), rgba[..., 2].mean()))
