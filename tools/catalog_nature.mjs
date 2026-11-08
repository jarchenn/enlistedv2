// Natural ground dressing catalogue (docs/nature.md): boulders and outcrops ("rock"), stone / flower / tall-grass
// patches ("scatter") and small vegetation (the foliage "tree" block with fern / nettle card styles).
// Standalone and deterministic: `node tools/catalog_nature.mjs` rewrites only content/assets/nat_*.json
// (never run tools/build_assets.mjs for this: it regenerates every asset and wipes hand edits).
import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "content", "assets");
const base = (id, name, category, description, footprint, extra) => ({
  id, version: 1, name, category, description, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 3,
  materials: [], parts: [], furniture: [], interactables: [], panels: [], ...extra,
});
const write = (a) => { a.version = Math.max(a.version, ntVersion(a.id)); writeFileSync(join(OUT, a.id + ".json"), JSON.stringify(a) + "\n"); return a.id; };
const ids = [];

// ---------------------------------------------------------------- boulders and outcrops (category Nature)
const piece = (lith, seed, size, pos = [0, 0, 0], yaw = 0, tilt = [0, 0], sink = 0.25) => ({ lith, seed, size, pos, yaw, tilt, sink });
const boulder = (id, name, desc, pieces, fp) =>
  ids.push(write(base(id, name, "Nature", desc, fp, { rock: { pieces, collide: true }, nav: { effect: "block" } })));
boulder("nat_boulder_granite_s", "Granite boulder (small)", "Rounded weathered granite, about 0.7 m. Blocks movement and stops rounds.",
  [piece("granite", 11, [0.75, 0.6, 0.65])], [0.8, 0.7]);
boulder("nat_boulder_granite_l", "Granite boulder (large)", "Big rounded granite erratic, about 2.2 m. Hard cover; solid for bots.",
  [piece("granite", 12, [2.3, 1.7, 1.9], [0, 0, 0], 20, [0, 0], 0.22)], [2.3, 2.0]);
boulder("nat_boulder_limestone_m", "Limestone boulder", "Blocky grey-brown limestone, about 1.3 m, with bedding and lichen like the cliffs.",
  [piece("limestone", 21, [1.4, 1.05, 1.2])], [1.4, 1.2]);
boulder("nat_boulder_limestone_l", "Limestone block (large)", "A fallen limestone block, about 2.8 m, flat-topped with jointed faces.",
  [piece("limestone", 22, [2.9, 1.9, 2.3], [0, 0, 0], 0, [0, 0], 0.2)], [2.9, 2.4]);
boulder("nat_boulder_schist_slab", "Schist slab", "A tilted slab of dark schist, about 2.4 m long and 0.9 m high.",
  [piece("schist", 31, [2.5, 0.95, 1.6], [0, 0, 0], 0, [6, 4], 0.3)], [2.5, 1.7]);
boulder("nat_boulder_schist_m", "Schist boulder", "Dark laminated schist, about 1.2 m.",
  [piece("schist", 32, [1.25, 0.85, 1.0])], [1.3, 1.1]);
boulder("nat_boulders_granite_group", "Boulder group (granite)", "Five granite boulders of 0.5-1.3 m around a 4 m spot.",
  [piece("granite", 41, [1.3, 1.0, 1.1], [0, 0, 0]), piece("granite", 42, [0.8, 0.6, 0.75], [1.3, 0, 0.6], 40),
   piece("granite", 43, [0.6, 0.45, 0.55], [-1.1, 0, 0.9], 80), piece("granite", 44, [0.95, 0.7, 0.8], [-0.6, 0, -1.3], 130),
   piece("granite", 45, [0.5, 0.4, 0.5], [1.4, 0, -1.0], 10)], [3.6, 3.4]);
