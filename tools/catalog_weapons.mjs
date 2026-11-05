// First-person weapon definitions -> content/weapons/*.json
// Local frame: butt at z = 0, muzzle toward +z, +y up, +x to the shooter's right. Metres.
// Ammunition presets and their evidence are in docs/weapons.md (source numbers refer to the update brief).
import { Asset } from "./kit.mjs";

function mats(A) {
  A.mat("walnut", [0.20, 0.10, 0.05], 0.55, "wood", { grain: 2 });
  A.mat("walnut_y", [0.20, 0.10, 0.05], 0.55, "wood", { grain: 1 });
  A.mat("parker", [0.10, 0.10, 0.095], 0.72, "metal", { metallic: 0.25 });   // parkerized: matte grey phosphate
  A.mat("blued", [0.20, 0.21, 0.24], 0.36, "metal", { metallic: 0.85 });   // blued steel: dark but reflective
  A.mat("bakelite", [0.11, 0.055, 0.03], 0.45, "plain");
  A.mat("leather", [0.16, 0.09, 0.045], 0.7, "fabric");
  A.mat("brass", [0.62, 0.44, 0.18], 0.32, "metal", { metallic: 1.0 });
  A.mat("lens", [0.02, 0.03, 0.05], 0.05, "plain");
  A.mat("canvas_od", [0.16, 0.15, 0.08], 0.9, "fabric");
}
// octagonal bar along z (union of four bars)
function tube(arr, A, c, r, len, m) {
  const L = 2 * r, W = 2 * r * Math.tan(Math.PI / 8);
  for (const a of [0, 45, 90, 135]) A.obox(c, [L, W, len], [0, 0, a], m, { into: arr, k: 0 });
}
function bx(arr, A, min, max, m) { A.box(min, max, m, { into: arr, k: 0 }); }
function ob(arr, A, c, s, r, m) { A.obox(c, s, r, m, { into: arr, k: 0 }); }
// rear aperture: a ring of four boxes around the sight line
function aperture(arr, A, x, y, z, outer, inner, m) {
  bx(arr, A, [x - outer, y + inner, z - 0.004], [x + outer, y + outer, z + 0.004], m);
  bx(arr, A, [x - outer, y - outer, z - 0.004], [x + outer, y - inner, z + 0.004], m);
  bx(arr, A, [x - outer, y - inner, z - 0.004], [x - inner, y + inner, z + 0.004], m);
  bx(arr, A, [x + inner, y - inner, z - 0.004], [x + outer, y + inner, z + 0.004], m);
}
function vnotch(arr, A, x, y, z, m) {
  bx(arr, A, [x - 0.012, y - 0.012, z - 0.003], [x + 0.012, y - 0.003, z + 0.003], m);
  ob(arr, A, [x - 0.007, y - 0.001, z], [0.008, 0.008, 0.006], [0, 0, 35], m);
  ob(arr, A, [x + 0.007, y - 0.001, z], [0.008, 0.008, 0.006], [0, 0, -35], m);
}

function weapon(meta, build) {
  const A = new Asset({ id: meta.id, version: 1, name: meta.name, category: "Weapons", description: meta.note || "", floors: 0, enterable: false, footprint: [1, 1] });
  mats(A);
  const parts = { body: [], bolt: [], mag: [], cover: [], clip: [], bipod: [], scope: [], handle: [] };
  build(A, parts);
  const j = A.json();
  return { ...meta, materials: j.materials, parts };
}

