#!/bin/bash
# EK: the elk knight on foot (Knight 10 + the Spear Knight's clips, the halberd, the steel shield, the wings, no cloak)
set -e; cd /e/nmk/stag; G=/e/nmk/wt-glit/content/characters; T=/e/nmk/wt-pf3/tools/enemies
python -I clip_copy.py $G/knight_spear_ks1.glb $G/knight_10.glb foot_a.glb "spear thrust" "spear thrust (2)" "spear thrust (3)" "spear thrust (4)" "spear poke" >/dev/null
LINE_GLB=$G/knight_spear_ks1.glb LINE_MAT=Material.001 LENGTH=2.2 SHIFT=0.4 ROLL=270 python -I weapon_swap.py foot_a.glb /e/nmk/incoming_halberd/renaissance_halberd.glb foot_b.glb KNB_RightHand_sword_0 KNB_LeftForeArm_shield_0 KNB_LeftForeArm_shield_0 | grep -E "slid|wrote"
python -I shield_recolor.py foot_b.glb foot_b2.glb KNB_LeftForeArm_shield_0 elk_armoured.glb ha_steel >/dev/null
python -I - <<'PY'
import json,struct
b=open('E:/nmk/stag/foot_b2.glb','rb').read(); jl=struct.unpack('<I',b[12:16])[0]; bl=struct.unpack('<I',b[20+jl:24+jl])[0]; J=json.loads(b[20:20+jl]); BIN=b[28+jl:28+jl+bl]
for n in J['nodes']:
    if n.get('name')=='K10_cloak' and 'mesh' in n: del n['mesh']; n.pop('skin',None)
js=json.dumps(J,separators=(',',':')).encode()
while len(js)%4: js+=b' '
open('E:/nmk/stag/foot_c.glb','wb').write(struct.pack('<III',0x46546C67,2,28+len(js)+len(BIN))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(BIN),0x004E4942)+BIN)
PY
python -I wings_attach.py $T/retarget_clips.py foot_c.glb /e/nmk/incoming_wings/angel-wings.glb foot_d.glb mixamorig:Spine2 0.5 25 0.1 0.24 -0.16 >/dev/null
python -I $T/gltf_prune.py foot_d.glb knight10_elk_foot.glb | tail -1
python -I props_skin.py knight10_elk_foot.glb foot_pv.glb >/dev/null
