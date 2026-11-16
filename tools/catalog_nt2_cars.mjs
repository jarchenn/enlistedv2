// NT2: parked and wrecked cars from "Generic passenger car pack" by Comrade1280 (Sketchfab, CC BY 4.0): one model
// per car (incoming/modern_models/car_<kind>.glb, converted by a Blender background script), two variants each:
// mod_pcar_<kind> (abandoned: dusty, faded) and mod_pcar_<kind>_wreck (rust-tinted, a wheel gone). The bakes are
// committed (content/assets/models/pcar_*); without them the asset shows the generated car of the nearest type ("mf" parts, hidden once
// the bake is on disk) and keeps its colliders and cover.
import { modernPropsAssets } from "./catalog_modern_props.mjs";

const CARS = [   // kind, size [w, h, l] m (cars.json), generated stand-in (intact, wreck)
  ["compact", [1.81, 1.48, 3.67], "mod_hatchback", "mod_sedan_wrecked"], ["hatchback", [1.96, 1.52, 4.45], "mod_hatchback", "mod_sedan_wrecked"],
  ["minivan", [2.1, 1.98, 5.19], "mod_delivery_van", "mod_van_wrecked"], ["offroad", [2.21, 1.77, 4.43], "mod_suv", "mod_sedan_wrecked"],
  ["pickup", [2.33, 1.91, 5.83], "mod_pickup", "mod_sedan_wrecked"], ["sedan", [2.19, 1.51, 4.91], "mod_sedan", "mod_sedan_wrecked"],
  ["sport", [1.99, 1.17, 4.4], "mod_sedan_red", "mod_sedan_wrecked"], ["suv", [2.33, 1.95, 5.19], "mod_suv", "mod_sedan_wrecked"],
  ["wagon", [2.19, 1.55, 4.93], "mod_sedan", "mod_sedan_wrecked"],
];
const CREDIT = "Generic passenger car pack by Comrade1280 (https://sketchfab.com/3d-models/generic-passenger-car-pack-20f9af9b8a404d5cb022ac6fe87f21f5), CC BY 4.0, modified";

export function nt2CarAssets() {
  const gen = new Map(modernPropsAssets().map((A) => [A.json().id, A]));
  const out = [];
  for (const [kind, size, intact, wreck] of CARS) {
    for (const wrecked of [false, true]) {
      const src = gen.get(wrecked ? wreck : intact);
      const id = "mod_pcar_" + kind + (wrecked ? "_wreck" : "");
      out.push({ json: () => {
        const j = JSON.parse(JSON.stringify(src.json()));
        j.id = id; j.version = 1;
        j.name = (wrecked ? "Wrecked " : "Abandoned ") + kind + " (model)";
        j.description = wrecked ? "A rusted, stripped " + kind + " with a wheel gone, left where it died." : "A dusty, faded " + kind + " abandoned at the kerb.";
        j.footprint = [size[0], size[2]];
        for (const p of j.parts) if (p.v !== 0) p.mf = 1;   // stand-in look; colliders stay
        j.model = { src: "car_" + kind + ".glb", dir: "pcar_" + kind + (wrecked ? "_wreck" : ""), pivot: "ground", scale: 1, maxTex: 1024, simplify: 0.6,
          lodDist: 35, cullDist: 400, tint: wrecked ? [0.62, 0.5, 0.4] : [0.85, 0.83, 0.79], ...(wrecked ? { hide: ["wheel_0"] } : {}), credit: CREDIT };
        return j;
      } });
    }
  }
  return out;
}
