/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2013 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <formats/data_transfer.h>
#include <formats/image.h>
#include <formats/rpng.h>

#include "rpng_front.h"

/* Bytes read per fill step.  The read is budgeted rather than run to
 * completion so the decode overlaps it: the PNG chunk walk gathers
 * IDAT from the resident prefix and stalls at the byte frontier
 * (image_transfer_need_more) until the next step raises it. */
#ifndef RPNG_FRONT_READ_CHUNK
#define RPNG_FRONT_READ_CHUNK (64 * 1024)
#endif

unsigned rpng_load_apng_argb(const char *path, uint32_t ***frames,
      unsigned *width, unsigned *height)
{
   unsigned w             = 0;
   unsigned h             = 0;
   int num_frames         = 0;
   int loop_count         = 0;
   unsigned emitted       = 0;
   size_t file_len        = 0;
   size_t frame_size      = 0;
   const uint8_t *ptr     = NULL;
   uint32_t **out         = NULL;
   rpng_apng_stream_t *s  = NULL;
   data_transfer_t *dt    = data_transfer_open_prefix(path, 0);

   *frames                = NULL;

   if (!dt)
      return 0;

   /* The whole file has to be resident before the stream opens: frames
    * are indexed up front, and unlike the still path there is no
    * budgeted overlap to win here - sprite sheets are small. */
   while (!data_transfer_complete(dt))
   {
      data_transfer_iterate(dt, RPNG_FRONT_READ_CHUNK);
      if (data_transfer_failed(dt))
         goto end;
   }

   if (!(ptr = data_transfer_ptr(dt, &file_len)) || file_len == 0)
      goto end;

   /* A still PNG has no acTL and no stream; fall back to the ordinary
    * loader so a one-frame "animation" still resolves. */
   if (!rpng_is_apng(ptr, file_len))
   {
      uint32_t *single = NULL;

      data_transfer_free(dt);
      dt = NULL;

      if (!rpng_load_image_argb(path, &single, width, height))
         return 0;
      if (!(out = (uint32_t**)malloc(sizeof(*out))))
      {
         free(single);
         return 0;
      }
      out[0]  = single;
      *frames = out;
      return 1;
   }

   if (!(s = rpng_apng_stream_open(ptr, file_len)))
      goto end;

   rpng_apng_stream_set_argb(s, 1);
   rpng_apng_stream_get_info(s, &w, &h, &num_frames, &loop_count);

   if (num_frames <= 0 || w == 0 || h == 0)
      goto end;

   if (!(out = (uint32_t**)calloc((size_t)num_frames, sizeof(*out))))
      goto end;

   frame_size = (size_t)w * h * sizeof(uint32_t);

   for (emitted = 0; emitted < (unsigned)num_frames; emitted++)
   {
      const uint32_t *canvas = rpng_apng_stream_next(s, NULL);
      if (!canvas)
         goto end;
      if (!(out[emitted] = (uint32_t*)malloc(frame_size)))
         goto end;
      memcpy(out[emitted], canvas, frame_size);
   }

   rpng_apng_stream_close(s);
   data_transfer_free(dt);
   *frames = out;
   *width  = w;
   *height = h;
   return (unsigned)num_frames;

end:
   if (out)
   {
      unsigned i;
      for (i = 0; i < emitted; i++)
         free(out[i]);
      free(out);
   }
   if (s)
      rpng_apng_stream_close(s);
   data_transfer_free(dt);
   return 0;
}

bool rpng_load_image_argb(const char *path, uint32_t **data,
      unsigned *width, unsigned *height)
{
   int retval;
   size_t file_len     = 0;
   size_t avail        = 0;
   bool started        = false;
   bool gathered       = false;
   bool ret            = false;
   void *handle        = NULL;
   const uint8_t *ptr  = NULL;
   data_transfer_t *dt = data_transfer_open_prefix(path, 0);

   *data               = NULL;

   if (!dt)
      return false;

   /* The buffer base is valid and stable from open, so the decoder can
    * be pointed at it before a single byte has arrived. */
   if (!(ptr = data_transfer_ptr(dt, &file_len)) || file_len == 0)
      goto end;

   if (!(handle = image_transfer_new(IMAGE_TYPE_PNG)))
      goto end;

   image_transfer_set_buffer_ptr(handle, IMAGE_TYPE_PNG,
         (void*)ptr, file_len);

   while (!gathered)
   {
      avail = data_transfer_iterate(dt, RPNG_FRONT_READ_CHUNK);

      if (data_transfer_failed(dt))
         goto end;

      /* The walk can only start once the signature and IHDR are
       * resident. */
      if (!started)
      {
         if (!rpng_header_ready(ptr, avail))
         {
            if (data_transfer_complete(dt))
               goto end;
            continue;
         }

         image_transfer_set_avail(handle, IMAGE_TYPE_PNG, avail);

         if (!image_transfer_start(handle, IMAGE_TYPE_PNG))
            goto end;

         started = true;
      }
      else
         image_transfer_set_avail(handle, IMAGE_TYPE_PNG, avail);

      while (image_transfer_iterate(handle, IMAGE_TYPE_PNG));

      /* iterate() returns false both when the walk has finished and
       * when it stalled at the resident frontier - treating a stall as
       * completion would decode a partially gathered image. */
      if (!image_transfer_need_more(handle, IMAGE_TYPE_PNG))
         gathered = true;
      else if (data_transfer_complete(dt))
         goto end;
   }

   if (!image_transfer_is_valid(handle, IMAGE_TYPE_PNG))
      goto end;

   do
   {
      retval = image_transfer_process(handle, IMAGE_TYPE_PNG,
            data, file_len, width, height, false);
   }while (retval == IMAGE_PROCESS_NEXT);

   ret = (retval != IMAGE_PROCESS_ERROR)
      && (retval != IMAGE_PROCESS_ERROR_END);

end:
   if (handle)
      image_transfer_free(handle, IMAGE_TYPE_PNG);
   data_transfer_free(dt);
   if (!ret)
   {
      free(*data);
      *data = NULL;
   }
   return ret;
}
