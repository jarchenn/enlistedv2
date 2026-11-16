// NT4: four abandoned modern military frame tents for Outpost Woods (our own geometry):
//   mod_tent_sleeping  5 x 8 m: a row of folding cots with rolled sleeping bags, footlockers, a lantern on a hook.
//   mod_tent_mess      6 x 10 m: folding tables and benches, a field kitchen stove, jerrycans, crates.
//   mod_tent_medical   5 x 8 m: stretchers on trestles, a steel medical cabinet, red-cross panels, an IV stand.
//   mod_tent_command   5 x 6 m: map table, radio set on a desk, folding chairs, a map board, an antenna mast outside.
// Shell: olive canvas over a steel pole frame (uprights, rafters, eave pipes). The canvas is a grid of thin rotated
// panels sampled from one sagging surface (ridge dips between rafters, roof strips belly between eave and ridge, walls
// bow out between poles), so seams read as folds. Front (+z) door flap rolled up (opening 1.5 x 2.1 m, walkable);
// back door with one flap tied half open over a colliding mesh liner. Guy ropes to pegs, a torn roof strip with ragged
// edges and a hanging flap, mud on the lower walls, water streaks and leaves on the roof, one sagging corner with a
// bent pole and a snapped guy. Decals sit 4+ mm proud of their panel, each one higher than the last (no coplanar faces).
import { Asset } from "./kit.mjs";

const D2R = Math.PI / 180, R2D = 180 / Math.PI;
function rnd(seed, k) { const x = Math.sin(seed * 12.9898 + k * 78.233) * 43758.5453; return x - Math.floor(x); }
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = (a) => Math.hypot(a[0], a[1], a[2]);
const nrm = (a) => mul(a, 1 / len(a));
const lerp = (a, b, t) => add(a, mul(sub(b, a), t));

// euler degrees (x, then y, then z: glm::quat(vec3) = qz * qy * qx) for the orthonormal basis whose columns are x, y, z
export function basisEuler(x, y, z) {
  if (Math.abs(x[2]) > 0.99999) {   // gimbal: pitch +-90, put the remaining turn on z
    const b = -Math.asin(Math.sign(x[2]));
    return [0, b * R2D, Math.atan2(-y[0], y[1]) * R2D];
  }
  return [Math.atan2(y[2], z[2]) * R2D, -Math.asin(x[2]) * R2D, Math.atan2(x[1], x[0]) * R2D];
}
// a box whose local x runs along X and local z along Z (Z is orthogonalised against X); returns its frame
function frame(A, c, X, Z, s, m, o = {}) {
  const x = nrm(X), z = nrm(sub(Z, mul(x, dot(Z, x)))), y = cross(z, x);
  A.obox(c, s, basisEuler(x, y, z), m, o);
  return { c, x, y, z, s, n: 0 };
}
// a canvas panel over four corners (p10 - p00 = local x, p01 - p00 = local z), normal turned toward hint
function quad(A, p00, p10, p01, p11, t, m, hint, o = {}, ov = 0.03, push = 0) {
  const c = mul(add(add(p00, p10), add(p01, p11)), 0.25);
  const U = mul(add(sub(p10, p00), sub(p11, p01)), 0.5), Zv = mul(add(sub(p01, p00), sub(p11, p10)), 0.5);
  const x = nrm(U); let z = nrm(sub(Zv, mul(x, dot(Zv, x)))), y = cross(z, x);
  if (dot(y, hint) < 0) { z = mul(z, -1); y = mul(y, -1); }
  const [ou, oz, sh = 0] = Array.isArray(ov) ? ov : [ov, ov];   // overlaps along x / z, and a shift along x
  const cc = add(add(c, mul(y, push)), mul(x, sh)), s = [len(U) + ou, t, len(Zv) + oz];
  A.obox(cc, s, basisEuler(x, y, z), m, o);
  return { c: cc, x, y, z, s, lu: len(U), lz: len(Zv), n: 0, L: [] };
}
// Layer gaps (no depth fighting at any distance): a layer sits >= 10 mm off whatever it covers, 15 mm when it is large
// (> 0.1 m2). Each surface keeps the 2D rectangles laid on it; a new layer goes above every one it overlaps.
const GAP = (area) => (area > 0.1 ? 0.016 : 0.011);   // 1 mm of margin over 15 / 10
function layerAt(list, r, h, gap) {
  let y = gap;
  for (const e of list) if (r[0] < e[1] - 1e-4 && r[1] > e[0] + 1e-4 && r[2] < e[3] - 1e-4 && r[3] > e[2] + 1e-4) y = Math.max(y, e[4] + gap);
  list.push([...r, y + h]);
  return y;
}
// ground layers (above the terrain at y = 0): rectangle x0..x1, z0..z1, thickness h; returns the layer's bottom
function gnd(A, x0, x1, z0, z1, h) { A._gnd = A._gnd || []; return layerAt(A._gnd, [x0, x1, z0, z1], h, GAP((x1 - x0) * (z1 - z0))); }
// a thin decal on a panel frame P at (a along x, b along z), turned rot degrees in the panel plane, no collider
// (kept inside the panel by 4 cm so it never runs under a neighbouring panel's overlap)
function decal(A, P, a, b, su, sv, h, m, rot = 0) {
  const t = rot * D2R, ct = Math.abs(Math.cos(t)), st = Math.abs(Math.sin(t));
  const mu = P.lu / 2 - 0.04, mz = P.lz / 2 - 0.04;
  let eu = (su * ct + sv * st) / 2, ez = (su * st + sv * ct) / 2;
  const k = Math.min(1, mu / eu, mz / ez); su *= k; sv *= k; eu *= k; ez *= k;
  a = Math.max(-mu + eu, Math.min(mu - eu, a)); b = Math.max(-mz + ez, Math.min(mz - ez, b));
  const X = add(mul(P.x, Math.cos(t)), mul(P.z, Math.sin(t))), Z = add(mul(P.x, -Math.sin(t)), mul(P.z, Math.cos(t)));
  const lift = layerAt(P.L, [a - eu, a + eu, b - ez, b + ez], h, GAP(su * sv));
  const c = add(add(P.c, add(mul(P.x, a), mul(P.z, b))), mul(P.y, P.s[1] / 2 + lift + h / 2));
  return frame(A, c, X, Z, [su, h, sv], m, { k: 0 });
}
// a pipe / rope between two points (box rotated onto the segment)
function pipe(A, a, b, w, m, o = {}) {
  const d = sub(b, a), L = len(d);
  const yaw = Math.atan2(d[0], d[2]) * R2D, pitch = -Math.asin(d[1] / L) * R2D;
  return A.obox(mul(add(a, b), 0.5), [w, w, L], [pitch, yaw, 0], m, o);
}
// a flat n-gon on the ground layer (its own height: the next free layer over what it covers)
function gdisc(A, x, z, d, h, m, n = 9, rot = 0, sq = 1) { return disc(A, x, z, d, gnd(A, x - d / 2, x + d / 2, z - d / 2, z + d / 2, h) + h, h, m, n, rot, sq); }
// a flat n-gon lying with its top face at y = top
function disc(A, x, z, d, top, h, m, n = 9, rot = 0, sq = 1) { return A.cone([x, top - h / 2, z], [d, h, d * sq], m, { n, top: 1, k: 0, r: [0, rot, 0] }); }
// convex polygon in (x, y) extruded along z (fan of prisms)
function poly(A, z0, z1, pts, m, o = {}) { for (let i = 1; i + 1 < pts.length; i++) A.prism(2, z0, z1, [pts[0], pts[i], pts[i + 1]], m, o); }

