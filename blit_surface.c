/* Dinothawr - a drawable surface (implementation).
 * MSVC C89. See blit_surface.h. */

#include "blit_surface.h"

#include <stdlib.h>

void blit_surface_init(blit_surface_t *surf)
{
   surf->data             = NULL;
   surf->alts             = NULL;
   surf->attribs          = NULL;
   surf->active_alt       = NULL;
   surf->active_alt_index = 0;
   surf->rect             = blit_rect_zero();
   surf->ignore_camera    = 0;
}

void blit_surface_init_data(blit_surface_t *surf,
      blit_surface_data_t *data)
{
   blit_surface_init(surf);
   surf->data = blit_surface_data_ref(data);
   if (data)
      surf->rect = blit_rect(blit_pos_zero(), data->w, data->h);
}

int blit_surface_init_filled(blit_surface_t *surf, blit_pixel_t fill,
      int w, int h)
{
   blit_surface_data_t *data = blit_surface_data_new_filled(fill, w, h);

   blit_surface_init(surf);

   if (!data)
      return 0;

   /* init_data takes a reference of its own; drop the one we made. */
   blit_surface_init_data(surf, data);
   blit_surface_data_unref(data);
   return 1;
}

int blit_surface_init_alts(blit_surface_t *surf, blit_alt_table_t *alts,
      const char *start_id)
{
   size_t               count = blit_alt_table_size(alts);
   blit_surface_data_t *first;
   size_t               i;

   blit_surface_init(surf);

   if (count == 0)
      return 0;

   /* Every face has to be the same size: the surface carries one rect
    * and switching face must not change where it sits. */
   first = blit_alt_table_data_at(alts, 0);
   for (i = 1; i < count; i++)
   {
      blit_surface_data_t *face = blit_alt_table_data_at(alts, i);

      if (!face || face->w != first->w || face->h != first->h)
         return 0;
   }

   surf->alts = blit_alt_table_ref(alts);
   surf->rect = blit_rect(blit_pos_zero(), first->w, first->h);

   if (!blit_surface_set_active_alt(surf, start_id, 0))
   {
      blit_surface_release(surf);
      return 0;
   }

   return 1;
}

void blit_surface_retain(const blit_surface_t *surf)
{
   blit_surface_data_ref(surf->data);
   blit_alt_table_ref(surf->alts);
   blit_attr_table_ref(surf->attribs);
}

void blit_surface_release(blit_surface_t *surf)
{
   blit_alt_table_unref(surf->alts);
   blit_attr_table_unref(surf->attribs);
   blit_surface_data_unref(surf->data);

   surf->data       = NULL;
   surf->alts       = NULL;
   surf->attribs    = NULL;
   surf->active_alt = NULL;
}

void blit_surface_assign(blit_surface_t *dst, const blit_surface_t *src)
{
   blit_surface_t incoming = *src;

   blit_surface_retain(&incoming);
   blit_surface_release(dst);
   *dst = incoming;
}

int blit_surface_set_active_alt(blit_surface_t *surf, const char *id,
      unsigned index)
{
   blit_surface_data_t *face;
   const char          *tag;

   if (blit_alt_table_count(surf->alts, id) <= index)
      return 0;

   face = blit_alt_table_at(surf->alts, id, index);
   tag  = blit_alt_table_tag(surf->alts, id);

   if (!face || !tag)
      return 0;

   /* The table holds its own reference; take a second for the active
    * slot before dropping the old one, in case they are the same. */
   blit_surface_data_ref(face);
   blit_surface_data_unref(surf->data);

   surf->data             = face;
   surf->active_alt       = tag;
   surf->active_alt_index = index;
   return 1;
}

int blit_surface_set_attr(blit_surface_t *surf, const char *key,
      const char *value)
{
   if (!surf->attribs)
   {
      if (!(surf->attribs = blit_attr_table_new()))
         return 0;
   }
   else if (blit_attr_table_shared(surf->attribs))
   {
      blit_attr_table_t *fresh = blit_attr_table_clone(surf->attribs);

      if (!fresh)
         return 0;

      blit_attr_table_unref(surf->attribs);
      surf->attribs = fresh;
   }

   return blit_attr_table_set(surf->attribs, key, value);
}

int blit_surface_refill_color(blit_surface_t *surf, blit_pixel_t colour)
{
   blit_surface_data_t *fresh;
   blit_pixel_t        *pixels;
   size_t               count;
   size_t               i;

   if (!surf->data)
      return 0;

   count  = (size_t)surf->data->w * (size_t)surf->data->h;
   pixels = (blit_pixel_t*)malloc((count ? count : 1) * sizeof(*pixels));

   if (!pixels)
      return 0;

   for (i = 0; i < count; i++)
      pixels[i] = (surf->data->pixels[i] & BLIT_PIXEL_ALPHA_MASK)
         ? colour : (blit_pixel_t)0;

   fresh = blit_surface_data_new(pixels, surf->data->w, surf->data->h);
   if (!fresh)
      return 0;

   blit_surface_data_unref(surf->data);
   surf->data = fresh;
   return 1;
}
