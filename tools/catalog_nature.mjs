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
const write = (a) => { writeFileSync(join(OUT, a.id + ".json"), JSON.stringify(a) + "\n"); return a.id; };
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

// ---------------------------------------------------------------- small vegetation (foliage block)
const BARK = { name: "bark", albedo: [0.17, 0.14, 0.11], roughness: 0.95, pattern: "wood", metallic: 0, grain: 1 };
const stem = (x, z, h, w) => ({ t: "b", c: [x, h / 2, z], s: [w, h, w], m: "bark", k: 1 });
const plant = (id, name, desc, fp, tree, nav, parts = []) =>
  ids.push(write(base(id, name, "Vegetation", desc, fp, { materials: [BARK], parts, tree: { hpBullet: 6, hpAT: 1, stumpHeight: 0.12, trunkRadius: 0.04, mass: 25, seed: 1, hedge: false, ...tree }, nav })));
plant("nat_hawthorn_bush", "Hawthorn bush", "Dense hawthorn, about 2.2 m: hides a soldier, slows bots (brush), the stems stop a few rounds.", [2.4, 2.2],
  { hpBullet: 12, stumpHeight: 0.2, trunkRadius: 0.07, mass: 80, canopy: [[0, 1.35, 0, 0.85], [0.6, 1.0, 0.3, 0.7], [-0.55, 1.05, -0.25, 0.7], [0.1, 1.8, -0.2, 0.55]],
    cardDensity: 85, leafSize: 0.3, leafColor: [0.10, 0.15, 0.06], seed: 71 },
  { effect: "area", area: "brush" }, [stem(0, 0, 1.3, 0.11), stem(0.25, 0.1, 1.0, 0.07)]);
plant("nat_bramble_thicket", "Bramble thicket", "Wide blackberry thicket, about 1.1 m high and 3 m across: concealment and brush for bots, no collision.", [3.0, 2.4],
  { canopy: [[0, 0.55, 0, 0.7], [0.9, 0.45, 0.3, 0.6], [-0.9, 0.45, -0.2, 0.65], [0.3, 0.6, -0.8, 0.55], [-0.3, 0.4, 0.8, 0.55]],
    cardDensity: 80, leafSize: 0.3, leafColor: [0.11, 0.15, 0.065], seed: 72 }, { effect: "area", area: "brush" });
plant("nat_blackthorn_low", "Blackthorn scrub (low)", "Low blackthorn / sloe scrub, about 0.9 m.", [1.6, 1.4],
  { canopy: [[0, 0.5, 0, 0.55], [0.5, 0.4, 0.2, 0.45], [-0.4, 0.45, -0.25, 0.45]], cardDensity: 90, leafSize: 0.26, leafColor: [0.09, 0.13, 0.06], seed: 73 },
  { effect: "area", area: "brush" });
plant("nat_gorse_shrub", "Gorse shrub", "Rounded gorse (ajonc) of the heath and quarry edges, about 1 m, yellow-green.", [1.5, 1.4],
  { canopy: [[0, 0.55, 0, 0.6], [0.35, 0.45, 0.3, 0.45], [-0.3, 0.5, -0.3, 0.45]], cardDensity: 110, leafSize: 0.22, leafColor: [0.20, 0.21, 0.06], seed: 74 },
  { effect: "area", area: "brush" });
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
  { hpBullet: 4, stumpHeight: 0.15, trunkRadius: 0.035, mass: 15, canopy: [[0, 2.0, 0, 0.55], [0.25, 1.6, 0.15, 0.4]], cardDensity: 70, leafSize: 0.32, leafColor: [0.13, 0.20, 0.07], seed: 78 },
  { effect: "none" }, [stem(0, 0, 2.0, 0.06)]);
plant("nat_sapling_oak", "Oak sapling", "A young oak, about 1.8 m.", [1.0, 1.0],
  { hpBullet: 4, stumpHeight: 0.15, trunkRadius: 0.04, mass: 15, canopy: [[0, 1.35, 0, 0.5], [-0.25, 1.05, 0.1, 0.38]], cardDensity: 80, leafSize: 0.3, leafColor: [0.11, 0.17, 0.06], seed: 79 },
  { effect: "none" }, [stem(0, 0, 1.4, 0.07)]);

console.log(`catalog_nature: wrote ${ids.length} assets: ${ids.join(", ")}`);