function mats(A) {
  const C = { texture: "canvas_rough" };
  A.mat("canvas", [0.26, 0.27, 0.17], 0.95, "fabric", C);
  A.mat("canvas_b", [0.24, 0.255, 0.155], 0.96, "fabric", C);
  A.mat("canvas_c", [0.29, 0.29, 0.19], 0.95, "fabric", C);   // sun-bleached strip / patch
  A.mat("canvas_dk", [0.17, 0.18, 0.12], 0.97, "fabric", C);  // the flap's inside, the bindings
  A.mat("net", [0.07, 0.075, 0.06], 0.95, "fabric", { texture: "cloth_grey" });
  A.mat("mud", [0.21, 0.17, 0.12], 1.0, "plain", { texture: "dirt_compacted" });
  A.mat("mud_dry", [0.33, 0.28, 0.2], 1.0, "plain", { texture: "dirt_compacted" });
  A.mat("streak", [0.16, 0.165, 0.11], 0.9, "fabric", C);
  A.mat("mould", [0.12, 0.14, 0.08], 1.0, "fabric", C);
  A.mat("leaf", [0.36, 0.24, 0.1], 0.95, "fabric", { texture: "leaf_litter" });
  A.mat("leaf_b", [0.3, 0.27, 0.12], 0.95, "fabric", { texture: "leaf_litter" });
  A.mat("leaf_c", [0.42, 0.3, 0.12], 0.95, "fabric", { texture: "leaf_litter" });
  A.mat("twig", [0.26, 0.2, 0.14], 0.95, "wood", { texture: "bark_fir" });
  A.mat("pole", [0.27, 0.29, 0.21], 0.75, "metal", { texture: "steel_rust_light", metallic: 0.3 });
  A.mat("rust", [0.33, 0.2, 0.11], 0.9, "metal", { texture: "rust_heavy", metallic: 0.15 });
  A.mat("rope", [0.45, 0.41, 0.31], 0.95, "fabric", C);
  A.mat("peg", [0.44, 0.38, 0.3], 0.92, "wood", { texture: "boards_weathered" });
  A.mat("wood", [0.5, 0.42, 0.32], 0.9, "wood", { texture: "boards_weathered" });
  A.mat("crate", [0.42, 0.4, 0.27], 0.9, "wood", { texture: "planks_raw" });
  A.mat("od_paint", [0.25, 0.27, 0.18], 0.7, "metal", { texture: "steel_rust_light", metallic: 0.25 });
  A.mat("steel", [0.42, 0.42, 0.4], 0.6, "metal", { texture: "steel_rust_light", metallic: 0.5 });
  A.mat("black", [0.05, 0.05, 0.055], 0.6, "plain", { texture: "plastic_black" });
  A.mat("fab_od", [0.22, 0.24, 0.16], 0.95, "fabric", { texture: "cloth_grey" });
  A.mat("fab_bag", [0.2, 0.22, 0.13], 0.95, "fabric", { texture: "cloth_grey" });
  A.mat("fab_grey", [0.36, 0.36, 0.34], 0.95, "fabric", { texture: "cloth_grey" });
  A.mat("white", [0.68, 0.67, 0.62], 0.85, "fabric", C);
  A.mat("red", [0.5, 0.08, 0.06], 0.85, "fabric", C);
  A.mat("blood", [0.22, 0.06, 0.04], 0.6, "plain");
  A.mat("paper", [0.72, 0.68, 0.56], 0.9, "plain");
  A.mat("map", [0.6, 0.62, 0.48], 0.9, "plain");
  A.mat("board", [0.78, 0.78, 0.74], 0.35, "plain");
  A.mat("ink", [0.1, 0.12, 0.25], 0.6, "plain");
  A.mat("glass", [0.3, 0.32, 0.3], 0.35, "glass");
  A.mat("plastic", [0.62, 0.64, 0.6], 0.5, "plain");
}

