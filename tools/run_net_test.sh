#!/usr/bin/env bash
# LAN self-test on one PC: a host and four client processes on 127.0.0.1 (ports 27970+), see src/selftest_net.cpp.
#   host  lobby, start, wall breach + smoke before the late joiner, validation counters, clients' world state, ends
#   a     joins the lobby with 60 ms simulated delay + 5 % loss, plays, misbehaves (over-rate fire, teleport), crews an
#         MG 42 and fires it, resupplies at a crate, takes a rack weapon, places a sandbag, and is refused someone
#         else's gun, a gun / placement out of reach, and a placement / gun while dead
#   b     joins mid-match once A is done (late join: wall, smoke age, match state, A's gun, crate, rack weapon, sandbag)
#   c     different map  -> refused with the map message
#   d     lobby full     -> refused with the full message
# Usage: tools/run_net_test.sh <build dir> [port]
# Only processes this script started are stopped. Localhost cannot test discovery across machines or firewalls.
set -u
B="${1:?build dir}"; PORT="${2:-27970}"
cd "$B" || exit 2
rm -f selftest_out/report_net_*.txt selftest_out/net_*.flag   # B waits for A's flag (src/selftest_net.cpp)
./NormandyMapmaker.exe --selftest-net host "$PORT" > net_host.log 2>&1 & H=$!
sleep 4
./NormandyMapmaker.exe --selftest-net a "$PORT" > net_a.log 2>&1 & A=$!
./NormandyMapmaker.exe --selftest-net c "$PORT" > net_c.log 2>&1 & C=$!
sleep 22
./NormandyMapmaker.exe --selftest-net b "$PORT" > net_b.log 2>&1 & BB=$!
sleep 8
./NormandyMapmaker.exe --selftest-net d "$PORT" > net_d.log 2>&1 & D=$!
status=0
for p in $H $A $BB $C $D; do
  for i in $(seq 1 720); do kill -0 $p 2>/dev/null || break; sleep 1; done
  kill -0 $p 2>/dev/null && { echo "process $p still running after 12 min: stopping it"; kill $p; status=1; }
  wait $p || status=1
done
for r in host a b c d; do printf 'net %-5s ' "$r"; tail -1 "selftest_out/report_net_$r.txt" 2>/dev/null || { echo "no report"; status=1; }; done
exit $status
