#!/usr/bin/env bash
# Fast mode of tools/run_all_tests.sh: runs only the suites the change touches, picked from the files changed since a
# base commit, plus a small core set. The slow AI suites (ai ~4 min, aicourse ~5 min) and the LAN test run only when
# AI / nav / net code changed; the maps frame-time gate runs whenever rendering, terrain, maps or assets changed.
# The GPU-free match suite runs alongside the GPU suites. Every GPU suite goes through tools/gpu_turn.sh.
# The full run (tools/run_all_tests.sh) stays the check before master.
#
# Usage: tools/run_fast_tests.sh <build dir> <base ref> [--dry-run] [--net-port N]
#   <base ref>: what the change is compared against, e.g. master or the branch point (git merge-base)
#   --dry-run:  print the chosen suites and why, run nothing
# Suites added to run_all_tests.sh later are picked up automatically: a suite with no rule below always runs.
set -u
B="${1:?build dir}"; BASE="${2:?base ref}"; shift 2
DRY=0; NETPORT=27979
while [ $# -gt 0 ]; do case "$1" in --dry-run) DRY=1;; --net-port) NETPORT="$2"; shift;; esac; shift; done
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(git rev-parse --show-toplevel 2>/dev/null || (cd "$HERE/.." && pwd))"   # the checkout it is run from (a worktree may run another copy of this script)
[ -f "$REPO/tools/run_all_tests.sh" ] || REPO="$(cd "$HERE/.." && pwd)"
CHANGED="$(git -C "$REPO" diff --name-only "$BASE"...HEAD 2>/dev/null; git -C "$REPO" diff --name-only 2>/dev/null)"
[ -n "$CHANGED" ] || { echo "no changes against $BASE"; exit 0; }

# suite -> regex over changed paths (extended regex). "ALWAYS" = core set.
rule() {
  case "$1" in
    A)          echo 'ALWAYS' ;;
    B)          echo 'src/(app|editor|mapdoc|interior|assets)\.' ;;
    damage)     echo 'src/(damage|wallfield|ballistics|assets|interior|tank_ram)|content/assets/(house_|small_|large_|vil_|bunker_|ruin_|furn_)|content/settings/destruction' ;;
    pak)        echo 'src/(station|weapons|arsenal|fx)|content/(weapons|assets/gun_)' ;;
    weapons)    echo 'src/(weapons|arsenal|viewmodel|ballistics)|content/weapons/' ;;
    grass)      echo 'src/(grass|nature|render|terrain|surface)|content/assets/nat_' ;;
    fps)        echo 'src/(viewmodel|weapons|render|model|settings)|content/weapons/' ;;
    env)        echo 'src/(explore|ballistics|damage|builder|render|tank_ram|physics|mantle)' ;;
    arsenal)    echo 'src/(arsenal|weapons|station|builder)' ;;
    move)       echo 'src/(explore|physics|mantle|pose)|content/settings/movement' ;;
    swim)       echo 'src/(swim|water|explore|physics|mantle|pose|viewmodel|nav\.|render|audio|rules)|content/settings/movement' ;;
    sights)     echo 'src/(viewmodel|render|weapons|model)|content/weapons/' ;;
    visuals)    echo 'src/(thumbs|render|assets|editor|palette)|content/assets/' ;;
    update)     echo 'src/(explore|weapons|mantle|viewmodel)' ;;
    entities)   echo 'src/(equipment|paths|explore|builder|audio|wire)|content/audio/' ;;
    tanks)      echo 'src/(tank|damage|ballistics|wallfield|interior)|content/vehicles/' ;;
    interiors)  echo 'src/(interior|assets|wallfield|render|lights?)|content/assets/(furn_|house_|small_|large_|vil_|ruin_)' ;;
    editor)     echo 'src/(editor|terrain|snap|palette|dropin|builder|app)\.|src/editor_nav' ;;
    match)      echo 'src/(mpai|match|scenario|rules|game)' ;;
    ai)         echo 'src/(ai_|nav|mpai)|content/ai/' ;;
    rules)      echo 'src/(rules|damage|match|game|w1)' ;;
    gore)       echo 'src/(gore|decals|render)' ;;
    structures) echo 'src/(assets|physics|terrain_cuts|nav_links|interior)|content/assets/' ;;
    aicourse)   echo 'src/(ai_|nav)|content/ai/' ;;
    nature)     echo 'src/(nature|grass|snap|terrain|app)\.|content/assets/nat_' ;;
    settings)   echo 'src/(settings|render|audio)' ;;
    autopath)   echo 'src/(autopath|paths)' ;;
    snap)       echo 'src/(snap|editor|builder|paths)|content/assets/(aw_|bcg_|bch_|path_|wat_|rub_)' ;;
    terrain)    echo 'src/(terrain|beautify|ground)|content/settings/beautify' ;;
    render)     echo 'src/(render|water|frustum|lighting|textures|settings)|content/textures/' ;;
    lights)     echo 'src/(render|lighting|lights?|interior)|content/assets/' ;;
    maps)       echo 'src/(render|terrain|w5|assets|interior|nature|grass|settings|frustum|lighting|textures|water)|content/(assets|templates|textures|settings/maps)' ;;
    *)          echo 'ALWAYS' ;;   # a suite added later with no rule here: always run
  esac
}
NETRULE='src/(net|inet|mpai|game|damage|interior|wallfield|lights?|mapdoc)'
HEADLESS=" match "

