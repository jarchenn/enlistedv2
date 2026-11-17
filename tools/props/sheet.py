# PR1: contact sheet of --capture-props shots (selftest_out/props.txt + prop_<k>_<n|f>.png), labelled, near / far pairs.
#   python tools/props/sheet.py <selftest_out dir> <out.png> [columns=4]
import sys, os
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]; cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
ids = [l.strip() for l in open(os.path.join(d, "props.txt")) if l.strip()]
W, H = 480, 270
rows = (len(ids) + cols - 1) // cols
sheet = Image.new("RGB", (cols * W * 2, rows * (H + 22)), (24, 24, 24)); dr = ImageDraw.Draw(sheet)
for k, pid in enumerate(ids):
    x, y = (k % cols) * W * 2, (k // cols) * (H + 22)
    for j, s in enumerate("nf"):
        p = os.path.join(d, "prop_%d_%s.png" % (k, s))
        try: sheet.paste(Image.open(p).convert("RGB").resize((W, H)), (x + j * W, y + 22))
        except OSError: print("unreadable or missing", p)   # a shot cut short leaves its cell blank
    dr.text((x + 6, y + 4), "%d  %s   (1.5 m | 4 m)" % (k, pid), fill=(235, 235, 200))
sheet.save(out); print(out, sheet.size)
