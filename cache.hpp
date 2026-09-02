#ifndef CACHE_HPP__
#define CACHE_HPP__

/* The C++ side of the surface cache: three wrappers that raise what the
 * C entry points report by return value. Every caller here treats a
 * missing asset as fatal, so raising is what they all want. */

#include <stdexcept>
#include <string>

#include "blit_surface_cache.h"

namespace Blit
{
   /* Thin wrappers over blit_surface_cache.h: the C side reports
    * failure by return value, and these raise it, since every caller
    * here treats a missing asset as fatal. Each hands back a surface the
    * caller owns and must release. */
   inline blit_surface_t cache_image(const std::string& path)
   {
      blit_surface_t out;
      if (!blit_surface_cache_image(blit_surface_cache(), path.c_str(), &out))
         throw std::runtime_error(
               blit_surface_cache_error(blit_surface_cache()));
      return out;
   }

   inline blit_surface_t cache_animation(const std::string& path,
         unsigned frame)
   {
      blit_surface_t out;
      if (!blit_surface_cache_animation(blit_surface_cache(), path.c_str(),
               frame, &out))
         throw std::runtime_error(
               blit_surface_cache_error(blit_surface_cache()));
      return out;
   }

   inline blit_surface_t cache_sprite(const std::string& path)
   {
      blit_surface_t out;
      if (!blit_surface_cache_sprite(blit_surface_cache(), path.c_str(), &out))
         throw std::runtime_error(
               blit_surface_cache_error(blit_surface_cache()));
      return out;
   }

}

#endif

