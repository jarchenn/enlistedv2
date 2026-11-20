#!/usr/bin/env bash
# P1: the VO benchmark (src/bench_views.cpp): one process per map (a map that crashes is run once more), the graphics
# card held exclusively for each, the table assembled into docs/perf/bench_<tag>.md (+ .jsonl, one line per run).
# Usage: bash tools/bench_views.sh <build dir> [tag]
#   NMK_BENCH_MAPS="a b" (default: the five VO maps), NMK_BENCH_PRESETS="Low,High", NMK_BENCH_SETTLE / NMK_BENCH_MEASURE (s);
#   renderer switches (NMK_PERF_AB, NMK_*) pass through the environment.
set -u
B="${1:?build dir}"; TAG="${2:-$(date +%Y%m%d-%H%M)}"
HERE="$(cd "$(dirname "$0")" && pwd)"; DOCS="$HERE/../docs/perf"; O="$B/selftest_out"
MAPS="${NMK_BENCH_MAPS:-chateau_town newtown outpost_woods dday sd_town}"
mkdir -p "$O" "$DOCS"
rm -f "$O/bench_views_rows.md" "$O/bench_views.jsonl" "$O/bench_views_derived.jsonl"
failed=""
for m in ${MAPS//,/ }; do
  for try in 1 2; do
    n0=$(cat "$O/bench_views.jsonl" 2>/dev/null | wc -l)
    ( cd "$B" && NMK_MUTE=1 NMK_BENCH_TAG="$TAG" NMK_BENCH_MAPS="$m" GPU_TURN_EXCLUSIVE=1 GPU_TURN_TAG="bench-views $TAG $m" "$HERE/gpu_turn.sh" ./NormandyMapmaker.exe --bench-views ) >/dev/null 2>&1 && break
    rc=$?; cp -f "$B/normandy.log" "$O/bench_fail_${m}_$try.log" 2>/dev/null; echo "$m: run $try failed (exit $rc; log selftest_out/bench_fail_${m}_$try.log: $(grep -h "last stage" "$B/normandy.log" | tail -1))"
    # a crash after some runs finished: keep them only if the retry fails too
    if [ "$try" = 1 ]; then head -n "$n0" "$O/bench_views.jsonl" > "$O/j.tmp" 2>/dev/null; mv -f "$O/j.tmp" "$O/bench_views.jsonl"
      grep -v "^| $m |" "$O/bench_views_rows.md" > "$O/r.tmp" 2>/dev/null; mv -f "$O/r.tmp" "$O/bench_views_rows.md"; else failed="$failed $m"; fi
  done
done
[ -s "$O/bench_views_rows.md" ] || { echo "no results written"; exit 1; }
FB=$(grep -o '"framebuffer":\[[0-9]*,[0-9]*\]' "$O/bench_views.jsonl" | head -1 | grep -o '[0-9]*,[0-9]*' | tr , x)
{ echo "# VO benchmark: $TAG"; echo
  echo "\`bash tools/bench_views.sh <build dir> $TAG\` on $(date '+%Y-%m-%d %H:%M'), commit $(git -C "$HERE/.." rev-parse --short HEAD), framebuffer $FB, 2 pm clear sky, UI hidden, the card held exclusively. Views: content/settings/bench_views.json. Times in ms: GPU = the per-pass timers summed, CPU = wall-clock frame time (no vsync), render CPU = render() alone, probes / frame = GI probe captures per frame (mean); median / p95 / p99 over the measured frames.${failed:+ Not measured (crashed twice):$failed.}"; echo
  echo "| Map | View | Preset | GPU med | GPU p95 | GPU p99 | CPU med | CPU p95 | CPU p99 | render CPU | Probes / frame | Draws | Tris (M) | Top passes (median ms) |"
  echo "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|"
  cat "$O/bench_views_rows.md"; } > "$DOCS/bench_$TAG.md"
cp "$O/bench_views.jsonl" "$DOCS/bench_$TAG.jsonl"
[ -s "$O/bench_views_derived.jsonl" ] && echo "views were derived for some maps (not in content/settings/bench_views.json): $O/bench_views_derived.jsonl"
echo "written docs/perf/bench_$TAG.md${failed:+ (failed:$failed)}"
