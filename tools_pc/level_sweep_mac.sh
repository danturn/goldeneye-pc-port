#!/bin/bash
# macOS/arm64 level sweep: bare -level_XX boot per level, crash-log detection.
# The GE_PCDUMP capture path is currently broken on macOS (it reads the bound
# FBO / front buffer and writes black frames), so PASS here means "ran the
# whole window without a crash log", not "rendered non-clear pixels".
# Env: SWEEP_SECS (default 18).
cd "$(dirname "$0")/.."
LV="Dam:33 Facility:34 Runway:35 Surface1:36 Bunker1:09 Silo:20 Frigate:26 Surface2:43 Bunker2:27 Statue:22 Archives:24 Streets:29 Depot:30 Train:25 Jungle:37 Control:23 Caverns:39 Cradle:41 Aztec:28 Egypt:32 Cuba:54"
OUT=/tmp/mac_sweep_results; : > $OUT
for entry in $LV; do
  name=${entry%%:*}; num=${entry##*:}
  rm -f ge007.crash.log
  ./build-pc/ge007.aarch64 -level_$num > /tmp/sweeplogs/$name.log 2>&1 &
  pid=$!
  sleep ${SWEEP_SECS:-18}
  kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
  sleep 1
  frames=$(grep -cE 'frame [0-9]+ rendered' /tmp/sweeplogs/$name.log 2>/dev/null)
  if [ -f ge007.crash.log ]; then
    M=$(grep -oE 'MODULE: 0x[0-9a-f]+' ge007.crash.log|head -1|awk '{print $2}')
    P=$(grep -oE '^PC: 0x[0-9a-f]+' ge007.crash.log|awk '{print $2}')
    sym=$(atos -o build-pc/ge007.aarch64 -l "$M" "$P" 2>/dev/null | head -1)
    cp ge007.crash.log /tmp/sweeplogs/$name.crash.log
    echo "$name ($num): CRASH @ $sym" | tee -a $OUT
  else
    echo "$name ($num): OK (ran the window)" | tee -a $OUT
  fi
done
echo "SWEEP DONE" | tee -a $OUT