// ------------------------------------------------------------------------------------------------- the tent shell
// T: { W, L, Hw (eave), Hr (ridge), bays, seed, tear: {s, zi, k}, guys: station list }; front door at +z
function shell(A, T) {
  const { W, L, Hw, Hr, bays, seed } = T, hw = W / 2, bl = L / bays, dw = 0.75, dh = 2.1, bw = 0.7, bh = 2.05;
  const S = -1;   // the sagging corner: x < 0 at the back (z = -L/2)
  const bf = (z) => { let t = (z + L / 2) / bl; t = Math.min(Math.max(t, 0), bays); return Math.sin(Math.PI * (t - Math.floor(t))); };
  const dropF = (s, z) => (s === S ? 0.3 * Math.pow(Math.max(0, 1 - (z + L / 2) / (bl * 1.4)), 1.5) : 0);
  const eaveY = (s, z) => Hw - 0.035 * bf(z) - dropF(s, z);
  const ridgeY = (z) => Hr - 0.07 * bf(z) - 0.3 * dropF(S, z);
  const roofPt = (s, u, z) => { const e = eaveY(s, z), r = ridgeY(z), uu = Math.min(Math.max(u, 0), 1); return [s * hw * (1 - u), e + (r - e) * u - 0.075 * Math.sin(Math.PI * uu) * bf(z), z]; };
  const wallPt = (s, v, z) => [s * (hw + 0.045 * Math.sin(Math.PI * v) * bf(z)), v === 0 ? -0.03 : v * eaveY(s, z), z];
  const yTop = (x, z) => { const s = x < 0 ? -1 : 1, e = eaveY(s, z); return e + (ridgeY(z) - e) * (1 - Math.abs(x) / hw); };
  const zs = []; for (let i = 0; i <= bays * 2; i++) zs.push(-L / 2 + i * bl / 2);
  const us = [-0.06, 0.34, 0.68, 1.012];
  const roofP = [], wallP = [];
  let q = 0;
  // roof strips
  for (const s of [-1, 1]) for (let zi = 0; zi + 1 < zs.length; zi++) for (let k = 0; k < 3; k++, q++) {
    const z0 = zs[zi], z1 = zs[zi + 1];
    const c = [roofPt(s, us[k], z0), roofPt(s, us[k + 1], z0), roofPt(s, us[k], z1), roofPt(s, us[k + 1], z1)];
    if (T.tear && T.tear.s === s && T.tear.zi === zi && T.tear.k === k) { tornHole(A, c, s, seed); continue; }
    const m = rnd(seed, q) < 0.18 ? "canvas_c" : rnd(seed, q + 300) < 0.5 ? "canvas" : "canvas_b";
    const par = (zi + k) % 2;   // checkerboard: neighbours 20 mm apart along the normal, their end faces 20-30 mm apart
    roofP.push({ P: quad(A, c[0], c[1], c[2], c[3], 0.02, m, [s * 0.5, 1, 0], {}, par ? [0.09, 0.07] : 0.03, par ? 0.01 : -0.01), s, zi, k });
  }
  // side walls (two strips high, bowed out between poles)
  for (const s of [-1, 1]) for (let zi = 0; zi + 1 < zs.length; zi++) for (let k = 0; k < 2; k++, q++) {
    const z0 = zs[zi], z1 = zs[zi + 1], v0 = k * 0.5, v1 = k + 0.5 > 1 ? 1 : 0.5;
    const par = (zi + k) % 2;
    const et = par ? 0 : 0.03, eb = par ? 0.08 : 0.01;   // top ends 30 mm apart (the top one just under the roof skin), bottoms 70 mm
    const P = quad(A, wallPt(s, v0, z0), wallPt(s, v1, z0), wallPt(s, v0, z1), wallPt(s, v1, z1), 0.02, rnd(seed, q) < 0.5 ? "canvas" : "canvas_b", [s, 0, 0], {}, [et + eb, par ? 0.07 : 0.03, (et - eb) / 2], par ? 0.01 : -0.01);
    wallP.push({ P, s, zi, k });
  }
  // sod cloth lying out on the ground along each wall
  for (const s of [-1, 1]) for (let b = 0; b < bays; b++) {
    const x0 = s * (hw + 0.012), x1 = s * (hw + 0.2 + rnd(seed, b + s * 9) * 0.08), za = -L / 2 + b * bl + 0.02, zb2 = -L / 2 + (b + 1) * bl - 0.02;
    const y0 = gnd(A, Math.min(x0, x1), Math.max(x0, x1), za, zb2, 0.008);
    A.box([Math.min(x0, x1), y0, za], [Math.max(x0, x1), y0 + 0.008, zb2], "canvas_dk", { k: 0 });
  }
  // gable ends
  const zf = L / 2, zb = -L / 2, t2 = 0.01;
  const ef = (s) => eaveY(s, zf), eb = (s) => eaveY(s, zb);
  poly(A, zf - t2, zf + t2, [[-hw, -0.03], [-dw, -0.03], [-dw, yTop(-dw, zf)], [-hw, ef(-1)]], "canvas");
  poly(A, zf - t2, zf + t2, [[dw, -0.03], [hw, -0.03], [hw, ef(1)], [dw, yTop(dw, zf)]], "canvas");
  poly(A, zf - t2, zf + t2, [[-dw, dh], [dw, dh], [dw, yTop(dw, zf)], [0, ridgeY(zf)], [-dw, yTop(-dw, zf)]], "canvas");
  poly(A, zb - t2, zb + t2, [[-hw, -0.03], [-bw, -0.03], [-bw, yTop(-bw, zb)], [-hw, eb(-1)]], "canvas_b");
  poly(A, zb - t2, zb + t2, [[bw, -0.03], [hw, -0.03], [hw, eb(1)], [bw, yTop(bw, zb)]], "canvas_b");
  poly(A, zb - t2, zb + t2, [[-bw, bh], [bw, bh], [bw, yTop(bw, zb)], [0, ridgeY(zb)], [-bw, yTop(-bw, zb)]], "canvas_b");
  // front door: the flap rolled up over the opening, two ties, dark bindings down the jambs
  A.cyl([0, dh + 0.11, zf + 0.12], [0.22, 1.62, 0.22], "canvas_b", { k: 0, r: [0, 0, 90] });
  A.cyl([0.05, dh + 0.1, zf + 0.13], [0.16, 1.5, 0.19], "canvas_dk", { k: 0, r: [0, 0, 90] });   // the inner turn of the roll
  for (const x of [-0.45, 0.45]) {
    A.box([x - 0.02, dh - 0.12, zf + 0.242], [x + 0.02, dh + 0.24, zf + 0.25], "canvas_dk", { k: 0 });
    pipe(A, [x, dh - 0.12, zf + 0.24], [x + 0.06, dh - 0.42, zf + 0.21], 0.012, "rope", { k: 0 });   // dangling tie end
  }
  for (const x of [-dw - 0.03, dw + 0.03]) A.box([x - 0.03, -0.02, zf + t2 + 0.010], [x + 0.03, dh + 0.02, zf + t2 + 0.015], "canvas_dk", { k: 0 });
  A.box([-dw - 0.06, dh - 0.03, zf + t2 + 0.025], [dw + 0.06, dh + 0.01, zf + t2 + 0.03], "canvas_dk", { k: 0 });
  // back door: mesh liner (collides) behind one closed flap and one tied half open
  A.box([-bw, -0.03, zb + 0.03], [bw, bh + 0.02, zb + 0.045], "net");
  A.box([-bw - 0.04, -0.01, zb - 0.046], [0.03, bh, zb - 0.026], "canvas_b", { k: 0 });
  A.prism(2, zb - 0.046, zb - 0.026, [[bw + 0.04, -0.01], [bw + 0.04, bh], [0.04, bh]], "canvas", { k: 0 });
  A.prism(2, zb - 0.072, zb - 0.056, [[bw + 0.04, 0.35], [bw + 0.04, 1.2], [0.5, 0.85]], "canvas_dk", { k: 0 });   // the folded-back corner
  A.cyl([bw + 0.1, 0.9, zb - 0.1], [0.11, 0.6, 0.11], "canvas_b", { k: 0, r: [0, 0, 12] });
  pipe(A, [bw + 0.02, 0.95, zb - 0.13], [bw + 0.2, 0.92, zb - 0.13], 0.014, "rope", { k: 0 });
  pipe(A, [bw + 0.2, 0.92, zb - 0.13], [bw + 0.26, 0.55, zb - 0.12], 0.012, "rope", { k: 0 });

  // frame: uprights, rafters, eave pipes (inside the canvas, no colliders)
  for (let j = 0; j <= bays; j++) {
    const z = Math.min(Math.max(-L / 2 + j * bl, -L / 2 + 0.065), L / 2 - 0.065);
    for (const s of [-1, 1]) {
      const top = [s * (hw - 0.06), eaveY(s, z) - 0.06, z], foot = [s * (hw - 0.06), 0.011, z];
      if (s === S && j === 0) {   // the bent corner pole
        const kink = [s * (hw - 0.2), 0.95, z + 0.06];
        pipe(A, [s * (hw - 0.07), 0.011, z + 0.02], kink, 0.045, "pole", { k: 0 });
        pipe(A, kink, top, 0.045, "pole", { k: 0 });
        A.box([kink[0] - 0.03, kink[1] - 0.06, kink[2] - 0.03], [kink[0] + 0.03, kink[1] + 0.06, kink[2] + 0.03], "rust", { k: 0 });
      } else pipe(A, foot, top, 0.045, "pole", { k: 0 });
      A.box([foot[0] - 0.045, 0, z - 0.045], [foot[0] + 0.045, 0.012, z + 0.045], "rust", { k: 0 });
      pipe(A, top, [0, ridgeY(z) - 0.06, z], 0.04, "pole", { k: 0 });
      if (j < bays) {
        const z2 = Math.min(-L / 2 + (j + 1) * bl, L / 2 - 0.065);
        pipe(A, [s * (hw - 0.06), eaveY(s, z) - 0.09, z], [s * (hw - 0.06), eaveY(s, z2) - 0.09, z2], 0.035, "pole", { k: 0 });
      }
    }
    A.box([-0.07, ridgeY(z) - 0.1, z - 0.035], [0.07, ridgeY(z) - 0.03, z + 0.035], "rust", { k: 0 });   // ridge fitting
  }

  // guy ropes to pegs: 3 per side; the one at the sagging corner snapped, its peg pulled out
  const st = [0, Math.floor(bays / 2), bays];
  st.forEach((j, n) => {
    for (const s of [-1, 1]) {
      const z = Math.min(Math.max(-L / 2 + j * bl, -L / 2 + 0.05), L / 2 - 0.05), zo = j === 0 ? -0.5 : j === bays ? 0.5 : 0;
      const a = roofPt(s, -0.06, z), px = s * (hw + 1.45 + rnd(seed, n + s) * 0.15), pz = z + zo;
      if (s === S && j === 0) {
        const lo = [s * (hw + 0.25), 0.02, z - 0.25];
        pipe(A, a, lo, 0.015, "rope", { k: 0 });
        pipe(A, lo, [s * (hw + 0.95), 0.01, z - 0.55], 0.015, "rope", { k: 0 });
        A.obox([s * (hw + 1.0), 0.02, z - 0.6], [0.04, 0.35, 0.025], [0, 20, 90], "peg", { k: 0 });
        continue;
      }
      const b = [px - s * 0.05, 0.14, pz];
      pipe(A, a, b, 0.015, "rope", { k: 0 });
      pipe(A, lerp(a, b, 0.69), lerp(a, b, 0.75), 0.04, "wood", { k: 0 });   // the tensioner
      if ((n + (s > 0 ? 1 : 0)) % 2) A.obox([px, 0.11, pz], [0.045, 0.32, 0.028], [0, 0, -s * 20], "peg", { k: 0 });
      else A.cyl([px, 0.11, pz], [0.022, 0.3, 0.022], "rust", { k: 0, r: [0, 0, -s * 18] });
    }
  });
  // a back ridge guy
  pipe(A, [0, ridgeY(zb) + 0.02, zb - 0.02], [0.1, 0.14, zb - 1.4], 0.015, "rope", { k: 0 });
  A.obox([0.11, 0.11, zb - 1.46], [0.045, 0.32, 0.028], [-20, 0, 0], "peg", { k: 0 });

  // wear: mud on the lower walls, splashes higher up
  for (const { P, k } of wallP) {
    if (k !== 0) continue;
    const r1 = rnd(seed, q++), r2 = rnd(seed, q++), r3 = rnd(seed, q++);
    const hgt = 0.14 + r1 * 0.28, wid = Math.min(P.lz * 0.95, 0.6 + r2 * 0.7);
    decal(A, P, -P.lu / 2 + hgt / 2 + 0.03, (r3 - 0.5) * (P.lz - wid), hgt, wid, 0.003, r1 < 0.5 ? "mud" : "mud_dry");
    if (r2 < 0.55) decal(A, P, -P.lu / 2 + hgt + 0.08 + r3 * 0.2, (r1 - 0.5) * P.lz * 0.6, 0.06 + r2 * 0.08, 0.15 + r3 * 0.2, 0.003, "mud", (r1 - 0.5) * 40);
    if (r3 < 0.3) decal(A, P, (r2 - 0.5) * 0.3, (r1 - 0.5) * P.lz * 0.5, 0.25, 0.4, 0.003, "mould", 10);
  }
  for (const [x0, x1, z] of [[-hw + 0.05, -dw - 0.1, zf + t2 + 0.015], [dw + 0.1, hw - 0.05, zf + t2 + 0.015], [-hw + 0.05, -bw - 0.1, zb - t2 - 0.018], [bw + 0.1, hw - 0.05, zb - t2 - 0.018]]) {
    const h = 0.18 + rnd(seed, q++) * 0.2;
    A.box([x0, -0.01, z], [x1, h, z + 0.003], "mud", { k: 0 });   // 15 mm off the gable
  }
  gdisc(A, 0, zf + 0.35, 1.5, 0.006, "mud", 10, 15, 0.7);   // the trodden doorway
  gdisc(A, 0.1, zf - 0.6, 1.1, 0.006, "mud_dry", 9, 40, 0.8);
  // roof: water streaks, a bleached patch, leaves (thicker in the dips and at the eaves), a fallen twig
  for (let i = 0; i < 7; i++) {
    const R = roofP[Math.floor(rnd(seed, q++) * roofP.length)].P;
    decal(A, R, (rnd(seed, q++) - 0.5) * R.lu * 0.5, (rnd(seed, q++) - 0.5) * R.lz * 0.6, R.lu * 0.85, 0.12 + rnd(seed, q++) * 0.25, 0.003, "streak", (rnd(seed, q++) - 0.5) * 10);
  }
  const lm = ["leaf", "leaf_b", "leaf_c"];
  const nLeaves = Math.round(4 * L + 30);
  for (let i = 0; i < nLeaves; i++) {
    const pick = rnd(seed, q++), lowStrip = pick < 0.45;
    const cands = roofP.filter((r) => (lowStrip ? r.k === 0 : r.zi % 2 === (i % 2)));
    const R = cands[Math.floor(rnd(seed, q++) * cands.length)].P;
    if (R.n > 7) continue;
    const sz = 0.06 + rnd(seed, q++) * 0.08;
    decal(A, R, (rnd(seed, q++) - 0.5) * R.lu * 0.9, (rnd(seed, q++) - 0.5) * R.lz * 0.9, sz, sz * (0.7 + rnd(seed, q++) * 0.6), 0.003, lm[i % 3], (rnd(seed, q++) - 0.5) * 120);
  }
  {
    const R = roofP.find((r) => r.s === 1 && r.k === 1 && r.zi === 2).P;
    const t = 30 * D2R, X = add(mul(R.x, Math.cos(t)), mul(R.z, Math.sin(t)));
    const c = add(R.c, mul(R.y, R.s[1] / 2 + 0.03));
    pipe(A, add(c, mul(X, -0.55)), add(c, mul(X, 0.55)), 0.035, "twig", { k: 0 });
    pipe(A, add(c, mul(X, 0.1)), add(add(c, mul(X, 0.35)), mul(R.z, 0.25)), 0.02, "twig", { k: 0 });
  }
  // leaf litter drifted against the walls and blown in at the door
  for (let i = 0; i < 12; i++) {
    const s = i % 2 ? 1 : -1, z = -L / 2 + rnd(seed, q++) * L, d = 0.3 + rnd(seed, q++) * 0.5;
    gdisc(A, s * (hw + 0.25 + rnd(seed, q++) * 0.3), z, d, 0.01, lm[i % 3], 7, rnd(seed, q++) * 90, 0.6);
  }
  for (let i = 0; i < 4; i++) gdisc(A, (rnd(seed, q++) - 0.5) * 1.2, zf - 0.4 - rnd(seed, q++) * 1.2, 0.25 + rnd(seed, q++) * 0.3, 0.008, lm[i % 3], 7, rnd(seed, q++) * 90, 0.7);

  return { hw, L, bl, roofP, wallP, eaveY, ridgeY, zf, zb, dw, dh, bw, bh };
}