boulder("nat_outcrop_limestone", "Limestone outcrop", "A rock outcrop of four limestone blocks breaking the turf, about 5 x 4 m and 2.4 m high.",
  [piece("limestone", 51, [2.6, 2.4, 2.1], [0, 0, 0], 0, [0, 0], 0.3), piece("limestone", 52, [2.0, 1.5, 1.7], [1.9, 0, 0.6], 25, [0, 4], 0.3),
   piece("limestone", 53, [1.5, 1.1, 1.4], [-1.7, 0, 0.9], 60, [3, 0], 0.3), piece("limestone", 54, [1.2, 0.8, 1.0], [0.6, 0, -1.6], 110, [0, 0], 0.3)], [5.2, 4.2]);
boulder("nat_outcrop_schist", "Schist outcrop", "Tilted schist slabs pushing out of a slope, about 5 x 3 m.",
  [piece("schist", 61, [2.8, 1.4, 1.5], [0, 0, 0], 10, [18, 0], 0.35), piece("schist", 62, [2.2, 1.1, 1.3], [0.5, 0, 1.3], 15, [20, 3], 0.35),
   piece("schist", 63, [1.8, 0.9, 1.1], [-0.4, 0, -1.2], 5, [16, -4], 0.35)], [4.8, 3.6]);

// ---------------------------------------------------------------- scatter patches
const patch = (id, name, category, desc, scatter, nav = { effect: "none" }) => {
  const r = scatter.radius;
  ids.push(write(base(id, name, category, desc, [2 * r, 2 * r], { scatter, nav })));
};
patch("nat_stones_patch", "Loose stones (patch)", "Nature", "Field stones of 5-40 cm scattered over a 3 m patch. Visual only; radius / density / seed in the Selection panel.",
  { kind: "stones", radius: 3, density: 2.5, size: [0.05, 0.4], sizePow: 2.4, lith: "limestone", clump: 0.55, maxDensity: 20 });
patch("nat_scree_patch", "Scree (patch)", "Nature", "Dense limestone and schist scree, 4-25 cm, for cliff feet and quarry floors.",
  { kind: "stones", radius: 4, density: 12, size: [0.04, 0.25], sizePow: 2.0, lith: "mixed", clump: 0.35, maxDensity: 30 });
patch("nat_stones_granite_patch", "Granite stones (patch)", "Nature", "Rounded granite cobbles and stones, 8-40 cm.",
  { kind: "stones", radius: 3, density: 1.5, size: [0.08, 0.4], sizePow: 1.8, lith: "granite", clump: 0.6, maxDensity: 20 });

const flowers = (id, name, desc, mix, density, radius = 3, clump = 0.55, extra = {}) =>
  patch(id, name, "Vegetation", desc, { kind: "flowers", radius, density, mix, clump, ...extra });
flowers("nat_flowers_poppy", "Poppies (patch)", "Red field poppies with a little clover.", { poppy: 1, clover: 0.15 }, 5);
flowers("nat_flowers_daisy", "Ox-eye daisies (patch)", "Ox-eye daisies in a meadow drift.", { daisy: 1 }, 6);
flowers("nat_flowers_buttercup", "Buttercups (patch)", "Meadow buttercups, small and many.", { buttercup: 1, clover: 0.2 }, 9);
flowers("nat_flowers_cornflower", "Cornflowers (patch)", "Cornflowers and poppies of a field margin.", { cornflower: 1, poppy: 0.6 }, 5);
flowers("nat_flowers_cowparsley", "Cow parsley (patch)", "Cow parsley / Queen Anne's lace along a hedgerow verge, 0.7-1.2 m.", { cowparsley: 1, tallgrass: 0.5 }, 2.5, 3, 0.4);
flowers("nat_flowers_foxglove", "Foxgloves (patch)", "Foxgloves for wood edges and banks, 0.8-1.4 m.", { foxglove: 1 }, 1.0, 2.5, 0.5);
flowers("nat_flowers_clover", "Flowering clover (patch)", "Low red and white clover heads.", { clover: 1 }, 16, 3, 0.4);
flowers("nat_flowers_meadow", "Meadow flowers (mixed patch)", "Mixed Normandy meadow: poppy, ox-eye daisy, buttercup, cornflower, clover and tall grass in drifts.",
  { poppy: 0.8, daisy: 1, buttercup: 1, cornflower: 0.4, clover: 0.8, tallgrass: 1.2 }, 7, 5, 0.45);
