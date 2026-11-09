// Atlantic Wall structures for the MP & Creative update (SPEC 3.6, docs/assets-mpu.md): the R667 flanking casemate,
// the unfinished R612, three Tobruks (tank turret, MG ringstand, mortar), connectable zig-zag trench pieces with
// wattle revetment, a personnel dugout, a log-roofed dugout and a two-man foxhole.
// Below-grade assets carry a "terrainCut" (src/navmeta.h); the terrain applies it (W3). Until it does, the floors sit
// under the ground. Every asset carries "nav" metadata and a "tour" (scripted route run by --selftest-structures).
// Also exports the shared helpers used by catalog_beach / catalog_bocage / catalog_village2.
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs for these).
import { Asset, wbox } from "./kit.mjs";
import { stdMaterials, wall, door, stairs, table } from "./parts.mjs";
import { bunkZ, stove, stool, lamp, crates, sacks, shelfZ, crateBox } from "./furniture2.mjs";
import { smallArmsCache, gunAmmoStack } from "./catalog_ammo.mjs";
import { SLIT } from "./catalog_bunkers.mjs";
import { rod, sandbag, seeded, doorKnuckles, ventPipe, dmat } from "./defence_detail.mjs";
// D1: cosmetic detail parts go to A.d1 and are appended after every other part (atlanticAssets), so the colliding parts
// keep their indices
const d1 = (A) => (A.d1 ??= []);
const flushD1 = (A) => { if (A.d1) { A.parts.push(...A.d1); delete A.d1; } return A; };
// a ring laid flat (two half-ring arches turned onto their backs) centred at c: outer radius R, ring width th, height h
function flatRing(A, c, R, th, h, m, o = {}) {
  for (const s of [1, -1]) A.arch([c[0], c[1], c[2] + (s * R) / 2], [2 * R, R, h], m, { th, seg: 16, ...o, r: [90, s > 0 ? 0 : 180, 0] });
}

// ============================================================================ shared helpers (exported)
export const r3 = (v) => Math.round(v * 1000) / 1000;
const MPU_MATS = {
  wattle: [[0.2, 0.145, 0.08], 0.93, "wood", { grain: 0 }],
  picket: [[0.17, 0.12, 0.075], 0.9, "wood", { grain: 1 }],
  mud: [[0.1, 0.08, 0.055], 0.75, "plain"],
  spoil: [[0.2, 0.16, 0.11], 0.97, "plain"],
  sand: [[0.42, 0.37, 0.27], 0.97, "plain"],
  sand_wet: [[0.27, 0.235, 0.17], 0.55, "plain"],
  shingle: [[0.31, 0.3, 0.28], 0.8, "stone"],
  rock: [[0.36, 0.33, 0.27], 0.9, "stone"],
  rock_dark: [[0.24, 0.22, 0.19], 0.92, "stone"],
  water: [[0.025, 0.045, 0.04], 0.06, "plain"],
  reed: [[0.34, 0.31, 0.15], 0.9, "fabric"],
  reed_green: [[0.13, 0.17, 0.06], 0.9, "fabric"],
  rust: [[0.2, 0.095, 0.045], 0.8, "metal", { metallic: 0.2 }],
  steel_dark: [[0.09, 0.09, 0.085], 0.6, "metal", { metallic: 0.5 }],
  rebar: [[0.24, 0.12, 0.06], 0.75, "metal", { metallic: 0.4 }],
  form_board: [[0.36, 0.27, 0.17], 0.85, "wood", { grain: 1 }],
  cement: [[0.55, 0.54, 0.5], 0.95, "plain"],
  mixer_paint: [[0.12, 0.17, 0.09], 0.6, "painted", { grain: 0 }],
  tyre: [[0.03, 0.03, 0.03], 0.85, "plain"],
  turret_paint: [[0.25, 0.24, 0.19], 0.65, "metal", { metallic: 0.2 }],
  sign_white: [[0.72, 0.7, 0.6], 0.8, "painted", { grain: 1 }],
  sign_black: [[0.03, 0.03, 0.03], 0.8, "plain"],
  sign_red: [[0.45, 0.05, 0.03], 0.8, "painted", { grain: 1 }],
  tellermine: [[0.2, 0.21, 0.15], 0.6, "metal", { metallic: 0.3 }],
  bark_grey: [[0.17, 0.155, 0.13], 0.95, "wood", { grain: 1 }],
  log_raw: [[0.2, 0.15, 0.1], 0.92, "wood", { grain: 1 }],
  burnt: [[0.03, 0.028, 0.025], 0.95, "plain"],
};
// add one of the shared materials once (stdMaterials covers the rest)
export function M(A, name) {
  if (!A.materials.some((m) => m.name === name)) {
    const [albedo, rough, pattern, extra] = MPU_MATS[name];
    A.mat(name, albedo, rough, pattern, extra || {});
  }
  return name;
}
export function mats(A, ...names) { for (const n of names) M(A, n); }
// deterministic random numbers (mulberry32): generator output is stable across runs
export function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
export const rect = (x0, z0, x1, z1) => [[r3(x0), r3(z0)], [r3(x1), r3(z0)], [r3(x1), r3(z1)], [r3(x0), r3(z1)]];
// asset-local yaw convention for nav points: degrees about +Y, 0 = facing local +Z (the asset front), 90 = facing +X
// (the same rotation as an instance's yaw)
export const fire = (p, yaw, kind) => ({ p: p.map(r3), yaw, kind });
export const cover = (p, yaw, height = "low", peek = ["over"]) => ({ p: p.map(r3), yaw, height, peek });
export const room = (id, min, max) => ({ id, min: min.map(r3), max: max.map(r3) });
export const corners = (id, x0, x1, z0, z1, y, inset = 0.45) => [[x0 + inset, z0 + inset], [x1 - inset, z0 + inset], [x1 - inset, z1 - inset], [x0 + inset, z1 - inset]].map(([x, z]) => ({ p: [r3(x), r3(y), r3(z)], room: id }));
export function setNav(A, nav, cuts = null) { A.meta.nav = nav; if (cuts) A.meta.terrainCut = cuts.map((c) => ({ outline: c.outline.map((p) => p.map(r3)), depth: r3(c.depth), bank: r3(c.bank ?? 0.3) })); }
// scripted route for --selftest-structures (same step vocabulary as "walkthrough"; kept under its own key so the
// legacy --selftest phase A does not pick these assets up). cut: the test applies the terrain cut before walking.
export function setTour(A, start, steps, extra = {}) { A.meta.tour = { start, steps, ...extra }; }
// V6 snap sockets (src/snap.h): p asset-space, dir the outward direction (x, z) through that end; pieces whose sockets
// meet join at one base height. A part tagged cap: n closes socket n's free end and is left out while it is joined.
export const sock = (p, dir, kind) => ({ p: p.map(r3), dir: dir.map(r3), kind });
export function setSnap(A, sockets) { A.meta.snap = sockets; }
export function asCap(part, idx) { if (part) part.cap = idx; return part; }
// trench end cap at socket idx (point p, outward dir): wattle end wall on the socket plane and earth fill out over the
// cut's open end (both collide: the end is closed until another piece joins there)
export function trenchCap(A, p, dir, idx) {
  const { D, HW, RV, FILL, PAR, PARADOS } = TR;
  const n = [-dir[1], dir[0]], at = (a, o) => [p[0] + dir[0] * a + n[0] * o, p[1] + dir[1] * a + n[1] * o];
  const w = HW + RV;
  asCap(segBox(A, at(RV / 2, w), at(RV / 2, -w), 0, -D - 0.2, Math.max(PAR, PARADOS) - 0.03, RV, "wattle"), idx);
  asCap(segBox(A, at(RV + 0.6, w + FILL + 0.2), at(RV + 0.6, -w - FILL - 0.2), 0, -D - 0.25, 0.1, 1.2, "spoil"), idx);
  for (const o of [-0.4, 0, 0.4]) roundPicket(asCap(segBox(A, at(RV + 0.07, o - 0.03), at(RV + 0.07, o + 0.03), 0, -D - 0.1, PAR + 0.1, 0.06, "picket", { k: 0 }), idx));
}

// D1: a picket box turned in place into a round stake (same slot, so no index moves)
function roundPicket(q) { if (q) { q.t = "c"; q.s = [0.075, q.s[1], 0.075]; q.seg = 8; } return q; }
// segment-aligned box: runs from p0 to p1 ([x, z]) offset by `off` along the left normal n = (-dz, dx), y0..y1,
// thickness th across the segment, extended by e0 / e1 past the ends; tilt leans its top toward +n (degrees)
export function segBox(A, p0, p1, off, y0, y1, th, m, o = {}, e0 = 0, e1 = 0, tilt = 0) {
  const dx = p1[0] - p0[0], dz = p1[1] - p0[1], L = Math.hypot(dx, dz), ux = dx / L, uz = dz / L, nx = -uz, nz = ux;
  const a = -e0, b = L + e1, mid = (a + b) / 2;
  const c = [p0[0] + ux * mid + nx * off, (y0 + y1) / 2, p0[1] + uz * mid + nz * off];
  const yaw = (Math.atan2(-dz, dx) * 180) / Math.PI;
  if (Math.abs(yaw % 90) < 1e-6 && !tilt) {   // axis aligned: a plain box keeps auto-panel extraction possible
    const hx = Math.abs(ux) > 0.5 ? (b - a) / 2 : th / 2, hz = Math.abs(ux) > 0.5 ? th / 2 : (b - a) / 2;
    return A.box([c[0] - hx, y0, c[2] - hz], [c[0] + hx, y1, c[2] + hz], m, o);
  }
  return A.obox(c, [b - a, y1 - y0, th], [tilt, yaw, 0], m, o);
}
// polyline offset by d along the left normal, mitred joints
export function offsetLine(pts, d) {
  const out = [];
  for (let i = 0; i < pts.length; i++) {
    const nrm = (a, b) => { const dx = b[0] - a[0], dz = b[1] - a[1], L = Math.hypot(dx, dz); return [-dz / L, dx / L]; };
    if (i === 0) { const n = nrm(pts[0], pts[1]); out.push([pts[0][0] + n[0] * d, pts[0][1] + n[1] * d]); continue; }
    if (i === pts.length - 1) { const n = nrm(pts[i - 1], pts[i]); out.push([pts[i][0] + n[0] * d, pts[i][1] + n[1] * d]); continue; }
    const n0 = nrm(pts[i - 1], pts[i]), n1 = nrm(pts[i], pts[i + 1]);
    let mx = n0[0] + n1[0], mz = n0[1] + n1[1]; const ml = Math.hypot(mx, mz); mx /= ml; mz /= ml;
    const k = d / Math.max(0.3, mx * n0[0] + mz * n0[1]);
    out.push([pts[i][0] + mx * k, pts[i][1] + mz * k]);
  }
  return out;
}
// polyline with its first and last points pushed out by e along the end segments
export function extendEnds(pts, e) {
  const p = pts.map((q) => q.slice()), n = p.length - 1;
  const u = (a, b) => { const L = Math.hypot(b[0] - a[0], b[1] - a[1]); return [(b[0] - a[0]) / L, (b[1] - a[1]) / L]; };
  const a = u(p[0], p[1]), b = u(p[n - 1], p[n]);
  p[0] = [p[0][0] - a[0] * e, p[0][1] - a[1] * e]; p[n] = [p[n][0] + b[0] * e, p[n][1] + b[1] * e];
  return p;
}
export function band(pts, hw) { const l = offsetLine(pts, hw), r = offsetLine(pts, -hw); return l.concat(r.reverse()).map((p) => p.map(r3)); }

