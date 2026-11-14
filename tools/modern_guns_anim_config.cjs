// MG1 animated viewmodels: one config per source for tools/modern_guns_anim.py.
// usage: node tools/modern_guns_anim_config.cjs <incoming/modern_guns dir> <assets_src/weapons dir> <config out dir> [<extract dir>]
// (MG2: the .rar sources extracted into <extract dir>/<name>/, as for modern_guns_config.cjs)
// Needs the static conversion first (modern_guns_config.cjs + modern_guns_convert.py: <glb>_summary.json) and the
// weapon JSONs (their measured sights / muzzle / scope eye are carried into rig space).
// Clips: a name -> source action, or -> [first, last] frame of a one-timeline source (Cinema 4D exports: cut at the
// rest frames between the actions, see docs/fps-weapons.md#mg1-animated).
const fs = require("fs"), path = require("path");
const G = path.resolve(process.argv[2]) + "/", O = path.resolve(process.argv[3]) + "/", CFG = path.resolve(process.argv[4] || ".");
const WDIR = path.resolve(__dirname, "../content/weapons") + "/";
const X = path.resolve(process.argv[5] || G + "_extracted") + "/";
// MG2: Unreal mannequin arms (AR-15, MP7, RPG-7) and the Rigify agent's arms (Scorpion, Uzi)
const manny = { MI_Manny_01: { albedo: "T_Manny_01_D.png", rough_f: 0.6 }, MI_Manny_02: { albedo: "T_Manny_02_D.png", rough_f: 0.6 } };
const agent = { Gloves: { albedo: "Gloves_BaseColor.1003.png", rough: "Gloves_Roughness.1003.png", metal: "Gloves_Metallic.1003.png" }, Cloths: { albedo: "Cloths_BaseColor.1002.png", rough: "Cloths_Roughness.1002.png", metal: "Cloths_Metallic.1002.png" },
                Watch: { albedo: "Watch_BaseColor.1004.png", rough: "Watch_Roughness.1004.png", metal: "Watch_Metallic.1004.png" }, Watch_Emission: { factor: [0.05, 0.05, 0.05], rough_f: 0.4 }, Body: { factor: [0.55, 0.4, 0.32], rough_f: 0.6 } };
const unityArms = { glove_hardknuckle: { albedo: "glove_hardknuckle_Base_color.png", normal: "glove_hardknuckle_Normal_OpenGL.png", metal: "glove_hardknuckle_Metallic.png", rough_f: 0.75 },
                    sleeve_st6_generalist: { albedo: "sleeve_st6_generalist_Base_color.png", normal: "sleeve_st6_generalist_Normal_OpenGL.png", rough_f: 0.85 },
                    v_l_skin: { factor: [0.42, 0.27, 0.19], rough_f: 0.6 } };
