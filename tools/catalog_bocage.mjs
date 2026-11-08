// Bocage and Mont-Castre terrain pieces for the MP & Creative update (SPEC 3.3 / 3.4 / 3.6, docs/assets-mpu.md):
// hedgerow banks (straight, corner, gap) with hedge and trees on top, a sunken lane, a quarry face, an earth rampart
// piece, an abatis of felled trees and a marsh / reed patch.
// Hedges and trees use the engine's foliage block ("tree": leaf cards on canopy spheres). Everything in a foliage
// asset goes through the stump / trunk split, so these pieces use boxes only (no prisms) and are indestructible
// ("hedge": true), like the existing hedgerow_bocage. Round parts (B1 frustums) are fine: they never split at the
// stump cut, they go whole to the trunk mesh, which a hedge never fells.
import { Asset } from "./kit.mjs";
import { stdMaterials } from "./parts.mjs";
import { M, mats, rng, rect, fire, cover, setNav, setTour, segBox, r3, extendEnds, band, sock, setSnap, asCap } from "./catalog_atlantic.mjs";
import { smallArmsCache } from "./catalog_ammo.mjs";

const SOIL = { name: "bank_soil", albedo: [0.19, 0.21, 0.11], roughness: 0.97, pattern: "plain", metallic: 0, grain: 1 };
function bocageMats(A) {
  stdMaterials(A);
  A.materials.push({ ...SOIL });
  mats(A, "spoil", "mud", "rock", "rock_dark", "log_raw", "bark_grey", "water", "reed", "reed_green");
  // NT pass: bark is the L4 bark set along each part's axis over a plain base (the wood pattern drew plank seams)
  for (const m of A.materials) if (m.name === "bark" || m.name === "bark_grey") Object.assign(m, { pattern: "plain", texture: "bark", grain: 1 });
  // quarry rock: the natural-rock pattern (limestone lithology, as the slope rock and boulders; its albedo multiplies
  // that palette) instead of coursed masonry, which made the face and talus read as brickwork
  for (const m of A.materials) if (m.name === "rock" || m.name === "rock_dark") Object.assign(m, { albedo: m.name === "rock" ? [0.95, 0.92, 0.86] : [0.7, 0.68, 0.64], pattern: "rock", texture: "rock" });
  A.materials.push({ name: "bark_moss", albedo: [0.13, 0.15, 0.085], roughness: 0.97, pattern: "plain", texture: "bark", metallic: 0, grain: 1 });
}
// ---- NT pass (v2): round tapered stems and limbs, coppiced hedge stools with bare stems at the foot and an uneven
// top, clumped leaf masses (as tools/gen_env_assets.py seg3 / limb3 / clump3 / coppice3)
const D2R = Math.PI / 180;
function seg(A, a, b, ra, rb, m, n = 6) {   // one tapered round segment from a to b, visual only
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  const pitch = Math.asin(Math.max(-1, Math.min(1, d[1] / L))) / D2R, yaw = Math.atan2(d[0], d[2]) / D2R;
  const o = { seg: n, top: Math.min(1, rb / ra), k: 0 };
  if (Math.abs(90 - pitch) > 0.05) o.r = [90 - pitch, yaw, 0];
  A.cone([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [2 * ra, L + 0.02, 2 * ra], m, o);
  return b;
}
const along = (pts, f) => { const s = Math.min(pts.length - 2, Math.floor(f * (pts.length - 1))), t = f * (pts.length - 1) - s; return [0, 1, 2].map((i) => pts[s][i] + (pts[s + 1][i] - pts[s][i]) * t); };
function limb(A, R, base, L, yaw, pitch, bend, ra, rb, m, nseg = 2, n = 6) {   // a curved tapering limb; returns its points
  const pts = [base];
  for (let s = 0; s < nseg; s++) {
    const pr = (pitch + (bend * s) / Math.max(1, nseg - 1)) * D2R, yr = (yaw + (R() - 0.5) * 16) * D2R, p = pts[pts.length - 1];
    pts.push([p[0] + Math.sin(yr) * Math.cos(pr) * L / nseg, p[1] + Math.sin(pr) * L / nseg, p[2] + Math.cos(yr) * Math.cos(pr) * L / nseg]);
    seg(A, p, pts[pts.length - 1], ra + ((rb - ra) * s) / nseg, ra + ((rb - ra) * (s + 1)) / nseg, m, n);
  }
  return pts;
}
function clump(R, canopy, c, r, sats, out, up = 0.25) {   // a main leaf sphere and smaller satellites pushed outward
  canopy.push([r3(c[0]), r3(c[1] + r * up), r3(c[2]), r3(r * (0.82 + R() * 0.13))]);
  for (let k = 0; k < sats; k++) {
    const a = R() * 2 * Math.PI, e = -0.3 + R() * 1.05;
    const d = [Math.cos(e) * Math.sin(a) + out[0] * 0.8, Math.sin(e), Math.cos(e) * Math.cos(a) + out[1] * 0.8], o = (r * (0.6 + R() * 0.25)) / Math.hypot(...d);
    canopy.push([r3(c[0] + d[0] * o), r3(c[1] + r * up + d[1] * o), r3(c[2] + d[2] * o), r3(r * (0.45 + R() * 0.17))]);
  }
}
// a coppice stool: stems splaying from one root, a leaf lobe at each top and one on the outer side; the lower `bare`
// share of each stem carries no leaves
function coppice(A, R, canopy, x, z, y, stems, h0, h1, rs, spread, lobe, bare = 0.4, side = true) {
  for (let i = 0; i < stems; i++) {
    const yaw = (360 * i) / stems + (R() - 0.5) * 60, pitch = 90 - spread + R() * (spread - 4), h = h0 + R() * (h1 - h0);
    const pts = limb(A, R, [x + (R() - 0.5) * 0.12, y - 0.05, z + (R() - 0.5) * 0.12], h / Math.sin(pitch * D2R), yaw, pitch, -(4 + R() * 10), rs, rs * 0.3, "bark", 2, 5);
    const out = [Math.sin(yaw * D2R), Math.cos(yaw * D2R)];
    clump(R, canopy, pts[pts.length - 1], lobe * (0.85 + R() * 0.25), 1, out, 0.1);
    if (!side) continue;
    const m = along(pts, bare + 0.15 + R() * 0.2);
    canopy.push([r3(m[0] + out[0] * lobe * 0.45), r3(m[1]), r3(m[2] + out[1] * lobe * 0.45), r3(lobe * (0.55 + R() * 0.15))]);
  }
}
// earth bank along a segment: base W, top Wt, height H (a core box and two battered side slabs)
function bankSeg(A, p0, p1, H, W = 3.0, Wt = 1.1, e0 = 0, e1 = 0) {
  segBox(A, p0, p1, 0, -0.4, H, Wt + 0.2, "bank_soil", {}, e0, e1);
  const run = (W - Wt) / 2, fl = Math.hypot(run, H), tilt = (Math.atan2(run, H) * 180) / Math.PI, t = 0.25;
  const ux = run / fl, uy = H / fl;   // outward-down face direction is (ux, -uy); its outward normal is (uy, ux)
  for (const side of [-1, 1]) {
    const mz = Wt / 2 + run / 2 - uy * t, my = H / 2 - ux * t;
    // battered sides: shortened only where a leg runs on into another (corner); at a piece end they reach the end
    // plane so joined pieces close without a notch (V6 snapping)
    segBox(A, p0, p1, side * mz, my - (fl + 0.6) / 2, my + (fl + 0.6) / 2, 2 * t, "bank_soil", {}, e0 > 0 ? e0 - 0.3 : 0, e1 > 0 ? e1 - 0.3 : 0, -side * tilt);
  }
}
// hedge along the bank top: a continuous body of overlapping leaf masses whose undersides sit ~0.45 m above the bank
// (the stems show below), rolling with a slow wave; coppice stools whose tops break the line unevenly, the odd
// standard, a low skirt in places
function hedgeAlong(A, R, p0, p1, H, canopy) {
  const dx = p1[0] - p0[0], dz = p1[1] - p0[1], L = Math.hypot(dx, dz), ux = dx / L, uz = dz / L, ph = R() * 6.28;
  const at = (s, off) => [p0[0] + ux * s - uz * off, p0[1] + uz * s + ux * off];
  for (let s = 0.3; s < L - 0.1; s += 0.6 + R() * 0.15) {
    const [x, z] = at(s, (R() - 0.5) * 0.35), r = 0.72 + R() * 0.14;
    canopy.push([r3(x), r3(H + 0.5 + r * 0.95 + 0.25 * Math.sin(s * 0.8 + ph)), r3(z), r3(r)]);
  }
  for (let s = 0.4; s < L - 0.2; s += 0.9 + R() * 0.4) {
    const [x, z] = at(s, (R() - 0.5) * 0.5), tall = 0.35 * Math.sin(s * 0.8 + ph) + (R() < 0.18 ? 0.9 : 0);
    coppice(A, R, canopy, x, z, H, R() < 0.4 ? 3 : 2, 1.7 + tall, 2.3 + tall, 0.035, 18, 0.5, 0.4, false);
    if (R() < 0.3) canopy.push([r3(x + (R() - 0.5) * 0.4), r3(H + 0.35), r3(z + (R() - 0.5) * 0.4), r3(0.38)]);
  }
}
// hedgerow tree on the bank (oak / ash pollard): tapered trunk, a few limbs, a crown of canopy spheres
// NT: the collider boxes are as before (now invisible); the visible tree is a flared, wobbling round bole with curved
// tapering limbs, a secondary on each and clumped leaf masses
function bankTree(A, R, x, z, H, canopy, h = 5.5) {
  A.box([x - 0.24, H - 0.3, z - 0.24], [x + 0.24, H + h * 0.5, z + 0.24], "bark", { v: 0 });
  A.box([x - 0.17, H + h * 0.5, z - 0.17], [x + 0.17, H + h, z + 0.17], "bark", { v: 0 });
  seg(A, [x, H - 0.35, z], [x, H + 0.3, z], 0.4, 0.26, "bark_moss", 12);   // root flare over the bank top
  const bole = [[x, H + 0.25, z]];
  for (let k = 1; k <= 3; k++) bole.push([x + (R() - 0.5) * 0.14, H + 0.25 + ((h - 0.25) * k) / 3, z + (R() - 0.5) * 0.14]);
  for (let k = 0; k < 3; k++) seg(A, bole[k], bole[k + 1], 0.25 - 0.035 * k, 0.215 - 0.035 * k, "bark", 12);
  for (let k = 0; k < 4; k++) {
    const yaw = k * 90 + R() * 50, len = 2.0 + R() * 1.2, out = [Math.sin(yaw * D2R), Math.cos(yaw * D2R)];
    const pts = limb(A, R, along(bole, 0.55 + 0.12 * k), len, yaw, 35 + R() * 15, -14, 0.1, 0.04, "bark", 2, 7);
    const sp = limb(A, R, along(pts, 0.5), len * 0.55, yaw + (R() - 0.5) * 80, 50, 0, 0.045, 0.015, "bark", 1, 5);
    clump(R, canopy, pts[2], 1.2 + R() * 0.35, 2, out);
    clump(R, canopy, sp[1], 0.75 + R() * 0.2, 0, out);
  }
  clump(R, canopy, [x, H + h + 0.9, z], 1.6, 2, [0, 0], 0.3);
}
function foliage(A, canopy, seed, leaf = [0.14, 0.21, 0.075]) {
  A.meta.tree = { hpBullet: 99999, hpAT: 99999, stumpHeight: 0.15, trunkRadius: 0.05, mass: 30, canopy, cardsPerSphere: 70, cardDensity: 70, leafSize: 0.34, leafColor: leaf, seed, hedge: true };
}
function hedgerowAsset(id, name, desc, footprint) {
  const A = new Asset({ id, version: 2, name, category: "Bocage", description: desc, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 1.5, noAutoPanels: true });
  bocageMats(A);
  return A;
}
// rounded bank end over socket idx's free end (p on the end plane, outward dir); left out while joined (V6)
function bankCap(A, p, dir, H, idx) {
  const c = [p[0] + dir[0] * 0.1, p[1] + dir[1] * 0.1], hx = Math.abs(dir[0]) > 0.5 ? 0.6 : 1.35, hz = Math.abs(dir[0]) > 0.5 ? 1.35 : 0.6;
  asCap(A.rbox([c[0] - hx, -0.3, c[1] - hz], [c[0] + hx, H - 0.15, c[1] + hz], 0.55, "bank_soil"), idx);
}
const bankCover = (x0, x1, z, yaw) => { const out = []; for (let x = x0; x <= x1 + 1e-6; x += 2.5) out.push(cover([x, 0, z], yaw, "high", ["over"])); return out; };

// ============================================================================ hedgerows
export function hedgerowStraight() {
  const H = 1.6, L = 10;
  const A = hedgerowAsset("bcg_hedgerow_straight", "Hedgerow bank, straight (10 m)", "Norman bocage hedgerow: an earth bank 1.6 m high and 3 m wide at the base, topped by a dense hazel / hawthorn hedge and two pollard trees, about 4 m to the hedge top and 9 m to the crowns. Blocks movement and sight; vehicles cannot cross. Pieces join end to end along local X.", [L, 3.2]);
  const R = rng(7101), canopy = [];
  bankSeg(A, [-L / 2, 0], [L / 2, 0], H, 3.0, 1.1, 0.0, 0.0);
  hedgeAlong(A, R, [-L / 2, 0], [L / 2, 0], H, canopy);
  bankTree(A, R, -2.6, 0.1, H, canopy, 5.5); bankTree(A, R, 3.1, -0.15, H, canopy, 4.8);
  setSnap(A, [sock([-L / 2, 0, 0], [-1, 0], "hedge"), sock([L / 2, 0, 0], [1, 0], "hedge")]);
  bankCap(A, [-L / 2, 0], [-1, 0], H, 0); bankCap(A, [L / 2, 0], [1, 0], H, 1);
  foliage(A, canopy, 7101);
  setNav(A, { effect: "block", cover: bankCover(-3.75, 3.75, -1.9, 0).concat(bankCover(-3.75, 3.75, 1.9, 180)) });
  setTour(A, { pos: [0.8, 0, -6.0], yaw: 180 }, [{ goto: [0.8, -2.2] }, { look: [0.8, 2.2, 0], hold: 0.5 }, { shot: "mpu_hedgerow_from_field" }]);
  return A;
}
export function hedgerowCorner() {
  const H = 1.5;
  const A = hedgerowAsset("bcg_hedgerow_corner", "Hedgerow bank, corner", "Corner of a bocage field: two 6 m hedgerow banks meeting at a right angle (along -X and +Z from the corner), 1.5 m earth banks with hedge and a corner oak. The classic MG corner. Joins straight pieces at its two ends.", [7.6, 7.6]);
  const R = rng(7202), canopy = [];
  // legs: from (-6, 0) to the corner (0, 0), then up to (0, 6)
  bankSeg(A, [-6, 0], [0, 0], H, 3.0, 1.1, 0, 0.6);
  bankSeg(A, [0, 0], [0, 6], H, 3.0, 1.1, 0.6, 0);
  hedgeAlong(A, R, [-6, 0], [-0.3, 0], H, canopy); hedgeAlong(A, R, [0, 0.3], [0, 6], H, canopy);
  bankTree(A, R, 0.0, 0.0, H, canopy, 6.0);
  setSnap(A, [sock([-6, 0, 0], [-1, 0], "hedge"), sock([0, 0, 6], [0, 1], "hedge")]);
  bankCap(A, [-6, 0], [-1, 0], H, 0); bankCap(A, [0, 6], [0, 1], H, 1);
  foliage(A, canopy, 7202);
  setNav(A, { effect: "block", cover: [cover([-1.9, 0, -1.9], 0, "high", ["left"]), cover([-4.2, 0, -1.9], 0, "high", ["over"]), cover([1.9, 0, 1.9], 270, "high", ["right"]), cover([1.9, 0, 4.2], 270, "high", ["over"])],
    firingPoints: [] });
  setTour(A, { pos: [-4.0, 0, 5.0], yaw: 180 }, [{ goto: [-2.2, 2.2], note: "inside the corner" }, { look: [0, 2.0, 0], hold: 0.5 }, { shot: "mpu_hedgerow_corner" }]);
  return A;
}
export function hedgerowGap() {
  const H = 1.6, L = 10, G = 3.6;
  const A = hedgerowAsset("bcg_hedgerow_gap", "Hedgerow bank with field gap (10 m)", "10 m bocage hedgerow with a 3.6 m field gateway in the middle: the bank ends rounded off either side of the gap, hedge and a tree on each half. The gap is the only way through. Joins end to end along local X.", [L, 3.2]);
  const R = rng(7303), canopy = [];
  for (const s of [-1, 1]) {
    const a = [s * L / 2, 0], b = [s * G / 2, 0];
    bankSeg(A, s < 0 ? a : b, s < 0 ? b : a, H, 3.0, 1.1);
    A.rbox([s * G / 2 - 0.9, -0.3, -1.3], [s * G / 2 + 0.9, H - 0.2, 1.3], 0.6, "bank_soil");   // rounded bank end
    hedgeAlong(A, R, s < 0 ? [a[0], 0] : [b[0] + 0.3, 0], s < 0 ? [b[0] - 0.3, 0] : [a[0], 0], H, canopy);
    bankTree(A, R, s * (G / 2 + 2.3), 0.05, H, canopy, 5.0);
  }
  A.box([-G / 2 + 0.4, -0.05, -1.6], [G / 2 - 0.4, 0.02, 1.6], "mud", { k: 0 });   // trodden gateway
  setSnap(A, [sock([-L / 2, 0, 0], [-1, 0], "hedge"), sock([L / 2, 0, 0], [1, 0], "hedge")]);
  bankCap(A, [-L / 2, 0], [-1, 0], H, 0); bankCap(A, [L / 2, 0], [1, 0], H, 1);
  setNav(A, { effect: "block", areas: [{ outline: rect(-G / 2 + 0.6, -1.5, G / 2 - 0.6, 1.5), area: "grass" }],
    cover: [cover([-G / 2 - 1.0, 0, -1.9], 0, "high", ["right"]), cover([G / 2 + 1.0, 0, -1.9], 0, "high", ["left"]), cover([-G / 2 - 1.0, 0, 1.9], 180, "high", ["left"]), cover([G / 2 + 1.0, 0, 1.9], 180, "high", ["right"])] });
  foliage(A, canopy, 7303);
  setTour(A, { pos: [0.2, 0, -5.0], yaw: 180 }, [{ goto: [0.2, -2.2] }, { goto: [0.2, 2.6], expectY: 0, note: "through the field gap" }, { look: [0.2, 1.5, 8], hold: 0.4 }, { shot: "mpu_hedgerow_gap" }]);
  return A;
}

// ============================================================================ sunken lane
// 12 m of lane running along local Z, its floor 2.0 m below the fields, steep earth sides, hedged banks on both
// lips (1.0 m high). Pieces join end to end along Z.
export function sunkenLane() {
  const A = hedgerowAsset("bcg_sunken_lane", "Sunken lane (12 m)", "Norman chemin creux: a 3 m cart track sunk 2 m below the fields between steep earth sides, a hedged bank on each lip, so the lane is 4-5 m deep to the hedge tops and roofed by branches in places. Concealed approach; the sides are too steep to climb except where pieces end. Joins end to end along local Z (cut into the ground).", [9.0, 12]);
  const R = rng(7404), canopy = [], D = 2.0, W = 3.0, L = 12, Z = L / 2, side = 0.9;
  A.box([-W / 2 - 0.2, -D - 0.3, -Z], [W / 2 + 0.2, -D, Z], "mud");
  for (const x of [-0.65, 0.65]) A.box([x - 0.25, -D, -Z], [x + 0.25, -D + 0.015, Z], "spoil", { k: 0 });   // ruts
  for (const s of [-1, 1]) {
    // steep side: a battered slab from the lane floor to the lip
    const tilt = (Math.atan2(side, D) * 180) / Math.PI, fl = Math.hypot(side, D);
    A.obox([s * (W / 2 + side / 2 + 0.2), -D / 2, 0], [0.4, fl + 0.5, L], [0, 0, -s * tilt], "bank_soil");
    A.box([s > 0 ? W / 2 + side : -W / 2 - side - 1.0, -D - 0.2, -Z], [s > 0 ? W / 2 + side + 1.0 : -W / 2 - side, 0.0, Z], "bank_soil");
    bankSeg(A, [s * (W / 2 + side + 0.9), -Z], [s * (W / 2 + side + 0.9), Z], 1.0, 2.2, 0.8);
    hedgeAlong(A, R, [s * (W / 2 + side + 0.9), -Z + 0.2], [s * (W / 2 + side + 0.9), Z - 0.2], 1.0, canopy);
    if (s > 0) bankTree(A, R, W / 2 + side + 0.9, 1.5, 1.0, canopy, 6.0);
  }
  foliage(A, canopy, 7404);
  setSnap(A, [sock([0, 0, -Z], [0, -1], "lane"), sock([0, 0, Z], [0, 1], "lane")]);   // open ends: no caps
  const cutW = W / 2 + side + 0.2;
  setNav(A, { effect: "area", area: "road", areas: [{ outline: rect(-W / 2, -Z, W / 2, Z), area: "road" }],
    cover: [cover([-1.1, -D, -2], 270, "high", ["over"]), cover([1.1, -D, 2], 90, "high", ["over"])] },
    [{ outline: rect(-cutW - 0.3, -Z - 0.8, cutW + 0.3, Z + 0.8), depth: D + 0.3, bank: 0.3 }]);
  setTour(A, { pos: [0.2, 0, -Z - 6.0], yaw: 180 }, [
    { goto: [0.2, -Z - 2.0] }, { goto: [0.2, -Z + 0.3] }, { wait: 0.8 }, { goto: [0.2, -Z + 0.6], note: "down into the lane end" },
    { goto: [0.0, Z - 1.0], expectY: -D, note: "along the sunken lane" }, { look: [0, -D + 1.6, -Z], hold: 0.5 }, { shot: "mpu_sunken_lane" },
  ], { cut: true });
  return A;
}

// ============================================================================ quarry face
// The old stone quarry of Mont-Castre: a 12 m rock face (local +Z faces the quarry floor) with ledges and a talus of
// fallen blocks at its foot, a 5 m lip on top and the walkable back slope (about 35 deg) down to the ground behind.
export function quarryFace() {
  const A = new Asset({ id: "bcg_quarry_face", version: 2, name: "Quarry face (12 m)", category: "Bocage",
    description: "Old stone-quarry face as on Mont-Castre: a 12 m sandstone face, 22 m wide, with ledges and a talus of fallen blocks at its foot (cover), a 5 m lip on top for an MG nest, and a walkable back slope (about 35 deg) down behind. Local +Z faces the quarry floor.",
    floors: 0, enterable: false, footprint: [22, 24], plinthDepth: 2.0, maxGrade: 3.0, noAutoPanels: true });
  bocageMats(A);
  const R = rng(7505), X = 11, Hq = 12, ZF = 5.5, ZL = 0.5, ZB = -18;
  // the face: columns of blocks of varying depth and height (the lip is the top of the tallest)
  for (let x = -X; x < X - 0.01; x += 2.2) {
    const d = R() * 0.9, h = Hq - R() * 1.2;
    A.rbox([x, -2.0, ZL], [x + 2.25, h, ZF - d], 0.35, R() < 0.5 ? "rock" : "rock_dark");
    const ly = 3 + R() * 5;   // a ledge
    A.rbox([x + 0.2, ly - 0.4, ZF - d - 0.3], [x + 2.0, ly, ZF - d + 0.6], 0.15, "rock", { k: 1 });
  }
  A.box([-X, Hq - 0.4, ZL - 0.2], [X, Hq, ZF - 0.9], "bank_soil");             // turf lip
  // back slope from the lip down to grade
  const sl = Math.hypot(Hq, ZL - ZB), ang = (Math.atan2(Hq, ZL - ZB) * 180) / Math.PI;
  A.obox([0, Hq / 2 - 0.35, (ZL + ZB) / 2 + 0.25], [2 * X, 0.7, sl + 0.6], [-ang, 0, 0], "bank_soil");
  // talus of fallen blocks at the foot (cover for the attackers): faceted n-sided frustums, tilted, a broken cap on most
  for (let k = 0; k < 16; k++) {
    const x = (R() * 2 - 1) * (X - 1.5), z = ZF + 0.8 + R() * 4.0, s = 0.6 + R() * 1.3, top = s * (0.55 + R() * 0.4), m = R() < 0.5 ? "rock" : "rock_dark";
    const tilt = () => (R() - 0.5) * 14;
    A.cone([x, (top - 0.4) / 2, z], [s * 1.2, top + 0.4, s], m, { n: 5 + (k % 3), top: 0.55 + R() * 0.3, r: [tilt(), R() * 360, tilt()] });
    if (R() < 0.7) A.cone([x + (R() - 0.5) * s * 0.3, top + s * 0.08, z + (R() - 0.5) * s * 0.3], [s * 0.7, s * 0.3, s * 0.55], m, { n: 5, top: 0.45 + R() * 0.3, r: [tilt() * 2, R() * 360, tilt() * 2] });
  }
  for (let k = 0; k < 10; k++) { const x = (R() * 2 - 1) * (X - 1), z = ZB + 2 + R() * 14; A.rbox([x - 0.5, Hq * (z - ZB) / (ZL - ZB) - 0.4, z - 0.5], [x + 0.5, Hq * (z - ZB) / (ZL - ZB) + 0.3, z + 0.5], 0.3, "reed_green", { k: 0 }); }   // scrub on the slope
  smallArmsCache(A, "Small-arms ammunition", 4.0, Hq, ZL + 0.6, 0, { boxes: 3, bandoliers: 2 });
  mats(A, "picket");
  for (let x = -4.2; x < 3.6; x += 0.62) for (let c = 0; c < 3; c++) A.obox([x + (c % 2) * 0.3, Hq + 0.14 + c * 0.27, 4.55], [0.58, 0.28, 0.36], [0, (c * 7) % 5, 0], "sacking");   // MG nest on the lip
  A.box([-4.5, Hq, 4.35], [3.9, Hq + 0.85, 4.75], "sacking", { v: 0 });
  setNav(A, { effect: "block", firingPoints: [fire([-2.0, Hq, ZF - 1.6], 0, "mg"), fire([2.5, Hq, ZF - 1.6], 0, "mg")],
    cover: [cover([-3.0, Hq, ZF - 2.8], 0, "low", ["over"])], areas: [{ outline: rect(-X + 0.5, ZL, X - 0.5, ZF - 1.0), area: "grass" }] });
  setTour(A, { pos: [0.3, 0, ZB - 3], yaw: 180 }, [
    { goto: [0.3, ZB + 1.0], note: "foot of the back slope" }, { goto: [0.3, ZL + 0.6], expectY: Hq, note: "up to the quarry lip" },
    { goto: [-2.0, ZF - 1.6], expectY: Hq }, { look: [-2, 2, 30], hold: 0.5 }, { shot: "mpu_quarry_lip" },
  ]);
  return A;
}

// ============================================================================ earth rampart
// a 12 m piece of the old (Roman / medieval) earth rampart reused as a trench line: 3 m high, 11 m base, a 1.6 m
// crest with a low firing parapet on the +Z side, scrub and two trees on it
export function earthRampart() {
  const A = hedgerowAsset("bcg_rampart", "Earth rampart (12 m)", "Grass-grown earth rampart of the Mont-Castre camp, reused as a trench line: 3 m high and 11 m wide at the base, a 1.6 m walk on the crest behind a low firing parapet facing +Z, scrub and two trees on it. Both slopes are walkable. Joins end to end along local X.", [12, 11]);
  const R = rng(7606), canopy = [], H = 3.0, L = 12;
  bankSeg(A, [-L / 2, 0], [L / 2, 0], H, 11, 2.6);
  A.box([-L / 2, H, 0.75], [L / 2, H + 0.85, 1.35], "bank_soil");   // firing parapet
  for (let x = -L / 2 + 1; x < L / 2 - 0.5; x += 1.5 + R()) canopy.push([r3(x), r3(H + 0.9 + R() * 0.4), r3(-1.6 - R() * 1.5), r3(0.6 + R() * 0.3)]);
  bankTree(A, R, -3.5, -0.6, H, canopy, 5.0); bankTree(A, R, 3.8, -0.4, H, canopy, 5.5);
  setSnap(A, [sock([-L / 2, 0, 0], [-1, 0], "rampart"), sock([L / 2, 0, 0], [1, 0], "rampart")]);   // V6: open ends
  foliage(A, canopy, 7606, [0.15, 0.22, 0.08]);
  const fp = []; for (const x of [-4.5, -1.5, 1.5, 4.5]) fp.push(fire([x, H, 0.25], 0, "firestep"));
  setNav(A, { effect: "block", firingPoints: fp, cover: [-4, 0, 4].map((x) => cover([x, H, 0.15], 0, "low", ["over"])) });
  setTour(A, { pos: [0.5, 0, -9.0], yaw: 180 }, [
    { goto: [0.5, -5.0] }, { goto: [-1.5, 0.25], expectY: H, note: "up onto the rampart walk" },
    { look: [-1.5, H + 1.6, 12], hold: 0.5 }, { shot: "mpu_rampart_walk" },
  ]);
  return A;
}

// ============================================================================ abatis
// felled trees laid crown-first toward the enemy (+Z), interlaced: the trunks are solid, the tangle of branches is a
// player-only volume (rounds and sight pass, nobody walks through)
export function abatis() {
  const A = new Asset({ id: "bcg_abatis", version: 2, name: "Abatis (felled trees, 8 m)", category: "Bocage",
    description: "Abatis across a forest trail: four felled trees laid crowns-first toward the enemy (+Z) and interlaced, the trunks staked down. The trunks stop rounds and give cover; the branch tangle cannot be walked through (rounds and sight pass).",
    floors: 0, enterable: false, footprint: [8.6, 8.0], plinthDepth: 0, maxGrade: 1.2, noAutoPanels: true });
  bocageMats(A);
  M(A, "burnt");
  const R = rng(7707);
  for (let k = 0; k < 4; k++) {
    const x0 = -3.3 + k * 2.2 + (R() - 0.5) * 0.3, yaw = (R() - 0.5) * 16, r = 0.4 + R() * 0.08, Lt = 4.5 + R() * 1.5;
    const pr = (yaw * Math.PI) / 180, z0 = -3.4, x1 = x0 + Math.sin(pr) * Lt, z1 = z0 + Math.cos(pr) * Lt;
    A.cone([(x0 + x1) / 2, r, (z0 + z1) / 2], [2 * r, Lt, 2 * r], "bark_grey", { top: 0.7, seg: 12, r: [90, yaw, 0] });   // round log, thinner to the crown
    A.cyl([x0 - Math.sin(pr) * 0.02, r, z0 - Math.cos(pr) * 0.02], [2 * r * 0.96, 0.05, 2 * r * 0.96], "log_raw", { seg: 12, r: [90, yaw, 0], k: 0 });   // saw cut
    for (let b = 0; b < 9; b++) {   // branches from the crown end
      const s = 0.45 + R() * 0.55, bx = x0 + Math.sin(pr) * Lt * s, bz = z0 + Math.cos(pr) * Lt * s;
      const by = yaw + (R() - 0.5) * 120, len = 1.4 + R() * 1.8, pit = 15 + R() * 50, bpr = (by * Math.PI) / 180;
      seg(A, [bx, r, bz], [bx + Math.sin(bpr) * Math.cos(pit * D2R) * len * 0.8, r + Math.sin(pit * D2R) * len * 0.8, bz + Math.cos(bpr) * Math.cos(pit * D2R) * len * 0.8], 0.05, 0.015, "bark", 5);
      A.rbox([bx + Math.sin(bpr) * len * 0.8 - 0.35, r + 0.5, bz + Math.cos(bpr) * len * 0.8 - 0.35], [bx + Math.sin(bpr) * len * 0.8 + 0.35, r + 1.0 + R() * 0.4, bz + Math.cos(bpr) * len * 0.8 + 0.35], 0.2, "reed", { k: 0 });   // withered leaves
    }
    A.box([x0 - 0.05, -0.3, z0 + 0.3], [x0 + 0.05, 0.6, z0 + 0.4], "picket", { k: 0 });
  }
  M(A, "picket");
  A.box([-4.3, 0, -1.0], [4.3, 1.6, 3.6], "bark", { v: 0 }).k = 2;   // the tangle: player-only traversal volume
  setNav(A, { effect: "block", cover: [-3.3, -1.1, 1.1, 3.3].map((x) => cover([x, 0, -3.8], 0, "low", ["over"])) });
  setTour(A, { pos: [0.3, 0, -7.0], yaw: 180 }, [{ goto: [0.3, -4.3] }, { look: [0.3, 1.0, 2], hold: 0.5 }, { shot: "mpu_abatis" }]);
  return A;
}

// ============================================================================ marsh / reed patch
// an irregular patch of standing water and mud in a shallow terrain dip, reed beds and tussocks; nothing collides
export function marshPatch() {
  const A = new Asset({ id: "bcg_marsh_patch", version: 1, name: "Marsh / reed patch", category: "Bocage",
    description: "Marsh patch as at the foot of Mont-Castre: about 14 x 10 m of standing water and mud in a shallow dip (cut into the ground), reed beds and grass tussocks. A marsh nav area (bots pay 3x to cross); slows nothing in Explore yet.",
    floors: 0, enterable: false, footprint: [14, 10], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  bocageMats(A);
  const R = rng(7808);
  const outline = [];
  for (let k = 0; k < 14; k++) { const t = (k / 14) * Math.PI * 2, rr = 0.82 + R() * 0.18; outline.push([r3(Math.cos(t) * 6.6 * rr), r3(Math.sin(t) * 4.6 * rr)]); }
  const D = 0.35;
  for (const [x, z, sx, sz] of [[0, 0, 9.5, 6.0], [-2.5, 1.2, 5.0, 4.5], [3.0, -1.0, 5.0, 4.0]]) A.box([x - sx / 2, -D + 0.1, z - sz / 2], [x + sx / 2, -D + 0.12, z + sz / 2], "water", { k: 0 });
  A.box([-5.5, -D + 0.02, -3.6], [5.5, -D + 0.1, 3.6], "mud", { k: 0 });
  for (let k = 0; k < 26; k++) {   // reed clumps
    const t = R() * Math.PI * 2, rr = Math.sqrt(R()), x = Math.cos(t) * 6.0 * rr, z = Math.sin(t) * 4.2 * rr;
    for (let j = 0; j < 7; j++) {
      const h = 1.2 + R() * 0.9;
      A.obox([x + (R() - 0.5) * 0.6, -D + h / 2, z + (R() - 0.5) * 0.6], [0.035, h, 0.035], [(R() - 0.5) * 18, R() * 180, (R() - 0.5) * 18], R() < 0.6 ? "reed" : "reed_green", { k: 0 });
    }
  }
  for (let k = 0; k < 10; k++) { const x = (R() * 2 - 1) * 5, z = (R() * 2 - 1) * 3.4; A.rbox([x - 0.35, -D - 0.05, z - 0.35], [x + 0.35, -D + 0.35, z + 0.35], 0.18, "reed_green", { k: 0 }); }
  setNav(A, { effect: "area", area: "marsh", areas: [{ outline, area: "marsh" }], cover: [] }, [{ outline, depth: D, bank: 1.2 }]);
  setTour(A, { pos: [0.3, 0, -8.0], yaw: 180 }, [{ goto: [0.3, -2.0], note: "wade in" }, { goto: [0.5, 2.0] }, { look: [0, 0.5, 6], hold: 0.4 }, { shot: "mpu_marsh" }], { cut: true });
  return A;
}

export function bocageAssets() {
  return [hedgerowStraight(), hedgerowCorner(), hedgerowGap(), sunkenLane(), quarryFace(), earthRampart(), abatis(), marshPatch()];
}
void extendEnds; void band;
