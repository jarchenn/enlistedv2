// Water features (V1, docs/rendering.md#water): two ponds, a flooded shell crater, a cluster of puddles and three stream
// pieces (straight, bend, end). Each asset lowers the ground with a "terrainCut" (the bed: flat floor, linear banks) and
// carries a "water" block that src/water.cpp draws in the water pass after the opaque world:
//   "water": { "surfaces": [ { "outline": [[x, z], ...], "level": y, "kind": "pond|stream|puddle|crater|sea",
//                              "path": [[x, z], ...], "speed": m/s, "clarity": x, "waves": x } ] }
// Positions are asset-local; "level" is relative to the placement height. The surface outline reaches past the
// waterline under the banks: the ground hides its edge and the depth-based shore fade blends the waterline. Waterline:
// the cut is padded 0.45 m outward (terrain grid) and ramps down over `bank`, so it lies bank * (-level) / depth inside
// the padded outline. Each surface also has a thin
// glass slab of its outline (water_proxy): it gives the asset its bounds and thumbnail and is not drawn in the world
// (the water pass replaces it). Stream pieces carry snap sockets for V6 ("snap": [{p, dir, kind: "stream"}]); their bed
// runs on past each socket by the bank width, so joined pieces share one continuous channel floor.
// Written by tools/build_mpu_assets.mjs (never run tools/build_assets.mjs for these).
import { Asset } from "./kit.mjs";
import { setNav, r3, rng, offsetLine, extendEnds } from "./catalog_atlantic.mjs";

function mats(A) {
  A.mat("water_proxy", [0.05, 0.1, 0.1], 0.1, "glass");
  A.mat("bank_stone", [0.34, 0.32, 0.28], 0.9, "stone");
  A.mat("mud", [0.11, 0.09, 0.065], 0.95, "plain");
  A.mat("reed", [0.2, 0.24, 0.09], 0.85, "plain");
  A.mat("reed_dry", [0.36, 0.3, 0.16], 0.9, "plain");
  A.mat("earth", [0.12, 0.09, 0.06], 1.0, "plain");
}
const circle = (r, n, cx = 0, cz = 0, wob = null) => Array.from({ length: n }, (_, i) => {
  const a = (i / n) * Math.PI * 2, rr = r * (wob ? wob(a) : 1);
  return [r3(cx + Math.cos(a) * rr), r3(cz + Math.sin(a) * rr)];
});
const area = (p) => { let s = 0; for (let i = 0, j = p.length - 1; i < p.length; j = i++) s += (p[j][0] - p[i][0]) * (p[j][1] + p[i][1]); return s / 2; };
const ccw = (p) => (area(p) < 0 ? p.slice().reverse() : p);
// ear clipping of a simple polygon (x, z): triangles as index triples
function earClip(poly) {
  const P = ccw(poly), idx = P.map((_, i) => i), tris = [];
  const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  const inside = (p, a, b, c) => cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
  let guard = 0;
  while (idx.length > 3 && guard++ < 10000) {
    let cut = false;
    for (let k = 0; k < idx.length; k++) {
      const i0 = idx[(k + idx.length - 1) % idx.length], i1 = idx[k], i2 = idx[(k + 1) % idx.length];
      const a = P[i0], b = P[i1], c = P[i2];
      if (cross(a, b, c) <= 1e-9) continue;
      if (idx.some((j) => j !== i0 && j !== i1 && j !== i2 && inside(P[j], a, b, c))) continue;
      tris.push([a, b, c]); idx.splice(k, 1); cut = true; break;
    }
    if (!cut) break;
  }
  if (idx.length === 3) tris.push(idx.map((i) => P[i]));
  return tris;
}
// the glass slab standing in for a surface (bounds, thumbnail): 1 cm thick, just under the level
function proxy(A, outline, level) {
  for (const t of earClip(outline)) A.prism(1, level - 0.01, level, t, "water_proxy", { k: 0 });
}
function surface(A, s) {
  const w = { outline: ccw(s.outline).map((p) => p.map(r3)), level: r3(s.level), kind: s.kind };
  if (s.path) { w.path = s.path.map((p) => p.map(r3)); w.speed = s.speed ?? 0.5; }
  if (s.clarity) w.clarity = s.clarity;
  if (s.waves) w.waves = s.waves;
  (A.meta.water ??= { surfaces: [] }).surfaces.push(w);
  proxy(A, s.outline, s.level);
}
// reeds: clumps of thin leaning blades around a point (visual only)
function reeds(A, R, x, z, n, h = 1.3) {
  for (let i = 0; i < n; i++) {
    const a = R() * Math.PI * 2, d = R() * 0.45, hh = h * (0.6 + R() * 0.5);
    A.obox([x + Math.cos(a) * d, hh / 2 - 0.15, z + Math.sin(a) * d], [0.025, hh, 0.012], [(R() - 0.5) * 16, R() * 180, (R() - 0.5) * 16], R() < 0.25 ? "reed_dry" : "reed", { k: 0 });
  }
}
function stones(A, R, pts, s0 = 0.18, s1 = 0.45, y = -0.15) {
  for (const [x, z] of pts) { const s = s0 + R() * (s1 - s0); A.rbox([x - s, y - s * 0.3, z - s * 0.8], [x + s, y + s * 0.55, z + s * 0.8], s * 0.35, "bank_stone", { k: 0 }); }
}
const waterNav = (outline) => ({ effect: "none", areas: [{ outline: outline.map((p) => p.map(r3)), area: "water" }] });

