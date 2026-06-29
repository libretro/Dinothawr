/* Dinothawr - deterministic int16 audio mixer (implementation).
 * MSVC C89. See mixer_i16.h for the contract. */

#include "mixer_i16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <retro_inline.h>

/* ------------------------------------------------------------------- *
 * Small helpers
 * ------------------------------------------------------------------- */

/* Round-to-nearest float->Q15, clamping negatives to silence. Done once
 * per gain at setup time, never in the per-sample loop. */
int32_t mixer_i16_q15_from_float(float gain)
{
   float scaled;

   if (gain <= 0.0f)
      return 0;

   scaled = gain * (float)MIXER_I16_UNITY_Q15;
   /* + 0.5 then truncate == round to nearest for non-negative values. */
   return (int32_t)(scaled + 0.5f);
}

/* Combine two Q15 gains into one Q15 gain. Both operands are
 * non-negative, so the division is well defined and portable. */
static INLINE int32_t q15_combine(int32_t a, int32_t b)
{
   return (int32_t)(((int32_t)a * (int32_t)b) / MIXER_I16_UNITY_Q15);
}

/* Apply a non-negative Q15 gain to a signed int16 sample. The sign is
 * kept out of the division so behaviour does not depend on the
 * implementation-defined rounding of signed integer division in C89. */
static INLINE int32_t q15_apply(int32_t sample, int32_t gain_q15)
{
   int32_t mag;
   int32_t scaled;

   if (sample < 0)
   {
      mag    = -sample;
      scaled = (mag * gain_q15) / MIXER_I16_UNITY_Q15;
      return -scaled;
   }

   scaled = (sample * gain_q15) / MIXER_I16_UNITY_Q15;
   return scaled;
}

/* ------------------------------------------------------------------- *
 * Reference-counted shared PCM buffer
 * ------------------------------------------------------------------- */

struct i16_buf
{
   int16_t *data;
   size_t   samples; /* total int16 count (frames * MIXER_I16_CHANNELS) */
   int      refcount;
};

i16_buf_t *i16_buf_new(int16_t *data, size_t samples)
{
   i16_buf_t *buf = (i16_buf_t*)malloc(sizeof(*buf));

   if (!buf)
      return NULL;

   buf->data     = data;
   buf->samples  = samples;
   buf->refcount = 1;
   return buf;
}

i16_buf_t *i16_buf_ref(i16_buf_t *buf)
{
   if (buf)
      buf->refcount++;
   return buf;
}

void i16_buf_unref(i16_buf_t *buf)
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
 * PCM stream (plays a shared i16_buf)
 * ------------------------------------------------------------------- */

typedef struct
{
   i16_stream_t base;
   i16_buf_t   *buf;
   size_t       ptr; /* read cursor, in int16 samples */
} i16_pcm_t;

static size_t i16_pcm_render(i16_stream_t *s, int16_t *out, size_t frames)
{
   i16_pcm_t *p    = (i16_pcm_t*)s;
   size_t     want = frames * MIXER_I16_CHANNELS;
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

      memcpy(out + done, p->buf->data + p->ptr, chunk * sizeof(int16_t));
      p->ptr += chunk;
      done   += chunk;
   }

   return done / MIXER_I16_CHANNELS;
}

static int i16_pcm_valid(const i16_stream_t *s)
{
   const i16_pcm_t *p = (const i16_pcm_t*)s;
   if (s->loop)
      return 1;
   return p->ptr < p->buf->samples;
}

static void i16_pcm_rewind(i16_stream_t *s)
{
   i16_pcm_t *p = (i16_pcm_t*)s;
   p->ptr = 0;
}

static void i16_pcm_destroy(i16_stream_t *s)
{
   i16_pcm_t *p = (i16_pcm_t*)s;
   if (!p)
      return;
   i16_buf_unref(p->buf);
   free(p);
}

i16_stream_t *i16_pcm_stream_new(i16_buf_t *buf, int loop, int32_t volume_q15)
{
   i16_pcm_t *p;

   if (!buf)
      return NULL;

   p = (i16_pcm_t*)malloc(sizeof(*p));
   if (!p)
      return NULL;

   p->base.render     = i16_pcm_render;
   p->base.valid      = i16_pcm_valid;
   p->base.rewind     = i16_pcm_rewind;
   p->base.destroy    = i16_pcm_destroy;
   p->base.volume_q15 = volume_q15;
   p->base.loop       = loop;
   p->buf             = i16_buf_ref(buf);
   p->ptr             = 0;
   return &p->base;
}

