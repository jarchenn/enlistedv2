#!/usr/bin/env bash
# SD5: attacker plant rate on Sun Fort clutter variants. Each variant: tools/mapgen/build.py sd_desert with a clutter seed
# (NMK_SD_SEED) and the large clusters (NMK_SD_BIG=1), synced into the build (ninja's content copy), then
# --selftest-sd-plant (NMK_SD_ROUNDS rounds). The repo's sd_desert files are restored afterwards.
#   tools/sd5_variants.sh <build dir> <out dir> <variant>...   variant: base | <seed>
set -u
W=$(cd "$(dirname "$0")/.." && pwd); B=$1; O=$2; shift 2; PY=${PY:-$TEMP/nmk/py/python.exe}; mkdir -p "$O"
for v in "$@"; do
  if [ "$v" = base ]; then (cd "$W" && git checkout -- content/templates/sd_desert.json content/templates/sd_desert.nmap.json content/templates/sd_desert.height.png 2>/dev/null)
  else (cd "$W/tools/mapgen" && NMK_SD_SEED=$v NMK_SD_BIG=1 "$PY" -I build.py sd_desert > "$O/gen_$v.log" 2>&1); fi
  cp "$W/content/templates/sd_desert.nmap.json" "$B/content/templates/sd_desert.nmap.json"
  rm -f "$B/content/templates/sd_desert.nav" "$B/content/templates/sd_desert.nmap.nav" 2>/dev/null
  (cd "$B" && NMK_SD_ROUNDS=${NMK_SD_ROUNDS:-20} GPU_TURN_TAG="sd5 $v" bash "$W/tools/gpu_turn.sh" ./NormandyMapmaker.exe --selftest-sd-plant > "$O/run_$v.log" 2>&1)
  cp "$B/selftest_out/report_sdplant.txt" "$O/report_$v.txt"
  echo "$v: $(grep -o 'plant rate: [^;]*' "$O/report_$v.txt")"
done
(cd "$W" && git checkout -- content/templates/sd_desert.json content/templates/sd_desert.nmap.json content/templates/sd_desert.height.png tools/mapgen/maps/summary_sd_desert.txt 2>/dev/null; true)
