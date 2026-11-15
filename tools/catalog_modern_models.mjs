// MOD1: modern props from user-supplied models (incoming/modern_models, baked by the engine:
//   NormandyMapmaker --import-weapon-models <incoming/modern_models> <content root>
// into content/assets/models/<dir>/). The parts are invisible colliders; the model is visual only (src/assets.cpp).
// Credits: docs/credits.md. concrete_barriers.glb is converted from an FBX pack of unknown licence: its baked files
// are git-ignored (local only) and the asset falls back to its visible box parts when the bake is missing.
import { Asset } from "./kit.mjs";
import { setNav, cover } from "./catalog_atlantic.mjs";

function modelAsset(id, name, category, desc, model, size, opt = {}) {
  const A = new Asset({ id, version: 1, name, category, description: desc, footprint: [size[0], size[2]], floors: 0, enterable: false, plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  A.mat("collider", [0.4, 0.4, 0.38], 0.9, "concrete", { texture: "concrete_aged" });
  // the collider box; visible only when the model bake is missing (opt.fallback)
  A.box([-size[0] / 2, 0, -size[2] / 2], [size[0] / 2, size[1], size[2] / 2], "collider", opt.fallback ? {} : { v: 0 });
  A.meta.model = { pivot: "ground", maxTex: 1024, simplify: 0.5, lodDist: 30, cullDist: 300, ...model };
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], areas: [],
    cover: opt.cover ?? [cover([0, 0, size[2] / 2 + 0.5], 0, size[1] > 1.3 ? "high" : "low", ["over", "left", "right"]), cover([0, 0, -size[2] / 2 - 0.5], 180, size[1] > 1.3 ? "high" : "low", ["over", "left", "right"])] });
  return A;
}

export function modernModelAssets() {
  return [
    modelAsset("mod_car_wreck_sedan", "Wrecked sedan (model)", "Modern: Vehicles", "A stripped, rusted full-size sedan wreck.",
      { src: "car_destroyed.glb", dir: "mod_car_wreck_sedan", scale: 1.5, credit: "Destroyed Car 03 / Backrooms Car (Gameready ver) by Renafox (sketchfab.com/kryik1023), CC-BY-4.0" }, [4.7, 1.44, 2.46]),
    modelAsset("mod_cars_burned_pair", "Burned-out cars (model)", "Modern: Vehicles", "Two burned-out car shells side by side.",
      { src: "cars_burned.glb", dir: "mod_cars_burned_pair", scale: 0.0155, credit: "Burned-out Cars by Renafox (sketchfab.com/kryik1023), CC-BY-4.0" }, [4.4, 1.35, 4.6]),
    modelAsset("mod_ammo_can", "Ammo can (model)", "Modern: Military", "A steel ammunition can with a carry handle.",
      { src: "ammo_box.glb", dir: "mod_ammo_can", scale: 0.01, maxTex: 512, credit: "Ammo Box by murilojones (sketchfab.com/murilojones), CC-BY-4.0" }, [0.33, 0.25, 0.17], { cover: [] }),
    modelAsset("mod_barriers_weathered", "Weathered barriers, pair (model)", "Modern: Barriers", "Two weathered concrete road barriers, one red, one yellow.",
      { src: "concrete_barriers.glb", dir: "mod_barriers_weathered", scale: 1, credit: "weathered concrete barriers (Sketchfab pack supplied by the user, licence not stated: local only)" }, [3.97, 0.83, 1.49], { fallback: false }),
  ];
}
