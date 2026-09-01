/* Dinothawr - the render target (implementation).
 * MSVC C89. See blit_render_target.h. */

#include "blit_render_target.h"

#include <stdlib.h>

void blit_render_target_init(blit_render_target_t *target)
{
   target->buffer = NULL;
   target->count  = 0;
   target->rect   = blit_rect_zero();
}

int blit_render_target_init_size(blit_render_target_t *target,
      int w, int h)
{
   size_t count;

   blit_render_target_init(target);

   if (w < 0 || h < 0)
      return 0;

   /* Zeroed, not merely allocated: the std::vector this replaces
    * value-initialised its pixels, and callers rely on a fresh target
    * being transparent before anything is drawn into it. */
   count = (size_t)w * (size_t)h;
   if (!(target->buffer = (blit_pixel_t*)calloc(
               (count ? count : 1), sizeof(blit_pixel_t))))
      return 0;

   target->count = count;
   target->rect  = blit_rect(blit_pos_zero(), w, h);
   return 1;
}

void blit_render_target_release(blit_render_target_t *target)
{
   if (target->buffer)
      free(target->buffer);
   blit_render_target_init(target);
}

void blit_render_target_clear(blit_render_target_t *target,
      blit_pixel_t fill)
{
   size_t i;
   for (i = 0; i < target->count; i++)
      target->buffer[i] = fill;
}

blit_surface_data_t *blit_render_target_to_data(
      blit_render_target_t *target)
{
   blit_surface_data_t *data = blit_surface_data_new(target->buffer,
         target->rect.w, target->rect.h);

   if (!data)
      return NULL;

   /* The data owns the buffer now. */
   target->buffer = NULL;
   target->count  = 0;
   target->rect   = blit_rect_zero();
   return data;
}

void blit_render_target_blit_offset(blit_render_target_t *target,
      const blit_surface_t *surf, blit_rect_t subrect, blit_pos_t offset)
{
   blit_rect_t         surf_rect = blit_rect_offset(surf->rect, offset);
   blit_rect_t         dest_rect = target->rect;
   blit_rect_t         clip;
   const blit_pixel_t *src;
   blit_pixel_t       *dst;

   if (surf->ignore_camera)
      dest_rect.pos = blit_pos_zero();

   clip = blit_rect_intersect(surf_rect, dest_rect);

   if (blit_rect_valid(subrect))
      clip = blit_rect_intersect(clip,
            blit_rect_offset(subrect, surf_rect.pos));

   if (!blit_rect_valid(clip))
      return;

   src = blit_surface_pixel_raw(surf, blit_pos_sub(clip.pos, offset));
   dst = surf->ignore_camera
      ? blit_render_target_pixel_raw_no_offset(target, clip.pos)
      : blit_render_target_pixel_raw(target, clip.pos);

   if (!src || !dst)
      return;

   /* Strides in locals, not re-read from the structs each row: the blit
    * stores through a blit_pixel_t*, which is unsigned int and so may
    * alias the int members it would otherwise hoist. */
   {
      const int    src_stride = surf->data->w;
      const int    dst_stride = target->rect.w;
      const size_t run        = (size_t)clip.w;
      int          y;

      for (y = 0; y < clip.h; y++, src += src_stride, dst += dst_stride)
         blit_pixel_set_line_if_alpha(dst, src, run);
   }
}

void blit_render_target_blit(blit_render_target_t *target,
      const blit_surface_t *surf, blit_rect_t subrect)
{
   blit_render_target_blit_offset(target, surf, subrect,
         blit_pos_zero());
}
