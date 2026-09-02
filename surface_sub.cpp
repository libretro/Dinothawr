#include "surface_cache.hpp"
#include <new>

namespace Blit
{
   blit_surface_t surface_sub(const blit_surface_t& src, Rect rect)
   {
      blit_render_target_t target;
      blit_surface_t       shifted = src;
      blit_surface_data_t *data;
      blit_surface_t       out;

      if (!blit_render_target_init_size(&target, rect.w, rect.h))
         throw std::bad_alloc();

      /* Draw the source shifted so that @rect lands at the target's
       * origin; the target is exactly the size of the rect. */
      blit_surface_retain(&shifted);
      shifted.rect.pos = blit_pos_neg(rect.pos);
      blit_render_target_blit(&target, &shifted, rect);
      blit_surface_release(&shifted);

      data = blit_render_target_to_data(&target);
      blit_render_target_release(&target);

      if (!data)
         throw std::bad_alloc();

      blit_surface_init_data(&out, data);
      blit_surface_data_unref(data);
      return out;
   }
}