patch("nat_tallgrass_patch", "Tall meadow grass (patch)", "Vegetation", "Uncut meadow grass, 0.6-1.2 m with seed heads; hides a prone or crouched soldier. Visual only.",
  { kind: "tallgrass", radius: 3, density: 16, clump: 0.3, mix: { tallgrass: 1 } });
patch("nat_tallgrass_verge", "Tall grass verge (patch)", "Vegetation", "Rank verge of tall grass with cow parsley and a few foxgloves.",
  { kind: "tallgrass", radius: 3, density: 12, clump: 0.35, mix: { tallgrass: 1, cowparsley: 0.18, foxglove: 0.05 } });

// ---------------------------------------------------------------- V9 ground cover (docs/nature.md#ground-cover)
// Scatter patches like the stone patches: plants through the grass shader (wind, distance fade, the grass quality
// preset's share), most with a pebble layer underneath for depth. Species: turf, meadowgrass, seedgrass, weeds, straw
// (the pasture species), tallgrass .. clover (flowers), wheat, brush, stubble. "dry": [min, max] share of each plant's
// colour turned to straw in world-space drifts; "tint": per-species albedo.
const pebbles = (density, size = [0.03, 0.14], lith = "mixed") => ({ density, size, lith });
const cover = (id, name, desc, scatter) => patch(id, name, "Vegetation", desc, { kind: "grass", ...scatter });
cover("nat_brush_patchy", "Patchy brush (patch)", "Low patchy brush: twiggy shrublets, dry tussocks and seed grass in clumps with bare ground and pebbles between.",
  { radius: 4, density: 6, clump: 0.85, mix: { brush: 1, straw: 0.6, meadowgrass: 0.8, seedgrass: 0.4, tallgrass: 0.3 }, dry: [0.1, 0.45], pebbles: pebbles(1.5) });
cover("nat_brush_scrub", "Dry scrub (patch)", "Sun-dried scrub of shrublets and straw on stony ground, for banks, quarry floors and field corners.",
  { radius: 4, density: 5, clump: 0.7, mix: { brush: 1.2, straw: 1, seedgrass: 0.5 }, dry: [0.3, 0.7], pebbles: pebbles(3, [0.03, 0.18]) });
cover("nat_brush_rank", "Rank brush (patch)", "Rank brush at a hedge foot or ruin: shrublets, tall grass, weeds and a little cow parsley, up to about 1 m.",
  { radius: 3.5, density: 7, clump: 0.6, mix: { brush: 1, tallgrass: 0.8, cowparsley: 0.12, meadowgrass: 0.6, weeds: 0.5 }, heightScale: [1.0, 1.35], dry: [0, 0.2], pebbles: pebbles(0.8) });
cover("nat_drygrass_tufts", "Dry grass tufts (patch)", "Yellowed tussocks and straw of a dry summer pasture, with a few pebbles.",
  { radius: 3.5, density: 12, clump: 0.6, mix: { meadowgrass: 1, straw: 1, turf: 0.4 }, dry: [0.55, 0.95], pebbles: pebbles(1, [0.03, 0.1]) });
cover("nat_drygrass_tall", "Tall dry grass (patch)", "Uncut grass gone yellow, 0.5-1.1 m with seed heads.",
  { radius: 3.5, density: 12, clump: 0.35, mix: { tallgrass: 1, seedgrass: 0.8, straw: 0.4 }, dry: [0.6, 1.0], heightScale: [0.85, 1.15] });
cover("nat_drygrass_sparse", "Sparse dry grass (patch)", "Thin, straw-coloured grass over stony ground: worn verges, tracksides, chalky banks.",
  { radius: 4, density: 6, clump: 0.8, mix: { straw: 1, meadowgrass: 0.6, seedgrass: 0.3 }, dry: [0.5, 0.9], pebbles: pebbles(3.5, [0.03, 0.16], "limestone") });
cover("nat_clover_tufts", "Clover and green tufts (patch)", "Clover leaves, flowering clover and short lush grass tufts.",
  { radius: 3, density: 18, clump: 0.5, mix: { weeds: 1, clover: 0.5, turf: 0.6, meadowgrass: 0.4 }, tint: { meadowgrass: [0.13, 0.22, 0.06] } });
