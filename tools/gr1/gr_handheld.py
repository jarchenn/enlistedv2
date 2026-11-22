# GR11 (the user): the approved take's handheld motion, shared by arms_full.py (the preview take) and gar_prep.py (the game
# rig): the head and the hands wander on their own (smooth sums of slow sines, unrelated phases, seed 11) with a damped knock
# after events (the clip driven home, the rod slammed forward). Times in seconds; w scales the wander (not the knocks).
import math, random
from mathutils import Matrix, Vector
_hr = random.Random(11)
WOB = {k: [(_hr.uniform(0.18, 0.45), _hr.uniform(0.6, 1.0), _hr.uniform(0, 6.283)), (_hr.uniform(0.6, 1.1), _hr.uniform(0.3, 0.5), _hr.uniform(0, 6.283)), (_hr.uniform(1.4, 2.3), _hr.uniform(0.1, 0.2), _hr.uniform(0, 6.283))] for k in ('cp', 'cy', 'cr', 'cx', 'cz', 'rp', 'ry', 'rr', 'rx', 'ry_', 'rz')}
def wob(k, t): return sum(a * math.sin(2 * math.pi * fq * t + ph) for fq, a, ph in WOB[k]) / 1.5
def jolt(t, events):   # events: [(time s, size)]: a damped knock after each, in units of its size
    j = 0.0
    for te, g in events:
        x = t - te
        if x > 0: j += g * math.exp(-x / 0.09) * math.sin(2 * math.pi * 7.0 * x)
    return j
def handheld(t, w, events, K):   # -> camera offset (its own frame), the hands' turn and shift (camera frame); K: rig units per metre
    jt = jolt(t, events)
    Rc = Matrix.Rotation(math.radians(w * 0.35 * wob('cp', t) - 0.25 * jt), 4, 'X') @ Matrix.Rotation(math.radians(w * 0.35 * wob('cy', t)), 4, 'Y') @ Matrix.Rotation(math.radians(w * 0.25 * wob('cr', t)), 4, 'Z')
    Tc = Matrix.Translation(Vector((w * 0.004 * wob('cx', t), w * 0.004 * wob('cz', t) - 0.002 * jt, 0)) * K)
    Rr = Matrix.Rotation(math.radians(w * 0.6 * wob('rp', t) - 0.6 * jt), 4, 'X') @ Matrix.Rotation(math.radians(w * 0.5 * wob('ry', t)), 4, 'Y') @ Matrix.Rotation(math.radians(w * 0.4 * wob('rr', t)), 4, 'Z')
    Tr = Vector((w * 0.006 * wob('rx', t), w * 0.006 * wob('ry_', t) - 0.004 * jt, w * 0.004 * wob('rz', t) + 0.003 * jt)) * K
    return Tc @ Rc, Rr, Tr
