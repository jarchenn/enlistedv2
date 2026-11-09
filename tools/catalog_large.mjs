// Large multi-storey buildings (3-5 floors). Each has TWO independent stair cores (switchback flights
// with half landings, stacked floor over floor) at opposite ends of a corridor that runs along the
// rear wall on every floor. Rooms open off the corridor; the ground-floor hall opens to the front door.
import { Asset, cutRect } from "./kit.mjs";
import { stdMaterials, wall, partition, window, door, stairs, railing, gableRoof, quoins, table, chair, bench, bed, armoire, chest } from "./parts.mjs";
import { shelfX, shelfZ, crates, barrel, sacks, stove, lamp, crateBox } from "./furniture2.mjs";

const CORE_W = 1.2, SPINE = 0.1, ENC = 0.15, LAND = 1.3;

function largeBuilding(A, c) {
  const { X, Z, n, h } = c;
  // half-landings: boards like floor_x, but a material interior.cpp reads as neither a floor (a floor_x landing became a
  // storey and hid the floors around it) nor a stair tread (tread_x merged it into the flights and swung the stair
  // keep-clear box across the corridor)
  A.mat("landing_x", [0.24, 0.16, 0.095], 0.62, "floorfin", { grain: 0 });
  const T = c.T ?? 0.5, GF = 0.3, XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  const EAVE = GF + n * h, RIDGE = EAVE + (c.roofRise ?? 3.2);
  const zA = -ZI + (c.corridor ?? 2.0);                    // corridor/core boundary
  const nr = Math.round(h / 2 / 0.18), R = nr * 0.27;       // risers and run per half flight
  const coreLen = 2 * CORE_W + SPINE;
  const cores = [
    { name: "A", xa: -XI, encX: [-XI + coreLen, -XI + coreLen + ENC] },
    { name: "B", xa: XI - coreLen, encX: [XI - coreLen - ENC, XI - coreLen] },
  ];
  for (const k of cores) {
    k.l1 = [k.xa, k.xa + CORE_W]; k.l2 = [k.xa + CORE_W + SPINE, k.xa + coreLen];
    k.l1c = (k.l1[0] + k.l1[1]) / 2; k.l2c = (k.l2[0] + k.l2[1]) / 2;
  }
  const xL = cores[0].encX[1], xR = cores[1].encX[0];
  const yAt = (k) => GF + k * h;

  // ---- plinth, ground floor, slabs with two stairwell holes, attic floor
  A.box([-X - 0.06, -1.2, -Z - 0.06], [X + 0.06, y0, Z + 0.06], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], c.groundFloor || "terracotta");
  const holes = cores.map((k) => ({ a0: k.xa, a1: k.xa + coreLen, y0: zA, y1: zA + R + LAND }));
  for (let f = 1; f < n; f++) for (const [x0, x1, z0, z1] of cutRect(-XI, XI, -ZI, ZI, holes)) A.box([x0, yAt(f) - 0.2, z0], [x1, yAt(f), z1], c.upperFloor || "floor_x");
  A.box([-XI, EAVE - 0.2, -ZI], [XI, EAVE, ZI], "plaster");

  // ---- stair cores
  for (const k of cores) {
    for (let f = 0; f < n - 1; f++) {
      const y = yAt(f);
      stairs(A, { x0: k.l1[0], x1: k.l1[1], z0: zA, dir: +1, y0: y, rise: h / 2, n: nr, run: R, openSide: 1, core: true });
      A.box([k.xa, y + h / 2 - 0.2, zA + R], [k.xa + coreLen, y + h / 2, zA + R + LAND], "landing_x");
      stairs(A, { x0: k.l2[0], x1: k.l2[1], z0: zA + R, dir: -1, y0: y + h / 2, rise: h / 2, n: nr, run: R, openSide: 1, core: true });
    }
    A.box([k.l1[1], GF, zA], [k.l2[0], EAVE - 0.2, zA + R], "plaster");                     // spine wall
    A.box([k.xa, GF, zA + R + LAND], [k.xa + coreLen, EAVE - 0.2, zA + R + LAND + ENC], "plaster"); // end wall
    A.box([k.encX[0], GF, zA], [k.encX[1], EAVE - 0.2, ZI], "plaster");                     // enclosure side
    A.box([k.l2[0], GF, zA], [k.l2[1], GF + h / 2 - 0.6, zA + R], "plaster");               // closes the void under the first up-flight's return
    railing(A, "x", k.l1[0], k.l1[1], zA + 0.04, yAt(n - 1));                                // top floor: guard over the last flight
    for (let f = 0; f < n; f++) lamp(A, k.xa + coreLen / 2, yAt(f + 1) - 0.2, zA - 0.9);
  }

  // ---- openings
  const front = [], back = [];
  const fr = { axis: "x", outer: Z, inner: ZI }, bk = { axis: "x", outer: -Z, inner: -ZI };
  front.push(door(A, { id: "front_door", ...fr, c: c.frontDoorX ?? 0, floor: GF, w: c.frontDoorW ?? 1.1, h: 2.3, hingeSide: 1, double: !!c.frontDouble, leafMat: c.frontDoorMat }));
  back.push(door(A, { id: "rear_door", ...bk, c: c.rearDoorX ?? 0, floor: GF, w: 1.0, h: 2.2, hingeSide: -1 }));
  const win = c.window || { w: 1.1, h: 1.5, sill: 0.9 };
  for (let f = 0; f < n; f++) {
    const shutters = f < (c.shutterFloors ?? 2);
    for (const [i, x] of (c.frontWindows(f)).entries()) front.push(window(A, { id: `f${f}_${i}`, ...fr, c: x, sill: yAt(f) + win.sill, w: win.w, h: win.h, shutters }));
    for (const [i, x] of (c.rearWindows(f)).entries()) back.push(window(A, { id: `r${f}_${i}`, ...bk, c: x, sill: yAt(f) + win.sill, w: win.w, h: win.h, shutters }));
  }
  const wm = { stone: c.wallMat || "stone", plaster: "plaster" };
  const bands = [];
  for (let f = 0; f < n; f++) bands.push([f === 0 ? y0 : yAt(f) - 0.2, f === n - 1 ? EAVE : yAt(f + 1) - 0.2, f, yAt(f)]);
  const pn = (side) => ({ side, family: "masonry", bands });
  wall(A, { ...fr, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: front, ...wm, panel: pn("front") });
  wall(A, { ...bk, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: back, ...wm, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], ...wm, panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], ...wm, panel: pn("west") });
  quoins(A, X, Z, y0, EAVE);
  // string courses marking each floor
  for (let f = 1; f < n; f++) {
    const y = yAt(f);
    for (const s of [-1, 1]) A.box([-X - 0.04, y - 0.18, Math.min(s * Z, s * (Z + 0.04))], [X + 0.04, y - 0.02, Math.max(s * Z, s * (Z + 0.04))], "stone_dressed", { k: 0 });
    for (const s of [-1, 1]) A.box([Math.min(s * X, s * (X + 0.04)), y - 0.18, -Z], [Math.max(s * X, s * (X + 0.04)), y - 0.02, Z], "stone_dressed", { k: 0 });
  }
  gableRoof(A, { X, Z, XI, EAVE, RIDGE, gableMat: c.wallMat || "stone" });

  // ---- corridor wall + rooms on every floor
  for (let f = 0; f < n; f++) {
    const y = yAt(f), top = yAt(f + 1) - 0.2;
    const parts = c.partitions(f);           // null => open plan floor
    if (parts === null) { c.furnish?.(A, { f, y, x0: xL, x1: xR, z0: zA, z1: ZI, open: true }); continue; }
    const bounds = [xL, ...parts, xR];
    const doors = [];
    for (let r = 0; r + 1 < bounds.length; r++) {
      const cx = (bounds[r] + bounds[r + 1]) / 2;
      doors.push({ a0: cx - 0.5, a1: cx + 0.5, y0: y - 1, y1: y + 2.1 });
      A.box([cx - 0.6, y + 2.1, zA - 0.02], [cx + 0.6, y + 2.25, zA + 0.17], "oak_x");
    }
    partition(A, { axis: "x", d0: zA, d1: zA + 0.15, a0: xL, a1: xR, y0: y, y1: top, holes: doors });
    for (const x of parts) A.box([x - 0.075, y, zA + 0.15], [x + 0.075, top, ZI], "plaster");
    for (let r = 0; r + 1 < bounds.length; r++)
      c.furnish?.(A, { f, r, y, x0: bounds[r] + (r ? 0.075 : 0), x1: bounds[r + 1] - (r + 2 < bounds.length ? 0.075 : 0), z0: zA + 0.15, z1: ZI, doorX: (bounds[r] + bounds[r + 1]) / 2 });
  }

  // ---- traversal: front door -> hall -> corridor -> up core A to the top -> top-floor window ->
  //      corridor -> down core B to the ground -> rear door
  const zLand = zA - 0.6, zH = zA + R + LAND / 2, fdx = c.frontDoorX ?? 0, rdx = c.rearDoorX ?? 0;
  const S = [];
  S.push({ goto: [fdx, Z + 1.1], note: "approach front door" });
  S.push({ interact: "front_door", aim: [fdx + 0.25, GF + 1.1, ZI + 0.05], expectOpen: true });
  S.push({ goto: [fdx, ZI - 1.4], expectY: GF, note: "enter ground-floor hall" });
  S.push({ look: [fdx, GF + 1.4, zA - 1], hold: 0.5 }, { shot: c.shot + "_a_hall" });
  S.push({ goto: [fdx, zA + 0.7] }, { goto: [fdx, zLand], expectY: GF, note: "into the corridor" });
  const [A1, B1] = cores;
  S.push({ goto: [A1.l1c, zLand] });
  for (let f = 0; f < n - 1; f++) {
    S.push({ goto: [A1.l1c, zH], expectY: yAt(f) + h / 2, note: `core A: floor ${f} up to half landing` });
    if (f === 0) S.push({ look: [A1.l2c, yAt(1) + 0.5, zA - 0.4], hold: 0.5 }, { shot: c.shot + "_b_stair_core" });
    S.push({ goto: [A1.l2c, zH] });
    S.push({ goto: [A1.l2c, zLand], expectY: yAt(f + 1), note: `core A reaches floor ${f + 1}` });
    if (f + 1 < n - 1) S.push({ goto: [A1.l1c, zLand] });
  }
  const rw = c.rearWindows(n - 1)[0];
  S.push({ goto: [rw, -ZI + 0.95], expectY: yAt(n - 1), note: "top-floor corridor window" });
  S.push({ interact: `r${n - 1}_0_casement`, aim: [rw + 0.25, yAt(n - 1) + win.sill + win.h / 2, -ZI - 0.05], expectOpen: true });
  S.push({ look: [rw, yAt(n - 1) - 3, -ZI - 20], hold: 0.8 }, { shot: c.shot + "_c_top_floor_view" });
  if (c.roomShot) S.push({ goto: [c.roomShot.door, zLand] }, { look: [c.roomShot.door, yAt(c.roomShot.f) + 0.6, ZI - 0.5], hold: 0.5 });
  S.push({ goto: [B1.l2c, zLand] });
  for (let f = n - 1; f > 0; f--) {
    S.push({ goto: [B1.l2c, zH], expectY: yAt(f) - h / 2, note: `core B: floor ${f} down to half landing` });
    S.push({ goto: [B1.l1c, zH] });
    S.push({ goto: [B1.l1c, zLand], expectY: yAt(f - 1), note: `core B reaches floor ${f - 1}` });
    if (f > 1) S.push({ goto: [B1.l2c, zLand] });
  }
  if (c.roomShot0) S.push({ goto: [c.roomShot0, zLand] }, { look: [c.roomShot0, GF + 0.6, ZI - 0.8], hold: 0.5 }, { shot: c.shot + "_d_room" });
  S.push({ goto: [rdx, zLand], expectY: GF });
  S.push({ interact: "rear_door", aim: [rdx - 0.2, GF + 1.1, -ZI - 0.05], expectOpen: true });
  S.push({ goto: [rdx, -Z - 1.6], note: "leave through the rear door" });
  A.walkthrough = { start: { pos: [fdx, 0, Z + 2.2], yaw: 0 }, steps: S };
  return { XI, ZI, zA, xL, xR, yAt };
}

