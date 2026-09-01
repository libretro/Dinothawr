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
   /* A value-semantics shim over blit_surface_t. It holds one and does
    * the retain and release the C struct expects around copies, so the
    * engine code that is still C++ can keep putting Surfaces in
    * std::map and std::vector. Everything it does is a forward to
    * blit_surface.h; when the containers go, so does this. */
   class Surface
   {
      public:
         typedef blit_surface_data_t Data;

         Surface() { blit_surface_init(&s); }

         Surface(Pixel pix, int width, int height)
         {
            if (!blit_surface_init_filled(&s, pix, width, height))
               throw std::bad_alloc();
         }

         /* Takes its own reference; the caller keeps theirs. */
         Surface(Data *data) { blit_surface_init_data(&s, data); }

         /* Shows @start_id from @alts, taking its own reference on the
          * table. */
         Surface(blit_alt_table_t *alts, const char *start_id);

         Surface(const Surface& other) : s(other.s)
         { blit_surface_retain(&s); }

         Surface& operator=(const Surface& other)
         {
            if (this != &other)
            {
               /* Retain before release: an assignment from a Surface
                * sharing our tables would otherwise free them first. */
               blit_surface_retain(&other.s);
               blit_surface_release(&s);
               s = other.s;
            }
            return *this;
         }

         /* Declaring the copy operations suppresses the implicit move
          * ones, and this type is moved a lot: SurfaceCluster keeps its
          * elements in a vector it sorts every frame. */
         Surface(Surface&& other) : s(other.s)
         { blit_surface_init(&other.s); }

         Surface& operator=(Surface&& other)
         {
            if (this != &other)
            {
               blit_surface_release(&s);
               s = other.s;
               blit_surface_init(&other.s);
            }
            return *this;
         }

         ~Surface() { blit_surface_release(&s); }

         Surface sub(Rect rect) const;
         void refill_color(Pixel pix)
         {
            if (!blit_surface_refill_color(&s, pix))
               throw std::bad_alloc();
         }

         Rect& rect() { return s.rect; }
         const Rect& rect() const { return s.rect; }

         void ignore_camera(bool ignore) { s.ignore_camera = ignore; }
         bool ignore_camera() const { return s.ignore_camera != 0; }

         Pixel pixel(Pos pos) const { return blit_surface_pixel(&s, pos); }

         const Pixel* pixel_raw(Pos pos) const
         {
            const Pixel *pixel = blit_surface_pixel_raw(&s, pos);
            if (!pixel)
               pixel_raw_out_of_bounds(pos);
            return pixel;
         }

         void active_alt(const std::string& id, unsigned index = 0);
         void active_alt_index(unsigned index)
         { active_alt(s.active_alt ? s.active_alt : "", index); }

         /* The value for @key, or NULL when the tile has no such
          * attribute. */
         const char *attr(const char *key) const
         { return blit_attr_table_find(s.attribs, key); }

         /* Copy-on-write: a shared table is cloned before the write, so
          * one tile's attributes never reach another's view. */
         void set_attr(const char *key, const char *value)
         {
            if (!blit_surface_set_attr(&s, key, value))
               throw std::bad_alloc();
         }

         const blit_attr_table_t *attr_table() const { return s.attribs; }

         const blit_surface_t& raw() const { return s; }
         blit_surface_t& raw() { return s; }

      private:
         /* Always throws; never returns. */
         void pixel_raw_out_of_bounds(Pos pos) const;

         blit_surface_t s;
   };

   class RenderTarget;

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

         void add(const Surface& surf, Pos offset)
         { add(&surf.raw(), offset); }

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

         void render(RenderTarget& target) const;

      private:
         Pos position;
         blit_surface_cluster_t c;
   };

   class SurfaceCache
   {
      public:
         SurfaceCache();
         ~SurfaceCache();

         Surface from_image(const std::string& path);
         Surface from_sprite(const std::string& path);
         /* One frame of an APNG as a standalone surface; the whole
          * animation is decoded and cached on the first request. */
         Surface from_animation(const std::string& path, unsigned frame);

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
         Surface::Data *load_image(const std::string& path);
         /* Face inside an APNG: decodes the whole animation on the first
          * request for any of its frames and fills one cache entry per
          * frame under "path#N" keys, so the file is read and decoded
          * once however many faces reference it. */
         Surface::Data *load_apng_frame(
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
    * Sharing is safe because Surface::Data is immutable once cached:
    * Surface::pixel_raw is a const accessor, and refill_color swaps in
    * a fresh Data rather than writing through the shared one.
    * The only mutable pixel buffer is RenderTarget's, which is its own.
    * All loading happens on the thread that calls retro_load_game and
    * retro_run - the core's only worker threads decode audio. */
   SurfaceCache& surface_cache();

   /* A value-semantics shim over blit_render_target_t, for the engine
    * code that still holds one by value. Everything forwards. */
   class RenderTarget
   {
      public:
         RenderTarget() { blit_render_target_init(&t); }

         RenderTarget(int width, int height)
         {
            if (!blit_render_target_init_size(&t, width, height))
               throw std::bad_alloc();
         }

         /* A target owns its pixels outright, so a copy is a deep copy.
          * Only the move is on any hot path - ui_target is assigned from
          * a temporary when a game loads. */
         RenderTarget(const RenderTarget& other)
         {
            if (!blit_render_target_init_size(&t, other.t.rect.w,
                     other.t.rect.h))
               throw std::bad_alloc();
            if (other.t.buffer)
               std::memcpy(t.buffer, other.t.buffer,
                     other.t.count * sizeof(Pixel));
            t.rect = other.t.rect;
         }

         RenderTarget& operator=(const RenderTarget& other)
         {
            if (this != &other)
            {
               RenderTarget copy(other);
               *this = std::move(copy);
            }
            return *this;
         }

         RenderTarget(RenderTarget&& other) : t(other.t)
         { blit_render_target_init(&other.t); }

         RenderTarget& operator=(RenderTarget&& other)
         {
            if (this != &other)
            {
               blit_render_target_release(&t);
               t = other.t;
               blit_render_target_init(&other.t);
            }
            return *this;
         }

         ~RenderTarget() { blit_render_target_release(&t); }

         Surface convert_surface();

         const Pixel* buffer() const { return t.buffer; }

         Pixel* pixel_raw(Pos pos)
         {
            Pixel *pixel = blit_render_target_pixel_raw(&t, pos);
            if (!pixel)
               pixel_out_of_bounds(blit_pos_sub(pos, t.rect.pos));
            return pixel;
         }

         Pixel* pixel_raw_no_offset(Pos pos)
         {
            Pixel *pixel = blit_render_target_pixel_raw_no_offset(&t, pos);
            if (!pixel)
               pixel_out_of_bounds(pos);
            return pixel;
         }

         int width() const { return t.rect.w; }
         int height() const { return t.rect.h; }

         void clear(Pixel pix) { blit_render_target_clear(&t, pix); }

         void camera_move(Pos pos) { t.rect.pos += pos; }
         void camera_set(Pos pos) { t.rect.pos = pos; }
         Pos camera_pos() const { return t.rect.pos; }

         void blit(const Surface& surf, Rect subrect)
         { blit_render_target_blit(&t, &surf.raw(), subrect); }

         void blit_offset(const Surface& surf, Rect subrect, Pos offset)
         { blit_render_target_blit_offset(&t, &surf.raw(), subrect, offset); }

         blit_render_target_t& raw() { return t; }

      private:
         /* Always throws; never returns. */
         void pixel_out_of_bounds(Pos pos) const;

         blit_render_target_t t;
   };
}

#endif

