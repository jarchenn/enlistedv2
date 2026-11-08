// B2 dressing (docs/asset-format.md#b1-geometry-kit, docs/asset-usage-default-maps.md): the yard, garden, churchyard and
// roadside pieces placed around the reworked buildings (catalog_houses2.mjs "companions") and on the chateau-town map.
// Front faces +Z, ground at y = 0, members embedded 1 cm or more into what they sit on (shimmer rule).
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
import { Asset, swingSign } from "./kit.mjs";
import { stdMaterials, weather } from "./parts.mjs";
import { rng, cover, setNav, sock, setSnap, r3 } from "./catalog_atlantic.mjs";

const ensure = (A, name, ...def) => { if (!A.materials.some((m) => m.name === name)) A.mat(name, ...def); return name; };
function base(meta) {
  const A = new Asset({ version: 1, category: "Dressing", floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.6, ...meta });
  stdMaterials(A);
  return A;
}
const hay = (A) => ensure(A, "hay", [0.48, 0.4, 0.2], 0.95, "fabric", { grain: 0 });
const lineCover = (L, z, height = "low") => {
  const pts = [];
  for (let x = -L / 2 + 0.6; x <= L / 2 - 0.5; x += Math.max(1.2, L / Math.max(1, Math.round(L / 2.4)))) pts.push(cover([x, 0, -z], 0, height), cover([x, 0, z], 180, height));
  return pts;
};
const hz = 4 * Math.PI / 180;   // a few degrees of seeded lean / turn

// ============================================================================ garden wall with saddle-back coping
// a 4 m rubble run with a footing, chained end to end (snap kind garden_wall, shared with the gate piers)
export function gardenWall() {
  const L = 4.0, T = 0.45, H = 1.15;
  const A = base({ id: "drs_garden_wall", name: "Garden wall with coping (4 m)", footprint: [L, 0.7],
    description: "4 m run of rubble-limestone garden wall, 1.3 m high, on a chamfered footing with a dressed saddle-back coping. Chains end to end and onto the gate piers.",
    ivy: { auto: true, density: 0.55 } });
  A.box([-L / 2, -0.35, -T / 2 - 0.06], [L / 2, 0.12, T / 2 + 0.06], "stone_plinth", { bv: 0.03 });
  A.box([-L / 2, 0.1, -T / 2], [L / 2, H, T / 2], "stone");
  A.box([-L / 2, H - 0.01, -T / 2 - 0.05], [L / 2, H + 0.09, T / 2 + 0.05], "stone_dressed", { bv: 0.02 });
  for (const s of [1, -1]) A.wedge([0, H + 0.13, s * (T / 2 + 0.05) / 2], [L, 0.1, T / 2 + 0.05], "stone_dressed", { top: 0.25, r: [0, s > 0 ? 0 : 180, 0] });
  const R = rng(4401);   // through-stones and a patch of dressed repair
  for (let i = 0; i < 4; i++) { const x = -L / 2 + 0.4 + R() * (L - 0.8), y = 0.3 + R() * 0.6; for (const s of [1, -1]) A.box([x - 0.16, y, s * (T / 2 - 0.01)], [x + 0.16, y + 0.12, s * (T / 2 + 0.012)], "stone_dressed", { bv: 0.01, k: 0 }); }
  setNav(A, { effect: "carve", cover: lineCover(L, 0.75) });
  setSnap(A, [sock([-L / 2, 0, 0], [-1, 0], "garden_wall"), sock([L / 2, 0, 0], [1, 0], "garden_wall")]);
  return A;
}

