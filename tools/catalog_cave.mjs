// CV1 cave kit (Caverns map, tools/mapgen/maps/caverns.py): ceiling lids, stalactites / stalagmites / columns, ribbed
// "organ" walls, a spine arch, an ancient spire, rock shelves and the cave's light emitters.
// Everything rocky is built from the nature "rock" block (docs/nature.md): noise-displaced, plane-cut icosphere hulls,
// each piece its own convex collider. A piece turned over (tilt [180, 0]) hangs its lumpy dome down from a flat top:
// ceilings and stalactites. Standalone and deterministic: `node tools/catalog_cave.mjs` rewrites only
// content/assets/cave_*.json (never run tools/build_assets.mjs for this).
import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "content", "assets");
const base = (id, name, description, footprint, extra, category = "Cave") => ({
  id, version: 1, name, category, description, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 6,
  materials: [], parts: [], furniture: [], interactables: [], panels: [], ...extra,
});
const write = (a) => { writeFileSync(join(OUT, a.id + ".json"), JSON.stringify(a) + "\n"); return a.id; };
const ids = [];
const piece = (lith, seed, size, pos = [0, 0, 0], yaw = 0, tilt = [0, 0], sink = 0.25, detail) =>
  detail === undefined ? { lith, seed, size, pos, yaw, tilt, sink } : { lith, seed, size, pos, yaw, tilt, sink, detail };
const rock = (id, name, desc, pieces, fp, nav = "block", collide = true, category = "Cave") => ids.push(write(base(id, name, desc, fp, { rock: { pieces, collide }, nav: { effect: nav } }, category)));
let S = 1;   // deterministic pseudo-random for the layouts below
const rnd = () => { S = (S * 16807) % 2147483647; return (S - 1) / 2147483646; };
const lerp = (a, b, t) => a + (b - a) * t;
const r2 = (v) => Math.round(v * 100) / 100;

// ---------------------------------------------------------------- ceiling lids (flat top at the instance y, dome hanging below)
for (const [sfx, sz, seeds] of [["l", [42, 16, 40], [501, 502, 503]], ["m", [28, 11, 27], [511, 512, 513]], ["s", [16, 7, 15], [521, 522]]]) {
  seeds.forEach((sd, k) => rock(`cave_lid_${sfx}${k}`, `Cave ceiling (${sfx === "l" ? "large" : sfx === "m" ? "medium" : "small"} ${k + 1})`,
    "A slab of cave roof hung from a flat top: a lumpy, fractured underside about " + Math.round(sz[1] * 0.78) + " m deep. Placed overlapping to close a cavern.",
    [piece("dripstone", sd, sz, [0, 0, 0], 0, [180, 0], 0, 3)], [sz[0], sz[2]], "block", true, "Cave roof"));
}