// ---- Teller mine 35 (0.32 m dia, 0.1 m high): an octagonal disc of two crossed boxes plus the pressure cap
export function tellerMine(A, c, yawDeg = 0, tiltX = 0) {
  M(A, "tellermine");
  A.obox(c, [0.3, 0.09, 0.3], [tiltX, yawDeg, 0], "tellermine", { k: 0 });
  A.obox(c, [0.3, 0.09, 0.3], [tiltX, yawDeg + 45, 0], "tellermine", { k: 0 });
  A.obox([c[0], c[1] + 0.055, c[2]], [0.11, 0.03, 0.11], [tiltX, yawDeg, 0], "steel_dark", { k: 0 });
}

// ---- wattle-revetted trench run (Feldstellung): floor D below grade, inner width 1.2 m, earth fill behind the
// revetment out to the cut edge, a parapet (front, +n side when front = 1) and a parados. firestep: segment indices
// with a 0.33 m firestep along the front. niche: [segIdx, t] ammunition niche in the rear revetment (axis-aligned
// segments only). Returns the floor band outline (nav area) and the cut outline.
// Terrain cuts are padded outward by one terrain grid step (0.45 m) and resolved on that grid (W3, terrain_cuts.cpp), so the
// ground steps down up to ~0.9 m outside an outline. Where a ramp climbs out of a cut to grade, the outline stops
// CUT_SETBACK inside the ramp top (the padded edge then lies just short of it) and a grade-level apron CUT_APRON long
// covers the step; along the sides, spoil heaps or parapet slabs cover it.
// Closed walls: the outline lies CUT_PAD inside the outer wall face (the padded floor reaches the face) and a ring
// CUT_RING wide at grade, earth banks or spoil heaps cover the step outside.
export const CUT_PAD = 0.45, CUT_SETBACK = 0.3, CUT_APRON = 1.0, CUT_RING = 0.6;
export const TR = { D: 1.5, HW: 0.6, RV: 0.1, FILL: 0.6, STEP: 0.33, STEPW: 0.45, PAR: 0.4, PARADOS: 0.25 };
// endExt: how far the parapet / parados slabs reach past the run's first / last point (0 at a snap socket: joined
// pieces meet at exactly 8.0 m without overlapping spoil; V6 snapping)
export function trenchRun(A, pts, { front = 1, firestep = [], niche = null, cache = "Small-arms ammunition (niche)", endExt = [0, 0] } = {}) {
  mats(A, "wattle", "picket", "mud", "spoil");
  const { D, HW, RV, FILL, STEP, STEPW, PAR, PARADOS } = TR;
  const n = pts.length - 1;
  const segs = [];
  for (let i = 0; i < n; i++) segs.push([pts[i], pts[i + 1]]);
  const join0 = (i) => (i > 0 ? HW + RV + FILL : 0), join1 = (i) => (i < n - 1 ? HW + RV + FILL : 0);
  for (const side of [1, -1]) {
    const isFront = side === front, top = isFront ? PAR : PARADOS;
    const rv = offsetLine(pts, side * (HW + RV / 2)), fl = offsetLine(pts, side * (HW + RV + FILL / 2));
    for (let i = 0; i < n; i++) {
      const nicheHere = niche && niche[0] === i && !isFront;
      const L = Math.hypot(rv[i + 1][0] - rv[i][0], rv[i + 1][1] - rv[i][1]);
      const spans = nicheHere ? [[0, niche[1] * L - 0.6], [niche[1] * L + 0.6, L]] : [[0, L]];
      for (const [s0, s1] of spans) {
        const ux = (rv[i + 1][0] - rv[i][0]) / L, uz = (rv[i + 1][1] - rv[i][1]) / L;
        const a = [rv[i][0] + ux * s0, rv[i][1] + uz * s0], b = [rv[i][0] + ux * s1, rv[i][1] + uz * s1];
        segBox(A, a, b, 0, -D - 0.2, top - 0.03, RV, "wattle");                                      // revetment
        // fill ends on the fill line's own end points (its mitred length differs from the revetment's): no overshoot past a piece end
        const fa = s0 === 0 ? fl[i] : [fl[i][0] + ux * s0, fl[i][1] + uz * s0], fb = s1 === L ? fl[i + 1] : [fl[i][0] + ux * s1, fl[i][1] + uz * s1];
        segBox(A, fa, fb, 0, -D - 0.25, top, FILL, "spoil", {}, s0 === 0 ? (i > 0 ? 0.3 : 0) : 0, s1 === L ? (i < n - 1 ? 0.3 : 0) : 0);
        for (let s = s0 + 0.15; s < s1 - 0.05; s += 0.95) {                                            // pickets
          const p = [rv[i][0] + ux * s, rv[i][1] + uz * s];
          roundPicket(segBox(A, [p[0] - ux * 0.03, p[1] - uz * 0.03], [p[0] + ux * 0.03, p[1] + uz * 0.03], -side * 0.07, -D - 0.1, top + 0.12, 0.06, "picket", { k: 0 }));
        }
        rod(A, [a[0], top - 0.03, a[1]], [b[0], top - 0.03, b[1]], 0.075, "picket", { into: d1(A), seg: 8 });   // D1: capping pole on the revetment
      }
      // parapet / parados slab: top face from (edge, top) down to grade `run` further out
      const run = isFront ? 2.2 : 1.6, alpha = Math.atan2(top, run), t = 0.5, edge = HW + RV + FILL;
      const cz = edge + run / 2 + Math.sin(alpha) * t * -1, cy = top / 2 - Math.cos(alpha) * t;
      const ej = -(HW + RV + FILL);   // stop short of a joint: the corner mounds close it (no slab reaches into the next leg)
      segBox(A, pts[i], pts[i + 1], side * (cz + 0.05), cy - t, cy + t, Math.hypot(run, top) + 0.1, "spoil", { k: 1 }, i > 0 ? ej : endExt[0], i < n - 1 ? ej : endExt[1], side * (alpha * 180) / Math.PI);
    }
    // outer-corner mounds close the wedge gaps between parapet slabs
    for (let i = 1; i < n; i++) {
      const p = offsetLine(pts, side * (HW + RV + FILL + 0.9))[i];
      A.rbox([p[0] - 0.9, -0.4, p[1] - 0.9], [p[0] + 0.9, top * 0.8, p[1] + 0.9], 0.35, "spoil", { k: 0 });
    }
  }
  // floor: duckboards over mud, one invisible collision slab per segment, pads at the joints
  for (let i = 0; i < n; i++) {
    const [p0, p1] = segs[i];
    segBox(A, p0, p1, 0, -D - 0.2, -D, 2 * HW, "mud", { v: 0 }, join0(i) ? HW : 0, join1(i) ? HW : 0);
    segBox(A, p0, p1, 0, -D - 0.2, -D - 0.07, 2 * HW, "mud", { k: 0 }, join0(i) ? HW : 0, join1(i) ? HW : 0);
    const L = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]), ux = (p1[0] - p0[0]) / L, uz = (p1[1] - p0[1]) / L;
    for (const off of [-0.25, 0.25]) segBox(A, p0, p1, off, -D - 0.07, -D - 0.03, 0.08, "plank_x", { k: 0 }, -0.05, -0.05);
    for (let s = 0.15; s + 0.12 < L - 0.02; s += 0.32) segBox(A, [p0[0] + ux * s, p0[1] + uz * s], [p0[0] + ux * (s + 0.12), p0[1] + uz * (s + 0.12)], 0, -D - 0.03, -D, 0.75, "plank_x", { k: 0 });
    if (firestep.includes(i)) {
      const off = front * (HW - STEPW / 2), e0 = i > 0 ? -1.0 : 0, e1 = i < n - 1 ? -1.0 : 0;
      segBox(A, p0, p1, off, -D, -D + STEP - 0.05, STEPW, "spoil", {}, e0, e1);
      segBox(A, p0, p1, off, -D + STEP - 0.05, -D + STEP, STEPW, "plank_x", {}, e0, e1);
    }
  }
  if (niche) {   // ammunition niche behind the rear revetment of an axis-aligned segment
    const [p0, p1] = segs[niche[0]], L = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]), ux = (p1[0] - p0[0]) / L, uz = (p1[1] - p0[1]) / L;
    const c = [p0[0] + ux * niche[1] * L, p0[1] + uz * niche[1] * L], side = -front, nx = -uz * side, nz = ux * side;
    const back = HW + RV + 0.55, yTop = -D + 1.1;
    const at = (s, o) => [c[0] + ux * s + nx * o, c[1] + uz * s + nz * o];
    segBox(A, at(-0.6, 0), at(0.6, 0), side * back, -D - 0.2, PARADOS, 0.1, "wattle");              // niche back
    for (const s of [-0.6, 0.6]) segBox(A, at(s - 0.05, 0), at(s + 0.05, 0), side * (HW + RV + 0.25), -D - 0.2, PARADOS, 0.6, "wattle");
    segBox(A, at(-0.65, 0), at(0.65, 0), side * (HW + RV + 0.3), yTop, yTop + 0.08, 0.7, "plank_x");   // lintel boards
    segBox(A, at(-0.65, 0), at(0.65, 0), side * (HW + RV + 0.3), yTop + 0.08, PARADOS, 0.7, "spoil");
    segBox(A, at(-0.55, 0), at(0.55, 0), side * (HW + RV + 0.27), -D - 0.2, -D, 0.6, "mud", { v: 0 });
    const yaw = (Math.atan2(-uz, ux) * 180) / Math.PI;
    const cc = at(0, HW + RV + 0.27);
    smallArmsCache(A, cache, cc[0], -D, cc[1], yaw + (side > 0 ? 180 : 0), { boxes: 3, bandoliers: 2 });
  }
  // D1: lumpy spoil and grass clods on the parapet / parados slopes (mostly sunk, a few cm proud: the slab stays the
  // walking and cover surface), riser boards and pegs on the firesteps
  const R = seeded(A.meta.id + "trench" + pts.length);
  dmat(A, "spoil");
  for (const side of [1, -1]) {
    const top = side === front ? PAR : PARADOS, run = side === front ? 2.2 : 1.6, edge = HW + RV + FILL;
    for (let i = 0; i < n; i++) {
      const [p0, p1] = segs[i], L = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]), ux = (p1[0] - p0[0]) / L, uz = (p1[1] - p0[1]) / L, nx = -uz * side, nz = ux * side;
      const yaw = (Math.atan2(ux, uz) * 180) / Math.PI;
      for (let k = 0, m = Math.max(3, Math.round(L / 0.4)); k < m; k++) {
        const t = (k + 0.15 + 0.7 * R()) / m * (L - 1.2) + 0.6, x = 0.25 + R() * (run - 0.55), y = top * (1 - x / run);
        const h = 0.14 + 0.1 * R(), w = 0.45 + 0.6 * R(), d = 0.35 + 0.4 * R(), mat = R() < 0.15 ? "turf" : R() < 0.45 ? "earth" : "spoil";
        const c = [p0[0] + ux * t + nx * (edge + x), y - h * 0.6, p0[1] + uz * t + nz * (edge + x)];
        A.obox(c, [w, h, d], [0, yaw + R() * 60 - 30, 0], mat, { into: d1(A), k: 0, bv: h * 0.45 });
      }
    }
  }
  for (const i of firestep) {
    const [p0, p1] = segs[i], e0 = i > 0 ? -1.0 : 0, e1 = i < n - 1 ? -1.0 : 0, off = front * (HW - STEPW - 0.012);
    segBox(A, p0, p1, off, -D - 0.02, -D + STEP - 0.05, 0.025, "plank_x", { into: d1(A), k: 0 }, e0, e1);
    const L = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]), ux = (p1[0] - p0[0]) / L, uz = (p1[1] - p0[1]) / L;
    for (let t = 0.4 - e0; t < L + e1 - 0.2; t += 0.9) {
      const o2 = off - front * 0.035, c = [p0[0] + ux * t - uz * o2, -D + 0.12, p0[1] + uz * t + ux * o2];
      A.cyl(c, [0.06, 0.32, 0.06], "picket", { into: d1(A), k: 0, seg: 8 });
    }
  }
  return { floor: band(pts, HW), cut: band(extendEnds(pts, 0.8), HW + RV + FILL + 0.1) };
}

