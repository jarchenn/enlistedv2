// Field ordnance and supply: 7.5 cm PaK 40 station, M2HB on M3 tripod, ammunition crates.
// Proportions follow TM-E 30-451 (1945) ch. VII sec. e for the PaK 40 and the 1944 Standard Ordnance
// Items Catalog for the M2HB; geometry is a game approximation built from boxes.
import { Asset } from "./kit.mjs";
import { stdMaterials } from "./parts.mjs";
import { rod, wheel, handwheel, rivets } from "./defence_detail.mjs";

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

// D1: round parts on the B1 kit (cylinder tube and recuperator, frustum barrel, spoked handwheels, tyred disc
// wheels with lug nuts, riveted spaced shield, bevelled carriage). Pivots, muzzle, optic, crew points and the
// collision proxies are unchanged; registered in tools/build_mpu_assets.mjs (ordnanceAssets).
export function pak40() {
  const A = new Asset({
    id: "gun_pak40", version: 2, name: "7.5 cm PaK 40", category: "Field guns",
    description: "German 7.5 cm anti-tank gun on split-trail carriage (about 1,425 kg / 3,136 lb). Crew station: Aim (gunner, ZF 3x8 sight) or Move (handler). 65 deg traverse, -5 to +22 deg elevation [TM-E 30-451]. Sandbox asset: its presence does not imply a gun at a particular D-Day position.",
    floors: 0, enterable: false, footprint: [2.2, 6.6], plinthDepth: 0.0, maxGrade: 0.6,
  });
  gunMaterials(A);
  const AX = 0.43;   // axle height (wheel radius)
  const Y = { into: [], k: 0 }, E = { into: [], k: 0 }, R = { into: [], k: 0 };
  const X = [1, 0, 0], Zd = [0, 0, 1];
  // ---- lower carriage (static body): axle, tyred disc wheels, box-section split trails with spades
  rod(A, [-0.86, AX, 0], [0.86, AX, 0], 0.1, "gun_paint", { seg: 16 });
  A.box([-0.86, AX - 0.05, -0.06], [0.86, AX + 0.05, 0.06], "gun_paint", { v: 0 });   // the old axle box stays as a collider
  A.box([-0.28, AX - 0.12, -0.2], [0.28, AX + 0.04, 0.2], "gun_paint", { k: 0, bv: 0.025 });   // axle saddle
  for (const s of [-1, 1]) {
    wheel(A, [s * 0.9, AX, 0], X, { dia: 0.86, w: 0.16, out: s, lugs: 6 });
    rod(A, [s * 0.62, AX + 0.02, 0.02], [s * 0.8, AX + 0.02, 0.02], 0.13, "gun_paint", { seg: 14 });   // stub axle housing
    // trail: from the axle saddle splayed back to the spade, sloping to the ground
    const x0 = s * 0.22, z0 = -0.15, x1 = s * 0.98, z1 = -3.05, y0 = AX + 0.02, y1 = 0.16;
    const len = Math.hypot(x1 - x0, z1 - z0, y1 - y0);
    const yaw = (Math.atan2(x1 - x0, z1 - z0) * 180) / Math.PI;
    const pitch = (Math.atan2(y0 - y1, Math.hypot(x1 - x0, z1 - z0)) * 180) / Math.PI;
    A.obox([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], [0.16, 0.18, len], [-pitch, yaw, 0], "gun_paint", { k: 0, bv: 0.025 });
    A.obox([(x0 + x1) / 2, 0.22, (z0 + z1) / 2], [0.26, 0.44, Math.hypot(x1 - x0, z1 - z0)], [0, yaw, 0], "gun_paint", { v: 0 });   // trail-leg collider, vertical sides
    const at = (t, dy = 0) => [x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + dy, z0 + (z1 - z0) * t];
    rod(A, at(0.0, 0.12), at(0.0, -0.12), 0.07, "gun_dark", { seg: 10 });                         // trail hinge pin
    for (const t of [0.35, 0.7]) rod(A, at(t, 0.09), at(t + 0.08, 0.09), 0.03, "gun_dark", { seg: 6 });   // tie-down lugs
    A.obox([x1, 0.16, z1 - 0.07], [0.36, 0.32, 0.035], [-12, yaw, 0], "gun_dark", { k: 0, bv: 0.01 });   // spade plate, raked
    A.wedge([x1, 0.12, z1 + 0.06], [0.06, 0.2, 0.18], "gun_dark", { k: 0, r: [0, yaw + 180, 0] });         // spade gusset
    rod(A, [x1, 0.34, z1 - 0.02], [x1, 0.34, z1 + 0.2], 0.05, "gun_dark", { seg: 10 });                 // handspike socket
    rod(A, at(0.88, 0.1), at(0.88, 0.22), 0.025, "gun_dark", { seg: 6 });                              // lifting handle
    rod(A, at(0.95, 0.1), at(0.95, 0.22), 0.025, "gun_dark", { seg: 6 });
    rod(A, at(0.88, 0.22), at(0.95, 0.22), 0.025, "gun_dark", { seg: 6 });
  }
  // collision proxies for the carriage (vertical-sided, so the player and the sweep test agree)
  A.box([-1.0, 0.0, -0.45], [1.0, 0.9, 0.45], "gun_paint", { v: 0 });

  // ---- upper carriage on the traverse pivot: saddle, riveted spaced shield, handwheels
  const YP = [0, AX + 0.12, 0.05];
  A.box([-0.32, AX + 0.05, -0.35], [0.32, AX + 0.22, 0.45], "gun_paint", { ...Y, bv: 0.02 });
  rod(A, [0, AX + 0.02, 0.05], [0, AX + 0.08, 0.05], 0.46, "gun_dark", { ...Y, seg: 28 });          // traverse ring
  const rk = (-15 * Math.PI) / 180, nrm = [0, -Math.sin(rk), Math.cos(rk)];                         // shield face normal
  for (const s of [-1, 1]) {
    // main shield plates either side of the barrel slot, raked back ~15 deg, plus a spaced front plate
    // sight aperture in the left plates, on the ZF 3x8 line of sight
    const hole = s < 0 ? [0.25, 0.16, 0.08, 0.07] : null;
    plate(A, [s * 0.58, AX + 0.55, 0.42], 0.84, 0.95, 0.012, -15, "gun_paint", Y, hole);
    plate(A, [s * 0.6, AX + 0.53, 0.5], 0.8, 0.85, 0.008, -15, "gun_paint", Y, s < 0 ? [0.27, 0.18, 0.08, 0.07] : null);
    A.box([s * 0.98 - 0.02, AX + 0.12, 0.3], [s * 0.98 + 0.02, AX + 0.95, 0.55], "gun_paint", { ...Y, bv: 0.006 });
    // front plate: rivets along the top and bottom edges, spacer bolts at its corners
    const fp = (u, v) => [s * 0.6 + u, AX + 0.53 + v * Math.cos(rk), 0.5 + v * Math.sin(rk) + 0.004];
    const pts = [];
    for (let i = 0; i < 6; i++) { const u = -0.34 + (0.68 * i) / 5; pts.push(fp(u, 0.39), fp(u, -0.39)); }
    rivets(A, pts, nrm, "gun_paint", Y);
    for (const [u, v] of [[-0.33, 0.33], [0.33, 0.33], [-0.33, -0.33], [0.33, -0.33]]) {
      const p = fp(u, v), q = [s * 0.58 + u, AX + 0.55 + v * Math.cos(rk), 0.42 + v * Math.sin(rk)];
      rod(A, q, p, 0.03, "gun_dark", { ...Y, seg: 6 });
    }
  }
  A.obox([0, AX + 0.98, 0.36], [0.26, 0.2, 0.012], [-15, 0, 0], "gun_paint", Y);          // over-barrel plate
  A.box([-0.98, AX - 0.2, 0.42], [0.98, AX + 0.08, 0.45], "gun_paint", { ...Y, bv: 0.006 });   // lower apron
  handwheel(A, [-0.42, AX + 0.42, -0.25], X, 0.22, "gun_dark", Y);                       // elevation handwheel
  handwheel(A, [-0.3, AX + 0.3, -0.42], Zd, 0.2, "gun_dark", Y);                         // traverse handwheel
  rod(A, [-0.3, AX + 0.3, -0.4], [-0.3, AX + 0.12, -0.3], 0.04, "gun_dark", { ...Y, seg: 8 });   // traverse gear shaft
  for (const s of [-1, 1]) rod(A, [s * 0.27, AX + 0.18, -0.1], [s * 0.27, AX + 0.46, 0.2], 0.07, "gun_paint", { ...Y, seg: 12 });   // equilibrator springs
  // ---- cradle on the trunnions (elevation)
  const EP = [0, AX + 0.52, 0.12];
  A.box([-0.15, AX + 0.36, -0.55], [0.15, AX + 0.47, 1.05], "gun_paint", { ...E, bv: 0.02 });   // cradle
  rod(A, [0, AX + 0.52, -0.05], [0, AX + 0.52, 1.05], 0.16, "gun_paint", { ...E, seg: 20 });       // barrel sleeve
  for (const s of [-1, 1]) rod(A, [s * 0.15, AX + 0.52, 0.12], [s * 0.2, AX + 0.52, 0.12], 0.09, "gun_dark", { ...E, seg: 12 });   // trunnions
  rod(A, [0, AX + 0.69, -0.45], [0, AX + 0.69, 0.85], 0.13, "gun_paint", { ...E, seg: 20 });       // recuperator above
  rod(A, [0, AX + 0.69, 0.85], [0, AX + 0.69, 0.9], 0.1, "gun_dark", { ...E, seg: 14 });
  for (const z of [-0.2, 0.55]) A.box([-0.05, AX + 0.56, z - 0.03], [0.05, AX + 0.64, z + 0.03], "gun_paint", { ...E, bv: 0.01 });   // recuperator clamps
  rod(A, [0.11, AX + 0.4, -0.4], [0.11, AX + 0.4, 0.7], 0.1, "gun_paint", { ...E, seg: 16 });      // recoil buffer
  rod(A, [0.11, AX + 0.4, 0.7], [0.11, AX + 0.4, 0.74], 0.075, "gun_dark", { ...E, seg: 12 });
  // ZF 3x8 sight on the left of the cradle (eyepiece faces the gunner); nothing ahead of the objective (the optic eye)
  A.box([-0.27, AX + 0.6, -0.15], [-0.19, AX + 0.66, 0.05], "gun_dark", { ...E, bv: 0.008 });
  rod(A, [-0.33, AX + 0.71, -0.38], [-0.33, AX + 0.71, 0.04], 0.07, "gun_dark", { ...E, seg: 14 });
  rod(A, [-0.33, AX + 0.71, -0.04], [-0.33, AX + 0.71, 0.04], 0.085, "gun_dark", { ...E, seg: 14 });   // objective bell
  rod(A, [-0.33, AX + 0.71, -0.405], [-0.33, AX + 0.71, -0.375], 0.1, "rubber", { ...E, seg: 14 });  // eyecup
  rod(A, [-0.33, AX + 0.75, -0.2], [-0.33, AX + 0.79, -0.2], 0.04, "gun_dark", { ...E, seg: 8 });    // range drum
  // ---- recoiling parts: tapered tube, collar, double-baffle muzzle brake, breech ring and sliding block
  rod(A, [0, AX + 0.52, -0.32], [0, AX + 0.52, 2.7], 0.13, "gun_paint", { ...R, top: 0.68, seg: 20 });
  rod(A, [0, AX + 0.52, 2.62], [0, AX + 0.52, 2.8], 0.11, "gun_paint", { ...R, seg: 18 });          // muzzle collar
  rod(A, [0, AX + 0.52, 2.78], [0, AX + 0.52, 3.12], 0.1, "gun_dark", { ...R, seg: 16 });           // brake core
  for (const z of [2.86, 3.02]) A.box([-0.115, AX + 0.43, z], [0.115, AX + 0.61, z + 0.09], "gun_dark", { ...R, bv: 0.025 });   // baffles
  A.box([-0.08, AX + 0.45, 2.8], [0.08, AX + 0.59, 3.12], "gun_dark", { ...R, bv: 0.02 });          // brake side cheeks
  A.box([-0.17, AX + 0.36, -0.82], [0.17, AX + 0.68, -0.4], "gun_dark", { ...R, bv: 0.03 });        // breech ring
  A.box([-0.12, AX + 0.4, -0.84], [0.12, AX + 0.64, -0.78], "gun_paint", { ...R, bv: 0.01 });       // breech block face
  rod(A, [0.17, AX + 0.6, -0.62], [0.27, AX + 0.6, -0.62], 0.04, "gun_dark", { ...R, seg: 8 });     // block lever
  rod(A, [0.27, AX + 0.6, -0.62], [0.27, AX + 0.47, -0.7], 0.03, "gun_dark", { ...R, seg: 8 });
  rod(A, [0.25, AX + 0.47, -0.7], [0.3, AX + 0.47, -0.7], 0.04, "rubber", { ...R, seg: 8 });
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
    reserveAT: 4, ammoAT: "75mm_ap",
  };
  A.meta.faction = "DE";
  A.meta.nav = { effect: "carve", cover: [{ p: [0, 0, -1.2], yaw: 0, height: "low", peek: ["left", "right"] }] };
  return A;
}
// D1: the field ordnance written by tools/build_mpu_assets.mjs (gun_m2hb_tripod is drawn from the supplied M2 GLB;
// it stays with build_assets.mjs)
export function ordnanceAssets() { return [pak40()]; }

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
