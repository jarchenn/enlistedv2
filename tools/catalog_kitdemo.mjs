// B1 geometry kit demonstration piece (docs/asset-format.md#b1-geometry-kit): every new part type and parts.mjs B1 helper on one
// small garden folly, so B2 / B3 have a working reference and the render / structures / visuals suites a fixture.
// Not enterable: a solid stone core behind real facade depth. Front faces +Z.
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
import { Asset, cutRect, wbox } from "./kit.mjs";
import {
  stdMaterials, pilaster, stringCourse, cornice, surround, archDoor, archWindow, colonnade, balcony, dormer,
  hipRoof, turret, timberFrame, chimney, fbox, weather,
} from "./parts.mjs";
import { cover, setNav, setTour } from "./catalog_atlantic.mjs";

export function kitDemoFolly() {
  const A = new Asset({ id: "kit_demo_folly", version: 1, name: "Garden folly (B1 kit demo)", category: "Landmarks",
    description: "Small stone garden folly built from the B1 geometry kit: an arcaded loggia on round columns, arched door and windows with keystones and fanlights, a half-timbered upper storey with a balcony, pilasters, string course and stepped cornice, a hipped slate roof with a dormer and chimney, a round corner turret with a conical spire and a slim octagonal one.",
    floors: 2, enterable: false, footprint: [9.8, 7.9], plinthDepth: 0.6, maxGrade: 0.6,
    ivy: [{ face: "+z", a0: -3.45, a1: -1.0, y0: 0.45, y1: 3.4 }, { face: "+x", a0: -2.0, a1: 1.9, y0: 0.45, y1: 6.2, density: 0.8 }, { face: "-z", a0: -2.5, a1: 2.0, y0: 0.45, y1: 5.0 }] });
  stdMaterials(A);
  const X = 3.5, Z = 2.25, G = 0.45, F1 = 3.6, F2 = 3.8, EAVE = 6.4;
  // plinth (chamfered), stone core of both storeys, plastered upper core
  const plinth = A.box([-X - 0.5, 0, -Z - 0.5], [X + 0.5, G, Z + 1.65], "stone_plinth", { bv: 0.03 });
  A.box([-X, G, -Z], [X, F1, Z - 0.3], "stone");
  A.box([-X, F1, -Z], [X, F2, Z], "stone_dressed", { bv: 0.02 });   // floor band
  A.box([-X + 0.01, F2, -Z + 0.01], [X - 0.01, EAVE, Z - 0.01], "render");
  // ground-floor front wall (0.3 m) with an arched door and two arched windows, spandrels closing the arch heads
  const fr = { axis: "x", d: Z, out: 1 }, door = { a0: -0.7, a1: 0.7, y1: G + 2.0 }, wins = [[-2.5, -1.7], [1.7, 2.5]];
  const holes = [{ a0: door.a0, a1: door.a1, y0: G, y1: door.y1 + 0.72 }, ...wins.map(([a0, a1]) => ({ a0, a1, y0: G + 0.8, y1: G + 2.0 + 0.42 }))];
  for (const [a0, a1, y0, y1] of cutRect(-X, X, G, F1, holes)) wbox(A, "x", a0, a1, y0, y1, Z - 0.3, Z, "stone");
  archDoor(A, fr, { ...door, jambs: G, wall: { e0: -0.3, e1: 0 }, fan: -0.2 });
  fbox(A, fr, door.a0, door.a1, G, door.y1 - 0.03, -0.26, -0.2, "door_paint");
  for (const [a0, a1] of wins) {
    archWindow(A, fr, { a0, a1, y1: G + 2.0, jambs: G + 0.8, wall: { e0: -0.3, e1: 0 } });
    fbox(A, fr, a0, a1, G + 0.8, G + 2.0 - 0.03, -0.16, -0.14, "glass", { k: 0 });
  }
  // loggia: four round columns and three arches in front of the door, a slab roof over it
  const colonnadeSpring = colonnade(A, { axis: "x", a0: -3.0, a1: 3.0, d: Z + 1.1, y0: G, y1: F1, n: 4, r: 0.2, depth: 0.5 });
  A.box([-3.4, F1, Z], [3.4, F1 + 0.18, Z + 1.4], "stone_dressed", { bv: 0.02 });
  // side walls: pilasters and a string course (west), a window surround and the balcony (east)
  const west = { axis: "z", d: -X, out: -1 }, east = { axis: "z", d: X, out: 1 };
  for (const a of [-1.0, 1.0]) pilaster(A, west, a, G, F1);
  stringCourse(A, west, -Z + 0.6, Z - 0.6, F1 - 0.6);
  surround(A, east, { a0: -0.45, a1: 0.45, y0: G + 0.9, y1: G + 2.2, key: true });
  fbox(A, east, -0.45, 0.45, G + 0.9, G + 2.2, -0.02, 0.004, "glass", { k: 0 });
  balcony(A, east, { a0: -1.1, a1: 1.1, y: F2 + 0.05, depth: 0.8 });
  // half-timbered upper front, its window glazed proud of the infill
  const win = { a0: -0.6, a1: 0.6, y0: F2 + 0.8, y1: F2 + 2.0 };
  timberFrame(A, fr, { a0: -X, a1: X, y0: F2, y1: EAVE, holes: [win], rails: [F2 + 1.3], sp: 0.9 });
  fbox(A, fr, win.a0, win.a1, win.y0, win.y1, -0.005, 0.012, "glass", { k: 0 });
  fbox(A, fr, -0.03, 0.03, win.y0, win.y1, -0.005, 0.03, "window_paint", { k: 0 });
  // cornice: the long walls own the corners
  for (const [F, a0, a1, ext] of [[fr, -X, X, true], [{ axis: "x", d: -Z, out: -1 }, -X, X, true], [west, -Z, Z, false], [east, -Z, Z, false]]) cornice(A, F, a0, a1, EAVE, { ext });
  const ROOF = EAVE + 0.36;
  hipRoof(A, { X: X + 0.1, Z: Z + 0.1, EAVE: ROOF, RIDGE: ROOF + 2.3, ov: 0.25 });
  dormer(A, { x: 1.6, z: Z + 0.02, y: ROOF + 0.15, w: 1.0, h: 1.0, depth: 1.5 });
  chimney(A, { x: -1.8, z: -0.9, y0: EAVE, y1: ROOF + 3.0, w: 0.7, d: 0.5, m: "brick" });
  // corner turrets: round with a conical spire (back right), octagonal (back left)
  turret(A, { x: X, z: -Z, y0: 0, y1: EAVE + 1.1, r: 1.25, spire: 4.6 });
  turret(A, { x: -X, z: -Z, y0: 0, y1: EAVE + 0.4, r: 0.75, spire: 3.0, sides: 8, m: "stone_dressed" });
  // imperfections: trims jittered and chipped, shell scars on the rendered upper walls (not damage bays: the render
  // block is 4.5 m deep). The plinth stays exact (a collider probe measures its edge).
  weather(A, { keep: [plinth], pocks: [{ F: west, a0: -1.8, a1: 1.8, y0: F2 + 0.4, y1: EAVE - 0.3, n: 4 }, { F: { axis: "x", d: -Z + 0.01, out: -1 }, a0: -2.5, a1: 2.5, y0: F2 + 0.4, y1: EAVE - 0.3, n: 3 }] });
  setNav(A, {
    effect: "block",
    cover: [cover([X + 1.4, 0, 1.8], 270, "high", ["right"]), cover([-X - 1.0, 0, 1.8], 90, "high", ["left"]), cover([0, 0, -Z - 1.6], 0, "high", ["left", "right"])],
  });
  // --selftest-structures collider probes (asset space): o, d, the expected hit distance (or none: clear), tolerance
  const ys = colonnadeSpring, ri = 0.7, cd = Z + 1.1, probe = (note, o, d, hit, tol = 0.03) => ({ note, o, d, hit, tol });
  setTour(A, { pos: [0, 0, 9], yaw: 0 }, [], { probes: [
    probe("round column face (cylinder)", [-1, 1.3, 6], [0, 0, -1], 6 - cd - 0.2),
    probe("round column off-centre: round, not a box", [-1 + 0.17, 1.3, 6], [0, 0, -1], 6 - cd - Math.sqrt(0.04 - 0.17 ** 2), 0.02),
    probe("through the loggia arch and the arched window to the core", [2.1, 1.6, 6], [0, 0, -1], 6 - (Z - 0.3)),
    probe("arch soffit at the crown", [2, 1.5, cd], [0, 1, 0], ys + ri - 1.5),
    probe("arch soffit off-centre: curved", [2.5, 1.5, cd], [0, 1, 0], ys + Math.sqrt(ri * ri - 0.25) - 1.5),
    probe("round turret wall", [X + 3, 3.0, -Z], [-1, 0, 0], 3 - 1.25),
    probe("conical spire", [X, 20, -Z + 0.75], [0, -1, 0], 20 - (EAVE + 1.1 - 0.02 + 4.6 * 0.5), 0.05),
    probe("sloped sill (wedge)", [X + 0.075, 2.0, 0], [0, -1, 0], 2.0 - (G + 0.9 - 0.045 * 0.085 / 0.11), 0.012),
    probe("bevelled plinth keeps a box collider", [3.99, 1.0, 1.0], [0, -1, 0], 1.0 - G, 0.01),
  ] });
  return A;
}

export function kitDemoAssets() { return [kitDemoFolly()]; }
