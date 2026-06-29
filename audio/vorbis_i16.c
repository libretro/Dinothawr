/* Dinothawr - Ogg Vorbis -> interleaved stereo int16 decode (impl).
 * MSVC C89. See vorbis_i16.h.
 *
 * Decode is integer-only via Tremor's ov_read(): no float appears
 * anywhere on this path, so the produced PCM is bit-deterministic across
 * platforms. */

#include "vorbis_i16.h"

#include <stdio.h>
#include <stdlib.h>

#include <tremor/ivorbisfile.h>

#include <retro_inline.h>

/* Decode scratch, in int16 samples (8 KiB). */
#define VORBIS_I16_CHUNK_SAMPLES 4096

/* Grow 'buf' (int16 capacity 'cap') so it can hold at least 'need'
 * int16 samples. Returns the (possibly moved) buffer, or NULL on OOM
 * after freeing the old buffer. */
static int16_t *vorbis_i16_reserve(int16_t *buf, size_t *cap, size_t need)
{
   size_t   new_cap;
   int16_t *grown;

   if (need <= *cap)
      return buf;

   new_cap = (*cap != 0) ? *cap : 8192;
   while (new_cap < need)
      new_cap *= 2;

   grown = (int16_t*)realloc(buf, new_cap * sizeof(int16_t));
   if (!grown)
   {
      if (buf)
         free(buf);
      return NULL;
   }

   *cap = new_cap;
   return grown;
}

i16_buf_t *vorbis_i16_decode_file(const char *path)
{
   FILE           *fp;
   OggVorbis_File  vf;
   vorbis_info    *info;
   i16_buf_t      *buf;
   int16_t        *out     = NULL;
   size_t          out_len = 0; /* int16 samples written    */
   size_t          out_cap = 0; /* int16 samples allocated  */
   int             channels;
   int             is_mono;
   int             bitstream;
   int16_t         chunk[VORBIS_I16_CHUNK_SAMPLES];

   fp = fopen(path, "rb");
   if (!fp)
      return NULL;

   /* ov_open takes ownership of fp; ov_clear() fcloses it. */
   if (ov_open(fp, &vf, NULL, 0) < 0)
   {
      fclose(fp);
      return NULL;
   }

   info = ov_info(&vf, -1);
   if (    !info
        || info->rate     != VORBIS_I16_SAMPLE_RATE
        || info->channels  < 1
        || info->channels  > 2)
   {
      ov_clear(&vf);
      return NULL;
   }
   channels = info->channels;
   is_mono  = (channels == 1);

   for (;;)
   {
      long   bytes = ov_read(&vf, (char*)chunk, (int)sizeof(chunk),
            &bitstream);
      long   frames_in;
      long   i;
      size_t need;

      if (bytes <= 0) /* 0 == EOF, < 0 == decode error: stop either way */
         break;

      frames_in = (bytes / 2) / channels; /* int16 samples / channels */

      need = out_len + (size_t)frames_in * MIXER_I16_CHANNELS;
      out  = vorbis_i16_reserve(out, &out_cap, need);
      if (!out)
      {
         ov_clear(&vf);
         return NULL;
      }

      if (is_mono)
      {
         for (i = 0; i < frames_in; i++)
         {
            int16_t s = chunk[i];
            out[out_len++] = s;
            out[out_len++] = s;
         }
      }
      else
      {
         long n = frames_in * MIXER_I16_CHANNELS;
         for (i = 0; i < n; i++)
            out[out_len++] = chunk[i];
      }
   }

   ov_clear(&vf);

   if (!out || out_len == 0)
   {
      if (out)
         free(out);
      return NULL;
   }

   /* Trim the over-allocation; keep the original buffer if shrink fails. */
   {
      int16_t *fit = (int16_t*)realloc(out, out_len * sizeof(int16_t));
      if (fit)
         out = fit;
   }

   buf = i16_buf_new(out, out_len);
   if (!buf)
   {
      free(out);
      return NULL;
   }
   return buf;
}