// ============================================================================ gate piers + wrought-iron gate
export function gatePiers() {
  const O = 1.3, PW = 0.6, H = 1.9;
  const A = base({ id: "drs_gate_piers", name: "Gate piers with iron gate", footprint: [2 * (O + PW), 0.8],
    description: "Pair of dressed-stone gate piers with moulded caps and ball finials hung with a double wrought-iron gate (2.6 m opening) that opens inward. Chains onto the garden wall." });
  for (const sx of [-1, 1]) {
    const x = sx * (O + PW / 2);
    A.box([x - PW / 2 - 0.06, -0.3, -PW / 2 - 0.06], [x + PW / 2 + 0.06, 0.3, PW / 2 + 0.06], "stone_plinth", { bv: 0.03 });
    A.box([x - PW / 2, 0.28, -PW / 2], [x + PW / 2, H, PW / 2], "stone_dressed", { bv: 0.015 });
    A.box([x - PW / 2 - 0.05, H - 0.01, -PW / 2 - 0.05], [x + PW / 2 + 0.05, H + 0.1, PW / 2 + 0.05], "stone_dressed", { bv: 0.02 });
    A.box([x - PW / 2 - 0.09, H + 0.09, -PW / 2 - 0.09], [x + PW / 2 + 0.09, H + 0.2, PW / 2 + 0.09], "stone_dressed", { bv: 0.03 });
    A.cyl([x, H + 0.25, 0], [0.2, 0.12, 0.2], "stone_dressed", { k: 0 });
    A.cone([x, H + 0.36, 0], [0.36, 0.12, 0.36], "stone_dressed", { top: 0.55, r: [180, 0, 0], k: 0 });
    A.cyl([x, H + 0.46, 0], [0.36, 0.08, 0.36], "stone_dressed", { k: 0 });
    A.cone([x, H + 0.56, 0], [0.36, 0.12, 0.36], "stone_dressed", { top: 0.55, k: 0 });
    for (const y of [0.45, 1.45]) A.box([x - sx * PW / 2 - sx * 0.05, y, -0.03], [x - sx * PW / 2 + sx * 0.01, y + 0.06, 0.03], "iron", { k: 0 });   // hinge pins
  }
  A.box([-O, -0.05, -0.12], [O, 0.04, 0.12], "stone_dressed", { bv: 0.01 });   // threshold kerb
  const leaves = [];
  for (const sx of [-1, 1]) {
    const L = { parts: [] }, T = { into: L.parts, k: 0 }, h0 = sx * O, h1 = sx * 0.012, xa = Math.min(h0, h1), xb = Math.max(h0, h1);
    A.box([xa, 0.1, -0.025], [xb, 0.15, 0.025], "iron", T);
    A.box([xa, 0.9, -0.02], [xb, 0.94, 0.02], "iron", T);
    A.box([xa, 1.5, -0.025], [xb, 1.55, 0.025], "iron", T);
    for (const xx of [xa, xb - 0.05]) A.box([xx, 0.08, -0.03], [xx + 0.05, 1.62, 0.03], "iron", T);
    for (let x = xa + 0.12; x < xb - 0.08; x += 0.12) {
      const top = 1.5 + 0.12 * (1 - Math.abs(x - h1) / O);   // bars rise toward the meeting stiles
      A.cyl([x, (0.12 + top) / 2, 0], [0.022, top - 0.12, 0.022], "iron", { ...T, n: 4 });
      A.cone([x, top + 0.06, 0], [0.05, 0.12, 0.05], "iron", { ...T, n: 4 });
    }
    A.box([xa, 0.08, -0.03], [xb, 1.6, 0.03], "iron", { into: L.parts, v: 0 });
    const hinge = [h0, 0, 0], mid = [(h0 + h1) / 2, 0, 0];
    L.hinge = hinge; L.sign = swingSign("x", mid, hinge, -1);
    leaves.push(L);
  }
  A.interactables.push({ id: "gate", kind: "door", label: "gate", sound: "steel", axis: [0, 1, 0], openAngle: 95, duration: 1.4, defaultOpen: false, leaves,
    aperture: { min: [-O, 0.05, -0.06], max: [O, 1.65, 0.06] } });
  setNav(A, { effect: "none", cover: [cover([-(O + PW / 2), 0, -0.9], 0, "high", ["right"]), cover([O + PW / 2, 0, -0.9], 0, "high", ["left"]), cover([-(O + PW / 2), 0, 0.9], 180, "high", ["left"]), cover([O + PW / 2, 0, 0.9], 180, "high", ["right"])] });
  setSnap(A, [sock([-(O + PW), 0, 0], [-1, 0], "garden_wall"), sock([O + PW, 0, 0], [1, 0], "garden_wall")]);
  weather(A);
  return A;
}

// ============================================================================ picket fence
export function picketFence() {
  const L = 3.0;
  const A = base({ id: "drs_picket_fence", name: "Picket fence (3 m)", footprint: [L, 0.2],
    description: "3 m of painted picket fence on square posts with pyramid caps: cottage gardens and the schoolyard. Chains end to end." });
  const R = rng(4402), paint = "window_paint";
  for (const x of [-L / 2 + 0.05, 0, L / 2 - 0.05]) {
    A.box([x - 0.05, -0.35, -0.1], [x + 0.05, 1.05, 0.0], paint, { bv: 0.008, k: 0 });
    A.cone([x, 1.09, -0.05], [0.11, 0.09, 0.11], paint, { n: 4, k: 0 });
  }
  for (const y of [0.28, 0.78]) A.box([-L / 2, y, -0.075], [L / 2, y + 0.07, -0.035], paint, { k: 0 });
  for (let x = -L / 2 + 0.16; x < L / 2 - 0.1; x += 0.13) {
    if (Math.abs(x) < 0.07 || Math.abs(Math.abs(x) - (L / 2 - 0.05)) < 0.08) continue;
    const h = 0.86 + (R() - 0.5) * 0.03, lean = (R() - 0.5) * 2;
    A.obox([x, 0.08 + h / 2 - 0.1, -0.024], [0.07, h, 0.022], [0, 0, lean], paint, { k: 0 });
    A.cone([x, 0.08 + h - 0.1 + 0.045, -0.024], [0.07, 0.09, 0.022], paint, { n: 4, k: 0 });
  }
  A.box([-L / 2, 0, -0.1], [L / 2, 1.0, 0.0], paint, { v: 0 });
  setNav(A, { effect: "carve" });
  setSnap(A, [sock([-L / 2, 0, -0.05], [-1, 0], "picket_fence"), sock([L / 2, 0, -0.05], [1, 0], "picket_fence")]);
  return A;
}

