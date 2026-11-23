# PK1: the Paladin (AI type 51)

From DevonLux's "5 Piece Platemail - MetaHuman (rigged)" and HASSAN's "guts sword - (band of the hawk)" (both CC-BY-4.0):

1. `lustre.py`: the steel's metal / roughness at Knight 10's means (its silver base colour kept; the leather left).
2. `rig_platemail.py` (Blender): the four armour sets' MetaHuman rigs as one Mixamo-named rig in a T-pose bind (joints mapped,
   the rest merged into their nearest mapped ancestor; a mixamorig:Weapon joint on the right hand).
3. `retarget_mx.py` (retarget_clips.py that also reads raw `mixamorig:` names): Knight 4's 51 Great Sword Pack clips onto it.
4. `sword_attach.py <...> 2.0` with `SHIFT_XYZ` from `grip_check.py`: the sword along Knight 4's blade line, its hilt in both fists.
5. `ik_left.py`: the left hand on the hilt by a two-bone IK where it holds the sword (not in the deaths or casts).
6. `gltf_prune.py`, `tools/enemies/elk/tex_shrink.py` (2048 px) -> `content/characters/paladin.glb`.

`tpose.py`: the T-posed, unrigged FBX for a Mixamo upload (not used by the game).
