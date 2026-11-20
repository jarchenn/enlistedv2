# WS1: the user's approved grips (blender_anim/gun_in_clip.py GIC_SAVEPOSE: both wrists and every finger, relative to the gun,
# captured on the Mixamo skeleton) -> content/weapons/<id>.json "hold"."pose", which pose.cpp applies rigidly.
#   python -I tools/grip_to_game.py <dir with *_grip.json>...
# Capture space: Blender world (z up, the soldier faces -y), gun-local x right / y muzzle / z up, origin the grip centre.
# Game: model space y up / +z forward / +x left; weapon space x left / y up / z muzzle.
#   r / l .p: the wrist joint from its anchor, weapon space (r: the rightGrip socket; l: "grip" = the rightGrip socket too, so
#             the approved hand spacing is kept, or "mag" = the magazine's centre for hold.left = magazine)
#   r / l .q: the hand's turn from its rest, in weapon space (model-space turn = weapon rotation * q), xyzw
#   fingers:  each finger joint's turn from rest against its parent's, model axes, xyzw (joint name without its prefix)
import json, sys, os, glob, re
import numpy as np
IDS = {'Garand': ['m1_garand', 'm1c_garand'], 'Thompson': ['m1_thompson'], 'M3': ['m3_grease_gun'], 'BAR': ['m1918a2_bar'], 'MP40': ['mp40'],
       'StG44': ['stg44'], 'Kar98k': ['kar98k', 'kar98k_zf39'], 'FG42': ['fg42'], 'MG42': ['mg42'], 'Panzerschreck': ['panzerschreck'], 'M1911': ['m1911a1'], 'P08': ['luger']}
WRIST = 0.065   # m: the game bodies' wrist joints were moved from mid-forearm (Mixamo's placement) to the cuff; the same shift here keeps the hands where they were approved
CW = np.array([[-1, 0, 0], [0, 0, 1], [0, 1, 0]], float)     # gun-local -> weapon space
CM = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)     # Blender world -> model space
def quat(m):   # rotation matrix -> xyzw
    t = np.trace(m)
    if t > 0: s = np.sqrt(t + 1) * 2; q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    else:
        i = int(np.argmax(np.diag(m))); j, k = (i + 1) % 3, (i + 2) % 3; s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2
        q = [0, 0, 0, (m[k, j] - m[j, k]) / s]; q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s
    return [round(float(v), 4) for v in q]
def rot(wxyz):
    w, x, y, z = wxyz
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
def ortho(m): u, _, v = np.linalg.svd(np.array(m, float)); return u @ v
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'content', 'weapons')
def hand(g, side, anchor):   # one wrist: its place from `anchor` (gun-local) and its turn, weapon space
    M = np.array(g['R' if side == 'r' else 'L'], float); rest = ortho(g['rest']['mixamorig:' + ('Right' if side == 'r' else 'Left') + 'Hand'])
    wrist = M[:3, 3] + ortho(M[:3, :3]) @ np.array([0, WRIST, 0])   # the body's wrist joint sits WRIST further down its own axis than the capture rig's (see WRIST)
    return {'p': [round(float(v), 4) for v in CW @ (wrist - np.array(anchor, float))], 'q': quat(CW @ ortho(M[:3, :3]) @ rest.T @ CM.T)}
def fingers(g, which):       # the finger turns of one hand ('Left' / 'Right')
    return {n.split(':')[-1]: quat(CM @ ortho(g['rest'][n]) @ rot(q) @ ortho(g['rest'][n]).T @ CM.T) for n, q in g['fingers'].items() if which in n}
