/* Dinothawr - the game's two audio managers (implementation).
 * MSVC C89. See game_audio.h. */

#include "game_audio.h"

#include <stdlib.h>
#include <string.h>

#include "async_job.h"
#include "mixer_f32.h"
#include "mixer_i16.h"
#include "vorbis_f32.h"
#include "vorbis_i16.h"

#include "../blit_str_map.h"

/* ---- Sound effects --------------------------------------------------- */

struct icy_sfx
{
   /* One map either way: only one mixer is live, so a sample is decoded
    * in that type and nothing else. Values are f32_buf_t* or i16_buf_t*
    * accordingly. */
   blit_str_map_t *effects;
   int             is_float;
};

static icy_sfx_t *g_sfx;
static icy_bgm_t *g_bgm;

icy_sfx_t *icy_sfx(void)
{
   if (g_sfx)
      return g_sfx;

   if (!(g_sfx = (icy_sfx_t*)calloc(1, sizeof(*g_sfx))))
      return NULL;

   if (!(g_sfx->effects = blit_str_map_new()))
   {
      free(g_sfx);
      g_sfx = NULL;
      return NULL;
   }

   g_sfx->is_float = icy_audio_is_float();
   return g_sfx;
}

static void sfx_release_value(int is_float, void *value)
{
   if (!value)
      return;

   if (is_float)
      f32_buf_unref((f32_buf_t*)value);
   else
      i16_buf_unref((i16_buf_t*)value);
}

int icy_sfx_add(icy_sfx_t *sfx, const char *ident, const char *path)
{
   void *buf = NULL;
   void *old = NULL;
   int   replaced = 0;

   if (!sfx)
      return 0;

   /* The mixer is chosen per game, and add runs after that choice. */
   sfx->is_float = icy_audio_is_float();

   if (sfx->is_float)
   {
      size_t  samples = 0;
      float  *pcm     = wav_f32_load(path, &samples);

      if (pcm && !(buf = f32_buf_new(pcm, samples)))
         free(pcm);
   }
   else
   {
      size_t   samples = 0;
      int16_t *pcm     = wav_i16_load(path, &samples);

      if (pcm && !(buf = i16_buf_new(pcm, samples)))
         free(pcm);
   }

   if (!buf)
      return 0;

   if (!blit_str_map_set(sfx->effects, ident, buf, &old, &replaced))
   {
      sfx_release_value(sfx->is_float, buf);
      return 0;
   }

   if (replaced)
      sfx_release_value(sfx->is_float, old);

   return 1;
}

void icy_sfx_play(const icy_sfx_t *sfx, const char *ident, float volume)
{
   void *buf;

   if (!sfx || !(buf = blit_str_map_find(sfx->effects, ident)))
      return;

   if (sfx->is_float)
   {
      mixer_f32_t  *mixer = icy_mixer_f32();
      f32_stream_t *stream;

      if (!mixer || !mixer_f32_enabled(mixer))
         return;

      if ((stream = f32_pcm_stream_new((f32_buf_t*)buf, 0, volume)))
         mixer_f32_add(mixer, stream);
   }
   else
   {
      mixer_i16_t  *mixer = icy_mixer_i16();
      i16_stream_t *stream;

      if (!mixer || !mixer_i16_enabled(mixer))
         return;

      if ((stream = i16_pcm_stream_new((i16_buf_t*)buf, 0,
                  mixer_i16_q15_from_float(volume))))
         mixer_i16_add(mixer, stream);
   }
}

/* ---- Background music ------------------------------------------------ */

typedef struct
{
   char  *path;
   float  gain;
} icy_track_t;

struct icy_bgm
{
   icy_track_t *tracks;
   size_t       count;

   int          first;
   unsigned     last;

   /* Private shuffle state. This used to be srand()/rand(): a core
    * reseeding the global C PRNG walks over whatever the frontend or
    * another statically linked core had set up, and gets walked over in
    * turn. */
   uint32_t     rng_state;

   /* The next track is decoded off-thread so a track change does not
    * stall the game. The job outlives step(), and job_path is the string
    * it reads on its own thread, so both live here. */
   async_job_t *job;
   char        *job_path;
   int          job_is_float;
};

icy_bgm_t *icy_bgm(void)
{
   if (!g_bgm)
      g_bgm = (icy_bgm_t*)calloc(1, sizeof(*g_bgm));
   return g_bgm;
}

static void bgm_clear_tracks(icy_bgm_t *bgm)
{
   size_t i;

   for (i = 0; i < bgm->count; i++)
      free(bgm->tracks[i].path);

   free(bgm->tracks);
   bgm->tracks = NULL;
   bgm->count  = 0;
}

void icy_bgm_stop(icy_bgm_t *bgm)
{
   if (!bgm)
      return;

   /* The buffer a decode in flight is producing is reachable only
    * through the job, so it has to be collected here or it is lost.
    * Collecting waits for the decode to finish, which is bounded by one
    * track and is what a reset already does. */
   if (bgm->job)
   {
      void *buf = async_job_collect(bgm->job);

      if (bgm->job_is_float)
         f32_buf_unref((f32_buf_t*)buf);
      else
         i16_buf_unref((i16_buf_t*)buf);

      bgm->job = NULL;
   }

   free(bgm->job_path);
   bgm->job_path = NULL;

   if (icy_audio_is_float())
   {
      mixer_f32_t *m = icy_mixer_f32();
      if (m)
         mixer_f32_set_music(m, NULL);
   }
   else
   {
      mixer_i16_t *m = icy_mixer_i16();
      if (m)
         mixer_i16_set_music(m, NULL);
   }
}

