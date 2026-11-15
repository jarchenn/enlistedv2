// ZF1 z-fight audit: coplanar, same-facing, overlapping faces of different drawn parts in every content/assets/*.json.
// Drawn parts = `parts` plus each wall panel's intact state (panels[k].states[0]); hidden pieces (v: 0) are skipped.
// Part maths follow src/assets.cpp (parsePart, prismPoint, wedgeProfile, glm::quat from euler degrees).
// Opposite-facing coplanar faces are two solids touching back to back: both faces are hidden, so they are not counted.
// Undersides at or below ground level (asset y <= 2 cm) sit on the terrain and are skipped.
// A pair is skipped when its overlap is buried in a third solid part (sampled 5 mm in front of the face).
// Usage: node tools/audit_zfight.mjs [--json out.json] [--asset id-prefix] [--top N] [--fix]
//   --fix: run dezfight on the JSON files themselves (for assets no generator in build_mpu_assets.mjs writes);
//   --except ids.txt: whitespace-separated ids to leave alone (build_mpu_assets.mjs prints the ids it writes)
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const DIR = path.join(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '..', 'content', 'assets');
const PLANE_EPS = 0.002, AREA_MIN = 1e-4;   // 2 mm, 1 cm^2

// ---- vector helpers
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = a => { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
// glm::quat(vec3 euler radians)
function quatEuler(r) {
  const [x, y, z] = r.map(d => d * Math.PI / 360);
  const cx = Math.cos(x), sx = Math.sin(x), cy = Math.cos(y), sy = Math.sin(y), cz = Math.cos(z), sz = Math.sin(z);
  return [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];   // x y z w
}
function qrot(q, v) {   // q = [x y z w]
  const u = [q[0], q[1], q[2]], w = q[3];
  return add(add(mul(u, 2 * dot(u, v)), mul(v, w * w - dot(u, u))), mul(cross(u, v), 2 * w));
}

// ---- part -> convex faces (asset space) and convex half-spaces for the inside test
function partGeom(j) {
  const t = j.t || 'b';
  if (t === 'a') return null;   // arches: curved, not audited
  const faces = [];   // {pts, n}
  let local = [];     // local polygons {pts, n}
  if (t === 'p') {
    const axis = j.axis ?? 0, P = (a, uv) => axis === 0 ? [a, uv[1], uv[0]] : axis === 2 ? [uv[0], uv[1], a] : [uv[0], a, uv[1]];
    const A = j.tri.map(uv => P(j.a[0], uv)), B = j.tri.map(uv => P(j.a[1], uv));
    const cen = mul([...A, ...B].reduce(add, [0, 0, 0]), 1 / 6);
    const face = pts => { let n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))); const m = mul(pts.reduce(add, [0, 0, 0]), 1 / pts.length); if (dot(n, sub(m, cen)) < 0) n = mul(n, -1); faces.push({ pts, n }); };
    face(A); face(B);
    for (let k = 0; k < 3; ++k) face([A[k], A[(k + 1) % 3], B[(k + 1) % 3], B[k]]);
    return { faces, planes: faces.map(f => ({ n: f.n, d: dot(f.n, f.pts[0]) })) };
  }
  const s = j.s || [1, 1, 1], h = mul(s, 0.5), c = j.c || [0, 0, 0];
  const q = j.q ? j.q : j.r ? quatEuler(j.r) : [0, 0, 0, 1];
  const half = Math.min(h[0], h[1], h[2]);
  if (t === 'b' || t === 'r') {
    const inset = t === 'r' ? Math.min(Math.max(j.rad ?? 0.1, 0), half) : Math.min(Math.max(j.bv ?? 0, 0), half);
    for (let ax = 0; ax < 3; ++ax) for (const sg of [-1, 1]) {
      const u = (ax + 1) % 3, w = (ax + 2) % 3, pts = [];
      for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) { const p = [0, 0, 0]; p[ax] = sg * h[ax]; p[u] = a * (h[u] - inset); p[w] = b * (h[w] - inset); pts.push(p); }
      const n = [0, 0, 0]; n[ax] = sg; local.push({ pts, n });
    }
  } else if (t === 'c' || t === 'k') {
    const N = 24, top = Math.min(Math.max(j.top ?? (t === 'k' ? 0 : 1), 0), 1);
    const ring = Array.from({ length: N }, (_, i) => [Math.cos(2 * Math.PI * i / N) * h[0], Math.sin(2 * Math.PI * i / N) * h[2]]);
    local.push({ pts: ring.map(r => [r[0], -h[1], r[1]]).reverse(), n: [0, -1, 0] });
    if (top > 1e-3) local.push({ pts: ring.map(r => [r[0] * top, h[1], r[1] * top]), n: [0, 1, 0] });
    for (let i = 0; i < N; ++i) {   // sides: inside test only (curved, never audited as faces)
      const a = ring[i], b = ring[(i + 1) % N];
      local.push({ side: true, pts: [[a[0], -h[1], a[1]], [b[0], -h[1], b[1]], [b[0] * top, h[1], b[1] * top]], n: null });
    }
  } else if (t === 'w') {
    const top = Math.min(Math.max(j.top ?? 0, 0), 1);
    const prof = [[-h[2], -h[1]], [h[2], -h[1]]]; if (top > 1e-3) prof.push([h[2], -h[1] + s[1] * top]); prof.push([-h[2], h[1]]);
    for (const x of [-h[0], h[0]]) local.push({ pts: prof.map(([z, y]) => [x, y, z]), n: [Math.sign(x), 0, 0] });
    for (let i = 0; i < prof.length; ++i) {
      const [z0, y0] = prof[i], [z1, y1] = prof[(i + 1) % prof.length];
      local.push({ pts: [[-h[0], y0, z0], [h[0], y0, z0], [h[0], y1, z1], [-h[0], y1, z1]], n: null });
    }
  } else return null;
  for (const f of local) {
    // the face's local axis (box-like faces only): what dezfight moves
    const ln = f.n || norm(cross(sub(f.pts[1], f.pts[0]), sub(f.pts[2], f.pts[0])));
    const ax = ln.findIndex(x => Math.abs(Math.abs(x) - 1) < 1e-6);
    if (ax >= 0 && !f.side) { const sg = f.n ? Math.sign(ln[ax]) : Math.sign(f.pts[0][ax]); f.axis = sg ? { ax, sg } : null; }
    const pts = f.pts.map(p => add(c, qrot(q, p)));
    let n = f.n ? qrot(q, f.n) : norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])));
    if (!f.n && dot(n, sub(pts[0], c)) < 0) n = mul(n, -1);
    f.w = { pts, n: norm(n), axis: f.axis || null };
    if (!f.side) faces.push(f.w);
  }
  return { faces, planes: local.map(f => ({ n: f.w.n, d: dot(f.w.n, f.w.pts[0]) })) };
}
const inside = (g, p, eps = 1e-4) => g.planes.every(pl => dot(pl.n, p) - pl.d < -eps);