const c4dArms = (metal = "armmetallic.png", smooth = "armsmoothness.png") => ({ albedo: "armColor.png", normal: "armNormal.png", metal, smooth, ao: "armAO.png" });
const A = {
  modern_ak: { src: "fps-ak-animated/source/arms@ak.fbx", tex: "fps-ak-animated", stat: "modern_ak",
    keep: ["armmesh", "base", "bolt", "bullet", "mag", "release", "trigger"], rigidify: ["base", "bolt", "bullet", "mag", "release", "trigger"], gun: "base", timeline: [1, 244],
    clips: { fire: [1, 9], reload_empty: [9, 101], reload: [101, 153], inspect: [153, 201], idle: [201, 221], draw: [221, 244] },
    mats: { ak74: { albedo: "ak74Color.png", normal: "ak74Normal.png", metal: "akMetallic.png", smooth: "aksmoothness.png" }, arms: c4dArms() } },
  modern_m4: { src: "fps-animated-carbine/source/arms@carbine.fbx", tex: "fps-animated-carbine", stat: "modern_carbine",
    keep: ["armmesh", "base", "bolt", "boltcatch", "bullet", "clip", "handle", "handle2", "lid", "release", "scope", "trigger"], rigidify: ["base", "bolt", "boltcatch", "bullet", "clip", "handle", "handle2", "lid", "release", "scope", "trigger"], gun: "base", timeline: [1, 223],
    clips: { fire: [1, 9], reload_empty: [9, 69], reload: [69, 135], inspect: [135, 177], idle: [177, 205], draw: [205, 223] },
    mats: { carbine: { albedo: "carbineColor.png", normal: "carbineNormal.png", metal: "carbineMetallic.png", smooth: "carbinespecular.png" }, arms: c4dArms() } },
  modern_sniper_bolt: { src: "sniper-animated/source/arms@sniper.fbx", tex: "sniper-animated", stat: "modern_dmr", lengthPart: "base",
    keep: ["armmesh", "base", "bolt", "boltrear", "bullet", "cheekrest", "clip", "scope", "silencer", "trigger"], rigidify: ["base", "bolt", "boltrear", "bullet", "cheekrest", "clip", "scope", "silencer", "trigger"], gun: "base", timeline: [1, 201],
    clips: { fire: [1, 49], reload: [49, 101], inspect: [101, 149], idle: [149, 181], draw: [181, 201] },
    mats: { sniper: { albedo: "sniper1Color.png", normal: "sniper1Normal.png", metal: "sniper1Metallic.png", smooth: "sniper1Smoothness.png" }, arms: c4dArms() } },
  modern_lmg: { src: "lmg-animated/source/arms@LMG.fbx", tex: "lmg-animated", stat: "modern_lmg",
    keep: ["arms", "base", "bolt", "bulletfirst", ...Array.from({ length: 12 }, (_, i) => "bulletholder" + i), "ejectorplate", "handle", "lid", "loader", "loaderplate", "mag", "trigger"],
    rigidify: ["base", "bolt", "bulletfirst", ...Array.from({ length: 12 }, (_, i) => "bulletholder" + i), "ejectorplate", "handle", "lid", "loader", "loaderplate", "mag", "trigger"], gun: "base", timeline: [1, 468], hipRear: [0.12, 0.32, -0.1],
    clips: { fire: [1, 13], reload: [13, 165], reload_empty: [165, 317], inspect: [317, 381], idle: [381, 425], draw: [425, 453] },
    mats: { LMG: { albedo: "LMGcolor.png", normal: "LMGnormal.png", metal: "LMGmetallic.png", rough: "LMGroughness.png" }, arms: { albedo: "armColor.png", normal: "armNormal.png", metal: "armMetallic.png", rough: "armRoughness.png" } } },
  modern_shotgun: { src: "remington-870-shotgun-animated/source/Remington 870 - Animated.fbx", tex: "remington-870-shotgun-animated", stat: "modern_shotgun", restFrame: 2,
    keep: ["armmesh", "remington870_base", "remington870_lid", "remington870_pump", "remington870_slug", "remington870_trigger"], rigidify: ["remington870_base", "remington870_lid", "remington870_pump", "remington870_slug", "remington870_trigger"], gun: "remington870_base", timeline: [2, 144],
    // MG2 shell loop (frames at 30 fps): start = the support hand leaves the pump; insert = hand out of view -> a shell
    // pushed into the port -> hand out of view again (loops); end = the hand back on the pump. 74-106 (a drop out of
    // view, then a pump stroke) and the draw are not used.
    clips: { fire: [2, 30], reload_start: [30, 38], reload_insert: [38, 62], reload_end: [62, 74], idle: [106, 126] },
    mats: { "Material.001": { albedo: "Material.001_Base_color.png", metal: "Material.001_Metallic.png", rough: "Material.001_Roughness.png" },
            shotgun: { albedo: "shotgunColor.png", metal: "shotgunmetallic.png", smooth: "shotgunsmoothness.png" }, Mat: c4dArms() } },
  modern_scar: { src: "scar/source/SCAR.fbx", tex: "scar", stat: "modern_scar", camera: "Camera", cameraAction: "Armature|Armature|Idle",
    keep: ["Circle.001_low", "Circle.001_low.001", "Circle.001_low.002", "hardknuckle", "sleeve"], gun: "Circle.001_low",
    clips: { idle: "Armature|Armature|Idle", fire: "Armature|Armature|Shoot", reload: "Armature|Armature|Reload", draw: "Armature|Armature|Take", holster: "Armature|Armature|Hide", walk: "Armature|Armature|Walk", run: "Armature|Armature|Run" },
    mats: { Material: { albedo: "SCARL_LPForNormalFix_Material_AlbedoTransp.png", metal: "SCARL_LPForNormalFix_Material_MetallicSmoo.png", rough: "SCARL_LPForNormalFix_Material_Roughness.png" }, ...unityArms } },
  modern_m16: { src: "animated-assault-rifle/source/HandWithGloves.fbx", tex: "animated-assault-rifle", stat: "modern_m16", camera: "Camera", cameraAction: "Armature|Armature|Idle",
    keep: ["BARREL", "BARREL.001", "BARREL.002", "hardknuckle", "sleeve"], gun: "BARREL",
    clips: { idle: "Armature|Armature|Idle", fire: "Armature|Armature|Shoot", reload: "Armature|Armature|Reload", draw: "Armature|Armature|Take", holster: "Armature|Armature|Hide", inspect: "Armature|Armature|Watch" },
    mats: { M16A3: { albedo: "m16_M16A3_BaseColor.png", metal: "m16_M16A3_Metallic.png", rough: "m16_M16A3_Roughness.png" }, ...unityArms,
            glove_hardknuckle: { ...unityArms.glove_hardknuckle, rough: "glove_hardknuckle_Roughness.png" } } },
  modern_sniper_semi: { src: "fps-animations-sniper-rifle/source/BoltActionRifle.fbx", tex: "fps-animations-sniper-rifle", stat: "modern_boltsniper", camera: "Camera", cameraAction: "Rig|Rig|SRifle_Idle",
    keep: ["Rifle", "arms"], gun: "Rifle", gunBone: "Body", dropLow: 0.06, hipRear: [0.12, 0.32, -0.09],
    clips: { idle: "Rig|Rig|SRifle_Idle", fire: "Rig|Rig|FPS_Pistol_Fire", fire_bolt: "Rig|Rig|SRifle_Shot_nosight", ads_fire: "Rig|Rig|SRifle_Shot_sight", reload: "Rig|Rig|SRifle_Reload", reload_empty: "Rig|Rig|SRifle_Reload_Full", walk: "Rig|Rig|SRifle_Walk" },
    mats: { Rifle: { albedo: "Rifle_albedo.jpg", metal: "Rifle_metallic.jpg", rough: "Rifle_roughness.jpg" }, Other: { albedo: "Other_albedo.jpg", metal: "Other_metallic.jpg", rough: "Other_roughness.jpg" },
            arms: { albedo: "armColor.png", rough: "armRoughness.png" } } },
  modern_pistol: { src: "animatedpistol/source/rig.blend", tex: "animatedpistol", stat: "modern_pistol", camera: "A_Camera", cameraAction: "Idle",
    keep: ["xd_grip_safe", "xd_mag", "xd_slide", "DMC5_Soldier_Gloves", "DMC5_Soldier_Skin", "DMC5_Soldier_Shirt", "DMC5_Soldier_Gear"], gun: "xd_grip_safe", hipRear: [0.07, 0.3, -0.09],
    clips: { idle: "Idle", fire: "Shoot", reload: "Reload", draw: "Take", holster: "Hide", run: "Run" },
    mats: { "None.001": { albedo: "for_texturing_None.001_Diffuse.png", gloss: "for_texturing_None.001_Glossiness.png", spec: "for_texturing_None.001_Specular.png" },
            DMC5_Soldier_Hands_Mat: { albedo: "soldierhand_d.png", rough_f: 0.7 }, DMC5_Soldier_Shirt_Mat: { factor: [0.09, 0.1, 0.07], rough_f: 0.9 }, DMC5_Soldier_Gear_Mat: { factor: [0.05, 0.05, 0.05], rough_f: 0.7 } } },
  // ---- MG2 (no draw clips, no movement clips; still idles)
  modern_ar15: { src: "ar-15-animations-miniset/source/Base_Rig_Test_Scetc.fbx", tex: "ar-15-animations-miniset", stat: "modern_ar15", keep: ["AR_15", "SKM_Manny_LOD0_Hands"], gun: "AR_15", cutLens: [{ a: [0, 0.124, 0.25], b: [0, 0.124, 0.47], r: 0.02 }], decimate: { AR_15: 0.3, SKM_Manny_LOD0_Hands: 0.35 },
    // the set has no idle and no fire clip: the idle is the draw's last frame (held), a shot is a procedural kick
    clips: { idle: { action: "root|Draw", range: [117, 118] }, reload: "root|Reload", reload_empty: "root|Full_Reload" },
    mats: { "texture set 1": { albedo: "texture_set_1_Base_color.png", rough: "texture_set_1_Roughness.png" }, "texture set 2": { albedo: "texture_set_2_Base_color.png", rough: "texture_set_2_Roughness.png", metal: "texture_set_2_Metallic.png" }, ...manny } },
  modern_mp7: { src: X + "mp7/MP7A1_Animations/model/MP7A1.fbx", abs: true, tex: "mp7a1-animations", stat: "modern_mp7", keep: ["mp7a1_Object", "SK_Mannequin_Arms"], gun: "mp7a1_Object", cutLens: [{ a: [0.002, 0.141, 0.27], b: [0.002, 0.141, 0.39], r: 0.013 }], decimate: { mp7a1_Object: 0.7, SK_Mannequin_Arms: 0.35 },
    clips: { idle: "root|root|MC_MP7_static_pose", fire: "root|root|MC_MP7_shoot", reload: "root|root|MC_MP7_reload", reload_empty: "root|root|MC_MP7_full_reload" },
    mats: { ...Object.fromEntries([["Slot_1", "Slot_1"], ["slot_2", "slot_2"], ["slot_3", "slot_3"]].map(([m, f]) => [m, { albedo: `mp7_low_${f}_BaseColor.png`, rough: `mp7_low_${f}_Roughness.png`, metal: `mp7_low_${f}_Metallic.png` }])), ...manny } },
  modern_rpg7: { src: X + "rpg7/RPG7_Animations/model/RPG_7.fbx", abs: true, tex: "rpg-7-animations", stat: "modern_rpg7", keep: ["rpg_low", "SK_Mannequin_Arms"], gun: "rpg_low", decimate: { SK_Mannequin_Arms: 0.35 },
    clips: { idle: "root|MC_RPG7_static_pose", idle_empty: "root|MC_RPG7_static_pose_empty", fire: "root|MC_RPG7_shoot", reload: "root|MC_RPG7_reload" },
    mats: { "rpg-7": { albedo: "RPG-7_low_rpg-7_BaseColor.png", rough: "RPG-7_low_rpg-7_Roughness.png", metal: "RPG-7_low_rpg-7_Metallic.png" }, rockets: { albedo: "RPG-7_low_rockets_BaseColor.png", rough: "RPG-7_low_rockets_Roughness.png", metal: "RPG-7_low_rockets_Metallic.png" }, ...manny } },
  modern_scorpion: { src: X + "scorpion/CZ_Scorpion_Evo3_A1_Anim.blend", abs: true, tex: "scorpion-first-person-animations-2026-remake", stat: "modern_scorpion", keep: ["CZScorpion", "Mesh"], gun: "CZScorpion", defOnly: true,
    clips: { idle: "Idle", fire: "Fire", reload: "Reload", reload_empty: "Reload_Empty" },
    mats: { CZScorpion: { factor: [0.055, 0.055, 0.05], rough_f: 0.55 }, ...agent } },
  modern_uzi: { src: X + "uzi/1Matzh_Uzi_Animations.blend", abs: true, tex: "uzi-first-person-animations-2026-remake", stat: "modern_uzi", keep: ["UZI", "Mesh"], gun: "UZI", defOnly: true, decimate: { UZI: 0.7 },
    clips: { idle: "Idle", fire: "Fire", reload: "Reload", reload_empty: "Reload_Empty" },
    mats: { UZI: { albedo: "UZI_Base_color.png", rough: "UZI_Roughness.png", metal: "UZI_Metallic.png" }, "UZI Magazine and Bullet": { albedo: "UZI_Magazine_and_Bullet_Base_color.png", rough: "UZI_Magazine_and_Bullet_Roughness.png", metal: "UZI_Magazine_and_Bullet_Metallic.png" }, ...agent } },
  modern_shotgun2: { src: "shotgun-animated/source/shotgunAnimated.fbx", tex: "shotgun-animated", stat: "modern_shotgun2", keep: ["boomstick", "arm"], gun: "boomstick", gunBone: "weapon",
    // no idle clip: the first frame of Fire, held; fire = Fire then PostFire (the pump stroke); tube-fed shell loop
    clips: { idle: { action: "Armature|Armature|Fire", range: [1, 2] }, fire: [{ action: "Armature|Armature|Fire" }, { action: "Armature|Armature|PostFire" }],
             reload_start: "Armature|Armature|PriorToReload", reload_insert: "Armature|Armature|ReloadOne", reload_end: "Armature|Armature|ReloadLastOne" },
    mats: { boomstick: { albedo: "boomstickColor.png", rough: "boomstickRoughness.png", metal: "boomstickmetallic.png" }, arms: { albedo: "armColor.png", rough: "armRoughness.png" } } },
};
for (const [id, a] of Object.entries(A)) {
  const w = JSON.parse(fs.readFileSync(WDIR + id + ".json", "utf8"));
  const sum = JSON.parse(fs.readFileSync(O + a.stat + "_summary.json", "utf8"));
  const parts = a.lengthPart ? sum.parts.filter(p => p.name === a.lengthPart) : sum.parts;
  const mn = [0, 1, 2].map(i => Math.min(...parts.map(p => p.gltf_min[i]))), mx = [0, 1, 2].map(i => Math.max(...parts.map(p => p.gltf_max[i])));
  const s = w.model.realLength / (mx[2] - mn[2]);   // the static import's scale (realLength over the length parts' extent)
  const points = { rear: w.sights.rear, front: w.sights.front, muzzle: w.muzzle };
  if (w.scope) points.eye = w.scope.eye;
  const cfg = { ...a, src: a.abs ? a.src : G + a.src, texdir: G + a.tex + "/textures", out: O + id + "_vm.glb", points,
    static: { s, o: [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mn[2]] } };
  fs.writeFileSync(path.join(CFG, "anim_" + id + ".json"), JSON.stringify(cfg, null, 1));
}
console.log(Object.keys(A).join(" "));
