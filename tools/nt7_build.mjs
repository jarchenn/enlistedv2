// NT7: rebuild only the named Newtown buildings and their abandoned variants (the full tools/build_mpu_assets.mjs takes
// over ten minutes); the same JSON (tdMerge, check, dezfight) as the full build writes.
//   node tools/nt7_build.mjs shopSmall cafe houseSmall
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as B from "./catalog_newtown_b.mjs";
import * as C from "./catalog_newtown_c.mjs";
import * as NA from "./catalog_newtown_a.mjs";
import * as MA from "./catalog_modern_buildings_a.mjs";
import { ruinVariant, ruinVariants } from "./ruin_kit.mjs";
import { mod2Assets } from "./catalog_mod2.mjs";
import { modernOutdoorAssets } from "./catalog_modern_outdoor.mjs";
import { modernBuildingsB } from "./catalog_modern_buildings_b.mjs";
import { dezfight } from "./audit_zfight.mjs";
import { rb1Assets } from "./catalog_rb1.mjs";
import { nt3Assets } from "./catalog_nt3.mjs";
import { bocageAssets } from "./catalog_bocage.mjs";

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "content", "assets");
// the full build runs newtownAssetsB() etc., which add tdMerge and a check; find each builder's finished asset there
const lists = [B.newtownAssetsB, C.newtownAssetsC, NA.newtownAssetsA, MA.modernBuildingsA, rb1Assets].filter(Boolean);
const want = new Set(process.argv.slice(2));
// --all-ruins: every abandoned variant, from the same lists as the full build (it stops early on the range kit's
// missing nav blocks, before it reaches the ruins)
if (want.has("--all-ruins")) {
  const src = [...mod2Assets(), ...modernOutdoorAssets(), ...MA.modernBuildingsA(), ...modernBuildingsB(), ...NA.newtownAssetsA(), ...B.newtownAssetsB(), ...C.newtownAssetsC()];
  let n = 0;
  for (const v of ruinVariants(src)) {
    const z = JSON.parse(JSON.stringify(v.json()));
    dezfight(z);
    const f = path.join(out, z.id + ".json"), text = JSON.stringify(z);
    if (!fs.existsSync(f) || fs.readFileSync(f, "utf8") !== text) { fs.writeFileSync(f, text); n++; }
  }
  console.log("ruins written:", n);
  process.exit(0);
}
// --all (TX3): every asset of these lists, the outdoor / nt3 / bocage ones and the modern ruin variants, written only where
// the JSON changed (the full build stops at the range kit before writing anything)
if (want.has("--all")) {
  const mod = [...mod2Assets(), ...modernOutdoorAssets(), ...MA.modernBuildingsA(), ...modernBuildingsB(), ...NA.newtownAssetsA(), ...B.newtownAssetsB(), ...C.newtownAssetsC()];
  let n = 0;
  for (const v of [...rb1Assets(), ...modernOutdoorAssets(), ...MA.modernBuildingsA(), ...modernBuildingsB(), ...NA.newtownAssetsA(), ...B.newtownAssetsB(), ...C.newtownAssetsC(), ...mod2Assets(), ...nt3Assets(), ...bocageAssets(), ...ruinVariants(mod)]) {
    const z = JSON.parse(JSON.stringify(v.json()));
    dezfight(z);
    const f = path.join(out, z.id + ".json"), text = JSON.stringify(z);
    if (!fs.existsSync(f) || fs.readFileSync(f, "utf8") !== text) { fs.writeFileSync(f, text); n++; }
  }
  console.log("written:", n);
  process.exit(0);
}
for (const list of lists) {
  for (const A of list()) {
    const j = A.json();
    const fn = [...want].find((w) => j.id === w || j.id === w + "_ruin");
    if (!fn) continue;
    for (const v of j.id.startsWith("rb1_") ? [A] : [A, ruinVariant(A)]) {   // RB1 buildings are abandoned already: no _ruin variant
      const z = JSON.parse(JSON.stringify(v.json()));
      dezfight(z);
      fs.writeFileSync(path.join(out, z.id + ".json"), JSON.stringify(z));
      console.log(z.id.padEnd(28), z.parts.length, "parts");
    }
  }
}
