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

## PK2: Paladin 2 (AI type 52; the heavy armour)

`fbx_to_glb.py` (the FBX with its textures relinked by file name; the MetaHuman face under the closed helm dropped), then the steps
above. `rig_platemail.py` also moves stray arm weights of body garments to the pelvis and removes a small loose piece left far from
the body after the bake (the heavy armour's belt end). `hilt.py` prints the sword's guard and pommel (bind space) for `grip_check.py`
and `ik_left.py`. The model is local-only (git-ignored) until its licence is confirmed.
