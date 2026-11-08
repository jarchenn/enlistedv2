// Generates content/assets/*.json and content/templates/*.json.
// Run: node tools/build_assets.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { pak40, m2hb, crates } from "./catalog_ordnance.mjs";
import { weapons } from "./catalog_weapons.mjs";
import { mgCasemate, observationPost, fieldShelter, commandShelter, largeBunker, pakCasemate, mg42Mount } from "./catalog_bunkers.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outAssets = path.join(root, "content", "assets");
const outTemplates = path.join(root, "content", "templates");
fs.mkdirSync(outAssets, { recursive: true });
fs.mkdirSync(outTemplates, { recursive: true });

// The houses, outbuildings, large buildings (and the church) are B2's: tools/catalog_houses2.mjs, written by build_mpu_assets.mjs.
const assets = [mgCasemate(), observationPost(), fieldShelter(), commandShelter(), largeBunker(), pakCasemate(), mg42Mount(), pak40(), m2hb(), ...crates()];
for (const A of assets) {
  const j = A.json();
  fs.writeFileSync(path.join(outAssets, j.id + ".json"), JSON.stringify(j));
  const nParts = j.parts.length + j.furniture.reduce((s, f) => s + f.parts.length, 0);
  console.log(`${j.id}: ${nParts} parts, ${j.interactables.length} interactables, ${j.furniture.length} furniture groups`);
}

// ---- first-person weapons
const outWeapons = path.join(root, "content", "weapons");
fs.mkdirSync(outWeapons, { recursive: true });
for (const w of weapons()) {
  fs.writeFileSync(path.join(outWeapons, w.id + ".json"), JSON.stringify(w));
  const n = Object.values(w.parts).reduce((s, p) => s + p.length, 0);
  console.log(`weapon ${w.id}: ${n} parts`);
}

// ---- templates
const templates = [
  { id: "blank_slate", version: 1, order: 0, name: "Blank Slate", available: true, seed: 1944,
    description: "About 600 x 600 m of level, buildable Norman pasture. Gentle variation in grass colour and height for scale. The whole marked build area accepts structures, including its corners. Toggle the grid overlay with H.",
    size: 800, buildHalf: 300, camera: { pos: [0, 38, 95], yaw: 0, pitch: -18 } },
  { id: "omaha_easy_red", version: 0, order: 1, name: "Normandy Beaches - Omaha, Easy Red", available: false, seed: 1,
    description: "Terrain reconstruction of the E-1 draw sector, 6 June 1944, with all wartime structures intentionally omitted. Planned; see docs/normandy-terrain.md for the research plan.",
    size: 800, buildHalf: 300, camera: { pos: [0, 30, 120], yaw: 0, pitch: -12 } },
  { id: "normandy_forest", version: 0, order: 2, name: "Normandy Forest", available: false, seed: 7,
    description: "Fictional Norman woodland with three connected clearings (command post, overlook, fallback). Planned for a later milestone.",
    size: 800, buildHalf: 300, camera: { pos: [0, 30, 120], yaw: 0, pitch: -12 } },
];
for (const t of templates) fs.writeFileSync(path.join(outTemplates, t.id + ".json"), JSON.stringify(t, null, 2));
console.log(`${templates.length} templates written`);
