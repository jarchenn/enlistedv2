// GX4 exterior pass (docs/exterior-references.md): run last on a finished building (after dress() / weather()), it
// rebuilds the exterior faces toward the CC-BY GLB design references without touching ids, interiors, panels, nav or
// colliders:
//  - roof courses: every pitched roof slab (gable slabs, cross gables, dormer roofs) and every hip roof gets real
//    tile / slate courses, thin strips laid like the tiles themselves (each tilted so its lower edge stands proud of the
//    course below), their length phased to the L4 texture rows so a texture joint falls on every geometric edge;
//  - hip rolls along the hip lines, segmented ridge tiles;
//  - timber framing: deeper, chamfered members with adzed width variation;
//  - quoins: rusticated, each stone at its own projection with deeper joints (the brief's "height variation");
//  - rain-streak decal anchors under every sill nose (meta.decals, for GX5's static decal system).
// Detail-only parts carry "lod": 1 (k: 0, never in a bay): a far LOD may drop them and keep a correct silhouette
// (the slab under each course stays). The flag is ignored by the current loader.
import { extOf, fbox } from "./parts.mjs";
import { cutRect } from "./kit.mjs";

const q4 = (v) => Math.round(v * 1e4) / 1e4;
// L4 texture rows (content/textures/textures.json slots; tools/exterior.mjs measured the joint phase from the image
// top as a fraction of a row): row = set size / scale / rows per tile
export const ROOF_TEX = {
  tile: { row: 2.9 / 11, phase: 0.586, th: 0.022, lift: 0.024 },    // roof_tile slot: roof_clay_old (RoofingTiles014B), 11 rows
  slate: { row: 2.0 / 0.6 / 16, phase: 0.477, th: 0.014, lift: 0.013 },   // slate slot: slate_square (RoofingTiles003) at scale 0.6, 16 rows
};
const ROOF_MATS = { slate: "slate", tile: "tile" };

// ---------------------------------------------------------------- quaternions (glm order: x, y, z, w; quat(euler) = qz qy qx)
function qEuler(r) {
  const [x, y, z] = r.map((d) => (d * Math.PI) / 360), cx = Math.cos(x), sx = Math.sin(x), cy = Math.cos(y), sy = Math.sin(y), cz = Math.cos(z), sz = Math.sin(z);
  return [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];
}
const qMul = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
function qRot(q, v) {
  const [x, y, z, w] = q, t = [2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])];
  return [v[0] + w * t[0] + y * t[2] - z * t[1], v[1] + w * t[1] + z * t[0] - x * t[2], v[2] + w * t[2] + x * t[1] - y * t[0]];
}
const qAxis = (ax, deg) => { const h = (deg * Math.PI) / 360, s = Math.sin(h); return [ax[0] * s, ax[1] * s, ax[2] * s, Math.cos(h)]; };
// rotation whose columns are the orthonormal ex, ey, ez (made right-handed by flipping ex)
function qBasis(ex, ey, ez) {
  if (dot(cross(ex, ey), ez) < 0) ex = ex.map((v) => -v);
  const m00 = ex[0], m11 = ey[1], m22 = ez[2], tr = m00 + m11 + m22;
  let q;
  if (tr > 0) { const s = Math.sqrt(tr + 1) * 2; q = [(ey[2] - ez[1]) / s, (ez[0] - ex[2]) / s, (ex[1] - ey[0]) / s, 0.25 * s]; }
  else if (m00 > m11 && m00 > m22) { const s = Math.sqrt(1 + m00 - m11 - m22) * 2; q = [0.25 * s, (ey[0] + ex[1]) / s, (ez[0] + ex[2]) / s, (ey[2] - ez[1]) / s]; }
  else if (m11 > m22) { const s = Math.sqrt(1 + m11 - m00 - m22) * 2; q = [(ey[0] + ex[1]) / s, 0.25 * s, (ez[1] + ey[2]) / s, (ez[0] - ex[2]) / s]; }
  else { const s = Math.sqrt(1 + m22 - m00 - m11) * 2; q = [(ez[0] + ex[2]) / s, (ez[1] + ey[2]) / s, 0.25 * s, (ex[1] - ey[0]) / s]; }
  const n = Math.hypot(...q);
  return q.map((v) => v / n);
}
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add = (a, b, k = 1) => [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k];
const unit = (a) => { const n = Math.hypot(...a); return a.map((v) => v / n); };

function seeded(str) {
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
  return () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; };
}

