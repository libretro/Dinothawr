#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/msvc.h>
#include <file/file_path.h>
#include <streams/file_stream.h>

#include "libretro.h"
#include "libretro_core_options.h"

#include "audio/game_audio.h"
#include "audio/mixer_f32.h"
#include "audio/mixer_i16.h"
#include "blit_surface_cache.h"
#include "icy_game.h"
#include "icy_input.h"
#include "icy_manager.h"
#include "icy_path.h"
#include "icy_rate.h"
#include "icy_save.h"

static icy_manager_t *game;
static char game_path[1024];
static char game_path_dir[1024];

static mixer_f32_t *mixer_f32 = NULL;
/* Integer audio backend, created only when the frontend does not
 * advertise float output. When live, all SFX/BG audio flows through this
 * deterministic int16 mixer instead of the float one. */
static mixer_i16_t *mixer_i16 = NULL;
static bool s_audio_float = false;

/* No lock hooks: audio is rendered from retro_run on the same thread
 * that mutates the mixer, and the audio decode jobs never reach into it.
 */

retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
/* Set if the frontend supports float audio output (negotiated via
 * RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT). Dinothawr's mixer is
 * float-native, so when this is available we hand samples over as float
 * and skip the float->int16 squash entirely. */
static retro_audio_sample_batch_float_t audio_batch_float_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/* Rate the frontend drives retro_run at, and therefore the rate the
 * simulation is stepped at. See icy_rate.h. */
static double   g_framerate = 60.0;


#define AUDIO_SAMPLE_RATE 44100
/* One audio block is emitted per emulated frame; its size (samples per
 * frame) is AUDIO_SAMPLE_RATE / framerate, so the delivered rate stays
 * AUDIO_SAMPLE_RATE for any frame rate. Buffers are sized for the lowest
 * frame rate we clamp to (30). */
#define AUDIO_MAX_FRAMES 2048
static unsigned audio_frames = AUDIO_SAMPLE_RATE / 60;
static int16_t  audio_buffer  [2 * AUDIO_MAX_FRAMES];
static float    audio_buffer_f[2 * AUDIO_MAX_FRAMES];
static bool     s_av_info_queried = false;

static void check_system_specs(void)
{
   /* TODO: ballpark average. */
   unsigned level = 4;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

/* The C audio managers reach the mixers through these. */
int icy_audio_is_float(void) { return s_audio_float; }
mixer_f32_t *icy_mixer_f32(void) { return mixer_f32; }
mixer_i16_t *icy_mixer_i16(void) { return mixer_i16; }

void retro_init(void)
{
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;
   check_system_specs();
}

void retro_deinit(void)
{
   /* The surface cache holds every decoded image for the session. */
   blit_surface_cache_free();
   icy_game_audio_free();
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Dinothawr";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "v1.0" GIT_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = "game";
}

static void get_av_info(struct retro_system_av_info *info)
{
   unsigned width  = ICY_GAME_FB_WIDTH;
   unsigned height = ICY_GAME_FB_HEIGHT;

   info->timing.fps          = g_framerate;
   info->timing.sample_rate  = (double)AUDIO_SAMPLE_RATE;
   info->geometry.base_width   = width;
   info->geometry.base_height  = height;
   info->geometry.max_width    = width;
   info->geometry.max_height   = height;
   info->geometry.aspect_ratio = 0.0f;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   get_av_info(info);
   s_av_info_queried = true;
}


void retro_set_environment(retro_environment_t cb)
{
   bool no_content = true;
   struct retro_vfs_interface_info vfs_iface_info;

   environ_cb = cb;
   libretro_set_core_options(environ_cb);
   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

   /* Hand the frontend's VFS to libretro-common, which is what every
    * read in this core now goes through: the asset loaders (rpng via
    * image_transfer, the WAV and Ogg decoders via audio_transfer) all
    * sit on data_transfer, which reads through filestream, and the
    * existence check in retro_load_game goes through path_is_valid.
    * Without this they fall back to the built-in stdio implementation
    * and paths only the frontend can resolve - archive members,
    * Android content:// documents, frontend overrides - do not open. */
   /* Ask for the newest interface each consumer can use and step down
    * until the frontend agrees: filestream needs 2, path_is_valid's stat
    * needs 3, and 4 buys the 64-bit stat.  The frontend refuses outright
    * rather than negotiating, so the descent is ours to walk - and each
    * init re-checks the version it was handed, so a version 2 result
    * wires up filestream and correctly leaves path on its fallback. */
   {
      static const unsigned wanted[] = { 4, 3, 2 };
      unsigned i;

      for (i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++)
      {
         vfs_iface_info.required_interface_version = wanted[i];
         vfs_iface_info.iface                      = NULL;

         if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE,
                  &vfs_iface_info))
         {
            filestream_vfs_init(&vfs_iface_info);
            path_vfs_init(&vfs_iface_info);
            break;
         }
      }
   }
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static void audio_callback(void)
{
   unsigned n = audio_frames;
   unsigned i;

   /* Float-native path: the mixer renders float, so when the frontend
    * accepts float we push it straight through - no float->int16 step. */
   if (audio_batch_float_cb)
   {
      mixer_f32_render(mixer_f32, audio_buffer_f, n);

      for (i = 0; i < n; )
         i += audio_batch_float_cb(audio_buffer_f + 2 * i, n - i);
      return;
   }

   mixer_i16_render(mixer_i16, audio_buffer, n);

   for (i = 0; i < n; )
      i += audio_batch_cb(audio_buffer + 2 * i, n - i);
}

