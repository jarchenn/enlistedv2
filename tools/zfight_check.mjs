// Coplanar-overlap (z-fighting) check over the enterable building assets (GX4).
// Two visible box faces (boxes, rotated boxes, rounded boxes) of different parts z-fight when they face the same way,
// lie within 6 mm of each other and overlap in area, unless the overlap is buried inside a third solid (a face hidden in
// a wall never shows). Parts: the static parts, every wall bay's intact state, the door / window leaves (closed).
// node tools/zfight_check.mjs [--ref <git-ref>] [--list] [ids...]     (exit 1 if any overlap)
// The same rule runs in --selftest-structures (src/selftest_structures.cpp, "no coplanar overlaps").
import fs from "node:fs";
import path from "node:path";
import { execSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
export const EPS = 0.006, AREA = 1e-4;   // the >= 6 mm rule: faces closer than this fight at range

function qEuler(r) {   // glm::quat(radians(r)): qz * qy * qx
  const [x, y, z] = r.map((d) => (d * Math.PI) / 360), cx = Math.cos(x), sx = Math.sin(x), cy = Math.cos(y), sy = Math.sin(y), cz = Math.cos(z), sz = Math.sin(z);
  return [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];
}
function qRot(q, v) {
  const [x, y, z, w] = q, t = [2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])];
  return [v[0] + w * t[0] + y * t[2] - z * t[1], v[1] + w * t[1] + z * t[0] - x * t[2], v[2] + w * t[2] + x * t[1] - y * t[0]];
}
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const add = (a, b, k = 1) => [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k];

// a box part as centre, axes, half sizes
export function boxOf(p) {
  if ((p.t ?? "b") !== "b" && p.t !== "r") return null;
  const q = p.q ? p.q : p.r ? qEuler(p.r) : [0, 0, 0, 1], ax = [[1, 0, 0], [0, 1, 0], [0, 0, 1]].map((v) => qRot(q, v));
  return { c: p.c, ax, h: p.s.map((v) => v / 2), m: p.m };
}
const inside = (b, pt, tol = 1e-4) => { const d = add(pt, b.c, -1); return [0, 1, 2].every((i) => Math.abs(dot(d, b.ax[i])) < b.h[i] - tol); };