// ============================================================================ flower bed
export function flowerBed() {
  const W = 2.2, D = 1.0;
  const A = base({ id: "drs_flower_bed", name: "Flower bed", footprint: [W + 0.1, D + 0.1],
    description: "Raised cottage flower bed in a dressed-stone kerb: geraniums, marigolds, daisies and cornflowers over dark leaves. Purely decorative (walk-over)." });
  const pet = { red: ensure(A, "petal_red", [0.5, 0.045, 0.035], 0.7, "plain"), yel: ensure(A, "petal_yellow", [0.62, 0.42, 0.04], 0.7, "plain"),
    wht: ensure(A, "petal_white", [0.72, 0.71, 0.64], 0.7, "plain"), blu: ensure(A, "petal_blue", [0.1, 0.14, 0.48], 0.7, "plain"), pnk: ensure(A, "petal_pink", [0.62, 0.22, 0.32], 0.7, "plain") };
  const leaf = ensure(A, "leaf_green", [0.045, 0.1, 0.03], 0.85, "plain"), leaf2 = ensure(A, "leaf_dark", [0.03, 0.07, 0.025], 0.85, "plain");
  for (const s of [-1, 1]) {
    A.box([-W / 2 - 0.05, -0.1, s > 0 ? D / 2 - 0.06 : -D / 2 - 0.05], [W / 2 + 0.05, 0.16, s > 0 ? D / 2 + 0.05 : -D / 2 + 0.06], "stone_dressed", { bv: 0.012, k: 0 });
    A.box([s > 0 ? W / 2 - 0.06 : -W / 2 - 0.04, -0.1, -D / 2 + 0.04], [s > 0 ? W / 2 + 0.04 : -W / 2 + 0.06, 0.15, D / 2 - 0.04], "stone_dressed", { bv: 0.012, k: 0 });
  }
  A.box([-W / 2 + 0.05, -0.1, -D / 2 + 0.05], [W / 2 - 0.05, 0.12, D / 2 - 0.05], "earth", { k: 0 });
  const R = rng(4403), cols = Object.values(pet);
  for (let i = 0; i < 16; i++) {
    const x = -W / 2 + 0.22 + (i % 8) * ((W - 0.44) / 7) + (R() - 0.5) * 0.1, z = (i < 8 ? -0.2 : 0.2) + (R() - 0.5) * 0.12;
    const h = 0.18 + R() * 0.22, w = 0.2 + R() * 0.12;
    A.cone([x, 0.11 + h / 2, z], [w, h, w], R() < 0.5 ? leaf : leaf2, { top: 0.55, n: R() < 0.5 ? 0 : 8, k: 0 });
    const c = cols[Math.floor(R() * cols.length)];
    for (let k = 0, n = 3 + Math.floor(R() * 3); k < n; k++) {
      const a = R() * 6.283, d = w * 0.3 * R(), y = 0.11 + h + R() * 0.08;
      A.cyl([x + Math.cos(a) * d, y, z + Math.sin(a) * d], [0.06 + R() * 0.03, 0.03, 0.06 + R() * 0.03], c, { seg: 8, k: 0 });
    }
  }
  setNav(A, { effect: "none" });
  return A;
}

// ============================================================================ hay bale stack
export function hayStack() {
  const A = base({ id: "drs_hay_bale_stack", name: "Hay bale stack", footprint: [3.2, 1.2],
    description: "Pyramid of twine-bound square hay bales (3-2-1, two deep). Stops a rifle round at the base; low cover." });
  const H = hay(A), tw = ensure(A, "twine", [0.3, 0.24, 0.14], 0.9, "plain"), R = rng(4404);
  const bw = 0.95, bh = 0.45, bd = 0.5;
  [[3, 0], [2, 1], [1, 2]].forEach(([n, row]) => {
    for (let i = 0; i < n; i++) for (const z of [-bd / 2 - 0.01, bd / 2 + 0.01]) {
      if (row === 2 && z < 0 && R() < 0.5) continue;
      const x = (i - (n - 1) / 2) * (bw + 0.02) + (R() - 0.5) * 0.06, y = row * (bh - 0.01), yaw = (R() - 0.5) * 6;
      A.rbox([x - bw / 2, y, z - bd / 2], [x + bw / 2, y + bh, z + bd / 2], 0.06, H, { r: [0, yaw, 0] });
      for (const f of [-0.25, 0.25]) A.obox([x + f * bw, y + bh / 2, z], [0.02, bh + 0.012, bd + 0.012], [0, yaw, 0], tw, { k: 0 });
    }
  });
  for (let i = 0; i < 9; i++) A.obox([(R() - 0.5) * 3.0, 0.01, (R() - 0.5) * 1.6 + (R() < 0.5 ? 0.9 : -0.9)], [0.2 + R() * 0.3, 0.02, 0.05 + R() * 0.1], [0, R() * 180, 0], H, { k: 0 });   // loose straw
  setNav(A, { effect: "carve", cover: [cover([-0.9, 0, -1.0], 0), cover([0.9, 0, -1.0], 0), cover([-0.9, 0, 1.0], 180), cover([0.9, 0, 1.0], 180)] });
  return A;
}

// ============================================================================ woodpile with a lean-to roof and chopping block
export function woodpile() {
  const A = base({ id: "drs_woodpile", name: "Woodpile", footprint: [3.4, 1.3],
    description: "Split logs stacked between oak posts under a plank lean-to, with a chopping block and axe beside it." });
  const R = rng(4405), L = 2.2, D = 0.55;
  for (const x of [-L / 2 - 0.08, L / 2 + 0.08]) for (const z of [-0.3, 0.3]) A.box([x - 0.06, -0.3, z - 0.06], [x + 0.06, z < 0 ? 1.62 : 1.42, z + 0.06], "oak_y", { bv: 0.008, k: 0 });
  for (const x of [-L / 2 - 0.08, L / 2 + 0.08]) A.box([x - 0.07, 1.36, -0.4], [x + 0.07, 1.46, 0.4], "oak_z", { k: 0 });
  A.obox([0, 1.56, 0.0], [L + 0.6, 0.04, 1.0], [-14, 0, 0], "plank_x", { k: 0 });
  A.obox([0, 1.6, 0.0], [L + 0.64, 0.025, 0.08], [-14, 0, 0], "oak_x", { k: 0 });
  for (let row = 0, y = 0.02; y < 1.2; row++) {
    let x = -L / 2 + 0.02, rmax = 0;
    while (x < L / 2 - 0.1) {
      const d = 0.13 + R() * 0.07; rmax = Math.max(rmax, d);
      A.cyl([x + d / 2, y + d / 2, (R() - 0.5) * 0.06], [d, D + (R() - 0.5) * 0.08, d], R() < 0.3 ? "bark" : "log_z", { r: [90, 0, 0], seg: R() < 0.5 ? 6 : 10, k: 0 });
      x += d + 0.005;
    }
    y += rmax * 0.82;
  }
  A.box([-L / 2 - 0.1, 0, -D / 2], [L / 2 + 0.1, 1.2, D / 2], "log_z", { v: 0 });
  A.cyl([1.75, 0.22, 0.15], [0.46, 0.46, 0.46], "log_z", { seg: 12 });
  A.obox([1.68, 0.62, 0.12], [0.04, 0.6, 0.035], [0, 30, -32], "oak_y", { k: 0 });
  A.obox([1.83, 0.46, 0.21], [0.13, 0.06, 0.02], [0, 30, -32], "steel_bare", { k: 0 });
  for (let i = 0; i < 6; i++) A.obox([1.2 + R() * 1.2, 0.03, -0.3 + R() * 0.9], [0.12 + R() * 0.15, 0.04, 0.05], [0, R() * 180, 0], "log_z", { k: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.0], 0, "low", ["left", "right"])] });
  return A;
}