// ---- 2D convex clip (Sutherland-Hodgman) in the face plane
function basis(n) { const a = Math.abs(n[0]) < 0.9 ? [1, 0, 0] : [0, 1, 0]; const u = norm(cross(n, a)); return [u, cross(n, u)]; }
const area2 = P => { let s = 0; for (let i = 0; i < P.length; ++i) { const a = P[i], b = P[(i + 1) % P.length]; s += a[0] * b[1] - a[1] * b[0]; } return s / 2; };
function clip(subj, clipP) {
  let out = subj;
  const ccw = area2(clipP) > 0 ? 1 : -1;
  for (let i = 0; i < clipP.length && out.length; ++i) {
    const a = clipP[i], b = clipP[(i + 1) % clipP.length], inp = out; out = [];
    const side = p => ccw * ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]));
    for (let k = 0; k < inp.length; ++k) {
      const p = inp[k], q = inp[(k + 1) % inp.length], sp = side(p), sq = side(q);
      if (sp >= 0) out.push(p);
      if ((sp >= 0) !== (sq >= 0)) { const t = sp / (sp - sq); out.push([p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t]); }
    }
  }
  return out;
}

// ---- one asset
export function auditAsset(a) {
  const drawn = [];   // {j, label}
  (a.parts || []).forEach((j, i) => { if (j.v !== 0) drawn.push({ j, label: `parts[${i}]` }); });
  (a.panels || []).forEach((pn, k) => (pn.states?.[0] || []).forEach((j, i) => { if (j.v !== 0) drawn.push({ j, label: `panels[${k}:${pn.id}][${i}]` }); }));
  const geo = drawn.map(d => partGeom(d.j));
  const buckets = new Map(), pairs = [];
  const qn = n => n.map(x => Math.round(x * 500)).join(',');
  geo.forEach((g, pi) => g && g.faces.forEach(f => {
    const d = dot(f.n, f.pts[0]), key = qn(f.n), db = Math.floor(d / PLANE_EPS);
    if (f.n[1] < -0.99 && -d <= 0.02) return;   // an underside at or below ground level: on the terrain, never seen
    const rec = { pi, f, d };
    for (const k2 of [db - 1, db, db + 1]) for (const o of buckets.get(key + '|' + k2) || []) {
      if (o.pi === pi || Math.abs(o.d - d) > PLANE_EPS || dot(o.f.n, f.n) < 0.9999) continue;
      const [u, v] = basis(f.n), P = x => [dot(x, u), dot(x, v)];
      const ov = clip(f.pts.map(P), o.f.pts.map(P)); if (ov.length < 3) continue;
      const ar = Math.abs(area2(ov)); if (ar < AREA_MIN) continue;
      // buried: the overlap (centroid and points toward its corners) 5 mm in front of the face lies in a third solid
      const cen = ov.reduce((s, p) => [s[0] + p[0] / ov.length, s[1] + p[1] / ov.length], [0, 0]);
      const samples = [cen, ...ov.map(p => [cen[0] + (p[0] - cen[0]) * 0.8, cen[1] + (p[1] - cen[1]) * 0.8])].map(p => add(add(mul(u, p[0]), mul(v, p[1])), mul(f.n, d + 0.005)));
      const buried = samples.every(sp => geo.some((g3, k3) => g3 && k3 !== pi && k3 !== o.pi && inside(g3, sp)));
      if (buried) continue;
      const A = drawn[o.pi], B = drawn[pi];
      const dup = JSON.stringify({ ...A.j, k: 0 }) === JSON.stringify({ ...B.j, k: 0 });
      const ga = geo[o.pi], gb = geo[pi];
      const allIn = (x, y) => x.faces.every(ff => ff.pts.every(p => y.planes.every(pl => dot(pl.n, p) - pl.d < 1e-3)));
      const fa = P2 => Math.abs(area2(P2.pts.map(P)));
      pairs.push({ a: A.label, b: B.label, ma: A.j.m, mb: B.j.m, area: ar, n: f.n.map(x => +x.toFixed(3)), d: +d.toFixed(4), at: add(add(mul(u, cen[0]), mul(v, cen[1])), mul(f.n, d)).map(x => +x.toFixed(3)), dup, thinIn: !dup && (allIn(ga, gb) || allIn(gb, ga)),
                   ref: { A: A.j, B: B.j, fA: o.f.axis, fB: f.axis, areaA: fa(o.f), areaB: fa(f) } });
    }
    const bk = key + '|' + db; if (!buckets.has(bk)) buckets.set(bk, []); buckets.get(bk).push(rec);
  }));
  pairs.sort((x, y) => y.area - x.area);
  return { pairs, area: pairs.reduce((s, p) => s + p.area, 0) };
}