// ---------------------------------------------------------------- roof courses
// One sloped face: eave = the midpoint of the eave edge on the top surface, dn = downhill (unit, in the face), n = the
// face's up normal, ac = across (unit), Ls = slope length eave -> top, half(u) = the half width of the face u metres up
// from the eave (constant on a gable slab, shrinking on hips). Each course is a strip whose visible part is one texture
// row; strips run on up under the next (the texture-phased length), the top one stops at the face's top edge.
function courses(A, { eave, dn, n, ac, Ls, half, m }) {
  const T = ROOF_TEX[ROOF_MATS[m]], e = T.row, H = (T.phase + 1) * e, L = 2 * H, up = dn.map((v) => -v);
  const q0 = qBasis(ac, n, up), dl = Math.atan(T.lift / L) * 180 / Math.PI, q = qMul(q0, qAxis([1, 0, 0], dl));
  const out = [];
  for (let k = 0, u = 0; u < Ls - 0.05; k++, u += e) {
    const len = Math.min(L, Ls - u), w = 2 * half(Math.min(u + len, Ls)) - 0.012;
    if (w < 0.15 || len < 0.06) continue;
    // a strip shortened at the top keeps its lower edge on the texture joint: shift the centre, not the phase
    const c = add(add(eave, up, u + len / 2), n, T.th / 2 + T.lift / 2 - 0.004);
    out.push({ t: "b", c: c.map(q4), s: [w, T.th, len].map(q4), q: q.map((v) => Math.round(v * 1e6) / 1e6), m, k: 0, lod: 1 });
  }
  return out;
}
// pitched roof slabs: boxes of a roof material whose thinnest axis points up at 15-75 degrees
function slabCourses(A, p) {
  if (p.t !== "b" || !ROOF_MATS[p.m] || !(p.r || p.q) || p.lod) return [];
  const q = p.q ? p.q : qEuler(p.r), ax = [[1, 0, 0], [0, 1, 0], [0, 0, 1]].map((v) => qRot(q, v));
  const ti = p.s.indexOf(Math.min(...p.s));
  let n = ax[ti]; if (n[1] < 0) n = n.map((v) => -v);
  if (n[1] < 0.26 || n[1] > 0.966) return [];
  const [i1, i2] = [0, 1, 2].filter((i) => i !== ti), si = Math.abs(ax[i1][1]) > Math.abs(ax[i2][1]) ? i1 : i2, ci = si === i1 ? i2 : i1;
  let dn = ax[si]; if (dn[1] > 0) dn = dn.map((v) => -v);
  const Ls = p.s[si], W = p.s[ci];
  if (Ls < 0.6 || W < 0.4) return [];
  const top = add(p.c, n, p.s[ti] / 2), eave = add(top, dn, Ls / 2);
  return courses(A, { eave, dn: unit(dn), n: unit(n), ac: unit(ax[ci]), Ls, half: () => W / 2, m: p.m });
}
// hip roof (parts.hipRoof, centred on the origin): the two long trapezoids and the two hip triangles, plus hip rolls
function hipCourses(A, { X, Z, EAVE, RIDGE, ov, m }) {
  if (!ROOF_MATS[m]) return [];
  const alongX = X >= Z, Lh = alongX ? X : Z, S = alongX ? Z : X, pitch = (RIDGE - EAVE) / S, Lo = Lh + ov, So = S + ov;
  const ye = EAVE - ov * pitch, run = Lo - So, th = Math.atan(pitch), cs = Math.cos(th), sn = Math.sin(th), Ls = So / cs;
  const W = (l, y, s) => (alongX ? [l, y, s] : [s, y, l]);
  const out = [];
  for (const sg of [-1, 1]) {
    // long face, outward sg along the short axis
    out.push(...courses(A, { eave: W(0, ye, sg * So), dn: unit(W(0, -sn, sg * cs)), n: unit(W(0, cs, sg * sn)), ac: unit(W(1, 0, 0)), Ls, half: (u) => Math.max(run, Lo - u * cs) - 0.015, m }));
    // hip end, outward sg along the long axis (a triangle: the apex at the ridge end)
    out.push(...courses(A, { eave: W(sg * Lo, ye, 0), dn: unit(W(sg * cs, -sn, 0)), n: unit(W(sg * sn, cs, 0)), ac: unit(W(0, 0, 1)), Ls, half: (u) => So - u * cs - 0.015, m }));
  }
  // hip rolls: half-round ridge tiles down each hip line, from the ridge ends to the eave corners
  for (const sl of [-1, 1]) for (const ss of [-1, 1]) {
    const a = W(sl * Math.max(run, 0), RIDGE + 0.05, 0), b = W(sl * Lo, ye + 0.05, ss * So);
    const d = add(b, a, -1), len = Math.hypot(...d), ey = unit(d), ex = unit(cross(ey, [0, 1, 0]));
    out.push({ t: "c", c: add(a, ey, len / 2).map(q4), s: [0.17, len, 0.17].map(q4), q: qBasis(ex, ey, cross(ex, ey)).map((v) => Math.round(v * 1e6) / 1e6), m: "ridge_tile", k: 0, lod: 1 });
  }
  return out;
}

