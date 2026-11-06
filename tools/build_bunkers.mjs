// Regenerates ONLY the bunker assets (content/assets/bunker_*.json, gun_mg42_embrasure.json) from tools/catalog_bunkers.mjs.
// Run: node tools/build_bunkers.mjs   (tools/build_assets.mjs rewrites every catalogue asset and the weapons)
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { mgCasemate, observationPost, fieldShelter, commandShelter, largeBunker, pakCasemate, mg42Mount } from "./catalog_bunkers.mjs";

const out = path.join(path.resolve(path.dirname(fileURLToPath(import.meta.url)), ".."), "content", "assets");
for (const A of [mgCasemate(), observationPost(), fieldShelter(), commandShelter(), largeBunker(), pakCasemate(), mg42Mount()]) {
  const j = A.json();
  fs.writeFileSync(path.join(out, j.id + ".json"), JSON.stringify(j));
  console.log(j.id, j.parts.length, "parts,", j.panels.length, "panels");
}
