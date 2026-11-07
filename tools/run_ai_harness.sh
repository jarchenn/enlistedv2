#!/usr/bin/env bash
# W6b AI tuning harness: runs the AI_SPEC 12.2-12.5 set against a built NormandyMapmaker.exe and prints one line per run.
#   obstacle course (12.2) on the course map(s), live rebuild (12.4) on the course and every soak map, soak (12.3 / 12.5)
#   on every soak map. Thresholds: content/ai/test_thresholds.json. Exit 1 if any run misses a threshold.
# Usage: tools/run_ai_harness.sh <build dir> [--maps "village random omaha"] [--course-maps "course"] [--minutes 20]
#                                [--bots 32] [--suites "course rebuild soak"] [--difficulty regular] [-- extra exe args]
#   Maps: village, course, random[:seed[:size]], a map name in content/examples or maps/, or a .nmap.json path.
#   Smoke test: tools/run_ai_harness.sh <build> --maps village --minutes 2
# Results: <build>/selftest_out/aiharness_<suite>_<map>.json / .txt (every stuck event with map, position, duration and
# the ladder step that resolved it), report_aiharness_<suite>_<map>.txt, and aiharness_summary.txt.
set -u
B="${1:?build dir}"; shift || true
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/.." && pwd)"
MAPS="village random"; CMAPS="course"; MIN=20; BOTS=32; SUITES="course rebuild soak"; DIFF=""; EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    --maps) MAPS="$2"; shift 2;; --course-maps) CMAPS="$2"; shift 2;; --minutes) MIN="$2"; shift 2;;
    --bots) BOTS="$2"; shift 2;; --suites) SUITES="$2"; shift 2;; --difficulty) DIFF="--difficulty $2"; shift 2;;
    --) shift; EXTRA=("$@"); break;;
    *) echo "unknown option $1"; exit 2;;
  esac
done
cd "$B" || exit 2
mkdir -p selftest_out
SUM=selftest_out/aiharness_summary.txt
echo "AI harness $(date '+%Y-%m-%d %H:%M')  maps: $MAPS  course: $CMAPS  minutes: $MIN  bots: $BOTS" > "$SUM"
status=0
run() {   # run <suite> <map> <args...>
  local suite="$1" map="$2"; shift 2
  local tag; tag="$(echo "$map" | tr -c 'A-Za-z0-9_-' '_' | sed 's/_*$//')"
  local t0=$SECONDS
  NMK_SRC="$REPO" ./NormandyMapmaker.exe --ai-harness "$suite" --map "$map" --tag "$tag" --baseline "$REPO/docs/baseline/ai_baseline.json" $DIFF "$@" "${EXTRA[@]}" >/dev/null 2>&1
  local rc=$?
  local rep="selftest_out/report_aiharness_${suite}_${tag}.txt"
  local line; line="$(printf '%-8s %-24s exit %d  %4ds  %s' "$suite" "$map" $rc $((SECONDS - t0)) "$(tail -1 "$rep" 2>/dev/null)")"
  echo "$line"; echo "$line" >> "$SUM"
  grep -E '^FAIL' "$rep" 2>/dev/null | head -12 | sed 's/^/           /' | tee -a "$SUM"
  [ $rc -eq 0 ] || status=1
}
for s in $SUITES; do
  case "$s" in
    course)  for m in $CMAPS; do run course "$m"; done;;
    rebuild) for m in $CMAPS $MAPS; do run rebuild "$m"; done;;
    soak)    for m in $MAPS; do run soak "$m" --minutes "$MIN" --bots "$BOTS"; done;;
    *) echo "unknown suite $s"; status=2;;
  esac
done
echo "summary: $B/$SUM"
exit $status
