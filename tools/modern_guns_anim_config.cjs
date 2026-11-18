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
const M12 = path.resolve(process.env.MG12_SRC || "E:/nmk/incoming/mg12") + "/";   // MG12 sources (moved off C: to E:, user disk policy)
const NG3 = path.resolve(process.env.NG3_SRC || "E:/nmk/incoming/ng3") + "/";   // NG3 sources (licences unknown: local-only bakes)
const NG1 = path.resolve(process.env.NG1_SRC || "E:/nmk/incoming/ng1") + "/";   // NG1 sources (licences unknown: local-only bakes into models/<id>_local)
// MG2: Unreal mannequin arms (AR-15, MP7, RPG-7) and the Rigify agent's arms (Scorpion, Uzi)
const manny = { MI_Manny_01: { albedo: "T_Manny_01_D.png", rough_f: 0.6 }, MI_Manny_02: { albedo: "T_Manny_02_D.png", rough_f: 0.6 } };
const agent = { Gloves: { albedo: "Gloves_BaseColor.1003.png", rough: "Gloves_Roughness.1003.png", metal: "Gloves_Metallic.1003.png" }, Cloths: { albedo: "Cloths_BaseColor.1002.png", rough: "Cloths_Roughness.1002.png", metal: "Cloths_Metallic.1002.png" },
                Watch: { albedo: "Watch_BaseColor.1004.png", rough: "Watch_Roughness.1004.png", metal: "Watch_Metallic.1004.png" }, Watch_Emission: { factor: [0.05, 0.05, 0.05], rough_f: 0.4 }, Body: { factor: [0.55, 0.4, 0.32], rough_f: 0.6 } };
const unityArms = { glove_hardknuckle: { albedo: "glove_hardknuckle_Base_color.png", normal: "glove_hardknuckle_Normal_OpenGL.png", metal: "glove_hardknuckle_Metallic.png", rough_f: 0.75 },
                    sleeve_st6_generalist: { albedo: "sleeve_st6_generalist_Base_color.png", normal: "sleeve_st6_generalist_Normal_OpenGL.png", rough_f: 0.85 },
                    v_l_skin: { factor: [0.42, 0.27, 0.19], rough_f: 0.6 } };