// ---------------------------------------------------------------- timber, quoins
function timber(A, R) {
  for (const { F, proj, parts } of extOf(A).timber) {
    const di = F.axis === "x" ? 2 : 0;
    for (const p of parts) {
      if (p.t !== "b" || !/^oak_/.test(p.m)) continue;
      const want = 0.03 + 0.012 * R(), dp = Math.max(0, want - proj);   // deeper members (>= 3 cm proud, under the shutters' 3.5 cm)
      p.s[di] = q4(p.s[di] + dp); p.c[di] = q4(p.c[di] + (F.out * dp) / 2);
      const wi = [0, 1, 2].filter((i) => i !== di).sort((a, b) => p.s[a] - p.s[b])[0];   // adzed width variation
      if (p.s[wi] < 0.4) p.s[wi] = q4(p.s[wi] + (R() - 0.5) * 0.022);
      p.bv = q4(Math.min(0.016, p.s[di] / 2 - 0.004, p.s[wi] / 2 - 0.004));
    }
  }
}
// quoins (parts.quoins: 0.335 x 0.565 plan, 0.29 high, stone_dressed): each stone stands out its own amount
function quoins(A, R) {
  for (const p of A.parts) {
    if (p.t !== "b" || p.m !== "stone_dressed" || p.r || p.q) continue;
    const hs = [p.s[0], p.s[2]].sort((a, b) => a - b);
    if (!(Math.abs(hs[0] - 0.335) < 0.003 && Math.abs(hs[1] - 0.565) < 0.003 && p.s[1] < 0.296)) continue;
    const e = 0.008 + 0.03 * R(), sx = Math.sign(p.c[0]) || 1, sz = Math.sign(p.c[2]) || 1;
    p.s[0] = q4(p.s[0] + e); p.c[0] = q4(p.c[0] + (sx * e) / 2);
    p.s[2] = q4(p.s[2] + e); p.c[2] = q4(p.c[2] + (sz * e) / 2);
    const dh = 0.01 + 0.02 * R();   // deeper bed joints, the stone's own height
    p.s[1] = q4(p.s[1] - dh); p.c[1] = q4(p.c[1] - dh / 2 + 0.004);
    p.bv = q4(0.014 + 0.012 * R());
  }
}

