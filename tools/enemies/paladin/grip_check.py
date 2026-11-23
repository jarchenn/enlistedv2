# where the hands are against the sword's axis, through clips: distance off the axis and position along it (m, from the guard; + toward the pommel)
import json, sys
import numpy as np
ARGV = list(sys.argv)
exec(open('E:/nmk/stag/measure_elk.py').read().split("mode = sys.argv[1]")[0].replace("import json, sys", "import json, sys\nsys.argv=['x']").replace("E:/nmk/wt-pf3/tools/enemies/retarget_clips.py", "E:/nmk/platemail/retarget_mx.py"))
sys.argv = ARGV; R = Rig(*g["load"](sys.argv[1])); GUARD = np.array(json.loads(sys.argv[2])); POMMEL = np.array(json.loads(sys.argv[3]))   # bind space
sk = R.J['skins'][0]; a = R.J['accessors'][sk['inverseBindMatrices']]; v = R.J['bufferViews'][a['bufferView']]
IB = np.frombuffer(bytes(R.BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
ix = lambda nm: next(i for i, x in enumerate(R.N) if x.get('name') == nm); wj = ix('mixamorig:Weapon'); kw = sk['joints'].index(wj)
for clip in sys.argv[4:]:
    an = next(x for x in R.J['animations'] if x['name'] == clip); T, W = posedT(R, an, 12); M = W(wj) @ IB[kw]
    g_ = (M @ np.r_[GUARD, 1])[:, :3]; p_ = (M @ np.r_[POMMEL, 1])[:, :3]; d = (p_ - g_) / np.linalg.norm(p_ - g_, axis=1, keepdims=True)
    for hand in ('RightHand', 'LeftHand'):
        hm = W(ix('mixamorig:' + hand)); mid = W(ix('mixamorig:' + hand.replace('Hand', 'HandMiddle1')))[:, :3, 3]
        pal = (hm[:, :3, 3] + mid) / 2   # the palm: between the wrist and the middle knuckle
        along = np.einsum('ij,ij->i', pal - g_, d); off = np.linalg.norm((pal - g_) - along[:, None] * d, axis=1)
        print('%-22s %-9s along %s  off-axis %s' % (clip[:22], hand, np.round(along, 2), np.round(off, 2)))
# the move that seats the hilt in both fists: the right palm 0.07 m below the guard, the axis 0.04 m from the right palm
if len(sys.argv) > 4:
    an = next(x for x in R.J['animations'] if x['name'] == sys.argv[4]); T, W = posedT(R, an, 2); M = W(wj)[0] @ IB[kw]; Mi = np.linalg.inv(M)
    hm = W(ix('mixamorig:RightHand'))[0, :3, 3]; mid = W(ix('mixamorig:RightHandMiddle1'))[0, :3, 3]; pb = (Mi @ np.r_[(hm + mid) / 2, 1])[:3]
    d = (POMMEL - GUARD) / np.linalg.norm(POMMEL - GUARD); al = (pb - GUARD) @ d; foot = GUARD + d * al; lat = pb - foot; off = np.linalg.norm(lat)
    delta = d * (al - 0.07) + lat * (1 - 0.04 / off)
    print('SHIFT', json.dumps(delta.round(4).tolist()), 'along %.3f off %.3f' % (al, off))