int icy_bgm_set_tracks(icy_bgm_t *bgm, const char *const *paths,
      const float *gains, size_t count)
{
   size_t i;

   if (!bgm)
      return 0;

   icy_bgm_stop(bgm);
   bgm_clear_tracks(bgm);

   if (count && !(bgm->tracks = (icy_track_t*)calloc(count,
               sizeof(*bgm->tracks))))
      return 0;

   for (i = 0; i < count; i++)
   {
      size_t len = strlen(paths[i]);

      if (!(bgm->tracks[i].path = (char*)malloc(len + 1)))
         return 0;

      memcpy(bgm->tracks[i].path, paths[i], len + 1);
      bgm->tracks[i].gain = gains ? gains[i] : 1.0f;
      bgm->count++;
   }

   /* Seeded from the track list, not from a clock. A wall-clock seed
    * makes a session unreproducible for no gain the player can hear -
    * the order is arbitrary either way. Hashing the paths keeps a
    * different game shuffling differently while making one game's order
    * a function of its content. Zero is the one state xorshift cannot
    * leave, so it is steered away from. */
   bgm->rng_state = 2166136261u;
   for (i = 0; i < bgm->count; i++)
   {
      const char *c;
      for (c = bgm->tracks[i].path; *c; c++)
         bgm->rng_state = (bgm->rng_state ^ (uint32_t)(unsigned char)*c)
            * 16777619u;
   }
   if (!bgm->rng_state)
      bgm->rng_state = 0x9e3779b9u;

   bgm->first = 1;
   bgm->last  = 0;
   return 1;
}

static unsigned bgm_rng_next(icy_bgm_t *bgm, unsigned n)
{
   bgm->rng_state ^= bgm->rng_state << 13;
   bgm->rng_state ^= bgm->rng_state >> 17;
   bgm->rng_state ^= bgm->rng_state << 5;
   return n ? (bgm->rng_state % n) : 0;
}

/* The first track is the playlist's own opener; after that, any track
 * but the one that just played. */
static unsigned bgm_next_index(icy_bgm_t *bgm)
{
   if (bgm->first)
   {
      bgm->first = 0;
      bgm->last  = 0;
      return 0;
   }

   if (bgm->count > 1)
   {
      unsigned index = 1 + bgm_rng_next(bgm, (unsigned)bgm->count - 1);
      bgm->last = (bgm->last + index) % (unsigned)bgm->count;
   }

   return bgm->last;
}

static void *bgm_decode_f32(void *userdata)
{
   return vorbis_f32_decode_file((const char*)userdata);
}

static void *bgm_decode_i16(void *userdata)
{
   return vorbis_i16_decode_file((const char*)userdata);
}

void icy_bgm_step(icy_bgm_t *bgm)
{
   int is_float;

   if (!bgm || !bgm->count)
      return;

   is_float = icy_audio_is_float();

   if (is_float)
   {
      mixer_f32_t *m = icy_mixer_f32();

      if (!m || mixer_f32_music_active(m))
         return;
   }
   else
   {
      mixer_i16_t *m = icy_mixer_i16();

      if (!m || mixer_i16_music_active(m))
         return;
   }

   if (!bgm->job)
   {
      unsigned    index = bgm_next_index(bgm);
      const char *path  = bgm->tracks[index].path;
      size_t      len   = strlen(path);

      free(bgm->job_path);
      if (!(bgm->job_path = (char*)malloc(len + 1)))
         return;
      memcpy(bgm->job_path, path, len + 1);

      bgm->job_is_float = is_float;
      bgm->job = async_job_start(is_float ? bgm_decode_f32
            : bgm_decode_i16, bgm->job_path);

      /* No thread available - decode inline rather than go silent. A
       * track change is the only place this can be reached and one track
       * is the whole cost. */
      if (!bgm->job)
      {
         void *buf = is_float ? bgm_decode_f32(bgm->job_path)
            : bgm_decode_i16(bgm->job_path);

         if (!buf)
            return;

         if (is_float)
         {
            mixer_f32_set_music(icy_mixer_f32(),
                  f32_pcm_stream_new((f32_buf_t*)buf, 0,
                     bgm->tracks[bgm->last].gain));
            f32_buf_unref((f32_buf_t*)buf);
         }
         else
         {
            mixer_i16_set_music(icy_mixer_i16(),
                  i16_pcm_stream_new((i16_buf_t*)buf, 0,
                     mixer_i16_q15_from_float(bgm->tracks[bgm->last].gain)));
            i16_buf_unref((i16_buf_t*)buf);
         }
         return;
      }
   }

   if (async_job_ready(bgm->job))
   {
      void *buf = async_job_collect(bgm->job);

      bgm->job = NULL;

      if (!buf)
         return;

      if (bgm->job_is_float)
      {
         mixer_f32_set_music(icy_mixer_f32(),
               f32_pcm_stream_new((f32_buf_t*)buf, 0,
                  bgm->tracks[bgm->last].gain));
         f32_buf_unref((f32_buf_t*)buf);
      }
      else
      {
         mixer_i16_set_music(icy_mixer_i16(),
               i16_pcm_stream_new((i16_buf_t*)buf, 0,
                  mixer_i16_q15_from_float(bgm->tracks[bgm->last].gain)));
         i16_buf_unref((i16_buf_t*)buf);
      }
   }
}

void icy_game_audio_free(void)
{
   if (g_sfx)
   {
      size_t i;

      for (i = 0; i < blit_str_map_count(g_sfx->effects); i++)
         sfx_release_value(g_sfx->is_float,
               blit_str_map_value_at(g_sfx->effects, i));

      blit_str_map_free(g_sfx->effects);
      free(g_sfx);
      g_sfx = NULL;
   }

   if (g_bgm)
   {
      icy_bgm_stop(g_bgm);
      bgm_clear_tracks(g_bgm);
      free(g_bgm);
      g_bgm = NULL;
   }
}
