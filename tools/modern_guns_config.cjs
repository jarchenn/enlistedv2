// usage: node tools/modern_guns_config.cjs <incoming/modern_guns dir> <assets_src/weapons dir> <config out dir>; then per config:
//   blender -b --factory-startup --python tools/modern_guns_convert.py -- <config>.json
const path = require("path"), G = path.resolve(process.argv[2]) + "/", O = path.resolve(process.argv[3]) + "/", CFG = path.resolve(process.argv[4] || ".");
const k = (...n) => Object.fromEntries(n.map(x => [x, x]));
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
  case: { src: G + "hardcase_for_weapons.glb", tex: "", scale: 1.27,
        keep: k("CaseLower_low_Hardcase_0", "CaseUpped_low_Hardcase_0", "Foam_low_Hardcase_0", "Handle_low_Hardcase_0", "HandleRods_low_Hardcase_0", "HingesLower_low_Hardcase_0", "HingesUpper_low_Hardcase_0", "Knob_low_Hardcase_0", "LockRods_low_Hardcase_0", "Locks_low_Hardcase_0") },
};
const fs = require("fs");
for (const [n, c] of Object.entries(C)) {
  c.texdir = c.tex ? G + c.tex + "/textures" : ""; c.out = O + "modern_" + n + ".glb";
  fs.writeFileSync(CFG + "/cfg_" + n + ".json", JSON.stringify(c, null, 1));
}
console.log(Object.keys(C).join(" "));
