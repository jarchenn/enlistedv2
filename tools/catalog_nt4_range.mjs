// NT4: shooting-range kit for the outpost maps.
//   Baked models (content/assets/models/<dir>, --import-weapon-models from incoming/nt4_models):
//     "Gun Targets" by FrodoUndead (Sketchfab, CC BY 4.0): split into a single target, a double target, a moving
//       target on its rails and a shooting booth.
//     "Old Ammo Crate" by Geoffroy.Sainte.Catherine (Sketchfab, CC BY 4.0).
//     A paper target on a stand (shooting-target.zip, no author / licence recorded): local only (git-ignored bake).
//   Each baked asset carries generated stand-in parts ("mf": 1, hidden once the bake is on disk) and its colliders.
//   Built here: the roofed firing-line shelter, range flags, lane markers and a range sign.
import { Asset } from "./kit.mjs";

const CREDIT_T = "Gun Targets by FrodoUndead (https://sketchfab.com/3d-models/gun-targets-eb9fbe283faa41ef8685359a0769d303), CC BY 4.0, modified";
const CREDIT_C = "Old Ammo Crate by Geoffroy.Sainte.Catherine (https://sketchfab.com/3d-models/old-ammo-crate-320d935472354cc79590bc35ade9d855), CC BY 4.0, modified";
const CREDIT_P = "Shooting target (shooting-target.zip, supplied by the user): licence to be confirmed (local only)";
const META = { category: "Modern: Military", floors: 0, enterable: false, plinthDepth: 0.1, maxGrade: 1.5, noAutoPanels: true, version: 1 };

function mats(A) {
  A.mat("wood", [0.4, 0.31, 0.2], 0.9, "wood", { texture: "boards_weathered" });
  A.mat("board", [0.62, 0.58, 0.5], 0.95, "plain", { texture: "plaster_flaking" });
  A.mat("steel", [0.3, 0.22, 0.15], 0.75, "metal", { texture: "steel_rust_light", metallic: 0.2 });
  A.mat("corr", [0.34, 0.27, 0.2], 0.8, "metal", { texture: "corrugated_blue_rusty", metallic: 0.15 });
  A.mat("concrete", [0.48, 0.47, 0.44], 0.96, "concrete", { texture: "concrete_stained" });
  A.mat("sandbag", [0.42, 0.37, 0.26], 1.0, "fabric", { texture: "canvas_rough" });
  A.mat("red", [0.5, 0.1, 0.08], 0.9, "fabric", { texture: "canvas_rough" });
  A.mat("white", [0.66, 0.64, 0.6], 0.9, "plain", { texture: "plaster_flaking" });
  A.mat("brass", [0.62, 0.45, 0.18], 0.4, "metal", { metallic: 0.8 });
}
function modelAsset(id, name, desc, fp, src, dir, scale, credit, standIn) {
  const A = new Asset({ id, name, description: desc, footprint: fp, ...META });
  mats(A);
  standIn(A);
  for (const p of A.parts) if (p.v !== 0) p.mf = 1;
  A.meta.model = { src, dir, pivot: "ground", scale, maxTex: 1024, simplify: 0.8, lodDist: 30, cullDist: 250, credit };
  return A;
}