// ---------------------------------------------------------------- ridges, dentils, crow steps
// ridge tiles: the ridge bar / roll gets a run of tapered half-round tiles, each lapping the next (lower end flared)
function ridgeTiles(A) {
  const out = [];
  for (const p of A.parts) {
    if (p.m !== "ridge_tile" || p.lod || !p.r) continue;
    let ax, L, D;
    if (p.t === "b" && Math.abs(Math.abs(p.r[0]) - 45) < 0.1 && !p.r[1] && !p.r[2]) { ax = 0; L = p.s[0]; D = 0.29; }
    else if (p.t === "c" && Math.abs(Math.abs(p.r[0]) - 90) < 0.1) { ax = 2; L = p.s[1]; D = p.s[0] + 0.05; }
    else if (p.t === "c" && Math.abs(Math.abs(p.r[2]) - 90) < 0.1) { ax = 0; L = p.s[1]; D = p.s[0] + 0.05; }
    else continue;
    if (L < 1.2) continue;
    const n = Math.max(2, Math.round(L / 0.4)), sl = L / n, yc = p.t === "b" ? p.c[1] + 0.03 : p.c[1];
    for (let i = 0; i < n; i++) {
      const c = [...p.c]; c[ax] = q4(p.c[ax] - L / 2 + sl * (i + 0.5)); c[1] = q4(yc);
      out.push({ t: "k", c, s: [D, sl + 0.03, D].map(q4), m: "ridge_tile", top: 0.86, seg: 10, r: ax === 0 ? [0, 0, 90] : [90, 0, 0], k: 0, lod: 1 });
    }
  }
  A.parts.push(...out);
}
// dentil course under each cornice's top band (P1 town houses, P1-6 / P1-7: a toothed bed mould under the corona)
function dentils(A) {
  for (const { F, a0, a1, y, h, proj, steps, m } of extOf(A).cornices) {
    if (steps < 2) continue;
    const yb = y + (h * (steps - 2)) / steps, yt = y + (h * (steps - 1)) / steps, pe = (proj * (steps - 1)) / steps + 0.035;
    const ext = (proj * (steps - 1)) / steps;
    for (let a = a0 - ext + 0.08; a < a1 + ext - 0.06; a += 0.17)
      { const d = fbox(A, F, a, a + 0.085, yb + 0.012, yt, -0.01, pe, m, { k: 0, bv: 0.006 }); if (d) d.lod = 1; }
  }
}
// Crow-stepped gables (P1-0, the stepped-gable hall; P1-2): the gable walls rise in coped steps above the roof, the roof
// stops inside them (no verge overhang, no barge boards). Visual only (k: 0): the gable prism and roof stay the colliders.
function crowSteps(A, g) {
  const { x0, x1, xi, Z, EAVE, RIDGE, gov, t, gableMat } = g, th = Math.atan2(RIDGE - EAVE, Z), tn = Math.tan(th), xc = (x0 + x1) / 2, X = (x1 - x0) / 2;
  const yb = (z) => RIDGE - Math.abs(z) * tn, yt = (z) => yb(z) + t / Math.cos(th);
  const keep = [];
  for (const p of A.parts) {
    const near = (v, w) => Math.abs(v - w) < 0.03;
    if (p.t === "b" && p.m === "oak_x" && p.r && near(p.s[0], 0.04) && near(Math.abs(p.c[0] - xc), X + gov - 0.02)) continue;   // barge boards
    if (p.t === "b" && p.r && near(p.c[0], xc) && near(p.s[0], 2 * X + 2 * gov)) p.s[0] = q4(2 * X - 0.06);   // roof slabs
    else if (p.t === "b" && p.r && p.m === "ridge_tile" && near(p.s[0], 2 * X + 2 * gov + 0.04)) p.s[0] = q4(2 * X - 0.1);
    else if (p.t === "b" && !p.r && (p.m === "oak_x" || p.m === "zinc") && near(p.s[0], 2 * X + 2 * gov) && p.c[1] < EAVE + 0.3) p.s[0] = q4(2 * X);   // fascia, gutter
    keep.push(p);
  }
  A.parts.length = 0; A.parts.push(...keep);
  const n = Math.max(3, Math.round(Z / 0.75)), w = Z / n, wt = xi !== null ? x1 - xi : 0.6;
  for (const sx of [-1, 1]) {
    const xa = sx > 0 ? x1 - wt : x0, xb = sx > 0 ? x1 : x0 + wt;
    const step = (z0, z1, top, low) => {
      A.box([xa, low, z0], [xb, top, z1], gableMat, { k: 0 });
      A.box([xa - 0.035, top - 0.005, z0 - 0.035], [xb + 0.035, top + 0.07, z1 + 0.035], "stone_dressed", { k: 0, bv: 0.012 });
    };
    for (let k = 0; k < n - 1; k++) for (const sz of [-1, 1]) {
      const zo = Z - k * w, zin = Z - (k + 1) * w;
      step(Math.min(sz * zo, sz * zin), Math.max(sz * zo, sz * zin), yt(zin) + 0.32, yb(zo) - 0.03);
    }
    step(-w, w, yt(0) + 0.5, yb(w) - 0.03);
  }
}

