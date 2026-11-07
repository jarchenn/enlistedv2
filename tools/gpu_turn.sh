#!/usr/bin/env bash
# GPU turn-taking for parallel work sessions: runs "<cmd...>" while holding one of N machine-wide GPU slots, so the
# self-test suites of many worktrees share the GPU in a controlled way instead of all at once.
# Usage: tools/gpu_turn.sh <cmd> [args...]          e.g. tools/gpu_turn.sh ./NormandyMapmaker.exe --selftest-interiors
#   GPU_TURN_SLOTS=<n>      how many runs may use the GPU at once (default 5; the user raised it from 1 to 3 to 5, 2026-11-07)
#   GPU_TURN_EXCLUSIVE=1    take every slot (frame-time measurements that must not share the GPU)
#   GPU_TURN_LOCK=<dir>     slot 1 directory (default: %TEMP%/nmk/gpu.lock; slot k > 1 is <dir>.k)
#   GPU_TURN_STALE=<min>    a slot older than this is treated as abandoned and taken over (default 40)
#   GPU_TURN_TAG=<text>     who holds the slot (shown to waiters; default: the current directory)
# Pause: while the file <lock>.pause exists (tools/gpu_pause.sh on|off), no new run takes a slot; running ones finish.
# Fair queue: each waiter takes a ticket in <lock>.q (named by arrival time). A waiter may take a free slot only while
# fewer than N older tickets are waiting, so nobody is starved; an exclusive waiter must be first and waits for every
# slot. A waiter touches its ticket every poll; tickets untouched for 2 minutes belong to a waiter that died and are
# removed. Slots are directories (mkdir is atomic); each has an "owner" file. The command's exit code is returned.
set -u
[ $# -ge 1 ] || { echo "usage: tools/gpu_turn.sh <cmd> [args...]"; exit 2; }
LOCK="${GPU_TURN_LOCK:-${TEMP:-/tmp}/nmk/gpu.lock}"
Q="$LOCK.q"
N="${GPU_TURN_SLOTS:-5}"; [ "$N" -ge 1 ] 2>/dev/null || N=1
EXCL="${GPU_TURN_EXCLUSIVE:-0}"
STALE="${GPU_TURN_STALE:-40}"
TAG="${GPU_TURN_TAG:-$(pwd)}"
slot() { [ "$1" -eq 1 ] && echo "$LOCK" || echo "$LOCK.$1"; }
mkdir -p "$(dirname "$LOCK")" "$Q"
TICKET="$Q/$(date +%s%N)-$$-$RANDOM$([ "$EXCL" = 1 ] && echo -X)"   # -X: exclusive (nobody behind it takes a slot)
echo "$TAG | $*" > "$TICKET"
HELD=()
release() { rm -f "$TICKET"; for d in "${HELD[@]}"; do rm -rf "$d"; done; }
trap release EXIT INT TERM
waited=0
while :; do
  touch "$TICKET"
  find "$Q" -type f -mmin +2 ! -path "$TICKET" -delete 2>/dev/null    # waiters that died
  for k in $(seq 1 "$N"); do d="$(slot $k)"
    if [ -n "$(find "$d" -maxdepth 0 -mmin +"$STALE" 2>/dev/null)" ]; then echo "gpu_turn: slot $k older than $STALE min ($(cat "$d/owner" 2>/dev/null)): taking it over"; rm -rf "$d"; fi
  done
  ahead=$(ls -1 "$Q" 2>/dev/null | sort | awk -v me="$(basename "$TICKET")" '$0 < me' | wc -l)
  if [ -e "$LOCK.pause" ]; then   # the user is playing: no new test takes the GPU (running ones finish)
    [ $((waited % 60)) -eq 0 ] && echo "gpu_turn: paused ($(cat "$LOCK.pause" 2>/dev/null)); waiting"
    sleep 5; waited=$((waited + 5)); continue
  fi
  if [ "$EXCL" = 1 ]; then
    if [ "$ahead" -eq 0 ]; then
      for k in $(seq 1 "$N"); do d="$(slot $k)"; if mkdir "$d" 2>/dev/null; then HELD+=("$d"); fi; done   # keep what we got; wait for the rest
      [ "${#HELD[@]}" -eq "$N" ] && break
    fi
  elif [ "$ahead" -lt "$N" ] && ! ls -1 "$Q" 2>/dev/null | sort | awk -v me="$(basename "$TICKET")" '$0 < me' | grep -q -- "-X$"; then
    got=""; for k in $(seq 1 "$N"); do d="$(slot $k)"; if mkdir "$d" 2>/dev/null; then got="$d"; break; fi; done
    [ -n "$got" ] && { HELD+=("$got"); break; }
  fi
  if [ $((waited % 60)) -eq 0 ]; then
    busy=""; for k in $(seq 1 "$N"); do d="$(slot $k)"; [ -d "$d" ] && busy="$busy [$(cut -d'|' -f1 "$d/owner" 2>/dev/null | sed 's/ *$//')]"; done
    echo "gpu_turn: waiting for a GPU slot ($ahead ahead; busy:$busy)"
  fi
  sleep 5; waited=$((waited + 5))
done
for d in "${HELD[@]}"; do echo "$TAG | $* | since $(date '+%H:%M:%S')" > "$d/owner"; done
rm -f "$TICKET"
[ $waited -gt 0 ] && echo "gpu_turn: got the GPU after ${waited}s"
"$@"
