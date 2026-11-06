// Village buildings for the MP & Creative update (SPEC 3.3 / 3.4 / 3.6, docs/assets-mpu.md): a Norman longere
// farmhouse, a stone barn with a hay loft, the village church with its steeple (sniper position in the belfry), a
// roofless ruined church and the ruined chateau keep of Le Plessis-Lastelle. All enterable, built with the house kit
// (two-layer rubble / plaster walls with real openings, destructible wall bays, hinged doors and casements, stairs
// with a smooth collision ramp) and furnished like the existing houses.
import { Asset, wbox } from "./kit.mjs";
import { stdMaterials, wall, partition, window, door, stairs, railing, quoins, gableRoof, table, chair, bench, bed, armoire, dresser, chest } from "./parts.mjs";
import { shelfX, shelfZ, crates, barrel, sacks, stool } from "./furniture2.mjs";
import { mats, rng, fire, cover, room, corners, setNav, setTour, r3 } from "./catalog_atlantic.mjs";

const PT = 0.03;
const pnM = (side, y0, y1, GF) => ({ side, family: "masonry", bands: [[y0, y1, 0, GF]] });
function shell(A, { X, Z, T, GF, EAVE, front = [], back = [], east = [], west = [], panel = true }) {
  const XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  A.box([-X - 0.05, -0.9, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  const p = (s) => (panel ? pnM(s, y0, EAVE, GF) : null);
  wall(A, { axis: "x", outer: Z, inner: ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: front, pt: PT, panel: p("front") });
  wall(A, { axis: "x", outer: -Z, inner: -ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: back, pt: PT, panel: p("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - PT, ZI + PT], aIn: [-ZI, ZI], y0, y1: EAVE, holes: east, pt: PT, panel: p("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - PT, ZI + PT], aIn: [-ZI, ZI], y0, y1: EAVE, holes: west, pt: PT, panel: p("west") });
  return { XI, ZI, y0 };
}
// slate gable roof over x0..x1 (ridge along X, symmetric in Z); gables: which end gables to build
function roofX(A, x0, x1, Z, EAVE, RIDGE, { ov = 0.35, gov = 0.28, t = 0.14, gables = [true, true], wallT = 0.6, gableMat = "stone" } = {}) {
  const xc = (x0 + x1) / 2, X = (x1 - x0) / 2, th = Math.atan2(RIDGE - EAVE, Z), deg = (th * 180) / Math.PI;
  if (gables[0]) A.prism(0, x0, x0 + wallT, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], gableMat);
  if (gables[1]) A.prism(0, x1 - wallT, x1, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], gableMat);
  const gl = gables[0] ? gov : 0, gr = gables[1] ? gov : 0;
  for (const s of [1, -1]) {
    const zA = -0.12 * s, zB = s * (Z + ov), zm = (zA + zB) / 2, ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2;
    const len = Math.abs(zB - zA) / Math.cos(th), a = s > 0 ? deg : -deg;
    const nrm = [0, Math.cos((a * Math.PI) / 180), Math.sin((a * Math.PI) / 180)];
    A.obox([xc + (gr - gl) / 2, ym + (nrm[1] * t) / 2, zm + (nrm[2] * t) / 2], [2 * X + gl + gr, t, len], [a, 0, 0], "slate");
    const ye = EAVE - ov * Math.tan(th);
    A.box([x0 - gl, ye - 0.24, Math.min(s * (Z + ov - 0.03), s * (Z + ov))], [x1 + gr, ye + 0.02, Math.max(s * (Z + ov - 0.03), s * (Z + ov))], "oak_x");
    A.box([x0 - gl, ye - 0.2, Math.min(s * (Z + ov), s * (Z + ov + 0.13))], [x1 + gr, ye - 0.1, Math.max(s * (Z + ov), s * (Z + ov + 0.13))], "zinc", { k: 0 });
  }
  A.obox([xc + (gr - gl) / 2, RIDGE + t / Math.cos(th) - 0.02, 0], [2 * X + gl + gr + 0.04, 0.2, 0.2], [45, 0, 0], "ridge_tile");
}
// a wall whose top is broken: equal bays along a0..a1, each with its own height, plus loose stones on the tops
function raggedWall(A, R, { axis, outer, inner, a0, a1, y0, heights, holes = [], side }) {
  const n = heights.length, w = (a1 - a0) / n;
  for (let i = 0; i < n; i++) {
    const b0 = a0 + i * w, b1 = b0 + w, h = heights[i];
    if (h <= y0 + 0.05) continue;
    const hs = holes.filter((q) => q.a1 > b0 && q.a0 < b1).map((q) => ({ ...q, a0: Math.max(q.a0, b0), a1: Math.min(q.a1, b1) }));
    wall(A, { axis, outer, inner, aOut: [b0, b1], aIn: [b0, b1], y0, y1: h, holes: hs, pt: PT, panel: h - y0 > 1.5 && w > 1.2 ? pnM(side + "_" + i, y0, h, y0 + 0.02) : null });
    for (let k = 0; k < 2; k++) { const a = b0 + 0.2 + R() * (w - 0.6), d0 = Math.min(outer, inner), d1 = Math.max(outer, inner); wbox(A, axis, a, a + 0.25 + R() * 0.35, h, h + 0.12 + R() * 0.25, d0 + R() * 0.2, d1 - R() * 0.2, "stone", { k: 0 }); }
  }
}
// pews facing +X (toward the altar) in a block z0..z1, rows from x0 to x1
function pews(A, x0, x1, z0, z1, GF) {
  for (let x = x0; x <= x1 + 1e-6; x += 1.0) {
    A.beginFurniture("Pew");
    A.box([x - 0.2, GF + 0.42, z0], [x + 0.2, GF + 0.47, z1], "oak_z");
    A.box([x - 0.24, GF + 0.47, z0], [x - 0.19, GF + 0.95, z1], "oak_z");
    for (const z of [z0, z1 - 0.05]) A.box([x - 0.24, GF, z], [x + 0.2, GF + 0.95, z + 0.05], "oak_y");
    A.endFurniture();
  }
}
// square slate spire from y0 to y1 over a base of half-width h: each face a stack of leaning bands
function spire(A, cx, cz, h, y0, y1, bands = 8) {
  const H = y1 - y0, lean = Math.atan2(h, H), ldeg = (lean * 180) / Math.PI, sl = H / Math.cos(lean) / bands;
  for (let b = 0; b < bands; b++) {
    const f0 = b / bands, f1 = (b + 1) / bands, fm = (f0 + f1) / 2, w = 2 * h * (1 - fm) + 0.1, y = y0 + H * fm, off = h * (1 - fm);
    A.obox([cx, y, cz + off], [w, sl + 0.04, 0.12], [-ldeg, 0, 0], "slate");
    A.obox([cx, y, cz - off], [w, sl + 0.04, 0.12], [ldeg, 0, 0], "slate");
    A.obox([cx + off, y, cz], [0.12, sl + 0.04, w], [0, 0, ldeg], "slate");
    A.obox([cx - off, y, cz], [0.12, sl + 0.04, w], [0, 0, -ldeg], "slate");
  }
  A.box([cx - 0.05, y1 - 0.2, cz - 0.05], [cx + 0.05, y1 + 1.4, cz + 0.05], "iron", { k: 0 });   // cross
  A.box([cx - 0.35, y1 + 0.85, cz - 0.04], [cx + 0.35, y1 + 0.95, cz + 0.04], "iron", { k: 0 });
}
const unglazed = (p, yaw) => ({ ...fire(p, yaw, "window"), unglazed: true });