const ng2Arms = { matte: [0.7, 0.95] };   // NG2: sleeves, gloves and skin matte (their roughness maps into 0.7-0.95, never metallic)
const c4dArms = (metal = "armmetallic.png", smooth = "armsmoothness.png") => ({ albedo: "armColor.png", normal: "armNormal.png", metal, smooth, ao: "armAO.png" });
const A = {
  _modern_ak_mg1: {   // NG1: parked (the AK-74M pack replaced this rig)
    src: "fps-ak-animated/source/arms@ak.fbx", tex: "fps-ak-animated", stat: "modern_ak",
    keep: ["armmesh", "base", "bolt", "bullet", "mag", "release", "trigger"], rigidify: ["base", "bolt", "bullet", "mag", "release", "trigger"], gun: "base", timeline: [1, 244],
    clips: { fire: [1, 9], reload_empty: [9, 101], reload: [101, 153], inspect: [153, 201], idle: [201, 221], draw: [221, 244] },
    mats: { ak74: { albedo: "ak74Color.png", normal: "ak74Normal.png", metal: "akMetallic.png", smooth: "aksmoothness.png" }, arms: c4dArms() } },
  modern_m4: { src: "fps-animated-carbine/source/arms@carbine.fbx", tex: "fps-animated-carbine", stat: "modern_carbine",
    cutLens: [{ a: [0.0018, 0.11, 0.28], b: [0.0018, 0.11, 0.39], r: 0.0135, all: true, black: true, blackR: 0.016, widen: 1.2, widenMax: 0.0145 }],   // MG3 / MG4: lenses and inner rims (axis-facing) inside 13.5 mm deleted; the tube and housing walls there kept, flat black
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
    // MG3: the user's reflex sight (red-dot-sight/rds_low.fbx) on the receiver top, its blue lens dropped (the window is open)
    attach: { src: G + "red-dot-sight/source/rds_low.fbx", mesh: "RedDotSight_mesh", dropMats: ["08 - Default"], anchor: [-0.002, 0, -0.0133], at: [-0.0015, 0.094, 0.47] },
    mats: { "01 - Default": { albedo: G + "red-dot-sight/textures/rds_low_DefaultMaterial_BaseColor.tga.png", rough: G + "red-dot-sight/textures/rds_low_DefaultMaterial_Roughness.tga.png", metal: G + "red-dot-sight/textures/rds_low_DefaultMaterial_Metallic.tga.png" },
            "Material.001": { albedo: "Material.001_Base_color.png", metal: "Material.001_Metallic.png", rough: "Material.001_Roughness.png" },
            shotgun: { albedo: "shotgunColor.png", metal: "shotgunmetallic.png", smooth: "shotgunsmoothness.png" }, Mat: c4dArms() } },
  // modern_scar: the MG1 SCAR.fbx rig, replaced in MG7 by the SCAR-H GLB (below)
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
    dropBones: { DMC5_Soldier_Shirt: ["UpperArm.L", "UpperArm.R"], DMC5_Soldier_Gear: ["UpperArm.R"] },   // NG2: the shirt's upper arms (across the eye in the reload)
    mats: { "None.001": { albedo: "for_texturing_None.001_Diffuse.png", gloss: "for_texturing_None.001_Glossiness.png", spec: "for_texturing_None.001_Specular.png" },
            // NG2: the shirt / gear on their own textures and normal maps (they were flat colours: a plastic sheen on the sleeves), all matte
            DMC5_Soldier_Hands_Mat: { albedo: "soldierhand_d.png", normal: "soldierhand_n.png", rough_f: 0.8, matte: [0.8, 0.8] },
            DMC5_Soldier_Shirt_Mat: { albedo: "pl2000_main3_d.png", normal: "pl2000_main3_n.png", rough_f: 0.92, matte: [0.92, 0.92] },
            DMC5_Soldier_Gear_Mat: { albedo: "pl2000_main2_d.png", normal: "pl2000_main2_n.png", rough_f: 0.85, matte: [0.85, 0.85] } }, normals: true },
  // ---- MG2 (no draw clips, no movement clips; still idles)
  modern_ar15: { src: "ar-15-animations-miniset/source/Base_Rig_Test_Scetc.fbx", tex: "ar-15-animations-miniset", stat: "modern_ar15", keep: ["AR_15", "SKM_Manny_LOD0_Hands"], gun: "AR_15", cutLens: [{ a: [0, 0.124, 0.25], b: [0, 0.124, 0.47], r: 0.02 }],
    // the set has no idle and no fire clip: the idle is the draw's last frame (held), a shot is a procedural kick
    clips: { idle: { action: "root|Draw", range: [117, 118] }, reload: "root|Reload", reload_empty: "root|Full_Reload" },
    mats: { "texture set 1": { albedo: "texture_set_1_Base_color.png", rough: "texture_set_1_Roughness.png" }, "texture set 2": { albedo: "texture_set_2_Base_color.png", rough: "texture_set_2_Roughness.png", metal: "texture_set_2_Metallic.png" }, ...manny } },
  modern_mp7: { src: X + "mp7/MP7A1_Animations/model/MP7A1.fbx", abs: true, tex: "mp7a1-animations", stat: "modern_mp7", keep: ["mp7a1_Object", "SK_Mannequin_Arms"], gun: "mp7a1_Object", cutLens: [{ a: [0.002, 0.141, 0.27], b: [0.002, 0.141, 0.39], r: 0.016, islands: true, black: true, blackR: 0.017, widen: 1.2, widenMax: 0.0165 }],
    clips: { idle: "root|root|MC_MP7_static_pose", fire: "root|root|MC_MP7_shoot", reload: "root|root|MC_MP7_reload", reload_empty: "root|root|MC_MP7_full_reload" },
    mats: { ...Object.fromEntries([["Slot_1", "Slot_1"], ["slot_2", "slot_2"], ["slot_3", "slot_3"]].map(([m, f]) => [m, { albedo: `mp7_low_${f}_BaseColor.png`, rough: `mp7_low_${f}_Roughness.png`, metal: `mp7_low_${f}_Metallic.png` }])), ...manny } },
  modern_rpg7: { src: X + "rpg7/RPG7_Animations/model/RPG_7.fbx", abs: true, tex: "rpg-7-animations", stat: "modern_rpg7", keep: ["rpg_low", "SK_Mannequin_Arms"], gun: "rpg_low",
    clips: { idle: "root|MC_RPG7_static_pose", idle_empty: "root|MC_RPG7_static_pose_empty", fire: "root|MC_RPG7_shoot", reload: "root|MC_RPG7_reload" },
    mats: { "rpg-7": { albedo: "RPG-7_low_rpg-7_BaseColor.png", rough: "RPG-7_low_rpg-7_Roughness.png", metal: "RPG-7_low_rpg-7_Metallic.png" }, rockets: { albedo: "RPG-7_low_rockets_BaseColor.png", rough: "RPG-7_low_rockets_Roughness.png", metal: "RPG-7_low_rockets_Metallic.png" }, ...manny } },
  // WP2: the gun has no textures in the set: tools/bake_scorpion_textures.py bakes a 4096 x 2048 set (both UDIM tiles in one,
  // its UVs halved) into <set>/wp2_bake with the UV-halved source blend; normals opt in (the other rigs keep their look)
  modern_scorpion: { src: G + "scorpion-first-person-animations-2026-remake/wp2_bake/scorpion_uv_halved.blend", abs: true, maxTex: 4096, normals: true, tex: "scorpion-first-person-animations-2026-remake", stat: "modern_scorpion", keep: ["CZScorpion", "Mesh"], gun: "CZScorpion", defOnly: true,
    clips: { idle: "Idle", fire: "Fire", reload: "Reload", reload_empty: "Reload_Empty" },
    mats: { CZScorpion: { albedo: "../wp2_bake/scorpion_base.png", rough: "../wp2_bake/scorpion_rough.png", normal: "../wp2_bake/scorpion_normal.png", metal: "../wp2_bake/scorpion_metal.png" }, ...agent } },
  modern_uzi: { src: X + "uzi/1Matzh_Uzi_Animations.blend", abs: true, tex: "uzi-first-person-animations-2026-remake", stat: "modern_uzi", keep: ["UZI", "Mesh"], gun: "UZI", defOnly: true,
    clips: { idle: "Idle", fire: "Fire", reload: "Reload", reload_empty: "Reload_Empty" },
    mats: { UZI: { albedo: "UZI_Base_color.png", rough: "UZI_Roughness.png", metal: "UZI_Metallic.png" }, "UZI Magazine and Bullet": { albedo: "UZI_Magazine_and_Bullet_Base_color.png", rough: "UZI_Magazine_and_Bullet_Roughness.png", metal: "UZI_Magazine_and_Bullet_Metallic.png" }, ...agent } },
  modern_shotgun2: { src: "shotgun-animated/source/shotgunAnimated.fbx", tex: "shotgun-animated", stat: "modern_shotgun2", keep: ["boomstick", "arm"], gun: "boomstick", gunBone: "weapon",
    // no idle clip: the first frame of Fire, held; fire = Fire then PostFire (the pump stroke); tube-fed shell loop
    clips: { idle: { action: "Armature|Armature|Fire", range: [1, 2] }, fire: [{ action: "Armature|Armature|Fire" }, { action: "Armature|Armature|PostFire" }],
             reload_start: "Armature|Armature|PriorToReload", reload_insert: "Armature|Armature|ReloadOne", reload_end: "Armature|Armature|ReloadLastOne" },
    mats: { boomstick: { albedo: "boomstickColor.png", rough: "boomstickRoughness.png", metal: "boomstickmetallic.png" }, arms: { albedo: "armColor.png", rough: "armRoughness.png" } } },
  // ---- MG3 (Cinema 4D one-timeline sources, as the AK: gun parts are object-animated -> bones)
  modern_fal: { src: "battle-rifle-animated/source/arms@FAL.fbx", tex: "battle-rifle-animated", stat: "modern_fal",
    keep: ["arms", "base", "bolt", "bullet", "bullet_2", "clip", "handle", "magrelease", "trigger"], rigidify: ["base", "bolt", "bullet", "bullet_2", "clip", "handle", "magrelease", "trigger"], gun: "base", timeline: [1, 250],
    clips: { fire: [1, 9], reload_empty: [9, 90], reload: [90, 152], idle: [200, 230] },   // rest frames 9, 90, 152, 200, 230 (inspect 152-200 and the draw 230-250 not used)
    mats: { FAL: { albedo: "FALcolor.png", metal: "FALmetallic.png", rough: "FALroughness.png" }, arms: { albedo: "armColor.png", metal: "armMetallic.png", rough: "armRoughness.png" } } },
  modern_pistol2: { src: "pistol-animated/source/arms@pistol1.fbx", tex: "pistol-animated", stat: "modern_pistol1",
    keep: ["armMesh", "base", "bolt", "bullet", "clip", "trigger"], rigidify: ["base", "bolt", "bullet", "clip", "trigger"], gun: "base", timeline: [1, 215], hipRear: [0.07, 0.3, -0.09],
    clips: { fire: [1, 11], reload_empty: [11, 57], reload: [58, 135], idle: [169, 197] },   // 135-150: the gun leaves the view; inspect 151-168 and the draw 197-215 not used
    mats: { pistol1: { albedo: "pistol1Color.png", metal: "pistol1Metallic.png", rough_f: 0.45 }, arms: { albedo: "armcolor.png", rough_f: 0.6 } } },
  // ---- MG4: six-shot revolver grenade launcher (Cinema 4D timeline, 30 fps)
  modern_glauncher: { src: "grenadelauncher-animated/source/arms@Glauncher.fbx", tex: "grenadelauncher-animated", stat: "modern_glauncher",
    keep: ["armmesh", "base", "casing", "front", "release", "scope", "scopebase", "slug1", "slug2", "slug3", "slug4", "slug5", "slug6", "trigger"], rigidify: ["base", "casing", "front", "release", "scope", "scopebase", "slug1", "slug2", "slug3", "slug4", "slug5", "slug6", "trigger"], gun: "base", timeline: [1, 303],
    // one round per 26 frames (rest frames 120, 146, 172): the insert loops the 4th round; 216-266 (out of view, inspect) and the draw 287-303 not used
    clips: { fire: [1, 15], reload_start: [15, 42], reload_insert: [120, 146], reload_end: [198, 215], idle: [267, 287] },
    mats: { glauncher: { albedo: "grenadelauncherColor.png", metal: "glMetallic.png", rough_f: 0.5 }, arms: { albedo: "armColor.png", metal: "armmetallic.png", smooth: "armsmoothness.png" } } },
  // ---- MG6: .50 BMG bolt-action bullpup (Cinema 4D timeline, 30 fps; rest frames 13, 84, 134, 176, 202)
  modern_sniper_50: { src: "../fps-50cal/source/arms@bmg.fbx", tex: "../fps-50cal", stat: "modern_bmg50",
    keep: ["armmesh", "base", "bolt", "bullet", "grip", "mag", "scope", "trigger"], rigidify: ["base", "bolt", "bullet", "grip", "mag", "scope", "trigger"], gun: "base", timeline: [1, 218],
    // fire = the shot (1-13) then the bolt stroke (134-176) back to back; reload_empty 13-84 (bolt pull 64-73), reload 84-134; the draw 202-218 not used
    clips: { fire: [{ action: "Armature|CINEMA_4D_Main|Layer0", range: [1, 13] }, { action: "Armature|CINEMA_4D_Main|Layer0", range: [134, 176] }], reload_empty: [13, 84], reload: [84, 134], idle: [176, 202] },
    mats: { bmg: { albedo: "50bmgColor.png", metal: "bmgmetallic.png", smooth: "bmgsmoothness.png" }, arms: { albedo: "armColor.png", metal: "armmetallic.png", smooth: "armsmoothness.png" } } },
  // ---- MG8: M4 with iron sights (named clips on one timeline; gun parts are bone-parented meshes)
  modern_m4_irons: { src: "../mg8/m4pack/source/M4.fbx", tex: "../mg8/m4pack", stat: "modern_m4irons", gun: "Main", gunBone: "Main_j",
    keep: ["Hand_Mesh", "Main", "Bolt", "BoltCatch", "Mag1", "Mag2", "Bullet5_56x45mm", "Bullet5_56x45mm.001", "Safety", "Trigger"],
    clips: { idle: "Armature|Idle|BaseLayer", fire: "Armature|Fire|BaseLayer", reload: "Armature|Reload|BaseLayer", reload_empty: "Armature|Reload_Empty|BaseLayer" },   // Draw, Holster not used
    mats: { M4_D: { albedo: "M4_D.png", rough_f: 0.5, metal_f: 0.6 }, Bullet5_56x45mm_D: { albedo: "Bullet5.56x45mm_D.png", rough_f: 0.4, metal_f: 0.9 },
            Hand_D: { albedo: "Hand_D.jpg", rough_f: 0.6 }, Glove_D: { albedo: "Glove_D.png", rough_f: 0.7 } } },
  // ---- MG7 guns
  modern_sniper_3: { src: "../mg7/sniper/source/arms@sniper.fbx", tex: "../mg7/sniper", stat: "modern_sniper3",
    keep: ["armMesh", "base", "bolt", "bullet", "clip", "magrelease", "trigger"], rigidify: ["base", "bolt", "bullet", "clip", "magrelease", "trigger"], gun: "base", timeline: [1, 159],
    // rest frames 14, 51, 93, 114, 141: fire = the shot and the bolt (1-51), reload 51-93, idle 114-141 (93-114 and the draw 141-159 not used)
    clips: { fire: [1, 51], reload: [51, 93], idle: [114, 141] },
    mats: { sniper: { albedo: "snipercolor.png", metal: "snipermetallic.png", smooth: "snipersmoothness.png" }, arms: { albedo: "armcolor.png", smooth: "armssmoothness.png" } } },
  modern_pistol3: { src: "../mg7/pistol43/source/Glock_Anim.fbx", tex: "../mg7/pistol43", stat: "modern_glock", lengthPart: "slide", staticPoseFrame: 1, staticPoseAction: "Hands|Hands|Shot",   // the static bake posed at the idle (this set animates bone scale)
    staticPoseFrame: 1,   // the static bake is posed (its unskinned rest is ~3x off the skin)
    keep: ["Glock", "BF3"], gun: "Glock", gunBone: "Wpn_body", hipRear: [0.07, 0.3, -0.09],
    clips: { idle: { action: "Hands|Hands|Shot", range: [1, 2] }, fire: "Hands|Hands|Shot",   // the 2-frame Origin action is not the holding pose
             reload: "Hands|Hands|Reloat_t", reload_empty: "Hands|Hands|Reloat_f" },
    mats: { Glock_Mat: { albedo: "LP_TEX_Glock_Mat_BaseColor.png", rough: "LP_TEX_Glock_Mat_Roughness.png", metal: "LP_TEX_Glock_Mat_Metallic.png" },
            "Tex_0011_1.dds": { albedo: "Tex_0011_1.png", rough_f: 0.7 }, "Tex_0012_1.dds": { albedo: "Tex_0012_1.png", rough_f: 0.7 } } },
  modern_ak12: { src: "../mg7/ak-12animated/ak-12animated.glb", tex: "../mg7/ak-12animated", stat: "modern_ak12", gun: "mtl_weapon_ak12_weapon_ak12_col.tga.001_0", gunBone: "Rifle_metarig",
    keep: ["mtl_weapon_ak12_weapon_ak12_col.tga.001_0", "aim_0", "barrel_0", "bullets_0", "Group_10_0", "mag.001_0", "mag_0", "rail_0", "rearsight_0", "stock_0", "TomGroup_4.001_0", "TomGroup_4_0", "Requests_Studio_Hands_0", "Requests_Studio_Hands_1", "watch_0"],
    clips: { idle: "Idle", fire: "Shoot", reload: "Reload" } },   // the lens mesh (lense_0) is left out: the optic is open; Equip not used
  // not exported: the GLB's hand skin is broken (stretched sleeves even in Blender's own import); kept for reference
  _modern_scar_unused: { src: "../mg7/scar-h_first-person_fps_animated/scar-h_first-person_fps_animated.glb", tex: "../mg7/scar-h_first-person_fps_animated", stat: "modern_scarh", staticPoseFrame: 60,
    keep: ["Object_94", "Object_92", "Object_96"], gun: "Object_94", gunBone: "Weapon_057",
    // one 0-280 timeline at 24 fps: equip 1-45 (not used), idle 46-76 (still), four shots 77-97, reload 100-180, reload from empty 180-280
    clips: { idle: [46, 76], fire: [77, 81], reload: [100, 180], reload_empty: [180, 280] } },
  // ---- MG12: DJMaesen's Cinema 4D arm rigs (incoming/mg12/<name>, CC BY 4.0; 30 fps timelines, cuts at the rest frames found
  // from the arm and part motion; the descriptions on Sketchfab list the pistols' ranges)
  modern_revolver: { abs: true, src: M12 + "revolver-animated/source/arms@revolver.fbx", tex: M12 + "revolver-animated", stat: "modern_revolver", hipRear: [0.07, 0.3, -0.09],
    keep: ["armmesh", "base", "bullet1", "bullet2", "bullet3", "bullet4", "bullet5", "bullet6", "cylinder", "ejector", "hammer", "release", "trigger"],
    rigidify: ["base", "bullet1", "bullet2", "bullet3", "bullet4", "bullet5", "bullet6", "cylinder", "ejector", "hammer", "release", "trigger"], gun: "base", timeline: [1, 321],
    // rest frames 15, 51, 190, 224, 270, 297: the cylinder swings out and the empties go (15-52), one round per 23 frames (52-75 loops the
    // first), it closes (190-224); 224-270 a cylinder spin (inspect), 297-321 hide / draw not used
    clips: { fire: [1, 15], reload_start: [15, 52], reload_insert: [52, 75], reload_end: [190, 224], inspect: [224, 270], idle: [270, 297] },
    mats: { revolver: { albedo: "revolverColor.png", metal: "revolvermetallic.png", smooth: "revolversmoothness.png" }, arms: c4dArms() } },
  modern_sawnoff: { abs: true, src: M12 + "sawnoff-animated/source/arms@sawnoff.fbx", tex: M12 + "sawnoff-animated", stat: "modern_sawnoff",
    keep: ["armmesh", "base", "ejector", "front", "release", "slug1", "slug2", "trigger1", "trigger2"], rigidify: ["base", "ejector", "front", "release", "slug1", "slug2", "trigger1", "trigger2"], gun: "base", timeline: [1, 271],
    clips: { fire: [1, 15], reload: [15, 107], reload_empty: [107, 195], idle: [221, 252] },   // rest frames 15, 107, 195, 221, 252 (195-221 and the draw 252-271 not used)
    mats: { sawnoff: { albedo: "sawnoffColor.png", metal: "sawnoffMetallic.png", smooth: "sawnoffSmoothness.png" }, arms: c4dArms() } },
  modern_smg9: { abs: true, src: M12 + "fps-smg9-animated/source/arms@smg9.fbx", tex: M12 + "fps-smg9-animated", stat: "modern_smg9",   // the red dot's lens object is not kept (open window)
    keep: ["armmesh", "base", "bolt", "bullet", "grip", "mag", "release", "scope", "trigger"], rigidify: ["base", "bolt", "bullet", "grip", "mag", "release", "scope", "trigger"], gun: "base", timeline: [1, 222],
    clips: { fire: [1, 9], reload_empty: [9, 79], reload: [79, 133], inspect: [133, 179], idle: [179, 206] },   // rest frames 79, 133, 179, 206; the hide / draw 206-222 not used
    mats: { smg9: { albedo: "smg9color.png", metal: "smg9Metallic.png", smooth: "smg9Smoothness.png" }, arms: c4dArms() } },
  modern_deagle: { abs: true, src: M12 + "animated-pistol/source/arms@talon.fbx", tex: M12 + "animated-pistol", stat: "modern_deagle", hipRear: [0.07, 0.3, -0.09],
    keep: ["arms", "base", "bullet", "bullet2", "hammer", "mag", "slide", "trigger"], rigidify: ["base", "bullet", "bullet2", "hammer", "mag", "slide", "trigger"], gun: "base", timeline: [1, 271],
    // Sketchfab keyframes (0-based): fire 0-14, reload mag 15-96, reload full 70-157, hide 158-170, ready 170-218, ambient 219-248, melee 249-270; rest frames 16, 71, 159, 220, 250
    clips: { fire: [1, 16], reload: [16, 71], reload_empty: [71, 159], idle: [220, 250] },
    mats: { talon: { albedo: "talonColor.png", metal: "talonMetallic.png", rough: "talonRoughness.png" }, arms: { albedo: "armColor.png", metal: "armMetallic.png", rough: "armRoughness.png" } } },
  // ---- MG12 batch 2
  modern_glock34: { abs: true, src: M12 + "fps-pistol-animated/source/arms@bpPistol.fbx", tex: M12 + "fps-pistol-animated", stat: "modern_glock34", hipRear: [0.07, 0.3, -0.09],
    keep: ["armmesh", "base", "bullet", "bullet_1", "clip", "lid", "muzzlebreak", "slide", "slidecatch", "trigger"], rigidify: ["base", "bullet", "bullet_1", "clip", "lid", "muzzlebreak", "slide", "slidecatch", "trigger"], gun: "base", timeline: [1, 235],
    // Sketchfab keyframes (0-based): fireA 0-9 (the last round: the slide stays back), reloadA 9-64 (from the slide lock: empty), reloadB 65-130, ambient 179-204, fireB 225-234 (the slide cycles)
    clips: { fire: [226, 235], reload_empty: [10, 66], reload: [66, 132], idle: [180, 206] },
    mats: { Mat: { albedo: "blackpantherColor.png", metal: "bpPistoMetallic.png", smooth: "bpPistolsmoothness.png" }, "Mat.001": c4dArms() } },
  _modern_makarov_mg12: {   // NG1: parked (the Makarov PM FBX pack replaced this)
    abs: true, src: M12 + "makarov_pm_fps_animations/makarov_pm_fps_animations.glb", tex: M12 + "makarov_pm_fps_animations", stat: "modern_makarov", hipRear: [0.07, 0.3, -0.09], staticPoseFrame: 0, staticPoseAction: "Rig|MK_Idle",   // the static bake is posed (its rest is taken apart)
    keep: ["Object_9", "Object_96"], gun: "Object_9", gunBone: "Body_015",   // its attachments mesh (suppressor, optic bracket, parked off the gun) is left out
    clips: { idle: "Rig|MK_Idle", fire: "Rig|MK_Shot", reload: "Rig|MK_ReloadFast", reload_empty: "Rig|MK_ReloadFull" } },
  modern_mark23: { abs: true, src: M12 + "mark-23-animated-free/source/Mark23.fbx", tex: M12 + "mark-23-animated-free", stat: "modern_mark23", hipRear: [0.07, 0.3, -0.09], gun: "main", gunBone: "main_j",   // LOCAL-ONLY bake (Sketchfab Standard licence)
    keep: ["Hand_Mesh", "main", "Side", "mag"],
    clips: { idle: "Armature|Idle|BaseLayer", fire: "Armature|Shoot|BaseLayer", reload: "Armature|Reload|BaseLayer" },
    mats: { Mark23_D: { albedo: "Mark23_D.png", rough_f: 0.5, metal_f: 0.5 }, Hand_D: { albedo: "Hand_D.png", rough_f: 0.6 }, Glove_D: { albedo: "Glove_D.png", rough_f: 0.7 } } },
  // ---- NG1 (user-supplied packs; the weapon JSONs' local overrides carry the sights): local-only bakes
  modern_ak: { abs: true, maxTex: 1024,   // LIC2: committed: rig textures at 1024
    src: NG1 + "ak74m/source/FpsAnims.fbx", tex: NG1 + "ak74m", stat: "modern_ak74m", keep: ["Ak", "Mag_2", "arms"], gun: "Ak", gunBone: "PBody",
    clips: { idle: "Rig|Rig|AK_Idle", fire: "Rig|Rig|AK_Shot", reload: "Rig|Rig|AK_Reload", reload_empty: "Rig|Rig|AK_Reload_full" },   // AK_Draw / Run / Walk not used
    mats: { ak74m: { albedo: "ak74m_AlbedoTransparency.png", metal: "ak74m_Metallic.png", rough: "ak74m_Roughness.png" }, arms: { ...ng2Arms, albedo: "armColor.png", rough: "armRoughness.png", normal: "armNormal.png" } }, normals: true },
  modern_makarov: { abs: true, maxTex: 1024,   // LIC2: committed: rig textures at 1024
    src: NG1 + "makarov_pm/source/Arms_Mak.fbx", tex: NG1 + "makarov_pm", stat: "modern_makarovpm", hipRear: [0.07, 0.3, -0.09], keep: ["Makarov_base", "mesh"], gun: "Makarov_base", gunBone: "Body",
    dropMats: { Makarov_base: ["makarov_attachments_material"] },   // its parked suppressor / optic bracket
    clips: { idle: "Rig|Rig|MK_Idle", fire: "Rig|Rig|MK_Shot", reload: "Rig|Rig|MK_ReloadFast", reload_empty: "Rig|Rig|MK_ReloadFull" },
    mats: { makarov_material: { albedo: "Makarov_main_Base_Color.png", metal: "Makarov_main_Metallic_png-Makarov_main_Rou.png", rough_f: 0.5 }, Mat: { ...ng2Arms, albedo: "armCcolor.png", rough: "armcRoughness.png", normal: "armCNormal.png" } }, normals: true },
  modern_fn502: { abs: true, maxTex: 1024,   // LIC2: committed: rig textures at 1024
    src: NG1 + "fn502/source/FPS_Anims.fbx", tex: NG1 + "fn502", stat: "modern_fn502", hipRear: [0.07, 0.3, -0.09], keep: ["502_1_low.001", "arms"], gun: "502_1_low.001", gunBone: "Main",
    // NG2: no idle clip: the default pose's frame 2 (its frame 1 is a different pose: the left hand up by the face holding the spare
    // magazine, which the engine held as the idle); the upper arms out (the eye sits behind the shoulders at the engine's eye relief)
    clips: { idle: { action: "Armature|Armature|FN_DefPose", range: [2, 3] }, fire: "Armature|Armature|FN_Shot", reload: "Armature|Armature|FN_Reload", reload_empty: "Armature|Armature|FN_ReloadFull" },
    dropBones: { arms: ["Arm_L", "Arm_R", "UpArm_L", "UpArm_R"] },
    mats: { gun: { albedo: "material_diffuse.png", smooth: "material_specularGlossiness.png", spec: "material_specularGlossiness.png" }, arms: { ...ng2Arms, albedo: "armColor.png", rough: "armRoughness.png", normal: "armNormal.png" } }, normals: true },
  modern_smg45: { abs: true, maxTex: 1024,   // LIC2: committed: rig textures at 1024
    src: NG1 + "smg/source/arms@smg45.fbx", tex: NG1 + "smg", stat: "modern_smg45", keep: ["armmesh", "base", "bolt", "bullet", "carrier", "clip", "trigger"],
    rigidify: ["base", "bolt", "bullet", "carrier", "clip", "trigger"], gun: "base", timeline: [1, 235],
    clips: { fire: [1, 9], reload_empty: [9, 81], reload: [81, 130], inspect: [130, 177], idle: [177, 216] },   // NG1: rest frames 9, 81, 130, 177, 216 (the magazine / bolt / gun-turn tracks); the draw 216-235 not used
    mats: { smg45: { albedo: "smg45Color.png", metal: "smg45Metallic.png", rough_f: 0.5 }, arms: { ...c4dArms(), ...ng2Arms } }, normals: true },
  modern_pistol4: { abs: true, src: NG1 + "animated_pistol_1/source/arms@beretta.fbx", tex: NG1 + "animated_pistol_1", stat: "modern_beretta", hipRear: [0.07, 0.3, -0.09],
    keep: ["armsmesh", "base", "bullet", "bullet_2", "bullet_2_2", "hammer", "mag", "shell", "shell_1", "shell_1_2", "slide", "stopper", "trigger"],
    rigidify: ["base", "bullet", "bullet_2", "bullet_2_2", "hammer", "mag", "shell", "shell_1", "shell_1_2", "slide", "stopper", "trigger"], gun: "base", timeline: [1, 265],
    clips: { fire: [1, 12], reload: [12, 84], reload_empty: [96, 177], inspect: [177, 234], idle: [234, 265] },   // NG1: 84-96 a last shot (slide locked back) not used
    mats: { beretta: { albedo: "berettaColor.png", metal: "berettaMetallic.png", rough: "berettaRoughness.png" }, arms: { ...ng2Arms, albedo: "armsColor.png", rough: "armsRoughness.png", normal: "armsNormal.png" } }, normals: true },
  // ---- NG3 (user-supplied packs): local-only bakes
  modern_scar: { abs: true, maxTex: 1024,   // LIC2: committed: rig textures at 1024
    src: NG3 + "Scar_animation/source/FP_ScarH.fbx", tex: NG3 + "Scar_animation", stat: "modern_scarh3", keep: ["BODY_MAG", "BUTTSTOCK_BARREL", "FPS_Hands"], gun: "BODY_MAG", gunBone: "Weapon",
    staticPoseFrame: 61, staticPoseAction: "Dragunov_FP_Rig|Dragunov_FP_Rig|Dragunov_FP_RigAction",   // the static bake was posed at the idle
    // one 1-281 timeline at 24 fps (as MG7's GLB of the same rig, one frame later): equip 1-46 (not used), idle 47-77, four shots from 78, reload 101-181, from empty 181-281
    clips: { idle: { action: "Dragunov_FP_Rig|Dragunov_FP_Rig|Dragunov_FP_RigAction", range: [47, 77] }, fire: { action: "Dragunov_FP_Rig|Dragunov_FP_Rig|Dragunov_FP_RigAction", range: [78, 83] },
             reload: { action: "Dragunov_FP_Rig|Dragunov_FP_Rig|Dragunov_FP_RigAction", range: [101, 181] }, reload_empty: { action: "Dragunov_FP_Rig|Dragunov_FP_Rig|Dragunov_FP_RigAction", range: [181, 281] } },
    ironsSplit: { BODY_MAG: 0.05, BUTTSTOCK_BARREL: 0.05 }, normals: true, dropBones: { FPS_Hands: ["upper_arm.L", "upper_arm.R", "shoulder.L", "shoulder.R"] },
    mats: { SCAR_Body_Mat: { albedo: "ScarH_Body_D.png", metal: "ScarH_Body_M.png", rough: "ScarH_Body_R.png" }, SCAR_Buttock_Mat: { albedo: "ScarH_Buttock_D.png", metal: "ScarH_Buttock_M.png", rough: "ScarH_Buttock_R.png" },
            Hands_Mat: { ...ng2Arms, albedo: "FPS_Hands_D.png", normal: "FPS_Hands_N.png", rough_f: 0.75 } } },
  modern_m4_irons: { abs: true, maxTex: 768,   // LIC2: committed: rig textures at 768 (four 4096 px sets)
    src: NG3 + "animated-fps-m4a4/source/rig.fbx", tex: NG3 + "animated-fps-m4a4", stat: "modern_m4a4", hipRear: [0.12, 0.15, -0.07],
    keep: ["M4A4_CS2", "M4A4_CS2.001", "DMC5_Soldier_Gloves", "DMC5_Soldier_Skin", "DMC5_Soldier_Shirt", "DMC5_Soldier_Gear"], gun: "M4A4_CS2",
    clips: { idle: "Armature|Armature|Idle", fire: "Armature|Armature|Shoot", reload: "Armature|Armature|Reload" },   // Take / Hide not used
    ironsSplit: { M4A4_CS2: 0.04 }, normals: true,   // the upper arms kept: cut, the left sleeve's open end showed in ADS (the XD's cut does not show)
    mats: { weapon_rif_m4a4: { albedo: "m4a4_default_color_psd_3b0b391e.png", normal: "m4a4_default_normal_png_d39c4b9a.png", rough: "m4a4_default_rough.png", metal_f: 0.6 },
            DMC5_Soldier_Hands_Mat: { albedo: "soldierhand_d.png", normal: "soldierhand_n.png", rough_f: 0.8, matte: [0.8, 0.8] },
            DMC5_Soldier_Shirt_Mat: { albedo: "pl2000_main3_d.png", normal: "pl2000_main3_n.png", rough_f: 0.92, matte: [0.92, 0.92] },
            DMC5_Soldier_Gear_Mat: { albedo: "pl2000_main2_d.png", normal: "pl2000_main2_n.png", rough_f: 0.85, matte: [0.85, 0.85] } } },
  modern_vsk: { abs: true, src: NG3 + "fps-animations-vsk/source/FPS_VSK.fbx", tex: NG3 + "fps-animations-vsk", stat: "modern_vsk3", keep: ["VSK-94", "Male_04_Animation_Ready"], gun: "VSK-94", gunBone: "Body", maxTex: 1024,
    clips: { idle: "Rig|Rig|VSK_Idle", fire: "Rig|Rig|VSK_Fire", reload: "Rig|Rig|VSK_ReloadNotFull", reload_empty: "Rig|Rig|VSK_ReloadFull" },   // run / sprint / walk not used
    ironsSplit: { "VSK-94": 0.04 }, normals: true,
    mats: { "Main body": { albedo: "Main_body_Base_color.png", metal: "Main_body_Metallic.png", rough: "Main_body_Roughness.png" },
            "Mag+cover": { albedo: "Mag+cover_Base_color.png", metal: "Mag+cover_Metallic.png", rough: "Mag+cover_Roughness.png" },
            "Stock-silencer": { albedo: "Stock-silencer_Base_color.png", metal: "Stock-silencer_Metallic.png", rough: "Stock-silencer_Roughness.png" },
            Male_04_Animation_Ready: { ...ng2Arms, albedo: "Body_Colour.jpg", normal: "Body_Normal.jpg", rough: "Body_Roughness.jpg" } } },
  modern_revolver: { abs: true, src: NG3 + "revovlerReplace/source/AnimatedMagnum.fbx", tex: NG3 + "revovlerReplace", stat: "modern_ragingbull", hipRear: [0.07, 0.3, -0.09],
    keep: ["Magnum.002", "Object002", "Object003", "Charger", "hardknuckle", "sleeve"], gun: "Magnum.002", dropBones: { sleeve: ["upperarm_L.L", "upperarm_L.R"], hardknuckle: ["upperarm_L.L", "upperarm_L.R"] },   // the upper arms (across the aimed view)
    clips: { idle: "Armature|Armature|Idle", fire: "Armature|Armature|Shoot", reload: "Armature|Armature|Reload" },   // Take / Hide / Watch not used
    normals: true,
    mats: { "Default_OBJ.001": { albedo: "weapon_raging_D.png", rough_f: 0.4, metal_f: 0.7 },
            glove_hardknuckle: { ...ng2Arms, albedo: "glove_hardknuckle_Base_color.png", normal: "glove_hardknuckle_Normal_OpenGL.png", rough_f: 0.8 },
            sleeve_st6_generalist: { ...ng2Arms, albedo: "sleeve_st6_generalist_Base_color.png", normal: "sleeve_st6_generalist_Normal_OpenGL.png", rough_f: 0.9 },
            v_l_skin: { ...ng2Arms, factor: [0.42, 0.27, 0.19], rough_f: 0.75 } } },
  // ---- MG7: the first-person knife (knife_animated.glb, Sketchfab; one 0-116 timeline at 24 fps); framed by its camera: the eye
  // at the scene origin looking along -y in the exported frame (found by a test export), centimetres
  knife_bayonet: { src: "../mg7/knife_animated/knife_animated.glb", tex: "../mg7/knife_animated", frame: { rear: [0, 0, 0], front: [0, -100, 0], up: [0, 0, 1], scale: 0.01 }, hipRear: [0, 0, 0],
    keep: ["Object_7", "knife_knife_0"], boneAttach: [{ mesh: "knife_knife_0", bone: "R_wrist_027", frame: 10 }], gun: "knife_knife_0", gunBone: "R_wrist_027",
    clips: { idle: [0, 1], fire: [32, 48] } },   // fire = the quick slash (strike ~frame 35); the big swing 52-64, the overhead stab 68-80 and the wide slash 84-100 not used
};
for (const [id, a] of Object.entries(A)) {
  if (id.startsWith("_")) continue;   // parked entries (MG7: the SCAR-H rig, not exported)
  if (process.env.MG_ONLY && !process.env.MG_ONLY.split(",").includes(id)) continue;   // MG12: only these ids (the others need their own static summaries)
  if (a.frame) {   // MG7 (the knife): framed by its own camera, no static model or sights
    fs.writeFileSync(path.join(CFG, "anim_" + id + ".json"), JSON.stringify({ maxTex: 2048, ...a, src: a.abs ? a.src : G + a.src, texdir: (path.isAbsolute(a.tex) ? a.tex : G + a.tex) + "/textures", out: O + id + "_vm.glb", points: {} }, null, 1));
    continue;
  }
  const w = JSON.parse(fs.readFileSync(WDIR + id + ".json", "utf8"));
  if (fs.existsSync(WDIR + "local/" + id + ".json")) Object.assign(w, JSON.parse(fs.readFileSync(WDIR + "local/" + id + ".json", "utf8")));   // MG12: a local-only bake's real model / sights (content/weapons/local/<id>.json)
  const sum = JSON.parse(fs.readFileSync(O + a.stat + "_summary.json", "utf8"));
  const parts = a.lengthPart ? sum.parts.filter(p => p.name === a.lengthPart) : sum.parts;
  const mn = [0, 1, 2].map(i => Math.min(...parts.map(p => p.gltf_min[i]))), mx = [0, 1, 2].map(i => Math.max(...parts.map(p => p.gltf_max[i])));
  const s = w.model.realLength / (mx[2] - mn[2]);   // the static import's scale (realLength over the length parts' extent)
  const points = { rear: w.sights.rear, front: w.sights.front, muzzle: w.muzzle };
  if (w.scope) points.eye = w.scope.eye;
  const cfg = { maxTex: 2048, ...a, src: a.abs ? a.src : G + a.src,   // MG4: rig textures up to 2048 (never upscaled)
    texdir: (path.isAbsolute(a.tex) ? a.tex : G + a.tex) + "/textures", out: O + id + "_vm.glb", points,
    static: { s, o: [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mn[2]] } };
  fs.writeFileSync(path.join(CFG, "anim_" + id + ".json"), JSON.stringify(cfg, null, 1));
}
console.log(Object.keys(A).join(" "));
