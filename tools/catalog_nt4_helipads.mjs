// NT4: two helicopter pads for the outpost maps, built from the user's reference picture (our own geometry):
//   mod_helipad_raised: an octagonal concrete deck 18 m across, 2.8 m up on square piers with cross beams, a worn
//     yellow circle and "H", a pipe railing, and a two-flight concrete stair with a landing and rusty handrails / mesh.
//   mod_helipad_ground: a 20 m square of 4 x 5 m concrete slabs with open joints, mossy and stained, a faded yellow
//     rounded-square outline with "H", a railing along one side.
// Both fit an AH-64 (15 m rotor). Paint sits in thin layers 4-8 mm above the concrete (no coplanar faces); the stairs
// are visible treads over one invisible ramp each (players and bots walk them). Grit: chipped curbs, cracks, oil spots,
// moss, rust runs on the piers, all seeded.
import { Asset } from "./kit.mjs";

const R3 = (v) => Math.round(v * 1000) / 1000;
function rnd(seed, k) { const x = Math.sin(seed * 12.9898 + k * 78.233) * 43758.5453; return x - Math.floor(x); }

function mats(A) {
  A.mat("deck", [0.5, 0.49, 0.46], 0.95, "concrete", { texture: "concrete_aged" });   // NT5: the stained set read as blotches across the deck
  A.mat("deck_b", [0.47, 0.46, 0.43], 0.96, "concrete", { texture: "concrete_dirty_wet" });
  A.mat("deck_c", [0.52, 0.5, 0.46], 0.95, "concrete", { texture: "concrete_aged" });
  A.mat("pier", [0.46, 0.45, 0.42], 0.97, "concrete", { texture: "bunker_weathered" });
  A.mat("paint_y", [0.6, 0.47, 0.1], 0.85, "concrete", { texture: "concrete_aged" });   // faded: the concrete grain shows through
  A.mat("rail", [0.32, 0.2, 0.12], 0.8, "metal", { texture: "steel_rust_light", metallic: 0.2 });
  A.mat("rail_dark", [0.22, 0.14, 0.09], 0.85, "metal", { texture: "rust_heavy", metallic: 0.15 });
  A.mat("joint", [0.12, 0.11, 0.1], 1.0);
  A.mat("oil", [0.17, 0.16, 0.14], 0.6);
  A.mat("moss", [0.17, 0.21, 0.08], 1.0);
  A.mat("stain", [0.3, 0.27, 0.22], 0.98);
  A.mat("rust_run", [0.36, 0.2, 0.1], 0.95);
}