/* Resolve the configured frame rate. "Auto" follows the frontend's target
 * refresh rate; otherwise the literal value is used. Clamped to the range
 * the audio buffers are sized for. */
static double resolve_framerate(void)
{
   double fps;
   struct retro_variable var;

   var.key   = "dino_framerate";
   var.value = NULL;
   fps = 60.0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Auto"))
      {
         float target = 0.0f;
         if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &target)
               && target >= 1.0f)
            fps = target;
      }
      else
         fps = atof(var.value);
   }

   if (fps < 30.0)
      fps = 30.0;
   else if (fps > 360.0)
      fps = 360.0;
   return fps;
}

/* Apply the configured frame rate. The frontend drives retro_run at this
 * rate and the core runs exactly one tick per call, so timing.fps is the
 * game rate; audio_frames keeps delivered audio at AUDIO_SAMPLE_RATE. If it
 * changes after the frontend has read the av info (a live option change),
 * push the new timing with SET_SYSTEM_AV_INFO. */
static void apply_framerate(void)
{
   double fps = resolve_framerate();
   if (fps == g_framerate)
      return;

   g_framerate  = fps;
   icy_rate_set(fps);
   audio_frames = (unsigned)(AUDIO_SAMPLE_RATE / fps + 0.5);
   if (audio_frames < 1)
      audio_frames = 1;
   else if (audio_frames > AUDIO_MAX_FRAMES)
      audio_frames = AUDIO_MAX_FRAMES;

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "Dinothawr: frame rate %.4g Hz (%u samples/frame).\n", fps, audio_frames);

   if (s_av_info_queried && game)
   {
      struct retro_system_av_info info;
      get_av_info(&info);
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
   }
}

static void update_variables()
{
   apply_framerate();
}

static void check_variables()
{
   bool update = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &update) && update)
      update_variables();
}

static void show_message(const char *text);

void retro_run(void)
{
   check_variables();

   input_poll_cb();

   /* Standard libretro pacing: one tick and one audio block per call. The
    * frontend drives retro_run at timing.fps, so the game advances at the
    * rate the "Frame rate" option selects. Fast-forward just calls
    * retro_run more often, which speeds the audio up too - the old async
    * audio callback pulled samples at the device rate regardless of
    * emulation speed, so fast-forward never sped up sound. */
   /* Same reason retro_load_game catches: an exception leaving a
    * libretro entry point terminates the frontend. The ones that can
    * reach here are assertions about level data - a sprite face that
    * does not exist, goal squares and blocks that do not match - so a
    * bad level would take the frontend down mid-play. Report and ask to
    * be unloaded instead, which loses the session and nothing else. */
   if (!icy_manager_iterate(game))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Dinothawr: %s\n",
               icy_manager_error(game));

      show_message("Dinothawr: level data is broken, stopping");
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
      return;
   }

   icy_bgm_step(icy_bgm());
   audio_callback();

   if (icy_manager_done(game))
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

