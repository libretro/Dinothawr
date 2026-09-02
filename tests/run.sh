#!/bin/sh
# Runs the oracles against the assets in this repository and checks them
# against known-good hashes.
#
# Every hash here was produced by a build believed correct at the time.
# A mismatch means behaviour changed - which is sometimes intended, and
# then the hash is what gets updated, deliberately and in the same commit
# as the change. It is not a number to re-baseline when it goes red.
#
# usage: tests/run.sh [core.so]
set -e

core="${1:-./dinothawr_libretro.so}"
sysdir="."
harness="tests/harness"
dump="tests/dump_tilemap"
status=0

check()
{
   name="$1"; want="$2"; got="$3"
   if [ "$want" = "$got" ]; then
      echo "ok   $name"
   else
      echo "FAIL $name: expected $want, got $got"
      status=1
   fi
}

video_hash()
{
   "$harness" "$core" "$sysdir" "$1" 2>&1 |
      sed -n 's/.*video \([0-9a-f]*\).*/\1/p'
}

# Gameplay: the scripted walk pushes blocks around level 1-1.
check "frame hash (walk)" d9607ffca98cd203 "$(CYCLES=3 video_hash 1500)"

# The front end: menu navigation, the slide, the locked-chapter path.
check "frame hash (menu)" f180da434714a3d7 \
   "$(INPUT=menu CYCLES=3 video_hash 2500)"

# The only script that finishes a level, so the only one that reaches the
# win check, the win animation and the level advance behind them. The
# save is the check here, not the frame.
check "save after solving 1-1" 248a4ef26ac68f44 \
   "$(INPUT=solve CYCLES=1 "$harness" "$core" "$sysdir" 1500 2>&1 |
      sed -n 's/.*sram: [0-9]* bytes, \([0-9a-f]*\).*/\1/p')"

# The save survives a round trip through the frontend's SRAM.
tests/make-save.sh /tmp/icy-save-in.bin
SRAM_LOAD=/tmp/icy-save-in.bin SRAM_DUMP=/tmp/icy-save-out.bin \
   "$harness" "$core" "$sysdir" 300 >/dev/null 2>&1
if cmp -s /tmp/icy-save-in.bin /tmp/icy-save-out.bin; then
   echo "ok   save round-trip"
else
   echo "FAIL save round-trip: the save did not survive load and store"
   status=1
fi

# The map parse, which the frame hash can only tell you is wrong, never
# where.
check "tilemap level_1-1"  8cead375aed458f82aa9a360ffd6d280 \
   "$("$dump" dinothawr/level_1-1.tmx  | md5sum | cut -d' ' -f1)"
check "tilemap level_5-3"  71098612d4405d82b5ac23485b4f0759 \
   "$("$dump" dinothawr/level_5-3.tmx  | md5sum | cut -d' ' -f1)"
check "tilemap level_10-5" b8d3fb395f3fdb96bc638a19ab53b715 \
   "$("$dump" dinothawr/level_10-5.tmx | md5sum | cut -d' ' -f1)"

exit $status
