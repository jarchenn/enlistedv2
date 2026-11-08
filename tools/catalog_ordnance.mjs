// Field ordnance and supply: 7.5 cm PaK 40 station, M2HB on M3 tripod, ammunition crates.
// Proportions follow TM-E 30-451 (1945) ch. VII sec. e for the PaK 40 and the 1944 Standard Ordnance
// Items Catalog for the M2HB; geometry is a game approximation built from boxes.
import { Asset } from "./kit.mjs";
import { stdMaterials } from "./parts.mjs";

function gunMaterials(A) {
  stdMaterials(A);
  A.mat("gun_paint", [0.34, 0.28, 0.15], 0.62, "metal", { metallic: 0.0 });   // dunkelgelb paint (dielectric)
  A.mat("gun_dark", [0.08, 0.08, 0.075], 0.55, "metal", { metallic: 0.15 });
  A.mat("rubber", [0.02, 0.02, 0.02], 0.9, "plain");
  A.mat("od_paint", [0.13, 0.14, 0.075], 0.62, "metal", { metallic: 0.0 });       // US olive drab paint
  A.mat("stencil", [0.6, 0.58, 0.5], 0.8, "plain");
}
// octagonal prism along an axis ("x" or "z") — wheels, tubes
function oct(A, axis, c, radius, len, m, o = {}) {
  // a regular octagon is the union of four bars (length 2r, width 2r*tan 22.5) at 45 deg steps
  const L = 2 * radius, Wd = 2 * radius * Math.tan(Math.PI / 8);
  for (const a of [0, 45, 90, 135]) {
    if (axis === "x") A.obox(c, [len, L, Wd], [a, 0, 0], m, o);
    else if (axis === "z") A.obox(c, [L, Wd, len], [0, 0, a], m, o);
    else A.obox(c, [L, len, Wd], [0, a, 0], m, o);
  }
}
// raked shield plate with an optional rectangular aperture (plate-local u,v centre and half sizes)
function plate(A, c, w, h, t, rakeDeg, m, o, hole) {
  const r = (rakeDeg * Math.PI) / 180;
  const at = (u, v) => [c[0] + u, c[1] + v * Math.cos(r), c[2] + v * Math.sin(r)];
  const rects = [];
  if (!hole) rects.push([-w / 2, w / 2, -h / 2, h / 2]);
  else {
    const [hu, hv, hw, hh] = hole;
    rects.push([-w / 2, w / 2, -h / 2, hv - hh], [-w / 2, w / 2, hv + hh, h / 2], [-w / 2, hu - hw, hv - hh, hv + hh], [hu + hw, w / 2, hv - hh, hv + hh]);
  }
  for (const [u0, u1, v0, v1] of rects) if (u1 - u0 > 0.005 && v1 - v0 > 0.005) A.obox(at((u0 + u1) / 2, (v0 + v1) / 2), [u1 - u0, v1 - v0, t], [rakeDeg, 0, 0], m, o);
}