// ============================================================================ well
export function well() {
  const A = base({ id: "drs_well", name: "Stone well", footprint: [1.9, 1.9],
    description: "Round farmyard well: a rubble drum with a dressed coping, oak posts carrying a winch roller and crank, a slate hood and a bucket on its rope." });
  const water = ensure(A, "well_dark", [0.012, 0.016, 0.014], 0.1, "plain");
  A.cyl([0, 0.35, 0], [1.5, 1.0, 1.5], "stone", { seg: 32 });
  A.cyl([0, 0.885, 0], [1.64, 0.1, 1.64], "stone_dressed", { seg: 32 });
  A.cyl([0, 0.94, 0], [1.08, 0.012, 1.08], water, { seg: 28, k: 0 });
  A.cyl([0, -0.25, 0], [1.6, 0.3, 1.6], "stone_plinth", { seg: 24 });
  for (const sx of [-1, 1]) A.box([sx * 0.7 - 0.07, 0.7, -0.07], [sx * 0.7 + 0.07, 2.32, 0.07], "oak_y", { bv: 0.01 });
  A.cyl([0, 1.75, 0], [0.16, 1.5, 0.16], "oak_x", { r: [0, 0, 90], k: 0 });
  A.box([0.78, 1.73, -0.02], [0.82, 1.77, 0.02], "iron", { k: 0 });
  A.box([0.8, 1.6, -0.02], [0.96, 1.64, 0.02], "iron", { k: 0 });
  A.obox([0.9, 1.68, 0], [0.04, 0.2, 0.04], [0, 0, 0], "iron", { k: 0 });
  A.cyl([0.12, 1.42, 0], [0.018, 0.62, 0.018], "sacking", { n: 4, k: 0 });
  A.cone([0.12, 1.0, 0], [0.3, 0.3, 0.3], "oak_y", { top: 0.75, r: [180, 0, 0], k: 0 });
  A.cyl([0.12, 1.12, 0], [0.31, 0.03, 0.31], "iron", { k: 0 });
  A.box([-0.85, 2.3, -0.1], [0.85, 2.38, 0.1], "oak_x", { bv: 0.01, k: 0 });
  for (const s of [1, -1]) A.obox([0, 2.5, s * 0.32], [1.9, 0.05, 0.78], [s * 33, 0, 0], "slate", { k: 0 });
  A.cyl([0, 2.71, 0], [0.12, 1.96, 0.12], "ridge_tile", { r: [0, 0, 90], k: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.3], 0, "low", ["left", "right"])] });
  weather(A);
  return A;
}

