// BR1 backrooms kit (docs/backrooms.md): an enclosed interior on an 8 m grid for the Backrooms map
// (tools/mapgen/maps/backrooms.py). Three zones:
//   y  yellow rooms: faded yellow wallpaper, damp beige carpet, a low suspended-tile ceiling (2.7 m), brown trim
//   s  service areas: bare stained concrete, exposed pipes and cable trays under a 3.2 m slab, electrical boxes
//   p  poolrooms: pale square tiles on floor, walls and ceiling (3.4 m), still turquoise water, arches
// A map is cells (brm_cell_*: an 8 x 8 m floor slab and the zone's ceiling over the whole cell) with walls on the cell
// edges (brm_wall_*: 8 m long, 3.6 m high so they close the gap between any two ceiling heights; plain, doorway, wide
// opening, pool arch). Free-standing partitions and columns repeat inside the yellow rooms. Lights hang under the
// ceiling (instance y = the ceiling): fluorescent panels (most only emissive, every few a real light, some dead), pool
// lights, service bulkheads; none flicker. Dressing: furniture built here from boxes, tilted and half sunk into the
// floor or a wall (instances have yaw only, so the tilt lives in the asset), piles of chairs and wardrobes.
// Ids "brm_*". Original work in the style only: no logos, text or assets from any game or film.
import { Asset } from "./kit.mjs";
import { rng, room, cover, setNav } from "./catalog_atlantic.mjs";

const CELL = 8, HC = CELL / 2, WALL_H = 4.6, WT = 0.2, FLOOR_T = 0.3, CEIL_T = 0.3;
export const BR_CELL = CELL;
export const BR_CEIL = { y: 3.6, s: 4.2, p: 4.4 };   // BR3: about 1.3x higher (was 2.7, 3.2, 3.4)
const r3 = (v) => Math.round(v * 1000) / 1000;
const AMB = 1.0;   // BR2: the surfaces' faint glow (a stand-in for bounce light); the 1.5x ambient is the map's extra.lighting.ambient (render.cpp), which lights furniture too
const amb = (c) => c.map((v) => r3(v * AMB));
const NAV0 = { rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] };

function bmats(A) {
  A.mat("wallpaper", [0.98, 0.9, 0.12], 0.9, "plain", { texture: "wallpaper_backrooms", emissive: amb([0.16, 0.145, 0.02]) });   // BR2: the striped yellow paper (tools/gen_wallpaper_backrooms.py)   // BR2: plain saturated yellow, only a fine low-contrast grain (painted plaster slot)
  A.mat("wallpaper_stain", [0.5, 0.43, 0.22], 0.9, "painted", { texture: "wallpaper_aged" });
  A.mat("trim", [0.9, 0.81, 0.12], 0.8, "plain", { emissive: amb([0.15, 0.135, 0.02]) });   // BR2: the wall's yellow (no dark skirting)
  // BR2: the yellow surfaces glow faintly: a stand-in for the bounce light the night lighting (no GI) does not give
  A.mat("carpet", [0.94, 0.84, 0.14], 0.97, "plain", { texture: "none", emissive: amb([0.18, 0.16, 0.025]) });   // BR2: plain yellow carpet   // BR2: yellow-beige   // BR2: clean beige-yellow
  A.mat("carpet_damp", [0.36, 0.3, 0.19], 0.8, "fabric", { texture: "rug_beige" });
  A.mat("ceiling_tile", [0.92, 0.87, 0.22], 0.95, "plain", { emissive: amb([0.2, 0.19, 0.04]) });   // BR2: pale yellow   // BR2: flat pale yellow
  A.mat("tbar", [0.78, 0.72, 0.18], 0.8, "plain", { emissive: amb([0.16, 0.15, 0.03]) });
  A.mat("concrete", [0.46, 0.45, 0.42], 0.9, "concrete", { texture: "concrete_stained", emissive: amb([0.06, 0.058, 0.052]) });
  A.mat("concrete_floor", [0.36, 0.35, 0.33], 0.85, "concrete", { texture: "concrete_aged", emissive: amb([0.045, 0.044, 0.04]) });
  A.mat("pipe", [0.34, 0.32, 0.28], 0.55, "metal", { metallic: 0.5 });
  A.mat("pipe_green", [0.42, 0.52, 0.44], 0.6, "metal", { metallic: 0.2 });
  A.mat("rust", [0.24, 0.12, 0.06], 0.8, "metal", { metallic: 0.2 });
  A.mat("ebox", [0.44, 0.46, 0.42], 0.6, "metal", { metallic: 0.4 });
  A.mat("tile", [0.86, 0.88, 0.84], 0.25, "plain", { texture: "floor_cement_tile", emissive: amb([0.07, 0.08, 0.078]) });
  A.mat("tile_pool", [0.62, 0.84, 0.8], 0.2, "plain", { texture: "floor_cement_tile", emissive: amb([0.03, 0.07, 0.065]) });
  A.mat("grout_dark", [0.5, 0.55, 0.52], 0.5, "plain");
  A.mat("water_proxy", [0.05, 0.12, 0.12], 0.1, "glass");
  A.mat("puddle", [0.03, 0.03, 0.025], 0.05, "plain");
}

