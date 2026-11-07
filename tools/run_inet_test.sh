#!/usr/bin/env bash
# Internet-play self-test on one PC (src/selftest_net.cpp, roles ihost / e / f / z): the rendezvous server
# (tools/rendezvous/rendezvous.py) on 127.0.0.1, an internet-room host, a client joining by room code directly
# (UDP hole punch over loopback), a client joining by room code through the relay, and a hostile-traffic sender.
# Usage: tools/run_inet_test.sh <build dir> [game port] [server port]
#   RENDEZVOUS=<path to rendezvous.py>   (default: tools/rendezvous/rendezvous.py next to this script)
#   PYTHON=<python>                      (default: %TEMP%/nmk/py/python.exe, then python3)
# Only processes this script started are stopped. Loopback cannot test real NATs, firewalls or campus Wi-Fi.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
B="${1:?build dir}"; PORT="${2:-28980}"; SPORT="${3:-28990}"
RV="${RENDEZVOUS:-$HERE/rendezvous/rendezvous.py}"
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
  if [ -x "${TEMP:-/tmp}/nmk/py/python.exe" ]; then PY="${TEMP}/nmk/py/python.exe"; else PY=python3; fi
fi
[ -f "$RV" ] || { echo "rendezvous server not found: $RV"; exit 2; }
cd "$B" || exit 2
mkdir -p selftest_out
rm -f selftest_out/report_net_ihost.txt selftest_out/report_net_e.txt selftest_out/report_net_f.txt selftest_out/report_net_z.txt selftest_out/inet_code.txt
RVW="$RV"; command -v cygpath >/dev/null && RVW="$(cygpath -w "$RV")"
"$PY" -I "$RVW" --bind 127.0.0.1 --port "$SPORT" --conf nonexistent.conf > inet_server.log 2>&1 & S=$!
sleep 1
kill -0 $S 2>/dev/null || { echo "rendezvous server did not start:"; cat inet_server.log; exit 2; }
export NMK_INET_SERVER="127.0.0.1:$SPORT"
./NormandyMapmaker.exe --selftest-net ihost "$PORT" > net_ihost.log 2>&1 & H=$!
sleep 3
./NormandyMapmaker.exe --selftest-net e "$PORT" > net_e.log 2>&1 & E=$!
NMK_INET_FORCE_RELAY=1 ./NormandyMapmaker.exe --selftest-net f "$PORT" > net_f.log 2>&1 & F=$!
sleep 12
./NormandyMapmaker.exe --selftest-net z "$PORT" > net_z.log 2>&1 & Z=$!
status=0
for p in $H $E $F $Z; do
  for i in $(seq 1 720); do kill -0 $p 2>/dev/null || break; sleep 1; done
  kill -0 $p 2>/dev/null && { echo "process $p still running after 12 min: stopping it"; kill $p; status=1; }
  wait $p || status=1
done
kill $S 2>/dev/null; wait $S 2>/dev/null
for r in ihost e f z; do printf 'inet %-5s ' "$r"; tail -1 "selftest_out/report_net_$r.txt" 2>/dev/null || { echo "no report"; status=1; }; done
exit $status
