# Regression and benchmark harness

`harness.c` is a headless libretro frontend. It loads the core, runs it
for a fixed number of frames against a scripted input pattern, and
prints an FNV-1a hash of every frame the core hands back, plus the time
spent inside `retro_run`.

```
gcc -O2 -std=gnu99 -I../libretro-common/include harness.c -o harness -ldl
./harness ../dinothawr_libretro.so <system-dir> <frames>
```

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