// the missing roof strip: ragged shreds round the hole and a flap hanging in
function tornHole(A, c, s, seed) {
  const [p00, p10, p01, p11] = c, hint = [s * 0.5, 1, 0];
  for (let i = 0; i < 4; i++) {   // shreds along the lower and upper edges
    const t = 0.15 + i * 0.23;
    for (const [e0, e1, into] of [[p00, p01, 1], [p10, p11, -1]]) {
      const a = lerp(e0, e1, t), b = lerp(e0, e1, t + 0.1), dep = 0.06 + rnd(seed, i * 7 + into) * 0.14;
      const toward = mul(sub(into > 0 ? lerp(p10, p11, t) : lerp(p00, p01, t), a), dep / len(sub(p10, p00)));
      quad(A, a, add(add(a, toward), [0, -0.02, 0.02]), b, add(add(b, mul(toward, 0.5)), [0, -0.03, 0]), 0.012, "canvas_b", hint, { k: 0 }, 0.0);
    }
  }
  for (const [e0, e1] of [[p00, p10], [p01, p11]]) {   // shreds at the ends of the hole
    const a = lerp(e0, e1, 0.3), b = lerp(e0, e1, 0.6), into = mul(nrm(sub(p01 === e0 ? p00 : p01, e0)), 0.09);
    quad(A, a, b, add(a, into), add(b, mul(into, 1.6)), 0.012, "canvas", hint, { k: 0 }, 0.0);
  }
  const a = lerp(p10, p11, 0.25), b = lerp(p10, p11, 0.7);   // the long flap hanging into the tent from the upper edge
  quad(A, a, add(a, [-s * 0.12, -0.62, 0.05]), b, add(b, [-s * 0.18, -0.5, -0.04]), 0.012, "canvas_b", [s, 0.3, 0], { k: 0 }, 0.0);
}

// ------------------------------------------------------------------------------------------------- prop kit
// a prop transform: origin o, orthonormal axes X, Y, Z (aa: axis-aligned, so boxes stay plain boxes)
function TY(x, z, yaw = 0, y = 0) {
  const t = yaw * D2R, aa = Math.abs(yaw % 90) < 1e-9, r = (v) => (aa ? Math.round(v) : v);
  return { o: [x, y, z], X: [r(Math.cos(t)), 0, r(-Math.sin(t))], Y: [0, 1, 0], Z: [r(Math.sin(t)), 0, r(Math.cos(t))], aa };
}
function TT(o, X, Y) { return { o, X: nrm(X), Y: nrm(Y), Z: cross(nrm(X), nrm(Y)), aa: false }; }
const tp = (T, l) => add(T.o, add(add(mul(T.X, l[0]), mul(T.Y, l[1])), mul(T.Z, l[2])));
const tv = (T, l) => add(add(mul(T.X, l[0]), mul(T.Y, l[1])), mul(T.Z, l[2]));
function PB(A, T, mn, mx, m, o = {}) {
  if (T.aa) { const a = tp(T, mn), b = tp(T, mx); return A.box([0, 1, 2].map((i) => Math.min(a[i], b[i])), [0, 1, 2].map((i) => Math.max(a[i], b[i])), m, o); }
  return frame(A, tp(T, mul(add(mn, mx), 0.5)), T.X, T.Z, [0, 1, 2].map((i) => mx[i] - mn[i]), m, o);
}
function PR(A, T, mn, mx, rad, m, o = {}) {   // rounded box (aa transforms only; else a bevelled box)
  if (T.aa) { const a = tp(T, mn), b = tp(T, mx); return A.rbox([0, 1, 2].map((i) => Math.min(a[i], b[i])), [0, 1, 2].map((i) => Math.max(a[i], b[i])), rad, m, o); }
  return PB(A, T, mn, mx, m, { ...o, bv: rad });
}
// oriented box in prop space: centre c, local axes lx, lz (prop space)
function PO(A, T, c, lx, lz, s, m, o = {}) { return frame(A, tp(T, c), tv(T, lx), tv(T, lz), s, m, o); }
function PP(A, T, a, b, w, m, o = {}) { return pipe(A, tp(T, a), tp(T, b), w, m, o); }
// cylinder (or cone with o.top) in prop space along local axis ax ("x" | "y" | "z")
function PC(A, T, c, d, h, m, o = {}, ax = "y") {
  const Y = ax === "x" ? T.X : ax === "z" ? T.Z : T.Y, X = ax === "y" ? T.X : T.Y, Z = cross(X, Y);
  const plain = T.aa && ax === "y";
  const opt = { ...o }; if (!plain) opt.r = basisEuler(X, Y, Z);
  return o.top !== undefined ? A.cone(tp(T, c), [d, h, d], m, opt) : A.cyl(tp(T, c), [d, h, d], m, opt);
}
// invisible collider box (world space)
const COL = (A, mn, mx) => A.box(mn, mx, "wood", { v: 0 });