// ---------------------------------------------------------------- stalactites (hang from y = 0 down), stalagmites (rise from the floor), columns
// a dripstone spike: three stacked pieces narrowing to a point (a single stretched piece reads as a capsule).
// dir +1 grows up from y0 (stalagmite), -1 hangs down from y0 (stalactite, pieces turned over)
const spike = (P, seed, x, z, y0, h, w, dir, yaw, lean) => {
  const segs = [[1.0, 0.0, 0.62], [0.68, 0.26, 0.56], [0.42, 0.5, 0.5], [0.2, 0.72, 0.36]];   // each piece starts well inside the one below: no ledges
  segs.forEach(([ws, at, hs], k) => P.push(piece("limestone", seed + k, [r2(w * ws), r2(h * hs), r2(w * ws * 0.9)],
    [r2(x + lean[0] * at * h), r2(y0 + dir * at * h), r2(z + lean[1] * at * h)], (yaw + k * 40) % 360, dir > 0 ? [0, 0] : [180, 0], 0, k === 0 ? 2 : 1)));
};
for (let v = 0; v < 4; v++) {
  S = 900 + v * 37;
  const P = [], n = 4 + v * 2;
  for (let k = 0; k < n; k++) {
    const a = rnd() * 6.283, d = k === 0 ? 0 : 0.8 + rnd() * (1.6 + v * 0.8), h = (k === 0 ? 1 : 0.35 + rnd() * 0.6) * (4 + v * 2.5), w = 0.45 + h * (0.12 + rnd() * 0.06);
    spike(P, 1000 + v * 50 + k * 3, r2(Math.cos(a) * d), r2(Math.sin(a) * d), 0.6, h, w, -1, Math.round(rnd() * 360), [(rnd() - 0.5) * 0.08, (rnd() - 0.5) * 0.08]);
  }
  rock(`cave_stalactites_${v}`, `Stalactites (cluster ${v + 1})`, `A cluster of ${n} dripstone stalactites up to ${Math.round(4 + v * 2.5)} m long, hung from the cave roof.`, P, [4 + v * 2, 4 + v * 2]);
}
for (let v = 0; v < 4; v++) {
  S = 1700 + v * 41;
  const P = [], n = 3 + v * 2;
  for (let k = 0; k < n; k++) {
    const a = rnd() * 6.283, d = k === 0 ? 0 : 0.9 + rnd() * (1.4 + v * 0.7), h = (k === 0 ? 1 : 0.3 + rnd() * 0.6) * (2.5 + v * 1.8), w = 0.8 + h * (0.2 + rnd() * 0.1);
    spike(P, 1300 + v * 50 + k * 3, r2(Math.cos(a) * d), r2(Math.sin(a) * d), -0.25, h, w, 1, Math.round(rnd() * 360), [(rnd() - 0.5) * 0.12, (rnd() - 0.5) * 0.12]);
  }
  rock(`cave_stalagmites_${v}`, `Stalagmites (cluster ${v + 1})`, `${n} dripstone stalagmites up to ${Math.round(2.5 + v * 1.8)} m: cover that breaks sightlines.`, P, [3 + v * 2, 3 + v * 2]);
}
for (const [sfx, H] of [["m", 12], ["l", 20], ["xl", 30], ["h45", 45], ["h60", 60], ["h75", 75]]) {   // a stalagmite and a stalactite grown together, waisted where they meet
  const w = 2.4 + H * 0.11;
  S = 1650 + H;
  const P = [piece("dripstone", 1600 + H, [w, H * 0.62, w * 0.9], [0, 0, 0], 0, [0, 0], 0.05, 3),
     piece("dripstone", 1601 + H, [w * 0.95, H * 0.6, w], [0, H, 0], 70, [180, 0], 0.0, 3),
     piece("dripstone", 1602 + H, [w * 1.9, H * 0.12, w * 1.7], [0, 0, 0], 20, [0, 0], 0.3, 2),
     piece("dripstone", 1603 + H, [w * 1.7, H * 0.12, w * 1.9], [0, H + 0.6, 0], 140, [180, 0], 0, 2)];
  if (H >= 30) for (let k = 0; k < 9; k++) {   // fluting: thin ribs running up from the foot and down from the head
    const a = k / 9 * 6.283 + rnd() * 0.3, up = k % 2 === 0;
    P.push(piece("dripstone", 1700 + H + k, [r2(w * (0.18 + rnd() * 0.1)), r2(H * (0.3 + rnd() * 0.2)), r2(w * 0.2)], [r2(Math.cos(a) * w * 0.42), up ? 0 : H, r2(Math.sin(a) * w * 0.42)], Math.round(a * 57.3), up ? [0, 0] : [180, 0], 0.02, 2));
  }
  rock(`cave_column_${sfx}`, `Dripstone column (${H} m)`, `A ${H} m dripstone column: a stalagmite and a stalactite fused at the waist, flared at floor and roof${H >= 30 ? ", fluted" : ""}.`, P, [w * 1.9, w * 1.9]);
}

