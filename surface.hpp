#ifndef SURFACE_HPP__
#define SURFACE_HPP__

#include "blit.hpp"
#include "blit_surface_data.h"
#include "blit_attr_table.h"
#include "blit_alt_table.h"
#include "blit_surface.h"

#include <memory>
#include <new>
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

         /* Borrowed: the session cache owns a reference to every face
          * for as long as it lives, and a Surface built from a set of
          * alts takes its own. */
         struct Alt
         {
            Data *data;
            std::string tag;
         };

         Surface() { blit_surface_init(&s); }

         Surface(Pixel pix, int width, int height)
         {
            if (!blit_surface_init_filled(&s, pix, width, height))
               throw std::bad_alloc();
         }

         /* Takes its own reference; the caller keeps theirs. */
         Surface(Data *data) { blit_surface_init_data(&s, data); }

         Surface(const std::vector<Alt>& alts, const std::string& start_id);

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

      private:
         /* Always throws; never returns. */
         void pixel_raw_out_of_bounds(Pos pos) const;

         blit_surface_t s;
   };

   class RenderTarget;

   class Renderable
   {
      public:
         /* Pos is a C aggregate with no constructor, so members of this
          * type have to be given their zero explicitly. */
         Renderable() : position(blit_pos_zero()) {}

         /* Polymorphic base: Level and SurfaceCluster derive from it and are
          * held in containers that destroy through this type. */
         virtual ~Renderable() {}

         virtual void render(RenderTarget& target) const = 0;
         virtual Pos pos() const { return position; }
         virtual void pos(Pos position) { this->position = position; }

         void move(Pos offset) { pos(pos() + offset); }

      protected:
         Pos position;
   };

   class SurfaceCluster : public Renderable
   {
      public:
         struct Elem
         {
            Surface surf;
            Pos offset;
            unsigned tag;
         };

         SurfaceCluster()
         {
         }

         std::vector<Elem>& vec();
         const std::vector<Elem>& vec() const;

         void set_transform(std::function<Pos (Pos)> func);
         void render(RenderTarget& target) const;

      private:
         std::vector<Elem> elems;
         std::function<Pos (Pos)> func;
   };

   class SurfaceCache
   {
      public:
         ~SurfaceCache();

         Surface from_image(const std::string& path);
         Surface from_sprite(const std::string& path);
         /* One frame of an APNG as a standalone surface; the whole
          * animation is decoded and cached on the first request. */
         Surface from_animation(const std::string& path, unsigned frame);

      private:
         /* Session-long, never evicted: one reference per entry, all
          * released in the destructor. */
         std::map<std::string, Surface::Data*> cache;
         /* from_sprite's XML parse, kept alongside the pixel cache: the
          * faces it names were already shared, but the .sprite document
          * behind them was re-read and re-parsed on every call - 50 times
          * each for dino.sprite and frozen.sprite over a session. */
         struct SpriteDef
         {
            std::vector<Surface::Alt> alts;
            std::string start_id;
         };
         std::map<std::string, SpriteDef> sprites;
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

   class RenderTarget
   {
      public:
         RenderTarget() : rect(blit_rect_zero())
         {
         }

         RenderTarget(int width, int height);

         Surface convert_surface();

         const Pixel* buffer() const;
         Pixel* pixel_raw(Pos pos);
         Pixel* pixel_raw_no_offset(Pos pos);

         int width() const;
         int height() const;

         void clear(Pixel pix);

         void camera_move(Pos pos);
         void camera_set(Pos pos);
         Pos camera_pos() const;

         void blit(const Surface& surf, Rect subrect);
         void blit_offset(const Surface& surf, Rect subrect, Pos offset);

      private:
         std::vector<Pixel> m_buffer;
         Rect rect;
   };
}

#endif