// -------------------------------------------------------------- furniture specific to large buildings
function pupilDesk(A, x, z, y) {
  A.box([x - 0.55, y + 0.68, z - 0.22], [x + 0.55, y + 0.72, z + 0.18], "oak_x");
  A.box([x - 0.55, y + 0.4, z + 0.32], [x + 0.55, y + 0.44, z + 0.55], "oak_x");
  for (const sx of [-0.5, 0.46]) {
    A.box([x + sx, y, z - 0.2], [x + sx + 0.04, y + 0.68, z + 0.55], "oak_y");
    A.box([x + sx, y + 0.12, z - 0.2], [x + sx + 0.04, y + 0.16, z + 0.55], "oak_z");
  }
  A.box([x - 0.12, y + 0.72, z - 0.15], [x - 0.08, y + 0.74, z - 0.11], "faience");
}
function classroom(A, rm, rows = 3, cols = 3) {
  const { x0, x1, z0, z1, y } = rm;
  A.beginFurniture("Chalkboard");
  A.box([x0, y + 0.95, z0 + 1.2], [x0 + 0.03, y + 2.1, z1 - 0.8], "blackboard");
  A.box([x0, y + 0.92, z0 + 1.2], [x0 + 0.08, y + 0.95, z1 - 0.8], "oak_z");
  A.endFurniture(false);
  A.beginFurniture("Teacher's desk");
  A.box([x0 + 0.6, y, z0 + 1.6], [x0 + 1.2, y + 0.78, z0 + 2.7], "cherry_z");
  A.endFurniture();
  for (let r = 0; r < rows; r++) {
    A.beginFurniture("Pupils' desks");
    for (let cI = 0; cI < cols; cI++) {
      const x = x0 + 2.2 + r * 1.15, z = z0 + 1.55 + cI * 1.05;
      if (x + 0.6 < x1 - 0.2 && z + 0.6 < z1 - 0.1) pupilDesk(A, x, z, y);
    }
    A.endFurniture();
  }
  A.beginFurniture("Wall map");
  A.box([x1 - 0.02, y + 1.2, (z0 + z1) / 2 - 0.6], [x1, y + 2.0, (z0 + z1) / 2 + 0.6], "paper");
  A.endFurniture(false);
}

