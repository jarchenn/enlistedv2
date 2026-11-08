// Village buildings for the MP & Creative update (SPEC 3.3 / 3.4 / 3.6, docs/assets-mpu.md): a Norman longere
// farmhouse, a stone barn with a hay loft, the village church with its steeple (sniper position in the belfry), a
// roofless ruined church and the ruined chateau keep of Le Plessis-Lastelle. All enterable, built with the house kit
// (two-layer rubble / plaster walls with real openings, destructible wall bays, hinged doors and casements, stairs
// with a smooth collision ramp) and furnished like the existing houses.
import { Asset, wbox } from "./kit.mjs";
import { stdMaterials, wall, partition, window, door, stairs, railing, quoins, gableRoof, table, chair, bench, bed, armoire, dresser, chest } from "./parts.mjs";
import { shelfX, shelfZ, crates, barrel, sacks, stool } from "./furniture2.mjs";
import { mats, rng, fire, cover, room, corners, setNav, setTour, r3 } from "./catalog_atlantic.mjs";

export const PT = 0.03;
export const pnM = (side, y0, y1, GF) => ({ side, family: "masonry", bands: [[y0, y1, 0, GF]] });
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
export function roofX(A, x0, x1, Z, EAVE, RIDGE, { ov = 0.35, gov = 0.28, t = 0.14, gables = [true, true], wallT = 0.6, gableMat = "stone" } = {}) {
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
// a wall whose top is broken: equal bays along a0..a1, each with its own height, plus loose stones on the tops.
// inA: the inner (plaster) span, inset like shell() does. With the plaster running the full outer length, its end faces
// lay coplanar with the crossing wall's stone face at every corner and shimmered (docs/rendering.md).
function raggedWall(A, R, { axis, outer, inner, a0, a1, inA = [a0, a1], y0, heights, holes = [], side }) {
  const n = heights.length, w = (a1 - a0) / n;
  for (let i = 0; i < n; i++) {
    const b0 = a0 + i * w, b1 = b0 + w, h = heights[i];
    if (h <= y0 + 0.05) continue;
    const hs = holes.filter((q) => q.a1 > b0 && q.a0 < b1).map((q) => ({ ...q, a0: Math.max(q.a0, b0), a1: Math.min(q.a1, b1) }));
    const aIn = [Math.max(b0, inA[0]), Math.min(b1, inA[1])];
    wall(A, { axis, outer, inner, aOut: [b0, b1], aIn, y0, y1: h, holes: hs, pt: PT, panel: h - y0 > 1.5 && w > 1.2 ? pnM(side + "_" + i, y0, h, y0 + 0.02) : null });
    for (let k = 0; k < 2; k++) { const a = b0 + 0.2 + R() * (w - 0.6), d0 = Math.min(outer, inner), d1 = Math.max(outer, inner); wbox(A, axis, a, a + 0.25 + R() * 0.35, h, h + 0.12 + R() * 0.25, d0 + R() * 0.2, d1 - R() * 0.2, "stone", { k: 0 }); }
  }
}
// pews facing +X (toward the altar) in a block z0..z1, rows from x0 to x1
export function pews(A, x0, x1, z0, z1, GF) {
  for (let x = x0; x <= x1 + 1e-6; x += 1.0) {
    A.beginFurniture("Pew");
    A.box([x - 0.2, GF + 0.42, z0], [x + 0.2, GF + 0.47, z1], "oak_z");
    A.box([x - 0.24, GF + 0.47, z0], [x - 0.19, GF + 0.95, z1], "oak_z");
    for (const z of [z0, z1 - 0.05]) A.box([x - 0.24, GF, z], [x + 0.2, GF + 0.95, z + 0.05], "oak_y");
    A.endFurniture();
  }
}
export const unglazed = (p, yaw) => ({ ...fire(p, yaw, "window"), unglazed: true });

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
  barrel(A, "Cider barrel", 3.7, -2.55, GF); barrel(A, "Cider barrel", 4.45, -2.55, GF);
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
    { goto: [-3.2, -1.7] }, { goto: [-0.6, -1.7] }, { goto: [2.0, -1.7] }, { goto: [3.6, -1.7] }, { goto: [4.2, -1.0], expectY: GF, note: "into the cellier" },
    { goto: [3.6, -1.7] }, { goto: [2.0, -1.7] }, { goto: [0.2, 2.0] }, { goto: [0.2, 4.8], note: "leave by the salle door" },
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
  // tie beams and king posts; none over the loft (it would sit at head height)
  for (const x of [-4.5, 0]) { A.box([x - 0.12, EAVE - 0.3, -ZI], [x + 0.12, EAVE - 0.02, ZI], "oak_z"); A.obox([x, (EAVE + RIDGE) / 2 - 0.3, 0], [0.16, (RIDGE - EAVE) * 0.95, 0.16], [0, 0, 0], "oak_y", { k: 0 }); }
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
  raggedWall(A, R, { axis: "x", outer: Z, inner: ZI, a0: X0, a1: X1, inA: [XI0, XI1], y0, heights: [5.2, 4.1, 3.0, 1.2, 2.6, 4.8, 5.9], holes: [breach, sWin(-4.4), sWin(4.4)], side: "s" });
  raggedWall(A, R, { axis: "x", outer: -Z, inner: -ZI, a0: X0, a1: X1, inA: [XI0, XI1], y0, heights: [5.8, 5.0, 2.2, 1.5, 1.6, 3.5, 5.0], holes: [sWin(-4.4), sWin(4.4)], side: "n" });
  raggedWall(A, R, { axis: "z", outer: X0, inner: XI0, a0: -ZI - PT, a1: ZI + PT, inA: [-ZI, ZI], y0, heights: [6.5, 8.2, 6.0], holes: [wdoor], side: "w" });
  raggedWall(A, R, { axis: "z", outer: X1, inner: XI1, a0: -ZI - PT, a1: ZI + PT, inA: [-ZI, ZI], y0, heights: [5.0, 6.2, 4.6], holes: [{ a0: -1.6, a1: 1.6, y0: -10, y1: GF + 4.0 }], side: "e" });
  // choir: x 7..10.2, z -2.6..2.6, walls to ~4 m, east window
  const CX1 = 10.2, CZ = 2.6, CZI = CZ - T;
  A.box([X1, -0.9, -CZ], [CX1, y0, CZ], "stone_plinth");
  A.box([X1, y0, -CZI], [CX1 - T, GF, CZI], "earth");
  raggedWall(A, R, { axis: "x", outer: CZ, inner: CZI, a0: X1, a1: CX1, inA: [X1, CX1 - T], y0, heights: [4.2, 3.4], side: "cs" });
  raggedWall(A, R, { axis: "x", outer: -CZ, inner: -CZI, a0: X1, a1: CX1, inA: [X1, CX1 - T], y0, heights: [3.6, 4.4], side: "cn" });
  raggedWall(A, R, { axis: "z", outer: CX1, inner: CX1 - T, a0: -CZI - PT, a1: CZI + PT, inA: [-CZI, CZI], y0, heights: [3.8, 4.6], holes: [{ a0: -0.45, a1: 0.45, y0: GF + 1.6, y1: GF + 3.6 }], side: "ce" });
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
  raggedWall(A, R, { axis: "x", outer: X, inner: XI, a0: -X, a1: X, inA: [-XI, XI], y0, heights: [14.5, 12.0, 13.2, 15.0], holes: [ent, win(-1.2), arrow(-2.4), arrow(3.0)], side: "s" });
  raggedWall(A, R, { axis: "x", outer: -X, inner: -XI, a0: -X, a1: X, inA: [-XI, XI], y0, heights: [15.2, 14.0, 11.0, 12.8], holes: [], side: "n" });
  raggedWall(A, R, { axis: "z", outer: X, inner: XI, a0: -XI - PT, a1: XI + PT, inA: [-XI, XI], y0, heights: [13.0, 11.4, 14.2], holes: [win(1.6)], side: "e" });
  raggedWall(A, R, { axis: "z", outer: -X, inner: -XI, a0: -XI - PT, a1: XI + PT, inA: [-XI, XI], y0, heights: [14.8, 13.4, 12.2], holes: [win(2.2)], side: "w" });
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
  return [churchRuined(), chateauKeep()];   // the farmhouse, barn and church are B2's (catalog_houses2.mjs)
}
void bench; void chest; void shelfX; void stool;
