#ifndef BLIT_HPP__
#define BLIT_HPP__

#include "blit_pixel.h"
#include "blit_geom.h"
#include <stdint.h>

namespace Blit
{
   /* The pixel format and its operations live in blit_pixel.h as plain
    * C. Pixel stays as the name the engine uses for it. */
   typedef blit_pixel_t Pixel;

   typedef blit_pos_t  Pos;
   typedef blit_rect_t Rect;

}

#endif