// ============================================================================ gravestones (3) and the churchyard cross
function plot(A, R, len = 1.9) {   // a grave mound in front of the stone with a scatter of grass-dark earth
  A.rbox([-0.38, -0.2, 0.15], [0.38, 0.1 + R() * 0.05, 0.15 + len], 0.1, "earth", { k: 0 });
}
export function gravestoneRound() {
  const A = base({ id: "drs_gravestone_round", name: "Gravestone (round-headed)", category: "Dressing", footprint: [0.9, 2.2],
    description: "Weathered round-headed limestone headstone on a chamfered base, its grave mound in front (+Z)." });
  const R = rng(4406);
  A.box([-0.38, -0.2, -0.16], [0.38, 0.14, 0.16], "stone_plinth", { bv: 0.02 });
  A.box([-0.28, 0.13, -0.06], [0.28, 0.82, 0.06], "stone_dressed", { bv: 0.012 });
  A.arch([0, 0.81 + 0.14, 0], [0.56, 0.28, 0.12], "stone_dressed", { th: 99 });
  A.box([-0.2, 0.55, 0.055], [0.2, 0.57, 0.066], "stone_plinth", { k: 0 });
  A.box([-0.16, 0.48, 0.055], [0.16, 0.495, 0.066], "stone_plinth", { k: 0 });
  plot(A, R);
  setNav(A, { effect: "carve", cover: [cover([0, 0, -0.6], 0, "low", ["left", "right"])] });
  weather(A);
  return A;
}
export function gravestoneCross() {
  const A = base({ id: "drs_gravestone_cross", name: "Gravestone (cross in a kerbed plot)", footprint: [1.0, 2.3],
    description: "Stone cross on a stepped base at the head of a kerbed plot filled with grey chippings." });
  A.box([-0.32, -0.15, -0.2], [0.32, 0.14, 0.2], "stone_plinth", { bv: 0.02 });
  A.box([-0.2, 0.13, -0.13], [0.2, 0.3, 0.13], "stone_dressed", { bv: 0.015 });
  A.box([-0.065, 0.29, -0.055], [0.065, 1.32, 0.055], "stone_dressed", { bv: 0.01 });
  A.box([-0.3, 0.94, -0.05], [0.3, 1.07, 0.05], "stone_dressed", { bv: 0.01 });
  for (const s of [-1, 1]) A.box([s > 0 ? 0.36 : -0.46, -0.1, 0.2], [s > 0 ? 0.46 : -0.36, 0.14, 2.2], "stone_dressed", { bv: 0.012, k: 0 });
  A.box([-0.46, -0.1, 2.1], [0.46, 0.14, 2.2], "stone_dressed", { bv: 0.012, k: 0 });
  A.box([-0.37, -0.1, 0.2], [0.37, 0.07, 2.11], ensure(A, "chippings", [0.4, 0.39, 0.36], 0.95, "stone"), { k: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -0.6], 0, "low", ["left", "right"])] });
  weather(A);
  return A;
}
export function tombChest() {
  const A = base({ id: "drs_tomb_chest", name: "Tomb chest", footprint: [1.1, 2.2],
    description: "Family tomb chest: a panelled limestone chest under a moulded ledger slab, on a chamfered plinth. Waist-high hard cover." });
  A.box([-0.52, -0.2, -1.07], [0.52, 0.12, 1.07], "stone_plinth", { bv: 0.025 });
  A.box([-0.42, 0.11, -0.97], [0.42, 0.72, 0.97], "stone_dressed", { bv: 0.015 });
  A.box([-0.49, 0.71, -1.04], [0.49, 0.8, 1.04], "stone_dressed", { bv: 0.025 });
  A.box([-0.46, 0.79, -1.0], [0.46, 0.83, 1.0], "stone_dressed", { bv: 0.015 });
  for (const s of [-1, 1]) for (const z of [-0.6, 0, 0.6]) A.box([s * 0.41, 0.22, z - 0.22], [s * 0.432, 0.6, z + 0.22], "stone", { bv: 0.006, k: 0 });   // sunk side panels
  setNav(A, { effect: "carve", cover: [cover([0.9, 0, 0], 270, "low", ["over", "left", "right"]), cover([-0.9, 0, 0], 90, "low", ["over", "left", "right"])] });
  weather(A);
  return A;
}
export function churchyardCross() {
  const A = base({ id: "drs_churchyard_cross", name: "Churchyard cross", footprint: [1.8, 1.8],
    description: "Calvary cross of the churchyard: three chamfered steps, a socket stone, an octagonal tapering shaft and a wheel-head cross, 4.6 m high." });
  [[1.7, 0.0], [1.25, 0.2], [0.85, 0.4]].forEach(([w, y]) => A.box([-w / 2, y - (y ? 0.01 : 0.25), -w / 2], [w / 2, y + 0.2, w / 2], "stone_dressed", { bv: 0.025 }));
  A.box([-0.27, 0.59, -0.27], [0.27, 0.95, 0.27], "stone_dressed", { bv: 0.03 });
  A.cone([0, 0.94 + 1.4, 0], [0.26, 2.8, 0.26], "stone_dressed", { n: 8, top: 0.7 });
  A.box([-0.12, 3.72, -0.12], [0.12, 3.82, 0.12], "stone_dressed", { bv: 0.012 });
  A.box([-0.085, 3.81, -0.07], [0.085, 4.62, 0.07], "stone_dressed", { bv: 0.012 });
  A.box([-0.36, 4.12, -0.065], [0.36, 4.28, 0.065], "stone_dressed", { bv: 0.012 });
  A.arch([0, 4.2 + 0.13, 0], [0.5, 0.26, 0.09], "stone_dressed", { th: 0.05, k: 0 });
  A.arch([0, 4.2 - 0.13, 0], [0.5, 0.26, 0.09], "stone_dressed", { th: 0.05, r: [0, 0, 180], k: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.3], 0, "low", ["left", "right"]), cover([0, 0, 1.3], 180, "low", ["left", "right"])] });
  weather(A);
  return A;
}

