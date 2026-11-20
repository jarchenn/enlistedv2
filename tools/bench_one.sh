#!/usr/bin/env bash
# One --bench-views process, run by tools/bench_views.sh inside its exclusive GPU slot (cwd: the build dir). Before and
# after the game, the CPU seconds of other games / Blender go to <out>/fg0.txt and fg1.txt: they don't take GPU slots,
# so the script compares the two to flag a run that shared the machine.
O="${1:?out dir}"
snap() { powershell.exe -NoProfile -NonInteractive -Command 'Get-Process NormandyMapmaker,blender -EA SilentlyContinue | ForEach-Object { $_.ProcessName + ":" + $_.Id + " " + [int]$_.CPU }' 2>/dev/null | tr -d '\r'; }
clk() { nvidia-smi --query-gpu=clocks.gr,clocks.mem,temperature.gpu --format=csv,noheader,nounits 2>/dev/null | tr -d '\r' | head -1; }   # read-only
snap > "$O/fg0.txt"; rm -f "$O/clk0.txt" "$O/clk1.txt"
( sleep 60; clk > "$O/clk0.txt"; sleep 120; clk > "$O/clk1.txt" ) & smp=$!   # clocks while the game runs (1 and 3 min in)
./NormandyMapmaker.exe --bench-views; rc=$?
kill $smp 2>/dev/null; snap > "$O/fg1.txt"
exit $rc