// ---------------------------------------------------------------- the unsettling formations
for (let v = 0; v < 3; v++) {   // ribbed organ wall: tall laminated ribs on a concave arc (faces local -z), each curving inward as it rises, like a vast ribcage
  S = 2100 + v * 53;
  const P = [], R = 7 + v * 2.5, n = 11 + v * 3, span = 1.9;
  for (let k = 0; k < n; k++) {
    const t = k / (n - 1), a = (t - 0.5) * span, x = Math.sin(a) * R, z = Math.cos(a) * R - R;
    const h = (10 + v * 3) * (0.55 + 0.45 * Math.sin(t * Math.PI)) * (0.85 + rnd() * 0.3), w = 1.1 + rnd() * 0.5;
    const inx = -Math.sin(a), inz = -Math.cos(a);   // toward the arc's centre
    for (let g = 0; g < 3; g++) {   // three long overlapping segments, narrowing and leaning further in
      const f = g / 3, y = h * f * 0.8, lean = 0.35 * h * f * f;
      P.push(piece("schist", 2200 + v * 120 + k * 5 + g, [r2(w * (1 - f * 0.5)), r2(h * 0.62), r2((1.3 + rnd() * 0.4) * (1 - f * 0.4))],
        [r2(x + inx * lean), r2(y), r2(z + inz * lean)], Math.round(a * 57.3), [Math.round(-6 - 16 * f), 0], 0.02, 1));
    }
    if (k < n - 1) P.push(piece("schist", 2300 + v * 60 + k, [r2(0.6 + rnd() * 0.3), r2(h * (0.3 + rnd() * 0.2)), 0.9], [r2(Math.sin(a + span / (2 * (n - 1))) * (R + 0.6)), 0, r2(Math.cos(a + span / (2 * (n - 1))) * (R + 0.6) - R)], Math.round(a * 57.3), [0, 0], 0.04, 1));
  }
  rock(`cave_ribwall_${v}`, `Ribbed rock wall (${v + 1})`, `A concave wall of ${n} tall laminated rock ribs curving inward, ${Math.round(10 + v * 3)} m high: an ancient-looking organ / ribcage formation.`, P, [R * 2, R * 0.9]);
}
{   // spine arch: a natural bridge of vertebra-like blocks over a 14 m span, knobbed along its crest
  S = 2600;
  const P = [], span = 14, rise = 9, n = 13;
  for (let k = 0; k < n; k++) {
    const t = k / (n - 1), a = Math.PI * t, x = -Math.cos(a) * span / 2, y = Math.sin(a) * rise, sz = 3.2 - Math.sin(a) * 1.1;
    P.push(piece("schist", 2610 + k, [r2(sz * 0.8), r2(sz * 0.9), r2(sz)], [r2(x), r2(y - (k === 0 || k === n - 1 ? 0 : sz * 0.45)), 0], 90, [0, Math.round(-90 + t * 180)], k === 0 || k === n - 1 ? 0.2 : 0, 2));
    if (k > 0 && k < n - 1) P.push(piece("schist", 2640 + k, [0.9, 1.6 + rnd() * 0.8, 0.9], [r2(x * 1.04), r2(y + sz * 0.35), 0], Math.round(rnd() * 360), [0, Math.round(-90 + t * 180)], 0, 1));
  }
  rock("cave_spine_arch", "Spine arch", "A 14 m natural arch of vertebra-like rock blocks, knobs along its crest, 9 m high: walk under it.", P, [span + 3, 4]);
}
{   // the ancient spire: a twisted, segmented column with rib flanges, the centre's landmark
  S = 2700;
  const P = [], n = 9;
  let y = 0;
  for (let k = 0; k < n; k++) {
    const s = lerp(3.6, 1.0, k / (n - 1)), h = lerp(4.6, 3.4, k / (n - 1));   // tall, overlapping segments: a column, not a cairn
    P.push(piece("schist", 2710 + k, [r2(s), r2(h), r2(s * 0.85)], [0, r2(y), 0], k * 23, [Math.round((rnd() - 0.5) * 6), Math.round((rnd() - 0.5) * 6)], k === 0 ? 0.15 : 0, 2));
    for (const side of [-1, 1]) if (k < n - 2) P.push(piece("schist", 2740 + k * 2 + (side > 0), [0.7, r2(h * 1.4), r2(s * 0.55)], [r2(side * s * 0.62), r2(y + 0.2), 0], k * 23, [0, side * 14], 0, 1));
    y += h * 0.55;
  }
  rock("cave_spire_ancient", "Ancient spire", `A ${Math.round(y)} m twisted, segmented rock spire with rib flanges: an unsettling, almost carved-looking formation.`, P, [5, 5]);
}

