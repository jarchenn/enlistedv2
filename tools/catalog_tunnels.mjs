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
  A.mat("puddle", [0.02, 0.025, 0.022], 0.05, "plain");
  A.mat("rust", [0.2, 0.095, 0.045], 0.8, "metal", { metallic: 0.2 });
  A.mat("steel", [0.12, 0.11, 0.1], 0.6, "metal", { metallic: 0.5 });
  A.mat("plank_y", [0.2, 0.15, 0.1], 0.9, "wood", { grain: 1 });
  A.mat("plank_x", [0.2, 0.15, 0.1], 0.9, "wood", { grain: 0 });
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
  const h = opt.h || H, lined = !!opt.lined, wallM = lined ? "concrete_wash" : "rock";
  // shell: floor and ceiling over the whole cell, the skin from the floor to the ceiling
  A.box([-CELL / 2, -FLOOR_T, -CELL / 2], [CELL / 2, 0, CELL / 2], lined ? "concrete_floor" : "mud");
  A.box([-CELL / 2, h, -CELL / 2], [CELL / 2, h + CEIL_T, CELL / 2], lined ? "concrete_wash" : "rock_dark");
  for (const [x0, z0, x1, z1] of rects(skin(g))) A.box([x0, -0.05, z0], [x1, h + 0.02, z1], wallM);
  // the portal frames: each open side keeps the shared PORTAL_H head (a lintel band under the ceiling)
  for (const s of sides) {
    const [dx, dz] = SIDE[s], e = CELL / 2;
    if (h > PORTAL_H + 0.01) A.box(dx ? [dx > 0 ? e - 0.25 : -e, PORTAL_H, -PORTAL / 2] : [-PORTAL / 2, PORTAL_H, dz > 0 ? e - 0.25 : -e],
      dx ? [dx > 0 ? e : -e + 0.25, h + 0.02, PORTAL / 2] : [PORTAL / 2, h + 0.02, dz > 0 ? e : -e + 0.25], wallM);
  }
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
    const ps = (opt.pipeSide || 1), off = PORTAL / 2 - 0.12, y = h - 0.42;
    const c0 = [px * ps * off, y, pz * ps * off], c1 = [dx * CELL / 2 + px * ps * off, y, dz * CELL / 2 + pz * ps * off];
    A.cyl([(c0[0] + c1[0]) / 2, y, (c0[2] + c1[2]) / 2], [0.09, CELL / 2, 0.09], "rust", { ...D, r: dx ? [0, 0, 90] : [90, 0, 0], n: 8 });
  }
  for (let k = 0; k < (opt.lumps ?? 10); k++) {   // rock lumps on the walls (visual roughness)
    const s = sides[Math.floor(R() * sides.length)], [dx, dz] = SIDE[s], px = -dz, pz = dx, t = 0.6 + R() * 2.6, sd = R() < 0.5 ? -1 : 1;
    const x = dx * t + px * sd * (PORTAL / 2 + 0.02), z = dz * t + pz * sd * (PORTAL / 2 + 0.02), y = 0.3 + R() * (h - 0.8), sz = 0.25 + R() * 0.35;
    A.obox([r3(x), r3(y), r3(z)], [sz, sz * 0.8, sz], [R() * 40, R() * 90, R() * 40], lined ? "concrete_wash" : "rock", D);
  }
  for (let k = 0; k < (opt.puddles ?? 2); k++) {   // mud and puddles on the floor
    const x = (R() - 0.5) * 1.6, z = (R() - 0.5) * 1.6, w = 0.6 + R() * 1.0;
    A.box([r3(x - w / 2), 0.002, r3(z - w * 0.35)], [r3(x + w / 2), 0.008, r3(z + w * 0.35)], R() < 0.5 ? "puddle" : "mud", { k: 0 });
  }
  if (opt.extra) opt.extra(A, R, h);
  // nav: the passage as rooms (hub + spokes), cover by the frames, snap sockets at the portals
  const rooms = [room("hub", [-hubR, 0, -hubR], [hubR, h, hubR])];
  for (const s of sides) { const [dx, dz] = SIDE[s]; rooms.push(room("sp_" + s, dx ? [dx > 0 ? 0 : -CELL / 2, 0, -PORTAL / 2] : [-PORTAL / 2, 0, dz > 0 ? 0 : -CELL / 2], dx ? [dx > 0 ? CELL / 2 : 0, h, PORTAL / 2] : [PORTAL / 2, h, dz > 0 ? CELL / 2 : 0])); }
  setNav(A, { effect: "block", rooms, doors: [], entrances: sides.map((s) => ({ p: [SIDE[s][0] * CELL / 2, 0, SIDE[s][1] * CELL / 2], dir: [SIDE[s][0], 0, SIDE[s][1]], room: "sp_" + s })),
    corners: [], firingPoints: [], cover: opt.cover || [], areas: [] });
  A.meta.snap = sides.map((s) => ({ p: [SIDE[s][0] * CELL / 2, 0, SIDE[s][1] * CELL / 2], dir: [SIDE[s][0], SIDE[s][1]], kind: "tunnel" }));
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
function chamber(id, name, kind, seed) {
  const R = rng(seed), S = 3 * CELL, hS = S / 2, h = 3.3;
  const A = new Asset({ id, version: 1, name, category: "Tunnels", floors: 1, enterable: true,
    description: name + ": a 24 x 24 m excavated chamber with a portal in the middle of each side.",
    footprint: [S, S], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  tmats(A);
  const lined = kind !== "collapse" && kind !== "sump", wallM = lined ? "concrete_wash" : "rock";
  // air: an irregular hall (a chamfered rectangle with bays) and four corridors to the portals
  const NN = S / VOX, a = new Uint8Array(NN * NN), c = (i) => -hS + (i + 0.5) * VOX;
  const inHall = (x, z) => Math.abs(x) < 8.5 && Math.abs(z) < 8.5 && Math.abs(x) + Math.abs(z) < 14.5;
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
  A.box([-hS, -FLOOR_T, -hS], [hS, 0, hS], lined ? "concrete_floor" : "mud");
  A.box([-hS, h, -hS], [hS, h + CEIL_T, hS], lined ? "concrete_wash" : "rock_dark");
  for (const [dx, dz] of [[0, 1], [1, 0], [0, -1], [-1, 0]]) {   // portal heads at the shared height
    const e = hS;
    A.box(dx ? [dx > 0 ? e - 0.25 : -e, PORTAL_H, -PORTAL / 2] : [-PORTAL / 2, PORTAL_H, dz > 0 ? e - 0.25 : -e], dx ? [dx > 0 ? e : -e + 0.25, h + 0.02, PORTAL / 2] : [PORTAL / 2, h + 0.02, dz > 0 ? e : -e + 0.25], wallM);
  }
  const D = { k: 0, lod: 1 };
  for (const x of [-5, 0, 5]) A.box([x - 0.15, h - 0.35, -8.5], [x + 0.15, h - 0.02, 8.5], lined ? "steel" : "plank_x", D);   // roof beams
  for (const [x, z] of [[-5, -5], [5, -5], [-5, 5], [5, 5]]) A.box([x - 0.16, 0, z - 0.16], [x + 0.16, h - 0.3, z + 0.16], lined ? "concrete_wash" : "plank_y");   // pillars (cover)
  if (kind === "sump") {   // flooded: a sunken pool in the middle, its dark water a little under the floor
    A.box([-5.2, -0.9, -5.2], [5.2, -0.6, 5.2], "mud", { k: 0 });
    A.box([-5.0, -0.05, -5.0], [5.0, -0.02, 5.0], "puddle", { k: 0 });
  }
  if (kind === "collapse") {   // a fallen roof slab and rock spill
    A.obox([2.5, 1.2, -2.0], [5.5, 0.5, 3.5], [0, 25, 18], "rock_dark");
    for (let k = 0; k < 14; k++) A.obox([r3(-3 + R() * 7), r3(0.2 + R() * 0.4), r3(-5 + R() * 6)], [0.6 + R() * 0.8, 0.4 + R() * 0.6, 0.6 + R() * 0.8], [R() * 40, R() * 90, R() * 40], "rock");
  }
  for (let k = 0; k < 6; k++) { const x = (R() - 0.5) * 14, z = (R() - 0.5) * 14, w = 0.8 + R() * 1.6; A.box([r3(x - w / 2), 0.002, r3(z - w / 3)], [r3(x + w / 2), 0.008, r3(z + w / 3)], R() < 0.5 ? "puddle" : "mud", { k: 0 }); }
  const cv = [];
  for (const [x, z] of [[-5, -5], [5, -5], [-5, 5], [5, 5]]) for (const yaw of [0, 90, 180, 270]) cv.push(cover([x + Math.sin((yaw * Math.PI) / 180) * -0.6, 0, z + Math.cos((yaw * Math.PI) / 180) * -0.6], yaw, "high", ["left", "right"]));
  setNav(A, { effect: "block", rooms: [room("hall", [-8.5, 0, -8.5], [8.5, h, 8.5])], doors: [],
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

export function tunnelAssets() {
  const out = [];
  const P = (id, name, sides, opt) => out.push(passage(id, name, sides, opt));
  P("tun_end", "Tunnel dead end", ["S"], { seed: 11, spokes: { S: { bulge: 2.9 } }, hub: 1.5, desc: "A tunnel ending in rough rock." });
  P("tun_straight", "Tunnel, straight", ["N", "S"], { seed: 12, spokes: { N: { bulge: 2.8 }, S: { bulge: 2.7 } } });
  P("tun_straight_kink", "Tunnel, kinked", ["N", "S"], { seed: 13, spokes: { N: { kink: 0.7, bulge: 2.9 }, S: { kink: -0.6, bulge: 2.8 } }, pipeSide: -1 });
  P("tun_straight_niche", "Tunnel, with a niche", ["N", "S"], { seed: 14, spokes: { N: { bulge: 3.0, niche: true }, S: { bulge: 2.7 } } });
  P("tun_straight_lined", "Tunnel, concrete-lined", ["N", "S"], { seed: 15, lined: true, lumps: 4, spokes: { N: { bulge: 2.6 }, S: { bulge: 2.6 } } });
  P("tun_wide", "Tunnel widening", ["N", "S"], { seed: 16, hub: 2.4, h: 3.0, spokes: { N: { bulge: 3.6 }, S: { bulge: 3.4 } },
    cover: [cover([1.4, 0, 0], 90, "low", ["over"]), cover([-1.4, 0, 0], 270, "low", ["over"])] });
  P("tun_crawl", "Tunnel squeeze (low)", ["N", "S"], { seed: 17, h: 2.6, lumps: 6, desc: "A pinched side passage: 1.5 m wide and 1.7 m high in the middle (too tight for the heavies).",
    extra: (A) => { A.box([-1.3, 1.7, -1.0], [1.3, 2.62, 1.0], "rock_dark"); A.box([-1.3, -0.05, -1.0], [-0.75, 1.75, 1.0], "rock"); A.box([0.75, -0.05, -1.0], [1.3, 1.75, 1.0], "rock"); } });
  P("tun_corner", "Tunnel bend", ["N", "E"], { seed: 18, hub: 1.7, spokes: { N: { bulge: 2.8, kink: 0.3 }, E: { bulge: 2.7, kink: -0.3 } } });
  P("tun_corner_lined", "Tunnel bend, lined", ["N", "E"], { seed: 19, lined: true, lumps: 3, hub: 1.6 });
  P("tun_tee", "Tunnel T junction", ["N", "E", "W"], { seed: 20, hub: 1.9, h: 2.9, spokes: { N: { bulge: 2.8 }, E: { bulge: 2.7, kink: 0.25 }, W: { bulge: 2.9 } } });
  P("tun_cross", "Tunnel crossing", ["N", "E", "S", "W"], { seed: 21, hub: 2.1, h: 2.9, spokes: { N: { bulge: 2.8 }, E: { bulge: 2.7 }, S: { bulge: 2.8 }, W: { bulge: 2.7 } } });
  out.push(stairs());
  out.push(chamber("tun_chamber_store", "Tunnel chamber: storage", "store", 31));
  out.push(chamber("tun_chamber_bunks", "Tunnel chamber: bunk room", "bunks", 32));
  out.push(chamber("tun_chamber_generator", "Tunnel chamber: generator room", "generator", 33));
  out.push(chamber("tun_chamber_sump", "Tunnel chamber: flooded sump", "sump", 34));
  out.push(chamber("tun_chamber_collapse", "Tunnel chamber: collapsed", "collapse", 35));
  out.push(lamp("tun_lamp", "Tunnel lamp", { color: [1.0, 0.72, 0.42], intensity: 0.75, radius: 7.5, use: "electric" }));
  out.push(lamp("tun_lamp_flicker", "Tunnel lamp (failing)", { color: [1.0, 0.7, 0.4], intensity: 0.65, radius: 7.0, flicker: 0.55, use: "electric" }));
  out.push(lamp("tun_lamp_shadow", "Tunnel lamp (shadows)", { color: [1.0, 0.74, 0.45], intensity: 0.85, radius: 8.0, shadow: 1, use: "electric" }));
  out.push(lamp("tun_lamp_red", "Tunnel emergency lamp", { color: [1.0, 0.12, 0.06], intensity: 0.7, radius: 6.0, flicker: 0.15, use: "electric" }));
  out.push(lamp("tun_lamp_dead", "Tunnel lamp (dead)", null));
  return out;
}
export const TUNNEL_CELL = CELL;
