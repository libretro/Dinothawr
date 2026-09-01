#include "surface.hpp"
#include <stdexcept>
#include <utility>

using namespace std;

namespace Blit
{
   static bool same_size_func(const vector<Surface::Alt>& alts, Pos size)
   {
      for( vector<Surface::Alt>::const_iterator alt = alts.begin(); alt!=alts.end(); alt++ )
         if(size != blit_pos(alt->data->w, alt->data->h))
            return false;
      return true;
   }

   Surface::Surface(const vector<Alt>& alts, const string& start_id)
   {
      blit_surface_init(&s);

      if (alts.empty())
         throw logic_error("Alts is empty.");

      {
         Pos size = blit_pos(alts.front().data->w, alts.front().data->h);
         s.rect = blit_rect(blit_pos_zero(), size.x, size.y);

         if (!same_size_func(alts, size))
            throw logic_error("Not all alts are of same size.");
      }

      if (!(s.alts = blit_alt_table_new()))
         throw std::bad_alloc();

      for( vector<Alt>::const_iterator alt = alts.begin(); alt!=alts.end(); alt++ )
         if (!blit_alt_table_add(s.alts, alt->tag.c_str(), alt->data))
            throw std::bad_alloc();

      active_alt(start_id);
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