// a flat disc (n-gon) lying at height top (its top face), thickness h; no collider
function disc(A, x, z, d, top, h, m, n = 24, rot = 0) { return A.cone([x, top - h / 2, z], [d, h, d], m, { n, top: 1, k: 0, r: [0, rot, 0] }); }
// a pipe between two points (box rotated to the segment)
function pipe(A, a, b, w, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  const yaw = Math.atan2(d[0], d[2]) * 180 / Math.PI, pitch = -Math.asin(d[1] / L) * 180 / Math.PI;
  return A.obox([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [w, w, L], [pitch, yaw, 0], m, o);
}
// grit on a horizontal surface: oil spots, stains, moss at the edges
function grit(A, seed, cx, cz, rad, top, nOil, nStain, nMoss) {
  for (let k = 0; k < nOil; k++) { const a = rnd(seed, k) * 6.283, r = rnd(seed, k + 50) * rad * 0.7; disc(A, cx + Math.cos(a) * r, cz + Math.sin(a) * r, 0.3 + rnd(seed, k + 90) * 0.7, top + 0.02, 0.01, "oil", 10, rnd(seed, k + 7) * 90); }
  for (let k = 0; k < nStain; k++) { const a = rnd(seed, k + 200) * 6.283, r = rnd(seed, k + 250) * rad; disc(A, cx + Math.cos(a) * r, cz + Math.sin(a) * r, 1.0 + rnd(seed, k + 290) * 2.2, top + 0.01, 0.01, "stain", 9, rnd(seed, k + 17) * 90); }
  for (let k = 0; k < nMoss; k++) { const a = rnd(seed, k + 400) * 6.283, r = rad * (0.82 + rnd(seed, k + 450) * 0.15); disc(A, cx + Math.cos(a) * r, cz + Math.sin(a) * r, 0.6 + rnd(seed, k + 490) * 1.6, top + 0.03, 0.01, "moss", 9, rnd(seed, k + 27) * 90); }
}
// an "H" lying at height top, centred, legs along z
function letterH(A, cx, cz, top, s, m) {
  for (const sx of [-1, 1]) A.box([cx + sx * 1.15 * s - 0.45 * s, top - 0.012, cz - 2.3 * s], [cx + sx * 1.15 * s + 0.45 * s, top, cz + 2.3 * s], m, { k: 0 });
  A.box([cx - 0.7 * s, top - 0.0125, cz - 0.42 * s], [cx + 0.7 * s, top - 0.0005, cz + 0.42 * s], m, { k: 0 });
}
// NT5: a straight flight, rebuilt clean: even risers (~0.175 m) and goings, each tread a solid block (down to the ground
// for a ground flight, 0.32 m deep on a raised one), a stringer plate each side along the pitch, handrails on posts
// following the nosing line, and one invisible ramp (players and bots walk it). from = the top nosing, to = the bottom.
function flight(A, from, to, w, m, railM, solid) {
  const along = Math.abs(to[0] - from[0]) > Math.abs(to[2] - from[2]) ? 0 : 2, side = along === 0 ? 2 : 0;
  const run = to[along] - from[along], rise = to[1] - from[1], n = Math.max(2, Math.round(Math.abs(rise) / 0.175));
  const sRun = run / n, sRise = rise / n;
  for (let i = 0; i < n - 1; i++) {   // the last riser lands on the landing / ground itself
    const a0 = from[along] + sRun * i, a1 = a0 + sRun, top = from[1] + sRise * (i + 1);
    const mn = [0, 0, 0], mx = [0, 0, 0];
    mn[along] = Math.min(a0, a1); mx[along] = Math.max(a0, a1);
    mn[1] = solid ? Math.min(from[1], to[1]) - 0.05 : top - 0.32; mx[1] = top;
    mn[side] = from[side] - w / 2; mx[side] = from[side] + w / 2;
    A.box(mn, mx, m, { k: 0, bv: 0.012 });
  }
  const L = Math.hypot(run, rise), ang = Math.atan2(Math.abs(rise), Math.abs(run)) * 180 / Math.PI, up = Math.sign(rise) * Math.sign(run);
  const rot = along === 2 ? [-ang * up, 0, 0] : [0, 0, ang * up];
  const c = [(from[0] + to[0]) / 2, (from[1] + to[1]) / 2 - 0.12, (from[2] + to[2]) / 2];
  A.obox(c, along === 2 ? [w, 0.2, L] : [L, 0.2, w], rot, m, { v: 0 });   // the walkable ramp
  for (const sgn of [-1, 1]) {
    const o = [0, 0, 0]; o[side] = sgn * (w / 2 + 0.05);
    const sc = [c[0] + o[0], c[1] - 0.02, c[2] + o[2]];
    A.obox(sc, along === 2 ? [0.08, 0.34, L] : [L, 0.34, 0.08], rot, railM, { k: 0 });   // stringer plate
    const a = [from[0] + o[0], from[1] + 0.95, from[2] + o[2]], b = [to[0] + o[0], to[1] + 0.95, to[2] + o[2]];
    pipe(A, a, b, 0.05, railM, { k: 0 });                                                   // handrail
    pipe(A, [a[0], a[1] - 0.48, a[2]], [b[0], b[1] - 0.48, b[2]], 0.035, railM, { k: 0 });  // knee rail
    for (const t of [0, 0.5, 1]) { const p = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]; A.box([p[0] - 0.03, p[1] - 0.98, p[2] - 0.03], [p[0] + 0.03, p[1] + 0.02, p[2] + 0.03], railM, { k: 0 }); }
  }
}