function cellNav(A, h) {
  const sides = [[0, 1], [1, 0], [0, -1], [-1, 0]];
  setNav(A, { ...NAV0, effect: "block", rooms: [room("cell", [-HC, 0, -HC], [HC, h, HC])],
    entrances: sides.map(([dx, dz]) => ({ p: [dx * HC, 0, dz * HC], dir: [dx, 0, dz], room: "cell" })) });
  A.meta.snap = sides.map(([dx, dz]) => ({ p: [dx * HC, 0, dz * HC], dir: [dx, dz], kind: "backrooms" }));
}
function water(A, half, level) {   // still turquoise water over a square (src/water.cpp)
  const o = [[-half, -half], [half, -half], [half, half], [-half, half]];
  (A.meta.water ??= { surfaces: [] }).surfaces.push({ outline: o.map((p) => p.map(r3)), level: r3(level), kind: "pond", clarity: 3.0, waves: 0.08, tint: [0.03, 0.26, 0.22] });
  A.box([-half, level - 0.01, -half], [half, level, half], "water_proxy", { k: 0 });
}

// ---------------------------------------------------------------- cells (floor + ceiling over one 8 m cell)
function cell(id, name, zone, opt = {}) {
  const R = rng(opt.seed || 1), h = BR_CEIL[zone];
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: opt.desc || name, floors: 1, enterable: true,
    footprint: [CELL, CELL], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  bmats(A);
  const D = { k: 0, lod: 1 };
  const floorM = zone === "y" ? "carpet" : zone === "s" ? "concrete_floor" : "tile";
  if (opt.basin) {   // a sunken pool: a 6 x 6 m basin 1.0 m deep, steps down on its south side
    const b = 3.0, d = 1.0;
    A.box([-HC, -FLOOR_T, -HC], [HC, 0, -b], floorM); A.box([-HC, -FLOOR_T, b], [HC, 0, HC], floorM);
    A.box([-HC, -FLOOR_T, -b], [-b, 0, b], floorM); A.box([b, -FLOOR_T, -b], [HC, 0, b], floorM);
    A.box([-b, -d - FLOOR_T, -b], [b, -d, b], "tile_pool");
    for (const [x0, z0, x1, z1] of [[-b, -b, b, -b + 0.15], [-b, b - 0.15, b, b], [-b, -b, -b + 0.15, b], [b - 0.15, -b, b, b]]) A.box([x0, -d, z0], [x1, 0, z1], "tile_pool");
    for (let k = 0; k < 4; k++) A.box([-0.9, -d, -b + 0.15 + k * 0.32], [0.9, -d + (4 - k) * (d / 5), -b + 0.15 + (k + 1) * 0.32], "tile_pool");
    for (const x of [-1.0, 1.0]) A.cyl([x, 0.35, -b + 0.2], [0.04, 0.9, 0.04], "pipe", { k: 0, n: 6 });   // hand rails
    water(A, b - 0.02, -0.15);
  } else A.box([-HC, -FLOOR_T, -HC], [HC, 0, HC], floorM);
  A.box([-HC, h, -HC], [HC, h + CEIL_T, HC], zone === "y" ? "ceiling_tile" : zone === "s" ? "concrete" : "tile");
  if (zone === "y") {   // the suspended grid: T-bars every 1.2 / 0.6 m under the ceiling tiles
    for (let x = -HC + 0.4; x < HC; x += 1.2) A.box([r3(x - 0.015), h - 0.02, -HC], [r3(x + 0.015), h, HC], "tbar", D);
    for (let z = -HC + 0.4; z < HC; z += 1.2) A.box([-HC, h - 0.02, r3(z - 0.015)], [HC, h, r3(z + 0.015)], "tbar", D);
  }
  if (zone === "s") {   // exposed pipe runs and a cable tray under the slab
    const runs = opt.cross ? [["x", -2.2], ["x", 1.6], ["z", 2.6], ["z", -0.8]] : [["x", -2.4], ["x", -1.8], ["x", 1.9], ["x", 2.5]];
    runs.forEach(([ax, o], k) => {
      const y = h - 0.25 - (k % 2) * 0.32, r = k % 2 ? 0.07 : 0.13, m = k === 2 ? "pipe_green" : k === 3 ? "rust" : "pipe";
      if (ax === "x") A.cyl([0, y, o], [r * 2, CELL, r * 2], m, { ...D, r: [0, 0, 90], n: 10 });
      else A.cyl([o, y, 0], [r * 2, CELL, r * 2], m, { ...D, r: [90, 0, 0], n: 10 });
    });
    A.box([-HC, h - 0.62, -0.6], [HC, h - 0.58, -0.2], "ebox", D);   // cable tray
    for (let x = -HC + 1; x < HC; x += 2) A.box([r3(x - 0.02), h - 0.62, -0.62], [r3(x + 0.02), h, -0.58], "ebox", D);
    if (opt.cross) A.cyl([2.9, h / 2, 2.9], [0.24, h, 0.24], "pipe", { k: 0, n: 10 });

  }
  if (zone === "p" && opt.flooded) water(A, HC, 0.28);
  cellNav(A, h);
  return A;
}

