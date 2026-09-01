#include "surface.hpp"
#include <stdexcept>
#include <utility>

using namespace std;

namespace Blit
{
   Surface::Surface(blit_alt_table_t *alts, const char *start_id)
   {
      if (!blit_surface_init_alts(&s, alts, start_id))
         throw logic_error(Utils::join(
                  "Sprite has no faces, faces of differing size, or no "
                  "face named \"", start_id ? start_id : "", "\"."));
   }

   void Surface::active_alt(const string& id, unsigned index)
   {
      if (blit_alt_table_count(s.alts, id.c_str()) <= index)
         throw logic_error(Utils::join("Subindex is out of bounds. Requested Alt: \"", id, "\" Index: ", index));

      if (!blit_surface_set_active_alt(&s, id.c_str(), index))
         throw logic_error(Utils::join("Alt ID ", id, " does not exist."));
   }

   /* Out of line: the check that calls this inlines at every read, and
    * building the message needs a stack frame and a canary that the
    * reads should not be paying for. */
   void Surface::pixel_raw_out_of_bounds(Pos pos) const
   {
      int x = pos.x - s.rect.pos.x;
      int y = pos.y - s.rect.pos.y;

      throw logic_error(Utils::join(
               "Pixel was fetched out-of-bounds. ",
               "Asked for: (", x, ", ", y, "). ",
               "Real dimension: (", s.data ? s.data->w : 0, ", ",
               s.data ? s.data->h : 0, ")."
               ));
   }

   Surface Surface::sub(Rect rect) const
   {
      RenderTarget target(rect.w, rect.h);
      Surface surf(*this);
      surf.rect().pos = -rect.pos;
      target.blit(surf, rect);
      return target.convert_surface();
   }
}
