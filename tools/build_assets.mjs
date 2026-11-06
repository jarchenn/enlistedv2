// Generates content/assets/*.json and content/templates/*.json.
// Run: node tools/build_assets.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Asset, wbox } from "./kit.mjs";
import { workshop, dairy, storehouse, office } from "./catalog_small.mjs";
import { schoolhouse, inn, warehouse, municipal } from "./catalog_large.mjs";
import { pak40, m2hb, crates } from "./catalog_ordnance.mjs";
import { weapons } from "./catalog_weapons.mjs";
import { mgCasemate, observationPost, fieldShelter, commandShelter, largeBunker, pakCasemate, mg42Mount } from "./catalog_bunkers.mjs";
import { stdMaterials, wall, partition, window, door, stairs, railing, table, chair, bench, bed, armoire, dresser, chest, gableRoof, quoins } from "./parts.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outAssets = path.join(root, "content", "assets");
const outTemplates = path.join(root, "content", "templates");
fs.mkdirSync(outAssets, { recursive: true });
fs.mkdirSync(outTemplates, { recursive: true });

// =============================================================================
// Two-storey Norman stone farmhouse (corps de ferme), 12 x 9 m.
// Ground floor: kitchen with hearth (west), hall/front room (east).
// Upper floor: two bedrooms and a landing.
// Circulation: a primary stair in the hall and a separate rear service stair
// against the east gable, giving two independent routes between floors.
// =============================================================================
function farmhouse() {
  const A = new Asset({
    id: "house_farmhouse_2story", version: 1, name: "Two-Story Farmhouse", category: "Houses",
    description: "Rubble-limestone Norman farmhouse with slate roof. Kitchen with open hearth, hall, two bedrooms. Front stair in the hall plus a rear service stair: two independent routes between floors. Shuttered casement windows on both floors.",
    floors: 2, enterable: true, footprint: [12.2, 9.2], plinthDepth: 1.2, maxGrade: 1.0,
  });
  stdMaterials(A);
  const X = 6, Z = 4.5, T = 0.5, XI = X - T, ZI = Z - T;
  const GF = 0.3, FF = 3.3, EAVE = 6.3, RIDGE = 8.8, CEIL = 6.1;
  const y0 = GF - 0.02;

  // ---- plinth / socle (also the foundation skirt for uneven ground)
  A.box([-X - 0.05, -1.2, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "terracotta");

  // ---- openings
  const front = { axis: "x", outer: Z, inner: ZI };
  const back = { axis: "x", outer: -Z, inner: -ZI };
  const holesFront = [], holesBack = [];
  holesFront.push(door(A, { id: "front_door", ...front, c: 1.5, floor: GF, w: 1.0, h: 2.1, hingeSide: +1 }));
  holesFront.push(window(A, { id: "kitchen", ...front, c: -3.0, sill: GF + 0.9, w: 1.0, h: 1.3, shuttersOpen: false }));
  holesFront.push(window(A, { id: "parlour", ...front, c: 4.0, sill: GF + 0.9, w: 1.0, h: 1.3 }));
  holesFront.push(window(A, { id: "bedroom_w_front", ...front, c: -3.0, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesFront.push(window(A, { id: "bedroom_e_front", ...front, c: 3.6, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesBack.push(door(A, { id: "rear_door", ...back, c: 3.2, floor: GF, w: 1.0, h: 2.05, hingeSide: -1 }));
  holesBack.push(window(A, { id: "kitchen_rear", ...back, c: -4.2, sill: GF + 0.9, w: 0.9, h: 1.2 }));
  holesBack.push(window(A, { id: "hall_rear", ...back, c: 1.0, sill: GF + 0.9, w: 1.0, h: 1.3 }));
  holesBack.push(window(A, { id: "bedroom_w_rear", ...back, c: -3.0, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesBack.push(window(A, { id: "bedroom_e_rear", ...back, c: 1.5, sill: FF + 0.9, w: 1.0, h: 1.25 }));

  // ---- exterior walls (front/back run the full width; gables sit between them)
  const pn = (side) => ({ side, family: "masonry", bands: [[y0, FF - 0.2, 0, GF], [FF - 0.2, EAVE, 1, FF]] });
  wall(A, { ...front, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: holesFront, panel: pn("front") });
  wall(A, { ...back, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: holesBack, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("west") });
  // gables
  for (const s of [-1, 1]) A.prism(0, s > 0 ? XI + 0.03 : -X, s > 0 ? X : -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  // dressed quoins at the four corners
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    for (let i = 0, y = y0; y < EAVE - 0.1; y += 0.31, i++) {
      const longX = i % 2 === 0;
      const lx = longX ? 0.55 : 0.32, lz = longX ? 0.32 : 0.55;
      const xa = sx * (X + 0.015), xb = sx * (X - lx), za = sz * (Z + 0.015), zb = sz * (Z - lz);
      A.box([Math.min(xa, xb), y, Math.min(za, zb)], [Math.max(xa, xb), Math.min(y + 0.29, EAVE), Math.max(za, zb)], "stone_dressed");
    }
  }

  // ---- interior partition (kitchen | hall), both floors
  partition(A, { axis: "z", d0: -0.5, d1: -0.3, a0: -ZI, a1: ZI, y0: GF, y1: FF - 0.2,
    holes: [{ a0: 2.8, a1: 3.8, y0: GF - 1, y1: GF + 2.1 }, { a0: -3.0, a1: -2.0, y0: GF - 1, y1: GF + 2.1 }] });
  partition(A, { axis: "z", d0: -0.5, d1: -0.3, a0: -ZI, a1: ZI, y0: FF, y1: CEIL,
    holes: [{ a0: -3.0, a1: -2.0, y0: FF - 1, y1: FF + 2.1 }] });
  for (const [a0, a1, yy] of [[2.8, 3.8, GF], [-3.0, -2.0, GF], [-3.0, -2.0, FF]]) {
    A.box([-0.52, yy + 2.1, a0 - 0.1], [-0.28, yy + 2.26, a1 + 0.1], "oak_z");
    for (const z of [a0, a1]) A.box([-0.52, yy, z - (z === a0 ? 0.0 : 0.06)], [-0.28, yy + 2.1, z + (z === a0 ? 0.06 : 0.0)], "oak_y");
  }

  // ---- stairs: primary (hall, ascending toward the back) and rear service (east gable, ascending forward)
  const n = 15, run = 3.8;
  const s1 = stairs(A, { x0: -0.3, x1: 0.7, z0: 2.6, dir: -1, y0: GF, rise: FF - GF, n, run, openSide: +1 });
  const s2 = stairs(A, { x0: 4.5, x1: 5.5, z0: -3.0, dir: +1, y0: GF, rise: FF - GF, n, run, openSide: -1 });

  // ---- upper floor slab with two stairwells (boards over joists), main beam
  const slab = [
    [-XI, -0.3, -ZI, ZI], [-0.3, 0.7, -ZI, s1.holeZ[0]], [-0.3, 0.7, s1.holeZ[1], ZI],
    [0.7, 4.5, -ZI, ZI], [4.5, XI, s2.holeZ[1], ZI], [4.5, XI, -ZI, s2.holeZ[0]],
  ];
  for (const [xa, xb, za, zb] of slab) A.box([xa, FF - 0.2, za], [xb, FF, zb], "floor_x");
  for (let x = -XI + 0.35; x < XI - 0.2; x += 0.75) {
    for (const [xa, xb, za, zb] of slab) if (x - 0.06 >= xa && x + 0.06 <= xb) A.box([x - 0.06, FF - 0.36, za], [x + 0.06, FF - 0.2, zb], "oak_z", { k: 0 });
  }
  A.box([-XI, FF - 0.48, -1.75], [4.5, FF - 0.2, -1.45], "oak_x");
  // attic floor / upper ceiling with joists
  A.box([-XI, CEIL, -ZI], [XI, CEIL + 0.2, ZI], "floor_x");
  for (let x = -XI + 0.4; x < XI - 0.2; x += 0.8) A.box([x - 0.06, CEIL - 0.15, -ZI], [x + 0.06, CEIL, ZI], "oak_z", { k: 0 });

  // ---- stairwell balustrades on the upper floor
  railing(A, "z", s1.holeZ[0], s1.holeZ[1], 0.73, FF);
  railing(A, "x", -0.3, 0.7, s1.holeZ[1] + 0.03, FF);
  railing(A, "z", s2.holeZ[0], s2.holeZ[1], 4.47, FF);
  railing(A, "x", 4.5, XI, s2.holeZ[0] - 0.03, FF);

  // ---- hearth (west gable, kitchen) and chimney
  A.box([-XI, GF, -1.0], [-4.95, GF + 1.05, -0.7], "stone_dressed");
  A.box([-XI, GF, 0.7], [-4.95, GF + 1.05, 1.0], "stone_dressed");
  A.box([-XI, GF + 1.05, -1.15], [-4.85, GF + 1.3, 1.15], "oak_z");
  A.box([-XI, GF + 1.3, -0.95], [-5.05, FF - 0.2, 0.95], "plaster_smoke");
  A.box([-XI, GF, -0.7], [-XI + 0.05, GF + 1.05, 0.7], "soot");
  A.box([-XI, GF + 0.85, -0.7], [-4.95, GF + 1.05, 0.7], "soot", { k: 0 });
  A.box([-XI + 0.05, GF, -1.1], [-4.6, GF + 0.035, 1.1], "hearthstone", { k: 0 });
  A.box([-XI, GF, -1.0], [-4.95, GF + 1.05, 1.0], "stone", { v: 0 }); // keeps the player out of the firebox
  for (const z of [-0.35, 0.35]) { A.box([-5.25, GF + 0.035, z - 0.02], [-5.05, GF + 0.3, z + 0.02], "iron", { k: 0 }); }
  for (const [y, z, r] of [[GF + 0.16, -0.12, 8], [GF + 0.16, 0.12, -6], [GF + 0.27, 0.0, 3]]) A.obox([-5.2, y, z], [0.09, 0.09, 0.62], [0, 90 + r, 0], "bark", { k: 0 });
  A.box([-XI, FF, -0.6], [-5.1, CEIL, 0.6], "plaster");   // chimney breast upstairs
  A.box([-X, 7.6, -0.45], [-5.15, 10.0, 0.45], "stone");
  A.box([-X - 0.05, 10.0, -0.5], [-5.1, 10.12, 0.5], "stone_dressed");
  for (const z of [-0.2, 0.2]) A.box([-5.7, 10.12, z - 0.09], [-5.45, 10.4, z + 0.09], "ridge_tile");

  // ---- roof: slate on two planes, clay ridge, fascia + zinc gutters
  const th = Math.atan2(RIDGE - EAVE, Z), deg = (th * 180) / Math.PI, ov = 0.38, gov = 0.3, t = 0.14;
  for (const s of [1, -1]) {
    const zA = -0.12 * s, zB = s * (Z + ov);
    const yu = (z) => RIDGE - Math.abs(z) * Math.tan(th) * Math.sign(z * s || 1);
    const zm = (zA + zB) / 2, ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2;
    const len = Math.abs(zB - zA) / Math.cos(th);
    const a = s > 0 ? deg : -deg;
    const nrm = [0, Math.cos((a * Math.PI) / 180), Math.sin((a * Math.PI) / 180)];
    A.obox([0, ym + nrm[1] * t / 2, zm + nrm[2] * t / 2], [2 * X + 2 * gov, t, len], [a, 0, 0], "slate");
    void yu;
    A.box([-X - gov, EAVE - ov * Math.tan(th) - 0.24, Math.min(s * (Z + ov - 0.03), s * (Z + ov))], [X + gov, EAVE - ov * Math.tan(th) + 0.02, Math.max(s * (Z + ov - 0.03), s * (Z + ov))], "oak_x");
    A.box([-X - gov, EAVE - ov * Math.tan(th) - 0.2, Math.min(s * (Z + ov), s * (Z + ov + 0.13))], [X + gov, EAVE - ov * Math.tan(th) - 0.1, Math.max(s * (Z + ov), s * (Z + ov + 0.13))], "zinc", { k: 0 });
  }
  A.obox([0, RIDGE + t / Math.cos(th) - 0.02, 0], [2 * X + 2 * gov + 0.04, 0.2, 0.2], [45, 0, 0], "ridge_tile");
  // barge boards
  for (const sx of [-1, 1]) for (const s of [1, -1]) {
    const a = s > 0 ? deg : -deg, len = (Z + ov) / Math.cos(th);
    const zm = s * (Z + ov) / 2, ym = (RIDGE + EAVE - ov * Math.tan(th)) / 2;
    A.obox([sx * (X + gov - 0.02), ym + 0.02, zm], [0.04, 0.24, len], [a, 0, 0], "oak_x", { k: 0 });
  }
  // downpipe
  A.box([X - 0.25, 0.05, Z + 0.06], [X - 0.17, EAVE - 0.3, Z + 0.14], "zinc", { k: 0 });

  // ---- furniture: kitchen
  table(A, "Kitchen table", -3.0, 0.6, 1.9, 0.85, GF);
  chair(A, "Kitchen chair", -3.5, 1.33, GF, "-z");
  chair(A, "Kitchen chair", -2.5, 1.33, GF, "-z");
  chair(A, "Kitchen chair", -1.65, 0.6, GF, "-x");
  bench(A, "Kitchen bench", -3.85, -2.15, -0.2, GF);
  dresser(A, "Dresser with plates", -3.3, -1.7, -ZI, +1, GF);
  chest(A, "Grain chest", -1.35, -0.55, -1.15, -0.5, GF, 0.7);
  // cast-iron range beside the hearth with its flue into the chimney
  A.beginFurniture("Cast-iron range");
  A.box([-XI, GF + 0.1, -2.55], [-4.85, GF + 0.8, -1.55], "cast_iron");
  A.box([-XI, GF + 0.8, -2.6], [-4.8, GF + 0.85, -1.5], "cast_iron");
  A.box([-4.86, GF + 0.25, -2.4], [-4.84, GF + 0.6, -2.05], "iron");
  A.box([-4.84, GF + 0.62, -2.45], [-4.81, GF + 0.645, -1.65], "brass");
  for (const z of [-2.5, -1.6]) A.box([-5.35, GF, z - 0.04], [-4.95, GF + 0.1, z + 0.04], "cast_iron");
  A.box([-5.3, GF + 0.85, -2.12], [-5.15, GF + 2.2, -1.97], "cast_iron");
  A.box([-XI, GF + 2.05, -2.12], [-5.15, GF + 2.2, -1.97], "cast_iron");
  A.box([-5.1, GF + 0.85, -2.3], [-4.9, GF + 0.98, -2.1], "copper");
  A.endFurniture();
  // copper pans on the wall, lamp on the table
  A.beginFurniture("Copper pans");
  for (const [z, s] of [[1.35, 0.22], [1.75, 0.28], [2.2, 0.32]]) A.box([-XI, GF + 1.35, z - s / 2], [-XI + 0.02, GF + 1.35 + s, z + s / 2], "copper");
  A.box([-XI, GF + 1.95, 1.15], [-XI + 0.05, GF + 1.98, 2.45], "iron");
  A.endFurniture(false);
  A.beginFurniture("Oil lamp");
  A.box([-3.08, GF + 0.76, 0.52], [-2.92, GF + 0.8, 0.68], "brass");
  A.box([-3.04, GF + 0.8, 0.56], [-2.96, GF + 0.9, 0.64], "brass");
  A.box([-3.03, GF + 0.9, 0.57], [-2.97, GF + 1.08, 0.63], "glass");
  A.endFurniture(false);
  // ---- hall / front room
  A.beginFurniture("Long-case clock");
  A.box([5.05, GF, 1.5], [XI, GF + 0.55, 1.98], "cherry_y");
  A.box([5.15, GF + 0.55, 1.58], [XI, GF + 1.75, 1.9], "cherry_y");
  A.box([5.0, GF + 1.75, 1.45], [XI, GF + 2.3, 2.03], "cherry_y");
  A.box([4.99, GF + 1.86, 1.56], [5.0, GF + 2.2, 1.92], "faience");
  A.box([5.13, GF + 0.8, 1.66], [5.15, GF + 1.6, 1.82], "glass");
  A.box([5.2, GF + 0.85, 1.71], [5.22, GF + 1.35, 1.77], "brass");
  A.endFurniture();
  A.beginFurniture("Armchair");
  A.box([3.0, GF + 0.38, 2.35], [3.7, GF + 0.48, 3.0], "wool_green");
  A.box([3.0, GF + 0.48, 2.85], [3.7, GF + 1.05, 3.0], "wool_green");
  for (const x of [3.0, 3.62]) A.box([x, GF + 0.48, 2.35], [x + 0.08, GF + 0.7, 3.0], "cherry_z");
  for (const x of [3.03, 3.62]) for (const z of [2.38, 2.92]) A.box([x, GF, z], [x + 0.05, GF + 0.38, z + 0.05], "cherry_y");
  A.endFurniture();
  table(A, "Side table", 4.15, 2.75, 0.5, 0.5, GF, 0.62);
  bench(A, "Hall bench", 1.35, 2.35, -3.82, GF);
  A.beginFurniture("Coat pegs");
  A.box([2.3, GF + 1.7, ZI - 0.03], [3.0, GF + 1.8, ZI], "oak_x");
  for (const x of [2.4, 2.65, 2.9]) A.box([x - 0.015, GF + 1.73, ZI - 0.12], [x + 0.015, GF + 1.76, ZI - 0.03], "oak_z");
  A.endFurniture(false);

  // ---- west bedroom
  bed(A, "Bed (west bedroom)", -XI + 0.02, -3.45, 1.3, 2.7, FF, "x0");
  armoire(A, "Armoire normande", -5.3, -3.9, -ZI, +1, FF);
  chest(A, "Blanket chest", -3.38, -2.95, 1.5, 2.5, FF, 0.5);
  A.beginFurniture("Washstand");
  A.box([-1.6, FF + 0.74, 3.35], [-0.85, FF + 0.78, ZI], "cherry_x");
  for (const x of [-1.56, -0.93]) for (const z of [3.39, 3.92]) A.box([x, FF, z], [x + 0.04, FF + 0.74, z + 0.04], "cherry_y");
  A.box([-1.38, FF + 0.78, 3.5], [-1.08, FF + 0.86, 3.8], "faience");
  A.box([-1.0, FF + 0.78, 3.7], [-0.9, FF + 1.0, 3.8], "faience_blue");
  A.endFurniture();
  chair(A, "Bedroom chair", -1.9, 0.0, FF, "-x");
  // ---- east bedroom / landing
  bed(A, "Bed (east bedroom)", 1.4, 2.8, 1.95, ZI - 0.02, FF, "z1");
  armoire(A, "Wardrobe", 3.0, 4.2, -ZI, +1, FF, 0.55, 2.1);
  A.beginFurniture("Writing desk");
  A.box([1.05, FF + 0.72, -ZI], [1.95, FF + 0.76, -3.45], "cherry_x");
  for (const x of [1.08, 1.88]) for (const z of [-3.97, -3.52]) A.box([x, FF, z], [x + 0.04, FF + 0.72, z + 0.04], "cherry_y");
  A.box([1.1, FF + 0.55, -3.95], [1.9, FF + 0.68, -3.5], "cherry_x");
  A.endFurniture();
  chair(A, "Desk chair", 1.5, -3.15, FF, "-z");

  // ---- scripted traversal (asset-local), run by --selftest through the real controller
  A.walkthrough = {
    start: { pos: [1.5, 0, 6.6], yaw: 0 },
    steps: [
      { goto: [1.5, 5.3], note: "approach front door" },
      { look: [1.5, 1.4, 4.0], hold: 0.5 }, { shot: "03_explore_front_door" },
      { interact: "front_door", aim: [1.6, 1.3, 4.03], expectOpen: true },
      { goto: [1.5, 3.55], expectY: GF, note: "stand in the door's swing" },
      { interact: "front_door", aim: [1.97, 1.3, 3.55], expectOpen: true, note: "closing door meets player and reopens" },
      { goto: [1.5, 3.2], expectY: GF, note: "enter hall" },
      { goto: [0.2, 3.3] }, { goto: [-0.9, 3.3], note: "kitchen doorway" },
      { goto: [-3.0, 3.05], expectY: GF, note: "kitchen window" },
      { look: [-3.0, 1.8, 4.6], hold: 0.4 }, { shot: "04a_kitchen_window_closed" },
      { interact: "kitchen_casement", aim: [-2.75, 1.9, 4.05], expectOpen: true },
      { interact: "kitchen_shutters", aim: [-3.0, 1.85, 4.52], expectOpen: true },
      { look: [-3.0, 1.5, 12.0], hold: 0.8 }, { shot: "04b_kitchen_window_open" },
      { goto: [-2.0, 2.3] }, { look: [-5.4, 0.9, -0.2], hold: 1.2 }, { shot: "05_furnished_kitchen" },
      { goto: [-0.9, 3.3] }, { goto: [0.2, 3.3] }, { goto: [0.2, 2.95], note: "front stair foot" },
      { goto: [0.2, 0.75], note: "halfway up the front stair" },
      { goto: [0.2, 1.9], note: "reverse direction mid-flight" },
      { goto: [0.2, -1.7], expectY: FF, note: "front staircase reaches upper floor" },
      { goto: [0.2, -2.4] }, { goto: [2.5, -1.9] }, { goto: [3.6, 1.3] }, { goto: [3.6, 3.35], expectY: FF, note: "east bedroom window" },
      { interact: "bedroom_e_front_casement", aim: [3.85, 4.8, 4.05], expectOpen: true },
      { look: [3.6, 2.6, 16.0], hold: 0.8 }, { shot: "06_upper_floor_view" },
      { goto: [3.6, 1.3] }, { goto: [5.0, 1.5], expectY: FF, note: "rear stair head" },
      { goto: [5.0, -3.45], expectY: GF, note: "rear service staircase reaches ground floor" },
      { goto: [3.9, -3.55] }, { goto: [3.35, -2.85], note: "clear of the rear door swing" },
      { interact: "rear_door", aim: [3.15, 1.3, -4.03], expectOpen: true },
      { goto: [3.2, -5.2], note: "exit through the rear door" },
      { interact: "rear_door", aim: [2.82, 1.3, -3.7], expectOpen: false, note: "close the rear door from outside" },
      { goto: [3.2, -6.4] }, { look: [3.0, 1.8, -4.5], hold: 0.6 }, { shot: "07_rear_exit" },
    ],
  };
  return A;
}


// =============================================================================
// Single-storey Norman stone cottage (chaumière en pierre), 8 x 6 m.
// Living room with hearth and a separate bedroom. One entrance, three shuttered windows.
// =============================================================================
function cottage() {
  const A = new Asset({
    id: "house_stone_cottage", version: 1, name: "Single-Story Stone Cottage", category: "Houses",
    description: "Compact rubble-stone cottage under slate. Living room with hearth, table and dresser; separate bedroom. One ledged door and three shuttered casements. Single storey: no stairs.",
    floors: 1, enterable: true, footprint: [8.2, 6.2], plinthDepth: 1.0, maxGrade: 0.8,
  });
  stdMaterials(A);
  const X = 4, Z = 3, T = 0.5, XI = X - T, ZI = Z - T, GF = 0.3, EAVE = 3.4, CEIL = 3.05, RIDGE = 5.6, y0 = GF - 0.02;
  A.box([-X - 0.05, -1.0, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "terracotta");
  const front = { axis: "x", outer: Z, inner: ZI }, back = { axis: "x", outer: -Z, inner: -ZI };
  const hf = [], hb = [];
  hf.push(door(A, { id: "front_door", ...front, c: 0.6, floor: GF, w: 1.0, h: 2.05, hingeSide: -1 }));
  hf.push(window(A, { id: "living", ...front, c: -2.2, sill: GF + 0.9, w: 1.0, h: 1.25 }));
  hf.push(window(A, { id: "bedroom", ...front, c: 2.4, sill: GF + 0.9, w: 0.9, h: 1.2, shuttersOpen: false }));
  hb.push(window(A, { id: "living_rear", ...back, c: -1.5, sill: GF + 0.9, w: 0.9, h: 1.2 }));
  const pn = (side) => ({ side, family: "masonry", bands: [[y0, EAVE, 0, GF]] });
  wall(A, { ...front, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: hf, panel: pn("front") });
  wall(A, { ...back, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: hb, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("west") });
  quoins(A, X, Z, y0, EAVE);
  partition(A, { axis: "z", d0: 1.2, d1: 1.35, a0: -ZI, a1: ZI, y0: GF, y1: CEIL, holes: [{ a0: -2.2, a1: -1.3, y0: GF - 1, y1: GF + 2.05 }] });
  A.box([1.18, GF + 2.05, -2.3], [1.37, GF + 2.2, -1.2], "oak_z");
  A.box([-XI, CEIL, -ZI], [XI, CEIL + 0.2, ZI], "floor_x");
  for (let x = -XI + 0.4; x < XI - 0.2; x += 0.8) A.box([x - 0.06, CEIL - 0.15, -ZI], [x + 0.06, CEIL, ZI], "oak_z", { k: 0 });
  A.box([-XI, CEIL - 0.42, -0.15], [XI, CEIL - 0.15, 0.15], "oak_x");
  gableRoof(A, { X, Z, XI, EAVE, RIDGE });
  // hearth on the west gable + chimney stack
  A.box([-XI, GF, -0.85], [-3.0, GF + 1.0, -0.6], "stone_dressed");
  A.box([-XI, GF, 0.6], [-3.0, GF + 1.0, 0.85], "stone_dressed");
  A.box([-XI, GF + 1.0, -1.0], [-2.9, GF + 1.22, 1.0], "oak_z");
  A.box([-XI, GF + 1.22, -0.8], [-3.08, CEIL, 0.8], "plaster_smoke");
  A.box([-XI, GF, -0.6], [-XI + 0.05, GF + 1.0, 0.6], "soot");
  A.box([-XI + 0.05, GF, -0.95], [-2.7, GF + 0.035, 0.95], "hearthstone", { k: 0 });
  A.box([-XI, GF, -0.85], [-3.0, GF + 1.0, 0.85], "stone", { v: 0 });
  for (const [y, z, r] of [[GF + 0.15, -0.1, 6], [GF + 0.15, 0.12, -5]]) A.obox([-3.25, y, z], [0.09, 0.09, 0.55], [0, 90 + r, 0], "bark", { k: 0 });
  A.box([-X, 4.6, -0.4], [-3.2, 6.3, 0.4], "stone");
  A.box([-X - 0.05, 6.3, -0.45], [-3.15, 6.42, 0.45], "stone_dressed");
  // furniture
  table(A, "Table", -1.6, 0.1, 1.4, 0.8, GF);
  chair(A, "Chair", -2.0, 0.8, GF, "-z");
  chair(A, "Chair", -1.2, -0.6, GF, "+z");
  dresser(A, "Dresser with plates", -0.9, 0.4, -ZI, +1, GF);
  bed(A, "Bed", 2.15, 3.45, -1.05, 0.95, GF, "z0");
  chest(A, "Blanket chest", 2.6, 3.4, 1.1, 1.45, GF, 0.5);
  chair(A, "Bedroom chair", 3.1, -2.05, GF, "-x");
  A.beginFurniture("Copper pans");
  for (const [z, s] of [[1.05, 0.22], [1.45, 0.28]]) A.box([-XI, GF + 1.3, z - s / 2], [-XI + 0.02, GF + 1.3 + s, z + s / 2], "copper");
  A.endFurniture(false);
  A.walkthrough = {
    start: { pos: [0.6, 0, 5.0], yaw: 0 },
    steps: [
      { goto: [0.6, 3.8], note: "approach cottage door" },
      { interact: "front_door", aim: [0.8, 1.3, 2.53], expectOpen: true },
      { goto: [0.6, 1.6], expectY: GF, note: "enter cottage" },
      { goto: [-2.2, 1.85], expectY: GF, note: "living-room window" },
      { interact: "living_casement", aim: [-2.0, 1.9, 2.55], expectOpen: true },
      { interact: "living_casement", aim: [-2.67, 1.9, 2.3], expectOpen: false, note: "close it again" },
      { goto: [-0.4, 1.6] }, { goto: [0.5, -1.6] }, { goto: [1.75, -1.75], expectY: GF, note: "bedroom doorway" },
      { goto: [1.75, 1.6] }, { goto: [2.25, 1.75], expectY: GF, note: "bedroom window" },
      { interact: "bedroom_casement", aim: [2.6, 1.85, 2.55], expectOpen: true },
      { interact: "bedroom_shutters", aim: [2.4, 1.8, 3.02], expectOpen: true },
      { look: [2.4, 1.6, 9.0], hold: 0.6 }, { shot: "08_cottage_bedroom_window" },
      { goto: [1.75, 1.6] }, { goto: [1.75, -1.75] }, { goto: [0.5, -1.6] }, { goto: [0.6, 1.6] }, { goto: [0.6, 4.2], note: "leave cottage" },
    ],
  };
  return A;
}

const assets = [farmhouse(), cottage(), workshop(), dairy(), storehouse(), office(), mgCasemate(), observationPost(), fieldShelter(), commandShelter(), largeBunker(), pakCasemate(), mg42Mount(), schoolhouse(), inn(), warehouse(), municipal(), pak40(), m2hb(), ...crates()];
for (const A of assets) {
  const j = A.json();
  fs.writeFileSync(path.join(outAssets, j.id + ".json"), JSON.stringify(j));
  const nParts = j.parts.length + j.furniture.reduce((s, f) => s + f.parts.length, 0);
  console.log(`${j.id}: ${nParts} parts, ${j.interactables.length} interactables, ${j.furniture.length} furniture groups`);
}

// ---- first-person weapons
const outWeapons = path.join(root, "content", "weapons");
fs.mkdirSync(outWeapons, { recursive: true });
for (const w of weapons()) {
  fs.writeFileSync(path.join(outWeapons, w.id + ".json"), JSON.stringify(w));
  const n = Object.values(w.parts).reduce((s, p) => s + p.length, 0);
  console.log(`weapon ${w.id}: ${n} parts`);
}

// ---- templates
const templates = [
  { id: "blank_slate", version: 1, order: 0, name: "Blank Slate", available: true, seed: 1944,
    description: "About 600 x 600 m of level, buildable Norman pasture. Gentle variation in grass colour and height for scale. The whole marked build area accepts structures, including its corners. Toggle the grid overlay with H.",
    size: 800, buildHalf: 300, camera: { pos: [0, 38, 95], yaw: 0, pitch: -18 } },
  { id: "omaha_easy_red", version: 0, order: 1, name: "Normandy Beaches - Omaha, Easy Red", available: false, seed: 1,
    description: "Terrain reconstruction of the E-1 draw sector, 6 June 1944, with all wartime structures intentionally omitted. Planned; see docs/normandy-terrain.md for the research plan.",
    size: 800, buildHalf: 300, camera: { pos: [0, 30, 120], yaw: 0, pitch: -12 } },
  { id: "normandy_forest", version: 0, order: 2, name: "Normandy Forest", available: false, seed: 7,
    description: "Fictional Norman woodland with three connected clearings (command post, overlook, fallback). Planned for a later milestone.",
    size: 800, buildHalf: 300, camera: { pos: [0, 30, 120], yaw: 0, pitch: -12 } },
];
for (const t of templates) fs.writeFileSync(path.join(outTemplates, t.id + ".json"), JSON.stringify(t, null, 2));
console.log(`${templates.length} templates written`);