// ============================================================================ stone arched bridges (1 and 3 arches)
// The road runs along X; the arches span along X between piers, the stream runs along Z under them. y = 0 is the road
// at the bridge ends; the springings are 1.7 m below, foundations 3.2 m below (set it over a stream channel). Ramps
// at both ends fall from the deck to the road.
function bridge({ id, name, spans, W = 4.4 }) {
  const th = 0.45, ys = -1.7, crown = 0.5, P = 1.3, AB = 1.8, PAR = 0.35, PH = 0.95, RAMP = 3.6;
  const total = spans.reduce((s, v) => s + v, 0) + P * (spans.length - 1), half = total / 2 + AB;
  const yd = ys + Math.max(...spans) / 2 + crown;
  const A = base({ id, name, category: "Dressing", footprint: [r3(2 * (half + RAMP)), W + 0.4], maxGrade: 1.6,
    description: `${name}: rubble-limestone ${spans.length > 1 ? `${spans.length}-arch` : "single-arch"} road bridge with dressed voussoir rings, ${spans.length > 1 ? "cutwaters on the piers, " : ""}a string course at deck level and parapets with coping. The arches spring 1.7 m below the road; ramps fall to the road at both ends. Place it over a stream channel.` ,
    ivy: [{ face: "+z", a0: -half, a1: -half + AB, y0: -1.5, y1: yd, density: 0.7 }, { face: "-z", a0: half - AB, a1: half, y0: -1.5, y1: yd, density: 0.6 }] });
  const cob = ensure(A, "setts", [0.24, 0.23, 0.21], 0.85, "stone");
  // arches and piers along x
  let x = -total / 2;
  spans.forEach((S, i) => {
    const xc = x + S / 2, R = S / 2;
    A.arch([xc, ys + (R + th) / 2, 0], [S + 2 * th, R + th, W + 0.08], "stone_dressed", { th });
    A.arch([xc, (ys + yd) / 2, 0], [S + 0.04, yd - ys, W], "stone", { th: yd - ys - R - 0.02, fill: true });
    x += S;
    if (i + 1 < spans.length) {   // pier with cutwaters both faces (diamond nose, pyramid cap)
      A.box([x - 0.01, -3.2, -W / 2], [x + P + 0.01, yd, W / 2], "stone");
      for (const s of [-1, 1]) {
        const nz = s * W / 2, nh = ys + 0.9 + 3.2;
        A.obox([x + P / 2, -3.2 + nh / 2, nz], [P * 0.707, nh, P * 0.707], [0, 45, 0], "stone_dressed", { bv: 0.02 });
        A.cone([x + P / 2, ys + 0.9 + 0.35, nz], [P * 0.71, 0.7, P * 0.71], "stone_dressed", { n: 4, r: [0, 45, 0] });
      }
      x += P;
    }
  });
  for (const s of [-1, 1]) {   // abutments
    const x0 = s > 0 ? total / 2 - 0.01 : -half, x1 = s > 0 ? half : -total / 2 + 0.01;
    A.box([x0, -3.2, -W / 2], [x1, yd, W / 2], "stone");
    A.box([s > 0 ? half - 0.3 : -half - 0.02, -3.2, -W / 2 - 0.35], [s > 0 ? half + 0.02 : -half + 0.3, yd - 0.3, W / 2 + 0.35], "stone_dressed", { bv: 0.02 });   // end pilasters
  }
  A.box([-half, -3.25, -W / 2 - 0.15], [half, -2.9, W / 2 + 0.15], "stone_plinth", { bv: 0.03 });
  // deck, string courses, parapets with coping
  A.box([-half, yd - 0.01, -W / 2 + PAR], [half, yd + 0.06, W / 2 - PAR], cob);
  for (const s of [-1, 1]) {
    A.box([-half - 0.04, yd - 0.22, s > 0 ? W / 2 - 0.02 : -W / 2 - 0.08], [half + 0.04, yd - 0.04, s > 0 ? W / 2 + 0.08 : -W / 2 + 0.02], "stone_dressed", { bv: 0.015, k: 0 });
    const z0 = s > 0 ? W / 2 - PAR : -W / 2, z1 = s > 0 ? W / 2 : -W / 2 + PAR;
    A.box([-half, yd - 0.02, z0], [half, yd + PH, z1], "stone");
    A.box([-half - 0.02, yd + PH - 0.01, z0 - 0.05], [half + 0.02, yd + PH + 0.12, z1 + 0.05], "stone_dressed", { bv: 0.025 });
  }
  // ramps: a wedge of fill falling from the deck to the road, with sloped parapets
  const ang = Math.atan2(yd + 0.06, RAMP), deg = (ang * 180) / Math.PI, Lp = Math.hypot(RAMP, yd + 0.06);
  for (const s of [-1, 1]) {
    const xm = s * (half + RAMP / 2);
    A.wedge([xm, (yd + 0.06 - 0.4) / 2, 0], [W, yd + 0.06 + 0.4, RAMP + 0.02], cob, { r: [0, s * 90, 0], top: 0.4 / (yd + 0.46) });
    A.box([s > 0 ? half - 0.01 : -half - RAMP, -1.2, -W / 2], [s > 0 ? half + RAMP : -half + 0.01, -0.39, W / 2], "stone");   // footing under the ramp
    for (const zz of [-1, 1]) {
      const zc = zz * (W / 2 - PAR / 2);
      A.obox([xm, (yd + 0.06) / 2 + PH / 2 - 0.1, zc], [Lp + 0.02, PH + 0.2, PAR], [0, 0, -s * deg], "stone");
      A.obox([xm, (yd + 0.06) / 2 + PH + 0.04, zc], [Lp + 0.04, 0.12, PAR + 0.1], [0, 0, -s * deg], "stone_dressed", { bv: 0.025 });
      A.box([s * (half + RAMP) - 0.3, -0.3, zc - 0.25], [s * (half + RAMP) + 0.3, 1.0, zc + 0.25], "stone_dressed", { bv: 0.03 });   // end newels
    }
  }
  setNav(A, { effect: "block", cover: [cover([-half + 0.8, yd, -W / 2 + PAR + 0.5], 180, "low", ["over"]), cover([half - 0.8, yd, W / 2 - PAR - 0.5], 0, "low", ["over"])] });
  weather(A, { keep: A.parts.filter((p) => p.m === cob || p.t !== "b") });
  return A;
}
export const bridgeSmall = () => bridge({ id: "drs_bridge_stone_small", name: "Stone arched bridge (single arch)", spans: [5.0] });
export const bridgeLarge = () => bridge({ id: "drs_bridge_stone_large", name: "Stone arched bridge (three arches)", spans: [4.4, 5.6, 4.4], W: 5.0 });