export function pak40() {
  const A = new Asset({
    id: "gun_pak40", version: 1, name: "7.5 cm PaK 40", category: "Field guns",
    description: "German 7.5 cm anti-tank gun on split-trail carriage (about 1,425 kg / 3,136 lb). Crew station: Aim (gunner, ZF 3x8 sight) or Move (handler). 65 deg traverse, -5 to +22 deg elevation [TM-E 30-451]. Sandbox asset: its presence does not imply a gun at a particular D-Day position.",
    floors: 0, enterable: false, footprint: [2.2, 6.6], plinthDepth: 0.0, maxGrade: 0.6,
  });
  gunMaterials(A);
  const AX = 0.43;   // axle height (wheel radius)
  const Y = { into: [], k: 0 }, E = { into: [], k: 0 }, R = { into: [], k: 0 };
  // ---- lower carriage (static body): axle, disc wheels, split trails with spades
  A.box([-0.86, AX - 0.05, -0.06], [0.86, AX + 0.05, 0.06], "gun_paint");
  for (const s of [-1, 1]) {
    oct(A, "x", [s * 0.9, AX, 0], 0.43, 0.16, "rubber", { k: 0 });
    oct(A, "x", [s * 0.9, AX, 0], 0.34, 0.18, "gun_paint", { k: 0 });
    oct(A, "x", [s * 0.99, AX, 0], 0.09, 0.06, "gun_dark", { k: 0 });
    // trail: from the axle saddle splayed back to the spade, sloping to the ground
    const x0 = s * 0.22, z0 = -0.15, x1 = s * 0.98, z1 = -3.05, y0 = AX + 0.02, y1 = 0.16;
    const len = Math.hypot(x1 - x0, z1 - z0, y1 - y0);
    const yaw = (Math.atan2(x1 - x0, z1 - z0) * 180) / Math.PI;
    const pitch = (Math.atan2(y0 - y1, Math.hypot(x1 - x0, z1 - z0)) * 180) / Math.PI;
    A.obox([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], [0.16, 0.18, len], [-pitch, yaw, 0], "gun_paint", { k: 0 });
    A.obox([(x0 + x1) / 2, 0.22, (z0 + z1) / 2], [0.26, 0.44, Math.hypot(x1 - x0, z1 - z0)], [0, yaw, 0], "gun_paint", { v: 0 });   // trail-leg collider, vertical sides
    A.box([x1 - 0.18, 0.0, z1 - 0.16], [x1 + 0.18, 0.32, z1 + 0.02], "gun_dark", { k: 0 });       // spade
    A.box([x1 - 0.05, 0.3, z1 - 0.06], [x1 + 0.05, 0.38, z1 + 0.18], "gun_dark", { k: 0 });       // handspike socket
  }
  // collision proxies for the carriage (vertical-sided, so the player and the sweep test agree)
  A.box([-1.0, 0.0, -0.45], [1.0, 0.9, 0.45], "gun_paint", { v: 0 });

  // ---- upper carriage on the traverse pivot: saddle, spaced shield, handwheels
  const YP = [0, AX + 0.12, 0.05];
  A.box([-0.32, AX + 0.05, -0.35], [0.32, AX + 0.22, 0.45], "gun_paint", Y);
  for (const s of [-1, 1]) {
    // main shield plates either side of the barrel slot, raked back ~15 deg, plus a spaced front plate
    // sight aperture in the left plates, on the ZF 3x8 line of sight
    const hole = s < 0 ? [0.25, 0.16, 0.08, 0.07] : null;
    plate(A, [s * 0.58, AX + 0.55, 0.42], 0.84, 0.95, 0.012, -15, "gun_paint", Y, hole);
    plate(A, [s * 0.6, AX + 0.53, 0.5], 0.8, 0.85, 0.008, -15, "gun_paint", Y, s < 0 ? [0.27, 0.18, 0.08, 0.07] : null);
    A.box([s * 0.98 - 0.02, AX + 0.12, 0.3], [s * 0.98 + 0.02, AX + 0.95, 0.55], "gun_paint", Y);
  }
  A.obox([0, AX + 0.98, 0.36], [0.26, 0.2, 0.012], [-15, 0, 0], "gun_paint", Y);          // over-barrel plate
  A.box([-0.98, AX - 0.2, 0.42], [0.98, AX + 0.08, 0.45], "gun_paint", Y);               // lower apron
  oct(A, "x", [-0.42, AX + 0.42, -0.25], 0.11, 0.03, "gun_dark", Y);                     // elevation handwheel
  oct(A, "z", [-0.3, AX + 0.3, -0.42], 0.1, 0.03, "gun_dark", Y);                        // traverse handwheel
  // ---- cradle on the trunnions (elevation)
  const EP = [0, AX + 0.52, 0.12];
  A.box([-0.15, AX + 0.36, -0.55], [0.15, AX + 0.47, 1.05], "gun_paint", E);             // cradle
  oct(A, "z", [0, AX + 0.69, 0.2], 0.06, 1.3, "gun_paint", E);                           // recuperator above
  oct(A, "z", [0.11, AX + 0.4, 0.15], 0.05, 1.1, "gun_paint", E);                        // recoil buffer
  // ZF 3x8 sight on the left of the cradle (eyepiece faces the gunner)
  A.box([-0.27, AX + 0.6, -0.15], [-0.19, AX + 0.66, 0.05], "gun_dark", E);
  oct(A, "z", [-0.33, AX + 0.71, -0.17], 0.035, 0.42, "gun_dark", E);
  oct(A, "z", [-0.33, AX + 0.71, -0.39], 0.05, 0.03, "rubber", E);
  // ---- recoiling parts: tube, double-baffle muzzle brake, breech ring and sliding block
  oct(A, "z", [0, AX + 0.52, 1.18], 0.055, 3.0, "gun_paint", R);
  oct(A, "z", [0, AX + 0.52, 2.75], 0.07, 0.25, "gun_paint", R);
  A.box([-0.11, AX + 0.43, 2.88], [0.11, AX + 0.61, 3.12], "gun_dark", R);
  for (const z of [2.92, 3.0, 3.08]) A.box([-0.115, AX + 0.425, z], [0.115, AX + 0.615, z + 0.025], "gun_paint", R);
  A.box([-0.17, AX + 0.36, -0.82], [0.17, AX + 0.68, -0.4], "gun_dark", R);
  A.box([0.17, AX + 0.45, -0.75], [0.2, AX + 0.58, -0.48], "gun_dark", R);
  A.walkthrough = undefined;
  const rel = (parts, p) => parts.map((q) => ({ ...q, c: [q.c[0] - p[0], q.c[1] - p[1], q.c[2] - p[2]] }));
  A.meta.station = {
    kind: "pak40", yawPivot: YP, elevPivot: EP,
    muzzle: [0, AX + 0.52, 3.13], optic: [-0.33, AX + 0.71, 0.07],   // eye at the objective: the view passes the shield aperture
    gunner: [-1.05, 0, -0.95], handler: [0, 0, -3.6],
    traverse: [-32.5, 32.5], elevation: [-5, 22], traverseRate: 14, elevateRate: 8,
    recoil: 0.6, reloadTime: 4.0, loaded: 1, reserve: 5, ammo: "75mm_he",
    moveHalf: [1.0, 0.5, 1.6], serviceRadius: 6,
    note: "Symmetrical -32.5/+32.5 deg split of the 65 deg total traverse is an implementation assumption.",
    yawParts: rel(Y.into, YP), elevParts: rel(E.into, EP), recoilParts: rel(R.into, EP),
  };
  return A;
}