cover("nat_green_tufts", "Green grass tufts (patch)", "Lush green tussocks of a damp field corner or ditch side.",
  { radius: 3, density: 14, clump: 0.55, mix: { meadowgrass: 1, seedgrass: 0.25, turf: 0.5, weeds: 0.2 }, tint: { meadowgrass: [0.12, 0.23, 0.06], turf: [0.11, 0.21, 0.05] } });
cover("nat_meadow_mixed", "Mixed meadow (patch)", "An unmown Normandy meadow: grasses of every height, clover, buttercups, daisies, a few poppies and cornflowers.",
  { radius: 5, density: 12, clump: 0.45, mix: { meadowgrass: 1, tallgrass: 0.8, seedgrass: 0.5, weeds: 0.4, clover: 0.35, buttercup: 0.3, daisy: 0.25, poppy: 0.12, cornflower: 0.08, straw: 0.2 },
    dry: [0, 0.25], pebbles: pebbles(0.4) });

// Wheat field squares (10 x 10 m, size locked so they tile): golden stalks with ears that sway in rolling waves, a soil /
// straw-litter layer, edge sockets for snapping (V6, src/snap.cpp: {p, dir, kind}); tanks and blasts lay them flat.
const W = 5;
const wheatSnap = [{ p: [0, 0, W], dir: [0, 0, 1], kind: "wheat" }, { p: [0, 0, -W], dir: [0, 0, -1], kind: "wheat" },
  { p: [W, 0, 0], dir: [1, 0, 0], kind: "wheat" }, { p: [-W, 0, 0], dir: [-1, 0, 0], kind: "wheat" }];
const wheat = (id, name, desc, state, scatter) => ids.push(write(base(id, name, "Vegetation", desc, [2 * W, 2 * W], {
  scatter: { kind: "wheat", shape: "square", state, radius: W, minRadius: W, maxRadius: W, density: 34, minDensity: 12, maxDensity: 50, clump: 0.04,
    heightScale: [0.92, 1.06], ...scatter },
  snap: wheatSnap, nav: { effect: "none" } })));
wheat("nat_wheat_field", "Wheat field (10 m square)", "Ripe wheat, about 1.1 m, golden ears swaying in the wind, a few poppies and cornflowers. Tiles edge to edge; tanks and shell bursts flatten it.",
  "ripe", { mix: { wheat: 1, poppy: 0.012, cornflower: 0.006 } });
wheat("nat_wheat_trampled", "Wheat field, trampled (10 m square)", "Ripe wheat beaten down in swathes and trails, as after troops and vehicles went through.",
  "trampled", { density: 30, mix: { wheat: 1, poppy: 0.008 } });
wheat("nat_wheat_stubble", "Wheat stubble (10 m square)", "A harvested field: short cut stubble with straw lying between and a few stones.",
  "stubble", { density: 30, mix: { stubble: 1 }, soil: [0.31, 0.26, 0.17], pebbles: pebbles(0.8, [0.03, 0.09], "limestone") });

