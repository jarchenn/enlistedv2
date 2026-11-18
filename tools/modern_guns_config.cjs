// usage: node tools/modern_guns_config.cjs <incoming/modern_guns dir> <assets_src/weapons dir> <config out dir> [<extract dir>]; then per config:
// (MG2: four sources ship as .rar; extract each into <extract dir>/<name>/ first: rpg7, mp7, scorpion, uzi; nothing in them is run)
//   blender -b --factory-startup --python tools/modern_guns_convert.py -- <config>.json
const path = require("path"), G = path.resolve(process.argv[2]) + "/", O = path.resolve(process.argv[3]) + "/", CFG = path.resolve(process.argv[4] || ".");
const k = (...n) => Object.fromEntries(n.map(x => [x, x]));
const X = path.resolve(process.argv[5] || G + "_extracted") + "/";
const M12 = path.resolve(process.env.MG12_SRC || "E:/nmk/incoming/mg12") + "/";   // MG12 sources (moved off C: to E:, user disk policy)
const NG1 = path.resolve(process.env.NG1_SRC || "E:/nmk/incoming/ng1") + "/";   // NG1 sources (licences unknown: the bakes are local-only)
const C = {
  ak: { src: G + "fps-ak-animated/source/arms@ak.fbx", tex: "fps-ak-animated", keep: k("base", "bolt", "bullet", "mag", "release", "trigger"),
        mats: { ak74: { albedo: "ak74Color.png", normal: "ak74Normal.png", metal: "akMetallic.png", smooth: "aksmoothness.png", ao: "akAO.png" } } },
  carbine: { src: G + "fps-animated-carbine/source/arms@carbine.fbx", tex: "fps-animated-carbine", keep: k("base", "bolt", "boltcatch", "bullet", "clip", "handle", "handle2", "lid", "release", "scope", "trigger"),
        mats: { carbine: { albedo: "carbineColor.png", normal: "carbineNormal.png", metal: "carbineMetallic.png", smooth: "carbinespecular.png", ao: "carbineAo.png" } } },
  dmr: { src: G + "sniper-animated/source/arms@sniper.fbx", tex: "sniper-animated", keep: k("base", "bolt", "boltrear", "bullet", "cheekrest", "clip", "scope", "silencer", "trigger"),
        mats: { sniper: { albedo: "sniper1Color.png", normal: "sniper1Normal.png", metal: "sniper1Metallic.png", smooth: "sniper1Smoothness.png", ao: "sniperAO.png" } } },
  lmg: { src: G + "lmg-animated/source/arms@LMG.fbx", tex: "lmg-animated",
        keep: k("base", "bolt", "bulletfirst", ...Array.from({ length: 12 }, (_, i) => "bulletholder" + i), "ejectorplate", "handle", "lid", "loader", "loaderplate", "mag", "trigger"),
        mats: { LMG: { albedo: "LMGcolor.png", normal: "LMGnormal.png", metal: "LMGmetallic.png", rough: "LMGroughness.png", ao: "LMGao.png" } } },
  shotgun: { src: G + "remington-870-shotgun-animated/source/Remington 870 - Animated.fbx", tex: "remington-870-shotgun-animated", keep: { remington870_base: "base", remington870_lid: "lid", remington870_pump: "pump", remington870_trigger: "trigger" },
        mats: { "Material.001": { albedo: "Material.001_Base_color.png", normal: "Material.001_Normal_OpenGL.png", metal: "Material.001_Metallic.png", rough: "Material.001_Roughness.png", ao: "Material.001_Mixed_AO.png" } } },
  scar: { src: G + "scar/source/SCAR.fbx", tex: "scar", keep: { "Circle.001_low": "base", "Circle.001_low.001": "mag", "Circle.001_low.002": "trigger" },
        mats: { Material: { albedo: "SCARL_LPForNormalFix_Material_AlbedoTransp.png", normal: "SCARL_LPForNormalFix_Material_NormalOpenGL.png", metal: "SCARL_LPForNormalFix_Material_MetallicSmoo.png", rough: "SCARL_LPForNormalFix_Material_Roughness.png", ao: "SCARL_LPForNormalFix_Material_AO.png" } } },
  m16: { src: G + "animated-assault-rifle/source/HandWithGloves.fbx", tex: "animated-assault-rifle", keep: { BARREL: "base", "BARREL.001": "mag", "BARREL.002": "trigger" },
        mats: { M16A3: { albedo: "m16_M16A3_BaseColor.png", normal: "m16_M16A3_Normal.png", metal: "m16_M16A3_Metallic.png", rough: "m16_M16A3_Roughness.png" } } },
  boltsniper: { src: G + "fps-animations-sniper-rifle/source/BoltActionRifle.fbx", tex: "fps-animations-sniper-rifle", dropLow: 0.06, keep: { Rifle: "base" }, split: { Rifle: { Bolt: "bolt", Mag: "mag" } },
        mats: { Rifle: { albedo: "Rifle_albedo.jpg", normal: "Rifle_normal.png", metal: "Rifle_metallic.jpg", rough: "Rifle_roughness.jpg", ao: "Rifle_AO.jpg" },
                Other: { albedo: "Other_albedo.jpg", normal: "Other_normal.png", metal: "Other_metallic.jpg", rough: "Other_roughness.jpg" } } },
  pistol: { src: G + "animatedpistol/source/rig.blend", tex: "animatedpistol", keep: { xd_grip_safe: "frame", xd_mag: "mag", xd_slide: "slide" },
        mats: { "None.001": { albedo: "for_texturing_None.001_Diffuse.png", normal: "for_texturing_None.001_Normal.png", gloss: "for_texturing_None.001_Glossiness.png", spec: "for_texturing_None.001_Specular.png" } } },
  // ---- MG2
  ar15: { src: G + "ar-15-animations-miniset/source/Base_Rig_Test_Scetc.fbx", tex: "ar-15-animations-miniset", keep: { AR_15: "base" },
        split: { AR_15: { mag: "mag", mag_2: "mag2", bolt: "bolt", charging_handle: "handle" } },
        mats: { "texture set 1": { albedo: "texture_set_1_Base_color.png", normal: "texture_set_1_Normal_DirectX.png", normalDX: true, rough: "texture_set_1_Roughness.png" },
                "texture set 2": { albedo: "texture_set_2_Base_color.png", normal: "texture_set_2_Normal_DirectX.png", normalDX: true, rough: "texture_set_2_Roughness.png", metal: "texture_set_2_Metallic.png" } } },
  mp7: { src: X + "mp7/MP7A1_Animations/model/MP7A1.fbx", tex: "mp7a1-animations", keep: { mp7a1_Object: "base" },
        split: { mp7a1_Object: { mag: "mag", bullet_1: "mag", bullet_2: "mag", bolt_handle: "handle" } },
        mats: Object.fromEntries([["Slot_1", "Slot_1"], ["slot_2", "slot_2"], ["slot_3", "slot_3"]].map(([m, f]) => [m, { albedo: `mp7_low_${f}_BaseColor.png`, normal: `mp7_low_${f}_Normal.png`, rough: `mp7_low_${f}_Roughness.png`, metal: `mp7_low_${f}_Metallic.png` }])) },
  rpg7: { src: X + "rpg7/RPG7_Animations/model/RPG_7.fbx", tex: "rpg-7-animations", keep: { rpg_low: "base" }, split: { rpg_low: { missile: "rocket" } },
        mats: { "rpg-7": { albedo: "RPG-7_low_rpg-7_BaseColor.png", normal: "RPG-7_low_rpg-7_Normal.png", rough: "RPG-7_low_rpg-7_Roughness.png", metal: "RPG-7_low_rpg-7_Metallic.png" },
                rockets: { albedo: "RPG-7_low_rockets_BaseColor.png", normal: "RPG-7_low_rockets_Normal.png", rough: "RPG-7_low_rockets_Roughness.png", metal: "RPG-7_low_rockets_Metallic.png" } } },
  scorpion: { src: X + "scorpion/CZ_Scorpion_Evo3_A1_Anim.blend", tex: "", keep: { CZScorpion: "base" },
        split: { CZScorpion: { Magazine: "mag", Bullets: "mag", Magazine_2: "mag2", Changing_handle: "handle" } },
        mats: { CZScorpion: { factor: [0.055, 0.055, 0.05], metal: 0.1, rough: 0.55 } } },
  uzi: { src: X + "uzi/1Matzh_Uzi_Animations.blend", tex: "uzi-first-person-animations-2026-remake", keep: { UZI: "base" },
        split: { UZI: { "UZI Mag": "mag", "UZI Bullets": "mag", "UZI Bullet C": "mag2", "UZI BP": "bolt" } },
        mats: { UZI: { albedo: "UZI_Base_color.png", normal: "UZI_Normal_OpenGL.png", rough: "UZI_Roughness.png", metal: "UZI_Metallic.png" },
                "UZI Magazine and Bullet": { albedo: "UZI_Magazine_and_Bullet_Base_color.png", normal: "UZI_Magazine_and_Bullet_Normal_OpenGL.png", rough: "UZI_Magazine_and_Bullet_Roughness.png", metal: "UZI_Magazine_and_Bullet_Metallic.png", ao: "UZI_Magazine_and_Bullet_Mixed_AO.png" } } },
  shotgun2: { src: G + "shotgun-animated/source/shotgunAnimated.fbx", tex: "shotgun-animated", keep: { boomstick: "base" }, split: { boomstick: { pump: "pump", slug: "mag2", inserter: "mag2" } },
        mats: { boomstick: { albedo: "boomstickColor.png", normal: "boomstickNormal.png", rough: "boomstickRoughness.png", metal: "boomstickmetallic.png" } } },
  // ---- MG3
  fal: { src: G + "battle-rifle-animated/source/arms@FAL.fbx", tex: "battle-rifle-animated", keep: { base: "base", bolt: "bolt", bullet: "mag", bullet_2: "mag", clip: "mag", handle: "handle", magrelease: "trigger", trigger: "trigger" },
        mats: { FAL: { albedo: "FALcolor.png", normal: "FALnormal.png", metal: "FALmetallic.png", rough: "FALroughness.png", ao: "FALao.png" } } },
  pistol1: { src: G + "pistol-animated/source/arms@pistol1.fbx", tex: "pistol-animated", keep: { base: "frame", bolt: "slide", bullet: "mag", clip: "mag", trigger: "trigger" },
        mats: { pistol1: { albedo: "pistol1Color.png", normal: "pistol1Normal.png", metal: "pistol1Metallic.png", rough_f: 0.45 } } },
  // ---- MG4
  glauncher: { src: G + "grenadelauncher-animated/source/arms@Glauncher.fbx", tex: "grenadelauncher-animated", keep: k("base", "casing", "front", "release", "scope", "scopebase", "slug1", "slug2", "slug3", "slug4", "slug5", "slug6", "trigger"),
        mats: { glauncher: { albedo: "grenadelauncherColor.png", normal: "grenadelauncherNormal.png", metal: "glMetallic.png", rough_f: 0.5, ao: "glAO.png" } } },
  // ---- MG6: .50 BMG bolt-action bullpup (Barrett M95 type), zip extracted to incoming/fps-50cal
  bmg50: { src: G + "../fps-50cal/source/arms@bmg.fbx", tex: "../fps-50cal", keep: { base: "base", bolt: "bolt", bullet: "mag", grip: "base", mag: "mag", scope: "scope", trigger: "trigger" },
        mats: { bmg: { albedo: "50bmgColor.png", normal: "50bmgNormal.png", metal: "bmgmetallic.png", smooth: "bmgsmoothness.png", ao: "bmgAo.png" } } },
  // ---- MG7 (sources extracted / copied to incoming/mg7/<name>/)
  sniper3: { src: G + "../mg7/sniper/source/arms@sniper.fbx", tex: "../mg7/sniper", keep: { base: "base", bolt: "bolt", bullet: "mag", clip: "mag", magrelease: "trigger", trigger: "trigger" },
        mats: { sniper: { albedo: "snipercolor.png", normal: "sniperNormal.png", metal: "snipermetallic.png", smooth: "snipersmoothness.png", ao: "sniperAO.png" } } },
  glock: { src: G + "../mg7/pistol43/source/Glock_Anim.fbx", tex: "../mg7/pistol43", poseFrame: 1, poseAction: "Hands|Hands|Shot", keep: { Glock: "frame" }, split: { Glock: { Slide: "slide", Magazine: "mag", Bullet: "mag", Magazine2: "mag2" } },
        mats: { Glock_Mat: { albedo: "LP_TEX_Glock_Mat_BaseColor.png", normal: "LP_TEX_Glock_Mat_Normal.png", rough: "LP_TEX_Glock_Mat_Roughness.png", metal: "LP_TEX_Glock_Mat_Metallic.png" } } },
  scarh: { src: G + "../mg7/scar-h_first-person_fps_animated/scar-h_first-person_fps_animated.glb", tex: "", poseFrame: 60, keep: { Object_94: "base", Object_92: "stock" }, split: { Object_94: { Magazine_058: "mag", Slide_060: "handle" } } },
  ak12: { src: G + "../mg7/ak-12animated/ak-12animated.glb", tex: "", keep: { "mtl_weapon_ak12_weapon_ak12_col.tga.001_0": "base", aim_0: "scope", barrel_0: "barrel", Group_10_0: "grp10", mag_0: "mag", rail_0: "rail", rearsight_0: "rearsight", stock_0: "stock", "TomGroup_4.001_0": "tg4b", TomGroup_4_0: "tg4" } },
  // ---- MG8: M4 with iron sights (m4-fps-weapon-animations-pack-v1.zip, extracted to incoming/mg8/m4pack)
  m4irons: { src: G + "../mg8/m4pack/source/M4.fbx", tex: "../mg8/m4pack", poseFrame: 226, poseAction: "Armature|Idle|BaseLayer", keep: { Main: "base", Bolt: "bolt", BoltCatch: "base", Mag1: "mag", Bullet5_56x45mm: "mag", Safety: "base", Trigger: "trigger" },
        mats: { M4_D: { albedo: "M4_D.png", normal: "M4_N.png", rough_f: 0.5, metal_f: 0.6 }, Bullet5_56x45mm_D: { albedo: "Bullet5.56x45mm_D.png", normal: "Bullet5.56x45mm_N.png", rough_f: 0.4, metal_f: 0.9 } } },
  // ---- MG12 (incoming/mg12/<name>/: DJMaesen's Cinema 4D arm rigs, CC BY 4.0; gun parts are object-animated)
  revolver: { src: M12 + "revolver-animated/source/arms@revolver.fbx", tex: M12 + "revolver-animated",
        keep: k("base", "bullet1", "bullet2", "bullet3", "bullet4", "bullet5", "bullet6", "cylinder", "ejector", "hammer", "release", "trigger"),
        mats: { revolver: { albedo: "revolverColor.png", normal: "revolverNormal.png", metal: "revolvermetallic.png", smooth: "revolversmoothness.png", ao: "revolverAo.png" } } },
  sawnoff: { src: M12 + "sawnoff-animated/source/arms@sawnoff.fbx", tex: M12 + "sawnoff-animated",
        keep: k("base", "ejector", "front", "release", "slug1", "slug2", "trigger1", "trigger2"),
        mats: { sawnoff: { albedo: "sawnoffColor.png", normal: "sawnoffNormal.png", metal: "sawnoffMetallic.png", smooth: "sawnoffSmoothness.png", ao: "sawnoffAo.png" } } },
  smg9: { src: M12 + "fps-smg9-animated/source/arms@smg9.fbx", tex: M12 + "fps-smg9-animated",   // the red dot's lens is dropped (the tube is open)
        keep: k("base", "bolt", "bullet", "grip", "mag", "release", "scope", "trigger"),
        mats: { smg9: { albedo: "smg9color.png", normal: "smg9Normal.png", metal: "smg9Metallic.png", smooth: "smg9Smoothness.png", ao: "smg9AO.png" } } },
  deagle: { src: M12 + "animated-pistol/source/arms@talon.fbx", tex: M12 + "animated-pistol",
        keep: k("base", "bullet", "bullet2", "hammer", "mag", "slide", "trigger"),
        mats: { talon: { albedo: "talonColor.png", normal: "talonNormal.png", metal: "talonMetallic.png", rough: "talonRoughness.png", ao: "talonAO.png" } } },
  // ---- MG12 batch 2
  glock34: { src: M12 + "fps-pistol-animated/source/arms@bpPistol.fbx", tex: M12 + "fps-pistol-animated",   // long-slide 9 mm with a compensator ("muzzlebreak")
        keep: { base: "frame", slide: "slide", slidecatch: "frame", muzzlebreak: "frame", clip: "mag", bullet: "mag", bullet_1: "mag", lid: "mag", trigger: "trigger" },
        mats: { Mat: { albedo: "blackpantherColor.png", normal: "blackpantherNormal.png", metal: "bpPistoMetallic.png", smooth: "bpPistolsmoothness.png", ao: "bpPistolAO.png" } } },
  makarov: { src: M12 + "makarov_pm_fps_animations/makarov_pm_fps_animations.glb", tex: "", poseFrame: 0, poseAction: "Rig|MK_Idle", keep: { Object_9: "frame" }, split: { Object_9: { Slide_016: "slide", Mag_019: "mag" } } },
  mark23: { src: M12 + "mark-23-animated-free/source/Mark23.fbx", tex: M12 + "mark-23-animated-free", keep: { main: "frame", Side: "slide", mag: "mag" },
        mats: { Mark23_D: { albedo: "Mark23_D.png", rough_f: 0.5, metal_f: 0.5 } } },
  // ---- MG12 batch 3 (Cransh GLBs: the arms' skin is broken, the guns are baked posed at their idle; baked posed at the idle)
  saiga: { src: M12 + "fps_arms_saiga_animations_remake/fps_arms_saiga_animations_remake.glb", tex: "", poseFrame: 0, poseAction: "Rig|Saiga_Idle",
        keep: { Object_95: "frame", Object_96: "device" }, split: { Object_95: { Rif_059: "bolt", Pmag_062: "mag" } } },
  vsk: { src: M12 + "fps_animations_vsk/fps_animations_vsk.glb", tex: "", poseFrame: 0, poseAction: "Rig|VSK_Idle", rotX: 180,   // its idle pose holds it reversed and upside down
        keep: { Object_69: "frame", Object_68: "stock", Object_67: "magcover" }, split: { Object_69: { Slider_052: "bolt", Mag_055: "mag" } } },
  // ---- NG1 (user-supplied packs; the guns are one skinned mesh split by their bones, or C4D object parts)
  ak74m: { src: NG1 + "ak74m/source/FpsAnims.fbx", tex: NG1 + "ak74m", keep: { Ak: "base" }, split: { Ak: { Rif: "bolt", Pmag: "mag" } },
        mats: { ak74m: { albedo: "ak74m_AlbedoTransparency.png", normal: "ak74m_Normal.png", metal: "ak74m_Metallic.png", rough: "ak74m_Roughness.png", ao: "ak74m_AO.png" } } },
  makarovpm: { src: NG1 + "makarov_pm/source/Arms_Mak.fbx", tex: NG1 + "makarov_pm", keep: { Makarov_base: "frame" }, split: { Makarov_base: { Slide: "slide", Mag: "mag" } },
        dropMats: { Makarov_base: ["makarov_attachments_material"] },   // its parked suppressor / optic bracket
        mats: { makarov_material: { albedo: "Makarov_main_Base_Color.png", normal: "Makarov_main_Normal_OpenGL.png", metal: "Makarov_main_Metallic_png-Makarov_main_Rou.png", rough_f: 0.5 } } },
  fn502: { src: NG1 + "fn502/source/FPS_Anims.fbx", tex: NG1 + "fn502", keep: { "502_1_low.001": "frame" }, split: { "502_1_low.001": { Slide: "slide", Mag: "mag" } },
        mats: { gun: { albedo: "material_diffuse.png", normal: "material_normal.png", smooth: "material_specularGlossiness.png", smoothCh: 3, spec: "material_specularGlossiness.png" } } },
  smg45: { src: NG1 + "smg/source/arms@smg45.fbx", tex: NG1 + "smg", keep: { base: "base", bolt: "bolt", carrier: "bolt", clip: "mag", bullet: "mag", trigger: "trigger" },
        mats: { smg45: { albedo: "smg45Color.png", normal: "smg45Normal.png", metal: "smg45Metallic.png", ao: "smg45Ao.png", rough_f: 0.5 } } },
  beretta: { src: NG1 + "animated_pistol_1/source/arms@beretta.fbx", tex: NG1 + "animated_pistol_1", keep: { base: "frame", slide: "slide", mag: "mag", hammer: "frame", trigger: "trigger", stopper: "frame" },
        mats: { beretta: { albedo: "berettaColor.png", normal: "berettaNormal.png", metal: "berettaMetallic.png", rough: "berettaRoughness.png", ao: "berettaAO.png" } } },
  case: { src: G + "hardcase_for_weapons.glb", tex: "", scale: 1.27,
        keep: k("CaseLower_low_Hardcase_0", "CaseUpped_low_Hardcase_0", "Foam_low_Hardcase_0", "Handle_low_Hardcase_0", "HandleRods_low_Hardcase_0", "HingesLower_low_Hardcase_0", "HingesUpper_low_Hardcase_0", "Knob_low_Hardcase_0", "LockRods_low_Hardcase_0", "Locks_low_Hardcase_0") },
};
const fs = require("fs");
for (const [n, c] of Object.entries(C)) {
  c.texdir = c.tex ? (path.isAbsolute(c.tex) ? c.tex : G + c.tex) + "/textures" : ""; c.out = O + "modern_" + n + ".glb";
  fs.writeFileSync(CFG + "/cfg_" + n + ".json", JSON.stringify(c, null, 1));
}
console.log(Object.keys(C).join(" "));
