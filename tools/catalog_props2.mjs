// P1 (improve all models): supply crates ported out of the legacy tools/build_assets.mjs (catalog_ordnance.mjs `crates`)
// and the hand-written rack_allied_crates.json, rebuilt with the B1 kit. Ids, footprints, colliders and the `crate` /
// `rack` blocks are kept exactly (the supplies are the hand-tuned values the JSON carried, not catalog_ordnance's
// originals); only the look changed. Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
import { Asset } from "./kit.mjs";

const BV = 0.008;   // timber edge chamfer (mesh only)
// rope loop handle on an end face at x (outward sign sx): two cleats and a sagging rope arch between them
function ropeHandle(A, x, sx, y, z, m, rope, w = 0.16) {
  for (const s of [-1, 1]) A.box([Math.min(x, x + sx * 0.025), y - 0.02, z + s * w / 2 - 0.02], [Math.max(x, x + sx * 0.025), y + 0.02, z + s * w / 2 + 0.02], m, { k: 0, bv: 0.004 });
  A.arch([x + sx * 0.018, y - 0.045, z], [w, 0.05, 0.016], rope, { r: [180, 90, 0], th: 0.016, k: 0 });
}
function nails(A, xs, y, zf, m) { for (const x of xs) A.cyl([x, y, zf + 0.0015], [0.009, 0.003, 0.009], m, { seg: 6, r: [90, 0, 0], k: 0 }); }

// German-style ammunition crate on a skid: boarded sides, end battens, a lid with iron straps carried down the faces,
// two hasps and staples, rope handles, corner plates and a stencil plate. 1.0 x 0.6 m on the skid, 0.57 m high.
function crate(id, name, desc, supplies, colour) {
  const A = new Asset({ id, version: 2, name, category: "Supply", description: desc, floors: 0, enterable: false, footprint: [1.1, 0.7], plinthDepth: 0, maxGrade: 0.4 });
  A.mat("oak_x", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 0 });
  A.mat("iron", [0.05, 0.048, 0.046], 0.55, "metal", { metallic: 0.15 });
  A.mat("crate_olive", [0.11, 0.12, 0.06], 0.7, "painted", { grain: 0 });
  A.mat("stencil", [0.6, 0.58, 0.5], 0.8, "plain");
  A.mat("rope", [0.5, 0.44, 0.32], 0.95, "fabric");
  // the colliders of version 1, now chamfered (bv is mesh only)
  A.box([-0.5, 0.0, -0.3], [0.5, 0.06, 0.3], "oak_x", { bv: 0.01 });
  A.box([-0.48, 0.06, -0.28], [0.48, 0.52, 0.28], colour, { bv: 0.006 });
  for (const x of [-0.42, 0.42]) A.box([x - 0.04, 0.06, -0.29], [x + 0.04, 0.53, 0.29], colour, { bv: BV });
  A.box([-0.49, 0.52, -0.29], [0.49, 0.56, 0.29], colour, { bv: BV });
  for (const x of [-0.25, 0.25]) A.box([x - 0.05, 0.53, -0.3], [x + 0.05, 0.57, 0.3], "iron", { bv: 0.004 });
  // boarded faces: three planks per long side standing 4 mm proud, the gaps reading as seams
  for (const s of [-1, 1]) for (let i = 0; i < 3; i++) {
    const y0 = 0.07 + i * 0.15;
    A.box([-0.375, y0 + 0.004, s > 0 ? 0.278 : -0.284], [0.375, y0 + 0.142, s > 0 ? 0.284 : -0.278], colour, { k: 0, bv: 0.004 });
  }
  // the lid straps carried down both faces, nailed; corner plates on the battens
  for (const x of [-0.25, 0.25]) for (const s of [-1, 1]) {
    A.box([x - 0.03, 0.4, s > 0 ? 0.284 : -0.29], [x + 0.03, 0.57, s > 0 ? 0.29 : -0.284], "iron", { k: 0, bv: 0.002 });
    nails(A, [x - 0.015, x + 0.015], 0.44, s > 0 ? 0.29 : -0.293, "iron");
  }
  for (const x of [-0.46, 0.46]) for (const s of [-1, 1]) for (const y of [0.09, 0.5]) A.box([x - 0.03, y - 0.03, s > 0 ? 0.29 : -0.296], [x + 0.03, y + 0.03, s > 0 ? 0.296 : -0.29], "iron", { k: 0, bv: 0.002 });
  // hasps (on the front, +Z) over staples, the hinge knuckles at the back
  for (const x of [-0.12, 0.12]) {
    A.box([x - 0.018, 0.47, 0.29], [x + 0.018, 0.56, 0.297], "iron", { k: 0, bv: 0.002 });
    A.arch([x, 0.455, 0.297], [0.026, 0.02, 0.008], "iron", { th: 0.005, k: 0 });
  }
  for (const x of [-0.3, 0.3]) A.cyl([x, 0.54, -0.3], [0.022, 0.1, 0.022], "iron", { seg: 8, r: [0, 0, 90], k: 0 });
  A.box([-0.2, 0.25, 0.281], [0.2, 0.4, 0.29], "stencil", { k: 0 });
  for (const sx of [-1, 1]) ropeHandle(A, sx * 0.48, sx, 0.34, 0, "oak_x", "rope");
  A.meta.crate = supplies;
  A.meta.nav = { effect: "none" };
  return A;
}

