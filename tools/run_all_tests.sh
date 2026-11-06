#!/usr/bin/env bash
# Runs every self-test suite against a built NormandyMapmaker.exe and prints each report's tally.
# Usage: tools/run_all_tests.sh <build dir>      (benchmarks: add --bench; close other GPU apps first)
set -u
B="${1:?build dir}"; shift || true
HERE="$(cd "$(dirname "$0")" && pwd)"   # resolve before cd: $0 may be relative
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
run update   --selftest-update
run entities --selftest-entities
run tanks    --selftest-tanks
run interiors --selftest-interiors   # walls, interiors and furniture (docs/interiors.md)
run match    --selftest-match   # multiplayer + AI: headless rules suite
run ai       --selftest-ai      # multiplayer + AI: village match integration (~4 min)
run structures --selftest-structures   # W4 structure assets: nav, colliders, cuts, walk-throughs
if [ "${1:-}" = "--net" ]; then bash "$HERE/run_net_test.sh" "$B" "${NMK_NET_PORT:-27970}" || status=1; fi   # LAN: 5 processes on 127.0.0.1 (NMK_NET_PORT: parallel sessions use distinct ports)
if [ "${1:-}" = "--bench" ]; then
  ./NormandyMapmaker.exe --benchmark >/dev/null 2>&1; echo "benchmark          exit $?  -> selftest_out/benchmark.json"
  ./NormandyMapmaker.exe --benchmark-stress >/dev/null 2>&1; echo "benchmark-stress   exit $?  -> selftest_out/benchmark_stress.json"
fi
exit $status
