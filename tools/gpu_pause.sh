#!/usr/bin/env bash
# Pause / resume the background test queue (tools/gpu_turn.sh) while the user plays: "on" stops new test runs from
# taking the GPU (runs already going finish), "off" resumes. Usage: tools/gpu_pause.sh on|off|status [reason]
LOCK="${GPU_TURN_LOCK:-${TEMP:-/tmp}/nmk/gpu.lock}"
case "${1:-status}" in
  on)  echo "${2:-user is playing} since $(date '+%H:%M')" > "$LOCK.pause"; echo "GPU tests paused" ;;
  off) rm -f "$LOCK.pause"; echo "GPU tests resumed" ;;
  *)   [ -e "$LOCK.pause" ] && echo "paused: $(cat "$LOCK.pause")" || echo "running" ;;
esac
