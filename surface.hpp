#ifndef SURFACE_HPP__
#define SURFACE_HPP__

#include "blit.hpp"
#include "blit_surface_data.h"
#include "blit_attr_table.h"

#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <utility>

namespace Blit
{
   class Surface
   {
      public:
         /* The pixels are blit_surface_data_t now; Data stays as the
          * name the rest of the engine spells it. */
         typedef blit_surface_data_t Data;

         /* Borrowed: the session cache owns a reference to every face
          * for as long as it lives, and a Surface built from a set of
          * alts takes its own. */
         struct Alt
         {
            Data *data;
            std::string tag;
         };

         Surface();
         Surface(Pixel pix, int width, int height);
         /* Takes its own reference; the caller keeps theirs. */
         Surface(Data *data);
         Surface(const std::vector<Alt>& alts, const std::string& start_id);

         /* Explicit rule of three: the references are counted by hand
          * now rather than by shared_ptr. Defined here rather than in
          * the .cpp so they still inline into the blit path, which
          * copies a Surface per call. */
         Surface(const Surface& other)
            : data(other.data), alts(other.alts),
            m_active_alt(other.m_active_alt),
            m_active_alt_index(other.m_active_alt_index),
            attribs(other.attribs), m_rect(other.m_rect),
            m_ignore_camera(other.m_ignore_camera)
         {
            retain_all();
         }

         Surface& operator=(const Surface& other)
         {
            if (this != &other)
            {
               /* Retain before release: an assignment from a Surface
                * sharing our data would otherwise free it first. */
               other.retain_all();
               release_all();

               data               = other.data;
               alts               = other.alts;
               m_active_alt       = other.m_active_alt;
               m_active_alt_index = other.m_active_alt_index;
               attribs            = other.attribs;
               m_rect             = other.m_rect;
               m_ignore_camera    = other.m_ignore_camera;
            }
            return *this;
         }

         /* Declaring the copy operations suppresses the implicit move
          * ones, and this type is moved a lot: SurfaceCluster keeps its
          * elements in a vector it sorts every frame. Without these,
          * that sort copies - refcount traffic on every swap, and the
          * strings and maps duplicated rather than stolen. */
         Surface(Surface&& other)
            : data(other.data), alts(std::move(other.alts)),
            m_active_alt(std::move(other.m_active_alt)),
            m_active_alt_index(other.m_active_alt_index),
            attribs(other.attribs), m_rect(other.m_rect),
            m_ignore_camera(other.m_ignore_camera)
         {
            /* References move with the members; the source keeps none. */
            other.data    = NULL;
            other.attribs = NULL;
            other.alts.clear();
         }

         Surface& operator=(Surface&& other)
         {
            if (this != &other)
            {
               release_all();

               data               = other.data;
               alts               = std::move(other.alts);
               m_active_alt       = std::move(other.m_active_alt);
               m_active_alt_index = other.m_active_alt_index;
               attribs            = other.attribs;
               m_rect             = other.m_rect;
               m_ignore_camera    = other.m_ignore_camera;

               other.data    = NULL;
               other.attribs = NULL;
               other.alts.clear();
            }
            return *this;
         }

         ~Surface() { release_all(); }

         Surface sub(Rect rect) const;
         void refill_color(Pixel pix);

         Rect& rect() { return m_rect; }
         const Rect& rect() const { return m_rect; }

         void ignore_camera(bool ignore);
         bool ignore_camera() const;

         Pixel pixel(Pos pos) const;
         const Pixel* pixel_raw(Pos pos) const;

         std::pair<std::string, unsigned> active_alt() const { return std::pair<std::string, unsigned>(m_active_alt, m_active_alt_index); }
         void active_alt(const std::string& id, unsigned index = 0);
         void active_alt_index(unsigned index);

         /* The value for @key, or NULL when the tile has no such
          * attribute. */
         const char *attr(const char *key) const
         { return blit_attr_table_find(attribs, key); }

         /* Copy-on-write: a shared table is cloned before the write, so
          * one tile's attributes never reach another's view. */
         void set_attr(const char *key, const char *value);

         const blit_attr_table_t *attr_table() const { return attribs; }

      private:
         /* One reference held here, and one for every entry in alts. */
         Data *data;

         std::multimap<std::string, Data*> alts;

         /* One reference for 'data' and one per entry in 'alts'. */
         void retain_all() const
         {
            std::multimap<std::string, Data*>::const_iterator it;
            blit_surface_data_ref(data);
            blit_attr_table_ref(attribs);
            for (it = alts.begin(); it != alts.end(); ++it)
               blit_surface_data_ref(it->second);
         }

         void release_all()
         {
            std::multimap<std::string, Data*>::iterator it;
            for (it = alts.begin(); it != alts.end(); ++it)
               blit_surface_data_unref(it->second);
            blit_attr_table_unref(attribs);
            blit_surface_data_unref(data);
         }
         std::string m_active_alt;
         unsigned m_active_alt_index;

         /* Shared and reference counted, NULL meaning empty - see
          * blit_attr_table.h. This is what keeps the per-blit Surface
          * copy cheap. */
         blit_attr_table_t *attribs;
         Rect m_rect;
         bool m_ignore_camera;
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

