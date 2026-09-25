#!/usr/bin/env bash
# golden_gate.sh - one-command per-patch visual gate.
#
# Pins data/ge007.ini to a defaults-only 640x480 window, captures Bunker 1
# frames with GE_PCDUMP, and compares against the golden baseline with
# tools_pc/framediff.py. The maintainer's ini is ALWAYS restored (EXIT/INT/TERM
# trap); data/ge007.eep is never touched.
#
# Usage:
#   bash tools_pc/golden_gate.sh                     # golden compare (default)
#   bash tools_pc/golden_gate.sh --level 20          # custom level, no compare
#   bash tools_pc/golden_gate.sh --level 20 --dump 400-1000:600
#
# Custom mode (--level/--dump) skips the golden compare and just reports
# frames captured + any 'Crash!' in the log.
#
# Exit codes: golden mode -> framediff.py's (0 pass, 1 over threshold,
# 2 usage/IO). Custom mode -> 0 if frames captured and no 'Crash!', else 1.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT" || exit 2

INI=data/ge007.ini
LOG=scratch/golden_gate.log
FRAMEDIFF_OUT=scratch/golden_gate.framediff
LEVEL=09
DUMP="640-1120:240"
GOLDEN=1

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --level) shift; LEVEL="${1:?--level needs a value}"; GOLDEN=0 ;;
    --dump)  shift; DUMP="${1:?--dump needs lo-hi:step}"; GOLDEN=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

case "$LEVEL" in *[!0-9]*|"") echo "--level must be numeric, got '$LEVEL'" >&2; exit 2;; esac
case "$DUMP" in *[!0-9:-]*|:*|*::*) echo "--dump must look like lo-hi:step, got '$DUMP'" >&2; exit 2;; esac

mkdir -p scratch

# --- ini pin + guaranteed restore -------------------------------------------
INI_EXISTED=1
[ -f "$INI" ] || INI_EXISTED=0
BAK="$(mktemp)" || { echo "mktemp failed" >&2; exit 2; }
if [ "$INI_EXISTED" = 1 ]; then cp "$INI" "$BAK"; fi

restore_ini() {
  if [ "$INI_EXISTED" = 1 ]; then
    cp "$BAK" "$INI"
  else
    rm -f "$INI"
  fi
  rm -f "$BAK"
}
trap restore_ini EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf '[Window]\nWidth = 640\nHeight = 480\n' > "$INI"

# --- run ---------------------------------------------------------------------
rm -rf ppm
PATH="/c/msys64/mingw64/bin:$PATH" GE_PCDUMP="$DUMP" \
  timeout 90 ./build-pc/ge007.x86_64.exe "-level_$LEVEL" > "$LOG" 2>&1

if [ "$GOLDEN" = 1 ]; then
  python tools_pc/framediff.py ppm > "$FRAMEDIFF_OUT" 2>&1
  RC=$?
  tail -n 1 "$FRAMEDIFF_OUT"
  rm -rf ppm
  exit $RC
else
  N=0
  [ -d ppm ] && N=$(ls ppm | wc -l)
  CRASH=no
  grep -q 'Crash!' "$LOG" && CRASH=yes
  echo "level_$LEVEL dump=$DUMP frames=$N crash=$CRASH (log: $LOG)"
  rm -rf ppm
  if [ "$N" -gt 0 ] && [ "$CRASH" = no ]; then exit 0; else exit 1; fi
fi