// ---------------------------------------------------------------- shelves, rubble
for (let v = 0; v < 3; v++) {
  rock(`cave_shelf_${v}`, `Rock shelf (${v + 1})`, "A flat-topped fallen limestone slab, waist to chest high: cover.",
    [piece("limestone", 2800 + v, [6 + v * 2, 1.6 + v * 0.35, 3.5 + v], [0, 0, 0], 0, [Math.round((v - 1) * 3), 2], 0.3, 2),
     piece("limestone", 2810 + v, [2.4, 1.2, 2], [r2(3 + v), 0, r2(1.2)], 40, [0, 0], 0.3, 1)], [7 + v * 2, 4.5 + v]);
}

// ---------------------------------------------------------------- light emitters (invisible fittings; the light is the asset)
const lightAsset = (id, name, desc, light, extraParts = [], materials = []) => ids.push(write(base(id, name, desc, [0.6, 0.6], {
  materials: [{ name: "none", albedo: [0, 0, 0], roughness: 1, pattern: "plain", metallic: 0, grain: 1 }, ...materials],
  parts: [{ t: "b", c: [0, 0.05, 0], s: [0.1, 0.1, 0.1], m: "none", v: 0, k: 0 }, ...extraParts], light, nav: { effect: "none" } })));
const crackParts = [{ t: "b", c: [0, -0.05, 0], s: [5.5, 0.06, 0.45], r: [0, 18, 0], m: "sky", k: 0 }, { t: "b", c: [1.6, -0.06, 0.9], s: [2.4, 0.06, 0.22], r: [0, 52, 0], m: "sky", k: 0 },
  { t: "b", c: [-1.9, -0.06, -0.5], s: [1.8, 0.06, 0.18], r: [0, -12, 0], m: "sky", k: 0 }];   // the crack itself: a cold, bright sliver of sky in the roof
const skyMat = [{ name: "sky", albedo: [0.5, 0.6, 0.75], roughness: 1, pattern: "plain", metallic: 0, grain: 1, emissive: [2.2, 2.7, 3.4] }];
lightAsset("cave_light_crack", "Cave light (ceiling crack)", "Cold, faint daylight falling through a crack in the cave roof: a narrow downward spot that casts shadows.",
  { color: [0.55, 0.68, 0.95], intensity: 2200, radius: 60, dir: [0, -1, 0], cone: [8, 22], shadow: 0, use: "electric", fx: "shaft", fxSize: 30 });
lightAsset("cave_light_crack_ns", "Cave light (ceiling crack, no shadow)", "Cold light through a crack in the cave roof, without a shadow map (cheaper).",
  { color: [0.55, 0.68, 0.95], intensity: 1700, radius: 55, dir: [0, -1, 0], cone: [5, 19], use: "electric", fx: "shaft", fxSize: 26 });
lightAsset("cave_light_cold", "Cave light (cold fill)", "A faint cold glow over water: no fitting, no shadow.",
  { color: [0.45, 0.6, 0.85], intensity: 9, radius: 18, use: "electric", fx: "mist", fxSize: 9 });
lightAsset("cave_glow_ember", "Ember fissure", "A crack in the floor glowing deep orange: a small, flickering warm accent.",
  { color: [1.0, 0.42, 0.12], intensity: 14, radius: 11, flicker: 0.35, use: "fire" },
  [{ t: "b", c: [0, 0.02, 0], s: [1.6, 0.04, 0.22], r: [0, 25, 0], m: "ember", k: 0 }, { t: "b", c: [0.5, 0.02, 0.3], s: [0.9, 0.04, 0.14], r: [0, -35, 0], m: "ember", k: 0 }],
  [{ name: "ember", albedo: [0.3, 0.08, 0.02], roughness: 0.9, pattern: "plain", metallic: 0, grain: 1, emissive: [3.2, 0.9, 0.18] }]);

lightAsset("cave_fire_altar", "Cave fire (altar)", "A low fire of embers and flames on a flat stone: the cavern's warm heart.",
  { color: [1.0, 0.48, 0.16], intensity: 140, radius: 40, offset: [0, 0.7, 0], flicker: 0.3, shadow: 1, use: "fire", fx: "flame", fxSize: 0.7 },
  [{ t: "b", c: [0, 0.25, 0], s: [1.6, 0.5, 1.2], m: "stone", bv: 0.06 }, { t: "b", c: [0, 0.53, 0], s: [1.0, 0.07, 0.7], m: "ember", k: 0 }],
  [{ name: "stone", albedo: [0.12, 0.11, 0.1], roughness: 0.85, pattern: "stone", metallic: 0, grain: 1 },
   { name: "ember", albedo: [0.3, 0.08, 0.02], roughness: 0.9, pattern: "plain", metallic: 0, grain: 1, emissive: [4.0, 1.1, 0.2] }]);