// ---------------------------------------------------------------- NT pass: shrub and sapling models
// Round tapered stems (B1 frustums, visual only; the old stem boxes stay as invisible colliders), clumped leaf lobes
// with satellites, bare stems at the foot, arching bramble canes. As tools/gen_env_assets.py seg3 / clump3 / coppice3.
function ntVersion(id) {   // hoisted: write() runs before this block; models rebuilt in the NT pass
  return ["nat_hawthorn_bush", "nat_bramble_thicket", "nat_blackthorn_low", "nat_gorse_shrub", "nat_sapling_ash", "nat_sapling_oak"].includes(id) ? 2 : 1;
}
const ntR = (seed) => { let s = seed >>> 0; return () => (s = (Math.imul(s, 1103515245) + 12345) >>> 0) / 4294967296; };
const nt3 = (v) => Math.round(v * 1000) / 1000;
function ntSeg(parts, a, b, ra, rb, n = 5) {   // one tapered round segment from a to b, visual only
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d), pitch = (Math.asin(Math.max(-1, Math.min(1, d[1] / L))) * 180) / Math.PI;
  const p = { t: "k", c: a.map((v, i) => nt3((v + b[i]) / 2)), s: [nt3(2 * ra), nt3(L + 0.02), nt3(2 * ra)], top: nt3(Math.min(1, rb / ra)), seg: n, m: "bark", k: 0 };
  if (Math.abs(90 - pitch) > 0.05) p.r = [nt3(90 - pitch), nt3((Math.atan2(d[0], d[2]) * 180) / Math.PI), 0];
  parts.push(p);
  return b;
}
function ntLobe(R, canopy, c, r, out, sat = true) {   // a leaf lobe and (optionally) a smaller satellite pushed outward
  canopy.push([nt3(c[0]), nt3(c[1] + r * 0.1), nt3(c[2]), nt3(r * (0.85 + R() * 0.25))]);
  if (sat) canopy.push([nt3(c[0] + out[0] * r * 0.7 + (R() - 0.5) * r * 0.5), nt3(c[1] + (R() - 0.3) * r * 0.6), nt3(c[2] + out[1] * r * 0.7 + (R() - 0.5) * r * 0.5), nt3(r * (0.5 + R() * 0.15))]);
}
// a bush: stems splaying from the root, bending outward, the lowest `bare` share leafless; a lobe at each top and one
// part-way down the outside
function ntBush(seed, stems, h0, h1, spread, rs, lobe, bare = 0.35) {
  const R = ntR(seed), canopy = [], parts = [];
  for (let i = 0; i < stems; i++) {
    const yaw = (2 * Math.PI * i) / stems + (R() - 0.5), pitch = ((90 - spread * (0.3 + 0.7 * R())) * Math.PI) / 180, h = h0 + R() * (h1 - h0);
    const o = [Math.sin(yaw), Math.cos(yaw)], run = (h / Math.tan(pitch)) * 0.5, a = [(R() - 0.5) * 0.1, -0.03, (R() - 0.5) * 0.1];
    const mid = [a[0] + o[0] * run * 0.7, h * 0.55, a[2] + o[1] * run * 0.7], tip = [mid[0] + o[0] * run * 1.3, h, mid[2] + o[1] * run * 1.3];
    ntSeg(parts, a, mid, rs, rs * 0.6); ntSeg(parts, mid, tip, rs * 0.6, rs * 0.25, 4);
    ntLobe(R, canopy, tip, lobe, o);
    const f = bare + 0.2 + R() * 0.2, m = [a[0] + (tip[0] - a[0]) * f, h * f, a[2] + (tip[2] - a[2]) * f];
    canopy.push([nt3(m[0] + o[0] * lobe * 0.5), nt3(m[1]), nt3(m[2] + o[1] * lobe * 0.5), nt3(lobe * (0.6 + R() * 0.15))]);
  }
  return { canopy, parts };
}
// arching canes (bramble): up and out from the crown, over, and down to root again; leaf lobes along them
function ntCanes(seed, n, reach, height, lobe) {
  const R = ntR(seed), canopy = [[0, nt3(height * 0.5), 0, nt3(lobe * 1.3)]], parts = [];
  for (let i = 0; i < n; i++) {
    const yaw = (2 * Math.PI * i) / n + (R() - 0.5) * 0.8, o = [Math.sin(yaw), Math.cos(yaw)], r = reach * (0.7 + R() * 0.3), hh = height * (0.75 + R() * 0.3);
    const P = [[(R() - 0.5) * 0.2, 0, (R() - 0.5) * 0.2]];
    for (const [f, y] of [[0.3, 0.85], [0.65, 1.0], [1.0, 0.12]]) P.push([P[0][0] + o[0] * r * f, hh * y, P[0][2] + o[1] * r * f]);
    for (let k = 0; k < 3; k++) ntSeg(parts, P[k], P[k + 1], 0.01 - 0.002 * k, 0.008 - 0.002 * k, 4);
    for (const k of [1, 2]) ntLobe(R, canopy, [P[k][0], Math.max(lobe * 0.7, P[k][1] - lobe * 0.3), P[k][2]], lobe * (0.85 + R() * 0.3), o, k === 2);
    const e = [(P[2][0] + P[3][0]) / 2, (P[2][1] + P[3][1]) * 0.4, (P[2][2] + P[3][2]) / 2];   // leaves on the falling end too, down to the tip
    for (const q of [e, P[3]]) canopy.push([nt3(q[0]), nt3(Math.max(lobe * 0.55, q[1])), nt3(q[2]), nt3(lobe * (0.6 + R() * 0.15))]);
  }
  return { canopy, parts };
}
const ntHawthorn = ntBush(71, 7, 1.5, 2.1, 35, 0.045, 0.52, 0.3);
const ntBramble = ntCanes(72, 8, 1.5, 1.05, 0.34);
const ntBlackthorn = ntBush(73, 6, 0.65, 0.95, 45, 0.022, 0.36, 0.25);
const ntGorse = ntBush(74, 8, 0.7, 1.05, 50, 0.018, 0.34, 0.2);
ntGorse.canopy.push([0, 0.45, 0, 0.42]);   // gorse is a closed mound
// a sapling: a stub that stays on the stump (below the 0.15 m cut), a tapering leader, side twigs, a lobe on each
function ntSapling(seed, h, r, lobe, twigs) {
  const R = ntR(seed), canopy = [], parts = [{ t: "k", c: [0, 0.07, 0], s: [nt3(2 * r), 0.14, nt3(2 * r)], top: 0.97, seg: 6, m: "bark", k: 0 }];
  const top = [(R() - 0.5) * 0.12, h, (R() - 0.5) * 0.12], mid = [top[0] * 0.4, h * 0.55, top[2] * 0.4];
  ntSeg(parts, [0, 0.14, 0], mid, r * 0.95, r * 0.6, 6); ntSeg(parts, mid, top, r * 0.6, r * 0.2, 5);
  ntLobe(R, canopy, top, lobe, [0, 0]);
  for (let k = 0; k < twigs; k++) {
    const f = 0.5 + (0.35 * k) / Math.max(1, twigs - 1), yaw = k * 2.4 + R(), L = 0.35 + R() * 0.25;
    const b = [top[0] * f, h * f, top[2] * f], t = [b[0] + Math.sin(yaw) * L, b[1] + L * 0.6, b[2] + Math.cos(yaw) * L];
    ntSeg(parts, b, t, r * 0.35, r * 0.1, 4);
    ntLobe(R, canopy, t, lobe * (0.6 + R() * 0.15), [Math.sin(yaw), Math.cos(yaw)], false);
  }
  return { canopy, parts };
}
const ntAsh = ntSapling(78, 2.4, 0.032, 0.42, 3), ntOak = ntSapling(79, 1.75, 0.036, 0.4, 3);

