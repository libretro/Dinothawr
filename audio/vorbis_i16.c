/* Dinothawr - Ogg Vorbis -> interleaved stereo int16 decode (impl).
 * MSVC C89. See vorbis_i16.h. */

#include "vorbis_i16.h"

#include <stdlib.h>

#include <vorbis/vorbisfile.h>

#include <retro_inline.h>

/* Decode block size, in frames, requested from ov_read_float per call. */
#define VORBIS_I16_CHUNK_FRAMES 4096

/* Saturating float [-1,1] -> int16, matching convert_float_to_s16
 * (scale by 32768, clamp). */
static INLINE int16_t vorbis_i16_clip(float f)
{
   int s = (int)(f * 32768.0f);

   if (s > 32767)
      s = 32767;
   else if (s < -32768)
      s = -32768;
   return (int16_t)s;
}

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
   OggVorbis_File  vf;
   vorbis_info    *info;
   i16_buf_t      *buf;
   int16_t        *out     = NULL;
   size_t          out_len = 0; /* int16 samples written    */
   size_t          out_cap = 0; /* int16 samples allocated  */
   int             is_mono;
   int             bitstream;

   if (ov_fopen(path, &vf) < 0)
      return NULL;

   info = ov_info(&vf, -1);
   if (    !info
        || info->rate     != VORBIS_I16_SAMPLE_RATE
        || info->channels  < 1
        || info->channels  > 2)
   {
      ov_clear(&vf);
      return NULL;
   }
   is_mono = (info->channels == 1);

   for (;;)
   {
      float **pcm;
      long    ret = ov_read_float(&vf, &pcm, VORBIS_I16_CHUNK_FRAMES,
            &bitstream);
      long    i;
      size_t  need;

      if (ret <= 0) /* 0 == EOF, < 0 == decode error: stop either way */
         break;

      need = out_len + (size_t)ret * MIXER_I16_CHANNELS;
      out  = vorbis_i16_reserve(out, &out_cap, need);
      if (!out)
      {
         ov_clear(&vf);
         return NULL;
      }

      if (is_mono)
      {
         for (i = 0; i < ret; i++)
         {
            int16_t s = vorbis_i16_clip(pcm[0][i]);
            out[out_len++] = s;
            out[out_len++] = s;
         }
      }
      else
      {
         for (i = 0; i < ret; i++)
         {
            out[out_len++] = vorbis_i16_clip(pcm[0][i]);
            out[out_len++] = vorbis_i16_clip(pcm[1][i]);
         }
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