// folding cot, long axis local x (1.8 m), head at -x. v: "roll" | "bag" | "torn"
function cot(A, T, v, seed) {
  A.beginFurniture("Folding cot");
  // the fabric's edges are buried in the rails and end bars (no near-coplanar faces at the seams)
  for (const z of [-0.31, 0.31]) PB(A, T, [-0.875, 0.385, z - 0.0175], [0.875, 0.42, z + 0.0175], "od_paint");
  for (const x of [-0.88, 0.88]) PB(A, T, [x - 0.015, 0.38, -0.315], [x + 0.015, 0.415, 0.315], "od_paint");
  if (v === "torn") {
    PB(A, T, [-0.862, 0.395, -0.3], [-0.1, 0.407, 0.3], "fab_od");
    PB(A, T, [0.25, 0.395, -0.3], [0.862, 0.407, 0.3], "fab_od");
    PO(A, T, [0.07, 0.3, 0.1], [0.5, -0.86, 0], [0, 0, 1], [0.3, 0.01, 0.34], "fab_od");   // the torn middle hanging down
  } else PB(A, T, [-0.862, 0.395, -0.3], [0.862, 0.407, 0.3], "fab_od");
  for (const x of [-0.74, 0, 0.74]) {
    PP(A, T, [x, 0.01, -0.3], [x, 0.38, 0.29], 0.022, "od_paint");
    PP(A, T, [x + 0.035, 0.01, 0.3], [x + 0.035, 0.38, -0.29], 0.022, "od_paint");   // 13 mm clear of its partner
  }
  if (v === "roll") {
    PC(A, T, [-0.68, 0.517, 0], 0.22, 0.56, "fab_bag", {}, "z");
    for (const z of [-0.17, 0.17]) PC(A, T, [-0.68, 0.517, z], 0.228, 0.03, "black", {}, "z");
  } else if (v === "bag") {
    PR(A, T, [-0.84, 0.4105, -0.27], [0.78, 0.47, 0.26], 0.03, "fab_bag");   // resting things sink a few mm into what they rest on
    PO(A, T, [0.6, 0.49, 0.05], [1, 0.18, 0], [0, 0, 1], [0.38, 0.05, 0.5], "fab_bag");   // the turned-back top
    PR(A, T, [-0.83, 0.464, -0.2], [-0.55, 0.53, 0.18], 0.025, "fab_grey");
  }
  PB(A, T, [-0.92, 0, -0.34], [0.92, 0.5, 0.34], "od_paint", { v: 0 });   // one collider (the X legs would inflate the auto box)
  A.endFurniture(false);
}
function footlocker(A, T, open) {
  A.beginFurniture("Footlocker");
  if (!open) {
    PB(A, T, [-0.4, 0, -0.2], [0.4, 0.33, 0.2], "od_paint", { bv: 0.01 });
    PB(A, T, [-0.41, 0.33, -0.21], [0.41, 0.37, 0.21], "od_paint", { bv: 0.008 });
    PB(A, T, [-0.04, 0.24, 0.195], [0.04, 0.318, 0.215], "steel");
  } else {
    PB(A, T, [-0.4, 0, -0.2], [0.4, 0.03, 0.2], "od_paint");
    for (const z of [-0.2, 0.2]) PB(A, T, [-0.38, 0.03, z - 0.01 * Math.sign(z) - 0.005], [0.38, 0.33, z - 0.01 * Math.sign(z) + 0.005], "od_paint");
    for (const x of [-0.39, 0.39]) PB(A, T, [x - 0.01, 0.03, -0.19], [x + 0.01, 0.33, 0.19], "od_paint");
    PO(A, T, [0, 0.52, -0.25], [1, 0, 0], [0, 0.26, 0.966], [0.82, 0.42, 0.04], "od_paint");   // the lid thrown back
    PR(A, T, [-0.35, 0.025, -0.17], [0.05, 0.22, 0.17], 0.03, "fab_od");   // folded clothes
    PO(A, T, [0.35, 0.36, 0.02], [0.9, 0.42, 0], [0, 0, 1], [0.35, 0.03, 0.3], "fab_grey");   // a shirt over the side
  }
  for (const x of [-0.405, 0.405]) PB(A, T, [x - 0.01, 0.2, -0.06], [x + 0.01, 0.23, 0.06], "black");
  const fz = open ? 0.195 : 0.2;
  PB(A, T, [0.18, 0.12, fz + 0.001], [0.34, 0.2, fz + 0.016], "white");   // stencil plate, 16 mm proud
  A.endFurniture();
}
function lantern(A, x, y, z) {
  A.beginFurniture("Lantern on a hook");
  A.box([x - 0.006, y + 0.2, z - 0.006], [x + 0.006, y + 0.5, z + 0.006], "steel");
  A.obox([x, y + 0.255, z], [0.05, 0.006, 0.006], [0, 0, 90], "steel");
  A.cyl([x, y + 0.015, z], [0.14, 0.03, 0.14], "od_paint");
  A.cyl([x, y + 0.1025, z], [0.11, 0.155, 0.11], "glass");   // its ends buried in the base and the hood
  A.cone([x, y + 0.21, z], [0.15, 0.07, 0.15], "od_paint", { top: 0.3 });
  A.box([x - 0.06, y + 0.25, z - 0.004], [x + 0.06, y + 0.26, z + 0.004], "steel");
  A.endFurniture(false);
}
function clutter(A, items, seed) {
  A.beginFurniture("Clutter");
  for (const it of items) {
    const [kind, x, z, yaw] = it, T = TY(x, z, yaw);
    if (kind === "boots") for (const dz of [-0.08, 0.08]) {
      PR(A, TY(x, z + dz, yaw + dz * 100), [-0.15, 0, -0.05], [0.15, 0.04, 0.05], 0.015, "black");
      PR(A, TY(x, z + dz, yaw + dz * 100), [-0.13, 0.04, -0.045], [0.0, 0.24, 0.045], 0.03, "black");
    }
    else if (kind === "helmet") { PC(A, T, [0, 0.06, 0], 0.27, 0.12, "od_paint", { top: 0.72 }); PC(A, T, [0, 0.135, 0], 0.19, 0.03, "od_paint", { top: 0.5 }); }
    else if (kind === "canteen") PC(A, TT([x, 0.075, z], [0, 1, 0], [Math.cos(yaw * D2R), 0, Math.sin(yaw * D2R)]), [0, 0, 0], 0.15, 0.09, "od_paint", {}, "y");
    else if (kind === "paper") { const y = gnd(A, x - 0.19, x + 0.19, z - 0.19, z + 0.19, 0.002); PB(A, T, [-0.105, y, -0.15], [0.105, y + 0.002, 0.15], "paper"); }
    else if (kind === "bag") { const y = gnd(A, x - 0.61, x + 0.61, z - 0.61, z + 0.61, 0.07); PR(A, T, [-0.5, y, -0.35], [0.5, y + 0.07, 0.35], 0.03, "fab_bag"); }
    else if (kind === "can") PC(A, TT([x, 0.035, z], [0, 1, 0], [Math.cos(yaw * D2R), 0, Math.sin(yaw * D2R)]), [0, 0, 0], 0.07, 0.11, "steel", {}, "y");
    else if (kind === "bandage") { const y = gnd(A, x - 0.26, x + 0.26, z - 0.26, z + 0.26, 0.003); PB(A, T, [-0.04, y, -0.25], [0.04, y + 0.003, 0.25], "white"); }
  }
  A.endFurniture(false);
}

function folTable(A, T, w = 0.76, l = 1.8, h = 0.75) {
  PR(A, T, [-l / 2, h - 0.03, -w / 2], [l / 2, h, w / 2], 0.01, "wood");
  PB(A, T, [-l / 2 + 0.03, h - 0.07, -w / 2 + 0.02], [l / 2 - 0.03, h - 0.025, -w / 2 + 0.05], "od_paint");
  PB(A, T, [-l / 2 + 0.03, h - 0.07, w / 2 - 0.05], [l / 2 - 0.03, h - 0.025, w / 2 - 0.02], "od_paint");
  for (const x of [-l / 2 + 0.12, l / 2 - 0.12]) {
    for (const z of [-w / 2 + 0.06, w / 2 - 0.06]) PB(A, T, [x - 0.015, 0, z - 0.015], [x + 0.015, h - 0.07, z + 0.015], "od_paint");
  }
  PB(A, T, [-l / 2 + 0.135, 0.12, -0.012], [l / 2 - 0.135, 0.145, 0.012], "od_paint");   // the leg stretcher
}
function bench(A, T) {
  PR(A, T, [-0.9, 0.42, -0.14], [0.9, 0.45, 0.14], 0.008, "wood");
  for (const x of [-0.75, 0.75]) {
    for (const z of [-0.11, 0.11]) PB(A, T, [x - 0.015, 0, z - 0.015], [x + 0.015, 0.425, z + 0.015], "od_paint");
  }
}
function chair(A, T, col = true) {
  PB(A, T, [-0.21, 0.44, -0.2], [0.21, 0.465, 0.2], "od_paint");
  PO(A, T, [0, 0.76, -0.235], [1, 0, 0], [0, 0.17, 0.985], [0.4, 0.26, 0.022], "od_paint");
  for (const x of [-0.22, 0.22]) {
    PP(A, T, [x, 0, -0.27], [x, 0.9, -0.215], 0.022, "steel");
    PP(A, T, [x * 0.97, 0, 0.21], [x * 0.97, 0.45, -0.05], 0.02, "steel");
  }
  if (col) PB(A, T, [-0.24, 0, -0.29], [0.24, 0.9, 0.23], "steel", { v: 0 });
}
function crate(A, T, sx, sy, sz, lidOff = 0) {
  PB(A, T, [-sx / 2, 0, -sz / 2], [sx / 2, sy, sz / 2], "crate", { bv: 0.01 });
  for (const x of [-sx / 2 + 0.05, sx / 2 - 0.05]) PB(A, T, [x - 0.03, 0.01, -sz / 2 - 0.012], [x + 0.03, sy - 0.01, -sz / 2 + 0.004], "wood");
  for (const x of [-sx / 2 + 0.05, sx / 2 - 0.05]) PB(A, T, [x - 0.03, 0.01, sz / 2 - 0.004], [x + 0.03, sy - 0.01, sz / 2 + 0.012], "wood");
  PB(A, T, [-sx / 2 + 0.12, sy * 0.4, sz / 2 - 0.003], [sx / 2 - 0.12, sy * 0.6, sz / 2 + 0.012], "black");   // stencil
  if (lidOff) PO(A, T, [lidOff, sy + 0.03, 0.05], [1, 0, 0.2], [0, 0.08, 1], [sx * 0.95, 0.025, sz * 0.95], "crate");
}
function jerrycan(A, T, m = "od_paint") {
  PB(A, T, [-0.0825, 0, -0.1725], [0.0825, 0.43, 0.1725], m, { bv: 0.015 });
  for (const sgn of [-1, 1]) {
    PO(A, T, [sgn * 0.0855, 0.2, 0], [0, 0.77, 0.64], [0, -0.64, 0.77], [0.36, 0.006, 0.03], m, { k: 0 });   // the pressed X
    PO(A, T, [sgn * 0.0855 + sgn * 0.0045, 0.2, 0], [0, 0.77, -0.64], [0, 0.64, 0.77], [0.36, 0.006, 0.03], m, { k: 0 });
  }
  for (const z of [-0.09, 0, 0.09]) PB(A, T, [-0.012, 0.43, z - 0.012], [0.012, 0.47, z + 0.012], m);
  PB(A, T, [-0.012, 0.47, -0.11], [0.012, 0.49, 0.11], m);
  PC(A, T, [0, 0.455, 0.14], 0.05, 0.06, "rust", {}, "y");
}