// ---------------------------------------------------------------- CV1b: the dripstone family (stalactites / stalagmites of very different shapes and sizes)
// Each asset is a seeded cluster: one main form of a chosen kind and size class (hand-sized to 25 m) with many smaller,
// irregularly spaced satellites of mixed kinds. Kinds: soda-straw needles, tapering spears, fat bulbous drips, fluted
// spears, broken stumps, curved / leaning ones, twinned and fused pairs, flowstone curtains (hanging), stacked
// "wedding-cake" stalagmites and squat mounds (standing). Hanging clusters below 9 m do not collide (out of reach).
const spk = (P, seed, x, z, y0, h, w, dir, yaw, lean, nseg = 4, curve = 1, lith = "dripstone") => {
  const segs = [[1.0, 0.0, 0.62], [0.68, 0.26, 0.56], [0.42, 0.5, 0.5], [0.2, 0.72, 0.36]].slice(0, nseg);
  segs.forEach(([ws, at, hs], k) => {
    const o = Math.pow(at, curve) * h;
    P.push(piece(lith, seed + k, [r2(Math.max(0.06, w * ws)), r2(h * hs), r2(Math.max(0.06, w * ws * (0.8 + 0.2 * ((seed + k) % 3) / 2)))],
      [r2(x + lean[0] * o), r2(y0 + dir * at * h), r2(z + lean[1] * o)], (yaw + k * 47) % 360, dir > 0 ? [0, 0] : [180, 0], 0, w * ws > 1.4 ? 2 : 1));
  });
};
const KDOWN = ["needle", "spear", "bulb", "fluted", "curved", "twin", "curtain", "stump", "spear", "needle"];
const KUP = ["spear", "bulb", "fluted", "stump", "curved", "twin", "cake", "mound", "spear", "stump"];
const drip = (P, kind, sd, x, z, y0, h, dir) => {
  const yaw = Math.round(rnd() * 360), ln = [(rnd() - 0.5) * 0.1, (rnd() - 0.5) * 0.1];
  if (kind === "needle") { spk(P, sd, x, z, y0, h, Math.max(0.08, h * (0.05 + rnd() * 0.04)), dir, yaw, ln, 3); return; }
  if (kind === "spear") { spk(P, sd, x, z, y0, h, h * (0.09 + rnd() * 0.06), dir, yaw, ln, 4); return; }
  if (kind === "bulb") { spk(P, sd, x, z, y0, h, h * (0.32 + rnd() * 0.18), dir, yaw, ln, 2); return; }
  if (kind === "stump") { spk(P, sd, x, z, y0, h * 0.55, h * (0.16 + rnd() * 0.08), dir, yaw, ln, 2); return; }
  if (kind === "curved") { const a = rnd() * 6.283, m = 0.25 + rnd() * 0.3; spk(P, sd, x, z, y0, h, h * (0.1 + rnd() * 0.05), dir, yaw, [Math.cos(a) * m, Math.sin(a) * m], 4, 2.2); return; }
  if (kind === "twin") { const w = h * (0.1 + rnd() * 0.05), a = rnd() * 6.283; spk(P, sd, x, z, y0, h, w, dir, yaw, ln, 4); spk(P, sd + 5, x + Math.cos(a) * w * 0.55, z + Math.sin(a) * w * 0.55, y0, h * (0.65 + rnd() * 0.25), w * 0.8, dir, yaw + 90, ln, 4); return; }
  if (kind === "fluted") {
    const w = h * (0.12 + rnd() * 0.05); spk(P, sd, x, z, y0, h, w, dir, yaw, ln, 4);
    const m = 5 + Math.floor(rnd() * 4);
    for (let k = 0; k < m; k++) { const a = k / m * 6.283 + rnd() * 0.4; spk(P, sd + 10 + k * 4, x + Math.cos(a) * w * 0.42, z + Math.sin(a) * w * 0.42, y0, h * (0.6 + rnd() * 0.32), w * 0.32, dir, yaw + k * 31, ln, 3); }
    return;
  }
  if (kind === "curtain") {   // a flowstone drapery hanging along a wavy crack
    const L = h * (1.1 + rnd() * 0.9), m = 8 + Math.floor(rnd() * 8), a0 = rnd() * 6.283, wav = 0.15 + rnd() * 0.25;
    for (let k = 0; k < m; k++) {
      const t = k / (m - 1) - 0.5, a = a0 + Math.sin(t * 6 + rnd()) * wav, px = x + Math.cos(a0) * t * L - Math.sin(a0) * Math.sin(t * 7) * L * 0.08, pz = z + Math.sin(a0) * t * L + Math.cos(a0) * Math.sin(t * 7) * L * 0.08;
      const hh = h * (0.45 + 0.55 * Math.abs(Math.sin(t * 5.3 + 1.1))) * (0.8 + rnd() * 0.3);
      P.push(piece("dripstone", sd + 30 + k, [r2(L / m * 1.9), r2(hh), r2(0.22 + rnd() * 0.15)], [r2(px), r2(y0), r2(pz)], Math.round(-a * 57.3 + 90) % 360, [180, 0], 0, 1));
    }
    return;
  }
  if (kind === "cake") {   // stacked flattened tiers
    const n = 4 + Math.floor(rnd() * 4); let y = y0;
    for (let k = 0; k < n; k++) { const f = 1 - k / n, w = h * 0.6 * f + 0.2; P.push(piece("dripstone", sd + 40 + k, [r2(w), r2(h / n * 1.7), r2(w * 0.9)], [r2(x + (rnd() - 0.5) * 0.2), r2(y), r2(z + (rnd() - 0.5) * 0.2)], Math.round(rnd() * 360), [0, 0], 0.05, w > 1.4 ? 2 : 1)); y += h / n * 0.95; }
    return;
  }
  if (kind === "mound") { P.push(piece("dripstone", sd + 60, [r2(h * 2.4), r2(h), r2(h * 2.1)], [x, y0, z], yaw, [0, 0], 0.2, 2)); return; }
};
const CLS = [1.5, 3, 5, 8, 12, 17, 21, 25];
const dripAsset = (dir, v) => {
  S = (dir > 0 ? 7100 : 5100) + v * 97;
  const P = [], cls = CLS[v % CLS.length] * (0.8 + rnd() * 0.35), kinds = dir > 0 ? KUP : KDOWN;
  let main = kinds[(v * 3 + Math.floor(rnd() * 3)) % kinds.length];
  if (dir > 0 && cls > 12 && (main === "mound" || main === "cake")) main = "fluted";   // giants are spears / fluted / bulbs
  const H = dir > 0 ? Math.min(cls, 15) : cls, y0 = dir > 0 ? -0.2 : 0.8;
  drip(P, main, 8000 + v * 200 + (dir > 0 ? 100000 : 0), 0, 0, y0, H, dir);
  const R = 1.5 + H * 0.35, n = 5 + Math.floor(rnd() * (H > 8 ? 16 : 10));
  for (let k = 0; k < n; k++) {
    const a = rnd() * 6.283, r = R * (0.25 + 0.75 * Math.pow(rnd(), 1.4)), hh = H * (0.05 + 0.4 * Math.pow(rnd(), 1.8)) + 0.3;
    drip(P, rnd() < 0.35 ? "needle" : kinds[Math.floor(rnd() * kinds.length)], 8000 + v * 200 + 3 + k * 9 + (dir > 0 ? 100000 : 0), r2(Math.cos(a) * r), r2(Math.sin(a) * r), y0, hh, dir);
  }
  const big = dir > 0 || H >= 9;
  rock(`cave_drip_${dir > 0 ? "up" : "down"}_${v}`, `${dir > 0 ? "Stalagmites" : "Stalactites"}: ${main} cluster (${Math.round(H)} m)`,
    `A seeded dripstone cluster: a ${Math.round(H)} m ${main} ${dir > 0 ? "rising from the floor" : "hanging from the roof"} with ${n} smaller forms of mixed kinds around it.`,
    P, [r2(R * 2), r2(R * 2)], big ? "block" : "none", big);
};
for (let v = 0; v < 32; v++) dripAsset(-1, v);
for (let v = 0; v < 24; v++) dripAsset(1, v);