/* Free functions rather than lambdas: both only ever read the frontend
 * callbacks, which are file-scope, so the captures were empty and the
 * std::function they were stored in outlived the scope that made them. */
static int poll_input(void *ctx, enum icy_input input)
{
   unsigned btn;
   (void)ctx;

   switch (input)
   {
      case ICY_INPUT_UP: btn = RETRO_DEVICE_ID_JOYPAD_UP; break;
      case ICY_INPUT_DOWN: btn = RETRO_DEVICE_ID_JOYPAD_DOWN; break;
      case ICY_INPUT_LEFT: btn = RETRO_DEVICE_ID_JOYPAD_LEFT; break;
      case ICY_INPUT_RIGHT: btn = RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
      case ICY_INPUT_PUSH: btn = RETRO_DEVICE_ID_JOYPAD_B; break;
      case ICY_INPUT_MENU: btn = RETRO_DEVICE_ID_JOYPAD_A; break;
      case ICY_INPUT_RESET: btn = RETRO_DEVICE_ID_JOYPAD_X; break;
      default: return 0;
   }

   return input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, btn) != 0;
}

static void refresh_video(void *ctx, const void *data, unsigned width,
      unsigned height, size_t pitch)
{
   (void)ctx;
   video_cb(data, width, height, pitch);
}

/* Non-zero on success; @error, if given, receives the reason. */
static int load_game(const char *path, char *error, size_t error_len)
{
   icy_manager_free(game);
   game = icy_manager_new(path, poll_input, NULL, refresh_video, NULL,
         error, error_len);

   return game != NULL;
}

/* Reload, keeping the save: the manager is rebuilt from the .game, so
 * the SRAM has to be carried across by hand. */
void retro_reset(void)
{
   icy_save_t saved;
   size_t     size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
   void      *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);

   if (size > sizeof(saved.data))
      size = sizeof(saved.data);

   if (data)
      memcpy(saved.data, data, size);

   {
      char err[256];

      if (!load_game(game_path, err, sizeof(err)))
      {
         if (log_cb)
            log_cb(RETRO_LOG_ERROR, "Dinothawr: reset failed: %s\n", err);
         return;
      }
   }

   if ((data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)))
      memcpy(data, saved.data, size);
}

/* Both message interfaces, so a failure is visible on old frontends
 * too. */
static void show_message(const char *text)
{
   unsigned msg_interface_version = 0;

   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION,
         &msg_interface_version);

   if (msg_interface_version >= 1)
   {
      struct retro_message_ext msg;

      msg.msg      = text;
      msg.duration = 3000;
      msg.priority = 3;
      msg.level    = RETRO_LOG_ERROR;
      msg.target   = RETRO_MESSAGE_TARGET_ALL;
      msg.type     = RETRO_MESSAGE_TYPE_NOTIFICATION;
      msg.progress = -1;

      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
   }
   else
   {
      struct retro_message msg;

      msg.msg    = text;
      msg.frames = 180;

      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
   }
}