function raised() {
  const A = new Asset({ id: "mod_helipad_raised", version: 2, name: "Helipad (raised)", category: "Modern: Military",
    description: "An octagonal concrete helipad 18 m across, 2.8 m up on square piers with cross beams: a worn yellow circle and H, a rusty pipe railing, and a two-flight concrete stair with a landing. Takes a parked attack helicopter.",
    footprint: [19, 24], floors: 0, enterable: false, plinthDepth: 0.4, maxGrade: 1.5, noAutoPanels: true,
    nav: { effect: "carve" } });
  mats(A);
  // NT5: the octagon's flats face the axes (no rotation), so every edge is known exactly: inradius 9 m, edges 7.46 m
  const top = 2.8, D = 18, inR = D / 2, edge = 2 * inR * Math.tan(Math.PI / 8), oz = 2.0;
  A.cone([0, top - 0.2, oz], [D, 0.4, D], "deck", { n: 8, top: 1 });   // the deck (walkable collider)
  A.cone([0, top - 0.48, oz], [D - 0.6, 0.16, D - 0.6], "pier", { n: 8, top: 1, k: 0 });   // the slab soffit edge
  const P = [-6.2, 0, 6.2];
  for (const x of P) for (const z of P) {
    A.rbox([x - 0.35, 0, oz + z - 0.35], [x + 0.35, top - 0.4, oz + z + 0.35], 0.04, "pier");
    if (rnd(x * 3 + z, 1) < 0.6) A.box([x + 0.351, 0.4, oz + z - 0.1], [x + 0.36, top - 0.6, oz + z + 0.05], "rust_run", { k: 0 });
  }
  for (const x of P) A.box([x - 0.25, top - 0.85, oz - 6.2], [x + 0.25, top - 0.4, oz + 6.2], "pier", { k: 0 });
  for (const z of P) A.box([-6.2, top - 0.8, oz + z - 0.22], [6.2, top - 0.42, oz + z + 0.22], "pier", { k: 0 });
  const w = 1.5, xA = 0.0, zEdge = oz - inR;   // the stair leaves from the -z edge's middle
  for (let k = 0; k < 8; k++) {   // curb and railing along each edge, set 0.15 m in; rails stop at corner posts
    const a = k * Math.PI / 4, nx = Math.sin(a), nz = Math.cos(a), tx = Math.cos(a), tz = -Math.sin(a);
    const r = inR - 0.15, half = edge / 2 - 0.15 * Math.tan(Math.PI / 8) - 0.04;
    const cx = nx * r, cz = oz + nz * r;
    A.obox([cx, top + 0.08, cz], [edge - 0.2, 0.16, 0.22], [0, a * 180 / Math.PI, 0], "deck_c", { k: 0, bv: 0.03 });   // along the edge (+90 laid it radially: bars jutting out)
    const p0 = [cx - tx * half, cz - tz * half], p1 = [cx + tx * half, cz + tz * half];
    const stairEdge = k === 4, g = (w / 2 + 0.06) / (2 * half);
    const segs = stairEdge ? [[0, 0.5 - g], [0.5 + g, 1]] : [[0, 1]];
    for (const [t0, t1] of segs) {
      const q0 = [p0[0] + (p1[0] - p0[0]) * t0, p0[1] + (p1[1] - p0[1]) * t0], q1 = [p0[0] + (p1[0] - p0[0]) * t1, p0[1] + (p1[1] - p0[1]) * t1];
      pipe(A, [q0[0], top + 1.0, q0[1]], [q1[0], top + 1.0, q1[1]], 0.055, "rail");
      pipe(A, [q0[0], top + 0.52, q0[1]], [q1[0], top + 0.52, q1[1]], 0.04, "rail", { k: 0 });
      for (const q of [q0, q1]) A.cyl([q[0], top + 0.58, q[1]], [0.065, 0.84, 0.065], "rail", { k: 0 });   // posts at the ends
    }
  }
  // markings: a yellow ring and the H, each layer a few mm higher; a few faint oil drips near the centre (no blotches)
  disc(A, 0, oz, 13.2, top + 0.012, 0.012, "paint_y", 48);
  disc(A, 0, oz, 11.6, top + 0.024, 0.024, "deck", 48);
  letterH(A, 0, oz, top + 0.036, 1.0, "paint_y");
  for (let k = 0; k < 4; k++) disc(A, (rnd(k, 41) - 0.5) * 3.0, oz + (rnd(k, 42) - 0.5) * 3.0, 0.18 + rnd(k, 43) * 0.25, top + 0.046, 0.006, "oil", 10, rnd(k, 44) * 90);
  // the stair: flight 1 from the deck edge down to a landing at 1.4 m, flight 2 along -x to the ground
  const go = 0.28, n1 = 8;
  const zL = zEdge - n1 * go;   // the landing's near edge (eight goings of 0.28 m)
  flight(A, [xA, top, zEdge], [xA, 1.4, zL], w, "deck_c", "rail", false);
  A.box([xA - w / 2 - 0.05, 1.15, zL - w - 0.1], [xA + w / 2 + 0.05, 1.4, zL], "deck_c", { bv: 0.03 });   // landing
  A.rbox([xA - 0.3, 0, zL - w + 0.15], [xA + 0.3, 1.15, zL - 0.35], 0.03, "pier");                      // its pier
  const zm = zL - w / 2 - 0.05;
  flight(A, [xA - w / 2 - 0.05, 1.4, zm], [xA - w / 2 - 0.05 - n1 * go, 0.0, zm], w, "deck_c", "rail", true);
  pipe(A, [xA + w / 2 + 0.1, 2.4, zL], [xA + w / 2 + 0.1, 2.4, zL - w - 0.1], 0.05, "rail", { k: 0 });       // landing rails, on posts
  pipe(A, [xA - w / 2 - 0.05, 2.4, zL - w - 0.15], [xA + w / 2 + 0.1, 2.4, zL - w - 0.15], 0.05, "rail", { k: 0 });
  for (const [px, pz] of [[xA + w / 2 + 0.1, zL], [xA + w / 2 + 0.1, zL - w - 0.15], [xA - w / 2 - 0.05, zL - w - 0.15]]) A.box([px - 0.03, 1.4, pz - 0.03], [px + 0.03, 2.42, pz + 0.03], "rail", { k: 0 });
  return A;
}