function shelter() {   // the firing line: 12 m x 4 m, a sloped corrugated roof on posts, a counter with sandbag rests, a back wall
  const A = new Asset({ id: "mod_range_shelter", name: "Range firing-line shelter",
    description: "A roofed firing line 12 m long: rusted corrugated roof on timber posts, a shooting counter with sandbag rests, a low back wall, spent brass underfoot.",
    footprint: [12.4, 4.4], ...META, nav: { effect: "carve" } });
  mats(A);
  A.box([-6.2, -0.1, -2.2], [6.2, 0.08, 2.2], "concrete", { bv: 0.03 });   // slab
  for (const x of [-6, -2, 2, 6]) for (const [z, h] of [[-2.0, 2.7], [2.0, 2.3]]) A.box([x - 0.09, 0.08, z - 0.09], [x + 0.09, h, z + 0.09], "wood", { bv: 0.01 });
  for (let k = 0; k < 6; k++) {   // roof sheets, one missing, one slipped
    if (k === 4) continue;
    const x0 = -6.3 + k * 2.1, slip = k === 2 ? 0.25 : 0;
    A.obox([x0 + 1.05, 2.55 - slip * 0.3, 0 + slip], [2.12, 0.03, 4.7], [-5.7, 0, k === 2 ? 3 : 0], "corr", {});
  }
  for (const z of [-2.0, 2.0]) A.box([-6.2, z > 0 ? 2.25 : 2.65, z - 0.08], [6.2, z > 0 ? 2.38 : 2.78, z + 0.08], "wood", { k: 0 });   // purlins
  A.box([-6.0, 0.95, -2.0], [6.0, 1.05, -1.4], "wood", { bv: 0.01 });   // counter
  for (const x of [-5.6, -2, 2, 5.6]) A.box([x - 0.07, 0.08, -1.95], [x + 0.07, 0.95, -1.45], "wood", { k: 0 });
  for (let k = 0; k < 6; k++) A.rbox([-5.0 + k * 2.0 - 0.3, 1.05, -1.95], [-5.0 + k * 2.0 + 0.3, 1.2, -1.6], 0.05, "sandbag", { k: 0 });   // rests
  A.box([-6.1, 0.08, 1.95], [6.1, 1.1, 2.15], "concrete", { bv: 0.02 });   // back wall
  for (let k = 0; k < 40; k++) {   // spent brass
    const x = -5.5 + ((k * 37) % 110) / 10, z = -1.2 + ((k * 13) % 20) / 10;
    A.obox([x, 0.088, z], [0.012, 0.012, 0.05], [0, (k * 47) % 180, 90], "brass", { k: 0 });
  }
  return A;
}
function flag() {
  const A = new Asset({ id: "mod_range_flag", name: "Range flag", description: "A 6 m steel pole with a sun-faded, torn red range flag.", footprint: [0.6, 0.6], ...META });
  mats(A);
  A.cyl([0, 3, 0], [0.08, 6, 0.08], "steel");
  A.box([0.06, 4.9, -0.01], [1.3, 5.75, 0.01], "red", { k: 0 });
  A.obox([1.45, 5.3, 0], [0.35, 0.45, 0.02], [0, 0, -12], "red", { k: 0 });   // the torn, drooping tail
  A.box([-0.25, 0, -0.25], [0.25, 0.2, 0.25], "concrete", { bv: 0.03 });
  return A;
}
function marker() {
  const A = new Asset({ id: "mod_range_marker", name: "Range lane marker", description: "A white lane post with a red top, chipped and leaning a little.", footprint: [0.3, 0.3], ...META });
  mats(A);
  A.obox([0, 0.6, 0], [0.14, 1.2, 0.14], [0, 0, 3], "white", {});
  A.obox([0.03, 1.12, 0], [0.15, 0.16, 0.15], [0, 0, 3], "red", { k: 0 });
  return A;
}
function sign() {
  const A = new Asset({ id: "mod_range_sign", name: "Range warning sign", description: "A red and white range warning board on two posts, faded and bullet-holed.", footprint: [1.6, 0.3], ...META });
  mats(A);
  for (const x of [-0.6, 0.6]) A.box([x - 0.05, 0, -0.05], [x + 0.05, 2.0, 0.05], "wood");
  A.box([-0.8, 1.1, 0.05], [0.8, 1.9, 0.08], "white", { k: 0 });
  for (let k = 0; k < 4; k++) A.box([-0.8 + k * 0.4 + 0.02, 1.12, 0.081], [-0.8 + k * 0.4 + 0.2, 1.88, 0.085], "red", { k: 0 });
  return A;
}

export function nt4RangeAssets() {
  return [
    modelAsset("mod_range_target_single", "Range target (single)", "A steel target stand with a square paper target, shot through.", [1.1, 0.4],
      "range_target_single.glb", "range_target_single", 0.045, CREDIT_T, (A) => { A.box([-0.5, 0, -0.08], [0.5, 1.8, 0.08], "board"); }),
    modelAsset("mod_range_target_double", "Range target (double)", "A double target stand with two man-shaped paper targets.", [2.5, 0.4],
      "range_target_double.glb", "range_target_double", 0.045, CREDIT_T, (A) => { A.box([-1.2, 0, -0.08], [1.2, 1.8, 0.08], "board"); }),
    modelAsset("mod_range_target_moving", "Range target (moving, on rails)", "A man-shaped target on a carriage riding a 14 m rail track.", [1.7, 14.3],
      "range_target_moving.glb", "range_target_moving", 0.045, CREDIT_T, (A) => { A.box([-0.8, 0, -7.1], [0.8, 0.2, 7.1], "steel"); A.box([-0.35, 0.2, 6.2], [0.35, 1.85, 6.4], "board"); }),
    modelAsset("mod_range_booth", "Shooting booth", "A wooden shooting booth: a firing counter with a side wall.", [1.9, 1.3],
      "range_booth.glb", "range_booth", 0.045, CREDIT_T, (A) => { A.box([-0.9, 0, -0.6], [0.9, 1.05, 0.6], "wood"); A.box([-0.95, 0, -0.6], [-0.85, 1.95, 0.6], "wood"); }),
    modelAsset("mod_ammo_crate_old", "Old ammo crate", "A battered wooden ammo crate with rope handles and steel latches.", [1.25, 0.55],
      "ammo_crate_old.glb", "ammo_crate_old", 0.6, CREDIT_C, (A) => { A.box([-0.6, 0, -0.26], [0.6, 0.5, 0.26], "wood"); }),
    modelAsset("mod_range_target_paper", "Range target (paper, on stand)", "A paper target on a timber stand.", [2.1, 1.2],
      "range_target_paper.glb", "range_target_paper", 1, CREDIT_P, (A) => { A.box([-0.9, 0, -0.1], [0.9, 2.4, 0.1], "board"); }),
    shelter(), flag(), marker(), sign(),
  ];
}
