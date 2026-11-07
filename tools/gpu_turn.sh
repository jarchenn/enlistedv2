#!/usr/bin/env bash
# GPU turn-taking for parallel work sessions: runs "<cmd...>" only while holding a machine-wide lock, so self-test suites
# from different worktrees don't share the GPU (frame-time gates, timing-sensitive tests) or fight over it.
# Usage: tools/gpu_turn.sh <cmd> [args...]          e.g. tools/gpu_turn.sh ./NormandyMapmaker.exe --selftest-interiors
#   GPU_TURN_LOCK=<dir>     lock directory (default: %TEMP%/nmk/gpu.lock)
#   GPU_TURN_STALE=<min>    a lock older than this is treated as abandoned and taken over (default 40)
#   GPU_TURN_TAG=<text>     who holds the lock (shown to waiters; default: the current directory)
# The lock is a directory (mkdir is atomic); its "owner" file says who holds it and since when. The command's exit code is
# returned. The lock is released on exit, Ctrl+C or kill.
set -u
[ $# -ge 1 ] || { echo "usage: tools/gpu_turn.sh <cmd> [args...]"; exit 2; }
LOCK="${GPU_TURN_LOCK:-${TEMP:-/tmp}/nmk/gpu.lock}"
STALE="${GPU_TURN_STALE:-40}"
TAG="${GPU_TURN_TAG:-$(pwd)}"
mkdir -p "$(dirname "$LOCK")"
waited=0
while ! mkdir "$LOCK" 2>/dev/null; do
  if [ -n "$(find "$LOCK" -maxdepth 0 -mmin +"$STALE" 2>/dev/null)" ]; then
    echo "gpu_turn: lock older than $STALE min ($(cat "$LOCK/owner" 2>/dev/null)): taking it over"
    rm -rf "$LOCK"; continue
  fi
  if [ $((waited % 60)) -eq 0 ]; then echo "gpu_turn: waiting for the GPU ($(cat "$LOCK/owner" 2>/dev/null))"; fi
  sleep 5; waited=$((waited + 5))
done
echo "$TAG | $* | since $(date '+%H:%M:%S')" > "$LOCK/owner"
trap 'rm -rf "$LOCK"' EXIT INT TERM
[ $waited -gt 0 ] && echo "gpu_turn: got the GPU after ${waited}s"
"$@"