// ------------------------------------------------------------------------------------------------- the four tents
function mk(id, name, description, W, L) {
  const A = new Asset({ id, version: 1, name, category: "Modern: Military", description, footprint: [W, L], floors: 1, enterable: true,
    plinthDepth: 0, maxGrade: 0.5, noAutoPanels: true });
  mats(A);
  return A;
}
function nav(A, sh, W) {
  const hw = W / 2;
  A.meta.nav = { effect: "carve", rooms: [{ id: "inside", min: [-hw + 0.1, 0, -sh.L / 2 + 0.1], max: [hw - 0.1, 1.7, sh.L / 2 - 0.1] }],
    entrances: [{ p: [0, 0, sh.L / 2 + 0.6], dir: [0, 0, -1], room: "inside" }], doors: [], corners: [], firingPoints: [], cover: [], areas: [] };
  // footprint: the ground extent of everything (guy pegs, the mast), symmetric about the origin
  let mx = 0, mz = 0;
  for (const p of A.parts) {
    if (!p.c) continue;
    const [a, b, g] = (p.r || [0, 0, 0]).map((d) => d * D2R), [ca, sa, cb, sb, cg, sg] = [Math.cos(a), Math.sin(a), Math.cos(b), Math.sin(b), Math.cos(g), Math.sin(g)];
    const M = [[cg * cb, cg * sb * sa - sg * ca, cg * sb * ca + sg * sa], [sg * cb, sg * sb * sa + cg * ca, sg * sb * ca - cg * sa], [-sb, cb * sa, cb * ca]];   // Rz Ry Rx
    const e = (i) => M[i].reduce((t, m, j) => t + Math.abs(m) * p.s[j] / 2, 0);
    mx = Math.max(mx, Math.abs(p.c[0]) + e(0)); mz = Math.max(mz, Math.abs(p.c[2]) + e(2));
  }
  A.meta.footprint = [Math.ceil(mx * 20) / 10, Math.ceil(mz * 20) / 10];
}

function sleeping() {
  const W = 5, L = 8;
  const A = mk("mod_tent_sleeping", "Sleeping tent (abandoned)",
    "An abandoned olive-canvas sleeping tent, 5 x 8 m on a steel frame, with a row of folding cots and rolled sleeping bags, footlockers and a lantern on a hook; torn, muddy and leaf-strewn, one corner sagging.", W, L);
  const sh = shell(A, { W, L, Hw: 1.75, Hr: 3.0, bays: 4, seed: 11, tear: { s: 1, zi: 3, k: 1 } });
  const vs = ["roll", "bag", "roll", "roll", "bag", "roll", "torn", "roll"];
  [-3.0, -1.6, -0.2, 1.2].forEach((z, i) => {
    cot(A, TY(-1.43, z, 0), vs[i], i);   // head at the wall (-x)
    cot(A, TY(1.43, z, 180), vs[i + 4], i + 4);
  });
  [-2.3, -0.9, 0.5].forEach((z, i) => {
    footlocker(A, TY(-2.0, z, 0), i === 1);
    footlocker(A, TY(2.0, z, 0), false);
  });
  footlocker(A, TY(1.95, 2.3, 0), true);
  lantern(A, 0.0, sh.ridgeY(0) - 0.62, 0.0);
  clutter(A, [["boots", -0.3, 2.4, 20], ["helmet", 0.35, -2.4, 0], ["canteen", -0.25, -0.9, 40], ["paper", 0.2, 0.6, 30], ["paper", 0.3, 0.8, 75],
    ["paper", -0.15, 2.0, 10], ["bag", 0.0, 2.9, 25], ["can", 0.4, 1.6, 120]]);
  nav(A, sh, W);
  return A;
}

function mess() {
  const W = 6, L = 10;
  const A = mk("mod_tent_mess", "Mess tent (abandoned)",
    "An abandoned olive-canvas mess tent, 6 x 10 m on a steel frame, with folding tables and benches, a field kitchen stove, water jerrycans and ration crates; torn, muddy and leaf-strewn, one corner sagging.", W, L);
  const sh = shell(A, { W, L, Hw: 1.8, Hr: 3.15, bays: 4, seed: 23, tear: { s: -1, zi: 4, k: 1 } });
  let tb = 0;
  for (const x of [-1.6, 1.6]) for (const z of [-1.6, 1.0, 3.3]) {
    if (z === 3.3 && x > 0) continue;   // the front-right corner kept clear for the crates
    A.beginFurniture("Mess table");
    const T = TY(x, z, 90);
    folTable(A, T);
    for (let i = 0; i < 2; i++) PB(A, TY(x + (rnd(tb, i) - 0.5) * 0.4, z + (i - 1) * 0.55, 90 * (i % 2)), [-0.16, 0.746, -0.12], [0.16, 0.762, 0.12], "steel");   // mess trays (sunk into the top, 12 mm proud)
    PC(A, TY(x + 0.2, z + 0.25), [0, 0.798, 0], 0.08, 0.1, "steel");
    A.endFurniture();
    for (const dx of [-0.62, 0.62]) {
      if (tb === 1 && dx > 0) {   // a bench kicked over onto its side
        A.beginFurniture("Bench (overturned)");
        bench(A, TT([x + dx + 0.1, 0.14, z], [0, 0, 1], [1, 0, 0]));
        COL(A, [x + dx - 0.35, 0, z - 0.92], [x + dx + 0.55, 0.3, z + 0.92]);
        A.endFurniture(false);
      } else { A.beginFurniture("Bench"); bench(A, TY(x + dx, z, 90)); A.endFurniture(); }
    }
    tb++;
  }
  // field kitchen: stove with flue, prep table, pots
  A.beginFurniture("Field kitchen stove");
  {
    const T = TY(-1.7, -4.15, 0);
    PB(A, T, [-0.5, 0.12, -0.38], [0.5, 0.865, 0.38], "od_paint", { bv: 0.012 });
    PB(A, T, [-0.52, 0.86, -0.4], [0.52, 0.89, 0.4], "steel");
    for (const x of [-0.25, 0.25]) PC(A, T, [x, 0.8955, 0.05], 0.3, 0.013, "rust");
    PB(A, T, [-0.4, 0.25, 0.375], [0.4, 0.7, 0.397], "rust");
    PB(A, T, [-0.3, 0.62, 0.388], [0.3, 0.645, 0.425], "steel");
    for (const x of [-0.42, 0.42]) for (const z of [-0.32, 0.32]) PB(A, T, [x - 0.025, 0, z - 0.025], [x + 0.025, 0.12, z + 0.025], "od_paint");
    PC(A, T, [-0.25, 1.015, 0.05], 0.36, 0.28, "steel");   // pot and pan sunk into the top plate
    PC(A, T, [-0.25, 1.155, 0.05], 0.37, 0.012, "steel");
    PC(A, T, [0.28, 0.91, 0.08], 0.28, 0.07, "black");
    PB(A, T, [0.42, 0.925, 0.05], [0.68, 0.94, 0.11], "black");
    PC(A, T, [0.2, 1.51, -0.3], 0.13, 1.25, "rust");   // flue
    PC(A, T, [0.2, 2.18, -0.3], 0.24, 0.1, "rust", { top: 0.4 });
  }
  A.endFurniture();
  A.beginFurniture("Prep table");
  folTable(A, TY(1.6, -4.15, 0), 0.7, 1.6, 0.86);
  PC(A, TY(1.25, -4.1), [0, 0.975, 0], 0.32, 0.24, "steel");
  PB(A, TY(1.9, -4.2, 0), [-0.25, 0.855, -0.18], [0.25, 0.88, 0.18], "wood");
  PB(A, TY(1.95, -4.05, 30), [-0.14, 0.876, -0.015], [0.14, 0.892, 0.015], "steel");
  A.endFurniture();
  // water jerrycans along the back wall, one on its side
  [[0.95, -4.75, 0], [1.15, -4.75, 0], [1.35, -4.75, 0], [-0.9, -4.5, 90]].forEach(([x, z, yaw]) => { A.beginFurniture("Jerrycan"); jerrycan(A, TY(x, z, yaw), "od_paint"); A.endFurniture(); });
  A.beginFurniture("Jerrycan (fallen)");
  jerrycan(A, TT([0.4, 0.0825, -3.6], [0, -1, 0], [0.34, 0, 0.94]));
  COL(A, [0.15, 0, -3.95], [0.65, 0.17, -3.25]);
  A.endFurniture(false);
  // ration crates stacked at the front-right corner
  A.beginFurniture("Crates"); crate(A, TY(2.3, 4.2, 0), 0.9, 0.5, 0.6); crate(A, TY(2.3, 4.2, 0, 0.5), 0.9, 0.5, 0.6, 0.06); A.endFurniture();
  A.beginFurniture("Crate"); crate(A, TY(2.4, 3.3, 90), 0.8, 0.45, 0.55); A.endFurniture();
  A.beginFurniture("Ration box"); PR(A, TY(1.5, 4.3, 15), [-0.25, 0, -0.18], [0.25, 0.28, 0.18], 0.02, "paper"); A.endFurniture();
  clutter(A, [["can", 0.3, 0.3, 30], ["can", -0.2, -2.8, 100], ["paper", 0.1, 2.0, 20], ["paper", -0.3, -0.4, 60], ["helmet", 0.35, -1.0, 0], ["boots", 2.5, -2.8, 80]]);
  nav(A, sh, W);
  return A;
}