// ============================================================================ Norman longere farmhouse
// 15 x 7 m single-storey longhouse under a steep slate roof: kitchen with hearth (west), salle with the box bed
// (middle), cellier / dairy (east, own door). The loft is closed off (one floor: no second stair route needed).
export function normanFarmhouse() {
  const A = new Asset({ id: "vil_norman_farmhouse", version: 1, name: "Norman stone farmhouse (longere)", category: "Houses",
    description: "Norman longere, 15 x 7 m: rubble-limestone walls under a steep slate roof. Kitchen with an open hearth (west), the salle with a curtained box bed and armoire (middle), and the cellier with barrels and the churn (east, its own door). Two front doors, a rear kitchen door, shuttered casements. Single storey (loft closed).",
    floors: 1, enterable: true, footprint: [15.2, 7.2], plinthDepth: 1.0, maxGrade: 0.8 });
  stdMaterials(A);
  const X = 7.5, Z = 3.5, T = 0.6, GF = 0.3, EAVE = 3.4, RIDGE = 7.6, CEIL = 3.0;
  const fr = { axis: "x", outer: Z, inner: Z - T }, bk = { axis: "x", outer: -Z, inner: -Z + T };
  const front = [door(A, { id: "salle_door", ...fr, c: 0.2, floor: GF, w: 1.0, h: 2.05, hingeSide: 1 }), door(A, { id: "cellier_door", ...fr, c: 4.9, floor: GF, w: 1.0, h: 2.0, hingeSide: -1 }),
    window(A, { id: "kitchen", ...fr, c: -4.6, sill: GF + 0.9, w: 1.0, h: 1.25 }), window(A, { id: "salle", ...fr, c: 1.8, sill: GF + 0.9, w: 1.0, h: 1.25 }), window(A, { id: "cellier", ...fr, c: 6.3, sill: GF + 1.0, w: 0.6, h: 0.8, shuttersOpen: false })];
  const back = [door(A, { id: "kitchen_rear_door", ...bk, c: -4.4, floor: GF, w: 0.95, h: 2.0, hingeSide: 1 }), window(A, { id: "kitchen_rear", ...bk, c: -6.0, sill: GF + 0.9, w: 0.8, h: 1.1 }), window(A, { id: "salle_rear", ...bk, c: 0.9, sill: GF + 0.9, w: 1.0, h: 1.25 })];
  const { XI, ZI } = shell(A, { X, Z, T, GF, EAVE, front, back });
  A.box([-XI, GF - 0.02, -ZI], [XI, GF, ZI], "terracotta");
  quoins(A, X, Z, GF - 0.02, EAVE);
  gableRoof(A, { X, Z, XI: XI + PT - 0.03, EAVE, RIDGE, ov: 0.4 });
  A.box([-XI, CEIL, -ZI], [XI, CEIL + 0.12, ZI], "plaster");   // ceiling (loft closed)
  for (let x = -XI + 0.5; x < XI - 0.2; x += 0.9) A.box([x - 0.07, CEIL - 0.16, -ZI], [x + 0.07, CEIL, ZI], "oak_z", { k: 0 });
  // partitions: kitchen | salle at x -2.2, salle | cellier at x 2.8; doorways near the rear
  for (const xp of [-2.2, 2.8]) {
    partition(A, { axis: "z", d0: xp - 0.1, d1: xp + 0.1, a0: -ZI, a1: ZI, y0: GF, y1: CEIL, holes: [{ a0: -2.2, a1: -1.2, y0: GF - 1, y1: GF + 2.05 }] });
    A.box([xp - 0.12, GF + 2.05, -2.3], [xp + 0.12, GF + 2.2, -1.1], "oak_z");
  }
  // kitchen hearth on the west gable, chimney stack
  A.box([-XI, GF, -1.0], [-XI + 0.55, GF + 1.05, -0.7], "stone_dressed"); A.box([-XI, GF, 0.7], [-XI + 0.55, GF + 1.05, 1.0], "stone_dressed");
  A.box([-XI, GF + 1.05, -1.15], [-XI + 0.65, GF + 1.3, 1.15], "oak_z");
  A.box([-XI, GF + 1.3, -0.95], [-XI + 0.45, CEIL, 0.95], "plaster_smoke");
  A.box([-XI, GF, -0.7], [-XI + 0.05, GF + 1.05, 0.7], "soot");
  A.box([-XI, GF, -1.0], [-XI + 0.55, GF + 1.05, 1.0], "stone", { v: 0 });
  A.box([-X - 0.05, RIDGE - 1.0, -0.6], [-X + 0.9, RIDGE + 1.1, 0.6], "stone_dressed");
  // kitchen
  table(A, "Kitchen table", -4.4, 0.4, 1.9, 0.85, GF);
  chair(A, "Kitchen chair", -4.9, 1.15, GF, "-z"); chair(A, "Kitchen chair", -3.9, 1.15, GF, "-z"); chair(A, "Kitchen chair", -4.4, -0.35, GF, "+z");
  dresser(A, "Dresser with plates", -3.9, -2.6, -ZI, 1, GF);
  bench(A, "Bench", -6.4, -5.2, 2.3, GF);
  // salle: box bed, armoire, table, a map and a field telephone left by the occupiers
  bed(A, "Box bed", -2.05, -0.6, 0.6, ZI - 0.02, GF, "x0");
  armoire(A, "Armoire normande", 0.9, 2.3, -ZI, 1, GF);
  table(A, "Table", 1.4, 0.5, 1.4, 0.8, GF);
  chair(A, "Chair", 1.4, -0.25, GF, "+z");
  A.beginFurniture("Map and field telephone");
  A.box([0.8, GF + 0.76, 0.25], [1.9, GF + 0.765, 0.8], "paper");
  A.box([1.75, GF + 0.76, 0.3], [2.0, GF + 0.92, 0.5], "crate_olive");
  A.endFurniture(false);
  // cellier
  barrel(A, "Cider barrel", 3.5, -2.2, GF); barrel(A, "Cider barrel", 4.25, -2.2, GF);
  shelfZ(A, "Shelves", -2.5, 0.5, XI, -1, GF, 1.8, 0.4, 4, "plank_z", null);
  sacks(A, "Sacks", [[3.6, GF, 1.8, 10], [3.3, GF + 0.28, 1.7, -8]]);
  A.beginFurniture("Butter churn"); A.box([5.6, GF, 0.6], [5.95, GF + 0.85, 0.95], "oak_y"); A.box([5.74, GF + 0.85, 0.74], [5.8, GF + 1.25, 0.8], "oak_y"); A.endFurniture();
  setNav(A, {
    effect: "block",
    rooms: [room("kitchen", [-XI, GF, -ZI], [-2.3, CEIL, ZI]), room("salle", [-2.1, GF, -ZI], [2.7, CEIL, ZI]), room("cellier", [2.9, GF, -ZI], [XI, CEIL, ZI])],
    doors: [{ a: "salle", b: "outside", p: [0.2, GF, Z - T / 2] }, { a: "cellier", b: "outside", p: [4.9, GF, Z - T / 2] }, { a: "kitchen", b: "outside", p: [-4.4, GF, -Z + T / 2] },
            { a: "kitchen", b: "salle", p: [-2.2, GF, -1.7] }, { a: "salle", b: "cellier", p: [2.8, GF, -1.7] }],
    entrances: [{ p: [0.2, 0, Z + 1.0], dir: [0, 0, -1], room: "salle" }, { p: [4.9, 0, Z + 1.0], dir: [0, 0, -1], room: "cellier" }, { p: [-4.4, 0, -Z - 1.0], dir: [0, 0, 1], room: "kitchen" }],
    corners: [...corners("kitchen", -XI, -2.3, -ZI, ZI, GF), ...corners("salle", -2.1, 2.7, -ZI, ZI, GF), ...corners("cellier", 2.9, XI, -ZI, ZI, GF)],
    firingPoints: [fire([-4.6, GF, ZI - 0.6], 0, "window"), fire([1.8, GF, ZI - 0.6], 0, "window"), fire([-6.0, GF, -ZI + 0.6], 180, "window"), fire([0.9, GF, -ZI + 0.6], 180, "window")],
    cover: [cover([X + 0.6, 0, 2.0], 270, "high", ["right"]), cover([-X - 0.6, 0, 2.0], 90, "high", ["left"]), cover([X + 0.6, 0, -2.0], 270, "high", ["left"]), cover([-X - 0.6, 0, -2.0], 90, "high", ["right"])],
  });
  setTour(A, { pos: [0.2, 0, 6.0], yaw: 0 }, [
    { goto: [0.2, 4.6], note: "approach the salle door" },
    { interact: "salle_door", aim: [0.35, 1.3, 2.93], expectOpen: true },
    { goto: [0.2, 2.0], expectY: GF, note: "into the salle" },
    { goto: [-0.6, -1.7] }, { goto: [-3.2, -1.7], expectY: GF, note: "through to the kitchen" },
    { goto: [-4.4, -1.2] }, { look: [-6.9, 1.0, 0], hold: 0.6 }, { shot: "mpu_farmhouse_kitchen" },
    { goto: [-3.2, -1.7] }, { goto: [-0.6, -1.7] }, { goto: [2.0, -1.7] }, { goto: [4.2, -1.0], expectY: GF, note: "into the cellier" },
    { goto: [2.0, -1.7] }, { goto: [0.2, 2.0] }, { goto: [0.2, 4.8], note: "leave by the salle door" },
  ]);
  return A;
}

