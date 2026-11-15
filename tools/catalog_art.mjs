// DEC1: wall-hung paintings and posters from "Paintings and Posters Collection - Game Ready" by Melon Polygons
// (https://sketchfab.com/Melonpolygons), CC-BY-4.0: one furniture piece per image (tools/art_pieces.json, split by a
// Blender background script into incoming/dec1_out/<piece>.glb, baked into content/assets/models/art_<piece>).
// The model is the visual (frame + canvas at real size); the piece hangs from the wall: origin on the wall face at the
// bottom edge, image toward +z, no collider. The room dressing picks them per wall slot (src/interior.cpp artPick,
// content/settings/art.json): framed paintings in houses, offices, restaurants and halls, posters in shops, schools,
// barracks, workshops and garages; "period" pieces anywhere, "modern" ones in modern buildings only.
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { Asset } from "./kit.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const r3 = (v) => Math.round(v * 1000) / 1000;

export function artAssets() {
  const man = JSON.parse(fs.readFileSync(path.join(here, "art_pieces.json"), "utf8"));
  return man.pieces.map((p) => {
    const [w, h, t] = p.size;
    const A = new Asset({ id: p.id, version: 1, name: (p.kind === "painting" ? "Painting" : "Poster") + " " + p.id.replace("furn_art_", "").toUpperCase(), category: "Furniture",
      description: (p.kind === "painting" ? "Framed painting" : "Poster") + ", " + Math.round(w * 100) + " x " + Math.round(h * 100) + " cm, hung on a wall" + (p.era === "modern" ? " (modern)." : "."),
      footprint: [r3(w), r3(Math.max(t, 0.02))], floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.3, noAutoPanels: true });
    A.mat("print", [0.5, 0.48, 0.44], 0.8, "plain");
    A.box([-w / 2, 0, 0], [w / 2, h, Math.max(t, 0.01)], "print", { v: 0, k: 0 });   // the model draws it; bounds for placement
    const mount = Math.max(0.2, Math.min(2.2, 1.62 - h / 2));   // centre near 1.6 m; big canvases come down
    A.meta.prop = { family: p.kind === "painting" ? "wood" : "paper", rounds: 2, blast: 6, blocking: false, wall: true, front: 0, mount: r3(mount),
      groups: ["print"], fragments: [0], keep: [], surfaces: [], bounds: [[r3(-w / 2), 0, 0], [r3(w / 2), r3(h), r3(Math.max(t, 0.01))]] };
    A.meta.art = { kind: p.kind, era: p.era };
    A.meta.model = { src: p.src, dir: "art_" + p.id.replace("furn_art_", ""), pivot: "ground", offset: [0, 0, r3(t / 2)], scale: 1, maxTex: h > 1.2 || w > 1.2 ? 512 : 256,
      lodDist: 14, cullDist: 70, credit: "Paintings and Posters Collection - Game Ready by Melon Polygons (sketchfab.com/Melonpolygons), CC-BY-4.0" };
    A.meta.nav = { effect: "none" };
    return A;
  });
}