// ---------------------------------------------------------------- M1 Garand family
function garandBody(A, P, scoped) {
  const b = P.body;
  // stock: butt, wrist, fore-end, with butt plate
  bx(b, A, [-0.022, -0.115, 0.0], [0.022, 0.035, 0.012], "parker");
  ob(b, A, [0, -0.045, 0.16], [0.042, 0.13, 0.3], [-6, 0, 0], "walnut");
  ob(b, A, [0, -0.02, 0.36], [0.034, 0.06, 0.14], [4, 0, 0], "walnut");
  bx(b, A, [-0.022, -0.035, 0.42], [0.022, 0.01, 0.98], "walnut");
  bx(b, A, [-0.019, 0.01, 0.66], [0.019, 0.032, 0.98], "walnut");    // upper handguard
  if (scoped) ob(b, A, [-0.024, 0.0, 0.2], [0.012, 0.07, 0.17], [-6, 0, 0], "leather");   // cheek pad (left)
  // receiver, trigger guard, magazine well, op rod
  bx(b, A, [-0.018, -0.005, 0.44], [0.018, 0.04, 0.66], "parker");
  bx(b, A, [-0.01, -0.055, 0.44], [0.01, -0.035, 0.53], "parker");
  bx(b, A, [-0.016, 0.012, 0.6], [0.016, 0.045, 0.66], "parker");
  // barrel, gas cylinder with front sight wings and post
  tube(b, A, [0, 0.02, 0.86], 0.0115, 0.48, "parker");
  bx(b, A, [-0.012, 0.0, 1.02], [0.012, 0.035, 1.095], "parker");
  bx(b, A, [-0.012, 0.035, 1.07], [-0.009, 0.072, 1.085], "parker");
  bx(b, A, [0.009, 0.035, 1.07], [0.012, 0.072, 1.085], "parker");
  bx(b, A, [-0.002, 0.035, 1.073], [0.002, 0.072, 1.082], "parker");   // post: top on the sight line (y 0.072)
  if (scoped) { tube(b, A, [0, 0.02, 1.12], 0.016, 0.06, "parker"); }      // M2 flash hider (cone approximated)
  // rear aperture sight on the receiver (sight line y = 0.072)
  aperture(b, A, 0, 0.072, 0.56, 0.011, 0.0048, "parker");
  bx(b, A, [-0.014, 0.04, 0.55], [0.014, 0.06, 0.57], "parker");
  // operating rod handle (animated: cycles back on firing / clip insertion)
  bx(P.handle, A, [0.018, 0.01, 0.6], [0.034, 0.024, 0.625], "parker");
  bx(P.handle, A, [0.016, 0.012, 0.62], [0.022, 0.02, 0.8], "parker");
  // en-bloc clip (shown during reload, ejected after the last round)
  bx(P.clip, A, [-0.013, 0.0, 0.5], [0.013, 0.045, 0.56], "brass");
}
const garand = () => weapon({ id: "m1_garand", name: "M1 Garand", side: "US", action: "semi", feed: "enbloc", cap: 8, loaded: 8, reserve: 72, container: "30-06_clip", containerSize: 8,
  rpm: 400, velocity: 853, caliber: ".30-06", recoil: { pitch: 2.4, yaw: 0.6, recover: 9, visual: 0.07 }, reload: { empty: 2.3, partial: 2.8 },
  sights: { rear: [0, 0.072, 0.56], front: [0, 0.072, 1.08] }, muzzle: [0, 0.02, 1.1], zero: 100,
  note: "Semi-automatic, 8-round en-bloc clip; clip ejects with a ping after the last round. Preset: 8 loaded + 9 clips = 80 [3]." },
  (A, P) => garandBody(A, P, false));
const m1c = () => weapon({ id: "m1c_garand", name: "M1C Garand (sniper)", side: "US", action: "semi", feed: "enbloc", cap: 8, loaded: 8, reserve: 72, container: "30-06_clip", containerSize: 8,
  rpm: 400, velocity: 853, caliber: ".30-06", recoil: { pitch: 2.4, yaw: 0.5, recover: 9, visual: 0.07 }, reload: { empty: 2.4, partial: 2.9 },
  sights: { rear: [0, 0.072, 0.56], front: [0, 0.072, 1.08] }, muzzle: [0, 0.02, 1.15], zero: 100,
  scope: { mag: 2.5, reticle: "us_post_cross", eye: [-0.032, 0.108, 0.3], irons: true },
  laterWar: true,
  note: "Later-war sandbox option, not a documented 6 June 1944 issue weapon [7]. Offset left scope keeps the clip path clear; cheek pad on the stock. Inherits the rifle belt preset 8 + 72 [3,7]." },
  (A, P) => {
    garandBody(A, P, true);
    const s = P.scope;
    bx(s, A, [-0.045, 0.04, 0.47], [-0.02, 0.09, 0.5], "blued");     // offset mount on the left of the receiver
    bx(s, A, [-0.045, 0.04, 0.6], [-0.02, 0.09, 0.63], "blued");
    tube(s, A, [-0.032, 0.108, 0.53], 0.0125, 0.24, "blued");
    tube(s, A, [-0.032, 0.108, 0.68], 0.017, 0.08, "blued");
    tube(s, A, [-0.032, 0.108, 0.405], 0.016, 0.04, "blued");
    bx(s, A, [-0.043, 0.098, 0.721], [-0.021, 0.118, 0.722], "lens");
  });

