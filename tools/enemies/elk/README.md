# EK: the Elk Knight (AI type 47)

Pipeline (paths as on the user's machine, E:/nmk/stag; the elk download unpacked to E:/nmk/incoming_elk/unz):

1. `elk_build.py` (Blender, background): the elk FBX's takes as the mounted knight's clips (walk held steady fore-aft, cycles in place),
   the barding from `moose_armour_pieces.py` fitted to the elk in idle frame 1, skinned to the elk's own weights, pushed out of the
   skin through every clip; antlers in the armour steel. Writes `elk_armoured.glb` and `elk_seat.json`. (`CLIPCHK=1`: a clipping report.)
2. `weapon_swap.py` on `knight10_rider.glb`: the halberd for the spear (`LENGTH=2.2`), the shield flipped (`FLIP_SHIELD=1`);
   `shield_recolor.py`: its own design in the armour steel.
3. `gltf_clip_alias.py elk_armoured.glb look_around=idle`, then `elk_rider.py`: the rider re-seated on the elk (look_around built here).
4. `horse_merge.py`, `wings_attach.py ... mixamorig:Spine2 0.5 25 0.1 0.24 -0.135`, `gltf_prune.py`, `tex_shrink.py` -> `knight_10_elk.glb`.
5. `build_foot.sh` (`clip_copy.py`, `weapon_swap.py` with the Spear Knight's spear line, `SHIFT=0.4 ROLL=270`, `shield_recolor.py`,
   the cloak hidden, `wings_attach.py`, `gltf_prune.py`), `tex_shrink.py` -> `knight_10_elk_foot.glb`.
6. `measure_elk.py`: the numbers in zombie.json (head height, strike 'at' and ranges, foot_at, ground speeds).

Previews: `props_skin.py` (Blender misplaces bone-parented props), `pv_glb.py` (stills), `anim_mp4.py` (videos).

K10W (user: "add wings to knight 10"): `wings_attach.py ... knight_10.glb angel-wings.glb out.glb mixamorig:Spine2 0.5 25 0.1 0.24 -0.30`,
the wings' front 4 cm outside the cloak (its rest back -0.262), clear of the cloak in every clip Knight 10 plays (a 2 cm grid of the
wings' front against the skinned cloak, per frame). The rest height is unchanged (1.8406), so height_m stays 2.1956.