// ---- stepped embrasure (Treppenscharte) and rectangular poured shell (after catalog_bunkers.mjs, kept local so the
// two generators can evolve independently)
const STEP = { dy: 0.065, da: 0.12 };
function slit(A, { axis, outer, inner, a0, a1, y0, y1, n = 3, step = {}, m = "concrete" }) {
  const st = { da: STEP.da, dyT: STEP.dy, dyB: STEP.dy, ...step };
  const out = Math.sign(outer - inner), t = Math.abs(outer - inner) / n, g = n - 1;
  const H = { a0: a0 - g * st.da, a1: a1 + g * st.da, y0: y0 - g * st.dyB, y1: y1 + g * st.dyT };
  for (let k = 0; k < g; k++) {
    const d0 = inner + out * k * t, d1 = inner + out * (k + 1) * t;
    const oa0 = a0 - k * st.da, oa1 = a1 + k * st.da, oy0 = y0 - k * st.dyB, oy1 = y1 + k * st.dyT;
    if (oy0 > H.y0 + 1e-4) wbox(A, axis, H.a0, H.a1, H.y0, oy0, d0, d1, m);
    wbox(A, axis, H.a0, H.a1, oy1, H.y1, d0, d1, m);
    wbox(A, axis, H.a0, oa0, oy0, oy1, d0, d1, m);
    wbox(A, axis, oa1, H.a1, oy0, oy1, d0, d1, m);
  }
  return H;
}
function concreteMats(A, lip, raw = false) {
  stdMaterials(A);
  const set = (name, albedo, roughness, grain, pattern = "bunker") => Object.assign(A.materials.find((m) => m.name === name), { albedo, roughness, pattern, grain });
  if (raw) {   // fresh, unweathered pour (R612 under construction)
    set("concrete", [0.47, 0.46, 0.42], 0.95, 0, "concrete");
    set("concrete_wash", [0.52, 0.51, 0.47], 0.95, 0, "concrete");
  } else {
    set("concrete", [0.27, 0.26, 0.235], 0.93, Math.round(lip * 10));
    set("concrete_wash", [0.5, 0.49, 0.45], 0.92, 0);
  }
}
const pnC = (side, y0, y1, GF) => ({ side, family: "concrete", bands: [[y0, y1, 0, GF]], collar: 0.3, minW: 1.1 });

