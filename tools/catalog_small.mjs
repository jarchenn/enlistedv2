// Small buildings: carpenter's workshop, stone dairy, storehouse, roadside office.
import { Asset } from "./kit.mjs";
import { stdMaterials, wall, window, door, gableRoof, quoins, table, chair, bench, chest } from "./parts.mjs";
import { shelfX, shelfZ, crates, barrel, sacks, stove, stool, crateBox } from "./furniture2.mjs";

function shell(A, o) {
  const { X, Z, T, GF, EAVE, RIDGE } = o;
  const XI = X - T, ZI = Z - T, y0 = GF - 0.02, pt = o.pt ?? 0.03;
  A.box([-X - 0.05, -0.8, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], o.floor || "hearthstone");
  const stone = o.stone || "stone", plaster = o.plaster || "plaster";
  wall(A, { axis: "x", outer: Z, inner: ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: o.front || [], stone, plaster, pt });
  wall(A, { axis: "x", outer: -Z, inner: -ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: o.back || [], stone, plaster, pt });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: EAVE, holes: o.east || [], stone, plaster, pt });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: EAVE, holes: o.west || [], stone, plaster, pt });
  if (o.quoins !== false) quoins(A, X, Z, y0, EAVE);
  gableRoof(A, { X, Z, XI: XI + pt - 0.03, EAVE, RIDGE, gableMat: stone });
  if (o.ceil) A.box([-XI, o.ceil, -ZI], [XI, o.ceil + 0.12, ZI], o.ceilMat || "plaster");
  return { XI, ZI };
}