// ---------------------------------------------------------------- walls on the cell edges (local x along the edge)
function wall(id, name, zone, gap = null, opt = {}) {
  const R = rng(opt.seed || 3);
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: opt.desc || name, floors: 0, enterable: false,
    footprint: [CELL, WT], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  bmats(A);
  const m = zone === "y" ? "wallpaper" : zone === "s" ? "concrete" : "tile", t = WT / 2, D = { k: 0, lod: 1 };
  if (!gap) A.box([-HC, 0, -t], [HC, WALL_H, t], m);
  else {
    const [w, hh] = gap;
    A.box([-HC, 0, -t], [-w / 2, WALL_H, t], m); A.box([w / 2, 0, -t], [HC, WALL_H, t], m);
    A.box([-w / 2, hh, -t], [w / 2, WALL_H, t], m);
    if (opt.arch) for (let k = 1; k <= 3; k++) {   // stepped arch head
      const ww = (w / 2) * (1 - (k / 4) ** 2), y0 = hh - k * 0.18;
      A.box([-w / 2, y0, -t], [-ww, y0 + 0.18, t], m); A.box([ww, y0, -t], [w / 2, y0 + 0.18, t], m);
    }
  }
  const spans = gap ? [[-HC, -gap[0] / 2], [gap[0] / 2, HC]] : [[-HC, HC]];
  if (zone === "y") for (const sd of [-1, 1]) for (const [a, b] of spans) {   // a baseboard on both faces (BR2: no stains)
    A.box([a, 0, sd > 0 ? t : -t - 0.015], [b, 0.11, sd > 0 ? t + 0.015 : -t], "trim", D);
  }
  if (zone === "s") {   // electrical boxes and conduit on one face
    const x = gap ? (R() < 0.5 ? -2.6 : 2.6) : (R() - 0.5) * 4;
    A.box([x - 0.3, 1.2, t], [x + 0.3, 1.9, t + 0.18], "ebox", D);
    A.box([x - 0.03, 1.9, t], [x + 0.03, WALL_H, t + 0.06], "pipe", D);
    if (R() < 0.6) A.box([x + 0.5, 1.4, t], [x + 0.8, 1.7, t + 0.12], "ebox", D);
  }
  if (zone === "p") for (const sd of [-1, 1]) for (const [a, b] of spans) A.box([a, 0, sd > 0 ? t : -t - 0.01], [b, 0.08, sd > 0 ? t + 0.01 : -t], "grout_dark", D);
  setNav(A, { ...NAV0, effect: "block" });
  return A;
}