// ---- the fix (tools/build_mpu_assets.mjs runs it on every generated asset; --fix patches the other JSON files):
// different materials: the detail part's fighting face moves OFF outward (the part grows by OFF on that side only), so
// trim / cladding / markings sit in front of the surface they were flush with. The detail part is the non-colliding one
// (k: 0) when only one is, else the one with the smaller face. Identical duplicates: the second is hidden (v: 0); its
// collider stays and no part index moves. Same-material flush pairs are left (one colour, no visible fight).
export const OFF = 0.003, MAXMOVES = 2;
export function dezfight(a) {
  let moved = 0, hidden = 0;
  const done = new Map();   // part -> face -> moves: at most MAXMOVES (stacked layers need two; crossing trims would otherwise grow in layers)
  for (let pass = 0; pass < 4; ++pass) {
    let any = false; const now = new Set();   // faces moved in this pass
    const fkey = (P, f) => f ? f.ax * 2 + (f.sg > 0 ? 1 : 0) : -1, ever = (P, f) => (done.get(P)?.get(fkey(P, f)) || 0) >= MAXMOVES;
    const movedNow = (P, f) => now.has(P) && done.get(P)?.has(fkey(P, f));
    for (const p of auditAsset(a).pairs) {
      const { A, B, fA, fB, areaA, areaB } = p.ref;
      if (p.dup) { if (A.m === B.m && B.v !== 0) { B.v = 0; hidden++; any = true; } continue; }
      if (p.ma === p.mb) continue;
      // the other face moved this pass: both out by OFF would meet again, so the pair waits for the next pass
      if ((fA && movedNow(A, fA)) || (fB && movedNow(B, fB))) continue;
      const kA = A.k ?? 1, kB = B.k ?? 1;
      const order = (kA === 0) !== (kB === 0) ? (kA === 0 ? ['A', 'B'] : ['B', 'A']) : areaA <= areaB ? ['A', 'B'] : ['B', 'A'];
      const pick = order.map(x => x === 'A' ? [A, fA] : [B, fB]).find(([P, f]) => f && !ever(P, f));
      if (!pick) continue;   // no box-like face left to move: reported as left
      const [J, f] = pick; if (!done.has(J)) done.set(J, new Map()); done.get(J).set(fkey(J, f), (done.get(J).get(fkey(J, f)) || 0) + 1); now.add(J);
      const q = J.q ? J.q : J.r ? quatEuler(J.r) : [0, 0, 0, 1], e = [0, 0, 0]; e[f.ax] = f.sg * OFF / 2;
      const s = [...(J.s || [1, 1, 1])]; s[f.ax] += OFF; J.s = s.map(x => +x.toFixed(5));
      J.c = add(J.c || [0, 0, 0], qrot(q, e)).map(x => +x.toFixed(5));
      moved++; any = true;
    }
    if (!any) break;
  }
  return { moved, hidden, left: auditAsset(a).pairs.filter(p => p.ma !== p.mb && !p.dup).length };
}