cd "$B" || exit 2
mapfile -t LINES < <(grep -E '^run [a-zA-Z]' "$REPO/tools/run_all_tests.sh")
pick=(); why=()
for ln in "${LINES[@]}"; do
  set -- $ln; name="$2"; r="$(rule "$name")"
  if [ "$r" = ALWAYS ]; then pick+=("$ln"); why+=("$name: core / no rule"); continue; fi
  hit="$(printf '%s\n' "$CHANGED" | grep -E -m1 "$r")"
  [ -n "$hit" ] && { pick+=("$ln"); why+=("$name: $hit"); }
done
net=0; nh="$(printf '%s\n' "$CHANGED" | grep -E -m1 "$NETRULE")"; [ -n "$nh" ] && net=1
echo "fast mode against $BASE: ${#pick[@]} of ${#LINES[@]} suites$([ $net = 1 ] && echo " + LAN ($nh)")"
for w in "${why[@]}"; do echo "  $w"; done
[ $DRY = 1 ] && exit 0

export GPU_TURN_TAG="${GPU_TURN_TAG:-fast tests $(basename "$REPO")}"
status=0; bg=()
runone() {   # line from run_all_tests.sh: "run <name> <args> # comment"
  local ln="$1"; set -- $ln; local name="$2"; shift 2
  local args="${*%%#*}"; eval "set -- $args"
  if [[ "$HEADLESS" == *" $name "* ]]; then ./NormandyMapmaker.exe "$@" >/dev/null 2>&1; else bash "$HERE/gpu_turn.sh" ./NormandyMapmaker.exe "$@" >/dev/null 2>&1; fi
  local rc=$?; printf '%-12s exit %d  %s\n' "$name" $rc "$(tail -1 "selftest_out/report_$name.txt" 2>/dev/null)"; return $rc
}
for ln in "${pick[@]}"; do set -- $ln; [[ "$HEADLESS" == *" $2 "* ]] && { runone "$ln" & bg+=($!); }; done   # GPU-free suites in parallel
for ln in "${pick[@]}"; do set -- $ln; [[ "$HEADLESS" == *" $2 "* ]] || runone "$ln" || status=1; done
for p in "${bg[@]}"; do wait "$p" || status=1; done
if [ $net = 1 ]; then echo "--- net"; bash "$HERE/gpu_turn.sh" bash "$HERE/run_net_test.sh" "$B" "$NETPORT" 2>&1 | tail -6; [ ${PIPESTATUS[0]} -eq 0 ] || status=1; fi
exit $status
