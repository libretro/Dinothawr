/* Dinothawr - headless libretro frontend for regression and benchmark
 * runs.
 *
 * Loads the core, drives it for a fixed number of frames with a scripted
 * input pattern, and prints a hash of every frame it was handed. The
 * hash is the point: the renderer's output is deterministic, so any
 * change that is meant to be behaviour-preserving can be checked exactly
 * rather than by eye. It found an uninitialised Renderable::position
 * that only showed on one of the two audio paths.
 *
 * Build:
 *   gcc -O2 -std=gnu99 -I../libretro-common/include harness.c -o harness -ldl
 *
 * Run:
 *   ./harness ../dinothawr_libretro.so <system-dir> <frames>
 *
 * <system-dir> holds a "dinothawr" directory - point it at a directory
 * containing the game's assets.
 *
 * Environment:
 *   CYCLES=n      load and unload n times before the measured run, so
 *                 teardown paths are exercised. Leak checking wants
 *                 this; one load hides nothing that repeats.
 *   AUDIO_FLOAT=1 advertise float audio output, so the float mixer runs
 *                 instead of the int16 one. Both need testing: they are
 *                 disjoint paths, and a bug in one is invisible in the
 *                 other.
 *   NOHASH=1      skip hashing during timing runs. Hashing the
 *                 framebuffer costs more than a frame of the core, so
 *                 leaving it in the timed region drowns whatever is
 *                 being measured.
 *
 * Benchmarking notes, learned the hard way on this core:
 *
 *  - Disable music in the assets used for timing. The vorbis decode runs
 *    on its own thread and on a single-core box it steals a chunk of the
 *    run, at random.
 *  - Compare variants in a symmetric order (A B B A) within one batch
 *    and take medians over tens of samples. Non-paired batches drift
 *    enough to reverse a result.
 *  - Build every variant twice, once normally and once with
 *    -falign-loops=32 -falign-functions=32, and only believe a result
 *    that holds both ways. Code alignment alone is worth about six
 *    percent here, which is larger than most changes worth making.
 */

/* Minimal libretro frontend used to exercise the Dinothawr core under
 * sanitizers. Loads the shipped dinothawr.game via the system directory
 * path, runs frames with a scripted input pattern, then unloads. */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

#include <libretro.h>

static void *g_lib;
static const char *g_sysdir;
static unsigned g_frames;
static uint64_t g_video_acc;
static uint64_t g_audio_acc;
static uint64_t g_audio_frames;
static unsigned g_cur_frame;

static size_t audio_batch_float_cb(const float *data, size_t frames);

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char**)data = g_sysdir;
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char**)data = g_sysdir;
         return true;
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback*)data)->log = log_cb;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return *(const enum retro_pixel_format*)data
            == RETRO_PIXEL_FORMAT_XRGB8888;
      case RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT:
         if (!getenv("AUDIO_FLOAT"))
            return false;
         ((struct retro_audio_sample_float_callback*)data)->batch =
            audio_batch_float_cb;
         return true;
      case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
         *(unsigned*)data = 1;
         return true;
      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_MESSAGE:
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool*)data = false;
         return true;
      default:
         break;
   }
   return false;
}

/* FNV-1a over every frame, so the video output has one comparable
 * number. Deterministic: nothing off-thread reaches the framebuffer. */
static uint64_t g_video_hash = 1469598103934665603ULL;

static int g_nohash;

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   const uint8_t *p = (const uint8_t*)data;
   unsigned y, x;
   if (!p || g_nohash)
      return;
   /* Touch every pixel so an out-of-bounds framebuffer is caught. */
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         uint32_t px = ((const uint32_t*)(p + y * pitch))[x];
         g_video_acc  += px;
         g_video_hash ^= px;
         g_video_hash *= 1099511628211ULL;
      }
}

static void audio_sample_cb(int16_t l, int16_t r)
{
   g_audio_frames++;
   g_audio_acc += (uint16_t)l + (uint16_t)r;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
   size_t i;
   for (i = 0; i < frames * 2; i++)
      g_audio_acc += (uint16_t)data[i];
   g_audio_frames += frames;
   return frames;
}

