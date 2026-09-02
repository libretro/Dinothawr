#!/bin/sh
# Writes a synthetic Dinothawr save.
#
# The format is one line per chapter, holding a comma-separated
# best-push count per level, trailing comma included, zero-padded to 512
# bytes. A non-zero count means the level was cleared in that many
# pushes.
#
# This exists so the harness has something other than an empty buffer to
# parse. The scripted input never clears a level, so without an injected
# save the save code is only ever exercised on zeroes.
#
# usage: make-save.sh <output file>
set -e
out="$1"
[ -n "$out" ] || { echo "usage: $0 <output>" >&2; exit 1; }

: > "$out"
c=0
while [ $c -lt 10 ]; do
   line=""
   l=0
   while [ $l -lt 5 ]; do
      if [ $(( (c + l) % 3 )) -eq 0 ]; then
         line="$line$(( 3 + c + l )),"
      else
         line="${line}0,"
      fi
      l=$(( l + 1 ))
   done
   printf '%s\n' "$line" >> "$out"
   c=$(( c + 1 ))
done

# Pad to the 512 bytes the core expects.
size=$(wc -c < "$out")
dd if=/dev/zero bs=1 count=$(( 512 - size )) >> "$out" 2>/dev/null
