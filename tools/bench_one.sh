#!/usr/bin/env bash
# One --bench-views process, run by tools/bench_views.sh inside its exclusive GPU slot (cwd: the build dir). Before and
# after the game, the CPU seconds of other games / Blender go to <out>/fg0.txt and fg1.txt: they don't take GPU slots,
# so the script compares the two to flag a run that shared the machine.
O="${1:?out dir}"
snap() { powershell.exe -NoProfile -NonInteractive -Command 'Get-Process NormandyMapmaker,blender -EA SilentlyContinue | ForEach-Object { $_.ProcessName + ":" + $_.Id + " " + [int]$_.CPU }' 2>/dev/null | tr -d '\r'; }
snap > "$O/fg0.txt"
./NormandyMapmaker.exe --bench-views; rc=$?
snap > "$O/fg1.txt"
exit $rc
