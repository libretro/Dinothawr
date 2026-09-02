# Regression and benchmark harness

`harness.c` is a headless libretro frontend. It loads the core, runs it
for a fixed number of frames against a scripted input pattern, and
prints an FNV-1a hash of every frame the core hands back, plus the time
spent inside `retro_run`.

```
make          # the core
make tests    # harness and dump_tilemap, into tests/
tests/harness dinothawr_libretro.so <system-dir> <frames>
```

`make tests` is not part of `all`: the test programs need a host
toolchain and are meaningless when cross-compiling. Build them anyway
when you touch this core. Nothing built `dump_tilemap` for several
patches after the map reader moved to C, and it had stopped compiling
without anyone noticing - a dead oracle reads exactly like a passing
one.

`<system-dir>` is a directory containing a `dinothawr` directory of game
assets.

## Checking a behaviour-preserving change

The renderer is deterministic, so the frame hash is exact: a change that
is not meant to alter output must not alter the hash. Both audio paths
have to be run, because they are disjoint and a bug in one is invisible
in the other:

```
CYCLES=5 ./harness ../dinothawr_libretro.so ./sys 2000
AUDIO_FLOAT=1 CYCLES=5 ./harness ../dinothawr_libretro.so ./sys 2000
```

`CYCLES` loads and unloads that many times before the measured run, so
teardown is exercised. A single load hides anything that only repeats.
The same two invocations under ASan/UBSan/LSan and under TSan are what
the core is swept with before a commit.

## Menu navigation

`INPUT=menu` swaps the scripted walk for one that drives the front end:
short taps that step the level and chapter selection, OK to enter a
level, START to come back out.

The default walk holds each direction long enough to move the player,
which in the menu runs the selection to one end and leaves it there. So
a default run draws the menu but never its transitions - the slide
animation, the chapter change, the locked-chapter path. Those are only
reached under `INPUT=menu`, and it produces a different frame hash
because of it.

Run both. The default covers gameplay, `INPUT=menu` covers the front
end, and combined with `SRAM_LOAD` it covers the front end with levels
already cleared, which is a third set of states again.

## The background APNG

`tests/make-bg-apng.py` packs the four full-screen backgrounds into
`dinothawr/assets/bg.apng`, which the .game file refers to by frame.
Rerun it if any of them changes; it checks its own output is pixel-exact
before writing.

The four are all 320x200, none uses alpha, and they use 74 colours
between them - so one indexed frame format holds all four exactly. That
matters: converting them to RGBA instead, which is what most tools do by
default, produces a file nearly twice the size of the PNGs it replaces.

## Running everything

`tests/run.sh` runs every oracle below against the assets in this
repository and checks them against known-good hashes, exiting non-zero
on a mismatch.

```
make && make tests
tests/run.sh
```

A hash going red means behaviour changed. That is sometimes intended -
and then the hash is updated deliberately, in the same commit as the
change that moved it. It is not a number to re-baseline when it is
inconvenient.

## Solving a level

`INPUT=solve` plays level_1-1 to completion: face the block, push it
twice to walk it across, go round behind it and push it onto the goal.
It is the only script here that finishes a level, so it is the only one
that reaches the win check, the win animation and the level advance
behind them.

```
INPUT=solve tests/harness dinothawr_libretro.so <system-dir> 1500
```

The save hash changes to `248a4ef26ac68f44` when it works - a completed
level is recorded - so that hash is the check, not the frame hash.

This is worth having. A bug sat in the win animation for two patches:
it walked a fixed-size array to its end rather than to the number of
entries filled, dereferencing uninitialised pointers on every tick the
animation ran. Every other oracle in this directory passed the whole
time. Under `INPUT=solve` with the bug reintroduced, UBSan reports a
null member access and ASan segfaults on the first frame of the
animation.

Timing matters: a move is ignored while the previous one is still in
flight, so the script leaves 90 frames between presses. If the level or
its timings change, retime it rather than assume it still solves - a
script that stops solving silently goes back to covering nothing.

## The save

The game's save is SRAM, not the serialize entry points - those return
zero - so a run that only checks frames never touches it. The harness
hashes `retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)` before unloading
and prints the size and hash; `SRAM_DUMP=path` writes the bytes out.

The scripted input walks and pushes but never clears a level, so on its
own the run only ever exercises the save code on zeroes. `SRAM_LOAD`
fixes that: it writes a save into SRAM before the first frame and calls
`retro_reset`, which is what makes the game re-read it.

```
tests/make-save.sh /tmp/save.bin
SRAM_LOAD=/tmp/save.bin SRAM_DUMP=/tmp/out.bin \
   ./harness ../dinothawr_libretro.so ./sys 300
cmp /tmp/save.bin /tmp/out.bin
```

That round-trips exactly, which covers both halves of the save code -
the parse on the way in and the write on the way out. It also changes
the frame hash, because the menu draws which levels are cleared, so an
injected save widens what the video oracle covers as well.

## Benchmarking

Four things matter more than the numbers, all of them learned by getting
a result wrong first:

1. `NOHASH=1` for timing runs. Hashing the framebuffer costs more per
   frame than the core does, and leaving it in the timed region hides
   whatever is being measured.
2. Strip the music from the assets used for timing - see
   `quiet-assets.sh`. The vorbis decode runs on its own thread and on a
   single-core machine it lands in a random part of the run.
3. Run variants in a symmetric order (A B B A) inside one batch and take
   medians over tens of samples. Separate batches drift enough to
   reverse a result.
4. Build every variant twice, once normally and once with
   `-falign-loops=32 -falign-functions=32`, and only believe a result
   that holds both ways. Code alignment alone is worth about six percent
   on this workload, which is more than most changes worth making.

## Rewriting a parser

`dump_tilemap.c` prints everything a `.tmx` parse produced - layer
names and attributes, every tile's position, size and attributes, and
the collision grid - in a stable order.

It exists because the frame hash is the wrong oracle for a parser. The
hash says the picture changed; it does not say which tile got the wrong
attributes, and hunting that difference by reading a five-hundred-line
rewrite does not work. Dump before, dump after, diff, and only then
check the hash.

Run it over more than one level. `level_1-1` has a single Blocks tile
and exercises almost none of the tileset code; `level_5-3` and
`level_10-5` are better.

The same argument applies to anything else whose output is data rather
than pixels - the save format, the sprite parse, the audio decode. Build
the oracle at the layer being changed, not three layers downstream.
