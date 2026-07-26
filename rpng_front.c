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