// ---------------------------------------------------------------- ponds
function pondRound() {
  const A = new Asset({ id: "wat_pond_round", version: 1, name: "Pond (round, 13 m)", category: "Water",
    description: "Farm pond about 13 m across: a bowl 1.3 m deep with gentle banks, peaty green water, reeds on three sides and a few stones at the edge. No collision on the water: you wade in.",
    floors: 0, enterable: false, footprint: [16, 16], plinthDepth: 0, maxGrade: 1.2, noAutoPanels: true });
  mats(A); const R = rng(7101);
  const cut = circle(7.4, 32);
  setNav(A, waterNav(circle(6.6, 24)), [{ outline: cut, depth: 1.3, bank: 3.0 }]);
  surface(A, { outline: circle(7.4, 32), level: -0.35, kind: "pond" });   // waterline r 7.04
  for (const a of [0.4, 2.2, 4.4]) reeds(A, R, Math.cos(a) * 6.8, Math.sin(a) * 6.8, 22);
  stones(A, R, [[6.9, -1.6], [-6.6, 2.4], [-2.0, -6.8]]);
  return A;
}
function pondIrregular() {
  const A = new Asset({ id: "wat_pond_irregular", version: 1, name: "Pond (irregular, 15 x 10 m)", category: "Water",
    description: "Kidney-shaped field pond, 15 x 10 m and 1.1 m deep, with a reedy shallow end and muddy margins.",
    floors: 0, enterable: false, footprint: [18, 13], plinthDepth: 0, maxGrade: 1.2, noAutoPanels: true });
  mats(A); const R = rng(7102);
  const wob = (k) => (a) => 1 + 0.16 * Math.sin(2 * a + 0.6) + 0.09 * Math.sin(3 * a + 1.9) + k;
  const shape = (r, n, k = 0) => circle(r, n, 0, 0, wob(k)).map(([x, z]) => [r3(x * 1.45), r3(z * 0.95)]);
  setNav(A, waterNav(shape(4.4, 28)), [{ outline: shape(5.6, 40), depth: 1.1, bank: 2.6 }]);
  surface(A, { outline: shape(5.7, 40), level: -0.3, kind: "pond", clarity: 0.8 });   // waterline ~0.26 m inside the cut
  for (const [x, z] of [[-6.8, 1.0], [-6.0, -2.6], [5.6, 3.6], [1.0, -5.4]]) reeds(A, R, x, z, 18);
  stones(A, R, [[7.2, -0.8], [-2.8, 5.2]]);
  return A;
}
// ---------------------------------------------------------------- flooded shell crater
function craterPool() {
  const A = new Asset({ id: "wat_crater_pool", version: 1, name: "Flooded shell crater", category: "Water",
    description: "A 9 m shell crater with a thrown-up earth rim, half full of muddy rainwater 0.7 m deep. Clods and stones lie around the lip.",
    floors: 0, enterable: false, footprint: [12, 12], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  mats(A); const R = rng(7103);
  const wob = (a) => 1 + 0.07 * Math.sin(3 * a + 0.4) + 0.05 * Math.sin(5 * a + 2.1);
  setNav(A, waterNav(circle(2.6, 20, 0, 0, wob)), [{ outline: circle(4.4, 32, 0, 0, wob), depth: 1.8, bank: 2.6 }]);
  surface(A, { outline: circle(3.6, 28, 0, 0, wob), level: -1.1, kind: "crater" });   // waterline r ~3.26
  // the rim: a continuous low berm of thrown earth just outside the cut (overlapping slabs laid along the ring, leaning
  // outward), then loose clods on and beyond it
  for (let i = 0; i < 22; i++) {
    const a = (i / 22) * Math.PI * 2 + (R() - 0.5) * 0.08, r = (4.85 + R() * 0.35) * wob(a), x = Math.cos(a) * r, z = Math.sin(a) * r;
    const yaw = -((a * 180) / Math.PI + 90), lean = 10 + R() * 8, h = 0.22 + R() * 0.16;
    A.obox([x, h * 0.25, z], [1.75 + R() * 0.5, h, 1.3 + R() * 0.4], [0, yaw, lean], R() < 0.3 ? "mud" : "earth", { k: 0 });
  }
  for (let i = 0; i < 30; i++) {
    const a = R() * Math.PI * 2, r = (4.6 + R() * 2.0) * wob(a), s = 0.1 + R() * 0.22;
    A.obox([Math.cos(a) * r, s * 0.3, Math.sin(a) * r], [s * 2, s, s * 1.6], [(R() - 0.5) * 30, R() * 360, (R() - 0.5) * 30], R() < 0.4 ? "mud" : "earth", { k: 0 });
  }
  stones(A, R, [[5.4, 1.2], [-4.9, -2.4], [1.0, 5.6], [-1.6, -5.5]], 0.12, 0.3, 0.0);
  return A;
}
// ---------------------------------------------------------------- puddles
function puddles() {
  const A = new Asset({ id: "wat_puddles", version: 1, name: "Puddle cluster", category: "Water",
    description: "Five rain puddles 1.2-2.8 m across in shallow dips, a few centimetres of muddy water: for lanes, yards and churned field gates.",
    floors: 0, enterable: false, footprint: [9, 7], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  mats(A); const R = rng(7104);
  const P = [[-2.6, -1.2, 1.4, 0.8], [0.4, -1.8, 0.9, 0.6], [2.4, 0.4, 1.2, 0.9], [-0.8, 1.4, 1.0, 0.7], [-3.4, 1.8, 0.7, 0.5]];
  const cuts = [];
  for (const [x, z, rx, rz] of P) {
    const ph = R() * 6;
    const shape = (k) => circle(1, 16).map(([u, v], i) => { const w = 1 + 0.18 * Math.sin(3 * ((i / 16) * Math.PI * 2) + ph); return [r3(x + u * rx * k * w), r3(z + v * rz * k * w)]; });
    cuts.push({ outline: shape(1.35), depth: 0.12, bank: 0.55 });
    surface(A, { outline: shape(1.75), level: -0.05, kind: "puddle" });   // waterline ~0.22 m outside the cut outline
  }
  setNav(A, { effect: "none" }, cuts);
  return A;
}
// ---------------------------------------------------------------- streams
// a brook about 4 m wide (waterline 1.96 m off the centreline; the surface runs to 2.15 m under the banks) in a channel
// 0.8 m deep; flow runs along the centreline (path)
const SW = 2.15, CW = 2.3, SD = 0.8, SB = 1.4, SL = -0.45, EXT = 1.6;
function streamPiece(A, R, center, { endCap = false } = {}) {
  const ext = extendEnds(center, EXT);
  const cl = endCap ? [center[0], ...ext.slice(1)] : ext;   // an end cap's own end is closed (rounded) instead
  let cutPoly = offsetLine(cl, CW).concat(offsetLine(cl, -CW).reverse());
  let waterPoly = offsetLine(center, SW).concat(offsetLine(center, -SW).reverse());
  if (endCap) {   // round the start: a half circle behind the first centreline point, from the right bank to the left
    const [x0, z0] = center[0], [x1, z1] = center[1], L = Math.hypot(x1 - x0, z1 - z0), ux = (x1 - x0) / L, uz = (z1 - z0) / L, nx = -uz, nz = ux;
    const arc = (r) => Array.from({ length: 7 }, (_, i) => { const f = ((i + 1) / 8) * Math.PI, c = Math.cos(f), sn = Math.sin(f); return [x0 + r * (-nx * c - ux * sn), z0 + r * (-nz * c - uz * sn)]; });
    cutPoly = cutPoly.concat(arc(CW));
    waterPoly = waterPoly.concat(arc(SW));
  }
  setNav(A, waterNav(waterPoly.map((p) => p.map(r3))), [{ outline: cutPoly.map((p) => p.map(r3)), depth: SD, bank: SB }]);
  surface(A, { outline: waterPoly, level: SL, kind: "stream", path: center, speed: 0.55 });
  // bank dressing: reeds and stones along both banks
  for (let i = 0; i < center.length - 1; i += 2) {
    const side = i % 4 === 0 ? 1 : -1, [x, z] = offsetLine(center, side * (SW + 0.5))[i];
    reeds(A, R, x, z, 10, 1.1);
  }
  const sb = offsetLine(center, -(SW + 0.25)), sa = offsetLine(center, SW + 0.25);
  stones(A, R, [sb[Math.floor(sb.length / 2)], sa[sa.length - 1], sa[0]], 0.15, 0.35, -0.3);
}
const sock = (p, dir) => ({ p: [r3(p[0]), 0, r3(p[1])], dir: [r3(dir[0]), 0, r3(dir[1])], kind: "stream" });
function streamStraight() {
  const A = new Asset({ id: "wat_stream_straight", version: 1, name: "Stream (straight, 8 m)", category: "Water",
    description: "8 m of brook 4 m wide in a channel 0.8 m deep, flowing toward local +Z. Snaps end to end with the other stream pieces.",
    floors: 0, enterable: false, footprint: [5, 8], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  mats(A); const R = rng(7105);
  const center = Array.from({ length: 5 }, (_, i) => [0, -4 + i * 2]);
  streamPiece(A, R, center);
  A.meta.snap = [sock([0, -4], [0, -1]), sock([0, 4], [0, 1])];
  return A;
}
function streamBend() {
  const A = new Asset({ id: "wat_stream_bend", version: 1, name: "Stream (90 degree bend)", category: "Water",
    description: "A quarter bend of brook on a 6 m radius: enters along +Z at one end and leaves toward -X. Snaps to the other stream pieces.",
    floors: 0, enterable: false, footprint: [10, 10], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  mats(A); const R = rng(7106);
  const Rr = 6, cx = -3, cz = -3, n = 12;
  const center = Array.from({ length: n + 1 }, (_, i) => { const a = (i / n) * (Math.PI / 2); return [r3(cx + Math.cos(a) * Rr), r3(cz + Math.sin(a) * Rr)]; });
  streamPiece(A, R, center);
  A.meta.snap = [sock(center[0], [0, -1]), sock(center[n], [-1, 0])];
  return A;
}
function streamEnd() {
  const A = new Asset({ id: "wat_stream_end", version: 1, name: "Stream (spring end, 5 m)", category: "Water",
    description: "Where a brook rises: a rounded spring pool with stones, flowing out along +Z for 4 m to a snap socket. Caps an open stream end.",
    floors: 0, enterable: false, footprint: [5, 6.5], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  mats(A); const R = rng(7107);
  const center = Array.from({ length: 3 }, (_, i) => [0, i * 2]);
  streamPiece(A, R, center, { endCap: true });
  stones(A, R, [[-0.6, -1.9], [0.7, -1.7], [-1.4, -1.0], [1.5, -0.7]], 0.2, 0.42, -0.2);
  A.meta.snap = [sock([0, 4], [0, 1])];
  return A;
}

export function waterAssets() {
  return [pondRound(), pondIrregular(), craterPool(), puddles(), streamStraight(), streamBend(), streamEnd()];
}