// ---------------------------------------------------------------- facade zoning on the body box (extOf(A).body)
const FACE = (X, Z) => [{ axis: "x", d: Z, out: 1, a0: -X, a1: X }, { axis: "x", d: -Z, out: -1, a0: -X, a1: X }, { axis: "z", d: X, out: 1, a0: -Z, a1: Z }, { axis: "z", d: -X, out: -1, a0: -Z, a1: Z }];
// door / window apertures on a face, padded (clear of the surrounds)
function holesOn(A, F, pad) {
  const n = F.axis === "x" ? 2 : 0, u = 2 - n, out = [];
  for (const it of A.interactables) {
    if (!it.aperture || (it.kind !== "door" && it.kind !== "window")) continue;
    const { min, max } = it.aperture, cd = (min[n] + max[n]) / 2;
    if (max[n] - min[n] > 0.6 || Math.sign(cd) !== F.out || Math.abs(Math.abs(cd) - Math.abs(F.d)) > 0.8) continue;
    out.push({ a0: min[u] - pad, a1: max[u] + pad, y0: min[1] - pad, y1: max[1] + pad });
  }
  return out;
}
// the wall face material at height y (the box whose outer face lies on the face plane)
function faceMat(A, F, y) {
  const n = F.axis === "x" ? 2 : 0, cnt = {};
  for (const p of [...A.parts, ...A.panels.flatMap((pn) => pn.states[0] || [])]) {   // the wall bays live in the panels
    if (p.t !== "b" || p.r || p.q || p.v === 0 || p.k === 0) continue;
    if (Math.abs(p.c[n] + (F.out * p.s[n]) / 2 - F.d) > 0.02 || p.c[1] - p.s[1] / 2 > y || p.c[1] + p.s[1] / 2 < y) continue;
    cnt[p.m] = (cnt[p.m] || 0) + p.s[2 - n] * p.s[1];
  }
  return Object.entries(cnt).sort((a, b) => b[1] - a[1])[0]?.[0];
}
// A facade strip a0..a1 x y0..y1 standing e proud, cut round the holes. Pieces over a destructible wall bay go into that
// bay's intact state (they fall with the render when it is damaged or breached); the rest stay static.
function bayRanges(A, F) {
  const n = F.axis === "x" ? 2 : 0, u = 2 - n;
  return A.panels.filter((pn) => pn.states?.length && Math.abs((pn.min[n] + pn.max[n]) / 2 - F.d) < 0.8 && Math.sign(pn.normal?.[n] ?? 0) === F.out)
    .map((pn) => ({ a0: pn.min[u], a1: pn.max[u], y0: pn.min[1], y1: pn.max[1], st: pn.states[0] }));
}
const strip = (A, F, a0, a1, y0, y1, e, m, holes, o = {}) => {
  const bays = bayRanges(A, F);
  for (const [p0, p1, q0, q1] of cutRect(a0, a1, y0, y1, holes)) {
    if (p1 - p0 < 0.12 || q1 - q0 < 0.02) continue;
    const cuts = [...new Set([p0, p1, ...bays.flatMap((b) => [b.a0, b.a1]).filter((v) => v > p0 + 1e-4 && v < p1 - 1e-4)])].sort((x, y) => x - y);
    for (let i = 0; i + 1 < cuts.length; i++) {
      const c0 = cuts[i], c1 = cuts[i + 1];
      if (c1 - c0 < 0.04) continue;
      const bay = bays.find((b) => b.a0 <= c0 + 1e-4 && b.a1 >= c1 - 1e-4 && b.y0 <= q0 + 1e-4 && b.y1 >= q1 - 1e-4);
      const p = fbox(A, F, c0, c1, q0, q1, -0.01, e, m, { k: 0, bv: bay ? 0 : o.bv ?? 0.006, into: bay ? bay.st : undefined });
      if (p && o.lod !== 0 && !bay) p.lod = 1;
    }
  }
};
// refends (P1-6 / P1-7 ground floors): the ground storey in channelled courses, horizontal joints 2.5 cm, 2.2 cm proud
function rusticate(A, b) {
  for (const F of FACE(b.X, b.Z)) {
    const m = faceMat(A, F, b.GF + 1.2), holes = holesOn(A, F, 0.13), top = b.GF + b.h - 0.32;
    if (!m || m === "glass") continue;
    for (let y = b.GF + 0.02; y < top - 0.15; y += 0.36) strip(A, F, F.a0 + 0.6, F.a1 - 0.6, y, Math.min(y + 0.335, top), 0.022, m, holes, { bv: 0.009 });
  }
}
// stone bands on brick (P1-0 speklagen, P3 villa): a band at each storey's sill line and over the window heads
function stoneBands(A, b, storeys) {
  for (const F of FACE(b.X, b.Z)) for (const k of storeys) {
    const y0 = b.GF + k * b.h, m = faceMat(A, F, y0 + 1.5);
    if (m !== "brick") continue;
    const holes = holesOn(A, F, 0.15);
    for (const y of [y0 + 0.72, y0 + 2.42]) strip(A, F, F.a0 + 0.58, F.a1 - 0.58, y, y + 0.14, 0.02, "stone_dressed", holes, { bv: 0.008, lod: 0 });
  }
}
// tarred / painted soubassement on rendered fronts (P1-3: the dark painted plinth band)
function paintedPlinth(A, b) {
  if (!A.materials.some((m) => m.name === "plinth_paint")) A.mat("plinth_paint", [0.045, 0.055, 0.05], 0.58, "painted", { grain: 1 });
  for (const F of FACE(b.X, b.Z)) {
    const m = faceMat(A, F, b.GF + 0.5);
    if (m !== "render" && m !== "render_cream" && m !== "plaster") continue;
    const holes = holesOn(A, F, 0.02).filter((h) => h.y0 < b.GF + 0.4);
    strip(A, F, F.a0 + 0.02, F.a1 - 0.02, b.GF - 0.02, b.GF + 0.62, 0.012, "plinth_paint", holes, { bv: 0.004, lod: 0 });
  }
}