static size_t audio_batch_float_cb(const float *data, size_t frames)
{
   size_t i;
   for (i = 0; i < frames * 2; i++)
      g_audio_acc += (uint32_t)(int32_t)(data[i] * 32767.0f);
   g_audio_frames += frames;
   return frames;
}

static void input_poll_cb(void) { }

static int16_t input_state_cb(unsigned port, unsigned device,
      unsigned index, unsigned id)
{
   (void)index;
   if (port != 0 || device != RETRO_DEVICE_JOYPAD)
      return 0;
   /* Scripted walk: hold a direction for a while, tap A, repeat. */
   switch (id)
   {
      case RETRO_DEVICE_ID_JOYPAD_RIGHT:
         return (g_cur_frame % 240) < 40;
      case RETRO_DEVICE_ID_JOYPAD_DOWN:
         return (g_cur_frame % 240) >= 60 && (g_cur_frame % 240) < 100;
      case RETRO_DEVICE_ID_JOYPAD_LEFT:
         return (g_cur_frame % 240) >= 120 && (g_cur_frame % 240) < 160;
      case RETRO_DEVICE_ID_JOYPAD_UP:
         return (g_cur_frame % 240) >= 180 && (g_cur_frame % 240) < 220;
      case RETRO_DEVICE_ID_JOYPAD_A:
         return (g_cur_frame % 47) == 0;
      case RETRO_DEVICE_ID_JOYPAD_START:
         return (g_cur_frame % 613) == 0;
      default:
         break;
   }
   return 0;
}