// ============================================================================ sandbag wall and ring
// a filled bag: a box chamfered 6 cm (mesh only), visual (each run collides through its own hidden boxes)
function bag(A, x, y, z, yaw, R, long = 0.62) {
  A.obox([x, y + 0.085, z], [long, 0.17, 0.36], [(R() - 0.5) * 4, yaw + (R() - 0.5) * 8, (R() - 0.5) * 4], "sacking", { bv: 0.06, k: 0 });
}
export function sandbagWall() {
  const L = 3.0;
  const A = base({ id: "drs_sandbag_wall", name: "Sandbag wall (3 m, chest high)", category: "Defences", footprint: [L, 0.85],
    description: "3 m breastwork of filled sandbags laid in stretcher bond, seven courses (1.15 m) two bags deep, tapering to one at the top. Stops small-arms fire." });
  const R = rng(4407);
  for (let c = 0; c < 7; c++) {
    const two = c < 5, off = c % 2 ? 0.31 : 0;
    for (let x = -L / 2 + 0.31 + off; x < L / 2 - 0.2; x += 0.62) for (const z of two ? [-0.19, 0.19] : [0]) bag(A, x, c * 0.165 - 0.02, z, 0, R);
  }
  A.box([-L / 2, 0, -0.38], [L / 2, 1.15, 0.38], "sacking", { v: 0 });
  A.meta.defense = { kind: "sandbag", hp: 360, stopsBullets: true, blastOcclusion: 0.12, maxSlopeDeg: 35, sockets: [{ p: [-L / 2, 0, 0], t: [-1, 0, 0] }, { p: [L / 2, 0, 0], t: [1, 0, 0] }] };
  setNav(A, { effect: "carve", cover: lineCover(L, 0.9) });
  return A;
}
export function sandbagRing() {
  const A = base({ id: "drs_sandbag_ring", name: "Sandbag ring (MG nest)", category: "Defences", footprint: [4.2, 4.2],
    description: "Circular sandbag nest for a machine gun or mortar team, 3.4 m across inside, 0.95 m high with an entry gap at the back (-Z)." });
  const R = rng(4408), r0 = 1.75;
  for (let c = 0; c < 6; c++) {
    const n = 18, off = c % 2 ? 0.5 : 0;
    for (let i = 0; i < n; i++) {
      const a = ((i + off) / n) * 2 * Math.PI, deg = (a * 180) / Math.PI;
      if (Math.abs(Math.sin(a / 2 + Math.PI / 2)) < 0.17) continue;   // the gap at -Z (a ~ 180 deg)
      for (const rr of c < 4 ? [r0, r0 + 0.36] : [r0 + 0.18]) bag(A, Math.sin(a) * rr, c * 0.165 - 0.02, Math.cos(a) * rr, deg, R, 0.58);
    }
  }
  for (let i = 0; i < 12; i++) {   // collider: a ring of boxes with the same gap
    const a = ((i + 0.5) / 12) * 2 * Math.PI;
    if (Math.abs(Math.sin(a / 2 + Math.PI / 2)) < 0.2) continue;
    A.obox([Math.sin(a) * (r0 + 0.18), 0.48, Math.cos(a) * (r0 + 0.18)], [1.0, 0.96, 0.72], [0, (a * 180) / Math.PI, 0], "sacking", { v: 0 });
  }
  A.meta.defense = { kind: "sandbag", hp: 360, stopsBullets: true, blastOcclusion: 0.12, maxSlopeDeg: 30 };
  setNav(A, { effect: "carve", cover: [cover([0, 0, 1.2], 0, "low", ["over"]), cover([1.1, 0, 0.6], 60, "low", ["over"]), cover([-1.1, 0, 0.6], -60, "low", ["over"])] });
  return A;
}

// ============================================================================ wooden X anti-tank obstacle
export function woodenX() {
  const A = base({ id: "drs_wooden_x_obstacle", name: "Wooden X obstacle (anti-tank)", category: "Defences", footprint: [2.6, 2.2],
    description: "Two X-frames of 25 cm logs, 1.6 m high, bolted to a ridge log and braced at the foot: the timber road block of the town approaches. Stops vehicles; low cover." });
  const R = rng(4409), log = "log_x", d = 0.25, H = 1.6;
  for (const z of [-0.85, 0.85]) {
    for (const s of [-1, 1]) A.cyl([0, H / 2, z], [d, Math.hypot(H, 2.0) + 0.1, d], s > 0 ? log : "bark", { r: [0, 0, s * 51 + (R() - 0.5) * 3], seg: 10 });
    A.cyl([0, H / 2, z + Math.sign(z) * 0.14], [0.06, 0.32, 0.06], "iron", { r: [90, 0, 0], k: 0 });
  }
  A.cyl([0, H / 2 + 0.17, 0], [d * 0.9, 2.3, d * 0.9], log, { r: [90, 0, 0], seg: 10 });
  for (const s of [-1, 1]) A.cyl([s * 0.95, 0.14, 0], [0.18, 2.2, 0.18], "bark", { r: [90, 0, 0], seg: 8 });
  for (const s of [-1, 1]) A.obox([s * 0.5, 0.55, 0], [0.12, 0.12, 2.0], [s * 30, 0, 0], "plank_z", { k: 0 });
  A.meta.defense = { kind: "tank_trap", hp: 900, smallArmsMultiplier: 0.05, blocksVehicles: true, maxSlopeDeg: 30 };
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.5], 0, "low", ["left", "right"]), cover([0, 0, 1.5], 180, "low", ["left", "right"])] });
  return A;
}

