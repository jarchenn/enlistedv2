// MOD2: mod_mil_hut, a modern military hut (Quonset-style arch) from the user-supplied model
// "Military-style building with animations" by TampaJoey (sketchfab.com/TampaJoey), licence not stated in the
// download: the bake is local-only (git-ignored, content/assets/models/mod_mil_hut*); the committed asset carries a
// part-built stand-in of the same footprint ("mf": 1 parts, hidden by the engine once the bake is on disk).
// The model is visual only. Around it: arch colliders that follow the roof, the end walls with their openings, a floor
// slab for room detection (3 cm under the model's floor), the front door (inner end of the entrance vestibule) and the
// garage door at the back as engine door interactables (the FBX's armature clips are not used: the engine has no
// skinned map props), window apertures (keep-clear for the furnisher, FN1), nav rooms, cover and firing points.
// Model space (Blender: x right, y back, z up; front at -y) -> asset space (x, y up, z front): E = [x, z + 0.094, -y - 1.494]
// (the bake's ground pivot centres the bounding box, the apron included).
import { Asset } from "./kit.mjs";
import { setNav, room, cover, fire } from "./catalog_atlantic.mjs";

const E = (x, y, z) => [x, z + 0.094, -y - 1.494];
const FLOOR = 0.25;                                   // the slab's top (the model's floor is at 0.275)
// inside arch profile (model x, z), from the ceiling mesh
const ARCH = [[5.63, 0.26], [5.55, 0.87], [5.34, 1.73], [5.0, 2.54], [4.54, 3.29], [3.97, 3.96], [3.3, 4.54], [2.55, 5.0], [1.74, 5.33], [0.88, 5.54], [0, 5.61]];
const ZB = E(0, 7.3, 0)[2], ZF = E(0, -8.36, 0)[2];   // inside faces of the back / front end walls
const ZBO = E(0, 7.72, 0)[2], ZFO = E(0, -8.66, 0)[2];