// ----------------------------------------------------------------------------- carpenter's workshop
export function workshop() {
  const A = new Asset({
    id: "small_carpenter_workshop", version: 1, name: "Carpenter's Workshop", category: "Small buildings",
    description: "Single-storey stone workshop with wide double cart doors, open timber roof, workbench under the rear windows, lumber rack and tool wall. Wide door gives a clear line into the yard.",
    floors: 1, enterable: true, footprint: [9.2, 6.2], plinthDepth: 0.8, maxGrade: 0.7,
  });
  stdMaterials(A);
  const X = 4.5, Z = 3, T = 0.45, GF = 0.15, EAVE = 3.3, RIDGE = 5.3, XI = X - T, ZI = Z - T;
  const fr = { axis: "x", outer: Z, inner: ZI }, bk = { axis: "x", outer: -Z, inner: -ZI };
  const front = [door(A, { id: "cart_doors", ...fr, c: 0, floor: GF, w: 2.2, h: 2.4, double: true, leafMat: "plank_y", label: "cart doors" }),
    window(A, { id: "front", ...fr, c: -3.0, sill: GF + 0.9, w: 1.0, h: 1.25 })];
  const back = [window(A, { id: "rear_w", ...bk, c: -2.6, sill: GF + 0.9, w: 1.0, h: 1.25 }), window(A, { id: "rear_e", ...bk, c: 2.6, sill: GF + 0.9, w: 1.0, h: 1.25 })];
  shell(A, { X, Z, T, GF, EAVE, RIDGE, front, back });
  for (const x of [-1.5, 1.5]) A.box([x - 0.1, EAVE - 0.26, -ZI], [x + 0.1, EAVE - 0.02, ZI], "oak_z");
  // workbench with vice, planes and mallet
  A.beginFurniture("Workbench");
  A.box([-1.6, GF + 0.78, -ZI], [1.6, GF + 0.86, -1.85], "oak_x");
  for (const x of [-1.5, 1.42]) for (const z of [-2.47, -1.95]) A.box([x, GF, z], [x + 0.08, GF + 0.78, z + 0.08], "oak_y");
  A.box([-1.5, GF + 0.18, -2.45], [1.5, GF + 0.22, -1.95], "plank_x");
  A.box([1.25, GF + 0.86, -1.95], [1.45, GF + 0.98, -1.8], "iron");
  A.box([1.32, GF + 0.92, -1.8], [1.38, GF + 0.95, -1.6], "steel_bare");
  A.box([-0.9, GF + 0.86, -2.2], [-0.62, GF + 0.95, -2.12], "cherry_x");
  A.box([-0.3, GF + 0.86, -2.3], [0.3, GF + 0.88, -2.0], "plank_x");
  A.box([0.6, GF + 0.86, -2.0], [0.7, GF + 0.96, -1.9], "oak_y");
  A.endFurniture();
  A.beginFurniture("Tool wall");
  A.box([-1.3, GF + 1.15, -ZI], [1.3, GF + 1.95, -ZI + 0.02], "plank_x");
  A.box([-1.2, GF + 1.35, -ZI + 0.02], [-0.6, GF + 1.6, -ZI + 0.025], "steel_bare");
  A.box([-0.6, GF + 1.42, -ZI + 0.02], [-0.45, GF + 1.53, -ZI + 0.05], "oak_y");
  for (let i = 0; i < 6; i++) A.box([-0.2 + i * 0.1, GF + 1.35, -ZI + 0.02], [-0.18 + i * 0.1, GF + 1.6, -ZI + 0.04], i % 2 ? "steel_bare" : "oak_y");
  A.box([0.6, GF + 1.3, -ZI + 0.02], [1.0, GF + 1.33, -ZI + 0.04], "steel_bare");
  A.box([0.6, GF + 1.3, -ZI + 0.02], [0.63, GF + 1.75, -ZI + 0.04], "steel_bare");
  A.endFurniture(false);
  // lumber rack along the east gable
  A.beginFurniture("Lumber rack");
  for (const z of [-1.8, 0, 1.8]) A.box([XI - 0.5, GF, z - 0.05], [XI, GF + 1.9, z + 0.05], "oak_y");
  for (const y of [0.4, 0.95, 1.5]) {
    A.box([XI - 0.5, GF + y, -2.0], [XI, GF + y + 0.04, 2.0], "oak_x", { k: 0 });
    for (let k = 0; k < 4; k++) A.box([XI - 0.48 + k * 0.12, GF + y + 0.04, -2.1], [XI - 0.38 + k * 0.12, GF + y + 0.07, 2.1], "plank_z");
  }
  A.endFurniture();
  // sawhorses with a beam
  A.beginFurniture("Sawhorses");
  for (const z of [-0.7, 0.7]) {
    A.box([1.6, GF + 0.62, z - 0.05], [2.6, GF + 0.7, z + 0.05], "oak_x");
    for (const x of [1.7, 2.45]) for (const s of [-1, 1]) A.obox([x, GF + 0.31, z + s * 0.08], [0.05, 0.66, 0.05], [s * 14, 0, 0], "oak_y");
  }
  A.box([1.8, GF + 0.7, -1.2], [2.0, GF + 0.86, 1.2], "oak_z");
  A.endFurniture();
  chest(A, "Tool chest", -XI, -XI + 0.5, -1.6, -0.8, GF, 0.55);
  stove(A, "Workshop stove", -3.6, -1.95, GF, 3.0);
  stool(A, "Stool", -0.6, -1.4, GF, 0.6);
  A.walkthrough = {
    start: { pos: [0, 0, 5.2], yaw: 0 },
    steps: [
      { goto: [0, 4.3], note: "approach cart doors" },
      { interact: "cart_doors", aim: [0.5, 1.3, 2.6], expectOpen: true },
      { goto: [0, 1.0], expectY: GF, note: "enter workshop" },
      { goto: [-3.0, 1.55], expectY: GF, note: "front window" },
      { interact: "front_casement", aim: [-2.78, 1.7, 2.6], expectOpen: true },
      { look: [0.2, 1.0, -2.3], hold: 0.8 }, { shot: "09_workshop_interior" },
      { goto: [0, 1.0] }, { goto: [0, 4.3] },
      { look: [0, 1.4, 0], hold: 0.5 }, { shot: "10_workshop_doors_open" },
      { goto: [0, 5.3], note: "leave workshop" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- stone dairy
export function dairy() {
  const A = new Asset({
    id: "small_stone_dairy", version: 1, name: "Stone Dairy Outbuilding", category: "Small buildings",
    description: "Thick-walled limewashed dairy (laiterie) kept cool by small high windows. Stone slab bench with cream pans, milk churns, butter churn and cheese press.",
    floors: 1, enterable: true, footprint: [6.2, 5.2], plinthDepth: 0.8, maxGrade: 0.7,
  });
  stdMaterials(A);
  const X = 3, Z = 2.5, T = 0.5, GF = 0.15, EAVE = 2.9, RIDGE = 4.5, XI = X - T, ZI = Z - T;
  const fr = { axis: "x", outer: Z, inner: ZI }, bk = { axis: "x", outer: -Z, inner: -ZI };
  const front = [door(A, { id: "dairy_door", ...fr, c: -1.0, floor: GF, w: 1.0, h: 2.0, hingeSide: -1 }),
    window(A, { id: "dairy_front", ...fr, c: 1.4, sill: GF + 1.2, w: 0.7, h: 0.8 })];
  const back = [window(A, { id: "dairy_rear", ...bk, c: 0.5, sill: GF + 1.2, w: 0.7, h: 0.8, shuttersOpen: false })];
  shell(A, { X, Z, T, GF, EAVE, RIDGE, front, back, ceil: 2.7 });
  // slab bench on stone piers along the rear wall, with cream pans and crocks
  A.beginFurniture("Stone slab bench");
  A.box([-XI, GF + 0.78, -ZI], [XI, GF + 0.86, -1.45], "stone_dressed");
  for (const x of [-2.2, -0.6, 1.0, 2.2]) A.box([x - 0.12, GF, -ZI], [x + 0.12, GF + 0.78, -1.55], "stone");
  for (const x of [-1.9, -1.2, -0.5, 0.2]) A.box([x - 0.25, GF + 0.86, -1.95], [x + 0.25, GF + 0.94, -1.55], "faience");
  for (const x of [1.0, 1.4, 1.8]) A.box([x - 0.12, GF + 0.86, -1.9], [x + 0.12, GF + 1.15, -1.66], "terracotta");
  A.endFurniture();
  for (const z of [-1.0, -0.45, 0.1]) barrel(A, "Milk churn", -2.2, z, GF, 0.17, 0.62, "zinc");
  barrel(A, "Butter churn", -2.1, 0.95, GF, 0.2, 0.95, "oak_y");
  A.beginFurniture("Cheese press");
  A.box([2.0, GF, -0.8], [2.45, GF + 0.5, 0.2], "oak_x");
  for (const z of [-0.75, 0.1]) A.box([2.15, GF + 0.5, z], [2.3, GF + 1.5, z + 0.05], "oak_y");
  A.box([2.1, GF + 1.5, -0.8], [2.35, GF + 1.62, 0.2], "oak_z");
  A.box([2.12, GF + 0.5, -0.45], [2.33, GF + 0.75, -0.15], "faience");
  A.box([2.2, GF + 0.75, -0.32], [2.25, GF + 1.5, -0.28], "iron");
  A.endFurniture();
  shelfZ(A, "Crock shelf", 0.6, 1.9, XI, -1, GF + 1.0, 0.9, 0.3, 2, "plank_z", (y, xa, xx) => {
    for (const z of [0.8, 1.2, 1.6]) A.box([xa + 0.05, y, z - 0.1], [xa + 0.25, y + 0.25, z + 0.1], "terracotta");
  });
  A.walkthrough = {
    start: { pos: [-1.0, 0, 4.3], yaw: 0 },
    steps: [
      { goto: [-1.0, 3.4], note: "approach dairy door" },
      { interact: "dairy_door", aim: [-0.8, 1.2, 2.04], expectOpen: true },
      { goto: [-1.0, 0.6], expectY: GF, note: "enter dairy" },
      { goto: [1.4, 0.9], expectY: GF, note: "dairy window" },
      { interact: "dairy_front_casement", aim: [1.55, 1.75, 2.04], expectOpen: true },
      { look: [0, 1.0, -1.8], hold: 0.8 }, { shot: "11_dairy_interior" },
      { goto: [-1.0, 0.6] }, { goto: [-1.0, 3.5], note: "leave dairy" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- storehouse
export function storehouse() {
  const A = new Asset({
    id: "small_storehouse", version: 1, name: "Small Storehouse", category: "Small buildings",
    description: "Bare-stone store with plank double doors, a single shuttered gable window, shelving of crates and sacks, and barrels by the door.",
    floors: 1, enterable: true, footprint: [7.2, 5.2], plinthDepth: 0.8, maxGrade: 0.7,
  });
  stdMaterials(A);
  const X = 3.5, Z = 2.5, T = 0.45, GF = 0.15, EAVE = 3.0, RIDGE = 4.8, XI = X - T, ZI = Z - T;
  const fr = { axis: "x", outer: Z, inner: ZI };
  const front = [door(A, { id: "store_doors", ...fr, c: 0, floor: GF, w: 1.8, h: 2.2, double: true, leafMat: "plank_y", label: "doors" })];
  const east = [window(A, { id: "gable", axis: "z", outer: X, inner: XI, c: 0, sill: GF + 1.0, w: 0.8, h: 0.9 })];
  shell(A, { X, Z, T, GF, EAVE, RIDGE, front, east, plaster: "stone", floor: "concrete_floor" });
  for (const x of [-1.2, 1.2]) A.box([x - 0.1, EAVE - 0.26, -ZI], [x + 0.1, EAVE - 0.02, ZI], "oak_z");
  shelfX(A, "Store shelving", -2.95, 1.6, -ZI, 1, GF, 2.0, 0.45, 3, "plank_x", (y, za, zz, i) => {
    for (let x = -2.7; x < 1.4; x += 0.62) crateBox(A, x, y, (za + zz) / 2, 0.5, 0.32 + 0.05 * ((i + Math.round(x * 3)) % 2), 0.38, i === 1 ? "crate_olive" : "plank_x");
  });
  crates(A, "Crate stack", [[-2.55, GF, 1.5, 0.8, 0.55, 0.6], [-2.55, GF + 0.55, 1.5, 0.75, 0.5, 0.55], [-2.5, GF, 0.75, 0.7, 0.5, 0.6, 12], [-1.75, GF, 1.6, 0.55, 0.45, 0.5, -8]]);
  sacks(A, "Grain sacks", [[-2.45, GF, -0.4, 5], [-1.9, GF, -0.35, -10], [-2.2, GF + 0.3, -0.38, 2]]);
  barrel(A, "Cider barrel", 2.4, 0.85, GF);
  barrel(A, "Cider barrel", 2.4, 1.5, GF);
  A.walkthrough = {
    start: { pos: [0, 0, 4.4], yaw: 0 },
    steps: [
      { goto: [0, 3.5], note: "approach store doors" },
      { interact: "store_doors", aim: [0.4, 1.3, 2.1], expectOpen: true },
      { goto: [0, 0.5], expectY: GF, note: "enter storehouse" },
      { goto: [2.2, 0.0], expectY: GF, note: "gable window" },
      { interact: "gable_casement", aim: [3.09, 1.6, 0.15], expectOpen: true },
      { look: [-1.0, 1.0, -1.8], hold: 0.8 }, { shot: "12_storehouse_interior" },
      { goto: [0, 0.5] }, { goto: [0, 3.6], note: "leave storehouse" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- roadside office
export function office() {
  const A = new Asset({
    id: "small_roadside_office", version: 1, name: "Roadside Office", category: "Small buildings",
    description: "One-room rendered office of the kind found at level crossings and road depots: desk with telephone and ledger, filing cabinet, stove, waiting bench and notice board.",
    floors: 1, enterable: true, footprint: [5.2, 4.2], plinthDepth: 0.7, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 2.5, Z = 2.0, T = 0.35, GF = 0.2, EAVE = 3.0, RIDGE = 4.2, XI = X - T, ZI = Z - T;
  const fr = { axis: "x", outer: Z, inner: ZI }, bk = { axis: "x", outer: -Z, inner: -ZI };
  const front = [door(A, { id: "office_door", ...fr, c: -1.2, floor: GF, w: 1.0, h: 2.1, hingeSide: -1 }),
    window(A, { id: "office_front", ...fr, c: 0.9, sill: GF + 0.9, w: 1.0, h: 1.3 })];
  const back = [window(A, { id: "office_rear", ...bk, c: 0.2, sill: GF + 0.9, w: 1.0, h: 1.3 })];
  shell(A, { X, Z, T, GF, EAVE, RIDGE, front, back, stone: "render", quoins: false, floor: "floor_x", ceil: 2.8 });
  A.beginFurniture("Desk");
  A.box([-0.5, GF + 0.72, -ZI], [0.9, GF + 0.76, -1.05], "oak_x");
  A.box([-0.5, GF, -ZI], [-0.1, GF + 0.72, -1.05], "oak_y");
  A.box([0.5, GF, -ZI], [0.9, GF + 0.72, -1.05], "oak_y");
  A.box([-0.3, GF + 0.76, -1.5], [0.15, GF + 0.79, -1.2], "paper");
  A.box([0.55, GF + 0.76, -1.55], [0.75, GF + 0.86, -1.4], "blackboard");
  A.box([0.6, GF + 0.86, -1.52], [0.7, GF + 0.9, -1.43], "blackboard");
  A.box([0.25, GF + 0.76, -1.6], [0.31, GF + 0.83, -1.54], "glass");
  A.endFurniture();
  chair(A, "Desk chair", 0.2, -0.72, GF, "-z");
  A.beginFurniture("Filing cabinet");
  A.box([-XI, GF, -ZI], [-1.65, GF + 1.3, -1.2], "steel_plate");
  for (const y of [0.3, 0.7, 1.1]) A.box([-2.05, GF + y - 0.02, -1.2], [-1.75, GF + y + 0.02, -1.18], "brass");
  A.endFurniture();
  stove(A, "Office stove", -1.85, -0.1, GF, 2.55, [-XI, -0.1]);
  bench(A, "Waiting bench", -0.7, 0.7, 0, GF);
  A.beginFurniture("Notice board");
  A.box([XI - 0.02, GF + 1.2, -0.7], [XI, GF + 1.9, 0.7], "cherry_z");
  for (const [z, y] of [[-0.45, 1.65], [-0.05, 1.5], [0.35, 1.7], [0.2, 1.3]]) A.box([XI - 0.025, GF + y - 0.12, z - 0.1], [XI - 0.02, GF + y + 0.12, z + 0.1], "paper");
  A.endFurniture(false);
  A.walkthrough = {
    start: { pos: [-1.2, 0, 3.4], yaw: 0 },
    steps: [
      { goto: [-1.2, 2.75], note: "approach office door" },
      { interact: "office_door", aim: [-1.0, 1.3, 1.69], expectOpen: true },
      { goto: [-1.2, 0.5], expectY: GF, note: "enter office" },
      { goto: [0.9, 0.8], expectY: GF, note: "office window" },
      { interact: "office_front_casement", aim: [1.1, 1.75, 1.69], expectOpen: true },
      { look: [0.2, 0.9, -1.5], hold: 0.8 }, { shot: "20_office_interior" },
      { goto: [-1.2, 0.5] }, { goto: [-1.2, 3.0], note: "leave office" },
    ],
  };
  return A;
}