// ---------------------------------------------------------------- free-standing partitions and columns
function partition(id, name, zone, segs, h) {
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: name, floors: 0, enterable: false,
    footprint: [4, 4], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  bmats(A);
  const m = zone === "y" ? "wallpaper" : zone === "s" ? "concrete" : "tile";
  for (const [x0, z0, x1, z1] of segs) {
    A.box([x0, 0, z0], [x1, h, z1], m);
    if (zone === "y") A.box([x0 - 0.012, 0, z0 - 0.012], [x1 + 0.012, 0.1, z1 + 0.012], "trim", { k: 0 });
  }
  const cv = segs.length === 1 && Math.abs(segs[0][2] - segs[0][0]) > 1 ? [cover([0, 0, 0.5], 0, "high", ["left", "right"]), cover([0, 0, -0.5], 180, "high", ["left", "right"])] : [];
  setNav(A, { ...NAV0, effect: "block", cover: cv });
  return A;
}
function column(id, name, zone, h, round = false) {
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: name, floors: 0, enterable: false,
    footprint: [0.8, 0.8], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  bmats(A);
  const m = zone === "y" ? "wallpaper" : zone === "s" ? "concrete" : "tile";
  if (round) A.cyl([0, h / 2, 0], [0.8, h, 0.8], m, { n: 16 }); else A.box([-0.6, 0, -0.6], [0.6, h, 0.6], m);   // BR2: thick 1.2 m pillars
  if (zone === "y") A.box([-0.365, 0, -0.365], [0.365, 0.1, 0.365], "trim", { k: 0 });
  setNav(A, { ...NAV0, effect: "block", cover: [cover([0, 0, 0.7], 0, "high", ["left", "right"]), cover([0, 0, -0.7], 180, "high", ["left", "right"])] });
  return A;
}

// ---------------------------------------------------------------- lights (origin at the ceiling underside)
function light(id, name, kind, L, glow = !!L) {
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: name, floors: 0, enterable: false,
    footprint: [1.2, 0.6], plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  A.mat("frame", kind === "panel" ? [0.92, 0.87, 0.22] : [0.7, 0.7, 0.66], 0.5, kind === "panel" ? "plain" : "metal", kind === "panel" ? { emissive: amb([0.2, 0.19, 0.04]) } : { metallic: 0.3 });   // BR2: flush panels, no dark frame
  A.mat("diffuser", [0.95, 0.94, 0.88], 0.4, "plain", glow ? { emissive: kind === "bulkhead" ? [9.0, 8.0, 6.2] : [14.0, 13.6, 11.5] } : {});   // BR2: slightly warm
  if (kind === "panel") {   // a 1.0 x 1.0 m flush square fluorescent panel (BR2)
    A.box([-0.5, -0.012, -0.5], [0.5, 0, 0.5], "frame", { k: 0 });
    A.box([-0.47, -0.016, -0.47], [0.47, -0.008, 0.47], "diffuser", { k: 0 });
  } else if (kind === "pool") {   // a round recessed light
    A.cyl([0, -0.015, 0], [0.7, 0.03, 0.7], "frame", { k: 0, n: 16 });
    A.cyl([0, -0.025, 0], [0.6, 0.012, 0.6], "diffuser", { k: 0, n: 16 });
  } else {   // a caged bulkhead under the slab
    A.box([-0.2, -0.12, -0.12], [0.2, 0, 0.12], "frame", { k: 0 });
    A.box([-0.16, -0.16, -0.09], [0.16, -0.12, 0.09], "diffuser", { k: 0 });
  }
  if (L) A.meta.light = { offset: [0, kind === "bulkhead" ? -0.25 : -0.12, 0], use: "electric", ...L };
  setNav(A, { ...NAV0, effect: "none" });
  return A;
}

