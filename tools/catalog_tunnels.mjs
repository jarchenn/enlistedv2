// UG1 tunnel kit (docs/tunnels.md): closed underground passages for the labyrinth map (tools/mapgen/maps/tunnels.py).
// Every piece fills one 8 m cell (chambers 3 x 3 cells, stairs one cell) and is a closed shell: a full-cell floor slab,
// a full-cell ceiling slab and a rock / concrete skin round the air, so nothing outside a passage is visible or reachable.
// The air is laid out on a 0.25 m voxel grid: the hub and a spoke to each open side, the spokes kinked, bulging and
// niched by variant; a spoke always reaches the cell edge straight and exactly PORTAL wide and PORTAL_H high, so any
// two pieces meet without a gap whatever their variants. The skin is the solid voxels next to the air, merged into
// boxes (rough, stepped walls). Dressing: timber shoring frames, a rusty pipe run on one wall, rock lumps, mud and
// puddles. Ids "tun_*" (never furnished: interior.cpp autoFurnishable).
import { Asset } from "./kit.mjs";
import { rng, room, cover, setNav, rect } from "./catalog_atlantic.mjs";

const CELL = 8, VOX = 0.25, N = CELL / VOX, PORTAL = 2.5, H = 2.75, PORTAL_H = 2.6, FLOOR_T = 0.3, CEIL_T = 0.35;
export const TUNNEL_LEVEL = 3.6;   // the upper level's floor height (stairs rise this much)
const r3 = (v) => Math.round(v * 1000) / 1000;

function tmats(A) {
  A.mat("rock", [0.27, 0.25, 0.21], 0.92, "stone");
  A.mat("rock_dark", [0.17, 0.155, 0.135], 0.94, "stone");
  A.mat("concrete_wash", [0.33, 0.32, 0.29], 0.9, "plain");
  A.mat("concrete_floor", [0.26, 0.25, 0.23], 0.9, "plain");
  A.mat("mud", [0.13, 0.105, 0.08], 0.75, "plain");
  // UG4: textured floors with strong relief (slots tun_floor_* in content/textures/textures.json), varied per piece
  A.mat("floor_rock", [0.2, 0.18, 0.15], 0.9, "plain", { texture: "tun_floor_rock" });
  A.mat("floor_rock2", [0.17, 0.155, 0.13], 0.9, "plain", { texture: "tun_floor_rock2" });
  A.mat("floor_gravel", [0.19, 0.17, 0.14], 0.9, "plain", { texture: "tun_floor_gravel" });
  A.mat("floor_rubble", [0.18, 0.16, 0.135], 0.9, "plain", { texture: "tun_floor_rubble" });
  A.mat("floor_mud", [0.14, 0.115, 0.09], 0.8, "plain", { texture: "tun_floor_mud" });
  A.mat("floor_slab", [0.25, 0.24, 0.22], 0.9, "plain", { texture: "tun_floor_slab" });
  A.mat("puddle", [0.02, 0.025, 0.022], 0.05, "plain");
  A.mat("rust", [0.2, 0.095, 0.045], 0.8, "metal", { metallic: 0.2 });
  A.mat("steel", [0.12, 0.11, 0.1], 0.6, "metal", { metallic: 0.5 });
  A.mat("plank_y", [0.2, 0.15, 0.1], 0.9, "wood", { grain: 1 });
  A.mat("plank_x", [0.2, 0.15, 0.1], 0.9, "wood", { grain: 0 });
  A.mat("scorch", [0.05, 0.045, 0.04], 0.95, "plain");
}
// UG2: the distance from (x, z) along (px, pz) to the first solid voxel (the wall)
function wallDist(g, x, z, px, pz) {
  for (let d = 0; d < CELL / 2; d += 0.05) {
    const i = Math.floor((x + px * d + CELL / 2) / VOX), j = Math.floor((z + pz * d + CELL / 2) / VOX);
    if (i < 0 || j < 0 || i >= N || j >= N || !g.a[j * N + i]) return d;
  }
  return CELL / 2;
}
// UG2: a shell crater on the floor: a scorched dish ringed by a low spoil rim and broken rock
// UG3: thin decals each get their own height step (coplanar overlapping decals z-fought: the "flicker" in the tunnels)
let decalK = 0;
const decalY = () => 0.003 + ((decalK++ % 8) * 0.004);
// UG4: real relief over a floor: low mounds (colliding, under step height; flatter and non-colliding in the middle
// band where the track runs), embedded rocks, loose stones, shallow hollows (dark decals), rubble drifts at the wall
// feet, broken slab edges on concrete. inAir(x, z) says whether a point is inside the passage.
const FLOORS = ["floor_rock", "floor_rock2", "floor_gravel", "floor_rubble", "floor_mud"];
function floorRelief(A, R, inAir, lined, n = 28, half = CELL / 2) {
  const D = { k: 0, lod: 1 }, mound = lined ? "concrete_floor" : "rock_dark";
  for (let k = 0, tries = 0; k < n && tries < n * 6; tries++) {
    const x = (R() - 0.5) * 2 * half, z = (R() - 0.5) * 2 * half;
    if (!inAir(x, z)) continue;
    k++;
    const band = Math.abs(x) < 0.8 || Math.abs(z) < 0.8, w = 0.5 + R() * 1.1, d = 0.4 + R() * 0.9;
    const hgt = band ? 0.04 + R() * 0.03 : 0.07 + R() * 0.08;
    if (lined && R() < 0.5) A.obox([r3(x), r3(hgt * 0.5), r3(z)], [r3(w * 0.8), 0.05, r3(d * 0.7)], [r3((R() - 0.5) * 14), r3(R() * 90), r3((R() - 0.5) * 14)], "floor_slab", band ? D : { lod: 1 });   // a broken slab edge
    else A.obox([r3(x), 0, r3(z)], [r3(w), r3(hgt * 2), r3(d)], [r3((R() - 0.5) * 8), r3(R() * 180), r3((R() - 0.5) * 8)], R() < 0.4 ? mound : FLOORS[Math.floor(R() * FLOORS.length)], band ? D : { lod: 1 });
  }
  for (let k = 0, tries = 0; k < n * 0.8 && tries < n * 5; tries++) {   // loose and embedded stones
    const x = (R() - 0.5) * 2 * half, z = (R() - 0.5) * 2 * half;
    if (!inAir(x, z)) continue;
    k++;
    const s = 0.07 + R() * 0.16;
    A.obox([r3(x), r3(s * (R() < 0.5 ? 0.15 : 0.35)), r3(z)], [r3(s * 1.4), r3(s), r3(s * 1.1)], [r3(R() * 40), r3(R() * 180), r3(R() * 40)], R() < 0.5 ? "rock" : "rock_dark", D);
  }
  for (let k = 0, tries = 0; k < 4 && tries < 30; tries++) {   // shallow hollows (dark, on their own height step)
    const x = (R() - 0.5) * 2 * half, z = (R() - 0.5) * 2 * half;
    if (!inAir(x, z)) continue;
    k++;
    const y0 = decalY(), r = 0.4 + R() * 0.6;
    A.cyl([r3(x), r3(y0), r3(z)], [r3(r * 2), 0.003, r3(r * 1.4)], "mud", { k: 0, n: 10 });
  }
  for (let k = 0, tries = 0; k < 7 && tries < 80; tries++) {   // rubble drifts at the wall feet
    const x = (R() - 0.5) * 2 * half, z = (R() - 0.5) * 2 * half;
    if (!inAir(x, z) || (inAir(x + 0.5, z) && inAir(x - 0.5, z) && inAir(x, z + 0.5) && inAir(x, z - 0.5))) continue;
    k++;
    for (let q = 0; q < 4; q++) { const s = 0.18 + R() * 0.3; A.obox([r3(x + (R() - 0.5) * 0.7), r3(s * 0.3), r3(z + (R() - 0.5) * 0.7)], [r3(s * 1.5), r3(s), r3(s * 1.2)], [r3(R() * 40), r3(R() * 180), r3(R() * 40)], q % 2 ? "rock" : "mud", D); }
  }
}
function floorCrater(A, R, x, z, r) {
  const D = { k: 0, lod: 1 }, y0 = decalY();
  A.cyl([r3(x), r3(y0), r3(z)], [r, 0.004, r], "scorch", { k: 0, n: 12 });
  A.cyl([r3(x), r3(y0 + 0.004), r3(z)], [r * 0.45, 0.004, r * 0.45], "puddle", { k: 0, n: 10 });
  for (let k = 0; k < 9; k++) {
    const a = (k / 9) * Math.PI * 2 + R() * 0.4, rr = r * (0.95 + R() * 0.2), sz = 0.25 + R() * 0.3;
    A.obox([r3(x + Math.cos(a) * rr), r3(sz * 0.3), r3(z + Math.sin(a) * rr)], [sz * 1.6, sz * 0.7, sz], [R() * 20, (-a * 180) / Math.PI, R() * 20], R() < 0.5 ? "mud" : "rock", D);
  }
}