// Allied beach stack: waterproofed weapon and ammunition crates as unloaded by the first waves. The four crate bodies
// stay the colliders of version 1; bands, stencils, lid battens, rope handles and corner irons are dressing.
function alliedCrates() {
  const A = new Asset({ id: "rack_allied_crates", version: 2, name: "Allied weapons (beach crates)", category: "Supply",
    description: "Waterproofed weapon and ammunition crates stacked on the shingle / in an LCVP well, as unloaded by the first waves on Omaha. E opens the Allied arsenal, exactly like the A-frame rack.",
    floors: 0, enterable: false, footprint: [2.6, 1.1], plinthDepth: 0, maxGrade: 0.4 });
  A.mat("crate_wood", [0.3, 0.27, 0.17], 0.88, "wood");
  A.mat("crate_olive", [0.2, 0.22, 0.13], 0.8, "wood");
  A.mat("crate_band", [0.1, 0.1, 0.09], 0.6, "metal", { metallic: 0.5, grain: 0 });
  A.mat("crate_stencil", [0.75, 0.72, 0.6], 0.9, "plain", { grain: 0 });
  A.mat("rope", [0.5, 0.44, 0.32], 0.95, "fabric");
  // [centre, size, yaw, material, stencil size]
  const C = [[[-0.62, 0.25, -0.1], [1.15, 0.5, 0.7], 3, "crate_olive", [0.46, 0.125]], [[0.62, 0.25, -0.12], [1.15, 0.5, 0.7], -2, "crate_wood", [0.46, 0.125]],
    [[0, 0.71, -0.15], [1.2, 0.42, 0.62], 5, "crate_olive", [0.48, 0.105]], [[-1.05, 0.16, 0.35], [0.5, 0.32, 0.35], 20, "crate_wood", [0.2, 0.08]]];
  const at = (c, yaw, l) => { const r = (yaw * Math.PI) / 180; return [c[0] + l[0] * Math.cos(r) + l[2] * Math.sin(r), c[1] + l[1], c[2] - l[0] * Math.sin(r) + l[2] * Math.cos(r)]; };
  for (const [c, s, yaw, m, st] of C) {
    A.obox(c, s, [0, yaw, 0], m, { bv: 0.012 });   // the collider, chamfered
    const hx = s[0] / 2, hy = s[1] / 2, hz = s[2] / 2;
    for (const f of [-1, 1]) {   // two bands near the ends, a lid batten across the top, a rope handle on each end
      A.obox(at(c, yaw, [f * (hx - 0.1), 0, 0]), [0.04, s[1] + 0.01, s[2] + 0.01], [0, yaw, 0], "crate_band", { k: 0, bv: 0.003 });
      A.obox(at(c, yaw, [f * (hx - 0.24), hy + 0.012, 0]), [0.06, 0.024, s[2] - 0.04], [0, yaw, 0], m, { k: 0, bv: 0.006 });
      A.obox(at(c, yaw, [f * (hx + 0.006), 0.04, -0.06]), [0.012, 0.04, 0.03], [0, yaw, 0], m, { k: 0, bv: 0.004 });
      A.obox(at(c, yaw, [f * (hx + 0.006), 0.04, 0.06]), [0.012, 0.04, 0.03], [0, yaw, 0], m, { k: 0, bv: 0.004 });
      A.arch(at(c, yaw, [f * (hx + 0.016), 0.0, 0]), [0.14, 0.05, 0.014], "rope", { r: [180, yaw + 90, 0], th: 0.014, k: 0 });
    }
    A.obox(at(c, yaw, [0, 0.025, hz + 0.003]), [st[0], st[1], 0.005], [0, yaw, 0], "crate_stencil", { k: 0 });
    A.obox(at(c, yaw, [0, -hy + 0.04, hz + 0.004]), [s[0] - 0.04, 0.012, 0.006], [0, yaw, 0], "crate_band", { k: 0 });   // waterproofing tape seam
  }
  A.meta.faction = "US";
  A.meta.rack = {
    faction: "US",
    display: [{ weapon: "m1_garand", pose: "lean", p: [-0.84, 0, 0.36], lean: 16 }, { weapon: "m1_carbine", pose: "lean", p: [-0.42, 0, 0.36], lean: 16 },
      { weapon: "m1_thompson", pose: "lean", p: [0, 0, 0.36], lean: 16 }, { weapon: "m1918a2_bar", pose: "lean", p: [0.42, 0, 0.36], lean: 16 },
      { weapon: "m1c_garand", pose: "lean", p: [0.84, 0, 0.36], lean: 16 }, { weapon: "bazooka_m9", pose: "flat", p: [0, 0.93, -0.15], dir: 1 },
      { weapon: "m2hb_carry", pose: "stand", p: [1.75, 0, 0], yaw: 0 }],
    deploy: ["gun_m2hb_tripod"],
  };
  A.meta.nav = { effect: "carve" };
  return A;
}

export function props2Assets() {
  return [
    crate("crate_pak40_he", "PaK 40 ammunition crate", "Wooden crate of 7.5 cm HE rounds for a PaK 40 within 6 m. Finite stock by default (editable).",
      { supplies: { "75mm_he": 12, "75mm_ap": 6 }, unlimited: false, serviceRadius: 6 }, "crate_olive"),
    crate("crate_mixed_supply", "Mixed supply crate", "Sandbox range crate: rifle clips, magazines, belts, .50 links and 7.5 cm HE. Feeds personal reserve or a nearby emplacement; finite by default.",
      { supplies: { "30-06_clip": 30, "792_clip": 30, "9mm_mag32": 12, "45_mag20": 12, "792_belt50": 10, "50bmg_belt": 6, "75mm_he": 10, "30-06_bar_mag20": 12, "75mm_ap": 2 }, unlimited: false, serviceRadius: 6 }, "oak_x"),
    alliedCrates(),
  ];
}