bool retro_load_game(const struct retro_game_info* info)
{
   enum retro_pixel_format fmt;
   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Push" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Menu" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Reset" },

      { 0 },
   };

   /* The core sets SET_SUPPORT_NO_GAME, and a frontend starting it with
    * no content may hand over either a NULL info or a zeroed one - the
    * second of which used to reach std::string(NULL) and segfault.
    * Both mean the same thing: fall through to the system directory. */
   if (info && info->path)
   {
      snprintf(game_path, sizeof(game_path), "%s", info->path);
      icy_path_dir(game_path_dir, sizeof(game_path_dir), game_path);
   }
   else
   {
      const char *system_dir = NULL;
      bool game_file_exists  = false;

      /* Get system directory */
      if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) &&
          system_dir)
      {
         icy_path_join(game_path_dir, sizeof(game_path_dir), system_dir,
               "dinothawr");
         icy_path_join(game_path, sizeof(game_path), game_path_dir,
               "dinothawr.game");

         /* path_is_valid stats through the frontend's VFS, so a system
          * directory the frontend can reach but stdio cannot still
          * resolves - and opening the file only to close it again was
          * never what this wanted to ask. */
         game_file_exists = path_is_valid(game_path);
      }

      if (!game_file_exists)
      {
         unsigned msg_interface_version = 0;
         environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION,
               &msg_interface_version);

         if (msg_interface_version >= 1)
         {
            struct retro_message_ext msg = {
               "Dinothawr game files missing from frontend system directory",
               3000,
               3,
               RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL,
               RETRO_MESSAGE_TYPE_NOTIFICATION,
               -1
            };
            environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
         }
         else
         {
            struct retro_message msg = {
               "Dinothawr game files missing from frontend system directory",
               180
            };
            environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
         }

         return false;
      }
   }

   /* Negotiate float audio output. If the frontend supports it, the
    * mixer's float output is delivered directly; otherwise we run the
    * deterministic int16 pipeline (int16 mixer + int16 SFX/BG decode). */
   {
      struct retro_audio_sample_float_callback float_cb;
      float_cb.batch = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT, &float_cb)
            && float_cb.batch)
         audio_batch_float_cb = float_cb.batch;
      else
         audio_batch_float_cb = NULL;

      s_audio_float = (audio_batch_float_cb != NULL);

      /* (Re)create whichever mixer this game will use. Freeing any
       * previous instance also tears down its streams; the managers keep
       * their decoded buffers separately, so this is safe on reload. */
      if (mixer_f32)
      {
         mixer_f32_free(mixer_f32);
         mixer_f32 = NULL;
      }
      if (mixer_i16)
      {
         mixer_i16_free(mixer_i16);
         mixer_i16 = NULL;
      }

      if (s_audio_float)
         mixer_f32 = mixer_f32_new();
      else
         mixer_i16 = mixer_i16_new();
   }

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   /* Every failure below the frontend boundary is an exception, and
    * nothing between here and it catches. Letting one out of
    * retro_load_game terminates the frontend rather than failing the
    * load, so this is where they stop: report and return false, which
    * is what a core is supposed to do with content it cannot read. */
   {
      char err[256];

      if (!load_game(game_path, err, sizeof(err)))
      {
         if (log_cb)
            log_cb(RETRO_LOG_ERROR, "Dinothawr: failed to load: %s\n", err);

         show_message("Dinothawr: failed to load game files");
         return false;
      }
   }

   /* Audio is now produced synchronously from retro_run (no async audio
    * callback), so the mixers are simply always enabled while a game is
    * loaded - the audio_set_state enable/disable handshake is gone. */
   mixer_f32_set_enabled(mixer_f32, true); /* both are NULL-safe: only */
   mixer_i16_set_enabled(mixer_i16, true); /* the live one is non-NULL */

   fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

   update_variables();
   return true;
}

bool retro_load_game_special(unsigned type,
      const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

void retro_unload_game(void)
{
   icy_manager_free(game);
   game = NULL;

   /* Before the mixer goes away: the music slot points into it, and a
    * decode may still be in flight holding a buffer nothing else can
    * reach. */
   icy_bgm_stop(icy_bgm());

   if (mixer_f32)
   {
      mixer_f32_free(mixer_f32);
      mixer_f32 = NULL;
   }
   if (mixer_i16)
   {
      mixer_i16_free(mixer_i16);
      mixer_i16 = NULL;
   }
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void* retro_get_memory_data(unsigned id)
{
   if (id != RETRO_MEMORY_SAVE_RAM)
      return NULL;

   return icy_manager_save_data(game);
}

size_t retro_get_memory_size(unsigned id)
{
   if (id != RETRO_MEMORY_SAVE_RAM)
      return 0;

   return icy_manager_save_size(game);
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;}