// ============================================================================ stone barn
// 16 x 9 m, 5 m to the eaves: wide cart doors, a hay loft over the east third reached by a stair, ventilation slits
// usable as loopholes, a pitching door in the east gable wall at loft level.
export function stoneBarn() {
  const A = new Asset({ id: "vil_stone_barn", version: 1, name: "Stone barn", category: "Houses",
    description: "Norman stone barn, 16 x 9 m and 5 m to the eaves: wide double cart doors and a side door, ventilation slits used as loopholes, a hay loft over the east third reached by a timber stair, with an open pitching door in the gable looking out over the fields. Farm cart, hay, stalls.",
    floors: 1, enterable: true, footprint: [16.2, 9.2], plinthDepth: 1.0, maxGrade: 0.8 });
  stdMaterials(A);
  A.mat("hay", [0.48, 0.4, 0.2], 0.95, "fabric");
  const X = 8, Z = 4.5, T = 0.6, GF = 0.15, EAVE = 5.0, RIDGE = 9.0, LOFT = 3.3;
  const fr = { axis: "x", outer: Z, inner: Z - T }, bk = { axis: "x", outer: -Z, inner: -Z + T };
  const slit = (c, y0 = GF + 1.25) => ({ a0: c - 0.09, a1: c + 0.09, y0, y1: y0 + 1.0 });
  const front = [door(A, { id: "cart_doors", ...fr, c: -2.0, floor: GF, w: 3.2, h: 3.4, double: true, leafMat: "plank_y", label: "cart doors", step: false }),
    door(A, { id: "barn_side_door", ...fr, c: 5.2, floor: GF, w: 1.0, h: 2.1, hingeSide: 1, leafMat: "plank_y", step: false }), slit(1.8), slit(-6.2)];
  const back = [slit(-4.5), slit(-0.5), slit(3.5)];
  const pitch = { a0: -0.6, a1: 0.6, y0: LOFT + 0.05, y1: EAVE };
  const { XI, ZI } = shell(A, { X, Z, T, GF, EAVE, front, back, east: [pitch], west: [slit(-1.5), slit(1.5)] });
  A.box([-XI, GF - 0.02, -ZI], [XI, GF, ZI], "earth");
  quoins(A, X, Z, GF - 0.02, EAVE);
  gableRoof(A, { X, Z, XI: XI + PT - 0.03, EAVE, RIDGE, ov: 0.45 });
  for (const x of [-4.5, 0, 4.5]) { A.box([x - 0.12, EAVE - 0.3, -ZI], [x + 0.12, EAVE - 0.02, ZI], "oak_z"); A.obox([x, (EAVE + RIDGE) / 2 - 0.3, 0], [0.16, (RIDGE - EAVE) * 0.95, 0.16], [0, 0, 0], "oak_y", { k: 0 }); }
  // loft: x 3.0 .. XI at LOFT, on beams and two posts, a railing along its open edge
  A.box([3.0, LOFT - 0.2, -ZI], [XI, LOFT, ZI], "floor_x");
  for (let z = -ZI + 0.4; z < ZI; z += 1.1) A.box([3.0, LOFT - 0.42, z - 0.08], [XI, LOFT - 0.2, z + 0.08], "oak_x", { k: 0 });
  for (const z of [-2.2, 2.2]) A.box([2.95, GF, z - 0.12], [3.19, LOFT - 0.2, z + 0.12], "oak_y");
  stairs(A, { x0: 2.0, x1: 3.0, z0: -3.0, dir: 1, y0: GF, rise: LOFT - GF, n: 14, run: 3.6, openSide: -1 });
  railing(A, "z", 1.5, ZI, 3.03, LOFT);
  for (let k = 0; k < 6; k++) A.box([4.0 + (k % 3) * 1.05, LOFT, 1.6 + Math.floor(k / 3) * 0.6], [4.95 + (k % 3) * 1.05, LOFT + 0.5, 2.15 + Math.floor(k / 3) * 0.6], "hay");
  for (let k = 0; k < 4; k++) A.box([4.4 + (k % 2) * 1.05, LOFT + Math.floor(k / 2) * 0.5, -ZI + 0.1], [5.35 + (k % 2) * 1.05, LOFT + 0.5 + Math.floor(k / 2) * 0.5, -ZI + 0.65], "hay");
  // ground floor: farm cart, stalls along the west end, loose hay, tools
  A.beginFurniture("Farm cart");
  A.box([-5.9, 0.7, 1.0], [-3.9, 0.78, 2.4], "plank_x");
  for (const z of [1.0, 2.35]) A.box([-5.9, 0.78, z], [-3.9, 1.1, z + 0.05], "plank_x");
  for (const z of [0.9, 2.5]) for (const a of [0, 45]) A.obox([-4.6, 0.62, z], [1.2, 1.2, 0.08], [0, 0, a], "oak_x");
  A.obox([-2.9, 0.5, 1.7], [2.2, 0.08, 0.08], [0, 0, -12], "oak_x");
  A.endFurniture();
  for (const z of [-3.0, -1.2]) A.box([-XI, GF, z - 0.04], [-5.2, GF + 1.3, z + 0.04], "plank_x");
  crates(A, "Hay bales", [[-1.0, GF, -3.4, 0.95, 0.5, 0.5], [-1.0, GF + 0.5, -3.4, 0.95, 0.5, 0.5], [0.1, GF, -3.4, 0.95, 0.5, 0.5]], "hay");
  barrel(A, "Water barrel", 6.8, 3.4, GF);
  setNav(A, {
    effect: "block",
    rooms: [room("barn", [-XI, GF, -ZI], [XI, LOFT - 0.2, ZI]), room("loft", [3.0, LOFT, -ZI], [XI, EAVE + 1.0, ZI])],
    doors: [{ a: "barn", b: "outside", p: [-2.0, GF, Z - T / 2] }, { a: "barn", b: "outside", p: [5.2, GF, Z - T / 2] }, { a: "barn", b: "loft", p: [2.5, LOFT, 0.7] }],
    entrances: [{ p: [-2.0, 0, Z + 1.2], dir: [0, 0, -1], room: "barn" }, { p: [5.2, 0, Z + 1.0], dir: [0, 0, -1], room: "barn" }],
    corners: [...corners("barn", -XI, XI, -ZI, ZI, GF, 0.6), ...corners("loft", 3.0, XI, -ZI, ZI, LOFT, 0.5)],
    firingPoints: [fire([1.8, GF, ZI - 0.4], 0, "embrasure"), fire([-6.2, GF, ZI - 0.4], 0, "embrasure"), fire([-4.5, GF, -ZI + 0.4], 180, "embrasure"), fire([-0.5, GF, -ZI + 0.4], 180, "embrasure"),
                   fire([3.5, GF, -ZI + 0.4], 180, "embrasure"), fire([-XI + 0.4, GF, -1.5], 270, "embrasure"), fire([-XI + 0.4, GF, 1.5], 270, "embrasure"), unglazed([XI - 0.5, LOFT, 0], 90)],
    cover: [cover([X + 0.6, 0, 2.5], 270, "high", ["right"]), cover([-X - 0.6, 0, 2.5], 90, "high", ["left"]), cover([0.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-2.0, 0, 8.0], yaw: 0 }, [
    { goto: [-2.0, 6.0], note: "approach the cart doors" },
    { interact: "cart_doors", aim: [-1.5, 1.5, Z - T + 0.08], expectOpen: true },
    { goto: [-1.6, 2.0], expectY: GF, note: "into the barn" },
    { goto: [1.2, -3.55] }, { goto: [2.5, -3.55], note: "stair foot" },
    { goto: [2.5, 0.5], expectY: LOFT, note: "up the loft stair" }, { goto: [3.6, 0.8], expectY: LOFT, note: "onto the hay loft" },
    { goto: [XI - 0.6, 0.0], expectY: LOFT }, { look: [XI + 10, LOFT + 1.4, 0], hold: 0.6 }, { shot: "mpu_barn_pitching_door" },
    { goto: [3.6, 0.8] }, { goto: [2.5, 0.4] }, { goto: [2.5, -3.55], expectY: GF, note: "back down the stair" }, { goto: [1.2, -3.55] }, { goto: [-1.6, 2.0] }, { goto: [-2.0, 6.0], note: "leave the barn" },
  ]);
  return A;
}

// ============================================================================ church with steeple
// Tower (west, x -14.4..-8.0) with a four-flight stair to the belfry at 10.7 m (openings on three sides: the sniper
// position) under a slate spire to 25 m; nave (x -8..6, 8 m wide, 6 m to the eaves) with pews; choir (x 6..11).
export function church() {
  const A = new Asset({ id: "vil_church", version: 1, name: "Village church with steeple", category: "Landmarks",
    description: "Norman village church (Saint-Laurent): a 14 x 8 m nave with pews, a narrower choir with the altar, and a west tower 6.4 m square rising 15 m to the belfry under a slate spire (25 m). A four-flight stair climbs the tower to the belfry floor at 10.7 m, whose open louvred arches on three sides are the sniper position. South door into the nave, south door into the tower base.",
    floors: 1, enterable: true, footprint: [28.8, 9.4], plinthDepth: 1.0, maxGrade: 0.8 });
  stdMaterials(A);
  A.mat("bronze", [0.3, 0.22, 0.1], 0.45, "metal", { metallic: 0.9 });
  const R = rng(9101);
  const GF = 0.3, y0 = GF - 0.02, NT = 0.8;
  // ---- tower: outer x -14.4..-8.0, z -3.2..3.2; inner x -13.6..-8.8, z -2.4..2.4
  const TX0 = -14.4, TX1 = -8.0, TZ = 3.2, TT = 0.8, TXI0 = TX0 + TT, TXI1 = TX1 - TT, TZI = TZ - TT, TOP = 15.0, BF = 10.7;
  const tdoor = door(A, { id: "tower_door", axis: "x", outer: TZ, inner: TZI, c: -11.2, floor: GF, w: 1.1, h: 2.3, hingeSide: 1 });
  const belfry = (c) => ({ a0: c - 0.55, a1: c + 0.55, y0: BF + 0.9, y1: BF + 3.2 });
  const lancet = (c, y) => ({ a0: c - 0.15, a1: c + 0.15, y0: y, y1: y + 1.1 });
  const tArch = { a0: -0.7, a1: 0.7, y0: -10, y1: GF + 2.7 };
  const tp = (s) => pnM(s, y0, BF - 0.4, GF);
  A.box([TX0 - 0.05, -0.9, -TZ - 0.05], [TX1, y0, TZ + 0.05], "stone_plinth");
  A.box([TXI0, y0, -TZI], [TXI1, GF, TZI], "stone_dressed");
  wall(A, { axis: "x", outer: TZ, inner: TZI, aOut: [TX0, TX1], aIn: [TXI0, TXI1], y0, y1: TOP, holes: [tdoor, belfry(-11.2), lancet(-11.2, 6.2)], pt: PT, panel: tp("tower_s") });
  wall(A, { axis: "x", outer: -TZ, inner: -TZI, aOut: [TX0, TX1], aIn: [TXI0, TXI1], y0, y1: TOP, holes: [belfry(-11.2), lancet(-11.2, 4.0)], pt: PT, panel: tp("tower_n") });
  wall(A, { axis: "z", outer: TX0, inner: TXI0, aOut: [-TZI - PT, TZI + PT], aIn: [-TZI, TZI], y0, y1: TOP, holes: [belfry(0)], pt: PT, panel: tp("tower_w") });
  wall(A, { axis: "z", outer: TX1, inner: TXI1, aOut: [-TZI - PT, TZI + PT], aIn: [-TZI, TZI], y0, y1: TOP, holes: [tArch], pt: PT, panel: null });
  for (const s of [-1, 1]) for (const x of [TX0, TX1 - 0.6]) A.box([x - (x === TX0 ? 0.25 : 0), y0, s > 0 ? TZ - 0.6 : -TZ - 0.25], [x + 0.6 + (x === TX0 ? 0 : 0.25), 7.0 - (x === TX0 ? 0 : 1.5), s > 0 ? TZ + 0.25 : -TZ + 0.6], "stone_dressed");   // buttresses
  A.box([TX0 - 0.15, TOP - 0.3, -TZ - 0.15], [TX1 + 0.15, TOP, TZ + 0.15], "stone_dressed");   // cornice
  for (const [axis, d0, d1, c] of [["x", TZI, TZ, -11.2], ["x", -TZ, -TZI, -11.2], ["z", TX0, TXI0, 0]]) for (let k = 0; k < 2; k++) wbox(A, axis, c - 0.55, c + 0.55, BF + 2.75 + k * 0.22, BF + 2.82 + k * 0.22, d0, d1, "oak_x", { k: 0 });   // abat-son slats (top only)
  spire(A, (TX0 + TX1) / 2, 0, TZ + 0.1, TOP, 25.0, 9);
  // tower stair: W strip x -13.6..-12.6 climbs +Z, E strip x -9.8..-8.8 climbs -Z, landings at the ends
  const fl = [[TXI0, TXI0 + 1.0, -1.6, 1, GF], [TXI1 - 1.0, TXI1, 1.6, -1, GF + 2.6], [TXI0, TXI0 + 1.0, -1.6, 1, GF + 5.2], [TXI1 - 1.0, TXI1, 1.6, -1, GF + 7.8]];
  fl.forEach(([x0, x1, z0, dir, yb], i) => {
    const west = dir > 0;
    stairs(A, { x0, x1, z0, dir, y0: yb, rise: 2.6, n: 12, run: 3.2, openSide: west ? 1 : -1, core: !west });
    if (!west) {   // E flights: no spandrel (the ground passage runs under them); a vertical guard per tread toward the void
      for (let k = 0; k < 12; k++) { const za = z0 - k * (3.2 / 12), zb = za - 3.2 / 12, top = yb + (k + 1) * (2.6 / 12); A.box([x0 - 0.06, top - 0.05, zb], [x0, top + 1.1, za], "oak_x", { v: 0 }); }
      A.obox([x0 - 0.03, yb + 1.3 + 0.9, 0], [0.05, 0.07, Math.hypot(3.2, 2.6)], [Math.atan2(2.6, 3.2) * 180 / Math.PI, 0, 0], "oak_x", { k: 0 });
    }
    void i;
  });
  for (const [z0, z1, y] of [[1.6, TZI, GF + 2.6], [-TZI, -1.6, GF + 5.2], [1.6, TZI, GF + 7.8]]) {
    A.box([TXI0, y - 0.2, z0], [TXI1, y, z1], "floor_x");
    railing(A, "x", TXI0 + 1.0, TXI1 - 1.0, z0 > 0 ? z0 - 0.03 : z1 + 0.03, y);
  }
  // belfry floor with the stair hole over the last E flight, its guard, the bell frame and bell
  A.box([TXI0, BF - 0.2, -TZI], [TXI1 - 1.0, BF, TZI], "floor_x");
  A.box([TXI1 - 1.0, BF - 0.2, -TZI], [TXI1, BF, -1.6], "floor_x");
  railing(A, "z", -1.6, TZI, TXI1 - 1.03, BF);
  for (const x of [-12.2, -10.6]) for (const z of [-0.6, 1.0]) A.box([x - 0.1, BF, z - 0.1], [x + 0.1, BF + 3.6, z + 0.1], "oak_y");
  for (const z of [-0.6, 1.0]) A.box([-12.3, BF + 3.4, z - 0.12], [-10.5, BF + 3.6, z + 0.12], "oak_x", { k: 0 });
  A.box([-11.6, BF + 3.6, -0.7], [-11.2, BF + 3.75, 1.1], "oak_z", { k: 0 });
  for (const [w, y] of [[0.5, 3.35], [0.75, 3.0], [0.95, 2.6]]) A.box([-11.4 - w / 2, BF + y, 0.2 - w / 2], [-11.4 + w / 2, BF + y + 0.38, 0.2 + w / 2], "bronze", { k: 0 });
  A.box([TXI0, BF + 3.8, -TZI], [TXI1, BF + 4.0, TZI], "floor_x");   // belfry ceiling under the spire
  // ---- nave: outer x -8.0..6.0, z -4..4; inner x -7.2..5.2, z -3.2..3.2
  const NX0 = -8.0, NX1 = 6.0, NZ = 4.0, NXI0 = NX0 + NT, NXI1 = NX1 - NT, NZI = NZ - NT, NE = 6.0, NR = 10.0;
  const sdoor = door(A, { id: "south_door", axis: "x", outer: NZ, inner: NZI, c: -2.6, floor: GF, w: 1.6, h: 2.8, double: true, label: "church doors" });
  const cw = (id, outer, inner, c) => window(A, { id, axis: "x", outer, inner, c, sill: GF + 2.4, w: 0.9, h: 2.2, shutters: false });
  const nS = [sdoor, cw("nave_s1", NZ, NZI, -5.8), cw("nave_s2", NZ, NZI, 1.0), cw("nave_s3", NZ, NZI, 3.8)];
  const nN = [cw("nave_n1", -NZ, -NZI, -5.8), cw("nave_n2", -NZ, -NZI, -2.6), cw("nave_n3", -NZ, -NZI, 1.0), cw("nave_n4", -NZ, -NZI, 3.8)];
  const chancel = { a0: -1.8, a1: 1.8, y0: -10, y1: GF + 4.4 };
  A.box([NX0, -0.9, -NZ - 0.05], [NX1, y0, NZ + 0.05], "stone_plinth");
  A.box([NXI0, y0, -NZI], [NXI1, GF, NZI], "stone_dressed");
  const np = (s) => pnM(s, y0, NE, GF);
  wall(A, { axis: "x", outer: NZ, inner: NZI, aOut: [NX0, NX1], aIn: [NXI0, NXI1], y0, y1: NE, holes: nS, pt: PT, panel: np("nave_s") });
  wall(A, { axis: "x", outer: -NZ, inner: -NZI, aOut: [NX0, NX1], aIn: [NXI0, NXI1], y0, y1: NE, holes: nN, pt: PT, panel: np("nave_n") });
  wall(A, { axis: "z", outer: NX0, inner: NXI0, aOut: [-NZI - PT, NZI + PT], aIn: [-NZI, NZI], y0, y1: NE, holes: [tArch], pt: PT, panel: null });
  wall(A, { axis: "z", outer: NX1, inner: NXI1, aOut: [-NZI - PT, NZI + PT], aIn: [-NZI, NZI], y0, y1: NE, holes: [chancel], pt: PT, panel: null });
  for (const s of [-1, 1]) for (const x of [-4.3, -0.8, 2.6]) A.box([x - 0.35, y0, s > 0 ? NZ : -NZ - 0.55], [x + 0.35, NE - 1.2, s > 0 ? NZ + 0.55 : -NZ], "stone_dressed");   // buttresses
  roofX(A, NX0, NX1, NZ, NE, NR, { gables: [false, true], wallT: NT });
  A.prism(0, NX0, NX0 + NT, [[-NZ, NE], [NZ, NE], [0, NR]], "stone");
  // ---- choir: outer x 6..11, z -3..3; inner x 6..10.2, z -2.2..2.2
  const CX1 = 11.0, CZ = 3.0, CXI1 = CX1 - NT, CZI = CZ - NT, CE = 5.0, CR = 7.8;
  A.box([NX1, -0.9, -CZ - 0.05], [CX1 + 0.05, y0, CZ + 0.05], "stone_plinth");
  A.box([NX1 - 0.02, y0, -CZI], [CXI1, GF + 0.15, CZI], "stone_dressed");   // choir floor one step up
  const cp = (s) => pnM(s, y0, CE, GF);
  wall(A, { axis: "x", outer: CZ, inner: CZI, aOut: [NX1, CX1], aIn: [NX1, CXI1], y0, y1: CE, holes: [cw("choir_s", CZ, CZI, 8.4)], pt: PT, panel: cp("choir_s") });
  wall(A, { axis: "x", outer: -CZ, inner: -CZI, aOut: [NX1, CX1], aIn: [NX1, CXI1], y0, y1: CE, holes: [cw("choir_n", -CZ, -CZI, 8.4)], pt: PT, panel: cp("choir_n") });
  wall(A, { axis: "z", outer: CX1, inner: CXI1, aOut: [-CZI - PT, CZI + PT], aIn: [-CZI, CZI], y0, y1: CE, holes: [window(A, { id: "choir_e", axis: "z", outer: CX1, inner: CXI1, c: 0, sill: GF + 2.0, w: 1.0, h: 2.4, shutters: false })], pt: PT, panel: cp("choir_e") });
  roofX(A, NX1 - 0.3, CX1, CZ, CE, CR, { gables: [false, true], wallT: NT });
  // ---- furnishings
  pews(A, -6.4, 3.6, -3.0, -0.6, GF); pews(A, -6.4, 3.6, 0.6, 3.0, GF);
  A.beginFurniture("Altar"); A.box([9.2, GF + 0.15, -1.1], [9.9, GF + 1.15, 1.1], "stone_dressed"); A.box([9.15, GF + 1.15, -1.2], [9.95, GF + 1.22, 1.2], "linen"); A.endFurniture();
  A.beginFurniture("Altar candlesticks"); for (const z of [-0.8, 0.8]) A.box([9.5, GF + 1.22, z - 0.03], [9.56, GF + 1.6, z + 0.03], "brass"); A.endFurniture(false);
  A.beginFurniture("Pulpit"); A.box([4.3, GF, 2.2], [5.0, GF + 1.3, 2.95], "oak_y"); A.endFurniture();
  A.beginFurniture("Font"); A.box([-6.6, GF, 2.0], [-6.0, GF + 0.95, 2.6], "stone_dressed"); A.endFurniture();
  table(A, "Vestry table", 7.0, -1.7, 1.0, 0.6, GF + 0.15);
  setNav(A, {
    effect: "block",
    rooms: [room("tower", [TXI0, GF, -TZI], [TXI1, BF - 0.2, TZI]), room("belfry", [TXI0, BF, -TZI], [TXI1, BF + 3.8, TZI]),
            room("nave", [NXI0, GF, -NZI], [NXI1, NE, NZI]), room("choir", [NX1, GF + 0.15, -CZI], [CXI1, CE, CZI])],
    doors: [{ a: "nave", b: "outside", p: [-2.6, GF, NZ - NT / 2] }, { a: "tower", b: "outside", p: [-11.2, GF, TZ - TT / 2] }, { a: "tower", b: "nave", p: [-8.0, GF, 0] },
            { a: "nave", b: "choir", p: [5.6, GF, 0] }, { a: "tower", b: "belfry", p: [-9.3, BF, -2.0] }],
    entrances: [{ p: [-2.6, 0, NZ + 1.2], dir: [0, 0, -1], room: "nave" }, { p: [-11.2, 0, TZ + 1.2], dir: [0, 0, -1], room: "tower" }],
    corners: [...corners("tower", TXI0, TXI1, -TZI, TZI, GF, 0.4), ...corners("belfry", TXI0, TXI1, -TZI, TZI, BF, 0.4), ...corners("nave", NXI0, NXI1, -NZI, NZI, GF, 0.6), ...corners("choir", NX1, CXI1, -CZI, CZI, GF + 0.15, 0.5)],
    firingPoints: [unglazed([-11.2, BF, TZI - 0.35], 0), unglazed([-11.2, BF, -TZI + 0.35], 180), unglazed([TXI0 + 0.35, BF, 0], 270)],
    cover: [cover([-4.3, 0, NZ + 1.2], 180, "high", ["left", "right"]), cover([2.6, 0, -NZ - 1.2], 0, "high", ["left", "right"]), cover([TX0 - 0.6, 0, 0], 90, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-11.2, 0, 7.0], yaw: 0 }, [
    { goto: [-11.2, 4.6], note: "approach the tower door" },
    { interact: "tower_door", aim: [-11.0, 1.3, TZI + 0.05], expectOpen: true },
    { goto: [-11.2, 1.0], expectY: GF, note: "into the tower base" },
    { goto: [-11.2, -2.08] }, { goto: [-13.1, -2.08], note: "foot of the first flight" },
    { goto: [-13.1, 2.0], expectY: GF + 2.6, note: "first landing" }, { goto: [-9.3, 2.0] },
    { goto: [-9.3, -2.08], expectY: GF + 5.2, note: "second landing" }, { goto: [-13.1, -2.08] },
    { goto: [-13.1, 2.0], expectY: GF + 7.8, note: "third landing" }, { goto: [-9.3, 2.0] },
    { goto: [-9.3, -2.0], expectY: BF, note: "belfry floor" }, { goto: [-11.2, -1.95], expectY: BF, note: "at the north arch" },
    { look: [-11.2, BF - 6, -40], hold: 0.8 }, { shot: "mpu_church_belfry_view" },
    { goto: [-9.3, -2.0] }, { goto: [-9.3, 2.0], expectY: GF + 7.8, note: "down" }, { goto: [-13.1, 2.0] }, { goto: [-13.1, -2.08], expectY: GF + 5.2 },
    { goto: [-9.3, -2.08] }, { goto: [-9.3, 2.0], expectY: GF + 2.6 }, { goto: [-13.1, 2.0] }, { goto: [-13.1, -2.08], expectY: GF, note: "back at the tower base" },
    { goto: [-11.2, -0.2] }, { goto: [-7.6, 0], note: "through the tower arch" }, { goto: [-2.6, 0.0], expectY: GF, note: "nave" },
    { look: [9.5, 1.5, 0], hold: 0.6 }, { shot: "mpu_church_nave" },
    { goto: [5.6, 0.0] }, { goto: [8.2, 0.0], expectY: GF + 0.15, note: "choir" },
  ]);
  void R;
  return A;
}

// ============================================================================ ruined church (roofless)
export function churchRuined() {
  const A = new Asset({ id: "vil_church_ruined", version: 1, name: "Ruined church (roofless)", category: "Landmarks",
    description: "Roofless ruin of a small church as on the Hill 122 summit: a 14 x 8 m nave with broken wall tops 1.5-6 m high, a breach in the south wall, the west doorway, the choir walls standing to 4 m with an empty east window, rubble and charred roof timbers inside. Enterable from three sides.",
    floors: 1, enterable: true, footprint: [20.4, 9.6], plinthDepth: 1.0, maxGrade: 1.0 });
  stdMaterials(A);
  const R = rng(9202), GF = 0.15, y0 = GF - 0.02, T = 0.8, X0 = -7.0, X1 = 7.0, Z = 4.0, XI0 = X0 + T, XI1 = X1 - T, ZI = Z - T;
  A.box([X0, -0.9, -Z], [X1, y0, Z], "stone_plinth");
  A.box([XI0, y0, -ZI], [XI1, GF, ZI], "earth");
  const wdoor = { a0: -0.75, a1: 0.75, y0: -10, y1: GF + 2.8 }, breach = { a0: 0.4, a1: 2.6, y0: -10, y1: 20 };
  const sWin = (c) => ({ a0: c - 0.45, a1: c + 0.45, y0: GF + 2.4, y1: GF + 4.4 });
  raggedWall(A, R, { axis: "x", outer: Z, inner: ZI, a0: X0, a1: X1, y0, heights: [5.2, 4.1, 3.0, 1.2, 2.6, 4.8, 5.9], holes: [breach, sWin(-4.4), sWin(4.4)], side: "s" });
  raggedWall(A, R, { axis: "x", outer: -Z, inner: -ZI, a0: X0, a1: X1, y0, heights: [5.8, 5.0, 2.2, 1.5, 1.6, 3.5, 5.0], holes: [sWin(-4.4), sWin(4.4)], side: "n" });
  raggedWall(A, R, { axis: "z", outer: X0, inner: XI0, a0: -ZI - PT, a1: ZI + PT, y0, heights: [6.5, 8.2, 6.0], holes: [wdoor], side: "w" });
  raggedWall(A, R, { axis: "z", outer: X1, inner: XI1, a0: -ZI - PT, a1: ZI + PT, y0, heights: [5.0, 6.2, 4.6], holes: [{ a0: -1.6, a1: 1.6, y0: -10, y1: GF + 4.0 }], side: "e" });
  // choir: x 7..10.2, z -2.6..2.6, walls to ~4 m, east window
  const CX1 = 10.2, CZ = 2.6, CZI = CZ - T;
  A.box([X1, -0.9, -CZ], [CX1, y0, CZ], "stone_plinth");
  A.box([X1, y0, -CZI], [CX1 - T, GF, CZI], "earth");
  raggedWall(A, R, { axis: "x", outer: CZ, inner: CZI, a0: X1, a1: CX1, y0, heights: [4.2, 3.4], side: "cs" });
  raggedWall(A, R, { axis: "x", outer: -CZ, inner: -CZI, a0: X1, a1: CX1, y0, heights: [3.6, 4.4], side: "cn" });
  raggedWall(A, R, { axis: "z", outer: CX1, inner: CX1 - T, a0: -CZI - PT, a1: CZI + PT, y0, heights: [3.8, 4.6], holes: [{ a0: -0.45, a1: 0.45, y0: GF + 1.6, y1: GF + 3.6 }], side: "ce" });
  // rubble heaps and charred timbers
  for (let k = 0; k < 40; k++) {
    const x = XI0 + 0.5 + R() * (XI1 - XI0 - 1), z = (R() < 0.5 ? -1 : 1) * (ZI - 0.4 - R() * 1.0), s = 0.25 + R() * 0.5;
    A.rbox([x - s, GF - 0.1, z - s * 0.8], [x + s, GF + s * 0.5, z + s * 0.8], s * 0.25, R() < 0.3 ? "stone_dressed" : "stone", { k: 0 });
  }
  for (const [x, z, yaw] of [[-3.0, 1.6, 15], [1.2, -1.8, -25], [4.0, 0.8, 70]]) A.obox([x, GF + 0.12, z], [4.2, 0.24, 0.26], [0, yaw, 6], "soot", { k: 0 });
  A.rbox([-1.0, GF - 0.1, -2.9], [0.8, GF + 1.0, -1.9], 0.2, "stone");   // a solid heap (cover)
  A.beginFurniture("Broken altar"); A.box([8.4, GF, -0.9], [9.1, GF + 0.9, 0.9], "stone_dressed"); A.endFurniture();
  setNav(A, {
    effect: "block",
    rooms: [room("nave", [XI0, GF, -ZI], [XI1, GF + 6, ZI]), room("choir", [X1, GF, -CZI], [CX1 - T, GF + 4, CZI])],
    doors: [{ a: "nave", b: "outside", p: [X0 + T / 2, GF, 0] }, { a: "nave", b: "outside", p: [1.5, GF, Z - T / 2] }, { a: "nave", b: "choir", p: [X1 - T / 2, GF, 0] }],
    entrances: [{ p: [X0 - 1.2, 0, 0], dir: [1, 0, 0], room: "nave" }, { p: [1.5, 0, Z + 1.2], dir: [0, 0, -1], room: "nave" }],
    corners: [...corners("nave", XI0, XI1, -ZI, ZI, GF, 0.6), ...corners("choir", X1, CX1 - T, -CZI, CZI, GF, 0.5)],
    firingPoints: [fire([-0.2, GF, ZI - 0.3], 0, "firestep"), fire([0.0, GF, -ZI + 0.3], 180, "firestep")],
    cover: [cover([-0.1, GF, -1.5], 180, "low", ["over"]), cover([-4.4, 0, Z + 0.6], 180, "high", ["left", "right"]), cover([4.4, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [X0 - 4.0, 0, 0.2], yaw: 270 }, [
    { goto: [X0 - 1.4, 0.0], note: "west doorway" }, { goto: [-4.0, 0.2], expectY: GF, note: "into the roofless nave" },
    { look: [4, 3, 0], hold: 0.6 }, { shot: "mpu_church_ruin_nave" },
    { goto: [5.0, 0.0] }, { goto: [8.0, 0.4], expectY: GF, note: "into the choir" },
    { goto: [3.0, 1.0] }, { goto: [1.5, 2.6] }, { goto: [1.5, 5.6], note: "out through the south breach" },
  ]);
  return A;
}

// ============================================================================ ruined chateau keep
// Le Plessis-Lastelle: an 11 m square donjon with 2 m walls standing 11-15 m, a stone stair up the inner west wall
// to a part-collapsed first floor with wide window embrasures (the MG in the keep), and a breached stretch of curtain
// wall to the east.
export function chateauKeep() {
  const A = new Asset({ id: "vil_chateau_keep", version: 1, name: "Ruined chateau keep", category: "Landmarks",
    description: "Ruined donjon of the chateau of Le Plessis-Lastelle: 11 m square with 2 m walls standing 11-15 m, an entrance arch and two ground-floor arrow slits, a stone stair up the inner west wall to a part-collapsed timber first floor at 4.8 m with wide window embrasures on three sides (MG position), and a breached stretch of curtain wall to the east. Rubble at the foot.",
    floors: 2, enterable: true, footprint: [24.0, 11.4], plinthDepth: 1.0, maxGrade: 1.0 });
  stdMaterials(A);
  const R = rng(9303), X = 5.5, T = 2.0, XI = X - T, GF = 0.3, y0 = GF - 0.02, FF = 4.8;
  A.box([-X - 0.3, -0.9, -X - 0.3], [X + 0.3, y0, X + 0.3], "stone_plinth");
  A.rbox([-X - 0.4, -0.9, -X - 0.4], [X + 0.4, 0.9, X + 0.4], 0.3, "stone", { k: 0 });   // battered base (visual)
  A.box([-XI, y0, -XI], [XI, GF, XI], "stone_dressed");
  const ent = { a0: 0.8, a1: 2.2, y0: -10, y1: GF + 2.7 };
  const win = (c) => ({ a0: c - 0.5, a1: c + 0.5, y0: FF + 0.85, y1: FF + 2.3 });
  const arrow = (c) => ({ a0: c - 0.12, a1: c + 0.12, y0: GF + 1.2, y1: GF + 2.4 });
  raggedWall(A, R, { axis: "x", outer: X, inner: XI, a0: -X, a1: X, y0, heights: [14.5, 12.0, 13.2, 15.0], holes: [ent, win(-1.2), arrow(-2.4), arrow(3.0)], side: "s" });
  raggedWall(A, R, { axis: "x", outer: -X, inner: -XI, a0: -X, a1: X, y0, heights: [15.2, 14.0, 11.0, 12.8], holes: [], side: "n" });
  raggedWall(A, R, { axis: "z", outer: X, inner: XI, a0: -XI - PT, a1: XI + PT, y0, heights: [13.0, 11.4, 14.2], holes: [win(1.6)], side: "e" });
  raggedWall(A, R, { axis: "z", outer: -X, inner: -XI, a0: -XI - PT, a1: XI + PT, y0, heights: [14.8, 13.4, 12.2], holes: [win(2.2)], side: "w" });
  // stair up the inner west wall: x -3.5..-2.5, from z -2.4 toward +Z to 2.2, then the first floor
  stairs(A, { x0: -XI, x1: -XI + 1.0, z0: -2.4, dir: 1, y0: GF, rise: FF - GF, n: 20, run: 4.6, openSide: 1 });
  // first floor: z -0.8..3.5 except the stair hole; the southern part has fallen in (broken joists hang down)
  A.box([-XI + 1.0, FF - 0.2, -0.8], [XI, FF, XI], "floor_x");
  A.box([-XI, FF - 0.2, 2.2], [-XI + 1.0, FF, XI], "floor_x");
  for (let x = -XI + 1.3; x < XI; x += 0.8) { A.box([x - 0.08, FF - 0.42, -0.8], [x + 0.08, FF - 0.2, XI], "oak_z", { k: 0 }); if (R() < 0.6) A.obox([x, FF - 0.9, -1.6], [0.16, 0.22, 1.8], [-35, 0, 0], "soot", { k: 0 }); }
  for (const x of [-1.0, 1.8]) A.box([x - 0.15, GF, -0.95], [x + 0.15, FF - 0.42, -0.65], "oak_y");   // props under the broken edge
  // rubble inside and at the foot, the curtain wall with its breach
  for (let k = 0; k < 30; k++) { const x = (R() * 2 - 1) * (XI - 0.6), z = -XI + 0.4 + R() * 2.0, s = 0.25 + R() * 0.45; A.rbox([x - s, GF - 0.1, z - s * 0.7], [x + s, GF + s * 0.45, z + s * 0.7], s * 0.25, "stone", { k: 0 }); }
  const cw = (a0, a1, h) => wall(A, { axis: "x", outer: 1.0, inner: -1.0, aOut: [a0, a1], aIn: [a0, a1], y0, y1: h, holes: [], pt: PT, panel: pnM("curtain", y0, h, GF) });
  A.box([X, -0.9, -1.0], [12.0, y0, 1.0], "stone_plinth");
  cw(X, 8.0, 6.0); cw(9.6, 12.0, 5.2);
  for (let k = 0; k < 8; k++) { const x = 8.1 + R() * 1.4, z = (R() * 2 - 1) * 1.6, s = 0.3 + R() * 0.4; A.rbox([x - s, -0.1, z - s], [x + s, s * 0.6, z + s], s * 0.25, "stone", { k: 0 }); }
  const fpw = (c, side) => side === "s" ? unglazed([c, FF, XI - 0.4], 0) : side === "e" ? unglazed([XI - 0.4, FF, c], 90) : unglazed([-XI + 0.4, FF, c], 270);
  setNav(A, {
    effect: "block",
    rooms: [room("hall", [-XI, GF, -XI], [XI, FF - 0.42, XI]), room("upper", [-XI, FF, -0.8], [XI, FF + 3.5, XI])],
    doors: [{ a: "hall", b: "outside", p: [1.5, GF, X - T / 2] }, { a: "hall", b: "upper", p: [-3.0, FF, 2.4] }],
    entrances: [{ p: [1.5, 0, X + 1.2], dir: [0, 0, -1], room: "hall" }],
    corners: [...corners("hall", -XI, XI, -XI, XI, GF, 0.5), ...corners("upper", -XI, XI, -0.8, XI, FF, 0.5)],
    firingPoints: [fpw(-1.2, "s"), fpw(1.6, "e"), fpw(2.2, "w"), fire([-2.4, GF, XI - 0.4], 0, "embrasure"), fire([3.0, GF, XI - 0.4], 0, "embrasure")],
    cover: [cover([-1.5, 0, X + 0.7], 180, "high", ["left", "right"]), cover([10.8, 0, -1.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [1.5, 0, X + 4.0], yaw: 0 }, [
    { goto: [1.5, X + 1.0], note: "approach the entrance arch" }, { goto: [1.5, 2.6], expectY: GF, note: "into the hall" },
    { goto: [-1.6, -3.15] }, { goto: [-3.0, -3.15], note: "stair foot" },
    { goto: [-3.0, 2.8], expectY: FF, note: "up to the first floor" }, { goto: [-1.2, 3.0], expectY: FF, note: "at the south window" },
    { look: [-1.2, FF + 1.5, 30], hold: 0.6 }, { shot: "mpu_keep_window" },
    { look: [0, FF - 2, -2], hold: 0.5 }, { shot: "mpu_keep_interior" },
  ]);
  return A;
}

export function villageAssets() {
  return [normanFarmhouse(), stoneBarn(), church(), churchRuined(), chateauKeep()];
}
void bench; void chest; void shelfX; void stool;