// ---------------------------------------------------------------- Kar98k family
function karBody(A, P, scoped) {
  const b = P.body;
  bx(b, A, [-0.022, -0.115, 0.0], [0.022, 0.03, 0.012], "parker");
  ob(b, A, [0, -0.048, 0.17], [0.042, 0.125, 0.32], [-7, 0, 0], "walnut");
  ob(b, A, [0, -0.022, 0.37], [0.032, 0.055, 0.12], [6, 0, 0], "walnut");
  bx(b, A, [-0.022, -0.04, 0.42], [0.022, 0.012, 0.96], "walnut");
  bx(b, A, [-0.017, 0.012, 0.7], [0.017, 0.03, 0.9], "walnut");
  bx(b, A, [-0.017, -0.005, 0.44], [0.017, 0.035, 0.66], "blued");       // receiver
  bx(b, A, [-0.012, -0.06, 0.46], [0.012, -0.04, 0.6], "blued");        // floorplate / trigger guard
  for (const z of [0.86, 0.94]) bx(b, A, [-0.024, -0.042, z], [0.024, 0.033, z + 0.015], "blued");   // barrel bands
  tube(b, A, [0, 0.018, 0.88], 0.0105, 0.46, "blued");
  bx(b, A, [-0.004, -0.02, 1.06], [0.004, -0.008, 1.1], "blued");        // bayonet lug
  if (!scoped) {
    // tangent rear sight with V-notch, hooded front post (sight line y = 0.058)
    bx(b, A, [-0.013, 0.03, 0.66], [0.013, 0.045, 0.72], "blued");
    vnotch(b, A, 0, 0.058, 0.715, "blued");
    bx(b, A, [-0.0025, 0.03, 1.085], [0.0025, 0.058, 1.095], "blued");
    bx(b, A, [-0.012, 0.03, 1.078], [-0.009, 0.068, 1.1], "blued");
    bx(b, A, [0.009, 0.03, 1.078], [0.012, 0.068, 1.1], "blued");
    bx(b, A, [-0.012, 0.065, 1.078], [0.012, 0.068, 1.1], "blued");
  }
  // bolt with turned-down handle (animated)
  tube(P.bolt, A, [0, 0.028, 0.53], 0.009, 0.16, "blued");
  ob(P.bolt, A, [0.032, 0.006, 0.48], [0.045, 0.01, 0.01], [0, 0, -35], "blued");
  tube(P.bolt, A, [0.052, -0.008, 0.48], 0.009, 0.02, "blued");
  // stripper clip (shown while stripping rounds into the internal magazine)
  bx(P.clip, A, [-0.008, 0.04, 0.52], [0.008, 0.075, 0.56], "brass");
}
const kar = () => weapon({ id: "kar98k", name: "Karabiner 98k", side: "DE", action: "bolt", feed: "stripper", cap: 5, loaded: 5, reserve: 55, container: "792_clip", containerSize: 5,
  rpm: 60, velocity: 760, caliber: "7.92x57 mm", recoil: { pitch: 2.9, yaw: 0.7, recover: 8, visual: 0.08 }, reload: { empty: 2.2, round: 0.5 }, boltTime: 0.85,
  sights: { rear: [0, 0.058, 0.715], front: [0, 0.058, 1.09] }, muzzle: [0, 0.018, 1.11], zero: 100,
  note: "Bolt action, 5-round internal magazine. Empty: strip a 5-round clip; partial: top off single rounds. Preset 5 + 11 clips = 60 (pouch baseline) [5,6]." },
  (A, P) => karBody(A, P, false));
const karScoped = () => weapon({ id: "kar98k_zf39", name: "Karabiner 98k with ZF39 (4x)", side: "DE", action: "bolt", feed: "single", cap: 5, loaded: 5, reserve: 55, container: "792_clip", containerSize: 5,
  rpm: 55, velocity: 760, caliber: "7.92x57 mm", recoil: { pitch: 2.9, yaw: 0.6, recover: 8, visual: 0.08 }, reload: { round: 0.6 }, boltTime: 1.0,
  sights: { rear: [0, 0.112, 0.45], front: [0, 0.112, 0.7] }, muzzle: [0, 0.018, 1.11], zero: 100,
  scope: { mag: 4, reticle: "de_absehen1", eye: [0, 0.112, 0.36], irons: false },
  note: "4x ZF39 on a low turret mount over the receiver: it blocks the stripper-clip path, so rounds load singly. Irons are blocked by the scope. Inherits the 5 + 55 pouch preset [5,8]." },
  (A, P) => {
    karBody(A, P, true);
    const s = P.scope;
    bx(s, A, [-0.015, 0.035, 0.47], [0.015, 0.09, 0.5], "blued");
    bx(s, A, [-0.015, 0.035, 0.62], [0.015, 0.09, 0.65], "blued");
    tube(s, A, [0, 0.112, 0.56], 0.0135, 0.24, "blued");
    tube(s, A, [0, 0.112, 0.7], 0.02, 0.06, "blued");
    tube(s, A, [0, 0.112, 0.43], 0.018, 0.05, "blued");
    bx(s, A, [-0.012, 0.1, 0.731], [0.012, 0.124, 0.732], "lens");
  });