// ---------------------------------------------------------------- the air grid
// sides: N (+z), E (+x), S (-z), W (-x); a voxel (i, j) covers x in [-4 + i * VOX, ...], z likewise
const SIDE = { N: [0, 1], E: [1, 0], S: [0, -1], W: [-1, 0] };
function Grid() { return { a: new Uint8Array(N * N) }; }
const vc = (i) => -CELL / 2 + (i + 0.5) * VOX;   // voxel centre
function disc(g, x, z, r) {
  for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) { const dx = vc(i) - x, dz = vc(j) - z; if (dx * dx + dz * dz <= r * r) g.a[j * N + i] = 1; }
}
function box(g, x0, z0, x1, z1) {
  for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) { const x = vc(i), z = vc(j); if (x >= x0 && x <= x1 && z >= z0 && z <= z1) g.a[j * N + i] = 1; }
}
// a spoke from the hub to side s: the last 1.25 m exactly PORTAL wide and straight on the axis; before that it wanders
// by `kink` m across and widens to `bulge` m
function spoke(g, s, R, { kink = 0, bulge = PORTAL, niche = false } = {}) {
  const [dx, dz] = SIDE[s], px = -dz, pz = dx;   // along, across
  for (let t = 0; t <= CELL / 2; t += 0.05) {
    const u = t / (CELL / 2), off = kink * Math.sin(Math.min(1, u / 0.8) * Math.PI), w = t > CELL / 2 - 1.25 ? PORTAL : PORTAL + (bulge - PORTAL) * Math.sin(Math.min(1, u / 0.75) * Math.PI);
    const cx = dx * t + px * off, cz = dz * t + pz * off;
    if (t > CELL / 2 - 1.25) box(g, cx - (dx ? 0.03 : w / 2), cz - (dz ? 0.03 : w / 2), cx + (dx ? 0.03 : w / 2), cz + (dz ? 0.03 : w / 2));
    else disc(g, cx, cz, w / 2);
  }
  if (niche) { const t = 1.9, side = R() < 0.5 ? -1 : 1, nx = dx * t + px * side * (bulge / 2 + 0.4), nz = dz * t + pz * side * (bulge / 2 + 0.4); disc(g, nx, nz, 0.75); }
}
// solid voxels within one voxel of the air (8-neighbourhood): the wall skin
function skin(g) {
  const s = new Uint8Array(N * N);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    if (g.a[j * N + i]) continue;
    for (let dj = -1; dj <= 1 && !s[j * N + i]; dj++) for (let di = -1; di <= 1; di++) {
      const ii = i + di, jj = j + dj;
      if (ii >= 0 && jj >= 0 && ii < N && jj < N && g.a[jj * N + ii]) { s[j * N + i] = 1; break; }
    }
  }
  return s;
}
// greedy rectangles over a voxel mask
function rects(m) {
  const used = new Uint8Array(N * N), out = [];
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    if (!m[j * N + i] || used[j * N + i]) continue;
    let ie = i; while (ie + 1 < N && m[j * N + ie + 1] && !used[j * N + ie + 1]) ie++;
    let je = j;
    for (;;) { if (je + 1 >= N) break; let ok = true; for (let x = i; x <= ie && ok; x++) ok = m[(je + 1) * N + x] && !used[(je + 1) * N + x]; if (!ok) break; je++; }
    for (let y = j; y <= je; y++) for (let x = i; x <= ie; x++) used[y * N + x] = 1;
    out.push([-CELL / 2 + i * VOX, -CELL / 2 + j * VOX, -CELL / 2 + (ie + 1) * VOX, -CELL / 2 + (je + 1) * VOX]);
  }
  return out;
}