/* ------------------------------------------------------------------- *
 * WAV loader (16-bit PCM, mono/stereo, 44100 Hz)
 * ------------------------------------------------------------------- */

static INLINE unsigned read_le16(const unsigned char *p)
{
   return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static INLINE unsigned read_le32(const unsigned char *p)
{
   return  (unsigned)p[0]
        | ((unsigned)p[1] << 8)
        | ((unsigned)p[2] << 16)
        | ((unsigned)p[3] << 24);
}

int16_t *wav_i16_load(const char *path, size_t *out_samples)
{
   unsigned char  header[44];
   FILE          *file;
   unsigned       channels;
   unsigned       sample_rate;
   unsigned       bits;
   unsigned       riff_size;
   unsigned       data_bytes;
   size_t         in_samples;
   size_t         read_samples;
   int16_t       *raw;
   int16_t       *pcm;

   if (out_samples)
      *out_samples = 0;

   file = fopen(path, "rb");
   if (!file)
      return NULL;

   if (fread(header, 1, sizeof(header), file) != sizeof(header))
   {
      fclose(file);
      return NULL;
   }

   if (    memcmp(header + 0,  "RIFF", 4) != 0
        || memcmp(header + 8,  "WAVE", 4) != 0
        || memcmp(header + 12, "fmt ", 4) != 0)
   {
      fclose(file);
      return NULL;
   }

   if (read_le16(header + 20) != 1) /* PCM */
   {
      fclose(file);
      return NULL;
   }

   channels    = read_le16(header + 22);
   sample_rate = read_le32(header + 24);
   bits        = read_le16(header + 34);

   if (channels < 1 || channels > 2 || sample_rate != 44100 || bits != 16)
   {
      fclose(file);
      return NULL;
   }

   riff_size  = read_le32(header + 4);
   data_bytes = riff_size + 8 - (unsigned)sizeof(header);

   in_samples = data_bytes / sizeof(int16_t);
   if (in_samples == 0)
   {
      fclose(file);
      return NULL;
   }

   raw = (int16_t*)malloc(in_samples * sizeof(int16_t));
   if (!raw)
   {
      fclose(file);
      return NULL;
   }

   read_samples = fread(raw, sizeof(int16_t), in_samples, file);
   fclose(file);

   /* Tolerate a short final read rather than rejecting the whole clip. */
   if (read_samples == 0)
   {
      free(raw);
      return NULL;
   }
   in_samples = read_samples;

   if (channels == 2)
   {
      if (out_samples)
         *out_samples = in_samples;
      return raw;
   }

   /* Mono: duplicate each sample into a stereo frame. */
   {
      size_t i;
      pcm = (int16_t*)malloc(in_samples * MIXER_I16_CHANNELS * sizeof(int16_t));
      if (!pcm)
      {
         free(raw);
         return NULL;
      }

      for (i = 0; i < in_samples; i++)
      {
         pcm[2 * i + 0] = raw[i];
         pcm[2 * i + 1] = raw[i];
      }

      free(raw);
      if (out_samples)
         *out_samples = in_samples * MIXER_I16_CHANNELS;
      return pcm;
   }
}

/* ------------------------------------------------------------------- *
 * Mixer
 * ------------------------------------------------------------------- */

struct mixer_i16
{
   i16_stream_t **streams;
   size_t         count;
   size_t         capacity;

   int32_t       *acc;       /* int32 accumulator scratch          */
   int16_t       *mix;       /* per-stream render scratch          */
   size_t         scratch_frames;

   int32_t        master_q15;
};

mixer_i16_t *mixer_i16_new(void)
{
   mixer_i16_t *mixer = (mixer_i16_t*)malloc(sizeof(*mixer));

   if (!mixer)
      return NULL;

   mixer->streams        = NULL;
   mixer->count          = 0;
   mixer->capacity       = 0;
   mixer->acc            = NULL;
   mixer->mix            = NULL;
   mixer->scratch_frames = 0;
   mixer->master_q15     = MIXER_I16_UNITY_Q15;
   return mixer;
}

void mixer_i16_clear(mixer_i16_t *mixer)
{
   size_t i;

   if (!mixer)
      return;

   for (i = 0; i < mixer->count; i++)
   {
      i16_stream_t *s = mixer->streams[i];
      if (s && s->destroy)
         s->destroy(s);
   }
   mixer->count = 0;
}

void mixer_i16_free(mixer_i16_t *mixer)
{
   if (!mixer)
      return;

   mixer_i16_clear(mixer);

   if (mixer->streams)
      free(mixer->streams);
   if (mixer->acc)
      free(mixer->acc);
   if (mixer->mix)
      free(mixer->mix);
   free(mixer);
}

void mixer_i16_add(mixer_i16_t *mixer, i16_stream_t *stream)
{
   if (!mixer || !stream)
      return;

   if (mixer->count == mixer->capacity)
   {
      size_t          new_cap = mixer->capacity ? (mixer->capacity * 2) : 4;
      i16_stream_t  **grown   = (i16_stream_t**)realloc(mixer->streams,
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

void mixer_i16_set_master_q15(mixer_i16_t *mixer, int32_t q15)
{
   if (!mixer)
      return;
   if (q15 < 0)
      q15 = 0;
   mixer->master_q15 = q15;
}

static void mixer_i16_purge(mixer_i16_t *mixer)
{
   size_t r = 0;
   size_t w = 0;

   for (r = 0; r < mixer->count; r++)
   {
      i16_stream_t *s = mixer->streams[r];
      if (s && s->valid && s->valid(s))
         mixer->streams[w++] = s;
      else if (s && s->destroy)
         s->destroy(s);
   }
   mixer->count = w;
}

/* Grow the int32 accumulator / int16 render scratch to hold 'frames'.
 * Returns non-zero on success. */
static int mixer_i16_ensure_scratch(mixer_i16_t *mixer, size_t frames)
{
   size_t samples;

   if (frames <= mixer->scratch_frames && mixer->acc && mixer->mix)
      return 1;

   samples = frames * MIXER_I16_CHANNELS;

   {
      int32_t *acc = (int32_t*)realloc(mixer->acc, samples * sizeof(int32_t));
      int16_t *mix;

      if (!acc)
         return 0;
      mixer->acc = acc;

      mix = (int16_t*)realloc(mixer->mix, samples * sizeof(int16_t));
      if (!mix)
         return 0;
      mixer->mix = mix;
   }

   mixer->scratch_frames = frames;
   return 1;
}

void mixer_i16_render(mixer_i16_t *mixer, int16_t *out, size_t frames)
{
   size_t  samples;
   size_t  i;
   size_t  si;

   if (!out || frames == 0)
      return;

   samples = frames * MIXER_I16_CHANNELS;

   if (!mixer)
   {
      memset(out, 0, samples * sizeof(int16_t));
      return;
   }

   mixer_i16_purge(mixer);

   if (!mixer_i16_ensure_scratch(mixer, frames))
   {
      memset(out, 0, samples * sizeof(int16_t));
      return;
   }

   for (i = 0; i < samples; i++)
      mixer->acc[i] = 0;

   for (si = 0; si < mixer->count; si++)
   {
      i16_stream_t *s        = mixer->streams[si];
      int32_t       gain_q15;
      size_t        rendered;
      size_t        n;

      if (!s || !s->render)
         continue;

      gain_q15 = q15_combine(mixer->master_q15, s->volume_q15);

      rendered = s->render(s, mixer->mix, frames);
      n        = rendered * MIXER_I16_CHANNELS;

      if (gain_q15 == MIXER_I16_UNITY_Q15)
      {
         for (i = 0; i < n; i++)
            mixer->acc[i] += (int32_t)mixer->mix[i];
      }
      else if (gain_q15 != 0)
      {
         for (i = 0; i < n; i++)
            mixer->acc[i] += q15_apply((int32_t)mixer->mix[i], gain_q15);
      }
   }

   for (i = 0; i < samples; i++)
   {
      int32_t v = mixer->acc[i];
      if (v >  32767)
         v =  32767;
      else if (v < -32768)
         v = -32768;
      out[i] = (int16_t)v;
   }
}