// ---------------------------------------------------------------- small vegetation (foliage block)
const BARK = { name: "bark", albedo: [0.17, 0.14, 0.11], roughness: 0.95, pattern: "wood", metallic: 0, grain: 1 };
const stem = (x, z, h, w) => ({ t: "b", c: [x, h / 2, z], s: [w, h, w], m: "bark", k: 1 });
const plant = (id, name, desc, fp, tree, nav, parts = []) =>
  ids.push(write(base(id, name, "Vegetation", desc, fp, { materials: [BARK], parts, tree: { hpBullet: 6, hpAT: 1, stumpHeight: 0.12, trunkRadius: 0.04, mass: 25, seed: 1, hedge: false, ...tree }, nav })));
plant("nat_hawthorn_bush", "Hawthorn bush", "Dense hawthorn, about 2.2 m: hides a soldier, slows bots (brush), the stems stop a few rounds.", [2.4, 2.2],
  { hpBullet: 12, stumpHeight: 0.2, trunkRadius: 0.07, mass: 80, canopy: ntHawthorn.canopy,
    cardDensity: 66, leafSize: 0.3, leafColor: [0.10, 0.15, 0.06], seed: 71 },
  { effect: "area", area: "brush" }, [{ ...stem(0, 0, 1.3, 0.11), v: 0 }, { ...stem(0.25, 0.1, 1.0, 0.07), v: 0 }, ...ntHawthorn.parts]);
