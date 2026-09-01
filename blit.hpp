#ifndef BLIT_HPP__
#define BLIT_HPP__

#include "utils.hpp"
#include "blit_pixel.h"
#include <algorithm>
#include <stdint.h>
#include <vector>

#include <climits>

#if defined(__SSE2__) && defined(USE_SIMD)
#include <emmintrin.h>
#endif

namespace Blit
{
   /* The pixel format and its operations live in blit_pixel.h as plain
    * C. Pixel stays as the name the engine uses for it. */
   typedef blit_pixel_t Pixel;

   struct Pos
   {
      Pos() : x(0), y(0) {}
      Pos(int x, int y) : x(x), y(y) {}

      Pos& operator+=(Pos pos)       { x += pos.x; y += pos.y; return *this; }
      Pos& operator-=(Pos pos)       { x -= pos.x; y -= pos.y; return *this; }
      Pos& operator*=(Pos pos)       { x *= pos.x; y *= pos.y; return *this; }
      Pos& operator/=(int div)       { x /= div; y /= div; return *this; }
      Pos  operator+ (Pos pos) const { return Pos( x + pos.x, y + pos.y ); }
      Pos  operator- (Pos pos) const { return Pos( x - pos.x, y - pos.y ); }
      Pos  operator* (Pos pos) const { return Pos( x * pos.x, y * pos.y ); }
      Pos  operator/ (int div) const { return Pos( x / div, y / div ); }
      bool operator==(Pos pos) const { return x == pos.x && y == pos.y; }
      bool operator!=(Pos pos) const { return !(*this == pos); }

      // Allows Pos to be placed in binary trees.
      bool operator<(Pos pos) const
      {
         //static_assert(CHAR_BIT * sizeof(int) == 32, "int is not 32-bit. This algorithm will fail.");
         uint64_t self = static_cast<uint32_t>(x);
         self <<= 32;
         self |= static_cast<uint32_t>(y);

         uint64_t other = static_cast<uint32_t>(pos.x);
         other <<= 32;
         other |= static_cast<uint32_t>(pos.y);

         return self < other;
      }

      int x, y;
   };

   inline Pos operator-(Pos pos)
   {
      return Pos(-pos.x, -pos.y);
   }

   inline Pos operator*(int scale, Pos pos)
   {
      return Pos(scale * pos.x, scale * pos.y);
   }

   inline std::ostream& operator<<(std::ostream& ostr, Pos pos)
   {
      ostr << "[ " << pos.x << ", " << pos.y << " ]";
      return ostr;
   }

   struct Rect
   {
      Rect() : w(0), h(0) {}
      Rect(Pos pos, int w, int h) : pos(pos), w(w), h(h) {}
      Rect(int w, int h) : w(w), h(h) {}

      Rect& operator+=(Pos pos)       { this->pos += pos; return *this; }
      Rect& operator-=(Pos pos)       { this->pos -= pos; return *this; }
      Rect  operator+ (Pos pos) const { return Rect( this->pos + pos, w, h ); }
      Rect  operator- (Pos pos) const { return Rect( this->pos - pos, w, h ); }

      // Intersection
      Rect  operator&(Rect rect) const
      {
         int x_left  = std::max(pos.x, rect.pos.x);
         int x_right = std::min(pos.x + w, rect.pos.x + rect.w);
         int width   = x_right - x_left;

         int y_top    = std::max(pos.y, rect.pos.y);
         int y_bottom = std::min(pos.y + h, rect.pos.y + rect.h);
         int height   = y_bottom - y_top;

         if (width <= 0 || height <= 0)
            return Rect(Pos(0, 0), 0, 0);
         else
            return Rect(Pos(x_left, y_top), width, height);
      }

      Rect& operator&=(Rect rect)
      {
         *this = operator&(rect);
         return *this;
      }

      operator bool() const { return w > 0 && h > 0; }

      Pos pos;
      int w, h;
   };
}

#endif