// ----------------------------------------------------------------------------- schoolhouse (3 floors)
export function schoolhouse() {
  const A = new Asset({ id: "large_village_schoolhouse", version: 1, name: "Village Schoolhouse", category: "Large buildings",
    description: "Three-storey brick-and-stone école communale, 20 m long. A corridor along the back on every floor links two separate stairwells at either end. Classrooms with desks and chalkboards, a teacher's room and a library.",
    floors: 3, enterable: true, footprint: [20.2, 8.2], plinthDepth: 1.2, maxGrade: 1.0 });
  stdMaterials(A);
  largeBuilding(A, {
    X: 10, Z: 4, n: 3, h: 3.4, wallMat: "brick", shot: "21_school",
    frontWindows: (f) => (f === 0 ? [-5.2, -1.9, 1.9, 5.2] : [-5.2, -1.9, 1.9, 5.2]),
    rearWindows: () => [-7.2, -3.5, 3.5, 7.2],
    partitions: () => [-3.5, 3.5],
    roomShot0: -5.2,
    furnish: (A, rm) => {
      const { f, r, x0, x1, z0, z1, y } = rm;
      if (f === 0 && r === 1) {   // entrance hall: benches and coat pegs, centre kept clear
        bench(A, "Hall bench", x0 + 0.3, x0 + 1.9, z1 - 0.35, y);
        bench(A, "Hall bench", x1 - 1.9, x1 - 0.3, z1 - 0.35, y);
        A.beginFurniture("Coat pegs");
        A.box([x0, y + 1.5, z0 + 0.8], [x0 + 0.03, y + 1.6, z1 - 0.8], "oak_z");
        for (let z = z0 + 1.0; z < z1 - 0.9; z += 0.3) A.box([x0 + 0.03, y + 1.52, z - 0.015], [x0 + 0.12, y + 1.56, z + 0.015], "oak_x");
        A.endFurniture(false);
        return;
      }
      if (f === 0 && r === 0) {   // teacher's room
        table(A, "Staff table", (x0 + x1) / 2, z0 + 2.6, 1.6, 0.9, y);
        chair(A, "Chair", (x0 + x1) / 2 - 0.5, z0 + 1.9, y, "+z");
        chair(A, "Chair", (x0 + x1) / 2 + 0.5, z0 + 1.9, y, "+z");
        armoire(A, "Cupboard", x0 + 0.1, x0 + 1.3, z1, -1, y, 0.5, 2.0);
        return;
      }
      if (f === 2 && r === 2) {   // library
        shelfZ(A, "Bookshelves", z0 + 1.1, z1 - 0.1, x1, -1, y, 2.0, 0.32, 5, "cherry_z", (yy, xa, xx, i) => {
          for (let z = z0 + 1.2; z < z1 - 0.3; z += 0.34) A.box([xa + 0.04, yy, z], [xx - 0.03, yy + 0.24 + 0.04 * ((i + Math.round(z * 3)) % 2), z + 0.3], i % 2 ? "wool_red" : "wool_green");
        });
        table(A, "Reading table", (x0 + x1) / 2 - 0.3, z0 + 2.4, 1.2, 0.8, y, 0.76, true);
        return;
      }
      classroom(A, rm, r === 1 ? 4 : 1, 3);
    },
  });
  return A;
}

