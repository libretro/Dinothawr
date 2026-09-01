/* Dinothawr - shared surface pixel data (implementation).
 * MSVC C89. See blit_surface_data.h. */

#include "blit_surface_data.h"

#include <stdlib.h>

blit_surface_data_t *blit_surface_data_new(blit_pixel_t *pixels,
      int w, int h)
{
   blit_surface_data_t *data;

   if (!pixels)
      return NULL;

   if (!(data = (blit_surface_data_t*)malloc(sizeof(*data))))
   {
      free(pixels);
      return NULL;
   }

   data->pixels   = pixels;
   data->w        = w;
   data->h        = h;
   data->refcount = 1;
   return data;
}

blit_surface_data_t *blit_surface_data_new_filled(blit_pixel_t fill,
      int w, int h)
{
   blit_pixel_t *pixels;
   size_t        count;
   size_t        i;

   if (w < 0 || h < 0)
      return NULL;

   count  = (size_t)w * (size_t)h;
   pixels = (blit_pixel_t*)malloc((count ? count : 1) * sizeof(*pixels));
   if (!pixels)
      return NULL;

   for (i = 0; i < count; i++)
      pixels[i] = fill;

   return blit_surface_data_new(pixels, w, h);
}

void blit_surface_data_unref(blit_surface_data_t *data)
{
   if (!data)
      return;

   if (--data->refcount > 0)
      return;

   if (data->pixels)
      free(data->pixels);
   free(data);
}
