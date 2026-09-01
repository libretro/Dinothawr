#include "surface.hpp"
#include <stdexcept>
#include <utility>
#include <memory>
#include <stdlib.h>
#include <new>

using namespace std;

namespace Blit
{
   Surface::Surface(Pixel pix, int width, int height)
      : data(blit_surface_data_new_filled(pix, width, height)),
      alts(NULL), m_active_alt(NULL), m_active_alt_index(0), attribs(NULL),
      m_rect(blit_rect(blit_pos_zero(), width, height)), m_ignore_camera(false)
   {
      if (!data)
         throw std::bad_alloc();
   }

   Surface::Surface(Data *data)
      : data(blit_surface_data_ref(data)), alts(NULL), m_active_alt(NULL),
      m_active_alt_index(0), attribs(NULL),
      m_rect(blit_rect(blit_pos_zero(), data->w, data->h)),
      m_ignore_camera(false)
   {}

   static bool same_size_func(const vector<Surface::Alt>& alts, Pos size)
   {
      for( vector<Surface::Alt>::const_iterator alt = alts.begin(); alt!=alts.end(); alt++ )
         if(size != blit_pos(alt->data->w, alt->data->h))
            return false;
      return true;
   }

   Surface::Surface(const vector<Alt>& alts, const string& start_id)
      : data(NULL), alts(NULL), m_active_alt(NULL), m_active_alt_index(0),
      attribs(NULL), m_ignore_camera(false)
   {
      if (alts.empty())
         throw logic_error("Alts is empty.");

      Pos size = blit_pos(alts.front().data->w, alts.front().data->h);
      m_rect = blit_rect(blit_pos_zero(), size.x, size.y);

      bool same_size = same_size_func(alts,size);

      if (!same_size)
         throw logic_error("Not all alts are of same size.");

      if (!(this->alts = blit_alt_table_new()))
         throw std::bad_alloc();

      for( vector<Alt>::const_iterator alt = alts.begin(); alt!=alts.end(); alt++ )
         if (!blit_alt_table_add(this->alts, alt->tag.c_str(), alt->data))
            throw std::bad_alloc();

      active_alt(start_id);
   }

   void Surface::active_alt(const string& id, unsigned index)
   {
      size_t      have = blit_alt_table_count(alts, id.c_str());
      const char *tag;
      Data       *ptr;

      if (have <= index)
         throw logic_error(Utils::join("Subindex is out of bounds. Requested Alt: \"", id, "\" Index: ", index));

      ptr = blit_alt_table_at(alts, id.c_str(), index);
      tag = blit_alt_table_tag(alts, id.c_str());
      if (!ptr || !tag)
         throw logic_error(Utils::join("Alt ID ", id, " does not exist."));

      /* The tag belongs to the table, which this Surface keeps a
       * reference to for as long as it points at it. */
      m_active_alt       = tag;
      m_active_alt_index = index;

      /* The table holds its own reference; this takes a second one for
       * the active slot, released when the old one is dropped. */
      blit_surface_data_ref(ptr);
      blit_surface_data_unref(data);
      data = ptr;
   }

   void Surface::active_alt_index(unsigned index)
   {
      active_alt(m_active_alt ? m_active_alt : "", index);
   }

   Surface::Surface()
      : data(NULL), alts(NULL), m_active_alt(NULL), m_active_alt_index(0),
      attribs(NULL), m_rect(blit_rect_zero()), m_ignore_camera(false)
   {}

   Surface Surface::sub(Rect rect) const
   {
      RenderTarget target(rect.w, rect.h);
      Surface surf(*this);
      surf.rect().pos = -rect.pos;
      target.blit(surf, rect);
      return target.convert_surface();
   }

   Pixel Surface::pixel(Pos pos) const
   {
      pos -= m_rect.pos;
      int x = pos.x, y = pos.y;

      if (x >= data->w || y >= data->h)
         return 0;

      return data->pixels[y * data->w + x];
   }

   const Pixel* Surface::pixel_raw(Pos pos) const
   {
      pos -= m_rect.pos;
      int x = pos.x, y = pos.y;

      if (x >= data->w || y >= data->h || x < 0 || y < 0)
         throw logic_error(Utils::join(
                  "Pixel was fetched out-of-bounds. ",
                  "Asked for: (", x, ", ", y, "). ",
                  "Real dimension: (", data->w, ", ", data->h, ")."
                  ));

      return &data->pixels[y * data->w + x];
   }

   static Pixel* pixel_ptr = NULL;
   static Pixel transform_func(Pixel old)
   {
      return (old & BLIT_PIXEL_ALPHA_MASK) ? *pixel_ptr : (Pixel)0;
   }

   void Surface::refill_color(Pixel pixel)
   {
      size_t  count = (size_t)data->w * (size_t)data->h;
      Pixel  *pix   = (Pixel*)malloc((count ? count : 1) * sizeof(Pixel));
      Data   *fresh;
      size_t  i;

      if (!pix)
         throw std::bad_alloc();

      pixel_ptr = &pixel;
      for (i = 0; i < count; i++)
         pix[i] = transform_func(data->pixels[i]);

      fresh = blit_surface_data_new(pix, data->w, data->h);
      if (!fresh)
         throw std::bad_alloc();

      blit_surface_data_unref(data);
      data = fresh;
   }

   void Surface::set_attr(const char *key, const char *value)
   {
      if (!attribs)
      {
         if (!(attribs = blit_attr_table_new()))
            throw std::bad_alloc();
      }
      else if (blit_attr_table_shared(attribs))
      {
         blit_attr_table_t *fresh = blit_attr_table_clone(attribs);
         if (!fresh)
            throw std::bad_alloc();
         blit_attr_table_unref(attribs);
         attribs = fresh;
      }

      if (!blit_attr_table_set(attribs, key, value))
         throw std::bad_alloc();
   }

   void Surface::ignore_camera(bool ignore)
   {
      m_ignore_camera = ignore;
   }

   bool Surface::ignore_camera() const
   {
      return m_ignore_camera;
   }

}