// ---- self-check: two boxes sharing a top face fight; a 3 mm step does not; back-to-back contact does not
{
  const t1 = auditAsset({ parts: [{ t: 'b', c: [0, 0, 0], s: [1, 1, 1] }, { t: 'b', c: [0, 0.25, 0], s: [0.5, 0.5, 0.5] }] });
  const t2 = auditAsset({ parts: [{ t: 'b', c: [0, 0, 0], s: [1, 1, 1] }, { t: 'b', c: [0, 0.253, 0], s: [0.5, 0.5, 0.5] }] });
  const t3 = auditAsset({ parts: [{ t: 'b', c: [0, 0, 0], s: [1, 1, 1] }, { t: 'b', c: [0, 1, 0], s: [1, 1, 1] }] });
  const t4 = auditAsset({ parts: [{ t: 'b', c: [0, 0, 0], s: [1, 1, 1], r: [0, 90, 0] }, { t: 'b', c: [0, 0, 0.6], s: [0.2, 0.2, 0.2] }] });
  const a5 = { parts: [{ t: 'b', c: [0, 1, 0], s: [4, 2, 0.3], m: 'plaster' }, { t: 'b', c: [0, 1, 0.13], s: [1, 0.2, 0.04], m: 'trim', k: 0 }, { t: 'b', c: [0, 1, 0], s: [4, 2, 0.3], m: 'plaster', k: 0 }] };
  const f5 = dezfight(a5);
  if (!(t1.pairs.length && t1.pairs[0].thinIn && !t2.pairs.length && !t3.pairs.length && !t4.pairs.length && f5.moved === 1 && f5.hidden === 1 && f5.left === 0 && Math.abs(a5.parts[1].c[2] - 0.1315) < 1e-9 && a5.parts[2].v === 0)) { console.error('self-check failed', t1, t2, t3, t4, f5, a5); process.exit(2); }
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'))) {
  const only = opt('--asset', ''), top = +opt('--top', 40), out = opt('--json', '');
  const rows = [], fixed = [], fix = args.includes('--fix');
  const except = new Set(opt('--except', '') ? fs.readFileSync(opt('--except'), 'utf8').split(/s+/).filter(Boolean) : []);   // ids a generator writes
  for (const f of fs.readdirSync(DIR).filter(f => f.endsWith('.json')).sort()) {
    let a, text; try { text = fs.readFileSync(path.join(DIR, f), 'utf8'); a = JSON.parse(text); } catch { continue; }
    const id = a.id || f.replace(/\.json$/, ''); if (only && !id.startsWith(only)) continue;
    if (fix && !except.has(id)) {
      const r = dezfight(a);
      if (r.moved || r.hidden) {   // keep the file's own layout: indent and line ends
        const ind = /^\{\r?\n( +)/.exec(text)?.[1].length, eol = text.includes('\r\n') ? '\r\n' : '\n';
        let s = JSON.stringify(a, null, ind); if (ind) s = s.replace(/\n/g, eol); if (/\r?\n$/.test(text)) s += eol;
        fs.writeFileSync(path.join(DIR, f), s); fixed.push(`${id} (${r.moved} moved, ${r.hidden} hidden, ${r.left} left)`);
      }
    }
    const r = auditAsset(a), dm = r.pairs.filter(p => p.ma !== p.mb && !p.dup);
    if (r.pairs.length) rows.push({ id, count: r.pairs.length, area: r.area, diffMat: dm.length, diffArea: dm.reduce((s, p) => s + p.area, 0), dup: r.pairs.filter(p => p.dup).length, thinIn: r.pairs.filter(p => p.thinIn).length, worst: (dm.length ? dm : r.pairs).slice(0, 5).map(({ ref, ...w }) => w) });
  }
  // worst first by the visible kind: different materials flush on one plane (same-material pairs show one colour)
  rows.sort((x, y) => y.diffArea - x.diffArea || y.area - x.area);
  const sum = k => rows.reduce((s, r) => s + r[k], 0);
  console.log(`z-fight pairs: ${sum('count')} in ${rows.length} assets, ${sum('area').toFixed(2)} m2; different materials ${sum('diffMat')} pairs, ${sum('diffArea').toFixed(2)} m2 (dup parts ${sum('dup')}, thin-inside ${sum('thinIn')})`);
  console.log('asset'.padEnd(36), 'pairs'.padStart(6), 'm2'.padStart(8), 'diffMat'.padStart(8), 'diff m2'.padStart(8), 'dup'.padStart(5), 'thinIn'.padStart(7), '  worst pair');
  for (const r of rows.slice(0, top)) { const w = r.worst[0]; console.log(r.id.padEnd(36), String(r.count).padStart(6), r.area.toFixed(3).padStart(8), String(r.diffMat).padStart(8), r.diffArea.toFixed(3).padStart(8), String(r.dup).padStart(5), String(r.thinIn).padStart(7), `  ${w.a} ${w.ma} / ${w.b} ${w.mb} ${(w.area * 1e4).toFixed(0)} cm2`); }
  if (out) fs.writeFileSync(out, JSON.stringify({ total: sum('count'), diffMat: sum('diffMat'), area: sum('area'), diffArea: sum('diffArea'), rows }, null, 1));
  if (fixed.length) console.log(`fixed ${fixed.length} files:\n  ` + fixed.join('\n  '));
}