// ---------------------------------------------------------------- a passage piece
function passage(id, name, sides, opt = {}) {
  const R = rng(opt.seed || 1);
  const A = new Asset({ id, version: 1, name, category: "Tunnels", description: opt.desc || name, floors: 1, enterable: true,
    footprint: [CELL, CELL], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  tmats(A);
  const g = Grid(), hubR = opt.hub || 1.45;
  disc(g, 0, 0, hubR);
  for (const s of sides) spoke(g, s, R, (opt.spokes && opt.spokes[s]) || {});
  if (opt.room) box(g, -opt.room[0], -opt.room[1], opt.room[0], opt.room[1]);   // UG2: a small room at the hub
  const craters = [];
  for (let k = 0; k < (opt.craters || 0); k++) {   // UG2: deep blast craters in the walls (1.1-1.8 m into the rock)
    const s = sides[Math.floor(R() * sides.length)], [dx, dz] = SIDE[s], px = -dz, pz = dx;
    const t = 0.8 + R() * 1.7, sd = R() < 0.5 ? -1 : 1, r = 0.7 + R() * 0.45;
    const w = wallDist(g, dx * t, dz * t, px * sd, pz * sd), d = Math.min(w + r * 0.55, CELL / 2 - r - 0.35);
    disc(g, dx * t + px * sd * d, dz * t + pz * sd * d, r);
    craters.push([dx * t + px * sd * d, dz * t + pz * sd * d, r]);
  }
  const h = opt.h || H, lined = !!opt.lined, wallM = lined ? "concrete_wash" : "rock", rise = opt.rise || 0;
  const yAt = (z) => (rise * (z + CELL / 2)) / CELL;   // UG3: a ramp rises rise m from its south (-z) to its north (+z) portal
  if (!rise) {
    // shell: floor and ceiling over the whole cell, the skin from the floor to the ceiling
    A.box([-CELL / 2, -FLOOR_T, -CELL / 2], [CELL / 2, 0, CELL / 2], lined ? "floor_slab" : FLOORS[(opt.seed || 1) % FLOORS.length]);   // UG4: textured floors
    A.box([-CELL / 2, h, -CELL / 2], [CELL / 2, h + CEIL_T, CELL / 2], lined ? "concrete_wash" : "rock_dark");
  } else {
    const len = Math.hypot(CELL, rise) + 0.1, ang = (Math.atan2(rise, CELL) * 180) / Math.PI, cs = Math.cos((ang * Math.PI) / 180);
    A.obox([0, r3(rise / 2 - FLOOR_T / 2 / cs), 0], [CELL, FLOOR_T, r3(len)], [r3(-ang), 0, 0], FLOORS[(opt.seed || 1) % FLOORS.length]);
    A.obox([0, r3(rise / 2 + h + CEIL_T / 2 / cs), 0], [CELL, CEIL_T, r3(len)], [r3(-ang), 0, 0], "rock_dark");
    A.box([-CELL / 2, -FLOOR_T - 0.2, -CELL / 2], [CELL / 2, -FLOOR_T, -CELL / 2 + 0.6], "mud");   // seal under the low end
  }
  for (const [x0, z0, x1, z1] of rects(skin(g))) A.box([x0, -0.05, z0], [x1, rise + h + 0.02, z1], wallM);
  // the portal frames: each open side keeps the shared PORTAL_H head (a lintel band under the ceiling)
  for (const s of sides) {
    const [dx, dz] = SIDE[s], e = CELL / 2, y0 = dz > 0 ? rise : 0;
    if (h > PORTAL_H + 0.01 || rise) A.box(dx ? [dx > 0 ? e - 0.25 : -e, PORTAL_H, -PORTAL / 2] : [-PORTAL / 2, y0 + PORTAL_H, dz > 0 ? e - 0.25 : -e],
      dx ? [dx > 0 ? e : -e + 0.25, h + 0.02, PORTAL / 2] : [PORTAL / 2, y0 + h + (rise ? 0.35 : 0.02), dz > 0 ? e : -e + 0.25], wallM);
  }
  const dressFrom = A.parts.length;
  // dressing (detail only)
  const D = { k: 0, lod: 1 };
  for (const s of sides) {   // timber shoring frames across each spoke, a rusty pipe along one wall
    const [dx, dz] = SIDE[s], px = -dz, pz = dx;
    if (!lined && R() < 0.75) {
      const t = 1.6 + R() * 0.9, w = PORTAL / 2 + 0.05;
      for (const sd of [-1, 1]) A.box([dx * t + px * sd * w - 0.09, 0, dz * t + pz * sd * w - 0.09].map((v, k) => k === 1 ? 0 : v), [dx * t + px * sd * w + 0.09, h - 0.02, dz * t + pz * sd * w + 0.09], "plank_y", D);
      const ax = dx * t, az = dz * t;
      A.box([ax - (dx ? 0.1 : w + 0.1), h - 0.22, az - (dz ? 0.1 : w + 0.1)], [ax + (dx ? 0.1 : w + 0.1), h - 0.02, az + (dz ? 0.1 : w + 0.1)], "plank_x", D);
    }
    if (rise || opt.noPipe) continue;
    const ps = (opt.pipeSide || 1), off = PORTAL / 2 - 0.12, y = h - 0.42;
    const c0 = [px * ps * off, y, pz * ps * off], c1 = [dx * CELL / 2 + px * ps * off, y, dz * CELL / 2 + pz * ps * off];
    A.cyl([(c0[0] + c1[0]) / 2, y, (c0[2] + c1[2]) / 2], [0.09, CELL / 2, 0.09], "rust", { ...D, r: dx ? [0, 0, 90] : [90, 0, 0], n: 8 });
  }
  for (const [cx, cz, r] of craters) {   // UG2: spoil and broken rock in each crater, a scorched ceiling over it
    for (let k = 0; k < 4; k++) A.obox([r3(cx + (R() - 0.5) * r), r3(0.15 + R() * 0.2), r3(cz + (R() - 0.5) * r)], [0.4 + R() * 0.5, 0.3 + R() * 0.4, 0.4 + R() * 0.5], [R() * 40, R() * 90, R() * 40], "rock", D);
    A.cyl([r3(cx), r3(h - decalY()), r3(cz)], [r * 1.2, 0.003, r * 1.2], "scorch", { k: 0, n: 10 });
  }
  for (let k = 0; k < (opt.lumps ?? 14); k++) {   // rock protrusions (UG2: bigger, standing well out of the wall; the big ones collide)
    const s = sides[Math.floor(R() * sides.length)], [dx, dz] = SIDE[s], px = -dz, pz = dx, t = 0.6 + R() * 2.4, sd = R() < 0.5 ? -1 : 1;
    const big = !lined && R() < 0.45, sz = big ? 0.75 + R() * 0.55 : 0.35 + R() * 0.4, intr = big ? 0.3 + R() * 0.15 : 0.15 + R() * 0.2;
    const w = wallDist(g, dx * t, dz * t, px * sd, pz * sd), off = w + sz / 2 - intr;
    const x = dx * t + px * sd * off, z = dz * t + pz * sd * off, y = big && R() < 0.5 ? sz * 0.35 : 0.4 + R() * (h - 1.0);
    A.obox([r3(x), r3(y), r3(z)], [sz, sz * 0.85, sz], [R() * 40, R() * 90, R() * 40], lined ? "concrete_wash" : R() < 0.3 ? "rock_dark" : "rock", big ? { lod: 1 } : D);
  }
  for (let k = 0; k < (opt.floorCraters || 0); k++) { const s = sides[Math.floor(R() * sides.length)], t = 0.5 + R() * 1.5; floorCrater(A, R, SIDE[s][0] * t, SIDE[s][1] * t, 0.8 + R() * 0.3); }
  floorRelief(A, R, (x, z) => { const i = Math.floor((x + CELL / 2) / VOX), j = Math.floor((z + CELL / 2) / VOX); return i > 0 && j > 0 && i < N - 1 && j < N - 1 && !!g.a[j * N + i]; }, lined, rise ? 14 : 28);   // UG4
  for (let k = 0; k < (opt.puddles ?? 2); k++) {   // mud and puddles on the floor
    const x = (R() - 0.5) * 1.6, z = (R() - 0.5) * 1.6, w = 0.6 + R() * 1.0;
    const y0 = decalY(); A.box([r3(x - w / 2), r3(y0), r3(z - w * 0.35)], [r3(x + w / 2), r3(y0 + 0.003), r3(z + w * 0.35)], R() < 0.5 ? "puddle" : "mud", { k: 0 });
  }
  for (let k = 0; k < (opt.humps || 0); k++) {   // UG3: uneven humped rock floor (gentle mounds, walkable)
    const z = -2.6 + k * (5.2 / Math.max(1, (opt.humps || 1) - 1)), x = (R() - 0.5) * 0.8;
    A.obox([r3(x), 0.0, r3(z)], [r3(1.8 + R() * 0.6), r3(0.34 + R() * 0.16), r3(1.6 + R() * 0.8)], [r3((R() - 0.5) * 6), r3(R() * 30), r3((R() - 0.5) * 6)], "rock_dark", { lod: 1 });
  }
  if (opt.timbered) for (const t of [-3.4, -2.2, -1.0, 0.2, 1.4, 2.6, 3.6]) {   // UG3: close-set timber sets with lagging boards behind them
    const w = PORTAL / 2 + 0.08;
    for (const sd of [-1, 1]) A.box([sd * w - 0.1, 0, t - 0.1], [sd * w + 0.1, h - 0.02, t + 0.1], "plank_y", D);
    A.box([-w - 0.1, h - 0.24, t - 0.1], [w + 0.1, h - 0.02, t + 0.1], "plank_x", D);
    for (const sd of [-1, 1]) for (const y of [0.5, 1.1, 1.7, 2.3]) A.box([sd * (w + 0.12) - 0.02, y, t - 0.62], [sd * (w + 0.12) + 0.02, y + 0.22, t + 0.62], "plank_x", D);
  }
  if (opt.mesh) {   // UG3: roof mesh and bolt plates
    for (let x = -1.1; x <= 1.11; x += 0.3) A.box([r3(x - 0.008), h - 0.03, -CELL / 2 + 0.3], [r3(x + 0.008), h - 0.015, CELL / 2 - 0.3], "steel", D);
    for (let z = -3.6; z <= 3.61; z += 0.3) A.box([-1.15, h - 0.035, r3(z - 0.008)], [1.15, h - 0.02, r3(z + 0.008)], "steel", D);
    for (let k = 0; k < 10; k++) A.box([r3(-0.9 + (k % 3) * 0.9 - 0.07), h - 0.05, r3(-3.2 + Math.floor(k / 3) * 2.1 - 0.07)], [r3(-0.9 + (k % 3) * 0.9 + 0.07), h - 0.035, r3(-3.2 + Math.floor(k / 3) * 2.1 + 0.07)], "rust", D);
  }
  if (rise) for (let k = dressFrom; k < A.parts.length; k++) A.parts[k].c[1] = r3(A.parts[k].c[1] + yAt(A.parts[k].c[2]));   // dressing follows the slope
  if (opt.extra) opt.extra(A, R, h);
  // nav: the passage as rooms (hub + spokes), cover by the frames, snap sockets at the portals
  const hx = opt.room ? opt.room[0] : hubR, hz = opt.room ? opt.room[1] : hubR;
  const rooms = [room("hub", [-hx, 0, -hz], [hx, h + rise, hz])];
  for (const s of sides) { const [dx, dz] = SIDE[s]; rooms.push(room("sp_" + s, dx ? [dx > 0 ? 0 : -CELL / 2, 0, -PORTAL / 2] : [-PORTAL / 2, 0, dz > 0 ? 0 : -CELL / 2], dx ? [dx > 0 ? CELL / 2 : 0, h + rise, PORTAL / 2] : [PORTAL / 2, h + rise, dz > 0 ? CELL / 2 : 0])); }
  const py = (s) => (SIDE[s][1] > 0 ? rise : 0);
  setNav(A, { effect: "block", rooms, doors: [], entrances: sides.map((s) => ({ p: [SIDE[s][0] * CELL / 2, py(s), SIDE[s][1] * CELL / 2], dir: [SIDE[s][0], 0, SIDE[s][1]], room: "sp_" + s })),
    corners: [], firingPoints: [], cover: opt.cover || [], areas: [] });
  A.meta.snap = sides.map((s) => ({ p: [SIDE[s][0] * CELL / 2, py(s), SIDE[s][1] * CELL / 2], dir: [SIDE[s][0], SIDE[s][1]], kind: "tunnel" }));
  return A;
}

// ---------------------------------------------------------------- stairs between the levels (one cell, along z)
function stairs() {
  const A = new Asset({ id: "tun_stairs", version: 1, name: "Tunnel stairs (up 3.6 m)", category: "Tunnels", floors: 1, enterable: true,
    description: "A timber-shored stair drift rising 3.6 m from its south (-z) portal to its north portal: the link between the tunnel levels.",
    footprint: [CELL, CELL], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  tmats(A);
  const L = TUNNEL_LEVEL, z0 = -2.6, z1 = 2.6, w = PORTAL, hw = w / 2, sl = (L) / (z1 - z0), ang = (Math.atan2(L, z1 - z0) * 180) / Math.PI;
  const D = { k: 0, lod: 1 };
  A.box([-CELL / 2, -FLOOR_T, -CELL / 2], [CELL / 2, 0, z0], "mud");                      // the low landing
  A.box([-CELL / 2, L - FLOOR_T, z1], [CELL / 2, L, CELL / 2], "concrete_floor");          // the high landing
  const len = Math.hypot(z1 - z0, L);
  A.obox([0, L / 2 - 0.15, 0], [w + 0.1, 0.3, len], [-ang, 0, 0], "mud", { v: 0 });       // the walking ramp (collider)
  for (let k = 0; k < 18; k++) {   // visible treads (the ramp is their collider)
    const t0 = z0 + (k / 18) * (z1 - z0), t1 = z0 + ((k + 1) / 18) * (z1 - z0), y = (k + 1) * (L / 18);
    A.box([-hw, y - L / 18 - 0.02, t0], [hw, y, t1 + 0.02], k % 2 ? "plank_x" : "concrete_floor", { k: 0 });
  }
  // walls: full height over the drift, the ceiling stepping up with it
  A.box([-CELL / 2, -0.05, -CELL / 2], [-hw, L + PORTAL_H + 0.1, CELL / 2], "rock");
  A.box([hw, -0.05, -CELL / 2], [CELL / 2, L + PORTAL_H + 0.1, CELL / 2], "rock");
  A.box([-CELL / 2, PORTAL_H, -CELL / 2], [CELL / 2, PORTAL_H + CEIL_T, z0 - 0.25], "rock_dark");                // low ceiling
  A.obox([0, PORTAL_H + L / 2 + 0.18, 0], [w + 0.2, CEIL_T, len + 0.5], [-ang, 0, 0], "rock_dark");            // sloping ceiling
  A.box([-CELL / 2, L + PORTAL_H, z1 + 0.25], [CELL / 2, L + PORTAL_H + CEIL_T, CELL / 2], "rock_dark");        // high ceiling
  A.box([-hw, PORTAL_H - 0.1, z0 - 0.3], [hw, PORTAL_H + 1.0, z0], "rock");                                    // head walls closing the steps' gaps
  A.box([-hw, L - 0.2, z1], [hw, L + 0.05, z1 + 0.3], "rock");
  for (const z of [-2.0, 0, 2.0]) { const y = (z - z0) * sl; for (const sd of [-1, 1]) A.box([sd * (hw - 0.05) - 0.08, y, z - 0.08], [sd * (hw - 0.05) + 0.08, y + PORTAL_H - 0.1, z + 0.08], "plank_y", D); }
  setNav(A, { effect: "block", rooms: [room("low", [-hw, 0, -CELL / 2], [hw, PORTAL_H, z0]), room("flight", [-hw, 0, z0], [hw, L + PORTAL_H, z1]), room("high", [-hw, L, z1], [hw, L + PORTAL_H, CELL / 2])],
    doors: [], entrances: [{ p: [0, 0, -CELL / 2], dir: [0, 0, -1], room: "low" }, { p: [0, L, CELL / 2], dir: [0, 0, 1], room: "high" }], corners: [], firingPoints: [], cover: [], areas: [] });
  A.meta.snap = [{ p: [0, 0, -CELL / 2], dir: [0, -1], kind: "tunnel" }, { p: [0, L, CELL / 2], dir: [0, 1], kind: "tunnel" }];
  return A;
}

// ---------------------------------------------------------------- chambers (3 x 3 cells, a portal in the middle of each side)
function chamber(id, name, kind, seed, o = {}) {
  const R = rng(seed), S = 3 * CELL, hS = S / 2, h = o.h || 3.3, hw = o.hall || 8.5;   // UG2: hw = the hall's half size (medium rooms 5-7)
  const A = new Asset({ id, version: 1, name, category: "Tunnels", floors: 1, enterable: true,
    description: name + (o.hall ? ": a " + Math.round(2 * hw) + " m room off the tunnels, a doorway in the middle of each side." : ": a 24 x 24 m excavated chamber with a portal in the middle of each side."),
    footprint: [S, S], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  tmats(A);
  const lined = !["collapse", "sump", "dugout", "pump"].includes(kind), wallM = lined ? "concrete_wash" : "rock";
  // air: an irregular hall (a chamfered rectangle with bays) and four corridors to the portals
  const NN = S / VOX, a = new Uint8Array(NN * NN), c = (i) => -hS + (i + 0.5) * VOX;
  const crat = [];   // UG2: deep blast craters in the hall walls
  for (let k = 0; k < (o.craters ?? 5); k++) {
    const sd = Math.floor(R() * 4), u = (R() - 0.5) * (2 * hw - 3), r = 0.8 + R() * 0.6, d = hw + r * 0.5;
    crat.push(sd === 0 ? [u, d, r] : sd === 1 ? [d, u, r] : sd === 2 ? [u, -d, r] : [-d, u, r]);
  }
  const inHall = (x, z) => (Math.abs(x) < hw && Math.abs(z) < hw && Math.abs(x) + Math.abs(z) < hw * 1.7) || crat.some(([cx, cz, r]) => (x - cx) ** 2 + (z - cz) ** 2 < r * r);
  for (let j = 0; j < NN; j++) for (let i = 0; i < NN; i++) {
    const x = c(i), z = c(j);
    let air = inHall(x, z) || (Math.abs(x) < PORTAL / 2 && Math.abs(z) < hS) || (Math.abs(z) < PORTAL / 2 && Math.abs(x) < hS);
    if (Math.abs(x) > 8 && Math.abs(x) < 8.5 && Math.abs(z) > PORTAL / 2 && R() < 0.0) air = false;
    a[j * NN + i] = air ? 1 : 0;
  }
  const sk = new Uint8Array(NN * NN);
  for (let j = 0; j < NN; j++) for (let i = 0; i < NN; i++) {
    if (a[j * NN + i]) continue;
    outer: for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) { const ii = i + di, jj = j + dj; if (ii >= 0 && jj >= 0 && ii < NN && jj < NN && a[jj * NN + ii]) { sk[j * NN + i] = 1; break outer; } }
  }
  const used = new Uint8Array(NN * NN);
  for (let j = 0; j < NN; j++) for (let i = 0; i < NN; i++) {
    if (!sk[j * NN + i] || used[j * NN + i]) continue;
    let ie = i; while (ie + 1 < NN && sk[j * NN + ie + 1] && !used[j * NN + ie + 1]) ie++;
    let je = j; for (;;) { if (je + 1 >= NN) break; let ok = true; for (let x = i; x <= ie && ok; x++) ok = sk[(je + 1) * NN + x] && !used[(je + 1) * NN + x]; if (!ok) break; je++; }
    for (let y = j; y <= je; y++) for (let x = i; x <= ie; x++) used[y * NN + x] = 1;
    A.box([-hS + i * VOX, -0.05, -hS + j * VOX], [-hS + (ie + 1) * VOX, h + 0.02, -hS + (je + 1) * VOX], wallM);
  }
  if (o.pit) {   // UG3: a shaft: a pit o.pit m across and o.pitDepth deep in the middle, ledges round it, a timber bridge N-S, switchback stairs out
    const p = o.pit / 2, dp = o.pitDepth, fm = lined ? "concrete_floor" : "mud";
    A.box([-hS, -FLOOR_T, -hS], [hS, 0, -p], fm); A.box([-hS, -FLOOR_T, p], [hS, 0, hS], fm);
    A.box([-hS, -FLOOR_T, -p], [-p, 0, p], fm); A.box([p, -FLOOR_T, -p], [hS, 0, p], fm);
    for (const [x0, z0, x1, z1] of [[-p - 0.3, -p - 0.3, p + 0.3, -p], [-p - 0.3, p, p + 0.3, p + 0.3], [-p - 0.3, -p, -p, p], [p, -p, p + 0.3, p]]) A.box([x0, -dp, z0], [x1, -FLOOR_T, z1], "rock");   // pit walls
    A.box([-p, -dp - FLOOR_T, -p], [p, -dp, p], "mud");   // pit floor
    const D2 = { k: 0, lod: 1 };
    A.box([-0.8, -0.25, -p - 0.2], [0.8, 0, p + 0.2], "plank_x");   // the bridge deck
    for (const x of [-0.8, 0.8]) { A.box([x - 0.05, 0, -p], [x + 0.05, 1.0, p], "plank_y", D2); A.box([x - 0.06, 0.95, -p], [x + 0.06, 1.05, p], "plank_x", D2); }
    for (const z of [-p + 0.5, 0, p - 0.5]) A.box([-0.9, -1.8, z - 0.12], [0.9, -0.25, z + 0.12], "plank_y", D2);   // trestles under it
    // switchback stairs out of the pit: west wall (south -> north, -dp -> -dp/2), north wall (west -> east, -dp/2 -> 0)
    const half = dp / 2, run = 2 * p - 1.6, angA = (Math.atan2(half, run) * 180) / Math.PI;
    A.obox([-p + 0.75, r3(-dp + half / 2 - 0.12), 0], [1.5, 0.24, r3(Math.hypot(run, half))], [r3(-angA), 0, 0], "rock", { v: 0 });
    A.obox([0, r3(-half / 2 - 0.12), p - 0.75], [r3(Math.hypot(run, half)), 0.24, 1.5], [0, 0, r3(angA)], "rock", { v: 0 });
    for (let k = 0; k < 16; k++) {
      const u = k / 16, ya = -dp + half * (k + 1) / 16, yb = -half + half * (k + 1) / 16;
      A.box([-p, r3(ya - half / 16 - 0.05), r3(-run / 2 + u * run)], [-p + 1.5, r3(ya), r3(-run / 2 + (u + 1 / 16) * run + 0.02)], k % 2 ? "plank_x" : "rock", D2);
      A.box([r3(-run / 2 + u * run), r3(yb - half / 16 - 0.05), p - 1.5], [r3(-run / 2 + (u + 1 / 16) * run + 0.02), r3(yb), p], k % 2 ? "plank_x" : "rock", D2);
    }
    A.box([-p, -half - 0.24, run / 2], [-p + 1.5, -half, p], "rock");   // the landing between the flights
  } else if (kind === "sump") {
    const p = 4.6, fm = "mud";
    A.box([-hS, -FLOOR_T, -hS], [hS, 0, -p], fm); A.box([-hS, -FLOOR_T, p], [hS, 0, hS], fm);
    A.box([-hS, -FLOOR_T, -p], [-p, 0, p], fm); A.box([p, -FLOOR_T, -p], [hS, 0, p], fm);
    for (const [x0, z0, x1, z1] of [[-p - 0.3, -p - 0.3, p + 0.3, -p], [-p - 0.3, p, p + 0.3, p + 0.3], [-p - 0.3, -p, -p, p], [p, -p, p + 0.3, p]]) A.box([x0, -2.4, z0], [x1, -FLOOR_T, z1], "rock");
    A.box([-p, -2.7, -p], [p, -2.4, p], "mud");
  } else A.box([-hS, -FLOOR_T, -hS], [hS, 0, hS], lined ? "floor_slab" : FLOORS[seed % FLOORS.length]);   // UG4: textured floors
  A.box([-hS, h, -hS], [hS, h + CEIL_T, hS], lined ? "concrete_wash" : "rock_dark");
  for (const [dx, dz] of [[0, 1], [1, 0], [0, -1], [-1, 0]]) {   // portal heads at the shared height
    const e = hS;
    A.box(dx ? [dx > 0 ? e - 0.25 : -e, PORTAL_H, -PORTAL / 2] : [-PORTAL / 2, PORTAL_H, dz > 0 ? e - 0.25 : -e], dx ? [dx > 0 ? e : -e + 0.25, h + 0.02, PORTAL / 2] : [PORTAL / 2, h + 0.02, dz > 0 ? e : -e + 0.25], wallM);
  }
  const D = { k: 0, lod: 1 };
  const pil = hw > 7 ? [[-5, -5], [5, -5], [-5, 5], [5, 5]] : hw > 5.8 ? [[0, 0]] : [];
  for (const x of [-hw * 0.6, 0, hw * 0.6]) A.box([r3(x - 0.15), h - 0.35, -hw], [r3(x + 0.15), h - 0.02, hw], lined ? "steel" : "plank_x", D);   // roof beams
  for (const [x, z] of pil) A.box([x - 0.16, 0, z - 0.16], [x + 0.16, h - 0.3, z + 0.16], lined ? "concrete_wash" : "plank_y");   // pillars (cover)
  for (const [cx, cz, r] of crat) for (let k = 0; k < 3; k++) A.obox([r3(cx + (R() - 0.5) * r), r3(0.15 + R() * 0.25), r3(cz + (R() - 0.5) * r)], [0.4 + R() * 0.6, 0.3 + R() * 0.5, 0.4 + R() * 0.6], [R() * 40, R() * 90, R() * 40], "rock", D);
  for (let k = 0; k < (lined ? 4 : 10); k++) {   // UG2: rock bosses standing out of the hall walls
    const sd = Math.floor(R() * 4), u = (R() - 0.5) * (2 * hw - 3), sz = lined ? 0.4 + R() * 0.3 : 0.8 + R() * 0.7, d = hw + sz / 2 - (lined ? 0.12 : 0.35);
    const [x, z] = sd === 0 ? [u, d] : sd === 1 ? [d, u] : sd === 2 ? [u, -d] : [-d, u];
    A.obox([r3(x), r3(R() < 0.5 ? sz * 0.35 : 0.5 + R() * (h - 1.2)), r3(z)], [sz, sz * 0.85, sz], [R() * 40, R() * 90, R() * 40], lined ? "concrete_wash" : "rock", lined ? D : { lod: 1 });
  }
  if (o.floorCraters) for (let k = 0; k < o.floorCraters; k++) floorCrater(A, R, (R() - 0.5) * (hw - 1.5) * 2, (R() - 0.5) * (hw - 1.5) * 2, 0.9 + R() * 0.4);
  if (kind === "pump") {   // UG2: a small sunken pump pit with dark water
    A.box([-2.2, -0.9, -2.2], [2.2, -0.6, 2.2], "mud", { k: 0 });
    A.box([-2.0, -0.05, -2.0], [2.0, -0.02, 2.0], "puddle", { k: 0 });
  }
  if (kind === "sump") {   // UG3: a deep sump: dark water over a 2.4 m deep pool (wade or swim), steps out on its south side
    (A.meta.water ??= { surfaces: [] }).surfaces.push({ outline: [[-4.6, -4.6], [4.6, -4.6], [4.6, 4.6], [-4.6, 4.6]], level: -0.5, kind: "pond", clarity: 0.6, waves: 0.05, tint: [0.03, 0.04, 0.035] });
    A.box([-4.6, -0.51, -4.6], [4.6, -0.5, 4.6], "puddle", { v: 0, k: 0 });
    for (let k = 0; k < 6; k++) A.box([-1.2, -2.4, r3(-4.6 + k * 0.4)], [1.2, r3(-2.4 + (6 - k) * 0.38), r3(-4.6 + (k + 1) * 0.4)], "rock");
  }
  if (kind === "collapse") {   // a fallen roof slab and rock spill
    A.obox([2.5, 1.2, -2.0], [5.5, 0.5, 3.5], [0, 25, 18], "rock_dark");
    for (let k = 0; k < 14; k++) A.obox([r3(-3 + R() * 7), r3(0.2 + R() * 0.4), r3(-5 + R() * 6)], [0.6 + R() * 0.8, 0.4 + R() * 0.6, 0.6 + R() * 0.8], [R() * 40, R() * 90, R() * 40], "rock");
  }
  for (let k = 0; k < (o.pit || kind === "sump" ? 0 : 6); k++) { const x = (R() - 0.5) * (hw * 1.6), z = (R() - 0.5) * (hw * 1.6), w = 0.8 + R() * 1.6; const y0 = decalY(); A.box([r3(x - w / 2), r3(y0), r3(z - w / 3)], [r3(x + w / 2), r3(y0 + 0.003), r3(z + w / 3)], R() < 0.5 ? "puddle" : "mud", { k: 0 }); }
  floorRelief(A, R, (x, z) => Math.abs(x) < hw - 0.3 && Math.abs(z) < hw - 0.3 && !(o.pit && Math.abs(x) < o.pit / 2 + 0.4 && Math.abs(z) < o.pit / 2 + 0.4) && !(kind === "sump" && Math.abs(x) < 5 && Math.abs(z) < 5) && !(kind === "pump" && Math.abs(x) < 2.6 && Math.abs(z) < 2.6), lined, Math.round(hw * hw * 0.9), hw);   // UG4
  const cv = [];
  for (const [x, z] of pil) for (const yaw of [0, 90, 180, 270]) cv.push(cover([x + Math.sin((yaw * Math.PI) / 180) * -0.6, 0, z + Math.cos((yaw * Math.PI) / 180) * -0.6], yaw, "high", ["left", "right"]));
  setNav(A, { effect: "block", rooms: [room("hall", [-hw, 0, -hw], [hw, h, hw])], doors: [],
    entrances: [[0, 1], [1, 0], [0, -1], [-1, 0]].map(([dx, dz]) => ({ p: [dx * hS, 0, dz * hS], dir: [dx, 0, dz], room: "hall" })), corners: [], firingPoints: [], cover: cv, areas: [] });
  A.meta.snap = [[0, 1], [1, 0], [0, -1], [-1, 0]].map(([dx, dz]) => ({ p: [dx * hS, 0, dz * hS], dir: [dx, dz], kind: "tunnel" }));
  return A;
}

// ---------------------------------------------------------------- lamps (hung under the ceiling: instance y = the ceiling)
function lamp(id, name, light) {
  const A = new Asset({ id, version: 1, name, category: "Tunnels", floors: 0, enterable: false, footprint: [0.4, 0.4], plinthDepth: 0, maxGrade: 5, noAutoPanels: true, description: name });
  A.mat("steel", [0.12, 0.11, 0.1], 0.6, "metal", { metallic: 0.5 });
  A.mat("bulb", [1, 0.8, 0.55], 0.3, "plain", light ? { emissive: light.color[0] > 0.9 && light.color[1] < 0.3 ? [5.0, 0.6, 0.4] : [6.0, 4.6, 2.9] } : {});
  A.cyl([0, -0.2, 0], [0.012, 0.4, 0.012], "steel", { k: 0, n: 6 });                 // the cable
  A.cyl([0, -0.46, 0], [0.16, 0.14, 0.16], "steel", { k: 0, n: 8 });                  // the shade
  A.cyl([0, -0.56, 0], [0.09, 0.12, 0.09], "bulb", { k: 0, n: 8 });                   // the bulb in its cage
  for (const a of [0, 60, 120]) A.obox([0, -0.58, 0], [0.18, 0.16, 0.012], [0, a, 0], "steel", { k: 0 });
  if (light) A.meta.light = { offset: [0, -0.6, 0], ...light };
  setNav(A, { effect: "none", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] });
  return A;
}

// ---------------------------------------------------------------- UG3: rail, carts and mine props
function mquat([rx, ry, rz]) {   // glm::quat(euler) as the engine builds it
  const d = Math.PI / 360, cx = Math.cos(rx * d), cy = Math.cos(ry * d), cz = Math.cos(rz * d), sx = Math.sin(rx * d), sy = Math.sin(ry * d), sz = Math.sin(rz * d);
  return [cx * cy * cz + sx * sy * sz, sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz];
}
function mrot(q, [x, y, z]) {
  const [w, qx, qy, qz] = q, tx = 2 * (qy * z - qz * y), ty = 2 * (qz * x - qx * z), tz = 2 * (qx * y - qy * x);
  return [x + w * tx + (qy * tz - qz * ty), y + w * ty + (qz * tx - qx * tz), z + w * tz + (qx * ty - qy * tx)];
}
// axis-aligned boxes [min, max, mat] of a piece, turned as one by `tilt` and moved to `at`
function mput(A, boxes, at, tilt, o = {}) {
  const q = mquat(tilt);
  for (const [mn, mx, m] of boxes) {
    const c = mrot(q, [0, 1, 2].map((i) => (mn[i] + mx[i]) / 2));
    A.obox([r3(c[0] + at[0]), r3(c[1] + at[1]), r3(c[2] + at[2])], [0, 1, 2].map((i) => r3(Math.abs(mx[i] - mn[i]))), tilt.map(r3), m, o);
  }
}
function mmats(A) {
  tmats(A);
  A.mat("rail", [0.32, 0.29, 0.26], 0.45, "metal", { metallic: 0.7 });
  A.mat("sleeper", [0.22, 0.16, 0.1], 0.9, "wood", { grain: 1 });
  A.mat("cart", [0.28, 0.2, 0.14], 0.7, "metal", { metallic: 0.4 });
  A.mat("ore", [0.3, 0.27, 0.22], 0.9, "stone");
  A.mat("paint_yellow", [0.62, 0.5, 0.08], 0.6, "painted");
  A.mat("paint_black", [0.05, 0.05, 0.05], 0.6, "painted");
  A.mat("paint_green", [0.2, 0.28, 0.2], 0.6, "painted");
  A.mat("crate_red", [0.36, 0.14, 0.1], 0.8, "wood", { grain: 1 });
  A.mat("rubber", [0.05, 0.05, 0.05], 0.8, "plain");
  A.mat("canvas", [0.36, 0.33, 0.24], 0.95, "fabric");
}
const GAUGE = 0.6;
function trackBoxes(len, z0 = -CELL / 2) {   // a straight run along z from z0, rails on sleepers every 0.6 m
  const b = [];
  for (let z = z0 + 0.3; z < z0 + len; z += 0.6) b.push([[-0.55, 0, z - 0.09], [0.55, 0.08, z + 0.09], "sleeper"]);
  for (const x of [-GAUGE / 2, GAUGE / 2]) b.push([[x - 0.035, 0.08, z0], [x + 0.035, 0.17, z0 + len], "rail"]);
  return b;
}
function trackCurveBoxes(r = CELL / 2, n = 12) {   // a quarter circle from the south portal (0, -4) to the east portal (4, 0), centred on (4, -4)
  const b = [], cx = CELL / 2, cz = -CELL / 2;
  for (let k = 0; k < n; k++) {
    const a0 = Math.PI - (k / n) * (Math.PI / 2), a1 = Math.PI - ((k + 1) / n) * (Math.PI / 2), am = (a0 + a1) / 2;
    for (const rr of [r - GAUGE / 2, r + GAUGE / 2]) {
      const x = cx + Math.cos(am) * rr, z = cz + Math.sin(am) * rr, L = rr * (Math.PI / 2) / n + 0.02, yaw = (Math.atan2(-Math.sin(am), Math.cos(am)) * 180) / Math.PI;
      b.push({ c: [x, 0.125, z], s: [0.07, 0.09, L], r: [0, r3(-yaw), 0], m: "rail" });
    }
    if (k % 1 === 0) { const x = cx + Math.cos(am) * r, z = cz + Math.sin(am) * r, yaw = (Math.atan2(-Math.sin(am), Math.cos(am)) * 180) / Math.PI; b.push({ c: [x, 0.04, z], s: [1.1, 0.08, 0.18], r: [0, r3(-yaw), 0], m: "sleeper" }); }
  }
  return b;
}
function trackAsset(id, name, kind, opt = {}) {
  const A = new Asset({ id, version: 1, name, category: "Tunnels", description: opt.desc || name, floors: 0, enterable: false,
    footprint: [CELL, CELL], plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  mmats(A);
  const D = { k: 0 };
  if (kind === "straight" || kind === "points" || kind === "buffer") {
    const len = kind === "buffer" ? CELL / 2 + 1.2 : CELL;
    for (const [mn, mx, m] of trackBoxes(len)) A.box(mn, mx, m, D);
    A.box([-0.75, 0.004, -CELL / 2], [0.75, 0.007, -CELL / 2 + len], "mud", { k: 0 });   // UG4: the worn, trodden strip along the haulage line
    if (kind === "buffer") {   // a timber buffer stop at the end of the run
      const z = -CELL / 2 + len;
      A.box([-0.7, 0, z], [0.7, 0.7, z + 0.3], "sleeper"); A.box([-0.6, 0.3, z - 0.15], [0.6, 0.55, z], "paint_yellow");
      for (const x of [-0.6, 0.6]) A.obox([x, 0.35, z + 0.6], [0.14, 0.14, 1.0], [-45, 0, 0], "sleeper");
    }
  }
  if (kind === "curve" || kind === "points") for (const p of trackCurveBoxes()) A.obox(p.c.map(r3), p.s.map(r3), p.r, p.m, D);
  if (kind === "points") { A.box([-1.1, 0, -2.2], [-0.8, 0.35, -1.9], "paint_black"); A.obox([-0.95, 0.6, -2.05], [0.05, 0.6, 0.05], [0, 0, 25], "rail"); }   // the points lever
  if (opt.rise) {   // a straight run on an incline: the whole run tilted
    const ang = (Math.atan2(opt.rise, CELL) * 180) / Math.PI, k = Math.hypot(CELL, opt.rise) / CELL;
    for (let z = -CELL / 2 + 0.3; z < CELL / 2; z += 0.6) mput(A, [[[-0.55, 0, -0.09], [0.55, 0.08, 0.09], "sleeper"]], [0, opt.rise * (z + CELL / 2) / CELL, z * 1], [-ang, 0, 0], D);
    for (const x of [-GAUGE / 2, GAUGE / 2]) mput(A, [[[x - 0.035, 0.08, -CELL / 2 * k], [x + 0.035, 0.17, CELL / 2 * k], "rail"]], [0, opt.rise / 2, 0], [-ang, 0, 0], D);
  }
  setNav(A, { effect: "none", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] });
  return A;
}
function cartBoxes(load) {   // a 1.5 m tub on a chassis (origin on the rail head line)
  const b = [[[-0.5, 0.15, -0.6], [0.5, 0.28, 0.6], "cart"]];
  b.push([[-0.48, 0.28, -0.75], [0.48, 0.34, 0.75], "cart"]);
  for (const [x0, z0, x1, z1] of [[-0.48, -0.75, 0.48, -0.7], [-0.48, 0.7, 0.48, 0.75], [-0.48, -0.75, -0.43, 0.75], [0.43, -0.75, 0.48, 0.75]]) b.push([[x0, 0.34, z0], [x1, 1.0, z1], "cart"]);
  for (const z of [-0.78, 0.78]) b.push([[-0.15, 0.2, z - 0.05], [0.15, 0.3, z + 0.05], "rail"]);   // couplings
  if (load) b.push([[-0.42, 0.34, -0.68], [0.42, 0.92, 0.68], "ore"]);
  return b;
}
function cartAsset(id, name, kind) {
  const A = new Asset({ id, version: 1, name, category: "Tunnels", description: name, floors: 0, enterable: false,
    footprint: [1.2, 1.8], plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  mmats(A);
  const R = rng(kind.length * 7 + 3);
  const load = kind === "ore" || kind === "tipped";
  const tilt = kind === "tipped" ? [0, 8, 95] : kind === "derailed" ? [6, 22, -9] : [0, 0, 0];
  const at = kind === "tipped" ? [0.45, 0.5, 0] : kind === "derailed" ? [0.35, 0.05, 0] : [0, 0.17, 0];
  mput(A, cartBoxes(load && kind !== "tipped"), at, tilt);
  for (const z of [-0.45, 0.45]) for (const x of [-GAUGE / 2, GAUGE / 2]) {   // wheels
    const c = mrot(mquat(tilt), [x, 0.0, z]);
    A.cyl([r3(c[0] + at[0]), r3(c[1] + at[1] + 0.0), r3(c[2] + at[2])], [0.3, 0.06, 0.3], "rail", { r: [r3(tilt[0]), r3(tilt[1]), r3(tilt[2] + 90)], n: 10 });
  }
  if (kind === "ore" || kind === "tipped") for (let k = 0; k < (kind === "tipped" ? 12 : 6); k++) {
    const x = kind === "tipped" ? -0.6 - R() * 1.2 : (R() - 0.5) * 0.7, z = (R() - 0.5) * (kind === "tipped" ? 1.8 : 1.1), y = kind === "tipped" ? 0.12 : 0.95, s = 0.18 + R() * 0.22;
    A.obox([r3(x), r3(y), r3(z)], [r3(s), r3(s * 0.8), r3(s)], [r3(R() * 40), r3(R() * 90), r3(R() * 40)], "ore", { k: 0 });
  }
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], areas: [],
    cover: [cover([0.9, 0, 0], 90, "low", ["over", "left", "right"]), cover([-0.9, 0, 0], 270, "low", ["over", "left", "right"])] });
  return A;
}
function prop(id, name, boxes, fp, opt = {}) {   // a prop from parts: [kind, ...]: ["b", min, max, mat] | ["o", c, s, r, mat] | ["c", c, s, mat, r?]
  const A = new Asset({ id, version: 1, name, category: "Tunnels", description: opt.desc || name, floors: 0, enterable: false,
    footprint: fp, plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  mmats(A);
  for (const p of boxes) {
    const o = p[p.length - 1] && typeof p[p.length - 1] === "object" && !Array.isArray(p[p.length - 1]) ? p[p.length - 1] : {};
    if (p[0] === "b") A.box(p[1], p[2], p[3], o);
    else if (p[0] === "o") A.obox(p[1], p[2], p[3], p[4], o);
    else if (p[0] === "c") A.cyl(p[1], p[2], p[3], { n: 12, ...o });
  }
  setNav(A, { effect: opt.nav || "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], areas: [], cover: opt.cover || [] });
  return A;
}
function mineAssets() {
  const out = [], K = { k: 0 };
  out.push(trackAsset("tun_track_straight", "Mine track, straight (8 m)", "straight", { desc: "Narrow-gauge mine track (0.6 m) on timber sleepers along one cell." }));
  out.push(trackAsset("tun_track_curve", "Mine track, curve", "curve", { desc: "Mine track turning from the south to the east portal of a cell." }));
  out.push(trackAsset("tun_track_points", "Mine track, points", "points", { desc: "Mine track points: straight on, or turning east; a lever by the switch." }));
  out.push(trackAsset("tun_track_buffer", "Mine track, buffer stop", "buffer", { desc: "The end of a siding: rails into a timber buffer stop." }));
  out.push(trackAsset("tun_track_ramp_12", "Mine track on an incline (1.2 m)", "ramp", { rise: 1.2 }));
  out.push(trackAsset("tun_track_ramp_24", "Mine track on an incline (2.4 m)", "ramp", { rise: 2.4 }));
  out.push(cartAsset("tun_cart", "Mine cart (empty)", "empty"));
  out.push(cartAsset("tun_cart_ore", "Mine cart (loaded with ore)", "ore"));
  out.push(cartAsset("tun_cart_tipped", "Mine cart (tipped over, ore spilled)", "tipped"));
  out.push(cartAsset("tun_cart_derailed", "Mine cart (derailed)", "derailed"));
  const covHi = (r) => [cover([r, 0, 0], 90, "high", ["left", "right"]), cover([-r, 0, 0], 270, "high", ["left", "right"])];
  out.push(prop("tun_duct", "Ventilation duct (8 m)", [["c", [0, 0, 0], [0.6, 8.0, 0.6], "steel", { r: [90, 0, 0], k: 0, n: 14 }],
    ...[-3, -1, 1, 3].map((z) => ["b", [-0.33, -0.02, z - 0.03], [0.33, 0.45, z + 0.03], "rust", K])], [0.7, 8], { nav: "none", desc: "A canvas-and-steel vent duct hung under the roof (origin on its axis)." }));
  out.push(prop("tun_fan", "Ventilation fan", [["c", [0, 0.8, 0], [1.3, 1.1, 1.3], "paint_green", { r: [90, 0, 0] }], ["c", [0, 0.8, 0.65], [0.5, 0.4, 0.5], "steel", { r: [90, 0, 0] }],
    ...[0, 60, 120].map((a) => ["o", [0, 0.8, -0.52], [1.1, 0.12, 0.02], [0, 0, a], "steel", K]), ["b", [-0.7, 0, -0.5], [0.7, 0.2, 0.5], "steel"]], [1.4, 1.4], { cover: covHi(0.9) }));
  out.push(prop("tun_hopper", "Ore hopper and chute", [["o", [0, 2.2, 0], [1.8, 1.2, 1.8], [0, 0, 0], "cart"], ["o", [0, 1.4, 0.4], [0.9, 0.6, 1.2], [30, 0, 0], "cart"],
    ...[[-0.85, -0.85], [0.85, -0.85], [-0.85, 0.85], [0.85, 0.85]].map(([x, z]) => ["b", [x - 0.08, 0, z - 0.08], [x + 0.08, 1.7, z + 0.08], "sleeper"]), ["o", [0, 2.75, 0], [1.6, 0.3, 1.6], [0, 15, 0], "ore", K]], [2, 2.4], { cover: covHi(1.1) }));
  out.push(prop("tun_winch", "Shaft winch", [["c", [0, 0.7, 0], [0.8, 1.4, 0.8], "rail", { r: [0, 0, 90] }], ["b", [-0.9, 0, -0.5], [0.9, 0.25, 0.5], "steel"],
    ...[-0.8, 0.8].map((x) => ["b", [x - 0.08, 0, -0.35], [x + 0.08, 1.2, 0.35], "steel"]), ["b", [0.95, 0.3, -0.3], [1.5, 0.9, 0.3], "paint_green"], ["c", [0, 0.7, 0], [0.84, 1.0, 0.84], "rust", { r: [0, 0, 90], k: 0 }]], [2, 1.2], { cover: covHi(1.0) }));
  out.push(prop("tun_ladder", "Ladder (4 m)", [...[-0.25, 0.25].map((x) => ["b", [x - 0.03, 0, -0.03], [x + 0.03, 4.0, 0.03], "plank_y", K]), ...Array.from({ length: 13 }, (_, k) => ["b", [-0.25, 0.3 + k * 0.3, -0.02], [0.25, 0.34 + k * 0.3, 0.02], "plank_x", K])], [0.6, 0.2], { nav: "none" }));
  out.push(prop("tun_ore_pile", "Ore pile", Array.from({ length: 16 }, (_, k) => { const a = k * 2.4, r = 0.2 + (k % 5) * 0.22; return ["o", [r3(Math.cos(a) * r), r3(0.15 + (4 - (k % 5)) * 0.12), r3(Math.sin(a) * r)], [0.5, 0.35, 0.45], [k * 17 % 40, k * 37 % 180, k * 23 % 40], "ore"]; }), [2, 2], { cover: [cover([0, 0, 1.2], 0, "low", ["over"])] }));
  out.push(prop("tun_tool_rack", "Tool rack (picks and shovels)", [["b", [-1.0, 0, -0.1], [1.0, 1.6, 0.0], "sleeper"], ["b", [-1.0, 1.3, 0], [1.0, 1.36, 0.12], "plank_x"],
    ...Array.from({ length: 6 }, (_, k) => ["o", [r3(-0.8 + k * 0.32), 0.75, 0.08], [0.04, 1.3, 0.04], [0, 0, (k % 2) * 6 - 3], "plank_y", K]),
    ...Array.from({ length: 6 }, (_, k) => k % 2 ? ["b", [r3(-0.88 + k * 0.32), 0.08, 0.05], [r3(-0.72 + k * 0.32), 0.34, 0.1], "steel", K] : ["o", [r3(-0.8 + k * 0.32), 1.4, 0.08], [0.42, 0.05, 0.05], [0, 0, 8], "steel", K])], [2, 0.4], { nav: "none" }));
  out.push(prop("tun_drill_rig", "Pneumatic drill rig", [["b", [-0.4, 0.25, -0.6], [0.4, 0.45, 0.6], "paint_yellow"], ...[[-0.35, -0.5], [0.35, -0.5], [-0.35, 0.5], [0.35, 0.5]].map(([x, z]) => ["c", [x, 0.2, z], [0.4, 0.1, 0.4], "rubber", { r: [0, 0, 90] }]),
    ["o", [0, 1.1, 0.3], [0.12, 1.6, 0.12], [-35, 0, 0], "steel"], ["c", [0, 1.55, 0.95], [0.16, 0.9, 0.16], "steel", { r: [-35, 0, 0], k: 0 }]], [1, 1.6], { cover: [cover([0.7, 0, 0], 90, "low", ["over"])] }));
  out.push(prop("tun_compressor", "Air compressor", [["b", [-0.6, 0, -1.0], [0.6, 0.15, 1.0], "steel"], ["c", [0, 0.6, 0.3], [0.8, 1.2, 0.8], "paint_green", { r: [90, 0, 0] }], ["b", [-0.45, 0.15, -0.95], [0.45, 1.0, -0.3], "paint_green"], ["c", [0, 1.1, -0.6], [0.3, 0.2, 0.3], "steel"]], [1.2, 2], { cover: covHi(0.8) }));
  out.push(prop("tun_cable_reel", "Cable reel", [["c", [0, 0.6, -0.3], [1.2, 0.08, 1.2], "sleeper", { r: [90, 0, 0] }], ["c", [0, 0.6, 0.3], [1.2, 0.08, 1.2], "sleeper", { r: [90, 0, 0] }], ["c", [0, 0.6, 0], [0.9, 0.56, 0.9], "rubber", { r: [90, 0, 0] }]], [1.2, 0.8], { cover: [cover([0, 0, 0.7], 0, "low", ["over"])] }));
  out.push(prop("tun_powder_crates", "Powder crates (stacked)", [["b", [-0.6, 0, -0.35], [0.0, 0.45, 0.35], "crate_red"], ["b", [0.02, 0, -0.35], [0.62, 0.45, 0.35], "crate_red"], ["b", [-0.3, 0.45, -0.33], [0.3, 0.9, 0.33], "crate_red"], ["b", [-0.62, 0.2, -0.36], [0.64, 0.24, 0.36], "sleeper", K]], [1.3, 0.8], { cover: [cover([0, 0, 0.6], 0, "low", ["over"]), cover([0, 0, -0.6], 180, "low", ["over"])] }));
  out.push(prop("tun_pump", "Sump pump and pipes", [["b", [-0.5, 0, -0.4], [0.5, 0.2, 0.4], "steel"], ["c", [0, 0.55, 0], [0.6, 0.7, 0.6], "paint_green"], ["c", [0.55, 0.55, 0], [0.18, 0.6, 0.18], "rust", { r: [0, 0, 90], k: 0 }], ["c", [0.85, -0.6, 0], [0.18, 2.3, 0.18], "rust", { k: 0 }], ["c", [-0.3, 1.1, 0], [0.12, 1.4, 0.12], "rust", { r: [0, 0, 90], k: 0 }]], [1.2, 1], { cover: [cover([0, 0, 0.7], 0, "low", ["over"])] }));
  out.push(prop("tun_conveyor", "Conveyor section (6 m)", [["b", [-0.45, 0.85, -3], [0.45, 0.92, 3], "rubber"], ...[-0.5, 0.5].map((x) => ["b", [x - 0.05, 0.7, -3], [x + 0.05, 0.95, 3], "steel"]), ...[-2.6, 0, 2.6].flatMap((z) => [-0.5, 0.5].map((x) => ["b", [x - 0.05, 0, z - 0.05], [x + 0.05, 0.75, z + 0.05], "steel"])),
    ...Array.from({ length: 6 }, (_, k) => ["o", [r3(-0.3 + (k % 3) * 0.3), 1.0, r3(-2.4 + k * 0.9)], [0.25, 0.15, 0.25], [k * 20, k * 33, 0], "ore", K])], [1.1, 6], { cover: [cover([0.8, 0, 0], 90, "low", ["over"]), cover([-0.8, 0, 0], 270, "low", ["over"])] }));
  out.push(prop("tun_warning_board", "Warning board (striped, no text)", [["b", [-0.04, 0, -0.04], [0.04, 1.6, 0.04], "sleeper"], ["b", [-0.45, 1.2, 0.04], [0.45, 1.75, 0.07], "paint_yellow"],
    ...[-0.3, -0.1, 0.1, 0.3].map((x) => ["o", [x, 1.475, 0.075], [0.1, 0.6, 0.006], [0, 0, 35], "paint_black", K])], [0.9, 0.2], { nav: "none" }));
  return out;
}

export function tunnelAssets() {
  const out = [];
  const P = (id, name, sides, opt) => out.push(passage(id, name, sides, opt));
  P("tun_end", "Tunnel dead end", ["S"], { seed: 11, spokes: { S: { bulge: 2.9 } }, hub: 1.5, desc: "A tunnel ending in rough rock." });
  P("tun_straight", "Tunnel, straight", ["N", "S"], { seed: 12, craters: 1, spokes: { N: { bulge: 2.8 }, S: { bulge: 2.7 } } });
  P("tun_straight_kink", "Tunnel, kinked", ["N", "S"], { seed: 13, craters: 2, spokes: { N: { kink: 0.7, bulge: 2.9 }, S: { kink: -0.6, bulge: 2.8 } }, pipeSide: -1 });
  P("tun_straight_niche", "Tunnel, with a niche", ["N", "S"], { seed: 14, craters: 1, floorCraters: 1, spokes: { N: { bulge: 3.0, niche: true }, S: { bulge: 2.7 } } });
  P("tun_straight_lined", "Tunnel, concrete-lined", ["N", "S"], { seed: 15, lined: true, lumps: 4, spokes: { N: { bulge: 2.6 }, S: { bulge: 2.6 } } });
  P("tun_wide", "Tunnel widening", ["N", "S"], { seed: 16, craters: 2, floorCraters: 1, hub: 2.4, h: 3.0, spokes: { N: { bulge: 3.6 }, S: { bulge: 3.4 } },
    cover: [cover([1.4, 0, 0], 90, "low", ["over"]), cover([-1.4, 0, 0], 270, "low", ["over"])] });
  P("tun_crawl", "Tunnel squeeze (low)", ["N", "S"], { seed: 17, h: 2.6, lumps: 6, desc: "A pinched side passage: 1.5 m wide and 1.7 m high in the middle (too tight for the heavies).",
    extra: (A) => { A.box([-1.3, 1.7, -1.0], [1.3, 2.62, 1.0], "rock_dark"); A.box([-1.3, -0.05, -1.0], [-0.75, 1.75, 1.0], "rock"); A.box([0.75, -0.05, -1.0], [1.3, 1.75, 1.0], "rock"); } });
  P("tun_corner", "Tunnel bend", ["N", "E"], { seed: 18, craters: 2, hub: 1.7, spokes: { N: { bulge: 2.8, kink: 0.3 }, E: { bulge: 2.7, kink: -0.3 } } });
  P("tun_corner_lined", "Tunnel bend, lined", ["N", "E"], { seed: 19, lined: true, lumps: 3, hub: 1.6 });
  P("tun_tee", "Tunnel T junction", ["N", "E", "W"], { seed: 20, craters: 1, hub: 1.9, h: 2.9, spokes: { N: { bulge: 2.8 }, E: { bulge: 2.7, kink: 0.25 }, W: { bulge: 2.9 } } });
  P("tun_cross", "Tunnel crossing", ["N", "E", "S", "W"], { seed: 21, craters: 1, hub: 2.1, h: 2.9, spokes: { N: { bulge: 2.8 }, E: { bulge: 2.7 }, S: { bulge: 2.8 }, W: { bulge: 2.7 } } });
  // UG2: wall damage, breaches, small rooms (one cell, 3-5 m) and medium rooms (3 x 3 cells, 10-14 m)
  P("tun_straight_shelled", "Tunnel, shelled", ["N", "S"], { seed: 22, craters: 3, floorCraters: 1, spokes: { N: { bulge: 2.9, kink: 0.3 }, S: { bulge: 2.8 } } });
  P("tun_breach", "Tunnel breach (blown through)", ["N", "S"], { seed: 23, craters: 4, floorCraters: 1, lumps: 18, h: 2.9, desc: "A hole blasted through the rock between two galleries: ragged, cratered, full of spoil.",
    spokes: { N: { bulge: 3.1, kink: 0.5 }, S: { bulge: 3.0, kink: -0.4 } } });
  const SR = { end: ["S"], straight: ["N", "S"], corner: ["N", "E"], tee: ["N", "E", "W"], cross: ["N", "E", "S", "W"] };
  let sk = 40;
  for (const [shape, sides] of Object.entries(SR)) {
    const [hx, hz] = [[2.2, 2.0], [2.0, 2.4], [2.3, 2.3], [2.4, 2.1], [2.5, 2.5]][sk - 40];
    P("tun_room_s_" + shape, "Small tunnel room (" + shape + ")", sides, { seed: sk++, room: [hx, hz], lined: true, lumps: 4, craters: 1, h: 2.75,
      desc: "A small concrete-lined room (" + (2 * hx).toFixed(1) + " x " + (2 * hz).toFixed(1) + " m) at a tunnel junction.",
      cover: [cover([hx - 0.5, 0, hz - 0.5], 45, "low", ["over"]), cover([-hx + 0.5, 0, -hz + 0.5], 225, "low", ["over"])] });
  }
  P("tun_cross_shelled", "Tunnel crossing, shelled", ["N", "E", "S", "W"], { seed: 24, hub: 2.2, h: 2.9, craters: 3, floorCraters: 1, spokes: { N: { bulge: 2.9 }, E: { bulge: 2.8 }, S: { bulge: 2.7 }, W: { bulge: 2.9 } } });
  P("tun_tee_shelled", "Tunnel T junction, shelled", ["N", "E", "W"], { seed: 25, hub: 2.0, h: 2.9, craters: 3, floorCraters: 1, spokes: { N: { bulge: 2.9 }, E: { bulge: 2.8 }, W: { bulge: 2.8, kink: 0.3 } } });
  const M = [["office", "Tunnel room: signals office", 5.0, 2.9], ["workshop", "Tunnel room: workshop", 6.0, 3.0], ["magazine", "Tunnel room: ammunition store", 5.5, 2.9],
             ["dugout", "Tunnel room: rock dugout", 6.5, 3.0], ["pump", "Tunnel room: pump pit", 5.5, 2.9], ["mess", "Tunnel room: mess hall", 7.0, 3.0]];
  M.forEach(([kind, name, hall, h], k) => out.push(chamber("tun_room_m_" + kind, name, kind, 60 + k, { hall, h, craters: 3 + (k % 3), floorCraters: kind === "dugout" ? 2 : kind === "pump" ? 0 : 1 })));
  // UG3: elevation pieces, rail, carts and mine props
  P("tun_ramp_12", "Tunnel incline (up 1.2 m)", ["N", "S"], { seed: 70, rise: 1.2, lumps: 10, spokes: { N: { bulge: 2.8 }, S: { bulge: 2.8 } }, desc: "A passage rising 1.2 m from its south to its north portal." });
  P("tun_ramp_24", "Tunnel incline (up 2.4 m)", ["N", "S"], { seed: 71, rise: 2.4, lumps: 10, spokes: { N: { bulge: 2.9 }, S: { bulge: 2.8, kink: 0.2 } }, desc: "A steep passage rising 2.4 m from its south to its north portal." });
  P("tun_straight_humped", "Tunnel, humped floor", ["N", "S"], { seed: 72, humps: 3, puddles: 1, spokes: { N: { bulge: 2.9, kink: 0.4 }, S: { bulge: 2.8 } } });
  P("tun_straight_timbered", "Tunnel, timbered", ["N", "S"], { seed: 73, timbered: true, noPipe: true, lumps: 6, spokes: { N: { bulge: 2.6 }, S: { bulge: 2.6 } } });
  P("tun_straight_meshed", "Tunnel, roof mesh and bolts", ["N", "S"], { seed: 74, mesh: true, lumps: 8, spokes: { N: { bulge: 2.8 }, S: { bulge: 2.8 } } });
  out.push(chamber("tun_chamber_shaft", "Tunnel chamber: shaft", "shaft", 36, { craters: 4, pit: 9.0, pitDepth: 10.0 }));
  out.push(...mineAssets());
  out.push(stairs());
  out.push(chamber("tun_chamber_store", "Tunnel chamber: storage", "store", 31, { craters: 5, floorCraters: 1 }));
  out.push(chamber("tun_chamber_bunks", "Tunnel chamber: bunk room", "bunks", 32, { craters: 5, floorCraters: 1 }));
  out.push(chamber("tun_chamber_generator", "Tunnel chamber: generator room", "generator", 33, { craters: 5, floorCraters: 1 }));
  out.push(chamber("tun_chamber_sump", "Tunnel chamber: flooded sump", "sump", 34, { craters: 6 }));
  out.push(chamber("tun_chamber_collapse", "Tunnel chamber: collapsed", "collapse", 35, { craters: 7, floorCraters: 3 }));
  out.push(lamp("tun_lamp", "Tunnel lamp", { color: [1.0, 0.72, 0.42], intensity: 0.8, radius: 5.2, use: "electric" }));   // UG4: a shorter reach: pools of light, dark between
  out.push(lamp("tun_lamp_flicker", "Tunnel lamp (failing)", { color: [1.0, 0.7, 0.4], intensity: 0.65, radius: 7.0, use: "electric" }));
  out.push(lamp("tun_lamp_shadow", "Tunnel lamp (shadows)", { color: [1.0, 0.74, 0.45], intensity: 0.85, radius: 8.0, shadow: 1, use: "electric" }));   // UG3: no longer placed (shadow slots swapping read as flicker)
  out.push(lamp("tun_lamp_red", "Tunnel emergency lamp", { color: [1.0, 0.12, 0.06], intensity: 0.7, radius: 4.5, use: "electric" }));
  out.push(lamp("tun_lamp_dead", "Tunnel lamp (dead)", null));
  return out;
}
export const TUNNEL_CELL = CELL;