// ---------------------------------------------------------------- CV1b: organ walls: continuous fluted ribs rising the full height, packed on a concave arc (faces local -z)
for (let v = 0; v < 4; v++) {
  S = 9300 + v * 71;
  const P = [], R = 11 + v * 3, H = 26 + v * 6, span = 1.6 + v * 0.1, n = Math.floor(R * span / 1.5);
  for (let k = 0; k < n; k++) {
    const t = k / (n - 1), a = (t - 0.5) * span, x = Math.sin(a) * R, z = Math.cos(a) * R - R;
    const h = H * (0.6 + 0.4 * Math.sin(t * Math.PI)) * (0.82 + rnd() * 0.3), w = 1.2 + rnd() * 0.9, inx = -Math.sin(a), inz = -Math.cos(a);
    P.push(piece("dripstone", 9400 + v * 300 + k * 3, [r2(w), r2(h * 0.66), r2(w * 1.1)], [r2(x), 0, r2(z)], Math.round(a * 57.3), [-3, 0], 0.03, 2));
    P.push(piece("dripstone", 9401 + v * 300 + k * 3, [r2(w * 0.78), r2(h * 0.6), r2(w * 0.9)], [r2(x + inx * h * 0.06), r2(h * 0.42), r2(z + inz * h * 0.06)], Math.round(a * 57.3) + 20, [-8, 0], 0, 2));
    if (k < n - 1) P.push(piece("dripstone", 9402 + v * 300 + k * 3, [r2(w * 0.45), r2(h * (0.25 + rnd() * 0.45)), r2(w * 0.5)], [r2(Math.sin(a + span / (2 * (n - 1))) * (R + 0.5)), 0, r2(Math.cos(a + span / (2 * (n - 1))) * (R + 0.5) - R)], Math.round(a * 57.3), [0, 0], 0.03, 1));
  }
  rock(`cave_organ_${v}`, `Organ wall (${H} m)`, `A ${H} m concave wall of ${n} fluted rock ribs packed like organ pipes, rising nearly the full height of a cavern.`, P, [r2(R * 2), r2(R * 0.8)]);
}