def butt(g): b = CW @ np.array(g.get('butt') or (0, 0, 0), float); return [round(float(b[0]), 4), round(float(b[1]), 4)]   # the butt plate from the grip centre, weapon x / y (its z is the model's rear end)
# per-gun corrections from review, on top of the grip or template: weapon space metres (x left, y up, z toward the muzzle)
ADJUST = {'modern_m4_irons': {'l': (0, 0, 0.11)}}   # the support hand 11 cm further forward on the handguard (M4 only)
def write(wid, pose, left_mag):
    for side, d in ADJUST.get(wid, {}).items(): pose[side] = dict(pose[side], p=[round(a + b, 4) for a, b in zip(pose[side]['p'], d)])
    p = os.path.join(root, wid + '.json'); s = open(p, encoding='utf-8', newline='').read()
    s = re.sub(r'"pose":\{"r":.*?"fingers":\{[^}]*\}\},?', '', s)                                   # an earlier export out
    if left_mag and '"left":"magazine"' not in s: s = s.replace('"hold":{', '"hold":{"left":"magazine",', 1) if '"hold":{' in s else s.replace('"sockets":', '"hold":{"left":"magazine"},"sockets":', 1)
    txt = '"pose":' + json.dumps(pose, separators=(',', ':'))
    s = s.replace('"hold":{', '"hold":{' + txt + (',' if not s.split('"hold":{', 1)[1].startswith('}') else ''), 1) if '"hold":{' in s else s.replace('"sockets":', '"hold":{' + txt + '},"sockets":', 1)
    json.loads(s); open(p, 'w', encoding='utf-8', newline='').write(s)
G = {}
for d in sys.argv[1:]:
    for f in sorted(glob.glob(os.path.join(d, '*_grip.json'))):
        g = json.load(open(f))
        if 'rest' in g: G[os.path.basename(f)[:-10]] = g
done = set()
for tag, ids in IDS.items():           # 1: the grips approved on the gun itself
    if tag not in G: continue
    g = G[tag]
    for wid in ids:
        mag = json.load(open(os.path.join(root, wid + '.json'), encoding='utf-8')).get('hold', {}).get('left') == 'magazine' and g.get('mag_c') is not None
        pose = {'r': hand(g, 'r', (0, 0, 0)), 'l': dict(hand(g, 'l', g['mag_c'] if mag else (0, 0, 0)), anchor='mag' if mag else 'grip'), 'butt': butt(g), 'fingers': {**fingers(g, 'Left'), **fingers(g, 'Right')}}
        write(wid, pose, False); done.add(wid); print(wid, 'own grip, l anchor', pose['l']['anchor'])
# 2: every other gun from an approved grip of its family, each hand anchored on that gun's own sockets (r: rightGrip; l: the
# magazine's centre or the leftSupport socket). (right-hand source, left-hand source, left anchor); marked "template".
FAM = {'mag': ('StG44', 'StG44', 'mag'), 'pg': ('StG44', 'FG42', 'support'), 'straight': ('Garand', 'Garand', 'support'), 'launcher': ('Panzerschreck', 'Panzerschreck', 'support'), 'pistol': ('M1911', 'M1911', 'grip')}
def family(wid, wj):
    n = wid.replace('modern_', '')
    if any(k in wid for k in ('blaster', 'flame', 'flammen', 'm2hb', 'grenade', 'smoke', 'molotov', 'knife', 'hammer', 'explosion', 'handling')): return None
    if any(k in n for k in ('rpg', 'bazooka', 'glauncher', 'panzer')): return 'launcher'
    if any(k in n for k in ('pistol', 'glock', 'deagle', 'fn502', 'makarov', 'mark23', 'revolver', 'luger', 'm1911')): return 'pistol'   # the approved two-handed pistol grip (the postures are pose.cpp's)
    if any(k in n for k in ('ak', 'mp5', 'mp7', 'uzi', 'smg', 'scorpion', 'ump', 'vsk')): return 'mag'
    if any(k in n for k in ('carbine', 'sten', 'shotgun', 'sawnoff', 'sniper_bolt', 'sniper_3', 'sniper_50')): return 'straight'
    return 'pg'
for f in sorted(glob.glob(os.path.join(root, '*.json'))):
    wid = os.path.basename(f)[:-5]
    if wid in done: continue
    try: wj = json.load(open(f, encoding='utf-8'))
    except Exception: continue
    fam = family(wid, wj)
    if not fam or 'sockets' not in wj: continue
    rs, ls, anc = FAM[fam]
    if rs not in G or ls not in G: continue
    gl = G[ls]
    pose = {'r': hand(G[rs], 'r', (0, 0, 0)), 'l': dict(hand(gl, 'l', gl['mag_c'] if anc == 'mag' else (0, 0, 0) if anc == 'grip' else gl['s_loc']), anchor=anc), 'template': fam, 'butt': butt(G[rs]), 'fingers': {**fingers(gl, 'Left'), **fingers(G[rs], 'Right')}}
    write(wid, pose, anc == 'mag'); print(wid, 'template', fam)