export function m2hb() {
  const A = new Asset({
    id: "gun_m2hb_tripod", version: 1, name: "Browning M2HB on M3 tripod", category: "Field guns",
    description: "Cal. .50 heavy-barrel machine gun on the M3 tripod, flexible ground mount. Operated from the spade grips; two 110-round link increments at the emplacement [1944 Standard Ordnance Items Catalog].",
    floors: 0, enterable: false, footprint: [1.4, 1.9], plinthDepth: 0.0, maxGrade: 0.5,
  });
  gunMaterials(A);
  const Y = { into: [], k: 0 }, E = { into: [], k: 0 }, R = { into: [], k: 0 };
  const H = 0.62;
  // M3 tripod: front leg + two rear legs (low mount)
  A.obox([0, H / 2, 0.45], [0.05, 0.05, 1.0], [-38, 0, 0], "od_paint", { k: 0 });
  for (const s of [-1, 1]) A.obox([s * 0.35, H / 2, -0.35], [0.05, 0.05, 1.0], [32, s * -35, 0], "od_paint", { k: 0 });
  A.box([-0.4, 0.0, -0.75], [0.4, 0.04, -0.6], "od_paint", { k: 0 });
  A.box([-0.45, 0, -0.8], [0.45, 0.6, 0.9], "od_paint", { v: 0 });
  const YP = [0, H, 0];
  A.box([-0.05, H - 0.05, -0.08], [0.05, H + 0.06, 0.08], "od_paint", Y);   // pintle / cradle
  const EP = [0, H + 0.12, 0];
  // receiver, heavy barrel with perforated jacket, feed cover, spade grips, sights, belt box
  A.box([-0.08, H + 0.04, -0.62], [0.08, H + 0.22, 0.25], "od_paint", R);
  oct(A, "z", [0, H + 0.12, 0.85], 0.032, 1.15, "gun_dark", R);
  oct(A, "z", [0, H + 0.12, 0.38], 0.05, 0.25, "gun_dark", R);
  A.box([-0.075, H + 0.22, -0.5], [0.075, H + 0.25, 0.15], "od_paint", R);
  for (const s of [-1, 1]) A.box([s * 0.07 - 0.015, H + 0.05, -0.75], [s * 0.07 + 0.015, H + 0.2, -0.62], "rubber", R);
  A.box([-0.01, H + 0.22, -0.32], [0.01, H + 0.34, -0.29], "gun_dark", R);   // rear leaf sight
  A.box([-0.006, H + 0.22, 0.2], [0.006, H + 0.3, 0.22], "gun_dark", R);    // front sight
  A.box([0.1, H - 0.08, -0.35], [0.26, H + 0.12, -0.05], "od_paint", E);   // ammunition box
  const rel = (parts, p) => parts.map((q) => ({ ...q, c: [q.c[0] - p[0], q.c[1] - p[1], q.c[2] - p[2]] }));
  A.meta.station = {
    kind: "m2hb", yawPivot: YP, elevPivot: EP, muzzle: [0, H + 0.12, 1.43], optic: [0, H + 0.3, -0.85],
    gunner: [0, 0, -1.25], handler: [0, 0, -1.25], traverse: [-60, 60], elevation: [-15, 25], traverseRate: 120, elevateRate: 90,
    recoil: 0.02, reloadTime: 5.5, loaded: 110, reserve: 110, ammo: "50bmg_belt", moveHalf: [0.45, 0.35, 0.8], serviceRadius: 5,
    yawParts: rel(Y.into, YP), elevParts: rel([...E.into, ...R.into], EP), recoilParts: [],
  };
  return A;
}

