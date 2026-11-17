// MOD1: modern props from user-supplied models (incoming/modern_models, baked by the engine:
//   NormandyMapmaker --import-weapon-models <incoming/modern_models> <content root>
// into content/assets/models/<dir>/). The parts are invisible colliders; the model is visual only (src/assets.cpp).
// Credits: docs/credits.md. concrete_barriers.glb is converted from an FBX pack of unknown licence: its baked files
// are git-ignored (local only) and the asset falls back to its visible box parts when the bake is missing.
import { Asset } from "./kit.mjs";
import { setNav, cover } from "./catalog_atlantic.mjs";
import fs from "fs";

function modelAsset(id, name, category, desc, model, size, opt = {}) {
  const A = new Asset({ id, version: 1, name, category, description: desc, footprint: [size[0], size[2]], floors: 0, enterable: false, plinthDepth: 0, maxGrade: 5, noAutoPanels: true });
  A.mat("collider", [0.4, 0.4, 0.38], 0.9, "concrete", { texture: "concrete_aged" });
  // the collider box (or PR1: one box per piece of an open frame, opt.colliders [cx, cy, cz, sx, sy, sz]); visible only
  // when the model bake is missing (opt.fallback)
  for (const [cx, cy, cz, sx, sy, sz] of opt.colliders ?? [[0, size[1] / 2, 0, ...size]])
  { A.box([cx - sx / 2, cy - sy / 2, cz - sz / 2], [cx + sx / 2, cy + sy / 2, cz + sz / 2], "collider", opt.fallback ? {} : { v: 0 });
    if (opt.local) A.parts[A.parts.length - 1].mf = 1; }   // PR1 local-only bake: the stand-in hides once the model is on disk
  opt.extra?.(A);
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
    // PR1: the user's supplied models (incoming/pr1, converted by tools/props/convert_supplied.py; credits in docs/credits.md)
    modelAsset("mod_play_slide_old", "Old playground slide (model)", "Modern outdoor", "A rusted steel children's slide with a ladder and side rails, paint flaking.",
      { src: "pr1_slide.glb", dir: "mod_play_slide_old", jpgMaps: true, scale: 1, simplify: 1, lod1: 0.4, lodDist: 15, credit: "Abandoned children's slide by sergeilihandristov (sketchfab.com/sergeilihandristov), CC-BY-4.0, sketchfab.com/3d-models/abandoned-childrens-slide-11de2b21ec264c0a814ac024c45ab425" },
      [3.21, 2.09, 0.63], { colliders: [[0.2, 0.77, -0.0, 2.58, 1.49, 0.6], [0.81, 0.44, 0.3, 1.4, 0.89, 0.04], [0.86, 0.49, -0.3, 1.49, 0.8, 0.04], [-1.35, 0.76, -0.3, 0.52, 1.52, 0.04], [-1.35, 0.76, 0.3, 0.52, 1.52, 0.04], [-0.17, 1.3, -0.3, 0.78, 0.85, 0.04], [-0.17, 1.3, 0.3, 0.78, 0.85, 0.04], [-0.2, 1.19, -0.3, 0.67, 0.66, 0.04], [-0.2, 1.19, 0.3, 0.67, 0.66, 0.04], [-0.15, 1.27, -0.29, 0.61, 0.61, 0.04]], cover: [] }),
    modelAsset("mod_payphone", "Public payphone (model)", "Modern: Urban", "A battered public payphone with a handset, keypad and card reader on a steel post.",
      { src: "ph_korean_public_payphone_01.glb", dir: "mod_payphone", jpgMaps: true, scale: 1, maxTex: 1024, simplify: 0.5, lod1: 0.2, lodDist: 15, offset: [0, 0.86, 0], credit: "Korean Public Payphone 01 by UM JOORIN, Poly Haven (polyhaven.com/a/korean_public_payphone_01), CC0" },
      [0.42, 1.6, 0.32], { cover: [], extra: (A) => { A.mat("post", [0.3, 0.31, 0.3], 0.5, "metal", { metallic: 0.6, texture: "steel_worn_grey" });
        A.box([-0.045, 0, -0.17], [0.045, 0.9, -0.08], "post", { k: 0, bv: 0.01 }); A.box([-0.12, 0, -0.22], [0.12, 0.012, -0.03], "post", { k: 0, bv: 0.004 }); } }),
    modelAsset("mod_play_swings_old", "Old playground set: swings, arch ladder, seesaw (model)", "Modern outdoor", "A rusted playground corner: a two-seat swing frame, an arched climbing ladder and a seesaw.",
      { src: "pr1_swings.glb", dir: "mod_play_swings_old", jpgMaps: true, scale: 1, maxTex: 1024, simplify: 1, lod1: 0.4, lodDist: 15, credit: "abandoned playground (abandoned-playground.zip supplied by the user; 3ds Max scene by 'Sergei', likely sergeilihandristov on Sketchfab; source URL and licence not in the files: local only)" },
      [4.92, 1.95, 4.55], { colliders: [[-1.33, 0.65, 1.14, 1.2, 1.27, 2.27], [-1.86, 0.65, 0.87, 1.2, 1.27, 2.27], [1.66, 0.44, 0.82, 1.59, 0.88, 1.77], [1.59, 0.44, 0.75, 1.59, 0.88, 1.77], [0.77, 0.98, -2.24, 0.58, 1.95, 0.06], [0.77, 0.98, 0.9, 0.58, 1.95, 0.06], [0.18, 0.98, -2.24, 0.58, 1.95, 0.06], [0.18, 0.98, 0.9, 0.58, 1.95, 0.06], [0.83, 1.03, -1.14, 0.85, 1.5, 0.04], [0.83, 1.03, -1.66, 0.85, 1.5, 0.04], [0.15, 1.01, 0.31, 0.79, 1.55, 0.04], [0.15, 1.01, -0.21, 0.79, 1.55, 0.04]], cover: [], fallback: true, local: true }),
    modelAsset("mod_hydrant_cast", "Fire hydrant, cast iron (model)", "Modern: Urban", "A green cast-iron pillar fire hydrant, chipped and tagged.",
      { src: "pr1_hydrant.glb", dir: "mod_hydrant_cast", jpgMaps: true, scale: 1, maxTex: 1024, simplify: 1, lod1: 0.3, lodDist: 15, credit: "hydrant (hydrant-legendgamedev.zip supplied by the user; author and source not in the files: local only)" },
      [0.44, 0.8, 0.4], { cover: [], fallback: true, local: true }),
    modelAsset("mod_traffic_barrel", "Traffic barrier barrel (model)", "Modern: Barriers", "An orange and white plastic traffic drum on a ballast base.",
      { src: "pr1_traffic_barrel.glb", dir: "mod_traffic_barrel", jpgMaps: true, scale: 1, maxTex: 1024, simplify: 0.4, lod1: 0.25, lodDist: 15, credit: "[FREE] traffic barrier barrel by LaSamFish (sketchfab.com/lasamfish), Sketchfab Standard licence (not CC): local only, sketchfab.com/3d-models/free-traffic-barrier-barrel-0e40ea8b59ac44a0ba51732ad462cec8" },
      [0.83, 0.85, 0.83], { cover: [], fallback: true, local: true }),
    // PR1 Poly Haven props (CC0): tools/props/ph_props.json, written by tools/props/ph_batch.py
    ...Object.entries(JSON.parse(fs.readFileSync(new URL("./props/ph_props.json", import.meta.url), "utf8"))).map(([id, p]) =>
      modelAsset(id, p.name + " (model)", p.category, `${p.name}, ${p.size.map((v) => v.toFixed(2)).join(" x ")} m (Poly Haven).`,
        { src: id + ".glb", dir: id, jpgMaps: true, scale: 1, maxTex: p.maxTex, simplify: 0.5, lod1: 0.25, lodDist: p.lodDist, credit: p.credit, ...(p.foliage ? { foliage: true, wind: 0 } : {}) },
        p.size, { colliders: p.colliders.length ? p.colliders : undefined, cover: p.size[1] >= 0.8 && Math.max(p.size[0], p.size[2]) >= 0.6 ? undefined : [] })),
  ];
}
