#!/bin/sh
# Builds a copy of the game assets with the music removed, for timing
# runs. The background decode runs off-thread and its cost lands in
# whichever frame it happens to overlap, which is noise a benchmark
# cannot average away on a single-core machine.
#
# usage: quiet-assets.sh <source dinothawr dir> <destination system dir>
set -e
src="$1"
dst="$2"
[ -n "$src" ] && [ -n "$dst" ] || { echo "usage: $0 <dinothawr-dir> <system-dir>" >&2; exit 1; }
mkdir -p "$dst"
cp -r "$src" "$dst/dinothawr"
sed -i 's|[[:space:]]*<bg source="[^"]*"[^/]*/>||g' "$dst/dinothawr/dinothawr.game"
echo "wrote $dst/dinothawr (music stripped)"
