#include "surface.hpp"
#include <new>
#include <stdexcept>

namespace Blit
{
   Surface RenderTarget::convert_surface()
   {
      blit_surface_data_t *data = blit_render_target_to_data(&t);

      if (!data)
         throw std::bad_alloc();

      {
         /* Surface takes its own reference. */
         Surface out(data);
         blit_surface_data_unref(data);
         return out;
      }
   }

   /* Out of line: the check that calls this inlines at every access, and
    * building the message needs a stack frame the accesses should not be
    * paying for. */
   void RenderTarget::pixel_out_of_bounds(Pos pos) const
   {
      throw std::logic_error(Utils::join(
               "Pixel was fetched out-of-bounds. ",
               "Asked for: (", pos.x, ", ", pos.y, "). ",
               "Real dimension: (", t.rect.w, ", ", t.rect.h, ")."
               ));
   }
}
