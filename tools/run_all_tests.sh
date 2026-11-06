#!/usr/bin/env bash
# Runs every self-test suite against a built NormandyMapmaker.exe and prints each report's tally.
# Usage: tools/run_all_tests.sh <build dir>      (benchmarks: add --bench; close other GPU apps first)
set -u
B="${1:?build dir}"; shift || true
cd "$B" || exit 2
status=0
run() { local name="$1"; shift; ./NormandyMapmaker.exe "$@" >/dev/null 2>&1; local rc=$?; printf '%-28s exit %d  ' "$name" $rc; tail -1 "selftest_out/report_$name.txt" 2>/dev/null || echo; [ $rc -eq 0 ] || status=1; }
run A        --selftest
run B        --selftest-reopen "maps/selftest_regression.nmap.json"
run damage   --selftest-damage
run pak      --selftest-pak
run weapons  --selftest-weapons
run grass    --selftest-grass
run fps      --selftest-fps
run env      --selftest-env
run arsenal  --selftest-arsenal
run move     --selftest-move
run sights   --selftest-sights
if [ "${1:-}" = "--bench" ]; then
  ./NormandyMapmaker.exe --benchmark >/dev/null 2>&1; echo "benchmark          exit $?  -> selftest_out/benchmark.json"
  ./NormandyMapmaker.exe --benchmark-stress >/dev/null 2>&1; echo "benchmark-stress   exit $?  -> selftest_out/benchmark_stress.json"
fi
exit $status
