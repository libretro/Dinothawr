/* Dinothawr - compressed audio -> interleaved stereo int16 decode (impl).
 * MSVC C89. See vorbis_i16.h.
 *
 * Everything goes through libretro-common's audio_transfer facade over a
 * data_transfer handle: the transfer owns the encoded bytes, the decoder
 * borrows them, and WAV and Ogg Vorbis are the same code path.  The s16
 * read quantises once on the way out of the decoder's own buffers, which
 * for a float-internal codec like Vorbis is the minimum possible. */

#include "vorbis_i16.h"

#include <stdlib.h>

#include <formats/audio.h>
#include <formats/data_transfer.h>

#include <retro_inline.h>

/* Decode scratch, in frames. */
#define AUDIO_I16_CHUNK_FRAMES 2048

/* Grow 'buf' (int16 capacity 'cap') so it can hold at least 'need'
 * int16 samples. Returns the (possibly moved) buffer, or NULL on OOM
 * after freeing the old buffer. */
static int16_t *audio_i16_reserve(int16_t *buf, size_t *cap, size_t need)
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

int16_t *audio_i16_decode_file(const char *path, size_t *out_samples)
{
   const uint8_t       *ptr      = NULL;
   size_t               len      = 0;
   data_transfer_t     *dt       = NULL;
   void                *xfer     = NULL;
   int16_t             *out      = NULL;
   size_t               out_len  = 0; /* int16 samples written   */
   size_t               out_cap  = 0; /* int16 samples allocated */
   unsigned             channels = 0;
   unsigned             rate     = 0;
   uint64_t             total_frames = 0;
   enum audio_type_enum type     = audio_decode_get_type(path);
   int16_t              chunk[AUDIO_I16_CHUNK_FRAMES * MIXER_I16_CHANNELS];

   if (out_samples)
      *out_samples = 0;

   if (type == AUDIO_TYPE_NONE)
      return NULL;

   if (!(dt = data_transfer_open_prefix(path, 0)))
      return NULL;

   data_transfer_iterate(dt, 0);
   ptr = data_transfer_ptr(dt, &len);

   if (!data_transfer_complete(dt) || !ptr || !len)
      goto error;

   if (!(xfer = audio_transfer_new(type)))
      goto error;

   audio_transfer_set_buffer_ptr(xfer, type, (void*)ptr, len);

   if (    !audio_transfer_start(xfer, type)
        || !audio_transfer_info(xfer, type, &channels, &rate,
              &total_frames))
      goto error;

   if (rate != AUDIO_I16_SAMPLE_RATE || channels < 1 || channels > 2)
      goto error;

   /* One read of the whole stream when the length is known.  No more
    * blocking than the chunked loop below - that ran the file to
    * completion without yielding either, and the caller runs this on a
    * decode job - but it sizes the buffer once instead of growing it by
    * doubling, which transiently holds both buffers at every move. */
   if (total_frames)
   {
      size_t got = 0;

      out_cap = (size_t)total_frames * MIXER_I16_CHANNELS;
      if (!(out = (int16_t*)malloc(out_cap * sizeof(int16_t))))
         goto error;

      if (audio_transfer_read_s16(xfer, type, out, (size_t)total_frames,
               &got) >= AUDIO_PROCESS_NEXT && got)
      {
         if (channels == 1)
         {
            /* Decoded as one channel into the head of the buffer.
             * Expand backwards: every write lands at 2i >= i, so it
             * never lands on a sample still to be read. */
            size_t i = got;
            while (i-- > 0)
            {
               int16_t v      = out[i];
               out[2 * i + 0] = v;
               out[2 * i + 1] = v;
            }
         }
         out_len = got * MIXER_I16_CHANNELS;
      }
      else
      {
         /* The single read declined; fall back to the chunked loop. */
         free(out);
         out     = NULL;
         out_cap = 0;
         if (!audio_transfer_seek(xfer, type, 0))
            goto error;
      }
   }

   if (!out_len) for (;;)
   {
      size_t got = 0;
      size_t need;
      size_t i;

      /* A short read is not end of stream; zero frames is. */
      if (audio_transfer_read_s16(xfer, type, chunk,
               AUDIO_I16_CHUNK_FRAMES, &got) < AUDIO_PROCESS_NEXT)
         goto error;

      if (!got)
         break;

      need = out_len + got * MIXER_I16_CHANNELS;
      out  = audio_i16_reserve(out, &out_cap, need);
      if (!out)
         goto error_freed;

      if (channels == 1)
      {
         for (i = 0; i < got; i++)
         {
            int16_t s = chunk[i];
            out[out_len++] = s;
            out[out_len++] = s;
         }
      }
      else
      {
         size_t n = got * MIXER_I16_CHANNELS;
         for (i = 0; i < n; i++)
            out[out_len++] = chunk[i];
      }
   }

   audio_transfer_free(xfer, type);
   data_transfer_free(dt);

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

   if (out_samples)
      *out_samples = out_len;
   return out;

error:
   if (out)
      free(out);
error_freed:
   if (xfer)
      audio_transfer_free(xfer, type);
   data_transfer_free(dt);
   return NULL;
}

i16_buf_t *vorbis_i16_decode_file(const char *path)
{
   size_t   samples = 0;
   int16_t *pcm     = audio_i16_decode_file(path, &samples);
   i16_buf_t *buf;

   if (!pcm)
      return NULL;

   buf = i16_buf_new(pcm, samples);
   if (!buf)
   {
      free(pcm);
      return NULL;
   }
   return buf;
}
