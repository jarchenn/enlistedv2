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
run swim     --selftest-swim    # swimming: depth hysteresis, bob, no fire, arms, underwater fog, breath, mantle out, bots, nav (docs/movement.md#swimming)
run sights   --selftest-sights
run visuals  --selftest-visuals   # W2: thumbnails (blank tiles, cache), footprint labels, bunker ammo / nav / OP + shelter pass
run update   --selftest-update
run entities --selftest-entities
run tanks    --selftest-tanks
run interiors --selftest-interiors   # walls, interiors and furniture (docs/interiors.md)
run editor   --selftest-editor  # editor & terrain: map size, chunks, cuts, camera, drop-in (W3)
run match    --selftest-match   # multiplayer + AI: headless rules suite
run ai       --selftest-ai      # multiplayer + AI: village match integration (~4 min)
run rules    --selftest-rules   # W1: spawn protection, movement / fall damage, damage table
run gore     --selftest-gore    # W7 gore: decals, droplets, budgets, Off mode, protected hits, profile
run structures --selftest-structures   # W4 structure assets: nav, colliders, cuts, walk-throughs
run aicourse --selftest-ai-course   # W6 AI: live navmesh rebuild + obstacle course (AI_SPEC 12.2 / 12.4, ~5 min)
run nature   --selftest-nature  # natural ground dressing: boulders, patches, shrubs (docs/nature.md)
run terrain  --selftest-terrain # V8 terrain sculpt, Generate hills, Beautify ring (docs/terrain-tools.md)
run settings --selftest-settings   # V2: graphics presets, settings file and window, firing smoke (docs/settings.md)
run autopath --selftest-autopath  # V9 auto paths: Connect houses on the village maps, curved path pieces (docs/entities.md)
run snap     --selftest-snap    # V6: snapping trenches / hedges / paths: joints, caps, cut floors, texture origin
run lasso    --selftest-lasso   # lasso tool: groups, elevation, water fill, ground paint, move land (docs/lasso.md)
run maps     --selftest-maps    # W5 default maps: template load, scenario, SPEC 2.1 distances, asset-usage doc, views + frame time
run lights   --selftest-lights   # L2: clusters, light shadows, probe GI, time of day
run render   --selftest-render  # V1: facade shimmer 20 / 50 / 100 m (static + strafe), rough walls, water assets / pass (docs/rendering.md §9)
run textures --selftest-textures   # L4: ambientCG texture layer, slots, on / off look, missing-texture fallback (docs/textures.md)
if [ "${1:-}" = "--net" ]; then bash "$HERE/run_net_test.sh" "$B" "${NMK_NET_PORT:-27970}" || status=1; fi   # LAN: 5 processes on 127.0.0.1 (NMK_NET_PORT: parallel sessions use distinct ports)
if [ "${1:-}" = "--bench" ]; then
  ./NormandyMapmaker.exe --benchmark >/dev/null 2>&1; echo "benchmark          exit $?  -> selftest_out/benchmark.json"
  ./NormandyMapmaker.exe --benchmark-stress >/dev/null 2>&1; echo "benchmark-stress   exit $?  -> selftest_out/benchmark_stress.json"
fi
exit $status