// ----------------------------------------------------------------------------- inn (4 floors)
export function inn() {
  const A = new Asset({ id: "large_village_inn", version: 1, name: "Village Inn", category: "Large buildings",
    description: "Four-storey rendered auberge. Ground-floor dining room with bar and cider barrels; three floors of small guest rooms off a rear corridor. Main stair and a separate service stair at opposite ends.",
    floors: 4, enterable: true, footprint: [15.2, 8.2], plinthDepth: 1.2, maxGrade: 1.0 });
  stdMaterials(A);
  largeBuilding(A, {
    X: 7.5, Z: 4, n: 4, h: 3.2, wallMat: "render", shot: "22_inn",
    frontWindows: (f) => (f === 0 ? [-3.0, 3.0] : [-2.9, 0, 2.9]),
    rearWindows: () => [-5.5, -2.0, 2.0, 5.5],
    partitions: (f) => (f === 0 ? [] : [-1.5, 1.5]),
    roomShot0: 0,
    furnish: (A, rm) => {
      const { f, x0, x1, z0, z1, y } = rm;
      if (f === 0) {             // dining room: bar along the west, tables east, clear centre lane
        A.beginFurniture("Bar counter");
        A.box([x0 + 0.3, y, z0 + 1.4], [x0 + 0.9, y + 1.05, z1 - 0.6], "cherry_z");
        A.box([x0 + 0.25, y + 1.05, z0 + 1.35], [x0 + 0.95, y + 1.1, z1 - 0.55], "oak_z");
        for (let z = z0 + 1.6; z < z1 - 0.8; z += 0.22) A.box([x0 + 0.55, y + 1.1, z], [x0 + 0.62, y + 1.38, z + 0.07], "glass");
        A.endFurniture();
        shelfZ(A, "Bottle shelves", z0 + 1.2, z1 - 0.4, x0, 1, y + 0.9, 1.4, 0.25, 3, "cherry_z", (yy, xa, xx) => {
          for (let z = z0 + 1.3; z < z1 - 0.6; z += 0.16) A.box([xa + 0.08, yy, z], [xa + 0.15, yy + 0.3, z + 0.07], "glass");
        });
        barrel(A, "Cider barrel", x0 + 0.45, z0 + 0.75, y);
        for (const [tx, tz] of [[x1 - 2.2, z0 + 1.8], [x1 - 2.2, z1 - 1.0], [x1 - 0.9, z0 + 2.9]]) {
          table(A, "Dining table", tx, tz, 0.9, 0.9, y);
          chair(A, "Chair", tx - 0.75, tz, y, "+x");
          chair(A, "Chair", tx + 0.75, tz, y, "-x");
        }
        return;
      }
      // guest room
      const cx = (x0 + x1) / 2;
      bed(A, "Guest bed", x0 + 0.05, x0 + 1.45, z1 - 2.25, z1 - 0.25, y, "z0");
      armoire(A, "Wardrobe", x1 - 0.85, x1 - 0.05, z0, 1, y, 0.5, 1.9);
      A.beginFurniture("Washstand");
      A.box([x1 - 0.6, y + 0.74, z0 + 1.3], [x1 - 0.1, y + 0.78, z0 + 2.0], "cherry_z");
      A.box([x1 - 0.55, y, z0 + 1.35], [x1 - 0.15, y + 0.74, z0 + 1.95], "cherry_y");
      A.box([x1 - 0.5, y + 0.78, z0 + 1.45], [x1 - 0.2, y + 0.86, z0 + 1.85], "faience");
      A.endFurniture();
      chair(A, "Chair", x1 - 0.45, z1 - 1.1, y, "-x"); void cx;
    },
  });
  return A;
}