function ground() {
  const A = new Asset({ id: "mod_helipad_ground", version: 1, name: "Helipad (ground)", category: "Modern: Military",
    description: "A 20 m square helipad of 4 x 5 m concrete slabs with open joints, mossy and stained, a faded yellow rounded-square outline with an H and a railing along one side.",
    footprint: [21, 20], floors: 0, enterable: false, plinthDepth: 0.3, maxGrade: 1.0, noAutoPanels: true, nav: { effect: "none" } });
  mats(A);
  const top = 0.14, J = 0.035;
  A.box([-10, -0.25, -10], [10, top - 0.05, 10], "joint");   // the bed under the open joints
  const ms = ["deck", "deck_b", "deck_c"];
  let k = 0;
  for (let i = 0; i < 5; i++) for (let j = 0; j < 4; j++) {   // 5 x 4 slabs, 4 m x 5 m
    const x0 = -10 + i * 4 + J / 2, z0 = -10 + j * 5 + J / 2, dy = (rnd(k, 1) - 0.5) * 0.02;
    A.box([x0, -0.05, z0], [x0 + 4 - J, top + dy, z0 + 5 - J], ms[Math.floor(rnd(k, 2) * 3)], { bv: 0.025 });
    if (rnd(k, 3) < 0.45) A.box([x0 + 0.3 + rnd(k, 4) * 2.5, top + dy + 0.0005, z0 + 0.2], [x0 + 0.33 + rnd(k, 4) * 2.5, top + dy + 0.004, z0 + 0.2 + 1.5 + rnd(k, 5) * 2.5], "joint", { k: 0 });   // a crack
    k++;
  }
  // markings over the slabs (a few mm above the highest slab top): rounded-square outline, H
  const pt = top + 0.04, S = 11.5, wd = 0.45, rr = 1.6;
  A.box([-S / 2 + rr, pt - 0.006, -S / 2], [S / 2 - rr, pt, -S / 2 + wd], "paint_y", { k: 0 });
  A.box([-S / 2 + rr, pt - 0.006, S / 2 - wd], [S / 2 - rr, pt, S / 2], "paint_y", { k: 0 });
  A.box([-S / 2, pt - 0.006, -S / 2 + rr], [-S / 2 + wd, pt, S / 2 - rr], "paint_y", { k: 0 });
  A.box([S / 2 - wd, pt - 0.006, -S / 2 + rr], [S / 2, pt, S / 2 - rr], "paint_y", { k: 0 });
  for (const [cx, cz, a0] of [[S / 2 - rr, S / 2 - rr, 0], [-S / 2 + rr, S / 2 - rr, 90], [-S / 2 + rr, -S / 2 + rr, 180], [S / 2 - rr, -S / 2 + rr, 270]]) {
    for (let s = 0; s < 4; s++) {   // the rounded corners: four short chords
      const a = (a0 + 11.25 + s * 22.5) * Math.PI / 180, r = rr - wd / 2;
      A.obox([cx + Math.cos(a) * r, pt - 0.003, cz + Math.sin(a) * r], [wd, 0.006, 2 * r * Math.sin(Math.PI / 16) + 0.06], [0, -a * 180 / Math.PI, 0], "paint_y", { k: 0 });
    }
  }
  letterH(A, 0, 0, pt, 0.95, "paint_y");
  for (let q = 0; q < 4; q++) disc(A, (rnd(q, 61) - 0.5) * 3.0, (rnd(q, 62) - 0.5) * 3.0, 0.18 + rnd(q, 63) * 0.25, pt + 0.012, 0.006, "oil", 10, rnd(q, 64) * 90);   // NT5: a few faint oil drips, no blotches
  // the railing along the +x side
  const x = 10.25;
  pipe(A, [x, 1.0, -9.8], [x, 1.0, 9.8], 0.055, "rail");
  pipe(A, [x, 0.5, -9.8], [x, 0.5, 9.8], 0.04, "rail", { k: 0 });
  for (let z = -9.8; z <= 9.81; z += 2.45) A.cyl([x, 0.5, z], [0.06, 1.0, 0.06], "rail", { k: 0 });
  return A;
}

export function nt4HelipadAssets() { return [raised(), ground()]; }