// ---------------------------------------------------------------- MP 40
const mp40 = () => weapon({ id: "mp40", name: "MP 40", side: "DE", action: "auto", feed: "magazine", cap: 32, loaded: 32, reserve: 160, container: "9mm_mag32", containerSize: 32,
  rpm: 550, velocity: 400, caliber: "9x19 mm", recoil: { pitch: 0.75, yaw: 0.35, recover: 11, climb: 0.18, visual: 0.03 }, reload: { empty: 2.6, partial: 2.4 },
  sights: { rear: [0, 0.058, 0.36], front: [0, 0.058, 0.66] }, muzzle: [0, 0.018, 0.69], zero: 100,
  note: "Open-bolt, 32-round straight box; folding steel stock. Preset: 6 magazines = 192 rounds [6]. Partial magazines are kept." },
  (A, P) => {
    const b = P.body;
    // folding stock: two pressed-steel arms + butt plate
    for (const s of [-1, 1]) bx(b, A, [s * 0.018 - 0.004, -0.04, 0.02], [s * 0.018 + 0.004, -0.03, 0.27], "blued");
    bx(b, A, [-0.025, -0.1, 0.0], [0.025, -0.02, 0.02], "blued");
    tube(b, A, [0, 0.025, 0.33], 0.022, 0.2, "blued");           // receiver tube
    bx(b, A, [-0.014, -0.045, 0.2], [0.014, 0.01, 0.36], "bakelite");   // lower receiver / grip frame
    ob(b, A, [0, -0.08, 0.26], [0.026, 0.08, 0.035], [-12, 0, 0], "bakelite");   // pistol grip
    bx(b, A, [-0.016, -0.03, 0.43], [0.016, 0.01, 0.47], "blued");      // magazine housing
    tube(b, A, [0, 0.018, 0.56], 0.0085, 0.26, "blued");          // barrel
    bx(b, A, [-0.008, -0.002, 0.58], [0.008, 0.008, 0.62], "bakelite");  // rest bar
    // hooded front post and flip rear sight (sight line y = 0.058)
    bx(b, A, [-0.002, 0.026, 0.655], [0.002, 0.058, 0.665], "blued");
    bx(b, A, [-0.011, 0.026, 0.648], [-0.008, 0.066, 0.672], "blued"); bx(b, A, [0.008, 0.026, 0.648], [0.011, 0.066, 0.672], "blued");
    bx(b, A, [-0.011, 0.063, 0.648], [0.011, 0.066, 0.672], "blued");
    bx(b, A, [-0.012, 0.046, 0.35], [0.012, 0.05, 0.37], "blued");
    vnotch(b, A, 0, 0.058, 0.36, "blued");
    bx(P.bolt, A, [-0.026, 0.02, 0.33], [-0.022, 0.035, 0.36], "blued");   // cocking handle (left side)
    bx(P.mag, A, [-0.012, -0.29, 0.432], [0.012, -0.03, 0.468], "blued");
  });

// ---------------------------------------------------------------- M1 Thompson
const thompson = () => weapon({ id: "m1_thompson", name: "M1 Thompson", side: "US", action: "auto", feed: "magazine", cap: 20, loaded: 20, reserve: 80, container: "45_mag20", containerSize: 20,
  rpm: 700, velocity: 285, caliber: ".45 ACP", recoil: { pitch: 0.95, yaw: 0.45, recover: 9, climb: 0.24, visual: 0.04 }, reload: { empty: 2.8, partial: 2.6 },
  sights: { rear: [0, 0.072, 0.47], front: [0, 0.072, 0.77] }, muzzle: [0, 0.026, 0.8], zero: 50,
  note: "M1 pattern: side charging handle, fixed protected rear sight, plain barrel, 20-round stick magazine (no drum, no top actuator). Preset: five-pocket 20-round carrier = 100 [3,4]." },
  (A, P) => {
    const b = P.body;
    bx(b, A, [-0.022, -0.12, 0.0], [0.022, 0.025, 0.012], "parker");
    ob(b, A, [0, -0.05, 0.16], [0.04, 0.12, 0.3], [-5, 0, 0], "walnut");
    bx(b, A, [-0.02, -0.02, 0.3], [0.02, 0.055, 0.55], "parker");     // receiver
    ob(b, A, [0, -0.07, 0.33], [0.03, 0.08, 0.04], [-10, 0, 0], "walnut");     // grip
    bx(b, A, [-0.008, -0.04, 0.32], [0.008, -0.02, 0.39], "parker");
    bx(b, A, [-0.018, -0.045, 0.57], [0.018, 0.0, 0.7], "walnut");      // horizontal fore-end
    tube(b, A, [0, 0.026, 0.68], 0.011, 0.25, "parker");             // plain barrel
    bx(b, A, [-0.002, 0.036, 0.765], [0.002, 0.072, 0.775], "parker");    // front blade
    aperture(b, A, 0, 0.072, 0.47, 0.011, 0.0035, "parker");          // fixed aperture
    bx(b, A, [-0.014, 0.055, 0.46], [-0.011, 0.085, 0.48], "parker"); bx(b, A, [0.011, 0.055, 0.46], [0.014, 0.085, 0.48], "parker");   // protective ears
    bx(P.bolt, A, [0.02, 0.025, 0.41], [0.032, 0.04, 0.43], "parker");    // side charging handle (right)
    bx(P.mag, A, [-0.011, -0.2, 0.505], [0.011, -0.015, 0.54], "parker");
  });