function medical() {
  const W = 5, L = 8;
  const A = mk("mod_tent_medical", "Medical tent (abandoned)",
    "An abandoned olive-canvas field medical tent, 5 x 8 m on a steel frame, with red-cross panels, stretchers on trestles, a steel medical cabinet and an IV stand; torn, stained and leaf-strewn, one corner sagging.", W, L);
  const sh = shell(A, { W, L, Hw: 1.75, Hr: 3.0, bays: 4, seed: 37, tear: { s: -1, zi: 4, k: 1 } });
  // red crosses: on both side walls (upper strip of a mid bay), on both roof slopes, on the back gable
  const cross_ = (P, sz) => {
    const a = sz * 0.2, h = sz * 0.39;
    decal(A, P, 0, 0, sz, sz, 0.004, "white");
    decal(A, P, 0, 0, a * 2, h * 2, 0.004, "red");   // the bar along z, then the two arms along x (they abut it: same layer)
    decal(A, P, (a + h) / 2, 0, h - a, a * 2, 0.004, "red");
    decal(A, P, -(a + h) / 2, 0, h - a, a * 2, 0.004, "red");
  };
  for (const s of [-1, 1]) {
    cross_(sh.wallP.find((w) => w.s === s && w.k === 1 && w.zi === 3).P, 0.75);
    cross_(sh.roofP.find((r) => r.s === s && r.k === 1 && r.zi === 2).P, 0.85);
  }
  {
    // white 15 mm off the gable, red 15 mm off the white
    const z = sh.zb - 0.01 - 0.015, zr = z - 0.004 - 0.015, x = -1.55, y = 1.15, sz = 0.7, a = sz * 0.2, h = sz * 0.39;
    A.box([x - sz / 2, y - sz / 2, z - 0.004], [x + sz / 2, y + sz / 2, z], "white", { k: 0 });
    A.box([x - h, y - a, zr - 0.004], [x + h, y + a, zr], "red", { k: 0 });
    A.box([x - a, y + a, zr - 0.004], [x + a, y + h, zr], "red", { k: 0 });
    A.box([x - a, y - h, zr - 0.004], [x + a, y - a, zr], "red", { k: 0 });
  }
  // stretchers on trestles: two along the left wall, one along the right
  const stretcher = (x, z, stain, blanket) => {
    A.beginFurniture("Stretcher on trestles");
    const T = TY(x, z, 90);
    for (const tx of [-0.7, 0.7]) {
      PB(A, T, [tx - 0.04, 0.66, -0.36], [tx + 0.04, 0.7, 0.36], "wood");
      for (const zz of [-0.3, 0.3]) for (const dx of [-0.2, 0.2]) PP(A, T, [tx, 0.67, zz], [tx + dx, 0, zz * 1.06], 0.035, "wood");
      PB(A, T, [tx - 0.17, 0.2, -0.015], [tx + 0.17, 0.23, 0.015], "wood");
    }
    for (const zz of [-0.27, 0.27]) {
      PB(A, T, [-1.1, 0.7, zz - 0.0175], [1.1, 0.735, zz + 0.0175], "wood");
      for (const xe of [-1.165, 1.165]) PC(A, T, [xe, 0.7175, zz], 0.03, 0.15, "black", {}, "x");
    }
    PB(A, T, [-0.95, 0.712, -0.26], [0.95, 0.722, 0.26], "fab_od");   // edges buried in the poles
    if (stain) {   // 15 mm off the canvas, the second stain 10 mm over the first
      PB(A, T, [-0.3, 0.737, -0.16], [0.25, 0.74, 0.12], "blood");
      if (!blanket) PB(A, T, [0.1, 0.75, -0.05], [0.45, 0.753, 0.2], "blood");
    }
    if (blanket) { PR(A, T, [0.3, 0.727, -0.245], [0.9, 0.79, 0.245], 0.03, "fab_grey"); PO(A, T, [0.95, 0.5, 0.305], [0, -1, 0], [1, 0, 0], [0.5, 0.012, 0.45], "fab_grey"); }
    PB(A, T, [-1.25, 0, -0.38], [1.25, 0.8, 0.38], "wood", { v: 0 });
    A.endFurniture(false);
  };
  stretcher(-1.75, -2.2, true, false);
  stretcher(-1.75, 0.8, false, true);
  stretcher(1.75, -0.6, true, true);
  // steel medical cabinet at the back right, the right door hanging open
  A.beginFurniture("Medical cabinet");
  {
    const T = TY(1.55, -3.6, 0), w = 0.9, h = 1.8, d = 0.45;
    PB(A, T, [-w / 2, 0, -d / 2], [w / 2, 0.06, d / 2], "steel");
    PB(A, T, [-w / 2, h - 0.03, -d / 2], [w / 2, h, d / 2], "steel");
    PB(A, T, [-w / 2, 0.06, -d / 2], [w / 2, h - 0.03, -d / 2 + 0.02], "steel");
    for (const x of [-w / 2 + 0.01, w / 2 - 0.01]) PB(A, T, [x - 0.01, 0.06, -d / 2 + 0.02], [x + 0.01, h - 0.03, d / 2], "steel");
    for (const y of [0.5, 0.95, 1.4]) PB(A, T, [-w / 2 + 0.02, y, -d / 2 + 0.02], [w / 2 - 0.02, y + 0.015, d / 2 - 0.02], "steel");
    PB(A, T, [-w / 2, 0.08, d / 2], [0, h - 0.04, d / 2 + 0.02], "steel");   // left door, shut
    PB(A, T, [-0.08, 0.95, d / 2 + 0.015], [-0.05, 1.1, d / 2 + 0.04], "black");
    PO(A, T, [w / 2 + 0.225 * 0.42, 0.92, d / 2 + 0.02 + 0.225 * 0.91], [0.42, 0, 0.91], [-0.91, 0, 0.42], [0.45, 1.68, 0.02], "steel");   // right door swung open ~65 deg
    PB(A, T, [-w / 2 + 0.005, 1.6, d / 2 + 0.03], [-0.2, 1.75, d / 2 + 0.033], "white");   // label 10 mm off the door, its cross 10 mm off the label
    PB(A, T, [-0.36, 1.66, d / 2 + 0.043], [-0.29, 1.69, d / 2 + 0.046], "red");
    for (let i = 0; i < 6; i++) PC(A, T, [-0.3 + i * 0.12, 0.96 + (0.12 + (i % 3) * 0.04) / 2, 0.05], 0.06, 0.12 + (i % 3) * 0.04, i % 2 ? "glass" : "plastic", {}, "y");
    PB(A, T, [-0.35, 1.41, -0.1], [0.05, 1.6, 0.15], "paper");
    PB(A, T, [0.1, 0.51, -0.12], [0.38, 0.68, 0.14], "white");
    PC(A, TT(tp(T, [0.25, 1.445, 0.1]), [0, 1, 0], [1, 0, 0]), [0, 0, 0], 0.06, 0.15, "glass", {}, "x");   // a bottle on its side
    COL(A, [1.55 - w / 2, 0, -3.6 - d / 2], [1.55 + w / 2 + 0.45, h, -3.6 + d / 2 + 0.65]);
  }
  A.endFurniture(false);
  // IV stand next to the first stretcher
  A.beginFurniture("IV stand");
  {
    const x = -0.95, z = -1.5;
    for (let i = 0; i < 5; i++) { const a = i * 72 * D2R; A.obox([x + Math.sin(a) * 0.14, 0.05, z + Math.cos(a) * 0.14], [0.025, 0.02, 0.28], [0, i * 72, 0], "steel"); A.cyl([x + Math.sin(a) * 0.27, 0.025, z + Math.cos(a) * 0.27], [0.04, 0.05, 0.04], "black"); }
    A.cyl([x, 1.03, z], [0.025, 1.95, 0.025], "steel");
    A.box([x - 0.16, 1.95, z - 0.006], [x + 0.16, 1.962, z + 0.006], "steel");
    A.rbox([x + 0.06, 1.62, z - 0.025], [x + 0.18, 1.9, z + 0.025], 0.015, "plastic");
    pipe(A, [x + 0.12, 1.62, z], [-1.45, 0.75, -1.9], 0.005, "plastic");
    COL(A, [x - 0.15, 0, z - 0.15], [x + 0.15, 1.95, z + 0.15]);
  }
  A.endFurniture(false);
  A.beginFurniture("Supply crates"); crate(A, TY(-1.95, 3.0, 90), 0.8, 0.45, 0.55); crate(A, TY(-1.95, 3.0, 90, 0.45), 0.7, 0.4, 0.5, 0.05); A.endFurniture();
  A.beginFurniture("Waste bin"); PC(A, TY(1.95, 1.2), [0, 0.3, 0], 0.4, 0.6, "od_paint"); PB(A, TY(1.95, 1.2), [-0.12, 0.6, -0.12], [0.12, 0.63, 0.12], "od_paint"); A.endFurniture();
  clutter(A, [["bandage", 0.2, 0.2, 30], ["bandage", -0.5, -0.4, 80], ["paper", 0.3, -1.2, 15], ["paper", 0.35, -1.4, 50], ["can", 0.6, 2.1, 0], ["boots", 0.8, 2.8, 120], ["bag", 0.6, 1.1, 60]]);
  nav(A, sh, W);
  return A;
}