// ----------------------------------------------------------------------------- warehouse (3 floors)
export function warehouse() {
  const A = new Asset({ id: "large_stone_warehouse", version: 1, name: "Stone Warehouse", category: "Large buildings",
    description: "Three-storey stone warehouse with tall open floors on oak posts, cart doors, crates, sacks and shelving. Two enclosed stair cores at opposite ends; the open floors give long interior sightlines.",
    floors: 3, enterable: true, footprint: [18.2, 9.2], plinthDepth: 1.2, maxGrade: 1.0 });
  stdMaterials(A);
  largeBuilding(A, {
    X: 9, Z: 4.5, n: 3, h: 3.8, wallMat: "stone", shot: "23_warehouse", groundFloor: "concrete_floor",
    frontDouble: true, frontDoorW: 2.6, frontDoorMat: "plank_y", rearDoorX: 3.0,
    window: { w: 0.9, h: 1.2, sill: 1.2 }, shutterFloors: 1,
    frontWindows: () => [-5.6, -3.0, 3.0, 5.6],
    rearWindows: () => [-6.2, -1.0, 1.4, 6.2],
    partitions: () => null,
    roomShot0: 1.4,
    furnish: (A, rm) => {
      const { f, x0, x1, z0, z1, y } = rm;
      const top = y + 3.6;
      for (const x of [-3.5, 0, 3.5]) for (const z of [z0 + 1.6, z1 - 1.4]) if (Math.abs(x) > 0.1) A.box([x - 0.13, y, z - 0.13], [x + 0.13, top, z + 0.13], "oak_y");
      for (const z of [z0 + 1.6, z1 - 1.4]) A.box([x0, top - 0.3, z - 0.14], [x1, top, z + 0.14], "oak_x", { k: 0 });
      crates(A, "Crate stacks", [
        [x0 + 0.7, y, z1 - 0.6, 0.9, 0.7, 0.8], [x0 + 0.7, y + 0.7, z1 - 0.6, 0.85, 0.6, 0.75], [x0 + 1.7, y, z1 - 0.6, 0.9, 0.7, 0.8, 6],
        [x0 + 0.7, y, z1 - 1.6, 0.9, 0.7, 0.8, -4], [x1 - 0.7, y, z1 - 0.6, 0.9, 0.7, 0.8], [x1 - 1.7, y, z1 - 0.6, 0.9, 0.7, 0.8, 9]], f === 1 ? "crate_olive" : "plank_x");
      sacks(A, "Grain sacks", [[x1 - 0.8, y, z1 - 1.7, 0], [x1 - 1.4, y, z1 - 1.75, 8], [x1 - 1.1, y + 0.3, z1 - 1.72, -6], [x1 - 2.2, y, z1 - 1.7, 3]]);
      if (f !== 0) shelfX(A, "Racking", -2.6, 2.6, z1, -1, y, 2.2, 0.5, 4, "plank_x", (yy, za, zz) => {
        for (let x = -2.4; x < 2.3; x += 0.7) crateBox(A, x, yy, (za + zz) / 2, 0.55, 0.35, 0.4, "plank_x");
      });
      if (f === 0) for (const z of [z1 - 0.6, z1 - 1.3]) barrel(A, "Barrel", 2.0, z, y);
    },
  });
  return A;
}