plant("nat_bramble_thicket", "Bramble thicket", "Wide blackberry thicket, about 1.1 m high and 3 m across: concealment and brush for bots, no collision.", [3.0, 2.4],
  { canopy: ntBramble.canopy,
    cardDensity: 66, leafSize: 0.3, leafColor: [0.11, 0.15, 0.065], seed: 72 }, { effect: "area", area: "brush" }, ntBramble.parts);
plant("nat_blackthorn_low", "Blackthorn scrub (low)", "Low blackthorn / sloe scrub, about 0.9 m.", [1.6, 1.4],
  { canopy: ntBlackthorn.canopy, cardDensity: 62, leafSize: 0.26, leafColor: [0.09, 0.13, 0.06], seed: 73 },
  { effect: "area", area: "brush" }, ntBlackthorn.parts);
plant("nat_gorse_shrub", "Gorse shrub", "Rounded gorse (ajonc) of the heath and quarry edges, about 1 m, yellow-green.", [1.5, 1.4],
  { canopy: ntGorse.canopy, cardDensity: 70, leafSize: 0.22, leafColor: [0.20, 0.21, 0.06], seed: 74 },
  { effect: "area", area: "brush" }, ntGorse.parts);
plant("nat_bracken_clump", "Bracken clump", "Bracken fronds, about 1 m, on slopes and wood edges. Visual only.", [1.8, 1.8],
  { canopy: [[0, 0.5, 0, 0.8]], cardsPerSphere: 0, cardDensity: 0, leafColor: [0.17, 0.22, 0.07], seed: 75,
    fronds: { count: 13, len: [0.7, 1.0], width: 0.45, elev: [12, 38], droop: 0.3, radius: 0.25, stalk: 0.55 } }, { effect: "none" });
plant("nat_fern_clump", "Fern clump", "A male-fern shuttlecock, about 0.6 m, for banks and ditches. Visual only.", [1.2, 1.2],
  { canopy: [[0, 0.35, 0, 0.55]], cardsPerSphere: 0, cardDensity: 0, leafColor: [0.11, 0.19, 0.06], seed: 76,
    fronds: { count: 18, len: [0.65, 0.95], width: 0.22, elev: [55, 80], droop: 0.35, radius: 0.06 } }, { effect: "none" });
plant("nat_nettle_clump", "Nettle patch", "Stinging nettles, 0.6-1.1 m, around ruins, dung heaps and ditch banks. Visual only.", [1.2, 1.2],
  { canopy: [[0, 0.5, 0, 0.55]], cardsPerSphere: 0, cardDensity: 0, leafColor: [0.10, 0.17, 0.06], seed: 77,
    nettles: { stems: 16, height: [0.6, 1.1], radius: 0.45, leaf: 0.11 } }, { effect: "none" });
plant("nat_sapling_ash", "Ash sapling", "A young ash, about 2.6 m, thin stem.", [1.0, 1.0],
  { hpBullet: 4, stumpHeight: 0.15, trunkRadius: 0.035, mass: 15, canopy: ntAsh.canopy, cardDensity: 70, leafSize: 0.32, leafColor: [0.13, 0.20, 0.07], seed: 78 },
  { effect: "none" }, [{ ...stem(0, 0, 2.0, 0.06), v: 0 }, ...ntAsh.parts]);
plant("nat_sapling_oak", "Oak sapling", "A young oak, about 1.8 m.", [1.0, 1.0],
  { hpBullet: 4, stumpHeight: 0.15, trunkRadius: 0.04, mass: 15, canopy: ntOak.canopy, cardDensity: 80, leafSize: 0.3, leafColor: [0.11, 0.17, 0.06], seed: 79 },
  { effect: "none" }, [{ ...stem(0, 0, 1.4, 0.07), v: 0 }, ...ntOak.parts]);

console.log(`catalog_nature: wrote ${ids.length} assets: ${ids.join(", ")}`);
