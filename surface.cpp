#include "surface.hpp"

namespace Blit
{
   blit_surface_t surface_sub(const blit_surface_t& src, Rect rect)
   {
      RenderTarget   target(rect.w, rect.h);
      blit_surface_t shifted = src;

      /* Draw the source shifted so that @rect lands at the target's
       * origin; the target is exactly the size of the rect. */
      blit_surface_retain(&shifted);
      shifted.rect.pos = blit_pos_neg(rect.pos);
      target.blit(&shifted, rect);
      blit_surface_release(&shifted);

      return target.convert_surface();
   }
}
