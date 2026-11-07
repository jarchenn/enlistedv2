#!/usr/bin/env bash
# GPU turn-taking for parallel work sessions: runs "<cmd...>" only while holding a machine-wide lock, so self-test suites
# from different worktrees don't share the GPU (frame-time gates, timing-sensitive tests) or fight over it.
# Usage: tools/gpu_turn.sh <cmd> [args...]          e.g. tools/gpu_turn.sh ./NormandyMapmaker.exe --selftest-interiors
#   GPU_TURN_LOCK=<dir>     lock directory (default: %TEMP%/nmk/gpu.lock)
#   GPU_TURN_STALE=<min>    a lock older than this is treated as abandoned and taken over (default 40)
#   GPU_TURN_TAG=<text>     who holds the lock (shown to waiters; default: the current directory)
# Fair queue: each waiter takes a ticket in <lock>.q (named by arrival time); only the oldest live ticket may take the
# lock, so a back-to-back runner cannot cut in front of someone who has waited longer. A waiter touches its ticket
# every poll; tickets untouched for 2 minutes belong to a waiter that died and are removed. The lock is a directory
# (mkdir is atomic); its "owner" file says who holds it and since when. The command's exit code is returned. The lock
# and the ticket are released on exit, Ctrl+C or kill.
set -u
[ $# -ge 1 ] || { echo "usage: tools/gpu_turn.sh <cmd> [args...]"; exit 2; }
LOCK="${GPU_TURN_LOCK:-${TEMP:-/tmp}/nmk/gpu.lock}"
Q="$LOCK.q"
STALE="${GPU_TURN_STALE:-40}"
TAG="${GPU_TURN_TAG:-$(pwd)}"
mkdir -p "$(dirname "$LOCK")" "$Q"
TICKET="$Q/$(date +%s%N)-$$-$RANDOM"
echo "$TAG | $*" > "$TICKET"
trap 'rm -f "$TICKET"' EXIT INT TERM
waited=0
while :; do
  touch "$TICKET"
  find "$Q" -type f -mmin +2 ! -path "$TICKET" -delete 2>/dev/null    # waiters that died
  if [ -n "$(find "$LOCK" -maxdepth 0 -mmin +"$STALE" 2>/dev/null)" ]; then
    echo "gpu_turn: lock older than $STALE min ($(cat "$LOCK/owner" 2>/dev/null)): taking it over"; rm -rf "$LOCK"
  fi
  first="$(ls -1 "$Q" 2>/dev/null | sort | head -1)"
  if [ "$Q/$first" = "$TICKET" ] && mkdir "$LOCK" 2>/dev/null; then break; fi
  if [ $((waited % 60)) -eq 0 ]; then
    ahead=$(ls -1 "$Q" 2>/dev/null | sort | awk -v me="$(basename "$TICKET")" '$0 < me' | wc -l)
    echo "gpu_turn: waiting for the GPU ($(cat "$LOCK/owner" 2>/dev/null); $ahead ahead in the queue)"
  fi
  sleep 5; waited=$((waited + 5))
done
echo "$TAG | $* | since $(date '+%H:%M:%S')" > "$LOCK/owner"
rm -f "$TICKET"
trap 'rm -rf "$LOCK"' EXIT INT TERM
[ $waited -gt 0 ] && echo "gpu_turn: got the GPU after ${waited}s"
"$@"
