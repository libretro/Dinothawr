/* Dinothawr - float audio mixer (implementation).
 * MSVC C89. See mixer_f32.h for the contract. */

#include "mixer_f32.h"

#include <stdlib.h>
#include <string.h>

#include <audio/audio_mix.h>
#include <retro_inline.h>

/* ------------------------------------------------------------------- *
 * Reference-counted shared PCM buffer
 * ------------------------------------------------------------------- */

struct f32_buf
{
   float *data;
   size_t samples; /* total float count (frames * MIXER_F32_CHANNELS) */
   int    refcount;
};

f32_buf_t *f32_buf_new(float *data, size_t samples)
{
   f32_buf_t *buf = (f32_buf_t*)malloc(sizeof(*buf));

   if (!buf)
      return NULL;

   buf->data     = data;
   buf->samples  = samples;
   buf->refcount = 1;
   return buf;
}

f32_buf_t *f32_buf_ref(f32_buf_t *buf)
{
   if (buf)
      buf->refcount++;
   return buf;
}

void f32_buf_unref(f32_buf_t *buf)
{
   if (!buf)
      return;

   if (--buf->refcount > 0)
      return;

   if (buf->data)
      free(buf->data);
   free(buf);
}

/* ------------------------------------------------------------------- *
 * PCM stream (plays a shared f32_buf)
 * ------------------------------------------------------------------- */

typedef struct
{
   f32_stream_t base;
   f32_buf_t   *buf;
   size_t       ptr; /* read cursor, in float samples */
} f32_pcm_t;

static size_t f32_pcm_render(f32_stream_t *s, float *out, size_t frames)
{
   f32_pcm_t *p    = (f32_pcm_t*)s;
   size_t     want = frames * MIXER_F32_CHANNELS;
   size_t     done = 0;

   if (p->buf->samples == 0)
      return 0;

   /* Fill the whole block. When looping, wrap as many times as the clip
    * length requires (so even a sub-block-length loop stays continuous);
    * when not looping, stop at the end and let the mixer pad with
    * silence. */
   while (done < want)
   {
      size_t avail = p->buf->samples - p->ptr;
      size_t chunk;

      if (avail == 0)
      {
         if (!s->loop)
            break;
         p->ptr = 0;
         avail  = p->buf->samples;
      }

      chunk = want - done;
      if (chunk > avail)
         chunk = avail;

      memcpy(out + done, p->buf->data + p->ptr, chunk * sizeof(float));
      p->ptr += chunk;
      done   += chunk;
   }

   return done / MIXER_F32_CHANNELS;
}

static int f32_pcm_valid(const f32_stream_t *s)
{
   const f32_pcm_t *p = (const f32_pcm_t*)s;
   if (s->loop)
      return 1;
   return p->ptr < p->buf->samples;
}

static void f32_pcm_rewind(f32_stream_t *s)
{
   f32_pcm_t *p = (f32_pcm_t*)s;
   p->ptr = 0;
}

static void f32_pcm_destroy(f32_stream_t *s)
{
   f32_pcm_t *p = (f32_pcm_t*)s;
   if (!p)
      return;
   f32_buf_unref(p->buf);
   free(p);
}

f32_stream_t *f32_pcm_stream_new(f32_buf_t *buf, int loop, float volume)
{
   f32_pcm_t *p;

   if (!buf)
      return NULL;

   p = (f32_pcm_t*)malloc(sizeof(*p));
   if (!p)
      return NULL;

   p->base.render  = f32_pcm_render;
   p->base.valid   = f32_pcm_valid;
   p->base.rewind  = f32_pcm_rewind;
   p->base.destroy = f32_pcm_destroy;
   p->base.volume  = volume;
   p->base.loop    = loop;
   p->buf          = f32_buf_ref(buf);
   p->ptr          = 0;
   return &p->base;
}

/* ------------------------------------------------------------------- *
 * Mixer
 * ------------------------------------------------------------------- */

struct mixer_f32
{
   f32_stream_t **streams;   /* fire-and-forget SFX                   */
   size_t         count;
   size_t         capacity;

   f32_stream_t  *music;     /* dedicated playlist slot (may be NULL) */

   float         *mix;       /* per-stream render scratch             */
   size_t         scratch_frames;

   float          master;
   int            enabled;
};

mixer_f32_t *mixer_f32_new(void)
{
   mixer_f32_t *mixer = (mixer_f32_t*)malloc(sizeof(*mixer));

   if (!mixer)
      return NULL;

   mixer->streams        = NULL;
   mixer->count          = 0;
   mixer->capacity       = 0;
   mixer->music          = NULL;
   mixer->mix            = NULL;
   mixer->scratch_frames = 0;
   mixer->master         = 1.0f;
   mixer->enabled        = 0;
   return mixer;
}

void mixer_f32_clear(mixer_f32_t *mixer)
{
   size_t i;

   if (!mixer)
      return;

   for (i = 0; i < mixer->count; i++)
   {
      f32_stream_t *s = mixer->streams[i];
      if (s && s->destroy)
         s->destroy(s);
   }
   mixer->count = 0;

   if (mixer->music && mixer->music->destroy)
      mixer->music->destroy(mixer->music);
   mixer->music = NULL;
}