#define SYM(x) do { \
   *(void**)(&x) = dlsym(g_lib, #x); \
   if (!x) { fprintf(stderr, "missing %s\n", #x); return 1; } \
} while (0)

int main(int argc, char **argv)
{
   void (*retro_init)(void);
   void (*retro_deinit)(void);
   void (*retro_set_environment)(retro_environment_t);
   void (*retro_set_video_refresh)(retro_video_refresh_t);
   void (*retro_set_audio_sample)(retro_audio_sample_t);
   void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*retro_set_input_poll)(retro_input_poll_t);
   void (*retro_set_input_state)(retro_input_state_t);
   void (*retro_get_system_info)(struct retro_system_info*);
   void (*retro_get_system_av_info)(struct retro_system_av_info*);
   bool (*retro_load_game)(const struct retro_game_info*);
   void (*retro_unload_game)(void);
   void (*retro_run)(void);
   void (*retro_reset)(void);
   void  *(*retro_get_memory_data)(unsigned);
   size_t (*retro_get_memory_size)(unsigned);
   size_t (*retro_serialize_size)(void);
   bool (*retro_serialize)(void*, size_t);
   bool (*retro_unserialize)(const void*, size_t);

   struct retro_system_info sys;
   struct retro_system_av_info av;
   unsigned i;
   void *state = NULL;
   size_t state_sz;

   if (argc < 4)
   {
      fprintf(stderr, "usage: %s <core.so> <system-dir> <frames>\n", argv[0]);
      return 1;
   }

   g_nohash = getenv("NOHASH") != NULL;
   g_sysdir = argv[2];
   g_frames = (unsigned)strtoul(argv[3], NULL, 10);

   g_lib = dlopen(argv[1], RTLD_NOW | RTLD_NODELETE);
   if (!g_lib)
   {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }

   SYM(retro_init);
   SYM(retro_deinit);
   SYM(retro_set_environment);
   SYM(retro_set_video_refresh);
   SYM(retro_set_audio_sample);
   SYM(retro_set_audio_sample_batch);
   SYM(retro_set_input_poll);
   SYM(retro_set_input_state);
   SYM(retro_get_system_info);
   SYM(retro_get_system_av_info);
   SYM(retro_load_game);
   SYM(retro_unload_game);
   SYM(retro_run);
   SYM(retro_reset);
   SYM(retro_get_memory_data);
   SYM(retro_get_memory_size);
   SYM(retro_serialize_size);
   SYM(retro_serialize);
   SYM(retro_unserialize);

   retro_get_system_info(&sys);
   fprintf(stderr, "core: %s %s\n", sys.library_name, sys.library_version);

   retro_set_environment(env_cb);
   retro_init();
   retro_set_video_refresh(video_cb);
   retro_set_audio_sample(audio_sample_cb);
   retro_set_audio_sample_batch(audio_batch_cb);
   retro_set_input_poll(input_poll_cb);
   retro_set_input_state(input_state_cb);

 { unsigned cyc, ncyc = (unsigned)strtoul(getenv("CYCLES") ? getenv("CYCLES") : "1", NULL, 10);
   for (cyc = 0; cyc + 1 < ncyc; cyc++)
   {
      unsigned k;
      if (!retro_load_game(NULL))
      {
         fprintf(stderr, "retro_load_game failed (cycle %u)\n", cyc);
         return 1;
      }
      for (k = 0; k < 120; k++) { g_cur_frame = k; retro_run(); }
      retro_unload_game();
   } }

   if (!retro_load_game(NULL))
   {
      fprintf(stderr, "retro_load_game failed\n");
      return 1;
   }

   /* Inject a save before the first frame, so the parse path runs on
    * something other than an empty buffer. retro_reset is what makes the
    * game re-read it: it copies SRAM out, reloads, and copies back. */
   if (getenv("SRAM_LOAD"))
   {
      FILE *f = fopen(getenv("SRAM_LOAD"), "rb");

      if (f)
      {
         void  *sram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
         size_t len  = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

         if (sram && len)
         {
            size_t got = fread(sram, 1, len, f);
            (void)got;
            retro_reset();
         }
         fclose(f);
      }
   }

   retro_get_system_av_info(&av);
   fprintf(stderr, "av: %ux%u @ %.2f, %.1f Hz\n",
         (unsigned)av.geometry.base_width, (unsigned)av.geometry.base_height,
         av.timing.fps, av.timing.sample_rate);

   {
   struct timespec t0, t1;
   double run_ns = 0.0;
   for (i = 0; i < g_frames; i++)
   {
      g_cur_frame = i;
      clock_gettime(CLOCK_MONOTONIC, &t0);
      retro_run();
      clock_gettime(CLOCK_MONOTONIC, &t1);
      run_ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);

      if (i == g_frames / 3)
      {
         state_sz = retro_serialize_size();
         if (state_sz)
         {
            state = malloc(state_sz);
            if (state && !retro_serialize(state, state_sz))
               fprintf(stderr, "retro_serialize failed\n");
         }
      }
      if (i == (2 * g_frames) / 3 && state)
      {
         if (!retro_unserialize(state, state_sz))
            fprintf(stderr, "retro_unserialize failed\n");
      }
      if (i == g_frames - 20)
         retro_reset();
   }
   fprintf(stderr, "retro_run: %.1f us/frame over %u frames\n",
         run_ns / 1000.0 / g_frames, g_frames);
   }

   free(state);

   /* The save lives in SRAM, not in the serialize entry points, so it
    * is invisible to a run that only checks frames. Hash it: a change to
    * the save format or to what the game records shows up here and
    * nowhere else. */
   {
      const unsigned char *sram = (const unsigned char*)
         retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
      size_t   len  = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
      uint64_t hash = 1469598103934665603ULL;
      size_t   i;

      for (i = 0; sram && i < len; i++)
      {
         hash ^= sram[i];
         hash *= 1099511628211ULL;
      }

      fprintf(stderr, "sram: %llu bytes, %016llx\n",
            (unsigned long long)len, (unsigned long long)hash);

      if (getenv("SRAM_DUMP") && sram)
      {
         FILE *f = fopen(getenv("SRAM_DUMP"), "wb");
         if (f)
         {
            fwrite(sram, 1, len, f);
            fclose(f);
         }
      }
   }

   retro_unload_game();
   retro_deinit();
   /* keep loaded so LSan can symbolise */

   fprintf(stderr, "ok: %u frames, video %016llx, %llu audio frames, audio acc %llu\n",
         g_frames, (unsigned long long)g_video_hash,
         (unsigned long long)g_audio_frames,
         (unsigned long long)g_audio_acc);
   return 0;
}