export function mod2Assets() {
  const A = new Asset({ id: "mod_mil_hut", version: 1, name: "Military hut (Quonset)", category: "Modern",
    description: "Prefabricated military hut with a corrugated arched roof, 11.8 x 16.5 m on a concrete apron: an entrance vestibule with a steel door, front windows behind blast shutters, a garage door at the back, fluorescent lights, cameras and bollards. One open hall inside.",
    footprint: [11.76, 19.58], floors: 1, enterable: true, plinthDepth: 0, maxGrade: 2, noAutoPanels: true });
  A.mat("concrete_floor", [0.52, 0.51, 0.49], 0.9, "concrete", { texture: "concrete_grey_cast" });
  A.mat("hut_wall", [0.46, 0.47, 0.42], 0.85, "concrete", { texture: "concrete_stained" });
  A.mat("hut_roof", [0.34, 0.37, 0.31], 0.6, "metal", { texture: "corrugated_grey", metallic: 0.5 });
  A.mat("door_steel", [0.22, 0.25, 0.2], 0.55, "metal", { texture: "metal_plates_dark", metallic: 0.4 });
  A.mat("door_trim", [0.15, 0.15, 0.15], 0.5, "metal", { metallic: 0.5 });
  A.mat("glass", [0.25, 0.3, 0.32], 0.1, "glass");
  A.mat("bollard", [0.75, 0.6, 0.1], 0.6, "metal");
  const MF = { mf: 1 };   // stand-in look, hidden when the bake exists (still collides)
  const P = (p) => { p.mf = 1; return p; };
  // floor slab (visible: rooms come from it) and the apron in front
  A.box([-5.62, FLOOR - 0.25, ZB], [5.62, FLOOR, ZF], "concrete_floor");
  P(A.box([-5.87, 0, ZFO], [5.87, 0.18, 9.79], "concrete_floor", { bv: 0.03 }));
  // arch: one slab per profile segment, both sides, the length of the hall
  const zc = (ZB + ZF) / 2, zl = ZF - ZB + 0.6;
  for (let i = 0; i + 1 < ARCH.length; ++i) {
    const [x0, y0] = ARCH[i], [x1, y1] = ARCH[i + 1];
    const mx = (x0 + x1) / 2, my = (y0 + y1) / 2 + 0.094, len = Math.hypot(x1 - x0, y1 - y0) + 0.06, ang = Math.atan2(y1 - y0, x1 - x0) * 180 / Math.PI;
    const nxo = (y1 - y0) / len, nyo = -(x1 - x0) / len;   // outward normal of the right-hand segment
    for (const s of [1, -1]) P(A.obox([s * (mx + nxo * 0.09), my + nyo * 0.09, zc], [len, 0.16, zl], [0, 0, s > 0 ? ang : -ang], "hut_roof"));
  }
  // end walls: front (vestibule opening x +-1.63 to 3.57 m, windows 1.9-3.85 from 0.94 to 2.61) and back (garage x -1.75..1.70 to 3.61)
  const wallZ = (z0, z1, x0, x1, y0, y1) => P(A.box([x0, y0, z0], [x1, y1, z1], "hut_wall"));
  const top = (x) => { let h = 0.26; for (const [ax, ay] of ARCH) if (Math.abs(x) <= ax + 1e-6) h = Math.max(h, ay); return h + 0.094; };
  const strip = (z0, z1, x0, x1, y0) => { for (let x = x0; x < x1 - 1e-3; x += 0.5) { const xe = Math.min(x1, x + 0.5); wallZ(z0, z1, x, xe, y0, Math.min(top(x), top(xe))); } };
  // front wall
  strip(ZF, ZFO, -5.6, -3.85, FLOOR); strip(ZF, ZFO, 3.85, 5.6, FLOOR);
  strip(ZF, ZFO, -1.9, -1.63, FLOOR); strip(ZF, ZFO, 1.63, 1.9, FLOOR);
  for (const s of [-1, 1]) { const a = s < 0 ? -3.85 : 1.9, b = s < 0 ? -1.9 : 3.85; wallZ(ZF, ZFO, a, b, FLOOR, 0.94); strip(ZF, ZFO, a, b, 2.61); }
  strip(ZF, ZFO, -1.63, 1.63, 3.57);
  // vestibule: side walls, the inner wall with the door (x +-0.62 to 2.41), a ceiling
  const ZV = E(0, -7.09, 0)[2];
  for (const s of [-1, 1]) wallZ(ZV, ZF, s * 1.63 - 0.05, s * 1.63 + 0.05, FLOOR, 3.57);
  wallZ(ZV - 0.08, ZV, -1.63, -0.62, FLOOR, 3.57); wallZ(ZV - 0.08, ZV, 0.62, 1.63, FLOOR, 3.57); wallZ(ZV - 0.08, ZV, -0.62, 0.62, 2.41, 3.57);
  P(A.box([-1.63, 3.57, ZV - 0.08], [1.63, 3.67, ZF], "hut_wall"));
  // back wall
  strip(ZBO, ZB, -5.6, -1.75, FLOOR); strip(ZBO, ZB, 1.7, 5.6, FLOOR); strip(ZBO, ZB, -1.75, 1.7, 3.61);
  // bollards (the model's yellow posts)
  for (const [x, y] of [[1.8, -10.99], [-1.79, -10.99], [1.8, -9.56], [-1.79, -9.56], [0, -10.99], [-4.02, 6.67], [-1.93, 6.67], [3.99, 6.67], [1.94, 6.67], [1.42, -6.72], [-1.46, -6.72]]) {
    const c = E(x, y, 0.5); A.cyl([c[0], 0.55, c[2]], [0.21, 1.1, 0.21], "bollard", { n: 8, mf: 1 }); A.parts[A.parts.length - 1].mf = 1;
  }
  // stand-in front glazing (the model has its own glass and frames)
  for (const s of [-1, 1]) P(A.box([s < 0 ? -3.85 : 1.9, 0.94, ZF + 0.12], [s < 0 ? -1.9 : 3.85, 2.61, ZF + 0.14], "glass", { k: 0 }));
  // the doors: steel front door (hinged at -x, opens into the hall) and the garage's up-and-over door
  const dz = ZV - 0.04;
  A.interactables.push({ id: "front_door", kind: "door", label: "door", sound: "steel", axis: [0, 1, 0], openAngle: 95, duration: 1.1, defaultOpen: false,
    leaves: [{ parts: [{ t: "b", c: [0, 1.33, dz], s: [1.22, 2.14, 0.05], m: "door_steel", k: 0, bv: 0.008 }, { t: "b", c: [0.46, 1.25, dz + 0.05], s: [0.03, 0.18, 0.05], m: "door_trim", k: 0 },
      { t: "b", c: [0, 0.4, dz + 0.03], s: [1.0, 0.26, 0.01], m: "door_trim", k: 0 }, { t: "b", c: [0, 1.33, dz], s: [1.22, 2.14, 0.05], m: "door_steel", v: 0 }], hinge: [-0.61, 0, dz], sign: -1 }],
    aperture: { min: [-0.62, FLOOR, ZV - 0.1], max: [0.62, 2.41, ZV + 0.02] } });
  const gz = ZB + 0.12;
  A.interactables.push({ id: "garage_door", kind: "door", label: "garage door", sound: "steel", axis: [1, 0, 0], openAngle: 88, duration: 2.4, defaultOpen: false,
    leaves: [{ parts: [{ t: "b", c: [-0.02, 1.93, gz], s: [3.42, 3.36, 0.06], m: "door_steel", k: 0 }, ...[0, 1, 2, 3, 4, 5, 6].map((i) => ({ t: "b", c: [-0.02, 0.5 + i * 0.48, gz + 0.04], s: [3.38, 0.04, 0.02], m: "door_trim", k: 0 })),
      { t: "b", c: [-0.02, 1.93, gz], s: [3.42, 3.36, 0.06], m: "door_steel", v: 0 }], hinge: [0, 3.61, gz], sign: -1 }],
    aperture: { min: [-1.75, FLOOR, ZBO], max: [1.7, 3.61, ZB + 0.2] } });
  // window apertures (fixed glazing in the model: keep-clear for furniture and the doorway rules, not openable)
  for (const s of [-1, 1]) A.interactables.push({ id: s < 0 ? "window_l" : "window_r", kind: "window", label: "window", sound: "glass", axis: [0, 1, 0], openAngle: 0, duration: 0.5, defaultOpen: false,
    leaves: [{ parts: [{ t: "b", c: [s * 2.875, 1.78, ZF + 0.13], s: [1.95, 1.67, 0.02], m: "glass", v: 0, k: 0 }], hinge: [s * 3.85, 0, ZF + 0.13], sign: 1 }],
    aperture: { min: [s < 0 ? -3.85 : 1.9, 0.94, ZF], max: [s < 0 ? -1.9 : 3.85, 2.61, ZFO] } });
  A.meta.roomTypes = { hall: "mil_quarters", vestibule: "hall" };
  A.meta.model = { src: "military_hut.glb", dir: "mod_mil_hut", pivot: "ground", scale: 1, maxTex: 1024, simplify: 0.5, lodDist: 60, cullDist: 600,
    credit: "Military-style building with animations by TampaJoey (sketchfab.com/TampaJoey), licence not stated: local only" };
  A.meta.tour = { start: { pos: [0, 0, 9.2] }, steps: [{ goto: [0, 7.6] }, { goto: [0, 5.2] }, { goto: [0, 0] }, { goto: [0, -7.5] }] };
  setNav(A, { effect: "block",
    rooms: [room("hall", [-5.3, FLOOR, ZB + 0.1], [5.3, 5.2, ZV - 0.1]), room("vestibule", [-1.55, FLOOR, ZV + 0.02], [1.55, 3.5, ZF - 0.05])],
    doors: [{ a: "hall", b: "vestibule", p: [0, FLOOR, ZV - 0.04] }, { a: "vestibule", b: "outside", p: [0, FLOOR, ZFO + 0.1] }, { a: "hall", b: "outside", p: [0, FLOOR, ZBO - 0.1] }],
    entrances: [{ p: [0, 0, 8.6], dir: [0, 0, -1], room: "vestibule" }, { p: [0, 0, -9.7], dir: [0, 0, 1], room: "hall" }],
    corners: [{ p: [-4.8, FLOOR, ZB + 0.6], room: "hall" }, { p: [4.8, FLOOR, ZB + 0.6], room: "hall" }, { p: [-4.8, FLOOR, ZV - 0.6], room: "hall" }, { p: [4.8, FLOOR, ZV - 0.6], room: "hall" }],
    firingPoints: [fire([-2.875, FLOOR, ZF - 0.5], 0, "window"), fire([2.875, FLOOR, ZF - 0.5], 0, "window")],
    cover: [cover([-6.4, 0, 6.0], 0, "high", ["left"]), cover([6.4, 0, 6.0], 0, "high", ["right"]), cover([-6.4, 0, -8.0], 180, "high", ["right"]), cover([6.4, 0, -8.0], 180, "high", ["left"]),
      cover([1.8, 0, 9.0], 0, "low", ["over"]), cover([-1.8, 0, 9.0], 0, "low", ["over"])],
    areas: [] });
  return [A];
}
