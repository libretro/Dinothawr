#include "surface.hpp"
#include <new>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Blit
{
   RenderTarget::RenderTarget(int width, int height)
      : m_buffer(width * height), rect(blit_rect(blit_pos_zero(), width, height))
   {}

   const Pixel* RenderTarget::buffer() const
   {
      return &m_buffer.front();
   }

   void RenderTarget::clear(Pixel pix)
   {
      std::fill(m_buffer.begin(), m_buffer.end(), pix);
   }

   Surface RenderTarget::convert_surface()
   {
      int width = rect.w, height = rect.h;
      size_t count = m_buffer.size();
      Pixel *pix;
      Surface::Data *data;

      rect = blit_rect_zero();

      pix = (Pixel*)malloc((count ? count : 1) * sizeof(Pixel));
      if (!pix)
         throw std::bad_alloc();
      if (count)
         memcpy(pix, &m_buffer[0], count * sizeof(Pixel));
      m_buffer.clear();

      if (!(data = blit_surface_data_new(pix, width, height)))
         throw std::bad_alloc();

      {
         /* Surface takes its own reference. */
         Surface out(data);
         blit_surface_data_unref(data);
         return out;
      }
   }

   int RenderTarget::width() const
   {
      return rect.w;
   }

   int RenderTarget::height() const
   {
      return rect.h;
   }

   Pos RenderTarget::camera_pos() const
   {
      return rect.pos;
   }

   void RenderTarget::camera_move(Pos pos)
   {
      rect.pos += pos;
   }

   void RenderTarget::camera_set(Pos pos)
   {
      rect.pos = pos;
   }

   void RenderTarget::blit(const Surface& surf, Rect subrect)
   {
      blit_offset(surf, subrect, blit_pos(0, 0));
   }

   void RenderTarget::blit_offset(const Surface& surf_, Rect subrect, Pos pos)
   {
      const Surface& surf = surf_;

      Rect surf_rect = blit_rect_offset(surf.rect(), pos);
      Rect dest_rect = rect;

      bool ignore_camera = surf.ignore_camera();
      if (ignore_camera)
         dest_rect.pos = blit_pos_zero();

      /* 'clip' rather than 'blit_rect': that is the name of the C
       * constructor now. */
      Rect clip = surf_rect & dest_rect;

      if (blit_rect_valid(subrect))
      {
         subrect += surf.rect().pos;
         clip &= subrect;
      }

      if (!blit_rect_valid(clip))
         return;

      const Pixel* src_data = surf.pixel_raw(blit_pos_sub(clip.pos, pos));
      Pixel* dst_data = ignore_camera ?
         pixel_raw_no_offset(clip.pos) : pixel_raw(clip.pos);

      /* Strides in locals, not re-read from the members each row. The
       * blit stores through a blit_pixel_t*, which is unsigned int and
       * so may alias the int members it would otherwise be reloading;
       * the compiler could hoist them when the store went through a
       * distinct struct type, and cannot now. */
      {
         const int    src_stride = surf_rect.w;
         const int    dst_stride = rect.w;
         const size_t run        = (size_t)clip.w;
         int y;

         for (y = 0; y < clip.h; y++,
               src_data += src_stride, dst_data += dst_stride)
            blit_pixel_set_line_if_alpha(dst_data, src_data, run);
      }
   }

   Pixel* RenderTarget::pixel_raw_no_offset(Pos pos)
   {
      int x = pos.x, y = pos.y;

      if (x >= rect.w || y >= rect.h || x < 0 || y < 0)
         throw std::logic_error(Utils::join(
                  "Pixel was fetched out-of-bounds. ",
                  "Asked for: (", x, ", ", y, "). ",
                  "Real dimension: (", rect.w, ", ", rect.h, ")."
                  ));

      return &m_buffer[y * rect.w + x];
   }

   Pixel* RenderTarget::pixel_raw(Pos pos)
   {
      return pixel_raw_no_offset(pos - rect.pos);
   }
}