// ----------------------------------------------------------------------------- municipal building (5 floors)
export function municipal() {
  const A = new Asset({ id: "large_municipal_building", version: 1, name: "Municipal Building", category: "Large buildings",
    description: "Five-storey dressed-stone mairie. Entrance hall and offices on the ground floor, council chamber above, offices and archives on the upper floors. A tall observation point over the town; two stairwells serve every floor.",
    floors: 5, enterable: true, footprint: [16.2, 9.2], plinthDepth: 1.2, maxGrade: 1.0 });
  stdMaterials(A);
  largeBuilding(A, {
    X: 8, Z: 4.5, n: 5, h: 3.3, wallMat: "stone_dressed", shot: "24_mairie", roofRise: 3.4,
    frontWindows: (f) => (f === 0 ? [-3.4, 3.4] : [-3.4, -1.0, 1.0, 3.4]),
    rearWindows: () => [-5.8, -2.2, 2.2, 5.8],
    partitions: (f) => (f === 1 ? [] : [-2.0, 2.0]),
    roomShot0: -3.4,
    furnish: (A, rm) => {
      const { f, r, x0, x1, z0, z1, y } = rm;
      const cx = (x0 + x1) / 2;
      if (f === 1) {                 // council chamber
        A.beginFurniture("Council table");
        A.box([cx - 2.6, y + 0.74, z0 + 2.2], [cx + 2.6, y + 0.8, z0 + 3.4], "cherry_x");
        for (const x of [cx - 2.4, cx + 2.32]) for (const z of [z0 + 2.3, z0 + 3.22]) A.box([x, y, z], [x + 0.08, y + 0.74, z + 0.08], "cherry_y");
        A.box([cx - 2.6, y + 0.8, z0 + 2.3], [cx + 2.6, y + 0.805, z0 + 3.3], "wool_green", { k: 0 });
        A.endFurniture();
        for (let x = cx - 2.0; x <= cx + 2.01; x += 1.0) { chair(A, "Councillor's chair", x, z0 + 1.65, y, "+z"); chair(A, "Councillor's chair", x, z0 + 3.95, y, "-z"); }
        A.beginFurniture("Portrait");
        A.box([x0 + 0.02, y + 1.3, cx * 0 + (z0 + z1) / 2 - 0.5], [x0 + 0.05, y + 2.4, (z0 + z1) / 2 + 0.5], "cherry_z");
        A.box([x0 + 0.05, y + 1.4, (z0 + z1) / 2 - 0.4], [x0 + 0.06, y + 2.3, (z0 + z1) / 2 + 0.4], "paper");
        A.endFurniture(false);
        return;
      }
      if (f === 0 && r === 1) {      // entrance hall
        bench(A, "Hall bench", x0 + 0.3, x0 + 1.2, z1 - 0.35, y);
        bench(A, "Hall bench", x1 - 1.2, x1 - 0.3, z1 - 0.35, y);
        A.beginFurniture("Notice board");
        A.box([x0, y + 1.2, z0 + 1.0], [x0 + 0.02, y + 2.0, z0 + 2.6], "cherry_z");
        for (const z of [z0 + 1.2, z0 + 1.7, z0 + 2.2]) A.box([x0 + 0.02, y + 1.35, z], [x0 + 0.025, y + 1.8, z + 0.35], "paper");
        A.endFurniture(false);
        return;
      }
      if (f >= 3 && r !== 1) {       // archives
        for (const zb of [z0 + 1.5, z0 + 2.8]) shelfX(A, "Archive shelving", x0 + 0.3, x1 - 0.3, zb, 1, y, 2.2, 0.4, 5, "plank_x", (yy, za, zz) => {
          for (let x = x0 + 0.4; x < x1 - 0.5; x += 0.3) A.box([x, yy, za + 0.05], [x + 0.26, yy + 0.3, zz - 0.05], "paper");
        });
        return;
      }
      // office
      A.beginFurniture("Office desk");
      A.box([cx - 0.7, y + 0.74, z1 - 1.6], [cx + 0.7, y + 0.78, z1 - 0.9], "oak_x");
      A.box([cx - 0.7, y, z1 - 1.6], [cx - 0.3, y + 0.74, z1 - 0.9], "oak_y");
      A.box([cx + 0.3, y, z1 - 1.6], [cx + 0.7, y + 0.74, z1 - 0.9], "oak_y");
      A.box([cx - 0.2, y + 0.78, z1 - 1.4], [cx + 0.2, y + 0.8, z1 - 1.1], "paper");
      A.endFurniture();
      chair(A, "Chair", cx, z1 - 2.0, y, "+z");
      A.beginFurniture("Filing cabinet");
      A.box([x0 + 0.05, y, z1 - 0.6], [x0 + 0.6, y + 1.3, z1 - 0.05], "steel_plate");
      A.endFurniture();
    },
  });
  return A;
}