// ============================================================================ R667 flanking casemate (WN65)
// Regelbau 667 (Geschuetzschartenstand ohne Nebenraeume, 5 cm KwK): a single gun room whose embrasure fires along the
// beach (+X here), shielded from the sea (+Z) by a sloping wing wall (Flankenmauer), earth heaped on the sea and
// west faces, rear steel door. The gun room is enlarged from ~4 x 4 m to 5.0 x 4.6 m so the crewable PaK 40 station
// fits as the stand-in for the 5 cm pedestal gun (no 5 cm gun model is supplied; guns come only from the user's GLBs).
export function r667() {
  const GF = 0.02, CEIL = 2.7, ROOF = 4.3, XI0 = -2.8, XI1 = 2.2, XO0 = -4.4, XO1 = 3.8, ZI = 2.3, ZO = 3.9, pt = 0.02;
  const A = new Asset({
    id: "aw_r667_casemate", version: 2, name: "R667 flanking casemate (5 cm)", category: "Atlantic Wall",
    description: "Regelbau 667 gun casemate as at WN65 (Ruquet, E-1 draw): 8.2 x 7.8 m, 1.6 m walls and a 1.6 m roof. One gun room with a wide stepped embrasure firing along the beach, shielded from the sea by a sloping wing wall; earth heaped on the sea and landward flanks; rear steel door. Placed with a PaK 40 station as stand-in for the 5 cm KwK (no 5 cm gun model supplied), 5 cm shell crates and a small-arms cache. Embrasure faces local +X.",
    floors: 1, enterable: true, footprint: [15.6, 12.6], plinthDepth: 0.6, maxGrade: 0.6,
  });
  concreteMats(A, CEIL); mats(A, "sand");
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  // embrasure in the east wall (fires +X): PaK casemate geometry (2 m throat, wide upward steps)
  const emb = slit(A, { axis: "z", outer: XO1, inner: XI1, a0: -1.0, a1: 1.0, y0: 0.62, y1: 1.7, step: { da: 0.33, dyT: 0.24, dyB: 0 } });
  const rd = door(A, { id: "r667_door", axis: "x", outer: -ZO, inner: -ZI, c: -1.4, floor: GF, w: 1.0, h: 2.0, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  A.box([XO0, -0.6, -ZO], [XO1, GF - 0.02, ZO], "concrete");
  A.box([XI0, GF - 0.02, -ZI], [XI1, GF, ZI], "concrete_floor");
  wall(A, { axis: "x", outer: ZO, inner: ZI, aOut: [XO0, XO1], aIn: [XI0, XI1], y0: 0, y1: CEIL, holes: [], ...m, panel: pnC("front", 0, CEIL, GF) });
  wall(A, { axis: "x", outer: -ZO, inner: -ZI, aOut: [XO0, XO1], aIn: [XI0, XI1], y0: 0, y1: CEIL, holes: [rd], ...m, panel: pnC("rear", 0, CEIL, GF) });
  wall(A, { axis: "z", outer: XO1, inner: XI1, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0: 0, y1: CEIL, holes: [emb], ...m, panel: pnC("east", 0, CEIL, GF) });
  wall(A, { axis: "z", outer: XO0, inner: XI0, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0: 0, y1: CEIL, holes: [], ...m, panel: pnC("west", 0, CEIL, GF) });
  A.rbox([XO0 - 0.3, CEIL, -ZO - 0.3], [XO1 + 0.5, ROOF, ZO + 0.3], 0.25, "concrete");   // roof slab, overhang over the embrasure
  A.rbox([XO0 - 0.3, -0.6, -ZO - 0.3], [XO1 + 0.3, 0.0, ZO + 0.3], 0.1, "concrete");    // footing, flush (the floor stays level with the ground for the gun)
  // embrasure hood and cheeks (the recess of the photo), and the wing wall: a vertical base and a sloping top
  A.rbox([XO1 - 0.1, 1.95, -1.9], [XO1 + 0.9, ROOF - 0.2, 1.9], 0.2, "concrete");
  A.rbox([XO1 - 0.1, 0.0, -2.5], [XO1 + 0.9, 1.95, -1.75], 0.15, "concrete");
  A.box([XO1, -0.6, 2.4], [7.6, 0.9, ZO + 0.1], "concrete");
  A.prism(2, 2.4, ZO + 0.1, [[XO1, 0.9], [XO1, ROOF], [7.6, 0.9]], "concrete");
  A.rbox([7.3, -0.6, 2.3], [7.8, 1.1, ZO + 0.2], 0.15, "concrete");   // nose of the wing wall
  // earth on the sea face and the west flank (walkable slopes up to the roof edge)
  A.prism(0, XO0 - 0.3, XO1, [[ZO, -0.3], [ZO, ROOF - 0.5], [ZO + 4.0, -0.3]], "turf");
  A.prism(2, -ZO, ZO, [[XO0, -0.3], [XO0, ROOF - 0.6], [XO0 - 4.0, -0.3]], "turf");
  // rear: blast wall in front of the door, a step
  A.box([-2.4, -0.3, -ZO - 2.4], [0.2, 2.2, -ZO - 1.9], "concrete");
  A.box([rd.a0, -0.2, -ZO - 0.6], [rd.a1, GF, -ZO], "concrete");
  // interior: the PaK 40 stand-in (trunnion 1.05 m behind the inner face), 5 cm shell crates, cache, bench, lamps
  A.meta.companions = [{ asset: "gun_pak40", pos: [XI1 - 1.05, 0, 0], yaw: 90 }];
  // D1 fittings (cosmetic, appended last): door hinge knuckles, roof ventilation pipes
  { const T0 = A.parts.length;
    doorKnuckles(A, { axis: "x", outer: -ZO, inner: -ZI, c: -1.4, floor: GF, w: 1.0, h: 2.0, hingeSide: -1, steel: true });
    ventPipe(A, -2.6, ROOF, -2.2); ventPipe(A, 0.9, ROOF, -2.8, { h: 0.35, dia: 0.13 });
    d1(A).push(...A.parts.splice(T0)); }
  gunAmmoStack(A, "5 cm shell crates", -1.2, GF, 1.85, 0, { cal: "50", crates: 6, carriers: 0, loose: 2 });
  smallArmsCache(A, "Small-arms ammunition", XI0 + 0.35, GF, 0.9, 90);
  shelfZ(A, "Shell rack", -1.3, 0.2, XI0, 1, GF, 1.5, 0.35, 3, "plank_z", (y, xa, xx, i) => {
    if (i > 0) for (let z = -1.1; z < 0.1; z += 0.4) crateBox(A, (xa + xx) / 2, y, z, 0.26, 0.17, 0.34, "crate_olive");
  });
  for (const [x, z] of [[-1.5, 0.0], [1.0, -1.2]]) lamp(A, x, CEIL, z);
  const eye = GF + SLIT.eye;
  setNav(A, {
    effect: "block",
    rooms: [room("gun_room", [XI0, GF, -ZI], [XI1, CEIL, ZI])],
    doors: [{ a: "gun_room", b: "outside", p: [-1.4, GF, -3.1] }],
    entrances: [{ p: [-1.4, GF, -ZO - 0.9], dir: [0, 0, 1], room: "gun_room" }],
    corners: corners("gun_room", XI0, XI1, -ZI, ZI, GF),
    firingPoints: [fire([XI1 - 1.05, GF, 0], 90, "embrasure"), fire([XI1 - 0.5, GF, -0.7], 90, "embrasure")],
    cover: [cover([5.6, 0, 1.8], 0, "high", ["left"]), cover([-1.1, 0, -ZO - 1.5], 180, "high", ["left", "right"]), cover([-5.0, 0, -4.6], 0, "high", ["left"])],
  });
  setTour(A, { pos: [-1.4, 0, -8.0], yaw: 180 }, [
    { goto: [1.2, -7.4] }, { goto: [0.8, -5.2], note: "round the blast wall" }, { goto: [-1.4, -4.6] },
    { interact: "r667_door", aim: [-1.2, GF + 1.1, -ZI - 0.08], expectOpen: true },
    { goto: [-1.4, -1.85], expectY: GF, note: "inside the gun room" },
    { goto: [1.3, -1.8], expectY: GF, note: "beside the gun at the embrasure" },
    { look: [12, eye, -1.0], hold: 0.8 }, { shot: "mpu_r667_embrasure" },
    { look: [-2.5, 0.8, 1.6], hold: 0.6 }, { shot: "mpu_r667_interior" },
    { goto: [-1.4, -1.85] }, { goto: [-1.4, -4.6] }, { goto: [0.8, -5.2] }, { goto: [1.2, -7.4], note: "leave the casemate" },
  ], { companions: true });
  return A;
}

// ============================================================================ R612, unfinished (WN64)
// The WN64 R612 was still under construction on 6 June: here the floor slab and the front wall with the embrasure
// are poured, the east wall stands to full height with its starter bars, the west wall to 1.2 m with the formwork
// standing above it, the rear wall up to the open gun-door opening. No roof: a timber deck on props over the rear
// half, rebar mats over it. Cement mixer, sand, cement sacks and timber outside.
export function r612Unfinished() {
  const GF = 0.02, CEIL = 2.7, XO = 4.2, XI = 3.0, ZF = 3.5, ZFI = 2.0, ZB = -4.2, ZBI = -3.0, pt = 0.02;
  const A = new Asset({
    id: "aw_r612_unfinished", version: 1, name: "R612 casemate (unfinished)", category: "Atlantic Wall",
    description: "Regelbau 612 gun casemate caught under construction as at WN64: raw fresh concrete, the front wall and embrasure poured, the west wall to 1.2 m with timber formwork above, exposed starter bars and rebar mats, no roof slab (a timber deck on props over the rear half), the gun-door opening unfitted. A stalled cement mixer, sand heap, cement sacks and a timber stack outside; small-arms cache inside.",
    floors: 1, enterable: true, footprint: [14.0, 11.0], plinthDepth: 0.6, maxGrade: 0.4,
  });
  concreteMats(A, CEIL, true); mats(A, "rebar", "form_board", "sand", "cement", "mixer_paint", "tyre", "steel_dark");
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  const R = rng(612);
  const gun = slit(A, { axis: "x", outer: ZF, inner: ZFI, a0: -1.0, a1: 1.0, y0: 0.62, y1: 1.7, step: { da: 0.33, dyT: 0.24, dyB: 0 } });
  A.box([-XO, -0.6, ZB], [XO, GF - 0.02, ZF], "concrete");
  A.box([-XI, GF - 0.02, ZBI], [XI, GF, ZFI], "concrete_floor");
  const gdo = { a0: -1.3, a1: 1.3, y0: -10, y1: 2.2 };   // gun-door opening, no doors fitted
  wall(A, { axis: "x", outer: ZF, inner: ZFI, aOut: [-XO, XO], aIn: [-XI, XI], y0: 0, y1: 2.2, holes: [gun], ...m, panel: pnC("front", 0, 2.2, GF) });
  wall(A, { axis: "x", outer: ZB, inner: ZBI, aOut: [-XO, XO], aIn: [-XI, XI], y0: 0, y1: 2.2, holes: [gdo], ...m, panel: pnC("rear", 0, 2.2, GF) });
  wall(A, { axis: "z", outer: XO, inner: XI, aOut: [ZBI - pt, ZFI + pt], aIn: [ZBI, ZFI], y0: 0, y1: CEIL, holes: [], ...m, panel: pnC("east", 0, CEIL, GF) });
  wall(A, { axis: "z", outer: -XO, inner: -XI, aOut: [ZBI - pt, ZFI + pt], aIn: [ZBI, ZFI], y0: 0, y1: 1.2, holes: [], ...m, panel: pnC("west", 0, 1.2, GF) });
  A.box([-1.3, 2.2, ZB], [1.3, 2.32, ZBI], "form_board");   // timber lintel form over the gun-door opening
  // starter bars sticking up from the wall tops (visual), in two layers
  const bars = (x0, x1, z0, z1, y, h) => {
    const alongX = x1 - x0 > z1 - z0;
    for (let s = (alongX ? x0 : z0) + 0.15; s < (alongX ? x1 : z1) - 0.1; s += 0.25)
      for (const f of [0.18, 0.82]) {
        const x = alongX ? s : x0 + (x1 - x0) * f, z = alongX ? z0 + (z1 - z0) * f : s, hh = h * (0.75 + 0.5 * R());
        A.obox([x, y + hh / 2, z], [0.025, hh, 0.025], [(R() - 0.5) * 8, 0, (R() - 0.5) * 8], "rebar", { k: 0 });
      }
  };
  bars(-XO, XO, ZFI, ZF, 2.2, 0.9); bars(XI, XO, ZBI, ZFI, CEIL, 0.7); bars(-XO, XO, ZB, ZBI, 2.2, 0.8); bars(-XO, -XI, ZBI, ZFI, 1.2, 1.1);
  // formwork above the low west wall: boards both faces, walers and raking props
  for (const [x0, x1] of [[-XO - 0.05, -XO], [-XI, -XI + 0.05]]) {
    for (let z = ZBI; z < ZFI - 0.01; z += 0.6) A.box([x0, 1.2, z], [x1, CEIL, Math.min(z + 0.58, ZFI)], "form_board");
  }
  for (const y of [1.55, 2.3]) for (const x of [-XO - 0.13, -XI + 0.05]) A.box([x, y, ZBI], [x + 0.08, y + 0.12, ZFI], "plank_z", { k: 0 });
  for (let z = ZBI + 0.6; z < ZFI; z += 1.6) A.obox([-XO - 0.8, 1.1, z], [0.1, 2.3, 0.1], [0, 0, -22], "plank_y", { k: 0 });
  // roof deck on props over the rear half, rebar mat on it; open over the gun
  const DY = CEIL;
  for (let x = -2.6; x <= 2.7; x += 1.3) for (const z of [-2.6, -0.8]) A.box([x - 0.06, GF, z - 0.06], [x + 0.06, DY - 0.2, z + 0.06], "plank_y");
  for (const z of [-2.6, -0.8]) A.box([-XO, DY - 0.2, z - 0.08], [XO, DY - 0.05, z + 0.08], "plank_x", { k: 0 });
  A.box([-XO, DY - 0.05, ZB], [XO, DY, -0.6], "form_board");
  for (let x = -XO + 0.1; x < XO; x += 0.3) A.box([x - 0.012, DY + 0.05, ZB + 0.1], [x + 0.012, DY + 0.075, -0.7], "rebar", { k: 0 });
  for (let z = ZB + 0.1; z < -0.7; z += 0.3) A.box([-XO + 0.1, DY + 0.075, z - 0.012], [XO - 0.1, DY + 0.1, z + 0.012], "rebar", { k: 0 });
  // cement mixer (stalled) on the west side
  {
    const x = -6.6, z = -1.0;
    A.beginFurniture("Cement mixer");
    for (const s of [-1, 1]) A.box([x - 0.9, 0.35, z + s * 0.45 - 0.04], [x + 0.9, 0.45, z + s * 0.45 + 0.04], "mixer_paint");
    for (const s of [-1, 1]) { A.obox([x - 0.3, 0.35, z + s * 0.62], [0.7, 0.7, 0.18], [0, 0, 0], "tyre"); A.obox([x - 0.3, 0.35, z + s * 0.62], [0.7, 0.7, 0.18], [0, 0, 45], "tyre"); }
    A.box([x + 0.75, 0, z - 0.06], [x + 0.85, 0.4, z + 0.06], "mixer_paint");
    for (const s of [-1, 1]) A.box([x + 0.1, 0.45, z + s * 0.45 - 0.05], [x + 0.2, 1.3, z + s * 0.45 + 0.05], "mixer_paint");
    const drum = [x + 0.15, 1.25, z];
    for (const a of [0, 45]) A.obox(drum, [0.95, 0.95, 0.8], [a, 0, 30], "mixer_paint");
    A.obox([x + 0.48, 1.45, z], [0.4, 0.55, 0.55], [0, 0, 30], "mixer_paint");
    A.box([x - 0.75, 0.45, z - 0.3], [x - 0.25, 1.0, z + 0.3], "steel_dark");   // engine
    A.box([x + 0.7, 0.45, z - 0.45], [x + 1.3, 0.55, z + 0.45], "cement", { k: 0 });   // spilt cement
    A.endFurniture();
  }
  // sand heap, cement sacks, timber stack, a wheelbarrow
  A.prism(0, -8.2, -6.8, [[1.2, -0.3], [2.3, 1.0], [3.6, -0.3]], "sand");
  A.prism(2, 1.2, 3.6, [[-8.8, -0.3], [-7.5, 1.0], [-6.4, -0.3]], "sand", { k: 0 });
  sacks(A, "Cement sacks", [[-6.4, 0, -3.2, 10], [-5.8, 0, -3.25, -5], [-6.1, 0.28, -3.2, 3], [-6.1, 0, -2.5, 80]]);
  A.beginFurniture("Timber stack");
  for (let k = 0; k < 6; k++) A.box([5.0 + (k % 3) * 0.32, 0.12 + Math.floor(k / 3) * 0.12, -3.8], [5.28 + (k % 3) * 0.32, 0.24 + Math.floor(k / 3) * 0.12, 0.2], "form_board");
  for (const z of [-3.4, -1.8, -0.2]) A.box([4.95, 0, z - 0.06], [6.0, 0.12, z + 0.06], "plank_x");
  A.endFurniture();
  A.beginFurniture("Wheelbarrow");
  A.obox([-5.2, 0.42, 1.8], [0.6, 0.3, 0.9], [-8, 20, 0], "steel_dark");
  A.obox([-5.0, 0.2, 2.4], [0.08, 0.4, 0.4], [0, 20, 0], "tyre");
  A.endFurniture();
  smallArmsCache(A, "Small-arms ammunition", 2.4, GF, -2.4, 90);
  setNav(A, {
    effect: "block",
    rooms: [room("gun_room", [-XI, GF, ZBI], [XI, CEIL, ZFI])],
    doors: [{ a: "gun_room", b: "outside", p: [0, GF, (ZB + ZBI) / 2] }],
    entrances: [{ p: [0, GF, ZB - 0.8], dir: [0, 0, 1], room: "gun_room" }],
    corners: corners("gun_room", -XI, XI, ZBI, ZFI, GF),
    firingPoints: [fire([0, GF, 1.1], 0, "embrasure")],
    cover: [cover([-3.5, 0, ZB - 0.5], 0, "high", ["left"]), cover([3.5, 0, ZB - 0.5], 0, "high", ["right"]), cover([-6.6, 0, -2.0], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [0.8, 0, -8.5], yaw: 180 }, [
    { goto: [0.6, -5.6], note: "approach the unfitted gun-door opening" },
    { goto: [0.6, -2.4], expectY: GF, note: "inside the unfinished gun room" },
    { goto: [-1.8, 0.8], expectY: GF },
    { look: [0, 1.2, 14], hold: 0.8 }, { shot: "mpu_r612_embrasure" },
    { look: [-3.2, 2.2, -1.0], hold: 0.6 }, { shot: "mpu_r612_formwork" },
    { goto: [0.6, -2.4] }, { goto: [0.6, -5.6] }, { goto: [-4.8, -6.0], note: "round to the mixer" },
    { look: [-6.6, 1.0, -1.0], hold: 0.6 }, { shot: "mpu_r612_mixer" },
  ]);
  return A;
}

// ============================================================================ Tobruks (Ringstand 58c family)
// A buried concrete chamber (floor 2.0 m down) whose roof slab sits flush with the ground, entered from the rear by
// a ramped entrance trench. mg: ringstand with the MG rail ring; the access hatch and the ring share one opening, a
// short stair under it raises the gunner so his chest clears the ring. mortar: an open circular pit 1.4 m deep with
// the mortar pedestal and range ring. turret: Panzerstellung, a cast light-tank turret (37 mm, APX-R type, as in
// reference photo 06) on the ring; static dressing, not crewable.
function tobruk(kind) {
  const info = {
    mg: { id: "aw_tobruk_mg", name: "Tobruk ringstand (MG)", desc: "Ringstand 58c: a buried concrete chamber whose roof is flush with the ground, the round MG ring opening at the front with its rail, a short stair under it so the gunner stands chest-high in the ring. Entered by a ramped trench from the rear; small-arms cache inside." },
    mortar: { id: "aw_tobruk_mortar", name: "Mortar Tobruk", desc: "Granatwerferstand: an open circular concrete pit 1.4 m deep and 2.4 m across with the mortar pedestal, the painted range ring and an ammunition niche, entered by a ramped trench from the rear. The mortar itself is not modelled (guns come only from supplied models)." },
    turret: { id: "aw_tobruk_turret", name: "Tank-turret Tobruk (Panzerstellung)", desc: "Panzerstellung as on Omaha (reference photo 06): a cast light-tank turret with its 37 mm gun on a Tobruk ring set in a flush concrete slab, the buried crew chamber beneath, entered by a ramped trench from the rear. The turret is static dressing (not crewable)." },
  }[kind];
  const A = new Asset({ id: info.id, version: 2, name: info.name, category: "Atlantic Wall", description: info.desc,
    floors: 0, enterable: true, footprint: [4.6, 9.4], plinthDepth: 0.3, maxGrade: 0.5 });
  stdMaterials(A); mats(A, "wattle", "picket", "spoil", "mud", "turret_paint", "steel_dark", "rust");
  const C = A.materials.find((q) => q.name === "concrete"); Object.assign(C, { albedo: [0.29, 0.28, 0.25], pattern: "bunker", grain: 3 });
  const pit = kind === "mortar";
  const GF = pit ? -1.4 : -2.0, CEIL = -0.15, RT = 0.25, X = 1.6, XI = 1.1, Z = 2.0, ZI = 1.5;
  // floor and walls
  A.box([-X, GF - 0.35, -Z], [X, GF - 0.02, Z], "concrete");
  A.box([-XI, GF - 0.02, -ZI], [XI, GF, ZI], "concrete_floor");
  const top = pit ? RT : CEIL;
  for (const s of [-1, 1]) A.box([s > 0 ? XI : -X, GF - 0.02, -Z], [s > 0 ? X : -XI, top, Z], "concrete");
  A.box([-XI, GF - 0.02, ZI], [XI, top, Z], "concrete");
  A.box([-XI, GF - 0.02, -Z], [-0.5, top, -ZI], "concrete"); A.box([0.5, GF - 0.02, -Z], [XI, top, -ZI], "concrete");
  if (GF + 1.95 < top) A.box([-0.5, GF + 1.95, -Z], [0.5, top, -ZI], "concrete");
  if (!pit) {
    // roof slab with the opening: mg 0.9 x 2.55 (the ring at the front, the access hatch over the stair behind it: the
    // stair must stay under open sky for a standing man), turret 1.3 x 1.3 under the turret ring
    const o = kind === "mg" ? { x: 0.45, z0: -1.5, z1: 1.05 } : { x: 0.65, z0: -0.65, z1: 0.65 };
    A.box([-X, CEIL, -Z], [X, RT, o.z0], "concrete"); A.box([-X, CEIL, o.z1], [X, RT, Z], "concrete");
    A.box([-X, CEIL, o.z0], [-o.x, RT, o.z1], "concrete"); A.box([o.x, CEIL, o.z0], [X, RT, o.z1], "concrete");
    // apron lip; D1: for the ringstand it frames the opening (it used to cover it): the old part keeps its slot as the rear piece
    if (kind === "mg") {
      A.rbox([-X - 0.3, RT - 0.25, -Z - 0.3], [X + 0.3, RT, o.z0], 0.08, "concrete", { k: 0 });
      A.rbox([-X - 0.3, RT - 0.25, o.z1], [X + 0.3, RT, Z + 0.3], 0.08, "concrete", { into: d1(A), k: 0 });
      for (const sx of [-1, 1]) A.box([sx > 0 ? o.x : -X - 0.3, RT - 0.25, o.z0 - 0.01], [sx > 0 ? X + 0.3 : -o.x, RT, o.z1 + 0.01], "concrete", { into: d1(A), k: 0, bv: 0.03 });
    } else A.rbox([-X - 0.3, RT - 0.25, -Z - 0.3], [X + 0.3, RT, Z + 0.3], 0.08, "concrete", { k: 0 });
    if (kind === "mg") {
      // ring collar and the MG rail
      for (const a of [0, 45]) A.obox([0, RT + 0.06, 0.55], [1.2, 0.12, 0.12], [0, a, 0], "concrete", { k: 0, v: 0 });   // D1: replaced by the ring below
      for (let k = 0; k < 8; k++) { const t = (k * Math.PI) / 4; A.obox([Math.sin(t) * 0.55, RT + 0.15, 0.55 + Math.cos(t) * 0.55], [0.44, 0.05, 0.05], [0, (k * 45) + 90, 0], "steel_dark", { k: 0, v: 0 }); }
      flatRing(A, [0, RT - 0.01, 0.55], 0.72, 0.24, 0.13, "concrete", { into: d1(A), k: 0 });            // D1: round ring collar
      flatRing(A, [0, RT + 0.12, 0.55], 0.575, 0.05, 0.04, "steel_dark", { into: d1(A), k: 0 });          // and its MG rail
      for (let k = 0; k < 6; k++) { const t = (k * Math.PI) / 3 + 0.5; A.cyl([Math.sin(t) * 0.55, RT + 0.08, 0.55 + Math.cos(t) * 0.55], [0.03, 0.1, 0.03], "steel_dark", { into: d1(A), k: 0, seg: 6 }); }
      stairs(A, { x0: -0.42, x1: 0.42, z0: -1.1, dir: 1, y0: GF, rise: 1.02, n: 4, run: 1.2, openSide: 1, core: true });   // 40 deg
      A.box([-0.45, GF, 0.1], [0.45, GF + 1.02, 1.05], "concrete_wash");   // gunner's platform under the ring
    } else {
      // Panzerstellung turret: faceted cast body, cone mantlet and 37 mm barrel facing +Z, cupola, hatch handles
      // D1: the faceted boxes stay as the (now invisible) collider; the visible turret is a round casting below
      const ty = RT, T = "turret_paint", H = { v: 0 }, G = { k: 0, v: 0 };
      A.box([-0.68, ty, -0.6], [0.68, ty + 0.12, 0.6], T, H);
      A.box([-0.62, ty + 0.12, -0.52], [0.62, ty + 0.72, 0.5], T, H);
      for (const s of [-1, 1]) for (const zz of [-1, 1]) A.obox([s * 0.5, ty + 0.42, zz * 0.42], [0.36, 0.6, 0.36], [0, 45, 0], T, H);
      A.obox([0, ty + 0.62, -0.1], [1.12, 0.22, 0.95], [-6, 0, 0], T, H);
      A.box([-0.24, ty + 0.72, -0.35], [0.24, ty + 0.92, 0.1], T, H);                  // cupola
      A.box([-0.27, ty + 0.92, -0.38], [0.27, ty + 0.95, 0.13], "steel_dark", H);
      A.obox([0.0, ty + 0.4, 0.62], [0.36, 0.36, 0.3], [0, 0, 45], T, H);             // mantlet block
      A.obox([0.0, ty + 0.4, 0.84], [0.22, 0.22, 0.28], [0, 0, 0], T, H);             // cone
      A.obox([0.0, ty + 0.4, 1.08], [0.11, 0.11, 0.36], [0, 0, 0], "steel_dark", G);   // 37 mm barrel
      A.obox([0.18, ty + 0.4, 0.66], [0.05, 0.05, 0.12], [0, 0, 0], "steel_dark", G);   // coaxial MG port
      for (const x of [-0.35, 0.2]) A.box([x, ty + 0.72, 0.05], [x + 0.15, ty + 0.78, 0.09], "steel_dark", G);   // handles
      A.box([-0.7, ty - 0.02, -0.62], [0.7, ty + 0.02, 0.62], "rust", G);    // turret ring
      const V = { into: d1(A), k: 0 };
      A.cyl([0, ty + 0.01, 0], [1.42, 0.05, 1.3], "rust", { ...V, seg: 32 });                                  // turret ring
      A.cyl([0, ty + 0.06, 0], [1.34, 0.08, 1.2], T, { ...V, seg: 32 });                                         // base flange
      A.cyl([0, ty + 0.4, -0.02], [1.26, 0.62, 1.1], T, { ...V, seg: 32, top: 0.84 });                          // cast body, tapering
      A.cone([0, ty + 0.75, -0.04], [1.06, 0.1, 0.92], T, { ...V, seg: 32, top: 0.82 });                         // rounded roof edge
      A.cyl([0.0, ty + 0.86, -0.14], [0.5, 0.16, 0.5], T, { ...V, seg: 24 });                                    // cupola
      A.cone([0.0, ty + 0.97, -0.14], [0.46, 0.08, 0.46], T, { ...V, seg: 24, top: 0.55 });                      // cupola dome
      A.cyl([0.0, ty + 1.02, -0.14], [0.22, 0.03, 0.22], "steel_dark", { ...V, seg: 16 });                        // cupola hatch
      for (let k = 0; k < 5; k++) { const a = -1.1 + k * 0.55; A.obox([Math.sin(a) * 0.25, ty + 0.88, -0.14 + Math.cos(a) * 0.25], [0.09, 0.025, 0.02], [0, (a * 180) / Math.PI, 0], "steel_dark", V); }   // vision slits
      rod(A, [-0.22, ty + 0.4, 0.52], [0.22, ty + 0.4, 0.52], 0.36, T, { ...V, seg: 20 });                        // mantlet trunnion housing
      rod(A, [0, ty + 0.4, 0.55], [0, ty + 0.4, 0.86], 0.26, T, { ...V, seg: 20, top: 0.55 });                    // gun mantlet cone
      rod(A, [0, ty + 0.4, 0.8], [0, ty + 0.4, 1.26], 0.075, "steel_dark", { ...V, seg: 14 });                    // 37 mm barrel
      rod(A, [0, ty + 0.4, 1.2], [0, ty + 0.4, 1.27], 0.09, "steel_dark", { ...V, seg: 14 });                     // muzzle ring
      rod(A, [0.17, ty + 0.4, 0.62], [0.17, ty + 0.4, 0.76], 0.045, "steel_dark", { ...V, seg: 10 });            // coaxial MG
      for (const s of [-1, 1]) {                                                                                   // lifting eyes, hatch handles
        rod(A, [s * 0.5, ty + 0.62, -0.25], [s * 0.5, ty + 0.62, -0.1], 0.03, "steel_dark", { ...V, seg: 6 });
        rod(A, [s * 0.12, ty + 1.04, -0.08], [s * 0.12, ty + 1.04, -0.2], 0.018, "steel_dark", { ...V, seg: 6 });
      }
      A.box([-0.12, GF, 0.6], [0.12, CEIL, 0.66], "steel_dark", { k: 0 });          // ladder rails
    }
  } else {
    // mortar pit: octagonal collar, pedestal with the base-plate pin, range ring, ammunition niche in the front wall
    for (const a of [0, 45]) A.obox([0, RT + 0.05, 0], [2.6, 0.1, 0.25], [0, a, 0], "concrete", { k: 0, v: 0 });   // D1: replaced by the ring below
    for (const a of [90, 135]) A.obox([0, RT + 0.05, 0], [2.6, 0.1, 0.25], [0, a, 0], "concrete", { k: 0, v: 0 });
    flatRing(A, [0, RT - 0.02, 0], 1.42, 0.3, 0.14, "concrete", { into: d1(A), k: 0 });   // D1: round pit collar
    A.rbox([-0.35, GF, -0.35], [0.35, GF + 0.45, 0.35], 0.08, "concrete_wash");
    A.box([-0.06, GF + 0.45, -0.06], [0.06, GF + 0.6, 0.06], "steel_dark", { k: 0 });
    for (let k = 0; k < 16; k++) { const t = (k * Math.PI) / 8; A.obox([Math.sin(t) * 0.75, GF + 0.004, Math.cos(t) * 0.75], [0.28, 0.008, 0.04], [0, k * 22.5 + 90, 0], k % 4 ? "plaster" : "soot", { k: 0 }); }
    crates(A, "Mortar bomb crates", [[-0.75, GF, 1.25, 0.6, 0.18, 0.28], [-0.75, GF + 0.18, 1.25, 0.6, 0.18, 0.28], [0.1, GF, 1.25, 0.6, 0.18, 0.28]], "crate_olive");
  }
  // entrance: rear door opening, then a ramped trench up to grade (wattle sides, earth fill behind)
  const ramp0 = -Z - 0.7, ramp1 = -Z - 5.0, rise = -GF;
  A.box([-0.5, GF - 0.2, ramp0 - 0.05], [0.5, GF, -Z], "mud");   // flat landing: the ramp starts clear of the roof edge
  const ang = (Math.atan2(rise, ramp0 - ramp1) * 180) / Math.PI, L = Math.hypot(rise, ramp0 - ramp1);
  A.obox([0, GF / 2 - 0.12, (ramp0 + ramp1) / 2], [1.0, 0.24, L + 0.1], [ang, 0, 0], "mud", { v: 0 });   // collision ramp
  for (let k = 0; k < 9; k++) { const zz = ramp0 - 0.25 - k * 0.48, yy = GF * (zz - ramp1) / (ramp0 - ramp1); A.box([-0.45, yy - 0.12, zz - 0.07], [0.45, yy + 0.04, zz + 0.07], "log_x", { k: 0 }); }
  A.obox([0, GF / 2 - 0.2, (ramp0 + ramp1) / 2], [1.0, 0.2, L + 0.1], [ang, 0, 0], "mud", { k: 0 });
  for (const s of [-1, 1]) {
    A.box([s * 0.5 + (s > 0 ? 0 : -0.1), GF - 0.2, ramp1], [s * 0.5 + (s > 0 ? 0.1 : 0), 0.15, -Z], "wattle");
    A.box([s * 0.6 + (s > 0 ? 0 : -1.55), GF - 0.2, ramp1], [s * 0.6 + (s > 0 ? 1.55 : 0), 0.15, -Z], "spoil");   // spoil heaps cover the cut's side transition
    for (let zz = ramp1 + 0.2; zz < ramp0; zz += 0.9) A.box([s * 0.47 - 0.03, GF, zz - 0.03], [s * 0.47 + 0.03, 0.25, zz + 0.03], "picket", { k: 0 });
  }
  // trampled apron at grade over the cut's open end, and a ring round the front and flanks over the cut's step (CUT_RING)
  A.box([-2.15, -0.25, ramp1 - CUT_APRON], [2.15, 0.03, ramp1], "spoil");
  A.box([-X - CUT_RING, -0.25, Z], [X + CUT_RING, 0.03, Z + CUT_RING], "spoil");
  for (const s of [-1, 1]) A.box([s > 0 ? X : -X - CUT_RING, -0.25, -Z - CUT_RING], [s > 0 ? X + CUT_RING : -X, 0.03, Z], "spoil");
  smallArmsCache(A, "Small-arms ammunition", pit ? 0.68 : -0.8, GF, pit ? -0.2 : 1.0, 90, { boxes: 3, bandoliers: 2 });
  if (!pit) lamp(A, 0.6, CEIL, -1.0);
  const fp = kind === "mg" ? [fire([0, GF + 1.02, 0.55], 0, "mg")] : kind === "mortar" ? [{ ...fire([0, GF, -0.6], 0, "firestep"), indirect: true }] : [];
  const yAt = (z) => GF * (z - ramp1) / (ramp0 - ramp1);
  setNav(A, {
    effect: "block",
    rooms: [room("chamber", [-XI, GF, -ZI], [XI, pit ? RT : CEIL, ZI])],
    doors: [{ a: "chamber", b: "outside", p: [0, GF, -1.75] }],
    entrances: [{ p: [0, 0, ramp1 - 0.3], dir: [0, 0, 1], room: "chamber" }],
    corners: corners("chamber", -XI, XI, -ZI, ZI, GF, 0.35),
    firingPoints: fp,
    cover: [cover([0, yAt(-4.2), -4.2], 90, "low", ["over"]), cover([0, yAt(-4.2), -4.2], 270, "low", ["over"])].concat(kind === "turret" ? [cover([0, RT, -1.05], 0, "low", ["left", "right"])] : []),
    areas: [{ outline: rect(-0.5, ramp1, 0.5, -Z), area: "trench" }],
  }, [{ outline: rect(-X + CUT_PAD, -Z + CUT_PAD, X - CUT_PAD, Z - CUT_PAD), depth: -GF + 0.4, bank: 0.15 }, { outline: rect(-1.2, ramp1 + CUT_SETBACK, 1.2, -Z + 0.6), depth: -GF + 0.3, bank: 0.2 }]);
  const eyeY = kind === "mg" ? GF + 1.02 + SLIT.eye : GF + SLIT.eye;
  const inside = kind === "mg" ? [0, -1.62] : [0, -1.0];   // straight in through the door (no diagonal past the jambs)
  const steps = [
    { goto: [0, ramp1 - 0.2], note: "top of the entrance ramp" },
    { goto: [0, -2.3] }, { goto: inside }, { wait: 0.4 },
    { goto: inside, expectY: GF, note: "down into the chamber" },
  ];
  if (kind === "mg") steps.push({ goto: [0, 0.55], expectY: GF + 1.02, note: "up into the ring" }, { look: [0, eyeY, 12], hold: 0.8 }, { shot: "mpu_tobruk_mg_ring" }, { goto: inside }, { wait: 0.4 }, { goto: inside, expectY: GF, note: "back down from the ring" });
  else if (kind === "mortar") steps.push({ goto: [-0.7, 0.3] }, { wait: 0.4 }, { goto: [-0.7, 0.3], expectY: GF }, { look: [0, eyeY, 10], hold: 0.6 }, { shot: "mpu_tobruk_mortar_pit" });
  else steps.push({ look: [0, -0.4, 0.9], hold: 0.6 }, { shot: "mpu_tobruk_turret_chamber" });
  steps.push({ goto: [0, -2.4] }, { goto: [0, ramp1 - 0.4], expectY: 0, note: "back up the ramp" });
  setTour(A, { pos: [0, 0, ramp1 - 2.0], yaw: 180 }, steps, { cut: true });
  return A;
}
export const tobrukMg = () => tobruk("mg");
export const tobrukMortar = () => tobruk("mortar");
export const tobrukTurret = () => tobruk("turret");

// ============================================================================ trench pieces
// All pieces join end to end: a run enters at local (-L/2, 0) and leaves at (+L/2, 0) along +X (the corner leaves
// along +Z at (0, +L/2)), floor 1.5 m down, front (parapet and firestep) toward +Z.
// GX1 (docs/graphics-overhaul.md#gx1-terrain-conformance): the engine carves joined pieces as one terrain network along
// `path` (x, z, depth scale), with the niches as `branches`, and generates the revetment along the cut; the slab parts
// listed in `replace` are dropped at load while content/settings/terrain_blend.json is enabled
export const TRENCH_FLOOR_DROP = { materials: ["plank_x"], topBelow: -1.45 };
export const TRENCH_STEP_DROP = { materials: ["plank_x"], topAbove: -1.3, minWidth: 0.3 };   // the firestep's full-width top plank
export const TRENCH_REPLACE = { materials: ["wattle", "spoil", "earth", "turf", "picket", "plank_x"], aboveY: -1.0, keepCollision: ["wattle", "spoil"] };
// GX1: the trench floor reads as dug soil (the default mud texture set has a green cast)
// and its plank edges are dark, worn field timber (the generated duckboards replace the asset's, "floorDrop")
function trenchMud(A) {
  const m = A.materials.find((x) => x.name === "mud"); if (m) m.texture = "soil";
  const p = A.materials.find((x) => x.name === "plank_x"); if (p) { p.albedo = [0.15, 0.11, 0.075]; p.texture = "wood"; }
}
export function trenchNiche(pts, niche) {   // from the centreline into the rear wall (right of the run), to the niche's back
  const [i, t] = niche, p = pts[i], q = pts[i + 1], L = Math.hypot(q[0] - p[0], q[1] - p[1]), ux = (q[0] - p[0]) / L, uz = (q[1] - p[1]) / L;
  const c = [p[0] + ux * L * t, p[1] + uz * L * t];
  return [[r3(c[0]), r3(c[1]), 1], [r3(c[0] + uz * 1.25), r3(c[1] - ux * 1.25), 1]];
}
function trenchPiece(id, name, desc, pts, opt, footprint, tourPts) {
  const A = new Asset({ id, version: 3, name, category: "Atlantic Wall", description: desc, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  stdMaterials(A);
  trenchMud(A);
  const r = trenchRun(A, pts, opt);
  const unit = (a, b) => { const L = Math.hypot(b[0] - a[0], b[1] - a[1]); return [(b[0] - a[0]) / L, (b[1] - a[1]) / L]; };
  const ends = [[pts[0], unit(pts[1], pts[0])], [pts[pts.length - 1], unit(pts[pts.length - 2], pts[pts.length - 1])]];
  setSnap(A, ends.map(([p, d]) => sock([p[0], 0, p[1]], d, "trench")));
  ends.forEach(([p, d], k) => trenchCap(A, p, d, k));
  const D = TR.D;
  const fps = [];
  pts.forEach((p, i) => { if (i < pts.length - 1 && (opt.firestep || []).includes(i)) { const q = pts[i + 1], L = Math.hypot(q[0] - p[0], q[1] - p[1]); for (let s = 1.0; s < L - 0.6; s += 2.0) { const ux = (q[0] - p[0]) / L, uz = (q[1] - p[1]) / L; fps.push(fire([p[0] + ux * s - uz * (TR.HW - TR.STEPW / 2), -D + TR.STEP, p[1] + uz * s + ux * (TR.HW - TR.STEPW / 2)], (Math.atan2(-uz, ux) * 180) / Math.PI, "firestep")); } } });
  setNav(A, { effect: "area", area: "trench", areas: [{ outline: r.floor, area: "trench" }], firingPoints: fps,
    cover: pts.slice(0, -1).map((p, i) => { const q = pts[i + 1], L = Math.hypot(q[0] - p[0], q[1] - p[1]), ux = (q[0] - p[0]) / L, uz = (q[1] - p[1]) / L; return cover([(p[0] + q[0]) / 2 + uz * 0.2, -D, (p[1] + q[1]) / 2 - ux * 0.2], (Math.atan2(-uz, ux) * 180) / Math.PI, "high", ["over"]); }) },
    [{ outline: r.cut, depth: D + 0.25, bank: 0.2 }]);
  A.meta.trench = { path: pts.map((p) => [p[0], p[1], 1]), branches: opt.niche ? [trenchNiche(pts, opt.niche)] : [], replace: TRENCH_REPLACE, floorDrop: TRENCH_FLOOR_DROP, stepDrop: TRENCH_STEP_DROP };
  return { A, D };
}
function trenchTour(A, D, x0, route, shot) {
  // the test places an entry piece joined at the run's -X end (tour.join), walks down its ramp and along the run
  setTour(A, { pos: [x0 - 8.0, 0, 0], yaw: 270 }, [
    { goto: [x0 - 6.7, 0], note: "top of the joined entry ramp" },
    { goto: [x0 - 2.6, 0], expectY: -D, note: "down into the trench" },
    ...route.map((p) => ({ goto: p, expectY: -D, note: "along the trench" })),
    { look: [route[route.length - 1][0] + 4, -D + 1.6, route[route.length - 1][1] + 3], hold: 0.5 }, { shot },
  ], { cut: true, join: { asset: "aw_trench_entry", pos: [x0 - 3, 0], yaw: 0 } });
}
export function trenchStraight() {
  const { A, D } = trenchPiece("aw_trench_straight", "Trench, straight (8 m, wattle)", "Straight 8 m fire trench, floor 1.5 m down: wattle revetment on pickets, duckboards, a firestep under the front parapet, parados behind, an ammunition niche with a small-arms cache in the rear wall. Joins end to end with the other trench pieces (cut into the ground).",
    [[-4, 0], [4, 0]], { firestep: [0], niche: [0, 0.3] }, [8, 6.6]);
  trenchTour(A, D, -4, [[-1.0, -0.15], [3.4, -0.15]], "mpu_trench_straight");
  return A;
}
export function trenchZigzag() {
  const pts = [[-6, 0], [-3.5, 0], [-1.6, 1.8], [1.6, 1.8], [3.5, 0], [6, 0]];
  const { A, D } = trenchPiece("aw_trench_zigzag", "Trench, zig-zag bay (12 m, wattle)", "12 m zig-zag trench piece: a fire bay pushed 1.8 m forward between two traverses, floor 1.5 m down, wattle revetment, duckboards, firesteps in the bays, ammunition niche with a small-arms cache. Joins end to end with the straight, corner and entry pieces.",
    pts, { firestep: [0, 2, 4], niche: [2, 0.5] }, [12, 8.8]);
  trenchTour(A, D, -6, [[-3.6, -0.1], [-1.6, 1.6], [1.6, 1.6], [3.5, -0.1], [5.6, -0.1]], "mpu_trench_zigzag");
  return A;
}
export function trenchCorner() {
  const pts = [[-4, 0], [0, 0], [0, 4]];
  const { A, D } = trenchPiece("aw_trench_corner", "Trench, corner (90 deg, wattle)", "Right-angle trench corner: enters along +X, leaves along +Z, floor 1.5 m down, wattle revetment, duckboards, a firestep on the first leg, ammunition niche with a small-arms cache. Joins end to end with the other trench pieces.",
    pts, { firestep: [0], niche: [0, 0.4] }, [7.4, 7.4]);
  trenchTour(A, D, -4, [[-0.1, -0.1], [-0.1, 3.6]], "mpu_trench_corner");
  return A;
}
// entry: a 3.6 m ramp from grade down to the trench floor, then 2.4 m of trench; joins at local (+3, 0)
export function trenchEntry() {
  const A = new Asset({ id: "aw_trench_entry", version: 3, name: "Trench, entry ramp (wattle)", description: "Trench end with a log-stepped ramp from the ground down to the 1.5 m trench floor, wattle revetment and a small-arms cache in a niche. Joins a trench run at its +X end.", category: "Atlantic Wall", floors: 0, enterable: false, footprint: [6, 6.6], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  stdMaterials(A);
  trenchMud(A);
  const D = TR.D;
  const r = trenchRun(A, [[0.4, 0], [3, 0]], { firestep: [], niche: [0, 0.5], endExt: [0.2, 0] });
  setSnap(A, [sock([3, 0, 0], [1, 0], "trench")]);
  trenchCap(A, [3, 0], [1, 0], 0);
  // ramp from (-3.2, grade) down to (0.4, -D): collision slab, log steps, wattle sides and fill
  const x0 = -3.3, x1 = 0.45, ang = (Math.atan2(D, x1 - x0) * 180) / Math.PI, L = Math.hypot(D, x1 - x0);
  A.obox([(x0 + x1) / 2, -D / 2 - 0.12, 0], [L + 0.1, 0.24, 1.2], [0, 0, -ang], "mud", { v: 0 });
  A.obox([(x0 + x1) / 2, -D / 2 - 0.2, 0], [L + 0.1, 0.2, 1.2], [0, 0, -ang], "mud", { k: 0 });
  for (let x = x0 + 0.3; x < x1; x += 0.45) { const y = -D * (x - x0) / (x1 - x0); A.box([x - 0.07, y - 0.1, -0.55], [x + 0.07, y + 0.04, 0.55], "log_z", { k: 0 }); }
  for (const s of [-1, 1]) {
    A.box([x0, -D - 0.2, s * (TR.HW + TR.RV / 2) - TR.RV / 2], [x1, 0.2, s * (TR.HW + TR.RV / 2) + TR.RV / 2], "wattle");
    A.box([x0, -D - 0.25, s > 0 ? TR.HW + TR.RV : -TR.HW - TR.RV - 1.6], [x1 + 0.3, 0.2, s > 0 ? TR.HW + TR.RV + 1.6 : -TR.HW - TR.RV], "spoil");   // spoil heaps over the cut's side transition
  }
  A.box([x0 - CUT_APRON, -0.25, -2.3], [x0, 0.03, 2.3], "spoil");   // trampled apron at grade over the cut's open end
  setNav(A, { effect: "area", area: "trench", areas: [{ outline: band([[x0, 0], [3, 0]], TR.HW), area: "trench" }], firingPoints: [], cover: [cover([1.6, -D, 0], 0, "high", ["over"])] },
    [{ outline: band([[x0 + CUT_SETBACK, 0], [3.8, 0]], TR.HW + TR.RV + TR.FILL + 0.1), depth: D + 0.25, bank: 0.2 }]);
  A.meta.trench = { path: [[x0, 0, 0], [0.6, 0, 1], [3, 0, 1]], branches: [trenchNiche([[0.4, 0], [3, 0]], [0, 0.5])], replace: TRENCH_REPLACE, floorDrop: TRENCH_FLOOR_DROP, stepDrop: TRENCH_STEP_DROP };   // GX1: the ramp shallows to grade at x0
  setTour(A, { pos: [-5.0, 0, 0], yaw: 270 }, [
    { goto: [-3.6, 0], note: "top of the ramp" }, { goto: [0.6, 0], expectY: -D, note: "down the ramp" }, { goto: [2.6, -0.1], expectY: -D },
    { look: [-3, 0.5, 0], hold: 0.5 }, { shot: "mpu_trench_entry" }, { goto: [0.6, 0] }, { goto: [-3.8, 0], expectY: 0, note: "back up the ramp" },
  ], { cut: true });
  return A;
}

// ============================================================================ dugouts and foxhole
// Personnel dugout (Mannschaftsunterstand): a timber-lined chamber 2.3 m down under a log roof and an earth mound,
// entered down a stepped trench from the rear through a plank door. Bunks, stove, table, cache.
export function dugoutPersonnel() {
  const A = new Asset({ id: "aw_dugout_personnel", version: 2, name: "Personnel dugout", category: "Atlantic Wall",
    description: "Timber-lined personnel dugout 2.3 m below ground: a 4.0 x 3.0 m chamber of log walls on posts under a double log roof and an earth mound, entered down a stepped trench and a plank door. Two-tier bunks for six, trench stove, table and bench, small-arms cache.",
    floors: 0, enterable: true, footprint: [6.4, 11.2], plinthDepth: 0.3, maxGrade: 0.6 });
  stdMaterials(A); mats(A, "spoil", "mud", "wattle", "picket");
  const GF = -2.3, CEIL = -0.25, X = 2.4, XI = 2.0, Z = 1.9, ZI = 1.5;
  A.box([-X, GF - 0.3, -Z], [X, GF - 0.02, Z], "mud");
  A.box([-XI, GF - 0.02, -ZI], [XI, GF, ZI], "plank_x");
  for (const s of [-1, 1]) A.box([s > 0 ? XI : -X, GF - 0.02, -Z], [s > 0 ? X : -XI, CEIL, Z], "log_z");
  A.box([-XI, GF - 0.02, ZI], [XI, CEIL, Z], "log_x");
  const dr = door(A, { id: "dugout_door", axis: "x", outer: -Z, inner: -ZI, c: 0, floor: GF, w: 0.9, h: 1.95, hingeSide: -1, leafMat: "plank_y", frameMat: "plank_y", thresholdMat: "plank_x", lintel: false, step: false });
  A.box([-XI, GF - 0.02, -Z], [dr.a0, CEIL, -ZI], "log_x"); A.box([dr.a1, GF - 0.02, -Z], [XI, CEIL, -ZI], "log_x");
  A.box([dr.a0, GF + 1.95, -Z], [dr.a1, CEIL, -ZI], "log_x");
  for (const x of [-XI, -0.1, XI - 0.2]) for (const z of [-ZI, ZI - 0.2]) A.box([x, GF, z], [x + 0.2, CEIL, z + 0.2], "log_z", { k: 0 });
  A.box([-XI, CEIL - 0.2, -0.1], [XI, CEIL, 0.1], "log_x", { k: 0 });
  for (let x = -X - 0.1; x < X + 0.1; x += 0.27) A.box([x, CEIL, -Z], [x + 0.26, CEIL + 0.26, Z + 0.1], "log_z");
  for (let z = -Z; z < Z; z += 0.27) A.box([-X - 0.1, CEIL + 0.26, z], [X + 0.1, CEIL + 0.52, z + 0.26], "log_x", { k: 0 });
  A.box([-X - 0.1, CEIL + 0.26, -Z], [X + 0.1, CEIL + 0.52, Z + 0.1], "log_x", { v: 0 });
  A.prism(0, -X - 0.8, X + 0.8, [[-Z - 0.1, -0.2], [Z + 1.6, -0.2], [0, 1.1]], "turf");   // earth mound (clear of the entrance)
  for (const s of [-1, 1]) A.prism(2, -Z - 1.0, Z + 1.0, [[s * (X + 0.6), -0.2], [s * (X + 0.6), 0.6], [s * (X + 2.4), -0.2]], "turf");
  // stepped entrance trench from the rear
  const rs = -Z - 0.7, ramp1 = rs - 4.6, ang = (Math.atan2(-GF, 4.6) * 180) / Math.PI, L = Math.hypot(GF, 4.6);
  A.box([-0.5, GF - 0.2, rs - 0.05], [0.5, GF, -Z], "mud");   // flat landing at the door
  A.obox([0, GF / 2 - 0.12, (rs + ramp1) / 2], [1.0, 0.24, L + 0.1], [ang, 0, 0], "mud", { v: 0 });
  A.obox([0, GF / 2 - 0.2, (rs + ramp1) / 2], [1.0, 0.2, L + 0.1], [ang, 0, 0], "mud", { k: 0 });
  for (let k = 0; k < 9; k++) { const zz = rs - 0.25 - k * 0.5, yy = GF * (zz - ramp1) / (rs - ramp1); A.box([-0.45, yy - 0.12, zz - 0.07], [0.45, yy + 0.04, zz + 0.07], "log_x", { k: 0 }); }
  for (const s of [-1, 1]) {
    A.box([s > 0 ? 0.5 : -0.6, GF - 0.2, ramp1], [s > 0 ? 0.6 : -0.5, 0.2, -Z], "wattle");
    A.box([s > 0 ? 0.6 : -2.95, GF - 0.2, ramp1], [s > 0 ? 2.95 : -0.6, 0.2, -Z], "spoil");   // spoil heaps over the cut's step
  }
  A.box([-2.95, -0.25, ramp1 - CUT_APRON], [2.95, 0.03, ramp1], "spoil");   // trampled apron at grade over the open end
  bunkZ(A, "Bunks", 1.2, 1.95, -1.45, 0.6, GF, 2);
  bunkZ(A, "Bunks", -1.95, -1.2, -0.4, 1.45, GF, 2);
  stove(A, "Trench stove", 1.55, 1.15, GF, CEIL + 1.2);
  dmat(A, "steel_dark");   // D1: rain cap on the flue
  A.cyl([1.55, CEIL + 1.22, 1.15], [0.15, 0.04, 0.15], "steel_dark", { into: d1(A), k: 0, seg: 12 });
  A.cone([1.55, CEIL + 1.32, 1.15], [0.3, 0.09, 0.3], "steel_dark", { into: d1(A), k: 0, top: 0.15, seg: 14 });
  table(A, "Table", 0.05, 0.9, 1.0, 0.6, GF, 0.72);
  stool(A, "Stool", -0.5, 0.25, GF, 0.45);
  smallArmsCache(A, "Small-arms ammunition", -1.45, GF, -1.15, 0, { boxes: 3, bandoliers: 2 });
  lamp(A, 0.0, CEIL - 0.2, -0.4);
  setNav(A, { effect: "block", rooms: [room("chamber", [-XI, GF, -ZI], [XI, CEIL, ZI])], doors: [{ a: "chamber", b: "outside", p: [0, GF, -Z + 0.2] }],
    entrances: [{ p: [0, 0, ramp1 - 0.3], dir: [0, 0, 1], room: "chamber" }], corners: corners("chamber", -XI, XI, -ZI, ZI, GF, 0.5),
    firingPoints: [], cover: [cover([0, GF * (-4.4 - ramp1) / (rs - ramp1), -4.4], 90, "low", ["over"]), cover([0, GF * (-4.4 - ramp1) / (rs - ramp1), -4.4], 270, "low", ["over"])],
    areas: [{ outline: rect(-0.5, ramp1, 0.5, -Z), area: "trench" }] },
    [{ outline: rect(-X + CUT_PAD, -Z + CUT_PAD, X - CUT_PAD, Z - CUT_PAD), depth: -GF + 0.4, bank: 0.15 }, { outline: rect(-1.25, ramp1 + CUT_SETBACK, 1.25, -Z + 0.6), depth: -GF + 0.3, bank: 0.2 }]);
  setTour(A, { pos: [0, 0, ramp1 - 2.0], yaw: 180 }, [
    { goto: [0, ramp1 - 0.2] }, { goto: [0, -Z - 0.6], note: "down the stepped entrance" },
    { interact: "dugout_door", aim: [0.2, GF + 1.1, -ZI - 0.05], expectOpen: true },
    { goto: [0.05, -0.5], expectY: GF, note: "inside the dugout" },
    { look: [1.5, GF + 0.9, 0.2], hold: 0.6 }, { shot: "mpu_dugout_personnel" },
    { goto: [0, -Z - 0.6] }, { goto: [0, ramp1 - 0.4], expectY: 0, note: "back out" },
  ], { cut: true });
  return A;
}
// Log-roofed dugout (Mont-Castre, dug into quarry spoil): half sunk, 1.0 m down, log walls rising 0.9 m above ground
// with a firing slit, log roof and earth, entered by a short ramp at the side.
export function dugoutLog() {
  const A = new Asset({ id: "aw_dugout_log", version: 1, name: "Log-roofed dugout", category: "Atlantic Wall",
    description: "Half-sunk log dugout as dug into the Mont-Castre quarry spoil: floor 1.0 m down, log walls rising 0.9 m above ground with a firing slit at the front, a log roof under turf, entered down a short ramp at the side. Bench, shelf, small-arms cache.",
    floors: 0, enterable: true, footprint: [7.6, 6.8], plinthDepth: 0.3, maxGrade: 0.6 });
  stdMaterials(A); mats(A, "spoil", "mud", "wattle");
  const GF = -1.0, CEIL = 0.95, X = 2.0, XI = 1.65, Z = 1.6, ZI = 1.25;
  A.box([-X, GF - 0.3, -Z], [X, GF - 0.02, Z], "mud"); A.box([-XI, GF - 0.02, -ZI], [XI, GF, ZI], "mud");
  const slitH = { a0: -0.6, a1: 0.6, y0: GF + SLIT.sill, y1: GF + SLIT.top };
  const dr = { a0: -ZI + 0.25, a1: -ZI + 1.15, y0: -10, y1: GF + 1.85 };
  const lw = (axis, outer, inner, aOut, aIn, holes, m, side) => wall(A, { axis, outer, inner, aOut, aIn, y0: GF - 0.02, y1: CEIL, holes, stone: m, plaster: m, pt: 0.02, panel: { side, family: "timber", bands: [[GF - 0.02, CEIL, 0, GF]], collar: 0.25 } });
  lw("x", Z, ZI, [-X, X], [-XI, XI], [slitH], "log_x", "front");
  lw("x", -Z, -ZI, [-X, X], [-XI, XI], [], "log_x", "rear");
  lw("z", X, XI, [-ZI - 0.02, ZI + 0.02], [-ZI, ZI], [dr], "log_z", "east");
  lw("z", -X, -XI, [-ZI - 0.02, ZI + 0.02], [-ZI, ZI], [], "log_z", "west");
  for (let x = -X - 0.15; x < X + 0.15; x += 0.27) A.box([x, CEIL, -Z - 0.15], [x + 0.26, CEIL + 0.26, Z + 0.15], "log_z");
  A.prism(0, -X - 0.2, X + 0.2, [[-Z - 0.5, CEIL + 0.26], [Z + 0.5, CEIL + 0.26], [0, CEIL + 0.75]], "turf");
  A.prism(0, -X, X, [[Z, -0.3], [Z, 0.35], [Z + 1.6, -0.3]], "turf");      // earth banked against the front below the slit
  A.prism(0, -X, X, [[-Z, -0.3], [-Z, CEIL], [-Z - 1.6, -0.3]], "turf");
  A.prism(2, -Z, Z, [[-X, -0.3], [-X, CEIL], [-X - 1.6, -0.3]], "turf");
  // side ramp to the east doorway
  const ang = (Math.atan2(-GF, 2.4) * 180) / Math.PI, L = Math.hypot(GF, 2.4), zc = (dr.a0 + dr.a1) / 2, RX = X + 0.6;
  A.box([X, GF - 0.2, zc - 0.5], [RX + 0.05, GF, zc + 0.5], "mud");   // flat landing at the door
  A.obox([RX + 1.2, GF / 2 - 0.12, zc], [L + 0.1, 0.24, 1.0], [0, 0, ang], "mud", { v: 0 });
  A.obox([RX + 1.2, GF / 2 - 0.2, zc], [L + 0.1, 0.2, 1.0], [0, 0, ang], "mud", { k: 0 });
  for (const s of [-1, 1]) A.box([X, GF - 0.2, zc + s * 0.55 - 0.05], [RX + 2.4, 0.15, zc + s * 0.55 + 0.05], "wattle");
  // spoil heaps beside the ramp along the east wall and an apron at grade over the open end: they cover the cut's step
  A.box([X, GF - 0.2, zc + 0.6], [RX + 2.4, 0.15, Z + CUT_RING], "spoil"); A.box([X, GF - 0.2, -Z - CUT_RING], [RX + 2.4, 0.15, zc - 0.6], "spoil");
  A.box([RX + 2.4, -0.25, zc - 1.6], [RX + 2.4 + CUT_APRON, 0.03, zc + 1.6], "spoil");
  A.beginFurniture("Bench"); A.box([-XI, GF + 0.42, -0.4], [-1.25, GF + 0.47, 1.0], "plank_z"); for (const z of [-0.3, 0.9]) A.box([-1.6, GF, z - 0.04], [-1.3, GF + 0.42, z + 0.04], "plank_y"); A.endFurniture();
  smallArmsCache(A, "Small-arms ammunition", -0.6, GF, -0.95, 0, { boxes: 3, bandoliers: 2 });
  setNav(A, { effect: "block", rooms: [room("chamber", [-XI, GF, -ZI], [XI, CEIL, ZI])], doors: [{ a: "chamber", b: "outside", p: [X - 0.2, GF, zc] }],
    entrances: [{ p: [X + 3.3, 0, zc], dir: [-1, 0, 0], room: "chamber" }], corners: corners("chamber", -XI, XI, -ZI, ZI, GF, 0.4),
    firingPoints: [fire([0, GF, ZI - 0.35], 0, "embrasure")], cover: [cover([0, 0, -Z - 1.8], 0, "low", ["left", "right"]), cover([-X - 1.4, 0, 0], 90, "low", ["left", "right"])],
    areas: [{ outline: rect(X, zc - 0.5, X + 3.0, zc + 0.5), area: "trench" }] },
    [{ outline: rect(-X + CUT_PAD, -Z + CUT_PAD, X - CUT_PAD, Z - CUT_PAD), depth: -GF + 0.35, bank: 0.15 }, { outline: rect(X - 0.6, zc - 0.65, RX + 2.4 - CUT_SETBACK, zc + 0.65), depth: -GF + 0.3, bank: 0.2 }]);
  setTour(A, { pos: [X + 4.8, 0, zc], yaw: 90 }, [
    { goto: [X + 3.2, zc] }, { goto: [X - 0.4, zc], expectY: GF, note: "down the side ramp" }, { goto: [0, 0.6], expectY: GF, note: "at the slit" },
    { look: [0, GF + SLIT.eye, 10], hold: 0.6 }, { shot: "mpu_dugout_log_slit" }, { goto: [X - 0.4, zc] }, { goto: [X + 3.5, zc], expectY: 0, note: "back out" },
  ], { cut: true });
  return A;
}
// Two-man foxhole: 1.9 x 0.8 m, 1.3 m deep, spoil parapet to the front and flanks, a few sandbags, small cache.
export function foxhole() {
  const A = new Asset({ id: "aw_foxhole", version: 2, name: "Foxhole (two-man)", category: "Atlantic Wall",
    description: "Two-man foxhole 1.9 x 0.8 m and 1.2 m deep: the spoil thrown up as a parapet to the front and flanks, a few sandbags, a small ammunition cache at one end. Standing in it the eye clears the parapet; the rear lip is low enough to climb out.",
    floors: 0, enterable: false, footprint: [4.4, 4.0], plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true });
  stdMaterials(A); mats(A, "spoil", "mud");
  const D = 1.2, X = 0.95, Z = 0.4;
  A.box([-X, -D - 0.2, -Z], [X, -D, Z], "mud");
  for (const s of [-1, 1]) { A.box([-X - 0.15, -D - 0.2, s > 0 ? Z : -Z - 0.15], [X + 0.15, 0.0, s > 0 ? Z + 0.15 : -Z], "spoil"); A.box([s > 0 ? X : -X - 0.15, -D - 0.2, -Z], [s > 0 ? X + 0.15 : -X, 0.0, Z], "spoil"); }
  A.prism(0, -X - 0.6, X + 0.6, [[Z + 0.15, -0.2], [Z + 0.15, 0.3], [Z + 1.6, -0.2]], "spoil");
  for (const s of [-1, 1]) A.prism(2, -Z - 0.2, Z + 1.2, [[s * (X + 0.15), -0.2], [s * (X + 0.15), 0.2], [s * (X + 1.1), -0.2]], "spoil");
  for (const [x, z, r] of [[-1.45, 0.1, 85], [1.45, -0.1, 95], [1.45, 0.5, 80]]) A.obox([x, 0.3, z], [0.58, 0.18, 0.32], [0, r, 0], "sacking", { v: 0 });   // flank sandbags (collider)
  { const T0 = A.parts.length;   // D1: soft sandbags over the colliders
    for (const [x, z, r] of [[-1.45, 0.1, 85], [1.45, -0.1, 95], [1.45, 0.5, 80]]) sandbag(A, [x, 0.3, z], r + 90, { L: 0.64, W: 0.34, H: 0.19 });
    d1(A).push(...A.parts.splice(T0)); }
  A.box([-X - 1.0, -0.25, -Z - 1.05], [X + 1.0, 0.03, -Z - 0.15], "spoil");   // trampled rear lip at grade over the cut's step
  smallArmsCache(A, "Small-arms ammunition", -0.62, -D, 0.0, 90, { boxes: 1, crate: false, bandoliers: 2 });
  setNav(A, { effect: "area", area: "trench", areas: [{ outline: rect(-X, -Z, X, Z), area: "trench" }],
    firingPoints: [fire([0.2, -D, 0.1], 0, "firestep"), fire([0.6, -D, 0.1], 0, "firestep")], cover: [cover([0.3, -D, 0], 0, "high", ["over"])] },
    [{ outline: rect(-X, -Z, X, Z), depth: D + 0.25, bank: 0.15 }]);
  setTour(A, { pos: [0.3, 0, -2.4], yaw: 180 }, [
    { goto: [0.3, -0.9], note: "to the rear lip" }, { goto: [0.35, 0.05] }, { wait: 1.0 }, { goto: [0.35, 0.05], expectY: -D, note: "dropped into the foxhole" },
    { look: [0.3, -D + SLIT.eye, 8], hold: 0.6 }, { shot: "mpu_foxhole_view" },
  ], { cut: true });
  return A;
}

export function atlanticAssets() {
  return [r667(), r612Unfinished(), tobrukTurret(), tobrukMg(), tobrukMortar(), trenchStraight(), trenchZigzag(), trenchCorner(), trenchEntry(), dugoutPersonnel(), dugoutLog(), foxhole()].map(flushD1);
}
