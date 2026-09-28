#!/bin/bash
# sweep-captures/capture.sh OUTDIR — capture GE_PCDUMP windows (sweep stride
# 80-400:40, ini pinned to 640x480 like verify.sh) for all 21 solo levels
# into OUTDIR/<level>/*.ppm. For pixel-identical comparison between trees:
# run once on the baseline tree, once per landing; then per level:
#   python3 tools_pc/framediff.py OUTDIR_A/<level> --golden OUTDIR_B/<level> --json
# (framediff exit 0 + worst_cell 0 = pixel-identical within its thresholds.)
set -u
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
export PATH="/c/msys64/mingw64/bin:$PATH"   # mingw runtime DLLs for the exe
OUT="$1"
rm -rf "$OUT"; mkdir -p "$OUT"

INI="$ROOT/data/ge007.ini"
INI_BAK=$(mktemp)
[ -f "$INI" ] && cp "$INI" "$INI_BAK"
printf '[Window]\nWidth = 640\nHeight = 480\n' > "$INI"
KILL() { "C:/Windows/system32/taskkill.exe" //F //IM ge007.x86_64.exe >/dev/null 2>&1; }
restore() { [ -s "$INI_BAK" ] && cp "$INI_BAK" "$INI" || rm -f "$INI"; KILL; }
trap restore EXIT INT TERM

LEVELS="dam:33 facility:34 runway:35 surface1:36 bunker1:09 silo:20 frigate:26 surface2:43 bunker2:27 statue:22 archives:24 streets:29 depot:30 train:25 jungle:37 control:23 caverns:39 cradle:41 aztec:28 egypt:32 cuba:54"
DUMP="80-400:40"
LO=80; HI=400; STEP=40
LAST=$(( LO + STEP * ((HI - LO) / STEP) ))
TARGET="$ROOT/ppm/frame_$(printf '%06d' $LAST).ppm"

FAIL=0
for e in $LEVELS; do
  name="${e%%:*}"; num="${e##*:}"
  KILL; sleep 0.3
  rm -f "$ROOT"/ppm/*.ppm "$ROOT/ge007.crash.log"
  ( cd "$ROOT"
    export GE_PCDUMP="$DUMP"
    timeout 90 ./build-pc/ge007.x86_64.exe "-level_$num" >"$OUT/$name.log" 2>&1 &
    gpid=$!
    for i in $(seq 1 450); do
      if [ -f "$TARGET" ] || [ -f "$ROOT/ge007.crash.log" ]; then sleep 0.2; break; fi
      kill -0 "$gpid" 2>/dev/null || break
      sleep 0.2
    done
    kill "$gpid" >/dev/null 2>&1
    wait "$gpid" 2>/dev/null )
  mkdir -p "$OUT/$name"
  mv "$ROOT"/ppm/*.ppm "$OUT/$name/" 2>/dev/null
  n=$(ls "$OUT/$name"/*.ppm 2>/dev/null | wc -l)
  crash=no; [ -f "$ROOT/ge007.crash.log" ] && crash=yes
  echo "$name: frames=$n crash=$crash"
  [ "$n" -gt 0 ] || FAIL=1
done
exit $FAIL