// ---------------------------------------------------------------- CV1b: human-scale cues
{
  S = 9900;
  const P = [];
  for (let k = 0; k < 7; k++) { const a = k / 7 * 6.283 + rnd() * 0.3, r = 5.5 + rnd(); P.push(piece("limestone", 9910 + k, [r2(0.7 + rnd() * 0.4), r2(1.8 + rnd() * 1.4), r2(0.5 + rnd() * 0.3)], [r2(Math.cos(a) * r), 0, r2(Math.sin(a) * r)], Math.round(a * 57.3 + 90), [Math.round((rnd() - 0.5) * 16), Math.round((rnd() - 0.5) * 10)], 0.12, 2)); }
  rock("cave_standing_stones", "Standing stones (ring)", "Seven rough standing stones, 1.8-3.2 m, in a ring 11 m across: old, placed by someone.", P, [13, 13]);
}
{
  const P = [];
  for (let k = 0; k < 5; k++) P.push(piece("limestone", 9950 + k, [r2(3.6 - k * 0.2), 0.42, 1.25], [0, r2(k * 0.24), r2(k * 0.95)], k * 3, [0, 0], 0.5, 2));
  rock("cave_steps", "Rough stone steps", "Five worn slabs rising 1.2 m: rough steps cut long ago.", P, [3.8, 5]);
}
lightAsset("cave_light_rim", "Cave light (rim)", "A faint, high cold glow with no fitting: grazes distant formations so their silhouettes separate from the dark.",
  { color: [0.5, 0.62, 0.85], intensity: 60, radius: 40, use: "electric" });
lightAsset("cave_light_shaft_main", "Cave light (great shaft)", "The big crack over the central cavern: a strong cold beam through haze onto the wet floor.",
  { color: [0.6, 0.72, 0.98], intensity: 16000, radius: 115, dir: [0, -1, 0], cone: [7, 17], use: "electric", fx: "shaft", fxSize: 66 });

console.log(`cave catalog: ${ids.length} assets`);
