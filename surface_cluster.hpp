#ifndef SURFACE_CLUSTER_HPP__
#define SURFACE_CLUSTER_HPP__

#include "blit.hpp"
#include "blit_surface_data.h"
#include "blit_attr_table.h"
#include "blit_alt_table.h"
#include "blit_surface.h"
#include "blit_render_target.h"
#include "blit_surface_cluster.h"
#include "blit_surface_cache.h"

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace Blit
{
   /* A sub-rectangle of @src as its own surface, rendered through a
    * scratch target. The caller owns the result. */
   blit_surface_t surface_sub(const blit_surface_t& src, Rect rect);


   /* A wrapper over blit_surface_cluster_t. The elements belong to the
    * C struct; this adds the value semantics Tilemap's layers need and
    * forwards everything else. */
   class SurfaceCluster
   {
      public:
         typedef blit_cluster_elem_t Elem;

         SurfaceCluster() : position(blit_pos_zero())
         { blit_surface_cluster_init(&c); }

         Pos pos() const { return position; }
         void pos(Pos p) { position = p; }
         void move(Pos offset) { position += offset; }

         SurfaceCluster(const SurfaceCluster& other)
         {
            blit_surface_cluster_init(&c);
            *this = other;
         }

         SurfaceCluster& operator=(const SurfaceCluster& other)
         {
            if (this != &other)
            {
               size_t i;

               blit_surface_cluster_release(&c);
               position = other.position;

               for (i = 0; i < other.c.count; i++)
                  if (!blit_surface_cluster_add(&c, &other.c.elems[i].surf,
                           other.c.elems[i].offset))
                     throw std::bad_alloc();
            }
            return *this;
         }

         SurfaceCluster(SurfaceCluster&& other) : c(other.c)
         {
            position = other.position;
            blit_surface_cluster_init(&other.c);
         }

         SurfaceCluster& operator=(SurfaceCluster&& other)
         {
            if (this != &other)
            {
               blit_surface_cluster_release(&c);
               c        = other.c;
               position = other.position;
               blit_surface_cluster_init(&other.c);
            }
            return *this;
         }

         ~SurfaceCluster() { blit_surface_cluster_release(&c); }

         void add(const blit_surface_t *surf, Pos offset)
         {
            if (!blit_surface_cluster_add(&c, surf, offset))
               throw std::bad_alloc();
         }

         size_t size() const { return c.count; }
         Elem* at(size_t i) { return &c.elems[i]; }
         const Elem* at(size_t i) const { return &c.elems[i]; }

         /* The element whose surface sits at @offset, or NULL. */
         Elem* find(Pos offset) { return blit_surface_cluster_find(&c, offset); }
         const Elem* find(Pos offset) const
         { return blit_surface_cluster_find(&c, offset); }

         void render(blit_render_target_t& target) const;

      private:
         Pos position;
         blit_surface_cluster_t c;
   };

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

