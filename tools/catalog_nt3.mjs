// NT3: trees for the two military-outpost maps, cloned from the shipped oak / ash / poplar (their trunks, collision,
// nav and hit points) with a different crown: dead (bare limbs, a few dry leaves), dry (sparse olive-grey desert
// crown), and dark forest trees (dense, deep green) for the woods.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ASSETS = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "content", "assets");
const TREES = [   // id, source, name, description, crown changes, bark albedo scale
  ["nt3_tree_dead_oak", "tree_oak", "Dead oak", "A drought-killed oak: bare grey limbs with a few clinging dry leaves.",
    { cardsPerSphere: 5, cardDensity: 5, leafColor: [0.2, 0.15, 0.08], leafSize: 0.16 }, 1.7],
  ["nt3_tree_dead_ash", "tree_ash", "Dead ash", "A dead ash, bleached and bare, its crown thinned to twigs.",
    { cardsPerSphere: 4, cardDensity: 4, leafColor: [0.22, 0.17, 0.09], leafSize: 0.15 }, 1.8],
  ["nt3_tree_dry_ash", "tree_ash", "Dry scrub tree", "A drought-stunted tree with a sparse, dusty olive-grey crown.",
    { cardsPerSphere: 22, cardDensity: 22, leafColor: [0.17, 0.18, 0.1], leafSize: 0.18 }, 1.35],
  ["nt3_tree_forest_oak", "tree_oak", "Forest oak", "A tall woodland oak with a dense, deep green crown and mossy bark.",
    { cardsPerSphere: 72, cardDensity: 72, leafColor: [0.07, 0.13, 0.045] }, 0.85],
  ["nt3_tree_forest_ash", "tree_ash", "Forest ash", "A woodland ash, dark crowned, grown tall in the shade.",
    { cardsPerSphere: 66, cardDensity: 66, leafColor: [0.075, 0.14, 0.05] }, 0.85],
  ["nt3_tree_forest_poplar", "tree_poplar", "Forest aspen", "A slim aspen at the forest edge, dark green crown.",
    { leafColor: [0.08, 0.15, 0.055] }, 0.9],
];

export function nt3Assets() {
  return TREES.map(([id, src, name, desc, crown, bark]) => ({ json: () => {
    const j = JSON.parse(fs.readFileSync(path.join(ASSETS, src + ".json"), "utf8"));
    j.id = id; j.version = 1; j.name = name; j.description = desc;
    Object.assign(j.tree, crown);
    for (const m of j.materials) m.albedo = m.albedo.map((v) => Math.min(0.6, +(v * bark).toFixed(3)));
    return j;
  } }));
}
