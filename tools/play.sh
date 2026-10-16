#!/bin/sh
# Run the game in the runner's live SDL window.
#
# Opens nds_runner's interactive frontend (`--interactive`) so the game is
# displayed as it executes, instead of the headless `--serve` path that
# tools/runwatch.py drives. See README.md ("Run") for the build steps this
# assumes: generated title banks plus a runner configured with
# PKMNG5PC_BUILD_RUNNER=ON (which supplies SDL3).
#
# Only Black is banked today, so the ROM defaults to the Black image in roms/.
#
# Usage:
#   sh tools/play.sh
#   sh tools/play.sh --rom "roms/<your-rom>.nds"
#   sh tools/play.sh --layout separate
#   sh tools/play.sh --frames 600 --stats     # present 600 frames, print stats, exit
#   sh tools/play.sh --save-path banks/black.sav
#   sh tools/play.sh --diagnostics            # keep a perf bundle in banks/play/
#
# The positional BIOS directory and ARM9 cycle budget are still required by the
# runner, but the budget is inert under --interactive: the window stays open
# until you close it or press Esc.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

rom=${PKMNG5PC_ROM:-}
runner=${PKMNG5PC_RUNNER:-}
bios_dir=bios
layout=stacked
frames=0
stats=0
coverage=0
save_path=
save=0
diagnostics=0
extra=
while [ $# -gt 0 ]; do
  case $1 in
    --rom)      rom=$2; shift 2 ;;
    --runner)   runner=$2; shift 2 ;;
    --bios-dir) bios_dir=$2; shift 2 ;;
    --layout)   layout=$2; shift 2 ;;
    --frames)   frames=$2; shift 2 ;;
    --save-path) save_path=$2; shift 2 ;;
    --save)     save=1; shift ;;
    --stats)    stats=1; shift ;;
    --coverage) coverage=1; shift ;;
    --diagnostics) diagnostics=1; shift ;;
    --)         shift; extra=$*; break ;;
    -h|--help)  sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)          echo "play: unknown option: $1" >&2; exit 2 ;;
  esac
done

fail() { echo "play: $1" >&2; exit 1; }

# --- runner -----------------------------------------------------------------

if [ -z "$runner" ]; then
  for preset in linux-relwithdebinfo linux-release linux-debug; do
    candidate="$root/build/$preset/third_party/ndsrecomp/runner/nds_runner"
    if [ -x "$candidate" ]; then runner=$candidate; break; fi
  done
fi
[ -n "$runner" ] || fail "no built runner found. Configure and build one first:
  cmake --preset linux-relwithdebinfo -DPKMNG5PC_BUILD_RUNNER=ON
  cmake --build --preset linux-relwithdebinfo
or pass --runner <path>."

# --- banks ------------------------------------------------------------------

banks="$root/banks/black"
set -- "$banks"/*_[0-9][0-9].c
[ -e "$1" ] || fail "no generated title banks in $banks. Generate them first:
  build/linux-debug/tools/pkmn_bankgen \"roms/<your-rom>.nds\" banks/black \\
      --recompiler third_party/ndsrecomp/recompiler/build/nds_recompile"

# --- ROM --------------------------------------------------------------------

# The NDS header carries the 4-byte game code at 0x0C; Black is IRBO.
game_code() { dd if="$1" bs=1 skip=12 count=4 2>/dev/null; }

if [ -z "$rom" ]; then
  black=
  count=0
  for f in "$root"/roms/*.nds; do
    [ -e "$f" ] || continue
    count=$((count + 1))
    if [ "$(game_code "$f")" = "IRBO" ] && [ -z "$black" ]; then black=$f; fi
  done
  if [ -n "$black" ]; then
    rom=$black
  elif [ "$count" -eq 1 ]; then
    for f in "$root"/roms/*.nds; do rom=$f; done
  else
    fail "no Black ROM (game code IRBO) found in roms/ - only Black is banked so far.
Pass --rom <path>."
  fi
fi
[ -f "$rom" ] || fail "no such ROM: $rom"

# --- command ----------------------------------------------------------------

set -- "$runner" "$bios_dir" 250000000 \
  --boot direct --generated-firmware --freebios \
  --rom "$rom" --interactive --screen-layout "$layout"
[ "$coverage" -eq 1 ] && set -- "$@" || set -- "$@" --no-coverage-manifest
if [ "$diagnostics" -eq 1 ]; then set -- "$@" --diagnostics on --diagnostics-dir "$root/banks/play"
else set -- "$@" --diagnostics off; fi
if [ -n "$save_path" ]; then set -- "$@" --save-path "$save_path"
elif [ "$save" -eq 0 ]; then set -- "$@" --no-save; fi
[ -n "$extra" ] && set -- "$@" $extra

[ "$frames" -gt 0 ] && NDS_FRONTEND_MAX_FRAMES=$frames && export NDS_FRONTEND_MAX_FRAMES
if [ "$stats" -eq 1 ] || [ "$frames" -gt 0 ]; then NDS_FRONTEND_STATS=1; export NDS_FRONTEND_STATS; fi
# Same reason as tools/play.ps1: the threaded 2D scanline path wedges one
# scheduler round at t=83.69 s emulated; run the byte-locked inline path.
if [ -z "$NDS_GPU2D_THREADED" ]; then NDS_GPU2D_THREADED=0; export NDS_GPU2D_THREADED; fi

echo "runner: $runner"
echo "rom:    $rom"
echo "banks:  $banks"
echo "keys:   arrows=D-pad  Z=A X=B  A=Y S=X  Q=L W=R  Enter=Start  Backspace=Select  Esc=quit"

cd "$root"
exec "$@"
