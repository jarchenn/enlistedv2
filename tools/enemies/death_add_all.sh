#!/bin/bash
# KD1: nine more deaths on every knight model (the soldier body's seven directional deaths, the archer's two)
R=$(cd "$(dirname "$0")/../.." && pwd); C=${KD_CHARS:-$R/content/characters}; T=$R/tools/enemies/death_add.py; W=${KD_WORK:-/tmp/kdeaths}; mkdir -p "$W"
A=("death from the front=death front" "death from the back=death back" "death from front headshot=death front headshot" "death from back headshot=death back headshot" "death from right=death right" "Falling Forward Death=death falling forward" "Flying Back Death=death flying back")
for m in ${MODELS:-knight_ss knight_4 knight_5 knight_6 knight_7 knight_8 knight_9 knight_10 knight_10_elk_foot knight_spear_ks1 knight_spear_ks2 knight_spear_ks3 paladin paladin_2 samurai crusader chinese_soldier fallen_knight_2}; do
  python -I $T $C/allied_body.glb $C/$m.glb $W/a_$m.glb "${A[@]}" > $W/log_$m.txt 2>&1 &&
  python -I $T $C/archer_1.glb $W/a_$m.glb $W/b_$m.glb "standing death backward 01=death backward" "standing death forward 01=death forward" >> $W/log_$m.txt 2>&1 &&
  cp $W/b_$m.glb $C/$m.glb && echo "$m ok $(grep -h held $W/log_$m.txt | head -1)" || { echo "$m FAILED"; tail -3 $W/log_$m.txt; }
done
# rerun from the original models (git checkout -- content/characters/), else the clips are replaced in place (same names)
