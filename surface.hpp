#ifndef SURFACE_HPP__
#define SURFACE_HPP__

#include "blit.hpp"
#include "blit_surface_data.h"
#include "blit_attr_table.h"
#include "blit_alt_table.h"
#include "blit_surface.h"
#include "blit_render_target.h"
#include "blit_surface_cluster.h"
#include "blit_str_map.h"

#include <memory>
#include <new>
#include <cstring>
#include <vector>
#include <map>
#include <functional>
#include <utility>

namespace Blit
{
   /* A sub-rectangle of @src as its own surface, rendered through a
    * scratch target. The caller owns the result. */
   blit_surface_t surface_sub(const blit_surface_t& src, Rect rect);


   /* A wrapper over blit_surface_cluster_t, so the engine's Renderable
    * dispatch still reaches it. The elements belong to the C struct;
    * this only forwards. */
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

   class SurfaceCache
   {
      public:
         SurfaceCache();
         ~SurfaceCache();

         /* Each hands back a surface the caller owns and must release. */
         blit_surface_t from_image(const std::string& path);
         blit_surface_t from_sprite(const std::string& path);
         /* One frame of an APNG as a standalone surface; the whole
          * animation is decoded and cached on the first request. */
         blit_surface_t from_animation(const std::string& path, unsigned frame);

      private:
         /* Session-long, never evicted: one reference per entry, all
          * released in the destructor. */
         blit_str_map_t *cache;
         /* from_sprite's XML parse, kept alongside the pixel cache: the
          * faces it names were already shared, but the .sprite document
          * behind them was re-read and re-parsed on every call - 50 times
          * each for dino.sprite and frozen.sprite over a session. */
         /* A parsed .sprite: its face table and the face it starts on.
          * The cache owns one reference on the table and the string. */
         struct SpriteDef
         {
            blit_alt_table_t *alts;
            char             *start_id;
         };
         blit_str_map_t *sprites;
         blit_surface_data_t *load_image(const std::string& path);
         /* Face inside an APNG: decodes the whole animation on the first
          * request for any of its frames and fills one cache entry per
          * frame under "path#N" keys, so the file is read and decoded
          * once however many faces reference it. */
         blit_surface_data_t *load_apng_frame(
               const std::string& path, unsigned frame);
   };

   /* One cache for the whole session.
    *
    * Every user of this used to hold its own: a local inside the tileset
    * and glyph loaders (destroyed at the end of the function, so those
    * cached nothing at all), and a member on each Game, which is
    * constructed per level.  The tilesets and the dino sprites are shared
    * across levels, so they were re-read and re-decoded once per level -
    * about 2100 file opens over a session where 57 files exist.
    *
    * Sharing is safe because blit_surface_data_t is immutable once cached:
    * Surface::pixel_raw is a const accessor, and refill_color swaps in
    * a fresh Data rather than writing through the shared one.
    * The only mutable pixel buffer is RenderTarget's, which is its own.
    * All loading happens on the thread that calls retro_load_game and
    * retro_run - the core's only worker threads decode audio. */
   SurfaceCache& surface_cache();

}

#endif

