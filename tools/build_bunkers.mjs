// Regenerates ONLY the bunker assets (content/assets/bunker_*.json, gun_mg42_embrasure.json) from tools/catalog_bunkers.mjs.
// Run: node tools/build_bunkers.mjs   (tools/build_assets.mjs rewrites every catalogue asset and the weapons)
// W2 (MP & Creative update spec 10.2): also adds the 8.8 cm gun ammunition (tools/catalog_ammo.mjs gunAmmoStack) to the
// Flak 18 emplacement. That asset is written by tools/gen_env_assets.py (Python, no access to the JS part builders), so
// the stack is merged into content/assets/gun_flak18.json here: re-run this script after gen_env_assets.py.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { mgCasemate, observationPost, fieldShelter, commandShelter, largeBunker, pakCasemate, mg42Mount } from "./catalog_bunkers.mjs";
import { Asset } from "./kit.mjs";
import { gunAmmoStack } from "./catalog_ammo.mjs";

const out = path.join(path.resolve(path.dirname(fileURLToPath(import.meta.url)), ".."), "content", "assets");
for (const A of [mgCasemate(), observationPost(), fieldShelter(), commandShelter(), largeBunker(), pakCasemate(), mg42Mount()]) {
  const j = A.json();
  fs.writeFileSync(path.join(out, j.id + ".json"), JSON.stringify(j));
  console.log(j.id, j.parts.length, "parts,", j.panels.length, "panels");
}

// Flak 18 emplacement: shell crates, wicker carriers and loose rounds in the quadrant between the +x outrigger and the
// rear arm, clear of the gunner's position (-1.3, -1.7) and of the barrel at any traverse (bore 1.8 m up)
{
  const fp = path.join(out, "gun_flak18.json");
  const fj = JSON.parse(fs.readFileSync(fp, "utf8"));
  const T = new Asset({});
  T.materials = fj.materials.map((m) => ({ ...m }));
  gunAmmoStack(T, "Gun ammunition", 1.3, 0, -1.9, 0, { cal: "88", crates: 4, carriers: 2, loose: 2 });
  fj.materials = T.materials;
  fj.furniture = (fj.furniture || []).filter((f) => f.name !== "Gun ammunition").concat(T.furniture);
  fs.writeFileSync(fp, JSON.stringify(fj));
  console.log(fj.id, "+ gun ammunition:", T.furniture[0].parts.length, "parts");
}