// ============================================================================ barrels and cart
// oak cask: two frustums meeting at the bilge, iron hoops; standing, or lying along x
function cask(A, x, z, y, { r = 0.3, h = 0.86, lying = false, mat = "oak_y" } = {}) {
  const at = (t) => (lying ? [x + (t - 0.5) * h, y + r, z] : [x, y + t * h, z]);
  const lo = lying ? [0, 0, 90] : [180, 0, 0], hi = lying ? [0, 0, -90] : [0, 0, 0], ax = lying ? [0, 0, 90] : [0, 0, 0];
  A.cone(at(0.25), [2 * r, h / 2 + 0.002, 2 * r], mat, { top: 0.86, r: lo, k: 0 });
  A.cone(at(0.75), [2 * r, h / 2 + 0.002, 2 * r], mat, { top: 0.86, r: hi, k: 0 });
  for (const t of [0.08, 0.36, 0.64, 0.92]) { const d = 2 * r * (1 - 0.14 * Math.abs(t - 0.5) * 2) + 0.014; A.cyl(at(t), [d, 0.035, d], "iron", { r: ax, k: 0 }); }
  A.cyl(at(0.5), [2 * r * 0.93, h, 2 * r * 0.93], mat, { r: ax, v: 0 });
}
export function barrels() {
  const A = base({ id: "drs_barrels", name: "Barrels (group)", footprint: [2.0, 1.4],
    description: "Two oak cider casks standing and one lying chocked on its side, with a rusty oil drum: yards, inns and depots." });
  cask(A, -0.55, -0.25, 0);
  cask(A, 0.12, -0.32, 0, { r: 0.28, h: 0.8 });
  cask(A, -0.2, 0.42, 0, { lying: true });
  for (const s of [-1, 1]) A.wedge([-0.2, 0.05, 0.42 + s * 0.24], [0.12, 0.1, 0.12], "oak_x", { r: [0, s > 0 ? 180 : 0, 0], k: 0 });   // chocks
  const rust = ensure(A, "drum_rust", [0.17, 0.08, 0.04], 0.8, "metal", { metallic: 0.3 });
  A.cyl([0.72, 0.44, 0.15], [0.58, 0.88, 0.58], rust, { seg: 20 });
  for (const y of [0.29, 0.59]) A.cyl([0.72, y, 0.15], [0.6, 0.03, 0.6], rust, { k: 0 });
  A.cyl([0.62, 0.885, 0.1], [0.06, 0.012, 0.06], "steel_bare", { k: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.0], 0, "low", ["left", "right"])] });
  return A;
}
export function cart() {
  const A = base({ id: "drs_cart", name: "Farm cart", footprint: [3.8, 1.9],
    description: "Two-wheeled Norman farm cart (charrette) resting on its shafts: plank bed with rails, spoked wheels with iron tyres, a few sacks in the back." });
  const R = rng(4411), wr = 0.62, ax = wr + 0.02, tn = (ax - 0.02) / 2.2, tilt = (Math.atan(tn) * 180) / Math.PI;
  for (const s of [-1, 1]) {   // wheels at z = +-0.82, the axle along z
    const z = s * 0.82;
    A.cyl([0, ax, z], [0.22, 0.2, 0.22], "oak_y", { r: [90, 0, 0], k: 0 });
    for (let i = 0; i < 12; i++) {
      const a = (i / 12) * 360, t = (a * Math.PI) / 180, at = (rr) => [Math.sin(t) * rr, ax + Math.cos(t) * rr, z];
      A.obox(at(wr * 0.52), [0.045, wr * 0.9, 0.04], [0, 0, -a], "oak_y", { k: 0 });
      A.obox(at(wr - 0.04), [0.34, 0.08, 0.075], [0, 0, -a], "oak_x", { k: 0 });
      A.obox(at(wr - 0.003), [0.33, 0.012, 0.08], [0, 0, -a], "iron", { k: 0 });
    }
  }
  A.cyl([0, ax, 0], [0.09, 1.95, 0.09], "oak_x", { r: [90, 0, 0], k: 0 });
  // the bed pitches forward about the axle onto the shafts, whose tips rest on the ground at x = 2.2
  const bed = ([x, y, z], s, m) => A.obox([x, ax + y - x * tn, z], s, [0, 0, -tilt], m, { k: 0 });
  bed([-0.2, 0.12, 0], [2.4, 0.06, 1.3], "plank_x");
  for (const s of [-1, 1]) {
    bed([-0.2, 0.38, s * 0.66], [2.4, 0.06, 0.05], "plank_x");
    bed([-0.2, 0.26, s * 0.66], [2.4, 0.18, 0.03], "plank_x");
    for (const x of [-1.25, -0.4, 0.4, 0.95]) bed([x, 0.27, s * 0.66], [0.05, 0.36, 0.06], "oak_y");
    bed([0.4, 0.03, s * 0.55], [3.6, 0.09, 0.09], "oak_x");
  }
  bed([-1.4, 0.27, 0], [0.05, 0.3, 1.3], "plank_z");
  for (const [x, z] of [[-0.6, -0.2], [-0.15, 0.25]]) A.rbox([x - 0.36, ax + 0.15 - x * tn, z - 0.22], [x + 0.36, ax + 0.43 - x * tn, z + 0.22], 0.09, "sacking", { r: [0, (R() - 0.5) * 30, -tilt], k: 0 });
  A.box([-1.45, 0, -0.95], [2.2, ax + 0.5, 0.95], "oak_x", { v: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.5], 0, "low", ["left", "right"]), cover([0, 0, 1.5], 180, "low", ["left", "right"])] });
  return A;
}

export function dressingAssets() {
  return [gardenWall(), gatePiers(), picketFence(), flowerBed(), hayStack(), woodpile(), well(), gravestoneRound(), gravestoneCross(), tombChest(),
    churchyardCross(), bridgeSmall(), bridgeLarge(), sandbagWall(), sandbagRing(), woodenX(), barrels(), cart()];
}