// crate_pak40_he / crate_mixed_supply are generated by tools/catalog_props2.mjs since P1 (version 2); this legacy
// builder (build_assets.mjs only) is kept for reference.
function crate(id, name, desc, supplies, colour) {
  const A = new Asset({ id, version: 1, name, category: "Supply", description: desc, floors: 0, enterable: false, footprint: [1.1, 0.7], plinthDepth: 0, maxGrade: 0.4 });
  gunMaterials(A);
  A.box([-0.5, 0.0, -0.3], [0.5, 0.06, 0.3], "oak_x");
  A.box([-0.48, 0.06, -0.28], [0.48, 0.52, 0.28], colour);
  for (const x of [-0.42, 0.42]) A.box([x - 0.04, 0.06, -0.29], [x + 0.04, 0.53, 0.29], colour);
  A.box([-0.49, 0.52, -0.29], [0.49, 0.56, 0.29], colour);
  for (const x of [-0.25, 0.25]) A.box([x - 0.05, 0.53, -0.3], [x + 0.05, 0.57, 0.3], "iron");
  A.box([-0.2, 0.25, 0.281], [0.2, 0.4, 0.29], "stencil", { k: 0 });
  for (const s of [-1, 1]) A.box([s * 0.49 - 0.02, 0.3, -0.08], [s * 0.49 + 0.02, 0.34, 0.08], "iron", { k: 0 });
  A.meta.crate = { supplies, unlimited: false, serviceRadius: 6 };
  return A;
}

export function crates() {
  return [
    crate("crate_pak40_he", "PaK 40 ammunition crate", "Wooden crate of 7.5 cm HE rounds for a PaK 40 within 6 m. Finite stock by default (editable).", { "75mm_he": 12 }, "crate_olive"),
    crate("crate_mixed_supply", "Mixed supply crate", "Sandbox range crate: rifle clips, magazines, belts, .50 links and 7.5 cm HE. Feeds personal reserve or a nearby emplacement; finite by default.",
      { "30-06_clip": 30, "792_clip": 30, "9mm_mag32": 12, "45_mag20": 12, "792_belt50": 10, "50bmg_belt": 6, "75mm_he": 10 }, "oak_x"),
  ];
}