// per building: the reference-driven features (docs/exterior-references.md)
const FEATURES = {
  large_village_schoolhouse: { crow: true, dentils: true, bands: [0, 1, 2] },   // P1-0 stepped-gable brick hall
  large_municipal_building: { dentils: true, rustic: true },                     // P1-6 neoclassical town house
  large_village_inn: { rustic: true },                                           // P1-7 / Little Rock Inn: heavy base under the timbering
  vil_rectory: { dentils: true, rustic: true, plinth: true },                    // P1-6 / P1-3
  vil_terrace_pair: { dentils: true, plinth: true, bands: [1, 2] },             // P1-4: a rendered and a brick town house side by side
  vil_corner_shop: { dentils: true, bands: [1] },                               // P1-7 / P1-3 corner shop
  vil_villa_deauville: { dentils: true, bands: [0] },                           // P3 brick villa
  small_roadside_office: { plinth: true },
  // GX6 (tools/catalog_gx_buildings.mjs): they build their own reference trims; the generic pass only, and the
  // concrete stronghold keeps its shuttering (no courses, no quoin relief)
  mil_stronghold: { roof: false, quoins: false },
};
// a recorded hip roof is still where hipRoof() put it (not turned afterwards): its middle prism is in the part list
const hipInPlace = (A, h) => {
  const alongX = h.X >= h.Z, run = (alongX ? h.X : h.Z) - (alongX ? h.Z : h.X);
  return run <= 0.01 || A.parts.some((p) => p.t === "p" && p.axis === (alongX ? 0 : 2) && Math.abs(p.a[0] + run) < 0.01 && Math.abs(p.tri[2][1] - h.RIDGE) < 0.01);
};
// a recorded sill nose still has a dressed-stone part there (turned wings move their parts, not the record)
const sillInPlace = (A, pt) => A.parts.some((p) => p.t === "b" && p.m === "stone_dressed" && [0, 1, 2].every((i) => Math.abs(pt[i] - p.c[i]) <= p.s[i] / 2 + 0.06));

// ---------------------------------------------------------------- the pass
export function exterior(A, opt = {}) {
  const R = seeded(A.meta.id + ":gx4"), x = extOf(A), f = FEATURES[A.meta.id] || {};
  if (f.crow) for (const g of x.gables) crowSteps(A, g);
  if (f.dentils) dentils(A);
  const b = x.body;
  if (b && f.rustic) rusticate(A, b);
  if (b && f.bands) stoneBands(A, b, f.bands);
  if (b && f.plinth) paintedPlinth(A, b);
  ridgeTiles(A);
  const roof = [];
  if (f.roof !== false) {
    for (const p of A.parts) roof.push(...slabCourses(A, p));
    for (const h of x.hips) if (hipInPlace(A, h)) roof.push(...hipCourses(A, h));
  }
  A.parts.push(...roof);
  timber(A, R);
  if (f.quoins !== false && opt.quoins !== false) quoins(A, R);
  // rain-streak anchors (GX5 decals): under each sill nose, on the wall face, streak length by storey height
  A.meta.decals = x.sills.flatMap((s) => {
    const n = s.axis === "x" ? [0, 0, s.out] : [s.out, 0, 0], ac = (s.a0 + s.a1) / 2;
    const pos = s.axis === "x" ? [ac, s.y, s.d - s.out * 0.06 + s.out * 0.002] : [s.d - s.out * 0.06 + s.out * 0.002, s.y, ac];
    const h = q4(Math.min(1.4, 0.5 + 0.6 * R()));
    return sillInPlace(A, pos) ? [{ kind: "rain_streak", pos: pos.map(q4), n, w: q4(s.a1 - s.a0 + 0.1), h }] : [];
  });
  A.meta.version = (A.meta.version || 1) + 1;
  A.meta.description += opt.note ? " " + opt.note : "";
  return A;
}