function command() {
  const W = 5, L = 6;
  const A = mk("mod_tent_command", "Command tent (abandoned)",
    "An abandoned olive-canvas command tent, 5 x 6 m on a steel frame, with a map table, a radio set on a desk, folding chairs and a map board, and a guyed antenna mast outside; torn, muddy and leaf-strewn.", W, L);
  const sh = shell(A, { W, L, Hw: 1.75, Hr: 3.0, bays: 3, seed: 51, tear: { s: -1, zi: 3, k: 1 } });
  // map table
  A.beginFurniture("Map table");
  {
    const T = TY(-0.3, -0.5, 0);
    folTable(A, T, 1.1, 1.8, 0.86);
    // sheets and flat things in layers over the top (0.86): world rectangles, each above what it covers
    const TLs = [], lay = (x, z, hx, hz, h) => 0.86 + layerAt(TLs, [x - hx, x + hx, z - hz, z + hz], h, GAP(4 * hx * hz));
    let y = lay(-0.325, -0.515, 0.775, 0.465, 0.003); PB(A, T, [-0.8, y, -0.48], [0.75, y + 0.003, 0.45], "map");
    y = lay(0.1, -0.4, 0.36, 0.3, 0.003); PB(A, TY(0.1, -0.4, 12), [-0.32, y, -0.24], [0.32, y + 0.003, 0.24], "map");
    for (let i = 0; i < 9; i++) PC(A, T, [-0.6 + rnd(i, 1) * 1.2, 0.8875, -0.35 + rnd(i, 2) * 0.7], 0.012, 0.065, i % 3 ? "red" : "ink");   // pins through the sheets
    for (let i = 0; i < 3; i++) { const x = -0.6 + i * 0.3, z = -0.6 + i * 0.08; y = lay(x, z, 0.18, 0.18, 0.002); PB(A, TY(x, z, 20 + i * 35), [-0.18, y, -0.004], [0.18, y + 0.002, 0.004], "ink"); }   // drawn lines
    y = lay(0.3, -0.85, 0.16, 0.16, 0.005); PB(A, TY(0.3, -0.85, 70), [-0.15, y, -0.02], [0.15, y + 0.005, 0.02], "plastic");   // ruler
    PC(A, T, [-0.65, 0.9, 0.3], 0.085, 0.09, "od_paint");   // mug, sunk into the top
    y = lay(0.2, -0.2, 0.035, 0.035, 0.015); PC(A, TY(0.2, -0.2), [0, y + 0.0075, 0], 0.07, 0.015, "black");   // compass
  }
  A.endFurniture();
  // radio desk on the right wall
  A.beginFurniture("Radio desk");
  {
    const T = TY(1.85, -1.4, 270);   // local x along the wall, front (+z local) faces -x world
    folTable(A, T, 0.7, 1.4, 0.76);
    PB(A, T, [-0.45, 0.755, -0.25], [0.05, 1.06, 0.12], "od_paint", { bv: 0.012 });   // the set
    PB(A, T, [-0.42, 0.79, 0.115], [0.02, 1.03, 0.132], "black");
    for (let i = 0; i < 4; i++) PC(A, T, [-0.37 + i * 0.1, 0.86, 0.15], 0.035, 0.04, "black", {}, "z");
    PB(A, T, [-0.3, 0.94, 0.128], [-0.1, 1.0, 0.145], "glass");
    PB(A, T, [0.08, 0.755, -0.2], [0.42, 0.98, 0.1], "od_paint", { bv: 0.01 });   // battery / amplifier box
    PR(A, T, [0.15, 0.755, 0.14], [0.35, 0.81, 0.22], 0.02, "black");   // handset
    PP(A, T, [0.15, 0.785, 0.18], [-0.05, 0.77, 0.2], 0.012, "black");
    PP(A, T, [-0.05, 0.77, 0.2], [-0.2, 0.9, 0.125], 0.012, "black");
    PB(A, T, [-0.5, 0.77, 0.15], [-0.25, 0.773, 0.3], "paper");   // the log book, 10 mm off the top
    PP(A, T, [-0.4, 1.06, -0.2], [-0.4, 1.7, -0.33], 0.014, "black");   // antenna lead up the wall and out
  }
  A.endFurniture();
  pipe(A, [2.18, 1.7, -1.8], [2.62, 1.45, -1.8], 0.014, "black", { k: 0 });
  pipe(A, [2.62, 1.45, -1.8], [2.75, 0.02, -1.8], 0.014, "black", { k: 0 });
  pipe(A, [2.75, 0.02, -1.8], [3.35, 0.02, -1.9], 0.014, "black", { k: 0 });
  // folding chairs: two at the map table, one at the desk, one knocked over
  for (const [x, z, yaw] of [[-0.6, 0.35, 180], [0.4, 0.3, 200], [1.15, -1.5, 90]]) { A.beginFurniture("Folding chair"); chair(A, TY(x, z, yaw)); A.endFurniture(false); }
  A.beginFurniture("Folding chair (fallen)");
  chair(A, TT([-1.6, 0.235, 0.6], [0, 1, 0], [-0.6, 0, 0.8]), false);
  COL(A, [-2.4, 0, 0.4], [-1.4, 0.47, 1.5]);
  A.endFurniture(false);
  // map board on a stand in the back left
  A.beginFurniture("Map board");
  {
    const T = TY(-1.45, -2.45, 0);
    PB(A, T, [-0.65, 0.795, -0.015], [0.65, 1.755, 0.015], "board");
    PB(A, T, [-0.68, 0.77, -0.025], [0.68, 0.8, 0.025], "steel");
    PB(A, T, [-0.68, 1.75, -0.025], [0.68, 1.78, 0.025], "steel");
    PB(A, T, [-0.55, 0.9, 0.03], [0.2, 1.65, 0.033], "map");   // 15 mm off the board
    for (let i = 0; i < 4; i++) PB(A, TY(-1.25 + i * 0.12, -2.45, 0), [-0.002, 1.0 + i * 0.1, 0.043], [0.25, 1.012 + i * 0.1, 0.046], "ink");   // marker arrows, 10 mm off the map
    PB(A, T, [0.28, 1.3, 0.025], [0.55, 1.6, 0.028], "paper");
    for (const x of [-0.6, 0.6]) { PB(A, T, [x - 0.02, 0.03, -0.02], [x + 0.02, 0.77, 0.02], "steel"); PB(A, T, [x - 0.03, 0, -0.25], [x + 0.03, 0.03, 0.25], "steel"); }
    PB(A, T, [-0.6, 0.76, 0.025], [0.6, 0.77, 0.08], "steel");   // marker tray
  }
  A.endFurniture();
  A.beginFurniture("Ammo box"); PB(A, TY(1.9, 0.9, 90), [-0.15, 0, -0.08], [0.15, 0.19, 0.08], "od_paint", { bv: 0.008 }); PB(A, TY(1.9, 0.9, 90), [-0.16, 0.19, -0.09], [0.16, 0.21, 0.09], "od_paint"); A.endFurniture();
  clutter(A, [["paper", 0.4, 1.0, 20], ["paper", 0.6, 1.3, 70], ["paper", -0.1, 1.6, 40], ["can", 0.9, -0.1, 30], ["helmet", 1.9, 1.6, 0]]);
  // antenna mast outside the right wall, leaning a little, three guy wires
  {
    const base = [3.65, 0, -1.95], dir = nrm([0.03, 1, 0.015]), at = (h) => add(base, mul(dir, h));
    const X = nrm(cross(dir, [0, 0, 1])), Z = cross(X, dir), r = basisEuler(X, dir, Z);
    A.box([base[0] - 0.25, 0, base[2] - 0.25], [base[0] + 0.25, 0.02, base[2] + 0.25], "rust", { k: 0 });
    A.cyl(at(1.31), [0.08, 2.6, 0.08], "od_paint", { r });
    A.cyl(at(3.8), [0.064, 2.6, 0.064], "od_paint", { k: 0, r });
    A.cyl(at(6.1), [0.05, 2.2, 0.05], "od_paint", { k: 0, r });
    for (const h of [2.55, 5.05]) A.cyl(at(h), [0.1, 0.07, 0.1], "steel", { k: 0, r });
    A.cone(at(7.74), [0.03, 1.1, 0.03], "black", { k: 0, r, top: 0.3 });
    pipe(A, at(7.0), add(at(7.0), [0.55, -0.05, 0]), 0.012, "steel", { k: 0 });
    pipe(A, at(7.0), add(at(7.0), [-0.55, -0.05, 0]), 0.012, "steel", { k: 0 });
    for (let i = 0; i < 3; i++) {
      const a = (i * 120 + 10) * D2R, peg = [base[0] + Math.cos(a) * 1.6, 0.1, base[2] + Math.sin(a) * 1.6];
      if (Math.abs(peg[0]) < 2.7) peg[0] = 2.8;
      pipe(A, at(6.6), peg, 0.008, "steel", { k: 0 });
      A.cyl(peg, [0.022, 0.25, 0.022], "rust", { k: 0 });
    }
  }
  nav(A, sh, W);
  return A;
}

export function nt4TentAssets() { return [sleeping(), mess(), medical(), command()]; }