// convex polygon clip (Sutherland-Hodgman) in 2D
function clip(poly, edge) {
  const [a, b] = edge, side = (p) => (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
  const out = [];
  for (let i = 0; i < poly.length; i++) {
    const p = poly[i], q = poly[(i + 1) % poly.length], sp = side(p), sq = side(q);
    if (sp >= 0) out.push(p);
    if ((sp >= 0) !== (sq >= 0)) { const t = sp / (sp - sq); out.push([p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t]); }
  }
  return out;
}
const area = (P) => { let s = 0; for (let i = 0; i < P.length; i++) { const a = P[i], b = P[(i + 1) % P.length]; s += a[0] * b[1] - b[0] * a[1]; } return s / 2; };

export function overlaps(j) {
  const parts = [];
  const push = (list, tag) => { for (const p of list || []) if (p.v !== 0) { const b = boxOf(p); if (b) parts.push({ ...b, tag, p }); } };
  push(j.parts, "part");
  for (const pn of j.panels || []) push(pn.states?.[0], "bay:" + pn.id);
  for (const it of j.interactables || []) for (const L of it.leaves || []) push(L.parts, "leaf:" + it.id);
  // faces
  const faces = [];
  parts.forEach((b, pi) => {
    for (let i = 0; i < 3; i++) for (const s of [-1, 1]) {
      const n = b.ax[i].map((v) => v * s), j2 = (i + 1) % 3, k2 = (i + 2) % 3;
      const fc = add(b.c, n, b.h[i]);
      faces.push({ pi, n, d: dot(n, fc), fc, u: b.ax[j2], v: b.ax[k2], hu: b.h[j2], hv: b.h[k2] });
    }
  });
  // bucket by quantised normal + plane offset
  const key = (f, dd) => f.n.map((v) => Math.round(v * 200)).join(",") + "|" + Math.round((f.d + dd) / EPS);
  const buckets = new Map();
  faces.forEach((f, fi) => { const k = key(f, 0); if (!buckets.has(k)) buckets.set(k, []); buckets.get(k).push(fi); });
  // solids grid for the buried test
  const G = 1.0, grid = new Map();
  parts.forEach((b, pi) => {
    const r = Math.hypot(...b.h);
    for (let x = Math.floor((b.c[0] - r) / G); x <= Math.floor((b.c[0] + r) / G); x++)
      for (let y = Math.floor((b.c[1] - r) / G); y <= Math.floor((b.c[1] + r) / G); y++)
        for (let z = Math.floor((b.c[2] - r) / G); z <= Math.floor((b.c[2] + r) / G); z++) {
          const k = x + "," + y + "," + z; if (!grid.has(k)) grid.set(k, []); grid.get(k).push(pi);
        }
  });
  const buried = (pt, a, b) => (grid.get(pt.map((v) => Math.floor(v / G)).join(",")) || []).some((pi) => pi !== a && pi !== b && inside(parts[pi], pt));
  const seen = new Set(), out = [];
  for (const [k, list] of buckets) {
    const [nk, dk] = k.split("|"), cand = [...list];
    for (const dd of [-1, 1]) { const o = buckets.get(nk + "|" + (+dk + dd)); if (o) cand.push(...o); }
    for (const fa of list) for (const fb of cand) {
      if (fb <= fa) continue;
      const A = faces[fa], B = faces[fb];
      if (A.pi === B.pi || dot(A.n, B.n) < 0.9999 || Math.abs(A.d - B.d) > EPS + 2e-4) continue;   // +0.2 mm: the float selftest flags pairs at exactly 6 mm
      const pk = Math.min(A.pi, B.pi) + ":" + Math.max(A.pi, B.pi);
      if (seen.has(pk)) continue;
      // B's rectangle in A's (u, v) frame, clipped by A's rectangle
      const to2 = (p) => { const d = add(p, A.fc, -1); return [dot(d, A.u), dot(d, A.v)]; };
      let poly = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([su, sv]) => to2(add(add(B.fc, B.u, su * B.hu), B.v, sv * B.hv)));
      if (area(poly) < 0) poly.reverse();
      const rect = [[-A.hu, -A.hv], [A.hu, -A.hv], [A.hu, A.hv], [-A.hu, A.hv]];
      for (let e = 0; e < 4 && poly.length; e++) poly = clip(poly, [rect[e], rect[(e + 1) % 4]]);
      if (poly.length < 3) continue;
      const ar = Math.abs(area(poly));
      if (ar < AREA) continue;
      // buried: the overlap's centre (and its corners, pulled 5 mm in) just in front of both faces lies in a third solid
      const cx = poly.reduce((s, p) => s + p[0], 0) / poly.length, cy = poly.reduce((s, p) => s + p[1], 0) / poly.length;
      const pts = [[cx, cy], ...poly.map((p) => { const dx = p[0] - cx, dy = p[1] - cy, l = Math.hypot(dx, dy) || 1; return [p[0] - (dx / l) * Math.min(0.005, l * 0.5), p[1] - (dy / l) * Math.min(0.005, l * 0.5)]; })];
      const front = Math.max(A.d, B.d) - A.d + 0.003;   // a solid within 3 mm in front: only a grazing sliver could show
      if (pts.every(([x, y]) => buried(add(add(add(A.fc, A.u, x), A.v, y), A.n, front), A.pi, B.pi))) continue;
      seen.add(pk);
      const pa = parts[A.pi], pb = parts[B.pi];
      out.push({ da: A.d, db: B.d, nf: A.n, fu: A.u, fv: A.v, ta: pa.tag, tb: pb.tag, a: pa.tag + " " + pa.m, b: pb.tag + " " + pb.m, gap: Math.abs(A.d - B.d), area: ar, at: add(add(A.fc, A.u, cx), A.v, cy).map((v) => Math.round(v * 1000) / 1000), n: A.n.map((v) => Math.round(v * 100) / 100), pa: pa.p, pb: pb.p });
    }
  }
  return out;
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  const args = process.argv.slice(2), ri = args.indexOf("--ref"), ref = ri >= 0 ? args[ri + 1] : null, list = args.includes("--list");
  const ids = args.filter((a, i) => !a.startsWith("--") && (ri < 0 || i !== ri + 1));
  const dir = path.join(root, "content", "assets");
  const read = (f) => JSON.parse(ref ? execSync(`git show ${ref}:content/assets/${f}`, { cwd: root, maxBuffer: 1 << 28 }).toString() : fs.readFileSync(path.join(dir, f), "utf8"));
  const files = ref ? execSync(`git ls-tree --name-only ${ref} content/assets/`, { cwd: root }).toString().split("\n").filter((f) => f.endsWith(".json")).map((f) => path.basename(f)) : fs.readdirSync(dir).filter((f) => f.endsWith(".json"));
  let total = 0;
  for (const f of files.sort()) {
    if (ids.length && !ids.includes(f.slice(0, -5))) continue;
    let j; try { j = read(f); } catch { continue; }
    if (!j.enterable) continue;
    const o = overlaps(j);
    total += o.length;
    if (o.length) console.log(j.id.padEnd(30), o.length);
    if (list) for (const x of o.slice(0, 400)) console.log("   ", x.a, "|", x.b, "gap", x.gap.toFixed(4), "area", x.area.toFixed(4), "at", x.at.join(","), "n", x.n.join(","));
  }
  console.log("TOTAL", total);
  process.exit(total ? 1 : 0);
}