// ---------------------------------------------------------------- MG 42
const mg42 = () => weapon({ id: "mg42", name: "MG 42 (light, bipod)", side: "DE", action: "auto", feed: "belt", cap: 50, loaded: 50, reserve: 200, container: "792_belt50", containerSize: 50,
  rpm: 1200, velocity: 755, caliber: "7.92x57 mm", recoil: { pitch: 0.9, yaw: 0.6, recover: 7, climb: 0.3, visual: 0.035, supported: 0.35 }, reload: { empty: 4.6, partial: 4.9 },
  sights: { rear: [0, 0.075, 0.53], front: [0, 0.075, 1.02] }, muzzle: [0, 0.04, 1.23], zero: 200, tracerEvery: 5,
  note: "Belt-fed from joinable 50-round segments; bipod support (crouch, stationary) makes bursts far easier to control. Preset 50 + 4 segments = 250 ready rounds for the gun team [3,6,9]." },
  (A, P) => {
    const b = P.body;
    bx(b, A, [-0.02, -0.13, 0.0], [0.02, 0.02, 0.012], "bakelite");
    ob(b, A, [0, -0.05, 0.16], [0.04, 0.11, 0.3], [-6, 0, 0], "bakelite");
    bx(b, A, [-0.026, -0.03, 0.3], [0.026, 0.06, 0.58], "blued");     // receiver
    ob(b, A, [0, -0.09, 0.37], [0.03, 0.09, 0.04], [-12, 0, 0], "bakelite");
    bx(b, A, [-0.01, -0.05, 0.36], [0.01, -0.03, 0.43], "blued");
    // perforated barrel jacket: square-ish shroud with dark slots
    bx(b, A, [-0.024, 0.016, 0.58], [0.024, 0.064, 1.12], "blued");
    for (let z = 0.62; z < 1.1; z += 0.06) for (const s of [-1, 1]) bx(b, A, [s * 0.0245 - 0.001, 0.03, z], [s * 0.0245 + 0.001, 0.05, z + 0.03], "lens");
    tube(b, A, [0, 0.04, 1.17], 0.018, 0.1, "blued");                 // muzzle booster
    bx(b, A, [-0.012, 0.064, 0.52], [0.012, 0.075, 0.55], "blued"); vnotch(b, A, 0, 0.075, 0.535, "blued");
    bx(b, A, [-0.002, 0.064, 1.015], [0.002, 0.075, 1.025], "blued");
    bx(b, A, [-0.026, -0.02, 0.46], [-0.06, 0.02, 0.5], "blued");     // feed tray (left)
    for (let k = 0; k < 9; ++k) bx(b, A, [-0.06 - k * 0.012, -0.012, 0.465], [-0.052 - k * 0.012, 0.0, 0.495], "brass");   // belt
    bx(P.cover, A, [-0.027, 0.06, 0.38], [0.027, 0.07, 0.58], "blued");   // top cover (hinged at its front)
    bx(P.bolt, A, [0.026, 0.0, 0.42], [0.036, 0.02, 0.45], "blued");      // cocking handle (right)
    for (const s of [-1, 1]) ob(P.bipod, A, [s * 0.03, 0.02, 0.95], [0.012, 0.012, 0.3], [0, s * 6, 0], "blued");   // folded legs
  });

export function weapons() { return [garand(), kar(), mp40(), thompson(), m1c(), karScoped(), mg42()]; }