// ---------------------------------------------------------------- furniture from boxes, tilted as one (glm::quat(euler) like the engine)
function quat([rx, ry, rz]) {
  const d = Math.PI / 360, cx = Math.cos(rx * d), cy = Math.cos(ry * d), cz = Math.cos(rz * d), sx = Math.sin(rx * d), sy = Math.sin(ry * d), sz = Math.sin(rz * d);
  return [cx * cy * cz + sx * sy * sz, sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz];   // w x y z
}
function rot(q, [x, y, z]) {
  const [w, qx, qy, qz] = q, tx = 2 * (qy * z - qz * y), ty = 2 * (qz * x - qx * z), tz = 2 * (qx * y - qy * x);
  return [x + w * tx + (qy * tz - qz * ty), y + w * ty + (qz * tx - qx * tz), z + w * tz + (qx * ty - qy * tx)];
}
// boxes [min, max, mat] in the piece's own frame (origin on the floor at its centre) -> rotated by tilt, moved to at
function put(A, boxes, at, tilt, o = {}) {
  const q = quat(tilt);
  for (const [mn, mx, m] of boxes) {
    const c = rot(q, [0, 1, 2].map((i) => (mn[i] + mx[i]) / 2)), s = [0, 1, 2].map((i) => Math.abs(mx[i] - mn[i]));
    A.obox([r3(c[0] + at[0]), r3(c[1] + at[1]), r3(c[2] + at[2])], s.map(r3), tilt.map(r3), m, o);
  }
}
const SHAPES = {
  chair: () => [[[-0.22, 0.42, -0.22], [0.22, 0.46, 0.22], "wood_f"], ...[[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([a, b]) => [[a * 0.2 - 0.02, 0, b * 0.2 - 0.02], [a * 0.2 + 0.02, 0.42, b * 0.2 + 0.02], "wood_f"]),
    [[-0.22, 0.46, 0.18], [0.22, 0.9, 0.22], "wood_f"]],
  wardrobe: () => [[[-0.5, 0, -0.28], [0.5, 1.95, 0.28], "wood_d"], [[-0.005, 0.1, 0.28], [0.005, 1.85, 0.29], "trim"], [[-0.52, 1.95, -0.3], [0.52, 2.02, 0.3], "wood_d"]],
  table: () => [[[-0.6, 0.72, -0.4], [0.6, 0.76, 0.4], "wood_f"], ...[[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([a, b]) => [[a * 0.55 - 0.03, 0, b * 0.35 - 0.03], [a * 0.55 + 0.03, 0.72, b * 0.35 + 0.03], "wood_f"])],
  bed: () => [[[-0.5, 0.2, -1.0], [0.5, 0.45, 1.0], "wood_d"], [[-0.48, 0.45, -0.98], [0.48, 0.6, 0.98], "mattress"], [[-0.5, 0, 0.95], [0.5, 1.0, 1.02], "wood_d"],
    ...[[-1, -1], [1, -1]].map(([a, b]) => [[a * 0.47 - 0.03, 0, b * 0.97 - 0.03], [a * 0.47 + 0.03, 0.2, b * 0.97 + 0.03], "wood_d"])],
  armchair: () => [[[-0.45, 0, -0.4], [0.45, 0.42, 0.4], "fabric_f"], [[-0.45, 0.42, 0.25], [0.45, 0.95, 0.4], "fabric_f"], [[-0.45, 0.42, -0.4], [-0.33, 0.65, 0.25], "fabric_f"], [[0.33, 0.42, -0.4], [0.45, 0.65, 0.25], "fabric_f"]],
};
function fmats(A) {
  bmats(A);
  A.mat("wood_f", [0.36, 0.26, 0.17], 0.7, "wood", { grain: 1 });
  A.mat("wood_d", [0.28, 0.2, 0.13], 0.7, "wood", { grain: 1 });
  A.mat("mattress", [0.62, 0.58, 0.5], 0.95, "fabric");
  A.mat("fabric_f", [0.42, 0.36, 0.26], 0.95, "fabric");
}
function sunk(id, name, items, desc, fp = [2, 2]) {
  const A = new Asset({ id, version: 1, name, category: "Backrooms", description: desc, floors: 0, enterable: false,
    footprint: fp, plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  fmats(A);
  for (const [shape, at, tilt, o] of items) put(A, SHAPES[shape](), at, tilt, o || {});
  setNav(A, { ...NAV0, effect: "carve" });
  return A;
}
function pile(id, name, shape, n, seed, spread, desc) {
  const R = rng(seed), items = [];
  for (let k = 0; k < n; k++) {
    const lvl = Math.floor(k / Math.ceil(n / 3));
    items.push([shape, [r3((R() - 0.5) * spread), r3(lvl * (shape === "chair" ? 0.45 : 0.6)), r3((R() - 0.5) * spread)], [r3((R() - 0.5) * 120), r3(R() * 360), r3((R() - 0.5) * 120)], { k: 0 }]);
  }
  const A = sunk(id, name, items, desc, [spread + 1, spread + 1]);
  const hgt = shape === "chair" ? 1.4 : 2.0;
  A.box([-spread / 2, 0, -spread / 2], [spread / 2, hgt, spread / 2], "water_proxy", { v: 0 });   // one collider round the heap
  return A;
}

export function backroomsAssets() {
  const out = [];
  out.push(cell("brm_cell_yellow", "Yellow room cell", "y", { seed: 1, desc: "8 x 8 m of beige carpet under a 2.7 m suspended-tile ceiling." }));
  out.push(cell("brm_cell_yellow_damp", "Yellow room cell, damp", "y", { seed: 2, damp: true, desc: "Yellow room cell with the carpet soaked in dark patches, a puddle and stained ceiling tiles." }));
  out.push(cell("brm_cell_service", "Service cell", "s", { seed: 3, desc: "Bare concrete under a 3.2 m slab with pipe runs and a cable tray." }));
  out.push(cell("brm_cell_service_cross", "Service cell, crossing pipes", "s", { seed: 4, cross: true }));
  out.push(cell("brm_cell_pool", "Poolroom cell", "p", { seed: 5, desc: "Pale square tiles on the floor and the 3.4 m ceiling." }));
  out.push(cell("brm_cell_pool_flooded", "Poolroom cell, flooded", "p", { seed: 6, flooded: true, desc: "A tiled cell under 0.28 m of still turquoise water (you wade through)." }));
  out.push(cell("brm_cell_pool_basin", "Poolroom cell, basin", "p", { seed: 7, basin: true, desc: "A 6 x 6 m tiled basin 1 m deep full of still water, steps and rails on its south side." }));
  let s = 10;
  for (const [z, zn] of [["y", "yellow"], ["s", "service"], ["p", "pool"]]) {
    out.push(wall("brm_wall_" + z, "Backrooms wall (" + zn + ")", z, null, { seed: s++ }));
    out.push(wall("brm_wall_" + z + "_door", "Backrooms wall, doorless doorway (" + zn + ")", z, [1.3, 2.6], { seed: s++ }));
    out.push(wall("brm_wall_" + z + "_wide", "Backrooms wall, wide opening (" + zn + ")", z, [4.0, 3.1], { seed: s++ }));
  }
  out.push(wall("brm_wall_p_arch", "Poolroom wall, arch", "p", [3.0, 3.4], { seed: s++, arch: true }));
  out.push(partition("brm_partition", "Yellow partition (4 m)", "y", [[-2.0, -0.075, 2.0, 0.075]], BR_CEIL.y));
  out.push(partition("brm_partition_short", "Yellow partition (2.4 m)", "y", [[-1.2, -0.075, 1.2, 0.075]], BR_CEIL.y));
  out.push(partition("brm_partition_l", "Yellow partition, L", "y", [[-1.8, -0.075, 1.2, 0.075], [1.05, -0.075, 1.2, 1.9]], BR_CEIL.y));
  out.push(partition("brm_partition_low", "Yellow half partition", "y", [[-1.6, -0.075, 1.6, 0.075]], 1.25));
  out.push(column("brm_column_y", "Yellow room column", "y", BR_CEIL.y));
  out.push(column("brm_column_s", "Service column", "s", BR_CEIL.s));
  out.push(column("brm_column_p", "Poolroom column", "p", BR_CEIL.p, true));
  out.push(light("brm_panel", "Fluorescent panel", "panel", null, true));   // most panels only glow: the lit ones light the room
  out.push(light("brm_panel_lit", "Fluorescent panel (light)", "panel", { color: [1.0, 0.97, 0.85], intensity: 3.8, radius: 12.0, shadow: 1 }));
  out.push(light("brm_panel_dead", "Fluorescent panel (dead)", "panel", null));
  out.push(light("brm_pool_light", "Poolroom light", "pool", { color: [0.88, 0.98, 0.96], intensity: 1.5, radius: 13.0 }));
  out.push(light("brm_bulkhead", "Service bulkhead light", "bulkhead", { color: [1.0, 0.9, 0.74], intensity: 1.6, radius: 10.0 }));
  out.push(sunk("brm_sunk_wardrobe_floor", "Wardrobe sunk into the floor", [["wardrobe", [0, -0.75, 0], [18, 30, -12]]], "A wardrobe half swallowed by the floor, leaning."));
  out.push(sunk("brm_sunk_wardrobe_wall", "Wardrobe sunk into a wall", [["wardrobe", [0, 0.1, -0.15], [-14, 0, 8]]], "A wardrobe pushed half into the wall behind it (origin on the wall face, the back half inside it).", [1.4, 1.0]));
  out.push(sunk("brm_sunk_bed_wall", "Bed sunk into a wall", [["bed", [0, 0.9, -0.2], [-62, 0, 6]]], "A bed stood up on its end, its head buried in the wall (origin on the wall face).", [1.4, 1.4]));
  out.push(sunk("brm_sunk_table_floor", "Table sunk into the floor", [["table", [0, -0.35, 0], [24, 15, 31]]], "A table tipped sideways into the floor, two legs in the air."));
  out.push(sunk("brm_sunk_armchair_floor", "Armchair sunk into the floor", [["armchair", [0, -0.3, 0], [-21, 140, 9]]], "An armchair sinking into the carpet."));
  out.push(sunk("brm_sunk_chair_wall", "Chair sunk into a wall", [["chair", [0, 0.9, -0.05], [74, 20, 0]]], "A chair half inside the wall at chest height (origin on the wall face).", [0.8, 0.8]));
  out.push(pile("brm_pile_chairs", "Pile of chairs", "chair", 11, 71, 1.8, "A heap of identical chairs tangled at odd angles."));
  out.push(pile("brm_pile_wardrobes", "Pile of wardrobes", "wardrobe", 4, 72, 2.2, "Wardrobes tipped onto each other."));
  out.push(sunk("brm_chair_lone", "A lone chair", [["chair", [0, 0, 0], [0, 0, 0]]], "One chair, facing nothing.", [0.6, 0.6]));
  return out;
}
