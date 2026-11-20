#!/usr/bin/env bash
# The performance benchmark (src/bench_views.cpp, CP step 0): one process per map (a map that crashes, or that shared the
# machine with other games / Blender, is run once more), the graphics card held exclusively for each; the table goes to
# docs/perf/bench_<tag>.md (+ .jsonl, one line per run; per-frame CSVs in <build dir>/selftest_out/bench_frames).
# Usage: bash tools/bench_views.sh <build dir> [tag]
#   NMK_BENCH_KIND=path (default: the real-time camera path, plus one bots run on NMK_BENCH_BOTS_MAP, default dday, "none"
#     to skip; the bots run is added only when NMK_BENCH_MAPS is not set) | views (the P1 static views) | bots;
#   NMK_BENCH_MAPS="a b" (default: the five VO maps); NMK_BENCH_PRESETS="Low,High"; NMK_BENCH_RES=1920x1080 (default
#   2560x1440); NMK_BENCH_LAPS (3); NMK_BENCH_LIGHTING=classic (default: New), NMK_BENCH_MSAA=0|4 (over the preset),
#   NMK_BENCH_RQOFF=a,b (Renderer::perf switches off); views only: NMK_BENCH_SETTLE / NMK_BENCH_MEASURE (s),
#   NMK_BENCH_SHOTS=1 (a still and a pan frame per view); NMK_BENCH_AB="none;hex;kograss;ssr;msaa4;scale0.5" runs each
#   knock-out config in turn (Renderer::perf names, ssr, msaa4, scale<s>; "+" joins several).
# GPU clocks are logged per map (nvidia-smi, read-only), not locked: locking them (nvidia-smi -lgc) changes a system
# setting and needs an elevated prompt. Compare two runs with tools/bench_compare.py.
set -u
B="${1:?build dir}"; TAG="${2:-$(date +%Y%m%d-%H%M)}"
HERE="$(cd "$(dirname "$0")" && pwd)"; DOCS="$HERE/../docs/perf"; O="$B/selftest_out"
MAPS="${NMK_BENCH_MAPS:-chateau_town newtown outpost_woods dday sd_town}"; KIND="${NMK_BENCH_KIND:-path}"
ENTRIES=""; for m in ${MAPS//,/ }; do ENTRIES="$ENTRIES $KIND:$m"; done
BOTS="${NMK_BENCH_BOTS_MAP:-dday}"; [ "$KIND" = path ] && [ "$BOTS" != none ] && [ -z "${NMK_BENCH_MAPS:-}" ] && ENTRIES="$ENTRIES bots:$BOTS"
mkdir -p "$O" "$DOCS"
rm -f "$O/bench_views_rows.md" "$O/bench_views.jsonl" "$O/bench_views_derived.jsonl" "$O/bench_clocks.txt"
failed=""; shared=""
for e in $ENTRIES; do
  k="${e%%:*}"; m="${e#*:}"
  pat="^| $m | $k"; [ "$k" = views ] && pat="^| $m |"   # this entry's rows (views: one row per view)
  for try in 1 2; do
    n0=$(cat "$O/bench_views.jsonl" 2>/dev/null | wc -l)
    ( cd "$B" && NMK_MUTE=1 NMK_BENCH_TAG="$TAG" NMK_BENCH_MAPS="$m" NMK_BENCH_KIND="$k" GPU_TURN_EXCLUSIVE=1 GPU_TURN_TAG="bench-views $TAG $k $m" "$HERE/gpu_turn.sh" bash "$HERE/bench_one.sh" "$O" ) >/dev/null 2>&1; rc=$?
    drop() { head -n "$n0" "$O/bench_views.jsonl" > "$O/j.tmp" 2>/dev/null; mv -f "$O/j.tmp" "$O/bench_views.jsonl"
      grep -v "$pat" "$O/bench_views_rows.md" > "$O/r.tmp" 2>/dev/null; mv -f "$O/r.tmp" "$O/bench_views_rows.md"; }
    # other games / Blender working during the run (they ignore the slot lock): drop it and run once more, then keep it flagged
    fg=$(awk 'NR==FNR { c[$1] = $2; next } { d = $2 - ($1 in c ? c[$1] : 0); if (d > 5) printf "%s +%ds, ", $1, d }' "$O/fg0.txt" "$O/fg1.txt" 2>/dev/null | sed 's/, $//')
    if [ $rc = 0 ] && [ -n "$fg" ]; then
      echo "$k $m: run $try shared the machine ($fg CPU)"
      if [ "$try" = 1 ]; then drop; continue; fi
      shared="$shared $k:$m ($fg)"; break
    fi
    if [ $rc = 0 ]; then echo "$k $m: at 1 min $(cat "$O/clk0.txt" 2>/dev/null), at 3 min $(cat "$O/clk1.txt" 2>/dev/null)" >> "$O/bench_clocks.txt"; break; fi
    cp -f "$B/normandy.log" "$O/bench_fail_${m}_$try.log" 2>/dev/null; echo "$k $m: run $try failed (exit $rc; log selftest_out/bench_fail_${m}_$try.log: $(grep -h "last stage" "$B/normandy.log" | tail -1))"
    if [ "$try" = 1 ]; then drop; else failed="$failed $k:$m"; fi   # a crash after some runs finished: keep them only if the retry fails too
  done
done
[ -s "$O/bench_views_rows.md" ] || { echo "no results written"; exit 1; }
FB=$(grep -o '"framebuffer":\[[0-9]*,[0-9]*\]' "$O/bench_views.jsonl" | sort -u | grep -o '[0-9]*,[0-9]*' | tr , x | tr '\n' ' ' | sed 's/ $//')
LIGHT=$(grep -o '"lighting":"[a-z]*"' "$O/bench_views.jsonl" | sort -u | cut -d'"' -f4 | tr '\n' ' ' | sed 's/ $//')
WANT="${NMK_BENCH_RES:-2560x1440}"; [ "$FB" = "$WANT" ] || echo "WARNING: framebuffer sizes in this run: $FB (not all $WANT; not comparable)" >&2
{ echo "# Benchmark: $TAG"; echo
  echo "\`bash tools/bench_views.sh <build dir> $TAG\` on $(date '+%Y-%m-%d %H:%M'), commit $(git -C "$HERE/.." rev-parse --short HEAD), framebuffer $FB, Lighting $LIGHT, 2 pm clear sky, UI hidden, dynamic resolution / vsync / FPS cap off, the card held exclusively. Path rows: a real-time camera path (warm-up lap, then ${NMK_BENCH_LAPS:-3} timed laps of a 40 s loop and an 8 s 360 deg turn); bots: a ${NMK_BENCH_BOTS:-100}-bot match, the player turning; views: static views. Times in ms over the timed frames: GPU = the per-pass timestamps summed; CPU = wall-clock frame time without the time blocked in swap; frame = wall clock with it; hitches = frames over 1.5x the moving median / over 33.3 ms / over 50 ms. Fragments = fragment-shader invocations per internal pixel in that pass (the warm-up lap).${failed:+ Not measured (crashed twice):$failed.}${shared:+ **Not exclusive** (other games / Blender busy in both tries, CPU seconds):$shared.}"; echo
  echo "| Map | Run | Preset | Internal | GPU P50 | GPU P95 | GPU P99 | GPU P99.9 | GPU max | CPU P50 | CPU P99 | Frame P99 | Swap P50 | Hitches | Probes / frame | Draws | Tris (M) | Top passes (mean ms, fragments / px) |"
  echo "|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---|"
  cat "$O/bench_views_rows.md"; echo
  [ -s "$O/bench_clocks.txt" ] && { echo "GPU clocks 1 and 3 minutes into each process (graphics MHz, memory MHz, deg C; logged, not locked):"; echo; sed 's/^/- /' "$O/bench_clocks.txt"; }
} > "$DOCS/bench_$TAG.md"
cp "$O/bench_views.jsonl" "$DOCS/bench_$TAG.jsonl"
[ -s "$O/bench_views_derived.jsonl" ] && echo "views / paths were derived for some maps (not in content/settings/bench_views.json): $O/bench_views_derived.jsonl"
echo "written docs/perf/bench_$TAG.md${failed:+ (failed:$failed)}"
