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
const base = (id, name, description, footprint, extra) => ({
  id, version: 1, name, category: "Cave", description, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 6,
  materials: [], parts: [], furniture: [], interactables: [], panels: [], ...extra,
});
const write = (a) => { writeFileSync(join(OUT, a.id + ".json"), JSON.stringify(a) + "\n"); return a.id; };
const ids = [];
const piece = (lith, seed, size, pos = [0, 0, 0], yaw = 0, tilt = [0, 0], sink = 0.25, detail) =>
  detail === undefined ? { lith, seed, size, pos, yaw, tilt, sink } : { lith, seed, size, pos, yaw, tilt, sink, detail };
const rock = (id, name, desc, pieces, fp, nav = "block") => ids.push(write(base(id, name, desc, fp, { rock: { pieces, collide: true }, nav: { effect: nav } })));
let S = 1;   // deterministic pseudo-random for the layouts below
const rnd = () => { S = (S * 16807) % 2147483647; return (S - 1) / 2147483646; };
const lerp = (a, b, t) => a + (b - a) * t;
const r2 = (v) => Math.round(v * 100) / 100;

// ---------------------------------------------------------------- ceiling lids (flat top at the instance y, dome hanging below)
for (const [sfx, sz, seeds] of [["l", [42, 16, 40], [501, 502, 503]], ["m", [28, 11, 27], [511, 512, 513]], ["s", [16, 7, 15], [521, 522]]]) {
  seeds.forEach((sd, k) => rock(`cave_lid_${sfx}${k}`, `Cave ceiling (${sfx === "l" ? "large" : sfx === "m" ? "medium" : "small"} ${k + 1})`,
    "A slab of cave roof hung from a flat top: a lumpy, fractured underside about " + Math.round(sz[1] * 0.78) + " m deep. Placed overlapping to close a cavern.",
    [piece(k % 2 ? "schist" : "limestone", sd, sz, [0, 0, 0], 0, [180, 0], 0, 3)], [sz[0], sz[2]]));
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
for (const [sfx, H] of [["m", 12], ["l", 20], ["xl", 30]]) {   // a stalagmite and a stalactite grown together, waisted where they meet
  const w = 2.4 + H * 0.12;
  rock(`cave_column_${sfx}`, `Dripstone column (${H} m)`, `A ${H} m dripstone column: a stalagmite and a stalactite fused at the waist, flared at floor and roof.`,
    [piece("limestone", 1600 + H, [w, H * 0.62, w * 0.9], [0, 0, 0], 0, [0, 0], 0.05, 3),
     piece("limestone", 1601 + H, [w * 0.95, H * 0.6, w], [0, H, 0], 70, [180, 0], 0.0, 3),
     piece("limestone", 1602 + H, [w * 1.9, H * 0.12, w * 1.7], [0, 0, 0], 20, [0, 0], 0.3, 2),
     piece("limestone", 1603 + H, [w * 1.7, H * 0.12, w * 1.9], [0, H + 0.6, 0], 140, [180, 0], 0, 2)], [w * 1.9, w * 1.9]);
}

// ---------------------------------------------------------------- the unsettling formations
for (let v = 0; v < 3; v++) {   // ribbed organ wall: tall laminated ribs on a concave arc (faces local -z), each curving inward as it rises, like a vast ribcage
  S = 2100 + v * 53;
  const P = [], R = 7 + v * 2.5, n = 11 + v * 3, span = 1.9;
  for (let k = 0; k < n; k++) {
    const t = k / (n - 1), a = (t - 0.5) * span, x = Math.sin(a) * R, z = Math.cos(a) * R - R;
    const h = (10 + v * 3) * (0.55 + 0.45 * Math.sin(t * Math.PI)) * (0.85 + rnd() * 0.3), w = 1.1 + rnd() * 0.5;
    const inx = -Math.sin(a), inz = -Math.cos(a);   // toward the arc's centre
    for (let g = 0; g < 4; g++) {   // four segments, narrowing and leaning further in
      const f = g / 4, y = h * f * 0.92, lean = 0.35 * h * f * f;
      P.push(piece("schist", 2200 + v * 120 + k * 5 + g, [r2(w * (1 - f * 0.55)), r2(h * 0.46), r2((1.3 + rnd() * 0.4) * (1 - f * 0.4))],
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
    const s = lerp(4.2, 1.2, k / (n - 1)), h = lerp(3.2, 2.4, k / (n - 1));
    P.push(piece("schist", 2710 + k, [r2(s), r2(h), r2(s * 0.85)], [0, r2(y), 0], k * 23, [Math.round((rnd() - 0.5) * 6), Math.round((rnd() - 0.5) * 6)], k === 0 ? 0.15 : 0, 2));
    for (const side of [-1, 1]) if (k < n - 2) P.push(piece("schist", 2740 + k * 2 + (side > 0), [0.7, r2(h * 1.4), r2(s * 0.55)], [r2(side * s * 0.62), r2(y + 0.2), 0], k * 23, [0, side * 14], 0, 1));
    y += h * 0.78;
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
  { color: [0.55, 0.68, 0.95], intensity: 2200, radius: 60, dir: [0, -1, 0], cone: [8, 22], shadow: 0, use: "electric" }, crackParts, skyMat);
lightAsset("cave_light_crack_ns", "Cave light (ceiling crack, no shadow)", "Cold light through a crack in the cave roof, without a shadow map (cheaper).",
  { color: [0.55, 0.68, 0.95], intensity: 1700, radius: 55, dir: [0, -1, 0], cone: [5, 19], use: "electric" }, crackParts, skyMat);
lightAsset("cave_light_cold", "Cave light (cold fill)", "A faint cold glow over water: no fitting, no shadow.",
  { color: [0.45, 0.6, 0.85], intensity: 9, radius: 18, use: "electric" });
lightAsset("cave_glow_ember", "Ember fissure", "A crack in the floor glowing deep orange: a small, flickering warm accent.",
  { color: [1.0, 0.42, 0.12], intensity: 14, radius: 11, flicker: 0.35, use: "fire" },
  [{ t: "b", c: [0, 0.02, 0], s: [1.6, 0.04, 0.22], r: [0, 25, 0], m: "ember", k: 0 }, { t: "b", c: [0.5, 0.02, 0.3], s: [0.9, 0.04, 0.14], r: [0, -35, 0], m: "ember", k: 0 }],
  [{ name: "ember", albedo: [0.3, 0.08, 0.02], roughness: 0.9, pattern: "plain", metallic: 0, grain: 1, emissive: [3.2, 0.9, 0.18] }]);

lightAsset("cave_fire_altar", "Cave fire (altar)", "A low fire of embers and flames on a flat stone: the cavern's warm heart.",
  { color: [1.0, 0.48, 0.16], intensity: 140, radius: 40, offset: [0, 0.7, 0], flicker: 0.3, shadow: 1, use: "fire", fx: "flame", fxSize: 0.7 },
  [{ t: "b", c: [0, 0.25, 0], s: [1.6, 0.5, 1.2], m: "stone", bv: 0.06 }, { t: "b", c: [0, 0.53, 0], s: [1.0, 0.07, 0.7], m: "ember", k: 0 }],
  [{ name: "stone", albedo: [0.12, 0.11, 0.1], roughness: 0.85, pattern: "stone", metallic: 0, grain: 1 },
   { name: "ember", albedo: [0.3, 0.08, 0.02], roughness: 0.9, pattern: "plain", metallic: 0, grain: 1, emissive: [4.0, 1.1, 0.2] }]);

console.log(`cave catalog: ${ids.length} assets`);