void mixer_f32_free(mixer_f32_t *mixer)
{
   if (!mixer)
      return;

   mixer_f32_clear(mixer);

   if (mixer->streams)
      free(mixer->streams);
   if (mixer->mix)
      free(mixer->mix);
   free(mixer);
}

void mixer_f32_add(mixer_f32_t *mixer, f32_stream_t *stream)
{
   if (!mixer || !stream)
   {
      if (stream && stream->destroy)
         stream->destroy(stream);
      return;
   }

   if (mixer->count == mixer->capacity)
   {
      size_t         new_cap = mixer->capacity ? (mixer->capacity * 2) : 4;
      f32_stream_t **grown   = (f32_stream_t**)realloc(mixer->streams,
            new_cap * sizeof(*grown));

      if (!grown)
      {
         /* Out of memory: drop the stream rather than leak it. */
         if (stream->destroy)
            stream->destroy(stream);
         return;
      }

      mixer->streams  = grown;
      mixer->capacity = new_cap;
   }

   mixer->streams[mixer->count++] = stream;
}

void mixer_f32_set_master(mixer_f32_t *mixer, float gain)
{
   if (!mixer)
      return;
   if (gain < 0.0f)
      gain = 0.0f;
   mixer->master = gain;
}

float mixer_f32_master(const mixer_f32_t *mixer)
{
   if (!mixer)
      return 0.0f;
   return mixer->master;
}

void mixer_f32_set_music(mixer_f32_t *mixer, f32_stream_t *stream)
{
   if (!mixer)
   {
      if (stream && stream->destroy)
         stream->destroy(stream);
      return;
   }

   if (mixer->music && mixer->music->destroy)
      mixer->music->destroy(mixer->music);
   mixer->music = stream;
}

int mixer_f32_music_active(const mixer_f32_t *mixer)
{
   if (!mixer)
      return 0;
   return (mixer->music && mixer->music->valid)
      ? mixer->music->valid(mixer->music) : 0;
}

void mixer_f32_set_enabled(mixer_f32_t *mixer, int enabled)
{
   if (!mixer)
      return;
   mixer->enabled = enabled ? 1 : 0;
}

int mixer_f32_enabled(const mixer_f32_t *mixer)
{
   if (!mixer)
      return 0;
   return mixer->enabled;
}

static void mixer_f32_purge(mixer_f32_t *mixer)
{
   size_t r;
   size_t w = 0;

   for (r = 0; r < mixer->count; r++)
   {
      f32_stream_t *s = mixer->streams[r];
      if (s && s->valid && s->valid(s))
         mixer->streams[w++] = s;
      else if (s && s->destroy)
         s->destroy(s);
   }
   mixer->count = w;
}

/* Grow the per-stream render scratch to hold 'frames'. Returns non-zero
 * on success. */
static int mixer_f32_ensure_scratch(mixer_f32_t *mixer, size_t frames)
{
   float *mix;

   if (frames <= mixer->scratch_frames && mixer->mix)
      return 1;

   mix = (float*)realloc(mixer->mix,
         frames * MIXER_F32_CHANNELS * sizeof(float));
   if (!mix)
      return 0;

   mixer->mix            = mix;
   mixer->scratch_frames = frames;
   return 1;
}

/* Render one stream and accumulate it into 'out' with its
 * (master * per-stream) gain applied. audio_mix_volume is
 * libretro-common's accumulate-with-gain, so this picks up its SIMD
 * paths rather than open-coding the loop. */
static void mixer_f32_mix_one(mixer_f32_t *mixer, f32_stream_t *s,
      float *out, size_t frames)
{
   float  gain;
   size_t rendered;

   if (!s || !s->render)
      return;

   gain     = mixer->master * s->volume;
   rendered = s->render(s, mixer->mix, frames);

   if (rendered && gain != 0.0f)
      audio_mix_volume(out, mixer->mix, gain,
            rendered * MIXER_F32_CHANNELS);
}

void mixer_f32_render(mixer_f32_t *mixer, float *out, size_t frames)
{
   size_t samples;
   size_t i;
   size_t si;

   if (!out || frames == 0)
      return;

   samples = frames * MIXER_F32_CHANNELS;

   if (!mixer)
   {
      memset(out, 0, samples * sizeof(float));
      return;
   }

   mixer_f32_purge(mixer);

   /* Retire a finished music track so the slot frees up for the next. */
   if (mixer->music && mixer->music->valid
         && !mixer->music->valid(mixer->music))
   {
      if (mixer->music->destroy)
         mixer->music->destroy(mixer->music);
      mixer->music = NULL;
   }

   if (!mixer_f32_ensure_scratch(mixer, frames))
   {
      memset(out, 0, samples * sizeof(float));
      return;
   }

   for (i = 0; i < samples; i++)
      out[i] = 0.0f;

   if (mixer->music)
      mixer_f32_mix_one(mixer, mixer->music, out, frames);

   for (si = 0; si < mixer->count; si++)
      mixer_f32_mix_one(mixer, mixer->streams[si], out, frames);
}
